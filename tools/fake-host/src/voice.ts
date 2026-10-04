// SPDX-License-Identifier: Apache-2.0
// Conversation turns (PROTOCOL.md §4.4) with a scripted speech-to-text result and reply:
// heard → working → three cumulative replies 250 ms apart → final reply → test-tone speech → done
// (with doneBeforeSpeech: final reply → done → test-tone speech).
import { Buffer } from "node:buffer";
import type { ReplyMsg, TurnOutcome } from "../../../protocol/lib/types.ts";
import { BinaryKind, MIC_RATE, TEXT_FRAME_MAX, UTTERANCE_MAX_MS } from "../../../protocol/lib/types.ts";
import type { Feature, HostContext } from "./context.ts";
import { foldLatin1 } from "./fold.ts";
import type { GadgetSession } from "./session.ts";
import { speakerRate, speechOf } from "./speech.ts";

export const WORKING_TEXT = "checking your calendar";
export const NOTHING_HEARD = "Didn't catch that";
export const BAD_MIC_RATE = "Unsupported mic rate";
export const NO_BOT = "Pick a bot for this gadget in MausBot → Settings → Remote access";
const MIC_BYTES_MAX = (MIC_RATE * 2 * UTTERANCE_MAX_MS) / 1000;

/** The reply in three cumulative parts, cut at word boundaries (the last part is the whole text). */
export function cumulativeParts(text: string): [string, string, string] {
  const words = text.split(" ");
  const at = (k: number): string => words.slice(0, Math.ceil((words.length * k) / 3)).join(" ");
  return [at(1), at(2), text];
}

/** A reply frame within the text frame limit: a longer text is cut from the start behind "…". */
export function fitReply(turn: string, text: string, final: boolean): ReplyMsg {
  const fits = (t: string): boolean => Buffer.byteLength(JSON.stringify({ op: "reply", turn, text: t, final }), "utf8") <= TEXT_FRAME_MAX;
  if (fits(text)) return { op: "reply", turn, text, final };
  const chars = [...text];
  let lo = 1;
  let hi = chars.length;
  while (lo < hi) {
    const mid = (lo + hi) >> 1;
    if (fits("…" + chars.slice(mid).join(""))) hi = mid;
    else lo = mid + 1;
  }
  return { op: "reply", turn, text: "…" + chars.slice(lo).join(""), final };
}

interface Turn { id: string; recording: boolean; stream: number | null; audioBytes: number; timers: Set<NodeJS.Timeout> }

class VoiceSession {
  private readonly session: GadgetSession;
  private readonly host: HostContext;
  private turn: Turn | null = null;
  /** A turn that already got `done ok` while its speech may still play (doneBeforeSpeech). */
  private speakingTurn: string | null = null;

  constructor(session: GadgetSession, host: HostContext) {
    this.session = session;
    this.host = host;
    session.onOp("voice.begin", (m) => this.onVoiceBegin(m));
    session.onOp("voice.end", (m) => this.onVoiceEnd(m));
    session.onOp("voice.drop", (m) => {
      if (this.turn && m.turn === this.turn.id) this.end(this.turn, "stopped");
    });
    session.onOp("say", (m) => this.onSay(m));
    session.onOp("stop", (m) => {
      if (this.turn && (m.turn === undefined || m.turn === this.turn.id)) this.end(this.turn, "stopped");
    });
    session.onBinary((kind, stream, payload) => {
      const t = this.turn;
      if (kind !== BinaryKind.mic || !t || !t.recording || stream !== t.stream) return;
      t.audioBytes = Math.min(MIC_BYTES_MAX, t.audioBytes + payload.length);
    });
    session.onClose(() => {
      if (this.turn) for (const timer of this.turn.timers) clearTimeout(timer);
      this.turn = null;
      this.speakingTurn = null;
    });
  }

  private event(turn: string, phase: string, outcome?: TurnOutcome): void {
    this.host.emit({ event: "turn", gadget: this.session.record!.id, turn, phase, ...(outcome ? { outcome } : {}) });
  }

  private start(id: string, stream: number | null): Turn {
    if (this.turn) this.end(this.turn, "stopped");
    else if (this.speakingTurn !== null) speechOf(this.session).stopTurn(this.speakingTurn);
    this.speakingTurn = null;
    const t: Turn = { id, recording: stream !== null, stream, audioBytes: 0, timers: new Set() };
    this.turn = t;
    this.event(id, "started");
    return t;
  }

  /** Ends the turn: speak.stop for its speech when stopping, then done. Nothing follows for it. */
  private end(t: Turn, outcome: TurnOutcome, reason?: string): void {
    if (this.turn !== t) return;
    this.turn = null;
    for (const timer of t.timers) clearTimeout(timer);
    if (outcome !== "ok") speechOf(this.session).stopTurn(t.id);
    this.session.send(reason === undefined ? { op: "done", turn: t.id, outcome } : { op: "done", turn: t.id, outcome, reason: foldLatin1(reason) });
    this.event(t.id, "done", outcome);
  }

  private later(t: Turn, ms: number, fn: () => void): void {
    const timer = setTimeout(() => {
      t.timers.delete(timer);
      if (this.turn === t) fn();
    }, ms);
    t.timers.add(timer);
  }

  private onVoiceBegin(m: Record<string, unknown>): void {
    if (typeof m.turn !== "string" || m.turn === "" || m.turn.length > 32) return;
    if (typeof m.stream !== "number" || !Number.isInteger(m.stream) || m.stream < 1 || m.stream > 255) return;
    const t = this.start(m.turn, m.stream);
    if (m.rate !== MIC_RATE) this.end(t, "failed", BAD_MIC_RATE);
  }

  private onVoiceEnd(m: Record<string, unknown>): void {
    const t = this.turn;
    if (!t || !t.recording || m.turn !== t.id) return;
    t.recording = false;
    if (this.session.record!.bot.id === "") return this.end(t, "failed", NO_BOT);
    const heard = foldLatin1(this.host.script.heard);
    if (heard.trim() === "") return this.end(t, "failed", NOTHING_HEARD);
    this.session.send({ op: "heard", turn: t.id, text: heard });
    this.event(t.id, "heard");
    this.respond(t);
  }

  private onSay(m: Record<string, unknown>): void {
    if (typeof m.turn !== "string" || m.turn === "" || m.turn.length > 32 || typeof m.text !== "string") return;
    const t = this.start(m.turn, null);
    if (this.session.record!.bot.id === "") return this.end(t, "failed", NO_BOT);
    this.respond(t);
  }

  private respond(t: Turn): void {
    const every = this.host.options.replyIntervalMs;
    const parts = cumulativeParts(foldLatin1(this.host.script.reply));
    this.session.send({ op: "working", turn: t.id, text: WORKING_TEXT });
    parts.forEach((text, i) => this.later(t, every * (i + 1), () => this.session.send(fitReply(t.id, text, false))));
    this.later(t, every * 4, () => {
      this.session.send(fitReply(t.id, parts[2], true));
      this.event(t.id, "reply");
      const toneMs = this.host.options.toneMs;
      const speech = speechOf(this.session);
      if (toneMs <= 0 || speakerRate(this.session) === null) return this.end(t, "ok");
      if (this.host.options.doneBeforeSpeech) {
        // MausBot's usual order: done when the bot's turn completes, the speech after it (§4.4).
        this.end(t, "ok");
        this.speakingTurn = t.id;
        this.event(t.id, "speech");
        void speech.play(t.id, toneMs).then(() => {
          if (this.speakingTurn === t.id) this.speakingTurn = null;
        });
        return;
      }
      this.event(t.id, "speech");
      void speech.play(t.id, toneMs).then((played) => {
        if (played) this.end(t, "ok");
      });
    });
  }
}

function setText(field: "heard" | "reply"): Feature["commands"][string] {
  return (call) => {
    if (typeof call.cmd.text !== "string") throw new Error("text must be a string");
    call.host.script[field] = call.cmd.text;
  };
}

export const voiceFeature: Feature = {
  attach(session, host) {
    new VoiceSession(session, host);
  },
  commands: { heard: setText("heard"), reply: setText("reply") },
};
