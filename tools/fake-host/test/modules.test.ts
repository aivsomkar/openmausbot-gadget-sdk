// SPDX-License-Identifier: Apache-2.0
// Every module loads under Node's type stripping (erasable TypeScript only) without side effects.
// main.ts and gen-vectors.ts are programs; cli.test.ts and `npm run vectors:check` run them.
import { test } from "node:test";
import assert from "node:assert/strict";
import { readdirSync } from "node:fs";

const dirs = ["../../../protocol/lib/", "../src/"];

test("every protocol/lib and fake-host module imports cleanly", async () => {
  let n = 0;
  for (const dir of dirs) {
    for (const f of readdirSync(new URL(dir, import.meta.url))) {
      if (!f.endsWith(".ts") || f === "main.ts") continue;
      await import(new URL(dir + f, import.meta.url).href);
      n++;
    }
  }
  assert.ok(n >= 20, `imported ${n} modules`);
});
