// SPDX-License-Identifier: Apache-2.0
import { test } from "node:test";
import assert from "node:assert/strict";
import { b64DecodeCanonical, b64Encode, canonicalJson, hexDecode, hexEncode } from "../lib/encoding.ts";

test("b64Encode uses the standard alphabet with padding", () => {
  assert.equal(b64Encode(new Uint8Array([])), "");
  assert.equal(b64Encode(new Uint8Array([0])), "AA==");
  assert.equal(b64Encode(new Uint8Array([0, 1])), "AAE=");
  assert.equal(b64Encode(new Uint8Array([0xfb, 0xff])), "+/8=");
});

test("b64DecodeCanonical accepts only text that re-encodes unchanged", () => {
  assert.deepEqual(b64DecodeCanonical("AAEC"), new Uint8Array([0, 1, 2]));
  assert.deepEqual(b64DecodeCanonical(""), new Uint8Array([]));
  for (const bad of ["AA", "AA=", "AB==", "AAE", "AA==\n", " AA==", "-_8=", "AA==AA==", "A===", "@@@@"]) {
    assert.equal(b64DecodeCanonical(bad), null, JSON.stringify(bad));
  }
});

test("hexEncode and hexDecode round-trip lowercase hex", () => {
  assert.equal(hexEncode(new Uint8Array([0, 0xab, 0xff])), "00abff");
  assert.deepEqual(hexDecode("00abff"), new Uint8Array([0, 0xab, 0xff]));
  assert.throws(() => hexDecode("0"), /hex/);
  assert.throws(() => hexDecode("zz"), /hex/);
  assert.throws(() => hexDecode("AB"), /hex/);
});

test("canonicalJson sorts keys at every depth and has no whitespace", () => {
  assert.equal(canonicalJson({ b: 1, a: [{ d: 2, c: "x" }], c: null }), '{"a":[{"c":"x","d":2}],"b":1,"c":null}');
  assert.equal(canonicalJson("é"), '"é"');
  assert.equal(canonicalJson({ z: undefined, y: 1 }), '{"y":1}');
});
