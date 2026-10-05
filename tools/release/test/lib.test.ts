// SPDX-License-Identifier: Apache-2.0
import assert from "node:assert/strict";
import { test } from "node:test";
import { firmwareText } from "../../../protocol/lib/identity.ts";
import { verifyP256 } from "../../../protocol/lib/verify.ts";
import {
  RELEASE_REPO,
  assetNames,
  buildInstallIndex,
  checkKeyTables,
  indexOfBytes,
  installEntryFromFlasherArgs,
  keysReleaseC,
  manifestUrl,
  parseReleasePubFiles,
  parseTag,
  readAppDescriptor,
  sha256sumsText,
} from "../lib.ts";
import { FLASHER_ARGS, T1_PUB_B64, fakeAppImage } from "./helpers.ts";

test("parseTag: release, prerelease and refusals", () => {
  assert.deepEqual(parseTag("v1.1.0"), { version: "1.1.0", prerelease: false });
  assert.deepEqual(parseTag("v1.2.0-rc.1"), { version: "1.2.0-rc.1", prerelease: true });
  for (const bad of ["1.1.0", "v1.1", "v1.1.0-dev", "v0.0.0-dev", "v1.1.0+build", "v01.1.0x", "v"]) {
    assert.throws(() => parseTag(bad), Error, bad);
  }
});

test("asset names and manifest URL follow contract §4.1 and §4.3", () => {
  assert.deepEqual(assetNames("amoled-175c", "1.1.0"), {
    app: "openmausbot-gadget-amoled-175c-1.1.0.bin",
    bootloader: "openmausbot-gadget-amoled-175c-1.1.0-bootloader.bin",
    partitionTable: "openmausbot-gadget-amoled-175c-1.1.0-partition-table.bin",
    otaData: "openmausbot-gadget-amoled-175c-1.1.0-ota-data-initial.bin",
    full: "openmausbot-gadget-amoled-175c-1.1.0-full.bin",
  });
  assert.equal(
    manifestUrl(RELEASE_REPO, "amoled-175c", "1.1.0"),
    "https://github.com/aivsomkar/openmausbot-gadget-sdk/releases/download/v1.1.0/openmausbot-gadget-amoled-175c-1.1.0.bin",
  );
});

test("install entry copies flasher_args.json offsets and sorts parts by address", () => {
  assert.deepEqual(installEntryFromFlasherArgs(FLASHER_ARGS, "amoled-175c", "1.1.0"), {
    parts: [
      { path: "openmausbot-gadget-amoled-175c-1.1.0-bootloader.bin", offset: "0x0" },
      { path: "openmausbot-gadget-amoled-175c-1.1.0-partition-table.bin", offset: "0x8000" },
      { path: "openmausbot-gadget-amoled-175c-1.1.0-ota-data-initial.bin", offset: "0xf000" },
      { path: "openmausbot-gadget-amoled-175c-1.1.0.bin", offset: "0x20000" },
    ],
    full: "openmausbot-gadget-amoled-175c-1.1.0-full.bin",
  });
});

test("install entry never hard-codes offsets", () => {
  const moved: { flash_files: Record<string, string> } = structuredClone(FLASHER_ARGS);
  moved.flash_files = {
    "0x0": "bootloader/bootloader.bin",
    "0x8000": "partition_table/partition-table.bin",
    "0xd000": "ota_data_initial.bin",
    "0x10000": "openmausbot-gadget.bin",
  };
  const entry = installEntryFromFlasherArgs(moved, "lcd-154", "2.0.0");
  assert.deepEqual(entry.parts.map((p) => p.offset), ["0x0", "0x8000", "0xd000", "0x10000"]);
});

test("install entry refuses a missing, extra or foreign image", () => {
  const missing = structuredClone(FLASHER_ARGS) as { flash_files: Record<string, string> };
  delete missing.flash_files["0xf000"];
  assert.throws(() => installEntryFromFlasherArgs(missing, "devkit", "1.0.0"), /ota_data_initial\.bin/);
  const extra = structuredClone(FLASHER_ARGS) as { flash_files: Record<string, string> };
  extra.flash_files["0x9000"] = "nvs.bin";
  assert.throws(() => installEntryFromFlasherArgs(extra, "devkit", "1.0.0"), /unexpected image/);
  const chip = structuredClone(FLASHER_ARGS);
  chip.extra_esptool_args.chip = "esp32";
  assert.throws(() => installEntryFromFlasherArgs(chip, "devkit", "1.0.0"), /esp32s3/);
});

test("buildInstallIndex keeps board order and refuses mixed versions", () => {
  const e = installEntryFromFlasherArgs(FLASHER_ARGS, "devkit", "1.0.0");
  const idx = buildInstallIndex("1.0.0", [
    { board: "devkit", version: "1.0.0", install: e },
    { board: "amoled-175c", version: "1.0.0", install: e },
  ]);
  assert.deepEqual(Object.keys(idx.boards), ["amoled-175c", "devkit"]);
  assert.throws(() => buildInstallIndex("1.0.0", [{ board: "devkit", version: "0.9.0", install: e }]), /0\.9\.0/);
});

test("readAppDescriptor reads version and project name at offset 32", () => {
  assert.deepEqual(readAppDescriptor(fakeAppImage("1.2.3")), { version: "1.2.3", projectName: "openmausbot-gadget" });
  const bad = fakeAppImage("1.2.3");
  bad[32] = 0;
  assert.throws(() => readAppDescriptor(bad), /0xABCD5432/);
  assert.throws(() => readAppDescriptor(new Uint8Array(200)), /0xE9/);
});

test("indexOfBytes finds a key inside an image", () => {
  const key = new Uint8Array(Buffer.from(T1_PUB_B64, "base64"));
  const img = fakeAppImage("1.0.0", [key]);
  assert.equal(indexOfBytes(img, key), 512);
  assert.equal(indexOfBytes(fakeAppImage("1.0.0"), key), -1);
});

test("SHA256SUMS is GNU format, sorted by name", () => {
  assert.equal(
    sha256sumsText([
      { name: "b.bin", sha256: "bb" },
      { name: "a.json", sha256: "aa" },
    ]),
    "aa  a.json\nbb  b.bin\n",
  );
});

test("the signed firmware text matches the contract's t1 vector", () => {
  const text = firmwareText("amoled-175c", "1.1.0", 1234567, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
  const der = Buffer.from(
    "3046022100de2dfa125231a739f0b1b27e2ec085e4027b59e4486978457ffccb95b3895f2d022100ef00bd5c4177cb97ba4a4f4036046f6a20b3c896c5ab1124720b54afe6179744",
    "hex",
  );
  assert.equal(verifyP256(new Uint8Array(Buffer.from(T1_PUB_B64, "base64")), text, new Uint8Array(der)), true);
});

test("checkKeyTables: empty, foreign ids, mismatches and a leaked test key", () => {
  const r1 = Buffer.from(T1_PUB_B64, "base64").toString("hex");
  const files = new Map([["r1", r1]]);
  assert.deepEqual(checkKeyTables({ release: [{ id: "r1", pub: r1 }], test: [] }, files), []);
  assert.match(checkKeyTables({ release: [], test: [] }, new Map()).join("\n"), /empty/);
  assert.match(checkKeyTables({ release: [{ id: "t1", pub: r1 }], test: [] }, files).join("\n"), /does not match/);
  assert.match(checkKeyTables({ release: [{ id: "r1", pub: "04".padEnd(130, "0") }], test: [] }, files).join("\n"), /differs/);
  assert.match(checkKeyTables({ release: [{ id: "r1", pub: r1 }], test: [{ id: "t1", pub: r1 }] }, files).join("\n"), /test key table/);
  assert.match(checkKeyTables({ release: [{ id: "r1", pub: r1 }], test: [] }, new Map([["r1", r1], ["r2", r1]])).join("\n"), /r2/);
});

test("parseReleasePubFiles requires canonical base64 of a SEC1 point and one newline", () => {
  assert.equal(parseReleasePubFiles([{ name: "release-r1.pub.b64", text: `${T1_PUB_B64}\n` }]).size, 1);
  assert.equal(parseReleasePubFiles([{ name: "test-t1.pub.b64", text: `${T1_PUB_B64}\n` }]).size, 0);
  assert.throws(() => parseReleasePubFiles([{ name: "release-r1.pub.b64", text: "AAAA\n" }]), /65-byte/);
});

test("keysReleaseC writes the sentinel with no keys and one row per key", () => {
  assert.match(keysReleaseC([]), /\{\{NULL, \{0\}\}\};\nconst size_t gadget_release_keys_count = 0;/);
  const pub = new Uint8Array(Buffer.from(T1_PUB_B64, "base64"));
  const c = keysReleaseC([
    { id: "r2", pub },
    { id: "r1", pub },
  ]);
  assert.ok(c.indexOf('"r1"') < c.indexOf('"r2"'));
  assert.match(c, /gadget_release_keys_count = 2;/);
  assert.throws(() => keysReleaseC([{ id: "t1", pub }]), /bad release key id/);
});
