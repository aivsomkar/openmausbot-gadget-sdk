// SPDX-License-Identifier: Apache-2.0
// P-256 ECDSA with node:crypto only. node:crypto accepts high-S signatures, which the
// firmware produces about half the time (it never normalizes S; PROTOCOL.md §4.9).
import { Buffer } from "node:buffer";
import { createECDH, createPrivateKey, createPublicKey, sign, verify } from "node:crypto";

const SPKI_P256_PREFIX = Buffer.from("3059301306072a8648ce3d020106082a8648ce3d030107034200", "hex");
const PRIVATE_HEX_RE = /^[0-9a-f]{64}$/;

/** True only when `der` is a valid ECDSA-P256-SHA256 signature over the UTF-8 text. False on any
 *  parse error: wrong key length, a point not on the curve, malformed DER. */
export function verifyP256(pub65: Uint8Array, text: string, der: Uint8Array): boolean {
  if (pub65.length !== 65 || pub65[0] !== 0x04) return false;
  try {
    const key = createPublicKey({ key: Buffer.concat([SPKI_P256_PREFIX, pub65]), format: "der", type: "spki" });
    return verify("sha256", Buffer.from(text, "utf8"), { key, dsaEncoding: "der" }, der);
  } catch {
    return false;
  }
}

/** The 65-byte SEC1 uncompressed public key for a 32-byte private scalar given as 64 lowercase hex. */
export function publicKeyFromPrivate(privateKeyHex: string): Uint8Array {
  if (!PRIVATE_HEX_RE.test(privateKeyHex)) throw new Error("private key must be 64 lowercase hex characters");
  const ecdh = createECDH("prime256v1");
  ecdh.setPrivateKey(Buffer.from(privateKeyHex, "hex"));
  return new Uint8Array(ecdh.getPublicKey());
}

/** DER ECDSA-P256-SHA256 with a random k (node:crypto). For the fake host and dev tools; the
 *  deterministic vectors come from protocol/tools/gen-vectors.ts. */
export function signP256(privateKeyHex: string, text: string): Uint8Array {
  const pub = publicKeyFromPrivate(privateKeyHex);
  const b64u = (b: Uint8Array): string => Buffer.from(b).toString("base64url");
  const key = createPrivateKey({
    key: { kty: "EC", crv: "P-256", d: b64u(Buffer.from(privateKeyHex, "hex")), x: b64u(pub.subarray(1, 33)), y: b64u(pub.subarray(33)) },
    format: "jwk",
  });
  return new Uint8Array(sign("sha256", Buffer.from(text, "utf8"), { key, dsaEncoding: "der" }));
}
