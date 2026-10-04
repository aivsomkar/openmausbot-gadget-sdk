// SPDX-License-Identifier: Apache-2.0
// The gadget side of openmausbot-gadget/1, for the fake host's tests: handshake with node:crypto
// signatures, and an inbox that tests wait on (no sleeps).
import { Buffer } from "node:buffer";
import { generateKeyPairSync } from "node:crypto";
import { WebSocket } from "ws";
import { b64Encode } from "../../../protocol/lib/encoding.ts";
import { encodeBinary, decodeBinary, type Kind } from "../../../protocol/lib/frames.ts";
import { gadgetIdFromPubkey, proveText } from "../../../protocol/lib/identity.ts";
import { publicKeyFromPrivate, signP256 } from "../../../protocol/lib/verify.ts";
import type { GadgetCaps } from "../../../protocol/lib/types.ts";
import type { FakeHost } from "../src/server.ts";
import type { HostEvent } from "../src/context.ts";

export const CAPS: GadgetCaps = {
  screen: { w: 466, h: 466, round: true, text: "latin1" }, image: { w: 300, h: 300 },
  mic: { rate: 16000 }, speaker: { rate: 16000 }, input: ["touch", "talk", "cancel"], battery: true, ota: { max: 6291456 },
};

export function randomKey(): string {
  const { privateKey } = generateKeyPairSync("ec", { namedCurve: "prime256v1" });
  return Buffer.from(privateKey.export({ format: "jwk" }).d as string, "base64url").toString("hex").padStart(64, "0");
}

export interface GadgetOptions {
  port: number;
  key?: string;               // 64 hex; random when absent
  enroll?: string;            // pairing code sent in prove
  name?: string;
  board?: string;
  fw?: string;
  caps?: GadgetCaps;
  actions?: unknown[];
  proto?: number;
}

export interface BinaryFrame { kind: Kind; stream: number; payload: Uint8Array }
type Msg = Record<string, any>;
interface Waiter<T> { match: (x: T) => boolean; resolve: (x: T) => void }

export class TestGadget {
  readonly ws: WebSocket;
  readonly key: string;
  readonly pub: Uint8Array;
  readonly id: string;
  readonly inbox: Msg[] = [];
  readonly binInbox: BinaryFrame[] = [];
  readonly all: Msg[] = [];          // every text frame received, in order
  readonly closed: Promise<{ code: number; reason: string }>;
  pings = 0;
  private readonly waiters: Waiter<Msg>[] = [];
  private readonly binWaiters: Waiter<BinaryFrame>[] = [];
  private pingWaiters: Array<() => void> = [];

  constructor(ws: WebSocket, key: string) {
    this.ws = ws;
    this.key = key;
    this.pub = publicKeyFromPrivate(key);
    this.id = gadgetIdFromPubkey(this.pub);
    ws.on("message", (data: Buffer, isBinary: boolean) => {
      if (isBinary) {
        const f = decodeBinary(new Uint8Array(data));
        if (!f) return;
        const w = this.binWaiters.findIndex((x) => x.match(f));
        if (w >= 0) this.binWaiters.splice(w, 1)[0].resolve(f);
        else this.binInbox.push(f);
        return;
      }
      const msg = JSON.parse(data.toString("utf8")) as Msg;
      this.all.push(msg);
      const w = this.waiters.findIndex((x) => x.match(msg));
      if (w >= 0) this.waiters.splice(w, 1)[0].resolve(msg);
      else this.inbox.push(msg);
    });
    ws.on("ping", () => {
      this.pings++;
      const waiting = this.pingWaiters;
      this.pingWaiters = [];
      for (const w of waiting) w();
    });
    this.closed = new Promise((resolve) => ws.on("close", (code: number, reason: Buffer) => resolve({ code, reason: reason.toString() })));
  }

  send(msg: Msg): void {
    this.ws.send(JSON.stringify(msg));
  }
  sendRaw(data: string | Uint8Array): void {
    this.ws.send(data);
  }
  sendBinary(kind: Kind, stream: number, payload: Uint8Array): void {
    this.ws.send(encodeBinary(kind, stream, payload));
  }

  /** The first unread text frame with this op (and matching `where`), waiting up to timeoutMs. */
  next(op: string, where: (m: Msg) => boolean = () => true, timeoutMs = 3000): Promise<Msg> {
    return this.nextOf([op], where, timeoutMs);
  }

  /** The first unread text frame whose op is one of `ops`. */
  nextOf(ops: string[], where: (m: Msg) => boolean = () => true, timeoutMs = 3000): Promise<Msg> {
    const match = (m: Msg): boolean => ops.includes(m.op) && where(m);
    const i = this.inbox.findIndex(match);
    if (i >= 0) return Promise.resolve(this.inbox.splice(i, 1)[0]);
    return new Promise((resolve, reject) => {
      const waiter: Waiter<Msg> = { match, resolve: (m) => { clearTimeout(t); resolve(m); } };
      const t = setTimeout(() => {
        this.waiters.splice(this.waiters.indexOf(waiter), 1);
        reject(new Error(`timed out waiting for ${ops.join("|")}; inbox: ${JSON.stringify(this.inbox.map((m) => m.op))}`));
      }, timeoutMs);
      this.waiters.push(waiter);
    });
  }

  nextBinary(kind?: Kind, timeoutMs = 3000): Promise<BinaryFrame> {
    const match = (f: BinaryFrame): boolean => kind === undefined || f.kind === kind;
    const i = this.binInbox.findIndex(match);
    if (i >= 0) return Promise.resolve(this.binInbox.splice(i, 1)[0]);
    return new Promise((resolve, reject) => {
      const waiter: Waiter<BinaryFrame> = { match, resolve: (f) => { clearTimeout(t); resolve(f); } };
      const t = setTimeout(() => {
        this.binWaiters.splice(this.binWaiters.indexOf(waiter), 1);
        reject(new Error(`timed out waiting for a binary frame of kind ${kind}`));
      }, timeoutMs);
      this.binWaiters.push(waiter);
    });
  }

  /** Resolves on the next WebSocket ping from the host. */
  nextPing(timeoutMs = 3000): Promise<void> {
    return new Promise((resolve, reject) => {
      const t = setTimeout(() => reject(new Error("timed out waiting for a ping")), timeoutMs);
      this.pingWaiters.push(() => {
        clearTimeout(t);
        resolve();
      });
    });
  }

  /** The ops received so far, in order. */
  ops(): string[] {
    return this.all.map((m) => m.op);
  }

  close(code = 1000): Promise<{ code: number; reason: string }> {
    this.ws.close(code);
    return this.closed;
  }
}

/** Opens the socket (no handshake). */
export function openSocket(port: number, key = randomKey()): Promise<TestGadget> {
  return new Promise((resolve, reject) => {
    const ws = new WebSocket(`ws://127.0.0.1:${port}/gadget`, "openmausbot-gadget.1");
    const g = new TestGadget(ws, key);
    ws.once("open", () => resolve(g));
    ws.once("error", reject);
  });
}

export function helloFor(g: TestGadget, o: GadgetOptions): Msg {
  return {
    op: "hello", proto: o.proto ?? 1, id: g.id, pubkey: b64Encode(g.pub), name: o.name ?? "Test Maus",
    board: o.board ?? "amoled-175c", fw: o.fw ?? "1.0.0", caps: o.caps ?? CAPS, actions: o.actions ?? [],
    sensors: { battery_pct: 82, charging: false },
  };
}

/** Opens a socket and runs hello / challenge / prove. Resolves with the gadget and the host's
 *  answer to prove: the `ready` frame, or the `error` frame. */
export async function connectGadget(o: GadgetOptions): Promise<{ gadget: TestGadget; challenge: Msg; result: Msg }> {
  const gadget = await openSocket(o.port, o.key ?? randomKey());
  gadget.send(helloFor(gadget, o));
  const first = await gadget.nextOf(["challenge", "error"]);
  if (first.op === "error") return { gadget, challenge: {}, result: first };
  const sig = b64Encode(signP256(gadget.key, proveText(gadget.id, first.nonce, first.host_id)));
  gadget.send(o.enroll === undefined ? { op: "prove", sig } : { op: "prove", sig, enroll: o.enroll });
  const result = await gadget.nextOf(["ready", "error"]);
  return { gadget, challenge: first, result };
}

/** Waits for the first host event matching `match`, recorded from now on. */
export function nextEvent(host: FakeHost, match: (e: HostEvent) => boolean, timeoutMs = 3000): Promise<HostEvent> {
  return new Promise((resolve, reject) => {
    const off = host.on((e) => {
      if (!match(e)) return;
      off();
      clearTimeout(t);
      resolve(e);
    });
    const t = setTimeout(() => {
      off();
      reject(new Error("timed out waiting for a host event"));
    }, timeoutMs);
  });
}

/** Tries an upgrade and reports the HTTP status, or "open" with the negotiated extensions. */
export function tryUpgrade(port: number, opts: { path?: string; protocol?: string | null; headers?: Record<string, string>; deflate?: boolean }): Promise<string> {
  return new Promise((resolve) => {
    const protocols = opts.protocol === null ? undefined : (opts.protocol ?? "openmausbot-gadget.1");
    const ws = new WebSocket(`ws://127.0.0.1:${port}${opts.path ?? "/gadget"}`, protocols, {
      headers: opts.headers, perMessageDeflate: opts.deflate ?? false,
    });
    ws.on("unexpected-response", (_req: unknown, res: { statusCode: number }) => resolve(`status ${res.statusCode}`));
    ws.on("error", (e: Error) => resolve(`error ${e.message}`));
    ws.on("open", () => {
      resolve(`open ext=${JSON.stringify(ws.extensions)}`);
      ws.close();
    });
  });
}

/** For the few negative checks that must let time pass ("nothing more arrives"). */
export const delay = (ms: number): Promise<void> => new Promise((r) => setTimeout(r, ms));
