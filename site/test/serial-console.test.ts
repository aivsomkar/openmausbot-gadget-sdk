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
