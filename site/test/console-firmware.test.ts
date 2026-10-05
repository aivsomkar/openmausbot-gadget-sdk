// SPDX-License-Identifier: Apache-2.0
// Review Focus 1: what quoteArg writes must split back exactly with the
// firmware's own splitter (P2a's gadget_console_split), not only with the
// JavaScript copy in console.test.ts. Compiled the way check-keys.ts compiles
// the key tables; skipped with a message when there is no C compiler.
import assert from "node:assert/strict";
import { execFileSync, spawnSync } from "node:child_process";
import { mkdtempSync, readFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { test } from "node:test";
import { fileURLToPath } from "node:url";
import { quoteArg } from "../src/console.ts";

const repo = fileURLToPath(new URL("../../", import.meta.url));
const cc = process.env.CC ?? "cc";
const hasCc = spawnSync(cc, ["--version"], { stdio: "ignore" }).error === undefined;
const AWKWARD = JSON.parse(readFileSync(new URL("./awkward-values.json", import.meta.url), "utf8")) as string[];

test("quoteArg output splits back exactly with the firmware's gadget_console_split", { skip: hasCc ? false : `no C compiler (${cc})` }, () => {
  const exe = join(mkdtempSync(join(tmpdir(), "omb-split-")), "split-argv");
  execFileSync(
    cc,
    [
      "-std=c11",
      "-Wall",
      "-Wextra",
      "-Werror",
      "-I",
      join(repo, "firmware/core/include"),
      fileURLToPath(new URL("./split-argv.c", import.meta.url)),
      join(repo, "firmware/core/src/console.c"),
      "-o",
      exe,
    ],
    { stdio: ["ignore", "inherit", "inherit"] },
  );
  const lines = AWKWARD.map((v) => `wifi ${quoteArg(v)} ${quoteArg("pass word")}`);
  const out = execFileSync(exe, { input: `${lines.join("\n")}\n`, encoding: "utf8" }).trimEnd().split("\n");
  assert.equal(out.length, AWKWARD.length);
  AWKWARD.forEach((v, i) => assert.deepEqual(JSON.parse(out[i] as string), ["wifi", v, "pass word"], JSON.stringify(v)));
});
