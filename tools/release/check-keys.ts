// SPDX-License-Identifier: Apache-2.0
// Release CI's key-table check (spec §8, contract §2.13): compiles core's key
// tables the way a release build does and requires at least one release key,
// only /^r[0-9]+$/ ids, each equal to keys/release-<id>.pub.b64, and an empty
// test table.
//   node tools/release/check-keys.ts [--root .] [--cc cc]
import { execFileSync } from "node:child_process";
import { mkdtemp, readFile, readdir, rm } from "node:fs/promises";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { fileURLToPath } from "node:url";
import { parseArgs } from "node:util";
import { fail, runIfMain } from "./cli.ts";
import { checkKeyTables, parseReleasePubFiles, type KeyTables } from "./lib.ts";

export async function main(argv: string[]): Promise<number> {
  const { values } = parseArgs({
    args: argv,
    options: { root: { type: "string", default: "." }, cc: { type: "string", default: process.env.CC ?? "cc" } },
    strict: true,
  });
  const root = values.root;
  const tmp = await mkdtemp(join(tmpdir(), "omb-keys-"));
  try {
    const exe = join(tmp, "keytable");
    try {
      execFileSync(
        values.cc,
        [
          "-std=c11",
          "-Wall",
          "-Wextra",
          "-Werror",
          "-I",
          join(root, "firmware/core/include"),
          fileURLToPath(new URL("./keytable.c", import.meta.url)),
          join(root, "firmware/core/src/keys_release.c"),
          join(root, "firmware/core/src/keys_test.c"),
          "-o",
          exe,
        ],
        { stdio: ["ignore", "inherit", "inherit"] },
      );
    } catch {
      return fail("check-keys", "the key tables do not compile");
    }
    const tables = JSON.parse(execFileSync(exe, { encoding: "utf8" })) as KeyTables;
    const keyDir = join(root, "keys");
    // Only the files at the top of keys/: a retired key lives in keys/retired/.
    const names = (await readdir(keyDir, { withFileTypes: true })).filter((e) => e.isFile()).map((e) => e.name);
    const pubs = parseReleasePubFiles(
      await Promise.all(names.map(async (name) => ({ name, text: await readFile(join(keyDir, name), "utf8") }))),
    );
    const errors = checkKeyTables(tables, pubs);
    for (const e of errors) console.error(`check-keys: ${e}`);
    if (errors.length > 0) return 1;
    console.log(`check-keys: release keys ${tables.release.map((k) => k.id).join(", ")}; test table empty`);
    return 0;
  } finally {
    await rm(tmp, { recursive: true, force: true });
  }
}

runIfMain(import.meta.url, main);
