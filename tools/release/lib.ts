// SPDX-License-Identifier: Apache-2.0
// Pure helpers for release.yml (spec §8, contract §4.1–§4.3). No I/O here:
// the CLIs in this folder read and write files and call these functions.
import { createHash } from "node:crypto";

export const BOARDS = ["amoled-175c", "amoled-175", "lcd-154", "devkit"] as const;
export const RELEASE_REPO = "aivsomkar/openmausbot-gadget-sdk";
export const OTA_SLOT_SIZE = 6291456;
export const PROJECT_NAME = "openmausbot-gadget";
export const VERSION_RE = /^\d+\.\d+\.\d+(-[0-9A-Za-z.]+)?$/;
export const BOARD_RE = /^[a-z0-9-]{1,32}$/;
export const RELEASE_KEY_ID_RE = /^r[0-9]+$/;

/** Build-directory files, relative to build/<board>/ (contract §4.3). */
export const BUILD_FILES = {
  app: "openmausbot-gadget.bin",
  bootloader: "bootloader/bootloader.bin",
  partitionTable: "partition_table/partition-table.bin",
  otaData: "ota_data_initial.bin",
  full: "merged.bin",
} as const;

export type PartKind = "app" | "bootloader" | "partitionTable" | "otaData";

export interface AssetNames {
  app: string;
  bootloader: string;
  partitionTable: string;
  otaData: string;
  full: string;
}

export interface InstallPart {
  path: string;
  offset: string;
}

export interface InstallBoard {
  parts: InstallPart[];
  full: string;
}

export interface InstallIndexOut {
  version: string | null;
  boards: Record<string, InstallBoard>;
}

export interface ManifestBoard {
  url: string;
  size: number;
  sha256: string;
  sig: string;
  key_id: string;
}

export interface Manifest {
  version: string;
  boards: Record<string, ManifestBoard>;
}

export interface BoardMeta {
  board: string;
  version: string;
  install: InstallBoard;
}

export interface KeyEntry {
  id: string | null;
  pub: string; // lowercase hex of the 65-byte SEC1 point
}

export interface KeyTables {
  release: KeyEntry[];
  test: KeyEntry[];
}

export interface AppDescriptor {
  version: string;
  projectName: string;
}

/** `v1.1.0` → `{version: "1.1.0", prerelease: false}`. Throws on anything a release must not carry. */
export function parseTag(tag: string): { version: string; prerelease: boolean } {
  if (!tag.startsWith("v")) throw new Error(`tag ${JSON.stringify(tag)} must start with "v"`);
  const version = tag.slice(1);
  if (!VERSION_RE.test(version)) throw new Error(`tag ${JSON.stringify(tag)} is not v<major>.<minor>.<patch>[-<pre>]`);
  if (version.endsWith("-dev")) throw new Error(`release versions never end in -dev (got ${version})`);
  return { version, prerelease: version.includes("-") };
}

export function assetNames(board: string, version: string): AssetNames {
  if (!BOARD_RE.test(board)) throw new Error(`bad board id ${JSON.stringify(board)}`);
  const p = `openmausbot-gadget-${board}-${version}`;
  return {
    app: `${p}.bin`,
    bootloader: `${p}-bootloader.bin`,
    partitionTable: `${p}-partition-table.bin`,
    otaData: `${p}-ota-data-initial.bin`,
    full: `${p}-full.bin`,
  };
}

export function releaseDownloadBase(repo: string): string {
  if (!/^[A-Za-z0-9_.-]+\/[A-Za-z0-9_.-]+$/.test(repo)) throw new Error(`bad repository ${JSON.stringify(repo)}`);
  return `https://github.com/${repo}/releases/download/`;
}

export function manifestUrl(repo: string, board: string, version: string): string {
  return `${releaseDownloadBase(repo)}v${version}/${assetNames(board, version).app}`;
}

const PART_BY_BUILD_FILE: Record<string, PartKind> = {
  [BUILD_FILES.app]: "app",
  [BUILD_FILES.bootloader]: "bootloader",
  [BUILD_FILES.partitionTable]: "partitionTable",
  [BUILD_FILES.otaData]: "otaData",
};

/**
 * The install.json entry for one board, from its build/<board>/flasher_args.json.
 * Offsets are copied verbatim from `flash_files`; parts come out in flash-address order.
 */
export function installEntryFromFlasherArgs(flasherArgs: unknown, board: string, version: string): InstallBoard {
  if (typeof flasherArgs !== "object" || flasherArgs === null) throw new Error("flasher_args.json is not an object");
  const fa = flasherArgs as { flash_files?: unknown; extra_esptool_args?: { chip?: unknown } };
  if (fa.extra_esptool_args?.chip !== "esp32s3") {
    throw new Error(`flasher_args.json chip is ${JSON.stringify(fa.extra_esptool_args?.chip)}, expected "esp32s3"`);
  }
  if (typeof fa.flash_files !== "object" || fa.flash_files === null) throw new Error("flasher_args.json has no flash_files");
  const names = assetNames(board, version);
  const seen = new Set<PartKind>();
  const parts: Array<InstallPart & { address: number }> = [];
  for (const [offset, file] of Object.entries(fa.flash_files as Record<string, unknown>)) {
    if (!/^0x[0-9a-fA-F]+$/.test(offset)) throw new Error(`flash_files offset ${JSON.stringify(offset)} is not hex`);
    if (typeof file !== "string") throw new Error(`flash_files[${offset}] is not a file name`);
    const kind = PART_BY_BUILD_FILE[file];
    if (kind === undefined) throw new Error(`flash_files has an unexpected image ${JSON.stringify(file)}`);
    if (seen.has(kind)) throw new Error(`flash_files lists ${file} twice`);
    seen.add(kind);
    parts.push({ path: names[kind], offset, address: Number.parseInt(offset, 16) });
  }
  for (const kind of ["bootloader", "partitionTable", "otaData", "app"] as const) {
    if (!seen.has(kind)) throw new Error(`flash_files is missing ${BUILD_FILES[kind]}`);
  }
  parts.sort((a, b) => a.address - b.address);
  return { parts: parts.map(({ path, offset }) => ({ path, offset })), full: names.full };
}

export function buildInstallIndex(version: string, metas: readonly BoardMeta[]): InstallIndexOut {
  const order = (board: string): number => (BOARDS as readonly string[]).indexOf(board);
  const boards: Record<string, InstallBoard> = {};
  for (const meta of [...metas].sort((a, b) => order(a.board) - order(b.board))) {
    if (meta.version !== version) throw new Error(`${meta.board} was built as ${meta.version}, not ${version}`);
    if (boards[meta.board]) throw new Error(`${meta.board} appears twice`);
    boards[meta.board] = meta.install;
  }
  return { version, boards };
}

/** esp_app_desc_t sits at sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t) = 32. */
export function readAppDescriptor(image: Uint8Array): AppDescriptor {
  if (image.length < 112 || image[0] !== 0xe9) throw new Error("not an ESP app image (magic 0xE9 missing)");
  const view = new DataView(image.buffer, image.byteOffset, image.byteLength);
  if (view.getUint32(32, true) !== 0xabcd5432) throw new Error("app descriptor magic 0xABCD5432 missing at offset 32");
  return { version: cString(image.subarray(48, 80)), projectName: cString(image.subarray(80, 112)) };
}

function cString(bytes: Uint8Array): string {
  const end = bytes.indexOf(0);
  return new TextDecoder().decode(end === -1 ? bytes : bytes.subarray(0, end));
}

export function indexOfBytes(haystack: Uint8Array, needle: Uint8Array): number {
  if (needle.length === 0) return 0;
  outer: for (let i = haystack.indexOf(needle[0]); i !== -1 && i <= haystack.length - needle.length; i = haystack.indexOf(needle[0], i + 1)) {
    for (let j = 1; j < needle.length; j++) if (haystack[i + j] !== needle[j]) continue outer;
    return i;
  }
  return -1;
}

export function sha256Hex(bytes: Uint8Array): string {
  return createHash("sha256").update(bytes).digest("hex");
}

/** GNU sha256sum text format: "<hex>  <name>\n", sorted by name. */
export function sha256sumsText(files: ReadonlyArray<{ name: string; sha256: string }>): string {
  return [...files]
    .sort((a, b) => (a.name < b.name ? -1 : a.name > b.name ? 1 : 0))
    .map((f) => `${f.sha256}  ${f.name}\n`)
    .join("");
}

/** Errors in the compiled key tables against keys/release-*.pub.b64 (decoded, as hex). Empty = OK. */
export function checkKeyTables(tables: KeyTables, releasePubFiles: ReadonlyMap<string, string>): string[] {
  const errors: string[] = [];
  if (tables.release.length === 0) {
    errors.push("the release key table is empty: generate r1 and run tools/release/gen-release-keys.ts (docs/release-keys.md)");
  }
  const ids = new Set<string>();
  for (const key of tables.release) {
    if (key.id === null || !RELEASE_KEY_ID_RE.test(key.id)) {
      errors.push(`release key id ${JSON.stringify(key.id)} does not match /^r[0-9]+$/`);
      continue;
    }
    if (ids.has(key.id)) errors.push(`release key ${key.id} appears twice`);
    ids.add(key.id);
    const file = releasePubFiles.get(key.id);
    if (file === undefined) errors.push(`release key ${key.id} has no keys/release-${key.id}.pub.b64`);
    else if (file !== key.pub) errors.push(`release key ${key.id} differs from keys/release-${key.id}.pub.b64`);
  }
  for (const id of releasePubFiles.keys()) {
    if (!ids.has(id)) errors.push(`keys/release-${id}.pub.b64 is not in firmware/core/src/keys_release.c`);
  }
  if (tables.test.length !== 0) {
    errors.push(`the test key table holds ${tables.test.map((k) => k.id).join(", ")} without GADGET_TEST_KEYS`);
  }
  return errors;
}

/** The text of firmware/core/src/keys_release.c for these keys (sorted by number). */
export function keysReleaseC(keys: ReadonlyArray<{ id: string; pub: Uint8Array }>): string {
  const sorted = [...keys].sort((a, b) => Number(a.id.slice(1)) - Number(b.id.slice(1)));
  for (const k of sorted) {
    if (!RELEASE_KEY_ID_RE.test(k.id)) throw new Error(`bad release key id ${k.id}`);
    if (k.pub.length !== 65 || k.pub[0] !== 0x04) throw new Error(`release key ${k.id} is not a 65-byte SEC1 point`);
  }
  const head = [
    "/* firmware/core/src/keys_release.c */",
    "/* SPDX-License-Identifier: Apache-2.0 */",
    "/* Release public keys (spec §8). Written by tools/release/gen-release-keys.ts",
    " * from keys/release-*.pub.b64; see docs/release-keys.md. Do not edit by hand. */",
    '#include "gadget_ota.h"',
    "",
  ];
  if (sorted.length === 0) {
    return [...head, "const gadget_release_key_t gadget_release_keys[] = {{NULL, {0}}};", "const size_t gadget_release_keys_count = 0;", ""].join("\n");
  }
  const rows = sorted.map((k) => {
    const bytes = [...k.pub].map((b) => `0x${b.toString(16).padStart(2, "0")}`);
    const lines: string[] = [];
    for (let i = 0; i < bytes.length; i += 12) lines.push(`      ${bytes.slice(i, i + 12).join(", ")}`);
    return `    {"${k.id}",\n     {\n${lines.join(",\n")}\n     }}`;
  });
  return [
    ...head,
    "const gadget_release_key_t gadget_release_keys[] = {",
    `${rows.join(",\n")},`,
    "};",
    `const size_t gadget_release_keys_count = ${sorted.length};`,
    "",
  ].join("\n");
}

/** Release public key files: keys/release-<id>.pub.b64 → id → decoded hex. Throws on a malformed file. */
export function parseReleasePubFiles(files: ReadonlyArray<{ name: string; text: string }>): Map<string, string> {
  const out = new Map<string, string>();
  for (const f of files) {
    const m = /^release-(r[0-9]+)\.pub\.b64$/.exec(f.name);
    if (!m) continue;
    const text = f.text.endsWith("\n") ? f.text.slice(0, -1) : f.text;
    const bytes = Buffer.from(text, "base64");
    if (bytes.toString("base64") !== text || bytes.length !== 65 || bytes[0] !== 0x04) {
      throw new Error(`keys/${f.name} is not canonical base64 of a 65-byte SEC1 point followed by one newline`);
    }
    out.set(m[1], bytes.toString("hex"));
  }
  return out;
}
