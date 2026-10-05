// SPDX-License-Identifier: Apache-2.0
// Spec §5.10, §6.4 and §11: the docs say what the spec requires them to say.
import assert from "node:assert/strict";
import { readFile, readdir } from "node:fs/promises";
import { test } from "node:test";
import { BOARDS } from "../lib.ts";

const repo = new URL("../../../", import.meta.url);
const read = (path: string) => readFile(new URL(path, repo), "utf8");
const TRADEMARK = "The OpenMausBot name, the MausBot name and the Maus mascot are trademarks of Supamaus Software Private Limited.";
const PAIR_PATH = "MausBot → Settings → Remote access → Pair a gadget";

test("README: headline path, Remote access, Pair a gadget, installer and trademark", async () => {
  const readme = await read("README.md");
  assert.match(readme, /^## Ask your MausBot to flash it$/m);
  assert.ok(readme.includes("Remote access must be on"));
  assert.ok(readme.includes(PAIR_PATH));
  assert.ok(readme.includes("https://aivsomkar.github.io/openmausbot-gadget-sdk/"));
  assert.ok(readme.includes("[AGENTS.md](AGENTS.md)"));
  assert.ok(readme.includes(TRADEMARK));
  for (const board of BOARDS) assert.ok(readme.includes(`\`${board}\``), board);
});

test("AGENTS.md covers spec §5.10", async () => {
  const agents = await read("AGENTS.md");
  for (const needle of [
    "brew install libgcrypt glib pixman sdl2 libslirp dfu-util ninja && brew tap espressif/eim && brew install eim && eim install -i v6.0.3 -t esp32s3",
    "./install.sh esp32s3",
    "idf.py -B build/<board> -D GADGET_BOARD=<board> -D SDKCONFIG=build/<board>/sdkconfig build",
    "flash monitor",
    "tools/check-size.sh <board>",
    "node tools/fake-host/src/main.ts --port 8810 --code 123456",
    "./build/host/ports/sim/gadget-sim --board amoled-175c --host 127.0.0.1:8810 --pair 123456",
    "--headless --script",
    "tools/console/omb_console.py",
    "write-flash < parts.txt",
    "## Add a board",
    "Speaker-rate rule",
    "6291456",
    "Art profile",
    "## Add an action",
    "gadget_action_register(name, description, schema, risk, handler)",
    // P4a refuses these schemas with 400; a maker who registers one has an action no bot can run.
    'MausBot refuses to run an action whose params schema uses `pattern`, `patternProperties` or `"format": "regex"`',
    "use `enum`, `minimum`/`maximum` and `maxLength` instead",
    "GADGET_RISK_CONFIRM",
    "does not authenticate MausBot",
    "Do not register actions whose misuse is unsafe",
    "Plug the board in by the USB-C port labelled USB, not the one labelled UART.",
    "Remote access must be on",
    PAIR_PATH,
  ]) {
    assert.ok(agents.includes(needle), `AGENTS.md is missing: ${needle}`);
  }
  for (const board of BOARDS) assert.ok(agents.includes(`| \`${board}\` |`), board);
});

test("docs/release-keys.md names the environment, the secret and the tools", async () => {
  const doc = await read("docs/release-keys.md");
  for (const needle of ["`release`", "GADGET_RELEASE_KEY_R1", "tools/release/gen-release-keys.ts", "tools/release/check-keys.ts", "tag** rule `v*`", "project-owned"]) {
    assert.ok(doc.includes(needle), needle);
  }
});

test("no doc points at a Devices page that does not exist", async () => {
  for (const path of ["README.md", "AGENTS.md", "CONTRIBUTING.md", "docs/installer-checklist.md", "site/src/copy.ts", "site/index.html"]) {
    assert.equal((await read(path)).includes("Settings → Devices"), false, path);
  }
});

test("README shows every docs/images PNG, each one exists and is a PNG, and every image has alt text", async () => {
  const readme = await read("README.md");
  const shown: string[] = [];
  for (const [tag] of readme.matchAll(/<img\s[^>]*>/g)) {
    const src = /\ssrc="([^"]+)"/.exec(tag)?.[1];
    assert.ok(src, `no src: ${tag}`);
    assert.ok((/\salt="([^"]*)"/.exec(tag)?.[1] ?? "").trim().length > 0, `no alt text: ${tag}`);
    shown.push(src);
  }
  for (const [whole, alt, src] of readme.matchAll(/!\[([^\]]*)\]\(([^)\s]+)/g)) {
    assert.ok((alt ?? "").trim().length > 0, `no alt text: ${whole}`);
    shown.push(src!);
  }
  assert.ok(shown.length > 0, "README shows no images");
  const PNG_SIGNATURE = [0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a];
  for (const src of shown) {
    const bytes = await readFile(new URL(src, repo)).catch(() => null);
    assert.ok(bytes, `README shows ${src}, which does not exist`);
    assert.deepEqual([...bytes.subarray(0, 8)], PNG_SIGNATURE, `${src} is not a PNG`);
  }
  const onDisk = (await readdir(new URL("docs/images/", repo))).filter((f) => f.endsWith(".png")).map((f) => `docs/images/${f}`);
  assert.ok(onDisk.length >= 10, `only ${onDisk.length} images in docs/images`);
  for (const file of onDisk) assert.ok(shown.includes(file), `README does not show ${file}`);
});
