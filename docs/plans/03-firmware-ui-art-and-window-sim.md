# P2b — Firmware UI, Maus art and the simulator window: Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Draw every gadget screen of spec §5.5 with LVGL 9.6.0 from core's `ui_model_t`, with a layered, animated Maus generated from the OpenMausBot app's own mascot geometry, Latin-1 fonts, the simulator's SDL window mode, and golden-PNG snapshot tests at 466 round, 240×240 and 320×240.

**Architecture:** `tools/art` (Node, run by people, never by the firmware build) reads the app's mascot tables at a pinned commit with `git show`, renders each layer with resvg and emits committed LVGL C images; `npm run fonts` emits committed Latin-1 bitmap fonts with the locked `lv_font_conv`. `firmware/ui` is a dual-use library: pure C pieces (screen copy, the Maus animation engine) are unit-tested without drawing, and the LVGL layer (`ui.c`, `ui_screens.c`, `ui_maus.c`, `ui_pager.c`) is tested on LVGL's in-memory test display. `firmware/ports/sim` gains the LVGL display seam (`sim_display_lvgl.c`: test display when headless, SDL window otherwise, shared pointer, round mask, snapshots), the SDL event filter (`sim_sdl.c`) and SDL queued audio (`sim_audio_sdl.c`); P2a's main loop calls them through the pinned seams.

**Tech Stack:** C11, CMake ≥ 3.24, LVGL 9.6.0 (FetchContent, with its bundled lodepng), SDL2 (Homebrew `sdl2` = sdl2-compat 2.32.x; Ubuntu `libsdl2-dev`), Unity 2.7.0 + CTest (from P2a), Node ≥ 22.18 with type stripping (CI: Node 24), `@resvg/resvg-js` 2.6.2, `pngjs` 7.0.0, `lv_font_conv` 1.5.3, Montserrat Medium (OFL-1.1, shipped inside LVGL 9.6.0).

**Spec:** `/Users/omkar/Desktop/openmaus/openmausbot-gadget-sdk/docs/specs/2026-10-04-openmausbot-gadget-design.md` (v1.1; §5.5 Screens and the Maus, §5.7 Simulator, §10 UI and Art rows, §11) and the binding interface contract `/Users/omkar/Desktop/openmaus/openmausbot-gadget-sdk/docs/plans/00-interfaces.md` (§1.1, §1.4–§1.6, §2.1, §2.8, §2.9, §2.15, §2.16, §2.18, §5.1). Where they differ on names, types, paths or ownership, the contract wins; on behaviour, the spec wins.

## Global Constraints

- Repository: `/Users/omkar/Desktop/openmaus/openmausbot-gadget-sdk`. Branch **`p2b-ui`**, created from **`p2a-core`** (contract §1.3). Every command below runs from the repository root unless a step says `cd`.
- Publishing belongs to Omkar: never push, open a PR, create a release or dispatch a workflow. Commit on `p2b-ui` and stop.
- Original-work rule (spec §11): never open, fetch, quote or cite other gadget SDKs or voice-assistant firmware. Use only vendor and primary sources (LVGL, SDL, Espressif, npm package docs) and this repository's research.
- The OpenMausBot checkout at `/Users/omkar/Desktop/openmaus/OpenGrokBot` belongs to another session: only `git -C /Users/omkar/Desktop/openmaus/OpenGrokBot show <commit>:<path>` (used by `npm run extract`). No checkout, stash, reset, edit or fetch there.
- **LVGL 9.6.0** on the desktop: `https://github.com/lvgl/lvgl/archive/refs/tags/v9.6.0.tar.gz`, `URL_HASH SHA256=b20ee3acc1bba13c62d854f9ebd62e4c51e0b443b1e0225892e86442defa84df`. The ESP side pins `lvgl/lvgl: "9.6.0~1"` (P2c). Falling back to 9.5.0 happens only on both sides together; object flags therefore change only through `firmware/ui/ui_lv_compat.h`.
- **C11**, `CMAKE_C_EXTENSIONS OFF` for `gadget_ui`; every hand-written source compiles with `-Wall -Wextra -Wpedantic -Werror` and also as gnu23. `/* SPDX-License-Identifier: Apache-2.0 */` heads every source file we write (fonts: see Contract deviations).
- `firmware/ui` includes `lvgl.h` and core headers only (contract §2.1). Single thread: every 10 ms the port calls `core_tick(now)`, `ui_render(core_ui_model())`, `ui_tick(now)`; `ui_tick` calls `lv_timer_handler()` exactly once.
- **Fonts:** `lv_font_conv --font Montserrat-Medium.ttf -r 0x20-0x7E,0xA0-0xFF,0x2026,0x2192 --size <px> --bpp 4 --format lvgl --no-compress --lv-include lvgl.h`, run only through `npm run fonts` (the locked `node_modules/.bin/lv_font_conv`, never `npx`). Applied with `lv_obj_set_style_text_font()` on the UI root and widgets; `LV_USE_CUSTOM_FONT_DEFAULT` is never used.
- **Art** (spec §5.5, A20, contract §2.15): one RGB565A8 body per profile (`s240` body 201×240 for amoled-175c/amoled-175, `s150` body 125×150 for lcd-154/devkit), A8 eyes per expression × 4 blink steps (openness 1.0, 0.6, 0.25, 0.04), RGB565A8 closed mouths, RGB565A8 open mouths for expressions 19 and 6 at 3 levels; `MAUS_EXPR_COUNT 18`. Budgets: `s240` ≤ 524288 bytes, `s150` ≤ 262144 bytes (white-eye fallback: 819200 and 327680). Generated C is committed; the drift check runs on linux-x64 only.
- **State pools** (app expression numbers): idle [6,0,8], listening [1,10,19], thinking [17,8,16,14,5], working [10,7,16,11], sleeping [22,13,4], curious [21,3,0,15], notifying [21,3,0], alerting [21,3], speaking [19,6]. Expression changes happen under a blink. Motion is translation only; amplitudes are integers in 0.1 px (contract D17).
- **Palette:** body gradient `#8cd1b3` (0) → `#009957` (0.55) → `#005932` (1), corner to corner top-right to bottom-left; eyes and mouth `#ffffff`; open-mouth interior `#005932`. Screen on black: accent `#2fd187`, ok `#3ddc84`, bad `#ff5a4f`, warn `#ffb020`, ink `#f2f4f8`, mute `#9aa2b2`, dim `#6f7787`. `#007a45` never appears on the device screen.
- **Determinism:** core and UI use the xorshift32 PRNG of `gadget_util.h`; the headless simulator passes a fixed seed (`--seed`, default 1); LVGL draws integer-only. `--seed` does not fix the device key core generates, so every scripted snapshot run starts from a state folder holding only the RFC key of contract §1.7 as `dev_key` (`fresh_dir.cmake`), and the screen always shows `gad_b18b86ce1389e46d`.
- A8 eye images get `lv_obj_set_style_image_recolor(obj, lv_color_white(), 0)`; the body and mouth layers never set recolor.
- **Simulator:** headless never touches SDL (`lv_test_display_create` + RGB565, `lv_tick_inc` on the virtual clock, `lv_test_screenshot_compare`). Window mode: SDL event filter for Space (TALK), Esc (CANCEL) and the mouse (touch), each only where the board's `input_mask` has that source (Contract deviations 3), key repeat ignored, quit and window-close dropped, `LV_SDL_DIRECT_EXIT 0`, `LV_SDL_ACCELERATED 0` (SDL's software renderer, which SDL's dummy video driver in CI also has), one custom pointer device shared with the script, mic opened lazily on the first TALK with a permission hint after about 1 s of silence, `SDL_Quit` before every `execv` (in `sim_display_deinit`). Without a usable audio device (no driver, or a device that will not open) the mic and speaker are silent, never a HAL error.
- **Screen copy** is exactly the contract §2.15 table (unit-tested string by string in Task 4).
- **Node tools:** `engines.node >= 22.18`; `.ts` files run directly (erasable syntax only: no `enum`, no parameter properties, no namespaces; relative imports end in `.ts`); every install is `npm ci` once the lockfile exists.
- **P2a files** are edited only at the insertion points contract §5.1 gives P2b: append to `firmware/cmake/deps.cmake`, add `add_subdirectory(ui)` and the `ON` default to `firmware/CMakeLists.txt` (and, only if P2a ships one, remove its `GADGET_WITH_LVGL` `FATAL_ERROR` guard: Contract deviations 4), append to `firmware/ports/sim/CMakeLists.txt` and `firmware/tests/CMakeLists.txt`, append two lines to `.gitattributes`, append two jobs to `.github/workflows/ci.yml`.
- Trademark sentence, exactly: "The OpenMausBot name, the MausBot name and the Maus mascot are trademarks of Supamaus Software Private Limited."
- Commits end with the trailer `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.

## Review Focus

These are the inputs the spec implies but does not test that most likely bite a person using a gadget; each has its test in the owning task.

1. **Long or unbroken text** (a 95-character bot name with no space, an 8 KiB reply with no space, a 191-byte ask title, a 95-character host name, a toast with a 95-character bot name and 511 characters, a card with a 191-byte title and a 1535-byte body, all unbroken): text must wrap or cut inside the screen (inside the circle on round boards) and never overflow a buffer. Tests: `test_long_names_never_overflow` (Task 4), `test_long_unbroken_text_stays_on_screen` (Task 5: Idle, Reply, Ask, Setup Pairing and Bad code, Offline Unreachable and Protocol, the toast, the card).
2. **Out-of-range model values** from a core bug (`screen`, `maus`, `setup.step`, `offline.reason`, `update.phase` outside their enums): the UI draws defined, possibly empty text and never indexes past the art's state table or prints uninitialised memory. Tests: `test_out_of_range_enums_give_defined_text` (Task 4), `test_out_of_range_model_values_do_not_crash` (Task 5).
3. **Time passing with no model change** (`rev` unchanged): "Retrying in N s" counts down, long Setup/Offline copy turns its pages, an ask's buttons unlock after 0.6 s, speaking pages advance (also before `speak.end`, when the stream's length is unknown). Tests (Task 5): the countdown in `test_offline`, `test_ask_unlocks_without_a_model_change`, `test_pages_turn_without_a_model_change` (Setup Need code on lcd-154, Offline Unreachable on amoled-175c) and `test_speaking_pages_by_time_when_total_unknown`.
4. **An image before `image.end`** (`pixels == NULL`), a second picture with the same size, or a third that reuses the first descriptor slot with another size: nothing stale, no crash, the new pixels show. Tests: `test_image_without_pixels_draws_nothing` and the second and third pictures of `test_image` (Task 5); `ui_lv_requirements.h` keeps LVGL's image caches, which are keyed by the descriptor's address, off on every build.
5. **A machine without a usable audio device, and closing the window:** the simulator stays silent instead of failing, both when SDL audio cannot start and when it starts but no capture or playback device opens (a Mac mini without a mic, a Linux box without a source), and closing the window or Cmd-Q never makes LVGL exit or delete the display behind core's back. Tests (Task 6): `sim.audio_sdl.no_device` (no driver), `sim.audio_sdl.open_fails` (SDL's disk driver pointed at a missing folder, so neither device opens), `test_quit_and_close_are_dropped_but_remembered` (which also checks that `sim_display_deinit()` leaves SDL fully shut, as every `execv` restart needs).
6. **Model changes that change nothing drawn:** core bumps `rev` every 10–20 ms while listening (`mic_level`) and speaking (`speak_level`, the speaking clock). Re-laying out an 8 KiB reply on each of those ticks costs over a millisecond on this Mac and would eat the ESP32-S3's 10 ms loop, which also feeds the speaker. `ui_render` compares the model without those fields and re-lays out only on a real change. Test: `test_level_and_clock_changes_do_not_relayout` (Task 5).

---

## How this plan fits P2a (read before Task 1)

P2b builds on files P2a owns and that do not exist while this plan is written. The plan relies only on what contract §2.16 and §2.18 pin, and on these assumptions, each checked by a command in Task 1 Step 1:

| Assumption | Checked by | If it is false |
|---|---|---|
| `firmware/CMakeLists.txt` declares `option(GADGET_WITH_LVGL "<doc>" OFF)` **before** `include(cmake/deps.cmake)`, and has `add_subdirectory(core)`, `add_subdirectory(ports/sim)`, `add_subdirectory(tests)` | `grep -n` in Task 1 Step 1; the configure line `-- gadget: LVGL 9.6.0 UI on` in Task 3 Step 6 | Move P2b's deps block into a new file `firmware/cmake/lvgl.cmake` included right after the option; never reorder P2a's lines (P2b's test blocks are gated on options, not on targets, so the order of the `add_subdirectory` lines does not matter to them) |
| `firmware/CMakeLists.txt` has no `message(FATAL_ERROR ...)` for `GADGET_WITH_LVGL` (a P2a placeholder saying "plan P2b adds firmware/ui") | `grep -n 'FATAL_ERROR' firmware/CMakeLists.txt` in Task 1 Step 1 | Task 3 Step 5 removes exactly that three-line guard before adding `add_subdirectory(ui)` (Contract deviations 4); with it, every `-DGADGET_WITH_LVGL=ON` configure stops with "Configuring incomplete, errors occurred!" |
| `gadget-sim` lists `sim_display_null.c` and `ui_stub.c` in its `SOURCES` (or not at all) | Task 6 Step 5 (`nm` shows the LVGL UI) | The P2b block in `ports/sim/CMakeLists.txt` filters them by regex; if P2a links them through a library instead, stop and ask P2a to gate that library on `NOT GADGET_WITH_LVGL` |
| P2a's `main.c` calls `sim_display_*`, `ui_*` and `sim_audio_use(sim_audio_sdl_backend())` under `GADGET_WITH_SDL` as contract §2.16 says | Task 6 Steps 5–7 | Report to P2a; do not edit `main.c` |
| P2a's script engine implements `model`, `expect`, `snapshot`, `net_*` and fails the run when `sim_display_snapshot()` returns anything but 1 | Task 8 Step 2 (a missing golden fails the run) | Report to P2a |
| Core uses the `dev_key` it finds in `storage.json` (contract §4.6) and generates one only when there is none | Task 8 Step 4 (`setup_need_code` shows `gad_b18b86ce1389e46d`) | Report to P2a; the scripted goldens cannot be repeatable without a fixed identity |
| `gadget_core` exports `firmware/core/include`; `unity::framework` exists | Task 3 build | — |

The desktop build directory is `build/host` (contract §1.5); the snapshot-update build is `build/snap`.

## File Structure

```
tools/art/                          Node tools; run by people and CI, never by the firmware build
  package.json  package-lock.json   devDependencies: @resvg/resvg-js 2.6.2, lv_font_conv 1.5.3, pngjs 7.0.0
  parse.ts                          text parsers for the app's mascot sources (pure)
  extract.ts                        npm run extract: app commit -> source/*.json (git show only)
  svg.ts                            one SVG per layer (body, eyes, closed mouth, speaking mouth)
  lvgl.ts                           RGBA -> RGB565A8 / A8 with ordered dither; C emitter
  states.ts                         states.json -> maus_state_def_t rows (pure)
  build.ts                          npm run art: source -> firmware/ui/art + out/ previews
  maus_art.h.in                     contract §2.15 header, copied verbatim into firmware/ui/art
  fonts.ts                          npm run fonts: Montserrat -> firmware/ui/fonts
  budget.ts                         npm run budget: art bytes within the profile budgets
  test/*.test.ts                    node:test (parse, states, lvgl, svg, budget)
  source/                           maus-face.json maus-body-cursor.json states.json provenance.json (extracted),
                                    palette.json README.md (hand-written)
firmware/ui/                        [P2b] the LVGL UI (dual-use component / static lib gadget_ui)
  CMakeLists.txt  lv_conf.h  ui_lv_requirements.h  ui_lv_compat.h  README.md
  ui.c                              gadget_ui.h API: init, render cache (model compared without levels and clocks), tick, deinit
  ui_priv.h                         state shared by ui.c and ui_screens.c
  ui_screens.c                      every screen: captions, ask, card, image, update, battery, toast, ring
  ui_theme.h  ui_metrics.c          colours and per-board layout
  ui_pager.h  ui_pager.c            whole-line captions that page through long text, plus a host and a status line
  ui_maus.h   ui_maus.c             the Maus widget (body, eyes, mouth images)
  ui_maus_engine.h  ui_maus_engine.c  expressions, blinks, mouths, motion (pure, seeded)
  ui_copy.h   ui_copy.c             contract §2.15 copy (pure)
  art/maus_art.h  art/maus_art.c  art/s240/*.c  art/s150/*.c   generated by npm run art
  fonts/font_latin1_{14,16,20,24,28,40}.c  fonts/ui_fonts.h  generated by npm run fonts
firmware/ports/sim/                 [P2a dir; P2b adds]
  sim_display_lvgl.c                sim_display.h on LVGL: test display / SDL window, pointer, mask, snapshots
  sim_sdl.h  sim_sdl.c              window creation, event filter, shutdown
  sim_audio_sdl.c                   sim_audio_sdl_backend(): SDL queued audio
  CMakeLists.txt                    (append) library gadget_sim_lvgl; swaps the null display and UI stub out
firmware/tests/                     [P2a dir; P2b adds]
  test_ui_lv_compat.c test_ui_art.c test_ui_copy.c test_ui_maus_engine.c test_ui_screens.c
  test_sim_display.c test_sim_sdl.c test_sim_audio_sdl.c test_ui_snapshots.c
  fresh_dir.cmake                   empties a state folder before a scripted run (optionally seeding dev_key)
  scripts/snap_screens.txt  scripts/snap_update_{amoled-175c,lcd-154,devkit}.txt  scripts/window_smoke.txt
  snapshots/{amoled-175c,lcd-154,devkit}/*.png   committed goldens (fx_*.png from fixtures, the rest from scripts)
  CMakeLists.txt                    (append) CTest ui.*, sim.*, ui.snap.*
firmware/CMakeLists.txt             (modify) GADGET_WITH_LVGL default ON; add_subdirectory(ui)
firmware/cmake/deps.cmake           (append) LVGL 9.6.0, SDL2, GADGET_WITH_SDL, GADGET_SNAPSHOT_UPDATE
.gitattributes                      (append) firmware/ui/art/** and firmware/ui/fonts/** text eol=lf
.github/workflows/ci.yml            (append) jobs ui and art-drift
.github/workflows/art-regen.yml     workflow_dispatch: art + goldens regenerated on linux-x64
```

**Owned elsewhere (not built here):** core's screen selection, `ui_model_t` contents, speak/mic levels, `ui_layout_ask`/`ui_hit_test`/`ui_safe_area`, the toast's chime and every protocol behaviour (P2a); the simulator CLI, script engine, main loop, stdin console, file audio and re-exec (P2a); the ESP32 LVGL Kconfig, display/touch drivers, the art profile per board, `docs/hardware-checklist.md` (P2c); `THIRD_PARTY.md`, `NOTICE`, `AGENTS.md`, README and installer copy (P2d); every OpenMausBot change (P3a–P4b).

## Spec coverage

| Requirement | Task |
|---|---|
| §5.5 art from the app's geometry at a pinned commit (A20), not from old exports; provenance + trademark | 1, 2 |
| §5.5 layered images (RGB565A8 body, A8 eyes × 4 blink steps, mouths, offsets), profiles s240/s150 | 2 |
| §5.5 speaking state with 3 original open-mouth levels | 2 (art), 4 (engine), 5 (drawn), 7 (snapshots) |
| §5.5 expression changes under a blink; translation-only motion; pulse as a bob | 2 (states), 4 (engine) |
| §5.5 palette; screen colours | 2, 5 |
| §5.5 determinism (seeded xorshift32, fixed seed headless) | 4, 7 |
| §5.5 byte budgets checked in CI; drift check on linux-x64 only | 2, 3, 9 |
| §5.5/A21 Latin-1 fonts + `…` + `→` via styles on the UI root | 3, 5 |
| §5.5 screens: Idle, Listening (ring + countdown), Thinking (heard → working), Speaking (pages with audio), Reply (+ failed reason), Ask, Card, Image, Post toast, Setup (all steps, host_name, device id), Offline (reason, host_name, retry, Remote access and Windows hints), Update (progress) | 4 (copy), 5 (drawing), 7, 8 (snapshots) |
| §5.5 round layouts keep text inside the circle; battery arc at the top on boards with a battery | 5 |
| §5.1 LVGL config: `lv_conf.h` (sim), `ui_lv_requirements.h`, `ui_lv_compat.h` | 3 |
| §5.7 window mode: board-sized window, round mask, zoom, Space/Esc/mouse through an event filter, repeat ignored, quit/close dropped, one pointer device, Mac mic and speakers (silent without a usable device), lazy mic + permission hint, `SDL_Quit` before `execv` (all three with tests) | 6 |
| §5.7 headless: test display RGB565, virtual clock, scripted touch on the same pointer, `lv_test_screenshot_compare` with lodepng | 6, 7, 8 |
| §10 UI: snapshots of every screen at 466 round, 240×240, 320×240 compared to committed PNGs | 7, 8 |
| §10 Art: budget + drift check | 3, 9 |
| §6.5 open item "do A8 eyes with image_recolor draw white?" (simulator half) | 5 (`test_a8_eyes_draw_white`); device half in the hardware checks (Task 9 README, Task 10) |

---

### Task 1: Extract the Maus geometry from OpenMausBot

**Files:**
- Create: `tools/art/package.json`, `tools/art/package-lock.json` (generated), `tools/art/parse.ts`, `tools/art/extract.ts`, `tools/art/test/parse.test.ts`
- Create: `tools/art/source/palette.json`, `tools/art/source/README.md`
- Create (generated by `npm run extract`): `tools/art/source/maus-face.json`, `tools/art/source/maus-body-cursor.json`, `tools/art/source/states.json`, `tools/art/source/provenance.json`

**Interfaces:**
- Consumes: OpenMausBot commit `6dd4403d8fbbbd5c17169724cb2a529f11d7543e` files `src/components/cursor-face-data.ts`, `shared/mascot-bodies.ts`, `src/components/CursorAvatar.tsx` (tables at L110 `MOTION`, L480 `POOLS`, L680 `EXPR_CADENCE`, L840 `BLINK`), `android/app/src/main/kotlin/com/openmausbot/companion/ui/MausBodies.kt` (cursor bounds at L97–L100), read with `git show`.
- Produces: `parseArrayTable(src, marker): Record<string, number[] | null>`, `parseMotionTable(src, marker): Record<string, Motion>`, `parseCursorBody(bodiesTs, bodiesKt): CursorBody` (`parse.ts`); the JSON sources below, read by Task 2:
  - `maus-face.json`: `{face_box: number, face_centre: [x, y], mouth_stroke: number, expressions: [{id, eyes: [[x,y]×48]×2, mouth: [halfWidth, curve, gap, skew], mouth_frame: {x, y, angle}}]×25}`
  - `maus-body-cursor.json`: `{fit: {tx, ty, scale}, path: string, anchor: {x, y, scale}, bounds: {left, top, right, bottom}}`
  - `states.json`: `{states: {<name>: {pool, cadence: [min,max], blink: [min,max] | null, motion: {bob?|pulse?|jitter?|circle?: [amount, ms], sway?, tilt?, squash?}, source}}, speak: {expressions: [19, 6], open: [6, 11, 16], widen: 1.5}}`
  - `palette.json`: `{body_gradient: [[color, offset]×3], face, speak_interior, eye_format: "A8" | "RGB565A8"}`

- [ ] **Step 1: Branch from P2a and check its baseline and the assumptions above**

```bash
cd /Users/omkar/Desktop/openmaus/openmausbot-gadget-sdk
git status --short                       # expect no output
git checkout p2a-core && git checkout -b p2b-ui
cmake -S firmware -B build/host -DCMAKE_BUILD_TYPE=Debug -DGADGET_WITH_LVGL=OFF
cmake --build build/host -j10
ctest --test-dir build/host --output-on-failure -L unit
grep -n 'option(GADGET_WITH_LVGL\|option(GADGET_BUILD_SIM\|include(cmake/deps.cmake)\|add_subdirectory' firmware/CMakeLists.txt
grep -n 'FATAL_ERROR' firmware/CMakeLists.txt
ls firmware/ports/sim/sim_display.h firmware/ports/sim/sim_hal.h firmware/ports/sim/sim_display_null.c firmware/ports/sim/ui_stub.c
```

Expected: `100% tests passed`; the `option(GADGET_WITH_LVGL ... OFF)` line number is smaller than the `include(cmake/deps.cmake)` one; `option(GADGET_BUILD_SIM` exists (contract §2.18); `add_subdirectory(ports/sim)` comes before `add_subdirectory(tests)` (P2b's test blocks are gated on `GADGET_WITH_LVGL AND GADGET_BUILD_SIM`, not on `TARGET`, so they would still register in the other order, but note it); the `FATAL_ERROR` grep prints nothing about `GADGET_WITH_LVGL` (if it does, Task 3 Step 5 removes that guard); all four sim files exist. If the option comes after the include, note it: Task 3 Step 5 handles it.

- [ ] **Step 2: Create `tools/art/package.json` and install**

```json
{
  "name": "openmausbot-gadget-art",
  "private": true,
  "type": "module",
  "license": "Apache-2.0",
  "engines": {
    "node": ">=22.18"
  },
  "scripts": {
    "extract": "node extract.ts",
    "art": "node build.ts",
    "fonts": "node fonts.ts",
    "budget": "node budget.ts",
    "test": "node --test \"test/*.test.ts\""
  },
  "devDependencies": {
    "@resvg/resvg-js": "2.6.2",
    "lv_font_conv": "1.5.3",
    "pngjs": "7.0.0"
  }
}
```

```bash
cd tools/art && npm install --no-audit --no-fund && ls node_modules/.bin/lv_font_conv && cd ../..
```

Expected: `added 4 packages`, `package-lock.json` created (it records every `@resvg/resvg-js-*` platform binary, including `linux-x64-gnu` for CI), and `node_modules/.bin/lv_font_conv` exists.

- [ ] **Step 3: Write the failing parser test**

`tools/art/test/parse.test.ts`:

```ts
// SPDX-License-Identifier: Apache-2.0
import assert from 'node:assert/strict'
import { test } from 'node:test'
import { parseArrayTable, parseCursorBody, parseMotionTable } from '../parse.ts'

const AVATAR = `
export const POOLS = {
  idle: [
    6,
    0
  ],
  'odd-name': [
    3
  ],
}

const BLINK = {
  sleeping: null,
  idle: [
    1000,
    2000
  ],
}

export const MOTION = {
  // a comment line
  idle: { pulse: [0.014, 3600] },
  curious: { sway: [3.4, 1900], tilt: -4 },
}
`

test('array tables: multi-line arrays, quoted keys and null', () => {
  assert.deepEqual(parseArrayTable(AVATAR, 'export const POOLS = {'), { idle: [6, 0], 'odd-name': [3] })
  assert.deepEqual(parseArrayTable(AVATAR, 'const BLINK = {'), { sleeping: null, idle: [1000, 2000] })
})

test('motion table: pairs and scalars', () => {
  assert.deepEqual(parseMotionTable(AVATAR, 'export const MOTION = {'), {
    idle: { pulse: [0.014, 3600] },
    curious: { sway: [3.4, 1900], tilt: -4 },
  })
})

test('a missing table is an error, not an empty result', () => {
  assert.throws(() => parseArrayTable(AVATAR, 'const EXPR_CADENCE = {'), /not found/)
})

test('cursor body from the TS bodies file and the Kotlin bounds', () => {
  const ts = `export const MASCOT_BODIES = {
  cursor: {
    id: "cursor",
    fit: "translate(68.1612 9.8302) scale(0.593918)",
    body: "<path fill=\\"{{GRADIENT}}\\" d=\\"M0 0 C1 1 2 2 3 3 Z\\"/>",
    anchor: { x: 85.54, y: 106.35, scale: 0.791 },
  },
  blob: {
  },
}`
  const kt = `        "cursor" to Body(
            left = 18.7298f,
            top = 0f,
            right = 209.8112f,
            bottom = 228.541f,
        ),`
  assert.deepEqual(parseCursorBody(ts, kt), {
    fit: { tx: 68.1612, ty: 9.8302, scale: 0.593918 },
    path: 'M0 0 C1 1 2 2 3 3 Z',
    anchor: { x: 85.54, y: 106.35, scale: 0.791 },
    bounds: { left: 18.7298, top: 0, right: 209.8112, bottom: 228.541 },
  })
})
```

- [ ] **Step 4: Run it and watch it fail**

Run: `cd tools/art && npm test; cd ../..`
Expected: FAIL with `ERR_MODULE_NOT_FOUND` for `parse.ts`.

- [ ] **Step 5: Write `tools/art/parse.ts`**

```ts
// SPDX-License-Identifier: Apache-2.0
// Text parsers for the OpenMausBot mascot sources (pure; no I/O).

export type Motion = Record<string, number | [number, number]>

export interface CursorBody {
  fit: { tx: number; ty: number; scale: number }
  path: string
  anchor: { x: number; y: number; scale: number }
  bounds: { left: number; top: number; right: number; bottom: number }
}

function num(s: string): number {
  const v = Number(s)
  if (!Number.isFinite(v)) throw new Error(`not a number: ${s}`)
  return v
}

/** The `cursor: { ... }` entry of shared/mascot-bodies.ts. */
export function parseCursorBody(bodiesTs: string, bodiesKt: string): Omit<CursorBody, never> {
  const start = bodiesTs.indexOf('\n  cursor: {')
  if (start < 0) throw new Error('mascot-bodies.ts: no cursor entry')
  const end = bodiesTs.indexOf('\n  },', start)
  const block = bodiesTs.slice(start, end)
  const fit = /fit: "translate\(([-\d.]+) ([-\d.]+)\) scale\(([-\d.]+)\)"/.exec(block)
  const body = /body: ("(?:[^"\\]|\\.)*")/.exec(block)
  const anchor = /anchor: \{ x: ([-\d.]+), y: ([-\d.]+), scale: ([-\d.]+) \}/.exec(block)
  if (!fit || !body || !anchor) throw new Error('mascot-bodies.ts: cursor entry has an unexpected shape')
  const markup = JSON.parse(body[1]) as string
  const d = /d="([^"]+)"/.exec(markup)
  if (!d) throw new Error('mascot-bodies.ts: cursor body has no path')

  const ktStart = bodiesKt.indexOf('"cursor" to Body(')
  if (ktStart < 0) throw new Error('MausBodies.kt: no cursor entry')
  const kt = bodiesKt.slice(ktStart, bodiesKt.indexOf('\n        ),', ktStart))
  const side = (name: string): number => {
    const m = new RegExp(`${name} = ([-\\d.]+)f`).exec(kt)
    if (!m) throw new Error(`MausBodies.kt: cursor has no ${name}`)
    return num(m[1])
  }
  return {
    fit: { tx: num(fit[1]), ty: num(fit[2]), scale: num(fit[3]) },
    path: d[1],
    anchor: { x: num(anchor[1]), y: num(anchor[2]), scale: num(anchor[3]) },
    bounds: { left: side('left'), top: side('top'), right: side('right'), bottom: side('bottom') },
  }
}

/** The object literal that starts at `marker` and ends at the first line that is exactly "}". */
function objectBody(src: string, marker: string): string {
  const start = src.indexOf(marker)
  if (start < 0) throw new Error(`CursorAvatar.tsx: ${marker} not found`)
  const end = src.indexOf('\n}', start)
  return src.slice(start + marker.length, end)
}

/** `name: [a, b, ...]` or `name: null` entries of POOLS / EXPR_CADENCE / BLINK. */
export function parseArrayTable(src: string, marker: string): Record<string, number[] | null> {
  const body = objectBody(src, marker)
  const out: Record<string, number[] | null> = {}
  const re = /^ {2}'?([\w-]+)'?: (null|\[[^\]]*\])/gm
  let m: RegExpExecArray | null
  while ((m = re.exec(body))) {
    out[m[1]] = m[2] === 'null' ? null : m[2].slice(1, -1).split(',').map(s => s.trim()).filter(Boolean).map(num)
  }
  return out
}

/** `name: { bob: [2, 2600], tilt: -4 }` entries of MOTION. */
export function parseMotionTable(src: string, marker: string): Record<string, Motion> {
  const body = objectBody(src, marker)
  const out: Record<string, Motion> = {}
  const re = /^ {2}'?([\w-]+)'?: \{([^}]*)\}/gm
  let m: RegExpExecArray | null
  while ((m = re.exec(body))) {
    const motion: Motion = {}
    const inner = /(\w+): (\[[^\]]*\]|-?[\d.]+)/g
    let f: RegExpExecArray | null
    while ((f = inner.exec(m[2]))) {
      if (f[2].startsWith('[')) {
        const pair = f[2].slice(1, -1).split(',').map(s => num(s.trim()))
        if (pair.length !== 2) throw new Error(`MOTION.${m[1]}.${f[1]}: expected [amount, ms]`)
        motion[f[1]] = [pair[0], pair[1]]
      } else {
        motion[f[1]] = num(f[2])
      }
    }
    out[m[1]] = motion
  }
  return out
}
```

- [ ] **Step 6: Run the test and watch it pass**

Run: `cd tools/art && npm test; cd ../..`
Expected: `# pass 4`, `# fail 0`.

- [ ] **Step 7: Write `tools/art/extract.ts`, `source/palette.json` and `source/README.md`**

`tools/art/extract.ts`:

```ts
// SPDX-License-Identifier: Apache-2.0
// Re-pin the Maus art source: read the app's mascot geometry at one commit
// with `git show` (never touching the app's working tree) and write
// source/maus-face.json, source/maus-body-cursor.json, source/states.json
// and source/provenance.json.
//
//   npm run extract -- --app <OpenMausBot checkout> --commit <40-hex sha>
import { execFileSync } from 'node:child_process'
import { createHash } from 'node:crypto'
import { mkdtempSync, rmSync, writeFileSync } from 'node:fs'
import { tmpdir } from 'node:os'
import { dirname, join } from 'node:path'
import { fileURLToPath, pathToFileURL } from 'node:url'
import { parseArrayTable, parseCursorBody, parseMotionTable } from './parse.ts'

const HERE = dirname(fileURLToPath(import.meta.url))
const SOURCE = join(HERE, 'source')
const APP_REPO = 'https://github.com/milind-soni/OpenMausBot'
const FILES = {
  face: 'src/components/cursor-face-data.ts',
  bodies: 'shared/mascot-bodies.ts',
  bodiesKt: 'android/app/src/main/kotlin/com/openmausbot/companion/ui/MausBodies.kt',
  avatar: 'src/components/CursorAvatar.tsx',
}
// The app states the gadget uses (contract §2.15), in ui_maus_state_t order.
const APP_STATES = ['idle', 'listening', 'thinking', 'working', 'sleeping', 'curious', 'notifying', 'alerting']
// Gadget-only state: new, original art and timing (spec §5.5). Not from the app.
const SPEAKING = {
  pool: [19, 6],
  cadence: [3000, 6000],
  blink: [2500, 5000],
  motion: { bob: [1.5, 1800] },
  source: 'gadget-only (original to this SDK)',
}
const SPEAK = { expressions: [19, 6], open: [6, 11, 16], widen: 1.5 }

function arg(name: string): string {
  const i = process.argv.indexOf(name)
  if (i < 0 || !process.argv[i + 1]) {
    console.error('usage: npm run extract -- --app <OpenMausBot checkout> --commit <sha>')
    process.exit(2)
  }
  return process.argv[i + 1]
}

const app = arg('--app')
const commit = arg('--commit')
if (!/^[0-9a-f]{40}$/.test(commit)) {
  console.error('--commit must be a full 40-hex commit id')
  process.exit(2)
}
const show = (path: string): string =>
  execFileSync('git', ['-C', app, 'show', `${commit}:${path}`], { encoding: 'utf8', maxBuffer: 64 << 20 })

const text = {
  face: show(FILES.face),
  bodies: show(FILES.bodies),
  bodiesKt: show(FILES.bodiesKt),
  avatar: show(FILES.avatar),
}

// cursor-face-data.ts is plain TypeScript with erasable types: load it with
// Node's type stripping from a private temp folder (never under node_modules).
const tmp = mkdtempSync(join(tmpdir(), 'omb-art-'))
let face: {
  FACE_BOX: number
  FACE_CENTRE: [number, number]
  MOUTH_STROKE: number
  EXPRESSIONS: [number, number][][][]
  MOUTHS: number[][]
  mouthFrame: (rings: [number, number][][], spec: number[]) => { x: number; y: number; angle: number }
}
try {
  const file = join(tmp, 'cursor-face-data.ts')
  writeFileSync(file, text.face)
  face = await import(pathToFileURL(file).href)
} finally {
  rmSync(tmp, { recursive: true, force: true })
}

const r6 = (v: number): number => Math.round(v * 1e6) / 1e6
const expressions = face.EXPRESSIONS.map((rings, id) => {
  const frame = face.mouthFrame(rings, face.MOUTHS[id])
  return {
    id,
    eyes: rings.map(ring => ring.map(([x, y]) => [r6(x), r6(y)])),
    mouth: face.MOUTHS[id],
    mouth_frame: { x: r6(frame.x), y: r6(frame.y), angle: r6(frame.angle) },
  }
})

const pools = parseArrayTable(text.avatar, 'export const POOLS = {')
const cadence = parseArrayTable(text.avatar, 'const EXPR_CADENCE = {')
const blink = parseArrayTable(text.avatar, 'const BLINK = {')
const motion = parseMotionTable(text.avatar, 'export const MOTION = {')
const states: Record<string, unknown> = {}
for (const name of APP_STATES) {
  const pool = pools[name]
  const cad = cadence[name]
  if (!pool || !cad || !(name in blink) || !motion[name]) throw new Error(`CursorAvatar.tsx: state ${name} is incomplete`)
  states[name] = { pool, cadence: cad, blink: blink[name], motion: motion[name], source: FILES.avatar }
}
states.speaking = SPEAKING

const sha = (s: string): string => createHash('sha256').update(s, 'utf8').digest('hex')
const write = (name: string, value: unknown): void => {
  writeFileSync(join(SOURCE, name), JSON.stringify(value, null, 1) + '\n')
  console.log(`wrote source/${name}`)
}

write('maus-face.json', {
  face_box: face.FACE_BOX,
  face_centre: face.FACE_CENTRE,
  mouth_stroke: face.MOUTH_STROKE,
  expressions,
})
write('maus-body-cursor.json', parseCursorBody(text.bodies, text.bodiesKt))
write('states.json', { states, speak: SPEAK })
write('provenance.json', {
  app_repo: APP_REPO,
  commit,
  license: 'Apache-2.0',
  files: Object.fromEntries((Object.keys(FILES) as (keyof typeof FILES)[]).map(k => [FILES[k], sha(text[k])])),
})
```

`tools/art/source/palette.json` (from `src/lib/mascot.ts` `MAUS_COLORS.green` and `src/components/Avatar.tsx` `gradientFor` at the pinned commit; `eye_format` switches to `"RGB565A8"` only for the white-eye fallback):

```json
{
 "body_gradient": [["#8cd1b3", 0], ["#009957", 0.55], ["#005932", 1]],
 "face": "#ffffff",
 "speak_interior": "#005932",
 "eye_format": "A8"
}
```

`tools/art/source/README.md`:

```markdown
# Maus art source

The Maus on the gadget is generated by `tools/art` from the mascot geometry of
the OpenMausBot app, so the gadget and the app draw the same character.

| File | What | Where it comes from |
|---|---|---|
| `maus-face.json` | 25 expressions: two 48-point eye outlines each, the mouth spec `[halfWidth, curve, gap, skew]` and the mouth frame `{x, y, angle}` | `src/components/cursor-face-data.ts` (`EXPRESSIONS`, `MOUTHS`, `mouthFrame()`) |
| `maus-body-cursor.json` | The cursor body outline, its fit transform, the face anchor and the body's bounds in face-box units | `shared/mascot-bodies.ts` (`MASCOT_BODIES.cursor`) and `android/app/src/main/kotlin/com/openmausbot/companion/ui/MausBodies.kt` (bounds). `npm run art` renders the outline and stops if any side of the Kotlin bounds is more than 0.5 face units from it ("MausBodies.kt bounds disagree with shared/mascot-bodies.ts"), so a drifted copy never clips or shifts the body |
| `states.json` | Expression pool, expression cadence, blink rhythm and body motion per state | `src/components/CursorAvatar.tsx` (`POOLS`, `EXPR_CADENCE`, `BLINK`, `MOTION`); the `speaking` state and the `speak` open-mouth levels are new, original to this SDK |
| `palette.json` | Body gradient `#8cd1b3` / `#009957` / `#005932`, white face, the speaking mouth's interior, the eye image format | `src/lib/mascot.ts` (`MAUS_COLORS.green`) and `src/components/Avatar.tsx` (`gradientFor`) |
| `provenance.json` | App repository, pinned commit and the SHA-256 of every file read | written by `npm run extract` |

**Pinned commit:** OpenMausBot (<https://github.com/milind-soni/OpenMausBot>,
Apache-2.0) at `6dd4403d8fbbbd5c17169724cb2a529f11d7543e`. The app's code and
data are Apache-2.0 licensed; `tools/art` reads them with `git show` and writes
these JSON files, so building the SDK never needs the app checkout.

**Original to this SDK:** the `speaking` state (pool 19 and 6, its timing and
motion), the three open-mouth levels drawn as closed lenses, the blink-cut
expression switch, and the translation-only motion (the app's breathing pulse
becomes a vertical bob of `pulse × face_box / 2` face units; sway, tilt and
squash are not used in v1).

**Re-pinning** (rare): `cd tools/art && npm run extract -- --app <OpenMausBot checkout> --commit <sha>`,
then `npm run art && npm run budget` and review `tools/art/out/<profile>/*.png`.

**Pre-publish check (not a build blocker):** before the SDK is published,
Omkar confirms in writing that the 25-expression geometry is project-owned and
records the confirmation here.

The OpenMausBot name, the MausBot name and the Maus mascot are trademarks of Supamaus Software Private Limited.
```

- [ ] **Step 8: Extract at the pinned commit and check the result**

```bash
cd tools/art
npm run extract -- --app /Users/omkar/Desktop/openmaus/OpenGrokBot --commit 6dd4403d8fbbbd5c17169724cb2a529f11d7543e
node -e "
const f = require('./source/maus-face.json'), b = require('./source/maus-body-cursor.json'), s = require('./source/states.json'), p = require('./source/provenance.json');
console.log(f.expressions.length, f.expressions[6].eyes[0].length, JSON.stringify(f.expressions[6].mouth_frame));
console.log(JSON.stringify(b.bounds), JSON.stringify(b.anchor));
console.log(Object.keys(s.states).join(','));
console.log(p.commit, Object.keys(p.files).length);"
cd ../..
```

Expected:

```text
wrote source/maus-face.json
wrote source/maus-body-cursor.json
wrote source/states.json
wrote source/provenance.json
25 48 {"x":113.604076,"y":161.047993,"angle":0.164423}
{"left":18.7298,"top":0,"right":209.8112,"bottom":228.541} {"x":85.54,"y":106.35,"scale":0.791}
idle,listening,thinking,working,sleeping,curious,notifying,alerting,speaking
6dd4403d8fbbbd5c17169724cb2a529f11d7543e 4
```

`states.json` must hold idle `[6,0,8]`, listening `[1,10,19]`, thinking `[17,8,16,14,5]`, working `[10,7,16,11]`, sleeping `[22,13,4]` (blink `null`), curious `[21,3,0,15]`, notifying `[21,3,0]`, alerting `[21,3]` (blink `null`), speaking `[19,6]`.

- [ ] **Step 9: Extraction is repeatable**

```bash
git add tools/art
(cd tools/art && npm run extract -- --app /Users/omkar/Desktop/openmaus/OpenGrokBot --commit 6dd4403d8fbbbd5c17169724cb2a529f11d7543e > /dev/null)
git diff --exit-code tools/art/source && echo repeatable
```

Expected: `repeatable`. (`node_modules/` and `tools/art/out/` are ignored by P1's `.gitignore`.)

- [ ] **Step 10: Commit**

```bash
git add tools/art
git commit -m "feat(art): extract the Maus geometry from OpenMausBot at 6dd4403" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 2: Generate the layered Maus art

**Files:**
- Create: `tools/art/svg.ts`, `tools/art/lvgl.ts`, `tools/art/states.ts`, `tools/art/build.ts`, `tools/art/maus_art.h.in`
- Test: `tools/art/test/lvgl.test.ts`, `tools/art/test/states.test.ts`, `tools/art/test/svg.test.ts`
- Create (generated by `npm run art`, committed): `firmware/ui/art/maus_art.h`, `firmware/ui/art/maus_art.c`, `firmware/ui/art/s240/{body,eyes,mouths,speak,tables}.c`, `firmware/ui/art/s150/{body,eyes,mouths,speak,tables}.c`
- Modify: `.gitattributes` (append two lines)

**Interfaces:**
- Consumes: Task 1's JSON sources.
- Produces (contract §2.15): `maus_art.h` exactly as pinned (`MAUS_EXPR_COUNT 18`, `MAUS_BLINK_STEPS 4`, `MAUS_SPEAK_EXPRS 2`, `MAUS_SPEAK_LEVELS 3`, `maus_layer_t`, `maus_state_def_t`, `maus_art_t`, `maus_art_s240`, `maus_art_s150`, `maus_art_for()`); image symbols `maus_<profile>_body`, `maus_<profile>_eye_<expr>_<step>`, `maus_<profile>_mouth_<expr>`, `maus_<profile>_speak_<expr>_<level>`; `maus_art_for()` returns a profile only when `MAUS_ART_HAS_S240` / `MAUS_ART_HAS_S150` is defined (Task 3's CMake defines both on the desktop, one on ESP32). `expr_ids` = `{0, 1, 3, 4, 5, 6, 7, 8, 10, 11, 13, 14, 15, 16, 17, 19, 21, 22}`; `states[UI_MAUS_*]` rows in `ui_maus_state_t` order. Node exports: `trim`, `rgb565`, `toLvImage`, `cImage`, `C_HEADER` (lvgl.ts); `bodySvg`, `eyesSvg`, `mouthSvg`, `speakMouthSvg`, `previewSvg` (svg.ts); `STATE_ORDER`, `unionOfPools`, `stateDefs`, `toTenthsPx` (states.ts).

- [ ] **Step 1: Write the failing tests**

`tools/art/test/lvgl.test.ts`:

```ts
// SPDX-License-Identifier: Apache-2.0
import assert from 'node:assert/strict'
import { test } from 'node:test'
import { cImage, rgb565, toLvImage, trim, type Rgba } from '../lvgl.ts'

function rgba(width: number, height: number, px: (x: number, y: number) => [number, number, number, number]): Rgba {
  const data = new Uint8Array(width * height * 4)
  for (let y = 0; y < height; y++) for (let x = 0; x < width; x++) data.set(px(x, y), (y * width + x) * 4)
  return { width, height, data }
}

test('rgb565 keeps the extremes exact under the dither', () => {
  for (let y = 0; y < 4; y++) {
    for (let x = 0; x < 4; x++) {
      assert.equal(rgb565(255, 255, 255, x, y), 0xffff)
      assert.equal(rgb565(0, 0, 0, x, y), 0x0000)
    }
  }
  assert.equal(rgb565(0, 153, 87, 0, 0), (0 << 11) | (38 << 5) | 10) // #009957, Bayer threshold 0
})

test('trim crops to alpha > 0 and reports the offset', () => {
  const img = rgba(10, 8, (x, y) => (x >= 3 && x <= 5 && y >= 2 && y <= 6 ? [255, 255, 255, 255] : [0, 0, 0, 0]))
  const t = trim(img)
  assert.deepEqual([t.x, t.y, t.image.width, t.image.height], [3, 2, 3, 5])
  assert.throws(() => trim(rgba(2, 2, () => [0, 0, 0, 0])), /fully transparent/)
})

test('RGB565A8 is the colour plane then the alpha plane', () => {
  const img = rgba(2, 1, x => (x === 0 ? [255, 255, 255, 255] : [255, 0, 0, 128]))
  const lv = toLvImage('t', img, 'RGB565A8')
  assert.equal(lv.stride, 4)
  assert.deepEqual(Array.from(lv.data), [0xff, 0xff, 0x00, 0xf8, 255, 128])
})

test('A8 keeps only alpha, and transparent pixels carry no colour', () => {
  const img = rgba(3, 1, x => [200, 10, 10, x * 100])
  assert.deepEqual(Array.from(toLvImage('e', img, 'A8').data), [0, 100, 200])
  assert.deepEqual(Array.from(toLvImage('m', img, 'RGB565A8').data.subarray(0, 2)), [0, 0])
})

test('C output is an LVGL 9 image descriptor', () => {
  const c = cImage(toLvImage('maus_s150_eye_6_0', rgba(2, 2, () => [255, 255, 255, 255]), 'A8'))
  assert.match(c, /static const LV_ATTRIBUTE_MEM_ALIGN uint8_t maus_s150_eye_6_0_map\[\] = \{\n  0xff,0xff,0xff,0xff\n\};/)
  assert.match(c, /\.cf = LV_COLOR_FORMAT_A8,/)
  assert.match(c, /\.w = 2,\n    \.h = 2,\n    \.stride = 2,/)
  assert.match(c, /\.data_size = sizeof\(maus_s150_eye_6_0_map\),/)
})
```

`tools/art/test/states.test.ts`:

```ts
// SPDX-License-Identifier: Apache-2.0
import assert from 'node:assert/strict'
import { readFileSync } from 'node:fs'
import { test } from 'node:test'
import { stateDefs, toTenthsPx, unionOfPools, type StatesFile } from '../states.ts'

const states = JSON.parse(readFileSync(new URL('../source/states.json', import.meta.url), 'utf8')) as StatesFile

test('the committed pools cover exactly the 18 expressions of maus_art.h', () => {
  assert.deepEqual(unionOfPools(states), [0, 1, 3, 4, 5, 6, 7, 8, 10, 11, 13, 14, 15, 16, 17, 19, 21, 22])
})

test('pulse becomes a bob of pulse * face_box / 2, added to a bob of the same period', () => {
  const defs = stateDefs(states, 228.541, 240, 228.541)
  const listening = defs.find(d => d.key === 'listening')!
  // bob 2 + pulse 0.012 * 114.2705 = 3.371 face units -> 3.54 px at 240 -> 35 tenths
  assert.deepEqual(listening.bob, [35, 2600])
  const idle = defs.find(d => d.key === 'idle')!
  assert.deepEqual(idle.bob, [17, 3600])
  assert.deepEqual(defs.find(d => d.key === 'alerting')!.jitter, [27, 85])
  assert.deepEqual(defs.find(d => d.key === 'sleeping')!.blink, [0, 0])
  assert.equal(defs.map(d => d.enumName).join(','),
    'UI_MAUS_IDLE,UI_MAUS_LISTENING,UI_MAUS_THINKING,UI_MAUS_WORKING,UI_MAUS_SPEAKING,UI_MAUS_SLEEPING,UI_MAUS_CURIOUS,UI_MAUS_NOTIFYING,UI_MAUS_ALERTING')
})

test('sway, tilt and squash are dropped (translation only in v1)', () => {
  const curious = stateDefs(states, 228.541, 240, 228.541).find(d => d.key === 'curious')!
  assert.deepEqual([curious.bob, curious.jitter, curious.circle], [[0, 0], [0, 0], [0, 0]])
})

test('bob and pulse with different periods are refused', () => {
  const bad = structuredClone(states)
  bad.states.listening.motion = { bob: [2, 2600], pulse: [0.012, 3000] }
  assert.throws(() => stateDefs(bad, 228.541, 240, 228.541), /periods differ/)
})

test('a speaking expression outside every pool is refused', () => {
  const bad = structuredClone(states)
  bad.speak.expressions = [19, 2]
  assert.throws(() => unionOfPools(bad), /speaking expression 2/)
})

test('face units to tenths of a pixel', () => {
  assert.equal(toTenthsPx(228.541, 150, 228.541), 1500)
  assert.equal(toTenthsPx(1, 240, 228.541), 11)
})
```

`tools/art/test/svg.test.ts`:

```ts
// SPDX-License-Identifier: Apache-2.0
import assert from 'node:assert/strict'
import { readFileSync } from 'node:fs'
import { test } from 'node:test'
import { bodySvg, eyesSvg, mouthSvg, speakMouthSvg, type Body, type Face, type Palette } from '../svg.ts'

const read = (n: string): unknown => JSON.parse(readFileSync(new URL(`../source/${n}`, import.meta.url), 'utf8'))
const face = read('maus-face.json') as Face
const body = read('maus-body-cursor.json') as Body
const palette = read('palette.json') as Palette

test('every layer shares the body-tight viewBox', () => {
  const vb = 'viewBox="18.73 0 191.081 228.541"'
  assert.ok(bodySvg(body, palette).includes(vb))
  assert.ok(eyesSvg(face, body, face.expressions[6], 1, '#ffffff').includes(vb))
  assert.ok(mouthSvg(face, body, face.expressions[6], '#ffffff').includes(vb))
})

test('the body gradient runs top-right to bottom-left with the app palette', () => {
  const svg = bodySvg(body, palette)
  assert.match(svg, /x1="1" y1="0" x2="0" y2="1"/)
  assert.match(svg, /offset="0" stop-color="#8cd1b3".*offset="0.55" stop-color="#009957".*offset="1" stop-color="#005932"/)
})

test('a closed blink squashes each eye to 4 % of its height', () => {
  const ys = (svg: string): number[] =>
    [...svg.matchAll(/[ML](-?[\d.]+) (-?[\d.]+)/g)].slice(0, 48).map(m => Number(m[2]))
  const open = ys(eyesSvg(face, body, face.expressions[6], 1, '#fff'))
  const shut = ys(eyesSvg(face, body, face.expressions[6], 0.04, '#fff'))
  const span = (v: number[]): number => Math.max(...v) - Math.min(...v)
  assert.ok(Math.abs(span(shut) / span(open) - 0.04) < 0.01)
})

test('speaking mouths are closed lenses filled with the shadow green', () => {
  const svg = speakMouthSvg(face, body, face.expressions[19], 16, 3, 1.5, '#ffffff', '#005932')
  assert.match(svg, /d="M[^"]+ Q[^"]+ Q[^"]+ Z" fill="#005932" stroke="#ffffff"/)
})
```

- [ ] **Step 2: Run them and watch them fail**

Run: `cd tools/art && npm test; cd ../..`
Expected: `parse.test.ts` passes; the three new files fail with `ERR_MODULE_NOT_FOUND` (`lvgl.ts`, `states.ts`, `svg.ts`).

- [ ] **Step 3: Write `tools/art/lvgl.ts`**

The RGB565A8 layout is LVGL 9's: the whole RGB565 plane (little-endian, stride `w*2`), then the whole A8 plane. A 4×4 ordered dither (bias `0..step-1` before truncation) avoids banding in the gradient; fully transparent pixels carry colour 0.

```ts
// SPDX-License-Identifier: Apache-2.0
// RGBA (straight alpha) -> LVGL 9 image data, and the C text for it.
//
// RGB565A8 (LVGL 9 layout): the whole RGB565 plane first (little-endian
// uint16 per pixel, stride w*2), then the whole A8 plane (stride w).
// A8: one alpha byte per pixel; LVGL draws it in the image_recolor colour.

export interface Rgba {
  width: number
  height: number
  data: Uint8Array        // straight (not premultiplied) RGBA, row-major
}
export type ColorFormat = 'RGB565A8' | 'A8'
export interface LvImage {
  name: string
  cf: ColorFormat
  w: number
  h: number
  stride: number
  data: Uint8Array
}
export interface Trimmed {
  image: Rgba
  x: number
  y: number
}

const BAYER4 = [0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5]

/** Crop to the bounding box of alpha > 0. A fully transparent input is an error. */
export function trim(src: Rgba): Trimmed {
  let x0 = src.width, y0 = src.height, x1 = -1, y1 = -1
  for (let y = 0; y < src.height; y++) {
    for (let x = 0; x < src.width; x++) {
      if (src.data[(y * src.width + x) * 4 + 3] !== 0) {
        if (x < x0) x0 = x
        if (x > x1) x1 = x
        if (y < y0) y0 = y
        if (y > y1) y1 = y
      }
    }
  }
  if (x1 < 0) throw new Error('trim: layer is fully transparent')
  const w = x1 - x0 + 1
  const h = y1 - y0 + 1
  const data = new Uint8Array(w * h * 4)
  for (let y = 0; y < h; y++) {
    const from = ((y0 + y) * src.width + x0) * 4
    data.set(src.data.subarray(from, from + w * 4), y * w * 4)
  }
  return { image: { width: w, height: h, data }, x: x0, y: y0 }
}

/** RGB565 with a 4x4 ordered dither: bias 0..step-1 before truncation. Transparent pixels are 0. */
export function rgb565(r: number, g: number, b: number, x: number, y: number): number {
  const t = BAYER4[(y & 3) * 4 + (x & 3)]
  const r5 = Math.min(31, (r + (t >> 1)) >> 3)
  const g6 = Math.min(63, (g + (t >> 2)) >> 2)
  const b5 = Math.min(31, (b + (t >> 1)) >> 3)
  return (r5 << 11) | (g6 << 5) | b5
}

export function toLvImage(name: string, src: Rgba, cf: ColorFormat): LvImage {
  const { width: w, height: h, data } = src
  if (cf === 'A8') {
    const out = new Uint8Array(w * h)
    for (let i = 0; i < w * h; i++) out[i] = data[i * 4 + 3]
    return { name, cf, w, h, stride: w, data: out }
  }
  const out = new Uint8Array(w * h * 3)
  for (let y = 0; y < h; y++) {
    for (let x = 0; x < w; x++) {
      const i = y * w + x
      const a = data[i * 4 + 3]
      const c = a === 0 ? 0 : rgb565(data[i * 4], data[i * 4 + 1], data[i * 4 + 2], x, y)
      out[i * 2] = c & 0xff
      out[i * 2 + 1] = c >> 8
      out[w * h * 2 + i] = a
    }
  }
  return { name, cf, w, h, stride: w * 2, data: out }
}

export function cBytes(data: Uint8Array): string {
  const lines: string[] = []
  for (let i = 0; i < data.length; i += 24) {
    lines.push('  ' + Array.from(data.subarray(i, i + 24), v => '0x' + v.toString(16).padStart(2, '0')).join(','))
  }
  return lines.join(',\n')
}

/** One image as C: a static byte array plus a public lv_image_dsc_t. */
export function cImage(img: LvImage): string {
  return `static const LV_ATTRIBUTE_MEM_ALIGN uint8_t ${img.name}_map[] = {\n${cBytes(img.data)}\n};\n\n` +
    `const lv_image_dsc_t ${img.name} = {\n` +
    `  .header = {\n` +
    `    .magic = LV_IMAGE_HEADER_MAGIC,\n` +
    `    .cf = LV_COLOR_FORMAT_${img.cf},\n` +
    `    .flags = 0,\n` +
    `    .w = ${img.w},\n` +
    `    .h = ${img.h},\n` +
    `    .stride = ${img.stride},\n` +
    `  },\n` +
    `  .data_size = sizeof(${img.name}_map),\n` +
    `  .data = ${img.name}_map,\n` +
    `};\n`
}

export const C_HEADER = '/* SPDX-License-Identifier: Apache-2.0 */\n' +
  '/* GENERATED by tools/art (npm run art). Do not edit.\n' +
  ' * Geometry: OpenMausBot (Apache-2.0); see tools/art/source/README.md. */\n'
```

- [ ] **Step 4: Write `tools/art/svg.ts`**

Every layer shares one viewBox, the cursor body's tight bounds in face-box units, so a layer rendered at the profile height lines up pixel for pixel with the body; the face group is `translate(anchor) scale(anchor.scale) translate(-face_centre)`, as the app draws it with gaze 0. The speaking mouth is new: a closed lens between an upper and a lower quadratic, `open` face units deep, widened by `widen × level`.

```ts
// SPDX-License-Identifier: Apache-2.0
// SVG builders for one Maus layer at a time. Every layer uses the same
// viewBox (the cursor body's tight bounds in face-box units), so layers
// rendered at the same height line up pixel for pixel with the body.

export type Point = [number, number]
export interface Expression {
  id: number
  eyes: Point[][]
  mouth: number[]            // [halfWidth, curve, gap, skew]
  mouth_frame: { x: number; y: number; angle: number }
}
export interface Face {
  face_box: number
  face_centre: [number, number]
  mouth_stroke: number
  expressions: Expression[]
}
export interface Body {
  fit: { tx: number; ty: number; scale: number }
  path: string
  anchor: { x: number; y: number; scale: number }
  bounds: { left: number; top: number; right: number; bottom: number }
}
export interface Palette {
  body_gradient: [string, number][]
  face: string
  speak_interior: string
  eye_format: 'A8' | 'RGB565A8'
}

const f = (v: number): string => (Math.round(v * 1000) / 1000).toString()

function wrap(body: Body, inner: string, defs = ''): string {
  const b = body.bounds
  return `<svg xmlns="http://www.w3.org/2000/svg" viewBox="${f(b.left)} ${f(b.top)} ${f(b.right - b.left)} ${f(b.bottom - b.top)}">` +
    `${defs ? `<defs>${defs}</defs>` : ''}${inner}</svg>`
}

function faceGroup(face: Face, body: Body, inner: string): string {
  const a = body.anchor
  const c = face.face_centre
  return `<g transform="translate(${f(a.x)} ${f(a.y)}) scale(${f(a.scale)}) translate(${f(-c[0])} ${f(-c[1])})">${inner}</g>`
}

/** The body alone, filled with the 3-stop gradient (corner to corner, top-right to bottom-left). */
export function bodySvg(body: Body, palette: Palette): string {
  const stops = palette.body_gradient.map(([color, at]) => `<stop offset="${f(at)}" stop-color="${color}"/>`).join('')
  const defs = `<linearGradient id="g" x1="1" y1="0" x2="0" y2="1">${stops}</linearGradient>`
  const t = body.fit
  return wrap(body, `<g transform="translate(${f(t.tx)} ${f(t.ty)}) scale(${f(t.scale)})"><path fill="url(#g)" d="${body.path}"/></g>`, defs)
}

/** Both eyes of one expression, squashed toward each eye's centre line by `open` (1 = open). */
export function eyesSvg(face: Face, body: Body, e: Expression, open: number, color: string): string {
  const paths = e.eyes.map(ring => {
    const cy = ring.reduce((s, p) => s + p[1], 0) / ring.length
    const d = ring.map((p, i) => `${i === 0 ? 'M' : 'L'}${f(p[0])} ${f(cy + (p[1] - cy) * open)}`).join(' ') + ' Z'
    return `<path fill="${color}" d="${d}"/>`
  })
  return wrap(body, faceGroup(face, body, paths.join('')))
}

function mouthPoint(e: Expression, lx: number, ly: number): string {
  const fr = e.mouth_frame
  const ca = Math.cos(fr.angle)
  const sa = Math.sin(fr.angle)
  return `${f(fr.x + lx * ca - ly * sa)} ${f(fr.y + lx * sa + ly * ca)}`
}

/** The expression's own closed mouth: one quadratic stroke with round caps. */
export function mouthSvg(face: Face, body: Body, e: Expression, color: string): string {
  const [hw, curve] = e.mouth
  const d = `M${mouthPoint(e, -hw, 0)} Q${mouthPoint(e, 0, curve)} ${mouthPoint(e, hw, 0)}`
  return wrap(body, faceGroup(face, body,
    `<path d="${d}" fill="none" stroke="${color}" stroke-width="${f(face.mouth_stroke)}" stroke-linecap="round"/>`))
}

/**
 * Gadget-only speaking mouth (original to this SDK): a closed lens between an
 * upper and a lower quadratic, `open` face units deep, widened by `widen` per level.
 */
export function speakMouthSvg(face: Face, body: Body, e: Expression, open: number, level: number, widen: number,
  stroke: string, interior: string): string {
  const hw = e.mouth[0] + widen * level
  const curve = e.mouth[1]
  const upper = Math.min(curve, 0) * 0.5 - open * 0.15
  const lower = curve + open
  const d = `M${mouthPoint(e, -hw, 0)} Q${mouthPoint(e, 0, upper)} ${mouthPoint(e, hw, 0)} ` +
    `Q${mouthPoint(e, 0, lower)} ${mouthPoint(e, -hw, 0)} Z`
  return wrap(body, faceGroup(face, body,
    `<path d="${d}" fill="${interior}" stroke="${stroke}" stroke-width="${f(face.mouth_stroke)}" stroke-linejoin="round"/>`))
}

/** Body plus face in one picture, for review previews only. */
export function previewSvg(face: Face, body: Body, palette: Palette, e: Expression): string {
  const bodyInner = bodySvg(body, palette).replace(/^<svg[^>]*>/, '').replace(/<\/svg>$/, '')
  const eyes = eyesSvg(face, body, e, 1, palette.face).replace(/^<svg[^>]*>/, '').replace(/<\/svg>$/, '')
  const mouth = mouthSvg(face, body, e, palette.face).replace(/^<svg[^>]*>/, '').replace(/<\/svg>$/, '')
  return wrap(body, bodyInner + eyes + mouth)
}
```

- [ ] **Step 5: Write `tools/art/states.ts`**

```ts
// SPDX-License-Identifier: Apache-2.0
// states.json -> maus_state_def_t rows (pure; no I/O).
//
// Motion is translation only in v1 (spec §5.5): bob, jitter and circle are
// kept; the app's breathing pulse becomes a vertical bob of
// pulse * face_box / 2 face units; sway, tilt, squash, enter and settle are
// dropped. Amplitudes are emitted as integers in 0.1 px (contract D17).

export type Pair = [number, number]
export interface StateSource {
  pool: number[]
  cadence: Pair
  blink: Pair | null
  motion: Record<string, number | Pair>
  source: string
}
export interface StatesFile {
  states: Record<string, StateSource>
  speak: { expressions: number[]; open: number[]; widen: number }
}
export interface StateDef {
  key: string
  enumName: string
  pool: number[]
  cad: Pair
  blink: Pair
  bob: Pair
  jitter: Pair
  circle: Pair
}

/** ui_maus_state_t order, without UI_MAUS_NONE. */
export const STATE_ORDER = ['idle', 'listening', 'thinking', 'working', 'speaking', 'sleeping', 'curious', 'notifying', 'alerting'] as const

export function unionOfPools(file: StatesFile): number[] {
  const set = new Set<number>()
  for (const key of STATE_ORDER) {
    const s = file.states[key]
    if (!s) throw new Error(`states.json: missing state ${key}`)
    for (const e of s.pool) set.add(e)
  }
  for (const e of file.speak.expressions) {
    if (!set.has(e)) throw new Error(`states.json: speaking expression ${e} is in no pool`)
  }
  return [...set].sort((a, b) => a - b)
}

function pair(v: number | Pair | undefined, what: string): Pair | undefined {
  if (v === undefined) return undefined
  if (!Array.isArray(v) || v.length !== 2) throw new Error(`${what}: expected [amount, period_ms]`)
  return v
}

/** Face units -> 0.1 px at this profile's height. */
export function toTenthsPx(faceUnits: number, profileHeight: number, bodyHeightFu: number): number {
  return Math.round(faceUnits * profileHeight / bodyHeightFu * 10)
}

export function stateDefs(file: StatesFile, faceBox: number, profileHeight: number, bodyHeightFu: number): StateDef[] {
  return STATE_ORDER.map(key => {
    const s = file.states[key]
    const what = `states.json ${key}`
    const bob = pair(s.motion.bob, `${what} bob`)
    const pulse = pair(s.motion.pulse, `${what} pulse`)
    const jitter = pair(s.motion.jitter, `${what} jitter`)
    const circle = pair(s.motion.circle, `${what} circle`)
    let bobFu = 0
    let bobMs = 0
    if (bob) { bobFu = bob[0]; bobMs = bob[1] }
    if (pulse) {
      if (bob && bob[1] !== pulse[1]) throw new Error(`${what}: bob and pulse periods differ`)
      bobFu += pulse[0] * faceBox / 2
      bobMs = pulse[1]
    }
    const px = (fu: number): number => toTenthsPx(fu, profileHeight, bodyHeightFu)
    return {
      key,
      enumName: `UI_MAUS_${key.toUpperCase()}`,
      pool: s.pool,
      cad: s.cadence,
      blink: s.blink ?? [0, 0],
      bob: [px(bobFu), bobMs],
      jitter: jitter ? [px(jitter[0]), jitter[1]] : [0, 0],
      circle: circle ? [px(circle[0]), circle[1]] : [0, 0],
    }
  })
}
```

- [ ] **Step 6: Run the tests and watch them pass**

Run: `cd tools/art && npm test; cd ../..`
Expected: `# pass 19`, `# fail 0`.

- [ ] **Step 7: Write `tools/art/maus_art.h.in` (the contract header, verbatim) and check it**

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

```bash
node -e "
const fs=require('fs');
const md=fs.readFileSync('docs/plans/00-interfaces.md','utf8');
const m=/\`\`\`c\n(\/\* firmware\/ui\/art\/maus_art\.h[\s\S]*?)\`\`\`/.exec(md);
console.log(m && m[1]===fs.readFileSync('tools/art/maus_art.h.in','utf8') ? 'maus_art.h.in matches the contract' : 'MISMATCH')"
```

Expected: `maus_art.h.in matches the contract`.

- [ ] **Step 8: Write `tools/art/build.ts`**

Each layer is rendered on the full body canvas, checked to lie where the body is fully opaque (no clip mask is used: the app's solver guarantees the face fits at gaze 0), then trimmed to its alpha box; the trim offset is the layer's `x, y` relative to the body's top-left. Before any of that, `checkBodyBounds` renders the body outline from `shared/mascot-bodies.ts` at 4 px per face unit and checks it against the bounds from the Android twin `MausBodies.kt`, which set the shared viewBox: at the pinned commit the outline reaches about 18.75, 0, 210, 228.75 against 18.7298, 0, 209.8112, 228.541, well inside the 0.5-unit tolerance (checked: moving the Kotlin `right` by 2 units stops the build with that message).

```ts
// SPDX-License-Identifier: Apache-2.0
// npm run art: source/*.json -> firmware/ui/art/{maus_art.h, maus_art.c, s240/*.c, s150/*.c}
// plus review previews in tools/art/out/<profile>/ (gitignored).
import { mkdirSync, readFileSync, rmSync, writeFileSync } from 'node:fs'
import { createRequire } from 'node:module'
import { dirname, join } from 'node:path'
import { fileURLToPath } from 'node:url'
import { C_HEADER, cImage, toLvImage, trim, type ColorFormat, type LvImage, type Rgba } from './lvgl.ts'
import { bodySvg, eyesSvg, mouthSvg, previewSvg, speakMouthSvg, type Body, type Face, type Palette } from './svg.ts'
import { stateDefs, unionOfPools, type StatesFile } from './states.ts'

const require = createRequire(import.meta.url)
const { Resvg } = require('@resvg/resvg-js') as { Resvg: new (svg: string, opts: object) => { render(): { asPng(): Buffer } } }
const { PNG } = require('pngjs') as { PNG: { sync: { read(b: Buffer): { width: number; height: number; data: Buffer } } } }

const HERE = dirname(fileURLToPath(import.meta.url))
const ART = join(HERE, '..', '..', 'firmware', 'ui', 'art')
const OUT = join(HERE, 'out')
const read = (name: string): unknown => JSON.parse(readFileSync(join(HERE, 'source', name), 'utf8'))

const face = read('maus-face.json') as Face
const body = read('maus-body-cursor.json') as Body
const states = read('states.json') as StatesFile
const palette = read('palette.json') as Palette

export const PROFILES = [{ name: 's240', height: 240 }, { name: 's150', height: 150 }] as const
const BLINK = [1.0, 0.6, 0.25, 0.04]
const EXPR_COUNT = 18

function render(svg: string, height: number): Rgba {
  const png = new Resvg(svg, { fitTo: { mode: 'height', value: height }, background: 'rgba(0,0,0,0)' }).render().asPng()
  const decoded = PNG.sync.read(png)
  return { width: decoded.width, height: decoded.height, data: new Uint8Array(decoded.data) }
}

/** Every visible face pixel must sit where the body is fully opaque (no clip mask is used). */
function assertInsideBody(layer: Rgba, bodyImg: Rgba, what: string): void {
  for (let i = 0; i < layer.width * layer.height; i++) {
    if (layer.data[i * 4 + 3] !== 0 && bodyImg.data[i * 4 + 3] !== 255) {
      throw new Error(`${what}: pixel ${i % layer.width},${Math.floor(i / layer.width)} lies outside the body`)
    }
  }
}

/**
 * The body's tight bounds (the shared viewBox) come from the Android twin,
 * MausBodies.kt, and the outline from shared/mascot-bodies.ts. Render the
 * outline at 4 px per face unit on a generous canvas and check that the two
 * agree within 0.5 face units, so a drifting copy can never clip or shift
 * the body silently.
 */
function checkBodyBounds(b: Body): void {
  const [x0, y0, size, perUnit] = [-64, -64, 384, 4]
  const t = b.fit
  const svg = `<svg xmlns="http://www.w3.org/2000/svg" viewBox="${x0} ${y0} ${size} ${size}">` +
    `<g transform="translate(${t.tx} ${t.ty}) scale(${t.scale})"><path fill="#000" d="${b.path}"/></g></svg>`
  const box = trim(render(svg, size * perUnit))
  const drawn = {
    left: x0 + box.x / perUnit,
    top: y0 + box.y / perUnit,
    right: x0 + (box.x + box.image.width) / perUnit,
    bottom: y0 + (box.y + box.image.height) / perUnit,
  }
  for (const side of ['left', 'top', 'right', 'bottom'] as const) {
    if (Math.abs(drawn[side] - b.bounds[side]) > 0.5) {
      throw new Error(`MausBodies.kt bounds disagree with shared/mascot-bodies.ts: ${side} is ${b.bounds[side]}, ` +
        `the path reaches ${drawn[side]}`)
    }
  }
}

interface Layer { img: LvImage; x: number; y: number }

function layer(name: string, svg: string, height: number, bodyImg: Rgba, cf: ColorFormat): Layer {
  const full = render(svg, height)
  assertInsideBody(full, bodyImg, name)
  const t = trim(full)
  return { img: toLvImage(name, t.image, cf), x: t.x, y: t.y }
}

function declare(images: LvImage[]): string {
  return images.map(i => `extern const lv_image_dsc_t ${i.name};`).join('\n') + '\n'
}

function buildProfile(name: string, height: number, exprs: number[]): number {
  const dir = join(ART, name)
  rmSync(dir, { recursive: true, force: true })
  mkdirSync(dir, { recursive: true })
  const preview = join(OUT, name)
  mkdirSync(preview, { recursive: true })

  const bodyRgba = render(bodySvg(body, palette), height)
  const bodyImg = toLvImage(`maus_${name}_body`, bodyRgba, 'RGB565A8')
  const eyeCf: ColorFormat = palette.eye_format
  const eyes = exprs.map(e => BLINK.map((open, step) =>
    layer(`maus_${name}_eye_${e}_${step}`, eyesSvg(face, body, face.expressions[e], open, palette.face), height, bodyRgba, eyeCf)))
  const mouths = exprs.map(e =>
    layer(`maus_${name}_mouth_${e}`, mouthSvg(face, body, face.expressions[e], palette.face), height, bodyRgba, 'RGB565A8'))
  const speak = states.speak.expressions.map(e => states.speak.open.map((open, i) =>
    layer(`maus_${name}_speak_${e}_${i + 1}`,
      speakMouthSvg(face, body, face.expressions[e], open, i + 1, states.speak.widen, palette.face, palette.speak_interior),
      height, bodyRgba, 'RGB565A8')))

  const all: LvImage[] = [bodyImg, ...eyes.flat().map(l => l.img), ...mouths.map(l => l.img), ...speak.flat().map(l => l.img)]
  const total = all.reduce((s, i) => s + i.data.length, 0)
  const include = '#include "lvgl.h"\n\n'
  writeFileSync(join(dir, 'body.c'), C_HEADER + include + cImage(bodyImg))
  writeFileSync(join(dir, 'eyes.c'), C_HEADER + include + eyes.flat().map(l => cImage(l.img)).join('\n'))
  writeFileSync(join(dir, 'mouths.c'), C_HEADER + include + mouths.map(l => cImage(l.img)).join('\n'))
  writeFileSync(join(dir, 'speak.c'), C_HEADER + include + speak.flat().map(l => cImage(l.img)).join('\n'))

  const idx = (e: number): number => {
    const i = exprs.indexOf(e)
    if (i < 0) throw new Error(`expression ${e} is not in the baked union`)
    return i
  }
  const lay = (l: Layer): string => `{&${l.img.name}, ${l.x}, ${l.y}}`
  const defs = stateDefs(states, face.face_box, height, body.bounds.bottom - body.bounds.top)
  const pools = defs.map(d => `static const uint8_t pool_${d.key}[] = {${d.pool.map(idx).join(', ')}};`).join('\n')
  const stateRows = defs.map(d =>
    `  [${d.enumName}] = {pool_${d.key}, ${d.pool.length}, ${d.cad[0]}, ${d.cad[1]}, ${d.blink[0]}, ${d.blink[1]}, ` +
    `${d.bob[0]}, ${d.bob[1]}, ${d.jitter[0]}, ${d.jitter[1]}, ${d.circle[0]}, ${d.circle[1]}},`).join('\n')
  const tables = C_HEADER + '#include "maus_art.h"\n\n' + declare(all) + '\n' +
    `static const uint8_t expr_ids[MAUS_EXPR_COUNT] = {${exprs.join(', ')}};\n\n` +
    `static const maus_layer_t eyes[MAUS_EXPR_COUNT][MAUS_BLINK_STEPS] = {\n` +
    eyes.map(row => `  {${row.map(lay).join(', ')}},`).join('\n') + '\n};\n\n' +
    `static const maus_layer_t mouth[MAUS_EXPR_COUNT] = {\n` + mouths.map(l => `  ${lay(l)},`).join('\n') + '\n};\n\n' +
    `static const uint8_t speak_expr[MAUS_SPEAK_EXPRS] = {${states.speak.expressions.map(idx).join(', ')}};\n\n` +
    `static const maus_layer_t speak[MAUS_SPEAK_EXPRS][MAUS_SPEAK_LEVELS] = {\n` +
    speak.map(row => `  {${row.map(lay).join(', ')}},`).join('\n') + '\n};\n\n' +
    pools + '\n\n' +
    `/* pool, pool_len, cadence ms, blink ms (0 = never), bob/jitter/circle: amplitude in 0.1 px, period ms */\n` +
    `static const maus_state_def_t states[UI_MAUS__COUNT] = {\n${stateRows}\n};\n\n` +
    `const maus_art_t maus_art_${name} = {\n` +
    `  .profile = "${name}",\n  .w = ${bodyImg.w},\n  .h = ${bodyImg.h},\n  .body = &${bodyImg.name},\n` +
    `  .expr_ids = expr_ids,\n  .eyes = eyes,\n  .mouth = mouth,\n  .speak_expr = speak_expr,\n  .speak = speak,\n` +
    `  .states = states,\n  .total_bytes = ${total},\n};\n`
  writeFileSync(join(dir, 'tables.c'), tables)

  writeFileSync(join(preview, 'body.png'), new Resvg(bodySvg(body, palette), { fitTo: { mode: 'height', value: height } }).render().asPng())
  for (const e of exprs) {
    writeFileSync(join(preview, `expr_${e}.png`),
      new Resvg(previewSvg(face, body, palette, face.expressions[e]), { fitTo: { mode: 'height', value: height } }).render().asPng())
  }
  console.log(`${name}: body ${bodyImg.w}x${bodyImg.h}, ${all.length} images, ${total} bytes`)
  return total
}

checkBodyBounds(body)
const exprs = unionOfPools(states)
if (exprs.length !== EXPR_COUNT) throw new Error(`state pools cover ${exprs.length} expressions; maus_art.h says ${EXPR_COUNT}`)
mkdirSync(ART, { recursive: true })
writeFileSync(join(ART, 'maus_art.h'), readFileSync(join(HERE, 'maus_art.h.in'), 'utf8'))
writeFileSync(join(ART, 'maus_art.c'), C_HEADER + `#include "maus_art.h"

/* MAUS_ART_HAS_S240 / MAUS_ART_HAS_S150 come from firmware/ui/CMakeLists.txt:
 * the desktop build links both profiles, an ESP32 build only its board's. */
const maus_art_t *maus_art_for(gadget_art_profile_t profile) {
  switch (profile) {
#if defined(MAUS_ART_HAS_S240)
  case GADGET_ART_S240:
    return &maus_art_s240;
#endif
#if defined(MAUS_ART_HAS_S150)
  case GADGET_ART_S150:
    return &maus_art_s150;
#endif
  default:
    return NULL;
  }
}
`)
for (const p of PROFILES) buildProfile(p.name, p.height, exprs)
```

- [ ] **Step 9: Generate the art and look at it**

```bash
cd tools/art && npm run art && npm run test && cd ../..
ls firmware/ui/art firmware/ui/art/s240 firmware/ui/art/s150
```

Expected:

```text
s240: body 201x240, 97 images, 356786 bytes
s150: body 125x150, 97 images, 140260 bytes
```

(byte counts measured on darwin-arm64 with resvg 2.6.2; linux-x64 may differ by a few bytes, see Task 9), and `maus_art.c maus_art.h s150 s240`, each profile holding `body.c eyes.c mouths.c speak.c tables.c`. Open `tools/art/out/s240/expr_6.png`, `expr_19.png`, `expr_22.png` and `tools/art/out/s150/expr_6.png`: a green cursor-shaped Maus with white eyes and mouth, the face inside the body, no clipped strokes.

- [ ] **Step 10: The art is repeatable and keeps LF endings**

```bash
find firmware/ui/art -type f -exec shasum {} \; | sort > /tmp/art-1.txt
(cd tools/art && npm run art > /dev/null)
find firmware/ui/art -type f -exec shasum {} \; | sort > /tmp/art-2.txt
diff /tmp/art-1.txt /tmp/art-2.txt && echo same
printf '%s\n' 'firmware/ui/art/** text eol=lf' 'firmware/ui/fonts/** text eol=lf' >> .gitattributes
git check-attr text eol -- firmware/ui/art/maus_art.h
```

Expected: `same`, then `firmware/ui/art/maus_art.h: text: set` and `firmware/ui/art/maus_art.h: eol: lf`.

- [ ] **Step 11: Commit**

```bash
git add tools/art firmware/ui/art .gitattributes
git commit -m "feat(art): layered Maus art for the s240 and s150 profiles" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 3: LVGL 9.6.0 on the desktop, Latin-1 fonts and the art budget

**Files:**
- Create: `firmware/ui/lv_conf.h`, `firmware/ui/ui_lv_requirements.h`, `firmware/ui/ui_lv_compat.h`, `firmware/ui/CMakeLists.txt`
- Create: `tools/art/fonts.ts`, `tools/art/budget.ts`, `tools/art/test/budget.test.ts`
- Create (generated by `npm run fonts`, committed): `firmware/ui/fonts/font_latin1_{14,16,20,24,28,40}.c`, `firmware/ui/fonts/ui_fonts.h`
- Modify: `firmware/cmake/deps.cmake` (append the P2b block), `firmware/CMakeLists.txt` (`add_subdirectory(ui)`; the `ON` default waits for Task 6), `firmware/tests/CMakeLists.txt` (append)
- Test: `firmware/tests/test_ui_lv_compat.c`, `firmware/tests/test_ui_art.c`

**Interfaces:**
- Consumes: Task 2's art; P2a's `gadget_core`, `unity::framework`; contract §2.18 options.
- Produces: CMake targets `lvgl` / `lvgl::lvgl` (9.6.0) and `gadget_ui` (static, `PUBLIC` includes `firmware/ui`, `firmware/ui/art`, `firmware/ui/fonts`; links `gadget_core`, `lvgl::lvgl`); options `GADGET_WITH_SDL` (default ON with LVGL), `GADGET_SNAPSHOT_UPDATE` (default OFF; defines `LV_TEST_SCREENSHOT_CREATE_REFERENCE_IMAGE=1` PUBLIC on `lvgl`); `lvgl` gets `GADGET_LV_NO_SDL=1` when SDL is off. `ui_set_hidden(obj, bool)`, `ui_set_clickable(obj, bool)`, `ui_set_scrollable(obj, bool)`, `ui_is_hidden(obj)`, `ui_box(parent)` (`ui_lv_compat.h`). `const lv_font_t font_latin1_14 … font_latin1_40` declared in `fonts/ui_fonts.h`. CMake function `gadget_ui_test(<area> [libs…])` → executable `test_ui_<area>`, CTest `ui.<area>`, label `unit`. Node: `BUDGETS`, `totalBytes(tablesC)`, `overBudget(profile, eyeFormat, total)` (budget.ts).

- [ ] **Step 1: Write the failing C tests and register them**

`firmware/tests/test_ui_lv_compat.c`:

```c
/* SPDX-License-Identifier: Apache-2.0 */
/* LVGL 9.6.0 builds with firmware/ui/lv_conf.h, meets ui_lv_requirements.h,
 * and the flag wrappers in ui_lv_compat.h do what they say. */
#include "ui_lv_compat.h"
#include "ui_lv_requirements.h"
#include "unity.h"

static lv_display_t *disp;

void setUp(void) {
  lv_init();
  disp = lv_test_display_create(240, 240);
  lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);
}
void tearDown(void) { lv_deinit(); }

static void test_pinned_lvgl_version(void) {
  TEST_ASSERT_EQUAL_INT(9, LVGL_VERSION_MAJOR);
  TEST_ASSERT_EQUAL_INT(6, LVGL_VERSION_MINOR);
  TEST_ASSERT_EQUAL_INT(0, LVGL_VERSION_PATCH);
}

static void test_display_is_rgb565(void) {
  TEST_ASSERT_EQUAL_INT(LV_COLOR_FORMAT_RGB565, lv_display_get_color_format(disp));
}

static void test_hidden_wrapper(void) {
  lv_obj_t *o = ui_box(lv_screen_active());
  TEST_ASSERT_FALSE(ui_is_hidden(o));
  ui_set_hidden(o, true);
  TEST_ASSERT_TRUE(ui_is_hidden(o));
  ui_set_hidden(o, false);
  TEST_ASSERT_FALSE(ui_is_hidden(o));
}

static void test_box_is_inert(void) {
  lv_obj_t *o = ui_box(lv_screen_active());
  TEST_ASSERT_FALSE(lv_obj_is_clickable(o));
  TEST_ASSERT_FALSE(lv_obj_is_scrollable(o));
  ui_set_clickable(o, true);
  ui_set_scrollable(o, true);
  TEST_ASSERT_TRUE(lv_obj_is_clickable(o));
  TEST_ASSERT_TRUE(lv_obj_is_scrollable(o));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_pinned_lvgl_version);
  RUN_TEST(test_display_is_rgb565);
  RUN_TEST(test_hidden_wrapper);
  RUN_TEST(test_box_is_inert);
  return UNITY_END();
}
```

`firmware/tests/test_ui_art.c`:

```c
/* SPDX-License-Identifier: Apache-2.0 */
/* The generated art (tools/art) is internally consistent and within budget. */
#include <string.h>

#include "art/maus_art.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

static uint32_t layer_bytes(const maus_art_t *a, const maus_layer_t *l) {
  TEST_ASSERT_NOT_NULL(l->img);
  TEST_ASSERT_EQUAL_UINT32(LV_IMAGE_HEADER_MAGIC, l->img->header.magic);
  TEST_ASSERT_TRUE(l->x >= 0 && l->y >= 0);
  TEST_ASSERT_TRUE(l->x + (int)l->img->header.w <= a->w);
  TEST_ASSERT_TRUE(l->y + (int)l->img->header.h <= a->h);
  return l->img->data_size;
}

static void check_profile(const maus_art_t *a, const char *name, uint16_t w, uint16_t h, uint32_t budget,
                          uint32_t budget_fallback) {
  TEST_ASSERT_EQUAL_STRING(name, a->profile);
  TEST_ASSERT_EQUAL_UINT16(w, a->w);
  TEST_ASSERT_EQUAL_UINT16(h, a->h);
  TEST_ASSERT_EQUAL_UINT32(LV_COLOR_FORMAT_RGB565A8, a->body->header.cf);
  TEST_ASSERT_EQUAL_UINT32((uint32_t)w * h * 3u, a->body->data_size);
  uint32_t total = a->body->data_size;
  bool a8_eyes = a->eyes[0][0].img->header.cf == LV_COLOR_FORMAT_A8;
  for (int e = 0; e < MAUS_EXPR_COUNT; e++) {
    if (e > 0) TEST_ASSERT_TRUE(a->expr_ids[e] > a->expr_ids[e - 1]); /* sorted, unique */
    for (int s = 0; s < MAUS_BLINK_STEPS; s++) {
      const maus_layer_t *l = &a->eyes[e][s];
      TEST_ASSERT_EQUAL_UINT32(a8_eyes ? LV_COLOR_FORMAT_A8 : LV_COLOR_FORMAT_RGB565A8, l->img->header.cf);
      total += layer_bytes(a, l);
    }
    TEST_ASSERT_EQUAL_UINT32(LV_COLOR_FORMAT_RGB565A8, a->mouth[e].img->header.cf);
    total += layer_bytes(a, &a->mouth[e]);
  }
  for (int k = 0; k < MAUS_SPEAK_EXPRS; k++) {
    TEST_ASSERT_TRUE(a->speak_expr[k] < MAUS_EXPR_COUNT);
    for (int l = 0; l < MAUS_SPEAK_LEVELS; l++) total += layer_bytes(a, &a->speak[k][l]);
  }
  TEST_ASSERT_EQUAL_UINT32(total, a->total_bytes);
  TEST_ASSERT_TRUE_MESSAGE(a->total_bytes <= (a8_eyes ? budget : budget_fallback), "art over its byte budget");
  for (int s = UI_MAUS_IDLE; s < UI_MAUS__COUNT; s++) {
    const maus_state_def_t *d = &a->states[s];
    TEST_ASSERT_TRUE(d->pool_len >= 1);
    for (int i = 0; i < d->pool_len; i++) TEST_ASSERT_TRUE(d->pool[i] < MAUS_EXPR_COUNT);
    TEST_ASSERT_TRUE(d->cad_min_ms > 0 && d->cad_min_ms <= d->cad_max_ms);
    TEST_ASSERT_TRUE(d->blink_min_ms <= d->blink_max_ms);
  }
}

static void test_s240_profile(void) {
  check_profile(&maus_art_s240, "s240", 201, 240, 524288u, 819200u);
}

static void test_s150_profile(void) {
  check_profile(&maus_art_s150, "s150", 125, 150, 262144u, 327680u);
}

static void test_profiles_by_board_art_profile(void) {
  TEST_ASSERT_EQUAL_PTR(&maus_art_s240, maus_art_for(GADGET_ART_S240));
  TEST_ASSERT_EQUAL_PTR(&maus_art_s150, maus_art_for(GADGET_ART_S150));
  TEST_ASSERT_NULL(maus_art_for((gadget_art_profile_t)7));
}

static void test_state_pools_match_the_contract(void) {
  /* contract 2.15: app expression numbers per state */
  static const uint8_t idle[] = {6, 0, 8}, listening[] = {1, 10, 19}, thinking[] = {17, 8, 16, 14, 5},
                       working[] = {10, 7, 16, 11}, sleeping[] = {22, 13, 4}, curious[] = {21, 3, 0, 15},
                       notifying[] = {21, 3, 0}, alerting[] = {21, 3}, speaking[] = {19, 6};
  static const struct { ui_maus_state_t s; const uint8_t *ids; uint8_t n; } want[] = {
    {UI_MAUS_IDLE, idle, 3}, {UI_MAUS_LISTENING, listening, 3}, {UI_MAUS_THINKING, thinking, 5},
    {UI_MAUS_WORKING, working, 4}, {UI_MAUS_SLEEPING, sleeping, 3}, {UI_MAUS_CURIOUS, curious, 4},
    {UI_MAUS_NOTIFYING, notifying, 3}, {UI_MAUS_ALERTING, alerting, 2}, {UI_MAUS_SPEAKING, speaking, 2},
  };
  const maus_art_t *a = &maus_art_s240;
  for (size_t i = 0; i < sizeof want / sizeof want[0]; i++) {
    const maus_state_def_t *d = &a->states[want[i].s];
    TEST_ASSERT_EQUAL_UINT8(want[i].n, d->pool_len);
    for (uint8_t k = 0; k < want[i].n; k++) TEST_ASSERT_EQUAL_UINT8(want[i].ids[k], a->expr_ids[d->pool[k]]);
  }
  TEST_ASSERT_EQUAL_UINT8(19, a->expr_ids[a->speak_expr[0]]);
  TEST_ASSERT_EQUAL_UINT8(6, a->expr_ids[a->speak_expr[1]]);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_s240_profile);
  RUN_TEST(test_s150_profile);
  RUN_TEST(test_profiles_by_board_art_profile);
  RUN_TEST(test_state_pools_match_the_contract);
  return UNITY_END();
}
```

Append to `firmware/tests/CMakeLists.txt`:

```bash
cat >> firmware/tests/CMakeLists.txt <<'EOF'

# ---- P2b: firmware/ui tests ------------------------------------------------
if(GADGET_WITH_LVGL)
  # gadget_ui_test(<area> [libs...]): test_ui_<area>.c -> CTest ui.<area>, label unit.
  function(gadget_ui_test area)
    add_executable(test_ui_${area} test_ui_${area}.c)
    target_link_libraries(test_ui_${area} PRIVATE gadget_ui unity::framework ${ARGN})
    target_compile_options(test_ui_${area} PRIVATE -Wall -Wextra -Werror)
    add_test(NAME ui.${area} COMMAND test_ui_${area})
    set_tests_properties(ui.${area} PROPERTIES LABELS unit TIMEOUT 120)
  endfunction()
  gadget_ui_test(lv_compat)
  gadget_ui_test(art)
endif()
EOF
```

- [ ] **Step 2: Watch the build fail**

Run: `cmake -S firmware -B build/host -DCMAKE_BUILD_TYPE=Debug -DGADGET_WITH_LVGL=ON && cmake --build build/host -j10`
Expected: FAIL: `fatal error: 'ui_lv_compat.h' file not found` (or a link error for `gadget_ui`): nothing builds `firmware/ui` yet.

- [ ] **Step 3: Write the LVGL configuration and the two shared headers**

`firmware/ui/lv_conf.h` (simulator and tests only; the ESP32 uses `CONFIG_LV_*`):

```c
/* SPDX-License-Identifier: Apache-2.0 */
/* LVGL 9.6.0 configuration for the desktop simulator and host tests only.
 * The ESP32 port configures LVGL through CONFIG_LV_* in sdkconfig.defaults
 * (P2c); ui_lv_requirements.h checks that both provide what the UI needs. */
#ifndef LV_CONF_H
#define LV_CONF_H

#define LV_COLOR_FORMAT_DEFAULT LV_COLOR_FORMAT_RGB565
#define LV_USE_STDLIB_MALLOC LV_STDLIB_CLIB
#define LV_USE_STDLIB_STRING LV_STDLIB_CLIB
#define LV_USE_STDLIB_SPRINTF LV_STDLIB_CLIB
#define LV_USE_OS LV_OS_NONE
#define LV_DEF_REFR_PERIOD 33
#define LV_DRAW_SW_COMPLEX 1
#define LV_DRAW_SW_SUPPORT_RGB565A8 1
#define LV_DRAW_SW_SUPPORT_A8 1

#define LV_USE_LOG 1
#define LV_LOG_LEVEL LV_LOG_LEVEL_WARN
#define LV_LOG_PRINTF 1
#define LV_USE_ASSERT_NULL 1
#define LV_USE_ASSERT_MALLOC 1

/* The built-in ASCII font stays LVGL's default; the UI sets font_latin1_*
 * through styles on its root and widgets (never LV_USE_CUSTOM_FONT_DEFAULT). */
#define LV_FONT_MONTSERRAT_14 1

#define LV_USE_ANIMIMG 1

#if !defined(GADGET_LV_NO_SDL) /* set on lvgl when GADGET_WITH_SDL is OFF (headless-only build) */
#define LV_USE_SDL 1
#define LV_SDL_INCLUDE_PATH <SDL2/SDL.h>
#define LV_SDL_RENDER_MODE LV_DISPLAY_RENDER_MODE_DIRECT
#define LV_SDL_BUF_COUNT 1
#define LV_SDL_ACCELERATED 0 /* software renderer: SDL dummy video (CI) has no accelerated one */
#define LV_SDL_FULLSCREEN 0
#define LV_SDL_DIRECT_EXIT 0 /* window close and quit never exit() the simulator */
#endif

#define LV_USE_TEST 1
#define LV_USE_TEST_SCREENSHOT_COMPARE 1
#ifndef LV_TEST_SCREENSHOT_CREATE_REFERENCE_IMAGE
#define LV_TEST_SCREENSHOT_CREATE_REFERENCE_IMAGE 0 /* -D...=1 on lvgl in a GADGET_SNAPSHOT_UPDATE build */
#endif
#define LV_USE_LODEPNG 1
#define LV_USE_FS_STDIO 1
#define LV_FS_STDIO_LETTER 'A'
#define LV_FS_DEFAULT_DRIVER_LETTER 'A' /* lodepng's file I/O goes through lv_fs in 9.6 */

#define LV_BUILD_EXAMPLES 0
#define LV_BUILD_DEMOS 0

#endif /* LV_CONF_H */
```

`firmware/ui/ui_lv_requirements.h`:

```c
/* SPDX-License-Identifier: Apache-2.0 */
/* The LVGL options the UI needs, checked at compile time against whichever
 * configuration is active: firmware/ui/lv_conf.h (simulator, tests) or the
 * CONFIG_LV_* Kconfig values from sdkconfig.defaults (ESP32, P2c). */
#ifndef UI_LV_REQUIREMENTS_H
#define UI_LV_REQUIREMENTS_H

#include "lvgl.h"

#if LVGL_VERSION_MAJOR != 9 || LVGL_VERSION_MINOR < 5
#error "firmware/ui needs LVGL 9.5 or 9.6 (contract 1.4: 9.6.0)"
#endif
#if !LV_USE_ANIMIMG
#error "LVGL config: LV_USE_ANIMIMG must be 1"
#endif
#if !LV_DRAW_SW_SUPPORT_RGB565A8
#error "LVGL config: LV_DRAW_SW_SUPPORT_RGB565A8 must be 1 (Maus body and mouths)"
#endif
#if !LV_DRAW_SW_SUPPORT_A8
#error "LVGL config: LV_DRAW_SW_SUPPORT_A8 must be 1 (Maus eyes)"
#endif
#if LV_USE_OS != LV_OS_NONE
#error "LVGL config: LV_USE_OS must be LV_OS_NONE (core and UI are single-threaded)"
#endif
#if LV_COLOR_DEPTH != 16
#error "LVGL config: the display colour format must be RGB565 (16-bit)"
#endif
/* LVGL defaults the UI relies on (P2c keeps them on in sdkconfig.defaults) */
#if !LV_DRAW_SW_COMPLEX
#error "LVGL config: LV_DRAW_SW_COMPLEX must be 1 (arcs, rounded buttons, the round mask)"
#endif
#if !LV_USE_LABEL || !LV_USE_IMAGE || !LV_USE_ARC || !LV_USE_BAR
#error "LVGL config: the label, image, arc and bar widgets must be enabled"
#endif
/* The Image screen refills one of two lv_image_dsc_t slots per picture; LVGL's
 * caches are keyed by that pointer and would show a stale header or pixels. */
#if LV_CACHE_DEF_SIZE || LV_IMAGE_HEADER_CACHE_DEF_CNT
#error "LVGL config: LV_CACHE_DEF_SIZE and LV_IMAGE_HEADER_CACHE_DEF_CNT must be 0 (image caches off)"
#endif

#endif /* UI_LV_REQUIREMENTS_H */
```

`firmware/ui/ui_lv_compat.h`:

```c
/* SPDX-License-Identifier: Apache-2.0 */
/* Every object-flag change in firmware/ui goes through these wrappers.
 * LVGL 9.6 deprecates lv_obj_add_flag/remove_flag/has_flag (compile warning
 * plus a runtime log line per call) in favour of per-flag setters that 9.5
 * lacks, so the 9.5.0 fallback (contract 1.4) stays a version-pin change. */
#ifndef UI_LV_COMPAT_H
#define UI_LV_COMPAT_H

#include <stdbool.h>
#include "lvgl.h"

#if LVGL_VERSION_MAJOR > 9 || (LVGL_VERSION_MAJOR == 9 && LVGL_VERSION_MINOR >= 6)
static inline void ui_set_hidden(lv_obj_t *obj, bool hidden) { lv_obj_set_hidden(obj, hidden); }
static inline void ui_set_clickable(lv_obj_t *obj, bool on) { lv_obj_set_clickable(obj, on); }
static inline void ui_set_scrollable(lv_obj_t *obj, bool on) { lv_obj_set_scrollable(obj, on); }
static inline bool ui_is_hidden(const lv_obj_t *obj) { return lv_obj_is_hidden(obj); }
#else
static inline void ui_set_hidden(lv_obj_t *obj, bool hidden) {
  if (hidden) lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
  else lv_obj_remove_flag(obj, LV_OBJ_FLAG_HIDDEN);
}
static inline void ui_set_clickable(lv_obj_t *obj, bool on) {
  if (on) lv_obj_add_flag(obj, LV_OBJ_FLAG_CLICKABLE);
  else lv_obj_remove_flag(obj, LV_OBJ_FLAG_CLICKABLE);
}
static inline void ui_set_scrollable(lv_obj_t *obj, bool on) {
  if (on) lv_obj_add_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
  else lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
}
static inline bool ui_is_hidden(const lv_obj_t *obj) { return lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN); }
#endif

/* A plain, inert container: no theme style, no click, no scroll. */
static inline lv_obj_t *ui_box(lv_obj_t *parent) {
  lv_obj_t *o = lv_obj_create(parent);
  lv_obj_remove_style_all(o);
  ui_set_clickable(o, false);
  ui_set_scrollable(o, false);
  return o;
}

#endif /* UI_LV_COMPAT_H */
```

- [ ] **Step 4: Write `firmware/ui/CMakeLists.txt`**

This first version builds the art and fonts only (`UI_SRCS` grows in Tasks 4 and 5). The ESP-IDF branch is for P2c, which may correct it (contract §2.17). ESP-IDF v6.0.3 first runs every component's `CMakeLists.txt` in CMake script mode (`cmake -P tools/cmake/scripts/component_get_requirements.cmake`, `CMAKE_BUILD_EARLY_EXPANSION` set) to collect requirements, before any Kconfig value exists; in script mode `file(GLOB ... CONFIGURE_DEPENDS)` is an error and `CONFIG_GADGET_ART_PROFILE` is empty. So the file opens with the six-line early-expansion guard P2c's plan (04, Task 2 Step 6b) inserts, word for word, which turns that step into a no-op, and the art-profile check also skips early expansion. Checked with a script-mode stand-in for the requirements pass (it registers `REQUIRES lvgl__lvgl;core`) and a project-mode stand-in for the real pass (`s240` and `s150` each register only their own art and define `MAUS_ART_HAS_<PROFILE>`; an empty profile stops with the message).

```cmake
# SPDX-License-Identifier: Apache-2.0
# firmware/ui: LVGL screens, the Maus animation, generated art and fonts.
# Dual-use like core: an ESP-IDF component (built by P2c's project) or a
# desktop static library (GADGET_WITH_LVGL builds).
# ESP-IDF early expansion runs this file in script mode: no sdkconfig values and no
# file(GLOB CONFIGURE_DEPENDS). Report the requirements only.
if(ESP_PLATFORM AND CMAKE_BUILD_EARLY_EXPANSION)
  idf_component_register(INCLUDE_DIRS . art fonts REQUIRES lvgl__lvgl core)
  return()
endif()
set(UI_SRCS
  art/maus_art.c)
file(GLOB UI_FONT_SRCS CONFIGURE_DEPENDS ${CMAKE_CURRENT_LIST_DIR}/fonts/*.c)

if(ESP_PLATFORM)
  # One art profile per board (contract 2.15): CONFIG_GADGET_ART_PROFILE is s240 or s150.
  set(_profile "${CONFIG_GADGET_ART_PROFILE}")
  if(NOT CMAKE_BUILD_EARLY_EXPANSION AND NOT _profile MATCHES "^(s240|s150)$")
    message(FATAL_ERROR "CONFIG_GADGET_ART_PROFILE must be s240 or s150, not '${_profile}'")
  endif()
  file(GLOB UI_ART_SRCS CONFIGURE_DEPENDS ${CMAKE_CURRENT_LIST_DIR}/art/${_profile}/*.c)
  idf_component_register(SRCS ${UI_SRCS} ${UI_FONT_SRCS} ${UI_ART_SRCS}
                         INCLUDE_DIRS . art fonts
                         REQUIRES lvgl__lvgl core)
  string(TOUPPER "${_profile}" _profile_upper)
  target_compile_definitions(${COMPONENT_LIB} PRIVATE MAUS_ART_HAS_${_profile_upper}=1)
else()
  file(GLOB UI_ART_SRCS CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_LIST_DIR}/art/s240/*.c ${CMAKE_CURRENT_LIST_DIR}/art/s150/*.c)
  add_library(gadget_ui STATIC ${UI_SRCS} ${UI_FONT_SRCS} ${UI_ART_SRCS})
  target_include_directories(gadget_ui PUBLIC ${CMAKE_CURRENT_LIST_DIR} ${CMAKE_CURRENT_LIST_DIR}/art ${CMAKE_CURRENT_LIST_DIR}/fonts)
  target_link_libraries(gadget_ui PUBLIC gadget_core lvgl::lvgl)
  target_compile_definitions(gadget_ui PRIVATE MAUS_ART_HAS_S240=1 MAUS_ART_HAS_S150=1)
  set_target_properties(gadget_ui PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
  # Our sources (hand-written and tools/art output): the contract's warning set.
  # lv_font_conv output is third-party generated code: default warnings only.
  set_source_files_properties(${UI_SRCS} ${UI_ART_SRCS} PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra;-Wpedantic;-Werror")
  if(GADGET_SANITIZE)
    # PUBLIC: every test and gadget-sim linking gadget_ui runs sanitized too.
    # -fno-sanitize-recover as in cmake/warnings.cmake: a UBSan finding aborts
    # and fails its test instead of printing and passing.
    target_compile_options(gadget_ui PUBLIC -fsanitize=address,undefined
      -fno-sanitize-recover=undefined -fno-omit-frame-pointer)
    target_link_options(gadget_ui PUBLIC -fsanitize=address,undefined -fno-sanitize-recover=undefined)
  endif()
endif()
```

- [ ] **Step 5: Add LVGL and SDL to `deps.cmake` and the `ui` directory to the build**

```bash
cat >> firmware/cmake/deps.cmake <<'EOF'

# ---- P2b: LVGL 9.6.0 and SDL2 for firmware/ui and the simulator display ----
if(GADGET_WITH_LVGL)
  include(FetchContent)
  include(CMakeDependentOption)
  cmake_dependent_option(GADGET_WITH_SDL "SDL2 window mode for gadget-sim" ON "GADGET_WITH_LVGL" OFF)
  option(GADGET_SNAPSHOT_UPDATE "Rewrite every snapshot PNG instead of comparing it" OFF)
  set(LV_BUILD_CONF_PATH ${CMAKE_CURRENT_LIST_DIR}/../ui/lv_conf.h CACHE PATH "" FORCE)
  set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
  set(CONFIG_LV_USE_THORVG OFF CACHE BOOL "" FORCE)
  set(CONFIG_LV_USE_THORVG_INTERNAL OFF CACHE BOOL "" FORCE)
  set(CONFIG_LV_USE_SDL ${GADGET_WITH_SDL} CACHE BOOL "" FORCE)
  set(LV_FETCH_DEPENDENCIES OFF CACHE BOOL "" FORCE)
  set(LV_BUILD_INSTALL OFF CACHE BOOL "" FORCE)
  if(GADGET_WITH_SDL)
    find_package(SDL2 REQUIRED CONFIG)
  endif()
  FetchContent_Declare(lvgl
    URL https://github.com/lvgl/lvgl/archive/refs/tags/v9.6.0.tar.gz
    URL_HASH SHA256=b20ee3acc1bba13c62d854f9ebd62e4c51e0b443b1e0225892e86442defa84df
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
  FetchContent_MakeAvailable(lvgl)
  # Third-party code: none of our warning flags, no -Werror.
  set_property(TARGET lvgl PROPERTY COMPILE_OPTIONS "")
  target_compile_options(lvgl PRIVATE -w)
  if(NOT GADGET_WITH_SDL)
    target_compile_definitions(lvgl PUBLIC GADGET_LV_NO_SDL=1)
  endif()
  if(GADGET_SNAPSHOT_UPDATE)
    target_compile_definitions(lvgl PUBLIC LV_TEST_SCREENSHOT_CREATE_REFERENCE_IMAGE=1)
  endif()
  message(STATUS "gadget: LVGL 9.6.0 UI on (SDL window ${GADGET_WITH_SDL}, snapshot update ${GADGET_SNAPSHOT_UPDATE})")
endif()
EOF
```

Then in `firmware/CMakeLists.txt`: first remove P2a's placeholder guard if it is there (a three-line `if(GADGET_WITH_LVGL)` / `message(FATAL_ERROR "GADGET_WITH_LVGL needs firmware/ui, which plan P2b adds. ...")` / `endif()` before `include(cmake/deps.cmake)`; with it, every LVGL configure below stops with "Configuring incomplete, errors occurred!"; Contract deviations 4), then add the `ui` directory:

```bash
perl -0pi -e 's/if\(GADGET_WITH_LVGL\)\n  message\(FATAL_ERROR[^\n]*\nendif\(\)\n\n?//' firmware/CMakeLists.txt && ! grep -q 'FATAL_ERROR "GADGET_WITH_LVGL' firmware/CMakeLists.txt
grep -q 'add_subdirectory(ui)' firmware/CMakeLists.txt || perl -0pi -e 's/(add_subdirectory\(core\)\n)/$1if(GADGET_WITH_LVGL)\n  add_subdirectory(ui)\nendif()\n/' firmware/CMakeLists.txt
grep -n 'option(GADGET_WITH_LVGL\|FATAL_ERROR\|add_subdirectory' firmware/CMakeLists.txt
```

Expected: the first command exits 0 (it changes nothing when P2a ships no guard); no `FATAL_ERROR` line mentions `GADGET_WITH_LVGL`; `add_subdirectory(ui)` sits inside `if(GADGET_WITH_LVGL)` right after `add_subdirectory(core)`; the option still defaults to `OFF`. Task 6 flips it once the simulator can draw, so `gadget-sim` never loses its display in between; until then every LVGL build passes `-DGADGET_WITH_LVGL=ON` and builds only the UI test targets. If Task 1 Step 1 found the option **after** `include(cmake/deps.cmake)`, put the appended block in a new `firmware/cmake/lvgl.cmake` instead and add `include(cmake/lvgl.cmake)` directly below the option line.

- [ ] **Step 6: Configure (fetches LVGL, about 111 MB the first time)**

Run: `cmake -S firmware -B build/host -DCMAKE_BUILD_TYPE=Debug -DGADGET_WITH_LVGL=ON 2>&1 | grep 'gadget:'`
Expected: `-- gadget: LVGL 9.6.0 UI on (SDL window ON, snapshot update OFF)`. On macOS without `pkgconf` LVGL also prints a harmless `pkg-config not found` warning. If SDL2 is missing: `brew install sdl2` (macOS) or `sudo apt-get install -y libsdl2-dev` (Ubuntu).

- [ ] **Step 7: Write `tools/art/fonts.ts` and generate the fonts**

`tools/art/fonts.ts` takes LVGL 9.6.0's `Montserrat-Medium.ttf` from `--ttf`, else from a configured build tree (`build/host` or `build/snap`), else downloads that one file from LVGL's `v9.6.0` tag on GitHub (so contract §1.5's `npm run art && npm run fonts && npm run budget` also works on a fresh clone), checks its SHA-256 whichever way it came, and runs `lv_font_conv` from a temp folder with relative arguments so the `Opts:` line in every file is the same on every machine:

```ts
// SPDX-License-Identifier: Apache-2.0
// npm run fonts [-- --ttf <Montserrat-Medium.ttf>]
// Latin-1 (+ U+2026, U+2192) bitmap fonts for LVGL from Montserrat Medium (OFL-1.1),
// with the locked lv_font_conv 1.5.3. Writes firmware/ui/fonts/font_latin1_<px>.c
// and firmware/ui/fonts/ui_fonts.h. The TTF is --ttf, else the copy in a
// configured build tree (build/host or build/snap), else LVGL v9.6.0's own file
// downloaded from GitHub; whichever it is must have the pinned SHA-256.
import { execFileSync } from 'node:child_process'
import { createHash } from 'node:crypto'
import { existsSync, mkdirSync, mkdtempSync, readFileSync, rmSync, writeFileSync } from 'node:fs'
import { tmpdir } from 'node:os'
import { dirname, join, resolve } from 'node:path'
import { fileURLToPath } from 'node:url'

export const SIZES = [14, 16, 20, 24, 28, 40]
const RANGES = '0x20-0x7E,0xA0-0xFF,0x2026,0x2192'
// SHA-256 of scripts/generators/built_in_font/Montserrat-Medium.ttf in LVGL v9.6.0.
const TTF_SHA256 = '421f26b23e2be6b98373d32acd3cb2897b154d4bf0a77d26534ce476e4cbed53'
const TTF_URL = 'https://raw.githubusercontent.com/lvgl/lvgl/v9.6.0/scripts/generators/built_in_font/Montserrat-Medium.ttf'

const HERE = dirname(fileURLToPath(import.meta.url))
const ROOT = resolve(HERE, '..', '..')
const FONTS = join(ROOT, 'firmware', 'ui', 'fonts')
const CONV = join(HERE, 'node_modules', '.bin', 'lv_font_conv')
const CANDIDATES = ['build/host', 'build/snap'].map(b =>
  join(ROOT, b, '_deps', 'lvgl-src', 'scripts', 'generators', 'built_in_font', 'Montserrat-Medium.ttf'))

async function loadTtf(): Promise<{ from: string; bytes: Buffer }> {
  const i = process.argv.indexOf('--ttf')
  if (i >= 0) {
    const path = resolve(process.argv[i + 1])
    return { from: path, bytes: readFileSync(path) }
  }
  const found = CANDIDATES.find(p => existsSync(p))
  if (found) return { from: found, bytes: readFileSync(found) }
  console.log(`no configured build tree: downloading ${TTF_URL}`)
  try {
    const res = await fetch(TTF_URL)
    if (!res.ok) throw new Error(`HTTP ${res.status}`)
    return { from: TTF_URL, bytes: Buffer.from(await res.arrayBuffer()) }
  } catch (e) {
    console.error(`Montserrat-Medium.ttf: download failed (${(e as Error).message}). Configure the desktop build ` +
      'first (cmake -S firmware -B build/host), or pass --ttf <path to LVGL 9.6.0 Montserrat-Medium.ttf>.')
    process.exit(2)
  }
}

const ttf = await loadTtf()
const sha = createHash('sha256').update(ttf.bytes).digest('hex')
if (sha !== TTF_SHA256) {
  console.error(`${ttf.from}: SHA-256 ${sha} is not LVGL 9.6.0's Montserrat-Medium.ttf (${TTF_SHA256})`)
  process.exit(1)
}

// Relative arguments only, so the "Opts:" line lv_font_conv writes into each
// file is identical on every machine.
const work = mkdtempSync(join(tmpdir(), 'omb-fonts-'))
try {
  writeFileSync(join(work, 'Montserrat-Medium.ttf'), ttf.bytes)
  mkdirSync(FONTS, { recursive: true })
  for (const px of SIZES) {
    const out = `font_latin1_${px}.c`
    execFileSync(CONV, ['--font', 'Montserrat-Medium.ttf', '-r', RANGES, '--size', String(px), '--bpp', '4',
      '--format', 'lvgl', '--no-compress', '--lv-include', 'lvgl.h', '-o', out], { cwd: work, stdio: 'inherit' })
    const body = readFileSync(join(work, out), 'utf8')
    writeFileSync(join(FONTS, out),
      '/* SPDX-License-Identifier: OFL-1.1 */\n' +
      '/* GENERATED by tools/art (npm run fonts) from Montserrat Medium (SIL Open Font\n' +
      ' * License 1.1, shipped with LVGL 9.6.0) with lv_font_conv 1.5.3. Do not edit. */\n' + body)
    console.log(`wrote firmware/ui/fonts/${out}`)
  }
} finally {
  rmSync(work, { recursive: true, force: true })
}

writeFileSync(join(FONTS, 'ui_fonts.h'),
  '/* SPDX-License-Identifier: Apache-2.0 */\n' +
  '/* GENERATED by tools/art (npm run fonts). Do not edit. */\n' +
  '#ifndef UI_FONTS_H\n#define UI_FONTS_H\n\n#include "lvgl.h"\n\n' +
  SIZES.map(px => `LV_FONT_DECLARE(font_latin1_${px})`).join('\n') + '\n\n#endif /* UI_FONTS_H */\n')
console.log('wrote firmware/ui/fonts/ui_fonts.h')
```

```bash
cd tools/art && npm run fonts && cd ../..
grep -h 'Opts:' firmware/ui/fonts/font_latin1_24.c
grep -c 'U+2026\|U+2192' firmware/ui/fonts/font_latin1_24.c
```

Expected: six `wrote firmware/ui/fonts/font_latin1_<px>.c` lines and `wrote firmware/ui/fonts/ui_fonts.h` (without a configured build tree, first `no configured build tree: downloading https://raw.githubusercontent.com/lvgl/lvgl/v9.6.0/...`; the downloaded file has the pinned SHA-256 and gives byte-identical fonts, checked 2026-10-05); the Opts line is ` * Opts: --font Montserrat-Medium.ttf -r 0x20-0x7E,0xA0-0xFF,0x2026,0x2192 --size 24 --bpp 4 --format lvgl --no-compress --lv-include lvgl.h -o font_latin1_24.c` (no absolute path); the glyph count line prints `2` (both extra code points exist in Montserrat Medium; verified with lv_font_conv 1.5.3 on 2026-10-04).

- [ ] **Step 8: Write the budget check (test first)**

`tools/art/test/budget.test.ts`:

```ts
// SPDX-License-Identifier: Apache-2.0
import assert from 'node:assert/strict'
import { test } from 'node:test'
import { BUDGETS, overBudget, totalBytes } from '../budget.ts'

test('budgets are the contract numbers', () => {
  assert.deepEqual(BUDGETS, { s240: { A8: 524288, RGB565A8: 819200 }, s150: { A8: 262144, RGB565A8: 327680 } })
})

test('total bytes come from the generated maus_art_t', () => {
  assert.equal(totalBytes('const maus_art_t maus_art_s150 = {\n  .total_bytes = 140260,\n};\n'), 140260)
  assert.throws(() => totalBytes('nothing here'), /total_bytes/)
})

test('within, at and over the limit', () => {
  assert.equal(overBudget('s240', 'A8', 524288), null)
  assert.match(overBudget('s240', 'A8', 524289)!, /over the 524288-byte budget/)
  assert.equal(overBudget('s150', 'RGB565A8', 300000), null)
  assert.match(overBudget('s999', 'A8', 1)!, /unknown art profile/)
})
```

Run `cd tools/art && npm test; cd ../..` — expected FAIL (`budget.ts` missing). Then write `tools/art/budget.ts`:

```ts
// SPDX-License-Identifier: Apache-2.0
// npm run budget: every profile's total image bytes within its budget
// (spec 5.5, contract 2.15). Reads the committed firmware/ui/art/<profile>/tables.c.
import { readFileSync } from 'node:fs'
import { dirname, join, resolve } from 'node:path'
import { fileURLToPath } from 'node:url'

export const BUDGETS: Record<string, { A8: number; RGB565A8: number }> = {
  s240: { A8: 524288, RGB565A8: 819200 },
  s150: { A8: 262144, RGB565A8: 327680 },
}

export function totalBytes(tablesC: string): number {
  const m = /\.total_bytes = (\d+),/.exec(tablesC)
  if (!m) throw new Error('tables.c has no .total_bytes')
  return Number(m[1])
}

/** null when within budget, else the message to print. */
export function overBudget(profile: string, eyeFormat: 'A8' | 'RGB565A8', total: number): string | null {
  const limits = BUDGETS[profile]
  if (!limits) return `unknown art profile ${profile}`
  const limit = limits[eyeFormat]
  return total <= limit ? null : `${profile}: ${total} bytes of art is over the ${limit}-byte budget (${eyeFormat} eyes)`
}

if (process.argv[1] && resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  const here = dirname(fileURLToPath(import.meta.url))
  const palette = JSON.parse(readFileSync(join(here, 'source', 'palette.json'), 'utf8')) as { eye_format: 'A8' | 'RGB565A8' }
  let failed = false
  for (const profile of Object.keys(BUDGETS)) {
    const total = totalBytes(readFileSync(join(here, '..', '..', 'firmware', 'ui', 'art', profile, 'tables.c'), 'utf8'))
    const problem = overBudget(profile, palette.eye_format, total)
    if (problem) {
      console.error(problem)
      failed = true
    } else {
      console.log(`${profile}: ${total} bytes (budget ${BUDGETS[profile][palette.eye_format]})`)
    }
  }
  process.exit(failed ? 1 : 0)
}
```

```bash
cd tools/art && npm test && npm run budget && cd ../..
```

Expected: `# pass 22`, then `s240: 356786 bytes (budget 524288)` and `s150: 140260 bytes (budget 262144)`.

- [ ] **Step 9: Build and run the tests**

```bash
cmake -S firmware -B build/host -DCMAKE_BUILD_TYPE=Debug -DGADGET_WITH_LVGL=ON
cmake --build build/host -j10 --target test_ui_lv_compat test_ui_art
ctest --test-dir build/host --output-on-failure -R '^ui\.(lv_compat|art)$'
```

Expected: `ui.lv_compat` (4 tests) and `ui.art` (4 tests) pass.

- [ ] **Step 10: Commit**

```bash
git add firmware/ui firmware/cmake/deps.cmake firmware/CMakeLists.txt firmware/tests/CMakeLists.txt \
        firmware/tests/test_ui_lv_compat.c firmware/tests/test_ui_art.c tools/art
git commit -m "feat(ui): LVGL 9.6.0 build, Latin-1 fonts and the art budget" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 4: Screen copy and the Maus animation engine

**Files:**
- Create: `firmware/ui/ui_copy.h`, `firmware/ui/ui_copy.c`, `firmware/ui/ui_maus_engine.h`, `firmware/ui/ui_maus_engine.c`
- Modify: `firmware/ui/CMakeLists.txt` (the `set(UI_SRCS …)` lines), `firmware/tests/CMakeLists.txt` (append)
- Test: `firmware/tests/test_ui_copy.c`, `firmware/tests/test_ui_maus_engine.c`

**Interfaces:**
- Consumes: `ui_model_t` and enums (`gadget_ui_model.h`), `gadget_prng_t`, `gadget_prng_seed`, `gadget_prng_range` (`gadget_util.h`, P2a), `maus_art_t` / `maus_state_def_t` (Task 2).
- Produces:
  - `ui_copy.h`: `UI_ELLIPSIS`, `UI_ARROW`, `UI_COPY_MAX` (512), `UI_COPY_ASK_ELSEWHERE`, `ui_copy_t {char caption[512]; char host[UI_NAME_MAX]; char status[512];}` (`host`: the host name on its own line, spec §5.5: every Setup step but Pairing, whose caption names it, and every Offline reason but Unreachable, whose caption names it; `""` when there is none or the step/reason is out of range), `uint32_t ui_retry_seconds(uint64_t now_ms, uint64_t retry_at_ms)`, `void ui_copy_idle(const ui_model_t *, char *, size_t)`, `void ui_copy_setup(const ui_model_t *, ui_copy_t *)`, `void ui_copy_offline(const ui_model_t *, ui_copy_t *)`, `void ui_copy_update(const ui_model_t *, char *, size_t)`, `void ui_copy_countdown(const ui_model_t *, char *, size_t)`.
  - `ui_maus_engine.h`: `MAUS_BLINK_MS` 320, `MAUS_BLINK_CLOSE_MS` 134, `maus_frame_t {uint8_t expr, blink_step; int8_t speak; uint8_t speak_level; int16_t dx, dy;}`, `maus_engine_t`, `void maus_engine_init(maus_engine_t *, const maus_art_t *, uint32_t seed)`, `void maus_engine_set_state(maus_engine_t *, ui_maus_state_t, uint64_t now_ms)`, `maus_frame_t maus_engine_step(maus_engine_t *, uint64_t now_ms, uint8_t speak_level)`, `int32_t maus_sin_q15(uint32_t phase16)`, `uint8_t maus_blink_step(uint32_t t_ms)`.

- [ ] **Step 1: Write the failing tests**

`firmware/tests/test_ui_copy.c` (every string of contract §2.15, plus Review Focus 1 and 2):

```c
/* SPDX-License-Identifier: Apache-2.0 */
/* Exact screen copy (contract 2.15), without LVGL. */
#include <string.h>

#include "ui_copy.h"
#include "unity.h"

static ui_model_t m;
static ui_copy_t c;
static char line[UI_COPY_MAX];

void setUp(void) {
  memset(&m, 0, sizeof m);
  memset(&c, 0, sizeof c);
  line[0] = '\0';
}
void tearDown(void) {}

static void test_idle_greets_the_bot_or_just_hi(void) {
  ui_copy_idle(&m, line, sizeof line);
  TEST_ASSERT_EQUAL_STRING("Hi", line);
  strcpy(m.bot_name, "Jev");
  ui_copy_idle(&m, line, sizeof line);
  TEST_ASSERT_EQUAL_STRING("Hi, I'm Jev", line);
}

static void test_setup_copy_for_every_step(void) {
  strcpy(m.device_id, "gad_b18b86ce1389e46d");
  m.setup.step = UI_SETUP_NEED_WIFI;
  ui_copy_setup(&m, &c);
  TEST_ASSERT_EQUAL_STRING("Connect me to Wi-Fi with the installer.", c.caption);
  TEST_ASSERT_EQUAL_STRING("", c.host); /* no challenge yet */
  TEST_ASSERT_EQUAL_STRING("gad_b18b86ce1389e46d", c.status);

  m.setup.step = UI_SETUP_NEED_CODE;
  ui_copy_setup(&m, &c);
  TEST_ASSERT_EQUAL_STRING("Pair me: MausBot \xE2\x86\x92 Settings \xE2\x86\x92 Remote access \xE2\x86\x92 Pair a gadget\n"
                           "Enter the code in the installer.\n"
                           "Remote access must be on.", c.caption);

  m.setup.step = UI_SETUP_PAIRING;
  ui_copy_setup(&m, &c);
  TEST_ASSERT_EQUAL_STRING("Pairing\xE2\x80\xA6", c.caption);
  strcpy(m.host_name, "Omkar's computer");
  ui_copy_setup(&m, &c);
  TEST_ASSERT_EQUAL_STRING("Pairing with Omkar's computer\xE2\x80\xA6", c.caption);
  TEST_ASSERT_EQUAL_STRING("", c.host); /* already in the caption */

  m.setup.step = UI_SETUP_HOST_NOT_FOUND;
  ui_copy_setup(&m, &c);
  TEST_ASSERT_EQUAL_STRING("Can't find MausBot. Enter the address shown under Pair a gadget in the installer.", c.caption);

  m.setup.step = UI_SETUP_BAD_CODE;
  ui_copy_setup(&m, &c);
  TEST_ASSERT_EQUAL_STRING("That code didn't work. Get a new one from Pair a gadget.", c.caption);
  TEST_ASSERT_EQUAL_STRING("Omkar's computer", c.host); /* spec 5.5: once a challenge has arrived */
  TEST_ASSERT_EQUAL_STRING("gad_b18b86ce1389e46d", c.status);

  m.setup.step = UI_SETUP_DEVICE_LIMIT;
  ui_copy_setup(&m, &c);
  TEST_ASSERT_EQUAL_STRING("MausBot has too many devices. Remove one in Remote access. Retrying\xE2\x80\xA6", c.caption);
  TEST_ASSERT_EQUAL_STRING("Omkar's computer", c.host);
}

static void test_offline_copy_for_every_reason(void) {
  m.now_ms = 10000;
  m.offline.reason = UI_OFFLINE_WIFI_CONNECTING;
  ui_copy_offline(&m, &c);
  TEST_ASSERT_EQUAL_STRING("Connecting to Wi-Fi\xE2\x80\xA6", c.caption);
  TEST_ASSERT_EQUAL_STRING("", c.host); /* nothing stored yet */
  TEST_ASSERT_EQUAL_STRING("", c.status);

  m.offline.reason = UI_OFFLINE_WIFI_FAILED;
  strcpy(m.offline.ssid, "Home 5G");
  ui_copy_offline(&m, &c);
  TEST_ASSERT_EQUAL_STRING("Can't join Home 5G.", c.caption);

  m.offline.reason = UI_OFFLINE_HOST_LOOKUP;
  ui_copy_offline(&m, &c);
  TEST_ASSERT_EQUAL_STRING("Looking for MausBot\xE2\x80\xA6", c.caption);

  m.offline.reason = UI_OFFLINE_HOST_UNREACHABLE;
  strcpy(m.host_name, "Omkar's computer");
  m.offline.retry_at_ms = 16000;
  ui_copy_offline(&m, &c);
  TEST_ASSERT_EQUAL_STRING("Can't reach Omkar's computer.\nIs Remote access on in MausBot?\n"
                           "On Windows, set this network to Private.", c.caption);
  TEST_ASSERT_EQUAL_STRING("", c.host); /* already in the caption */
  TEST_ASSERT_EQUAL_STRING("Retrying in 6 s", c.status);

  m.offline.reason = UI_OFFLINE_IN_USE_ELSEWHERE;
  m.offline.retry_at_ms = 0;
  ui_copy_offline(&m, &c);
  TEST_ASSERT_EQUAL_STRING("In use elsewhere\nPress TALK to use it here.", c.caption);
  TEST_ASSERT_EQUAL_STRING("Omkar's computer", c.host); /* spec 5.5: the stored host_name */
  TEST_ASSERT_EQUAL_STRING("", c.status);

  m.offline.reason = UI_OFFLINE_PROTOCOL;
  m.offline.retry_at_ms = 12500;
  ui_copy_offline(&m, &c);
  TEST_ASSERT_EQUAL_STRING("MausBot didn't accept me.", c.caption);
  TEST_ASSERT_EQUAL_STRING("Omkar's computer", c.host);
  TEST_ASSERT_EQUAL_STRING("Retrying in 3 s", c.status);
  m.offline.reason = UI_OFFLINE_HOST_LOOKUP;
  ui_copy_offline(&m, &c);
  TEST_ASSERT_EQUAL_STRING("Omkar's computer", c.host);
}

static void test_offline_without_names_falls_back(void) {
  m.offline.reason = UI_OFFLINE_HOST_UNREACHABLE;
  ui_copy_offline(&m, &c);
  TEST_ASSERT_EQUAL_STRING("Can't reach MausBot.\nIs Remote access on in MausBot?\n"
                           "On Windows, set this network to Private.", c.caption);
  TEST_ASSERT_EQUAL_STRING("", c.status); /* retry_at_ms 0: no automatic retry */
  m.offline.reason = UI_OFFLINE_WIFI_FAILED;
  ui_copy_offline(&m, &c);
  TEST_ASSERT_EQUAL_STRING("Can't join Wi-Fi.", c.caption);
}

static void test_retry_seconds_round_up_and_never_show_zero(void) {
  TEST_ASSERT_EQUAL_UINT32(6, ui_retry_seconds(10000, 16000));
  TEST_ASSERT_EQUAL_UINT32(6, ui_retry_seconds(10001, 16000));
  TEST_ASSERT_EQUAL_UINT32(1, ui_retry_seconds(15999, 16000));
  TEST_ASSERT_EQUAL_UINT32(1, ui_retry_seconds(16000, 16000));
  TEST_ASSERT_EQUAL_UINT32(1, ui_retry_seconds(20000, 16000));
  TEST_ASSERT_EQUAL_UINT32(60, ui_retry_seconds(0, 60000));
}

static void test_update_copy_for_every_phase(void) {
  m.update.phase = UI_UPDATE_RECEIVING;
  m.update.pct = 42;
  ui_copy_update(&m, line, sizeof line);
  TEST_ASSERT_EQUAL_STRING("Updating\xE2\x80\xA6 42%", line);
  m.update.pct = 250; /* never more than 100 % */
  ui_copy_update(&m, line, sizeof line);
  TEST_ASSERT_EQUAL_STRING("Updating\xE2\x80\xA6 100%", line);
  m.update.phase = UI_UPDATE_VERIFYING;
  ui_copy_update(&m, line, sizeof line);
  TEST_ASSERT_EQUAL_STRING("Checking update\xE2\x80\xA6", line);
  m.update.phase = UI_UPDATE_RESTARTING;
  ui_copy_update(&m, line, sizeof line);
  TEST_ASSERT_EQUAL_STRING("Restarting\xE2\x80\xA6", line);
}

static void test_countdown_shows_5_to_1_only(void) {
  m.listening.countdown_s = 0;
  ui_copy_countdown(&m, line, sizeof line);
  TEST_ASSERT_EQUAL_STRING("", line);
  m.listening.countdown_s = 5;
  ui_copy_countdown(&m, line, sizeof line);
  TEST_ASSERT_EQUAL_STRING("5", line);
  m.listening.countdown_s = 1;
  ui_copy_countdown(&m, line, sizeof line);
  TEST_ASSERT_EQUAL_STRING("1", line);
  m.listening.countdown_s = 9;
  ui_copy_countdown(&m, line, sizeof line);
  TEST_ASSERT_EQUAL_STRING("", line);
}

static void test_long_names_never_overflow(void) {
  memset(m.bot_name, 'x', sizeof m.bot_name - 1);
  m.bot_name[sizeof m.bot_name - 1] = '\0';
  ui_copy_idle(&m, line, sizeof line);
  TEST_ASSERT_EQUAL_size_t(strlen("Hi, I'm ") + sizeof m.bot_name - 1, strlen(line));
  memset(m.host_name, 'y', sizeof m.host_name - 1);
  m.host_name[sizeof m.host_name - 1] = '\0';
  m.setup.step = UI_SETUP_PAIRING;
  ui_copy_setup(&m, &c);
  TEST_ASSERT_TRUE(strlen(c.caption) < sizeof c.caption);
  m.setup.step = UI_SETUP_BAD_CODE;
  ui_copy_setup(&m, &c);
  TEST_ASSERT_EQUAL_size_t(sizeof m.host_name - 1, strlen(c.host));
}

static void test_out_of_range_enums_give_defined_text(void) {
  memset(&c, 'x', sizeof c);
  m.offline.reason = (ui_offline_reason_t)42;
  strcpy(m.host_name, "Omkar's computer");
  ui_copy_offline(&m, &c);
  TEST_ASSERT_EQUAL_STRING("", c.caption);
  TEST_ASSERT_EQUAL_STRING("", c.host);
  TEST_ASSERT_EQUAL_STRING("", c.status);
  memset(&c, 'x', sizeof c);
  strcpy(m.device_id, "gad_b18b86ce1389e46d");
  m.setup.step = (ui_setup_step_t)42;
  ui_copy_setup(&m, &c);
  TEST_ASSERT_EQUAL_STRING("", c.caption);
  TEST_ASSERT_EQUAL_STRING("", c.host);
  TEST_ASSERT_EQUAL_STRING("gad_b18b86ce1389e46d", c.status);
  memset(line, 'x', sizeof line);
  m.update.phase = (ui_update_phase_t)42;
  ui_copy_update(&m, line, sizeof line);
  TEST_ASSERT_EQUAL_STRING("", line);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_idle_greets_the_bot_or_just_hi);
  RUN_TEST(test_setup_copy_for_every_step);
  RUN_TEST(test_offline_copy_for_every_reason);
  RUN_TEST(test_offline_without_names_falls_back);
  RUN_TEST(test_retry_seconds_round_up_and_never_show_zero);
  RUN_TEST(test_update_copy_for_every_phase);
  RUN_TEST(test_countdown_shows_5_to_1_only);
  RUN_TEST(test_long_names_never_overflow);
  RUN_TEST(test_out_of_range_enums_give_defined_text);
  return UNITY_END();
}
```

`firmware/tests/test_ui_maus_engine.c`:

```c
/* SPDX-License-Identifier: Apache-2.0 */
/* The Maus animation engine: blink-cut expression changes, deterministic
 * PRNG, integer motion, speaking mouths. Uses a hand-made art table. */
#include <string.h>

#include "ui_maus_engine.h"
#include "unity.h"

/* expr_ids index:  0  1  2  3 */
static const uint8_t k_ids[MAUS_EXPR_COUNT] = {6, 0, 8, 19};
static const uint8_t k_pool_idle[] = {0, 1, 2};
static const uint8_t k_pool_one[] = {2};
static const uint8_t k_pool_speak[] = {3, 0};
static const uint8_t k_speak_expr[MAUS_SPEAK_EXPRS] = {3, 0};
static maus_state_def_t k_states[UI_MAUS__COUNT];
static maus_art_t k_art;
static maus_engine_t e;

void setUp(void) {
  memset(k_states, 0, sizeof k_states);
  k_states[UI_MAUS_IDLE] = (maus_state_def_t){k_pool_idle, 3, 2000, 4000, 1000, 2000, 0, 0, 0, 0, 0, 0};
  k_states[UI_MAUS_SLEEPING] = (maus_state_def_t){k_pool_idle, 3, 2000, 4000, 0, 0, 0, 0, 0, 0, 0, 0};
  k_states[UI_MAUS_CURIOUS] = (maus_state_def_t){k_pool_one, 1, 1000, 1000, 0, 0, 100, 1000, 0, 0, 0, 0};
  k_states[UI_MAUS_ALERTING] = (maus_state_def_t){k_pool_one, 1, 1000, 1000, 0, 0, 0, 0, 27, 85, 0, 0};
  k_states[UI_MAUS_SPEAKING] = (maus_state_def_t){k_pool_speak, 2, 3000, 6000, 2500, 5000, 0, 0, 0, 0, 0, 0};
  memset(&k_art, 0, sizeof k_art);
  k_art.expr_ids = k_ids;
  k_art.speak_expr = k_speak_expr;
  k_art.states = k_states;
  maus_engine_init(&e, &k_art, 1);
}
void tearDown(void) {}

static void test_sine_table_hits_the_quarter_points(void) {
  TEST_ASSERT_EQUAL_INT32(0, maus_sin_q15(0));
  TEST_ASSERT_EQUAL_INT32(32767, maus_sin_q15(16384));
  TEST_ASSERT_EQUAL_INT32(0, maus_sin_q15(32768));
  TEST_ASSERT_EQUAL_INT32(-32767, maus_sin_q15(49152));
  TEST_ASSERT_INT32_WITHIN(40, 23170, maus_sin_q15(8192)); /* sin(pi/4) */
}

static void test_blink_closes_fast_and_opens_slowly(void) {
  TEST_ASSERT_EQUAL_UINT8(0, maus_blink_step(0));
  TEST_ASSERT_EQUAL_UINT8(1, maus_blink_step(40));
  TEST_ASSERT_EQUAL_UINT8(2, maus_blink_step(90));
  TEST_ASSERT_EQUAL_UINT8(3, maus_blink_step(134));
  TEST_ASSERT_EQUAL_UINT8(3, maus_blink_step(150));
  TEST_ASSERT_EQUAL_UINT8(2, maus_blink_step(200));
  TEST_ASSERT_EQUAL_UINT8(1, maus_blink_step(250));
  TEST_ASSERT_EQUAL_UINT8(0, maus_blink_step(319));
}

static void test_first_state_shows_its_first_expression_at_once(void) {
  maus_engine_set_state(&e, UI_MAUS_IDLE, 0);
  maus_frame_t f = maus_engine_step(&e, 0, 0);
  TEST_ASSERT_EQUAL_UINT8(0, f.expr);
  TEST_ASSERT_EQUAL_UINT8(0, f.blink_step);
}

static void test_expression_changes_only_while_the_eyes_are_closed(void) {
  maus_engine_set_state(&e, UI_MAUS_IDLE, 0);
  uint8_t last = maus_engine_step(&e, 0, 0).expr;
  int changes = 0;
  for (uint64_t t = 10; t <= 60000; t += 10) {
    maus_frame_t f = maus_engine_step(&e, t, 0);
    if (f.expr != last) {
      TEST_ASSERT_EQUAL_UINT8(MAUS_BLINK_STEPS - 1, f.blink_step);
      changes++;
      last = f.expr;
    }
  }
  TEST_ASSERT_GREATER_OR_EQUAL(10, changes); /* cadence 2-4 s over 60 s */
}

static void test_state_change_swaps_expression_under_a_blink(void) {
  maus_engine_set_state(&e, UI_MAUS_IDLE, 0);
  (void)maus_engine_step(&e, 0, 0);
  maus_engine_set_state(&e, UI_MAUS_CURIOUS, 500); /* pool {2}, current is 0 */
  maus_frame_t f = maus_engine_step(&e, 500, 0);
  TEST_ASSERT_EQUAL_UINT8(0, f.expr); /* not yet: eyes still open */
  for (uint64_t t = 510; t <= 900; t += 10) {
    f = maus_engine_step(&e, t, 0);
    if (f.expr == 2) {
      TEST_ASSERT_EQUAL_UINT8(MAUS_BLINK_STEPS - 1, f.blink_step);
      return;
    }
  }
  TEST_FAIL_MESSAGE("curious never reached its first expression");
}

static void test_never_blinking_states_only_blink_to_change_expression(void) {
  maus_engine_set_state(&e, UI_MAUS_SLEEPING, 0); /* blink 0, 0: never on its own */
  maus_frame_t prev = maus_engine_step(&e, 0, 0);
  int blinks = 0;
  int changes = 0;
  for (uint64_t t = 10; t <= 30000; t += 10) {
    maus_frame_t f = maus_engine_step(&e, t, 0);
    if (prev.blink_step == 0 && f.blink_step != 0) blinks++;
    if (f.expr != prev.expr) changes++;
    prev = f;
  }
  TEST_ASSERT_GREATER_THAN(0, changes);
  /* every blink carries an expression change (the last one may still be closing) */
  TEST_ASSERT_TRUE(blinks == changes || blinks == changes + 1);
}

static void test_same_seed_same_frames(void) {
  maus_engine_t a, b;
  maus_engine_init(&a, &k_art, 7);
  maus_engine_init(&b, &k_art, 7);
  maus_engine_set_state(&a, UI_MAUS_IDLE, 0);
  maus_engine_set_state(&b, UI_MAUS_IDLE, 0);
  for (uint64_t t = 0; t <= 30000; t += 10) {
    maus_frame_t fa = maus_engine_step(&a, t, 0);
    maus_frame_t fb = maus_engine_step(&b, t, 0);
    TEST_ASSERT_EQUAL_MEMORY(&fa, &fb, sizeof fa);
  }
}

static void test_different_seeds_differ(void) {
  maus_engine_t a, b;
  maus_engine_init(&a, &k_art, 1);
  maus_engine_init(&b, &k_art, 2);
  maus_engine_set_state(&a, UI_MAUS_IDLE, 0);
  maus_engine_set_state(&b, UI_MAUS_IDLE, 0);
  int differ = 0;
  for (uint64_t t = 0; t <= 30000; t += 10) {
    maus_frame_t fa = maus_engine_step(&a, t, 0);
    maus_frame_t fb = maus_engine_step(&b, t, 0);
    if (fa.expr != fb.expr || fa.blink_step != fb.blink_step) differ++;
  }
  TEST_ASSERT_GREATER_THAN(0, differ);
}

static void test_bob_moves_up_then_down_in_whole_pixels(void) {
  maus_engine_set_state(&e, UI_MAUS_CURIOUS, 0); /* bob 10.0 px, 1000 ms */
  TEST_ASSERT_EQUAL_INT16(0, maus_engine_step(&e, 0, 0).dy);
  TEST_ASSERT_EQUAL_INT16(-10, maus_engine_step(&e, 250, 0).dy);
  TEST_ASSERT_EQUAL_INT16(0, maus_engine_step(&e, 500, 0).dy);
  TEST_ASSERT_EQUAL_INT16(10, maus_engine_step(&e, 750, 0).dy);
  TEST_ASSERT_EQUAL_INT16(0, maus_engine_step(&e, 750, 0).dx);
}

static void test_jitter_stays_within_its_amplitude(void) {
  maus_engine_set_state(&e, UI_MAUS_ALERTING, 0); /* jitter 2.7 px, 85 ms */
  int moved = 0;
  for (uint64_t t = 0; t <= 2000; t += 10) {
    maus_frame_t f = maus_engine_step(&e, t, 0);
    TEST_ASSERT_TRUE(f.dx >= -3 && f.dx <= 3);
    TEST_ASSERT_TRUE(f.dy >= -3 && f.dy <= 3);
    if (f.dx != 0 || f.dy != 0) moved++;
  }
  TEST_ASSERT_GREATER_THAN(50, moved);
}

static void test_speaking_opens_the_mouth_only_when_speaking(void) {
  maus_engine_set_state(&e, UI_MAUS_SPEAKING, 0); /* first expression: index 3 (app 19) */
  maus_frame_t f = maus_engine_step(&e, 0, 2);
  TEST_ASSERT_EQUAL_INT8(0, f.speak); /* speak_expr[0] == 3 */
  TEST_ASSERT_EQUAL_UINT8(2, f.speak_level);
  f = maus_engine_step(&e, 10, 0);
  TEST_ASSERT_EQUAL_INT8(-1, f.speak);
  f = maus_engine_step(&e, 20, 9); /* clamped to the art's levels */
  TEST_ASSERT_EQUAL_UINT8(MAUS_SPEAK_LEVELS, f.speak_level);
  maus_engine_set_state(&e, UI_MAUS_IDLE, 30);
  f = maus_engine_step(&e, 30, 3);
  TEST_ASSERT_EQUAL_INT8(-1, f.speak);
}

static void test_hidden_state_draws_nothing(void) {
  maus_frame_t f = maus_engine_step(&e, 100, 3);
  TEST_ASSERT_EQUAL_INT8(-1, f.speak);
  TEST_ASSERT_EQUAL_INT16(0, f.dx);
  TEST_ASSERT_EQUAL_INT16(0, f.dy);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_sine_table_hits_the_quarter_points);
  RUN_TEST(test_blink_closes_fast_and_opens_slowly);
  RUN_TEST(test_first_state_shows_its_first_expression_at_once);
  RUN_TEST(test_expression_changes_only_while_the_eyes_are_closed);
  RUN_TEST(test_state_change_swaps_expression_under_a_blink);
  RUN_TEST(test_never_blinking_states_only_blink_to_change_expression);
  RUN_TEST(test_same_seed_same_frames);
  RUN_TEST(test_different_seeds_differ);
  RUN_TEST(test_bob_moves_up_then_down_in_whole_pixels);
  RUN_TEST(test_jitter_stays_within_its_amplitude);
  RUN_TEST(test_speaking_opens_the_mouth_only_when_speaking);
  RUN_TEST(test_hidden_state_draws_nothing);
  return UNITY_END();
}
```

Append to `firmware/tests/CMakeLists.txt`:

```bash
cat >> firmware/tests/CMakeLists.txt <<'EOF'

if(GADGET_WITH_LVGL)
  gadget_ui_test(copy)
  gadget_ui_test(maus_engine)
endif()
EOF
```

- [ ] **Step 2: Watch them fail**

Run: `cmake -S firmware -B build/host && cmake --build build/host -j10 --target test_ui_copy test_ui_maus_engine`
Expected: FAIL: `fatal error: 'ui_copy.h' file not found` and `'ui_maus_engine.h' file not found`.

- [ ] **Step 3: Write `ui_copy.h` and `ui_copy.c`**

`firmware/ui/ui_copy.h`:

```c
/* SPDX-License-Identifier: Apache-2.0 */
/* Screen copy (contract 2.15): pure functions over the model, no LVGL, so
 * the exact English strings are unit-tested on their own. */
#ifndef UI_COPY_H
#define UI_COPY_H

#include <stddef.h>
#include <stdint.h>
#include "gadget_ui_model.h"

#define UI_ELLIPSIS "\xE2\x80\xA6" /* U+2026 */
#define UI_ARROW "\xE2\x86\x92"    /* U+2192 */
#define UI_COPY_MAX 512
#define UI_COPY_ASK_ELSEWHERE "Answer on your computer or phone"

typedef struct {
  char caption[UI_COPY_MAX]; /* main text, lines separated by '\n' */
  char host[UI_NAME_MAX];    /* the host name on its own line (spec 5.5), "" for none */
  char status[UI_COPY_MAX];  /* one line at the bottom, "" for none */
} ui_copy_t;

/* Whole seconds until retry_at_ms, rounded up, never below 1. */
uint32_t ui_retry_seconds(uint64_t now_ms, uint64_t retry_at_ms);

void ui_copy_idle(const ui_model_t *m, char *out, size_t cap);
void ui_copy_setup(const ui_model_t *m, ui_copy_t *out);
void ui_copy_offline(const ui_model_t *m, ui_copy_t *out);
void ui_copy_update(const ui_model_t *m, char *out, size_t cap);
/* "5" … "1" during the last 5 s of a recording, "" otherwise. */
void ui_copy_countdown(const ui_model_t *m, char *out, size_t cap);

#endif /* UI_COPY_H */
```

`firmware/ui/ui_copy.c`:

```c
/* SPDX-License-Identifier: Apache-2.0 */
#include "ui_copy.h"

#include <stdio.h>

uint32_t ui_retry_seconds(uint64_t now_ms, uint64_t retry_at_ms) {
  if (retry_at_ms <= now_ms) return 1;
  uint64_t s = (retry_at_ms - now_ms + 999u) / 1000u;
  return s < 1 ? 1u : (uint32_t)s;
}

void ui_copy_idle(const ui_model_t *m, char *out, size_t cap) {
  if (m->bot_name[0]) snprintf(out, cap, "Hi, I'm %s", m->bot_name);
  else snprintf(out, cap, "Hi");
}

void ui_copy_setup(const ui_model_t *m, ui_copy_t *out) {
  const char *c = NULL; /* an unknown step shows only the device id */
  out->caption[0] = '\0';
  out->host[0] = '\0';
  switch (m->setup.step) {
  case UI_SETUP_NEED_WIFI:
    c = "Connect me to Wi-Fi with the installer.";
    break;
  case UI_SETUP_NEED_CODE:
    c = "Pair me: MausBot " UI_ARROW " Settings " UI_ARROW " Remote access " UI_ARROW " Pair a gadget\n"
        "Enter the code in the installer.\n"
        "Remote access must be on.";
    break;
  case UI_SETUP_PAIRING: /* the host name is part of this caption */
    if (m->host_name[0]) snprintf(out->caption, sizeof out->caption, "Pairing with %s" UI_ELLIPSIS, m->host_name);
    else snprintf(out->caption, sizeof out->caption, "Pairing" UI_ELLIPSIS);
    break;
  case UI_SETUP_HOST_NOT_FOUND:
    c = "Can't find MausBot. Enter the address shown under Pair a gadget in the installer.";
    break;
  case UI_SETUP_BAD_CODE:
    c = "That code didn't work. Get a new one from Pair a gadget.";
    break;
  case UI_SETUP_DEVICE_LIMIT:
    c = "MausBot has too many devices. Remove one in Remote access. Retrying" UI_ELLIPSIS;
    break;
  }
  if (c) {
    snprintf(out->caption, sizeof out->caption, "%s", c);
    snprintf(out->host, sizeof out->host, "%s", m->host_name); /* "" until a challenge has arrived */
  }
  snprintf(out->status, sizeof out->status, "%s", m->device_id);
}

void ui_copy_offline(const ui_model_t *m, ui_copy_t *out) {
  const char *host = m->host_name[0] ? m->host_name : "MausBot";
  bool retry = false;
  bool host_line = true; /* the stored host name on its own line, unless the caption names it */
  out->caption[0] = '\0';
  out->host[0] = '\0';
  out->status[0] = '\0';
  switch (m->offline.reason) {
  case UI_OFFLINE_WIFI_CONNECTING:
    snprintf(out->caption, sizeof out->caption, "Connecting to Wi-Fi" UI_ELLIPSIS);
    break;
  case UI_OFFLINE_WIFI_FAILED:
    snprintf(out->caption, sizeof out->caption, "Can't join %s.", m->offline.ssid[0] ? m->offline.ssid : "Wi-Fi");
    break;
  case UI_OFFLINE_HOST_LOOKUP:
    snprintf(out->caption, sizeof out->caption, "Looking for MausBot" UI_ELLIPSIS);
    break;
  case UI_OFFLINE_HOST_UNREACHABLE:
    snprintf(out->caption, sizeof out->caption,
             "Can't reach %s.\nIs Remote access on in MausBot?\nOn Windows, set this network to Private.", host);
    retry = true;
    host_line = false;
    break;
  case UI_OFFLINE_IN_USE_ELSEWHERE:
    snprintf(out->caption, sizeof out->caption, "In use elsewhere\nPress TALK to use it here.");
    break;
  case UI_OFFLINE_PROTOCOL:
    snprintf(out->caption, sizeof out->caption, "MausBot didn't accept me.");
    retry = true;
    break;
  }
  if (!out->caption[0]) return; /* an unknown reason shows nothing */
  if (host_line) snprintf(out->host, sizeof out->host, "%s", m->host_name);
  if (retry && m->offline.retry_at_ms != 0) {
    snprintf(out->status, sizeof out->status, "Retrying in %u s",
             (unsigned)ui_retry_seconds(m->now_ms, m->offline.retry_at_ms));
  }
}

void ui_copy_update(const ui_model_t *m, char *out, size_t cap) {
  if (cap) out[0] = '\0';
  switch (m->update.phase) {
  case UI_UPDATE_RECEIVING:
    snprintf(out, cap, "Updating" UI_ELLIPSIS " %u%%", (unsigned)(m->update.pct > 100 ? 100 : m->update.pct));
    break;
  case UI_UPDATE_VERIFYING:
    snprintf(out, cap, "Checking update" UI_ELLIPSIS);
    break;
  case UI_UPDATE_RESTARTING:
    snprintf(out, cap, "Restarting" UI_ELLIPSIS);
    break;
  }
}

void ui_copy_countdown(const ui_model_t *m, char *out, size_t cap) {
  if (m->listening.countdown_s >= 1 && m->listening.countdown_s <= 5) snprintf(out, cap, "%u", (unsigned)m->listening.countdown_s);
  else if (cap) out[0] = '\0';
}
```

- [ ] **Step 4: Write `ui_maus_engine.h` and `ui_maus_engine.c`**

Port of the app's face engine (`CursorAvatar.tsx` `blinkScale` L1574–L1583 and the per-state cadences) to integers: a blink lasts 320 ms, closes in 134 ms and opens in 186 ms, and snaps to the nearest of the four baked steps; a due expression change starts a blink and swaps at the closed step ("blink-cut"); motion uses a 65-entry quarter-wave sine table (`round(sin(i·π/128)·32767)`), so frames are identical on arm64 and x86_64.

`firmware/ui/ui_maus_engine.h`:

```c
/* SPDX-License-Identifier: Apache-2.0 */
/* The Maus animation as plain state: which expression, which blink step,
 * which mouth and how far the body is moved, for a time `now`. No LVGL
 * objects; ui_maus.c turns a frame into image sources and positions.
 * Integer-only and driven by the seeded xorshift32 PRNG (gadget_util.h),
 * so a fixed seed gives the same frames on every machine (spec 5.5). */
#ifndef UI_MAUS_ENGINE_H
#define UI_MAUS_ENGINE_H

#include "art/maus_art.h"
#include "gadget_util.h"

#define MAUS_BLINK_MS 320u
#define MAUS_BLINK_CLOSE_MS 134u /* the eye is fully closed at 0.42 of the blink */

typedef struct {
  uint8_t expr;        /* index into art->expr_ids */
  uint8_t blink_step;  /* 0 (open) .. MAUS_BLINK_STEPS-1 (closed) */
  int8_t speak;        /* index into art->speak_expr when an open mouth shows, else -1 */
  uint8_t speak_level; /* 1..MAUS_SPEAK_LEVELS when speak >= 0, else 0 */
  int16_t dx, dy;      /* body offset in px */
} maus_frame_t;

typedef struct {
  const maus_art_t *art;
  gadget_prng_t prng;
  ui_maus_state_t state; /* UI_MAUS_NONE: hidden */
  uint64_t since_ms;     /* start of the current state (motion phase 0) */
  uint8_t expr;
  int16_t pending;       /* expression to switch to at the blink's closed step, -1 = none */
  bool blinking;
  uint64_t blink_start;
  uint64_t next_blink;   /* 0 = this state never blinks on its own */
  uint64_t next_expr;
} maus_engine_t;

void maus_engine_init(maus_engine_t *e, const maus_art_t *art, uint32_t seed);
/* Enter a state. Coming from UI_MAUS_NONE shows the pool's first expression
 * at once; otherwise a different first expression arrives under a blink. */
void maus_engine_set_state(maus_engine_t *e, ui_maus_state_t state, uint64_t now_ms);
/* Advance to now_ms (monotonic) and return the frame to draw. */
maus_frame_t maus_engine_step(maus_engine_t *e, uint64_t now_ms, uint8_t speak_level);

/* Helpers, exposed for tests. */
int32_t maus_sin_q15(uint32_t phase16);           /* sin(2*pi*phase16/65536) * 32767 */
uint8_t maus_blink_step(uint32_t t_ms);           /* blink step at t ms into a blink */

#endif /* UI_MAUS_ENGINE_H */
```

`firmware/ui/ui_maus_engine.c`:

```c
/* SPDX-License-Identifier: Apache-2.0 */
#include "ui_maus_engine.h"

#include <string.h>

/* sin(i * pi / 128) * 32767 for i = 0..64 (a quarter turn in 64 steps). */
static const int16_t k_sin_q15[65] = {
  0, 804, 1608, 2410, 3212, 4011, 4808, 5602, 6393, 7179, 7962, 8739, 9512,
  10278, 11039, 11793, 12539, 13279, 14010, 14732, 15446, 16151, 16846, 17530, 18204, 18868,
  19519, 20159, 20787, 21403, 22005, 22594, 23170, 23731, 24279, 24811, 25329, 25832, 26319,
  26790, 27245, 27683, 28105, 28510, 28898, 29268, 29621, 29956, 30273, 30571, 30852, 31113,
  31356, 31580, 31785, 31971, 32137, 32285, 32412, 32521, 32609, 32678, 32728, 32757, 32767,
};

static int32_t sin_at(uint32_t i) { /* i in 1/256 turns */
  i &= 255u;
  if (i <= 64u) return k_sin_q15[i];
  if (i <= 128u) return k_sin_q15[128u - i];
  if (i <= 192u) return -k_sin_q15[i - 128u];
  return -k_sin_q15[256u - i];
}

int32_t maus_sin_q15(uint32_t phase16) {
  uint32_t i = (phase16 & 0xFFFFu) >> 8;
  int32_t f = (int32_t)(phase16 & 0xFFu);
  int32_t a = sin_at(i);
  int32_t b = sin_at(i + 1u);
  return a + (b - a) * f / 256;
}

uint8_t maus_blink_step(uint32_t t_ms) {
  int32_t s; /* eye openness x1000: fast close, slower open, never below 0.04 */
  if (t_ms < MAUS_BLINK_CLOSE_MS) s = 1000 - (int32_t)(t_ms * 1000u / MAUS_BLINK_CLOSE_MS);
  else s = (int32_t)((t_ms - MAUS_BLINK_CLOSE_MS) * 1000u / (MAUS_BLINK_MS - MAUS_BLINK_CLOSE_MS));
  if (s < 40) s = 40;
  /* nearest baked step: 1.0, 0.6, 0.25, 0.04 */
  if (s > 800) return 0;
  if (s > 425) return 1;
  if (s > 145) return 2;
  return 3;
}

static uint32_t phase16(uint64_t t_ms, uint32_t period_ms) {
  return (uint32_t)((t_ms % period_ms) * 65536u / period_ms);
}

static int16_t tenths_to_px(int32_t v) {
  return (int16_t)(v >= 0 ? (v + 5) / 10 : -((-v + 5) / 10));
}

static uint64_t after(maus_engine_t *e, uint64_t now, uint16_t lo, uint16_t hi) {
  return now + gadget_prng_range(&e->prng, lo, hi);
}

static void start_blink(maus_engine_t *e, uint64_t now, int16_t pending) {
  e->blinking = true;
  e->blink_start = now;
  e->pending = pending;
}

void maus_engine_init(maus_engine_t *e, const maus_art_t *art, uint32_t seed) {
  memset(e, 0, sizeof *e);
  e->art = art;
  gadget_prng_seed(&e->prng, seed);
  e->state = UI_MAUS_NONE;
  e->pending = -1;
}

void maus_engine_set_state(maus_engine_t *e, ui_maus_state_t state, uint64_t now_ms) {
  if (state == e->state) return;
  ui_maus_state_t prev = e->state;
  e->state = state;
  e->since_ms = now_ms;
  if (state == UI_MAUS_NONE) return;
  const maus_state_def_t *d = &e->art->states[state];
  uint8_t first = d->pool[0];
  if (prev == UI_MAUS_NONE) {
    e->expr = first;
    e->blinking = false;
    e->pending = -1;
  } else if (e->expr != first) {
    start_blink(e, now_ms, (int16_t)first);
  }
  e->next_expr = after(e, now_ms, d->cad_min_ms, d->cad_max_ms);
  e->next_blink = d->blink_max_ms ? after(e, now_ms, d->blink_min_ms, d->blink_max_ms) : 0;
}

static uint8_t pick_other(maus_engine_t *e, const maus_state_def_t *d) {
  uint8_t cand[16];
  uint8_t n = 0;
  for (uint8_t i = 0; i < d->pool_len && n < sizeof cand; i++) {
    if (d->pool[i] != e->expr) cand[n++] = d->pool[i];
  }
  if (n == 0) return e->expr;
  return cand[gadget_prng_range(&e->prng, 0, (uint32_t)n - 1u)];
}

maus_frame_t maus_engine_step(maus_engine_t *e, uint64_t now, uint8_t speak_level) {
  maus_frame_t f = {0, 0, -1, 0, 0, 0};
  if (e->state == UI_MAUS_NONE) return f;
  const maus_state_def_t *d = &e->art->states[e->state];

  if (now >= e->next_expr) {
    if (d->pool_len > 1 && !e->blinking) start_blink(e, now, (int16_t)pick_other(e, d));
    e->next_expr = after(e, now, d->cad_min_ms, d->cad_max_ms);
  }
  if (!e->blinking && e->next_blink != 0 && now >= e->next_blink) {
    start_blink(e, now, -1);
    e->next_blink = after(e, now, d->blink_min_ms, d->blink_max_ms);
  }

  if (e->blinking) {
    uint64_t t = now - e->blink_start;
    if (t >= MAUS_BLINK_MS) {
      e->blinking = false;
      if (e->pending >= 0) e->expr = (uint8_t)e->pending; /* a tick skipped the closed step */
      e->pending = -1;
    } else {
      f.blink_step = maus_blink_step((uint32_t)t);
      if (e->pending >= 0 && f.blink_step == MAUS_BLINK_STEPS - 1) {
        e->expr = (uint8_t)e->pending;
        e->pending = -1;
      }
    }
  }
  f.expr = e->expr;

  if (e->state == UI_MAUS_SPEAKING && speak_level > 0) {
    for (uint8_t i = 0; i < MAUS_SPEAK_EXPRS; i++) {
      if (e->art->speak_expr[i] == e->expr) {
        f.speak = (int8_t)i;
        f.speak_level = speak_level > MAUS_SPEAK_LEVELS ? MAUS_SPEAK_LEVELS : speak_level;
      }
    }
  }

  uint64_t t = now - e->since_ms;
  int32_t dx = 0;
  int32_t dy = 0;
  if (d->bob_ms) dy -= d->bob_px_x10 * maus_sin_q15(phase16(t, d->bob_ms)) / 32767;
  if (d->jitter_ms) {
    uint32_t py = (uint32_t)d->jitter_ms * 63u / 100u;
    dx += d->jitter_px_x10 * maus_sin_q15(phase16(t, d->jitter_ms)) / 32767;
    dy += d->jitter_px_x10 * maus_sin_q15(phase16(t, py ? py : 1u) + 11473u) / 32767; /* +1.1 rad */
  }
  if (d->circle_ms) {
    uint32_t p = phase16(t, d->circle_ms);
    dx += d->circle_px_x10 * maus_sin_q15(p) / 32767;
    dy += d->circle_px_x10 * maus_sin_q15(p + 16384u) / 32767; /* +pi/2 */
  }
  f.dx = tenths_to_px(dx);
  f.dy = tenths_to_px(dy);
  return f;
}
```

- [ ] **Step 5: Add the sources to `gadget_ui`**

In `firmware/ui/CMakeLists.txt` replace

```cmake
set(UI_SRCS
  art/maus_art.c)
```

with

```cmake
set(UI_SRCS
  ui_maus_engine.c ui_copy.c
  art/maus_art.c)
```

- [ ] **Step 6: Run the tests and watch them pass**

```bash
cmake --build build/host -j10 --target test_ui_copy test_ui_maus_engine
ctest --test-dir build/host --output-on-failure -R '^ui\.(copy|maus_engine)$'
```

Expected: `ui.copy` 9 tests and `ui.maus_engine` 12 tests, `0 Failures`.

- [ ] **Step 7: Commit**

```bash
git add firmware/ui firmware/tests/CMakeLists.txt firmware/tests/test_ui_copy.c firmware/tests/test_ui_maus_engine.c
git commit -m "feat(ui): screen copy and the Maus animation engine" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 5: The LVGL screens

**Files:**
- Create: `firmware/ui/ui_theme.h`, `firmware/ui/ui_metrics.c`, `firmware/ui/ui_pager.h`, `firmware/ui/ui_pager.c`, `firmware/ui/ui_maus.h`, `firmware/ui/ui_maus.c`, `firmware/ui/ui_priv.h`, `firmware/ui/ui_screens.c`, `firmware/ui/ui.c`
- Modify: `firmware/ui/CMakeLists.txt` (`UI_SRCS`), `firmware/tests/CMakeLists.txt` (append)
- Test: `firmware/tests/test_ui_screens.c`

**Interfaces:**
- Consumes: `gadget_ui.h` (`ui_layout_ask`, `ui_safe_area` from P2a core; the four `ui_*` functions implemented here), `gadget_board_by_id` (P2a), Tasks 2–4.
- Produces: `gadget_status_t ui_init(const gadget_board_t *, uint32_t prng_seed)` (`GADGET_ERR_ARG` for NULL, `GADGET_ERR_UNSUPPORTED` when the board's art profile is not linked), `void ui_render(const ui_model_t *)`, `void ui_tick(uint64_t)`, `void ui_deinit(void)`; private: `ui_metrics_t`, `ui_metrics_for()`, `ui_rect_in_circle()` (ui_theme.h), `ui_pager_t` + `ui_pager_*` (ui_pager.h, `UI_PAGE_ROTATE_MS` 4000, `UI_MS_PER_LINE` 2000; `ui_pager_set(p, text, font, color, align, sub, status, status_font, status_color)`, where `sub` is the host-name line above the status line), `ui_maus_t` + `ui_maus_create/set_state/tick` (ui_maus.h), `ui_state_t g_ui` (ui_priv.h; `g_ui.applies` counts `ui_screens_apply()` calls for the render-cache test). `ui.c` keeps two `ui_model_t` copies for the render cache, declared `static UI_MODEL_COPY_ATTR ui_model_t s_last, s_cur;` (`UI_MODEL_COPY_ATTR` defaults to empty; P2c may define it to place them in PSRAM).

Layout decisions (all in `ui_metrics.c`): round 466 → Maus top-centre at y 30, caption = the widest rectangle inside the circle below it; square 240 → Maus top-centre, caption underneath; landscape 320×240 → Maus left, text column right, status line full width at the bottom. Fonts: 466 boards title 28 / body 24 / small 20 / tiny 16 / big 40; smaller boards 20 / 16 / 14 / 14 / 28. Captions show whole lines only (`ui_pager`): Setup and Offline copy that does not fit turns its page every 4 s, Thinking and Reply show the last lines, Speaking pages with the stream's progress. Under the caption, from the bottom up: the status line (device id, retry countdown, working) and, on Setup and Offline, the host name on its own dim line (spec §5.5: "shows `host_name` once a `challenge` has arrived", "the stored `host_name`"; no new English, the name itself) wherever the caption does not already name it. The battery is a 30° arc centred on the top edge on every board with a battery (spec §5.5), cut from a circle as wide as the screen. On button boards (no touch) the ask shows up to two pills with `TALK` / `CANCEL` under them (P2b-local strings; no other plan quotes them). `ui_render` re-lays out a screen only when a model field other than `rev`, `now_ms`, `mic_level`, `speak_level` and the speaking clock changes (`reply.speak_elapsed_ms`, `reply.speak_total_ms`); those are drawn every render by `ui_screens_apply_time()` and `ui_maus_tick()`. On this Mac that cut a speaking tick with an 8 KiB reply from about 1.3 ms to about 50 µs (measured in review).

- [ ] **Step 1: Write the failing test**

`firmware/tests/test_ui_screens.c` draws every screen on all four boards with LVGL's test display and checks the copy, what is hidden, that every visible text box stays on the screen (inside the circle on round boards), the Review Focus cases (long text, out-of-range values, time-only changes, images, the render cache), and that A8 eyes land as white pixels. It includes the private `ui_priv.h` only to read `g_ui.applies`:

```c
/* SPDX-License-Identifier: Apache-2.0 */
/* Every screen on every board size, drawn by the real UI on LVGL's test
 * display: the copy that must show, what must be hidden, and that every
 * visible text box stays on the screen (inside the circle on round boards). */
#include <stdio.h>
#include <string.h>

#include "gadget_board.h"
#include "gadget_ui.h"
#include "lvgl.h"
#include "ui_copy.h"
#include "ui_lv_compat.h"
#include "ui_pager.h"
#include "ui_priv.h" /* g_ui.applies: the render-cache test */
#include "art/maus_art.h"
#include "unity.h"

static ui_model_t m;
static const gadget_board_t *board;
static lv_display_t *disp;
static uint16_t k_pixels[40 * 30];

static const char *const k_boards[] = {"amoled-175c", "amoled-175", "lcd-154", "devkit"};

void setUp(void) {}

static bool g_started;

static void stop(void) {
  if (!g_started) return;
  ui_deinit();
  lv_deinit();
  g_started = false;
}

static void start(const char *id) {
  stop();
  board = gadget_board_by_id(id);
  TEST_ASSERT_NOT_NULL(board);
  lv_init();
  disp = lv_test_display_create(board->screen_w, board->screen_h);
  lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);
  memset(&m, 0, sizeof m);
  strcpy(m.device_id, "gad_b18b86ce1389e46d");
  g_started = true;
  TEST_ASSERT_EQUAL_INT(GADGET_OK, ui_init(board, 1));
}

void tearDown(void) { stop(); }

static void render(uint64_t now) {
  m.rev++;
  m.now_ms = now;
  lv_tick_inc(10);
  ui_render(&m);
  ui_tick(now);
  lv_obj_update_layout(lv_screen_active());
  lv_refr_now(disp);
}

static bool visible(const lv_obj_t *o) {
  for (; o; o = lv_obj_get_parent(o)) {
    if (ui_is_hidden(o)) return false;
  }
  return true;
}

static bool clip(lv_area_t *a, const lv_area_t *b) {
  a->x1 = LV_MAX(a->x1, b->x1);
  a->y1 = LV_MAX(a->y1, b->y1);
  a->x2 = LV_MIN(a->x2, b->x2);
  a->y2 = LV_MIN(a->y2, b->y2);
  return a->x1 <= a->x2 && a->y1 <= a->y2;
}

/* The part of `o` that can show: its area clipped by every ancestor. */
static bool shown_area(const lv_obj_t *o, lv_area_t *out) {
  lv_obj_get_coords(o, out);
  for (const lv_obj_t *p = lv_obj_get_parent(o); p; p = lv_obj_get_parent(p)) {
    lv_area_t pa;
    lv_obj_get_coords(p, &pa);
    if (!clip(out, &pa)) return false;
  }
  return true;
}

typedef struct {
  const char *needle;
  bool found;
  lv_obj_t *obj;
} find_t;

static void walk(lv_obj_t *o, void (*fn)(lv_obj_t *, void *), void *ctx) {
  fn(o, ctx);
  uint32_t n = lv_obj_get_child_count(o);
  for (uint32_t i = 0; i < n; i++) walk(lv_obj_get_child(o, (int32_t)i), fn, ctx);
}

static void find_cb(lv_obj_t *o, void *ctx) {
  find_t *f = ctx;
  if (lv_obj_check_type(o, &lv_label_class) && visible(o) && strstr(lv_label_get_text(o), f->needle)) {
    f->found = true;
    f->obj = o;
  }
}

static bool shows(const char *needle) {
  find_t f = {needle, false, NULL};
  walk(lv_screen_active(), find_cb, &f);
  return f.found;
}

static bool inside(int32_t x, int32_t y) {
  if (x < 0 || y < 0 || x >= board->screen_w || y >= board->screen_h) return false;
  if (!board->screen_round) return true;
  int32_t r = board->screen_w / 2;
  int32_t dx = x - r;
  int32_t dy = y - r;
  return dx * dx + dy * dy <= r * r;
}

static void bounds_cb(lv_obj_t *o, void *ctx) {
  (void)ctx;
  if (!lv_obj_check_type(o, &lv_label_class) || !visible(o) || lv_label_get_text(o)[0] == '\0') return;
  lv_area_t a;
  if (!shown_area(o, &a)) return;
  char msg[320];
  snprintf(msg, sizeof msg, "%s: text \"%.40s\" at %d,%d-%d,%d leaves the screen", board->id, lv_label_get_text(o),
           (int)a.x1, (int)a.y1, (int)a.x2, (int)a.y2);
  TEST_ASSERT_TRUE_MESSAGE(inside(a.x1, a.y1) && inside(a.x2, a.y1) && inside(a.x1, a.y2) && inside(a.x2, a.y2), msg);
}

static void assert_text_on_screen(void) { walk(lv_screen_active(), bounds_cb, NULL); }

/* The visible label whose text contains `needle`. */
static lv_obj_t *label_with(const char *needle) {
  find_t f = {needle, false, NULL};
  walk(lv_screen_active(), find_cb, &f);
  TEST_ASSERT_NOT_NULL_MESSAGE(f.obj, needle);
  return f.obj;
}

/* A label's y inside its parent, after LVGL has applied pending moves. */
static int32_t label_y(lv_obj_t *l) {
  lv_obj_update_layout(lv_screen_active());
  return lv_obj_get_y(l);
}

static uint16_t centre_pixel(void) {
  lv_draw_buf_t *buf = lv_display_get_buf_active(disp);
  uint16_t px;
  memcpy(&px, buf->data + (uint32_t)(board->screen_h / 2) * buf->header.stride + (uint32_t)(board->screen_w / 2) * 2u, 2);
  return px;
}

static void each_board(void (*body)(void)) {
  for (size_t i = 0; i < sizeof k_boards / sizeof k_boards[0]; i++) {
    start(k_boards[i]);
    body();
    stop();
  }
}

/* ---- Maus screens ------------------------------------------------------ */

static void idle_body(void) {
  m.screen = UI_SCREEN_IDLE;
  m.maus = UI_MAUS_IDLE;
  render(1000);
  TEST_ASSERT_TRUE(shows("Hi"));
  strcpy(m.bot_name, "Jev");
  render(1010);
  TEST_ASSERT_TRUE(shows("Hi, I'm Jev"));
  assert_text_on_screen();
}
static void test_idle(void) { each_board(idle_body); }

static void setup_body(void) {
  m.screen = UI_SCREEN_SETUP;
  m.maus = UI_MAUS_CURIOUS;
  for (int step = UI_SETUP_NEED_WIFI; step <= UI_SETUP_DEVICE_LIMIT; step++) {
    m.setup.step = (ui_setup_step_t)step;
    for (uint64_t t = 0; t <= 6 * UI_PAGE_ROTATE_MS; t += UI_PAGE_ROTATE_MS) { /* every rotated page */
      render(t);
      assert_text_on_screen();
    }
    ui_copy_t c;
    ui_copy_setup(&m, &c);
    TEST_ASSERT_TRUE(shows(c.caption));
    TEST_ASSERT_TRUE(shows("gad_b18b86ce1389e46d"));
  }
  m.setup.step = UI_SETUP_NEED_CODE;
  render(0);
  TEST_ASSERT_TRUE(shows("Pair me: MausBot " UI_ARROW " Settings " UI_ARROW " Remote access " UI_ARROW " Pair a gadget"));
  TEST_ASSERT_TRUE(shows("Remote access must be on."));
  TEST_ASSERT_FALSE(shows("Omkar's computer"));
  strcpy(m.host_name, "Omkar's computer"); /* a challenge has arrived (spec 5.5) */
  m.setup.step = UI_SETUP_BAD_CODE;
  render(0);
  TEST_ASSERT_TRUE(shows("Omkar's computer"));
  TEST_ASSERT_TRUE(shows("gad_b18b86ce1389e46d"));
  assert_text_on_screen();
}
static void test_setup(void) { each_board(setup_body); }

static void offline_body(void) {
  m.screen = UI_SCREEN_OFFLINE;
  m.maus = UI_MAUS_SLEEPING;
  strcpy(m.host_name, "Omkar's computer");
  for (int r = UI_OFFLINE_WIFI_CONNECTING; r <= UI_OFFLINE_PROTOCOL; r++) {
    m.offline.reason = (ui_offline_reason_t)r;
    m.offline.retry_at_ms = (r == UI_OFFLINE_HOST_UNREACHABLE || r == UI_OFFLINE_PROTOCOL) ? 9000 : 0;
    render(1000);
    assert_text_on_screen();
    render(5000);
    assert_text_on_screen();
  }
  m.offline.reason = UI_OFFLINE_HOST_UNREACHABLE;
  m.offline.retry_at_ms = 9000;
  render(1000);
  TEST_ASSERT_TRUE(shows("Can't reach Omkar's computer."));
  TEST_ASSERT_TRUE(shows("Retrying in 8 s"));
  /* the countdown moves without a model change */
  m.now_ms = 6500;
  ui_render(&m);
  TEST_ASSERT_TRUE(shows("Retrying in 3 s"));
  /* the stored host_name on its own line where the caption does not name it (spec 5.5) */
  m.offline.reason = UI_OFFLINE_IN_USE_ELSEWHERE;
  m.offline.retry_at_ms = 0;
  render(7000);
  TEST_ASSERT_TRUE(shows("Omkar's computer"));
  m.offline.reason = UI_OFFLINE_PROTOCOL;
  m.offline.retry_at_ms = 9000;
  render(7010);
  TEST_ASSERT_TRUE(shows("Omkar's computer"));
  TEST_ASSERT_TRUE(shows("Retrying in 2 s"));
  assert_text_on_screen();
}
static void test_offline(void) { each_board(offline_body); }

static void listening_body(void) {
  m.screen = UI_SCREEN_LISTENING;
  m.maus = UI_MAUS_LISTENING;
  m.mic_level = 200;
  render(100);
  TEST_ASSERT_FALSE(shows("5"));
  m.listening.countdown_s = 3;
  render(56000);
  TEST_ASSERT_TRUE(shows("3"));
  assert_text_on_screen();
}
static void test_listening(void) { each_board(listening_body); }

static void thinking_body(void) {
  m.screen = UI_SCREEN_THINKING;
  m.maus = UI_MAUS_THINKING;
  strcpy(m.thinking.heard, "What's on my calendar today?");
  render(100);
  TEST_ASSERT_TRUE(shows("What's on my calendar today?"));
  m.maus = UI_MAUS_WORKING;
  strcpy(m.thinking.working, "checking your calendar");
  render(200);
  TEST_ASSERT_TRUE(shows("checking your calendar"));
  assert_text_on_screen();
}
static void test_thinking(void) { each_board(thinking_body); }

static void reply_body(void) {
  m.screen = UI_SCREEN_REPLY;
  m.maus = UI_MAUS_IDLE;
  strcpy(m.reply.text, "You have two meetings today: design review at 10 and lunch with Sam at 1.");
  m.reply.final = true;
  render(100);
  TEST_ASSERT_TRUE(shows("lunch with Sam at 1."));
  assert_text_on_screen();
  m.maus = UI_MAUS_ALERTING;
  m.reply.failed = true;
  strcpy(m.reply.reason, "Connection lost");
  render(200);
  TEST_ASSERT_TRUE(shows("Connection lost"));
  TEST_ASSERT_FALSE(shows("lunch with Sam"));
  assert_text_on_screen();
}
static void test_reply(void) { each_board(reply_body); }

static void speaking_body(void) {
  m.screen = UI_SCREEN_SPEAKING;
  m.maus = UI_MAUS_SPEAKING;
  m.speak_level = 2;
  for (size_t i = 0; i < 40; i++) strcat(m.reply.text, "Word after word. ");
  render(100);
  assert_text_on_screen();
  m.reply.speak_total_ms = 20000;
  m.reply.speak_elapsed_ms = 19000;
  render(200);
  assert_text_on_screen();
}
static void test_speaking(void) { each_board(speaking_body); }

/* ---- text screens ------------------------------------------------------ */

static void fill_ask(uint8_t n, bool question) {
  static const char *const labels[] = {"Allow", "Deny", "Maybe", "Later"};
  m.screen = UI_SCREEN_ASK;
  m.maus = UI_MAUS_NONE;
  strcpy(m.ask.id, "ask-1");
  m.ask.question = question;
  strcpy(m.ask.title, question ? "Which room?" : "Run shell command?");
  strcpy(m.ask.body, "ls -la ~/Documents and then summarise what is in there for me");
  m.ask.n_options = n;
  gadget_rect_t rects[UI_ASK_OPTIONS_MAX] = {{0}};
  ui_layout_ask(board, n, rects);
  for (uint8_t i = 0; i < n; i++) {
    strcpy(m.ask.options[i].id, labels[i]);
    strcpy(m.ask.options[i].label, labels[i]);
    m.ask.options[i].style = question ? UI_STYLE_NEUTRAL : (i == 0 ? UI_STYLE_ALLOW : UI_STYLE_DENY);
    m.ask.options[i].rect = rects[i];
  }
  bool touch = (board->input_mask & GADGET_INPUT_TOUCH) != 0;
  m.ask.answerable = n > 0 && (touch ? n <= 4 : n <= 2);
  m.ask.chosen = -1;
  m.ask.locked_until_ms = 600;
}

static void ask_body(void) {
  fill_ask(2, false);
  render(0);
  TEST_ASSERT_TRUE(shows("Run shell command?"));
  TEST_ASSERT_TRUE(shows("Allow"));
  TEST_ASSERT_TRUE(shows("Deny"));
  TEST_ASSERT_FALSE(shows(UI_COPY_ASK_ELSEWHERE));
  assert_text_on_screen();
  render(1000);

  fill_ask(3, true);
  render(2000);
  if (board->input_mask & GADGET_INPUT_TOUCH) {
    TEST_ASSERT_TRUE(shows("Maybe"));
  } else {
    TEST_ASSERT_TRUE(shows(UI_COPY_ASK_ELSEWHERE));
    TEST_ASSERT_FALSE(shows("Maybe"));
  }
  assert_text_on_screen();

  fill_ask(0, true);
  render(3000);
  TEST_ASSERT_TRUE(shows(UI_COPY_ASK_ELSEWHERE));
  TEST_ASSERT_TRUE(shows("Which room?"));
  assert_text_on_screen();
}
static void test_ask(void) { each_board(ask_body); }

static void card_body(void) {
  m.screen = UI_SCREEN_CARD;
  m.maus = UI_MAUS_NONE;
  strcpy(m.card.title, "Build finished");
  for (int i = 0; i < 30; i++) strcat(m.card.body, "All 312 tests passed. ");
  render(100);
  TEST_ASSERT_TRUE(shows("Build finished"));
  assert_text_on_screen();
}
static void test_card(void) { each_board(card_body); }

static void update_body(void) {
  m.screen = UI_SCREEN_UPDATE;
  m.maus = UI_MAUS_NONE;
  m.update.phase = UI_UPDATE_RECEIVING;
  m.update.pct = 42;
  render(100);
  TEST_ASSERT_TRUE(shows("Updating" UI_ELLIPSIS " 42%"));
  m.update.phase = UI_UPDATE_VERIFYING;
  render(200);
  TEST_ASSERT_TRUE(shows("Checking update" UI_ELLIPSIS));
  m.update.phase = UI_UPDATE_RESTARTING;
  render(300);
  TEST_ASSERT_TRUE(shows("Restarting" UI_ELLIPSIS));
  assert_text_on_screen();
}
static void test_update(void) { each_board(update_body); }

static void image_body(void) {
  for (size_t i = 0; i < sizeof k_pixels / sizeof k_pixels[0]; i++) k_pixels[i] = 0xF800; /* red */
  m.screen = UI_SCREEN_IMAGE;
  m.maus = UI_MAUS_NONE;
  m.image.w = 40;
  m.image.h = 30;
  m.image.pixels = k_pixels;
  m.image.pixels_rev = 1;
  render(100);
  TEST_ASSERT_EQUAL_HEX16(0xF800, centre_pixel());
  /* new pixels under a new pixels_rev show up */
  for (size_t i = 0; i < sizeof k_pixels / sizeof k_pixels[0]; i++) k_pixels[i] = 0x001F; /* blue */
  m.image.pixels_rev = 2;
  render(200);
  TEST_ASSERT_EQUAL_HEX16(0x001F, centre_pixel());
  /* a third picture reuses the first descriptor slot with another size */
  static uint16_t small[20 * 10];
  for (size_t i = 0; i < sizeof small / sizeof small[0]; i++) small[i] = 0x07E0; /* green */
  m.image.w = 20;
  m.image.h = 10;
  m.image.pixels = small;
  m.image.pixels_rev = 3;
  render(300);
  TEST_ASSERT_EQUAL_HEX16(0x07E0, centre_pixel());
}
static void test_image(void) { each_board(image_body); }

static void toast_body(void) {
  m.screen = UI_SCREEN_IDLE;
  m.maus = UI_MAUS_NOTIFYING;
  m.toast.visible = true;
  m.toast.kind = UI_POST_ROUTINE;
  strcpy(m.toast.bot_name, "Jev");
  strcpy(m.toast.text, "Morning brief is ready: 3 meetings, 2 reviews waiting.");
  render(100);
  TEST_ASSERT_TRUE(shows("Morning brief is ready"));
  assert_text_on_screen();
  m.toast.visible = false;
  render(200);
  TEST_ASSERT_FALSE(shows("Morning brief is ready"));
}
static void test_toast(void) { each_board(toast_body); }

/* ---- Review Focus ------------------------------------------------------- */

static void long_text_body(void) {
  memset(m.bot_name, 'W', sizeof m.bot_name - 1); /* one 95-character word */
  m.bot_name[sizeof m.bot_name - 1] = '\0';
  m.screen = UI_SCREEN_IDLE;
  m.maus = UI_MAUS_IDLE;
  render(100);
  assert_text_on_screen();
  m.screen = UI_SCREEN_REPLY;
  memset(m.reply.text, 'a', UI_REPLY_MAX - 1); /* 8 KiB without a space */
  m.reply.text[UI_REPLY_MAX - 1] = '\0';
  render(200);
  assert_text_on_screen();
  fill_ask(2, false);
  memset(m.ask.title, 'T', UI_TITLE_MAX - 1);
  m.ask.title[UI_TITLE_MAX - 1] = '\0';
  memset(m.ask.body, 'b', UI_BODY_MAX - 1);
  m.ask.body[UI_BODY_MAX - 1] = '\0';
  render(300);
  assert_text_on_screen();
  /* a 95-character host name with no space, on Setup and on Offline */
  memset(m.host_name, 'H', sizeof m.host_name - 1);
  m.host_name[sizeof m.host_name - 1] = '\0';
  m.screen = UI_SCREEN_SETUP;
  m.maus = UI_MAUS_CURIOUS;
  m.setup.step = UI_SETUP_PAIRING;
  render(400);
  assert_text_on_screen();
  m.setup.step = UI_SETUP_BAD_CODE; /* the host name on its own line */
  render(500);
  assert_text_on_screen();
  m.screen = UI_SCREEN_OFFLINE;
  m.maus = UI_MAUS_SLEEPING;
  m.offline.reason = UI_OFFLINE_HOST_UNREACHABLE;
  m.offline.retry_at_ms = 9000;
  render(600);
  assert_text_on_screen();
  m.offline.reason = UI_OFFLINE_PROTOCOL;
  render(700);
  assert_text_on_screen();
  /* a toast with a 95-character bot name and 511 characters, both unbroken */
  m.screen = UI_SCREEN_IDLE;
  m.maus = UI_MAUS_NOTIFYING;
  m.toast.visible = true;
  memset(m.toast.bot_name, 'N', sizeof m.toast.bot_name - 1);
  m.toast.bot_name[sizeof m.toast.bot_name - 1] = '\0';
  memset(m.toast.text, 't', UI_TOAST_MAX - 1);
  m.toast.text[UI_TOAST_MAX - 1] = '\0';
  render(800);
  assert_text_on_screen();
  m.toast.visible = false;
  /* a card with a 191-byte title and a 1535-byte body, both unbroken */
  m.screen = UI_SCREEN_CARD;
  m.maus = UI_MAUS_NONE;
  memset(m.card.title, 'C', UI_TITLE_MAX - 1);
  m.card.title[UI_TITLE_MAX - 1] = '\0';
  memset(m.card.body, 'c', UI_BODY_MAX - 1);
  m.card.body[UI_BODY_MAX - 1] = '\0';
  render(900);
  assert_text_on_screen();
}
static void test_long_unbroken_text_stays_on_screen(void) { each_board(long_text_body); }

static void bad_enum_body(void) {
  m.screen = (ui_screen_t)99;
  m.maus = (ui_maus_state_t)99;
  render(100);
  m.screen = UI_SCREEN_OFFLINE;
  m.offline.reason = (ui_offline_reason_t)99;
  m.maus = UI_MAUS_SLEEPING;
  render(200);
  assert_text_on_screen();
}
static void test_out_of_range_model_values_do_not_crash(void) { each_board(bad_enum_body); }

static void ask_unlock_body(void) {
  fill_ask(2, false); /* locked until 600 ms */
  render(0);
  find_t f = {"Allow", false, NULL};
  walk(lv_screen_active(), find_cb, &f);
  TEST_ASSERT_NOT_NULL(f.obj);
  lv_obj_t *button = lv_obj_get_parent(f.obj);
  TEST_ASSERT_EQUAL_UINT8(LV_OPA_50, lv_obj_get_style_opa(button, 0));
  m.now_ms = 700; /* time only: no model change */
  ui_render(&m);
  TEST_ASSERT_EQUAL_UINT8(LV_OPA_COVER, lv_obj_get_style_opa(button, 0));
}
static void test_ask_unlocks_without_a_model_change(void) { each_board(ask_unlock_body); }

static void image_pending_body(void) {
  m.screen = UI_SCREEN_IMAGE;
  m.maus = UI_MAUS_NONE;
  m.image.w = 40;
  m.image.h = 30;
  m.image.pixels = NULL; /* image.begin seen, image.end not yet */
  render(100);
  TEST_ASSERT_EQUAL_HEX16(0x0000, centre_pixel());
}
static void test_image_without_pixels_draws_nothing(void) { each_board(image_pending_body); }

/* Review Focus 3: copy longer than its box turns its page with time alone. */
static void assert_page_turns(const char *needle) {
  render(0);
  lv_obj_t *l = label_with(needle);
  int32_t y0 = label_y(l);
  TEST_ASSERT_TRUE_MESSAGE(lv_obj_get_height(l) > lv_obj_get_height(lv_obj_get_parent(l)), "the copy fits on one page");
  m.now_ms = UI_PAGE_ROTATE_MS; /* time only: rev unchanged */
  ui_render(&m);
  TEST_ASSERT_NOT_EQUAL(y0, label_y(l));
}

static void test_pages_turn_without_a_model_change(void) {
  start("lcd-154");
  m.screen = UI_SCREEN_SETUP;
  m.maus = UI_MAUS_CURIOUS;
  m.setup.step = UI_SETUP_NEED_CODE;
  assert_page_turns("Pair me");
  start("amoled-175c");
  m.screen = UI_SCREEN_OFFLINE;
  m.maus = UI_MAUS_SLEEPING;
  strcpy(m.host_name, "Omkar's computer");
  m.offline.reason = UI_OFFLINE_HOST_UNREACHABLE;
  m.offline.retry_at_ms = 9000;
  assert_page_turns("Can't reach");
  stop();
}

/* Review Focus 3: before speak.end (total unknown) speaking pages by
 * UI_MS_PER_LINE per line, again with time alone. */
static void speaking_unknown_total_body(void) {
  m.screen = UI_SCREEN_SPEAKING;
  m.maus = UI_MAUS_SPEAKING;
  for (size_t i = 0; i < 40; i++) strcat(m.reply.text, "Word after word. ");
  m.reply.speak_total_ms = 0;
  m.reply.speak_elapsed_ms = 0;
  render(100);
  lv_obj_t *l = label_with("Word after word.");
  int32_t y0 = label_y(l);
  m.reply.speak_elapsed_ms = 30 * UI_MS_PER_LINE; /* past the first page on every board */
  ui_render(&m);
  TEST_ASSERT_LESS_THAN_INT32(y0, label_y(l));
}
static void test_speaking_pages_by_time_when_total_unknown(void) { each_board(speaking_unknown_total_body); }

/* Contract 2.9: ui_render is cheap when only levels and the speaking clock
 * move (core bumps rev for them every tick): no re-layout, no new text. */
static void render_cache_body(void) {
  m.screen = UI_SCREEN_SPEAKING;
  m.maus = UI_MAUS_SPEAKING;
  for (size_t i = 0; i < 40; i++) strcat(m.reply.text, "Word after word. ");
  m.reply.speak_total_ms = 20000;
  render(100);
  lv_obj_t *l = label_with("Word after word.");
  const char *text = lv_label_get_text(l);
  uint32_t applies = g_ui.applies;
  for (uint32_t i = 1; i <= 10; i++) {
    m.speak_level = (uint8_t)(i % 4);
    m.mic_level = (uint8_t)(i * 20);
    m.reply.speak_elapsed_ms = i * 1000;
    render(100 + 10 * i); /* rev++ each time */
  }
  TEST_ASSERT_EQUAL_UINT32(applies, g_ui.applies);
  TEST_ASSERT_EQUAL_PTR(text, lv_label_get_text(l));
  strcat(m.reply.text, "And more."); /* a real change is drawn */
  render(300);
  TEST_ASSERT_EQUAL_UINT32(applies + 1, g_ui.applies);
  TEST_ASSERT_TRUE(shows("And more."));
}
static void test_level_and_clock_changes_do_not_relayout(void) { each_board(render_cache_body); }

/* ---- the open item of spec 6.5: do A8 eyes draw white? ------------------ */

typedef struct {
  const void *src;
  lv_obj_t *found;
} img_find_t;

static void img_cb(lv_obj_t *o, void *ctx) {
  img_find_t *f = ctx;
  if (lv_obj_check_type(o, &lv_image_class) && visible(o) && lv_image_get_src(o) == f->src) f->found = o;
}

static void test_a8_eyes_draw_white(void) {
  start("amoled-175c");
  m.screen = UI_SCREEN_IDLE;
  m.maus = UI_MAUS_IDLE;
  render(0);
  const maus_art_t *art = maus_art_for(board->art_profile);
  const lv_image_dsc_t *eye = art->eyes[art->states[UI_MAUS_IDLE].pool[0]][0].img;
  img_find_t f = {eye, NULL};
  walk(lv_screen_active(), img_cb, &f);
  TEST_ASSERT_NOT_NULL_MESSAGE(f.found, "the idle Maus does not show its first expression with open eyes");
  /* a fully opaque pixel of the eye image */
  const uint8_t *alpha = eye->data + (eye->header.cf == LV_COLOR_FORMAT_A8 ? 0 : eye->header.w * eye->header.h * 2);
  int32_t ex = -1, ey = -1;
  for (int32_t y = 0; y < (int32_t)eye->header.h && ex < 0; y++) {
    for (int32_t x = 0; x < (int32_t)eye->header.w; x++) {
      if (alpha[y * (int32_t)eye->header.w + x] == 255) {
        ex = x;
        ey = y;
        break;
      }
    }
  }
  TEST_ASSERT_TRUE(ex >= 0);
  lv_area_t a;
  lv_obj_get_coords(f.found, &a);
  lv_draw_buf_t *buf = lv_display_get_buf_active(disp);
  uint16_t px;
  memcpy(&px, buf->data + (uint32_t)(a.y1 + ey) * buf->header.stride + (uint32_t)(a.x1 + ex) * 2u, 2);
  TEST_ASSERT_EQUAL_HEX16_MESSAGE(0xFFFF, px, "A8 eye pixels must draw white (else switch palette.json eye_format)");
  stop();
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_idle);
  RUN_TEST(test_setup);
  RUN_TEST(test_offline);
  RUN_TEST(test_listening);
  RUN_TEST(test_thinking);
  RUN_TEST(test_reply);
  RUN_TEST(test_speaking);
  RUN_TEST(test_ask);
  RUN_TEST(test_card);
  RUN_TEST(test_update);
  RUN_TEST(test_image);
  RUN_TEST(test_toast);
  RUN_TEST(test_long_unbroken_text_stays_on_screen);
  RUN_TEST(test_out_of_range_model_values_do_not_crash);
  RUN_TEST(test_ask_unlocks_without_a_model_change);
  RUN_TEST(test_image_without_pixels_draws_nothing);
  RUN_TEST(test_pages_turn_without_a_model_change);
  RUN_TEST(test_speaking_pages_by_time_when_total_unknown);
  RUN_TEST(test_level_and_clock_changes_do_not_relayout);
  RUN_TEST(test_a8_eyes_draw_white);
  return UNITY_END();
}
```

Append to `firmware/tests/CMakeLists.txt`:

```bash
cat >> firmware/tests/CMakeLists.txt <<'EOF'

if(GADGET_WITH_LVGL)
  gadget_ui_test(screens)
endif()
EOF
```

- [ ] **Step 2: Watch it fail**

Run: `cmake -S firmware -B build/host && cmake --build build/host -j10 --target test_ui_screens`
Expected: FAIL: `fatal error: 'ui_pager.h' file not found`.

- [ ] **Step 3: Write the theme and the per-board metrics**

`firmware/ui/ui_theme.h`:

```c
/* SPDX-License-Identifier: Apache-2.0 */
/* Screen colours (contract 2.15) and per-board layout metrics. Private to firmware/ui. */
#ifndef UI_THEME_H
#define UI_THEME_H

#include "lvgl.h"
#include "gadget_board.h"

#define UI_COLOR_BG 0x000000
#define UI_COLOR_ACCENT 0x2fd187
#define UI_COLOR_OK 0x3ddc84
#define UI_COLOR_BAD 0xff5a4f
#define UI_COLOR_WARN 0xffb020
#define UI_COLOR_INK 0xf2f4f8
#define UI_COLOR_MUTE 0x9aa2b2
#define UI_COLOR_DIM 0x6f7787

typedef struct {
  bool round, landscape, large;
  int16_t w, h;
  const lv_font_t *font_title, *font_body, *font_small, *font_tiny, *font_big;
  int16_t pad;
  gadget_rect_t maus;     /* Maus body (top-left, size) on Maus screens */
  gadget_rect_t caption;  /* text under (or beside) the Maus */
  gadget_rect_t status;   /* one status line (device id, retry, working); bottom-aligned in it */
  gadget_rect_t safe;     /* ui_safe_area(): text screens (ask, card, update) */
  gadget_rect_t toast;    /* post toast overlay */
  int16_t ring_d;         /* listening ring diameter, centred on the Maus body */
} ui_metrics_t;

/* Fills `out` for `board`; `maus_w`/`maus_h` are the art profile's body size. */
void ui_metrics_for(const gadget_board_t *board, uint16_t maus_w, uint16_t maus_h, ui_metrics_t *out);

/* Widest rectangle inside a circle of diameter `d` (top-left at 0,0) that
 * spans rows y0..y1 (inclusive), minus `margin` px on each side. */
gadget_rect_t ui_rect_in_circle(int16_t d, int16_t y0, int16_t y1, int16_t margin);

#endif /* UI_THEME_H */
```

`firmware/ui/ui_metrics.c`:

```c
/* SPDX-License-Identifier: Apache-2.0 */
#include "ui_theme.h"

#include "gadget_ui.h"
#include "fonts/ui_fonts.h"

static int32_t isqrt32(int32_t v) {
  if (v <= 0) return 0;
  int32_t r = 0;
  while ((r + 1) * (r + 1) <= v) r++;
  return r;
}

gadget_rect_t ui_rect_in_circle(int16_t d, int16_t y0, int16_t y1, int16_t margin) {
  int32_t r = d / 2;
  int32_t far0 = y0 - r < 0 ? r - y0 : y0 - r;
  int32_t far1 = y1 - r < 0 ? r - y1 : y1 - r;
  int32_t far = far0 > far1 ? far0 : far1;
  int32_t half = isqrt32(r * r - far * far) - margin;
  if (half < 0) half = 0;
  gadget_rect_t out = {(int16_t)(r - half), y0, (int16_t)(2 * half), (int16_t)(y1 - y0 + 1)};
  return out;
}

void ui_metrics_for(const gadget_board_t *board, uint16_t maus_w, uint16_t maus_h, ui_metrics_t *m) {
  const int16_t w = (int16_t)board->screen_w;
  const int16_t h = (int16_t)board->screen_h;
  *m = (ui_metrics_t){0};
  m->w = w;
  m->h = h;
  m->round = board->screen_round;
  m->landscape = w > h;
  m->large = w >= 400;
  m->safe = ui_safe_area(board);
  if (m->large) {
    m->font_title = &font_latin1_28;
    m->font_body = &font_latin1_24;
    m->font_small = &font_latin1_20;
    m->font_tiny = &font_latin1_16;
    m->font_big = &font_latin1_40;
    m->pad = 10;
  } else {
    m->font_title = &font_latin1_20;
    m->font_body = &font_latin1_16;
    m->font_small = &font_latin1_14;
    m->font_tiny = &font_latin1_14;
    m->font_big = &font_latin1_28;
    m->pad = 6;
  }
  const int16_t mw = (int16_t)maus_w;
  const int16_t mh = (int16_t)maus_h;
  if (m->landscape) {
    /* Maus on the left, text column on the right. */
    m->maus = (gadget_rect_t){16, (int16_t)((h - mh) / 2), mw, mh};
    int16_t cx = (int16_t)(m->maus.x + mw + 12);
    m->caption = (gadget_rect_t){cx, 12, (int16_t)(w - cx - 8), (int16_t)(h - 24)};
    m->ring_d = (int16_t)(mh + 8);
    m->toast = (gadget_rect_t){8, (int16_t)(h - 8 - 60), (int16_t)(w - 16), 60};
    /* full-width line under everything: a device id does not fit the text column */
    int16_t sh = (int16_t)lv_font_get_line_height(m->font_small);
    m->status = (gadget_rect_t){8, (int16_t)(h - 4 - sh), (int16_t)(w - 16), sh};
    m->caption.h = (int16_t)(m->status.y - 4 - m->caption.y);
  } else if (m->round) {
    /* Maus near the top, caption in the circle below it. */
    m->maus = (gadget_rect_t){(int16_t)((w - mw) / 2), 30, mw, mh};
    int16_t y0 = (int16_t)(m->maus.y + mh + 8);
    m->caption = ui_rect_in_circle(w, y0, (int16_t)(h - 50), 12);
    m->ring_d = (int16_t)(mh + 24);
    m->toast = ui_rect_in_circle(w, (int16_t)(h - 140), (int16_t)(h - 46), 10);
    int16_t sh = (int16_t)lv_font_get_line_height(m->font_small);
    m->status = (gadget_rect_t){m->caption.x, (int16_t)(m->caption.y + m->caption.h - sh), m->caption.w, sh};
  } else {
    /* Square: Maus on top, caption underneath. */
    m->maus = (gadget_rect_t){(int16_t)((w - mw) / 2), 8, mw, mh};
    int16_t y0 = (int16_t)(m->maus.y + mh + 6);
    m->caption = (gadget_rect_t){8, y0, (int16_t)(w - 16), (int16_t)(h - y0 - 6)};
    m->ring_d = (int16_t)(mh + 16);
    m->toast = (gadget_rect_t){8, (int16_t)(h - 8 - 60), (int16_t)(w - 16), 60};
    int16_t sh = (int16_t)lv_font_get_line_height(m->font_small);
    m->status = (gadget_rect_t){m->caption.x, (int16_t)(m->caption.y + m->caption.h - sh), m->caption.w, sh};
  }
}
```

- [ ] **Step 4: Write the pager**

`firmware/ui/ui_pager.h`:

```c
/* SPDX-License-Identifier: Apache-2.0 */
/* A caption that shows whole lines only: a clipping box holding one wrapped
 * label, moved up a page at a time, plus up to two optional one-line labels
 * stacked on the bottom of the status area (status at the bottom, sub above
 * it). Private to firmware/ui. */
#ifndef UI_PAGER_H
#define UI_PAGER_H

#include "lvgl.h"
#include "gadget_types.h"

#define UI_PAGE_ROTATE_MS 4000u /* Setup/Offline copy that does not fit turns its page this often */
#define UI_MS_PER_LINE 2000u    /* speaking: page time per line while the stream's length is unknown */

typedef struct {
  lv_obj_t *box;
  lv_obj_t *label;
  lv_obj_t *sub;    /* one line above the status (the host name on Setup and Offline) */
  lv_obj_t *status;
  gadget_rect_t area;
  gadget_rect_t status_area; /* status and sub sit on its bottom edge */
  bool center_v;           /* centre text that fits (landscape layout) */
  int32_t line_h;
  int32_t lines_per_page;
  int32_t total_lines;
  int32_t text_h;
  uint16_t page;
} ui_pager_t;

void ui_pager_create(ui_pager_t *p, lv_obj_t *parent, gadget_rect_t area, gadget_rect_t status_area, bool center_v);
/* sub and status may each be NULL or "" (no line). Both are one line in
 * status_font and status_color: status on the bottom edge of status_area,
 * sub just above it (or on that edge without a status). The text gets the
 * whole area, or stops 4 px above the topmost line where they overlap. */
void ui_pager_set(ui_pager_t *p, const char *text, const lv_font_t *font, uint32_t color, lv_text_align_t align,
                  const char *sub, const char *status, const lv_font_t *status_font, uint32_t status_color);
uint16_t ui_pager_pages(const ui_pager_t *p);
void ui_pager_show_page(ui_pager_t *p, uint16_t page);
void ui_pager_show_rotating(ui_pager_t *p, uint64_t now_ms);
void ui_pager_show_tail(ui_pager_t *p);
/* total_ms 0 = unknown: UI_MS_PER_LINE per line of a page. */
void ui_pager_show_progress(ui_pager_t *p, uint32_t elapsed_ms, uint32_t total_ms);
void ui_pager_set_hidden(ui_pager_t *p, bool hidden);

#endif /* UI_PAGER_H */
```

`firmware/ui/ui_pager.c`:

```c
/* SPDX-License-Identifier: Apache-2.0 */
#include "ui_pager.h"

#include <string.h>
#include "ui_lv_compat.h"

static lv_obj_t *one_line(lv_obj_t *parent, int32_t w) {
  lv_obj_t *l = lv_label_create(parent);
  lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_DOTS);
  lv_obj_set_width(l, w);
  lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
  ui_set_hidden(l, true);
  return l;
}

void ui_pager_create(ui_pager_t *p, lv_obj_t *parent, gadget_rect_t area, gadget_rect_t status_area, bool center_v) {
  memset(p, 0, sizeof *p);
  p->area = area;
  p->status_area = status_area;
  p->center_v = center_v;
  p->box = ui_box(parent);
  lv_obj_set_pos(p->box, area.x, area.y);
  lv_obj_set_size(p->box, area.w, area.h);
  p->label = lv_label_create(p->box);
  lv_label_set_long_mode(p->label, LV_LABEL_LONG_MODE_WRAP);
  lv_obj_set_width(p->label, area.w);
  lv_obj_set_pos(p->label, 0, 0);
  p->sub = one_line(parent, status_area.w);
  p->status = one_line(parent, status_area.w);
}

/* Shows `text` (or hides the line when it is NULL or "") with its top at y. */
static bool set_line(lv_obj_t *l, const char *text, int32_t x, int32_t y, int32_t h, const lv_font_t *font,
                     uint32_t color) {
  bool on = text && text[0];
  ui_set_hidden(l, !on);
  if (!on) return false;
  lv_obj_set_style_text_font(l, font, 0);
  lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
  lv_obj_set_height(l, h);
  lv_obj_set_pos(l, x, y);
  lv_label_set_text(l, text);
  return true;
}

void ui_pager_set(ui_pager_t *p, const char *text, const lv_font_t *font, uint32_t color, lv_text_align_t align,
                  const char *sub, const char *status, const lv_font_t *status_font, uint32_t status_color) {
  int32_t text_area_h = p->area.h;
  int32_t sh = status_font ? lv_font_get_line_height(status_font) : 0;
  int32_t top = p->status_area.y + p->status_area.h; /* lines stack up from the bottom edge */
  if (set_line(p->status, status, p->status_area.x, top - sh, sh, status_font, status_color)) top -= sh;
  if (set_line(p->sub, sub, p->status_area.x, top - sh, sh, status_font, status_color)) top -= sh;
  if (top < p->area.y + p->area.h) text_area_h = top - 4 - p->area.y;

  lv_obj_set_style_text_font(p->label, font, 0);
  lv_obj_set_style_text_color(p->label, lv_color_hex(color), 0);
  lv_obj_set_style_text_align(p->label, align, 0);
  lv_label_set_text(p->label, text);

  lv_point_t size;
  lv_text_get_size(&size, text, font, 0, 0, p->area.w, LV_TEXT_FLAG_NONE);
  p->line_h = lv_font_get_line_height(font);
  p->text_h = size.y;
  p->total_lines = (size.y + p->line_h - 1) / p->line_h;
  if (p->total_lines < 1) p->total_lines = 1;
  p->lines_per_page = text_area_h / p->line_h;
  if (p->lines_per_page < 1) p->lines_per_page = 1;
  lv_obj_set_height(p->box, p->lines_per_page * p->line_h);
  p->page = UINT16_MAX;
  ui_pager_show_page(p, 0);
}

uint16_t ui_pager_pages(const ui_pager_t *p) {
  return (uint16_t)((p->total_lines + p->lines_per_page - 1) / p->lines_per_page);
}

static void place(ui_pager_t *p, int32_t first_line) {
  int32_t box_h = p->lines_per_page * p->line_h;
  int32_t y = -first_line * p->line_h;
  if (p->total_lines <= p->lines_per_page && p->center_v) y = (box_h - p->text_h) / 2;
  lv_obj_set_y(p->label, y);
}

void ui_pager_show_page(ui_pager_t *p, uint16_t page) {
  uint16_t n = ui_pager_pages(p);
  if (page >= n) page = (uint16_t)(n - 1);
  if (page == p->page) return;
  p->page = page;
  place(p, (int32_t)page * p->lines_per_page);
}

void ui_pager_show_rotating(ui_pager_t *p, uint64_t now_ms) {
  uint16_t n = ui_pager_pages(p);
  ui_pager_show_page(p, (uint16_t)((now_ms / UI_PAGE_ROTATE_MS) % n));
}

void ui_pager_show_tail(ui_pager_t *p) {
  int32_t first = p->total_lines - p->lines_per_page;
  p->page = UINT16_MAX; /* the tail is not on a page boundary */
  place(p, first > 0 ? first : 0);
}

void ui_pager_show_progress(ui_pager_t *p, uint32_t elapsed_ms, uint32_t total_ms) {
  uint32_t n = ui_pager_pages(p);
  uint32_t page;
  if (total_ms > 0) page = (uint32_t)((uint64_t)elapsed_ms * n / total_ms);
  else page = elapsed_ms / ((uint32_t)p->lines_per_page * UI_MS_PER_LINE);
  ui_pager_show_page(p, (uint16_t)(page >= n ? n - 1 : page));
}

void ui_pager_set_hidden(ui_pager_t *p, bool hidden) {
  ui_set_hidden(p->box, hidden);
  if (hidden) {
    ui_set_hidden(p->sub, true);
    ui_set_hidden(p->status, true);
  }
}
```

- [ ] **Step 5: Write the Maus widget**

`firmware/ui/ui_maus.h`:

```c
/* SPDX-License-Identifier: Apache-2.0 */
/* The Maus widget: three stacked images (body, eyes, mouth) in one
 * container that moves by whole pixels. Private to firmware/ui. */
#ifndef UI_MAUS_H
#define UI_MAUS_H

#include "ui_maus_engine.h"

typedef struct {
  maus_engine_t engine;
  lv_obj_t *box;
  lv_obj_t *body;
  lv_obj_t *eyes;
  lv_obj_t *mouth;
  int16_t base_x, base_y;
  const void *eyes_src;   /* last image set, to skip redundant updates */
  const void *mouth_src;
  int16_t x, y;
} ui_maus_t;

void ui_maus_create(ui_maus_t *w, lv_obj_t *parent, const maus_art_t *art, uint32_t seed, int16_t x, int16_t y);
/* Hide (UI_MAUS_NONE) or show a state; the engine keeps its own timing. */
void ui_maus_set_state(ui_maus_t *w, ui_maus_state_t state, uint64_t now_ms);
/* Advance the animation and move the images (call from ui_tick). */
void ui_maus_tick(ui_maus_t *w, uint64_t now_ms, uint8_t speak_level);

#endif /* UI_MAUS_H */
```

`firmware/ui/ui_maus.c`:

```c
/* SPDX-License-Identifier: Apache-2.0 */
#include "ui_maus.h"

#include "ui_lv_compat.h"

static lv_obj_t *layer_image(lv_obj_t *parent) {
  lv_obj_t *img = lv_image_create(parent);
  ui_set_clickable(img, false);
  return img;
}

static void set_layer(lv_obj_t *img, const maus_layer_t *layer, const void **last) {
  if (*last != layer->img) {
    lv_image_set_src(img, layer->img);
    *last = layer->img;
  }
  lv_obj_set_pos(img, layer->x, layer->y);
}

void ui_maus_create(ui_maus_t *w, lv_obj_t *parent, const maus_art_t *art, uint32_t seed, int16_t x, int16_t y) {
  maus_engine_init(&w->engine, art, seed);
  w->box = ui_box(parent);
  lv_obj_set_size(w->box, art->w, art->h);
  w->base_x = x;
  w->base_y = y;
  w->x = x;
  w->y = y;
  lv_obj_set_pos(w->box, x, y);
  w->body = layer_image(w->box);
  lv_image_set_src(w->body, art->body);
  lv_obj_set_pos(w->body, 0, 0);
  w->eyes = layer_image(w->box);
  /* A8 eye frames draw in the recolor colour (contract 2.15). recolor_opa stays
   * 0, so RGB565A8 eyes (the white-eye fallback) keep LVGL's fast path. The
   * body and mouth layers never set recolor. */
  lv_obj_set_style_image_recolor(w->eyes, lv_color_white(), 0);
  w->mouth = layer_image(w->box);
  w->eyes_src = NULL;
  w->mouth_src = NULL;
  ui_set_hidden(w->box, true);
}

void ui_maus_set_state(ui_maus_t *w, ui_maus_state_t state, uint64_t now_ms) {
  if ((unsigned)state >= UI_MAUS__COUNT) state = UI_MAUS_NONE; /* never index past the art's state table */
  maus_engine_set_state(&w->engine, state, now_ms);
  ui_set_hidden(w->box, state == UI_MAUS_NONE);
}

void ui_maus_tick(ui_maus_t *w, uint64_t now_ms, uint8_t speak_level) {
  if (w->engine.state == UI_MAUS_NONE) return;
  const maus_art_t *art = w->engine.art;
  maus_frame_t f = maus_engine_step(&w->engine, now_ms, speak_level);
  set_layer(w->eyes, &art->eyes[f.expr][f.blink_step], &w->eyes_src);
  if (f.speak >= 0) set_layer(w->mouth, &art->speak[f.speak][f.speak_level - 1], &w->mouth_src);
  else set_layer(w->mouth, &art->mouth[f.expr], &w->mouth_src);
  int16_t x = (int16_t)(w->base_x + f.dx);
  int16_t y = (int16_t)(w->base_y + f.dy);
  if (x != w->x || y != w->y) {
    lv_obj_set_pos(w->box, x, y);
    w->x = x;
    w->y = y;
  }
}
```

- [ ] **Step 6: Write the shared state, the screens and the API**

`firmware/ui/ui_priv.h`:

```c
/* SPDX-License-Identifier: Apache-2.0 */
/* State shared by ui.c and ui_screens.c. Private to firmware/ui. */
#ifndef UI_PRIV_H
#define UI_PRIV_H

#include "gadget_ui.h"
#include "ui_lv_requirements.h"
#include "ui_copy.h"
#include "ui_maus.h"
#include "ui_pager.h"
#include "ui_theme.h"

typedef struct {
  const gadget_board_t *board;
  const maus_art_t *art;
  ui_metrics_t mt;
  lv_obj_t *root;

  /* Maus screens */
  ui_maus_t maus;
  lv_obj_t *ring;
  ui_pager_t caption;
  lv_obj_t *countdown;

  /* text screens: ask, card, update, image */
  lv_obj_t *title;
  lv_obj_t *body;
  lv_obj_t *note;
  lv_obj_t *opt[UI_ASK_OPTIONS_MAX];
  lv_obj_t *opt_label[UI_ASK_OPTIONS_MAX];
  lv_obj_t *opt_hint[UI_ASK_OPTIONS_MAX];
  lv_obj_t *bar;
  lv_obj_t *image;
  lv_image_dsc_t image_dsc[2]; /* alternated so a new picture is a new image source */
  uint8_t image_slot;
  uint32_t image_rev;

  /* on every screen */
  lv_obj_t *battery;
  lv_obj_t *toast;
  lv_obj_t *toast_name;
  lv_obj_t *toast_text;

  /* render cache */
  bool rendered;
  uint32_t rev;
  uint32_t applies;   /* ui_screens_apply() calls, for the render-cache test */
  ui_screen_t screen;
  uint8_t speak_level;
  int16_t mic_level;  /* -1 = ring width not drawn yet */
  int16_t ask_locked; /* -1 unknown, 0 unlocked, 1 locked */
  char status[UI_COPY_MAX]; /* last status text drawn (retry countdown) */
} ui_state_t;

extern ui_state_t g_ui;

/* ui_screens.c */
void ui_screens_create(void);
void ui_screens_apply(const ui_model_t *m);     /* the model changed (rev) */
void ui_screens_apply_time(const ui_model_t *m); /* every render: time-driven parts */

#endif /* UI_PRIV_H */
```

`firmware/ui/ui_screens.c`:

```c
/* SPDX-License-Identifier: Apache-2.0 */
/* Every screen of spec 5.5, drawn from the model. Objects are created once
 * in ui_screens_create() and shown, hidden and filled per render. */
#include <stdio.h>
#include <string.h>

#include "ui_copy.h"
#include "ui_lv_compat.h"
#include "ui_priv.h"

#define BATTERY_ARC_DEG 30 /* a short arc centred on the top edge of the screen */

static lv_obj_t *label(lv_obj_t *parent, const lv_font_t *font, uint32_t color, lv_label_long_mode_t mode) {
  lv_obj_t *l = lv_label_create(parent);
  ui_set_clickable(l, false);
  lv_label_set_long_mode(l, mode);
  lv_obj_set_style_text_font(l, font, 0);
  lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
  return l;
}

static int32_t line_h(const lv_font_t *f) { return lv_font_get_line_height(f); }

/* Height of `text` wrapped at width w, capped to max_lines lines. */
static int32_t text_h(const char *text, const lv_font_t *f, int32_t w, int32_t max_lines) {
  lv_point_t s;
  lv_text_get_size(&s, text, f, 0, 0, w, LV_TEXT_FLAG_NONE);
  int32_t cap = max_lines * line_h(f);
  return s.y < cap ? s.y : cap;
}

/* Spec 5.5: a battery arc at the top edge, on every board with a battery. It
 * is a slice of a circle of the screen's width, so on round boards it follows
 * the glass. */
static void create_battery(void) {
  ui_metrics_t *mt = &g_ui.mt;
  lv_obj_t *a = lv_arc_create(g_ui.root);
  lv_obj_remove_style(a, NULL, LV_PART_KNOB);
  ui_set_clickable(a, false);
  lv_obj_set_size(a, mt->w - 8, mt->w - 8);
  lv_obj_set_pos(a, 4, 4);
  lv_arc_set_bg_angles(a, 270 - BATTERY_ARC_DEG / 2, 270 + BATTERY_ARC_DEG / 2);
  lv_arc_set_range(a, 0, 100);
  lv_obj_set_style_arc_width(a, 4, LV_PART_MAIN);
  lv_obj_set_style_arc_width(a, 4, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(a, lv_color_hex(UI_COLOR_DIM), LV_PART_MAIN);
  lv_obj_set_style_arc_opa(a, LV_OPA_50, LV_PART_MAIN);
  lv_obj_set_style_arc_rounded(a, true, LV_PART_MAIN);
  lv_obj_set_style_arc_rounded(a, true, LV_PART_INDICATOR);
  g_ui.battery = a;
  ui_set_hidden(a, true);
}

static void create_toast(void) {
  ui_metrics_t *mt = &g_ui.mt;
  lv_obj_t *t = ui_box(g_ui.root);
  lv_obj_set_pos(t, mt->toast.x, mt->toast.y);
  lv_obj_set_size(t, mt->toast.w, mt->toast.h);
  lv_obj_set_style_radius(t, 14, 0);
  lv_obj_set_style_bg_color(t, lv_color_hex(UI_COLOR_DIM), 0);
  lv_obj_set_style_bg_opa(t, LV_OPA_40, 0);
  lv_obj_set_style_pad_all(t, 6, 0);
  g_ui.toast_name = label(t, mt->font_tiny, UI_COLOR_ACCENT, LV_LABEL_LONG_MODE_DOTS);
  lv_obj_set_size(g_ui.toast_name, mt->toast.w - 12, line_h(mt->font_tiny));
  lv_obj_set_pos(g_ui.toast_name, 0, 0);
  lv_obj_set_style_text_align(g_ui.toast_name, LV_TEXT_ALIGN_CENTER, 0);
  g_ui.toast_text = label(t, mt->font_small, UI_COLOR_INK, LV_LABEL_LONG_MODE_DOTS);
  int32_t th = mt->toast.h - 12 - line_h(mt->font_tiny);
  th -= th % line_h(mt->font_small);
  lv_obj_set_size(g_ui.toast_text, mt->toast.w - 12, th);
  lv_obj_set_pos(g_ui.toast_text, 0, line_h(mt->font_tiny));
  lv_obj_set_style_text_align(g_ui.toast_text, LV_TEXT_ALIGN_CENTER, 0);
  g_ui.toast = t;
  ui_set_hidden(t, true);
}

void ui_screens_create(void) {
  ui_metrics_t *mt = &g_ui.mt;

  /* listening ring, centred on the Maus body */
  lv_obj_t *r = lv_arc_create(g_ui.root);
  lv_obj_remove_style(r, NULL, LV_PART_KNOB);
  ui_set_clickable(r, false);
  lv_obj_set_size(r, mt->ring_d, mt->ring_d);
  lv_obj_set_pos(r, mt->maus.x + mt->maus.w / 2 - mt->ring_d / 2, mt->maus.y + mt->maus.h / 2 - mt->ring_d / 2);
  lv_arc_set_bg_angles(r, 0, 360);
  lv_arc_set_angles(r, 0, 360);
  lv_obj_set_style_arc_opa(r, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_arc_color(r, lv_color_hex(UI_COLOR_ACCENT), LV_PART_INDICATOR);
  g_ui.ring = r;
  ui_set_hidden(r, true);
  lv_obj_move_to_index(r, 0); /* behind the Maus */

  ui_pager_create(&g_ui.caption, g_ui.root, mt->caption, mt->status, mt->landscape);
  ui_pager_set_hidden(&g_ui.caption, true);

  g_ui.countdown = label(g_ui.root, mt->font_big, UI_COLOR_ACCENT, LV_LABEL_LONG_MODE_CLIP);
  lv_obj_set_size(g_ui.countdown, mt->caption.w, line_h(mt->font_big));
  lv_obj_set_pos(g_ui.countdown, mt->caption.x, mt->caption.y + (mt->caption.h - line_h(mt->font_big)) / 2);
  lv_obj_set_style_text_align(g_ui.countdown, LV_TEXT_ALIGN_CENTER, 0);
  ui_set_hidden(g_ui.countdown, true);

  g_ui.title = label(g_ui.root, mt->font_title, UI_COLOR_INK, LV_LABEL_LONG_MODE_DOTS);
  g_ui.body = label(g_ui.root, mt->font_small, UI_COLOR_MUTE, LV_LABEL_LONG_MODE_DOTS);
  g_ui.note = label(g_ui.root, mt->font_small, UI_COLOR_MUTE, LV_LABEL_LONG_MODE_WRAP);
  lv_obj_set_style_text_align(g_ui.title, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_align(g_ui.body, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_align(g_ui.note, LV_TEXT_ALIGN_CENTER, 0);
  for (int i = 0; i < UI_ASK_OPTIONS_MAX; i++) {
    lv_obj_t *o = ui_box(g_ui.root);
    lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_color(o, lv_color_hex(UI_COLOR_MUTE), 0);
    g_ui.opt[i] = o;
    g_ui.opt_label[i] = label(o, mt->font_body, UI_COLOR_INK, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_style_text_align(g_ui.opt_label[i], LV_TEXT_ALIGN_CENTER, 0);
    g_ui.opt_hint[i] = label(g_ui.root, mt->font_tiny, UI_COLOR_DIM, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_style_text_align(g_ui.opt_hint[i], LV_TEXT_ALIGN_CENTER, 0);
  }
  g_ui.bar = lv_bar_create(g_ui.root);
  ui_set_clickable(g_ui.bar, false);
  lv_bar_set_range(g_ui.bar, 0, 100);
  lv_obj_set_style_bg_color(g_ui.bar, lv_color_hex(UI_COLOR_DIM), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(g_ui.bar, LV_OPA_50, LV_PART_MAIN);
  lv_obj_set_style_bg_color(g_ui.bar, lv_color_hex(UI_COLOR_ACCENT), LV_PART_INDICATOR);
  g_ui.image = lv_image_create(g_ui.root);
  ui_set_clickable(g_ui.image, false);

  create_battery();
  create_toast();
}

static void hide_all(void) {
  ui_pager_set_hidden(&g_ui.caption, true);
  ui_set_hidden(g_ui.ring, true);
  ui_set_hidden(g_ui.countdown, true);
  ui_set_hidden(g_ui.title, true);
  ui_set_hidden(g_ui.body, true);
  ui_set_hidden(g_ui.note, true);
  for (int i = 0; i < UI_ASK_OPTIONS_MAX; i++) {
    ui_set_hidden(g_ui.opt[i], true);
    ui_set_hidden(g_ui.opt_hint[i], true);
  }
  ui_set_hidden(g_ui.bar, true);
  ui_set_hidden(g_ui.image, true);
}

static void show_caption(const char *text, const lv_font_t *f, uint32_t color, lv_text_align_t align,
                         const char *sub, const char *status, const lv_font_t *sf, uint32_t scolor) {
  ui_pager_set(&g_ui.caption, text, f, color, align, sub, status, sf, scolor);
  ui_pager_set_hidden(&g_ui.caption, false);
}

/* Title at the top of the safe area (≤ 2 lines); returns its bottom edge. */
static int32_t place_title(const char *text) {
  const gadget_rect_t s = g_ui.mt.safe;
  const lv_font_t *f = g_ui.mt.font_title;
  lv_label_set_text(g_ui.title, text);
  lv_obj_set_pos(g_ui.title, s.x, s.y);
  lv_obj_set_size(g_ui.title, s.w, text_h(text, f, s.w, 2));
  ui_set_hidden(g_ui.title, false);
  return s.y + text_h(text, f, s.w, 2);
}

/* Body between y0 and y1, whole lines only. */
static void place_body(const char *text, int32_t y0, int32_t y1) {
  const gadget_rect_t s = g_ui.mt.safe;
  const lv_font_t *f = g_ui.mt.font_small;
  int32_t lh = line_h(f);
  int32_t lines = (y1 - y0) / lh;
  if (!text[0] || lines < 1) return;
  int32_t h = text_h(text, f, s.w, lines);
  lv_label_set_text(g_ui.body, text);
  lv_obj_set_pos(g_ui.body, s.x, y0);
  lv_obj_set_size(g_ui.body, s.w, h);
  ui_set_hidden(g_ui.body, false);
}

static void place_note(const char *text) {
  const gadget_rect_t s = g_ui.mt.safe;
  int32_t h = text_h(text, g_ui.mt.font_small, s.w, 3);
  lv_label_set_text(g_ui.note, text);
  lv_obj_set_width(g_ui.note, s.w);
  lv_obj_set_pos(g_ui.note, s.x, s.y + s.h - h);
  ui_set_hidden(g_ui.note, false);
}

static void style_option(int i, const ui_ask_option_t *o, gadget_rect_t r) {
  lv_obj_t *b = g_ui.opt[i];
  lv_obj_t *l = g_ui.opt_label[i];
  lv_obj_set_pos(b, r.x, r.y);
  lv_obj_set_size(b, r.w, r.h);
  uint32_t bg = o->style == UI_STYLE_ALLOW ? UI_COLOR_OK : o->style == UI_STYLE_DENY ? UI_COLOR_BAD : UI_COLOR_BG;
  lv_obj_set_style_bg_color(b, lv_color_hex(bg), 0);
  lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(b, o->style == UI_STYLE_NEUTRAL ? 2 : 0, 0);
  lv_obj_set_style_text_color(l, lv_color_hex(o->style == UI_STYLE_NEUTRAL ? UI_COLOR_INK : UI_COLOR_BG), 0);
  lv_label_set_text(l, o->label);
  int32_t lh = line_h(g_ui.mt.font_body);
  lv_obj_set_size(l, r.w - 16, lh);
  lv_obj_set_pos(l, 8, (r.h - lh) / 2);
  ui_set_hidden(b, false);
}

/* Returns the top edge of the option row(s), or the safe-area bottom. */
static int32_t place_options(const ui_model_t *m) {
  const gadget_rect_t s = g_ui.mt.safe;
  if (!m->ask.answerable || m->ask.n_options == 0) {
    place_note(UI_COPY_ASK_ELSEWHERE);
    return s.y + s.h - text_h(UI_COPY_ASK_ELSEWHERE, g_ui.mt.font_small, s.w, 3);
  }
  int32_t top = s.y + s.h;
  bool touch = (g_ui.board->input_mask & GADGET_INPUT_TOUCH) != 0;
  if (touch) {
    for (int i = 0; i < m->ask.n_options && i < UI_ASK_OPTIONS_MAX; i++) {
      gadget_rect_t r = m->ask.options[i].rect;
      style_option(i, &m->ask.options[i], r);
      if (r.y < top) top = r.y;
    }
    return top;
  }
  /* Button boards: TALK answers option 1, CANCEL option 2 (spec 5.4). */
  static const char *const hints[2] = {"TALK", "CANCEL"};
  int n = m->ask.n_options > 2 ? 2 : m->ask.n_options;
  int32_t hint_h = line_h(g_ui.mt.font_tiny);
  int32_t pill_h = line_h(g_ui.mt.font_body) + 12;
  int32_t y = s.y + s.h - hint_h - 2 - pill_h;
  int32_t gap = g_ui.mt.pad;
  int32_t w = n == 1 ? s.w : (s.w - gap) / 2;
  for (int i = 0; i < n; i++) {
    gadget_rect_t r = {(int16_t)(s.x + i * (w + gap)), (int16_t)y, (int16_t)w, (int16_t)pill_h};
    style_option(i, &m->ask.options[i], r);
    lv_label_set_text(g_ui.opt_hint[i], hints[i]);
    lv_obj_set_size(g_ui.opt_hint[i], w, hint_h);
    lv_obj_set_pos(g_ui.opt_hint[i], r.x, y + pill_h + 2);
    ui_set_hidden(g_ui.opt_hint[i], false);
  }
  return y;
}

static void apply_ask(const ui_model_t *m) {
  int32_t pad = g_ui.mt.pad;
  int32_t y = place_title(m->ask.title) + pad;
  int32_t bottom = place_options(m) - pad;
  place_body(m->ask.body, y, bottom);
  g_ui.ask_locked = -1;
}

static void apply_card(const ui_model_t *m) {
  const gadget_rect_t s = g_ui.mt.safe;
  int32_t y = place_title(m->card.title) + g_ui.mt.pad;
  place_body(m->card.body, y, s.y + s.h);
}

static void apply_update(const ui_model_t *m) {
  const gadget_rect_t s = g_ui.mt.safe;
  char text[64];
  ui_copy_update(m, text, sizeof text);
  int32_t th = line_h(g_ui.mt.font_title);
  int32_t bar_h = g_ui.mt.large ? 12 : 8;
  int32_t y = s.y + (s.h - th - g_ui.mt.pad - bar_h) / 2;
  lv_label_set_text(g_ui.title, text);
  lv_obj_set_size(g_ui.title, s.w, th);
  lv_obj_set_pos(g_ui.title, s.x, y);
  ui_set_hidden(g_ui.title, false);
  lv_obj_set_size(g_ui.bar, s.w - 2 * g_ui.mt.pad, bar_h);
  lv_obj_set_pos(g_ui.bar, s.x + g_ui.mt.pad, y + th + g_ui.mt.pad);
  lv_bar_set_value(g_ui.bar, m->update.phase == UI_UPDATE_RECEIVING ? m->update.pct : 100, LV_ANIM_OFF);
  ui_set_hidden(g_ui.bar, false);
}

static void apply_image(const ui_model_t *m) {
  if (!m->image.pixels || m->image.w == 0 || m->image.h == 0) return;
  if (m->image.pixels_rev != g_ui.image_rev || lv_image_get_src(g_ui.image) == NULL) {
    g_ui.image_slot ^= 1u;
    lv_image_dsc_t *d = &g_ui.image_dsc[g_ui.image_slot];
    /* A slot is reused every second picture, maybe with another size. LVGL
     * keys its image and header caches by this pointer, so
     * ui_lv_requirements.h requires both caches off. */
    memset(d, 0, sizeof *d);
    d->header.magic = LV_IMAGE_HEADER_MAGIC;
    d->header.cf = LV_COLOR_FORMAT_RGB565;
    d->header.w = m->image.w;
    d->header.h = m->image.h;
    d->header.stride = (uint32_t)m->image.w * 2u;
    d->data_size = (uint32_t)m->image.w * m->image.h * 2u;
    d->data = (const uint8_t *)m->image.pixels;
    lv_image_set_src(g_ui.image, d);
    g_ui.image_rev = m->image.pixels_rev;
  }
  lv_obj_set_pos(g_ui.image, (g_ui.mt.w - m->image.w) / 2, (g_ui.mt.h - m->image.h) / 2);
  ui_set_hidden(g_ui.image, false);
}

static void apply_battery(const ui_model_t *m) {
  if (!g_ui.board->has_battery || !m->battery.present) {
    ui_set_hidden(g_ui.battery, true);
    return;
  }
  uint8_t pct = m->battery.pct > 100 ? 100 : m->battery.pct;
  uint32_t c = m->battery.charging ? UI_COLOR_ACCENT : pct <= 10 ? UI_COLOR_BAD : pct <= 25 ? UI_COLOR_WARN : UI_COLOR_OK;
  lv_arc_set_value(g_ui.battery, pct);
  lv_obj_set_style_arc_color(g_ui.battery, lv_color_hex(c), LV_PART_INDICATOR);
  ui_set_hidden(g_ui.battery, false);
}

static void apply_toast(const ui_model_t *m) {
  if (!m->toast.visible) {
    ui_set_hidden(g_ui.toast, true);
    return;
  }
  lv_label_set_text(g_ui.toast_name, m->toast.bot_name);
  lv_label_set_text(g_ui.toast_text, m->toast.text);
  ui_set_hidden(g_ui.toast, false);
}

void ui_screens_apply(const ui_model_t *m) {
  ui_metrics_t *mt = &g_ui.mt;
  ui_copy_t copy;
  char line[128];
  hide_all();
  g_ui.applies++;
  g_ui.screen = m->screen;
  g_ui.status[0] = '\0';
  g_ui.mic_level = -1;
  switch (m->screen) {
  case UI_SCREEN_BOOT:
  case UI_SCREEN__COUNT:
    break;
  case UI_SCREEN_SETUP:
    ui_copy_setup(m, &copy);
    show_caption(copy.caption, mt->font_small, UI_COLOR_INK, LV_TEXT_ALIGN_CENTER, copy.host, copy.status, mt->font_tiny,
                 UI_COLOR_DIM);
    break;
  case UI_SCREEN_OFFLINE:
    ui_copy_offline(m, &copy);
    show_caption(copy.caption, mt->font_small, UI_COLOR_INK, LV_TEXT_ALIGN_CENTER, copy.host, copy.status, mt->font_tiny,
                 UI_COLOR_MUTE);
    snprintf(g_ui.status, sizeof g_ui.status, "%s", copy.status);
    break;
  case UI_SCREEN_IDLE:
    ui_copy_idle(m, line, sizeof line);
    show_caption(line, mt->font_title, UI_COLOR_INK, LV_TEXT_ALIGN_CENTER, NULL, NULL, NULL, 0);
    break;
  case UI_SCREEN_LISTENING:
    ui_set_hidden(g_ui.ring, false);
    break;
  case UI_SCREEN_THINKING:
    show_caption(m->thinking.heard, mt->font_body, UI_COLOR_INK, LV_TEXT_ALIGN_CENTER, NULL,
                 m->thinking.working, mt->font_small, UI_COLOR_ACCENT);
    ui_pager_show_tail(&g_ui.caption);
    break;
  case UI_SCREEN_SPEAKING:
    show_caption(m->reply.text, mt->font_body, UI_COLOR_INK, LV_TEXT_ALIGN_LEFT, NULL, NULL, NULL, 0);
    break;
  case UI_SCREEN_REPLY:
    if (m->reply.failed) {
      show_caption(m->reply.reason, mt->font_body, UI_COLOR_BAD, LV_TEXT_ALIGN_CENTER, NULL, NULL, NULL, 0);
    } else {
      show_caption(m->reply.text, mt->font_body, UI_COLOR_INK, LV_TEXT_ALIGN_LEFT, NULL, NULL, NULL, 0);
      ui_pager_show_tail(&g_ui.caption);
    }
    break;
  case UI_SCREEN_ASK:
    apply_ask(m);
    break;
  case UI_SCREEN_CARD:
    apply_card(m);
    break;
  case UI_SCREEN_IMAGE:
    apply_image(m);
    break;
  case UI_SCREEN_UPDATE:
    apply_update(m);
    break;
  }
  apply_battery(m);
  apply_toast(m);
}

void ui_screens_apply_time(const ui_model_t *m) {
  switch (g_ui.screen) {
  case UI_SCREEN_SETUP:
    ui_pager_show_rotating(&g_ui.caption, m->now_ms);
    break;
  case UI_SCREEN_OFFLINE: {
    ui_copy_t copy;
    ui_copy_offline(m, &copy);
    if (strcmp(copy.status, g_ui.status) != 0 && copy.status[0] && g_ui.status[0]) {
      lv_label_set_text(g_ui.caption.status, copy.status);
      snprintf(g_ui.status, sizeof g_ui.status, "%s", copy.status);
    }
    ui_pager_show_rotating(&g_ui.caption, m->now_ms);
    break;
  }
  case UI_SCREEN_LISTENING: {
    char digit[4];
    ui_copy_countdown(m, digit, sizeof digit);
    ui_set_hidden(g_ui.countdown, digit[0] == '\0');
    if (digit[0] && strcmp(lv_label_get_text(g_ui.countdown), digit) != 0) lv_label_set_text(g_ui.countdown, digit);
    if ((int16_t)m->mic_level != g_ui.mic_level) {
      int32_t w = (g_ui.mt.large ? 4 : 3) + (int32_t)m->mic_level * (g_ui.mt.large ? 14 : 9) / 255;
      lv_obj_set_style_arc_width(g_ui.ring, w, LV_PART_INDICATOR);
      g_ui.mic_level = m->mic_level;
    }
    break;
  }
  case UI_SCREEN_SPEAKING:
    ui_pager_show_progress(&g_ui.caption, m->reply.speak_elapsed_ms, m->reply.speak_total_ms);
    break;
  case UI_SCREEN_ASK: {
    int16_t locked = m->now_ms < m->ask.locked_until_ms ? 1 : 0;
    if (locked != g_ui.ask_locked) {
      for (int i = 0; i < UI_ASK_OPTIONS_MAX; i++) {
        lv_opa_t opa = LV_OPA_COVER;
        if (locked) opa = LV_OPA_50;
        else if (m->ask.chosen >= 0 && m->ask.chosen != i) opa = LV_OPA_30;
        lv_obj_set_style_opa(g_ui.opt[i], opa, 0);
      }
      g_ui.ask_locked = locked;
    }
    break;
  }
  default:
    break;
  }
}
```

`firmware/ui/ui.c`:

```c
/* SPDX-License-Identifier: Apache-2.0 */
/* The UI API (gadget_ui.h) on LVGL 9: ui_init, ui_render, ui_tick, ui_deinit. */
#include <string.h>

#include "ui_lv_compat.h"
#include "ui_priv.h"

ui_state_t g_ui;

/* Render cache (contract 2.9: ui_render is cheap when nothing it draws has
 * changed). Core bumps rev on every model change, including the levels and
 * the speaking clock that move every tick. Those are drawn by
 * ui_screens_apply_time() and ui_maus_tick(), so they are zeroed in a copy
 * before comparing, and only a real change re-lays out the screen. On the
 * ESP32, P2c may place the two copies in PSRAM through UI_MODEL_COPY_ATTR. */
#ifndef UI_MODEL_COPY_ATTR
#define UI_MODEL_COPY_ATTR
#endif
static UI_MODEL_COPY_ATTR ui_model_t s_last, s_cur;

static void strip_time_fields(ui_model_t *c) {
  c->rev = 0;
  c->now_ms = 0;
  c->mic_level = 0;
  c->speak_level = 0;
  c->reply.speak_elapsed_ms = 0;
  c->reply.speak_total_ms = 0;
}

gadget_status_t ui_init(const gadget_board_t *board, uint32_t prng_seed) {
  if (!board) return GADGET_ERR_ARG;
  const maus_art_t *art = maus_art_for(board->art_profile);
  if (!art) return GADGET_ERR_UNSUPPORTED;
  memset(&g_ui, 0, sizeof g_ui);
  g_ui.board = board;
  g_ui.art = art;
  g_ui.ask_locked = -1;
  ui_metrics_for(board, art->w, art->h, &g_ui.mt);

  lv_obj_t *scr = lv_screen_active();
  lv_obj_set_style_bg_color(scr, lv_color_hex(UI_COLOR_BG), 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
  ui_set_scrollable(scr, false);

  g_ui.root = ui_box(scr);
  lv_obj_set_size(g_ui.root, board->screen_w, board->screen_h);
  lv_obj_set_pos(g_ui.root, 0, 0);
  lv_obj_set_style_bg_color(g_ui.root, lv_color_hex(UI_COLOR_BG), 0);
  lv_obj_set_style_bg_opa(g_ui.root, LV_OPA_COVER, 0);
  /* Latin-1 fonts through styles on the UI root (contract 2.15); widgets inherit. */
  lv_obj_set_style_text_font(g_ui.root, g_ui.mt.font_body, 0);
  lv_obj_set_style_text_color(g_ui.root, lv_color_hex(UI_COLOR_INK), 0);

  ui_maus_create(&g_ui.maus, g_ui.root, art, prng_seed, g_ui.mt.maus.x, g_ui.mt.maus.y);
  ui_screens_create();
  return GADGET_OK;
}

void ui_render(const ui_model_t *m) {
  if (!g_ui.root || !m) return;
  ui_maus_set_state(&g_ui.maus, m->maus, m->now_ms);
  g_ui.speak_level = m->speak_level;
  if (!g_ui.rendered || m->rev != g_ui.rev) {
    memcpy(&s_cur, m, sizeof s_cur);
    strip_time_fields(&s_cur);
    if (!g_ui.rendered || memcmp(&s_cur, &s_last, sizeof s_cur) != 0) {
      ui_screens_apply(m);
      memcpy(&s_last, &s_cur, sizeof s_last);
    }
    g_ui.rev = m->rev;
    g_ui.rendered = true;
  }
  ui_screens_apply_time(m);
}

void ui_tick(uint64_t now_ms) {
  if (g_ui.root) ui_maus_tick(&g_ui.maus, now_ms, g_ui.speak_level);
  lv_timer_handler();
}

void ui_deinit(void) {
  if (g_ui.root) lv_obj_delete(g_ui.root);
  memset(&g_ui, 0, sizeof g_ui);
}
```

- [ ] **Step 7: Add the sources to `gadget_ui`**

In `firmware/ui/CMakeLists.txt` replace

```cmake
set(UI_SRCS
  ui_maus_engine.c ui_copy.c
  art/maus_art.c)
```

with

```cmake
set(UI_SRCS
  ui.c ui_screens.c ui_maus.c ui_maus_engine.c ui_pager.c ui_copy.c ui_metrics.c
  art/maus_art.c)
```

The finished file:

```cmake
# SPDX-License-Identifier: Apache-2.0
# firmware/ui: LVGL screens, the Maus animation, generated art and fonts.
# Dual-use like core: an ESP-IDF component (built by P2c's project) or a
# desktop static library (GADGET_WITH_LVGL builds).
# ESP-IDF early expansion runs this file in script mode: no sdkconfig values and no
# file(GLOB CONFIGURE_DEPENDS). Report the requirements only.
if(ESP_PLATFORM AND CMAKE_BUILD_EARLY_EXPANSION)
  idf_component_register(INCLUDE_DIRS . art fonts REQUIRES lvgl__lvgl core)
  return()
endif()
set(UI_SRCS
  ui.c ui_screens.c ui_maus.c ui_maus_engine.c ui_pager.c ui_copy.c ui_metrics.c
  art/maus_art.c)
file(GLOB UI_FONT_SRCS CONFIGURE_DEPENDS ${CMAKE_CURRENT_LIST_DIR}/fonts/*.c)

if(ESP_PLATFORM)
  # One art profile per board (contract 2.15): CONFIG_GADGET_ART_PROFILE is s240 or s150.
  set(_profile "${CONFIG_GADGET_ART_PROFILE}")
  if(NOT CMAKE_BUILD_EARLY_EXPANSION AND NOT _profile MATCHES "^(s240|s150)$")
    message(FATAL_ERROR "CONFIG_GADGET_ART_PROFILE must be s240 or s150, not '${_profile}'")
  endif()
  file(GLOB UI_ART_SRCS CONFIGURE_DEPENDS ${CMAKE_CURRENT_LIST_DIR}/art/${_profile}/*.c)
  idf_component_register(SRCS ${UI_SRCS} ${UI_FONT_SRCS} ${UI_ART_SRCS}
                         INCLUDE_DIRS . art fonts
                         REQUIRES lvgl__lvgl core)
  string(TOUPPER "${_profile}" _profile_upper)
  target_compile_definitions(${COMPONENT_LIB} PRIVATE MAUS_ART_HAS_${_profile_upper}=1)
else()
  file(GLOB UI_ART_SRCS CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_LIST_DIR}/art/s240/*.c ${CMAKE_CURRENT_LIST_DIR}/art/s150/*.c)
  add_library(gadget_ui STATIC ${UI_SRCS} ${UI_FONT_SRCS} ${UI_ART_SRCS})
  target_include_directories(gadget_ui PUBLIC ${CMAKE_CURRENT_LIST_DIR} ${CMAKE_CURRENT_LIST_DIR}/art ${CMAKE_CURRENT_LIST_DIR}/fonts)
  target_link_libraries(gadget_ui PUBLIC gadget_core lvgl::lvgl)
  target_compile_definitions(gadget_ui PRIVATE MAUS_ART_HAS_S240=1 MAUS_ART_HAS_S150=1)
  set_target_properties(gadget_ui PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
  # Our sources (hand-written and tools/art output): the contract's warning set.
  # lv_font_conv output is third-party generated code: default warnings only.
  set_source_files_properties(${UI_SRCS} ${UI_ART_SRCS} PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra;-Wpedantic;-Werror")
  if(GADGET_SANITIZE)
    # PUBLIC: every test and gadget-sim linking gadget_ui runs sanitized too.
    # -fno-sanitize-recover as in cmake/warnings.cmake: a UBSan finding aborts
    # and fails its test instead of printing and passing.
    target_compile_options(gadget_ui PUBLIC -fsanitize=address,undefined
      -fno-sanitize-recover=undefined -fno-omit-frame-pointer)
    target_link_options(gadget_ui PUBLIC -fsanitize=address,undefined -fno-sanitize-recover=undefined)
  endif()
endif()
```

- [ ] **Step 8: Run the tests and watch them pass**

```bash
cmake --build build/host -j10 --target test_ui_lv_compat test_ui_art test_ui_copy test_ui_maus_engine test_ui_screens
ctest --test-dir build/host --output-on-failure -R '^ui\.'
```

Expected: `ui.screens` reports `23 Tests 0 Failures` (20 as first written; Contract deviations 5 adds three), including `test_a8_eyes_draw_white:PASS` (the simulator half of spec §6.5's open item: an opaque A8 eye pixel lands as `0xFFFF`); every `ui.*` test passes. If `test_a8_eyes_draw_white` fails with `Expected 0xFFFF Was 0x0000`, stop: the recolor path changed in LVGL; switch `eye_format` to `RGB565A8` per `firmware/ui/README.md` and report it.

- [ ] **Step 9: Commit**

```bash
git add firmware/ui firmware/tests/CMakeLists.txt firmware/tests/test_ui_screens.c
git commit -m "feat(ui): every gadget screen on LVGL with the animated Maus" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 6: The simulator's LVGL display, SDL window and SDL audio

**Files:**
- Create: `firmware/ports/sim/sim_display_lvgl.c`, `firmware/ports/sim/sim_sdl.h`, `firmware/ports/sim/sim_sdl.c`, `firmware/ports/sim/sim_audio_sdl.c`
- Create: `firmware/tests/fresh_dir.cmake`, `firmware/tests/scripts/window_smoke.txt`
- Modify: `firmware/ports/sim/CMakeLists.txt` (append), `firmware/tests/CMakeLists.txt` (append), `firmware/CMakeLists.txt` (`GADGET_WITH_LVGL` default `ON`)
- Test: `firmware/tests/test_sim_display.c`, `firmware/tests/test_sim_sdl.c`, `firmware/tests/test_sim_audio_sdl.c`

**Interfaces:**
- Consumes: `sim_display.h` and `sim_hal.h` (contract §2.16, P2a): implements every `sim_display_*` function and `sim_audio_sdl_backend()`; calls `sim_post_event()` (P2a's `sim_events.c`; tests supply their own).
- Produces: static library `gadget_sim_lvgl` (`sim_display_lvgl.c`, plus `sim_sdl.c` and `sim_audio_sdl.c` when `GADGET_WITH_SDL`; `PUBLIC GADGET_WITH_SDL=1` and, in snapshot-update builds, `PUBLIC GADGET_SNAPSHOT_UPDATE=1`); `gadget-sim` links it instead of `sim_display_null.c` and `ui_stub.c`. Private: `sim_sdl_window_create(board, zoom)`, `sim_sdl_pump()`, `sim_sdl_quit_requested()`, `sim_sdl_shutdown(disp)`. `sim_display_snapshot()` returns 1 passed, 0 failed (LVGL writes `<name>_err.png`), 2 no reference, -1 no display; paths longer than 120 characters fail (LVGL cuts the `_err.png` name at 128). `sim_audio_sdl.c` treats a capture or playback device that will not open like no audio at all (prints the reason once, returns `GADGET_OK`, stays silent), and fills each 20 ms mic frame across `SDL_DequeueAudio` calls, because SDL may hand over less than asked for (on sdl2-compat it often does; the remainder of the frame used to be posted as uninitialised stack bytes, which the new `noisy_frames` checks catch). `fresh_dir.cmake` (`cmake -DDIR=<path> [-DDEV_KEY=<64 hex>] -P ...`) empties a state folder and, with `DEV_KEY`, writes a `storage.json` (contract §4.6) holding only that `dev_key`. The sim test targets get `_POSIX_C_SOURCE=200809L` (and `_DARWIN_C_SOURCE` on macOS), as `gadget_sim_lvgl` does, for `dup2`/`fileno` in the audio test.

- [ ] **Step 1: Write the failing tests and the smoke script**

`firmware/tests/test_sim_display.c`:

```c
/* SPDX-License-Identifier: Apache-2.0 */
/* The headless LVGL display seam: virtual clock, round mask, the shared
 * pointer, snapshot results. No SDL is touched in headless mode. */
#include <string.h>

#include "gadget_board.h"
#include "gadget_ui.h"
#include "lvgl.h"
#include "sim_display.h"
#include "sim_hal.h"
#include "unity.h"

void sim_post_event(const gadget_event_t *ev) { (void)ev; }

static bool g_open;

static void open_headless(const char *id) {
  const gadget_board_t *b = gadget_board_by_id(id);
  TEST_ASSERT_NOT_NULL(b);
  sim_display_opts_t o = {true, 1.0f, "firmware/tests/snapshots/none"};
  TEST_ASSERT_EQUAL_INT(0, sim_display_init(b, &o));
  g_open = true;
}

void setUp(void) {}
void tearDown(void) {
  if (g_open) sim_display_deinit();
  g_open = false;
}

static void test_virtual_clock_moves_only_when_advanced(void) {
  open_headless("lcd-154");
  uint32_t t0 = lv_tick_get();
  sim_display_advance(10);
  sim_display_advance(10);
  TEST_ASSERT_EQUAL_UINT32(t0 + 20, lv_tick_get());
  TEST_ASSERT_FALSE(sim_display_quit_requested());
}

static void test_round_boards_get_a_mask_square_ones_do_not(void) {
  open_headless("amoled-175c");
  TEST_ASSERT_EQUAL_UINT32(1, lv_obj_get_child_count(lv_layer_sys()));
  sim_display_deinit();
  g_open = false;
  open_headless("devkit");
  TEST_ASSERT_EQUAL_UINT32(0, lv_obj_get_child_count(lv_layer_sys()));
}

static void test_script_touch_drives_the_pointer(void) {
  open_headless("amoled-175c");
  lv_indev_t *in = lv_indev_get_next(NULL);
  TEST_ASSERT_NOT_NULL(in);
  sim_display_touch(true, 120, 300);
  lv_indev_read(in);
  lv_point_t p;
  lv_indev_get_point(in, &p);
  TEST_ASSERT_EQUAL_INT32(120, p.x);
  TEST_ASSERT_EQUAL_INT32(300, p.y);
  TEST_ASSERT_EQUAL_INT(LV_INDEV_STATE_PRESSED, lv_indev_get_state(in));
  sim_display_touch(false, 120, 300);
  lv_indev_read(in);
  TEST_ASSERT_EQUAL_INT(LV_INDEV_STATE_RELEASED, lv_indev_get_state(in));
}

static void test_missing_reference_is_reported(void) {
  open_headless("lcd-154");
  TEST_ASSERT_EQUAL_INT(GADGET_OK, ui_init(gadget_board_by_id("lcd-154"), 1));
#if !defined(GADGET_SNAPSHOT_UPDATE)
  TEST_ASSERT_EQUAL_INT(2, sim_display_snapshot("no-such-golden"));
#endif
  ui_deinit();
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_virtual_clock_moves_only_when_advanced);
  RUN_TEST(test_round_boards_get_a_mask_square_ones_do_not);
  RUN_TEST(test_script_touch_drives_the_pointer);
  RUN_TEST(test_missing_reference_is_reported);
  return UNITY_END();
}
```

`firmware/tests/test_sim_sdl.c`:

```c
/* SPDX-License-Identifier: Apache-2.0 */
/* Window mode input through the real SDL event filter, on SDL's dummy video
 * driver (CTest sets SDL_VIDEODRIVER=dummy). This test supplies its own
 * sim_post_event to record what the filter queues. */
#include <string.h>
#include <SDL2/SDL.h>

#include "gadget_board.h"
#include "lvgl.h"
#include "sim_display.h"
#include "sim_hal.h"
#include "unity.h"

static gadget_event_t posted[16];
static int n_posted;

void sim_post_event(const gadget_event_t *ev) {
  if (n_posted < 16) posted[n_posted++] = *ev;
}

static bool g_open;

static void open_window(const char *board_id, float zoom) {
  const gadget_board_t *b = gadget_board_by_id(board_id);
  TEST_ASSERT_NOT_NULL(b);
  sim_display_opts_t o = {false, zoom, "."};
  TEST_ASSERT_EQUAL_INT(0, sim_display_init(b, &o));
  g_open = true;
}

static void close_window(void) {
  sim_display_deinit();
  g_open = false;
}

void setUp(void) { n_posted = 0; }
void tearDown(void) {
  if (g_open) close_window();
}

static void key(SDL_EventType type, SDL_Keycode sym, Uint8 repeat) {
  SDL_Event e;
  SDL_zero(e);
  e.type = type;
  e.key.keysym.sym = sym;
  e.key.repeat = repeat;
  TEST_ASSERT_EQUAL_INT(0, SDL_PushEvent(&e)); /* 0: the filter consumed it */
}

static void mouse_button(SDL_EventType type, int x, int y) {
  SDL_Event e;
  SDL_zero(e);
  e.type = type;
  e.button.button = SDL_BUTTON_LEFT;
  e.button.x = x;
  e.button.y = y;
  TEST_ASSERT_EQUAL_INT(0, SDL_PushEvent(&e));
}

static void test_space_is_talk_and_repeat_is_ignored(void) {
  open_window("amoled-175c", 1.0f);
  key(SDL_KEYDOWN, SDLK_SPACE, 0);
  key(SDL_KEYDOWN, SDLK_SPACE, 1);
  key(SDL_KEYUP, SDLK_SPACE, 0);
  TEST_ASSERT_EQUAL_INT(2, n_posted);
  TEST_ASSERT_EQUAL_INT(GADGET_EV_INPUT, posted[0].type);
  TEST_ASSERT_EQUAL_INT(GADGET_IN_TALK_DOWN, posted[0].u.input.type);
  TEST_ASSERT_EQUAL_INT(GADGET_IN_TALK_UP, posted[1].u.input.type);
}

static void test_escape_is_cancel_only_on_boards_with_cancel(void) {
  open_window("amoled-175c", 1.0f);
  key(SDL_KEYDOWN, SDLK_ESCAPE, 0);
  key(SDL_KEYUP, SDLK_ESCAPE, 0);
  TEST_ASSERT_EQUAL_INT(2, n_posted);
  TEST_ASSERT_EQUAL_INT(GADGET_IN_CANCEL_DOWN, posted[0].u.input.type);
  TEST_ASSERT_EQUAL_INT(GADGET_IN_CANCEL_UP, posted[1].u.input.type);
  close_window();
  n_posted = 0;
  open_window("amoled-175", 1.0f); /* touch + talk only */
  key(SDL_KEYDOWN, SDLK_ESCAPE, 0);
  TEST_ASSERT_EQUAL_INT(0, n_posted);
}

static void test_mouse_is_touch_in_screen_pixels(void) {
  open_window("amoled-175c", 2.0f);
  mouse_button(SDL_MOUSEBUTTONDOWN, 200, 120);
  SDL_Event e;
  SDL_zero(e);
  e.type = SDL_MOUSEMOTION;
  e.motion.x = 220;
  e.motion.y = 140;
  TEST_ASSERT_EQUAL_INT(0, SDL_PushEvent(&e));
  mouse_button(SDL_MOUSEBUTTONUP, 2000, 2000); /* outside: clamped to the screen */
  TEST_ASSERT_EQUAL_INT(3, n_posted);
  TEST_ASSERT_EQUAL_INT(GADGET_IN_TOUCH_DOWN, posted[0].u.input.type);
  TEST_ASSERT_EQUAL_INT16(100, posted[0].u.input.x);
  TEST_ASSERT_EQUAL_INT16(60, posted[0].u.input.y);
  TEST_ASSERT_EQUAL_INT(GADGET_IN_TOUCH_MOVE, posted[1].u.input.type);
  TEST_ASSERT_EQUAL_INT16(110, posted[1].u.input.x);
  TEST_ASSERT_EQUAL_INT(GADGET_IN_TOUCH_UP, posted[2].u.input.type);
  TEST_ASSERT_EQUAL_INT16(465, posted[2].u.input.x);
  TEST_ASSERT_EQUAL_INT16(465, posted[2].u.input.y);
}

static void test_mouse_does_nothing_on_button_boards(void) {
  open_window("lcd-154", 2.0f);
  mouse_button(SDL_MOUSEBUTTONDOWN, 10, 10);
  TEST_ASSERT_EQUAL_INT(0, n_posted);
}

static void test_quit_and_close_are_dropped_but_remembered(void) {
  open_window("devkit", 2.0f);
  TEST_ASSERT_FALSE(sim_display_quit_requested());
  SDL_Event e;
  SDL_zero(e);
  e.type = SDL_WINDOWEVENT;
  e.window.event = SDL_WINDOWEVENT_CLOSE;
  TEST_ASSERT_EQUAL_INT(0, SDL_PushEvent(&e));
  TEST_ASSERT_TRUE(sim_display_quit_requested());
  SDL_zero(e);
  e.type = SDL_QUIT;
  TEST_ASSERT_EQUAL_INT(0, SDL_PushEvent(&e));
  sim_display_poll();
  lv_timer_handler(); /* LVGL's SDL timer must not exit or delete the display */
  TEST_ASSERT_NOT_NULL(lv_display_get_default());
  /* main.c runs sim_display_deinit() before every execv restart: SDL must be fully shut */
  close_window();
  TEST_ASSERT_EQUAL_UINT32(0, SDL_WasInit(SDL_INIT_EVERYTHING));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_space_is_talk_and_repeat_is_ignored);
  RUN_TEST(test_escape_is_cancel_only_on_boards_with_cancel);
  RUN_TEST(test_mouse_is_touch_in_screen_pixels);
  RUN_TEST(test_mouse_does_nothing_on_button_boards);
  RUN_TEST(test_quit_and_close_are_dropped_but_remembered);
  return UNITY_END();
}
```

`firmware/tests/test_sim_audio_sdl.c`:

```c
/* SPDX-License-Identifier: Apache-2.0 */
/* The SDL audio backend on SDL's dummy audio driver (CTest sets
 * SDL_AUDIODRIVER=dummy): the lazy mic and its permission hint, rates, queue
 * limits, stop, mic frames; and silence without a usable device. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <SDL2/SDL.h>

#include "sim_hal.h"
#include "unity.h"

static int mic_frames;
static int bad_frames;
static int noisy_frames; /* the dummy driver records silence: any other sample is not from SDL */

void sim_post_event(const gadget_event_t *ev) {
  if (ev->type != GADGET_EV_MIC_FRAME) return;
  if (ev->u.mic.samples != GADGET_MIC_FRAME_SAMPLES || !ev->u.mic.pcm) {
    bad_frames++;
    return;
  }
  mic_frames++;
  for (size_t i = 0; i < GADGET_MIC_FRAME_SAMPLES; i++) {
    if (ev->u.mic.pcm[i] != 0) {
      noisy_frames++;
      break;
    }
  }
}

static const sim_audio_backend_t *b;

void setUp(void) {
  b = sim_audio_sdl_backend();
  mic_frames = 0;
  bad_frames = 0;
  noisy_frames = 0;
}
void tearDown(void) {
  b->mic_stop();
  b->spk_stop();
}

/* Runs first, so nothing in this process has touched SDL audio yet. */
static void test_mic_opens_lazily_and_hints_on_silence(void) {
  TEST_ASSERT_EQUAL_UINT32(0, SDL_WasInit(SDL_INIT_AUDIO)); /* selecting the backend opens nothing */
  FILE *log = tmpfile();
  TEST_ASSERT_NOT_NULL(log);
  fflush(stderr);
  int saved = dup(fileno(stderr));
  dup2(fileno(log), fileno(stderr));
  gadget_status_t st = b->mic_start(GADGET_MIC_RATE); /* the first TALK */
  for (int i = 0; i < 100; i++) {                    /* 2 s of the dummy driver's silence */
    SDL_Delay(20);
    b->pump(0);
  }
  b->mic_stop();
  fflush(stderr);
  dup2(saved, fileno(stderr));
  close(saved);
  TEST_ASSERT_EQUAL_INT(GADGET_OK, st);
  TEST_ASSERT_NOT_EQUAL(0, SDL_WasInit(SDL_INIT_AUDIO));
  char text[4096];
  /* stderr wrote through the descriptor, behind log's stdio buffer: read it the same way */
  lseek(fileno(log), 0, SEEK_SET);
  ssize_t n = read(fileno(log), text, sizeof text - 1);
  fclose(log);
  text[n > 0 ? n : 0] = '\0';
  int hints = 0;
  for (const char *p = text; (p = strstr(p, "the microphone is silent")) != NULL; p++) hints++;
  TEST_ASSERT_EQUAL_INT_MESSAGE(1, hints, "the permission hint prints once after about 1 s of silence");
  TEST_ASSERT_GREATER_OR_EQUAL(50, mic_frames); /* silent frames still reach core */
  TEST_ASSERT_EQUAL_INT(0, noisy_frames);
}

static void test_rates_outside_v1_are_refused(void) {
  TEST_ASSERT_EQUAL_INT(GADGET_ERR_UNSUPPORTED, b->mic_start(24000));
  TEST_ASSERT_EQUAL_INT(GADGET_ERR_UNSUPPORTED, b->spk_open(44100));
}

static void test_speaker_queues_reports_and_stops(void) {
  static int16_t pcm[1600]; /* 100 ms at 16 kHz */
  for (size_t i = 0; i < 1600; i++) pcm[i] = (int16_t)((i % 32) * 500 - 8000);
  TEST_ASSERT_EQUAL_INT(GADGET_OK, b->spk_open(16000));
  b->spk_stop();
  TEST_ASSERT_EQUAL_size_t(1600, b->spk_write(pcm, 1600));
  uint32_t ms = b->spk_buffered_ms();
  TEST_ASSERT_TRUE(ms > 50 && ms <= 100);
  b->spk_stop();
  TEST_ASSERT_EQUAL_UINT32(0, b->spk_buffered_ms());
}

static void test_speaker_never_queues_more_than_two_seconds(void) {
  static int16_t pcm[24000];
  TEST_ASSERT_EQUAL_INT(GADGET_OK, b->spk_open(24000));
  b->spk_stop();
  TEST_ASSERT_EQUAL_size_t(24000, b->spk_write(pcm, 24000)); /* 1 s */
  TEST_ASSERT_EQUAL_size_t(24000, b->spk_write(pcm, 24000)); /* 2 s */
  TEST_ASSERT_TRUE(b->spk_write(pcm, 24000) < 24000);        /* only what played meanwhile */
  TEST_ASSERT_TRUE(b->spk_buffered_ms() <= 2000);
}

static void test_mic_posts_20_ms_frames_after_talk(void) {
  TEST_ASSERT_EQUAL_INT(GADGET_OK, b->mic_start(GADGET_MIC_RATE));
  for (int i = 0; i < 40; i++) { /* ~400 ms of real time */
    SDL_Delay(10);
    b->pump(0);
  }
  b->mic_stop();
  TEST_ASSERT_GREATER_THAN(5, mic_frames);
  TEST_ASSERT_EQUAL_INT(0, bad_frames);
  TEST_ASSERT_EQUAL_INT(0, noisy_frames); /* whole frames only, even when SDL hands over part of one */
  int after = mic_frames;
  SDL_Delay(50);
  b->pump(0);
  TEST_ASSERT_EQUAL_INT(after, mic_frames); /* nothing once stopped */
}

/* Run alone (GADGET_TEST_NO_AUDIO) twice: with SDL_AUDIODRIVER naming a driver
 * that does not exist (SDL audio does not start), and with SDL's disk driver
 * pointed at a missing folder (SDL starts, but neither device opens). */
static void test_no_audio_device_is_silent_not_fatal(void) {
  static int16_t pcm[320];
  TEST_ASSERT_EQUAL_INT(GADGET_OK, b->mic_start(GADGET_MIC_RATE));
  SDL_Delay(100);
  b->pump(0);
  TEST_ASSERT_EQUAL_INT(0, mic_frames);
  TEST_ASSERT_EQUAL_INT(GADGET_OK, b->spk_open(16000));
  TEST_ASSERT_EQUAL_size_t(320, b->spk_write(pcm, 320)); /* accepted and dropped */
  TEST_ASSERT_EQUAL_UINT32(0, b->spk_buffered_ms());
}

int main(void) {
  UNITY_BEGIN();
  if (getenv("GADGET_TEST_NO_AUDIO")) {
    RUN_TEST(test_no_audio_device_is_silent_not_fatal);
    return UNITY_END();
  }
  RUN_TEST(test_mic_opens_lazily_and_hints_on_silence); /* first: SDL audio still untouched */
  RUN_TEST(test_rates_outside_v1_are_refused);
  RUN_TEST(test_speaker_queues_reports_and_stops);
  RUN_TEST(test_speaker_never_queues_more_than_two_seconds);
  RUN_TEST(test_mic_posts_20_ms_frames_after_talk);
  int r = UNITY_END();
  SDL_Quit();
  return r;
}
```

`firmware/tests/fresh_dir.cmake`:

```cmake
# SPDX-License-Identifier: Apache-2.0
# cmake -DDIR=<path> [-DDEV_KEY=<64 lowercase hex>] -P fresh_dir.cmake
# An empty state folder at <path>. With DEV_KEY, the folder's storage.json
# (contract 4.6) holds only that device key, so the simulator keeps a fixed
# identity instead of generating a random one: the RFC key of contract 1.7
# gives the device id gad_b18b86ce1389e46d.
if(NOT DIR)
  message(FATAL_ERROR "fresh_dir.cmake: pass -DDIR=<path>")
endif()
file(REMOVE_RECURSE "${DIR}")
file(MAKE_DIRECTORY "${DIR}")
if(DEV_KEY)
  if(NOT DEV_KEY MATCHES "^[0-9a-f]+$")
    message(FATAL_ERROR "fresh_dir.cmake: DEV_KEY must be lowercase hex")
  endif()
  file(WRITE "${DIR}/storage.json" "{\"version\":1,\"entries\":{\"dev_key\":{\"blob\":\"${DEV_KEY}\"}}}\n")
endif()
```

`firmware/tests/scripts/window_smoke.txt`:

```text
# SPDX-License-Identifier: Apache-2.0
# Window mode on SDL's dummy drivers (CTest sim.window_smoke): the SDL window,
# the event filter and the SDL audio backend start and shut down cleanly.
model screen setup 5000
wait 300
console status
wait 300
```

Append to `firmware/tests/CMakeLists.txt`:

```bash
cat >> firmware/tests/CMakeLists.txt <<'EOF'

# ---- P2b: simulator display, SDL window and SDL audio tests ---------------
# Gated on the options, never on `TARGET`: gadget_sim_lvgl and gadget-sim come
# from ports/sim (added only with GADGET_BUILD_SIM), and target names resolve at
# generate time, so the order of add_subdirectory() calls does not matter.
if(GADGET_WITH_LVGL AND GADGET_BUILD_SIM)
  # gadget_sim_test(<area>): test_sim_<area>.c on SDL's dummy drivers -> CTest sim.<area>.
  function(gadget_sim_test area)
    add_executable(test_sim_${area} test_sim_${area}.c)
    target_link_libraries(test_sim_${area} PRIVATE gadget_sim_lvgl unity::framework)
    target_compile_definitions(test_sim_${area} PRIVATE _POSIX_C_SOURCE=200809L $<$<PLATFORM_ID:Darwin>:_DARWIN_C_SOURCE>)
    target_compile_options(test_sim_${area} PRIVATE -Wall -Wextra -Werror)
    add_test(NAME sim.${area} COMMAND test_sim_${area})
    set_tests_properties(sim.${area} PROPERTIES LABELS unit TIMEOUT 60
                         ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy")
  endfunction()
  gadget_sim_test(display)
  if(GADGET_WITH_SDL)
    gadget_sim_test(sdl)
    gadget_sim_test(audio_sdl)
    # No usable audio: SDL audio does not start (unknown driver), or it starts
    # but neither device opens (the disk driver pointed at a missing folder).
    add_test(NAME sim.audio_sdl.no_device COMMAND test_sim_audio_sdl)
    set_tests_properties(sim.audio_sdl.no_device PROPERTIES LABELS unit TIMEOUT 60
                         ENVIRONMENT "SDL_AUDIODRIVER=gadget-none;GADGET_TEST_NO_AUDIO=1")
    set(_no_dir ${CMAKE_CURRENT_BINARY_DIR}/no-such-dir)
    add_test(NAME sim.audio_sdl.open_fails COMMAND test_sim_audio_sdl)
    set_tests_properties(sim.audio_sdl.open_fails PROPERTIES LABELS unit TIMEOUT 60
                         ENVIRONMENT "SDL_AUDIODRIVER=disk;SDL_DISKAUDIOFILE=${_no_dir}/out.raw;SDL_DISKAUDIOFILEIN=${_no_dir}/in.raw;GADGET_TEST_NO_AUDIO=1")
    # The whole simulator in window mode on SDL's dummy drivers, from a fresh state folder.
    set(_win_state ${CMAKE_BINARY_DIR}/snap-state/window-smoke)
    add_test(NAME sim.window_smoke.fresh
             COMMAND ${CMAKE_COMMAND} -DDIR=${_win_state} -P ${CMAKE_CURRENT_SOURCE_DIR}/fresh_dir.cmake)
    set_tests_properties(sim.window_smoke.fresh PROPERTIES FIXTURES_SETUP sim.window_smoke.state LABELS unit)
    add_test(NAME sim.window_smoke
             COMMAND $<TARGET_FILE:gadget-sim> --board amoled-175c --host script --state-dir ${_win_state}
                     --script ${CMAKE_CURRENT_SOURCE_DIR}/scripts/window_smoke.txt)
    set_tests_properties(sim.window_smoke PROPERTIES FIXTURES_REQUIRED sim.window_smoke.state LABELS unit TIMEOUT 60
                         ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy")
  endif()
endif()
EOF
```

- [ ] **Step 2: Watch them fail**

Run: `cmake -S firmware -B build/host && cmake --build build/host -j10`
Expected: FAIL: the tests cannot link `gadget_sim_lvgl` (`library 'gadget_sim_lvgl' not found` or undefined `sim_display_init`).

- [ ] **Step 3: Write the display seam**

`firmware/ports/sim/sim_display_lvgl.c`:

```c
/* SPDX-License-Identifier: Apache-2.0 */
/* The LVGL display seam (contract 2.16) for GADGET_WITH_LVGL builds:
 * --headless: LVGL's in-memory test display (RGB565) on the virtual clock,
 *             snapshots through lv_test_screenshot_compare (bundled lodepng);
 * otherwise:  the SDL window (sim_sdl.c), zoomed, on SDL's real clock.
 * Both use one custom pointer device that reads the shared touch state the
 * script (`touch`/`release`) and the mouse both drive, and on round boards a
 * black ring on lv_layer_sys() masks the corners like the device's glass. */
#include "sim_display.h"

#include <stdatomic.h>
#include <stdio.h>
#include <unistd.h>

#include "lvgl.h"
#include "ui_lv_compat.h"
#if defined(GADGET_WITH_SDL)
#include "sim_sdl.h"
#endif

static struct {
  const gadget_board_t *board;
  bool headless;
  const char *snapshot_dir;
  lv_display_t *disp;
  lv_indev_t *pointer;
} s;

/* bit 31: pressed; bits 16..30: x; bits 0..15: y */
static atomic_uint_fast32_t s_touch;

static void pointer_read(lv_indev_t *indev, lv_indev_data_t *data) {
  (void)indev;
  uint32_t v = (uint32_t)atomic_load(&s_touch);
  data->point.x = (int32_t)((v >> 16) & 0x7FFFu);
  data->point.y = (int32_t)(v & 0xFFFFu);
  data->state = (v & 0x80000000u) ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

void sim_display_touch(bool pressed, int16_t x, int16_t y) {
  uint32_t v = ((uint32_t)(x < 0 ? 0 : x) & 0x7FFFu) << 16 | ((uint32_t)(y < 0 ? 0 : y) & 0xFFFFu);
  if (pressed) v |= 0x80000000u;
  atomic_store(&s_touch, v);
}

/* A full-screen black ring whose inner edge is the screen's circle. */
static void round_mask(int32_t w, int32_t h) {
  int32_t b = w * 45 / 200 + 2; /* > (sqrt(2) - 1) * r, so the corners are covered */
  lv_obj_t *m = lv_obj_create(lv_layer_sys());
  lv_obj_remove_style_all(m);
  lv_obj_set_size(m, w + 2 * b, h + 2 * b);
  lv_obj_set_pos(m, -b, -b);
  lv_obj_set_style_radius(m, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_opa(m, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(m, b, 0);
  lv_obj_set_style_border_color(m, lv_color_black(), 0);
  lv_obj_set_style_border_opa(m, LV_OPA_COVER, 0);
  ui_set_clickable(m, false);
  ui_set_scrollable(m, false);
}

int sim_display_init(const gadget_board_t *board, const sim_display_opts_t *opts) {
  s.board = board;
  s.headless = opts->headless;
  s.snapshot_dir = opts->snapshot_dir;
  atomic_store(&s_touch, 0);
  lv_init();
  if (s.headless) {
    s.disp = lv_test_display_create(board->screen_w, board->screen_h);
    if (s.disp) lv_display_set_color_format(s.disp, LV_COLOR_FORMAT_RGB565);
  } else {
#if defined(GADGET_WITH_SDL)
    s.disp = sim_sdl_window_create(board, opts->zoom);
#else
    fprintf(stderr, "gadget-sim: this build has no SDL window; use --headless\n");
    s.disp = NULL;
#endif
  }
  if (!s.disp) {
    lv_deinit();
    return -1;
  }
  s.pointer = lv_indev_create();
  lv_indev_set_type(s.pointer, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(s.pointer, pointer_read);
  lv_indev_set_display(s.pointer, s.disp);
  if (board->screen_round) round_mask(board->screen_w, board->screen_h);
  return 0;
}

void sim_display_advance(uint32_t elapsed_ms) {
  if (s.headless) lv_tick_inc(elapsed_ms);
}

void sim_display_poll(void) {
#if defined(GADGET_WITH_SDL)
  if (!s.headless && s.disp) sim_sdl_pump();
#endif
}

bool sim_display_quit_requested(void) {
#if defined(GADGET_WITH_SDL)
  if (!s.headless && s.disp) return sim_sdl_quit_requested();
#endif
  return false;
}

int sim_display_snapshot(const char *name) {
  if (!s.disp) return -1;
  char path[256];
  int n = snprintf(path, sizeof path, "%s/%s.png", s.snapshot_dir ? s.snapshot_dir : ".", name);
  if (n < 0 || (size_t)n >= sizeof path || (size_t)n > 120) {
    fprintf(stderr, "gadget-sim: snapshot path too long: %s/%s.png\n", s.snapshot_dir, name);
    return 0;
  }
#if defined(GADGET_SNAPSHOT_UPDATE)
  unlink(path); /* LVGL writes a reference only when none exists */
#endif
  switch (lv_test_screenshot_compare(path)) {
  case LV_TEST_SCREENSHOT_RESULT_PASSED:
    return 1;
  case LV_TEST_SCREENSHOT_RESULT_NO_REFERENCE_IMAGE:
    return 2;
  default:
    return 0;
  }
}

void sim_display_deinit(void) {
  if (!s.disp) return;
#if defined(GADGET_WITH_SDL)
  if (!s.headless) {
    sim_sdl_shutdown(s.disp);
    s.disp = NULL;
  }
#endif
  lv_deinit();
  s.disp = NULL;
  s.pointer = NULL;
}
```

- [ ] **Step 4: Write the window mode and the SDL audio backend**

`firmware/ports/sim/sim_sdl.h`:

```c
/* SPDX-License-Identifier: Apache-2.0 */
/* SDL window mode (P2b): the LVGL SDL window plus an event filter that turns
 * Space, Esc and the mouse into HAL input events. Private to ports/sim. */
#ifndef SIM_SDL_H
#define SIM_SDL_H

#include <stdbool.h>
#include "gadget_board.h"
#include "lvgl.h"

/* lv_sdl_window_create + title + zoom, then SDL_SetEventFilter. NULL on failure. */
lv_display_t *sim_sdl_window_create(const gadget_board_t *board, float zoom);
/* SDL_PumpEvents(): runs the filter for everything queued since the last call. */
void sim_sdl_pump(void);
bool sim_sdl_quit_requested(void);
/* Delete the window's display, then SDL_Quit (lv_sdl_quit). */
void sim_sdl_shutdown(lv_display_t *disp);

#endif /* SIM_SDL_H */
```

`firmware/ports/sim/sim_sdl.c`:

```c
/* SPDX-License-Identifier: Apache-2.0 */
/* Window mode input (spec 5.7, contract 2.16): Space = TALK, Esc = CANCEL,
 * mouse = touch, key repeat ignored; quit and window-close are dropped so
 * LVGL never exits or deletes the display (LV_SDL_DIRECT_EXIT is 0). The
 * filter only queues events (sim_post_event is thread-safe) and updates
 * the shared pointer state; it never calls into core or LVGL. */
#include "sim_sdl.h"

#include <stdatomic.h>
#include <stdio.h>
#include <SDL2/SDL.h>

#include "sim_display.h"
#include "sim_hal.h"

static const gadget_board_t *s_board;
static float s_zoom = 1.0f;
static atomic_bool s_quit;
static bool s_mouse_down;

static void post_input(gadget_input_type_t type, int16_t x, int16_t y) {
  gadget_event_t ev;
  ev.type = GADGET_EV_INPUT;
  ev.u.input.type = type;
  ev.u.input.x = x;
  ev.u.input.y = y;
  ev.u.input.dir = GADGET_SWIPE_UP;
  sim_post_event(&ev);
}

static int16_t to_screen(int32_t v, uint16_t limit) {
  int32_t p = (int32_t)((float)v / s_zoom);
  if (p < 0) p = 0;
  if (p >= (int32_t)limit) p = (int32_t)limit - 1;
  return (int16_t)p;
}

static int SDLCALL filter(void *userdata, SDL_Event *e) {
  (void)userdata;
  const uint32_t inputs = s_board->input_mask;
  switch (e->type) {
  case SDL_KEYDOWN:
  case SDL_KEYUP: {
    const bool down = e->type == SDL_KEYDOWN;
    if (e->key.keysym.sym == SDLK_SPACE) {
      if (!e->key.repeat && (inputs & GADGET_INPUT_TALK)) post_input(down ? GADGET_IN_TALK_DOWN : GADGET_IN_TALK_UP, 0, 0);
      return 0;
    }
    if (e->key.keysym.sym == SDLK_ESCAPE) {
      if (!e->key.repeat && (inputs & GADGET_INPUT_CANCEL)) post_input(down ? GADGET_IN_CANCEL_DOWN : GADGET_IN_CANCEL_UP, 0, 0);
      return 0;
    }
    return 1;
  }
  case SDL_TEXTINPUT:
    return 0;
  case SDL_MOUSEBUTTONDOWN:
  case SDL_MOUSEBUTTONUP: {
    if (e->button.button != SDL_BUTTON_LEFT) return 0;
    if (!(inputs & GADGET_INPUT_TOUCH)) return 0;
    const bool down = e->type == SDL_MOUSEBUTTONDOWN;
    int16_t x = to_screen(e->button.x, s_board->screen_w);
    int16_t y = to_screen(e->button.y, s_board->screen_h);
    s_mouse_down = down;
    sim_display_touch(down, x, y);
    post_input(down ? GADGET_IN_TOUCH_DOWN : GADGET_IN_TOUCH_UP, x, y);
    return 0;
  }
  case SDL_MOUSEMOTION: {
    if (s_mouse_down && (inputs & GADGET_INPUT_TOUCH)) {
      int16_t x = to_screen(e->motion.x, s_board->screen_w);
      int16_t y = to_screen(e->motion.y, s_board->screen_h);
      sim_display_touch(true, x, y);
      post_input(GADGET_IN_TOUCH_MOVE, x, y);
    }
    return 0;
  }
  case SDL_QUIT:
    atomic_store(&s_quit, true);
    return 0;
  case SDL_WINDOWEVENT:
    if (e->window.event == SDL_WINDOWEVENT_CLOSE) {
      atomic_store(&s_quit, true);
      return 0;
    }
    return 1;
  default:
    return 1;
  }
}

lv_display_t *sim_sdl_window_create(const gadget_board_t *board, float zoom) {
  s_board = board;
  s_zoom = zoom > 0.0f ? zoom : 1.0f;
  atomic_store(&s_quit, false);
  s_mouse_down = false;
  lv_display_t *disp = lv_sdl_window_create(board->screen_w, board->screen_h);
  if (!disp) {
    fprintf(stderr, "gadget-sim: cannot open a window: %s\n", SDL_GetError());
    return NULL;
  }
  char title[64];
  snprintf(title, sizeof title, "gadget-sim %s", board->id);
  lv_sdl_window_set_title(disp, title);
  lv_sdl_window_set_zoom(disp, s_zoom);
  SDL_SetEventFilter(filter, NULL);
  return disp;
}

void sim_sdl_pump(void) { SDL_PumpEvents(); }

bool sim_sdl_quit_requested(void) { return atomic_load(&s_quit); }

void sim_sdl_shutdown(lv_display_t *disp) {
  SDL_SetEventFilter(NULL, NULL);
  if (disp) lv_display_delete(disp);
  lv_sdl_quit();
}
```

`firmware/ports/sim/sim_audio_sdl.c`:

```c
/* SPDX-License-Identifier: Apache-2.0 */
/* Window-mode audio (contract 2.16): the Mac's (or PC's) microphone and
 * speakers through SDL2's queued audio. The capture device opens lazily on
 * the first TALK, so macOS asks for microphone access only then; after
 * about 1 s of all-zero samples while talking we print a permission hint.
 * Without an audio device (no audio driver, or a device that will not open)
 * the backend is silent, never fatal: each problem is printed once. */
#include <stdio.h>
#include <string.h>
#include <SDL2/SDL.h>

#include "sim_hal.h"

#define SIM_SPK_QUEUE_MS 2000u   /* hal_spk_write accepts up to 2 s ahead */
#define SIM_SILENT_HINT_MS 1000u

static struct {
  bool tried, ok;
  bool cap_reported, play_reported; /* an open failure was printed */
  SDL_AudioDeviceID cap;
  SDL_AudioDeviceID play;
  uint32_t play_rate;
  uint8_t volume;
  bool capturing;
  uint32_t silent_ms;
  bool hinted;
  int16_t frame[GADGET_MIC_FRAME_SAMPLES]; /* the mic frame being filled */
  size_t frame_bytes;
} a = {.volume = 100};

static bool audio_ready(void) {
  if (!a.tried) {
    a.tried = true;
    a.ok = SDL_InitSubSystem(SDL_INIT_AUDIO) == 0;
    if (!a.ok) fprintf(stderr, "gadget-sim: no audio (%s); the mic and speaker are silent\n", SDL_GetError());
  }
  return a.ok;
}

static gadget_status_t mic_start(uint32_t rate) {
  if (rate != GADGET_MIC_RATE) return GADGET_ERR_UNSUPPORTED;
  if (!audio_ready()) return GADGET_OK;
  if (!a.cap) {
    SDL_AudioSpec want;
    SDL_zero(want);
    want.freq = (int)GADGET_MIC_RATE;
    want.format = AUDIO_S16SYS;
    want.channels = 1;
    want.samples = GADGET_MIC_FRAME_SAMPLES;
    a.cap = SDL_OpenAudioDevice(NULL, 1, &want, NULL, 0); /* SDL converts to exactly this format */
    if (!a.cap) {
      if (!a.cap_reported) fprintf(stderr, "gadget-sim: no microphone (%s); recordings are silent\n", SDL_GetError());
      a.cap_reported = true;
      return GADGET_OK; /* silent: pump() posts nothing while a.cap is 0 */
    }
  }
  SDL_ClearQueuedAudio(a.cap);
  SDL_PauseAudioDevice(a.cap, 0);
  a.capturing = true;
  a.silent_ms = 0;
  a.frame_bytes = 0;
  return GADGET_OK;
}

static void mic_stop(void) {
  if (a.cap) {
    SDL_PauseAudioDevice(a.cap, 1);
    SDL_ClearQueuedAudio(a.cap);
  }
  a.capturing = false;
  a.frame_bytes = 0;
}

static gadget_status_t spk_open(uint32_t rate) {
  if (rate != 16000u && rate != 24000u) return GADGET_ERR_UNSUPPORTED;
  if (!audio_ready()) return GADGET_OK;
  if (a.play && a.play_rate == rate) return GADGET_OK;
  if (a.play) SDL_CloseAudioDevice(a.play);
  SDL_AudioSpec want;
  SDL_zero(want);
  want.freq = (int)rate;
  want.format = AUDIO_S16SYS;
  want.channels = 1;
  want.samples = (Uint16)(rate / 50u);
  a.play = SDL_OpenAudioDevice(NULL, 0, &want, NULL, 0);
  if (!a.play) {
    if (!a.play_reported) fprintf(stderr, "gadget-sim: no speaker (%s); playback is silent\n", SDL_GetError());
    a.play_reported = true;
    a.play_rate = 0;
    return GADGET_OK; /* silent: spk_write() accepts and drops while a.play is 0 */
  }
  a.play_rate = rate;
  SDL_PauseAudioDevice(a.play, 0);
  return GADGET_OK;
}

static size_t spk_write(const int16_t *pcm, size_t samples) {
  if (!a.play) return samples; /* silent: accept and drop */
  uint32_t queued = SDL_GetQueuedAudioSize(a.play) / 2u;
  uint32_t cap = a.play_rate * SIM_SPK_QUEUE_MS / 1000u;
  if (queued >= cap) return 0;
  size_t n = samples < (size_t)(cap - queued) ? samples : (size_t)(cap - queued);
  int16_t chunk[480];
  for (size_t done = 0; done < n;) {
    size_t k = n - done < 480 ? n - done : 480;
    for (size_t i = 0; i < k; i++) chunk[i] = (int16_t)((int32_t)pcm[done + i] * a.volume / 100);
    SDL_QueueAudio(a.play, chunk, (Uint32)(k * 2u));
    done += k;
  }
  return n;
}

static uint32_t spk_buffered_ms(void) {
  if (!a.play || !a.play_rate) return 0;
  return (uint32_t)((uint64_t)(SDL_GetQueuedAudioSize(a.play) / 2u) * 1000u / a.play_rate);
}

static void spk_stop(void) {
  if (a.play) SDL_ClearQueuedAudio(a.play);
}

static void spk_set_volume(uint8_t pct) { a.volume = pct > 100 ? 100 : pct; }

static void pump(uint64_t now_ms) {
  (void)now_ms;
  if (!a.cap || !a.capturing) return;
  for (;;) {
    /* SDL can hand over less than asked for: fill one 20 ms frame across calls. */
    Uint32 got = SDL_DequeueAudio(a.cap, (Uint8 *)a.frame + a.frame_bytes, (Uint32)(sizeof a.frame - a.frame_bytes));
    if (got == 0) return;
    a.frame_bytes += got;
    if (a.frame_bytes < sizeof a.frame) continue;
    a.frame_bytes = 0;
    bool silent = true;
    for (size_t i = 0; i < GADGET_MIC_FRAME_SAMPLES && silent; i++) silent = a.frame[i] == 0;
    a.silent_ms = silent ? a.silent_ms + 20u : 0u;
    if (a.silent_ms >= SIM_SILENT_HINT_MS && !a.hinted) {
      a.hinted = true;
      fprintf(stderr,
              "gadget-sim: the microphone is silent. On macOS, allow your terminal app in System Settings > "
              "Privacy & Security > Microphone (to ask again: tccutil reset Microphone <your terminal's bundle id>, "
              "e.g. com.apple.Terminal), then restart it.\n");
    }
    gadget_event_t ev;
    memset(&ev, 0, sizeof ev);
    ev.type = GADGET_EV_MIC_FRAME;
    ev.u.mic.pcm = a.frame;
    ev.u.mic.samples = (uint16_t)GADGET_MIC_FRAME_SAMPLES;
    sim_post_event(&ev); /* copies the samples */
  }
}

static const sim_audio_backend_t k_backend = {
  mic_start, mic_stop, spk_open, spk_write, spk_buffered_ms, spk_stop, spk_set_volume, pump,
};

const sim_audio_backend_t *sim_audio_sdl_backend(void) { return &k_backend; }
```

Append the P2b block to `firmware/ports/sim/CMakeLists.txt`:

```bash
cat >> firmware/ports/sim/CMakeLists.txt <<'EOF'

# ---- P2b: the LVGL display (headless + SDL window) and SDL audio ----------
if(GADGET_WITH_LVGL)
  set(_sim_lvgl_srcs sim_display_lvgl.c)
  if(GADGET_WITH_SDL)
    list(APPEND _sim_lvgl_srcs sim_sdl.c sim_audio_sdl.c)
  endif()
  add_library(gadget_sim_lvgl STATIC ${_sim_lvgl_srcs})
  target_include_directories(gadget_sim_lvgl PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
  target_link_libraries(gadget_sim_lvgl PUBLIC gadget_ui gadget_core lvgl::lvgl)
  target_compile_definitions(gadget_sim_lvgl PRIVATE _POSIX_C_SOURCE=200809L $<$<PLATFORM_ID:Darwin>:_DARWIN_C_SOURCE>)
  set_target_properties(gadget_sim_lvgl PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
  target_compile_options(gadget_sim_lvgl PRIVATE -Wall -Wextra -Wpedantic -Werror)
  if(GADGET_WITH_SDL)
    target_link_libraries(gadget_sim_lvgl PUBLIC SDL2::SDL2)
    target_compile_definitions(gadget_sim_lvgl PUBLIC GADGET_WITH_SDL=1)
  endif()
  if(GADGET_SNAPSHOT_UPDATE)
    target_compile_definitions(gadget_sim_lvgl PUBLIC GADGET_SNAPSHOT_UPDATE=1)
  endif()
  # Exactly one display and one UI are linked (contract 2.16): drop the null
  # display and the UI stub from gadget-sim and link the LVGL ones.
  get_target_property(_sim_srcs gadget-sim SOURCES)
  list(FILTER _sim_srcs EXCLUDE REGEX "(^|/)(sim_display_null|ui_stub)\\.c$")
  set_property(TARGET gadget-sim PROPERTY SOURCES ${_sim_srcs})
  target_link_libraries(gadget-sim PRIVATE gadget_sim_lvgl)
endif()
EOF
```

Now the simulator can draw, so LVGL becomes the default (contract §2.18):

```bash
perl -0pi -e 's/option\(GADGET_WITH_LVGL ("[^"]*") OFF\)/option(GADGET_WITH_LVGL $1 ON)/' firmware/CMakeLists.txt
grep -n 'option(GADGET_WITH_LVGL' firmware/CMakeLists.txt
```

Expected: the option line ends in `ON)`.

- [ ] **Step 5: Build, and check `gadget-sim` now draws with LVGL**

```bash
cmake -S firmware -B build/host -DCMAKE_BUILD_TYPE=Debug
cmake --build build/host -j10
nm build/host/ports/sim/gadget-sim | grep -c 'ui_screens_create\|sim_sdl_window_create'
```

Expected: build succeeds (macOS `ld` may warn `ignoring duplicate libraries`; harmless) and `2`. A `0` means the UI stub is still linked: see the P2a assumptions table. If the link fails with an undefined `sim_audio_sdl_backend`, P2a defines `GADGET_WITH_SDL` on `gadget-sim` itself and `sim_audio_sdl.c` was not compiled: check `GADGET_WITH_SDL` is ON in `build/host/CMakeCache.txt`.

- [ ] **Step 6: Run the simulator tests**

Run: `ctest --test-dir build/host --output-on-failure -R '^sim\.'`
Expected: `sim.display` (4), `sim.sdl` (5), `sim.audio_sdl` (5), `sim.audio_sdl.no_device` (1), `sim.audio_sdl.open_fails` (1), `sim.window_smoke.fresh` and `sim.window_smoke` all pass. `test_mic_opens_lazily_and_hints_on_silence` runs first in its process: SDL audio is not initialised until the first `mic_start`, and two seconds of the dummy driver's silence print the permission hint exactly once. `sim.window_smoke` runs the whole simulator in window mode on SDL's dummy drivers; if P2a's simulator refuses `--script` without `--headless`, remove that test from the block, note it in the hand-off, and keep the rest.

- [ ] **Step 7: P2a's end-to-end tests still pass with the LVGL UI**

```bash
npm ci
ctest --test-dir build/host --output-on-failure -L "unit|vectors|e2e"
```

Expected: `100% tests passed`. (P2a's e2e scripts now run with the real UI on the test display; a `snapshot` line in one of them would now compare against a golden: if one fails with "no reference", tell P2a — P2b does not own `firmware/tests/e2e/**`.)

- [ ] **Step 8: Manual window check (needs a display, a mic and speakers; Omkar or whoever runs this on a desktop)**

```bash
node tools/fake-host/src/main.ts --port 8810 --code 123456 &
./build/host/ports/sim/gadget-sim --board amoled-175c --host 127.0.0.1:8810 --pair 123456 --name window-check
```

Check: a 466×466 window, masked round; the Maus "curious", then "Hi, I'm Fake Bot"; hold Space → the ring follows your voice (the first time, macOS asks for microphone access for your terminal app; if the mic is silent for a second the simulator prints the permission hint); release → "What's on my calendar today?", "checking your calendar", the reply, the tone plays and the mouth moves; Esc while talking cancels; a mouse click-and-hold also talks; `--board lcd-154` opens a 480×480 window (zoom 2) where the mouse does nothing; closing the window ends the simulator (exit 0). Then `kill %1`.

- [ ] **Step 9: Commit**

```bash
git add firmware/ports/sim firmware/tests firmware/CMakeLists.txt
git commit -m "feat(sim): LVGL display, SDL window mode and SDL audio" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 7: Fixture snapshots (turn screens and copy variants)

**Files:**
- Test: `firmware/tests/test_ui_snapshots.c`
- Modify: `firmware/tests/CMakeLists.txt` (append)
- Create (generated, committed): `firmware/tests/snapshots/{amoled-175c,lcd-154,devkit}/fx_*.png` (18 per board)

**Interfaces:**
- Consumes: `gadget_sim_lvgl` (headless display with the round mask, `sim_display_snapshot`), `gadget_ui`.
- Produces: CTest `ui.snap.fixtures.<board>` (label `snapshot`, run from the repository root, argv[1] = board id); goldens `fx_thinking`, `fx_thinking_working`, `fx_speaking_1..3`, `fx_speaking_page2`, `fx_reply`, `fx_reply_failed`, `fx_setup_need_wifi`, `fx_setup_host_not_found`, `fx_offline_wifi_connecting`, `fx_offline_wifi_failed`, `fx_offline_looking`, `fx_offline_protocol`, `fx_update_verifying`, `fx_update_restarting`, `fx_ask_chosen`, `fx_battery_charging`.

Why fixtures: a scripted host cannot answer a turn because the gadget picks its turn id with `hal_crypto_random` (contract §2.12), so Thinking, Speaking and Reply are drawn here from model fixtures, through the same headless display, seed 1 and 10 ms virtual steps. (Since contract D27, `net_text` replaces `${turn}` with the gadget's last turn id, so a script can now reach these screens; the fixtures are kept by choice, Contract deviations 2.) Setup "need Wi-Fi" is a fixture too: the simulator always reports Wi-Fi connected (contract §2.5), so core starts a fresh simulator at "need code" and no script can reach it.

- [ ] **Step 1: Write the test and register it**

`firmware/tests/test_ui_snapshots.c`:

```c
/* SPDX-License-Identifier: Apache-2.0 */
/* Golden PNGs of the turn screens (thinking, speaking, reply), kept as
 * model fixtures by choice (net_text's ${turn}, contract 2.16 D27, now lets a
 * script reach them end to end), plus every copy variant the simulator
 * scripts skip. Drawn by the real UI through the simulator's headless display
 * (round mask included) on the virtual clock with seed 1. Run from the
 * repository root: argv[1] = board id. */
#include <stdio.h>
#include <string.h>

#include "gadget_board.h"
#include "gadget_ui.h"
#include "lvgl.h"
#include "sim_display.h"
#include "sim_hal.h"
#include "unity.h"

void sim_post_event(const gadget_event_t *ev) { (void)ev; }

static const gadget_board_t *board;
static ui_model_t m;
static uint64_t now;
static char dir[96];

static const char *const k_reply =
  "You have two meetings today: design review at 10 and lunch with Sam at 1. "
  "The review moved to room Atlas, and Sam asked whether you can bring the printed slides. "
  "After that your afternoon is free until the 4 pm call with the Berlin team.";

void setUp(void) {
  sim_display_opts_t o = {true, 1.0f, dir};
  TEST_ASSERT_EQUAL_INT(0, sim_display_init(board, &o));
  TEST_ASSERT_EQUAL_INT(GADGET_OK, ui_init(board, 1));
  uint32_t rev = m.rev;
  memset(&m, 0, sizeof m);
  m.rev = rev;
  strcpy(m.device_id, "gad_b18b86ce1389e46d");
  strcpy(m.bot_name, "Jev");
  strcpy(m.host_name, "Omkar's computer");
  m.battery.present = board->has_battery;
  m.battery.pct = 82;
  now = 0;
}

void tearDown(void) {
  ui_deinit();
  sim_display_deinit();
}

/* Show the model from t = 0 to 3 s in 10 ms steps, then compare. */
static void shot(const char *name) {
  m.rev++;
  for (; now <= 3000; now += 10) {
    sim_display_advance(10);
    m.now_ms = now;
    ui_render(&m);
    ui_tick(now);
  }
  char msg[320];
  int r = sim_display_snapshot(name);
  snprintf(msg, sizeof msg, "%s/%s.png: %s", dir, name,
           r == 2 ? "missing (build with -DGADGET_SNAPSHOT_UPDATE=ON to create it)" : "differs (see the _err.png next to it)");
  TEST_ASSERT_EQUAL_INT_MESSAGE(1, r, msg);
}

static void test_thinking(void) {
  m.screen = UI_SCREEN_THINKING;
  m.maus = UI_MAUS_THINKING;
  strcpy(m.thinking.heard, "What's on my calendar today?");
  shot("fx_thinking");
}

static void test_thinking_working(void) {
  m.screen = UI_SCREEN_THINKING;
  m.maus = UI_MAUS_WORKING;
  strcpy(m.thinking.heard, "What's on my calendar today?");
  strcpy(m.thinking.working, "checking your calendar");
  shot("fx_thinking_working");
}

static void speaking(uint8_t level, const char *name) {
  m.screen = UI_SCREEN_SPEAKING;
  m.maus = UI_MAUS_SPEAKING;
  m.speak_level = level;
  strcpy(m.reply.text, k_reply);
  shot(name);
}
static void test_speaking_1(void) { speaking(1, "fx_speaking_1"); }
static void test_speaking_2(void) { speaking(2, "fx_speaking_2"); }
static void test_speaking_3(void) { speaking(3, "fx_speaking_3"); }

static void test_speaking_second_page(void) {
  m.reply.speak_total_ms = 12000;
  m.reply.speak_elapsed_ms = 7000;
  speaking(2, "fx_speaking_page2");
}

static void test_reply(void) {
  m.screen = UI_SCREEN_REPLY;
  m.maus = UI_MAUS_IDLE;
  strcpy(m.reply.text, k_reply);
  m.reply.final = true;
  shot("fx_reply");
}

static void test_reply_failed(void) {
  m.screen = UI_SCREEN_REPLY;
  m.maus = UI_MAUS_ALERTING;
  m.reply.failed = true;
  strcpy(m.reply.reason, "Didn't catch that");
  shot("fx_reply_failed");
}

/* The simulator always has Wi-Fi (contract 2.5), so no script reaches this step. */
static void test_setup_need_wifi(void) {
  m.screen = UI_SCREEN_SETUP;
  m.maus = UI_MAUS_CURIOUS;
  m.setup.step = UI_SETUP_NEED_WIFI;
  m.host_name[0] = '\0'; /* no challenge yet */
  shot("fx_setup_need_wifi");
}

static void test_setup_host_not_found(void) {
  m.screen = UI_SCREEN_SETUP;
  m.maus = UI_MAUS_CURIOUS;
  m.setup.step = UI_SETUP_HOST_NOT_FOUND;
  m.host_name[0] = '\0'; /* `host auto` found nothing: no challenge yet */
  shot("fx_setup_host_not_found");
}

static void offline(ui_offline_reason_t reason, uint64_t retry_at, const char *name) {
  m.screen = UI_SCREEN_OFFLINE;
  m.maus = UI_MAUS_SLEEPING;
  m.offline.reason = reason;
  strcpy(m.offline.ssid, "Home");
  m.offline.retry_at_ms = retry_at;
  shot(name);
}
static void test_offline_wifi_connecting(void) { offline(UI_OFFLINE_WIFI_CONNECTING, 0, "fx_offline_wifi_connecting"); }
static void test_offline_wifi_failed(void) { offline(UI_OFFLINE_WIFI_FAILED, 0, "fx_offline_wifi_failed"); }
static void test_offline_looking(void) { offline(UI_OFFLINE_HOST_LOOKUP, 0, "fx_offline_looking"); }
static void test_offline_protocol(void) { offline(UI_OFFLINE_PROTOCOL, 9000, "fx_offline_protocol"); }

static void test_update_verifying(void) {
  m.screen = UI_SCREEN_UPDATE;
  m.update.phase = UI_UPDATE_VERIFYING;
  m.update.pct = 100;
  shot("fx_update_verifying");
}

static void test_update_restarting(void) {
  m.screen = UI_SCREEN_UPDATE;
  m.update.phase = UI_UPDATE_RESTARTING;
  m.update.pct = 100;
  shot("fx_update_restarting");
}

static void test_ask_answered_here(void) {
  m.screen = UI_SCREEN_ASK;
  strcpy(m.ask.title, "Run shell command?");
  strcpy(m.ask.body, "ls -la ~/Documents");
  m.ask.n_options = 2;
  gadget_rect_t r[UI_ASK_OPTIONS_MAX] = {{0}};
  ui_layout_ask(board, 2, r);
  strcpy(m.ask.options[0].label, "Allow");
  m.ask.options[0].style = UI_STYLE_ALLOW;
  m.ask.options[0].rect = r[0];
  strcpy(m.ask.options[1].label, "Deny");
  m.ask.options[1].style = UI_STYLE_DENY;
  m.ask.options[1].rect = r[1];
  m.ask.answerable = true;
  m.ask.chosen = 0;
  shot("fx_ask_chosen");
}

static void test_battery_low_and_charging(void) {
  m.screen = UI_SCREEN_IDLE;
  m.maus = UI_MAUS_IDLE;
  m.battery.pct = 8;
  m.battery.charging = true;
  shot("fx_battery_charging");
}

int main(int argc, char **argv) {
  if (argc != 2 || !(board = gadget_board_by_id(argv[1]))) {
    fprintf(stderr, "usage: test_ui_snapshots <board-id>  (run from the repository root)\n");
    return 2;
  }
  snprintf(dir, sizeof dir, "firmware/tests/snapshots/%s", board->id);
  UNITY_BEGIN();
  RUN_TEST(test_thinking);
  RUN_TEST(test_thinking_working);
  RUN_TEST(test_speaking_1);
  RUN_TEST(test_speaking_2);
  RUN_TEST(test_speaking_3);
  RUN_TEST(test_speaking_second_page);
  RUN_TEST(test_reply);
  RUN_TEST(test_reply_failed);
  RUN_TEST(test_setup_need_wifi);
  RUN_TEST(test_setup_host_not_found);
  RUN_TEST(test_offline_wifi_connecting);
  RUN_TEST(test_offline_wifi_failed);
  RUN_TEST(test_offline_looking);
  RUN_TEST(test_offline_protocol);
  RUN_TEST(test_update_verifying);
  RUN_TEST(test_update_restarting);
  RUN_TEST(test_ask_answered_here);
  RUN_TEST(test_battery_low_and_charging);
  return UNITY_END();
}
```

Append to `firmware/tests/CMakeLists.txt`:

```bash
cat >> firmware/tests/CMakeLists.txt <<'EOF'

# ---- P2b: fixture snapshots (turn screens and copy variants) --------------
if(GADGET_WITH_LVGL AND GADGET_BUILD_SIM) # gadget_sim_lvgl lives in ports/sim
  add_executable(test_ui_snapshots test_ui_snapshots.c)
  target_link_libraries(test_ui_snapshots PRIVATE gadget_sim_lvgl unity::framework)
  target_compile_options(test_ui_snapshots PRIVATE -Wall -Wextra -Werror)
  foreach(board amoled-175c lcd-154 devkit)
    add_test(NAME ui.snap.fixtures.${board} COMMAND test_ui_snapshots ${board}
             WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/..)
    set_tests_properties(ui.snap.fixtures.${board} PROPERTIES LABELS snapshot TIMEOUT 120)
  endforeach()
endif()
EOF
```

- [ ] **Step 2: Watch it fail (no goldens yet)**

```bash
cmake -S firmware -B build/host && cmake --build build/host -j10
ctest --test-dir build/host --output-on-failure -R '^ui\.snap\.fixtures\.'
```

Expected: FAIL: `firmware/tests/snapshots/amoled-175c/fx_thinking.png: missing (build with -DGADGET_SNAPSHOT_UPDATE=ON to create it)`.

- [ ] **Step 3: Write the goldens with a snapshot-update build**

```bash
cmake -S firmware -B build/snap -DCMAKE_BUILD_TYPE=Debug -DGADGET_SNAPSHOT_UPDATE=ON
cmake --build build/snap -j10
ctest --test-dir build/snap --output-on-failure -R '^ui\.snap\.fixtures\.'
for b in amoled-175c lcd-154 devkit; do echo "$b $(ls firmware/tests/snapshots/$b/fx_*.png | wc -l)"; done
```

Expected: 3 tests pass; `18` PNGs per board.

- [ ] **Step 4: Look at every golden before trusting it**

Open each PNG (an image viewer, or the Read tool). On amoled-175c the corners are black (round mask) and nothing touches the circle's edge. Check: `fx_thinking` — Maus with eyes from the thinking pool, "What's on my calendar today?"; `fx_thinking_working` — plus "checking your calendar" in green; `fx_speaking_1/2/3` — the same reply's first page with the mouth opening wider from 1 to 3; `fx_speaking_page2` — later lines of the reply; `fx_reply` — the reply's last lines; `fx_reply_failed` — "Didn't catch that" in red; `fx_setup_need_wifi` — "Connect me to Wi-Fi with the installer." and `gad_b18b86ce1389e46d`; `fx_setup_host_not_found` — the host-not-found copy and `gad_b18b86ce1389e46d` (no host line: no challenge yet); `fx_offline_*` — "Connecting to Wi-Fi…", "Can't join Home.", "Looking for MausBot…", "MausBot didn't accept me." + "Retrying in 6 s", each with "Omkar's computer" on a dim line under it; `fx_update_*` — "Checking update…" / "Restarting…" with a full bar; `fx_ask_chosen` — "Allow" bright, "Deny" dimmed; `fx_battery_charging` — the battery arc at the top edge in green (amoled-175c and lcd-154), nothing on devkit. On boards with a battery the other fixtures show the arc at 82 %.

- [ ] **Step 5: Normal build passes, and the goldens are repeatable**

```bash
ctest --test-dir build/host --output-on-failure -R '^ui\.snap\.fixtures\.'
git add firmware/tests/snapshots
ctest --test-dir build/snap -R '^ui\.snap\.fixtures\.' > /dev/null
git diff --quiet -- firmware/tests/snapshots && echo "goldens repeatable"
```

Expected: 3 tests pass, then `goldens repeatable`: regenerating writes byte-identical PNGs, so the working tree still matches what was just staged (verified on darwin-arm64: two update runs gave the same MD5 for all 54 files). `git status --porcelain` is the wrong check here: it lists every staged PNG as `A ` even when nothing changed.

- [ ] **Step 6: Commit**

```bash
git add firmware/tests/test_ui_snapshots.c firmware/tests/CMakeLists.txt firmware/tests/snapshots
git commit -m "test(ui): fixture snapshots for turn screens and copy variants" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 8: Snapshots through the whole simulator

**Files:**
- Create: `firmware/tests/scripts/snap_screens.txt`, `firmware/tests/scripts/snap_update_amoled-175c.txt`, `firmware/tests/scripts/snap_update_lcd-154.txt`, `firmware/tests/scripts/snap_update_devkit.txt`
- Modify: `firmware/tests/CMakeLists.txt` (append)
- Create (generated, committed): `firmware/tests/snapshots/<board>/{setup_need_code,setup_pairing,setup_pairing_host,setup_bad_code,setup_device_limit,idle,listening,listening_countdown,ask_permission,ask_question,ask_elsewhere,card,image,post_toast,offline_in_use,offline_unreachable,update_receiving}.png`

**Interfaces:**
- Consumes: `gadget-sim` CLI and script grammar (contract §2.16: `--headless`, `--host script`, `--seed`, `--battery`, `--state-dir`, `--snapshot-dir`, `--script`; `model`, `expect`, `snapshot`, `wait`, `console`, `talk_down`/`talk_up`, `cancel`, `net_open`, `net_text`, `net_binary`, `net_close`); protocol frames of spec §4.3–§4.8; pinned values of contract §1.7 (nonce, `host_id`, test key `t1`).
- Produces: CTest `ui.snap.<board>` and `ui.snap.<board>.update` (label `snapshot`; each with a `.fresh` fixture that empties its state folder under `build/<dir>/snap-state/` and writes a `storage.json` holding only the RFC key of contract §1.7 as `dev_key`, so the device id is always `gad_b18b86ce1389e46d`).

Three things the gadget does decide what the scripts may assume (all three found by running Tasks 1–8 against P2a's reference core): core generates a random device key in an empty state folder (`hal_crypto_keygen`; `--seed` does not touch it), hence the seeded `dev_key`; the simulator always reports Wi-Fi connected, so a fresh simulator starts at "need code" and "need Wi-Fi" is a fixture (Task 7); and the gadget closes the connection itself when an `error` frame arrives, using up the one `GADGET_EV_WS_CLOSED` a connection may deliver (contract §2.5), so the script sends no `net_close` after an `error` (the next `console pair` or `talk_down` reconnects).

The update scripts carry real offers: a 100-byte image (bytes `0x00..0x63`, SHA-256 `bce0aff19cf5aa6a7469a30d61d04e4376e4bbf6381052ee9e7f33925c954d52`) signed with `t1` over `openmausbot-gadget/1\nfirmware\n<board>\n1.1.0\n100\n<sha256>`. Each signature was produced and verified with Node `crypto` against `keys/test-t1.pub.b64`'s key on 2026-10-04 (the same key verifies contract §1.7's firmware vector).

- [ ] **Step 1: Write the scripts and register them**

`firmware/tests/scripts/snap_screens.txt`:

```text
# SPDX-License-Identifier: Apache-2.0
# Screens reached through the whole simulator (core + firmware/ui + the
# headless LVGL display) with the host side scripted (--host script).
# CTest ui.snap.<board> runs it for amoled-175c, lcd-154 and devkit from a
# fresh --state-dir holding only the RFC key (device id gad_b18b86ce1389e46d);
# goldens live in firmware/tests/snapshots/<board>/.
# Turn screens (thinking, speaking, reply) are drawn by test_ui_snapshots
# from model fixtures (fx_*.png), by choice: net_text's ${turn} (contract
# 2.16, D27) now lets a script reach them end to end. Setup "need Wi-Fi"
# never shows here (the simulator always has Wi-Fi), so it is a fixture too.
# After an `error` frame the gadget closes the connection itself, so the
# script sends no net_close there.
# The script sends an unknown op ("x-keepalive") before long waits: any
# inbound frame resets the gadget's 45 s liveness timer, and receivers
# ignore unknown ops (PROTOCOL.md 4.1).

# ---- Setup: nothing stored but the device key -----------------------------
model screen setup
console wifi "Home" "password123"
model setup.step need_code
wait 2000
snapshot setup_need_code

# ---- Setup: a wrong code ----------------------------------------------------
console pair 123456
model setup.step pairing
wait 300
snapshot setup_pairing
net_open
expect hello
net_text {"op":"challenge","nonce":"AAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8=","host_id":"000102030405060708090a0b0c0d0e0f","host_name":"Omkar's computer"}
expect prove
model host_name ~Omkar
wait 300
snapshot setup_pairing_host
net_text {"op":"error","code":"bad_code","message":"That code is not valid."}
model setup.step bad_code
wait 1500
snapshot setup_bad_code

# ---- Setup: too many devices, then the 10 s retry pairs -------------------
console pair 654321
wait 300
net_open
expect hello
net_text {"op":"challenge","nonce":"AAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8=","host_id":"000102030405060708090a0b0c0d0e0f","host_name":"Omkar's computer"}
expect prove
net_text {"op":"error","code":"device_limit","message":"MausBot has too many devices."}
model setup.step device_limit
wait 1000
snapshot setup_device_limit
wait 10000
net_open
expect hello
net_text {"op":"challenge","nonce":"AAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8=","host_id":"000102030405060708090a0b0c0d0e0f","host_name":"Omkar's computer"}
expect prove
net_text {"op":"ready","session":"s_0123456789ab","bot":{"id":"b_jev","name":"Jev"},"settings":{"speak_pushes":false}}
model screen idle
model bot_name Jev
wait 2000
snapshot idle

# ---- Listening, the countdown, CANCEL drops the recording -----------------
talk_down
wait 600
model screen listening
snapshot listening
wait 20000
net_text {"op":"x-keepalive"}
wait 20000
net_text {"op":"x-keepalive"}
wait 15400
snapshot listening_countdown
cancel
expect voice.drop
model screen idle
net_text {"op":"x-keepalive"}

# ---- Asks: permission, a question, one only a computer or phone can answer -
net_text {"op":"ask","id":"a_0000000000000001","kind":"permission","title":"Run shell command?","body":"ls -la ~/Documents","options":[{"id":"allow","label":"Allow","style":"allow"},{"id":"deny","label":"Deny","style":"deny"}]}
model screen ask
wait 1000
snapshot ask_permission
net_text {"op":"ask.close","id":"a_0000000000000001","reason":"answered"}
model screen idle
net_text {"op":"ask","id":"a_0000000000000002","kind":"question","title":"Which room?","body":"For tomorrow's design review.","options":[{"id":"atlas","label":"Atlas"},{"id":"borealis","label":"Borealis"},{"id":"cosmos","label":"Cosmos"}]}
model screen ask
wait 1000
snapshot ask_question
net_text {"op":"ask.close","id":"a_0000000000000002","reason":"withdrawn"}
model screen idle
net_text {"op":"ask","id":"a_0000000000000003","kind":"question","title":"Pick the slides to keep","body":"Seven slides, choose any.","options":[]}
model ask.answerable false
wait 1000
snapshot ask_elsewhere
net_text {"op":"ask.close","id":"a_0000000000000003","reason":"expired"}
model screen idle

# ---- A card, an 8x8 image (4 green rows, 4 white rows), a push -------------
net_text {"op":"card","id":"c_build","title":"Build finished","body":"All 312 tests passed in 4 min 12 s.","ttl_s":0}
model screen card
wait 1000
snapshot card
net_text {"op":"card.close","id":"c_build"}
model screen idle
net_text {"op":"image.begin","id":"i_flag","stream":1,"w":8,"h":8,"ttl_s":0}
net_binary 0301902e902e902e902e902e902e902e902e902e902e902e902e902e902e902e902e902e902e902e902e902e902e902e902e902e902e902e902e902e902e902e902effffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff
net_text {"op":"image.end","stream":1}
model screen image
wait 1000
snapshot image
net_text {"op":"card.close","id":"i_flag"}
model screen idle
net_text {"op":"post","id":"p_brief","bot":{"id":"b_jev","name":"Jev"},"kind":"routine","text":"Morning brief is ready: 3 meetings, 2 reviews waiting.","speak":false}
model toast.visible true
wait 500
snapshot post_toast
model toast.visible false 30000
net_text {"op":"x-keepalive"}

# ---- Offline: replaced elsewhere, TALK takes it back; then a dropped session
net_text {"op":"error","code":"replaced","message":"Another connection took over."}
model screen offline
model offline.reason in_use_elsewhere
wait 1000
snapshot offline_in_use
talk_down
wait 400
talk_up
wait 300
net_open
expect hello
net_text {"op":"challenge","nonce":"AAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8=","host_id":"000102030405060708090a0b0c0d0e0f","host_name":"Omkar's computer"}
expect prove
net_text {"op":"ready","session":"s_0123456789ac","bot":{"id":"b_jev","name":"Jev"},"settings":{"speak_pushes":false}}
model screen idle
net_close
model screen offline
model offline.reason host_unreachable
wait 500
snapshot offline_unreachable
```

`firmware/tests/scripts/snap_update_amoled-175c.txt`:

```text
# SPDX-License-Identifier: Apache-2.0
# The Update screen on amoled-175c (CTest ui.snap.amoled-175c.update, fresh --state-dir
# holding only the RFC key).
# The offer is a real one for this board: a 100-byte image (bytes 0x00..0x63,
# SHA-256 below) signed with the test key t1 (keys/test-t1.key.hex, compiled
# into GADGET_TEST_KEYS builds) over
#   openmausbot-gadget/1\nfirmware\namoled-175c\n1.1.0\n100\n<sha256>
# The script then sends the first 50 bytes, so the screen shows 50 %.
console wifi "Home" "password123"
console pair 123456
wait 300
net_open
expect hello
net_text {"op":"challenge","nonce":"AAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8=","host_id":"000102030405060708090a0b0c0d0e0f","host_name":"Omkar's computer"}
expect prove
net_text {"op":"ready","session":"s_0123456789ab","bot":{"id":"b_jev","name":"Jev"},"settings":{"speak_pushes":false}}
model screen idle
net_text {"op":"fw.offer","stream":7,"board":"amoled-175c","version":"1.1.0","size":100,"sha256":"bce0aff19cf5aa6a7469a30d61d04e4376e4bbf6381052ee9e7f33925c954d52","sig":"MEQCIAXnr3GgiV/05foNgHX4bqQCaX660cAvnPwSIC5xYrnIAiBkVtPRbNKqHfMz1x/d9grFe5bH92SpwHW7UWVjEQs4kg==","key_id":"t1"}
expect fw.ready
net_binary 040700000000000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f202122232425262728292a2b2c2d2e2f3031
model screen update
model update.pct 50
wait 500
snapshot update_receiving
```

`firmware/tests/scripts/snap_update_lcd-154.txt`:

```text
# SPDX-License-Identifier: Apache-2.0
# The Update screen on lcd-154 (CTest ui.snap.lcd-154.update, fresh --state-dir
# holding only the RFC key).
# The offer is a real one for this board: a 100-byte image (bytes 0x00..0x63,
# SHA-256 below) signed with the test key t1 (keys/test-t1.key.hex, compiled
# into GADGET_TEST_KEYS builds) over
#   openmausbot-gadget/1\nfirmware\nlcd-154\n1.1.0\n100\n<sha256>
# The script then sends the first 50 bytes, so the screen shows 50 %.
console wifi "Home" "password123"
console pair 123456
wait 300
net_open
expect hello
net_text {"op":"challenge","nonce":"AAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8=","host_id":"000102030405060708090a0b0c0d0e0f","host_name":"Omkar's computer"}
expect prove
net_text {"op":"ready","session":"s_0123456789ab","bot":{"id":"b_jev","name":"Jev"},"settings":{"speak_pushes":false}}
model screen idle
net_text {"op":"fw.offer","stream":7,"board":"lcd-154","version":"1.1.0","size":100,"sha256":"bce0aff19cf5aa6a7469a30d61d04e4376e4bbf6381052ee9e7f33925c954d52","sig":"MEUCIQCim4bgWq9EuOsKklOr9fP8IAjs1rO8aOvElRgfR3nCQwIgWDYXtzYo4Th1X5f4APSrU9G4RRQ23+4MjTSxbIJBJXs=","key_id":"t1"}
expect fw.ready
net_binary 040700000000000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f202122232425262728292a2b2c2d2e2f3031
model screen update
model update.pct 50
wait 500
snapshot update_receiving
```

`firmware/tests/scripts/snap_update_devkit.txt`:

```text
# SPDX-License-Identifier: Apache-2.0
# The Update screen on devkit (CTest ui.snap.devkit.update, fresh --state-dir
# holding only the RFC key).
# The offer is a real one for this board: a 100-byte image (bytes 0x00..0x63,
# SHA-256 below) signed with the test key t1 (keys/test-t1.key.hex, compiled
# into GADGET_TEST_KEYS builds) over
#   openmausbot-gadget/1\nfirmware\ndevkit\n1.1.0\n100\n<sha256>
# The script then sends the first 50 bytes, so the screen shows 50 %.
console wifi "Home" "password123"
console pair 123456
wait 300
net_open
expect hello
net_text {"op":"challenge","nonce":"AAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8=","host_id":"000102030405060708090a0b0c0d0e0f","host_name":"Omkar's computer"}
expect prove
net_text {"op":"ready","session":"s_0123456789ab","bot":{"id":"b_jev","name":"Jev"},"settings":{"speak_pushes":false}}
model screen idle
net_text {"op":"fw.offer","stream":7,"board":"devkit","version":"1.1.0","size":100,"sha256":"bce0aff19cf5aa6a7469a30d61d04e4376e4bbf6381052ee9e7f33925c954d52","sig":"MEUCIQDyP9492FEi0yvp4vcGU0WjRoKRVcqqz4keP58CfNqhbgIgbl3bQV/h6EQ8xsh0BFbDnfCuJcX6qeo8mqeC5khJTbI=","key_id":"t1"}
expect fw.ready
net_binary 040700000000000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f202122232425262728292a2b2c2d2e2f3031
model screen update
model update.pct 50
wait 500
snapshot update_receiving
```

Append to `firmware/tests/CMakeLists.txt`:

```bash
cat >> firmware/tests/CMakeLists.txt <<'EOF'

# ---- P2b: snapshots through the whole simulator, host scripted -----------
# ui.snap.<board> runs scripts/snap_screens.txt, ui.snap.<board>.update runs
# scripts/snap_update_<board>.txt. Each starts from a state folder that holds
# only the RFC key of contract 1.7 as dev_key, so every run shows the same
# device id (gad_b18b86ce1389e46d); --seed seeds only the UI's PRNG.
# Gated on the options, not on `TARGET gadget-sim` (see the sim tests above).
if(GADGET_WITH_LVGL AND GADGET_BUILD_SIM)
  set(_rfc_dev_key c9afa9d845ba75166b5c215767b1d6934e50c3db36e89b127b8a622b120f6721)
  foreach(board amoled-175c lcd-154 devkit)
    foreach(run screens update)
      if(run STREQUAL "screens")
        set(_name ui.snap.${board})
        set(_script firmware/tests/scripts/snap_screens.txt)
      else()
        set(_name ui.snap.${board}.update)
        set(_script firmware/tests/scripts/snap_update_${board}.txt)
      endif()
      set(_state ${CMAKE_BINARY_DIR}/snap-state/${board}-${run})
      add_test(NAME ${_name}.fresh
               COMMAND ${CMAKE_COMMAND} -DDIR=${_state} -DDEV_KEY=${_rfc_dev_key}
                       -P ${CMAKE_CURRENT_SOURCE_DIR}/fresh_dir.cmake)
      set_tests_properties(${_name}.fresh PROPERTIES FIXTURES_SETUP ${_name}.state LABELS snapshot)
      add_test(NAME ${_name}
               COMMAND $<TARGET_FILE:gadget-sim> --board ${board} --headless --host script --seed 1 --battery 82
                       --state-dir ${_state} --snapshot-dir firmware/tests/snapshots/${board} --script ${_script}
               WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/..)
      set_tests_properties(${_name} PROPERTIES FIXTURES_REQUIRED ${_name}.state LABELS snapshot TIMEOUT 300)
    endforeach()
  endforeach()
endif()
EOF
```

- [ ] **Step 2: Watch them fail (no goldens yet)**

```bash
cmake -S firmware -B build/host && cmake --build build/host -j10
ctest --test-dir build/host --output-on-failure -R '^ui\.snap\.(amoled-175c|lcd-154|devkit)'
```

Expected: every `ui.snap.<board>` and `ui.snap.<board>.update` run fails at its first `snapshot` line (no reference); every `.fresh` test passes. Then make sure no snapshot test went missing (for example through a configure that skipped the block):

```bash
ctest --test-dir build/host -N -L snapshot | tail -1
```

Expected: `Total Tests: 15` (3 `ui.snap.fixtures.<board>`, 3 `ui.snap.<board>`, 3 `ui.snap.<board>.update` and their 6 `.fresh` fixtures).

- [ ] **Step 3: Write the goldens**

```bash
cmake -S firmware -B build/snap -DCMAKE_BUILD_TYPE=Debug -DGADGET_SNAPSHOT_UPDATE=ON
cmake --build build/snap -j10
ctest --test-dir build/snap --output-on-failure -L snapshot
for b in amoled-175c lcd-154 devkit; do echo "$b $(ls firmware/tests/snapshots/$b/*.png | wc -l)"; done
```

Expected: every snapshot test passes; `35` PNGs per board (18 fixtures + 16 screens + 1 update).

If a `model` or `expect` line times out, rerun that board by hand with `--trace` to see every frame (the state folder gets the same RFC key as the CTest fixture; LVGL does not create the PNG folder, so make it first):

```bash
cmake -DDIR=/tmp/snapdebug -DDEV_KEY=c9afa9d845ba75166b5c215767b1d6934e50c3db36e89b127b8a622b120f6721 -P firmware/tests/fresh_dir.cmake
mkdir -p /tmp/snapdebug-png
./build/snap/ports/sim/gadget-sim --board lcd-154 --headless --host script --seed 1 --battery 82 \
  --state-dir /tmp/snapdebug --snapshot-dir /tmp/snapdebug-png --script firmware/tests/scripts/snap_screens.txt --trace
```

Compare the frames with spec §4.3–§4.8 and contract §2.16. The script is P2b's to fix (for example a different wait before `net_open`); if core breaks the contract (for example it never reaches `setup.step need_code` from a state folder that holds only the key), report it to P2a and do not edit core.

- [ ] **Step 4: Look at every new golden**

On each board: `setup_need_code` the Pair-a-gadget copy with `→` arrows (one page of it) and the device id `gad_b18b86ce1389e46d` (on every Setup golden); `setup_pairing` "Pairing…"; `setup_pairing_host` "Pairing with Omkar's computer…"; `setup_bad_code` "That code didn't work…"; `setup_device_limit` "MausBot has too many devices…" (the page showing at that moment). On these two the host-name line is absent with P2a's reference core, which publishes the challenge's `host_name` only while that connection is open (see Hand-off notes, P2a); if core keeps it, "Omkar's computer" shows dim above the device id. `idle` "Hi, I'm Jev" with the battery arc at 82 % at the top edge (not on devkit); `listening` the ring; `listening_countdown` a big digit; `ask_permission` Allow (green) and Deny (red) — with `TALK`/`CANCEL` under them on lcd-154 and devkit; `ask_question` three neutral buttons on amoled-175c, "Answer on your computer or phone" on the button boards; `ask_elsewhere` the note; `card` "Build finished"; `image` an 8×8 block, top half green, bottom half white, centred; `post_toast` "Jev" over the morning-brief text; `offline_in_use` "In use elsewhere / Press TALK to use it here." with the stored host name "Omkar's computer" under it; `offline_unreachable` "Can't reach Omkar's computer." with the Remote access and Windows hints and a retry line; `update_receiving` "Updating… 50%" with a half bar.

- [ ] **Step 5: Normal build passes, and the goldens are repeatable**

```bash
ctest --test-dir build/host --output-on-failure -L snapshot
git add firmware/tests/snapshots
ctest --test-dir build/snap -L snapshot > /dev/null
git diff --quiet -- firmware/tests/snapshots && echo "goldens repeatable"
```

Expected: all 15 snapshot tests pass, then `goldens repeatable` (a second update run changes no PNG: with the seeded key the device id no longer changes between runs).

- [ ] **Step 6: Commit**

```bash
git add firmware/tests/scripts firmware/tests/CMakeLists.txt firmware/tests/snapshots
git commit -m "test(ui): simulator snapshots of every scripted screen on three board sizes" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 9: CI jobs, the art-regen workflow and the UI notes

**Files:**
- Modify: `.github/workflows/ci.yml` (append jobs `ui` and `art-drift`)
- Create: `.github/workflows/art-regen.yml`, `firmware/ui/README.md`

**Interfaces:**
- Consumes: contract §1.6 job ids and duties; Tasks 2–8 commands.
- Produces: CI job `ui` (ubuntu-24.04, Node 24, root `npm ci`, `libsdl2-dev`, desktop build with LVGL on, `ctest -L "unit|vectors|e2e|snapshot"`, `*_err.png` uploaded on failure), CI job `art-drift` (ubuntu-24.04, Node 24; `npm ci && npm test && npm run art`, `test -z "$(git status --porcelain -- firmware/ui/art)"`, `npm run budget`; artifact `art-linux-x64` on failure), workflow `art-regen` (`workflow_dispatch` only; artifact `art-and-snapshots-linux-x64` with `firmware/ui/art` and `firmware/tests/snapshots`).

- [ ] **Step 1: Append the CI jobs**

```bash
cat >> .github/workflows/ci.yml <<'EOF'

  ui:
    runs-on: ubuntu-24.04
    steps:
      - uses: actions/checkout@v7.0.1
      - uses: actions/setup-node@v7.0.0
        with:
          node-version: 24
      - name: Root tools (the e2e label runs the fake host)
        run: npm ci
      - name: SDL2 for the simulator's window mode
        run: sudo apt-get update && sudo apt-get install -y libsdl2-dev
      - name: Configure (GADGET_WITH_LVGL defaults to ON)
        run: cmake -S firmware -B build/host -DCMAKE_BUILD_TYPE=Debug
      - name: Build
        run: cmake --build build/host -j4
      - name: Unit, vector, end-to-end and snapshot tests
        run: ctest --test-dir build/host --output-on-failure -L "unit|vectors|e2e|snapshot"
      - name: Upload snapshot differences
        if: failure()
        uses: actions/upload-artifact@v7
        with:
          name: snapshot-diffs
          path: firmware/tests/snapshots/**/*_err.png
          if-no-files-found: ignore

  art-drift:
    runs-on: ubuntu-24.04
    steps:
      - uses: actions/checkout@v7.0.1
      - uses: actions/setup-node@v7.0.0
        with:
          node-version: 24
      - name: Regenerate the Maus art (linux-x64 is the reference)
        working-directory: tools/art
        run: npm ci && npm test && npm run art
      - name: Committed art matches
        run: test -z "$(git status --porcelain -- firmware/ui/art)"
      - name: Art byte budget
        working-directory: tools/art
        run: npm run budget
      - name: Upload the regenerated art
        if: failure()
        uses: actions/upload-artifact@v7
        with:
          name: art-linux-x64
          path: firmware/ui/art
EOF
```

- [ ] **Step 2: Create `.github/workflows/art-regen.yml`**

```yaml
# SPDX-License-Identifier: Apache-2.0
# Manual only: regenerate the Maus art and every snapshot golden on linux-x64,
# the reference platform for the art-drift job. Download the artifact and
# commit both folders unchanged (see firmware/ui/README.md).
name: art-regen
on:
  workflow_dispatch:
jobs:
  regen:
    runs-on: ubuntu-24.04
    steps:
      - uses: actions/checkout@v7.0.1
      - uses: actions/setup-node@v7.0.0
        with:
          node-version: 24
      - name: SDL2 for the simulator build
        run: sudo apt-get update && sudo apt-get install -y libsdl2-dev
      - name: Regenerate the art
        working-directory: tools/art
        run: npm ci && npm run art && npm run budget
      - name: Configure a snapshot-update build
        run: cmake -S firmware -B build/snap -DGADGET_SNAPSHOT_UPDATE=ON
      - name: Build
        run: cmake --build build/snap -j4
      - name: Rewrite every snapshot
        run: ctest --test-dir build/snap --output-on-failure -L snapshot
      - name: Upload art and snapshots
        uses: actions/upload-artifact@v7
        with:
          name: art-and-snapshots-linux-x64
          path: |
            firmware/ui/art
            firmware/tests/snapshots
```

- [ ] **Step 3: Both workflows parse**

```bash
ruby -e 'require "yaml"; puts YAML.load_file(".github/workflows/ci.yml")["jobs"].keys.inspect'
ruby -e 'require "yaml"; puts YAML.load_file(".github/workflows/art-regen.yml")["jobs"].keys.inspect'
```

Expected: the first list ends with `"ui", "art-drift"` after the jobs P1 and P2a added (for example `["protocol", "fake-host", "host-c", "ui", "art-drift"]`); the second is `["regen"]`.

- [ ] **Step 4: Write `firmware/ui/README.md`** (regeneration, the white-eye fallback, and the checks that need hardware; P2c's `docs/hardware-checklist.md` links it)

```markdown
# firmware/ui

The gadget's screens on LVGL 9.6.0. Core publishes a `ui_model_t`; this
library draws it (`gadget_ui.h`: `ui_init`, `ui_render`, `ui_tick`,
`ui_deinit`). It includes `lvgl.h` and core's headers only and is built twice:
as an ESP-IDF component by `firmware/ports/esp32` and as the desktop static
library `gadget_ui` for the simulator and the tests.

| File | Does |
|---|---|
| `ui.c` | The API: root object, render cache, tick |
| `ui_screens.c` | Every screen of spec §5.5: captions, ask, card, image, update, battery, toast, listening ring |
| `ui_maus.c`, `ui_maus_engine.c` | The Maus: three image layers moved by whole pixels; the engine picks expressions, blinks, mouths and offsets from the seeded PRNG |
| `ui_pager.c` | Captions that show whole lines and page through long text |
| `ui_copy.c` | The exact English copy (contract §2.15), pure C |
| `ui_metrics.c`, `ui_theme.h` | Layout per board size and the screen colours |
| `ui_lv_compat.h`, `ui_lv_requirements.h` | LVGL 9.5/9.6 flag wrappers and the compile-time LVGL configuration check |
| `lv_conf.h` | Simulator/test LVGL configuration (the ESP32 uses `CONFIG_LV_*`) |
| `art/` | Generated by `tools/art` (`npm run art`); do not edit |
| `fonts/` | Generated by `tools/art` (`npm run fonts`) from Montserrat Medium; do not edit |

## Regenerating

- Art: `cd tools/art && npm ci && npm run art && npm run budget`. CI's
  `art-drift` job regenerates on linux-x64 and fails on any difference. resvg
  can differ in the last bit between darwin-arm64 and linux-x64; when
  `art-drift` fails only for that reason, run the `art-regen` workflow and
  commit its `firmware/ui/art` and `firmware/tests/snapshots` folders unchanged.
- Fonts: `cd tools/art && npm run fonts`. It uses LVGL 9.6.0's
  Montserrat from a configured build tree (`build/host` or `build/snap`), or
  downloads that one file from LVGL's `v9.6.0` tag, and checks its SHA-256
  either way; `-- --ttf <path>` takes a local copy instead.
- Snapshot goldens: `cmake -S firmware -B build/snap -DGADGET_SNAPSHOT_UPDATE=ON && cmake --build build/snap -j10 && ctest --test-dir build/snap -L snapshot`,
  then look at every changed PNG before committing.

## White-eye fallback

Eye frames are A8 images drawn in the image recolor colour (white). The
`ui.screens` test checks that an opaque eye pixel lands as `0xFFFF` in the
simulator. If an ESP32 panel shows black eyes, set `"eye_format": "RGB565A8"`
in `tools/art/source/palette.json`, run `npm run art`, and the budget check
switches to the fallback limits (800 KiB / 320 KiB).

## Checks that need hardware

Run on each board after flashing (`docs/hardware-checklist.md` links here):

1. The Maus's eyes are white, not black (A8 recolor on the device's draw path).
2. The body gradient shows no banding worse than the simulator's snapshot, and colours are not swapped (red/blue or byte order of RGB565 on the panel).
3. Idle bob and alerting jitter look smooth; note the frame rate with `CONFIG_LV_USE_PERF_MONITOR=y` (target: no visible stutter at the 33 ms refresh period).
4. On the round 1.75" panels no text touches the glass edge on Setup, Offline, Ask and the toast.
5. Accented text renders (`console say "Café über naïve"` → the reply shows é, ü, ï), and `…` and `→` render on the Setup screen.
6. The speaking mouth opens and closes with the reply's audio, and closes when playback stops.
7. The battery arc at the top edge follows the charger being plugged and unplugged (green while charging, red at 10 % or less).
```

- [ ] **Step 5: Commit**

```bash
git add .github/workflows/ci.yml .github/workflows/art-regen.yml firmware/ui/README.md
git commit -m "ci(ui): ui and art-drift jobs, art-regen workflow, UI notes" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 10: Branch verification

**Files:** none changed (fix and recommit in the owning task if anything fails).

**Interfaces:**
- Consumes: everything above.
- Produces: a verified `p2b-ui` branch and the hand-off notes for P2c, P2d and Omkar.

- [ ] **Step 1: Node tools**

```bash
npm ci && npm test
cd tools/art && npm ci && npm test && npm run art && cd ../..
test -z "$(git status --porcelain -- firmware/ui/art)" && echo "art matches"
(cd tools/art && npm run budget)
```

Expected: P1's tests pass; `# pass 22`; `art matches`; both profiles within budget.

- [ ] **Step 2: The full desktop build and every test label**

```bash
rm -rf build/host
cmake -S firmware -B build/host -DCMAKE_BUILD_TYPE=Debug
cmake --build build/host -j10
ctest --test-dir build/host --output-on-failure -L "unit|vectors|e2e|snapshot"
```

Expected: `100% tests passed` — P2a's `core.*`, `vectors.c`, `e2e.*` plus P2b's `ui.*`, `sim.*`, `ui.snap.*`.

- [ ] **Step 3: The configurations other jobs and people use**

```bash
cmake -S firmware -B build/nolvgl -DGADGET_WITH_LVGL=OFF && cmake --build build/nolvgl -j10 && ctest --test-dir build/nolvgl -L "unit|vectors"
cmake -S firmware -B build/nosim -DGADGET_BUILD_SIM=OFF && cmake --build build/nosim -j10 && ctest --test-dir build/nosim -L "unit|vectors|snapshot"
cmake -S firmware -B build/nosdl -DGADGET_WITH_SDL=OFF && cmake --build build/nosdl -j10 && ctest --test-dir build/nosdl -L "unit|snapshot"
cmake -S firmware -B build/asan -DGADGET_SANITIZE=ON && cmake --build build/asan -j10 && ctest --test-dir build/asan -R '^(ui|sim)\.'
```

Expected: all pass. (`build/nolvgl` is P2a's `host-c` configuration and must not need LVGL or SDL; `build/nosim` (contract §2.18 `GADGET_BUILD_SIM=OFF`) configures without `gadget-sim` and runs only the `core.*` and `ui.*` unit tests, because every block that needs the simulator is gated on `GADGET_WITH_LVGL AND GADGET_BUILD_SIM`; `build/nosdl` builds a headless-only simulator; in the ASan/UBSan build `gadget_ui` passes the sanitizer flags `PUBLIC`ly, so every UI and simulator test runs sanitized — verified clean on darwin-arm64.)

- [ ] **Step 4: Static checks**

```bash
for f in $(git diff --name-only --diff-filter=A p2a-core..HEAD -- '*.c' '*.h' '*.ts' '*.cmake' '*.yml' | grep -v '^firmware/ui/fonts/'); do head -3 "$f" | grep -q 'SPDX-License-Identifier: Apache-2.0' || echo "missing SPDX: $f"; done
git grep -n 'lv_obj_add_flag\|lv_obj_remove_flag\|lv_obj_has_flag' -- firmware/ui firmware/ports/sim | grep -v ui_lv_compat.h
git grep -n 'define LV_USE_CUSTOM_FONT_DEFAULT' -- firmware
git grep -n -i '007a45' -- firmware
git log --oneline p2a-core..p2b-ui
```

Expected: no "missing SPDX" line; no flag call outside `ui_lv_compat.h`; no custom default font (the grep looks for a definition, so `lv_conf.h`'s comment that names the macro does not count); `#007a45` absent from firmware; nine commits (Tasks 1–9).

- [ ] **Step 5: Stop**

Do not push. Report the branch, the commit list, and the hand-off notes below.

**Verified while writing this plan (2026-10-04, darwin-arm64, cmake 4.3.4, Apple clang 21, Homebrew sdl2-compat 2.32.x, Node 22.22.3):** every C and TS file of this plan was built and run in a scratch copy against the real LVGL 9.6.0 tarball (the pinned SHA-256), Unity 2.7.0, SDL2 and the art and fonts generated from OpenMausBot `6dd4403d` — all 22 Node tests, `ui.lv_compat`, `ui.art`, `ui.copy`, `ui.maus_engine`, `ui.screens` (including A8 eyes drawing `0xFFFF`), `sim.display`, `sim.sdl`, `sim.audio_sdl`, `sim.audio_sdl.no_device`, and the fixture snapshots (byte-identical across regenerations); every UI and sim source also compiled as gnu23 with `-Wall -Wextra -Wpedantic -Werror`; window mode opened, rendered and shut down twice in one process on SDL's dummy drivers. The CI YAML parsed with Ruby's YAML loader. The `lv_font_conv` command, both extra glyphs, the TTF hash and the firmware signatures were checked with the tools themselves.

**Re-verified after the two reviews (2026-10-05, same machine), with this revision's code:**
- In the scratch prototype: `ui.screens` 20/20 (and `test_level_and_clock_changes_do_not_relayout` fails with `Expected 1 Was 11` when the render cache is taken out), `ui.copy` 9/9, all `sim.*` tests including `sim.audio_sdl.open_fails`; the mic test passed 10 runs in a row after the frame-filling fix (before it, the silence hint failed in most runs and the new `noisy_frames` checks failed every run); the whole set again under ASan/UBSan (`GADGET_SANITIZE=ON`); every changed source as gnu23 with `-Wall -Wextra -Wpedantic -Werror`; 18 fixture goldens per board, byte-identical across two update runs and also under ASan.
- End to end, on P2a's reference build (core, CLI, script engine, task 17) with Tasks 1–8 of this revision applied as written: configure prints `-- gadget: LVGL 9.6.0 UI on (SDL window ON, snapshot update OFF)`; `nm` finds the LVGL UI in `gadget-sim`; `ctest -N -L snapshot` lists 15 tests; without goldens they fail on missing references; the `build/snap` run writes 35 PNGs per board and all 15 pass; the `build/host` run then passes and a second `build/snap` run is byte-identical; the scripted goldens show `gad_b18b86ce1389e46d`; `-L "unit|vectors|e2e"` passes 38/38 (`sim.window_smoke` included); `GADGET_BUILD_SIM=OFF`, `GADGET_WITH_LVGL=OFF` and `GADGET_WITH_SDL=OFF` configure, build and pass.
- `npm run art` with `checkBodyBounds` writes the same art (byte for byte) and stops with the "disagree" message when the Kotlin `right` bound is moved by 2 units; `npm run fonts` without a build tree downloads the TTF (pinned SHA-256) and writes byte-identical fonts.
- ESP-IDF v6.0.3's two passes over `firmware/ui/CMakeLists.txt`, simulated: the script-mode requirements pass registers `REQUIRES lvgl__lvgl;core`; the project-mode pass registers only `art/s240` or `art/s150` and defines `MAUS_ART_HAS_<PROFILE>`; an empty profile stops with the message; P2c's Step 6b command is a no-op on this file and `sed -n '1,11p'` prints the head P2c expects.

**Not verifiable without CI (linux-x64, GCC):** the `ui` and `art-drift` jobs; GCC-only warnings under `-Werror` (buffers were sized so `-Wformat-truncation` has nothing to report); whether resvg on linux-x64 reproduces the darwin-arm64 art byte for byte (if not, run `art-regen` and commit its artifact; see `firmware/ui/README.md`). Real SDL2 (Ubuntu's 2.30) was checked by review 2 from a source build: with `LV_SDL_ACCELERATED 1` every `sim.sdl` test and `sim.window_smoke` failed on the dummy video driver (no accelerated renderer); with `0` they pass. `sim.audio_sdl.open_fails` was run on sdl2-compat only; SDL2's own disk driver reads the same `SDL_DISKAUDIOFILE` / `SDL_DISKAUDIOFILEIN` variables and fails to open a file in a missing folder the same way.

**Not verifiable without hardware (checklist in `firmware/ui/README.md`, for P2c's `docs/hardware-checklist.md`):** A8 eyes drawing white on the CO5300 and ST7789 draw paths; RGB565 byte order and colour on the panels; animation smoothness at the 33 ms refresh period on the ESP32-S3; text legibility at 14–40 px on 1.54"–2" and 1.75" round screens and nothing touching the glass edge; accented glyphs, `…` and `→` on the device; the speaking mouth following real playback; the battery arc following the charger; the ESP-IDF branch of `firmware/ui/CMakeLists.txt` (P2c builds it).

## Hand-off notes

- **P2c:** link one art profile per board (`CONFIG_GADGET_ART_PROFILE` `s240` for amoled-175c and amoled-175, `s150` for lcd-154 and devkit); the ESP branch of `firmware/ui/CMakeLists.txt` then compiles only `art/<profile>/*.c` and defines `MAUS_ART_HAS_<PROFILE>`. The art-profile check runs only after Kconfig is loaded: the file already opens with your plan's early-expansion guard (04, Task 2 Step 6b), word for word, so that step finds `CMAKE_BUILD_EARLY_EXPANSION` and does nothing, and your Contract deviation 2 can be closed; keep the guard in later edits. Keep `CONFIG_LV_CONF_SKIP=y` (the default): `firmware/ui/lv_conf.h` is on the ui component's public include path (`INCLUDE_DIRS . art fonts`) and LVGL's ESP component adds `-DLV_CONF_INCLUDE_SIMPLE`, so it must never be picked up on the device. Keep these LVGL options on in `sdkconfig.defaults` (`ui_lv_requirements.h` fails the build otherwise): `CONFIG_LV_COLOR_FORMAT_RGB565`, `CONFIG_LV_OS_NONE`, `CONFIG_LV_DRAW_SW_SUPPORT_RGB565A8`, `CONFIG_LV_DRAW_SW_SUPPORT_A8`, `CONFIG_LV_DRAW_SW_COMPLEX`, `CONFIG_LV_USE_ANIMIMG`, and the default label, image, arc and bar widgets; and keep LVGL's image caches off (`CONFIG_LV_CACHE_DEF_SIZE=0` and `CONFIG_LV_IMAGE_HEADER_CACHE_DEF_CNT=0`, the Kconfig defaults): the Image screen reuses two `lv_image_dsc_t` slots, and both caches are keyed by that address. The UI needs about 3 KiB of stack in the task that calls `ui_render` (two `ui_copy_t` of about 1.1 KiB at most). The render cache in `ui.c` keeps two `ui_model_t` copies as statics (`sizeof(ui_model_t)` is 14488 bytes on the desktop, so about 29 KB); to put them in PSRAM, compile `gadget_ui` with `UI_MODEL_COPY_ATTR` defined to your PSRAM BSS attribute (for example `EXT_RAM_BSS_ATTR` with `CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY=y`, plus whatever header defines it). Link `firmware/ui/README.md`'s hardware checks from `docs/hardware-checklist.md`.
- **P2a:** (1) P2b's scripted snapshots start from a state folder whose `storage.json` holds only `dev_key` (the RFC key), so core must keep using a stored key rather than generating one. (2) Contract §2.5's "the simulator always reports `GADGET_WIFI_CONNECTED`" means no script reaches Setup "need Wi-Fi"; P2b draws it as a fixture. (3) Core closes the connection on an `error` frame, so P2b's scripts send no `net_close` after one. (4) Core publishes a challenge's `host_name` only while that connection is open (`ch_name_live` is cleared in `on_closed`), and the stored name is written only on `ready`; so on a never-paired gadget, Setup after `bad_code` or `device_limit` has no host name to show, although spec §5.5 says Setup shows it "once a `challenge` has arrived". P2b draws whatever the model holds; whether core should keep the last challenge's name in the model until the next challenge is P2a's call (open question). (5) If `firmware/CMakeLists.txt` ships a `FATAL_ERROR` guard for `GADGET_WITH_LVGL`, P2b's Task 3 Step 5 removes it (Contract deviations 4); better not to ship it.
- **P2d:** `THIRD_PARTY.md` needs rows for `lvgl` (MIT; bundled lodepng, Zlib), SDL2 (Zlib), Montserrat (OFL-1.1, via LVGL), `@resvg/resvg-js` (MPL-2.0, build time only), `lv_font_conv` (MIT, build time only) and `pngjs` (MIT, build time only); P2d's licensing test reads `FetchContent_Declare(lvgl` from `deps.cmake` and the three `tools/art` devDependencies. `tools/art/source/README.md` carries the trademark sentence. `AGENTS.md` can quote the window-mode command of Task 6 Step 8 and the snapshot-update command of contract §1.5.
- **Whoever publishes:** if `art-drift` fails on GitHub only by least-significant-bit differences, run `art-regen` and commit its `firmware/ui/art` and `firmware/tests/snapshots` folders unchanged (contract §1.6). Before the SDK is published, Omkar confirms that the 25-expression geometry is project-owned and records it in `tools/art/source/README.md` (spec §11; not a build blocker).

## Contract deviations

1. **Licence header of the generated font files.** Contract §2.1 asks for `SPDX-License-Identifier: Apache-2.0` on every source file. `firmware/ui/fonts/font_latin1_*.c` are bitmaps derived from Montserrat, which is OFL-1.1, so `fonts.ts` writes `/* SPDX-License-Identifier: OFL-1.1 */` on those six files only (`ui_fonts.h` keeps Apache-2.0). Proposed change: contract §2.1 adds "except generated font files, which carry `OFL-1.1`". If the review keeps Apache-2.0, change the first header line in `tools/art/fonts.ts` and rerun `npm run fonts`; nothing else depends on it.
2. **Turn screens are fixtures by choice (closed by contract D27).** When this plan was written, `heard`, `reply`, `speak.*` and `done` needed the gadget's turn id, which contract §2.12 pins to `hal_crypto_random`, so no `--host script` run could reach Thinking, Speaking or Reply. Contract D27 (P2a deviation 7) has since made `net_text` replace every `${turn}` with the `turn` of the last `voice.begin` or `say` the gadget sent (`firmware/ports/sim/sim_script.c`), so a script can now reach these screens end to end. P2b keeps their fixture snapshots (Task 7) by choice: they pin each speaking level and the second page without timing a stream. The comments in `test_ui_snapshots.c` and `snap_screens.txt` say so.
3. **Clarification, no pinned shape changed: window-mode input follows the board's `input_mask`.** Contract §2.16 says the SDL filter posts Space as TALK, Esc as CANCEL and the mouse as touch; contract §2.5 says the board's `input_mask` says which input sources exist. P2b posts each only where the board has that source: Esc posts nothing on amoled-175 (no CANCEL), and the mouse posts nothing on lcd-154 and devkit (no TOUCH), so the window behaves like the device. `test_escape_is_cancel_only_on_boards_with_cancel`, `test_mouse_does_nothing_on_button_boards` and Task 6 Step 8 check it. Proposed change: §2.16 adds "for the input sources in the board's `input_mask`". If the review prefers posting unconditionally, drop the two `inputs &` conditions in `filter()` and those two tests.
4. **P2a's `firmware/CMakeLists.txt` beyond §5.1's insertion points.** If P2a ships a placeholder `if(GADGET_WITH_LVGL)` / `message(FATAL_ERROR "GADGET_WITH_LVGL needs firmware/ui, which plan P2b adds. ...")` / `endif()` before `include(cmake/deps.cmake)`, every LVGL configure in this plan stops ("Configuring incomplete, errors occurred!", reproduced in review). Task 3 Step 5 deletes exactly that guard with one `perl` line (a no-op when it is absent) before adding `add_subdirectory(ui)`. Proposed change: contract §5.1 lists "remove the `GADGET_WITH_LVGL` placeholder guard" among P2b's edits to that file, or P2a agrees not to ship the guard.
5. **The post toast, found in review of Tasks 5–8 (commit `736042e` and its follow-up `fix(P2b): address review of tasks 5, 6, 7, 8`).** Task 5 drew the toast 40 % translucent at the bottom of every screen. Two things were wrong with that. On the smaller boards the caption and the Maus showed through it. On the round touch boards it sat over the ask's option buttons, and core still hit-tests their rects on `TOUCH_UP` (`display_ask_input()` → `ui_hit_test()`), so a tap meant to dismiss the toast answered the option under it. What changed:
   - `736042e`: the toast is opaque (the dim colour mixed 40 % onto black, `LV_OPA_COVER`), and on the Maus screens the caption steps aside (hidden) while a toast covers part of it. `ui_pager.h` gains `int32_t ui_pager_bottom(const ui_pager_t *)` (the lowest y the caption shows) for that check. Two new tests cover it: `test_toast_covers_what_is_under_it` and `test_toast_never_cuts_text` (Idle and Offline).
   - Follow-up: `ui_metrics_t` gains `gadget_rect_t toast_top`. On round boards it is `ui_rect_in_circle(w, 46, 140, 10)`, otherwise `{8, 8, w - 16, 60}`, the same size as `toast`. The toast sits there on Ask, Card, Update and Listening, and at the bottom on the other screens. On Ask, Card and Update the title, body and update line start under it (`text_top()` in `ui_screens.c`). The reviewer's alternative was to hide each label the toast cuts, as the caption is hidden; starting under it keeps the question being asked readable above Allow/Deny, and every board still has room for the title and some body. On Listening the toast covers the Maus, not the countdown digit. With the toast gone the text returns to the top of the safe area. `test_toast_never_cuts_text` now also covers Listening (countdown 3), Card (long body), Ask (2 and 3 options) and Update. A new test, `test_toast_never_hides_ask_options`, checks every board with 2 and 4 options: no drawn option, and on touch boards no hit rect, meets the toast.
   - Also in the follow-up, two layout fixes. (a) Setup and Offline pages turned on absolute time (`now_ms / UI_PAGE_ROTATE_MS`), so a new message could open on a middle page. On lcd-154 the scripted goldens `setup_bad_code`, `setup_device_limit` and `offline_unreachable` (and `setup_device_limit` on amoled-175c) opened mid-message. `ui_state_t` gains `caption_t0` and `caption_key`, and pages now count from the moment the screen or its caption changed (`ui_pager_show_rotating(p, age_ms)`). `test_setup` and `test_offline` check that a new message opens on its first line. (b) On devkit the listening ring reached x = −1. The landscape Maus now sits at `max(16, ring_d / 2 − mw / 2 + 2)`, 19 px for s150, and `test_listening` checks that the ring stays on the screen. The devkit goldens, the three lcd-154 goldens and amoled-175c's `setup_device_limit` were regenerated and looked at; two regenerations were byte-identical.
   - Counts: `ui.screens` had 20 tests as written, 22 after `736042e` and 23 after the follow-up. No pinned item changes: toast placement is P2b-local layout. Contract §2.7 says only "A post toast overlays any screen", and it still does.
6. **UBSan aborts in UI and simulator code, found in Task 10 (commit `fix(P2b): UBSan findings in UI and simulator code abort their test`).** Task 3 gave `gadget_ui` `-fsanitize=address,undefined` `PUBLIC`ly but not P2a's `-fno-sanitize-recover=undefined` (`firmware/cmake/warnings.cmake`). `gadget_ui`, `gadget_sim_lvgl`, `test_ui_*`, `test_sim_*` and `test_ui_snapshots` do not call `gadget_warnings`, so under `GADGET_SANITIZE=ON` they used UBSan's recoverable handlers (`nm -u` on `libgadget_ui.a` listed only non-`_abort` `__ubsan_handle_*`): a finding printed `runtime error` and its test still passed. A probe with signed overflow, built with `test_ui_copy`'s flags, exited 0 before the fix and aborted (134) after it; both libraries now reference only `_abort` handlers. The two code blocks of Task 3 Step 5 and Task 5 show the new lines. The `build/asan` run (`-R '^(ui|sim)\.'`, 34 tests, and every label, 56 tests) passes with the fix, and `Testing/Temporary/LastTest.log` has no `runtime error`, `AddressSanitizer` or `LeakSanitizer` line, so nothing was hiding. No pinned item changes.

Additions (allowed by contract §0 item 3, read by no other plan): CTest names `ui.<area>`, `sim.<area>`, `sim.audio_sdl.no_device`, `sim.audio_sdl.open_fails`, `sim.window_smoke`, `ui.snap.fixtures.<board>`, `ui.snap.<board>.update` and the `.fresh` fixtures (`fresh_dir.cmake` with its optional `DEV_KEY`); `ui_lv_requirements.h` also checks LVGL defaults the UI needs (`LV_DRAW_SW_COMPLEX`, label, image, arc, bar) and that both image caches are off; `UI_MODEL_COPY_ATTR` (render-cache placement hook for P2c); `firmware/ui/README.md`; the P2b-local `TALK`/`CANCEL` hints under the ask buttons on button boards; the host-name line on Setup and Offline (spec §5.5; it shows `host_name` itself and adds no English, so contract §2.15's copy table is unchanged).

## Self-review

- **Spec coverage:** every row of the coverage table maps to a task; the only spec items in §5.5/§5.7 left to other plans are named under "Owned elsewhere".
- **Placeholder scan:** every code step carries the complete file or the exact block to append; no "TBD", no "similar to Task N"; generated files (art, fonts, PNGs) come from commands with stated expected output.
- **Type consistency:** `maus_art_t`/`maus_state_def_t`/`maus_layer_t` come verbatim from the contract header; `ui_copy_t` (with `host`), `maus_frame_t`, `maus_engine_t`, `ui_pager_t` (with `sub`), `ui_metrics_t`, `ui_state_t` (with `applies`) are defined once and used with the same names in later tasks; every `ui_pager_set`/`show_caption` call passes the `sub` argument; CMake targets `gadget_ui`, `gadget_sim_lvgl`, `lvgl::lvgl`, `SDL2::SDL2`, `unity::framework`, `gadget_core`, `gadget-sim` match contract §2.18.
- **Review Focus:** each of the six lines has its tests in the owning task (Tasks 4, 5 and 6), named in the list, and each new test was run; the render-cache and mic-frame tests were also seen failing against the code they guard.
- **Counts:** `ui.screens` 23 (20 as first written; Contract deviations 5), `ui.copy` 9, `sim.sdl` 5, `sim.audio_sdl` 5; 18 `fx_*.png` + 16 scripted + 1 update = 35 goldens per board; 15 tests labelled `snapshot`; Node tests 22.
