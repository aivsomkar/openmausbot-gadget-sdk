// SPDX-License-Identifier: Apache-2.0
import assert from "node:assert/strict";
import { cp, mkdir, readFile, writeFile } from "node:fs/promises";
import { join } from "node:path";
import { test } from "node:test";
import { fileURLToPath } from "node:url";
import { main as checkKeys } from "../check-keys.ts";
import { main as genKeys } from "../gen-release-keys.ts";
import { makeKeysDir, makeReleaseKey, tempDir } from "./helpers.ts";

const repo = fileURLToPath(new URL("../../../", import.meta.url));
const SENTINEL = [
  '#include "gadget_ota.h"',
  "const gadget_release_key_t gadget_release_keys[] = {{NULL, {0}}};",
  "const size_t gadget_release_keys_count = 0;",
  "",
].join("\n");

/** A root holding core's real headers and keys_test.c, a chosen keys_release.c and keys/. */
async function root(keysRelease: string | null, keys = [makeReleaseKey("r1")]) {
  const dir = await tempDir("omb-keys-root-");
  await cp(join(repo, "firmware/core/include"), join(dir, "firmware/core/include"), { recursive: true });
  await mkdir(join(dir, "firmware/core/src"), { recursive: true });
  await cp(join(repo, "firmware/core/src/keys_test.c"), join(dir, "firmware/core/src/keys_test.c"));
  await makeKeysDir(join(dir, "keys"), keys);
  if (keysRelease !== null) await writeFile(join(dir, "firmware/core/src/keys_release.c"), keysRelease);
  return dir;
}

test("check-keys fails on the empty sentinel table (no release key yet)", async () => {
  const dir = await root(SENTINEL);
  assert.equal(await checkKeys(["--root", dir]), 1);
});

test("gen-release-keys output passes check-keys", async () => {
  const dir = await root(null, [makeReleaseKey("r1"), makeReleaseKey("r2")]);
  assert.equal(await genKeys(["--root", dir]), 0);
  assert.match(await readFile(join(dir, "firmware/core/src/keys_release.c"), "utf8"), /gadget_release_keys_count = 2;/);
  assert.equal(await checkKeys(["--root", dir]), 0);
});

test("check-keys fails when a committed public key changes after generation", async () => {
  const dir = await root(null);
  assert.equal(await genKeys(["--root", dir]), 0);
  await makeKeysDir(join(dir, "keys"), [makeReleaseKey("r1")]);
  assert.equal(await checkKeys(["--root", dir]), 1);
});

test("a keys/retired/ folder is ignored", async () => {
  const dir = await root(null);
  await mkdir(join(dir, "keys/retired"), { recursive: true });
  await writeFile(join(dir, "keys/retired/release-r9.pub.b64"), "x\n");
  assert.equal(await genKeys(["--root", dir]), 0);
  assert.equal(await checkKeys(["--root", dir]), 0);
});
