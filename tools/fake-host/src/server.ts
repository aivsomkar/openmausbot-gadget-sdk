// SPDX-License-Identifier: Apache-2.0
// The fake host: an HTTP server whose only WebSocket path is /gadget (PROTOCOL.md §4.1), the
// sessions on it, the event stream and the command interface.
import { Buffer } from "node:buffer";
import { createServer, type IncomingMessage } from "node:http";
import type { AddressInfo } from "node:net";
import type { Duplex } from "node:stream";
import { WebSocketServer } from "ws";
import { GADGET_PATH, GADGET_SUBPROTOCOL, PAIR_CODE_RE, TEXT_FRAME_MAX } from "../../../protocol/lib/types.ts";
import type { Ack, Command, CommandCall, CommandResult, Feature, HostContext, HostEvent, Script } from "./context.ts";
import { FEATURES } from "./features.ts";
import { DEFAULT_OPTIONS, type FakeHostOptions } from "./options.ts";
import { GadgetSession } from "./session.ts";
import { HostState } from "./state.ts";

export interface FakeHost {
  readonly port: number;
  readonly hostId: string;
  readonly state: HostState;
  readonly script: Script;
  on(listener: (event: HostEvent) => void): () => void;
  /** Runs one control command; the ack is also emitted as an `ack` event. */
  command(cmd: Command): Promise<Ack>;
  /** Close code 1001 to every gadget, then stop listening. */
  close(): Promise<void>;
}

export interface StartOptions {
  features?: Feature[];
  /** Attached before the first event, so it sees `listening` and `code`. */
  listener?: (event: HostEvent) => void;
}

/** Answers a refused upgrade and destroys the socket once the answer is written. Node's HTTP
 *  server removes its own socket error listener before 'upgrade', so without ours a client that
 *  resets crashes the host (EPIPE), and one that keeps its half open would hold up close(). */
function refuseUpgrade(socket: Duplex, status: number, text: string, extra = ""): void {
  socket.on("error", () => socket.destroy());
  socket.once("finish", () => socket.destroy());
  socket.end(`HTTP/1.1 ${status} ${text}\r\n${extra}Connection: close\r\nContent-Length: 0\r\n\r\n`);
}

export async function startFakeHost(partial: Partial<FakeHostOptions> = {}, start: StartOptions = {}): Promise<FakeHost> {
  const options: FakeHostOptions = { ...DEFAULT_OPTIONS, ...partial };
  const features = start.features ?? FEATURES;
  const state = HostState.load({ stateDir: options.stateDir, hostId: options.hostId, hostName: options.hostName });
  const script: Script = { heard: options.heard, reply: options.reply };
  const listeners = new Set<(event: HostEvent) => void>();
  if (start.listener) listeners.add(start.listener);
  const sessions = new Set<GadgetSession>();
  const live = new Map<string, GadgetSession>();
  let lastReady: string | null = null;

  const ctx: HostContext = {
    options, state, script,
    emit(event) {
      for (const l of [...listeners]) l(event);
    },
    log(line) {
      if (!options.quiet) process.stderr.write(`fake-host: ${line}\n`);
    },
    live: (id) => live.get(id) ?? null,
  };

  const wss = new WebSocketServer({
    noServer: true,
    perMessageDeflate: false,
    maxPayload: TEXT_FRAME_MAX,
    handleProtocols: (protocols: Set<string>) => (protocols.has(GADGET_SUBPROTOCOL) ? GADGET_SUBPROTOCOL : false),
  });
  const http = createServer((_req, res) => {
    res.writeHead(404, { "content-type": "text/plain" }).end("openmausbot fake host: gadgets connect to ws://<host>/gadget\n");
  });
  http.on("upgrade", (req: IncomingMessage, socket: Duplex, head: Buffer) => {
    const path = new URL(req.url ?? "/", "http://fake-host.invalid").pathname;
    if (path !== GADGET_PATH) return refuseUpgrade(socket, 404, "Not Found");
    if (req.headers.origin !== undefined) return refuseUpgrade(socket, 403, "Forbidden");
    if (req.method !== "GET") return refuseUpgrade(socket, 400, "Bad Request");
    if (req.headers["sec-websocket-version"] !== "13") return refuseUpgrade(socket, 426, "Upgrade Required", "Sec-WebSocket-Version: 13\r\n");
    const protocols = String(req.headers["sec-websocket-protocol"] ?? "").split(",").map((p) => p.trim());
    if (!protocols.includes(GADGET_SUBPROTOCOL)) return refuseUpgrade(socket, 400, "Bad Request");
    wss.handleUpgrade(req, socket, head, (ws) => {
      const remote = `${req.socket.remoteAddress}:${req.socket.remotePort}`;
      ctx.emit({ event: "connected", remote });
      const session = new GadgetSession(ws, remote, ctx, {
        takeOver(s) {
          const id = s.record!.id;
          const old = live.get(id);
          if (old && old !== s) old.fail("replaced", "another connection with the same id took over");
          live.set(id, s);
          lastReady = id;
        },
        attach(s) {
          for (const f of features) f.attach(s, ctx);
        },
        closed(s, code) {
          sessions.delete(s);
          const id = s.record?.id ?? null;
          if (id && live.get(id) === s) live.delete(id);
          ctx.emit({ event: "closed", gadget: id, code });
        },
      });
      sessions.add(session);
    });
  });

  await new Promise<void>((resolve, reject) => {
    http.once("error", reject);
    http.listen(options.port, options.bind, () => resolve());
  });
  const port = (http.address() as AddressInfo).port;

  function openWindow(code: string | null): { code: string; expires_at: number } {
    const w = state.openWindow(code, options.codeTtlS, Date.now());
    ctx.emit({ event: "code", code: w.code, expires_at: w.expiresAt });
    ctx.log(`pairing code ${w.code} (valid ${options.codeTtlS} s, 5 attempts, single use)`);
    return { code: w.code, expires_at: w.expiresAt };
  }

  async function close(): Promise<void> {
    for (const s of sessions) s.close(1001, "host shutting down");
    const deadline = Date.now() + 1000;
    while (sessions.size > 0 && Date.now() < deadline) await new Promise((r) => setTimeout(r, 10));
    for (const s of sessions) s.terminate();
    wss.close();
    await new Promise<void>((resolve) => http.close(() => resolve()));
  }

  const builtins: Record<string, (call: CommandCall) => CommandResult | Promise<CommandResult>> = {
    code(call) {
      const c = call.cmd.code;
      if (c !== undefined && (typeof c !== "string" || !PAIR_CODE_RE.test(c))) throw new Error("code must be six digits");
      return openWindow((c as string | undefined) ?? null);
    },
    revoke(call) {
      const id = call.gadgetId();
      if (!state.gadgets.delete(id)) throw new Error(`unknown gadget ${id}`);
      state.save();
      live.get(id)?.fail("revoked", "this gadget was removed");
    },
    replace(call) {
      call.session().fail("replaced", "another connection with the same id took over");
    },
    drop(call) {
      call.session().terminate();
    },
    close(call) {
      const code = call.cmd.code ?? 1000;
      if (typeof code !== "number" || !Number.isInteger(code)) throw new Error("code must be an integer");
      // The codes an application may send (RFC 6455 §7.4); `ws` throws on 1005, 1006 and the like.
      if (code !== 1000 && (code < 3000 || code > 4999)) throw new Error("code must be 1000 or 3000-4999");
      call.session().close(code);
    },
    async quit() {
      await close();
    },
  };

  async function command(cmd: Command): Promise<Ack> {
    const name = typeof cmd?.cmd === "string" ? cmd.cmd : null;
    const call: CommandCall = {
      cmd,
      host: ctx,
      gadgetId() {
        const id = typeof cmd.gadget === "string" ? cmd.gadget : lastReady;
        if (!id) throw new Error("no gadget has connected yet");
        return id;
      },
      session() {
        const id = this.gadgetId();
        const s = live.get(id);
        if (!s) throw new Error(`gadget ${id} is not connected`);
        return s;
      },
    };
    let ack: Ack;
    try {
      if (name === null) throw new Error("missing cmd");
      // Own properties only: "toString", "constructor" or "__proto__" are unknown commands.
      const handler = Object.hasOwn(builtins, name)
        ? builtins[name]
        : features.find((f) => Object.hasOwn(f.commands, name))?.commands[name];
      if (!handler) throw new Error(`unknown command ${name}`);
      const result = await handler(call);
      ack = { event: "ack", cmd: name, ok: true, ...(result ?? {}) };
    } catch (err) {
      ack = { event: "ack", cmd: name, ok: false, error: (err as Error).message };
    }
    ctx.emit(ack);
    return ack;
  }

  ctx.emit({ event: "listening", port, host_id: state.hostId });
  ctx.log(`listening on ws://${options.bind}:${port}${GADGET_PATH} (host_id ${state.hostId})`);
  openWindow(options.code);

  return {
    port,
    hostId: state.hostId,
    state,
    script,
    on(listener) {
      listeners.add(listener);
      return () => listeners.delete(listener);
    },
    command,
    close,
  };
}
