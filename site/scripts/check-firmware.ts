// SPDX-License-Identifier: Apache-2.0
// pages.yml runs this after copying the latest release into dist/firmware:
// install.json must parse, every part and full image it names must exist,
// and, when the release's SHA256SUMS was copied too, every file it lists that
// is present must match it, and every installer file must be listed.
//   node site/scripts/check-firmware.ts site/dist/firmware
import { createHash } from "node:crypto";
import { readFile, stat } from "node:fs/promises";
import { join } from "node:path";
import { pathToFileURL } from "node:url";
import { parseInstallIndex } from "../src/install.ts";

async function readIfExists(path: string): Promise<string | null> {
  return readFile(path, "utf8").catch(() => null);
}

export async function checkFirmwareDir(dir: string): Promise<string> {
  const index = parseInstallIndex(JSON.parse(await readFile(join(dir, "install.json"), "utf8")));
  if (index.version === null) return "no firmware published yet (placeholder install.json)";
  const needed = new Set<string>(["install.json"]);
  for (const [board, entry] of Object.entries(index.boards)) {
    for (const file of [...entry.parts.map((p) => p.path), entry.full]) {
      const info = await stat(join(dir, file)).catch(() => null);
      if (info === null || !info.isFile() || info.size === 0) throw new Error(`${board}: ${file} is missing or empty`);
      needed.add(file);
    }
  }
  const sums = await readIfExists(join(dir, "SHA256SUMS"));
  if (sums !== null) {
    const listed = new Map<string, string>();
    for (const line of sums.split("\n").filter(Boolean)) {
      const m = /^([0-9a-f]{64}) {2}(\S+)$/.exec(line);
      if (!m?.[1] || !m[2]) throw new Error(`SHA256SUMS: bad line ${JSON.stringify(line)}`);
      listed.set(m[2], m[1]);
    }
    for (const file of needed) {
      const want = listed.get(file);
      if (want === undefined) throw new Error(`SHA256SUMS does not list ${file}`);
      const got = createHash("sha256").update(await readFile(join(dir, file))).digest("hex");
      if (got !== want) throw new Error(`${file} does not match SHA256SUMS`);
    }
  }
  return `firmware ${index.version} for ${Object.keys(index.boards).join(", ")}${sums === null ? "" : ", checksums match"}`;
}

if (process.argv[1] !== undefined && import.meta.url === pathToFileURL(process.argv[1]).href) {
  const dir = process.argv[2];
  if (dir === undefined) {
    console.error("usage: node site/scripts/check-firmware.ts <dist/firmware>");
    process.exitCode = 2;
  } else {
    checkFirmwareDir(dir).then(
      (summary) => console.log(`check-firmware: ${summary}`),
      (err: unknown) => {
        console.error(`check-firmware: ${(err as Error).message}`);
        process.exitCode = 1;
      },
    );
  }
}
