// SPDX-License-Identifier: Apache-2.0
// Fake host options: the CLI of docs/plans/00-interfaces.md §4.7, plus timing knobs that only
// in-process tests set (the CLI always uses the protocol's real timings).
import { resolve } from "node:path";
import { fileURLToPath } from "node:url";
import { parseArgs } from "node:util";
import type { BotRef } from "../../../protocol/lib/types.ts";
import {
  ACT_TIMEOUT_MS, FW_READY_TIMEOUT_MS, HANDSHAKE_TIMEOUT_MS, HOST_ID_RE, IDLE_TIMEOUT_MS, PAIR_CODE_RE,
  PING_INTERVAL_MS, REPLY_MIN_INTERVAL_MS,
} from "../../../protocol/lib/types.ts";

export interface FakeHostOptions {
  port: number;
  bind: string;
  code: string | null;          // null = a random six-digit code
  codeTtlS: number;
  stateDir: string | null;      // null = memory only
  hostId: string | null;        // null = random (or the one in the state file)
  hostName: string;
  bot: BotRef;
  heard: string;
  reply: string;
  toneMs: number;
  otaKeyFile: string;
  otaKeyId: string;
  quiet: boolean;
  maxDevices: number;
  doneBeforeSpeech: boolean;    // send done ok right after the final reply, then the speech (MausBot's usual order)
  // In-process only (tests): protocol timings.
  pingMs: number;
  idleMs: number;
  handshakeMs: number;
  replyIntervalMs: number;
  actTimeoutMs: number;
  fwReadyTimeoutMs: number;
}

export const DEFAULT_OTA_KEY_FILE = fileURLToPath(new URL("../../../keys/test-t1.key.hex", import.meta.url));

export const DEFAULT_OPTIONS: FakeHostOptions = {
  port: 8810,
  bind: "127.0.0.1",
  code: null,
  codeTtlS: 120,
  stateDir: null,
  hostId: null,
  hostName: "Fake MausBot",
  bot: { id: "b_fake", name: "Fake Bot" },
  heard: "What's on my calendar today?",
  reply: "You have two meetings today: design review at 10 and lunch with Sam at 1.",
  toneMs: 800,
  otaKeyFile: DEFAULT_OTA_KEY_FILE,
  otaKeyId: "t1",
  quiet: false,
  maxDevices: 20,
  doneBeforeSpeech: false,
  pingMs: PING_INTERVAL_MS,
  idleMs: IDLE_TIMEOUT_MS,
  handshakeMs: HANDSHAKE_TIMEOUT_MS,
  replyIntervalMs: REPLY_MIN_INTERVAL_MS,
  actTimeoutMs: ACT_TIMEOUT_MS,
  fwReadyTimeoutMs: FW_READY_TIMEOUT_MS,
};

function int(name: string, value: string, min: number, max: number): number {
  const n = Number(value);
  if (!/^\d+$/.test(value) || n < min || n > max) throw new Error(`--${name} must be an integer from ${min} to ${max}`);
  return n;
}

/** "<id>:<name>"; the name may contain ":". "" before the colon means an unbound gadget. */
export function parseBot(value: string): BotRef {
  const at = value.indexOf(":");
  if (at < 0) throw new Error("--bot must be <id>:<name>");
  return { id: value.slice(0, at), name: value.slice(at + 1) };
}

/** Parses argv (without node and the script). Throws Error with a message for bad input. */
export function parseCli(argv: string[]): FakeHostOptions {
  const { values } = parseArgs({
    args: argv,
    strict: true,
    allowPositionals: false,
    options: {
      port: { type: "string" }, bind: { type: "string" }, code: { type: "string" }, "code-ttl": { type: "string" },
      state: { type: "string" }, "host-id": { type: "string" }, "host-name": { type: "string" }, bot: { type: "string" },
      heard: { type: "string" }, reply: { type: "string" }, "tone-ms": { type: "string" }, "ota-key": { type: "string" },
      "ota-key-id": { type: "string" }, quiet: { type: "boolean" }, "max-devices": { type: "string" },
      "done-before-speech": { type: "boolean" },
    },
  });
  const o: FakeHostOptions = { ...DEFAULT_OPTIONS };
  if (values.port !== undefined) o.port = int("port", values.port, 0, 65535);
  if (values.bind !== undefined) o.bind = values.bind;
  if (values.code !== undefined) {
    if (!PAIR_CODE_RE.test(values.code)) throw new Error("--code must be six digits");
    o.code = values.code;
  }
  if (values["code-ttl"] !== undefined) o.codeTtlS = int("code-ttl", values["code-ttl"], 1, 86400);
  if (values.state !== undefined) o.stateDir = resolve(values.state);
  if (values["host-id"] !== undefined) {
    if (!HOST_ID_RE.test(values["host-id"])) throw new Error("--host-id must be 32 lowercase hex characters");
    o.hostId = values["host-id"];
  }
  if (values["host-name"] !== undefined) o.hostName = values["host-name"];
  if (values.bot !== undefined) o.bot = parseBot(values.bot);
  if (values.heard !== undefined) o.heard = values.heard;
  if (values.reply !== undefined) o.reply = values.reply;
  if (values["tone-ms"] !== undefined) o.toneMs = int("tone-ms", values["tone-ms"], 0, 60000);
  if (values["ota-key"] !== undefined) o.otaKeyFile = resolve(values["ota-key"]);
  if (values["ota-key-id"] !== undefined) o.otaKeyId = values["ota-key-id"];
  if (values.quiet) o.quiet = true;
  if (values["max-devices"] !== undefined) o.maxDevices = int("max-devices", values["max-devices"], 0, 1000);
  if (values["done-before-speech"]) o.doneBeforeSpeech = true;
  return o;
}
