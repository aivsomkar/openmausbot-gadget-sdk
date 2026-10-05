// SPDX-License-Identifier: Apache-2.0
import assert from "node:assert/strict";
import { createHash } from "node:crypto";
import { mkdtemp, writeFile } from "node:fs/promises";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { test } from "node:test";
import { checkFirmwareDir } from "../scripts/check-firmware.ts";

const index = {
  version: "1.1.0",
  boards: { devkit: { parts: [{ path: "b.bin", offset: "0x0" }, { path: "a.bin", offset: "0x20000" }], full: "f.bin" } },
};
const sha = (s: string) => createHash("sha256").update(s).digest("hex");

async function dir(idx: unknown, files: Record<string, string>): Promise<string> {
  const d = await mkdtemp(join(tmpdir(), "omb-site-fw-"));
  await writeFile(join(d, "install.json"), JSON.stringify(idx));
  for (const [name, body] of Object.entries(files)) await writeFile(join(d, name), body);
  return d;
}

test("the placeholder passes and says nothing is published", async () => {
  assert.match(await checkFirmwareDir(await dir({ version: null, boards: {} }, {})), /no firmware/);
});

test("a release passes when every file it names is there", async () => {
  assert.match(await checkFirmwareDir(await dir(index, { "b.bin": "b", "a.bin": "a", "f.bin": "f" })), /1\.1\.0 for devkit/);
});

test("a missing part or full image fails the Pages build", async () => {
  await assert.rejects(checkFirmwareDir(await dir(index, { "b.bin": "b", "f.bin": "f" })), /a\.bin/);
  await assert.rejects(checkFirmwareDir(await dir(index, { "b.bin": "b", "a.bin": "a" })), /f\.bin/);
});

test("SHA256SUMS from the release must match every installer file; unlisted extras are ignored", async () => {
  const files = { "b.bin": "b", "a.bin": "a", "f.bin": "f" };
  const lines = (over: Record<string, string> = {}) =>
    Object.entries({ "a.bin": "a", "b.bin": "b", "f.bin": "f", "manifest.json": "m", ...over })
      .map(([n, body]) => `${sha(body)}  ${n}\n`)
      .join("") + `${sha(JSON.stringify(index))}  install.json\n`;
  assert.match(await checkFirmwareDir(await dir(index, { ...files, SHA256SUMS: lines() })), /checksums match/);
  await assert.rejects(checkFirmwareDir(await dir(index, { ...files, SHA256SUMS: lines({ "a.bin": "tampered" }) })), /a\.bin does not match/);
  const missing = lines().split("\n").filter((l) => !l.endsWith("  f.bin")).join("\n");
  await assert.rejects(checkFirmwareDir(await dir(index, { ...files, SHA256SUMS: missing })), /does not list f\.bin/);
});
