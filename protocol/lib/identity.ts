// SPDX-License-Identifier: Apache-2.0
// Gadget identity and the two signed texts (PROTOCOL.md §4.2, §4.3, §4.8).
import { createHash } from "node:crypto";

export const HOST_ID_RE = /^[0-9a-f]{32}$/;
const SHA256_HEX_RE = /^[0-9a-f]{64}$/;

/** "gad_" + the first 16 lowercase hex characters of SHA-256(pubkey bytes). */
export function gadgetIdFromPubkey(pub65: Uint8Array): string {
  if (pub65.length !== 65 || pub65[0] !== 0x04) throw new Error("expected a 65-byte SEC1 uncompressed public key");
  return "gad_" + createHash("sha256").update(pub65).digest("hex").slice(0, 16);
}

/** The text a gadget signs in `prove`: the nonce is the exact base64 string from `challenge`. */
export function proveText(id: string, nonceB64: string, hostId: string): string {
  return ["openmausbot-gadget/1", "prove", id, nonceB64, hostId].join("\n");
}

/** The text the release key signs for a firmware image. size is base-10 without leading zeros. */
export function firmwareText(board: string, version: string, size: number, sha256Hex: string): string {
  if (!Number.isSafeInteger(size) || size < 0) throw new Error(`size must be a non-negative integer, got ${size}`);
  if (!SHA256_HEX_RE.test(sha256Hex)) throw new Error("sha256 must be 64 lowercase hex characters");
  return ["openmausbot-gadget/1", "firmware", board, version, String(size), sha256Hex].join("\n");
}
