// SPDX-License-Identifier: Apache-2.0
// Firmware updates (PROTOCOL.md §4.8): offer an image signed with the test key, stream it in
// 4 KiB chunks with at most 64 KiB unacknowledged, commit, and report fw.installed.
import { createHash } from "node:crypto";
import { readFileSync } from "node:fs";
import { b64Encode } from "../../../protocol/lib/encoding.ts";
import { firmwareText } from "../../../protocol/lib/identity.ts";
import { signP256 } from "../../../protocol/lib/verify.ts";
import { encodeFwChunk } from "../../../protocol/lib/frames.ts";
import { BOARD_ID_RE, FW_CHUNK_BYTES, FW_WINDOW_BYTES } from "../../../protocol/lib/types.ts";
import type { CommandCall, Feature, HostContext } from "./context.ts";
import type { GadgetSession } from "./session.ts";

export type Tamper = "sig" | "sha256" | "size";

export interface Offer { board: string; version: string; size: number; sha256: string; sig: string; key_id: string }

/** Builds the fw.offer fields. tamper "sig" flips the signature's last byte (gadget: bad_sig);
 *  "sha256" signs the hash of a different image (gadget: checksum at commit); "size" offers
 *  caps.ota.max + 1 bytes, correctly signed (gadget: too_large). */
export function buildOffer(input: {
  image: Uint8Array; board: string; version: string; keyHex: string; keyId: string; otaMax: number; tamper?: Tamper;
}): Offer {
  const { image, board, version, tamper } = input;
  let sha256 = createHash("sha256").update(image).digest("hex");
  let size = image.length;
  if (tamper === "sha256") {
    const other = Uint8Array.from(image);
    other[0] ^= 0xff;
    sha256 = createHash("sha256").update(other).digest("hex");
  }
  if (tamper === "size") size = input.otaMax + 1;
  const der = signP256(input.keyHex, firmwareText(board, version, size, sha256));
  if (tamper === "sig") der[der.length - 1] ^= 0x01;
  return { board, version, size, sha256, sig: b64Encode(der), key_id: input.keyId };
}

const running = new WeakSet<GadgetSession>();

async function runOta(host: HostContext, s: GadgetSession, image: Uint8Array, offer: Offer): Promise<void> {
  const gadget = s.record!.id;
  const emit = (phase: string, extra: Record<string, unknown> = {}): void => host.emit({ event: "ota", gadget, phase, ...extra });
  const stream = s.allocStream();
  running.add(s);
  // Every fw.* frame for this stream, and a "closed" marker, queue up here until read.
  const inbox: any[] = [];
  let waiter: ((m: any) => void) | null = null;
  const deliver = (m: any): void => {
    if (waiter) {
      const w = waiter;
      waiter = null;
      w(m);
    } else inbox.push(m);
  };
  const offs = ["fw.ready", "fw.fail", "fw.progress"].map((op) => s.onOp(op, (m) => {
    if (m.stream === stream) deliver(m);
  }));
  let closed = false;
  const offClose = s.onClose(() => {
    closed = true;
    deliver({ op: "closed" });
  });
  const next = (timeoutMs: number): Promise<any> => {
    if (inbox.length > 0) return Promise.resolve(inbox.shift());
    return new Promise((resolve) => {
      const t = setTimeout(() => {
        waiter = null;
        resolve({ op: "timeout" });
      }, timeoutMs);
      waiter = (m) => {
        clearTimeout(t);
        resolve(m);
      };
    });
  };
  try {
    s.send({ op: "fw.offer", stream, ...offer });
    emit("offered", { size: offer.size, version: offer.version });
    const answer = await next(host.options.fwReadyTimeoutMs);
    if (answer.op === "fw.fail") return emit("failed", { code: answer.code });
    if (answer.op === "timeout") return emit("failed", { code: "ready_timeout" });
    if (answer.op === "closed") return emit("failed", { code: "disconnected" });
    if (answer.op !== "fw.ready") return emit("failed", { code: "unexpected" });
    emit("ready");
    let sent = 0;
    let acked = 0;
    const total = image.length;
    while (acked < total) {
      while (sent < total && sent - acked < FW_WINDOW_BYTES) {
        const n = Math.min(FW_CHUNK_BYTES, total - sent, FW_WINDOW_BYTES - (sent - acked));
        const frame = encodeFwChunk(stream, sent, image.subarray(sent, sent + n));
        if (!(await s.sendBinary(4, stream, frame.subarray(2)))) return emit("failed", { code: "disconnected" });
        sent += n;
      }
      const m = await next(30_000);
      if (m.op === "fw.fail") return emit("failed", { code: m.code });
      if (m.op === "closed") return emit("failed", { code: "disconnected" });
      if (m.op === "timeout") return emit("failed", { code: "progress_timeout" });
      if (m.op === "fw.progress" && typeof m.offset === "number" && m.offset > acked && m.offset <= sent) {
        acked = m.offset;
        emit("progress", { offset: acked });
      }
    }
    s.send({ op: "fw.commit", stream });
    emit("committed");
    const after = await next(30_000);
    if (after.op === "fw.fail") emit("failed", { code: after.code });
  } finally {
    for (const off of offs) off();
    offClose();
    s.releaseStream(stream);
    running.delete(s);
    if (closed) host.log(`${gadget}: connection closed during or after the update`);
  }
}

function otaCommand(call: CommandCall): Record<string, unknown> {
  const s = call.session();
  const caps = s.hello!.caps;
  if (!caps.ota || typeof caps.ota.max !== "number") throw new Error("this gadget has no ota caps");
  if (running.has(s)) throw new Error("an update is already running on this gadget");
  const { image: path, version } = call.cmd;
  if (typeof path !== "string") throw new Error("image must be a file path");
  if (typeof version !== "string" || version === "") throw new Error("version must be a non-empty string");
  const board = call.cmd.board ?? s.hello!.board;
  if (typeof board !== "string" || !BOARD_ID_RE.test(board)) throw new Error("board must be a board id");
  const tamper = call.cmd.tamper as Tamper | undefined;
  if (tamper !== undefined && !["sig", "sha256", "size"].includes(tamper)) throw new Error("tamper must be sig, sha256 or size");
  const image = new Uint8Array(readFileSync(path));
  if (image.length === 0) throw new Error("the image is empty");
  const keyHex = readFileSync(call.host.options.otaKeyFile, "utf8").trim();
  const offer = buildOffer({ image, board, version, keyHex, keyId: call.host.options.otaKeyId, otaMax: caps.ota.max, tamper });
  void runOta(call.host, s, image, offer);
  return { size: offer.size, sha256: offer.sha256 };
}

export const otaFeature: Feature = {
  attach(session, host) {
    session.onOp("fw.installed", (m) => {
      host.emit({ event: "ota", gadget: session.record!.id, phase: "installed", version: m.version });
    });
  },
  commands: { ota: otaCommand },
};
