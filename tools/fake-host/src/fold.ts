// SPDX-License-Identifier: Apache-2.0
// Screen text for caps.screen.text "latin1" (PROTOCOL.md §4.4): U+0020–U+007E, U+00A0–U+00FF and
// newline, plus U+2026 and U+2192. Other characters map to a close equivalent or are dropped.
// The fake host does not shape Markdown: its scripted texts are plain.
import { Buffer } from "node:buffer";

// Code point -> replacement. Curly quotes and primes, dashes and minus, bullet, thin and wide spaces,
// tab, and letters with no Unicode decomposition (L/l with stroke, D/d with stroke, OE, dotless i).
const MAP = new Map<number, string>([
  [0x2018, "'"], [0x2019, "'"], [0x201a, "'"], [0x201b, "'"], [0x2032, "'"],
  [0x201c, '"'], [0x201d, '"'], [0x201e, '"'], [0x201f, '"'], [0x2033, '"'],
  [0x2010, "-"], [0x2011, "-"], [0x2012, "-"], [0x2013, "-"], [0x2014, "-"], [0x2015, "-"], [0x2212, "-"],
  [0x2022, String.fromCodePoint(0xb7)], [0x2002, " "], [0x2003, " "], [0x2009, " "], [0x200a, " "], [0x202f, " "], [0x09, " "],
  [0x0141, "L"], [0x0142, "l"], [0x0110, "D"], [0x0111, "d"], [0x0152, "OE"], [0x0153, "oe"], [0x0131, "i"],
]);

function allowed(cp: number): boolean {
  return cp === 0x0a || (cp >= 0x20 && cp <= 0x7e) || (cp >= 0xa0 && cp <= 0xff) || cp === 0x2026 || cp === 0x2192;
}

/** Folds text to the latin1 screen charset. */
export function foldLatin1(text: string): string {
  let out = "";
  for (const ch of text.normalize("NFC")) {
    const cp = ch.codePointAt(0)!;
    if (allowed(cp)) out += ch;
    else if (MAP.has(cp)) out += MAP.get(cp);
    else {
      const base = ch.normalize("NFKD").replace(/\p{M}/gu, "");
      if (base.length > 0 && [...base].every((c) => allowed(c.codePointAt(0)!))) out += base;
    }
  }
  return out;
}

/** Cuts to at most `max` characters (code points). */
export function cutChars(text: string, max: number): string {
  const chars = [...text];
  return chars.length <= max ? text : chars.slice(0, max).join("");
}

/** Cuts to at most `maxBytes` UTF-8 bytes on a code-point boundary, keeping the start. */
export function cutUtf8(text: string, maxBytes: number): string {
  let out = "";
  let bytes = 0;
  for (const ch of text) {
    const n = Buffer.byteLength(ch, "utf8");
    if (bytes + n > maxBytes) break;
    out += ch;
    bytes += n;
  }
  return out;
}
