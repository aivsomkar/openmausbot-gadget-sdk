// SPDX-License-Identifier: Apache-2.0
import { test } from "node:test";
import assert from "node:assert/strict";
import { createHash } from "node:crypto";
import { readFileSync } from "node:fs";
import { b64DecodeCanonical, b64Encode, hexDecode, hexEncode } from "../lib/encoding.ts";
import { HOST_ID_RE, firmwareText, gadgetIdFromPubkey, proveText } from "../lib/identity.ts";
import { publicKeyFromPrivate, signP256, verifyP256 } from "../lib/verify.ts";
import { derToRaw, rawToDer } from "../lib/der.ts";
import * as F from "./fixed-values.ts";

const rfcPub = (): Uint8Array => b64DecodeCanonical(F.RFC_PUBKEY_B64)!;

test("publicKeyFromPrivate gives the 65-byte SEC1 point", () => {
  const pub = publicKeyFromPrivate(F.RFC_PRIVATE_HEX);
  assert.equal(pub.length, 65);
  assert.equal(pub[0], 0x04);
  assert.equal(b64Encode(pub), F.RFC_PUBKEY_B64);
  assert.equal(b64Encode(publicKeyFromPrivate(F.T1_PRIVATE_HEX)), F.T1_PUBKEY_B64);
});

test("gadgetIdFromPubkey is gad_ + 16 hex of sha256(pubkey)", () => {
  assert.equal(gadgetIdFromPubkey(rfcPub()), F.RFC_ID);
  assert.throws(() => gadgetIdFromPubkey(rfcPub().slice(0, 33)), /65-byte/);
});

test("proveText and firmwareText build the pinned texts with no trailing newline", () => {
  assert.equal(proveText(F.RFC_ID, F.PINNED_NONCE_B64, F.PINNED_HOST_ID), F.PROVE_TEXT);
  assert.equal(firmwareText("amoled-175c", "1.1.0", 1234567, createHash("sha256").update("").digest("hex")), F.FIRMWARE_TEXT);
  assert.throws(() => firmwareText("amoled-175c", "1.1.0", 12.5, "e3".repeat(32)), /size/);
  assert.throws(() => firmwareText("amoled-175c", "1.1.0", 1, "E3".repeat(32)), /sha256/);
  assert.ok(HOST_ID_RE.test(F.PINNED_HOST_ID));
  assert.ok(!HOST_ID_RE.test("h_0123456789abcdef"));
});

test("verifyP256 accepts the high-S prove and firmware signatures (node:crypto)", () => {
  assert.equal(verifyP256(rfcPub(), F.PROVE_TEXT, hexDecode(F.PROVE_DER_HEX)), true);
  assert.equal(b64Encode(hexDecode(F.PROVE_DER_HEX)), F.PROVE_SIG_B64);
  assert.equal(verifyP256(b64DecodeCanonical(F.T1_PUBKEY_B64)!, F.FIRMWARE_TEXT, hexDecode(F.FIRMWARE_T1_DER_HEX)), true);
  assert.equal(verifyP256(rfcPub(), "sample", hexDecode(F.RFC_SAMPLE_DER_HEX)), true);
  assert.equal(verifyP256(rfcPub(), F.SHORT_DER_MESSAGE, hexDecode(F.SHORT_DER_HEX)), true);
});

test("verifyP256 also accepts the low-S twin (n - s) and rejects tampering", () => {
  const raw = derToRaw(hexDecode(F.PROVE_DER_HEX))!;
  const s = BigInt("0x" + hexEncode(raw.slice(32)));
  const twin = new Uint8Array(64);
  twin.set(raw.slice(0, 32), 0);
  twin.set(hexDecode((F.P256_N - s).toString(16).padStart(64, "0")), 32);
  assert.equal(verifyP256(rfcPub(), F.PROVE_TEXT, rawToDer(twin)), true);
  assert.equal(verifyP256(rfcPub(), F.PROVE_TEXT.replace(F.PINNED_HOST_ID, "000102030405060708090a0b0c0d0e0e"), hexDecode(F.PROVE_DER_HEX)), false);
  assert.equal(verifyP256(rfcPub(), F.PROVE_TEXT, hexDecode(F.PROVE_DER_HEX).slice(0, 70)), false);
  assert.equal(verifyP256(rfcPub().slice(0, 64), F.PROVE_TEXT, hexDecode(F.PROVE_DER_HEX)), false);
  const offCurve = rfcPub().slice();
  offCurve[64] ^= 1;
  assert.equal(verifyP256(offCurve, F.PROVE_TEXT, hexDecode(F.PROVE_DER_HEX)), false);
});

test("signP256 (random k) produces signatures verifyP256 accepts", () => {
  const der = signP256(F.T1_PRIVATE_HEX, F.FIRMWARE_TEXT);
  assert.ok(der.length >= 8 && der.length <= 72);
  assert.equal(verifyP256(publicKeyFromPrivate(F.T1_PRIVATE_HEX), F.FIRMWARE_TEXT, der), true);
});

test("derToRaw and rawToDer convert between DER and r||s", () => {
  const sample = hexDecode(F.RFC_SAMPLE_DER_HEX);
  const raw = derToRaw(sample)!;
  assert.equal(hexEncode(raw), F.RFC_SAMPLE_DER_HEX.slice(10, 74) + F.RFC_SAMPLE_DER_HEX.slice(80));
  assert.deepEqual(rawToDer(raw), sample);
  const short = hexDecode(F.SHORT_DER_HEX);
  assert.equal(short.length, 69);
  assert.deepEqual(rawToDer(derToRaw(short)!), short);
  assert.equal(derToRaw(hexDecode("3006020100020101")), null, "zero r is not a valid signature integer");
  assert.equal(derToRaw(new Uint8Array([0x30, 0x00])), null);
});

test("keys/test-t1.* hold the t1 test key derived from its seed text", () => {
  const keyHex = readFileSync(new URL("../../keys/test-t1.key.hex", import.meta.url), "utf8");
  const pubB64 = readFileSync(new URL("../../keys/test-t1.pub.b64", import.meta.url), "utf8");
  assert.equal(keyHex, createHash("sha256").update(F.T1_SEED_TEXT, "utf8").digest("hex") + "\n");
  assert.equal(keyHex, F.T1_PRIVATE_HEX + "\n");
  assert.equal(pubB64, F.T1_PUBKEY_B64 + "\n");
});
