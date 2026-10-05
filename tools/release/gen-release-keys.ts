// SPDX-License-Identifier: Apache-2.0
// Writes firmware/core/src/keys_release.c from keys/release-*.pub.b64.
// Omkar runs it once after committing a new release public key (docs/release-keys.md).
//   node tools/release/gen-release-keys.ts [--root .]
import { readFile, readdir, writeFile } from "node:fs/promises";
import { join } from "node:path";
import { parseArgs } from "node:util";
import { fail, runIfMain } from "./cli.ts";
import { keysReleaseC, parseReleasePubFiles } from "./lib.ts";

export async function main(argv: string[]): Promise<number> {
  const { values } = parseArgs({ args: argv, options: { root: { type: "string", default: "." } }, strict: true });
  const keyDir = join(values.root, "keys");
  let pubs: Map<string, string>;
  try {
    // Only the files at the top of keys/: a retired key lives in keys/retired/.
    const names = (await readdir(keyDir, { withFileTypes: true })).filter((e) => e.isFile()).map((e) => e.name);
    pubs = parseReleasePubFiles(
      await Promise.all(names.map(async (name) => ({ name, text: await readFile(join(keyDir, name), "utf8") }))),
    );
  } catch (err) {
    return fail("gen-release-keys", (err as Error).message);
  }
  const keys = [...pubs].map(([id, hex]) => ({ id, pub: new Uint8Array(Buffer.from(hex, "hex")) }));
  const out = join(values.root, "firmware/core/src/keys_release.c");
  await writeFile(out, keysReleaseC(keys));
  console.log(`gen-release-keys: wrote ${out} with ${keys.length === 0 ? "no keys" : keys.map((k) => k.id).join(", ")}`);
  return 0;
}

runIfMain(import.meta.url, main);
