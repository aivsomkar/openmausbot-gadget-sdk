// SPDX-License-Identifier: Apache-2.0
import assert from "node:assert/strict";
import { test } from "node:test";
import { COPY, PAIR_PATH, pairErrorMessage, pairingFailure } from "../src/copy.ts";

test("the installer names the desktop path, Remote access and the Windows hint (spec §5.8 step 5)", () => {
  assert.equal(PAIR_PATH, "MausBot → Settings → Remote access → Pair a gadget");
  assert.match(COPY.codeHow, /MausBot → Settings → Remote access → Pair a gadget/);
  assert.match(COPY.remoteAccessOn, /Remote access must be on/);
  assert.match(COPY.windowsPublic, /Public/);
  assert.match(COPY.windowsPublic, /Private/);
  assert.equal(COPY.pressRst, "Press RST or unplug and replug the board");
  assert.equal(COPY.devkitPort, "Plug the board in by the USB-C port labelled USB, not the one labelled UART.");
});

test("every pairing failure has a sentence", () => {
  const status = { op: "status", wifi: "connected", id: "gad_x", pair: "error", fw: "1.0.0" } as const;
  assert.match(pairingFailure({ kind: "pair_error", code: "bad_code", status }, "Home"), /Pair a gadget/);
  assert.match(pairingFailure({ kind: "wifi_failed", status }, "Home"), /can't join Home/);
  assert.match(pairingFailure({ kind: "device_limit", status }, "Home"), /too many devices/);
  assert.match(pairingFailure({ kind: "command_error", cmd: "wifi", message: "too long" }, "Home"), /too long/);
  assert.match(pairingFailure({ kind: "timeout", status: null }, "Home"), /Remote access is on/);
  assert.match(pairingFailure({ kind: "lost" }, "Home"), /Press RST/);
  for (const code of ["bad_code", "device_limit", "proto_unsupported", "bad_sig", "something_new"]) {
    assert.notEqual(pairErrorMessage(code), "");
  }
  assert.equal(
    COPY.deviceLimitWaiting,
    "MausBot has too many devices. Remove one in MausBot → Settings → Remote access; the gadget keeps trying for two minutes.",
  );
});

test("the port hint says only ESP32-S3 boards on native USB show up", () => {
  // requestPort() filters on 0x303A/0x1001: a plain ESP32 or a USB-to-UART port never appears (Review Focus 5).
  assert.match(COPY.noPortHint, /ESP32-S3/);
  assert.match(COPY.noPortHint, /plain ESP32/);
  assert.match(COPY.noPortHint, /USB-C cable that carries data/);
  assert.match(COPY.noPortHint, /hold BOOT/);
});
