// SPDX-License-Identifier: Apache-2.0
// One gadget connection: liveness, the §4.3 handshake, and op/binary dispatch after `ready`.
import { Buffer } from "node:buffer";
import { randomBytes } from "node:crypto";
import { decodeBinary, encodeBinary, type Kind } from "../../../protocol/lib/frames.ts";
import type { ChallengeMsg, GadgetErrorCode, HostToGadget } from "../../../protocol/lib/types.ts";
import { BINARY_FRAME_MAX, TEXT_FRAME_MAX } from "../../../protocol/lib/types.ts";
import type { HostContext } from "./context.ts";
import { checkHello, decideProve, makeChallenge, type NormalizedHello } from "./enroll.ts";
import { foldLatin1 } from "./fold.ts";
import type { GadgetRecord } from "./state.ts";

/** The parts of a `ws` WebSocket the session uses. */
export interface WsLike {
  send(data: string | Uint8Array, cb?: (err?: Error) => void): void;
  close(code?: number, reason?: string): void;
  terminate(): void;
  ping(): void;
  on(event: string, listener: (...args: any[]) => void): void;
  readonly bufferedAmount: number;
}

export interface SessionHooks {
  /** prove verified: replace any older session with this id and make this one live. */
  takeOver(session: GadgetSession): void;
  /** ready (and any pending settings) sent: attach features. */
  attach(session: GadgetSession): void;
  closed(session: GadgetSession, code: number): void;
}

type OpListener = (msg: any) => void;
type BinaryListener = (kind: Kind, stream: number, payload: Uint8Array) => void;

const SEND_BUFFER_MAX = 256 * 1024;

export class GadgetSession {
  readonly remote: string;
  readonly host: HostContext;
  phase: "hello" | "challenged" | "ready" | "closed" = "hello";
  hello: NormalizedHello | null = null;
  record: GadgetRecord | null = null;
  sessionId = "";
  private readonly ws: WsLike;
  private readonly hooks: SessionHooks;
  private challenge: ChallengeMsg | null = null;
  /** Set once the host starts closing (error, close, terminate). `ws` still emits 'message'
   *  while CLOSING, and PROTOCOL.md §4.3 says nothing after `error` is processed. */
  private closing = false;
  private readonly ops = new Map<string, Set<OpListener>>();
  private readonly binaries = new Set<BinaryListener>();
  private readonly closers = new Set<(code: number) => void>();
  private readonly streams = new Set<number>();
  private nextStream = 1;
  private readonly pingTimer: NodeJS.Timeout;
  private readonly idleTimer: NodeJS.Timeout;
  private readonly handshakeTimer: NodeJS.Timeout;

  constructor(ws: WsLike, remote: string, host: HostContext, hooks: SessionHooks) {
    this.ws = ws;
    this.remote = remote;
    this.host = host;
    this.hooks = hooks;
    const o = host.options;
    this.pingTimer = setInterval(() => this.ws.ping(), o.pingMs);
    this.idleTimer = setTimeout(() => {
      this.host.log(`${this.label()}: no inbound frame for ${o.idleMs} ms, dropping`);
      this.terminate();
    }, o.idleMs);
    this.handshakeTimer = setTimeout(() => {
      if (this.phase !== "ready") this.close(1008, "handshake timeout");
    }, o.handshakeMs);
    ws.on("message", (data: Buffer, isBinary: boolean) => {
      this.idleTimer.refresh();
      if (this.closing) return;
      if (isBinary) this.onBinaryFrame(new Uint8Array(data.buffer, data.byteOffset, data.byteLength));
      else this.onText(data.toString("utf8"));
    });
    ws.on("ping", () => this.idleTimer.refresh());
    ws.on("pong", () => this.idleTimer.refresh());
    ws.on("error", (err: Error) => this.host.log(`${this.label()}: ${err.message}`));
    ws.on("close", (code: number) => this.onClosed(code));
  }

  get gadgetId(): string | null {
    return this.phase === "ready" && this.record ? this.record.id : null;
  }
  get closed(): boolean {
    return this.phase === "closed";
  }
  private label(): string {
    return this.record?.id ?? this.hello?.id ?? this.remote;
  }

  /** Sends one text frame. Strings for the screen must already be folded. */
  send(msg: HostToGadget): boolean {
    if (this.phase === "closed" || this.closing) return false;
    const text = JSON.stringify(msg);
    if (Buffer.byteLength(text, "utf8") > TEXT_FRAME_MAX) {
      this.host.log(`${this.label()}: refusing to send a ${msg.op} frame over 16 KiB`);
      return false;
    }
    this.ws.send(text);
    this.host.emit({ event: "tx", gadget: this.gadgetId, msg: text });
    return true;
  }

  /** Sends one binary frame and resolves once it is written to the socket (false if closed). */
  sendBinary(kind: Kind, stream: number, payload: Uint8Array): Promise<boolean> {
    if (this.phase === "closed" || this.closing) return Promise.resolve(false);
    if (this.ws.bufferedAmount > SEND_BUFFER_MAX) {
      this.close(1008, "send buffer full");
      return Promise.resolve(false);
    }
    const frame = encodeBinary(kind, stream, payload);
    return new Promise((resolve) => this.ws.send(frame, (err) => resolve(!err)));
  }

  /** A host-assigned stream id (1–255) that is not in use. */
  allocStream(): number {
    for (let i = 0; i < 255; i++) {
      const s = this.nextStream;
      this.nextStream = s === 255 ? 1 : s + 1;
      if (!this.streams.has(s)) {
        this.streams.add(s);
        return s;
      }
    }
    throw new Error("all 255 host streams are in use");
  }
  releaseStream(stream: number): void {
    this.streams.delete(stream);
  }

  onOp(op: string, listener: OpListener): () => void {
    const set = this.ops.get(op) ?? new Set<OpListener>();
    set.add(listener);
    this.ops.set(op, set);
    return () => set.delete(listener);
  }
  onBinary(listener: BinaryListener): () => void {
    this.binaries.add(listener);
    return () => this.binaries.delete(listener);
  }
  onClose(listener: (code: number) => void): () => void {
    this.closers.add(listener);
    return () => this.closers.delete(listener);
  }

  /** Sends `error {code, message}` and closes with 1000 (PROTOCOL.md §4.3). */
  fail(code: GadgetErrorCode, message: string): void {
    if (this.phase === "closed" || this.closing) return;
    this.ws.send(JSON.stringify({ op: "error", code, message }));
    this.host.emit({ event: "tx", gadget: this.gadgetId, msg: JSON.stringify({ op: "error", code, message }) });
    this.close(1000, code);
  }
  close(code = 1000, reason = ""): void {
    if (this.phase === "closed" || this.closing) return;
    this.closing = true;
    this.ws.close(code, reason);
  }
  terminate(): void {
    this.closing = true;
    this.ws.terminate();
  }

  private onText(text: string): void {
    this.host.emit({ event: "rx", gadget: this.gadgetId, msg: text });
    let msg: unknown;
    try {
      msg = JSON.parse(text);
    } catch {
      return;
    }
    if (typeof msg !== "object" || msg === null || Array.isArray(msg)) return;
    const m = msg as Record<string, unknown>;
    if (typeof m.op !== "string") return;
    if (this.phase === "hello") {
      if (m.op === "hello") this.onHello(m);
    } else if (this.phase === "challenged") {
      if (m.op === "prove") this.onProve(m);
    } else if (this.phase === "ready") {
      for (const listener of [...(this.ops.get(m.op) ?? [])]) listener(m);
    }
  }

  private refuse(code: GadgetErrorCode, message: string): void {
    this.host.emit({ event: "refused", gadget: this.hello?.id ?? null, code, message });
    this.host.log(`${this.label()}: refused ${code} (${message})`);
    this.fail(code, message);
  }

  private onHello(m: Record<string, unknown>): void {
    const r = checkHello(m);
    if (!r.ok) return this.refuse(r.code, r.message);
    this.hello = r.hello;
    this.challenge = makeChallenge(this.host.state.hostId, this.host.state.hostName);
    this.phase = "challenged";
    this.send(this.challenge);
  }

  private onProve(m: Record<string, unknown>): void {
    const d = decideProve({
      hello: this.hello!, challenge: this.challenge!, prove: m, state: this.host.state,
      bot: this.host.options.bot, maxDevices: this.host.options.maxDevices, now: Date.now(),
    });
    if (!d.ok) return this.refuse(d.code, d.message);
    clearTimeout(this.handshakeTimer);
    this.record = d.record;
    this.sessionId = "s_" + randomBytes(6).toString("hex");
    this.phase = "ready";
    if (d.enrolled) this.host.emit({ event: "enrolled", gadget: d.record.id });
    this.hooks.takeOver(this);
    const bot = { id: d.record.bot.id, name: foldLatin1(d.record.bot.name) };
    this.send({ op: "ready", session: this.sessionId, bot, settings: { ...d.record.settings } });
    if (d.sendName) this.send({ op: "settings", bot, settings: { ...d.record.settings }, name: foldLatin1(d.record.name) });
    this.host.emit({ event: "ready", gadget: d.record.id, session: this.sessionId });
    this.host.log(`${d.record.id} ready (${d.record.name}, ${d.record.board}, fw ${d.record.fw})`);
    this.hooks.attach(this);
  }

  private onBinaryFrame(frame: Uint8Array): void {
    if (frame.length > BINARY_FRAME_MAX) return this.close(1009, "binary frame over 8 KiB");
    const d = decodeBinary(frame);
    if (!d) return;
    this.host.emit({ event: "rx_binary", gadget: this.gadgetId, kind: d.kind, stream: d.stream, bytes: d.payload.length });
    if (this.phase !== "ready") return;
    for (const listener of [...this.binaries]) listener(d.kind, d.stream, d.payload);
  }

  private onClosed(code: number): void {
    if (this.phase === "closed") return;
    this.phase = "closed";
    clearInterval(this.pingTimer);
    clearTimeout(this.idleTimer);
    clearTimeout(this.handshakeTimer);
    for (const listener of [...this.closers]) listener(code);
    this.hooks.closed(this, code);
  }
}
