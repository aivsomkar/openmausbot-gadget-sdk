// SPDX-License-Identifier: Apache-2.0
// Approvals (§4.5), push (§4.6), cards, images and actions (§4.7), and settings (§4.3), driven by
// control commands.
import { Buffer } from "node:buffer";
import { randomBytes } from "node:crypto";
import { canonicalJson } from "../../../protocol/lib/encoding.ts";
import type { AskMsg, AskOption, BotRef, PostKind } from "../../../protocol/lib/types.ts";
import { ASK_OPTIONS_MAX, BinaryKind, HOST_SENT_ID_RE, IMAGE_FRAME_PAYLOAD_MAX, TEXT_FRAME_MAX } from "../../../protocol/lib/types.ts";
import type { CommandCall, Feature, HostContext } from "./context.ts";
import { cutChars, foldLatin1 } from "./fold.ts";
import type { GadgetSession } from "./session.ts";
import { speechOf } from "./speech.ts";

export const PERMISSION_OPTIONS: AskOption[] = [
  { id: "allow", label: "Allow", style: "allow" },
  { id: "deny", label: "Deny", style: "deny" },
];
const CARD_TITLE_MAX = 80;
const CARD_BODY_MAX = 600;
const DEFAULT_TTL_S = 30;

const NOT_SENT = "not sent: frame over 16 KiB or gadget closing";
const frameBytes = (msg: unknown): number => Buffer.byteLength(JSON.stringify(msg), "utf8");
/** GadgetSession.send, but a frame that was not sent fails the command instead of acking ok. */
function sendOrThrow(s: GadgetSession, msg: Parameters<GadgetSession["send"]>[0]): void {
  if (!s.send(msg)) throw new Error(NOT_SENT);
}

const newId = (prefix: string): string => prefix + randomBytes(6).toString("hex");
function hostId(call: CommandCall, prefix: string): string {
  const id = call.cmd.id ?? newId(prefix);
  if (typeof id !== "string" || !HOST_SENT_ID_RE.test(id)) throw new Error("id must be 1-40 characters from [A-Za-z0-9_.:-]");
  return id;
}
function text(call: CommandCall, field: string, required = true): string {
  const v = call.cmd[field];
  if (v === undefined && !required) return "";
  if (typeof v !== "string") throw new Error(`${field} must be a string`);
  return v;
}
function ttl(call: CommandCall): number {
  const v = call.cmd.ttl_s ?? DEFAULT_TTL_S;
  if (typeof v !== "number" || !Number.isInteger(v) || v < 0) throw new Error("ttl_s must be a non-negative integer");
  return v;
}

// ---- asks: one at a time per gadget, oldest first, resent on reconnect -------------------------
interface AskQueue { current: AskMsg | null; pending: AskMsg[]; timer: NodeJS.Timeout | null }
const queuesByHost = new WeakMap<HostContext, Map<string, AskQueue>>();
function queueOf(host: HostContext, gadgetId: string): AskQueue {
  let queues = queuesByHost.get(host);
  if (!queues) {
    queues = new Map();
    queuesByHost.set(host, queues);
  }
  let q = queues.get(gadgetId);
  if (!q) {
    q = { current: null, pending: [], timer: null };
    queues.set(gadgetId, q);
  }
  return q;
}

function showNext(host: HostContext, gadgetId: string): void {
  const q = queueOf(host, gadgetId);
  if (q.current || q.pending.length === 0) return;
  const ask = q.pending.shift()!;
  q.current = ask;
  if (ask.expires_s !== undefined) {
    q.timer = setTimeout(() => closeAsk(host, gadgetId, ask.id, "expired"), ask.expires_s * 1000);
  }
  host.live(gadgetId)?.send(ask);
}

function closeAsk(host: HostContext, gadgetId: string, id: string, reason: "answered" | "expired" | "withdrawn"): boolean {
  const q = queueOf(host, gadgetId);
  if (q.current?.id !== id) return false;
  if (q.timer) clearTimeout(q.timer);
  q.timer = null;
  q.current = null;
  host.live(gadgetId)?.send({ op: "ask.close", id, reason });
  showNext(host, gadgetId);
  return true;
}

function askCommand(call: CommandCall): Record<string, unknown> {
  const gadgetId = call.session().record!.id;
  const kind = call.cmd.kind;
  if (kind !== "permission" && kind !== "question") throw new Error("kind must be permission or question");
  let options: AskOption[];
  if (kind === "permission") {
    // Omitted, or exactly Allow and Deny (contract §4.7 lets a script pass them explicitly).
    if (call.cmd.options !== undefined && canonicalJson(call.cmd.options) !== canonicalJson(PERMISSION_OPTIONS)) {
      throw new Error("permission asks always carry Allow and Deny");
    }
    options = PERMISSION_OPTIONS;
  } else {
    const raw = call.cmd.options ?? [];
    if (!Array.isArray(raw) || raw.length > ASK_OPTIONS_MAX) throw new Error("options must be an array of at most 4 entries");
    options = raw.map((o: any) => {
      if (typeof o?.id !== "string" || o.id === "" || typeof o.label !== "string") throw new Error("each option needs an id and a label");
      if (o.style !== undefined && !["allow", "deny", "neutral"].includes(o.style)) throw new Error("style must be allow, deny or neutral");
      return o.style === undefined ? { id: o.id, label: foldLatin1(o.label) } : { id: o.id, label: foldLatin1(o.label), style: o.style };
    });
  }
  const expires = call.cmd.expires_s;
  if (expires !== undefined && (typeof expires !== "number" || !Number.isInteger(expires) || expires < 1)) {
    throw new Error("expires_s must be a positive integer");
  }
  const ask: AskMsg = {
    op: "ask", id: hostId(call, "a_"), kind, title: foldLatin1(text(call, "title")), body: foldLatin1(text(call, "body", false)), options,
  };
  if (expires !== undefined) ask.expires_s = expires;
  // Checked before queueing: an ask that can never be sent would block every ask after it.
  if (frameBytes(ask) > TEXT_FRAME_MAX) throw new Error("ask exceeds the 16 KiB text frame limit");
  const q = queueOf(call.host, gadgetId);
  q.pending.push(ask);
  const queued = q.current !== null;
  showNext(call.host, gadgetId);
  return { id: ask.id, queued };
}

function askCloseCommand(call: CommandCall): void {
  const gadgetId = call.gadgetId();
  const id = text(call, "id");
  const reason = call.cmd.reason ?? "withdrawn";
  if (reason !== "answered" && reason !== "expired" && reason !== "withdrawn") throw new Error("reason must be answered, expired or withdrawn");
  const q = queueOf(call.host, gadgetId);
  const i = q.pending.findIndex((a) => a.id === id);
  if (i >= 0) {
    q.pending.splice(i, 1);
    return;
  }
  if (!closeAsk(call.host, gadgetId, id, reason)) throw new Error(`no open ask ${id}`);
}

// ---- post, card, image, act, settings -----------------------------------------------------------
function postCommand(call: CommandCall): Record<string, unknown> {
  const s = call.session();
  const record = s.record!;
  const kind = call.cmd.kind as PostKind;
  if (kind !== "routine" && kind !== "message") throw new Error("kind must be routine or message");
  const speak = call.cmd.speak ?? record.settings.speak_pushes;
  if (typeof speak !== "boolean") throw new Error("speak must be a boolean");
  const id = hostId(call, "p_");
  const bot: BotRef = { id: record.bot.id, name: foldLatin1(record.bot.name) };
  sendOrThrow(s, { op: "post", id, bot, kind, text: foldLatin1(text(call, "text")), speak });
  if (speak) void speechOf(s).play(undefined, call.host.options.toneMs);
  return { id };
}

function cardCommand(call: CommandCall): Record<string, unknown> {
  const s = call.session();
  const id = hostId(call, "c_");
  sendOrThrow(s, {
    op: "card", id, title: cutChars(foldLatin1(text(call, "title")), CARD_TITLE_MAX),
    body: cutChars(foldLatin1(text(call, "body", false)), CARD_BODY_MAX), ttl_s: ttl(call),
  });
  return { id };
}

/** RGB565 little-endian pixels: "bars" (eight vertical colour bars) or a "#rrggbb" fill. */
export function imagePixels(w: number, h: number, pattern: string): Uint8Array {
  const rgb565 = (r: number, g: number, b: number): number => ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
  const BARS = [[255, 255, 255], [255, 255, 0], [0, 255, 255], [0, 255, 0], [255, 0, 255], [255, 0, 0], [0, 0, 255], [0, 0, 0]];
  let solid: number | null = null;
  if (pattern !== "bars") {
    const m = /^#([0-9a-fA-F]{2})([0-9a-fA-F]{2})([0-9a-fA-F]{2})$/.exec(pattern);
    if (!m) throw new Error('pattern must be "bars" or "#rrggbb"');
    solid = rgb565(parseInt(m[1], 16), parseInt(m[2], 16), parseInt(m[3], 16));
  }
  const out = new Uint8Array(w * h * 2);
  const view = new DataView(out.buffer);
  for (let y = 0; y < h; y++) {
    for (let x = 0; x < w; x++) {
      const c = solid ?? rgb565(...(BARS[Math.floor((x * 8) / w)] as [number, number, number]));
      view.setUint16((y * w + x) * 2, c, true);
    }
  }
  return out;
}

async function imageCommand(call: CommandCall): Promise<Record<string, unknown>> {
  const s = call.session();
  const cap = s.hello!.caps.image;
  const { w, h } = call.cmd;
  if (!cap) throw new Error("this gadget has no image caps");
  if (typeof w !== "number" || typeof h !== "number" || !Number.isInteger(w) || !Number.isInteger(h) || w < 1 || h < 1) {
    throw new Error("w and h must be positive integers");
  }
  if (w > cap.w || h > cap.h) throw new Error(`image must fit ${cap.w}x${cap.h}`);
  const pixels = imagePixels(w, h, typeof call.cmd.pattern === "string" ? call.cmd.pattern : "bars");
  const id = hostId(call, "i_");
  const stream = s.allocStream();
  try {
    sendOrThrow(s, { op: "image.begin", id, stream, w, h, ttl_s: ttl(call) });
    for (let at = 0; at < pixels.length; at += IMAGE_FRAME_PAYLOAD_MAX) {
      if (!(await s.sendBinary(BinaryKind.image, stream, pixels.subarray(at, at + IMAGE_FRAME_PAYLOAD_MAX)))) {
        throw new Error("the gadget disconnected");
      }
    }
    sendOrThrow(s, { op: "image.end", stream });
  } finally {
    s.releaseStream(stream);
  }
  return { id, stream };
}

function actCommand(call: CommandCall): Record<string, unknown> {
  const s = call.session();
  const gadget = s.record!.id;
  const name = text(call, "name");
  const args = call.cmd.args ?? {};
  if (typeof args !== "object" || args === null || Array.isArray(args)) throw new Error("args must be an object");
  const id = hostId(call, "x_");
  const off = s.onOp("act.result", (m) => {
    if (m.id !== id) return;
    finish();
    const e: Record<string, unknown> = { event: "act.result", gadget, id, ok: m.ok === true };
    if (m.data !== undefined) e.data = m.data;
    if (m.error !== undefined) e.error = m.error;
    call.host.emit(e as { event: string });
  });
  const timer = setTimeout(() => {
    finish();
    call.host.emit({ event: "act.result", gadget, id, timeout: true });
  }, call.host.options.actTimeoutMs);
  const offClose = s.onClose(() => clearTimeout(timer));
  function finish(): void {
    off();
    offClose();
    clearTimeout(timer);
  }
  if (!s.send({ op: "act", id, name, args: args as Record<string, unknown> })) {
    finish();
    throw new Error(NOT_SENT);
  }
  return { id };
}

function settingsCommand(call: CommandCall): void {
  const id = call.gadgetId();
  const record = call.host.state.gadgets.get(id);
  if (!record) throw new Error(`unknown gadget ${id}`);
  const { bot, speak_pushes: speak, name } = call.cmd as { bot?: unknown; speak_pushes?: unknown; name?: unknown };
  let nextBot = record.bot;
  let nextSettings = record.settings;
  let nextName = record.name;
  if (bot !== undefined) {
    const b = bot as Record<string, unknown>;
    if (typeof b !== "object" || b === null || typeof b.id !== "string" || typeof b.name !== "string") throw new Error("bot must be {id, name}");
    nextBot = { id: b.id, name: b.name };
  }
  if (speak !== undefined) {
    if (typeof speak !== "boolean") throw new Error("speak_pushes must be a boolean");
    nextSettings = { speak_pushes: speak };
  }
  if (name !== undefined) {
    if (typeof name !== "string" || name.trim() === "") throw new Error("name must be a non-empty string");
    nextName = cutChars(name, 32);
  }
  const base = { op: "settings" as const, bot: { id: nextBot.id, name: foldLatin1(nextBot.name) }, settings: { ...nextSettings } };
  const msg = name === undefined ? base : { ...base, name: foldLatin1(nextName) };
  // Checked before anything changes: a refused command leaves the record as it was.
  if (frameBytes(msg) > TEXT_FRAME_MAX) throw new Error("settings exceed the 16 KiB text frame limit");
  record.bot = nextBot;
  record.settings = nextSettings;
  record.name = nextName;
  const s = call.host.live(id);
  const sent = s ? s.send(msg) : false;
  // Offline, or live but closing: the next `ready` carries bot and settings, and a pending
  // rename is sent right after it.
  if (!sent && name !== undefined) record.namePending = true;
  call.host.state.save();
  if (s && !sent) throw new Error("not sent: the gadget is closing");
}

export const displayFeature: Feature = {
  attach(session: GadgetSession, host: HostContext) {
    const gadgetId = session.record!.id;
    const q = queueOf(host, gadgetId);
    if (q.current) session.send(q.current);
    session.onOp("answer", (m) => {
      const cur = q.current;
      if (!cur || m.id !== cur.id || !cur.options.some((o) => o.id === m.option)) return;
      host.emit({ event: "answer", gadget: gadgetId, id: m.id, option: m.option });
      closeAsk(host, gadgetId, cur.id, "answered");
    });
  },
  commands: {
    ask: askCommand,
    "ask.close": askCloseCommand,
    post: postCommand,
    card: cardCommand,
    "card.close": (call) => {
      sendOrThrow(call.session(), { op: "card.close", id: text(call, "id") });
    },
    image: imageCommand,
    act: actCommand,
    settings: settingsCommand,
  },
};
