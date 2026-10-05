// SPDX-License-Identifier: Apache-2.0
// Spec §11: THIRD_PARTY.md lists every dependency with its license.
import assert from "node:assert/strict";
import { readFile, readdir } from "node:fs/promises";
import { join } from "node:path";
import { test } from "node:test";
import { fileURLToPath } from "node:url";

const repo = fileURLToPath(new URL("../../../", import.meta.url));
const TRADEMARK = "The OpenMausBot name, the MausBot name and the Maus mascot are trademarks of Supamaus Software Private Limited.";

async function readIf(path: string): Promise<string | null> {
  return readFile(join(repo, path), "utf8").catch(() => null);
}

async function findFiles(dir: string, name: string): Promise<string[]> {
  const out: string[] = [];
  const entries = await readdir(join(repo, dir), { withFileTypes: true }).catch(() => []);
  for (const e of entries) {
    const rel = join(dir, e.name);
    if (e.isDirectory() && !["build", "managed_components", "node_modules"].includes(e.name)) out.push(...(await findFiles(rel, name)));
    else if (e.isFile() && e.name === name) out.push(rel);
  }
  return out;
}

/** Every direct dependency the repository declares, by the identifier its build uses. */
async function declaredDependencies(): Promise<Map<string, string>> {
  const deps = new Map<string, string>();
  for (const pkg of ["package.json", "site/package.json", "tools/art/package.json", "tools/screenshots/package.json"]) {
    const text = await readIf(pkg);
    if (text === null) continue;
    const json = JSON.parse(text) as { dependencies?: Record<string, string>; devDependencies?: Record<string, string> };
    for (const name of Object.keys({ ...json.dependencies, ...json.devDependencies })) deps.set(name, pkg);
  }
  for (const manifest of await findFiles("firmware/ports/esp32", "idf_component.yml")) {
    const text = (await readIf(manifest)) ?? "";
    for (const m of text.matchAll(/^\s+([a-z0-9_.-]+\/[a-z0-9_.-]+)\s*:/gm)) if (m[1]) deps.set(m[1], manifest);
  }
  for (const cmake of ["firmware/cmake/deps.cmake"]) {
    const text = (await readIf(cmake)) ?? "";
    for (const m of text.matchAll(/FetchContent_Declare\(\s*([A-Za-z0-9_.-]+)/g)) if (m[1]) deps.set(m[1], cmake);
  }
  const consoleFiles = await readdir(join(repo, "tools/console")).catch(() => [] as string[]);
  for (const name of consoleFiles.filter((n) => n.endsWith(".py"))) {
    const text = (await readIf(join("tools/console", name))) ?? "";
    if (/^\s*import serial\b/m.test(text)) deps.set("pyserial", `tools/console/${name}`);
  }
  return deps;
}

test("THIRD_PARTY.md lists every declared dependency by its identifier", async () => {
  const doc = ((await readIf("THIRD_PARTY.md")) ?? "").toLowerCase();
  const deps = await declaredDependencies();
  assert.ok(deps.has("esptool-js"), "site/package.json was read");
  assert.ok(deps.has("pyserial"), "tools/console/omb_console.py was read");
  const missing = [...deps].filter(([name]) => !doc.includes(`\`${name.toLowerCase()}\``)).map(([name, from]) => `${name} (from ${from})`);
  assert.deepEqual(missing, [], `add these to THIRD_PARTY.md: ${missing.join(", ")}`);
});

test("every THIRD_PARTY.md row names a license", async () => {
  const doc = (await readIf("THIRD_PARTY.md")) ?? "";
  const rows = doc.split("\n").filter((l) => l.startsWith("| ") && !l.startsWith("|---") && !/^\| (Component|Material) \|/.test(l));
  assert.ok(rows.length >= 30, `only ${rows.length} rows`);
  const licenses = /Apache-2\.0|MIT|Zlib|OFL-1\.1|MPL-2\.0|BSD|WTFPL/;
  for (const row of rows) assert.match(row, licenses, row);
});

test("NOTICE, CONTRIBUTING and the art provenance carry the required text", async () => {
  const notice = (await readIf("NOTICE")) ?? "";
  assert.ok(notice.includes(TRADEMARK));
  assert.ok(notice.includes("Apache License, Version 2.0"));
  // Apache-2.0 §4(d): a derivative work carries the attribution of the work it derives from (OpenMausBot's NOTICE).
  assert.ok(
    notice.includes(
      "The Maus art derives from OpenMausBot, Copyright 2026 Milind Soni and OpenMausBot contributors, licensed under the Apache License, Version 2.0.",
    ),
    "NOTICE must carry OpenMausBot's copyright attribution for the Maus art",
  );
  assert.ok(((await readIf("tools/art/source/README.md")) ?? "").includes(TRADEMARK), "tools/art/source/README.md (P2b) must carry the trademark sentence");
  const contributing = (await readIf("CONTRIBUTING.md")) ?? "";
  assert.match(contributing, /^## Original-work rule$/m);
  assert.ok(contributing.includes("must not copy code, documentation, art or protocol text from other gadget SDKs or device firmware projects"));
});
