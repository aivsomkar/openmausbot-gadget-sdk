// SPDX-License-Identifier: Apache-2.0
import assert from "node:assert/strict";
import { readFile, readdir } from "node:fs/promises";
import { join } from "node:path";
import { test } from "node:test";
import { main } from "../collect.ts";
import { PARTITION_ROWS, T1_PUB_B64, makeBuildDir, makeKeysDir, makeReleaseKey, tempDir, writePartitionsCsv, type PartitionRow } from "./helpers.ts";

const t1 = new Uint8Array(Buffer.from(T1_PUB_B64, "base64"));

interface SetupOptions {
  version?: string;
  embedRelease?: boolean;
  embedT1?: boolean;
  appSize?: number;
  project?: string;
  partitions?: readonly PartitionRow[];
  bootloaderSize?: number;
}

async function setup(opts: SetupOptions = {}) {
  const root = await tempDir();
  const key = makeReleaseKey("r1");
  const embed: Uint8Array[] = [];
  if (opts.embedRelease ?? true) embed.push(key.pub);
  if (opts.embedT1) embed.push(t1);
  const build = await makeBuildDir(join(root, "build"), {
    version: opts.version ?? "1.1.0",
    embed,
    appSize: opts.appSize,
    project: opts.project,
    partitions: opts.partitions,
    bootloaderSize: opts.bootloaderSize,
  });
  const keys = await makeKeysDir(join(root, "keys"), [key]);
  const csv = await writePartitionsCsv(join(root, "partitions"));
  return { root, build, keys, csv, assets: join(root, "out/assets"), meta: join(root, "out/meta") };
}

function args(s: Awaited<ReturnType<typeof setup>>, extra: string[] = [], version = "1.1.0"): string[] {
  return ["--board", "amoled-175c", "--version", version, "--build", s.build, "--assets", s.assets, "--meta", s.meta, "--keys", s.keys, "--partitions", s.csv, ...extra];
}

test("collect copies the five assets and writes the board's install entry", async () => {
  const s = await setup();
  assert.equal(await main(args(s)), 0);
  assert.deepEqual((await readdir(s.assets)).sort(), [
    "openmausbot-gadget-amoled-175c-1.1.0-bootloader.bin",
    "openmausbot-gadget-amoled-175c-1.1.0-full.bin",
    "openmausbot-gadget-amoled-175c-1.1.0-ota-data-initial.bin",
    "openmausbot-gadget-amoled-175c-1.1.0-partition-table.bin",
    "openmausbot-gadget-amoled-175c-1.1.0.bin",
  ]);
  const meta = JSON.parse(await readFile(join(s.meta, "amoled-175c.json"), "utf8"));
  assert.equal(meta.version, "1.1.0");
  assert.deepEqual(meta.install.parts.map((p: { offset: string }) => p.offset), ["0x0", "0x8000", "0xf000", "0x20000"]);
});

test("collect refuses an image whose embedded version is not the tag's", async () => {
  const s = await setup({ version: "0.0.0-dev" });
  assert.equal(await main(args(s)), 1);
});

test("collect refuses a -dev version unless --local", async () => {
  const s = await setup({ version: "0.0.0-dev", embedRelease: false });
  assert.equal(await main(args(s, [], "0.0.0-dev")), 1);
  assert.equal(await main(args(s, ["--local"], "0.0.0-dev")), 0);
});

test("collect refuses a release image that carries the test key t1", async () => {
  const s = await setup({ embedT1: true });
  assert.equal(await main(args(s)), 1);
});

test("collect refuses a release image without the release key", async () => {
  const s = await setup({ embedRelease: false });
  assert.equal(await main(args(s)), 1);
});

test("collect refuses an app larger than the OTA slot", async () => {
  const s = await setup({ appSize: 6291457 });
  assert.equal(await main(args(s)), 1);
});

test("collect refuses another project's image and an unknown board", async () => {
  const s = await setup({ project: "hello_world" });
  assert.equal(await main(args(s)), 1);
  const s2 = await setup();
  const a = args(s2);
  a[1] = "esp32-c3";
  assert.equal(await main(a), 1);
});

test("collect refuses a table that differs from 16mb.csv", async () => {
  const smallerNvs = PARTITION_ROWS.map((r) => (r.name === "nvs" ? { ...r, size: 0x5000 } : r));
  const s = await setup({ partitions: smallerNvs });
  assert.equal(await main(args(s)), 1);
});

test("collect refuses a part that overlaps nvs", async () => {
  const s = await setup({ bootloaderSize: 0x9400 }); // flashed at 0x0, so it reaches past 0x9000
  assert.equal(await main(args(s)), 1);
});
