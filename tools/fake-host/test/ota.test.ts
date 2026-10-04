// SPDX-License-Identifier: Apache-2.0
import { test, type TestContext } from "node:test";
import assert from "node:assert/strict";
import { createHash, randomBytes } from "node:crypto";
import { mkdtempSync, readFileSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { b64DecodeCanonical } from "../../../protocol/lib/encoding.ts";
import { decodeFwChunk } from "../../../protocol/lib/frames.ts";
import { firmwareText } from "../../../protocol/lib/identity.ts";
import { verifyP256 } from "../../../protocol/lib/verify.ts";
import { startFakeHost, type FakeHost } from "../src/server.ts";
import type { HostEvent } from "../src/context.ts";
import { buildOffer } from "../src/ota.ts";
import { CAPS, connectGadget, nextEvent, randomKey, type TestGadget } from "./gadget-client.ts";

const T1_PUB = b64DecodeCanonical(readFileSync(new URL("../../../keys/test-t1.pub.b64", import.meta.url), "utf8").trim())!;
const T1_KEY = readFileSync(new URL("../../../keys/test-t1.key.hex", import.meta.url), "utf8").trim();

/** The gadget side of §4.8, as the firmware does it: checks in order, contiguous chunks,
 *  progress every 16 KiB and at the end, size + SHA-256 at commit. */
async function gadgetOta(g: TestGadget, own: { board: string; fw: string; otaMax: number }): Promise<{ code?: string; image?: Uint8Array; maxUnacked: number }> {
  const offer = await g.next("fw.offer");
  const fail = (code: string) => {
    g.send({ op: "fw.fail", stream: offer.stream, code });
    return { code, maxUnacked: 0 };
  };
  if (offer.board !== own.board) return fail("wrong_board");
  if (offer.version === own.fw) return fail("same_version");
  if (offer.size > own.otaMax) return fail("too_large");
  if (offer.key_id !== "t1") return fail("unknown_key");
  const sig = b64DecodeCanonical(offer.sig);
  if (!sig || !verifyP256(T1_PUB, firmwareText(own.board, offer.version, offer.size, offer.sha256), sig)) return fail("bad_sig");
  g.send({ op: "fw.ready", stream: offer.stream });
  const image = new Uint8Array(offer.size);
  let written = 0;
  let acked = 0;
  let maxUnacked = 0;
  while (written < offer.size) {
    const f = await g.nextBinary(4);
    const chunk = decodeFwChunk(f.payload)!;
    if (f.stream !== offer.stream || chunk.offset !== written) return fail("sequence");
    image.set(chunk.data, written);
    written += chunk.data.length;
    maxUnacked = Math.max(maxUnacked, written - acked);
    if (Math.floor(written / 16384) > Math.floor(acked / 16384) || written === offer.size) {
      g.send({ op: "fw.progress", stream: offer.stream, offset: written });
      acked = written;
    }
  }
  await g.next("fw.commit");
  if (createHash("sha256").update(image).digest("hex") !== offer.sha256) return { ...fail("checksum"), maxUnacked };
  return { image, maxUnacked };
}

async function setup(t: TestContext, o: Record<string, unknown> = {}): Promise<{ h: FakeHost; events: HostEvent[]; key: string; gadget: TestGadget; imagePath: string; image: Uint8Array }> {
  const events: HostEvent[] = [];
  const h = await startFakeHost({ port: 0, quiet: true, code: "123456", ...o }, { listener: (e) => events.push(e) });
  t.after(() => h.close());
  const key = randomKey();
  const { gadget } = await connectGadget({ port: h.port, enroll: "123456", key, fw: "1.0.0" });
  const image = new Uint8Array(randomBytes(200_000));
  const imagePath = join(mkdtempSync(join(tmpdir(), "fake-host-ota-")), "app.bin");
  writeFileSync(imagePath, image);
  return { h, events, key, gadget, imagePath, image };
}

test("buildOffer signs the §4.8 text with t1, and each tamper breaks exactly one check", () => {
  const image = new Uint8Array([1, 2, 3]);
  const base = { image, board: "amoled-175c", version: "1.1.0", keyHex: T1_KEY, keyId: "t1", otaMax: 6291456 };
  const ok = buildOffer(base);
  assert.equal(ok.size, 3);
  assert.equal(ok.sha256, createHash("sha256").update(image).digest("hex"));
  assert.ok(verifyP256(T1_PUB, firmwareText("amoled-175c", "1.1.0", 3, ok.sha256), b64DecodeCanonical(ok.sig)!));
  const sig = buildOffer({ ...base, tamper: "sig" });
  assert.ok(!verifyP256(T1_PUB, firmwareText("amoled-175c", "1.1.0", 3, sig.sha256), b64DecodeCanonical(sig.sig)!));
  const sha = buildOffer({ ...base, tamper: "sha256" });
  assert.notEqual(sha.sha256, ok.sha256);
  assert.ok(verifyP256(T1_PUB, firmwareText("amoled-175c", "1.1.0", 3, sha.sha256), b64DecodeCanonical(sha.sig)!));
  const size = buildOffer({ ...base, tamper: "size" });
  assert.equal(size.size, 6291457);
  assert.ok(verifyP256(T1_PUB, firmwareText("amoled-175c", "1.1.0", 6291457, size.sha256), b64DecodeCanonical(size.sig)!));
});

test("a full update: offer, ready, windowed chunks, progress, commit, then fw.installed after the restart", async (t) => {
  const { h, events, key, gadget, imagePath, image } = await setup(t);
  const ack = await h.command({ cmd: "ota", image: imagePath, version: "1.1.0" });
  assert.equal(ack.ok, true);
  assert.equal(ack.size, image.length);
  const r = await gadgetOta(gadget, { board: "amoled-175c", fw: "1.0.0", otaMax: 6291456 });
  assert.equal(r.code, undefined);
  assert.deepEqual(r.image, image);
  assert.ok(r.maxUnacked <= 65536, `host kept ${r.maxUnacked} bytes unacknowledged`);
  const offer = gadget.all.find((m) => m.op === "fw.offer")!;
  assert.deepEqual([offer.board, offer.version, offer.key_id, offer.size], ["amoled-175c", "1.1.0", "t1", image.length]);
  await gadget.close();
  const { gadget: rebooted } = await connectGadget({ port: h.port, key, fw: "1.1.0" });
  const installed = nextEvent(h, (e) => e.event === "ota" && e.phase === "installed");
  rebooted.send({ op: "fw.installed", version: "1.1.0" });
  assert.deepEqual(await installed, { event: "ota", gadget: rebooted.id, phase: "installed", version: "1.1.0" });
  const phases = events.filter((e) => e.event === "ota").map((e) => e.phase);
  assert.deepEqual([phases[0], phases[1], phases.at(-2), phases.at(-1)], ["offered", "ready", "committed", "installed"]);
  assert.ok(phases.filter((p) => p === "progress").length >= Math.floor(image.length / 16384));
});

test("refused offers: bad_sig, too_large, wrong_board, same_version are reported as failed", async (t) => {
  const { h, gadget, imagePath } = await setup(t);
  const own = { board: "amoled-175c", fw: "1.0.0", otaMax: 6291456 };
  const cases: Array<[Record<string, unknown>, string]> = [
    [{ tamper: "sig" }, "bad_sig"], [{ tamper: "size" }, "too_large"], [{ board: "lcd-154" }, "wrong_board"], [{ version: "1.0.0" }, "same_version"],
  ];
  for (const [extra, code] of cases) {
    const failed = nextEvent(h, (e) => e.event === "ota" && e.phase === "failed");
    assert.equal((await h.command({ cmd: "ota", image: imagePath, version: "1.1.0", ...extra })).ok, true);
    assert.equal((await gadgetOta(gadget, own)).code, code);
    assert.equal((await failed).code, code);
  }
});

test("a tampered sha256 is accepted at offer time and fails with checksum at commit", async (t) => {
  const { h, gadget, imagePath } = await setup(t);
  const failed = nextEvent(h, (e) => e.event === "ota" && e.phase === "failed", 5000);
  await h.command({ cmd: "ota", image: imagePath, version: "1.1.0", tamper: "sha256" });
  assert.equal((await gadgetOta(gadget, { board: "amoled-175c", fw: "1.0.0", otaMax: 6291456 })).code, "checksum");
  assert.equal((await failed).code, "checksum");
});

test("no fw.ready within the timeout: the host gives up; only one update at a time; ota needs caps", async (t) => {
  const { h, gadget, imagePath } = await setup(t, { fwReadyTimeoutMs: 150 });
  const failed = nextEvent(h, (e) => e.event === "ota" && e.phase === "failed");
  await h.command({ cmd: "ota", image: imagePath, version: "1.1.0" });
  assert.equal((await h.command({ cmd: "ota", image: imagePath, version: "1.1.0" })).error, "an update is already running on this gadget");
  await gadget.next("fw.offer");
  assert.equal((await failed).code, "ready_timeout");
  const { gadget: plain } = await connectGadget({ port: h.port, enroll: undefined, key: gadget.key, caps: { ...CAPS, ota: undefined } });
  assert.equal((await h.command({ cmd: "ota", gadget: plain.id, image: imagePath, version: "1.1.0" })).error, "this gadget has no ota caps");
});

test("the host keeps at most 64 KiB unacknowledged and each fw.progress opens the window again", async (t) => {
  const { h, gadget, imagePath } = await setup(t);
  await h.command({ cmd: "ota", image: imagePath, version: "1.1.0" });
  const offer = await gadget.next("fw.offer");
  gadget.send({ op: "fw.ready", stream: offer.stream });
  const drain = async (): Promise<number> => {
    let bytes = 0;
    for (;;) {
      try {
        bytes += decodeFwChunk((await gadget.nextBinary(4, 300)).payload)!.data.length;
      } catch {
        return bytes;
      }
    }
  };
  assert.equal(await drain(), 65536, "without fw.progress the host stops after one 64 KiB window");
  gadget.send({ op: "fw.progress", stream: offer.stream, offset: 16384 });
  assert.equal(await drain(), 16384, "acknowledging 16 KiB lets exactly 16 KiB more through");
});
