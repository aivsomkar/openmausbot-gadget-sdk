# SPDX-License-Identifier: Apache-2.0
"""python3 -B -m unittest discover -s tools/console -p 'test_*.py'"""
import argparse
import io
import json
import os
import shutil
import subprocess
import sys
import tempfile
import unittest

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
        args = argparse.Namespace(lines=["log off"], pair="123456", wifi=["Home Net", "p w"], host="auto")
        self.assertEqual(oc.build_lines(args), ["log off", "pair 123456", 'wifi "Home Net" "p w"', "host auto"])
        with self.assertRaises(ValueError):
            oc.build_lines(argparse.Namespace(lines=[], pair="12345", wifi=None, host=None))


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

    def test_device_limit_keeps_polling_with_one_warning(self):
        limit = {"pair": "error", "error": "device_limit"}
        port = FakePort([limit] * 5 + [{"pair": "paired"}])
        code, _, err = self.run_helper(port, ["pair 123456"])
        self.assertEqual(code, oc.EXIT_PAIRED)
        self.assertEqual(err.count("too many devices"), 1)


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
