# P2d — Browser installer, release CI and docs Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Ship the SDK's browser installer (`site/`), the signed release pipeline (`release.yml`, `pages.yml`, `tools/release/`), the console helper for agents, and the repository's user-facing docs and licensing files (`README.md`, `AGENTS.md`, `CONTRIBUTING.md`, `NOTICE`, `THIRD_PARTY.md`, release-key docs).

**Architecture:** The installer is a static TypeScript page bundled with esbuild. Its logic lives in small pure modules (`@omb` console parsing, `install.json`, flashing through an injected esptool-js loader, the reset-to-app sequence, the setup state machine), all unit-tested in Node against fakes; `main.ts` only wires them to the DOM and Web Serial. Release CI builds the four boards in `espressif/idf:v6.0.3`, checks and renames the outputs with Node tools (`tools/release/`), signs app images with Node's `crypto` in the protected `release` environment, publishes the GitHub release, and dispatches `pages.yml`, which copies the latest release's firmware into the site so the page loads it same-origin.

**Tech Stack:** TypeScript run directly by Node ≥ 22.18 (type stripping; `node:test`), esptool-js 0.7.0, esbuild 0.28.2, spark-md5 3.0.2, TypeScript 5.9.3 (type check only), Web Serial, GitHub Actions (`espressif/idf:v6.0.3` container, environments, Pages), `gh` CLI, two small C11 test programs (key-table check; the firmware's console splitter), Python 3.10+ with pyserial (console helper).

**Spec:** `/Users/omkar/Desktop/openmaus/openmausbot-gadget-sdk/docs/specs/2026-10-04-openmausbot-gadget-design.md` (v1.1; §5.8, §5.10, §8, §11, plus §4.8 signed text and §5.6 console) and the binding interface contract `/Users/omkar/Desktop/openmaus/openmausbot-gadget-sdk/docs/plans/00-interfaces.md` (§1.4–§1.7, §2.11, §2.13, §2.15, §2.17, §4.1–§4.3, §4.5, §4.8, §5). The plan-research amendments (`AMENDMENTS.md`, A1, A2, A36–A38) are folded into spec v1.1.

## Global Constraints

- **Original-work rule (spec §11):** write everything yourself from vendor and primary sources (Espressif, Waveshare, LVGL, SDL, Mbed TLS, npm docs, RFCs). Never open, copy, quote, cite or link third-party gadget SDKs or voice-assistant firmware projects. Nothing in this branch may name them.
- **Repository and branch:** `/Users/omkar/Desktop/openmaus/openmausbot-gadget-sdk`, branch `p2d-installer` created from `p2c-esp32` (contract §1.3). No pushes, no pull requests, no tags, no releases: publishing belongs to Omkar. This plan never touches the OpenMausBot checkout at `/Users/omkar/Desktop/openmaus/OpenGrokBot`.
- **Node:** SDK tools need Node ≥ 22.18 (the Mac's default Node 22.22.3 is fine); CI uses Node 24. TypeScript is erasable syntax only (no `enum`, no parameter properties, no namespaces) and relative imports end in `.ts`.
- **Pinned installer dependencies (contract §1.4):** `esptool-js` **0.7.0**, `esbuild` **0.28.2**, `spark-md5` **3.0.2**, `@types/w3c-web-serial` **1.0.8**. P2d-private additions: `typescript` 5.9.3, `@types/node` 24.19.1, `@types/spark-md5` 3.0.5. Every SDK npm install in CI is `npm ci`.
- **Toolchain:** ESP-IDF **v6.0.3**, Docker `espressif/idf:v6.0.3`. Board build command: `idf.py -B build/<board> -D GADGET_BOARD=<board> -D SDKCONFIG=build/<board>/sdkconfig [-D PROJECT_VER=<version>] build`, run from `firmware/ports/esp32`.
- **Boards:** `amoled-175c`, `amoled-175`, `lcd-154`, `devkit`. All use `partitions/16mb.csv`, unchanged across releases (spec §5.3); OTA slot 6291456 bytes. Installer offsets always come from each board's `flasher_args.json`, never from constants. Release CI refuses a build whose `partition-table.bin` differs from `16mb.csv` or whose flashed parts reach into `nvs` or `phy_init`.
- **GitHub Actions versions:** exact versions as in P1's `ci.yml` (`actions/checkout@v7.0.1`, `actions/setup-node@v7.0.0`, `actions/upload-artifact@v7.0.1`, `actions/download-artifact@v8.0.1`, `actions/configure-pages@v6.0.0`, `actions/upload-pages-artifact@v5.0.0`, `actions/deploy-pages@v5.0.1`); the `release` job, which holds the signing key, pins full commit SHAs.
- **Commits:** every `git commit` ends with the trailer `-m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"`, as in P1's plan.
- **URLs:** installer `https://aivsomkar.github.io/openmausbot-gadget-sdk/`; release asset base `https://github.com/aivsomkar/openmausbot-gadget-sdk/releases/download/`; manifest `…/releases/latest/download/manifest.json`.
- **`install.json`** is exactly `{"version": "<version>"|null, "boards": {"<board>": {"parts": [{"path", "offset"}], "full"}}}` (contract §4.2); before the first release it is `{"version": null, "boards": {}}`.
- **Release assets per board** (contract §4.3): `openmausbot-gadget-<b>-<v>.bin`, `-bootloader.bin`, `-partition-table.bin`, `-ota-data-initial.bin`, `-full.bin`; plus `manifest.json`, `install.json`, `SHA256SUMS` (GNU format, sorted by name, over every other asset).
- **Signed firmware text (spec §4.8):** `openmausbot-gadget/1\nfirmware\n<board>\n<version>\n<size>\n<sha256 lowercase hex>`, DER ECDSA-P256-SHA256, base64. Release key ids match `/^r[0-9]+$/`; the private key lives only in the GitHub Actions environment `release` as secret `GADGET_RELEASE_KEY_R1`. The test key `t1` never appears in a release image.
- **Release rules (spec §8):** a `v*` tag triggers `release.yml`; a version ending in `-dev` fails; tags containing `-` are prereleases (`--prerelease --latest=false`); the workflow ends with `gh workflow run pages.yml --ref main`.
- **Exact copy:** "MausBot → Settings → Remote access → Pair a gadget"; "Remote access must be on"; "Press RST or unplug and replug the board"; "Plug the board in by the USB-C port labelled USB, not the one labelled UART."; and, in README, NOTICE and the installer footer, exactly "The OpenMausBot name, the MausBot name and the Maus mascot are trademarks of Supamaus Software Private Limited."
- **Installer behaviour (spec §5.8, contract §4.8):** gate on `'serial' in navigator`; flash the separate `parts` (never `full`) with `flashMode/flashFreq/flashSize: "keep"`, and `eraseAll: true` only for "Erase everything"; `resetToApp()` = `writeReg(0x6000812C, 0, 1)`, `setDTR(false)`, `setRTS(true)`, 100 ms, `setRTS(false)`, `transport.disconnect()`, reopen raw Web Serial at 115200 with DTR and RTS false, re-acquire USB 0x303A/0x1001; no app output for ~5 s → the RST prompt; console text goes straight to `port.writable`, never `Transport.write`; after the app answers: `log off`, `scan`, then `pair <code>`, `wifi "<ssid>" "<password>"`, `host auto` (or `host <address>`) in that order, then poll `status` until `"pair":"paired"`. A `pair: "error"` or `wifi: "failed"` left over from the previous attempt is not a result: it counts only after the gadget shows progress on that side or after three status replies. `device_limit` keeps polling (spec §4.3: the gadget retries for 120 s).
- **`@omb` lines (contract §2.11):** read only lines that start `@omb ` after stripping ANSI colour codes; ignore unknown ops and unknown fields.
- **Contract precedence:** names, types, paths and formats in `00-interfaces.md` win; anything added here is private to P2d (see "Contract notes" at the end).

## Review Focus

These are the inputs most likely to bite a person using the installer or the release pipeline that the spec implies but does not spell out. Each has its test in the owning task.

1. **Wi-Fi names and passwords with spaces, quotes, backslashes, tabs, accents or line breaks.** People expect any network to work. The installer and the console helper must quote them exactly as the firmware's own `gadget_console_split` reads them back, and refuse line breaks instead of sending a broken command. → Task 5, tests "quoteArg round-trips awkward SSIDs and passwords through the firmware's splitter" (a JavaScript copy) and "quoteArg output splits back exactly with the firmware's gadget_console_split" (P2a's real `console.c`, compiled); Task 11, test "quote_arg output splits back exactly with the firmware's splitter".
2. **Console output cut mid-line across USB packets, wrapped in ESP-IDF log colours, or interleaved with log lines; a half-written `@omb` line; a multi-byte UTF-8 character (an accented SSID in a `scan` line) cut between two packets.** The page must assemble whole lines across chunks (including a CR at the end of one chunk and LF at the start of the next), decode UTF-8 across chunk boundaries, and treat junk as "not an `@omb` line", never as a crash. → Task 5, tests "the line splitter handles…" and "parseOmbLine strips ANSI…"; Task 8, tests "SerialConsole assembles lines across chunks…" and "a UTF-8 character cut between USB packets still decodes".
3. **The USB device drops and re-enumerates after the reset, or another program (a serial monitor, another tab) holds the port.** People expect the installer to find the board again by itself, or to say what is in the way. Chrome rejects `open()` with the same "Failed to open serial port." in both cases, so the page tells them apart by whether a matching port is still present, not by the message. → Task 8, tests "after the USB device re-enumerates, the new 0x303A/0x1001 port is used" and "a present port that refuses gives port_busy; a vanished board gives port_lost (same open() error)". Another gadget that is plugged in is never opened in its place: tests "a second gadget already plugged in is never opened after the reset" and "a busy board never falls back to another gadget" (Deviations recorded during the build, item 1).
4. **Reinstalling a gadget that is already paired, or retrying after a wrong code or password.** People expect a reinstall to keep the pairing: the page must never write the merged `-full.bin` (it erases NVS at 0x9000), release CI must refuse a partition table that moves NVS, and the page must recognise "still paired" instead of asking for a new code. A retry must not be judged by the previous attempt's leftover error. → Task 2, tests "collect refuses a table that differs from 16mb.csv" and "collect refuses a part that overlaps nvs"; Task 6, test "the plan never includes the merged -full.bin"; Task 9, tests "a reflashed gadget that kept its pairing is recognised", "a retry after bad_code ignores the stale error status" and "a retry after wifi failed ignores the stale failure".
5. **The wrong board: a plain ESP32 or any board on a USB-to-UART bridge, another chip that shares 0x303A/0x1001, or the 8 MB ESP32-S3-DevKitC-1-N8R8.** People expect to be told before anything is written, not to see a bricked-looking board. The port chooser filters on 0x303A/0x1001, so a plain ESP32 or a bridge port never appears in it, and the hint under the button says that only ESP32-S3 boards on their native USB port show up. Another chip on 0x303A/0x1001 gets `wrong_chip`, and an 8 MB S3 gets `flash_too_small`, both before writing. → Task 9, test "the port hint says only ESP32-S3 boards on native USB show up"; Task 7, tests "a board that is not an ESP32-S3 is refused before writing" and "an 8 MB board is refused; an unknown flash size is allowed".

---

## File Structure

| Path | Responsibility | Task |
|---|---|---|
| `tools/release/lib.ts` | Pure release helpers: tag parsing, asset names, manifest URLs, `install.json` entry from `flasher_args.json`, app-descriptor reader, SHA256SUMS text, key-table checks, `keys_release.c` text | 1 |
| `tools/release/cli.ts` | `runIfMain()` and `fail()` for the CLIs | 1 |
| `tools/release/test/helpers.ts` | Fake ESP-IDF build folders (with a real-format `partition-table.bin`), test keys | 1 |
| `tools/release/partitions.ts` | Parses `16mb.csv` and the built `partition-table.bin`; the NVS-safety check | 2 |
| `tools/release/collect.ts` | Checks one board's build (version, project, size, keys, partition table) and copies it into release assets plus a meta file | 2 |
| `tools/release/install-json.ts` | Writes `install.json` from meta files (local staging; shared reader for `sign.ts`) | 2 |
| `tools/release/sign.ts` | Signs app images, writes `manifest.json`, `install.json`, `SHA256SUMS` | 3 |
| `tools/release/keytable.c`, `check-keys.ts`, `gen-release-keys.ts` | Release key-table check and generator | 4 |
| `keys/README.md`, `docs/release-keys.md` | What the key files are; Omkar's key setup, release and rotation steps | 4 |
| `tools/release/tsconfig.json` | Type check for `tools/release` (uses the site's compiler) | 5 |
| `site/package.json`, `package-lock.json`, `tsconfig.json` | Installer package, pinned dependencies, strict type check | 5 |
| `site/src/console.ts` | `@omb` parser, line splitter, argument quoting, setup commands, input validation (contract §4.8 exports) | 5 |
| `site/test/split-argv.c`, `site/test/awkward-values.json` | Test harness around the firmware's own `gadget_console_split`; the awkward SSIDs and passwords both languages test | 5 |
| `site/src/install.ts` | `install.json` parser, flash plan, same-origin fetches (contract §4.8 exports) | 6 |
| `site/src/errors.ts`, `site/src/flash.ts` | `InstallerError`; flashing through an injected esptool-js loader | 7 |
| `site/src/reset.ts`, `site/src/serial-console.ts` | `resetToApp()`, console port (re)acquisition; raw Web Serial console session | 8 |
| `site/src/setup.ts`, `site/src/copy.ts` | App detection and the RST prompt loop, Wi-Fi scan, pairing state machine; every sentence the page shows | 9 |
| `site/src/boards.ts`, `site/src/page.ts`, `site/src/main.ts`, `site/index.html`, `site/style.css` | Board list, the Web Serial gate and release line, DOM wiring | 10 |
| `site/scripts/build.ts`, `site/scripts/check-firmware.ts` | esbuild bundle + licenses + placeholder; Pages-time firmware folder check | 10 |
| `site/test/*.ts` | Node tests and fakes (esptool-js mock, Web Serial port, scripted gadget) | 5–10 |
| `tools/console/omb_console.py`, `test_omb_console.py` | Non-interactive console helper for agents | 11 |
| `.github/workflows/release.yml`, `pages.yml`; `ci.yml` (append job `site`) | Release CI, Pages deploy, PR CI for this plan's code | 12 |
| `NOTICE`, `THIRD_PARTY.md`, `CONTRIBUTING.md`, `tools/release/test/licensing.test.ts` | Licensing and the original-work rule | 13 |
| `README.md`, `AGENTS.md`, `docs/installer-checklist.md`, `tools/release/test/docs.test.ts` | User docs, agent docs, on-device checklist | 14 |
| `tools/screenshots/package.json`, `make.ts`, `test/make.test.ts`; `docs/images/*.png` | README images from the simulator's goldens and a headless-Chrome capture of the installer; the generated PNGs, committed | 15 |

## Scope map

**Spec requirements in this plan:**

| Spec | Requirement | Task |
|---|---|---|
| §5.8 | Static page on GitHub Pages at the pinned URL | 10, 12 |
| §5.8 | Needs Web Serial; checks `'serial' in navigator` | 10 (`page.ts` `supportsWebSerial`, tested), 14 (checklist item 18) |
| §5.8 | Pages workflow downloads the latest release's assets server-side; page fetches same-origin `firmware/install.json` and parts | 6 (`loadInstallIndex`, `fetchPart`), 12 (`pages.yml`), 10 (`check-firmware.ts`) |
| §5.8 | `install.json` format; `offset` from `flasher_args.json`; placeholder before the first release, and the page says so | 2 (producer), 6 (parser), 10 (placeholder, `releaseLine`, tested) |
| §5.3, A36 | One 16 MB partition table for every board, unchanged across releases, so NVS survives an installer reinstall | 2 (`partitions.ts`: the built table must equal `16mb.csv`, OTA slots 6291456, no flashed part in `nvs` or `phy_init`) |
| §5.8 | esptool-js as a build-time dependency | 5 |
| §5.8 step 1 | Pick a board | 10 |
| §5.8 step 2 | Flash the separate parts at their offsets; NVS survives; explicit "Erase everything" | 6, 7, 10 |
| §5.8 step 3 | `resetToApp()` exact sequence; handle 0x303A/0x1001 disconnect/reconnect; no app output in ~5 s → press RST or replug | 7 (watchdogs off before writing), 8, 9 (`detectApp`, `waitForApp`), 10 |
| §5.8 step 4 | `log off`, then `scan`; person picks a network from the gadget's list and types the password | 9 (`scanNetworks`), 10 |
| §5.8 step 5 | Pair a gadget path, Remote access must be on, Windows Public-network hint | 9 (`copy.ts`), 10 |
| §5.8 step 6 | `pair`, `wifi`, `host auto` in that order; poll `status` until paired; empty or several `hosts` → ask for the address or a pick, send `host <address>` | 5 (`setupCommands`), 9 (`pairAndWait`), 10 (keeps a typed address for the next code), 11 (exit 3) |
| §4.3 | `bad_code` leaves `pair` = `error` until the next `pair`; `device_limit` keeps the window open while the gadget retries for 120 s | 9 (`pairAndWait` stale-status guard, `device_limit` notice), 11 (the same in `omb_console.py`) |
| §5.8 | Merged `-full.bin` for command-line recovery only | 2 (asset), 14 (AGENTS.md) |
| §8 | `v*` tag runs `release.yml`; builds every board on `espressif/idf:v6.0.3` with its own `SDKCONFIG` and `-D PROJECT_VER`; fails on `-dev` | 12, 1 (`parseTag`), 2 (embedded version check) |
| §8 | Separate signing job in environment `release` with required reviewers; never runs the IDF build or component manager | 12, 3, 4 (docs) |
| §8 | Publishes per board app, bootloader, partition table, `ota_data_initial`, `-full.bin`; plus `manifest.json`, `install.json`, `SHA256SUMS` | 2, 3, 12 |
| §8 | Tags with `-` are prereleases | 12 |
| §8 | Ends with `gh workflow run pages.yml --ref main` | 12 |
| §8 | `manifest.json` with absolute release URLs | 1, 3 |
| §8 | Release key only in the `release` environment; public half compiled into the firmware; test key only in simulator/test builds; release CI asserts only `r*` ids | 4, 2 (t1 bytes absent, release key present), 12 (sdkconfig line) |
| §5.10 | `AGENTS.md`: ESP-IDF via EIM or `install.sh`; build and flash each board; serial log; simulator and fake host; add a board (OTA slot size, speaker-rate rule, art profile); add an action within the `hello` limits, with no `pattern`, `patternProperties` or `"format": "regex"` in its params schema (P4a's Task 10 refuses those with 400, spec §7); warning about unsafe actions | 14 (+ 11 for the non-interactive console) |
| §5.10 | README headline "ask your MausBot to flash it"; Remote access must be on; pairing at Pair a gadget | 14 |
| §11 | `THIRD_PARTY.md` lists every dependency with its license (including pyserial for the console helper); `NOTICE` with OpenMausBot's Apache-2.0 attribution for the art; trademark sentence; contributors' original-work rule | 13 |
| §11 | Before the SDK is published, Omkar confirms the expression geometry is project-owned (pre-publish check) | 4 (`docs/release-keys.md` §4 step 0), 14 (checklist item 20) |
| §3 ownership | Sub-project 2 owns `release.yml`, `pages.yml`, the `install.json` and `manifest.json` formats, and the CI check of the release key table | 1–4, 12 |
| §10 | Hardware checklist item "installer reflash keeping the pairing" | 14 (`docs/installer-checklist.md`) |

**Owned by other plans (not built here):** the firmware console commands and `@omb` lines (P2a), the board directories, `sdkconfig.defaults` with `# CONFIG_GADGET_TEST_KEYS is not set`, `tools/check-size.sh`, `dependencies.lock.<board>` and the `esp32` CI jobs (P2c), `tools/art/source/README.md` with the trademark sentence (P2b; Task 13 only checks it), `keys/test-t1.*`, `protocol/lib/*` and the fake host (P1), the "Pair a gadget" panel with the LAN address and Public-network hint (P3a), and everything about checking and installing updates in MausBot: `releases.ts`, `RELEASE_KEYS`, the Update button and `tools/release/dev-release.ts` (P4b).

## Verified while writing this plan

Run on this Mac (macOS arm64, Node 22.22.3, Apple clang 21, OpenSSL 3.6.3) in `/private/tmp`, with stand-ins for P1's `protocol/lib` functions and P2a's key tables written from the contract's pinned signatures:

- **Every file in this plan was written and run as given.** After the review revision, the plan was replayed again, task by task, from its own text: every file was extracted in task order into a scratch repository holding stand-ins for P1's `protocol/lib`, P2a's key tables, the contract's `gadget_types.h`/`gadget_util.h`/`gadget_console.h`, P2a's `console.c` (from P2a's plan), P2c's `16mb.csv` and P2b's provenance README. Each task's tests were then run at its boundary. Release tools: 13 → 22 → 29 → 33, then 36 → 40 after Tasks 13–14, type check clean. Site: 9 → 15 → 24 → 33 → 52 → 59, with `tsc` clean under `erasableSyntaxOnly` and `noUncheckedIndexedAccess`, and the firmware round-trip test compiled, not skipped. Python helper: 13 tests. `npm run build` bundles esptool-js, pako, atob-lite and spark-md5 with `licenses.txt`.
- **Review fixes checked beyond the unit tests:** `partitionTableBin()` is byte-identical to ESP-IDF v6.0.3's `gen_esp32part.py --flash-size 16MB` output for P2c's `16mb.csv`, and `partitionProblems()` finds nothing wrong with that real binary. `split-argv.c` compiles with `-std=c11 -Wall -Wextra -Werror` against P2a's `console.c`. The two retry tests fail if the pair and Wi-Fi sides share one progress flag (a leftover `wifi: "connected", pair: "error"` already looks like progress). The AGENTS.md download and flash blocks, extracted from the replayed file, pass all four parts to a fake `esptool` under both `zsh -f` and bash, while the old forms fail in zsh exactly as reported. `omb_console.py` was run against a real pseudo-terminal through pyserial 3.5: the lines went (DTR, RTS) (1,1) → (1,0) → (0,0), and `pair` was sent only after the booting fake gadget answered `status`. `tail -f /dev/null | npm run serve &` serves `/`, `app.js` and `firmware/install.json`, and `pkill -f 'esbuild --servedir=dist'` stops it. `git check-ignore site/node_modules site/dist/` prints both paths with P1's `.gitignore`. The release job's three action SHAs were resolved with `git ls-remote` and `gh api repos/actions/<name>/commits/<tag>`. AGENTS.md's no-regex bullet (P4a's 400 for `pattern`, `patternProperties` and `"format": "regex"`) was extracted with `docs.test.ts` from this plan's text: the "AGENTS.md covers spec §5.10" test passes, and fails with the bullet removed.
- **The bundled page** (`dist/app.js` against `index.html`, in jsdom with a stubbed `navigator.serial`) renders the release version, the four boards, the devkit hint, the Pair a gadget copy, the "no firmware yet" state and the unsupported-browser state.
- **ESP-IDF v6.0.3 sources** (fetched from Espressif's repository): `flasher_args.json.in` and `esptool_py_flash_target_image` (the `flash_files` map is `"<offset>": "<build-relative file>"`), `idf.py merge-bin` writes into the build directory, `esp_app_desc_t` sits at offset 32 (24-byte image header + 8-byte segment header) with magic `0xABCD5432`, version at 48 and project name at 80.
- **esptool-js 0.7.0** type definitions and code: `ESPLoader({transport, baudrate, romBaudrate, terminal})`, `main(mode)`, `detectFlashSize()`, `writeFlash(FlashOptions)` with `Uint8Array` data, `writeReg(addr, value, mask)`, `readReg(addr, timeout?): Promise<number>`, `chip.CHIP_NAME`; `Transport.connect()` calls `device.open()`, so an already-open port makes it throw; its `HardReset` only lowers RTS, which is why the page has its own `resetToApp()`; it never touches the S3's watchdogs, which is why `flashBoard` does.
- **The key procedure** in `docs/release-keys.md` (OpenSSL 3.6.3; LibreSSL 3.3.6 also produces the same files) → `gen-release-keys.ts` → `check-keys.ts` passes → `sign.ts` → an independent `openssl dgst -sha256 -verify` of a manifest signature prints `Verified OK`, and `shasum -a 256 -c SHA256SUMS` passes.
- **C:** `keytable.c` with the contract's `gadget_types.h`/`gadget_ota.h` compiles with `-std=c11 -Wall -Wextra -Werror`; the AGENTS.md action example compiles against the contract's `gadget_actions.h` and cJSON 1.7.19.
- **Workflows:** all three YAML files parse (js-yaml 4.1.0); the Pages copy step, the publish step and the version guard were run against a fake `gh` (release / no release / API failure; release vs prerelease; five tags). macOS `sha256sum` is not GNU, so the Pages checksum check lives in `check-firmware.ts` instead of `sha256sum --ignore-missing`.
- **CLIs:** `esptool` 5.4.0 (`write-flash <address> <file>…`, `--chip`, `--port`), `gh` 2.89 (`release create --verify-tag --latest=false`, `release download --pattern`), `esbuild --servedir=dist --serve=127.0.0.1:8080`. The console helper ran against a real pseudo-terminal through pyserial 3.5.
- **Licenses:** npm `license` fields for every npm package; Espressif's component registry plus the license files of the five managed driver components (all Apache-2.0); `espressif/cjson` MIT.
- **Not verified here:** anything on GitHub (container build, environment approval, release creation, Pages deploy), anything on a real board or real browser (Chrome's `getPorts()` behaviour for a held port against a vanished one, the watchdog registers, how P2a's firmware moves `status` after a new `pair` or `wifi`), and the PowerShell block in AGENTS.md (no Windows machine here). The GitHub Actions versions come from research R7 (dated 2026-10-04), and the tags were confirmed to exist. Task 16 lists what remains for hardware.

---

### Task 1: Release library

**Files:**
- Create: `tools/release/lib.ts`
- Create: `tools/release/cli.ts`
- Create: `tools/release/test/helpers.ts`
- Test: `tools/release/test/lib.test.ts`

**Interfaces:**
- Consumes (P1, contract §4.4.1): `firmwareText(board: string, version: string, size: number, sha256Hex: string): string` from `protocol/lib/identity.ts`; `verifyP256(pub65: Uint8Array, text: string, der: Uint8Array): boolean` from `protocol/lib/verify.ts`.
- Produces (`tools/release/lib.ts`): `BOARDS` (`["amoled-175c","amoled-175","lcd-154","devkit"]`), `RELEASE_REPO`, `OTA_SLOT_SIZE` (6291456), `PROJECT_NAME`, `VERSION_RE`, `BOARD_RE`, `RELEASE_KEY_ID_RE`, `BUILD_FILES`; types `AssetNames`, `InstallPart`, `InstallBoard`, `InstallIndexOut`, `ManifestBoard`, `Manifest`, `BoardMeta {board, version, install}`, `KeyEntry {id: string|null, pub: hex}`, `KeyTables {release, test}`, `AppDescriptor {version, projectName}`; functions `parseTag(tag): {version, prerelease}`, `assetNames(board, version): AssetNames`, `releaseDownloadBase(repo): string`, `manifestUrl(repo, board, version): string`, `installEntryFromFlasherArgs(json, board, version): InstallBoard`, `buildInstallIndex(version, metas): InstallIndexOut`, `readAppDescriptor(image): AppDescriptor`, `indexOfBytes(haystack, needle): number`, `sha256Hex(bytes): string`, `sha256sumsText(files): string`, `checkKeyTables(tables, releasePubFiles: Map<id, hex>): string[]`, `keysReleaseC(keys): string`, `parseReleasePubFiles(files): Map<id, hex>`.
- Produces (`tools/release/cli.ts`): `runIfMain(metaUrl, main: (argv) => Promise<number>)`, `fail(tool, message): 1`.
- Produces (`tools/release/test/helpers.ts`): `T1_PUB_B64`, `FLASHER_ARGS`, `PARTITIONS_CSV`, `PARTITION_ROWS`, `interface PartitionRow {name, type, subtype, offset, size, flags?}`, `partitionTableBin(rows?): Uint8Array` (byte-identical to ESP-IDF v6.0.3's `gen_esp32part.py` output), `writePartitionsCsv(dir): Promise<string>`, `tempDir()`, `fakeAppImage(version, embed?, size?, project?)`, `makeBuildDir(dir, {version, embed?, appSize?, flasherArgs?, project?, partitions?, bootloaderSize?})`, `makeReleaseKey(id): {id, pem, pub, pubB64}`, `makeKeysDir(dir, keys)`.

- [ ] **Step 1: Create the branch and check what earlier plans left**

```bash
cd /Users/omkar/Desktop/openmaus/openmausbot-gadget-sdk
git status --short
git checkout -b p2d-installer p2c-esp32
ls protocol/lib/identity.ts protocol/lib/verify.ts firmware/core/include/gadget_ota.h \
   firmware/core/src/keys_release.c firmware/core/src/keys_test.c keys/test-t1.pub.b64 \
   firmware/ports/esp32/tools/check-size.sh .github/workflows/ci.yml README.md \
   tools/art/source/README.md firmware/core/include/gadget_actions.h firmware/ports/esp32/partitions/16mb.csv \
   firmware/core/include/gadget_console.h firmware/core/src/console.c
node --version
```

Expected: `git status --short` prints nothing (you start on `p2c-esp32`, where P2c's plan finished); the branch switches; `ls` lists all fourteen paths; `node --version` prints v22.18 or newer. If a path is missing, stop: an earlier plan (P1, P2a, P2b or P2c) is not finished on this branch. Later tasks read these: Task 2 checks builds against `16mb.csv`, Task 5 compiles P2a's `console.c`, Task 13 checks P2b's `tools/art/source/README.md`, and AGENTS.md's action example uses `gadget_actions.h`.

- [ ] **Step 2: Write the test helpers**

`tools/release/test/helpers.ts`:

```ts
// SPDX-License-Identifier: Apache-2.0
// Test fixtures for tools/release: fake ESP-IDF build folders and keys.
import { createHash, generateKeyPairSync } from "node:crypto";
import { mkdir, mkdtemp, writeFile } from "node:fs/promises";
import { tmpdir } from "node:os";
import { join } from "node:path";

export const T1_PUB_B64 = "BIUTH1UeVJw2VXn0Ehdhd8apNOHmB6VvjV512SxIbCCXfUhvlLRTuOTLHtrsVAbIx06JhMoOMbC3ic73hZpxMwg=";

/** firmware/ports/esp32/partitions/16mb.csv as spec §5.3 and P2c write it. */
export const PARTITIONS_CSV = [
  "# Name,   Type, SubType,  Offset,   Size,     Flags",
  "nvs,      data, nvs,      0x9000,   0x6000,",
  "otadata,  data, ota,      0xf000,   0x2000,",
  "phy_init, data, phy,      0x11000,  0x1000,",
  "ota_0,    app,  ota_0,    0x20000,  0x600000,",
  "ota_1,    app,  ota_1,    0x620000, 0x600000,",
  "coredump, data, coredump, 0xc20000, 0x10000,",
  "",
].join("\n");

export interface PartitionRow {
  name: string;
  type: number;
  subtype: number;
  offset: number;
  size: number;
  flags?: number;
}

/** The same table as numbers (type and subtype codes from ESP-IDF's gen_esp32part.py). */
export const PARTITION_ROWS: readonly PartitionRow[] = [
  { name: "nvs", type: 1, subtype: 2, offset: 0x9000, size: 0x6000 },
  { name: "otadata", type: 1, subtype: 0, offset: 0xf000, size: 0x2000 },
  { name: "phy_init", type: 1, subtype: 1, offset: 0x11000, size: 0x1000 },
  { name: "ota_0", type: 0, subtype: 0x10, offset: 0x20000, size: 0x600000 },
  { name: "ota_1", type: 0, subtype: 0x11, offset: 0x620000, size: 0x600000 },
  { name: "coredump", type: 1, subtype: 3, offset: 0xc20000, size: 0x10000 },
];

/**
 * partition-table.bin as ESP-IDF v6.0.3 writes it: one 32-byte entry per row
 * (bytes AA 50, type, subtype, offset u32 LE, size u32 LE, 16-byte name, flags
 * u32 LE), then the MD5 entry (EB EB, 14 × FF, MD5 of the entries), then 0xFF
 * up to 0xC00 bytes. For PARTITION_ROWS it is byte-identical to
 * `gen_esp32part.py --flash-size 16MB 16mb.csv`.
 */
export function partitionTableBin(rows: readonly PartitionRow[] = PARTITION_ROWS): Uint8Array {
  const out = new Uint8Array(0xc00).fill(0xff);
  const view = new DataView(out.buffer);
  rows.forEach((r, i) => {
    const at = i * 32;
    out.set([0xaa, 0x50, r.type, r.subtype], at);
    view.setUint32(at + 4, r.offset, true);
    view.setUint32(at + 8, r.size, true);
    out.fill(0, at + 12, at + 28);
    out.set(new TextEncoder().encode(r.name), at + 12);
    view.setUint32(at + 28, r.flags ?? 0, true);
  });
  const end = rows.length * 32;
  out.set([0xeb, 0xeb], end);
  out.set(createHash("md5").update(out.subarray(0, end)).digest(), end + 16);
  return out;
}

/** Writes PARTITIONS_CSV as <dir>/16mb.csv and returns its path (collect.ts --partitions). */
export async function writePartitionsCsv(dir: string): Promise<string> {
  await mkdir(dir, { recursive: true });
  const path = join(dir, "16mb.csv");
  await writeFile(path, PARTITIONS_CSV);
  return path;
}

/** flasher_args.json as ESP-IDF v6.0.3 writes it for partitions/16mb.csv. */
export const FLASHER_ARGS = {
  write_flash_args: ["--flash-mode", "dio", "--flash-size", "16MB", "--flash-freq", "80m"],
  flash_settings: { flash_mode: "dio", flash_size: "16MB", flash_freq: "80m" },
  flash_files: {
    "0x0": "bootloader/bootloader.bin",
    "0x20000": "openmausbot-gadget.bin",
    "0x8000": "partition_table/partition-table.bin",
    "0xf000": "ota_data_initial.bin",
  },
  bootloader: { offset: "0x0", file: "bootloader/bootloader.bin", encrypted: "false" },
  app: { offset: "0x20000", file: "openmausbot-gadget.bin", encrypted: "false" },
  "partition-table": { offset: "0x8000", file: "partition_table/partition-table.bin", encrypted: "false" },
  otadata: { offset: "0xf000", file: "ota_data_initial.bin", encrypted: "false" },
  extra_esptool_args: { after: "hard-reset", before: "default-reset", stub: true, chip: "esp32s3" },
};

export async function tempDir(prefix = "omb-release-"): Promise<string> {
  return mkdtemp(join(tmpdir(), prefix));
}

/** An app image with a valid header and app descriptor; `embed` byte strings are placed after it. */
export function fakeAppImage(version: string, embed: Uint8Array[] = [], size = 8192, project = "openmausbot-gadget"): Uint8Array {
  const img = new Uint8Array(size);
  img[0] = 0xe9;
  img[1] = 4;
  new DataView(img.buffer).setUint32(32, 0xabcd5432, true);
  img.set(new TextEncoder().encode(version), 48);
  img.set(new TextEncoder().encode(project), 80);
  let at = 512;
  for (const bytes of embed) {
    img.set(bytes, at);
    at += bytes.length + 16;
  }
  return img;
}

export interface FakeBuild {
  version: string;
  embed?: Uint8Array[];
  appSize?: number;
  flasherArgs?: unknown;
  project?: string;
  /** Rows of the built partition table (default: PARTITION_ROWS, i.e. 16mb.csv). */
  partitions?: readonly PartitionRow[];
  /** Size of bootloader/bootloader.bin (default 1024). */
  bootloaderSize?: number;
}

export async function makeBuildDir(dir: string, opts: FakeBuild): Promise<string> {
  await mkdir(join(dir, "bootloader"), { recursive: true });
  await mkdir(join(dir, "partition_table"), { recursive: true });
  await writeFile(join(dir, "flasher_args.json"), JSON.stringify(opts.flasherArgs ?? FLASHER_ARGS, null, 4));
  await writeFile(join(dir, "openmausbot-gadget.bin"), fakeAppImage(opts.version, opts.embed ?? [], opts.appSize ?? 8192, opts.project));
  await writeFile(join(dir, "bootloader/bootloader.bin"), new Uint8Array(opts.bootloaderSize ?? 1024).fill(0xb0));
  await writeFile(join(dir, "partition_table/partition-table.bin"), partitionTableBin(opts.partitions));
  await writeFile(join(dir, "ota_data_initial.bin"), new Uint8Array(8192).fill(0xff));
  await writeFile(join(dir, "merged.bin"), new Uint8Array(16384).fill(0x5a));
  return dir;
}

export interface TestReleaseKey {
  id: string;
  pem: string;
  pub: Uint8Array;
  pubB64: string;
}

export function makeReleaseKey(id: string): TestReleaseKey {
  const { privateKey, publicKey } = generateKeyPairSync("ec", { namedCurve: "prime256v1" });
  const pub = new Uint8Array(publicKey.export({ type: "spki", format: "der" }).subarray(-65));
  return { id, pem: privateKey.export({ type: "pkcs8", format: "pem" }) as string, pub, pubB64: Buffer.from(pub).toString("base64") };
}

/** keys/ with release-<id>.pub.b64 for each key plus the committed test key. */
export async function makeKeysDir(dir: string, keys: TestReleaseKey[]): Promise<string> {
  await mkdir(dir, { recursive: true });
  for (const k of keys) await writeFile(join(dir, `release-${k.id}.pub.b64`), `${k.pubB64}\n`);
  await writeFile(join(dir, "test-t1.pub.b64"), `${T1_PUB_B64}\n`);
  return dir;
}
```

- [ ] **Step 3: Write the failing tests**

`tools/release/test/lib.test.ts`:

```ts
// SPDX-License-Identifier: Apache-2.0
import assert from "node:assert/strict";
import { test } from "node:test";
import { firmwareText } from "../../../protocol/lib/identity.ts";
import { verifyP256 } from "../../../protocol/lib/verify.ts";
import {
  RELEASE_REPO,
  assetNames,
  buildInstallIndex,
  checkKeyTables,
  indexOfBytes,
  installEntryFromFlasherArgs,
  keysReleaseC,
  manifestUrl,
  parseReleasePubFiles,
  parseTag,
  readAppDescriptor,
  sha256sumsText,
} from "../lib.ts";
import { FLASHER_ARGS, T1_PUB_B64, fakeAppImage } from "./helpers.ts";

test("parseTag: release, prerelease and refusals", () => {
  assert.deepEqual(parseTag("v1.1.0"), { version: "1.1.0", prerelease: false });
  assert.deepEqual(parseTag("v1.2.0-rc.1"), { version: "1.2.0-rc.1", prerelease: true });
  for (const bad of ["1.1.0", "v1.1", "v1.1.0-dev", "v0.0.0-dev", "v1.1.0+build", "v01.1.0x", "v"]) {
    assert.throws(() => parseTag(bad), Error, bad);
  }
});

test("asset names and manifest URL follow contract §4.1 and §4.3", () => {
  assert.deepEqual(assetNames("amoled-175c", "1.1.0"), {
    app: "openmausbot-gadget-amoled-175c-1.1.0.bin",
    bootloader: "openmausbot-gadget-amoled-175c-1.1.0-bootloader.bin",
    partitionTable: "openmausbot-gadget-amoled-175c-1.1.0-partition-table.bin",
    otaData: "openmausbot-gadget-amoled-175c-1.1.0-ota-data-initial.bin",
    full: "openmausbot-gadget-amoled-175c-1.1.0-full.bin",
  });
  assert.equal(
    manifestUrl(RELEASE_REPO, "amoled-175c", "1.1.0"),
    "https://github.com/aivsomkar/openmausbot-gadget-sdk/releases/download/v1.1.0/openmausbot-gadget-amoled-175c-1.1.0.bin",
  );
});

test("install entry copies flasher_args.json offsets and sorts parts by address", () => {
  assert.deepEqual(installEntryFromFlasherArgs(FLASHER_ARGS, "amoled-175c", "1.1.0"), {
    parts: [
      { path: "openmausbot-gadget-amoled-175c-1.1.0-bootloader.bin", offset: "0x0" },
      { path: "openmausbot-gadget-amoled-175c-1.1.0-partition-table.bin", offset: "0x8000" },
      { path: "openmausbot-gadget-amoled-175c-1.1.0-ota-data-initial.bin", offset: "0xf000" },
      { path: "openmausbot-gadget-amoled-175c-1.1.0.bin", offset: "0x20000" },
    ],
    full: "openmausbot-gadget-amoled-175c-1.1.0-full.bin",
  });
});

test("install entry never hard-codes offsets", () => {
  const moved: { flash_files: Record<string, string> } = structuredClone(FLASHER_ARGS);
  moved.flash_files = {
    "0x0": "bootloader/bootloader.bin",
    "0x8000": "partition_table/partition-table.bin",
    "0xd000": "ota_data_initial.bin",
    "0x10000": "openmausbot-gadget.bin",
  };
  const entry = installEntryFromFlasherArgs(moved, "lcd-154", "2.0.0");
  assert.deepEqual(entry.parts.map((p) => p.offset), ["0x0", "0x8000", "0xd000", "0x10000"]);
});

test("install entry refuses a missing, extra or foreign image", () => {
  const missing = structuredClone(FLASHER_ARGS) as { flash_files: Record<string, string> };
  delete missing.flash_files["0xf000"];
  assert.throws(() => installEntryFromFlasherArgs(missing, "devkit", "1.0.0"), /ota_data_initial\.bin/);
  const extra = structuredClone(FLASHER_ARGS) as { flash_files: Record<string, string> };
  extra.flash_files["0x9000"] = "nvs.bin";
  assert.throws(() => installEntryFromFlasherArgs(extra, "devkit", "1.0.0"), /unexpected image/);
  const chip = structuredClone(FLASHER_ARGS);
  chip.extra_esptool_args.chip = "esp32";
  assert.throws(() => installEntryFromFlasherArgs(chip, "devkit", "1.0.0"), /esp32s3/);
});

test("buildInstallIndex keeps board order and refuses mixed versions", () => {
  const e = installEntryFromFlasherArgs(FLASHER_ARGS, "devkit", "1.0.0");
  const idx = buildInstallIndex("1.0.0", [
    { board: "devkit", version: "1.0.0", install: e },
    { board: "amoled-175c", version: "1.0.0", install: e },
  ]);
  assert.deepEqual(Object.keys(idx.boards), ["amoled-175c", "devkit"]);
  assert.throws(() => buildInstallIndex("1.0.0", [{ board: "devkit", version: "0.9.0", install: e }]), /0\.9\.0/);
});

test("readAppDescriptor reads version and project name at offset 32", () => {
  assert.deepEqual(readAppDescriptor(fakeAppImage("1.2.3")), { version: "1.2.3", projectName: "openmausbot-gadget" });
  const bad = fakeAppImage("1.2.3");
  bad[32] = 0;
  assert.throws(() => readAppDescriptor(bad), /0xABCD5432/);
  assert.throws(() => readAppDescriptor(new Uint8Array(200)), /0xE9/);
});

test("indexOfBytes finds a key inside an image", () => {
  const key = new Uint8Array(Buffer.from(T1_PUB_B64, "base64"));
  const img = fakeAppImage("1.0.0", [key]);
  assert.equal(indexOfBytes(img, key), 512);
  assert.equal(indexOfBytes(fakeAppImage("1.0.0"), key), -1);
});

test("SHA256SUMS is GNU format, sorted by name", () => {
  assert.equal(
    sha256sumsText([
      { name: "b.bin", sha256: "bb" },
      { name: "a.json", sha256: "aa" },
    ]),
    "aa  a.json\nbb  b.bin\n",
  );
});

test("the signed firmware text matches the contract's t1 vector", () => {
  const text = firmwareText("amoled-175c", "1.1.0", 1234567, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
  const der = Buffer.from(
    "3046022100de2dfa125231a739f0b1b27e2ec085e4027b59e4486978457ffccb95b3895f2d022100ef00bd5c4177cb97ba4a4f4036046f6a20b3c896c5ab1124720b54afe6179744",
    "hex",
  );
  assert.equal(verifyP256(new Uint8Array(Buffer.from(T1_PUB_B64, "base64")), text, new Uint8Array(der)), true);
});

test("checkKeyTables: empty, foreign ids, mismatches and a leaked test key", () => {
  const r1 = Buffer.from(T1_PUB_B64, "base64").toString("hex");
  const files = new Map([["r1", r1]]);
  assert.deepEqual(checkKeyTables({ release: [{ id: "r1", pub: r1 }], test: [] }, files), []);
  assert.match(checkKeyTables({ release: [], test: [] }, new Map()).join("\n"), /empty/);
  assert.match(checkKeyTables({ release: [{ id: "t1", pub: r1 }], test: [] }, files).join("\n"), /does not match/);
  assert.match(checkKeyTables({ release: [{ id: "r1", pub: "04".padEnd(130, "0") }], test: [] }, files).join("\n"), /differs/);
  assert.match(checkKeyTables({ release: [{ id: "r1", pub: r1 }], test: [{ id: "t1", pub: r1 }] }, files).join("\n"), /test key table/);
  assert.match(checkKeyTables({ release: [{ id: "r1", pub: r1 }], test: [] }, new Map([["r1", r1], ["r2", r1]])).join("\n"), /r2/);
});

test("parseReleasePubFiles requires canonical base64 of a SEC1 point and one newline", () => {
  assert.equal(parseReleasePubFiles([{ name: "release-r1.pub.b64", text: `${T1_PUB_B64}\n` }]).size, 1);
  assert.equal(parseReleasePubFiles([{ name: "test-t1.pub.b64", text: `${T1_PUB_B64}\n` }]).size, 0);
  assert.throws(() => parseReleasePubFiles([{ name: "release-r1.pub.b64", text: "AAAA\n" }]), /65-byte/);
});

test("keysReleaseC writes the sentinel with no keys and one row per key", () => {
  assert.match(keysReleaseC([]), /\{\{NULL, \{0\}\}\};\nconst size_t gadget_release_keys_count = 0;/);
  const pub = new Uint8Array(Buffer.from(T1_PUB_B64, "base64"));
  const c = keysReleaseC([
    { id: "r2", pub },
    { id: "r1", pub },
  ]);
  assert.ok(c.indexOf('"r1"') < c.indexOf('"r2"'));
  assert.match(c, /gadget_release_keys_count = 2;/);
  assert.throws(() => keysReleaseC([{ id: "t1", pub }]), /bad release key id/);
});
```

- [ ] **Step 4: Run them and watch them fail**

Run: `node --test tools/release/test/lib.test.ts`
Expected: FAIL with `ERR_MODULE_NOT_FOUND` for `tools/release/lib.ts`.

- [ ] **Step 5: Write the CLI plumbing**

`tools/release/cli.ts`:

```ts
// SPDX-License-Identifier: Apache-2.0
// Shared CLI plumbing for tools/release: run main() when the file is executed directly.
import { pathToFileURL } from "node:url";

export function runIfMain(metaUrl: string, main: (argv: string[]) => Promise<number>): void {
  const entry = process.argv[1];
  if (entry !== undefined && metaUrl === pathToFileURL(entry).href) {
    main(process.argv.slice(2)).then(
      (code) => {
        process.exitCode = code;
      },
      (err: unknown) => {
        console.error(err instanceof Error ? err.message : String(err));
        process.exitCode = 1;
      },
    );
  }
}

export function fail(tool: string, message: string): number {
  console.error(`${tool}: ${message}`);
  return 1;
}
```

- [ ] **Step 6: Write the library**

`tools/release/lib.ts`:

```ts
// SPDX-License-Identifier: Apache-2.0
// Pure helpers for release.yml (spec §8, contract §4.1–§4.3). No I/O here:
// the CLIs in this folder read and write files and call these functions.
import { createHash } from "node:crypto";

export const BOARDS = ["amoled-175c", "amoled-175", "lcd-154", "devkit"] as const;
export const RELEASE_REPO = "aivsomkar/openmausbot-gadget-sdk";
export const OTA_SLOT_SIZE = 6291456;
export const PROJECT_NAME = "openmausbot-gadget";
export const VERSION_RE = /^\d+\.\d+\.\d+(-[0-9A-Za-z.]+)?$/;
export const BOARD_RE = /^[a-z0-9-]{1,32}$/;
export const RELEASE_KEY_ID_RE = /^r[0-9]+$/;

/** Build-directory files, relative to build/<board>/ (contract §4.3). */
export const BUILD_FILES = {
  app: "openmausbot-gadget.bin",
  bootloader: "bootloader/bootloader.bin",
  partitionTable: "partition_table/partition-table.bin",
  otaData: "ota_data_initial.bin",
  full: "merged.bin",
} as const;

export type PartKind = "app" | "bootloader" | "partitionTable" | "otaData";

export interface AssetNames {
  app: string;
  bootloader: string;
  partitionTable: string;
  otaData: string;
  full: string;
}

export interface InstallPart {
  path: string;
  offset: string;
}

export interface InstallBoard {
  parts: InstallPart[];
  full: string;
}

export interface InstallIndexOut {
  version: string | null;
  boards: Record<string, InstallBoard>;
}

export interface ManifestBoard {
  url: string;
  size: number;
  sha256: string;
  sig: string;
  key_id: string;
}

export interface Manifest {
  version: string;
  boards: Record<string, ManifestBoard>;
}

export interface BoardMeta {
  board: string;
  version: string;
  install: InstallBoard;
}

export interface KeyEntry {
  id: string | null;
  pub: string; // lowercase hex of the 65-byte SEC1 point
}

export interface KeyTables {
  release: KeyEntry[];
  test: KeyEntry[];
}

export interface AppDescriptor {
  version: string;
  projectName: string;
}

/** `v1.1.0` → `{version: "1.1.0", prerelease: false}`. Throws on anything a release must not carry. */
export function parseTag(tag: string): { version: string; prerelease: boolean } {
  if (!tag.startsWith("v")) throw new Error(`tag ${JSON.stringify(tag)} must start with "v"`);
  const version = tag.slice(1);
  if (!VERSION_RE.test(version)) throw new Error(`tag ${JSON.stringify(tag)} is not v<major>.<minor>.<patch>[-<pre>]`);
  if (version.endsWith("-dev")) throw new Error(`release versions never end in -dev (got ${version})`);
  return { version, prerelease: version.includes("-") };
}

export function assetNames(board: string, version: string): AssetNames {
  if (!BOARD_RE.test(board)) throw new Error(`bad board id ${JSON.stringify(board)}`);
  const p = `openmausbot-gadget-${board}-${version}`;
  return {
    app: `${p}.bin`,
    bootloader: `${p}-bootloader.bin`,
    partitionTable: `${p}-partition-table.bin`,
    otaData: `${p}-ota-data-initial.bin`,
    full: `${p}-full.bin`,
  };
}

export function releaseDownloadBase(repo: string): string {
  if (!/^[A-Za-z0-9_.-]+\/[A-Za-z0-9_.-]+$/.test(repo)) throw new Error(`bad repository ${JSON.stringify(repo)}`);
  return `https://github.com/${repo}/releases/download/`;
}

export function manifestUrl(repo: string, board: string, version: string): string {
  return `${releaseDownloadBase(repo)}v${version}/${assetNames(board, version).app}`;
}

const PART_BY_BUILD_FILE: Record<string, PartKind> = {
  [BUILD_FILES.app]: "app",
  [BUILD_FILES.bootloader]: "bootloader",
  [BUILD_FILES.partitionTable]: "partitionTable",
  [BUILD_FILES.otaData]: "otaData",
};

/**
 * The install.json entry for one board, from its build/<board>/flasher_args.json.
 * Offsets are copied verbatim from `flash_files`; parts come out in flash-address order.
 */
export function installEntryFromFlasherArgs(flasherArgs: unknown, board: string, version: string): InstallBoard {
  if (typeof flasherArgs !== "object" || flasherArgs === null) throw new Error("flasher_args.json is not an object");
  const fa = flasherArgs as { flash_files?: unknown; extra_esptool_args?: { chip?: unknown } };
  if (fa.extra_esptool_args?.chip !== "esp32s3") {
    throw new Error(`flasher_args.json chip is ${JSON.stringify(fa.extra_esptool_args?.chip)}, expected "esp32s3"`);
  }
  if (typeof fa.flash_files !== "object" || fa.flash_files === null) throw new Error("flasher_args.json has no flash_files");
  const names = assetNames(board, version);
  const seen = new Set<PartKind>();
  const parts: Array<InstallPart & { address: number }> = [];
  for (const [offset, file] of Object.entries(fa.flash_files as Record<string, unknown>)) {
    if (!/^0x[0-9a-fA-F]+$/.test(offset)) throw new Error(`flash_files offset ${JSON.stringify(offset)} is not hex`);
    if (typeof file !== "string") throw new Error(`flash_files[${offset}] is not a file name`);
    const kind = PART_BY_BUILD_FILE[file];
    if (kind === undefined) throw new Error(`flash_files has an unexpected image ${JSON.stringify(file)}`);
    if (seen.has(kind)) throw new Error(`flash_files lists ${file} twice`);
    seen.add(kind);
    parts.push({ path: names[kind], offset, address: Number.parseInt(offset, 16) });
  }
  for (const kind of ["bootloader", "partitionTable", "otaData", "app"] as const) {
    if (!seen.has(kind)) throw new Error(`flash_files is missing ${BUILD_FILES[kind]}`);
  }
  parts.sort((a, b) => a.address - b.address);
  return { parts: parts.map(({ path, offset }) => ({ path, offset })), full: names.full };
}

export function buildInstallIndex(version: string, metas: readonly BoardMeta[]): InstallIndexOut {
  const order = (board: string): number => (BOARDS as readonly string[]).indexOf(board);
  const boards: Record<string, InstallBoard> = {};
  for (const meta of [...metas].sort((a, b) => order(a.board) - order(b.board))) {
    if (meta.version !== version) throw new Error(`${meta.board} was built as ${meta.version}, not ${version}`);
    if (boards[meta.board]) throw new Error(`${meta.board} appears twice`);
    boards[meta.board] = meta.install;
  }
  return { version, boards };
}

/** esp_app_desc_t sits at sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t) = 32. */
export function readAppDescriptor(image: Uint8Array): AppDescriptor {
  if (image.length < 112 || image[0] !== 0xe9) throw new Error("not an ESP app image (magic 0xE9 missing)");
  const view = new DataView(image.buffer, image.byteOffset, image.byteLength);
  if (view.getUint32(32, true) !== 0xabcd5432) throw new Error("app descriptor magic 0xABCD5432 missing at offset 32");
  return { version: cString(image.subarray(48, 80)), projectName: cString(image.subarray(80, 112)) };
}

function cString(bytes: Uint8Array): string {
  const end = bytes.indexOf(0);
  return new TextDecoder().decode(end === -1 ? bytes : bytes.subarray(0, end));
}

export function indexOfBytes(haystack: Uint8Array, needle: Uint8Array): number {
  if (needle.length === 0) return 0;
  outer: for (let i = haystack.indexOf(needle[0]); i !== -1 && i <= haystack.length - needle.length; i = haystack.indexOf(needle[0], i + 1)) {
    for (let j = 1; j < needle.length; j++) if (haystack[i + j] !== needle[j]) continue outer;
    return i;
  }
  return -1;
}

export function sha256Hex(bytes: Uint8Array): string {
  return createHash("sha256").update(bytes).digest("hex");
}

/** GNU sha256sum text format: "<hex>  <name>\n", sorted by name. */
export function sha256sumsText(files: ReadonlyArray<{ name: string; sha256: string }>): string {
  return [...files]
    .sort((a, b) => (a.name < b.name ? -1 : a.name > b.name ? 1 : 0))
    .map((f) => `${f.sha256}  ${f.name}\n`)
    .join("");
}

/** Errors in the compiled key tables against keys/release-*.pub.b64 (decoded, as hex). Empty = OK. */
export function checkKeyTables(tables: KeyTables, releasePubFiles: ReadonlyMap<string, string>): string[] {
  const errors: string[] = [];
  if (tables.release.length === 0) {
    errors.push("the release key table is empty: generate r1 and run tools/release/gen-release-keys.ts (docs/release-keys.md)");
  }
  const ids = new Set<string>();
  for (const key of tables.release) {
    if (key.id === null || !RELEASE_KEY_ID_RE.test(key.id)) {
      errors.push(`release key id ${JSON.stringify(key.id)} does not match /^r[0-9]+$/`);
      continue;
    }
    if (ids.has(key.id)) errors.push(`release key ${key.id} appears twice`);
    ids.add(key.id);
    const file = releasePubFiles.get(key.id);
    if (file === undefined) errors.push(`release key ${key.id} has no keys/release-${key.id}.pub.b64`);
    else if (file !== key.pub) errors.push(`release key ${key.id} differs from keys/release-${key.id}.pub.b64`);
  }
  for (const id of releasePubFiles.keys()) {
    if (!ids.has(id)) errors.push(`keys/release-${id}.pub.b64 is not in firmware/core/src/keys_release.c`);
  }
  if (tables.test.length !== 0) {
    errors.push(`the test key table holds ${tables.test.map((k) => k.id).join(", ")} without GADGET_TEST_KEYS`);
  }
  return errors;
}

/** The text of firmware/core/src/keys_release.c for these keys (sorted by number). */
export function keysReleaseC(keys: ReadonlyArray<{ id: string; pub: Uint8Array }>): string {
  const sorted = [...keys].sort((a, b) => Number(a.id.slice(1)) - Number(b.id.slice(1)));
  for (const k of sorted) {
    if (!RELEASE_KEY_ID_RE.test(k.id)) throw new Error(`bad release key id ${k.id}`);
    if (k.pub.length !== 65 || k.pub[0] !== 0x04) throw new Error(`release key ${k.id} is not a 65-byte SEC1 point`);
  }
  const head = [
    "/* firmware/core/src/keys_release.c */",
    "/* SPDX-License-Identifier: Apache-2.0 */",
    "/* Release public keys (spec §8). Written by tools/release/gen-release-keys.ts",
    " * from keys/release-*.pub.b64; see docs/release-keys.md. Do not edit by hand. */",
    '#include "gadget_ota.h"',
    "",
  ];
  if (sorted.length === 0) {
    return [...head, "const gadget_release_key_t gadget_release_keys[] = {{NULL, {0}}};", "const size_t gadget_release_keys_count = 0;", ""].join("\n");
  }
  const rows = sorted.map((k) => {
    const bytes = [...k.pub].map((b) => `0x${b.toString(16).padStart(2, "0")}`);
    const lines: string[] = [];
    for (let i = 0; i < bytes.length; i += 12) lines.push(`      ${bytes.slice(i, i + 12).join(", ")}`);
    return `    {"${k.id}",\n     {\n${lines.join(",\n")}\n     }}`;
  });
  return [
    ...head,
    "const gadget_release_key_t gadget_release_keys[] = {",
    `${rows.join(",\n")},`,
    "};",
    `const size_t gadget_release_keys_count = ${sorted.length};`,
    "",
  ].join("\n");
}

/** Release public key files: keys/release-<id>.pub.b64 → id → decoded hex. Throws on a malformed file. */
export function parseReleasePubFiles(files: ReadonlyArray<{ name: string; text: string }>): Map<string, string> {
  const out = new Map<string, string>();
  for (const f of files) {
    const m = /^release-(r[0-9]+)\.pub\.b64$/.exec(f.name);
    if (!m) continue;
    const text = f.text.endsWith("\n") ? f.text.slice(0, -1) : f.text;
    const bytes = Buffer.from(text, "base64");
    if (bytes.toString("base64") !== text || bytes.length !== 65 || bytes[0] !== 0x04) {
      throw new Error(`keys/${f.name} is not canonical base64 of a 65-byte SEC1 point followed by one newline`);
    }
    out.set(m[1], bytes.toString("hex"));
  }
  return out;
}
```

- [ ] **Step 7: Run the tests**

Run: `node --test tools/release/test/lib.test.ts`
Expected: `# pass 13` and `# fail 0`. The test "the signed firmware text matches the contract's t1 vector" checks P1's `firmwareText` against contract §1.7; if only that one fails, P1's text builder differs from the contract: report it to P1 rather than changing the vector.

- [ ] **Step 8: Commit**

```bash
git add tools/release/lib.ts tools/release/cli.ts tools/release/test/helpers.ts tools/release/test/lib.test.ts
git commit -m "feat(release): release helpers for tags, assets, install.json and key tables" \
  -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

### Task 2: Collect and check one board's build

**Files:**
- Create: `tools/release/partitions.ts`
- Create: `tools/release/collect.ts`
- Create: `tools/release/install-json.ts`
- Test: `tools/release/test/collect.test.ts`

**Interfaces:**
- Consumes: Task 1's `lib.ts` (`BOARDS`, `BUILD_FILES`, `OTA_SLOT_SIZE`, `PROJECT_NAME`, `VERSION_RE`, `assetNames`, `indexOfBytes`, `installEntryFromFlasherArgs`, `parseReleasePubFiles`, `readAppDescriptor`, `buildInstallIndex`, `BoardMeta`), `cli.ts`, `helpers.ts` (`PARTITION_ROWS`, `writePartitionsCsv`, `makeBuildDir` with `partitions`/`bootloaderSize`); the ESP-IDF build folder layout from contract §2.17/§4.3 (`build/<board>/openmausbot-gadget.bin`, `bootloader/bootloader.bin`, `partition_table/partition-table.bin`, `ota_data_initial.bin`, `flasher_args.json`, and `merged.bin` from `idf.py … merge-bin -o merged.bin`); P2c's `firmware/ports/esp32/partitions/16mb.csv`.
- Produces (`partitions.ts`): `interface Partition {name, type, subtype, offset, size, flags}`, `parsePartitionCsv(text): Partition[]`, `parsePartitionTable(bin: Uint8Array): Partition[]`, `partitionProblems(built, csv, images: {file, offset, size}[]): string[]`.
- Produces: CLI `node tools/release/collect.ts --board <id> --version <v> --build <dir> --assets <dir> --meta <dir> [--keys keys] [--partitions firmware/ports/esp32/partitions/16mb.csv] [--local]` → exit 0 and five renamed assets in `--assets` plus `<meta>/<board>.json` (`BoardMeta`), or exit 1 with `collect: <reason>`. `main(argv): Promise<number>` exported for tests. Reads only the files at the top of `--keys` (a `keys/retired/` folder is ignored).
- Produces: CLI `node tools/release/install-json.ts --meta <dir> --version <v> --out <file>`; exported `readMetas(dir): Promise<BoardMeta[]>` (used by Task 3) and `main(argv)`.

- [ ] **Step 1: Write the failing tests**

`tools/release/test/collect.test.ts`:

```ts
// SPDX-License-Identifier: Apache-2.0
import assert from "node:assert/strict";
import { readFile, readdir } from "node:fs/promises";
import { join } from "node:path";
import { test } from "node:test";
import { main } from "../collect.ts";
import { PARTITION_ROWS, T1_PUB_B64, makeBuildDir, makeKeysDir, makeReleaseKey, tempDir, writePartitionsCsv, type PartitionRow } from "./helpers.ts";

const t1 = new Uint8Array(Buffer.from(T1_PUB_B64, "base64"));

interface SetupOptions {
  version?: string;
  embedRelease?: boolean;
  embedT1?: boolean;
  appSize?: number;
  project?: string;
  partitions?: readonly PartitionRow[];
  bootloaderSize?: number;
}

async function setup(opts: SetupOptions = {}) {
  const root = await tempDir();
  const key = makeReleaseKey("r1");
  const embed: Uint8Array[] = [];
  if (opts.embedRelease ?? true) embed.push(key.pub);
  if (opts.embedT1) embed.push(t1);
  const build = await makeBuildDir(join(root, "build"), {
    version: opts.version ?? "1.1.0",
    embed,
    appSize: opts.appSize,
    project: opts.project,
    partitions: opts.partitions,
    bootloaderSize: opts.bootloaderSize,
  });
  const keys = await makeKeysDir(join(root, "keys"), [key]);
  const csv = await writePartitionsCsv(join(root, "partitions"));
  return { root, build, keys, csv, assets: join(root, "out/assets"), meta: join(root, "out/meta") };
}

function args(s: Awaited<ReturnType<typeof setup>>, extra: string[] = [], version = "1.1.0"): string[] {
  return ["--board", "amoled-175c", "--version", version, "--build", s.build, "--assets", s.assets, "--meta", s.meta, "--keys", s.keys, "--partitions", s.csv, ...extra];
}

test("collect copies the five assets and writes the board's install entry", async () => {
  const s = await setup();
  assert.equal(await main(args(s)), 0);
  assert.deepEqual((await readdir(s.assets)).sort(), [
    "openmausbot-gadget-amoled-175c-1.1.0-bootloader.bin",
    "openmausbot-gadget-amoled-175c-1.1.0-full.bin",
    "openmausbot-gadget-amoled-175c-1.1.0-ota-data-initial.bin",
    "openmausbot-gadget-amoled-175c-1.1.0-partition-table.bin",
    "openmausbot-gadget-amoled-175c-1.1.0.bin",
  ]);
  const meta = JSON.parse(await readFile(join(s.meta, "amoled-175c.json"), "utf8"));
  assert.equal(meta.version, "1.1.0");
  assert.deepEqual(meta.install.parts.map((p: { offset: string }) => p.offset), ["0x0", "0x8000", "0xf000", "0x20000"]);
});

test("collect refuses an image whose embedded version is not the tag's", async () => {
  const s = await setup({ version: "0.0.0-dev" });
  assert.equal(await main(args(s)), 1);
});

test("collect refuses a -dev version unless --local", async () => {
  const s = await setup({ version: "0.0.0-dev", embedRelease: false });
  assert.equal(await main(args(s, [], "0.0.0-dev")), 1);
  assert.equal(await main(args(s, ["--local"], "0.0.0-dev")), 0);
});

test("collect refuses a release image that carries the test key t1", async () => {
  const s = await setup({ embedT1: true });
  assert.equal(await main(args(s)), 1);
});

test("collect refuses a release image without the release key", async () => {
  const s = await setup({ embedRelease: false });
  assert.equal(await main(args(s)), 1);
});

test("collect refuses an app larger than the OTA slot", async () => {
  const s = await setup({ appSize: 6291457 });
  assert.equal(await main(args(s)), 1);
});

test("collect refuses another project's image and an unknown board", async () => {
  const s = await setup({ project: "hello_world" });
  assert.equal(await main(args(s)), 1);
  const s2 = await setup();
  const a = args(s2);
  a[1] = "esp32-c3";
  assert.equal(await main(a), 1);
});

test("collect refuses a table that differs from 16mb.csv", async () => {
  const smallerNvs = PARTITION_ROWS.map((r) => (r.name === "nvs" ? { ...r, size: 0x5000 } : r));
  const s = await setup({ partitions: smallerNvs });
  assert.equal(await main(args(s)), 1);
});

test("collect refuses a part that overlaps nvs", async () => {
  const s = await setup({ bootloaderSize: 0x9400 }); // flashed at 0x0, so it reaches past 0x9000
  assert.equal(await main(args(s)), 1);
});
```

- [ ] **Step 2: Run them and watch them fail**

Run: `node --test tools/release/test/collect.test.ts`
Expected: FAIL with `ERR_MODULE_NOT_FOUND` for `tools/release/collect.ts`.

- [ ] **Step 3: Write `partitions.ts`**

The formats are ESP-IDF v6.0.3's (`components/partition_table/gen_esp32part.py`): a 32-byte entry per partition starting with the bytes `AA 50`, then an MD5 entry starting `EB EB`, then `0xFF` padding.

`tools/release/partitions.ts`:

```ts
// SPDX-License-Identifier: Apache-2.0
// The partition-table check (spec §5.3, A36). An installer reinstall keeps NVS
// (the gadget's identity key, Wi-Fi and pairing) only while every release uses
// the same partitions/16mb.csv and no flashed image reaches into nvs or
// phy_init. Formats from ESP-IDF v6.0.3 components/partition_table/gen_esp32part.py.
import { createHash } from "node:crypto";
import { OTA_SLOT_SIZE } from "./lib.ts";

export interface Partition {
  name: string;
  type: number;
  subtype: number;
  offset: number;
  size: number;
  flags: number;
}

const APP = 0x00;
const DATA = 0x01;
const TYPES: Record<string, number> = { app: APP, data: DATA };
const SUBTYPES: Record<number, Record<string, number>> = {
  [APP]: { factory: 0x00, test: 0x20, ...Object.fromEntries(Array.from({ length: 16 }, (_, i) => [`ota_${i}`, 0x10 + i])) },
  [DATA]: { ota: 0x00, phy: 0x01, nvs: 0x02, coredump: 0x03, nvs_keys: 0x04, efuse: 0x05, undefined: 0x06, fat: 0x81, spiffs: 0x82, littlefs: 0x83 },
};
const FLAG_BITS: Record<string, number> = { encrypted: 1, readonly: 2 };
const NVS = 0x02;
const PHY = 0x01;

function csvNumber(text: string, what: string): number {
  const m = /^(0x[0-9a-f]+|\d+)([km])?$/i.exec(text);
  if (m === null || m[1] === undefined) throw new Error(`16mb.csv: ${what} ${JSON.stringify(text)} is not a number`);
  const unit = m[2] === undefined ? 1 : m[2].toLowerCase() === "k" ? 1024 : 1024 * 1024;
  return Number(m[1]) * unit;
}

function lookup(table: Record<string, number> | undefined, text: string, what: string): number {
  if (table !== undefined && Object.hasOwn(table, text)) return table[text] as number;
  return csvNumber(text, what);
}

/** Rows of a partitions CSV. Every row must give its offset: the release table is pinned (spec §5.3). */
export function parsePartitionCsv(text: string): Partition[] {
  const rows: Partition[] = [];
  for (const raw of text.split(/\r?\n/)) {
    const line = raw.trim();
    if (line === "" || line.startsWith("#")) continue;
    const [name = "", typeText = "", subtypeText = "", offsetText = "", sizeText = "", flagsText = ""] = line.split(",").map((s) => s.trim());
    if (name === "" || sizeText === "") throw new Error(`16mb.csv: bad row ${JSON.stringify(line)}`);
    if (offsetText === "") throw new Error(`16mb.csv: ${name} has no offset; the release table pins every offset (spec §5.3)`);
    const type = lookup(TYPES, typeText, `${name} type`);
    let flags = 0;
    for (const flag of flagsText.split(":").filter(Boolean)) {
      if (!Object.hasOwn(FLAG_BITS, flag)) throw new Error(`16mb.csv: ${name} has an unknown flag ${flag}`);
      flags |= FLAG_BITS[flag] as number;
    }
    rows.push({
      name,
      type,
      subtype: lookup(SUBTYPES[type], subtypeText, `${name} subtype`),
      offset: csvNumber(offsetText, `${name} offset`),
      size: csvNumber(sizeText, `${name} size`),
      flags,
    });
  }
  return rows;
}

/** Entries of a built partition-table.bin, after checking its MD5 entry. */
export function parsePartitionTable(bin: Uint8Array): Partition[] {
  const view = new DataView(bin.buffer, bin.byteOffset, bin.byteLength);
  const rows: Partition[] = [];
  for (let at = 0; at + 32 <= bin.length; at += 32) {
    if (bin[at] === 0xaa && bin[at + 1] === 0x50) {
      const label = bin.subarray(at + 12, at + 28);
      const nul = label.indexOf(0);
      rows.push({
        name: new TextDecoder().decode(nul === -1 ? label : label.subarray(0, nul)),
        type: view.getUint8(at + 2),
        subtype: view.getUint8(at + 3),
        offset: view.getUint32(at + 4, true),
        size: view.getUint32(at + 8, true),
        flags: view.getUint32(at + 28, true),
      });
      continue;
    }
    if (bin[at] === 0xeb && bin[at + 1] === 0xeb) {
      const want = Buffer.from(bin.subarray(at + 16, at + 32)).toString("hex");
      if (createHash("md5").update(bin.subarray(0, at)).digest("hex") !== want) {
        throw new Error("partition-table.bin: its MD5 entry does not match the table");
      }
      return rows;
    }
    if (bin[at] === 0xff && bin[at + 1] === 0xff) return rows;
    throw new Error(`partition-table.bin: unexpected bytes at 0x${at.toString(16)}`);
  }
  return rows;
}

const describe = (p: Partition): string =>
  `${p.name} (type 0x${p.type.toString(16)}, subtype 0x${p.subtype.toString(16)}, 0x${p.offset.toString(16)}, 0x${p.size.toString(16)} bytes, flags ${p.flags})`;

/**
 * Why this build would break an installer reinstall or OTA; empty = OK. The
 * built table must equal 16mb.csv, both OTA slots must be OTA_SLOT_SIZE, and no
 * flashed image may overlap nvs or phy_init.
 */
export function partitionProblems(
  built: readonly Partition[],
  csv: readonly Partition[],
  images: ReadonlyArray<{ file: string; offset: number; size: number }>,
): string[] {
  const problems: string[] = [];
  for (let i = 0; i < Math.max(built.length, csv.length); i++) {
    const b = built[i];
    const c = csv[i];
    if (b === undefined || c === undefined || describe(b) !== describe(c)) {
      problems.push(`the built partition table differs from 16mb.csv at entry ${i + 1}: built ${b ? describe(b) : "nothing"}, 16mb.csv ${c ? describe(c) : "nothing"}`);
      break;
    }
  }
  for (const subtype of [0x10, 0x11]) {
    const slot = built.find((p) => p.type === APP && p.subtype === subtype);
    if (slot === undefined) problems.push(`the partition table has no ota_${subtype - 0x10}`);
    else if (slot.size !== OTA_SLOT_SIZE) problems.push(`${slot.name} is ${slot.size} bytes, not the ${OTA_SLOT_SIZE}-byte OTA slot every board advertises`);
  }
  const kept = built.filter((p) => p.type === DATA && (p.subtype === NVS || p.subtype === PHY));
  if (!kept.some((p) => p.subtype === NVS)) problems.push("the partition table has no nvs partition");
  for (const img of images) {
    for (const p of kept) {
      if (img.offset < p.offset + p.size && p.offset < img.offset + img.size) {
        problems.push(`${img.file} (0x${img.offset.toString(16)}, ${img.size} bytes) overlaps ${p.name} (0x${p.offset.toString(16)}-0x${(p.offset + p.size).toString(16)}), so a reinstall would erase it`);
      }
    }
  }
  return problems;
}
```

- [ ] **Step 4: Write `collect.ts`**

`tools/release/collect.ts`:

```ts
// SPDX-License-Identifier: Apache-2.0
// Turns one board's ESP-IDF build folder into release assets plus a meta file
// with its install.json entry, after checking the image (spec §8, contract §4.3)
// and its partition table (spec §5.3).
//   node tools/release/collect.ts --board <id> --version <v> --build <dir> \
//        --assets <dir> --meta <dir> [--keys keys] \
//        [--partitions firmware/ports/esp32/partitions/16mb.csv] [--local]
// --local (installer tests with a local build): allows -dev and skips the key
// checks. The partition check always runs.
import { copyFile, mkdir, readFile, readdir, stat, writeFile } from "node:fs/promises";
import { join } from "node:path";
import { parseArgs } from "node:util";
import { fail, runIfMain } from "./cli.ts";
import {
  BOARDS,
  BUILD_FILES,
  OTA_SLOT_SIZE,
  PROJECT_NAME,
  VERSION_RE,
  assetNames,
  indexOfBytes,
  installEntryFromFlasherArgs,
  parseReleasePubFiles,
  readAppDescriptor,
  type BoardMeta,
} from "./lib.ts";
import { parsePartitionCsv, parsePartitionTable, partitionProblems } from "./partitions.ts";

export async function main(argv: string[]): Promise<number> {
  const { values } = parseArgs({
    args: argv,
    options: {
      board: { type: "string" },
      version: { type: "string" },
      build: { type: "string" },
      assets: { type: "string" },
      meta: { type: "string" },
      keys: { type: "string", default: "keys" },
      partitions: { type: "string", default: "firmware/ports/esp32/partitions/16mb.csv" },
      local: { type: "boolean", default: false },
    },
    strict: true,
  });
  const { board, version, build, assets, meta } = values;
  if (!board || !version || !build || !assets || !meta) {
    return fail("collect", "--board, --version, --build, --assets and --meta are required");
  }
  if (!(BOARDS as readonly string[]).includes(board)) return fail("collect", `unknown board ${board} (known: ${BOARDS.join(", ")})`);
  if (!VERSION_RE.test(version)) return fail("collect", `bad version ${version}`);
  if (!values.local && version.endsWith("-dev")) return fail("collect", `release versions never end in -dev (got ${version})`);

  let flashFiles: Array<[string, string]>;
  let install;
  try {
    const flasherArgs = JSON.parse(await readFile(join(build, "flasher_args.json"), "utf8")) as { flash_files: Record<string, string> };
    install = installEntryFromFlasherArgs(flasherArgs, board, version);
    flashFiles = Object.entries(flasherArgs.flash_files); // validated by installEntryFromFlasherArgs
  } catch (err) {
    return fail("collect", `${board}: ${(err as Error).message}`);
  }

  const app = new Uint8Array(await readFile(join(build, BUILD_FILES.app)));
  let desc;
  try {
    desc = readAppDescriptor(app);
  } catch (err) {
    return fail("collect", `${board}: ${(err as Error).message}`);
  }
  if (desc.version !== version) return fail("collect", `${board}: the image reports version ${desc.version}, expected ${version} (was PROJECT_VER passed?)`);
  if (desc.projectName !== PROJECT_NAME) return fail("collect", `${board}: the image's project is ${desc.projectName}, expected ${PROJECT_NAME}`);
  if (app.length > OTA_SLOT_SIZE) return fail("collect", `${board}: app is ${app.length} bytes, larger than the ${OTA_SLOT_SIZE}-byte OTA slot`);

  // Spec §5.3: one table for every board, unchanged across releases, or an
  // installer reinstall loses the gadget's identity, Wi-Fi and pairing.
  try {
    const images = await Promise.all(
      flashFiles.map(async ([offset, file]) => ({ file, offset: Number.parseInt(offset, 16), size: (await stat(join(build, file))).size })),
    );
    const problems = partitionProblems(
      parsePartitionTable(new Uint8Array(await readFile(join(build, BUILD_FILES.partitionTable)))),
      parsePartitionCsv(await readFile(values.partitions, "utf8")),
      images,
    );
    if (problems.length > 0) return fail("collect", `${board}: ${problems.join("; ")}`);
  } catch (err) {
    return fail("collect", `${board}: ${(err as Error).message}`);
  }

  if (!values.local) {
    const keyDir = values.keys;
    // Only the files at the top of keys/: a retired key lives in keys/retired/ (docs/release-keys.md).
    const names = (await readdir(keyDir, { withFileTypes: true })).filter((e) => e.isFile()).map((e) => e.name);
    const release = parseReleasePubFiles(
      await Promise.all(names.map(async (name) => ({ name, text: await readFile(join(keyDir, name), "utf8") }))),
    );
    if (release.size === 0) return fail("collect", "keys/ holds no release-r*.pub.b64: generate r1 first (docs/release-keys.md)");
    for (const [id, hex] of release) {
      if (indexOfBytes(app, Buffer.from(hex, "hex")) === -1) return fail("collect", `${board}: release key ${id} is not compiled into the image`);
    }
    const t1 = Buffer.from((await readFile(join(keyDir, "test-t1.pub.b64"), "utf8")).trim(), "base64");
    if (indexOfBytes(app, t1) !== -1) return fail("collect", `${board}: the test key t1 is compiled into a release image (CONFIG_GADGET_TEST_KEYS must be off)`);
  }

  const out = assetNames(board, version);
  await mkdir(assets, { recursive: true });
  await mkdir(meta, { recursive: true });
  const copies: Array<[string, string]> = [
    [BUILD_FILES.app, out.app],
    [BUILD_FILES.bootloader, out.bootloader],
    [BUILD_FILES.partitionTable, out.partitionTable],
    [BUILD_FILES.otaData, out.otaData],
    [BUILD_FILES.full, out.full],
  ];
  for (const [from, to] of copies) {
    const bytes = await readFile(join(build, from));
    if (bytes.length === 0) return fail("collect", `${board}: ${from} is empty`);
    await copyFile(join(build, from), join(assets, to));
  }
  const record: BoardMeta = { board, version, install };
  await writeFile(join(meta, `${board}.json`), `${JSON.stringify(record, null, 2)}\n`);
  console.log(`collect: ${board} ${version}: ${app.length} bytes, parts ${install.parts.map((p) => p.offset).join(" ")}`);
  return 0;
}

runIfMain(import.meta.url, main);
```

- [ ] **Step 5: Write `install-json.ts`**

`tools/release/install-json.ts`:

```ts
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
```

- [ ] **Step 6: Run the tests**

Run: `node --test tools/release/test/collect.test.ts`
Expected: `# pass 9` and `# fail 0`. The refused cases print `collect: …` lines on stderr; that is expected. The two partition tests print `collect: amoled-175c: the built partition table differs from 16mb.csv at entry 1: …` and `collect: amoled-175c: bootloader/bootloader.bin (0x0, 37888 bytes) overlaps nvs (0x9000-0xf000), so a reinstall would erase it`; if they fail for another reason, the fixture is wrong.

- [ ] **Step 7: Commit**

```bash
git add tools/release/partitions.ts tools/release/collect.ts tools/release/install-json.ts tools/release/test/collect.test.ts
git commit -m "feat(release): collect and check each board's build into release assets" \
  -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

### Task 3: Sign images and write the release manifests

**Files:**
- Create: `tools/release/sign.ts`
- Test: `tools/release/test/sign.test.ts`

**Interfaces:**
- Consumes: `firmwareText`, `verifyP256` (P1); Task 1 `lib.ts`; Task 2 `readMetas` and `collect.main`.
- Produces: CLI `node tools/release/sign.ts --assets <dir> --meta <dir> --tag <vX.Y.Z[-pre]> --repo <owner/name> --key-id <rN> --key-env <ENV_NAME> --pub <keys/release-rN.pub.b64> [--boards a,b,c,d]`, reading the PKCS#8 PEM from the named environment variable. `--repo` must be `RELEASE_REPO` (`aivsomkar/openmausbot-gadget-sdk`): contract §4.1 and P4b's `RELEASE_URL_PREFIX` pin the manifest URLs to it, so a renamed, transferred or forked repository fails here instead of publishing a manifest MausBot rejects. Writes `manifest.json` (contract §4.1), `install.json` (§4.2) and `SHA256SUMS` (§4.3) into `--assets`. `main(argv, env = process.env): Promise<number>` exported for tests.

- [ ] **Step 1: Write the failing tests**

`tools/release/test/sign.test.ts`:

```ts
// SPDX-License-Identifier: Apache-2.0
import assert from "node:assert/strict";
import { createHash } from "node:crypto";
import { readFile, readdir, writeFile } from "node:fs/promises";
import { join } from "node:path";
import { test } from "node:test";
import { firmwareText } from "../../../protocol/lib/identity.ts";
import { verifyP256 } from "../../../protocol/lib/verify.ts";
import { main as collect } from "../collect.ts";
import { BOARDS, RELEASE_REPO } from "../lib.ts";
import { main as sign } from "../sign.ts";
import { makeBuildDir, makeKeysDir, makeReleaseKey, tempDir, writePartitionsCsv, type TestReleaseKey } from "./helpers.ts";

async function release(version: string, key: TestReleaseKey = makeReleaseKey("r1")) {
  const root = await tempDir();
  const keys = await makeKeysDir(join(root, "keys"), [key]);
  const csv = await writePartitionsCsv(join(root, "partitions"));
  const assets = join(root, "out/assets");
  const meta = join(root, "out/meta");
  for (const board of BOARDS) {
    const build = await makeBuildDir(join(root, "raw", board), { version, embed: [key.pub] });
    const collected = await collect(["--board", board, "--version", version, "--build", build, "--assets", assets, "--meta", meta, "--keys", keys, "--partitions", csv]);
    assert.equal(collected, 0);
  }
  const argv = ["--assets", assets, "--meta", meta, "--tag", `v${version}`, "--repo", RELEASE_REPO, "--key-id", key.id, "--key-env", "KEY", "--pub", join(keys, `release-${key.id}.pub.b64`)];
  return { root, keys, assets, meta, key, argv, env: { KEY: key.pem } };
}

test("sign writes a manifest whose signatures verify, plus install.json and SHA256SUMS", async () => {
  const r = await release("1.1.0");
  assert.equal(await sign(r.argv, r.env), 0);
  const manifest = JSON.parse(await readFile(join(r.assets, "manifest.json"), "utf8"));
  assert.equal(manifest.version, "1.1.0");
  assert.deepEqual(Object.keys(manifest.boards), [...BOARDS]);
  for (const board of BOARDS) {
    const entry = manifest.boards[board];
    const image = await readFile(join(r.assets, `openmausbot-gadget-${board}-1.1.0.bin`));
    assert.equal(entry.url, `https://github.com/aivsomkar/openmausbot-gadget-sdk/releases/download/v1.1.0/openmausbot-gadget-${board}-1.1.0.bin`);
    assert.equal(entry.size, image.length);
    assert.equal(entry.sha256, createHash("sha256").update(image).digest("hex"));
    assert.equal(entry.key_id, "r1");
    const sig = new Uint8Array(Buffer.from(entry.sig, "base64"));
    assert.equal(Buffer.from(sig).toString("base64"), entry.sig);
    assert.equal(verifyP256(r.key.pub, firmwareText(board, "1.1.0", entry.size, entry.sha256), sig), true);
    assert.equal(verifyP256(r.key.pub, firmwareText("lcd-154" === board ? "devkit" : "lcd-154", "1.1.0", entry.size, entry.sha256), sig), false);
  }
  const install = JSON.parse(await readFile(join(r.assets, "install.json"), "utf8"));
  assert.equal(install.version, "1.1.0");
  assert.equal(install.boards.devkit.full, "openmausbot-gadget-devkit-1.1.0-full.bin");

  const sums = await readFile(join(r.assets, "SHA256SUMS"), "utf8");
  const lines = sums.trimEnd().split("\n");
  const names = (await readdir(r.assets)).filter((n) => n !== "SHA256SUMS").sort();
  assert.deepEqual(lines.map((l) => l.split("  ")[1]), names);
  for (const line of lines) {
    const [hash, name] = line.split("  ");
    assert.equal(hash, createHash("sha256").update(await readFile(join(r.assets, name))).digest("hex"));
  }
});

test("sign accepts a prerelease tag", async () => {
  const r = await release("1.2.0-rc.1");
  assert.equal(await sign(r.argv, r.env), 0);
});

test("sign refuses a secret whose public half is not the committed key", async () => {
  const r = await release("1.1.0");
  assert.equal(await sign(r.argv, { KEY: makeReleaseKey("r1").pem }), 1);
});

test("sign refuses an empty secret, a test key id and a -dev tag", async () => {
  const r = await release("1.1.0");
  assert.equal(await sign(r.argv, {}), 1);
  const t = [...r.argv];
  t[t.indexOf("--key-id") + 1] = "t1";
  assert.equal(await sign(t, r.env), 1);
  const d = [...r.argv];
  d[d.indexOf("--tag") + 1] = "v1.1.0-dev";
  assert.equal(await sign(d, r.env), 1);
});

test("sign refuses a stray file in the asset folder", async () => {
  const r = await release("1.1.0");
  await writeFile(join(r.assets, "notes.txt"), "hello");
  assert.equal(await sign(r.argv, r.env), 1);
});

test("sign refuses when a board is missing from the build", async () => {
  const r = await release("1.1.0");
  const argv = [...r.argv, "--boards", "amoled-175c,amoled-175,lcd-154,devkit,extra-board"];
  assert.equal(await sign(argv, r.env), 1);
});

test("sign refuses a repository other than the one the manifest URLs are pinned to", async () => {
  const r = await release("1.1.0");
  const argv = [...r.argv];
  argv[argv.indexOf("--repo") + 1] = "other/name";
  assert.equal(await sign(argv, r.env), 1);
});
```

- [ ] **Step 2: Run them and watch them fail**

Run: `node --test tools/release/test/sign.test.ts`
Expected: FAIL with `ERR_MODULE_NOT_FOUND` for `tools/release/sign.ts`.

- [ ] **Step 3: Write `sign.ts`**

`tools/release/sign.ts`:

```ts
// SPDX-License-Identifier: Apache-2.0
// Signs every board's app image with the release key and writes manifest.json,
// install.json and SHA256SUMS next to the assets (spec §4.8, §8; contract §4.1–§4.3).
// Runs only in release.yml's `release` job, inside the `release` environment.
//   GADGET_RELEASE_KEY_R1="$(cat key.pem)" node tools/release/sign.ts --assets out/assets \
//     --meta out/meta --tag v1.1.0 --repo aivsomkar/openmausbot-gadget-sdk \
//     --key-id r1 --key-env GADGET_RELEASE_KEY_R1 --pub keys/release-r1.pub.b64
import { createPrivateKey, createPublicKey, sign } from "node:crypto";
import { readFile, readdir, writeFile } from "node:fs/promises";
import { join } from "node:path";
import { parseArgs } from "node:util";
import { firmwareText } from "../../protocol/lib/identity.ts";
import { verifyP256 } from "../../protocol/lib/verify.ts";
import { fail, runIfMain } from "./cli.ts";
import { readMetas } from "./install-json.ts";
import {
  BOARDS,
  RELEASE_KEY_ID_RE,
  RELEASE_REPO,
  assetNames,
  buildInstallIndex,
  manifestUrl,
  parseReleasePubFiles,
  parseTag,
  sha256Hex,
  sha256sumsText,
  type Manifest,
} from "./lib.ts";

export async function main(argv: string[], env: NodeJS.ProcessEnv = process.env): Promise<number> {
  const { values } = parseArgs({
    args: argv,
    options: {
      assets: { type: "string" },
      meta: { type: "string" },
      tag: { type: "string" },
      repo: { type: "string" },
      "key-id": { type: "string" },
      "key-env": { type: "string" },
      pub: { type: "string" },
      boards: { type: "string", default: BOARDS.join(",") },
    },
    strict: true,
  });
  const { assets, meta, tag, repo, pub } = values;
  const keyId = values["key-id"];
  const keyEnv = values["key-env"];
  if (!assets || !meta || !tag || !repo || !keyId || !keyEnv || !pub) {
    return fail("sign", "--assets, --meta, --tag, --repo, --key-id, --key-env and --pub are required");
  }
  if (repo !== RELEASE_REPO) {
    return fail("sign", `--repo is ${repo}, but manifest URLs are pinned to ${RELEASE_REPO} (contract §4.1)`);
  }
  let version: string;
  let prerelease: boolean;
  try {
    ({ version, prerelease } = parseTag(tag));
  } catch (err) {
    return fail("sign", (err as Error).message);
  }
  if (!RELEASE_KEY_ID_RE.test(keyId)) return fail("sign", `key id ${keyId} is not a release key id (/^r[0-9]+$/)`);

  const pem = env[keyEnv];
  if (!pem) return fail("sign", `${keyEnv} is empty: the release environment holds this secret (docs/release-keys.md)`);
  let privateKey;
  try {
    privateKey = createPrivateKey(pem);
  } catch {
    return fail("sign", `${keyEnv} is not a PEM private key`);
  }
  if (privateKey.asymmetricKeyType !== "ec" || privateKey.asymmetricKeyDetails?.namedCurve !== "prime256v1") {
    return fail("sign", `${keyEnv} is not a P-256 key`);
  }
  const pubFromSecret = createPublicKey(privateKey).export({ type: "spki", format: "der" }).subarray(-65);
  const pubName = pub.split("/").pop() ?? pub;
  let committed: Map<string, string>;
  try {
    committed = parseReleasePubFiles([{ name: pubName, text: await readFile(pub, "utf8") }]);
  } catch (err) {
    return fail("sign", (err as Error).message);
  }
  const committedHex = committed.get(keyId);
  if (committedHex === undefined) return fail("sign", `${pub} is not keys/release-${keyId}.pub.b64`);
  if (pubFromSecret.toString("hex") !== committedHex) {
    return fail("sign", `the public half of ${keyEnv} does not match ${pub}`);
  }
  const pub65 = new Uint8Array(Buffer.from(committedHex, "hex"));

  const boards = values.boards.split(",").filter(Boolean);
  const metas = await readMetas(meta);
  const metaBoards = metas.map((m) => m.board).sort();
  if (JSON.stringify(metaBoards) !== JSON.stringify([...boards].sort())) {
    return fail("sign", `built boards ${metaBoards.join(", ")} differ from --boards ${boards.join(", ")}`);
  }

  const expected = new Set<string>();
  for (const board of boards) for (const name of Object.values(assetNames(board, version))) expected.add(name);
  const present = (await readdir(assets)).sort();
  const stray = present.filter((n) => !expected.has(n));
  const missing = [...expected].filter((n) => !present.includes(n));
  if (stray.length > 0 || missing.length > 0) {
    return fail("sign", `asset folder mismatch: unexpected [${stray.join(", ")}], missing [${missing.join(", ")}]`);
  }

  const manifest: Manifest = { version, boards: {} };
  for (const board of boards) {
    const name = assetNames(board, version).app;
    const image = new Uint8Array(await readFile(join(assets, name)));
    const sha256 = sha256Hex(image);
    const text = firmwareText(board, version, image.length, sha256);
    const der = new Uint8Array(sign("sha256", Buffer.from(text, "utf8"), privateKey));
    if (!verifyP256(pub65, text, der)) return fail("sign", `${board}: the new signature does not verify with ${pub}`);
    manifest.boards[board] = {
      url: manifestUrl(repo, board, version),
      size: image.length,
      sha256,
      sig: Buffer.from(der).toString("base64"),
      key_id: keyId,
    };
  }
  let install;
  try {
    install = buildInstallIndex(version, metas);
  } catch (err) {
    return fail("sign", (err as Error).message);
  }
  await writeFile(join(assets, "manifest.json"), `${JSON.stringify(manifest, null, 2)}\n`);
  await writeFile(join(assets, "install.json"), `${JSON.stringify(install, null, 2)}\n`);
  const files = await Promise.all(
    (await readdir(assets))
      .filter((n) => n !== "SHA256SUMS")
      .map(async (name) => ({ name, sha256: sha256Hex(new Uint8Array(await readFile(join(assets, name)))) })),
  );
  await writeFile(join(assets, "SHA256SUMS"), sha256sumsText(files));
  console.log(`sign: ${tag} (${prerelease ? "prerelease" : "release"}), ${boards.length} boards signed with ${keyId}`);
  return 0;
}

runIfMain(import.meta.url, (argv) => main(argv));
```

- [ ] **Step 4: Run the tests**

Run: `node --test tools/release/test/sign.test.ts`
Expected: `# pass 7` and `# fail 0`.

- [ ] **Step 5: Commit**

```bash
git add tools/release/sign.ts tools/release/test/sign.test.ts
git commit -m "feat(release): sign app images and write manifest.json, install.json and SHA256SUMS" \
  -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

### Task 4: Release key table check, generator and key docs

**Files:**
- Create: `tools/release/keytable.c`
- Create: `tools/release/check-keys.ts`
- Create: `tools/release/gen-release-keys.ts`
- Create: `keys/README.md`
- Create: `docs/release-keys.md`
- Test: `tools/release/test/check-keys.test.ts`

**Interfaces:**
- Consumes: contract §2.13 `gadget_release_key_t`, `gadget_release_keys[]`/`_count`, `gadget_test_keys[]`/`_count` from P2a's `firmware/core/include/gadget_ota.h`, `firmware/core/src/keys_release.c` and `keys_test.c`; Task 1 `checkKeyTables`, `parseReleasePubFiles`, `keysReleaseC`.
- Produces: `node tools/release/check-keys.ts [--root .] [--cc cc]` (exit 0 only when the release table holds ≥ 1 key, every id matches `/^r[0-9]+$/` and equals `keys/release-<id>.pub.b64`, every such file is in the table, and the test table is empty without `GADGET_TEST_KEYS`); `node tools/release/gen-release-keys.ts [--root .]` writes `firmware/core/src/keys_release.c` (contract §5.1: "P2d fills `keys_release.c` once r1 exists"). Both read only the files at the top of `keys/`, so the leak procedure's `keys/retired/` folder is ignored.

- [ ] **Step 1: Write the failing tests**

`tools/release/test/check-keys.test.ts`:

```ts
// SPDX-License-Identifier: Apache-2.0
import assert from "node:assert/strict";
import { cp, mkdir, readFile, writeFile } from "node:fs/promises";
import { join } from "node:path";
import { test } from "node:test";
import { fileURLToPath } from "node:url";
import { main as checkKeys } from "../check-keys.ts";
import { main as genKeys } from "../gen-release-keys.ts";
import { makeKeysDir, makeReleaseKey, tempDir } from "./helpers.ts";

const repo = fileURLToPath(new URL("../../../", import.meta.url));
const SENTINEL = [
  '#include "gadget_ota.h"',
  "const gadget_release_key_t gadget_release_keys[] = {{NULL, {0}}};",
  "const size_t gadget_release_keys_count = 0;",
  "",
].join("\n");

/** A root holding core's real headers and keys_test.c, a chosen keys_release.c and keys/. */
async function root(keysRelease: string | null, keys = [makeReleaseKey("r1")]) {
  const dir = await tempDir("omb-keys-root-");
  await cp(join(repo, "firmware/core/include"), join(dir, "firmware/core/include"), { recursive: true });
  await mkdir(join(dir, "firmware/core/src"), { recursive: true });
  await cp(join(repo, "firmware/core/src/keys_test.c"), join(dir, "firmware/core/src/keys_test.c"));
  await makeKeysDir(join(dir, "keys"), keys);
  if (keysRelease !== null) await writeFile(join(dir, "firmware/core/src/keys_release.c"), keysRelease);
  return dir;
}

test("check-keys fails on the empty sentinel table (no release key yet)", async () => {
  const dir = await root(SENTINEL);
  assert.equal(await checkKeys(["--root", dir]), 1);
});

test("gen-release-keys output passes check-keys", async () => {
  const dir = await root(null, [makeReleaseKey("r1"), makeReleaseKey("r2")]);
  assert.equal(await genKeys(["--root", dir]), 0);
  assert.match(await readFile(join(dir, "firmware/core/src/keys_release.c"), "utf8"), /gadget_release_keys_count = 2;/);
  assert.equal(await checkKeys(["--root", dir]), 0);
});

test("check-keys fails when a committed public key changes after generation", async () => {
  const dir = await root(null);
  assert.equal(await genKeys(["--root", dir]), 0);
  await makeKeysDir(join(dir, "keys"), [makeReleaseKey("r1")]);
  assert.equal(await checkKeys(["--root", dir]), 1);
});

test("a keys/retired/ folder is ignored", async () => {
  const dir = await root(null);
  await mkdir(join(dir, "keys/retired"), { recursive: true });
  await writeFile(join(dir, "keys/retired/release-r9.pub.b64"), "x\n");
  assert.equal(await genKeys(["--root", dir]), 0);
  assert.equal(await checkKeys(["--root", dir]), 0);
});
```

- [ ] **Step 2: Run them and watch them fail**

Run: `node --test tools/release/test/check-keys.test.ts`
Expected: FAIL with `ERR_MODULE_NOT_FOUND` for `tools/release/check-keys.ts`.

- [ ] **Step 3: Write the key-table printer**

`tools/release/keytable.c`:

```c
/* SPDX-License-Identifier: Apache-2.0 */
/* Prints firmware/core's key tables as one JSON line for
 * tools/release/check-keys.ts. Compiled without GADGET_TEST_KEYS, exactly
 * as a release build compiles core (contract §2.13). */
#include <stdio.h>

#include "gadget_ota.h"

static void print_id(const char *id) {
  if (id == NULL) {
    fputs("null", stdout);
    return;
  }
  putchar('"');
  for (const char *p = id; *p != '\0'; p++) {
    unsigned char c = (unsigned char)*p;
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_') {
      putchar(c);
    } else {
      printf("\\u%04x", c);
    }
  }
  putchar('"');
}

static void print_table(const gadget_release_key_t *table, size_t count) {
  putchar('[');
  for (size_t i = 0; i < count; i++) {
    if (i > 0) putchar(',');
    fputs("{\"id\":", stdout);
    print_id(table[i].id);
    fputs(",\"pub\":\"", stdout);
    for (size_t j = 0; j < GADGET_PUBKEY_LEN; j++) printf("%02x", table[i].pub[j]);
    fputs("\"}", stdout);
  }
  putchar(']');
}

int main(void) {
  fputs("{\"release\":", stdout);
  print_table(gadget_release_keys, gadget_release_keys_count);
  fputs(",\"test\":", stdout);
  print_table(gadget_test_keys, gadget_test_keys_count);
  fputs("}\n", stdout);
  return 0;
}
```

- [ ] **Step 4: Write `check-keys.ts`**

`tools/release/check-keys.ts`:

```ts
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
```

- [ ] **Step 5: Write `gen-release-keys.ts`**

`tools/release/gen-release-keys.ts`:

```ts
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
```

- [ ] **Step 6: Run the tests**

Run: `node --test tools/release/test/check-keys.test.ts`
Expected: `# pass 4` and `# fail 0`. The tests compile P2a's real headers and `keys_test.c` with `cc`; a compile error here means P2a's key-table files differ from contract §2.13.

- [ ] **Step 7: Run the check on the branch as it is**

Run: `node tools/release/check-keys.ts; echo "exit=$?"`
Expected: `check-keys: the release key table is empty: generate r1 and run tools/release/gen-release-keys.ts (docs/release-keys.md)` and `exit=1`. This is correct until Omkar creates r1 (contract §4.5: release CI fails by design until then). Do **not** create a key yourself.

- [ ] **Step 8: Write `keys/README.md`**

`keys/README.md`:

```markdown
# keys/

Public keys for firmware signatures (spec §4.8, §8). Nothing secret lives here.

| File | What |
|---|---|
| `test-t1.key.hex` | The **test** signing key's private scalar (64 lowercase hex). Committed on purpose: the fake host and the tests sign OTA images with it. It is compiled only into simulator and test builds; every board's `sdkconfig.defaults` turns `CONFIG_GADGET_TEST_KEYS` off, and release CI refuses an image that contains it |
| `test-t1.pub.b64` | Its public key: base64 of the 65-byte SEC1 point, then a newline |
| `release-r1.pub.pem` | The release key `r1`'s public half, PEM |
| `release-r1.pub.b64` | The same key as base64 of the 65-byte SEC1 point, then a newline. `firmware/core/src/keys_release.c` is generated from these files |

Release private keys never enter the repository. Each lives only as a secret of the GitHub Actions environment `release` (`GADGET_RELEASE_KEY_R1`), plus Omkar's offline backup. [docs/release-keys.md](../docs/release-keys.md) explains how to create and rotate them.
```

- [ ] **Step 9: Write `docs/release-keys.md`**

`docs/release-keys.md`:

````markdown
# Release keys and the release workflow

Official firmware images are signed with a P-256 release key (spec §4.8, §8). Gadgets accept an over-the-air update only when its signature verifies with a release key compiled into their firmware, and MausBot checks the same signature before it offers the update. This page is Omkar's one-time setup and the rotation procedure. Until step 3 is done, `release.yml` fails on purpose: the release key table is empty.

## 1. GitHub settings (once)

1. **Pages:** Settings → Pages → Build and deployment → Source: **GitHub Actions**.
2. **Release environment:** Settings → Environments → New environment → `release`.
   - Required reviewers: Omkar (and anyone else who may approve a release).
   - Deployment branches and tags: **Selected branches and tags** → add a **tag** rule `v*`. Without it, tag runs cannot use the environment.
3. Leave the `github-pages` environment on its default (main only). `release.yml` dispatches `pages.yml` on `main`, so the installer redeploys from main.

## 2. Create the r1 key (once, on a trusted computer)

Use OpenSSL 3 (`brew install openssl@3`; macOS's LibreSSL also works):

```sh
cd /path/to/openmausbot-gadget-sdk
umask 077
openssl genpkey -algorithm EC -pkeyopt ec_paramgen_curve:P-256 -out ~/release-r1.pem
openssl pkey -in ~/release-r1.pem -pubout -out keys/release-r1.pub.pem
openssl pkey -in ~/release-r1.pem -pubout -outform DER | tail -c 65 | openssl base64 -A > keys/release-r1.pub.b64
echo >> keys/release-r1.pub.b64
gh secret set GADGET_RELEASE_KEY_R1 --env release --repo aivsomkar/openmausbot-gadget-sdk < ~/release-r1.pem
```

Move `~/release-r1.pem` to offline storage (a password manager or an encrypted drive), then delete the local copy. Anyone with it can sign firmware that every gadget accepts.

## 3. Compile the key into the firmware

```sh
node tools/release/gen-release-keys.ts
node tools/release/check-keys.ts
```

`check-keys.ts` must print `check-keys: release keys r1; test table empty`. Commit `keys/release-r1.pub.pem`, `keys/release-r1.pub.b64` and `firmware/core/src/keys_release.c` together.

MausBot needs the same public key: copy the one line of `keys/release-r1.pub.b64` into OpenMausBot's `companion/src/gadget/release-keys.ts` (`RELEASE_KEYS`, plan P4b).

## 4. Cut a release

0. **Before the first public release only:** confirm that the mascot expression geometry (`src/components/cursor-face-data.ts` in OpenMausBot at the pinned commit `6dd4403d8fbbbd5c17169724cb2a529f11d7543e`, which `tools/art` generates the Maus art from) is project-owned (spec §11). This is a pre-publish check, not a build step: nothing in CI checks it.
1. Make sure `main` is green.
2. Tag and push. A first dry run as a prerelease is a good idea:

   ```sh
   git tag v1.0.0-rc.1 && git push origin v1.0.0-rc.1
   ```

3. In the Actions tab, approve the `release` job when it waits for review. It signs the images, publishes the release with the per-board assets, `manifest.json`, `install.json` and `SHA256SUMS`, and dispatches `pages.yml`.
4. Tags with a `-` are prereleases: they never become "latest", so neither MausBot nor the installer sees them. Tag `v1.0.0` when the prerelease looks right.

What the workflow refuses: a tag that is not `v<major>.<minor>.<patch>[-<pre>]`, a version ending in `-dev`, an image whose embedded version is not the tag's, an app larger than the 6 MiB OTA slot, a built partition table that differs from `firmware/ports/esp32/partitions/16mb.csv` or a flashed part that reaches into `nvs` or `phy_init` (a reinstall would wipe the gadget's pairing), an image that contains the test key `t1` or lacks a release key, a secret whose public half differs from `keys/release-r1.pub.b64`, a repository other than `aivsomkar/openmausbot-gadget-sdk` (the manifest URLs are pinned to it; a rename or transfer means changing `RELEASE_REPO` here and `RELEASE_URL_PREFIX` in MausBot together), and a release key table that is empty or holds a non-`r` id.

## Rotating to a new key

Gadgets trust every key in their firmware's table, so rotate in two releases:

1. Create `r2` exactly as in step 2 (secret `GADGET_RELEASE_KEY_R2`, files `keys/release-r2.pub.*`), run step 3 (the table now holds r1 and r2), and add r2 to OpenMausBot's `RELEASE_KEYS`. Release as usual: this release is still signed with r1, and teaches gadgets r2.
2. Once most gadgets run that release, change the `release` job in `.github/workflows/release.yml` to `--key-id r2 --key-env GADGET_RELEASE_KEY_R2 --pub keys/release-r2.pub.b64` with `GADGET_RELEASE_KEY_R2` in its `env`, and release again.

## If a key leaks

Current gadgets trust only the keys in their firmware, so the leaked key has to sign one last release that drops it:

1. Create `r2` as in step 2 (secret `GADGET_RELEASE_KEY_R2`, files `keys/release-r2.pub.*`).
2. Retire r1: `mkdir -p keys/retired && git mv keys/release-r1.pub.pem keys/release-r1.pub.b64 keys/retired/`, then run step 3. The table now holds only r2; `gen-release-keys.ts`, `check-keys.ts` and `collect.ts` read only the files at the top of `keys/` and skip `keys/retired/`.
3. In `.github/workflows/release.yml`, point the `release` job's `--pub` at `keys/retired/release-r1.pub.b64` and release. This release is still signed with r1, so gadgets and MausBot accept it, and it teaches gadgets to trust only r2. Add r2 to OpenMausBot's `RELEASE_KEYS` in the same round.
4. Switch the `release` job to `--key-id r2 --key-env GADGET_RELEASE_KEY_R2 --pub keys/release-r2.pub.b64` (with `GADGET_RELEASE_KEY_R2` in its `env`), delete the `GADGET_RELEASE_KEY_R1` secret, and remove r1 from OpenMausBot's `RELEASE_KEYS`.

v1 has no revocation: until a gadget installs the release from step 3, whoever holds the leaked key can sign images it accepts. Ask users to update, or to reflash over USB.
````

- [ ] **Step 10: Commit**

```bash
git add tools/release/keytable.c tools/release/check-keys.ts tools/release/gen-release-keys.ts \
        tools/release/test/check-keys.test.ts keys/README.md docs/release-keys.md
git commit -m "feat(release): release key-table check, generator and key docs" \
  -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

### Task 5: Installer package and the console module

**Files:**
- Create: `site/package.json`, `site/package-lock.json` (generated), `site/tsconfig.json`
- Create: `site/src/console.ts`
- Create: `tools/release/tsconfig.json`
- Create: `site/test/awkward-values.json`, `site/test/split-argv.c`
- Test: `site/test/console.test.ts`, `site/test/console-firmware.test.ts`

**Interfaces:**
- Consumes: contract §2.11 console grammar and `@omb` lines; §4.8 pinned exports; P2a's `firmware/core/src/console.c` (`gadget_console_split`) and `firmware/core/include/gadget_console.h`, compiled with `cc` by the firmware round-trip test. `console.c` calls `gadget_pair_code_valid` and `gadget_utf8_len` from `util.c`; the harness stubs those two, because the splitter never calls them and `util.c` needs P2a's crypto HAL.
- Produces (`site/src/console.ts`, pinned by contract §4.8): `type OmbMessage` (union of `boot`, `status`, `scan`, `hosts`, `say`, `error`), `parseOmbLine(line): OmbMessage | null`, `createLineSplitter(): (chunk: string) => string[]`, `quoteArg(value): string` (throws `RangeError` on CR/LF), `setupCommands({code, ssid, password, address?}): string[]` (throws `RangeError` on invalid input). P2d additions: `type WifiState`, `PairState`, `WifiAuth`; `wifiProblem(ssid, password): string | null`; `normalizePairCode(text): string | null`; `normalizeHostAddress(text): string | null`; `isDownloadModeLine(line): boolean`.
- Produces: `npm test` = `tsc -p tsconfig.json && node --test "test/*.test.ts"`; `npm run build`; `npm run serve`. Test fixtures `site/test/awkward-values.json` (the SSIDs and passwords quoting must survive) and `site/test/split-argv.c` (splits each stdin line with the firmware's `gadget_console_split` and prints one JSON array per line, `null` for an unterminated quote); Task 11's Python test uses both.

- [ ] **Step 1: Write the package and compiler settings**

`site/package.json`:

```json
{
  "name": "openmausbot-gadget-installer",
  "private": true,
  "type": "module",
  "license": "Apache-2.0",
  "engines": { "node": ">=22.18" },
  "scripts": {
    "typecheck": "tsc -p tsconfig.json",
    "test": "tsc -p tsconfig.json && node --test \"test/*.test.ts\"",
    "build": "node scripts/build.ts",
    "serve": "esbuild --servedir=dist --serve=127.0.0.1:8080"
  },
  "devDependencies": {
    "@types/node": "24.19.1",
    "@types/spark-md5": "3.0.5",
    "@types/w3c-web-serial": "1.0.8",
    "esbuild": "0.28.2",
    "esptool-js": "0.7.0",
    "spark-md5": "3.0.2",
    "typescript": "5.9.3"
  }
}
```

`site/tsconfig.json`:

```json
{
  "compilerOptions": {
    "target": "ES2022",
    "lib": ["ES2022", "DOM", "DOM.Iterable"],
    "module": "preserve",
    "moduleResolution": "bundler",
    "allowImportingTsExtensions": true,
    "verbatimModuleSyntax": true,
    "erasableSyntaxOnly": true,
    "esModuleInterop": true,
    "noEmit": true,
    "strict": true,
    "noUncheckedIndexedAccess": true,
    "skipLibCheck": true,
    "types": ["node", "w3c-web-serial"]
  },
  "include": ["src/**/*.ts", "test/**/*.ts", "scripts/**/*.ts"]
}
```

The release tools are type-checked with the site's compiler and `@types/node`:

`tools/release/tsconfig.json`:

```json
{
  "compilerOptions": {
    "target": "ES2022",
    "lib": ["ES2022"],
    "module": "nodenext",
    "moduleResolution": "nodenext",
    "allowImportingTsExtensions": true,
    "verbatimModuleSyntax": true,
    "erasableSyntaxOnly": true,
    "noEmit": true,
    "strict": true,
    "skipLibCheck": true,
    "types": ["node"],
    "typeRoots": ["../../site/node_modules/@types"]
  },
  "include": ["*.ts", "test/*.ts"]
}
```

- [ ] **Step 2: Install and lock the dependencies**

Run: `cd site && npm install --no-audit --no-fund && cd ..`
Expected: `added 11 packages`, and a new `site/package-lock.json`. `site/node_modules/` and `site/dist/` are already ignored by P1's `.gitignore`; check with `git check-ignore site/node_modules site/dist/` (prints both paths and exits 0). Keep the trailing slash: `site/dist` does not exist yet, and a directory-only pattern such as `site/dist/` matches a missing path only when it is spelled as a directory.

- [ ] **Step 3: Write the failing tests**

`site/test/console.test.ts`:

```ts
// SPDX-License-Identifier: Apache-2.0
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { test } from "node:test";
import {
  createLineSplitter,
  isDownloadModeLine,
  normalizeHostAddress,
  normalizePairCode,
  parseOmbLine,
  quoteArg,
  setupCommands,
  wifiProblem,
} from "../src/console.ts";

/** SSIDs and passwords the console quoting must survive (Review Focus 1); console-firmware.test.ts and tools/console use the same file. */
const AWKWARD = JSON.parse(readFileSync(new URL("./awkward-values.json", import.meta.url), "utf8")) as string[];

/** A JavaScript copy of the firmware's argument splitter rules (contract §2.11); console-firmware.test.ts checks the real one. */
function splitArgv(line: string): string[] | null {
  const args: string[] = [];
  let cur = "";
  let inArg = false;
  let quoted = false;
  for (let i = 0; i < line.length; i++) {
    const c = line[i] as string;
    if (c === "\\" && i + 1 < line.length && ["\\", '"', " "].includes(line[i + 1] as string)) {
      cur += line[++i];
      inArg = true;
    } else if (c === '"') {
      quoted = !quoted;
      inArg = true;
    } else if (/\s/.test(c) && !quoted) {
      if (inArg) args.push(cur);
      cur = "";
      inArg = false;
    } else {
      cur += c;
      inArg = true;
    }
  }
  if (quoted) return null;
  if (inArg) args.push(cur);
  return args;
}

test("parseOmbLine reads every contract example", () => {
  const lines = [
    '@omb {"op":"boot","board":"amoled-175c","fw":"1.0.0","id":"gad_3f9a0c2b7e41d856"}',
    '@omb {"op":"status","wifi":"connected","ssid":"Home","host":"192.168.1.20:8810","id":"gad_3f9a0c2b7e41d856","pair":"paired","fw":"1.0.0","battery":{"pct":82,"charging":false},"board":"amoled-175c","name":"Desk Maus","host_name":"Omkar\'s computer"}',
    '@omb {"op":"status","wifi":"connecting","ssid":"Home","id":"gad_3f9a0c2b7e41d856","pair":"error","error":"bad_code","fw":"1.0.0","board":"lcd-154","name":"Maus 3f9a"}',
    '@omb {"op":"scan","networks":[{"ssid":"Home","rssi":-52,"auth":"wpa2"},{"ssid":"Cafe","rssi":-80,"auth":"open"}]}',
    '@omb {"op":"hosts","hosts":[{"name":"Omkar\'s computer","address":"192.168.1.20:8810","id":"0123456789abcdef0123456789abcdef"}]}',
    '@omb {"op":"say","turn":"t3f9a0c2b-7"}',
    '@omb {"op":"error","cmd":"pair","message":"pair needs a six-digit code"}',
  ];
  assert.deepEqual(lines.map((l) => parseOmbLine(l)?.op), ["boot", "status", "status", "scan", "hosts", "say", "error"]);
  const status = parseOmbLine(lines[2] as string);
  assert.equal(status?.op === "status" && status.error, "bad_code");
});

test("parseOmbLine strips ANSI colour and a trailing CR, and ignores the rest of the log", () => {
  assert.equal(parseOmbLine('\u001b[0;32m@omb {"op":"say","turn":"t1-1"}\u001b[0m\r')?.op, "say");
  assert.equal(parseOmbLine("\u001b[0;32mI (812) wifi: connected\u001b[0m"), null);
  assert.equal(parseOmbLine("I (812) main: @omb is not at the start"), null);
  assert.equal(parseOmbLine("@omb {not json"), null);
  assert.equal(parseOmbLine('@omb {"op":"status","wifi":"connec'), null);
  assert.equal(parseOmbLine('@omb {"op":"weather","temp":20}'), null);
  assert.equal(parseOmbLine('@omb {"op":"status","wifi":"maybe","id":"x","pair":"paired","fw":"1"}'), null);
  assert.equal(parseOmbLine('@omb ["op","status"]'), null);
  assert.equal(parseOmbLine('@omb {"op":"status","wifi":"off","id":"x","pair":"unpaired","fw":"1","extra":true}')?.op, "status");
});

test("the line splitter handles CR, LF, CRLF, and lines cut across USB packets", () => {
  const split = createLineSplitter();
  assert.deepEqual(split('@omb {"op":"sa'), []);
  assert.deepEqual(split('y","turn":"t1-1"}\r'), ['@omb {"op":"say","turn":"t1-1"}']);
  assert.deepEqual(split("\nnext\r\nthird\n"), ["next", "third"]);
  assert.deepEqual(split("a\n\nb\r\r"), ["a", "", "b", ""]);
  assert.deepEqual(split("é😀 partial"), []);
  assert.deepEqual(split("\n"), ["é😀 partial"]);
});

test("quoteArg round-trips awkward SSIDs and passwords through the firmware's splitter", () => {
  for (const v of AWKWARD) {
    const line = `wifi ${quoteArg(v)} ${quoteArg("pass word")}`;
    assert.deepEqual(splitArgv(line), ["wifi", v, "pass word"], JSON.stringify(v));
  }
  assert.throws(() => quoteArg("line\nbreak"), RangeError);
  assert.throws(() => quoteArg("cr\rhere"), RangeError);
});

test("setupCommands sends pair, then wifi, then host", () => {
  assert.deepEqual(setupCommands({ code: "123456", ssid: "Home Net", password: "hunter22" }), [
    "pair 123456",
    'wifi "Home Net" "hunter22"',
    "host auto",
  ]);
  assert.deepEqual(setupCommands({ code: "123456", ssid: "Cafe", password: "", address: "http://192.168.1.20:8810/" })[2], "host 192.168.1.20:8810");
  assert.throws(() => setupCommands({ code: "12345", ssid: "Home", password: "" }), RangeError);
  assert.throws(() => setupCommands({ code: "123456", ssid: "Home", password: "short" }), RangeError);
});

test("wifiProblem mirrors the console's wifi rules", () => {
  assert.equal(wifiProblem("Home", ""), null);
  assert.equal(wifiProblem("Home", "12345678"), null);
  assert.equal(wifiProblem("Home", "a".repeat(63)), null);
  assert.equal(wifiProblem("Home", "0123456789abcdef".repeat(4)), null);
  assert.match(wifiProblem("Home", "1234567") ?? "", /8 to 63/);
  assert.match(wifiProblem("Home", "z".repeat(64)) ?? "", /8 to 63/);
  assert.match(wifiProblem("", "") ?? "", /1 to 32/);
  assert.match(wifiProblem("ü".repeat(17), "") ?? "", /1 to 32/);
  assert.match(wifiProblem("Home", "pass\nword") ?? "", /line breaks/);
});

test("pairing codes and host addresses are normalized from what people type", () => {
  assert.equal(normalizePairCode(" 123 456 "), "123456");
  assert.equal(normalizePairCode("123-456"), "123456");
  assert.equal(normalizePairCode("12345"), null);
  assert.equal(normalizePairCode("１２３４５６"), null);
  assert.equal(normalizeHostAddress(" 192.168.1.20:8810 "), "192.168.1.20:8810");
  assert.equal(normalizeHostAddress("http://192.168.1.20:8810/gadget"), "192.168.1.20:8810");
  assert.equal(normalizeHostAddress("omkars-mac.local"), "omkars-mac.local");
  assert.equal(normalizeHostAddress("192.168.1.20:0"), null);
  assert.equal(normalizeHostAddress("192.168.1.20:70000"), null);
  assert.equal(normalizeHostAddress("my mac"), null);
  assert.equal(normalizeHostAddress("[fe80::1]:8810"), null);
});

test("download-mode banners are recognised", () => {
  assert.equal(isDownloadModeLine("rst:0x15 (USB_UART_CHIP_RESET),boot:0x0 (DOWNLOAD(USB/UART0))"), true);
  assert.equal(isDownloadModeLine("waiting for download"), true);
  assert.equal(isDownloadModeLine("rst:0x15 (USB_UART_CHIP_RESET),boot:0x8 (SPI_FAST_FLASH_BOOT)"), false);
});
```

One shared list, so the JavaScript, firmware and Python checks can't drift apart:

`site/test/awkward-values.json`:

```json
["Home", "My Home Wi-Fi", "say \"hi\"", "back\\slash", "trailing\\", "\\\"", "", "Café ☕ 5G", "  spaces  ", "tab\there", "a\\ b", "\"quoted\""]
```

The JavaScript `splitArgv` above is a copy of the firmware's rules. If the two disagree (tabs, `\ `, a quote in the middle of an argument), the installer would send lines the gadget splits differently, so this test runs the same lines through P2a's real `gadget_console_split`.

`site/test/split-argv.c`:

```c
/* SPDX-License-Identifier: Apache-2.0 */
/* Test harness: splits each stdin line with the firmware's own
 * gadget_console_split (firmware/core/src/console.c) and prints the arguments
 * as one JSON array per line, or null for an unterminated quote or too many
 * arguments. Used by site/test/console-firmware.test.ts and
 * tools/console/test_omb_console.py. */
#include <stdio.h>
#include <string.h>

#include "gadget_console.h"
#include "gadget_util.h"

/* console.c's grammar calls these two util.c validators; the splitter never
 * does. Stubbing them keeps util.c and its crypto HAL out of this build. */
bool gadget_pair_code_valid(const char *code) {
  (void)code;
  return false;
}
size_t gadget_utf8_len(const char *s) { return strlen(s); }

static void put_json(const char *s) {
  putchar('"');
  for (const unsigned char *p = (const unsigned char *)s; *p != '\0'; p++) {
    if (*p == '"' || *p == '\\') {
      printf("\\%c", *p);
    } else if (*p < 0x20) {
      printf("\\u%04x", *p);
    } else {
      putchar(*p);
    }
  }
  putchar('"');
}

int main(void) {
  static char line[GADGET_CONSOLE_LINE_MAX];
  while (fgets(line, sizeof line, stdin) != NULL) {
    line[strcspn(line, "\r\n")] = '\0';
    char *argv[GADGET_CONSOLE_ARGV_MAX];
    int argc = gadget_console_split(line, argv, (int)GADGET_CONSOLE_ARGV_MAX);
    if (argc < 0 || argc > (int)GADGET_CONSOLE_ARGV_MAX) {
      puts("null");
      continue;
    }
    putchar('[');
    for (int i = 0; i < argc; i++) {
      if (i > 0) putchar(',');
      put_json(argv[i]);
    }
    puts("]");
  }
  return 0;
}
```

`site/test/console-firmware.test.ts`:

```ts
// SPDX-License-Identifier: Apache-2.0
// Review Focus 1: what quoteArg writes must split back exactly with the
// firmware's own splitter (P2a's gadget_console_split), not only with the
// JavaScript copy in console.test.ts. Compiled the way check-keys.ts compiles
// the key tables; skipped with a message when there is no C compiler.
import assert from "node:assert/strict";
import { execFileSync, spawnSync } from "node:child_process";
import { mkdtempSync, readFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { test } from "node:test";
import { fileURLToPath } from "node:url";
import { quoteArg } from "../src/console.ts";

const repo = fileURLToPath(new URL("../../", import.meta.url));
const cc = process.env.CC ?? "cc";
const hasCc = spawnSync(cc, ["--version"], { stdio: "ignore" }).error === undefined;
const AWKWARD = JSON.parse(readFileSync(new URL("./awkward-values.json", import.meta.url), "utf8")) as string[];

test("quoteArg output splits back exactly with the firmware's gadget_console_split", { skip: hasCc ? false : `no C compiler (${cc})` }, () => {
  const exe = join(mkdtempSync(join(tmpdir(), "omb-split-")), "split-argv");
  execFileSync(
    cc,
    [
      "-std=c11",
      "-Wall",
      "-Wextra",
      "-Werror",
      "-I",
      join(repo, "firmware/core/include"),
      fileURLToPath(new URL("./split-argv.c", import.meta.url)),
      join(repo, "firmware/core/src/console.c"),
      "-o",
      exe,
    ],
    { stdio: ["ignore", "inherit", "inherit"] },
  );
  const lines = AWKWARD.map((v) => `wifi ${quoteArg(v)} ${quoteArg("pass word")}`);
  const out = execFileSync(exe, { input: `${lines.join("\n")}\n`, encoding: "utf8" }).trimEnd().split("\n");
  assert.equal(out.length, AWKWARD.length);
  AWKWARD.forEach((v, i) => assert.deepEqual(JSON.parse(out[i] as string), ["wifi", v, "pass word"], JSON.stringify(v)));
});
```

- [ ] **Step 4: Run them and watch them fail**

Run: `cd site && npm test; cd ..`
Expected: FAIL: `error TS2307: Cannot find module '../src/console.ts'`.

- [ ] **Step 5: Write `console.ts`**

`site/src/console.ts`:

```ts
// SPDX-License-Identifier: Apache-2.0
// The gadget's USB console as the installer sees it (spec §5.6, contract §2.11, §4.8).
// Pure functions only: no Web Serial here, so Node tests cover all of it.

export type WifiState = "off" | "connecting" | "connected" | "failed";
export type PairState = "unpaired" | "code_stored" | "connecting" | "paired" | "error";
export type WifiAuth = "open" | "wep" | "wpa" | "wpa2" | "wpa3" | "wpa2-ent" | "other";

export type OmbMessage =
  | { op: "boot"; board: string; fw: string; id: string }
  | {
      op: "status";
      wifi: WifiState;
      ssid?: string;
      host?: string;
      id: string;
      pair: PairState;
      error?: string;
      fw: string;
      battery?: { pct: number; charging: boolean };
      board?: string;
      name?: string;
      host_name?: string;
    }
  | { op: "scan"; networks: Array<{ ssid: string; rssi: number; auth: WifiAuth }> }
  | { op: "hosts"; hosts: Array<{ name: string; address: string; id: string }> }
  | { op: "say"; turn: string }
  | { op: "error"; cmd: string; message: string };

const OMB_PREFIX = "@omb ";
// CSI sequences (colours, cursor moves) that ESP-IDF's log adds around lines.
const ANSI_RE = /\u001b\[[0-9;?]*[ -/]*[@-~]/g;
const WIFI: readonly string[] = ["off", "connecting", "connected", "failed"];
const PAIR: readonly string[] = ["unpaired", "code_stored", "connecting", "paired", "error"];
const AUTH: readonly string[] = ["open", "wep", "wpa", "wpa2", "wpa3", "wpa2-ent", "other"];

function isObj(v: unknown): v is Record<string, unknown> {
  return typeof v === "object" && v !== null && !Array.isArray(v);
}
const isStr = (v: unknown): v is string => typeof v === "string";
const optStr = (v: unknown): boolean => v === undefined || typeof v === "string";

/** One `@omb` line → message. Strips ANSI first; null for every other line, bad JSON and unknown ops. */
export function parseOmbLine(line: string): OmbMessage | null {
  const clean = line.replace(ANSI_RE, "").replace(/\r$/, "");
  if (!clean.startsWith(OMB_PREFIX)) return null;
  let v: unknown;
  try {
    v = JSON.parse(clean.slice(OMB_PREFIX.length));
  } catch {
    return null;
  }
  if (!isObj(v) || !isStr(v.op)) return null;
  switch (v.op) {
    case "boot":
      return isStr(v.board) && isStr(v.fw) && isStr(v.id) ? (v as OmbMessage) : null;
    case "status": {
      if (!isStr(v.wifi) || !WIFI.includes(v.wifi) || !isStr(v.id) || !isStr(v.pair) || !PAIR.includes(v.pair) || !isStr(v.fw)) return null;
      if (!optStr(v.ssid) || !optStr(v.host) || !optStr(v.error) || !optStr(v.board) || !optStr(v.name) || !optStr(v.host_name)) return null;
      if (v.battery !== undefined && !(isObj(v.battery) && typeof v.battery.pct === "number" && typeof v.battery.charging === "boolean")) return null;
      return v as OmbMessage;
    }
    case "scan":
      return Array.isArray(v.networks) &&
        v.networks.every((n) => isObj(n) && isStr(n.ssid) && typeof n.rssi === "number" && isStr(n.auth) && AUTH.includes(n.auth))
        ? (v as OmbMessage)
        : null;
    case "hosts":
      return Array.isArray(v.hosts) && v.hosts.every((h) => isObj(h) && isStr(h.name) && isStr(h.address) && isStr(h.id))
        ? (v as OmbMessage)
        : null;
    case "say":
      return isStr(v.turn) ? (v as OmbMessage) : null;
    case "error":
      return isStr(v.cmd) && isStr(v.message) ? (v as OmbMessage) : null;
    default:
      return null;
  }
}

/** Returns a feeder: give it raw text chunks, get back complete lines. CR, LF and CRLF each end one line. */
export function createLineSplitter(): (chunk: string) => string[] {
  let buf = "";
  let lastCr = false;
  return (chunk: string): string[] => {
    const lines: string[] = [];
    for (const ch of chunk) {
      if (ch === "\n") {
        if (lastCr) {
          lastCr = false;
          continue;
        }
        lines.push(buf);
        buf = "";
      } else if (ch === "\r") {
        lines.push(buf);
        buf = "";
        lastCr = true;
        continue;
      } else {
        buf += ch;
      }
      lastCr = false;
    }
    return lines;
  };
}

/** One console argument in esp_console_split_argv quoting: `"…"` with `\\` and `\"`. Throws on CR or LF. */
export function quoteArg(value: string): string {
  if (/[\r\n]/.test(value)) throw new RangeError("console arguments cannot contain line breaks");
  return `"${value.replace(/\\/g, "\\\\").replace(/"/g, '\\"')}"`;
}

/** In this order: pair <code>, wifi "<ssid>" "<password>", host auto (or host <address>). */
export function setupCommands(input: { code: string; ssid: string; password: string; address?: string }): string[] {
  if (!/^\d{6}$/.test(input.code)) throw new RangeError("the pairing code is six digits");
  const problem = wifiProblem(input.ssid, input.password);
  if (problem !== null) throw new RangeError(problem);
  const host = input.address === undefined ? "host auto" : `host ${checkedAddress(input.address)}`;
  return [`pair ${input.code}`, `wifi ${quoteArg(input.ssid)} ${quoteArg(input.password)}`, host];
}

const utf8Length = (s: string): number => new TextEncoder().encode(s).length;

/** The console's `wifi` rules (contract §2.11) as a sentence for the person, or null when fine. */
export function wifiProblem(ssid: string, password: string): string | null {
  if (/[\r\n]/.test(ssid) || /[\r\n]/.test(password)) return "Network names and passwords can't contain line breaks.";
  const s = utf8Length(ssid);
  if (s < 1 || s > 32) return "The network name must be 1 to 32 bytes long.";
  if (password === "") return null;
  if (/^[0-9a-fA-F]{64}$/.test(password)) return null;
  const p = utf8Length(password);
  if (p < 8 || p > 63) return "The password must be 8 to 63 characters, 64 hex digits, or empty for an open network.";
  return null;
}

/** Six digits from what the person typed ("123 456", " 123-456 "), or null. */
export function normalizePairCode(text: string): string | null {
  const digits = text.replace(/[\s-]/g, "");
  return /^\d{6}$/.test(digits) ? digits : null;
}

/** `host` argument from what the person typed, or null: hostname or IPv4, optional port 1–65535. */
export function normalizeHostAddress(text: string): string | null {
  let t = text.trim().replace(/^(?:https?|wss?):\/\//i, "");
  t = t.replace(/\/.*$/, "");
  const m = /^([A-Za-z0-9](?:[A-Za-z0-9.-]{0,251}[A-Za-z0-9])?)(?::(\d{1,5}))?$/.exec(t);
  const host = m?.[1];
  if (m === null || host === undefined) return null;
  const port = m[2];
  if (port === undefined) return host;
  const n = Number(port);
  return n >= 1 && n <= 65535 ? `${host}:${n}` : null;
}

function checkedAddress(address: string): string {
  const a = normalizeHostAddress(address);
  if (a === null) throw new RangeError(`not a host address: ${address}`);
  return a;
}

/** The ROM's download-mode banner: the chip did not start the app. */
export function isDownloadModeLine(line: string): boolean {
  return /waiting for download/i.test(line) || /boot:0x[0-9a-f]+ \(DOWNLOAD/i.test(line);
}
```

- [ ] **Step 6: Run the tests and both type checks**

Run: `cd site && npm test && cd .. && site/node_modules/.bin/tsc -p tools/release/tsconfig.json && echo release-tools-typecheck-ok`
Expected: `# pass 9`, `# fail 0`, then `release-tools-typecheck-ok`. A type error inside `protocol/lib/*.ts` belongs to P1: report it there. If only "quoteArg output splits back exactly with the firmware's gadget_console_split" fails, `quoteArg` and P2a's `gadget_console_split` disagree on the value it names: compare the line with contract §2.11's quoting rules and report it to P2a if the firmware breaks them. Never change the shared value list to make it pass. A compile error in `console.c` means P2a's file now calls more of `util.c` than the two stubs in `split-argv.c` cover.

- [ ] **Step 7: Commit**

```bash
git add site/package.json site/package-lock.json site/tsconfig.json site/src/console.ts site/test/console.test.ts \
        site/test/console-firmware.test.ts site/test/split-argv.c site/test/awkward-values.json tools/release/tsconfig.json
git commit -m "feat(site): installer package and @omb console module" \
  -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

### Task 6: `install.json` and the flash plan

**Files:**
- Create: `site/src/install.ts`
- Test: `site/test/install.test.ts`

**Interfaces:**
- Consumes: contract §4.2 format (produced by Tasks 2–3).
- Produces (pinned by contract §4.8): `interface InstallIndex`, `parseInstallIndex(json): InstallIndex` (throws on any shape error), `flashPlan(index, board): Array<{path, address}>` (address order; throws `RangeError` "no published firmware for <board>"). P2d additions: `FIRMWARE_BASE = "firmware/"`, `type FetchLike`, `loadInstallIndex(fetchFn, base?)`, `fetchPart(fetchFn, path, base?): Promise<Uint8Array>`.

- [ ] **Step 1: Write the failing tests**

`site/test/install.test.ts`:

```ts
// SPDX-License-Identifier: Apache-2.0
import assert from "node:assert/strict";
import { test } from "node:test";
import { fetchPart, flashPlan, loadInstallIndex, parseInstallIndex, type FetchLike } from "../src/install.ts";

const RELEASE = {
  version: "1.1.0",
  boards: {
    "amoled-175c": {
      parts: [
        { path: "openmausbot-gadget-amoled-175c-1.1.0-bootloader.bin", offset: "0x0" },
        { path: "openmausbot-gadget-amoled-175c-1.1.0-partition-table.bin", offset: "0x8000" },
        { path: "openmausbot-gadget-amoled-175c-1.1.0-ota-data-initial.bin", offset: "0xf000" },
        { path: "openmausbot-gadget-amoled-175c-1.1.0.bin", offset: "0x20000" },
      ],
      full: "openmausbot-gadget-amoled-175c-1.1.0-full.bin",
    },
  },
};

test("the contract's install.json parses and plans four writes in address order", () => {
  const index = parseInstallIndex(RELEASE);
  assert.deepEqual(flashPlan(index, "amoled-175c"), [
    { path: "openmausbot-gadget-amoled-175c-1.1.0-bootloader.bin", address: 0x0 },
    { path: "openmausbot-gadget-amoled-175c-1.1.0-partition-table.bin", address: 0x8000 },
    { path: "openmausbot-gadget-amoled-175c-1.1.0-ota-data-initial.bin", address: 0xf000 },
    { path: "openmausbot-gadget-amoled-175c-1.1.0.bin", address: 0x20000 },
  ]);
});

test("the plan never includes the merged -full.bin (it would erase NVS)", () => {
  const plan = flashPlan(parseInstallIndex(RELEASE), "amoled-175c");
  assert.equal(plan.some((p) => p.path.endsWith("-full.bin")), false);
  assert.equal(plan.some((p) => p.address === 0x9000), false);
});

test("before the first release the index is empty and every board is unpublished", () => {
  const index = parseInstallIndex({ version: null, boards: {} });
  assert.equal(index.version, null);
  assert.throws(() => flashPlan(index, "amoled-175c"), /no published firmware/);
});

test("parts listed out of order are still flashed in address order; uppercase hex is fine", () => {
  const shuffled = structuredClone(RELEASE);
  shuffled.boards["amoled-175c"].parts.reverse();
  shuffled.boards["amoled-175c"].parts[1] = { path: "openmausbot-gadget-amoled-175c-1.1.0-ota-data-initial.bin", offset: "0xF000" };
  assert.deepEqual(flashPlan(parseInstallIndex(shuffled), "amoled-175c").map((p) => p.address), [0, 0x8000, 0xf000, 0x20000]);
});

test("parseInstallIndex refuses broken or unsafe files", () => {
  const bad: unknown[] = [
    null,
    [],
    { version: "1.1", boards: {} },
    { version: "1.1.0", boards: [] },
    { version: null, boards: RELEASE.boards },
    { version: "1.1.0", boards: { "Amoled!": RELEASE.boards["amoled-175c"] } },
    { version: "1.1.0", boards: { x: { parts: [], full: "f.bin" } } },
    { version: "1.1.0", boards: { x: { parts: [{ path: "../../etc/passwd", offset: "0x0" }], full: "f.bin" } } },
    { version: "1.1.0", boards: { x: { parts: [{ path: "a.bin", offset: "0x0" }], full: "sub/f.bin" } } },
    { version: "1.1.0", boards: { x: { parts: [{ path: "a.bin", offset: "8000" }], full: "f.bin" } } },
    { version: "1.1.0", boards: { x: { parts: [{ path: "a.bin", offset: "0x8001" }], full: "f.bin" } } },
    { version: "1.1.0", boards: { x: { parts: [{ path: "a.bin", offset: "0x1000000" }], full: "f.bin" } } },
    { version: "1.1.0", boards: { x: { parts: [{ path: "a.bin", offset: "0x0" }, { path: "b.bin", offset: "0x000" }], full: "f.bin" } } },
    { version: "1.1.0", boards: { x: { parts: [{ path: "a.bin", offset: "0x0" }] } } },
  ];
  for (const json of bad) assert.throws(() => parseInstallIndex(json), Error, JSON.stringify(json));
});

function fakeFetch(files: Record<string, unknown>): FetchLike & { urls: string[] } {
  const urls: string[] = [];
  const f = async (url: string) => {
    urls.push(url);
    const body = files[url];
    return {
      ok: body !== undefined,
      status: body === undefined ? 404 : 200,
      json: async () => body,
      arrayBuffer: async () => (body instanceof Uint8Array ? body.slice().buffer : new ArrayBuffer(0)),
    };
  };
  return Object.assign(f, { urls });
}

test("the page loads install.json and parts from its own origin", async () => {
  const f = fakeFetch({ "firmware/install.json": RELEASE, "firmware/a.bin": new Uint8Array([1, 2, 3]) });
  assert.equal((await loadInstallIndex(f)).version, "1.1.0");
  assert.deepEqual(await fetchPart(f, "a.bin"), new Uint8Array([1, 2, 3]));
  assert.deepEqual(f.urls, ["firmware/install.json", "firmware/a.bin"]);
  await assert.rejects(fetchPart(f, "missing.bin"), /HTTP 404/);
  await assert.rejects(fetchPart(fakeFetch({ "firmware/e.bin": new Uint8Array(0) }), "e.bin"), /empty/);
  await assert.rejects(fetchPart(f, "../x.bin"), RangeError);
  await assert.rejects(loadInstallIndex(fakeFetch({})), /HTTP 404/);
});
```

- [ ] **Step 2: Run them and watch them fail**

Run: `cd site && npm test; cd ..`
Expected: FAIL: `error TS2307: Cannot find module '../src/install.ts'`.

- [ ] **Step 3: Write `install.ts`**

`site/src/install.ts`:

```ts
// SPDX-License-Identifier: Apache-2.0
// firmware/install.json (spec §5.8, contract §4.2): parse it strictly, then
// turn one board's entry into esptool-js write addresses.

export interface InstallIndex {
  version: string | null;
  boards: Record<string, { parts: Array<{ path: string; offset: string }>; full: string }>;
}

export const FIRMWARE_BASE = "firmware/";
const VERSION_RE = /^\d+\.\d+\.\d+(-[0-9A-Za-z.]+)?$/;
const BOARD_RE = /^[a-z0-9-]{1,32}$/;
const FILE_RE = /^[A-Za-z0-9][A-Za-z0-9._-]*$/;
const OFFSET_RE = /^0x[0-9a-fA-F]{1,8}$/;
const FLASH_LIMIT = 16 * 1024 * 1024;
const SECTOR = 0x1000;

class InstallIndexError extends Error {
  constructor(message: string) {
    super(`install.json: ${message}`);
    this.name = "InstallIndexError";
  }
}

function isObj(v: unknown): v is Record<string, unknown> {
  return typeof v === "object" && v !== null && !Array.isArray(v);
}

/** Throws on any shape error; the page treats a throw as "the published firmware is broken". */
export function parseInstallIndex(json: unknown): InstallIndex {
  if (!isObj(json)) throw new InstallIndexError("not an object");
  const { version, boards } = json;
  if (version !== null && !(typeof version === "string" && VERSION_RE.test(version))) throw new InstallIndexError("bad version");
  if (!isObj(boards)) throw new InstallIndexError("boards is not an object");
  if (version === null && Object.keys(boards).length > 0) throw new InstallIndexError("boards without a version");
  const out: InstallIndex = { version, boards: {} };
  for (const [board, entry] of Object.entries(boards)) {
    if (!BOARD_RE.test(board)) throw new InstallIndexError(`bad board id ${JSON.stringify(board)}`);
    if (!isObj(entry) || !Array.isArray(entry.parts) || typeof entry.full !== "string" || !FILE_RE.test(entry.full)) {
      throw new InstallIndexError(`${board}: needs parts and full`);
    }
    if (entry.parts.length < 1 || entry.parts.length > 8) throw new InstallIndexError(`${board}: 1 to 8 parts`);
    const seen = new Set<number>();
    const parts = entry.parts.map((p: unknown) => {
      if (!isObj(p) || typeof p.path !== "string" || !FILE_RE.test(p.path) || typeof p.offset !== "string" || !OFFSET_RE.test(p.offset)) {
        throw new InstallIndexError(`${board}: bad part ${JSON.stringify(p)}`);
      }
      const address = Number.parseInt(p.offset, 16);
      if (address % SECTOR !== 0 || address >= FLASH_LIMIT) throw new InstallIndexError(`${board}: offset ${p.offset} is not a 4 KiB sector in 16 MB`);
      if (seen.has(address)) throw new InstallIndexError(`${board}: offset ${p.offset} twice`);
      seen.add(address);
      return { path: p.path, offset: p.offset };
    });
    out.boards[board] = { parts, full: entry.full };
  }
  return out;
}

/** The parts to write for one board, in address order. Throws when the release has no build for it. */
export function flashPlan(index: InstallIndex, board: string): Array<{ path: string; address: number }> {
  const entry = index.boards[board];
  if (entry === undefined) throw new RangeError(`no published firmware for ${board}`);
  return entry.parts
    .map((p) => ({ path: p.path, address: Number.parseInt(p.offset, 16) }))
    .sort((a, b) => a.address - b.address);
}

export type FetchLike = (url: string, init?: { cache?: "no-store" }) => Promise<{
  ok: boolean;
  status: number;
  json(): Promise<unknown>;
  arrayBuffer(): Promise<ArrayBuffer>;
}>;

export async function loadInstallIndex(fetchFn: FetchLike, base = FIRMWARE_BASE): Promise<InstallIndex> {
  const res = await fetchFn(`${base}install.json`, { cache: "no-store" });
  if (!res.ok) throw new Error(`install.json: HTTP ${res.status}`);
  return parseInstallIndex(await res.json());
}

export async function fetchPart(fetchFn: FetchLike, path: string, base = FIRMWARE_BASE): Promise<Uint8Array> {
  if (!FILE_RE.test(path)) throw new RangeError(`bad part name ${path}`);
  const res = await fetchFn(`${base}${path}`, { cache: "no-store" });
  if (!res.ok) throw new Error(`${path}: HTTP ${res.status}`);
  const bytes = new Uint8Array(await res.arrayBuffer());
  if (bytes.length === 0) throw new Error(`${path}: empty file`);
  return bytes;
}
```

- [ ] **Step 4: Run the tests**

Run: `cd site && npm test; cd ..`
Expected: `# pass 15` and `# fail 0`.

- [ ] **Step 5: Commit**

```bash
git add site/src/install.ts site/test/install.test.ts
git commit -m "feat(site): parse install.json and plan separate-part flashing" \
  -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

### Task 7: Flashing through esptool-js

**Files:**
- Create: `site/src/errors.ts`
- Create: `site/src/flash.ts`
- Create: `site/test/fakes-esptool.ts`
- Test: `site/test/flash.test.ts`

**Interfaces:**
- Consumes: Task 6 `flashPlan`, `InstallIndex`; `FlashOptions` type from esptool-js 0.7.0.
- Produces: `class InstallerError extends Error { code: InstallerErrorCode }` with codes `port_busy | port_lost | wrong_chip | flash_too_small | board_not_published | download_failed | write_failed | console_write_timeout`; `interface LoaderLike` (the slice of `ESPLoader` used: `chip.CHIP_NAME`, `main`, `detectFlashSize`, `writeFlash`, `writeReg`, `readReg(addr): Promise<number>`); `type FlashStage = "downloading" | "connecting" | "writing" | "done"`; `interface FlashDeps` (with optional `onWarning(message)`); `REQUIRED_FLASH_MB = 16`; `flashSizeMb(size): number | null`; `disableWatchdogs(loader): Promise<void>`; `flashBoard(deps): Promise<{bytes}>`. Test doubles `FakeLoader` (an esptool-js mock that records calls, answers `readReg`, and drives `reportProgress`/`calculateMD5Hash`) and `FakeTransport`.
- Why the watchdogs: after the USB-Serial-JTAG core reset that puts the S3 into download mode, the RTC watchdog and the super watchdog can still be running and reset the chip in the middle of a write. Espressif's Python esptool switches both off before flashing over USB-Serial-JTAG (`esp32s3.py`); esptool-js 0.7.0 does not (research R7 #4). The register addresses and keys are the S3's, from that file: `RTC_CNTL_WDTWPROTECT_REG` 0x600080B0 (key 0x50D83AA1), `RTC_CNTL_WDTCONFIG0_REG` 0x60008098, `RTC_CNTL_SWD_WPROTECT_REG` 0x600080B8 (key 0x8F1D312A), `RTC_CNTL_SWD_CONF_REG` 0x600080B4 (bit 31 `SWD_AUTO_FEED_EN`).

- [ ] **Step 1: Write the esptool-js test doubles**

`site/test/fakes-esptool.ts`:

```ts
// SPDX-License-Identifier: Apache-2.0
// Test doubles for esptool-js: a loader that records calls and a transport.
import type { FlashOptions } from "esptool-js";
import type { LoaderLike } from "../src/flash.ts";

export class FakeLoader implements LoaderLike {
  chip = { CHIP_NAME: "ESP32-S3" };
  flashSize: string | undefined = "16MB";
  writeFlashError: Error | null = null;
  /** What readReg returns for RTC_CNTL_SWD_CONF_REG (0x600080B4); every other register reads 0. */
  swdConf = 0x12;
  calls: string[] = [];
  written: FlashOptions | null = null;
  md5s: string[] = [];
  constructor(log: string[] = []) {
    this.calls = log;
  }
  async main(mode?: string): Promise<string> {
    this.calls.push(`main ${mode ?? ""}`.trim());
    return this.chip.CHIP_NAME;
  }
  async detectFlashSize(): Promise<string | undefined> {
    this.calls.push("detectFlashSize");
    return this.flashSize;
  }
  async writeFlash(options: FlashOptions): Promise<void> {
    this.calls.push(`writeFlash eraseAll=${options.eraseAll}`);
    this.written = options;
    options.fileArray.forEach((f, i) => {
      options.reportProgress?.(i, 0, f.data.length);
      if (options.calculateMD5Hash) this.md5s.push(options.calculateMD5Hash(f.data));
      options.reportProgress?.(i, f.data.length, f.data.length);
    });
    if (this.writeFlashError) throw this.writeFlashError;
  }
  async writeReg(addr: number, value: number, mask?: number): Promise<void> {
    this.calls.push(`writeReg 0x${addr.toString(16)} ${value} ${mask ?? 0xffffffff}`);
  }
  async readReg(addr: number): Promise<number> {
    this.calls.push(`readReg 0x${addr.toString(16)}`);
    return addr === 0x600080b4 ? this.swdConf : 0;
  }
}

export class FakeTransport {
  readonly log: string[];
  constructor(log: string[]) {
    this.log = log;
  }
  async setDTR(state: boolean): Promise<void> {
    this.log.push(`setDTR ${state}`);
  }
  async setRTS(state: boolean): Promise<void> {
    this.log.push(`setRTS ${state}`);
  }
  async disconnect(): Promise<void> {
    this.log.push("disconnect");
  }
}
```

- [ ] **Step 2: Write the failing tests**

`site/test/flash.test.ts`:

```ts
// SPDX-License-Identifier: Apache-2.0
import assert from "node:assert/strict";
import { test } from "node:test";
import { InstallerError } from "../src/errors.ts";
import { flashBoard, flashSizeMb, type FlashDeps, type FlashStage } from "../src/flash.ts";
import { parseInstallIndex } from "../src/install.ts";
import { FakeLoader } from "./fakes-esptool.ts";

const index = parseInstallIndex({
  version: "1.1.0",
  boards: {
    "lcd-154": {
      parts: [
        { path: "b.bin", offset: "0x0" },
        { path: "p.bin", offset: "0x8000" },
        { path: "o.bin", offset: "0xf000" },
        { path: "a.bin", offset: "0x20000" },
      ],
      full: "f.bin",
    },
  },
});
const sizes: Record<string, number> = { "b.bin": 100, "p.bin": 50, "o.bin": 50, "a.bin": 800 };
/** A writeReg call as FakeLoader records it (no mask → 0xffffffff). */
const reg = (addr: number, value: number) => `writeReg 0x${addr.toString(16)} ${value} ${0xffffffff}`;

function deps(loader: FakeLoader, over: Partial<FlashDeps> = {}) {
  const stages: FlashStage[] = [];
  const progress: number[] = [];
  const fetched: string[] = [];
  const warnings: string[] = [];
  const d: FlashDeps = {
    loader,
    index,
    board: "lcd-154",
    eraseAll: false,
    fetchPart: async (path) => {
      fetched.push(path);
      loader.calls.push(`fetch ${path}`);
      return new Uint8Array(sizes[path] ?? 0).fill(1);
    },
    md5: (data) => `md5:${data.length}`,
    onStage: (s) => stages.push(s),
    onProgress: (f) => progress.push(f),
    onWarning: (m) => warnings.push(m),
    ...over,
  };
  return { d, stages, progress, fetched, warnings };
}

test("flashBoard downloads every part, checks the board, turns the watchdogs off, then writes the separate parts", async () => {
  const loader = new FakeLoader();
  const { d, stages, progress, warnings } = deps(loader);
  assert.deepEqual(await flashBoard(d), { bytes: 1000 });
  assert.deepEqual(loader.calls, [
    "fetch b.bin",
    "fetch p.bin",
    "fetch o.bin",
    "fetch a.bin",
    "main default_reset",
    "detectFlashSize",
    reg(0x600080b0, 0x50d83aa1), // unlock the RTC watchdog
    reg(0x60008098, 0), //           RTC watchdog off
    reg(0x600080b0, 0), //           lock it again
    reg(0x600080b8, 0x8f1d312a), // unlock the super watchdog
    "readReg 0x600080b4",
    reg(0x600080b4, 0x80000012), //  SWD_AUTO_FEED_EN, other bits kept
    reg(0x600080b8, 0), //           lock it again
    "writeFlash eraseAll=false",
  ]);
  assert.deepEqual(warnings, []);
  const w = loader.written;
  assert.ok(w);
  assert.deepEqual(w.fileArray.map((f) => f.address), [0x0, 0x8000, 0xf000, 0x20000]);
  assert.equal(w.flashMode, "keep");
  assert.equal(w.flashFreq, "keep");
  assert.equal(w.flashSize, "keep");
  assert.equal(w.compress, true);
  assert.deepEqual(loader.md5s, ["md5:100", "md5:50", "md5:50", "md5:800"]);
  assert.deepEqual(stages, ["downloading", "connecting", "writing", "done"]);
  assert.equal(progress.at(-1), 1);
  assert.ok(progress.every((p, i) => i === 0 || p >= (progress[i - 1] ?? 0)), "progress never goes backwards");
});

test("Erase everything passes eraseAll to esptool-js", async () => {
  const loader = new FakeLoader();
  await flashBoard(deps(loader, { eraseAll: true }).d);
  assert.equal(loader.written?.eraseAll, true);
});

test("a failed download never puts the board into download mode", async () => {
  const loader = new FakeLoader();
  const { d } = deps(loader, {
    fetchPart: async (p) => {
      if (p === "a.bin") throw new Error("HTTP 404");
      return new Uint8Array(10);
    },
  });
  await assert.rejects(flashBoard(d), (e: unknown) => e instanceof InstallerError && e.code === "download_failed");
  assert.equal(loader.calls.includes("main default_reset"), false);
});

test("a board that is not an ESP32-S3 is refused before writing", async () => {
  const loader = new FakeLoader();
  loader.chip.CHIP_NAME = "ESP32";
  await assert.rejects(flashBoard(deps(loader).d), (e: unknown) => e instanceof InstallerError && e.code === "wrong_chip");
  assert.equal(loader.written, null);
});

test("an 8 MB board is refused; an unknown flash size is allowed", async () => {
  const small = new FakeLoader();
  small.flashSize = "8MB";
  await assert.rejects(flashBoard(deps(small).d), (e: unknown) => e instanceof InstallerError && e.code === "flash_too_small");
  assert.equal(small.written, null);
  const unknown = new FakeLoader();
  unknown.flashSize = undefined;
  await flashBoard(deps(unknown).d);
  assert.ok(unknown.written);
});

test("a write that fails mid-way surfaces as write_failed", async () => {
  const loader = new FakeLoader();
  loader.writeFlashError = new Error("Timeout");
  await assert.rejects(flashBoard(deps(loader).d), (e: unknown) => e instanceof InstallerError && e.code === "write_failed" && /Timeout/.test(e.message));
});

test("a board that refuses the watchdog registers is still flashed, with a warning", async () => {
  const loader = new FakeLoader();
  loader.writeReg = async () => {
    throw new Error("Timeout waiting for response");
  };
  const { d, warnings } = deps(loader);
  await flashBoard(d);
  assert.ok(loader.written);
  assert.equal(warnings.length, 1);
  assert.match(warnings[0] ?? "", /watchdog/);
});

test("a board missing from the release is refused before any download", async () => {
  const loader = new FakeLoader();
  const { d, fetched } = deps(loader, { board: "devkit" });
  await assert.rejects(flashBoard(d), (e: unknown) => e instanceof InstallerError && e.code === "board_not_published");
  assert.deepEqual(fetched, []);
});

test("flashSizeMb parses esptool-js sizes", () => {
  assert.equal(flashSizeMb("16MB"), 16);
  assert.equal(flashSizeMb("32MB"), 32);
  assert.equal(flashSizeMb("512KB"), 0.5);
  assert.equal(flashSizeMb(undefined), null);
  assert.equal(flashSizeMb("detect"), null);
});
```

- [ ] **Step 3: Run them and watch them fail**

Run: `cd site && npm test; cd ..`
Expected: FAIL: `error TS2307: Cannot find module '../src/flash.ts'` (and `../src/errors.ts`).

- [ ] **Step 4: Write `errors.ts`**

`site/src/errors.ts`:

```ts
// SPDX-License-Identifier: Apache-2.0
export type InstallerErrorCode =
  | "port_busy"
  | "port_lost"
  | "wrong_chip"
  | "flash_too_small"
  | "board_not_published"
  | "download_failed"
  | "write_failed"
  | "console_write_timeout";

export class InstallerError extends Error {
  readonly code: InstallerErrorCode;
  constructor(code: InstallerErrorCode, message: string) {
    super(message);
    this.code = code;
    this.name = "InstallerError";
  }
}
```

- [ ] **Step 5: Write `flash.ts`**

`site/src/flash.ts`:

```ts
// SPDX-License-Identifier: Apache-2.0
// Flashing one board with esptool-js (spec §5.8 step 2). Downloads every part
// first, then connects, checks the chip and flash size, turns the chip's
// watchdogs off, and writes the separate parts so NVS (identity, Wi-Fi,
// pairing) survives.
import type { FlashOptions } from "esptool-js";
import { InstallerError } from "./errors.ts";
import { flashPlan, type InstallIndex } from "./install.ts";

/** The slice of esptool-js's ESPLoader the installer uses (tests pass a mock). */
export interface LoaderLike {
  chip: { CHIP_NAME: string };
  main(mode?: "default_reset"): Promise<string>;
  detectFlashSize(): Promise<string | undefined>;
  writeFlash(options: FlashOptions): Promise<void>;
  writeReg(addr: number, value: number, mask?: number): Promise<void>;
  readReg(addr: number): Promise<number>;
}

export type FlashStage = "downloading" | "connecting" | "writing" | "done";

export interface FlashDeps {
  loader: LoaderLike;
  index: InstallIndex;
  board: string;
  eraseAll: boolean;
  fetchPart(path: string): Promise<Uint8Array>;
  md5(data: Uint8Array): string;
  onStage(stage: FlashStage): void;
  onProgress(fraction: number): void;
  /** Something went wrong that does not stop the install (shown in the console log). */
  onWarning?(message: string): void;
}

export const REQUIRED_FLASH_MB = 16;

// ESP32-S3 RTC watchdog and super watchdog registers (Espressif's esptool, esp32s3.py).
const RTC_CNTL_WDTCONFIG0_REG = 0x60008098;
const RTC_CNTL_WDTWPROTECT_REG = 0x600080b0;
const RTC_CNTL_SWD_CONF_REG = 0x600080b4;
const RTC_CNTL_SWD_WPROTECT_REG = 0x600080b8;
const RTC_CNTL_WDT_WKEY = 0x50d83aa1;
const RTC_CNTL_SWD_WKEY = 0x8f1d312a;
const RTC_CNTL_SWD_AUTO_FEED_EN = 0x80000000;

/**
 * Turns off the RTC watchdog and lets the super watchdog feed itself, as
 * Python esptool does before flashing an S3 over USB-Serial-JTAG: the core
 * reset into download mode can leave both running, and either one would reset
 * the chip in the middle of the write. esptool-js 0.7.0 does not do this.
 */
export async function disableWatchdogs(loader: LoaderLike): Promise<void> {
  await loader.writeReg(RTC_CNTL_WDTWPROTECT_REG, RTC_CNTL_WDT_WKEY);
  await loader.writeReg(RTC_CNTL_WDTCONFIG0_REG, 0);
  await loader.writeReg(RTC_CNTL_WDTWPROTECT_REG, 0);
  await loader.writeReg(RTC_CNTL_SWD_WPROTECT_REG, RTC_CNTL_SWD_WKEY);
  const swd = await loader.readReg(RTC_CNTL_SWD_CONF_REG);
  await loader.writeReg(RTC_CNTL_SWD_CONF_REG, (swd | RTC_CNTL_SWD_AUTO_FEED_EN) >>> 0);
  await loader.writeReg(RTC_CNTL_SWD_WPROTECT_REG, 0);
}

/** "16MB" → 16, "512KB" → 0.5, anything else → null. */
export function flashSizeMb(size: string | undefined): number | null {
  const m = /^(\d+)(KB|MB)$/.exec(size ?? "");
  if (!m) return null;
  return m[2] === "MB" ? Number(m[1]) : Number(m[1]) / 1024;
}

export async function flashBoard(deps: FlashDeps): Promise<{ bytes: number }> {
  let plan;
  try {
    plan = flashPlan(deps.index, deps.board);
  } catch {
    throw new InstallerError("board_not_published", `No firmware for ${deps.board} in this release.`);
  }
  deps.onStage("downloading");
  const fileArray: FlashOptions["fileArray"] = [];
  for (const part of plan) {
    try {
      fileArray.push({ address: part.address, data: await deps.fetchPart(part.path) });
    } catch (err) {
      throw new InstallerError("download_failed", `Couldn't download ${part.path}: ${(err as Error).message}`);
    }
  }
  const total = fileArray.reduce((n, f) => n + f.data.length, 0);

  deps.onStage("connecting");
  await deps.loader.main("default_reset");
  const chip = deps.loader.chip.CHIP_NAME;
  if (chip !== "ESP32-S3") throw new InstallerError("wrong_chip", `This board is an ${chip}, not an ESP32-S3.`);
  const mb = flashSizeMb(await deps.loader.detectFlashSize());
  if (mb !== null && mb < REQUIRED_FLASH_MB) {
    throw new InstallerError("flash_too_small", `This board has ${mb} MB of flash. The gadget firmware needs ${REQUIRED_FLASH_MB} MB.`);
  }
  try {
    await disableWatchdogs(deps.loader);
  } catch (err) {
    deps.onWarning?.(`Couldn't turn the chip's watchdogs off before writing (${(err as Error).message}); installing anyway.`);
  }

  deps.onStage("writing");
  const before = fileArray.map((_, i) => fileArray.slice(0, i).reduce((n, f) => n + f.data.length, 0));
  try {
    await deps.loader.writeFlash({
      fileArray,
      flashMode: "keep",
      flashFreq: "keep",
      flashSize: "keep",
      eraseAll: deps.eraseAll,
      compress: true,
      reportProgress: (fileIndex, written, size) => {
        const part = fileArray[fileIndex]?.data.length ?? 0;
        const done = (before[fileIndex] ?? 0) + (size > 0 ? (written / size) * part : 0);
        deps.onProgress(Math.min(1, total > 0 ? done / total : 1));
      },
      calculateMD5Hash: (image) => deps.md5(image),
    });
  } catch (err) {
    throw new InstallerError("write_failed", `Writing the flash failed: ${(err as Error).message}`);
  }
  deps.onProgress(1);
  deps.onStage("done");
  return { bytes: total };
}
```

- [ ] **Step 6: Run the tests**

Run: `cd site && npm test; cd ..`
Expected: `# pass 24` and `# fail 0`.

- [ ] **Step 7: Commit**

```bash
git add site/src/errors.ts site/src/flash.ts site/test/fakes-esptool.ts site/test/flash.test.ts
git commit -m "feat(site): flash separate parts through an injected esptool-js loader" \
  -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

### Task 8: Reset into the app and the raw console

**Files:**
- Create: `site/src/reset.ts`
- Create: `site/src/serial-console.ts`
- Create: `site/test/fakes-serial.ts`
- Test: `site/test/reset.test.ts`, `site/test/serial-console.test.ts`

**Interfaces:**
- Consumes: Task 5 `createLineSplitter`, `parseOmbLine`, `OmbMessage`; Task 7 `InstallerError`, `FakeLoader`, `FakeTransport`.
- Produces (`reset.ts`): `RTC_CNTL_OPTION1_REG = 0x6000812c`, `USB_JTAG = {usbVendorId: 0x303a, usbProductId: 0x1001}`, `CONSOLE_BAUD = 115200`, `interface SerialPortLike`, `interface SerialLike {getPorts()}`, `interface ResetDeps`, `isUsbJtag(port)`, `resetToApp(deps): Promise<SerialPortLike>` (contract §4.8 sequence), `openConsolePort(port, serial, sleep, waitMs, others?): Promise<SerialPortLike>` (`others`: the other boards' ports, never opened; default every port listed on entry except `port`; Deviations recorded during the build, item 1). `openConsolePort` decides `port_busy` against `port_lost` by presence: Chrome rejects `open()` with the same `NetworkError` "Failed to open serial port." when another program holds the port and when the device is gone, so the message says nothing. `port_busy` means a port that `serial.getPorts()` still lists refused to open on the last pass; `port_lost` means no matching port was listed.
- Produces (`serial-console.ts`): `interface ConsoleEvent {line, msg}`, `interface ConsoleIO {send(line), drain(), lost}`, `interface ConsoleSession extends ConsoleIO {close()}`, `class SerialConsole implements ConsoleSession` (`lastOutputAt`, `onEvent`, `finished`, `close()`).
- Produces (tests): `FakePort` (`emit`, `emitBytes`, `unplug`, `sent`, `openFailures`, `signals`), `fakeSerial(ports)`, `fakeClock(start?)`.

- [ ] **Step 1: Write the Web Serial test doubles**

`site/test/fakes-serial.ts`:

```ts
// SPDX-License-Identifier: Apache-2.0
// Test doubles for Web Serial: a port you can push output into, a
// navigator.serial stand-in and a virtual clock.
import type { SerialLike, SerialPortLike } from "../src/reset.ts";

/** A Web Serial port: push device output with emit(), read what the page wrote with sent(). */
export class FakePort implements SerialPortLike {
  readable: ReadableStream<Uint8Array> | null = null;
  writable: WritableStream<Uint8Array> | null = null;
  info: { usbVendorId?: number; usbProductId?: number };
  openFailures: Error[] = [];
  signals: Array<{ dataTerminalReady?: boolean; requestToSend?: boolean }> = [];
  opened = 0;
  private controller: ReadableStreamDefaultController<Uint8Array> | null = null;
  private written = "";
  constructor(info = { usbVendorId: 0x303a, usbProductId: 0x1001 }) {
    this.info = info;
  }
  async open(_options: { baudRate: number }): Promise<void> {
    const failure = this.openFailures.shift();
    if (failure) throw failure;
    this.opened++;
    this.readable = new ReadableStream<Uint8Array>({ start: (c) => void (this.controller = c) });
    this.writable = new WritableStream<Uint8Array>({ write: (chunk) => void (this.written += new TextDecoder().decode(chunk)) });
  }
  async close(): Promise<void> {
    this.readable = null;
    this.writable = null;
  }
  async setSignals(signals: { dataTerminalReady?: boolean; requestToSend?: boolean }): Promise<void> {
    this.signals.push(signals);
  }
  getInfo(): { usbVendorId?: number; usbProductId?: number } {
    return this.info;
  }
  /** One USB packet of raw bytes, which may end in the middle of a UTF-8 character. */
  emitBytes(bytes: Uint8Array): void {
    this.controller?.enqueue(bytes);
  }
  emit(text: string): void {
    this.emitBytes(new TextEncoder().encode(text));
  }
  unplug(): void {
    this.controller?.error(new Error("The device has been lost."));
  }
  sent(): string[] {
    return this.written.split("\n").filter((l) => l !== "");
  }
}

export function fakeSerial(ports: SerialPortLike[]): SerialLike {
  return { getPorts: async () => ports };
}

export function fakeClock(start = 0): { t: number; now(): number; sleep(ms: number): Promise<void> } {
  const clock = {
    t: start,
    now: () => clock.t,
    sleep: async (ms: number) => {
      clock.t += ms;
      await Promise.resolve();
    },
  };
  return clock;
}
```

- [ ] **Step 2: Write the failing tests**

`site/test/reset.test.ts`:

```ts
// SPDX-License-Identifier: Apache-2.0
import assert from "node:assert/strict";
import { test } from "node:test";
import { InstallerError } from "../src/errors.ts";
import { openConsolePort, resetToApp } from "../src/reset.ts";
import { FakeLoader, FakeTransport } from "./fakes-esptool.ts";
import { FakePort, fakeClock, fakeSerial } from "./fakes-serial.ts";

test("resetToApp clears FORCE_DOWNLOAD_BOOT, pulses RTS, disconnects, reopens at 115200 with DTR and RTS low", async () => {
  const log: string[] = [];
  const loader = new FakeLoader(log);
  const transport = new FakeTransport(log);
  const port = new FakePort();
  const clock = fakeClock();
  const sleep = async (ms: number) => {
    log.push(`sleep ${ms}`);
    await clock.sleep(ms);
  };
  const out = await resetToApp({ loader, transport, port, serial: fakeSerial([port]), sleep });
  assert.equal(out, port);
  assert.deepEqual(log, ["writeReg 0x6000812c 0 1", "setDTR false", "setRTS true", "sleep 100", "setRTS false", "disconnect"]);
  assert.equal(port.opened, 1);
  assert.deepEqual(port.signals, [{ dataTerminalReady: false, requestToSend: false }]);
});

test("resetToApp still resets when the stub no longer answers writeReg", async () => {
  const log: string[] = [];
  const loader = new FakeLoader(log);
  loader.writeReg = async () => {
    throw new Error("Timeout waiting for response");
  };
  const port = new FakePort();
  await resetToApp({ loader, transport: new FakeTransport(log), port, serial: fakeSerial([port]), sleep: fakeClock().sleep });
  assert.deepEqual(log.slice(0, 3), ["setDTR false", "setRTS true", "setRTS false"]);
});

/** What Chrome's open() rejects with both when another program holds the port and when the device is gone. */
const openFailed = () => Array.from({ length: 100 }, () => new Error("Failed to open serial port."));

test("after the USB device re-enumerates, the new 0x303A/0x1001 port is used", async () => {
  const old = new FakePort();
  old.openFailures = openFailed();
  const reborn = new FakePort();
  reborn.openFailures = [new Error("Failed to open serial port.")];
  const bridge = new FakePort({ usbVendorId: 0x10c4, usbProductId: 0xea60 });
  const out = await openConsolePort(old, fakeSerial([bridge, reborn]), fakeClock().sleep, 10_000);
  assert.equal(out, reborn);
  assert.equal(bridge.opened, 0);
  assert.deepEqual(reborn.signals, [{ dataTerminalReady: false, requestToSend: false }]);
});

test("a present port that refuses gives port_busy; a vanished board gives port_lost (same open() error)", async () => {
  // Busy: a serial monitor holds the port, so it stays in getPorts() and keeps refusing.
  const held = new FakePort();
  held.openFailures = openFailed();
  await assert.rejects(openConsolePort(held, fakeSerial([held]), fakeClock().sleep, 1000), (e: unknown) => e instanceof InstallerError && e.code === "port_busy");
  // Lost: the board never came back, so getPorts() lists nothing and the old port refuses.
  const gone = new FakePort();
  gone.openFailures = openFailed();
  await assert.rejects(openConsolePort(gone, fakeSerial([]), fakeClock().sleep, 1000), (e: unknown) => e instanceof InstallerError && e.code === "port_lost");
});
```

`site/test/serial-console.test.ts`:

```ts
// SPDX-License-Identifier: Apache-2.0
import assert from "node:assert/strict";
import { test } from "node:test";
import { InstallerError } from "../src/errors.ts";
import { SerialConsole } from "../src/serial-console.ts";
import { FakePort } from "./fakes-serial.ts";

const tick = () => new Promise((r) => setTimeout(r, 5));

test("SerialConsole assembles lines across chunks and parses @omb lines", async () => {
  const port = new FakePort();
  await port.open({ baudRate: 115200 });
  const con = new SerialConsole(port, { now: () => 42 });
  port.emit("\u001b[0;32mI (12) boot: hello\u001b[0m\r\n@omb {\"op\":\"boot\",\"board\":\"lcd-154\",");
  port.emit('"fw":"1.0.0","id":"gad_3f9a0c2b7e41d856"}\r\n');
  await tick();
  const events = con.drain();
  assert.equal(events.length, 2);
  assert.equal(events[0]?.msg, null);
  assert.equal(events[1]?.msg?.op, "boot");
  assert.equal(con.lastOutputAt, 42);
  assert.deepEqual(con.drain(), []);
  await con.close();
});

test("a UTF-8 character cut between USB packets still decodes (an accented SSID in a scan line)", async () => {
  const port = new FakePort();
  await port.open({ baudRate: 115200 });
  const con = new SerialConsole(port);
  const bytes = new TextEncoder().encode('@omb {"op":"scan","networks":[{"ssid":"Café","rssi":-50,"auth":"wpa2"}]}\r\n');
  const cut = bytes.indexOf(0xc3) + 1; // between the two bytes of "é" (0xC3 0xA9)
  assert.equal(bytes[cut], 0xa9);
  port.emitBytes(bytes.slice(0, cut));
  port.emitBytes(bytes.slice(cut));
  await tick();
  const msg = con.drain()[0]?.msg;
  assert.equal(msg?.op === "scan" && msg.networks[0]?.ssid, "Café");
  await con.close();
});

test("SerialConsole writes raw text lines (no SLIP framing)", async () => {
  const port = new FakePort();
  await port.open({ baudRate: 115200 });
  const con = new SerialConsole(port);
  await con.send('wifi "Home Net" "pass"');
  await con.send("status");
  assert.deepEqual(port.sent(), ['wifi "Home Net" "pass"', "status"]);
  await con.close();
});

test("SerialConsole reports a lost device and refuses further writes", async () => {
  const port = new FakePort();
  await port.open({ baudRate: 115200 });
  const con = new SerialConsole(port);
  port.unplug();
  await con.finished;
  assert.equal(con.lost, true);
  await assert.rejects(con.send("status"), (e: unknown) => e instanceof InstallerError && e.code === "port_lost");
});

test("a write the gadget never reads times out instead of hanging", async () => {
  const port = new FakePort();
  await port.open({ baudRate: 115200 });
  port.writable = new WritableStream<Uint8Array>({ write: () => new Promise(() => undefined) });
  const con = new SerialConsole(port, { writeTimeoutMs: 20 });
  await assert.rejects(con.send("status"), (e: unknown) => e instanceof InstallerError && e.code === "console_write_timeout");
});
```

- [ ] **Step 3: Run them and watch them fail**

Run: `cd site && npm test; cd ..`
Expected: FAIL: `error TS2307: Cannot find module '../src/reset.ts'` (and `../src/serial-console.ts`).

- [ ] **Step 4: Write `reset.ts`**

`site/src/reset.ts`:

```ts
// SPDX-License-Identifier: Apache-2.0
// Leaving download mode and opening the app's console (spec §5.8 step 3,
// contract §4.8). esptool-js's own hard reset only lowers RTS, so the
// installer pulses it itself, the way Espressif's Python esptool does for the S3.
import { InstallerError } from "./errors.ts";

export const RTC_CNTL_OPTION1_REG = 0x6000812c; // bit 0: FORCE_DOWNLOAD_BOOT
export const USB_JTAG = { usbVendorId: 0x303a, usbProductId: 0x1001 } as const;
export const CONSOLE_BAUD = 115200;

/** The parts of a Web Serial SerialPort the installer uses (tests pass a fake). */
export interface SerialPortLike {
  readonly readable: ReadableStream<Uint8Array> | null;
  readonly writable: WritableStream<Uint8Array> | null;
  open(options: { baudRate: number }): Promise<void>;
  close(): Promise<void>;
  setSignals(signals: { dataTerminalReady?: boolean; requestToSend?: boolean }): Promise<void>;
  getInfo(): { usbVendorId?: number; usbProductId?: number };
}

export interface SerialLike {
  getPorts(): Promise<SerialPortLike[]>;
}

export interface ResetDeps {
  loader: { writeReg(addr: number, value: number, mask?: number): Promise<void> };
  transport: { setDTR(state: boolean): Promise<void>; setRTS(state: boolean): Promise<void>; disconnect(): Promise<void> };
  port: SerialPortLike;
  serial: SerialLike;
  sleep(ms: number): Promise<void>;
  /** How long to wait for the board to come back after the reset. Default 10 s. */
  reacquireMs?: number;
}

export function isUsbJtag(port: SerialPortLike): boolean {
  const info = port.getInfo();
  return info.usbVendorId === USB_JTAG.usbVendorId && info.usbProductId === USB_JTAG.usbProductId;
}

/** Reset into the app and return the open console port (the same port, or the re-enumerated one). */
export async function resetToApp(deps: ResetDeps): Promise<SerialPortLike> {
  try {
    await deps.loader.writeReg(RTC_CNTL_OPTION1_REG, 0, 1);
  } catch {
    // The stub may already be gone; the RTS pulse below still resets the chip.
  }
  await deps.transport.setDTR(false);
  await deps.transport.setRTS(true);
  await deps.sleep(100);
  await deps.transport.setRTS(false);
  try {
    await deps.transport.disconnect();
  } catch {
    // The USB device may already have dropped off the bus.
  }
  return openConsolePort(deps.port, deps.serial, deps.sleep, deps.reacquireMs ?? 10_000);
}

/**
 * Open a console port at 115200 with DTR and RTS low (either line high resets
 * or straps a USB-Serial-JTAG chip). Tries `port` first, then any granted
 * 0x303A/0x1001 port, until `waitMs` has passed.
 *
 * Chrome rejects open() with the same "Failed to open serial port." when
 * another program holds the port and when the device is gone, so the error
 * text cannot tell them apart. Presence can: getPorts() lists only connected
 * devices. A listed port that refused on the last pass → port_busy; nothing
 * listed → port_lost.
 */
export async function openConsolePort(port: SerialPortLike, serial: SerialLike, sleep: (ms: number) => Promise<void>, waitMs: number): Promise<SerialPortLike> {
  const step = 250;
  let presentButRefused = false;
  for (let waited = 0; waited <= waitMs; waited += step) {
    const listed = await serial.getPorts();
    const candidates = [port, ...listed.filter((p) => p !== port && isUsbJtag(p))];
    presentButRefused = false;
    for (const candidate of candidates) {
      try {
        await candidate.open({ baudRate: CONSOLE_BAUD });
      } catch {
        if (listed.includes(candidate)) presentButRefused = true;
        continue;
      }
      await candidate.setSignals({ dataTerminalReady: false, requestToSend: false });
      return candidate;
    }
    await sleep(step);
  }
  throw presentButRefused
    ? new InstallerError("port_busy", "Another program or tab is using the board's port.")
    : new InstallerError("port_lost", "The board did not come back after the reset.");
}
```

- [ ] **Step 5: Write `serial-console.ts`**

`site/src/serial-console.ts`:

```ts
// SPDX-License-Identifier: Apache-2.0
// Raw Web Serial console session. Writes go straight to port.writable, never
// through esptool-js's Transport.write, which SLIP-encodes (contract §4.8).
import { createLineSplitter, parseOmbLine, type OmbMessage } from "./console.ts";
import { InstallerError } from "./errors.ts";
import type { SerialPortLike } from "./reset.ts";

export interface ConsoleEvent {
  line: string;
  msg: OmbMessage | null;
}

/** What the setup steps need from a console; tests pass a scripted fake. */
export interface ConsoleIO {
  send(line: string): Promise<void>;
  /** Every line received since the last drain, oldest first. */
  drain(): ConsoleEvent[];
  readonly lost: boolean;
}

/** A console the page can close (and later reopen on the same or a re-enumerated port). */
export interface ConsoleSession extends ConsoleIO {
  close(): Promise<void>;
}

export class SerialConsole implements ConsoleSession {
  readonly port: SerialPortLike;
  readonly finished: Promise<void>;
  lost = false;
  lastOutputAt: number | null = null;
  onEvent: ((event: ConsoleEvent) => void) | null = null;
  private readonly now: () => number;
  private readonly writeTimeoutMs: number;
  private reader: ReadableStreamDefaultReader<Uint8Array> | null = null;
  private queue: ConsoleEvent[] = [];
  private closed = false;

  constructor(port: SerialPortLike, options: { now?: () => number; writeTimeoutMs?: number } = {}) {
    this.port = port;
    this.now = options.now ?? Date.now;
    this.writeTimeoutMs = options.writeTimeoutMs ?? 3000;
    this.finished = this.readLoop();
  }

  private async readLoop(): Promise<void> {
    const readable = this.port.readable;
    if (readable === null) {
      this.lost = true;
      return;
    }
    const reader = readable.getReader();
    this.reader = reader;
    const decoder = new TextDecoder();
    const split = createLineSplitter();
    try {
      for (;;) {
        const { value, done } = await reader.read();
        if (done) break;
        if (value !== undefined && value.length > 0) {
          this.lastOutputAt = this.now();
          for (const line of split(decoder.decode(value, { stream: true }))) this.push(line);
        }
      }
    } catch {
      // The device dropped off the bus (reset, unplug); `lost` tells the caller.
    } finally {
      reader.releaseLock();
      if (!this.closed) this.lost = true;
    }
  }

  private push(line: string): void {
    const event = { line, msg: parseOmbLine(line) };
    this.queue.push(event);
    this.onEvent?.(event);
  }

  drain(): ConsoleEvent[] {
    const events = this.queue;
    this.queue = [];
    return events;
  }

  async send(line: string): Promise<void> {
    if (this.lost || this.closed || this.port.writable === null) throw new InstallerError("port_lost", "The board's port closed.");
    const writer = this.port.writable.getWriter();
    let timer: ReturnType<typeof setTimeout> | undefined;
    try {
      await Promise.race([
        writer.write(new TextEncoder().encode(`${line}\n`)),
        new Promise<never>((_, reject) => {
          timer = setTimeout(
            () => reject(new InstallerError("console_write_timeout", "The gadget isn't reading its console.")),
            this.writeTimeoutMs,
          );
        }),
      ]);
    } finally {
      clearTimeout(timer);
      writer.releaseLock();
    }
  }

  async close(): Promise<void> {
    this.closed = true;
    try {
      await this.reader?.cancel();
    } catch {
      // already gone
    }
    await this.finished;
    try {
      await this.port.close();
    } catch {
      // already closed
    }
  }
}
```

- [ ] **Step 6: Run the tests**

Run: `cd site && npm test; cd ..`
Expected: `# pass 33` and `# fail 0`.

- [ ] **Step 7: Commit**

```bash
git add site/src/reset.ts site/src/serial-console.ts site/test/fakes-serial.ts site/test/reset.test.ts site/test/serial-console.test.ts
git commit -m "feat(site): reset into the app and talk to the raw console" \
  -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

### Task 9: Setup steps and the page's copy

**Files:**
- Create: `site/src/setup.ts`
- Create: `site/src/copy.ts`
- Create: `site/test/fakes-console.ts`
- Test: `site/test/setup.test.ts`, `site/test/copy.test.ts`

**Interfaces:**
- Consumes: Task 5 `isDownloadModeLine`, `setupCommands`, `OmbMessage`, `parseOmbLine`; Task 8 `ConsoleIO`, `ConsoleSession`, `ConsoleEvent`, `fakeClock`.
- Produces (`setup.ts`): types `StatusMsg`, `ScanMsg`, `HostsMsg`, `Clock {now, sleep}`, `realClock`, `AppCheck` (`app` with `status | null`, `download_mode`, `silent`, `lost`), `WaitPrompt` (`waiting | press_rst`), `ExistingState` (`paired | reconnecting | needs_setup`), `PairingResult` (`paired | need_host | pair_error | wifi_failed | device_limit | command_error | timeout | lost`), `PairingNotice` (`device_limit`); constant `STALE_POLLS = 3`; functions `detectApp(io, clock, waitMs = 5000)`, `waitForApp(open: () => Promise<ConsoleSession>, clock, onPrompt: (p: WaitPrompt) => void): Promise<{io, status}>`, `classifyStatus(status)`, `waitForPaired(io, clock, waitMs = 20000)`, `scanNetworks(io, clock, waitMs = 15000)` (sends `log off`, then `scan`), `pairAndWait(io, commands, clock, {pollMs = 1000, timeoutMs = 150000, onNotice?})`.
- `pairAndWait` and stale state: spec §4.3 leaves `pair` = `error` after `bad_code` until the next `pair`, and neither spec §5.6 nor contract §2.11 says when a new `pair <code>` or `wifi` clears it. So right after the commands the first statuses can still describe the previous attempt. The pair side and the Wi-Fi side are tracked separately: a `pair: "error"` counts once `pair` has been `code_stored`, `connecting` or `paired` since the commands were sent, and `wifi: "failed"` counts once `wifi` has been `connecting` or `connected`; either counts anyway after `STALE_POLLS` status replies. They are separate because a status left over from a wrong code (`wifi: "connected", pair: "error"`) already shows Wi-Fi progress. `error: "device_limit"` never ends the wait: the gadget keeps the code and retries every 10 s for 120 s (spec §4.3), so the page shows `COPY.deviceLimitWaiting` once (through `onNotice`) and keeps polling. It returns `device_limit` only if the time runs out on that state.
- Produces (`copy.ts`): `PAIR_PATH`, `COPY` (every sentence, including `deviceLimitWaiting`), `pairErrorMessage(code)`, `pairingFailure(result, ssid)`.
- Produces (tests): `FakeGadget implements ConsoleSession` (answers `status` from its state; `onCommand` hook; `emit`, `set` (a field set to `undefined` is dropped), `close`/`closed`).

- [ ] **Step 1: Write the scripted gadget**

`site/test/fakes-console.ts`:

```ts
// SPDX-License-Identifier: Apache-2.0
// A scripted gadget console that answers commands like the firmware (contract §2.11).
import { parseOmbLine } from "../src/console.ts";
import type { ConsoleEvent, ConsoleSession } from "../src/serial-console.ts";

export interface GadgetScript {
  status?: Record<string, unknown>;
  onCommand?: (line: string, gadget: FakeGadget) => void;
}

export class FakeGadget implements ConsoleSession {
  lost = false;
  closed = false;
  sent: string[] = [];
  status: Record<string, unknown> = { op: "status", wifi: "off", id: "gad_3f9a0c2b7e41d856", pair: "unpaired", fw: "1.0.0" };
  onCommand: (line: string, gadget: FakeGadget) => void;
  private queue: ConsoleEvent[] = [];
  constructor(script: GadgetScript = {}) {
    if (script.status) this.status = { ...this.status, ...script.status };
    this.onCommand = script.onCommand ?? (() => undefined);
  }
  async send(line: string): Promise<void> {
    if (this.lost) throw new Error("lost");
    this.sent.push(line);
    this.onCommand(line, this);
    if (line === "status") this.emit(`@omb ${JSON.stringify(this.status)}`);
  }
  emit(line: string): void {
    this.queue.push({ line, msg: parseOmbLine(line) });
  }
  /** Merge fields into the status; a field set to undefined disappears from the @omb line. */
  set(fields: Record<string, unknown>): void {
    this.status = { ...this.status, ...fields };
  }
  drain(): ConsoleEvent[] {
    const q = this.queue;
    this.queue = [];
    return q;
  }
  async close(): Promise<void> {
    this.closed = true;
  }
}
```

- [ ] **Step 2: Write the failing tests**

`site/test/setup.test.ts`:

```ts
// SPDX-License-Identifier: Apache-2.0
import assert from "node:assert/strict";
import { test } from "node:test";
import { setupCommands } from "../src/console.ts";
import { classifyStatus, detectApp, pairAndWait, scanNetworks, waitForApp, waitForPaired, type PairingNotice, type StatusMsg, type WaitPrompt } from "../src/setup.ts";
import { FakeGadget } from "./fakes-console.ts";
import { fakeClock } from "./fakes-serial.ts";

const cmds = setupCommands({ code: "123456", ssid: "Home", password: "hunter22" });

/** Answers like a gadget that joins Wi-Fi after 2 polls and pairs after 4. */
function happyGadget(): FakeGadget {
  let polls = 0;
  return new FakeGadget({
    onCommand: (line, g) => {
      if (line.startsWith("pair ")) g.set({ pair: "code_stored" });
      if (line.startsWith("wifi ")) g.set({ wifi: "connecting", ssid: "Home" });
      if (line === "status") {
        polls++;
        if (polls === 2) g.set({ wifi: "connected", host: "192.168.1.20:8810", pair: "connecting" });
        if (polls === 4) g.set({ pair: "paired", host_name: "Omkar's computer" });
      }
    },
  });
}

test("pairAndWait sends pair, wifi, host auto in that order and returns once paired", async () => {
  const g = happyGadget();
  const r = await pairAndWait(g, cmds, fakeClock());
  assert.equal(r.kind, "paired");
  assert.equal(r.kind === "paired" && r.status.host_name, "Omkar's computer");
  assert.deepEqual(g.sent.slice(0, 3), ["pair 123456", 'wifi "Home" "hunter22"', "host auto"]);
});

test("a wrong code ends with pair_error bad_code", async () => {
  const g = new FakeGadget({
    onCommand: (line, gg) => {
      if (line === "host auto") gg.set({ wifi: "connected", pair: "error", error: "bad_code" });
    },
  });
  const r = await pairAndWait(g, cmds, fakeClock());
  assert.deepEqual(r.kind === "pair_error" && r.code, "bad_code");
});

test("a wrong Wi-Fi password ends with wifi_failed", async () => {
  const g = new FakeGadget({ onCommand: (line, gg) => line.startsWith("wifi ") && gg.set({ wifi: "failed", ssid: "Home" }) });
  assert.equal((await pairAndWait(g, cmds, fakeClock())).kind, "wifi_failed");
});

test("a retry after bad_code ignores the stale error status", async () => {
  // Left over from the wrong code: Wi-Fi is up, pair is still "error" until the new pair is applied.
  let polls = 0;
  const g = new FakeGadget({
    status: { wifi: "connected", ssid: "Home", host: "192.168.1.20:8810", pair: "error", error: "bad_code" },
    onCommand: (line, gg) => line === "status" && ++polls === 3 && gg.set({ pair: "paired", error: undefined, host_name: "Mac" }),
  });
  const r = await pairAndWait(g, cmds, fakeClock());
  assert.equal(r.kind, "paired");
});

test("a retry after wifi failed ignores the stale failure", async () => {
  let polls = 0;
  const g = new FakeGadget({
    status: { wifi: "failed", ssid: "Home", pair: "code_stored" },
    onCommand: (line, gg) => {
      if (line !== "status") return;
      polls++;
      if (polls === 3) gg.set({ wifi: "connected", pair: "connecting" });
      if (polls === 4) gg.set({ pair: "paired" });
    },
  });
  assert.equal((await pairAndWait(g, cmds, fakeClock())).kind, "paired");
});

test("device_limit keeps polling with one notice; it is a result only when time runs out", async () => {
  let polls = 0;
  const notices: PairingNotice[] = [];
  const g = new FakeGadget({
    onCommand: (line, gg) => {
      if (line === "host auto") gg.set({ wifi: "connected", pair: "error", error: "device_limit" });
      if (line === "status" && ++polls === 4) gg.set({ pair: "paired", error: undefined }); // a device was removed
    },
  });
  const r = await pairAndWait(g, cmds, fakeClock(), { onNotice: (n) => notices.push(n) });
  assert.equal(r.kind, "paired");
  assert.deepEqual(notices, ["device_limit"]);
  const stuck = new FakeGadget({ status: { wifi: "connected", pair: "error", error: "device_limit" } });
  assert.equal((await pairAndWait(stuck, cmds, fakeClock(), { timeoutMs: 5000 })).kind, "device_limit");
});

test("no MausBot found asks for an address; several ask the person to pick", async () => {
  const none = new FakeGadget({ onCommand: (line, g) => line === "host auto" && g.emit('@omb {"op":"hosts","hosts":[]}') });
  const r1 = await pairAndWait(none, cmds, fakeClock());
  assert.deepEqual(r1.kind === "need_host" && r1.hosts, []);
  const two = new FakeGadget({
    onCommand: (line, g) =>
      line === "host auto" &&
      g.emit('@omb {"op":"hosts","hosts":[{"name":"A","address":"10.0.0.2:8810","id":""},{"name":"B","address":"10.0.0.3:8810","id":""}]}'),
  });
  const r2 = await pairAndWait(two, cmds, fakeClock());
  assert.equal(r2.kind === "need_host" && r2.hosts.length, 2);
});

test("a single listed MausBot is chosen automatically", async () => {
  const g = new FakeGadget({
    onCommand: (line, gg) => {
      if (line === "host auto") gg.emit('@omb {"op":"hosts","hosts":[{"name":"A","address":"10.0.0.2:8810","id":""}]}');
      if (line === "host 10.0.0.2:8810") gg.set({ wifi: "connected", pair: "paired" });
    },
  });
  assert.equal((await pairAndWait(g, cmds, fakeClock())).kind, "paired");
  assert.ok(g.sent.includes("host 10.0.0.2:8810"));
});

test("a console error for one of the setup commands is reported", async () => {
  const g = new FakeGadget({
    onCommand: (line, gg) => line.startsWith("wifi ") && gg.emit('@omb {"op":"error","cmd":"wifi","message":"password must be 8-63 bytes"}'),
  });
  const r = await pairAndWait(g, cmds, fakeClock());
  assert.deepEqual(r.kind === "command_error" && [r.cmd, r.message], ["wifi", "password must be 8-63 bytes"]);
});

test("pairing gives up after the timeout with the last status", async () => {
  const g = new FakeGadget({ status: { wifi: "connected", pair: "connecting" } });
  const clock = fakeClock();
  const r = await pairAndWait(g, cmds, clock, { timeoutMs: 10_000 });
  assert.equal(r.kind, "timeout");
  assert.equal(r.kind === "timeout" && r.status?.pair, "connecting");
  assert.ok(clock.t >= 10_000 && clock.t <= 12_000);
});

test("an unplugged board ends pairing with lost", async () => {
  const g = new FakeGadget();
  g.lost = true;
  assert.equal((await pairAndWait(g, cmds, fakeClock())).kind, "lost");
});

test("detectApp: app, silent board, download mode, boot line only", async () => {
  assert.equal((await detectApp(new FakeGadget(), fakeClock())).kind, "app");
  const silent = new FakeGadget();
  silent.send = async () => undefined;
  assert.equal((await detectApp(silent, fakeClock())).kind, "silent");
  const rom = new FakeGadget();
  rom.send = async () => undefined;
  rom.emit("rst:0x15 (USB_UART_CHIP_RESET),boot:0x0 (DOWNLOAD(USB/UART0))");
  rom.emit("waiting for download");
  assert.equal((await detectApp(rom, fakeClock())).kind, "download_mode");
  const booted = new FakeGadget();
  booted.send = async () => undefined;
  booted.emit('@omb {"op":"boot","board":"lcd-154","fw":"1.0.0","id":"gad_3f9a0c2b7e41d856"}');
  const r = await detectApp(booted, fakeClock());
  assert.deepEqual(r, { kind: "app", status: null });
});

test("a silent board keeps the RST prompt up and is not reopened", async () => {
  // Spec §5.8 step 3: ~5 s without the app → "Press RST…". The board stays plugged in and silent for 20 s.
  const clock = fakeClock();
  const g = new FakeGadget();
  const answer = g.send.bind(g);
  g.send = async (line) => {
    if (clock.t >= 20_000) await answer(line);
  };
  let opens = 0;
  const prompts: WaitPrompt[] = [];
  const r = await waitForApp(
    async () => {
      opens++;
      return g;
    },
    clock,
    (p) => prompts.push(p),
  );
  assert.equal(r.status?.op, "status");
  assert.deepEqual(prompts, ["waiting", "press_rst"]); // the last prompt before the app answered is the RST prompt
  assert.equal(opens, 1);
  assert.equal(g.closed, false);
});

test("a board that drops off USB is reopened, and the app is found on the new console", async () => {
  const gone = new FakeGadget();
  gone.lost = true;
  const back = new FakeGadget();
  const sessions = [gone, back];
  const prompts: WaitPrompt[] = [];
  const r = await waitForApp(async () => sessions.shift() as FakeGadget, fakeClock(), (p) => prompts.push(p));
  assert.equal(r.io, back);
  assert.equal(gone.closed, true);
  assert.deepEqual(prompts, ["waiting", "press_rst"]);
});

test("a reflashed gadget that kept its pairing is recognised", async () => {
  const base = { op: "status", id: "gad_x", fw: "1.1.0" } as const;
  assert.equal(classifyStatus({ ...base, wifi: "connected", pair: "paired" } as StatusMsg), "paired");
  assert.equal(classifyStatus({ ...base, wifi: "connecting", ssid: "Home", pair: "connecting" } as StatusMsg), "reconnecting");
  assert.equal(classifyStatus({ ...base, wifi: "off", pair: "unpaired" } as StatusMsg), "needs_setup");
  assert.equal(classifyStatus(null), "needs_setup");
  let polls = 0;
  const g = new FakeGadget({
    status: { wifi: "connecting", ssid: "Home", pair: "connecting" },
    onCommand: (line, gg) => line === "status" && ++polls === 3 && gg.set({ pair: "paired", host_name: "Mac" }),
  });
  assert.equal((await waitForPaired(g, fakeClock()))?.host_name, "Mac");
  assert.equal(await waitForPaired(new FakeGadget(), fakeClock(), 3000), null);
});

test("scanNetworks silences the log, scans, and returns the gadget's list", async () => {
  const g = new FakeGadget({
    onCommand: (line, gg) => line === "scan" && gg.emit('@omb {"op":"scan","networks":[{"ssid":"Home","rssi":-50,"auth":"wpa2"}]}'),
  });
  assert.deepEqual(await scanNetworks(g, fakeClock()), [{ ssid: "Home", rssi: -50, auth: "wpa2" }]);
  assert.deepEqual(g.sent, ["log off", "scan"]);
  await assert.rejects(scanNetworks(new FakeGadget(), fakeClock(), 1000), /didn't list/);
});
```

`site/test/copy.test.ts`:

```ts
// SPDX-License-Identifier: Apache-2.0
import assert from "node:assert/strict";
import { test } from "node:test";
import { COPY, PAIR_PATH, pairErrorMessage, pairingFailure } from "../src/copy.ts";

test("the installer names the desktop path, Remote access and the Windows hint (spec §5.8 step 5)", () => {
  assert.equal(PAIR_PATH, "MausBot → Settings → Remote access → Pair a gadget");
  assert.match(COPY.codeHow, /MausBot → Settings → Remote access → Pair a gadget/);
  assert.match(COPY.remoteAccessOn, /Remote access must be on/);
  assert.match(COPY.windowsPublic, /Public/);
  assert.match(COPY.windowsPublic, /Private/);
  assert.equal(COPY.pressRst, "Press RST or unplug and replug the board");
  assert.equal(COPY.devkitPort, "Plug the board in by the USB-C port labelled USB, not the one labelled UART.");
});

test("every pairing failure has a sentence", () => {
  const status = { op: "status", wifi: "connected", id: "gad_x", pair: "error", fw: "1.0.0" } as const;
  assert.match(pairingFailure({ kind: "pair_error", code: "bad_code", status }, "Home"), /Pair a gadget/);
  assert.match(pairingFailure({ kind: "wifi_failed", status }, "Home"), /can't join Home/);
  assert.match(pairingFailure({ kind: "device_limit", status }, "Home"), /too many devices/);
  assert.match(pairingFailure({ kind: "command_error", cmd: "wifi", message: "too long" }, "Home"), /too long/);
  assert.match(pairingFailure({ kind: "timeout", status: null }, "Home"), /Remote access is on/);
  assert.match(pairingFailure({ kind: "lost" }, "Home"), /Press RST/);
  for (const code of ["bad_code", "device_limit", "proto_unsupported", "bad_sig", "something_new"]) {
    assert.notEqual(pairErrorMessage(code), "");
  }
  assert.equal(
    COPY.deviceLimitWaiting,
    "MausBot has too many devices. Remove one in MausBot → Settings → Remote access; the gadget keeps trying for two minutes.",
  );
});

test("the port hint says only ESP32-S3 boards on native USB show up", () => {
  // requestPort() filters on 0x303A/0x1001: a plain ESP32 or a USB-to-UART port never appears (Review Focus 5).
  assert.match(COPY.noPortHint, /ESP32-S3/);
  assert.match(COPY.noPortHint, /plain ESP32/);
  assert.match(COPY.noPortHint, /USB-C cable that carries data/);
  assert.match(COPY.noPortHint, /hold BOOT/);
});
```

- [ ] **Step 3: Run them and watch them fail**

Run: `cd site && npm test; cd ..`
Expected: FAIL: `error TS2307: Cannot find module '../src/setup.ts'` (and `../src/copy.ts`).

- [ ] **Step 4: Write `setup.ts`**

`site/src/setup.ts`:

```ts
// SPDX-License-Identifier: Apache-2.0
// Console steps after flashing (spec §5.8 steps 3–6): find the app, list
// Wi-Fi networks, send pair → wifi → host, and poll status until paired.
import { isDownloadModeLine, type OmbMessage } from "./console.ts";
import type { ConsoleIO, ConsoleSession } from "./serial-console.ts";

export type StatusMsg = Extract<OmbMessage, { op: "status" }>;
export type ScanMsg = Extract<OmbMessage, { op: "scan" }>;
export type HostsMsg = Extract<OmbMessage, { op: "hosts" }>;

export interface Clock {
  now(): number;
  sleep(ms: number): Promise<void>;
}

export const realClock: Clock = {
  now: () => Date.now(),
  sleep: (ms) => new Promise((resolve) => setTimeout(resolve, ms)),
};

export type AppCheck =
  | { kind: "app"; status: StatusMsg | null }
  | { kind: "download_mode" }
  | { kind: "silent" }
  | { kind: "lost" };

/** Sends `status` every second; "silent" after waitMs with no app output (spec: ~5 s → press RST). */
export async function detectApp(io: ConsoleIO, clock: Clock, waitMs = 5000): Promise<AppCheck> {
  const end = clock.now() + waitMs;
  let booted = false;
  let downloadMode = false;
  for (;;) {
    for (const e of io.drain()) {
      if (e.msg?.op === "status") return { kind: "app", status: e.msg };
      if (e.msg?.op === "boot") booted = true;
      if (isDownloadModeLine(e.line)) downloadMode = true;
    }
    if (io.lost) return { kind: "lost" };
    if (clock.now() >= end) {
      if (booted) return { kind: "app", status: null };
      return downloadMode ? { kind: "download_mode" } : { kind: "silent" };
    }
    try {
      await io.send("status");
    } catch {
      if (io.lost) return { kind: "lost" };
    }
    await clock.sleep(1000);
  }
}

export type WaitPrompt = "waiting" | "press_rst";

/**
 * Waits for the app on the console `open()` returns (spec §5.8 step 3). After
 * ~5 s with no app output it shows the RST prompt and keeps it up while it
 * keeps asking the same console. It reopens only when the port is lost:
 * pressing RST or replugging drops the USB device, while a silent board that
 * is still plugged in keeps its port. `open()` is called again for each reopen.
 */
export async function waitForApp(
  open: () => Promise<ConsoleSession>,
  clock: Clock,
  onPrompt: (prompt: WaitPrompt) => void,
): Promise<{ io: ConsoleSession; status: StatusMsg | null }> {
  let io = await open();
  let shown: WaitPrompt = "waiting";
  onPrompt(shown);
  for (;;) {
    const check = await detectApp(io, clock);
    if (check.kind === "app") return { io, status: check.status };
    if (shown !== "press_rst") {
      shown = "press_rst";
      onPrompt(shown);
    }
    if (check.kind !== "lost") continue;
    await io.close();
    io = await open();
  }
}

export type ExistingState = "paired" | "reconnecting" | "needs_setup";

/** A reinstall keeps NVS, so a reflashed gadget may still be paired. */
export function classifyStatus(status: StatusMsg | null): ExistingState {
  if (status === null) return "needs_setup";
  if (status.pair === "paired") return "paired";
  if (status.pair === "connecting" && status.ssid !== undefined) return "reconnecting";
  return "needs_setup";
}

/** Polls status until `pair` is `paired` (→ that status) or waitMs passes (→ null). */
export async function waitForPaired(io: ConsoleIO, clock: Clock, waitMs = 20_000): Promise<StatusMsg | null> {
  const end = clock.now() + waitMs;
  for (;;) {
    for (const e of io.drain()) if (e.msg?.op === "status" && e.msg.pair === "paired") return e.msg;
    if (io.lost || clock.now() >= end) return null;
    try {
      await io.send("status");
    } catch {
      return null;
    }
    await clock.sleep(1000);
  }
}

/** `log off`, then `scan`; resolves with the gadget's own network list (strongest first). */
export async function scanNetworks(io: ConsoleIO, clock: Clock, waitMs = 15_000): Promise<ScanMsg["networks"]> {
  io.drain();
  await io.send("log off");
  await io.send("scan");
  const end = clock.now() + waitMs;
  for (;;) {
    for (const e of io.drain()) {
      if (e.msg?.op === "scan") return e.msg.networks;
      if (e.msg?.op === "error" && e.msg.cmd === "scan") throw new Error(e.msg.message);
    }
    if (io.lost) throw new Error("The board's port closed.");
    if (clock.now() >= end) throw new Error("The gadget didn't list any Wi-Fi networks.");
    await clock.sleep(250);
  }
}

export type PairingResult =
  | { kind: "paired"; status: StatusMsg }
  | { kind: "need_host"; hosts: HostsMsg["hosts"] }
  | { kind: "pair_error"; code: string; status: StatusMsg }
  | { kind: "wifi_failed"; status: StatusMsg }
  | { kind: "device_limit"; status: StatusMsg }
  | { kind: "command_error"; cmd: string; message: string }
  | { kind: "timeout"; status: StatusMsg | null }
  | { kind: "lost" };

/** Something the person should know while pairing goes on. */
export type PairingNotice = "device_limit";

const SETUP_CMDS = new Set(["pair", "wifi", "host"]);
const PAIR_PROGRESS: readonly string[] = ["code_stored", "connecting", "paired"];
const WIFI_PROGRESS: readonly string[] = ["connecting", "connected"];
/** Status replies after the commands that may still describe the previous attempt. */
export const STALE_POLLS = 3;

/**
 * Sends `commands`, then polls `status` every pollMs until paired, a failure,
 * or timeoutMs. A `pair: "error"` or `wifi: "failed"` left over from the
 * previous attempt is ignored until that side shows progress or STALE_POLLS
 * replies have passed. `device_limit` keeps polling (spec §4.3).
 */
export async function pairAndWait(
  io: ConsoleIO,
  commands: readonly string[],
  clock: Clock,
  options: { pollMs?: number; timeoutMs?: number; onNotice?: (notice: PairingNotice) => void } = {},
): Promise<PairingResult> {
  const pollMs = options.pollMs ?? 1000;
  const end = clock.now() + (options.timeoutMs ?? 150_000);
  io.drain();
  try {
    for (const c of commands) await io.send(c);
  } catch {
    return { kind: "lost" };
  }
  let last: StatusMsg | null = null;
  let polls = 0;
  let pairMoved = false; // pair has been code_stored/connecting/paired since the commands: the new code is in use
  let wifiMoved = false; // wifi has been connecting/connected since the commands: the new network is in use
  let noticed = false;
  for (;;) {
    for (const e of io.drain()) {
      const m = e.msg;
      if (m === null) continue;
      if (m.op === "error" && SETUP_CMDS.has(m.cmd)) return { kind: "command_error", cmd: m.cmd, message: m.message };
      if (m.op === "hosts") {
        const only = m.hosts.length === 1 ? m.hosts[0] : undefined;
        if (only !== undefined) {
          try {
            await io.send(`host ${only.address}`);
          } catch {
            return { kind: "lost" };
          }
          continue;
        }
        return { kind: "need_host", hosts: m.hosts };
      }
      if (m.op === "status") {
        last = m;
        polls++;
        if (PAIR_PROGRESS.includes(m.pair)) pairMoved = true;
        if (WIFI_PROGRESS.includes(m.wifi)) wifiMoved = true;
        const settled = polls > STALE_POLLS;
        if (m.pair === "paired") return { kind: "paired", status: m };
        if (m.pair === "error" && m.error === "device_limit") {
          if (!noticed) options.onNotice?.("device_limit");
          noticed = true;
          continue;
        }
        if (m.pair === "error" && m.error !== undefined && (pairMoved || settled)) return { kind: "pair_error", code: m.error, status: m };
        if (m.wifi === "failed" && (wifiMoved || settled)) return { kind: "wifi_failed", status: m };
      }
    }
    if (io.lost) return { kind: "lost" };
    if (clock.now() >= end) {
      return last?.pair === "error" && last.error === "device_limit" ? { kind: "device_limit", status: last } : { kind: "timeout", status: last };
    }
    try {
      await io.send("status");
    } catch {
      return { kind: "lost" };
    }
    await clock.sleep(pollMs);
  }
}

```

- [ ] **Step 5: Write `copy.ts`**

`site/src/copy.ts`:

```ts
// SPDX-License-Identifier: Apache-2.0
// Every sentence the installer shows. Gadget-facing copy always names
// "MausBot → Settings → Remote access → Pair a gadget" (spec §6.4).
import type { PairingResult } from "./setup.ts";

export const PAIR_PATH = "MausBot → Settings → Remote access → Pair a gadget";

export const COPY = {
  title: "Install OpenMausBot Gadget",
  needSerial: "This browser can't reach USB serial devices. Open this page in Chrome or Edge on a computer.",
  noFirmware: "No firmware is published yet. Build it from source with AGENTS.md, or come back after the first release.",
  brokenIndex: "The published firmware list is broken. Try again later.",
  boardNotPublished: "This release has no firmware for this board.",
  devkitPort: "Plug the board in by the USB-C port labelled USB, not the one labelled UART.",
  noPortHint:
    "Don't see your board? Only ESP32-S3 boards on their native USB port show up here (a plain ESP32 or a USB-to-UART port won't). Use a USB-C cable that carries data, and if it still doesn't show, hold BOOT while you plug it in.",
  eraseLabel: "Erase everything",
  eraseHelp:
    "Wipes the whole flash, including the gadget's identity, Wi-Fi and pairing. You'll pair it again, and you can remove its old entry in MausBot → Settings → Remote access.",
  keepHelp: "A normal install keeps the gadget's identity, Wi-Fi and pairing.",
  installedPath: "Already installed? Set up Wi-Fi and pairing",
  pressRst: "Press RST or unplug and replug the board",
  waitingForBoard: "Waiting for the gadget to start…",
  stillPaired: (host: string | undefined) => (host ? `Still paired with ${host}. You're done.` : "Still paired. You're done."),
  reconnecting: "Reconnecting to MausBot…",
  wifiTitle: "Choose a Wi-Fi network",
  wifiOther: "Other network…",
  codeTitle: "Pair with MausBot",
  codeHow: `Open ${PAIR_PATH} and type the six-digit code here.`,
  remoteAccessOn: "Remote access must be on in MausBot.",
  windowsPublic:
    "If MausBot runs on Windows and this network is set to Public, Windows' firewall blocks the gadget. Open Windows Settings → Network & internet, choose this network and set its network profile type to Private.",
  hostTitle: "Find MausBot",
  hostNone: "The gadget couldn't find MausBot on this network. Type the address shown under Pair a gadget, for example 192.168.1.20:8810.",
  hostPick: "The gadget found more than one MausBot. Pick yours, or type the address shown under Pair a gadget.",
  deviceLimitWaiting:
    "MausBot has too many devices. Remove one in MausBot → Settings → Remote access; the gadget keeps trying for two minutes.",
  paired: (host: string | undefined) => (host ? `Paired with ${host}. Hold to talk!` : "Paired. Hold to talk!"),
  flashStages: {
    downloading: "Downloading firmware…",
    connecting: "Connecting to the board…",
    writing: "Installing…",
    done: "Installed.",
  },
} as const;

export function pairErrorMessage(code: string): string {
  switch (code) {
    case "bad_code":
      return "That code didn't work. Get a new one from Pair a gadget.";
    case "device_limit":
      return "MausBot still has too many devices. Remove one in MausBot → Settings → Remote access, then get a new code and try again.";
    case "proto_unsupported":
      return "This MausBot can't talk to this firmware. Update MausBot, then try again.";
    case "bad_sig":
      return "MausBot didn't accept the gadget's identity. Install again with Erase everything.";
    default:
      return `Pairing failed (${code}). Get a new code and try again.`;
  }
}

/** The sentence for every outcome except paired and need_host, which have their own screens. */
export function pairingFailure(result: PairingResult, ssid: string): string {
  switch (result.kind) {
    case "pair_error":
      return pairErrorMessage(result.code);
    case "device_limit":
      return pairErrorMessage("device_limit");
    case "wifi_failed":
      return `The gadget can't join ${ssid}. Check the password and try again.`;
    case "command_error":
      return `The gadget refused "${result.cmd}": ${result.message}`;
    case "timeout":
      return `Pairing didn't finish. Check that Remote access is on in MausBot, get a new code from Pair a gadget, and try again.`;
    case "lost":
      return `The board's port closed. ${COPY.pressRst}, then try again.`;
    case "paired":
    case "need_host":
      return "";
  }
}
```

- [ ] **Step 6: Run the tests**

Run: `cd site && npm test; cd ..`
Expected: `# pass 65` and `# fail 0` (52 as first planned; the review of Tasks 5–8 adds 10 and the review of Tasks 9–12 adds 3 here, Deviations recorded during the build).

- [ ] **Step 7: Commit**

```bash
git add site/src/setup.ts site/src/copy.ts site/test/fakes-console.ts site/test/setup.test.ts site/test/copy.test.ts
git commit -m "feat(site): detect the app, scan Wi-Fi and pair, with the page's copy" \
  -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

### Task 10: The page, the build and the Pages firmware check

**Files:**
- Create: `site/src/boards.ts`, `site/src/page.ts`, `site/src/main.ts`, `site/index.html`, `site/style.css`
- Create: `site/scripts/build.ts`, `site/scripts/check-firmware.ts`
- Test: `site/test/page.test.ts`, `site/test/check-firmware.test.ts`

**Interfaces:**
- Consumes: every `site/src` module from Tasks 5–9 (including `waitForApp`, `ConsoleSession`, `PairingNotice`, `COPY.deviceLimitWaiting`); `ESPLoader`, `Transport` from esptool-js; `SparkMD5.ArrayBuffer.hash`.
- Produces: `BOARD_CHOICES: readonly {id, name, detail}[]`; `supportsWebSerial(nav: object): boolean` and `releaseLine(index: InstallIndex): string` (`site/src/page.ts`, pure, tested in Node); `npm run build` → `site/dist/` with `index.html`, `style.css`, `app.js`, `app.js.map`, `licenses.txt` and `firmware/install.json` = `{"version":null,"boards":{}}`; `checkFirmwareDir(dir): Promise<string>` and CLI `node site/scripts/check-firmware.ts <dir>` (used by `pages.yml`).
- Rules `main.ts` follows, because Web Serial refuses a second `open()` on a port that is already open ("The port is already open."), and esptool-js 0.7.0's `Transport.connect()` calls `port.open()`:
  - every flow that opens the port first closes the page's console (`closeConsole()`);
  - `install()` disconnects the esptool-js `Transport` when flashing fails, and hands it to `resetToApp()` (which disconnects it) when flashing succeeds.
- Other rules: the wait for the app goes through `waitForApp`, so the RST prompt stays up until the app answers. A `wifi_failed` result returns to the Wi-Fi step with the reason shown there. An address typed after `need_host` is kept (`pinnedHost`) for the next code until "Change Wi-Fi or pair again". The Pair and "Use this address" buttons are disabled while a pairing runs, so a second submit cannot start a second polling loop on the same console.

- [ ] **Step 1: Write the failing tests**

`site/test/page.test.ts`:

```ts
// SPDX-License-Identifier: Apache-2.0
import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import { test } from "node:test";
import { COPY } from "../src/copy.ts";
import { releaseLine, supportsWebSerial } from "../src/page.ts";

test("index.html carries the trademark sentence and every element main.ts looks up", async () => {
  const html = await readFile(new URL("../index.html", import.meta.url), "utf8");
  assert.ok(html.includes("The OpenMausBot name, the MausBot name and the Maus mascot are trademarks of Supamaus Software Private Limited."));
  const main = await readFile(new URL("../src/main.ts", import.meta.url), "utf8");
  const ids = new Set([...main.matchAll(/(?:el(?:<[^>]+>)?|text|show)\("([a-z0-9-]+)"/g)].map((m) => m[1]));
  for (const id of ids) assert.ok(html.includes(`id="${id}"`), `index.html is missing #${id}`);
  // main.ts uses the tested helpers below rather than its own copies.
  assert.match(main, /supportsWebSerial\(navigator\)/);
  assert.match(main, /releaseLine\(index\)/);
});

test("the page gates on 'serial' in navigator, not on the browser's name (spec §5.8)", () => {
  assert.equal(supportsWebSerial({}), false);
  assert.equal(supportsWebSerial({ serial: {} }), true);
});

test("before the first release the page says no firmware is published", () => {
  assert.equal(releaseLine({ version: null, boards: {} }), COPY.noFirmware);
  assert.equal(releaseLine({ version: "1.1.0", boards: {} }), "Firmware 1.1.0");
});
```

`site/test/check-firmware.test.ts`:

```ts
// SPDX-License-Identifier: Apache-2.0
import assert from "node:assert/strict";
import { createHash } from "node:crypto";
import { mkdtemp, writeFile } from "node:fs/promises";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { test } from "node:test";
import { checkFirmwareDir } from "../scripts/check-firmware.ts";

const index = {
  version: "1.1.0",
  boards: { devkit: { parts: [{ path: "b.bin", offset: "0x0" }, { path: "a.bin", offset: "0x20000" }], full: "f.bin" } },
};
const sha = (s: string) => createHash("sha256").update(s).digest("hex");

async function dir(idx: unknown, files: Record<string, string>): Promise<string> {
  const d = await mkdtemp(join(tmpdir(), "omb-site-fw-"));
  await writeFile(join(d, "install.json"), JSON.stringify(idx));
  for (const [name, body] of Object.entries(files)) await writeFile(join(d, name), body);
  return d;
}

test("the placeholder passes and says nothing is published", async () => {
  assert.match(await checkFirmwareDir(await dir({ version: null, boards: {} }, {})), /no firmware/);
});

test("a release passes when every file it names is there", async () => {
  assert.match(await checkFirmwareDir(await dir(index, { "b.bin": "b", "a.bin": "a", "f.bin": "f" })), /1\.1\.0 for devkit/);
});

test("a missing part or full image fails the Pages build", async () => {
  await assert.rejects(checkFirmwareDir(await dir(index, { "b.bin": "b", "f.bin": "f" })), /a\.bin/);
  await assert.rejects(checkFirmwareDir(await dir(index, { "b.bin": "b", "a.bin": "a" })), /f\.bin/);
});

test("SHA256SUMS from the release must match every installer file; unlisted extras are ignored", async () => {
  const files = { "b.bin": "b", "a.bin": "a", "f.bin": "f" };
  const lines = (over: Record<string, string> = {}) =>
    Object.entries({ "a.bin": "a", "b.bin": "b", "f.bin": "f", "manifest.json": "m", ...over })
      .map(([n, body]) => `${sha(body)}  ${n}\n`)
      .join("") + `${sha(JSON.stringify(index))}  install.json\n`;
  assert.match(await checkFirmwareDir(await dir(index, { ...files, SHA256SUMS: lines() })), /checksums match/);
  await assert.rejects(checkFirmwareDir(await dir(index, { ...files, SHA256SUMS: lines({ "a.bin": "tampered" }) })), /a\.bin does not match/);
  const missing = lines().split("\n").filter((l) => !l.endsWith("  f.bin")).join("\n");
  await assert.rejects(checkFirmwareDir(await dir(index, { ...files, SHA256SUMS: missing })), /does not list f\.bin/);
});
```

- [ ] **Step 2: Run them and watch them fail**

Run: `cd site && npm test; cd ..`
Expected: FAIL: `error TS2307: Cannot find module '../scripts/check-firmware.ts'` (and `../src/page.ts`).

- [ ] **Step 3: Write the board list and the page helpers**

`site/src/boards.ts`:

```ts
// SPDX-License-Identifier: Apache-2.0
// The boards the installer offers (spec §5.3). Ids match install.json keys.
export interface BoardChoice {
  id: string;
  name: string;
  detail: string;
}

export const BOARD_CHOICES: readonly BoardChoice[] = [
  { id: "amoled-175c", name: "Waveshare ESP32-S3-Touch-AMOLED-1.75C", detail: "Round 1.75-inch AMOLED in an aluminum case, speaker and battery bay." },
  { id: "amoled-175", name: "Waveshare ESP32-S3-Touch-AMOLED-1.75", detail: "Round 1.75-inch AMOLED board with a speaker connector." },
  { id: "lcd-154", name: "Waveshare ESP32-S3-LCD-1.54", detail: "1.54-inch LCD with BOOT and PLUS buttons and a speaker." },
  { id: "devkit", name: "ESP32-S3-DevKitC-1-N16R8 breadboard build", detail: "2-inch ST7789, INMP441 mic and MAX98357A amp on a breadboard." },
];
```

`site/src/page.ts`:

```ts
// SPDX-License-Identifier: Apache-2.0
// Pure helpers for main.ts, so the page's gates are unit-tested in Node (spec §5.8).
import { COPY } from "./copy.ts";
import type { InstallIndex } from "./install.ts";

/** Feature detection, never the browser's name: the page needs Web Serial. */
export function supportsWebSerial(nav: object): boolean {
  return "serial" in nav;
}

/** The line under the heading; before the first release it says no firmware is published. */
export function releaseLine(index: InstallIndex): string {
  return index.version === null ? COPY.noFirmware : `Firmware ${index.version}`;
}
```

- [ ] **Step 4: Write the page wiring**

`site/src/main.ts`:

```ts
// SPDX-License-Identifier: Apache-2.0
// Page wiring for the installer. The logic lives in the tested modules; this
// file only connects them to the DOM and to Web Serial.
import { ESPLoader, Transport } from "esptool-js";
import SparkMD5 from "spark-md5";
import { BOARD_CHOICES } from "./boards.ts";
import { normalizeHostAddress, normalizePairCode, setupCommands, wifiProblem } from "./console.ts";
import { COPY, pairingFailure } from "./copy.ts";
import { InstallerError } from "./errors.ts";
import { flashBoard } from "./flash.ts";
import { fetchPart, loadInstallIndex, type InstallIndex } from "./install.ts";
import { releaseLine, supportsWebSerial } from "./page.ts";
import { USB_JTAG, openConsolePort, resetToApp, type SerialLike, type SerialPortLike } from "./reset.ts";
import { SerialConsole } from "./serial-console.ts";
import { classifyStatus, pairAndWait, realClock, scanNetworks, waitForApp, waitForPaired, type PairingResult } from "./setup.ts";

const el = <T extends HTMLElement = HTMLElement>(id: string): T => {
  const node = document.getElementById(id);
  if (node === null) throw new Error(`missing #${id}`);
  return node as T;
};
const show = (id: string, visible: boolean): void => {
  el(id).hidden = !visible;
};
const text = (id: string, value: string): void => {
  el(id).textContent = value;
};

let index: InstallIndex | null = null;
let board: string | null = null;
let con: SerialConsole | null = null;
let wifi = { ssid: "", password: "" };
/** The address typed after need_host. The next code reuses it; "Change Wi-Fi or pair again" forgets it. */
let pinnedHost: string | undefined;
let busy = false;
let pairing = false;

function log(line: string): void {
  const pre = el<HTMLPreElement>("log");
  pre.textContent = `${pre.textContent ?? ""}${line}\n`.slice(-20_000);
}

function friendly(err: unknown): string {
  if (err instanceof InstallerError) {
    switch (err.code) {
      case "port_busy":
        return `${err.message} Close it (a serial monitor or another tab) and try again.`;
      case "port_lost":
        return `${err.message} ${COPY.pressRst}.`;
      case "write_failed":
        return `${err.message} Click Connect and install to try again.`;
      case "flash_too_small":
        return `${err.message} Use an ESP32-S3-DevKitC-1-N16R8 or one of the Waveshare boards.`;
      default:
        return err.message;
    }
  }
  if (err instanceof DOMException && err.name === "NotFoundError") return COPY.noPortHint;
  return err instanceof Error ? err.message : String(err);
}

function step(name: "wifi" | "pair" | "done" | null): void {
  show("step-wifi", name === "wifi");
  show("step-pair", name === "pair");
  show("step-done", name === "done");
}

function setBusy(value: boolean): void {
  busy = value;
  el<HTMLButtonElement>("install").disabled = value || board === null || index?.boards[board] === undefined;
  el<HTMLButtonElement>("installed").disabled = value || board === null;
}

function setPairing(value: boolean): void {
  pairing = value;
  el<HTMLButtonElement>("code-submit").disabled = value;
  el<HTMLButtonElement>("host-submit").disabled = value;
}

function renderBoards(): void {
  const box = el("boards");
  box.replaceChildren();
  for (const b of BOARD_CHOICES) {
    const published = index?.boards[b.id] !== undefined;
    const label = document.createElement("label");
    label.className = published ? "board" : "board unpublished";
    const input = document.createElement("input");
    input.type = "radio";
    input.name = "board";
    input.value = b.id;
    input.addEventListener("change", () => {
      board = b.id;
      show("devkit-hint", b.id === "devkit");
      text("install-status", published ? "" : COPY.boardNotPublished);
      setBusy(busy);
    });
    const name = document.createElement("strong");
    name.textContent = b.name;
    const detail = document.createElement("span");
    detail.textContent = published ? b.detail : `${b.detail} ${COPY.boardNotPublished}`;
    label.append(input, name, detail);
    box.append(label);
  }
}

/** Web Serial refuses a second open() on a port that is already open, so every flow that opens the port closes our console first. */
async function closeConsole(): Promise<void> {
  const open = con;
  con = null;
  await open?.close();
}

async function install(): Promise<void> {
  if (index === null || board === null) return;
  setBusy(true);
  step(null);
  const progress = el<HTMLProgressElement>("flash-progress");
  let transport: Transport | null = null;
  try {
    await closeConsole();
    const port = await navigator.serial.requestPort({ filters: [{ ...USB_JTAG }] });
    transport = new Transport(port, false);
    const loader = new ESPLoader({
      transport,
      baudrate: 921600,
      romBaudrate: 115200,
      terminal: { clean: () => undefined, write: (s) => log(s), writeLine: (s) => log(s) },
    });
    progress.hidden = false;
    await flashBoard({
      loader,
      index,
      board,
      eraseAll: el<HTMLInputElement>("erase-all").checked,
      fetchPart: (path) => fetchPart(fetch, path),
      md5: (data) => SparkMD5.ArrayBuffer.hash(data.slice().buffer),
      onStage: (s) => text("install-status", COPY.flashStages[s]),
      onProgress: (f) => {
        progress.value = f;
      },
      onWarning: (message) => log(message),
    });
    text("install-status", COPY.waitingForBoard);
    const flashed = transport;
    transport = null; // resetToApp disconnects it
    const consolePort = await resetToApp({ loader, transport: flashed, port, serial: navigator.serial as SerialLike, sleep: realClock.sleep });
    await afterReset(consolePort);
  } catch (err) {
    // A failed flash leaves esptool-js holding the port; release it so "Connect and install" works again.
    if (transport !== null) await transport.disconnect().catch(() => undefined);
    text("install-status", friendly(err));
  } finally {
    progress.hidden = true;
    setBusy(false);
  }
}

async function setUpInstalled(): Promise<void> {
  setBusy(true);
  step(null);
  try {
    await closeConsole();
    const port = await navigator.serial.requestPort({ filters: [{ ...USB_JTAG }] });
    text("install-status", COPY.waitingForBoard);
    await afterReset(await openConsolePort(port, navigator.serial as SerialLike, realClock.sleep, 2000));
  } catch (err) {
    text("install-status", friendly(err));
  } finally {
    setBusy(false);
  }
}

/** Find the app on the console, then either finish (still paired) or start Wi-Fi setup. */
async function afterReset(port: SerialPortLike): Promise<void> {
  const serial = navigator.serial as SerialLike;
  let current = port;
  // The other boards, taken while `current` is open: a reopen never falls back to one of them, and the
  // board itself may already be listed under a new port when the reopen starts (Deviations, item 1).
  let others = (await serial.getPorts()).filter((p) => p !== current);
  let first = true;
  const { io, status } = await waitForApp(
    async () => {
      // The first console uses the port that is already open; later ones re-acquire it after RST or a replug.
      if (!first) {
        current = await openConsolePort(current, serial, realClock.sleep, 120_000, others);
        others = (await serial.getPorts()).filter((p) => p !== current);
      }
      first = false;
      const session = new SerialConsole(current);
      session.onEvent = (e) => log(e.line);
      con = session;
      return session;
    },
    realClock,
    (prompt) => text("install-status", prompt === "press_rst" ? `${COPY.pressRst}.` : COPY.waitingForBoard),
  );
  const state = classifyStatus(status);
  if (state === "paired") return done(COPY.stillPaired(status?.host_name));
  if (state === "reconnecting") {
    text("install-status", COPY.reconnecting);
    const again = await waitForPaired(io, realClock);
    if (again !== null) return done(COPY.stillPaired(again.host_name));
  }
  text("install-status", "");
  return startWifi();
}

/** The Wi-Fi step. `notice` explains why the page came back here (a wrong password). */
async function startWifi(notice?: string): Promise<void> {
  if (con === null) return;
  step("wifi");
  const list = el("networks");
  list.replaceChildren();
  show("wifi-error", false);
  if (notice !== undefined) {
    text("wifi-error", notice);
    show("wifi-error", true);
  }
  try {
    const networks = await scanNetworks(con, realClock);
    for (const n of networks) {
      const item = document.createElement("li");
      const button = document.createElement("button");
      button.type = "button";
      button.className = "network";
      button.textContent = `${n.ssid}${n.auth === "open" ? "" : " 🔒"}  ${n.rssi} dBm`;
      button.addEventListener("click", () => {
        el<HTMLInputElement>("ssid").value = n.ssid;
        el<HTMLInputElement>("password").focus();
      });
      item.append(button);
      list.append(item);
    }
  } catch (err) {
    text("wifi-error", `${notice === undefined ? "" : `${notice} `}${friendly(err)} Type the network name instead.`);
    show("wifi-error", true);
  }
}

function onWifiSubmit(event: SubmitEvent): void {
  event.preventDefault();
  const ssid = el<HTMLInputElement>("ssid").value;
  const password = el<HTMLInputElement>("password").value;
  const problem = wifiProblem(ssid, password);
  show("wifi-error", problem !== null);
  if (problem !== null) return text("wifi-error", problem);
  wifi = { ssid, password };
  step("pair");
  show("host-form", false);
  el<HTMLInputElement>("code").focus();
}

/** One pairing at a time: a second polling loop on the same console would steal half of its lines. */
async function runPairing(commands: string[]): Promise<void> {
  if (con === null || pairing) return;
  setPairing(true);
  text("pair-status", "Pairing…");
  try {
    handlePairing(await pairAndWait(con, commands, realClock, { onNotice: () => text("pair-status", COPY.deviceLimitWaiting) }));
  } finally {
    setPairing(false);
  }
}

async function onCodeSubmit(event: SubmitEvent): Promise<void> {
  event.preventDefault();
  if (pairing) return;
  const code = normalizePairCode(el<HTMLInputElement>("code").value);
  if (code === null) return text("pair-status", "The code is six digits.");
  // A typed address stays pinned: `host auto` would erase the gadget's stored host_addr (contract §2.11).
  await runPairing(setupCommands({ code, ...wifi, address: pinnedHost }));
}

async function onHostSubmit(event: SubmitEvent): Promise<void> {
  event.preventDefault();
  if (pairing) return;
  const address = normalizeHostAddress(el<HTMLInputElement>("host-address").value);
  if (address === null) return text("pair-status", "Type an address like 192.168.1.20:8810.");
  pinnedHost = address;
  await runPairing([`host ${address}`]);
}

function handlePairing(result: PairingResult): void {
  if (result.kind === "paired") return done(COPY.paired(result.status.host_name));
  if (result.kind === "need_host") {
    show("host-form", true);
    text("host-help", result.hosts.length === 0 ? COPY.hostNone : COPY.hostPick);
    const list = el("hosts");
    list.replaceChildren();
    for (const h of result.hosts) {
      const item = document.createElement("li");
      const button = document.createElement("button");
      button.type = "button";
      button.textContent = `${h.name} (${h.address})`;
      button.addEventListener("click", () => {
        el<HTMLInputElement>("host-address").value = h.address;
      });
      item.append(button);
      list.append(item);
    }
    text("pair-status", "");
    return;
  }
  // The Wi-Fi step hides #step-pair, so its reason goes to the Wi-Fi step's own message.
  if (result.kind === "wifi_failed") return void startWifi(pairingFailure(result, wifi.ssid));
  text("pair-status", pairingFailure(result, wifi.ssid));
}

function done(message: string): void {
  step("done");
  text("done-text", message);
  text("install-status", "");
}

async function main(): Promise<void> {
  text("erase-label", COPY.eraseLabel);
  text("erase-help", COPY.keepHelp);
  text("installed", COPY.installedPath);
  text("port-hint", COPY.noPortHint);
  text("devkit-hint", COPY.devkitPort);
  text("code-how", COPY.codeHow);
  text("remote-on", COPY.remoteAccessOn);
  text("windows-public", COPY.windowsPublic);
  el<HTMLInputElement>("erase-all").addEventListener("change", (e) => {
    text("erase-help", (e.target as HTMLInputElement).checked ? COPY.eraseHelp : COPY.keepHelp);
  });
  el("install").addEventListener("click", () => void install());
  el("installed").addEventListener("click", () => void setUpInstalled());
  el<HTMLFormElement>("wifi-form").addEventListener("submit", onWifiSubmit);
  el<HTMLFormElement>("code-form").addEventListener("submit", (e) => void onCodeSubmit(e));
  el<HTMLFormElement>("host-form").addEventListener("submit", (e) => void onHostSubmit(e));
  el("setup-again").addEventListener("click", () => {
    pinnedHost = undefined;
    void startWifi();
  });

  try {
    index = await loadInstallIndex(fetch);
    text("release", releaseLine(index));
  } catch {
    text("release", COPY.brokenIndex);
  }
  renderBoards();
  const supported = supportsWebSerial(navigator);
  if (!supported) {
    text("unsupported", COPY.needSerial);
    show("unsupported", true);
  }
  setBusy(!supported);
}

void main();
```

- [ ] **Step 5: Write the HTML and CSS**

`site/index.html`:

```html
<!doctype html>
<html lang="en">
  <head>
    <meta charset="utf-8" />
    <meta name="viewport" content="width=device-width, initial-scale=1" />
    <title>OpenMausBot Gadget installer</title>
    <meta name="description" content="Flash an ESP32-S3 board with OpenMausBot Gadget firmware, connect it to Wi-Fi and pair it with MausBot." />
    <link rel="stylesheet" href="style.css" />
    <script type="module" src="app.js"></script>
  </head>
  <body>
    <main>
      <header>
        <h1>Install OpenMausBot Gadget</h1>
        <p class="lede">Flash your board, connect it to Wi-Fi and pair it with MausBot, all from this page.</p>
        <p id="release" class="muted"></p>
        <p id="unsupported" class="alert" hidden></p>
      </header>

      <section id="step-board" class="step">
        <h2>1. Pick your board</h2>
        <div id="boards" class="boards" role="radiogroup" aria-label="Board"></div>
        <p id="devkit-hint" class="hint" hidden></p>
      </section>

      <section id="step-install" class="step">
        <h2>2. Install</h2>
        <label class="check"><input type="checkbox" id="erase-all" /> <span id="erase-label"></span></label>
        <p id="erase-help" class="hint"></p>
        <div class="row">
          <button id="install" type="button" disabled>Connect and install</button>
          <button id="installed" type="button" class="link" disabled></button>
        </div>
        <p id="port-hint" class="hint"></p>
        <progress id="flash-progress" max="1" value="0" hidden></progress>
        <p id="install-status" role="status" aria-live="polite"></p>
      </section>

      <section id="step-wifi" class="step" hidden>
        <h2>3. Connect to Wi-Fi</h2>
        <ul id="networks" class="networks"></ul>
        <form id="wifi-form">
          <label>Network <input id="ssid" autocomplete="off" required /></label>
          <label>Password <input id="password" type="password" autocomplete="off" /></label>
          <p class="hint">Leave the password empty for an open network.</p>
          <button type="submit">Next</button>
          <p id="wifi-error" class="alert" hidden></p>
        </form>
      </section>

      <section id="step-pair" class="step" hidden>
        <h2>4. Pair with MausBot</h2>
        <p id="code-how"></p>
        <p id="remote-on" class="hint"></p>
        <p id="windows-public" class="hint"></p>
        <form id="code-form">
          <label>Code <input id="code" inputmode="numeric" autocomplete="one-time-code" required /></label>
          <button id="code-submit" type="submit">Pair</button>
        </form>
        <form id="host-form" hidden>
          <p id="host-help"></p>
          <ul id="hosts" class="networks"></ul>
          <label>Address <input id="host-address" placeholder="192.168.1.20:8810" /></label>
          <button id="host-submit" type="submit">Use this address</button>
        </form>
        <p id="pair-status" role="status" aria-live="polite"></p>
      </section>

      <section id="step-done" class="step" hidden>
        <h2>Done</h2>
        <p id="done-text"></p>
        <button id="setup-again" type="button" class="link">Change Wi-Fi or pair again</button>
      </section>

      <details class="log">
        <summary>Console log</summary>
        <pre id="log"></pre>
      </details>

      <footer>
        <p>
          Open source under the Apache License 2.0:
          <a href="https://github.com/aivsomkar/openmausbot-gadget-sdk">source</a>,
          <a href="licenses.txt">third-party licenses</a>.
        </p>
        <p>The OpenMausBot name, the MausBot name and the Maus mascot are trademarks of Supamaus Software Private Limited.</p>
      </footer>
    </main>
  </body>
</html>
```

`site/style.css`:

```css
/* SPDX-License-Identifier: Apache-2.0 */
:root {
  --bg: #ffffff;
  --ink: #15181e;
  --mute: #5b6372;
  --line: #d9dee6;
  --accent: #007a45;
  --bad: #b3261e;
  --card: #f5f7f9;
  color-scheme: light dark;
}
@media (prefers-color-scheme: dark) {
  :root {
    --bg: #0d0f12;
    --ink: #f2f4f8;
    --mute: #9aa2b2;
    --line: #2a2f38;
    --accent: #2fd187;
    --bad: #ff5a4f;
    --card: #161a20;
  }
}
* { box-sizing: border-box; }
body { margin: 0; background: var(--bg); color: var(--ink); font: 16px/1.5 system-ui, -apple-system, "Segoe UI", sans-serif; }
main { max-width: 44rem; margin: 0 auto; padding: 2rem 1rem 4rem; }
h1 { font-size: 1.75rem; margin: 0 0 0.5rem; }
h2 { font-size: 1.15rem; margin: 0 0 0.75rem; }
.lede { margin: 0 0 0.5rem; }
.muted, .hint { color: var(--mute); font-size: 0.9rem; }
.alert { color: var(--bad); }
.step { border-top: 1px solid var(--line); padding: 1.25rem 0; }
.boards { display: grid; gap: 0.5rem; }
.board { display: grid; grid-template-columns: auto 1fr; column-gap: 0.75rem; padding: 0.75rem; border: 1px solid var(--line); border-radius: 10px; background: var(--card); cursor: pointer; }
.board input { grid-row: span 2; margin-top: 0.3rem; }
.board span { color: var(--mute); font-size: 0.9rem; }
.board.unpublished { opacity: 0.6; }
.row { display: flex; flex-wrap: wrap; gap: 0.75rem; align-items: center; }
button { font: inherit; padding: 0.55rem 1rem; border-radius: 8px; border: 1px solid var(--accent); background: var(--accent); color: var(--bg); cursor: pointer; }
button:disabled { opacity: 0.5; cursor: not-allowed; }
button.link, .networks button { background: transparent; color: var(--accent); }
label { display: block; margin: 0.5rem 0; }
input[type="text"], input:not([type]), input[type="password"] { display: block; width: 100%; max-width: 24rem; font: inherit; padding: 0.45rem 0.6rem; border: 1px solid var(--line); border-radius: 8px; background: var(--card); color: var(--ink); }
.check { display: flex; gap: 0.5rem; align-items: center; }
.networks { list-style: none; padding: 0; display: grid; gap: 0.35rem; }
.networks button { width: 100%; text-align: left; border-color: var(--line); }
progress { width: 100%; height: 0.75rem; }
.log pre { max-height: 16rem; overflow: auto; background: var(--card); padding: 0.75rem; border-radius: 8px; font-size: 0.8rem; }
footer { border-top: 1px solid var(--line); margin-top: 2rem; padding-top: 1rem; color: var(--mute); font-size: 0.85rem; }
a { color: var(--accent); }
```

- [ ] **Step 6: Write the build script**

`site/scripts/build.ts`:

```ts
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
```

- [ ] **Step 7: Write the Pages firmware check**

`site/scripts/check-firmware.ts`:

```ts
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
```

- [ ] **Step 8: Run the tests and the build**

Run: `cd site && npm test && npm run build && ls dist dist/firmware && cat dist/firmware/install.json && node scripts/check-firmware.ts dist/firmware; cd ..`
Expected: `# pass 73` (59 + the 10 tests from the review of Tasks 5–8 + the 4 from the review of Tasks 9–12), `# fail 0`; `site: built …/site/dist (bundled atob-lite, esptool-js, pako, spark-md5)`; `dist` lists `app.js  app.js.map  firmware  index.html  licenses.txt  style.css`; `install.json` is `{"version":null,"boards":{}}`; `check-firmware: no firmware published yet (placeholder install.json)`.

- [ ] **Step 9: Look at it in Chrome**

Run: `cd site && npm run serve` (leave it running; Ctrl-C to stop) and open http://127.0.0.1:8080/ in Chrome. esbuild's server stops as soon as its stdin closes, so from a non-interactive shell or an agent run `tail -f /dev/null | npm run serve &` instead, and stop it afterwards with `pkill -f 'esbuild --servedir=dist'`.
Expected: the heading "Install OpenMausBot Gadget", the line "No firmware is published yet…", four greyed boards, and choosing the devkit shows the USB-port hint. The console of the browser's developer tools shows no errors. (No board is needed for this step.)

- [ ] **Step 10: Commit**

```bash
git add site/src/boards.ts site/src/page.ts site/src/main.ts site/index.html site/style.css site/scripts/build.ts \
        site/scripts/check-firmware.ts site/test/page.test.ts site/test/check-firmware.test.ts
git commit -m "feat(site): installer page, build and Pages firmware check" \
  -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

### Task 11: Console helper for agents

**Files:**
- Create: `tools/console/omb_console.py`
- Test: `tools/console/test_omb_console.py`

**Interfaces:**
- Consumes: contract §2.11 console grammar and `@omb` lines; Task 5's `site/test/split-argv.c` and `site/test/awkward-values.json` (the firmware round-trip test compiles the same harness against P2a's `console.c`).
- Produces: `python3 tools/console/omb_console.py --port <path> [--wait S] [--pair CODE] [--wifi SSID PASSWORD] [--host auto|ADDRESS] [--until-paired] [LINE …]`. It first sends `status` every 0.5 s until any `@omb` line answers, for up to 10 s, and consumes those replies without printing them. The board may still be booting from `esptool write-flash`, and lines sent during boot are lost. Then it sends the raw LINEs, then `pair`, `wifi`, `host` (installer order), and prints each `@omb` line as one JSON line. With `--until-paired` it polls `status` every second, applying the installer's rules: a stale `pair: error`/`wifi: failed` counts only after progress on that side or three replies; `device_limit` keeps polling with one stderr hint; a single listed host is chosen with `host <address>`.
- Exit codes: 0 paired (or done); 1 wrong code or not paired within `--wait`; 2 bad arguments (argparse); 3 `hosts` listed none or several (ask for the address, rerun with `--host`); 4 `wifi: failed` (wrong password); 5 the app never answered (press RST or replug). The help text lists them. Exit 5 is used, not 2, because argparse already exits 2 for bad arguments.
- Functions and constants: `parse_omb_line`, `quote_arg`, `LineSplitter`, `build_lines`, `wait_for_app(port, clock, timeout=10, every=0.5)`, `run(port, lines, wait, until_paired, out, err, clock)`, `open_port(path)` (pyserial; after opening, RTS low then DTR low, so the lines go (1,1) → (1,0) → (0,0) and never pass through DTR=0/RTS=1, the USB-Serial-JTAG reset state), `EXIT_*`, `STALE_POLLS`. Used by AGENTS.md (Task 14).

- [ ] **Step 1: Write the failing tests**

`tools/console/test_omb_console.py`:

```python
# SPDX-License-Identifier: Apache-2.0
"""python3 -B -m unittest discover -s tools/console -p 'test_*.py'"""
import argparse
import io
import json
import os
import shutil
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(__file__))
import omb_console as oc  # noqa: E402

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
CC = os.environ.get("CC", "cc")


class FakeClock:
    def __init__(self):
        self.t = 0.0

    def __call__(self):
        self.t += 0.05
        return self.t


class FakePort:
    """A gadget console. Each `status` gets the next state in `states` (the last one repeats);
    `on_line(line, port)` sees every other line the helper writes."""

    def __init__(self, states=None, on_line=None):
        self.written = []
        self.pending = b""
        self.states = list(states or [{"pair": "connecting"}, {"pair": "connecting"}, {"pair": "paired"}])
        self.on_line = on_line

    def reply(self, msg):
        self.pending += b"\x1b[0;32mI (1) wifi: ok\x1b[0m\r\n@omb " + json.dumps(msg).encode() + b"\r\n"

    def write(self, data):
        line = data.decode()
        self.written.append(line)
        if line == "status\n":
            state = self.states.pop(0) if len(self.states) > 1 else self.states[0]
            self.reply(dict({"op": "status", "wifi": "connected", "id": "gad_x", "pair": "connecting", "fw": "1.0.0"}, **state))
        elif self.on_line is not None:
            self.on_line(line.rstrip("\n"), self)

    def read(self, n):
        out, self.pending = self.pending[:n], self.pending[n:]
        return out


class BootingPort(FakePort):
    """Ignores everything until the fake clock passes 1 s, like an app that is still booting."""

    def __init__(self, clock, **kwargs):
        super().__init__(**kwargs)
        self.clock = clock

    def write(self, data):
        if self.clock.t < 1.0:
            self.written.append(data.decode())
            return
        super().write(data)


class Pure(unittest.TestCase):
    def test_parse_omb_line(self):
        self.assertEqual(oc.parse_omb_line('@omb {"op":"say","turn":"t1-1"}\r'), {"op": "say", "turn": "t1-1"})
        self.assertEqual(oc.parse_omb_line('\x1b[0;32m@omb {"op":"say","turn":"t"}\x1b[0m')["op"], "say")
        self.assertIsNone(oc.parse_omb_line("I (12) boot: hello"))
        self.assertIsNone(oc.parse_omb_line("@omb {bad"))
        self.assertIsNone(oc.parse_omb_line('@omb ["op"]'))

    def test_quote_arg(self):
        self.assertEqual(oc.quote_arg('a "b" \\c'), '"a \\"b\\" \\\\c"')
        self.assertEqual(oc.quote_arg(""), '""')
        with self.assertRaises(ValueError):
            oc.quote_arg("a\nb")

    def test_line_splitter(self):
        s = oc.LineSplitter()
        self.assertEqual(s.feed("a\r"), ["a"])
        self.assertEqual(s.feed("\nb\nc"), ["b"])
        self.assertEqual(s.feed("\r\n"), ["c"])

    def test_build_lines_order(self):
        args = argparse.Namespace(lines=["log off"], pair="123456", wifi=["Home Net", "p w"], host="auto")
        self.assertEqual(oc.build_lines(args), ["log off", "pair 123456", 'wifi "Home Net" "p w"', "host auto"])
        with self.assertRaises(ValueError):
            oc.build_lines(argparse.Namespace(lines=[], pair="12345", wifi=None, host=None))


class Run(unittest.TestCase):
    def run_helper(self, port, lines, wait=10, until_paired=True, clock=None):
        out, err = io.StringIO(), io.StringIO()
        code = oc.run(port, lines, wait, until_paired, out=out, err=err, clock=clock or FakeClock())
        return code, out.getvalue(), err.getvalue()

    def test_until_paired_succeeds(self):
        port = FakePort()
        code, out, _ = self.run_helper(port, ["pair 123456"])
        self.assertEqual(code, 0)
        self.assertEqual(port.written[:2], ["status\n", "pair 123456\n"])  # the app answered before anything was sent
        self.assertIn('"pair": "paired"', out.splitlines()[-1])

    def test_until_paired_fails_on_error_and_timeout(self):
        wrong = FakePort([{"pair": "connecting"}, {"pair": "connecting"}, {"pair": "error", "error": "bad_code"}])
        self.assertEqual(self.run_helper(wrong, [])[0], oc.EXIT_FAILED)
        self.assertEqual(self.run_helper(FakePort([{"pair": "connecting"}]), [], wait=2)[0], oc.EXIT_FAILED)

    def test_plain_listen_prints_only_omb_lines(self):
        code, out, _ = self.run_helper(FakePort(), ["status"], wait=1, until_paired=False)
        self.assertEqual(code, 0)
        self.assertEqual([json.loads(l)["op"] for l in out.splitlines()], ["status"])

    def test_waits_for_the_app_before_sending_and_gives_up_on_silence(self):
        clock = FakeClock()
        port = BootingPort(clock)
        code, _, _ = self.run_helper(port, ["pair 123456"], clock=clock)
        self.assertEqual(code, 0)
        first = port.written.index("pair 123456\n")
        self.assertGreaterEqual(first, 2)  # some probes went unanswered while it booted
        self.assertTrue(all(w == "status\n" for w in port.written[:first]))
        silent = BootingPort(FakeClock())  # nothing advances its own clock, so it never finishes booting
        code, _, err = self.run_helper(silent, ["pair 123456"])
        self.assertEqual(code, oc.EXIT_NO_APP)
        self.assertIn("press RST", err)
        self.assertNotIn("pair 123456\n", silent.written)

    def test_a_retry_after_bad_code_ignores_the_stale_error(self):
        stale = {"pair": "error", "error": "bad_code"}
        port = FakePort([stale, stale, stale, {"pair": "paired"}])  # the probe takes the first
        self.assertEqual(self.run_helper(port, ["pair 654321"])[0], oc.EXIT_PAIRED)

    def test_wrong_wifi_password_exits_4(self):
        joining = {"wifi": "connecting", "pair": "code_stored"}
        port = FakePort([joining, joining, {"wifi": "failed", "pair": "code_stored"}])
        self.assertEqual(self.run_helper(port, ['wifi "Home" "wrongpass"'])[0], oc.EXIT_WIFI_FAILED)

    def test_no_mausbot_found_exits_3_at_once(self):
        def answer(line, port):
            if line == "host auto":
                port.reply({"op": "hosts", "hosts": []})

        port = FakePort([{"pair": "code_stored"}], on_line=answer)
        clock = FakeClock()
        code, out, _ = self.run_helper(port, ["host auto"], wait=150, clock=clock)
        self.assertEqual(code, oc.EXIT_NEED_HOST)
        self.assertIn('"op": "hosts"', out)
        self.assertLess(clock.t, 20)  # it did not wait out --wait 150

    def test_device_limit_keeps_polling_with_one_warning(self):
        limit = {"pair": "error", "error": "device_limit"}
        port = FakePort([limit] * 5 + [{"pair": "paired"}])
        code, _, err = self.run_helper(port, ["pair 123456"])
        self.assertEqual(code, oc.EXIT_PAIRED)
        self.assertEqual(err.count("too many devices"), 1)


class Firmware(unittest.TestCase):
    @unittest.skipUnless(shutil.which(CC), "no C compiler (" + CC + ")")
    def test_quote_arg_output_splits_back_exactly_with_the_firmware_splitter(self):
        # The same harness and values as site/test/console-firmware.test.ts.
        with open(os.path.join(REPO, "site/test/awkward-values.json"), encoding="utf-8") as f:
            values = json.load(f)
        with tempfile.TemporaryDirectory() as tmp:
            exe = os.path.join(tmp, "split-argv")
            subprocess.run(
                [CC, "-std=c11", "-Wall", "-Wextra", "-Werror", "-I", os.path.join(REPO, "firmware/core/include"),
                 os.path.join(REPO, "site/test/split-argv.c"), os.path.join(REPO, "firmware/core/src/console.c"), "-o", exe],
                check=True,
            )
            lines = "".join("wifi " + oc.quote_arg(v) + " " + oc.quote_arg("pass word") + "\n" for v in values)
            done = subprocess.run([exe], input=lines.encode("utf-8"), capture_output=True, check=True)
        got = [json.loads(line) for line in done.stdout.decode("utf-8").splitlines()]
        self.assertEqual(got, [["wifi", v, "pass word"] for v in values])


if __name__ == "__main__":
    unittest.main()
```

- [ ] **Step 2: Run them and watch them fail**

Run: `python3 -B -m unittest discover -s tools/console -p 'test_*.py'`
Expected: FAIL: `ModuleNotFoundError: No module named 'omb_console'`.

- [ ] **Step 3: Write the helper**

`tools/console/omb_console.py`:

```python
#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Talk to a gadget's USB console without a terminal: send lines, print @omb lines.

For coding agents and scripts (AGENTS.md). Needs pyserial, which esptool and
ESP-IDF install. The helper first waits until the gadget's app answers
`status` (a board that was just flashed is still booting), then sends every
LINE argument, then --pair, --wifi and --host (the browser installer's order).

  omb_console.py --port /dev/cu.usbmodem1101 "log off" status
  omb_console.py --port /dev/cu.usbmodem1101 scan --wait 10
  omb_console.py --port /dev/cu.usbmodem1101 "log off" --pair 123456 \\
      --wifi "Home Net" "secret pass" --host auto --until-paired --wait 150
"""
import argparse
import json
import re
import sys
import time

ANSI_RE = re.compile(r"\x1b\[[0-9;?]*[ -/]*[@-~]")
HOST_RE = re.compile(r"[A-Za-z0-9.-]+(:[0-9]{1,5})?")

EXIT_PAIRED = 0
EXIT_FAILED = 1  # a wrong code, or not paired within --wait
EXIT_NEED_HOST = 3  # MausBot not found (or several): ask for the address shown under Pair a gadget
EXIT_WIFI_FAILED = 4  # the gadget can't join the network: wrong Wi-Fi password
EXIT_NO_APP = 5  # the app never answered: press RST or unplug and replug the board

# A status left over from the previous attempt (spec §4.3 keeps pair=error
# after bad_code until the next pair) is not a result until that side shows
# progress or this many status replies have passed.
STALE_POLLS = 3
PAIR_PROGRESS = ("code_stored", "connecting", "paired")
WIFI_PROGRESS = ("connecting", "connected")

EXIT_CODES = """exit codes:
  0  done (with --until-paired: the gadget reported "pair": "paired")
  1  with --until-paired: a wrong code, or not paired within --wait
  2  bad arguments
  3  with --until-paired: MausBot not found (or several): ask for the address
     shown under Pair a gadget, rerun with --host <address> (and a new code if
     more than two minutes have passed)
  4  with --until-paired: the gadget can't join the network (wrong password)
  5  the gadget's app didn't answer: press RST or unplug and replug the board"""


def parse_omb_line(line):
    """The JSON object of an @omb line, or None for any other line."""
    clean = ANSI_RE.sub("", line).rstrip("\r")
    if not clean.startswith("@omb "):
        return None
    try:
        msg = json.loads(clean[5:])
    except ValueError:
        return None
    return msg if isinstance(msg, dict) and isinstance(msg.get("op"), str) else None


def quote_arg(value):
    """One console argument: "..." with \\\\ and \\" escapes (esp_console_split_argv rules)."""
    if "\r" in value or "\n" in value:
        raise ValueError("console arguments cannot contain line breaks")
    return '"' + value.replace("\\", "\\\\").replace('"', '\\"') + '"'


class LineSplitter:
    """Feed text chunks, get complete lines; CR, LF and CRLF each end one line."""

    def __init__(self):
        self.buf = ""
        self.last_cr = False

    def feed(self, text):
        lines = []
        for ch in text:
            if ch == "\n" and self.last_cr:
                self.last_cr = False
                continue
            self.last_cr = ch == "\r"
            if ch in "\r\n":
                lines.append(self.buf)
                self.buf = ""
            else:
                self.buf += ch
        return lines


def build_lines(args):
    lines = list(args.lines)
    if args.pair is not None:
        if not re.fullmatch(r"\d{6}", args.pair):
            raise ValueError("--pair needs the six-digit code from MausBot → Settings → Remote access → Pair a gadget")
        lines.append("pair " + args.pair)
    if args.wifi is not None:
        lines.append("wifi " + quote_arg(args.wifi[0]) + " " + quote_arg(args.wifi[1]))
    if args.host is not None:
        lines.append("host auto" if args.host == "auto" else "host " + args.host)
    return lines


def wait_for_app(port, clock, timeout=10.0, every=0.5):
    """Send `status` every `every` s until any @omb line comes back; False after `timeout` s.
    The probe's replies are consumed, not printed."""
    splitter = LineSplitter()
    deadline = clock() + timeout
    next_probe = clock()
    while clock() < deadline:
        if clock() >= next_probe:
            port.write(b"status\n")
            next_probe = clock() + every
        for line in splitter.feed(port.read(4096).decode("utf-8", "replace")):
            if parse_omb_line(line) is not None:
                return True
    return False


def run(port, lines, wait, until_paired, out=sys.stdout, err=sys.stderr, clock=time.monotonic):
    """Wait for the app, send lines, print @omb lines until `wait` seconds pass.
    With until_paired, poll status every second and return one of the EXIT_* codes."""
    if not wait_for_app(port, clock):
        print("the gadget's app didn't answer: press RST or unplug and replug the board", file=err)
        return EXIT_NO_APP
    splitter = LineSplitter()
    for line in lines:
        port.write((line + "\n").encode("utf-8"))
    deadline = clock() + wait
    next_poll = clock()
    polls = 0
    pair_moved = wifi_moved = warned_limit = False
    while clock() < deadline:
        if until_paired and clock() >= next_poll:
            port.write(b"status\n")
            next_poll = clock() + 1.0
        data = port.read(4096)
        for line in splitter.feed(data.decode("utf-8", "replace")):
            msg = parse_omb_line(line)
            if msg is None:
                continue
            print(json.dumps(msg, ensure_ascii=False), file=out, flush=True)
            if not until_paired:
                continue
            if msg.get("op") == "hosts":
                hosts = msg.get("hosts")
                only = hosts[0] if isinstance(hosts, list) and len(hosts) == 1 and isinstance(hosts[0], dict) else None
                address = only.get("address") if only is not None else None
                if isinstance(address, str) and HOST_RE.fullmatch(address):
                    port.write(("host " + address + "\n").encode("utf-8"))  # one MausBot: use it, like the installer
                    continue
                return EXIT_NEED_HOST
            if msg.get("op") != "status":
                continue
            polls += 1
            pair_moved = pair_moved or msg.get("pair") in PAIR_PROGRESS
            wifi_moved = wifi_moved or msg.get("wifi") in WIFI_PROGRESS
            settled = polls > STALE_POLLS
            if msg.get("pair") == "paired":
                return EXIT_PAIRED
            if msg.get("pair") == "error" and msg.get("error") == "device_limit":
                if not warned_limit:
                    print("MausBot has too many devices: remove one in MausBot → Settings → Remote access; "
                          "the gadget keeps trying for two minutes", file=err)
                warned_limit = True
                continue
            if msg.get("pair") == "error" and (pair_moved or settled):
                return EXIT_FAILED
            if msg.get("wifi") == "failed" and (wifi_moved or settled):
                return EXIT_WIFI_FAILED
    return EXIT_FAILED if until_paired else 0


def open_port(path):
    import serial  # pyserial

    port = serial.Serial(path, 115200, timeout=0.1)
    # The OS raises DTR and RTS when the port opens. DTR low with RTS high is the
    # USB-Serial-JTAG reset state, so drop RTS first, then DTR:
    # (DTR, RTS) goes (1,1) → (1,0) → (0,0) and never passes (0,1).
    port.rts = False
    port.dtr = False
    return port


def main(argv=None):
    p = argparse.ArgumentParser(
        description="Send console lines to a gadget and print its @omb lines.",
        epilog=EXIT_CODES,
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    p.add_argument("--port", required=True, help="serial port, e.g. /dev/cu.usbmodem1101, /dev/ttyACM0 or COM5")
    p.add_argument("--wait", type=float, default=3.0, help="seconds to listen after sending (default 3)")
    p.add_argument("--pair", metavar="CODE", help="six-digit code from Pair a gadget")
    p.add_argument("--wifi", nargs=2, metavar=("SSID", "PASSWORD"), help='use "" as the password for an open network')
    p.add_argument("--host", metavar="auto|ADDRESS", help="auto, or an address like 192.168.1.20:8810")
    p.add_argument("--until-paired", action="store_true", help="poll status; exit 0 once paired (see exit codes)")
    p.add_argument("lines", nargs="*", metavar="LINE", help="raw console lines, e.g. status, scan, \"log off\"")
    args = p.parse_args(argv)
    try:
        lines = build_lines(args)
    except ValueError as err:
        p.error(str(err))
    port = open_port(args.port)
    try:
        return run(port, lines, args.wait, args.until_paired)
    finally:
        port.close()


if __name__ == "__main__":
    sys.exit(main())
```

- [ ] **Step 4: Run the tests**

Run: `python3 -B -m unittest discover -s tools/console -p 'test_*.py'`
Expected: `Ran 26 tests` and `OK` (13 as first planned; the review of Tasks 9–12 adds 13, Deviations recorded during the build). The tests need only the standard library and a C compiler (`cc`; without one the firmware round-trip test reports `OK (skipped=1)`). pyserial is imported only when a real port is opened; the `Main` tests stand in for it through `sys.modules`.

- [ ] **Step 5: Check the help text**

Run: `python3 tools/console/omb_console.py --help`
Expected: usage text listing `--port`, `--wait`, `--pair`, `--wifi SSID PASSWORD`, `--host`, `--until-paired` and `LINE`, then the `exit codes:` block with 0, 1, 2, 3, 4 and 5.

- [ ] **Step 6: Commit**

```bash
chmod +x tools/console/omb_console.py
git add tools/console/omb_console.py tools/console/test_omb_console.py
git commit -m "feat(tools): non-interactive console helper for agents" \
  -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

### Task 12: Release, Pages and CI workflows

**Files:**
- Create: `.github/workflows/release.yml`
- Create: `.github/workflows/pages.yml`
- Modify: `.github/workflows/ci.yml` (append job `site` at the end of `jobs:`; contract §1.6 fixes the id)

**Interfaces:**
- Consumes: P2c's build command, `tools/check-size.sh <board>` and the `# CONFIG_GADGET_TEST_KEYS is not set` line in each built `sdkconfig` (contract §2.17); Tasks 2–4 CLIs; Task 10 `npm run build` and `scripts/check-firmware.ts`; Task 11 tests.
- Produces: `release.yml` (jobs `build` → `assemble` → `release`), `pages.yml` (jobs `build` → `deploy`, `workflow_dispatch` so `release.yml` can dispatch it), CI job `site`.

- [ ] **Step 1: Write `release.yml`**

The `build` job runs only the ESP-IDF build and never sees the key. `assemble` runs the release tool tests, the key-table check and every board's checks (image, keys, and the partition table against `firmware/ports/esp32/partitions/16mb.csv`, `collect.ts`'s default `--partitions`) without secrets. Only `release` runs in the `release` environment (required reviewers) and holds `GADGET_RELEASE_KEY_R1`; it runs no ESP-IDF build and no component manager, and installs no npm packages. Because it holds the key, its three actions are pinned by full commit SHA rather than by a tag that can move. The SHAs are `actions/checkout` v7.0.1, `actions/setup-node` v7.0.0 and `actions/download-artifact` v8.0.1, resolved with `git ls-remote https://github.com/actions/<name> refs/tags/<tag>` on 2026-10-04 (all three are lightweight tags on those commits). Every other job uses the exact versions P1's `ci.yml` uses. `sign.ts` still gets `--repo "$GITHUB_REPOSITORY"`, so a rename, transfer or fork fails loudly instead of publishing manifest URLs that MausBot rejects.

`.github/workflows/release.yml`:

```yaml
# SPDX-License-Identifier: Apache-2.0
# Builds every board for a v* tag, signs the app images with the release key
# in the protected `release` environment, publishes the release and refreshes
# the installer site (spec §8, contract §4.1–§4.3).
name: release

on:
  push:
    tags: ["v*"]

permissions:
  contents: read

concurrency:
  group: release-${{ github.ref }}
  cancel-in-progress: false

jobs:
  build:
    name: build (${{ matrix.board }})
    runs-on: ubuntu-24.04
    container: espressif/idf:v6.0.3
    strategy:
      fail-fast: true
      matrix:
        board: [amoled-175c, amoled-175, lcd-154, devkit]
    defaults:
      run:
        shell: bash
    steps:
      - uses: actions/checkout@v7.0.1
      - name: Version from the tag
        run: |
          set -euo pipefail
          VERSION="${GITHUB_REF_NAME#v}"
          if [[ ! "$VERSION" =~ ^[0-9]+\.[0-9]+\.[0-9]+(-[0-9A-Za-z.]+)?$ ]]; then
            echo "::error::tag $GITHUB_REF_NAME is not v<major>.<minor>.<patch>[-<pre>]"; exit 1
          fi
          if [[ "$VERSION" == *-dev ]]; then
            echo "::error::release versions never end in -dev"; exit 1
          fi
          echo "VERSION=$VERSION" >> "$GITHUB_ENV"
      - name: Build ${{ matrix.board }}
        env:
          BOARD: ${{ matrix.board }}
        run: |
          set -euo pipefail
          git config --global --add safe.directory '*'
          . "$IDF_PATH/export.sh"
          cd firmware/ports/esp32
          ARGS=(-B "build/$BOARD" -D GADGET_BOARD="$BOARD" -D SDKCONFIG="build/$BOARD/sdkconfig" -D PROJECT_VER="$VERSION")
          idf.py "${ARGS[@]}" build
          idf.py "${ARGS[@]}" merge-bin -o merged.bin
          tools/check-size.sh "$BOARD"
          grep -qxF '# CONFIG_GADGET_TEST_KEYS is not set' "build/$BOARD/sdkconfig"
      - name: Stage the build outputs
        env:
          BOARD: ${{ matrix.board }}
        run: |
          set -euo pipefail
          S="firmware/ports/esp32/build/$BOARD"
          D="raw/$BOARD"
          mkdir -p "$D/bootloader" "$D/partition_table"
          cp "$S/openmausbot-gadget.bin" "$S/ota_data_initial.bin" "$S/merged.bin" "$S/flasher_args.json" "$S/sdkconfig" "$D/"
          cp "$S/bootloader/bootloader.bin" "$D/bootloader/"
          cp "$S/partition_table/partition-table.bin" "$D/partition_table/"
      - uses: actions/upload-artifact@v7.0.1
        with:
          name: raw-${{ matrix.board }}
          path: raw/
          if-no-files-found: error

  assemble:
    needs: build
    runs-on: ubuntu-24.04
    steps:
      - uses: actions/checkout@v7.0.1
      - uses: actions/setup-node@v7.0.0
        with:
          node-version: 24
      - name: Release tool tests
        run: node --test "tools/release/test/*.test.ts"
      - name: Release key table holds only r* keys
        run: node tools/release/check-keys.ts
      - uses: actions/download-artifact@v8.0.1
        with:
          pattern: raw-*
          path: raw
          merge-multiple: true
      - name: Check and collect every board
        run: |
          set -euo pipefail
          VERSION="${GITHUB_REF_NAME#v}"
          for BOARD in amoled-175c amoled-175 lcd-154 devkit; do
            node tools/release/collect.ts --board "$BOARD" --version "$VERSION" --build "raw/$BOARD" \
              --assets out/assets --meta out/meta --keys keys
          done
      - uses: actions/upload-artifact@v7.0.1
        with:
          name: unsigned
          path: out/
          if-no-files-found: error

  release:
    needs: assemble
    runs-on: ubuntu-24.04
    environment: release
    permissions:
      contents: write
      actions: write
    steps:
      - uses: actions/checkout@3d3c42e5aac5ba805825da76410c181273ba90b1 # v7.0.1
      - uses: actions/setup-node@820762786026740c76f36085b0efc47a31fe5020 # v7.0.0
        with:
          node-version: 24
      - uses: actions/download-artifact@3e5f45b2cfb9172054b4087a40e8e0b5a5461e7c # v8.0.1
        with:
          name: unsigned
          path: out
      - name: Sign, and write manifest.json, install.json and SHA256SUMS
        env:
          GADGET_RELEASE_KEY_R1: ${{ secrets.GADGET_RELEASE_KEY_R1 }}
        run: >-
          node tools/release/sign.ts --assets out/assets --meta out/meta
          --tag "$GITHUB_REF_NAME" --repo "$GITHUB_REPOSITORY"
          --key-id r1 --key-env GADGET_RELEASE_KEY_R1 --pub keys/release-r1.pub.b64
      - name: Publish the release
        env:
          GH_TOKEN: ${{ github.token }}
        run: |
          set -euo pipefail
          FLAGS=(--repo "$GITHUB_REPOSITORY" --verify-tag --title "OpenMausBot Gadget $GITHUB_REF_NAME" --generate-notes)
          # Tags with a "-" are prereleases, so they never become "latest" (spec §8).
          if [[ "$GITHUB_REF_NAME" == *-* ]]; then FLAGS+=(--prerelease --latest=false); fi
          gh release create "$GITHUB_REF_NAME" out/assets/* "${FLAGS[@]}"
      - name: Refresh the installer site
        env:
          GH_TOKEN: ${{ github.token }}
        run: gh workflow run pages.yml --repo "$GITHUB_REPOSITORY" --ref main
```

- [ ] **Step 2: Write `pages.yml`**

`gh api …/releases/latest` returns 404 only when there is no published (non-pre)release; any other failure stops the job, so a GitHub outage never replaces published firmware with the placeholder.

`.github/workflows/pages.yml`:

```yaml
# SPDX-License-Identifier: Apache-2.0
# Builds the browser installer and publishes it with GitHub Pages. Release
# assets send no CORS headers, so this job copies the latest release's
# firmware into the site and the page loads it same-origin (spec §5.8).
name: pages

on:
  push:
    branches: [main]
    paths: ["site/**", ".github/workflows/pages.yml"]
  workflow_dispatch:

permissions:
  contents: read
  pages: write
  id-token: write

concurrency:
  group: pages
  cancel-in-progress: false

jobs:
  build:
    runs-on: ubuntu-24.04
    steps:
      - uses: actions/checkout@v7.0.1
      - uses: actions/setup-node@v7.0.0
        with:
          node-version: 24
          cache: npm
          cache-dependency-path: site/package-lock.json
      - name: Test and build the installer
        working-directory: site
        run: npm ci && npm test && npm run build
      - name: Copy the latest release's firmware into the site
        env:
          GH_TOKEN: ${{ github.token }}
        run: |
          set -euo pipefail
          cd site/dist/firmware
          if TAG=$(gh api "repos/$GITHUB_REPOSITORY/releases/latest" --jq .tag_name 2> "$RUNNER_TEMP/latest.err"); then
            echo "Latest release: $TAG"
            gh release download "$TAG" --repo "$GITHUB_REPOSITORY" \
              --pattern '*.bin' --pattern install.json --pattern SHA256SUMS --clobber
          elif grep -q 'HTTP 404' "$RUNNER_TEMP/latest.err"; then
            echo "No release yet: keeping the placeholder install.json"
          else
            cat "$RUNNER_TEMP/latest.err"; exit 1
          fi
      - name: Check the firmware folder
        run: node site/scripts/check-firmware.ts site/dist/firmware
      - uses: actions/configure-pages@v6.0.0
      - uses: actions/upload-pages-artifact@v5.0.0
        with:
          path: site/dist

  deploy:
    needs: build
    runs-on: ubuntu-24.04
    environment:
      name: github-pages
      url: ${{ steps.deployment.outputs.page_url }}
    steps:
      - id: deployment
        uses: actions/deploy-pages@v5.0.1
```

- [ ] **Step 3: Append the `site` job to `ci.yml`**

Add exactly these lines at the end of `.github/workflows/ci.yml` (two-space indent, inside `jobs:`; P1's file ends with its last job, so the new job goes after it). Do not edit any other job.

`.github/workflows/ci.yml (append)`:

```yaml
  site:
    runs-on: ubuntu-24.04
    steps:
      - uses: actions/checkout@v7.0.1
      - uses: actions/setup-node@v7.0.0
        with:
          node-version: 24
          cache: npm
          cache-dependency-path: site/package-lock.json
      - name: Installer tests and build
        working-directory: site
        run: npm ci && npm test && npm run build
      - name: Release tools, licensing and docs tests
        run: node --test "tools/release/test/*.test.ts"
      - name: Release tools type check
        run: site/node_modules/.bin/tsc -p tools/release/tsconfig.json
      - name: Console helper tests
        run: python3 -B -m unittest discover -s tools/console -p 'test_*.py'
```

- [ ] **Step 4: Check that all three files parse**

Run: `for f in ci release pages; do npx --yes js-yaml@4.1.0 .github/workflows/$f.yml > /dev/null && echo "$f.yml parses"; done && grep -c '^  site:$' .github/workflows/ci.yml`
Expected: `ci.yml parses`, `release.yml parses`, `pages.yml parses`, then `1`.

- [ ] **Step 5: Run the workflows' shell steps against a fake `gh`**

Save this script as `/private/tmp/p2d-wfsim.sh` (outside the repository) and run `bash /private/tmp/p2d-wfsim.sh /Users/omkar/Desktop/openmaus/openmausbot-gadget-sdk`:

`/private/tmp/p2d-wfsim.sh`:

```bash
#!/bin/bash
# Runs the shell steps of release.yml and pages.yml against a fake `gh`.
# Usage: bash simulate-workflow-steps.sh /Users/omkar/Desktop/openmaus/openmausbot-gadget-sdk
set -euo pipefail
REPO="$1"
SIM=/private/tmp/p2d-wfsim
rm -rf "$SIM" && mkdir -p "$SIM/bin" "$SIM/tmp"
cd "$SIM"
npm init -y >/dev/null && npm install --no-audit --no-fund js-yaml@4.1.0 >/dev/null 2>&1
cat > bin/gh <<'GH'
#!/bin/bash
echo "gh $*" >> "$GH_LOG"
case "$1 $2" in
  "api repos/"*)
    case "$FAKE_MODE" in
      ok) echo "v1.1.0" ;;
      none) echo "gh: Not Found (HTTP 404)" >&2; exit 1 ;;
      down) echo "error connecting to api.github.com" >&2; exit 1 ;;
    esac ;;
  "release download") echo '{"version":"1.1.0","boards":{}}' > install.json ;;
esac
GH
chmod +x bin/gh
node -e '
const y = require("js-yaml"), fs = require("fs"), repo = process.argv[1];
const load = (f) => y.load(fs.readFileSync(`${repo}/.github/workflows/${f}`, "utf8"));
const p = load("pages.yml"), r = load("release.yml");
fs.writeFileSync("pages-copy.sh", p.jobs.build.steps.find((s) => s.name === "Copy the latest release'"'"'s firmware into the site").run);
fs.writeFileSync("publish.sh", r.jobs.release.steps.find((s) => s.name === "Publish the release").run);
fs.writeFileSync("version.sh", r.jobs.build.steps.find((s) => s.name === "Version from the tag").run);
' "$REPO"
export PATH="$SIM/bin:$PATH" GH_LOG="$SIM/gh.log" RUNNER_TEMP="$SIM/tmp" GITHUB_REPOSITORY=aivsomkar/openmausbot-gadget-sdk
for mode in ok none down; do
  rm -rf w && mkdir -p w/site/dist/firmware && echo '{"version":null,"boards":{}}' > w/site/dist/firmware/install.json
  if (cd w && FAKE_MODE=$mode bash -e ../pages-copy.sh >/dev/null 2>&1); then rc=0; else rc=$?; fi
  echo "pages $mode: exit $rc, install.json $(cat w/site/dist/firmware/install.json)"
done
: > gh.log
for tag in v1.1.0 v1.2.0-rc.1; do
  rm -rf w && mkdir -p w/out/assets && touch w/out/assets/a.bin
  (cd w && GITHUB_REF_NAME=$tag bash -e ../publish.sh)
done
sed 's/^/publish: /' gh.log
for tag in v1.1.0 v1.2.0-rc.1 v1.1.0-dev v1.1 v1.1.0+meta; do
  : > env.txt
  if GITHUB_REF_NAME=$tag GITHUB_ENV=env.txt bash version.sh >/dev/null 2>&1; then echo "version $tag: $(cat env.txt)"; else echo "version $tag: refused"; fi
done
```

Expected output:

```
pages ok: exit 0, install.json {"version":"1.1.0","boards":{}}
pages none: exit 0, install.json {"version":null,"boards":{}}
pages down: exit 1, install.json {"version":null,"boards":{}}
publish: gh release create v1.1.0 out/assets/a.bin --repo aivsomkar/openmausbot-gadget-sdk --verify-tag --title OpenMausBot Gadget v1.1.0 --generate-notes
publish: gh release create v1.2.0-rc.1 out/assets/a.bin --repo aivsomkar/openmausbot-gadget-sdk --verify-tag --title OpenMausBot Gadget v1.2.0-rc.1 --generate-notes --prerelease --latest=false
version v1.1.0: VERSION=1.1.0
version v1.2.0-rc.1: VERSION=1.2.0-rc.1
version v1.1.0-dev: refused
version v1.1: refused
version v1.1.0+meta: refused
```

- [ ] **Step 6: Run what the `site` job runs**

Run: `(cd site && npm ci && npm test && npm run build) && node --test "tools/release/test/*.test.ts" && site/node_modules/.bin/tsc -p tools/release/tsconfig.json && python3 -B -m unittest discover -s tools/console -p 'test_*.py'`
Expected: site `# pass 73`; release tools `# pass 33` (Tasks 1–4; Tasks 13–14 add 7 more); no type errors; `Ran 26 tests` and `OK` (counts after the review of Tasks 9–12).

- [ ] **Step 7: Commit**

```bash
git add .github/workflows/release.yml .github/workflows/pages.yml .github/workflows/ci.yml
git commit -m "ci: release, Pages and installer workflows" \
  -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

### Task 13: Licensing files and the original-work rule

**Files:**
- Create: `NOTICE`
- Create: `THIRD_PARTY.md`
- Create: `CONTRIBUTING.md`
- Test: `tools/release/test/licensing.test.ts`

**Interfaces:**
- Consumes: every dependency manifest the earlier plans committed: root `package.json` (P1), `site/package.json` (Task 5), `tools/art/package.json` (P2b), every `firmware/ports/esp32/**/idf_component.yml` (P2c), `firmware/cmake/deps.cmake` (P2a/P2b); the Python files in `tools/console/` (Task 11; `import serial` means pyserial); `tools/art/source/README.md` (P2b). The console helper is new scope beyond the spec's layout, and pyserial is the dependency it brings with it.
- Produces: the licensing check that later dependency additions must keep green.

- [ ] **Step 1: Write the failing tests**

`tools/release/test/licensing.test.ts`:

```ts
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
  for (const pkg of ["package.json", "site/package.json", "tools/art/package.json"]) {
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
```

- [ ] **Step 2: Run them and watch them fail**

Run: `node --test tools/release/test/licensing.test.ts`
Expected: FAIL: "add these to THIRD_PARTY.md: …" listing every dependency (including `pyserial (from tools/console/omb_console.py)`), "only 0 rows", and the NOTICE assertion.

- [ ] **Step 3: Write `NOTICE`**

`NOTICE`:

```text
OpenMausBot Gadget SDK
Copyright 2026 The OpenMausBot Gadget SDK authors

This product is licensed under the Apache License, Version 2.0 (see LICENSE).

The OpenMausBot name, the MausBot name and the Maus mascot are trademarks of Supamaus Software Private Limited.

The Maus art in firmware/ui/art is generated by tools/art from the mascot
geometry of OpenMausBot (Apache-2.0) at commit
6dd4403d8fbbbd5c17169724cb2a529f11d7543e. tools/art/source/README.md
records its provenance.

The Maus art derives from OpenMausBot, Copyright 2026 Milind Soni and OpenMausBot contributors, licensed under the Apache License, Version 2.0.

This product includes third-party software. THIRD_PARTY.md lists every
component and its license. Firmware releases contain ESP-IDF and the
components listed there; the installer page bundles esptool-js and its
dependencies, whose notices it serves as licenses.txt.
```

- [ ] **Step 4: Write `THIRD_PARTY.md`**

`THIRD_PARTY.md`:

```markdown
# Third-party software

Every dependency of this repository, with its license. The **Identifier** column holds the exact name the build uses (npm package, ESP-IDF managed component or CMake FetchContent name); `node --test "tools/release/test/*.test.ts"` checks that every direct dependency appears here. All of these licenses allow distribution under the project's Apache-2.0 license.

## In the firmware and the simulator

| Component | Identifier | Version | License | Where |
|---|---|---|---|---|
| ESP-IDF | `esp-idf` | v6.0.3 (builds on v5.5.5) | Apache-2.0; it bundles further components (FreeRTOS, lwIP, newlib and others) under their own licenses, listed in ESP-IDF's "Copyrights and Licenses" page | Board firmware |
| ESP WebSocket client | `espressif/esp_websocket_client` | ^1.8.0 | Apache-2.0 | Board firmware |
| mDNS | `espressif/mdns` | ^1.14.0 | Apache-2.0 | Board firmware |
| cJSON (ESP-IDF component) | `espressif/cjson` | ^1.7.19 | MIT | Board firmware |
| Audio codec driver | `espressif/esp_codec_dev` | ^1.6.2 | Apache-2.0 | Board firmware |
| CO5300 AMOLED driver | `espressif/esp_lcd_co5300` | ^2.2.0 | Apache-2.0 | amoled-175c, amoled-175 |
| LCD touch framework | `espressif/esp_lcd_touch` | ^1.2.1 | Apache-2.0 | amoled-175c, amoled-175 |
| CST9217 touch driver | `waveshare/esp_lcd_touch_cst9217` | ^2.0.0 | Apache-2.0 | amoled-175c, amoled-175 |
| LVGL | `lvgl/lvgl`, `lvgl` | 9.6.0 | MIT | Firmware UI and simulator |
| lodepng (bundled with LVGL) | `lodepng` | as in LVGL 9.6.0 | Zlib | Simulator snapshots only |
| cJSON | `cjson` | 1.7.19 | MIT | Simulator and tests |
| Mbed TLS | `mbedtls` | 3.6.7 and 4.2.0 (desktop); the version ESP-IDF bundles (device) | Apache-2.0 (dual-licensed Apache-2.0 OR GPL-2.0-or-later; used under Apache-2.0) | PSA crypto |
| wslay | `wslay` | 1.1.1 | MIT | Simulator WebSocket client |
| SDL2 | `SDL2` | 2.x from the system | Zlib | Simulator window |
| Unity | `unity` | 2.7.0 | MIT | C unit tests only, never shipped |
| Montserrat | `Montserrat` | the copy in LVGL 9.6.0 | OFL-1.1 | Latin-1 fonts generated into `firmware/ui/fonts` |

Espressif's and Waveshare's components are fetched at build time by the ESP-IDF component manager and are never copied into this repository.

## Build-time and tooling only (never in the firmware)

| Component | Identifier | Version | License | Where |
|---|---|---|---|---|
| noble-curves | `@noble/curves` | 2.4.0 | MIT | Protocol vector generator |
| noble-hashes | `@noble/hashes` | 2.4.0 | MIT | Dependency of `@noble/curves` |
| ws | `ws` | 8.22.0 | MIT | Fake host |
| resvg-js | `@resvg/resvg-js` | 2.6.2 | MPL-2.0 | Art generator (`tools/art`) |
| pngjs | `pngjs` | 7.0.0 | MIT | Art generator |
| lv_font_conv | `lv_font_conv` | 1.5.3 | MIT | Font generator |
| esbuild | `esbuild` | 0.28.2 | MIT | Installer build |
| TypeScript | `typescript` | 5.9.3 | Apache-2.0 | Installer type check |
| Node.js type definitions | `@types/node` | 24.19.1 | MIT | Installer type check |
| Web Serial type definitions | `@types/w3c-web-serial` | 1.0.8 | MIT | Installer type check |
| SparkMD5 type definitions | `@types/spark-md5` | 3.0.5 | MIT | Installer type check |
| pyserial | `pyserial` | 3.5 (installed with esptool or ESP-IDF) | BSD-3-Clause | Console helper (`tools/console/omb_console.py`) |

## In the installer page

These are bundled into the installer's `app.js`; the page serves their full license texts as `licenses.txt`.

| Component | Identifier | Version | License |
|---|---|---|---|
| esptool-js | `esptool-js` | 0.7.0 | Apache-2.0 |
| pako (esptool-js dependency) | `pako` | 2.x, locked in `site/package-lock.json` | MIT AND Zlib |
| atob-lite (esptool-js dependency) | `atob-lite` | 2.0.0 | MIT |
| SparkMD5 | `spark-md5` | 3.0.2 | WTFPL OR MIT (used under MIT) |

## Source material

| Material | License | Notes |
|---|---|---|
| OpenMausBot mascot geometry (`shared/mascot-bodies.ts`, `src/components/cursor-face-data.ts`) | Apache-2.0 | Pinned commit `6dd4403d8fbbbd5c17169724cb2a529f11d7543e`; see `tools/art/source/README.md`. The OpenMausBot name, the MausBot name and the Maus mascot are trademarks of Supamaus Software Private Limited. |
```

- [ ] **Step 5: Write `CONTRIBUTING.md`**

`CONTRIBUTING.md`:

```markdown
# Contributing

Thanks for helping. This file covers the rules every change follows, how to run the tests, and how to send a change.

## Original-work rule

Everything in this repository is written for this project. Contributors and coding agents must not copy code, documentation, art or protocol text from other gadget SDKs or device firmware projects, and the repository does not refer to them: no links, no names, no "inspired by" notes.

What you may use:

- Vendor datasheets, schematics and reference manuals (Espressif, Waveshare, the codec, display and power-chip makers).
- Espressif's and Waveshare's published driver components, fetched at build time by the ESP-IDF component manager under their own licenses. Never copy their source into this repository.
- The documentation and public APIs of the libraries listed in [THIRD_PARTY.md](THIRD_PARTY.md).
- OpenMausBot itself (Apache-2.0), which the Maus art is generated from.

If you are unsure whether a source is allowed, ask in the pull request before using it. A pull request that breaks this rule is closed, whatever else it does.

## Dependencies and licenses

Every new dependency (npm package, ESP-IDF managed component, CMake FetchContent download, font or generated asset source) needs a row in [THIRD_PARTY.md](THIRD_PARTY.md) with its identifier, version and license. `node --test "tools/release/test/*.test.ts"` fails when one is missing. Licenses must be compatible with Apache-2.0 distribution.

## Setting up

- Node.js 22.18 or newer (CI uses Node 24).
- CMake 3.24 or newer, a C11 compiler, and SDL2 for the simulator window.
- ESP-IDF v6.0.3 for the boards ([AGENTS.md](AGENTS.md) has the install steps).

## Tests

Run what your change touches; CI runs all of it on every push and pull request.

| Area | Command |
|---|---|
| Protocol vectors and fake host | `npm ci && npm run vectors:check && npm test` |
| Firmware core, simulator, end to end, snapshots | `cmake -S firmware -B build/host -DCMAKE_BUILD_TYPE=Debug && cmake --build build/host -j10 && ctest --test-dir build/host --output-on-failure -L "unit|vectors|e2e|snapshot"` |
| One board | see "Build and flash a board" in AGENTS.md, then `tools/check-size.sh <board>` |
| Installer | `cd site && npm ci && npm test && npm run build` |
| Release tools, licensing and docs | `node --test "tools/release/test/*.test.ts"` |
| Console helper | `python3 -B -m unittest discover -s tools/console -p 'test_*.py'` |

Generated files are committed and checked for drift: protocol vectors (`npm run vectors`), the Maus art and fonts (`cd tools/art && npm run art && npm run fonts`) and snapshot goldens. Regenerate them with the tool, never by hand.

## Code style

- C is C11 and must also compile as gnu23 under ESP-IDF 6 with warnings as errors. Core and UI are single-threaded and include only what `firmware/core` allows.
- TypeScript runs directly on Node (type stripping): erasable syntax only (no `enum`, no parameter properties, no namespaces), and relative imports end in `.ts`.
- Every source file starts with `SPDX-License-Identifier: Apache-2.0`.
- User-facing text says "MausBot → Settings → Remote access → Pair a gadget" for pairing.

## Sending a change

1. Open an issue first for anything bigger than a fix.
2. Keep each pull request to one change, with its tests.
3. Describe what you tested on real hardware, if anything. Changes to board code need a run through [docs/hardware-checklist.md](docs/hardware-checklist.md) for that board.

By contributing, you agree that your contribution is licensed under the Apache License 2.0, the same license as the project.

## Security

Please report security problems privately through GitHub's "Report a vulnerability" button on the repository's Security tab, not in a public issue.
```

- [ ] **Step 6: Run the tests**

Run: `node --test tools/release/test/licensing.test.ts`
Expected: `# pass 3` and `# fail 0`.

If the first test names a dependency the table lacks (an earlier plan added something the contract did not pin, for example a FetchContent name spelled differently), add a row for it: its identifier exactly as the build spells it, its version from its manifest, and its license from its registry page or license file. Never weaken the check. If the third test fails only on `tools/art/source/README.md`, P2b's provenance file lacks the trademark sentence: report it to P2b instead of editing their file.

- [ ] **Step 7: Commit**

```bash
git add NOTICE THIRD_PARTY.md CONTRIBUTING.md tools/release/test/licensing.test.ts
git commit -m "docs: NOTICE, THIRD_PARTY.md and CONTRIBUTING with the original-work rule" \
  -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

### Task 14: README, AGENTS.md and the installer checklist

**Files:**
- Modify: `README.md` (replace P1's stub entirely)
- Create: `AGENTS.md`
- Create: `docs/installer-checklist.md`
- Test: `tools/release/test/docs.test.ts`

**Interfaces:**
- Consumes: commands pinned in contract §1.4–§1.5 (ESP-IDF install with EIM's macOS prerequisites in front, as in [Contract notes](#contract-notes); board build, simulator at `build/host/ports/sim/gadget-sim`, fake host), §2.3 board table, §2.10 `gadget_action_register`, §2.11 console, §2.16 simulator flags, §2.17 `board_api.h`; Task 11's helper; Task 10's copy; P4a's rule (its Task 10, "never runs a regular expression from a gadget's schema") that MausBot refuses, with 400, to run an action whose params schema uses `pattern`, `patternProperties` or `"format": "regex"` anywhere inside it, while a param merely named `pattern` is fine.
- Produces: the docs the spec requires (§5.10, §6.4 copy rule, §11).

- [ ] **Step 1: Write the failing tests**

`tools/release/test/docs.test.ts`:

```ts
// SPDX-License-Identifier: Apache-2.0
// Spec §5.10, §6.4 and §11: the docs say what the spec requires them to say.
import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
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
```

- [ ] **Step 2: Run them and watch them fail**

Run: `node --test tools/release/test/docs.test.ts`
Expected: FAIL on the README headline (P1's stub) and `ENOENT` for `AGENTS.md`. The `docs/release-keys.md` test already passes (Task 4).

- [ ] **Step 3: Replace `README.md`**

`README.md`:

````markdown
# OpenMausBot Gadget SDK

Turn a small ESP32-S3 board with a screen, a microphone and a speaker into a desk terminal for your own MausBot. Hold to talk and your words go to the bot running on your computer. The reply appears on the screen and is spoken back. The gadget also shows your bot's approval questions so you can answer with a tap, shows what routines and background work push to it, and gives bots a few tools to drive it.

This repository has the firmware for four boards, a desktop simulator that runs the same UI, a browser installer and the protocol. The host side ships inside MausBot itself: there is nothing else to run.

## Ask your MausBot to flash it

The quickest way is to ask. Plug the board into your computer and tell your MausBot something like:

> Flash my Waveshare 1.75C with the OpenMausBot gadget firmware.

Your bot follows [AGENTS.md](AGENTS.md): it installs the official firmware (or builds it from source if you ask), then helps you connect the gadget to Wi-Fi and pair it. On Windows it usually opens the browser installer below for you.

Two things to know first:

- **Remote access must be on.** The gadget talks to MausBot through Remote access, which is off by default. Turn it on in MausBot → Settings → Remote access. If your organization blocks it, the gadget can't connect.
- **Pairing starts in MausBot.** Open **MausBot → Settings → Remote access → Pair a gadget**. It shows a six-digit code and lets you pick the bot the gadget talks to. Type the code into the installer (or give it to your bot).

## Or use the browser installer

Open **https://aivsomkar.github.io/openmausbot-gadget-sdk/** in Chrome or Edge on a computer:

1. Pick your board.
2. Click **Connect and install** and choose the board's USB port. A normal install keeps the gadget's identity, Wi-Fi and pairing; **Erase everything** wipes them.
3. Pick your Wi-Fi network from the list the gadget finds, and type its password.
4. Open MausBot → Settings → Remote access → Pair a gadget and type the six-digit code.

If MausBot runs on Windows and your network is set to Public, Windows' firewall blocks the gadget: set the network's profile type to Private in Windows Settings → Network & internet.

## Boards

| Board | Screen | Notes |
|---|---|---|
| Waveshare ESP32-S3-Touch-AMOLED-1.75C (`amoled-175c`) | 466×466 round AMOLED, touch | Aluminum case, built-in speaker, battery bay. BOOT is TALK, PWR is CANCEL (holding PWR for 6 s powers it off) |
| Waveshare ESP32-S3-Touch-AMOLED-1.75 (`amoled-175`) | 466×466 round AMOLED, touch | Speaker connector, battery |
| Waveshare ESP32-S3-LCD-1.54 (`lcd-154`) | 240×240 LCD | BOOT is TALK, PLUS is CANCEL; onboard speaker; battery |
| ESP32-S3-DevKitC-1-N16R8 breadboard build (`devkit`) | 2" ST7789, 320×240 | INMP441 mic, MAX98357A amp, two buttons. Use the USB-C port labelled USB |

All four need 16 MB of flash. The 8 MB DevKitC-1-N8R8 is not supported.

## What it does

- **Talk and listen.** Hold anywhere (or TALK) to talk, release to send. Speech-to-text and voices run on your computer, so better voices never need a firmware update.
- **Approvals.** When a bot wants to run a tool, the gadget shows Allow and Deny.
- **Push.** Routine results and messages your bot sends on its own appear as a toast with a chime, and can be read aloud.
- **Bot tools.** Bots can show cards and images on the gadget and run actions you add.
- **Updates.** MausBot shows Update available for official releases and installs them over the air. Firmware you build yourself is a custom build and updates over USB.

## Build it yourself

Everything a coding agent or a maker needs is in [AGENTS.md](AGENTS.md): installing ESP-IDF v6.0.3, building and flashing each board, the serial console, the simulator and the fake host, adding a board and adding an action. Try the UI without hardware:

```sh
cmake -S firmware -B build/host -DCMAKE_BUILD_TYPE=Debug && cmake --build build/host -j10
npm ci
node tools/fake-host/src/main.ts --port 8810 --code 123456 &
./build/host/ports/sim/gadget-sim --board amoled-175c --host 127.0.0.1:8810 --pair 123456
```

## Documentation

- [AGENTS.md](AGENTS.md): build, flash, monitor, simulate, add a board or an action
- [protocol/PROTOCOL.md](protocol/PROTOCOL.md): the `openmausbot-gadget/1` protocol
- [docs/hardware-checklist.md](docs/hardware-checklist.md) and [docs/installer-checklist.md](docs/installer-checklist.md): checks on real boards
- [docs/release-keys.md](docs/release-keys.md): how releases are signed
- [CONTRIBUTING.md](CONTRIBUTING.md): how to contribute, including the original-work rule
- [THIRD_PARTY.md](THIRD_PARTY.md): every dependency and its license

## License

Apache License 2.0. See [LICENSE](LICENSE) and [NOTICE](NOTICE).

The OpenMausBot name, the MausBot name and the Maus mascot are trademarks of Supamaus Software Private Limited.
````

- [ ] **Step 4: Write `AGENTS.md`**

`AGENTS.md`:

````markdown
# AGENTS.md

Instructions for coding agents working with this repository, MausBot's own bots first. Every command here runs from the repository root unless a step says otherwise.

## Rules

- **Original work only.** Write code, docs and art yourself, from vendor datasheets, schematics, Espressif's and Waveshare's published driver components, and the documentation of the libraries in [THIRD_PARTY.md](THIRD_PARTY.md). Never copy code, documentation, art or protocol text from other gadget SDKs or device firmware projects, and never link to them. [CONTRIBUTING.md](CONTRIBUTING.md) has the full rule.
- **Ask for the pairing code.** The person opens MausBot → Settings → Remote access → Pair a gadget and tells you the six digits, or types them into the installer. Never read a code from MausBot's local ports yourself.
- **Remote access must be on** in MausBot before a gadget can pair or connect.
- **Never commit secrets.** The release private key exists only in the GitHub `release` environment. `keys/test-t1.key.hex` is a test key, committed on purpose, and never compiled into release firmware.
- **Your builds are custom builds.** A local build reports `0.0.0-dev` (or the `PROJECT_VER` you pass). MausBot shows "Custom build" for any `-dev` firmware and never updates it over the air; reflash it over USB.

## Repository map

| Path | What |
|---|---|
| `protocol/` | `PROTOCOL.md` (the normative protocol), test vectors, the vector generator |
| `firmware/core/` | Portable C11: protocol codec, session, interaction, audio, OTA, console, PSA crypto |
| `firmware/ui/` | LVGL screens, the Maus animation, generated art and fonts |
| `firmware/ports/esp32/` | The ESP-IDF application, `partitions/16mb.csv`, `boards/<board>/` |
| `firmware/ports/sim/` | The desktop simulator |
| `firmware/tests/` | C unit tests, headless scripts, simulator ↔ fake-host end-to-end tests, snapshot goldens |
| `tools/fake-host/` | A Node stand-in for MausBot |
| `tools/art/` | Generates the Maus art and fonts |
| `tools/console/omb_console.py` | Sends console lines to a gadget and prints its `@omb` lines |
| `tools/release/` | Release helpers used by `.github/workflows/release.yml` |
| `site/` | The browser installer |

## Flash a gadget for someone

### Official firmware

The quickest way is the browser installer: open https://aivsomkar.github.io/openmausbot-gadget-sdk/ for the person in Chrome or Edge. They pick the board, click **Connect and install**, choose a Wi-Fi network and type the code from Pair a gadget. On Windows the browser installer is the preferred path; [On Windows](#on-windows) has the terminal flow.

From a terminal on macOS or Linux (bash or zsh), with Python 3.10 or newer. First get this repository, which has the console helper, unless you are already in a clone of it:

```sh
git clone --depth 1 https://github.com/aivsomkar/openmausbot-gadget-sdk.git ~/openmausbot-gadget-sdk && cd ~/openmausbot-gadget-sdk
```

Then download the latest release's separate parts. The parentheses run the download in a subshell, so your shell stays at the repository root:

```sh
python3 -m venv ~/.openmausbot-gadget/venv
~/.openmausbot-gadget/venv/bin/pip install "esptool>=5.3,<6"
(
  mkdir -p /tmp/omb-fw && cd /tmp/omb-fw
  BASE=https://github.com/aivsomkar/openmausbot-gadget-sdk/releases/latest/download
  BOARD=amoled-175c                      # amoled-175c, amoled-175, lcd-154 or devkit
  curl -fsSLO "$BASE/install.json"
  python3 -c 'import json,sys; b=json.load(open("install.json"))["boards"][sys.argv[1]]; print("\n".join(p["offset"]+" "+p["path"] for p in b["parts"]))' "$BOARD" > parts.txt
  for f in $(awk '{print $2}' parts.txt); do curl -fsSLO "$BASE/$f"; done
)
ls /dev | grep -E '^(cu\.usbmodem|ttyACM)'   # the board's port: cu.usbmodem… on macOS, ttyACM… on Linux
```

Flash them, with `/dev/` plus the name `ls` printed (for example `/dev/ttyACM0`) in place of `/dev/cu.usbmodem1101`:

```sh
(cd /tmp/omb-fw && xargs ~/.openmausbot-gadget/venv/bin/esptool --chip esp32s3 --port /dev/cu.usbmodem1101 write-flash < parts.txt)
```

`parts.txt` holds one `<offset> <file>` line per part, straight from `install.json`; never hard-code the offsets. `write-flash` resets the board into the new firmware when it finishes. These forms work in zsh (macOS's default shell) as well as bash: a glob such as `/dev/ttyACM*` that matches nothing stops zsh, and zsh does not split an unquoted `$VAR` into words, which is why the parts go through `parts.txt` and `xargs`.

Never flash the `-full.bin` image in this flow. It is the merged image for recovery only, and writing it at `0x0` erases NVS: the gadget's identity key, Wi-Fi and pairing. After a recovery flash the gadget pairs again as a new device, and the person can remove the old entry in Remote access.

Then set up Wi-Fi and pairing, either in the installer (**Already installed? Set up Wi-Fi and pairing**) or with the console helper, from the repository root. Ask the person for the network, its password and the code first:

```sh
~/.openmausbot-gadget/venv/bin/python tools/console/omb_console.py --port /dev/cu.usbmodem1101 "log off" scan --wait 10
~/.openmausbot-gadget/venv/bin/python tools/console/omb_console.py --port /dev/cu.usbmodem1101 \
    --pair 123456 --wifi "Home Net" "the password" --host auto --until-paired --wait 150
```

The helper waits up to 10 s for the gadget's app to answer before it sends anything, because a board that was just flashed is still starting. The second command's exit code says what to do next:

| Exit | Meaning | Next |
|---|---|---|
| 0 | The gadget reported `"pair":"paired"` | Done |
| 1 | The code was wrong, the gadget refused a setup command (stderr says which and why), or pairing did not finish within `--wait` | Check that Remote access is on; ask for a new code and rerun. If stderr says MausBot still has too many devices, ask the person to remove one in MausBot → Settings → Remote access first |
| 3 | MausBot wasn't found (or several were; the printed `@omb {"op":"hosts",…}` line lists them) | Ask the person for the address shown under Pair a gadget and rerun with `--host <address>`. If more than two minutes have passed, ask for a new code too: codes last 120 s |
| 4 | The gadget can't join the network | Ask for the Wi-Fi password again and rerun |
| 5 | The gadget's app didn't answer, or the board's port closed or could not be opened | Check `--port`; ask the person to press RST or unplug and replug the board, then rerun |

If MausBot already has the most devices it allows, the helper prints a line about it on stderr and keeps waiting: once the person removes a device in MausBot → Settings → Remote access, the gadget pairs by itself within two minutes. If none is removed in time, the gadget drops the code and the helper exits 1 with a line saying so.

### On Windows

Prefer the browser installer: it needs only Chrome or Edge. From PowerShell, with Python 3.10 or newer from python.org (the `py` launcher) and Git:

```powershell
git clone --depth 1 https://github.com/aivsomkar/openmausbot-gadget-sdk.git "$env:USERPROFILE\openmausbot-gadget-sdk"
cd "$env:USERPROFILE\openmausbot-gadget-sdk"
py -m venv "$env:USERPROFILE\.openmausbot-gadget\venv"
$venv = "$env:USERPROFILE\.openmausbot-gadget\venv\Scripts"
& "$venv\pip.exe" install "esptool>=5.3,<6"
$fw = "$env:TEMP\omb-fw"; New-Item -ItemType Directory -Force $fw | Out-Null
$base = "https://github.com/aivsomkar/openmausbot-gadget-sdk/releases/latest/download"
$board = "amoled-175c"                 # amoled-175c, amoled-175, lcd-154 or devkit
Invoke-WebRequest "$base/install.json" -OutFile "$fw\install.json"
$parts = (Get-Content "$fw\install.json" -Raw | ConvertFrom-Json).boards.$board.parts
foreach ($p in $parts) { Invoke-WebRequest "$base/$($p.path)" -OutFile "$fw\$($p.path)" }
Get-PnpDevice -PresentOnly | Where-Object InstanceId -Match 'VID_303A&PID_1001' | Select-Object FriendlyName
```

The board's port is the `COMn` in the `FriendlyName` that ends in `(COMn)`, or under Ports (COM & LPT) in Device Manager. With `COM5` as the example:

```powershell
& "$venv\esptool.exe" --chip esp32s3 --port COM5 write-flash @($parts | ForEach-Object { $_.offset; "$fw\$($_.path)" })
& "$venv\python.exe" tools\console\omb_console.py --port COM5 "log off" scan --wait 10
& "$venv\python.exe" tools\console\omb_console.py --port COM5 --pair 123456 --wifi "Home Net" "the password" --host auto --until-paired --wait 150
```

The exit codes are the ones in the table above. On Windows, `host auto` often finds nothing (exit 3): ask for the address shown under Pair a gadget.

### A build from source

Install ESP-IDF (next section), then build and flash the board as in [Build and flash a board](#build-and-flash-a-board). `idf.py flash` writes the separate parts too, so the pairing survives.

## Install ESP-IDF v6.0.3

With Espressif's EIM installer on macOS. The `brew install` at the start adds EIM's prerequisites: on macOS, EIM checks for them but does not install them, and it stops if one is missing. Homebrew skips any that are already installed.

```sh
brew install libgcrypt glib pixman sdl2 libslirp dfu-util ninja && brew tap espressif/eim && brew install eim && eim install -i v6.0.3 -t esp32s3
. ~/.espressif/tools/activate_idf_v6.0.3.sh
```

Or with `install.sh` on macOS or Linux:

```sh
git clone -b v6.0.3 --depth 1 --recursive --shallow-submodules https://github.com/espressif/esp-idf.git ~/esp/esp-idf-v6.0.3
cd ~/esp/esp-idf-v6.0.3 && ./install.sh esp32s3 && . ./export.sh
```

ESP-IDF needs ninja. The EIM line above installs it. With `install.sh`, run `brew install ninja`, or `python3 tools/idf_tools.py install ninja` inside the ESP-IDF folder. `idf.py --version` must print `ESP-IDF v6.0.3`. Every new shell needs the activate (or `export.sh`) line again; `eim run "idf.py build" v6.0.3` runs one command without activating.

On Windows, download the EIM command-line installer from Espressif's page https://dl.espressif.com/dl/eim/ and run it from PowerShell (not by double-clicking): `.\eim install -i v6.0.3 -t esp32s3 -a true` (`-a true` installs missing prerequisites such as Git and Python). Then open the `IDF_PowerShell` desktop icon EIM creates, or run single commands with `.\eim run "idf.py --version" v6.0.3`. The board's port is `COMn` (see [On Windows](#on-windows)), so `-p COM5` replaces `-p /dev/cu.usbmodem1101` below.

## Build and flash a board

| Board id | Board |
|---|---|
| `amoled-175c` | Waveshare ESP32-S3-Touch-AMOLED-1.75C |
| `amoled-175` | Waveshare ESP32-S3-Touch-AMOLED-1.75 |
| `lcd-154` | Waveshare ESP32-S3-LCD-1.54 |
| `devkit` | ESP32-S3-DevKitC-1-N16R8 with a 2" ST7789, INMP441 and MAX98357A |

From `firmware/ports/esp32/`, replacing `<board>` with a board id:

```sh
idf.py -B build/<board> -D GADGET_BOARD=<board> -D SDKCONFIG=build/<board>/sdkconfig build
tools/check-size.sh <board>
idf.py -B build/<board> -D GADGET_BOARD=<board> -D SDKCONFIG=build/<board>/sdkconfig -p /dev/cu.usbmodem1101 flash monitor
```

- Each board keeps its own `sdkconfig` in its build folder. Never drop `-D SDKCONFIG=…`: ESP-IDF would otherwise share one `sdkconfig` and every later board would inherit the first board's settings.
- Add `-D PROJECT_VER=1.2.0-dev` to stamp a version; without it the build reports `0.0.0-dev`. Keep the `-dev` suffix on anything you build yourself.
- `tools/check-size.sh` fails when the app is larger than the 6 MiB OTA slot or rollback is not enabled.
- If flashing can't connect, put the board in download mode: hold BOOT, press and release RST, release BOOT.
- `idf.py erase-flash` wipes the identity key, Wi-Fi and pairing, like the installer's Erase everything.
- **devkit:** Plug the board in by the USB-C port labelled USB, not the one labelled UART. The UART port goes through a bridge chip and shows neither the console nor the 0x303A/0x1001 USB device.

## Read the serial log

`idf.py -B build/<board> -D GADGET_BOARD=<board> -D SDKCONFIG=build/<board>/sdkconfig -p /dev/cu.usbmodem1101 monitor` shows the log (Ctrl-] quits). It needs a terminal; from a script, use `tools/console/omb_console.py` instead, which prints only the machine-readable `@omb` lines as JSON.

Console commands (USB serial on a board, stdin in the simulator). Arguments with spaces go in double quotes, with `\\` and `\"` as escapes:

| Command | Does |
|---|---|
| `wifi "<ssid>" "<password>"` | Saves the network and connects; an open network uses `""` |
| `scan` | Prints nearby networks as one `@omb {"op":"scan",…}` line |
| `host auto` / `host <address>[:port]` | Finds MausBot over mDNS, or pins an address (port 8810 by default) |
| `pair <code>` | Stores the six-digit code and reconnects at once |
| `name "<text>"` | The name shown in Remote access (up to 32 characters) |
| `say "<text>"` | Sends a typed message to the bot |
| `status` | Prints `@omb {"op":"status",…}`: Wi-Fi, host, id, pairing state, firmware, battery |
| `log off` / `log on` | Silences or restores the ESP log until reboot; `@omb` lines always print |
| `forget` | Erases pairing, key, Wi-Fi and name, then restarts |
| `reboot` | Restarts |

In `status`, `pair` is `unpaired`, `code_stored`, `connecting`, `paired` or `error` (then `error` holds the handshake error, such as `bad_code`). [protocol/PROTOCOL.md](protocol/PROTOCOL.md) has the protocol.

## Run the simulator and the fake host

Build the desktop targets (needs CMake ≥ 3.24 and SDL2 for the window; `brew install sdl2` on macOS, `sudo apt-get install -y libsdl2-dev` on Ubuntu):

```sh
cmake -S firmware -B build/host -DCMAKE_BUILD_TYPE=Debug && cmake --build build/host -j10
ctest --test-dir build/host --output-on-failure -L "unit|vectors|e2e|snapshot"
```

Start the fake host in the background (it prints its pairing code, here pinned to 123456), then the simulator window. Space is TALK, Esc is CANCEL, the mouse is touch:

```sh
npm ci
node tools/fake-host/src/main.ts --port 8810 --code 123456 &
./build/host/ports/sim/gadget-sim --board amoled-175c --host 127.0.0.1:8810 --pair 123456
```

To pair the simulator with your real MausBot instead, turn on Remote access, open Pair a gadget and run `./build/host/ports/sim/gadget-sim --board amoled-175c --pair <code>` (on Linux add `--host <address shown under Pair a gadget>`).

Headless runs (CI, screenshots) need `--headless --script <file>`; `firmware/tests/scripts/` has examples. Each pairing needs a fresh code: the fake host's code works once and lasts 120 s, so after the window simulator above has paired, restart the fake host (as below) or, if you started it with a pipe on its stdin, write `{"cmd":"code","code":"123456"}` to it before the next one. For instance:

```sh
pkill -f 'tools/fake-host/src/main.ts'; node tools/fake-host/src/main.ts --port 8810 --code 123456 & sleep 1
printf 'expect ready 10000\n' > /tmp/ready.txt
./build/host/ports/sim/gadget-sim --board lcd-154 --headless --host 127.0.0.1:8810 --pair 123456 --name ready-check --script /tmp/ready.txt
```

Each `--name` keeps its own key and settings under `~/.openmausbot-gadget/sim/<name>/`, so several simulated gadgets can pair at once.

Other test suites: `npm ci && npm test` (protocol vectors and fake host), `cd site && npm ci && npm test && npm run build` (installer), `node --test "tools/release/test/*.test.ts"` (release tools and docs), `python3 -B -m unittest discover -s tools/console -p 'test_*.py'` (console helper).

## Add a board

1. Add its descriptor to `firmware/core/src/boards.c`: id (`/^[a-z0-9-]{1,32}$/`), display name, screen size and shape, image size, mic rate 16000, speaker rate, input mask, battery, `ota_max` and art profile.
2. **OTA slot size.** Every official board uses `partitions/16mb.csv`, so `ota_max` is 6291456 and the board needs 16 MB of flash; the installer and release CI refuse anything smaller. A smaller board can only run as a custom build (its own partition table and `ota_max`, flashed with `idf.py`), never as an official release.
3. **Speaker-rate rule.** If the mic and speaker codecs share one I2S clock, `speaker.rate` must equal `mic.rate` (16000). Only a board with separate I2S controllers, like the devkit, may use 24000.
4. **Art profile.** `GADGET_ART_S240` for screens about 466 px across, `GADGET_ART_S150` for 240–320 px screens. The art budgets (`npm run budget` in `tools/art`) must still pass.
5. Create `firmware/ports/esp32/boards/<board>/`: `board.h` (pins, from the vendor's schematic), `board.c` (the functions in `main/board_api.h`: `board_early_init`, `board_display_init`, `board_touch_init`, `board_audio_init`, `board_buttons_init`, `board_talk_pressed`, `board_cancel_pressed`, `board_battery_read`, `board_set_brightness`) and `sdkconfig.defaults` with `CONFIG_GADGET_BOARD_ID`, `CONFIG_GADGET_ART_PROFILE`, `CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y`, `CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions/16mb.csv"` and `# CONFIG_GADGET_TEST_KEYS is not set`.
6. Add the id to the `esp32` matrix in `.github/workflows/ci.yml`, the `build` matrix and the board loop in `.github/workflows/release.yml`, `BOARDS` in `tools/release/lib.ts`, `BOARD_CHOICES` in `site/src/boards.ts` and the board table in `README.md`.
7. Add snapshot goldens for its screen size (`firmware/tests/snapshots/<board>/`) and a section in `docs/hardware-checklist.md`.

## Add an action

Bots can run actions a gadget declares. Register one after `core_init()` and before the first `core_tick()`: on a board, call your register function in `firmware/ports/esp32/main/main.c` right after `core_init()`; in the simulator, in `firmware/ports/sim/main.c` after `core_init()`.

```c
#include <stdio.h>

#include "gadget_actions.h"

void lamp_set(bool on); /* your board code */

static bool lamp_set_action(const cJSON *args, cJSON *data, char *error, size_t error_cap) {
  const cJSON *on = cJSON_GetObjectItemCaseSensitive(args, "on");
  if (!cJSON_IsBool(on)) {
    snprintf(error, error_cap, "on must be true or false");
    return false;
  }
  lamp_set(cJSON_IsTrue(on));
  cJSON_AddBoolToObject(data, "on", cJSON_IsTrue(on));
  return true;
}

void register_lamp_action(void) {
  gadget_status_t st = gadget_action_register(
      "lamp.set", "Turn the desk lamp on or off.",
      "{\"type\":\"object\",\"properties\":{\"on\":{\"type\":\"boolean\"}},\"required\":[\"on\"]}",
      GADGET_RISK_SAFE, lamp_set_action);
  if (st != GADGET_OK) printf("lamp.set not registered: %d\n", (int)st);
}
```

`gadget_action_register(name, description, schema, risk, handler)` checks the `hello` limits: name `/^[a-z][a-z0-9_.-]{0,31}$/`, a description of 1–200 characters, a params schema of at most 1 KiB, at most 16 actions including the built-in `chime`, so 15 of your own, and a whole `hello` of at most 16 KiB. It returns `GADGET_ERR_LIMIT`, `GADGET_ERR_ARG` or `GADGET_ERR_STATE` (a duplicate name) otherwise. The handler runs on the main thread, must return within 100 ms, and either fills `data` and returns true or writes a short Latin-1 reason into `error` and returns false.

- MausBot refuses to run an action whose params schema uses `pattern`, `patternProperties` or `"format": "regex"`; use `enum`, `minimum`/`maximum` and `maxLength` instead. The check looks inside nested schemas too (`items`, each property), so no regular expression from a gadget ever runs on your computer; a param that is only *named* `pattern` is fine.
- `GADGET_RISK_SAFE` actions run when a bot asks.
- `GADGET_RISK_CONFIRM` actions always ask the person on the computer first, even in Full access, unless they chose Always allow for that action on that gadget.
- **Do not register actions whose misuse is unsafe**: unlocking a door, switching mains power, anything that could hurt someone or damage something. In v1 the gadget does not authenticate MausBot (the connection is plain `ws://` on your network), so a device that impersonates MausBot on the LAN can send `act`, and `confirm` is enforced only on the Mac.

## Releases (maintainers)

Pushing a `v*` tag runs `.github/workflows/release.yml`: it builds every board, signs the app images in the protected `release` environment and publishes the release with `manifest.json`, `install.json` and `SHA256SUMS`. Tags with a `-` (for example `v1.2.0-rc.1`) become prereleases and never "latest". The workflow then rebuilds the installer site. [docs/release-keys.md](docs/release-keys.md) covers the one-time key setup.

## Troubleshooting

- **No port shows up.** Only ESP32-S3 boards on their native USB port (0x303A/0x1001) appear; a plain ESP32 or a USB-to-UART port never does. Use a USB-C cable that carries data. Hold BOOT while plugging the board in to force download mode. On the devkit, use the port labelled USB.
- **The gadget stays on "Looking for MausBot".** Check that Remote access is on. If MausBot runs on Windows on a Public network, set the network to Private. If mDNS doesn't work, pin the address shown under Pair a gadget: `host 192.168.1.20:8810`.
- **"That code didn't work."** Codes last 120 seconds and allow 5 tries. Open Pair a gadget again for a new one.
- **The installer says "Press RST or unplug and replug the board".** The chip stayed in download mode (BOOT was held during the reset). Press RST once.
````

- [ ] **Step 5: Write `docs/installer-checklist.md`**

`docs/installer-checklist.md`:

````markdown
# Installer and release checklist (real hardware)

Unit tests cover the installer's logic with fakes. These checks need real boards, real browsers and a real GitHub release. Record the date, board, browser and result for each.

## Installer: every board (amoled-175c, amoled-175, lcd-154, devkit)

Serve a local build: `cd site && npm run build && npm run serve`, then open http://127.0.0.1:8080/ (localhost counts as a secure context for Web Serial). esbuild's server stops when its stdin closes, so from a non-interactive shell or an agent run `tail -f /dev/null | npm run serve &` instead, and stop it with `pkill -f 'esbuild --servedir=dist'`. To flash a local build, stage it first (see "Local firmware" below).

1. [ ] Chrome on macOS: pick the board, **Connect and install**. The port chooser lists only the 0x303A/0x1001 device. Progress reaches 100%.
2. [ ] After flashing, the board restarts into the app on its own (no RST press) and the page moves to Wi-Fi within about 5 s.
3. [ ] Hold BOOT through the reset: the page says "Press RST or unplug and replug the board" and keeps saying it (it is not replaced by "Waiting for the gadget to start…"); pressing RST continues the flow.
4. [ ] The Wi-Fi list matches the networks nearby. Pick one, type the password, Next.
5. [ ] With Remote access on, type the code from MausBot → Settings → Remote access → Pair a gadget: the page shows "Paired with <computer name>" and the gadget's row appears under Gadgets.
6. [ ] A wrong code shows "That code didn't work. Get a new one from Pair a gadget."
6b. [ ] Straight after 6, a new correct code pairs; the page does not repeat "That code didn't work" while the gadget pairs.
7. [ ] A wrong Wi-Fi password returns to the Wi-Fi step with "The gadget can't join <network>." shown there; the right password then pairs.
8. [ ] Remote access off: pairing times out with the Remote access message.
8b. [ ] With MausBot at its device limit, the page says "MausBot has too many devices…"; removing a device in Remote access within two minutes lets the gadget pair without a new code.
9. [ ] Reinstall without Erase everything, without reloading the page: the page says "Still paired with <computer name>" and the gadget reconnects without a new code.
10. [ ] Reinstall with Erase everything, again without reloading the page: the gadget needs a new code and appears as a new device; the old row can be removed.
11. [ ] **Already installed? Set up Wi-Fi and pairing** on a flashed board skips flashing and reaches the Wi-Fi step, also right after an install in the same page.
12. [ ] A network name and password containing spaces, quotes and a backslash pair correctly.
13. [ ] An open network (empty password) works.
14. [ ] Unplug the board mid-flash: the page reports the failure; plug it back and **Connect and install** again succeeds without reloading the page.
14b. [ ] Flash a board that just ran the app, without holding BOOT: the write never resets midway (the chip's watchdogs are off while writing).
15. [ ] With `idf.py monitor` holding the port: the page says another program is using it.
15b. [ ] Unplug the board as soon as the write finishes and leave it unplugged: the page says the board did not come back and to press RST or replug, not that another program is using it.
16. [ ] devkit only: the hint about the USB port shows; the port labelled UART does not appear in the chooser.

## Installer: other browsers and hosts

17. [ ] Edge on Windows 11 flashes and pairs with MausBot on the same Windows computer. If `host auto` finds nothing, the page asks for the address and pairing completes with the address from Pair a gadget.
17b. [ ] After typing the address in 17, a wrong code and then a new code pair without asking for the address again.
18. [ ] Safari: the page says the browser can't reach USB serial devices, and the buttons are disabled.
19. [ ] An 8 MB ESP32-S3 board is refused with the flash-size message before anything is written.
19b. [ ] AGENTS.md's terminal flow flashes a board and `omb_console.py --until-paired` pairs it (exit 0): in zsh on macOS, and in PowerShell on Windows.

## Release workflow

20. [ ] Before the first public release: Omkar has confirmed that the mascot expression geometry (src/components/cursor-face-data.ts at the pinned commit) is project-owned (spec §11).
21. [ ] One-time setup in docs/release-keys.md is done, and `node tools/release/check-keys.ts` passes on main.
22. [ ] Push `v<x.y.z>-rc.1`: build, assemble and release jobs pass after approval; the release is marked prerelease and is not "latest"; its assets are the 5 per board plus `manifest.json`, `install.json` and `SHA256SUMS`.
23. [ ] `curl -fsSL https://github.com/aivsomkar/openmausbot-gadget-sdk/releases/download/<tag>/manifest.json` shows four boards with `key_id` `r1`.
24. [ ] Push `v<x.y.z>`: the release is "latest"; `pages.yml` runs and the installer shows "Firmware <x.y.z>" with all four boards selectable.
25. [ ] Flash a board from the live installer, then use MausBot's Update button with a newer release (P4b).

## Local firmware

Stage a board you built yourself into the local installer:

```sh
cd site && npm run build && cd ..
node tools/release/collect.ts --board lcd-154 --version 0.0.0-dev --build firmware/ports/esp32/build/lcd-154 \
  --assets site/dist/firmware --meta build/installer-meta --local
node tools/release/install-json.ts --meta build/installer-meta --version 0.0.0-dev --out site/dist/firmware/install.json
```

`collect.ts` needs `merged.bin` in the build folder: run `idf.py -B build/lcd-154 -D GADGET_BOARD=lcd-154 -D SDKCONFIG=build/lcd-154/sdkconfig merge-bin -o merged.bin` in `firmware/ports/esp32` first. It also checks the build's partition table against `firmware/ports/esp32/partitions/16mb.csv`, so run it from the repository root.
````

- [ ] **Step 6: Run the tests**

Run: `node --test tools/release/test/docs.test.ts && node --test "tools/release/test/*.test.ts"`
Expected: `# pass 4`, then for the whole folder `# pass 40` and `# fail 0`.

- [ ] **Step 7: Commit**

```bash
git add README.md AGENTS.md docs/installer-checklist.md tools/release/test/docs.test.ts
git commit -m "docs: README, AGENTS.md and the installer checklist" \
  -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

### Task 15: README screenshots

Omkar asked for screenshots in the README (2026-10-04). Every image is a real render: gadget screens come from the simulator's headless snapshots (P2b's committed goldens), and the installer image is a headless-Chrome capture of the built site. Nothing is drawn or mocked by hand.

**Files:**
- Create: `tools/screenshots/package.json` (private; devDependency `pngjs` 7.0.0, the same version P2b's `tools/art` uses)
- Create: `tools/screenshots/make.ts` (Node ≥ 22.18 type stripping, like the other tools)
- Create: `tools/screenshots/test/make.test.ts`
- Create: `docs/images/` (generated PNGs, committed)
- Modify: `README.md` (add the "On the screen" section after the intro, plus an installer image in the install section)
- Modify: `tools/release/test/docs.test.ts` (README image assertions)

**Interfaces:**
- Consumes: P2b's goldens `firmware/tests/snapshots/{amoled-175c,lcd-154,devkit}/*.png` (466×466 round, 240×240, 320×240 RGB565 renders; read the directory to learn the exact file names per screen) and the built installer page from Task 10 (`site/dist/index.html`).
- Produces: `docs/images/screen-<name>.png` for the eight README screens, `docs/images/boards.png`, `docs/images/installer.png`, and `npm --prefix tools/screenshots run make`, which regenerates all of them.

**What `make.ts` does:**
1. For each README screen (idle, listening, thinking, speaking, ask, post, setup, update), take the `amoled-175c` golden and write `docs/images/screen-<name>.png` at 466×466. Outside the round panel, pixels become transparent: alpha 0 when the pixel's centre is more than 233 px from (233, 233), with a 1 px anti-aliased edge. The round screen then reads as a device face on both light and dark GitHub themes. Map each name to its golden file explicitly in a table at the top of the file; if a mapped golden is missing, fail with a message naming it. Never fall back silently.
2. Write `docs/images/boards.png`: the idle screen at all three sizes side by side, bottom-aligned on a transparent canvas with 24 px gaps (round 466, square 240, rect 320×240), so the README shows the same firmware on every board.
3. If `--installer` is passed and `site/dist/index.html` exists, capture `docs/images/installer.png` at 1280×800 with headless Chrome:
   `"/Applications/Google Chrome.app/Contents/MacOS/Google Chrome" --headless=new --disable-gpu --hide-scrollbars --window-size=1280,800 --screenshot=<abs path> file://<abs path to site/dist/index.html>`
   On Linux CI, use `google-chrome` from PATH. Missing Chrome is an error only when `--installer` was asked for.
4. Print each file written with its size.

- [ ] **Step 1: Write the failing test** `tools/screenshots/test/make.test.ts` (node:test). Run make.ts into a temp dir against two small synthetic fixtures (a solid-colour 466×466 PNG and a 240×240 PNG) and assert:
  - pixel (233, 233) keeps alpha 255, pixels (0, 0) and (465, 465) have alpha 0, and a pixel 232 px from the centre is opaque;
  - boards.png is 466 + 24 + 240 + 24 + 320 = 1074 px wide and 466 px tall;
  - a missing mapped golden makes the run exit non-zero with the golden's name in stderr.
- [ ] **Step 2: Run it and watch it fail:** `node --test tools/screenshots/test/make.test.ts` → fails, because make.ts doesn't exist yet.
- [ ] **Step 3: Implement `make.ts`** (pngjs decode/encode, the mask, the composite, the optional Chrome capture). Fixture and output paths come from flags (`--snapshots <dir> --out <dir>`), defaulting to the repo paths, so the test can point at temp dirs.
- [ ] **Step 4: Run the test and watch it pass.**
- [ ] **Step 5: Generate the real images:** build the site (Task 10's command), then `npm --prefix tools/screenshots install && npm --prefix tools/screenshots run make -- --installer`. Open every PNG in `docs/images/` and check it shows the right screen (Read the image files). Re-run make and confirm the bytes don't change (`git status --short docs/images` is clean after a second run).
- [ ] **Step 6: README.** After the intro paragraph add:

```markdown
## On the screen

Real renders from the simulator, which runs the same firmware code as the boards.

| Idle | Listening | Thinking | Speaking |
|:---:|:---:|:---:|:---:|
| <img src="docs/images/screen-idle.png" width="200" alt="Idle: the green Maus says hi"> | <img src="docs/images/screen-listening.png" width="200" alt="Listening: a ring follows your voice"> | <img src="docs/images/screen-thinking.png" width="200" alt="Thinking: what it heard and what the bot is doing"> | <img src="docs/images/screen-speaking.png" width="200" alt="Speaking: the reply, read aloud"> |
| **Approval** | **Push** | **Pairing** | **Update** |
| <img src="docs/images/screen-ask.png" width="200" alt="An approval: Allow or Deny"> | <img src="docs/images/screen-post.png" width="200" alt="A routine result pushed to the gadget"> | <img src="docs/images/screen-setup.png" width="200" alt="Pairing: MausBot → Settings → Remote access → Pair a gadget"> | <img src="docs/images/screen-update.png" width="200" alt="Installing a firmware update"> |

<img src="docs/images/boards.png" width="640" alt="The same firmware on the round 1.75-inch AMOLED, the 1.54-inch square LCD and a 320×240 breadboard screen">
```

Rewrite the alt texts to describe what each render actually shows; read the images to check. In the install section, add `<img src="docs/images/installer.png" width="640" alt="The browser installer">` under the installer link.
- [ ] **Step 7: Extend `docs.test.ts`:** the README references every `docs/images/*.png`; every referenced image exists and is a valid PNG (check the 8-byte signature); every `<img>` has a non-empty `alt`. Run `node --test tools/release/test/` → passes.
- [ ] **Step 8: Commit** `tools/screenshots/`, `docs/images/`, `README.md` and `tools/release/test/docs.test.ts` with the `-- <paths>` form, message `docs(readme): screenshots of every gadget screen and the installer` plus the trailer. Do not push.

Desktop screenshots (the "Pair a gadget" dialog and a gadget row in MausBot's Remote access settings) are added after the OpenMausBot hub (P3a) is built. Leave them out of this task.

### Task 16: Branch verification

**Files:** none created. This task runs everything and records results.

- [ ] **Step 1: The branch is complete and clean**

Run: `git status --short && git log --oneline p2c-esp32..HEAD`
Expected: no uncommitted changes; 15 commits, Tasks 1–15 in order.

- [ ] **Step 2: Installer from a clean install**

Run: `cd site && rm -rf node_modules dist && npm ci && npm test && npm run build && node scripts/check-firmware.ts dist/firmware; cd ..`
Expected: `# pass 73`, `# fail 0` (and `# skipped 0`: the firmware round-trip test compiled P2a's `console.c`), the build line naming the four bundled packages, and `check-firmware: no firmware published yet (placeholder install.json)`.

- [ ] **Step 3: Release tools, licensing, docs and the console helper**

Run: `node --test "tools/release/test/*.test.ts" && site/node_modules/.bin/tsc -p tools/release/tsconfig.json && python3 -B -m unittest discover -s tools/console -p 'test_*.py'`
Expected: `# pass 40`, `# fail 0`; no type errors; `Ran 26 tests` / `OK` (no skips).

- [ ] **Step 4: Workflows parse and their shell steps behave**

Run: `for f in ci release pages; do npx --yes js-yaml@4.1.0 .github/workflows/$f.yml > /dev/null && echo "$f.yml parses"; done && bash /private/tmp/p2d-wfsim.sh "$PWD"`
Expected: the three "parses" lines and the ten lines listed in Task 12 Step 5.

- [ ] **Step 5: Earlier plans' suites still pass on this branch**

Run:

```bash
npm ci && npm run vectors:check && npm test
cmake -S firmware -B build/host -DCMAKE_BUILD_TYPE=Debug && cmake --build build/host -j10 && \
  ctest --test-dir build/host --output-on-failure -L "unit|vectors|e2e|snapshot"
```

Expected: every test passes. This plan changes no file those suites read, so a failure here is a regression from an earlier branch: report it to its owner.

- [ ] **Step 6: AGENTS.md's no-hardware commands work as written**

Run (fake host in the background, then the headless check from AGENTS.md):

```bash
node tools/fake-host/src/main.ts --port 8810 --code 123456 > /tmp/p2d-fakehost.log 2>&1 &
FH=$!; sleep 1
printf 'expect ready 10000\n' > /tmp/ready.txt
./build/host/ports/sim/gadget-sim --board lcd-154 --headless --host 127.0.0.1:8810 --pair 123456 --name ready-check --script /tmp/ready.txt; echo "sim exit=$?"
kill $FH; rm -rf ~/.openmausbot-gadget/sim/ready-check
python3 tools/console/omb_console.py --help | head -1
```

Expected: `sim exit=0` (the simulator paired with the fake host and saw `ready`), then the helper's usage line. If `gadget-sim` is not at `build/host/ports/sim/gadget-sim`, fix the path in AGENTS.md and README.md to where P2a's build puts it and rerun Task 14's tests.

- [ ] **Step 7: Collect a real ESP-IDF build**

This checks `collect.ts` and `install.json` against a real `flasher_args.json` and app image (the unit tests use a fixture written from ESP-IDF's template). ESP-IDF v6.0.3 is installed by P2c's plan; if it is missing, install it with the EIM steps in AGENTS.md.

```bash
. ~/.espressif/tools/activate_idf_v6.0.3.sh
cd firmware/ports/esp32
idf.py -B build/lcd-154 -D GADGET_BOARD=lcd-154 -D SDKCONFIG=build/lcd-154/sdkconfig build
idf.py -B build/lcd-154 -D GADGET_BOARD=lcd-154 -D SDKCONFIG=build/lcd-154/sdkconfig merge-bin -o merged.bin
cd ../../..
rm -rf build/p2d-check && node tools/release/collect.ts --board lcd-154 --version 0.0.0-dev \
  --build firmware/ports/esp32/build/lcd-154 --assets build/p2d-check/firmware --meta build/p2d-check/meta --local
node tools/release/install-json.ts --meta build/p2d-check/meta --version 0.0.0-dev --out build/p2d-check/firmware/install.json
node site/scripts/check-firmware.ts build/p2d-check/firmware
node -e '
const { readFileSync } = require("node:fs");
const img = readFileSync("firmware/ports/esp32/build/lcd-154/openmausbot-gadget.bin");
const t1 = Buffer.from(readFileSync("keys/test-t1.pub.b64", "utf8").trim(), "base64");
console.log(img.indexOf(t1) === -1 ? "t1 absent from the board image" : "T1 PRESENT: CONFIG_GADGET_TEST_KEYS is on");'
grep -xF '# CONFIG_GADGET_TEST_KEYS is not set' firmware/ports/esp32/build/lcd-154/sdkconfig
```

Expected: `collect: lcd-154 0.0.0-dev: <size> bytes, parts <offsets>`, where `<offsets>` are the keys of `flash_files` in `firmware/ports/esp32/build/lcd-154/flasher_args.json`, sorted by address (check with `node -e 'console.log(Object.keys(require("./firmware/ports/esp32/build/lcd-154/flasher_args.json").flash_files).sort((a, b) => parseInt(a, 16) - parseInt(b, 16)).join(" "))'`); `install-json: build/p2d-check/firmware/install.json (lcd-154)`; `check-firmware: firmware 0.0.0-dev for lcd-154`; `t1 absent from the board image`; and the grep prints the line. That `collect` passed also means the real `partition-table.bin` equals `16mb.csv` and no part reaches into `nvs` or `phy_init`. If `collect` reports a different set of images in `flash_files`, ESP-IDF's layout differs from the fixture: update `installEntryFromFlasherArgs` and `FLASHER_ARGS` together, never hard-code offsets. If it reports a partition-table difference, compare `python $IDF_PATH/components/partition_table/gen_esp32part.py firmware/ports/esp32/build/lcd-154/partition_table/partition-table.bin` with `16mb.csv` before touching `partitions.ts`.

- [ ] **Step 8: The release key check still fails by design**

Run: `node tools/release/check-keys.ts; echo "exit=$?"`
Expected: the "release key table is empty" line and `exit=1`, until Omkar follows `docs/release-keys.md`.

- [ ] **Step 9: Record what needs hardware or GitHub**

Add nothing to the repository. Report this list with the branch hand-off (it is also `docs/installer-checklist.md`):

- **Real flashing** with esptool-js on each of the four boards, from Chrome (macOS) and Edge (Windows); separate parts at the real offsets; "Erase everything"; the watchdog registers accepted by the stub, and no reset midway through a write (checklist item 14b).
- **`resetToApp()` on USB-Serial-JTAG**: whether the RTS pulse starts the app without pressing RST, the "Press RST or unplug and replug the board" prompt staying up when BOOT is held (item 3), and Chrome re-granting the re-enumerated 0x303A/0x1001 port.
- **Chrome's `open()` errors**: that `getPorts()` keeps listing a port another program holds (`port_busy`, item 15) and drops a board that is gone (`port_lost`, item 15b), and that reinstalling without reloading the page opens the port again (items 9–11, 14).
- **The firmware's real console** (P2a/P2c): `@omb boot`, `status`, `scan`, `hosts` and `error` lines as the installer and `omb_console.py` parse them, `log off` over USB-Serial-JTAG, and a reinstall keeping the pairing (spec §10 hardware row). Also how `status` moves after a new `pair` or `wifi` (items 6b, 7) and how it reports `device_limit` (item 8b); the contract does not pin these yet (Contract notes).
- **`detectFlashSize()`** on the 16 MB boards, 32 MB 1.75C units and an 8 MB board.
- **Windows mDNS** and the "type the address" path, including the pinned address surviving a new code (item 17b); the Windows Public-network hint.
- **GitHub:** the `release` environment approval, the container build of all four boards (including `collect.ts`'s partition check against the real `partition-table.bin`), `gh release create` with the prerelease rule, `pages.yml` dispatch and deploy, and the live page loading firmware same-origin (checklist items 21–25). These need Omkar's one-time settings, the r1 key, and the pre-publish geometry check (item 20).
- **The AGENTS.md terminal flows** against a real release: the zsh/bash block (`write-flash < parts.txt` with offsets from `install.json`), the PowerShell block on Windows, and `omb_console.py` right after `write-flash` (it waits for the app; it never toggles the reset lines when it opens the port) (item 19b).

## Self-review

1. **Spec coverage:** every §5.8, §5.10, §8 and §11 requirement in scope maps to a task in the scope map above; §8's checking/updating/custom-build rules and the desktop panel copy are named as P4b's and P3a's.
2. **Placeholders:** none. Every code step holds the complete file; every command has its expected output.
3. **Type consistency:** names match across tasks and the contract: `parseOmbLine`, `createLineSplitter`, `quoteArg`, `setupCommands`, `InstallIndex`, `parseInstallIndex`, `flashPlan`, `resetToApp` (contract §4.8); `BoardMeta`/`readMetas` (Tasks 2–3); `PartitionRow`/`partitionTableBin`/`writePartitionsCsv` (Task 1 helpers, used by Tasks 2–3) and `Partition`/`partitionProblems` (Task 2); `LoaderLike` (with `readReg`), `disableWatchdogs`, `SerialPortLike`, `ConsoleIO`, `ConsoleSession`, `Clock`, `WaitPrompt`/`waitForApp`, `PairingResult` (with `device_limit`), `PairingNotice`, `supportsWebSerial`/`releaseLine` (Tasks 7–10). The task-by-task replay confirmed `npm test` passes at every boundary.
4. **Review Focus:** each of the five lines has its test in Tasks 2 and 5–11.

## Contract notes

One pinned item changes. **Contract §1.4, ESP-IDF install (macOS) row:** AGENTS.md's EIM line puts `brew install libgcrypt glib pixman sdl2 libslirp dfu-util ninja && ` in front of the pinned `brew tap espressif/eim && brew install eim && eim install -i v6.0.3 -t esp32s3`, the same change as P2c's addition 7. On macOS, EIM checks these prerequisites but does not install them, and stops when one is missing, so the pinned line alone fails on a clean Mac. `docs.test.ts` expects the full line. Contract §1.4's row gets the same prefix, so the contract, P2c and AGENTS.md give one command.

Additions, all private to P2d (contract §0 item 3):

- Extra exports in `site/src/console.ts` and `site/src/install.ts` beside the pinned ones; extra site modules (`errors.ts`, `flash.ts`, `serial-console.ts`, `setup.ts`, `copy.ts`, `boards.ts`, `main.ts`) and `site/scripts/`.
- `tools/release/` contents (the contract leaves them to P2d) and `tools/release/tsconfig.json`.
- New files outside the contract's layout: `tools/console/omb_console.py` and its test (the non-interactive console AGENTS.md needs for agents), and `docs/installer-checklist.md`.
- Task 15's README images: `tools/screenshots/` (a private npm package with its own `pngjs` 7.0.0 devDependency: `package.json`, its lock file, `make.ts`, `test/make.test.ts`) and `docs/images/` (the generated PNGs, committed). No other plan reads them. Contract §1.1 lists both paths in the layout, and §5.1 gives both to P2d.
- The `site` CI job keeps its pinned id and command and adds three steps after it (release tools and docs tests, release tools type check, console helper tests).
- `firmware/core/src/keys_release.c` is written by `tools/release/gen-release-keys.ts` when Omkar adds r1; the generated text replaces P2a's sentinel layout with the same meaning.
- `site/test/split-argv.c` compiles P2a's `firmware/core/src/console.c` on its own, with stubs for the two `util.c` functions it calls (`gadget_pair_code_valid`, `gadget_utf8_len`). If P2a makes `console.c` call more of `util.c`, the stubs grow; nothing in P2a changes.
- `tools/release/collect.ts` reads P2c's `firmware/ports/esp32/partitions/16mb.csv` and requires every row to give its offset (P2c's file does). `OTA_SLOT_SIZE` (6291456) stays the one slot size for every official board.
- `tools/console/omb_console.py` has exit codes 0, 1, 3, 4 and 5 (2 is argparse's), documented in its `--help` and in AGENTS.md.

**Requests to P2a (the contract does not pin these; the installer and the console helper depend on them):**

- When `pair <code>` is accepted, the next `status` shows `pair` = `code_stored` or `connecting` and no `error` field. When `wifi` is accepted, the next `status` shows `wifi` = `connecting`. Spec §4.3 leaves `pair` = `error` after `bad_code` "until a new `pair` command", but neither spec §5.6 nor contract §2.11 says that the new command clears it at once. P2d works around this: a leftover error counts only after progress on that side or after three status replies. If P2a pins this behaviour, the page reports a second wrong code about three seconds sooner.
- While the gadget retries after `device_limit` (spec §4.3: it keeps the code and retries every 10 s for 120 s), `status` reports `pair` = `error` with `error` = `device_limit`. P2d keeps polling on exactly that state and shows "MausBot has too many devices…". If P2a reports it some other way (for example `connecting`), the page falls back to its timeout message, which tells the person to check Remote access.
- Optional, for makers: P2a's Task 13 could make `gadget_action_register` return `GADGET_ERR_ARG` for a params schema that uses `pattern`, `patternProperties` or `"format": "regex"` anywhere inside it (but not for a param merely named `pattern`), with a test beside its `"{not json"` and `"[1,2]"` cases. P4a's Task 10 refuses such an action with 400 when a bot calls it, so today a maker can register an action that no bot can ever run, and finds out only from a failed tool call. AGENTS.md's "Add an action" states the rule either way, and already lists `GADGET_ERR_ARG`, so it needs no change if P2a adds the check.

## Deviations recorded during the build

Review of Tasks 5–8 (SDK commit `fix(P2d): address review of tasks 5, 6, 7, 8`, on `main`). Where these differ from the code blocks in Tasks 5–8, the repository files are authoritative. Task 10's `afterReset` code block above is updated for item 1. `npm test` grows from 33 to 43 tests: `reset.test.ts` from 4 to 9, `serial-console.test.ts` from 5 to 10, and `console.test.ts` and `install.test.ts` gain assertions inside existing tests. The site counts expected after Tasks 9, 10, 12 and 16 rise by the same 10 (62, then 69). Each new test or assertion failed against the Task 5–8 code and passes after the fix.

1. **`openConsolePort` never opens another gadget (Task 8, `reset.ts`; signature change).** When the board's own port refused, `openConsolePort` fell back to any granted, connected 0x303A/0x1001 port. Another ESP32-S3 gadget that was plugged in and granted earlier was then opened in place of the board being set up, and the page would have read its status and sent it this board's pairing code and Wi-Fi password: after flashing while the board re-enumerates, in Task 10's `afterReset` reopen after RST or a replug, and in Task 10's `setUpInstalled` when the chosen board is held by a serial monitor (which should report `port_busy`; a busy port on another board also counted toward `port_busy`). The signature is now `openConsolePort(port, serial, sleep, waitMs, others?)`. `others` are the ports of the other boards, captured while this board was still known; the fallback considers only 0x303A/0x1001 ports that are not `port` and not in `others`, that is, ports that showed up new. Without `others`, the default is every port `getPorts()` lists on entry except `port`. `resetToApp` captures the snapshot before `writeReg` and the RTS pulse and passes it on. Task 10's `afterReset` passes a snapshot taken when `current` was opened (the board may already be listed under its new port when the reopen starts, so the default would skip it); `setUpInstalled` keeps the default. The test fake `fakeSerial(...lists)` now returns the n-th list on the n-th `getPorts()` call (the last repeats), so "after the USB device re-enumerates…" models `reborn` appearing after the reset; its assertions are unchanged. Tests: "a second gadget already plugged in is never opened after the reset", "a busy board never falls back to another gadget" and "a snapshot of the other boards taken earlier lets a reopen use a port that is already listed".
2. **Releasing RTS may fail as the chip leaves reset (Task 8, `resetToApp`).** `setRTS(false)` was not guarded, although `writeReg` and `disconnect` were. esptool-js's `setRTS` also sends a second control transfer (`setDTR`), and if the S3 drops off USB as it leaves reset, the call rejects with `NetworkError`, so a successful flash ended in a raw error instead of re-acquiring the port. The release is now in a `try`/`catch`. Test: "resetToApp still reopens the console when releasing RTS fails as the chip drops off USB".
3. **A port that vanishes between `open()` and `setSignals()` is closed and retried (Task 8, `openConsolePort`).** The raw `DOMException` escaped and left the port open, so the next `openConsolePort` got `InvalidStateError` from it and reported `port_busy` for a board nothing else held. The candidate is now closed and the loop goes on. `FakePort.open()` now refuses a port that is already open, as Chrome does, and takes `signalFailures`. Test: "a port that vanishes between open() and setSignals() is closed and tried again".
4. **The page refuses a host the firmware would refuse (Task 5, `normalizeHostAddress`).** It accepted hostnames up to 253 bytes, but the firmware's `parse_host` (`firmware/core/src/console.c`) takes at most 57 bytes before the port, so `addr:port` fits 64 bytes. A longer name went out as `host <name>` and came back as `@omb error` "host needs auto or <address>[:port]". The host part is now capped at 57 bytes; the "pairing codes and host addresses" test checks 57 (accepted, also with `:65535`) and 58 (refused).
5. **NUL is refused like a line break (Task 5, `quoteArg` and `wifiProblem`).** The firmware's line buffer is a C string, so `wifi "Ho\0me" …` reached it as `wifi "Ho` and the gadget answered `@omb error` "unterminated quote". `quoteArg` now throws `RangeError` on CR, LF or NUL, and `wifiProblem` says "Network names and passwords can't contain line breaks or NUL characters." Review Focus 1's "refuse line breaks" covers NUL the same way. Task 11's `quote_arg` needs no change: its values come from command-line arguments, which cannot hold a NUL.
6. **The console survives a UI error and overlapping writes (Task 8, `serial-console.ts`).** An `onEvent` callback that threw escaped `push()`, ended the read loop and marked the console `lost` while the port was fine; the call is now in a `try`/`catch`. Two overlapping `send()` calls made the second throw `TypeError: Invalid state: WritableStream is locked` instead of an `InstallerError`; `send()` now chains each write after the previous one (`tail`), in call order, and the old body is the private `write()`. Tasks 9 and 10 send one line at a time, so this was latent. Tests: "two overlapping sends both arrive, in order" and "an onEvent callback that throws does not end the console".
7. **A non-fatal read error no longer ends the session (Task 8, `SerialConsole.readLoop`).** Every read error set `lost`. Under the Web Serial spec, `BufferOverrunError`, `BreakError`, `FramingError` and `ParityError` error only the current stream, and `port.readable` hands out a fresh one, so a burst during a busy boot log sent the page into its lost/RST path while the board was still connected. The loop now reads one stream after another: a non-fatal error moves on to the next `port.readable`, any other error ends the loop, and `lost` is set only when the loop ends without `close()`. When the port hands back the same errored stream, or none, the session ends instead of spinning. `FakePort.readError(name, fresh?)` errors the current stream and, for the four non-fatal names, replaces `readable`. Tests: "a non-fatal read error keeps the console; a lost device still ends it", "close() during a session ends it without marking it lost" and "a port that hands back the errored stream ends the session instead of spinning" (it hangs without the same-stream check).
8. **The merged image is refused as a part (Task 6, `parseInstallIndex`).** Review Focus 4 relied only on release CI keeping the merged image out of `parts`. An `install.json` listing its `full` file, or any `-full.bin`, as a part parsed, and `flashBoard` would have written it at its offset, wiping NVS and the pairing with it. The parts loop now throws "<board>: <path> is the merged image" for either, and "parseInstallIndex refuses broken or unsafe files" has both cases.
9. **The JavaScript copy of the firmware's splitter matches `is_space` (Task 5, `console.test.ts`).** `splitArgv` split on any `/\s/` character, but the firmware's `is_space` is only space and tab, so the copy split an unquoted `\v`, `\f` or U+00A0 differently from the gadget. It now splits on space and tab only, and the quoteArg test asserts that `\v`, `\f` and U+00A0 stay inside an argument. `quoteArg` always quotes, so no output changed.

Review of Tasks 9–12 (SDK commit `fix(P2d): address review of tasks 9, 10, 11, 12`, on `main`). Where these differ from the code blocks in Tasks 9–12, the repository files are authoritative; Task 14's AGENTS.md exit-code table and `device_limit` paragraph above are updated for items 10–13. `npm test` grows from 69 to 73 tests (`setup.test.ts` +1, `copy.test.ts` +2, the new `build.test.ts` +1), so the site counts expected after Tasks 9, 10, 12 and 16 are 65, then 73. The Python helper grows from 13 to 26 tests (Tasks 11, 12 and 16 now expect `Ran 26 tests`). Each new test failed against the Task 9–12 code and passes after the fix, except the two in item 14, which cover behaviour that was already right and fail when that behaviour is removed.

10. **The `device_limit` window ends as `device_limit`, not as a timeout (Task 9, `pairAndWait`; Task 11, `run`).** Spec §4.3 has the gadget drop the code 120 s after `pair`, contract §2.11 rule 2 says `status` then shows `unpaired`, and P2a's `session_tick` does exactly that. `pairAndWait`'s deadline is 150 s from before `pair` is sent, so at the deadline the status was always `unpaired`, and the page replaced "MausBot has too many devices…" with the timeout sentence ("Check that Remote access is on…"), the wrong diagnosis. The plan's test used a gadget that stays in `device_limit` forever, which breaks contract rule 2. `pairAndWait` now records `limited` when it sees `device_limit`; a later `unpaired` returns `{kind: "device_limit"}` at once, and the deadline returns `device_limit` whenever `limited` is set. The existing test keeps its assertions. New test: "device_limit that the gadget clears after its 120 s window ends as device_limit, before the timeout" (the fake gadget turns `unpaired` 120 s after `pair`; the result arrives before 150 s with one notice). `omb_console.py` did the same: it waited out `--wait` and exited 1 with no hint. It now sets `limited`, and a later `unpaired` prints "MausBot still has too many devices: remove one in MausBot → Settings → Remote access, then get a new code and rerun" on stderr and exits 1; the same line is printed when `--wait` runs out after `device_limit`. Test: `test_device_limit_window_closing_exits_1_with_a_hint`. The request to P2a about reporting `device_limit` in `status` (Contract notes) is met by contract §2.11 rule 2.
11. **`omb_console.py` refuses what the gadget would refuse (Task 11, `build_lines`, `run`).** `build_lines` checked only `--pair`: `--host` went out raw, so `--host $'auto\nforget'` sent `host auto` and then `forget`, which wipes the gadget (Review Focus 1 says line breaks are refused). Every value, raw LINEs included, now refuses CR, LF and NUL (`check_value`; `quote_arg` uses it too). `--wifi` applies the port of `wifiProblem` (`wifi_problem`: SSID 1–32 UTF-8 bytes; password empty, 8–63 bytes or 64 hex digits). `--host` is accepted only as `auto` or a `HOST_RE` match whose host part is at most 57 bytes with a port of 1–65535 (`host_ok`, the firmware's `parse_host` rules). A refused value raises `ValueError`, so `main()` exits 2 through `p.error`. With `--until-paired`, an `@omb error` for `pair`, `wifi` or `host` is now printed on stderr and exits 1 at once, like the installer's `command_error`; before, `--wifi Home short` waited the whole `--wait`. Exit 1's help text reads "a wrong code, a refused setup command, or not paired within --wait". `test_build_lines_order` now uses the password "pass word": its "p w" is 3 bytes, which the firmware refuses, so the new rule rejects it; the order it checks is unchanged. Tests: `test_build_lines_refuses_line_breaks_and_nul_in_every_value`, `test_build_lines_applies_the_consoles_wifi_rules`, `test_build_lines_accepts_only_auto_or_an_address_the_firmware_takes`, `test_main_refuses_a_bad_argument_with_exit_2`, `test_a_refused_setup_command_exits_1_at_once`.
12. **A UTF-8 character split between two reads arrives whole (Task 11, `wait_for_app`, `run`).** Each pyserial read was decoded on its own, so an accented SSID in a `scan` line split across reads became two U+FFFD characters, and an agent would send that wrong name back (Review Focus 2; the page uses a streaming `TextDecoder`). Both loops now keep one `codecs.getincrementaldecoder("utf-8")("replace")`. Test: `test_a_character_split_between_two_reads_arrives_whole` (the fake port ends one read between the two bytes of "é" in "Café").
13. **A missing pyserial or a port that closes is an exit code, not a traceback (Task 11, `main`).** Without pyserial, `main()` now exits 2 with "needs pyserial (pip install pyserial)". `serial.SerialException` and `OSError` from opening the port or during `run` (a wrong `--port`, or the board dropping off USB on RST, `reboot` or `forget`) print "the board's port closed or could not be opened: press RST or unplug and replug the board" and exit 5; before, they were a traceback with exit 1, which the table reads as a wrong code. Exit 5's help text names the closed port. Tests: the `Main` class (`test_without_pyserial_exits_2`, `test_a_port_that_cannot_be_opened_exits_5`, `test_a_board_that_drops_off_usb_mid_run_exits_5`, `test_exit_code_help_names_the_new_cases`), with a stand-in `serial` module in `sys.modules`. Also checked with real pyserial 3.5: a missing port exits 5, and closing the master side of a pseudo-terminal after the helper sent `pair` exits 5 with the line (a macOS pty refuses the RTS/DTR ioctls, so only those two were stubbed for that run).
14. **Python tests for the single-host and stale-Wi-Fi rules (Task 11, test only).** Task 11's interface says the helper applies the installer's rules, but nothing tested choosing a single listed host with `host <address>` or ignoring a stale `wifi: failed`, so either could be deleted with all 13 tests passing. New tests `test_a_single_listed_mausbot_is_used` and `test_a_retry_after_wifi_failed_ignores_the_stale_failure` mirror `setup.test.ts`; each fails when its rule is removed from `run`.
15. **`release.yml` runs P2c's standard-build checks (Task 12).** Its build step also runs `tools/check-art-profile.sh "$BOARD"` and refuses a build without `# CONFIG_GADGET_NVS_ENCRYPT is not set`, the same checks P2c's `tools/build-all.sh` applies to a standard build (spec §10, contract §2.15, §2.17). Every standard `build/<board>/sdkconfig` contains that line. These lines were in the Task 12 commit but not recorded here.
16. **Every sentence the page shows is in `COPY` (Task 9, `copy.ts`; Task 10, `main.ts`).** `main.ts` hard-coded "Pairing…", "The code is six digits.", "Type an address like 192.168.1.20:8810.", "Type the network name instead." and the hints `friendly()` adds for `port_busy`, `write_failed` and `flash_too_small`. They are now `COPY.pairing`, `codeFormat`, `hostFormat`, `typeNetworkName`, `portBusyHint`, `writeFailedHint` and `flashTooSmallHint`, with the same text. Test: "the page's status lines and error hints live in COPY".
17. **A lost port while pairing points at the way back (Task 9, `pairingFailure`; Task 10, `setBusy`).** After a `lost` result the page said "…, then try again", but Pair reran `pairAndWait` on the same dead console and got `lost` again; the only way out, "Already installed? Set up Wi-Fi and pairing", was not mentioned and stayed disabled until a board was picked, although `setUpInstalled` never uses `board`. The `lost` sentence now ends "then click "Already installed? Set up Wi-Fi and pairing"" (that button closes the console and asks for the port again), and `#installed` is enabled whenever the page is not busy. Calling `setUpInstalled()` directly was not done: `requestPort()` needs a user gesture, and a pairing that ends after minutes no longer has one. `pairAndWait` still reports `lost` for a `console_write_timeout` while the port is open; the same button recovers from that too. Test: "a lost port while pairing points at the button that reopens the console".
18. **Licenses come from each bundled package's own directory (Task 10, `scripts/build.ts`).** The license collector took the first `node_modules/` segment of each esbuild input, so a package npm could not hoist (`node_modules/a/node_modules/b/…`) was attributed to `a`, and `b`'s license was left out of `licenses.txt`. `build.ts` now exports `bundledPackages(inputs)`, which takes the last `node_modules/` segment (scopes included) and returns each package's name and directory; `package.json` and the license file are read from that directory. esbuild's `absWorkingDir` is pinned to `site/`, so the directories resolve the same whether the build runs from `site/` or the repository root. The build runs only when `build.ts` is the entry point (the pattern of `check-firmware.ts`), so the test can import it. With today's flat lock file `licenses.txt` is byte-identical. Test: `site/test/build.test.ts`, "licenses come from each bundled package's own directory, nested and scoped ones included".
