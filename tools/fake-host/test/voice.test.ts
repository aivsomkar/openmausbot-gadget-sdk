// SPDX-License-Identifier: Apache-2.0
import { test, type TestContext } from "node:test";
import assert from "node:assert/strict";
import { Buffer } from "node:buffer";
import { startFakeHost, type FakeHost } from "../src/server.ts";
import type { FakeHostOptions } from "../src/options.ts";
import type { HostEvent } from "../src/context.ts";
import { cumulativeParts, fitReply } from "../src/voice.ts";
import { toneSamples } from "../src/speech.ts";
import { CAPS, connectGadget, delay, type GadgetOptions, type TestGadget } from "./gadget-client.ts";

async function setup(t: TestContext, o: Partial<FakeHostOptions> = {}, g: Partial<GadgetOptions> = {}): Promise<{ h: FakeHost; gadget: TestGadget; events: HostEvent[] }> {
  const events: HostEvent[] = [];
  const h = await startFakeHost({ port: 0, quiet: true, code: "123456", replyIntervalMs: 20, toneMs: 200, ...o }, { listener: (e) => events.push(e) });
  t.after(() => h.close());
  const { gadget, result } = await connectGadget({ port: h.port, enroll: "123456", ...g });
  assert.equal(result.op, "ready");
  return { h, gadget, events };
}
const mic = (ms: number): Uint8Array => new Uint8Array((16000 * 2 * ms) / 1000);
function talk(g: TestGadget, turn: string, stream = 1): void {
  g.send({ op: "voice.begin", turn, stream, rate: 16000 });
  for (let i = 0; i < 5; i++) g.sendBinary(1, stream, mic(20));
  g.send({ op: "voice.end", turn, ms: 100 });
}

test("a voice turn: heard, working, three cumulative replies, final, paced speech, done ok", async (t) => {
  const { gadget, events } = await setup(t, {}, {});
  talk(gadget, "t0a0b0c0d-1");
  const done = await gadget.next("done");
  assert.deepEqual(done, { op: "done", turn: "t0a0b0c0d-1", outcome: "ok" });
  assert.deepEqual(gadget.ops(), [
    "challenge", "ready", "heard", "working", "reply", "reply", "reply", "reply", "speak.begin", "speak.end", "done",
  ]);
  const by = (op: string) => gadget.all.filter((m) => m.op === op);
  assert.deepEqual(by("heard")[0], { op: "heard", turn: "t0a0b0c0d-1", text: "What's on my calendar today?" });
  assert.deepEqual(by("working")[0], { op: "working", turn: "t0a0b0c0d-1", text: "checking your calendar" });
  const replies = by("reply");
  const full = "You have two meetings today: design review at 10 and lunch with Sam at 1.";
  assert.deepEqual(replies.map((r) => r.final), [false, false, false, true]);
  assert.equal(replies[3].text, full);
  assert.ok(full.startsWith(replies[0].text) && full.startsWith(replies[1].text) && replies[0].text.length < replies[1].text.length);
  const begin = by("speak.begin")[0];
  assert.deepEqual([begin.rate, begin.turn], [16000, "t0a0b0c0d-1"]);
  const frames = gadget.binInbox.filter((f) => f.kind === 2);
  assert.ok(frames.every((f) => f.stream === begin.stream));
  assert.equal(frames.reduce((n, f) => n + f.payload.length, 0), 200 * 32, "200 ms at 16 kHz, 2 bytes per sample");
  assert.ok(frames.slice(0, -1).every((f) => f.payload.length === 1280), "40 ms frames");
  assert.deepEqual(frames[0].payload, toneSamples(16000, 0, 640));
  assert.equal(by("speak.end")[0].stream, begin.stream);
  assert.deepEqual(events.filter((e) => e.event === "turn").map((e) => e.phase), ["started", "heard", "reply", "speech", "done"]);
  assert.ok(events.some((e) => e.event === "rx_binary" && e.kind === 1 && e.bytes === 640));
});

test("speech is paced to real time, never more than 0.5 s ahead", async (t) => {
  const { gadget } = await setup(t, { toneMs: 1200 });
  gadget.send({ op: "say", turn: "t00000001-1", text: "hi" });
  await gadget.next("speak.begin", () => true, 5000);
  const t0 = Date.now();
  let audioMs = 0;
  let worst = 0;
  for (;;) {
    const f = await gadget.nextBinary(2, 5000);
    audioMs += f.payload.length / 32;
    worst = Math.max(worst, audioMs - (Date.now() - t0));
    if (audioMs >= 1200) break;
  }
  assert.ok(worst <= 540, `audio ran ${worst} ms ahead`);
  assert.ok(Date.now() - t0 >= 600, "1.2 s of audio took at least 0.7 s minus jitter to arrive");
  await gadget.next("done");
});

test("say skips heard; an empty STT result fails with Didn't catch that; a 24 kHz speaker gets 960-sample frames", async (t) => {
  const { h, gadget } = await setup(t, {}, { caps: { ...CAPS, speaker: { rate: 24000 } } });
  gadget.send({ op: "say", turn: "t00000001-1", text: "hello" });
  await gadget.next("done");
  assert.ok(!gadget.ops().includes("heard"));
  assert.equal(gadget.all.find((m) => m.op === "speak.begin")!.rate, 24000);
  assert.equal(gadget.binInbox.find((f) => f.kind === 2)!.payload.length, 1920);
  await h.command({ cmd: "heard", text: "" });
  talk(gadget, "t00000001-2");
  assert.deepEqual(await gadget.next("done"), { op: "done", turn: "t00000001-2", outcome: "failed", reason: "Didn't catch that" });
});

test("voice.begin at 8 kHz fails with Unsupported mic rate", async (t) => {
  const { gadget } = await setup(t);
  gadget.send({ op: "voice.begin", turn: "t00000002-1", stream: 1, rate: 8000 });
  assert.deepEqual(await gadget.next("done"), { op: "done", turn: "t00000002-1", outcome: "failed", reason: "Unsupported mic rate" });
});

test("stop during speech: speak.stop then done stopped, and nothing more for that turn", async (t) => {
  const { gadget } = await setup(t, { toneMs: 3000 });
  talk(gadget, "t00000003-1");
  const begin = await gadget.next("speak.begin", () => true, 5000);
  gadget.send({ op: "stop", turn: "t00000003-1" });
  const stop = await gadget.next("speak.stop");
  assert.equal(stop.stream, begin.stream);
  assert.deepEqual(await gadget.next("done"), { op: "done", turn: "t00000003-1", outcome: "stopped" });
  assert.ok(gadget.ops().indexOf("speak.stop") < gadget.ops().indexOf("done"), `speak.stop comes before done: ${gadget.ops().join(" ")}`);
  const seen = gadget.all.length;
  await delay(300);
  assert.deepEqual(gadget.all.slice(seen), [], "no frames after done");
  assert.ok(!gadget.ops().includes("speak.end"));
});

test("barge-in: a new voice.begin stops the old turn before the new turn's first message", async (t) => {
  const { gadget } = await setup(t, { replyIntervalMs: 200 });
  gadget.send({ op: "say", turn: "t00000004-1", text: "first" });
  await gadget.next("working");
  talk(gadget, "t00000004-2");
  const old = await gadget.next("done", (m) => m.turn === "t00000004-1");
  assert.equal(old.outcome, "stopped");
  await gadget.next("done", (m) => m.turn === "t00000004-2", 5000);
  const ops = gadget.all.map((m) => `${m.op}:${m.turn ?? ""}`);
  const stoppedAt = ops.indexOf("done:t00000004-1");
  const firstNew = ops.findIndex((o) => o.endsWith(":t00000004-2"));
  assert.ok(stoppedAt >= 0 && stoppedAt < firstNew, ops.join(" "));
  assert.ok(!ops.slice(stoppedAt + 1).some((o) => o.endsWith(":t00000004-1")), "nothing for the old turn after its done");
});

test("barge-in during speech: speak.stop for the old stream, then done stopped, before the new turn's first message", async (t) => {
  const { gadget } = await setup(t, { toneMs: 3000 });
  const seq: string[] = [];   // text frames and speaker frames, in arrival order
  gadget.ws.on("message", (data: Buffer, isBinary: boolean) => {
    if (isBinary) {
      if (data[0] === 2) seq.push(`speaker:${data[1]}`);
      return;
    }
    const m = JSON.parse(data.toString("utf8"));
    seq.push(m.op === "speak.stop" ? `speak.stop:${m.stream}` : `${m.op}:${m.turn ?? ""}`);
  });
  gadget.send({ op: "say", turn: "t00000009-1", text: "first" });
  const old = await gadget.next("speak.begin", () => true, 5000);
  gadget.send({ op: "say", turn: "t00000009-2", text: "second" });
  // The new turn's speak.begin is its last frame before 3 s of tone; the old stream had its chance.
  await gadget.next("speak.begin", (m) => m.turn === "t00000009-2", 5000);
  const stopAt = seq.indexOf(`speak.stop:${old.stream}`);
  const doneAt = seq.indexOf("done:t00000009-1");
  const firstNew = seq.findIndex((s) => s.endsWith(":t00000009-2"));
  assert.ok(stopAt >= 0 && stopAt < doneAt && doneAt < firstNew, seq.join(" "));
  assert.ok(!seq.slice(doneAt + 1).includes(`speaker:${old.stream}`), "no speaker frame for the old stream after its done");
});

test("done before speech: done ok precedes speak.begin, speech continues, and a new say sends speak.stop before the new turn's first message", async (t) => {
  const { gadget, events } = await setup(t, { doneBeforeSpeech: true, toneMs: 3000 });
  gadget.send({ op: "say", turn: "t0000000a-1", text: "first" });
  assert.deepEqual(await gadget.next("done", () => true, 5000), { op: "done", turn: "t0000000a-1", outcome: "ok" });
  const begin = await gadget.next("speak.begin", () => true, 5000);
  assert.equal(begin.turn, "t0000000a-1");
  assert.equal((await gadget.nextBinary(2)).stream, begin.stream, "speech continues after done");
  gadget.send({ op: "say", turn: "t0000000a-2", text: "second" });
  assert.equal((await gadget.next("speak.stop")).stream, begin.stream);
  await gadget.next("done", (m) => m.turn === "t0000000a-2", 5000);
  const seq = gadget.all.map((m) => (m.op === "speak.stop" ? `speak.stop:${m.stream}` : `${m.op}:${m.turn ?? ""}`));
  const doneAt = seq.indexOf("done:t0000000a-1");
  const beginAt = seq.indexOf("speak.begin:t0000000a-1");
  const stopAt = seq.indexOf(`speak.stop:${begin.stream}`);
  const firstNew = seq.findIndex((s) => s.endsWith(":t0000000a-2"));
  assert.ok(doneAt >= 0 && doneAt < beginAt && beginAt < stopAt && stopAt < firstNew, seq.join(" "));
  assert.ok(!gadget.ops().includes("speak.end"), "the first stream was stopped, not ended");
  const phases = events.filter((e) => e.event === "turn" && e.turn === "t0000000a-1").map((e) => e.phase);
  assert.deepEqual(phases, ["started", "reply", "done", "speech"]);
});

test("voice.drop ends the turn as stopped; stop for a turn not in flight is ignored", async (t) => {
  const { gadget } = await setup(t);
  gadget.send({ op: "voice.begin", turn: "t00000005-1", stream: 3, rate: 16000 });
  gadget.sendBinary(1, 3, mic(20));
  gadget.send({ op: "voice.drop", turn: "t00000005-1" });
  assert.deepEqual(await gadget.next("done"), { op: "done", turn: "t00000005-1", outcome: "stopped" });
  gadget.send({ op: "stop", turn: "t00000005-1" });
  gadget.send({ op: "say", turn: "t00000005-2", text: "x" });
  const next = await gadget.next("done", () => true, 5000);
  assert.equal(next.turn, "t00000005-2", "the stray stop produced no done");
});

test("no speaker: text only; replies are folded to Latin-1 and cut to the frame limit", async (t) => {
  const { h, gadget } = await setup(t, {}, { caps: { ...CAPS, speaker: undefined } });
  await h.command({ cmd: "reply", text: "Café “quoted” — done 🎉" });
  gadget.send({ op: "say", turn: "t00000006-1", text: "x" });
  await gadget.next("done");
  assert.ok(!gadget.ops().includes("speak.begin"));
  assert.equal(gadget.all.filter((m) => m.op === "reply").at(-1)!.text, 'Café "quoted" - done ');
  const long = "x".repeat(20000) + " END";
  const r = fitReply("t1", long, true);
  assert.ok(Buffer.byteLength(JSON.stringify(r)) <= 16384);
  assert.ok(r.text.startsWith("…") && r.text.endsWith(" END"));
  assert.deepEqual(cumulativeParts("one two three four five six"), ["one two", "one two three four", "one two three four five six"]);
});

test("an unbound gadget's turn fails with the Remote access pointer", async (t) => {
  const { gadget } = await setup(t, { bot: { id: "", name: "" } });
  gadget.send({ op: "say", turn: "t00000007-1", text: "x" });
  assert.deepEqual(await gadget.next("done"), {
    op: "done", turn: "t00000007-1", outcome: "failed", reason: "Pick a bot for this gadget in MausBot → Settings → Remote access",
  });
});

test("a disconnect mid-turn cancels the turn's timers", async (t) => {
  const { gadget, events } = await setup(t, { replyIntervalMs: 50 });
  gadget.send({ op: "say", turn: "t00000008-1", text: "x" });
  await gadget.next("working");
  await gadget.close();
  await delay(300);
  assert.ok(!events.some((e) => e.event === "turn" && e.phase === "reply"), "no reply after the socket closed");
});
