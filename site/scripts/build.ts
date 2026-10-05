// SPDX-License-Identifier: Apache-2.0
// Builds the installer into site/dist: app.js (esbuild bundle), the static
// files, licenses.txt for every bundled package, and a placeholder
// firmware/install.json that pages.yml replaces with the latest release's.
import { build } from "esbuild";
import { copyFile, mkdir, readFile, readdir, rm, writeFile } from "node:fs/promises";
import { join } from "node:path";
import { fileURLToPath, pathToFileURL } from "node:url";

const NODE_MODULES = "node_modules/";

/**
 * Every npm package among esbuild's metafile inputs, with the directory it was
 * bundled from. The last `node_modules/` segment names the package, so one that
 * npm could not hoist (node_modules/a/node_modules/b) is `b`, with b's own notice.
 */
export function bundledPackages(inputs: readonly string[]): Array<{ name: string; dir: string }> {
  const found = new Map<string, string>(); // dir → name
  for (const input of inputs) {
    const i = input.lastIndexOf(NODE_MODULES);
    if (i < 0) continue;
    const name = /^((?:@[^/]+\/)?[^/]+)\//.exec(input.slice(i + NODE_MODULES.length))?.[1];
    if (name !== undefined) found.set(input.slice(0, i + NODE_MODULES.length + name.length), name);
  }
  const order = (a: string, b: string): number => (a < b ? -1 : a > b ? 1 : 0);
  return [...found].map(([dir, name]) => ({ name, dir })).sort((a, b) => order(a.name, b.name) || order(a.dir, b.dir));
}

async function main(): Promise<void> {
  const site = fileURLToPath(new URL("..", import.meta.url));
  const dist = join(site, "dist");

  await rm(dist, { recursive: true, force: true });
  await mkdir(join(dist, "firmware"), { recursive: true });

  const result = await build({
    absWorkingDir: site, // metafile inputs are relative to site/, wherever the build is run from
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
  const packages = bundledPackages(Object.keys(result.metafile.inputs));
  const notices: string[] = [];
  for (const { name, dir: rel } of packages) {
    const dir = join(site, rel);
    const pkg = JSON.parse(await readFile(join(dir, "package.json"), "utf8")) as { version: string; license?: string };
    const licenseFile = (await readdir(dir)).find((f) => /^(licen[cs]e|copying)(\.md|\.txt)?$/i.test(f));
    if (licenseFile === undefined) throw new Error(`${name} (${rel}) has no license file to ship`);
    notices.push(`${name} ${pkg.version} (${pkg.license ?? "see below"})\n\n${(await readFile(join(dir, licenseFile), "utf8")).trim()}\n`);
  }
  await writeFile(join(dist, "licenses.txt"), `Third-party software bundled in app.js\n\n${notices.join("\n---\n\n")}`);

  for (const file of ["index.html", "style.css"]) await copyFile(join(site, file), join(dist, file));
  await writeFile(join(dist, "firmware", "install.json"), `${JSON.stringify({ version: null, boards: {} })}\n`);
  console.log(`site: built ${dist} (bundled ${packages.map((p) => p.name).join(", ")})`);
}

if (process.argv[1] !== undefined && import.meta.url === pathToFileURL(process.argv[1]).href) await main();
