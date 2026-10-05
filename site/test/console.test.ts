// SPDX-License-Identifier: Apache-2.0
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { test } from "node:test";
import {
  createLineSplitter,
  isDownloadModeLine,
  normalizeHostAddress,
  normalizePairCode,
  parseOmbLine,
  quoteArg,
  setupCommands,
  wifiProblem,
} from "../src/console.ts";

/** SSIDs and passwords the console quoting must survive (Review Focus 1); console-firmware.test.ts and tools/console use the same file. */
const AWKWARD = JSON.parse(readFileSync(new URL("./awkward-values.json", import.meta.url), "utf8")) as string[];

/** A JavaScript copy of the firmware's argument splitter rules (contract §2.11); console-firmware.test.ts checks the real one. */
function splitArgv(line: string): string[] | null {
  const args: string[] = [];
  let cur = "";
  let inArg = false;
  let quoted = false;
  for (let i = 0; i < line.length; i++) {
    const c = line[i] as string;
    if (c === "\\" && i + 1 < line.length && ["\\", '"', " "].includes(line[i + 1] as string)) {
      cur += line[++i];
      inArg = true;
    } else if (c === '"') {
      quoted = !quoted;
      inArg = true;
    } else if (/\s/.test(c) && !quoted) {
      if (inArg) args.push(cur);
      cur = "";
      inArg = false;
    } else {
      cur += c;
      inArg = true;
    }
  }
  if (quoted) return null;
  if (inArg) args.push(cur);
  return args;
}

test("parseOmbLine reads every contract example", () => {
  const lines = [
    '@omb {"op":"boot","board":"amoled-175c","fw":"1.0.0","id":"gad_3f9a0c2b7e41d856"}',
    '@omb {"op":"status","wifi":"connected","ssid":"Home","host":"192.168.1.20:8810","id":"gad_3f9a0c2b7e41d856","pair":"paired","fw":"1.0.0","battery":{"pct":82,"charging":false},"board":"amoled-175c","name":"Desk Maus","host_name":"Omkar\'s computer"}',
    '@omb {"op":"status","wifi":"connecting","ssid":"Home","id":"gad_3f9a0c2b7e41d856","pair":"error","error":"bad_code","fw":"1.0.0","board":"lcd-154","name":"Maus 3f9a"}',
    '@omb {"op":"scan","networks":[{"ssid":"Home","rssi":-52,"auth":"wpa2"},{"ssid":"Cafe","rssi":-80,"auth":"open"}]}',
    '@omb {"op":"hosts","hosts":[{"name":"Omkar\'s computer","address":"192.168.1.20:8810","id":"0123456789abcdef0123456789abcdef"}]}',
    '@omb {"op":"say","turn":"t3f9a0c2b-7"}',
    '@omb {"op":"error","cmd":"pair","message":"pair needs a six-digit code"}',
  ];
  assert.deepEqual(lines.map((l) => parseOmbLine(l)?.op), ["boot", "status", "status", "scan", "hosts", "say", "error"]);
  const status = parseOmbLine(lines[2] as string);
  assert.equal(status?.op === "status" && status.error, "bad_code");
});

test("parseOmbLine strips ANSI colour and a trailing CR, and ignores the rest of the log", () => {
  assert.equal(parseOmbLine('\u001b[0;32m@omb {"op":"say","turn":"t1-1"}\u001b[0m\r')?.op, "say");
  assert.equal(parseOmbLine("\u001b[0;32mI (812) wifi: connected\u001b[0m"), null);
  assert.equal(parseOmbLine("I (812) main: @omb is not at the start"), null);
  assert.equal(parseOmbLine("@omb {not json"), null);
  assert.equal(parseOmbLine('@omb {"op":"status","wifi":"connec'), null);
  assert.equal(parseOmbLine('@omb {"op":"weather","temp":20}'), null);
  assert.equal(parseOmbLine('@omb {"op":"status","wifi":"maybe","id":"x","pair":"paired","fw":"1"}'), null);
  assert.equal(parseOmbLine('@omb ["op","status"]'), null);
  assert.equal(parseOmbLine('@omb {"op":"status","wifi":"off","id":"x","pair":"unpaired","fw":"1","extra":true}')?.op, "status");
});

test("the line splitter handles CR, LF, CRLF, and lines cut across USB packets", () => {
  const split = createLineSplitter();
  assert.deepEqual(split('@omb {"op":"sa'), []);
  assert.deepEqual(split('y","turn":"t1-1"}\r'), ['@omb {"op":"say","turn":"t1-1"}']);
  assert.deepEqual(split("\nnext\r\nthird\n"), ["next", "third"]);
  assert.deepEqual(split("a\n\nb\r\r"), ["a", "", "b", ""]);
  assert.deepEqual(split("é😀 partial"), []);
  assert.deepEqual(split("\n"), ["é😀 partial"]);
});

test("quoteArg round-trips awkward SSIDs and passwords through the firmware's splitter", () => {
  for (const v of AWKWARD) {
    const line = `wifi ${quoteArg(v)} ${quoteArg("pass word")}`;
    assert.deepEqual(splitArgv(line), ["wifi", v, "pass word"], JSON.stringify(v));
  }
  assert.throws(() => quoteArg("line\nbreak"), RangeError);
  assert.throws(() => quoteArg("cr\rhere"), RangeError);
});

test("setupCommands sends pair, then wifi, then host", () => {
  assert.deepEqual(setupCommands({ code: "123456", ssid: "Home Net", password: "hunter22" }), [
    "pair 123456",
    'wifi "Home Net" "hunter22"',
    "host auto",
  ]);
  assert.deepEqual(setupCommands({ code: "123456", ssid: "Cafe", password: "", address: "http://192.168.1.20:8810/" })[2], "host 192.168.1.20:8810");
  assert.throws(() => setupCommands({ code: "12345", ssid: "Home", password: "" }), RangeError);
  assert.throws(() => setupCommands({ code: "123456", ssid: "Home", password: "short" }), RangeError);
});

test("wifiProblem mirrors the console's wifi rules", () => {
  assert.equal(wifiProblem("Home", ""), null);
  assert.equal(wifiProblem("Home", "12345678"), null);
  assert.equal(wifiProblem("Home", "a".repeat(63)), null);
  assert.equal(wifiProblem("Home", "0123456789abcdef".repeat(4)), null);
  assert.match(wifiProblem("Home", "1234567") ?? "", /8 to 63/);
  assert.match(wifiProblem("Home", "z".repeat(64)) ?? "", /8 to 63/);
  assert.match(wifiProblem("", "") ?? "", /1 to 32/);
  assert.match(wifiProblem("ü".repeat(17), "") ?? "", /1 to 32/);
  assert.match(wifiProblem("Home", "pass\nword") ?? "", /line breaks/);
});

test("pairing codes and host addresses are normalized from what people type", () => {
  assert.equal(normalizePairCode(" 123 456 "), "123456");
  assert.equal(normalizePairCode("123-456"), "123456");
  assert.equal(normalizePairCode("12345"), null);
  assert.equal(normalizePairCode("１２３４５６"), null);
  assert.equal(normalizeHostAddress(" 192.168.1.20:8810 "), "192.168.1.20:8810");
  assert.equal(normalizeHostAddress("http://192.168.1.20:8810/gadget"), "192.168.1.20:8810");
  assert.equal(normalizeHostAddress("omkars-mac.local"), "omkars-mac.local");
  assert.equal(normalizeHostAddress("192.168.1.20:0"), null);
  assert.equal(normalizeHostAddress("192.168.1.20:70000"), null);
  assert.equal(normalizeHostAddress("my mac"), null);
  assert.equal(normalizeHostAddress("[fe80::1]:8810"), null);
});

test("download-mode banners are recognised", () => {
  assert.equal(isDownloadModeLine("rst:0x15 (USB_UART_CHIP_RESET),boot:0x0 (DOWNLOAD(USB/UART0))"), true);
  assert.equal(isDownloadModeLine("waiting for download"), true);
  assert.equal(isDownloadModeLine("rst:0x15 (USB_UART_CHIP_RESET),boot:0x8 (SPI_FAST_FLASH_BOOT)"), false);
});
