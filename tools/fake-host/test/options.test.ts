// SPDX-License-Identifier: Apache-2.0
import { test } from "node:test";
import assert from "node:assert/strict";
import { DEFAULT_OPTIONS, parseBot, parseCli } from "../src/options.ts";
import { cutChars, cutUtf8, foldLatin1 } from "../src/fold.ts";

test("parseCli defaults match the contract's CLI table", () => {
  const o = parseCli([]);
  assert.equal(o.port, 8810);
  assert.equal(o.bind, "127.0.0.1");
  assert.equal(o.code, null);
  assert.equal(o.codeTtlS, 120);
  assert.equal(o.hostName, "Fake MausBot");
  assert.deepEqual(o.bot, { id: "b_fake", name: "Fake Bot" });
  assert.equal(o.heard, "What's on my calendar today?");
  assert.equal(o.toneMs, 800);
  assert.equal(o.otaKeyId, "t1");
  assert.ok(o.otaKeyFile.endsWith("keys/test-t1.key.hex"));
  assert.equal(o.doneBeforeSpeech, false);
  assert.equal(o.pingMs, 15000);
  assert.equal(o.idleMs, 45000);
  assert.deepEqual(o, DEFAULT_OPTIONS);
});

test("parseCli reads every option and validates them", () => {
  const o = parseCli([
    "--port", "0", "--bind", "0.0.0.0", "--code", "123456", "--code-ttl", "30", "--host-id", "0123456789abcdef0123456789abcdef",
    "--host-name", "Desk Mac", "--bot", "b_jev:Jev: the bot", "--heard", "", "--reply", "Hi", "--tone-ms", "0", "--quiet", "--max-devices", "1",
    "--done-before-speech",
  ]);
  assert.equal(o.port, 0);
  assert.equal(o.code, "123456");
  assert.equal(o.codeTtlS, 30);
  assert.equal(o.hostId, "0123456789abcdef0123456789abcdef");
  assert.deepEqual(o.bot, { id: "b_jev", name: "Jev: the bot" });
  assert.equal(o.heard, "");
  assert.equal(o.toneMs, 0);
  assert.equal(o.quiet, true);
  assert.equal(o.maxDevices, 1);
  assert.equal(o.doneBeforeSpeech, true);
  assert.throws(() => parseCli(["--code", "12345"]), /six digits/);
  assert.throws(() => parseCli(["--port", "70000"]), /--port/);
  assert.throws(() => parseCli(["--host-id", "h_0123"]), /--host-id/);
  assert.throws(() => parseCli(["--nope"]), /Unknown option/);
  assert.deepEqual(parseBot(":"), { id: "", name: "" });
});

test("foldLatin1 keeps Latin-1, … and →, maps lookalikes and drops the rest", () => {
  assert.equal(foldLatin1("Café “quoted” — ok… → next 🎉"), 'Café "quoted" - ok… → next ');
  assert.equal(foldLatin1("Łódź\tŠtěpán"), "Lódz Stepán");
  assert.equal(foldLatin1("line1\nline2\r"), "line1\nline2");
  assert.equal(foldLatin1("日本"), "");
});

test("cutChars and cutUtf8 cut on code-point boundaries", () => {
  assert.equal(cutChars("ab😀cd", 3), "ab😀");
  assert.equal(cutUtf8("é".repeat(40), 64), "é".repeat(32));
  assert.equal(cutUtf8("a😀", 4), "a");
});
