// SPDX-License-Identifier: Apache-2.0
import assert from "node:assert/strict";
import { test } from "node:test";
import { InstallerError } from "../src/errors.ts";
import { openConsolePort, resetToApp } from "../src/reset.ts";
import { FakeLoader, FakeTransport } from "./fakes-esptool.ts";
import { FakePort, fakeClock, fakeSerial } from "./fakes-serial.ts";

test("resetToApp clears FORCE_DOWNLOAD_BOOT, pulses RTS, disconnects, reopens at 115200 with DTR and RTS low", async () => {
  const log: string[] = [];
  const loader = new FakeLoader(log);
  const transport = new FakeTransport(log);
  const port = new FakePort();
  const clock = fakeClock();
  const sleep = async (ms: number) => {
    log.push(`sleep ${ms}`);
    await clock.sleep(ms);
  };
  const out = await resetToApp({ loader, transport, port, serial: fakeSerial([port]), sleep });
  assert.equal(out, port);
  assert.deepEqual(log, ["writeReg 0x6000812c 0 1", "setDTR false", "setRTS true", "sleep 100", "setRTS false", "disconnect"]);
  assert.equal(port.opened, 1);
  assert.deepEqual(port.signals, [{ dataTerminalReady: false, requestToSend: false }]);
});

test("resetToApp still resets when the stub no longer answers writeReg", async () => {
  const log: string[] = [];
  const loader = new FakeLoader(log);
  loader.writeReg = async () => {
    throw new Error("Timeout waiting for response");
  };
  const port = new FakePort();
  await resetToApp({ loader, transport: new FakeTransport(log), port, serial: fakeSerial([port]), sleep: fakeClock().sleep });
  assert.deepEqual(log.slice(0, 3), ["setDTR false", "setRTS true", "setRTS false"]);
});

/** What Chrome's open() rejects with both when another program holds the port and when the device is gone. */
const openFailed = () => Array.from({ length: 100 }, () => new Error("Failed to open serial port."));

test("after the USB device re-enumerates, the new 0x303A/0x1001 port is used", async () => {
  const old = new FakePort();
  old.openFailures = openFailed();
  const reborn = new FakePort();
  reborn.openFailures = [new Error("Failed to open serial port.")];
  const bridge = new FakePort({ usbVendorId: 0x10c4, usbProductId: 0xea60 });
  // Before the reset getPorts() lists the board as `old`; afterwards it lists `reborn` instead.
  const out = await openConsolePort(old, fakeSerial([old, bridge], [bridge, reborn]), fakeClock().sleep, 10_000);
  assert.equal(out, reborn);
  assert.equal(bridge.opened, 0);
  assert.deepEqual(reborn.signals, [{ dataTerminalReady: false, requestToSend: false }]);
});

test("a present port that refuses gives port_busy; a vanished board gives port_lost (same open() error)", async () => {
  // Busy: a serial monitor holds the port, so it stays in getPorts() and keeps refusing.
  const held = new FakePort();
  held.openFailures = openFailed();
  await assert.rejects(openConsolePort(held, fakeSerial([held]), fakeClock().sleep, 1000), (e: unknown) => e instanceof InstallerError && e.code === "port_busy");
  // Lost: the board never came back, so getPorts() lists nothing and the old port refuses.
  const gone = new FakePort();
  gone.openFailures = openFailed();
  await assert.rejects(openConsolePort(gone, fakeSerial([]), fakeClock().sleep, 1000), (e: unknown) => e instanceof InstallerError && e.code === "port_lost");
});

test("resetToApp still reopens the console when releasing RTS fails as the chip drops off USB", async () => {
  const log: string[] = [];
  const transport = new FakeTransport(log);
  transport.setRTS = async (state: boolean) => {
    log.push(`setRTS ${state}`);
    if (!state) throw Object.assign(new Error("Failed to execute 'setSignals' on 'SerialPort'."), { name: "NetworkError" });
  };
  const port = new FakePort();
  const out = await resetToApp({ loader: new FakeLoader(log), transport, port, serial: fakeSerial([port]), sleep: fakeClock().sleep });
  assert.equal(out, port);
  assert.deepEqual(log.slice(-2), ["setRTS false", "disconnect"]);
  assert.deepEqual(port.signals, [{ dataTerminalReady: false, requestToSend: false }]);
});

test("a second gadget already plugged in is never opened after the reset", async () => {
  // The flashed board refuses once while it re-enumerates under the same port.
  const other = new FakePort();
  const flashed = new FakePort();
  flashed.openFailures = [new Error("Failed to open serial port.")];
  const sleep = fakeClock().sleep;
  const out = await resetToApp({ loader: new FakeLoader(), transport: new FakeTransport([]), port: flashed, serial: fakeSerial([other, flashed]), sleep });
  assert.equal(out, flashed);
  assert.equal(other.opened, 0);
  // The flashed board comes back as a new port; the other gadget is still not touched.
  const gone = new FakePort();
  gone.openFailures = openFailed();
  const reborn = new FakePort();
  const again = await resetToApp({
    loader: new FakeLoader(),
    transport: new FakeTransport([]),
    port: gone,
    serial: fakeSerial([other, gone], [other, reborn]),
    sleep,
  });
  assert.equal(again, reborn);
  assert.equal(other.opened, 0);
});

test("a busy board never falls back to another gadget", async () => {
  const held = new FakePort();
  held.openFailures = openFailed();
  const other = new FakePort();
  await assert.rejects(openConsolePort(held, fakeSerial([held, other]), fakeClock().sleep, 1000), (e: unknown) => e instanceof InstallerError && e.code === "port_busy");
  assert.equal(other.opened, 0);
});

test("a snapshot of the other boards taken earlier lets a reopen use a port that is already listed", async () => {
  // A reopen after RST or a replug: the board may already be listed as `reborn` when the call starts,
  // so the caller passes the other boards it saw while the old port was open.
  const other = new FakePort();
  const old = new FakePort();
  old.openFailures = openFailed();
  const reborn = new FakePort();
  const out = await openConsolePort(old, fakeSerial([other, reborn]), fakeClock().sleep, 10_000, [other]);
  assert.equal(out, reborn);
  assert.equal(other.opened, 0);
});

test("a port that vanishes between open() and setSignals() is closed and tried again", async () => {
  const port = new FakePort();
  port.signalFailures = [Object.assign(new Error("The device has been lost."), { name: "NetworkError" })];
  const out = await openConsolePort(port, fakeSerial([port]), fakeClock().sleep, 10_000);
  assert.equal(out, port);
  assert.equal(port.opened, 2);
  assert.deepEqual(port.signals, [{ dataTerminalReady: false, requestToSend: false }]);
});
