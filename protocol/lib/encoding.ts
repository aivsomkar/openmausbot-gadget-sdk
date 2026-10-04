// SPDX-License-Identifier: Apache-2.0
// Pinned encodings (PROTOCOL.md §4.1): RFC 4648 §4 base64 with padding, lowercase hex,
// and canonical JSON for hashing.
import { Buffer } from "node:buffer";

const B64_RE = /^(?:[A-Za-z0-9+/]{4})*(?:[A-Za-z0-9+/]{2}==|[A-Za-z0-9+/]{3}=)?$/;
const HEX_RE = /^(?:[0-9a-f]{2})*$/;

export function b64Encode(bytes: Uint8Array): string {
  return Buffer.from(bytes.buffer, bytes.byteOffset, bytes.byteLength).toString("base64");
}

/** Decodes RFC 4648 §4 base64 with padding. Returns null unless the text re-encodes to exactly itself
 *  (Node's own decoder is lenient: it accepts the URL alphabet, missing padding and junk). */
export function b64DecodeCanonical(text: string): Uint8Array | null {
  if (!B64_RE.test(text)) return null;
  const bytes = new Uint8Array(Buffer.from(text, "base64"));
  return b64Encode(bytes) === text ? bytes : null;
}

export function hexEncode(bytes: Uint8Array): string {
  return Buffer.from(bytes.buffer, bytes.byteOffset, bytes.byteLength).toString("hex");
}

/** Lowercase, even-length hex only; throws otherwise. */
export function hexDecode(hex: string): Uint8Array {
  if (!HEX_RE.test(hex)) throw new Error(`not lowercase hex: ${JSON.stringify(hex.slice(0, 16))}`);
  return new Uint8Array(Buffer.from(hex, "hex"));
}

/** JSON with object keys sorted (UTF-16 code unit order) at every depth and no whitespace.
 *  Object members whose value is undefined are dropped, as JSON.stringify does. */
export function canonicalJson(value: unknown): string {
  if (Array.isArray(value)) return "[" + value.map((v) => canonicalJson(v === undefined ? null : v)).join(",") + "]";
  if (value !== null && typeof value === "object") {
    const obj = value as Record<string, unknown>;
    const keys = Object.keys(obj).filter((k) => obj[k] !== undefined).sort();
    return "{" + keys.map((k) => JSON.stringify(k) + ":" + canonicalJson(obj[k])).join(",") + "}";
  }
  return JSON.stringify(value);
}
