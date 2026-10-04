// SPDX-License-Identifier: Apache-2.0
// PROTOCOL.md and protocol/lib/types.ts describe the same protocol.
import { test } from "node:test";
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { ERROR_CODES, FW_FAIL_CODES, OPS } from "../lib/types.ts";
import * as F from "./fixed-values.ts";

const doc = readFileSync(new URL("../PROTOCOL.md", import.meta.url), "utf8");
const HEADINGS = [
  "## 4.1 Transport", "## 4.2 Identity", "## 4.3 Handshake", "## 4.4 Conversation", "## 4.5 Approvals",
  "## 4.6 Push", "## 4.7 Display, actions and sensors", "## 4.8 Firmware updates", "## 4.9 Versioning and test vectors",
];
function section(n: string): string {
  const start = doc.indexOf(`## ${n} `);
  assert.ok(start >= 0, `missing section ${n}`);
  const next = doc.indexOf("\n## ", start + 1);
  return next < 0 ? doc.slice(start) : doc.slice(start, next);
}

test("PROTOCOL.md keeps the spec's section numbering, in order", () => {
  const at = HEADINGS.map((h) => doc.indexOf("\n" + h + "\n"));
  assert.ok(at.every((i) => i > 0), `missing: ${HEADINGS.filter((_, i) => at[i] < 0).join(", ")}`);
  assert.deepEqual([...at].sort((a, b) => a - b), at);
});

test("the op index lists exactly the ops in types.ts, with direction and section", () => {
  const rows = [...doc.matchAll(/^\| `([a-z.]+)` \| (g→h|h→g) \| (4\.\d) \|$/gm)].map((m) => ({
    op: m[1], dir: m[2] === "g→h" ? "g2h" : "h2g", section: m[3],
  }));
  assert.deepEqual(rows, OPS.map((o) => ({ op: o.op, dir: o.dir, section: o.section })));
});

test("every op is documented in its own section", () => {
  for (const { op, section: n } of OPS) {
    const text = section(n);
    const found = text.includes("`" + op + "`") || text.includes("`" + op + " ") || text.includes("→ " + op + " ");
    assert.ok(found, `${op} is not documented in §${n}`);
  }
});

test("error codes and fw.fail codes match types.ts", () => {
  const s43 = section("4.3");
  const errorTable = s43.slice(s43.indexOf("### `error`"), s43.indexOf("### How the gadget reacts"));
  const errors = [...errorTable.matchAll(/^\| `([a-z_]+)` \| /gm)].map((m) => m[1]);
  assert.deepEqual(errors, [...ERROR_CODES]);
  const line = section("4.8").split("\n").find((l) => l.startsWith("- **Failure codes:**"));
  assert.ok(line);
  assert.deepEqual([...line.matchAll(/`([a-z_]+)`/g)].map((m) => m[1]), [...FW_FAIL_CODES]);
});

test("the pinned vector inputs in §4.9 match the fixed values", () => {
  const s = section("4.9");
  for (const v of [F.RFC_PRIVATE_HEX, F.PINNED_NONCE_B64, F.PINNED_HOST_ID, F.LOW_S_HOST_ID, F.T1_SEED_TEXT]) {
    assert.ok(s.includes(v), `§4.9 does not mention ${v}`);
  }
});

test("prove and firmware texts in PROTOCOL.md have the pinned line layout", () => {
  assert.ok(section("4.3").includes("```\nopenmausbot-gadget/1\nprove\n<id>\n<nonce>\n<host_id>\n```"));
  assert.ok(section("4.8").includes("openmausbot-gadget/1\n  firmware\n  <board>\n  <version>\n  <size>\n  <sha256 lowercase hex>\n"));
});
