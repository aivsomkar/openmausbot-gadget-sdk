// SPDX-License-Identifier: Apache-2.0
// Verifies every file in protocol/vectors with node:crypto and protocol/lib (never @noble/curves,
// so the generator and the verifier are independent implementations).
import { test } from "node:test";
import assert from "node:assert/strict";
import { Buffer } from "node:buffer";
import { createECDH, createHash } from "node:crypto";
import { readFileSync, readdirSync } from "node:fs";
import { b64DecodeCanonical, b64Encode, hexDecode, hexEncode } from "../lib/encoding.ts";
import { HOST_ID_RE, firmwareText, gadgetIdFromPubkey, proveText } from "../lib/identity.ts";
import { publicKeyFromPrivate, verifyP256 } from "../lib/verify.ts";
import { derToRaw, rawToDer } from "../lib/der.ts";
import { decodeBinary, decodeFwChunk, encodeBinary, encodeFwChunk, type Kind } from "../lib/frames.ts";
import { compareVersions, isCustomBuild } from "../lib/version.ts";
import * as F from "./fixed-values.ts";

const DIR = new URL("../vectors/", import.meta.url);
const EXPECTED_FILES = [
  "base64.json", "der.json", "firmware.json", "frames.json", "identity.json", "prove.json", "rfc6979.json", "versions.json",
];
type Case = Record<string, any>;
const read = (name: string): Buffer => readFileSync(new URL(name, DIR));
const load = (name: string): Case => JSON.parse(read(name).toString("utf8"));
const cases = (name: string): Case[] => load(name).cases;
const isHighS = (raw: Uint8Array): boolean => BigInt("0x" + hexEncode(raw.slice(32))) > F.P256_N / 2n;

test("the vector folder holds exactly the expected files and SHA256SUMS covers their bytes", () => {
  const files = readdirSync(DIR).filter((f) => f !== "SHA256SUMS").sort();
  assert.deepEqual(files, EXPECTED_FILES);
  const sums = read("SHA256SUMS").toString("utf8");
  assert.ok(sums.endsWith("\n") && !sums.includes("\r"));
  const expected = EXPECTED_FILES.map((f) => `${createHash("sha256").update(read(f)).digest("hex")}  ${f}`).join("\n") + "\n";
  assert.equal(sums, expected);
});

test("every vector file is 2-space JSON + LF with an envelope and unique case names", () => {
  for (const file of EXPECTED_FILES) {
    const text = read(file).toString("utf8");
    assert.ok(!text.includes("\r"), file);
    const value = JSON.parse(text);
    assert.equal(text, JSON.stringify(value, null, 2) + "\n", file);
    assert.equal(value.vectors, file.replace(/\.json$/, ""));
    assert.equal(value.version, 1);
    const list: Case[] = file === "versions.json" ? [...value.compare, ...value.custom] : value.cases;
    const names = list.map((c) => c.name);
    assert.equal(new Set(names).size, names.length, `${file} has duplicate names`);
  }
});

test("identity.json: pubkey and id derive from the private key", () => {
  const list = cases("identity.json");
  assert.deepEqual(list.map((c) => c.name), ["rfc6979-a25", "test-t1"]);
  for (const c of list) {
    const pub = publicKeyFromPrivate(c.private_key_hex);
    assert.equal(hexEncode(pub), c.pubkey_hex, c.name);
    assert.equal(b64Encode(pub), c.pubkey_b64, c.name);
    assert.equal(createHash("sha256").update(pub).digest("hex"), c.pubkey_sha256_hex, c.name);
    assert.equal(gadgetIdFromPubkey(pub), c.id, c.name);
  }
  assert.equal(list[0].id, F.RFC_ID);
  assert.equal(list[1].pubkey_b64, F.T1_PUBKEY_B64);
});

test("rfc6979.json: RFC 6979 A.2.5 values verify, k·G gives r, and high_s is right", () => {
  const list = cases("rfc6979.json");
  assert.deepEqual(list.map((c) => [c.name, c.high_s]), [["sample", true], ["test", false]]);
  for (const c of list) {
    assert.equal(createHash("sha256").update(c.message_utf8, "utf8").digest("hex"), c.message_sha256_hex);
    assert.equal(c.raw_hex, c.r_hex + c.s_hex);
    const der = hexDecode(c.der_hex);
    assert.equal(hexEncode(derToRaw(der)!), c.raw_hex);
    assert.deepEqual(rawToDer(hexDecode(c.raw_hex)), der);
    assert.equal(verifyP256(publicKeyFromPrivate(c.private_key_hex), c.message_utf8, der), true, c.name);
    const ecdh = createECDH("prime256v1");
    ecdh.setPrivateKey(Buffer.from(c.k_hex, "hex"));
    const kx = BigInt("0x" + ecdh.getPublicKey().subarray(1, 33).toString("hex"));
    assert.equal(kx % F.P256_N, BigInt("0x" + c.r_hex), `${c.name}: r = (k·G).x mod n`);
    assert.equal(isHighS(hexDecode(c.raw_hex)), c.high_s);
  }
  assert.equal(list[0].der_hex, F.RFC_SAMPLE_DER_HEX);
});

/** The host's checks in PROTOCOL.md §4.3 order, plus the gadget's host_id check on `challenge`. */
function proveVerdict(c: Case): string {
  if (!HOST_ID_RE.test(c.host_id)) return "reject_host_id";
  const pub = b64DecodeCanonical(c.pubkey_b64);
  if (!pub) return "reject_base64";
  if (pub.length !== 65 || pub[0] !== 0x04) return "reject_pubkey";
  if (gadgetIdFromPubkey(pub) !== c.id) return "reject_id";
  const sig = b64DecodeCanonical(c.sig_b64);
  if (!sig || !verifyP256(pub, proveText(c.id, c.nonce_b64, c.host_id), sig)) return "reject_sig";
  return "accept";
}

test("prove.json: every case gets its expected verdict from the host rules", () => {
  const list = cases("prove.json");
  assert.deepEqual(list.map((c) => c.name), [
    "pinned", "pinned-low-s", "low-s-host", "changed-host-id", "pubkey-not-id",
    "pubkey-non-canonical", "pubkey-compressed", "sig-truncated", "host-id-format",
  ]);
  for (const c of list) {
    assert.equal(proveVerdict(c), c.expect, c.name);
    assert.equal(c.text, proveText(c.id, c.nonce_b64, c.host_id), c.name);
    assert.equal(c.sig_b64, b64Encode(hexDecode(c.sig_der_hex)), c.name);
    const raw = derToRaw(hexDecode(c.sig_der_hex));
    assert.equal(raw ? isHighS(raw) : false, c.high_s, c.name);
  }
  const byName = Object.fromEntries(list.map((c) => [c.name, c]));
  const pinned = byName["pinned"];
  assert.equal(pinned.text, F.PROVE_TEXT);
  assert.equal(pinned.sig_der_hex, F.PROVE_DER_HEX);
  assert.equal(pinned.sig_b64, F.PROVE_SIG_B64);
  assert.deepEqual([pinned.deterministic, pinned.high_s], [true, true]);
  assert.deepEqual([byName["pinned-low-s"].deterministic, byName["pinned-low-s"].high_s], [false, false]);
  assert.deepEqual([byName["low-s-host"].host_id, byName["low-s-host"].high_s], [F.LOW_S_HOST_ID, false]);
  // Signature-valid negatives: only the named check may reject them.
  for (const name of ["pubkey-not-id", "pubkey-compressed", "host-id-format"]) {
    const c = byName[name];
    const pub = new Uint8Array(Buffer.from(c.pubkey_b64, "base64"));
    const der = hexDecode(c.sig_der_hex);
    if (name === "pubkey-compressed") assert.equal(pub.length, 33);
    else assert.equal(verifyP256(pub, c.text, der), true, `${name} signature itself is valid`);
  }
  // A lenient base64 decoder turns the non-canonical key into the real key: only the canonical check rejects it.
  assert.equal(Buffer.from(byName["pubkey-non-canonical"].pubkey_b64, "base64").toString("base64"), F.RFC_PUBKEY_B64);
});

test("der.json: strict DER parsing and raw conversion, and node:crypto rejects the invalid ones", () => {
  const list = cases("der.json");
  assert.deepEqual(list.map((c) => [c.name, c.valid]), [
    ["rfc-sample", true], ["short-der-69", true], ["prove-pinned", true],
    ["non-minimal-int", false], ["negative-int", false], ["trailing-bytes", false], ["wrong-tag", false],
  ]);
  for (const c of list) {
    const der = hexDecode(c.der_hex);
    const pub = publicKeyFromPrivate(c.private_key_hex);
    if (c.valid) {
      assert.equal(der.length, c.der_len, c.name);
      assert.equal(hexEncode(derToRaw(der)!), c.raw_hex, c.name);
      assert.deepEqual(rawToDer(hexDecode(c.raw_hex)), der, c.name);
      assert.equal(verifyP256(pub, c.message_utf8, der), true, c.name);
    } else {
      assert.equal(derToRaw(der), null, c.name);
      assert.equal(verifyP256(pub, c.message_utf8, der), false, c.name);
    }
  }
  assert.equal(list[1].der_len, 69);
});

test("firmware.json: the gadget's verdict uses its own board id", () => {
  const list = cases("firmware.json");
  assert.deepEqual(list.map((c) => [c.name, c.expect]), [
    ["t1-amoled", "accept"], ["other-board", "bad_sig"], ["size-changed", "bad_sig"], ["sha-uppercase", "bad_sig"],
  ]);
  for (const c of list) {
    const pub = b64DecodeCanonical(c.pubkey_b64)!;
    assert.deepEqual(pub, publicKeyFromPrivate(c.private_key_hex), c.name);
    const text = firmwareText(c.gadget_board, c.version, c.size, c.sha256);
    assert.equal(c.text, text, c.name);
    assert.equal(c.sig_b64, b64Encode(hexDecode(c.sig_der_hex)), c.name);
    assert.equal(verifyP256(pub, text, hexDecode(c.sig_der_hex)) ? "accept" : "bad_sig", c.expect, c.name);
  }
  assert.equal(list[0].text, F.FIRMWARE_TEXT);
  assert.equal(list[0].sig_der_hex, F.FIRMWARE_T1_DER_HEX);
  assert.equal(list[0].key_id, "t1");
});

test("base64.json: only canonical RFC 4648 §4 text decodes", () => {
  assert.deepEqual(cases("base64.json").map((c) => c.name), [
    "empty", "one-byte", "two-bytes", "three-bytes", "pinned-nonce", "rfc-pubkey", "no-padding", "short-padding", "pad-bits",
    "missing-padding-3", "trailing-newline", "leading-space", "url-alphabet", "concatenated",
  ]);
  for (const c of cases("base64.json")) {
    const bytes = b64DecodeCanonical(c.input);
    assert.equal(bytes !== null, c.canonical, c.name);
    if (c.canonical) assert.equal(hexEncode(bytes!), c.bytes_hex, c.name);
  }
});

test("frames.json: binary frames decode and re-encode byte for byte", () => {
  assert.deepEqual(cases("frames.json").map((c) => c.name), [
    "mic-20ms", "speaker-40ms-16k", "image-rows", "fw-chunk", "fw-chunk-last", "too-short", "stream-zero", "unknown-kind", "fw-no-offset",
  ]);
  for (const c of cases("frames.json")) {
    const frame = hexDecode(c.frame_hex);
    const d = decodeBinary(frame);
    const chunk = d && d.kind === 4 ? decodeFwChunk(d.payload) : null;
    const valid = d !== null && (d.kind !== 4 || chunk !== null);
    assert.equal(valid, c.valid, c.name);
    if (!c.valid) continue;
    assert.deepEqual([d!.kind, d!.stream, hexEncode(d!.payload)], [c.kind, c.stream, c.payload_hex], c.name);
    assert.deepEqual(encodeBinary(c.kind as Kind, c.stream, hexDecode(c.payload_hex)), frame, c.name);
    if (c.kind === 4) {
      assert.deepEqual([chunk!.offset, hexEncode(chunk!.data)], [c.offset, c.data_hex], c.name);
      assert.deepEqual(encodeFwChunk(c.stream, c.offset, hexDecode(c.data_hex)), frame, c.name);
    }
  }
});

test("versions.json: SemVer precedence and the custom-build rule", () => {
  const v = load("versions.json");
  assert.deepEqual(v.compare.map((c: Case) => c.name), [
    "patch-vs-minor", "numeric-minor", "release-vs-rc", "rc2-vs-rc1", "rc10-vs-rc9", "beta-vs-rc", "shorter-prerelease",
    "numeric-vs-alpha", "equal-release", "equal-rc",
    // Accepted by the pattern although SemVer 2.0.0 forbids them (PROTOCOL.md §4.1): C and TS must agree.
    "leading-zero-core", "leading-zero-prerelease", "empty-ident-vs-numeric", "empty-ident-vs-alpha",
  ]);
  assert.deepEqual(v.compare.slice(10).map((c: Case) => [c.a, c.b, c.cmp]), [
    ["1.01.0", "1.1.0", 0], ["1.1.0-rc.01", "1.1.0-rc.1", 0], ["1.0.0-rc..1", "1.0.0-rc.1", 1], ["1.0.0-.", "1.0.0-a", -1],
  ]);
  assert.deepEqual(v.custom.map((c: Case) => c.name), [
    "dev-zero", "dev-tagged", "release", "prerelease", "leading-v", "two-parts", "empty", "leading-zero", "empty-ident",
  ]);
  assert.deepEqual(v.custom.slice(7).map((c: Case) => [c.fw, c.custom]), [["1.01.0", false], ["1.0.0-.", false]]);
  for (const c of v.compare) {
    assert.equal(compareVersions(c.a, c.b), c.cmp, c.name);
    assert.equal(compareVersions(c.b, c.a), -c.cmp || 0, c.name);
  }
  for (const c of v.custom) assert.equal(isCustomBuild(c.fw), c.custom, c.name);
});
