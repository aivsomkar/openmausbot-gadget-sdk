// SPDX-License-Identifier: Apache-2.0
import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import { test } from "node:test";
import { COPY } from "../src/copy.ts";
import { releaseLine, supportsWebSerial } from "../src/page.ts";

test("index.html carries the trademark sentence and every element main.ts looks up", async () => {
  const html = await readFile(new URL("../index.html", import.meta.url), "utf8");
  assert.ok(html.includes("The OpenMausBot name, the MausBot name and the Maus mascot are trademarks of Supamaus Software Private Limited."));
  const main = await readFile(new URL("../src/main.ts", import.meta.url), "utf8");
  const ids = new Set([...main.matchAll(/(?:el(?:<[^>]+>)?|text|show)\("([a-z0-9-]+)"/g)].map((m) => m[1]));
  for (const id of ids) assert.ok(html.includes(`id="${id}"`), `index.html is missing #${id}`);
  // main.ts uses the tested helpers below rather than its own copies.
  assert.match(main, /supportsWebSerial\(navigator\)/);
  assert.match(main, /releaseLine\(index\)/);
});

test("the page gates on 'serial' in navigator, not on the browser's name (spec §5.8)", () => {
  assert.equal(supportsWebSerial({}), false);
  assert.equal(supportsWebSerial({ serial: {} }), true);
});

test("before the first release the page says no firmware is published", () => {
  assert.equal(releaseLine({ version: null, boards: {} }), COPY.noFirmware);
  assert.equal(releaseLine({ version: "1.1.0", boards: {} }), "Firmware 1.1.0");
});
