// SPDX-License-Identifier: Apache-2.0
// The host rules of PROTOCOL.md §4.3, with node:crypto verification (accepts high-S signatures).
import { Buffer } from "node:buffer";
import { randomBytes } from "node:crypto";
import { b64DecodeCanonical, b64Encode } from "../../../protocol/lib/encoding.ts";
import { gadgetIdFromPubkey, proveText } from "../../../protocol/lib/identity.ts";
import { verifyP256 } from "../../../protocol/lib/verify.ts";
import type {
  BotRef, ChallengeMsg, GadgetActionDecl, GadgetCaps, GadgetErrorCode, GadgetSensors, Risk,
} from "../../../protocol/lib/types.ts";
import {
  ACTION_DESCRIPTION_MAX, ACTION_NAME_RE, ACTION_PARAMS_MAX_BYTES, ACTIONS_MAX, BOARD_ID_RE, NAME_MAX_CHARS, PROTO_VERSION,
} from "../../../protocol/lib/types.ts";
import { cutChars, cutUtf8, foldLatin1 } from "./fold.ts";
import type { GadgetRecord, HostState } from "./state.ts";

export interface NormalizedHello {
  proto: number;
  id: string;
  pubkey: string;
  pubkeyBytes: Uint8Array;
  name: string;
  board: string;
  fw: string;
  caps: GadgetCaps;
  actions: Array<GadgetActionDecl & { risk: Risk }>;
  sensors: GadgetSensors;
}

export type HelloCheck =
  | { ok: true; hello: NormalizedHello }
  | { ok: false; code: "proto_unsupported" | "bad_sig"; message: string };

const isObject = (v: unknown): v is Record<string, unknown> => typeof v === "object" && v !== null && !Array.isArray(v);

/** Display name: control characters removed, cut to 32 characters; "" → "Maus " + 4 hex of the id. */
export function normalizeName(name: unknown, id: string): string {
  const clean = typeof name === "string" ? cutChars(name.replace(/\p{Cc}/gu, "").trim(), NAME_MAX_CHARS) : "";
  return clean === "" ? "Maus " + id.slice(4, 8) : clean;
}

function normalizeActions(list: unknown): Array<GadgetActionDecl & { risk: Risk }> {
  if (!Array.isArray(list)) return [];
  const out: Array<GadgetActionDecl & { risk: Risk }> = [];
  for (const a of list) {
    if (out.length >= ACTIONS_MAX) break;
    if (!isObject(a) || typeof a.name !== "string" || !ACTION_NAME_RE.test(a.name)) continue;
    if (typeof a.description !== "string" || [...a.description].length > ACTION_DESCRIPTION_MAX) continue;
    if (!isObject(a.params) || Buffer.byteLength(JSON.stringify(a.params), "utf8") > ACTION_PARAMS_MAX_BYTES) continue;
    out.push({ name: a.name, description: a.description, params: a.params, risk: a.risk === "safe" ? "safe" : "confirm" });
  }
  return out;
}

/** Rule 1 on `hello`: proto, and a canonical 65-byte SEC1 pubkey that hashes to id. */
export function checkHello(msg: Record<string, unknown>): HelloCheck {
  if (msg.proto !== PROTO_VERSION) return { ok: false, code: "proto_unsupported", message: `proto ${String(msg.proto)} is not supported` };
  const pub = typeof msg.pubkey === "string" ? b64DecodeCanonical(msg.pubkey) : null;
  if (!pub || pub.length !== 65 || pub[0] !== 0x04) {
    return { ok: false, code: "bad_sig", message: "pubkey must be canonical base64 of a 65-byte SEC1 point" };
  }
  const id = gadgetIdFromPubkey(pub);
  if (msg.id !== id) return { ok: false, code: "bad_sig", message: "id does not match pubkey" };
  if (typeof msg.board !== "string" || !BOARD_ID_RE.test(msg.board)) return { ok: false, code: "bad_sig", message: "bad board id" };
  return {
    ok: true,
    hello: {
      proto: PROTO_VERSION, id, pubkey: msg.pubkey as string, pubkeyBytes: pub,
      name: normalizeName(msg.name, id), board: msg.board, fw: typeof msg.fw === "string" ? msg.fw : "",
      caps: isObject(msg.caps) ? (msg.caps as GadgetCaps) : {}, actions: normalizeActions(msg.actions),
      sensors: isObject(msg.sensors) ? (msg.sensors as GadgetSensors) : {},
    },
  };
}

/** A fresh 32-byte nonce per connection; host_name folded and cut to 64 UTF-8 bytes. */
export function makeChallenge(hostId: string, hostName: string): ChallengeMsg {
  return { op: "challenge", nonce: b64Encode(randomBytes(32)), host_id: hostId, host_name: cutUtf8(foldLatin1(hostName), 64) };
}

export type ProveDecision =
  | { ok: true; record: GadgetRecord; enrolled: boolean; sendName: boolean }
  | { ok: false; code: GadgetErrorCode; message: string };

/** Rules 1 (signature) to 4 on `prove`. Mutates state only on success. */
export function decideProve(input: {
  hello: NormalizedHello;
  challenge: ChallengeMsg;
  prove: Record<string, unknown>;
  state: HostState;
  bot: BotRef;
  maxDevices: number;
  now: number;
}): ProveDecision {
  const { hello, challenge, prove, state, now } = input;
  const sig = typeof prove.sig === "string" ? b64DecodeCanonical(prove.sig) : null;
  if (!sig || !verifyP256(hello.pubkeyBytes, proveText(hello.id, challenge.nonce, challenge.host_id), sig)) {
    return { ok: false, code: "bad_sig", message: "signature does not verify" };
  }
  const known = state.gadgets.get(hello.id);
  if (known) {
    if (known.pubkey !== hello.pubkey) return { ok: false, code: "bad_sig", message: "pubkey differs from the enrolled key" };
    const sendName = known.namePending;
    if (!sendName) known.name = hello.name;
    known.namePending = false;
    known.board = hello.board;
    known.fw = hello.fw;
    known.lastSeenAt = now;
    state.save();
    return { ok: true, record: known, enrolled: false, sendName };
  }
  if (typeof prove.enroll !== "string") return { ok: false, code: "enroll_required", message: "this gadget is not paired" };
  const check = state.checkCode(prove.enroll, now);
  if (!check.ok) return { ok: false, code: "bad_code", message: `pairing code ${check.reason}` };
  if (state.gadgets.size >= input.maxDevices) return { ok: false, code: "device_limit", message: "device limit reached" };
  const record: GadgetRecord = {
    id: hello.id, pubkey: hello.pubkey, name: hello.name, board: hello.board, fw: hello.fw,
    bot: { ...input.bot }, settings: { speak_pushes: false }, namePending: false, createdAt: now, lastSeenAt: now,
  };
  state.gadgets.set(record.id, record);
  state.consumeWindow();
  state.save();
  return { ok: true, record, enrolled: true, sendName: false };
}
