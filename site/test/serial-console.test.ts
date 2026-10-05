// SPDX-License-Identifier: Apache-2.0
import assert from "node:assert/strict";
import { test } from "node:test";
import { InstallerError } from "../src/errors.ts";
import { SerialConsole } from "../src/serial-console.ts";
import { FakePort } from "./fakes-serial.ts";

const tick = () => new Promise((r) => setTimeout(r, 5));

test("SerialConsole assembles lines across chunks and parses @omb lines", async () => {
  const port = new FakePort();
  await port.open({ baudRate: 115200 });
  const con = new SerialConsole(port, { now: () => 42 });
  port.emit("\u001b[0;32mI (12) boot: hello\u001b[0m\r\n@omb {\"op\":\"boot\",\"board\":\"lcd-154\",");
  port.emit('"fw":"1.0.0","id":"gad_3f9a0c2b7e41d856"}\r\n');
  await tick();
  const events = con.drain();
  assert.equal(events.length, 2);
  assert.equal(events[0]?.msg, null);
  assert.equal(events[1]?.msg?.op, "boot");
  assert.equal(con.lastOutputAt, 42);
  assert.deepEqual(con.drain(), []);
  await con.close();
});

test("a UTF-8 character cut between USB packets still decodes (an accented SSID in a scan line)", async () => {
  const port = new FakePort();
  await port.open({ baudRate: 115200 });
  const con = new SerialConsole(port);
  const bytes = new TextEncoder().encode('@omb {"op":"scan","networks":[{"ssid":"Café","rssi":-50,"auth":"wpa2"}]}\r\n');
  const cut = bytes.indexOf(0xc3) + 1; // between the two bytes of "é" (0xC3 0xA9)
  assert.equal(bytes[cut], 0xa9);
  port.emitBytes(bytes.slice(0, cut));
  port.emitBytes(bytes.slice(cut));
  await tick();
  const msg = con.drain()[0]?.msg;
  assert.equal(msg?.op === "scan" && msg.networks[0]?.ssid, "Café");
  await con.close();
});

test("SerialConsole writes raw text lines (no SLIP framing)", async () => {
  const port = new FakePort();
  await port.open({ baudRate: 115200 });
  const con = new SerialConsole(port);
  await con.send('wifi "Home Net" "pass"');
  await con.send("status");
  assert.deepEqual(port.sent(), ['wifi "Home Net" "pass"', "status"]);
  await con.close();
});

test("SerialConsole reports a lost device and refuses further writes", async () => {
  const port = new FakePort();
  await port.open({ baudRate: 115200 });
  const con = new SerialConsole(port);
  port.unplug();
  await con.finished;
  assert.equal(con.lost, true);
  await assert.rejects(con.send("status"), (e: unknown) => e instanceof InstallerError && e.code === "port_lost");
});

test("a write the gadget never reads times out instead of hanging", async () => {
  const port = new FakePort();
  await port.open({ baudRate: 115200 });
  port.writable = new WritableStream<Uint8Array>({ write: () => new Promise(() => undefined) });
  const con = new SerialConsole(port, { writeTimeoutMs: 20 });
  await assert.rejects(con.send("status"), (e: unknown) => e instanceof InstallerError && e.code === "console_write_timeout");
});

test("two overlapping sends both arrive, in order", async () => {
  const port = new FakePort();
  await port.open({ baudRate: 115200 });
  const con = new SerialConsole(port);
  await Promise.all([con.send('wifi "Home Net" "pass word"'), con.send("status"), con.send("scan")]);
  assert.deepEqual(port.sent(), ['wifi "Home Net" "pass word"', "status", "scan"]);
  await con.close();
});

test("an onEvent callback that throws does not end the console", async () => {
  const port = new FakePort();
  await port.open({ baudRate: 115200 });
  const con = new SerialConsole(port);
  con.onEvent = () => {
    throw new Error("the page failed to draw a log line");
  };
  port.emit("I (10) boot: one\r\n");
  await tick();
  port.emit('@omb {"op":"say","turn":"t1-1"}\r\n');
  await tick();
  assert.equal(con.lost, false);
  assert.deepEqual(con.drain().map((e) => e.msg?.op ?? e.line), ["I (10) boot: one", "say"]);
  await con.send("status");
  assert.deepEqual(port.sent(), ["status"]);
  await con.close();
});

test("a non-fatal read error keeps the console; a lost device still ends it", async () => {
  for (const name of ["BufferOverrunError", "BreakError", "FramingError", "ParityError"]) {
    const port = new FakePort();
    await port.open({ baudRate: 115200 });
    const con = new SerialConsole(port);
    port.emit("I (10) boot: busy log\r\n");
    await tick();
    port.readError(name);
    await tick();
    port.emit('@omb {"op":"say","turn":"t1-1"}\r\n');
    await tick();
    assert.equal(con.lost, false, name);
    assert.deepEqual(con.drain().map((e) => e.msg?.op ?? e.line), ["I (10) boot: busy log", "say"], name);
    port.unplug();
    await con.finished;
    assert.equal(con.lost, true, name);
  }
});

test("close() during a session ends it without marking it lost", async () => {
  const port = new FakePort();
  await port.open({ baudRate: 115200 });
  const con = new SerialConsole(port);
  port.readError("BreakError");
  await tick();
  await con.close();
  assert.equal(con.lost, false);
  assert.equal(port.isOpen, false);
});

test("a port that hands back the errored stream ends the session instead of spinning", async () => {
  const port = new FakePort();
  await port.open({ baudRate: 115200 });
  const con = new SerialConsole(port);
  port.readError("BreakError", false);
  await con.finished;
  assert.equal(con.lost, true);
});
