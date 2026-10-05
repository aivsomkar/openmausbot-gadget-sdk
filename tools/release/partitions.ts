// SPDX-License-Identifier: Apache-2.0
// The partition-table check (spec §5.3, A36). An installer reinstall keeps NVS
// (the gadget's identity key, Wi-Fi and pairing) only while every release uses
// the same partitions/16mb.csv and no flashed image reaches into nvs or
// phy_init. Formats from ESP-IDF v6.0.3 components/partition_table/gen_esp32part.py.
import { createHash } from "node:crypto";
import { OTA_SLOT_SIZE } from "./lib.ts";

export interface Partition {
  name: string;
  type: number;
  subtype: number;
  offset: number;
  size: number;
  flags: number;
}

const APP = 0x00;
const DATA = 0x01;
const TYPES: Record<string, number> = { app: APP, data: DATA };
const SUBTYPES: Record<number, Record<string, number>> = {
  [APP]: { factory: 0x00, test: 0x20, ...Object.fromEntries(Array.from({ length: 16 }, (_, i) => [`ota_${i}`, 0x10 + i])) },
  [DATA]: { ota: 0x00, phy: 0x01, nvs: 0x02, coredump: 0x03, nvs_keys: 0x04, efuse: 0x05, undefined: 0x06, fat: 0x81, spiffs: 0x82, littlefs: 0x83 },
};
const FLAG_BITS: Record<string, number> = { encrypted: 1, readonly: 2 };
const NVS = 0x02;
const PHY = 0x01;

function csvNumber(text: string, what: string): number {
  const m = /^(0x[0-9a-f]+|\d+)([km])?$/i.exec(text);
  if (m === null || m[1] === undefined) throw new Error(`16mb.csv: ${what} ${JSON.stringify(text)} is not a number`);
  const unit = m[2] === undefined ? 1 : m[2].toLowerCase() === "k" ? 1024 : 1024 * 1024;
  return Number(m[1]) * unit;
}

function lookup(table: Record<string, number> | undefined, text: string, what: string): number {
  if (table !== undefined && Object.hasOwn(table, text)) return table[text] as number;
  return csvNumber(text, what);
}

/** Rows of a partitions CSV. Every row must give its offset: the release table is pinned (spec §5.3). */
export function parsePartitionCsv(text: string): Partition[] {
  const rows: Partition[] = [];
  for (const raw of text.split(/\r?\n/)) {
    const line = raw.trim();
    if (line === "" || line.startsWith("#")) continue;
    const [name = "", typeText = "", subtypeText = "", offsetText = "", sizeText = "", flagsText = ""] = line.split(",").map((s) => s.trim());
    if (name === "" || sizeText === "") throw new Error(`16mb.csv: bad row ${JSON.stringify(line)}`);
    if (offsetText === "") throw new Error(`16mb.csv: ${name} has no offset; the release table pins every offset (spec §5.3)`);
    const type = lookup(TYPES, typeText, `${name} type`);
    let flags = 0;
    for (const flag of flagsText.split(":").filter(Boolean)) {
      if (!Object.hasOwn(FLAG_BITS, flag)) throw new Error(`16mb.csv: ${name} has an unknown flag ${flag}`);
      flags |= FLAG_BITS[flag] as number;
    }
    rows.push({
      name,
      type,
      subtype: lookup(SUBTYPES[type], subtypeText, `${name} subtype`),
      offset: csvNumber(offsetText, `${name} offset`),
      size: csvNumber(sizeText, `${name} size`),
      flags,
    });
  }
  return rows;
}

/** Entries of a built partition-table.bin, after checking its MD5 entry. */
export function parsePartitionTable(bin: Uint8Array): Partition[] {
  const view = new DataView(bin.buffer, bin.byteOffset, bin.byteLength);
  const rows: Partition[] = [];
  for (let at = 0; at + 32 <= bin.length; at += 32) {
    if (bin[at] === 0xaa && bin[at + 1] === 0x50) {
      const label = bin.subarray(at + 12, at + 28);
      const nul = label.indexOf(0);
      rows.push({
        name: new TextDecoder().decode(nul === -1 ? label : label.subarray(0, nul)),
        type: view.getUint8(at + 2),
        subtype: view.getUint8(at + 3),
        offset: view.getUint32(at + 4, true),
        size: view.getUint32(at + 8, true),
        flags: view.getUint32(at + 28, true),
      });
      continue;
    }
    if (bin[at] === 0xeb && bin[at + 1] === 0xeb) {
      const want = Buffer.from(bin.subarray(at + 16, at + 32)).toString("hex");
      if (createHash("md5").update(bin.subarray(0, at)).digest("hex") !== want) {
        throw new Error("partition-table.bin: its MD5 entry does not match the table");
      }
      return rows;
    }
    if (bin[at] === 0xff && bin[at + 1] === 0xff) return rows;
    throw new Error(`partition-table.bin: unexpected bytes at 0x${at.toString(16)}`);
  }
  return rows;
}

const describe = (p: Partition): string =>
  `${p.name} (type 0x${p.type.toString(16)}, subtype 0x${p.subtype.toString(16)}, 0x${p.offset.toString(16)}, 0x${p.size.toString(16)} bytes, flags ${p.flags})`;

/**
 * Why this build would break an installer reinstall or OTA; empty = OK. The
 * built table must equal 16mb.csv, both OTA slots must be OTA_SLOT_SIZE, and no
 * flashed image may overlap nvs or phy_init.
 */
export function partitionProblems(
  built: readonly Partition[],
  csv: readonly Partition[],
  images: ReadonlyArray<{ file: string; offset: number; size: number }>,
): string[] {
  const problems: string[] = [];
  for (let i = 0; i < Math.max(built.length, csv.length); i++) {
    const b = built[i];
    const c = csv[i];
    if (b === undefined || c === undefined || describe(b) !== describe(c)) {
      problems.push(`the built partition table differs from 16mb.csv at entry ${i + 1}: built ${b ? describe(b) : "nothing"}, 16mb.csv ${c ? describe(c) : "nothing"}`);
      break;
    }
  }
  for (const subtype of [0x10, 0x11]) {
    const slot = built.find((p) => p.type === APP && p.subtype === subtype);
    if (slot === undefined) problems.push(`the partition table has no ota_${subtype - 0x10}`);
    else if (slot.size !== OTA_SLOT_SIZE) problems.push(`${slot.name} is ${slot.size} bytes, not the ${OTA_SLOT_SIZE}-byte OTA slot every board advertises`);
  }
  const kept = built.filter((p) => p.type === DATA && (p.subtype === NVS || p.subtype === PHY));
  if (!kept.some((p) => p.subtype === NVS)) problems.push("the partition table has no nvs partition");
  for (const img of images) {
    for (const p of kept) {
      if (img.offset < p.offset + p.size && p.offset < img.offset + img.size) {
        problems.push(`${img.file} (0x${img.offset.toString(16)}, ${img.size} bytes) overlaps ${p.name} (0x${p.offset.toString(16)}-0x${(p.offset + p.size).toString(16)}), so a reinstall would erase it`);
      }
    }
  }
  return problems;
}
