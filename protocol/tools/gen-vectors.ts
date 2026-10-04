// SPDX-License-Identifier: Apache-2.0
// Writes protocol/vectors/*.json and SHA256SUMS (PROTOCOL.md §4.9; contract 00-interfaces.md §4.4).
// The only importer of @noble/curves. Its p256 defaults are low-S signatures and compressed keys,
// which the firmware cannot match, so every call passes lowS: false and getPublicKey(sk, false).
// Run: node protocol/tools/gen-vectors.ts
import { p256 } from "@noble/curves/nist.js";
import { Buffer } from "node:buffer";
import { createHash } from "node:crypto";
import { mkdirSync, readdirSync, rmSync, writeFileSync } from "node:fs";
import { b64Encode, hexDecode, hexEncode } from "../lib/encoding.ts";
import { firmwareText, gadgetIdFromPubkey, proveText } from "../lib/identity.ts";
import { encodeBinary, encodeFwChunk } from "../lib/frames.ts";
import { compareVersions, isCustomBuild } from "../lib/version.ts";
import * as F from "../test/fixed-values.ts";

type Json = Record<string, unknown>;
const OUT = new URL("../vectors/", import.meta.url);
const utf8 = (s: string): Uint8Array => new TextEncoder().encode(s);
const sha256Hex = (b: Uint8Array | string): string => createHash("sha256").update(b).digest("hex");

function check(cond: boolean, what: string): void {
  if (!cond) throw new Error(`gen-vectors: ${what}`);
}

/** RFC 6979 deterministic DER over SHA-256(text), S never normalized. */
function signDet(skHex: string, text: string): Uint8Array {
  return p256.sign(utf8(text), hexDecode(skHex), { lowS: false, format: "der" });
}
function pubOf(skHex: string): Uint8Array {
  const pub = p256.getPublicKey(hexDecode(skHex), false);
  check(pub.length === 65 && pub[0] === 0x04, "getPublicKey(sk, false) must give 65 bytes");
  return pub;
}
function rawOf(der: Uint8Array): Uint8Array {
  return p256.Signature.fromBytes(der, "der").toBytes("compact");
}
function highS(der: Uint8Array): boolean {
  return p256.Signature.fromBytes(der, "der").hasHighS();
}
/** high_s for a case: false when the bytes are not a valid DER signature at all. */
function highSOrFalse(der: Uint8Array): boolean {
  try {
    return highS(der);
  } catch {
    return false;
  }
}
function equalBytes(a: Uint8Array, b: Uint8Array): boolean {
  return a.length === b.length && a.every((x, i) => x === b[i]);
}
function lowSTwin(der: Uint8Array): Uint8Array {
  const sig = p256.Signature.fromBytes(der, "der");
  return new p256.Signature(sig.r, p256.Point.CURVE().n - sig.s).toBytes("der");
}

// ---- identity.json -----------------------------------------------------------------------------
function identity(): Json {
  const one = (name: string, sk: string): Json => {
    const pub = pubOf(sk);
    return {
      name, private_key_hex: sk, pubkey_hex: hexEncode(pub), pubkey_b64: b64Encode(pub),
      pubkey_sha256_hex: sha256Hex(pub), id: gadgetIdFromPubkey(pub),
    };
  };
  const cases = [one("rfc6979-a25", F.RFC_PRIVATE_HEX), one("test-t1", F.T1_PRIVATE_HEX)];
  check(cases[0].pubkey_b64 === F.RFC_PUBKEY_B64 && cases[0].id === F.RFC_ID, "RFC key identity differs from §1.7");
  check(cases[1].pubkey_b64 === F.T1_PUBKEY_B64, "t1 public key differs from §1.7");
  check(F.T1_PRIVATE_HEX === sha256Hex(F.T1_SEED_TEXT), "t1 is SHA-256 of its seed text");
  return { vectors: "identity", version: 1, cases };
}

// ---- rfc6979.json ------------------------------------------------------------------------------
const RFC_K = {
  sample: "a6e3c57dd01abe90086538398355dd4c3b17aa873382b0f24d6129493d8aad60",
  test: "d16b6ae827f17175e040871a1c7ec3500192c4c92677336ec2537acaee0008e0",
};
function rfc6979(): Json {
  const one = (message: "sample" | "test"): Json => {
    const der = signDet(F.RFC_PRIVATE_HEX, message);
    const raw = rawOf(der);
    const sig = p256.Signature.fromBytes(der, "der");
    const kG = p256.Point.BASE.multiply(BigInt("0x" + RFC_K[message]));
    check(kG.x % p256.Point.CURVE().n === sig.r, `RFC 6979 k for "${message}" must give r`);
    return {
      name: message, private_key_hex: F.RFC_PRIVATE_HEX, message_utf8: message, message_sha256_hex: sha256Hex(message),
      k_hex: RFC_K[message], r_hex: hexEncode(raw.slice(0, 32)), s_hex: hexEncode(raw.slice(32)),
      raw_hex: hexEncode(raw), der_hex: hexEncode(der), high_s: highS(der),
    };
  };
  const cases = [one("sample"), one("test")];
  check(cases[0].der_hex === F.RFC_SAMPLE_DER_HEX && cases[0].high_s === true, "RFC sample must be the §1.7 high-S DER");
  check(cases[1].high_s === false, "RFC test is low-S");
  return { vectors: "rfc6979", version: 1, cases };
}

// ---- prove.json --------------------------------------------------------------------------------
function prove(): Json {
  const sk = F.RFC_PRIVATE_HEX;
  const pub = pubOf(sk);
  const pubB64 = b64Encode(pub);
  const make = (name: string, expect: string, f: { pubkey_b64?: string; id?: string; host_id?: string; der?: Uint8Array }): Json => {
    const id = f.id ?? F.RFC_ID;
    const hostId = f.host_id ?? F.PINNED_HOST_ID;
    const text = proveText(id, F.PINNED_NONCE_B64, hostId);
    const der = f.der ?? signDet(sk, text);
    return {
      name, expect, private_key_hex: sk, pubkey_b64: f.pubkey_b64 ?? pubB64, id, nonce_b64: F.PINNED_NONCE_B64,
      host_id: hostId, text, sig_der_hex: hexEncode(der), sig_b64: b64Encode(der),
      deterministic: equalBytes(der, signDet(sk, text)), high_s: highSOrFalse(der),
    };
  };
  const pinnedDer = signDet(sk, proveText(F.RFC_ID, F.PINNED_NONCE_B64, F.PINNED_HOST_ID));
  const nonCanonical = pubB64.slice(0, -2) + String.fromCharCode(pubB64.charCodeAt(pubB64.length - 2) + 1) + "=";
  check(Buffer.from(nonCanonical, "base64").toString("base64") === pubB64 && nonCanonical !== pubB64, "pad-bit variant");
  const compressed = p256.getPublicKey(hexDecode(sk), true);
  const compressedId = "gad_" + sha256Hex(compressed).slice(0, 16);
  const cases = [
    make("pinned", "accept", {}),
    make("pinned-low-s", "accept", { der: lowSTwin(pinnedDer) }),
    make("low-s-host", "accept", { host_id: F.LOW_S_HOST_ID }),
    make("changed-host-id", "reject_sig", { host_id: "000102030405060708090a0b0c0d0e0e", der: pinnedDer }),
    make("pubkey-not-id", "reject_id", { id: "gad_0000000000000000" }),
    make("pubkey-non-canonical", "reject_base64", { pubkey_b64: nonCanonical }),
    make("pubkey-compressed", "reject_pubkey", { pubkey_b64: b64Encode(compressed), id: compressedId }),
    make("sig-truncated", "reject_sig", { der: pinnedDer.slice(0, pinnedDer.length - 1) }),
    make("host-id-format", "reject_host_id", { host_id: "h_0123456789abcdef" }),
  ];
  const [pinned, twin, lowHost] = cases;
  check(pinned.text === F.PROVE_TEXT && pinned.sig_der_hex === F.PROVE_DER_HEX && pinned.sig_b64 === F.PROVE_SIG_B64, "pinned prove must equal §1.7");
  check(pinned.high_s === true && pinned.deterministic === true, "pinned prove is deterministic and high-S");
  check(twin.high_s === false && twin.deterministic === false, "pinned-low-s is the low-S twin");
  check(lowHost.high_s === false && lowHost.deterministic === true, "low-s-host is the low-S control");
  for (const c of cases) if (c.high_s === true) check(highS(hexDecode(c.sig_der_hex as string)), `${c.name} hasHighS()`);
  return { vectors: "prove", version: 1, cases };
}

// ---- der.json ----------------------------------------------------------------------------------
function der(): Json {
  const sk = F.RFC_PRIVATE_HEX;
  const valid = (name: string, message: string): Json => {
    const d = signDet(sk, message);
    return { name, valid: true, der_hex: hexEncode(d), raw_hex: hexEncode(rawOf(d)), der_len: d.length, private_key_hex: sk, message_utf8: message };
  };
  const invalid = (name: string, d: Uint8Array, message: string): Json => ({
    name, valid: false, der_hex: hexEncode(d), private_key_hex: sk, message_utf8: message,
  });
  const sample = signDet(sk, "sample");
  const short = signDet(sk, F.SHORT_DER_MESSAGE);
  check(hexEncode(short) === F.SHORT_DER_HEX && short.length === 69, "short DER must equal §1.7 (69 bytes)");
  const r = short.slice(4, 36);
  const s31 = short.slice(38);
  check(short[36] === 0x02 && short[37] === 0x1f && s31.length === 31, "short DER layout");
  const nonMinimal = new Uint8Array([0x30, 68, 0x02, 0x20, ...r, 0x02, 0x20, 0x00, ...s31]);
  const negative = new Uint8Array([0x30, 2 + 32 + (sample.length - 37), 0x02, 0x20, ...sample.slice(5, 37), ...sample.slice(37)]);
  const trailing = new Uint8Array([...short, 0x00]);
  const wrongTag = sample.slice();
  wrongTag[0] = 0x31;
  const cases = [
    valid("rfc-sample", "sample"),
    valid("short-der-69", F.SHORT_DER_MESSAGE),
    valid("prove-pinned", F.PROVE_TEXT),
    invalid("non-minimal-int", nonMinimal, F.SHORT_DER_MESSAGE),
    invalid("negative-int", negative, "sample"),
    invalid("trailing-bytes", trailing, F.SHORT_DER_MESSAGE),
    invalid("wrong-tag", wrongTag, "sample"),
  ];
  for (const c of cases.slice(3)) {
    let parsed = true;
    try {
      p256.Signature.fromBytes(hexDecode(c.der_hex as string), "der");
    } catch {
      parsed = false;
    }
    check(!parsed, `${c.name} must not parse as DER`);
  }
  return { vectors: "der", version: 1, cases };
}

// ---- firmware.json -----------------------------------------------------------------------------
function firmware(): Json {
  const sk = F.T1_PRIVATE_HEX;
  const pubB64 = b64Encode(pubOf(sk));
  const board = "amoled-175c";
  const version = "1.1.0";
  const size = 1234567;
  const sha = sha256Hex("");
  const signed = signDet(sk, firmwareText(board, version, size, sha));
  const make = (name: string, expect: string, f: { gadget_board?: string; size?: number; der?: Uint8Array }): Json => {
    const gadgetBoard = f.gadget_board ?? board;
    const sz = f.size ?? size;
    const text = firmwareText(gadgetBoard, version, sz, sha);
    const d = f.der ?? signDet(sk, text);
    return {
      name, expect, key_id: "t1", private_key_hex: sk, pubkey_b64: pubB64, board, gadget_board: gadgetBoard,
      version, size: sz, sha256: sha, text, sig_der_hex: hexEncode(d), sig_b64: b64Encode(d),
      deterministic: equalBytes(d, signDet(sk, text)),
    };
  };
  const upperText = ["openmausbot-gadget/1", "firmware", board, version, String(size), sha.toUpperCase()].join("\n");
  const cases = [
    make("t1-amoled", "accept", {}),
    make("other-board", "bad_sig", { gadget_board: "lcd-154", der: signed }),
    make("size-changed", "bad_sig", { size: size + 1, der: signed }),
    make("sha-uppercase", "bad_sig", { der: signDet(sk, upperText) }),
  ];
  check(cases[0].text === F.FIRMWARE_TEXT && cases[0].sig_der_hex === F.FIRMWARE_T1_DER_HEX, "t1-amoled must equal §1.7");
  check(highS(signed), "t1-amoled is high-S");
  return { vectors: "firmware", version: 1, cases };
}

// ---- base64.json -------------------------------------------------------------------------------
function base64(): Json {
  const ok = (name: string, input: string): Json => ({ name, input, canonical: true, bytes_hex: hexEncode(new Uint8Array(Buffer.from(input, "base64"))) });
  const bad = (name: string, input: string): Json => ({ name, input, canonical: false });
  const cases = [
    ok("empty", ""), ok("one-byte", "AA=="), ok("two-bytes", "AAE="), ok("three-bytes", "AAEC"),
    ok("pinned-nonce", F.PINNED_NONCE_B64), ok("rfc-pubkey", F.RFC_PUBKEY_B64),
    bad("no-padding", "AA"), bad("short-padding", "AA="), bad("pad-bits", "AB=="), bad("missing-padding-3", "AAE"),
    bad("trailing-newline", "AA==\n"), bad("leading-space", " AA=="), bad("url-alphabet", "-_8="), bad("concatenated", "AA==AA=="),
  ];
  for (const c of cases) check((Buffer.from(c.input as string, "base64").toString("base64") === c.input) === c.canonical, `${c.name} round-trip`);
  return { vectors: "base64", version: 1, cases };
}

// ---- frames.json -------------------------------------------------------------------------------
function pcm(samples: number): Uint8Array {
  const out = new Uint8Array(samples * 2);
  const view = new DataView(out.buffer);
  for (let i = 0; i < samples; i++) view.setInt16(i * 2, ((i * 37) % 2000) - 1000, true);
  return out;
}
function pattern(len: number, mul: number, add: number): Uint8Array {
  const out = new Uint8Array(len);
  for (let i = 0; i < len; i++) out[i] = (i * mul + add) & 0xff;
  return out;
}
function frames(): Json {
  const frame = (name: string, kind: 1 | 2 | 3, stream: number, payload: Uint8Array): Json => ({
    name, valid: true, frame_hex: hexEncode(encodeBinary(kind, stream, payload)), kind, stream, payload_hex: hexEncode(payload),
  });
  const fw = (name: string, stream: number, offset: number, data: Uint8Array): Json => {
    const f = encodeFwChunk(stream, offset, data);
    return { name, valid: true, frame_hex: hexEncode(f), kind: 4, stream, payload_hex: hexEncode(f.slice(2)), offset, data_hex: hexEncode(data) };
  };
  const bad = (name: string, hex: string): Json => ({ name, valid: false, frame_hex: hex });
  const cases = [
    frame("mic-20ms", 1, 1, pcm(320)),
    frame("speaker-40ms-16k", 2, 1, pcm(640)),
    frame("image-rows", 3, 2, pattern(1200, 5, 1)),
    fw("fw-chunk", 3, 65536, pattern(4096, 31, 7)),
    fw("fw-chunk-last", 3, 1232896, pattern(1671, 31, 7)),
    bad("too-short", "01"),
    bad("stream-zero", "010000"),
    bad("unknown-kind", "050100"),
    bad("fw-no-offset", "0401aabbcc"),
  ];
  check((cases[0].payload_hex as string).length === 1280 && (cases[1].payload_hex as string).length === 2560, "audio frame sizes");
  return { vectors: "frames", version: 1, cases };
}

// ---- versions.json -----------------------------------------------------------------------------
function versions(): Json {
  const compare = [
    ["patch-vs-minor", "1.1.0", "1.0.9", 1], ["numeric-minor", "1.10.0", "1.9.9", 1], ["release-vs-rc", "1.1.0", "1.1.0-rc.1", 1],
    ["rc2-vs-rc1", "1.1.0-rc.2", "1.1.0-rc.1", 1], ["rc10-vs-rc9", "1.1.0-rc.10", "1.1.0-rc.9", 1], ["beta-vs-rc", "1.1.0-beta", "1.1.0-rc", -1],
    ["shorter-prerelease", "1.1.0-rc", "1.1.0-rc.1", -1], ["numeric-vs-alpha", "1.1.0-1", "1.1.0-a", -1],
    ["equal-release", "1.1.0", "1.1.0", 0], ["equal-rc", "1.1.0-rc.1", "1.1.0-rc.1", 0],
    // The pattern accepts these although SemVer 2.0.0 forbids them (PROTOCOL.md §4.1): numeric
    // identifiers compare by value, and an empty pre-release identifier is alphanumeric.
    ["leading-zero-core", "1.01.0", "1.1.0", 0], ["leading-zero-prerelease", "1.1.0-rc.01", "1.1.0-rc.1", 0],
    ["empty-ident-vs-numeric", "1.0.0-rc..1", "1.0.0-rc.1", 1], ["empty-ident-vs-alpha", "1.0.0-.", "1.0.0-a", -1],
  ].map(([name, a, b, cmp]) => ({ name, a, b, cmp }));
  const custom = [
    ["dev-zero", "0.0.0-dev", true], ["dev-tagged", "1.2.0-dev", true], ["release", "1.2.0", false], ["prerelease", "1.2.0-rc.1", false],
    ["leading-v", "v1.2.0", true], ["two-parts", "1.2", true], ["empty", "", true],
    ["leading-zero", "1.01.0", false], ["empty-ident", "1.0.0-.", false],
  ].map(([name, fw, isCustom]) => ({ name, fw, custom: isCustom }));
  for (const c of compare) check(compareVersions(c.a as string, c.b as string) === c.cmp, `compare ${c.name}`);
  for (const c of custom) check(isCustomBuild(c.fw as string) === c.custom, `custom ${c.name}`);
  return { vectors: "versions", version: 1, compare, custom };
}

// ---- writing -----------------------------------------------------------------------------------
const BUILDERS: Record<string, () => Json> = {
  "base64.json": base64, "der.json": der, "firmware.json": firmware, "frames.json": frames,
  "identity.json": identity, "prove.json": prove, "rfc6979.json": rfc6979, "versions.json": versions,
};
const names = Object.keys(BUILDERS).sort();
mkdirSync(OUT, { recursive: true });
for (const stale of readdirSync(OUT)) if (stale.endsWith(".json") && !names.includes(stale)) rmSync(new URL(stale, OUT));
const sums: string[] = [];
for (const name of names) {
  const bytes = utf8(JSON.stringify(BUILDERS[name](), null, 2) + "\n");
  writeFileSync(new URL(name, OUT), bytes);
  sums.push(`${sha256Hex(bytes)}  ${name}`);
}
writeFileSync(new URL("SHA256SUMS", OUT), sums.join("\n") + "\n");
console.log(`wrote ${names.length} vector files and SHA256SUMS to protocol/vectors/`);
