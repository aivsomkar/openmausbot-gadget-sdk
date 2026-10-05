# SPDX-License-Identifier: Apache-2.0
"""python3 -B -m unittest discover -s tools/console -p 'test_*.py'"""
import argparse
import contextlib
import io
import json
import os
import shutil
import subprocess
import sys
import tempfile
import types
import unittest
from unittest import mock

sys.path.insert(0, os.path.dirname(__file__))
import omb_console as oc  # noqa: E402

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
CC = os.environ.get("CC", "cc")


class FakeClock:
    def __init__(self):
        self.t = 0.0

    def __call__(self):
        self.t += 0.05
        return self.t


class FakePort:
    """A gadget console. Each `status` gets the next state in `states` (the last one repeats);
    `on_line(line, port)` sees every other line the helper writes."""

    def __init__(self, states=None, on_line=None):
        self.written = []
        self.pending = b""
        self.states = list(states or [{"pair": "connecting"}, {"pair": "connecting"}, {"pair": "paired"}])
        self.on_line = on_line

    def reply(self, msg):
        self.pending += b"\x1b[0;32mI (1) wifi: ok\x1b[0m\r\n@omb " + json.dumps(msg).encode() + b"\r\n"

    def write(self, data):
        line = data.decode()
        self.written.append(line)
        if line == "status\n":
            state = self.states.pop(0) if len(self.states) > 1 else self.states[0]
            self.reply(dict({"op": "status", "wifi": "connected", "id": "gad_x", "pair": "connecting", "fw": "1.0.0"}, **state))
        elif self.on_line is not None:
            self.on_line(line.rstrip("\n"), self)

    def read(self, n):
        out, self.pending = self.pending[:n], self.pending[n:]
        return out


class BootingPort(FakePort):
    """Ignores everything until the fake clock passes 1 s, like an app that is still booting."""

    def __init__(self, clock, **kwargs):
        super().__init__(**kwargs)
        self.clock = clock

    def write(self, data):
        if self.clock.t < 1.0:
            self.written.append(data.decode())
            return
        super().write(data)


class SplitPort(FakePort):
    """Hands out its bytes so that one read ends in the middle of the UTF-8 bytes of "é" in "Café"."""

    def read(self, n):
        i = self.pending.find("Caf".encode() + b"\xc3")
        if i >= 0:
            out, self.pending = self.pending[: i + 4], self.pending[i + 4:]
            return out
        return super().read(n)


def fake_serial_module(port=None, open_error=None):
    """A stand-in for pyserial: Serial() returns `port` or raises `open_error`."""
    mod = types.ModuleType("serial")

    class SerialException(IOError):
        pass

    def serial_cls(path, baud, timeout):
        if open_error is not None:
            raise SerialException(open_error)
        return port

    mod.SerialException = SerialException
    mod.Serial = serial_cls
    return mod


class GonePort:
    """An open port whose device has dropped off USB: every read and write raises."""

    def __init__(self, exc):
        self.exc = exc
        self.closed = False

    def write(self, data):
        raise self.exc("device reports readiness to read but returned no data")

    def read(self, n):
        raise self.exc("device disconnected")

    def close(self):
        self.closed = True


def args(lines=(), pair=None, wifi=None, host=None):
    return argparse.Namespace(lines=list(lines), pair=pair, wifi=wifi, host=host)


class Pure(unittest.TestCase):
    def test_parse_omb_line(self):
        self.assertEqual(oc.parse_omb_line('@omb {"op":"say","turn":"t1-1"}\r'), {"op": "say", "turn": "t1-1"})
        self.assertEqual(oc.parse_omb_line('\x1b[0;32m@omb {"op":"say","turn":"t"}\x1b[0m')["op"], "say")
        self.assertIsNone(oc.parse_omb_line("I (12) boot: hello"))
        self.assertIsNone(oc.parse_omb_line("@omb {bad"))
        self.assertIsNone(oc.parse_omb_line('@omb ["op"]'))

    def test_quote_arg(self):
        self.assertEqual(oc.quote_arg('a "b" \\c'), '"a \\"b\\" \\\\c"')
        self.assertEqual(oc.quote_arg(""), '""')
        with self.assertRaises(ValueError):
            oc.quote_arg("a\nb")

    def test_line_splitter(self):
        s = oc.LineSplitter()
        self.assertEqual(s.feed("a\r"), ["a"])
        self.assertEqual(s.feed("\nb\nc"), ["b"])
        self.assertEqual(s.feed("\r\n"), ["c"])

    def test_build_lines_order(self):
        args = argparse.Namespace(lines=["log off"], pair="123456", wifi=["Home Net", "pass word"], host="auto")
        self.assertEqual(oc.build_lines(args), ["log off", "pair 123456", 'wifi "Home Net" "pass word"', "host auto"])
        with self.assertRaises(ValueError):
            oc.build_lines(argparse.Namespace(lines=[], pair="12345", wifi=None, host=None))

    def test_build_lines_refuses_line_breaks_and_nul_in_every_value(self):
        # `--host $'auto\nforget'` must never reach the gadget as `host auto` then `forget`.
        for bad in (
            args(host="auto\nforget"),
            args(host="auto\r"),
            args(lines=["status\nforget"]),
            args(lines=["log off\x00"]),
            args(pair="123456\n"),
            args(wifi=["Home\nNet", "password"]),
            args(wifi=["Home", "pass\x00word"]),
            args(wifi=["Home", "password\r"]),
        ):
            with self.subTest(bad=bad), self.assertRaises(ValueError):
                oc.build_lines(bad)

    def test_build_lines_applies_the_consoles_wifi_rules(self):
        ok = [["Home", ""], ["Home", "12345678"], ["Home", "p" * 63], ["Home", "0123456789abcdefABCDEF" * 2 + "0123456789abcdefABCD"],
              ["x" * 32, "password"], ["é" * 16, "password"], ["Home", "é" * 4]]
        for ssid, password in ok:
            with self.subTest(ok=(ssid, password)):
                self.assertEqual(oc.build_lines(args(wifi=[ssid, password])), ["wifi " + oc.quote_arg(ssid) + " " + oc.quote_arg(password)])
        bad = [["", "password"], ["x" * 33, "password"], ["é" * 17, "password"], ["Home", "short"], ["Home", "p" * 64],
               ["Home", "g" * 64], ["Home", "p" * 65], ["Home", "é" * 32]]
        for ssid, password in bad:
            with self.subTest(bad=(ssid, password)), self.assertRaises(ValueError):
                oc.build_lines(args(wifi=[ssid, password]))

    def test_build_lines_accepts_only_auto_or_an_address_the_firmware_takes(self):
        for host in ("auto", "192.168.1.20:8810", "omkars-mac.local", "a" * 57, "a" * 57 + ":65535", "h:1"):
            with self.subTest(ok=host):
                self.assertEqual(oc.build_lines(args(host=host)), ["host " + host])
        for host in ("", "a" * 58, "a" * 58 + ":8810", "my mac", "h:0", "h:65536", "h:", "h:1:2", "http://h", "auto forget", "ä.local"):
            with self.subTest(bad=host), self.assertRaises(ValueError):
                oc.build_lines(args(host=host))

    def test_main_refuses_a_bad_argument_with_exit_2(self):
        err = io.StringIO()
        with contextlib.redirect_stderr(err), self.assertRaises(SystemExit) as done:
            oc.main(["--port", "/dev/null", "--host", "auto\nforget"])
        self.assertEqual(done.exception.code, 2)


class Run(unittest.TestCase):
    def run_helper(self, port, lines, wait=10, until_paired=True, clock=None):
        out, err = io.StringIO(), io.StringIO()
        code = oc.run(port, lines, wait, until_paired, out=out, err=err, clock=clock or FakeClock())
        return code, out.getvalue(), err.getvalue()

    def test_until_paired_succeeds(self):
        port = FakePort()
        code, out, _ = self.run_helper(port, ["pair 123456"])
        self.assertEqual(code, 0)
        self.assertEqual(port.written[:2], ["status\n", "pair 123456\n"])  # the app answered before anything was sent
        self.assertIn('"pair": "paired"', out.splitlines()[-1])

    def test_until_paired_fails_on_error_and_timeout(self):
        wrong = FakePort([{"pair": "connecting"}, {"pair": "connecting"}, {"pair": "error", "error": "bad_code"}])
        self.assertEqual(self.run_helper(wrong, [])[0], oc.EXIT_FAILED)
        self.assertEqual(self.run_helper(FakePort([{"pair": "connecting"}]), [], wait=2)[0], oc.EXIT_FAILED)

    def test_plain_listen_prints_only_omb_lines(self):
        code, out, _ = self.run_helper(FakePort(), ["status"], wait=1, until_paired=False)
        self.assertEqual(code, 0)
        self.assertEqual([json.loads(l)["op"] for l in out.splitlines()], ["status"])

    def test_waits_for_the_app_before_sending_and_gives_up_on_silence(self):
        clock = FakeClock()
        port = BootingPort(clock)
        code, _, _ = self.run_helper(port, ["pair 123456"], clock=clock)
        self.assertEqual(code, 0)
        first = port.written.index("pair 123456\n")
        self.assertGreaterEqual(first, 2)  # some probes went unanswered while it booted
        self.assertTrue(all(w == "status\n" for w in port.written[:first]))
        silent = BootingPort(FakeClock())  # nothing advances its own clock, so it never finishes booting
        code, _, err = self.run_helper(silent, ["pair 123456"])
        self.assertEqual(code, oc.EXIT_NO_APP)
        self.assertIn("press RST", err)
        self.assertNotIn("pair 123456\n", silent.written)

    def test_a_retry_after_bad_code_ignores_the_stale_error(self):
        stale = {"pair": "error", "error": "bad_code"}
        port = FakePort([stale, stale, stale, {"pair": "paired"}])  # the probe takes the first
        self.assertEqual(self.run_helper(port, ["pair 654321"])[0], oc.EXIT_PAIRED)

    def test_wrong_wifi_password_exits_4(self):
        joining = {"wifi": "connecting", "pair": "code_stored"}
        port = FakePort([joining, joining, {"wifi": "failed", "pair": "code_stored"}])
        self.assertEqual(self.run_helper(port, ['wifi "Home" "wrongpass"'])[0], oc.EXIT_WIFI_FAILED)

    def test_no_mausbot_found_exits_3_at_once(self):
        def answer(line, port):
            if line == "host auto":
                port.reply({"op": "hosts", "hosts": []})

        port = FakePort([{"pair": "code_stored"}], on_line=answer)
        clock = FakeClock()
        code, out, _ = self.run_helper(port, ["host auto"], wait=150, clock=clock)
        self.assertEqual(code, oc.EXIT_NEED_HOST)
        self.assertIn('"op": "hosts"', out)
        self.assertLess(clock.t, 20)  # it did not wait out --wait 150

    def test_a_single_listed_mausbot_is_used(self):
        def answer(line, port):
            if line == "host auto":
                port.reply({"op": "hosts", "hosts": [{"name": "A", "address": "10.0.0.2:8810", "id": ""}]})
            if line == "host 10.0.0.2:8810":
                port.states = [{"pair": "paired"}]

        port = FakePort([{"pair": "code_stored"}], on_line=answer)
        self.assertEqual(self.run_helper(port, ["host auto"])[0], oc.EXIT_PAIRED)
        self.assertIn("host 10.0.0.2:8810\n", port.written)

    def test_a_retry_after_wifi_failed_ignores_the_stale_failure(self):
        stale = {"wifi": "failed", "pair": "code_stored"}
        port = FakePort([stale, stale, {"wifi": "connected", "pair": "connecting"}, {"pair": "paired"}])  # the probe takes the first
        self.assertEqual(self.run_helper(port, ['wifi "Home" "rightpass"'])[0], oc.EXIT_PAIRED)

    def test_a_refused_setup_command_exits_1_at_once(self):
        def answer(line, port):
            if line.startswith("wifi "):
                port.reply({"op": "error", "cmd": "wifi", "message": "the password must be empty, 8-63 characters or 64 hex digits"})

        port = FakePort([{"pair": "code_stored"}], on_line=answer)
        clock = FakeClock()
        code, _, err = self.run_helper(port, ["pair 123456", 'wifi "Home" "short"'], wait=150, clock=clock)
        self.assertEqual(code, oc.EXIT_FAILED)
        self.assertIn("8-63 characters", err)
        self.assertLess(clock.t, 20)  # it did not wait out --wait 150

    def test_a_character_split_between_two_reads_arrives_whole(self):
        def answer(line, port):
            if line == "scan":
                msg = {"op": "scan", "networks": [{"ssid": "Café", "rssi": -50, "auth": "wpa2"}]}
                port.pending += ("@omb " + json.dumps(msg, ensure_ascii=False) + "\r\n").encode("utf-8")

        code, out, _ = self.run_helper(SplitPort(on_line=answer), ["scan"], wait=1, until_paired=False)
        self.assertEqual(code, 0)
        self.assertIn('"ssid": "Café"', out)

    def test_device_limit_keeps_polling_with_one_warning(self):
        limit = {"pair": "error", "error": "device_limit"}
        port = FakePort([limit] * 5 + [{"pair": "paired"}])
        code, _, err = self.run_helper(port, ["pair 123456"])
        self.assertEqual(code, oc.EXIT_PAIRED)
        self.assertEqual(err.count("too many devices"), 1)

    def test_device_limit_window_closing_exits_1_with_a_hint(self):
        # Spec §4.3, contract §2.11 rule 2: 120 s after `pair` the gadget drops the code and shows `unpaired`.
        limit = {"pair": "error", "error": "device_limit"}
        port = FakePort([limit] * 3 + [{"pair": "unpaired"}])  # the probe takes the first
        clock = FakeClock()
        code, _, err = self.run_helper(port, ["pair 123456"], wait=150, clock=clock)
        self.assertEqual(code, oc.EXIT_FAILED)
        self.assertLess(clock.t, 20)  # it did not wait out --wait 150
        self.assertIn("still has too many devices", err)
        self.assertIn("get a new code and rerun", err)


class Main(unittest.TestCase):
    def call_main(self, argv):
        err = io.StringIO()
        with contextlib.redirect_stderr(err), contextlib.redirect_stdout(io.StringIO()):
            code = oc.main(argv)
        return code, err.getvalue()

    def test_without_pyserial_exits_2(self):
        err = io.StringIO()
        with mock.patch.dict(sys.modules, {"serial": None}), contextlib.redirect_stderr(err), self.assertRaises(SystemExit) as done:
            oc.main(["--port", "/dev/cu.usbmodem1101", "status"])
        self.assertEqual(done.exception.code, 2)
        self.assertIn("pip install pyserial", err.getvalue())

    def test_a_port_that_cannot_be_opened_exits_5(self):
        with mock.patch.dict(sys.modules, {"serial": fake_serial_module(open_error="could not open port /dev/nope")}):
            code, err = self.call_main(["--port", "/dev/nope", "status"])
        self.assertEqual(code, oc.EXIT_NO_APP)
        self.assertIn("press RST or unplug and replug the board", err)

    def test_a_board_that_drops_off_usb_mid_run_exits_5(self):
        mod = fake_serial_module()
        gone = GonePort(mod.SerialException)
        mod.Serial = lambda path, baud, timeout: gone
        with mock.patch.dict(sys.modules, {"serial": mod}):
            code, err = self.call_main(["--port", "/dev/cu.usbmodem1101", "--pair", "123456", "--until-paired"])
        self.assertEqual(code, oc.EXIT_NO_APP)
        self.assertIn("port closed", err)
        self.assertTrue(gone.closed)

    def test_exit_code_help_names_the_new_cases(self):
        self.assertIn("a refused setup command", oc.EXIT_CODES)
        self.assertIn("port closed or could not be opened", oc.EXIT_CODES)


class Firmware(unittest.TestCase):
    @unittest.skipUnless(shutil.which(CC), "no C compiler (" + CC + ")")
    def test_quote_arg_output_splits_back_exactly_with_the_firmware_splitter(self):
        # The same harness and values as site/test/console-firmware.test.ts.
        with open(os.path.join(REPO, "site/test/awkward-values.json"), encoding="utf-8") as f:
            values = json.load(f)
        with tempfile.TemporaryDirectory() as tmp:
            exe = os.path.join(tmp, "split-argv")
            subprocess.run(
                [CC, "-std=c11", "-Wall", "-Wextra", "-Werror", "-I", os.path.join(REPO, "firmware/core/include"),
                 os.path.join(REPO, "site/test/split-argv.c"), os.path.join(REPO, "firmware/core/src/console.c"), "-o", exe],
                check=True,
            )
            lines = "".join("wifi " + oc.quote_arg(v) + " " + oc.quote_arg("pass word") + "\n" for v in values)
            done = subprocess.run([exe], input=lines.encode("utf-8"), capture_output=True, check=True)
        got = [json.loads(line) for line in done.stdout.decode("utf-8").splitlines()]
        self.assertEqual(got, [["wifi", v, "pass word"] for v in values])


if __name__ == "__main__":
    unittest.main()
