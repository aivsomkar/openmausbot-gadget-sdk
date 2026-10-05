// SPDX-License-Identifier: Apache-2.0
// firmware/install.json (spec §5.8, contract §4.2): parse it strictly, then
// turn one board's entry into esptool-js write addresses.

export interface InstallIndex {
  version: string | null;
  boards: Record<string, { parts: Array<{ path: string; offset: string }>; full: string }>;
}

export const FIRMWARE_BASE = "firmware/";
const VERSION_RE = /^\d+\.\d+\.\d+(-[0-9A-Za-z.]+)?$/;
const BOARD_RE = /^[a-z0-9-]{1,32}$/;
const FILE_RE = /^[A-Za-z0-9][A-Za-z0-9._-]*$/;
const OFFSET_RE = /^0x[0-9a-fA-F]{1,8}$/;
const FLASH_LIMIT = 16 * 1024 * 1024;
const SECTOR = 0x1000;

class InstallIndexError extends Error {
  constructor(message: string) {
    super(`install.json: ${message}`);
    this.name = "InstallIndexError";
  }
}

function isObj(v: unknown): v is Record<string, unknown> {
  return typeof v === "object" && v !== null && !Array.isArray(v);
}

/** Throws on any shape error; the page treats a throw as "the published firmware is broken". */
export function parseInstallIndex(json: unknown): InstallIndex {
  if (!isObj(json)) throw new InstallIndexError("not an object");
  const { version, boards } = json;
  if (version !== null && !(typeof version === "string" && VERSION_RE.test(version))) throw new InstallIndexError("bad version");
  if (!isObj(boards)) throw new InstallIndexError("boards is not an object");
  if (version === null && Object.keys(boards).length > 0) throw new InstallIndexError("boards without a version");
  const out: InstallIndex = { version, boards: {} };
  for (const [board, entry] of Object.entries(boards)) {
    if (!BOARD_RE.test(board)) throw new InstallIndexError(`bad board id ${JSON.stringify(board)}`);
    if (!isObj(entry) || !Array.isArray(entry.parts) || typeof entry.full !== "string" || !FILE_RE.test(entry.full)) {
      throw new InstallIndexError(`${board}: needs parts and full`);
    }
    if (entry.parts.length < 1 || entry.parts.length > 8) throw new InstallIndexError(`${board}: 1 to 8 parts`);
    const seen = new Set<number>();
    const parts = entry.parts.map((p: unknown) => {
      if (!isObj(p) || typeof p.path !== "string" || !FILE_RE.test(p.path) || typeof p.offset !== "string" || !OFFSET_RE.test(p.offset)) {
        throw new InstallIndexError(`${board}: bad part ${JSON.stringify(p)}`);
      }
      const address = Number.parseInt(p.offset, 16);
      if (address % SECTOR !== 0 || address >= FLASH_LIMIT) throw new InstallIndexError(`${board}: offset ${p.offset} is not a 4 KiB sector in 16 MB`);
      if (seen.has(address)) throw new InstallIndexError(`${board}: offset ${p.offset} twice`);
      seen.add(address);
      return { path: p.path, offset: p.offset };
    });
    out.boards[board] = { parts, full: entry.full };
  }
  return out;
}

/** The parts to write for one board, in address order. Throws when the release has no build for it. */
export function flashPlan(index: InstallIndex, board: string): Array<{ path: string; address: number }> {
  const entry = index.boards[board];
  if (entry === undefined) throw new RangeError(`no published firmware for ${board}`);
  return entry.parts
    .map((p) => ({ path: p.path, address: Number.parseInt(p.offset, 16) }))
    .sort((a, b) => a.address - b.address);
}

export type FetchLike = (url: string, init?: { cache?: "no-store" }) => Promise<{
  ok: boolean;
  status: number;
  json(): Promise<unknown>;
  arrayBuffer(): Promise<ArrayBuffer>;
}>;

export async function loadInstallIndex(fetchFn: FetchLike, base = FIRMWARE_BASE): Promise<InstallIndex> {
  const res = await fetchFn(`${base}install.json`, { cache: "no-store" });
  if (!res.ok) throw new Error(`install.json: HTTP ${res.status}`);
  return parseInstallIndex(await res.json());
}

export async function fetchPart(fetchFn: FetchLike, path: string, base = FIRMWARE_BASE): Promise<Uint8Array> {
  if (!FILE_RE.test(path)) throw new RangeError(`bad part name ${path}`);
  const res = await fetchFn(`${base}${path}`, { cache: "no-store" });
  if (!res.ok) throw new Error(`${path}: HTTP ${res.status}`);
  const bytes = new Uint8Array(await res.arrayBuffer());
  if (bytes.length === 0) throw new Error(`${path}: empty file`);
  return bytes;
}
