// SPDX-License-Identifier: Apache-2.0
// Firmware versions (PROTOCOL.md §4.1; spec §8): the tag without "v"; SemVer 2.0.0 precedence.
// The spec's pattern also accepts leading zeros and empty pre-release identifiers, which SemVer
// forbids: numeric identifiers compare by value, and an empty identifier is alphanumeric
// (versions.json pins both).
import { RELEASE_VERSION_RE } from "./types.ts";

export { RELEASE_VERSION_RE };

function parse(v: string): { core: number[]; pre: string[] } {
  if (!RELEASE_VERSION_RE.test(v)) throw new Error(`not a release version: ${JSON.stringify(v)}`);
  const dash = v.indexOf("-");
  const core = (dash < 0 ? v : v.slice(0, dash)).split(".").map(Number);
  const pre = dash < 0 ? [] : v.slice(dash + 1).split(".");
  return { core, pre };
}

function cmpIdent(a: string, b: string): number {
  const an = /^\d+$/.test(a);
  const bn = /^\d+$/.test(b);
  if (an && bn) return Math.sign(Number(a) - Number(b));
  if (an !== bn) return an ? -1 : 1;
  return a < b ? -1 : a > b ? 1 : 0;
}

/** -1, 0 or 1 by SemVer 2.0.0 precedence. Throws unless both match RELEASE_VERSION_RE. */
export function compareVersions(a: string, b: string): number {
  const x = parse(a);
  const y = parse(b);
  for (let i = 0; i < 3; i++) if (x.core[i] !== y.core[i]) return x.core[i] < y.core[i] ? -1 : 1;
  if (x.pre.length === 0 || y.pre.length === 0) return x.pre.length === y.pre.length ? 0 : x.pre.length === 0 ? 1 : -1;
  for (let i = 0; i < Math.min(x.pre.length, y.pre.length); i++) {
    const c = cmpIdent(x.pre[i], y.pre[i]);
    if (c !== 0) return c;
  }
  return Math.sign(x.pre.length - y.pre.length);
}

/** A custom build: ends in "-dev", or is not a release version at all. */
export function isCustomBuild(fw: string): boolean {
  return fw.endsWith("-dev") || !RELEASE_VERSION_RE.test(fw);
}
