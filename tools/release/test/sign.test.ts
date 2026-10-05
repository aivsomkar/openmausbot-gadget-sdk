// SPDX-License-Identifier: Apache-2.0
import assert from "node:assert/strict";
import { createHash } from "node:crypto";
import { readFile, readdir, writeFile } from "node:fs/promises";
import { join } from "node:path";
import { test } from "node:test";
import { firmwareText } from "../../../protocol/lib/identity.ts";
import { verifyP256 } from "../../../protocol/lib/verify.ts";
import { main as collect } from "../collect.ts";
import { BOARDS, RELEASE_REPO } from "../lib.ts";
import { main as sign } from "../sign.ts";
import { makeBuildDir, makeKeysDir, makeReleaseKey, tempDir, writePartitionsCsv, type TestReleaseKey } from "./helpers.ts";

async function release(version: string, key: TestReleaseKey = makeReleaseKey("r1")) {
  const root = await tempDir();
  const keys = await makeKeysDir(join(root, "keys"), [key]);
  const csv = await writePartitionsCsv(join(root, "partitions"));
  const assets = join(root, "out/assets");
  const meta = join(root, "out/meta");
  for (const board of BOARDS) {
    const build = await makeBuildDir(join(root, "raw", board), { version, embed: [key.pub] });
    const collected = await collect(["--board", board, "--version", version, "--build", build, "--assets", assets, "--meta", meta, "--keys", keys, "--partitions", csv]);
    assert.equal(collected, 0);
  }
  const argv = ["--assets", assets, "--meta", meta, "--tag", `v${version}`, "--repo", RELEASE_REPO, "--key-id", key.id, "--key-env", "KEY", "--pub", join(keys, `release-${key.id}.pub.b64`)];
  return { root, keys, assets, meta, key, argv, env: { KEY: key.pem } };
}

test("sign writes a manifest whose signatures verify, plus install.json and SHA256SUMS", async () => {
  const r = await release("1.1.0");
  assert.equal(await sign(r.argv, r.env), 0);
  const manifest = JSON.parse(await readFile(join(r.assets, "manifest.json"), "utf8"));
  assert.equal(manifest.version, "1.1.0");
  assert.deepEqual(Object.keys(manifest.boards), [...BOARDS]);
  for (const board of BOARDS) {
    const entry = manifest.boards[board];
    const image = await readFile(join(r.assets, `openmausbot-gadget-${board}-1.1.0.bin`));
    assert.equal(entry.url, `https://github.com/aivsomkar/openmausbot-gadget-sdk/releases/download/v1.1.0/openmausbot-gadget-${board}-1.1.0.bin`);
    assert.equal(entry.size, image.length);
    assert.equal(entry.sha256, createHash("sha256").update(image).digest("hex"));
    assert.equal(entry.key_id, "r1");
    const sig = new Uint8Array(Buffer.from(entry.sig, "base64"));
    assert.equal(Buffer.from(sig).toString("base64"), entry.sig);
    assert.equal(verifyP256(r.key.pub, firmwareText(board, "1.1.0", entry.size, entry.sha256), sig), true);
    assert.equal(verifyP256(r.key.pub, firmwareText("lcd-154" === board ? "devkit" : "lcd-154", "1.1.0", entry.size, entry.sha256), sig), false);
  }
  const install = JSON.parse(await readFile(join(r.assets, "install.json"), "utf8"));
  assert.equal(install.version, "1.1.0");
  assert.equal(install.boards.devkit.full, "openmausbot-gadget-devkit-1.1.0-full.bin");

  const sums = await readFile(join(r.assets, "SHA256SUMS"), "utf8");
  const lines = sums.trimEnd().split("\n");
  const names = (await readdir(r.assets)).filter((n) => n !== "SHA256SUMS").sort();
  assert.deepEqual(lines.map((l) => l.split("  ")[1]), names);
  for (const line of lines) {
    const [hash, name] = line.split("  ");
    assert.equal(hash, createHash("sha256").update(await readFile(join(r.assets, name))).digest("hex"));
  }
});

test("sign accepts a prerelease tag", async () => {
  const r = await release("1.2.0-rc.1");
  assert.equal(await sign(r.argv, r.env), 0);
});

test("sign refuses a secret whose public half is not the committed key", async () => {
  const r = await release("1.1.0");
  assert.equal(await sign(r.argv, { KEY: makeReleaseKey("r1").pem }), 1);
});

test("sign refuses an empty secret, a test key id and a -dev tag", async () => {
  const r = await release("1.1.0");
  assert.equal(await sign(r.argv, {}), 1);
  const t = [...r.argv];
  t[t.indexOf("--key-id") + 1] = "t1";
  assert.equal(await sign(t, r.env), 1);
  const d = [...r.argv];
  d[d.indexOf("--tag") + 1] = "v1.1.0-dev";
  assert.equal(await sign(d, r.env), 1);
});

test("sign refuses a stray file in the asset folder", async () => {
  const r = await release("1.1.0");
  await writeFile(join(r.assets, "notes.txt"), "hello");
  assert.equal(await sign(r.argv, r.env), 1);
});

test("sign refuses when a board is missing from the build", async () => {
  const r = await release("1.1.0");
  const argv = [...r.argv, "--boards", "amoled-175c,amoled-175,lcd-154,devkit,extra-board"];
  assert.equal(await sign(argv, r.env), 1);
});

test("sign refuses a repository other than the one the manifest URLs are pinned to", async () => {
  const r = await release("1.1.0");
  const argv = [...r.argv];
  argv[argv.indexOf("--repo") + 1] = "other/name";
  assert.equal(await sign(argv, r.env), 1);
});
