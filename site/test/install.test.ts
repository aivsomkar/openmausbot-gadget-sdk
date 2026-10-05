// SPDX-License-Identifier: Apache-2.0
import assert from "node:assert/strict";
import { test } from "node:test";
import { fetchPart, flashPlan, loadInstallIndex, parseInstallIndex, type FetchLike } from "../src/install.ts";

const RELEASE = {
  version: "1.1.0",
  boards: {
    "amoled-175c": {
      parts: [
        { path: "openmausbot-gadget-amoled-175c-1.1.0-bootloader.bin", offset: "0x0" },
        { path: "openmausbot-gadget-amoled-175c-1.1.0-partition-table.bin", offset: "0x8000" },
        { path: "openmausbot-gadget-amoled-175c-1.1.0-ota-data-initial.bin", offset: "0xf000" },
        { path: "openmausbot-gadget-amoled-175c-1.1.0.bin", offset: "0x20000" },
      ],
      full: "openmausbot-gadget-amoled-175c-1.1.0-full.bin",
    },
  },
};

test("the contract's install.json parses and plans four writes in address order", () => {
  const index = parseInstallIndex(RELEASE);
  assert.deepEqual(flashPlan(index, "amoled-175c"), [
    { path: "openmausbot-gadget-amoled-175c-1.1.0-bootloader.bin", address: 0x0 },
    { path: "openmausbot-gadget-amoled-175c-1.1.0-partition-table.bin", address: 0x8000 },
    { path: "openmausbot-gadget-amoled-175c-1.1.0-ota-data-initial.bin", address: 0xf000 },
    { path: "openmausbot-gadget-amoled-175c-1.1.0.bin", address: 0x20000 },
  ]);
});

test("the plan never includes the merged -full.bin (it would erase NVS)", () => {
  const plan = flashPlan(parseInstallIndex(RELEASE), "amoled-175c");
  assert.equal(plan.some((p) => p.path.endsWith("-full.bin")), false);
  assert.equal(plan.some((p) => p.address === 0x9000), false);
});

test("before the first release the index is empty and every board is unpublished", () => {
  const index = parseInstallIndex({ version: null, boards: {} });
  assert.equal(index.version, null);
  assert.throws(() => flashPlan(index, "amoled-175c"), /no published firmware/);
});

test("parts listed out of order are still flashed in address order; uppercase hex is fine", () => {
  const shuffled = structuredClone(RELEASE);
  shuffled.boards["amoled-175c"].parts.reverse();
  shuffled.boards["amoled-175c"].parts[1] = { path: "openmausbot-gadget-amoled-175c-1.1.0-ota-data-initial.bin", offset: "0xF000" };
  assert.deepEqual(flashPlan(parseInstallIndex(shuffled), "amoled-175c").map((p) => p.address), [0, 0x8000, 0xf000, 0x20000]);
});

test("parseInstallIndex refuses broken or unsafe files", () => {
  const bad: unknown[] = [
    null,
    [],
    { version: "1.1", boards: {} },
    { version: "1.1.0", boards: [] },
    { version: null, boards: RELEASE.boards },
    { version: "1.1.0", boards: { "Amoled!": RELEASE.boards["amoled-175c"] } },
    { version: "1.1.0", boards: { x: { parts: [], full: "f.bin" } } },
    { version: "1.1.0", boards: { x: { parts: [{ path: "../../etc/passwd", offset: "0x0" }], full: "f.bin" } } },
    { version: "1.1.0", boards: { x: { parts: [{ path: "a.bin", offset: "0x0" }], full: "sub/f.bin" } } },
    { version: "1.1.0", boards: { x: { parts: [{ path: "a.bin", offset: "8000" }], full: "f.bin" } } },
    { version: "1.1.0", boards: { x: { parts: [{ path: "a.bin", offset: "0x8001" }], full: "f.bin" } } },
    { version: "1.1.0", boards: { x: { parts: [{ path: "a.bin", offset: "0x1000000" }], full: "f.bin" } } },
    { version: "1.1.0", boards: { x: { parts: [{ path: "a.bin", offset: "0x0" }, { path: "b.bin", offset: "0x000" }], full: "f.bin" } } },
    { version: "1.1.0", boards: { x: { parts: [{ path: "a.bin", offset: "0x0" }] } } },
    // The merged image as a part would be written over NVS and erase the pairing (Review Focus 4).
    { version: "1.1.0", boards: { x: { parts: [{ path: "f.bin", offset: "0x0" }], full: "f.bin" } } },
    { version: "1.1.0", boards: { x: { parts: [{ path: "a.bin", offset: "0x0" }, { path: "x-1.1.0-full.bin", offset: "0x20000" }], full: "f.bin" } } },
  ];
  for (const json of bad) assert.throws(() => parseInstallIndex(json), Error, JSON.stringify(json));
});

function fakeFetch(files: Record<string, unknown>): FetchLike & { urls: string[] } {
  const urls: string[] = [];
  const f = async (url: string) => {
    urls.push(url);
    const body = files[url];
    return {
      ok: body !== undefined,
      status: body === undefined ? 404 : 200,
      json: async () => body,
      arrayBuffer: async () => (body instanceof Uint8Array ? body.slice().buffer : new ArrayBuffer(0)),
    };
  };
  return Object.assign(f, { urls });
}

test("the page loads install.json and parts from its own origin", async () => {
  const f = fakeFetch({ "firmware/install.json": RELEASE, "firmware/a.bin": new Uint8Array([1, 2, 3]) });
  assert.equal((await loadInstallIndex(f)).version, "1.1.0");
  assert.deepEqual(await fetchPart(f, "a.bin"), new Uint8Array([1, 2, 3]));
  assert.deepEqual(f.urls, ["firmware/install.json", "firmware/a.bin"]);
  await assert.rejects(fetchPart(f, "missing.bin"), /HTTP 404/);
  await assert.rejects(fetchPart(fakeFetch({ "firmware/e.bin": new Uint8Array(0) }), "e.bin"), /empty/);
  await assert.rejects(fetchPart(f, "../x.bin"), RangeError);
  await assert.rejects(loadInstallIndex(fakeFetch({})), /HTTP 404/);
});
