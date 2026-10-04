// SPDX-License-Identifier: Apache-2.0
// Binary frames (PROTOCOL.md §4.1): byte 0 kind, byte 1 stream id (1–255), then the payload.

export type Kind = 1 | 2 | 3 | 4;
const BINARY_FRAME_MAX = 8192;
const FW_DATA_MAX = 4096;

export function encodeBinary(kind: Kind, stream: number, payload: Uint8Array): Uint8Array {
  if (![1, 2, 3, 4].includes(kind)) throw new Error(`unknown kind ${kind}`);
  if (!Number.isInteger(stream) || stream < 1 || stream > 255) throw new Error(`stream must be 1-255, got ${stream}`);
  if (payload.length + 2 > BINARY_FRAME_MAX) throw new Error(`binary frame would exceed 8192 bytes (${payload.length + 2})`);
  const out = new Uint8Array(payload.length + 2);
  out[0] = kind;
  out[1] = stream;
  out.set(payload, 2);
  return out;
}

export function decodeBinary(frame: Uint8Array): { kind: Kind; stream: number; payload: Uint8Array } | null {
  if (frame.length < 2 || frame.length > BINARY_FRAME_MAX) return null;
  const kind = frame[0];
  if (kind !== 1 && kind !== 2 && kind !== 3 && kind !== 4) return null;
  if (frame[1] === 0) return null;
  return { kind, stream: frame[1], payload: frame.slice(2) };
}

/** A kind-4 frame: u32 little-endian byte offset, then 1–4096 bytes of the image. */
export function encodeFwChunk(stream: number, offset: number, data: Uint8Array): Uint8Array {
  if (!Number.isInteger(offset) || offset < 0 || offset > 0xffffffff) throw new Error(`offset must fit a u32, got ${offset}`);
  if (data.length < 1 || data.length > FW_DATA_MAX) throw new Error(`a firmware chunk carries 1-4096 bytes, got ${data.length}`);
  const payload = new Uint8Array(4 + data.length);
  new DataView(payload.buffer).setUint32(0, offset, true);
  payload.set(data, 4);
  return encodeBinary(4, stream, payload);
}

export function decodeFwChunk(payload: Uint8Array): { offset: number; data: Uint8Array } | null {
  if (payload.length < 5 || payload.length > 4 + FW_DATA_MAX) return null;
  const offset = new DataView(payload.buffer, payload.byteOffset, payload.byteLength).getUint32(0, true);
  return { offset, data: payload.slice(4) };
}
