// SPDX-License-Identifier: Apache-2.0
import { test, type TestContext } from "node:test";
import assert from "node:assert/strict";
import { Buffer } from "node:buffer";
import { startFakeHost, type FakeHost } from "../src/server.ts";
import type { FakeHostOptions } from "../src/options.ts";
import type { HostEvent } from "../src/context.ts";
import { imagePixels } from "../src/display.ts";
import { CAPS, connectGadget, nextEvent, randomKey, type GadgetOptions, type TestGadget } from "./gadget-client.ts";

async function setup(t: TestContext, o: Partial<FakeHostOptions> = {}, g: Partial<GadgetOptions> = {}): Promise<{ h: FakeHost; gadget: TestGadget; events: HostEvent[]; key: string }> {
  const events: HostEvent[] = [];
  const h = await startFakeHost({ port: 0, quiet: true, code: "123456", toneMs: 120, ...o }, { listener: (e) => events.push(e) });
  t.after(() => h.close());
  const key = randomKey();
  const { gadget, result } = await connectGadget({ port: h.port, enroll: "123456", key, ...g });
  assert.equal(result.op, "ready");
  return { h, gadget, events, key };
}

test("a permission ask carries exactly Allow and Deny; answering closes it as answered", async (t) => {
  const { h, gadget, events } = await setup(t);
  const ack = await h.command({ cmd: "ask", kind: "permission", title: "Run “rm”?", body: "In ~/Downloads" });
  assert.equal(ack.ok, true);
  const ask = await gadget.next("ask");
  assert.deepEqual(ask, {
    op: "ask", id: ack.id, kind: "permission", title: 'Run "rm"?', body: "In ~/Downloads",
    options: [{ id: "allow", label: "Allow", style: "allow" }, { id: "deny", label: "Deny", style: "deny" }],
  });
  gadget.send({ op: "answer", id: ask.id, option: "deny" });
  assert.deepEqual(await gadget.next("ask.close"), { op: "ask.close", id: ask.id, reason: "answered" });
  assert.ok(events.some((e) => e.event === "answer" && e.id === ask.id && e.option === "deny"));
  const second = nextEvent(h, (e) => e.event === "rx" && String(e.msg).includes('"option":"allow"'));
  gadget.send({ op: "answer", id: ask.id, option: "allow" });
  await second;
  assert.equal(events.filter((e) => e.event === "answer").length, 1, "a second answer is ignored");
  assert.equal((await h.command({ cmd: "ask", kind: "permission", title: "x", options: [] })).error, "permission asks always carry Allow and Deny");
  const explicit = [{ label: "Allow", id: "allow", style: "allow" }, { id: "deny", style: "deny", label: "Deny" }];
  assert.equal((await h.command({ cmd: "ask", kind: "permission", title: "y", options: explicit })).ok, true, "explicit Allow and Deny are accepted");
  assert.deepEqual((await gadget.next("ask")).options, [{ id: "allow", label: "Allow", style: "allow" }, { id: "deny", label: "Deny", style: "deny" }]);
});

test("question asks: up to 4 neutral options, none for unsupported asks, more than 4 refused", async (t) => {
  const { h, gadget } = await setup(t);
  const opts = [{ id: "1", label: "Tea", style: "neutral" }, { id: "2", label: "Coffee" }];
  await h.command({ cmd: "ask", kind: "question", title: "Drink?", body: "", options: opts });
  const ask = await gadget.next("ask");
  assert.deepEqual(ask.options, [{ id: "1", label: "Tea", style: "neutral" }, { id: "2", label: "Coffee" }]);
  gadget.send({ op: "answer", id: ask.id, option: "9" });
  gadget.send({ op: "answer", id: ask.id, option: "2" });
  assert.equal((await gadget.next("ask.close")).reason, "answered");
  await h.command({ cmd: "ask", kind: "question", title: "Long form", body: "" });
  assert.deepEqual((await gadget.next("ask")).options, []);
  const five = Array.from({ length: 5 }, (_, i) => ({ id: String(i), label: String(i) }));
  assert.equal((await h.command({ cmd: "ask", kind: "question", title: "x", options: five })).error, "options must be an array of at most 4 entries");
});

test("asks go one at a time, oldest first; expiry and withdrawal close them", async (t) => {
  const { h, gadget } = await setup(t);
  const a = await h.command({ cmd: "ask", kind: "permission", title: "A", expires_s: 1 });
  const b = await h.command({ cmd: "ask", kind: "permission", title: "B" });
  const c = await h.command({ cmd: "ask", kind: "permission", title: "C" });
  assert.deepEqual([a.queued, b.queued, c.queued], [false, true, true]);
  assert.equal((await gadget.next("ask")).id, a.id);
  assert.deepEqual(await gadget.next("ask.close", () => true, 3000), { op: "ask.close", id: a.id, reason: "expired" });
  assert.equal((await gadget.next("ask")).id, b.id);
  assert.deepEqual(await h.command({ cmd: "ask.close", id: c.id }), { event: "ack", cmd: "ask.close", ok: true });
  assert.equal((await h.command({ cmd: "ask.close", id: b.id, reason: "withdrawn" })).ok, true);
  assert.deepEqual(await gadget.next("ask.close"), { op: "ask.close", id: b.id, reason: "withdrawn" });
  assert.equal(gadget.all.filter((m) => m.op === "ask").length, 2, "C was withdrawn before it was shown");
});

test("an open ask is sent again on reconnect", async (t) => {
  const { h, gadget, key } = await setup(t);
  const a = await h.command({ cmd: "ask", kind: "permission", title: "Still open" });
  await gadget.next("ask");
  await gadget.close();
  const { gadget: back } = await connectGadget({ port: h.port, key });
  assert.equal((await back.next("ask")).id, a.id);
});

test("post: routine and message kinds, bot from the record, speech only when asked", async (t) => {
  const { h, gadget } = await setup(t);
  const p = await h.command({ cmd: "post", kind: "routine", text: "Backups done ✅" });
  assert.deepEqual(await gadget.next("post"), {
    op: "post", id: p.id, bot: { id: "b_fake", name: "Fake Bot" }, kind: "routine", text: "Backups done ", speak: false,
  });
  await h.command({ cmd: "post", kind: "message", text: "Handoff finished", speak: true });
  assert.equal((await gadget.next("post")).speak, true);
  const begin = await gadget.next("speak.begin");
  assert.equal(begin.turn, undefined, "post speech has no turn");
  assert.equal((await gadget.next("speak.end")).stream, begin.stream);
  assert.equal((await h.command({ cmd: "post", kind: "digest", text: "x" })).error, "kind must be routine or message");
});

test("post speech starts only after the reply's speech has played out", async (t) => {
  const { h, gadget } = await setup(t, { toneMs: 300, replyIntervalMs: 10 });
  const begins: number[] = [];   // arrival times of speak.begin frames
  gadget.ws.on("message", (data: Buffer, isBinary: boolean) => {
    if (!isBinary && JSON.parse(data.toString("utf8")).op === "speak.begin") begins.push(Date.now());
  });
  gadget.send({ op: "say", turn: "t00000001-1", text: "x" });
  const replyBegin = await gadget.next("speak.begin", () => true, 5000);
  await h.command({ cmd: "post", kind: "message", text: "later", speak: true });
  const replyEnd = await gadget.next("speak.end", () => true, 5000);
  const postBegin = await gadget.next("speak.begin", () => true, 5000);
  assert.equal(replyEnd.stream, replyBegin.stream);
  assert.equal(postBegin.turn, undefined);
  const ops = gadget.ops();
  assert.ok(ops.indexOf("speak.end") < ops.lastIndexOf("speak.begin"));
  // Frames run up to 0.5 s ahead, so speak.end alone does not mean the reply has played out.
  assert.ok(begins[1] - begins[0] >= 300 - 60, `the post's speak.begin came ${begins[1] - begins[0]} ms after the reply's (300 ms of audio)`);
});

test("card and card.close; titles and bodies are cut to 80 and 600 characters", async (t) => {
  const { h, gadget } = await setup(t);
  const c = await h.command({ cmd: "card", title: "T".repeat(100), body: "B".repeat(700) });
  const card = await gadget.next("card");
  assert.deepEqual([card.id, card.title.length, card.body.length, card.ttl_s], [c.id, 80, 600, 30]);
  await h.command({ cmd: "card", id: "notice-voice", title: "Voice is off", body: "x", ttl_s: 8 });
  assert.equal((await gadget.next("card")).ttl_s, 8);
  await h.command({ cmd: "card.close", id: "notice-voice" });
  assert.deepEqual(await gadget.next("card.close"), { op: "card.close", id: "notice-voice" });
  assert.match(String((await h.command({ cmd: "card", id: "bad id!", title: "x" })).error), /id must be/);
});

test("image: begin, RGB565 rows of at most 8190 bytes, end; larger than caps.image is refused", async (t) => {
  const { h, gadget } = await setup(t);
  const ack = await h.command({ cmd: "image", w: 300, h: 300, pattern: "bars" });
  assert.equal(ack.ok, true);
  const begin = await gadget.next("image.begin");
  assert.deepEqual([begin.w, begin.h, begin.ttl_s], [300, 300, 30]);
  const end = await gadget.next("image.end");
  assert.equal(end.stream, begin.stream);
  const rows = gadget.binInbox.filter((f) => f.kind === 3);
  assert.ok(rows.every((f) => f.stream === begin.stream && f.payload.length <= 8190));
  const bytes = Buffer.concat(rows.map((f) => f.payload));
  assert.deepEqual(new Uint8Array(bytes), imagePixels(300, 300, "bars"));
  assert.equal(new DataView(bytes.buffer, bytes.byteOffset).getUint16(0, true), 0xffff, "first bar is white");
  assert.equal((await h.command({ cmd: "image", w: 301, h: 10 })).error, "image must fit 300x300");
  assert.deepEqual(imagePixels(1, 1, "#ff0000"), new Uint8Array([0x00, 0xf8]));
});

test("act: one act.result per act, timeout after the act timeout", async (t) => {
  const { h, gadget } = await setup(t, { actTimeoutMs: 200 }, { actions: [{ name: "chime", description: "Chime.", params: { type: "object" }, risk: "safe" }] });
  const a = await h.command({ cmd: "act", name: "chime" });
  const act = await gadget.next("act");
  assert.deepEqual(act, { op: "act", id: a.id, name: "chime", args: {} });
  const result = nextEvent(h, (e) => e.event === "act.result" && e.id === a.id);
  gadget.send({ op: "act.result", id: act.id, ok: true, data: { played: 1 } });
  const r = await result;
  assert.deepEqual(r, { event: "act.result", gadget: gadget.id, id: a.id, ok: true, data: { played: 1 } });
  const b = await h.command({ cmd: "act", name: "chime", args: { times: 2 } });
  assert.deepEqual((await gadget.next("act")).args, { times: 2 });
  const timeout = await nextEvent(h, (e) => e.event === "act.result" && e.id === b.id, 2000);
  assert.equal(timeout.timeout, true);
});

test("settings reach a live gadget; a rename while offline is sent right after the next ready", async (t) => {
  const { h, gadget, key } = await setup(t);
  await h.command({ cmd: "settings", bot: { id: "b_jev", name: "Jév" }, speak_pushes: true });
  assert.deepEqual(await gadget.next("settings"), { op: "settings", bot: { id: "b_jev", name: "Jév" }, settings: { speak_pushes: true } });
  const id = gadget.id;
  const offline = nextEvent(h, (e) => e.event === "closed" && e.gadget === id);
  await gadget.close();
  await offline;
  await h.command({ cmd: "settings", gadget: id, name: "Kitchen" });
  const { gadget: back, result } = await connectGadget({ port: h.port, key, name: "Desk" });
  assert.deepEqual(result.bot, { id: "b_jev", name: "Jév" });
  assert.deepEqual(await back.next("settings"), { op: "settings", bot: { id: "b_jev", name: "Jév" }, settings: { speak_pushes: true }, name: "Kitchen" });
  assert.equal(h.state.gadgets.get(id)!.name, "Kitchen");
  const { result: third } = await connectGadget({ port: h.port, key, name: "Desk 2" });
  assert.equal(third.op, "ready");
  assert.equal(h.state.gadgets.get(id)!.name, "Desk 2", "the gadget's own rename wins once nothing is pending");
});

test("sense and event frames are reported as rx events and need no answer", async (t) => {
  const { h, gadget, events } = await setup(t, {}, { caps: { ...CAPS } });
  const last = nextEvent(h, (e) => e.event === "rx" && String(e.msg).includes('"op":"event"'));
  gadget.send({ op: "sense", battery_pct: 80, charging: true });
  gadget.send({ op: "event", name: "button.long_press" });
  await last;
  const rx = events.filter((e) => e.event === "rx" && e.gadget === gadget.id).map((e) => JSON.parse(String(e.msg)).op);
  assert.deepEqual(rx.slice(-2), ["sense", "event"]);
});
