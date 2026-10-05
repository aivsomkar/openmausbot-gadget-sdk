#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Talk to a gadget's USB console without a terminal: send lines, print @omb lines.

For coding agents and scripts (AGENTS.md). Needs pyserial, which esptool and
ESP-IDF install. The helper first waits until the gadget's app answers
`status` (a board that was just flashed is still booting), then sends every
LINE argument, then --pair, --wifi and --host (the browser installer's order).

  omb_console.py --port /dev/cu.usbmodem1101 "log off" status
  omb_console.py --port /dev/cu.usbmodem1101 scan --wait 10
  omb_console.py --port /dev/cu.usbmodem1101 "log off" --pair 123456 \\
      --wifi "Home Net" "secret pass" --host auto --until-paired --wait 150
"""
import argparse
import json
import re
import sys
import time

ANSI_RE = re.compile(r"\x1b\[[0-9;?]*[ -/]*[@-~]")
HOST_RE = re.compile(r"[A-Za-z0-9.-]+(:[0-9]{1,5})?")

EXIT_PAIRED = 0
EXIT_FAILED = 1  # a wrong code, or not paired within --wait
EXIT_NEED_HOST = 3  # MausBot not found (or several): ask for the address shown under Pair a gadget
EXIT_WIFI_FAILED = 4  # the gadget can't join the network: wrong Wi-Fi password
EXIT_NO_APP = 5  # the app never answered: press RST or unplug and replug the board

# A status left over from the previous attempt (spec §4.3 keeps pair=error
# after bad_code until the next pair) is not a result until that side shows
# progress or this many status replies have passed.
STALE_POLLS = 3
PAIR_PROGRESS = ("code_stored", "connecting", "paired")
WIFI_PROGRESS = ("connecting", "connected")

EXIT_CODES = """exit codes:
  0  done (with --until-paired: the gadget reported "pair": "paired")
  1  with --until-paired: a wrong code, or not paired within --wait
  2  bad arguments
  3  with --until-paired: MausBot not found (or several): ask for the address
     shown under Pair a gadget, rerun with --host <address> (and a new code if
     more than two minutes have passed)
  4  with --until-paired: the gadget can't join the network (wrong password)
  5  the gadget's app didn't answer: press RST or unplug and replug the board"""


def parse_omb_line(line):
    """The JSON object of an @omb line, or None for any other line."""
    clean = ANSI_RE.sub("", line).rstrip("\r")
    if not clean.startswith("@omb "):
        return None
    try:
        msg = json.loads(clean[5:])
    except ValueError:
        return None
    return msg if isinstance(msg, dict) and isinstance(msg.get("op"), str) else None


def quote_arg(value):
    """One console argument: "..." with \\\\ and \\" escapes (esp_console_split_argv rules)."""
    if "\r" in value or "\n" in value:
        raise ValueError("console arguments cannot contain line breaks")
    return '"' + value.replace("\\", "\\\\").replace('"', '\\"') + '"'


class LineSplitter:
    """Feed text chunks, get complete lines; CR, LF and CRLF each end one line."""

    def __init__(self):
        self.buf = ""
        self.last_cr = False

    def feed(self, text):
        lines = []
        for ch in text:
            if ch == "\n" and self.last_cr:
                self.last_cr = False
                continue
            self.last_cr = ch == "\r"
            if ch in "\r\n":
                lines.append(self.buf)
                self.buf = ""
            else:
                self.buf += ch
        return lines


def build_lines(args):
    lines = list(args.lines)
    if args.pair is not None:
        if not re.fullmatch(r"\d{6}", args.pair):
            raise ValueError("--pair needs the six-digit code from MausBot → Settings → Remote access → Pair a gadget")
        lines.append("pair " + args.pair)
    if args.wifi is not None:
        lines.append("wifi " + quote_arg(args.wifi[0]) + " " + quote_arg(args.wifi[1]))
    if args.host is not None:
        lines.append("host auto" if args.host == "auto" else "host " + args.host)
    return lines


def wait_for_app(port, clock, timeout=10.0, every=0.5):
    """Send `status` every `every` s until any @omb line comes back; False after `timeout` s.
    The probe's replies are consumed, not printed."""
    splitter = LineSplitter()
    deadline = clock() + timeout
    next_probe = clock()
    while clock() < deadline:
        if clock() >= next_probe:
            port.write(b"status\n")
            next_probe = clock() + every
        for line in splitter.feed(port.read(4096).decode("utf-8", "replace")):
            if parse_omb_line(line) is not None:
                return True
    return False


def run(port, lines, wait, until_paired, out=sys.stdout, err=sys.stderr, clock=time.monotonic):
    """Wait for the app, send lines, print @omb lines until `wait` seconds pass.
    With until_paired, poll status every second and return one of the EXIT_* codes."""
    if not wait_for_app(port, clock):
        print("the gadget's app didn't answer: press RST or unplug and replug the board", file=err)
        return EXIT_NO_APP
    splitter = LineSplitter()
    for line in lines:
        port.write((line + "\n").encode("utf-8"))
    deadline = clock() + wait
    next_poll = clock()
    polls = 0
    pair_moved = wifi_moved = warned_limit = False
    while clock() < deadline:
        if until_paired and clock() >= next_poll:
            port.write(b"status\n")
            next_poll = clock() + 1.0
        data = port.read(4096)
        for line in splitter.feed(data.decode("utf-8", "replace")):
            msg = parse_omb_line(line)
            if msg is None:
                continue
            print(json.dumps(msg, ensure_ascii=False), file=out, flush=True)
            if not until_paired:
                continue
            if msg.get("op") == "hosts":
                hosts = msg.get("hosts")
                only = hosts[0] if isinstance(hosts, list) and len(hosts) == 1 and isinstance(hosts[0], dict) else None
                address = only.get("address") if only is not None else None
                if isinstance(address, str) and HOST_RE.fullmatch(address):
                    port.write(("host " + address + "\n").encode("utf-8"))  # one MausBot: use it, like the installer
                    continue
                return EXIT_NEED_HOST
            if msg.get("op") != "status":
                continue
            polls += 1
            pair_moved = pair_moved or msg.get("pair") in PAIR_PROGRESS
            wifi_moved = wifi_moved or msg.get("wifi") in WIFI_PROGRESS
            settled = polls > STALE_POLLS
            if msg.get("pair") == "paired":
                return EXIT_PAIRED
            if msg.get("pair") == "error" and msg.get("error") == "device_limit":
                if not warned_limit:
                    print("MausBot has too many devices: remove one in MausBot → Settings → Remote access; "
                          "the gadget keeps trying for two minutes", file=err)
                warned_limit = True
                continue
            if msg.get("pair") == "error" and (pair_moved or settled):
                return EXIT_FAILED
            if msg.get("wifi") == "failed" and (wifi_moved or settled):
                return EXIT_WIFI_FAILED
    return EXIT_FAILED if until_paired else 0


def open_port(path):
    import serial  # pyserial

    port = serial.Serial(path, 115200, timeout=0.1)
    # The OS raises DTR and RTS when the port opens. DTR low with RTS high is the
    # USB-Serial-JTAG reset state, so drop RTS first, then DTR:
    # (DTR, RTS) goes (1,1) → (1,0) → (0,0) and never passes (0,1).
    port.rts = False
    port.dtr = False
    return port


def main(argv=None):
    p = argparse.ArgumentParser(
        description="Send console lines to a gadget and print its @omb lines.",
        epilog=EXIT_CODES,
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    p.add_argument("--port", required=True, help="serial port, e.g. /dev/cu.usbmodem1101, /dev/ttyACM0 or COM5")
    p.add_argument("--wait", type=float, default=3.0, help="seconds to listen after sending (default 3)")
    p.add_argument("--pair", metavar="CODE", help="six-digit code from Pair a gadget")
    p.add_argument("--wifi", nargs=2, metavar=("SSID", "PASSWORD"), help='use "" as the password for an open network')
    p.add_argument("--host", metavar="auto|ADDRESS", help="auto, or an address like 192.168.1.20:8810")
    p.add_argument("--until-paired", action="store_true", help="poll status; exit 0 once paired (see exit codes)")
    p.add_argument("lines", nargs="*", metavar="LINE", help="raw console lines, e.g. status, scan, \"log off\"")
    args = p.parse_args(argv)
    try:
        lines = build_lines(args)
    except ValueError as err:
        p.error(str(err))
    port = open_port(args.port)
    try:
        return run(port, lines, args.wait, args.until_paired)
    finally:
        port.close()


if __name__ == "__main__":
    sys.exit(main())
