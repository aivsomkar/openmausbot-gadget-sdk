// SPDX-License-Identifier: Apache-2.0
// make.ts turns the simulator's snapshot goldens into the README's images.
import assert from "node:assert/strict";
import { spawnSync } from "node:child_process";
import { mkdirSync, mkdtempSync, readFileSync, rmSync, writeFileSync } from "node:fs";
import { createRequire } from "node:module";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { test } from "node:test";
import { fileURLToPath } from "node:url";
import { SCREENS } from "../make.ts";

interface Png {
  width: number;
  height: number;
  data: Buffer;
}
const require = createRequire(import.meta.url);
const { PNG } = require("pngjs") as {
  PNG: { new (opts: { width: number; height: number }): Png; sync: { read(b: Buffer): Png; write(p: Png): Buffer } };
};
const MAKE = fileURLToPath(new URL("../make.ts", import.meta.url));

const ROUND_RGB = [0x10, 0xa0, 0x50] as const;
const SQUARE_RGB = [0xc0, 0x30, 0x30] as const;
const RECT_RGB = [0x30, 0x30, 0xc0] as const;

function solid(width: number, height: number, rgb: readonly [number, number, number]): Buffer {
  const png = new PNG({ width, height });
  for (let i = 0; i < width * height; i++) {
    png.data[i * 4] = rgb[0];
    png.data[i * 4 + 1] = rgb[1];
    png.data[i * 4 + 2] = rgb[2];
    png.data[i * 4 + 3] = 255;
  }
  return PNG.sync.write(png);
}

/** A fake snapshot folder: every mapped amoled-175c golden, plus the idle screen of the two other sizes. */
function fixtures(): string {
  const dir = mkdtempSync(join(tmpdir(), "omb-shots-"));
  mkdirSync(join(dir, "snapshots/amoled-175c"), { recursive: true });
  mkdirSync(join(dir, "snapshots/lcd-154"), { recursive: true });
  mkdirSync(join(dir, "snapshots/devkit"), { recursive: true });
  const round = solid(466, 466, ROUND_RGB);
  for (const [, golden] of SCREENS) writeFileSync(join(dir, "snapshots/amoled-175c", golden), round);
  writeFileSync(join(dir, "snapshots/lcd-154/idle.png"), solid(240, 240, SQUARE_RGB));
  writeFileSync(join(dir, "snapshots/devkit/idle.png"), solid(320, 240, RECT_RGB));
  return dir;
}

function run(dir: string) {
  return spawnSync(process.execPath, [MAKE, "--snapshots", join(dir, "snapshots"), "--out", join(dir, "out")], { encoding: "utf8" });
}

function px(png: Png, x: number, y: number): [number, number, number, number] {
  const i = (y * png.width + x) * 4;
  return [png.data[i]!, png.data[i + 1]!, png.data[i + 2]!, png.data[i + 3]!];
}

test("a round screen keeps the panel and clears the corners", (t) => {
  const dir = fixtures();
  t.after(() => rmSync(dir, { recursive: true, force: true }));
  const r = run(dir);
  assert.equal(r.status, 0, r.stderr);
  for (const [name] of SCREENS) {
    const png = PNG.sync.read(readFileSync(join(dir, "out", `screen-${name}.png`)));
    assert.equal(png.width, 466, name);
    assert.equal(png.height, 466, name);
  }
  const idle = PNG.sync.read(readFileSync(join(dir, "out/screen-idle.png")));
  assert.deepEqual(px(idle, 233, 233), [...ROUND_RGB, 255]);
  assert.equal(px(idle, 0, 0)[3], 0);
  assert.equal(px(idle, 465, 465)[3], 0);
  assert.equal(px(idle, 465, 0)[3], 0);
  // 232 px from the centre (pixel 1 and its mirror, pixel 464): still inside the panel.
  assert.equal(px(idle, 1, 233)[3], 255);
  assert.equal(px(idle, 233, 1)[3], 255);
  assert.equal(px(idle, 464, 233)[3], 255);
  assert.equal(px(idle, 233, 464)[3], 255);
  // The outermost pixels straddle the 1 px edge: anti-aliased, not stepped.
  for (const [x, y] of [[0, 233], [465, 233], [233, 0], [233, 465]] as const) {
    const edge = px(idle, x, y)[3];
    assert.ok(edge > 0 && edge < 255, `edge alpha ${edge} at (${x}, ${y})`);
  }
});

test("boards.png puts the three sizes side by side, bottom-aligned with 24 px gaps", (t) => {
  const dir = fixtures();
  t.after(() => rmSync(dir, { recursive: true, force: true }));
  const r = run(dir);
  assert.equal(r.status, 0, r.stderr);
  const boards = PNG.sync.read(readFileSync(join(dir, "out/boards.png")));
  assert.equal(boards.width, 466 + 24 + 240 + 24 + 320);
  assert.equal(boards.width, 1074);
  assert.equal(boards.height, 466);
  assert.deepEqual(px(boards, 233, 233), [...ROUND_RGB, 255]);
  assert.equal(px(boards, 0, 0)[3], 0, "the round panel's corner is clear");
  assert.equal(px(boards, 466 + 12, 465)[3], 0, "the first gap is clear");
  assert.deepEqual(px(boards, 490, 465), [...SQUARE_RGB, 255]);
  assert.deepEqual(px(boards, 490 + 239, 226), [...SQUARE_RGB, 255]);
  assert.equal(px(boards, 490 + 120, 225)[3], 0, "above the square screen is clear");
  assert.equal(px(boards, 730 + 12, 465)[3], 0, "the second gap is clear");
  assert.deepEqual(px(boards, 754, 226), [...RECT_RGB, 255]);
  assert.deepEqual(px(boards, 1073, 465), [...RECT_RGB, 255]);
  assert.equal(px(boards, 754 + 160, 225)[3], 0, "above the 320x240 screen is clear");
});

test("the same goldens give the same bytes", (t) => {
  const dir = fixtures();
  t.after(() => rmSync(dir, { recursive: true, force: true }));
  assert.equal(run(dir).status, 0);
  const first = ["screen-idle.png", "boards.png"].map((f) => readFileSync(join(dir, "out", f)));
  assert.equal(run(dir).status, 0);
  const second = ["screen-idle.png", "boards.png"].map((f) => readFileSync(join(dir, "out", f)));
  assert.deepEqual(second, first);
});

test("a missing mapped golden fails and names it", (t) => {
  const dir = fixtures();
  t.after(() => rmSync(dir, { recursive: true, force: true }));
  const [, golden] = SCREENS[3]!;
  rmSync(join(dir, "snapshots/amoled-175c", golden));
  const r = run(dir);
  assert.notEqual(r.status, 0);
  assert.ok(r.stderr.includes(golden), r.stderr);
});

test("a missing idle golden for another board size fails and names it", (t) => {
  const dir = fixtures();
  t.after(() => rmSync(dir, { recursive: true, force: true }));
  rmSync(join(dir, "snapshots/devkit/idle.png"));
  const r = run(dir);
  assert.notEqual(r.status, 0);
  assert.ok(r.stderr.includes("devkit/idle.png"), r.stderr);
});
