// SPDX-License-Identifier: Apache-2.0
import { test, type TestContext } from "node:test";
import assert from "node:assert/strict";
import { Buffer } from "node:buffer";
import net from "node:net";
import { mkdtempSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { b64Encode } from "../../../protocol/lib/encoding.ts";
import { proveText } from "../../../protocol/lib/identity.ts";
import { signP256 } from "../../../protocol/lib/verify.ts";
import { startFakeHost, type FakeHost } from "../src/server.ts";
import type { FakeHostOptions } from "../src/options.ts";
import type { HostEvent } from "../src/context.ts";
import { connectGadget, delay, helloFor, nextEvent, openSocket, randomKey, tryUpgrade } from "./gadget-client.ts";

async function host(t: TestContext, o: Partial<FakeHostOptions> = {}): Promise<{ h: FakeHost; events: HostEvent[] }> {
  const events: HostEvent[] = [];
  const h = await startFakeHost({ port: 0, quiet: true, code: "123456", ...o }, { listener: (e) => events.push(e) });
  t.after(() => h.close());
  return { h, events };
}

test("upgrades: Origin 403, no subprotocol 400, other paths 404, bad version 426, no extensions", async (t) => {
  const { h } = await host(t);
  assert.equal(await tryUpgrade(h.port, { headers: { Origin: "http://example.test" } }), "status 403");
  assert.equal(await tryUpgrade(h.port, { protocol: null }), "status 400");
  assert.equal(await tryUpgrade(h.port, { protocol: "other.1" }), "status 400");
  assert.equal(await tryUpgrade(h.port, { path: "/other" }), "status 404");
  assert.equal(await tryUpgrade(h.port, { deflate: true }), 'open ext=""');
  const raw = await new Promise<string>((resolve) => {
    const s = net.connect(h.port, "127.0.0.1", () => {
      s.write("GET /gadget HTTP/1.1\r\nHost: x\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
        + "Sec-WebSocket-Version: 8\r\nSec-WebSocket-Protocol: openmausbot-gadget.1\r\n\r\n");
    });
    let buf = "";
    s.on("data", (d) => (buf += d.toString()));
    s.on("close", () => resolve(buf));
  });
  assert.match(raw, /^HTTP\/1\.1 426 /);
  assert.match(raw, /Sec-WebSocket-Version: 13/);
});

test("the listening and code events come first", async (t) => {
  const { h, events } = await host(t);
  assert.deepEqual(events.slice(0, 2).map((e) => e.event), ["listening", "code"]);
  assert.equal(events[0].port, h.port);
  assert.match(String(events[0].host_id), /^[0-9a-f]{32}$/);
  assert.equal(events[1].code, "123456");
});

test("enrolls with the code, then reconnects as a known gadget without one", async (t) => {
  const { h, events } = await host(t, { hostName: "Desk Mac" });
  const key = randomKey();
  const { gadget, challenge, result } = await connectGadget({ port: h.port, key, enroll: "123456" });
  assert.equal(challenge.host_id, h.hostId);
  assert.equal(challenge.host_name, "Desk Mac");
  assert.equal(challenge.nonce.length, 44);
  assert.equal(result.op, "ready");
  assert.match(result.session, /^s_[0-9a-f]{12}$/);
  assert.deepEqual(result.bot, { id: "b_fake", name: "Fake Bot" });
  assert.deepEqual(result.settings, { speak_pushes: false });
  assert.ok(events.some((e) => e.event === "enrolled" && e.gadget === gadget.id));
  assert.ok(events.some((e) => e.event === "ready" && e.gadget === gadget.id && e.session === result.session));
  assert.ok(events.some((e) => e.event === "rx" && e.gadget === null && String(e.msg).includes('"op":"hello"')));
  await gadget.close();
  const again = await connectGadget({ port: h.port, key, enroll: "999999" });
  assert.equal(again.result.op, "ready");
  assert.notEqual(again.challenge.nonce, challenge.nonce);
  assert.notEqual(again.result.session, result.session);
});

test("refusals: enroll_required, bad_code (wrong / used up), proto_unsupported, bad_sig; then close 1000", async (t) => {
  const { h, events } = await host(t);
  const none = await connectGadget({ port: h.port });
  assert.deepEqual([none.result.op, none.result.code], ["error", "enroll_required"]);
  assert.equal((await none.gadget.closed).code, 1000);
  assert.ok(events.some((e) => e.event === "refused" && e.code === "enroll_required"));
  const wrong = await connectGadget({ port: h.port, enroll: "000000" });
  assert.deepEqual([wrong.result.code, wrong.result.message], ["bad_code", "pairing code wrong"]);
  assert.equal((await connectGadget({ port: h.port, enroll: "123456" })).result.op, "ready");
  const used = await connectGadget({ port: h.port, enroll: "123456" });
  assert.deepEqual([used.result.code, used.result.message], ["bad_code", "pairing code used up"]);
  assert.equal((await connectGadget({ port: h.port, enroll: "123456", proto: 2 })).result.code, "proto_unsupported");
  const g = await openSocket(h.port);
  g.send({ ...helloFor(g, { port: h.port }), id: "gad_0000000000000000" });
  assert.equal((await g.next("error")).code, "bad_sig");
});

test("a device cap of 0 answers device_limit and keeps the window open", async (t) => {
  const { h } = await host(t, { maxDevices: 0 });
  assert.equal((await connectGadget({ port: h.port, enroll: "123456" })).result.code, "device_limit");
  assert.equal(h.state.window!.used, false);
});

test("a second connection with the same key replaces the first only after its prove", async (t) => {
  const { h } = await host(t);
  const key = randomKey();
  const first = await connectGadget({ port: h.port, key, enroll: "123456" });
  const intruder = await openSocket(h.port, randomKey());
  intruder.send({ ...helloFor(intruder, { port: h.port }), id: first.gadget.id });
  assert.equal((await intruder.next("error")).code, "bad_sig");
  // A forger replays the victim's public hello (same id and pubkey) but cannot sign its prove.
  const forger = await openSocket(h.port, key);
  forger.send(helloFor(forger, { port: h.port }));
  const ch = await forger.next("challenge");
  forger.send({ op: "prove", sig: b64Encode(signP256(randomKey(), proveText(forger.id, ch.nonce, ch.host_id))) });
  assert.equal((await forger.next("error")).code, "bad_sig");
  // The old session is still the live one: the close command reaches it, and it never saw an error.
  assert.equal((await h.command({ cmd: "close", gadget: first.gadget.id, code: 4002 })).ok, true);
  assert.equal((await first.gadget.closed).code, 4002, "the old session stays untouched until a prove verifies");
  assert.ok(!first.gadget.all.some((m) => m.op === "error"), first.gadget.ops().join(" "));
  const back = await connectGadget({ port: h.port, key });
  assert.equal(back.result.op, "ready");
  const second = await connectGadget({ port: h.port, key });
  assert.equal(second.result.op, "ready");
  const err = await back.gadget.next("error");
  assert.deepEqual([err.code, (await back.gadget.closed).code], ["replaced", 1000]);
});

test("revoke sends error revoked, forgets the gadget, and replace / drop / close act on the live socket", async (t) => {
  const { h } = await host(t);
  const key = randomKey();
  const a = await connectGadget({ port: h.port, key, enroll: "123456" });
  assert.deepEqual(await h.command({ cmd: "revoke" }), { event: "ack", cmd: "revoke", ok: true });
  assert.equal((await a.gadget.next("error")).code, "revoked");
  assert.equal((await connectGadget({ port: h.port, key })).result.code, "enroll_required");
  assert.equal((await h.command({ cmd: "code", code: "222222" })).code, "222222");
  const b = await connectGadget({ port: h.port, key, enroll: "222222" });
  assert.equal(b.result.op, "ready");
  await h.command({ cmd: "replace" });
  assert.equal((await b.gadget.next("error")).code, "replaced");
  const c = await connectGadget({ port: h.port, key });
  await h.command({ cmd: "close", code: 4000 });
  assert.equal((await c.gadget.closed).code, 4000);
  const d = await connectGadget({ port: h.port, key });
  await h.command({ cmd: "drop" });
  assert.equal((await d.gadget.closed).code, 1006);
  const ack = await h.command({ cmd: "drop" });
  assert.deepEqual([ack.ok, ack.error], [false, `gadget ${d.gadget.id} is not connected`]);
});

test("commands: unknown names, a missing gadget and bad codes are refused with an ack", async (t) => {
  const { h } = await host(t);
  assert.deepEqual(await h.command({ cmd: "nope" }), { event: "ack", cmd: "nope", ok: false, error: "unknown command nope" });
  assert.equal((await h.command({ cmd: "drop" })).error, "no gadget has connected yet");
  assert.equal((await h.command({ cmd: "code", code: "12" })).error, "code must be six digits");
});

test("ops before ready, unknown ops and invalid JSON are ignored", async (t) => {
  const { h } = await host(t);
  const g = await openSocket(h.port);
  g.send({ op: "say", turn: "t00000000-1", text: "too early" });
  g.sendRaw("not json");
  g.send(helloFor(g, { port: h.port }));
  assert.equal((await g.next("challenge")).op, "challenge");
  const { gadget } = await connectGadget({ port: h.port, enroll: "123456" });
  const lastJunk = nextEvent(h, (e) => e.event === "rx" && e.msg === '{"op": 5}');
  gadget.send({ op: "nope", x: 1 });
  gadget.sendRaw("[1,2]");
  gadget.sendRaw('{"op": 5}');
  await lastJunk;
  await h.command({ cmd: "revoke" });
  assert.equal((await gadget.next("error")).code, "revoked", "the session survived the junk");
});

test("timing: handshake deadline closes 1008, pings keep a gadget alive, and an idle gadget is dropped", async (t) => {
  const { h, events } = await host(t, { handshakeMs: 100, pingMs: 30, idleMs: 400 });
  const silent = await openSocket(h.port);
  assert.equal((await silent.closed).code, 1008);
  const { gadget } = await connectGadget({ port: h.port, enroll: "123456" });
  await gadget.nextPing();
  await gadget.nextPing();
  await delay(600);
  assert.ok(!events.some((e) => e.event === "closed" && e.gadget === gadget.id), "a gadget that answers pings is never idle");
  // Stop reading: the client no longer answers pings, so the host sees no inbound frame.
  const ws = gadget.ws as unknown as { _socket: net.Socket };
  ws._socket.pause();
  const closed = await nextEvent(h, (e) => e.event === "closed" && e.gadget === gadget.id, 3000);
  assert.equal(closed.event, "closed");
});

test("frame limits: text over 16 KiB and binary over 8 KiB close with 1009, invalid UTF-8 with 1007", async (t) => {
  const { h } = await host(t);
  const a = await connectGadget({ port: h.port, enroll: "123456" });
  a.gadget.sendRaw(JSON.stringify({ op: "event", name: "x", data: "y".repeat(17000) }));
  assert.equal((await a.gadget.closed).code, 1009);
  const b = await connectGadget({ port: h.port, key: a.gadget.key });
  b.gadget.sendRaw(new Uint8Array(8193).fill(1));
  assert.equal((await b.gadget.closed).code, 1009);
  const c = await connectGadget({ port: h.port, key: a.gadget.key });
  c.gadget.ws.send(Buffer.from([0x7b, 0xff, 0xfe, 0x7d]), { binary: false });
  assert.equal((await c.gadget.closed).code, 1007);
});

test("--state keeps host_id and enrolled gadgets across restarts", async (t) => {
  const dir = mkdtempSync(join(tmpdir(), "fake-host-state-"));
  const key = randomKey();
  const one = await startFakeHost({ port: 0, quiet: true, code: "123456", stateDir: dir });
  const first = await connectGadget({ port: one.port, key, enroll: "123456" });
  assert.equal(first.result.op, "ready");
  await one.close();
  const two = await startFakeHost({ port: 0, quiet: true, stateDir: dir });
  t.after(() => two.close());
  assert.equal(two.hostId, one.hostId);
  assert.equal((await connectGadget({ port: two.port, key })).result.op, "ready");
});
