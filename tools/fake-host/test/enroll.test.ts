// SPDX-License-Identifier: Apache-2.0
import { test } from "node:test";
import assert from "node:assert/strict";
import { mkdtempSync, readFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { b64Encode } from "../../../protocol/lib/encoding.ts";
import { gadgetIdFromPubkey, proveText } from "../../../protocol/lib/identity.ts";
import { publicKeyFromPrivate, signP256 } from "../../../protocol/lib/verify.ts";
import type { ChallengeMsg } from "../../../protocol/lib/types.ts";
import { checkHello, decideProve, makeChallenge, normalizeName, type NormalizedHello } from "../src/enroll.ts";
import { HostState } from "../src/state.ts";

const KEY = "c9afa9d845ba75166b5c215767b1d6934e50c3db36e89b127b8a622b120f6721";
const PUB = publicKeyFromPrivate(KEY);
const ID = gadgetIdFromPubkey(PUB);
const BOT = { id: "b_fake", name: "Fake Bot" };
const hello = (over: Record<string, unknown> = {}): Record<string, unknown> => ({
  op: "hello", proto: 1, id: ID, pubkey: b64Encode(PUB), name: "Desk Maus", board: "amoled-175c", fw: "1.0.0",
  caps: { speaker: { rate: 16000 } }, actions: [], sensors: {}, ...over,
});
function normalized(): NormalizedHello {
  const r = checkHello(hello());
  assert.ok(r.ok);
  return r.hello;
}
function prove(challenge: ChallengeMsg, enroll?: string, key = KEY): Record<string, unknown> {
  const sig = b64Encode(signP256(key, proveText(ID, challenge.nonce, challenge.host_id)));
  return enroll === undefined ? { op: "prove", sig } : { op: "prove", sig, enroll };
}
function freshState(): HostState {
  const s = new HostState("000102030405060708090a0b0c0d0e0f", "Fake MausBot", null);
  s.openWindow("123456", 120, 1000);
  return s;
}

test("checkHello applies rule 1 and normalizes the hello", () => {
  assert.equal(normalized().id, "gad_b18b86ce1389e46d");
  assert.deepEqual(checkHello(hello({ proto: 2 })), { ok: false, code: "proto_unsupported", message: "proto 2 is not supported" });
  const pubB64 = b64Encode(PUB);
  const nonCanonical = pubB64.slice(0, -2) + String.fromCharCode(pubB64.charCodeAt(pubB64.length - 2) + 1) + "=";
  for (const bad of [{ pubkey: nonCanonical }, { pubkey: b64Encode(PUB.slice(0, 33)) }, { id: "gad_0000000000000000" }, { board: "Bad Board" }]) {
    const r = checkHello(hello(bad));
    assert.equal(r.ok ? "ok" : r.code, "bad_sig", JSON.stringify(bad));
  }
});

test("checkHello cuts the name, drops actions that break a limit and defaults risk to confirm", () => {
  const long = "x".repeat(40);
  const ok = { name: "chime", description: "Play a short chime.", params: { type: "object" } };
  const actions = [
    ok, { ...ok, name: "safe_one", risk: "safe" }, { ...ok, name: "Bad Name" }, { ...ok, name: "long_desc", description: "d".repeat(201) },
    { ...ok, name: "big_params", params: { type: "object", description: "p".repeat(1100) } },
    ...Array.from({ length: 20 }, (_, i) => ({ ...ok, name: `extra_${i}` })),
  ];
  const r = checkHello(hello({ name: long, actions }));
  assert.ok(r.ok);
  assert.equal(r.hello.name, "x".repeat(32));
  assert.equal(r.hello.actions.length, 16);
  assert.deepEqual(r.hello.actions.slice(0, 2).map((a) => [a.name, a.risk]), [["chime", "confirm"], ["safe_one", "safe"]]);
  assert.ok(!r.hello.actions.some((a) => ["Bad Name", "long_desc", "big_params"].includes(a.name)));
  assert.equal(normalizeName("", ID), "Maus b18b");
  assert.equal(normalizeName("a" + String.fromCharCode(7) + "b", ID), "ab");
});

test("makeChallenge sends 32 fresh random bytes and a folded host name", () => {
  const a = makeChallenge("000102030405060708090a0b0c0d0e0f", "Omkar’s Mac 🖥");
  const b = makeChallenge("000102030405060708090a0b0c0d0e0f", "x");
  assert.equal(a.nonce.length, 44);
  assert.notEqual(a.nonce, b.nonce);
  assert.equal(a.host_name, "Omkar's Mac ");
});

test("rule 3: the right code enrolls once and consumes the window", () => {
  const state = freshState();
  const h = normalized();
  const ch = makeChallenge(state.hostId, state.hostName);
  const r = decideProve({ hello: h, challenge: ch, prove: prove(ch, "123456"), state, bot: BOT, maxDevices: 20, now: 2000 });
  assert.ok(r.ok && r.enrolled);
  assert.deepEqual(r.record.bot, BOT);
  assert.equal(state.window!.used, true);
  const other = checkHello(hello({ id: gadgetIdFromPubkey(publicKeyFromPrivate("11".repeat(32))), pubkey: b64Encode(publicKeyFromPrivate("11".repeat(32))) }));
  assert.ok(other.ok);
  const ch2 = makeChallenge(state.hostId, state.hostName);
  const sig = b64Encode(signP256("11".repeat(32), proveText(other.hello.id, ch2.nonce, ch2.host_id)));
  const r2 = decideProve({ hello: other.hello, challenge: ch2, prove: { sig, enroll: "123456" }, state, bot: BOT, maxDevices: 20, now: 2000 });
  assert.deepEqual(r2, { ok: false, code: "bad_code", message: "pairing code used up" });
});

test("rule 3: wrong codes use attempts, then the window is used up; expiry is reported", () => {
  const state = freshState();
  const h = normalized();
  const ch = makeChallenge(state.hostId, state.hostName);
  for (let i = 0; i < 5; i++) {
    const r = decideProve({ hello: h, challenge: ch, prove: prove(ch, i === 0 ? "12345" : "654321"), state, bot: BOT, maxDevices: 20, now: 2000 });
    assert.deepEqual(r, { ok: false, code: "bad_code", message: "pairing code wrong" });
  }
  const used = decideProve({ hello: h, challenge: ch, prove: prove(ch, "123456"), state, bot: BOT, maxDevices: 20, now: 2000 });
  assert.deepEqual(used, { ok: false, code: "bad_code", message: "pairing code used up" });
  const late = freshState();
  const r = decideProve({ hello: h, challenge: ch, prove: prove(ch, "123456"), state: late, bot: BOT, maxDevices: 20, now: 1000 + 120_000 });
  assert.deepEqual(r, { ok: false, code: "bad_code", message: "pairing code expired" });
});

test("rule 3: device_limit is checked only after the code matches and keeps the window open", () => {
  const state = freshState();
  const h = normalized();
  const ch = makeChallenge(state.hostId, state.hostName);
  const wrong = decideProve({ hello: h, challenge: ch, prove: prove(ch, "000000"), state, bot: BOT, maxDevices: 0, now: 2000 });
  assert.equal(wrong.ok ? "ok" : wrong.code, "bad_code");
  const full = decideProve({ hello: h, challenge: ch, prove: prove(ch, "123456"), state, bot: BOT, maxDevices: 0, now: 2000 });
  assert.deepEqual(full, { ok: false, code: "device_limit", message: "device limit reached" });
  assert.equal(state.window!.used, false);
  const ok = decideProve({ hello: h, challenge: ch, prove: prove(ch, "123456"), state, bot: BOT, maxDevices: 1, now: 2000 });
  assert.ok(ok.ok && ok.enrolled);
});

test("rules 2 and 4: a known id ignores enroll; an unknown id without enroll is refused; bad signatures fail", () => {
  const state = freshState();
  const h = normalized();
  const ch = makeChallenge(state.hostId, state.hostName);
  assert.deepEqual(decideProve({ hello: h, challenge: ch, prove: prove(ch), state, bot: BOT, maxDevices: 20, now: 2000 }),
    { ok: false, code: "enroll_required", message: "this gadget is not paired" });
  assert.ok(decideProve({ hello: h, challenge: ch, prove: prove(ch, "123456"), state, bot: BOT, maxDevices: 20, now: 2000 }).ok);
  const again = decideProve({ hello: h, challenge: ch, prove: prove(ch, "999999"), state, bot: BOT, maxDevices: 20, now: 3000 });
  assert.ok(again.ok && !again.enrolled);
  const forged = decideProve({ hello: h, challenge: ch, prove: prove(ch, undefined, "22".repeat(32)), state, bot: BOT, maxDevices: 20, now: 3000 });
  assert.deepEqual(forged, { ok: false, code: "bad_sig", message: "signature does not verify" });
  const otherHost = { ...ch, host_id: "000102030405060708090a0b0c0d0e0e" };
  const replayed = decideProve({ hello: h, challenge: otherHost, prove: prove(ch), state, bot: BOT, maxDevices: 20, now: 3000 });
  assert.equal(replayed.ok ? "ok" : replayed.code, "bad_sig");
  assert.equal(decideProve({ hello: h, challenge: ch, prove: { sig: "not base64" }, state, bot: BOT, maxDevices: 20, now: 3000 }).ok, false);
});

test("name: last writer wins, except a pending host-side rename", () => {
  const state = freshState();
  const h = normalized();
  const ch = makeChallenge(state.hostId, state.hostName);
  const first = decideProve({ hello: h, challenge: ch, prove: prove(ch, "123456"), state, bot: BOT, maxDevices: 20, now: 2000 });
  assert.ok(first.ok);
  first.record.name = "Kitchen";
  first.record.namePending = true;
  const next = decideProve({ hello: h, challenge: ch, prove: prove(ch), state, bot: BOT, maxDevices: 20, now: 3000 });
  assert.ok(next.ok && next.sendName);
  assert.equal(next.record.name, "Kitchen");
  const after = decideProve({ hello: { ...h, name: "Desk 2" }, challenge: ch, prove: prove(ch), state, bot: BOT, maxDevices: 20, now: 4000 });
  assert.ok(after.ok && !after.sendName);
  assert.equal(after.record.name, "Desk 2");
});

test("HostState persists host_id and gadgets, never the window", () => {
  const dir = mkdtempSync(join(tmpdir(), "fake-host-"));
  const a = HostState.load({ stateDir: dir, hostId: null, hostName: "x" });
  a.openWindow("123456", 120, 0);
  const h = normalized();
  const ch = makeChallenge(a.hostId, a.hostName);
  assert.ok(decideProve({ hello: h, challenge: ch, prove: prove(ch, "123456"), state: a, bot: BOT, maxDevices: 20, now: 1 }).ok);
  const b = HostState.load({ stateDir: dir, hostId: null, hostName: "x" });
  assert.equal(b.hostId, a.hostId);
  assert.ok(b.gadgets.has(ID));
  assert.equal(b.window, null);
  assert.match(a.hostId, /^[0-9a-f]{32}$/);
  assert.equal(JSON.parse(readFileSync(join(dir, "fake-host.json"), "utf8")).version, 1);
});
