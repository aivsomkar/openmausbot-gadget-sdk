// SPDX-License-Identifier: Apache-2.0
// Builds the installer into site/dist: app.js (esbuild bundle), the static
// files, licenses.txt for every bundled package, and a placeholder
// firmware/install.json that pages.yml replaces with the latest release's.
import { build } from "esbuild";
import { copyFile, mkdir, readFile, readdir, rm, writeFile } from "node:fs/promises";
import { join } from "node:path";
import { fileURLToPath } from "node:url";

const site = fileURLToPath(new URL("..", import.meta.url));
const dist = join(site, "dist");

await rm(dist, { recursive: true, force: true });
await mkdir(join(dist, "firmware"), { recursive: true });

const result = await build({
  entryPoints: [join(site, "src/main.ts")],
  bundle: true,
  format: "esm",
  platform: "browser",
  target: "es2022",
  minify: true,
  sourcemap: true,
  legalComments: "eof",
  outfile: join(dist, "app.js"),
  metafile: true,
  logLevel: "warning",
});

// One notice per bundled npm package (esptool-js and its dependencies, spark-md5).
const packages = new Set<string>();
for (const input of Object.keys(result.metafile.inputs)) {
  const m = /node_modules\/((?:@[^/]+\/)?[^/]+)\//.exec(input);
  if (m?.[1] !== undefined) packages.add(m[1]);
}
const notices: string[] = [];
for (const name of [...packages].sort()) {
  const dir = join(site, "node_modules", name);
  const pkg = JSON.parse(await readFile(join(dir, "package.json"), "utf8")) as { version: string; license?: string };
  const licenseFile = (await readdir(dir)).find((f) => /^(licen[cs]e|copying)(\.md|\.txt)?$/i.test(f));
  if (licenseFile === undefined) throw new Error(`${name} has no license file to ship`);
  notices.push(`${name} ${pkg.version} (${pkg.license ?? "see below"})\n\n${(await readFile(join(dir, licenseFile), "utf8")).trim()}\n`);
}
await writeFile(join(dist, "licenses.txt"), `Third-party software bundled in app.js\n\n${notices.join("\n---\n\n")}`);

for (const file of ["index.html", "style.css"]) await copyFile(join(site, file), join(dist, file));
await writeFile(join(dist, "firmware", "install.json"), `${JSON.stringify({ version: null, boards: {} })}\n`);
console.log(`site: built ${dist} (bundled ${[...packages].sort().join(", ")})`);
