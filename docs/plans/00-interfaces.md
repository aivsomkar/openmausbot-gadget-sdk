# 00 — Interface contract for the gadget plans

- **Status:** binding for plans 01–09 (written 2026-10-04; revised the same day after contract review 1: key-table sentinels, `sim_hal.h`, simulator `--boot`, ask classification, manifest base URL, presence timing, rollback sdkconfig and the minor fixes, with decisions D19–D21; revised 2026-10-05 after the cross-plan review: P1 on `main`, app merge order and non-adjacent anchors, approved plan deviations, status and script rules, with decisions D22–D28, and `gadget_event_send` awaiting Omkar's answer, §2.10)
- **Inputs:** `docs/specs/2026-10-04-openmausbot-gadget-design.md` (v1.1, "the spec"), the plan-research amendments A1–A38, and research reports R1–R9 against OpenMausBot `origin/main` **6dd4403d8fbbbd5c17169724cb2a529f11d7543e**.
- **Purpose:** nine implementation plans are written in parallel. This file pins every name, type, signature, path, route, file format and branch they share, so their code fits together without a second pass.

**Plans** (ids used throughout this file):

| Id | Plan file | Repo | Branch | Scope |
|---|---|---|---|---|
| P1 | `01-protocol-and-fake-host.md` | SDK | `main` (landed, §1.3) | PROTOCOL.md, vectors, generator, Node vector tests, fake host, repo bootstrap |
| P2a | `02-firmware-core-and-headless-sim.md` | SDK | `p2a-core` | core, HAL, console, headless simulator and its HAL backends, OTA state machine, C tests, sim ↔ fake-host e2e |
| P2b | `03-firmware-ui-art-and-window-sim.md` | SDK | `p2b-ui` | LVGL UI, art pipeline, fonts, SDL window mode, snapshot tests |
| P2c | `04-firmware-esp32-boards-ota.md` | SDK | `p2c-esp32` | ESP-IDF port, four boards, partition table, ESP CI, hardware checklist |
| P2d | `05-installer-release-docs.md` | SDK | `p2d-installer` | browser installer, release.yml, pages.yml, AGENTS.md, README, licensing files |
| P3a | `06-mausbot-companion-hub.md` | App | `feat/gadget-hub` | hub, ws, enrollment, registry, host id, session mapping, token plumbing, desktop UI |
| P3b | `07-mausbot-voice-stt-tts.md` | App | `feat/gadget-voice` | Speech helper file mode, `/api/stt`, PCM TTS, hub voice |
| P4a | `08-mausbot-bot-tools.md` | App | `feat/gadget-tools` | gadget tools, internal routes, control routes, approval gate, images, presence, timeouts |
| P4b | `09-ota-delivery.md` | App (+ SDK `p4b-ota`) | `feat/gadget-ota` | release keys, manifest check, OTA streaming, Update UI |

## 0. How plans use this contract

1. **Precedence.** For behaviour, the spec (with the amendments folded into v1.1) wins. For names, types, signatures, paths, formats and ownership, this contract wins over the research reports. Section 6 lists the places where this contract had to decide something the spec left open or worded differently; those decisions are binding too.
2. **No silent changes.** A plan must not rename, retype or move anything pinned here. If a plan finds a pinned item unworkable, it keeps the pinned shape, adds a "Contract deviations" section at the end of the plan that names the item, the problem and the proposed change, and stops for review on that item.
3. **Additive only.** A plan may add private helpers, private files and optional fields that no other plan reads. Anything another plan reads must be in this file.
4. **Protocol text lives in `protocol/PROTOCOL.md`** (owned by P1, written from spec §4). This contract lists the C and TS type names for every op (§2.12) and pins encodings, but it does not restate the op semantics. **PROTOCOL.md keeps the spec's numbering:** its sections are §4.1 Transport; §4.2 Identity; §4.3 Handshake (including the `prove` text); §4.4 Conversation; §4.5 Approvals; §4.6 Push; §4.7 Display, actions and sensors; §4.8 Firmware updates (including the firmware text and the `fw.fail` codes); §4.9 Versioning and test vectors. So "PROTOCOL.md §4.n" and "spec §4.n" name the same section. Until PROTOCOL.md exists, the spec's §4.n is the source.
5. **Ground rules that apply to every plan.**
   - Original-work rule (spec §11): never open, fetch, quote or cite third-party gadget SDKs or voice-assistant firmware projects. Vendor and primary sources only.
   - OpenMausBot app repo: the checkout at `/Users/omkar/Desktop/openmaus/OpenGrokBot` belongs to another session. Never checkout, stash, reset, edit or fetch there. Read it only with `git -C /Users/omkar/Desktop/openmaus/OpenGrokBot show origin/main:<path>` and `git … grep -n <pattern> origin/main -- <paths>`. All app work happens in worktrees (§1.3).
   - Publishing belongs to Omkar: no plan pushes a branch, opens a PR or creates a release. Plans prepare the branch, run the tests and stop. (P1 is the one exception, by its run instructions: it landed on the SDK's `main` and was pushed, §1.3.)
   - OpenMausBot needs Node ≥ 24: every app command runs with `export PATH="$HOME/.nvm/versions/node/v24.14.1/bin:$PATH"` first. That PATH has pnpm 11.9.0, while the app's `packageManager` pins pnpm 10.33.0. Inside the worktree, `pnpm --version` must print 10.33.0. If it does not, run `corepack pnpm@10.33.0 <args>` wherever this file says `pnpm <args>` (corepack is in the same bin directory), starting with `corepack pnpm@10.33.0 install --frozen-lockfile`.
   - Electron gotcha: Claude's shell may export `ELECTRON_RUN_AS_NODE=1`; unset it before launching Electron, or the app exits at once.

## 1. Repositories, branches, toolchains, commands

### 1.1 SDK repository layout

Repository: `/Users/omkar/Desktop/openmaus/openmausbot-gadget-sdk` (`origin` = `https://github.com/aivsomkar/openmausbot-gadget-sdk.git`). Every path below is relative to it. The owner plan is in brackets; §5.1 repeats this as a table.

```
.gitattributes                     [P1]  (P2b appends its lines)
.gitignore                         [P1]
LICENSE                            [P1]  Apache-2.0 full text
NOTICE                             [P2d]
README.md                          [P1 stub → P2d full]
AGENTS.md                          [P2d]
CONTRIBUTING.md                    [P2d]
THIRD_PARTY.md                     [P2d]  every dependency of every plan (§1.4)
package.json, package-lock.json    [P1]   root npm package for protocol/ and tools/fake-host/ (§1.4)
.github/workflows/ci.yml           [P1 creates; P2a, P2b, P2c, P2d append jobs (§1.6)]
.github/workflows/release.yml      [P2d]
.github/workflows/pages.yml        [P2d]
.github/workflows/art-regen.yml    [P2b]  workflow_dispatch only: regenerate art + snapshot goldens on linux-x64 (§1.6)
docs/specs/2026-10-04-openmausbot-gadget-design.md   (exists)
docs/plans/00-interfaces.md … 09-ota-delivery.md     (this file and the nine plans)
docs/hardware-checklist.md         [P2c]  (P4b appends a "MausBot path" section)
docs/installer-checklist.md        [P2d]  on-device installer checks
docs/images/*.png                  [P2d]  README images, generated by tools/screenshots, committed
docs/release-keys.md               [P2d]  how Omkar creates and rotates release keys
keys/
  test-t1.key.hex                  [P1]   test signing private scalar, 64 lowercase hex + "\n" (§4.5)
  test-t1.pub.b64                  [P1]   its SEC1 public key, base64 + "\n"
  release-r1.pub.pem               [P2d]  committed by Omkar after he generates r1 (§4.5)
  release-r1.pub.b64               [P2d]  same key, SEC1 base64 + "\n"
  README.md                        [P2d]
protocol/
  PROTOCOL.md                      [P1]   normative, from spec §4
  lib/                             [P1]   shared TS helpers (§4.4.1), imported by gen-vectors, tests, fake host, dev tools
    types.ts  encoding.ts  identity.ts  frames.ts  verify.ts  version.ts
  tools/gen-vectors.ts             [P1]
  test/vectors.test.ts             [P1]   node:test, verifies every vector with node:crypto
  vectors/                         [P1]   *.json + SHA256SUMS (§4.4)
tools/
  fake-host/                       [P1]
    src/main.ts                    CLI entry (§4.7)
    src/server.ts  src/session.ts  src/voice.ts  src/ota.ts  src/control.ts
    test/*.test.ts                 node:test, using test/gadget-client.ts
    README.md
  art/                             [P2b]
    package.json, package-lock.json
    extract.ts  build.ts  svg.ts  lvgl.ts  fonts.ts  budget.ts
    source/README.md               provenance + trademark sentence
    source/provenance.json  source/maus-face.json  source/maus-body-cursor.json  source/states.json  source/palette.json
    out/                           (gitignored previews)
  release/                         [P2d]  release helpers used by release.yml (P2d decides contents); test/*.test.ts run by the site job
  release/dev-release.ts           [P4b]  local test-signed manifest + image server (§3.16); SDK branch p4b-ota
  release/test/dev-release.test.ts [P4b]  its test, run by P2d's site job (§1.6); SDK branch p4b-ota
  console/                         [P2d]  omb_console.py (non-interactive console helper for agents) + its test
  screenshots/                     [P2d]  private npm package that makes docs/images from the simulator goldens and the installer
firmware/
  CMakeLists.txt                   [P2a]  desktop build only (core, sim, tests); P2b adds ui/
  cmake/deps.cmake                 [P2a]  cjson, mbedtls, unity, wslay; P2b adds lvgl + SDL2
  cmake/warnings.cmake             [P2a]
  core/                            [P2a]
    CMakeLists.txt                 dual-use: idf_component_register when ESP_PLATFORM, add_library otherwise
    include/                       every header in §2 except maus_art.h, ui_lv_*.h, lv_conf.h, sim_*.h, board_api.h
      gadget_types.h  gadget_board.h  gadget_events.h  gadget_hal.h  gadget_util.h
      gadget_core.h  gadget_ui_model.h  gadget_ui.h  gadget_actions.h  gadget_console.h
      gadget_proto.h  gadget_ota.h
    src/
      core.c  session.c  interaction.c  screens.c  audio.c  ota.c  console.c  actions.c
      proto.c  util.c  ui_layout.c  boards.c  crypto_psa.c  keys_release.c  keys_test.c
  ui/                              [P2b]
    CMakeLists.txt                 dual-use like core
    ui.c  ui_screens.c  ui_maus.c  ui_toast.c  ...  (P2b decides file split)
    ui_lv_compat.h  ui_lv_requirements.h
    lv_conf.h                      simulator only (device uses CONFIG_LV_* in sdkconfig.defaults)
    art/maus_art.h  art/s240/*.c  art/s150/*.c      generated by tools/art, committed
    fonts/font_latin1_<px>.c       generated by tools/art (lv_font_conv), committed
  ports/
    sim/                           [P2a; P2b adds LVGL/SDL files]
      CMakeLists.txt  main.c  sim_args.c  sim_script.c  sim_ws.c  sim_net_script.c  sim_storage.c
      sim_audio.c  sim_audio_file.c  sim_events.c  sim_mdns.c  sim_ota.c  sim_console.c  sim_wifi.c
      sim_battery.c  sim_system.c
      sim_display.h  sim_hal.h  sim_display_null.c  ui_stub.c            [P2a]
      sim_display_lvgl.c  sim_sdl.c  sim_audio_sdl.c                     [P2b]
    esp32/                         [P2c]
      CMakeLists.txt  sdkconfig.defaults  partitions/16mb.csv  dependencies.lock.<board>
      main/  (CMakeLists.txt, idf_component.yml, Kconfig.projbuild, board_api.h, main.c, hal_*.c, display.c, …)
      boards/amoled-175c/ boards/amoled-175/ boards/lcd-154/ boards/devkit/   each: board.h board.c sdkconfig.defaults
      sdkconfig.nvs-encrypt  sdkconfig.test-keys   opt-in variants (-D GADGET_NVS_ENCRYPT=1, -D GADGET_TEST_KEYS=1)
      host-tests/                    port logic tests that run on the desktop
      tools/check-size.sh            size check + rollback sdkconfig check (§2.17)
      tools/check-art-profile.sh  tools/build-all.sh   one art profile per image; every board in one checkout (§1.6)
  tests/                           [P2a; P2b adds snapshot tests]
    CMakeLists.txt  fake_hal.c  fake_hal.h  test_*.c
    scripts/*.txt                  headless sim scripts
    e2e/run.ts  e2e/*.txt          sim ↔ fake-host scenarios
    snapshots/<board>/<name>.png   [P2b] committed goldens
site/                              [P2d]
  package.json, package-lock.json, index.html, src/*.ts, test/*.test.ts, dist/ (gitignored)
```

`.gitignore` (P1) contains at least: `node_modules/`, `/build/`, `firmware/ports/esp32/build/`, `firmware/ports/esp32/sdkconfig`, `firmware/ports/esp32/sdkconfig.old`, `firmware/ports/esp32/managed_components/`, `site/dist/`, `tools/art/out/`, `*_err.png`, `.DS_Store`.

`.gitattributes` (P1 creates):

```
protocol/vectors/** -text
keys/** -text
*.png binary
```

P2b appends `firmware/ui/art/** text eol=lf` and `firmware/ui/fonts/** text eol=lf`.

### 1.2 OpenMausBot files touched (summary)

All paths relative to the app repo. Full ownership is in §5.2.

- Companion (`companion/src/`): new `gadget/` folder (`protocol.ts`, `types.ts`, `ws.ts`, `enroll.ts`, `harness-client.ts`, `shape.ts`, `session.ts`, `hub.ts`, `audio.ts`, `stt-client.ts`, `speech.ts`, `presence.ts`, `control-routes.ts`, `releases.ts`, `release-keys.ts`, `ota.ts`, `firmware.ts`), new `host-id.ts`, edits to `devices.ts`, `control.ts`, `index.ts`, `routes.ts`.
- Harness (`server/`): new `routes/stt.ts`, `stt/apple.ts`, `stt/scribe.ts`, `tts/pcm.ts`, `routes/gadgets.ts`, `routes/internal-gadgets.ts`, `gadget-control.ts`, `gadget-image.ts`; edits to `index.ts` (token message, inline `/api/tts/speak` block, internal-route dispatch, `agentsIntegration` env), `tts/*.ts`, `request-auth.ts`, `peer-approval.ts`, `peer-approval-key.ts`, `drivers/agents-catalog.ts`, `drivers/agents-call.ts`, `drivers/codex.ts`, `drivers/claude.ts`, `bot-attachment.ts`, `agent-tool-policy.ts`, `harness-capabilities.ts`.
- Electron: `electron/main.mjs`, `electron/companion.mjs`, `electron/preload.cjs`, `electron/resources/speech-helper.swift`.
- Renderer: `src/components/CompanionSection.tsx`, `PhoneSetupFlow.tsx`, `SidebarPhoneButton.tsx`, `SettingsModal.tsx` (keywords), new `PairGadgetPanel.tsx`, `GadgetRow.tsx`, `GadgetUpdateCell.tsx`, new `src/lib/gadgets.ts`, `src/locales/en.json`.
- Tests and fixtures: `companion/test/gadget/**`, `companion/test/fixtures/gadget-vectors/**`, `.gitattributes`, server and electron tests named per plan.
- Dependencies: root `package.json` gains `"pngjs": "7.0.0"` and `"jpeg-js": "0.4.4"` in `dependencies` (P4a).

### 1.3 Branches and worktrees

**SDK** (`/Users/omkar/Desktop/openmaus/openmausbot-gadget-sdk`). P1 landed directly on `main` and is pushed to `origin/main` (its run instructions replaced the `p1-protocol` branch this table first planned). Every later plan has one branch, each created from the previous one, in this order. No later plan commits to `main`.

| Plan | Branch | Created from |
|---|---|---|
| P1 | `main` (landed and pushed to `origin/main`; code tip `3da4585` or a later `fix(P1)` commit, `3a42101` at this revision; commits that touch only `docs/` are plan edits) | — |
| P2a | `p2a-core` | `main` |
| P2b | `p2b-ui` | `p2a-core` |
| P2c | `p2c-esp32` | `p2b-ui` |
| P2d | `p2d-installer` | `p2c-esp32` |
| P4b (SDK part: `tools/release/dev-release.ts`, `tools/release/test/dev-release.test.ts` and the "MausBot path" section of `docs/hardware-checklist.md`) | `p4b-ota` | `p2d-installer` |

**App** (OpenMausBot). One worktree, created once by P3a without touching the main checkout's working tree. The command names the commit by its SHA, not `origin/main`, because other sessions may fetch and every P3a patch is line-exact against that commit:

```
git -C /Users/omkar/Desktop/openmaus/OpenGrokBot worktree add -b feat/gadget-hub /Users/omkar/Desktop/openmaus/OpenGrokBot-gadget 6dd4403d8fbbbd5c17169724cb2a529f11d7543e
```

| Plan | Branch | Created from |
|---|---|---|
| P3a | `feat/gadget-hub` | `6dd4403d8fbbbd5c17169724cb2a529f11d7543e` (`origin/main` when the plans were written) |
| P3b | `feat/gadget-voice` | `feat/gadget-hub` (or `6dd4403d` for its early start, then rebased onto `feat/gadget-hub`; see below) |
| P4a | `feat/gadget-tools` | `feat/gadget-hub` |
| P4b | `feat/gadget-ota` | `feat/gadget-hub` |

Plans run in the worktree one at a time. If two app plans run at the same time, the later one creates its own worktree `/Users/omkar/Desktop/openmaus/OpenGrokBot-gadget-<voice|tools|ota>` from `feat/gadget-hub` and removes it when done. No `git fetch` anywhere.

- **When P3a is finished.** P3a is finished when its T19 hand-off (Task 19: branch-level verification and the hand-off summary) has been reported (the dispatcher confirms). Until then P3b/P4a/P4b use their own worktrees `OpenGrokBot-gadget-<voice|tools|ota>` (full paths above), which is also their default afterwards. P4a and P4b check P3a's deliverables on `feat/gadget-hub` (their Task 1) before they create or switch anything. A later plan takes the shared `/Users/omkar/Desktop/openmaus/OpenGrokBot-gadget` instead only on a first run, once the dispatcher has confirmed P3a is finished, and only when that tree is on `feat/gadget-hub` with an empty `git status --porcelain` (a clean tree alone is not enough, since P3a commits after every task).
- **P3b's early start.** P3b may start from `6dd4403d` (`6dd4403d8fbbbd5c17169724cb2a529f11d7543e`) before `feat/gadget-hub` exists, doing harness-only tasks (T1–T8: they touch only `server/`, `electron/resources/speech-helper.swift` and the allowlists, none of P3a's files), and must rebase onto `feat/gadget-hub` before any companion work (its T9 Step 1, once the dispatcher confirms P3a is finished). This is P3b's deviation D-P3b-2.
- **Merge order.** Omkar merges P3a → P3b → P4a → P4b. Before each later branch is merged, it is rebased onto the branch merged just before it (P3b onto `feat/gadget-hub`, P4a onto `feat/gadget-voice`, P4b onto `feat/gadget-tools`).

P3b, P4a and P4b all branch from `feat/gadget-hub`, not from each other. They touch some of the same files (`companion/src/index.ts`, `companion/src/control.ts`, `server/index.ts`, `electron/*`, `src/components/CompanionSection.tsx`, `src/locales/en.json`). Git reports a conflict when two branches change adjacent lines, so §3.12 (`companion/src/index.ts`) and §3.13 (`companion/src/control.ts`) pin each plan's edits to non-adjacent anchor lines: at least one unchanged line sits between any two plans' edits. Each later plan finds its anchor with `grep -n` on the anchor text and inserts or replaces exactly there. With those anchors the two files merge, and rebase in the order above, without conflicts (P3a checked this in a scratch repository cut from 6dd4403). In the other shared files each plan edits only at its own points (§3.15, §3.17, §3.18, §5.2) and keeps an unchanged line between its edits and any other plan's.

### 1.4 Toolchain and dependency pins

| Item | Pin | Where |
|---|---|---|
| ESP-IDF | **v6.0.3**; Docker `espressif/idf:v6.0.3`; component manifest `idf: ">=6.0.3,<6.1"`; optional CI job on **v5.5.5** with the range relaxed to `>=5.5.5,<6.1` | P2c |
| ESP-IDF install (macOS) | EIM, with its macOS prerequisites first (EIM checks them but does not install them, and stops when one is missing): `brew install libgcrypt glib pixman sdl2 libslirp dfu-util ninja && brew tap espressif/eim && brew install eim && eim install -i v6.0.3 -t esp32s3`, then `. ~/.espressif/tools/activate_idf_v6.0.3.sh`. Fallback: `git clone -b v6.0.3 --depth 1 --recursive --shallow-submodules https://github.com/espressif/esp-idf.git ~/esp/esp-idf-v6.0.3 && cd ~/esp/esp-idf-v6.0.3 && ./install.sh esp32s3 && . ./export.sh`. ninja: `brew install ninja` or `python3 tools/idf_tools.py install ninja` | P2c, AGENTS.md (P2d) |
| Python | 3.10–3.14 (local 3.14.6) | P2c |
| LVGL | **9.6.0**. Desktop FetchContent `https://github.com/lvgl/lvgl/archive/refs/tags/v9.6.0.tar.gz`, `URL_HASH SHA256=b20ee3acc1bba13c62d854f9ebd62e4c51e0b443b1e0225892e86442defa84df`. ESP `lvgl/lvgl: "9.6.0~1"`. Fallback to 9.5.0 only on both sides together | P2b (desktop), P2c (ESP) |
| mbedTLS (desktop) | **3.6.7** default: `https://github.com/Mbed-TLS/mbedtls/releases/download/mbedtls-3.6.7/mbedtls-3.6.7.tar.bz2`, SHA256 `a7e8bcbec0e6f761b4af24f25677626b35f762f68eef79c08677a363212d11f6`, target `mbedcrypto`. CI leg **4.2.0**: `…/mbedtls-4.2.0/mbedtls-4.2.0.tar.bz2`, SHA256 `2bed9d713b4668f76553b097e72b8aa30bc8f112a940d7ae228d524bbde6ffea`, target `tfpsacrypto`. Selected by CMake cache `GADGET_MBEDTLS_VERSION` | P2a |
| mbedTLS (ESP) | whatever IDF bundles (4.1.1 in v6.0.3; 3.6.6 in v5.5.5); PSA API only | P2c |
| wslay | **1.1.1**: `https://github.com/tatsuhiro-t/wslay/archive/refs/tags/release-1.1.1.tar.gz`, SHA256 `7b9f4b9df09adaa6e07ec309b68ab376c0db2cfd916613023b52a47adfda224a`; compiled from its five `.c` files, never its CMakeLists | P2a |
| cJSON | **1.7.19**. Desktop FetchContent `https://github.com/DaveGamble/cJSON/archive/refs/tags/v1.7.19.tar.gz`, SHA256 `7fa616e3046edfa7a28a32d5f9eacfd23f92900fe1f8ccd988c1662f30454562`, populate only, `add_library(cjson STATIC cJSON.c)`. ESP `espressif/cjson: "^1.7.19"` (component `espressif__cjson`). Always `#include "cJSON.h"`; never `REQUIRES json`; never Homebrew's `<cjson/cJSON.h>` | P2a, P2c |
| Unity (C tests) | **2.7.0**: `https://github.com/ThrowTheSwitch/Unity/archive/refs/tags/v2.7.0.tar.gz`, SHA256 `e84eb301ca7967831e68b1728f911e87fa2d345d8ddb64f897bc2f2ee24a321c`, target `unity::framework` | P2a |
| SDL2 | desktop window mode only: Homebrew `sdl2` (sdl2-compat 2.32.x) / Ubuntu `libsdl2-dev` 2.30.x; `find_package(SDL2 REQUIRED CONFIG)` → `SDL2::SDL2` | P2b |
| ESP managed components | `espressif/esp_websocket_client: "^1.8.0"`, `espressif/mdns: "^1.14.0"`, `espressif/cjson: "^1.7.19"`, `espressif/esp_codec_dev: "^1.6.2"`, `espressif/esp_lcd_co5300: "^2.2.0"`, `waveshare/esp_lcd_touch_cst9217: "^2.0.0"`, `espressif/esp_lcd_touch: "^1.2.1"`, `lvgl/lvgl: "9.6.0~1"`. No Waveshare BSP, no `esp_lvgl_port`, no `esp_lvgl_adapter` | P2c |
| `@noble/curves` | **2.4.0** exact, devDependency of the root package, used only by `protocol/tools/gen-vectors.ts` with `{lowS: false, format: "der"}` and `getPublicKey(sk, false)` | P1 |
| `ws` | **8.22.0** exact (same as the app's devDependency), devDependency of the root package, used only by `tools/fake-host` | P1 |
| `esptool-js` | **0.7.0** exact (`fileArray[].data` is a `Uint8Array`) | P2d |
| site build | `esbuild` **0.28.2**, `spark-md5` **3.0.2**, `@types/w3c-web-serial` **1.0.8** (types only) | P2d |
| art tools | `@resvg/resvg-js` **2.6.2** (MPL-2.0, build time only), `pngjs` **7.0.0**, `lv_font_conv` **1.5.3** (exact, in `tools/art/package.json` `devDependencies`, locked by its `package-lock.json`, run only through `npm run fonts` → `node_modules/.bin/lv_font_conv`; never `npx`), fonts from LVGL 9.6.0 `scripts/generators/built_in_font/Montserrat-Medium.ttf` | P2b |
| App image libs | `pngjs` **7.0.0** (MIT), `jpeg-js` **0.4.4** (BSD-3-Clause), root `dependencies` (esbuild inlines them). Both are CommonJS that `require()` Node built-ins, so the ESM server bundle needs the `createRequire` banner (the one `scripts/prepare-cua.mjs` uses) on the first `build()` in `scripts/bundle-server.mjs` (the server entry points); without it the bundle fails at load with `Dynamic require of "util" is not supported` (P4a deviation 1, approved; §5.2) | P4a |
| Node (SDK tools) | `engines.node >= 22.18` (runs `.ts` directly with type stripping; erasable syntax only: no `enum`, no parameter properties, no namespaces; relative imports end in `.ts`). CI uses Node 24 | P1, P2a, P2b, P2d |
| Node (app) | ≥ 24, local `~/.nvm/versions/node/v24.14.1/bin`; pnpm 10.33.0 (the app's `packageManager`; that PATH has 11.9.0, so check per §0 item 5); vitest ^4.1.10 (existing) | P3a, P3b, P4a, P4b |
| C | C11 (`CMAKE_C_STANDARD 11`, extensions off for `gadget_core` and `gadget_ui`); must also compile as gnu23 under IDF 6 with warnings as errors. `ports/sim` compiles with `_POSIX_C_SOURCE=200809L` (and `_DARWIN_C_SOURCE` on macOS) | P2a–P2c |
| CMake | desktop `cmake_minimum_required(VERSION 3.24)` (local 4.3.4); ESP project `3.22` | P2a, P2c |

SDK root `package.json` (P1), exact shape (P1 may add scripts, not dependencies other plans rely on):

```json
{
  "name": "openmausbot-gadget-sdk-tools",
  "private": true,
  "type": "module",
  "license": "Apache-2.0",
  "engines": { "node": ">=22.18" },
  "scripts": {
    "vectors": "node protocol/tools/gen-vectors.ts",
    "vectors:check": "node protocol/tools/gen-vectors.ts && git diff --exit-code protocol/vectors && test -z \"$(git status --porcelain -- protocol/vectors)\"",
    "test:protocol": "node --test \"protocol/test/**/*.test.ts\"",
    "fake-host": "node tools/fake-host/src/main.ts",
    "test:fake-host": "node --test \"tools/fake-host/test/**/*.test.ts\"",
    "test": "npm run test:protocol && npm run test:fake-host"
  },
  "devDependencies": { "@noble/curves": "2.4.0", "ws": "8.22.0" }
}
```

P2a adds `"test:e2e": "node firmware/tests/e2e/run.ts --all"` to the same file. `tools/art/` and `site/` have their own `package.json` and lockfile; every SDK npm install uses `npm ci`. Drift checks over generated files (`vectors:check`, the art drift check in §1.5/§1.6) always end with `test -z "$(git status --porcelain -- <dir>)"`, because `git diff --exit-code` alone ignores new files that were never committed.

### 1.5 Build and test commands

SDK, from the repo root:

| Area | Command |
|---|---|
| Protocol (P1) | `npm ci && npm run vectors:check && npm run test:protocol` |
| Fake host (P1) | `npm run test:fake-host`; run: `node tools/fake-host/src/main.ts --port 8810 --code 123456` |
| Core + headless sim + C tests (P2a) | `cmake -S firmware -B build/host -DCMAKE_BUILD_TYPE=Debug -DGADGET_WITH_LVGL=OFF && cmake --build build/host -j10 && ctest --test-dir build/host --output-on-failure -L "unit|vectors|e2e"` |
| mbedTLS 4 leg (P2a) | same with `-B build/host-mbedtls4 -DGADGET_MBEDTLS_VERSION=4.2.0` |
| UI + snapshots (P2b) | `cmake -S firmware -B build/host -DCMAKE_BUILD_TYPE=Debug && cmake --build build/host -j10 && ctest --test-dir build/host --output-on-failure -L "unit|vectors|e2e|snapshot"` (after P2b, `GADGET_WITH_LVGL` defaults to `ON`) |
| Update snapshot goldens (P2b) | `cmake -S firmware -B build/snap -DGADGET_SNAPSHOT_UPDATE=ON && cmake --build build/snap -j10 && ctest --test-dir build/snap -L snapshot`, then review `git status --short firmware/tests/snapshots` and `git diff --stat firmware/tests/snapshots`. In this build every `snapshot <name>` deletes `<snapshot-dir>/<name>.png` first and writes a fresh one (§2.16, §2.18), because LVGL 9.6.0 only creates a reference when the PNG is missing |
| Art + fonts (P2b) | `cd tools/art && npm ci && npm run art && npm run fonts && npm run budget`; drift check: `npm run art && test -z "$(git status --porcelain -- ../../firmware/ui/art)"` (linux-x64 CI only; §1.6 says how a darwin-arm64 result is reconciled) |
| Re-pin art source (P2b, rare) | `cd tools/art && npm run extract -- --app /Users/omkar/Desktop/openmaus/OpenGrokBot --commit 6dd4403d8fbbbd5c17169724cb2a529f11d7543e` (reads with `git show` only) |
| Window sim (P2b) | `./build/host/ports/sim/gadget-sim --board amoled-175c --host 127.0.0.1:8810 --pair 123456` |
| ESP32 board (P2c) | `. ~/.espressif/tools/activate_idf_v6.0.3.sh && cd firmware/ports/esp32 && idf.py -B build/<board> -D GADGET_BOARD=<board> -D SDKCONFIG=build/<board>/sdkconfig build && tools/check-size.sh <board>` |
| Flash + monitor (P2c, manual) | `idf.py -B build/<board> -D GADGET_BOARD=<board> -D SDKCONFIG=build/<board>/sdkconfig -p /dev/cu.usbmodem* flash monitor` |
| Site (P2d) | `cd site && npm ci && npm test && npm run build` |

The desktop build directory is `build/` at the repo root; the CMake binary tree mirrors `firmware/`, so the simulator is `build/host/ports/sim/gadget-sim` (P2a pins the exact path in its plan and in `AGENTS.md`). `ctest` labels: `unit`, `vectors`, `e2e`, `snapshot`.

App, from `/Users/omkar/Desktop/openmaus/OpenGrokBot-gadget` with Node 24 on PATH:

| Area | Command |
|---|---|
| Install | `pnpm --version` (must print 10.33.0, else use `corepack pnpm@10.33.0` per §0 item 5), then `pnpm install --frozen-lockfile` |
| Companion gadget tests | `pnpm exec vitest run companion/test/gadget` |
| Existing companion tests | `pnpm exec vitest run companion/test` |
| Harness route tests | `pnpm exec vitest run server/routes/stt.test.ts server/tts server/routes/internal-gadgets.test.ts server/gadget-image.test.ts server/gadget-control.test.ts` |
| Catalog goldens (P4a) | `UPDATE_AGENTS_CATALOG_GOLDENS=1 pnpm exec vitest run server/drivers/agents-catalog-wire.test.ts`, copy the printed sizes into `BUDGET_BASELINE`, rerun without the variable |
| Ratchet and auth | `pnpm exec vitest run scripts/testing/index-route-ratchet.test.ts server/request-auth.test.ts companion/test/routes.test.ts` |
| Renderer | `pnpm exec vitest run src/components/CompanionSection.gadget.test.ts src/lib/gadgets.test.ts` |
| Electron | `pnpm test:electron` |
| Static checks | `pnpm typecheck && pnpm lint && pnpm i18n:check` |
| Packaged server smoke | `pnpm test:packaged-server` (P3b, P4a: new harness dependencies must bundle) |
| Companion build | `pnpm build:companion` (plain `tsc`; the companion stays dependency-free) |

### 1.6 SDK CI jobs (`.github/workflows/ci.yml`)

One workflow, `on: [push, pull_request]`. P1 creates the file; each later plan appends its own job and never edits another plan's job. Job ids are fixed:

| Job id | Owner | Runs on | Does |
|---|---|---|---|
| `protocol` | P1 | ubuntu-24.04, Node 24 | `npm ci`, `npm run vectors:check`, `npm run test:protocol` |
| `fake-host` | P1 | ubuntu-24.04, Node 24 | `npm ci`, `npm run test:fake-host` |
| `host-c` | P2a | matrix ubuntu-24.04 + macos-14, mbedTLS 3.6.7 + 4.2.0 | `actions/setup-node` (Node 24) and `npm ci` at the repo root (the `e2e` label runs `node firmware/tests/e2e/run.ts`, whose fake host imports `ws`), then configure with `-DGADGET_WITH_LVGL=OFF`, build, `ctest -L "unit|vectors|e2e"` |
| `ui` | P2b | ubuntu-24.04 (`sudo apt-get install -y libsdl2-dev`) | `actions/setup-node` (Node 24) and `npm ci` at the repo root, then the full desktop build, `ctest -L snapshot` and the rest |
| `art-drift` | P2b | ubuntu-24.04 (linux-x64 only), Node 24 | `cd tools/art && npm ci && npm run art`, then `test -z "$(git status --porcelain -- firmware/ui/art)"`, plus `npm run budget`; on failure it uploads the regenerated `firmware/ui/art` as artifact `art-linux-x64` |
| `esp32` | P2c | `container: espressif/idf:v6.0.3`, matrix of the four boards | `. $IDF_PATH/export.sh` in every step (container jobs skip the image entrypoint), build per §1.5, `tools/check-size.sh`. Its `amoled-175c`/`plain` leg also runs `tools/build-all.sh` (all four boards built one after another in one checkout, spec §10); `build-all.sh` finds the boards under `boards/` |
| `esp32-idf55` | P2c | `container: espressif/idf:v5.5.5`, `continue-on-error: true` | relax the manifest's `idf` range, build `amoled-175c` |
| `site` | P2d | ubuntu-24.04, Node 24 | `cd site && npm ci && npm test && npm run build`; also runs `node --test "tools/release/test/*.test.ts"` from the repo root, which includes P4b's `dev-release.test.ts` (§5.1) |

**Art generated on this Mac vs the linux-x64 drift check (P2b).** resvg output can differ in the least significant bits between darwin-arm64 and linux-x64 (R8), and P2b runs on darwin-arm64, so its committed art may fail `art-drift` once Omkar pushes. linux-x64 is the reference. P2b also adds `.github/workflows/art-regen.yml` (`on: workflow_dispatch` only, one job on ubuntu-24.04 with Node 24): `cd tools/art && npm ci && npm run art`, then the snapshot-update build of §1.5 (`sudo apt-get install -y libsdl2-dev` first), then it uploads `firmware/ui/art` and `firmware/tests/snapshots` together as artifact `art-and-snapshots-linux-x64`. When `art-drift` fails only by such differences, the person publishing commits that artifact's two folders unchanged (the goldens change with the art). P2b's plan says this in its hand-off notes; no plan pushes or dispatches workflows itself.

### 1.7 Fixed values shared by tests

Every value below was computed with `@noble/curves` 2.4.0 (`lowS: false`) and checked with Node `crypto.verify`. P1's generator must reproduce them byte for byte, and the C and TS tests may hard-code them as cross-checks.

| Name | Value |
|---|---|
| RFC key (RFC 6979 A.2.5) | private `c9afa9d845ba75166b5c215767b1d6934e50c3db36e89b127b8a622b120f6721` |
| RFC key `pubkey` | `BGD+1LolWp0xyWHrdMY1bWjASbiSO2H6bOZpYi5g8p+2eQP+EAi4vJmkGunpVii8ZPLxsgwtfp9Rd6PClNRGIpk=` |
| RFC key `id` | `gad_b18b86ce1389e46d` |
| RFC "sample" DER (high-S, 72 B) | `3046022100efd48b2aacb6a8fd1140dd9cd45e81d69d2c877b56aaf991c34d0ea84eaf3716022100f7cb1c942d657c41d436c7a1b6e29f65f3e900dbb9aff4064dc4ab2f843acda8` |
| Pinned `nonce` | `AAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8=` (bytes 0x00–0x1f) |
| Pinned `host_id` | `000102030405060708090a0b0c0d0e0f` |
| Prove text | `openmausbot-gadget/1\nprove\ngad_b18b86ce1389e46d\nAAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8=\n000102030405060708090a0b0c0d0e0f` (no trailing newline) |
| Prove DER (deterministic, high-S, 72 B) | `3046022100f0c4fbe24029d797b16b36dcc0d05fb7a8b9df8c5ca4b27ad99d820d4d8a664202210087d087e1c83ab59e8feebee63a40b423c5af81956ceca8033264098abf44e35a` |
| Prove `sig` (base64) | `MEYCIQDwxPviQCnXl7FrNtzA0F+3qLnfjFyksnrZnYINTYpmQgIhAIfQh+HIOrWej+6+5jpAtCPFr4GVbOyoAzJkCYq/RONa` |
| Low-S control | the same inputs with `host_id` `0123456789abcdef0123456789abcdef` give a low-S signature (P1 records it as a non-high-S case) |
| Short-DER case (69 B) | RFC key over UTF-8 `openmausbot-gadget/1 der-short 13`: `3043022052c1af44f658bb58a5b434a96d609052855835feca85b011cdfcb4159f52c37e021f2d466a13caea297fa0c97498ce12ed77ea25e9895415c4bb6ebe64b2a049cb` |
| Test release key `t1` private | `274b871e29523ce21208177b90a0a3bd6a169cb84cf867236adc68f3d90fc0c8` = SHA-256 of the UTF-8 text `openmausbot-gadget/1 test release key t1` |
| Test key `t1` public | `BIUTH1UeVJw2VXn0Ehdhd8apNOHmB6VvjV512SxIbCCXfUhvlLRTuOTLHtrsVAbIx06JhMoOMbC3ic73hZpxMwg=` |
| Firmware text vector | `openmausbot-gadget/1\nfirmware\namoled-175c\n1.1.0\n1234567\ne3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855` |
| Firmware DER with `t1` (high-S) | `3046022100de2dfa125231a739f0b1b27e2ec085e4027b59e4486978457ffccb95b3895f2d022100ef00bd5c4177cb97ba4a4f4036046f6a20b3c896c5ab1124720b54afe6179744` |
| Ports | companion LAN `8810`, control `8811` (loopback), harness `8799` |
| SDK repo / manifest / Pages | `https://github.com/aivsomkar/openmausbot-gadget-sdk`, `…/releases/latest/download/manifest.json`, `https://aivsomkar.github.io/openmausbot-gadget-sdk/` |
| App pin for art and line refs | OpenMausBot `origin/main` `6dd4403d8fbbbd5c17169724cb2a529f11d7543e` |

## 2. Firmware C contract

### 2.1 Conventions

- C11; every header compiles with `-std=c11 -Wall -Wextra -Wpedantic -Werror` and with gnu23 (all headers below were compile-checked that way). License header `/* SPDX-License-Identifier: Apache-2.0 */` on every source file, except the generated `firmware/ui/fonts/font_latin1_*.c`, which carry `SPDX-License-Identifier: OFL-1.1` (written `/* SPDX-License-Identifier: OFL-1.1 */`): they are bitmaps derived from Montserrat (P2b deviation 1, approved). Their header `ui_fonts.h` keeps Apache-2.0, and P2d's `THIRD_PARTY.md` row for Montserrat covers the licence.
- Prefixes: `gadget_` shared types and helpers, `hal_` the port contract, `core_` the core API, `ui_` the UI API, `gp_` the protocol codec, `maus_` generated art, `sim_` simulator internals, `board_` ESP32 board glue.
- All contract headers live in `firmware/core/include/` (owned by P2a), including `gadget_ui.h`, which P2a's stub and P2b's LVGL UI both implement. Core sources include only `gadget_*.h`, `cJSON.h`, `psa/crypto.h` and libc (spec §5.1). `firmware/ui` includes `lvgl.h` and core headers only.
- Single thread: the port calls, in order and every 10 ms, `core_tick(now)`, `ui_render(core_ui_model())`, `ui_tick(now)`. Every `core_event()` call happens on that thread between ticks.
- Ownership of memory: core allocates with `malloc`/`free` (cJSON and image buffers; PSRAM on ESP32 through `CONFIG_SPIRAM_USE_MALLOC`). Pointers in events are borrowed for the duration of the call.
- Text: UTF-8 everywhere on the wire and in the model. "Latin-1" names the glyph repertoire (U+0020–U+007E, U+00A0–U+00FF, plus U+2026 and U+2192), not an encoding.

### 2.2 `gadget_types.h`

```c
/* firmware/core/include/gadget_types.h */
/* SPDX-License-Identifier: Apache-2.0 */
/* Shared constants and small types. Values mirror protocol/PROTOCOL.md. */
#ifndef GADGET_TYPES_H
#define GADGET_TYPES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
  GADGET_OK = 0,
  GADGET_ERR = -1,             /* unspecified failure */
  GADGET_ERR_ARG = -2,         /* bad argument */
  GADGET_ERR_NO_MEM = -3,
  GADGET_ERR_NOT_FOUND = -4,   /* storage key missing, unknown id */
  GADGET_ERR_BUSY = -5,        /* try again later (queue full, socket not open) */
  GADGET_ERR_LIMIT = -6,       /* a size or count limit was exceeded */
  GADGET_ERR_IO = -7,          /* flash, file or socket error */
  GADGET_ERR_CRYPTO = -8,      /* PSA call failed */
  GADGET_ERR_BAD_SIG = -9,     /* signature did not verify */
  GADGET_ERR_UNSUPPORTED = -10,/* not available on this port or board */
  GADGET_ERR_STATE = -11,      /* not valid in the current state */
  GADGET_ERR_TIMEOUT = -12,
  GADGET_ERR_PARSE = -13       /* malformed JSON, base64, DER or argument */
} gadget_status_t;

/* Protocol limits (PROTOCOL.md §4.1–§4.3; firmware ones §4.8) */
#define GADGET_PROTO_VERSION 1
#define GADGET_SUBPROTOCOL "openmausbot-gadget.1"
#define GADGET_WS_PATH "/gadget"
#define GADGET_DEFAULT_PORT 8810u
#define GADGET_TEXT_FRAME_MAX 16384u
#define GADGET_BINARY_FRAME_MAX 8192u
#define GADGET_IDLE_TIMEOUT_MS 45000u
#define GADGET_MIC_RATE 16000u
#define GADGET_MIC_FRAME_SAMPLES 320u     /* 20 ms at 16 kHz */
#define GADGET_UTTERANCE_MAX_MS 60000u
#define GADGET_SAY_MAX 2000u              /* characters */
#define GADGET_TURN_MAX 32u               /* bytes, excluding NUL */
#define GADGET_NAME_MAX 32u               /* characters */
#define GADGET_ACTIONS_MAX 16u
#define GADGET_ACTION_NAME_MAX 32u
#define GADGET_ACTION_DESC_MAX 200u
#define GADGET_ACTION_PARAMS_MAX 1024u    /* serialized bytes */
#define GADGET_SENSE_MIN_INTERVAL_MS 10000u
#define GADGET_FW_CHUNK_MAX 4096u
#define GADGET_FW_PROGRESS_EVERY 16384u
#define GADGET_FW_CHUNK_TIMEOUT_MS 30000u
#define GADGET_FW_COMMIT_TIMEOUT_MS 30000u
#define GADGET_PROBATION_MS 300000u
#define GADGET_JITTER_BUFFER_MS 1000u

/* Interaction (spec §5.4, §4.3) */
#define GADGET_PRESS_MIN_MS 300u
#define GADGET_ASK_LOCK_MS 600u
#define GADGET_COUNTDOWN_MS 5000u
#define GADGET_REPLY_IDLE_MS 20000u
#define GADGET_BACKOFF_MIN_MS 2000u
#define GADGET_BACKOFF_MAX_MS 60000u
#define GADGET_DEVICE_LIMIT_RETRY_MS 10000u
#define GADGET_DEVICE_LIMIT_WINDOW_MS 120000u
#define GADGET_HOST_AUTO_TIMEOUT_MS 5000u

/* Identity and crypto sizes */
#define GADGET_ID_LEN 20u                 /* "gad_" + 16 lowercase hex */
#define GADGET_HOST_ID_LEN 32u            /* 32 lowercase hex */
#define GADGET_PAIR_CODE_LEN 6u
#define GADGET_PRIVKEY_LEN 32u
#define GADGET_PUBKEY_LEN 65u             /* SEC1 uncompressed, starts 0x04 */
#define GADGET_PUBKEY_B64_LEN 88u         /* base64 of 65 bytes, with padding */
#define GADGET_NONCE_B64_LEN 44u          /* base64 of 32 bytes */
#define GADGET_SIG_DER_MAX 72u
#define GADGET_SIG_B64_MAX 96u
#define GADGET_SHA256_LEN 32u
#define GADGET_VERSION_MAX 32u            /* firmware version string, bytes */
#define GADGET_BOARD_ID_MAX 32u

typedef struct {
  int16_t x, y, w, h;
} gadget_rect_t;

typedef struct {
  uint8_t pct;     /* 0–100 */
  bool charging;
} gadget_battery_t;

#endif /* GADGET_TYPES_H */
```

### 2.3 `gadget_board.h` and the board table

```c
/* firmware/core/include/gadget_board.h */
/* SPDX-License-Identifier: Apache-2.0 */
/* Board descriptor: everything core and UI need to know about a board.
 * The four descriptors live in core/src/boards.c (one table for the
 * simulator and the ESP32 port); pins live in ports/esp32/boards/<id>/board.h. */
#ifndef GADGET_BOARD_H
#define GADGET_BOARD_H

#include "gadget_types.h"

typedef enum {
  GADGET_ART_S240 = 0,  /* body 201x240: amoled-175c, amoled-175 */
  GADGET_ART_S150 = 1   /* body 125x150: lcd-154, devkit */
} gadget_art_profile_t;

#define GADGET_INPUT_TOUCH  (1u << 0)
#define GADGET_INPUT_TALK   (1u << 1)
#define GADGET_INPUT_CANCEL (1u << 2)

typedef struct gadget_board {
  const char *id;              /* /^[a-z0-9-]{1,32}$/, e.g. "amoled-175c"; hello.board */
  const char *display_name;    /* "Waveshare ESP32-S3-Touch-AMOLED-1.75C" */
  uint16_t screen_w, screen_h; /* caps.screen.w/h */
  bool screen_round;           /* caps.screen.round */
  uint16_t image_w, image_h;   /* caps.image.w/h */
  uint32_t mic_rate;           /* caps.mic.rate: 16000 */
  uint32_t speaker_rate;       /* caps.speaker.rate: 16000 or 24000; 0 = no speaker (omitted) */
  uint32_t input_mask;         /* GADGET_INPUT_*; caps.input in order touch, talk, cancel */
  bool has_battery;            /* caps.battery (omitted when false) */
  uint32_t ota_max;            /* caps.ota.max = OTA slot size in bytes */
  gadget_art_profile_t art_profile;
} gadget_board_t;

/* Lookup over the four built-in descriptors; NULL for an unknown id. */
const gadget_board_t *gadget_board_by_id(const char *id);
/* Iteration: index 0..n-1, NULL past the end. */
const gadget_board_t *gadget_board_at(size_t index);

#endif /* GADGET_BOARD_H */
```

The four descriptors in `core/src/boards.c` (P2a). Adding a board means adding a row here, a directory under `ports/esp32/boards/`, and its art profile (AGENTS.md, P2d).

| `id` | `display_name` | screen | round | image | mic | speaker | input | battery | `ota_max` | art |
|---|---|---|---|---|---|---|---|---|---|---|
| `amoled-175c` | Waveshare ESP32-S3-Touch-AMOLED-1.75C | 466×466 | yes | 300×300 | 16000 | 16000 | TOUCH \| TALK \| CANCEL | yes | 6291456 | S240 |
| `amoled-175` | Waveshare ESP32-S3-Touch-AMOLED-1.75 | 466×466 | yes | 300×300 | 16000 | 16000 | TOUCH \| TALK | yes | 6291456 | S240 |
| `lcd-154` | Waveshare ESP32-S3-LCD-1.54 | 240×240 | no | 200×200 | 16000 | 16000 | TALK \| CANCEL | yes | 6291456 | S150 |
| `devkit` | ESP32-S3-DevKitC-1-N16R8 + 2" ST7789 | 320×240 | no | 280×200 | 16000 | 24000 | TALK \| CANCEL | no | 6291456 | S150 |

`hello.caps` is built from the descriptor exactly like this (amoled-175c shown; `speaker` is omitted when `speaker_rate` is 0, `battery` is omitted when false):

```json
{"screen":{"w":466,"h":466,"round":true,"text":"latin1"},"image":{"w":300,"h":300},
 "mic":{"rate":16000},"speaker":{"rate":16000},"input":["touch","talk","cancel"],
 "battery":true,"ota":{"max":6291456}}
```

### 2.4 `gadget_events.h`

```c
/* firmware/core/include/gadget_events.h */
/* SPDX-License-Identifier: Apache-2.0 */
/* Events a port delivers to core with core_event(), always on the main
 * thread (the thread that calls core_tick). Pointers inside an event are
 * valid only for the duration of the core_event() call; ports that queue
 * events across threads copy the payload first and free it afterwards. */
#ifndef GADGET_EVENTS_H
#define GADGET_EVENTS_H

#include "gadget_types.h"

typedef enum {
  GADGET_EV_INPUT = 1,      /* u.input */
  GADGET_EV_MIC_FRAME,      /* u.mic: GADGET_MIC_FRAME_SAMPLES samples at 16 kHz */
  GADGET_EV_WIFI_STATE,     /* u.wifi */
  GADGET_EV_WIFI_SCAN,      /* u.scan: result of hal_wifi_scan() */
  GADGET_EV_WS_OPEN,        /* the WebSocket upgrade completed */
  GADGET_EV_WS_TEXT,        /* u.ws: one complete text message (UTF-8, not NUL-terminated) */
  GADGET_EV_WS_BINARY,      /* u.ws: one complete binary message */
  GADGET_EV_WS_CONTROL,     /* a ping or pong arrived (liveness only; the port answers pings) */
  GADGET_EV_WS_CLOSED,      /* u.closed: once per hal_ws_open(), after failure or close */
  GADGET_EV_MDNS,           /* u.mdns: result of hal_mdns_browse() */
  GADGET_EV_CONSOLE_LINE,   /* u.console: one console line, line ending removed */
  GADGET_EV_OTA_WRITTEN,    /* u.ota_written: progress of hal_ota_write() */
  GADGET_EV_OTA_ERROR       /* u.ota_error: a queued OTA write failed */
} gadget_event_type_t;

typedef enum {
  GADGET_IN_TALK_DOWN = 1,
  GADGET_IN_TALK_UP,
  GADGET_IN_CANCEL_DOWN,
  GADGET_IN_CANCEL_UP,
  GADGET_IN_TOUCH_DOWN,     /* x, y in screen pixels */
  GADGET_IN_TOUCH_MOVE,
  GADGET_IN_TOUCH_UP,
  GADGET_IN_SWIPE           /* dir; only the simulator script sends this; core also derives swipes from touch */
} gadget_input_type_t;

typedef enum { GADGET_SWIPE_UP = 0, GADGET_SWIPE_DOWN, GADGET_SWIPE_LEFT, GADGET_SWIPE_RIGHT } gadget_swipe_dir_t;

typedef struct {
  gadget_input_type_t type;
  int16_t x, y;
  gadget_swipe_dir_t dir;
} gadget_input_t;

typedef struct {
  const int16_t *pcm;       /* mono PCM16, host byte order */
  uint16_t samples;         /* GADGET_MIC_FRAME_SAMPLES */
} gadget_mic_frame_t;

typedef enum {
  GADGET_WIFI_OFF = 0,      /* no network configured */
  GADGET_WIFI_CONNECTING,
  GADGET_WIFI_CONNECTED,
  GADGET_WIFI_FAILED
} gadget_wifi_state_t;

typedef struct {
  gadget_wifi_state_t state;
  char ip[16];              /* dotted IPv4 when CONNECTED, else "" */
} gadget_wifi_ev_t;

typedef enum {
  GADGET_AUTH_OPEN = 0, GADGET_AUTH_WEP, GADGET_AUTH_WPA, GADGET_AUTH_WPA2,
  GADGET_AUTH_WPA3, GADGET_AUTH_WPA2_ENT, GADGET_AUTH_OTHER
} gadget_wifi_auth_t;       /* @omb scan "auth": open wep wpa wpa2 wpa3 wpa2-ent other */

typedef struct {
  char ssid[33];
  int8_t rssi;              /* dBm */
  gadget_wifi_auth_t auth;
} gadget_wifi_ap_t;

typedef struct {
  const gadget_wifi_ap_t *aps;
  uint8_t count;            /* ≤ 20, strongest first, de-duplicated by ssid */
  bool ok;                  /* false: the scan failed */
} gadget_wifi_scan_ev_t;

typedef struct {
  const uint8_t *data;
  size_t len;
} gadget_ws_data_t;

typedef struct {
  uint16_t code;            /* close code from the peer, or 0 when the connect/upgrade failed or the socket dropped */
} gadget_ws_closed_t;

typedef struct {
  char name[64];            /* service instance name, e.g. "Omkar's computer" */
  char address[48];         /* "a.b.c.d:port" */
  char id[GADGET_HOST_ID_LEN + 1]; /* TXT id=, "" when absent */
} gadget_mdns_host_t;

typedef struct {
  const gadget_mdns_host_t *hosts;
  uint8_t count;            /* ≤ 8 */
  bool ok;                  /* false: browsing failed or is unsupported */
} gadget_mdns_ev_t;

typedef struct {
  const char *line;         /* NUL-terminated, ≤ GADGET_CONSOLE_LINE_MAX - 1 bytes, no CR/LF */
} gadget_console_ev_t;

typedef struct {
  uint32_t written;         /* contiguous bytes durably written since hal_ota_begin() */
} gadget_ota_written_t;

typedef struct {
  gadget_status_t err;
} gadget_ota_error_t;

typedef struct gadget_event {
  gadget_event_type_t type;
  union {
    gadget_input_t input;
    gadget_mic_frame_t mic;
    gadget_wifi_ev_t wifi;
    gadget_wifi_scan_ev_t scan;
    gadget_ws_data_t ws;
    gadget_ws_closed_t closed;
    gadget_mdns_ev_t mdns;
    gadget_console_ev_t console;
    gadget_ota_written_t ota_written;
    gadget_ota_error_t ota_error;
  } u;
} gadget_event_t;

#endif /* GADGET_EVENTS_H */
```

### 2.5 `gadget_hal.h`

The HAL groups of spec §5.2, function by function. P2a implements the simulator backends and a fake HAL for CTest; P2c implements the ESP32 port; core implements the crypto group.

```c
/* firmware/core/include/gadget_hal.h */
/* SPDX-License-Identifier: Apache-2.0 */
/* The whole contract between core and a port (spec §5.2).
 *
 * Threading: core and UI are single-threaded. The port calls core_tick()
 * every 10 ms and delivers every event (gadget_events.h) on that same thread.
 * Every hal_* function below is called only from that thread and must not
 * block for more than a few milliseconds, except where noted.
 *
 * The crypto group is implemented by core (core/src/crypto_psa.c) for both
 * ports, on the PSA Crypto API. Ports only call psa_crypto_init() at boot,
 * before core_init(). */
#ifndef GADGET_HAL_H
#define GADGET_HAL_H

#include <stdarg.h>
#include "gadget_types.h"
#include "gadget_events.h"

#if defined(__GNUC__) || defined(__clang__)
#define GADGET_PRINTF(f, a) __attribute__((format(printf, f, a)))
#else
#define GADGET_PRINTF(f, a)
#endif

/* ---- Mic ---------------------------------------------------------------- */
/* Start capture at `rate` (16000 in v1; anything else → GADGET_ERR_UNSUPPORTED).
 * Frames arrive as GADGET_EV_MIC_FRAME, 20 ms each, until hal_mic_stop(). */
gadget_status_t hal_mic_start(uint32_t rate);
void hal_mic_stop(void);

/* ---- Speaker ------------------------------------------------------------ */
/* Open (or keep open) playback at `rate`. Codec boards accept 16000 only. */
gadget_status_t hal_spk_open(uint32_t rate);
/* Queue mono PCM16 (host byte order). Never blocks; returns the number of
 * samples accepted (0 when the port's buffer is full). */
size_t hal_spk_write(const int16_t *pcm, size_t samples);
/* Milliseconds of audio queued but not yet played. */
uint32_t hal_spk_buffered_ms(void);
/* Drop everything queued and go silent: at once on boards with a codec
 * mute; within one DMA ring (≤ 60 ms on the devkit) on boards without one.
 * The device stays open. */
void hal_spk_stop(void);
/* Output level 0–100. */
void hal_spk_set_volume(uint8_t pct);

/* ---- Input -------------------------------------------------------------- */
/* Events only (GADGET_EV_INPUT). Ports report raw talk/cancel edges and raw
 * touch down/move/up; core derives holds, taps and swipes. The board's
 * input_mask says which sources exist. */

/* ---- Storage ------------------------------------------------------------ */
/* One key/value namespace "gadget" (NVS on ESP32, a JSON file in the
 * simulator). Keys are the GADGET_KEY_* names in gadget_core.h (≤ 15 bytes).
 * Writes are durable when the call returns. */
gadget_status_t hal_storage_get_str(const char *key, char *buf, size_t cap);   /* NOT_FOUND, LIMIT if cap too small */
gadget_status_t hal_storage_set_str(const char *key, const char *value);
gadget_status_t hal_storage_get_blob(const char *key, void *buf, size_t cap, size_t *len);
gadget_status_t hal_storage_set_blob(const char *key, const void *data, size_t len);
gadget_status_t hal_storage_erase(const char *key);   /* GADGET_OK when missing */
gadget_status_t hal_storage_erase_all(void);          /* the "gadget" namespace only */

/* ---- Net: Wi-Fi --------------------------------------------------------- */
/* The port starts the radio (ESP32: esp_wifi_start() in STA mode) before
 * core_init(), so the RNG is truly random when core generates the key.
 * The simulator always reports GADGET_WIFI_CONNECTED. */
/* "" = open network; result via GADGET_EV_WIFI_STATE. Before it returns,
 * hal_wifi_state() reports GADGET_WIFI_CONNECTING (or CONNECTED), so a
 * console `status` right after `wifi` shows "connecting". */
gadget_status_t hal_wifi_connect(const char *ssid, const char *password);
void hal_wifi_disconnect(void);
gadget_wifi_state_t hal_wifi_state(void);
gadget_status_t hal_wifi_scan(void);   /* result via GADGET_EV_WIFI_SCAN */

/* ---- Net: WebSocket ----------------------------------------------------- */
/* One connection at a time to ws://<host>:<port>/gadget with subprotocol
 * openmausbot-gadget.1, no Origin header, no extensions, 5 s connect
 * timeout, no automatic reconnect (core drives reconnects). The port
 * answers pings, reassembles fragments and delivers whole messages
 * (text ≤ 16 KiB, binary ≤ 8 KiB). Exactly one GADGET_EV_WS_CLOSED follows
 * every successful hal_ws_open(). */
gadget_status_t hal_ws_open(const char *host, uint16_t port);
gadget_status_t hal_ws_send_text(const char *data, size_t len);        /* BUSY when not open */
gadget_status_t hal_ws_send_binary(const uint8_t *data, size_t len);   /* BUSY when not open */
void hal_ws_close(uint16_t code);

/* ---- Net: mDNS ---------------------------------------------------------- */
/* Browse _openmausbot._tcp for up to timeout_ms; one GADGET_EV_MDNS with
 * every instance found (TXT id= included). Linux simulator:
 * GADGET_ERR_UNSUPPORTED (no event). */
gadget_status_t hal_mdns_browse(uint32_t timeout_ms);

/* ---- Crypto (implemented by core on PSA; ports do not implement) -------- */
/* P-256 key pair; priv = 32-byte scalar, pub = 65-byte SEC1 uncompressed. */
gadget_status_t hal_crypto_keygen(uint8_t priv[GADGET_PRIVKEY_LEN], uint8_t pub[GADGET_PUBKEY_LEN]);
gadget_status_t hal_crypto_pubkey(const uint8_t priv[GADGET_PRIVKEY_LEN], uint8_t pub[GADGET_PUBKEY_LEN]);
/* SHA-256 of msg, then PSA_ALG_DETERMINISTIC_ECDSA(PSA_ALG_SHA_256) via
 * psa_sign_hash; S is never normalized; output is DER (≤ 72 bytes). */
gadget_status_t hal_crypto_sign(const uint8_t priv[GADGET_PRIVKEY_LEN], const uint8_t *msg, size_t len,
                                uint8_t der[GADGET_SIG_DER_MAX], size_t *der_len);
/* SHA-256 of msg, then psa_verify_hash with PSA_ALG_ECDSA(PSA_ALG_SHA_256).
 * Accepts high-S. GADGET_OK or GADGET_ERR_BAD_SIG (also for malformed DER). */
gadget_status_t hal_crypto_verify(const uint8_t pub[GADGET_PUBKEY_LEN], const uint8_t *msg, size_t len,
                                  const uint8_t *der, size_t der_len);
gadget_status_t hal_crypto_sha256(const void *data, size_t len, uint8_t out[GADGET_SHA256_LEN]);
/* Multi-part SHA-256 for OTA images (psa_hash_setup/update/finish/abort).
 * begin mallocs a psa_hash_operation_t, sets it to PSA_HASH_OPERATION_INIT
 * and stores it in op (GADGET_ERR_NO_MEM when malloc fails); finish and
 * abort free it and set op to NULL; abort with op == NULL does nothing.
 * The struct's size never depends on the PSA implementation. */
typedef struct {
  void *op;   /* psa_hash_operation_t *, owned by crypto_psa.c */
} hal_sha256_t;
gadget_status_t hal_crypto_sha256_begin(hal_sha256_t *ctx);
gadget_status_t hal_crypto_sha256_update(hal_sha256_t *ctx, const void *data, size_t len);
gadget_status_t hal_crypto_sha256_finish(hal_sha256_t *ctx, uint8_t out[GADGET_SHA256_LEN]);
void hal_crypto_sha256_abort(hal_sha256_t *ctx);
gadget_status_t hal_crypto_random(void *buf, size_t len);

/* ---- OTA slot ----------------------------------------------------------- */
typedef enum {
  HAL_OTA_IMG_VALID = 0,         /* running image is confirmed (or OTA never used) */
  HAL_OTA_IMG_PENDING_VERIFY,    /* first boot of a new image: probation */
  HAL_OTA_IMG_UNKNOWN
} hal_ota_img_state_t;

/* Open the inactive slot for an image of `size` bytes (≤ the board's ota_max). */
gadget_status_t hal_ota_begin(uint32_t size);
/* Copy `len` bytes for `offset` into the port's write queue and return at
 * once. Offsets are contiguous. The port reports durable progress with
 * GADGET_EV_OTA_WRITTEN and failures with GADGET_EV_OTA_ERROR. The queue
 * holds at least 64 KiB + 4 KiB; BUSY when it is full. */
gadget_status_t hal_ota_write(uint32_t offset, const uint8_t *data, size_t len);
/* All bytes written: validate the image (ESP32: esp_ota_end). May block ≤ 2 s. */
gadget_status_t hal_ota_finalize(void);
/* Make the new slot the boot slot, in probation. `version` is recorded by
 * the simulator in otadata.json. Does not restart. */
gadget_status_t hal_ota_set_boot(const char *version);
void hal_ota_abort(void);
hal_ota_img_state_t hal_ota_running_state(void);
gadget_status_t hal_ota_mark_valid(void);               /* esp_ota_mark_app_valid_cancel_rollback */
_Noreturn void hal_ota_mark_invalid_and_reboot(void);   /* esp_ota_mark_app_invalid_rollback_and_reboot */

/* ---- Battery ------------------------------------------------------------ */
/* false when the board has no battery or none is fitted. Cheap; core polls it. */
bool hal_battery_read(gadget_battery_t *out);

/* ---- System, clock, log, console ---------------------------------------- */
typedef enum { GADGET_LOG_ERROR = 0, GADGET_LOG_WARN, GADGET_LOG_INFO, GADGET_LOG_DEBUG } gadget_log_level_t;

/* Monotonic milliseconds since boot. Core never calls it: core's only clock
 * is the argument of core_tick(). Ports use it to drive core_tick(). */
uint64_t hal_now_ms(void);
_Noreturn void hal_restart(void);    /* simulator: re-exec itself with --boot <n+1> (spec §5.7, contract §2.16) */
void hal_log(gadget_log_level_t level, const char *tag, const char *fmt, ...) GADGET_PRINTF(3, 4);
void hal_vlog(gadget_log_level_t level, const char *tag, const char *fmt, va_list ap);
/* `log off|on`: silences or restores all log output (ESP: esp_log_level_set("*", …)). */
void hal_log_set_enabled(bool enabled);
/* Write one line plus "\n" to the console output. Never silenced. Used for @omb lines. */
void hal_console_write(const char *line);

#endif /* GADGET_HAL_H */
```

Notes binding on implementers:

- **Crypto adapter** (`core/src/crypto_psa.c`, P2a) uses only `psa_generate_key`, `psa_import_key`, `psa_export_key`, `psa_export_public_key`, `psa_destroy_key`, `psa_sign_hash` with `PSA_ALG_DETERMINISTIC_ECDSA(PSA_ALG_SHA_256)`, `psa_verify_hash` with `PSA_ALG_ECDSA(PSA_ALG_SHA_256)`, `psa_hash_compute`, `psa_hash_setup/update/finish/abort` (multi-part SHA-256 for OTA; see §6 D2) and `psa_generate_random`. Volatile keys only (ESP-IDF disables PSA persistent storage); the 32-byte scalar lives in storage key `dev_key`. Key attributes: the device key pair (generated or imported from `dev_key`) has type `PSA_KEY_TYPE_ECC_KEY_PAIR(PSA_ECC_FAMILY_SECP_R1)`, 256 bits, usage `PSA_KEY_USAGE_SIGN_HASH | PSA_KEY_USAGE_EXPORT` and `psa_set_key_algorithm(&attr, PSA_ALG_DETERMINISTIC_ECDSA(PSA_ALG_SHA_256))`; imported public verify keys have type `PSA_KEY_TYPE_ECC_PUBLIC_KEY(PSA_ECC_FAMILY_SECP_R1)`, 256 bits, usage `PSA_KEY_USAGE_VERIFY_HASH` and `psa_set_key_algorithm(&attr, PSA_ALG_ECDSA(PSA_ALG_SHA_256))`. Without the algorithm in the policy, `psa_sign_hash` and `psa_verify_hash` return `PSA_ERROR_NOT_PERMITTED`. `hal_sha256_t` holds only a pointer (no size assertion against `psa_hash_operation_t`, whose size differs between mbedTLS builds and ESP-IDF's hardware drivers). DER↔raw conversion is our own (`gadget_der_*`). Never the legacy `mbedtls_ecdsa_*`, `mbedtls_ecp_*` or `mbedtls_sha256_*` APIs.
- **ESP32 WebSocket** (`esp_websocket_client`): `buffer_size = 16*1024 + 64`, `disable_auto_reconnect = true`, `subprotocol = "openmausbot-gadget.1"`; the port reassembles `payload_offset` pieces into whole messages before posting the event.
- **ESP32 OTA writes** run in a worker task fed by a queue of at least 68 KiB; `esp_ota_begin(part, OTA_WITH_SEQUENTIAL_WRITES, …)`.
- **`hal_spk_stop` without a codec mute** (P2c deviation 1, approved). The codec boards go silent at once through the ES8311 mute. The devkit's MAX98357A has no mute and no control bus, so up to one I2S DMA ring (`spk_latency_ms`, 60 ms: 6 descriptors × 240 frames at 24 kHz) still plays after a stop; the API is unchanged. `firmware/ports/esp32/boards/devkit/board.h` records the fix as a v2 note: wire the amplifier's SD (shutdown) pin to a free GPIO and give the devkit a mute that drives it.

### 2.6 `gadget_util.h`

```c
/* firmware/core/include/gadget_util.h */
/* SPDX-License-Identifier: Apache-2.0 */
/* Pure helpers shared by core, UI, ports and tests. No allocation. */
#ifndef GADGET_UTIL_H
#define GADGET_UTIL_H

#include "gadget_types.h"

/* Base64, RFC 4648 §4, standard alphabet, with padding. */
/* Writes a NUL-terminated string; returns its length, or 0 if cap is too small. */
size_t gadget_b64_encode(char *out, size_t cap, const uint8_t *in, size_t len);
/* Strict decode: rejects anything that does not re-encode to the same text
 * (missing or extra padding, non-zero pad bits, '-', '_', whitespace).
 * GADGET_ERR_PARSE or GADGET_ERR_LIMIT (cap too small). */
gadget_status_t gadget_b64_decode(const char *in, uint8_t *out, size_t cap, size_t *len);

/* Lowercase hex; out must hold 2*len + 1 bytes. */
void gadget_hex_encode(char *out, const uint8_t *in, size_t len);
gadget_status_t gadget_hex_decode(const char *in, uint8_t *out, size_t cap, size_t *len);

/* ECDSA P-256 signature conversion (spec §5.2: our own code, not mbedTLS'). */
/* raw = r||s, 32 bytes each, big-endian. DER = minimal canonical encoding. */
gadget_status_t gadget_der_from_raw(const uint8_t raw[64], uint8_t *der, size_t cap, size_t *der_len);
gadget_status_t gadget_der_to_raw(const uint8_t *der, size_t der_len, uint8_t raw[64]); /* PARSE on non-canonical or >32-byte integers */

/* "gad_" + first 16 lowercase hex chars of SHA-256(pub). out holds GADGET_ID_LEN + 1. */
gadget_status_t gadget_id_from_pubkey(const uint8_t pub[GADGET_PUBKEY_LEN], char out[GADGET_ID_LEN + 1]);
/* /^[0-9a-f]{32}$/ */
bool gadget_host_id_valid(const char *host_id);
/* /^\d{6}$/ */
bool gadget_pair_code_valid(const char *code);

/* UTF-8: copy src into dst (cap bytes incl. NUL), cutting on a code-point
 * boundary and ending with U+2026 "…" when cut. Returns bytes written. */
size_t gadget_utf8_copy(char *dst, size_t cap, const char *src);
/* Keep the END of src (reply tails), starting with "…" when cut. */
size_t gadget_utf8_copy_tail(char *dst, size_t cap, const char *src);
/* Number of code points; invalid sequences count one per byte. */
size_t gadget_utf8_len(const char *s);

/* xorshift32, the one PRNG core and UI use (spec §5.5 determinism):
 *   s ^= s << 13; s ^= s >> 17; s ^= s << 5;  seed 0 is replaced by 0x9E3779B9. */
typedef struct { uint32_t s; } gadget_prng_t;
void gadget_prng_seed(gadget_prng_t *p, uint32_t seed);
uint32_t gadget_prng_next(gadget_prng_t *p);
uint32_t gadget_prng_range(gadget_prng_t *p, uint32_t lo, uint32_t hi);  /* inclusive; lo when hi <= lo */

#endif /* GADGET_UTIL_H */
```

### 2.7 `gadget_core.h`

```c
/* firmware/core/include/gadget_core.h */
/* SPDX-License-Identifier: Apache-2.0 */
/* Core public API (spec §5.1, §5.2). Portable C11; single-threaded. */
#ifndef GADGET_CORE_H
#define GADGET_CORE_H

#include "gadget_board.h"
#include "gadget_events.h"
#include "gadget_ui_model.h"

/* Storage keys (namespace "gadget"; ≤ 15 bytes each). */
#define GADGET_KEY_DEV_KEY    "dev_key"     /* blob, 32-byte P-256 scalar */
#define GADGET_KEY_HOST_ID    "host_id"     /* str, 32 lowercase hex, written on ready */
#define GADGET_KEY_HOST_NAME  "host_name"   /* str, from challenge, written on ready */
#define GADGET_KEY_HOST_ADDR  "host_addr"   /* str "addr:port"; absent = host auto */
#define GADGET_KEY_PAIR_CODE  "pair_code"   /* str, 6 digits, cleared on ready / bad_code */
#define GADGET_KEY_WIFI_SSID  "wifi_ssid"   /* str */
#define GADGET_KEY_WIFI_PASS  "wifi_pass"   /* str ("" = open network) */
#define GADGET_KEY_NAME       "name"        /* str, ≤ 32 chars (console `name` or settings.name) */
#define GADGET_KEY_BOT_ID     "bot_id"      /* str, from ready/settings */
#define GADGET_KEY_BOT_NAME   "bot_name"    /* str, from ready/settings */
#define GADGET_KEY_SPEAK_PUSH "speak_push"  /* str "1"/"0", settings.speak_pushes */

typedef struct core_config {
  const gadget_board_t *board;   /* required */
  const char *fw_version;        /* required: PROJECT_VER, e.g. "1.1.0" or "0.0.0-dev" (≤ 32 bytes) */
  uint32_t prng_seed;            /* 0 = seed from hal_crypto_random(); headless sim passes --seed (default 1) */
  bool fail_probation;           /* test only (sim --fail-probation): ignore the first ready on a pending image */
  const char *default_name;      /* name used when storage has none; NULL = "Maus " + 4 hex of the id */
  uint32_t probation_ms;         /* test only (sim --probation-ms): probation length; 0 = GADGET_PROBATION_MS. ESP32 passes 0 */
} core_config_t;

typedef enum {
  CORE_PAIR_UNPAIRED = 0,   /* @omb pair "unpaired" */
  CORE_PAIR_CODE_STORED,    /* "code_stored" */
  CORE_PAIR_CONNECTING,     /* "connecting" */
  CORE_PAIR_PAIRED,         /* "paired": ready arrived on the current connection */
  CORE_PAIR_ERROR           /* "error": see core_last_error() */
} core_pair_state_t;

/* Load or create the identity (first boot: hal_crypto_keygen, then store
 * dev_key), read storage, register built-in actions ("chime"), start
 * probation if hal_ota_running_state() is PENDING_VERIFY, print the @omb
 * boot line. Call once, after the port initialized its HAL and called
 * psa_crypto_init(). */
gadget_status_t core_init(const core_config_t *cfg);
/* Deliver one event (main thread only). */
void core_event(const gadget_event_t *ev);
/* Run timers, reconnects, jitter-buffer feeding and screen selection.
 * now_ms is monotonic and is core's only clock. Call every 10 ms. */
void core_tick(uint64_t now_ms);
const ui_model_t *core_ui_model(void);
/* Tests and the simulator: free everything so core_init() can run again
 * (a simulated reboot inside one process). */
void core_deinit(void);

/* Introspection (console status, simulator `model`, tests). */
const char *core_device_id(void);              /* "gad_…" */
core_pair_state_t core_pair_state(void);
const char *core_last_error(void);             /* last handshake error code, "" when none */
const char *core_fw_version(void);

/* Protocol tap: called for every text frame crossing the socket, after
 * decode (rx) or before send (tx). Used by the simulator's `expect` and
 * --trace, and by tests. op is the frame's "op" string. */
typedef enum { CORE_TAP_RX = 0, CORE_TAP_TX } core_tap_dir_t;
typedef void (*core_tap_fn)(core_tap_dir_t dir, const char *op, const char *json, size_t len, void *ctx);
void core_set_tap(core_tap_fn fn, void *ctx);

#endif /* GADGET_CORE_H */
```

**Screen selection** (core decides; P2b only draws `screen`). With a ready session, the first match wins: Update (an OTA is receiving, verifying or restarting) → Listening (recording) → Ask (an ask is open) → Speaking (a speech stream for the current reply is playing) → Thinking (a turn is in flight and its reply text is empty) → Reply (reply text present: streaming, final or failed; returns to Idle after 20 s or a tap) → Image → Card → Idle. Without a ready session: Setup when the gadget was never paired (no stored `host_id`) or was revoked, otherwise Offline. A post toast overlays any screen; while it shows over a screen whose `maus` is not `UI_MAUS_NONE`, `maus` is `UI_MAUS_NOTIFYING`.

| Screen | `maus` |
|---|---|
| Boot, Ask, Card, Image, Update | `UI_MAUS_NONE` |
| Setup | `UI_MAUS_CURIOUS` |
| Offline | `UI_MAUS_SLEEPING` |
| Idle | `UI_MAUS_IDLE` |
| Listening | `UI_MAUS_LISTENING` |
| Thinking | `UI_MAUS_THINKING`, `UI_MAUS_WORKING` once `thinking.working` is non-empty |
| Speaking | `UI_MAUS_SPEAKING` (mouth from `speak_level`) |
| Reply | `UI_MAUS_IDLE`; `UI_MAUS_ALERTING` when `reply.failed` |

**Speaking level:** core computes the RMS of each 20 ms block it hands to `hal_spk_write`, delays it by `hal_spk_buffered_ms()`, and maps dBFS < −42 → 0, −42…−32 → 1, −32…−24 → 2, ≥ −24 → 3. Rising levels apply at once; falling levels drop one step per 60 ms; 0 when no stream plays. **Mic level:** RMS of the latest mic frame mapped linearly from −60 dBFS (0) to 0 dBFS (255).

### 2.8 `gadget_ui_model.h`

```c
/* firmware/core/include/gadget_ui_model.h */
/* SPDX-License-Identifier: Apache-2.0 */
/* What core publishes and the UI draws (spec §5.2, §5.5). Core owns the
 * one instance (core_ui_model()); the UI only reads it. All strings are
 * NUL-terminated UTF-8 already limited to the gadget charset (Latin-1 plus
 * U+2026 and U+2192); core truncates with gadget_utf8_copy(). */
#ifndef GADGET_UI_MODEL_H
#define GADGET_UI_MODEL_H

#include "gadget_types.h"

#define UI_ID_MAX 64          /* ask/card/image ids (host ids are ≤ 40 ASCII) */
#define UI_NAME_MAX 96        /* bot name, host name, device name */
#define UI_HEARD_MAX 512
#define UI_WORKING_MAX 256
#define UI_REPLY_MAX 8192     /* reply tail kept by core */
#define UI_REASON_MAX 160
#define UI_TITLE_MAX 192
#define UI_BODY_MAX 1536
#define UI_LABEL_MAX 64
#define UI_TOAST_MAX 512
#define UI_ASK_OPTIONS_MAX 4

typedef enum {
  UI_SCREEN_BOOT = 0,    /* before core_init() finishes */
  UI_SCREEN_SETUP,
  UI_SCREEN_OFFLINE,
  UI_SCREEN_IDLE,
  UI_SCREEN_LISTENING,
  UI_SCREEN_THINKING,
  UI_SCREEN_SPEAKING,
  UI_SCREEN_REPLY,
  UI_SCREEN_ASK,
  UI_SCREEN_CARD,
  UI_SCREEN_IMAGE,
  UI_SCREEN_UPDATE,
  UI_SCREEN__COUNT
} ui_screen_t;               /* script/`model` names: boot setup offline idle listening thinking speaking reply ask card image update */

typedef enum {
  UI_MAUS_NONE = 0,      /* text screens: ask, card, image, update */
  UI_MAUS_IDLE,
  UI_MAUS_LISTENING,
  UI_MAUS_THINKING,
  UI_MAUS_WORKING,
  UI_MAUS_SPEAKING,      /* gadget-only state, 3 open-mouth levels */
  UI_MAUS_SLEEPING,      /* offline */
  UI_MAUS_CURIOUS,       /* setup */
  UI_MAUS_NOTIFYING,     /* while a post toast shows over a Maus screen */
  UI_MAUS_ALERTING,      /* reply screen after a failed done */
  UI_MAUS__COUNT
} ui_maus_state_t;           /* names: none idle listening thinking working speaking sleeping curious notifying alerting */

typedef enum { UI_STYLE_NEUTRAL = 0, UI_STYLE_ALLOW, UI_STYLE_DENY } ui_option_style_t;

typedef enum {
  UI_SETUP_NEED_WIFI = 0,    /* no Wi-Fi stored */
  UI_SETUP_NEED_CODE,        /* Wi-Fi ok, never paired, no code stored */
  UI_SETUP_PAIRING,          /* code stored, connecting / waiting for ready */
  UI_SETUP_HOST_NOT_FOUND,   /* `host auto` found nothing (or several) */
  UI_SETUP_BAD_CODE,         /* last attempt: bad_code */
  UI_SETUP_DEVICE_LIMIT      /* last attempt: device_limit, retrying every 10 s */
} ui_setup_step_t;

typedef enum {
  UI_OFFLINE_WIFI_CONNECTING = 0,
  UI_OFFLINE_WIFI_FAILED,
  UI_OFFLINE_HOST_LOOKUP,        /* resolving MausBot over mDNS */
  UI_OFFLINE_HOST_UNREACHABLE,   /* connect failed or session dropped; retry_at_ms set */
  UI_OFFLINE_IN_USE_ELSEWHERE,   /* error replaced; no retry until reboot or TALK */
  UI_OFFLINE_PROTOCOL            /* proto_unsupported / bad_sig / bad host_id; retry_at_ms set */
} ui_offline_reason_t;

typedef enum { UI_UPDATE_RECEIVING = 0, UI_UPDATE_VERIFYING, UI_UPDATE_RESTARTING } ui_update_phase_t;
typedef enum { UI_POST_ROUTINE = 0, UI_POST_MESSAGE } ui_post_kind_t;

typedef struct {
  char id[UI_ID_MAX];
  char label[UI_LABEL_MAX];
  ui_option_style_t style;
  gadget_rect_t rect;         /* touch boards: from ui_layout_ask(); zeros on button boards */
} ui_ask_option_t;

typedef struct ui_model {
  uint32_t rev;               /* incremented on every change; ui_render() may skip an unchanged rev */
  uint64_t now_ms;            /* time of the last core_tick() */
  ui_screen_t screen;
  ui_maus_state_t maus;
  uint8_t speak_level;        /* 0–3: mouth level while speaking (0 = closed) */
  uint8_t mic_level;          /* 0–255: input level while listening */
  char device_id[GADGET_ID_LEN + 1];
  char device_name[UI_NAME_MAX];
  char bot_name[UI_NAME_MAX];   /* from ready/settings; "" before the first ready */
  char host_name[UI_NAME_MAX];  /* from challenge (live) or storage */

  struct { bool present; uint8_t pct; bool charging; } battery;

  struct {
    uint64_t started_ms;
    uint8_t countdown_s;      /* 5..1 during the last 5 s of the 60 s limit, else 0 */
  } listening;

  struct {
    char heard[UI_HEARD_MAX];
    char working[UI_WORKING_MAX];   /* "" when cleared */
  } thinking;

  struct {
    char text[UI_REPLY_MAX];  /* cumulative shaped reply (tail kept) */
    bool final;
    bool failed;              /* a failed done: show reason, maus alerting */
    char reason[UI_REASON_MAX];
    uint32_t speak_elapsed_ms;  /* audio played so far in the reply's speech stream */
    uint32_t speak_total_ms;    /* 0 until speak.end; then the stream's total */
  } reply;

  struct {
    char id[UI_ID_MAX];
    bool question;            /* kind question (false = permission) */
    char title[UI_TITLE_MAX];
    char body[UI_BODY_MAX];
    uint8_t n_options;
    ui_ask_option_t options[UI_ASK_OPTIONS_MAX];
    bool answerable;          /* false → "Answer on your computer or phone" */
    uint64_t locked_until_ms; /* presses before this are ignored (0.6 s) */
    int8_t chosen;            /* -1, or the option index sent in `answer` */
    uint8_t queued;           /* further asks waiting behind this one */
  } ask;

  struct {
    char id[UI_ID_MAX];
    char title[UI_TITLE_MAX];
    char body[UI_BODY_MAX];
    uint64_t expires_ms;      /* 0 = until dismissed */
  } card;

  struct {
    char id[UI_ID_MAX];
    uint16_t w, h;
    const uint16_t *pixels;   /* RGB565 little-endian, row-major, w*h; NULL until image.end; owned by core */
    uint32_t pixels_rev;      /* changes when pixels change */
    uint64_t expires_ms;
  } image;

  struct {
    bool visible;
    ui_post_kind_t kind;
    char bot_name[UI_NAME_MAX];
    char text[UI_TOAST_MAX];
    uint64_t until_ms;
  } toast;

  struct {
    ui_setup_step_t step;
    bool wifi_set;
    bool code_stored;
  } setup;

  struct {
    ui_offline_reason_t reason;
    char ssid[33];
    uint64_t retry_at_ms;     /* 0 = no automatic retry */
  } offline;

  struct {
    ui_update_phase_t phase;
    uint8_t pct;              /* 0–100 */
    char version[GADGET_VERSION_MAX + 1];
  } update;
} ui_model_t;

#endif /* GADGET_UI_MODEL_H */
```

### 2.9 `gadget_ui.h`

```c
/* firmware/core/include/gadget_ui.h */
/* SPDX-License-Identifier: Apache-2.0 */
/* The UI API. Implemented by firmware/ui (LVGL, P2b) and by
 * ports/sim/ui_stub.c (no LVGL, P2a). Called by the port's main loop only:
 *
 *   core_tick(now); ui_render(core_ui_model()); ui_tick(now);
 *
 * Layout and hit-testing are pure functions implemented in core
 * (core/src/ui_layout.c) so core can hit-test touches without LVGL and the
 * UI places its buttons on exactly the same rectangles. */
#ifndef GADGET_UI_H
#define GADGET_UI_H

#include "gadget_board.h"
#include "gadget_ui_model.h"

/* Port has already called lv_init() and created the display (and, for
 * touch, the pointer indev). Builds every screen on lv_screen_active() and
 * sets the Latin-1 fonts through styles on the UI root. */
gadget_status_t ui_init(const gadget_board_t *board, uint32_t prng_seed);
/* Apply the model. Cheap when m->rev is unchanged. Never calls into core. */
void ui_render(const ui_model_t *m);
/* Advance the Maus animation (expressions, blinks, bob/jitter, mouth) to
 * now_ms, then call lv_timer_handler() exactly once. */
void ui_tick(uint64_t now_ms);
void ui_deinit(void);

/* ---- Implemented in core (core/src/ui_layout.c) --------------------------- */
/* Button rectangles for an ask with n options (1..4) on a touch board,
 * inside the round safe area when board->screen_round. */
void ui_layout_ask(const gadget_board_t *board, uint8_t n_options, gadget_rect_t out[UI_ASK_OPTIONS_MAX]);
/* Option index (0..n-1) under (x, y) on the ask screen, or -1. */
int ui_hit_test(const ui_model_t *m, int16_t x, int16_t y);
/* The text-safe rectangle (inscribed square on round screens, minus margins). */
gadget_rect_t ui_safe_area(const gadget_board_t *board);

#endif /* GADGET_UI_H */
```

Hit-testing rules (core, P2a): on touch boards a touch-up inside `options[i].rect` after `locked_until_ms` answers option `i`. On button boards TALK answers option 0 and CANCEL option 1 when `answerable`; an ask is `answerable` when it has 1–2 options on a button board or 1–4 on a touch board. A one-option ask maps only TALK.

### 2.10 `gadget_actions.h`

```c
/* firmware/core/include/gadget_actions.h */
/* SPDX-License-Identifier: Apache-2.0 */
/* Actions a gadget declares in hello and runs on `act` (spec §4.7, §5.10). */
#ifndef GADGET_ACTIONS_H
#define GADGET_ACTIONS_H

#include "cJSON.h"
#include "gadget_types.h"

typedef enum {
  GADGET_RISK_CONFIRM = 0,   /* "confirm": MausBot always asks the person first */
  GADGET_RISK_SAFE = 1       /* "safe" */
} gadget_risk_t;

/* Runs on the main thread when an `act` for this action arrives. Fill
 * `data` (an empty cJSON object owned by core; left empty → "data" omitted)
 * and return true, or write a short reason into `error` (≤ error_cap - 1
 * bytes, Latin-1) and return false. Must not block for more than 100 ms.
 * Core sends exactly one act.result per act. */
typedef bool (*gadget_action_handler_t)(const cJSON *args, cJSON *data, char *error, size_t error_cap);

/* Declare an action. Call after core_init() and before the first
 * core_tick(); an action registered later is declared in the next hello.
 *   name:        /^[a-z][a-z0-9_.-]{0,31}$/
 *   description: 1..200 characters
 *   params_schema_json: a JSON Schema object as text (≤ 1024 bytes when
 *                re-serialized); NULL → {"type":"object","properties":{}}
 *   risk:        GADGET_RISK_SAFE or GADGET_RISK_CONFIRM
 * Returns GADGET_OK, GADGET_ERR_ARG (bad name, invalid JSON, NULL handler),
 * GADGET_ERR_STATE (duplicate name) or GADGET_ERR_LIMIT (more than 16
 * actions, a field too long, or the hello would exceed 16 KiB). */
gadget_status_t gadget_action_register(const char *name, const char *description, const char *params_schema_json,
                                       gadget_risk_t risk, gadget_action_handler_t handler);

/* Tell MausBot that something happened on the gadget: sends
 * `event {name, data?}` (spec §4.7). Main thread only. Events are
 * informational: MausBot keeps the last 10 per gadget and lists them to
 * bots as `recent_events`, and nothing reacts to them. Nothing is queued,
 * so an event sent while busy is lost.
 *   name: /^[a-z][a-z0-9_.-]{0,31}$/ (the action-name rule)
 *   data: any JSON value (copied; the caller keeps ownership), ≤ 1024
 *         bytes when serialized compactly; NULL → "data" omitted
 * Returns GADGET_OK, GADGET_ERR_ARG (bad name), GADGET_ERR_LIMIT (data too
 * long), GADGET_ERR_NO_MEM, GADGET_ERR_BUSY (no ready session: offline,
 * mid-handshake or before core_init()) or the socket's error. The name and
 * size checks come first, so they fail the same way offline. */
gadget_status_t gadget_event_send(const char *name, const cJSON *data);

#endif /* GADGET_ACTIONS_H */
```

Built-in action registered by `core_init()`: `chime`, description `Play a short chime.`, params `{"type":"object","properties":{}}`, risk `safe`; it plays a 300 ms two-tone chime when the board has a speaker and returns `{}`. An `act` for an unknown name gets `act.result {id, ok: false, error: "unknown action"}`.

**`gadget_event_send` (P2a deviation 9; adopted only if Omkar accepts it).** It sends `event` while the session is ready and returns `GADGET_ERR_BUSY` otherwise; the name follows the action-name regex; `data` is at most 1 KiB serialized. P2a implements it in `core/src/actions.c` with its test in `test_actions.c`; once it is accepted, P2d's `AGENTS.md` may document it next to "add an action" (today it does not mention events). The declaration above is byte-identical to P2a's header. **If Omkar declines:** delete the declaration and this paragraph, record §6 D29 as "`event` is never sent in v1" (core never sends `event`; `gp_encode_event` stays for the codec and its tests; `AGENTS.md` does not document events), and drop `recent_events` from §3.15's `GadgetDirectoryEntry` and from the `GET /api/internal/gadgets` row. **If he accepts:** delete §6 D29 and the words "adopted only if Omkar accepts it" above.

### 2.11 Console: `gadget_console.h`, grammar and `@omb` lines

```c
/* firmware/core/include/gadget_console.h */
/* SPDX-License-Identifier: Apache-2.0 */
/* Console line handling shared by both ports (spec §5.6). */
#ifndef GADGET_CONSOLE_H
#define GADGET_CONSOLE_H

#include "gadget_types.h"

/* Bytes incl. NUL; longer lines are rejected. Sized for the longest valid
 * line: `say` + a quoted 2000-code-point text at up to 4 UTF-8 bytes per
 * code point (escapes are 2 bytes for 1-byte characters) = 8007 bytes. */
#define GADGET_CONSOLE_LINE_MAX 8192u
#define GADGET_CONSOLE_ARGV_MAX 8u

/* Line assembler: feed raw console bytes; calls on_line for each complete
 * line. A line ends at CR, LF or CRLF (a CR followed by LF is one ending).
 * An over-long line is dropped up to its ending and reported as
 * on_line(NULL). Ports post each line as GADGET_EV_CONSOLE_LINE. */
typedef struct {
  char buf[GADGET_CONSOLE_LINE_MAX];
  size_t len;
  bool overflow;
  bool last_cr;
} gadget_linebuf_t;
typedef void (*gadget_line_fn)(const char *line, void *ctx);
void gadget_linebuf_init(gadget_linebuf_t *lb);
void gadget_linebuf_feed(gadget_linebuf_t *lb, const char *data, size_t n, gadget_line_fn on_line, void *ctx);

/* esp_console_split_argv rules: whitespace separates arguments; "…" groups;
 * inside or outside quotes, \\ → \, \" → ", "\ " → space. Splits in place.
 * Returns argc, or -1 for an unterminated quote. */
int gadget_console_split(char *line, char *argv[], int argv_max);

typedef enum {
  GC_EMPTY = 0, GC_WIFI, GC_SCAN, GC_HOST_AUTO, GC_HOST_SET, GC_PAIR, GC_NAME, GC_SAY,
  GC_STATUS, GC_LOG_OFF, GC_LOG_ON, GC_FORGET, GC_REBOOT,
  GC_UNKNOWN,    /* not a command → @omb error */
  GC_BAD_ARGS    /* a command with invalid arguments → @omb error */
} gadget_console_cmd_t;

typedef struct {
  gadget_console_cmd_t cmd;
  const char *cmd_name;   /* argv[0], for the error line */
  const char *a;          /* wifi ssid | host address | pair code | name | say text */
  const char *b;          /* wifi password */
  uint16_t port;          /* host <address>[:port]; default GADGET_DEFAULT_PORT */
  char error[96];         /* GC_UNKNOWN / GC_BAD_ARGS: Latin-1 message */
} gadget_console_parsed_t;

/* Parse one line (modified in place; out's pointers point into it). */
gadget_console_cmd_t gadget_console_parse(char *line, gadget_console_parsed_t *out);

#endif /* GADGET_CONSOLE_H */
```

**Grammar** (commands are lowercase and case-sensitive; arguments use the quoting above; an empty line is ignored):

| Line | Arguments | Effect |
|---|---|---|
| `wifi <ssid> <password>` | ssid 1–32 bytes; password `""` (open) or 8–63 bytes, or 64 hex | store `wifi_ssid`/`wifi_pass`, `hal_wifi_connect` |
| `scan` | — | `hal_wifi_scan`, then one `@omb scan` line |
| `host auto` | — | erase `host_addr`; resolve over mDNS once Wi-Fi is up (spec §5.6) |
| `host <address>[:port]` | hostname or IPv4, port 1–65535 (default 8810) | store `host_addr` as `addr:port`; reconnect |
| `pair <code>` | exactly 6 digits | store `pair_code`; reconnect at once |
| `name <text>` | 1–32 characters after trimming | store `name`; next hello carries it |
| `say <text>` | 1–2000 characters (code points; the line limit above fits the longest) | send `say`; print `@omb say` |
| `status` | — | print `@omb status` |
| `log off` / `log on` | — | `hal_log_set_enabled`; never hides `@omb` lines; lasts until reboot |
| `forget` | — | `hal_storage_erase_all()`, then `hal_restart()`. Nothing of the old identity survives in memory; the new key is generated at boot, so a later `pair` enrolls as a new id |
| `reboot` | — | `hal_restart()` |

**`@omb` lines.** Exactly `@omb ` (with one space) followed by one compact JSON object on one line. Field order below is the order written; parsers must not depend on it. Optional fields are omitted, never `null`.

```
@omb {"op":"boot","board":"amoled-175c","fw":"1.0.0","id":"gad_3f9a0c2b7e41d856"}
@omb {"op":"status","wifi":"connected","ssid":"Home","host":"192.168.1.20:8810","id":"gad_3f9a0c2b7e41d856","pair":"paired","fw":"1.0.0","battery":{"pct":82,"charging":false},"board":"amoled-175c","name":"Desk Maus","host_name":"Omkar's computer"}
@omb {"op":"status","wifi":"connecting","ssid":"Home","id":"gad_3f9a0c2b7e41d856","pair":"error","error":"bad_code","fw":"1.0.0","board":"lcd-154","name":"Maus 3f9a"}
@omb {"op":"scan","networks":[{"ssid":"Home","rssi":-52,"auth":"wpa2"},{"ssid":"Cafe","rssi":-80,"auth":"open"}]}
@omb {"op":"hosts","hosts":[{"name":"Omkar's computer","address":"192.168.1.20:8810","id":"0123456789abcdef0123456789abcdef"}]}
@omb {"op":"say","turn":"t3f9a0c2b-7"}
@omb {"op":"error","cmd":"pair","message":"pair needs a six-digit code"}
@omb {"op":"error","cmd":"say","message":"not connected to MausBot"}
@omb {"op":"error","cmd":"","message":"line too long"}
```

| Field | Values |
|---|---|
| `wifi` | `off`, `connecting`, `connected`, `failed` |
| `ssid` | present when a network is stored |
| `host` | `addr:port` of the host in use; omitted when unknown |
| `pair` | `unpaired`, `code_stored`, `connecting`, `paired`, `error` (definitions in spec §5.6) |
| `error` | only with `pair: "error"`: the last handshake error code |
| `battery` | omitted on boards without a battery |
| `board`, `name`, `host_name` | contract additions (§6 D3); `host_name` omitted when unknown |
| `scan.networks` | ≤ 20, strongest first, de-duplicated by ssid; `auth` ∈ `open wep wpa wpa2 wpa3 wpa2-ent other` |
| `hosts[].address` | `a.b.c.d:port`; `id` is the TXT `id=` value or `""` |
| `boot` | printed once by `core_init()` after the console is up (contract addition, lets the installer detect the app) |
| `error` op | printed for `GC_UNKNOWN` and `GC_BAD_ARGS`, for `say` without a ready session (`cmd` `say`, `message` `not connected to MausBot`) and for an over-long line (`gadget_linebuf_feed` reports `on_line(NULL)`; `cmd` `""`, `message` `line too long`) (contract additions, §6 D3) |

**Status rules** (what the next `status` shows after a command or during a retry; P2a implements them, P2d's installer relies on them):

1. **After an accepted `pair <code>`** the next `status` shows `pair` `code_stored` or `connecting`, with no `error` field, even when the previous attempt ended in `bad_code` (P2a already does this). After an accepted `wifi`, the next `status` shows `wifi` `connecting` (or `connected`; §2.5 `hal_wifi_connect`).
2. **`device_limit` retries.** While the gadget keeps the code and retries (spec §4.3: every 10 s for 120 s), `status` shows `"pair":"error","error":"device_limit"`; when the 120 s window closes it shows `unpaired`.
3. **Host not found (§6 D28).** A paired gadget (a stored `host_id`) whose MausBot is not among the services found keeps browsing with the §4.3 backoff, because its stored `host_id` picks its own MausBot as soon as it appears. It prints the `hosts` line only on the first miss of a run of misses; `ready`, `pair` and `host` start a new run, so the installer prompts once. A gadget that was never paired prints `hosts` and waits for `host <address>` (spec §5.6).
4. **`@omb error` lines** besides `GC_UNKNOWN`/`GC_BAD_ARGS`: `{"op":"error","cmd":"say","message":"not connected to MausBot"}` for `say` without a ready session, and `{"op":"error","cmd":"","message":"line too long"}` for a line longer than `GADGET_CONSOLE_LINE_MAX` (both ports print it; §6 D3).

The installer and tests read only lines that start with `@omb `, strip ANSI colour codes first, and ignore everything else.

### 2.12 Protocol codec: `gadget_proto.h` and the op table

```c
/* firmware/core/include/gadget_proto.h */
/* SPDX-License-Identifier: Apache-2.0 */
/* Protocol codec for openmausbot-gadget/1 (protocol/PROTOCOL.md). Core-
 * internal API, public for tests. Decoded strings point into the cJSON tree
 * owned by gp_msg_t and live until gp_msg_free(). Optional fields that are
 * absent decode as NULL (strings), false/0 (numbers) with has_* flags where
 * absence matters. Encoders write compact JSON, omit absent optional fields,
 * never write null, and return the length written (excluding NUL) or a
 * negative gadget_status_t. */
#ifndef GADGET_PROTO_H
#define GADGET_PROTO_H

#include "cJSON.h"
#include "gadget_actions.h"
#include "gadget_board.h"
#include "gadget_types.h"

typedef enum {
  GP_OP_UNKNOWN = 0,
  /* host → gadget */
  GP_OP_CHALLENGE, GP_OP_READY, GP_OP_ERROR, GP_OP_SETTINGS,
  GP_OP_HEARD, GP_OP_WORKING, GP_OP_REPLY, GP_OP_DONE,
  GP_OP_SPEAK_BEGIN, GP_OP_SPEAK_END, GP_OP_SPEAK_STOP,
  GP_OP_ASK, GP_OP_ASK_CLOSE, GP_OP_POST,
  GP_OP_CARD, GP_OP_CARD_CLOSE, GP_OP_IMAGE_BEGIN, GP_OP_IMAGE_END,
  GP_OP_ACT, GP_OP_FW_OFFER, GP_OP_FW_COMMIT,
  /* gadget → host */
  GP_OP_HELLO, GP_OP_PROVE, GP_OP_VOICE_BEGIN, GP_OP_VOICE_END, GP_OP_VOICE_DROP,
  GP_OP_SAY, GP_OP_STOP, GP_OP_ANSWER, GP_OP_ACT_RESULT, GP_OP_SENSE, GP_OP_EVENT,
  GP_OP_FW_READY, GP_OP_FW_FAIL, GP_OP_FW_PROGRESS, GP_OP_FW_INSTALLED,
  GP_OP__COUNT
} gp_op_t;

const char *gp_op_name(gp_op_t op);          /* "fw.offer"; NULL for UNKNOWN */
gp_op_t gp_op_from_name(const char *name);   /* GP_OP_UNKNOWN when not listed */

typedef enum { GP_STYLE_NEUTRAL = 0, GP_STYLE_ALLOW, GP_STYLE_DENY } gp_style_t;
typedef enum { GP_ASK_PERMISSION = 0, GP_ASK_QUESTION } gp_ask_kind_t;
typedef enum { GP_OUTCOME_OK = 0, GP_OUTCOME_FAILED, GP_OUTCOME_STOPPED } gp_outcome_t;
typedef enum { GP_POST_ROUTINE = 0, GP_POST_MESSAGE } gp_post_kind_t;

/* ---- host → gadget (decoded) -------------------------------------------- */
typedef struct { const char *id; const char *name; } gp_bot_t;
typedef struct { bool speak_pushes; } gp_settings_values_t;
typedef struct { const char *nonce; const char *host_id; const char *host_name; } gp_challenge_t;
typedef struct { const char *session; gp_bot_t bot; gp_settings_values_t settings; } gp_ready_t;
typedef struct { const char *code; const char *message; } gp_error_t;
typedef struct { bool has_bot; gp_bot_t bot; bool has_settings; gp_settings_values_t settings; const char *name; } gp_settings_t;
typedef struct { const char *turn; const char *text; } gp_heard_t;
typedef struct { const char *turn; const char *text; } gp_working_t;
typedef struct { const char *turn; const char *text; bool final; } gp_reply_t;
typedef struct { const char *turn; gp_outcome_t outcome; const char *reason; } gp_done_t;
typedef struct { uint8_t stream; uint32_t rate; const char *turn; } gp_speak_begin_t;
typedef struct { uint8_t stream; } gp_speak_end_t;
typedef struct { uint8_t stream; } gp_speak_stop_t;
typedef struct { const char *id; const char *label; gp_style_t style; } gp_option_t;
typedef struct {
  const char *id; gp_ask_kind_t kind; const char *title; const char *body;
  uint8_t n_options; gp_option_t options[4]; uint32_t expires_s; /* 0 = absent */
} gp_ask_t;
typedef struct { const char *id; const char *reason; /* answered | expired | withdrawn */ } gp_ask_close_t;
typedef struct { const char *id; gp_bot_t bot; gp_post_kind_t kind; const char *text; bool speak; } gp_post_t;
typedef struct { const char *id; const char *title; const char *body; uint32_t ttl_s; } gp_card_t;
typedef struct { const char *id; } gp_card_close_t;
typedef struct { const char *id; uint8_t stream; uint16_t w, h; uint32_t ttl_s; } gp_image_begin_t;
typedef struct { uint8_t stream; } gp_image_end_t;
typedef struct { const char *id; const char *name; const cJSON *args; /* object; NULL → {} */ } gp_act_t;
typedef struct {
  uint8_t stream; const char *board; const char *version; uint32_t size;
  const char *sha256; const char *sig; const char *key_id;
} gp_fw_offer_t;
typedef struct { uint8_t stream; } gp_fw_commit_t;

typedef struct gp_msg {
  gp_op_t op;
  union {
    gp_challenge_t challenge; gp_ready_t ready; gp_error_t error; gp_settings_t settings;
    gp_heard_t heard; gp_working_t working; gp_reply_t reply; gp_done_t done;
    gp_speak_begin_t speak_begin; gp_speak_end_t speak_end; gp_speak_stop_t speak_stop;
    gp_ask_t ask; gp_ask_close_t ask_close; gp_post_t post;
    gp_card_t card; gp_card_close_t card_close; gp_image_begin_t image_begin; gp_image_end_t image_end;
    gp_act_t act; gp_fw_offer_t fw_offer; gp_fw_commit_t fw_commit;
  } m;
  cJSON *root;   /* owns every string above */
} gp_msg_t;

/* Decode one host → gadget text frame. GADGET_OK with op == GP_OP_UNKNOWN
 * for an unknown op (caller ignores it); GADGET_ERR_PARSE for invalid JSON,
 * a missing "op", or a known op missing a required field. Unknown fields
 * are ignored. */
gadget_status_t gp_decode(const char *json, size_t len, gp_msg_t *out);
void gp_msg_free(gp_msg_t *m);

/* ---- gadget → host (encoded) -------------------------------------------- */
typedef struct {
  const char *name; const char *description; const char *params_json; gadget_risk_t risk;
} gp_action_decl_t;
typedef struct {
  const char *id; const char *pubkey_b64; const char *name; const char *fw;
  const gadget_board_t *board;           /* board, caps */
  const gp_action_decl_t *actions; uint8_t n_actions;
  bool battery_valid; uint8_t battery_pct; bool charging;   /* sensors */
} gp_hello_t;
typedef struct { const char *sig_b64; const char *enroll; /* NULL = omitted */ } gp_prove_t;
typedef struct { const char *turn; uint8_t stream; uint32_t rate; } gp_voice_begin_t;
typedef struct { const char *turn; uint32_t ms; } gp_voice_end_t;
typedef struct { const char *turn; } gp_voice_drop_t;
typedef struct { const char *turn; const char *text; } gp_say_t;
typedef struct { const char *turn; /* NULL = omitted */ } gp_stop_t;
typedef struct { const char *id; const char *option; } gp_answer_t;
typedef struct { const char *id; bool ok; const cJSON *data; const char *error; } gp_act_result_t;
typedef struct { bool battery_valid; uint8_t battery_pct; bool charging; } gp_sense_t;
typedef struct { const char *name; const cJSON *data; } gp_event_msg_t;
typedef struct { uint8_t stream; } gp_fw_ready_t;
typedef struct { uint8_t stream; const char *code; } gp_fw_fail_t;
typedef struct { uint8_t stream; uint32_t offset; } gp_fw_progress_t;
typedef struct { const char *version; } gp_fw_installed_t;

int gp_encode_hello(char *buf, size_t cap, const gp_hello_t *m);            /* cap ≥ GADGET_TEXT_FRAME_MAX */
int gp_encode_prove(char *buf, size_t cap, const gp_prove_t *m);
int gp_encode_voice_begin(char *buf, size_t cap, const gp_voice_begin_t *m);
int gp_encode_voice_end(char *buf, size_t cap, const gp_voice_end_t *m);
int gp_encode_voice_drop(char *buf, size_t cap, const gp_voice_drop_t *m);
int gp_encode_say(char *buf, size_t cap, const gp_say_t *m);
int gp_encode_stop(char *buf, size_t cap, const gp_stop_t *m);
int gp_encode_answer(char *buf, size_t cap, const gp_answer_t *m);
int gp_encode_act_result(char *buf, size_t cap, const gp_act_result_t *m);
int gp_encode_sense(char *buf, size_t cap, const gp_sense_t *m);
int gp_encode_event(char *buf, size_t cap, const gp_event_msg_t *m);
int gp_encode_fw_ready(char *buf, size_t cap, const gp_fw_ready_t *m);
int gp_encode_fw_fail(char *buf, size_t cap, const gp_fw_fail_t *m);
int gp_encode_fw_progress(char *buf, size_t cap, const gp_fw_progress_t *m);
int gp_encode_fw_installed(char *buf, size_t cap, const gp_fw_installed_t *m);

/* ---- binary frames (PROTOCOL.md §4.1) ----------------------------------- */
typedef enum { GP_BIN_MIC = 0x01, GP_BIN_SPEAKER = 0x02, GP_BIN_IMAGE = 0x03, GP_BIN_FIRMWARE = 0x04 } gp_bin_kind_t;
/* Writes [kind][stream][payload]; returns 2 + len, or 0 if cap is too small or the frame > 8 KiB. */
size_t gp_bin_encode(uint8_t *out, size_t cap, gp_bin_kind_t kind, uint8_t stream, const uint8_t *payload, size_t len);
gadget_status_t gp_bin_decode(const uint8_t *frame, size_t len, gp_bin_kind_t *kind, uint8_t *stream,
                              const uint8_t **payload, size_t *payload_len);   /* PARSE: len < 2, stream 0, unknown kind */
/* Firmware payload: u32 LE offset, then 1..4096 bytes. */
gadget_status_t gp_fw_chunk_decode(const uint8_t *payload, size_t len, uint32_t *offset,
                                   const uint8_t **data, size_t *data_len);

/* ---- signed texts (PROTOCOL.md §4.3 prove, §4.8 firmware) --------------- */
/* "openmausbot-gadget/1\nprove\n<id>\n<nonce_b64>\n<host_id>", no trailing newline. */
int gp_prove_text(char *out, size_t cap, const char *id, const char *nonce_b64, const char *host_id);
/* "openmausbot-gadget/1\nfirmware\n<board>\n<version>\n<size>\n<sha256 hex>", size base-10 without leading zeros. */
int gp_firmware_text(char *out, size_t cap, const char *board, const char *version, uint32_t size, const char *sha256_hex);

#endif /* GADGET_PROTO_H */
```

**Every op with its C and TS type names.** Semantics are in PROTOCOL.md (spec §4); TS names are the same in `tools/fake-host` (P1, `protocol/lib/types.ts`) and in the app's `companion/src/gadget/protocol.ts` (P3a).

| op | Direction | C type | TS type | PROTOCOL.md |
|---|---|---|---|---|
| `hello` | g→h | `gp_hello_t` | `HelloMsg` | §4.3 |
| `challenge` | h→g | `gp_challenge_t` | `ChallengeMsg` | §4.3 |
| `prove` | g→h | `gp_prove_t` | `ProveMsg` | §4.3 |
| `ready` | h→g | `gp_ready_t` | `ReadyMsg` | §4.3 |
| `error` | h→g | `gp_error_t` | `ErrorMsg` | §4.3 |
| `settings` | h→g | `gp_settings_t` | `SettingsMsg` | §4.3 |
| `voice.begin` | g→h | `gp_voice_begin_t` | `VoiceBeginMsg` | §4.4 |
| `voice.end` | g→h | `gp_voice_end_t` | `VoiceEndMsg` | §4.4 |
| `voice.drop` | g→h | `gp_voice_drop_t` | `VoiceDropMsg` | §4.4 |
| `say` | g→h | `gp_say_t` | `SayMsg` | §4.4 |
| `stop` | g→h | `gp_stop_t` | `StopMsg` | §4.4 |
| `heard` | h→g | `gp_heard_t` | `HeardMsg` | §4.4 |
| `working` | h→g | `gp_working_t` | `WorkingMsg` | §4.4 |
| `reply` | h→g | `gp_reply_t` | `ReplyMsg` | §4.4 |
| `done` | h→g | `gp_done_t` | `DoneMsg` | §4.4 |
| `speak.begin` | h→g | `gp_speak_begin_t` | `SpeakBeginMsg` | §4.4 |
| `speak.end` | h→g | `gp_speak_end_t` | `SpeakEndMsg` | §4.4 |
| `speak.stop` | h→g | `gp_speak_stop_t` | `SpeakStopMsg` | §4.4 |
| `ask` | h→g | `gp_ask_t` | `AskMsg` | §4.5 |
| `answer` | g→h | `gp_answer_t` | `AnswerMsg` | §4.5 |
| `ask.close` | h→g | `gp_ask_close_t` | `AskCloseMsg` | §4.5 |
| `post` | h→g | `gp_post_t` | `PostMsg` | §4.6 |
| `card` | h→g | `gp_card_t` | `CardMsg` | §4.7 |
| `card.close` | h→g | `gp_card_close_t` | `CardCloseMsg` | §4.7 |
| `image.begin` | h→g | `gp_image_begin_t` | `ImageBeginMsg` | §4.7 |
| `image.end` | h→g | `gp_image_end_t` | `ImageEndMsg` | §4.7 |
| `act` | h→g | `gp_act_t` | `ActMsg` | §4.7 |
| `act.result` | g→h | `gp_act_result_t` | `ActResultMsg` | §4.7 |
| `sense` | g→h | `gp_sense_t` | `SenseMsg` | §4.7 |
| `event` | g→h | `gp_event_msg_t` | `EventMsg` | §4.7 |
| `fw.offer` | h→g | `gp_fw_offer_t` | `FwOfferMsg` | §4.8 |
| `fw.ready` | g→h | `gp_fw_ready_t` | `FwReadyMsg` | §4.8 |
| `fw.fail` | g→h | `gp_fw_fail_t` | `FwFailMsg` | §4.8 |
| `fw.progress` | g→h | `gp_fw_progress_t` | `FwProgressMsg` | §4.8 |
| `fw.commit` | h→g | `gp_fw_commit_t` | `FwCommitMsg` | §4.8 |
| `fw.installed` | g→h | `gp_fw_installed_t` | `FwInstalledMsg` | §4.8 |
| binary 0x01 mic | g→h | `GP_BIN_MIC` | `BinaryKind.mic` | §4.1 |
| binary 0x02 speaker | h→g | `GP_BIN_SPEAKER` | `BinaryKind.speaker` | §4.1 |
| binary 0x03 image rows | h→g | `GP_BIN_IMAGE` | `BinaryKind.image` | §4.1 |
| binary 0x04 firmware | h→g | `GP_BIN_FIRMWARE` | `BinaryKind.firmware` | §4.1 |

Pinned details the spec leaves open (PROTOCOL.md states them; P1 owns the text):

- **Frame sizes on the wire:** mic frames carry 320 samples (640 bytes payload). Speaker frames carry 40 ms: 640 samples at 16 kHz, 960 at 24 kHz. Image row frames carry ≤ 8190 bytes of payload and may split a row. Firmware frames carry a 4-byte offset and ≤ 4096 bytes.
- **Streams:** gadget-assigned streams (`voice.begin`) and host-assigned streams (`speak.begin`, `image.begin`, `fw.offer`) are separate spaces; each side cycles 1→255→1 and never reuses an id that is still active.
- **`turn`:** `t` + 8 lowercase hex chosen at boot with `hal_crypto_random` + `-` + a decimal counter starting at 1 (`t3f9a0c2b-7`).
- **Host ids:** every id the host sends (`ask.id`, `card.id`, `image.id`, `act.id`) is ≤ 40 ASCII characters from `[A-Za-z0-9_.:-]`. `ready.session` is `s_` + 12 lowercase hex. The app hub never passes engine ids through: `ask.id = "a_" + sha256(threadId + ":" + requestId).hex.slice(0, 16)` (18 characters; card `requestId`s come from engines or `newId()` and have no length or charset guarantee), and the hub keeps a map `ask.id → {threadId, requestId}` to resolve `answer` (§3.11).
- **Default name:** a gadget with no stored name sends `"Maus " + <first 4 hex of its id after gad_>` (for example `Maus b18b`).
- **`sense`:** sent when `battery_pct` changes by ≥ 1 or `charging` flips, at most once per 10 s; boards without a battery never send it and send `"sensors": {}` in `hello`.
- **Unbound bot:** a gadget whose record has no bot gets `ready.bot = {"id": "", "name": ""}`; a turn then ends with `done {outcome: "failed", reason: "Pick a bot for this gadget in MausBot → Settings → Remote access"}`.
- **After `error`:** the host closes with WebSocket close code 1000. The gadget reacts to the `error` op (spec §4.3 table), not to the close code.

### 2.13 OTA: `gadget_ota.h` and key tables

```c
/* firmware/core/include/gadget_ota.h */
/* SPDX-License-Identifier: Apache-2.0 */
/* Firmware update state machine and key tables (spec §4.8, §8). Core-
 * internal, public for tests and for release CI's key check. */
#ifndef GADGET_OTA_H
#define GADGET_OTA_H

#include "gadget_types.h"

typedef struct {
  const char *id;                      /* "r1", "r2", … (release) or "t1" (test) */
  uint8_t pub[GADGET_PUBKEY_LEN];      /* SEC1 uncompressed */
} gadget_release_key_t;

/* Every key table has at least one element (C11 has no empty initializer
 * or zero-length array). An empty table is exactly
 *   const gadget_release_key_t gadget_release_keys[] = {{NULL, {0}}};
 *   const size_t gadget_release_keys_count = 0;
 * (the same for gadget_test_keys). Code loops i < *_count and never uses
 * sizeof on a table. */
/* core/src/keys_release.c: release keys only (ids /^r[0-9]+$/). Empty until
 * Omkar commits keys/release-r1.pub.b64; P2d fills it in. */
extern const gadget_release_key_t gadget_release_keys[];
extern const size_t gadget_release_keys_count;
/* core/src/keys_test.c: ALWAYS compiled. Holds t1 under
 * #if defined(GADGET_TEST_KEYS), otherwise the empty table above (count 0).
 * GADGET_TEST_KEYS is a private compile definition on the core library
 * (§2.17 ESP, §2.18 desktop); core sources never read sdkconfig.h. */
extern const gadget_release_key_t gadget_test_keys[];
extern const size_t gadget_test_keys_count;
/* Release table first, then the test table (count 0 unless GADGET_TEST_KEYS).
 * Skips entries whose id is NULL. NULL if unknown. */
const gadget_release_key_t *gadget_key_find(const char *key_id);

typedef enum {
  GADGET_OTA_IDLE = 0,
  GADGET_OTA_RECEIVING,     /* fw.ready sent, chunks arriving */
  GADGET_OTA_WAIT_COMMIT,   /* all bytes durably written */
  GADGET_OTA_FINALIZING,    /* checking size + SHA-256, hal_ota_finalize, set boot */
  GADGET_OTA_RESTARTING
} gadget_ota_state_t;

/* fw.fail codes (PROTOCOL.md §4.8), as sent on the wire. */
#define GADGET_FW_TOO_LARGE    "too_large"
#define GADGET_FW_WRONG_BOARD  "wrong_board"
#define GADGET_FW_SAME_VERSION "same_version"
#define GADGET_FW_UNKNOWN_KEY  "unknown_key"
#define GADGET_FW_BAD_SIG      "bad_sig"
#define GADGET_FW_BUSY         "busy"
#define GADGET_FW_FLASH        "flash"
#define GADGET_FW_SEQUENCE     "sequence"
#define GADGET_FW_CHECKSUM     "checksum"
#define GADGET_FW_TIMEOUT      "timeout"

gadget_ota_state_t core_ota_state(void);

#endif /* GADGET_OTA_H */
```

Rules: the offer is checked in the order `wrong_board`, `same_version`, `too_large`, `unknown_key`, `bad_sig` (signature over `gp_firmware_text()` built with the gadget's own board id, verified with `hal_crypto_verify`). Chunks hash into `hal_sha256_t` as they arrive; `fw.commit` checks size and SHA-256 (`checksum` on mismatch), then `hal_ota_finalize`, `hal_ota_set_boot(version)`, `hal_restart`. Probation (5 min, `GADGET_PROBATION_MS`, or `core_config.probation_ms` when non-zero) is core's: started in `core_init()` when the running image is `HAL_OTA_IMG_PENDING_VERIFY`; the first `ready` calls `hal_ota_mark_valid()` then sends `fw.installed {version}`; the timer calls `hal_ota_mark_invalid_and_reboot()`. With `fail_probation` the first `ready` is ignored for that purpose.

Key tables are hand-written C (`keys_release.c`, `keys_test.c`, P2a) from `keys/*.pub.b64` (§4.5). P2a ships `keys_release.c` as the empty sentinel table (`{{NULL, {0}}}`, count 0); P2d replaces it with the real entries once r1 exists. Both files are in every build of core; only the `GADGET_TEST_KEYS` definition decides whether `gadget_test_keys` holds t1 (count 1) or the sentinel (count 0), so `gadget_key_find` always links. Release CI (P2d) fails unless `gadget_release_keys_count ≥ 1` and every release id matches `/^r[0-9]+$/`; every board's `sdkconfig.defaults` sets `# CONFIG_GADGET_TEST_KEYS is not set`.

### 2.14 Storage keys

The `GADGET_KEY_*` names in `gadget_core.h` are the complete list; the simulator stores the same keys (§4.6). `forget` erases all of them. NVS stores strings with `nvs_set_str` and the blob with `nvs_set_blob`, namespace `gadget`, committed on every set.

### 2.15 Art, fonts and screen copy (P2b; consumed by P2c and P2d)

```c
/* firmware/ui/art/maus_art.h (generated) */
/* SPDX-License-Identifier: Apache-2.0 */
/* GENERATED by tools/art (npm run art). Do not edit.
 * Layered Maus art (spec §5.5): one RGB565A8 body per profile, A8 eye
 * frames per expression x 4 blink steps, RGB565A8 closed mouths, RGB565A8
 * open mouths for the gadget-only speaking state; offsets are relative to
 * the body's top-left corner. */
#ifndef MAUS_ART_H
#define MAUS_ART_H

#include "lvgl.h"
#include "gadget_board.h"
#include "gadget_ui_model.h"

#define MAUS_EXPR_COUNT 18      /* union of the state pools in tools/art/source/states.json */
#define MAUS_BLINK_STEPS 4      /* eye openness 1.0, 0.6, 0.25, 0.04 */
#define MAUS_SPEAK_EXPRS 2      /* expressions with open mouths: 19, 6 */
#define MAUS_SPEAK_LEVELS 3     /* open-mouth levels 1..3 (level 0 = the closed mouth) */

typedef struct {
  const lv_image_dsc_t *img;
  int16_t x, y;
} maus_layer_t;

typedef struct {
  const uint8_t *pool;            /* indexes into expr_ids */
  uint8_t pool_len;
  uint16_t cad_min_ms, cad_max_ms;      /* time between expression changes */
  uint16_t blink_min_ms, blink_max_ms;  /* 0, 0 = never blinks */
  int16_t bob_px_x10;  uint16_t bob_ms;     /* translation only (v1); amplitudes in 0.1 px */
  int16_t jitter_px_x10; uint16_t jitter_ms;
  int16_t circle_px_x10; uint16_t circle_ms;
} maus_state_def_t;

typedef struct {
  const char *profile;                                   /* "s240" | "s150" */
  uint16_t w, h;                                         /* body size: 201x240 | 125x150 */
  const lv_image_dsc_t *body;
  const uint8_t *expr_ids;                               /* [MAUS_EXPR_COUNT] app expression numbers */
  const maus_layer_t (*eyes)[MAUS_BLINK_STEPS];          /* [MAUS_EXPR_COUNT][MAUS_BLINK_STEPS] */
  const maus_layer_t *mouth;                             /* [MAUS_EXPR_COUNT] */
  const uint8_t *speak_expr;                             /* [MAUS_SPEAK_EXPRS] indexes into expr_ids */
  const maus_layer_t (*speak)[MAUS_SPEAK_LEVELS];        /* [MAUS_SPEAK_EXPRS][MAUS_SPEAK_LEVELS] */
  const maus_state_def_t *states;                        /* [UI_MAUS__COUNT]; [UI_MAUS_NONE] unused */
  uint32_t total_bytes;                                  /* sum of every image's data_size (budget check) */
} maus_art_t;

/* Simulator links both profiles; an ESP32 build links only its board's. */
extern const maus_art_t maus_art_s240;
extern const maus_art_t maus_art_s150;
/* NULL when that profile is not linked into this build. */
const maus_art_t *maus_art_for(gadget_art_profile_t profile);

#endif /* MAUS_ART_H */
```

- `tools/art` writes `firmware/ui/art/maus_art.h`, `art/maus_art.c` (the getters), `art/s240/*.c` and `art/s150/*.c`; symbol names `maus_<profile>_body`, `maus_<profile>_eye_<expr>_<step>`, `maus_<profile>_mouth_<expr>`, `maus_<profile>_speak_<expr>_<level>`. ESP32 builds compile only the board's profile directory (`MAUS_ART_PROFILE` = `s240` or `s150` from the board's `sdkconfig.defaults`, `CONFIG_GADGET_ART_PROFILE`), and `maus_art_for()` returns NULL for the other one.
- Budgets (CI, `npm run budget`): `s240` ≤ 524288 bytes, `s150` ≤ 262144 bytes; with the white-eye fallback (RGB565A8 eyes) 819200 and 327680.
- State pools (app expression numbers, from `CursorAvatar.tsx` at the pinned commit; speaking is new): idle [6,0,8], listening [1,10,19], thinking [17,8,16,14,5], working [10,7,16,11], sleeping [22,13,4], curious [21,3,0,15], notifying [21,3,0], alerting [21,3], speaking [19,6]. Expression changes happen under a blink. Motion is translation only.
- Palette: body gradient `#8cd1b3` (0) → `#009957` (0.55) → `#005932` (1), corner to corner top-right to bottom-left; eyes and mouth `#ffffff`; open-mouth interior `#005932`. Screen UI on black: accent `#2fd187`, ok `#3ddc84`, bad `#ff5a4f`, warn `#ffb020`, ink `#f2f4f8`, mute `#9aa2b2`, dim `#6f7787`. `#007a45` only on light backgrounds (docs).
- Fonts: `firmware/ui/fonts/font_latin1_<px>.c` defining `const lv_font_t font_latin1_<px>`, generated by `npm run fonts`, which runs the locked `node_modules/.bin/lv_font_conv` (1.5.3, a `tools/art` devDependency) as `lv_font_conv --font Montserrat-Medium.ttf -r 0x20-0x7E,0xA0-0xFF,0x2026,0x2192 --size <px> --bpp 4 --format lvgl --no-compress --lv-include lvgl.h`. P2b picks the sizes. Fonts are applied with `lv_obj_set_style_text_font()` on the UI root and widgets; `LV_USE_CUSTOM_FONT_DEFAULT` is never used.
- `firmware/ui/ui_lv_compat.h` wraps every flag change (`ui_set_hidden()`, `ui_set_clickable()`, `ui_set_scrollable()`); `firmware/ui/ui_lv_requirements.h` `#error`s unless `LV_USE_ANIMIMG`, `LV_DRAW_SW_SUPPORT_RGB565A8`, `LV_DRAW_SW_SUPPORT_A8`, `LV_USE_OS == LV_OS_NONE` and a 16-bit colour depth.
- A8 eye images need `lv_obj_set_style_image_recolor(obj, lv_color_white(), 0)`; the body and mouth layers never set recolor.

**Screen copy (exact English strings; P2b draws them, P2d's README and installer quote them).** All fit the gadget charset.

| Where | Text |
|---|---|
| Idle | `Hi, I'm <bot_name>` (just `Hi` when `bot_name` is empty) |
| Setup, need Wi-Fi | `Connect me to Wi-Fi with the installer.` |
| Setup, need code (main copy) | `Pair me: MausBot → Settings → Remote access → Pair a gadget` then `Enter the code in the installer.` then `Remote access must be on.` |
| Setup, pairing | `Pairing with <host_name>…` (or `Pairing…` before a challenge) |
| Setup, host not found | `Can't find MausBot. Enter the address shown under Pair a gadget in the installer.` |
| Setup, bad code | `That code didn't work. Get a new one from Pair a gadget.` |
| Setup, device limit | `MausBot has too many devices. Remove one in Remote access. Retrying…` |
| Offline, Wi-Fi connecting | `Connecting to Wi-Fi…` |
| Offline, Wi-Fi failed | `Can't join <ssid>.` |
| Offline, looking up | `Looking for MausBot…` |
| Offline, unreachable | `Can't reach <host_name>.` / `Is Remote access on in MausBot?` / `On Windows, set this network to Private.` / `Retrying in <n> s` |
| Offline, in use elsewhere | `In use elsewhere` / `Press TALK to use it here.` |
| Offline, protocol | `MausBot didn't accept me.` / `Retrying in <n> s` |
| Ask, not answerable | `Answer on your computer or phone` |
| Listening countdown | the digit `5` … `1` |
| Update | `Updating… <pct>%`, `Checking update…`, `Restarting…` |
| Device id footer (Setup) | `<device_id>` |

### 2.16 Simulator: CLI, script, internal seams (P2a; P2b extends)

**CLI** (`gadget-sim`):

| Flag | Meaning |
|---|---|
| `--board <id>` | required; one of the four board ids |
| `--name <name>` | state slot `~/.openmausbot-gadget/sim/<name>/` (default `default`); when given, also `core_config.default_name` (the display name until console `name` or a desktop rename) |
| `--state-dir <dir>` | use this folder instead (tests) |
| `--host <addr[:port]>` | same as console `host <addr>` before start; `--host script` selects the scripted network backend (no socket) |
| `--pair <code>` | same as console `pair <code>` before start; applied only on boot 0 (re-execs drop it, below) |
| `--boot <n>` | contract addition: restart count, default 0. Set only by the simulator's own re-exec; selects where `--script` resumes (`boot` command) |
| `--headless` | no SDL: LVGL test display (or the null display without LVGL) on a virtual clock; requires `--script` |
| `--script <file>` | run the script; exit 0 at its end, 1 on the first failure |
| `--mic-file <wav>` | 16 kHz mono PCM16 WAV fed to the mic from its start each time TALK is held; silence after its end |
| `--speaker-file <wav>` | write everything played to this WAV at the speaker rate |
| `--seed <u32>` | PRNG seed; default 1 with `--headless`, else random |
| `--battery <pct>[:charging]` | simulated battery for boards that have one (default `100`) |
| `--zoom <n>` | window zoom (default 2 when the screen is ≤ 320 px wide, else 1) |
| `--snapshot-dir <dir>` | default `firmware/tests/snapshots/<board>` |
| `--fail-probation` | test only (spec §5.7) |
| `--probation-ms <ms>` | test only, contract addition: `core_config.probation_ms` (default 0 = `GADGET_PROBATION_MS`), so the forced-rollback e2e test does not wait 300 s of paced time |
| `--trace` | print every frame as `>> {json}` (tx) / `<< {json}` (rx) on stderr |
| `--version`, `--help` | print and exit 0 |

Exit codes: 0 success, 1 script failure, 2 usage error, 3 fatal start-up error. Logs go to stderr; console output (`@omb` lines) to stdout.

**Modes and simulator additions (§6 D26).** Without `--headless`, a build with SDL opens the window; a build without SDL (P2a's, or any `GADGET_WITH_SDL` `OFF` build) runs on the real clock with the null display and reads console lines from stdin. `host auto` resolves (dns_sd, macOS) only in real-clock runs; headless runs pass `--host`. The default `--snapshot-dir` is `firmware/tests/snapshots/<board>`. P2a's headless scripts in `firmware/tests/scripts/` run as CTest tests named `sim.<script>` (label `unit`); P2b's snapshot scripts keep their `ui.snap.*` names (label `snapshot`).

**Clock:** window mode uses the real clock. Headless mode advances a virtual clock 10 ms per loop iteration (`sim_display_advance(10)`, `core_tick(vt)`, `ui_tick(vt)`). With a real socket (`--host <addr>`) each iteration also waits up to 10 ms of real time in `poll()`, so virtual time tracks real time; with `--host script` or no host it runs unpaced.

**Script** (one command per line, `#` comment):

| Command | Does |
|---|---|
| `wait <ms>` | advance the clock; I/O serviced on every tick |
| `touch <x> <y>` / `release` | press / release the shared pointer |
| `swipe <up\|down\|left\|right>` | deliver `GADGET_IN_SWIPE` |
| `talk_down` / `talk_up` / `cancel` | TALK edges; CANCEL press+release |
| `console <line…>` | feed one console line |
| `expect <op> [timeout_ms]` | wait until a frame with that op has crossed the socket either way (default 5000). It matches the first such frame since the previous `expect` matched, including frames that crossed before this line was reached, so scripts do not depend on network timing (§6 D23) |
| `snapshot <name>` | compare to `<snapshot-dir>/<name>.png`; in a build without LVGL it prints `skip snapshot <name>` and passes; in a `GADGET_SNAPSHOT_UPDATE` build it deletes the PNG first, so LVGL writes a fresh reference |
| `model <field> <value> [timeout_ms]` | contract addition: wait until a `ui_model` field equals `<value>` (or contains it when `<value>` starts with `~`); fields: `screen`, `maus`, `pair`, `speak_level`, `bot_name`, `host_name`, `thinking.heard`, `thinking.working`, `reply.text`, `reply.final`, `reply.failed`, `reply.reason`, `ask.title`, `ask.body`, `ask.n_options`, `ask.answerable`, `card.title`, `card.body`, `image.w`, `image.h`, `toast.visible`, `toast.text`, `setup.step`, `offline.reason`, `update.pct`, `update.phase`, `battery.pct` (enum values by their lowercase names, e.g. `setup.step need_code`, `offline.reason in_use_elsewhere`) |
| `net_open [timeout_ms]` | `--host script` only: wait up to `timeout_ms` (default 5000) for the gadget to connect (`hal_ws_open`), then deliver `GADGET_EV_WS_OPEN`; fails when no connection is asked for in time (§6 D24) |
| `net_text <json…>` | `--host script` only: deliver one text frame from the "host". Every `${turn}` in it becomes the `turn` of the last `voice.begin` or `say` the gadget sent; before the first one, `net_text` fails with `no turn yet` (§6 D27). A line without `${turn}` is sent as written |
| `net_binary <hex>` | `--host script` only: deliver one binary frame |
| `net_close [code]` | `--host script` only: deliver `GADGET_EV_WS_CLOSED` |
| `battery <pct> [charging]` | change the simulated battery |
| `boot <n> [timeout_ms]` | contract addition. In a process started with `--boot n`, the script starts on the line after `boot n` (exit 1 when the script has no such line). In an earlier boot, reaching `boot n` waits, advancing the clock, until the simulator restarts (OTA commit, probation rollback, `reboot`, `forget`), and fails after `timeout_ms` (default 10000). `boot` lines appear in increasing order; reaching `boot k` with k ≤ the current boot is a script error |

**Internal seams between P2a and P2b:** `ports/sim/sim_display.h` and `ports/sim/sim_hal.h` (below). P2a ships `sim_display_null.c` and `ui_stub.c` (records the last rendered `rev` and screen; no LVGL), `sim_events.c` (`sim_post_event` and its queue), `sim_audio.c` (the only definitions of `hal_mic_*`/`hal_spk_*`, forwarding to the selected backend) and `sim_audio_file.c` (file/null backend). P2b ships `sim_display_lvgl.c` (LVGL test display + `lv_test_screenshot_compare` for `--headless`, SDL window + round mask on `lv_layer_sys()` otherwise), `sim_sdl.c` (SDL init and the event filter) and `sim_audio_sdl.c` (`sim_audio_sdl_backend()`, SDL queued audio for window mode). `main.c` (P2a) only calls the seams, so P2b never edits a P2a file except to add its sources to `ports/sim/CMakeLists.txt`.

- **Audio selection (main.c, once, before `core_init()`):** in `--headless` mode, or when `--mic-file` or `--speaker-file` is given, `sim_audio_use(sim_audio_file_backend(mic_wav, spk_wav))` (NULL paths = null audio for that direction); otherwise `sim_audio_use(sim_audio_sdl_backend())` in a build with `GADGET_WITH_SDL`, else the file backend with NULL paths. Each loop iteration calls the selected backend's `pump(now)`, then delivers the queued events with `core_event()`, then calls `core_tick(now)`.
- **Window input (P2b, `sim_sdl.c`):** the SDL event filter calls `sim_post_event` with `GADGET_EV_INPUT` for Space (`GADGET_IN_TALK_DOWN`/`_UP`, key repeat ignored), Esc (`GADGET_IN_CANCEL_DOWN`/`_UP`) and the mouse (`GADGET_IN_TOUCH_DOWN`/`_MOVE`/`_UP` in screen pixels, after dividing by the zoom), and for the mouse also calls `sim_display_touch()` so LVGL's pointer follows. It posts each only for the input sources in the board's `input_mask` (§2.3), so the window behaves like the device: Esc does nothing on `amoled-175`, and the mouse does nothing on `lcd-154` and `devkit` (P2b deviation 3). It drops quit and window-close events (`LV_SDL_DIRECT_EXIT` 0).

```c
/* firmware/ports/sim/sim_hal.h */
/* SPDX-License-Identifier: Apache-2.0 */
/* Simulator seams between P2a's main loop and HAL backends and P2b's SDL
 * code. P2a implements sim_post_event (sim_events.c), sim_audio_use and
 * the hal_mic_* / hal_spk_* symbols (sim_audio.c), and
 * sim_audio_file_backend (sim_audio_file.c). P2b implements
 * sim_audio_sdl_backend (sim_audio_sdl.c). */
#ifndef SIM_HAL_H
#define SIM_HAL_H

#include "gadget_events.h"

/* Queue one event for core. Deep-copies every pointer payload (mic.pcm,
 * ws.data, console.line, scan.aps, mdns.hosts) and frees the copy after
 * delivery. Safe to call from any thread (SDL may run an event filter off
 * the main thread). The main loop delivers queued events in order, with
 * core_event(), before the next core_tick(). */
void sim_post_event(const gadget_event_t *ev);

/* The audio behind hal_mic_* / hal_spk_*. Each member has the semantics of
 * the HAL function of the same name (gadget_hal.h). Mic frames are posted
 * with sim_post_event(GADGET_EV_MIC_FRAME) from pump(). */
typedef struct {
  gadget_status_t (*mic_start)(uint32_t rate);
  void (*mic_stop)(void);
  gadget_status_t (*spk_open)(uint32_t rate);
  size_t (*spk_write)(const int16_t *pcm, size_t samples);
  uint32_t (*spk_buffered_ms)(void);
  void (*spk_stop)(void);
  void (*spk_set_volume)(uint8_t pct);
  void (*pump)(uint64_t now_ms);   /* main.c calls it once per loop iteration */
} sim_audio_backend_t;

/* Select the backend; sim_audio.c forwards every hal_mic_* / hal_spk_* call
 * to it. Before the first call, or with NULL, audio is null. */
void sim_audio_use(const sim_audio_backend_t *backend);
/* P2a: WAV in (--mic-file, replayed from its start on each TALK hold) and
 * WAV out (--speaker-file); a NULL path means null audio for that direction. */
const sim_audio_backend_t *sim_audio_file_backend(const char *mic_wav, const char *spk_wav);
#if defined(GADGET_WITH_SDL)
/* P2b: SDL queued audio for window mode (mic opened lazily on first TALK). */
const sim_audio_backend_t *sim_audio_sdl_backend(void);
#endif

#endif /* SIM_HAL_H */
```

```c
/* firmware/ports/sim/sim_display.h */
/* SPDX-License-Identifier: Apache-2.0 */
/* Simulator display seam. P2a ships sim_display_null.c (no LVGL); P2b ships
 * sim_display_lvgl.c (LVGL test display for --headless, SDL window
 * otherwise, round mask, the shared touch pointer indev). Exactly one is
 * linked, chosen by GADGET_WITH_LVGL. */
#ifndef SIM_DISPLAY_H
#define SIM_DISPLAY_H

#include "gadget_board.h"

typedef struct {
  bool headless;
  float zoom;              /* window mode only */
  const char *snapshot_dir;
} sim_display_opts_t;

/* lv_init() + display + indev (+ SDL init and event filter in window mode). */
int sim_display_init(const gadget_board_t *board, const sim_display_opts_t *opts);
/* Virtual-clock mode: lv_tick_inc(elapsed_ms). Window mode: no-op. */
void sim_display_advance(uint32_t elapsed_ms);
/* Pump SDL events (window mode); the event filter turns them into HAL
 * events with sim_post_event() (sim_hal.h). No-op when headless. */
void sim_display_poll(void);
/* The script's touch commands drive the same pointer state as the mouse. */
void sim_display_touch(bool pressed, int16_t x, int16_t y);
/* Window closed or SDL quit requested. */
bool sim_display_quit_requested(void);
/* `snapshot <name>`: 1 = passed, 0 = failed (writes <name>_err.png),
 * 2 = no reference, -1 = not supported in this build (null display).
 * Built with GADGET_SNAPSHOT_UPDATE: unlink("<snapshot-dir>/<name>.png")
 * first, so LVGL (which only creates a missing reference) writes it anew. */
int sim_display_snapshot(const char *name);
/* SDL_Quit / lv_deinit; must run before every execv restart. */
void sim_display_deinit(void);

#endif /* SIM_DISPLAY_H */
```

**OTA in the simulator** writes `slot0.bin`/`slot1.bin` and `otadata.json` (§4.6). **Every restart** (`hal_restart()` after an OTA commit, `hal_ota_mark_invalid_and_reboot()` at the end of probation, console `reboot` and `forget`) runs `sim_display_deinit()` and then `execv` of its own path (`_NSGetExecutablePath` / `/proc/self/exe`) with `argv0 <the original arguments without --pair and without --boot> --boot <n+1>`, where n is the current boot (0 when `--boot` was absent). So `--pair` is applied only on boot 0, and `--script` resumes after its `boot <n+1>` line instead of starting again at line 1. Sockets are `FD_CLOEXEC`; stdin, stdout and stderr are inherited. `fw` after the re-exec is the active slot's `version`.

### 2.17 ESP32 port seams (P2c)

- `firmware/ports/esp32/CMakeLists.txt`: fails unless `GADGET_BOARD` names a directory under `boards/`; sets `SDKCONFIG_DEFAULTS "sdkconfig.defaults;boards/${GADGET_BOARD}/sdkconfig.defaults"` and `PROJECT_VER` `0.0.0-dev` when not passed; adds `../../core` and `../../ui` to `EXTRA_COMPONENT_DIRS`; `project(openmausbot-gadget)`, so the app image is `build/<board>/openmausbot-gadget.bin`.
- `firmware/core/CMakeLists.txt` and `firmware/ui/CMakeLists.txt` (owned by P2a and P2b) contain an `if(ESP_PLATFORM)` branch: `idf_component_register(SRCS … INCLUDE_DIRS include REQUIRES espressif__cjson mbedtls)` for core, with `keys_release.c` and `keys_test.c` always in `SRCS`, followed by
  ```cmake
  if(CONFIG_GADGET_TEST_KEYS)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE GADGET_TEST_KEYS=1)
  endif()
  ```
  and `REQUIRES lvgl__lvgl core` for ui. P2c may correct these branches; it must not change the desktop branch.
- **Early-expansion guard (P2b owns it).** ESP-IDF first runs every component's `CMakeLists.txt` in CMake script mode (`CMAKE_BUILD_EARLY_EXPANSION` set) to collect requirements, before any `CONFIG_*` value exists, and `file(GLOB … CONFIGURE_DEPENDS)` is an error there. So `firmware/ui/CMakeLists.txt` opens, before anything else but its comments, with
  ```cmake
  if(ESP_PLATFORM AND CMAKE_BUILD_EARLY_EXPANSION)
    idf_component_register(INCLUDE_DIRS . art fonts REQUIRES lvgl__lvgl core)
    return()
  endif()
  ```
  P2b writes it and keeps it in every later edit; P2c only checks that it is there (and stops if it is not), and never edits the file outside its ESP branch.
- Shared `firmware/ports/esp32/sdkconfig.defaults` (P2c) contains, among P2c's other keys, `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y` and `# CONFIG_BOOTLOADER_APP_ANTI_ROLLBACK is not set`. Without app rollback a new image never enters `ESP_OTA_IMG_PENDING_VERIFY`, so probation (§2.13) would never run and a bad image would never roll back. It also pins `CONFIG_LV_CACHE_DEF_SIZE=0` and `CONFIG_LV_IMAGE_HEADER_CACHE_DEF_CNT=0` (both LVGL image caches off; the Image screen reuses two `lv_image_dsc_t` slots, both caches are keyed by that address, and `ui_lv_requirements.h` `#error`s otherwise) and `CONFIG_LV_CONF_SKIP=y` (the simulator's `firmware/ui/lv_conf.h` is on the ui component's include path and must never be read on the device). All three equal LVGL's Kconfig defaults and are written out so P2c's host test checks them.
- Kconfig (`main/Kconfig.projbuild`, P2c): `GADGET_BOARD_ID` (string), `GADGET_ART_PROFILE` (string `s240`/`s150`), `GADGET_TEST_KEYS` (bool, default n), `GADGET_NVS_ENCRYPT` (bool, default n; HMAC scheme with `CONFIG_NVS_SEC_HMAC_EFUSE_KEY_ID=5`; help text warns about the permanent eFuse burn). Each board's `sdkconfig.defaults` sets the first two and `CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y`, `CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions/16mb.csv"`.
- `main.c` order: `board_early_init()` → NVS init → `psa_crypto_init()` → Wi-Fi start (STA) → display/touch/audio/buttons → `core_init()` with `gadget_board_by_id(CONFIG_GADGET_BOARD_ID)` and `esp_app_get_description()->version` → `ui_init()` → the 10 ms loop.
- `main/board_api.h` (implemented by each `boards/<id>/board.c`): `esp_err_t board_early_init(void)`, `esp_err_t board_display_init(esp_lcd_panel_io_handle_t *io, esp_lcd_panel_handle_t *panel)`, `esp_err_t board_touch_init(esp_lcd_touch_handle_t *tp)` (`ESP_ERR_NOT_SUPPORTED` without touch), `esp_err_t board_audio_init(board_audio_t *out)`, `esp_err_t board_buttons_init(void)`, `bool board_talk_pressed(void)`, `bool board_cancel_pressed(void)`, `bool board_battery_read(gadget_battery_t *out)`, `void board_set_brightness(uint8_t pct)`. `board_audio_t` is P2c's. These are P2c-internal; P2d's AGENTS.md documents them for "add a board".
- `firmware/ports/esp32/tools/check-size.sh <board>` exits non-zero when `build/<board>/openmausbot-gadget.bin` is larger than 6291456 bytes, or when `build/<board>/sdkconfig` lacks either of the exact lines `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y` and `# CONFIG_BOOTLOADER_APP_ANTI_ROLLBACK is not set` (`grep -qxF`). The `esp32` CI job runs it for every board.
- **devkit USB port.** ESP32-S3-DevKitC-1 has two USB-C ports. The console, flashing and the installer use the one labelled **USB** (native USB-Serial-JTAG, VID/PID 0x303A/0x1001); the one labelled **UART** goes through a bridge chip and shows neither the console nor that VID/PID. AGENTS.md (P2d) and `docs/hardware-checklist.md` (P2c) say so, and the installer shows a hint when the board is `devkit` (§4.8).

### 2.18 Desktop CMake targets and options (P2a; P2b adds the UI ones)

| Target | Kind | Owner |
|---|---|---|
| `gadget_core` | static library (`firmware/core`) | P2a |
| `gadget_ui` | static library (`firmware/ui`), only with `GADGET_WITH_LVGL` | P2b |
| `gadget-sim` | executable (`firmware/ports/sim`) | P2a |
| `cjson`, `wslay`, `gadget_mbedcrypto` (interface over `mbedcrypto` or `tfpsacrypto`), `unity::framework`, `lvgl::lvgl`, `SDL2::SDL2` | dependencies | P2a / P2b |
| `test_<area>` + CTest `core.<area>` (label `unit`), `sim.<script>` (label `unit`, §2.16), `vectors.c` (label `vectors`), `e2e.<scenario>` (label `e2e`), `ui.snap.<board>` (label `snapshot`) | tests | P2a / P2b |

| Option / cache var | Default | Meaning |
|---|---|---|
| `GADGET_WITH_LVGL` | `OFF` in P2a, `ON` from P2b | build `gadget_ui`, LVGL and the LVGL/SDL simulator display |
| `GADGET_WITH_SDL` | `ON` when LVGL is on | window mode; adds `target_compile_definitions(gadget-sim PRIVATE GADGET_WITH_SDL=1)` (declares `sim_audio_sdl_backend()` in `sim_hal.h`); headless never needs it at runtime |
| `GADGET_BUILD_SIM`, `GADGET_BUILD_TESTS` | `ON` | |
| `GADGET_TEST_KEYS` | `ON` | `target_compile_definitions(gadget_core PRIVATE GADGET_TEST_KEYS=1)`, so the always-compiled `keys_test.c` holds t1; `OFF` leaves the empty test table |
| `GADGET_SANITIZE` | `OFF` | `-fsanitize=address,undefined` on our targets |
| `GADGET_SNAPSHOT_UPDATE` | `OFF` | defines `LV_TEST_SCREENSHOT_CREATE_REFERENCE_IMAGE=1` on `lvgl` as a PUBLIC definition (`firmware/ui/lv_conf.h` wraps its default `0` in `#ifndef LV_TEST_SCREENSHOT_CREATE_REFERENCE_IMAGE`, so the `-D` wins) and `GADGET_SNAPSHOT_UPDATE=1` on `gadget-sim`, where `sim_display_snapshot()` then deletes each reference PNG before comparing |
| `GADGET_MBEDTLS_VERSION` | `3.6.7` | or `4.2.0` |
| `GADGET_SIM_VERSION` | `0.0.0-dev` | the simulator's `fw` before any OTA. A `-dev` version is a custom build to the app, which refuses to update it; the dev-release flow (§3.16) builds the simulator with `-DGADGET_SIM_VERSION=1.0.0` |

The vectors directory is passed to C tests as `argv[1]` = `${CMAKE_SOURCE_DIR}/../protocol/vectors`.

## 3. OpenMausBot TypeScript contract

### 3.1 Conventions

- The companion ships as plain `tsc` output with no `node_modules` and cannot import `shared/` or `server/`: everything under `companion/src/gadget/` uses Node built-ins and relative imports with `.ts` extensions only. No new companion dependencies. `ws` (a root devDependency) may be used in tests only.
- The harness may `import type` from `companion/src/**` (it already imports `companion/src/routes.ts`); type-only imports are erased by esbuild.
- Code blocks below use `declare` for functions the owner implements with exactly that signature, and copies of existing types where the contract adds fields. Every block was type-checked together with `tsc --strict --erasableSyntaxOnly` against `@types/node` 24.
- Tests: vitest. Companion gadget tests live in `companion/test/gadget/*.test.ts`; shared helpers in `companion/test/gadget/helpers/` (§3.19). No sleeps; wait on events.
- Environment variables introduced:

| Variable | Read by | Meaning |
|---|---|---|
| `OMB_GADGET_CONTROL_TOKEN` | Electron (preferred over minting when it matches `/^[A-Za-z0-9_-]{43}$/`), companion and harness (when no parent message delivers one) | the gadget control token, for dev and tests. It never reaches a child process: Electron reads it once at startup, then deletes it from its own `process.env` and leaves it out of every child env it builds (companion, harness), so the token travels only by parentPort; the companion and the harness read it once at startup into a local and run `delete process.env.OMB_GADGET_CONTROL_TOKEN` before spawning anything, so engines and the agents proxy never inherit it |
| `OMB_COMPANION_CONTROL_PORT` | harness (default `8811`) | where the harness reaches `/gadget/*`; Electron sets it in the packaged harness's env |
| `OMB_SPEECH_HELPER_PATH` | harness | overrides the Speech helper bundle path |
| `OMB_GADGET_MANIFEST_URL` | companion | overrides `MANIFEST_URL` (dev, tests) |
| `OMB_GADGET_TRUST_TEST_KEY` | companion | `"1"` adds test key `t1` to the trusted release keys (dev, tests only) |
| `OMB_GADGETS` | agents proxy | catalog switch set by the harness (`"1"` while a gadget is paired) |

### 3.2 `companion/src/gadget/protocol.ts` (P3a)

```ts
// companion/src/gadget/protocol.ts (P3a) — openmausbot-gadget/1 on the host side.
// Node built-ins only. The SDK's protocol/lib/types.ts uses the same type names.
import type { Buffer } from "node:buffer";

export const GADGET_PATH = "/gadget";
export const GADGET_SUBPROTOCOL = "openmausbot-gadget.1";
export const PROTO_VERSION = 1;
export const TEXT_FRAME_MAX = 16 * 1024;
export const BINARY_FRAME_MAX = 8 * 1024;
export const PING_INTERVAL_MS = 15_000;
export const IDLE_TIMEOUT_MS = 45_000;
export const HANDSHAKE_TIMEOUT_MS = 10_000;
export const UTTERANCE_MAX_MS = 60_000;
export const SAY_MAX_CHARS = 2000;
export const REPLY_MIN_INTERVAL_MS = 250;
export const ACT_TIMEOUT_MS = 15_000;
export const SPEAK_AHEAD_MS = 500;
export const SPEAK_FRAME_MS = 40;
export const FW_CHUNK_BYTES = 4096;
export const FW_WINDOW_BYTES = 64 * 1024;
export const FW_READY_TIMEOUT_MS = 10_000;
export const NAME_MAX_CHARS = 32;
export const ACTIONS_MAX = 16;
export const ACTION_DESCRIPTION_MAX = 200;
export const ACTION_PARAMS_MAX_BYTES = 1024;
export const HOST_ID_RE = /^[0-9a-f]{32}$/;
export const GADGET_ID_RE = /^gad_[0-9a-f]{16}$/;
export const PAIR_CODE_RE = /^\d{6}$/;
export const BOARD_ID_RE = /^[a-z0-9-]{1,32}$/;
export const ACTION_NAME_RE = /^[a-z][a-z0-9_.-]{0,31}$/;
export const SHA256_HEX_RE = /^[0-9a-f]{64}$/;
export const RELEASE_VERSION_RE = /^\d+\.\d+\.\d+(-[0-9A-Za-z.]+)?$/;

export const BinaryKind = { mic: 0x01, speaker: 0x02, image: 0x03, firmware: 0x04 } as const;
export type BinaryKindValue = (typeof BinaryKind)[keyof typeof BinaryKind];

export type GadgetErrorCode =
  | "proto_unsupported" | "enroll_required" | "bad_code" | "bad_sig" | "revoked" | "device_limit" | "replaced";
export type FwFailCode =
  | "too_large" | "wrong_board" | "same_version" | "unknown_key" | "bad_sig"
  | "busy" | "flash" | "sequence" | "checksum" | "timeout";
export type Risk = "safe" | "confirm";
export type OptionStyle = "allow" | "deny" | "neutral";
export type AskCloseReason = "answered" | "expired" | "withdrawn";
export type TurnOutcome = "ok" | "failed" | "stopped";
export type PostKind = "routine" | "message";
export type ScreenCharset = "latin1";

export interface GadgetCaps {
  screen?: { w: number; h: number; round?: boolean; text?: ScreenCharset };
  image?: { w: number; h: number };
  mic?: { rate: number };
  speaker?: { rate: number };
  input?: string[];             // "touch" | "talk" | "cancel" (others ignored)
  battery?: boolean;
  ota?: { max: number };
}
export interface GadgetActionDecl { name: string; description: string; params: Record<string, unknown>; risk?: Risk }
export interface GadgetSensors { battery_pct?: number; charging?: boolean; [key: string]: unknown }
export interface BotRef { id: string; name: string }
export interface GadgetSettings { speak_pushes: boolean }
export interface AskOption { id: string; label: string; style?: OptionStyle }

// ---- gadget → host
export interface HelloMsg {
  op: "hello"; proto: number; id: string; pubkey: string; name: string; board: string; fw: string;
  caps: GadgetCaps; actions?: GadgetActionDecl[]; sensors?: GadgetSensors;
}
export interface ProveMsg { op: "prove"; sig: string; enroll?: string }
export interface VoiceBeginMsg { op: "voice.begin"; turn: string; stream: number; rate: number }
export interface VoiceEndMsg { op: "voice.end"; turn: string; ms: number }
export interface VoiceDropMsg { op: "voice.drop"; turn: string }
export interface SayMsg { op: "say"; turn: string; text: string }
export interface StopMsg { op: "stop"; turn?: string }
export interface AnswerMsg { op: "answer"; id: string; option: string }
export interface ActResultMsg { op: "act.result"; id: string; ok: boolean; data?: unknown; error?: string }
export interface SenseMsg { op: "sense"; battery_pct?: number; charging?: boolean; [key: string]: unknown }
export interface EventMsg { op: "event"; name: string; data?: unknown }
export interface FwReadyMsg { op: "fw.ready"; stream: number }
export interface FwFailMsg { op: "fw.fail"; stream: number; code: FwFailCode | string }
export interface FwProgressMsg { op: "fw.progress"; stream: number; offset: number }
export interface FwInstalledMsg { op: "fw.installed"; version: string }

// ---- host → gadget
export interface ChallengeMsg { op: "challenge"; nonce: string; host_id: string; host_name: string }
export interface ReadyMsg { op: "ready"; session: string; bot: BotRef; settings: GadgetSettings }
export interface ErrorMsg { op: "error"; code: GadgetErrorCode; message: string }
export interface SettingsMsg { op: "settings"; bot: BotRef; settings: GadgetSettings; name?: string }
export interface HeardMsg { op: "heard"; turn: string; text: string }
export interface WorkingMsg { op: "working"; turn: string; text: string }
export interface ReplyMsg { op: "reply"; turn: string; text: string; final: boolean }
export interface DoneMsg { op: "done"; turn: string; outcome: TurnOutcome; reason?: string }
export interface SpeakBeginMsg { op: "speak.begin"; stream: number; rate: 16000 | 24000; turn?: string }
export interface SpeakEndMsg { op: "speak.end"; stream: number }
export interface SpeakStopMsg { op: "speak.stop"; stream: number }
export interface AskMsg {
  op: "ask"; id: string; kind: "permission" | "question"; title: string; body: string;
  options: AskOption[]; expires_s?: number;
}
export interface AskCloseMsg { op: "ask.close"; id: string; reason: AskCloseReason }
export interface PostMsg { op: "post"; id: string; bot: BotRef; kind: PostKind; text: string; speak: boolean }
export interface CardMsg { op: "card"; id: string; title: string; body: string; ttl_s: number }
export interface CardCloseMsg { op: "card.close"; id: string }
export interface ImageBeginMsg { op: "image.begin"; id: string; stream: number; w: number; h: number; ttl_s: number }
export interface ImageEndMsg { op: "image.end"; stream: number }
export interface ActMsg { op: "act"; id: string; name: string; args: Record<string, unknown> }
export interface FwOfferMsg {
  op: "fw.offer"; stream: number; board: string; version: string; size: number;
  sha256: string; sig: string; key_id: string;
}
export interface FwCommitMsg { op: "fw.commit"; stream: number }

export type GadgetToHost =
  | HelloMsg | ProveMsg | VoiceBeginMsg | VoiceEndMsg | VoiceDropMsg | SayMsg | StopMsg | AnswerMsg
  | ActResultMsg | SenseMsg | EventMsg | FwReadyMsg | FwFailMsg | FwProgressMsg | FwInstalledMsg;
export type HostToGadget =
  | ChallengeMsg | ReadyMsg | ErrorMsg | SettingsMsg | HeardMsg | WorkingMsg | ReplyMsg | DoneMsg
  | SpeakBeginMsg | SpeakEndMsg | SpeakStopMsg | AskMsg | AskCloseMsg | PostMsg | CardMsg | CardCloseMsg
  | ImageBeginMsg | ImageEndMsg | ActMsg | FwOfferMsg | FwCommitMsg;
export type GadgetOp = GadgetToHost["op"];

/** Parse + shape-check one gadget → host text frame; null for invalid JSON,
 *  a missing op, an unknown op, or a known op with a wrong required field. */
export declare function parseGadgetMessage(text: string): GadgetToHost | null;
/** JSON.stringify that drops undefined fields (never writes null). */
export declare function encodeHostMessage(msg: HostToGadget): string;

export declare function encodeBinary(kind: BinaryKindValue, stream: number, payload: Uint8Array): Buffer;
export declare function decodeBinary(frame: Uint8Array): { kind: BinaryKindValue; stream: number; payload: Buffer } | null;
export declare function encodeFwChunk(stream: number, offset: number, data: Uint8Array): Buffer;

/** RFC 4648 §4 with padding; true only when decode→encode gives the same text. */
export declare function isCanonicalBase64(value: string): boolean;
/** "gad_" + first 16 hex of sha256(pub). */
export declare function gadgetIdFromPubkey(pub: Uint8Array): string;
/** 65 bytes starting 0x04 decoded from canonical base64, else null. */
export declare function decodePubkey(b64: string): Buffer | null;
export declare function proveText(id: string, nonceB64: string, hostId: string): string;
export declare function firmwareText(board: string, version: string, size: number, sha256Hex: string): string;
/** node:crypto verify (accepts high-S); false on any parse error. */
export declare function verifyP256(pub65: Uint8Array, text: string, derSig: Uint8Array): boolean;
/** JSON with object keys sorted (UTF-16 order) at every depth, no whitespace. */
export declare function canonicalJson(value: unknown): string;
/** First 16 hex of sha256(canonicalJson({name, description, params, risk})) with risk defaulted to "confirm". */
export declare function actionEntryHash(action: { name: string; description: string; params: unknown; risk: Risk }): string;
```
Notes: `parseGadgetMessage` enforces field types and the hello limits only as far as needed to reject garbage; `enroll.ts` applies the §4.3 limits. Every string sent for the screen goes through `shape.ts` first (spec §4.4).

### 3.3 `companion/src/gadget/types.ts` (P3a)

```ts
// companion/src/gadget/types.ts (P3a) — local copies of the few OpenMausBot
// wire types and helpers the hub needs. The companion cannot import shared/
// (tsc rootDir is companion/src). Source: origin/main 6dd4403 shared/wire.ts,
// shared/runtime-events.ts, shared/routines.ts, shared/ask-question.ts,
// shared/notification.ts. Field subsets only; unknown fields are ignored.

export interface WireTaskLite {
  threadId: string;
  title: string;
  routineRunId?: string;
  activity?: "working" | "waiting-on-you" | "idle" | "no-signal" | "dead" | "parked.computer";
  busy?: boolean;
}
export interface WireBotLite {
  id: string;
  name: string;
  threadId: string;          // the selected thread (the gadget's thread, pinned per turn)
  tasks?: WireTaskLite[];
  hidden?: boolean;
  pinned?: boolean;
  section?: string;
  chiefOfStaff?: boolean;
  voice?: string;            // passed as voiceId to /api/tts/*
  busy?: boolean;
}
export interface AskQuestionLite {
  question: string;
  header?: string;
  multiSelect?: boolean;
  options: Array<{ label: string; description?: string }>;
}
export interface OptionCardLite {
  title: string;
  subtitle: string;
  options: string[];
  requestType?: "permission" | "question";
  requestId?: string;
  tool?: string;
  answered?: string;
  dismissed?: boolean;
  expired?: boolean;
  allowKey?: string;
  questionRequest?: { version: 1; questions: AskQuestionLite[] };
  routineRequest?: unknown;
  skillRequest?: unknown;
  profileRequest?: unknown;
  modelRequest?: unknown;
  teamSetupRequest?: unknown;
}
export interface WireMessageLite {
  id: string;
  role: "bot" | "user";
  kind: "text" | "options" | "activity" | "screen" | "connector" | "secret" | "routine.run" | "goal.run" | "digest" | "compaction";
  text?: string;
  card?: OptionCardLite;
  tool?: { name: string; spoken?: string };
  turnId?: string;
  requestMessageId?: string;
  turnTerminal?: boolean;
  turnSucceeded?: boolean;
  sendId?: string;
  peerAsk?: { botId: string; name: string; unattended?: boolean };
  steered?: boolean;
  queued?: boolean;
  queueId?: string;
  at: number;
}
export type RoutineRunStatusLite = "queued" | "running" | "waiting" | "completed" | "failed" | "cancelled" | "missed";
export interface RoutineRunLite {
  id: string;
  botId: string;
  routineName: string;
  status: RoutineRunStatusLite;
  output?: string;
  error?: string;
  attention?: string;
  executionThreadId?: string;
}
export interface RuntimeEventLite {
  type: string;              // "turn.started" | "turn.completed" | "content.delta" | "request.opened" | "request.resolved" | …
  threadId: string;
  turnId?: string;
  requestId?: string;
  ok?: boolean;              // turn.completed
  stopReason?: string | null;
  streamKind?: "assistant_text" | "reasoning_text";  // content.delta
  delta?: string;
  behavior?: "allow" | "deny" | "answer";            // request.resolved
  source?: "user" | "auto" | "timeout" | "system" | "unavailable" | "peer";
}
export interface NotificationLite { kind: string; botId: string; threadId: string }
export type ServerFrameLite =
  | { kind: "hello"; cursor: string; resumed: boolean }
  | { kind: "ping" }
  | { kind: "message"; threadId: string; message: WireMessageLite }
  | { kind: "message.patch"; threadId: string; message: WireMessageLite }
  | { kind: "bot"; bot: WireBotLite }
  | { kind: "bot.queued"; queues: Record<string, Array<{ queueId: string; text: string; reason?: "capacity" | "group-turn" }>> }
  | { kind: "bot.deleted"; botId: string }
  | { kind: "notify"; notification: NotificationLite }
  | { kind: "routine.run"; run: RoutineRunLite }
  | { kind: "runtime"; event: RuntimeEventLite }
  | { kind: "other"; raw: { kind: string } };   // every other frame kind, passed through untouched

/** 202 body of POST /api/bots/:id/messages (server/index.ts:8691). */
export type DirectSendReceiptLite =
  | { ok: true; threadId: string; message: WireMessageLite; steered?: true }
  | { ok: true; queued: true; queueId: string; threadId: string; reason?: "capacity" | "group-turn" };

export declare const QUESTION_DISMISS_MESSAGE: string;   // copy of shared/ask-question.ts
export declare const ANSWER_PREAMBLE: string;            // "The user answered your questions."
/** Copy of shared/ask-question.ts isPersistentQuestionCard, with a test pinned to the original's cases. */
export declare function isPersistentQuestionCard(card?: OptionCardLite | null): boolean;
/** How a card reaches the gadget (spec §6.2 Asks, refined by contract D19). In this order:
 *  "unsupported" when any of routineRequest, skillRequest, profileRequest, modelRequest or
 *  teamSetupRequest is set (proposal cards; /respond would apply them on "allow");
 *  "question" when isPersistentQuestionCard(card) (the ≤ 4-choice / one single-select rule may
 *  still turn it into an unsupported ask); "permission" only when card.tool is a non-empty string
 *  (provider permission cards and the harness's peer-approval cards); "unsupported" otherwise.
 *  Unsupported cards are sent as ask {kind: "question", options: []}. P3a tests one card per
 *  proposal kind, plus a tool card, a peer-approval card and a question card. */
export declare function gadgetAskKind(card: OptionCardLite): "permission" | "question" | "unsupported";
/** Copy of shared/ask-question.ts formatQuestionAnswers. */
export declare function formatQuestionAnswers(questions: readonly AskQuestionLite[], answers: readonly (readonly string[])[]): string;
/** §4.3 rule 3 default bot over GET /api/bots (visible bots, server order):
 *  first chiefOfStaff && !section; else first pinned; else first !section && !chiefOfStaff; else first. */
export declare function defaultGadgetBot(bots: readonly WireBotLite[]): WireBotLite | null;
```
### 3.4 `companion/src/gadget/ws.ts` (P3a)

```ts
// companion/src/gadget/ws.ts (P3a) — hand-rolled RFC 6455 server, Node built-ins only.
import type { IncomingMessage } from "node:http";
import type { Duplex } from "node:stream";
import type { Buffer } from "node:buffer";

export interface WsServerOptions {
  subprotocol: string;          // GADGET_SUBPROTOCOL; required in Sec-WebSocket-Protocol
  maxText: number;              // TEXT_FRAME_MAX → close 1009 beyond
  maxBinary: number;            // BINARY_FRAME_MAX → close 1009 beyond
  pingIntervalMs?: number;      // default PING_INTERVAL_MS
  idleTimeoutMs?: number;       // default IDLE_TIMEOUT_MS: terminate after this long without any inbound frame
  drainThresholdBytes?: number; // default 64 KiB: sendBinaryDrained waits for 'drain' above it
  maxBufferedBytes?: number;    // default 256 KiB: close 1008 and drop the connection above it
}

/** Refusals are written as plain HTTP and the socket is destroyed:
 *  403 any Origin header; 400 not a GET upgrade, bad Sec-WebSocket-Key, missing subprotocol;
 *  426 Sec-WebSocket-Version ≠ 13 (with "Sec-WebSocket-Version: 13"). Extensions are never accepted. */
export type UpgradeCheck = { ok: true; key: string } | { ok: false; status: 400 | 403 | 426; reason: string };
export declare function checkUpgrade(req: IncomingMessage, subprotocol: string): UpgradeCheck;

export interface WsHandlers {
  onText(text: string): void;           // strict UTF-8 (invalid → close 1007)
  onBinary(data: Buffer): void;
  onClose(code: number, reason: string): void;   // exactly once
}

export interface WsConnection {
  attach(handlers: WsHandlers): void;   // frames before attach are buffered
  sendText(text: string): boolean;      // false once closing/closed
  sendBinary(data: Uint8Array): boolean;
  /** Waits for 'drain' while socket.writableLength > drainThresholdBytes; false if the connection closed. */
  sendBinaryDrained(data: Uint8Array): Promise<boolean>;
  close(code?: number, reason?: string): void;    // close frame, then end
  terminate(): void;                              // destroy now
  readonly closed: boolean;
  readonly bufferedAmount: number;                // socket.writableLength
  readonly remoteAddress: string;
}

/** Validate with checkUpgrade, write the 101 (echoing only the subprotocol),
 *  feed `head` to the parser and start ping/idle timers. null when refused. */
export declare function acceptUpgrade(req: IncomingMessage, socket: Duplex, head: Buffer, options: WsServerOptions): WsConnection | null;
```
Close codes: 1000 normal (also after an `error` op), 1001 shutdown, 1002 protocol error (RSV bits, unmasked client frame, unknown opcode, bad control frame), 1007 invalid UTF-8, 1008 backpressure above 256 KiB or handshake deadline, 1009 message too big, 1011 internal. Reply: `HTTP/1.1 101 Switching Protocols`, `Upgrade: websocket`, `Connection: Upgrade`, `Sec-WebSocket-Accept: base64(sha1(key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"))`, `Sec-WebSocket-Protocol: openmausbot-gadget.1`; `socket.setNoDelay(true)`.

### 3.5 `companion/src/devices.ts` changes (P3a)

```ts
// companion/src/devices.ts — the P3a changes (existing members unchanged unless shown).
import type { GadgetSensors } from "./gadget/protocol.ts";

interface DeviceRecordBase {
  id: string;
  name: string;
  createdAt: number;
  lastSeenAt: number;
}
/** A paired phone. A record with no `kind` on disk reads as a phone. */
export interface PhoneDeviceRecord extends DeviceRecordBase {
  kind?: "phone";
  /** sha256 of the bearer token — never the token itself */
  tokenHash: string;
  cloudDesktopAccess: boolean;
  browserControlAccess: boolean;
}
/** A paired gadget: no token; it proves its key on every connection. */
export interface GadgetDeviceRecord extends DeviceRecordBase {
  kind: "gadget";
  /** id is "gad_" + 16 lowercase hex = first 16 hex of sha256(pubkey bytes) */
  publicKey: string;          // canonical base64 of the 65-byte SEC1 point
  board: string;              // BOARD_ID_RE
  firmware: string;           // last hello.fw (≤ 32 chars)
  botId: string | null;       // null = no bot chosen yet
  speakPushes: boolean;       // "Read pushes aloud"
  lastSensors?: GadgetSensors;
  /** A desktop rename made while the gadget was offline (spec §4.3 last-writer-wins). */
  namePending?: true;
}
export type DeviceRecord = PhoneDeviceRecord | GadgetDeviceRecord;
/** What the UI may see. Gadget records carry no secret, so they pass through whole. */
export type PublicPhoneDevice = Omit<PhoneDeviceRecord, "tokenHash">;
export type PublicDevice = PublicPhoneDevice | GadgetDeviceRecord;

export interface PairingWindow {
  code: string;
  token: string;
  expiresAt: number;
  attemptsLeft: number;
  /** Bot chosen in "Pair a gadget"; absent for the phone flows. */
  botId?: string;
}

export interface GadgetEnrollment {
  id: string;
  publicKey: string;
  name: string;               // from hello, cleaned and cut to 32 chars
  board: string;
  firmware: string;
  botId: string | null;       // window.botId ?? defaultGadgetBot(GET /api/bots) ?? null
}
export type EnrollGadgetError = "no_window" | "bad_code" | "too_many" | "device_limit" | "save_failed";

export interface GadgetSettingsPatch {
  botId?: string | null;      // /^[\w-]{1,120}$/ or null
  speakPushes?: boolean;
  name?: string;              // 1..32 chars after cleanDeviceName
}

export declare class DeviceRegistry {
  constructor();
  list(): PublicDevice[];                      // gadgets include in-memory lastSensors
  count(): number;                             // phones + gadgets (MAX_DEVICES = 20)
  pairing(): PairingWindow | null;
  openPairing(botId?: string): PairingWindow;  // botId stored on the window
  /** PUT /pairing/bot: change the open window's bot. false when no window or token mismatch. */
  setPairingBot(botId: string | null, expectedToken: string): boolean;
  closePairing(expectedToken?: string): boolean;
  redeem(credential: string, name: unknown, pairRequestId?: unknown): { device: PublicPhoneDevice; token: string } | { error: string };
  /** Skips records without tokenHash, so a gadget never matches a bearer. */
  authenticate(token: string | undefined): PhoneDeviceRecord | null;
  revoke(id: string): boolean;
  /** false for gadgets (control answers 404). */
  setCloudDesktopAccess(id: string, allowed: boolean): boolean;
  setBrowserControlAccess(id: string, allowed: boolean): boolean;

  // ---- new (P3a)
  gadget(id: string): GadgetDeviceRecord | null;
  gadgets(): GadgetDeviceRecord[];
  /** Shares redeem()'s window logic: /^\d{6}$/ only (else bad_code without burning an attempt),
   *  wrong code burns an attempt (too_many when it reaches 0 and the window closes),
   *  device cap checked only after the code matches (window stays open on device_limit),
   *  consumes the window, persists with rollback. */
  enrollGadget(code: string, gadget: GadgetEnrollment):
    | { device: GadgetDeviceRecord }
    | { error: EnrollGadgetError; message: string };
  /** Persists with rollback; null when no such gadget; throws when saving fails (like setCloudDesktopAccess). */
  updateGadget(id: string, patch: GadgetSettingsPatch & { namePending?: boolean }): GadgetDeviceRecord | null;
  /** On every hello: last-writer-wins name (keeps the record's name when namePending; then
   *  returns sendName so the hub sends settings {name} right after ready), board, firmware, lastSeenAt. */
  noteGadgetHello(id: string, hello: { name: string; board: string; firmware: string }): { record: GadgetDeviceRecord; sendName?: string } | null;
  /** lastSeenAt and lastSensors in memory; persisted at most every 60 s (LAST_SEEN_WRITE_MS). */
  touchGadget(id: string, sensors?: GadgetSensors): void;
}
```
- Loader: accepts `kind === "gadget"` records with `GADGET_ID_RE` ids and a string `publicKey`; `normalizeDevice` keeps the gadget fields; records without `kind` read as phones. Gadgets share `devices.json` and the 20-device cap.
- `enrollGadget` error → protocol: `no_window`, `bad_code`, `too_many` → `error bad_code` (message: `wrong code`, `the code has expired or was already used`, `too many incorrect codes`); `device_limit` → `error device_limit`; `save_failed` → close 1011 without an `error` op.

### 3.6 `companion/src/host-id.ts` and the mDNS record (P3a)

```ts
// companion/src/host-id.ts (P3a)
/** 32 lowercase hex (randomBytes(16)), created once in DATA_DIR/host.json as
 *  {"hostId":"<32hex>"} (0600, writeFileAtomic), kept apart from devices.json.
 *  An unreadable or malformed file is replaced; a failed save logs and still returns the new id. */
export declare function loadOrCreateHostId(): string;
```
`companion/src/index.ts` adds `` `id=${HOST_ID}` `` to the `_openmausbot._tcp` TXT array after `v=1` and `name=…`. `companionState()` gains `hostId`.

### 3.7 `companion/src/gadget/harness-client.ts` (P3a)

```ts
// companion/src/gadget/harness-client.ts (P3a) — the hub's only way to reach the harness.
import type { IncomingHttpHeaders } from "node:http";
import type { Buffer } from "node:buffer";
import type { ServerFrameLite } from "./types.ts";

export interface HarnessClientOptions {
  harnessPort: number;                       // OMB_PORT (8799)
  /** The same getter the proxy gets: present under Electron; null while the token is pending. */
  mutationToken?: () => string | null;
  timeoutMs?: number;                        // default 10_000 (STT calls pass their own)
}
/** Thrown without contacting the harness: token pending under Electron (status 503),
 *  or denyReason() refused the route (its status). */
export declare class HarnessRefused extends Error {
  readonly status: number;
  constructor(status: number, message: string);
}
export interface HarnessJson<T> { status: number; body: T; headers: IncomingHttpHeaders }
export interface HarnessRaw { status: number; body: Buffer; headers: IncomingHttpHeaders }
export type HarnessMethod = "GET" | "POST" | "PATCH" | "DELETE";

export interface HarnessClient {
  /** deviceId: the gadget's gad_ id for per-gadget calls (companionIdentityHeaders(deviceId, token));
   *  null = only the companion marker header, allowed for GET only. Every call first checks
   *  denyReason({path: path without query, method, authenticated: true}) === null. */
  json<T>(method: HarnessMethod, path: string, deviceId: string | null, body?: unknown): Promise<HarnessJson<T>>;
  /** Raw body in and out: POST /api/stt (audio/wav, timeoutMs 60_000), POST /api/tts/speak (JSON in,
   *  PCM out, timeoutMs 30_000). Pinned timeouts are listed under the table below. */
  raw(method: "POST", path: string, deviceId: string, body: Uint8Array, contentType: string, timeoutMs?: number): Promise<HarnessRaw>;
  /** The single shared GET /api/events?screens=off stream (marker header only, no device id).
   *  Reconnects with Last-Event-ID; calls onHello for every hello frame (resumed false after a harness restart). */
  events(handlers: {
    onFrame(frame: ServerFrameLite): void;
    onHello?(resumed: boolean): void;
    onDown?(error: Error): void;
  }): { close(): void };
}
export declare function createHarnessClient(options: HarnessClientOptions): HarnessClient;
```
Harness calls the hub makes (all already in `companion/src/routes.ts` `ALLOWED` except `/api/stt`, which P3b adds):

| Call | Device header | Body → response |
|---|---|---|
| `GET /api/events?screens=off` | marker only | SSE `ServerFrame`s |
| `GET /api/bots?messages=0` | marker only | `{bots: WireBotLite[], …}` |
| `GET /api/threads/:threadId/messages?limit=50` | gadget id | `{messages: WireMessageLite[], hasMore, activeLeafId}` |
| `POST /api/bots/:botId/messages` | gadget id | `{text, threadId, sendId}` → 202 `DirectSendReceiptLite` |
| `POST /api/bots/:botId/interrupt` | gadget id | `{threadId}` → 200 `{ok: true}` |
| `DELETE /api/bots/:botId/queue/:queueId` | gadget id | JSON `{threadId}` (the turn's pinned thread; sent with `harness.json("DELETE", path, gadgetId, {threadId})`) → 200 `{ok: true}`; 404 `{error: "no such queued message"}` |
| `POST /api/threads/:threadId/respond` | gadget id | `{requestId, behavior: "allow" \| "deny" \| "answer", message?}` → 200 `{ok: true, outcome}` |
| `POST /api/tts/prepare` | gadget id | `{text, voiceId?}` → `{ready: boolean, utterances: string[]}` (P3b) |
| `POST /api/tts/speak` | gadget id | `{text, voiceId?, format: "pcm_16000" \| "pcm_24000"}` → PCM (P3b) |
| `POST /api/stt` | gadget id | WAV → `{text, provider}` (P3b) |

`sendId = "gdt" + createHash("sha256").update(`${gadgetId}:${sessionId}:${turn}`).digest("base64url").slice(0, 40)` (43 characters). `threadId` is always sent explicitly: the bot's `WireBot.threadId` at the start of the turn, pinned for the turn. That includes the queued-turn stop: on origin/main `server/index.ts:21879-21889` reads `body.threadId` for `DELETE …/queue/:queueId` (`requirePinnedClientThread` answers 409 to a companion call without it on a bot with several threads, and `requestedTaskBot` would otherwise look in the selected thread).

**Pinned timeouts** (end to end, all inside the hub's STT budget): `json()` calls use the 10 000 ms default (including `POST /api/tts/prepare`); the hub's `POST /api/stt` `raw()` uses 60 000 ms; the hub's `POST /api/tts/speak` `raw()` uses 30 000 ms. Inside `/api/stt` the harness calls `transcribeWithApple(…, {timeoutMs: 20_000})` (its own deadline is timeoutMs + 5 s = 25 s) and then, when it falls back, `transcribeWithScribe(…, {timeoutMs: 30_000})`, so the worst case (55 s) ends before the hub gives up.

### 3.8 `companion/src/gadget/shape.ts` (P3a)

```ts
// companion/src/gadget/shape.ts (P3a) — reply text → screen text (spec §4.4).
/** Companion-local, screen-oriented adaptation of server/tts/speech-text.ts speakable():
 *  fenced code → "[code]", images → "[image]", [label](url) → label, bare URLs → "[link]",
 *  inline code ≤ 40 chars kept else "[code]", heading/list/quote/emphasis markers stripped,
 *  table rows → cells joined by ", ", emoji removed, newlines kept (3+ collapse to 2). */
export declare function shapeReply(markdown: string): string;
/** Fold to the gadget charset: U+000A, U+0020–U+007E, U+00A0–U+00FF, U+2026, U+2192 kept;
 *  ‘ ’ ‚ → ', “ ” „ → ", – — − → -, • → ·, ← → <-, NBSP → space, zero-width removed;
 *  other characters → NFKD base letters when those are all in range, else dropped. */
export declare function foldLatin1(text: string): string;
/** shapeReply (when markdown) then foldLatin1, per caps.screen.text (absent = "latin1"). */
export declare function screenText(text: string, options: { markdown: boolean; charset?: "latin1" }): string;
/** Keep the tail so the encoded frame stays ≤ maxBytes: cut from the start and prefix "…". */
export declare function cutFromStart(text: string, maxBytes: number): string;
```
### 3.9 `companion/src/gadget/enroll.ts` (P3a)

```ts
// companion/src/gadget/enroll.ts (P3a) — spec §4.2–§4.3 host rules, Node crypto only.
import type { Buffer } from "node:buffer";
import type { DeviceRegistry, GadgetDeviceRecord } from "../devices.ts";
import type { ChallengeMsg, GadgetCaps, GadgetErrorCode, GadgetSensors, HelloMsg, ProveMsg, Risk } from "./protocol.ts";

export interface NormalizedAction {
  name: string;               // ACTION_NAME_RE
  description: string;        // ≤ 200 chars
  params: Record<string, unknown>;   // ≤ 1 KiB serialized
  risk: Risk;                 // missing → "confirm"
  entryHash: string;          // actionEntryHash(...)
}
export interface NormalizedHello {
  proto: number;
  id: string;
  pubkey: string;
  pubkeyBytes: Buffer;        // 65 bytes
  name: string;               // control chars removed, cut to 32 chars, "" → "Maus " + id.slice(4, 8)
  board: string;
  fw: string;
  caps: GadgetCaps;
  actions: NormalizedAction[];   // at most 16; any action breaking a limit is dropped
  sensors: GadgetSensors;
}
export type HelloCheck =
  | { ok: true; hello: NormalizedHello }
  | { ok: false; code: "proto_unsupported" | "bad_sig"; message: string };
/** proto must be 1; pubkey canonical base64 of 65 bytes starting 0x04 hashing to id; board BOARD_ID_RE. */
export declare function checkHello(msg: HelloMsg): HelloCheck;
/** Fresh 32-byte nonce per connection. hostName is used as given; the caller folds it first:
 *  P3a calls createChallenge(hostId, <screenText(machineName(), {markdown: false}) cut to 64 UTF-8
 *  bytes on a code-point boundary, keeping the start>), because the gadget shows and stores
 *  challenge.host_name (Setup, Offline, @omb status) and has only Latin-1 glyphs (contract D20). */
export declare function createChallenge(hostId: string, hostName: string): ChallengeMsg;

export type ProveResult =
  | { ok: true; device: GadgetDeviceRecord; enrolled: boolean; sendName?: string }
  | { ok: false; code: GadgetErrorCode; message: string }
  | { ok: false; code: "internal"; message: string };     // save failed: close 1011, no error op
/** Verifies sig over proveText(id, challenge.nonce, challenge.host_id); then rule 2 (known id:
 *  stored key must equal pubkey, enroll ignored, registry.noteGadgetHello), rule 3 (unknown id
 *  with enroll: registry.enrollGadget with botId from window.botId ?? resolveBot()), or rule 4
 *  (enroll_required). bad_code messages say "wrong", "expired" or "used up". */
export declare function completeProve(input: {
  hello: NormalizedHello;
  challenge: ChallengeMsg;
  prove: ProveMsg;
  registry: DeviceRegistry;
  resolveBot: () => Promise<string | null>;
}): Promise<ProveResult>;
```
### 3.10 `companion/src/gadget/session.ts` seams (P3a)

```ts
// companion/src/gadget/session.ts (P3a) — one live gadget connection after `ready`.
// Only the seams other plans use are pinned here; the §6.2 mapping is P3a-internal.
import type { Buffer } from "node:buffer";
import type { NormalizedHello } from "./enroll.ts";
import type { HarnessClient } from "./harness-client.ts";
import type { BinaryKindValue, GadgetOp, GadgetToHost, HostToGadget } from "./protocol.ts";

export interface GadgetSessionHandle {
  readonly deviceId: string;
  readonly sessionId: string;            // "s_" + 12 hex, sent in ready.session
  readonly hello: NormalizedHello;       // caps and actions as declared on this connection
  readonly connectedAt: number;
  readonly closed: boolean;
  /** Text frame; strings for the screen must already be folded (shape.ts). false once closed. */
  send(msg: HostToGadget): boolean;
  /** Binary frame through ws.sendBinaryDrained (waits for drain above 64 KiB buffered). */
  sendBinary(kind: BinaryKindValue, stream: number, payload: Uint8Array): Promise<boolean>;
  /** Host-assigned stream id 1–255 not currently in use; release it when the stream ends. */
  allocStream(): number;
  releaseStream(stream: number): void;
  /** Observe gadget → host ops after the session's own handling (act.result, fw.*, …). */
  on<K extends GadgetOp>(op: K, listener: (msg: Extract<GadgetToHost, { op: K }>) => void): () => void;
  onClose(listener: (code: number) => void): () => void;
}

// ---- voice seams: P3a ships the defaults, P3b supplies the real ones -------
export type SttOutcome =
  | { ok: true; text: string; provider: "apple" | "elevenlabs" }   // text "" → done failed "Didn't catch that"
  | { ok: false; reason: string };                                 // reason (already Latin-1) becomes the done reason
export type SttFn = (input: { deviceId: string; pcm: Buffer; rate: 16000 }) => Promise<SttOutcome>;

export interface SpeechOut {
  /** Final reply text (RAW, unshaped) for `turn`: prepare, synthesize, stream on one speech stream. */
  replyFinal(input: { turn: string; rawText: string; voiceId?: string }): void;
  /** A post with speak:true. Queued host-side; its speak.begin is sent only after the earlier
   *  stream's host-side playout end (pacer.drained()) plus SPEECH_GAP_MS (300 ms, private to
   *  speech.ts), so a spoken push never cuts a reply's tail. */
  post(input: { rawText: string; voiceId?: string }): void;
  /** Turn stopped, new turn, barge-in. Sends speak.stop {stream} synchronously through
   *  session.send before it returns, only for a stream that is playing, queued or in its gap
   *  (so the hub can then send the old turn's done and the new turn's first message in that
   *  order), then drops queued audio. With nothing playing it is a no-op that sends no frame
   *  (the hub calls it at the start of every turn). */
  stop(): void;
  close(): void;
}
export interface SpeechContext {
  session: GadgetSessionHandle;
  rate: 16000 | 24000 | null;            // from caps.speaker.rate; null = no speaker (text only)
  harness: HarnessClient;
  log?: (line: string) => void;
}
export type SpeechOutFactory = (context: SpeechContext) => SpeechOut;
export type VoiceProvider = (harness: HarnessClient) => { stt: SttFn; speech: SpeechOutFactory };

/** P3a default: every voice turn ends with the stt_unavailable copy; replies are text only. */
export declare const textOnlyVoice: VoiceProvider;
```
### 3.11 `companion/src/gadget/hub.ts` (P3a)

```ts
// companion/src/gadget/hub.ts (P3a)
import type { IncomingMessage } from "node:http";
import type { Duplex } from "node:stream";
import type { Buffer } from "node:buffer";
import type { DeviceRegistry } from "../devices.ts";
import type { GadgetSessionHandle, VoiceProvider } from "./session.ts";

export interface GadgetEventRecord { name: string; data?: unknown; at: number }

export interface GadgetHubOptions {
  devices: DeviceRegistry;
  harnessPort: number;
  mutationToken?: () => string | null;   // parentPort ? () => mutationToken : undefined
  hostId: string;                        // loadOrCreateHostId()
  hostName: () => string;                // machineName; the hub folds and cuts it before createChallenge (§3.9)
  /** connectedDevices.open: presence (online dot) + revocation terminates the session. */
  connected: (deviceId: string, terminate: () => void) => () => void;
  voice?: VoiceProvider;                 // P3b: createGadgetVoice; default textOnlyVoice
  /** P4a: presence notice to the harness. Called after a successful enroll and after revoke. */
  onDevicesChanged?: (change: { kind: "enrolled" | "removed"; deviceId: string }) => void;
  log?: (line: string) => void;
  now?: () => number;
}

export interface GadgetHub {
  /** new URL(rawUrl ?? "/", "http://companion.invalid").pathname === "/gadget" */
  isGadgetPath(rawUrl: string | undefined): boolean;
  /** Attached only to the 0.0.0.0:8810 server's 'upgrade', before proxy.upgrade; never to managedOrigin. */
  handleUpgrade(req: IncomingMessage, socket: Duplex, head: Buffer): void;
  session(deviceId: string): GadgetSessionHandle | null;
  online(): string[];
  /** Fires after ready is sent (and after any pending settings {name}). */
  onSessionReady(listener: (session: GadgetSessionHandle) => void): () => void;
  /** After PATCH /devices/:id/gadget: send settings {bot, settings, name?} to a live gadget;
   *  when it is offline and the name changed, mark namePending. */
  settingsChanged(deviceId: string, change: { nameChanged: boolean }): void;
  /** After DELETE /devices/:id: send error revoked and close a live session (also reached through
   *  connected()'s terminate), then call onDevicesChanged({kind: "removed"}) whether or not it was online. Idempotent. */
  revoke(deviceId: string): void;
  /** Last 10 `event` ops, newest last (spec §4.7). */
  recentEvents(deviceId: string): GadgetEventRecord[];
  /** Latest known bot name from the hub's bot map, or null. */
  botName(botId: string): string | null;
  /** Close code 1001 to every gadget, then destroy sockets after 1 s; stops the SSE stream. */
  close(): Promise<void>;
}
export declare function createGadgetHub(options: GadgetHubOptions): GadgetHub;
```
Hub rules other plans rely on:

- **Unbound gadgets (D13).** On every successful `prove` whose record has `botId === null`, the hub re-runs `defaultGadgetBot` over `GET /api/bots?messages=0` and, when that finds a bot, saves it with `devices.updateGadget(id, {botId})` before sending `ready`. A failed lookup (harness restarting) leaves the record unbound and `ready.bot` = `{"id": "", "name": ""}`; the next connection tries again.
- **Ask ids.** `ask.id` is derived as in §2.12 (`"a_"` + 16 hex of `sha256(threadId + ":" + requestId)`); the hub keeps `ask.id → {threadId, requestId}` while the ask is open and resolves `answer` through it. `gadgetAskKind` (§3.3) decides what kind of ask a card becomes.
### 3.12 Companion wiring in `companion/src/index.ts`

Each plan adds only its own lines, at these points (line numbers from 6dd4403):

| Point | P3a | P3b | P4a | P4b |
|---|---|---|---|---|
| parentPort listener (:45–56) | parse `message.gadgetControlToken` (43-char regex) into `let gadgetControlToken: string \| null`, falling back to `process.env.OMB_GADGET_CONTROL_TOKEN` (read once, then deleted from `process.env`, §3.1) when there is no parentPort; add `function onMutationToken(cb: () => void): void`, which runs `cb` once, the first time `mutationToken` becomes non-null (at once when it already is) | — | — | — |
| after `const devices = new DeviceRegistry()` (:126) | `const HOST_ID = loadOrCreateHostId();` | — | — | — |
| TXT (:152) | add `id=${HOST_ID}` | — | — | — |
| after `connectedDevices` (:155) | `const gadgetHub = createGadgetHub({devices, voice: undefined, harnessPort: HARNESS_PORT, mutationToken: parentPort ? () => mutationToken : undefined, hostId: HOST_ID, hostName: machineName, connected: connectedDevices.open, onDevicesChanged: undefined})`, one property per line in exactly this order: `voice: undefined,` directly follows `devices,`, and `onDevicesChanged: undefined,` is last | `voice: createGadgetVoice` | `onDevicesChanged: (c) => void notifyGadgetPresence({harnessPort: HARNESS_PORT, deviceId: c.deviceId, mutationToken: mutationToken ?? undefined})` | `const firmware = createGadgetFirmwareService({devices, hub: gadgetHub, checker: createReleaseChecker({keys: trustedReleaseKeys()})})` |
| upgrade (:175) | `companion.on("upgrade", (req, socket, head) => gadgetHub.isGadgetPath(req.url) ? gadgetHub.handleUpgrade(req, socket, head) : proxy.upgrade(req, socket, head))`; `managedOrigin` unchanged | — | — | — |
| `createControlServer({...})` (:179–202) | `hostId: HOST_ID, gadgetHub`; in `revoked`: call `gadgetHub.revoke(id)` before the mutation-token early return | — | `gadgetControlToken: () => gadgetControlToken` | `firmware` |
| `main()` after listen | — | — | once the control and LAN listeners are bound: `parentPort ? onMutationToken(sendStartNotice) : sendStartNotice()`, where `sendStartNotice` calls `notifyGadgetPresence({harnessPort: HARNESS_PORT, deviceId: "gadget-hub", mutationToken: mutationToken ?? undefined})` and, when it resolves `false`, retries after 2 s, 10 s and 30 s (then gives up). Under Electron the parentPort token may arrive after listen, and a notice sent without it gets 403 (a mutating route without companion auth); the harness's own `refresh()` on its token usually fails earlier still, because Electron starts the harness before it auto-starts the companion | — |
| `shutdown()` (:309) | `await gadgetHub.close()` before the `closeAllConnections` lines | — | — | `firmware.close()` |

**Anchors in `companion/src/index.ts` (§1.3).** P3b, P4a and P4b find each anchor with `grep -n` on `feat/gadget-hub` and edit exactly there; every two plans' edits keep at least one unchanged line between them:

| Where | P3b | P4a | P4b |
|---|---|---|---|
| imports | below `import { createGadgetHub } from "./gadget/hub.ts";` | below `import { notifyDeviceRevoked } from "./harness-notice.ts";` | below `import { loadOrCreateHostId, serviceTxt } from "./host-id.ts";` |
| `createGadgetHub({…})` | replaces the `voice: undefined,` line (directly after `devices,`) | replaces the `onDevicesChanged: undefined,` line (the last before `});`) | `const firmware = …` goes after the call's closing `});` |
| `createControlServer({…})` | — | its `gadgetControlToken` line goes after `gadgetHub,` | `firmware,` goes after `hostId: HOST_ID,` (the line inside `createControlServer({`; the `createGadgetHub` call has a line with the same text) |
| `shutdown()` | — | — | `firmware.close();` goes before `await gadgetHub.close();` |

### 3.13 Control port routes (`companion/src/control.ts`, loopback :8811)

```ts
// companion/src/control.ts — additions to the existing ControlOptions (each line names its plan).
import type { DeviceRegistry } from "./devices.ts";
import type { GadgetHub } from "./gadget/hub.ts";
import type { GadgetFirmwareService } from "./gadget/firmware.ts";
export interface ControlOptionsGadgetAdditions {
  devices: DeviceRegistry;                         // existing
  hostId?: string;                                 // P3a: companionState().hostId
  gadgetHub?: GadgetHub;                           // P3a: PATCH /devices/:id/gadget → hub.settingsChanged
  gadgetControlToken?: () => string | null;        // P4a: /gadget/* auth
  firmware?: GadgetFirmwareService;                // P4b: firmware routes + companionState().gadgetFirmware
}
```
The block lists the additions; it is not their order in the source. **Anchors in `companion/src/control.ts` (§1.3)**, found with `grep -n` on `feat/gadget-hub`:

- **Imports.** P4a's import goes after the `./devices.ts` import line. On origin/main that line is `import type { DeviceRegistry } from "./devices.ts";`; P3a's Task 12 rewrites it on `feat/gadget-hub` to `import { cleanGadgetName, type DeviceRegistry, type GadgetSettingsPatch } from "./devices.ts";`, so grep for `from "./devices.ts";`. P4b's import goes after `import type { GadgetHub } from "./gadget/hub.ts";` (P3a's line, two lines below).
- **`ControlOptions`.** P4a's member (`gadgetControlToken?: () => string | null;`) goes after `gadgetHub?: GadgetHub;`. P4b's `firmware?:` member goes directly after `publicNetworks?: () => ReadonlySet<string>;`, before P3a's `hostId` comment.

All bodies are JSON (`content-type: application/json`); responses use the existing `json()` helper. "State" means `companionState(options)`, which gains `hostId` (P3a), gadget records in `devices` (P3a), `pairing.botId` (P3a) and `gadgetFirmware` (P4b).

| Route | Owner | Caller | Auth | Request | Response |
|---|---|---|---|---|---|
| `POST /pairing` | P3a (extends) | Electron | existing loopback checks | optional `{botId?: string}` (`/^[\w-]{1,120}$/`; body ≤ 4096 bytes; empty body allowed) | 201 `{...state, code, token}` |
| `PUT /pairing/bot` | P3a (new) | Electron | existing | `{botId: string \| null, expectedToken: string}` | 200 state; 409 `{error: "no matching pairing window"}` |
| `PATCH /devices/:id/gadget` | P3a (new) | Electron | existing | `GadgetSettingsPatch` (at least one field) | 200 state; 400 `{error}`; 404 `{error: "no such gadget"}`; 500 `{error: "could not save gadget settings"}` |
| `POST\|DELETE /devices/:id/cloud-desktop`, `/browser-control` | existing | Electron | existing | — | 404 for gadget ids (registry returns false) |
| `DELETE /devices/:id` | existing | Electron | existing | — | also revokes gadgets (hub sends `error revoked`) |
| `GET /gadget/devices?botId=` | P4a | harness only | `x-openmausbot-gadget-control` | — | 200 `GadgetDevicesResponse` |
| `POST /gadget/display` | P4a | harness only | same | `GadgetDisplayRequest` | 200 `GadgetDisplayResponse` |
| `POST /gadget/act` | P4a | harness only | same | `GadgetActRequest` | 200 `GadgetActResponse` |
| `POST /devices/:id/firmware-update` | P4b | Electron | existing | `{}` | 202 state; 404/409 `{error, code}` (`no_gadget`, `offline`, `custom_build`, `no_update`, `busy`) |
| `POST /firmware-updates/check` | P4b | Electron | existing | `{}` | 202 state (the check runs in the background) |

`/gadget/*` auth (P4a): 503 `{error: "gadget control is not configured"}` when the companion holds no token; 403 `{error: "forbidden"}` when the header is missing or differs (`timingSafeEqual` on equal-length buffers). Checked before any other `/gadget/*` processing; `GET /state` and the other existing routes stay unauthenticated (spec §9 known limitation).

### 3.14 Voice (P3b)

Companion side:

```ts
// companion/src/gadget/audio.ts (P3b)
import type { Buffer } from "node:buffer";
/** RIFF/WAVE, fmt PCM (1), mono, 16-bit, `rate`, data = pcm (little-endian). */
export declare function wavFromPcm16(pcm: Uint8Array, rate: number): Buffer;
/** Real-time pacer for speaker PCM: frames of SPEAK_FRAME_MS (40 ms), never more than
 *  SPEAK_AHEAD_MS (500 ms) ahead of the playout clock; several utterances queue on one stream. */
export interface SpeechPacer {
  push(pcm: Uint8Array): void;          // append one utterance's PCM16LE
  end(): void;                          // no more audio for this stream
  /** Resolves when everything pushed has been handed to `send` and its playout time has passed. */
  drained(): Promise<void>;
  stop(): void;                         // drop everything now
}
export declare function createSpeechPacer(options: {
  rate: 16000 | 24000;
  send: (frame: Uint8Array) => Promise<boolean>;   // session.sendBinary(BinaryKind.speaker, stream, frame)
  now?: () => number;
}): SpeechPacer;
```
```ts
// companion/src/gadget/stt-client.ts (P3b)
import type { HarnessClient } from "./harness-client.ts";
import type { SttFn } from "./session.ts";
/** wavFromPcm16 → harness.raw("POST", "/api/stt", deviceId, wav, "audio/wav", 60_000).
 *  200 {text, provider} → ok; 409 stt_unavailable → STT_UNAVAILABLE_COPY; 502 stt_key_forbidden →
 *  body.message; anything else → "Speech-to-text failed". All reasons folded to Latin-1. */
export declare function createSttClient(harness: HarnessClient): SttFn;
export declare const STT_UNAVAILABLE_COPY: string;
```
```ts
// companion/src/gadget/speech.ts (P3b)
import type { SpeechOutFactory, VoiceProvider } from "./session.ts";
/** POST /api/tts/prepare {text: raw, voiceId} → utterances (json(), 10 s); POST /api/tts/speak
 *  {text, voiceId, format: "pcm_<rate>"} per utterance through raw(…, 30_000) (first plays while the rest synthesize);
 *  one speak.begin {stream, rate, turn?} … speak.end per reply; 409 once per session → card
 *  {id:"notice-voice", title:"Voice is off", body:"Add a voice in this bot's voice settings to hear replies.", ttl_s:8}
 *  and no more speech requests in that session; 415 pcm_unsupported → that reply text-only. */
export declare const createSpeechOut: SpeechOutFactory;
/** The hub's voice option: { stt: createSttClient(harness), speech: createSpeechOut }. */
export declare const createGadgetVoice: VoiceProvider;
```
Harness side:

```ts
// server/routes/stt.ts (P3b) — registered with ROUTES.push(createSttRoutes({...})) in server/index.ts.
import type { Buffer } from "node:buffer";
import type { ServiceCredential } from "../included-services.ts";
import type { RouteHandler } from "./table.ts";

export const STT_MAX_BYTES = 2 * 1024 * 1024;   // 413 {error:"too_large"} beyond, checked on content-length first
export type SttProvider = "apple" | "elevenlabs";
export interface SttRouteDeps {
  /** voiceCredential(cfg.tts?.key): the person's own ElevenLabs key (included only on a Cloud home). */
  elevenLabs(): ServiceCredential | null;
  /** macOS file-mode helper; undefined off darwin or when no helper bundle resolves. */
  apple?: (wavPath: string) => Promise<AppleSttResult>;
  fetch?: typeof fetch;                         // test seam for Scribe
  tmpDir?: () => string;                        // where the WAV is written for the helper
}
export type AppleSttResult =
  | { ok: true; text: string }                   // "" for no speech
  | { ok: false; error: "speech-not-authorized" | "recognizer-unavailable" | "dictation-disabled" | "recognition-error" | "timeout" | "file-unreadable" | "helper-failed" };
/** POST /api/stt, content-type audio/wav, RIFF mono PCM16 16 kHz ≤ 60 s.
 *  200 {text, provider}; 400 {error:"bad_audio", message}; 413 {error:"too_large"};
 *  409 {error:"stt_unavailable", message}; 502 {error:"stt_key_forbidden", message} | {error:"stt_failed", message}. */
export declare function createSttRoutes(deps: SttRouteDeps): RouteHandler;
export declare const STT_KEY_FORBIDDEN_MESSAGE: string;
export declare const STT_UNAVAILABLE_MESSAGE: string;
/** Parses a mono PCM16 16 kHz WAV (walking RIFF chunks); null when it is anything else. */
export declare function pcmFromWav16k(wav: Uint8Array): Buffer | null;
```
```ts
// server/stt/apple.ts (P3b)
import type { AppleSttResult } from "../routes/stt.ts";
/** OMB_SPEECH_HELPER_PATH → join(OMB_RESOURCES_PATH, "OpenMausBot Speech.app") → dev
 *  electron/resources/OpenMausBot Speech.app; first whose Contents/MacOS/speech-helper exists; null off darwin. */
export declare function resolveSpeechHelper(env?: NodeJS.ProcessEnv): string | null;
/** spawn("/usr/bin/open", ["-n","-g","-W","-o",out,"--stderr",err,bundle,"--args","--file",wav,"--stop-file",stop,"--timeout-ms",String(timeoutMs)]),
 *  then parse the last NDJSON line of `out`; harness-side deadline timeoutMs + 5000 writes the stop file. */
export declare function transcribeWithApple(bundle: string, wavPath: string, options?: { timeoutMs?: number }): Promise<AppleSttResult>;
```
```ts
// server/stt/scribe.ts (P3b) — ElevenLabs Scribe v2.
import type { ServiceCredential } from "../included-services.ts";
/** POST `${cred.api}/speech-to-text?enable_logging=false`, header xi-api-key, multipart:
 *  model_id=scribe_v2, file_format=pcm_s16le_16, tag_audio_events=false, file=<raw PCM16LE 16 kHz mono>.
 *  The query string and the one retry follow Omkar's answer to D-P3b-1 (contract §3.14). */
export declare function transcribeWithScribe(pcm: Uint8Array, cred: ServiceCredential, options?: { fetch?: typeof fetch; timeoutMs?: number }):
  Promise<{ ok: true; text: string } | { ok: false; status: number; forbidden: boolean; message: string }>;
```
**`enable_logging=false` (P3b deviation D-P3b-1, Omkar's answer pending; recommendation: A).** ElevenLabs documents zero-retention mode as enterprise-only and does not say what it does for other accounts. Under **(A)** the request stays as written, plus **one** retry without `enable_logging`, inside the same 30 s deadline, **only** when the refusal names it: status 400, 401, 403 or 422 with a body matching `/retention|enable_logging|logging/i`. Any other refusal is a failure after exactly one request, so the audio is never re-sent with logging on for an unrelated error. Under **(B)** the URL becomes `${cred.api}/speech-to-text` (no query; normal retention, like the TTS text the app already sends). Under **(C)** the request stays exactly as pinned and any refusal is a failure.
```ts
// server/tts/index.ts — P3b additions to the existing module.
export type SpeechFormat = "mp3" | "pcm_16000" | "pcm_24000";
export declare const SPEECH_FORMATS: ReadonlySet<SpeechFormat>;
export interface Audio { bytes: Uint8Array; mime: string }
/** Existing signature gains a 5th parameter; positional callers keep mp3. For pcm_* each provider
 *  is asked natively: ElevenLabs output_format=pcm_16000|pcm_24000; Fish format "wav" + sample_rate;
 *  xAI output_format {codec:"wav", sample_rate}; macOS say --data-format=LEI16@<rate>; Chatterbox WAV.
 *  Still throws NoVoiceConfigured synchronously. */
export declare function speak(cfg: unknown, text: string, voiceId?: string, run?: unknown, format?: SpeechFormat): Promise<Audio>;
```
```ts
// server/tts/pcm.ts (P3b)
import type { Buffer } from "node:buffer";
import type { Audio } from "./index.ts";
export interface Pcm16 { rate: number; samples: Int16Array }   // mono
/** Walks RIFF chunks (macOS say writes JUNK and FLLR before data); PCM16 only; downmixes to mono;
 *  a data size of 0xFFFFFFFF means "to the end". Throws on anything else. */
export declare function parseWav(bytes: Uint8Array): Pcm16;
/** Linear resampler for PCM16. */
export declare function resamplePcm16(pcm: Pcm16, rate: number): Pcm16;
/** Provider audio → raw PCM16LE mono at `rate`: audio/pcm with its rate= passes through (resampled
 *  when that rate differs), audio/wav is parsed and resampled; audio/pcm without rate=, audio/mpeg
 *  or anything else throws PcmUnsupported. */
export declare function toPcm16le(audio: Audio, rate: 16000 | 24000): Buffer;
export declare class PcmUnsupported extends Error {}
/** "audio/pcm;rate=<rate>;channels=1;bits=16;endian=little" */
export declare function pcmMime(rate: 16000 | 24000): string;
```
- **Route registration:** `import { createSttRoutes } from "./routes/stt.ts"` and `ROUTES.push(createSttRoutes({elevenLabs: () => voiceCredential(cfg.tts?.key), apple: …}))` next to the Live routes. No new path needles in `server/index.ts`.
- **Time budget (pinned, §3.7):** `apple` is `(wav) => transcribeWithApple(bundle, wav, {timeoutMs: 20_000})` (helper `--timeout-ms 20000`, harness deadline 25 s); the Scribe fallback calls `transcribeWithScribe(pcm, cred, {timeoutMs: 30_000})`. Worst case 55 s, inside the hub's 60 s `raw()` for `/api/stt`.
- **Allowlists:** `{ method: "POST", path: /^\/api\/stt$/ }` in `companion/src/routes.ts` `ALLOWED` (voice group) and `{ methods: ["POST"], path: /^\/api\/stt$/ }` in `server/request-auth.ts` `CLIENT_ALLOW` (voice group); test rows in `companion/test/routes.test.ts` and `server/request-auth.test.ts`.
- **`/api/tts/speak`:** the existing inline block is edited in place. Body `{text, voiceId?, format?}`; `format` absent or `mp3` → unchanged; `pcm_16000`/`pcm_24000` → `200`, `content-type: audio/pcm;rate=<rate>;channels=1;bits=16;endian=little`, raw PCM16LE mono; `PcmUnsupported` → `415 {error: "pcm_unsupported"}`; any other format → `400 {error: "format must be mp3, pcm_16000 or pcm_24000"}`; 409 (no voice) and 502 unchanged.
- **Copy (exact, Latin-1):** `STT_UNAVAILABLE_MESSAGE` = `Speech-to-text isn't set up. On a Mac, allow Speech Recognition for OpenMausBot (try dictation once), or add an ElevenLabs key in a bot's voice settings.`; `STT_KEY_FORBIDDEN_MESSAGE` = `Your ElevenLabs key can't use Speech to Text. Turn on its Speech to Text permission, or use a key without restrictions.`; empty transcript → the hub's done reason `Didn't catch that`.
- **Speech helper** (`electron/resources/speech-helper.swift`): new flags `--file <path>` and `--timeout-ms <n>` (default 30000, clamped 1000–120000), parsed like the existing flags. File mode runs before the existing `requestAuthorization` call and never returns into it: it checks `SFSpeechRecognizer.authorizationStatus() == .authorized` (else `fail("speech-not-authorized")`), picks the locale as today, uses `SFSpeechURLRecognitionRequest` with `shouldReportPartialResults = false`, on-device when supported, `addsPunctuation = true` under `#available(macOS 13, *)`, keeps `--stop-file` working, and prints exactly one line: `{"partial":false,"text":"…"}` (exit 0) or `{"error":"speech-not-authorized"|"recognizer-unavailable"|"dictation-disabled"|"recognition-error"|"no-speech"|"timeout"|"file-unreadable"}` (exit 1). `no-speech` (the recognizer's no-speech error on a silent file) maps to `{ok: true, text: ""}` in `apple.ts`.

### 3.15 Bot tools (P4a)

Companion side:

```ts
// companion/src/gadget/control-routes.ts (P4a) — /gadget/* on the loopback control port (:8811).
// The harness imports these types with `import type` (as request-auth.ts imports companion/src/routes.ts).
import type { IncomingMessage, ServerResponse } from "node:http";
import type { DeviceRegistry } from "../devices.ts";
import type { GadgetHub } from "./hub.ts";
import type { Risk } from "./protocol.ts";

export const GADGET_CONTROL_HEADER = "x-openmausbot-gadget-control";
export const GADGET_CONTROL_BODY_MAX = 1024 * 1024;   // POST /gadget/display (image) body cap

export interface GadgetDirectoryAction {
  name: string;
  description: string;
  params: Record<string, unknown>;
  risk: Risk;
  entryHash: string;            // actionEntryHash; echoed back in /gadget/act
}
export interface GadgetDirectoryEntry {
  id: string;
  name: string;
  board: string;
  firmware: string;
  online: boolean;
  botId: string | null;
  talksToYou: boolean;          // botId === the ?botId= query
  battery_pct?: number;
  charging?: boolean;
  screen?: { w: number; h: number; round: boolean; text: "latin1" };   // from the live hello; absent offline
  image?: { w: number; h: number };
  speaker: boolean;
  actions: GadgetDirectoryAction[];   // from the live hello; [] when offline
  sensors: Record<string, unknown>;   // lastSensors (live or persisted)
  recent_events: Array<{ name: string; data?: unknown; at: number }>;   // hub.recentEvents, data clamped to 512 bytes
}
/** GET /gadget/devices?botId=<id> → 200 */
export interface GadgetDevicesResponse { devices: GadgetDirectoryEntry[] }

/** POST /gadget/display — exactly one of card or image. */
export interface GadgetDisplayRequest {
  botId: string;
  device: string;
  card?: { title: string; body: string; ttl_s: number };          // companion folds/clamps: title 80, body 600 chars
  image?: { w: number; h: number; rgb565: string; ttl_s: number }; // base64 of w*h*2 bytes RGB565 LE; w,h ≤ caps.image
}
export type GadgetDisplayResponse = { ok: true; deviceName: string; id: string };

/** POST /gadget/act */
export interface GadgetActRequest {
  botId: string;
  device: string;
  name: string;
  args: Record<string, unknown>;
  risk: Risk;                   // the risk the harness approved under
  entryHash: string;            // the declared entry the harness approved
}
export type GadgetActResponse = { ok: boolean; data?: unknown; error?: string };

/** Error bodies on every /gadget/* route: {error: string, code?: string}.
 *  503 no token held · 403 header mismatch (timingSafeEqual on equal-length buffers) · 400 invalid body ·
 *  404 {code:"no_gadget"} / {code:"no_action"} · 409 {code:"offline"} / {code:"action_changed"} ·
 *  413 body too large · 504 {code:"timeout"} (no act.result within 15 s). */
export interface GadgetControlErrorBody { error: string; code?: "no_gadget" | "no_action" | "offline" | "action_changed" | "timeout" }

export interface GadgetControlDeps {
  devices: DeviceRegistry;
  hub: GadgetHub;
  token: () => string | null;   // gadgetControlToken (parentPort message or OMB_GADGET_CONTROL_TOKEN)
  log?: (line: string) => void;
}
/** Called by control.ts before its 404 fallthrough; true when it answered the request. */
export declare function handleGadgetControl(req: IncomingMessage, res: ServerResponse, url: URL, deps: GadgetControlDeps): boolean;
export declare function buildGadgetDirectory(deps: Pick<GadgetControlDeps, "devices" | "hub">, botId?: string): GadgetDirectoryEntry[];
```
```ts
// companion/src/gadget/presence.ts (P4a) — the companion → harness presence notice.
/** POST /api/gadgets/presence (a COMPANION_NOTICES route), headers
 *  {...companionIdentityHeaders(deviceId, mutationToken), "content-length": "0"}; deviceId is the
 *  enrolled/removed gadget's id, or "gadget-hub" at companion start. Best effort; never throws. */
export declare function notifyGadgetPresence(notice: { harnessPort: number; deviceId: string; mutationToken?: string; timeoutMs?: number }): Promise<boolean>;
```
`COMPANION_NOTICES` in `companion/src/routes.ts` gains `{ method: "POST", path: /^\/api\/gadgets\/presence$/ }`. `control.ts` calls `handleGadgetControl(req, res, requestUrl, {devices, hub: options.gadgetHub, token: options.gadgetControlToken})` for any path starting `/gadget/`, after the Host/Origin checks and before the 404.

Harness side:

```ts
// server/gadget-control.ts (P4a) — the harness's client for the companion's /gadget/* routes.
import type {
  GadgetActRequest, GadgetActResponse, GadgetDirectoryEntry, GadgetDisplayRequest, GadgetDisplayResponse,
} from "../companion/src/gadget/control-routes.ts";

export declare class GadgetControlError extends Error {
  readonly status: number;      // HTTP status from the companion, or 0 when it could not be reached
  readonly code?: string;
}
export interface GadgetControl {
  /** Cached "≥ 1 paired gadget" (online or not); false while no token is held. Synchronous. */
  pairedSnapshot(): boolean;
  /** GET /gadget/devices; updates the cache. Never throws. When it fails with status 0 (companion
   *  unreachable) while a token is held, it schedules another refresh() every 30 s until one
   *  succeeds (one timer at a time; a success or a cleared token stops it). */
  refresh(): Promise<void>;
  devices(botId: string): Promise<GadgetDirectoryEntry[]>;
  display(request: GadgetDisplayRequest): Promise<GadgetDisplayResponse>;
  /** Fetch timeout 17 s (the companion gives up at 15 s). */
  act(request: GadgetActRequest): Promise<GadgetActResponse>;
}
export declare function createGadgetControl(options: {
  port: () => number;                     // Number(process.env.OMB_COMPANION_CONTROL_PORT ?? 8811)
  token: () => string | undefined;        // gadgetControlToken from the parent message or OMB_GADGET_CONTROL_TOKEN
  fetch?: typeof fetch;
  timeoutMs?: number;
}): GadgetControl;
```
```ts
// server/gadget-image.ts (P4a)
import type { Buffer } from "node:buffer";
/** Decode PNG (pngjs 7, PNG.sync.read) or JPEG (jpeg-js 0.4.4, decode with useTArray), composite alpha
 *  over black, box-filter downscale to fit inside box keeping aspect (never upscale), pack RGB565 LE:
 *  ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3). GIF/WEBP/other → throws GadgetImageUnsupported (415). */
export declare function toGadgetImage(input: { bytes: Uint8Array; mime: "image/png" | "image/jpeg" }, box: { w: number; h: number }):
  { w: number; h: number; rgb565: Buffer };
export declare class GadgetImageUnsupported extends Error {}
```
```ts
// server/routes/gadgets.ts (P4a) — ROUTES.push(createGadgetPresenceRoutes({...})).
import type { RouteHandler } from "./table.ts";
/** POST /api/gadgets/presence: requires x-openmausbot-companion: 1 (request-auth already checked the
 *  companion token for COMPANION_NOTICES); calls onPresence(); 204. */
export declare function createGadgetPresenceRoutes(deps: { onPresence: () => void }): RouteHandler;
```
```ts
// server/routes/internal-gadgets.ts (P4a). NOT in ROUTES: server/index.ts calls it from inside its
// `/api/internal/` block, after the capability prelude, so the ratchet's path needles do not grow.
import type { RouteContext, PASS } from "./table.ts";
import type { GadgetControl } from "../gadget-control.ts";

export interface InternalGadgetContext extends RouteContext {
  sender: { id: string; name: string };            // internalSender (the calling bot)
  threadId: string;                                // internalCapability.threadId
  /** readInternalBody(): re-checks the capability and rejects mismatched fromBotId/fromThreadId. */
  readInternalBody(): Promise<Record<string, unknown>>;
  /** requireActiveInternalCapability(): throws when the turn ended. */
  requireActive(): void;
  /** Per-turn counter on InternalCapability (`gadgetActions`), capped at 20. */
  countAction(): boolean;                          // false when the cap is reached → 429
}
export interface InternalGadgetDeps {
  enabled: () => boolean;                          // !CLOUD_HOME && gadgetControlToken present
  control: GadgetControl;
  /** messageFileRootsForThread + attachmentVmForTurn + readBotImage (server/bot-attachment.ts). */
  readImage(ctx: InternalGadgetContext, path: string): Promise<{ bytes: Uint8Array; mime: "image/png" | "image/jpeg" }>;
  /** requestPeerApproval(approvalBus, sender, target, message, "gadget_action", threadId). */
  approve(ctx: InternalGadgetContext, target: { id: string; name: string }, message: string): Promise<"allow" | "deny" | "expired" | "cancelled">;
  compileSchema(schema: Record<string, unknown>): (value: unknown) => { ok: true } | { ok: false; error: string };  // compileToolSchema
}
export type InternalGadgetHandler = (ctx: InternalGadgetContext) => Promise<typeof PASS | void>;
/** GET /api/internal/gadgets · POST /api/internal/gadgets/display · POST /api/internal/gadgets/action */
export declare function createInternalGadgetRoutes(deps: InternalGadgetDeps): InternalGadgetHandler;
```
**Harness internal routes** (agents proxy → harness, per-turn bearer, `kind` `agents`):

| Route | Request | Response |
|---|---|---|
| `GET /api/internal/gadgets` | — | 200 `{devices: GadgetDirectoryEntry[]}` (names ≤ 32, descriptions ≤ 200, params ≤ 1 KiB, events clamped); 409 `{error: "No gadget is paired with this computer."}` when disabled or none paired |
| `POST /api/internal/gadgets/display` | `{fromBotId, fromThreadId, device?, title?, body?, image_path?, ttl_s?}`: exactly one of (`title` and `body`) or `image_path`; `ttl_s` 0–3600, default 30 | 200 `{ok: true, device, deviceName}`; 400 (shape, or several gadgets and no `device`: the error lists `id (name)` choices); 403 path outside the roots; 404 no such gadget / file; 409 offline; 413 image too large; 415 not PNG/JPEG |
| `POST /api/internal/gadgets/action` | `{fromBotId, fromThreadId, device?, name, args?}` (`args` default `{}`) | 200 `{ok: true, data?}`; 200 `{ok: false, error, approvalOutcome?, approvalSource?}` (denied/expired/cancelled card, or the gadget's own `act.result` failure); 400 args fail the action's schema (ajv message), the action's params schema uses a regular expression, or a `confirm` message is longer than 200 characters (approval gate below); 404 no such gadget/action; 409 offline or action changed; 429 more than 20 gadget actions this turn; 504 no answer in 15 s |

Device defaulting (display and action): the only paired gadget; else the single gadget whose `botId` is the calling bot; else 400 listing the choices.

**No regular expressions from a gadget:** an action whose params schema uses `pattern`, `patternProperties` or `"format": "regex"` anywhere inside it is refused with 400 before `compileToolSchema` (a param merely *named* `pattern` is fine). Device schemas are untrusted, and Ajv would otherwise run a gadget-supplied backtracking pattern on the harness's main thread against the bot's args. P2d's `AGENTS.md` tells makers.

**Approval gate:** risk `confirm` (or missing) always asks, also in Full access: `requestPeerApproval(approvalBus, sender, target, message, "gadget_action", threadId)` with `target.id` = `<deviceId>:<actionName>:<entryHash>` and `target.name` = the gadget's name, and `message` = `name + " " + JSON.stringify(args)`, never cut. The desktop card shows only the first 200 characters of a message, so a `confirm` action whose message is longer than 200 characters is refused with 400 ("These arguments are too long to show on an approval card (at most 200 characters with the action name). Send fewer or shorter arguments.") before any card, slot or act, and the person always sees every argument they approve (P4a deviation 2, approved). Because the gadget path must not be auto-approved, the call site passes a bus whose `autoApply` returns false (Full access does not apply; a desktop "Always allow" grant on that exact key does). `PeerAction` in `server/peer-approval-key.ts` becomes `"ask_bot" | "delegate_bot" | "post_to_room" | "gadget_action"`; `peer-approval.ts` adds `ACTION_VERB.gadget_action = "run an action on"`, quotes the target name for `gadget_action` like `post_to_room`, and includes `gadget_action` in `dismissStalePeerCards`. `requireActiveInternalCapability()` runs after the approval wait and after the act returns. `safe` actions and display calls never ask.

**Catalog** (`server/drivers/agents-catalog.ts`): `CatalogProfile.gadgets: boolean`; `catalogProfileFromEnv` sets `gadgets: env.OMB_GADGETS === "1"`; `export const GADGET_TOOL_NAMES = new Set(["gadget_devices", "gadget_display", "gadget_action"])`; filtered out unless `profile.gadgets` (external runtimes never get them). `agentsIntegration` env gains `OMB_GADGETS: !CLOUD_HOME && gadgetControl.pairedSnapshot() ? "1" : "0"`. `ToolCallContext.gadgets: boolean` with the second lock `"No gadget is paired with this computer."`. `server/agent-tool-policy.ts` adds `gadget_devices` to `READ_ONLY_AGENT_TOOL_NAMES`; `server/harness-capabilities.ts` `ENVELOPE` sets `gadgets: true`. Goldens: overlay profiles `direct+gadgets`, `room+gadgets`, `direct+skills+shared+voice+gadgets`, `room+own-thread+skills+shared+voice+gadgets` (env `OMB_GADGETS: "1"`); `FULL.direct` and `FULL.room` point at the two `…+gadgets` names; `external+everything` gains `OMB_GADGETS: "1"`; non-gadget profiles keep identical bytes; four new `BUDGET_BASELINE` entries.

**Tool schemas** (flat; no `oneOf`/`anyOf`/`$ref`; descriptions are P4a's, these are the shapes):

```json
[
  {"name": "gadget_devices",
   "inputSchema": {"type": "object", "additionalProperties": false, "properties": {}}},
  {"name": "gadget_display",
   "inputSchema": {"type": "object", "additionalProperties": false, "properties": {
     "device":     {"type": "string", "minLength": 1, "maxLength": 32},
     "title":      {"type": "string", "minLength": 1, "maxLength": 80},
     "body":       {"type": "string", "minLength": 1, "maxLength": 600},
     "image_path": {"type": "string", "minLength": 1, "maxLength": 4096},
     "ttl_s":      {"type": "integer", "minimum": 0, "maximum": 3600}}}},
  {"name": "gadget_action",
   "inputSchema": {"type": "object", "additionalProperties": false, "properties": {
     "device": {"type": "string", "minLength": 1, "maxLength": 32},
     "name":   {"type": "string", "minLength": 1, "maxLength": 32},
     "args":   {"type": "object", "additionalProperties": true}},
    "required": ["name"]}}
]
```

Tool results: `gadget_devices` → `JSON.stringify(body)` (compact); `gadget_display` → `Shown on <deviceName>.`; `gadget_action` → `JSON.stringify({ok: true, data})`, or an error result with the route's `error` (plus the approval outcome).

**Engine timeouts:** `server/drivers/codex.ts` `mountMcpServer` adds `-c mcp_servers.<agents server name>.tool_timeout_sec=960` for the harness-owned agents server (argv regression test); `server/drivers/claude.ts` sets `MCP_TOOL_TIMEOUT=960000` in the engine env unless the person already set it (env regression test).

**Token in the harness:** `server/index.ts`: `export let gadgetControlToken: string | null` (null under Electron until the message arrives), parsed by `gadgetControlTokenFrom(message)` and `takeGadgetControlTokenFromEnv(process.env)` in `server/gadget-control-token.ts` (P3a); `createGadgetControl({token: () => gadgetControlToken ?? undefined})`. The variable sits next to `companionMutationToken` and is exported only so P3a's branch passes `noUnusedLocals` before P4a reads it. P3a's Task 14 writes `const envGadgetControlToken = takeGadgetControlTokenFromEnv(process.env);` and `export let gadgetControlToken: string | null = DESKTOP_MANAGED ? null : envGadgetControlToken;`, and in `applyDesktopMutationTokenMessage` `const gadgetToken = gadgetControlTokenFrom(message); if (gadgetToken) gadgetControlToken = gadgetToken;`. `gadgetControlTokenFrom(message: Record<string, unknown>): string | null` applies the 43-character base64url regex to `message.gadgetControlToken`; `takeGadgetControlTokenFromEnv(env: NodeJS.ProcessEnv): string | null` reads `OMB_GADGET_CONTROL_TOKEN` once at startup (the token for a harness without a parent message) and always deletes it from `env`. P3a adds the variable, the file and the parse; P4a adds `void gadgetControl.refresh()` at the `if (gadgetToken)` line and reads the variable only through `createGadgetControl`'s `token` getter, which maps `null` to `undefined` (`GadgetControl`'s `token: () => string | undefined`).

**Images:** `server/bot-attachment.ts` gains `readBotImage(input: {path: string; roots: readonly string[]; guest?: {root: string; host: string}}): Promise<{bytes: Buffer; mime: "image/png" | "image/jpeg"}>` with the attach_file rules (`openMessageFile`, O_NOFOLLOW, roots = `messageFileRootsForThread(...)` minus `ATTACHMENTS_DIR`, Local VM mapping); `IMAGE_MAX_BYTES` (10 MB) → 413. Root `package.json` `dependencies` gain `"pngjs": "7.0.0"` and `"jpeg-js": "0.4.4"`; both licenses go into the app's notices.

### 3.16 OTA delivery (P4b)

```ts
// companion/src/gadget/release-keys.ts (P4b)
export interface ReleaseKey { id: string; pubkey: string }   // pubkey: base64 SEC1 (65 bytes)
/** Copied from the SDK's keys/release-<id>.pub.b64; empty until Omkar publishes r1. ids /^r\d+$/. */
export declare const RELEASE_KEYS: readonly ReleaseKey[];
/** The SDK's keys/test-t1.pub.b64. Trusted only when OMB_GADGET_TRUST_TEST_KEY === "1" (tests, dev). */
export declare const TEST_KEY_T1: ReleaseKey;
export declare function trustedReleaseKeys(env?: NodeJS.ProcessEnv): readonly ReleaseKey[];
```
```ts
// companion/src/gadget/releases.ts (P4b) — spec §8 host side.
import type { Buffer } from "node:buffer";
import type { ReleaseKey } from "./release-keys.ts";

export const MANIFEST_URL = "https://github.com/aivsomkar/openmausbot-gadget-sdk/releases/latest/download/manifest.json";
export const RELEASE_URL_PREFIX = "https://github.com/aivsomkar/openmausbot-gadget-sdk/releases/download/";
export const CHECK_INTERVAL_MS = 24 * 60 * 60 * 1000;

export interface ManifestBoard { url: string; size: number; sha256: string; sig: string; key_id: string }
export interface ReleaseManifest { version: string; boards: Record<string, ManifestBoard> }
/** Validates every field (§4.1 rules); throws on the first violation. The url rule is
 *  url === `${base}v${version}/openmausbot-gadget-${board}-${version}.bin`, where base defaults to
 *  RELEASE_URL_PREFIX. The checker passes base = new URL("./", manifestUrl).href only when the
 *  manifest URL is overridden (createReleaseChecker's manifestUrl or OMB_GADGET_MANIFEST_URL). */
export declare function parseManifest(json: unknown, options?: { base?: string }): ReleaseManifest;
/** SemVer 2.0.0 precedence: negative, 0, positive. */
export declare function compareVersions(a: string, b: string): number;
/** fw ends in "-dev", or does not match RELEASE_VERSION_RE. */
export declare function isCustomBuild(fw: string): boolean;
/** Update available: board in manifest, fw not custom, compareVersions(manifest.version, fw) > 0. */
export declare function updateFor(manifest: ReleaseManifest | null, board: string, fw: string): ManifestBoard | null;

export interface VerifiedImage { board: string; version: string; image: Buffer; size: number; sha256: string; sig: string; keyId: string }
export interface ReleaseChecker {
  /** Fetch MANIFEST_URL (or OMB_GADGET_MANIFEST_URL); keeps the last good manifest. Never throws. */
  check(): Promise<void>;
  latest(): { manifest: ReleaseManifest | null; checkedAt: number | null; error?: string; checking: boolean };
  /** Verify sig (firmwareText with the manifest board id) before downloading, then size and sha256 after. */
  download(board: string): Promise<VerifiedImage>;
}
export declare function createReleaseChecker(options: {
  keys: readonly ReleaseKey[];
  fetch?: typeof fetch;
  manifestUrl?: string;
  now?: () => number;
}): ReleaseChecker;
```
```ts
// companion/src/gadget/ota.ts (P4b) — spec §4.8 host side on one live session.
import type { GadgetSessionHandle } from "./session.ts";
import type { VerifiedImage } from "./releases.ts";
import type { FwFailCode } from "./protocol.ts";

export type FwPhase = "offering" | "sending" | "committing" | "restarting";
export type FwUpdateResult =
  | { ok: true }                                    // fw.commit sent; fw.installed arrives on a later session
  | { ok: false; code: FwFailCode | "closed" | "ready_timeout"; message: string };
/** fw.offer (stream from allocStream, board = manifest board id) → fw.ready within 10 s →
 *  4096-byte chunks with ≤ 64 KiB unacknowledged (acked by fw.progress offsets) → fw.commit. */
export declare function runFirmwareUpdate(session: GadgetSessionHandle, image: VerifiedImage,
  onProgress: (phase: FwPhase, offset: number, size: number) => void): Promise<FwUpdateResult>;
```
```ts
// companion/src/gadget/firmware.ts (P4b) — wires checker + OTA + /state for control.ts.
import type { DeviceRegistry } from "../devices.ts";
import type { GadgetHub } from "./hub.ts";
import type { ReleaseChecker } from "./releases.ts";

export type GadgetUpdatePhase =
  | "downloading" | "verifying" | "offering" | "sending" | "committing" | "restarting" | "installed" | "failed";
export interface GadgetUpdateStatus {
  phase: GadgetUpdatePhase;
  version: string;              // the version being installed
  offset?: number;
  size?: number;
  error?: string;               // Latin-1/English message for the row
  at: number;
}
/** Exposed as companionState().gadgetFirmware. */
export interface GadgetFirmwareState {
  latest: { version: string; boards: string[]; checkedAt: number } | null;
  checking: boolean;
  error?: string;
  updates: Record<string, GadgetUpdateStatus>;   // by device id; "installed" entries expire after 10 min
}
export interface GadgetFirmwareService {
  state(): GadgetFirmwareState;
  /** POST /firmware-updates/check: runs check() in the background when ≥ 1 gadget is paired. */
  requestCheck(): void;
  /** POST /devices/:id/firmware-update. Starts in the background. */
  startUpdate(deviceId: string):
    | { ok: true }
    | { ok: false; status: 404 | 409; code: "no_gadget" | "offline" | "custom_build" | "no_update" | "busy"; error: string };
  close(): void;
}
/** Also: daily check while ≥ 1 gadget is paired; hub.onSessionReady → listen for fw.installed and
 *  mark the update "installed" (registry firmware comes from the new hello). */
export declare function createGadgetFirmwareService(options: {
  devices: DeviceRegistry;
  hub: GadgetHub;
  checker: ReleaseChecker;
  log?: (line: string) => void;
}): GadgetFirmwareService;
```
- Check schedule: on companion start (when ≥ 1 gadget is paired), every 24 h after that, and on `POST /firmware-updates/check` (Electron calls it when Settings → Remote access mounts with ≥ 1 gadget row). No fetch at all while no gadget is paired.
- `fw.installed` handling: the firmware service listens on every ready session (`hub.onSessionReady`); the registry's `firmware` field updates from the new `hello` (P3a) and the update entry becomes `installed`.
- `fw.fail busy` (§6 D22): the gadget also answers `busy` while a recording is live or a turn is in flight. The entry becomes `failed` with the error `The gadget is busy. Try again when it is idle.`
- **Restart timeout on the new version (optional decision, P4b open question 4; Omkar's call).** If Omkar accepts it: when the restart timeout fires and the session that came back on the new version has been ready longer than `GADGET_PROBATION_MS` (300 000 ms, so the image has already marked itself valid and only the `fw.installed` frame was lost), the entry becomes `installed` ("Updated to …"), not failed with "did not confirm". Until then the entry fails with "did not confirm", and the next connection on the new firmware clears it.
- **SDK part (branch `p4b-ota`):** `tools/release/dev-release.ts --image <bin> --board <id> --version <v> --out <dir> [--port 0]` signs with `keys/test-t1.key.hex` (`key_id` `t1`), copies the image to `<dir>/v<v>/openmausbot-gadget-<board>-<v>.bin`, writes `<dir>/manifest.json` with `url` = `http://127.0.0.1:<port>/v<v>/openmausbot-gadget-<board>-<v>.bin`, and serves `<dir>` at `http://127.0.0.1:<port>/` (so the manifest is `/manifest.json`). With `OMB_GADGET_MANIFEST_URL=http://127.0.0.1:<port>/manifest.json OMB_GADGET_TRUST_TEST_KEY=1`, `parseManifest`'s base is `http://127.0.0.1:<port>/` and the URLs pass the §4.1 rule.
- **Full update against the simulator:** build the simulator with `-DGADGET_SIM_VERSION=1.0.0` (a `-dev` version is a custom build, and `startUpdate` answers `custom_build`), pair it, then run `dev-release.ts --version 1.0.1 --board <the simulator's --board> --image <any file ≤ 6291456 bytes>` and press Update.

### 3.17 Electron

`electron/main.mjs` (P3a unless marked):

```js
// next to companionMutationToken (:312)
const gadgetControlToken = /^[A-Za-z0-9_-]{43}$/.test(process.env.OMB_GADGET_CONTROL_TOKEN ?? "")
  ? process.env.OMB_GADGET_CONTROL_TOKEN
  : randomBytes(32).toString("base64url");
delete process.env.OMB_GADGET_CONTROL_TOKEN;   // children get the token only by parentPort (§3.1)
// companionLaunchOptions (:729-741) gains: gadgetControlToken,
// syncDesktopMutationToken (:1238-1248) posts:
proc.postMessage({ type: "openmausbot:desktop-mutation-token", token: desktopMutationToken,
                   companionToken: companionMutationToken, gadgetControlToken });
// packaged harness childEnv (≈:1304) gains: OMB_COMPANION_CONTROL_PORT: "8811",
// and next to `delete childEnv.OMB_BROWSER_CONNECTION` (:1320): delete childEnv.OMB_GADGET_CONTROL_TOKEN;
```

`electron/companion.mjs` `start()` destructures `gadgetControlToken`, leaves `OMB_GADGET_CONTROL_TOKEN` out of the env it starts the companion with, and posts:

```js
child.postMessage({ type: "openmausbot:companion-mutation-token", token: mutationToken, gadgetControlToken });
```

New `electron/companion.mjs` exports (ids validated with `/^[\w-]{1,64}$/`, bot ids with `/^[\w-]{1,120}$/`; each returns `companionState()` like the existing helpers):

| Function | Owner | Control call |
|---|---|---|
| `companionGadgetSettings(deviceId, patch)` | P3a | `PATCH /devices/:id/gadget` with only the valid fields of `{botId, speakPushes, name}` |
| `companionPairGadget(botId)` | P3a | `POST /pairing` with `{botId}` (applies `managedRemoteAccessRefusal()` in main like `companion:pairing`) |
| `companionPairingBot(botId, expectedToken)` | P3a | `PUT /pairing/bot` |
| `companionFirmwareUpdate(deviceId)` | P4b | `POST /devices/:id/firmware-update` through its own `fetch` (not the shared `control()` helper, which throws on any status other than 2xx and 404 without reading the body): 202 → `companionState()`; 404/409 → `{...await companionState(), firmwareRefusal: {deviceId, code, error}}` from the `{error, code}` body; anything else throws like `control()` |
| `companionFirmwareCheck()` | P4b | `POST /firmware-updates/check` |

IPC channels (`ipcMain.handle(…, localOnly(…))`), all registered **after** the `companion:revoke` handler (the browser-control…revoke slice is pinned by `electron/companion-browser.node-test.mjs`), each resolving to `desktopCompanionState()` (pairing ones to `decorateDesktopCompanionState(...)`):

| Channel | Owner | Preload (`window.ogb.companion.*`) |
|---|---|---|
| `companion:gadget` | P3a | `gadget: (deviceId, patch) => ipcRenderer.invoke("companion:gadget", deviceId, patch)` |
| `companion:pair-gadget` | P3a | `pairGadget: (botId) => ipcRenderer.invoke("companion:pair-gadget", botId)` |
| `companion:pairing-bot` | P3a | `pairingBot: (botId, expectedToken) => ipcRenderer.invoke("companion:pairing-bot", botId, expectedToken)` |
| `companion:firmware-update` | P4b | `firmwareUpdate: (deviceId) => ipcRenderer.invoke("companion:firmware-update", deviceId)` |
| `companion:firmware-check` | P4b | `firmwareCheck: () => ipcRenderer.invoke("companion:firmware-check")` |

The `companion:firmware-update` handler resolves to `{...await desktopCompanionState(), firmwareRefusal}` when the helper returned a `firmwareRefusal`, so the Update cell can show why nothing started. `electron/preload.node-test.mjs` gains the matching invocation rows.

### 3.18 Renderer

```ts
// src/components/PhoneSetupFlow.tsx — renderer types (P3a; P4b adds the marked members).
// The renderer keeps its own copies of companion types (as it already does for PhoneDevice);
// GadgetUpdateStatus and GadgetFirmwareState below mirror §3.16 field for field (P4b).
export interface GadgetUpdateStatus {
  phase: "downloading" | "verifying" | "offering" | "sending" | "committing" | "restarting" | "installed" | "failed";
  version: string;
  offset?: number;
  size?: number;
  error?: string;
  at: number;
}
export interface GadgetFirmwareState {
  latest: { version: string; boards: string[]; checkedAt: number } | null;
  checking: boolean;
  error?: string;
  updates: Record<string, GadgetUpdateStatus>;
}

export interface PhoneDevice {
  kind?: "phone";
  id: string;
  name: string;
  createdAt: number;
  lastSeenAt: number;
  cloudDesktopAccess: boolean;
  browserControlAccess?: boolean;
}
export interface GadgetSensorsView { battery_pct?: number; charging?: boolean; [key: string]: unknown }
export interface GadgetDevice {
  kind: "gadget";
  id: string;
  name: string;
  createdAt: number;
  lastSeenAt: number;
  publicKey: string;
  board: string;
  firmware: string;
  botId: string | null;
  speakPushes: boolean;
  lastSensors?: GadgetSensorsView;
  namePending?: true;
}
export type PairedDevice = PhoneDevice | GadgetDevice;
export interface GadgetSettingsPatch { botId?: string | null; speakPushes?: boolean; name?: string }

export interface CompanionStateGadgetFields {
  devices: PairedDevice[];                      // was PhoneDevice[]
  pairing: { code: string; token: string; expiresAt: number; botId?: string } | null;
  hostId?: string;                              // P3a
  gadgetFirmware?: GadgetFirmwareState;         // P4b
  /** P4b: present only on the result of firmwareUpdate() when the companion refused (404/409). */
  firmwareRefusal?: { deviceId: string; code: "no_gadget" | "offline" | "custom_build" | "no_update" | "busy"; error: string };
}
export interface CompanionBridgeGadgetMethods {
  gadget: (deviceId: string, patch: GadgetSettingsPatch) => Promise<unknown>;            // P3a → companion:gadget
  pairGadget: (botId: string | null) => Promise<unknown>;                                // P3a → companion:pair-gadget
  pairingBot: (botId: string | null, expectedToken: string) => Promise<unknown>;         // P3a → companion:pairing-bot
  firmwareUpdate: (deviceId: string) => Promise<unknown>;                                // P4b → companion:firmware-update
  firmwareCheck: () => Promise<unknown>;                                                 // P4b → companion:firmware-check
}
```
```ts
// src/lib/gadgets.ts (P3a; P4b adds the update helpers) — pure helpers, unit-tested.
import type { GadgetDevice, GadgetFirmwareState, GadgetUpdateStatus, PairedDevice } from "../components/PhoneSetupFlow.ts";

export declare function isGadget(device: PairedDevice): device is GadgetDevice;
/** {"amoled-175c":"ESP32-S3 AMOLED 1.75C","amoled-175":"ESP32-S3 AMOLED 1.75","lcd-154":"ESP32-S3 LCD 1.54","devkit":"ESP32-S3 DevKitC"}; unknown ids pass through. */
export declare function boardLabel(board: string): string;
export declare function countDevicesByKind(devices: readonly PairedDevice[], connectedIds: readonly string[]):
  { phones: number; phonesOnline: number; gadgets: number; gadgetsOnline: number };
/** Same rule as companion types.ts defaultGadgetBot, over the renderer's visible bots. */
export declare function defaultGadgetBotId(bots: ReadonlyArray<{ id: string; hidden?: boolean; pinned?: boolean; section?: string; chiefOfStaff?: boolean }>): string | null;
// P4b
export declare function isCustomBuild(fw: string): boolean;
export type GadgetUpdateCellState =
  | { kind: "custom" }
  | { kind: "current" }
  | { kind: "available"; version: string }
  | { kind: "updating"; status: GadgetUpdateStatus }
  | { kind: "installed"; version: string }
  | { kind: "failed"; error: string };
export declare function updateCellState(device: GadgetDevice, firmware: GadgetFirmwareState | undefined): GadgetUpdateCellState;
/** True while any gadget is mid-update; PhoneSetupFlow's shouldPoll then polls every second (§5.2). */
export declare function hasActiveGadgetUpdate(state: { gadgetFirmware?: GadgetFirmwareState } | null | undefined): boolean;
```
- **Components:** `src/components/PairGadgetPanel.tsx` (P3a: six digits large, "Talks to [bot ▾]" `<select>` starting on `defaultGadgetBotId`, calling `pairingBot` on change, the LAN address `state.lan:state.port`, the existing Public-network hint `phone.code.publicNetwork`, disabled with "Turn on Remote access first" while the companion is off); `src/components/GadgetRow.tsx` (P3a: `GadgetRow({device, online, bots, busy, onBot, onSpeakPushes, onRename?, onRemove, updateCell?})`; icon, name, `boardLabel` · firmware, battery, online dot from `connectedDeviceIds`, Talks to, Read pushes aloud, Remove; prop `onRename?: (name: string) => void`, the desktop rename of spec §4.3 and §6.4 (an inline name field, `maxLength` 32, aria-label `remote.gadgets.nameAria`, saved on Enter or blur; `CompanionSection` wires it to `companion.gadget(id, {name})`), rendered before Remove; P4b's prop `updateCell?: ReactNode` goes after Remove. P4b's `CompanionSection` test mocks copy P3a's, so they rely on `onRename` and the P3a keys below); `src/components/GadgetUpdateCell.tsx` (P4b: Update / Updating… / Custom build / Update available / failed). `CompanionSection.tsx` renders the "Pair a gadget" button and a "Gadgets" group directly under it, outside "Advanced & troubleshooting", and keeps phone rows where they are, filtered with `!isGadget`.
- `SidebarPhoneButton.tsx` and CompanionSection's status chip count by kind with `countDevicesByKind`. `PhoneSetupFlow`'s success copy names a gadget when the newly paired device is a gadget. `SettingsModal.tsx` adds `gadget` to the Remote access keywords.
- **i18n keys** (`src/locales/en.json` only; other packs fall back; avoid the words "workspace" and "organisation"):

| Key | English | Owner |
|---|---|---|
| `remote.gadgets.title` | `Gadgets` | P3a |
| `remote.gadgets.pair` | `Pair a gadget` | P3a |
| `remote.gadgets.pairNeedsRemote` | `Turn on Remote access first` | P3a |
| `remote.gadgets.pairBody` | `Enter this code in the gadget installer or on the gadget's console.` | P3a |
| `remote.gadgets.codeExpires` | `Code expires in {seconds} s` | P3a |
| `remote.gadgets.address` | `If the installer can't find this computer, enter {address}` | P3a |
| `remote.gadgets.talksTo` | `Talks to` | P3a |
| `remote.gadgets.talksToAria` | `Bot that {name} talks to` | P3a |
| `remote.gadgets.unknownBot` | `Unknown bot` | P3a |
| `remote.gadgets.empty` | `No gadgets are paired yet.` | P3a |
| `remote.gadgets.online` / `.offline` | `Online` / `Offline` | P3a |
| `remote.gadgets.firmware` | `{board} · firmware {version}` | P3a |
| `remote.gadgets.battery` / `.charging` | `Battery {percent}%` / `Charging` | P3a |
| `remote.gadgets.speakPushes` | `Read pushes aloud` | P3a |
| `remote.gadgets.speakPushesDetail` | `Speak routine results and messages the bot sends on its own.` | P3a |
| `remote.gadgets.speakPushesAria` | `Read pushes aloud on {name}` | P3a |
| `remote.gadgets.remove` | `Remove {name}` | P3a |
| `remote.gadgets.pairedToast` | `{name} is paired` | P3a |
| `remote.gadgets.pairedDetail` | `It talks to the bot shown under Gadgets in Remote access.` | P3a |
| `remote.gadgets.cancel` | `Stop pairing` | P3a |
| `remote.gadgets.nameAria` | `Name of {name}` | P3a |
| `remote.status.devicesOne` / `.devicesMany` | `1 device paired` / `{count} devices paired` | P3a |
| `remote.status.gadgetsOne` / `.gadgetsMany` | `1 gadget paired` / `{count} gadgets paired` | P3a |
| `remote.gadgets.update` | `Update to {version}` | P4b |
| `remote.gadgets.updateAvailable` | `Update available` | P4b |
| `remote.gadgets.updating` | `Updating… {percent}%` | P4b |
| `remote.gadgets.updated` | `Updated to {version}` | P4b |
| `remote.gadgets.updateFailed` | `Update failed: {reason}` | P4b |
| `remote.gadgets.customBuild` | `Custom build` | P4b |
| `remote.gadgets.customBuildDetail` | `Built from source. Update it over USB.` | P4b |

### 3.19 Shared app test helpers (P3a; reused by P3b, P4a, P4b)

```ts
// companion/test/gadget/helpers/gadget-client.ts (P3a; reused by P3b, P4a, P4b) — a TS gadget for tests.
import type { GadgetActionDecl, GadgetCaps, GadgetToHost, HostToGadget, BinaryKindValue } from "../../../src/gadget/protocol.ts";

export interface TestGadget {
  readonly id: string;
  readonly pubkey: string;
  /** The ready frame; null when the handshake was refused. */
  readonly ready: Extract<HostToGadget, { op: "ready" }> | null;
  /** The error frame when the handshake was refused (its code is what refusal tests assert); else null. */
  readonly error: Extract<HostToGadget, { op: "error" }> | null;
  send(msg: GadgetToHost): void;
  sendBinary(kind: BinaryKindValue, stream: number, payload: Uint8Array): void;
  /** Next text frame with this op (frames are buffered from connect). Rejects after timeoutMs (default 5000). */
  next<K extends HostToGadget["op"]>(op: K, timeoutMs?: number): Promise<Extract<HostToGadget, { op: K }>>;
  nextBinary(kind?: BinaryKindValue, timeoutMs?: number): Promise<{ kind: BinaryKindValue; stream: number; payload: Uint8Array }>;
  closed(): Promise<{ code: number; reason: string }>;
  close(code?: number): void;
}
export interface TestGadgetOptions {
  port: number;                          // companion port (tests listen on 0)
  privateKeyHex?: string;                // default: a fresh random key
  enroll?: string;                       // six-digit code to send in prove
  name?: string; board?: string; fw?: string;
  caps?: GadgetCaps; actions?: GadgetActionDecl[];
  /** Override the prove signature (negative tests). */
  tamper?: "sig" | "host_id";
}
/** Node 24 global WebSocket (no Origin header) with subprotocol openmausbot-gadget.1; signs with
 *  node:crypto. Resolves once ready or error arrives. */
export declare function connectTestGadget(options: TestGadgetOptions): Promise<TestGadget>;
```
```ts
// companion/test/gadget/helpers/fake-harness.ts (P3a; reused by P3b, P4a, P4b)
import type { IncomingHttpHeaders } from "node:http";
import type { Buffer } from "node:buffer";
import type { WireBotLite } from "../../../src/gadget/types.ts";

export interface RecordedRequest { method: string; path: string; headers: IncomingHttpHeaders; body: Buffer; json?: unknown }
export type FakeRoute = (req: RecordedRequest) => { status: number; json?: unknown; body?: Uint8Array; contentType?: string } | Promise<{ status: number; json?: unknown; body?: Uint8Array; contentType?: string }>;
export interface FakeHarness {
  readonly port: number;
  readonly requests: RecordedRequest[];
  bots: WireBotLite[];                                 // served by GET /api/bots
  route(method: string, path: RegExp, handler: FakeRoute): void;   // later routes win
  /** Push one SSE frame to every GET /api/events subscriber (id: "<stream>:<seq>"). */
  emit(frame: object): void;
  /** Simulate a harness restart: drop SSE clients; the next hello has resumed:false. */
  restart(): void;
  waitFor(method: string, path: RegExp, timeoutMs?: number): Promise<RecordedRequest>;
  close(): Promise<void>;
}
/** Default routes: GET /api/bots, GET /api/events (hello + ping), POST /api/bots/:id/messages (202 receipt),
 *  POST /api/bots/:id/interrupt, POST /api/threads/:id/respond, GET /api/threads/:id/messages. */
export declare function startFakeHarness(): Promise<FakeHarness>;
```
- **Vendored vectors:** `companion/test/fixtures/gadget-vectors/` holds a byte-exact copy of the SDK's `protocol/vectors/` (every `*.json` plus `SHA256SUMS`) and a `SOURCE` file with the SDK commit it came from. `.gitattributes` gains `companion/test/fixtures/gadget-vectors/** -text`. `companion/test/gadget/vectors.test.ts` (P3a) checks every file against `SHA256SUMS` and runs the identity, prove, base64, frames and firmware vectors through `protocol.ts`/`enroll.ts`; P4b's tests read `firmware.json` and `versions.json` from the same folder. P3a copies the folder from the SDK's `main`, where P1 has landed (§1.3), and records that commit in `SOURCE`.

## 4. Data formats

### 4.1 `manifest.json` (produced by P2d's release.yml, consumed by P4b)

Exactly the spec §8 shape. One object per release, uploaded as a release asset and fetched at `https://github.com/aivsomkar/openmausbot-gadget-sdk/releases/latest/download/manifest.json`:

```json
{
  "version": "1.1.0",
  "boards": {
    "amoled-175c": {
      "url": "https://github.com/aivsomkar/openmausbot-gadget-sdk/releases/download/v1.1.0/openmausbot-gadget-amoled-175c-1.1.0.bin",
      "size": 1234567,
      "sha256": "3a7bd3e2360a3d29eea436fcfb7e44c735d117c42d1c1835420b6b9942dd4f1b",
      "sig": "MEYCIQ…",
      "key_id": "r1"
    }
  }
}
```

| Field | Rule (P4b's `parseManifest` enforces every one) |
|---|---|
| `version` | the tag without `v`, matching `/^\d+\.\d+\.\d+(-[0-9A-Za-z.]+)?$/`, never ending in `-dev` |
| `boards` | keys are board ids (`/^[a-z0-9-]{1,32}$/`); one entry per board built |
| `url` | exactly `<base>v<version>/openmausbot-gadget-<board>-<version>.bin`. For every published release `<base>` is `https://github.com/aivsomkar/openmausbot-gadget-sdk/releases/download/` (`RELEASE_URL_PREFIX`), so the URL is `https://github.com/aivsomkar/openmausbot-gadget-sdk/releases/download/v<version>/openmausbot-gadget-<board>-<version>.bin`. Only when the app's manifest URL is overridden (dev, tests) is `<base>` the overriding URL's directory (§3.16 `parseManifest`) |
| `size` | integer, 1 … 6291456, base-10 in the signed text |
| `sha256` | 64 lowercase hex of the app image |
| `sig` | canonical base64 of the DER ECDSA-P256-SHA256 signature over `firmwareText(board, version, size, sha256)` |
| `key_id` | `/^r[0-9]+$/` (P4b additionally accepts `t1` only when `OMB_GADGET_TRUST_TEST_KEY=1`) |

Serialization: `jq -n` output (2-space indent) or compact; consumers must not depend on whitespace or key order.

**One repository value.** P2d's `RELEASE_REPO` (`tools/release/lib.ts`, `"aivsomkar/openmausbot-gadget-sdk"`, used to write the manifest URLs) and P4b's `RELEASE_URL_PREFIX` and `MANIFEST_URL` (`companion/src/gadget/releases.ts`, used to check them) all derive from the §1.7 "SDK repo" value. If the repository moves, all three must change together (the SDK's release tools and the app), or the app stops finding the manifest and refuses every URL in it under the `url` rule above.

### 4.2 `firmware/install.json` (produced by release.yml, copied into the site by pages.yml, read by the installer)

Exactly the spec §5.8 shape; `path` is relative to the site's `firmware/` folder, `offset` is the hex string from the board's `flasher_args.json`:

```json
{
  "version": "1.1.0",
  "boards": {
    "amoled-175c": {
      "parts": [
        {"path": "openmausbot-gadget-amoled-175c-1.1.0-bootloader.bin", "offset": "0x0"},
        {"path": "openmausbot-gadget-amoled-175c-1.1.0-partition-table.bin", "offset": "0x8000"},
        {"path": "openmausbot-gadget-amoled-175c-1.1.0-ota-data-initial.bin", "offset": "0xf000"},
        {"path": "openmausbot-gadget-amoled-175c-1.1.0.bin", "offset": "0x20000"}
      ],
      "full": "openmausbot-gadget-amoled-175c-1.1.0-full.bin"
    }
  }
}
```

Parts are listed in flash-address order. Before the first release, pages.yml writes `{"version": null, "boards": {}}` and the page says no firmware is published yet. The installer flashes `parts` (never `full`) with `flashMode/flashFreq/flashSize: "keep"`; "Erase everything" adds `eraseAll: true`.

### 4.3 Release assets (P2d)

For each board `<b>` and version `<v>` (tag `v<v>`):

| Asset | From the build dir |
|---|---|
| `openmausbot-gadget-<b>-<v>.bin` | `openmausbot-gadget.bin` (the app; what OTA sends) |
| `openmausbot-gadget-<b>-<v>-bootloader.bin` | `bootloader/bootloader.bin` |
| `openmausbot-gadget-<b>-<v>-partition-table.bin` | `partition_table/partition-table.bin` |
| `openmausbot-gadget-<b>-<v>-ota-data-initial.bin` | `ota_data_initial.bin` |
| `openmausbot-gadget-<b>-<v>-full.bin` | `idf.py -B build/<b> merge-bin -o merged.bin` (CLI recovery only; erases NVS) |

Plus once per release: `manifest.json`, `install.json`, `SHA256SUMS` (GNU `sha256sum` format over every other asset, sorted by name). Tags containing `-` are created with `--prerelease`. Release CI fails if `PROJECT_VER` ends in `-dev`.

### 4.4 Protocol vectors (`protocol/vectors/`, P1; consumed by P2a in C and P3a/P4b in TS)

Rules: every file is `JSON.stringify(value, null, 2) + "\n"`, UTF-8, LF; `SHA256SUMS` lists `<sha256 lowercase>  <file name>` (two spaces) for every `*.json`, sorted by file name, LF-terminated, and does not list itself. All hex is lowercase. `npm run vectors:check` regenerates and requires no diff. Every case has a unique `name`. Envelope: `{"vectors": "<file stem>", "version": 1, "cases": [ … ]}` (versions.json uses `compare` and `custom` instead of `cases`).

| File | Case fields | Cases (minimum) |
|---|---|---|
| `identity.json` | `name`, `private_key_hex`, `pubkey_hex` (130 hex, `04…`), `pubkey_b64`, `pubkey_sha256_hex`, `id` | `rfc6979-a25`, `test-t1` |
| `rfc6979.json` | `name`, `private_key_hex`, `message_utf8`, `message_sha256_hex`, `k_hex`, `r_hex`, `s_hex`, `raw_hex` (128 hex r‖s), `der_hex`, `high_s` | `sample` (high-S), `test` (low-S) |
| `prove.json` | `name`, `expect`, `private_key_hex`, `pubkey_b64`, `id`, `nonce_b64`, `host_id`, `text`, `sig_der_hex`, `sig_b64`, `deterministic`, `high_s` | `pinned` (accept, deterministic, high-S; §1.7 values), `pinned-low-s` (accept; n−s of `pinned`, not deterministic), `low-s-host` (accept; host_id `0123456789abcdef0123456789abcdef`), `changed-host-id` (`reject_sig`: text built with host_id `000102030405060708090a0b0c0d0e0e`, signature of `pinned`), `pubkey-not-id` (`reject_id`: id `gad_0000000000000000`), `pubkey-non-canonical` (`reject_base64`: `pinned`'s pubkey with a non-zero pad bit), `pubkey-compressed` (`reject_pubkey`: 33-byte key), `sig-truncated` (`reject_sig`), `host-id-format` (`reject_host_id`: host_id `h_0123456789abcdef`; a gadget-side check) |
| `der.json` | `name`, `valid`, `der_hex`, and for valid cases `raw_hex`, `der_len` (and `private_key_hex`, `message_utf8` when signed) | `rfc-sample` (72 B), `short-der-69` (§1.7), `prove-pinned` (72 B), invalid: `non-minimal-int`, `negative-int`, `trailing-bytes`, `wrong-tag` |
| `base64.json` | `name`, `input`, `canonical`, and `bytes_hex` when canonical | canonical: `""`, `AA==`, `AAE=`, `AAEC`, the pinned nonce, the RFC pubkey; non-canonical: `AA` (no padding), `AA=`, `AB==` (pad bits), `AAE`, `AA==\n`, `AA==` with a leading space, `-_8=` (URL alphabet), `AA==AA==` |
| `firmware.json` | `name`, `expect`, `key_id`, `private_key_hex`, `pubkey_b64`, `board`, `gadget_board`, `version`, `size`, `sha256`, `text`, `sig_der_hex`, `sig_b64`, `deterministic` | `t1-amoled` (accept; §1.7 values; `gadget_board` = `board`), `other-board` (`bad_sig`: `gadget_board` `lcd-154` rebuilds the text with its own board), `size-changed` (`bad_sig`), `sha-uppercase` (`bad_sig`: the text with uppercase hex does not verify) |
| `frames.json` | `name`, `valid`, `frame_hex`, and for valid frames `kind`, `stream`, `payload_hex` (+ `offset`, `data_hex` for kind 4) | `mic-20ms` (640-byte payload), `speaker-40ms-16k`, `image-rows`, `fw-chunk` (offset 65536, 4096 bytes), `fw-chunk-last` (short), invalid: `too-short`, `stream-zero`, `unknown-kind`, `fw-no-offset` |
| `versions.json` | `compare: [{a, b, cmp}]` (`cmp` ∈ −1, 0, 1, SemVer 2.0.0 precedence), `custom: [{fw, custom}]` | compare `1.1.0`>`1.0.9`, `1.10.0`>`1.9.9`, `1.1.0`>`1.1.0-rc.1`, `1.1.0-rc.2`>`1.1.0-rc.1`, `1.1.0-rc.10`>`1.1.0-rc.9`, `1.1.0-beta`<`1.1.0-rc`, equal pairs; custom: `0.0.0-dev` true, `1.2.0-dev` true, `1.2.0` false, `1.2.0-rc.1` false, `v1.2.0` true, `1.2` true, `""` true |

`expect` values: `accept`, `reject_sig`, `reject_id`, `reject_base64`, `reject_pubkey`, `reject_host_id`, `bad_sig`.

#### 4.4.1 `protocol/lib/` exports (P1; used by gen-vectors, protocol tests, fake host and P4b's dev-release tool)

```ts
// protocol/lib/types.ts      — the op types of §3.2 with the same names (HelloMsg … FwInstalledMsg, GadgetToHost, HostToGadget, BinaryKind)
// protocol/lib/encoding.ts
export declare function b64Encode(bytes: Uint8Array): string;
export declare function b64DecodeCanonical(text: string): Uint8Array | null;   // null unless decode→encode round-trips exactly
export declare function hexEncode(bytes: Uint8Array): string;
export declare function hexDecode(hex: string): Uint8Array;
export declare function canonicalJson(value: unknown): string;
// protocol/lib/identity.ts
export declare const HOST_ID_RE: RegExp;   // /^[0-9a-f]{32}$/
export declare function gadgetIdFromPubkey(pub65: Uint8Array): string;
export declare function proveText(id: string, nonceB64: string, hostId: string): string;
export declare function firmwareText(board: string, version: string, size: number, sha256Hex: string): string;
// protocol/lib/frames.ts
export declare function encodeBinary(kind: 1 | 2 | 3 | 4, stream: number, payload: Uint8Array): Uint8Array;
export declare function decodeBinary(frame: Uint8Array): { kind: 1 | 2 | 3 | 4; stream: number; payload: Uint8Array } | null;
export declare function encodeFwChunk(stream: number, offset: number, data: Uint8Array): Uint8Array;
export declare function decodeFwChunk(payload: Uint8Array): { offset: number; data: Uint8Array } | null;
// protocol/lib/verify.ts       — node:crypto only (accepts high-S)
export declare function verifyP256(pub65: Uint8Array, text: string, der: Uint8Array): boolean;
export declare function publicKeyFromPrivate(privateKeyHex: string): Uint8Array;   // createECDH("prime256v1")
export declare function signP256(privateKeyHex: string, text: string): Uint8Array; // DER, random k (node:crypto); for the fake host and dev tools
// protocol/lib/version.ts
export declare function compareVersions(a: string, b: string): number;
export declare function isCustomBuild(fw: string): boolean;
```

Only `protocol/tools/gen-vectors.ts` imports `@noble/curves` 2.4.0 (ESM-only, subpath exports), exactly as `import { p256 } from "@noble/curves/nist.js";` (`p256.getPublicKey(sk, false)`, `p256.sign(msg, sk, {lowS: false, format: "der"})`, `p256.Signature.fromBytes(der, "der").hasHighS()` asserted for every case marked `high_s: true`).

### 4.5 Key files (`keys/`)

| File | Format | Owner |
|---|---|---|
| `keys/test-t1.key.hex` | 64 lowercase hex + `\n`: the t1 private scalar (§1.7); test-only, committed on purpose | P1 |
| `keys/test-t1.pub.b64` | canonical base64 of the 65-byte SEC1 point + `\n` | P1 |
| `keys/release-r1.pub.pem` | `openssl pkey -pubout` PEM | Omkar, via P2d's `docs/release-keys.md` |
| `keys/release-r1.pub.b64` | `openssl pkey -in … -pubout -outform DER \| tail -c 65 \| openssl base64 -A` + `\n` | Omkar, via P2d |

The release private key lives only in the GitHub Actions environment `release` as secret `GADGET_RELEASE_KEY_R1` (PKCS#8 PEM). Generation is a manual step for Omkar; until `release-r1.pub.b64` exists, `gadget_release_keys_count` is 0, `RELEASE_KEYS` is empty, and release CI fails by design. P2d's release job asserts the secret's public half equals `keys/release-r1.pub.b64`.

### 4.6 Simulator state folder (P2a)

`~/.openmausbot-gadget/sim/<name>/` (or `--state-dir`), every file written to a temp name and renamed:

| File | Format |
|---|---|
| `storage.json` | `{"version": 1, "entries": {"<key>": {"str": "<value>"} \| {"blob": "<lowercase hex>"}}}`; keys are the `GADGET_KEY_*` names. Core uses the `dev_key` it finds here and generates one only when none exists (P2b's scripted goldens start from a folder whose `storage.json` holds only a `dev_key`, the RFC key) |
| `otadata.json` | `{"active": 0 \| 1, "state": "valid" \| "pending" \| "invalid", "version": "<fw>", "previous": {"slot": 0 \| 1, "version": "<fw>"}, "booted": true}`; absent = slot 0, `valid`, `GADGET_SIM_VERSION`. `"booted": true` (present only while true) is written when a `pending` image starts; a `pending` image that starts a second time without confirming (`hal_ota_mark_valid`) rolls back, as the device's bootloader does (§6 D25) |
| `slot0.bin`, `slot1.bin` | received OTA images (opaque bytes) |

### 4.7 Fake host (`tools/fake-host`, P1; driven by P2a's e2e runner)

**CLI:** `node tools/fake-host/src/main.ts [options]`

| Option | Default | Meaning |
|---|---|---|
| `--port <n>` | 8810 | 0 = any free port (reported in `listening`) |
| `--bind <addr>` | 127.0.0.1 | 0.0.0.0 to serve a real board on the LAN |
| `--code <6 digits>` | random | the one valid pairing code (window: `--code-ttl`, 5 attempts, single use) |
| `--code-ttl <s>` | 120 | |
| `--state <dir>` | memory only | persists `host_id` and enrolled gadgets in `<dir>/fake-host.json` |
| `--host-id <32 hex>` | random | |
| `--host-name <text>` | `Fake MausBot` | |
| `--bot <id>:<name>` | `b_fake:Fake Bot` | the bot in `ready` |
| `--heard <text>` | `What's on my calendar today?` | STT result for voice turns; `""` → `done failed "Didn't catch that"` |
| `--reply <text>` | `You have two meetings today: design review at 10 and lunch with Sam at 1.` | |
| `--tone-ms <n>` | 800 | test tone length (440 Hz sine, amplitude 8000) sent as speech; 0 = none |
| `--ota-key <file>` / `--ota-key-id <id>` | `keys/test-t1.key.hex` / `t1` | OTA signing key |
| `--max-devices <n>` | 20 | enrolled-gadget cap; enrolling past it gets `device_limit` |
| `--done-before-speech` | off | sends `done ok` before the turn's speech, as MausBot does (right after the final `reply`; PROTOCOL.md §4.4) |
| `--quiet` | off | no stderr logs |

Behaviour: speaks the host side of PROTOCOL.md: Origin refused, subprotocol required, ping every 15 s, 45 s idle timeout, Node `crypto` verification, enrollment rules with the one code, `device_limit` (with `--max-devices`), `replaced` handling. Voice turn: `heard`, `working "checking your calendar"`, three cumulative `reply` frames 250 ms apart, final `reply`, then (speaker present and `--tone-ms` > 0) `speak.begin {stream, rate: caps.speaker.rate, turn}`, 40 ms frames paced ≤ 0.5 s ahead, `speak.end`, then `done ok` once the tone has played out. With --done-before-speech: final `reply`, `done ok`, then `speak.begin`…`speak.end`; a new turn first sends `speak.stop` for that stream. P2a's e2e runner runs at least one voice turn in this order (its `e2e.bargein_after_done` scenario). `say` follows the same path without `heard`. `voice.begin` with rate ≠ 16000 → `done failed "Unsupported mic rate"`. `stop`, `voice.drop`, or a new turn while one is in flight → `speak.stop` + `done stopped` for the old turn first. `answer` → `ask.close {id, reason: "answered"}`. Post speech starts only after earlier speech has played out (and that turn's `done` has been sent); a reply's speech goes ahead of post speech that has not begun.

**Control:** JSON lines on stdin (`{"cmd": …}`), events as JSON lines on stdout (`{"event": …}`), logs on stderr. Every command may name `"gadget": "<id>"` (default: the most recently ready gadget) and gets one `{"event": "ack", "cmd": "<cmd>", "ok": true}` or `{"event": "ack", "cmd": "<cmd>", "ok": false, "error": "…"}`.

| Command | Fields | Effect |
|---|---|---|
| `code` | `code?` | open a new pairing window; emits `code` |
| `ask` | `id?`, `kind`, `title`, `body`, `options?` (permission: options omitted or exactly Allow/Deny (styles allow/deny); any other permission `options` gets `ok: false`; question: at most 4), `expires_s?` | send `ask` |
| `ask.close` | `id`, `reason` | |
| `post` | `kind`, `text`, `speak?` | send `post` (speech follows when `speak` and a speaker exist) |
| `card` / `card.close` | `id?`, `title`, `body`, `ttl_s?` / `id` | |
| `image` | `id?`, `w`, `h`, `ttl_s?`, `pattern?` (`bars` or `#rrggbb`) | `image.begin`, RGB565 rows, `image.end` |
| `act` | `id?`, `name`, `args?` | send `act`; emits `act.result` when it arrives (or `timeout` after 15 s) |
| `settings` | `bot?`, `speak_pushes?`, `name?` | send `settings` |
| `heard` / `reply` | `text` | change the scripted STT result / reply |
| `ota` | `image` (path), `version`, `board?` (default: the gadget's), `tamper?` (`sig`, `sha256`, `size`) | full §4.8 flow with a 64 KiB window |
| `revoke` / `replace` | — | send `error revoked` / `error replaced` and close |
| `drop` | — | destroy the socket without a close frame |
| `close` | `code?` (default 1000) | close frame with `code`; code ∈ 1000–1003, 1007–1014, 3000–4999 (any other code, 1004–1006 included, gets `ok: false` and the session keeps working) |
| `quit` | — | close everything and exit 0 |

| Event | Fields |
|---|---|
| `listening` | `port`, `host_id` |
| `code` | `code`, `expires_at` (epoch ms) |
| `connected` / `closed` | `remote` / `gadget`, `code` |
| `rx` / `tx` | `gadget` (null before `prove`), `msg` (the full text frame) |
| `rx_binary` | `gadget`, `kind`, `stream`, `bytes` |
| `enrolled` / `ready` / `refused` | `gadget` / `gadget`, `session` / `code`, `message` |
| `turn` | `gadget`, `turn`, `phase` (`started`, `heard`, `reply`, `speech`, `done`), `outcome?` |
| `answer` | `gadget`, `id`, `option` |
| `act.result` | `gadget`, `id`, `ok`, `data?`, `error?` (or `timeout: true`) |
| `ota` | `gadget`, `phase` (`offered`, `ready`, `progress`, `committed`, `installed`, `failed`), `offset?`, `size?`, `code?`, `version?` |
| `ack` | `cmd`, `ok`, `error?` |

### 4.8 Installer modules (`site/`, P2d)

The installer mirrors §2.11 exactly. Pinned exports (unit-tested in Node):

```ts
// site/src/console.ts
export type OmbMessage =
  | { op: "boot"; board: string; fw: string; id: string }
  | { op: "status"; wifi: "off" | "connecting" | "connected" | "failed"; ssid?: string; host?: string; id: string;
      pair: "unpaired" | "code_stored" | "connecting" | "paired" | "error"; error?: string; fw: string;
      battery?: { pct: number; charging: boolean }; board?: string; name?: string; host_name?: string }
  | { op: "scan"; networks: Array<{ ssid: string; rssi: number; auth: "open" | "wep" | "wpa" | "wpa2" | "wpa3" | "wpa2-ent" | "other" }> }
  | { op: "hosts"; hosts: Array<{ name: string; address: string; id: string }> }
  | { op: "say"; turn: string }
  | { op: "error"; cmd: string; message: string };
export declare function parseOmbLine(line: string): OmbMessage | null;       // strips ANSI first; null for non-@omb lines
export declare function createLineSplitter(): (chunk: string) => string[];   // CR, LF, CRLF
export declare function quoteArg(value: string): string;                     // "…" with \\ and \" escapes
/** In this order: pair <code>, wifi "<ssid>" "<password>", host auto (or host <address>). */
export declare function setupCommands(input: { code: string; ssid: string; password: string; address?: string }): string[];
// site/src/install.ts
export interface InstallIndex { version: string | null; boards: Record<string, { parts: Array<{ path: string; offset: string }>; full: string }> }
export declare function parseInstallIndex(json: unknown): InstallIndex;      // throws on any shape error
export declare function flashPlan(index: InstallIndex, board: string): Array<{ path: string; address: number }>;
```

`resetToApp()` (site/src/reset.ts): `writeReg(0x6000812C, 0, 1)`, `setDTR(false)`, `setRTS(true)`, 100 ms, `setRTS(false)`, `transport.disconnect()`, reopen raw Web Serial at 115200 with `setSignals({dataTerminalReady: false, requestToSend: false})`; re-acquire USB 0x303A/0x1001 after `disconnect`/`connect`; no output for ~5 s → "Press RST or unplug and replug the board". Console text is written to `port.writable` directly, never through esptool-js `Transport.write`.

When the chosen board is `devkit`, the installer shows, before the port picker: `Plug the board in by the USB-C port labelled USB, not the one labelled UART.` (the UART port goes through a bridge chip, so the console and the 0x303A/0x1001 device never appear; §2.17).

## 5. Ownership and consumption

Each file has exactly one owner. A non-owner may edit an owned file only where §1–§3 pin an insertion point for it (marked "appends" or listed in §3.12/§3.13/§3.17); P2a's `core/CMakeLists.txt` ESP branch may be corrected by P2c (§2.17).

### 5.1 SDK files

| Path | Owner | Notes |
|---|---|---|
| `LICENSE`, `.gitignore`, `.gitattributes`, root `package.json`/lock | P1 | P2a adds the `test:e2e` script; P2b appends `.gitattributes` lines |
| `README.md` | P1 stub, P2d final | |
| `.github/workflows/ci.yml` | P1 (file, jobs `protocol`, `fake-host`) | P2a `host-c`, P2b `ui` + `art-drift`, P2c `esp32` + `esp32-idf55`, P2d `site` |
| `protocol/PROTOCOL.md`, `protocol/lib/**`, `protocol/tools/**`, `protocol/test/**`, `protocol/vectors/**` | P1 | |
| `keys/test-t1.*` | P1 | |
| `keys/release-*`, `keys/README.md`, `docs/release-keys.md` | P2d (files committed by Omkar) | |
| `tools/fake-host/**` | P1 | |
| `firmware/CMakeLists.txt`, `firmware/cmake/**` | P2a | P2b adds LVGL/SDL to `deps.cmake` and `add_subdirectory(ui)` |
| `firmware/core/**` (all contract headers, all core sources, key tables) | P2a | P2d fills `keys_release.c` once r1 exists |
| `firmware/ports/sim/**` | P2a | P2b adds `sim_display_lvgl.c`, `sim_sdl.c`, `sim_audio_sdl.c` and their CMake lines |
| `firmware/tests/**` | P2a | P2b adds `snapshots/**`, snapshot scripts and their CTest entries |
| `firmware/tests/e2e/**` | P2a | |
| `firmware/ui/**` (incl. generated `art/`, `fonts/`, `lv_conf.h`) | P2b | |
| `tools/art/**` | P2b | |
| `firmware/ports/esp32/**` | P2c | includes `firmware/ports/esp32/host-tests/**`, `firmware/ports/esp32/tools/*.sh` (`check-size.sh`, `check-art-profile.sh`, `build-all.sh` and their test scripts) and `firmware/ports/esp32/sdkconfig.{nvs-encrypt,test-keys}` |
| `docs/hardware-checklist.md` | P2c | P4b may append one "MausBot path" section on `p4b-ota`: the on-device voice (V1–V5, P3b), bot-tool (T1–T6, P4a) and Update-button (U1–U6, P4b) checks, inserted before P2c's result table, plus that section's rows at the end of the table. P2c's checks, row numbers and wording stay unchanged |
| `site/**`, `.github/workflows/release.yml`, `.github/workflows/pages.yml`, `tools/release/**` (except `dev-release.ts` and `test/dev-release.test.ts`) | P2d | |
| `tools/console/**`, `tools/screenshots/**`, `docs/images/**`, `docs/installer-checklist.md` | P2d | |
| `.github/workflows/art-regen.yml` | P2b | |
| `AGENTS.md`, `CONTRIBUTING.md`, `NOTICE`, `THIRD_PARTY.md` | P2d | |
| `tools/release/dev-release.ts` | P4b (SDK branch `p4b-ota`) | |
| `tools/release/test/dev-release.test.ts` | P4b (SDK branch `p4b-ota`) | run by P2d's `site` job (`node --test "tools/release/test/*.test.ts"`, §1.6) with no CI edit |

### 5.2 App files (OpenMausBot)

| Path | Owner | Others who touch it (pinned points) |
|---|---|---|
| `companion/src/gadget/protocol.ts`, `types.ts`, `ws.ts`, `enroll.ts`, `harness-client.ts`, `shape.ts`, `session.ts`, `hub.ts` | P3a | — |
| `companion/src/gadget/audio.ts`, `stt-client.ts`, `speech.ts` | P3b | — |
| `companion/src/gadget/control-routes.ts`, `presence.ts` | P4a | — |
| `companion/src/gadget/releases.ts`, `release-keys.ts`, `ota.ts`, `firmware.ts` | P4b | — |
| `companion/src/host-id.ts` | P3a | — |
| `companion/src/devices.ts` | P3a | — |
| `companion/src/control.ts` | P3a (`POST /pairing` body, `PUT /pairing/bot`, `PATCH /devices/:id/gadget`, `hostId`) | P4a (`/gadget/*` dispatch, `gadgetControlToken` option), P4b (firmware routes, `gadgetFirmware` in state) |
| `companion/src/index.ts` | P3a | P3b, P4a, P4b at the §3.12 points |
| `companion/src/routes.ts` | — (existing) | P3b (`/api/stt` in `ALLOWED`), P4a (`/api/gadgets/presence` in `COMPANION_NOTICES`) |
| `companion/test/gadget/**`, `companion/test/fixtures/gadget-vectors/**`, `.gitattributes` line | P3a | P3b, P4a, P4b add their own test files |
| `server/routes/stt.ts`, `server/stt/**`, `server/tts/pcm.ts`, `server/tts/*.ts` provider edits, `server/tts/index.ts` | P3b | — |
| `server/index.ts` | — (existing) | P3a (`gadgetControlToken` variable + parse), P3b (`ROUTES.push(createSttRoutes…)`, the `/api/tts/speak` block), P4a (`gadgetControl`, presence route push, internal-route dispatch, `OMB_GADGETS`, `refresh()` on token) |
| `server/request-auth.ts` | — | P3b (`CLIENT_ALLOW` `/api/stt`) |
| `server/gadget-control.ts`, `server/gadget-image.ts`, `server/routes/gadgets.ts`, `server/routes/internal-gadgets.ts` | P4a | — |
| `server/peer-approval.ts`, `server/peer-approval-key.ts`, `server/bot-attachment.ts`, `server/agent-tool-policy.ts`, `server/harness-capabilities.ts`, `server/drivers/agents-catalog.ts`, `server/drivers/agents-call.ts`, `server/drivers/codex.ts`, `server/drivers/claude.ts`, catalog goldens | P4a (edits) | — |
| root `package.json` (`pngjs`, `jpeg-js`), lockfile, app notices | P4a | — |
| `electron/main.mjs` | — (existing) | P3a (token mint, both messages, harness env, three IPC handlers), P4b (two IPC handlers) |
| `electron/companion.mjs` | — | P3a (token in `start()`, three helpers), P4b (two helpers) |
| `electron/preload.cjs`, `electron/preload.node-test.mjs` | — | P3a (three methods), P4b (two methods) |
| `electron/resources/speech-helper.swift` | P3b (file mode) | — |
| `src/components/PairGadgetPanel.tsx`, `GadgetRow.tsx`, `src/lib/gadgets.ts` | P3a | P4b adds `isCustomBuild`/`updateCellState`/`hasActiveGadgetUpdate` to `gadgets.ts` |
| `src/components/GadgetUpdateCell.tsx` | P4b | — |
| `src/components/CompanionSection.tsx`, `PhoneSetupFlow.tsx`, `SidebarPhoneButton.tsx`, `SettingsModal.tsx` | P3a (edits) | P4b (pass `updateCell`, call `firmwareCheck` on mount, `gadgetFirmware` type; in `PhoneSetupFlow.tsx`, `shouldPoll` is also true while `hasActiveGadgetUpdate(state)`, with that import from `../lib/gadgets`, so the panel polls every second while an update runs; P4b deviation 1, approved) |
| `scripts/bundle-server.mjs` | — | P4a (banner only: the `createRequire` banner on the first `build()`, §1.4) |
| `src/locales/en.json` | — | P3a and P4b keys per §3.18 |

### 5.3 Who consumes which interface

| Interface | Producer | Consumers |
|---|---|---|
| `protocol/PROTOCOL.md` (all ops, encodings, limits) | P1 | P2a, P2b (copy limits), P2c, P2d (AGENTS.md links it), P3a, P3b, P4a, P4b |
| `protocol/vectors/*.json` + `SHA256SUMS` (§4.4) | P1 | P2a (C vector tests), P3a (vendored copy, enrollment/signature tests), P4b (firmware + versions) |
| `protocol/lib/*` (§4.4.1) | P1 | P1 fake host, P4b `dev-release.ts` |
| Fixed values (§1.7), test key t1 (`keys/test-t1.*`) | P1 | P2a (`keys_test.c`, OTA tests), P3a, P4b, P2d (docs) |
| Fake host CLI + JSON-lines control (§4.7) | P1 | P2a (e2e runner), P2b (manual window-mode checks), P2d (AGENTS.md) |
| `gadget_types.h`, `gadget_board.h`, `gadget_events.h`, `gadget_hal.h` (§2.2–2.5) | P2a | P2b (board, events through the sim), P2c (implements the HAL) |
| `gadget_core.h` (§2.7) | P2a | P2b (sim main loop, snapshots), P2c (`main.c`) |
| `gadget_ui_model.h`, `gadget_ui.h` incl. `ui_layout_ask`/`ui_hit_test`/`ui_safe_area` (§2.8–2.9) | P2a | P2b (implements `ui_*`, draws the model), P2c (calls `ui_init/render/tick`) |
| `gadget_actions.h` (§2.10) | P2a | P2c (board-specific actions, if any), P2d (AGENTS.md "add an action") |
| Console grammar + `@omb` lines (§2.11) | P2a | P2c (feeds lines via `gadget_linebuf`), P2d (installer `console.ts`, docs) |
| `gadget_proto.h` op/type names (§2.12) | P2a | tests only |
| `gadget_ota.h` key tables, Kconfig `GADGET_TEST_KEYS` (§2.13, §2.17) | P2a / P2c | P2d (release key check), P2c |
| Storage keys (§2.14) | P2a | P2c (NVS backend), P2a sim backend |
| `sim_display.h` and `sim_hal.h` seams, sim CLI and script grammar (§2.16) | P2a | P2b (implements the LVGL display, the SDL event filter through `sim_post_event`, and `sim_audio_sdl_backend`; writes snapshot scripts), P2d (AGENTS.md) |
| `maus_art.h`, fonts, screen copy (§2.15) | P2b | P2c (links one art profile), P2d (README/installer copy, art provenance in NOTICE/THIRD_PARTY) |
| ESP32 build command, board dirs, partition table, `check-size.sh` (§2.17) | P2c | P2d (release.yml, AGENTS.md) |
| `manifest.json` (§4.1) | P2d | P4b |
| `install.json`, release asset names (§4.2–4.3) | P2d | P2d site; P4b (asset URL rule) |
| `keys/release-r1.pub.b64` (§4.5) | P2d / Omkar | P2d (`keys_release.c`, release check), P4b (`RELEASE_KEYS`) |
| `protocol.ts`, `types.ts` (§3.2–3.3) | P3a | P3b, P4a, P4b |
| `ws.ts` (§3.4) | P3a | P3a only |
| `DeviceRegistry` gadget API, `GadgetDeviceRecord` (§3.5) | P3a | P4a (directory), P4b (firmware service), renderer types (P3a) |
| `harness-client.ts` (§3.7) | P3a | P3b (STT, TTS calls) |
| `shape.ts` (§3.8) | P3a | P4a (card text through `/gadget/display`), P3b (STT error reasons) |
| `GadgetSessionHandle`, `VoiceProvider`, `SttFn`, `SpeechOut` (§3.10) | P3a | P3b (voice), P4a (`act`, cards, images), P4b (OTA) |
| `GadgetHub` + `GadgetHubOptions` (§3.11) | P3a | P3b (`voice`), P4a (`onDevicesChanged`, `session`, `recentEvents`, `botName`), P4b (`onSessionReady`, `session`) |
| Companion wiring points (§3.12) | P3a | P3b, P4a, P4b |
| Control routes `/gadget/*` + types (§3.13, §3.15) | P4a | P4a harness client |
| Control routes for Electron (§3.13) | P3a, P4b | P3a/P4b Electron helpers |
| `/api/stt`, TTS `format` (§3.14) | P3b | P3b hub voice |
| `/api/gadgets/presence`, `/api/internal/gadgets*`, catalog, tool schemas (§3.15) | P4a | agents proxy (P4a) |
| `gadgetControlToken` messages and env (§3.17) | P3a | P4a (companion `/gadget/*` auth, harness client) |
| Bridge methods, IPC channels (§3.17–3.18) | P3a, P4b | renderer (P3a, P4b) |
| `src/lib/gadgets.ts`, `GadgetRow` `updateCell` prop (§3.18) | P3a | P4b |
| `GadgetFirmwareState` in `/state` (§3.16) | P4b | renderer (P4b) |
| Test helpers `connectTestGadget`, `startFakeHarness` (§3.19) | P3a | P3b, P4a, P4b |

## 6. Decisions this contract adds

The spec leaves these open or words them differently; plans follow the column "Contract".

| # | Topic | Spec / research | Contract |
|---|---|---|---|
| D1 | Hub harness client file | spec §6.1 names `harness.ts` | `companion/src/gadget/harness-client.ts` (plan list); `protocol.ts` added for op types and helpers, `types.ts` keeps the copied app types |
| D2 | Streaming SHA-256 | spec §5.2 lists `psa_hash_compute` only | OTA hashes chunks with `psa_hash_setup/update/finish/abort` (`hal_crypto_sha256_*`), the multi-part form of the same PSA hash; nothing else is added |
| D3 | Console output | spec §5.6 defines `status`, `scan`, `say`, `hosts` lines | adds `@omb boot` and `@omb error` lines and optional `board`, `name`, `host_name` in `status`; tools ignore unknown fields and ops. P2a's console additions in the same spirit (P2a deviation 5): `@omb error` with `cmd` `say` and `message` `not connected to MausBot` for `say` without a ready session, and with `cmd` `""` and `message` `line too long` for an over-long line (both ports); and the status rules of §2.11 |
| D4 | Simulator script | spec §5.7 command list | adds `model`, `net_open`, `net_text`, `net_binary`, `net_close`, `battery`, `boot` and the `--host script` backend, so UI snapshots are deterministic without a socket; adds `--state-dir`, `--seed`, `--battery`, `--zoom`, `--snapshot-dir`, `--trace`, `--boot` and the test-only `--probation-ms`; a re-exec drops `--pair` and passes `--boot <n+1>` (spec §5.7 only says "re-execing itself"; re-execing with the original argv would replay the script from line 1 and the pairing code after every restart). D23, D24, D26 and D27 extend it |
| D5 | Always-allow key for gadget actions | spec §7: target id `<deviceId>:<actionName>`, grant keyed to the entry hash | target id `<deviceId>:<actionName>:<entryHash>` (entryHash = 16 hex of the canonical entry), so a re-declared action no longer matches the grant |
| D6 | Changed-action check on `/gadget/act` | spec §7: "a hash of the gadget's declared actions" | per-action `entryHash` plus `risk`; the companion refuses with 409 `action_changed` when the live declaration differs |
| D7 | Full access and `gadget_action` | spec §7: never auto-approved | the call site gives `requestPeerApproval` a bus without `autoApply`; desktop "Always allow" grants still apply |
| D8 | Changing the picked bot while the code shows | spec §6.4: the picker's bot travels with the pairing request | new control route `PUT /pairing/bot {botId, expectedToken}` and IPC `companion:pairing-bot` |
| D9 | Token ownership | spec §3: sub-project 4 owns `gadgetControlToken` | P3a mints and delivers it (plan assignment); P4a consumes it |
| D10 | Internal gadget routes and the route ratchet | R4 placed them inline in `server/index.ts` | `server/routes/internal-gadgets.ts`, called from inside the existing `/api/internal/` block after the capability prelude, so the ratchet's needles do not grow |
| D11 | Board caps not given in the spec | — | `lcd-154` image 200×200, `devkit` image 280×200 and speaker 24000; `amoled-175` input `touch`,`talk` |
| D12 | Test signing key | spec: "a test key" | `t1` = SHA-256 of `openmausbot-gadget/1 test release key t1`, committed in `keys/` |
| D13 | Unbound gadget bot | spec: the hub picks a bot | when no bot can be found, `botId` is null, `ready.bot` is `{"id":"","name":""}` and turns fail with a pointer to Remote access; on every later connection of a gadget whose `botId` is null the hub runs `defaultGadgetBot` over `GET /api/bots?messages=0` again and saves a found bot before `ready` (§3.11), so a lookup that failed during enrollment heals itself |
| D14 | SSE identity | R9 suggested a `gadget-hub` device id | spec wins: the shared SSE stream and `GET /api/bots` send the companion marker only |
| D15 | Card limits | not in the spec | the companion clamps card titles to 80 and bodies to 600 characters; the gadget keeps 192/1536-byte buffers and truncates with `…` |
| D16 | Vector set | spec §4.9 lists categories | adds `base64.json`, `der.json`, `frames.json` and `versions.json` (SemVer precedence and custom-build cases shared by C, fake host and the app) |
| D17 | Art amplitudes | R8 used floats | `maus_state_def_t` amplitudes are integers in 0.1 px, so rendering stays integer-only and identical on arm64 and x86_64 |
| D18 | Presence notice sender id | spec: on enroll, remove and start | uses the gadget's id, or `gadget-hub` at start, because the harness requires a device header on companion-token requests; the start notice waits for the companion's mutation token and retries (§3.12), and the harness re-polls every 30 s while the companion is unreachable (§3.15) |
| D19 | Ask classification | spec §6.2 / A9: a card that is not a question card is a permission card | `gadgetAskKind` (§3.3): proposal cards (`routineRequest`, `skillRequest`, `profileRequest`, `modelRequest`, `teamSetupRequest`) are unsupported asks, because `isPersistentQuestionCard` returns false for them and `/respond` with `allow` would apply the proposal from the gadget; a permission ask needs a non-empty `card.tool` (provider permissions and peer approvals both have one) |
| D20 | Folded strings | spec §4.4 lists the strings folded to `caps.screen.text` | adds `challenge.host_name` (the gadget shows and stores it): `screenText(machineName(), {markdown: false})`, cut to 64 UTF-8 bytes |
| D21 | Console line length and `forget` | spec §5.6: `say` ≤ 2000 chars; `forget` erases the pairing, key, Wi-Fi and name | `GADGET_CONSOLE_LINE_MAX` is 8192 bytes so the longest `say` fits; `forget` erases storage and then restarts, so the old id never reconnects from memory |
| D22 | `fw.fail busy` | spec §4.8: `busy` while an update is already running | the gadget answers `fw.fail busy` while a recording is live or a turn is in flight, as well as during an update, so an offer never cuts the person off (P2a deviation 1). P4b shows `The gadget is busy. Try again when it is idle.` (§3.16) |
| D23 | Script `expect` | §2.16 "waits until a frame with that op crosses" (read as "from now on") | `expect <op>` matches the first such frame since the previous `expect` matched, including frames that crossed before the line was reached, so scripts do not depend on network timing (P2a deviation 2; §2.16) |
| D24 | Script `net_open` | §2.16 gave `net_open` no argument | `net_open [timeout_ms]` waits up to `timeout_ms` (default 5000) for the gadget to connect, for example to wait out the 2 s reconnect backoff (P2a deviation 3; §2.16) |
| D25 | Simulator rollback after an unconfirmed start | §4.6 `otadata.json` format | `otadata.json` gains `"booted": true`; a pending image that starts twice without confirming rolls back, as the device's bootloader does (P2a deviation 4; §4.6) |
| D26 | Simulator additions | spec §5.7 | CTest names `sim.<script>` (label `unit`); in a build without SDL, `gadget-sim` without `--headless` runs a real-clock stdin console; `host auto` resolves only in real-clock runs (headless runs pass `--host`); the default `--snapshot-dir` is `firmware/tests/snapshots/<board>` (P2a deviation 6; §2.16) |
| D27 | Script `${turn}` | turn ids carry a per-boot random prefix (§2.12) that `--seed` does not fix, so a `--host script` run could not answer a turn | in `net_text`, every `${turn}` becomes the `turn` of the last `voice.begin` or `say` the gadget sent, and `net_text` fails with `no turn yet` before the first; Thinking, Speaking and Reply can then be snapshotted headless (D4, spec §5.7 and §10). P2a deviation 7; it closes P2b's deviation 2 (§2.16) |
| D28 | `host auto` on a paired gadget | spec §5.6 "waits for `host <address>`" against spec §4.3's backoff | §5.6's wait applies only when no `host_id` is stored. A paired gadget whose MausBot is not among the services found keeps browsing with the §4.3 backoff and prints the `hosts` line only on the first miss of a run (`ready`, `pair` and `host` start a new run), so the installer prompts once (P2a deviation 8; §2.11 status rule 3) |
| D29 | Gadget `event` | spec §4.7: `event` is informational in v1 | **pending Omkar's answer** (P2a deviation 9). If he accepts, makers send events with §2.10's `gadget_event_send` and this row is deleted. If he declines, this row reads "`event` is never sent in v1: core never sends it and makers have no API for it", and §2.10 and §3.15 change as §2.10 says |
