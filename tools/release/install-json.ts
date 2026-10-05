// SPDX-License-Identifier: Apache-2.0
// Writes install.json from collect.ts meta files, without signing. sign.ts
// writes the release's copy; this CLI stages a local build for the installer.
//   node tools/release/install-json.ts --meta <dir> --version <v> --out <file>
import { readFile, readdir, writeFile } from "node:fs/promises";
import { join } from "node:path";
import { parseArgs } from "node:util";
import { fail, runIfMain } from "./cli.ts";
import { buildInstallIndex, type BoardMeta } from "./lib.ts";

export async function readMetas(dir: string): Promise<BoardMeta[]> {
  const names = (await readdir(dir)).filter((n) => n.endsWith(".json")).sort();
  return Promise.all(names.map(async (n) => JSON.parse(await readFile(join(dir, n), "utf8")) as BoardMeta));
}

export async function main(argv: string[]): Promise<number> {
  const { values } = parseArgs({
    args: argv,
    options: { meta: { type: "string" }, version: { type: "string" }, out: { type: "string" } },
    strict: true,
  });
  if (!values.meta || !values.version || !values.out) return fail("install-json", "--meta, --version and --out are required");
  try {
    const index = buildInstallIndex(values.version, await readMetas(values.meta));
    await writeFile(values.out, `${JSON.stringify(index, null, 2)}\n`);
    console.log(`install-json: ${values.out} (${Object.keys(index.boards).join(", ")})`);
    return 0;
  } catch (err) {
    return fail("install-json", (err as Error).message);
  }
}

runIfMain(import.meta.url, main);
