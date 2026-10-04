// SPDX-License-Identifier: Apache-2.0
// Speech out (PROTOCOL.md §4.4): a 440 Hz test tone instead of synthesized speech, sent as
// 40 ms PCM16 frames paced to real time and never more than 0.5 s ahead.
import { BinaryKind, SPEAK_AHEAD_MS, SPEAK_FRAME_MS } from "../../../protocol/lib/types.ts";
import type { GadgetSession } from "./session.ts";

const TONE_HZ = 440;
const TONE_AMPLITUDE = 8000;

/** PCM16 little-endian samples [from, from + count) of the test tone at `rate`. */
export function toneSamples(rate: number, from: number, count: number): Uint8Array {
  const out = new Uint8Array(count * 2);
  const view = new DataView(out.buffer);
  for (let i = 0; i < count; i++) {
    view.setInt16(i * 2, Math.round(TONE_AMPLITUDE * Math.sin((2 * Math.PI * TONE_HZ * (from + i)) / rate)), true);
  }
  return out;
}

/** caps.speaker.rate when it is 16000 or 24000, else null (no speaker). */
export function speakerRate(session: GadgetSession): 16000 | 24000 | null {
  const rate = session.hello?.caps.speaker?.rate;
  return rate === 16000 || rate === 24000 ? rate : null;
}

interface Playing { stream: number; turn?: string; cancelled: boolean }
interface Job { turn?: string; ms: number; done: (played: boolean) => void }
const sleep = (ms: number): Promise<void> => new Promise((r) => setTimeout(r, ms));

export class SpeechPlayer {
  private readonly session: GadgetSession;
  private playing: Playing | null = null;
  private readonly queue: Job[] = [];

  constructor(session: GadgetSession) {
    this.session = session;
    session.onClose(() => this.cancelAll());
  }

  /** Reply speech (with a turn) replaces whatever plays and goes ahead of queued post speech; post
   *  speech (no turn) waits until the earlier stream has played out (PROTOCOL.md §4.4). Resolves
   *  true once the stream has played out in real time, false when it is stopped or replaced
   *  first, or there is no speaker. */
  play(turn: string | undefined, ms: number): Promise<boolean> {
    if (speakerRate(this.session) === null || ms <= 0) return Promise.resolve(false);
    return new Promise((done) => {
      const job: Job = { turn, ms, done };
      if (turn !== undefined) {
        // Between two streams nothing plays while a post may already be queued (pump()).
        if (this.playing) this.playing.cancelled = true;
        this.queue.unshift(job);
      } else {
        this.queue.push(job);
      }
      if (!this.playing || this.playing.cancelled) void this.pump();
    });
  }

  /** speak.stop for the turn's stream if it plays, and drops its queued speech. */
  stopTurn(turn: string): void {
    for (let i = this.queue.length - 1; i >= 0; i--) {
      if (this.queue[i].turn === turn) this.queue.splice(i, 1)[0].done(false);
    }
    const p = this.playing;
    if (p && !p.cancelled && p.turn === turn) {
      p.cancelled = true;
      this.session.send({ op: "speak.stop", stream: p.stream });
    }
  }

  private cancelAll(): void {
    if (this.playing) this.playing.cancelled = true;
    for (const job of this.queue.splice(0)) job.done(false);
  }

  private pumping = false;
  private async pump(): Promise<void> {
    if (this.pumping) return;
    this.pumping = true;
    try {
      while (this.queue.length > 0 && !this.session.closed) {
        const job = this.queue.shift()!;
        job.done(await this.stream(job));
        // job.done only queues its continuation (voice.ts sends the turn's `done ok` there): let it
        // run before the next stream's speak.begin, so a post never begins inside a turn in flight.
        await new Promise<void>((r) => setImmediate(r));
      }
    } finally {
      this.pumping = false;
    }
  }

  private async stream(job: Job): Promise<boolean> {
    const rate = speakerRate(this.session);
    if (rate === null) return false;
    const stream = this.session.allocStream();
    const p: Playing = { stream, turn: job.turn, cancelled: false };
    this.playing = p;
    try {
      this.session.send(job.turn === undefined ? { op: "speak.begin", stream, rate } : { op: "speak.begin", stream, rate, turn: job.turn });
      const total = Math.round((rate * job.ms) / 1000);
      const perFrame = (rate * SPEAK_FRAME_MS) / 1000;
      const start = Date.now();
      const playoutEnd = start + (total * 1000) / rate;
      let sent = 0;
      while (sent < total && !p.cancelled) {
        const n = Math.min(perFrame, total - sent);
        const aheadMs = ((sent + n) * 1000) / rate - (Date.now() - start);
        if (aheadMs > SPEAK_AHEAD_MS) {
          await sleep(Math.max(1, Math.ceil(aheadMs - SPEAK_AHEAD_MS)));
          continue;
        }
        if (!(await this.session.sendBinary(BinaryKind.speaker, stream, toneSamples(rate, sent, n)))) return false;
        sent += n;
      }
      if (p.cancelled) return false;
      this.session.send({ op: "speak.end", stream });
      // The gadget still has up to 0.5 s buffered: keep the slot until it has played out, so a
      // post's speak.begin never cuts it. A stop or a reply's play() still cancels it at once.
      while (!p.cancelled && Date.now() < playoutEnd) await sleep(Math.min(20, playoutEnd - Date.now()));
      return !p.cancelled;
    } finally {
      this.session.releaseStream(stream);
      if (this.playing === p) this.playing = null;
    }
  }
}

const players = new WeakMap<GadgetSession, SpeechPlayer>();
/** The session's one speech player. */
export function speechOf(session: GadgetSession): SpeechPlayer {
  let p = players.get(session);
  if (!p) {
    p = new SpeechPlayer(session);
    players.set(session, p);
  }
  return p;
}
