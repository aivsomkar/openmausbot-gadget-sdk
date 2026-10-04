// SPDX-License-Identifier: Apache-2.0
// Strict DER <-> raw (r||s, 32 + 32 bytes) for P-256 ECDSA signatures. The firmware does the
// same conversion in C (spec §5.2); protocol/vectors/der.json pins both directions.

function readInt(der: Uint8Array, at: number): { value: Uint8Array; next: number } | null {
  if (at + 2 > der.length || der[at] !== 0x02) return null;
  const len = der[at + 1];
  if (len < 1 || len > 33 || at + 2 + len > der.length) return null;
  const bytes = der.subarray(at + 2, at + 2 + len);
  if (bytes[0] & 0x80) return null;                                  // negative
  if (bytes[0] === 0x00 && (len === 1 || !(bytes[1] & 0x80))) return null; // zero or non-minimal
  const value = bytes[0] === 0x00 ? bytes.subarray(1) : bytes;
  if (value.length > 32) return null;
  return { value, next: at + 2 + len };
}

/** Returns the 64-byte r||s, or null for anything that is not a minimal DER SEQUENCE of two
 *  positive INTEGERs of at most 32 value bytes with nothing after it. */
export function derToRaw(der: Uint8Array): Uint8Array | null {
  if (der.length < 8 || der.length > 72 || der[0] !== 0x30 || der[1] !== der.length - 2) return null;
  const r = readInt(der, 2);
  if (!r) return null;
  const s = readInt(der, r.next);
  if (!s || s.next !== der.length) return null;
  const raw = new Uint8Array(64);
  raw.set(r.value, 32 - r.value.length);
  raw.set(s.value, 64 - s.value.length);
  return raw;
}

function encodeInt(v: Uint8Array): Uint8Array {
  let i = 0;
  while (i < v.length - 1 && v[i] === 0) i++;
  const trimmed = v.subarray(i);
  const pad = trimmed[0] & 0x80 ? 1 : 0;
  const out = new Uint8Array(2 + pad + trimmed.length);
  out[0] = 0x02;
  out[1] = pad + trimmed.length;
  out.set(trimmed, 2 + pad);
  return out;
}

/** Minimal DER for a 64-byte r||s. */
export function rawToDer(raw: Uint8Array): Uint8Array {
  if (raw.length !== 64) throw new Error("expected 64-byte r||s");
  const r = encodeInt(raw.subarray(0, 32));
  const s = encodeInt(raw.subarray(32));
  const out = new Uint8Array(2 + r.length + s.length);
  out[0] = 0x30;
  out[1] = r.length + s.length;
  out.set(r, 2);
  out.set(s, 2 + r.length);
  return out;
}
