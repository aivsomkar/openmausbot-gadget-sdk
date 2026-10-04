// SPDX-License-Identifier: Apache-2.0
import { test } from "node:test";
import assert from "node:assert/strict";
import { decodeBinary, decodeFwChunk, encodeBinary, encodeFwChunk } from "../lib/frames.ts";
import { compareVersions, isCustomBuild } from "../lib/version.ts";

test("encodeBinary writes [kind][stream][payload]", () => {
  assert.deepEqual(encodeBinary(1, 7, new Uint8Array([0xaa, 0xbb])), new Uint8Array([1, 7, 0xaa, 0xbb]));
  assert.throws(() => encodeBinary(2, 0, new Uint8Array(1)), /stream/);
  assert.throws(() => encodeBinary(2, 256, new Uint8Array(1)), /stream/);
  assert.throws(() => encodeBinary(3, 1, new Uint8Array(8191)), /8192/);
  assert.equal(encodeBinary(3, 1, new Uint8Array(8190)).length, 8192);
});

test("decodeBinary rejects short frames, stream 0, unknown kinds and oversize frames", () => {
  assert.deepEqual(decodeBinary(new Uint8Array([2, 9, 1, 2])), { kind: 2, stream: 9, payload: new Uint8Array([1, 2]) });
  assert.equal(decodeBinary(new Uint8Array([1])), null);
  assert.equal(decodeBinary(new Uint8Array([1, 0, 5])), null);
  assert.equal(decodeBinary(new Uint8Array([5, 1, 5])), null);
  assert.equal(decodeBinary(new Uint8Array([0, 1, 5])), null);
  assert.equal(decodeBinary(new Uint8Array(8193).fill(1)), null);
});

test("firmware chunks carry a u32 little-endian offset and 1..4096 bytes", () => {
  const frame = encodeFwChunk(3, 65536, new Uint8Array([9, 8, 7]));
  assert.deepEqual(frame, new Uint8Array([4, 3, 0x00, 0x00, 0x01, 0x00, 9, 8, 7]));
  const decoded = decodeBinary(frame)!;
  assert.deepEqual(decodeFwChunk(decoded.payload), { offset: 65536, data: new Uint8Array([9, 8, 7]) });
  assert.equal(decodeFwChunk(new Uint8Array([0, 0, 0, 0])), null, "no data bytes");
  assert.equal(decodeFwChunk(new Uint8Array(4 + 4097)), null, "more than 4096 data bytes");
  assert.throws(() => encodeFwChunk(1, 0, new Uint8Array(4097)), /4096/);
  assert.throws(() => encodeFwChunk(1, 2 ** 32, new Uint8Array(1)), /offset/);
});

test("compareVersions follows SemVer 2.0.0 precedence", () => {
  assert.equal(compareVersions("1.1.0", "1.0.9"), 1);
  assert.equal(compareVersions("1.9.9", "1.10.0"), -1);
  assert.equal(compareVersions("1.1.0", "1.1.0-rc.1"), 1);
  assert.equal(compareVersions("1.1.0-rc.10", "1.1.0-rc.9"), 1);
  assert.equal(compareVersions("1.1.0-beta", "1.1.0-rc"), -1);
  assert.equal(compareVersions("1.1.0-rc", "1.1.0-rc.1"), -1);
  assert.equal(compareVersions("1.1.0-1", "1.1.0-a"), -1);
  assert.equal(compareVersions("2.0.0", "2.0.0"), 0);
  assert.throws(() => compareVersions("v1.0.0", "1.0.0"), /version/);
});

test("isCustomBuild flags -dev and anything that is not a release version", () => {
  for (const fw of ["0.0.0-dev", "1.2.0-dev", "v1.2.0", "1.2", ""]) assert.equal(isCustomBuild(fw), true, fw);
  for (const fw of ["1.2.0", "1.2.0-rc.1"]) assert.equal(isCustomBuild(fw), false, fw);
});
