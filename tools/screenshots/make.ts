// SPDX-License-Identifier: Apache-2.0
// Writes the README's images. Gadget screens come from the simulator's
// snapshot goldens (firmware/tests/snapshots), the installer image is a
// headless-Chrome capture of the built site (site/dist). Nothing is drawn by
// hand. Run: npm --prefix tools/screenshots run make [-- --installer]
import { spawn } from "node:child_process";
import { copyFileSync, existsSync, mkdirSync, mkdtempSync, readFileSync, rmSync, statSync, writeFileSync } from "node:fs";
import { createServer, type Server } from "node:http";
import { createRequire } from "node:module";
import { tmpdir } from "node:os";
import { extname, join, normalize, relative, resolve, sep } from "node:path";
import { fileURLToPath, pathToFileURL } from "node:url";
import { parseArgs } from "node:util";

interface Png {
  width: number;
  height: number;
  data: Buffer;
}
const require = createRequire(import.meta.url);
const { PNG } = require("pngjs") as {
  PNG: { new (opts: { width: number; height: number }): Png; sync: { read(b: Buffer): Png; write(p: Png): Buffer } };
};

/** README screen name → amoled-175c golden. Every name must map to a golden that exists; nothing falls back. */
export const SCREENS: readonly (readonly [string, string])[] = [
  ["idle", "idle.png"],
  ["listening", "listening.png"],
  ["thinking", "fx_thinking_working.png"],
  ["speaking", "fx_speaking_1.png"],
  ["ask", "ask_permission.png"],
  ["post", "post_toast.png"],
  // setup_need_code.png is left out: on the round panel it clips its last two lines (reported to P2b).
  ["setup", "setup_pairing_host.png"],
  ["update", "update_receiving.png"],
];

/** The round panel the screens come from, and the boards shown side by side in boards.png (left to right). */
export const ROUND_BOARD = "amoled-175c";
export const PANEL = 466;
export const RADIUS = PANEL / 2;
export const BOARD_ROW: readonly { board: string; round: boolean }[] = [
  { board: "amoled-175c", round: true },
  { board: "lcd-154", round: false },
  { board: "devkit", round: false },
];
export const GAP = 24;
export const INSTALLER_SIZE = { width: 1280, height: 800 } as const;

const repo = fileURLToPath(new URL("../../", import.meta.url));

function readGolden(snapshots: string, board: string, file: string): Png {
  const path = join(snapshots, board, file);
  if (!existsSync(path)) throw new Error(`missing snapshot golden ${board}/${file} (looked in ${path})`);
  return PNG.sync.read(readFileSync(path));
}

/** Clears everything outside the round panel: alpha 0 beyond RADIUS from the centre, a 1 px anti-aliased edge. */
export function maskRound(src: Png): Png {
  if (src.width !== PANEL || src.height !== PANEL) throw new Error(`a round screen must be ${PANEL}x${PANEL}, not ${src.width}x${src.height}`);
  const out = new PNG({ width: src.width, height: src.height });
  src.data.copy(out.data);
  for (let y = 0; y < src.height; y++) {
    for (let x = 0; x < src.width; x++) {
      const d = Math.hypot(x + 0.5 - RADIUS, y + 0.5 - RADIUS);
      const cover = Math.min(1, Math.max(0, RADIUS - d));
      const i = (y * src.width + x) * 4 + 3;
      out.data[i] = Math.round(out.data[i]! * cover);
    }
  }
  return out;
}

/** The images side by side, bottom-aligned, GAP px apart, on a transparent canvas. */
export function row(images: readonly Png[]): Png {
  const width = images.reduce((w, img) => w + img.width, 0) + GAP * Math.max(0, images.length - 1);
  const height = Math.max(...images.map((img) => img.height));
  const out = new PNG({ width, height });
  out.data.fill(0);
  let left = 0;
  for (const img of images) {
    const top = height - img.height;
    for (let y = 0; y < img.height; y++) {
      img.data.copy(out.data, ((top + y) * width + left) * 4, y * img.width * 4, (y + 1) * img.width * 4);
    }
    left += img.width + GAP;
  }
  return out;
}

function write(out: string, name: string, png: Png): void {
  const path = join(out, name);
  writeFileSync(path, PNG.sync.write(png));
  console.log(`${relative(repo, path)} ${png.width}x${png.height} ${statSync(path).size} bytes`);
}

const MIME: Record<string, string> = {
  ".html": "text/html; charset=utf-8",
  ".js": "text/javascript; charset=utf-8",
  ".css": "text/css; charset=utf-8",
  ".json": "application/json",
  ".map": "application/json",
  ".txt": "text/plain; charset=utf-8",
  ".bin": "application/octet-stream",
};

/** Serves site/dist on 127.0.0.1 so the page's module script and same-origin fetches load as on GitHub Pages. */
function serve(root: string): Promise<{ server: Server; url: string }> {
  const base = resolve(root);
  const server = createServer((req, res) => {
    const pathname = decodeURIComponent(new URL(req.url ?? "/", "http://127.0.0.1").pathname);
    let path = normalize(join(base, pathname));
    if (path !== base && !path.startsWith(base + sep)) {
      res.writeHead(403).end();
      return;
    }
    if (existsSync(path) && statSync(path).isDirectory()) path = join(path, "index.html");
    if (!existsSync(path)) {
      res.writeHead(404).end();
      return;
    }
    res.writeHead(200, { "content-type": MIME[extname(path)] ?? "application/octet-stream" });
    res.end(readFileSync(path));
  });
  return new Promise((ok, fail) => {
    server.once("error", fail);
    server.listen(0, "127.0.0.1", () => {
      const addr = server.address();
      if (addr === null || typeof addr === "string") return fail(new Error("no port"));
      ok({ server, url: `http://127.0.0.1:${addr.port}/` });
    });
  });
}

function chromePath(override: string | undefined): string {
  if (override) return override;
  if (process.platform === "darwin") return "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome";
  return "google-chrome";
}

const CHROME_TIMEOUT_MS = 60_000;

/**
 * Runs headless Chrome until it has written `shot`. Chrome on macOS sometimes
 * keeps running after it writes the screenshot, so once the file exists and
 * its size has held still for a second, Chrome is asked to quit (SIGTERM).
 */
function runChrome(chrome: string, args: string[], shot: string): Promise<void> {
  return new Promise((ok, fail) => {
    const child = spawn(chrome, args, { stdio: ["ignore", "ignore", "pipe"] });
    let stderr = "";
    let settled = false;
    let lastSize = -1;
    let stableSince = 0;
    const started = Date.now();
    const finish = (err?: Error) => {
      if (settled) return;
      settled = true;
      clearInterval(timer);
      if (child.exitCode === null && child.signalCode === null) child.kill("SIGTERM");
      if (err) fail(err);
      else ok();
    };
    const timer = setInterval(() => {
      const size = existsSync(shot) ? statSync(shot).size : -1;
      if (size > 0 && size === lastSize) {
        if (Date.now() - stableSince >= 1000) finish();
      } else {
        lastSize = size;
        stableSince = Date.now();
      }
      if (Date.now() - started > CHROME_TIMEOUT_MS) finish(new Error(`Chrome did not write the screenshot within ${CHROME_TIMEOUT_MS / 1000} s`));
    }, 200);
    child.stderr.on("data", (b: Buffer) => (stderr += b.toString()));
    child.once("error", (err) => finish(new Error(`could not start Chrome (${chrome}): ${err.message}`)));
    child.once("exit", (code) => {
      if (code === 0 && existsSync(shot)) finish();
      else if (!settled) finish(new Error(`Chrome exited with ${code} without a screenshot: ${stderr.trim().slice(-400)}`));
    });
  });
}

/** A headless-Chrome capture of the built installer page at 1280x800. */
async function captureInstaller(site: string, out: string, chrome: string): Promise<void> {
  if (!existsSync(join(site, "index.html"))) throw new Error(`${join(site, "index.html")} not found: build the site first (cd site && npm run build)`);
  const { server, url } = await serve(site);
  const profile = mkdtempSync(join(tmpdir(), "omb-shot-chrome-"));
  // Chrome writes into its own temp folder, so an older installer.png in `out` is never mistaken for the new one.
  const shot = join(profile, "installer.png");
  const path = resolve(out, "installer.png");
  try {
    await runChrome(chrome, [
      "--headless=new",
      "--disable-gpu",
      "--hide-scrollbars",
      "--no-first-run",
      "--no-default-browser-check",
      `--user-data-dir=${profile}`,
      `--window-size=${INSTALLER_SIZE.width},${INSTALLER_SIZE.height}`,
      // Lets the page fetch firmware/install.json and render the release line before the capture.
      "--virtual-time-budget=5000",
      `--screenshot=${shot}`,
      url,
    ], shot);
    const png = PNG.sync.read(readFileSync(shot));
    if (png.width !== INSTALLER_SIZE.width || png.height !== INSTALLER_SIZE.height) {
      throw new Error(`the installer capture is ${png.width}x${png.height}, not ${INSTALLER_SIZE.width}x${INSTALLER_SIZE.height}`);
    }
    copyFileSync(shot, path);
  } finally {
    server.close();
    rmSync(profile, { recursive: true, force: true });
  }
  console.log(`${relative(repo, path)} ${INSTALLER_SIZE.width}x${INSTALLER_SIZE.height} ${statSync(path).size} bytes`);
}

export async function main(argv: string[]): Promise<number> {
  const { values } = parseArgs({
    args: argv,
    options: {
      snapshots: { type: "string", default: join(repo, "firmware/tests/snapshots") },
      out: { type: "string", default: join(repo, "docs/images") },
      site: { type: "string", default: join(repo, "site/dist") },
      installer: { type: "boolean", default: false },
      chrome: { type: "string" },
    },
  });
  const snapshots = values.snapshots;
  const out = values.out;
  // Read every golden before writing anything, so a missing one leaves the output untouched.
  const screens = SCREENS.map(([name, file]) => [name, readGolden(snapshots, ROUND_BOARD, file)] as const);
  const idles = BOARD_ROW.map(({ board, round }) => {
    const idle = readGolden(snapshots, board, "idle.png");
    return round ? maskRound(idle) : idle;
  });
  mkdirSync(out, { recursive: true });
  for (const [name, png] of screens) write(out, `screen-${name}.png`, maskRound(png));
  write(out, "boards.png", row(idles));
  if (values.installer) await captureInstaller(values.site, out, chromePath(values.chrome));
  return 0;
}

if (process.argv[1] !== undefined && import.meta.url === pathToFileURL(process.argv[1]).href) {
  main(process.argv.slice(2)).then(
    (code) => {
      process.exitCode = code;
    },
    (err: unknown) => {
      console.error(`make: ${err instanceof Error ? err.message : String(err)}`);
      process.exitCode = 1;
    },
  );
}
