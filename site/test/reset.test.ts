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
  const out = await openConsolePort(old, fakeSerial([bridge, reborn]), fakeClock().sleep, 10_000);
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
