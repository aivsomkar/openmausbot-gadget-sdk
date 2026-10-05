// SPDX-License-Identifier: Apache-2.0
// Turns one board's ESP-IDF build folder into release assets plus a meta file
// with its install.json entry, after checking the image (spec §8, contract §4.3)
// and its partition table (spec §5.3).
//   node tools/release/collect.ts --board <id> --version <v> --build <dir> \
//        --assets <dir> --meta <dir> [--keys keys] \
//        [--partitions firmware/ports/esp32/partitions/16mb.csv] [--local]
// --local (installer tests with a local build): allows -dev and skips the key
// checks. The partition check always runs.
import { copyFile, mkdir, readFile, readdir, stat, writeFile } from "node:fs/promises";
import { join } from "node:path";
import { parseArgs } from "node:util";
import { fail, runIfMain } from "./cli.ts";
import {
  BOARDS,
  BUILD_FILES,
  OTA_SLOT_SIZE,
  PROJECT_NAME,
  VERSION_RE,
  assetNames,
  indexOfBytes,
  installEntryFromFlasherArgs,
  parseReleasePubFiles,
  readAppDescriptor,
  type BoardMeta,
} from "./lib.ts";
import { parsePartitionCsv, parsePartitionTable, partitionProblems } from "./partitions.ts";

export async function main(argv: string[]): Promise<number> {
  const { values } = parseArgs({
    args: argv,
    options: {
      board: { type: "string" },
      version: { type: "string" },
      build: { type: "string" },
      assets: { type: "string" },
      meta: { type: "string" },
      keys: { type: "string", default: "keys" },
      partitions: { type: "string", default: "firmware/ports/esp32/partitions/16mb.csv" },
      local: { type: "boolean", default: false },
    },
    strict: true,
  });
  const { board, version, build, assets, meta } = values;
  if (!board || !version || !build || !assets || !meta) {
    return fail("collect", "--board, --version, --build, --assets and --meta are required");
  }
  if (!(BOARDS as readonly string[]).includes(board)) return fail("collect", `unknown board ${board} (known: ${BOARDS.join(", ")})`);
  if (!VERSION_RE.test(version)) return fail("collect", `bad version ${version}`);
  if (!values.local && version.endsWith("-dev")) return fail("collect", `release versions never end in -dev (got ${version})`);

  let flashFiles: Array<[string, string]>;
  let install;
  try {
    const flasherArgs = JSON.parse(await readFile(join(build, "flasher_args.json"), "utf8")) as { flash_files: Record<string, string> };
    install = installEntryFromFlasherArgs(flasherArgs, board, version);
    flashFiles = Object.entries(flasherArgs.flash_files); // validated by installEntryFromFlasherArgs
  } catch (err) {
    return fail("collect", `${board}: ${(err as Error).message}`);
  }

  const app = new Uint8Array(await readFile(join(build, BUILD_FILES.app)));
  let desc;
  try {
    desc = readAppDescriptor(app);
  } catch (err) {
    return fail("collect", `${board}: ${(err as Error).message}`);
  }
  if (desc.version !== version) return fail("collect", `${board}: the image reports version ${desc.version}, expected ${version} (was PROJECT_VER passed?)`);
  if (desc.projectName !== PROJECT_NAME) return fail("collect", `${board}: the image's project is ${desc.projectName}, expected ${PROJECT_NAME}`);
  if (app.length > OTA_SLOT_SIZE) return fail("collect", `${board}: app is ${app.length} bytes, larger than the ${OTA_SLOT_SIZE}-byte OTA slot`);

  // Spec §5.3: one table for every board, unchanged across releases, or an
  // installer reinstall loses the gadget's identity, Wi-Fi and pairing.
  try {
    const images = await Promise.all(
      flashFiles.map(async ([offset, file]) => ({ file, offset: Number.parseInt(offset, 16), size: (await stat(join(build, file))).size })),
    );
    const problems = partitionProblems(
      parsePartitionTable(new Uint8Array(await readFile(join(build, BUILD_FILES.partitionTable)))),
      parsePartitionCsv(await readFile(values.partitions, "utf8")),
      images,
    );
    if (problems.length > 0) return fail("collect", `${board}: ${problems.join("; ")}`);
  } catch (err) {
    return fail("collect", `${board}: ${(err as Error).message}`);
  }

  if (!values.local) {
    const keyDir = values.keys;
    // Only the files at the top of keys/: a retired key lives in keys/retired/ (docs/release-keys.md).
    const names = (await readdir(keyDir, { withFileTypes: true })).filter((e) => e.isFile()).map((e) => e.name);
    const release = parseReleasePubFiles(
      await Promise.all(names.map(async (name) => ({ name, text: await readFile(join(keyDir, name), "utf8") }))),
    );
    if (release.size === 0) return fail("collect", "keys/ holds no release-r*.pub.b64: generate r1 first (docs/release-keys.md)");
    for (const [id, hex] of release) {
      if (indexOfBytes(app, Buffer.from(hex, "hex")) === -1) return fail("collect", `${board}: release key ${id} is not compiled into the image`);
    }
    const t1 = Buffer.from((await readFile(join(keyDir, "test-t1.pub.b64"), "utf8")).trim(), "base64");
    if (indexOfBytes(app, t1) !== -1) return fail("collect", `${board}: the test key t1 is compiled into a release image (CONFIG_GADGET_TEST_KEYS must be off)`);
  }

  const out = assetNames(board, version);
  await mkdir(assets, { recursive: true });
  await mkdir(meta, { recursive: true });
  const copies: Array<[string, string]> = [
    [BUILD_FILES.app, out.app],
    [BUILD_FILES.bootloader, out.bootloader],
    [BUILD_FILES.partitionTable, out.partitionTable],
    [BUILD_FILES.otaData, out.otaData],
    [BUILD_FILES.full, out.full],
  ];
  for (const [from, to] of copies) {
    const bytes = await readFile(join(build, from));
    if (bytes.length === 0) return fail("collect", `${board}: ${from} is empty`);
    await copyFile(join(build, from), join(assets, to));
  }
  const record: BoardMeta = { board, version, install };
  await writeFile(join(meta, `${board}.json`), `${JSON.stringify(record, null, 2)}\n`);
  console.log(`collect: ${board} ${version}: ${app.length} bytes, parts ${install.parts.map((p) => p.offset).join(" ")}`);
  return 0;
}

runIfMain(import.meta.url, main);
