// SPDX-License-Identifier: Apache-2.0
// Test fixtures for tools/release: fake ESP-IDF build folders and keys.
import { createHash, generateKeyPairSync } from "node:crypto";
import { mkdir, mkdtemp, writeFile } from "node:fs/promises";
import { tmpdir } from "node:os";
import { join } from "node:path";

export const T1_PUB_B64 = "BIUTH1UeVJw2VXn0Ehdhd8apNOHmB6VvjV512SxIbCCXfUhvlLRTuOTLHtrsVAbIx06JhMoOMbC3ic73hZpxMwg=";

/** firmware/ports/esp32/partitions/16mb.csv as spec §5.3 and P2c write it. */
export const PARTITIONS_CSV = [
  "# Name,   Type, SubType,  Offset,   Size,     Flags",
  "nvs,      data, nvs,      0x9000,   0x6000,",
  "otadata,  data, ota,      0xf000,   0x2000,",
  "phy_init, data, phy,      0x11000,  0x1000,",
  "ota_0,    app,  ota_0,    0x20000,  0x600000,",
  "ota_1,    app,  ota_1,    0x620000, 0x600000,",
  "coredump, data, coredump, 0xc20000, 0x10000,",
  "",
].join("\n");

export interface PartitionRow {
  name: string;
  type: number;
  subtype: number;
  offset: number;
  size: number;
  flags?: number;
}

/** The same table as numbers (type and subtype codes from ESP-IDF's gen_esp32part.py). */
export const PARTITION_ROWS: readonly PartitionRow[] = [
  { name: "nvs", type: 1, subtype: 2, offset: 0x9000, size: 0x6000 },
  { name: "otadata", type: 1, subtype: 0, offset: 0xf000, size: 0x2000 },
  { name: "phy_init", type: 1, subtype: 1, offset: 0x11000, size: 0x1000 },
  { name: "ota_0", type: 0, subtype: 0x10, offset: 0x20000, size: 0x600000 },
  { name: "ota_1", type: 0, subtype: 0x11, offset: 0x620000, size: 0x600000 },
  { name: "coredump", type: 1, subtype: 3, offset: 0xc20000, size: 0x10000 },
];

/**
 * partition-table.bin as ESP-IDF v6.0.3 writes it: one 32-byte entry per row
 * (bytes AA 50, type, subtype, offset u32 LE, size u32 LE, 16-byte name, flags
 * u32 LE), then the MD5 entry (EB EB, 14 × FF, MD5 of the entries), then 0xFF
 * up to 0xC00 bytes. For PARTITION_ROWS it is byte-identical to
 * `gen_esp32part.py --flash-size 16MB 16mb.csv`.
 */
export function partitionTableBin(rows: readonly PartitionRow[] = PARTITION_ROWS): Uint8Array {
  const out = new Uint8Array(0xc00).fill(0xff);
  const view = new DataView(out.buffer);
  rows.forEach((r, i) => {
    const at = i * 32;
    out.set([0xaa, 0x50, r.type, r.subtype], at);
    view.setUint32(at + 4, r.offset, true);
    view.setUint32(at + 8, r.size, true);
    out.fill(0, at + 12, at + 28);
    out.set(new TextEncoder().encode(r.name), at + 12);
    view.setUint32(at + 28, r.flags ?? 0, true);
  });
  const end = rows.length * 32;
  out.set([0xeb, 0xeb], end);
  out.set(createHash("md5").update(out.subarray(0, end)).digest(), end + 16);
  return out;
}

/** Writes PARTITIONS_CSV as <dir>/16mb.csv and returns its path (collect.ts --partitions). */
export async function writePartitionsCsv(dir: string): Promise<string> {
  await mkdir(dir, { recursive: true });
  const path = join(dir, "16mb.csv");
  await writeFile(path, PARTITIONS_CSV);
  return path;
}

/** flasher_args.json as ESP-IDF v6.0.3 writes it for partitions/16mb.csv. */
export const FLASHER_ARGS = {
  write_flash_args: ["--flash-mode", "dio", "--flash-size", "16MB", "--flash-freq", "80m"],
  flash_settings: { flash_mode: "dio", flash_size: "16MB", flash_freq: "80m" },
  flash_files: {
    "0x0": "bootloader/bootloader.bin",
    "0x20000": "openmausbot-gadget.bin",
    "0x8000": "partition_table/partition-table.bin",
    "0xf000": "ota_data_initial.bin",
  },
  bootloader: { offset: "0x0", file: "bootloader/bootloader.bin", encrypted: "false" },
  app: { offset: "0x20000", file: "openmausbot-gadget.bin", encrypted: "false" },
  "partition-table": { offset: "0x8000", file: "partition_table/partition-table.bin", encrypted: "false" },
  otadata: { offset: "0xf000", file: "ota_data_initial.bin", encrypted: "false" },
  extra_esptool_args: { after: "hard-reset", before: "default-reset", stub: true, chip: "esp32s3" },
};

export async function tempDir(prefix = "omb-release-"): Promise<string> {
  return mkdtemp(join(tmpdir(), prefix));
}

/** An app image with a valid header and app descriptor; `embed` byte strings are placed after it. */
export function fakeAppImage(version: string, embed: Uint8Array[] = [], size = 8192, project = "openmausbot-gadget"): Uint8Array {
  const img = new Uint8Array(size);
  img[0] = 0xe9;
  img[1] = 4;
  new DataView(img.buffer).setUint32(32, 0xabcd5432, true);
  img.set(new TextEncoder().encode(version), 48);
  img.set(new TextEncoder().encode(project), 80);
  let at = 512;
  for (const bytes of embed) {
    img.set(bytes, at);
    at += bytes.length + 16;
  }
  return img;
}

export interface FakeBuild {
  version: string;
  embed?: Uint8Array[];
  appSize?: number;
  flasherArgs?: unknown;
  project?: string;
  /** Rows of the built partition table (default: PARTITION_ROWS, i.e. 16mb.csv). */
  partitions?: readonly PartitionRow[];
  /** Size of bootloader/bootloader.bin (default 1024). */
  bootloaderSize?: number;
}

export async function makeBuildDir(dir: string, opts: FakeBuild): Promise<string> {
  await mkdir(join(dir, "bootloader"), { recursive: true });
  await mkdir(join(dir, "partition_table"), { recursive: true });
  await writeFile(join(dir, "flasher_args.json"), JSON.stringify(opts.flasherArgs ?? FLASHER_ARGS, null, 4));
  await writeFile(join(dir, "openmausbot-gadget.bin"), fakeAppImage(opts.version, opts.embed ?? [], opts.appSize ?? 8192, opts.project));
  await writeFile(join(dir, "bootloader/bootloader.bin"), new Uint8Array(opts.bootloaderSize ?? 1024).fill(0xb0));
  await writeFile(join(dir, "partition_table/partition-table.bin"), partitionTableBin(opts.partitions));
  await writeFile(join(dir, "ota_data_initial.bin"), new Uint8Array(8192).fill(0xff));
  await writeFile(join(dir, "merged.bin"), new Uint8Array(16384).fill(0x5a));
  return dir;
}

export interface TestReleaseKey {
  id: string;
  pem: string;
  pub: Uint8Array;
  pubB64: string;
}

export function makeReleaseKey(id: string): TestReleaseKey {
  const { privateKey, publicKey } = generateKeyPairSync("ec", { namedCurve: "prime256v1" });
  const pub = new Uint8Array(publicKey.export({ type: "spki", format: "der" }).subarray(-65));
  return { id, pem: privateKey.export({ type: "pkcs8", format: "pem" }) as string, pub, pubB64: Buffer.from(pub).toString("base64") };
}

/** keys/ with release-<id>.pub.b64 for each key plus the committed test key. */
export async function makeKeysDir(dir: string, keys: TestReleaseKey[]): Promise<string> {
  await mkdir(dir, { recursive: true });
  for (const k of keys) await writeFile(join(dir, `release-${k.id}.pub.b64`), `${k.pubB64}\n`);
  await writeFile(join(dir, "test-t1.pub.b64"), `${T1_PUB_B64}\n`);
  return dir;
}
