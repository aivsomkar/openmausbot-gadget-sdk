// SPDX-License-Identifier: Apache-2.0
import assert from "node:assert/strict";
import { test } from "node:test";
import { InstallerError } from "../src/errors.ts";
import { flashBoard, flashSizeMb, type FlashDeps, type FlashStage } from "../src/flash.ts";
import { parseInstallIndex } from "../src/install.ts";
import { FakeLoader } from "./fakes-esptool.ts";

const index = parseInstallIndex({
  version: "1.1.0",
  boards: {
    "lcd-154": {
      parts: [
        { path: "b.bin", offset: "0x0" },
        { path: "p.bin", offset: "0x8000" },
        { path: "o.bin", offset: "0xf000" },
        { path: "a.bin", offset: "0x20000" },
      ],
      full: "f.bin",
    },
  },
});
const sizes: Record<string, number> = { "b.bin": 100, "p.bin": 50, "o.bin": 50, "a.bin": 800 };
/** A writeReg call as FakeLoader records it (no mask → 0xffffffff). */
const reg = (addr: number, value: number) => `writeReg 0x${addr.toString(16)} ${value} ${0xffffffff}`;

function deps(loader: FakeLoader, over: Partial<FlashDeps> = {}) {
  const stages: FlashStage[] = [];
  const progress: number[] = [];
  const fetched: string[] = [];
  const warnings: string[] = [];
  const d: FlashDeps = {
    loader,
    index,
    board: "lcd-154",
    eraseAll: false,
    fetchPart: async (path) => {
      fetched.push(path);
      loader.calls.push(`fetch ${path}`);
      return new Uint8Array(sizes[path] ?? 0).fill(1);
    },
    md5: (data) => `md5:${data.length}`,
    onStage: (s) => stages.push(s),
    onProgress: (f) => progress.push(f),
    onWarning: (m) => warnings.push(m),
    ...over,
  };
  return { d, stages, progress, fetched, warnings };
}

test("flashBoard downloads every part, checks the board, turns the watchdogs off, then writes the separate parts", async () => {
  const loader = new FakeLoader();
  const { d, stages, progress, warnings } = deps(loader);
  assert.deepEqual(await flashBoard(d), { bytes: 1000 });
  assert.deepEqual(loader.calls, [
    "fetch b.bin",
    "fetch p.bin",
    "fetch o.bin",
    "fetch a.bin",
    "main default_reset",
    "detectFlashSize",
    reg(0x600080b0, 0x50d83aa1), // unlock the RTC watchdog
    reg(0x60008098, 0), //           RTC watchdog off
    reg(0x600080b0, 0), //           lock it again
    reg(0x600080b8, 0x8f1d312a), // unlock the super watchdog
    "readReg 0x600080b4",
    reg(0x600080b4, 0x80000012), //  SWD_AUTO_FEED_EN, other bits kept
    reg(0x600080b8, 0), //           lock it again
    "writeFlash eraseAll=false",
  ]);
  assert.deepEqual(warnings, []);
  const w = loader.written;
  assert.ok(w);
  assert.deepEqual(w.fileArray.map((f) => f.address), [0x0, 0x8000, 0xf000, 0x20000]);
  assert.equal(w.flashMode, "keep");
  assert.equal(w.flashFreq, "keep");
  assert.equal(w.flashSize, "keep");
  assert.equal(w.compress, true);
  assert.deepEqual(loader.md5s, ["md5:100", "md5:50", "md5:50", "md5:800"]);
  assert.deepEqual(stages, ["downloading", "connecting", "writing", "done"]);
  assert.equal(progress.at(-1), 1);
  assert.ok(progress.every((p, i) => i === 0 || p >= (progress[i - 1] ?? 0)), "progress never goes backwards");
});

test("Erase everything passes eraseAll to esptool-js", async () => {
  const loader = new FakeLoader();
  await flashBoard(deps(loader, { eraseAll: true }).d);
  assert.equal(loader.written?.eraseAll, true);
});

test("a failed download never puts the board into download mode", async () => {
  const loader = new FakeLoader();
  const { d } = deps(loader, {
    fetchPart: async (p) => {
      if (p === "a.bin") throw new Error("HTTP 404");
      return new Uint8Array(10);
    },
  });
  await assert.rejects(flashBoard(d), (e: unknown) => e instanceof InstallerError && e.code === "download_failed");
  assert.equal(loader.calls.includes("main default_reset"), false);
});

test("a board that is not an ESP32-S3 is refused before writing", async () => {
  const loader = new FakeLoader();
  loader.chip.CHIP_NAME = "ESP32";
  await assert.rejects(flashBoard(deps(loader).d), (e: unknown) => e instanceof InstallerError && e.code === "wrong_chip");
  assert.equal(loader.written, null);
});

test("an 8 MB board is refused; an unknown flash size is allowed", async () => {
  const small = new FakeLoader();
  small.flashSize = "8MB";
  await assert.rejects(flashBoard(deps(small).d), (e: unknown) => e instanceof InstallerError && e.code === "flash_too_small");
  assert.equal(small.written, null);
  const unknown = new FakeLoader();
  unknown.flashSize = undefined;
  await flashBoard(deps(unknown).d);
  assert.ok(unknown.written);
});

test("a write that fails mid-way surfaces as write_failed", async () => {
  const loader = new FakeLoader();
  loader.writeFlashError = new Error("Timeout");
  await assert.rejects(flashBoard(deps(loader).d), (e: unknown) => e instanceof InstallerError && e.code === "write_failed" && /Timeout/.test(e.message));
});

test("a board that refuses the watchdog registers is still flashed, with a warning", async () => {
  const loader = new FakeLoader();
  loader.writeReg = async () => {
    throw new Error("Timeout waiting for response");
  };
  const { d, warnings } = deps(loader);
  await flashBoard(d);
  assert.ok(loader.written);
  assert.equal(warnings.length, 1);
  assert.match(warnings[0] ?? "", /watchdog/);
});

test("a board missing from the release is refused before any download", async () => {
  const loader = new FakeLoader();
  const { d, fetched } = deps(loader, { board: "devkit" });
  await assert.rejects(flashBoard(d), (e: unknown) => e instanceof InstallerError && e.code === "board_not_published");
  assert.deepEqual(fetched, []);
});

test("flashSizeMb parses esptool-js sizes", () => {
  assert.equal(flashSizeMb("16MB"), 16);
  assert.equal(flashSizeMb("32MB"), 32);
  assert.equal(flashSizeMb("512KB"), 0.5);
  assert.equal(flashSizeMb(undefined), null);
  assert.equal(flashSizeMb("detect"), null);
});
