# Firmware Core and Headless Simulator (P2a) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build the gadget's portable C11 core (protocol codec, PSA crypto, session and pairing, talking, speech playback, asks/cards/images/posts, actions, console, OTA and probation) behind the HAL contract, plus a headless desktop simulator with real backends (WebSocket over wslay, storage file, WAV audio, OTA slot files with restart, mDNS on macOS) and the tests that prove it: CTest unit tests against a fake HAL, the protocol vectors in C, scripted simulator runs, and simulator ↔ fake-host end-to-end scenarios. The UI is a stub, so nothing here needs LVGL.

**Architecture:** `firmware/core` is one static library (an ESP-IDF component on the device) that only sees `gadget_hal.h`; the port calls `core_tick(now)` every 10 ms and delivers every HAL event on that thread, and core publishes a `ui_model_t` that the UI only reads. Inside core, `session.c` owns the connection, and the other modules (`interaction.c`, `audio.c`, `display.c`, `actions.c`, `ota.c`, `console_cmd.c`) hook in through `core.c` and talk through the private `core_internal.h`; `screens.c` picks the screen from flags each module sets. `firmware/ports/sim` implements every HAL group for the desktop and runs headless on a virtual clock driven by a script; `ui_stub.c` stands in for plan P2b's LVGL UI.

**Tech Stack:** C11, CMake ≥ 3.24 (local 4.3.4, Unix Makefiles), Apple clang 21 on macOS and GCC on ubuntu-24.04 (CI), cJSON 1.7.19, mbedTLS 3.6.7 (PSA API; CI leg 4.2.0), Unity 2.7.0, wslay 1.1.1, Apple `dns_sd`, Node ≥ 22.18 for the end-to-end runner, and P1's fake host (`tools/fake-host`, `ws` 8.22.0).

**Spec:** `/Users/omkar/Desktop/openmaus/openmausbot-gadget-sdk/docs/specs/2026-10-04-openmausbot-gadget-design.md` (v1.1), with the binding interface contract `/Users/omkar/Desktop/openmaus/openmausbot-gadget-sdk/docs/plans/00-interfaces.md` (names, types, paths, file formats and ownership win over everything else). Spec sections in scope: §5.1 `firmware/core`, §5.2 HAL, §5.4 interaction, §5.6 console, §5.7 simulator (headless mode and HAL backends), the core side of §4 and §4.8, and the firmware rows of §10.

**How this plan was verified.** Every code block below comes from a scratch repository where these tasks were executed one at a time on this Mac, with one commit per task. Each task's state was then rebuilt from scratch in its own worktree and passed its tests with zero compiler warnings. The finished branch also passed:

- the mbedTLS 4.2.0 leg;
- an AddressSanitizer + UndefinedBehaviorSanitizer build of every test, the simulator and the end-to-end runs;
- a `-std=gnu23 -Wall -Wextra -Wpedantic -Werror` compile of every core source (ESP-IDF 6's default C standard), once against mbedTLS 3.6.7's headers and once against 4.2.0's TF-PSA-Crypto headers (ESP-IDF 6.0.3 bundles mbedTLS 4.1.1).

The code was first written against stand-ins for P1's `protocol/vectors` and `tools/fake-host`, built to contract §1.7, §4.4 and §4.7. P1 has since landed on `main`, through `80dff83 fix(P1): address final review`. This revision was replayed onto a clone of that tree with `patch -p1 -F0` (no fuzz): every task's `firmware/` tree matched its verified commit, P1's `package.json` and `ci.yml` took their diffs cleanly, and the finished branch passed 26 of 26 tests on mbedTLS 3.6.7, on 4.2.0 and under ASan + UBSan, with the C vectors test reading P1's real vectors and the six end-to-end scenarios driving P1's real fake host. The fail-first step of every task whose tests changed in this revision (Tasks 7–9, 11–14 and 15a) was rerun and fails as written. A seventh scenario, `e2e.bargein_after_done` (Task 16), was added after that replay. It runs MausBot's usual order, `done` before the speech, through P1's real `--done-before-speech`, where the other six all run the fake host's default order (the speech, then `done`). It was run with `run.ts` against the replayed simulator builds (mbedTLS 3.6.7, 4.2.0, and ASan + UBSan) and P1's fake host, and it passes on all three; 7 of 7 scenarios pass on the 3.6.7 build. It fails as written in three cases: on a simulator before Task 16's client, on a simulator mutated to send `stop` on barge-in after `done`, and against the fake host's default order. The full 27-test CTest run with it registered is Task 18's check. The host-name fix in Tasks 8 and 10 (the model keeps the last challenge's `host_name` after its connection closes) was then replayed onto that replay's commits, and every later diff of `session.c` and `core_internal.h` was regenerated from the result. Its new and changed assertions fail on the previous code (`Expected 'Mac' Was ''`, `Expected 'Omkar's Mac' Was ''` and `Expected '' Was 'Mac'`), Task 10's fail-first step still fails as written, and the branch without the seventh scenario passed 26 of 26 tests on mbedTLS 3.6.7, on 4.2.0 and under ASan + UBSan. `gadget_event_send` (Contract deviations, item 9) was then added the same way: its header declaration, `actions.c` code and two tests were committed onto that replay's Task 13 and Tasks 14–17 were replayed on top without conflicts. Task 13's fail-first step fails as written (the link misses `gadget_action_register` and `gadget_event_send`), `test_actions` passes 9 of 9, each new test fails when its check is removed from `actions.c`, the branch without the seventh scenario again passed 26 of 26 tests on mbedTLS 3.6.7, on 4.2.0 and under ASan + UBSan, and the gnu23 check passed against both header sets. The `device_limit` status fix in Tasks 8 and 10 came next. Through the retry window, `pair` stays `error` with `error` `device_limit` (contract §2.11, status rule 2). It was checked by replaying this whole plan onto `80dff83` with `patch -p1 -F0`:
- Tasks 8 and 10 pass as written.
- The finished branch passed 27 of 27 tests on mbedTLS 3.6.7, the seven end-to-end scenarios included.
- `test_session` and `test_console` pass under ASan + UBSan, and `session.c` passes the gnu23 check.
- With the old `session_pair_state()`, the extended test fails in Task 8 (`Expected 4 Was 2`, so `connecting` instead of `error`). Its `status` checks fail too, printing `"pair":"connecting"` with no `error`.
- Task 10's fail-first step fails as written.

What could not be verified here is listed in Task 18.

## Global Constraints

- **Repository and branch:** `/Users/omkar/Desktop/openmaus/openmausbot-gadget-sdk`, branch `p2a-core` created from `p1-protocol`, or from `main`, where P1 actually landed (contract §1.3). One worktree per concurrent session (Task 1, Step 1). Never commit to `main`, push, open a PR or create a release; publishing belongs to Omkar.
- **The app repo is off limits.** Never touch `/Users/omkar/Desktop/openmaus/OpenGrokBot`. P2a needs nothing from it.
- **Original-work rule (spec §11):** write everything yourself from the spec, the contract and vendor documentation (Espressif, mbedTLS, wslay, Apple `dns_sd`, RFC 6455). Never open, copy or cite another gadget SDK or voice-assistant firmware.
- **C standard:** C11, `CMAKE_C_EXTENSIONS OFF`; `-Wall -Wextra -Wpedantic -Werror` on every target we own. Every core source must also compile as `gnu23` (ESP-IDF 6's default).
- **Core includes:** core sources include only `gadget_*.h`, `cJSON.h` (always spelled `"cJSON.h"`), `psa/crypto.h` and libc (spec §5.1). `firmware/ports/sim` compiles with `_POSIX_C_SOURCE=200809L`, plus `_DARWIN_C_SOURCE` on macOS.
- **Crypto is PSA only (spec §5.2, contract §2.5):** `psa_generate_key`, `psa_import_key`, `psa_export_key`, `psa_export_public_key`, `psa_destroy_key`, `psa_sign_hash` with `PSA_ALG_DETERMINISTIC_ECDSA(PSA_ALG_SHA_256)`, `psa_verify_hash` with `PSA_ALG_ECDSA(PSA_ALG_SHA_256)`, `psa_hash_compute`, `psa_hash_setup/update/finish/abort`, `psa_generate_random`. Volatile keys only. S is never normalized. Never `mbedtls_ecdsa_*`, `mbedtls_ecp_*` or `mbedtls_sha256_*`. DER ↔ raw conversion is our own code.
- **Desktop pins (contract §1.4):**
  - cJSON 1.7.19, SHA256 `7fa616e3046edfa7a28a32d5f9eacfd23f92900fe1f8ccd988c1662f30454562`, populate only;
  - Unity 2.7.0, SHA256 `e84eb301ca7967831e68b1728f911e87fa2d345d8ddb64f897bc2f2ee24a321c`;
  - mbedTLS 3.6.7, SHA256 `a7e8bcbec0e6f761b4af24f25677626b35f762f68eef79c08677a363212d11f6`, the default; 4.2.0, SHA256 `2bed9d713b4668f76553b097e72b8aa30bc8f112a940d7ae228d524bbde6ffea`, through `-DGADGET_MBEDTLS_VERSION=4.2.0`;
  - wslay 1.1.1, SHA256 `7b9f4b9df09adaa6e07ec309b68ab376c0db2cfd916613023b52a47adfda224a`, compiled from its five `.c` files.
  - LVGL and SDL belong to plan P2b and are not used here.
- **Build:** `cmake_minimum_required(VERSION 3.24)`. The build directory is `build/host`, and the simulator is `build/host/ports/sim/gadget-sim`. CTest labels are `unit`, `vectors` and `e2e`. `-DGADGET_WITH_LVGL=OFF` is the P2a default.
- **License header:** every source file starts with its path comment and `/* SPDX-License-Identifier: Apache-2.0 */` (`#` comments in CMake and `//` in TypeScript).
- **Contract names are binding:** nothing in contract §2 is renamed, retyped or moved. This plan adds only private files (`core_internal.h`, `console_cmd.c`, `display.c`, `sim_internal.h`, `run_sim_script.cmake`, test scripts) and the additions listed under "Contract deviations" at the end (the contract adopted items 1–4 and 6–8 there as D22–D28).
- **Protocol limits (spec §4.1):**
  - text frames ≤ 16 KiB, binary frames ≤ 8 KiB;
  - mic frames are 20 ms (320 samples at 16 kHz);
  - an utterance is ≤ 60 s, with one turn in flight;
  - 45 s with no inbound frame means a dead connection;
  - reconnect backoff is 2, 4, 8 … s, capped at 60 s.
- **Threading:** core is single-threaded. The port calls `core_tick(now)`, `ui_render(core_ui_model())` and `ui_tick(now)` every 10 ms, and calls `core_event()` only on that thread.
- **Node:** `firmware/tests/e2e/run.ts` runs directly on Node ≥ 22.18 (type stripping): erasable syntax only, so no `enum`, no parameter properties, and relative imports end in `.ts`. CI uses Node 24.
- **No global installs.** cmake 4.3.4, clang and node are already on this Mac. ninja is not needed.

## Review Focus

These five inputs are implied by the spec but no requirement spells them out. A test for each is added to the task that owns the code.

1. **An oversized frame from a misbehaving host** (binary > 8 KiB, text > 16 KiB): it is dropped before decoding, nothing of it reaches the speaker or the screen, and nothing crashes; a frame of exactly 8 KiB still plays. ASan found a stack overflow here while this plan was being verified. Tests: Task 8, `test_frames_over_the_size_limits_are_dropped` (its speaker check bites from Task 11 on, once speech plays), and Task 11, `test_a_frame_of_exactly_8_kib_plays`.
2. **An update offered while the person is talking or waiting for a reply:** the gadget answers `fw.fail busy` and cuts nobody off (contract D22). Test: Task 14, `test_busy_while_the_person_is_talking`.
3. **A reboot, crash or power cut of a new image before its first `ready`:** the gadget comes back on the previous image. Tests: Task 14, `test_probation_rolls_back_without_a_ready`, and Task 15b, `sim.reboot_rollback`.
4. **A tap while a post's chime is playing:** the tap hides the toast. It is not treated as "stop the speech". Test: Task 12, `test_posts_toast_and_chime`.
5. **Host text the fonts cannot draw, and replies longer than the screen buffer:** the text is folded to `?`, and a long reply keeps its tail after a leading `…`. Tests: Task 9, `test_text_outside_the_charset_is_folded` and `test_a_long_reply_keeps_its_tail`.

---

## File Structure

All paths are relative to the SDK repository. One responsibility per file.

| Path | Responsibility |
|---|---|
| `firmware/CMakeLists.txt` | Desktop build: options, deps, `core`, `ports/sim`, `tests` |
| `firmware/cmake/deps.cmake` | Pinned FetchContent sources: cJSON, Unity, mbedTLS (`gadget_mbedcrypto`), wslay |
| `firmware/cmake/warnings.cmake` | `gadget_warnings(target)`: warnings as errors plus optional sanitizers |
| `firmware/core/CMakeLists.txt` | Dual-use: ESP-IDF component or the `gadget_core` static library |
| `firmware/core/include/*.h` | The contract headers, verbatim from contract §2.2–§2.13 |
| `firmware/core/src/util.c` | Base64, hex, DER ↔ raw, id from pubkey, validators, UTF-8 cut, xorshift32 |
| `firmware/core/src/proto.c` | `gp_*` codec: op names, decode, encoders, binary frames, signed texts |
| `firmware/core/src/crypto_psa.c` | The HAL crypto group on PSA, for both ports |
| `firmware/core/src/boards.c` | The four board descriptors |
| `firmware/core/src/ui_layout.c` | `ui_safe_area`, `ui_layout_ask`, `ui_hit_test` (shared with the LVGL UI) |
| `firmware/core/src/console.c` | Line assembly, argument splitting, command grammar (pure) |
| `firmware/core/src/core_internal.h` | Private: `core_t` state and the calls between core modules |
| `firmware/core/src/core.c` | `core_init/event/tick/deinit`, identity, storage load, model revisions, hooks |
| `firmware/core/src/session.c` | Host lookup, handshake, error reactions, backoff, liveness, settings |
| `firmware/core/src/screens.c` | Screen and Maus-state selection |
| `firmware/core/src/interaction.c` | Presses, recording, mic frames, turns, cancel, 60 s limit, `say` |
| `firmware/core/src/console_cmd.c` | Running console commands, `@omb` lines |
| `firmware/core/src/audio.c` | Speech playback, jitter buffer, mouth level, chime |
| `firmware/core/src/display.c` | Asks and answers, cards, images, post toasts, battery `sense` |
| `firmware/core/src/actions.c` | `gadget_action_register`, hello declarations, `act` → `act.result`, built-in chime, `gadget_event_send` |
| `firmware/core/src/ota.c` | Offer checks, chunk flow, commit, probation, `gadget_key_find` |
| `firmware/core/src/keys_release.c`, `keys_test.c` | Release key table (empty until P2d) and the t1 test key table |
| `firmware/ports/sim/sim_hal.h`, `sim_display.h` | Contract seams (§2.16), verbatim |
| `firmware/ports/sim/sim_internal.h` | Private: options and calls between simulator files |
| `firmware/ports/sim/main.c` | The loop: virtual or real clock, events, ticks, script steps |
| `firmware/ports/sim/sim_args.c` | Command line |
| `firmware/ports/sim/sim_events.c` | Thread-safe event queue with deep copies |
| `firmware/ports/sim/sim_storage.c` | `storage.json` storage HAL, atomic writes |
| `firmware/ports/sim/sim_system.c` | Clock, log, console out, restart by `execv` |
| `firmware/ports/sim/sim_wifi.c`, `sim_battery.c`, `sim_console.c` | Always-on Wi-Fi with a fixed scan list; simulated battery; stdin console |
| `firmware/ports/sim/sim_audio.c`, `sim_audio_file.c` | `hal_mic_*`/`hal_spk_*` forwarding; WAV in/out and null audio |
| `firmware/ports/sim/sim_mdns.c` | `host auto` through `dns_sd` (macOS); unsupported on Linux |
| `firmware/ports/sim/sim_ota.c` | Slot files, `otadata.json`, rollback on start |
| `firmware/ports/sim/sim_ws.c`, `sim_net_script.c` | Real WebSocket client (wslay); the scripted network for `--host script` |
| `firmware/ports/sim/sim_script.c` | Headless script engine |
| `firmware/ports/sim/sim_display_null.c`, `ui_stub.c` | No-LVGL display and UI (P2b replaces them) |
| `firmware/tests/fake_hal.c`, `fake_hal.h` | In-memory HAL plus helpers for core tests |
| `firmware/tests/test_*.c` | Unity tests, one area per file |
| `firmware/tests/run_sim_script.cmake`, `scripts/sim_*.txt` | Scripted simulator runs (label `unit`) |
| `firmware/tests/e2e/run.ts`, `e2e/*.txt` | Simulator ↔ fake-host scenarios (label `e2e`) |
| `package.json` (P1's) | Gains the `test:e2e` script |
| `.github/workflows/ci.yml` (P1's) | Gains the `host-c` job |

Commands used throughout (run from the repository root):

```bash
# configure once (Task 1 shows the first run)
cmake -S firmware -B build/host -DCMAKE_BUILD_TYPE=Debug -DGADGET_WITH_LVGL=OFF
# build and run the C tests
cmake --build build/host -j10 && ctest --test-dir build/host --output-on-failure -L "unit|vectors"
```

A Unity test binary prints one line per test and ends with `<n> Tests 0 Failures 0 Ignored` and `OK`. CTest prints `100% tests passed, 0 tests failed out of <n>`.

**How code steps are written:**

- **`Create`** gives the whole file.
- **`Change … exactly as this diff shows`** gives a unified diff against the previous task's state of that file. Apply it by hand, or save the block (without the fence lines) to `/tmp/step.diff` and run `patch -p1 < /tmp/step.diff` from the repository root.
- Every diff was checked to apply cleanly in order: replaying this plan's steps from P1's tree reproduced each verified task state exactly.

---
### Task 1: Desktop build, contract headers and pure helpers

**Files:**
- Create: `firmware/CMakeLists.txt`, `firmware/cmake/warnings.cmake`, `firmware/cmake/deps.cmake`, `firmware/core/CMakeLists.txt`
- Create: the twelve contract headers in `firmware/core/include/` (contract §2.2–§2.13, verbatim)
- Create: `firmware/core/src/util.c`
- Test: `firmware/tests/CMakeLists.txt`, `firmware/tests/test_util.c`

**Interfaces:**
- Consumes: P1's tree, on `p1-protocol` or on `main` where it landed (`protocol/vectors/`, `keys/test-t1.*`, root `package.json`, `.github/workflows/ci.yml`, `tools/fake-host/`).
- Produces: every type and prototype of contract §2.2–§2.13, so later tasks only add `.c` files; the `gadget_core` target; the options of contract §2.18 that P2a owns. `GADGET_WITH_LVGL` is declared before `include(cmake/deps.cmake)`, because P2b relies on that order; `gadget_warnings(target)`; the test helper `gadget_pure_test(area)` (`test_<area>.c` → CTest `core.<area>`, label `unit`, argv[1] = `protocol/vectors`); and from `util.c`: `gadget_b64_encode/decode`, `gadget_hex_encode/decode` (lowercase only), `gadget_der_from_raw/to_raw`, `gadget_host_id_valid`, `gadget_pair_code_valid`, `gadget_utf8_copy/_tail/_len`, `gadget_prng_seed/next/range`. `gadget_id_from_pubkey` comes in Task 3, because it needs SHA-256.

- [ ] **Step 1: Check that P1 is in place, then create the branch**

First make sure no other session is working in this checkout: P1's last task has finished, and `git status --short` shows only `?? docs/plans/*` lines. `git switch` moves the whole working tree, so a session still working here would see its files change and would commit onto `p2a-core`.

```bash
cd /Users/omkar/Desktop/openmaus/openmausbot-gadget-sdk
test -f protocol/vectors/prove.json && test -f protocol/vectors/SHA256SUMS && test -f tools/fake-host/src/main.ts \
  && test -f keys/test-t1.pub.b64 && grep -q '"ws"' package.json && echo "P1 present"
BASE=$(git rev-parse --verify -q p1-protocol >/dev/null && echo p1-protocol || echo main)
test -z "$(git status --porcelain -- . ':!docs/plans')" && git switch -c p2a-core "$BASE" && git log --oneline -1
```

Expected: `P1 present`, then the new branch's first line of `git log`. P1 landed straight on `main` (its feature commits end at `3cd0254 feat(fake-host): JSON-lines CLI, README and CI job`, and its review fixes at `80dff83 fix(P1): address final review`), so today `BASE` is `main`, and the branch starts at the tip of `main`, which may also carry later `docs(plans)` commits. If `P1 present` does not print, stop: P1 must land first (contract §1.3). If the branch is not created, something other than plan files is uncommitted here: stop and ask Omkar.

If another session might still be active in this checkout, use a worktree instead (one worktree per concurrent session). Run the block above without its last line, then this, and run every later command of this plan from the worktree:

```bash
git worktree add -b p2a-core /Users/omkar/Desktop/openmaus/openmausbot-gadget-sdk-p2a "$BASE"
cd /Users/omkar/Desktop/openmaus/openmausbot-gadget-sdk-p2a && npm ci
```

Remove the worktree after the hand-off in Task 18 (`git -C /Users/omkar/Desktop/openmaus/openmausbot-gadget-sdk worktree remove /Users/omkar/Desktop/openmaus/openmausbot-gadget-sdk-p2a`); the branch stays.

- [ ] **Step 2: Write the build files and the contract headers**

The headers are contract §2.2–§2.13, copied byte for byte. Contract §2.10's `gadget_actions.h` includes `gadget_event_send` (spec §4.7; Contract deviations, item 9), which Task 13 implements. Only `gadget_core.h` and that declaration name functions that later tasks implement; that is fine, since a declaration without a caller links.

`firmware/CMakeLists.txt` stops at configure time when the C tests are on but the t1 test key is off (`-DGADGET_TEST_KEYS=OFF`): Tasks 14 and 15b sign OTA images with that key, so such a build would only produce red tests that look like regressions.

Create `firmware/CMakeLists.txt`:

```cmake
# firmware/CMakeLists.txt
# SPDX-License-Identifier: Apache-2.0
# Desktop build only: core, the simulator and the C tests. ESP-IDF builds
# boards from firmware/ports/esp32 with its own project file (plan P2c).
cmake_minimum_required(VERSION 3.24)
project(openmausbot_gadget_host C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# P2b adds firmware/ui and the LVGL/SDL simulator display behind this option
# (and flips its default to ON); until then it changes nothing.
option(GADGET_WITH_LVGL "Build the LVGL UI and the LVGL/SDL simulator display (plan P2b)" OFF)
option(GADGET_BUILD_SIM "Build the gadget-sim simulator" ON)
option(GADGET_BUILD_TESTS "Build the C tests" ON)
option(GADGET_TEST_KEYS "Compile the t1 test signing key into gadget_core" ON)
option(GADGET_SANITIZE "Build our targets with -fsanitize=address,undefined" OFF)
set(GADGET_MBEDTLS_VERSION "3.6.7" CACHE STRING "mbedTLS for the desktop build: 3.6.7 or 4.2.0")
set_property(CACHE GADGET_MBEDTLS_VERSION PROPERTY STRINGS 3.6.7 4.2.0)
set(GADGET_SIM_VERSION "0.0.0-dev" CACHE STRING "The simulator's fw version before any OTA")
set(GADGET_VECTORS_DIR "${CMAKE_SOURCE_DIR}/../protocol/vectors")
# The C tests sign OTA images with the t1 test key (contract §1.7).
if(GADGET_BUILD_TESTS AND NOT GADGET_TEST_KEYS)
  message(FATAL_ERROR "The C tests sign OTA images with the t1 test key: use -DGADGET_TEST_KEYS=ON or -DGADGET_BUILD_TESTS=OFF")
endif()

include(cmake/warnings.cmake)
include(cmake/deps.cmake)

add_subdirectory(core)

if(GADGET_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
```

Create `firmware/cmake/warnings.cmake`:

```cmake
# firmware/cmake/warnings.cmake
# SPDX-License-Identifier: Apache-2.0
# Warnings as errors on our own targets (never on fetched dependencies),
# plus the optional sanitizers.
function(gadget_warnings target)
  target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Werror)
  if(GADGET_SANITIZE)
    target_compile_options(${target} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(${target} PRIVATE -fsanitize=address,undefined)
  endif()
endfunction()
```

`deps.cmake` starts with cJSON and Unity; Task 3 adds mbedTLS and Task 16 adds wslay.

Create `firmware/cmake/deps.cmake`:

```cmake
# firmware/cmake/deps.cmake
# SPDX-License-Identifier: Apache-2.0
# Pinned third-party sources for the desktop build (contract §1.4).
include(FetchContent)

# cJSON 1.7.19 (MIT). Populate only: its own CMakeLists needs CMake < 3.5
# compatibility, which CMake 4 removed. Included as "cJSON.h", the same
# spelling as espressif/cjson on ESP-IDF.
FetchContent_Declare(cjson
  URL https://github.com/DaveGamble/cJSON/archive/refs/tags/v1.7.19.tar.gz
  URL_HASH SHA256=7fa616e3046edfa7a28a32d5f9eacfd23f92900fe1f8ccd988c1662f30454562
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
  SOURCE_SUBDIR _populate_only_)

# Unity 2.7.0 (MIT), the C test framework. Target unity::framework.
FetchContent_Declare(unity
  URL https://github.com/ThrowTheSwitch/Unity/archive/refs/tags/v2.7.0.tar.gz
  URL_HASH SHA256=e84eb301ca7967831e68b1728f911e87fa2d345d8ddb64f897bc2f2ee24a321c
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE)

FetchContent_MakeAvailable(cjson unity)

add_library(cjson STATIC ${cjson_SOURCE_DIR}/cJSON.c)
target_include_directories(cjson PUBLIC ${cjson_SOURCE_DIR})
set_target_properties(cjson PROPERTIES C_EXTENSIONS OFF)
if(UNIX AND NOT APPLE)
  target_link_libraries(cjson PUBLIC m)   # cJSON uses fabs/floor; macOS has libm in libSystem
endif()
```

`core/CMakeLists.txt` keeps one source list for both branches, so each later task adds one line. Plan P2c may correct the ESP-IDF branch (contract §2.17).

Create `firmware/core/CMakeLists.txt`:

```cmake
# firmware/core/CMakeLists.txt
# SPDX-License-Identifier: Apache-2.0
# Dual-use: an ESP-IDF component when ESP_PLATFORM is set (plan P2c may
# correct that branch), a static library for the desktop build otherwise.
set(GADGET_CORE_SRCS
  src/util.c
)

if(ESP_PLATFORM)
  idf_component_register(SRCS ${GADGET_CORE_SRCS}
                         INCLUDE_DIRS include
                         REQUIRES espressif__cjson mbedtls)
  return()
endif()

add_library(gadget_core STATIC ${GADGET_CORE_SRCS})
target_include_directories(gadget_core PUBLIC include)
target_link_libraries(gadget_core PUBLIC cjson)
gadget_warnings(gadget_core)
```

Create `firmware/core/include/gadget_types.h`:

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

Create `firmware/core/include/gadget_board.h`:

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

Create `firmware/core/include/gadget_events.h`:

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

Create `firmware/core/include/gadget_hal.h`:

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

Create `firmware/core/include/gadget_util.h`:

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

Create `firmware/core/include/gadget_core.h`:

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

Create `firmware/core/include/gadget_ui_model.h`:

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

Create `firmware/core/include/gadget_ui.h`:

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

Create `firmware/core/include/gadget_actions.h`:

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

Create `firmware/core/include/gadget_console.h`:

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

Create `firmware/core/include/gadget_proto.h`:

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

Create `firmware/core/include/gadget_ota.h`:

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

- [ ] **Step 3: Write the failing test**

Create `firmware/tests/CMakeLists.txt`:

```cmake
# firmware/tests/CMakeLists.txt
# SPDX-License-Identifier: Apache-2.0
# Native C tests (CTest). Labels: unit, vectors, e2e (P2b adds snapshot).

# A test of pure helpers that needs no HAL: test_<area>.c -> core.<area>.
function(gadget_pure_test area)
  add_executable(test_${area} test_${area}.c)
  target_link_libraries(test_${area} PRIVATE gadget_core unity::framework)
  gadget_warnings(test_${area})
  add_test(NAME core.${area} COMMAND test_${area} ${GADGET_VECTORS_DIR})
  set_tests_properties(core.${area} PROPERTIES LABELS unit TIMEOUT 60)
endfunction()

gadget_pure_test(util)
```

Create `firmware/tests/test_util.c`:

```c
/* firmware/tests/test_util.c */
/* SPDX-License-Identifier: Apache-2.0 */
#include <string.h>
#include "gadget_util.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

static void test_b64_encode_known_values(void) {
  char out[16];
  TEST_ASSERT_EQUAL_size_t(0, gadget_b64_encode(out, sizeof out, (const uint8_t *)"", 0));
  TEST_ASSERT_EQUAL_STRING("", out);
  TEST_ASSERT_EQUAL_size_t(4, gadget_b64_encode(out, sizeof out, (const uint8_t *)"\x00", 1));
  TEST_ASSERT_EQUAL_STRING("AA==", out);
  TEST_ASSERT_EQUAL_size_t(4, gadget_b64_encode(out, sizeof out, (const uint8_t *)"\x00\x01", 2));
  TEST_ASSERT_EQUAL_STRING("AAE=", out);
  TEST_ASSERT_EQUAL_size_t(8, gadget_b64_encode(out, sizeof out, (const uint8_t *)"foobar", 6));
  TEST_ASSERT_EQUAL_STRING("Zm9vYmFy", out);
  /* cap too small: needs 9 bytes for 8 chars + NUL */
  TEST_ASSERT_EQUAL_size_t(0, gadget_b64_encode(out, 8, (const uint8_t *)"foobar", 6));
}

static void test_b64_decode_is_strict(void) {
  uint8_t buf[8];
  size_t len = 99;
  TEST_ASSERT_EQUAL_INT(GADGET_OK, gadget_b64_decode("Zm9vYg==", buf, sizeof buf, &len));
  TEST_ASSERT_EQUAL_size_t(4, len);
  TEST_ASSERT_EQUAL_MEMORY("foob", buf, 4);
  TEST_ASSERT_EQUAL_INT(GADGET_OK, gadget_b64_decode("", buf, sizeof buf, &len));
  TEST_ASSERT_EQUAL_size_t(0, len);
  const char *bad[] = {"AA", "AA=", "AB==", "AAE", "AA==\n", " AA==", "-_8=", "AA==AA==", "A===", "=AAA", "AA=A"};
  for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
    TEST_ASSERT_EQUAL_INT_MESSAGE(GADGET_ERR_PARSE, gadget_b64_decode(bad[i], buf, sizeof buf, &len), bad[i]);
  }
  TEST_ASSERT_EQUAL_INT(GADGET_ERR_LIMIT, gadget_b64_decode("Zm9vYmFy", buf, 5, &len));
}

static void test_hex_round_trip_and_lowercase_only(void) {
  char out[9];
  const uint8_t in[4] = {0x00, 0xab, 0x7f, 0xff};
  gadget_hex_encode(out, in, 4);
  TEST_ASSERT_EQUAL_STRING("00ab7fff", out);
  uint8_t back[4];
  size_t len = 0;
  TEST_ASSERT_EQUAL_INT(GADGET_OK, gadget_hex_decode("00ab7fff", back, sizeof back, &len));
  TEST_ASSERT_EQUAL_size_t(4, len);
  TEST_ASSERT_EQUAL_MEMORY(in, back, 4);
  TEST_ASSERT_EQUAL_INT(GADGET_ERR_PARSE, gadget_hex_decode("00AB", back, sizeof back, &len));
  TEST_ASSERT_EQUAL_INT(GADGET_ERR_PARSE, gadget_hex_decode("abc", back, sizeof back, &len));
  TEST_ASSERT_EQUAL_INT(GADGET_ERR_PARSE, gadget_hex_decode("zz", back, sizeof back, &len));
  TEST_ASSERT_EQUAL_INT(GADGET_ERR_LIMIT, gadget_hex_decode("0011223344", back, sizeof back, &len));
}

/* RFC 6979 A.2.5 "sample": 72-byte DER, high S. */
static const char *SAMPLE_DER =
    "3046022100efd48b2aacb6a8fd1140dd9cd45e81d69d2c877b56aaf991c34d0ea84eaf3716"
    "022100f7cb1c942d657c41d436c7a1b6e29f65f3e900dbb9aff4064dc4ab2f843acda8";
/* Contract §1.7 short-DER case: 69 bytes, s has 31 significant bytes. */
static const char *SHORT_DER =
    "3043022052c1af44f658bb58a5b434a96d609052855835feca85b011cdfcb4159f52c37e"
    "021f2d466a13caea297fa0c97498ce12ed77ea25e9895415c4bb6ebe64b2a049cb";

static void der_round_trip(const char *der_hex, size_t expect_len) {
  uint8_t der[80], raw[64], again[80];
  size_t der_len = 0, again_len = 0;
  TEST_ASSERT_EQUAL_INT(GADGET_OK, gadget_hex_decode(der_hex, der, sizeof der, &der_len));
  TEST_ASSERT_EQUAL_size_t(expect_len, der_len);
  TEST_ASSERT_EQUAL_INT(GADGET_OK, gadget_der_to_raw(der, der_len, raw));
  TEST_ASSERT_EQUAL_INT(GADGET_OK, gadget_der_from_raw(raw, again, sizeof again, &again_len));
  TEST_ASSERT_EQUAL_size_t(der_len, again_len);
  TEST_ASSERT_EQUAL_MEMORY(der, again, der_len);
}

static void test_der_round_trips(void) {
  der_round_trip(SAMPLE_DER, 72);
  der_round_trip(SHORT_DER, 69);
  uint8_t der[80], raw[64];
  size_t len = 0;
  gadget_hex_decode(SHORT_DER, der, sizeof der, &len);
  TEST_ASSERT_EQUAL_INT(GADGET_OK, gadget_der_to_raw(der, len, raw));
  TEST_ASSERT_EQUAL_HEX8(0x00, raw[32]); /* s right-aligned into 32 bytes */
  TEST_ASSERT_EQUAL_HEX8(0x2d, raw[33]);
}

static void test_der_rejects_non_canonical(void) {
  uint8_t der[80], raw[64];
  size_t len = 0;
  gadget_hex_decode(SAMPLE_DER, der, sizeof der, &len);
  der[0] = 0x31; /* wrong tag */
  TEST_ASSERT_EQUAL_INT(GADGET_ERR_PARSE, gadget_der_to_raw(der, len, raw));
  gadget_hex_decode(SAMPLE_DER, der, sizeof der, &len);
  der[len] = 0x00; /* trailing byte */
  TEST_ASSERT_EQUAL_INT(GADGET_ERR_PARSE, gadget_der_to_raw(der, len + 1, raw));
  /* negative integer: r without its 0x00 pad */
  static const uint8_t neg[] = {0x30, 0x06, 0x02, 0x01, 0x80, 0x02, 0x01, 0x01};
  TEST_ASSERT_EQUAL_INT(GADGET_ERR_PARSE, gadget_der_to_raw(neg, sizeof neg, raw));
  /* non-minimal integer: 0x00 pad before a byte whose high bit is clear */
  static const uint8_t nonmin[] = {0x30, 0x07, 0x02, 0x02, 0x00, 0x01, 0x02, 0x01, 0x01};
  TEST_ASSERT_EQUAL_INT(GADGET_ERR_PARSE, gadget_der_to_raw(nonmin, sizeof nonmin, raw));
  /* integer longer than 32 bytes after the pad */
  uint8_t big[2 + 2 + 34 + 3] = {0x30, 2 + 34 + 3, 0x02, 34, 0x00, 0x80};
  big[2 + 2 + 34] = 0x02;
  big[2 + 2 + 34 + 1] = 0x01;
  big[2 + 2 + 34 + 2] = 0x01;
  TEST_ASSERT_EQUAL_INT(GADGET_ERR_PARSE, gadget_der_to_raw(big, sizeof big, raw));
  /* truncated */
  gadget_hex_decode(SAMPLE_DER, der, sizeof der, &len);
  TEST_ASSERT_EQUAL_INT(GADGET_ERR_PARSE, gadget_der_to_raw(der, len - 2, raw));
}

static void test_der_from_raw_minimal(void) {
  uint8_t raw[64] = {0};
  raw[31] = 0x01;      /* r = 1 */
  raw[32] = 0x80;      /* s has its high bit set: needs a 0x00 pad */
  uint8_t der[80];
  size_t len = 0;
  TEST_ASSERT_EQUAL_INT(GADGET_OK, gadget_der_from_raw(raw, der, sizeof der, &len));
  TEST_ASSERT_EQUAL_size_t(2 + 3 + 35, len);
  static const uint8_t head[] = {0x30, 38, 0x02, 0x01, 0x01, 0x02, 0x21, 0x00, 0x80};
  TEST_ASSERT_EQUAL_MEMORY(head, der, sizeof head);
  TEST_ASSERT_EQUAL_INT(GADGET_ERR_LIMIT, gadget_der_from_raw(raw, der, 10, &len));
}

static void test_validators(void) {
  TEST_ASSERT_TRUE(gadget_host_id_valid("000102030405060708090a0b0c0d0e0f"));
  TEST_ASSERT_FALSE(gadget_host_id_valid("000102030405060708090A0B0C0D0E0F"));
  TEST_ASSERT_FALSE(gadget_host_id_valid("h_0123456789abcdef"));
  TEST_ASSERT_FALSE(gadget_host_id_valid("000102030405060708090a0b0c0d0e0f0"));
  TEST_ASSERT_FALSE(gadget_host_id_valid(""));
  TEST_ASSERT_FALSE(gadget_host_id_valid(NULL));
  TEST_ASSERT_TRUE(gadget_pair_code_valid("123456"));
  TEST_ASSERT_TRUE(gadget_pair_code_valid("000000"));
  TEST_ASSERT_FALSE(gadget_pair_code_valid("12345"));
  TEST_ASSERT_FALSE(gadget_pair_code_valid("1234567"));
  TEST_ASSERT_FALSE(gadget_pair_code_valid("12a456"));
  TEST_ASSERT_FALSE(gadget_pair_code_valid(NULL));
}

static void test_utf8_copy_cuts_on_code_points(void) {
  char out[8];
  TEST_ASSERT_EQUAL_size_t(3, gadget_utf8_copy(out, sizeof out, "abc"));
  TEST_ASSERT_EQUAL_STRING("abc", out);
  /* "aé" is 3 bytes; 7 bytes fit exactly */
  TEST_ASSERT_EQUAL_size_t(7, gadget_utf8_copy(out, sizeof out, "abcdefg"));
  /* 8 bytes do not fit in cap 8: keep 4 bytes + "…" (3 bytes) */
  TEST_ASSERT_EQUAL_size_t(7, gadget_utf8_copy(out, sizeof out, "abcdefgh"));
  TEST_ASSERT_EQUAL_STRING("abcd\xe2\x80\xa6", out);
  /* never split "é" (C3 A9) */
  TEST_ASSERT_EQUAL_size_t(6, gadget_utf8_copy(out, sizeof out, "abc\xc3\xa9xyz"));
  TEST_ASSERT_EQUAL_STRING("abc\xe2\x80\xa6", out);
  char tiny[3];
  TEST_ASSERT_EQUAL_size_t(2, gadget_utf8_copy(tiny, sizeof tiny, "abcdef"));
  TEST_ASSERT_EQUAL_STRING("ab", tiny);
}

static void test_utf8_copy_tail_keeps_the_end(void) {
  char out[8];
  TEST_ASSERT_EQUAL_size_t(7, gadget_utf8_copy_tail(out, sizeof out, "abcdefghij"));
  TEST_ASSERT_EQUAL_STRING("\xe2\x80\xa6ghij", out);
  /* the cut would start inside "é" (C3 A9): skip forward to "xyz" */
  TEST_ASSERT_EQUAL_size_t(6, gadget_utf8_copy_tail(out, sizeof out, "abcde\xc3\xa9xyz"));
  TEST_ASSERT_EQUAL_STRING("\xe2\x80\xa6xyz", out);
  TEST_ASSERT_EQUAL_size_t(2, gadget_utf8_copy_tail(out, sizeof out, "hi"));
  TEST_ASSERT_EQUAL_STRING("hi", out);
}

static void test_utf8_len(void) {
  TEST_ASSERT_EQUAL_size_t(0, gadget_utf8_len(""));
  TEST_ASSERT_EQUAL_size_t(3, gadget_utf8_len("a\xc3\xa9\xe2\x80\xa6"));
  TEST_ASSERT_EQUAL_size_t(2, gadget_utf8_len("\xf0\x9f\x98\x80z"));  /* 4-byte sequence */
  TEST_ASSERT_EQUAL_size_t(3, gadget_utf8_len("\xff\xc3z"));          /* invalid: one per byte */
}

static void test_prng_is_xorshift32(void) {
  gadget_prng_t p;
  gadget_prng_seed(&p, 1);
  TEST_ASSERT_EQUAL_HEX32(270369u, gadget_prng_next(&p));
  TEST_ASSERT_EQUAL_HEX32(67634689u, gadget_prng_next(&p));
  gadget_prng_seed(&p, 0);
  TEST_ASSERT_EQUAL_HEX32(0x9E3779B9u, p.s);
  gadget_prng_seed(&p, 7);
  for (int i = 0; i < 1000; i++) {
    uint32_t v = gadget_prng_range(&p, 10, 20);
    TEST_ASSERT_TRUE(v >= 10 && v <= 20);
  }
  TEST_ASSERT_EQUAL_UINT32(5, gadget_prng_range(&p, 5, 5));
  TEST_ASSERT_EQUAL_UINT32(9, gadget_prng_range(&p, 9, 3));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_b64_encode_known_values);
  RUN_TEST(test_b64_decode_is_strict);
  RUN_TEST(test_hex_round_trip_and_lowercase_only);
  RUN_TEST(test_der_round_trips);
  RUN_TEST(test_der_rejects_non_canonical);
  RUN_TEST(test_der_from_raw_minimal);
  RUN_TEST(test_validators);
  RUN_TEST(test_utf8_copy_cuts_on_code_points);
  RUN_TEST(test_utf8_copy_tail_keeps_the_end);
  RUN_TEST(test_utf8_len);
  RUN_TEST(test_prng_is_xorshift32);
  return UNITY_END();
}
```

- [ ] **Step 4: Run it to see it fail**

Run: `cmake -S firmware -B build/host -DCMAKE_BUILD_TYPE=Debug -DGADGET_WITH_LVGL=OFF`
Expected: configuration fails with `Cannot find source file:` … `src/util.c`. The first run also downloads cJSON and Unity.

- [ ] **Step 5: Write the implementation**

Create `firmware/core/src/util.c`:

```c
/* firmware/core/src/util.c */
/* SPDX-License-Identifier: Apache-2.0 */
/* Pure helpers (gadget_util.h): base64, hex, DER, validators, UTF-8, PRNG. */
#include <string.h>
#include "gadget_util.h"

/* ---- base64 (RFC 4648 §4, standard alphabet, padded) ------------------- */

static const char B64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

size_t gadget_b64_encode(char *out, size_t cap, const uint8_t *in, size_t len) {
  size_t need = ((len + 2) / 3) * 4;
  if (out == NULL || cap < need + 1) return 0;
  size_t o = 0;
  for (size_t i = 0; i < len; i += 3) {
    uint32_t v = (uint32_t)in[i] << 16;
    if (i + 1 < len) v |= (uint32_t)in[i + 1] << 8;
    if (i + 2 < len) v |= in[i + 2];
    out[o++] = B64[(v >> 18) & 63];
    out[o++] = B64[(v >> 12) & 63];
    out[o++] = (i + 1 < len) ? B64[(v >> 6) & 63] : '=';
    out[o++] = (i + 2 < len) ? B64[v & 63] : '=';
  }
  out[o] = '\0';
  return o;
}

static int b64_value(char c) {
  if (c >= 'A' && c <= 'Z') return c - 'A';
  if (c >= 'a' && c <= 'z') return c - 'a' + 26;
  if (c >= '0' && c <= '9') return c - '0' + 52;
  if (c == '+') return 62;
  if (c == '/') return 63;
  return -1;
}

gadget_status_t gadget_b64_decode(const char *in, uint8_t *out, size_t cap, size_t *len) {
  if (in == NULL || len == NULL) return GADGET_ERR_ARG;
  size_t n = strlen(in);
  if (n % 4 != 0) return GADGET_ERR_PARSE;
  size_t pad = 0;
  if (n >= 1 && in[n - 1] == '=') pad++;
  if (n >= 2 && in[n - 2] == '=') pad++;
  size_t out_len = n / 4 * 3 - pad;
  for (size_t i = 0; i < n - pad; i++) {
    if (b64_value(in[i]) < 0) return GADGET_ERR_PARSE; /* also rejects '=' before the end */
  }
  if (pad > 0) {
    /* the bits that padding hides must be zero, or re-encoding would differ */
    int last = b64_value(in[n - pad - 1]);
    if (pad == 1 && (last & 0x03) != 0) return GADGET_ERR_PARSE;
    if (pad == 2 && (last & 0x0f) != 0) return GADGET_ERR_PARSE;
  }
  if (out_len > cap) return GADGET_ERR_LIMIT;
  size_t o = 0;
  for (size_t i = 0; i < n; i += 4) {
    uint32_t v = 0;
    for (size_t j = 0; j < 4; j++) {
      int d = (in[i + j] == '=') ? 0 : b64_value(in[i + j]);
      v = (v << 6) | (uint32_t)d;
    }
    if (o < out_len) out[o++] = (uint8_t)(v >> 16);
    if (o < out_len) out[o++] = (uint8_t)(v >> 8);
    if (o < out_len) out[o++] = (uint8_t)v;
  }
  *len = out_len;
  return GADGET_OK;
}

/* ---- hex (lowercase only, both ways) ------------------------------------ */

void gadget_hex_encode(char *out, const uint8_t *in, size_t len) {
  static const char H[] = "0123456789abcdef";
  for (size_t i = 0; i < len; i++) {
    out[2 * i] = H[in[i] >> 4];
    out[2 * i + 1] = H[in[i] & 15];
  }
  out[2 * len] = '\0';
}

static int hex_value(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  return -1;
}

gadget_status_t gadget_hex_decode(const char *in, uint8_t *out, size_t cap, size_t *len) {
  if (in == NULL || len == NULL) return GADGET_ERR_ARG;
  size_t n = strlen(in);
  if (n % 2 != 0) return GADGET_ERR_PARSE;
  for (size_t i = 0; i < n; i++) {
    if (hex_value(in[i]) < 0) return GADGET_ERR_PARSE;
  }
  if (n / 2 > cap) return GADGET_ERR_LIMIT;
  for (size_t i = 0; i < n / 2; i++) {
    out[i] = (uint8_t)((hex_value(in[2 * i]) << 4) | hex_value(in[2 * i + 1]));
  }
  *len = n / 2;
  return GADGET_OK;
}

/* ---- ECDSA signature DER <-> raw r||s ----------------------------------- */

/* Writes one DER INTEGER for a 32-byte big-endian unsigned value. */
static size_t der_put_int(uint8_t *out, const uint8_t v[32]) {
  size_t skip = 0;
  while (skip < 31 && v[skip] == 0) skip++;
  size_t n = 32 - skip;
  bool pad = (v[skip] & 0x80) != 0;
  out[0] = 0x02;
  out[1] = (uint8_t)(n + (pad ? 1 : 0));
  size_t o = 2;
  if (pad) out[o++] = 0x00;
  memcpy(out + o, v + skip, n);
  return o + n;
}

gadget_status_t gadget_der_from_raw(const uint8_t raw[64], uint8_t *der, size_t cap, size_t *der_len) {
  if (raw == NULL || der == NULL || der_len == NULL) return GADGET_ERR_ARG;
  uint8_t tmp[GADGET_SIG_DER_MAX];
  size_t o = 2;
  o += der_put_int(tmp + o, raw);
  o += der_put_int(tmp + o, raw + 32);
  tmp[0] = 0x30;
  tmp[1] = (uint8_t)(o - 2);
  if (o > cap) return GADGET_ERR_LIMIT;
  memcpy(der, tmp, o);
  *der_len = o;
  return GADGET_OK;
}

/* Reads one canonical DER INTEGER into a right-aligned 32-byte value. */
static gadget_status_t der_get_int(const uint8_t *p, size_t avail, size_t *used, uint8_t out[32]) {
  if (avail < 3 || p[0] != 0x02) return GADGET_ERR_PARSE;
  size_t n = p[1];
  if (n < 1 || n > 33 || n + 2 > avail) return GADGET_ERR_PARSE;
  const uint8_t *v = p + 2;
  if (v[0] & 0x80) return GADGET_ERR_PARSE;                     /* negative */
  if (n > 1 && v[0] == 0x00 && (v[1] & 0x80) == 0) return GADGET_ERR_PARSE; /* non-minimal */
  if (v[0] == 0x00 && n > 1) {
    v++;
    n--;
  }
  if (n > 32) return GADGET_ERR_PARSE;
  memset(out, 0, 32);
  memcpy(out + 32 - n, v, n);
  *used = (size_t)(p[1]) + 2;
  return GADGET_OK;
}

gadget_status_t gadget_der_to_raw(const uint8_t *der, size_t der_len, uint8_t raw[64]) {
  if (der == NULL || raw == NULL) return GADGET_ERR_ARG;
  if (der_len < 8 || der[0] != 0x30 || der[1] >= 0x80) return GADGET_ERR_PARSE;
  if ((size_t)der[1] + 2 != der_len) return GADGET_ERR_PARSE;
  size_t used = 0, off = 2;
  gadget_status_t st = der_get_int(der + off, der_len - off, &used, raw);
  if (st != GADGET_OK) return st;
  off += used;
  st = der_get_int(der + off, der_len - off, &used, raw + 32);
  if (st != GADGET_OK) return st;
  off += used;
  return off == der_len ? GADGET_OK : GADGET_ERR_PARSE;
}

/* ---- validators --------------------------------------------------------- */

bool gadget_host_id_valid(const char *host_id) {
  if (host_id == NULL || strlen(host_id) != GADGET_HOST_ID_LEN) return false;
  for (size_t i = 0; i < GADGET_HOST_ID_LEN; i++) {
    if (hex_value(host_id[i]) < 0) return false;
  }
  return true;
}

bool gadget_pair_code_valid(const char *code) {
  if (code == NULL || strlen(code) != GADGET_PAIR_CODE_LEN) return false;
  for (size_t i = 0; i < GADGET_PAIR_CODE_LEN; i++) {
    if (code[i] < '0' || code[i] > '9') return false;
  }
  return true;
}

/* ---- UTF-8 -------------------------------------------------------------- */

static const char ELLIPSIS[] = "\xe2\x80\xa6"; /* U+2026 */

static bool is_cont(unsigned char c) { return (c & 0xC0) == 0x80; }

size_t gadget_utf8_copy(char *dst, size_t cap, const char *src) {
  if (dst == NULL || cap == 0) return 0;
  if (src == NULL) src = "";
  size_t n = strlen(src);
  if (n < cap) {
    memcpy(dst, src, n + 1);
    return n;
  }
  bool ell = cap >= 4;
  size_t keep = ell ? cap - 4 : cap - 1;
  while (keep > 0 && is_cont((unsigned char)src[keep])) keep--;
  memcpy(dst, src, keep);
  size_t o = keep;
  if (ell) {
    memcpy(dst + o, ELLIPSIS, 3);
    o += 3;
  }
  dst[o] = '\0';
  return o;
}

size_t gadget_utf8_copy_tail(char *dst, size_t cap, const char *src) {
  if (dst == NULL || cap == 0) return 0;
  if (src == NULL) src = "";
  size_t n = strlen(src);
  if (n < cap) {
    memcpy(dst, src, n + 1);
    return n;
  }
  bool ell = cap >= 4;
  size_t keep = ell ? cap - 4 : cap - 1;
  size_t start = n - keep;
  while (start < n && is_cont((unsigned char)src[start])) start++;
  size_t o = 0;
  if (ell) {
    memcpy(dst, ELLIPSIS, 3);
    o = 3;
  }
  memcpy(dst + o, src + start, n - start);
  o += n - start;
  dst[o] = '\0';
  return o;
}

/* Length of the valid UTF-8 sequence at s, or 0 when it is invalid. */
static size_t utf8_seq_len(const unsigned char *s) {
  size_t n;
  if (s[0] < 0x80) return 1;
  if (s[0] >= 0xC2 && s[0] <= 0xDF) n = 2;
  else if (s[0] >= 0xE0 && s[0] <= 0xEF) n = 3;
  else if (s[0] >= 0xF0 && s[0] <= 0xF4) n = 4;
  else return 0;
  for (size_t i = 1; i < n; i++) {
    if (!is_cont(s[i])) return 0;
  }
  return n;
}

size_t gadget_utf8_len(const char *s) {
  if (s == NULL) return 0;
  size_t count = 0;
  const unsigned char *p = (const unsigned char *)s;
  while (*p) {
    size_t n = utf8_seq_len(p);
    p += n ? n : 1;
    count++;
  }
  return count;
}

/* ---- xorshift32 --------------------------------------------------------- */

void gadget_prng_seed(gadget_prng_t *p, uint32_t seed) { p->s = seed ? seed : 0x9E3779B9u; }

uint32_t gadget_prng_next(gadget_prng_t *p) {
  uint32_t s = p->s;
  s ^= s << 13;
  s ^= s >> 17;
  s ^= s << 5;
  p->s = s;
  return s;
}

uint32_t gadget_prng_range(gadget_prng_t *p, uint32_t lo, uint32_t hi) {
  if (hi <= lo) return lo;
  uint32_t span = hi - lo + 1u;
  uint32_t v = gadget_prng_next(p);
  return span == 0 ? v : lo + v % span;
}
```

- [ ] **Step 6: Run the tests**

Run: `cmake -S firmware -B build/host -DCMAKE_BUILD_TYPE=Debug -DGADGET_WITH_LVGL=OFF && cmake --build build/host -j10 && ctest --test-dir build/host --output-on-failure -L "unit|vectors"`
Expected: `100% tests passed, 0 tests failed out of 1`, and `./build/host/tests/test_util` prints `11 Tests 0 Failures 0 Ignored`. The build shows no compiler warnings, since they are errors.

- [ ] **Step 7: Commit**

```bash
git add firmware/CMakeLists.txt firmware/cmake firmware/core firmware/tests
git commit -m "firmware: desktop build, contract headers and pure helpers" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 2: Protocol codec

**Files:**
- Create: `firmware/core/src/proto.c`
- Modify: `firmware/core/CMakeLists.txt`, `firmware/tests/CMakeLists.txt`
- Test: `firmware/tests/test_proto.c`

**Interfaces:**
- Consumes: `gadget_proto.h`, `gadget_board.h` and `gadget_actions.h` from Task 1; cJSON.
- Produces: every `gp_*` function of contract §2.12:
  - `gp_decode` returns `GADGET_OK` with `GP_OP_UNKNOWN` for unknown ops, and for gadget→host ops that arrive from a host. It returns `GADGET_ERR_PARSE` for bad JSON, a missing `op`, or a missing or out-of-range required field. Strings point into `m.root` until `gp_msg_free`.
  - The encoders write compact JSON in the field order shown in the tests, and return the length or `GADGET_ERR_LIMIT`. The `hello` encoder builds `caps` from the board descriptor exactly as contract §2.3 shows.

- [ ] **Step 1: Write the failing test**

Create `firmware/tests/test_proto.c`:

```c
/* firmware/tests/test_proto.c */
/* SPDX-License-Identifier: Apache-2.0 */
#include <string.h>
#include "gadget_proto.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

/* A local copy of the amoled-175c descriptor (contract §2.3), so this test
 * does not depend on boards.c. */
static const gadget_board_t AMOLED = {
    .id = "amoled-175c", .display_name = "Waveshare ESP32-S3-Touch-AMOLED-1.75C",
    .screen_w = 466, .screen_h = 466, .screen_round = true, .image_w = 300, .image_h = 300,
    .mic_rate = 16000, .speaker_rate = 16000,
    .input_mask = GADGET_INPUT_TOUCH | GADGET_INPUT_TALK | GADGET_INPUT_CANCEL,
    .has_battery = true, .ota_max = 6291456, .art_profile = GADGET_ART_S240};

static const gadget_board_t DEVKIT_NO_SPK = {
    .id = "devkit", .display_name = "x", .screen_w = 320, .screen_h = 240, .screen_round = false,
    .image_w = 280, .image_h = 200, .mic_rate = 16000, .speaker_rate = 0,
    .input_mask = GADGET_INPUT_TALK | GADGET_INPUT_CANCEL, .has_battery = false, .ota_max = 6291456,
    .art_profile = GADGET_ART_S150};

static gp_msg_t decode_ok(const char *json) {
  gp_msg_t m;
  TEST_ASSERT_EQUAL_INT_MESSAGE(GADGET_OK, gp_decode(json, strlen(json), &m), json);
  return m;
}

static void test_op_names_round_trip(void) {
  for (int op = 1; op < GP_OP__COUNT; op++) {
    const char *name = gp_op_name((gp_op_t)op);
    TEST_ASSERT_NOT_NULL(name);
    TEST_ASSERT_EQUAL_INT(op, gp_op_from_name(name));
  }
  TEST_ASSERT_NULL(gp_op_name(GP_OP_UNKNOWN));
  TEST_ASSERT_EQUAL_INT(GP_OP_UNKNOWN, gp_op_from_name("nope"));
  TEST_ASSERT_EQUAL_STRING("fw.offer", gp_op_name(GP_OP_FW_OFFER));
  TEST_ASSERT_EQUAL_STRING("speak.begin", gp_op_name(GP_OP_SPEAK_BEGIN));
}

static void test_decode_handshake_ops(void) {
  gp_msg_t m = decode_ok("{\"op\":\"challenge\",\"nonce\":\"AAEC\",\"host_id\":\"000102030405060708090a0b0c0d0e0f\",\"host_name\":\"Mac\",\"extra\":1}");
  TEST_ASSERT_EQUAL_INT(GP_OP_CHALLENGE, m.op);
  TEST_ASSERT_EQUAL_STRING("AAEC", m.m.challenge.nonce);
  TEST_ASSERT_EQUAL_STRING("000102030405060708090a0b0c0d0e0f", m.m.challenge.host_id);
  TEST_ASSERT_EQUAL_STRING("Mac", m.m.challenge.host_name);
  gp_msg_free(&m);

  m = decode_ok("{\"op\":\"ready\",\"session\":\"s_81c2\",\"bot\":{\"id\":\"b_jev\",\"name\":\"Jev\"},\"settings\":{\"speak_pushes\":true}}");
  TEST_ASSERT_EQUAL_INT(GP_OP_READY, m.op);
  TEST_ASSERT_EQUAL_STRING("s_81c2", m.m.ready.session);
  TEST_ASSERT_EQUAL_STRING("b_jev", m.m.ready.bot.id);
  TEST_ASSERT_EQUAL_STRING("Jev", m.m.ready.bot.name);
  TEST_ASSERT_TRUE(m.m.ready.settings.speak_pushes);
  gp_msg_free(&m);

  m = decode_ok("{\"op\":\"error\",\"code\":\"bad_code\",\"message\":\"Wrong code\"}");
  TEST_ASSERT_EQUAL_STRING("bad_code", m.m.error.code);
  TEST_ASSERT_EQUAL_STRING("Wrong code", m.m.error.message);
  gp_msg_free(&m);

  m = decode_ok("{\"op\":\"settings\",\"name\":\"Desk\"}");
  TEST_ASSERT_FALSE(m.m.settings.has_bot);
  TEST_ASSERT_FALSE(m.m.settings.has_settings);
  TEST_ASSERT_EQUAL_STRING("Desk", m.m.settings.name);
  gp_msg_free(&m);
  m = decode_ok("{\"op\":\"settings\",\"bot\":{\"id\":\"b\",\"name\":\"B\"},\"settings\":{\"speak_pushes\":false}}");
  TEST_ASSERT_TRUE(m.m.settings.has_bot);
  TEST_ASSERT_TRUE(m.m.settings.has_settings);
  TEST_ASSERT_NULL(m.m.settings.name);
  gp_msg_free(&m);
}

static void test_decode_conversation_ops(void) {
  gp_msg_t m = decode_ok("{\"op\":\"reply\",\"turn\":\"t1-1\",\"text\":\"Hi\",\"final\":true}");
  TEST_ASSERT_EQUAL_INT(GP_OP_REPLY, m.op);
  TEST_ASSERT_TRUE(m.m.reply.final);
  gp_msg_free(&m);
  m = decode_ok("{\"op\":\"reply\",\"turn\":\"t1-1\",\"text\":\"Hi\"}");
  TEST_ASSERT_FALSE(m.m.reply.final);
  gp_msg_free(&m);
  m = decode_ok("{\"op\":\"done\",\"turn\":\"t1-1\",\"outcome\":\"failed\",\"reason\":\"Didn't catch that\"}");
  TEST_ASSERT_EQUAL_INT(GP_OUTCOME_FAILED, m.m.done.outcome);
  TEST_ASSERT_EQUAL_STRING("Didn't catch that", m.m.done.reason);
  gp_msg_free(&m);
  m = decode_ok("{\"op\":\"done\",\"turn\":\"t1-1\",\"outcome\":\"stopped\"}");
  TEST_ASSERT_EQUAL_INT(GP_OUTCOME_STOPPED, m.m.done.outcome);
  TEST_ASSERT_NULL(m.m.done.reason);
  gp_msg_free(&m);
  m = decode_ok("{\"op\":\"speak.begin\",\"stream\":3,\"rate\":24000}");
  TEST_ASSERT_EQUAL_UINT8(3, m.m.speak_begin.stream);
  TEST_ASSERT_EQUAL_UINT32(24000, m.m.speak_begin.rate);
  TEST_ASSERT_NULL(m.m.speak_begin.turn);
  gp_msg_free(&m);
  m = decode_ok("{\"op\":\"heard\",\"turn\":\"t\",\"text\":\"x\"}");
  gp_msg_free(&m);
  m = decode_ok("{\"op\":\"working\",\"turn\":\"t\",\"text\":\"\"}");
  TEST_ASSERT_EQUAL_STRING("", m.m.working.text);
  gp_msg_free(&m);
}

static void test_decode_display_ops(void) {
  gp_msg_t m = decode_ok(
      "{\"op\":\"ask\",\"id\":\"a_1\",\"kind\":\"permission\",\"title\":\"Run?\",\"body\":\"ls\","
      "\"options\":[{\"id\":\"allow\",\"label\":\"Allow\",\"style\":\"allow\"},{\"id\":\"deny\",\"label\":\"Deny\",\"style\":\"deny\"}],"
      "\"expires_s\":30}");
  TEST_ASSERT_EQUAL_INT(GP_ASK_PERMISSION, m.m.ask.kind);
  TEST_ASSERT_EQUAL_UINT8(2, m.m.ask.n_options);
  TEST_ASSERT_EQUAL_INT(GP_STYLE_ALLOW, m.m.ask.options[0].style);
  TEST_ASSERT_EQUAL_INT(GP_STYLE_DENY, m.m.ask.options[1].style);
  TEST_ASSERT_EQUAL_STRING("deny", m.m.ask.options[1].id);
  TEST_ASSERT_EQUAL_UINT32(30, m.m.ask.expires_s);
  gp_msg_free(&m);
  m = decode_ok("{\"op\":\"ask\",\"id\":\"a_2\",\"kind\":\"question\",\"title\":\"Q\",\"body\":\"B\",\"options\":[]}");
  TEST_ASSERT_EQUAL_INT(GP_ASK_QUESTION, m.m.ask.kind);
  TEST_ASSERT_EQUAL_UINT8(0, m.m.ask.n_options);
  gp_msg_free(&m);
  m = decode_ok("{\"op\":\"post\",\"id\":\"p1\",\"bot\":{\"id\":\"b\",\"name\":\"Jev\"},\"kind\":\"routine\",\"text\":\"Done\",\"speak\":true}");
  TEST_ASSERT_EQUAL_INT(GP_POST_ROUTINE, m.m.post.kind);
  TEST_ASSERT_TRUE(m.m.post.speak);
  gp_msg_free(&m);
  m = decode_ok("{\"op\":\"card\",\"id\":\"c1\",\"title\":\"T\",\"body\":\"B\",\"ttl_s\":0}");
  TEST_ASSERT_EQUAL_UINT32(0, m.m.card.ttl_s);
  gp_msg_free(&m);
  m = decode_ok("{\"op\":\"image.begin\",\"id\":\"i1\",\"stream\":7,\"w\":300,\"h\":200,\"ttl_s\":10}");
  TEST_ASSERT_EQUAL_UINT16(300, m.m.image_begin.w);
  TEST_ASSERT_EQUAL_UINT16(200, m.m.image_begin.h);
  gp_msg_free(&m);
  m = decode_ok("{\"op\":\"act\",\"id\":\"x1\",\"name\":\"chime\"}");
  TEST_ASSERT_NULL(m.m.act.args);
  gp_msg_free(&m);
  m = decode_ok("{\"op\":\"act\",\"id\":\"x1\",\"name\":\"relay\",\"args\":{\"on\":true}}");
  TEST_ASSERT_TRUE(cJSON_IsObject(m.m.act.args));
  gp_msg_free(&m);
  m = decode_ok("{\"op\":\"fw.offer\",\"stream\":1,\"board\":\"amoled-175c\",\"version\":\"1.1.0\",\"size\":1234567,"
                "\"sha256\":\"e3b0\",\"sig\":\"MEYC\",\"key_id\":\"t1\"}");
  TEST_ASSERT_EQUAL_UINT32(1234567, m.m.fw_offer.size);
  TEST_ASSERT_EQUAL_STRING("t1", m.m.fw_offer.key_id);
  gp_msg_free(&m);
}

static void test_decode_rejects_and_ignores(void) {
  gp_msg_t m;
  TEST_ASSERT_EQUAL_INT(GADGET_ERR_PARSE, gp_decode("{", 1, &m));
  TEST_ASSERT_EQUAL_INT(GADGET_ERR_PARSE, gp_decode("[]", 2, &m));
  TEST_ASSERT_EQUAL_INT(GADGET_ERR_PARSE, gp_decode("{\"x\":1}", 7, &m));
  const char *missing = "{\"op\":\"reply\",\"text\":\"no turn\"}";
  TEST_ASSERT_EQUAL_INT(GADGET_ERR_PARSE, gp_decode(missing, strlen(missing), &m));
  const char *bad_stream = "{\"op\":\"speak.end\",\"stream\":0}";
  TEST_ASSERT_EQUAL_INT(GADGET_ERR_PARSE, gp_decode(bad_stream, strlen(bad_stream), &m));
  const char *five = "{\"op\":\"ask\",\"id\":\"a\",\"kind\":\"question\",\"title\":\"t\",\"body\":\"b\",\"options\":["
                     "{\"id\":\"1\",\"label\":\"1\"},{\"id\":\"2\",\"label\":\"2\"},{\"id\":\"3\",\"label\":\"3\"},"
                     "{\"id\":\"4\",\"label\":\"4\"},{\"id\":\"5\",\"label\":\"5\"}]}";
  TEST_ASSERT_EQUAL_INT(GADGET_ERR_PARSE, gp_decode(five, strlen(five), &m));
  m = decode_ok("{\"op\":\"future.thing\",\"a\":1}");
  TEST_ASSERT_EQUAL_INT(GP_OP_UNKNOWN, m.op);
  TEST_ASSERT_EQUAL_STRING("future.thing", cJSON_GetObjectItemCaseSensitive(m.root, "op")->valuestring);
  gp_msg_free(&m);
  /* a gadget → host op arriving from the host is ignored like an unknown op */
  m = decode_ok("{\"op\":\"hello\"}");
  TEST_ASSERT_EQUAL_INT(GP_OP_UNKNOWN, m.op);
  gp_msg_free(&m);
}

static void test_encode_hello_exact(void) {
  static const gp_action_decl_t chime = {"chime", "Play a short chime.", NULL, GADGET_RISK_SAFE};
  gp_hello_t h = {.id = "gad_3f9a0c2b7e41d856", .pubkey_b64 = "BHx=", .name = "Desk Maus", .fw = "1.0.0",
                  .board = &AMOLED, .actions = &chime, .n_actions = 1,
                  .battery_valid = true, .battery_pct = 82, .charging = false};
  char buf[GADGET_TEXT_FRAME_MAX];
  int n = gp_encode_hello(buf, sizeof buf, &h);
  const char *want =
      "{\"op\":\"hello\",\"proto\":1,\"id\":\"gad_3f9a0c2b7e41d856\",\"pubkey\":\"BHx=\",\"name\":\"Desk Maus\","
      "\"board\":\"amoled-175c\",\"fw\":\"1.0.0\","
      "\"caps\":{\"screen\":{\"w\":466,\"h\":466,\"round\":true,\"text\":\"latin1\"},\"image\":{\"w\":300,\"h\":300},"
      "\"mic\":{\"rate\":16000},\"speaker\":{\"rate\":16000},\"input\":[\"touch\",\"talk\",\"cancel\"],"
      "\"battery\":true,\"ota\":{\"max\":6291456}},"
      "\"actions\":[{\"name\":\"chime\",\"description\":\"Play a short chime.\","
      "\"params\":{\"type\":\"object\",\"properties\":{}},\"risk\":\"safe\"}],"
      "\"sensors\":{\"battery_pct\":82,\"charging\":false}}";
  TEST_ASSERT_EQUAL_STRING(want, buf);
  TEST_ASSERT_EQUAL_INT((int)strlen(want), n);
}

static void test_encode_hello_without_speaker_or_battery(void) {
  gp_hello_t h = {.id = "gad_x", .pubkey_b64 = "B", .name = "n", .fw = "0.0.0-dev", .board = &DEVKIT_NO_SPK};
  char buf[GADGET_TEXT_FRAME_MAX];
  TEST_ASSERT_GREATER_THAN_INT(0, gp_encode_hello(buf, sizeof buf, &h));
  TEST_ASSERT_NULL(strstr(buf, "\"speaker\""));
  TEST_ASSERT_NULL(strstr(buf, "\"battery\""));
  TEST_ASSERT_NOT_NULL(strstr(buf, "\"input\":[\"talk\",\"cancel\"]"));
  TEST_ASSERT_NOT_NULL(strstr(buf, "\"actions\":[],\"sensors\":{}"));
}

static void test_encode_small_ops(void) {
  char buf[256];
  gp_prove_t p = {.sig_b64 = "MEYC", .enroll = NULL};
  gp_encode_prove(buf, sizeof buf, &p);
  TEST_ASSERT_EQUAL_STRING("{\"op\":\"prove\",\"sig\":\"MEYC\"}", buf);
  p.enroll = "123456";
  gp_encode_prove(buf, sizeof buf, &p);
  TEST_ASSERT_EQUAL_STRING("{\"op\":\"prove\",\"sig\":\"MEYC\",\"enroll\":\"123456\"}", buf);
  gp_voice_begin_t vb = {"t1a2b3c4d-1", 4, 16000};
  gp_encode_voice_begin(buf, sizeof buf, &vb);
  TEST_ASSERT_EQUAL_STRING("{\"op\":\"voice.begin\",\"turn\":\"t1a2b3c4d-1\",\"stream\":4,\"rate\":16000}", buf);
  gp_voice_end_t ve = {"t", 1240};
  gp_encode_voice_end(buf, sizeof buf, &ve);
  TEST_ASSERT_EQUAL_STRING("{\"op\":\"voice.end\",\"turn\":\"t\",\"ms\":1240}", buf);
  gp_voice_drop_t vd = {"t"};
  gp_encode_voice_drop(buf, sizeof buf, &vd);
  TEST_ASSERT_EQUAL_STRING("{\"op\":\"voice.drop\",\"turn\":\"t\"}", buf);
  gp_say_t s = {"t", "caf\xc3\xa9 \"x\""};
  gp_encode_say(buf, sizeof buf, &s);
  TEST_ASSERT_EQUAL_STRING("{\"op\":\"say\",\"turn\":\"t\",\"text\":\"caf\xc3\xa9 \\\"x\\\"\"}", buf);
  gp_stop_t st = {NULL};
  gp_encode_stop(buf, sizeof buf, &st);
  TEST_ASSERT_EQUAL_STRING("{\"op\":\"stop\"}", buf);
  gp_answer_t a = {"a_1", "allow"};
  gp_encode_answer(buf, sizeof buf, &a);
  TEST_ASSERT_EQUAL_STRING("{\"op\":\"answer\",\"id\":\"a_1\",\"option\":\"allow\"}", buf);
  gp_act_result_t r = {"x1", false, NULL, "unknown action"};
  gp_encode_act_result(buf, sizeof buf, &r);
  TEST_ASSERT_EQUAL_STRING("{\"op\":\"act.result\",\"id\":\"x1\",\"ok\":false,\"error\":\"unknown action\"}", buf);
  cJSON *data = cJSON_CreateObject();
  cJSON_AddNumberToObject(data, "n", 3);
  gp_act_result_t r2 = {"x2", true, data, NULL};
  gp_encode_act_result(buf, sizeof buf, &r2);
  TEST_ASSERT_EQUAL_STRING("{\"op\":\"act.result\",\"id\":\"x2\",\"ok\":true,\"data\":{\"n\":3}}", buf);
  cJSON_Delete(data);
  gp_sense_t se = {true, 81, true};
  gp_encode_sense(buf, sizeof buf, &se);
  TEST_ASSERT_EQUAL_STRING("{\"op\":\"sense\",\"battery_pct\":81,\"charging\":true}", buf);
  gp_event_msg_t ev = {"button.long_press", NULL};
  gp_encode_event(buf, sizeof buf, &ev);
  TEST_ASSERT_EQUAL_STRING("{\"op\":\"event\",\"name\":\"button.long_press\"}", buf);
  gp_fw_ready_t fr = {2};
  gp_encode_fw_ready(buf, sizeof buf, &fr);
  TEST_ASSERT_EQUAL_STRING("{\"op\":\"fw.ready\",\"stream\":2}", buf);
  gp_fw_fail_t ff = {2, "bad_sig"};
  gp_encode_fw_fail(buf, sizeof buf, &ff);
  TEST_ASSERT_EQUAL_STRING("{\"op\":\"fw.fail\",\"stream\":2,\"code\":\"bad_sig\"}", buf);
  gp_fw_progress_t fp = {2, 6291456};
  gp_encode_fw_progress(buf, sizeof buf, &fp);
  TEST_ASSERT_EQUAL_STRING("{\"op\":\"fw.progress\",\"stream\":2,\"offset\":6291456}", buf);
  gp_fw_installed_t fi = {"1.1.0"};
  gp_encode_fw_installed(buf, sizeof buf, &fi);
  TEST_ASSERT_EQUAL_STRING("{\"op\":\"fw.installed\",\"version\":\"1.1.0\"}", buf);
  /* too small a buffer */
  TEST_ASSERT_EQUAL_INT(GADGET_ERR_LIMIT, gp_encode_fw_installed(buf, 10, &fi));
}

static void test_binary_frames(void) {
  uint8_t frame[16];
  const uint8_t pay[3] = {9, 8, 7};
  TEST_ASSERT_EQUAL_size_t(5, gp_bin_encode(frame, sizeof frame, GP_BIN_MIC, 5, pay, 3));
  TEST_ASSERT_EQUAL_HEX8(0x01, frame[0]);
  TEST_ASSERT_EQUAL_HEX8(5, frame[1]);
  TEST_ASSERT_EQUAL_size_t(0, gp_bin_encode(frame, 4, GP_BIN_MIC, 5, pay, 3));
  TEST_ASSERT_EQUAL_size_t(0, gp_bin_encode(frame, sizeof frame, GP_BIN_MIC, 0, pay, 3));
  gp_bin_kind_t kind;
  uint8_t stream;
  const uint8_t *p;
  size_t plen;
  TEST_ASSERT_EQUAL_INT(GADGET_OK, gp_bin_decode(frame, 5, &kind, &stream, &p, &plen));
  TEST_ASSERT_EQUAL_INT(GP_BIN_MIC, kind);
  TEST_ASSERT_EQUAL_size_t(3, plen);
  const uint8_t bad_kind[] = {0x09, 1, 0};
  TEST_ASSERT_EQUAL_INT(GADGET_ERR_PARSE, gp_bin_decode(bad_kind, 3, &kind, &stream, &p, &plen));
  TEST_ASSERT_EQUAL_INT(GADGET_ERR_PARSE, gp_bin_decode(frame, 1, &kind, &stream, &p, &plen));
  const uint8_t chunk[] = {0x00, 0x00, 0x01, 0x00, 0xAA, 0xBB};
  uint32_t off;
  const uint8_t *data;
  size_t dlen;
  TEST_ASSERT_EQUAL_INT(GADGET_OK, gp_fw_chunk_decode(chunk, sizeof chunk, &off, &data, &dlen));
  TEST_ASSERT_EQUAL_UINT32(65536, off);
  TEST_ASSERT_EQUAL_size_t(2, dlen);
  TEST_ASSERT_EQUAL_INT(GADGET_ERR_PARSE, gp_fw_chunk_decode(chunk, 4, &off, &data, &dlen));
}

static void test_signed_texts(void) {
  char out[256];
  int n = gp_prove_text(out, sizeof out, "gad_b18b86ce1389e46d", "AAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8=",
                        "000102030405060708090a0b0c0d0e0f");
  const char *want = "openmausbot-gadget/1\nprove\ngad_b18b86ce1389e46d\nAAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8=\n"
                     "000102030405060708090a0b0c0d0e0f";
  TEST_ASSERT_EQUAL_STRING(want, out);
  TEST_ASSERT_EQUAL_INT((int)strlen(want), n);
  gp_firmware_text(out, sizeof out, "amoled-175c", "1.1.0", 1234567,
                   "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
  TEST_ASSERT_EQUAL_STRING("openmausbot-gadget/1\nfirmware\namoled-175c\n1.1.0\n1234567\n"
                           "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
                           out);
  TEST_ASSERT_EQUAL_INT(GADGET_ERR_LIMIT, gp_prove_text(out, 10, "a", "b", "c"));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_op_names_round_trip);
  RUN_TEST(test_decode_handshake_ops);
  RUN_TEST(test_decode_conversation_ops);
  RUN_TEST(test_decode_display_ops);
  RUN_TEST(test_decode_rejects_and_ignores);
  RUN_TEST(test_encode_hello_exact);
  RUN_TEST(test_encode_hello_without_speaker_or_battery);
  RUN_TEST(test_encode_small_ops);
  RUN_TEST(test_binary_frames);
  RUN_TEST(test_signed_texts);
  return UNITY_END();
}
```

Change `firmware/tests/CMakeLists.txt` exactly as this diff shows (`-` lines go, `+` lines come; the rest is context):

```diff
--- a/firmware/tests/CMakeLists.txt
+++ b/firmware/tests/CMakeLists.txt
@@ -12,3 +12,4 @@ function(gadget_pure_test area)
 endfunction()
 
 gadget_pure_test(util)
+gadget_pure_test(proto)
```

- [ ] **Step 2: Run it to see it fail**

Run: `cmake --build build/host -j10`
Expected: the link of `test_proto` fails with `Undefined symbols` (macOS) or `undefined reference` (Linux) for `gp_decode`, `gp_encode_hello` and the other codec functions.

- [ ] **Step 3: Write the implementation**

Create `firmware/core/src/proto.c`:

```c
/* firmware/core/src/proto.c */
/* SPDX-License-Identifier: Apache-2.0 */
/* openmausbot-gadget/1 codec (gadget_proto.h, protocol/PROTOCOL.md). */
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include "gadget_proto.h"

static const char *const OP_NAMES[GP_OP__COUNT] = {
    [GP_OP_UNKNOWN] = NULL,
    [GP_OP_CHALLENGE] = "challenge", [GP_OP_READY] = "ready", [GP_OP_ERROR] = "error",
    [GP_OP_SETTINGS] = "settings", [GP_OP_HEARD] = "heard", [GP_OP_WORKING] = "working",
    [GP_OP_REPLY] = "reply", [GP_OP_DONE] = "done", [GP_OP_SPEAK_BEGIN] = "speak.begin",
    [GP_OP_SPEAK_END] = "speak.end", [GP_OP_SPEAK_STOP] = "speak.stop", [GP_OP_ASK] = "ask",
    [GP_OP_ASK_CLOSE] = "ask.close", [GP_OP_POST] = "post", [GP_OP_CARD] = "card",
    [GP_OP_CARD_CLOSE] = "card.close", [GP_OP_IMAGE_BEGIN] = "image.begin",
    [GP_OP_IMAGE_END] = "image.end", [GP_OP_ACT] = "act", [GP_OP_FW_OFFER] = "fw.offer",
    [GP_OP_FW_COMMIT] = "fw.commit",
    [GP_OP_HELLO] = "hello", [GP_OP_PROVE] = "prove", [GP_OP_VOICE_BEGIN] = "voice.begin",
    [GP_OP_VOICE_END] = "voice.end", [GP_OP_VOICE_DROP] = "voice.drop", [GP_OP_SAY] = "say",
    [GP_OP_STOP] = "stop", [GP_OP_ANSWER] = "answer", [GP_OP_ACT_RESULT] = "act.result",
    [GP_OP_SENSE] = "sense", [GP_OP_EVENT] = "event", [GP_OP_FW_READY] = "fw.ready",
    [GP_OP_FW_FAIL] = "fw.fail", [GP_OP_FW_PROGRESS] = "fw.progress",
    [GP_OP_FW_INSTALLED] = "fw.installed",
};

const char *gp_op_name(gp_op_t op) {
  if (op <= GP_OP_UNKNOWN || op >= GP_OP__COUNT) return NULL;
  return OP_NAMES[op];
}

gp_op_t gp_op_from_name(const char *name) {
  if (name == NULL) return GP_OP_UNKNOWN;
  for (int op = 1; op < GP_OP__COUNT; op++) {
    if (strcmp(OP_NAMES[op], name) == 0) return (gp_op_t)op;
  }
  return GP_OP_UNKNOWN;
}

/* ---- decode helpers: each returns false when a required field is bad ---- */

static bool str_field(const cJSON *o, const char *key, bool required, const char **out) {
  const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, key);
  if (v == NULL) {
    *out = NULL;
    return !required;
  }
  if (!cJSON_IsString(v) || v->valuestring == NULL) return false;
  *out = v->valuestring;
  return true;
}

static bool u32_field(const cJSON *o, const char *key, bool required, uint32_t lo, uint32_t hi, uint32_t *out) {
  const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, key);
  if (v == NULL) {
    *out = 0;
    return !required;
  }
  if (!cJSON_IsNumber(v)) return false;
  double d = v->valuedouble;
  if (!(d >= (double)lo && d <= (double)hi)) return false; /* also rejects NaN */
  if ((double)(uint32_t)d != d) return false;            /* integers only */
  *out = (uint32_t)d;
  return true;
}

static bool bool_field(const cJSON *o, const char *key, bool *out) {
  const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, key);
  *out = false;
  if (v == NULL) return true;
  if (!cJSON_IsBool(v)) return false;
  *out = cJSON_IsTrue(v);
  return true;
}

static bool stream_field(const cJSON *o, uint8_t *out) {
  uint32_t v;
  if (!u32_field(o, "stream", true, 1, 255, &v)) return false;
  *out = (uint8_t)v;
  return true;
}

static bool bot_field(const cJSON *o, const char *key, bool required, gp_bot_t *out, bool *present) {
  const cJSON *b = cJSON_GetObjectItemCaseSensitive(o, key);
  if (present) *present = b != NULL;
  if (b == NULL) return !required;
  if (!cJSON_IsObject(b)) return false;
  return str_field(b, "id", true, &out->id) && str_field(b, "name", true, &out->name);
}

static bool settings_field(const cJSON *o, gp_settings_values_t *out, bool *present) {
  const cJSON *s = cJSON_GetObjectItemCaseSensitive(o, "settings");
  if (present) *present = s != NULL;
  if (s == NULL) return true;
  if (!cJSON_IsObject(s)) return false;
  return bool_field(s, "speak_pushes", &out->speak_pushes);
}

static gp_style_t style_from(const char *s) {
  if (s && strcmp(s, "allow") == 0) return GP_STYLE_ALLOW;
  if (s && strcmp(s, "deny") == 0) return GP_STYLE_DENY;
  return GP_STYLE_NEUTRAL;
}

static bool decode_ask(const cJSON *o, gp_ask_t *a) {
  const char *kind = NULL;
  if (!str_field(o, "id", true, &a->id) || !str_field(o, "kind", true, &kind) ||
      !str_field(o, "title", true, &a->title) || !str_field(o, "body", false, &a->body) ||
      !u32_field(o, "expires_s", false, 0, 86400, &a->expires_s)) {
    return false;
  }
  a->kind = strcmp(kind, "permission") == 0 ? GP_ASK_PERMISSION : GP_ASK_QUESTION;
  const cJSON *opts = cJSON_GetObjectItemCaseSensitive(o, "options");
  if (opts == NULL) return true;
  if (!cJSON_IsArray(opts) || cJSON_GetArraySize(opts) > 4) return false;
  const cJSON *it = NULL;
  cJSON_ArrayForEach(it, opts) {
    gp_option_t *op = &a->options[a->n_options];
    const char *style = NULL;
    if (!cJSON_IsObject(it) || !str_field(it, "id", true, &op->id) || !str_field(it, "label", true, &op->label) ||
        !str_field(it, "style", false, &style)) {
      return false;
    }
    op->style = style_from(style);
    a->n_options++;
  }
  return true;
}

static bool decode_fields(const cJSON *o, gp_msg_t *out) {
  switch (out->op) {
    case GP_OP_CHALLENGE: {
      gp_challenge_t *c = &out->m.challenge;
      return str_field(o, "nonce", true, &c->nonce) && str_field(o, "host_id", true, &c->host_id) &&
             str_field(o, "host_name", false, &c->host_name);
    }
    case GP_OP_READY: {
      gp_ready_t *r = &out->m.ready;
      return str_field(o, "session", true, &r->session) && bot_field(o, "bot", true, &r->bot, NULL) &&
             settings_field(o, &r->settings, NULL);
    }
    case GP_OP_ERROR:
      return str_field(o, "code", true, &out->m.error.code) && str_field(o, "message", false, &out->m.error.message);
    case GP_OP_SETTINGS: {
      gp_settings_t *s = &out->m.settings;
      return bot_field(o, "bot", false, &s->bot, &s->has_bot) && settings_field(o, &s->settings, &s->has_settings) &&
             str_field(o, "name", false, &s->name);
    }
    case GP_OP_HEARD:
      return str_field(o, "turn", true, &out->m.heard.turn) && str_field(o, "text", true, &out->m.heard.text);
    case GP_OP_WORKING:
      return str_field(o, "turn", true, &out->m.working.turn) && str_field(o, "text", true, &out->m.working.text);
    case GP_OP_REPLY:
      return str_field(o, "turn", true, &out->m.reply.turn) && str_field(o, "text", true, &out->m.reply.text) &&
             bool_field(o, "final", &out->m.reply.final);
    case GP_OP_DONE: {
      const char *outcome = NULL;
      gp_done_t *d = &out->m.done;
      if (!str_field(o, "turn", true, &d->turn) || !str_field(o, "outcome", true, &outcome) ||
          !str_field(o, "reason", false, &d->reason)) {
        return false;
      }
      d->outcome = strcmp(outcome, "ok") == 0        ? GP_OUTCOME_OK
                   : strcmp(outcome, "stopped") == 0 ? GP_OUTCOME_STOPPED
                                                     : GP_OUTCOME_FAILED;
      return true;
    }
    case GP_OP_SPEAK_BEGIN:
      return stream_field(o, &out->m.speak_begin.stream) &&
             u32_field(o, "rate", true, 8000, 48000, &out->m.speak_begin.rate) &&
             str_field(o, "turn", false, &out->m.speak_begin.turn);
    case GP_OP_SPEAK_END:
      return stream_field(o, &out->m.speak_end.stream);
    case GP_OP_SPEAK_STOP:
      return stream_field(o, &out->m.speak_stop.stream);
    case GP_OP_ASK:
      return decode_ask(o, &out->m.ask);
    case GP_OP_ASK_CLOSE:
      return str_field(o, "id", true, &out->m.ask_close.id) && str_field(o, "reason", false, &out->m.ask_close.reason);
    case GP_OP_POST: {
      gp_post_t *p = &out->m.post;
      const char *kind = NULL;
      if (!str_field(o, "id", true, &p->id) || !bot_field(o, "bot", true, &p->bot, NULL) ||
          !str_field(o, "kind", true, &kind) || !str_field(o, "text", true, &p->text) ||
          !bool_field(o, "speak", &p->speak)) {
        return false;
      }
      p->kind = strcmp(kind, "routine") == 0 ? GP_POST_ROUTINE : GP_POST_MESSAGE;
      return true;
    }
    case GP_OP_CARD: {
      gp_card_t *c = &out->m.card;
      return str_field(o, "id", true, &c->id) && str_field(o, "title", true, &c->title) &&
             str_field(o, "body", false, &c->body) && u32_field(o, "ttl_s", false, 0, 86400, &c->ttl_s);
    }
    case GP_OP_CARD_CLOSE:
      return str_field(o, "id", true, &out->m.card_close.id);
    case GP_OP_IMAGE_BEGIN: {
      gp_image_begin_t *b = &out->m.image_begin;
      uint32_t w = 0, h = 0;
      if (!str_field(o, "id", true, &b->id) || !stream_field(o, &b->stream) || !u32_field(o, "w", true, 1, 4096, &w) ||
          !u32_field(o, "h", true, 1, 4096, &h) || !u32_field(o, "ttl_s", false, 0, 86400, &b->ttl_s)) {
        return false;
      }
      b->w = (uint16_t)w;
      b->h = (uint16_t)h;
      return true;
    }
    case GP_OP_IMAGE_END:
      return stream_field(o, &out->m.image_end.stream);
    case GP_OP_ACT: {
      gp_act_t *a = &out->m.act;
      if (!str_field(o, "id", true, &a->id) || !str_field(o, "name", true, &a->name)) return false;
      const cJSON *args = cJSON_GetObjectItemCaseSensitive(o, "args");
      if (args != NULL && !cJSON_IsObject(args)) return false;
      a->args = args;
      return true;
    }
    case GP_OP_FW_OFFER: {
      gp_fw_offer_t *f = &out->m.fw_offer;
      return stream_field(o, &f->stream) && str_field(o, "board", true, &f->board) &&
             str_field(o, "version", true, &f->version) && u32_field(o, "size", true, 0, UINT32_MAX, &f->size) &&
             str_field(o, "sha256", true, &f->sha256) && str_field(o, "sig", true, &f->sig) &&
             str_field(o, "key_id", true, &f->key_id);
    }
    case GP_OP_FW_COMMIT:
      return stream_field(o, &out->m.fw_commit.stream);
    default:
      return true;
  }
}

gadget_status_t gp_decode(const char *json, size_t len, gp_msg_t *out) {
  if (json == NULL || out == NULL) return GADGET_ERR_ARG;
  memset(out, 0, sizeof *out);
  cJSON *root = cJSON_ParseWithLength(json, len);
  if (root == NULL) return GADGET_ERR_PARSE;
  const cJSON *op = cJSON_GetObjectItemCaseSensitive(root, "op");
  if (!cJSON_IsObject(root) || !cJSON_IsString(op)) {
    cJSON_Delete(root);
    return GADGET_ERR_PARSE;
  }
  gp_op_t code = gp_op_from_name(op->valuestring);
  /* gadget → host ops never come from a host: ignore them like unknown ops */
  out->op = (code >= GP_OP_HELLO) ? GP_OP_UNKNOWN : code;
  out->root = root;
  if (!decode_fields(root, out)) {
    gp_msg_free(out);
    return GADGET_ERR_PARSE;
  }
  return GADGET_OK;
}

void gp_msg_free(gp_msg_t *m) {
  if (m == NULL) return;
  cJSON_Delete(m->root);
  memset(m, 0, sizeof *m);
}

/* ---- encoders ------------------------------------------------------------ */

static cJSON *begin(const char *op) {
  cJSON *o = cJSON_CreateObject();
  if (o) cJSON_AddStringToObject(o, "op", op);
  return o;
}

/* Prints o compactly into buf and frees it; returns the length or an error. */
static int finish(cJSON *o, char *buf, size_t cap) {
  if (o == NULL) return GADGET_ERR_NO_MEM;
  char *text = cJSON_PrintUnformatted(o);
  cJSON_Delete(o);
  if (text == NULL) return GADGET_ERR_NO_MEM;
  size_t n = strlen(text);
  int rc = GADGET_ERR_LIMIT;
  if (n + 1 <= cap) {
    memcpy(buf, text, n + 1);
    rc = (int)n;
  }
  cJSON_free(text);
  return rc;
}

static cJSON *caps_json(const gadget_board_t *b) {
  cJSON *caps = cJSON_CreateObject();
  cJSON *screen = cJSON_AddObjectToObject(caps, "screen");
  cJSON_AddNumberToObject(screen, "w", b->screen_w);
  cJSON_AddNumberToObject(screen, "h", b->screen_h);
  cJSON_AddBoolToObject(screen, "round", b->screen_round);
  cJSON_AddStringToObject(screen, "text", "latin1");
  cJSON *image = cJSON_AddObjectToObject(caps, "image");
  cJSON_AddNumberToObject(image, "w", b->image_w);
  cJSON_AddNumberToObject(image, "h", b->image_h);
  cJSON *mic = cJSON_AddObjectToObject(caps, "mic");
  cJSON_AddNumberToObject(mic, "rate", b->mic_rate);
  if (b->speaker_rate != 0) {
    cJSON *spk = cJSON_AddObjectToObject(caps, "speaker");
    cJSON_AddNumberToObject(spk, "rate", b->speaker_rate);
  }
  cJSON *input = cJSON_AddArrayToObject(caps, "input");
  if (b->input_mask & GADGET_INPUT_TOUCH) cJSON_AddItemToArray(input, cJSON_CreateString("touch"));
  if (b->input_mask & GADGET_INPUT_TALK) cJSON_AddItemToArray(input, cJSON_CreateString("talk"));
  if (b->input_mask & GADGET_INPUT_CANCEL) cJSON_AddItemToArray(input, cJSON_CreateString("cancel"));
  if (b->has_battery) cJSON_AddTrueToObject(caps, "battery");
  cJSON *ota = cJSON_AddObjectToObject(caps, "ota");
  cJSON_AddNumberToObject(ota, "max", b->ota_max);
  return caps;
}

int gp_encode_hello(char *buf, size_t cap, const gp_hello_t *m) {
  cJSON *o = begin("hello");
  if (o == NULL) return GADGET_ERR_NO_MEM;
  cJSON_AddNumberToObject(o, "proto", GADGET_PROTO_VERSION);
  cJSON_AddStringToObject(o, "id", m->id);
  cJSON_AddStringToObject(o, "pubkey", m->pubkey_b64);
  cJSON_AddStringToObject(o, "name", m->name);
  cJSON_AddStringToObject(o, "board", m->board->id);
  cJSON_AddStringToObject(o, "fw", m->fw);
  cJSON_AddItemToObject(o, "caps", caps_json(m->board));
  cJSON *actions = cJSON_AddArrayToObject(o, "actions");
  for (uint8_t i = 0; i < m->n_actions; i++) {
    const gp_action_decl_t *a = &m->actions[i];
    cJSON *params = a->params_json ? cJSON_Parse(a->params_json) : NULL;
    if (params == NULL) {
      if (a->params_json != NULL) {
        cJSON_Delete(o);
        return GADGET_ERR_ARG;
      }
      params = cJSON_CreateObject();
      cJSON_AddStringToObject(params, "type", "object");
      cJSON_AddObjectToObject(params, "properties");
    }
    cJSON *it = cJSON_CreateObject();
    cJSON_AddStringToObject(it, "name", a->name);
    cJSON_AddStringToObject(it, "description", a->description);
    cJSON_AddItemToObject(it, "params", params);
    cJSON_AddStringToObject(it, "risk", a->risk == GADGET_RISK_SAFE ? "safe" : "confirm");
    cJSON_AddItemToArray(actions, it);
  }
  cJSON *sensors = cJSON_AddObjectToObject(o, "sensors");
  if (m->battery_valid) {
    cJSON_AddNumberToObject(sensors, "battery_pct", m->battery_pct);
    cJSON_AddBoolToObject(sensors, "charging", m->charging);
  }
  return finish(o, buf, cap);
}

int gp_encode_prove(char *buf, size_t cap, const gp_prove_t *m) {
  cJSON *o = begin("prove");
  if (o == NULL) return GADGET_ERR_NO_MEM;
  cJSON_AddStringToObject(o, "sig", m->sig_b64);
  if (m->enroll) cJSON_AddStringToObject(o, "enroll", m->enroll);
  return finish(o, buf, cap);
}

int gp_encode_voice_begin(char *buf, size_t cap, const gp_voice_begin_t *m) {
  cJSON *o = begin("voice.begin");
  if (o == NULL) return GADGET_ERR_NO_MEM;
  cJSON_AddStringToObject(o, "turn", m->turn);
  cJSON_AddNumberToObject(o, "stream", m->stream);
  cJSON_AddNumberToObject(o, "rate", m->rate);
  return finish(o, buf, cap);
}

int gp_encode_voice_end(char *buf, size_t cap, const gp_voice_end_t *m) {
  cJSON *o = begin("voice.end");
  if (o == NULL) return GADGET_ERR_NO_MEM;
  cJSON_AddStringToObject(o, "turn", m->turn);
  cJSON_AddNumberToObject(o, "ms", m->ms);
  return finish(o, buf, cap);
}

int gp_encode_voice_drop(char *buf, size_t cap, const gp_voice_drop_t *m) {
  cJSON *o = begin("voice.drop");
  if (o == NULL) return GADGET_ERR_NO_MEM;
  cJSON_AddStringToObject(o, "turn", m->turn);
  return finish(o, buf, cap);
}

int gp_encode_say(char *buf, size_t cap, const gp_say_t *m) {
  cJSON *o = begin("say");
  if (o == NULL) return GADGET_ERR_NO_MEM;
  cJSON_AddStringToObject(o, "turn", m->turn);
  cJSON_AddStringToObject(o, "text", m->text);
  return finish(o, buf, cap);
}

int gp_encode_stop(char *buf, size_t cap, const gp_stop_t *m) {
  cJSON *o = begin("stop");
  if (o == NULL) return GADGET_ERR_NO_MEM;
  if (m->turn) cJSON_AddStringToObject(o, "turn", m->turn);
  return finish(o, buf, cap);
}

int gp_encode_answer(char *buf, size_t cap, const gp_answer_t *m) {
  cJSON *o = begin("answer");
  if (o == NULL) return GADGET_ERR_NO_MEM;
  cJSON_AddStringToObject(o, "id", m->id);
  cJSON_AddStringToObject(o, "option", m->option);
  return finish(o, buf, cap);
}

int gp_encode_act_result(char *buf, size_t cap, const gp_act_result_t *m) {
  cJSON *o = begin("act.result");
  if (o == NULL) return GADGET_ERR_NO_MEM;
  cJSON_AddStringToObject(o, "id", m->id);
  cJSON_AddBoolToObject(o, "ok", m->ok);
  if (m->data != NULL && m->data->child != NULL) {
    cJSON_AddItemToObject(o, "data", cJSON_Duplicate(m->data, true));
  }
  if (m->error) cJSON_AddStringToObject(o, "error", m->error);
  return finish(o, buf, cap);
}

int gp_encode_sense(char *buf, size_t cap, const gp_sense_t *m) {
  cJSON *o = begin("sense");
  if (o == NULL) return GADGET_ERR_NO_MEM;
  if (m->battery_valid) {
    cJSON_AddNumberToObject(o, "battery_pct", m->battery_pct);
    cJSON_AddBoolToObject(o, "charging", m->charging);
  }
  return finish(o, buf, cap);
}

int gp_encode_event(char *buf, size_t cap, const gp_event_msg_t *m) {
  cJSON *o = begin("event");
  if (o == NULL) return GADGET_ERR_NO_MEM;
  cJSON_AddStringToObject(o, "name", m->name);
  if (m->data) cJSON_AddItemToObject(o, "data", cJSON_Duplicate(m->data, true));
  return finish(o, buf, cap);
}

int gp_encode_fw_ready(char *buf, size_t cap, const gp_fw_ready_t *m) {
  cJSON *o = begin("fw.ready");
  if (o == NULL) return GADGET_ERR_NO_MEM;
  cJSON_AddNumberToObject(o, "stream", m->stream);
  return finish(o, buf, cap);
}

int gp_encode_fw_fail(char *buf, size_t cap, const gp_fw_fail_t *m) {
  cJSON *o = begin("fw.fail");
  if (o == NULL) return GADGET_ERR_NO_MEM;
  cJSON_AddNumberToObject(o, "stream", m->stream);
  cJSON_AddStringToObject(o, "code", m->code);
  return finish(o, buf, cap);
}

int gp_encode_fw_progress(char *buf, size_t cap, const gp_fw_progress_t *m) {
  cJSON *o = begin("fw.progress");
  if (o == NULL) return GADGET_ERR_NO_MEM;
  cJSON_AddNumberToObject(o, "stream", m->stream);
  cJSON_AddNumberToObject(o, "offset", m->offset);
  return finish(o, buf, cap);
}

int gp_encode_fw_installed(char *buf, size_t cap, const gp_fw_installed_t *m) {
  cJSON *o = begin("fw.installed");
  if (o == NULL) return GADGET_ERR_NO_MEM;
  cJSON_AddStringToObject(o, "version", m->version);
  return finish(o, buf, cap);
}

/* ---- binary frames -------------------------------------------------------- */

size_t gp_bin_encode(uint8_t *out, size_t cap, gp_bin_kind_t kind, uint8_t stream, const uint8_t *payload, size_t len) {
  if (out == NULL || stream == 0 || len + 2 > cap || len + 2 > GADGET_BINARY_FRAME_MAX) return 0;
  out[0] = (uint8_t)kind;
  out[1] = stream;
  if (len) memcpy(out + 2, payload, len);
  return len + 2;
}

gadget_status_t gp_bin_decode(const uint8_t *frame, size_t len, gp_bin_kind_t *kind, uint8_t *stream,
                              const uint8_t **payload, size_t *payload_len) {
  if (frame == NULL || len < 2) return GADGET_ERR_PARSE;
  if (frame[0] < GP_BIN_MIC || frame[0] > GP_BIN_FIRMWARE || frame[1] == 0) return GADGET_ERR_PARSE;
  *kind = (gp_bin_kind_t)frame[0];
  *stream = frame[1];
  *payload = frame + 2;
  *payload_len = len - 2;
  return GADGET_OK;
}

gadget_status_t gp_fw_chunk_decode(const uint8_t *payload, size_t len, uint32_t *offset, const uint8_t **data,
                                   size_t *data_len) {
  if (payload == NULL || len < 5 || len - 4 > GADGET_FW_CHUNK_MAX) return GADGET_ERR_PARSE;
  *offset = (uint32_t)payload[0] | ((uint32_t)payload[1] << 8) | ((uint32_t)payload[2] << 16) |
            ((uint32_t)payload[3] << 24);
  *data = payload + 4;
  *data_len = len - 4;
  return GADGET_OK;
}

/* ---- signed texts ---------------------------------------------------------- */

int gp_prove_text(char *out, size_t cap, const char *id, const char *nonce_b64, const char *host_id) {
  int n = snprintf(out, cap, "openmausbot-gadget/1\nprove\n%s\n%s\n%s", id, nonce_b64, host_id);
  return (n < 0 || (size_t)n >= cap) ? GADGET_ERR_LIMIT : n;
}

int gp_firmware_text(char *out, size_t cap, const char *board, const char *version, uint32_t size,
                     const char *sha256_hex) {
  int n = snprintf(out, cap, "openmausbot-gadget/1\nfirmware\n%s\n%s\n%" PRIu32 "\n%s", board, version, size,
                   sha256_hex);
  return (n < 0 || (size_t)n >= cap) ? GADGET_ERR_LIMIT : n;
}
```

Change `firmware/core/CMakeLists.txt` exactly as this diff shows (`-` lines go, `+` lines come; the rest is context):

```diff
--- a/firmware/core/CMakeLists.txt
+++ b/firmware/core/CMakeLists.txt
@@ -4,6 +4,7 @@
 # correct that branch), a static library for the desktop build otherwise.
 set(GADGET_CORE_SRCS
   src/util.c
+  src/proto.c
 )
 
 if(ESP_PLATFORM)
```

- [ ] **Step 4: Run the tests**

Run: `cmake --build build/host -j10 && ctest --test-dir build/host --output-on-failure -L "unit|vectors"`
Expected: `100% tests passed, 0 tests failed out of 2`; `test_proto` prints `10 Tests 0 Failures 0 Ignored`.

- [ ] **Step 5: Commit**

```bash
git add firmware/core/src/proto.c firmware/core/CMakeLists.txt firmware/tests/test_proto.c firmware/tests/CMakeLists.txt
git commit -m "firmware: protocol codec for openmausbot-gadget/1" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 3: PSA crypto adapter and device ids

**Files:**
- Create: `firmware/core/src/crypto_psa.c`
- Modify: `firmware/cmake/deps.cmake` (mbedTLS), `firmware/core/CMakeLists.txt`, `firmware/core/src/util.c` (`gadget_id_from_pubkey`), `firmware/tests/CMakeLists.txt`
- Test: `firmware/tests/test_crypto.c`

**Interfaces:**
- Consumes: `gadget_der_from_raw`/`gadget_der_to_raw`, `gadget_hex_*`, `gadget_b64_*` (Task 1); `gp_prove_text` (Task 2).
- Produces:
  - the HAL crypto group, implemented once for both ports: `hal_crypto_keygen`, `hal_crypto_pubkey`, `hal_crypto_sign` (SHA-256, then `psa_sign_hash` deterministic, DER out, S never normalized), `hal_crypto_verify` (accepts high-S; returns `GADGET_ERR_BAD_SIG` for any bad signature, DER or public key), `hal_crypto_sha256`, `hal_crypto_sha256_begin/update/finish/abort` (`hal_sha256_t.op` is a malloc'd `psa_hash_operation_t`) and `hal_crypto_random`;
  - `gadget_id_from_pubkey`;
  - the CMake target `gadget_mbedcrypto` (`mbedcrypto` on 3.6, `tfpsacrypto` on 4.x).

Callers must run `psa_crypto_init()` first. The tests do it in `main()`, and the ports do it before `core_init()`.

Verified on this Mac: `psa_sign_hash` with a `PSA_KEY_USAGE_SIGN_HASH` policy reproduces the RFC 6979 A.2.5 signatures, and the contract's §1.7 prove signature, on both mbedTLS 3.6.7 and 4.2.0. The research had only verified `psa_sign_message`.

- [ ] **Step 1: Write the failing test**

Create `firmware/tests/test_crypto.c`:

```c
/* firmware/tests/test_crypto.c */
/* SPDX-License-Identifier: Apache-2.0 */
/* The PSA adapter (core/src/crypto_psa.c) against RFC 6979 A.2.5 and the
 * contract's fixed values (§1.7). Runs on mbedTLS 3.6.7 and 4.2.0. */
#include <string.h>
#include "gadget_hal.h"
#include "gadget_proto.h"
#include "gadget_util.h"
#include "psa/crypto.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

static const char *RFC_PRIV = "c9afa9d845ba75166b5c215767b1d6934e50c3db36e89b127b8a622b120f6721";
static const char *RFC_PUB_B64 = "BGD+1LolWp0xyWHrdMY1bWjASbiSO2H6bOZpYi5g8p+2eQP+EAi4vJmkGunpVii8ZPLxsgwtfp9Rd6PClNRGIpk=";
static const char *SAMPLE_DER =
    "3046022100efd48b2aacb6a8fd1140dd9cd45e81d69d2c877b56aaf991c34d0ea84eaf3716"
    "022100f7cb1c942d657c41d436c7a1b6e29f65f3e900dbb9aff4064dc4ab2f843acda8";
static const char *TEST_DER =
    "3045022100f1abb023518351cd71d881567b1ea663ed3efcf6c5132b354f28d3b0b7d38367"
    "0220019f4113742a2b14bd25926b49c649155f267e60d3814b4c0cc84250e46f0083";
static const char *PROVE_DER =
    "3046022100f0c4fbe24029d797b16b36dcc0d05fb7a8b9df8c5ca4b27ad99d820d4d8a6642"
    "02210087d087e1c83ab59e8feebee63a40b423c5af81956ceca8033264098abf44e35a";

static void unhex(const char *hex, uint8_t *out, size_t cap, size_t *len) {
  TEST_ASSERT_EQUAL_INT(GADGET_OK, gadget_hex_decode(hex, out, cap, len));
}

static void rfc_key(uint8_t priv[32], uint8_t pub[65]) {
  size_t n = 0;
  unhex(RFC_PRIV, priv, 32, &n);
  TEST_ASSERT_EQUAL_INT(GADGET_OK, hal_crypto_pubkey(priv, pub));
}

static void sign_hex(const uint8_t priv[32], const char *msg, char *out_hex) {
  uint8_t der[GADGET_SIG_DER_MAX];
  size_t der_len = 0;
  TEST_ASSERT_EQUAL_INT(GADGET_OK, hal_crypto_sign(priv, (const uint8_t *)msg, strlen(msg), der, &der_len));
  gadget_hex_encode(out_hex, der, der_len);
}

static void test_pubkey_and_id_from_rfc_key(void) {
  uint8_t priv[32], pub[65];
  rfc_key(priv, pub);
  char b64[GADGET_PUBKEY_B64_LEN + 1];
  gadget_b64_encode(b64, sizeof b64, pub, sizeof pub);
  TEST_ASSERT_EQUAL_STRING(RFC_PUB_B64, b64);
  char id[GADGET_ID_LEN + 1];
  TEST_ASSERT_EQUAL_INT(GADGET_OK, gadget_id_from_pubkey(pub, id));
  TEST_ASSERT_EQUAL_STRING("gad_b18b86ce1389e46d", id);
}

static void test_rfc6979_signatures_are_reproduced_without_s_normalization(void) {
  uint8_t priv[32], pub[65];
  rfc_key(priv, pub);
  char hex[2 * GADGET_SIG_DER_MAX + 1];
  sign_hex(priv, "sample", hex);
  TEST_ASSERT_EQUAL_STRING(SAMPLE_DER, hex); /* high S kept as is */
  sign_hex(priv, "test", hex);
  TEST_ASSERT_EQUAL_STRING(TEST_DER, hex);
}

static void test_prove_signature_matches_contract(void) {
  uint8_t priv[32], pub[65];
  rfc_key(priv, pub);
  char text[256];
  gp_prove_text(text, sizeof text, "gad_b18b86ce1389e46d", "AAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8=",
                "000102030405060708090a0b0c0d0e0f");
  char hex[2 * GADGET_SIG_DER_MAX + 1];
  sign_hex(priv, text, hex);
  TEST_ASSERT_EQUAL_STRING(PROVE_DER, hex);
}

static void test_verify_accepts_high_s_and_rejects_tampering(void) {
  uint8_t priv[32], pub[65], der[80];
  size_t len = 0;
  rfc_key(priv, pub);
  unhex(SAMPLE_DER, der, sizeof der, &len);
  TEST_ASSERT_EQUAL_INT(GADGET_OK, hal_crypto_verify(pub, (const uint8_t *)"sample", 6, der, len));
  TEST_ASSERT_EQUAL_INT(GADGET_ERR_BAD_SIG, hal_crypto_verify(pub, (const uint8_t *)"samplf", 6, der, len));
  der[len - 1] ^= 1;
  TEST_ASSERT_EQUAL_INT(GADGET_ERR_BAD_SIG, hal_crypto_verify(pub, (const uint8_t *)"sample", 6, der, len));
  der[len - 1] ^= 1;
  TEST_ASSERT_EQUAL_INT(GADGET_ERR_BAD_SIG, hal_crypto_verify(pub, (const uint8_t *)"sample", 6, der, len - 2));
  uint8_t bad_pub[65];
  memcpy(bad_pub, pub, 65);
  bad_pub[64] ^= 1; /* off the curve */
  TEST_ASSERT_EQUAL_INT(GADGET_ERR_BAD_SIG, hal_crypto_verify(bad_pub, (const uint8_t *)"sample", 6, der, len));
}

static void test_keygen_round_trip(void) {
  uint8_t priv[32], pub[65], pub2[65], der[GADGET_SIG_DER_MAX];
  size_t len = 0;
  TEST_ASSERT_EQUAL_INT(GADGET_OK, hal_crypto_keygen(priv, pub));
  TEST_ASSERT_EQUAL_HEX8(0x04, pub[0]);
  TEST_ASSERT_EQUAL_INT(GADGET_OK, hal_crypto_pubkey(priv, pub2));
  TEST_ASSERT_EQUAL_MEMORY(pub, pub2, 65);
  TEST_ASSERT_EQUAL_INT(GADGET_OK, hal_crypto_sign(priv, (const uint8_t *)"hi", 2, der, &len));
  TEST_ASSERT_TRUE(len >= 8 && len <= GADGET_SIG_DER_MAX);
  TEST_ASSERT_EQUAL_INT(GADGET_OK, hal_crypto_verify(pub, (const uint8_t *)"hi", 2, der, len));
  uint8_t other[32], other_pub[65];
  TEST_ASSERT_EQUAL_INT(GADGET_OK, hal_crypto_keygen(other, other_pub));
  TEST_ASSERT_TRUE(memcmp(priv, other, 32) != 0);
  TEST_ASSERT_EQUAL_INT(GADGET_ERR_BAD_SIG, hal_crypto_verify(other_pub, (const uint8_t *)"hi", 2, der, len));
}

static void test_sha256_one_shot_and_multi_part(void) {
  uint8_t a[32], b[32];
  char hex[65];
  TEST_ASSERT_EQUAL_INT(GADGET_OK, hal_crypto_sha256("abc", 3, a));
  gadget_hex_encode(hex, a, 32);
  TEST_ASSERT_EQUAL_STRING("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", hex);
  hal_sha256_t ctx = {0};
  TEST_ASSERT_EQUAL_INT(GADGET_OK, hal_crypto_sha256_begin(&ctx));
  TEST_ASSERT_NOT_NULL(ctx.op);
  TEST_ASSERT_EQUAL_INT(GADGET_OK, hal_crypto_sha256_update(&ctx, "a", 1));
  TEST_ASSERT_EQUAL_INT(GADGET_OK, hal_crypto_sha256_update(&ctx, "bc", 2));
  TEST_ASSERT_EQUAL_INT(GADGET_OK, hal_crypto_sha256_finish(&ctx, b));
  TEST_ASSERT_NULL(ctx.op);
  TEST_ASSERT_EQUAL_MEMORY(a, b, 32);
  hal_crypto_sha256_abort(&ctx); /* NULL op: nothing to do */
  TEST_ASSERT_EQUAL_INT(GADGET_OK, hal_crypto_sha256_begin(&ctx));
  hal_crypto_sha256_abort(&ctx);
  TEST_ASSERT_NULL(ctx.op);
}

static void test_random(void) {
  uint8_t a[16] = {0}, b[16] = {0};
  TEST_ASSERT_EQUAL_INT(GADGET_OK, hal_crypto_random(a, sizeof a));
  TEST_ASSERT_EQUAL_INT(GADGET_OK, hal_crypto_random(b, sizeof b));
  TEST_ASSERT_TRUE(memcmp(a, b, sizeof a) != 0);
}

int main(void) {
  TEST_ASSERT_EQUAL_INT(PSA_SUCCESS, psa_crypto_init());
  UNITY_BEGIN();
  RUN_TEST(test_pubkey_and_id_from_rfc_key);
  RUN_TEST(test_rfc6979_signatures_are_reproduced_without_s_normalization);
  RUN_TEST(test_prove_signature_matches_contract);
  RUN_TEST(test_verify_accepts_high_s_and_rejects_tampering);
  RUN_TEST(test_keygen_round_trip);
  RUN_TEST(test_sha256_one_shot_and_multi_part);
  RUN_TEST(test_random);
  return UNITY_END();
}
```

Change `firmware/tests/CMakeLists.txt` exactly as this diff shows (`-` lines go, `+` lines come; the rest is context):

```diff
--- a/firmware/tests/CMakeLists.txt
+++ b/firmware/tests/CMakeLists.txt
@@ -13,3 +13,4 @@ endfunction()
 
 gadget_pure_test(util)
 gadget_pure_test(proto)
+gadget_pure_test(crypto)
```

- [ ] **Step 2: Run it to see it fail**

Run: `cmake --build build/host -j10`
Expected: compiling `test_crypto.c` fails with `'psa/crypto.h' file not found`, because mbedTLS is not wired in yet.

- [ ] **Step 3: Write the implementation**

Change `firmware/cmake/deps.cmake` exactly as this diff shows (`-` lines go, `+` lines come; the rest is context):

```diff
--- a/firmware/cmake/deps.cmake
+++ b/firmware/cmake/deps.cmake
@@ -18,7 +18,34 @@ FetchContent_Declare(unity
   URL_HASH SHA256=e84eb301ca7967831e68b1728f911e87fa2d345d8ddb64f897bc2f2ee24a321c
   DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
 
-FetchContent_MakeAvailable(cjson unity)
+# mbedTLS (Apache-2.0), PSA Crypto only. 3.6.7 matches ESP-IDF 5.5's 3.6.x;
+# the 4.2.0 CI leg matches ESP-IDF 6.0's 4.x. Target mbedcrypto (3.6) or
+# tfpsacrypto (4.x), wrapped as gadget_mbedcrypto.
+if(GADGET_MBEDTLS_VERSION STREQUAL "3.6.7")
+  set(_gadget_mbedtls_sha a7e8bcbec0e6f761b4af24f25677626b35f762f68eef79c08677a363212d11f6)
+elseif(GADGET_MBEDTLS_VERSION STREQUAL "4.2.0")
+  set(_gadget_mbedtls_sha 2bed9d713b4668f76553b097e72b8aa30bc8f112a940d7ae228d524bbde6ffea)
+else()
+  message(FATAL_ERROR "GADGET_MBEDTLS_VERSION must be 3.6.7 or 4.2.0, not ${GADGET_MBEDTLS_VERSION}")
+endif()
+set(ENABLE_PROGRAMS OFF CACHE BOOL "" FORCE)
+set(ENABLE_TESTING OFF CACHE BOOL "" FORCE)
+set(GEN_FILES OFF CACHE BOOL "" FORCE)
+set(MBEDTLS_FATAL_WARNINGS OFF CACHE BOOL "" FORCE)
+set(TF_PSA_CRYPTO_FATAL_WARNINGS OFF CACHE BOOL "" FORCE)
+FetchContent_Declare(mbedtls
+  URL https://github.com/Mbed-TLS/mbedtls/releases/download/mbedtls-${GADGET_MBEDTLS_VERSION}/mbedtls-${GADGET_MBEDTLS_VERSION}.tar.bz2
+  URL_HASH SHA256=${_gadget_mbedtls_sha}
+  DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
+
+FetchContent_MakeAvailable(cjson unity mbedtls)
+
+add_library(gadget_mbedcrypto INTERFACE)
+if(TARGET tfpsacrypto)
+  target_link_libraries(gadget_mbedcrypto INTERFACE tfpsacrypto)
+else()
+  target_link_libraries(gadget_mbedcrypto INTERFACE mbedcrypto)
+endif()
 
 add_library(cjson STATIC ${cjson_SOURCE_DIR}/cJSON.c)
 target_include_directories(cjson PUBLIC ${cjson_SOURCE_DIR})
```

Change `firmware/core/CMakeLists.txt` exactly as this diff shows (`-` lines go, `+` lines come; the rest is context):

```diff
--- a/firmware/core/CMakeLists.txt
+++ b/firmware/core/CMakeLists.txt
@@ -5,6 +5,7 @@
 set(GADGET_CORE_SRCS
   src/util.c
   src/proto.c
+  src/crypto_psa.c
 )
 
 if(ESP_PLATFORM)
@@ -16,5 +17,5 @@ endif()
 
 add_library(gadget_core STATIC ${GADGET_CORE_SRCS})
 target_include_directories(gadget_core PUBLIC include)
-target_link_libraries(gadget_core PUBLIC cjson)
+target_link_libraries(gadget_core PUBLIC cjson gadget_mbedcrypto)
 gadget_warnings(gadget_core)
```

Create `firmware/core/src/crypto_psa.c`:

```c
/* firmware/core/src/crypto_psa.c */
/* SPDX-License-Identifier: Apache-2.0 */
/* The HAL crypto group for both ports, on the PSA Crypto API only (spec
 * §5.2). Builds unchanged on mbedTLS 3.6.x (ESP-IDF 5.5, the simulator) and
 * 4.x (ESP-IDF 6.0). Volatile keys only: the 32-byte scalar lives in
 * storage key dev_key. DER <-> raw conversion is ours (gadget_util.h).
 * Ports call psa_crypto_init() once before core_init(). */
#include <stdlib.h>
#include <string.h>
#include "gadget_hal.h"
#include "gadget_util.h"
#include "psa/crypto.h"

#define ALG_SIGN PSA_ALG_DETERMINISTIC_ECDSA(PSA_ALG_SHA_256)
#define ALG_VERIFY PSA_ALG_ECDSA(PSA_ALG_SHA_256)

static void pair_attributes(psa_key_attributes_t *a) {
  *a = psa_key_attributes_init();
  psa_set_key_type(a, PSA_KEY_TYPE_ECC_KEY_PAIR(PSA_ECC_FAMILY_SECP_R1));
  psa_set_key_bits(a, 256);
  psa_set_key_usage_flags(a, PSA_KEY_USAGE_SIGN_HASH | PSA_KEY_USAGE_EXPORT);
  psa_set_key_algorithm(a, ALG_SIGN);
}

static gadget_status_t import_pair(const uint8_t priv[GADGET_PRIVKEY_LEN], psa_key_id_t *id) {
  psa_key_attributes_t a;
  pair_attributes(&a);
  return psa_import_key(&a, priv, GADGET_PRIVKEY_LEN, id) == PSA_SUCCESS ? GADGET_OK : GADGET_ERR_CRYPTO;
}

static gadget_status_t export_public(psa_key_id_t id, uint8_t pub[GADGET_PUBKEY_LEN]) {
  size_t n = 0;
  if (psa_export_public_key(id, pub, GADGET_PUBKEY_LEN, &n) != PSA_SUCCESS || n != GADGET_PUBKEY_LEN) {
    return GADGET_ERR_CRYPTO;
  }
  return GADGET_OK;
}

gadget_status_t hal_crypto_keygen(uint8_t priv[GADGET_PRIVKEY_LEN], uint8_t pub[GADGET_PUBKEY_LEN]) {
  psa_key_attributes_t a;
  psa_key_id_t id = 0;
  pair_attributes(&a);
  if (psa_generate_key(&a, &id) != PSA_SUCCESS) return GADGET_ERR_CRYPTO;
  size_t n = 0;
  gadget_status_t st = GADGET_ERR_CRYPTO;
  if (psa_export_key(id, priv, GADGET_PRIVKEY_LEN, &n) == PSA_SUCCESS && n == GADGET_PRIVKEY_LEN) {
    st = export_public(id, pub);
  }
  psa_destroy_key(id);
  return st;
}

gadget_status_t hal_crypto_pubkey(const uint8_t priv[GADGET_PRIVKEY_LEN], uint8_t pub[GADGET_PUBKEY_LEN]) {
  psa_key_id_t id = 0;
  gadget_status_t st = import_pair(priv, &id);
  if (st != GADGET_OK) return st;
  st = export_public(id, pub);
  psa_destroy_key(id);
  return st;
}

gadget_status_t hal_crypto_sign(const uint8_t priv[GADGET_PRIVKEY_LEN], const uint8_t *msg, size_t len,
                                uint8_t der[GADGET_SIG_DER_MAX], size_t *der_len) {
  uint8_t hash[GADGET_SHA256_LEN];
  gadget_status_t st = hal_crypto_sha256(msg, len, hash);
  if (st != GADGET_OK) return st;
  psa_key_id_t id = 0;
  st = import_pair(priv, &id);
  if (st != GADGET_OK) return st;
  uint8_t raw[64];
  size_t raw_len = 0;
  psa_status_t ps = psa_sign_hash(id, ALG_SIGN, hash, sizeof hash, raw, sizeof raw, &raw_len);
  psa_destroy_key(id);
  if (ps != PSA_SUCCESS || raw_len != sizeof raw) return GADGET_ERR_CRYPTO;
  return gadget_der_from_raw(raw, der, GADGET_SIG_DER_MAX, der_len);
}

gadget_status_t hal_crypto_verify(const uint8_t pub[GADGET_PUBKEY_LEN], const uint8_t *msg, size_t len,
                                  const uint8_t *der, size_t der_len) {
  uint8_t raw[64];
  if (gadget_der_to_raw(der, der_len, raw) != GADGET_OK) return GADGET_ERR_BAD_SIG;
  uint8_t hash[GADGET_SHA256_LEN];
  if (hal_crypto_sha256(msg, len, hash) != GADGET_OK) return GADGET_ERR_CRYPTO;
  psa_key_attributes_t a = psa_key_attributes_init();
  psa_set_key_type(&a, PSA_KEY_TYPE_ECC_PUBLIC_KEY(PSA_ECC_FAMILY_SECP_R1));
  psa_set_key_bits(&a, 256);
  psa_set_key_usage_flags(&a, PSA_KEY_USAGE_VERIFY_HASH);
  psa_set_key_algorithm(&a, ALG_VERIFY);
  psa_key_id_t id = 0;
  if (psa_import_key(&a, pub, GADGET_PUBKEY_LEN, &id) != PSA_SUCCESS) return GADGET_ERR_BAD_SIG;
  psa_status_t ps = psa_verify_hash(id, ALG_VERIFY, hash, sizeof hash, raw, sizeof raw);
  psa_destroy_key(id);
  return ps == PSA_SUCCESS ? GADGET_OK : GADGET_ERR_BAD_SIG;
}

gadget_status_t hal_crypto_sha256(const void *data, size_t len, uint8_t out[GADGET_SHA256_LEN]) {
  size_t n = 0;
  psa_status_t ps = psa_hash_compute(PSA_ALG_SHA_256, (const uint8_t *)data, len, out, GADGET_SHA256_LEN, &n);
  return (ps == PSA_SUCCESS && n == GADGET_SHA256_LEN) ? GADGET_OK : GADGET_ERR_CRYPTO;
}

gadget_status_t hal_crypto_sha256_begin(hal_sha256_t *ctx) {
  psa_hash_operation_t *op = malloc(sizeof *op);
  if (op == NULL) return GADGET_ERR_NO_MEM;
  *op = psa_hash_operation_init();
  if (psa_hash_setup(op, PSA_ALG_SHA_256) != PSA_SUCCESS) {
    free(op);
    ctx->op = NULL;
    return GADGET_ERR_CRYPTO;
  }
  ctx->op = op;
  return GADGET_OK;
}

gadget_status_t hal_crypto_sha256_update(hal_sha256_t *ctx, const void *data, size_t len) {
  if (ctx->op == NULL) return GADGET_ERR_STATE;
  return psa_hash_update(ctx->op, (const uint8_t *)data, len) == PSA_SUCCESS ? GADGET_OK : GADGET_ERR_CRYPTO;
}

gadget_status_t hal_crypto_sha256_finish(hal_sha256_t *ctx, uint8_t out[GADGET_SHA256_LEN]) {
  if (ctx->op == NULL) return GADGET_ERR_STATE;
  size_t n = 0;
  psa_status_t ps = psa_hash_finish(ctx->op, out, GADGET_SHA256_LEN, &n);
  if (ps != PSA_SUCCESS) psa_hash_abort(ctx->op);
  free(ctx->op);
  ctx->op = NULL;
  return (ps == PSA_SUCCESS && n == GADGET_SHA256_LEN) ? GADGET_OK : GADGET_ERR_CRYPTO;
}

void hal_crypto_sha256_abort(hal_sha256_t *ctx) {
  if (ctx == NULL || ctx->op == NULL) return;
  psa_hash_abort(ctx->op);
  free(ctx->op);
  ctx->op = NULL;
}

gadget_status_t hal_crypto_random(void *buf, size_t len) {
  return psa_generate_random((uint8_t *)buf, len) == PSA_SUCCESS ? GADGET_OK : GADGET_ERR_CRYPTO;
}
```

Change `firmware/core/src/util.c` exactly as this diff shows (`-` lines go, `+` lines come; the rest is context):

```diff
--- a/firmware/core/src/util.c
+++ b/firmware/core/src/util.c
@@ -2,6 +2,7 @@
 /* SPDX-License-Identifier: Apache-2.0 */
 /* Pure helpers (gadget_util.h): base64, hex, DER, validators, UTF-8, PRNG. */
 #include <string.h>
+#include "gadget_hal.h"
 #include "gadget_util.h"
 
 /* ---- base64 (RFC 4648 §4, standard alphabet, padded) ------------------- */
@@ -162,6 +163,18 @@ gadget_status_t gadget_der_to_raw(const uint8_t *der, size_t der_len, uint8_t ra
   return off == der_len ? GADGET_OK : GADGET_ERR_PARSE;
 }
 
+/* ---- identity ----------------------------------------------------------- */
+
+gadget_status_t gadget_id_from_pubkey(const uint8_t pub[GADGET_PUBKEY_LEN], char out[GADGET_ID_LEN + 1]) {
+  if (pub == NULL || out == NULL) return GADGET_ERR_ARG;
+  uint8_t hash[GADGET_SHA256_LEN];
+  gadget_status_t st = hal_crypto_sha256(pub, GADGET_PUBKEY_LEN, hash);
+  if (st != GADGET_OK) return st;
+  memcpy(out, "gad_", 4);
+  gadget_hex_encode(out + 4, hash, 8); /* 16 hex characters */
+  return GADGET_OK;
+}
+
 /* ---- validators --------------------------------------------------------- */
 
 bool gadget_host_id_valid(const char *host_id) {
```

- [ ] **Step 4: Run the tests on mbedTLS 3.6.7**

Run: `cmake -S firmware -B build/host && cmake --build build/host -j10 && ctest --test-dir build/host --output-on-failure -L "unit|vectors"`
Expected: `100% tests passed, 0 tests failed out of 3`; `test_crypto` prints `7 Tests 0 Failures 0 Ignored`. mbedTLS's own CMake prints a harmless `CMake Deprecation Warning … cmake_minimum_required`.

- [ ] **Step 5: Run the tests on mbedTLS 4.2.0**

Run: `cmake -S firmware -B build/host-mbedtls4 -DCMAKE_BUILD_TYPE=Debug -DGADGET_WITH_LVGL=OFF -DGADGET_MBEDTLS_VERSION=4.2.0 && cmake --build build/host-mbedtls4 -j10 && ctest --test-dir build/host-mbedtls4 --output-on-failure -L "unit|vectors"`
Expected: `100% tests passed, 0 tests failed out of 3`.

- [ ] **Step 6: Commit**

```bash
git add firmware/cmake/deps.cmake firmware/core firmware/tests/test_crypto.c firmware/tests/CMakeLists.txt
git commit -m "firmware: PSA-only crypto adapter, device ids from public keys" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 4: Protocol vectors in C

**Files:**
- Modify: `firmware/tests/CMakeLists.txt`
- Test: `firmware/tests/test_vectors.c` (CTest `vectors.c`, label `vectors`)

**Interfaces:**
- Consumes: P1's `protocol/vectors/*.json` and `SHA256SUMS` in the shape of contract §4.4. It reads `identity`, `rfc6979`, `prove`, `der`, `base64`, `firmware` and `frames`; `versions.json` is host-side only. It also consumes Tasks 1–3.
- Produces: the firmware half of spec §4.9. The same files that OpenMausBot's TypeScript tests read must pass with core's own base64, DER, id, prove text, firmware text, frames and PSA signatures:
  - `deterministic` cases must be reproduced byte for byte;
  - `high_s` must match the signature's S;
  - every `expect` value must be one of `accept`, `reject_sig`, `reject_id`, `reject_base64`, `reject_pubkey`, `reject_host_id` and `bad_sig`; an unknown value fails the test.
  - `SHA256SUMS` is checked over the exact file bytes.

This task adds no production code. Its failing step proves the test bites before it is trusted.

- [ ] **Step 1: Write the test**

Create `firmware/tests/test_vectors.c`:

```c
/* firmware/tests/test_vectors.c */
/* SPDX-License-Identifier: Apache-2.0 */
/* The firmware side of protocol/vectors (contract §4.4): every case in the
 * files the gadget needs must pass with core's own code. argv[1] is the
 * vectors directory. versions.json is host-side only and is not read here. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cJSON.h"
#include "gadget_hal.h"
#include "gadget_proto.h"
#include "gadget_util.h"
#include "psa/crypto.h"
#include "unity.h"

static const char *g_dir = NULL;

void setUp(void) {}
void tearDown(void) {}

static char *read_file(const char *name, size_t *len_out) {
  char path[1024];
  snprintf(path, sizeof path, "%s/%s", g_dir, name);
  FILE *f = fopen(path, "rb");
  if (f == NULL) return NULL;
  fseek(f, 0, SEEK_END);
  long n = ftell(f);
  fseek(f, 0, SEEK_SET);
  char *buf = malloc((size_t)n + 1);
  size_t got = fread(buf, 1, (size_t)n, f);
  fclose(f);
  buf[got] = '\0';
  if (len_out) *len_out = got;
  return buf;
}

/* Loads <stem>.json and returns its "cases" array (the caller frees *root). */
static const cJSON *load_cases(const char *stem, cJSON **root) {
  char name[64];
  snprintf(name, sizeof name, "%s.json", stem);
  char *text = read_file(name, NULL);
  TEST_ASSERT_NOT_NULL_MESSAGE(text, name);
  *root = cJSON_Parse(text);
  free(text);
  TEST_ASSERT_NOT_NULL_MESSAGE(*root, name);
  const cJSON *cases = cJSON_GetObjectItemCaseSensitive(*root, "cases");
  TEST_ASSERT_TRUE_MESSAGE(cJSON_IsArray(cases) && cJSON_GetArraySize(cases) > 0, name);
  return cases;
}

static const char *s(const cJSON *c, const char *key) {
  const cJSON *v = cJSON_GetObjectItemCaseSensitive(c, key);
  TEST_ASSERT_TRUE_MESSAGE(cJSON_IsString(v), key);
  return v->valuestring;
}

static bool b(const cJSON *c, const char *key) {
  const cJSON *v = cJSON_GetObjectItemCaseSensitive(c, key);
  TEST_ASSERT_TRUE_MESSAGE(cJSON_IsBool(v), key);
  return cJSON_IsTrue(v);
}

static uint32_t u(const cJSON *c, const char *key) {
  const cJSON *v = cJSON_GetObjectItemCaseSensitive(c, key);
  TEST_ASSERT_TRUE_MESSAGE(cJSON_IsNumber(v), key);
  return (uint32_t)v->valuedouble;
}

static size_t unhex(const char *hex, uint8_t *out, size_t cap) {
  size_t n = 0;
  TEST_ASSERT_EQUAL_INT_MESSAGE(GADGET_OK, gadget_hex_decode(hex, out, cap, &n), hex);
  return n;
}

/* s > n/2 for P-256, on the raw big-endian s. */
static bool is_high_s(const uint8_t raw[64]) {
  static const uint8_t HALF_N[32] = {0x7f, 0xff, 0xff, 0xff, 0x80, 0x00, 0x00, 0x00, 0x7f, 0xff, 0xff,
                                     0xff, 0xff, 0xff, 0xff, 0xff, 0xde, 0x73, 0x7d, 0x56, 0xd3, 0x8b,
                                     0xcf, 0x42, 0x79, 0xdc, 0xe5, 0x61, 0x7e, 0x31, 0x92, 0xa8};
  return memcmp(raw + 32, HALF_N, 32) > 0;
}

static void sign_and_compare(const char *priv_hex, const char *text, const char *want_der_hex) {
  uint8_t priv[32], der[GADGET_SIG_DER_MAX];
  size_t der_len = 0;
  unhex(priv_hex, priv, sizeof priv);
  TEST_ASSERT_EQUAL_INT(GADGET_OK, hal_crypto_sign(priv, (const uint8_t *)text, strlen(text), der, &der_len));
  char hex[2 * GADGET_SIG_DER_MAX + 1];
  gadget_hex_encode(hex, der, der_len);
  TEST_ASSERT_EQUAL_STRING(want_der_hex, hex);
}

static void test_sha256sums_cover_the_exact_bytes(void) {
  char *sums = read_file("SHA256SUMS", NULL);
  TEST_ASSERT_NOT_NULL(sums);
  int lines = 0;
  for (char *line = strtok(sums, "\n"); line != NULL; line = strtok(NULL, "\n")) {
    char want[65], name[256];
    TEST_ASSERT_EQUAL_INT_MESSAGE(2, sscanf(line, "%64s  %255s", want, name), line);
    size_t len = 0;
    char *bytes = read_file(name, &len);
    TEST_ASSERT_NOT_NULL_MESSAGE(bytes, name);
    uint8_t h[32];
    char got[65];
    hal_crypto_sha256(bytes, len, h);
    gadget_hex_encode(got, h, 32);
    TEST_ASSERT_EQUAL_STRING_MESSAGE(want, got, name);
    free(bytes);
    lines++;
  }
  free(sums);
  TEST_ASSERT_GREATER_OR_EQUAL_INT(7, lines);
}

static void test_identity(void) {
  cJSON *root;
  const cJSON *c;
  cJSON_ArrayForEach(c, load_cases("identity", &root)) {
    uint8_t priv[32], pub[65];
    unhex(s(c, "private_key_hex"), priv, sizeof priv);
    TEST_ASSERT_EQUAL_INT(GADGET_OK, hal_crypto_pubkey(priv, pub));
    char hex[131], b64[GADGET_PUBKEY_B64_LEN + 1], id[GADGET_ID_LEN + 1], sha[65];
    gadget_hex_encode(hex, pub, 65);
    TEST_ASSERT_EQUAL_STRING_MESSAGE(s(c, "pubkey_hex"), hex, s(c, "name"));
    gadget_b64_encode(b64, sizeof b64, pub, 65);
    TEST_ASSERT_EQUAL_STRING(s(c, "pubkey_b64"), b64);
    uint8_t h[32];
    hal_crypto_sha256(pub, 65, h);
    gadget_hex_encode(sha, h, 32);
    TEST_ASSERT_EQUAL_STRING(s(c, "pubkey_sha256_hex"), sha);
    TEST_ASSERT_EQUAL_INT(GADGET_OK, gadget_id_from_pubkey(pub, id));
    TEST_ASSERT_EQUAL_STRING(s(c, "id"), id);
  }
  cJSON_Delete(root);
}

static void test_rfc6979(void) {
  cJSON *root;
  const cJSON *c;
  cJSON_ArrayForEach(c, load_cases("rfc6979", &root)) {
    sign_and_compare(s(c, "private_key_hex"), s(c, "message_utf8"), s(c, "der_hex"));
    uint8_t der[80], raw[64], want_raw[64];
    size_t n = unhex(s(c, "der_hex"), der, sizeof der);
    TEST_ASSERT_EQUAL_INT(GADGET_OK, gadget_der_to_raw(der, n, raw));
    unhex(s(c, "raw_hex"), want_raw, sizeof want_raw);
    TEST_ASSERT_EQUAL_MEMORY(want_raw, raw, 64);
    TEST_ASSERT_EQUAL_INT_MESSAGE(b(c, "high_s"), is_high_s(raw), s(c, "name"));
    uint8_t priv[32], pub[65];
    unhex(s(c, "private_key_hex"), priv, sizeof priv);
    hal_crypto_pubkey(priv, pub);
    const char *msg = s(c, "message_utf8");
    TEST_ASSERT_EQUAL_INT(GADGET_OK, hal_crypto_verify(pub, (const uint8_t *)msg, strlen(msg), der, n));
  }
  cJSON_Delete(root);
}

static void test_prove(void) {
  cJSON *root;
  const cJSON *c;
  cJSON_ArrayForEach(c, load_cases("prove", &root)) {
    const char *name = s(c, "name"), *expect = s(c, "expect");
    char text[512];
    gp_prove_text(text, sizeof text, s(c, "id"), s(c, "nonce_b64"), s(c, "host_id"));
    TEST_ASSERT_EQUAL_STRING_MESSAGE(s(c, "text"), text, name);
    uint8_t pub[80], der[80];
    size_t pub_len = 0, der_len = 0;
    gadget_status_t b64st = gadget_b64_decode(s(c, "pubkey_b64"), pub, sizeof pub, &pub_len);
    if (strcmp(expect, "reject_base64") == 0) {
      TEST_ASSERT_EQUAL_INT_MESSAGE(GADGET_ERR_PARSE, b64st, name);
      continue;
    }
    TEST_ASSERT_EQUAL_INT_MESSAGE(GADGET_OK, b64st, name);
    if (strcmp(expect, "reject_pubkey") == 0) {
      TEST_ASSERT_TRUE_MESSAGE(pub_len != GADGET_PUBKEY_LEN || pub[0] != 0x04, name);
      continue;
    }
    TEST_ASSERT_EQUAL_size_t(GADGET_PUBKEY_LEN, pub_len);
    char id[GADGET_ID_LEN + 1];
    gadget_id_from_pubkey(pub, id);
    if (strcmp(expect, "reject_id") == 0) {
      TEST_ASSERT_TRUE_MESSAGE(strcmp(id, s(c, "id")) != 0, name);
      continue;
    }
    TEST_ASSERT_EQUAL_STRING_MESSAGE(s(c, "id"), id, name);
    if (strcmp(expect, "reject_host_id") == 0) {
      TEST_ASSERT_FALSE_MESSAGE(gadget_host_id_valid(s(c, "host_id")), name);
      continue;
    }
    TEST_ASSERT_TRUE_MESSAGE(gadget_host_id_valid(s(c, "host_id")), name);
    TEST_ASSERT_EQUAL_INT(GADGET_OK, gadget_b64_decode(s(c, "sig_b64"), der, sizeof der, &der_len));
    uint8_t der2[80];
    TEST_ASSERT_EQUAL_size_t(der_len, unhex(s(c, "sig_der_hex"), der2, sizeof der2));
    TEST_ASSERT_EQUAL_MEMORY(der2, der, der_len);
    gadget_status_t v = hal_crypto_verify(pub, (const uint8_t *)text, strlen(text), der, der_len);
    if (strcmp(expect, "reject_sig") == 0) {
      TEST_ASSERT_EQUAL_INT_MESSAGE(GADGET_ERR_BAD_SIG, v, name);
      continue;
    }
    TEST_ASSERT_EQUAL_STRING_MESSAGE("accept", expect, name); /* unknown expect values fail here */
    TEST_ASSERT_EQUAL_INT_MESSAGE(GADGET_OK, v, name);
    uint8_t raw[64];
    TEST_ASSERT_EQUAL_INT(GADGET_OK, gadget_der_to_raw(der, der_len, raw));
    TEST_ASSERT_EQUAL_INT_MESSAGE(b(c, "high_s"), is_high_s(raw), name);
    if (b(c, "deterministic")) sign_and_compare(s(c, "private_key_hex"), text, s(c, "sig_der_hex"));
  }
  cJSON_Delete(root);
}

static void test_der(void) {
  cJSON *root;
  const cJSON *c;
  cJSON_ArrayForEach(c, load_cases("der", &root)) {
    uint8_t der[96], raw[64];
    size_t n = unhex(s(c, "der_hex"), der, sizeof der);
    if (!b(c, "valid")) {
      TEST_ASSERT_EQUAL_INT_MESSAGE(GADGET_ERR_PARSE, gadget_der_to_raw(der, n, raw), s(c, "name"));
      continue;
    }
    TEST_ASSERT_EQUAL_size_t(u(c, "der_len"), n);
    TEST_ASSERT_EQUAL_INT_MESSAGE(GADGET_OK, gadget_der_to_raw(der, n, raw), s(c, "name"));
    char hex[129];
    gadget_hex_encode(hex, raw, 64);
    TEST_ASSERT_EQUAL_STRING(s(c, "raw_hex"), hex);
    uint8_t again[GADGET_SIG_DER_MAX];
    size_t again_len = 0;
    TEST_ASSERT_EQUAL_INT(GADGET_OK, gadget_der_from_raw(raw, again, sizeof again, &again_len));
    TEST_ASSERT_EQUAL_size_t(n, again_len);
    TEST_ASSERT_EQUAL_MEMORY(der, again, n);
    const cJSON *msg = cJSON_GetObjectItemCaseSensitive(c, "message_utf8");
    if (cJSON_IsString(msg)) sign_and_compare(s(c, "private_key_hex"), msg->valuestring, s(c, "der_hex"));
  }
  cJSON_Delete(root);
}

static void test_base64(void) {
  cJSON *root;
  const cJSON *c;
  cJSON_ArrayForEach(c, load_cases("base64", &root)) {
    uint8_t buf[128];
    size_t n = 0;
    gadget_status_t st = gadget_b64_decode(s(c, "input"), buf, sizeof buf, &n);
    if (!b(c, "canonical")) {
      TEST_ASSERT_EQUAL_INT_MESSAGE(GADGET_ERR_PARSE, st, s(c, "name"));
      continue;
    }
    TEST_ASSERT_EQUAL_INT_MESSAGE(GADGET_OK, st, s(c, "name"));
    char hex[257], again[256];
    gadget_hex_encode(hex, buf, n);
    TEST_ASSERT_EQUAL_STRING(s(c, "bytes_hex"), hex);
    gadget_b64_encode(again, sizeof again, buf, n);
    TEST_ASSERT_EQUAL_STRING(s(c, "input"), again);
  }
  cJSON_Delete(root);
}

static void test_firmware(void) {
  cJSON *root;
  const cJSON *c;
  cJSON_ArrayForEach(c, load_cases("firmware", &root)) {
    const char *name = s(c, "name"), *expect = s(c, "expect");
    char text[512];
    gp_firmware_text(text, sizeof text, s(c, "gadget_board"), s(c, "version"), u(c, "size"), s(c, "sha256"));
    TEST_ASSERT_EQUAL_STRING_MESSAGE(s(c, "text"), text, name);
    uint8_t pub[80], der[80];
    size_t pub_len = 0, der_len = 0;
    TEST_ASSERT_EQUAL_INT(GADGET_OK, gadget_b64_decode(s(c, "pubkey_b64"), pub, sizeof pub, &pub_len));
    TEST_ASSERT_EQUAL_size_t(GADGET_PUBKEY_LEN, pub_len);
    TEST_ASSERT_EQUAL_INT(GADGET_OK, gadget_b64_decode(s(c, "sig_b64"), der, sizeof der, &der_len));
    gadget_status_t v = hal_crypto_verify(pub, (const uint8_t *)text, strlen(text), der, der_len);
    if (strcmp(expect, "bad_sig") == 0) {
      TEST_ASSERT_EQUAL_INT_MESSAGE(GADGET_ERR_BAD_SIG, v, name);
      continue;
    }
    TEST_ASSERT_EQUAL_STRING_MESSAGE("accept", expect, name);
    TEST_ASSERT_EQUAL_INT_MESSAGE(GADGET_OK, v, name);
    if (b(c, "deterministic")) sign_and_compare(s(c, "private_key_hex"), text, s(c, "sig_der_hex"));
  }
  cJSON_Delete(root);
}

static void test_frames(void) {
  cJSON *root;
  const cJSON *c;
  cJSON_ArrayForEach(c, load_cases("frames", &root)) {
    const char *name = s(c, "name");
    static uint8_t frame[GADGET_BINARY_FRAME_MAX + 16];
    size_t n = unhex(s(c, "frame_hex"), frame, sizeof frame);
    gp_bin_kind_t kind;
    uint8_t stream;
    const uint8_t *payload;
    size_t plen;
    gadget_status_t st = gp_bin_decode(frame, n, &kind, &stream, &payload, &plen);
    if (!b(c, "valid")) {
      bool chunk_bad = st == GADGET_OK && kind == GP_BIN_FIRMWARE;
      if (chunk_bad) {
        uint32_t off;
        const uint8_t *data;
        size_t dlen;
        st = gp_fw_chunk_decode(payload, plen, &off, &data, &dlen);
      }
      TEST_ASSERT_EQUAL_INT_MESSAGE(GADGET_ERR_PARSE, st, name);
      continue;
    }
    TEST_ASSERT_EQUAL_INT_MESSAGE(GADGET_OK, st, name);
    TEST_ASSERT_EQUAL_UINT32(u(c, "kind"), kind);
    TEST_ASSERT_EQUAL_UINT32(u(c, "stream"), stream);
    static uint8_t want[GADGET_BINARY_FRAME_MAX];
    size_t want_len = unhex(s(c, "payload_hex"), want, sizeof want);
    TEST_ASSERT_EQUAL_size_t(want_len, plen);
    TEST_ASSERT_EQUAL_MEMORY(want, payload, plen);
    static uint8_t again[GADGET_BINARY_FRAME_MAX];
    TEST_ASSERT_EQUAL_size_t(n, gp_bin_encode(again, sizeof again, kind, stream, payload, plen));
    TEST_ASSERT_EQUAL_MEMORY(frame, again, n);
    if (kind == GP_BIN_FIRMWARE) {
      uint32_t off;
      const uint8_t *data;
      size_t dlen;
      TEST_ASSERT_EQUAL_INT(GADGET_OK, gp_fw_chunk_decode(payload, plen, &off, &data, &dlen));
      TEST_ASSERT_EQUAL_UINT32(u(c, "offset"), off);
      size_t dwant = unhex(s(c, "data_hex"), want, sizeof want);
      TEST_ASSERT_EQUAL_size_t(dwant, dlen);
      TEST_ASSERT_EQUAL_MEMORY(want, data, dlen);
    }
  }
  cJSON_Delete(root);
}

int main(int argc, char **argv) {
  if (argc < 2) {
    fprintf(stderr, "usage: test_vectors <protocol/vectors dir>\n");
    return 2;
  }
  g_dir = argv[1];
  if (psa_crypto_init() != PSA_SUCCESS) return 3;
  UNITY_BEGIN();
  RUN_TEST(test_sha256sums_cover_the_exact_bytes);
  RUN_TEST(test_identity);
  RUN_TEST(test_rfc6979);
  RUN_TEST(test_prove);
  RUN_TEST(test_der);
  RUN_TEST(test_base64);
  RUN_TEST(test_firmware);
  RUN_TEST(test_frames);
  return UNITY_END();
}
```

Change `firmware/tests/CMakeLists.txt` exactly as this diff shows (`-` lines go, `+` lines come; the rest is context):

```diff
--- a/firmware/tests/CMakeLists.txt
+++ b/firmware/tests/CMakeLists.txt
@@ -14,3 +14,10 @@ endfunction()
 gadget_pure_test(util)
 gadget_pure_test(proto)
 gadget_pure_test(crypto)
+
+# protocol/vectors in C (label vectors).
+add_executable(test_vectors test_vectors.c)
+target_link_libraries(test_vectors PRIVATE gadget_core unity::framework)
+gadget_warnings(test_vectors)
+add_test(NAME vectors.c COMMAND test_vectors ${GADGET_VECTORS_DIR})
+set_tests_properties(vectors.c PROPERTIES LABELS vectors TIMEOUT 60)
```

- [ ] **Step 2: See it fail on corrupted vectors**

```bash
cmake --build build/host -j10
rm -rf /tmp/p2a-mut && cp -r protocol/vectors /tmp/p2a-mut
sed -i.bak 's/"gad_b18b86ce1389e46d"/"gad_b18b86ce1389e46e"/' /tmp/p2a-mut/identity.json
./build/host/tests/test_vectors /tmp/p2a-mut | grep FAIL
```

Expected: two failures, `test_sha256sums_cover_the_exact_bytes … identity.json` and `test_identity: Expected 'gad_b18b86ce1389e46e' Was 'gad_b18b86ce1389e46d'`.

- [ ] **Step 3: Run it on the real vectors**

Run: `ctest --test-dir build/host --output-on-failure -L "unit|vectors"`
Expected: `100% tests passed, 0 tests failed out of 4`; `test_vectors` prints `8 Tests 0 Failures 0 Ignored`. If a case fails, compare P1's file against contract §4.4 before touching code. The vectors are shared with OpenMausBot, so a mismatch is a contract question, not a firmware patch.

- [ ] **Step 4: Commit**

```bash
git add firmware/tests/test_vectors.c firmware/tests/CMakeLists.txt
git commit -m "firmware: protocol vectors pass in C" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 5: Board table and shared layout

**Files:**
- Create: `firmware/core/src/boards.c`, `firmware/core/src/ui_layout.c`
- Modify: `firmware/core/CMakeLists.txt`, `firmware/tests/CMakeLists.txt`
- Test: `firmware/tests/test_layout.c`

**Interfaces:**
- Consumes: `gadget_board.h`, `gadget_ui.h`, `gadget_ui_model.h`.
- Produces:
  - `gadget_board_by_id` and `gadget_board_at`: the four rows of contract §2.3, with `ota_max` 6291456 on all of them.
  - `ui_safe_area(board)`: on round screens, the inscribed square (`w * 181 / 256`, so 329 px on 466) minus 8 px, giving `{76, 76, 313, 313}` on the AMOLED boards; elsewhere the screen minus 8 px.
  - `ui_layout_ask(board, n, out)`: at most two buttons per row, anchored to the bottom of the safe area, with button height `max(40, safe.h / 6)` and an 8 px gap; with an odd count, the last button sits alone and full width. P2b draws its buttons on exactly these rectangles.
  - `ui_hit_test(m, x, y)`: the option index on an answerable Ask screen, else `-1`.

- [ ] **Step 1: Write the failing test**

Create `firmware/tests/test_layout.c`:

```c
/* firmware/tests/test_layout.c */
/* SPDX-License-Identifier: Apache-2.0 */
/* Board table (core/src/boards.c) and the pure layout functions
 * (core/src/ui_layout.c) that core and the LVGL UI share. */
#include <string.h>
#include "gadget_board.h"
#include "gadget_ui.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

static bool inside(gadget_rect_t outer, gadget_rect_t r) {
  return r.x >= outer.x && r.y >= outer.y && r.x + r.w <= outer.x + outer.w && r.y + r.h <= outer.y + outer.h;
}

static bool overlap(gadget_rect_t a, gadget_rect_t b) {
  return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h;
}

static void test_board_table_matches_contract(void) {
  const gadget_board_t *b = gadget_board_by_id("amoled-175c");
  TEST_ASSERT_NOT_NULL(b);
  TEST_ASSERT_EQUAL_STRING("Waveshare ESP32-S3-Touch-AMOLED-1.75C", b->display_name);
  TEST_ASSERT_EQUAL_UINT16(466, b->screen_w);
  TEST_ASSERT_TRUE(b->screen_round);
  TEST_ASSERT_EQUAL_UINT32(GADGET_INPUT_TOUCH | GADGET_INPUT_TALK | GADGET_INPUT_CANCEL, b->input_mask);
  TEST_ASSERT_EQUAL_INT(GADGET_ART_S240, b->art_profile);
  b = gadget_board_by_id("amoled-175");
  TEST_ASSERT_EQUAL_UINT32(GADGET_INPUT_TOUCH | GADGET_INPUT_TALK, b->input_mask);
  b = gadget_board_by_id("lcd-154");
  TEST_ASSERT_EQUAL_UINT16(240, b->screen_w);
  TEST_ASSERT_EQUAL_UINT16(200, b->image_w);
  TEST_ASSERT_TRUE(b->has_battery);
  TEST_ASSERT_EQUAL_INT(GADGET_ART_S150, b->art_profile);
  b = gadget_board_by_id("devkit");
  TEST_ASSERT_EQUAL_UINT32(24000, b->speaker_rate);
  TEST_ASSERT_FALSE(b->has_battery);
  TEST_ASSERT_EQUAL_UINT16(280, b->image_w);
  TEST_ASSERT_EQUAL_UINT16(200, b->image_h);
  TEST_ASSERT_NULL(gadget_board_by_id("nope"));
  TEST_ASSERT_NULL(gadget_board_by_id(NULL));
  size_t n = 0;
  while (gadget_board_at(n) != NULL) {
    const gadget_board_t *x = gadget_board_at(n);
    TEST_ASSERT_EQUAL_UINT32(6291456, x->ota_max);
    TEST_ASSERT_EQUAL_UINT32(16000, x->mic_rate);
    TEST_ASSERT_TRUE(strlen(x->id) <= 32);
    n++;
  }
  TEST_ASSERT_EQUAL_size_t(4, n);
}

static void test_safe_area(void) {
  gadget_rect_t r = ui_safe_area(gadget_board_by_id("amoled-175c"));
  /* inscribed square 329 px (466 * 181 / 256), centred, minus an 8 px margin */
  TEST_ASSERT_EQUAL_INT16(76, r.x);
  TEST_ASSERT_EQUAL_INT16(76, r.y);
  TEST_ASSERT_EQUAL_INT16(313, r.w);
  TEST_ASSERT_EQUAL_INT16(313, r.h);
  r = ui_safe_area(gadget_board_by_id("devkit"));
  TEST_ASSERT_EQUAL_INT16(8, r.x);
  TEST_ASSERT_EQUAL_INT16(8, r.y);
  TEST_ASSERT_EQUAL_INT16(304, r.w);
  TEST_ASSERT_EQUAL_INT16(224, r.h);
}

static void test_ask_buttons_fit_and_do_not_overlap(void) {
  for (size_t bi = 0; gadget_board_at(bi) != NULL; bi++) {
    const gadget_board_t *b = gadget_board_at(bi);
    gadget_rect_t safe = ui_safe_area(b);
    for (uint8_t n = 1; n <= UI_ASK_OPTIONS_MAX; n++) {
      gadget_rect_t r[UI_ASK_OPTIONS_MAX];
      ui_layout_ask(b, n, r);
      for (uint8_t i = 0; i < n; i++) {
        TEST_ASSERT_TRUE(r[i].w >= 40 && r[i].h >= 40);
        TEST_ASSERT_TRUE_MESSAGE(inside(safe, r[i]), b->id);
        for (uint8_t j = 0; j < i; j++) TEST_ASSERT_FALSE(overlap(r[i], r[j]));
      }
      for (uint8_t i = n; i < UI_ASK_OPTIONS_MAX; i++) TEST_ASSERT_EQUAL_INT16(0, r[i].w);
    }
  }
}

static void test_ask_rows_fill_left_to_right(void) {
  gadget_rect_t r[UI_ASK_OPTIONS_MAX];
  const gadget_board_t *b = gadget_board_by_id("amoled-175c");
  ui_layout_ask(b, 2, r);
  TEST_ASSERT_EQUAL_INT16(r[0].y, r[1].y);
  TEST_ASSERT_TRUE(r[0].x < r[1].x);
  ui_layout_ask(b, 3, r);
  TEST_ASSERT_TRUE(r[2].y > r[0].y);              /* the odd one out sits alone on the bottom row */
  TEST_ASSERT_EQUAL_INT16(ui_safe_area(b).w, r[2].w);
  ui_layout_ask(b, 0, r);
  TEST_ASSERT_EQUAL_INT16(0, r[0].w);
}

static void test_hit_test(void) {
  static ui_model_t m;
  memset(&m, 0, sizeof m);
  m.screen = UI_SCREEN_ASK;
  m.ask.n_options = 2;
  m.ask.answerable = true;
  gadget_rect_t r[UI_ASK_OPTIONS_MAX];
  ui_layout_ask(gadget_board_by_id("amoled-175c"), 2, r);
  m.ask.options[0].rect = r[0];
  m.ask.options[1].rect = r[1];
  TEST_ASSERT_EQUAL_INT(0, ui_hit_test(&m, (int16_t)(r[0].x + r[0].w / 2), (int16_t)(r[0].y + r[0].h / 2)));
  TEST_ASSERT_EQUAL_INT(1, ui_hit_test(&m, (int16_t)(r[1].x + 1), (int16_t)(r[1].y + 1)));
  TEST_ASSERT_EQUAL_INT(-1, ui_hit_test(&m, 0, 0));
  m.ask.answerable = false;
  TEST_ASSERT_EQUAL_INT(-1, ui_hit_test(&m, (int16_t)(r[0].x + 1), (int16_t)(r[0].y + 1)));
  m.ask.answerable = true;
  m.screen = UI_SCREEN_IDLE;
  TEST_ASSERT_EQUAL_INT(-1, ui_hit_test(&m, (int16_t)(r[0].x + 1), (int16_t)(r[0].y + 1)));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_board_table_matches_contract);
  RUN_TEST(test_safe_area);
  RUN_TEST(test_ask_buttons_fit_and_do_not_overlap);
  RUN_TEST(test_ask_rows_fill_left_to_right);
  RUN_TEST(test_hit_test);
  return UNITY_END();
}
```

Change `firmware/tests/CMakeLists.txt` exactly as this diff shows (`-` lines go, `+` lines come; the rest is context):

```diff
--- a/firmware/tests/CMakeLists.txt
+++ b/firmware/tests/CMakeLists.txt
@@ -14,6 +14,7 @@ endfunction()
 gadget_pure_test(util)
 gadget_pure_test(proto)
 gadget_pure_test(crypto)
+gadget_pure_test(layout)
 
 # protocol/vectors in C (label vectors).
 add_executable(test_vectors test_vectors.c)
```

- [ ] **Step 2: Run it to see it fail**

Run: `cmake --build build/host -j10`
Expected: the link of `test_layout` fails on `gadget_board_by_id`, `ui_safe_area`, `ui_layout_ask` and `ui_hit_test`.

- [ ] **Step 3: Write the implementation**

Create `firmware/core/src/boards.c`:

```c
/* firmware/core/src/boards.c */
/* SPDX-License-Identifier: Apache-2.0 */
/* The four board descriptors (contract §2.3), shared by the simulator and
 * the ESP32 port. Pins live in ports/esp32/boards/<id>/board.h (plan P2c).
 * Adding a board: a row here, a directory under ports/esp32/boards/, and
 * its art profile. */
#include <string.h>
#include "gadget_board.h"

#define OTA_SLOT_BYTES 6291456u /* partitions/16mb.csv ota_0 and ota_1: 0x600000 */

static const gadget_board_t BOARDS[] = {
    {.id = "amoled-175c",
     .display_name = "Waveshare ESP32-S3-Touch-AMOLED-1.75C",
     .screen_w = 466, .screen_h = 466, .screen_round = true,
     .image_w = 300, .image_h = 300,
     .mic_rate = 16000, .speaker_rate = 16000,
     .input_mask = GADGET_INPUT_TOUCH | GADGET_INPUT_TALK | GADGET_INPUT_CANCEL,
     .has_battery = true, .ota_max = OTA_SLOT_BYTES, .art_profile = GADGET_ART_S240},
    {.id = "amoled-175",
     .display_name = "Waveshare ESP32-S3-Touch-AMOLED-1.75",
     .screen_w = 466, .screen_h = 466, .screen_round = true,
     .image_w = 300, .image_h = 300,
     .mic_rate = 16000, .speaker_rate = 16000,
     .input_mask = GADGET_INPUT_TOUCH | GADGET_INPUT_TALK,
     .has_battery = true, .ota_max = OTA_SLOT_BYTES, .art_profile = GADGET_ART_S240},
    {.id = "lcd-154",
     .display_name = "Waveshare ESP32-S3-LCD-1.54",
     .screen_w = 240, .screen_h = 240, .screen_round = false,
     .image_w = 200, .image_h = 200,
     .mic_rate = 16000, .speaker_rate = 16000,
     .input_mask = GADGET_INPUT_TALK | GADGET_INPUT_CANCEL,
     .has_battery = true, .ota_max = OTA_SLOT_BYTES, .art_profile = GADGET_ART_S150},
    {.id = "devkit",
     .display_name = "ESP32-S3-DevKitC-1-N16R8 + 2\" ST7789",
     .screen_w = 320, .screen_h = 240, .screen_round = false,
     .image_w = 280, .image_h = 200,
     .mic_rate = 16000, .speaker_rate = 24000,
     .input_mask = GADGET_INPUT_TALK | GADGET_INPUT_CANCEL,
     .has_battery = false, .ota_max = OTA_SLOT_BYTES, .art_profile = GADGET_ART_S150},
};

#define BOARD_COUNT (sizeof BOARDS / sizeof BOARDS[0])

const gadget_board_t *gadget_board_by_id(const char *id) {
  if (id == NULL) return NULL;
  for (size_t i = 0; i < BOARD_COUNT; i++) {
    if (strcmp(BOARDS[i].id, id) == 0) return &BOARDS[i];
  }
  return NULL;
}

const gadget_board_t *gadget_board_at(size_t index) { return index < BOARD_COUNT ? &BOARDS[index] : NULL; }
```

Create `firmware/core/src/ui_layout.c`:

```c
/* firmware/core/src/ui_layout.c */
/* SPDX-License-Identifier: Apache-2.0 */
/* Pure layout shared by core (hit-testing) and the LVGL UI (drawing), so a
 * touch lands on exactly the button the UI draws (gadget_ui.h). */
#include <string.h>
#include "gadget_ui.h"

#define SAFE_MARGIN 8
#define BUTTON_GAP 8
#define BUTTON_MIN_H 40

gadget_rect_t ui_safe_area(const gadget_board_t *board) {
  gadget_rect_t r = {0, 0, (int16_t)board->screen_w, (int16_t)board->screen_h};
  if (board->screen_round) {
    /* the square inscribed in the circle: side = diameter / sqrt(2) */
    int16_t side = (int16_t)(((uint32_t)board->screen_w * 181u) / 256u);
    r.x = (int16_t)((board->screen_w - side) / 2);
    r.y = (int16_t)((board->screen_h - side) / 2);
    r.w = side;
    r.h = side;
  }
  r.x += SAFE_MARGIN;
  r.y += SAFE_MARGIN;
  r.w -= 2 * SAFE_MARGIN;
  r.h -= 2 * SAFE_MARGIN;
  return r;
}

/* Up to two buttons per row, rows anchored to the bottom of the safe area.
 * With an odd count the last option sits alone, full width, on the bottom row. */
void ui_layout_ask(const gadget_board_t *board, uint8_t n_options, gadget_rect_t out[UI_ASK_OPTIONS_MAX]) {
  memset(out, 0, sizeof(gadget_rect_t) * UI_ASK_OPTIONS_MAX);
  if (n_options == 0 || n_options > UI_ASK_OPTIONS_MAX) return;
  gadget_rect_t s = ui_safe_area(board);
  int16_t bh = (int16_t)(s.h / 6);
  if (bh < BUTTON_MIN_H) bh = BUTTON_MIN_H;
  int rows = (n_options + 1) / 2;
  int16_t half_w = (int16_t)((s.w - BUTTON_GAP) / 2);
  for (uint8_t i = 0; i < n_options; i++) {
    int row = i / 2;
    bool alone = (i == n_options - 1) && (n_options % 2 == 1);
    int16_t y = (int16_t)(s.y + s.h - (rows - row) * bh - (rows - 1 - row) * BUTTON_GAP);
    gadget_rect_t r;
    r.y = y;
    r.h = bh;
    if (alone) {
      r.x = s.x;
      r.w = s.w;
    } else {
      r.x = (int16_t)(s.x + (i % 2) * (half_w + BUTTON_GAP));
      r.w = half_w;
    }
    out[i] = r;
  }
}

int ui_hit_test(const ui_model_t *m, int16_t x, int16_t y) {
  if (m == NULL || m->screen != UI_SCREEN_ASK || !m->ask.answerable) return -1;
  for (uint8_t i = 0; i < m->ask.n_options && i < UI_ASK_OPTIONS_MAX; i++) {
    gadget_rect_t r = m->ask.options[i].rect;
    if (r.w > 0 && x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h) return i;
  }
  return -1;
}
```

Change `firmware/core/CMakeLists.txt` exactly as this diff shows (`-` lines go, `+` lines come; the rest is context):

```diff
--- a/firmware/core/CMakeLists.txt
+++ b/firmware/core/CMakeLists.txt
@@ -6,6 +6,8 @@ set(GADGET_CORE_SRCS
   src/util.c
   src/proto.c
   src/crypto_psa.c
+  src/boards.c
+  src/ui_layout.c
 )
 
 if(ESP_PLATFORM)
```

- [ ] **Step 4: Run the tests**

Run: `cmake --build build/host -j10 && ctest --test-dir build/host --output-on-failure -L "unit|vectors"`
Expected: `100% tests passed, 0 tests failed out of 5`; `test_layout` prints `5 Tests 0 Failures 0 Ignored`.

- [ ] **Step 5: Commit**

```bash
git add firmware/core/src/boards.c firmware/core/src/ui_layout.c firmware/core/CMakeLists.txt firmware/tests/test_layout.c firmware/tests/CMakeLists.txt
git commit -m "firmware: board descriptors and the ask layout core and UI share" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 6: Console lines, quoting and grammar

**Files:**
- Create: `firmware/core/src/console.c`
- Modify: `firmware/core/CMakeLists.txt`, `firmware/tests/CMakeLists.txt`
- Test: `firmware/tests/test_console_parse.c`

**Interfaces:**
- Consumes: `gadget_console.h`, `gadget_utf8_len`, `gadget_pair_code_valid`.
- Produces:
  - `gadget_linebuf_init` and `gadget_linebuf_feed`: CR, LF and CRLF each end a line, even when the CR and LF arrive in different chunks; an over-long line is reported as `on_line(NULL)`.
  - `gadget_console_split`, with the `esp_console_split_argv` rules. `""` is an empty argument, so `wifi "Cafe" ""` works. When a line has more than `argv_max` arguments, the return value is `argv_max + 1`.
  - `gadget_console_parse`, with the grammar of contract §2.11. The error texts are exact (P2d's installer shows them):

| Case | Error text |
|---|---|
| Unknown command | `unknown command; try status` |
| Bad `pair` | `pair needs a six-digit code` |
| Bad `wifi` | `wifi needs "<ssid>" "<password>"`, `the Wi-Fi name must be 1-32 bytes`, `the password must be empty, 8-63 characters or 64 hex digits` |
| Bad `host` | `host needs auto or <address>[:port]`, `the port must be 1-65535` |
| Bad `name` | `name needs 1-32 characters` |
| Bad `say` | `say needs 1-2000 characters` |
| Bad `log` | `log needs on or off` |
| Extra arguments | `<cmd> takes no arguments` |
| Open quote | `unterminated quote` (with `cmd_name` set to `""`) |

  `name` and `say` arguments are trimmed. Host addresses are a hostname or IPv4 address of at most 57 bytes.

- [ ] **Step 1: Write the failing test**

Create `firmware/tests/test_console_parse.c`:

```c
/* firmware/tests/test_console_parse.c */
/* SPDX-License-Identifier: Apache-2.0 */
/* Console line assembly, quoting and grammar (core/src/console.c). */
#include <string.h>
#include "gadget_console.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

static char g_lines[8][64];
static int g_count;
static int g_nulls;

static void on_line(const char *line, void *ctx) {
  (void)ctx;
  if (line == NULL) {
    g_nulls++;
    return;
  }
  snprintf(g_lines[g_count++ % 8], sizeof g_lines[0], "%s", line);
}

static void feed(gadget_linebuf_t *lb, const char *s) { gadget_linebuf_feed(lb, s, strlen(s), on_line, NULL); }

static void test_linebuf_handles_cr_lf_crlf(void) {
  static gadget_linebuf_t lb;
  g_count = g_nulls = 0;
  gadget_linebuf_init(&lb);
  feed(&lb, "status\r\nscan\nreboot\rlog on");
  TEST_ASSERT_EQUAL_INT(3, g_count);
  TEST_ASSERT_EQUAL_STRING("status", g_lines[0]);
  TEST_ASSERT_EQUAL_STRING("scan", g_lines[1]);
  TEST_ASSERT_EQUAL_STRING("reboot", g_lines[2]);
  feed(&lb, "\r");   /* completes "log on"; the LF may follow in the next chunk */
  feed(&lb, "\n");   /* ... and is not a second, empty line */
  TEST_ASSERT_EQUAL_INT(4, g_count);
  TEST_ASSERT_EQUAL_STRING("log on", g_lines[3]);
  feed(&lb, "\n");   /* a lone LF is an empty line */
  TEST_ASSERT_EQUAL_INT(5, g_count);
  TEST_ASSERT_EQUAL_STRING("", g_lines[4]);
}

static void test_linebuf_drops_overlong_lines(void) {
  static gadget_linebuf_t lb;
  static char big[GADGET_CONSOLE_LINE_MAX + 10];
  g_count = g_nulls = 0;
  gadget_linebuf_init(&lb);
  memset(big, 'x', sizeof big - 1);
  big[sizeof big - 1] = '\0';
  feed(&lb, big);
  feed(&lb, "\nstatus\n");
  TEST_ASSERT_EQUAL_INT(1, g_nulls);
  TEST_ASSERT_EQUAL_INT(1, g_count);
  TEST_ASSERT_EQUAL_STRING("status", g_lines[0]);
  /* exactly LINE_MAX - 1 bytes still fits */
  g_count = 0;
  big[GADGET_CONSOLE_LINE_MAX - 1] = '\0';
  feed(&lb, big);
  feed(&lb, "\n");
  TEST_ASSERT_EQUAL_INT(1, g_count);
  TEST_ASSERT_EQUAL_INT(1, g_nulls);
}

static void test_split_quotes_and_escapes(void) {
  char line[] = "wifi \"My Home\" \"p\\\"w\\\\d\" a\\ b \"\" x\"y z\"";
  char *argv[GADGET_CONSOLE_ARGV_MAX];
  int argc = gadget_console_split(line, argv, GADGET_CONSOLE_ARGV_MAX);
  TEST_ASSERT_EQUAL_INT(6, argc);
  TEST_ASSERT_EQUAL_STRING("wifi", argv[0]);
  TEST_ASSERT_EQUAL_STRING("My Home", argv[1]);
  TEST_ASSERT_EQUAL_STRING("p\"w\\d", argv[2]);
  TEST_ASSERT_EQUAL_STRING("a b", argv[3]);
  TEST_ASSERT_EQUAL_STRING("", argv[4]);
  TEST_ASSERT_EQUAL_STRING("xy z", argv[5]);
  char other[] = "  say \t hi\\n  ";
  argc = gadget_console_split(other, argv, GADGET_CONSOLE_ARGV_MAX);
  TEST_ASSERT_EQUAL_INT(2, argc);
  TEST_ASSERT_EQUAL_STRING("hi\\n", argv[1]); /* other escapes keep their backslash */
  char open_quote[] = "name \"Desk";
  TEST_ASSERT_EQUAL_INT(-1, gadget_console_split(open_quote, argv, GADGET_CONSOLE_ARGV_MAX));
  char many[] = "a b c d";
  TEST_ASSERT_EQUAL_INT(3, gadget_console_split(many, argv, 2)); /* argv_max + 1: too many */
  char empty[] = "   ";
  TEST_ASSERT_EQUAL_INT(0, gadget_console_split(empty, argv, GADGET_CONSOLE_ARGV_MAX));
}

static gadget_console_cmd_t parse(const char *text, gadget_console_parsed_t *p) {
  static char buf[GADGET_CONSOLE_LINE_MAX];
  strncpy(buf, text, sizeof buf - 1);
  buf[sizeof buf - 1] = '\0';
  return gadget_console_parse(buf, p);
}

static void test_parse_commands(void) {
  gadget_console_parsed_t p;
  TEST_ASSERT_EQUAL_INT(GC_EMPTY, parse("", &p));
  TEST_ASSERT_EQUAL_INT(GC_EMPTY, parse("   ", &p));
  TEST_ASSERT_EQUAL_INT(GC_WIFI, parse("wifi \"My Home\" \"secret123\"", &p));
  TEST_ASSERT_EQUAL_STRING("My Home", p.a);
  TEST_ASSERT_EQUAL_STRING("secret123", p.b);
  TEST_ASSERT_EQUAL_INT(GC_WIFI, parse("wifi Cafe \"\"", &p));
  TEST_ASSERT_EQUAL_STRING("", p.b);
  TEST_ASSERT_EQUAL_INT(GC_WIFI, parse("wifi Lab 0123456789abcdef0123456789ABCDEF0123456789abcdef0123456789abcdef", &p));
  TEST_ASSERT_EQUAL_INT(GC_SCAN, parse("scan", &p));
  TEST_ASSERT_EQUAL_INT(GC_HOST_AUTO, parse("host auto", &p));
  TEST_ASSERT_EQUAL_INT(GC_HOST_SET, parse("host 192.168.1.20", &p));
  TEST_ASSERT_EQUAL_STRING("192.168.1.20", p.a);
  TEST_ASSERT_EQUAL_UINT16(8810, p.port);
  TEST_ASSERT_EQUAL_INT(GC_HOST_SET, parse("host omkars-mac.local:9000", &p));
  TEST_ASSERT_EQUAL_STRING("omkars-mac.local", p.a);
  TEST_ASSERT_EQUAL_UINT16(9000, p.port);
  TEST_ASSERT_EQUAL_INT(GC_PAIR, parse("pair 123456", &p));
  TEST_ASSERT_EQUAL_STRING("123456", p.a);
  TEST_ASSERT_EQUAL_INT(GC_NAME, parse("name \"  Desk Maus  \"", &p));
  TEST_ASSERT_EQUAL_STRING("Desk Maus", p.a);
  TEST_ASSERT_EQUAL_INT(GC_SAY, parse("say \"What's on today?\"", &p));
  TEST_ASSERT_EQUAL_STRING("What's on today?", p.a);
  TEST_ASSERT_EQUAL_INT(GC_STATUS, parse("status", &p));
  TEST_ASSERT_EQUAL_INT(GC_LOG_OFF, parse("log off", &p));
  TEST_ASSERT_EQUAL_INT(GC_LOG_ON, parse("log on", &p));
  TEST_ASSERT_EQUAL_INT(GC_FORGET, parse("forget", &p));
  TEST_ASSERT_EQUAL_INT(GC_REBOOT, parse("reboot", &p));
}

static void test_parse_errors(void) {
  gadget_console_parsed_t p;
  TEST_ASSERT_EQUAL_INT(GC_UNKNOWN, parse("Status", &p)); /* case-sensitive */
  TEST_ASSERT_EQUAL_STRING("Status", p.cmd_name);
  TEST_ASSERT_EQUAL_STRING("unknown command; try status", p.error);
  TEST_ASSERT_EQUAL_INT(GC_BAD_ARGS, parse("pair 12345", &p));
  TEST_ASSERT_EQUAL_STRING("pair", p.cmd_name);
  TEST_ASSERT_EQUAL_STRING("pair needs a six-digit code", p.error);
  TEST_ASSERT_EQUAL_INT(GC_BAD_ARGS, parse("wifi Home short", &p));
  TEST_ASSERT_EQUAL_STRING("the password must be empty, 8-63 characters or 64 hex digits", p.error);
  TEST_ASSERT_EQUAL_INT(GC_BAD_ARGS, parse("wifi \"\" password1", &p));
  TEST_ASSERT_EQUAL_STRING("the Wi-Fi name must be 1-32 bytes", p.error);
  TEST_ASSERT_EQUAL_INT(GC_BAD_ARGS, parse("wifi \"123456789012345678901234567890123\" password1", &p));
  TEST_ASSERT_EQUAL_INT(GC_BAD_ARGS, parse("wifi Home", &p));
  TEST_ASSERT_EQUAL_STRING("wifi needs \"<ssid>\" \"<password>\"", p.error);
  TEST_ASSERT_EQUAL_INT(GC_BAD_ARGS, parse("host 1.2.3.4:0", &p));
  TEST_ASSERT_EQUAL_STRING("the port must be 1-65535", p.error);
  TEST_ASSERT_EQUAL_INT(GC_BAD_ARGS, parse("host 1.2.3.4:70000", &p));
  TEST_ASSERT_EQUAL_INT(GC_BAD_ARGS, parse("host fe80::1", &p));
  TEST_ASSERT_EQUAL_INT(GC_BAD_ARGS, parse("host bad_name", &p));
  TEST_ASSERT_EQUAL_STRING("host needs auto or <address>[:port]", p.error);
  TEST_ASSERT_EQUAL_INT(GC_BAD_ARGS, parse("name \"   \"", &p));
  TEST_ASSERT_EQUAL_STRING("name needs 1-32 characters", p.error);
  TEST_ASSERT_EQUAL_INT(GC_BAD_ARGS, parse("name 123456789012345678901234567890123", &p));
  TEST_ASSERT_EQUAL_INT(GC_NAME, parse("name \xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9"
                                      "\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9"
                                      "\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9"
                                      "\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9",
                                      &p)); /* 32 code points, 64 bytes */
  TEST_ASSERT_EQUAL_INT(GC_BAD_ARGS, parse("say \"\"", &p));
  TEST_ASSERT_EQUAL_STRING("say needs 1-2000 characters", p.error);
  TEST_ASSERT_EQUAL_INT(GC_BAD_ARGS, parse("log maybe", &p));
  TEST_ASSERT_EQUAL_STRING("log needs on or off", p.error);
  TEST_ASSERT_EQUAL_INT(GC_BAD_ARGS, parse("status now", &p));
  TEST_ASSERT_EQUAL_STRING("status takes no arguments", p.error);
  TEST_ASSERT_EQUAL_INT(GC_BAD_ARGS, parse("name \"Desk", &p));
  TEST_ASSERT_EQUAL_STRING("", p.cmd_name);
  TEST_ASSERT_EQUAL_STRING("unterminated quote", p.error);
}

static void test_say_accepts_the_longest_line(void) {
  /* 2000 two-byte code points, quoted: the longest valid say line fits */
  static char line[GADGET_CONSOLE_LINE_MAX];
  size_t o = 0;
  memcpy(line, "say \"", 5);
  o = 5;
  for (int i = 0; i < 2000; i++) {
    line[o++] = (char)0xc3;
    line[o++] = (char)0xa9;
  }
  line[o++] = '"';
  line[o] = '\0';
  gadget_console_parsed_t p;
  TEST_ASSERT_EQUAL_INT(GC_SAY, gadget_console_parse(line, &p));
  line[o - 1] = (char)0xc3; /* one more code point: 2001 */
  line[o++] = (char)0xa9;
  line[o++] = '"';
  line[o] = '\0';
  TEST_ASSERT_EQUAL_INT(GC_BAD_ARGS, gadget_console_parse(line, &p));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_linebuf_handles_cr_lf_crlf);
  RUN_TEST(test_linebuf_drops_overlong_lines);
  RUN_TEST(test_split_quotes_and_escapes);
  RUN_TEST(test_parse_commands);
  RUN_TEST(test_parse_errors);
  RUN_TEST(test_say_accepts_the_longest_line);
  return UNITY_END();
}
```

Change `firmware/tests/CMakeLists.txt` exactly as this diff shows (`-` lines go, `+` lines come; the rest is context):

```diff
--- a/firmware/tests/CMakeLists.txt
+++ b/firmware/tests/CMakeLists.txt
@@ -15,6 +15,7 @@ gadget_pure_test(util)
 gadget_pure_test(proto)
 gadget_pure_test(crypto)
 gadget_pure_test(layout)
+gadget_pure_test(console_parse)
 
 # protocol/vectors in C (label vectors).
 add_executable(test_vectors test_vectors.c)
```

- [ ] **Step 2: Run it to see it fail**

Run: `cmake --build build/host -j10`
Expected: the link of `test_console_parse` fails on `gadget_linebuf_feed`, `gadget_console_split` and `gadget_console_parse`.

- [ ] **Step 3: Write the implementation**

Create `firmware/core/src/console.c`:

```c
/* firmware/core/src/console.c */
/* SPDX-License-Identifier: Apache-2.0 */
/* Console line assembly, argument splitting and the command grammar
 * (gadget_console.h, spec §5.6, contract §2.11). Pure: no HAL calls. */
#include <stdio.h>
#include <string.h>
#include "gadget_console.h"
#include "gadget_util.h"

/* ---- line assembler -------------------------------------------------------- */

void gadget_linebuf_init(gadget_linebuf_t *lb) { memset(lb, 0, sizeof *lb); }

static void end_line(gadget_linebuf_t *lb, gadget_line_fn on_line, void *ctx) {
  if (lb->overflow) {
    on_line(NULL, ctx);
  } else {
    lb->buf[lb->len] = '\0';
    on_line(lb->buf, ctx);
  }
  lb->len = 0;
  lb->overflow = false;
}

void gadget_linebuf_feed(gadget_linebuf_t *lb, const char *data, size_t n, gadget_line_fn on_line, void *ctx) {
  for (size_t i = 0; i < n; i++) {
    char c = data[i];
    if (c == '\n') {
      if (lb->last_cr) {
        lb->last_cr = false; /* the LF of a CRLF: already ended */
        continue;
      }
      end_line(lb, on_line, ctx);
      continue;
    }
    lb->last_cr = false;
    if (c == '\r') {
      lb->last_cr = true;
      end_line(lb, on_line, ctx);
      continue;
    }
    if (lb->len + 1 >= GADGET_CONSOLE_LINE_MAX) {
      lb->overflow = true;
      continue;
    }
    lb->buf[lb->len++] = c;
  }
}

/* ---- argument splitting ------------------------------------------------------ */

static bool is_space(char c) { return c == ' ' || c == '\t'; }

int gadget_console_split(char *line, char *argv[], int argv_max) {
  int argc = 0;
  char *r = line;
  char *w = line;
  while (*r) {
    while (is_space(*r)) r++;
    if (*r == '\0') break;
    char *start = w;
    bool quoted = false;
    while (*r && (quoted || !is_space(*r))) {
      if (*r == '\\' && (r[1] == '\\' || r[1] == '"' || r[1] == ' ')) {
        *w++ = r[1];
        r += 2;
      } else if (*r == '"') {
        quoted = !quoted;
        r++;
      } else {
        *w++ = *r++;
      }
    }
    if (quoted) return -1;
    bool more = *r != '\0';
    if (more) r++;  /* skip the separating whitespace before writing the NUL */
    *w++ = '\0';
    if (argc < argv_max) argv[argc] = start;
    argc++;
    if (!more) break;
  }
  return argc > argv_max ? argv_max + 1 : argc;
}

/* ---- grammar ------------------------------------------------------------------ */

static gadget_console_cmd_t fail(gadget_console_parsed_t *out, const char *msg) {
  snprintf(out->error, sizeof out->error, "%s", msg);
  out->cmd = GC_BAD_ARGS;
  return GC_BAD_ARGS;
}

static char *trim(char *s) {
  while (is_space(*s)) s++;
  size_t n = strlen(s);
  while (n > 0 && is_space(s[n - 1])) s[--n] = '\0';
  return s;
}

static bool valid_password(const char *p) {
  size_t n = strlen(p);
  if (n == 0 || (n >= 8 && n <= 63)) return true;
  if (n != 64) return false;
  for (size_t i = 0; i < n; i++) {
    char c = p[i];
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) return false;
  }
  return true;
}

/* host <address>[:port]: a hostname or IPv4 address (≤ 57 bytes, so
 * "addr:port" fits a 64-byte buffer), port 1-65535. */
static gadget_console_cmd_t parse_host(char *arg, gadget_console_parsed_t *out) {
  static const char *usage = "host needs auto or <address>[:port]";
  char *colon = strchr(arg, ':');
  out->port = GADGET_DEFAULT_PORT;
  if (colon != NULL) {
    if (strchr(colon + 1, ':') != NULL) return fail(out, usage);
    *colon = '\0';
    const char *p = colon + 1;
    if (*p == '\0' || strlen(p) > 5) return fail(out, "the port must be 1-65535");
    unsigned long v = 0;
    for (; *p; p++) {
      if (*p < '0' || *p > '9') return fail(out, "the port must be 1-65535");
      v = v * 10 + (unsigned long)(*p - '0');
    }
    if (v < 1 || v > 65535) return fail(out, "the port must be 1-65535");
    out->port = (uint16_t)v;
  }
  size_t n = strlen(arg);
  if (n == 0 || n > 57) return fail(out, usage);
  for (size_t i = 0; i < n; i++) {
    char c = arg[i];
    bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '-';
    if (!ok) return fail(out, usage);
  }
  out->a = arg;
  out->cmd = GC_HOST_SET;
  return GC_HOST_SET;
}

gadget_console_cmd_t gadget_console_parse(char *line, gadget_console_parsed_t *out) {
  memset(out, 0, sizeof *out);
  out->cmd_name = "";
  char *argv[GADGET_CONSOLE_ARGV_MAX];
  int argc = gadget_console_split(line, argv, GADGET_CONSOLE_ARGV_MAX);
  if (argc < 0) return fail(out, "unterminated quote");
  if (argc == 0) {
    out->cmd = GC_EMPTY;
    return GC_EMPTY;
  }
  const char *cmd = argv[0];
  out->cmd_name = cmd;

  if (strcmp(cmd, "status") == 0 || strcmp(cmd, "scan") == 0 || strcmp(cmd, "forget") == 0 ||
      strcmp(cmd, "reboot") == 0) {
    if (argc != 1) {
      snprintf(out->error, sizeof out->error, "%s takes no arguments", cmd);
      out->cmd = GC_BAD_ARGS;
      return GC_BAD_ARGS;
    }
    out->cmd = cmd[0] == 's' ? (cmd[1] == 't' ? GC_STATUS : GC_SCAN) : (cmd[0] == 'f' ? GC_FORGET : GC_REBOOT);
    return out->cmd;
  }
  if (strcmp(cmd, "wifi") == 0) {
    if (argc != 3) return fail(out, "wifi needs \"<ssid>\" \"<password>\"");
    size_t n = strlen(argv[1]);
    if (n < 1 || n > 32) return fail(out, "the Wi-Fi name must be 1-32 bytes");
    if (!valid_password(argv[2])) return fail(out, "the password must be empty, 8-63 characters or 64 hex digits");
    out->a = argv[1];
    out->b = argv[2];
    out->cmd = GC_WIFI;
    return GC_WIFI;
  }
  if (strcmp(cmd, "host") == 0) {
    if (argc != 2) return fail(out, "host needs auto or <address>[:port]");
    if (strcmp(argv[1], "auto") == 0) {
      out->cmd = GC_HOST_AUTO;
      return GC_HOST_AUTO;
    }
    return parse_host(argv[1], out);
  }
  if (strcmp(cmd, "pair") == 0) {
    if (argc != 2 || !gadget_pair_code_valid(argv[1])) return fail(out, "pair needs a six-digit code");
    out->a = argv[1];
    out->cmd = GC_PAIR;
    return GC_PAIR;
  }
  if (strcmp(cmd, "name") == 0 || strcmp(cmd, "say") == 0) {
    bool is_name = cmd[0] == 'n';
    const char *msg = is_name ? "name needs 1-32 characters" : "say needs 1-2000 characters";
    if (argc != 2) return fail(out, msg);
    char *text = trim(argv[1]);
    size_t cps = gadget_utf8_len(text);
    if (cps < 1 || cps > (is_name ? GADGET_NAME_MAX : GADGET_SAY_MAX)) return fail(out, msg);
    out->a = text;
    out->cmd = is_name ? GC_NAME : GC_SAY;
    return out->cmd;
  }
  if (strcmp(cmd, "log") == 0) {
    if (argc == 2 && strcmp(argv[1], "off") == 0) {
      out->cmd = GC_LOG_OFF;
      return GC_LOG_OFF;
    }
    if (argc == 2 && strcmp(argv[1], "on") == 0) {
      out->cmd = GC_LOG_ON;
      return GC_LOG_ON;
    }
    return fail(out, "log needs on or off");
  }
  snprintf(out->error, sizeof out->error, "unknown command; try status");
  out->cmd = GC_UNKNOWN;
  return GC_UNKNOWN;
}
```

Change `firmware/core/CMakeLists.txt` exactly as this diff shows (`-` lines go, `+` lines come; the rest is context):

```diff
--- a/firmware/core/CMakeLists.txt
+++ b/firmware/core/CMakeLists.txt
@@ -8,6 +8,7 @@ set(GADGET_CORE_SRCS
   src/crypto_psa.c
   src/boards.c
   src/ui_layout.c
+  src/console.c
 )
 
 if(ESP_PLATFORM)
```

- [ ] **Step 4: Run the tests**

Run: `cmake --build build/host -j10 && ctest --test-dir build/host --output-on-failure -L "unit|vectors"`
Expected: `100% tests passed, 0 tests failed out of 6`; `test_console_parse` prints `6 Tests 0 Failures 0 Ignored`.

- [ ] **Step 5: Commit**

```bash
git add firmware/core/src/console.c firmware/core/CMakeLists.txt firmware/tests/test_console_parse.c firmware/tests/CMakeLists.txt
git commit -m "firmware: console line assembly, quoting and command grammar" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 7: Core init, identity and the fake HAL

**Files:**
- Create: `firmware/core/src/core_internal.h`, `firmware/core/src/core.c`
- Create (test support): `firmware/tests/fake_hal.h`, `firmware/tests/fake_hal.c`
- Modify: `firmware/core/CMakeLists.txt`, `firmware/tests/CMakeLists.txt`
- Test: `firmware/tests/test_core_init.c`

**Interfaces:**
- Consumes: Tasks 1–6; the storage keys `GADGET_KEY_*` from `gadget_core.h`.
- Produces:
  - **Public:** `core_init`, `core_event` (it accepts every event; later tasks add the handlers), `core_tick`, `core_ui_model`, `core_deinit`, `core_device_id`, `core_fw_version` and `core_set_tap`.
  - **Behavior:** `core_init` loads `dev_key`, or on first boot generates a key with `hal_crypto_keygen` and stores it; a stored key of the wrong length is replaced. It loads every `GADGET_KEY_*`, ignoring an invalid `host_id` or `pair_code`. The default name is `Maus ` plus the first 4 hex characters of the id, or `cfg.default_name` when set. Every name (stored, default, and later console `name` and desktop renames) goes through `core_set_name`, which folds it like other host text and cuts it to 32 code points, the last one `…` (spec §4.3). It joins stored Wi-Fi and prints `@omb {"op":"boot","board":…,"fw":…,"id":…}`. `ui_model_t.rev` changes only when something other than `rev` and `now_ms` changed.
  - **Private (`core_internal.h`):** `g_core` (state plus the screen flags `g_core.f.*`), `core_store_str`, `core_store_erase`, `core_text_copy` and `core_text_copy_tail` (cut, then fold anything outside Latin-1, U+2026 and U+2192 to `?`), `core_set_name`, `core_next_turn_id` and `core_omb(cJSON *)`.
  - **Test support (`fake_hal.h`):** the full API. Later tasks use `fake_boot`, `fake_ready`, `fake_handshake`, `fake_run`, `fake_ws_*`, `fake_mic_frames`, `fake_spk_*`, `fake_ota_*`, `fake_omb` and `FAKE_EXPECT_RESTART`. The fake implements every `hal_*` except crypto; it queues the HAL's asynchronous results (WS_CLOSED after `hal_ws_close`, OTA_WRITTEN) and delivers them before the next tick, and its `hal_restart` jumps back to the test with `longjmp`.

- [ ] **Step 1: Write the fake HAL and the failing test**

Create `firmware/tests/fake_hal.h`:

```c
/* firmware/tests/fake_hal.h */
/* SPDX-License-Identifier: Apache-2.0 */
/* An in-memory HAL for core's unit tests. It implements every hal_*
 * function except the crypto group (core's crypto_psa.c is real), records
 * what core asked for, and drives core_tick() on a virtual clock. Results
 * the real ports deliver asynchronously (WS_CLOSED after hal_ws_close,
 * OTA_WRITTEN after hal_ota_write, the Wi-Fi scan) are queued and delivered
 * before the next tick, never from inside a HAL call. */
#ifndef FAKE_HAL_H
#define FAKE_HAL_H

#include <setjmp.h>
#include "cJSON.h"
#include "gadget_core.h"
#include "gadget_hal.h"

/* Contract §1.7 pinned values. */
#define FAKE_RFC_PRIV_HEX "c9afa9d845ba75166b5c215767b1d6934e50c3db36e89b127b8a622b120f6721"
#define FAKE_RFC_ID "gad_b18b86ce1389e46d"
#define FAKE_HOST_ID "000102030405060708090a0b0c0d0e0f"
#define FAKE_NONCE "AAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8="

/* Forget everything (storage, frames, clock, flags). Call in setUp(). */
void fake_reset(void);

/* ---- clock: core_tick() every 10 ms ---------------------------------------- */
uint64_t fake_now(void);
void fake_run(uint32_t ms);                 /* deliver queued events, tick, repeat until ms elapsed */

/* ---- boot helpers ----------------------------------------------------------- */
/* core_deinit() then core_init() with fw "1.0.0", seed 1 and the given board,
 * then fake_run(10). Storage is kept, like a reboot. */
void fake_boot(const char *board_id);
void fake_boot_cfg(const core_config_t *cfg);   /* the same with a caller config */
/* Storage of a paired gadget: the RFC key as dev_key, host_addr
 * 127.0.0.1:8810, host_id FAKE_HOST_ID, host_name "Mac", bot b_jev/Jev. */
void fake_store_paired(void);
/* Accept the pending connection, send challenge (pinned nonce and host_id),
 * check that prove verifies against the gadget's own key, send ready with
 * bot {b_jev, Jev}, and clear the sent-frame log. Asserts on any deviation. */
void fake_handshake(void);
/* fake_store_paired(), fake_boot(board_id), fake_handshake(). */
void fake_ready(const char *board_id);

/* ---- storage ---------------------------------------------------------------- */
void fake_storage_put(const char *key, const char *value);
void fake_storage_put_blob(const char *key, const void *data, size_t len);
const char *fake_storage_str(const char *key);  /* NULL when absent or a blob */
bool fake_storage_has(const char *key);
size_t fake_storage_blob_len(const char *key);  /* 0 when absent */
int fake_storage_writes(void);                  /* set_* calls so far */

/* ---- console output (hal_console_write) ------------------------------------- */
size_t fake_console_count(void);
const char *fake_console_line(size_t i);
/* The last "@omb " line whose op equals op, parsed (caller cJSON_Delete()s), or NULL. */
cJSON *fake_omb(const char *op);
void fake_console_clear(void);
void fake_console_in(const char *line);         /* deliver GADGET_EV_CONSOLE_LINE now */

/* ---- input and mic ---------------------------------------------------------- */
void fake_input(gadget_input_type_t type, int16_t x, int16_t y);   /* deliver GADGET_EV_INPUT now */
void fake_swipe(gadget_swipe_dir_t dir);
bool fake_mic_running(void);
/* Deliver n 20 ms mic frames of a square wave with this amplitude, with
 * fake_run(20) after each. */
void fake_mic_frames(int n, int16_t amplitude);

/* ---- speaker ---------------------------------------------------------------- */
uint32_t fake_spk_rate(void);                   /* last hal_spk_open rate, 0 never */
size_t fake_spk_accepted(void);                 /* samples hal_spk_write accepted so far */
int fake_spk_stops(void);                       /* hal_spk_stop calls */
int16_t fake_spk_peak(void);                    /* largest |sample| accepted since fake_reset */

/* ---- Wi-Fi ------------------------------------------------------------------ */
void fake_wifi_set(gadget_wifi_state_t st);     /* hal_wifi_state() result + deliver GADGET_EV_WIFI_STATE */
const char *fake_wifi_ssid(void);               /* last hal_wifi_connect ssid, "" never */
int fake_wifi_connects(void);
int fake_wifi_scans(void);
void fake_wifi_scan_result(const gadget_wifi_ap_t *aps, uint8_t count, bool ok);  /* deliver GADGET_EV_WIFI_SCAN */

/* ---- WebSocket -------------------------------------------------------------- */
int fake_ws_opens(void);                        /* hal_ws_open calls */
const char *fake_ws_host(void);
uint16_t fake_ws_port(void);
bool fake_ws_live(void);                        /* opened and no WS_CLOSED delivered yet */
uint16_t fake_ws_close_code(void);              /* code of the last hal_ws_close, 0 none */
void fake_ws_accept(void);                      /* deliver GADGET_EV_WS_OPEN */
void fake_ws_in(const char *json);              /* deliver one text frame from the host */
void fake_ws_bin_in(const uint8_t *data, size_t len);
void fake_ws_ping_in(void);                     /* deliver GADGET_EV_WS_CONTROL */
void fake_ws_drop(uint16_t code);               /* deliver GADGET_EV_WS_CLOSED (host side close/drop) */
size_t fake_ws_sent(void);                      /* text frames sent since the last clear */
const char *fake_ws_text(size_t i);
size_t fake_ws_count(const char *op);
cJSON *fake_ws_last(const char *op);            /* the last sent frame with that op, parsed, or NULL */
size_t fake_ws_bin_sent(void);
const uint8_t *fake_ws_bin(size_t i, size_t *len);
void fake_ws_clear(void);

/* ---- mDNS ------------------------------------------------------------------- */
int fake_mdns_browses(void);
void fake_mdns_unsupported(bool on);            /* hal_mdns_browse returns GADGET_ERR_UNSUPPORTED */
void fake_mdns_result(const gadget_mdns_host_t *hosts, uint8_t count);   /* deliver GADGET_EV_MDNS */

/* ---- OTA -------------------------------------------------------------------- */
void fake_ota_set_running(hal_ota_img_state_t st);
uint32_t fake_ota_size(void);                   /* size given to hal_ota_begin, 0 none */
uint32_t fake_ota_written(void);                /* bytes accepted by hal_ota_write */
const uint8_t *fake_ota_image(void);
bool fake_ota_finalized(void);
const char *fake_ota_boot_version(void);        /* hal_ota_set_boot argument, "" none */
int fake_ota_aborts(void);
bool fake_ota_marked_valid(void);
bool fake_ota_invalidated(void);                /* hal_ota_mark_invalid_and_reboot ran */
void fake_ota_fail_writes(bool on);             /* queued writes report GADGET_EV_OTA_ERROR */

/* ---- battery, log ------------------------------------------------------------ */
void fake_battery_set(bool present, uint8_t pct, bool charging);
bool fake_log_enabled(void);

/* ---- restart ----------------------------------------------------------------- */
/* hal_restart() and hal_ota_mark_invalid_and_reboot() longjmp here. Use:
 *   FAKE_EXPECT_RESTART(fake_run(1000));
 * after which the test may fake_boot() again (storage survives). */
extern jmp_buf fake_restart_jmp;
extern volatile int fake_restart_armed;
int fake_restarts(void);
#define FAKE_EXPECT_RESTART(stmt)                              \
  do {                                                         \
    fake_restart_armed = 1;                                    \
    if (setjmp(fake_restart_jmp) == 0) {                       \
      stmt;                                                    \
      fake_restart_armed = 0;                                  \
      TEST_FAIL_MESSAGE("expected a restart");                 \
    }                                                          \
    fake_restart_armed = 0;                                    \
  } while (0)

#endif /* FAKE_HAL_H */
```

Create `firmware/tests/fake_hal.c`:

```c
/* firmware/tests/fake_hal.c */
/* SPDX-License-Identifier: Apache-2.0 */
#include "fake_hal.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gadget_proto.h"
#include "gadget_util.h"
#include "unity.h"

#define MAX_KEYS 32
#define MAX_LINES 256
#define MAX_FRAMES 1024
#define MAX_BIN 4096
#define MAX_QUEUE 64
#define OTA_MAX (6u * 1024u * 1024u)

typedef struct {
  char key[16];
  bool used, blob;
  uint8_t data[512];
  size_t len;
} kv_t;

typedef struct {
  gadget_event_type_t type;
  uint16_t code;
  uint32_t written;
  gadget_status_t err;
} queued_t;

static struct {
  uint64_t now;
  kv_t kv[MAX_KEYS];
  int storage_writes;
  char *lines[MAX_LINES];
  size_t n_lines;
  bool log_enabled;
  /* wifi */
  gadget_wifi_state_t wifi;
  char wifi_ssid[33];
  int wifi_connects, wifi_scans;
  /* ws */
  int ws_opens;
  char ws_host[64];
  uint16_t ws_port;
  bool ws_live;
  uint16_t ws_close_code;
  char *sent[MAX_FRAMES];
  size_t n_sent;
  uint8_t *bin[MAX_BIN];
  size_t bin_len[MAX_BIN];
  size_t n_bin;
  /* mdns */
  int mdns_browses;
  bool mdns_unsupported;
  /* audio */
  bool mic_running;
  uint32_t spk_rate;
  size_t spk_accepted;
  size_t spk_buffered; /* samples queued but not yet "played" */
  int spk_stops;
  int16_t spk_peak;
  /* ota */
  hal_ota_img_state_t ota_running;
  uint8_t *ota_image;
  uint32_t ota_size, ota_written, ota_durable;
  bool ota_finalized, ota_valid, ota_invalidated, ota_fail_writes;
  char ota_boot_version[GADGET_VERSION_MAX + 1];
  int ota_aborts;
  /* battery */
  bool batt_present, batt_charging;
  uint8_t batt_pct;
  /* async results */
  queued_t queue[MAX_QUEUE];
  size_t n_queue;
  int restarts;
} F;

jmp_buf fake_restart_jmp;
volatile int fake_restart_armed;

static void queue_event(queued_t q) {
  TEST_ASSERT_TRUE_MESSAGE(F.n_queue < MAX_QUEUE, "fake event queue full");
  F.queue[F.n_queue++] = q;
}

static void deliver_queue(void) {
  queued_t q[MAX_QUEUE];
  size_t n = F.n_queue;
  memcpy(q, F.queue, n * sizeof q[0]);
  F.n_queue = 0;
  for (size_t i = 0; i < n; i++) {
    gadget_event_t ev = {.type = q[i].type};
    if (q[i].type == GADGET_EV_WS_CLOSED) {
      if (!F.ws_live) continue;
      F.ws_live = false;
      ev.u.closed.code = q[i].code;
    } else if (q[i].type == GADGET_EV_OTA_WRITTEN) {
      ev.u.ota_written.written = q[i].written;
    } else if (q[i].type == GADGET_EV_OTA_ERROR) {
      ev.u.ota_error.err = q[i].err;
    }
    core_event(&ev);
  }
}

void fake_reset(void) {
  for (size_t i = 0; i < F.n_lines; i++) free(F.lines[i]);
  for (size_t i = 0; i < F.n_sent; i++) free(F.sent[i]);
  for (size_t i = 0; i < F.n_bin; i++) free(F.bin[i]);
  free(F.ota_image);
  memset(&F, 0, sizeof F);
  F.log_enabled = true;
  F.wifi = GADGET_WIFI_CONNECTED;
  F.ota_running = HAL_OTA_IMG_VALID;
}

/* ---- clock ------------------------------------------------------------------ */

uint64_t fake_now(void) { return F.now; }

static void drain_speaker(uint32_t ms) {
  if (F.spk_rate == 0) return;
  size_t played = (size_t)F.spk_rate * ms / 1000u;
  F.spk_buffered = played >= F.spk_buffered ? 0 : F.spk_buffered - played;
}

void fake_run(uint32_t ms) {
  for (uint32_t t = 0; t < ms; t += 10) {
    deliver_queue();
    F.now += 10;
    drain_speaker(10);
    core_tick(F.now);
  }
  deliver_queue();
}

/* ---- boot helpers ------------------------------------------------------------- */

void fake_boot_cfg(const core_config_t *cfg) {
  core_deinit();
  TEST_ASSERT_EQUAL_INT(GADGET_OK, core_init(cfg));
  fake_run(10);
}

void fake_boot(const char *board_id) {
  core_config_t cfg = {.board = gadget_board_by_id(board_id), .fw_version = "1.0.0", .prng_seed = 1};
  TEST_ASSERT_NOT_NULL_MESSAGE(cfg.board, board_id);
  fake_boot_cfg(&cfg);
}

void fake_store_paired(void) {
  uint8_t priv[32];
  size_t n = 0;
  gadget_hex_decode(FAKE_RFC_PRIV_HEX, priv, sizeof priv, &n);
  fake_storage_put_blob(GADGET_KEY_DEV_KEY, priv, sizeof priv);
  fake_storage_put(GADGET_KEY_HOST_ADDR, "127.0.0.1:8810");
  fake_storage_put(GADGET_KEY_HOST_ID, FAKE_HOST_ID);
  fake_storage_put(GADGET_KEY_HOST_NAME, "Mac");
  fake_storage_put(GADGET_KEY_BOT_ID, "b_jev");
  fake_storage_put(GADGET_KEY_BOT_NAME, "Jev");
}

void fake_handshake(void) {
  TEST_ASSERT_TRUE_MESSAGE(fake_ws_live(), "no connection is pending");
  size_t base = fake_ws_sent();
  fake_ws_accept();
  TEST_ASSERT_EQUAL_size_t_MESSAGE(base + 1, fake_ws_sent(), "hello not sent on open");
  cJSON *hello = fake_ws_last("hello");
  TEST_ASSERT_NOT_NULL(hello);
  const char *id = cJSON_GetObjectItem(hello, "id")->valuestring;
  uint8_t pub[80];
  size_t pub_len = 0;
  TEST_ASSERT_EQUAL_INT(GADGET_OK, gadget_b64_decode(cJSON_GetObjectItem(hello, "pubkey")->valuestring, pub,
                                                     sizeof pub, &pub_len));
  TEST_ASSERT_EQUAL_size_t(GADGET_PUBKEY_LEN, pub_len);
  char text[256];
  gp_prove_text(text, sizeof text, id, FAKE_NONCE, FAKE_HOST_ID);
  cJSON_Delete(hello);
  fake_ws_in("{\"op\":\"challenge\",\"nonce\":\"" FAKE_NONCE "\",\"host_id\":\"" FAKE_HOST_ID
             "\",\"host_name\":\"Mac\"}");
  cJSON *prove = fake_ws_last("prove");
  TEST_ASSERT_NOT_NULL_MESSAGE(prove, "prove not sent after challenge");
  uint8_t der[96];
  size_t der_len = 0;
  TEST_ASSERT_EQUAL_INT(GADGET_OK, gadget_b64_decode(cJSON_GetObjectItem(prove, "sig")->valuestring, der,
                                                     sizeof der, &der_len));
  TEST_ASSERT_EQUAL_INT_MESSAGE(GADGET_OK, hal_crypto_verify(pub, (const uint8_t *)text, strlen(text), der, der_len),
                                "prove signature does not verify");
  cJSON_Delete(prove);
  fake_ws_in("{\"op\":\"ready\",\"session\":\"s_0123456789ab\",\"bot\":{\"id\":\"b_jev\",\"name\":\"Jev\"},"
             "\"settings\":{\"speak_pushes\":false}}");
  fake_run(10);
  fake_ws_clear();
}

void fake_ready(const char *board_id) {
  fake_store_paired();
  fake_boot(board_id);
  fake_run(10);
  fake_handshake();
}

/* ---- storage ------------------------------------------------------------------ */

static kv_t *kv_find(const char *key) {
  for (int i = 0; i < MAX_KEYS; i++) {
    if (F.kv[i].used && strcmp(F.kv[i].key, key) == 0) return &F.kv[i];
  }
  return NULL;
}

static kv_t *kv_put(const char *key) {
  kv_t *e = kv_find(key);
  if (e) return e;
  for (int i = 0; i < MAX_KEYS; i++) {
    if (!F.kv[i].used) {
      F.kv[i].used = true;
      TEST_ASSERT_TRUE_MESSAGE(strlen(key) <= 15, key);
      strcpy(F.kv[i].key, key);
      return &F.kv[i];
    }
  }
  TEST_FAIL_MESSAGE("fake storage full");
  return NULL;
}

void fake_storage_put(const char *key, const char *value) {
  kv_t *e = kv_put(key);
  e->blob = false;
  e->len = strlen(value);
  memcpy(e->data, value, e->len + 1);
}

void fake_storage_put_blob(const char *key, const void *data, size_t len) {
  kv_t *e = kv_put(key);
  e->blob = true;
  e->len = len;
  memcpy(e->data, data, len);
}

const char *fake_storage_str(const char *key) {
  kv_t *e = kv_find(key);
  return (e && !e->blob) ? (const char *)e->data : NULL;
}

bool fake_storage_has(const char *key) { return kv_find(key) != NULL; }
size_t fake_storage_blob_len(const char *key) {
  kv_t *e = kv_find(key);
  return (e && e->blob) ? e->len : 0;
}
int fake_storage_writes(void) { return F.storage_writes; }

gadget_status_t hal_storage_get_str(const char *key, char *buf, size_t cap) {
  kv_t *e = kv_find(key);
  if (e == NULL || e->blob) return GADGET_ERR_NOT_FOUND;
  if (e->len + 1 > cap) return GADGET_ERR_LIMIT;
  memcpy(buf, e->data, e->len + 1);
  return GADGET_OK;
}

gadget_status_t hal_storage_set_str(const char *key, const char *value) {
  if (strlen(value) >= sizeof F.kv[0].data) return GADGET_ERR_LIMIT;
  F.storage_writes++;
  fake_storage_put(key, value);
  return GADGET_OK;
}

gadget_status_t hal_storage_get_blob(const char *key, void *buf, size_t cap, size_t *len) {
  kv_t *e = kv_find(key);
  if (e == NULL || !e->blob) return GADGET_ERR_NOT_FOUND;
  if (e->len > cap) return GADGET_ERR_LIMIT;
  memcpy(buf, e->data, e->len);
  *len = e->len;
  return GADGET_OK;
}

gadget_status_t hal_storage_set_blob(const char *key, const void *data, size_t len) {
  if (len > sizeof F.kv[0].data) return GADGET_ERR_LIMIT;
  F.storage_writes++;
  fake_storage_put_blob(key, data, len);
  return GADGET_OK;
}

gadget_status_t hal_storage_erase(const char *key) {
  kv_t *e = kv_find(key);
  if (e) memset(e, 0, sizeof *e);
  return GADGET_OK;
}

gadget_status_t hal_storage_erase_all(void) {
  memset(F.kv, 0, sizeof F.kv);
  return GADGET_OK;
}

/* ---- console -------------------------------------------------------------------- */

void hal_console_write(const char *line) {
  TEST_ASSERT_TRUE_MESSAGE(F.n_lines < MAX_LINES, "fake console full");
  F.lines[F.n_lines] = malloc(strlen(line) + 1);
  strcpy(F.lines[F.n_lines++], line);
}

size_t fake_console_count(void) { return F.n_lines; }
const char *fake_console_line(size_t i) { return i < F.n_lines ? F.lines[i] : NULL; }

cJSON *fake_omb(const char *op) {
  for (size_t i = F.n_lines; i-- > 0;) {
    if (strncmp(F.lines[i], "@omb ", 5) != 0) continue;
    cJSON *j = cJSON_Parse(F.lines[i] + 5);
    const cJSON *o = cJSON_GetObjectItem(j, "op");
    if (cJSON_IsString(o) && strcmp(o->valuestring, op) == 0) return j;
    cJSON_Delete(j);
  }
  return NULL;
}

void fake_console_clear(void) {
  for (size_t i = 0; i < F.n_lines; i++) free(F.lines[i]);
  F.n_lines = 0;
}

void fake_console_in(const char *line) {
  gadget_event_t ev = {.type = GADGET_EV_CONSOLE_LINE};
  ev.u.console.line = line;
  core_event(&ev);
}

/* ---- input, mic, speaker ------------------------------------------------------------ */

void fake_input(gadget_input_type_t type, int16_t x, int16_t y) {
  gadget_event_t ev = {.type = GADGET_EV_INPUT};
  ev.u.input.type = type;
  ev.u.input.x = x;
  ev.u.input.y = y;
  core_event(&ev);
}

void fake_swipe(gadget_swipe_dir_t dir) {
  gadget_event_t ev = {.type = GADGET_EV_INPUT};
  ev.u.input.type = GADGET_IN_SWIPE;
  ev.u.input.dir = dir;
  core_event(&ev);
}

gadget_status_t hal_mic_start(uint32_t rate) {
  if (rate != GADGET_MIC_RATE) return GADGET_ERR_UNSUPPORTED;
  F.mic_running = true;
  return GADGET_OK;
}

void hal_mic_stop(void) { F.mic_running = false; }
bool fake_mic_running(void) { return F.mic_running; }

void fake_mic_frames(int n, int16_t amplitude) {
  int16_t pcm[GADGET_MIC_FRAME_SAMPLES];
  for (int i = 0; i < (int)GADGET_MIC_FRAME_SAMPLES; i++) pcm[i] = (i / 8) % 2 ? amplitude : (int16_t)-amplitude;
  for (int k = 0; k < n; k++) {
    gadget_event_t ev = {.type = GADGET_EV_MIC_FRAME};
    ev.u.mic.pcm = pcm;
    ev.u.mic.samples = GADGET_MIC_FRAME_SAMPLES;
    core_event(&ev);
    fake_run(20);
  }
}

gadget_status_t hal_spk_open(uint32_t rate) {
  if (rate != 16000 && rate != 24000) return GADGET_ERR_UNSUPPORTED;
  if (rate != F.spk_rate) F.spk_buffered = 0;
  F.spk_rate = rate;
  return GADGET_OK;
}

size_t hal_spk_write(const int16_t *pcm, size_t samples) {
  if (F.spk_rate == 0) return 0;
  size_t cap = F.spk_rate / 5; /* a 200 ms output buffer */
  size_t room = F.spk_buffered >= cap ? 0 : cap - F.spk_buffered;
  size_t n = samples < room ? samples : room;
  for (size_t i = 0; i < n; i++) {
    int16_t v = pcm[i] < 0 ? (int16_t)-pcm[i] : pcm[i];
    if (v > F.spk_peak) F.spk_peak = v;
  }
  F.spk_buffered += n;
  F.spk_accepted += n;
  return n;
}

uint32_t hal_spk_buffered_ms(void) { return F.spk_rate ? (uint32_t)(F.spk_buffered * 1000u / F.spk_rate) : 0; }
void hal_spk_stop(void) {
  F.spk_buffered = 0;
  F.spk_stops++;
}
void hal_spk_set_volume(uint8_t pct) { (void)pct; }
uint32_t fake_spk_rate(void) { return F.spk_rate; }
size_t fake_spk_accepted(void) { return F.spk_accepted; }
int fake_spk_stops(void) { return F.spk_stops; }
int16_t fake_spk_peak(void) { return F.spk_peak; }

/* ---- Wi-Fi --------------------------------------------------------------------------- */

gadget_status_t hal_wifi_connect(const char *ssid, const char *password) {
  (void)password;
  snprintf(F.wifi_ssid, sizeof F.wifi_ssid, "%s", ssid);
  F.wifi_connects++;
  /* gadget_hal.h: CONNECTING (or CONNECTED) before this returns; the result
   * event comes from fake_wifi_set() */
  if (F.wifi != GADGET_WIFI_CONNECTED) F.wifi = GADGET_WIFI_CONNECTING;
  return GADGET_OK;
}

void hal_wifi_disconnect(void) { F.wifi = GADGET_WIFI_OFF; }
gadget_wifi_state_t hal_wifi_state(void) { return F.wifi; }

gadget_status_t hal_wifi_scan(void) {
  F.wifi_scans++;
  return GADGET_OK;
}

void fake_wifi_set(gadget_wifi_state_t st) {
  F.wifi = st;
  gadget_event_t ev = {.type = GADGET_EV_WIFI_STATE};
  ev.u.wifi.state = st;
  if (st == GADGET_WIFI_CONNECTED) strcpy(ev.u.wifi.ip, "192.168.1.50");
  core_event(&ev);
}

const char *fake_wifi_ssid(void) { return F.wifi_ssid; }
int fake_wifi_connects(void) { return F.wifi_connects; }
int fake_wifi_scans(void) { return F.wifi_scans; }

void fake_wifi_scan_result(const gadget_wifi_ap_t *aps, uint8_t count, bool ok) {
  gadget_event_t ev = {.type = GADGET_EV_WIFI_SCAN};
  ev.u.scan.aps = aps;
  ev.u.scan.count = count;
  ev.u.scan.ok = ok;
  core_event(&ev);
}

/* ---- WebSocket ------------------------------------------------------------------------- */

gadget_status_t hal_ws_open(const char *host, uint16_t port) {
  if (F.ws_live) return GADGET_ERR_BUSY;
  F.ws_opens++;
  snprintf(F.ws_host, sizeof F.ws_host, "%s", host);
  F.ws_port = port;
  F.ws_live = true;
  F.ws_close_code = 0;
  return GADGET_OK;
}

gadget_status_t hal_ws_send_text(const char *data, size_t len) {
  if (!F.ws_live) return GADGET_ERR_BUSY;
  TEST_ASSERT_TRUE_MESSAGE(len <= GADGET_TEXT_FRAME_MAX, "text frame over 16 KiB");
  TEST_ASSERT_TRUE_MESSAGE(F.n_sent < MAX_FRAMES, "fake sent log full");
  F.sent[F.n_sent] = malloc(len + 1);
  memcpy(F.sent[F.n_sent], data, len);
  F.sent[F.n_sent++][len] = '\0';
  return GADGET_OK;
}

gadget_status_t hal_ws_send_binary(const uint8_t *data, size_t len) {
  if (!F.ws_live) return GADGET_ERR_BUSY;
  TEST_ASSERT_TRUE_MESSAGE(len <= GADGET_BINARY_FRAME_MAX, "binary frame over 8 KiB");
  TEST_ASSERT_TRUE_MESSAGE(F.n_bin < MAX_BIN, "fake binary log full");
  F.bin[F.n_bin] = malloc(len);
  memcpy(F.bin[F.n_bin], data, len);
  F.bin_len[F.n_bin++] = len;
  return GADGET_OK;
}

void hal_ws_close(uint16_t code) {
  if (!F.ws_live) return;
  F.ws_close_code = code ? code : 1000;
  queue_event((queued_t){.type = GADGET_EV_WS_CLOSED, .code = code});
}

int fake_ws_opens(void) { return F.ws_opens; }
const char *fake_ws_host(void) { return F.ws_host; }
uint16_t fake_ws_port(void) { return F.ws_port; }
bool fake_ws_live(void) { return F.ws_live; }
uint16_t fake_ws_close_code(void) { return F.ws_close_code; }

void fake_ws_accept(void) {
  gadget_event_t ev = {.type = GADGET_EV_WS_OPEN};
  core_event(&ev);
}

void fake_ws_in(const char *json) {
  gadget_event_t ev = {.type = GADGET_EV_WS_TEXT};
  ev.u.ws.data = (const uint8_t *)json;
  ev.u.ws.len = strlen(json);
  core_event(&ev);
}

void fake_ws_bin_in(const uint8_t *data, size_t len) {
  gadget_event_t ev = {.type = GADGET_EV_WS_BINARY};
  ev.u.ws.data = data;
  ev.u.ws.len = len;
  core_event(&ev);
}

void fake_ws_ping_in(void) {
  gadget_event_t ev = {.type = GADGET_EV_WS_CONTROL};
  core_event(&ev);
}

void fake_ws_drop(uint16_t code) {
  if (!F.ws_live) return;
  F.ws_live = false;
  gadget_event_t ev = {.type = GADGET_EV_WS_CLOSED};
  ev.u.closed.code = code;
  core_event(&ev);
}

size_t fake_ws_sent(void) { return F.n_sent; }
const char *fake_ws_text(size_t i) { return i < F.n_sent ? F.sent[i] : NULL; }

static bool frame_has_op(const char *frame, const char *op) {
  cJSON *j = cJSON_Parse(frame);
  const cJSON *o = cJSON_GetObjectItem(j, "op");
  bool hit = cJSON_IsString(o) && strcmp(o->valuestring, op) == 0;
  cJSON_Delete(j);
  return hit;
}

size_t fake_ws_count(const char *op) {
  size_t n = 0;
  for (size_t i = 0; i < F.n_sent; i++) n += frame_has_op(F.sent[i], op);
  return n;
}

cJSON *fake_ws_last(const char *op) {
  for (size_t i = F.n_sent; i-- > 0;) {
    if (frame_has_op(F.sent[i], op)) return cJSON_Parse(F.sent[i]);
  }
  return NULL;
}

size_t fake_ws_bin_sent(void) { return F.n_bin; }
const uint8_t *fake_ws_bin(size_t i, size_t *len) {
  if (i >= F.n_bin) return NULL;
  *len = F.bin_len[i];
  return F.bin[i];
}

void fake_ws_clear(void) {
  for (size_t i = 0; i < F.n_sent; i++) free(F.sent[i]);
  for (size_t i = 0; i < F.n_bin; i++) free(F.bin[i]);
  F.n_sent = 0;
  F.n_bin = 0;
}

/* ---- mDNS -------------------------------------------------------------------------------- */

gadget_status_t hal_mdns_browse(uint32_t timeout_ms) {
  (void)timeout_ms;
  if (F.mdns_unsupported) return GADGET_ERR_UNSUPPORTED;
  F.mdns_browses++;
  return GADGET_OK;
}

int fake_mdns_browses(void) { return F.mdns_browses; }
void fake_mdns_unsupported(bool on) { F.mdns_unsupported = on; }

void fake_mdns_result(const gadget_mdns_host_t *hosts, uint8_t count) {
  gadget_event_t ev = {.type = GADGET_EV_MDNS};
  ev.u.mdns.hosts = hosts;
  ev.u.mdns.count = count;
  ev.u.mdns.ok = true;
  core_event(&ev);
}

/* ---- OTA ---------------------------------------------------------------------------------- */

gadget_status_t hal_ota_begin(uint32_t size) {
  if (size == 0 || size > OTA_MAX) return GADGET_ERR_LIMIT;
  free(F.ota_image);
  F.ota_image = calloc(1, size);
  F.ota_size = size;
  F.ota_written = F.ota_durable = 0;
  F.ota_finalized = false;
  return GADGET_OK;
}

gadget_status_t hal_ota_write(uint32_t offset, const uint8_t *data, size_t len) {
  if (F.ota_image == NULL || offset != F.ota_written || offset + len > F.ota_size) return GADGET_ERR_STATE;
  memcpy(F.ota_image + offset, data, len);
  F.ota_written += (uint32_t)len;
  if (F.ota_fail_writes) {
    queue_event((queued_t){.type = GADGET_EV_OTA_ERROR, .err = GADGET_ERR_IO});
  } else {
    queue_event((queued_t){.type = GADGET_EV_OTA_WRITTEN, .written = F.ota_written});
  }
  return GADGET_OK;
}

gadget_status_t hal_ota_finalize(void) {
  if (F.ota_image == NULL || F.ota_written != F.ota_size) return GADGET_ERR_STATE;
  F.ota_finalized = true;
  return GADGET_OK;
}

gadget_status_t hal_ota_set_boot(const char *version) {
  if (!F.ota_finalized) return GADGET_ERR_STATE;
  snprintf(F.ota_boot_version, sizeof F.ota_boot_version, "%s", version);
  return GADGET_OK;
}

void hal_ota_abort(void) {
  F.ota_aborts++;
  free(F.ota_image);
  F.ota_image = NULL;
  F.ota_size = F.ota_written = 0;
}

hal_ota_img_state_t hal_ota_running_state(void) { return F.ota_running; }

gadget_status_t hal_ota_mark_valid(void) {
  F.ota_valid = true;
  F.ota_running = HAL_OTA_IMG_VALID;
  return GADGET_OK;
}

void fake_ota_set_running(hal_ota_img_state_t st) { F.ota_running = st; }
uint32_t fake_ota_size(void) { return F.ota_size; }
uint32_t fake_ota_written(void) { return F.ota_written; }
const uint8_t *fake_ota_image(void) { return F.ota_image; }
bool fake_ota_finalized(void) { return F.ota_finalized; }
const char *fake_ota_boot_version(void) { return F.ota_boot_version; }
int fake_ota_aborts(void) { return F.ota_aborts; }
bool fake_ota_marked_valid(void) { return F.ota_valid; }
bool fake_ota_invalidated(void) { return F.ota_invalidated; }
void fake_ota_fail_writes(bool on) { F.ota_fail_writes = on; }

/* ---- battery, system ------------------------------------------------------------------------ */

bool hal_battery_read(gadget_battery_t *out) {
  if (!F.batt_present) return false;
  out->pct = F.batt_pct;
  out->charging = F.batt_charging;
  return true;
}

void fake_battery_set(bool present, uint8_t pct, bool charging) {
  F.batt_present = present;
  F.batt_pct = pct;
  F.batt_charging = charging;
}

uint64_t hal_now_ms(void) { return F.now; }

static _Noreturn void jump_restart(void) {
  F.restarts++;
  if (!fake_restart_armed) {
    fprintf(stderr, "fake_hal: unexpected restart\n");
    abort();
  }
  F.ws_live = false;
  F.n_queue = 0;
  longjmp(fake_restart_jmp, 1);
}

_Noreturn void hal_restart(void) { jump_restart(); }

_Noreturn void hal_ota_mark_invalid_and_reboot(void) {
  F.ota_invalidated = true;
  jump_restart();
}

int fake_restarts(void) { return F.restarts; }

void hal_vlog(gadget_log_level_t level, const char *tag, const char *fmt, va_list ap) {
  if (!F.log_enabled || getenv("FAKE_HAL_LOG") == NULL) return;
  fprintf(stderr, "[%d] %s: ", (int)level, tag);
  vfprintf(stderr, fmt, ap);
  fputc('\n', stderr);
}

void hal_log(gadget_log_level_t level, const char *tag, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  hal_vlog(level, tag, fmt, ap);
  va_end(ap);
}

void hal_log_set_enabled(bool enabled) { F.log_enabled = enabled; }
bool fake_log_enabled(void) { return F.log_enabled; }
```

Create `firmware/tests/test_core_init.c`:

```c
/* firmware/tests/test_core_init.c */
/* SPDX-License-Identifier: Apache-2.0 */
/* core_init(): identity, storage, the @omb boot line, the model. */
#include <string.h>
#include "fake_hal.h"
#include "gadget_util.h"
#include "psa/crypto.h"
#include "unity.h"

void setUp(void) { fake_reset(); }
void tearDown(void) { core_deinit(); }

static void test_first_boot_generates_and_stores_a_key(void) {
  fake_boot("amoled-175c");
  TEST_ASSERT_EQUAL_size_t(32, fake_storage_blob_len(GADGET_KEY_DEV_KEY));
  const char *id = core_device_id();
  TEST_ASSERT_EQUAL_size_t(GADGET_ID_LEN, strlen(id));
  TEST_ASSERT_EQUAL_MEMORY("gad_", id, 4);
  cJSON *boot = fake_omb("boot");
  TEST_ASSERT_NOT_NULL(boot);
  TEST_ASSERT_EQUAL_STRING("amoled-175c", cJSON_GetObjectItem(boot, "board")->valuestring);
  TEST_ASSERT_EQUAL_STRING("1.0.0", cJSON_GetObjectItem(boot, "fw")->valuestring);
  TEST_ASSERT_EQUAL_STRING(id, cJSON_GetObjectItem(boot, "id")->valuestring);
  cJSON_Delete(boot);
  char line[96];
  snprintf(line, sizeof line, "@omb {\"op\":\"boot\",\"board\":\"amoled-175c\",\"fw\":\"1.0.0\",\"id\":\"%s\"}", id);
  TEST_ASSERT_EQUAL_STRING(line, fake_console_line(0));
}

static void test_reboot_keeps_the_identity(void) {
  fake_boot("lcd-154");
  char first[GADGET_ID_LEN + 1];
  strcpy(first, core_device_id());
  int writes = fake_storage_writes();
  fake_boot("lcd-154");
  TEST_ASSERT_EQUAL_STRING(first, core_device_id());
  TEST_ASSERT_EQUAL_INT(writes, fake_storage_writes()); /* nothing new written */
}

static void test_stored_key_gives_the_contract_id(void) {
  uint8_t priv[32];
  size_t n = 0;
  gadget_hex_decode(FAKE_RFC_PRIV_HEX, priv, sizeof priv, &n);
  fake_storage_put_blob(GADGET_KEY_DEV_KEY, priv, sizeof priv);
  fake_boot("amoled-175c");
  TEST_ASSERT_EQUAL_STRING(FAKE_RFC_ID, core_device_id());
  TEST_ASSERT_EQUAL_STRING(FAKE_RFC_ID, core_ui_model()->device_id);
}

static void test_unusable_key_is_replaced(void) {
  fake_storage_put_blob(GADGET_KEY_DEV_KEY, "short", 5);
  fake_boot("amoled-175c");
  TEST_ASSERT_EQUAL_size_t(32, fake_storage_blob_len(GADGET_KEY_DEV_KEY));
}

static void test_names(void) {
  fake_store_paired();
  fake_boot("amoled-175c");
  TEST_ASSERT_EQUAL_STRING("Maus b18b", core_ui_model()->device_name); /* "Maus " + 4 hex of the id */
  TEST_ASSERT_EQUAL_STRING("Jev", core_ui_model()->bot_name);
  TEST_ASSERT_EQUAL_STRING("Mac", core_ui_model()->host_name);
  core_config_t cfg = {.board = gadget_board_by_id("amoled-175c"), .fw_version = "1.0.0", .default_name = "Kitchen"};
  fake_boot_cfg(&cfg);
  TEST_ASSERT_EQUAL_STRING("Kitchen", core_ui_model()->device_name);
  fake_storage_put(GADGET_KEY_NAME, "Desk Maus");
  fake_boot_cfg(&cfg);
  TEST_ASSERT_EQUAL_STRING("Desk Maus", core_ui_model()->device_name);
  /* spec §4.3: a name has at most 32 characters, the last one "…" when cut */
  fake_storage_put(GADGET_KEY_NAME, "");
  cfg.default_name = "Kitchen counter Maus by the window, left"; /* 40 */
  fake_boot_cfg(&cfg);
  TEST_ASSERT_EQUAL_STRING("Kitchen counter Maus by the win\xE2\x80\xA6", core_ui_model()->device_name);
  TEST_ASSERT_EQUAL_size_t(32, gadget_utf8_len(core_ui_model()->device_name));
  static char wide[2 * 40 + 1];
  for (int i = 0; i < 40; i++) memcpy(wide + 2 * i, "\xC3\xA9", 2); /* 40 x U+00E9 */
  cfg.default_name = wide;
  fake_boot_cfg(&cfg);
  TEST_ASSERT_EQUAL_size_t(32, gadget_utf8_len(core_ui_model()->device_name));
}

static void test_stored_wifi_is_joined_at_boot(void) {
  fake_storage_put(GADGET_KEY_WIFI_SSID, "Home");
  fake_storage_put(GADGET_KEY_WIFI_PASS, "secret123");
  fake_boot("lcd-154");
  TEST_ASSERT_EQUAL_INT(1, fake_wifi_connects());
  TEST_ASSERT_EQUAL_STRING("Home", fake_wifi_ssid());
}

static void test_bad_config_is_refused(void) {
  core_config_t cfg = {.board = NULL, .fw_version = "1.0.0"};
  TEST_ASSERT_EQUAL_INT(GADGET_ERR_ARG, core_init(&cfg));
  cfg.board = gadget_board_by_id("devkit");
  cfg.fw_version = "123456789012345678901234567890123"; /* 33 bytes */
  TEST_ASSERT_EQUAL_INT(GADGET_ERR_ARG, core_init(&cfg));
  TEST_ASSERT_EQUAL_INT(GADGET_ERR_ARG, core_init(NULL));
}

static void test_rev_changes_only_with_the_model(void) {
  fake_boot("devkit");
  uint32_t rev = core_ui_model()->rev;
  fake_run(500);
  TEST_ASSERT_EQUAL_UINT32(rev, core_ui_model()->rev); /* time alone is not a change */
  TEST_ASSERT_EQUAL_UINT64(fake_now(), core_ui_model()->now_ms);
  TEST_ASSERT_EQUAL_STRING("1.0.0", core_fw_version());
}

int main(void) {
  if (psa_crypto_init() != PSA_SUCCESS) return 3;
  UNITY_BEGIN();
  RUN_TEST(test_first_boot_generates_and_stores_a_key);
  RUN_TEST(test_reboot_keeps_the_identity);
  RUN_TEST(test_stored_key_gives_the_contract_id);
  RUN_TEST(test_unusable_key_is_replaced);
  RUN_TEST(test_names);
  RUN_TEST(test_stored_wifi_is_joined_at_boot);
  RUN_TEST(test_bad_config_is_refused);
  RUN_TEST(test_rev_changes_only_with_the_model);
  return UNITY_END();
}
```

`gadget_unit_test(area)` links the fake HAL; `gadget_pure_test` stays for the pure tests.

Change `firmware/tests/CMakeLists.txt` exactly as this diff shows (`-` lines go, `+` lines come; the rest is context):

```diff
--- a/firmware/tests/CMakeLists.txt
+++ b/firmware/tests/CMakeLists.txt
@@ -11,11 +11,21 @@ function(gadget_pure_test area)
   set_tests_properties(core.${area} PROPERTIES LABELS unit TIMEOUT 60)
 endfunction()
 
+# A test of core behind the fake HAL (fake_hal.c): test_<area>.c -> core.<area>.
+function(gadget_unit_test area)
+  add_executable(test_${area} test_${area}.c fake_hal.c)
+  target_link_libraries(test_${area} PRIVATE gadget_core unity::framework)
+  gadget_warnings(test_${area})
+  add_test(NAME core.${area} COMMAND test_${area} ${GADGET_VECTORS_DIR})
+  set_tests_properties(core.${area} PROPERTIES LABELS unit TIMEOUT 60)
+endfunction()
+
 gadget_pure_test(util)
 gadget_pure_test(proto)
 gadget_pure_test(crypto)
 gadget_pure_test(layout)
 gadget_pure_test(console_parse)
+gadget_unit_test(core_init)
 
 # protocol/vectors in C (label vectors).
 add_executable(test_vectors test_vectors.c)
```

- [ ] **Step 2: Run it to see it fail**

Run: `cmake --build build/host -j10`
Expected: the link of `test_core_init` fails on `core_init`, `core_event`, `core_tick`, `core_deinit`, `core_ui_model`, `core_device_id` and `core_fw_version`.

- [ ] **Step 3: Write the implementation**

Create `firmware/core/src/core_internal.h`:

```c
/* firmware/core/src/core_internal.h */
/* SPDX-License-Identifier: Apache-2.0 */
/* Private to core: the shared state and the calls between core's modules.
 * Nothing outside firmware/core/src includes this file. */
#ifndef CORE_INTERNAL_H
#define CORE_INTERNAL_H

#include "cJSON.h"
#include "gadget_core.h"
#include "gadget_hal.h"
#include "gadget_proto.h"
#include "gadget_util.h"

#define CORE_TAG "core"
#define CORE_HOST_ADDR_MAX 64   /* "addr:port" */
#define CORE_WIFI_PASS_MAX 65

typedef struct {
  const gadget_board_t *board;
  core_config_t cfg;                  /* pointers borrowed from the port */
  char fw[GADGET_VERSION_MAX + 1];
  uint8_t priv[GADGET_PRIVKEY_LEN];
  uint8_t pub[GADGET_PUBKEY_LEN];
  char pub_b64[GADGET_PUBKEY_B64_LEN + 1];
  char id[GADGET_ID_LEN + 1];
  char name[UI_NAME_MAX];             /* stored name or the default */
  char wifi_ssid[33];
  char wifi_pass[CORE_WIFI_PASS_MAX];
  char host_id[GADGET_HOST_ID_LEN + 1];   /* "" until the first ready */
  char host_name[UI_NAME_MAX];
  char host_addr[CORE_HOST_ADDR_MAX];     /* "" = host auto */
  char pair_code[GADGET_PAIR_CODE_LEN + 1];
  char bot_id[UI_ID_MAX];
  char bot_name[UI_NAME_MAX];
  bool speak_pushes;
  gadget_prng_t prng;
  char turn_prefix[10];               /* "t" + 8 lowercase hex, chosen at boot */
  uint32_t turn_counter;
  uint64_t now;                       /* the last core_tick() argument */
  bool ticked;                        /* core_tick() ran at least once */
  bool initialized;
  /* Screen inputs: each module sets its own flags; screens.c reads them. */
  struct {
    bool recording;       /* interaction: a recording is live (Listening) */
    bool turn_active;     /* interaction: a turn is in flight and its reply is empty (Thinking) */
    bool speaking;        /* audio: the current turn's speech is playing (Speaking) */
    bool reply_visible;   /* interaction: reply text or a failed done to show (Reply) */
    bool ask_visible;     /* display: an ask is open (Ask) */
    bool card_visible;    /* display: a card is up (Card) */
    bool image_visible;   /* display: an image is up (Image) */
    bool ota_active;      /* ota: receiving, verifying or restarting (Update) */
    bool toast_visible;   /* display: a post toast is up */
  } f;
  ui_model_t model;
} core_t;

extern core_t g_core;

/* ---- core.c --------------------------------------------------------------- */
/* Storage writes that log on failure (the in-memory copy is the truth). */
void core_store_str(const char *key, const char *value);
void core_store_erase(const char *key);
/* gadget_utf8_copy / _tail, then every code point outside the gadget
 * charset (Latin-1 plus U+2026, U+2192) becomes '?'. */
size_t core_text_copy(char *dst, size_t cap, const char *src);
size_t core_text_copy_tail(char *dst, size_t cap, const char *src);
/* g_core.name = src through core_text_copy, cut to GADGET_NAME_MAX code points
 * (spec §4.3), the last one "…" when cut. */
void core_set_name(const char *src);
/* "t3f9a0c2b-7": the boot prefix plus a counter starting at 1. */
void core_next_turn_id(char out[GADGET_TURN_MAX + 1]);
/* Print "@omb " + the compact JSON of obj on the console; frees obj. */
void core_omb(cJSON *obj);

#endif /* CORE_INTERNAL_H */
```

Create `firmware/core/src/core.c`:

```c
/* firmware/core/src/core.c */
/* SPDX-License-Identifier: Apache-2.0 */
/* Core entry points (gadget_core.h): init, events, ticks, the ui_model and
 * introspection. The modules (session, interaction, audio, display, ota,
 * actions, console commands) are wired in here. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "core_internal.h"

core_t g_core;

static core_tap_fn s_tap;
static void *s_tap_ctx;
static ui_model_t s_shadow; /* the model as of the last rev bump */

/* ---- storage ---------------------------------------------------------------- */

void core_store_str(const char *key, const char *value) {
  gadget_status_t st = hal_storage_set_str(key, value);
  if (st != GADGET_OK) hal_log(GADGET_LOG_ERROR, CORE_TAG, "storage set %s failed: %d", key, (int)st);
}

void core_store_erase(const char *key) {
  gadget_status_t st = hal_storage_erase(key);
  if (st != GADGET_OK) hal_log(GADGET_LOG_ERROR, CORE_TAG, "storage erase %s failed: %d", key, (int)st);
}

static void load_str(const char *key, char *buf, size_t cap) {
  if (hal_storage_get_str(key, buf, cap) != GADGET_OK) buf[0] = '\0';
}

/* ---- text ----------------------------------------------------------------------- */

/* Replace code points outside Latin-1 + U+2026 + U+2192 (and invalid bytes)
 * with '?'. The result is never longer than the input. */
static void fold_charset(char *s) {
  unsigned char *r = (unsigned char *)s, *w = (unsigned char *)s;
  while (*r) {
    unsigned char c = *r;
    if (c < 0x80) {
      *w++ = (c < 0x20 && c != '\n') ? ' ' : c;
      r++;
    } else if (c >= 0xC2 && c <= 0xC3 && (r[1] & 0xC0) == 0x80) {
      *w++ = r[0]; /* U+0080..U+00FF */
      *w++ = r[1];
      r += 2;
    } else if (c == 0xE2 && r[1] == 0x80 && r[2] == 0xA6) {
      memmove(w, r, 3); /* U+2026 */
      w += 3;
      r += 3;
    } else if (c == 0xE2 && r[1] == 0x86 && r[2] == 0x92) {
      memmove(w, r, 3); /* U+2192 */
      w += 3;
      r += 3;
    } else {
      size_t n = 1;
      if (c >= 0xC0 && c < 0xE0) n = 2;
      else if (c >= 0xE0 && c < 0xF0) n = 3;
      else if (c >= 0xF0 && c < 0xF8) n = 4;
      for (size_t i = 1; i < n; i++) {
        if ((r[i] & 0xC0) != 0x80) {
          n = i;
          break;
        }
      }
      *w++ = '?';
      r += n;
    }
  }
  *w = '\0';
}

size_t core_text_copy(char *dst, size_t cap, const char *src) {
  gadget_utf8_copy(dst, cap, src);
  fold_charset(dst);
  return strlen(dst);
}

size_t core_text_copy_tail(char *dst, size_t cap, const char *src) {
  gadget_utf8_copy_tail(dst, cap, src);
  fold_charset(dst);
  return strlen(dst);
}

void core_set_name(const char *src) {
  char tmp[UI_NAME_MAX + 4];
  core_text_copy(tmp, UI_NAME_MAX, src);
  size_t cps = 0;
  for (size_t i = 0; tmp[i] != '\0'; i++) {
    if (((unsigned char)tmp[i] & 0xC0u) == 0x80u) continue; /* a continuation byte */
    if (++cps == GADGET_NAME_MAX && gadget_utf8_len(tmp + i) > 1) {
      memcpy(tmp + i, "\xE2\x80\xA6", 4); /* the 32nd character becomes "…" */
      break;
    }
  }
  gadget_utf8_copy(g_core.name, sizeof g_core.name, tmp);
}

void core_next_turn_id(char out[GADGET_TURN_MAX + 1]) {
  snprintf(out, GADGET_TURN_MAX + 1, "%s-%lu", g_core.turn_prefix, (unsigned long)++g_core.turn_counter);
}

void core_omb(cJSON *obj) {
  if (obj == NULL) return;
  char *text = cJSON_PrintUnformatted(obj);
  cJSON_Delete(obj);
  if (text == NULL) return;
  size_t n = strlen(text);
  char *line = malloc(n + 6);
  if (line != NULL) {
    memcpy(line, "@omb ", 5);
    memcpy(line + 5, text, n + 1);
    hal_console_write(line);
    free(line);
  }
  cJSON_free(text);
}

/* ---- identity ------------------------------------------------------------------- */

static gadget_status_t load_identity(void) {
  size_t len = 0;
  bool have = hal_storage_get_blob(GADGET_KEY_DEV_KEY, g_core.priv, sizeof g_core.priv, &len) == GADGET_OK &&
              len == GADGET_PRIVKEY_LEN && hal_crypto_pubkey(g_core.priv, g_core.pub) == GADGET_OK;
  if (!have) {
    /* First boot (or an unusable key): the port started Wi-Fi, so the RNG is truly random. */
    gadget_status_t st = hal_crypto_keygen(g_core.priv, g_core.pub);
    if (st != GADGET_OK) return st;
    st = hal_storage_set_blob(GADGET_KEY_DEV_KEY, g_core.priv, GADGET_PRIVKEY_LEN);
    if (st != GADGET_OK) return st;
    hal_log(GADGET_LOG_INFO, CORE_TAG, "generated a new device key");
  }
  gadget_b64_encode(g_core.pub_b64, sizeof g_core.pub_b64, g_core.pub, GADGET_PUBKEY_LEN);
  return gadget_id_from_pubkey(g_core.pub, g_core.id);
}

static void load_settings(void) {
  char name[UI_NAME_MAX];
  load_str(GADGET_KEY_NAME, name, sizeof name);
  if (name[0] == '\0') {
    if (g_core.cfg.default_name != NULL) {
      snprintf(name, sizeof name, "%s", g_core.cfg.default_name);
    } else {
      snprintf(name, sizeof name, "Maus %.4s", g_core.id + 4);
    }
  }
  core_set_name(name);
  load_str(GADGET_KEY_WIFI_SSID, g_core.wifi_ssid, sizeof g_core.wifi_ssid);
  load_str(GADGET_KEY_WIFI_PASS, g_core.wifi_pass, sizeof g_core.wifi_pass);
  load_str(GADGET_KEY_HOST_ID, g_core.host_id, sizeof g_core.host_id);
  if (!gadget_host_id_valid(g_core.host_id)) g_core.host_id[0] = '\0';
  load_str(GADGET_KEY_HOST_NAME, g_core.host_name, sizeof g_core.host_name);
  load_str(GADGET_KEY_HOST_ADDR, g_core.host_addr, sizeof g_core.host_addr);
  load_str(GADGET_KEY_PAIR_CODE, g_core.pair_code, sizeof g_core.pair_code);
  if (!gadget_pair_code_valid(g_core.pair_code)) g_core.pair_code[0] = '\0';
  load_str(GADGET_KEY_BOT_ID, g_core.bot_id, sizeof g_core.bot_id);
  load_str(GADGET_KEY_BOT_NAME, g_core.bot_name, sizeof g_core.bot_name);
  char flag[4];
  load_str(GADGET_KEY_SPEAK_PUSH, flag, sizeof flag);
  g_core.speak_pushes = strcmp(flag, "1") == 0;
}

/* ---- model ------------------------------------------------------------------------ */

/* Bump rev when anything but rev and now_ms changed since the last bump. */
static void model_commit(void) {
  static ui_model_t tmp;
  memcpy(&tmp, &g_core.model, sizeof tmp);
  tmp.rev = s_shadow.rev;
  tmp.now_ms = s_shadow.now_ms;
  if (memcmp(&tmp, &s_shadow, sizeof tmp) != 0) {
    g_core.model.rev++;
    memcpy(&s_shadow, &g_core.model, sizeof s_shadow);
  }
}

static void publish_identity(void) {
  ui_model_t *m = &g_core.model;
  snprintf(m->device_id, sizeof m->device_id, "%s", g_core.id);
  snprintf(m->device_name, sizeof m->device_name, "%s", g_core.name);
  core_text_copy(m->bot_name, sizeof m->bot_name, g_core.bot_name);
  core_text_copy(m->host_name, sizeof m->host_name, g_core.host_name);
}

/* ---- public API -------------------------------------------------------------------- */

gadget_status_t core_init(const core_config_t *cfg) {
  if (cfg == NULL || cfg->board == NULL || cfg->fw_version == NULL || cfg->fw_version[0] == '\0' ||
      strlen(cfg->fw_version) > GADGET_VERSION_MAX) {
    return GADGET_ERR_ARG;
  }
  memset(&g_core, 0, sizeof g_core);
  memset(&s_shadow, 0, sizeof s_shadow);
  g_core.board = cfg->board;
  g_core.cfg = *cfg;
  snprintf(g_core.fw, sizeof g_core.fw, "%s", cfg->fw_version);
  g_core.model.screen = UI_SCREEN_BOOT;

  gadget_status_t st = load_identity();
  if (st != GADGET_OK) {
    hal_log(GADGET_LOG_ERROR, CORE_TAG, "identity failed: %d", (int)st);
    return st;
  }
  load_settings();

  uint32_t seed = cfg->prng_seed;
  if (seed == 0) hal_crypto_random(&seed, sizeof seed);
  gadget_prng_seed(&g_core.prng, seed);
  uint8_t rnd[4];
  if (hal_crypto_random(rnd, sizeof rnd) != GADGET_OK) memset(rnd, 0, sizeof rnd);
  g_core.turn_prefix[0] = 't';
  gadget_hex_encode(g_core.turn_prefix + 1, rnd, sizeof rnd);

  publish_identity();
  if (g_core.wifi_ssid[0] != '\0') hal_wifi_connect(g_core.wifi_ssid, g_core.wifi_pass);

  cJSON *boot = cJSON_CreateObject();
  cJSON_AddStringToObject(boot, "op", "boot");
  cJSON_AddStringToObject(boot, "board", g_core.board->id);
  cJSON_AddStringToObject(boot, "fw", g_core.fw);
  cJSON_AddStringToObject(boot, "id", g_core.id);
  core_omb(boot);
  hal_log(GADGET_LOG_INFO, CORE_TAG, "%s on %s, fw %s", g_core.id, g_core.board->id, g_core.fw);

  g_core.initialized = true;
  model_commit();
  return GADGET_OK;
}

void core_event(const gadget_event_t *ev) {
  if (!g_core.initialized || ev == NULL) return;
  switch (ev->type) {
    default:
      break;
  }
  model_commit();
}

void core_tick(uint64_t now_ms) {
  if (!g_core.initialized) return;
  g_core.now = now_ms;
  g_core.model.now_ms = now_ms;
  g_core.ticked = true;
  model_commit();
}

const ui_model_t *core_ui_model(void) { return &g_core.model; }

void core_deinit(void) {
  memset(&g_core, 0, sizeof g_core);
  memset(&s_shadow, 0, sizeof s_shadow);
}

const char *core_device_id(void) { return g_core.id; }
const char *core_fw_version(void) { return g_core.fw; }

void core_set_tap(core_tap_fn fn, void *ctx) {
  s_tap = fn;
  s_tap_ctx = ctx;
}
```

Change `firmware/core/CMakeLists.txt` exactly as this diff shows (`-` lines go, `+` lines come; the rest is context):

```diff
--- a/firmware/core/CMakeLists.txt
+++ b/firmware/core/CMakeLists.txt
@@ -9,6 +9,7 @@ set(GADGET_CORE_SRCS
   src/boards.c
   src/ui_layout.c
   src/console.c
+  src/core.c
 )
 
 if(ESP_PLATFORM)
```

- [ ] **Step 4: Run the tests**

Run: `cmake --build build/host -j10 && ctest --test-dir build/host --output-on-failure -L "unit|vectors"`
Expected: `100% tests passed, 0 tests failed out of 7`; `test_core_init` prints `8 Tests 0 Failures 0 Ignored`. Set `FAKE_HAL_LOG=1` to see core's log lines while debugging.

- [ ] **Step 5: Commit**

```bash
git add firmware/core firmware/tests/fake_hal.h firmware/tests/fake_hal.c firmware/tests/test_core_init.c firmware/tests/CMakeLists.txt
git commit -m "firmware: core init, identity, storage and the fake HAL" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 8: Session — host lookup, handshake, errors, backoff, liveness, screens

**Files:**
- Create: `firmware/core/src/session.c`, `firmware/core/src/screens.c`
- Modify: `firmware/core/src/core.c`, `firmware/core/src/core_internal.h`, `firmware/core/CMakeLists.txt`, `firmware/tests/CMakeLists.txt`
- Test: `firmware/tests/test_session.c`

**Interfaces:**
- Consumes: `gp_*` (Task 2), the crypto group (Task 3), `core_internal.h` (Task 7).
- Produces:
  - **Public:** `core_pair_state` and `core_last_error`.
  - **Private, from `session.c`:** `session_init/deinit/event/tick`, `session_ready`, `session_send(op, json, len)` (taps, then sends; it returns an encoder's negative length unchanged), `session_send_binary`, `session_reconnect_now(clear_error)` (for `pair` and `host`), `session_wake` (TALK after `replaced`), `session_pair_state`, `session_last_error`, `session_host_in_use`, `session_is_setup` and `session_clear_host_name` (for `host`, `host auto` and `forget`). Also the shared encode buffer `g_core_tx[GADGET_TEXT_FRAME_MAX]`.
  - **Private, from `screens.c`:** `screens_update`, which applies contract §2.7's order (Update → Listening → Ask → Speaking → Thinking → Reply → Image → Card → Idle). It falls back to Setup when nothing is paired, and to Offline otherwise.
  - **Private hooks in `core.c`:** `core_tap`, `core_on_ready`, `core_on_session_lost`, `core_on_msg` and `core_on_binary`, which Tasks 9–14 extend.
  - **The spec §4.3 reaction table:**
    - `bad_code` clears the code, sets `pair` to `error`, and stops.
    - `enroll_required` and `revoked` clear `host_id`, so the gadget is unpaired, shows Setup, and stops.
    - `device_limit` keeps the code and retries every 10 s until 120 s after the code was given, then clears it. Through that whole window, including while a retry is connecting, `core_pair_state()` is `CORE_PAIR_ERROR` and `core_last_error()` is `"device_limit"`. So `status` prints `"pair":"error","error":"device_limit"` (contract §2.11, status rule 2), and P2d's installer and console helper can say why the gadget is waiting instead of timing out. Once the code is cleared, a gadget that was never paired is `unpaired` and `status` has no `error` field. Test: `test_device_limit_retries_every_10_s_for_120_s` (Task 10 adds the `status` line checks).
    - `replaced` goes Offline with `in_use_elsewhere` and stops until TALK.
    - `proto_unsupported`, `bad_sig`, a bad `host_id` and drops set `error` and reconnect with backoff 2, 4, 8 … 60 s; `ready` resets the backoff.
  - **Liveness and frame limits:** 45 s without an inbound frame (a ping counts) closes the session with 1001. Inbound frames over the limits are dropped (Review Focus 1).
  - **host auto:** browse 5 s, then pick the service whose TXT `id` equals the stored `host_id`, or the only one before pairing. Otherwise print `@omb {"op":"hosts",…}`. A gadget that was never paired then waits for `host <address>` (spec §5.6), or for a new `pair` or `host auto`. A paired gadget keeps looking for its own MausBot with backoff (contract D28). The `hosts` line prints only on the first miss of a run of misses (`ready`, `pair` and `host` start a new run), so P2d's installer asks once. On a port without mDNS, print an empty `hosts` line and wait.
  - **`settings`:** updates the bot, `speak_pushes` and the name (through `core_set_name`, so at most 32 characters; stored; the next `hello` carries it).
  - **The model's `host_name` (spec §5.5: Setup "shows `host_name` once a `challenge` has arrived"):** the last challenge's `host_name` when one has arrived, otherwise the stored one. It outlives the connection that brought it, so on a gadget that was never paired the Setup screens after `bad_code` and `device_limit` still name the host. Only a new challenge, `host`, `host auto` and `forget` drop it (`session_clear_host_name`); a new `pair` keeps it, because the host is the same. The stored `host_name` is still written only on `ready`. Test: `test_setup_keeps_the_challenge_host_name_after_bad_code`.

- [ ] **Step 1: Write the failing test**

Create `firmware/tests/test_session.c`:

```c
/* firmware/tests/test_session.c */
/* SPDX-License-Identifier: Apache-2.0 */
/* The connection state machine (core/src/session.c, spec §4.3) and screen
 * selection without a session (core/src/screens.c). */
#include <string.h>
#include "fake_hal.h"
#include "gadget_util.h"
#include "psa/crypto.h"
#include "unity.h"

void setUp(void) { fake_reset(); }
void tearDown(void) { core_deinit(); }

static const char *READY =
    "{\"op\":\"ready\",\"session\":\"s_0123456789ab\",\"bot\":{\"id\":\"b_jev\",\"name\":\"Jev\"},"
    "\"settings\":{\"speak_pushes\":true}}";
static const char *CHALLENGE =
    "{\"op\":\"challenge\",\"nonce\":\"" FAKE_NONCE "\",\"host_id\":\"" FAKE_HOST_ID "\",\"host_name\":\"Omkar's Mac\"}";

static void store_code_only(void) {
  fake_storage_put(GADGET_KEY_HOST_ADDR, "192.168.1.20:8810");
  fake_storage_put(GADGET_KEY_PAIR_CODE, "123456");
}

/* Open, hello, challenge; returns with the prove sent. */
static void to_prove(void) {
  TEST_ASSERT_TRUE(fake_ws_live());
  fake_ws_accept();
  fake_ws_in(CHALLENGE);
}

static void host_error(const char *code) {
  char json[96];
  snprintf(json, sizeof json, "{\"op\":\"error\",\"code\":\"%s\",\"message\":\"x\"}", code);
  fake_ws_in(json);
  fake_run(10); /* the close core asked for is delivered */
}

static void test_unpaired_gadget_waits_on_setup(void) {
  fake_boot("amoled-175c");
  fake_run(5000);
  TEST_ASSERT_EQUAL_INT(0, fake_ws_opens());
  TEST_ASSERT_EQUAL_INT(CORE_PAIR_UNPAIRED, core_pair_state());
  const ui_model_t *m = core_ui_model();
  TEST_ASSERT_EQUAL_INT(UI_SCREEN_SETUP, m->screen);
  TEST_ASSERT_EQUAL_INT(UI_MAUS_CURIOUS, m->maus);
  TEST_ASSERT_EQUAL_INT(UI_SETUP_NEED_CODE, m->setup.step);
}

static void test_setup_asks_for_wifi_first(void) {
  fake_wifi_set(GADGET_WIFI_OFF);
  fake_boot("lcd-154");
  TEST_ASSERT_EQUAL_INT(UI_SETUP_NEED_WIFI, core_ui_model()->setup.step);
}

static void test_paired_gadget_connects_and_becomes_ready(void) {
  fake_store_paired();
  fake_boot("amoled-175c");
  TEST_ASSERT_EQUAL_INT(1, fake_ws_opens());
  TEST_ASSERT_EQUAL_STRING("127.0.0.1", fake_ws_host());
  TEST_ASSERT_EQUAL_UINT16(8810, fake_ws_port());
  TEST_ASSERT_EQUAL_INT(CORE_PAIR_CONNECTING, core_pair_state());
  TEST_ASSERT_EQUAL_INT(UI_SCREEN_OFFLINE, core_ui_model()->screen);
  TEST_ASSERT_EQUAL_INT(UI_OFFLINE_HOST_LOOKUP, core_ui_model()->offline.reason);
  fake_ws_accept();
  cJSON *hello = fake_ws_last("hello");
  TEST_ASSERT_EQUAL_INT(1, cJSON_GetObjectItem(hello, "proto")->valueint);
  TEST_ASSERT_EQUAL_STRING(FAKE_RFC_ID, cJSON_GetObjectItem(hello, "id")->valuestring);
  TEST_ASSERT_EQUAL_STRING("Maus b18b", cJSON_GetObjectItem(hello, "name")->valuestring);
  TEST_ASSERT_EQUAL_STRING("amoled-175c", cJSON_GetObjectItem(hello, "board")->valuestring);
  TEST_ASSERT_EQUAL_STRING("1.0.0", cJSON_GetObjectItem(hello, "fw")->valuestring);
  TEST_ASSERT_TRUE(cJSON_IsObject(cJSON_GetObjectItem(hello, "caps")));
  cJSON_Delete(hello);
  fake_ws_in(CHALLENGE);
  cJSON *prove = fake_ws_last("prove");
  TEST_ASSERT_NULL(cJSON_GetObjectItem(prove, "enroll")); /* a paired gadget holds no code */
  cJSON_Delete(prove);
  fake_ws_in(READY);
  fake_run(10);
  TEST_ASSERT_EQUAL_INT(CORE_PAIR_PAIRED, core_pair_state());
  TEST_ASSERT_EQUAL_INT(UI_SCREEN_IDLE, core_ui_model()->screen);
  TEST_ASSERT_EQUAL_INT(UI_MAUS_IDLE, core_ui_model()->maus);
  TEST_ASSERT_EQUAL_STRING("Omkar's Mac", core_ui_model()->host_name);
  TEST_ASSERT_EQUAL_STRING("1", fake_storage_str(GADGET_KEY_SPEAK_PUSH));
}

static void test_handshake_helper_verifies_the_prove_signature(void) {
  fake_ready("lcd-154"); /* asserts internally that the signature verifies */
  TEST_ASSERT_EQUAL_INT(CORE_PAIR_PAIRED, core_pair_state());
}

static void test_enrollment_sends_the_code_and_stores_the_host(void) {
  store_code_only();
  fake_boot("lcd-154");
  TEST_ASSERT_EQUAL_INT(CORE_PAIR_CONNECTING, core_pair_state());
  TEST_ASSERT_EQUAL_STRING("192.168.1.20", fake_ws_host());
  to_prove();
  cJSON *prove = fake_ws_last("prove");
  TEST_ASSERT_EQUAL_STRING("123456", cJSON_GetObjectItem(prove, "enroll")->valuestring);
  cJSON_Delete(prove);
  TEST_ASSERT_EQUAL_INT(UI_SCREEN_SETUP, core_ui_model()->screen);
  TEST_ASSERT_EQUAL_INT(UI_SETUP_PAIRING, core_ui_model()->setup.step);
  TEST_ASSERT_EQUAL_STRING("Omkar's Mac", core_ui_model()->host_name); /* shown once a challenge arrived */
  fake_ws_in(READY);
  fake_run(10);
  TEST_ASSERT_EQUAL_STRING(FAKE_HOST_ID, fake_storage_str(GADGET_KEY_HOST_ID));
  TEST_ASSERT_EQUAL_STRING("Omkar's Mac", fake_storage_str(GADGET_KEY_HOST_NAME));
  TEST_ASSERT_FALSE(fake_storage_has(GADGET_KEY_PAIR_CODE));
  TEST_ASSERT_EQUAL_STRING("b_jev", fake_storage_str(GADGET_KEY_BOT_ID));
  TEST_ASSERT_EQUAL_STRING("Jev", fake_storage_str(GADGET_KEY_BOT_NAME));
  TEST_ASSERT_EQUAL_STRING("Jev", core_ui_model()->bot_name);
}

static void test_challenge_with_a_bad_host_id_closes_and_backs_off(void) {
  fake_store_paired();
  fake_boot("amoled-175c");
  fake_ws_accept();
  fake_ws_in("{\"op\":\"challenge\",\"nonce\":\"" FAKE_NONCE "\",\"host_id\":\"h_0123456789abcdef\",\"host_name\":\"x\"}");
  TEST_ASSERT_EQUAL_size_t(0, fake_ws_count("prove"));
  TEST_ASSERT_EQUAL_UINT16(1002, fake_ws_close_code());
  fake_run(10);
  TEST_ASSERT_EQUAL_INT(UI_OFFLINE_PROTOCOL, core_ui_model()->offline.reason);
  TEST_ASSERT_TRUE(core_ui_model()->offline.retry_at_ms > 0);
  fake_run(1970);
  TEST_ASSERT_EQUAL_INT(1, fake_ws_opens());
  fake_run(30);
  TEST_ASSERT_EQUAL_INT(2, fake_ws_opens());
}

static void test_bad_code_clears_the_code_and_stops(void) {
  store_code_only();
  fake_boot("lcd-154");
  to_prove();
  host_error("bad_code");
  TEST_ASSERT_FALSE(fake_storage_has(GADGET_KEY_PAIR_CODE));
  TEST_ASSERT_EQUAL_INT(CORE_PAIR_ERROR, core_pair_state());
  TEST_ASSERT_EQUAL_STRING("bad_code", core_last_error());
  TEST_ASSERT_EQUAL_INT(UI_SETUP_BAD_CODE, core_ui_model()->setup.step);
  fake_run(130000);
  TEST_ASSERT_EQUAL_INT(1, fake_ws_opens()); /* no retry until a new pair command */
}

/* Spec 5.5: Setup shows host_name once a challenge has arrived, also after
 * the connection that brought it has closed. */
static void test_setup_keeps_the_challenge_host_name_after_bad_code(void) {
  store_code_only();
  fake_boot("lcd-154");
  TEST_ASSERT_EQUAL_STRING("", core_ui_model()->host_name); /* no challenge yet */
  fake_ws_accept();
  fake_ws_in("{\"op\":\"challenge\",\"nonce\":\"" FAKE_NONCE "\",\"host_id\":\"" FAKE_HOST_ID "\",\"host_name\":\"Mac\"}");
  host_error("bad_code");
  TEST_ASSERT_FALSE(fake_ws_live()); /* the connection that brought the name is closed */
  TEST_ASSERT_EQUAL_INT(UI_SCREEN_SETUP, core_ui_model()->screen);
  TEST_ASSERT_EQUAL_INT(UI_SETUP_BAD_CODE, core_ui_model()->setup.step);
  TEST_ASSERT_EQUAL_STRING("Mac", core_ui_model()->host_name);
  TEST_ASSERT_FALSE(fake_storage_has(GADGET_KEY_HOST_NAME)); /* stored only on ready */
}

static void test_enroll_required_and_revoked_unpair(void) {
  fake_store_paired();
  fake_boot("amoled-175c");
  to_prove();
  host_error("enroll_required");
  TEST_ASSERT_FALSE(fake_storage_has(GADGET_KEY_HOST_ID));
  TEST_ASSERT_EQUAL_INT(CORE_PAIR_UNPAIRED, core_pair_state());
  TEST_ASSERT_EQUAL_INT(UI_SCREEN_SETUP, core_ui_model()->screen);
  fake_run(70000);
  TEST_ASSERT_EQUAL_INT(1, fake_ws_opens());

  fake_reset();
  fake_ready("amoled-175c");
  fake_ws_in("{\"op\":\"error\",\"code\":\"revoked\"}");
  fake_run(10);
  TEST_ASSERT_FALSE(fake_storage_has(GADGET_KEY_HOST_ID));
  TEST_ASSERT_EQUAL_INT(CORE_PAIR_UNPAIRED, core_pair_state());
  TEST_ASSERT_EQUAL_INT(UI_SCREEN_SETUP, core_ui_model()->screen);
  fake_run(70000);
  TEST_ASSERT_EQUAL_INT(1, fake_ws_opens()); /* no automatic reconnect */
}

static void test_device_limit_retries_every_10_s_for_120_s(void) {
  store_code_only();
  fake_boot("lcd-154");
  to_prove();
  host_error("device_limit");
  TEST_ASSERT_TRUE(fake_storage_has(GADGET_KEY_PAIR_CODE));
  TEST_ASSERT_EQUAL_INT(UI_SETUP_DEVICE_LIMIT, core_ui_model()->setup.step);
  TEST_ASSERT_EQUAL_STRING("Omkar's Mac", core_ui_model()->host_name); /* kept after the close */
  /* pair "error" with "device_limit" for the whole window (contract §2.11), so
   * P2d's installer can say why instead of timing out */
  TEST_ASSERT_EQUAL_INT(CORE_PAIR_ERROR, core_pair_state());
  TEST_ASSERT_EQUAL_STRING("device_limit", core_last_error());
  int opens = fake_ws_opens();
  fake_run(9900);
  TEST_ASSERT_EQUAL_INT(opens, fake_ws_opens());
  fake_run(200);
  TEST_ASSERT_EQUAL_INT(opens + 1, fake_ws_opens());
  TEST_ASSERT_EQUAL_INT(CORE_PAIR_ERROR, core_pair_state()); /* also while a retry connects */
  /* keep answering device_limit until the window closes */
  uint64_t gave_up = 0;
  while (gave_up == 0 && fake_now() < 200000) {
    if (fake_ws_live()) {
      to_prove();
      host_error("device_limit");
    }
    fake_run(100);
    if (!fake_storage_has(GADGET_KEY_PAIR_CODE)) {
      gave_up = fake_now();
    } else {
      TEST_ASSERT_EQUAL_INT(CORE_PAIR_ERROR, core_pair_state());
      TEST_ASSERT_EQUAL_STRING("device_limit", core_last_error());
    }
  }
  TEST_ASSERT_TRUE(gave_up >= 120000 && gave_up <= 120200); /* 120 s after the code was stored */
  TEST_ASSERT_EQUAL_INT(CORE_PAIR_UNPAIRED, core_pair_state());
  opens = fake_ws_opens();
  fake_run(60000);
  TEST_ASSERT_EQUAL_INT(opens, fake_ws_opens());
}

static void test_replaced_shows_in_use_and_stays_offline(void) {
  fake_ready("amoled-175c");
  fake_ws_in("{\"op\":\"error\",\"code\":\"replaced\"}");
  fake_run(10);
  const ui_model_t *m = core_ui_model();
  TEST_ASSERT_EQUAL_INT(UI_SCREEN_OFFLINE, m->screen);
  TEST_ASSERT_EQUAL_INT(UI_MAUS_SLEEPING, m->maus);
  TEST_ASSERT_EQUAL_INT(UI_OFFLINE_IN_USE_ELSEWHERE, m->offline.reason);
  TEST_ASSERT_EQUAL_UINT64(0, m->offline.retry_at_ms);
  fake_run(120000);
  TEST_ASSERT_EQUAL_INT(1, fake_ws_opens());
}

static void test_protocol_errors_back_off_2_4_8_up_to_60_s(void) {
  fake_store_paired();
  fake_boot("amoled-175c");
  to_prove();
  host_error("bad_sig");
  TEST_ASSERT_EQUAL_INT(CORE_PAIR_ERROR, core_pair_state());
  TEST_ASSERT_EQUAL_STRING("bad_sig", core_last_error());
  static const uint32_t gaps[] = {2000, 4000, 8000, 16000, 32000, 60000, 60000};
  for (size_t i = 0; i < sizeof gaps / sizeof gaps[0]; i++) {
    int opens = fake_ws_opens();
    fake_run(gaps[i] - 20);
    TEST_ASSERT_EQUAL_INT_MESSAGE(opens, fake_ws_opens(), "retried too early");
    fake_run(20);
    TEST_ASSERT_EQUAL_INT_MESSAGE(opens + 1, fake_ws_opens(), "did not retry on time");
    fake_ws_drop(0); /* the connect fails */
  }
}

static void test_proto_unsupported_errors_and_backs_off(void) {
  fake_store_paired();
  fake_boot("amoled-175c");
  to_prove();
  host_error("proto_unsupported");
  TEST_ASSERT_EQUAL_INT(CORE_PAIR_ERROR, core_pair_state());
  TEST_ASSERT_EQUAL_STRING("proto_unsupported", core_last_error());
  TEST_ASSERT_EQUAL_INT(UI_OFFLINE_PROTOCOL, core_ui_model()->offline.reason);
  fake_run(1970);
  TEST_ASSERT_EQUAL_INT(1, fake_ws_opens());
  fake_run(30);
  TEST_ASSERT_EQUAL_INT(2, fake_ws_opens());
}

static void test_ready_resets_the_backoff(void) {
  fake_store_paired();
  fake_boot("amoled-175c");
  fake_ws_drop(0);
  fake_run(2000);
  fake_ws_drop(0);
  fake_run(4000);
  TEST_ASSERT_EQUAL_INT(3, fake_ws_opens());
  fake_handshake();
  fake_ws_drop(1006); /* a dropped session */
  TEST_ASSERT_EQUAL_INT(UI_OFFLINE_HOST_UNREACHABLE, core_ui_model()->offline.reason);
  fake_run(2000);
  TEST_ASSERT_EQUAL_INT(4, fake_ws_opens());
}

static void test_45_s_of_silence_closes_the_session(void) {
  fake_ready("amoled-175c");
  fake_run(30000);
  fake_ws_ping_in(); /* a ping counts as inbound */
  fake_run(44000);
  TEST_ASSERT_EQUAL_UINT16(0, fake_ws_close_code());
  fake_run(1100);
  TEST_ASSERT_EQUAL_UINT16(1001, fake_ws_close_code());
  TEST_ASSERT_EQUAL_INT(UI_SCREEN_OFFLINE, core_ui_model()->screen);
  fake_run(2000);
  TEST_ASSERT_EQUAL_INT(2, fake_ws_opens());
}

static void test_host_auto_picks_the_stored_host_id(void) {
  fake_store_paired();
  fake_storage_put(GADGET_KEY_HOST_ADDR, "");
  fake_boot("amoled-175c");
  TEST_ASSERT_EQUAL_INT(1, fake_mdns_browses());
  TEST_ASSERT_EQUAL_INT(0, fake_ws_opens());
  gadget_mdns_host_t hosts[2] = {
      {.name = "Other Mac", .address = "192.168.1.9:8810", .id = "ffffffffffffffffffffffffffffffff"},
      {.name = "Omkar's Mac", .address = "192.168.1.20:8810", .id = FAKE_HOST_ID},
  };
  fake_mdns_result(hosts, 2);
  TEST_ASSERT_EQUAL_INT(1, fake_ws_opens());
  TEST_ASSERT_EQUAL_STRING("192.168.1.20", fake_ws_host());
  /* a failed connect forgets the resolved address and browses again */
  fake_ws_drop(0);
  fake_run(2000);
  TEST_ASSERT_EQUAL_INT(2, fake_mdns_browses());
  /* a paired gadget whose MausBot is not advertised keeps looking, with backoff */
  gadget_mdns_host_t other = {.name = "Other Mac", .address = "192.168.1.9:8810", .id = "ffffffffffffffffffffffffffffffff"};
  fake_console_clear();
  fake_mdns_result(&other, 1);
  TEST_ASSERT_EQUAL_INT(1, fake_ws_opens());
  cJSON *line = fake_omb("hosts");
  TEST_ASSERT_NOT_NULL(line);
  cJSON_Delete(line);
  fake_run(4000);
  TEST_ASSERT_EQUAL_INT(3, fake_mdns_browses());
  /* the next miss of the same run prints no second hosts line */
  fake_console_clear();
  fake_mdns_result(&other, 1);
  TEST_ASSERT_NULL(fake_omb("hosts"));
  fake_run(8000);
  TEST_ASSERT_EQUAL_INT(4, fake_mdns_browses());
}

static void test_host_auto_before_pairing(void) {
  fake_storage_put(GADGET_KEY_PAIR_CODE, "123456");
  fake_boot("lcd-154");
  TEST_ASSERT_EQUAL_INT(CORE_PAIR_CODE_STORED, core_pair_state());
  gadget_mdns_host_t two[2] = {
      {.name = "A", .address = "10.0.0.1:8810", .id = "0123456789abcdef0123456789abcdef"},
      {.name = "B", .address = "10.0.0.2:8810", .id = ""},
  };
  fake_mdns_result(two, 2);
  cJSON *line = fake_omb("hosts");
  TEST_ASSERT_NOT_NULL(line);
  TEST_ASSERT_EQUAL_INT(2, cJSON_GetArraySize(cJSON_GetObjectItem(line, "hosts")));
  cJSON *first = cJSON_GetArrayItem(cJSON_GetObjectItem(line, "hosts"), 0);
  TEST_ASSERT_EQUAL_STRING("A", cJSON_GetObjectItem(first, "name")->valuestring);
  TEST_ASSERT_EQUAL_STRING("10.0.0.1:8810", cJSON_GetObjectItem(first, "address")->valuestring);
  TEST_ASSERT_EQUAL_STRING("0123456789abcdef0123456789abcdef", cJSON_GetObjectItem(first, "id")->valuestring);
  cJSON_Delete(line);
  TEST_ASSERT_EQUAL_INT(UI_SETUP_HOST_NOT_FOUND, core_ui_model()->setup.step);
  TEST_ASSERT_EQUAL_INT(0, fake_ws_opens());
  fake_run(30000);
  TEST_ASSERT_EQUAL_INT(1, fake_mdns_browses()); /* it waits for host <address> (spec §5.6) */
  TEST_ASSERT_EQUAL_INT(CORE_PAIR_CODE_STORED, core_pair_state());
  /* after a restart: nothing found within 5 s prints an empty list, and waits again */
  fake_boot("lcd-154");
  TEST_ASSERT_EQUAL_INT(2, fake_mdns_browses());
  fake_console_clear();
  fake_run(6100);
  line = fake_omb("hosts");
  TEST_ASSERT_NOT_NULL(line);
  TEST_ASSERT_EQUAL_INT(0, cJSON_GetArraySize(cJSON_GetObjectItem(line, "hosts")));
  cJSON_Delete(line);
  fake_run(30000);
  TEST_ASSERT_EQUAL_INT(2, fake_mdns_browses());
  /* exactly one service: use it */
  fake_boot("lcd-154");
  gadget_mdns_host_t one = {.name = "A", .address = "10.0.0.1:9000", .id = ""};
  fake_mdns_result(&one, 1);
  TEST_ASSERT_EQUAL_STRING("10.0.0.1", fake_ws_host());
  TEST_ASSERT_EQUAL_UINT16(9000, fake_ws_port());
  TEST_ASSERT_EQUAL_INT(CORE_PAIR_CONNECTING, core_pair_state());
}

static void test_host_auto_unsupported_prints_empty_hosts_and_waits(void) {
  fake_mdns_unsupported(true);
  fake_storage_put(GADGET_KEY_PAIR_CODE, "123456");
  fake_boot("devkit");
  cJSON *line = fake_omb("hosts");
  TEST_ASSERT_NOT_NULL(line);
  cJSON_Delete(line);
  TEST_ASSERT_EQUAL_INT(UI_SETUP_HOST_NOT_FOUND, core_ui_model()->setup.step);
  fake_console_clear();
  fake_run(30000);
  TEST_ASSERT_NULL(fake_omb("hosts")); /* halted: waits for host <address> */
}

static void test_wifi_drop_and_return(void) {
  fake_store_paired();
  fake_storage_put(GADGET_KEY_WIFI_SSID, "Home");
  fake_wifi_set(GADGET_WIFI_CONNECTING);
  fake_boot("lcd-154");
  TEST_ASSERT_EQUAL_INT(0, fake_ws_opens());
  TEST_ASSERT_EQUAL_INT(UI_OFFLINE_WIFI_CONNECTING, core_ui_model()->offline.reason);
  fake_wifi_set(GADGET_WIFI_FAILED);
  fake_run(10);
  TEST_ASSERT_EQUAL_INT(UI_OFFLINE_WIFI_FAILED, core_ui_model()->offline.reason);
  TEST_ASSERT_EQUAL_STRING("Home", core_ui_model()->offline.ssid);
  fake_wifi_set(GADGET_WIFI_CONNECTED);
  fake_run(10);
  TEST_ASSERT_EQUAL_INT(1, fake_ws_opens());
  fake_handshake();
  fake_wifi_set(GADGET_WIFI_CONNECTING); /* losing Wi-Fi closes the session */
  fake_run(10);
  TEST_ASSERT_FALSE(fake_ws_live());
  TEST_ASSERT_EQUAL_INT(UI_SCREEN_OFFLINE, core_ui_model()->screen);
}

static void test_settings_update_bot_name_and_speak_pushes(void) {
  fake_ready("amoled-175c");
  fake_ws_in("{\"op\":\"settings\",\"bot\":{\"id\":\"b_ada\",\"name\":\"Ada\"},\"settings\":{\"speak_pushes\":true},"
             "\"name\":\"Desk Maus\"}");
  fake_run(10);
  TEST_ASSERT_EQUAL_STRING("Ada", core_ui_model()->bot_name);
  TEST_ASSERT_EQUAL_STRING("b_ada", fake_storage_str(GADGET_KEY_BOT_ID));
  TEST_ASSERT_EQUAL_STRING("1", fake_storage_str(GADGET_KEY_SPEAK_PUSH));
  TEST_ASSERT_EQUAL_STRING("Desk Maus", fake_storage_str(GADGET_KEY_NAME));
  TEST_ASSERT_EQUAL_STRING("Desk Maus", core_ui_model()->device_name);
  /* the next hello carries the new name */
  fake_ws_drop(1006);
  fake_run(2000);
  fake_ws_accept();
  cJSON *hello = fake_ws_last("hello");
  TEST_ASSERT_EQUAL_STRING("Desk Maus", cJSON_GetObjectItem(hello, "name")->valuestring);
  cJSON_Delete(hello);
}

static void test_names_are_cut_to_32_characters(void) {
  fake_store_paired();
  core_config_t cfg = {.board = gadget_board_by_id("amoled-175c"), .fw_version = "1.0.0", .prng_seed = 1,
                       .default_name = "Kitchen counter Maus by the window, left"}; /* 40 */
  fake_boot_cfg(&cfg);
  fake_ws_accept();
  cJSON *hello = fake_ws_last("hello");
  const char *name = cJSON_GetObjectItem(hello, "name")->valuestring;
  TEST_ASSERT_EQUAL_size_t(32, gadget_utf8_len(name)); /* spec §4.3 */
  TEST_ASSERT_EQUAL_STRING("Kitchen counter Maus by the win\xE2\x80\xA6", name);
  cJSON_Delete(hello);
  fake_ws_in(CHALLENGE);
  fake_ws_in(READY);
  fake_ws_in("{\"op\":\"settings\",\"name\":\"A desktop rename that is far too long\"}"); /* 37 */
  fake_run(10);
  TEST_ASSERT_EQUAL_STRING("A desktop rename that is far to\xE2\x80\xA6", core_ui_model()->device_name);
  TEST_ASSERT_EQUAL_STRING(core_ui_model()->device_name, fake_storage_str(GADGET_KEY_NAME));
}

static void test_unknown_and_malformed_frames_are_ignored(void) {
  fake_ready("amoled-175c");
  fake_ws_in("{\"op\":\"future.op\",\"x\":1}");
  fake_ws_in("not json");
  fake_ws_in("{\"op\":\"reply\"}");
  fake_run(10);
  TEST_ASSERT_EQUAL_INT(CORE_PAIR_PAIRED, core_pair_state());
  TEST_ASSERT_FALSE(fake_ws_close_code() != 0);
}

static void test_frames_over_the_size_limits_are_dropped(void) {
  fake_ready("amoled-175c");
  static uint8_t big[GADGET_BINARY_FRAME_MAX + 2000];
  memset(big, 0x11, sizeof big);
  big[0] = 0x02; /* speaker audio, stream 1 */
  big[1] = 1;
  fake_ws_in("{\"op\":\"speak.begin\",\"stream\":1,\"rate\":16000}"); /* a stream that would take it */
  fake_ws_bin_in(big, sizeof big);
  static char text[GADGET_TEXT_FRAME_MAX + 100];
  memset(text, ' ', sizeof text - 1);
  static const char head[] = "{\"op\":\"card\",\"id\":\"c\",\"title\":\"t\",\"body\":\"";
  memcpy(text, head, strlen(head)); /* a valid card, just too big */
  memcpy(text + sizeof text - 4, "\"}", 2);
  text[sizeof text - 2] = '\0';
  fake_ws_in(text);
  fake_run(10);
  TEST_ASSERT_EQUAL_INT(CORE_PAIR_PAIRED, core_pair_state()); /* still fine, nothing shown */
  TEST_ASSERT_EQUAL_INT(UI_SCREEN_IDLE, core_ui_model()->screen);
  fake_ws_in("{\"op\":\"speak.end\",\"stream\":1}");
  fake_run(500);
  TEST_ASSERT_EQUAL_size_t(0, fake_spk_accepted()); /* the big frame never reached the speaker */
  TEST_ASSERT_EQUAL_STRING("", core_ui_model()->card.title); /* nor the big card the screen */
}

static int g_taps;
static char g_tap_ops[16][16];
static void tap(core_tap_dir_t dir, const char *op, const char *json, size_t len, void *ctx) {
  (void)json;
  (void)len;
  (void)ctx;
  if (g_taps < 16) snprintf(g_tap_ops[g_taps], 16, "%s%s", dir == CORE_TAP_TX ? ">" : "<", op);
  g_taps++;
}

static void test_tap_sees_every_text_frame(void) {
  g_taps = 0;
  core_set_tap(tap, NULL);
  fake_ready("amoled-175c");
  fake_ws_in("{\"op\":\"future.op\"}");
  core_set_tap(NULL, NULL);
  TEST_ASSERT_EQUAL_INT(5, g_taps);
  TEST_ASSERT_EQUAL_STRING(">hello", g_tap_ops[0]);
  TEST_ASSERT_EQUAL_STRING("<challenge", g_tap_ops[1]);
  TEST_ASSERT_EQUAL_STRING(">prove", g_tap_ops[2]);
  TEST_ASSERT_EQUAL_STRING("<ready", g_tap_ops[3]);
  TEST_ASSERT_EQUAL_STRING("<future.op", g_tap_ops[4]);
}

int main(void) {
  if (psa_crypto_init() != PSA_SUCCESS) return 3;
  UNITY_BEGIN();
  RUN_TEST(test_unpaired_gadget_waits_on_setup);
  RUN_TEST(test_setup_asks_for_wifi_first);
  RUN_TEST(test_paired_gadget_connects_and_becomes_ready);
  RUN_TEST(test_handshake_helper_verifies_the_prove_signature);
  RUN_TEST(test_enrollment_sends_the_code_and_stores_the_host);
  RUN_TEST(test_challenge_with_a_bad_host_id_closes_and_backs_off);
  RUN_TEST(test_bad_code_clears_the_code_and_stops);
  RUN_TEST(test_setup_keeps_the_challenge_host_name_after_bad_code);
  RUN_TEST(test_enroll_required_and_revoked_unpair);
  RUN_TEST(test_device_limit_retries_every_10_s_for_120_s);
  RUN_TEST(test_replaced_shows_in_use_and_stays_offline);
  RUN_TEST(test_protocol_errors_back_off_2_4_8_up_to_60_s);
  RUN_TEST(test_proto_unsupported_errors_and_backs_off);
  RUN_TEST(test_ready_resets_the_backoff);
  RUN_TEST(test_45_s_of_silence_closes_the_session);
  RUN_TEST(test_host_auto_picks_the_stored_host_id);
  RUN_TEST(test_host_auto_before_pairing);
  RUN_TEST(test_host_auto_unsupported_prints_empty_hosts_and_waits);
  RUN_TEST(test_wifi_drop_and_return);
  RUN_TEST(test_settings_update_bot_name_and_speak_pushes);
  RUN_TEST(test_names_are_cut_to_32_characters);
  RUN_TEST(test_unknown_and_malformed_frames_are_ignored);
  RUN_TEST(test_frames_over_the_size_limits_are_dropped);
  RUN_TEST(test_tap_sees_every_text_frame);
  return UNITY_END();
}
```

Change `firmware/tests/CMakeLists.txt` exactly as this diff shows (`-` lines go, `+` lines come; the rest is context):

```diff
--- a/firmware/tests/CMakeLists.txt
+++ b/firmware/tests/CMakeLists.txt
@@ -26,6 +26,7 @@ gadget_pure_test(crypto)
 gadget_pure_test(layout)
 gadget_pure_test(console_parse)
 gadget_unit_test(core_init)
+gadget_unit_test(session)
 
 # protocol/vectors in C (label vectors).
 add_executable(test_vectors test_vectors.c)
```

- [ ] **Step 2: Run it to see it fail**

Run: `cmake --build build/host -j10`
Expected: the link of `test_session` fails on `core_pair_state` and `core_last_error`.

- [ ] **Step 3: Write the implementation**

Create `firmware/core/src/session.c`:

```c
/* firmware/core/src/session.c */
/* SPDX-License-Identifier: Apache-2.0 */
/* The connection to MausBot (spec §4.3): finding the host, the handshake,
 * the reaction to every handshake error, reconnect backoff and liveness. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "core_internal.h"

#define TAG "session"
#define CONNECT_GUARD_MS 10000u   /* the port posts WS_CLOSED within 5 s; this is a safety net */

typedef enum {
  SS_IDLE = 0,   /* before the first tick */
  SS_WAIT_WIFI,
  SS_RESOLVING,  /* hal_mdns_browse running */
  SS_CONNECTING, /* hal_ws_open called, waiting for WS_OPEN */
  SS_HANDSHAKE,  /* hello sent, waiting for challenge / ready */
  SS_READY,
  SS_CLOSING,    /* hal_ws_close called, waiting for WS_CLOSED */
  SS_BACKOFF,    /* waiting until retry_at */
  SS_HALTED      /* no automatic reconnect */
} sess_state_t;

typedef enum {
  HALT_NONE = 0,
  HALT_UNPAIRED,   /* no host_id and no code: enroll_required, revoked, device-limit window over */
  HALT_BAD_CODE,
  HALT_REPLACED,   /* until reboot or TALK */
  HALT_NO_MDNS,    /* host auto is not available on this port */
  HALT_NO_HOST     /* never paired, and host auto found none or several: wait for host <address> */
} halt_t;

typedef enum { AFTER_RETRY = 0, AFTER_NOW, AFTER_HALT, AFTER_DEVICE_LIMIT } after_t;

char g_core_tx[GADGET_TEXT_FRAME_MAX];

static struct {
  sess_state_t st;
  halt_t halt, pending_halt;
  after_t after;               /* what the next WS_CLOSED leads to */
  bool ws_live;                /* hal_ws_open succeeded and WS_CLOSED has not arrived */
  bool ws_opened;              /* WS_OPEN arrived on this connection */
  bool challenged;
  bool was_ready;
  uint64_t last_rx, connect_at, retry_at, mdns_deadline;
  uint32_t backoff_ms;
  char resolved[CORE_HOST_ADDR_MAX];
  char ch_host_id[GADGET_HOST_ID_LEN + 1];
  char ch_host_name[UI_NAME_MAX]; /* the last challenge's; outlives its connection (spec 5.5) */
  char last_error[24];
  bool error_flag;             /* @omb pair "error" */
  bool protocol_error;         /* Offline reason "protocol" */
  bool host_not_found;
  bool hosts_printed;          /* this run of host auto misses printed its hosts line */
  bool device_limit;
  bool window_known;
  uint64_t window_start;       /* when the stored code was given (device_limit window) */
} S;

/* ---- sending ------------------------------------------------------------------- */

gadget_status_t session_send(const char *op, const char *json, int len) {
  if (len < 0) return (gadget_status_t)len;
  if (!S.ws_live || !S.ws_opened) return GADGET_ERR_BUSY;
  core_tap(CORE_TAP_TX, op, json, (size_t)len);
  return hal_ws_send_text(json, (size_t)len);
}

gadget_status_t session_send_binary(const uint8_t *frame, size_t len) {
  if (S.st != SS_READY) return GADGET_ERR_BUSY;
  return hal_ws_send_binary(frame, len);
}

static void send_hello(void) {
  gp_hello_t h = {.id = g_core.id, .pubkey_b64 = g_core.pub_b64, .name = g_core.name, .fw = g_core.fw,
                  .board = g_core.board};
  gadget_battery_t batt;
  if (g_core.board->has_battery && hal_battery_read(&batt)) {
    h.battery_valid = true;
    h.battery_pct = batt.pct > 100 ? 100 : batt.pct;
    h.charging = batt.charging;
  }
  int n = gp_encode_hello(g_core_tx, sizeof g_core_tx, &h);
  if (session_send("hello", g_core_tx, n) != GADGET_OK) hal_log(GADGET_LOG_ERROR, TAG, "hello not sent (%d)", n);
}

/* ---- console output ---------------------------------------------------------------- */

static void print_hosts(const gadget_mdns_host_t *hosts, uint8_t count) {
  cJSON *o = cJSON_CreateObject();
  cJSON_AddStringToObject(o, "op", "hosts");
  cJSON *arr = cJSON_AddArrayToObject(o, "hosts");
  for (uint8_t i = 0; i < count; i++) {
    cJSON *h = cJSON_CreateObject();
    cJSON_AddStringToObject(h, "name", hosts[i].name);
    cJSON_AddStringToObject(h, "address", hosts[i].address);
    cJSON_AddStringToObject(h, "id", hosts[i].id);
    cJSON_AddItemToArray(arr, h);
  }
  core_omb(o);
}

/* ---- state changes -------------------------------------------------------------------- */

static void schedule_retry(void) {
  S.retry_at = g_core.now + S.backoff_ms;
  S.backoff_ms = S.backoff_ms * 2 > GADGET_BACKOFF_MAX_MS ? GADGET_BACKOFF_MAX_MS : S.backoff_ms * 2;
  S.st = SS_BACKOFF;
}

static void halt(halt_t why) {
  S.st = SS_HALTED;
  S.halt = why;
}

static void close_conn(uint16_t code, after_t after, halt_t why) {
  S.after = after;
  S.pending_halt = why;
  if (S.ws_live) {
    S.st = SS_CLOSING;
    hal_ws_close(code);
  }
}

static void connect_to(const char *addr) {
  char host[CORE_HOST_ADDR_MAX];
  snprintf(host, sizeof host, "%s", addr);
  uint16_t port = GADGET_DEFAULT_PORT;
  char *colon = strrchr(host, ':');
  if (colon != NULL) {
    long p = strtol(colon + 1, NULL, 10);
    if (p > 0 && p <= 65535) port = (uint16_t)p;
    *colon = '\0';
  }
  if (hal_ws_open(host, port) != GADGET_OK) {
    hal_log(GADGET_LOG_WARN, TAG, "connect to %s:%u failed at once", host, (unsigned)port);
    schedule_retry();
    return;
  }
  hal_log(GADGET_LOG_INFO, TAG, "connecting to %s:%u", host, (unsigned)port);
  S.ws_live = true;
  S.ws_opened = false;
  S.challenged = false;
  S.connect_at = S.last_rx = g_core.now;
  S.st = SS_CONNECTING;
}

static void start_attempt(void) {
  if (g_core.host_id[0] == '\0' && g_core.pair_code[0] == '\0') {
    halt(HALT_UNPAIRED);
    return;
  }
  if (hal_wifi_state() != GADGET_WIFI_CONNECTED) {
    S.st = SS_WAIT_WIFI;
    return;
  }
  if (g_core.host_addr[0] != '\0') {
    connect_to(g_core.host_addr);
    return;
  }
  if (S.resolved[0] != '\0') {
    connect_to(S.resolved);
    return;
  }
  gadget_status_t st = hal_mdns_browse(GADGET_HOST_AUTO_TIMEOUT_MS);
  if (st == GADGET_ERR_UNSUPPORTED) {
    hal_log(GADGET_LOG_WARN, TAG, "host auto is not available here; use host <address>");
    print_hosts(NULL, 0);
    S.host_not_found = true;
    halt(HALT_NO_MDNS);
    return;
  }
  if (st != GADGET_OK) {
    schedule_retry();
    return;
  }
  S.mdns_deadline = g_core.now + GADGET_HOST_AUTO_TIMEOUT_MS + 1000u;
  S.st = SS_RESOLVING;
}

/* host auto found none, or several and no stored host_id picks one (spec §5.6).
 * The hosts line prints once per run of misses, so the installer asks once. */
static void host_not_found(const gadget_mdns_host_t *hosts, uint8_t count) {
  if (!S.hosts_printed) print_hosts(hosts, count);
  S.hosts_printed = true;
  S.host_not_found = true;
  if (g_core.host_id[0] == '\0') {
    halt(HALT_NO_HOST); /* the installer or the person answers with host <address> */
  } else {
    schedule_retry();   /* a paired gadget keeps looking for its own MausBot */
  }
}

static void on_mdns(const gadget_mdns_ev_t *r) {
  if (S.st != SS_RESOLVING) return;
  const gadget_mdns_host_t *pick = NULL;
  uint8_t count = r->ok ? r->count : 0;
  for (uint8_t i = 0; i < count; i++) {
    if (g_core.host_id[0] != '\0' && strcmp(r->hosts[i].id, g_core.host_id) == 0) pick = &r->hosts[i];
  }
  if (g_core.host_id[0] == '\0' && count == 1) pick = &r->hosts[0];
  if (pick == NULL) {
    host_not_found(r->hosts, count);
    return;
  }
  snprintf(S.resolved, sizeof S.resolved, "%s", pick->address);
  S.host_not_found = false;
  connect_to(S.resolved);
}

static void apply_bot(const gp_bot_t *bot) {
  if (strcmp(g_core.bot_id, bot->id) != 0) {
    snprintf(g_core.bot_id, sizeof g_core.bot_id, "%s", bot->id);
    core_store_str(GADGET_KEY_BOT_ID, g_core.bot_id);
  }
  char name[UI_NAME_MAX];
  core_text_copy(name, sizeof name, bot->name);
  if (strcmp(g_core.bot_name, name) != 0) {
    snprintf(g_core.bot_name, sizeof g_core.bot_name, "%s", name);
    core_store_str(GADGET_KEY_BOT_NAME, g_core.bot_name);
  }
}

static void apply_settings(const gp_settings_values_t *s) {
  if (g_core.speak_pushes != s->speak_pushes) {
    g_core.speak_pushes = s->speak_pushes;
    core_store_str(GADGET_KEY_SPEAK_PUSH, s->speak_pushes ? "1" : "0");
  }
}

static void on_challenge(const gp_challenge_t *c) {
  if (!gadget_host_id_valid(c->host_id) || c->nonce[0] == '\0' || strlen(c->nonce) > 64) {
    hal_log(GADGET_LOG_WARN, TAG, "challenge with a bad host_id or nonce; closing");
    S.protocol_error = true;
    close_conn(1002, AFTER_RETRY, HALT_NONE);
    return;
  }
  snprintf(S.ch_host_id, sizeof S.ch_host_id, "%s", c->host_id);
  core_text_copy(S.ch_host_name, sizeof S.ch_host_name, c->host_name ? c->host_name : "");
  S.challenged = true;
  char text[160];
  int tn = gp_prove_text(text, sizeof text, g_core.id, c->nonce, c->host_id);
  uint8_t der[GADGET_SIG_DER_MAX];
  size_t der_len = 0;
  char sig[GADGET_SIG_B64_MAX + 1];
  if (tn < 0 || hal_crypto_sign(g_core.priv, (const uint8_t *)text, (size_t)tn, der, &der_len) != GADGET_OK ||
      gadget_b64_encode(sig, sizeof sig, der, der_len) == 0) {
    hal_log(GADGET_LOG_ERROR, TAG, "signing the challenge failed");
    close_conn(1011, AFTER_RETRY, HALT_NONE);
    return;
  }
  gp_prove_t p = {.sig_b64 = sig, .enroll = g_core.pair_code[0] ? g_core.pair_code : NULL};
  session_send("prove", g_core_tx, gp_encode_prove(g_core_tx, sizeof g_core_tx, &p));
}

static void on_ready(const gp_ready_t *r) {
  S.st = SS_READY;
  S.was_ready = true;
  if (strcmp(g_core.host_id, S.ch_host_id) != 0) {
    snprintf(g_core.host_id, sizeof g_core.host_id, "%s", S.ch_host_id);
    core_store_str(GADGET_KEY_HOST_ID, g_core.host_id);
  }
  if (strcmp(g_core.host_name, S.ch_host_name) != 0) {
    snprintf(g_core.host_name, sizeof g_core.host_name, "%s", S.ch_host_name);
    core_store_str(GADGET_KEY_HOST_NAME, g_core.host_name);
  }
  if (g_core.pair_code[0] != '\0') {
    g_core.pair_code[0] = '\0';
    core_store_erase(GADGET_KEY_PAIR_CODE);
  }
  S.backoff_ms = GADGET_BACKOFF_MIN_MS;
  S.error_flag = S.protocol_error = S.host_not_found = S.hosts_printed = S.device_limit = false;
  S.last_error[0] = '\0';
  S.halt = HALT_NONE;
  apply_bot(&r->bot);
  apply_settings(&r->settings);
  hal_log(GADGET_LOG_INFO, TAG, "ready with %s (session %s)", g_core.host_name, r->session);
  core_on_ready();
}

static void on_error(const gp_error_t *e) {
  snprintf(S.last_error, sizeof S.last_error, "%s", e->code);
  hal_log(GADGET_LOG_WARN, TAG, "host error %s: %s", e->code, e->message ? e->message : "");
  if (strcmp(e->code, "bad_code") == 0) {
    g_core.pair_code[0] = '\0';
    core_store_erase(GADGET_KEY_PAIR_CODE);
    S.error_flag = true;
    close_conn(1000, AFTER_HALT, HALT_BAD_CODE);
  } else if (strcmp(e->code, "enroll_required") == 0 || strcmp(e->code, "revoked") == 0) {
    g_core.host_id[0] = '\0';
    core_store_erase(GADGET_KEY_HOST_ID);
    S.error_flag = false;
    close_conn(1000, AFTER_HALT, HALT_UNPAIRED);
  } else if (strcmp(e->code, "device_limit") == 0) {
    S.device_limit = true;
    close_conn(1000, AFTER_DEVICE_LIMIT, HALT_NONE);
  } else if (strcmp(e->code, "replaced") == 0) {
    close_conn(1000, AFTER_HALT, HALT_REPLACED);
  } else {
    /* proto_unsupported, bad_sig, or a code this firmware does not know */
    S.error_flag = true;
    S.protocol_error = true;
    close_conn(1000, AFTER_RETRY, HALT_NONE);
  }
}

static void on_settings(const gp_settings_t *s) {
  if (s->has_bot) apply_bot(&s->bot);
  if (s->has_settings) apply_settings(&s->settings);
  if (s->name != NULL && s->name[0] != '\0') {
    char old[UI_NAME_MAX];
    snprintf(old, sizeof old, "%s", g_core.name);
    core_set_name(s->name);
    if (strcmp(old, g_core.name) != 0) core_store_str(GADGET_KEY_NAME, g_core.name);
  }
}

static void on_text(const uint8_t *data, size_t len) {
  gp_msg_t m;
  if (gp_decode((const char *)data, len, &m) != GADGET_OK) {
    hal_log(GADGET_LOG_WARN, TAG, "ignored a malformed frame");
    return;
  }
  const char *op = gp_op_name(m.op);
  if (op == NULL) op = cJSON_GetObjectItemCaseSensitive(m.root, "op")->valuestring;
  core_tap(CORE_TAP_RX, op, (const char *)data, len);
  switch (m.op) {
    case GP_OP_UNKNOWN:
      break;
    case GP_OP_CHALLENGE:
      if (S.st == SS_HANDSHAKE && !S.challenged) on_challenge(&m.m.challenge);
      break;
    case GP_OP_READY:
      if (S.st == SS_HANDSHAKE && S.challenged) on_ready(&m.m.ready);
      break;
    case GP_OP_ERROR:
      if (S.st == SS_HANDSHAKE || S.st == SS_READY) on_error(&m.m.error);
      break;
    case GP_OP_SETTINGS:
      if (S.st == SS_READY) on_settings(&m.m.settings);
      break;
    default:
      if (S.st == SS_READY) core_on_msg(&m);
      break;
  }
  gp_msg_free(&m);
}

static void on_closed(uint16_t code) {
  if (!S.ws_live) return;
  hal_log(GADGET_LOG_INFO, TAG, "connection closed (%u)", (unsigned)code);
  S.ws_live = false;
  S.challenged = false;
  if (!S.ws_opened) S.resolved[0] = '\0'; /* connect failed: resolve again next time */
  if (S.was_ready) {
    S.was_ready = false;
    core_on_session_lost();
  }
  after_t after = S.after;
  S.after = AFTER_RETRY;
  switch (after) {
    case AFTER_NOW:
      S.st = SS_BACKOFF;
      S.retry_at = g_core.now;
      break;
    case AFTER_HALT:
      halt(S.pending_halt);
      break;
    case AFTER_DEVICE_LIMIT:
      S.st = SS_BACKOFF;
      S.retry_at = g_core.now + GADGET_DEVICE_LIMIT_RETRY_MS;
      break;
    case AFTER_RETRY:
    default:
      schedule_retry();
      break;
  }
}

/* ---- module API ------------------------------------------------------------------------- */

void session_init(void) {
  memset(&S, 0, sizeof S);
  S.backoff_ms = GADGET_BACKOFF_MIN_MS;
  S.st = SS_IDLE;
}

void session_deinit(void) {
  if (S.ws_live) hal_ws_close(1001);
  memset(&S, 0, sizeof S);
}

static void publish(void) {
  ui_model_t *m = &g_core.model;
  snprintf(m->device_name, sizeof m->device_name, "%s", g_core.name);
  core_text_copy(m->bot_name, sizeof m->bot_name, g_core.bot_name);
  core_text_copy(m->host_name, sizeof m->host_name, S.ch_host_name[0] ? S.ch_host_name : g_core.host_name);
  gadget_wifi_state_t wifi = hal_wifi_state();
  m->setup.wifi_set = g_core.wifi_ssid[0] != '\0' || wifi == GADGET_WIFI_CONNECTED;
  m->setup.code_stored = g_core.pair_code[0] != '\0';
  if (!m->setup.wifi_set) m->setup.step = UI_SETUP_NEED_WIFI;
  else if (S.error_flag && strcmp(S.last_error, "bad_code") == 0) m->setup.step = UI_SETUP_BAD_CODE;
  else if (S.device_limit) m->setup.step = UI_SETUP_DEVICE_LIMIT;
  else if (S.host_not_found) m->setup.step = UI_SETUP_HOST_NOT_FOUND;
  else if (m->setup.code_stored) m->setup.step = UI_SETUP_PAIRING;
  else m->setup.step = UI_SETUP_NEED_CODE;
  snprintf(m->offline.ssid, sizeof m->offline.ssid, "%s", g_core.wifi_ssid);
  m->offline.retry_at_ms = S.st == SS_BACKOFF ? S.retry_at : 0;
  if (wifi == GADGET_WIFI_CONNECTING) m->offline.reason = UI_OFFLINE_WIFI_CONNECTING;
  else if (wifi != GADGET_WIFI_CONNECTED) m->offline.reason = UI_OFFLINE_WIFI_FAILED;
  else if (S.st == SS_HALTED && S.halt == HALT_REPLACED) m->offline.reason = UI_OFFLINE_IN_USE_ELSEWHERE;
  else if (S.protocol_error) m->offline.reason = UI_OFFLINE_PROTOCOL;
  else if (S.host_not_found || S.st == SS_RESOLVING || S.st == SS_CONNECTING || S.st == SS_HANDSHAKE ||
           S.st == SS_IDLE)
    m->offline.reason = UI_OFFLINE_HOST_LOOKUP;
  else m->offline.reason = UI_OFFLINE_HOST_UNREACHABLE;
}

void session_event(const gadget_event_t *ev) {
  switch (ev->type) {
    case GADGET_EV_WIFI_STATE:
      if (ev->u.wifi.state != GADGET_WIFI_CONNECTED && S.ws_live) close_conn(1001, AFTER_RETRY, HALT_NONE);
      break;
    case GADGET_EV_WS_OPEN:
      if (!S.ws_live || S.st != SS_CONNECTING) break;
      S.ws_opened = true;
      S.last_rx = g_core.now;
      S.st = SS_HANDSHAKE;
      send_hello();
      break;
    case GADGET_EV_WS_TEXT:
      if (!S.ws_live) break;
      S.last_rx = g_core.now;
      if (ev->u.ws.len > GADGET_TEXT_FRAME_MAX) { /* over the protocol limit: never decoded */
        hal_log(GADGET_LOG_WARN, TAG, "dropped a %u-byte text frame", (unsigned)ev->u.ws.len);
        break;
      }
      on_text(ev->u.ws.data, ev->u.ws.len);
      break;
    case GADGET_EV_WS_BINARY:
      if (!S.ws_live) break;
      S.last_rx = g_core.now;
      if (ev->u.ws.len > GADGET_BINARY_FRAME_MAX) {
        hal_log(GADGET_LOG_WARN, TAG, "dropped a %u-byte binary frame", (unsigned)ev->u.ws.len);
        break;
      }
      if (S.st == SS_READY) {
        gp_bin_kind_t kind;
        uint8_t stream;
        const uint8_t *payload;
        size_t plen;
        if (gp_bin_decode(ev->u.ws.data, ev->u.ws.len, &kind, &stream, &payload, &plen) == GADGET_OK) {
          core_on_binary(kind, stream, payload, plen);
        }
      }
      break;
    case GADGET_EV_WS_CONTROL:
      if (S.ws_live) S.last_rx = g_core.now;
      break;
    case GADGET_EV_WS_CLOSED:
      on_closed(ev->u.closed.code);
      break;
    case GADGET_EV_MDNS:
      on_mdns(&ev->u.mdns);
      break;
    default:
      break;
  }
  publish();
}

void session_tick(void) {
  uint64_t now = g_core.now;
  if (g_core.pair_code[0] != '\0' && !S.window_known) {
    S.window_known = true;
    S.window_start = now;
  }
  switch (S.st) {
    case SS_IDLE:
      start_attempt();
      break;
    case SS_WAIT_WIFI:
      if (hal_wifi_state() == GADGET_WIFI_CONNECTED) start_attempt();
      break;
    case SS_BACKOFF:
      if (now >= S.retry_at) start_attempt();
      break;
    case SS_RESOLVING:
      if (now >= S.mdns_deadline) host_not_found(NULL, 0);
      break;
    case SS_CONNECTING:
      if (now - S.connect_at >= CONNECT_GUARD_MS) close_conn(1001, AFTER_RETRY, HALT_NONE);
      break;
    case SS_HANDSHAKE:
    case SS_READY:
      if (now - S.last_rx >= GADGET_IDLE_TIMEOUT_MS) {
        hal_log(GADGET_LOG_WARN, TAG, "nothing heard for 45 s; reconnecting");
        close_conn(1001, AFTER_RETRY, HALT_NONE);
      }
      break;
    default:
      break;
  }
  if (S.device_limit && now >= S.window_start + GADGET_DEVICE_LIMIT_WINDOW_MS) {
    hal_log(GADGET_LOG_WARN, TAG, "device limit: giving up on the pairing code");
    S.device_limit = false;
    g_core.pair_code[0] = '\0';
    core_store_erase(GADGET_KEY_PAIR_CODE);
    if (S.st == SS_BACKOFF && g_core.host_id[0] == '\0') halt(HALT_UNPAIRED);
  }
  publish();
}

bool session_ready(void) { return S.st == SS_READY; }

void session_reconnect_now(bool clear_error) {
  if (clear_error) {
    S.error_flag = false;
    S.last_error[0] = '\0';
    S.device_limit = false;
    S.window_known = false;
  }
  S.halt = HALT_NONE;
  S.backoff_ms = GADGET_BACKOFF_MIN_MS;
  S.protocol_error = false;
  S.host_not_found = false;
  S.hosts_printed = false;
  S.resolved[0] = '\0';
  if (S.ws_live) {
    close_conn(1000, AFTER_NOW, HALT_NONE);
  } else {
    S.st = SS_BACKOFF;
    S.retry_at = g_core.now;
  }
}

void session_wake(void) {
  if (S.st == SS_HALTED && S.halt == HALT_REPLACED) session_reconnect_now(false);
}

void session_clear_host_name(void) { S.ch_host_name[0] = '\0'; }

const char *session_host_in_use(void) {
  if (g_core.host_addr[0] != '\0') return g_core.host_addr;
  if (S.resolved[0] != '\0') return S.resolved;
  return NULL;
}

core_pair_state_t session_pair_state(void) {
  if (S.st == SS_READY) return CORE_PAIR_PAIRED;
  /* device_limit: "error" for its whole 120 s window, retries included; on_error
   * set last_error to "device_limit" (contract §2.11) */
  if (S.error_flag || S.device_limit) return CORE_PAIR_ERROR;
  if (g_core.host_id[0] == '\0' && g_core.pair_code[0] == '\0') return CORE_PAIR_UNPAIRED;
  if (session_host_in_use() != NULL) return CORE_PAIR_CONNECTING;
  if (g_core.pair_code[0] != '\0') return CORE_PAIR_CODE_STORED;
  return CORE_PAIR_CONNECTING;
}

const char *session_last_error(void) { return S.last_error; }
bool session_is_setup(void) { return g_core.host_id[0] == '\0'; }
```

Create `firmware/core/src/screens.c`:

```c
/* firmware/core/src/screens.c */
/* SPDX-License-Identifier: Apache-2.0 */
/* Screen and Maus-state selection (contract §2.7): core decides, the UI only
 * draws model.screen. Reads the flags each module keeps in g_core.f. */
#include "core_internal.h"

static ui_screen_t pick_screen(void) {
  if (!session_ready()) return session_is_setup() ? UI_SCREEN_SETUP : UI_SCREEN_OFFLINE;
  if (g_core.f.ota_active) return UI_SCREEN_UPDATE;
  if (g_core.f.recording) return UI_SCREEN_LISTENING;
  if (g_core.f.ask_visible) return UI_SCREEN_ASK;
  if (g_core.f.speaking) return UI_SCREEN_SPEAKING;
  if (g_core.f.turn_active) return UI_SCREEN_THINKING;
  if (g_core.f.reply_visible) return UI_SCREEN_REPLY;
  if (g_core.f.image_visible) return UI_SCREEN_IMAGE;
  if (g_core.f.card_visible) return UI_SCREEN_CARD;
  return UI_SCREEN_IDLE;
}

static ui_maus_state_t maus_for(ui_screen_t s, const ui_model_t *m) {
  switch (s) {
    case UI_SCREEN_SETUP: return UI_MAUS_CURIOUS;
    case UI_SCREEN_OFFLINE: return UI_MAUS_SLEEPING;
    case UI_SCREEN_IDLE: return UI_MAUS_IDLE;
    case UI_SCREEN_LISTENING: return UI_MAUS_LISTENING;
    case UI_SCREEN_THINKING: return m->thinking.working[0] ? UI_MAUS_WORKING : UI_MAUS_THINKING;
    case UI_SCREEN_SPEAKING: return UI_MAUS_SPEAKING;
    case UI_SCREEN_REPLY: return m->reply.failed ? UI_MAUS_ALERTING : UI_MAUS_IDLE;
    default: return UI_MAUS_NONE; /* boot, ask, card, image, update */
  }
}

void screens_update(void) {
  ui_model_t *m = &g_core.model;
  m->screen = pick_screen();
  m->maus = maus_for(m->screen, m);
  if (g_core.f.toast_visible && m->maus != UI_MAUS_NONE) m->maus = UI_MAUS_NOTIFYING;
}
```

Change `firmware/core/src/core_internal.h` exactly as this diff shows (`-` lines go, `+` lines come; the rest is context):

```diff
--- a/firmware/core/src/core_internal.h
+++ b/firmware/core/src/core_internal.h
@@ -71,5 +71,37 @@ void core_set_name(const char *src);
 void core_next_turn_id(char out[GADGET_TURN_MAX + 1]);
 /* Print "@omb " + the compact JSON of obj on the console; frees obj. */
 void core_omb(cJSON *obj);
+/* The protocol tap (core_set_tap) for one text frame. */
+void core_tap(core_tap_dir_t dir, const char *op, const char *json, size_t len);
+/* Session hooks: core.c hands these to the modules. */
+void core_on_ready(void);                       /* a ready arrived */
+void core_on_session_lost(void);                /* a ready session ended */
+void core_on_msg(const gp_msg_t *m);            /* a host op after ready (not handshake or settings) */
+void core_on_binary(gp_bin_kind_t kind, uint8_t stream, const uint8_t *payload, size_t len);
+
+/* ---- session.c -------------------------------------------------------------- */
+/* The shared encode buffer: encode into it, then session_send() it. */
+extern char g_core_tx[GADGET_TEXT_FRAME_MAX];
+void session_init(void);
+void session_deinit(void);
+void session_event(const gadget_event_t *ev);   /* Wi-Fi, WS_* and mDNS events */
+void session_tick(void);
+bool session_ready(void);
+/* Tap and send one encoded text frame; len < 0 (an encoder error) is returned as is. */
+gadget_status_t session_send(const char *op, const char *json, int len);
+gadget_status_t session_send_binary(const uint8_t *frame, size_t len);
+/* `pair`, `host`: drop any connection and connect again at once. */
+void session_reconnect_now(bool clear_error);
+/* TALK while halted by `replaced`: reconnect. */
+void session_wake(void);
+/* `host`, `host auto` and `forget`: drop the last challenge's host_name. */
+void session_clear_host_name(void);
+core_pair_state_t session_pair_state(void);
+const char *session_last_error(void);
+const char *session_host_in_use(void);          /* "addr:port", or NULL when unknown */
+bool session_is_setup(void);                    /* never paired (no host_id) */
+
+/* ---- screens.c -------------------------------------------------------------- */
+void screens_update(void);
 
 #endif /* CORE_INTERNAL_H */
```

Change `firmware/core/src/core.c` exactly as this diff shows (`-` lines go, `+` lines come; the rest is context):

```diff
--- a/firmware/core/src/core.c
+++ b/firmware/core/src/core.c
@@ -213,6 +213,7 @@ gadget_status_t core_init(const core_config_t *cfg) {
   gadget_hex_encode(g_core.turn_prefix + 1, rnd, sizeof rnd);
 
   publish_identity();
+  session_init();
   if (g_core.wifi_ssid[0] != '\0') hal_wifi_connect(g_core.wifi_ssid, g_core.wifi_pass);
 
   cJSON *boot = cJSON_CreateObject();
@@ -231,9 +232,19 @@ gadget_status_t core_init(const core_config_t *cfg) {
 void core_event(const gadget_event_t *ev) {
   if (!g_core.initialized || ev == NULL) return;
   switch (ev->type) {
+    case GADGET_EV_WIFI_STATE:
+    case GADGET_EV_WS_OPEN:
+    case GADGET_EV_WS_TEXT:
+    case GADGET_EV_WS_BINARY:
+    case GADGET_EV_WS_CONTROL:
+    case GADGET_EV_WS_CLOSED:
+    case GADGET_EV_MDNS:
+      session_event(ev);
+      break;
     default:
       break;
   }
+  screens_update();
   model_commit();
 }
 
@@ -242,20 +253,53 @@ void core_tick(uint64_t now_ms) {
   g_core.now = now_ms;
   g_core.model.now_ms = now_ms;
   g_core.ticked = true;
+  session_tick();
+  screens_update();
   model_commit();
 }
 
 const ui_model_t *core_ui_model(void) { return &g_core.model; }
 
 void core_deinit(void) {
+  if (g_core.initialized) session_deinit();
   memset(&g_core, 0, sizeof g_core);
   memset(&s_shadow, 0, sizeof s_shadow);
 }
 
 const char *core_device_id(void) { return g_core.id; }
+core_pair_state_t core_pair_state(void) { return session_pair_state(); }
+const char *core_last_error(void) { return session_last_error(); }
 const char *core_fw_version(void) { return g_core.fw; }
 
 void core_set_tap(core_tap_fn fn, void *ctx) {
   s_tap = fn;
   s_tap_ctx = ctx;
 }
+
+void core_tap(core_tap_dir_t dir, const char *op, const char *json, size_t len) {
+  if (s_tap != NULL) s_tap(dir, op, json, len, s_tap_ctx);
+}
+
+/* ---- session hooks ------------------------------------------------------------------ */
+
+void core_on_ready(void) { hal_log(GADGET_LOG_INFO, CORE_TAG, "talking to %s", g_core.bot_name); }
+
+void core_on_session_lost(void) { hal_log(GADGET_LOG_INFO, CORE_TAG, "session lost"); }
+
+void core_on_msg(const gp_msg_t *m) {
+  switch (m->op) {
+    default:
+      hal_log(GADGET_LOG_DEBUG, CORE_TAG, "ignored %s", gp_op_name(m->op));
+      break;
+  }
+}
+
+void core_on_binary(gp_bin_kind_t kind, uint8_t stream, const uint8_t *payload, size_t len) {
+  (void)payload;
+  switch (kind) {
+    default:
+      hal_log(GADGET_LOG_DEBUG, CORE_TAG, "ignored binary kind %d stream %u (%u bytes)", (int)kind, (unsigned)stream,
+              (unsigned)len);
+      break;
+  }
+}
```

Change `firmware/core/CMakeLists.txt` exactly as this diff shows (`-` lines go, `+` lines come; the rest is context):

```diff
--- a/firmware/core/CMakeLists.txt
+++ b/firmware/core/CMakeLists.txt
@@ -10,6 +10,8 @@ set(GADGET_CORE_SRCS
   src/ui_layout.c
   src/console.c
   src/core.c
+  src/session.c
+  src/screens.c
 )
 
 if(ESP_PLATFORM)
```

- [ ] **Step 4: Run the tests**

Run: `cmake --build build/host -j10 && ctest --test-dir build/host --output-on-failure -L "unit|vectors"`
Expected: `100% tests passed, 0 tests failed out of 8`; `test_session` prints `24 Tests 0 Failures 0 Ignored`. `fake_handshake()` asserts that every `prove` signature verifies against the gadget's own public key over the contract's prove text.

- [ ] **Step 5: Commit**

```bash
git add firmware/core firmware/tests/test_session.c firmware/tests/CMakeLists.txt
git commit -m "firmware: session handshake, error reactions, backoff, liveness and screens" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---
### Task 9: Talking — presses, recording, turns, cancel and disconnects

**Files:**
- Create: `firmware/core/src/interaction.c`
- Modify: `firmware/core/src/core.c`, `firmware/core/src/core_internal.h`, `firmware/core/CMakeLists.txt`, `firmware/tests/CMakeLists.txt`
- Test: `firmware/tests/test_turns.c`

**Interfaces:**
- Consumes: `session_send`, `session_send_binary`, `session_ready`, `session_wake`, `g_core_tx` (Task 8); `core_next_turn_id` and `core_text_copy[_tail]` (Task 7).
- Produces (`core_internal.h`): `interaction_init/deinit/input/mic/tick`, `interaction_on_msg` (`heard`, `working`, `reply`, `done`), `interaction_on_session_lost`, `interaction_say(text, turn_out)` (used by Task 10's console), `interaction_turn()` and `interaction_turn_in_flight()` (used by Tasks 11 and 14), and `core_mean_square()` (used by Task 11).
- Behavior (spec §4.4, §5.4):
  - **Pressing:** TALK or a touch hold starts the mic at once and buffers up to 300 ms; a press shorter than 300 ms is ignored and sends nothing. At 300 ms the gadget sends `stop {old turn}` first if a turn is still in flight, then `voice.begin {turn, stream, 16000}`, the buffered frames and live frames (little-endian PCM16, 642-byte binary frames). Release sends `voice.end {turn, ms}`.
  - **Cancel:** CANCEL or a swipe down drops a recording (`voice.drop`), stops a turn in flight once (`stop`), or clears the reply.
  - **The 60 s limit:** the countdown runs 5…1 in the last 5 s, and at 60 s the gadget ends the recording itself.
  - **Turn messages:** `heard`, `working`, `reply` and `done` count only for the current turn. A failed `done` shows its reason with the Maus alerting. The reply returns to Idle 20 s after `done`, or on a tap.
  - **Disconnects:** a dropped session ends the turn with `Connection lost`.
  - **Replaced:** TALK while halted by `replaced` reconnects instead of recording. So does a touch on a touch board, where holding anywhere is TALK (spec §4.3 "until reboot or TALK", §5.4).
  - **Mic level:** linear from −60 dBFS (0) to 0 dBFS (255), in integer 1 dB steps rounded to the nearest dB.

- [ ] **Step 1: Write the failing test**

Create `firmware/tests/test_turns.c`:

```c
/* firmware/tests/test_turns.c */
/* SPDX-License-Identifier: Apache-2.0 */
/* Voice and typed turns (core/src/interaction.c, spec §4.4, §5.4). */
#include <stdlib.h>
#include <string.h>
#include "fake_hal.h"
#include "psa/crypto.h"
#include "unity.h"

void setUp(void) { fake_reset(); }
void tearDown(void) { core_deinit(); }

static char *str_of(cJSON *j, const char *key) {
  static char buf[256];
  snprintf(buf, sizeof buf, "%s", cJSON_GetObjectItem(j, key)->valuestring);
  cJSON_Delete(j);
  return buf;
}

static const char *last_turn(const char *op) { return str_of(fake_ws_last(op), "turn"); }

static void host(const char *fmt, const char *turn) {
  char json[512];
  snprintf(json, sizeof json, fmt, turn);
  fake_ws_in(json);
}

/* Hold TALK for frames x 20 ms with a tone, then release. Returns the turn. */
static const char *talk(int frames) {
  fake_input(GADGET_IN_TALK_DOWN, 0, 0);
  fake_mic_frames(frames, 3000);
  fake_input(GADGET_IN_TALK_UP, 0, 0);
  fake_run(10);
  return last_turn("voice.begin");
}

static void test_a_held_talk_records_and_sends(void) {
  fake_ready("lcd-154");
  fake_input(GADGET_IN_TALK_DOWN, 0, 0);
  TEST_ASSERT_TRUE(fake_mic_running());
  fake_mic_frames(14, 3000); /* 280 ms: not a press yet */
  TEST_ASSERT_EQUAL_size_t(0, fake_ws_count("voice.begin"));
  TEST_ASSERT_EQUAL_INT(UI_SCREEN_IDLE, core_ui_model()->screen);
  fake_mic_frames(36, 3000); /* 1 s in all */
  cJSON *vb = fake_ws_last("voice.begin");
  TEST_ASSERT_NOT_NULL(vb);
  TEST_ASSERT_EQUAL_INT(1, cJSON_GetObjectItem(vb, "stream")->valueint);
  TEST_ASSERT_EQUAL_INT(16000, cJSON_GetObjectItem(vb, "rate")->valueint);
  const char *turn = cJSON_GetObjectItem(vb, "turn")->valuestring;
  TEST_ASSERT_EQUAL_size_t(11, strlen(turn)); /* "t" + 8 hex + "-1" */
  TEST_ASSERT_EQUAL_CHAR('t', turn[0]);
  TEST_ASSERT_EQUAL_STRING("-1", turn + 9);
  cJSON_Delete(vb);
  TEST_ASSERT_EQUAL_INT(UI_SCREEN_LISTENING, core_ui_model()->screen);
  TEST_ASSERT_EQUAL_INT(UI_MAUS_LISTENING, core_ui_model()->maus);
  TEST_ASSERT_TRUE(core_ui_model()->mic_level > 0);
  fake_input(GADGET_IN_TALK_UP, 0, 0);
  fake_run(10);
  TEST_ASSERT_FALSE(fake_mic_running());
  TEST_ASSERT_EQUAL_size_t(50, fake_ws_bin_sent()); /* the 15 buffered frames are sent too */
  size_t len = 0;
  const uint8_t *f = fake_ws_bin(0, &len);
  TEST_ASSERT_EQUAL_size_t(642, len);
  TEST_ASSERT_EQUAL_HEX8(0x01, f[0]);
  TEST_ASSERT_EQUAL_HEX8(1, f[1]);
  /* -3000 little-endian: 0x48 0xF4 */
  TEST_ASSERT_EQUAL_HEX8(0x48, f[2]);
  TEST_ASSERT_EQUAL_HEX8(0xF4, f[3]);
  cJSON *ve = fake_ws_last("voice.end");
  TEST_ASSERT_EQUAL_INT(1000, cJSON_GetObjectItem(ve, "ms")->valueint);
  cJSON_Delete(ve);
  TEST_ASSERT_EQUAL_INT(UI_SCREEN_THINKING, core_ui_model()->screen);
  TEST_ASSERT_EQUAL_INT(UI_MAUS_THINKING, core_ui_model()->maus);
}

static void test_a_short_press_is_ignored(void) {
  fake_ready("amoled-175c");
  fake_input(GADGET_IN_TOUCH_DOWN, 200, 200);
  fake_mic_frames(10, 3000);
  fake_input(GADGET_IN_TOUCH_UP, 200, 200);
  fake_run(500);
  TEST_ASSERT_EQUAL_size_t(0, fake_ws_sent());
  TEST_ASSERT_EQUAL_size_t(0, fake_ws_bin_sent());
  TEST_ASSERT_FALSE(fake_mic_running());
}

static void test_turn_messages_drive_the_screens(void) {
  fake_ready("amoled-175c");
  const char *turn = talk(30);
  char t[33];
  strcpy(t, turn);
  host("{\"op\":\"heard\",\"turn\":\"%s\",\"text\":\"What's on today?\"}", t);
  TEST_ASSERT_EQUAL_STRING("What's on today?", core_ui_model()->thinking.heard);
  host("{\"op\":\"working\",\"turn\":\"%s\",\"text\":\"checking your calendar\"}", t);
  fake_run(10);
  TEST_ASSERT_EQUAL_INT(UI_MAUS_WORKING, core_ui_model()->maus);
  host("{\"op\":\"reply\",\"turn\":\"%s\",\"text\":\"You have\"}", t);
  fake_run(10);
  TEST_ASSERT_EQUAL_INT(UI_SCREEN_REPLY, core_ui_model()->screen);
  TEST_ASSERT_FALSE(core_ui_model()->reply.final);
  host("{\"op\":\"reply\",\"turn\":\"%s\",\"text\":\"You have two meetings.\",\"final\":true}", t);
  host("{\"op\":\"done\",\"turn\":\"%s\",\"outcome\":\"ok\"}", t);
  fake_run(10);
  const ui_model_t *m = core_ui_model();
  TEST_ASSERT_EQUAL_STRING("You have two meetings.", m->reply.text);
  TEST_ASSERT_TRUE(m->reply.final);
  TEST_ASSERT_EQUAL_INT(UI_SCREEN_REPLY, m->screen);
  TEST_ASSERT_EQUAL_INT(UI_MAUS_IDLE, m->maus);
  fake_run(19900);
  TEST_ASSERT_EQUAL_INT(UI_SCREEN_REPLY, core_ui_model()->screen);
  fake_run(200);
  TEST_ASSERT_EQUAL_INT(UI_SCREEN_IDLE, core_ui_model()->screen); /* 20 s after done */
}

static void test_frames_for_other_turns_are_ignored(void) {
  fake_ready("amoled-175c");
  talk(30);
  fake_ws_in("{\"op\":\"reply\",\"turn\":\"t00000000-9\",\"text\":\"not ours\"}");
  fake_ws_in("{\"op\":\"done\",\"turn\":\"t00000000-9\",\"outcome\":\"ok\"}");
  fake_run(10);
  TEST_ASSERT_EQUAL_INT(UI_SCREEN_THINKING, core_ui_model()->screen);
  TEST_ASSERT_EQUAL_STRING("", core_ui_model()->reply.text);
}

static void test_failed_and_stopped_turns(void) {
  fake_ready("amoled-175c");
  char t[33];
  strcpy(t, talk(30));
  host("{\"op\":\"done\",\"turn\":\"%s\",\"outcome\":\"failed\",\"reason\":\"Didn't catch that\"}", t);
  fake_run(10);
  TEST_ASSERT_EQUAL_INT(UI_SCREEN_REPLY, core_ui_model()->screen);
  TEST_ASSERT_EQUAL_INT(UI_MAUS_ALERTING, core_ui_model()->maus);
  TEST_ASSERT_TRUE(core_ui_model()->reply.failed);
  TEST_ASSERT_EQUAL_STRING("Didn't catch that", core_ui_model()->reply.reason);
  strcpy(t, talk(30));
  host("{\"op\":\"done\",\"turn\":\"%s\",\"outcome\":\"stopped\"}", t);
  fake_run(10);
  TEST_ASSERT_EQUAL_INT(UI_SCREEN_IDLE, core_ui_model()->screen); /* nothing to show */
}

static void test_a_long_reply_keeps_its_tail(void) {
  fake_ready("amoled-175c");
  char t[33];
  strcpy(t, talk(30));
  static char json[12000];
  int n = snprintf(json, sizeof json, "{\"op\":\"reply\",\"turn\":\"%s\",\"text\":\"", t);
  for (int i = 0; i < 9990; i++) json[n++] = (char)('a' + i % 26);
  strcpy(json + n, "END\"}");
  fake_ws_in(json);
  const char *text = core_ui_model()->reply.text;
  TEST_ASSERT_EQUAL_size_t(UI_REPLY_MAX - 1, strlen(text));
  TEST_ASSERT_EQUAL_MEMORY("\xe2\x80\xa6", text, 3); /* cut from the start with "…" */
  TEST_ASSERT_EQUAL_STRING("END", text + strlen(text) - 3);
}

static void test_text_outside_the_charset_is_folded(void) {
  fake_ready("amoled-175c");
  char t[33];
  strcpy(t, talk(30));
  host("{\"op\":\"heard\",\"turn\":\"%s\",\"text\":\"caf\xc3\xa9 \xe2\x98\x95 \xe2\x86\x92 ok\xe2\x80\xa6\"}", t);
  TEST_ASSERT_EQUAL_STRING("caf\xc3\xa9 ? \xe2\x86\x92 ok\xe2\x80\xa6", core_ui_model()->thinking.heard);
}

static void test_tap_on_a_reply_goes_back_to_idle(void) {
  fake_ready("amoled-175c");
  char t[33];
  strcpy(t, talk(30));
  host("{\"op\":\"reply\",\"turn\":\"%s\",\"text\":\"Hi\",\"final\":true}", t);
  host("{\"op\":\"done\",\"turn\":\"%s\",\"outcome\":\"ok\"}", t);
  fake_run(10);
  fake_input(GADGET_IN_TOUCH_DOWN, 100, 100);
  fake_run(100);
  fake_input(GADGET_IN_TOUCH_UP, 100, 100);
  fake_run(10);
  TEST_ASSERT_EQUAL_INT(UI_SCREEN_IDLE, core_ui_model()->screen);
}

static void test_cancel_while_recording_drops_the_utterance(void) {
  fake_ready("lcd-154");
  fake_input(GADGET_IN_TALK_DOWN, 0, 0);
  fake_mic_frames(30, 3000);
  fake_input(GADGET_IN_CANCEL_DOWN, 0, 0);
  fake_input(GADGET_IN_CANCEL_UP, 0, 0);
  fake_input(GADGET_IN_TALK_UP, 0, 0);
  fake_run(10);
  cJSON *vb = fake_ws_last("voice.begin");
  cJSON *vd = fake_ws_last("voice.drop");
  TEST_ASSERT_NOT_NULL(vd);
  TEST_ASSERT_EQUAL_STRING(cJSON_GetObjectItem(vb, "turn")->valuestring, cJSON_GetObjectItem(vd, "turn")->valuestring);
  cJSON_Delete(vb);
  cJSON_Delete(vd);
  TEST_ASSERT_EQUAL_size_t(0, fake_ws_count("voice.end"));
  TEST_ASSERT_FALSE(fake_mic_running());
  TEST_ASSERT_EQUAL_INT(UI_SCREEN_IDLE, core_ui_model()->screen);
}

static void test_swipe_down_while_recording_drops_it(void) {
  fake_ready("amoled-175c");
  fake_input(GADGET_IN_TOUCH_DOWN, 233, 150);
  fake_mic_frames(30, 3000);
  TEST_ASSERT_EQUAL_size_t(1, fake_ws_count("voice.begin"));
  fake_input(GADGET_IN_TOUCH_MOVE, 236, 260); /* 110 px down, more than 466 / 8 */
  fake_input(GADGET_IN_TOUCH_UP, 236, 300);
  fake_run(10);
  TEST_ASSERT_EQUAL_size_t(1, fake_ws_count("voice.drop"));
  TEST_ASSERT_EQUAL_size_t(0, fake_ws_count("voice.end"));
}

static void test_cancel_while_thinking_sends_stop_once(void) {
  fake_ready("lcd-154");
  char t[33];
  strcpy(t, talk(30));
  fake_input(GADGET_IN_CANCEL_DOWN, 0, 0);
  fake_swipe(GADGET_SWIPE_DOWN);
  fake_run(10);
  TEST_ASSERT_EQUAL_size_t(1, fake_ws_count("stop"));
  TEST_ASSERT_EQUAL_STRING(t, last_turn("stop"));
}

static void test_recording_stops_at_60_s_with_a_countdown(void) {
  fake_ready("lcd-154");
  fake_input(GADGET_IN_TALK_DOWN, 0, 0);
  for (int i = 0; i < 5; i++) {
    fake_mic_frames(500, 1000); /* 10 s, with the host's ping that keeps the session alive */
    fake_ws_ping_in();
  }
  fake_mic_frames(249, 1000); /* 54.98 s */
  fake_ws_ping_in();
  TEST_ASSERT_EQUAL_UINT8(0, core_ui_model()->listening.countdown_s);
  fake_mic_frames(2, 1000);    /* 55.02 s */
  TEST_ASSERT_EQUAL_UINT8(5, core_ui_model()->listening.countdown_s);
  fake_mic_frames(200, 1000);  /* 59.02 s */
  TEST_ASSERT_EQUAL_UINT8(1, core_ui_model()->listening.countdown_s);
  fake_mic_frames(100, 1000);  /* past 60 s */
  TEST_ASSERT_EQUAL_size_t(1, fake_ws_count("voice.end"));
  cJSON *ve = fake_ws_last("voice.end");
  int ms = cJSON_GetObjectItem(ve, "ms")->valueint;
  cJSON_Delete(ve);
  TEST_ASSERT_TRUE(ms >= 59960 && ms <= 60000);
  TEST_ASSERT_EQUAL_INT(UI_SCREEN_THINKING, core_ui_model()->screen);
  fake_input(GADGET_IN_TALK_UP, 0, 0); /* the late release does nothing */
  fake_run(10);
  TEST_ASSERT_EQUAL_size_t(1, fake_ws_count("voice.end"));
}

static void test_a_new_turn_stops_the_old_one_first(void) {
  fake_ready("lcd-154");
  char old[33];
  strcpy(old, talk(30));
  fake_ws_clear();
  talk(30);
  TEST_ASSERT_EQUAL_STRING(old, str_of(cJSON_Parse(fake_ws_text(0)), "turn")); /* stop for the old turn ... */
  cJSON *first = cJSON_Parse(fake_ws_text(0));
  TEST_ASSERT_EQUAL_STRING("stop", cJSON_GetObjectItem(first, "op")->valuestring);
  cJSON_Delete(first);
  cJSON *second = cJSON_Parse(fake_ws_text(1));
  TEST_ASSERT_EQUAL_STRING("voice.begin", cJSON_GetObjectItem(second, "op")->valuestring); /* ... then the new turn */
  cJSON_Delete(second);
  host("{\"op\":\"reply\",\"turn\":\"%s\",\"text\":\"late\"}", old);
  TEST_ASSERT_EQUAL_STRING("", core_ui_model()->reply.text);
}

static void test_a_dropped_session_ends_the_turn_locally(void) {
  fake_ready("amoled-175c");
  talk(30);
  fake_ws_drop(1006);
  fake_run(10);
  TEST_ASSERT_EQUAL_INT(UI_SCREEN_OFFLINE, core_ui_model()->screen);
  TEST_ASSERT_TRUE(core_ui_model()->reply.failed);
  TEST_ASSERT_EQUAL_STRING("Connection lost", core_ui_model()->reply.reason);
  /* recording when the drop happens: mic stops, nothing resumes */
  fake_run(2000);
  fake_handshake();
  fake_input(GADGET_IN_TALK_DOWN, 0, 0);
  fake_mic_frames(30, 3000);
  fake_ws_drop(1006);
  fake_run(10);
  TEST_ASSERT_FALSE(fake_mic_running());
  fake_input(GADGET_IN_TALK_UP, 0, 0);
  fake_run(10);
  TEST_ASSERT_EQUAL_size_t(0, fake_ws_count("voice.end"));
}

static void test_talk_reconnects_after_replaced(void) {
  fake_ready("lcd-154");
  fake_ws_in("{\"op\":\"error\",\"code\":\"replaced\"}");
  fake_run(5000);
  TEST_ASSERT_EQUAL_INT(1, fake_ws_opens());
  fake_input(GADGET_IN_TALK_DOWN, 0, 0);
  fake_run(10);
  TEST_ASSERT_EQUAL_INT(2, fake_ws_opens());
  TEST_ASSERT_FALSE(fake_mic_running()); /* that press only woke the gadget */
  /* on a touch board, touching the screen is the TALK press */
  fake_reset();
  fake_ready("amoled-175c");
  fake_ws_in("{\"op\":\"error\",\"code\":\"replaced\"}");
  fake_run(5000);
  TEST_ASSERT_EQUAL_INT(1, fake_ws_opens());
  fake_input(GADGET_IN_TOUCH_DOWN, 200, 200);
  fake_run(10);
  TEST_ASSERT_EQUAL_INT(2, fake_ws_opens());
  TEST_ASSERT_FALSE(fake_mic_running());
  fake_input(GADGET_IN_TOUCH_UP, 200, 200);
}

static void test_mic_level_follows_the_signal(void) {
  fake_ready("lcd-154");
  fake_input(GADGET_IN_TALK_DOWN, 0, 0);
  fake_mic_frames(1, 328); /* -40 dBFS */
  TEST_ASSERT_EQUAL_UINT8(85, core_ui_model()->mic_level);
  fake_mic_frames(1, 32767);
  TEST_ASSERT_EQUAL_UINT8(255, core_ui_model()->mic_level);
  fake_mic_frames(1, 0);
  TEST_ASSERT_EQUAL_UINT8(0, core_ui_model()->mic_level);
  fake_input(GADGET_IN_TALK_UP, 0, 0);
}

static void test_nothing_records_without_a_session(void) {
  fake_boot("lcd-154"); /* unpaired */
  fake_input(GADGET_IN_TALK_DOWN, 0, 0);
  fake_mic_frames(30, 3000);
  fake_input(GADGET_IN_TALK_UP, 0, 0);
  TEST_ASSERT_FALSE(fake_mic_running());
  TEST_ASSERT_EQUAL_INT(UI_SCREEN_SETUP, core_ui_model()->screen);
}

int main(void) {
  if (psa_crypto_init() != PSA_SUCCESS) return 3;
  UNITY_BEGIN();
  RUN_TEST(test_a_held_talk_records_and_sends);
  RUN_TEST(test_a_short_press_is_ignored);
  RUN_TEST(test_turn_messages_drive_the_screens);
  RUN_TEST(test_frames_for_other_turns_are_ignored);
  RUN_TEST(test_failed_and_stopped_turns);
  RUN_TEST(test_a_long_reply_keeps_its_tail);
  RUN_TEST(test_text_outside_the_charset_is_folded);
  RUN_TEST(test_tap_on_a_reply_goes_back_to_idle);
  RUN_TEST(test_cancel_while_recording_drops_the_utterance);
  RUN_TEST(test_swipe_down_while_recording_drops_it);
  RUN_TEST(test_cancel_while_thinking_sends_stop_once);
  RUN_TEST(test_recording_stops_at_60_s_with_a_countdown);
  RUN_TEST(test_a_new_turn_stops_the_old_one_first);
  RUN_TEST(test_a_dropped_session_ends_the_turn_locally);
  RUN_TEST(test_talk_reconnects_after_replaced);
  RUN_TEST(test_mic_level_follows_the_signal);
  RUN_TEST(test_nothing_records_without_a_session);
  return UNITY_END();
}
```

Change `firmware/tests/CMakeLists.txt` exactly as this diff shows (`-` lines go, `+` lines come; the rest is context):

```diff
--- a/firmware/tests/CMakeLists.txt
+++ b/firmware/tests/CMakeLists.txt
@@ -27,6 +27,7 @@ gadget_pure_test(layout)
 gadget_pure_test(console_parse)
 gadget_unit_test(core_init)
 gadget_unit_test(session)
+gadget_unit_test(turns)
 
 # protocol/vectors in C (label vectors).
 add_executable(test_vectors test_vectors.c)
```

- [ ] **Step 2: Run it to see it fail**

Run: `cmake --build build/host -j10 && ./build/host/tests/test_turns`
Expected: it builds (the test uses only public API) and fails with `test_a_held_talk_records_and_sends:FAIL: Expected TRUE Was FALSE`, because the mic never starts. The run may then stop with a segmentation fault (exit 139) where a later test reads a frame that was never sent. Either way, it does not pass. Run the binary directly in a terminal; when output is piped, the crash can swallow the `FAIL` line.

- [ ] **Step 3: Write the implementation**

Create `firmware/core/src/interaction.c`:

```c
/* firmware/core/src/interaction.c */
/* SPDX-License-Identifier: Apache-2.0 */
/* Talking (spec §4.4, §5.4): presses, recording and mic frames, the turn in
 * flight and its heard / working / reply / done, typed `say` turns, cancel,
 * the 60 s limit and disconnects. One turn in flight at a time. */
#include <stdlib.h>
#include <string.h>
#include "core_internal.h"

#define TAG "turn"
#define PRE_FRAMES (GADGET_PRESS_MIN_MS / 20u) /* mic frames buffered before a press counts */

typedef enum { PRESS_NONE = 0, PRESS_TALK, PRESS_TOUCH } press_t;

static struct {
  press_t press;
  uint64_t press_at;
  int16_t x0, y0;
  bool moved;          /* the touch moved past the swipe threshold */
  bool rec;            /* voice.begin sent, frames streaming */
  char rec_turn[GADGET_TURN_MAX + 1];
  uint8_t rec_stream;
  uint8_t next_stream;
  uint32_t rec_frames;
  int16_t pre[PRE_FRAMES][GADGET_MIC_FRAME_SAMPLES];
  uint8_t pre_n;
  char turn[GADGET_TURN_MAX + 1]; /* the current turn: its frames are shown */
  bool in_flight;      /* sent, no done yet */
  bool stop_sent;
  bool reply_shown;
  uint64_t reply_until;  /* back to idle at this time; 0 = no timer */
} I;

/* Mean-square thresholds for -60 .. 0 dBFS in 1 dB steps, each at the half
 * dB below (32768^2 * 10^((dB - 0.5) / 10)) so levels round to the nearest dB.
 * Integer-only: identical on every platform. */
static const uint32_t DB_MS[61] = {
    957, 1205, 1517, 1909, 2404, 3026, 3810, 4796, 6038,
    7602, 9570, 12048, 15167, 19094, 24038, 30262, 38098, 47962,
    60381, 76015, 95697, 120476, 151670, 190941, 240381, 302622, 380978,
    479623, 603809, 760151, 956973, 1204758, 1516701, 1909413, 2403809, 3026216,
    3809780, 4796229, 6038094, 7601510, 9569734, 12047581, 15167006, 19094130, 24038085,
    30262156, 38097798, 47962285, 60380940, 76015100, 95697341, 120475814, 151670064, 190941298,
    240380852, 302621563, 380977976, 479622855, 603809400, 760150998, 956973408,
};

uint32_t core_mean_square(const int16_t *pcm, size_t n) {
  if (n == 0) return 0;
  uint64_t sum = 0;
  for (size_t i = 0; i < n; i++) sum += (uint64_t)((int32_t)pcm[i] * (int32_t)pcm[i]);
  return (uint32_t)(sum / n);
}

/* Linear from -60 dBFS (0) to 0 dBFS (255). */
static uint8_t mic_level(uint32_t ms) {
  int d = -1;
  for (int i = 0; i < 61; i++) {
    if (ms >= DB_MS[i]) d = i;
  }
  return d < 0 ? 0 : (uint8_t)(d * 255 / 60);
}

/* ---- sending --------------------------------------------------------------------- */

static void send_stop(const char *turn) {
  gp_stop_t s = {.turn = turn};
  session_send("stop", g_core_tx, gp_encode_stop(g_core_tx, sizeof g_core_tx, &s));
}

static void send_mic_frame(const int16_t *pcm) {
  uint8_t frame[2 + GADGET_MIC_FRAME_SAMPLES * 2];
  uint8_t payload[GADGET_MIC_FRAME_SAMPLES * 2];
  for (size_t i = 0; i < GADGET_MIC_FRAME_SAMPLES; i++) {
    uint16_t v = (uint16_t)pcm[i];
    payload[2 * i] = (uint8_t)(v & 0xff); /* little-endian on the wire */
    payload[2 * i + 1] = (uint8_t)(v >> 8);
  }
  size_t n = gp_bin_encode(frame, sizeof frame, GP_BIN_MIC, I.rec_stream, payload, sizeof payload);
  if (n > 0 && session_send_binary(frame, n) == GADGET_OK) I.rec_frames++;
}

/* ---- model ------------------------------------------------------------------------ */

static void clear_turn_model(void) {
  ui_model_t *m = &g_core.model;
  memset(&m->thinking, 0, sizeof m->thinking);
  memset(&m->reply, 0, sizeof m->reply);
  I.reply_shown = false;
  I.reply_until = 0;
}

static void publish(void) {
  ui_model_t *m = &g_core.model;
  g_core.f.recording = I.rec;
  g_core.f.turn_active = I.in_flight && m->reply.text[0] == '\0' && !m->reply.failed;
  g_core.f.reply_visible = I.reply_shown;
  if (!I.rec) {
    m->listening.countdown_s = 0;
    if (I.press == PRESS_NONE) m->mic_level = 0;
  }
}

/* ---- recording ---------------------------------------------------------------------- */

static void end_press(void) {
  if (I.press != PRESS_NONE) hal_mic_stop();
  I.press = PRESS_NONE;
  I.pre_n = 0;
  I.moved = false;
}

static void start_recording(void) {
  if (I.in_flight && !I.stop_sent) send_stop(I.turn); /* one turn in flight */
  I.in_flight = false;
  core_next_turn_id(I.rec_turn);
  memcpy(I.turn, I.rec_turn, sizeof I.turn);
  I.next_stream = (uint8_t)(I.next_stream % 255u + 1u);
  I.rec_stream = I.next_stream;
  I.rec_frames = 0;
  clear_turn_model();
  gp_voice_begin_t vb = {.turn = I.rec_turn, .stream = I.rec_stream, .rate = GADGET_MIC_RATE};
  if (session_send("voice.begin", g_core_tx, gp_encode_voice_begin(g_core_tx, sizeof g_core_tx, &vb)) != GADGET_OK) {
    end_press();
    return;
  }
  I.rec = true;
  g_core.model.listening.started_ms = I.press_at;
  for (uint8_t i = 0; i < I.pre_n; i++) send_mic_frame(I.pre[i]);
  I.pre_n = 0;
  hal_log(GADGET_LOG_INFO, TAG, "recording %s", I.rec_turn);
}

static void finish_recording(void) {
  gp_voice_end_t ve = {.turn = I.rec_turn, .ms = I.rec_frames * 20u};
  session_send("voice.end", g_core_tx, gp_encode_voice_end(g_core_tx, sizeof g_core_tx, &ve));
  I.rec = false;
  I.in_flight = true;
  I.stop_sent = false;
  hal_mic_stop();
}

static void cancel_recording(void) {
  if (I.rec) {
    gp_voice_drop_t vd = {.turn = I.rec_turn};
    session_send("voice.drop", g_core_tx, gp_encode_voice_drop(g_core_tx, sizeof g_core_tx, &vd));
    I.rec = false;
    I.turn[0] = '\0';
  }
  end_press(); /* the release that follows finds no press and does nothing */
}

static void begin_press(press_t src, int16_t x, int16_t y) {
  if (I.press != PRESS_NONE) return;
  I.press = src;
  I.press_at = g_core.now;
  I.x0 = x;
  I.y0 = y;
  I.moved = false;
  I.pre_n = 0;
  if (hal_mic_start(GADGET_MIC_RATE) != GADGET_OK) hal_log(GADGET_LOG_ERROR, TAG, "mic did not start");
}

static void release_press(press_t src) {
  if (I.press != src) return;
  if (I.rec) finish_recording();
  end_press();
}

/* CANCEL, or a swipe down: the most recent thing gives way. */
static void cancel_action(void) {
  if (I.press != PRESS_NONE || I.rec) {
    cancel_recording();
    return;
  }
  if (I.in_flight) {
    if (!I.stop_sent) {
      send_stop(I.turn);
      I.stop_sent = true;
    }
    return;
  }
  if (I.reply_shown) clear_turn_model();
}

/* A touch shorter than the press minimum. */
static void tap(int16_t x, int16_t y) {
  (void)x;
  (void)y;
  if (I.reply_shown && !I.in_flight) clear_turn_model();
}

static int16_t swipe_threshold(void) { return (int16_t)(g_core.board->screen_h / 8); }

/* ---- module API ---------------------------------------------------------------------- */

void interaction_init(void) { memset(&I, 0, sizeof I); }

void interaction_deinit(void) {
  if (I.press != PRESS_NONE) hal_mic_stop();
  memset(&I, 0, sizeof I);
}

void interaction_input(const gadget_input_t *in) {
  bool ready = session_ready() && !g_core.f.ota_active;
  switch (in->type) {
    case GADGET_IN_TALK_DOWN:
      if (!session_ready()) {
        session_wake();
        break;
      }
      if (ready) begin_press(PRESS_TALK, 0, 0);
      break;
    case GADGET_IN_TALK_UP:
      release_press(PRESS_TALK);
      break;
    case GADGET_IN_TOUCH_DOWN:
      if (!session_ready()) {
        session_wake(); /* hold anywhere is TALK on touch boards (spec §5.4) */
        break;
      }
      if (ready) begin_press(PRESS_TOUCH, in->x, in->y);
      break;
    case GADGET_IN_TOUCH_MOVE:
      if (I.press == PRESS_TOUCH && !I.moved) {
        int dx = in->x - I.x0, dy = in->y - I.y0;
        int th = swipe_threshold();
        if (abs(dx) > th || abs(dy) > th) {
          I.moved = true;
          if (dy > th && dy > abs(dx)) cancel_action(); /* swipe down */
        }
      }
      break;
    case GADGET_IN_TOUCH_UP:
      if (I.press == PRESS_TOUCH) {
        bool short_press = !I.rec && !I.moved && g_core.now - I.press_at < GADGET_PRESS_MIN_MS;
        release_press(PRESS_TOUCH);
        if (short_press) tap(in->x, in->y);
      }
      break;
    case GADGET_IN_CANCEL_DOWN:
      cancel_action();
      break;
    case GADGET_IN_SWIPE:
      if (in->dir == GADGET_SWIPE_DOWN) cancel_action();
      break;
    default:
      break;
  }
  publish();
}

void interaction_mic(const gadget_mic_frame_t *f) {
  if (I.press == PRESS_NONE || f->samples != GADGET_MIC_FRAME_SAMPLES) return;
  g_core.model.mic_level = mic_level(core_mean_square(f->pcm, f->samples));
  if (I.rec) {
    send_mic_frame(f->pcm);
  } else if (I.pre_n < PRE_FRAMES) {
    memcpy(I.pre[I.pre_n++], f->pcm, sizeof I.pre[0]);
  } else {
    memmove(I.pre[0], I.pre[1], sizeof I.pre[0] * (PRE_FRAMES - 1)); /* keep the newest */
    memcpy(I.pre[PRE_FRAMES - 1], f->pcm, sizeof I.pre[0]);
  }
}

void interaction_tick(void) {
  uint64_t now = g_core.now;
  if (I.press != PRESS_NONE && !I.rec && !I.moved && now - I.press_at >= GADGET_PRESS_MIN_MS) {
    start_recording();
  }
  if (I.rec) {
    uint64_t elapsed = now - I.press_at;
    if (elapsed >= GADGET_UTTERANCE_MAX_MS) {
      hal_log(GADGET_LOG_INFO, TAG, "60 s limit reached");
      finish_recording();
      end_press(); /* the release that follows does nothing */
    } else if (elapsed + GADGET_COUNTDOWN_MS >= GADGET_UTTERANCE_MAX_MS) {
      g_core.model.listening.countdown_s = (uint8_t)((GADGET_UTTERANCE_MAX_MS - elapsed + 999u) / 1000u);
    }
  }
  if (I.reply_shown && !I.in_flight && I.reply_until != 0 && now >= I.reply_until) clear_turn_model();
  publish();
}

bool interaction_on_msg(const gp_msg_t *m) {
  ui_model_t *mod = &g_core.model;
  switch (m->op) {
    case GP_OP_HEARD:
      if (I.in_flight && strcmp(m->m.heard.turn, I.turn) == 0) {
        core_text_copy(mod->thinking.heard, sizeof mod->thinking.heard, m->m.heard.text);
      }
      break;
    case GP_OP_WORKING:
      if (I.in_flight && strcmp(m->m.working.turn, I.turn) == 0) {
        core_text_copy(mod->thinking.working, sizeof mod->thinking.working, m->m.working.text);
      }
      break;
    case GP_OP_REPLY:
      if (I.turn[0] != '\0' && strcmp(m->m.reply.turn, I.turn) == 0 && (I.in_flight || I.reply_shown)) {
        core_text_copy_tail(mod->reply.text, sizeof mod->reply.text, m->m.reply.text);
        mod->reply.final = m->m.reply.final;
        I.reply_shown = mod->reply.text[0] != '\0';
      }
      break;
    case GP_OP_DONE:
      if (I.in_flight && strcmp(m->m.done.turn, I.turn) == 0) {
        I.in_flight = false;
        I.stop_sent = false;
        if (m->m.done.outcome == GP_OUTCOME_FAILED) {
          mod->reply.failed = true;
          core_text_copy(mod->reply.reason, sizeof mod->reply.reason, m->m.done.reason ? m->m.done.reason : "");
          I.reply_shown = true;
        } else {
          mod->reply.final = true;
          I.reply_shown = mod->reply.text[0] != '\0';
        }
        I.reply_until = g_core.now + GADGET_REPLY_IDLE_MS;
      }
      break;
    default:
      return false;
  }
  publish();
  return true;
}

void interaction_on_session_lost(void) {
  bool had_turn = I.rec || I.in_flight;
  end_press();
  I.rec = false;
  I.in_flight = false;
  I.stop_sent = false;
  if (had_turn) {
    ui_model_t *m = &g_core.model;
    m->reply.failed = true;
    core_text_copy(m->reply.reason, sizeof m->reply.reason, "Connection lost");
    I.reply_shown = true;
    I.reply_until = g_core.now + GADGET_REPLY_IDLE_MS;
  }
  publish();
}

bool interaction_say(const char *text, char turn_out[GADGET_TURN_MAX + 1]) {
  if (!session_ready()) return false;
  if (I.press != PRESS_NONE || I.rec) cancel_recording();
  if (I.in_flight && !I.stop_sent) send_stop(I.turn);
  clear_turn_model();
  core_next_turn_id(I.turn);
  gp_say_t s = {.turn = I.turn, .text = text};
  if (session_send("say", g_core_tx, gp_encode_say(g_core_tx, sizeof g_core_tx, &s)) != GADGET_OK) return false;
  I.in_flight = true;
  I.stop_sent = false;
  core_text_copy(g_core.model.thinking.heard, sizeof g_core.model.thinking.heard, text);
  memcpy(turn_out, I.turn, GADGET_TURN_MAX + 1);
  publish();
  return true;
}

const char *interaction_turn(void) { return I.turn; }
bool interaction_turn_in_flight(void) { return I.in_flight; }
```

Change `firmware/core/src/core_internal.h` exactly as this diff shows (`-` lines go, `+` lines come; the rest is context):

```diff
--- a/firmware/core/src/core_internal.h
+++ b/firmware/core/src/core_internal.h
@@ -101,6 +101,21 @@ const char *session_last_error(void);
 const char *session_host_in_use(void);          /* "addr:port", or NULL when unknown */
 bool session_is_setup(void);                    /* never paired (no host_id) */
 
+/* ---- interaction.c ------------------------------------------------------------ */
+void interaction_init(void);
+void interaction_deinit(void);
+void interaction_input(const gadget_input_t *in);
+void interaction_mic(const gadget_mic_frame_t *f);
+void interaction_tick(void);
+bool interaction_on_msg(const gp_msg_t *m);     /* heard, working, reply, done; false when not ours */
+void interaction_on_session_lost(void);
+/* `say`: start a typed turn; false when there is no session. */
+bool interaction_say(const char *text, char turn_out[GADGET_TURN_MAX + 1]);
+const char *interaction_turn(void);             /* the current turn id, "" when none */
+bool interaction_turn_in_flight(void);          /* sent and no done yet */
+/* Mean square of PCM16 samples (shared by the mic and speaker levels). */
+uint32_t core_mean_square(const int16_t *pcm, size_t n);
+
 /* ---- screens.c -------------------------------------------------------------- */
 void screens_update(void);
 
```

Change `firmware/core/src/core.c` exactly as this diff shows (`-` lines go, `+` lines come; the rest is context):

```diff
--- a/firmware/core/src/core.c
+++ b/firmware/core/src/core.c
@@ -214,6 +214,7 @@ gadget_status_t core_init(const core_config_t *cfg) {
 
   publish_identity();
   session_init();
+  interaction_init();
   if (g_core.wifi_ssid[0] != '\0') hal_wifi_connect(g_core.wifi_ssid, g_core.wifi_pass);
 
   cJSON *boot = cJSON_CreateObject();
@@ -232,6 +233,12 @@ gadget_status_t core_init(const core_config_t *cfg) {
 void core_event(const gadget_event_t *ev) {
   if (!g_core.initialized || ev == NULL) return;
   switch (ev->type) {
+    case GADGET_EV_INPUT:
+      interaction_input(&ev->u.input);
+      break;
+    case GADGET_EV_MIC_FRAME:
+      interaction_mic(&ev->u.mic);
+      break;
     case GADGET_EV_WIFI_STATE:
     case GADGET_EV_WS_OPEN:
     case GADGET_EV_WS_TEXT:
@@ -254,6 +261,7 @@ void core_tick(uint64_t now_ms) {
   g_core.model.now_ms = now_ms;
   g_core.ticked = true;
   session_tick();
+  interaction_tick();
   screens_update();
   model_commit();
 }
@@ -261,7 +269,10 @@ void core_tick(uint64_t now_ms) {
 const ui_model_t *core_ui_model(void) { return &g_core.model; }
 
 void core_deinit(void) {
-  if (g_core.initialized) session_deinit();
+  if (g_core.initialized) {
+    interaction_deinit();
+    session_deinit();
+  }
   memset(&g_core, 0, sizeof g_core);
   memset(&s_shadow, 0, sizeof s_shadow);
 }
@@ -284,9 +295,13 @@ void core_tap(core_tap_dir_t dir, const char *op, const char *json, size_t len)
 
 void core_on_ready(void) { hal_log(GADGET_LOG_INFO, CORE_TAG, "talking to %s", g_core.bot_name); }
 
-void core_on_session_lost(void) { hal_log(GADGET_LOG_INFO, CORE_TAG, "session lost"); }
+void core_on_session_lost(void) {
+  hal_log(GADGET_LOG_INFO, CORE_TAG, "session lost");
+  interaction_on_session_lost();
+}
 
 void core_on_msg(const gp_msg_t *m) {
+  if (interaction_on_msg(m)) return;
   switch (m->op) {
     default:
       hal_log(GADGET_LOG_DEBUG, CORE_TAG, "ignored %s", gp_op_name(m->op));
```

Change `firmware/core/CMakeLists.txt` exactly as this diff shows (`-` lines go, `+` lines come; the rest is context):

```diff
--- a/firmware/core/CMakeLists.txt
+++ b/firmware/core/CMakeLists.txt
@@ -12,6 +12,7 @@ set(GADGET_CORE_SRCS
   src/core.c
   src/session.c
   src/screens.c
+  src/interaction.c
 )
 
 if(ESP_PLATFORM)
```

- [ ] **Step 4: Run the tests**

Run: `cmake --build build/host -j10 && ctest --test-dir build/host --output-on-failure -L "unit|vectors"`
Expected: `100% tests passed, 0 tests failed out of 9`; `test_turns` prints `17 Tests 0 Failures 0 Ignored`. The 60 s test sends a ping every 10 s, as a real host does every 15 s; without pings, the 45 s liveness timer from Task 8 rightly drops the session first.

- [ ] **Step 5: Commit**

```bash
git add firmware/core firmware/tests/test_turns.c firmware/tests/CMakeLists.txt
git commit -m "firmware: voice and typed turns, cancel, 60 s limit, disconnects" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 10: Console commands and `@omb` lines

**Files:**
- Create: `firmware/core/src/console_cmd.c`
- Modify: `firmware/core/src/core.c`, `firmware/core/src/core_internal.h`, `firmware/core/CMakeLists.txt`, `firmware/tests/CMakeLists.txt`
- Test: `firmware/tests/test_console.c`; `firmware/tests/test_session.c` (the `status` line during and after a `device_limit` window)

**Interfaces:**
- Consumes: `gadget_console_parse` (Task 6); the `session_*` calls (Task 8); `interaction_say` (Task 9).
- Produces: `console_exec_line(line)` (NULL means an over-long line) and `console_on_scan(scan)`. These are wired to `GADGET_EV_CONSOLE_LINE` and `GADGET_EV_WIFI_SCAN`.
- The command table (contract §2.11):

| Command | Does |
|---|---|
| `wifi` | Stores and calls `hal_wifi_connect` |
| `scan` | `hal_wifi_scan`, then one `@omb scan` line |
| `host auto` | Erases `host_addr`, drops the last challenge's host name and reconnects through mDNS |
| `host <a>[:p]` | Stores `addr:port`, drops the last challenge's host name and reconnects |
| `pair <code>` | Stores the code, clears the error state and reconnects at once |
| `name` | Stores the name (through `core_set_name`); the next `hello` carries it |
| `say` | Sends a `say` turn and prints `@omb {"op":"say","turn":…}`; without a session, prints `@omb {"op":"error","cmd":"say","message":"not connected to MausBot"}` |
| `status` | Prints the `@omb status` line |
| `log off` / `log on` | `hal_log_set_enabled`; `@omb` lines always print |
| `forget` | Drops the last challenge's host name, `hal_storage_erase_all()`, then `hal_restart()` |
| `reboot` | `hal_restart()` |

  Errors print `@omb {"op":"error","cmd":…,"message":…}`. The `status` field order is exactly: `op`, `wifi`, `ssid`?, `host`?, `id`, `pair`, `error`?, `fw`, `battery`?, `board`, `name`, `host_name`?. P2d's installer and the end-to-end runner read these lines.
- `pair` and `error` come from `session_pair_state()` and `session_last_error()` (Task 8). Through a `device_limit` window, retries included, `status` prints `"pair":"error","error":"device_limit"`. Once the window closes and the code is cleared, a gadget that was never paired prints `"pair":"unpaired"` with no `error` field (contract §2.11, status rule 2). `test_device_limit_retries_every_10_s_for_120_s` checks both lines.

- [ ] **Step 1: Write the failing test**

Create `firmware/tests/test_console.c`:

```c
/* firmware/tests/test_console.c */
/* SPDX-License-Identifier: Apache-2.0 */
/* Console commands and @omb lines (core/src/console_cmd.c, spec §5.6). */
#include <string.h>
#include "fake_hal.h"
#include "psa/crypto.h"
#include "unity.h"

void setUp(void) { fake_reset(); }
void tearDown(void) { core_deinit(); }

static const char *last_line(void) { return fake_console_line(fake_console_count() - 1); }

static void test_status_of_a_fresh_gadget(void) {
  fake_battery_set(true, 82, false);
  fake_boot("amoled-175c");
  fake_console_in("status");
  char want[400];
  snprintf(want, sizeof want,
           "@omb {\"op\":\"status\",\"wifi\":\"connected\",\"id\":\"%s\",\"pair\":\"unpaired\",\"fw\":\"1.0.0\","
           "\"battery\":{\"pct\":82,\"charging\":false},\"board\":\"amoled-175c\",\"name\":\"Maus %.4s\"}",
           core_device_id(), core_device_id() + 4);
  TEST_ASSERT_EQUAL_STRING(want, last_line());
}

static void test_status_of_a_paired_gadget(void) {
  fake_storage_put(GADGET_KEY_WIFI_SSID, "Home");
  fake_ready("devkit");
  fake_console_in("status");
  TEST_ASSERT_EQUAL_STRING(
      "@omb {\"op\":\"status\",\"wifi\":\"connected\",\"ssid\":\"Home\",\"host\":\"127.0.0.1:8810\",\"id\":\"" FAKE_RFC_ID
      "\",\"pair\":\"paired\",\"fw\":\"1.0.0\",\"board\":\"devkit\",\"name\":\"Maus b18b\",\"host_name\":\"Mac\"}",
      last_line());
}

static void test_status_after_bad_code_then_a_new_pair(void) {
  fake_storage_put(GADGET_KEY_HOST_ADDR, "192.168.1.20:8810");
  fake_boot("lcd-154");
  fake_console_in("pair 111111");
  fake_run(10);
  fake_ws_accept();
  fake_ws_in("{\"op\":\"challenge\",\"nonce\":\"" FAKE_NONCE "\",\"host_id\":\"" FAKE_HOST_ID "\",\"host_name\":\"Mac\"}");
  fake_ws_in("{\"op\":\"error\",\"code\":\"bad_code\"}");
  fake_run(10);
  fake_console_in("status");
  cJSON *st = fake_omb("status");
  TEST_ASSERT_EQUAL_STRING("error", cJSON_GetObjectItem(st, "pair")->valuestring);
  TEST_ASSERT_EQUAL_STRING("bad_code", cJSON_GetObjectItem(st, "error")->valuestring);
  TEST_ASSERT_EQUAL_STRING("Mac", cJSON_GetObjectItem(st, "host_name")->valuestring); /* from the challenge */
  cJSON_Delete(st);
  int opens = fake_ws_opens();
  fake_console_in("pair 222222");
  fake_run(10);
  TEST_ASSERT_EQUAL_INT(opens + 1, fake_ws_opens()); /* reconnects at once */
  fake_console_in("status");
  st = fake_omb("status");
  TEST_ASSERT_EQUAL_STRING("connecting", cJSON_GetObjectItem(st, "pair")->valuestring);
  TEST_ASSERT_NULL(cJSON_GetObjectItem(st, "error"));
  cJSON_Delete(st);
  fake_ws_accept();
  fake_ws_in("{\"op\":\"challenge\",\"nonce\":\"" FAKE_NONCE "\",\"host_id\":\"" FAKE_HOST_ID "\",\"host_name\":\"Mac\"}");
  cJSON *prove = fake_ws_last("prove");
  TEST_ASSERT_EQUAL_STRING("222222", cJSON_GetObjectItem(prove, "enroll")->valuestring);
  cJSON_Delete(prove);
}

static void test_code_stored_before_a_host_is_known(void) {
  fake_wifi_set(GADGET_WIFI_OFF);
  fake_boot("lcd-154");
  fake_console_in("pair 123456");
  TEST_ASSERT_EQUAL_STRING("123456", fake_storage_str(GADGET_KEY_PAIR_CODE));
  fake_console_in("status");
  cJSON *st = fake_omb("status");
  TEST_ASSERT_EQUAL_STRING("code_stored", cJSON_GetObjectItem(st, "pair")->valuestring);
  TEST_ASSERT_EQUAL_STRING("off", cJSON_GetObjectItem(st, "wifi")->valuestring);
  cJSON_Delete(st);
}

static void test_wifi_host_and_name_are_stored(void) {
  fake_boot("lcd-154");
  fake_console_in("wifi \"My Home\" \"pass word\"");
  TEST_ASSERT_EQUAL_STRING("My Home", fake_storage_str(GADGET_KEY_WIFI_SSID));
  TEST_ASSERT_EQUAL_STRING("pass word", fake_storage_str(GADGET_KEY_WIFI_PASS));
  TEST_ASSERT_EQUAL_STRING("My Home", fake_wifi_ssid());
  fake_console_in("host omkars-mac.local:9000");
  TEST_ASSERT_EQUAL_STRING("omkars-mac.local:9000", fake_storage_str(GADGET_KEY_HOST_ADDR));
  fake_console_in("name \"Desk Maus\"");
  TEST_ASSERT_EQUAL_STRING("Desk Maus", fake_storage_str(GADGET_KEY_NAME));
  TEST_ASSERT_EQUAL_STRING("Desk Maus", core_ui_model()->device_name);
  fake_console_in("pair 123456");
  fake_run(10);
  TEST_ASSERT_EQUAL_STRING("omkars-mac.local", fake_ws_host());
  TEST_ASSERT_EQUAL_UINT16(9000, fake_ws_port());
  fake_ws_accept();
  cJSON *hello = fake_ws_last("hello");
  TEST_ASSERT_EQUAL_STRING("Desk Maus", cJSON_GetObjectItem(hello, "name")->valuestring);
  cJSON_Delete(hello);
  fake_console_in("host auto");
  fake_run(10);
  TEST_ASSERT_FALSE(fake_storage_has(GADGET_KEY_HOST_ADDR));
  TEST_ASSERT_EQUAL_INT(1, fake_mdns_browses()); /* the open session was dropped and mDNS asked */
}

static void test_host_and_host_auto_drop_the_challenge_host_name(void) {
  static const char *CHALLENGE_MAC =
      "{\"op\":\"challenge\",\"nonce\":\"" FAKE_NONCE "\",\"host_id\":\"" FAKE_HOST_ID "\",\"host_name\":\"Mac\"}";
  fake_storage_put(GADGET_KEY_HOST_ADDR, "192.168.1.20:8810");
  fake_storage_put(GADGET_KEY_PAIR_CODE, "111111");
  fake_boot("lcd-154");
  fake_ws_accept();
  fake_ws_in(CHALLENGE_MAC);
  fake_ws_in("{\"op\":\"error\",\"code\":\"bad_code\"}");
  fake_run(10);
  TEST_ASSERT_EQUAL_STRING("Mac", core_ui_model()->host_name);
  fake_console_in("pair 222222");
  fake_run(10);
  TEST_ASSERT_EQUAL_STRING("Mac", core_ui_model()->host_name); /* a new code keeps it: same host */
  fake_console_in("host 192.168.1.30");
  fake_run(10);
  TEST_ASSERT_EQUAL_STRING("", core_ui_model()->host_name); /* another host: its name is not known yet */
  TEST_ASSERT_EQUAL_STRING("192.168.1.30", fake_ws_host());
  fake_ws_accept();
  fake_ws_in(CHALLENGE_MAC);
  TEST_ASSERT_EQUAL_STRING("Mac", core_ui_model()->host_name);
  fake_console_in("host auto");
  fake_run(10);
  TEST_ASSERT_EQUAL_STRING("", core_ui_model()->host_name);
}

static void test_say_sends_a_typed_turn(void) {
  fake_boot("lcd-154");
  fake_console_in("say \"hello\"");
  TEST_ASSERT_EQUAL_STRING("@omb {\"op\":\"error\",\"cmd\":\"say\",\"message\":\"not connected to MausBot\"}",
                           last_line());
  fake_reset();
  fake_ready("lcd-154");
  fake_console_in("say \"What's on today?\"");
  cJSON *say = fake_ws_last("say");
  TEST_ASSERT_NOT_NULL(say);
  TEST_ASSERT_EQUAL_STRING("What's on today?", cJSON_GetObjectItem(say, "text")->valuestring);
  cJSON *line = fake_omb("say");
  TEST_ASSERT_EQUAL_STRING(cJSON_GetObjectItem(say, "turn")->valuestring, cJSON_GetObjectItem(line, "turn")->valuestring);
  cJSON_Delete(say);
  cJSON_Delete(line);
  fake_run(10);
  TEST_ASSERT_EQUAL_INT(UI_SCREEN_THINKING, core_ui_model()->screen);
  TEST_ASSERT_EQUAL_STRING("What's on today?", core_ui_model()->thinking.heard);
}

static void test_scan_prints_one_line(void) {
  fake_boot("lcd-154");
  fake_console_in("scan");
  TEST_ASSERT_EQUAL_INT(1, fake_wifi_scans());
  gadget_wifi_ap_t aps[2] = {{"Home", -52, GADGET_AUTH_WPA2}, {"Cafe", -80, GADGET_AUTH_OPEN}};
  fake_wifi_scan_result(aps, 2, true);
  TEST_ASSERT_EQUAL_STRING("@omb {\"op\":\"scan\",\"networks\":[{\"ssid\":\"Home\",\"rssi\":-52,\"auth\":\"wpa2\"},"
                           "{\"ssid\":\"Cafe\",\"rssi\":-80,\"auth\":\"open\"}]}",
                           last_line());
  fake_wifi_scan_result(NULL, 0, false);
  TEST_ASSERT_EQUAL_STRING("@omb {\"op\":\"scan\",\"networks\":[]}", last_line());
}

static void test_errors_are_reported(void) {
  fake_boot("lcd-154");
  fake_console_in("pair 12");
  TEST_ASSERT_EQUAL_STRING("@omb {\"op\":\"error\",\"cmd\":\"pair\",\"message\":\"pair needs a six-digit code\"}",
                           last_line());
  fake_console_in("dance");
  TEST_ASSERT_EQUAL_STRING("@omb {\"op\":\"error\",\"cmd\":\"dance\",\"message\":\"unknown command; try status\"}",
                           last_line());
  size_t n = fake_console_count();
  fake_console_in("");
  TEST_ASSERT_EQUAL_size_t(n, fake_console_count()); /* an empty line prints nothing */
  core_event(&(gadget_event_t){.type = GADGET_EV_CONSOLE_LINE, .u.console.line = NULL});
  TEST_ASSERT_EQUAL_STRING("@omb {\"op\":\"error\",\"cmd\":\"\",\"message\":\"line too long\"}", last_line());
}

static void test_log_off_and_on(void) {
  fake_boot("lcd-154");
  fake_console_in("log off");
  TEST_ASSERT_FALSE(fake_log_enabled());
  fake_console_in("status"); /* @omb lines still print */
  TEST_ASSERT_NOT_NULL(fake_omb("status"));
  fake_console_in("log on");
  TEST_ASSERT_TRUE(fake_log_enabled());
}

static void test_forget_erases_everything_and_restarts(void) {
  fake_storage_put(GADGET_KEY_WIFI_SSID, "Home");
  fake_ready("amoled-175c");
  FAKE_EXPECT_RESTART(fake_console_in("forget"));
  TEST_ASSERT_EQUAL_INT(1, fake_restarts());
  TEST_ASSERT_FALSE(fake_storage_has(GADGET_KEY_DEV_KEY));
  TEST_ASSERT_FALSE(fake_storage_has(GADGET_KEY_HOST_ID));
  TEST_ASSERT_FALSE(fake_storage_has(GADGET_KEY_WIFI_SSID));
  fake_boot("amoled-175c"); /* a new identity */
  TEST_ASSERT_TRUE(strcmp(FAKE_RFC_ID, core_device_id()) != 0);
  TEST_ASSERT_EQUAL_INT(CORE_PAIR_UNPAIRED, core_pair_state());
}

static void test_reboot_keeps_storage(void) {
  fake_ready("amoled-175c");
  FAKE_EXPECT_RESTART(fake_console_in("reboot"));
  TEST_ASSERT_TRUE(fake_storage_has(GADGET_KEY_HOST_ID));
}

int main(void) {
  if (psa_crypto_init() != PSA_SUCCESS) return 3;
  UNITY_BEGIN();
  RUN_TEST(test_status_of_a_fresh_gadget);
  RUN_TEST(test_status_of_a_paired_gadget);
  RUN_TEST(test_status_after_bad_code_then_a_new_pair);
  RUN_TEST(test_code_stored_before_a_host_is_known);
  RUN_TEST(test_wifi_host_and_name_are_stored);
  RUN_TEST(test_host_and_host_auto_drop_the_challenge_host_name);
  RUN_TEST(test_say_sends_a_typed_turn);
  RUN_TEST(test_scan_prints_one_line);
  RUN_TEST(test_errors_are_reported);
  RUN_TEST(test_log_off_and_on);
  RUN_TEST(test_forget_erases_everything_and_restarts);
  RUN_TEST(test_reboot_keeps_storage);
  return UNITY_END();
}
```

Change `firmware/tests/CMakeLists.txt` exactly as this diff shows (`-` lines go, `+` lines come; the rest is context):

```diff
--- a/firmware/tests/CMakeLists.txt
+++ b/firmware/tests/CMakeLists.txt
@@ -28,6 +28,7 @@ gadget_pure_test(console_parse)
 gadget_unit_test(core_init)
 gadget_unit_test(session)
 gadget_unit_test(turns)
+gadget_unit_test(console)
 
 # protocol/vectors in C (label vectors).
 add_executable(test_vectors test_vectors.c)
```

Extend Task 8's `test_device_limit_retries_every_10_s_for_120_s` so it reads the `status` line itself, the way P2d's installer and console helper do.

Change `firmware/tests/test_session.c` exactly as this diff shows (`-` lines go, `+` lines come; the rest is context):

```diff
--- a/firmware/tests/test_session.c
+++ b/firmware/tests/test_session.c
@@ -36,6 +36,16 @@ static void host_error(const char *code) {
   fake_run(10); /* the close core asked for is delivered */
 }
 
+/* `status` as P2d's installer reads it: the @omb line contains want. The field
+ * order is pinned (contract §2.11), so want can span fields. */
+static void expect_status(const char *want) {
+  fake_console_clear();
+  fake_console_in("status");
+  const char *line = fake_console_count() > 0 ? fake_console_line(fake_console_count() - 1) : "";
+  TEST_ASSERT_EQUAL_STRING_LEN_MESSAGE("@omb {\"op\":\"status\",", line, 20, "no @omb status line");
+  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(line, want), line);
+}
+
 static void test_unpaired_gadget_waits_on_setup(void) {
   fake_boot("amoled-175c");
   fake_run(5000);
@@ -190,12 +200,14 @@ static void test_device_limit_retries_every_10_s_for_120_s(void) {
    * P2d's installer can say why instead of timing out */
   TEST_ASSERT_EQUAL_INT(CORE_PAIR_ERROR, core_pair_state());
   TEST_ASSERT_EQUAL_STRING("device_limit", core_last_error());
+  expect_status("\"pair\":\"error\",\"error\":\"device_limit\",");
   int opens = fake_ws_opens();
   fake_run(9900);
   TEST_ASSERT_EQUAL_INT(opens, fake_ws_opens());
   fake_run(200);
   TEST_ASSERT_EQUAL_INT(opens + 1, fake_ws_opens());
   TEST_ASSERT_EQUAL_INT(CORE_PAIR_ERROR, core_pair_state()); /* also while a retry connects */
+  expect_status("\"pair\":\"error\",\"error\":\"device_limit\",");
   /* keep answering device_limit until the window closes */
   uint64_t gave_up = 0;
   while (gave_up == 0 && fake_now() < 200000) {
@@ -213,6 +225,7 @@ static void test_device_limit_retries_every_10_s_for_120_s(void) {
   }
   TEST_ASSERT_TRUE(gave_up >= 120000 && gave_up <= 120200); /* 120 s after the code was stored */
   TEST_ASSERT_EQUAL_INT(CORE_PAIR_UNPAIRED, core_pair_state());
+  expect_status("\"pair\":\"unpaired\",\"fw\":"); /* no error field once the code is gone */
   opens = fake_ws_opens();
   fake_run(60000);
   TEST_ASSERT_EQUAL_INT(opens, fake_ws_opens());
```

- [ ] **Step 2: Run it to see it fail**

Run: `cmake --build build/host -j10 && ./build/host/tests/test_console`
Expected: it builds and fails with `test_status_of_a_fresh_gadget:FAIL: Expected '@omb {"op":"status",…'`, because console lines are still ignored. A later test may end the run with a segmentation fault (exit 139) when it reads an `@omb` line that never printed.

Run: `./build/host/tests/test_session`
Expected: `test_device_limit_retries_every_10_s_for_120_s:FAIL: Expected '@omb {"op":"status",' Was ''` … `no @omb status line`, and `24 Tests 1 Failures 0 Ignored`. Task 8's core-level checks in that test still pass: only the `status` line is missing.

- [ ] **Step 3: Write the implementation**

Create `firmware/core/src/console_cmd.c`:

```c
/* firmware/core/src/console_cmd.c */
/* SPDX-License-Identifier: Apache-2.0 */
/* Running console commands and printing @omb lines (spec §5.6, contract
 * §2.11). Parsing is in console.c. */
#include <stdio.h>
#include <string.h>
#include "core_internal.h"
#include "gadget_console.h"

#define TAG "console"

static const char *wifi_name(gadget_wifi_state_t st) {
  switch (st) {
    case GADGET_WIFI_CONNECTING: return "connecting";
    case GADGET_WIFI_CONNECTED: return "connected";
    case GADGET_WIFI_FAILED: return "failed";
    default: return "off";
  }
}

static const char *pair_name(core_pair_state_t st) {
  switch (st) {
    case CORE_PAIR_CODE_STORED: return "code_stored";
    case CORE_PAIR_CONNECTING: return "connecting";
    case CORE_PAIR_PAIRED: return "paired";
    case CORE_PAIR_ERROR: return "error";
    default: return "unpaired";
  }
}

static const char *auth_name(gadget_wifi_auth_t a) {
  switch (a) {
    case GADGET_AUTH_OPEN: return "open";
    case GADGET_AUTH_WEP: return "wep";
    case GADGET_AUTH_WPA: return "wpa";
    case GADGET_AUTH_WPA2: return "wpa2";
    case GADGET_AUTH_WPA3: return "wpa3";
    case GADGET_AUTH_WPA2_ENT: return "wpa2-ent";
    default: return "other";
  }
}

static cJSON *line(const char *op) {
  cJSON *o = cJSON_CreateObject();
  cJSON_AddStringToObject(o, "op", op);
  return o;
}

static void print_error(const char *cmd, const char *message) {
  cJSON *o = line("error");
  cJSON_AddStringToObject(o, "cmd", cmd);
  cJSON_AddStringToObject(o, "message", message);
  core_omb(o);
}

static void print_status(void) {
  cJSON *o = line("status");
  cJSON_AddStringToObject(o, "wifi", wifi_name(hal_wifi_state()));
  if (g_core.wifi_ssid[0] != '\0') cJSON_AddStringToObject(o, "ssid", g_core.wifi_ssid);
  const char *host = session_host_in_use();
  if (host != NULL) cJSON_AddStringToObject(o, "host", host);
  cJSON_AddStringToObject(o, "id", g_core.id);
  core_pair_state_t pair = session_pair_state();
  cJSON_AddStringToObject(o, "pair", pair_name(pair));
  if (pair == CORE_PAIR_ERROR) cJSON_AddStringToObject(o, "error", session_last_error());
  cJSON_AddStringToObject(o, "fw", g_core.fw);
  gadget_battery_t b;
  if (g_core.board->has_battery && hal_battery_read(&b)) {
    cJSON *batt = cJSON_AddObjectToObject(o, "battery");
    cJSON_AddNumberToObject(batt, "pct", b.pct > 100 ? 100 : b.pct);
    cJSON_AddBoolToObject(batt, "charging", b.charging);
  }
  cJSON_AddStringToObject(o, "board", g_core.board->id);
  cJSON_AddStringToObject(o, "name", g_core.name);
  if (g_core.model.host_name[0] != '\0') cJSON_AddStringToObject(o, "host_name", g_core.model.host_name);
  core_omb(o);
}

void console_on_scan(const gadget_wifi_scan_ev_t *scan) {
  cJSON *o = line("scan");
  cJSON *arr = cJSON_AddArrayToObject(o, "networks");
  uint8_t n = scan->ok ? scan->count : 0;
  for (uint8_t i = 0; i < n && i < 20; i++) {
    cJSON *ap = cJSON_CreateObject();
    cJSON_AddStringToObject(ap, "ssid", scan->aps[i].ssid);
    cJSON_AddNumberToObject(ap, "rssi", scan->aps[i].rssi);
    cJSON_AddStringToObject(ap, "auth", auth_name(scan->aps[i].auth));
    cJSON_AddItemToArray(arr, ap);
  }
  core_omb(o);
}

void console_exec_line(const char *text) {
  static char buf[GADGET_CONSOLE_LINE_MAX];
  if (text == NULL) {
    print_error("", "line too long");
    return;
  }
  snprintf(buf, sizeof buf, "%s", text);
  gadget_console_parsed_t p;
  switch (gadget_console_parse(buf, &p)) {
    case GC_EMPTY:
      break;
    case GC_UNKNOWN:
    case GC_BAD_ARGS:
      print_error(p.cmd_name, p.error);
      break;
    case GC_WIFI:
      snprintf(g_core.wifi_ssid, sizeof g_core.wifi_ssid, "%s", p.a);
      snprintf(g_core.wifi_pass, sizeof g_core.wifi_pass, "%s", p.b);
      core_store_str(GADGET_KEY_WIFI_SSID, g_core.wifi_ssid);
      core_store_str(GADGET_KEY_WIFI_PASS, g_core.wifi_pass);
      if (hal_wifi_connect(g_core.wifi_ssid, g_core.wifi_pass) != GADGET_OK) print_error("wifi", "could not start Wi-Fi");
      break;
    case GC_SCAN:
      if (hal_wifi_scan() != GADGET_OK) print_error("scan", "scan failed");
      break;
    case GC_HOST_AUTO:
      g_core.host_addr[0] = '\0';
      core_store_erase(GADGET_KEY_HOST_ADDR);
      session_clear_host_name();
      session_reconnect_now(false);
      break;
    case GC_HOST_SET:
      snprintf(g_core.host_addr, sizeof g_core.host_addr, "%s:%u", p.a, (unsigned)p.port);
      core_store_str(GADGET_KEY_HOST_ADDR, g_core.host_addr);
      session_clear_host_name();
      session_reconnect_now(false);
      break;
    case GC_PAIR:
      snprintf(g_core.pair_code, sizeof g_core.pair_code, "%s", p.a);
      core_store_str(GADGET_KEY_PAIR_CODE, g_core.pair_code);
      session_reconnect_now(true);
      break;
    case GC_NAME:
      core_set_name(p.a);
      core_store_str(GADGET_KEY_NAME, g_core.name);
      snprintf(g_core.model.device_name, sizeof g_core.model.device_name, "%s", g_core.name);
      break;
    case GC_SAY: {
      char turn[GADGET_TURN_MAX + 1];
      if (!interaction_say(p.a, turn)) {
        print_error("say", "not connected to MausBot");
        break;
      }
      cJSON *o = line("say");
      cJSON_AddStringToObject(o, "turn", turn);
      core_omb(o);
      break;
    }
    case GC_STATUS:
      print_status();
      break;
    case GC_LOG_OFF:
      hal_log_set_enabled(false);
      break;
    case GC_LOG_ON:
      hal_log_set_enabled(true);
      break;
    case GC_FORGET:
      hal_log(GADGET_LOG_WARN, TAG, "forget: erasing the pairing, key, Wi-Fi and name");
      session_clear_host_name();
      hal_storage_erase_all();
      hal_restart();
    case GC_REBOOT:
      hal_restart();
  }
}
```

Change `firmware/core/src/core_internal.h` exactly as this diff shows (`-` lines go, `+` lines come; the rest is context):

```diff
--- a/firmware/core/src/core_internal.h
+++ b/firmware/core/src/core_internal.h
@@ -116,6 +116,10 @@ bool interaction_turn_in_flight(void);          /* sent and no done yet */
 /* Mean square of PCM16 samples (shared by the mic and speaker levels). */
 uint32_t core_mean_square(const int16_t *pcm, size_t n);
 
+/* ---- console_cmd.c ------------------------------------------------------------- */
+void console_exec_line(const char *line);       /* NULL: an over-long line was dropped */
+void console_on_scan(const gadget_wifi_scan_ev_t *scan);
+
 /* ---- screens.c -------------------------------------------------------------- */
 void screens_update(void);
 
```

Change `firmware/core/src/core.c` exactly as this diff shows (`-` lines go, `+` lines come; the rest is context):

```diff
--- a/firmware/core/src/core.c
+++ b/firmware/core/src/core.c
@@ -246,6 +246,12 @@ void core_event(const gadget_event_t *ev) {
     case GADGET_EV_MIC_FRAME:
       interaction_mic(&ev->u.mic);
       break;
+    case GADGET_EV_CONSOLE_LINE:
+      console_exec_line(ev->u.console.line);
+      break;
+    case GADGET_EV_WIFI_SCAN:
+      console_on_scan(&ev->u.scan);
+      break;
     case GADGET_EV_WIFI_STATE:
     case GADGET_EV_WS_OPEN:
     case GADGET_EV_WS_TEXT:
```

Change `firmware/core/CMakeLists.txt` exactly as this diff shows (`-` lines go, `+` lines come; the rest is context):

```diff
--- a/firmware/core/CMakeLists.txt
+++ b/firmware/core/CMakeLists.txt
@@ -13,6 +13,7 @@ set(GADGET_CORE_SRCS
   src/session.c
   src/screens.c
   src/interaction.c
+  src/console_cmd.c
 )
 
 if(ESP_PLATFORM)
```

- [ ] **Step 4: Run the tests**

Run: `cmake --build build/host -j10 && ctest --test-dir build/host --output-on-failure -L "unit|vectors"`
Expected: `100% tests passed, 0 tests failed out of 10`; `test_console` prints `12 Tests 0 Failures 0 Ignored` and `test_session` prints `25 Tests 0 Failures 0 Ignored`.

- [ ] **Step 5: Commit**

```bash
git add firmware/core firmware/tests/test_console.c firmware/tests/test_session.c firmware/tests/CMakeLists.txt
git commit -m "firmware: console commands and machine-readable @omb lines" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 11: Speech playback, barge-in and the mouth level

**Files:**
- Create: `firmware/core/src/audio.c`
- Modify: `firmware/core/src/interaction.c` (barge-in, tap-to-stop, reply timer after speech), `firmware/core/src/core.c`, `firmware/core/src/core_internal.h`, `firmware/core/CMakeLists.txt`, `firmware/tests/CMakeLists.txt`
- Test: `firmware/tests/test_audio.c`

**Interfaces:**
- Consumes: `interaction_turn()`, `core_mean_square()` (Task 9); `hal_spk_*`.
- Produces: `audio_init/deinit/tick`, `audio_on_msg` (`speak.begin`, `speak.end`, `speak.stop`), `audio_on_binary(stream, payload, len)` for speaker frames, `audio_stop_local()` and `audio_active()`.
- Behavior:
  - **Which streams play:** `speak.begin` opens a stream (`hal_spk_open(rate)`) unless its `turn` is not the current turn, or a recording is live. A new `speak.begin` replaces the playing stream.
  - **The jitter buffer:** frames go into a 1 s buffer (`GADGET_JITTER_BUFFER_MS`). Playback starts once 200 ms is buffered or `speak.end` has arrived, and is fed to `hal_spk_write` in 20 ms blocks. Audio past 1 s is dropped with one log line. A frame is decoded into a static buffer, so an 8 KiB frame never lands on the gadget task's stack (P2c's task has 16 KiB).
  - **The mouth level (contract §2.7):** each block's RMS is scheduled at `now + hal_spk_buffered_ms()` and mapped to <−42 dBFS → 0, −42…−32 → 1, −32…−24 → 2, ≥ −24 → 3, using integer mean-square thresholds. Rises apply at once; falls go one step per 60 ms; the level is 0 when nothing plays.
  - **The reply model:** `reply.speak_elapsed_ms` and `speak_total_ms` (set at `speak.end`). `g_core.f.speaking` drives the Speaking screen.
  - **Interaction changes:** any press while speech plays stops it at once (barge-in) and sends `stop` if the turn has not ended; that same tap does not also clear the reply. CANCEL stops speech. The 20 s reply timer starts when the speech ends.

- [ ] **Step 1: Write the failing test**

Create `firmware/tests/test_audio.c`:

```c
/* firmware/tests/test_audio.c */
/* SPDX-License-Identifier: Apache-2.0 */
/* Speech playback, the jitter buffer, barge-in, tap-to-stop and the mouth
 * level (core/src/audio.c, spec §4.4, §5.4, contract §2.7). */
#include <string.h>
#include "fake_hal.h"
#include "psa/crypto.h"
#include "unity.h"

void setUp(void) { fake_reset(); }
void tearDown(void) { core_deinit(); }

static char g_turn[33];

static void host(const char *fmt, const char *turn) {
  char json[512];
  snprintf(json, sizeof json, fmt, turn);
  fake_ws_in(json);
}

/* A typed turn (shorter to set up than a recording). */
static void start_turn(void) {
  fake_console_in("say \"hi\"");
  cJSON *say = fake_ws_last("say");
  snprintf(g_turn, sizeof g_turn, "%s", cJSON_GetObjectItem(say, "turn")->valuestring);
  cJSON_Delete(say);
}

/* One 40 ms speaker frame of a square wave (640 samples at 16 kHz). */
static void speech_frame(uint8_t stream, int16_t amp, uint32_t rate) {
  static uint8_t frame[2 + 960 * 2];
  size_t samples = rate / 25u;
  frame[0] = 0x02;
  frame[1] = stream;
  for (size_t i = 0; i < samples; i++) {
    int16_t v = (i / 10) % 2 ? amp : (int16_t)-amp;
    frame[2 + 2 * i] = (uint8_t)((uint16_t)v & 0xff);
    frame[3 + 2 * i] = (uint8_t)((uint16_t)v >> 8);
  }
  fake_ws_bin_in(frame, 2 + samples * 2);
}

/* frames x 40 ms of speech, paced in real time like the host. */
static void speak(uint8_t stream, int frames, int16_t amp) {
  for (int i = 0; i < frames; i++) {
    speech_frame(stream, amp, 16000);
    fake_run(40);
  }
}

static void test_speech_plays_and_the_screen_follows(void) {
  fake_ready("amoled-175c");
  start_turn();
  host("{\"op\":\"reply\",\"turn\":\"%s\",\"text\":\"You have two meetings.\",\"final\":true}", g_turn);
  host("{\"op\":\"speak.begin\",\"stream\":4,\"rate\":16000,\"turn\":\"%s\"}", g_turn);
  TEST_ASSERT_EQUAL_UINT32(16000, fake_spk_rate());
  speak(4, 3, 8000); /* 120 ms: still pre-buffering */
  TEST_ASSERT_EQUAL_size_t(0, fake_spk_accepted());
  speak(4, 22, 8000); /* 1 s in all */
  TEST_ASSERT_TRUE(fake_spk_accepted() > 0);
  TEST_ASSERT_EQUAL_INT(UI_SCREEN_SPEAKING, core_ui_model()->screen);
  TEST_ASSERT_EQUAL_INT(UI_MAUS_SPEAKING, core_ui_model()->maus);
  TEST_ASSERT_TRUE(core_ui_model()->reply.speak_elapsed_ms > 0);
  fake_ws_in("{\"op\":\"speak.end\",\"stream\":4}");
  TEST_ASSERT_EQUAL_UINT32(1000, core_ui_model()->reply.speak_total_ms);
  host("{\"op\":\"done\",\"turn\":\"%s\",\"outcome\":\"ok\"}", g_turn);
  fake_run(600);
  TEST_ASSERT_EQUAL_size_t(16000, fake_spk_accepted()); /* every sample reached the speaker */
  TEST_ASSERT_EQUAL_INT(UI_SCREEN_REPLY, core_ui_model()->screen);
  TEST_ASSERT_EQUAL_UINT8(0, core_ui_model()->speak_level);
  fake_run(19000);
  TEST_ASSERT_EQUAL_INT(UI_SCREEN_REPLY, core_ui_model()->screen); /* 20 s from the end of speech */
  fake_run(1500);
  TEST_ASSERT_EQUAL_INT(UI_SCREEN_IDLE, core_ui_model()->screen);
}

static void test_speak_level_rises_at_once_and_falls_a_step_per_60_ms(void) {
  fake_ready("amoled-175c");
  start_turn();
  host("{\"op\":\"speak.begin\",\"stream\":1,\"rate\":16000,\"turn\":\"%s\"}", g_turn);
  speak(1, 15, 16000); /* about -6 dBFS: level 3 */
  TEST_ASSERT_EQUAL_UINT8(3, core_ui_model()->speak_level);
  /* silence follows; sample the level every 10 ms while it plays out */
  uint8_t prev = 3;
  uint64_t last_drop = 0;
  int drops = 0;
  for (int i = 0; i < 25; i++) {
    speech_frame(1, 0, 16000);
    for (int k = 0; k < 4; k++) {
      fake_run(10);
      uint8_t lv = core_ui_model()->speak_level;
      TEST_ASSERT_TRUE_MESSAGE(lv <= prev, "the level rose during silence");
      if (lv < prev) {
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(prev - 1, lv, "the level fell more than one step");
        if (drops > 0) TEST_ASSERT_TRUE_MESSAGE(fake_now() - last_drop >= 60, "steps closer than 60 ms");
        last_drop = fake_now();
        drops++;
      }
      prev = lv;
    }
  }
  TEST_ASSERT_EQUAL_INT(3, drops);
  TEST_ASSERT_EQUAL_UINT8(0, core_ui_model()->speak_level);
  fake_ws_in("{\"op\":\"speak.end\",\"stream\":1}");
  fake_run(1000);
  /* quiet speech maps to level 1 */
  start_turn();
  host("{\"op\":\"speak.begin\",\"stream\":2,\"rate\":16000,\"turn\":\"%s\"}", g_turn);
  speak(2, 15, 400); /* about -38 dBFS */
  TEST_ASSERT_EQUAL_UINT8(1, core_ui_model()->speak_level);
}

static void test_tap_stops_speech_and_the_turn(void) {
  fake_ready("amoled-175c");
  start_turn();
  host("{\"op\":\"reply\",\"turn\":\"%s\",\"text\":\"Long answer\"}", g_turn);
  host("{\"op\":\"speak.begin\",\"stream\":1,\"rate\":16000,\"turn\":\"%s\"}", g_turn);
  speak(1, 10, 8000);
  int stops = fake_spk_stops();
  fake_input(GADGET_IN_TOUCH_DOWN, 233, 233);
  fake_run(50);
  fake_input(GADGET_IN_TOUCH_UP, 233, 233);
  fake_run(10);
  TEST_ASSERT_EQUAL_INT(stops + 1, fake_spk_stops());
  TEST_ASSERT_EQUAL_size_t(1, fake_ws_count("stop")); /* done has not arrived: stop the turn too */
  TEST_ASSERT_EQUAL_INT(UI_SCREEN_REPLY, core_ui_model()->screen); /* the reply stays for a second tap */
  host("{\"op\":\"done\",\"turn\":\"%s\",\"outcome\":\"stopped\"}", g_turn);
  fake_input(GADGET_IN_TOUCH_DOWN, 233, 233);
  fake_input(GADGET_IN_TOUCH_UP, 233, 233);
  fake_run(10);
  TEST_ASSERT_EQUAL_INT(UI_SCREEN_IDLE, core_ui_model()->screen);
}

static void test_tap_after_done_only_stops_playback(void) {
  fake_ready("amoled-175c");
  start_turn();
  host("{\"op\":\"reply\",\"turn\":\"%s\",\"text\":\"Done talking\",\"final\":true}", g_turn);
  host("{\"op\":\"done\",\"turn\":\"%s\",\"outcome\":\"ok\"}", g_turn);
  host("{\"op\":\"speak.begin\",\"stream\":1,\"rate\":16000,\"turn\":\"%s\"}", g_turn);
  speak(1, 10, 8000);
  fake_input(GADGET_IN_TOUCH_DOWN, 233, 233);
  fake_input(GADGET_IN_TOUCH_UP, 233, 233);
  fake_run(10);
  TEST_ASSERT_EQUAL_size_t(0, fake_ws_count("stop"));
  TEST_ASSERT_EQUAL_INT(UI_SCREEN_REPLY, core_ui_model()->screen);
}

static void test_barge_in_stops_the_old_turn_before_the_new_one(void) {
  fake_ready("lcd-154");
  start_turn();
  host("{\"op\":\"speak.begin\",\"stream\":1,\"rate\":16000,\"turn\":\"%s\"}", g_turn);
  speak(1, 10, 8000);
  fake_ws_clear();
  int stops = fake_spk_stops();
  fake_input(GADGET_IN_TALK_DOWN, 0, 0);
  TEST_ASSERT_EQUAL_INT(stops + 1, fake_spk_stops()); /* playback stops at once */
  fake_mic_frames(30, 3000);
  fake_input(GADGET_IN_TALK_UP, 0, 0);
  fake_run(10);
  cJSON *first = cJSON_Parse(fake_ws_text(0));
  TEST_ASSERT_EQUAL_STRING("stop", cJSON_GetObjectItem(first, "op")->valuestring);
  TEST_ASSERT_EQUAL_STRING(g_turn, cJSON_GetObjectItem(first, "turn")->valuestring);
  cJSON_Delete(first);
  cJSON *second = cJSON_Parse(fake_ws_text(1));
  TEST_ASSERT_EQUAL_STRING("voice.begin", cJSON_GetObjectItem(second, "op")->valuestring);
  cJSON_Delete(second);
  TEST_ASSERT_EQUAL_size_t(1, fake_ws_count("stop"));
}

static void test_cancel_stops_speech(void) {
  fake_ready("lcd-154");
  start_turn();
  host("{\"op\":\"speak.begin\",\"stream\":1,\"rate\":16000,\"turn\":\"%s\"}", g_turn);
  speak(1, 10, 8000);
  fake_input(GADGET_IN_CANCEL_DOWN, 0, 0);
  fake_run(10);
  TEST_ASSERT_NOT_EQUAL(UI_SCREEN_SPEAKING, core_ui_model()->screen);
  TEST_ASSERT_EQUAL_size_t(1, fake_ws_count("stop"));
}

static void test_speak_stop_and_replacement_streams(void) {
  fake_ready("amoled-175c");
  start_turn();
  host("{\"op\":\"speak.begin\",\"stream\":1,\"rate\":16000,\"turn\":\"%s\"}", g_turn);
  speak(1, 10, 8000);
  fake_ws_in("{\"op\":\"speak.stop\",\"stream\":1}");
  fake_run(10);
  TEST_ASSERT_NOT_EQUAL(UI_SCREEN_SPEAKING, core_ui_model()->screen);
  /* a new speak.begin replaces the playing stream; frames for the old one are dropped */
  host("{\"op\":\"speak.begin\",\"stream\":2,\"rate\":16000,\"turn\":\"%s\"}", g_turn);
  speak(2, 10, 8000);
  host("{\"op\":\"speak.begin\",\"stream\":3,\"rate\":16000,\"turn\":\"%s\"}", g_turn);
  size_t before = fake_spk_accepted();
  speak(2, 10, 8000);
  fake_ws_in("{\"op\":\"speak.end\",\"stream\":3}");
  fake_run(1000);
  TEST_ASSERT_EQUAL_size_t(before, fake_spk_accepted());
}

static void test_speech_for_another_turn_is_ignored(void) {
  fake_ready("amoled-175c");
  start_turn();
  fake_ws_in("{\"op\":\"speak.begin\",\"stream\":1,\"rate\":16000,\"turn\":\"t00000000-1\"}");
  speak(1, 10, 8000);
  TEST_ASSERT_EQUAL_size_t(0, fake_spk_accepted());
  TEST_ASSERT_EQUAL_INT(UI_SCREEN_THINKING, core_ui_model()->screen);
}

static void test_devkit_plays_24_khz(void) {
  fake_ready("devkit");
  start_turn();
  host("{\"op\":\"speak.begin\",\"stream\":1,\"rate\":24000,\"turn\":\"%s\"}", g_turn);
  TEST_ASSERT_EQUAL_UINT32(24000, fake_spk_rate());
  for (int i = 0; i < 10; i++) {
    speech_frame(1, 8000, 24000);
    fake_run(40);
  }
  fake_ws_in("{\"op\":\"speak.end\",\"stream\":1}");
  fake_run(1000);
  TEST_ASSERT_EQUAL_size_t(9600, fake_spk_accepted());
}

static void test_a_flood_beyond_the_buffer_is_dropped(void) {
  fake_ready("amoled-175c");
  start_turn();
  host("{\"op\":\"speak.begin\",\"stream\":1,\"rate\":16000,\"turn\":\"%s\"}", g_turn);
  for (int i = 0; i < 100; i++) speech_frame(1, 8000, 16000); /* 4 s at once, no pacing */
  fake_ws_in("{\"op\":\"speak.end\",\"stream\":1}");
  fake_run(3000);
  TEST_ASSERT_TRUE(fake_spk_accepted() <= 16000u + 3200u); /* the 1 s buffer plus what the speaker took */
  TEST_ASSERT_EQUAL_INT(CORE_PAIR_PAIRED, core_pair_state());
}

static void test_a_frame_of_exactly_8_kib_plays(void) {
  fake_ready("amoled-175c");
  static uint8_t frame[GADGET_BINARY_FRAME_MAX]; /* the protocol limit, header included */
  memset(frame, 0x11, sizeof frame);
  frame[0] = 0x02; /* speaker audio, stream 2 */
  frame[1] = 2;
  fake_ws_in("{\"op\":\"speak.begin\",\"stream\":2,\"rate\":16000}");
  fake_ws_bin_in(frame, sizeof frame);
  fake_ws_in("{\"op\":\"speak.end\",\"stream\":2}");
  fake_run(500);
  TEST_ASSERT_EQUAL_size_t((GADGET_BINARY_FRAME_MAX - 2) / 2, fake_spk_accepted()); /* 4095 samples */
}

static void test_a_drop_stops_playback(void) {
  fake_ready("amoled-175c");
  start_turn();
  host("{\"op\":\"speak.begin\",\"stream\":1,\"rate\":16000,\"turn\":\"%s\"}", g_turn);
  speak(1, 10, 8000);
  int stops = fake_spk_stops();
  fake_ws_drop(1006);
  fake_run(10);
  TEST_ASSERT_EQUAL_INT(stops + 1, fake_spk_stops());
  fake_run(300);
  TEST_ASSERT_EQUAL_UINT8(0, core_ui_model()->speak_level); /* the mouth closes */
}

int main(void) {
  if (psa_crypto_init() != PSA_SUCCESS) return 3;
  UNITY_BEGIN();
  RUN_TEST(test_speech_plays_and_the_screen_follows);
  RUN_TEST(test_speak_level_rises_at_once_and_falls_a_step_per_60_ms);
  RUN_TEST(test_tap_stops_speech_and_the_turn);
  RUN_TEST(test_tap_after_done_only_stops_playback);
  RUN_TEST(test_barge_in_stops_the_old_turn_before_the_new_one);
  RUN_TEST(test_cancel_stops_speech);
  RUN_TEST(test_speak_stop_and_replacement_streams);
  RUN_TEST(test_speech_for_another_turn_is_ignored);
  RUN_TEST(test_devkit_plays_24_khz);
  RUN_TEST(test_a_flood_beyond_the_buffer_is_dropped);
  RUN_TEST(test_a_frame_of_exactly_8_kib_plays);
  RUN_TEST(test_a_drop_stops_playback);
  return UNITY_END();
}
```

Change `firmware/tests/CMakeLists.txt` exactly as this diff shows (`-` lines go, `+` lines come; the rest is context):

```diff
--- a/firmware/tests/CMakeLists.txt
+++ b/firmware/tests/CMakeLists.txt
@@ -29,6 +29,7 @@ gadget_unit_test(core_init)
 gadget_unit_test(session)
 gadget_unit_test(turns)
 gadget_unit_test(console)
+gadget_unit_test(audio)
 
 # protocol/vectors in C (label vectors).
 add_executable(test_vectors test_vectors.c)
```

- [ ] **Step 2: Run it to see it fail**

Run: `cmake --build build/host -j10 && ./build/host/tests/test_audio`
Expected: it builds and the tests fail, starting with `test_speech_plays_and_the_screen_follows:FAIL: Expected 16000 Was 0`, because the speaker is never opened.

- [ ] **Step 3: Write the implementation**

Create `firmware/core/src/audio.c`:

```c
/* firmware/core/src/audio.c */
/* SPDX-License-Identifier: Apache-2.0 */
/* Speech playback (spec §4.4): speak.begin / frames / speak.end / speak.stop
 * into a 1 s jitter buffer that feeds hal_spk_write in 20 ms blocks, and the
 * mouth level the UI draws (contract §2.7 "Speaking level"). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "core_internal.h"

#define TAG "audio"
#define PREBUFFER_MS 200u
#define LEVEL_QUEUE 64u
#define LEVEL_DROP_MS 60u

/* Mean-square thresholds for -42, -32 and -24 dBFS (32768^2 * 10^(dB/10)). */
#define MS_42DB 67749u
#define MS_32DB 677485u
#define MS_24DB 4274643u

static struct {
  bool active;
  uint8_t stream;
  uint32_t rate;
  char turn[GADGET_TURN_MAX + 1];  /* "" for speech that belongs to no turn (posts) */
  int16_t *ring;
  size_t cap, head, count;         /* samples */
  bool ended;                      /* speak.end arrived */
  bool playing;                    /* prebuffer reached: feeding the HAL */
  bool overflow_logged;
  uint64_t received, written;      /* samples */
  struct {
    uint64_t at;
    uint8_t level;
  } lv[LEVEL_QUEUE];
  size_t lv_head, lv_n;
  uint8_t level;
  uint64_t level_changed;
} A;

static uint8_t level_of(const int16_t *pcm, size_t n) {
  uint32_t ms = core_mean_square(pcm, n);
  if (ms < MS_42DB) return 0;
  if (ms < MS_32DB) return 1;
  if (ms < MS_24DB) return 2;
  return 3;
}

static void release(void) {
  free(A.ring);
  A.ring = NULL;
  A.active = false;
  A.playing = false;
  A.count = A.head = 0;
}

void audio_stop_local(void) {
  if (!A.active) return;
  hal_spk_stop();
  release();
  A.lv_n = 0;
}

static bool begin_stream(uint8_t stream, uint32_t rate, const char *turn) {
  audio_stop_local();
  if (g_core.board->speaker_rate == 0 || hal_spk_open(rate) != GADGET_OK) {
    hal_log(GADGET_LOG_WARN, TAG, "no speaker at %u Hz", (unsigned)rate);
    return false;
  }
  A.cap = (size_t)rate * GADGET_JITTER_BUFFER_MS / 1000u;
  A.ring = malloc(A.cap * sizeof(int16_t));
  if (A.ring == NULL) return false;
  A.active = true;
  A.stream = stream;
  A.rate = rate;
  snprintf(A.turn, sizeof A.turn, "%s", turn ? turn : "");
  A.head = A.count = 0;
  A.ended = A.playing = A.overflow_logged = false;
  A.received = A.written = 0;
  g_core.model.reply.speak_elapsed_ms = 0;
  g_core.model.reply.speak_total_ms = 0;
  return true;
}

static void push_samples(const int16_t *pcm, size_t n) {
  for (size_t i = 0; i < n; i++) {
    if (A.count == A.cap) {
      if (!A.overflow_logged) hal_log(GADGET_LOG_WARN, TAG, "speech arrived more than 1 s ahead; dropping");
      A.overflow_logged = true;
      return;
    }
    A.ring[(A.head + A.count) % A.cap] = pcm[i];
    A.count++;
    A.received++;
  }
}

bool audio_on_msg(const gp_msg_t *m) {
  switch (m->op) {
    case GP_OP_SPEAK_BEGIN: {
      const gp_speak_begin_t *b = &m->m.speak_begin;
      const char *turn = b->turn;
      if (turn != NULL && strcmp(turn, interaction_turn()) != 0) break; /* speech for an old turn */
      if (g_core.f.recording) break;                                     /* never play into the mic */
      begin_stream(b->stream, b->rate, turn);
      break;
    }
    case GP_OP_SPEAK_END:
      if (A.active && m->m.speak_end.stream == A.stream) {
        A.ended = true;
        g_core.model.reply.speak_total_ms = (uint32_t)(A.received * 1000u / A.rate);
      }
      break;
    case GP_OP_SPEAK_STOP:
      if (A.active && m->m.speak_stop.stream == A.stream) audio_stop_local();
      break;
    default:
      return false;
  }
  return true;
}

void audio_on_binary(uint8_t stream, const uint8_t *payload, size_t len) {
  if (!A.active || stream != A.stream || A.ended) return;
  static int16_t pcm[GADGET_BINARY_FRAME_MAX / 2]; /* 8 KiB: never on the task stack (core is single-threaded) */
  size_t n = len / 2;
  for (size_t i = 0; i < n; i++) pcm[i] = (int16_t)(uint16_t)(payload[2 * i] | (payload[2 * i + 1] << 8));
  push_samples(pcm, n);
}

static void feed(void) {
  uint64_t now = g_core.now;
  if (!A.playing && (A.count * 1000u / A.rate >= PREBUFFER_MS || A.ended)) A.playing = true;
  if (!A.playing) return;
  size_t block = A.rate / 50u; /* 20 ms */
  while (A.count > 0) {
    size_t run = A.cap - A.head;
    if (run > A.count) run = A.count;
    if (run > block) run = block;
    uint32_t ahead = hal_spk_buffered_ms();
    size_t n = hal_spk_write(&A.ring[A.head], run);
    if (n == 0) break;
    if (A.lv_n < LEVEL_QUEUE) {
      size_t slot = (A.lv_head + A.lv_n) % LEVEL_QUEUE;
      A.lv[slot].at = now + ahead;
      A.lv[slot].level = level_of(&A.ring[A.head], n);
      A.lv_n++;
    }
    A.head = (A.head + n) % A.cap;
    A.count -= n;
    A.written += n;
    if (n < run) break;
  }
}

void audio_tick(void) {
  uint64_t now = g_core.now;
  ui_model_t *m = &g_core.model;
  if (A.active) {
    feed();
    uint32_t buffered = hal_spk_buffered_ms();
    uint32_t written_ms = (uint32_t)(A.written * 1000u / A.rate);
    m->reply.speak_elapsed_ms = written_ms > buffered ? written_ms - buffered : 0;
    if (A.ended && A.count == 0 && buffered == 0) release(); /* played out */
  }
  uint8_t target = A.active ? A.level : 0;
  bool popped = false;
  while (A.lv_n > 0 && A.lv[A.lv_head].at <= now) {
    target = A.lv[A.lv_head].level;
    popped = true;
    A.lv_head = (A.lv_head + 1) % LEVEL_QUEUE;
    A.lv_n--;
  }
  if (!popped && A.lv_n == 0 && !A.active) target = 0;
  if (target > A.level) {
    A.level = target;
    A.level_changed = now;
  } else if (target < A.level && now - A.level_changed >= LEVEL_DROP_MS) {
    A.level--;
    A.level_changed = now;
  }
  m->speak_level = A.level;
  g_core.f.speaking = A.active && A.turn[0] != '\0' && strcmp(A.turn, interaction_turn()) == 0;
}

bool audio_active(void) { return A.active; }

void audio_init(void) { memset(&A, 0, sizeof A); }

void audio_deinit(void) {
  free(A.ring);
  memset(&A, 0, sizeof A);
}
```

Change `firmware/core/src/interaction.c` exactly as this diff shows (`-` lines go, `+` lines come; the rest is context):

```diff
--- a/firmware/core/src/interaction.c
+++ b/firmware/core/src/interaction.c
@@ -17,6 +17,7 @@ static struct {
   uint64_t press_at;
   int16_t x0, y0;
   bool moved;          /* the touch moved past the swipe threshold */
+  bool press_stopped_audio; /* this press already stopped playback (barge-in or tap) */
   bool rec;            /* voice.begin sent, frames streaming */
   char rec_turn[GADGET_TURN_MAX + 1];
   uint8_t rec_stream;
@@ -157,6 +158,16 @@ static void begin_press(press_t src, int16_t x, int16_t y) {
   I.y0 = y;
   I.moved = false;
   I.pre_n = 0;
+  I.press_stopped_audio = false;
+  if (audio_active()) {
+    /* barge-in: stop playback now; tell the host if the turn is still running */
+    audio_stop_local();
+    I.press_stopped_audio = true;
+    if (I.in_flight && !I.stop_sent) {
+      send_stop(I.turn);
+      I.stop_sent = true;
+    }
+  }
   if (hal_mic_start(GADGET_MIC_RATE) != GADGET_OK) hal_log(GADGET_LOG_ERROR, TAG, "mic did not start");
 }
 
@@ -172,8 +183,9 @@ static void cancel_action(void) {
     cancel_recording();
     return;
   }
-  if (I.in_flight) {
-    if (!I.stop_sent) {
+  if (audio_active() || I.in_flight) {
+    audio_stop_local();
+    if (I.in_flight && !I.stop_sent) {
       send_stop(I.turn);
       I.stop_sent = true;
     }
@@ -186,6 +198,7 @@ static void cancel_action(void) {
 static void tap(int16_t x, int16_t y) {
   (void)x;
   (void)y;
+  if (I.press_stopped_audio) return; /* that tap stopped the speech: the reply stays */
   if (I.reply_shown && !I.in_flight) clear_turn_model();
 }
 
@@ -280,6 +293,7 @@ void interaction_tick(void) {
       g_core.model.listening.countdown_s = (uint8_t)((GADGET_UTTERANCE_MAX_MS - elapsed + 999u) / 1000u);
     }
   }
+  if (I.reply_shown && audio_active()) I.reply_until = now + GADGET_REPLY_IDLE_MS; /* 20 s after the speech ends */
   if (I.reply_shown && !I.in_flight && I.reply_until != 0 && now >= I.reply_until) clear_turn_model();
   publish();
 }
```

Change `firmware/core/src/core_internal.h` exactly as this diff shows (`-` lines go, `+` lines come; the rest is context):

```diff
--- a/firmware/core/src/core_internal.h
+++ b/firmware/core/src/core_internal.h
@@ -116,6 +116,15 @@ bool interaction_turn_in_flight(void);          /* sent and no done yet */
 /* Mean square of PCM16 samples (shared by the mic and speaker levels). */
 uint32_t core_mean_square(const int16_t *pcm, size_t n);
 
+/* ---- audio.c ------------------------------------------------------------------ */
+void audio_init(void);
+void audio_deinit(void);
+bool audio_on_msg(const gp_msg_t *m);           /* speak.begin / speak.end / speak.stop */
+void audio_on_binary(uint8_t stream, const uint8_t *payload, size_t len);   /* speaker frames */
+void audio_tick(void);
+void audio_stop_local(void);                    /* stop playback now (tap, barge-in, cancel) */
+bool audio_active(void);                        /* something is playing or buffered */
+
 /* ---- console_cmd.c ------------------------------------------------------------- */
 void console_exec_line(const char *line);       /* NULL: an over-long line was dropped */
 void console_on_scan(const gadget_wifi_scan_ev_t *scan);
```

Change `firmware/core/src/core.c` exactly as this diff shows (`-` lines go, `+` lines come; the rest is context):

```diff
--- a/firmware/core/src/core.c
+++ b/firmware/core/src/core.c
@@ -222,6 +222,7 @@ gadget_status_t core_init(const core_config_t *cfg) {
   publish_identity();
   session_init();
   interaction_init();
+  audio_init();
   if (g_core.wifi_ssid[0] != '\0') hal_wifi_connect(g_core.wifi_ssid, g_core.wifi_pass);
 
   cJSON *boot = cJSON_CreateObject();
@@ -275,6 +276,7 @@ void core_tick(uint64_t now_ms) {
   g_core.ticked = true;
   session_tick();
   interaction_tick();
+  audio_tick();
   screens_update();
   model_commit();
 }
@@ -283,6 +285,7 @@ const ui_model_t *core_ui_model(void) { return &g_core.model; }
 
 void core_deinit(void) {
   if (g_core.initialized) {
+    audio_deinit();
     interaction_deinit();
     session_deinit();
   }
@@ -310,11 +313,13 @@ void core_on_ready(void) { hal_log(GADGET_LOG_INFO, CORE_TAG, "talking to %s", g
 
 void core_on_session_lost(void) {
   hal_log(GADGET_LOG_INFO, CORE_TAG, "session lost");
+  audio_stop_local();
   interaction_on_session_lost();
 }
 
 void core_on_msg(const gp_msg_t *m) {
   if (interaction_on_msg(m)) return;
+  if (audio_on_msg(m)) return;
   switch (m->op) {
     default:
       hal_log(GADGET_LOG_DEBUG, CORE_TAG, "ignored %s", gp_op_name(m->op));
@@ -323,8 +328,10 @@ void core_on_msg(const gp_msg_t *m) {
 }
 
 void core_on_binary(gp_bin_kind_t kind, uint8_t stream, const uint8_t *payload, size_t len) {
-  (void)payload;
   switch (kind) {
+    case GP_BIN_SPEAKER:
+      audio_on_binary(stream, payload, len);
+      break;
     default:
       hal_log(GADGET_LOG_DEBUG, CORE_TAG, "ignored binary kind %d stream %u (%u bytes)", (int)kind, (unsigned)stream,
               (unsigned)len);
```

Change `firmware/core/CMakeLists.txt` exactly as this diff shows (`-` lines go, `+` lines come; the rest is context):

```diff
--- a/firmware/core/CMakeLists.txt
+++ b/firmware/core/CMakeLists.txt
@@ -14,6 +14,7 @@ set(GADGET_CORE_SRCS
   src/screens.c
   src/interaction.c
   src/console_cmd.c
+  src/audio.c
 )
 
 if(ESP_PLATFORM)
```

- [ ] **Step 4: Run the tests**

Run: `cmake --build build/host -j10 && ctest --test-dir build/host --output-on-failure -L "unit|vectors"`
Expected: `100% tests passed, 0 tests failed out of 11`; `test_audio` prints `12 Tests 0 Failures 0 Ignored`.

- [ ] **Step 5: Commit**

```bash
git add firmware/core firmware/tests/test_audio.c firmware/tests/CMakeLists.txt
git commit -m "firmware: speech playback with a jitter buffer, barge-in and mouth level" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 12: Asks, cards, images, posts and battery sense

**Files:**
- Create: `firmware/core/src/display.c`
- Modify: `firmware/core/src/audio.c` (the chime), `firmware/core/src/interaction.c` (ask input, dismiss, tap), `firmware/core/src/core.c`, `firmware/core/src/core_internal.h`, `firmware/core/CMakeLists.txt`, `firmware/tests/CMakeLists.txt`
- Test: `firmware/tests/test_display.c`

**Interfaces:**
- Consumes: `ui_layout_ask` and `ui_hit_test` (Task 5); `session_send` (Task 8); `audio_*` (Task 11).
- Produces: `display_init/deinit/tick`, `display_on_msg` (`ask`, `ask.close`, `post`, `card`, `card.close`, `image.begin`, `image.end`), `display_on_binary` (image rows), `display_on_session_lost`, `display_ask_input(in)`, `display_dismiss()`, `display_tap()`, plus `audio_play_chime()` and `audio_speech_active()`.
- **Asks** (spec §4.5, §5.4):
  - Asks queue up (up to 8; a repeated id updates in place). One shows at a time, with a fresh 0.6 s lock each time.
  - Touch boards hit-test touch-ups against the same rectangles the UI draws. Button boards: TALK picks option 1 and CANCEL picks option 2, and only TALK works on a one-option ask.
  - An ask is answerable with 1–4 options on touch boards and 1–2 on button boards. Unanswerable asks show without buttons, and TALK then records as usual.
  - The gadget answers at most once and keeps the ask on screen until `ask.close`. `expires_s` removes it locally. A dropped session clears the queue, since the host sends open asks again.
- **Cards, images and posts:**
  - Cards follow `ttl_s` (0 means until dismissed) and `card.close`.
  - Images must fit `caps.image`. Rows are collected in order, and an incomplete image is dropped. The image shows over a card, and `card.close` closes either one.
  - A post shows a toast for 8 s with the Maus notifying, plus a 300 ms two-tone chime (integer sine table). There is no chime while speech plays or the mic records.
  - A spoken post (`speak: true`, then a `speak.begin` without `turn`) keeps its chime: when that speech arrives at the chime's rate while the chime plays, it queues behind the chime's unplayed samples instead of cutting them, and then pre-buffers like any speech. At another rate the chime gives way, as before. The screen stays Idle and the toast stays up.
- **Dismissing:** CANCEL or a swipe down dismisses, in order, the image, then the card, then the toast. A tap hides the toast. A tap during a chime is not a speech stop (Review Focus 4).
- **`sense`:** the battery is polled every 1 s into the model. `sense` goes out when the percentage or charging state changes, at most every 10 s, and never on boards without a battery.

- [ ] **Step 1: Write the failing test**

Create `firmware/tests/test_display.c`:

```c
/* firmware/tests/test_display.c */
/* SPDX-License-Identifier: Apache-2.0 */
/* Asks, cards, images, posts and battery sense (core/src/display.c,
 * spec §4.5-§4.7, §5.4). */
#include <stdlib.h>
#include <string.h>
#include "fake_hal.h"
#include "gadget_ui.h"
#include "psa/crypto.h"
#include "unity.h"

void setUp(void) { fake_reset(); }
void tearDown(void) { core_deinit(); }

static const char *PERMISSION =
    "{\"op\":\"ask\",\"id\":\"a_1\",\"kind\":\"permission\",\"title\":\"Run a shell command?\",\"body\":\"ls -la\","
    "\"options\":[{\"id\":\"allow\",\"label\":\"Allow\",\"style\":\"allow\"},"
    "{\"id\":\"deny\",\"label\":\"Deny\",\"style\":\"deny\"}]}";

static void tap_at(gadget_rect_t r) {
  int16_t x = (int16_t)(r.x + r.w / 2), y = (int16_t)(r.y + r.h / 2);
  fake_input(GADGET_IN_TOUCH_DOWN, x, y);
  fake_input(GADGET_IN_TOUCH_UP, x, y);
  fake_run(10);
}

static void test_permission_ask_on_a_touch_board(void) {
  fake_ready("amoled-175c");
  fake_ws_in(PERMISSION);
  fake_run(10);
  const ui_model_t *m = core_ui_model();
  TEST_ASSERT_EQUAL_INT(UI_SCREEN_ASK, m->screen);
  TEST_ASSERT_EQUAL_INT(UI_MAUS_NONE, m->maus);
  TEST_ASSERT_EQUAL_STRING("Run a shell command?", m->ask.title);
  TEST_ASSERT_EQUAL_STRING("ls -la", m->ask.body);
  TEST_ASSERT_FALSE(m->ask.question);
  TEST_ASSERT_EQUAL_UINT8(2, m->ask.n_options);
  TEST_ASSERT_EQUAL_INT(UI_STYLE_ALLOW, m->ask.options[0].style);
  TEST_ASSERT_EQUAL_INT(UI_STYLE_DENY, m->ask.options[1].style);
  TEST_ASSERT_TRUE(m->ask.answerable);
  TEST_ASSERT_EQUAL_INT8(-1, m->ask.chosen);
  gadget_rect_t want[UI_ASK_OPTIONS_MAX];
  ui_layout_ask(gadget_board_by_id("amoled-175c"), 2, want);
  TEST_ASSERT_EQUAL_MEMORY(&want[1], &m->ask.options[1].rect, sizeof want[1]);
  tap_at(m->ask.options[1].rect); /* within the first 0.6 s: ignored */
  TEST_ASSERT_EQUAL_size_t(0, fake_ws_count("answer"));
  fake_run(600);
  tap_at(m->ask.options[1].rect);
  cJSON *ans = fake_ws_last("answer");
  TEST_ASSERT_EQUAL_STRING("a_1", cJSON_GetObjectItem(ans, "id")->valuestring);
  TEST_ASSERT_EQUAL_STRING("deny", cJSON_GetObjectItem(ans, "option")->valuestring);
  cJSON_Delete(ans);
  TEST_ASSERT_EQUAL_INT8(1, core_ui_model()->ask.chosen);
  tap_at(m->ask.options[0].rect); /* answers at most once */
  TEST_ASSERT_EQUAL_size_t(1, fake_ws_count("answer"));
  TEST_ASSERT_FALSE(fake_mic_running()); /* touches on the ask never record */
  fake_ws_in("{\"op\":\"ask.close\",\"id\":\"a_1\",\"reason\":\"answered\"}");
  fake_run(10);
  TEST_ASSERT_EQUAL_INT(UI_SCREEN_IDLE, core_ui_model()->screen);
}

static void test_buttons_answer_on_a_button_board(void) {
  fake_ready("lcd-154");
  fake_ws_in(PERMISSION);
  fake_run(10);
  TEST_ASSERT_EQUAL_INT16(0, core_ui_model()->ask.options[0].rect.w); /* no rectangles without touch */
  fake_input(GADGET_IN_TALK_DOWN, 0, 0);
  fake_input(GADGET_IN_TALK_UP, 0, 0);
  TEST_ASSERT_EQUAL_size_t(0, fake_ws_count("answer"));
  TEST_ASSERT_FALSE(fake_mic_running());
  fake_run(600);
  fake_input(GADGET_IN_CANCEL_DOWN, 0, 0);
  fake_input(GADGET_IN_CANCEL_UP, 0, 0);
  cJSON *ans = fake_ws_last("answer");
  TEST_ASSERT_EQUAL_STRING("deny", cJSON_GetObjectItem(ans, "option")->valuestring);
  cJSON_Delete(ans);
  fake_ws_in("{\"op\":\"ask.close\",\"id\":\"a_1\",\"reason\":\"answered\"}");
  fake_ws_in("{\"op\":\"ask\",\"id\":\"a_2\",\"kind\":\"question\",\"title\":\"Which?\",\"body\":\"\","
             "\"options\":[{\"id\":\"x\",\"label\":\"Only one\"}]}");
  fake_run(700);
  fake_input(GADGET_IN_CANCEL_DOWN, 0, 0); /* a one-option ask maps only TALK */
  TEST_ASSERT_EQUAL_size_t(1, fake_ws_count("answer"));
  fake_input(GADGET_IN_TALK_DOWN, 0, 0);
  ans = fake_ws_last("answer");
  TEST_ASSERT_EQUAL_STRING("x", cJSON_GetObjectItem(ans, "option")->valuestring);
  cJSON_Delete(ans);
}

static void test_unanswerable_asks(void) {
  fake_ready("lcd-154");
  fake_ws_in("{\"op\":\"ask\",\"id\":\"a_3\",\"kind\":\"question\",\"title\":\"Pick\",\"body\":\"\",\"options\":["
             "{\"id\":\"1\",\"label\":\"One\"},{\"id\":\"2\",\"label\":\"Two\"},{\"id\":\"3\",\"label\":\"Three\"}]}");
  fake_run(700);
  TEST_ASSERT_FALSE(core_ui_model()->ask.answerable); /* more than two options on buttons */
  fake_input(GADGET_IN_TALK_DOWN, 0, 0);
  fake_mic_frames(20, 3000);
  TEST_ASSERT_EQUAL_INT(UI_SCREEN_LISTENING, core_ui_model()->screen); /* TALK talks instead */
  fake_input(GADGET_IN_TALK_UP, 0, 0);
  TEST_ASSERT_EQUAL_size_t(0, fake_ws_count("answer"));
  fake_reset();
  fake_ready("amoled-175c");
  fake_ws_in("{\"op\":\"ask\",\"id\":\"a_4\",\"kind\":\"question\",\"title\":\"Long form\",\"body\":\"\",\"options\":[]}");
  fake_run(10);
  TEST_ASSERT_FALSE(core_ui_model()->ask.answerable);
  TEST_ASSERT_TRUE(core_ui_model()->ask.question);
  fake_ws_in("{\"op\":\"ask.close\",\"id\":\"a_4\",\"reason\":\"withdrawn\"}");
  fake_run(10);
  TEST_ASSERT_EQUAL_INT(UI_SCREEN_IDLE, core_ui_model()->screen);
}

static void test_asks_queue_one_at_a_time(void) {
  fake_ready("amoled-175c");
  fake_ws_in(PERMISSION);
  fake_ws_in(PERMISSION); /* sent again: not a second entry */
  fake_ws_in("{\"op\":\"ask\",\"id\":\"a_2\",\"kind\":\"permission\",\"title\":\"Second\",\"body\":\"\",\"options\":["
             "{\"id\":\"allow\",\"label\":\"Allow\",\"style\":\"allow\"},{\"id\":\"deny\",\"label\":\"Deny\",\"style\":\"deny\"}]}");
  fake_run(1000);
  TEST_ASSERT_EQUAL_STRING("a_1", core_ui_model()->ask.id);
  TEST_ASSERT_EQUAL_UINT8(1, core_ui_model()->ask.queued);
  fake_ws_in("{\"op\":\"ask.close\",\"id\":\"a_1\",\"reason\":\"answered\"}");
  TEST_ASSERT_EQUAL_STRING("a_2", core_ui_model()->ask.id);
  TEST_ASSERT_EQUAL_UINT64(fake_now() + 600, core_ui_model()->ask.locked_until_ms); /* a fresh lock */
  fake_ws_drop(1006);
  fake_run(10);
  TEST_ASSERT_EQUAL_UINT8(0, core_ui_model()->ask.n_options); /* the host sends open asks again */
  fake_run(2000);
  fake_handshake();
  TEST_ASSERT_EQUAL_INT(UI_SCREEN_IDLE, core_ui_model()->screen);
}

static void test_ask_expiry(void) {
  fake_ready("amoled-175c");
  fake_ws_in("{\"op\":\"ask\",\"id\":\"a_5\",\"kind\":\"permission\",\"title\":\"Quick\",\"body\":\"\",\"options\":["
             "{\"id\":\"allow\",\"label\":\"Allow\"},{\"id\":\"deny\",\"label\":\"Deny\"}],\"expires_s\":5}");
  fake_run(4900);
  TEST_ASSERT_EQUAL_INT(UI_SCREEN_ASK, core_ui_model()->screen);
  fake_run(200);
  TEST_ASSERT_EQUAL_INT(UI_SCREEN_IDLE, core_ui_model()->screen);
}

static void test_cards_show_expire_and_dismiss(void) {
  fake_ready("lcd-154");
  fake_ws_in("{\"op\":\"card\",\"id\":\"c1\",\"title\":\"Build passed\",\"body\":\"main is green\",\"ttl_s\":3}");
  fake_run(10);
  TEST_ASSERT_EQUAL_INT(UI_SCREEN_CARD, core_ui_model()->screen);
  TEST_ASSERT_EQUAL_STRING("Build passed", core_ui_model()->card.title);
  fake_run(3000);
  TEST_ASSERT_EQUAL_INT(UI_SCREEN_IDLE, core_ui_model()->screen);
  fake_ws_in("{\"op\":\"card\",\"id\":\"c2\",\"title\":\"Sticky\",\"body\":\"\",\"ttl_s\":0}");
  for (int i = 0; i < 6; i++) {
    fake_run(10000);
    fake_ws_ping_in(); /* the host's ping keeps the session alive */
  }
  TEST_ASSERT_EQUAL_INT(UI_SCREEN_CARD, core_ui_model()->screen); /* ttl 0: until dismissed */
  fake_ws_ping_in();
  fake_input(GADGET_IN_CANCEL_DOWN, 0, 0);
  fake_run(10);
  TEST_ASSERT_EQUAL_INT(UI_SCREEN_IDLE, core_ui_model()->screen);
  fake_ws_in("{\"op\":\"card\",\"id\":\"c3\",\"title\":\"x\",\"body\":\"\"}");
  fake_ws_in("{\"op\":\"card.close\",\"id\":\"c3\"}");
  fake_run(10);
  TEST_ASSERT_EQUAL_INT(UI_SCREEN_IDLE, core_ui_model()->screen);
}

static void send_image(uint8_t stream, uint16_t w, uint16_t h, size_t rows_bytes) {
  char begin[160];
  snprintf(begin, sizeof begin, "{\"op\":\"image.begin\",\"id\":\"i1\",\"stream\":%u,\"w\":%u,\"h\":%u,\"ttl_s\":0}",
           (unsigned)stream, (unsigned)w, (unsigned)h);
  fake_ws_in(begin);
  static uint8_t frame[2 + 8190];
  size_t sent = 0;
  while (sent < rows_bytes) {
    size_t n = rows_bytes - sent > 8190 ? 8190 : rows_bytes - sent;
    frame[0] = 0x03;
    frame[1] = stream;
    for (size_t i = 0; i < n; i++) frame[2 + i] = (uint8_t)((sent + i) & 0xff);
    fake_ws_bin_in(frame, 2 + n);
    sent += n;
  }
  char end[64];
  snprintf(end, sizeof end, "{\"op\":\"image.end\",\"stream\":%u}", (unsigned)stream);
  fake_ws_in(end);
  fake_run(10);
}

static void test_images(void) {
  fake_ready("amoled-175c");
  fake_ws_in("{\"op\":\"card\",\"id\":\"c1\",\"title\":\"Under\",\"body\":\"\"}");
  send_image(5, 300, 200, 300u * 200u * 2u);
  const ui_model_t *m = core_ui_model();
  TEST_ASSERT_EQUAL_INT(UI_SCREEN_IMAGE, m->screen); /* an image shows over a card */
  TEST_ASSERT_EQUAL_UINT16(300, m->image.w);
  TEST_ASSERT_NOT_NULL(m->image.pixels);
  const uint8_t *bytes = (const uint8_t *)(const void *)m->image.pixels;
  TEST_ASSERT_EQUAL_HEX8(0x00, bytes[0]);
  TEST_ASSERT_EQUAL_HEX8(0x07, bytes[8199]); /* (8190 + 9) & 0xff: rows continue across frames */
  uint32_t rev = m->image.pixels_rev;
  fake_ws_in("{\"op\":\"card.close\",\"id\":\"i1\"}");
  fake_run(10);
  TEST_ASSERT_EQUAL_INT(UI_SCREEN_CARD, core_ui_model()->screen);
  TEST_ASSERT_TRUE(core_ui_model()->image.pixels_rev != rev);
  TEST_ASSERT_NULL(core_ui_model()->image.pixels);
  send_image(6, 301, 200, 301u * 200u * 2u); /* larger than caps.image: ignored */
  TEST_ASSERT_EQUAL_INT(UI_SCREEN_CARD, core_ui_model()->screen);
  send_image(7, 100, 100, 1000); /* incomplete: dropped */
  TEST_ASSERT_EQUAL_INT(UI_SCREEN_CARD, core_ui_model()->screen);
}

/* Ports send raw touch events (contract §2.5). A swipe down worked out from
 * them dismisses the image, then the card, on a board with no CANCEL button. */
static void swipe_down_by_touch(void) {
  fake_input(GADGET_IN_TOUCH_DOWN, 233, 100);
  fake_run(100);
  fake_input(GADGET_IN_TOUCH_MOVE, 233, 210); /* 110 px down within 300 ms */
  fake_input(GADGET_IN_TOUCH_UP, 233, 250);
  fake_run(10);
}

static void test_a_touch_swipe_dismisses_the_image_then_the_card(void) {
  fake_ready("amoled-175");
  fake_ws_in("{\"op\":\"card\",\"id\":\"c1\",\"title\":\"Under\",\"body\":\"\"}");
  send_image(5, 100, 100, 100u * 100u * 2u);
  TEST_ASSERT_EQUAL_INT(UI_SCREEN_IMAGE, core_ui_model()->screen);
  swipe_down_by_touch();
  TEST_ASSERT_EQUAL_INT(UI_SCREEN_CARD, core_ui_model()->screen);
  swipe_down_by_touch();
  TEST_ASSERT_EQUAL_INT(UI_SCREEN_IDLE, core_ui_model()->screen);
  TEST_ASSERT_EQUAL_size_t(0, fake_ws_count("voice.begin"));
}

static void test_posts_toast_and_chime(void) {
  fake_ready("amoled-175c");
  fake_ws_in("{\"op\":\"post\",\"id\":\"p1\",\"bot\":{\"id\":\"b_jev\",\"name\":\"Jev\"},\"kind\":\"routine\","
             "\"text\":\"Morning brief is ready\",\"speak\":false}");
  fake_run(10);
  const ui_model_t *m = core_ui_model();
  TEST_ASSERT_TRUE(m->toast.visible);
  TEST_ASSERT_EQUAL_INT(UI_POST_ROUTINE, m->toast.kind);
  TEST_ASSERT_EQUAL_STRING("Jev", m->toast.bot_name);
  TEST_ASSERT_EQUAL_STRING("Morning brief is ready", m->toast.text);
  TEST_ASSERT_EQUAL_INT(UI_SCREEN_IDLE, m->screen);
  TEST_ASSERT_EQUAL_INT(UI_MAUS_NOTIFYING, m->maus);
  fake_run(400);
  TEST_ASSERT_EQUAL_size_t(4800, fake_spk_accepted()); /* 300 ms at 16 kHz */
  TEST_ASSERT_TRUE(fake_spk_peak() > 5000 && fake_spk_peak() <= 6000);
  fake_run(7700);
  TEST_ASSERT_FALSE(core_ui_model()->toast.visible);
  TEST_ASSERT_EQUAL_INT(UI_MAUS_IDLE, core_ui_model()->maus);
  /* a tap hides the toast */
  fake_ws_in("{\"op\":\"post\",\"id\":\"p2\",\"bot\":{\"id\":\"b_jev\",\"name\":\"Jev\"},\"kind\":\"message\","
             "\"text\":\"Done\",\"speak\":false}");
  fake_input(GADGET_IN_TOUCH_DOWN, 233, 233);
  fake_input(GADGET_IN_TOUCH_UP, 233, 233);
  fake_run(10);
  TEST_ASSERT_FALSE(core_ui_model()->toast.visible);
}

static void test_spoken_post_chimes_then_speaks(void) {
  fake_ready("amoled-175c");
  int stops = fake_spk_stops();
  fake_ws_in("{\"op\":\"post\",\"id\":\"p1\",\"bot\":{\"id\":\"b_jev\",\"name\":\"Jev\"},\"kind\":\"message\","
             "\"text\":\"Your build passed\",\"speak\":true}");
  fake_ws_in("{\"op\":\"speak.begin\",\"stream\":2,\"rate\":16000}"); /* no turn: the post's speech */
  static uint8_t frame[2 + 1280];
  frame[0] = 0x02;
  frame[1] = 2;
  for (int i = 0; i < 640; i++) frame[2 + 2 * i] = (uint8_t)(i % 2 ? 0x40 : 0xc0);
  for (int i = 0; i < 10; i++) fake_ws_bin_in(frame, sizeof frame); /* 10 x 40 ms */
  fake_ws_in("{\"op\":\"speak.end\",\"stream\":2}");
  fake_run(1500);
  TEST_ASSERT_TRUE(fake_spk_accepted() >= 4800 + 6400); /* the whole chime, then the speech */
  TEST_ASSERT_EQUAL_INT(stops, fake_spk_stops());        /* the chime was never cut */
  const ui_model_t *m = core_ui_model();
  TEST_ASSERT_EQUAL_INT(UI_SCREEN_IDLE, m->screen);
  TEST_ASSERT_TRUE(m->toast.visible);
  TEST_ASSERT_EQUAL_INT(UI_MAUS_NOTIFYING, m->maus);
}

static void test_no_chime_over_speech(void) {
  fake_ready("amoled-175c");
  fake_console_in("say \"hi\"");
  cJSON *say = fake_ws_last("say");
  char json[160];
  snprintf(json, sizeof json, "{\"op\":\"speak.begin\",\"stream\":1,\"rate\":16000,\"turn\":\"%s\"}",
           cJSON_GetObjectItem(say, "turn")->valuestring);
  cJSON_Delete(say);
  fake_ws_in(json);
  static uint8_t frame[2 + 1280];
  frame[0] = 0x02;
  frame[1] = 1;
  for (int i = 0; i < 10; i++) {
    fake_ws_bin_in(frame, sizeof frame);
    fake_run(40);
  }
  fake_ws_in("{\"op\":\"post\",\"id\":\"p1\",\"bot\":{\"id\":\"b\",\"name\":\"Jev\"},\"kind\":\"message\",\"text\":\"x\"}");
  fake_run(10);
  TEST_ASSERT_EQUAL_INT(UI_SCREEN_SPEAKING, core_ui_model()->screen); /* the speech keeps playing */
  TEST_ASSERT_EQUAL_INT16(0, fake_spk_peak());                     /* and no chime was mixed in */
}

static void test_battery_and_sense(void) {
  fake_battery_set(true, 82, false);
  fake_ready("amoled-175c");
  fake_run(1000);
  TEST_ASSERT_TRUE(core_ui_model()->battery.present);
  TEST_ASSERT_EQUAL_UINT8(82, core_ui_model()->battery.pct);
  fake_battery_set(true, 81, false);
  for (int i = 0; i < 8; i++) {
    fake_run(1000);
    fake_ws_ping_in();
  }
  TEST_ASSERT_EQUAL_size_t(0, fake_ws_count("sense")); /* at most one every 10 s */
  fake_run(2000);
  TEST_ASSERT_EQUAL_size_t(1, fake_ws_count("sense"));
  cJSON *s = fake_ws_last("sense");
  TEST_ASSERT_EQUAL_INT(81, cJSON_GetObjectItem(s, "battery_pct")->valueint);
  TEST_ASSERT_FALSE(cJSON_IsTrue(cJSON_GetObjectItem(s, "charging")));
  cJSON_Delete(s);
  fake_run(10000);
  TEST_ASSERT_EQUAL_size_t(1, fake_ws_count("sense")); /* nothing changed */
  fake_battery_set(true, 81, true);
  fake_ws_ping_in();
  fake_run(1100);
  TEST_ASSERT_EQUAL_size_t(2, fake_ws_count("sense"));
}

static void test_boards_without_a_battery_never_sense(void) {
  fake_ready("devkit");
  fake_run(30000);
  TEST_ASSERT_FALSE(core_ui_model()->battery.present);
  TEST_ASSERT_EQUAL_size_t(0, fake_ws_count("sense"));
}

int main(void) {
  if (psa_crypto_init() != PSA_SUCCESS) return 3;
  UNITY_BEGIN();
  RUN_TEST(test_permission_ask_on_a_touch_board);
  RUN_TEST(test_buttons_answer_on_a_button_board);
  RUN_TEST(test_unanswerable_asks);
  RUN_TEST(test_asks_queue_one_at_a_time);
  RUN_TEST(test_ask_expiry);
  RUN_TEST(test_cards_show_expire_and_dismiss);
  RUN_TEST(test_images);
  RUN_TEST(test_a_touch_swipe_dismisses_the_image_then_the_card);
  RUN_TEST(test_posts_toast_and_chime);
  RUN_TEST(test_spoken_post_chimes_then_speaks);
  RUN_TEST(test_no_chime_over_speech);
  RUN_TEST(test_battery_and_sense);
  RUN_TEST(test_boards_without_a_battery_never_sense);
  return UNITY_END();
}
```

Change `firmware/tests/CMakeLists.txt` exactly as this diff shows (`-` lines go, `+` lines come; the rest is context):

```diff
--- a/firmware/tests/CMakeLists.txt
+++ b/firmware/tests/CMakeLists.txt
@@ -30,6 +30,7 @@ gadget_unit_test(session)
 gadget_unit_test(turns)
 gadget_unit_test(console)
 gadget_unit_test(audio)
+gadget_unit_test(display)
 
 # protocol/vectors in C (label vectors).
 add_executable(test_vectors test_vectors.c)
```

- [ ] **Step 2: Run it to see it fail**

Run: `cmake --build build/host -j10 && ./build/host/tests/test_display`
Expected: it builds and fails with `test_permission_ask_on_a_touch_board:FAIL: Expected 8 Was 3` (the screen stays Idle, not Ask). A later test may end the run with a segmentation fault (exit 139) when it reads a frame that was never sent.

- [ ] **Step 3: Write the implementation**

Create `firmware/core/src/display.c`:

```c
/* firmware/core/src/display.c */
/* SPDX-License-Identifier: Apache-2.0 */
/* What the host puts on the screen (spec §4.5-§4.7, §5.4): asks and answers,
 * cards, images, post toasts with a chime, and battery `sense`. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "core_internal.h"
#include "gadget_ui.h"

#define TAG "display"
#define ASK_QUEUE 8
#define TOAST_MS 8000u
#define BATTERY_POLL_MS 1000u

typedef struct {
  char id[UI_ID_MAX];
  bool question;
  char title[UI_TITLE_MAX];
  char body[UI_BODY_MAX];
  uint8_t n;
  struct {
    char id[UI_ID_MAX];
    char label[UI_LABEL_MAX];
    ui_option_style_t style;
  } opt[UI_ASK_OPTIONS_MAX];
  uint32_t expires_s;
} ask_t;

static struct {
  ask_t q[ASK_QUEUE]; /* q[0] is on screen */
  size_t n;
  uint64_t expires_at;
  /* image being received */
  bool img_rx;
  uint8_t img_stream;
  char img_id[UI_ID_MAX];
  uint16_t img_w, img_h;
  uint32_t img_ttl_s;
  uint8_t *img_buf;
  size_t img_got;
  uint8_t *img_shown; /* published pixels, owned here */
  /* battery and sense */
  uint64_t batt_polled;
  bool batt_ok;
  gadget_battery_t batt;
  bool sense_base_ok;
  gadget_battery_t sense_base;
  uint64_t sense_at;
} D;

static bool touch_board(void) { return (g_core.board->input_mask & GADGET_INPUT_TOUCH) != 0; }

/* ---- asks ------------------------------------------------------------------------ */

static void show_head(void) {
  ui_model_t *m = &g_core.model;
  memset(&m->ask, 0, sizeof m->ask);
  m->ask.chosen = -1;
  g_core.f.ask_visible = D.n > 0;
  if (D.n == 0) return;
  const ask_t *a = &D.q[0];
  snprintf(m->ask.id, sizeof m->ask.id, "%s", a->id);
  m->ask.question = a->question;
  snprintf(m->ask.title, sizeof m->ask.title, "%s", a->title);
  snprintf(m->ask.body, sizeof m->ask.body, "%s", a->body);
  m->ask.n_options = a->n;
  gadget_rect_t rects[UI_ASK_OPTIONS_MAX] = {{0}};
  if (touch_board()) ui_layout_ask(g_core.board, a->n, rects);
  for (uint8_t i = 0; i < a->n; i++) {
    snprintf(m->ask.options[i].id, sizeof m->ask.options[i].id, "%s", a->opt[i].id);
    snprintf(m->ask.options[i].label, sizeof m->ask.options[i].label, "%s", a->opt[i].label);
    m->ask.options[i].style = a->opt[i].style;
    m->ask.options[i].rect = rects[i];
  }
  m->ask.answerable = a->n >= 1 && a->n <= (touch_board() ? UI_ASK_OPTIONS_MAX : 2);
  m->ask.locked_until_ms = g_core.now + GADGET_ASK_LOCK_MS;
  m->ask.queued = (uint8_t)(D.n - 1);
  D.expires_at = a->expires_s ? g_core.now + (uint64_t)a->expires_s * 1000u : 0;
}

static void remove_ask(size_t i) {
  if (i >= D.n) return;
  memmove(&D.q[i], &D.q[i + 1], (D.n - i - 1) * sizeof D.q[0]);
  D.n--;
  if (i == 0) {
    show_head();
  } else {
    g_core.model.ask.queued = (uint8_t)(D.n - 1);
  }
}

static void on_ask(const gp_ask_t *a) {
  size_t i = 0;
  while (i < D.n && strcmp(D.q[i].id, a->id) != 0) i++;
  if (i == D.n) {
    if (D.n == ASK_QUEUE) {
      hal_log(GADGET_LOG_WARN, TAG, "ask queue full; dropping %s", a->id);
      return;
    }
    D.n++;
  }
  ask_t *q = &D.q[i];
  memset(q, 0, sizeof *q);
  snprintf(q->id, sizeof q->id, "%s", a->id);
  q->question = a->kind == GP_ASK_QUESTION;
  core_text_copy(q->title, sizeof q->title, a->title);
  core_text_copy(q->body, sizeof q->body, a->body ? a->body : "");
  q->n = a->n_options;
  for (uint8_t k = 0; k < a->n_options; k++) {
    snprintf(q->opt[k].id, sizeof q->opt[k].id, "%s", a->options[k].id);
    core_text_copy(q->opt[k].label, sizeof q->opt[k].label, a->options[k].label);
    q->opt[k].style = a->options[k].style == GP_STYLE_ALLOW  ? UI_STYLE_ALLOW
                      : a->options[k].style == GP_STYLE_DENY ? UI_STYLE_DENY
                                                             : UI_STYLE_NEUTRAL;
  }
  q->expires_s = a->expires_s;
  if (i == 0) {
    show_head();
  } else {
    g_core.model.ask.queued = (uint8_t)(D.n - 1);
  }
}

static void answer(int idx) {
  ui_model_t *m = &g_core.model;
  if (idx < 0 || idx >= m->ask.n_options || m->ask.chosen >= 0 || !m->ask.answerable) return;
  if (g_core.now < m->ask.locked_until_ms) return; /* presses in the first 0.6 s are ignored */
  gp_answer_t ans = {.id = m->ask.id, .option = m->ask.options[idx].id};
  if (session_send("answer", g_core_tx, gp_encode_answer(g_core_tx, sizeof g_core_tx, &ans)) == GADGET_OK) {
    m->ask.chosen = (int8_t)idx;
  }
}

bool display_ask_input(const gadget_input_t *in) {
  if (!g_core.f.ask_visible) return false;
  if (touch_board()) {
    switch (in->type) {
      case GADGET_IN_TOUCH_DOWN:
      case GADGET_IN_TOUCH_MOVE:
        return true;
      case GADGET_IN_TOUCH_UP:
        answer(ui_hit_test(&g_core.model, in->x, in->y));
        return true;
      default:
        return false; /* TALK and CANCEL keep their meaning on touch boards */
    }
  }
  const ui_model_t *m = &g_core.model;
  if (!m->ask.answerable) return false;
  if (in->type == GADGET_IN_TALK_DOWN) {
    answer(0);
    return true;
  }
  if (in->type == GADGET_IN_TALK_UP) return true;
  if (m->ask.n_options == 2 && (in->type == GADGET_IN_CANCEL_DOWN || in->type == GADGET_IN_CANCEL_UP)) {
    if (in->type == GADGET_IN_CANCEL_DOWN) answer(1);
    return true;
  }
  return false;
}

/* ---- cards and images ------------------------------------------------------------ */

static void hide_card(void) {
  memset(&g_core.model.card, 0, sizeof g_core.model.card);
  g_core.f.card_visible = false;
}

static void hide_image(void) {
  ui_model_t *m = &g_core.model;
  free(D.img_shown);
  D.img_shown = NULL;
  uint32_t rev = m->image.pixels_rev;
  memset(&m->image, 0, sizeof m->image);
  m->image.pixels_rev = rev + 1;
  g_core.f.image_visible = false;
}

static void on_card(const gp_card_t *c) {
  ui_model_t *m = &g_core.model;
  snprintf(m->card.id, sizeof m->card.id, "%s", c->id);
  core_text_copy(m->card.title, sizeof m->card.title, c->title);
  core_text_copy(m->card.body, sizeof m->card.body, c->body ? c->body : "");
  m->card.expires_ms = c->ttl_s ? g_core.now + (uint64_t)c->ttl_s * 1000u : 0;
  g_core.f.card_visible = true;
}

static void image_rx_reset(void) {
  free(D.img_buf);
  D.img_buf = NULL;
  D.img_rx = false;
  D.img_got = 0;
}

static void on_image_begin(const gp_image_begin_t *b) {
  image_rx_reset();
  if (b->w > g_core.board->image_w || b->h > g_core.board->image_h) {
    hal_log(GADGET_LOG_WARN, TAG, "image %ux%u is larger than caps.image", (unsigned)b->w, (unsigned)b->h);
    return;
  }
  D.img_buf = malloc((size_t)b->w * b->h * 2u);
  if (D.img_buf == NULL) return;
  D.img_rx = true;
  D.img_stream = b->stream;
  snprintf(D.img_id, sizeof D.img_id, "%s", b->id);
  D.img_w = b->w;
  D.img_h = b->h;
  D.img_ttl_s = b->ttl_s;
}

static void on_image_rows(uint8_t stream, const uint8_t *payload, size_t len) {
  if (!D.img_rx || stream != D.img_stream) return;
  size_t total = (size_t)D.img_w * D.img_h * 2u;
  size_t n = len > total - D.img_got ? total - D.img_got : len;
  memcpy(D.img_buf + D.img_got, payload, n);
  D.img_got += n;
}

static void on_image_end(uint8_t stream) {
  if (!D.img_rx || stream != D.img_stream) return;
  if (D.img_got != (size_t)D.img_w * D.img_h * 2u) {
    hal_log(GADGET_LOG_WARN, TAG, "image %s incomplete; dropped", D.img_id);
    image_rx_reset();
    return;
  }
  hide_image();
  ui_model_t *m = &g_core.model;
  D.img_shown = D.img_buf;
  D.img_buf = NULL;
  D.img_rx = false;
  snprintf(m->image.id, sizeof m->image.id, "%s", D.img_id);
  m->image.w = D.img_w;
  m->image.h = D.img_h;
  m->image.pixels = (const uint16_t *)(const void *)D.img_shown;
  m->image.pixels_rev++;
  m->image.expires_ms = D.img_ttl_s ? g_core.now + (uint64_t)D.img_ttl_s * 1000u : 0;
  g_core.f.image_visible = true;
}

/* ---- posts ------------------------------------------------------------------------- */

static void on_post(const gp_post_t *p) {
  ui_model_t *m = &g_core.model;
  m->toast.visible = true;
  m->toast.kind = p->kind == GP_POST_ROUTINE ? UI_POST_ROUTINE : UI_POST_MESSAGE;
  core_text_copy(m->toast.bot_name, sizeof m->toast.bot_name, p->bot.name);
  core_text_copy(m->toast.text, sizeof m->toast.text, p->text);
  m->toast.until_ms = g_core.now + TOAST_MS;
  g_core.f.toast_visible = true;
  audio_play_chime();
}

static void hide_toast(void) {
  g_core.model.toast.visible = false;
  g_core.f.toast_visible = false;
}

/* ---- module API ---------------------------------------------------------------------- */

bool display_on_msg(const gp_msg_t *m) {
  switch (m->op) {
    case GP_OP_ASK:
      on_ask(&m->m.ask);
      break;
    case GP_OP_ASK_CLOSE:
      for (size_t i = 0; i < D.n; i++) {
        if (strcmp(D.q[i].id, m->m.ask_close.id) == 0) {
          remove_ask(i);
          break;
        }
      }
      break;
    case GP_OP_POST:
      on_post(&m->m.post);
      break;
    case GP_OP_CARD:
      on_card(&m->m.card);
      break;
    case GP_OP_CARD_CLOSE:
      if (g_core.f.card_visible && strcmp(g_core.model.card.id, m->m.card_close.id) == 0) hide_card();
      if (g_core.f.image_visible && strcmp(g_core.model.image.id, m->m.card_close.id) == 0) hide_image();
      break;
    case GP_OP_IMAGE_BEGIN:
      on_image_begin(&m->m.image_begin);
      break;
    case GP_OP_IMAGE_END:
      on_image_end(m->m.image_end.stream);
      break;
    default:
      return false;
  }
  return true;
}

void display_on_binary(uint8_t stream, const uint8_t *payload, size_t len) { on_image_rows(stream, payload, len); }

bool display_dismiss(void) {
  if (g_core.f.image_visible) {
    hide_image();
    return true;
  }
  if (g_core.f.card_visible) {
    hide_card();
    return true;
  }
  if (g_core.f.toast_visible) {
    hide_toast();
    return true;
  }
  return false;
}

bool display_tap(void) {
  if (!g_core.f.toast_visible) return false;
  hide_toast();
  return true;
}

static void battery_tick(void) {
  uint64_t now = g_core.now;
  ui_model_t *m = &g_core.model;
  if (!g_core.board->has_battery) return;
  if (D.batt_polled != 0 && now - D.batt_polled < BATTERY_POLL_MS) return;
  D.batt_polled = now;
  gadget_battery_t b;
  D.batt_ok = hal_battery_read(&b);
  if (D.batt_ok) {
    if (b.pct > 100) b.pct = 100;
    D.batt = b;
  }
  m->battery.present = D.batt_ok;
  m->battery.pct = D.batt_ok ? D.batt.pct : 0;
  m->battery.charging = D.batt_ok && D.batt.charging;
  if (!session_ready() || !D.batt_ok) return;
  if (!D.sense_base_ok) { /* hello carried this reading */
    D.sense_base = D.batt;
    D.sense_base_ok = true;
    D.sense_at = now;
    return;
  }
  bool changed = D.batt.pct != D.sense_base.pct || D.batt.charging != D.sense_base.charging;
  if (changed && now - D.sense_at >= GADGET_SENSE_MIN_INTERVAL_MS) {
    gp_sense_t s = {.battery_valid = true, .battery_pct = D.batt.pct, .charging = D.batt.charging};
    if (session_send("sense", g_core_tx, gp_encode_sense(g_core_tx, sizeof g_core_tx, &s)) == GADGET_OK) {
      D.sense_base = D.batt;
      D.sense_at = now;
    }
  }
}

void display_tick(void) {
  uint64_t now = g_core.now;
  ui_model_t *m = &g_core.model;
  if (D.n > 0 && D.expires_at != 0 && now >= D.expires_at) remove_ask(0);
  if (g_core.f.card_visible && m->card.expires_ms != 0 && now >= m->card.expires_ms) hide_card();
  if (g_core.f.image_visible && m->image.expires_ms != 0 && now >= m->image.expires_ms) hide_image();
  if (g_core.f.toast_visible && now >= m->toast.until_ms) hide_toast();
  battery_tick();
}

void display_on_session_lost(void) {
  D.n = 0; /* open asks are sent again on reconnect */
  show_head();
  image_rx_reset();
  D.sense_base_ok = false;
}

void display_init(void) { memset(&D, 0, sizeof D); }

void display_deinit(void) {
  image_rx_reset();
  free(D.img_shown);
  memset(&D, 0, sizeof D);
}
```

Change `firmware/core/src/audio.c` exactly as this diff shows (`-` lines go, `+` lines come; the rest is context):

```diff
--- a/firmware/core/src/audio.c
+++ b/firmware/core/src/audio.c
@@ -27,6 +27,7 @@ static struct {
   size_t cap, head, count;         /* samples */
   bool ended;                      /* speak.end arrived */
   bool playing;                    /* prebuffer reached: feeding the HAL */
+  size_t carry;                    /* chime samples at the head, ahead of a spoken post's speech */
   bool overflow_logged;
   uint64_t received, written;      /* samples */
   struct {
@@ -62,6 +63,16 @@ void audio_stop_local(void) {
 }
 
 static bool begin_stream(uint8_t stream, uint32_t rate, const char *turn) {
+  if (A.active && A.stream == 0 && stream != 0 && turn == NULL && rate == A.rate) {
+    /* A post's chime is still playing when its speech begins (spec §4.6): the
+     * speech queues behind the chime's unplayed samples instead of cutting them. */
+    A.stream = stream;
+    A.ended = A.overflow_logged = false;
+    A.carry = A.count;
+    A.playing = A.carry > 0;
+    A.received = A.written = 0;
+    return true;
+  }
   audio_stop_local();
   if (g_core.board->speaker_rate == 0 || hal_spk_open(rate) != GADGET_OK) {
     hal_log(GADGET_LOG_WARN, TAG, "no speaker at %u Hz", (unsigned)rate);
@@ -74,7 +85,7 @@ static bool begin_stream(uint8_t stream, uint32_t rate, const char *turn) {
   A.stream = stream;
   A.rate = rate;
   snprintf(A.turn, sizeof A.turn, "%s", turn ? turn : "");
-  A.head = A.count = 0;
+  A.head = A.count = A.carry = 0;
   A.ended = A.playing = A.overflow_logged = false;
   A.received = A.written = 0;
   g_core.model.reply.speak_elapsed_ms = 0;
@@ -137,6 +148,7 @@ static void feed(void) {
     size_t run = A.cap - A.head;
     if (run > A.count) run = A.count;
     if (run > block) run = block;
+    if (A.carry > 0 && run > A.carry) run = A.carry;
     uint32_t ahead = hal_spk_buffered_ms();
     size_t n = hal_spk_write(&A.ring[A.head], run);
     if (n == 0) break;
@@ -149,6 +161,13 @@ static void feed(void) {
     A.head = (A.head + n) % A.cap;
     A.count -= n;
     A.written += n;
+    if (A.carry > 0) {
+      A.carry -= n;
+      if (A.carry == 0 && !A.ended) {
+        A.playing = false; /* the chime is out: the speech pre-buffers like any other */
+        break;
+      }
+    }
     if (n < run) break;
   }
 }
@@ -184,6 +203,48 @@ void audio_tick(void) {
 }
 
 bool audio_active(void) { return A.active; }
+bool audio_speech_active(void) { return A.active && A.stream != 0; }
+
+/* ---- chime: 300 ms, two tones, integer-only ------------------------------------- */
+
+#define CHIME_MS 300u
+#define CHIME_AMP 6000
+#define CHIME_FADE_MS 5u
+
+/* round(32767 * sin(i * pi / 32)), i = 0..16: a quarter wave */
+static const int16_t QSIN[17] = {0,     3212,  6393,  9512,  12539, 15446, 18204, 20787, 23170,
+                                 25329, 27245, 28898, 30273, 31356, 32137, 32609, 32767};
+
+/* sin of a 16-bit phase (65536 = one turn), linear between table points */
+static int32_t sine(uint16_t phase) {
+  uint32_t quad = phase >> 14;
+  uint32_t p = phase & 0x3FFFu;
+  if (quad & 1u) p = 0x4000u - p;
+  uint32_t i = p >> 10, frac = p & 1023u;
+  int32_t v = i >= 16 ? QSIN[16] : QSIN[i] + ((QSIN[i + 1] - QSIN[i]) * (int32_t)frac) / 1024;
+  return (quad & 2u) ? -v : v;
+}
+
+bool audio_play_chime(void) {
+  uint32_t rate = g_core.board->speaker_rate;
+  if (rate == 0 || g_core.f.recording) return false;
+  if (A.active && A.stream != 0) return false; /* never over speech */
+  if (!begin_stream(0, rate, NULL)) return false; /* stream 0: host streams are 1..255 */
+  size_t n = (size_t)rate * CHIME_MS / 1000u, half = n / 2, fade = (size_t)rate * CHIME_FADE_MS / 1000u;
+  uint32_t phase = 0;
+  for (size_t k = 0; k < n; k++) {
+    bool first = k < half;
+    phase += (first ? 880u : 1320u) * 65536u / rate;
+    size_t pos = first ? k : k - half, len = first ? half : n - half;
+    int32_t env = CHIME_AMP;
+    if (pos < fade) env = env * (int32_t)pos / (int32_t)fade;
+    else if (len - pos < fade) env = env * (int32_t)(len - pos) / (int32_t)fade;
+    int16_t s = (int16_t)(sine((uint16_t)phase) * env / 32767);
+    push_samples(&s, 1);
+  }
+  A.ended = true;
+  return true;
+}
 
 void audio_init(void) { memset(&A, 0, sizeof A); }
 
```

Change `firmware/core/src/interaction.c` exactly as this diff shows (`-` lines go, `+` lines come; the rest is context):

```diff
--- a/firmware/core/src/interaction.c
+++ b/firmware/core/src/interaction.c
@@ -158,16 +158,15 @@ static void begin_press(press_t src, int16_t x, int16_t y) {
   I.y0 = y;
   I.moved = false;
   I.pre_n = 0;
-  I.press_stopped_audio = false;
-  if (audio_active()) {
-    /* barge-in: stop playback now; tell the host if the turn is still running */
-    audio_stop_local();
-    I.press_stopped_audio = true;
+  I.press_stopped_audio = audio_speech_active();
+  if (I.press_stopped_audio) {
+    /* barge-in: stop the speech now; tell the host if the turn is still running */
     if (I.in_flight && !I.stop_sent) {
       send_stop(I.turn);
       I.stop_sent = true;
     }
   }
+  audio_stop_local(); /* speech or a chime: never record over playback */
   if (hal_mic_start(GADGET_MIC_RATE) != GADGET_OK) hal_log(GADGET_LOG_ERROR, TAG, "mic did not start");
 }
 
@@ -183,15 +182,20 @@ static void cancel_action(void) {
     cancel_recording();
     return;
   }
-  if (audio_active() || I.in_flight) {
-    audio_stop_local();
+  bool speech = audio_speech_active();
+  audio_stop_local();
+  if (speech || I.in_flight) {
     if (I.in_flight && !I.stop_sent) {
       send_stop(I.turn);
       I.stop_sent = true;
     }
     return;
   }
-  if (I.reply_shown) clear_turn_model();
+  if (I.reply_shown) {
+    clear_turn_model();
+    return;
+  }
+  display_dismiss();
 }
 
 /* A touch shorter than the press minimum. */
@@ -199,7 +203,11 @@ static void tap(int16_t x, int16_t y) {
   (void)x;
   (void)y;
   if (I.press_stopped_audio) return; /* that tap stopped the speech: the reply stays */
-  if (I.reply_shown && !I.in_flight) clear_turn_model();
+  if (I.reply_shown && !I.in_flight) {
+    clear_turn_model();
+    return;
+  }
+  display_tap();
 }
 
 static int16_t swipe_threshold(void) { return (int16_t)(g_core.board->screen_h / 8); }
@@ -214,6 +222,10 @@ void interaction_deinit(void) {
 }
 
 void interaction_input(const gadget_input_t *in) {
+  if (display_ask_input(in)) {
+    publish();
+    return;
+  }
   bool ready = session_ready() && !g_core.f.ota_active;
   switch (in->type) {
     case GADGET_IN_TALK_DOWN:
```

Change `firmware/core/src/core_internal.h` exactly as this diff shows (`-` lines go, `+` lines come; the rest is context):

```diff
--- a/firmware/core/src/core_internal.h
+++ b/firmware/core/src/core_internal.h
@@ -124,6 +124,21 @@ void audio_on_binary(uint8_t stream, const uint8_t *payload, size_t len);   /* s
 void audio_tick(void);
 void audio_stop_local(void);                    /* stop playback now (tap, barge-in, cancel) */
 bool audio_active(void);                        /* something is playing or buffered */
+bool audio_speech_active(void);                 /* host speech (not the chime) is playing */
+/* A 300 ms two-tone chime; false without a speaker, while recording or over speech. */
+bool audio_play_chime(void);
+
+/* ---- display.c ------------------------------------------------------------------ */
+void display_init(void);
+void display_deinit(void);
+bool display_on_msg(const gp_msg_t *m);         /* ask, ask.close, post, card, card.close, image.* */
+void display_on_binary(uint8_t stream, const uint8_t *payload, size_t len);  /* image rows */
+void display_tick(void);
+void display_on_session_lost(void);
+/* Input while an ask is open; true when the ask consumed it. */
+bool display_ask_input(const gadget_input_t *in);
+bool display_dismiss(void);                     /* CANCEL / swipe down: image, then card, then toast */
+bool display_tap(void);                         /* a tap hides the toast */
 
 /* ---- console_cmd.c ------------------------------------------------------------- */
 void console_exec_line(const char *line);       /* NULL: an over-long line was dropped */
```

Change `firmware/core/src/core.c` exactly as this diff shows (`-` lines go, `+` lines come; the rest is context):

```diff
--- a/firmware/core/src/core.c
+++ b/firmware/core/src/core.c
@@ -223,6 +223,7 @@ gadget_status_t core_init(const core_config_t *cfg) {
   session_init();
   interaction_init();
   audio_init();
+  display_init();
   if (g_core.wifi_ssid[0] != '\0') hal_wifi_connect(g_core.wifi_ssid, g_core.wifi_pass);
 
   cJSON *boot = cJSON_CreateObject();
@@ -277,6 +278,7 @@ void core_tick(uint64_t now_ms) {
   session_tick();
   interaction_tick();
   audio_tick();
+  display_tick();
   screens_update();
   model_commit();
 }
@@ -285,6 +287,7 @@ const ui_model_t *core_ui_model(void) { return &g_core.model; }
 
 void core_deinit(void) {
   if (g_core.initialized) {
+    display_deinit();
     audio_deinit();
     interaction_deinit();
     session_deinit();
@@ -315,11 +318,13 @@ void core_on_session_lost(void) {
   hal_log(GADGET_LOG_INFO, CORE_TAG, "session lost");
   audio_stop_local();
   interaction_on_session_lost();
+  display_on_session_lost();
 }
 
 void core_on_msg(const gp_msg_t *m) {
   if (interaction_on_msg(m)) return;
   if (audio_on_msg(m)) return;
+  if (display_on_msg(m)) return;
   switch (m->op) {
     default:
       hal_log(GADGET_LOG_DEBUG, CORE_TAG, "ignored %s", gp_op_name(m->op));
@@ -332,6 +337,9 @@ void core_on_binary(gp_bin_kind_t kind, uint8_t stream, const uint8_t *payload,
     case GP_BIN_SPEAKER:
       audio_on_binary(stream, payload, len);
       break;
+    case GP_BIN_IMAGE:
+      display_on_binary(stream, payload, len);
+      break;
     default:
       hal_log(GADGET_LOG_DEBUG, CORE_TAG, "ignored binary kind %d stream %u (%u bytes)", (int)kind, (unsigned)stream,
               (unsigned)len);
```

Change `firmware/core/CMakeLists.txt` exactly as this diff shows (`-` lines go, `+` lines come; the rest is context):

```diff
--- a/firmware/core/CMakeLists.txt
+++ b/firmware/core/CMakeLists.txt
@@ -15,6 +15,7 @@ set(GADGET_CORE_SRCS
   src/interaction.c
   src/console_cmd.c
   src/audio.c
+  src/display.c
 )
 
 if(ESP_PLATFORM)
```

- [ ] **Step 4: Run the tests**

Run: `cmake --build build/host -j10 && ctest --test-dir build/host --output-on-failure -L "unit|vectors"`
Expected: `100% tests passed, 0 tests failed out of 12`; `test_display` prints `13 Tests 0 Failures 0 Ignored`.

- [ ] **Step 5: Commit**

```bash
git add firmware/core firmware/tests/test_display.c firmware/tests/CMakeLists.txt
git commit -m "firmware: asks, cards, images, post toasts with a chime, battery sense" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 13: Actions

**Files:**
- Create: `firmware/core/src/actions.c`
- Modify: `firmware/core/src/session.c` (`hello` declares the actions), `firmware/core/src/core.c`, `firmware/core/src/core_internal.h`, `firmware/core/CMakeLists.txt`, `firmware/tests/CMakeLists.txt`
- Test: `firmware/tests/test_actions.c`

**Interfaces:**
- Consumes: `gadget_actions.h`; `gp_encode_hello` and `gp_encode_event` (Task 2); `session_ready` and `session_send` (Task 8); `audio_play_chime` (Task 12).
- Produces: `gadget_action_register` (public, contract §2.10), `gadget_event_send` (public, contract §2.10; Contract deviations, item 9), `actions_init` (registers `chime`), `actions_deinit`, `actions_on_msg` (`act` → exactly one `act.result`) and `actions_decls(&n)` for `hello`.
- Registration rules:
  - The name must match `/^[a-z][a-z0-9_.-]{0,31}$/`. The description is 1–200 code points. The schema must be a JSON object of at most 1024 bytes once re-serialized compactly; `NULL` means `{"type":"object","properties":{}}`. There are at most 16 actions.
  - A registration that would push the `hello` past 16 KiB is refused with `GADGET_ERR_LIMIT`. The check encodes the `hello` with the longest name there can be (32 × `…`, 96 bytes) and battery, so a later rename never pushes it over.
  - A duplicate name returns `GADGET_ERR_STATE`.
  - Registration needs `core_init()` first (otherwise `GADGET_ERR_STATE`). Ports and makers register between `core_init()` and the first `core_tick()`.
- `act` behavior: an unknown name gets `{"ok":false,"error":"unknown action"}`. A handler's error text is folded to the gadget charset, and an empty `data` object is omitted.
- `gadget_event_send` rules (spec §4.7 and §7, so `recent_events` can fill):
  - The name follows the action-name rule (`GADGET_ERR_ARG` otherwise). The data is any JSON value or `NULL` (then `data` is omitted), at most 1024 bytes once serialized compactly (`GADGET_ERR_LIMIT` otherwise).
  - Without a ready session (offline, mid-handshake, or before `core_init()`) it returns `GADGET_ERR_BUSY` and queues nothing. The name and size checks come first, so a maker sees the same error offline.
  - It encodes with `gp_encode_event` and sends at once through `session_send`, which taps it like any other frame.

- [ ] **Step 1: Write the failing test**

Create `firmware/tests/test_actions.c`:

```c
/* firmware/tests/test_actions.c */
/* SPDX-License-Identifier: Apache-2.0 */
/* Action declarations, limits, act/act.result and events (core/src/actions.c). */
#include <string.h>
#include "fake_hal.h"
#include "gadget_actions.h"
#include "psa/crypto.h"
#include "unity.h"

void setUp(void) { fake_reset(); }
void tearDown(void) { core_deinit(); }

static int g_calls;
static bool relay(const cJSON *args, cJSON *data, char *error, size_t cap) {
  g_calls++;
  const cJSON *on = cJSON_GetObjectItem(args, "on");
  if (!cJSON_IsBool(on)) {
    snprintf(error, cap, "on must be true or false");
    return false;
  }
  cJSON_AddBoolToObject(data, "on", cJSON_IsTrue(on));
  return true;
}

static bool counts_args(const cJSON *args, cJSON *data, char *error, size_t cap) {
  (void)error;
  (void)cap;
  cJSON_AddNumberToObject(data, "n", cJSON_GetArraySize(args));
  return true;
}

static void reconnect_and_get_hello(cJSON **hello) {
  fake_ws_drop(1006);
  fake_run(2000);
  fake_ws_accept();
  *hello = fake_ws_last("hello");
}

static void test_hello_declares_the_chime(void) {
  fake_store_paired();
  fake_boot("amoled-175c");
  fake_ws_accept();
  cJSON *hello = fake_ws_last("hello");
  char *actions = cJSON_PrintUnformatted(cJSON_GetObjectItem(hello, "actions"));
  TEST_ASSERT_EQUAL_STRING("[{\"name\":\"chime\",\"description\":\"Play a short chime.\","
                           "\"params\":{\"type\":\"object\",\"properties\":{}},\"risk\":\"safe\"}]",
                           actions);
  cJSON_free(actions);
  cJSON_Delete(hello);
}

static void test_registered_actions_are_declared(void) {
  fake_ready("amoled-175c");
  TEST_ASSERT_EQUAL_INT(GADGET_OK, gadget_action_register("relay.on", "Switch the desk lamp.",
                                                          "{ \"type\": \"object\", \"properties\": { \"on\": {\"type\":\"boolean\"} } }",
                                                          GADGET_RISK_CONFIRM, relay));
  cJSON *hello;
  reconnect_and_get_hello(&hello);
  cJSON *list = cJSON_GetObjectItem(hello, "actions");
  TEST_ASSERT_EQUAL_INT(2, cJSON_GetArraySize(list));
  char *second = cJSON_PrintUnformatted(cJSON_GetArrayItem(list, 1));
  TEST_ASSERT_EQUAL_STRING("{\"name\":\"relay.on\",\"description\":\"Switch the desk lamp.\","
                           "\"params\":{\"type\":\"object\",\"properties\":{\"on\":{\"type\":\"boolean\"}}},\"risk\":\"confirm\"}",
                           second);
  cJSON_free(second);
  cJSON_Delete(hello);
}

static void test_registration_limits(void) {
  fake_boot("amoled-175c");
  TEST_ASSERT_EQUAL_INT(GADGET_ERR_ARG, gadget_action_register("Relay", "x", NULL, GADGET_RISK_SAFE, relay));
  TEST_ASSERT_EQUAL_INT(GADGET_ERR_ARG, gadget_action_register("1abc", "x", NULL, GADGET_RISK_SAFE, relay));
  TEST_ASSERT_EQUAL_INT(GADGET_ERR_ARG, gadget_action_register("a b", "x", NULL, GADGET_RISK_SAFE, relay));
  TEST_ASSERT_EQUAL_INT(GADGET_ERR_ARG, gadget_action_register("", "x", NULL, GADGET_RISK_SAFE, relay));
  TEST_ASSERT_EQUAL_INT(GADGET_ERR_ARG,
                        gadget_action_register("abcdefghijklmnopqrstuvwxyz0123456", "x", NULL, GADGET_RISK_SAFE, relay));
  TEST_ASSERT_EQUAL_INT(GADGET_ERR_ARG, gadget_action_register("ok", "", NULL, GADGET_RISK_SAFE, relay));
  TEST_ASSERT_EQUAL_INT(GADGET_ERR_ARG, gadget_action_register("ok", "x", NULL, GADGET_RISK_SAFE, NULL));
  TEST_ASSERT_EQUAL_INT(GADGET_ERR_ARG, gadget_action_register("ok", "x", "{not json", GADGET_RISK_SAFE, relay));
  TEST_ASSERT_EQUAL_INT(GADGET_ERR_ARG, gadget_action_register("ok", "x", "[1,2]", GADGET_RISK_SAFE, relay));
  char desc[202];
  memset(desc, 'd', 201);
  desc[201] = '\0';
  TEST_ASSERT_EQUAL_INT(GADGET_ERR_LIMIT, gadget_action_register("ok", desc, NULL, GADGET_RISK_SAFE, relay));
  desc[200] = '\0';
  TEST_ASSERT_EQUAL_INT(GADGET_OK, gadget_action_register("ok", desc, NULL, GADGET_RISK_SAFE, relay));
  static char big[1200];
  int n = snprintf(big, sizeof big, "{\"description\":\"");
  memset(big + n, 'p', 1010);
  strcpy(big + n + 1010, "\"}");
  TEST_ASSERT_EQUAL_INT(GADGET_ERR_LIMIT, gadget_action_register("big", "x", big, GADGET_RISK_SAFE, relay));
  TEST_ASSERT_EQUAL_INT(GADGET_ERR_STATE, gadget_action_register("chime", "again", NULL, GADGET_RISK_SAFE, relay));
  char name[16];
  int added = 2; /* chime and ok */
  for (int i = 0; i < 20; i++) {
    snprintf(name, sizeof name, "a%d", i);
    if (gadget_action_register(name, "x", NULL, GADGET_RISK_SAFE, relay) == GADGET_OK) added++;
  }
  TEST_ASSERT_EQUAL_INT(16, added); /* at most 16 actions */
}

static void test_the_hello_never_exceeds_16_kib(void) {
  fake_store_paired();
  fake_boot("amoled-175c");
  static char schema[1100];
  int n = snprintf(schema, sizeof schema, "{\"description\":\"");
  memset(schema + n, 's', 990);
  strcpy(schema + n + 990, "\"}");
  char desc[201];
  memset(desc, 'd', 200);
  desc[200] = '\0';
  int ok = 0;
  char name[16];
  for (int i = 0; i < 15; i++) {
    snprintf(name, sizeof name, "big%d", i);
    if (gadget_action_register(name, desc, schema, GADGET_RISK_CONFIRM, relay) == GADGET_OK) ok++;
  }
  TEST_ASSERT_TRUE(ok > 5 && ok < 15); /* the 16 KiB hello limit stops it first */
  fake_ws_accept(); /* the fake asserts every text frame is <= 16 KiB */
  TEST_ASSERT_EQUAL_size_t(1, fake_ws_count("hello"));
}

static void test_a_longer_name_still_fits_the_hello(void) {
  fake_store_paired();
  fake_boot("amoled-175c"); /* named "Maus b18b" */
  static char schema[1100];
  int n = snprintf(schema, sizeof schema, "{\"description\":\"");
  memset(schema + n, 's', 990);
  strcpy(schema + n + 990, "\"}");
  char name[16];
  for (int i = 0; i < 15; i++) {
    snprintf(name, sizeof name, "big%d", i);
    if (gadget_action_register(name, "x", schema, GADGET_RISK_SAFE, relay) != GADGET_OK) break;
  }
  /* fill what is left to the byte with one more action */
  for (int len = 990; len > 0; len--) {
    n = snprintf(schema, sizeof schema, "{\"description\":\"");
    memset(schema + n, 's', (size_t)len);
    strcpy(schema + n + len, "\"}");
    if (gadget_action_register("fill", "x", schema, GADGET_RISK_SAFE, relay) == GADGET_OK) break;
  }
  fake_ws_accept();
  TEST_ASSERT_EQUAL_size_t(1, fake_ws_count("hello"));
  /* the longest name a person can give still leaves room */
  char line[160] = "name \"";
  for (int i = 0; i < 32; i++) strcat(line, "\xE2\x80\xA6");
  strcat(line, "\"");
  fake_console_in(line);
  fake_ws_drop(1006);
  fake_run(2000);
  fake_ws_accept();
  TEST_ASSERT_EQUAL_size_t(2, fake_ws_count("hello"));
}

static void test_act_runs_the_handler(void) {
  fake_ready("amoled-175c");
  gadget_action_register("relay.on", "Switch the desk lamp.", NULL, GADGET_RISK_CONFIRM, relay);
  gadget_action_register("count", "Count args.", NULL, GADGET_RISK_SAFE, counts_args);
  g_calls = 0;
  fake_ws_in("{\"op\":\"act\",\"id\":\"x1\",\"name\":\"relay.on\",\"args\":{\"on\":true}}");
  TEST_ASSERT_EQUAL_INT(1, g_calls);
  TEST_ASSERT_EQUAL_STRING("{\"op\":\"act.result\",\"id\":\"x1\",\"ok\":true,\"data\":{\"on\":true}}",
                           fake_ws_text(fake_ws_sent() - 1));
  fake_ws_in("{\"op\":\"act\",\"id\":\"x2\",\"name\":\"relay.on\",\"args\":{}}");
  TEST_ASSERT_EQUAL_STRING("{\"op\":\"act.result\",\"id\":\"x2\",\"ok\":false,\"error\":\"on must be true or false\"}",
                           fake_ws_text(fake_ws_sent() - 1));
  fake_ws_in("{\"op\":\"act\",\"id\":\"x3\",\"name\":\"count\"}");
  TEST_ASSERT_EQUAL_STRING("{\"op\":\"act.result\",\"id\":\"x3\",\"ok\":true,\"data\":{\"n\":0}}",
                           fake_ws_text(fake_ws_sent() - 1));
  fake_ws_in("{\"op\":\"act\",\"id\":\"x4\",\"name\":\"self.destruct\"}");
  TEST_ASSERT_EQUAL_STRING("{\"op\":\"act.result\",\"id\":\"x4\",\"ok\":false,\"error\":\"unknown action\"}",
                           fake_ws_text(fake_ws_sent() - 1));
  TEST_ASSERT_EQUAL_size_t(4, fake_ws_count("act.result")); /* exactly one per act */
}

static void test_act_chime_plays_and_returns_ok(void) {
  fake_ready("lcd-154");
  fake_ws_in("{\"op\":\"act\",\"id\":\"c1\",\"name\":\"chime\",\"args\":{}}");
  TEST_ASSERT_EQUAL_STRING("{\"op\":\"act.result\",\"id\":\"c1\",\"ok\":true}", fake_ws_text(fake_ws_sent() - 1));
  fake_run(400);
  TEST_ASSERT_EQUAL_size_t(4800, fake_spk_accepted());
}

static void test_event_send_while_ready_and_busy_otherwise(void) {
  fake_store_paired();
  fake_boot("amoled-175c");
  TEST_ASSERT_EQUAL_INT(GADGET_ERR_BUSY, gadget_event_send("button.long_press", NULL)); /* not connected */
  fake_handshake();
  TEST_ASSERT_EQUAL_INT(GADGET_OK, gadget_event_send("button.long_press", NULL));
  TEST_ASSERT_EQUAL_STRING("{\"op\":\"event\",\"name\":\"button.long_press\"}", fake_ws_text(fake_ws_sent() - 1));
  cJSON *data = cJSON_Parse("{\"knob\":3,\"room\":\"Kitchen\"}");
  TEST_ASSERT_EQUAL_INT(GADGET_OK, gadget_event_send("knob.turn", data));
  TEST_ASSERT_EQUAL_STRING("{\"op\":\"event\",\"name\":\"knob.turn\",\"data\":{\"knob\":3,\"room\":\"Kitchen\"}}",
                           fake_ws_text(fake_ws_sent() - 1));
  fake_ws_drop(1006);
  TEST_ASSERT_EQUAL_INT(GADGET_ERR_BUSY, gadget_event_send("knob.turn", data)); /* the session is gone */
  fake_run(2000);
  fake_ws_accept();
  TEST_ASSERT_EQUAL_size_t(1, fake_ws_count("hello"));
  TEST_ASSERT_EQUAL_INT(GADGET_ERR_BUSY, gadget_event_send("knob.turn", data)); /* hello sent, no ready yet */
  TEST_ASSERT_EQUAL_size_t(2, fake_ws_count("event")); /* nothing was queued while busy */
  cJSON_Delete(data);
}

static void test_event_send_checks_the_name_and_the_data_size(void) {
  fake_ready("amoled-175c");
  TEST_ASSERT_EQUAL_INT(GADGET_ERR_ARG, gadget_event_send(NULL, NULL));
  TEST_ASSERT_EQUAL_INT(GADGET_ERR_ARG, gadget_event_send("", NULL));
  TEST_ASSERT_EQUAL_INT(GADGET_ERR_ARG, gadget_event_send("Button", NULL));
  TEST_ASSERT_EQUAL_INT(GADGET_ERR_ARG, gadget_event_send("abcdefghijklmnopqrstuvwxyz0123456", NULL));
  static char text[1024];
  memset(text, 'x', 1022);
  text[1022] = '\0';
  cJSON *fits = cJSON_CreateString(text); /* "xx…x" serializes to exactly 1024 bytes */
  text[1022] = 'x';
  text[1023] = '\0';
  cJSON *over = cJSON_CreateString(text); /* 1025 bytes */
  TEST_ASSERT_EQUAL_INT(GADGET_OK, gadget_event_send("note", fits));
  TEST_ASSERT_EQUAL_size_t(strlen("{\"op\":\"event\",\"name\":\"note\",\"data\":}") + 1024,
                           strlen(fake_ws_text(fake_ws_sent() - 1)));
  TEST_ASSERT_EQUAL_INT(GADGET_ERR_LIMIT, gadget_event_send("note", over));
  TEST_ASSERT_EQUAL_size_t(1, fake_ws_count("event"));
  fake_ws_drop(1006); /* offline, the name and size checks still come first */
  TEST_ASSERT_EQUAL_INT(GADGET_ERR_ARG, gadget_event_send("Button", NULL));
  TEST_ASSERT_EQUAL_INT(GADGET_ERR_LIMIT, gadget_event_send("note", over));
  TEST_ASSERT_EQUAL_INT(GADGET_ERR_BUSY, gadget_event_send("note", fits));
  cJSON_Delete(fits);
  cJSON_Delete(over);
}

int main(void) {
  if (psa_crypto_init() != PSA_SUCCESS) return 3;
  UNITY_BEGIN();
  RUN_TEST(test_hello_declares_the_chime);
  RUN_TEST(test_registered_actions_are_declared);
  RUN_TEST(test_registration_limits);
  RUN_TEST(test_the_hello_never_exceeds_16_kib);
  RUN_TEST(test_a_longer_name_still_fits_the_hello);
  RUN_TEST(test_act_runs_the_handler);
  RUN_TEST(test_act_chime_plays_and_returns_ok);
  RUN_TEST(test_event_send_while_ready_and_busy_otherwise);
  RUN_TEST(test_event_send_checks_the_name_and_the_data_size);
  return UNITY_END();
}
```

Change `firmware/tests/CMakeLists.txt` exactly as this diff shows (`-` lines go, `+` lines come; the rest is context):

```diff
--- a/firmware/tests/CMakeLists.txt
+++ b/firmware/tests/CMakeLists.txt
@@ -31,6 +31,7 @@ gadget_unit_test(turns)
 gadget_unit_test(console)
 gadget_unit_test(audio)
 gadget_unit_test(display)
+gadget_unit_test(actions)
 
 # protocol/vectors in C (label vectors).
 add_executable(test_vectors test_vectors.c)
```

- [ ] **Step 2: Run it to see it fail**

Run: `cmake --build build/host -j10`
Expected: the link of `test_actions` fails on `gadget_action_register` and `gadget_event_send`.

- [ ] **Step 3: Write the implementation**

Create `firmware/core/src/actions.c`:

```c
/* firmware/core/src/actions.c */
/* SPDX-License-Identifier: Apache-2.0 */
/* Actions a gadget declares in hello and runs on `act`, and the events it
 * sends (gadget_actions.h, spec §4.3 limits, §4.7, §5.10). The built-in
 * `chime` is registered by core_init(). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "core_internal.h"
#include "gadget_actions.h"

#define TAG "actions"
#define EVENT_DATA_MAX 1024u /* an event's data, serialized compactly (gadget_actions.h) */

typedef struct {
  char name[GADGET_ACTION_NAME_MAX + 1];
  char description[GADGET_ACTION_DESC_MAX * 4 + 1]; /* 200 code points, up to 4 bytes each */
  char *params;                                      /* compact schema JSON (malloc), NULL = the default */
  gadget_risk_t risk;
  gadget_action_handler_t handler;
} action_t;

static action_t s_actions[GADGET_ACTIONS_MAX];
static uint8_t s_count;
static gp_action_decl_t s_decls[GADGET_ACTIONS_MAX];

static bool valid_name(const char *n) {
  size_t len = n ? strlen(n) : 0;
  if (len < 1 || len > GADGET_ACTION_NAME_MAX || n[0] < 'a' || n[0] > 'z') return false;
  for (size_t i = 1; i < len; i++) {
    char c = n[i];
    bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '.' || c == '-';
    if (!ok) return false;
  }
  return true;
}

static const action_t *find(const char *name) {
  for (uint8_t i = 0; i < s_count; i++) {
    if (strcmp(s_actions[i].name, name) == 0) return &s_actions[i];
  }
  return NULL;
}

const gp_action_decl_t *actions_decls(uint8_t *count) {
  for (uint8_t i = 0; i < s_count; i++) {
    s_decls[i].name = s_actions[i].name;
    s_decls[i].description = s_actions[i].description;
    s_decls[i].params_json = s_actions[i].params;
    s_decls[i].risk = s_actions[i].risk;
  }
  *count = s_count;
  return s_decls;
}

/* The hello with the current actions fits the text frame limit whatever the
 * name becomes later (console `name`, a desktop rename): 32 x "…" (96 bytes)
 * is longer than any name core_set_name keeps, and battery_pct 100 with
 * charging false is the longest battery. */
static bool hello_fits(void) {
  char longest[3 * GADGET_NAME_MAX + 1];
  for (size_t i = 0; i < GADGET_NAME_MAX; i++) memcpy(longest + 3 * i, "\xE2\x80\xA6", 3);
  longest[3 * GADGET_NAME_MAX] = '\0';
  uint8_t n = 0;
  const gp_action_decl_t *decls = actions_decls(&n);
  gp_hello_t h = {.id = g_core.id, .pubkey_b64 = g_core.pub_b64, .name = longest, .fw = g_core.fw,
                  .board = g_core.board, .actions = decls, .n_actions = n,
                  .battery_valid = g_core.board->has_battery, .battery_pct = 100, .charging = false};
  return gp_encode_hello(g_core_tx, sizeof g_core_tx, &h) > 0;
}

gadget_status_t gadget_action_register(const char *name, const char *description, const char *params_schema_json,
                                       gadget_risk_t risk, gadget_action_handler_t handler) {
  if (!g_core.initialized) return GADGET_ERR_STATE;
  if (!valid_name(name) || handler == NULL || (risk != GADGET_RISK_SAFE && risk != GADGET_RISK_CONFIRM)) {
    return GADGET_ERR_ARG;
  }
  size_t dlen = description ? gadget_utf8_len(description) : 0;
  if (dlen == 0) return GADGET_ERR_ARG;
  if (dlen > GADGET_ACTION_DESC_MAX || strlen(description) >= sizeof s_actions[0].description) return GADGET_ERR_LIMIT;
  if (find(name) != NULL) return GADGET_ERR_STATE;
  if (s_count >= GADGET_ACTIONS_MAX) return GADGET_ERR_LIMIT;
  char *params = NULL;
  if (params_schema_json != NULL) {
    cJSON *schema = cJSON_Parse(params_schema_json);
    if (!cJSON_IsObject(schema)) {
      cJSON_Delete(schema);
      return GADGET_ERR_ARG;
    }
    params = cJSON_PrintUnformatted(schema);
    cJSON_Delete(schema);
    if (params == NULL) return GADGET_ERR_NO_MEM;
    if (strlen(params) > GADGET_ACTION_PARAMS_MAX) {
      cJSON_free(params);
      return GADGET_ERR_LIMIT;
    }
  }
  action_t *a = &s_actions[s_count++];
  snprintf(a->name, sizeof a->name, "%s", name);
  snprintf(a->description, sizeof a->description, "%s", description);
  a->params = params;
  a->risk = risk;
  a->handler = handler;
  if (!hello_fits()) {
    s_count--;
    cJSON_free(a->params);
    memset(a, 0, sizeof *a);
    return GADGET_ERR_LIMIT;
  }
  return GADGET_OK;
}

static void send_result(const char *id, bool ok, const cJSON *data, const char *error) {
  gp_act_result_t r = {.id = id, .ok = ok, .data = data, .error = error};
  session_send("act.result", g_core_tx, gp_encode_act_result(g_core_tx, sizeof g_core_tx, &r));
}

bool actions_on_msg(const gp_msg_t *m) {
  if (m->op != GP_OP_ACT) return false;
  const gp_act_t *act = &m->m.act;
  const action_t *a = find(act->name);
  if (a == NULL) {
    send_result(act->id, false, NULL, "unknown action");
    return true;
  }
  cJSON *empty = act->args == NULL ? cJSON_CreateObject() : NULL;
  cJSON *data = cJSON_CreateObject();
  char error[128] = "";
  bool ok = a->handler(act->args ? act->args : empty, data, error, sizeof error);
  if (ok) {
    send_result(act->id, true, data, NULL);
  } else {
    char folded[128];
    core_text_copy(folded, sizeof folded, error[0] ? error : "failed");
    send_result(act->id, false, NULL, folded);
  }
  cJSON_Delete(data);
  cJSON_Delete(empty);
  return true;
}

gadget_status_t gadget_event_send(const char *name, const cJSON *data) {
  if (!valid_name(name)) return GADGET_ERR_ARG;
  if (data != NULL) {
    char *json = cJSON_PrintUnformatted(data);
    if (json == NULL) return GADGET_ERR_NO_MEM;
    size_t len = strlen(json);
    cJSON_free(json);
    if (len > EVENT_DATA_MAX) return GADGET_ERR_LIMIT;
  }
  if (!g_core.initialized || !session_ready()) return GADGET_ERR_BUSY;
  gp_event_msg_t ev = {.name = name, .data = data};
  return session_send("event", g_core_tx, gp_encode_event(g_core_tx, sizeof g_core_tx, &ev));
}

static bool chime_handler(const cJSON *args, cJSON *data, char *error, size_t error_cap) {
  (void)args;
  (void)data;
  (void)error;
  (void)error_cap;
  audio_play_chime(); /* a nicety: skipped without a speaker or over speech, still ok */
  return true;
}

void actions_init(void) {
  s_count = 0;
  gadget_status_t st = gadget_action_register("chime", "Play a short chime.", NULL, GADGET_RISK_SAFE, chime_handler);
  if (st != GADGET_OK) hal_log(GADGET_LOG_ERROR, TAG, "chime not registered: %d", (int)st);
}

void actions_deinit(void) {
  for (uint8_t i = 0; i < s_count; i++) cJSON_free(s_actions[i].params);
  memset(s_actions, 0, sizeof s_actions);
  s_count = 0;
}
```

Change `firmware/core/src/session.c` exactly as this diff shows (`-` lines go, `+` lines come; the rest is context):

```diff
--- a/firmware/core/src/session.c
+++ b/firmware/core/src/session.c
@@ -75,6 +75,7 @@ gadget_status_t session_send_binary(const uint8_t *frame, size_t len) {
 static void send_hello(void) {
   gp_hello_t h = {.id = g_core.id, .pubkey_b64 = g_core.pub_b64, .name = g_core.name, .fw = g_core.fw,
                   .board = g_core.board};
+  h.actions = actions_decls(&h.n_actions);
   gadget_battery_t batt;
   if (g_core.board->has_battery && hal_battery_read(&batt)) {
     h.battery_valid = true;
```

Change `firmware/core/src/core_internal.h` exactly as this diff shows (`-` lines go, `+` lines come; the rest is context):

```diff
--- a/firmware/core/src/core_internal.h
+++ b/firmware/core/src/core_internal.h
@@ -140,6 +140,12 @@ bool display_ask_input(const gadget_input_t *in);
 bool display_dismiss(void);                     /* CANCEL / swipe down: image, then card, then toast */
 bool display_tap(void);                         /* a tap hides the toast */
 
+/* ---- actions.c ------------------------------------------------------------------ */
+void actions_init(void);                        /* registers the built-in chime */
+void actions_deinit(void);
+bool actions_on_msg(const gp_msg_t *m);         /* act -> exactly one act.result */
+const gp_action_decl_t *actions_decls(uint8_t *count);   /* for hello */
+
 /* ---- console_cmd.c ------------------------------------------------------------- */
 void console_exec_line(const char *line);       /* NULL: an over-long line was dropped */
 void console_on_scan(const gadget_wifi_scan_ev_t *scan);
```

Change `firmware/core/src/core.c` exactly as this diff shows (`-` lines go, `+` lines come; the rest is context):

```diff
--- a/firmware/core/src/core.c
+++ b/firmware/core/src/core.c
@@ -235,6 +235,7 @@ gadget_status_t core_init(const core_config_t *cfg) {
   hal_log(GADGET_LOG_INFO, CORE_TAG, "%s on %s, fw %s", g_core.id, g_core.board->id, g_core.fw);
 
   g_core.initialized = true;
+  actions_init();
   model_commit();
   return GADGET_OK;
 }
@@ -287,6 +288,7 @@ const ui_model_t *core_ui_model(void) { return &g_core.model; }
 
 void core_deinit(void) {
   if (g_core.initialized) {
+    actions_deinit();
     display_deinit();
     audio_deinit();
     interaction_deinit();
@@ -325,6 +327,7 @@ void core_on_msg(const gp_msg_t *m) {
   if (interaction_on_msg(m)) return;
   if (audio_on_msg(m)) return;
   if (display_on_msg(m)) return;
+  if (actions_on_msg(m)) return;
   switch (m->op) {
     default:
       hal_log(GADGET_LOG_DEBUG, CORE_TAG, "ignored %s", gp_op_name(m->op));
```

Change `firmware/core/CMakeLists.txt` exactly as this diff shows (`-` lines go, `+` lines come; the rest is context):

```diff
--- a/firmware/core/CMakeLists.txt
+++ b/firmware/core/CMakeLists.txt
@@ -16,6 +16,7 @@ set(GADGET_CORE_SRCS
   src/console_cmd.c
   src/audio.c
   src/display.c
+  src/actions.c
 )
 
 if(ESP_PLATFORM)
```

- [ ] **Step 4: Run the tests**

Run: `cmake --build build/host -j10 && ctest --test-dir build/host --output-on-failure -L "unit|vectors"`
Expected: `100% tests passed, 0 tests failed out of 13`; `test_actions` prints `9 Tests 0 Failures 0 Ignored`.

- [ ] **Step 5: Commit**

```bash
git add firmware/core firmware/tests/test_actions.c firmware/tests/CMakeLists.txt
git commit -m "firmware: gadget actions with hello limits, the built-in chime and events" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 14: Firmware updates, key tables and probation

**Files:**
- Create: `firmware/core/src/ota.c`, `firmware/core/src/keys_release.c`, `firmware/core/src/keys_test.c`
- Modify: `firmware/core/CMakeLists.txt` (sources, the `GADGET_TEST_KEYS` definition, the ESP-IDF `CONFIG_GADGET_TEST_KEYS` block from contract §2.17), `firmware/core/src/core.c`, `firmware/core/src/core_internal.h`, `firmware/tests/CMakeLists.txt`
- Test: `firmware/tests/test_ota.c`

**Interfaces:**
- Consumes: `gadget_ota.h`; `hal_crypto_verify` and the multi-part SHA-256 (Task 3); `gp_firmware_text` and `gp_fw_chunk_decode` (Task 2); `interaction_turn_in_flight` (Task 9).
- Produces:
  - **Keys:** `gadget_key_find`, `core_ota_state`, and the tables `gadget_release_keys` (the empty sentinel, count 0; P2d adds r1) and `gadget_test_keys` (t1 when `GADGET_TEST_KEYS`, otherwise the sentinel).
  - **Module API:** `ota_init` (probation starts when `hal_ota_running_state()` is `PENDING_VERIFY`), `ota_on_msg` (`fw.offer`, `fw.commit`), `ota_on_binary`, `ota_event` (`OTA_WRITTEN`/`OTA_ERROR`), `ota_on_ready`, `ota_on_session_lost` and `ota_tick`.
- **Offer checks, in the contract §2.13 order:** `busy` (also while a recording is live or a turn is in flight: contract D22, Review Focus 2), `wrong_board`, `same_version`, `too_large`, `unknown_key`, `bad_sig`. The signature is checked over the text built with the gadget's own board id, and `sha256` must be 64 lowercase hex characters. An older signed version is accepted.
- **Flow:**
  - `fw.ready`, then chunks written in order (otherwise `sequence`). `fw.progress` goes out at each 16 KiB boundary and at the end, using the durable `written` count.
  - `fw.commit` checks the size and the SHA-256 (`checksum` for either, so also for a commit that arrives before every offered byte), then calls `hal_ota_finalize` and `hal_ota_set_boot(version)`, and restarts 1 s later.
  - Timeouts: 30 s without a chunk, or 30 s from the last byte to `fw.commit`, fail with `timeout`. A write error fails with `flash`. A dropped session aborts silently.
- **Probation:** 5 min, or `cfg.probation_ms` when non-zero. The timer starts at the first tick. The first `ready` calls `hal_ota_mark_valid()` and then sends `fw.installed {version}`. With `cfg.fail_probation`, that first `ready` is ignored. When the timer expires, the gadget calls `hal_ota_mark_invalid_and_reboot()`.
- **Update screen:** shows phases receiving, verifying and restarting, with a percentage. TALK is ignored during an update.

- [ ] **Step 1: Write the failing test**

Create `firmware/tests/test_ota.c`:

```c
/* firmware/tests/test_ota.c */
/* SPDX-License-Identifier: Apache-2.0 */
/* Firmware updates and probation (core/src/ota.c, spec §4.8). */
#include <stdlib.h>
#include <string.h>
#include "fake_hal.h"
#include "gadget_ota.h"
#include "gadget_proto.h"
#include "gadget_util.h"
#include "psa/crypto.h"
#include "unity.h"

void setUp(void) { fake_reset(); }
void tearDown(void) { core_deinit(); }

#define T1_PRIV "274b871e29523ce21208177b90a0a3bd6a169cb84cf867236adc68f3d90fc0c8"
#define T1_PUB "BIUTH1UeVJw2VXn0Ehdhd8apNOHmB6VvjV512SxIbCCXfUhvlLRTuOTLHtrsVAbIx06JhMoOMbC3ic73hZpxMwg="
#define IMG_SIZE 70000u

static uint8_t g_img[IMG_SIZE];

static void make_image(void) {
  for (uint32_t i = 0; i < IMG_SIZE; i++) g_img[i] = (uint8_t)(i * 31u + 7u);
}

/* A fw.offer for g_img (or for sha_of when given), signed with t1 over the text
 * built with sign_board. */
static void offer(const char *board, const char *sign_board, const char *version, uint32_t size, const char *key_id,
                  const uint8_t *sha_of) {
  uint8_t sha[32];
  hal_crypto_sha256(sha_of ? sha_of : g_img, IMG_SIZE, sha);
  char hex[65];
  gadget_hex_encode(hex, sha, 32);
  char text[256];
  int tn = gp_firmware_text(text, sizeof text, sign_board, version, size, hex);
  uint8_t priv[32], der[GADGET_SIG_DER_MAX];
  size_t n = 0, der_len = 0;
  gadget_hex_decode(T1_PRIV, priv, sizeof priv, &n);
  hal_crypto_sign(priv, (const uint8_t *)text, (size_t)tn, der, &der_len);
  char sig[GADGET_SIG_B64_MAX + 1];
  gadget_b64_encode(sig, sizeof sig, der, der_len);
  char json[640];
  snprintf(json, sizeof json,
           "{\"op\":\"fw.offer\",\"stream\":3,\"board\":\"%s\",\"version\":\"%s\",\"size\":%u,\"sha256\":\"%s\","
           "\"sig\":\"%s\",\"key_id\":\"%s\"}",
           board, version, (unsigned)size, hex, sig, key_id);
  fake_ws_in(json);
}

static void chunk(uint32_t offset, uint32_t len) {
  static uint8_t frame[2 + 4 + 4096];
  frame[0] = 0x04;
  frame[1] = 3;
  frame[2] = (uint8_t)offset;
  frame[3] = (uint8_t)(offset >> 8);
  frame[4] = (uint8_t)(offset >> 16);
  frame[5] = (uint8_t)(offset >> 24);
  memcpy(frame + 6, g_img + offset, len);
  fake_ws_bin_in(frame, 6 + len);
}

static void send_all(void) {
  for (uint32_t off = 0; off < IMG_SIZE; off += 4096) {
    chunk(off, IMG_SIZE - off < 4096 ? IMG_SIZE - off : 4096);
    fake_run(10);
  }
}

static const char *last_fail(void) {
  static char code[32];
  cJSON *f = fake_ws_last("fw.fail");
  if (f == NULL) return "";
  snprintf(code, sizeof code, "%s", cJSON_GetObjectItem(f, "code")->valuestring);
  cJSON_Delete(f);
  return code;
}

static void test_key_tables(void) {
  const gadget_release_key_t *k = gadget_key_find("t1");
  TEST_ASSERT_NOT_NULL(k);
  uint8_t pub[65];
  size_t n = 0;
  gadget_b64_decode(T1_PUB, pub, sizeof pub, &n);
  TEST_ASSERT_EQUAL_MEMORY(pub, k->pub, 65);
  TEST_ASSERT_EQUAL_size_t(0, gadget_release_keys_count); /* P2d adds r1 */
  TEST_ASSERT_NULL(gadget_key_find("r1"));
  TEST_ASSERT_NULL(gadget_key_find(NULL));
  TEST_ASSERT_NULL(gadget_key_find(""));
}

static void test_a_full_update(void) {
  make_image();
  fake_ready("amoled-175c");
  offer("amoled-175c", "amoled-175c", "1.1.0", IMG_SIZE, "t1", NULL);
  TEST_ASSERT_EQUAL_STRING("{\"op\":\"fw.ready\",\"stream\":3}", fake_ws_text(fake_ws_sent() - 1));
  TEST_ASSERT_EQUAL_UINT32(IMG_SIZE, fake_ota_size());
  fake_run(10);
  TEST_ASSERT_EQUAL_INT(UI_SCREEN_UPDATE, core_ui_model()->screen);
  TEST_ASSERT_EQUAL_STRING("1.1.0", core_ui_model()->update.version);
  fake_input(GADGET_IN_TALK_DOWN, 0, 0); /* no talking during an update */
  fake_mic_frames(20, 3000);
  fake_input(GADGET_IN_TALK_UP, 0, 0);
  TEST_ASSERT_EQUAL_size_t(0, fake_ws_count("voice.begin"));
  send_all();
  TEST_ASSERT_EQUAL_MEMORY(g_img, fake_ota_image(), IMG_SIZE);
  /* progress every 16 KiB and at the end */
  TEST_ASSERT_EQUAL_size_t(5, fake_ws_count("fw.progress"));
  cJSON *p = fake_ws_last("fw.progress");
  TEST_ASSERT_EQUAL_INT((int)IMG_SIZE, cJSON_GetObjectItem(p, "offset")->valueint);
  cJSON_Delete(p);
  TEST_ASSERT_EQUAL_INT(UI_UPDATE_VERIFYING, core_ui_model()->update.phase);
  TEST_ASSERT_EQUAL_UINT8(100, core_ui_model()->update.pct);
  fake_ws_in("{\"op\":\"fw.commit\",\"stream\":3}");
  TEST_ASSERT_TRUE(fake_ota_finalized());
  TEST_ASSERT_EQUAL_STRING("1.1.0", fake_ota_boot_version());
  fake_run(10);
  TEST_ASSERT_EQUAL_INT(UI_UPDATE_RESTARTING, core_ui_model()->update.phase);
  FAKE_EXPECT_RESTART(fake_run(2000));
  TEST_ASSERT_EQUAL_INT(1, fake_restarts());
}

static void expect_fail(const char *board, const char *sign_board, const char *version, uint32_t size,
                        const char *key_id, const uint8_t *sha_of, const char *code) {
  fake_ws_clear();
  offer(board, sign_board, version, size, key_id, sha_of);
  TEST_ASSERT_EQUAL_STRING_MESSAGE(code, last_fail(), code);
  TEST_ASSERT_EQUAL_size_t(0, fake_ws_count("fw.ready"));
  fake_run(10);
  TEST_ASSERT_EQUAL_INT(UI_SCREEN_IDLE, core_ui_model()->screen);
}

static void test_offers_are_checked_in_order(void) {
  make_image();
  fake_ready("amoled-175c");
  expect_fail("lcd-154", "lcd-154", "1.1.0", IMG_SIZE, "t1", NULL, "wrong_board");
  expect_fail("amoled-175c", "amoled-175c", "1.0.0", IMG_SIZE, "t1", NULL, "same_version");
  expect_fail("amoled-175c", "amoled-175c", "1.1.0", 6291457u, "t1", NULL, "too_large");
  expect_fail("amoled-175c", "amoled-175c", "1.1.0", IMG_SIZE, "r9", NULL, "unknown_key");
  /* the signature is checked over the gadget's own board id */
  expect_fail("amoled-175c", "lcd-154", "1.1.0", IMG_SIZE, "t1", NULL, "bad_sig");
  /* an older signed version is accepted: anti-rollback is off */
  fake_ws_clear();
  offer("amoled-175c", "amoled-175c", "0.9.0", IMG_SIZE, "t1", NULL);
  TEST_ASSERT_EQUAL_size_t(1, fake_ws_count("fw.ready"));
  /* a second offer while one runs */
  fake_ws_clear();
  offer("amoled-175c", "amoled-175c", "1.2.0", IMG_SIZE, "t1", NULL);
  TEST_ASSERT_EQUAL_STRING("busy", last_fail());
}

static void test_busy_while_the_person_is_talking(void) {
  make_image();
  fake_ready("amoled-175c");
  fake_console_in("say \"hi\"");
  cJSON *say = fake_ws_last("say");
  char done[160];
  snprintf(done, sizeof done, "{\"op\":\"done\",\"turn\":\"%s\",\"outcome\":\"ok\"}",
           cJSON_GetObjectItem(say, "turn")->valuestring);
  cJSON_Delete(say);
  offer("amoled-175c", "amoled-175c", "1.1.0", IMG_SIZE, "t1", NULL);
  TEST_ASSERT_EQUAL_STRING("busy", last_fail()); /* a turn is in flight */
  fake_ws_in(done);
  fake_input(GADGET_IN_TALK_DOWN, 0, 0);
  fake_mic_frames(20, 3000);
  fake_ws_clear();
  offer("amoled-175c", "amoled-175c", "1.1.0", IMG_SIZE, "t1", NULL);
  TEST_ASSERT_EQUAL_STRING("busy", last_fail()); /* recording */
  fake_input(GADGET_IN_CANCEL_DOWN, 0, 0);
  fake_ws_clear();
  offer("amoled-175c", "amoled-175c", "1.1.0", IMG_SIZE, "t1", NULL);
  TEST_ASSERT_EQUAL_size_t(1, fake_ws_count("fw.ready")); /* idle again: accepted */
}

static void test_tampered_offer_fields(void) {
  make_image();
  fake_ready("amoled-175c");
  fake_ws_in("{\"op\":\"fw.offer\",\"stream\":3,\"board\":\"amoled-175c\",\"version\":\"1.1.0\",\"size\":70000,"
             "\"sha256\":\"E3B0C44298FC1C149AFBF4C8996FB92427AE41E4649B934CA495991B7852B855\",\"sig\":\"MEUCIQ==\","
             "\"key_id\":\"t1\"}");
  TEST_ASSERT_EQUAL_STRING("bad_sig", last_fail()); /* uppercase hex is never signed text */
  fake_ws_in("{\"op\":\"fw.offer\",\"stream\":3,\"board\":\"amoled-175c\",\"version\":\"1.1.0\",\"size\":70000,"
             "\"sha256\":\"e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855\",\"sig\":\"not base64!\","
             "\"key_id\":\"t1\"}");
  TEST_ASSERT_EQUAL_STRING("bad_sig", last_fail());
}

static void test_out_of_order_chunk_fails_sequence(void) {
  make_image();
  fake_ready("amoled-175c");
  offer("amoled-175c", "amoled-175c", "1.1.0", IMG_SIZE, "t1", NULL);
  chunk(0, 4096);
  chunk(8192, 4096);
  TEST_ASSERT_EQUAL_STRING("sequence", last_fail());
  TEST_ASSERT_EQUAL_INT(1, fake_ota_aborts());
  fake_run(10);
  TEST_ASSERT_EQUAL_INT(UI_SCREEN_IDLE, core_ui_model()->screen);
}

static void test_wrong_bytes_fail_checksum(void) {
  make_image();
  static uint8_t other[IMG_SIZE];
  memcpy(other, g_img, IMG_SIZE);
  other[1234] ^= 0xff;
  fake_ready("amoled-175c");
  offer("amoled-175c", "amoled-175c", "1.1.0", IMG_SIZE, "t1", other); /* signed for different bytes */
  send_all();
  fake_ws_in("{\"op\":\"fw.commit\",\"stream\":3}");
  TEST_ASSERT_EQUAL_STRING("checksum", last_fail());
  TEST_ASSERT_FALSE(fake_ota_boot_version()[0] != '\0');
}

static void test_early_commit_fails_checksum(void) {
  make_image();
  fake_ready("amoled-175c");
  offer("amoled-175c", "amoled-175c", "1.1.0", IMG_SIZE, "t1", NULL);
  chunk(0, 4096);
  fake_ws_in("{\"op\":\"fw.commit\",\"stream\":3}"); /* 4096 of 70000 bytes */
  TEST_ASSERT_EQUAL_STRING("checksum", last_fail());
  TEST_ASSERT_EQUAL_INT(1, fake_ota_aborts());
}

static void test_timeouts(void) {
  make_image();
  fake_ready("amoled-175c");
  offer("amoled-175c", "amoled-175c", "1.1.0", IMG_SIZE, "t1", NULL);
  chunk(0, 4096);
  for (int i = 0; i < 2; i++) {
    fake_run(14000);
    fake_ws_ping_in();
  }
  TEST_ASSERT_EQUAL_STRING("", last_fail());
  fake_run(2100);
  TEST_ASSERT_EQUAL_STRING("timeout", last_fail()); /* no chunk for 30 s */
  /* all bytes, then no commit within 30 s */
  fake_ws_clear();
  offer("amoled-175c", "amoled-175c", "1.1.0", IMG_SIZE, "t1", NULL);
  send_all();
  for (int i = 0; i < 3; i++) {
    fake_run(10000);
    fake_ws_ping_in();
  }
  TEST_ASSERT_EQUAL_STRING("timeout", last_fail());
}

static void test_flash_errors_and_disconnects(void) {
  make_image();
  fake_ready("amoled-175c");
  fake_ota_fail_writes(true);
  offer("amoled-175c", "amoled-175c", "1.1.0", IMG_SIZE, "t1", NULL);
  chunk(0, 4096);
  fake_run(10);
  TEST_ASSERT_EQUAL_STRING("flash", last_fail());
  fake_ota_fail_writes(false);
  offer("amoled-175c", "amoled-175c", "1.1.0", IMG_SIZE, "t1", NULL);
  chunk(0, 4096);
  int aborts = fake_ota_aborts();
  fake_ws_drop(1006);
  fake_run(10);
  TEST_ASSERT_EQUAL_INT(aborts + 1, fake_ota_aborts());
  fake_run(2000);
  fake_handshake();
  TEST_ASSERT_EQUAL_INT(UI_SCREEN_IDLE, core_ui_model()->screen);
}

static void test_probation_confirms_on_the_first_ready(void) {
  fake_ota_set_running(HAL_OTA_IMG_PENDING_VERIFY);
  fake_store_paired();
  fake_boot("amoled-175c");
  fake_run(10);
  fake_ws_accept();
  fake_ws_in("{\"op\":\"challenge\",\"nonce\":\"" FAKE_NONCE "\",\"host_id\":\"" FAKE_HOST_ID "\",\"host_name\":\"Mac\"}");
  fake_ws_in("{\"op\":\"ready\",\"session\":\"s_1\",\"bot\":{\"id\":\"b\",\"name\":\"B\"}}");
  TEST_ASSERT_TRUE(fake_ota_marked_valid());
  TEST_ASSERT_EQUAL_STRING("{\"op\":\"fw.installed\",\"version\":\"1.0.0\"}", fake_ws_text(fake_ws_sent() - 1));
  for (int i = 0; i < 40; i++) {
    fake_run(10000);
    fake_ws_ping_in();
  }
  TEST_ASSERT_FALSE(fake_ota_invalidated());
}

static void test_probation_rolls_back_without_a_ready(void) {
  fake_ota_set_running(HAL_OTA_IMG_PENDING_VERIFY);
  fake_store_paired();
  fake_boot("amoled-175c");
  fake_run(299000);
  TEST_ASSERT_FALSE(fake_ota_invalidated());
  FAKE_EXPECT_RESTART(fake_run(2000));
  TEST_ASSERT_TRUE(fake_ota_invalidated());
}

static void test_fail_probation_ignores_the_first_ready(void) {
  fake_ota_set_running(HAL_OTA_IMG_PENDING_VERIFY);
  fake_store_paired();
  core_config_t cfg = {.board = gadget_board_by_id("amoled-175c"), .fw_version = "1.1.0", .prng_seed = 1,
                       .fail_probation = true, .probation_ms = 3000};
  fake_boot_cfg(&cfg);
  fake_run(10);
  fake_handshake();
  TEST_ASSERT_FALSE(fake_ota_marked_valid());
  TEST_ASSERT_EQUAL_size_t(0, fake_ws_count("fw.installed"));
  FAKE_EXPECT_RESTART(fake_run(4000));
  TEST_ASSERT_TRUE(fake_ota_invalidated());
}

int main(void) {
  if (psa_crypto_init() != PSA_SUCCESS) return 3;
  UNITY_BEGIN();
  RUN_TEST(test_key_tables);
  RUN_TEST(test_a_full_update);
  RUN_TEST(test_offers_are_checked_in_order);
  RUN_TEST(test_busy_while_the_person_is_talking);
  RUN_TEST(test_tampered_offer_fields);
  RUN_TEST(test_out_of_order_chunk_fails_sequence);
  RUN_TEST(test_wrong_bytes_fail_checksum);
  RUN_TEST(test_early_commit_fails_checksum);
  RUN_TEST(test_timeouts);
  RUN_TEST(test_flash_errors_and_disconnects);
  RUN_TEST(test_probation_confirms_on_the_first_ready);
  RUN_TEST(test_probation_rolls_back_without_a_ready);
  RUN_TEST(test_fail_probation_ignores_the_first_ready);
  return UNITY_END();
}
```

Change `firmware/tests/CMakeLists.txt` exactly as this diff shows (`-` lines go, `+` lines come; the rest is context):

```diff
--- a/firmware/tests/CMakeLists.txt
+++ b/firmware/tests/CMakeLists.txt
@@ -32,6 +32,7 @@ gadget_unit_test(console)
 gadget_unit_test(audio)
 gadget_unit_test(display)
 gadget_unit_test(actions)
+gadget_unit_test(ota)
 
 # protocol/vectors in C (label vectors).
 add_executable(test_vectors test_vectors.c)
```

- [ ] **Step 2: Run it to see it fail**

Run: `cmake --build build/host -j10`
Expected: the link of `test_ota` fails on `gadget_key_find` and `gadget_release_keys_count`.

- [ ] **Step 3: Write the implementation**

The test key bytes are `keys/test-t1.pub.b64` decoded (contract §1.7: `BIUTH1Ue…MwgQ=`). The release table stays empty; plan P2d fills it in.

Create `firmware/core/src/keys_test.c`:

```c
/* firmware/core/src/keys_test.c */
/* SPDX-License-Identifier: Apache-2.0 */
/* The test signing key t1 (contract §1.7, keys/test-t1.pub.b64), used by
 * the fake host and the simulator's OTA tests. Always compiled; it holds t1
 * only when the build defines GADGET_TEST_KEYS (desktop default; every
 * board's sdkconfig.defaults leaves CONFIG_GADGET_TEST_KEYS unset). */
#include "gadget_ota.h"

#if defined(GADGET_TEST_KEYS)
const gadget_release_key_t gadget_test_keys[] = {
    {"t1",
     {0x04, 0x85, 0x13, 0x1f, 0x55, 0x1e, 0x54, 0x9c, 0x36, 0x55, 0x79, 0xf4, 0x12,
      0x17, 0x61, 0x77, 0xc6, 0xa9, 0x34, 0xe1, 0xe6, 0x07, 0xa5, 0x6f, 0x8d, 0x5e,
      0x75, 0xd9, 0x2c, 0x48, 0x6c, 0x20, 0x97, 0x7d, 0x48, 0x6f, 0x94, 0xb4, 0x53,
      0xb8, 0xe4, 0xcb, 0x1e, 0xda, 0xec, 0x54, 0x06, 0xc8, 0xc7, 0x4e, 0x89, 0x84,
      0xca, 0x0e, 0x31, 0xb0, 0xb7, 0x89, 0xce, 0xf7, 0x85, 0x9a, 0x71, 0x33, 0x08}},
};
const size_t gadget_test_keys_count = 1;
#else
const gadget_release_key_t gadget_test_keys[] = {{NULL, {0}}};
const size_t gadget_test_keys_count = 0;
#endif
```

Create `firmware/core/src/keys_release.c`:

```c
/* firmware/core/src/keys_release.c */
/* SPDX-License-Identifier: Apache-2.0 */
/* Release public keys (ids /^r[0-9]+$/) compiled into every build. Empty
 * until Omkar commits keys/release-r1.pub.b64; plan P2d then adds
 *   {"r1", {0x04, ...65 bytes...}}
 * and sets the count to 1. Release CI fails while the count is 0. */
#include "gadget_ota.h"

const gadget_release_key_t gadget_release_keys[] = {{NULL, {0}}};
const size_t gadget_release_keys_count = 0;
```

Create `firmware/core/src/ota.c`:

```c
/* firmware/core/src/ota.c */
/* SPDX-License-Identifier: Apache-2.0 */
/* Firmware updates (spec §4.8, contract §2.13): the offer checks, chunk
 * flow with progress, commit with size + SHA-256, and the firmware's own
 * probation timer. The same code runs on the ESP32 and in the simulator. */
#include <stdio.h>
#include <string.h>
#include "core_internal.h"
#include "gadget_ota.h"

#define TAG "ota"
#define RESTART_DELAY_MS 1000u

static struct {
  gadget_ota_state_t st;
  uint8_t stream;
  uint32_t size, received, written, reported;
  char version[GADGET_VERSION_MAX + 1];
  uint8_t sha[GADGET_SHA256_LEN];
  hal_sha256_t hash;
  uint64_t deadline;
  bool commit_pending;
  uint64_t restart_at;
  bool probation;          /* this boot is a new image on probation */
  bool armed;              /* the probation deadline is set (first tick) */
  bool validated;
  bool ignored_first_ready; /* --fail-probation */
  uint64_t probation_deadline;
} O;

const gadget_release_key_t *gadget_key_find(const char *key_id) {
  if (key_id == NULL) return NULL;
  for (size_t i = 0; i < gadget_release_keys_count; i++) {
    if (gadget_release_keys[i].id != NULL && strcmp(gadget_release_keys[i].id, key_id) == 0) {
      return &gadget_release_keys[i];
    }
  }
  for (size_t i = 0; i < gadget_test_keys_count; i++) {
    if (gadget_test_keys[i].id != NULL && strcmp(gadget_test_keys[i].id, key_id) == 0) return &gadget_test_keys[i];
  }
  return NULL;
}

gadget_ota_state_t core_ota_state(void) { return O.st; }

static void send_fail(uint8_t stream, const char *code) {
  hal_log(GADGET_LOG_WARN, TAG, "update failed: %s", code);
  gp_fw_fail_t f = {.stream = stream, .code = code};
  session_send("fw.fail", g_core_tx, gp_encode_fw_fail(g_core_tx, sizeof g_core_tx, &f));
}

static void reset(void) {
  hal_crypto_sha256_abort(&O.hash);
  O.st = GADGET_OTA_IDLE;
  O.commit_pending = false;
}

static void fail(const char *code) {
  send_fail(O.stream, code);
  hal_ota_abort();
  reset();
}

/* The offer checks, in the contract's order. NULL when the offer is fine. */
static const char *check_offer(const gp_fw_offer_t *o) {
  if (strcmp(o->board, g_core.board->id) != 0) return GADGET_FW_WRONG_BOARD;
  if (strcmp(o->version, g_core.fw) == 0) return GADGET_FW_SAME_VERSION;
  if (o->size > g_core.board->ota_max) return GADGET_FW_TOO_LARGE;
  const gadget_release_key_t *key = gadget_key_find(o->key_id);
  if (key == NULL) return GADGET_FW_UNKNOWN_KEY;
  size_t n = 0;
  uint8_t der[GADGET_SIG_DER_MAX + 8];
  size_t der_len = 0;
  char text[200];
  int tn = gp_firmware_text(text, sizeof text, g_core.board->id, o->version, o->size, o->sha256);
  if (strlen(o->sha256) != 64 || gadget_hex_decode(o->sha256, O.sha, sizeof O.sha, &n) != GADGET_OK || n != 32 ||
      gadget_b64_decode(o->sig, der, sizeof der, &der_len) != GADGET_OK || tn < 0 ||
      hal_crypto_verify(key->pub, (const uint8_t *)text, (size_t)tn, der, der_len) != GADGET_OK) {
    return GADGET_FW_BAD_SIG;
  }
  return NULL;
}

static void on_offer(const gp_fw_offer_t *o) {
  /* never cut off a person who is talking or waiting for an answer */
  if (O.st != GADGET_OTA_IDLE || g_core.f.recording || interaction_turn_in_flight()) {
    send_fail(o->stream, GADGET_FW_BUSY);
    return;
  }
  O.stream = o->stream;
  const char *code = check_offer(o);
  if (code == NULL && strlen(o->version) > GADGET_VERSION_MAX) code = GADGET_FW_FLASH;
  if (code == NULL && hal_ota_begin(o->size) != GADGET_OK) code = GADGET_FW_FLASH;
  if (code != NULL) {
    send_fail(o->stream, code);
    return;
  }
  if (hal_crypto_sha256_begin(&O.hash) != GADGET_OK) {
    fail(GADGET_FW_FLASH);
    return;
  }
  snprintf(O.version, sizeof O.version, "%s", o->version);
  O.size = o->size;
  O.received = O.written = O.reported = 0;
  O.commit_pending = false;
  O.deadline = g_core.now + GADGET_FW_CHUNK_TIMEOUT_MS;
  O.st = GADGET_OTA_RECEIVING;
  hal_log(GADGET_LOG_INFO, TAG, "receiving %s (%u bytes)", O.version, (unsigned)O.size);
  gp_fw_ready_t r = {.stream = O.stream};
  session_send("fw.ready", g_core_tx, gp_encode_fw_ready(g_core_tx, sizeof g_core_tx, &r));
}

static void do_commit(void) {
  O.st = GADGET_OTA_FINALIZING;
  uint8_t got[GADGET_SHA256_LEN];
  if (O.received != O.size) {
    fail(GADGET_FW_CHECKSUM); /* contract §2.13: size and SHA-256 are the commit's checks */
    return;
  }
  if (hal_crypto_sha256_finish(&O.hash, got) != GADGET_OK || memcmp(got, O.sha, sizeof got) != 0) {
    fail(GADGET_FW_CHECKSUM);
    return;
  }
  if (hal_ota_finalize() != GADGET_OK || hal_ota_set_boot(O.version) != GADGET_OK) {
    fail(GADGET_FW_FLASH);
    return;
  }
  hal_log(GADGET_LOG_INFO, TAG, "%s installed; restarting", O.version);
  O.st = GADGET_OTA_RESTARTING;
  O.restart_at = g_core.now + RESTART_DELAY_MS;
}

bool ota_on_msg(const gp_msg_t *m) {
  switch (m->op) {
    case GP_OP_FW_OFFER:
      on_offer(&m->m.fw_offer);
      return true;
    case GP_OP_FW_COMMIT:
      if (O.st == GADGET_OTA_IDLE || m->m.fw_commit.stream != O.stream) return true;
      if (O.st == GADGET_OTA_WAIT_COMMIT) {
        do_commit();
      } else if (O.st == GADGET_OTA_RECEIVING && O.received == O.size) {
        O.commit_pending = true; /* the last writes are still on their way to flash */
      } else if (O.st == GADGET_OTA_RECEIVING) {
        fail(GADGET_FW_CHECKSUM); /* a commit before all the offered bytes arrived */
      }
      return true;
    default:
      return false;
  }
}

void ota_on_binary(uint8_t stream, const uint8_t *payload, size_t len) {
  if (O.st != GADGET_OTA_RECEIVING || stream != O.stream) return;
  uint32_t offset;
  const uint8_t *data;
  size_t n;
  if (gp_fw_chunk_decode(payload, len, &offset, &data, &n) != GADGET_OK || offset != O.received ||
      (uint64_t)O.received + n > O.size) {
    fail(GADGET_FW_SEQUENCE);
    return;
  }
  if (hal_ota_write(offset, data, n) != GADGET_OK) {
    fail(GADGET_FW_FLASH);
    return;
  }
  hal_crypto_sha256_update(&O.hash, data, n);
  O.received += (uint32_t)n;
  O.deadline = g_core.now + (O.received == O.size ? GADGET_FW_COMMIT_TIMEOUT_MS : GADGET_FW_CHUNK_TIMEOUT_MS);
}

void ota_event(const gadget_event_t *ev) {
  if (O.st != GADGET_OTA_RECEIVING) return;
  if (ev->type == GADGET_EV_OTA_ERROR) {
    fail(GADGET_FW_FLASH);
    return;
  }
  uint32_t w = ev->u.ota_written.written;
  if (w <= O.written) return;
  O.written = w;
  bool boundary = w / GADGET_FW_PROGRESS_EVERY > O.reported / GADGET_FW_PROGRESS_EVERY;
  if (boundary || w == O.size) {
    gp_fw_progress_t p = {.stream = O.stream, .offset = w};
    session_send("fw.progress", g_core_tx, gp_encode_fw_progress(g_core_tx, sizeof g_core_tx, &p));
    O.reported = w;
  }
  if (w == O.size) {
    O.st = GADGET_OTA_WAIT_COMMIT;
    if (O.commit_pending) do_commit();
  }
}

void ota_on_ready(void) {
  if (!O.probation || O.validated) return;
  if (g_core.cfg.fail_probation && !O.ignored_first_ready) {
    O.ignored_first_ready = true;
    hal_log(GADGET_LOG_WARN, TAG, "test: ignoring the first ready, so probation runs out");
    return;
  }
  hal_ota_mark_valid();
  O.validated = true;
  hal_log(GADGET_LOG_INFO, TAG, "%s is good", g_core.fw);
  gp_fw_installed_t fi = {.version = g_core.fw};
  session_send("fw.installed", g_core_tx, gp_encode_fw_installed(g_core_tx, sizeof g_core_tx, &fi));
}

void ota_on_session_lost(void) {
  if (O.st == GADGET_OTA_RECEIVING || O.st == GADGET_OTA_WAIT_COMMIT || O.st == GADGET_OTA_FINALIZING) {
    hal_ota_abort();
    reset();
  }
}

void ota_tick(void) {
  uint64_t now = g_core.now;
  if ((O.st == GADGET_OTA_RECEIVING || O.st == GADGET_OTA_WAIT_COMMIT) && now >= O.deadline) fail(GADGET_FW_TIMEOUT);
  if (O.st == GADGET_OTA_RESTARTING && now >= O.restart_at) hal_restart();
  if (O.probation && !O.validated) {
    if (!O.armed) {
      O.armed = true;
      uint32_t ms = g_core.cfg.probation_ms ? g_core.cfg.probation_ms : GADGET_PROBATION_MS;
      O.probation_deadline = now + ms;
    } else if (now >= O.probation_deadline) {
      hal_log(GADGET_LOG_ERROR, TAG, "probation ran out before a ready; rolling back");
      hal_ota_mark_invalid_and_reboot();
    }
  }
  ui_model_t *m = &g_core.model;
  g_core.f.ota_active = O.st != GADGET_OTA_IDLE;
  if (O.st == GADGET_OTA_IDLE) {
    memset(&m->update, 0, sizeof m->update);
    return;
  }
  m->update.phase = O.st == GADGET_OTA_RESTARTING                                        ? UI_UPDATE_RESTARTING
                    : (O.st == GADGET_OTA_FINALIZING || O.st == GADGET_OTA_WAIT_COMMIT) ? UI_UPDATE_VERIFYING
                                                                                         : UI_UPDATE_RECEIVING;
  m->update.pct = O.size ? (uint8_t)((uint64_t)O.written * 100u / O.size) : 0;
  snprintf(m->update.version, sizeof m->update.version, "%s", O.version);
}

void ota_init(void) {
  memset(&O, 0, sizeof O);
  O.probation = hal_ota_running_state() == HAL_OTA_IMG_PENDING_VERIFY;
  if (O.probation) hal_log(GADGET_LOG_WARN, TAG, "new image on probation");
}

void ota_deinit(void) {
  hal_crypto_sha256_abort(&O.hash);
  memset(&O, 0, sizeof O);
}
```

Change `firmware/core/src/core_internal.h` exactly as this diff shows (`-` lines go, `+` lines come; the rest is context):

```diff
--- a/firmware/core/src/core_internal.h
+++ b/firmware/core/src/core_internal.h
@@ -147,6 +147,16 @@ void actions_deinit(void);
 bool actions_on_msg(const gp_msg_t *m);         /* act -> exactly one act.result */
 const gp_action_decl_t *actions_decls(uint8_t *count);   /* for hello */
 
+/* ---- ota.c ---------------------------------------------------------------------- */
+void ota_init(void);                            /* starts probation on a PENDING_VERIFY image */
+void ota_deinit(void);
+bool ota_on_msg(const gp_msg_t *m);             /* fw.offer, fw.commit */
+void ota_on_binary(uint8_t stream, const uint8_t *payload, size_t len);   /* firmware chunks */
+void ota_event(const gadget_event_t *ev);       /* GADGET_EV_OTA_WRITTEN / _ERROR */
+void ota_on_ready(void);                        /* probation: mark valid, send fw.installed */
+void ota_on_session_lost(void);
+void ota_tick(void);
+
 /* ---- console_cmd.c ------------------------------------------------------------- */
 void console_exec_line(const char *line);       /* NULL: an over-long line was dropped */
 void console_on_scan(const gadget_wifi_scan_ev_t *scan);
```

Change `firmware/core/src/core.c` exactly as this diff shows (`-` lines go, `+` lines come; the rest is context):

```diff
--- a/firmware/core/src/core.c
+++ b/firmware/core/src/core.c
@@ -224,6 +224,7 @@ gadget_status_t core_init(const core_config_t *cfg) {
   interaction_init();
   audio_init();
   display_init();
+  ota_init();
   if (g_core.wifi_ssid[0] != '\0') hal_wifi_connect(g_core.wifi_ssid, g_core.wifi_pass);
 
   cJSON *boot = cJSON_CreateObject();
@@ -255,6 +256,10 @@ void core_event(const gadget_event_t *ev) {
     case GADGET_EV_WIFI_SCAN:
       console_on_scan(&ev->u.scan);
       break;
+    case GADGET_EV_OTA_WRITTEN:
+    case GADGET_EV_OTA_ERROR:
+      ota_event(ev);
+      break;
     case GADGET_EV_WIFI_STATE:
     case GADGET_EV_WS_OPEN:
     case GADGET_EV_WS_TEXT:
@@ -280,6 +285,7 @@ void core_tick(uint64_t now_ms) {
   interaction_tick();
   audio_tick();
   display_tick();
+  ota_tick();
   screens_update();
   model_commit();
 }
@@ -288,6 +294,7 @@ const ui_model_t *core_ui_model(void) { return &g_core.model; }
 
 void core_deinit(void) {
   if (g_core.initialized) {
+    ota_deinit();
     actions_deinit();
     display_deinit();
     audio_deinit();
@@ -314,13 +321,17 @@ void core_tap(core_tap_dir_t dir, const char *op, const char *json, size_t len)
 
 /* ---- session hooks ------------------------------------------------------------------ */
 
-void core_on_ready(void) { hal_log(GADGET_LOG_INFO, CORE_TAG, "talking to %s", g_core.bot_name); }
+void core_on_ready(void) {
+  hal_log(GADGET_LOG_INFO, CORE_TAG, "talking to %s", g_core.bot_name);
+  ota_on_ready();
+}
 
 void core_on_session_lost(void) {
   hal_log(GADGET_LOG_INFO, CORE_TAG, "session lost");
   audio_stop_local();
   interaction_on_session_lost();
   display_on_session_lost();
+  ota_on_session_lost();
 }
 
 void core_on_msg(const gp_msg_t *m) {
@@ -328,6 +339,7 @@ void core_on_msg(const gp_msg_t *m) {
   if (audio_on_msg(m)) return;
   if (display_on_msg(m)) return;
   if (actions_on_msg(m)) return;
+  if (ota_on_msg(m)) return;
   switch (m->op) {
     default:
       hal_log(GADGET_LOG_DEBUG, CORE_TAG, "ignored %s", gp_op_name(m->op));
@@ -343,6 +355,9 @@ void core_on_binary(gp_bin_kind_t kind, uint8_t stream, const uint8_t *payload,
     case GP_BIN_IMAGE:
       display_on_binary(stream, payload, len);
       break;
+    case GP_BIN_FIRMWARE:
+      ota_on_binary(stream, payload, len);
+      break;
     default:
       hal_log(GADGET_LOG_DEBUG, CORE_TAG, "ignored binary kind %d stream %u (%u bytes)", (int)kind, (unsigned)stream,
               (unsigned)len);
```

Change `firmware/core/CMakeLists.txt` exactly as this diff shows (`-` lines go, `+` lines come; the rest is context):

```diff
--- a/firmware/core/CMakeLists.txt
+++ b/firmware/core/CMakeLists.txt
@@ -17,12 +17,18 @@ set(GADGET_CORE_SRCS
   src/audio.c
   src/display.c
   src/actions.c
+  src/ota.c
+  src/keys_release.c
+  src/keys_test.c
 )
 
 if(ESP_PLATFORM)
   idf_component_register(SRCS ${GADGET_CORE_SRCS}
                          INCLUDE_DIRS include
                          REQUIRES espressif__cjson mbedtls)
+  if(CONFIG_GADGET_TEST_KEYS)
+    target_compile_definitions(${COMPONENT_LIB} PRIVATE GADGET_TEST_KEYS=1)
+  endif()
   return()
 endif()
 
@@ -30,3 +36,6 @@ add_library(gadget_core STATIC ${GADGET_CORE_SRCS})
 target_include_directories(gadget_core PUBLIC include)
 target_link_libraries(gadget_core PUBLIC cjson gadget_mbedcrypto)
 gadget_warnings(gadget_core)
+if(GADGET_TEST_KEYS)
+  target_compile_definitions(gadget_core PRIVATE GADGET_TEST_KEYS=1)
+endif()
```

- [ ] **Step 4: Run the tests, then the sanitizer and gnu23 checks on all of core**

Run: `cmake --build build/host -j10 && ctest --test-dir build/host --output-on-failure -L "unit|vectors"`
Expected: `100% tests passed, 0 tests failed out of 14`; `test_ota` prints `13 Tests 0 Failures 0 Ignored`.

Run: `cmake -S firmware -B build/asan -DCMAKE_BUILD_TYPE=Debug -DGADGET_WITH_LVGL=OFF -DGADGET_SANITIZE=ON && cmake --build build/asan -j10 && ctest --test-dir build/asan --output-on-failure -L "unit|vectors"`
Expected: `100% tests passed, 0 tests failed out of 14`, with no `AddressSanitizer` or `runtime error` lines.

Run the gnu23 check (ESP-IDF 6's default C standard) twice: against mbedTLS 3.6.7's headers, and against the TF-PSA-Crypto headers of mbedTLS 4.x, which ESP-IDF 6 uses (6.0.3 bundles 4.1.1). The second set of `-I` flags comes from `crypto_psa.c`'s entry in a configure-only `build/host-mbedtls4` (the first configure downloads mbedTLS 4.2.0). The flags go through response files, so the loop works the same in bash and zsh.

```bash
cmake -S firmware -B build/host-mbedtls4 -DCMAKE_BUILD_TYPE=Debug -DGADGET_WITH_LVGL=OFF \
  -DGADGET_MBEDTLS_VERSION=4.2.0 -DCMAKE_EXPORT_COMPILE_COMMANDS=ON > /dev/null
printf '%s\n' -Ifirmware/core/include -Ibuild/host/_deps/cjson-src -Ibuild/host/_deps/mbedtls-src/include \
  > build/host/gnu23.rsp
node -e '
const cc = require(require("node:path").resolve(process.argv[1]));
const e = cc.find((x) => x.file.endsWith("core/src/crypto_psa.c"));
console.log((e.command ?? e.arguments.join(" ")).split(/\s+/).filter((a) => a.startsWith("-I")).join("\n"));
' build/host-mbedtls4/compile_commands.json > build/host-mbedtls4/gnu23.rsp
for rsp in build/host/gnu23.rsp build/host-mbedtls4/gnu23.rsp; do
  for f in firmware/core/src/*.c; do
    cc -std=gnu23 -Wall -Wextra -Wpedantic -Werror -fsyntax-only -DGADGET_TEST_KEYS=1 "@$rsp" "$f" || echo "FAIL $f"
  done; echo "gnu23 done ($rsp)"
done
```

Expected: `gnu23 done (build/host/gnu23.rsp)`, then `gnu23 done (build/host-mbedtls4/gnu23.rsp)`, and no `FAIL` line.

- [ ] **Step 5: Commit**

```bash
git add firmware/core firmware/tests/test_ota.c firmware/tests/CMakeLists.txt
git commit -m "firmware: OTA state machine, key tables and probation" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---
### Task 15a: Headless simulator sources

**Files:**
- Create (contract seams, verbatim from contract §2.16): `firmware/ports/sim/sim_hal.h`, `firmware/ports/sim/sim_display.h`
- Create: `firmware/ports/sim/CMakeLists.txt`, `sim_internal.h`, `main.c`, `sim_args.c`, `sim_events.c`, `sim_storage.c`, `sim_system.c`, `sim_wifi.c`, `sim_battery.c`, `sim_console.c`, `sim_audio.c`, `sim_audio_file.c`, `sim_mdns.c`, `sim_ota.c`, `sim_net_script.c`, `sim_ws.c` (the scripted network only; Task 16 adds the real client), `sim_script.c`, `sim_display_null.c`, `ui_stub.c`
- Modify: `firmware/CMakeLists.txt` (`add_subdirectory(ports/sim)`)

**Interfaces:**
- Consumes: the whole core API (Tasks 1–14); `gadget_linebuf_*` (Task 6).
- Produces:
  - **The `gadget-sim` command line** of contract §2.16: every flag, with exit codes 0, 1, 2 and 3. `@omb` lines go to stdout and logs to stderr; `--trace` prints `>> {json}` and `<< {json}`. Without `--headless` and without SDL (P2b), the simulator runs on the real clock with the null display and the stdin console (contract D26).
  - **The script grammar** of contract §2.16, with the three additions of contract D23, D24 and D27: `expect <op>` matches the first frame with that op since the previous `expect` matched, including frames that crossed before the line was reached; `net_open [timeout_ms]` waits (5 s by default) for the gadget to ask for a connection; and in `net_text`, every `${turn}` becomes the `turn` of the last `voice.begin` or `say` the gadget sent, so a script can answer a turn although its prefix is random at every boot (contract §2.12). `net_text` fails with `no turn yet` before the gadget has sent either.
  - **The seams P2b builds on:** `sim_post_event` (thread-safe, deep copies), `sim_audio_use`, `sim_audio_file_backend` (WAV mic replayed from its start on each TALK; speaker drained in clock time into a WAV, appended to after a restart), the null `sim_display_*`, and `ui_stub.c`'s `ui_*`. P2b adds `sim_display_lvgl.c`, `sim_sdl.c` and `sim_audio_sdl.c` by editing only this task's `CMakeLists.txt`.
  - **The state folder** (contract §4.6): `storage.json`, `otadata.json`, `slot0.bin` and `slot1.bin`, all written atomically: each is written to a temp name and renamed (an image goes to `slot<n>.bin.tmp` and is renamed by `hal_ota_finalize`; `hal_ota_abort` deletes the temp file). `otadata.json` gains the field `"booted": true` (contract D25): a pending image that boots a second time without confirmation rolls back (Review Focus 3).
  - **Restarts:** `execv` of the simulator's own path (`_NSGetExecutablePath` on macOS, `/proc/self/exe` on Linux) with the original arguments minus `--pair` and `--boot`, plus `--boot <n+1>`. `sim_display_deinit()` runs first.
  - **The clock:** `--host script` runs unpaced, so a 20 s wait takes milliseconds. A real host address (Task 16) paces each 10 ms tick to real time.
- Task 15b adds the scripts that exercise all of this.

- [ ] **Step 1: See that there is no simulator yet**

Run: `cmake -S firmware -B build/host && cmake --build build/host --target gadget-sim`
Expected: make stops with `No rule to make target` and `gadget-sim` (exit 2).

- [ ] **Step 2: Add the contract seams and the private header**

Create `firmware/ports/sim/sim_hal.h`:

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

Create `firmware/ports/sim/sim_display.h`:

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

Create `firmware/ports/sim/sim_internal.h`:

```c
/* firmware/ports/sim/sim_internal.h */
/* SPDX-License-Identifier: Apache-2.0 */
/* Private to the simulator: options and the calls between its files.
 * The seams other plans use are sim_hal.h and sim_display.h. */
#ifndef SIM_INTERNAL_H
#define SIM_INTERNAL_H

#include <stdio.h>
#include "gadget_board.h"
#include "gadget_core.h"

typedef struct {
  const gadget_board_t *board;
  const char *name;            /* --name, NULL = "default" */
  char state_dir[1024];        /* --state-dir or ~/.openmausbot-gadget/sim/<name> */
  const char *host;            /* --host; "script" = the scripted network */
  const char *pair;            /* --pair (boot 0 only) */
  unsigned boot;               /* --boot */
  bool headless;
  const char *script;
  const char *mic_file, *spk_file;
  uint32_t seed;
  bool seed_set;
  uint8_t battery_pct;
  bool battery_charging;
  float zoom;
  const char *snapshot_dir;
  bool fail_probation;
  uint32_t probation_ms;
  bool trace;
  int argc;                    /* the original command line, for restarts */
  char **argv;
} sim_args_t;

extern sim_args_t g_sim;

/* sim_args.c: 0 = run, 1 = printed --help/--version (exit 0), 2 = usage error */
int sim_args_parse(int argc, char **argv, sim_args_t *out);

/* sim_events.c: deliver everything sim_post_event() queued, in order. */
void sim_events_deliver(void);

/* sim_storage.c: load <dir>/storage.json (creating <dir>); 0 on success. */
int sim_storage_open(const char *dir);
/* Write text to path through a temp file and rename(); 0 on success. */
int sim_write_atomic(const char *path, const char *text);

/* sim_ota.c: load <dir>/otadata.json, finish a pending rollback, and write the
 * running image's version into fw (GADGET_SIM_VERSION before any OTA). */
int sim_ota_open(const char *dir, char *fw, size_t cap);

/* sim_ws.c: true when --host script selected the scripted network. */
bool sim_net_scripted(void);
/* Service the socket, waiting up to wait_ms for it (0 = just poll). */
void sim_net_poll(uint32_t wait_ms);
/* sim_net_script.c: the script's net_* commands; false when no connection is pending. */
bool sim_net_script_open(void);
bool sim_net_script_text(const char *json);
bool sim_net_script_binary(const uint8_t *data, size_t len);
bool sim_net_script_close(uint16_t code);
gadget_status_t sim_net_script_ws_open(void);
gadget_status_t sim_net_script_ws_send(void);
void sim_net_script_ws_close(uint16_t code);

/* sim_mdns.c */
void sim_mdns_poll(void);

/* sim_console.c: read stdin without blocking and post whole lines. */
void sim_console_poll(void);

/* sim_wifi.c: post the initial GADGET_WIFI_CONNECTED. */
void sim_wifi_start(void);

/* sim_battery.c */
void sim_battery_set(uint8_t pct, bool charging);

/* sim_audio.c: the selected backend's pump(now). */
void sim_audio_pump(uint64_t now_ms);

/* sim_script.c: 0 on success; prints the reason and returns non-zero otherwise. */
int sim_script_load(const char *path, unsigned boot);
/* One step: 0 = keep going, 1 = the script finished, -1 = it failed. */
int sim_script_step(uint64_t now_ms);
/* Every text frame op that crossed the socket (from core's tap). */
void sim_script_record(const char *op);
/* The turn of the gadget's last voice.begin or say (from core's tap): net_text's ${turn}. */
void sim_script_note_turn(const char *turn);

/* sim_system.c: re-exec with --boot <n+1>, dropping --pair (contract §2.16). */
_Noreturn void sim_restart(void);

#endif /* SIM_INTERNAL_H */
```

- [ ] **Step 3: Write the HAL backends**

Create `firmware/ports/sim/sim_events.c`:

```c
/* firmware/ports/sim/sim_events.c */
/* SPDX-License-Identifier: Apache-2.0 */
/* The simulator's event queue (sim_hal.h): any thread posts, the main loop
 * delivers in order before the next core_tick(). Payloads are deep-copied. */
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include "sim_hal.h"
#include "sim_internal.h"

typedef struct node {
  gadget_event_t ev;
  void *copy;
  struct node *next;
} node_t;

static pthread_mutex_t s_mu = PTHREAD_MUTEX_INITIALIZER;
static node_t *s_head, *s_tail;

static void *dup_bytes(const void *src, size_t n) {
  void *p = malloc(n ? n : 1);
  if (p != NULL && n) memcpy(p, src, n);
  return p;
}

void sim_post_event(const gadget_event_t *ev) {
  node_t *n = calloc(1, sizeof *n);
  if (n == NULL) return;
  n->ev = *ev;
  switch (ev->type) {
    case GADGET_EV_MIC_FRAME:
      n->copy = dup_bytes(ev->u.mic.pcm, ev->u.mic.samples * sizeof(int16_t));
      n->ev.u.mic.pcm = n->copy;
      break;
    case GADGET_EV_WS_TEXT:
    case GADGET_EV_WS_BINARY:
      n->copy = dup_bytes(ev->u.ws.data, ev->u.ws.len);
      n->ev.u.ws.data = n->copy;
      break;
    case GADGET_EV_CONSOLE_LINE:
      if (ev->u.console.line != NULL) {
        n->copy = dup_bytes(ev->u.console.line, strlen(ev->u.console.line) + 1);
        n->ev.u.console.line = n->copy;
      }
      break;
    case GADGET_EV_WIFI_SCAN:
      n->copy = dup_bytes(ev->u.scan.aps, ev->u.scan.count * sizeof(gadget_wifi_ap_t));
      n->ev.u.scan.aps = n->copy;
      break;
    case GADGET_EV_MDNS:
      n->copy = dup_bytes(ev->u.mdns.hosts, ev->u.mdns.count * sizeof(gadget_mdns_host_t));
      n->ev.u.mdns.hosts = n->copy;
      break;
    default:
      break;
  }
  pthread_mutex_lock(&s_mu);
  if (s_tail) s_tail->next = n;
  else s_head = n;
  s_tail = n;
  pthread_mutex_unlock(&s_mu);
}

void sim_events_deliver(void) {
  pthread_mutex_lock(&s_mu);
  node_t *list = s_head;
  s_head = s_tail = NULL;
  pthread_mutex_unlock(&s_mu);
  while (list != NULL) {
    node_t *next = list->next;
    core_event(&list->ev);
    free(list->copy);
    free(list);
    list = next;
  }
}
```

Create `firmware/ports/sim/sim_storage.c`:

```c
/* firmware/ports/sim/sim_storage.c */
/* SPDX-License-Identifier: Apache-2.0 */
/* The storage HAL as <state>/storage.json (contract §4.6):
 *   {"version": 1, "entries": {"<key>": {"str": "..."} | {"blob": "<hex>"}}}
 * Every write rewrites the file through a temp name and rename(). */
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "cJSON.h"
#include "gadget_hal.h"
#include "gadget_util.h"
#include "sim_internal.h"

static cJSON *s_root;     /* the "entries" object lives inside */
static char s_path[1100];

static int mkdirs(const char *dir) {
  char tmp[1024];
  snprintf(tmp, sizeof tmp, "%s", dir);
  for (char *p = tmp + 1; *p; p++) {
    if (*p == '/') {
      *p = '\0';
      if (mkdir(tmp, 0700) != 0 && errno != EEXIST) return -1;
      *p = '/';
    }
  }
  return (mkdir(tmp, 0700) == 0 || errno == EEXIST) ? 0 : -1;
}

static char *slurp(const char *path) {
  FILE *f = fopen(path, "rb");
  if (f == NULL) return NULL;
  fseek(f, 0, SEEK_END);
  long n = ftell(f);
  fseek(f, 0, SEEK_SET);
  char *buf = malloc((size_t)n + 1);
  size_t got = buf ? fread(buf, 1, (size_t)n, f) : 0;
  fclose(f);
  if (buf) buf[got] = '\0';
  return buf;
}

/* Writes text to path atomically (temp file + rename). */
int sim_write_atomic(const char *path, const char *text) {
  char tmp[1200];
  snprintf(tmp, sizeof tmp, "%s.tmp", path);
  FILE *f = fopen(tmp, "wb");
  if (f == NULL) return -1;
  size_t n = strlen(text);
  bool ok = fwrite(text, 1, n, f) == n;
  ok = (fclose(f) == 0) && ok;
  if (!ok || rename(tmp, path) != 0) return -1;
  return 0;
}

static cJSON *entries(void) { return cJSON_GetObjectItemCaseSensitive(s_root, "entries"); }

static gadget_status_t save(void) {
  char *text = cJSON_Print(s_root);
  if (text == NULL) return GADGET_ERR_NO_MEM;
  int rc = sim_write_atomic(s_path, text);
  cJSON_free(text);
  return rc == 0 ? GADGET_OK : GADGET_ERR_IO;
}

int sim_storage_open(const char *dir) {
  if (mkdirs(dir) != 0) {
    fprintf(stderr, "gadget-sim: cannot create %s\n", dir);
    return -1;
  }
  snprintf(s_path, sizeof s_path, "%s/storage.json", dir);
  char *text = slurp(s_path);
  s_root = text ? cJSON_Parse(text) : NULL;
  free(text);
  if (!cJSON_IsObject(s_root) || !cJSON_IsObject(entries())) {
    cJSON_Delete(s_root);
    s_root = cJSON_CreateObject();
    cJSON_AddNumberToObject(s_root, "version", 1);
    cJSON_AddObjectToObject(s_root, "entries");
  }
  return 0;
}

static gadget_status_t put(const char *key, cJSON *value) {
  if (strlen(key) > 15) {
    cJSON_Delete(value);
    return GADGET_ERR_ARG;
  }
  cJSON_DeleteItemFromObjectCaseSensitive(entries(), key);
  cJSON_AddItemToObject(entries(), key, value);
  return save();
}

gadget_status_t hal_storage_get_str(const char *key, char *buf, size_t cap) {
  const cJSON *e = cJSON_GetObjectItemCaseSensitive(entries(), key);
  const cJSON *v = cJSON_GetObjectItemCaseSensitive(e, "str");
  if (!cJSON_IsString(v)) return GADGET_ERR_NOT_FOUND;
  if (strlen(v->valuestring) + 1 > cap) return GADGET_ERR_LIMIT;
  memcpy(buf, v->valuestring, strlen(v->valuestring) + 1);
  return GADGET_OK;
}

gadget_status_t hal_storage_set_str(const char *key, const char *value) {
  cJSON *e = cJSON_CreateObject();
  cJSON_AddStringToObject(e, "str", value);
  return put(key, e);
}

gadget_status_t hal_storage_get_blob(const char *key, void *buf, size_t cap, size_t *len) {
  const cJSON *e = cJSON_GetObjectItemCaseSensitive(entries(), key);
  const cJSON *v = cJSON_GetObjectItemCaseSensitive(e, "blob");
  if (!cJSON_IsString(v)) return GADGET_ERR_NOT_FOUND;
  gadget_status_t st = gadget_hex_decode(v->valuestring, buf, cap, len);
  return st == GADGET_ERR_PARSE ? GADGET_ERR_IO : st;
}

gadget_status_t hal_storage_set_blob(const char *key, const void *data, size_t len) {
  char *hex = malloc(2 * len + 1);
  if (hex == NULL) return GADGET_ERR_NO_MEM;
  gadget_hex_encode(hex, data, len);
  cJSON *e = cJSON_CreateObject();
  cJSON_AddStringToObject(e, "blob", hex);
  free(hex);
  return put(key, e);
}

gadget_status_t hal_storage_erase(const char *key) {
  if (cJSON_GetObjectItemCaseSensitive(entries(), key) == NULL) return GADGET_OK;
  cJSON_DeleteItemFromObjectCaseSensitive(entries(), key);
  return save();
}

gadget_status_t hal_storage_erase_all(void) {
  cJSON_DeleteItemFromObjectCaseSensitive(s_root, "entries");
  cJSON_AddObjectToObject(s_root, "entries");
  return save();
}
```

Create `firmware/ports/sim/sim_system.c`:

```c
/* firmware/ports/sim/sim_system.c */
/* SPDX-License-Identifier: Apache-2.0 */
/* Clock, log, console output and restarts for the simulator. */
#include <errno.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif
#include "gadget_hal.h"
#include "sim_display.h"
#include "sim_internal.h"

static bool s_log = true;

uint64_t hal_now_ms(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
}

void hal_vlog(gadget_log_level_t level, const char *tag, const char *fmt, va_list ap) {
  if (!s_log) return;
  static const char L[] = "EWID";
  fprintf(stderr, "[%c] %s: ", L[level <= GADGET_LOG_DEBUG ? level : GADGET_LOG_DEBUG], tag);
  vfprintf(stderr, fmt, ap);
  fputc('\n', stderr);
}

void hal_log(gadget_log_level_t level, const char *tag, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  hal_vlog(level, tag, fmt, ap);
  va_end(ap);
}

void hal_log_set_enabled(bool enabled) { s_log = enabled; }

void hal_console_write(const char *line) {
  fputs(line, stdout);
  fputc('\n', stdout);
  fflush(stdout);
}

static bool self_path(char *buf, size_t cap) {
#if defined(__APPLE__)
  uint32_t size = (uint32_t)cap;
  return _NSGetExecutablePath(buf, &size) == 0;
#else
  ssize_t n = readlink("/proc/self/exe", buf, cap - 1);
  if (n <= 0) return false;
  buf[n] = '\0';
  return true;
#endif
}

_Noreturn void sim_restart(void) {
  char path[4096];
  static char boot[16];
  snprintf(boot, sizeof boot, "%u", g_sim.boot + 1);
  char **argv = calloc((size_t)g_sim.argc + 3, sizeof *argv);
  int n = 0;
  argv[n++] = g_sim.argv[0];
  for (int i = 1; i < g_sim.argc; i++) {
    if (strcmp(g_sim.argv[i], "--pair") == 0 || strcmp(g_sim.argv[i], "--boot") == 0) {
      i++; /* drop the option and its value */
      continue;
    }
    argv[n++] = g_sim.argv[i];
  }
  argv[n++] = "--boot";
  argv[n++] = boot;
  argv[n] = NULL;
  sim_display_deinit();
  fflush(stdout);
  fflush(stderr);
  if (!self_path(path, sizeof path)) {
    fprintf(stderr, "gadget-sim: cannot find my own path to restart\n");
    exit(3);
  }
  execv(path, argv);
  fprintf(stderr, "gadget-sim: restart failed: %s\n", strerror(errno));
  exit(3);
}

_Noreturn void hal_restart(void) {
  fprintf(stderr, "gadget-sim: restarting (boot %u)\n", g_sim.boot + 1);
  sim_restart();
}
```

Create `firmware/ports/sim/sim_wifi.c`:

```c
/* firmware/ports/sim/sim_wifi.c */
/* SPDX-License-Identifier: Apache-2.0 */
/* The simulator is always on the network (gadget_hal.h). */
#include <string.h>
#include "gadget_hal.h"
#include "sim_hal.h"
#include "sim_internal.h"

static void post_connected(void) {
  gadget_event_t ev = {.type = GADGET_EV_WIFI_STATE};
  ev.u.wifi.state = GADGET_WIFI_CONNECTED;
  strcpy(ev.u.wifi.ip, "127.0.0.1");
  sim_post_event(&ev);
}

void sim_wifi_start(void) { post_connected(); }

gadget_status_t hal_wifi_connect(const char *ssid, const char *password) {
  (void)ssid;
  (void)password;
  post_connected();
  return GADGET_OK;
}

void hal_wifi_disconnect(void) {}

gadget_wifi_state_t hal_wifi_state(void) { return GADGET_WIFI_CONNECTED; }

gadget_status_t hal_wifi_scan(void) {
  static const gadget_wifi_ap_t aps[] = {{"SimNet", -42, GADGET_AUTH_WPA2}, {"Cafe", -78, GADGET_AUTH_OPEN}};
  gadget_event_t ev = {.type = GADGET_EV_WIFI_SCAN};
  ev.u.scan.aps = aps;
  ev.u.scan.count = 2;
  ev.u.scan.ok = true;
  sim_post_event(&ev);
  return GADGET_OK;
}
```

Create `firmware/ports/sim/sim_battery.c`:

```c
/* firmware/ports/sim/sim_battery.c */
/* SPDX-License-Identifier: Apache-2.0 */
#include "gadget_hal.h"
#include "sim_internal.h"

void sim_battery_set(uint8_t pct, bool charging) {
  g_sim.battery_pct = pct > 100 ? 100 : pct;
  g_sim.battery_charging = charging;
}

bool hal_battery_read(gadget_battery_t *out) {
  if (g_sim.board == NULL || !g_sim.board->has_battery) return false;
  out->pct = g_sim.battery_pct;
  out->charging = g_sim.battery_charging;
  return true;
}
```

Create `firmware/ports/sim/sim_console.c`:

```c
/* firmware/ports/sim/sim_console.c */
/* SPDX-License-Identifier: Apache-2.0 */
/* The stdin console: the same commands as the USB serial console. */
#include <poll.h>
#include <unistd.h>
#include "gadget_console.h"
#include "sim_hal.h"
#include "sim_internal.h"

static gadget_linebuf_t s_lb;
static bool s_started, s_eof;

static void on_line(const char *line, void *ctx) {
  (void)ctx;
  gadget_event_t ev = {.type = GADGET_EV_CONSOLE_LINE};
  ev.u.console.line = line; /* NULL: an over-long line was dropped */
  sim_post_event(&ev);
}

void sim_console_poll(void) {
  if (s_eof) return;
  if (!s_started) {
    gadget_linebuf_init(&s_lb);
    s_started = true;
  }
  struct pollfd p = {.fd = STDIN_FILENO, .events = POLLIN};
  while (poll(&p, 1, 0) > 0 && (p.revents & (POLLIN | POLLHUP))) {
    char buf[512];
    ssize_t n = read(STDIN_FILENO, buf, sizeof buf);
    if (n <= 0) {
      s_eof = true; /* CI passes /dev/null */
      return;
    }
    gadget_linebuf_feed(&s_lb, buf, (size_t)n, on_line, NULL);
  }
}
```

Create `firmware/ports/sim/sim_audio.c`:

```c
/* firmware/ports/sim/sim_audio.c */
/* SPDX-License-Identifier: Apache-2.0 */
/* The only definitions of hal_mic_* and hal_spk_*: they forward to the
 * backend main.c selected (sim_hal.h). Without one, audio is null. */
#include "gadget_hal.h"
#include "sim_hal.h"
#include "sim_internal.h"

static const sim_audio_backend_t *s_backend;

void sim_audio_use(const sim_audio_backend_t *backend) { s_backend = backend; }

void sim_audio_pump(uint64_t now_ms) {
  if (s_backend) s_backend->pump(now_ms);
}

gadget_status_t hal_mic_start(uint32_t rate) {
  if (rate != GADGET_MIC_RATE) return GADGET_ERR_UNSUPPORTED;
  return s_backend ? s_backend->mic_start(rate) : GADGET_OK;
}

void hal_mic_stop(void) {
  if (s_backend) s_backend->mic_stop();
}

gadget_status_t hal_spk_open(uint32_t rate) { return s_backend ? s_backend->spk_open(rate) : GADGET_OK; }

size_t hal_spk_write(const int16_t *pcm, size_t samples) {
  return s_backend ? s_backend->spk_write(pcm, samples) : samples;
}

uint32_t hal_spk_buffered_ms(void) { return s_backend ? s_backend->spk_buffered_ms() : 0; }

void hal_spk_stop(void) {
  if (s_backend) s_backend->spk_stop();
}

void hal_spk_set_volume(uint8_t pct) {
  if (s_backend) s_backend->spk_set_volume(pct);
}
```

Create `firmware/ports/sim/sim_audio_file.c`:

```c
/* firmware/ports/sim/sim_audio_file.c */
/* SPDX-License-Identifier: Apache-2.0 */
/* File and null audio (sim_hal.h): the mic replays a 16 kHz mono PCM16 WAV
 * from its start on each TALK hold (silence after its end, or always when
 * there is no file); the speaker drains in real (virtual) time and writes
 * what it played to a WAV. Timing follows the main loop's clock. */
#include <stdlib.h>
#include <string.h>
#include "gadget_hal.h"
#include "sim_hal.h"
#include "sim_internal.h"

#define SPK_BUFFER_MS 200u

static struct {
  int16_t *mic;            /* the whole input WAV */
  size_t mic_len, mic_pos;
  bool mic_on;
  uint64_t mic_next;       /* when the next 20 ms frame is due; 0 = at the next pump */
  FILE *out;
  const char *out_path;
  uint32_t out_rate;
  uint32_t out_samples;    /* data written to the WAV so far */
  uint32_t rate;           /* the open speaker rate */
  int16_t *queue;          /* queued but not yet played */
  size_t queued, cap;
  uint64_t last_pump;
} F;

static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static void wr32(uint8_t *p, uint32_t v) {
  for (int i = 0; i < 4; i++) p[i] = (uint8_t)(v >> (8 * i));
}

/* Loads a PCM16 mono 16 kHz WAV; NULL (with a message) otherwise. */
static int16_t *load_wav(const char *path, size_t *samples) {
  FILE *f = fopen(path, "rb");
  if (f == NULL) {
    fprintf(stderr, "gadget-sim: cannot open %s\n", path);
    return NULL;
  }
  fseek(f, 0, SEEK_END);
  long n = ftell(f);
  fseek(f, 0, SEEK_SET);
  uint8_t *b = malloc((size_t)n);
  size_t got = b ? fread(b, 1, (size_t)n, f) : 0;
  fclose(f);
  int16_t *pcm = NULL;
  if (got >= 12 && memcmp(b, "RIFF", 4) == 0 && memcmp(b + 8, "WAVE", 4) == 0) {
    bool fmt_ok = false;
    size_t off = 12;
    while (off + 8 <= got) {
      uint32_t len = rd32(b + off + 4);
      if (memcmp(b + off, "fmt ", 4) == 0 && len >= 16) {
        fmt_ok = rd16(b + off + 8) == 1 && rd16(b + off + 10) == 1 && rd32(b + off + 12) == 16000 &&
                 rd16(b + off + 22) == 16;
      } else if (memcmp(b + off, "data", 4) == 0 && fmt_ok) {
        size_t bytes = len > got - off - 8 ? got - off - 8 : len;
        *samples = bytes / 2;
        pcm = malloc(*samples * sizeof(int16_t) + 1);
        for (size_t i = 0; i < *samples; i++) pcm[i] = (int16_t)rd16(b + off + 8 + 2 * i);
        break;
      }
      off += 8 + len + (len & 1u);
    }
  }
  free(b);
  if (pcm == NULL) fprintf(stderr, "gadget-sim: %s is not a 16 kHz mono 16-bit PCM WAV\n", path);
  return pcm;
}

static void write_header(void) {
  uint8_t h[44] = {'R', 'I', 'F', 'F', 0, 0, 0, 0, 'W', 'A', 'V', 'E', 'f', 'm', 't', ' ', 16, 0, 0, 0, 1, 0, 1, 0};
  wr32(h + 24, F.out_rate);
  wr32(h + 28, F.out_rate * 2u);
  h[32] = 2;
  h[34] = 16;
  memcpy(h + 36, "data", 4);
  wr32(h + 40, F.out_samples * 2u);
  wr32(h + 4, 36u + F.out_samples * 2u);
  fseek(F.out, 0, SEEK_SET);
  fwrite(h, 1, sizeof h, F.out);
  fseek(F.out, 0, SEEK_END);
}

static void open_out(uint32_t rate) {
  if (F.out != NULL || F.out_path == NULL) return;
  F.out_rate = rate;
  /* after a restart (--boot > 0) keep what the earlier boots played */
  if (g_sim.boot > 0 && (F.out = fopen(F.out_path, "r+b")) != NULL) {
    uint8_t h[44];
    if (fread(h, 1, sizeof h, F.out) == sizeof h && memcmp(h, "RIFF", 4) == 0) {
      F.out_rate = rd32(h + 24);
      F.out_samples = rd32(h + 40) / 2u;
      fseek(F.out, 0, SEEK_END);
      return;
    }
    fclose(F.out);
  }
  F.out = fopen(F.out_path, "w+b");
  if (F.out == NULL) {
    fprintf(stderr, "gadget-sim: cannot write %s\n", F.out_path);
    F.out_path = NULL;
    return;
  }
  F.out_samples = 0;
  write_header();
}

static gadget_status_t mic_start(uint32_t rate) {
  (void)rate;
  F.mic_on = true;
  F.mic_pos = 0;
  F.mic_next = 0;
  return GADGET_OK;
}

static void mic_stop(void) { F.mic_on = false; }

static gadget_status_t spk_open(uint32_t rate) {
  if (rate != 16000 && rate != 24000) return GADGET_ERR_UNSUPPORTED;
  if (rate != F.rate) F.queued = 0;
  F.rate = rate;
  F.cap = rate * SPK_BUFFER_MS / 1000u;
  int16_t *q = realloc(F.queue, F.cap * sizeof(int16_t));
  if (q == NULL) return GADGET_ERR_NO_MEM;
  F.queue = q;
  open_out(rate);
  return GADGET_OK;
}

static size_t spk_write(const int16_t *pcm, size_t samples) {
  if (F.rate == 0) return 0;
  size_t room = F.cap - F.queued;
  size_t n = samples < room ? samples : room;
  memcpy(F.queue + F.queued, pcm, n * sizeof(int16_t));
  F.queued += n;
  return n;
}

static uint32_t spk_buffered_ms(void) { return F.rate ? (uint32_t)(F.queued * 1000u / F.rate) : 0; }
static void spk_stop(void) { F.queued = 0; }
static void spk_set_volume(uint8_t pct) { (void)pct; }

static void pump(uint64_t now) {
  if (F.mic_on) {
    if (F.mic_next == 0) F.mic_next = now + 20;
    while (now >= F.mic_next) {
      int16_t frame[GADGET_MIC_FRAME_SAMPLES] = {0};
      for (size_t i = 0; i < GADGET_MIC_FRAME_SAMPLES && F.mic_pos < F.mic_len; i++) frame[i] = F.mic[F.mic_pos++];
      gadget_event_t ev = {.type = GADGET_EV_MIC_FRAME};
      ev.u.mic.pcm = frame;
      ev.u.mic.samples = GADGET_MIC_FRAME_SAMPLES;
      sim_post_event(&ev);
      F.mic_next += 20;
    }
  }
  uint64_t elapsed = F.last_pump ? now - F.last_pump : 0;
  F.last_pump = now;
  if (F.rate == 0 || F.queued == 0) return;
  size_t played = (size_t)(elapsed * F.rate / 1000u);
  if (played > F.queued) played = F.queued;
  if (played == 0) return;
  if (F.out != NULL) {
    for (size_t i = 0; i < played; i++) {
      uint8_t le[2] = {(uint8_t)((uint16_t)F.queue[i] & 0xff), (uint8_t)((uint16_t)F.queue[i] >> 8)};
      fwrite(le, 1, 2, F.out);
    }
    F.out_samples += (uint32_t)played;
    write_header();
    fflush(F.out);
  }
  memmove(F.queue, F.queue + played, (F.queued - played) * sizeof(int16_t));
  F.queued -= played;
}

static const sim_audio_backend_t BACKEND = {mic_start, mic_stop, spk_open, spk_write,
                                             spk_buffered_ms, spk_stop, spk_set_volume, pump};

const sim_audio_backend_t *sim_audio_file_backend(const char *mic_wav, const char *spk_wav) {
  memset(&F, 0, sizeof F);
  if (mic_wav != NULL) {
    F.mic = load_wav(mic_wav, &F.mic_len);
    if (F.mic == NULL) return NULL;
  }
  F.out_path = spk_wav;
  return &BACKEND;
}
```

`host auto` uses Apple's `dns_sd` (part of libSystem, so no link flag) and reports only fully resolved instances once the browse time is up. On Linux it reports `GADGET_ERR_UNSUPPORTED`, so core prints an empty `hosts` line and waits for `host <address>` (spec §5.7).

Create `firmware/ports/sim/sim_mdns.c`:

```c
/* firmware/ports/sim/sim_mdns.c */
/* SPDX-License-Identifier: Apache-2.0 */
/* host auto in the simulator: browse _openmausbot._tcp with Apple's dns_sd
 * (macOS only; Linux needs --host). Each found instance is resolved (port,
 * TXT id=) and its IPv4 address looked up; one GADGET_EV_MDNS reports all of
 * them when the browse time is up. Driven from the main loop, never blocks. */
#include <string.h>
#include "gadget_hal.h"
#include "sim_hal.h"
#include "sim_internal.h"

#if defined(__APPLE__)
#include <arpa/inet.h>
#include <dns_sd.h>
#include <poll.h>

#define MAX_REFS 24

static struct {
  bool active;
  uint64_t deadline;
  DNSServiceRef refs[MAX_REFS];
  int n_refs;
  gadget_mdns_host_t hosts[8];
  uint16_t ports[8];
  uint8_t count;
} M;

static void add_ref(DNSServiceRef r) {
  if (M.n_refs < MAX_REFS) M.refs[M.n_refs++] = r;
  else DNSServiceRefDeallocate(r);
}

static void DNSSD_API on_addr(DNSServiceRef ref, DNSServiceFlags flags, uint32_t ifindex, DNSServiceErrorType err,
                              const char *hostname, const struct sockaddr *addr, uint32_t ttl, void *ctx) {
  (void)ref;
  (void)flags;
  (void)ifindex;
  (void)hostname;
  (void)ttl;
  gadget_mdns_host_t *h = ctx;
  if (err != kDNSServiceErr_NoError || addr == NULL || addr->sa_family != AF_INET || h->address[0] != '\0') return;
  char ip[INET_ADDRSTRLEN];
  inet_ntop(AF_INET, &((const struct sockaddr_in *)(const void *)addr)->sin_addr, ip, sizeof ip);
  uint16_t port = M.ports[h - M.hosts];
  snprintf(h->address, sizeof h->address, "%s:%u", ip, (unsigned)port);
}

static void DNSSD_API on_resolve(DNSServiceRef ref, DNSServiceFlags flags, uint32_t ifindex, DNSServiceErrorType err,
                                 const char *fullname, const char *target, uint16_t port_be, uint16_t txt_len,
                                 const unsigned char *txt, void *ctx) {
  (void)ref;
  (void)flags;
  (void)fullname;
  gadget_mdns_host_t *h = ctx;
  if (err != kDNSServiceErr_NoError) return;
  M.ports[h - M.hosts] = ntohs(port_be);
  uint8_t vlen = 0;
  const void *v = TXTRecordGetValuePtr(txt_len, txt, "id", &vlen);
  if (v != NULL && vlen <= GADGET_HOST_ID_LEN) {
    memcpy(h->id, v, vlen);
    h->id[vlen] = '\0';
  }
  DNSServiceRef a = NULL;
  if (DNSServiceGetAddrInfo(&a, 0, ifindex, kDNSServiceProtocol_IPv4, target, on_addr, h) == kDNSServiceErr_NoError) {
    add_ref(a);
  }
}

static void DNSSD_API on_browse(DNSServiceRef ref, DNSServiceFlags flags, uint32_t ifindex, DNSServiceErrorType err,
                                const char *name, const char *type, const char *domain, void *ctx) {
  (void)ref;
  (void)ctx;
  if (err != kDNSServiceErr_NoError || !(flags & kDNSServiceFlagsAdd) || M.count >= 8) return;
  for (uint8_t i = 0; i < M.count; i++) {
    if (strcmp(M.hosts[i].name, name) == 0) return; /* seen on another interface */
  }
  gadget_mdns_host_t *h = &M.hosts[M.count++];
  memset(h, 0, sizeof *h);
  snprintf(h->name, sizeof h->name, "%s", name);
  DNSServiceRef r = NULL;
  if (DNSServiceResolve(&r, 0, ifindex, name, type, domain, on_resolve, h) == kDNSServiceErr_NoError) add_ref(r);
}

static void finish(void) {
  gadget_mdns_host_t found[8];
  uint8_t n = 0;
  for (uint8_t i = 0; i < M.count; i++) {
    if (M.hosts[i].address[0] != '\0') found[n++] = M.hosts[i]; /* only fully resolved ones */
  }
  for (int i = 0; i < M.n_refs; i++) DNSServiceRefDeallocate(M.refs[i]);
  M.n_refs = 0;
  M.active = false;
  gadget_event_t ev = {.type = GADGET_EV_MDNS};
  ev.u.mdns.hosts = found;
  ev.u.mdns.count = n;
  ev.u.mdns.ok = true;
  sim_post_event(&ev);
}

gadget_status_t hal_mdns_browse(uint32_t timeout_ms) {
  if (M.active) return GADGET_ERR_BUSY;
  memset(&M, 0, sizeof M);
  DNSServiceRef b = NULL;
  if (DNSServiceBrowse(&b, 0, kDNSServiceInterfaceIndexAny, "_openmausbot._tcp", NULL, on_browse, NULL) !=
      kDNSServiceErr_NoError) {
    return GADGET_ERR_IO;
  }
  add_ref(b);
  M.active = true;
  M.deadline = hal_now_ms() + timeout_ms;
  return GADGET_OK;
}

void sim_mdns_poll(void) {
  if (!M.active) return;
  for (int i = 0; i < M.n_refs; i++) {
    struct pollfd p = {.fd = DNSServiceRefSockFD(M.refs[i]), .events = POLLIN};
    if (poll(&p, 1, 0) > 0 && (p.revents & POLLIN)) DNSServiceProcessResult(M.refs[i]);
  }
  if (hal_now_ms() >= M.deadline) finish();
}

#else /* Linux: no dns_sd without a running avahi-daemon; --host is required */

gadget_status_t hal_mdns_browse(uint32_t timeout_ms) {
  (void)timeout_ms;
  return GADGET_ERR_UNSUPPORTED;
}

void sim_mdns_poll(void) {}

#endif
```

Create `firmware/ports/sim/sim_ota.c`:

```c
/* firmware/ports/sim/sim_ota.c */
/* SPDX-License-Identifier: Apache-2.0 */
/* OTA in the simulator (spec §5.7, contract §4.6): images are opaque bytes
 * written to slot0.bin / slot1.bin; otadata.json records
 *   {"active": 0|1, "state": "valid"|"pending"|"invalid", "version": "<fw>",
 *    "previous": {"slot": 0|1, "version": "<fw>"}, "booted": true}
 * ("booted" is this file's own addition: a pending image that boots a second
 * time without being confirmed rolls back, like a crash on the device). */
#include <stdlib.h>
#include <string.h>
#include "cJSON.h"
#include "gadget_hal.h"
#include "sim_hal.h"
#include "sim_internal.h"

#ifndef GADGET_SIM_VERSION
#define GADGET_SIM_VERSION "0.0.0-dev"
#endif

static struct {
  char dir[1024];
  int active;
  char state[16];
  char version[GADGET_VERSION_MAX + 1];
  bool has_prev;
  int prev_slot;
  char prev_version[GADGET_VERSION_MAX + 1];
  bool booted;
  hal_ota_img_state_t running;
  FILE *slot;
  int target;
  uint32_t size, written;
} O;

static void save(void) {
  cJSON *o = cJSON_CreateObject();
  cJSON_AddNumberToObject(o, "active", O.active);
  cJSON_AddStringToObject(o, "state", O.state);
  cJSON_AddStringToObject(o, "version", O.version);
  if (O.has_prev) {
    cJSON *p = cJSON_AddObjectToObject(o, "previous");
    cJSON_AddNumberToObject(p, "slot", O.prev_slot);
    cJSON_AddStringToObject(p, "version", O.prev_version);
  }
  if (O.booted) cJSON_AddTrueToObject(o, "booted");
  char *text = cJSON_Print(o);
  cJSON_Delete(o);
  char path[1100];
  snprintf(path, sizeof path, "%s/otadata.json", O.dir);
  if (text == NULL || sim_write_atomic(path, text) != 0) fprintf(stderr, "gadget-sim: cannot write %s\n", path);
  cJSON_free(text);
}

/* <dir>/slot<n>.bin, or its temp name while the image is written (contract §4.6). */
static void slot_path(char *out, size_t cap, int slot, bool tmp) {
  snprintf(out, cap, "%s/slot%d.bin%s", O.dir, slot, tmp ? ".tmp" : "");
}

static void roll_back(void) {
  fprintf(stderr, "gadget-sim: image %s was not confirmed; back to %s\n", O.version,
          O.has_prev ? O.prev_version : GADGET_SIM_VERSION);
  int bad_slot = O.active;
  char bad_version[GADGET_VERSION_MAX + 1];
  snprintf(bad_version, sizeof bad_version, "%s", O.version);
  O.active = O.has_prev ? O.prev_slot : 0;
  snprintf(O.version, sizeof O.version, "%s", O.has_prev ? O.prev_version : GADGET_SIM_VERSION);
  O.prev_slot = bad_slot;
  snprintf(O.prev_version, sizeof O.prev_version, "%s", bad_version);
  O.has_prev = true;
  snprintf(O.state, sizeof O.state, "valid");
  O.booted = false;
  save();
}

int sim_ota_open(const char *dir, char *fw, size_t cap) {
  memset(&O, 0, sizeof O);
  snprintf(O.dir, sizeof O.dir, "%s", dir);
  snprintf(O.state, sizeof O.state, "valid");
  snprintf(O.version, sizeof O.version, "%s", GADGET_SIM_VERSION);
  char path[1100];
  snprintf(path, sizeof path, "%s/otadata.json", dir);
  FILE *f = fopen(path, "rb");
  if (f != NULL) {
    char buf[1024];
    size_t n = fread(buf, 1, sizeof buf - 1, f);
    fclose(f);
    buf[n] = '\0';
    cJSON *o = cJSON_Parse(buf);
    const cJSON *a = cJSON_GetObjectItemCaseSensitive(o, "active");
    const cJSON *s = cJSON_GetObjectItemCaseSensitive(o, "state");
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, "version");
    const cJSON *p = cJSON_GetObjectItemCaseSensitive(o, "previous");
    if (cJSON_IsNumber(a) && cJSON_IsString(s) && cJSON_IsString(v)) {
      O.active = a->valueint == 1 ? 1 : 0;
      snprintf(O.state, sizeof O.state, "%s", s->valuestring);
      snprintf(O.version, sizeof O.version, "%s", v->valuestring);
      O.booted = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(o, "booted"));
      const cJSON *ps = cJSON_GetObjectItemCaseSensitive(p, "slot");
      const cJSON *pv = cJSON_GetObjectItemCaseSensitive(p, "version");
      if (cJSON_IsNumber(ps) && cJSON_IsString(pv)) {
        O.has_prev = true;
        O.prev_slot = ps->valueint == 1 ? 1 : 0;
        snprintf(O.prev_version, sizeof O.prev_version, "%s", pv->valuestring);
      }
    }
    cJSON_Delete(o);
  }
  O.running = HAL_OTA_IMG_VALID;
  if (strcmp(O.state, "invalid") == 0 || (strcmp(O.state, "pending") == 0 && O.booted)) {
    roll_back(); /* the bootloader's job on the device */
  } else if (strcmp(O.state, "pending") == 0) {
    O.booted = true;
    O.running = HAL_OTA_IMG_PENDING_VERIFY;
    save();
  }
  snprintf(fw, cap, "%s", O.version);
  return 0;
}

gadget_status_t hal_ota_begin(uint32_t size) {
  if (size == 0 || size > g_sim.board->ota_max) return GADGET_ERR_LIMIT;
  hal_ota_abort(); /* an unfinished earlier image */
  O.target = 1 - O.active;
  char path[1100];
  slot_path(path, sizeof path, O.target, true);
  O.slot = fopen(path, "wb");
  if (O.slot == NULL) return GADGET_ERR_IO;
  O.size = size;
  O.written = 0;
  return GADGET_OK;
}

gadget_status_t hal_ota_write(uint32_t offset, const uint8_t *data, size_t len) {
  if (O.slot == NULL || offset != O.written || offset + len > O.size) return GADGET_ERR_STATE;
  gadget_event_t ev;
  if (fwrite(data, 1, len, O.slot) != len) {
    ev.type = GADGET_EV_OTA_ERROR;
    ev.u.ota_error.err = GADGET_ERR_IO;
  } else {
    O.written += (uint32_t)len;
    ev.type = GADGET_EV_OTA_WRITTEN;
    ev.u.ota_written.written = O.written;
  }
  sim_post_event(&ev);
  return GADGET_OK;
}

gadget_status_t hal_ota_finalize(void) {
  if (O.slot == NULL || O.written != O.size) return GADGET_ERR_STATE;
  int rc = fclose(O.slot);
  O.slot = NULL;
  char tmp[1100], path[1100];
  slot_path(tmp, sizeof tmp, O.target, true);
  slot_path(path, sizeof path, O.target, false);
  if (rc != 0 || rename(tmp, path) != 0) {
    remove(tmp);
    return GADGET_ERR_IO;
  }
  return GADGET_OK;
}

gadget_status_t hal_ota_set_boot(const char *version) {
  O.prev_slot = O.active;
  snprintf(O.prev_version, sizeof O.prev_version, "%s", O.version);
  O.has_prev = true;
  O.active = O.target;
  snprintf(O.version, sizeof O.version, "%s", version);
  snprintf(O.state, sizeof O.state, "pending");
  O.booted = false;
  save();
  return GADGET_OK;
}

void hal_ota_abort(void) {
  if (O.slot != NULL) {
    fclose(O.slot);
    char tmp[1100];
    slot_path(tmp, sizeof tmp, O.target, true);
    remove(tmp);
  }
  O.slot = NULL;
  O.size = O.written = 0;
}

hal_ota_img_state_t hal_ota_running_state(void) { return O.running; }

gadget_status_t hal_ota_mark_valid(void) {
  snprintf(O.state, sizeof O.state, "valid");
  O.booted = false;
  O.running = HAL_OTA_IMG_VALID;
  save();
  return GADGET_OK;
}

_Noreturn void hal_ota_mark_invalid_and_reboot(void) {
  snprintf(O.state, sizeof O.state, "invalid");
  save();
  fprintf(stderr, "gadget-sim: probation failed; restarting into the previous image\n");
  sim_restart();
}
```

Create `firmware/ports/sim/sim_net_script.c`:

```c
/* firmware/ports/sim/sim_net_script.c */
/* SPDX-License-Identifier: Apache-2.0 */
/* --host script: no socket. hal_ws_open() only marks a connection pending;
 * the script's net_open / net_text / net_binary / net_close commands play
 * the host. What the gadget sends is seen through core's tap (expect). */
#include <string.h>
#include "gadget_hal.h"
#include "sim_hal.h"
#include "sim_internal.h"

static bool s_pending, s_open;

static void post_closed(uint16_t code) {
  gadget_event_t ev = {.type = GADGET_EV_WS_CLOSED};
  ev.u.closed.code = code;
  sim_post_event(&ev);
  s_pending = s_open = false;
}

gadget_status_t sim_net_script_ws_open(void) {
  if (s_pending) return GADGET_ERR_BUSY;
  s_pending = true;
  s_open = false;
  return GADGET_OK;
}

gadget_status_t sim_net_script_ws_send(void) { return s_open ? GADGET_OK : GADGET_ERR_BUSY; }

void sim_net_script_ws_close(uint16_t code) {
  if (s_pending) post_closed(code);
}

bool sim_net_script_open(void) {
  if (!s_pending || s_open) return false;
  s_open = true;
  gadget_event_t ev = {.type = GADGET_EV_WS_OPEN};
  sim_post_event(&ev);
  return true;
}

static bool post_data(gadget_event_type_t type, const uint8_t *data, size_t len) {
  if (!s_open) return false;
  gadget_event_t ev = {.type = type};
  ev.u.ws.data = data;
  ev.u.ws.len = len;
  sim_post_event(&ev);
  return true;
}

bool sim_net_script_text(const char *json) { return post_data(GADGET_EV_WS_TEXT, (const uint8_t *)json, strlen(json)); }
bool sim_net_script_binary(const uint8_t *data, size_t len) { return post_data(GADGET_EV_WS_BINARY, data, len); }

bool sim_net_script_close(uint16_t code) {
  if (!s_pending) return false;
  post_closed(code);
  return true;
}
```

`sim_ws.c` starts as the dispatcher for the scripted network. Task 16 replaces it with the real client.

Create `firmware/ports/sim/sim_ws.c`:

```c
/* firmware/ports/sim/sim_ws.c */
/* SPDX-License-Identifier: Apache-2.0 */
/* The WebSocket HAL. With --host script it is the scripted network
 * (sim_net_script.c); a real socket needs the client that the next task adds. */
#include <string.h>
#include "gadget_hal.h"
#include "sim_internal.h"

bool sim_net_scripted(void) { return g_sim.host != NULL && strcmp(g_sim.host, "script") == 0; }

gadget_status_t hal_ws_open(const char *host, uint16_t port) {
  (void)host;
  (void)port;
  if (sim_net_scripted()) return sim_net_script_ws_open();
  hal_log(GADGET_LOG_ERROR, "sim", "this build has no WebSocket client; use --host script");
  return GADGET_ERR_UNSUPPORTED;
}

gadget_status_t hal_ws_send_text(const char *data, size_t len) {
  (void)data;
  (void)len;
  return sim_net_scripted() ? sim_net_script_ws_send() : GADGET_ERR_BUSY;
}

gadget_status_t hal_ws_send_binary(const uint8_t *data, size_t len) {
  (void)data;
  (void)len;
  return sim_net_scripted() ? sim_net_script_ws_send() : GADGET_ERR_BUSY;
}

void hal_ws_close(uint16_t code) {
  if (sim_net_scripted()) sim_net_script_ws_close(code);
}

void sim_net_poll(uint32_t wait_ms) { (void)wait_ms; }
```

Create `firmware/ports/sim/sim_display_null.c`:

```c
/* firmware/ports/sim/sim_display_null.c */
/* SPDX-License-Identifier: Apache-2.0 */
/* The display without LVGL (GADGET_WITH_LVGL=OFF): nothing is drawn and
 * snapshots are skipped. Plan P2b adds sim_display_lvgl.c. */
#include "sim_display.h"

int sim_display_init(const gadget_board_t *board, const sim_display_opts_t *opts) {
  (void)board;
  (void)opts;
  return 0;
}

void sim_display_advance(uint32_t elapsed_ms) { (void)elapsed_ms; }
void sim_display_poll(void) {}
void sim_display_touch(bool pressed, int16_t x, int16_t y) {
  (void)pressed;
  (void)x;
  (void)y;
}
bool sim_display_quit_requested(void) { return false; }
int sim_display_snapshot(const char *name) {
  (void)name;
  return -1;
}
void sim_display_deinit(void) {}
```

Create `firmware/ports/sim/ui_stub.c`:

```c
/* firmware/ports/sim/ui_stub.c */
/* SPDX-License-Identifier: Apache-2.0 */
/* The UI API without LVGL: remembers the last rendered revision and screen,
 * and with --trace prints screen changes. Plan P2b's firmware/ui replaces it. */
#include <stdio.h>
#include "gadget_ui.h"
#include "sim_internal.h"

static const char *const SCREEN_NAMES[UI_SCREEN__COUNT] = {
    "boot", "setup", "offline", "idle", "listening", "thinking", "speaking", "reply", "ask", "card", "image", "update"};

static uint32_t s_rev;
static int s_screen = -1;

gadget_status_t ui_init(const gadget_board_t *board, uint32_t prng_seed) {
  (void)board;
  (void)prng_seed;
  s_rev = 0;
  s_screen = -1;
  return GADGET_OK;
}

void ui_render(const ui_model_t *m) {
  if (m->rev == s_rev) return;
  s_rev = m->rev;
  if ((int)m->screen != s_screen) {
    s_screen = (int)m->screen;
    if (g_sim.trace && m->screen < UI_SCREEN__COUNT) fprintf(stderr, "ui: %s\n", SCREEN_NAMES[m->screen]);
  }
}

void ui_tick(uint64_t now_ms) { (void)now_ms; }
void ui_deinit(void) {}
```

- [ ] **Step 4: Write the command line, the script engine and the main loop**

`main.c`'s protocol tap records every op for `expect`, and notes the `turn` of each `voice.begin` and `say` the gadget sends, for `${turn}`.

Create `firmware/ports/sim/sim_args.c`:

```c
/* firmware/ports/sim/sim_args.c */
/* SPDX-License-Identifier: Apache-2.0 */
/* gadget-sim command line (contract §2.16). */
#include <stdlib.h>
#include <string.h>
#include "sim_internal.h"

#ifndef GADGET_SIM_VERSION
#define GADGET_SIM_VERSION "0.0.0-dev"
#endif

sim_args_t g_sim;

static void usage(FILE *f) {
  fprintf(f,
          "usage: gadget-sim --board <id> [options]\n"
          "  --board <id>            amoled-175c, amoled-175, lcd-154 or devkit (required)\n"
          "  --name <name>           state slot ~/.openmausbot-gadget/sim/<name>/ (default: default)\n"
          "  --state-dir <dir>       use this state folder instead\n"
          "  --host <addr[:port]>    same as the console command host <addr>; --host script = scripted network\n"
          "  --pair <code>           same as the console command pair <code> (first boot only)\n"
          "  --headless              no window: virtual clock, null display (requires --script)\n"
          "  --script <file>         run a script; exit 0 at its end, 1 on the first failure\n"
          "  --mic-file <wav>        16 kHz mono PCM16 WAV played into the mic on each TALK hold\n"
          "  --speaker-file <wav>    write everything played to this WAV\n"
          "  --seed <u32>            PRNG seed (default 1 with --headless, else random)\n"
          "  --battery <pct>[:charging]  simulated battery (default 100)\n"
          "  --zoom <n>              window zoom\n"
          "  --snapshot-dir <dir>    default firmware/tests/snapshots/<board>\n"
          "  --fail-probation        test only: ignore the first ready on a new image\n"
          "  --probation-ms <ms>     test only: probation length\n"
          "  --trace                 print every text frame on stderr\n"
          "  --boot <n>              set by the simulator's own restarts\n"
          "  --version, --help\n");
}

static bool parse_u32(const char *s, uint32_t *out) {
  if (s == NULL || *s == '\0') return false;
  char *end = NULL;
  unsigned long v = strtoul(s, &end, 10);
  if (*end != '\0' || v > 0xFFFFFFFFul) return false;
  *out = (uint32_t)v;
  return true;
}

int sim_args_parse(int argc, char **argv, sim_args_t *a) {
  memset(a, 0, sizeof *a);
  a->argc = argc;
  a->argv = argv;
  a->battery_pct = 100;
  a->zoom = 0.0f;
  const char *board = NULL, *state_dir = NULL;
  for (int i = 1; i < argc; i++) {
    const char *k = argv[i];
    const char *v = i + 1 < argc ? argv[i + 1] : NULL;
#define TAKE()                                                    \
  do {                                                            \
    if (v == NULL) {                                              \
      fprintf(stderr, "gadget-sim: %s needs a value\n", k);       \
      return 2;                                                   \
    }                                                             \
    i++;                                                          \
  } while (0)
    if (strcmp(k, "--help") == 0) {
      usage(stdout);
      return 1;
    } else if (strcmp(k, "--version") == 0) {
      printf("gadget-sim %s\n", GADGET_SIM_VERSION);
      return 1;
    } else if (strcmp(k, "--board") == 0) {
      TAKE();
      board = v;
    } else if (strcmp(k, "--name") == 0) {
      TAKE();
      a->name = v;
    } else if (strcmp(k, "--state-dir") == 0) {
      TAKE();
      state_dir = v;
    } else if (strcmp(k, "--host") == 0) {
      TAKE();
      a->host = v;
    } else if (strcmp(k, "--pair") == 0) {
      TAKE();
      a->pair = v;
    } else if (strcmp(k, "--boot") == 0) {
      TAKE();
      uint32_t n;
      if (!parse_u32(v, &n)) goto bad;
      a->boot = n;
    } else if (strcmp(k, "--headless") == 0) {
      a->headless = true;
    } else if (strcmp(k, "--script") == 0) {
      TAKE();
      a->script = v;
    } else if (strcmp(k, "--mic-file") == 0) {
      TAKE();
      a->mic_file = v;
    } else if (strcmp(k, "--speaker-file") == 0) {
      TAKE();
      a->spk_file = v;
    } else if (strcmp(k, "--seed") == 0) {
      TAKE();
      if (!parse_u32(v, &a->seed)) goto bad;
      a->seed_set = true;
    } else if (strcmp(k, "--battery") == 0) {
      TAKE();
      char tmp[32];
      snprintf(tmp, sizeof tmp, "%s", v);
      char *colon = strchr(tmp, ':');
      a->battery_charging = colon != NULL && strcmp(colon + 1, "charging") == 0;
      if (colon) *colon = '\0';
      uint32_t pct;
      if (!parse_u32(tmp, &pct) || pct > 100 || (colon && !a->battery_charging)) goto bad;
      a->battery_pct = (uint8_t)pct;
    } else if (strcmp(k, "--zoom") == 0) {
      TAKE();
      a->zoom = (float)atof(v);
    } else if (strcmp(k, "--snapshot-dir") == 0) {
      TAKE();
      a->snapshot_dir = v;
    } else if (strcmp(k, "--fail-probation") == 0) {
      a->fail_probation = true;
    } else if (strcmp(k, "--probation-ms") == 0) {
      TAKE();
      if (!parse_u32(v, &a->probation_ms)) goto bad;
    } else if (strcmp(k, "--trace") == 0) {
      a->trace = true;
    } else {
      fprintf(stderr, "gadget-sim: unknown option %s\n", k);
      usage(stderr);
      return 2;
    }
    continue;
  bad:
    fprintf(stderr, "gadget-sim: bad value for %s: %s\n", k, v);
    return 2;
#undef TAKE
  }
  a->board = gadget_board_by_id(board);
  if (a->board == NULL) {
    fprintf(stderr, "gadget-sim: --board must be amoled-175c, amoled-175, lcd-154 or devkit\n");
    return 2;
  }
  if (a->headless && a->script == NULL) {
    fprintf(stderr, "gadget-sim: --headless requires --script\n");
    return 2;
  }
  if (a->zoom <= 0.0f) a->zoom = a->board->screen_w <= 320 ? 2.0f : 1.0f;
  if (!a->seed_set) a->seed = a->headless ? 1u : 0u; /* 0: core seeds from the RNG */
  if (a->snapshot_dir == NULL) {
    static char snap[256];
    snprintf(snap, sizeof snap, "firmware/tests/snapshots/%s", a->board->id);
    a->snapshot_dir = snap;
  }
  if (state_dir != NULL) {
    snprintf(a->state_dir, sizeof a->state_dir, "%s", state_dir);
  } else {
    const char *home = getenv("HOME");
    snprintf(a->state_dir, sizeof a->state_dir, "%s/.openmausbot-gadget/sim/%s", home ? home : ".",
             a->name ? a->name : "default");
  }
  return 0;
}
```

Create `firmware/ports/sim/sim_script.c`:

```c
/* firmware/ports/sim/sim_script.c */
/* SPDX-License-Identifier: Apache-2.0 */
/* Headless scripts (spec §5.7, contract §2.16): one command per line, '#'
 * starts a comment. A command that acts (input, console, net_*) runs once per
 * main-loop iteration; wait / expect / model / boot / net_open block until
 * they are met or time out. `expect <op>` matches the first frame with that op since the
 * previous expect matched, including frames that crossed before the line
 * was reached, so scripts do not race the network. `net_open [timeout_ms]`
 * waits (5 s by default) for the gadget to ask for a connection. In
 * `net_text`, every `${turn}` becomes the turn of the gadget's last
 * voice.begin or say, so a script can answer a turn whose prefix is random
 * at every boot (contract §2.12). */
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include "gadget_console.h"
#include "gadget_hal.h"
#include "gadget_util.h"
#include "sim_display.h"
#include "sim_hal.h"
#include "sim_internal.h"

#define MAX_LINES 4096
#define MAX_OPS 8192
#define DEFAULT_TIMEOUT_MS 5000u
#define BOOT_TIMEOUT_MS 10000u
#define LINE_BYTES (GADGET_CONSOLE_LINE_MAX + 64u) /* "console " + the longest console line */

static struct {
  const char *path;
  char *lines[MAX_LINES];
  int n, pc;            /* pc: index of the current line */
  bool started;         /* the current blocking command has its deadline */
  uint64_t deadline;
  char *ops[MAX_OPS];   /* every text frame op seen, in order */
  int n_ops, cursor;
  bool pressed;
  int16_t px, py;
  char turn[GADGET_TURN_MAX + 1];  /* "" until the gadget sends voice.begin or say */
} S;

void sim_script_record(const char *op) {
  if (S.n_ops < MAX_OPS) S.ops[S.n_ops++] = strdup(op);
}

void sim_script_note_turn(const char *turn) { snprintf(S.turn, sizeof S.turn, "%s", turn); }

static int fail(const char *fmt, ...) GADGET_PRINTF(1, 2);
static int fail(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  fprintf(stderr, "gadget-sim: %s:%d: ", S.path, S.pc + 1);
  vfprintf(stderr, fmt, ap);
  fputc('\n', stderr);
  va_end(ap);
  return -1;
}

int sim_script_load(const char *path, unsigned boot) {
  S.path = path;
  FILE *f = fopen(path, "r");
  if (f == NULL) {
    fprintf(stderr, "gadget-sim: cannot open script %s\n", path);
    return -1;
  }
  char buf[LINE_BYTES];
  while (S.n < MAX_LINES && fgets(buf, sizeof buf, f) != NULL) {
    size_t len = strlen(buf);
    while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r')) buf[--len] = '\0';
    S.lines[S.n++] = strdup(buf);
  }
  fclose(f);
  S.pc = 0;
  if (boot > 0) {
    char want[32];
    snprintf(want, sizeof want, "boot %u", boot);
    int found = -1;
    for (int i = 0; i < S.n; i++) {
      if (strncmp(S.lines[i], want, strlen(want)) == 0 &&
          (S.lines[i][strlen(want)] == '\0' || S.lines[i][strlen(want)] == ' ')) {
        found = i;
      }
    }
    if (found < 0) {
      fprintf(stderr, "gadget-sim: %s has no \"%s\" line to resume at\n", path, want);
      return -1;
    }
    S.pc = found + 1;
  }
  return 0;
}

/* ---- model fields ----------------------------------------------------------- */

static const char *const SCREENS[] = {"boot", "setup", "offline", "idle", "listening", "thinking",
                                      "speaking", "reply", "ask", "card", "image", "update"};
static const char *const MAUS[] = {"none", "idle", "listening", "thinking", "working", "speaking",
                                   "sleeping", "curious", "notifying", "alerting"};
static const char *const PAIR[] = {"unpaired", "code_stored", "connecting", "paired", "error"};
static const char *const SETUP[] = {"need_wifi", "need_code", "pairing", "host_not_found", "bad_code", "device_limit"};
static const char *const OFFLINE[] = {"wifi_connecting", "wifi_failed", "host_lookup", "host_unreachable",
                                      "in_use_elsewhere", "protocol"};
static const char *const UPDATE[] = {"receiving", "verifying", "restarting"};

/* The field's current value as text, or NULL for an unknown field. */
static const char *field(const char *name, char *num, size_t cap) {
  const ui_model_t *m = core_ui_model();
#define NUM(v) (snprintf(num, cap, "%lu", (unsigned long)(v)), num)
#define BOOL(v) ((v) ? "true" : "false")
  if (strcmp(name, "screen") == 0) return SCREENS[m->screen];
  if (strcmp(name, "maus") == 0) return MAUS[m->maus];
  if (strcmp(name, "pair") == 0) return PAIR[core_pair_state()];
  if (strcmp(name, "speak_level") == 0) return NUM(m->speak_level);
  if (strcmp(name, "bot_name") == 0) return m->bot_name;
  if (strcmp(name, "host_name") == 0) return m->host_name;
  if (strcmp(name, "thinking.heard") == 0) return m->thinking.heard;
  if (strcmp(name, "thinking.working") == 0) return m->thinking.working;
  if (strcmp(name, "reply.text") == 0) return m->reply.text;
  if (strcmp(name, "reply.final") == 0) return BOOL(m->reply.final);
  if (strcmp(name, "reply.failed") == 0) return BOOL(m->reply.failed);
  if (strcmp(name, "reply.reason") == 0) return m->reply.reason;
  if (strcmp(name, "ask.title") == 0) return m->ask.title;
  if (strcmp(name, "ask.body") == 0) return m->ask.body;
  if (strcmp(name, "ask.n_options") == 0) return NUM(m->ask.n_options);
  if (strcmp(name, "ask.answerable") == 0) return BOOL(m->ask.answerable);
  if (strcmp(name, "card.title") == 0) return m->card.title;
  if (strcmp(name, "card.body") == 0) return m->card.body;
  if (strcmp(name, "image.w") == 0) return NUM(m->image.w);
  if (strcmp(name, "image.h") == 0) return NUM(m->image.h);
  if (strcmp(name, "toast.visible") == 0) return BOOL(m->toast.visible);
  if (strcmp(name, "toast.text") == 0) return m->toast.text;
  if (strcmp(name, "setup.step") == 0) return SETUP[m->setup.step];
  if (strcmp(name, "offline.reason") == 0) return OFFLINE[m->offline.reason];
  if (strcmp(name, "update.pct") == 0) return NUM(m->update.pct);
  if (strcmp(name, "update.phase") == 0) return UPDATE[m->update.phase];
  if (strcmp(name, "battery.pct") == 0) return NUM(m->battery.pct);
  return NULL;
#undef NUM
#undef BOOL
}

/* ---- helpers ------------------------------------------------------------------ */

static bool all_digits(const char *s) {
  if (*s == '\0') return false;
  for (; *s; s++) {
    if (*s < '0' || *s > '9') return false;
  }
  return true;
}

static void input(gadget_input_type_t type, int16_t x, int16_t y) {
  gadget_event_t ev = {.type = GADGET_EV_INPUT};
  ev.u.input.type = type;
  ev.u.input.x = x;
  ev.u.input.y = y;
  sim_post_event(&ev);
}

/* Blocking commands: true while still waiting (and not timed out). */
static bool waiting(uint64_t now, uint32_t timeout_ms) {
  if (!S.started) {
    S.started = true;
    S.deadline = now + timeout_ms;
  }
  return now < S.deadline;
}

static void next_line(void) {
  S.pc++;
  S.started = false;
}

/* src with every ${turn} replaced by the last noted turn: 0 on success,
 * -1 when no turn was noted yet, -2 when the result does not fit. */
static int expand_turn(const char *src, char *dst, size_t cap) {
  static const char VAR[] = "${turn}";
  size_t n = 0, tl = strlen(S.turn);
  while (*src != '\0') {
    if (strncmp(src, VAR, sizeof VAR - 1) == 0) {
      if (tl == 0) return -1;
      if (n + tl >= cap) return -2;
      memcpy(dst + n, S.turn, tl);
      n += tl;
      src += sizeof VAR - 1;
    } else {
      if (n + 1 >= cap) return -2;
      dst[n++] = *src++;
    }
  }
  dst[n] = '\0';
  return 0;
}

/* ---- the step ------------------------------------------------------------------ */

int sim_script_step(uint64_t now) {
  /* skip blank lines and comments */
  while (S.pc < S.n) {
    const char *p = S.lines[S.pc];
    while (*p == ' ' || *p == '\t') p++;
    if (*p != '\0' && *p != '#') break;
    S.pc++;
  }
  if (S.pc >= S.n) return 1;

  char line[LINE_BYTES];
  snprintf(line, sizeof line, "%s", S.lines[S.pc]);
  char *save = NULL;
  char *cmd = strtok_r(line, " \t", &save);
  char *rest = save; /* the raw remainder (console, net_text, model values) */
  while (rest && (*rest == ' ' || *rest == '\t')) rest++;

  if (strcmp(cmd, "wait") == 0) {
    char *ms = strtok_r(NULL, " \t", &save);
    if (ms == NULL || !all_digits(ms)) return fail("wait needs milliseconds");
    if (waiting(now, (uint32_t)strtoul(ms, NULL, 10))) return 0;
    next_line();
    return 0;
  }
  if (strcmp(cmd, "expect") == 0) {
    char *op = strtok_r(NULL, " \t", &save);
    char *t = strtok_r(NULL, " \t", &save);
    if (op == NULL || (t != NULL && !all_digits(t))) return fail("expect needs <op> [timeout_ms]");
    for (int i = S.cursor; i < S.n_ops; i++) {
      if (strcmp(S.ops[i], op) == 0) {
        S.cursor = i + 1;
        next_line();
        return 0;
      }
    }
    if (waiting(now, t ? (uint32_t)strtoul(t, NULL, 10) : DEFAULT_TIMEOUT_MS)) return 0;
    return fail("expect %s: no such frame in time", op);
  }
  if (strcmp(cmd, "model") == 0) {
    char *name = strtok_r(NULL, " \t", &save);
    char *value = save;
    while (value && (*value == ' ' || *value == '\t')) value++;
    if (name == NULL || value == NULL) return fail("model needs <field> <value> [timeout_ms]");
    char want[1024];
    snprintf(want, sizeof want, "%s", value);
    uint32_t timeout = DEFAULT_TIMEOUT_MS;
    char *last = strrchr(want, ' ');
    if (last != NULL && all_digits(last + 1)) {
      timeout = (uint32_t)strtoul(last + 1, NULL, 10);
      *last = '\0';
    }
    char num[24];
    const char *got = field(name, num, sizeof num);
    if (got == NULL) return fail("model: unknown field %s", name);
    bool hit = want[0] == '~' ? strstr(got, want + 1) != NULL : strcmp(got, want) == 0;
    if (hit) {
      next_line();
      return 0;
    }
    if (waiting(now, timeout)) return 0;
    return fail("model %s is \"%s\"", name, got);
  }
  if (strcmp(cmd, "boot") == 0) {
    char *n = strtok_r(NULL, " \t", &save);
    char *t = strtok_r(NULL, " \t", &save);
    if (n == NULL || !all_digits(n)) return fail("boot needs <n> [timeout_ms]");
    if (strtoul(n, NULL, 10) <= g_sim.boot) return fail("boot %s: already at or past that boot", n);
    if (waiting(now, t ? (uint32_t)strtoul(t, NULL, 10) : BOOT_TIMEOUT_MS)) return 0; /* the restart re-execs */
    return fail("boot %s: the simulator did not restart in time", n);
  }

  /* acting commands: one per loop iteration */
  if (strcmp(cmd, "touch") == 0) {
    char *x = strtok_r(NULL, " \t", &save), *y = strtok_r(NULL, " \t", &save);
    if (x == NULL || y == NULL) return fail("touch needs <x> <y>");
    S.px = (int16_t)atoi(x);
    S.py = (int16_t)atoi(y);
    input(S.pressed ? GADGET_IN_TOUCH_MOVE : GADGET_IN_TOUCH_DOWN, S.px, S.py);
    sim_display_touch(true, S.px, S.py);
    S.pressed = true;
  } else if (strcmp(cmd, "release") == 0) {
    input(GADGET_IN_TOUCH_UP, S.px, S.py);
    sim_display_touch(false, S.px, S.py);
    S.pressed = false;
  } else if (strcmp(cmd, "swipe") == 0) {
    static const char *const DIRS[] = {"up", "down", "left", "right"};
    char *d = strtok_r(NULL, " \t", &save);
    int dir = -1;
    for (int i = 0; d && i < 4; i++) {
      if (strcmp(d, DIRS[i]) == 0) dir = i;
    }
    if (dir < 0) return fail("swipe needs up, down, left or right");
    gadget_event_t ev = {.type = GADGET_EV_INPUT};
    ev.u.input.type = GADGET_IN_SWIPE;
    ev.u.input.dir = (gadget_swipe_dir_t)dir;
    sim_post_event(&ev);
  } else if (strcmp(cmd, "talk_down") == 0) {
    input(GADGET_IN_TALK_DOWN, 0, 0);
  } else if (strcmp(cmd, "talk_up") == 0) {
    input(GADGET_IN_TALK_UP, 0, 0);
  } else if (strcmp(cmd, "cancel") == 0) {
    input(GADGET_IN_CANCEL_DOWN, 0, 0);
    input(GADGET_IN_CANCEL_UP, 0, 0);
  } else if (strcmp(cmd, "console") == 0) {
    gadget_event_t ev = {.type = GADGET_EV_CONSOLE_LINE};
    ev.u.console.line = rest ? rest : "";
    sim_post_event(&ev);
  } else if (strcmp(cmd, "snapshot") == 0) {
    char *name = strtok_r(NULL, " \t", &save);
    if (name == NULL) return fail("snapshot needs <name>");
    int r = sim_display_snapshot(name);
    if (r == -1) fprintf(stderr, "skip snapshot %s\n", name);
    else if (r == 0) return fail("snapshot %s differs (see %s_err.png)", name, name);
    else if (r == 2) return fail("snapshot %s has no reference image", name);
  } else if (strncmp(cmd, "net_", 4) == 0) {
    if (!sim_net_scripted()) return fail("%s needs --host script", cmd);
    bool ok = false;
    if (strcmp(cmd, "net_open") == 0) {
      char *t = strtok_r(NULL, " \t", &save);
      if (t != NULL && !all_digits(t)) return fail("net_open needs [timeout_ms]");
      /* blocks until the gadget has asked for a connection */
      if (!sim_net_script_open()) {
        if (waiting(now, t ? (uint32_t)strtoul(t, NULL, 10) : DEFAULT_TIMEOUT_MS)) return 0;
        return fail("net_open: the gadget did not connect in time");
      }
      ok = true;
    } else if (strcmp(cmd, "net_text") == 0) {
      static char json[GADGET_TEXT_FRAME_MAX + 1];
      int e = rest != NULL ? expand_turn(rest, json, sizeof json) : -2;
      if (e == -1) return fail("net_text: no turn yet");
      if (e != 0) return fail("net_text needs <json> of at most 16 KiB");
      ok = sim_net_script_text(json);
    } else if (strcmp(cmd, "net_binary") == 0) {
      static uint8_t bin[GADGET_BINARY_FRAME_MAX];
      size_t len = 0;
      char *hex = strtok_r(NULL, " \t", &save);
      if (hex == NULL || gadget_hex_decode(hex, bin, sizeof bin, &len) != GADGET_OK) {
        return fail("net_binary needs lowercase hex");
      }
      ok = sim_net_script_binary(bin, len);
    } else if (strcmp(cmd, "net_close") == 0) {
      char *code = strtok_r(NULL, " \t", &save);
      ok = sim_net_script_close(code ? (uint16_t)atoi(code) : 1006);
    } else {
      return fail("unknown command %s", cmd);
    }
    if (!ok) return fail("%s: no connection is open", cmd);
  } else if (strcmp(cmd, "battery") == 0) {
    char *pct = strtok_r(NULL, " \t", &save);
    char *ch = strtok_r(NULL, " \t", &save);
    if (pct == NULL || !all_digits(pct)) return fail("battery needs <pct> [charging]");
    sim_battery_set((uint8_t)atoi(pct), ch != NULL && strcmp(ch, "charging") == 0);
  } else {
    return fail("unknown command %s", cmd);
  }
  next_line();
  return 0;
}
```

Create `firmware/ports/sim/main.c`:

```c
/* firmware/ports/sim/main.c */
/* SPDX-License-Identifier: Apache-2.0 */
/* gadget-sim: the same core on the desktop (spec §5.7, contract §2.16).
 * Headless runs advance a virtual clock 10 ms per loop iteration; with a
 * real socket each iteration also waits for real time to catch up, so the
 * virtual clock tracks the host's. Window mode (plan P2b) uses the real clock. */
#include <stdlib.h>
#include <string.h>
#include "cJSON.h"
#include "gadget_core.h"
#include "gadget_hal.h"
#include "gadget_ui.h"
#include "psa/crypto.h"
#include "sim_display.h"
#include "sim_hal.h"
#include "sim_internal.h"

static void on_tap(core_tap_dir_t dir, const char *op, const char *json, size_t len, void *ctx) {
  (void)ctx;
  sim_script_record(op);
  if (dir == CORE_TAP_TX && (strcmp(op, "voice.begin") == 0 || strcmp(op, "say") == 0)) {
    cJSON *o = cJSON_ParseWithLength(json, len);
    const cJSON *turn = cJSON_GetObjectItemCaseSensitive(o, "turn");
    if (cJSON_IsString(turn)) sim_script_note_turn(turn->valuestring); /* the script's ${turn} */
    cJSON_Delete(o);
  }
  if (g_sim.trace) fprintf(stderr, "%s %.*s\n", dir == CORE_TAP_TX ? ">>" : "<<", (int)len, json);
}

static void feed_console(const char *line) {
  gadget_event_t ev = {.type = GADGET_EV_CONSOLE_LINE};
  ev.u.console.line = line;
  sim_post_event(&ev);
}

static const sim_audio_backend_t *pick_audio(void) {
  if (g_sim.headless || g_sim.mic_file || g_sim.spk_file) {
    return sim_audio_file_backend(g_sim.mic_file, g_sim.spk_file);
  }
#if defined(GADGET_WITH_SDL)
  return sim_audio_sdl_backend();
#else
  return sim_audio_file_backend(NULL, NULL);
#endif
}

int main(int argc, char **argv) {
  int rc = sim_args_parse(argc, argv, &g_sim);
  if (rc == 1) return 0;
  if (rc != 0) return 2;
  char fw[GADGET_VERSION_MAX + 1];
  if (sim_storage_open(g_sim.state_dir) != 0 || sim_ota_open(g_sim.state_dir, fw, sizeof fw) != 0) return 3;
  if (psa_crypto_init() != PSA_SUCCESS) {
    fprintf(stderr, "gadget-sim: psa_crypto_init failed\n");
    return 3;
  }
  const sim_audio_backend_t *audio = pick_audio();
  if (audio == NULL) return 3;
  sim_audio_use(audio);
  sim_display_opts_t dopts = {.headless = g_sim.headless, .zoom = g_sim.zoom, .snapshot_dir = g_sim.snapshot_dir};
  if (sim_display_init(g_sim.board, &dopts) != 0) return 3;

  core_config_t cfg = {.board = g_sim.board, .fw_version = fw, .prng_seed = g_sim.seed,
                       .fail_probation = g_sim.fail_probation, .default_name = g_sim.name,
                       .probation_ms = g_sim.probation_ms};
  core_set_tap(on_tap, NULL);
  if (core_init(&cfg) != GADGET_OK) {
    fprintf(stderr, "gadget-sim: core_init failed\n");
    return 3;
  }
  sim_wifi_start();
  if (g_sim.host != NULL) {
    char line[128];
    snprintf(line, sizeof line, "host %s", g_sim.host);
    feed_console(line);
  }
  if (g_sim.pair != NULL && g_sim.boot == 0) {
    char line[64];
    snprintf(line, sizeof line, "pair %s", g_sim.pair);
    feed_console(line);
  }
  if (ui_init(g_sim.board, g_sim.seed) != GADGET_OK) return 3;
  if (g_sim.script != NULL && sim_script_load(g_sim.script, g_sim.boot) != 0) return 1;
  if (!g_sim.headless) fprintf(stderr, "gadget-sim: %s on the real clock; type console commands here\n", g_sim.board->id);

  bool paced = !g_sim.headless || (g_sim.host != NULL && !sim_net_scripted());
  uint64_t t0 = hal_now_ms(), vt = 0;
  for (;;) {
    vt += 10;
    if (paced) {
      /* wait (servicing the socket) until real time reaches this tick */
      for (;;) {
        uint64_t real = hal_now_ms() - t0;
        if (real >= vt) break;
        sim_net_poll((uint32_t)(vt - real));
      }
    } else {
      sim_net_poll(0);
    }
    uint64_t now = g_sim.headless ? vt : hal_now_ms() - t0 + 10;
    if (g_sim.headless) sim_display_advance(10);
    else sim_display_poll();
    sim_mdns_poll();
    sim_console_poll();
    sim_audio_pump(now);
    sim_events_deliver();
    core_tick(now);
    ui_render(core_ui_model());
    ui_tick(now);
    if (g_sim.script != NULL) {
      int s = sim_script_step(now);
      if (s > 0) {
        fprintf(stderr, "gadget-sim: script passed\n");
        break;
      }
      if (s < 0) {
        sim_display_deinit();
        return 1;
      }
    }
    if (sim_display_quit_requested()) break;
  }
  ui_deinit();
  sim_display_deinit();
  core_deinit();
  return 0;
}
```

Create `firmware/ports/sim/CMakeLists.txt`:

```cmake
# firmware/ports/sim/CMakeLists.txt
# SPDX-License-Identifier: Apache-2.0
# gadget-sim: the desktop simulator. P2a builds it without LVGL (null display,
# ui_stub.c); plan P2b adds sim_display_lvgl.c, sim_sdl.c and sim_audio_sdl.c.
add_executable(gadget-sim
  main.c
  sim_args.c
  sim_events.c
  sim_storage.c
  sim_system.c
  sim_wifi.c
  sim_battery.c
  sim_console.c
  sim_audio.c
  sim_audio_file.c
  sim_mdns.c
  sim_ota.c
  sim_ws.c
  sim_net_script.c
  sim_script.c
  sim_display_null.c
  ui_stub.c
)
target_include_directories(gadget-sim PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(gadget-sim PRIVATE gadget_core)
# -std=c11 hides POSIX (poll, getaddrinfo, clock_gettime, strdup) on Linux.
target_compile_definitions(gadget-sim PRIVATE _POSIX_C_SOURCE=200809L GADGET_SIM_VERSION="${GADGET_SIM_VERSION}")
if(APPLE)
  target_compile_definitions(gadget-sim PRIVATE _DARWIN_C_SOURCE)
endif()
find_package(Threads REQUIRED)
target_link_libraries(gadget-sim PRIVATE Threads::Threads)
gadget_warnings(gadget-sim)
```

Change `firmware/CMakeLists.txt` exactly as this diff shows (`-` lines go, `+` lines come; the rest is context):

```diff
--- a/firmware/CMakeLists.txt
+++ b/firmware/CMakeLists.txt
@@ -30,6 +30,10 @@ include(cmake/deps.cmake)
 
 add_subdirectory(core)
 
+if(GADGET_BUILD_SIM)
+  add_subdirectory(ports/sim)
+endif()
+
 if(GADGET_BUILD_TESTS)
   enable_testing()
   add_subdirectory(tests)
```

- [ ] **Step 5: Build it and run it**

Run: `cmake -S firmware -B build/host && cmake --build build/host -j10 && ctest --test-dir build/host --output-on-failure -L "unit|vectors"`
Expected: no compiler warnings, then `100% tests passed, 0 tests failed out of 14` (the core suites, unchanged).

Run: `./build/host/ports/sim/gadget-sim --version; ./build/host/ports/sim/gadget-sim --board nope; echo "exit $?"`
Expected: `gadget-sim 0.0.0-dev`, then `gadget-sim: --board must be amoled-175c, amoled-175, lcd-154 or devkit` and `exit 2`.

- [ ] **Step 6: Commit**

```bash
git add firmware/CMakeLists.txt firmware/ports/sim
git commit -m "firmware: headless simulator with every HAL backend and a scripted network" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 15b: Scripted simulator runs

**Files:**
- Modify: `firmware/tests/CMakeLists.txt`
- Test: `firmware/tests/run_sim_script.cmake`, `firmware/tests/scripts/sim_handshake.txt`, `sim_voice.txt`, `sim_ask.txt`, `sim_ota.txt`, `sim_rollback.txt`, `sim_reboot_rollback.txt` (CTest `sim.<name>`, label `unit`)

**Interfaces:**
- Consumes: `gadget-sim` and its script grammar (Task 15a).
- Produces: the test helper `gadget_sim_script(name board [ARGS "<flags>"] [EXPECT "<regex on stdout>"])` and the six `sim.*` tests. Between them they reach every screen a turn has (Listening, Thinking, Speaking, Reply), an ask, a card, an image, a post toast, the Update screen, an OTA restart and both rollbacks, all on the scripted network.

- [ ] **Step 1: Write the runner and the scripts**

Each script plays the host through `net_*` commands. The runner deletes the state folder, runs the script with `--headless --host script`, fails on a non-zero exit, and can require a regular expression on stdout. That is how `sim_ota` checks the new `fw` in `@omb status` after the restart.

Create `firmware/tests/run_sim_script.cmake`:

```cmake
# firmware/tests/run_sim_script.cmake
# SPDX-License-Identifier: Apache-2.0
# Runs one headless simulator script from a fresh state folder:
#   cmake -DSIM=<gadget-sim> -DBOARD=<id> -DSCRIPT=<file> -DSTATE=<dir>
#         [-DARGS="<more sim flags>"] [-DEXPECT=<regex stdout must match>] -P run_sim_script.cmake
file(REMOVE_RECURSE "${STATE}")
separate_arguments(extra UNIX_COMMAND "${ARGS}")
execute_process(
  COMMAND "${SIM}" --board "${BOARD}" --headless --host script --state-dir "${STATE}" --script "${SCRIPT}" ${extra}
  INPUT_FILE /dev/null
  OUTPUT_VARIABLE out
  ERROR_VARIABLE err
  RESULT_VARIABLE rc
  TIMEOUT 120)
message("${out}")
message("${err}")
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "gadget-sim exited with ${rc}")
endif()
if(EXPECT AND NOT out MATCHES "${EXPECT}")
  message(FATAL_ERROR "stdout does not match ${EXPECT}")
endif()
```

`sim_handshake.txt` waits up to 3 s at its second `net_open`, because the reconnect comes after the 2 s backoff.

Create `firmware/tests/scripts/sim_handshake.txt`:

```text
# A new gadget enrolls with the code given on the command line (--pair).
model screen setup
model maus curious
model setup.step pairing
net_open
expect hello
net_text {"op":"challenge","nonce":"AAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8=","host_id":"000102030405060708090a0b0c0d0e0f","host_name":"Fake MausBot"}
expect prove
model host_name Fake MausBot
net_text {"op":"ready","session":"s_0123456789ab","bot":{"id":"b_fake","name":"Fake Bot"},"settings":{"speak_pushes":false}}
model pair paired
model screen idle
model maus idle
model bot_name Fake Bot
# a dropped session: offline, then back after the 2 s backoff
net_close 1006
model screen offline
model offline.reason host_unreachable
net_open 3000
expect hello
net_text {"op":"challenge","nonce":"AAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8=","host_id":"000102030405060708090a0b0c0d0e0f","host_name":"Fake MausBot"}
expect prove
net_text {"op":"ready","session":"s_0123456789ac","bot":{"id":"b_fake","name":"Fake Bot"}}
model screen idle
console status
```

`sim_voice.txt` answers the typed turn with `${turn}`: a final `reply`, three 20 ms speech frames (kind 2, stream 5, then 320 little-endian samples), `speak.end` and `done`, so the run passes through Speaking to Reply.

Create `firmware/tests/scripts/sim_voice.txt`:

```text
# A voice turn with the null mic, then a typed turn the "host" answers with
# speech (${turn} is the gadget's turn), then a cancelled typed turn.
net_open
expect hello
net_text {"op":"challenge","nonce":"AAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8=","host_id":"000102030405060708090a0b0c0d0e0f","host_name":"Fake MausBot"}
expect prove
net_text {"op":"ready","session":"s_0123456789ab","bot":{"id":"b_fake","name":"Fake Bot"}}
model screen idle
talk_down
wait 400
model screen listening
wait 600
talk_up
expect voice.begin
expect voice.end
model screen thinking
console say "What's on today?"
expect stop
expect say
model thinking.heard What's on today?
model screen thinking
net_text {"op":"reply","turn":"${turn}","text":"Two meetings","final":true}
net_text {"op":"speak.begin","stream":5,"rate":16000,"turn":"${turn}"}
# three 20 ms frames of an 800 Hz square wave at -12 dBFS
net_binary 0205c0e0c0e0c0e0c0e0c0e0c0e0c0e0c0e0c0e0c0e0401f401f401f401f401f401f401f401f401f401fc0e0c0e0c0e0c0e0c0e0c0e0c0e0c0e0c0e0c0e0401f401f401f401f401f401f401f401f401f401fc0e0c0e0c0e0c0e0c0e0c0e0c0e0c0e0c0e0c0e0401f401f401f401f401f401f401f401f401f401fc0e0c0e0c0e0c0e0c0e0c0e0c0e0c0e0c0e0c0e0401f401f401f401f401f401f401f401f401f401fc0e0c0e0c0e0c0e0c0e0c0e0c0e0c0e0c0e0c0e0401f401f401f401f401f401f401f401f401f401fc0e0c0e0c0e0c0e0c0e0c0e0c0e0c0e0c0e0c0e0401f401f401f401f401f401f401f401f401f401fc0e0c0e0c0e0c0e0c0e0c0e0c0e0c0e0c0e0c0e0401f401f401f401f401f401f401f401f401f401fc0e0c0e0c0e0c0e0c0e0c0e0c0e0c0e0c0e0c0e0401f401f401f401f401f401f401f401f401f401fc0e0c0e0c0e0c0e0c0e0c0e0c0e0c0e0c0e0c0e0401f401f401f401f401f401f401f401f401f401fc0e0c0e0c0e0c0e0c0e0c0e0c0e0c0e0c0e0c0e0401f401f401f401f401f401f401f401f401f401fc0e0c0e0c0e0c0e0c0e0c0e0c0e0c0e0c0e0c0e0401f401f401f401f401f401f401f401f401f401fc0e0c0e0c0e0c0e0c0e0c0e0c0e0c0e0c0e0c0e0401f401f401f401f401f401f401f401f401f401fc0e0c0e0c0e0c0e0c0e0c0e0c0e0c0e0c0e0c0e0401f401f401f401f401f401f401f401f401f401fc0e0c0e0c0e0c0e0c0e0c0e0c0e0c0e0c0e0c0e0401f401f401f401f401f401f401f401f401f401fc0e0c0e0c0e0c0e0c0e0c0e0c0e0c0e0c0e0c0e0401f401f401f401f401f401f401f401f401f401fc0e0c0e0c0e0c0e0c0e0c0e0c0e0c0e0c0e0c0e0401f401f401f401f401f401f401f401f401f401f
net_binary 0205c0e0c0e0c0e0c0e0c0e0c0e0c0e0c0e0c0e0c0e0401f401f401f401f401f401f401f401f401f401fc0e0c0e0c0e0c0e0c0e0c0e0c0e0c0e0c0e0c0e0401f401f401f401f401f401f401f401f401f401fc0e0c0e0c0e0c0e0c0e0c0e0c0e0c0e0c0e0c0e0401f401f401f401f401f401f401f401f401f401fc0e0c0e0c0e0c0e0c0e0c0e0c0e0c0e0c0e0c0e0401f401f401f401f401f401f401f401f401f401fc0e0c0e0c0e0c0e0c0e0c0e0c0e0c0e0c0e0c0e0401f401f401f401f401f401f401f401f401f401fc0e0c0e0c0e0c0e0c0e0c0e0c0e0c0e0c0e0c0e0401f401f401f401f401f401f401f401f401f401fc0e0c0e0c0e0c0e0c0e0c0e0c0e0c0e0c0e0c0e0401f401f401f401f401f401f401f401f401f401fc0e0c0e0c0e0c0e0c0e0c0e0c0e0c0e0c0e0c0e0401f401f401f401f401f401f401f401f401f401fc0e0c0e0c0e0c0e0c0e0c0e0c0e0c0e0c0e0c0e0401f401f401f401f401f401f401f401f401f401fc0e0c0e0c0e0c0e0c0e0c0e0c0e0c0e0c0e0c0e0401f401f401f401f401f401f401f401f401f401fc0e0c0e0c0e0c0e0c0e0c0e0c0e0c0e0c0e0c0e0401f401f401f401f401f401f401f401f401f401fc0e0c0e0c0e0c0e0c0e0c0e0c0e0c0e0c0e0c0e0401f401f401f401f401f401f401f401f401f401fc0e0c0e0c0e0c0e0c0e0c0e0c0e0c0e0c0e0c0e0401f401f401f401f401f401f401f401f401f401fc0e0c0e0c0e0c0e0c0e0c0e0c0e0c0e0c0e0c0e0401f401f401f401f401f401f401f401f401f401fc0e0c0e0c0e0c0e0c0e0c0e0c0e0c0e0c0e0c0e0401f401f401f401f401f401f401f401f401f401fc0e0c0e0c0e0c0e0c0e0c0e0c0e0c0e0c0e0c0e0401f401f401f401f401f401f401f401f401f401f
net_binary 0205c0e0c0e0c0e0c0e0c0e0c0e0c0e0c0e0c0e0c0e0401f401f401f401f401f401f401f401f401f401fc0e0c0e0c0e0c0e0c0e0c0e0c0e0c0e0c0e0c0e0401f401f401f401f401f401f401f401f401f401fc0e0c0e0c0e0c0e0c0e0c0e0c0e0c0e0c0e0c0e0401f401f401f401f401f401f401f401f401f401fc0e0c0e0c0e0c0e0c0e0c0e0c0e0c0e0c0e0c0e0401f401f401f401f401f401f401f401f401f401fc0e0c0e0c0e0c0e0c0e0c0e0c0e0c0e0c0e0c0e0401f401f401f401f401f401f401f401f401f401fc0e0c0e0c0e0c0e0c0e0c0e0c0e0c0e0c0e0c0e0401f401f401f401f401f401f401f401f401f401fc0e0c0e0c0e0c0e0c0e0c0e0c0e0c0e0c0e0c0e0401f401f401f401f401f401f401f401f401f401fc0e0c0e0c0e0c0e0c0e0c0e0c0e0c0e0c0e0c0e0401f401f401f401f401f401f401f401f401f401fc0e0c0e0c0e0c0e0c0e0c0e0c0e0c0e0c0e0c0e0401f401f401f401f401f401f401f401f401f401fc0e0c0e0c0e0c0e0c0e0c0e0c0e0c0e0c0e0c0e0401f401f401f401f401f401f401f401f401f401fc0e0c0e0c0e0c0e0c0e0c0e0c0e0c0e0c0e0c0e0401f401f401f401f401f401f401f401f401f401fc0e0c0e0c0e0c0e0c0e0c0e0c0e0c0e0c0e0c0e0401f401f401f401f401f401f401f401f401f401fc0e0c0e0c0e0c0e0c0e0c0e0c0e0c0e0c0e0c0e0401f401f401f401f401f401f401f401f401f401fc0e0c0e0c0e0c0e0c0e0c0e0c0e0c0e0c0e0c0e0401f401f401f401f401f401f401f401f401f401fc0e0c0e0c0e0c0e0c0e0c0e0c0e0c0e0c0e0c0e0401f401f401f401f401f401f401f401f401f401fc0e0c0e0c0e0c0e0c0e0c0e0c0e0c0e0c0e0c0e0401f401f401f401f401f401f401f401f401f401f
net_text {"op":"speak.end","stream":5}
model screen speaking
net_text {"op":"done","turn":"${turn}","outcome":"ok"}
model screen reply
model reply.text Two meetings
model reply.final true
# a typed turn cancelled while it is thinking
console say "And tomorrow?"
expect say
model screen thinking
cancel
expect stop
model screen thinking
```

The coordinates in `sim_ask.txt` come from `ui_layout_ask` for two options on the 466 px boards. Allow is `{76, 337, 152, 52}`, so (152, 363) is its center.

Create `firmware/tests/scripts/sim_ask.txt`:

```text
# A permission ask answered on the touch screen, then a card, a post and an action.
net_open
expect hello
net_text {"op":"challenge","nonce":"AAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8=","host_id":"000102030405060708090a0b0c0d0e0f","host_name":"Fake MausBot"}
expect prove
net_text {"op":"ready","session":"s_0123456789ab","bot":{"id":"b_fake","name":"Fake Bot"}}
net_text {"op":"ask","id":"a_1","kind":"permission","title":"Run a shell command?","body":"ls -la","options":[{"id":"allow","label":"Allow","style":"allow"},{"id":"deny","label":"Deny","style":"deny"}]}
model screen ask
model ask.n_options 2
model ask.answerable true
wait 700
# Allow sits at x 76..227, y 337..388 on the 466 px round screen (ui_layout_ask)
touch 152 363
release
expect answer
net_text {"op":"ask.close","id":"a_1","reason":"answered"}
model screen idle
net_text {"op":"card","id":"c1","title":"Build passed","body":"main is green","ttl_s":2}
model screen card
model card.title Build passed
model screen idle 3000
net_text {"op":"post","id":"p1","bot":{"id":"b_fake","name":"Fake Bot"},"kind":"routine","text":"Morning brief is ready","speak":false}
model toast.visible true
model maus notifying
model toast.text ~Morning brief
net_text {"op":"act","id":"x1","name":"chime","args":{}}
expect act.result
net_text {"op":"image.begin","id":"i1","stream":9,"w":2,"h":1,"ttl_s":0}
net_binary 0309001f00f8
net_text {"op":"image.end","stream":9}
model screen image
model image.w 2
swipe down
model screen idle
```

The OTA scripts carry a real offer for the 16-byte "image" `000102…0f`: sha256 `be45cb26…3a8991`, signed with the test key t1 over `amoled-175c` / `1.1.0` / `16` (computed with `@noble/curves` 2.4.0 and checked with `node:crypto`). The chunk frame is kind 4, stream 1, offset 0, then the 16 bytes.

Create `firmware/tests/scripts/sim_ota.txt`:

```text
# A signed 16-byte "image" over the scripted network, the restart, and the
# confirming ready on the new image (the test checks fw 1.1.0 in @omb status).
net_open
expect hello
net_text {"op":"challenge","nonce":"AAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8=","host_id":"000102030405060708090a0b0c0d0e0f","host_name":"Fake MausBot"}
expect prove
net_text {"op":"ready","session":"s_0123456789ab","bot":{"id":"b_fake","name":"Fake Bot"}}
net_text {"op":"fw.offer","stream":1,"board":"amoled-175c","version":"1.1.0","size":16,"sha256":"be45cb2605bf36bebde684841a28f0fd43c69850a3dce5fedba69928ee3a8991","sig":"MEUCIQDxbmBHJquaXcyreOXXR1v0Vxex/M4PR1/5cwZ3Gn6KqQIgOr9ONFDyaZnyb1f0SZF9FsK5Esvf0ruII1T8obpPBw8=","key_id":"t1"}
expect fw.ready
model screen update
net_binary 040100000000000102030405060708090a0b0c0d0e0f
expect fw.progress
net_text {"op":"fw.commit","stream":1}
model update.phase restarting
boot 1 5000
# the new image: probation until the first ready
net_open
expect hello
net_text {"op":"challenge","nonce":"AAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8=","host_id":"000102030405060708090a0b0c0d0e0f","host_name":"Fake MausBot"}
expect prove
net_text {"op":"ready","session":"s_0123456789ad","bot":{"id":"b_fake","name":"Fake Bot"}}
expect fw.installed
console status
wait 100
```

Create `firmware/tests/scripts/sim_rollback.txt`:

```text
# --fail-probation: the new image ignores its first ready, the 2 s probation
# runs out, and the simulator restarts into the old image (fw 0.0.0-dev).
net_open
expect hello
net_text {"op":"challenge","nonce":"AAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8=","host_id":"000102030405060708090a0b0c0d0e0f","host_name":"Fake MausBot"}
expect prove
net_text {"op":"ready","session":"s_0123456789ab","bot":{"id":"b_fake","name":"Fake Bot"}}
net_text {"op":"fw.offer","stream":1,"board":"amoled-175c","version":"1.1.0","size":16,"sha256":"be45cb2605bf36bebde684841a28f0fd43c69850a3dce5fedba69928ee3a8991","sig":"MEUCIQDxbmBHJquaXcyreOXXR1v0Vxex/M4PR1/5cwZ3Gn6KqQIgOr9ONFDyaZnyb1f0SZF9FsK5Esvf0ruII1T8obpPBw8=","key_id":"t1"}
expect fw.ready
net_binary 040100000000000102030405060708090a0b0c0d0e0f
expect fw.progress
net_text {"op":"fw.commit","stream":1}
boot 1 5000
net_open
expect hello
net_text {"op":"challenge","nonce":"AAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8=","host_id":"000102030405060708090a0b0c0d0e0f","host_name":"Fake MausBot"}
expect prove
net_text {"op":"ready","session":"s_0123456789ad","bot":{"id":"b_fake","name":"Fake Bot"}}
boot 2 10000
console status
wait 100
```

Create `firmware/tests/scripts/sim_reboot_rollback.txt`:

```text
# Any reboot of a new image before its first ready returns to the previous
# image (spec §4.8): here a console reboot during probation, like a crash or
# a power cut on the device.
net_open
expect hello
net_text {"op":"challenge","nonce":"AAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8=","host_id":"000102030405060708090a0b0c0d0e0f","host_name":"Fake MausBot"}
expect prove
net_text {"op":"ready","session":"s_0123456789ab","bot":{"id":"b_fake","name":"Fake Bot"}}
net_text {"op":"fw.offer","stream":1,"board":"amoled-175c","version":"1.1.0","size":16,"sha256":"be45cb2605bf36bebde684841a28f0fd43c69850a3dce5fedba69928ee3a8991","sig":"MEUCIQDxbmBHJquaXcyreOXXR1v0Vxex/M4PR1/5cwZ3Gn6KqQIgOr9ONFDyaZnyb1f0SZF9FsK5Esvf0ruII1T8obpPBw8=","key_id":"t1"}
expect fw.ready
net_binary 040100000000000102030405060708090a0b0c0d0e0f
expect fw.progress
net_text {"op":"fw.commit","stream":1}
boot 1 5000
# 1.1.0 is on probation and has not seen a ready: reboot it
console reboot
boot 2 5000
console status
wait 100
```

Change `firmware/tests/CMakeLists.txt` exactly as this diff shows (`-` lines go, `+` lines come; the rest is context):

```diff
--- a/firmware/tests/CMakeLists.txt
+++ b/firmware/tests/CMakeLists.txt
@@ -40,3 +40,25 @@ target_link_libraries(test_vectors PRIVATE gadget_core unity::framework)
 gadget_warnings(test_vectors)
 add_test(NAME vectors.c COMMAND test_vectors ${GADGET_VECTORS_DIR})
 set_tests_properties(vectors.c PROPERTIES LABELS vectors TIMEOUT 60)
+
+# Headless simulator scripts on the scripted network (label unit).
+if(GADGET_BUILD_SIM)
+  function(gadget_sim_script name board)
+    cmake_parse_arguments(S "" "EXPECT;ARGS" "" ${ARGN})
+    add_test(NAME sim.${name}
+             COMMAND ${CMAKE_COMMAND} -DSIM=$<TARGET_FILE:gadget-sim> -DBOARD=${board}
+                     -DSCRIPT=${CMAKE_CURRENT_SOURCE_DIR}/scripts/sim_${name}.txt
+                     -DSTATE=${CMAKE_CURRENT_BINARY_DIR}/sim-state/${name}
+                     "-DARGS=${S_ARGS}" "-DEXPECT=${S_EXPECT}"
+                     -P ${CMAKE_CURRENT_SOURCE_DIR}/run_sim_script.cmake)
+    set_tests_properties(sim.${name} PROPERTIES LABELS unit TIMEOUT 150)
+  endfunction()
+
+  gadget_sim_script(handshake amoled-175c ARGS "--pair 123456" EXPECT "\"pair\":\"paired\"")
+  gadget_sim_script(voice lcd-154 ARGS "--pair 123456")
+  gadget_sim_script(ask amoled-175c ARGS "--pair 123456")
+  gadget_sim_script(ota amoled-175c ARGS "--pair 123456" EXPECT "\"fw\":\"1\\.1\\.0\"")
+  gadget_sim_script(rollback amoled-175c ARGS "--pair 123456 --fail-probation --probation-ms 2000"
+                    EXPECT "\"fw\":\"0\\.0\\.0-dev\"")
+  gadget_sim_script(reboot_rollback amoled-175c ARGS "--pair 123456" EXPECT "\"fw\":\"0\\.0\\.0-dev\"")
+endif()
```

- [ ] **Step 2: Run them**

These scripts test what Task 15a built, so they pass on their first run. A failure here is a simulator or core bug: fix it in the file that owns it, never by loosening a script.

Run: `cmake -S firmware -B build/host && cmake --build build/host -j10 && ctest --test-dir build/host --output-on-failure -L "unit|vectors"`
Expected: `100% tests passed, 0 tests failed out of 20` (13 `core.*` suites, `vectors.c` and 6 `sim.*` scripts).

Run: `ctest --test-dir build/host -R sim.rollback -V | grep -E '@omb \{"op":"boot"'`
Expected: three boot lines, with `"fw":"0.0.0-dev"`, then `"fw":"1.1.0"`, then `"fw":"0.0.0-dev"`. `build/host/tests/sim-state/rollback/otadata.json` then holds `"active": 0`, `"state": "valid"`, and `"previous": {"slot": 1, "version": "1.1.0"}`.

Run: `ls build/host/tests/sim-state/ota`
Expected: `otadata.json`, `slot1.bin` and `storage.json`, and no `.tmp` file.

- [ ] **Step 3: Commit**

```bash
git add firmware/tests
git commit -m "firmware: scripted simulator runs for every screen, OTA and rollback" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 16: Real WebSocket client and simulator ↔ fake-host scenarios

**Files:**
- Modify: `firmware/cmake/deps.cmake` (wslay), `firmware/ports/sim/CMakeLists.txt` (link `wslay`), `firmware/ports/sim/sim_ws.c` (the real client), `firmware/tests/CMakeLists.txt` (the `e2e.*` tests), `package.json` (the `test:e2e` script, which contract §1.4 assigns to P2a)
- Create: `firmware/tests/e2e/run.ts`
- Test: `firmware/tests/e2e/enroll.txt`, `voice.txt`, `bargein.txt`, `bargein_after_done.txt`, `ask.txt`, `ota.txt`, `rollback.txt` (CTest `e2e.<name>`, label `e2e`)

**Interfaces:**
- Consumes: P1's fake host, `node tools/fake-host/src/main.ts --port 0 --code 123456 …` (contract §4.7). This plan relies on these parts of it:
  - **Events:** `listening {port}`, `enrolled`, `ready`, `rx`/`tx {msg}` (the runner accepts `msg` as an object or as a string), `turn {phase, outcome}`, `answer {id, option}`, `act.result {id, ok}`, `ota {phase, version}`, `ack {cmd, ok}`, `refused`.
  - **Commands:** `drop`, `ask`, `post`, `card`, `act`, `ota {image, version}` and `quit`.
  - **Behavior:** the voice turn sequence of contract §4.7, `--tone-ms`, and `--done-before-speech`. With that flag, `done ok` follows the final reply at once and the test tone comes after it, which is MausBot's usual order. A new turn then gets `speak.stop` for that tone before its first message.
  - It also consumes root `npm ci` (for `ws`).
- Produces:
  - **The real client:** `hal_ws_*` for real hosts. It does a non-blocking `connect` (`FD_CLOEXEC`, `SO_NOSIGPIPE` or `MSG_NOSIGNAL`), then sends `GET /gadget` with `Sec-WebSocket-Protocol: openmausbot-gadget.1`, no `Origin` header and no extensions. It checks the 101 status, `Upgrade` and `Sec-WebSocket-Accept` (base64 of SHA-1 through `psa_hash_compute(PSA_ALG_SHA_1)`) and the subprotocol. Bytes that follow the 101 response go to wslay. The connect and upgrade time out after 5 s, the close handshake after 2 s, and binary messages over 8 KiB are dropped. Exactly one `GADGET_EV_WS_CLOSED` follows every open.
  - **The runner:** `firmware/tests/e2e/run.ts` with the scenario format in its header comment, and `npm run test:e2e`.
  - **The CI target** `-L e2e`.
- Spec §10's simulator end-to-end list maps to scenarios: enroll → `enroll`; voice turn → `voice`; barge-in → `bargein` and `bargein_after_done`; ask/answer, post, card and action → `ask`; OTA → `ota`; forced rollback → `rollback`. The checks are protocol traffic plus `ui_model` state through `model` lines, never pixels.
- Barge-in needs both scenarios, because the turn can end in either of two orders:
  - `bargein` runs the fake host's default order, the speech and then `done`. TALK lands before `done`, so the gadget sends `stop`.
  - `bargein_after_done` runs MausBot's usual order through `--done-before-speech`: spec §6.2 sends `done` on `turn.completed`, before the speech ends. Spec §5.4 says TALK after `done` only stops playback and starts listening. Before this scenario, Task 11's unit test `test_tap_after_done_only_stops_playback` was the only check of that order, and it never ran against P1's fake host.

- [ ] **Step 1: Write the failing scenarios and the runner**

Create `firmware/tests/e2e/run.ts`:

```ts
// firmware/tests/e2e/run.ts
// SPDX-License-Identifier: Apache-2.0
// Simulator <-> fake host end-to-end runner (spec §10). Each scenario file
// drives both sides at the protocol level: the [host] steps talk to
// tools/fake-host over its JSON-lines control (contract §4.7), the [sim]
// lines become a headless gadget-sim script on a real socket, and [check]
// lines inspect the results. No pixels are compared here.
//
//   node firmware/tests/e2e/run.ts --all
//   node firmware/tests/e2e/run.ts [--sim <gadget-sim>] [--fake-host <main.ts>] <scenario.txt>...
//
// Scenario format (one directive per line, '#' comments, ${TMP} = the run's temp dir):
//   board <id>                      gadget-sim --board (default amoled-175c)
//   sim-args <flags...>             extra gadget-sim flags
//   host-args <flags...>            extra fake-host flags
//   mic tone <ms>                   write ${TMP}/mic.wav (300 Hz) and pass --mic-file
//   speaker                         pass --speaker-file ${TMP}/spk.wav
//   ota-image <bytes>               write ${TMP}/ota.bin
//   timeout <ms>                    for the whole scenario (default 60000)
//   [host]
//   await <event> [k=v...] [timeout=<ms>]   wait for a fake-host event; for rx/tx, k is looked up in msg first
//   send <json>                     write one control command and wait for its ok ack
//   [sim]
//   <gadget-sim script lines>
//   [check]
//   wav-min-ms <file> <ms>          the WAV in ${TMP} holds at least that much audio
//   no-event <event> [k=v...]       no such fake-host event happened
import { spawn } from "node:child_process";
import type { ChildProcess } from "node:child_process";
import { mkdtempSync, readFileSync, readdirSync, rmSync, writeFileSync, existsSync, statSync } from "node:fs";
import { tmpdir } from "node:os";
import { dirname, join, resolve } from "node:path";
import { createInterface } from "node:readline";
import { fileURLToPath } from "node:url";

const HERE = dirname(fileURLToPath(import.meta.url));
const REPO = resolve(HERE, "../../..");
const CODE = "123456";

type Ev = Record<string, unknown> & { event: string };
type Scenario = {
  name: string;
  board: string;
  simArgs: string[];
  hostArgs: string[];
  micMs: number;
  speaker: boolean;
  otaBytes: number;
  timeoutMs: number;
  host: string[];
  sim: string[];
  check: string[];
};

function parseScenario(path: string): Scenario {
  const s: Scenario = {
    name: path.split("/").pop()!.replace(/\.txt$/, ""),
    board: "amoled-175c", simArgs: [], hostArgs: [], micMs: 0, speaker: false, otaBytes: 0,
    timeoutMs: 60000, host: [], sim: [], check: [],
  };
  let section = "";
  for (const raw of readFileSync(path, "utf8").split("\n")) {
    const line = raw.trim();
    if (line === "" || line.startsWith("#")) continue;
    if (/^\[(host|sim|check)\]$/.test(line)) {
      section = line.slice(1, -1);
      continue;
    }
    if (section === "host") s.host.push(line);
    else if (section === "sim") s.sim.push(line);
    else if (section === "check") s.check.push(line);
    else {
      const [k, ...rest] = line.split(/\s+/);
      if (k === "board") s.board = rest[0];
      else if (k === "sim-args") s.simArgs.push(...rest);
      else if (k === "host-args") s.hostArgs.push(...rest);
      else if (k === "mic" && rest[0] === "tone") s.micMs = Number(rest[1]);
      else if (k === "speaker") s.speaker = true;
      else if (k === "ota-image") s.otaBytes = Number(rest[0]);
      else if (k === "timeout") s.timeoutMs = Number(rest[0]);
      else throw new Error(`${path}: unknown directive ${k}`);
    }
  }
  return s;
}

function writeTone(path: string, ms: number): void {
  const n = Math.round((16000 * ms) / 1000);
  const b = Buffer.alloc(44 + n * 2);
  b.write("RIFF", 0); b.writeUInt32LE(36 + n * 2, 4); b.write("WAVE", 8); b.write("fmt ", 12);
  b.writeUInt32LE(16, 16); b.writeUInt16LE(1, 20); b.writeUInt16LE(1, 22); b.writeUInt32LE(16000, 24);
  b.writeUInt32LE(32000, 28); b.writeUInt16LE(2, 32); b.writeUInt16LE(16, 34); b.write("data", 36);
  b.writeUInt32LE(n * 2, 40);
  for (let i = 0; i < n; i++) b.writeInt16LE(Math.round(6000 * Math.sin((2 * Math.PI * 300 * i) / 16000)), 44 + 2 * i);
  writeFileSync(path, b);
}

function wavMs(path: string): number {
  const b = readFileSync(path);
  return (b.readUInt32LE(40) / 2 / b.readUInt32LE(24)) * 1000;
}

/* k=v conditions; for rx/tx events k is looked up in msg first. */
function matches(ev: Ev, conds: string[]): boolean {
  for (const c of conds) {
    const eq = c.indexOf("=");
    const key = c.slice(0, eq);
    const want = c.slice(eq + 1);
    let got: unknown;
    if ((ev.event === "rx" || ev.event === "tx") && ev.msg !== undefined) {
      const msg = typeof ev.msg === "string" ? JSON.parse(ev.msg) : ev.msg;
      got = key.split(".").reduce<unknown>((o, k) => (o as Record<string, unknown> | undefined)?.[k], msg);
    }
    if (got === undefined) got = key.split(".").reduce<unknown>((o, k) => (o as Record<string, unknown> | undefined)?.[k], ev);
    if (String(got) !== want) return false;
  }
  return true;
}

class EventLog {
  events: Ev[] = [];
  cursor = 0;
  waiters: Array<() => void> = [];
  push(ev: Ev): void {
    this.events.push(ev);
    for (const w of this.waiters.splice(0)) w();
  }
  /* The first matching event after the previous await matched (earlier arrivals count). */
  async next(event: string, conds: string[], timeoutMs: number, from?: number): Promise<Ev> {
    const deadline = Date.now() + timeoutMs;
    for (;;) {
      for (let i = from ?? this.cursor; i < this.events.length; i++) {
        const ev = this.events[i];
        if (ev.event === event && matches(ev, conds)) {
          if (from === undefined) this.cursor = i + 1;
          return ev;
        }
      }
      const left = deadline - Date.now();
      if (left <= 0) throw new Error(`no ${event} ${conds.join(" ")} within ${timeoutMs} ms`);
      await new Promise<void>((res) => {
        const t = setTimeout(res, left);
        this.waiters.push(() => { clearTimeout(t); res(); });
      });
    }
  }
}

function lines(child: ChildProcess, which: "stdout" | "stderr", sink: string[], onLine?: (l: string) => void): void {
  const rl = createInterface({ input: child[which]! });
  rl.on("line", (l) => {
    sink.push(l);
    if (onLine) onLine(l);
  });
}

async function runScenario(path: string, sim: string, fakeHost: string): Promise<boolean> {
  const s = parseScenario(path);
  const tmp = mkdtempSync(join(tmpdir(), `gadget-e2e-${s.name}-`));
  const sub = (l: string) => l.replaceAll("${TMP}", tmp);
  const hostOut: string[] = [], hostErr: string[] = [], simOut: string[] = [], simErr: string[] = [];
  const log = new EventLog();
  let host: ChildProcess | undefined, gadget: ChildProcess | undefined;
  const t0 = Date.now();
  try {
    if (s.micMs > 0) writeTone(join(tmp, "mic.wav"), s.micMs);
    if (s.otaBytes > 0) writeFileSync(join(tmp, "ota.bin"), Buffer.from(Array.from({ length: s.otaBytes }, (_, i) => (i * 131 + 7) & 0xff)));
    host = spawn(process.execPath, [fakeHost, "--port", "0", "--code", CODE, ...s.hostArgs.map(sub)], { stdio: ["pipe", "pipe", "pipe"] });
    lines(host, "stdout", hostOut, (l) => {
      try { log.push(JSON.parse(l) as Ev); } catch { /* not an event line */ }
    });
    lines(host, "stderr", hostErr);
    const listening = await log.next("listening", [], 15000, 0);
    writeFileSync(join(tmp, "sim.txt"), s.sim.map(sub).join("\n") + "\n");
    const args = ["--board", s.board, "--headless", "--script", join(tmp, "sim.txt"), "--host", `127.0.0.1:${listening.port}`,
      "--pair", CODE, "--state-dir", join(tmp, "state")];
    if (s.micMs > 0) args.push("--mic-file", join(tmp, "mic.wav"));
    if (s.speaker) args.push("--speaker-file", join(tmp, "spk.wav"));
    args.push(...s.simArgs.map(sub));
    gadget = spawn(sim, args, { stdio: ["ignore", "pipe", "pipe"] });
    lines(gadget, "stdout", simOut);
    lines(gadget, "stderr", simErr);
    const simExit = new Promise<number>((res) => gadget!.on("exit", (code) => res(code ?? 1)));

    const hostSteps = (async () => {
      for (const raw of s.host) {
        const line = sub(raw);
        const [verb, ...rest] = line.split(/\s+/);
        if (verb === "await") {
          const timeoutArg = rest.find((r) => r.startsWith("timeout="));
          const conds = rest.slice(1).filter((r) => !r.startsWith("timeout="));
          await log.next(rest[0], conds, timeoutArg ? Number(timeoutArg.slice(8)) : 15000);
        } else if (verb === "send") {
          const json = line.slice(5).trim();
          const cmd = JSON.parse(json).cmd as string;
          const mark = log.events.length;
          host!.stdin!.write(json + "\n");
          const ack = await log.next("ack", [`cmd=${cmd}`], 10000, mark);
          if (ack.ok !== true) throw new Error(`fake host refused ${cmd}: ${String(ack.error)}`);
        } else {
          throw new Error(`unknown host step ${verb}`);
        }
      }
    })();

    const timeout = new Promise<never>((_, rej) => setTimeout(() => rej(new Error(`scenario timed out after ${s.timeoutMs} ms`)), s.timeoutMs));
    const [code] = await Promise.race([Promise.all([simExit, hostSteps]), timeout]);
    if (code !== 0) throw new Error(`gadget-sim exited with ${code}`);
    for (const raw of s.check) {
      const [verb, ...rest] = sub(raw).split(/\s+/);
      if (verb === "wav-min-ms") {
        const file = join(tmp, rest[0]);
        if (!existsSync(file) || statSync(file).size <= 44) throw new Error(`${rest[0]} was not written`);
        const ms = wavMs(file);
        if (ms < Number(rest[1])) throw new Error(`${rest[0]} holds ${ms.toFixed(0)} ms, want >= ${rest[1]}`);
      } else if (verb === "no-event") {
        const hit = log.events.find((ev) => ev.event === rest[0] && matches(ev, rest.slice(1)));
        if (hit) throw new Error(`unexpected ${JSON.stringify(hit)}`);
      } else {
        throw new Error(`unknown check ${verb}`);
      }
    }
    console.log(`PASS ${s.name} (${((Date.now() - t0) / 1000).toFixed(1)} s)`);
    return true;
  } catch (e) {
    console.log(`FAIL ${s.name}: ${(e as Error).message}`);
    console.log("--- gadget-sim stdout ---\n" + simOut.slice(-30).join("\n"));
    console.log("--- gadget-sim stderr ---\n" + simErr.slice(-40).join("\n"));
    console.log("--- fake host events ---\n" + hostOut.slice(-40).map((l) => l.slice(0, 300)).join("\n"));
    console.log("--- fake host stderr ---\n" + hostErr.slice(-20).join("\n"));
    return false;
  } finally {
    gadget?.kill("SIGKILL");
    if (host && host.exitCode === null) {
      host.stdin?.write(JSON.stringify({ cmd: "quit" }) + "\n");
      setTimeout(() => host!.kill("SIGKILL"), 2000).unref();
    }
    rmSync(tmp, { recursive: true, force: true });
  }
}

async function main(): Promise<void> {
  const argv = process.argv.slice(2);
  let sim = join(REPO, "build/host/ports/sim/gadget-sim");
  let fakeHost = join(REPO, "tools/fake-host/src/main.ts");
  const files: string[] = [];
  for (let i = 0; i < argv.length; i++) {
    if (argv[i] === "--sim") sim = resolve(argv[++i]);
    else if (argv[i] === "--fake-host") fakeHost = resolve(argv[++i]);
    else if (argv[i] === "--all") {
      for (const f of readdirSync(HERE).filter((f) => f.endsWith(".txt")).sort()) files.push(join(HERE, f));
    } else files.push(resolve(argv[i]));
  }
  if (files.length === 0) {
    console.error("usage: run.ts [--sim <gadget-sim>] [--fake-host <main.ts>] (--all | <scenario.txt>...)");
    process.exit(2);
  }
  if (!existsSync(sim)) {
    console.error(`no simulator at ${sim}; build it first: cmake -S firmware -B build/host -DGADGET_WITH_LVGL=OFF && cmake --build build/host -j10`);
    process.exit(2);
  }
  let failed = 0;
  for (const f of files) {
    if (!(await runScenario(f, sim, fakeHost))) failed++;
  }
  console.log(`${files.length - failed}/${files.length} scenarios passed`);
  process.exit(failed === 0 ? 0 : 1);
}

await main();
```

Create `firmware/tests/e2e/enroll.txt`:

```text
# Enrollment with the pairing code, then a dropped socket and the reconnect
# (2 s backoff) without the code: the gadget is now known by its key.
[host]
await enrolled
await ready
send {"cmd":"drop"}
await ready timeout=20000
[sim]
expect ready
model pair paired
model screen idle
model bot_name Fake Bot
expect ready 20000
model screen idle
console status
[check]
no-event refused
```

Create `firmware/tests/e2e/voice.txt`:

```text
# A held TALK with a WAV mic: voice.begin, mic frames, voice.end, heard,
# working, cumulative replies, speech into a speaker WAV, done. Then a typed turn.
board lcd-154
mic tone 1500
speaker
[host]
await rx op=voice.begin
await rx op=voice.end
await turn phase=done outcome=ok timeout=20000
await rx op=say
[sim]
expect ready
talk_down
wait 1500
talk_up
expect voice.end
expect heard
model thinking.heard ~calendar
expect reply
expect speak.begin 10000
model screen speaking 3000
expect done 15000
model reply.text ~two meetings
model reply.final true
console say "hello there"
expect say
expect done 15000
[check]
wav-min-ms spk.wav 700
```

Create `firmware/tests/e2e/bargein.txt`:

```text
# Barge-in: TALK during a long spoken reply stops playback, sends stop for the
# old turn (the host answers speak.stop and done stopped), then a new turn starts.
host-args --tone-ms 6000
[host]
await turn phase=speech timeout=15000
await rx op=stop timeout=15000
await tx op=done outcome=stopped
await rx op=voice.begin
[sim]
expect ready
console say "tell me a long story"
expect speak.begin 10000
wait 800
talk_down
expect stop
wait 600
talk_up
expect voice.begin
expect voice.end
```

Create `firmware/tests/e2e/bargein_after_done.txt`:

```text
# Barge-in after done, in MausBot's usual order (--done-before-speech): done ok
# comes right after the final reply and the speech follows it (spec §6.2 sends
# done on turn.completed), so `expect done` passes before `expect speak.begin`.
# TALK during that speech stops playback and starts a new turn without a stop,
# because the old turn already has its done (spec §5.4); the host stops its own
# speech with speak.stop when the new voice.begin arrives.
host-args --done-before-speech --tone-ms 3000
mic tone 600
speaker
[host]
await tx op=speak.stop
[sim]
expect ready
talk_down
wait 600
talk_up
expect voice.end
expect done 15000
expect speak.begin 10000
wait 300
talk_down
expect voice.begin
# stay connected until the host has stopped the old speech
expect speak.stop
[check]
no-event rx op=stop
```

Create `firmware/tests/e2e/ask.txt`:

```text
# A permission ask answered by touch, a post with its toast, a card and the
# built-in chime action.
[host]
await ready
send {"cmd":"ask","id":"a_e2e","kind":"permission","title":"Run a shell command?","body":"ls -la"}
await answer id=a_e2e option=allow
send {"cmd":"post","kind":"routine","text":"Morning brief is ready"}
send {"cmd":"card","id":"c_e2e","title":"Build passed","body":"main is green","ttl_s":0}
send {"cmd":"act","id":"x_e2e","name":"chime"}
await act.result id=x_e2e ok=true
[sim]
expect ready
expect ask
model screen ask
wait 700
# Allow is x 76..227, y 337..388 on the 466 px screen
touch 152 363
release
expect answer
expect ask.close
expect post
model toast.visible true
expect card
model screen card
model card.title Build passed
expect act
expect act.result
```

Create `firmware/tests/e2e/ota.txt`:

```text
# A signed update (test key t1) streamed with flow control, the restart into
# the new image, its first ready, and fw.installed.
ota-image 70000
timeout 90000
[host]
await ready
send {"cmd":"ota","image":"${TMP}/ota.bin","version":"1.1.0"}
await ota phase=committed timeout=30000
await rx op=hello fw=1.1.0 timeout=20000
await ota phase=installed version=1.1.0 timeout=20000
[sim]
expect ready
expect fw.offer
model screen update
expect fw.commit 30000
boot 1 10000
expect ready 15000
expect fw.installed
console status
```

Create `firmware/tests/e2e/rollback.txt`:

```text
# Forced rollback: the new image ignores its first ready (--fail-probation),
# probation (3 s here) runs out, and the gadget comes back on the old image.
ota-image 30000
sim-args --fail-probation --probation-ms 3000
timeout 90000
[host]
await ready
send {"cmd":"ota","image":"${TMP}/ota.bin","version":"1.1.0"}
await ota phase=committed timeout=30000
await rx op=hello fw=1.1.0 timeout=20000
await rx op=hello fw=0.0.0-dev timeout=30000
[sim]
expect ready
expect fw.commit 30000
boot 1 10000
expect ready 15000
boot 2 15000
expect ready 15000
[check]
no-event ota phase=installed
```

Change `firmware/tests/CMakeLists.txt` exactly as this diff shows (`-` lines go, `+` lines come; the rest is context):

```diff
--- a/firmware/tests/CMakeLists.txt
+++ b/firmware/tests/CMakeLists.txt
@@ -62,3 +62,19 @@ if(GADGET_BUILD_SIM)
                     EXPECT "\"fw\":\"0\\.0\\.0-dev\"")
   gadget_sim_script(reboot_rollback amoled-175c ARGS "--pair 123456" EXPECT "\"fw\":\"0\\.0\\.0-dev\"")
 endif()
+
+# Simulator <-> fake host end-to-end scenarios (label e2e). They need Node
+# >= 22.18 on PATH and `npm ci` at the repo root (the fake host imports ws).
+if(GADGET_BUILD_SIM)
+  find_program(GADGET_NODE node)
+  if(GADGET_NODE)
+    foreach(scenario enroll voice bargein bargein_after_done ask ota rollback)
+      add_test(NAME e2e.${scenario}
+               COMMAND ${GADGET_NODE} ${CMAKE_CURRENT_SOURCE_DIR}/e2e/run.ts --sim $<TARGET_FILE:gadget-sim>
+                       ${CMAKE_CURRENT_SOURCE_DIR}/e2e/${scenario}.txt)
+      set_tests_properties(e2e.${scenario} PROPERTIES LABELS e2e TIMEOUT 150)
+    endforeach()
+  else()
+    message(STATUS "node not found: the e2e tests are not registered")
+  endif()
+endif()
```

Change `package.json` exactly as this diff shows (`-` lines go, `+` lines come; the rest is context):

```diff
--- a/package.json
+++ b/package.json
@@ -10,7 +10,8 @@
     "test:protocol": "node --test \"protocol/test/**/*.test.ts\"",
     "fake-host": "node tools/fake-host/src/main.ts",
     "test:fake-host": "node --test \"tools/fake-host/test/**/*.test.ts\"",
-    "test": "npm run test:protocol && npm run test:fake-host"
+    "test": "npm run test:protocol && npm run test:fake-host",
+    "test:e2e": "node firmware/tests/e2e/run.ts --all"
   },
   "devDependencies": { "@noble/curves": "2.4.0", "ws": "8.22.0" }
 }
```

- [ ] **Step 2: Run them to see them fail**

Run: `npm ci && cmake -S firmware -B build/host && cmake --build build/host -j10 && node firmware/tests/e2e/run.ts --all`
Expected: every scenario prints `FAIL <name>: no … within … ms`, with `this build has no WebSocket client; use --host script` in the simulator's stderr, and the run ends `0/7 scenarios passed`.

- [ ] **Step 3: Write the real client**

Change `firmware/cmake/deps.cmake` exactly as this diff shows (`-` lines go, `+` lines come; the rest is context):

```diff
--- a/firmware/cmake/deps.cmake
+++ b/firmware/cmake/deps.cmake
@@ -38,7 +38,15 @@ FetchContent_Declare(mbedtls
   URL_HASH SHA256=${_gadget_mbedtls_sha}
   DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
 
-FetchContent_MakeAvailable(cjson unity mbedtls)
+# wslay 1.1.1 (MIT), the simulator's WebSocket framing. Populate only: its
+# CMakeLists needs CMake < 3.5 compatibility; we compile its five sources.
+FetchContent_Declare(wslay
+  URL https://github.com/tatsuhiro-t/wslay/archive/refs/tags/release-1.1.1.tar.gz
+  URL_HASH SHA256=7b9f4b9df09adaa6e07ec309b68ab376c0db2cfd916613023b52a47adfda224a
+  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
+  SOURCE_SUBDIR _populate_only_)
+
+FetchContent_MakeAvailable(cjson unity mbedtls wslay)
 
 add_library(gadget_mbedcrypto INTERFACE)
 if(TARGET tfpsacrypto)
@@ -53,3 +61,19 @@ set_target_properties(cjson PROPERTIES C_EXTENSIONS OFF)
 if(UNIX AND NOT APPLE)
   target_link_libraries(cjson PUBLIC m)   # cJSON uses fabs/floor; macOS has libm in libSystem
 endif()
+
+set(_wslay_gen ${CMAKE_BINARY_DIR}/wslay_gen)
+file(MAKE_DIRECTORY ${_wslay_gen}/wslay)
+file(WRITE ${_wslay_gen}/wslay/wslayver.h "#ifndef WSLAYVER_H\n#define WSLAYVER_H\n#define WSLAY_VERSION \"1.1.1\"\n#endif\n")
+file(WRITE ${_wslay_gen}/config.h "#define HAVE_ARPA_INET_H 1\n#define HAVE_NETINET_IN_H 1\n")
+add_library(wslay STATIC
+  ${wslay_SOURCE_DIR}/lib/wslay_event.c
+  ${wslay_SOURCE_DIR}/lib/wslay_frame.c
+  ${wslay_SOURCE_DIR}/lib/wslay_net.c
+  ${wslay_SOURCE_DIR}/lib/wslay_queue.c
+  ${wslay_SOURCE_DIR}/lib/wslay_stack.c)
+target_include_directories(wslay PUBLIC ${wslay_SOURCE_DIR}/lib/includes ${_wslay_gen} PRIVATE ${wslay_SOURCE_DIR}/lib)
+target_compile_definitions(wslay PRIVATE HAVE_CONFIG_H _POSIX_C_SOURCE=200809L)
+if(APPLE)
+  target_compile_definitions(wslay PRIVATE _DARWIN_C_SOURCE)
+endif()
```

Change `firmware/ports/sim/CMakeLists.txt` exactly as this diff shows (`-` lines go, `+` lines come; the rest is context):

```diff
--- a/firmware/ports/sim/CMakeLists.txt
+++ b/firmware/ports/sim/CMakeLists.txt
@@ -22,7 +22,7 @@ add_executable(gadget-sim
   ui_stub.c
 )
 target_include_directories(gadget-sim PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
-target_link_libraries(gadget-sim PRIVATE gadget_core)
+target_link_libraries(gadget-sim PRIVATE gadget_core wslay)
 # -std=c11 hides POSIX (poll, getaddrinfo, clock_gettime, strdup) on Linux.
 target_compile_definitions(gadget-sim PRIVATE _POSIX_C_SOURCE=200809L GADGET_SIM_VERSION="${GADGET_SIM_VERSION}")
 if(APPLE)
```

Replace the whole of `firmware/ports/sim/sim_ws.c` with:

```c
/* firmware/ports/sim/sim_ws.c */
/* SPDX-License-Identifier: Apache-2.0 */
/* The simulator's WebSocket HAL (spec §5.7). --host script uses the
 * scripted network (sim_net_script.c). Otherwise: our own non-blocking TCP
 * connect and HTTP/1.1 upgrade (RFC 6455 §4: no Origin header, subprotocol
 * openmausbot-gadget.1, Sec-WebSocket-Accept checked), then wslay 1.1.1 for
 * framing, fragments, ping replies and the close handshake. One connection at
 * a time; exactly one GADGET_EV_WS_CLOSED follows every successful open. */
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <unistd.h>
#include <wslay/wslay.h>
#include "gadget_hal.h"
#include "gadget_util.h"
#include "psa/crypto.h"
#include "sim_hal.h"
#include "sim_internal.h"

#define TAG "ws"
#define CONNECT_TIMEOUT_MS 5000u
#define CLOSE_TIMEOUT_MS 2000u
#define RESPONSE_MAX 4096u
#define WS_GUID "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"

typedef enum { WS_IDLE = 0, WS_CONNECTING, WS_HANDSHAKE, WS_OPEN, WS_CLOSING } ws_state_t;

static struct {
  ws_state_t st;
  int fd;
  uint64_t deadline;
  char key[32];
  char req[512];
  size_t req_len, req_sent;
  char resp[RESPONSE_MAX + 1];
  size_t resp_len;
  uint8_t extra[RESPONSE_MAX]; /* bytes that followed the 101 response */
  size_t extra_len, extra_pos;
  wslay_event_context_ptr ctx;
  uint16_t close_code;
} W = {.fd = -1};

bool sim_net_scripted(void) { return g_sim.host != NULL && strcmp(g_sim.host, "script") == 0; }

static void post_simple(gadget_event_type_t type) {
  gadget_event_t ev = {.type = type};
  sim_post_event(&ev);
}

/* End the connection and report it, once. */
static void finish(uint16_t code) {
  if (W.ctx != NULL) wslay_event_context_free(W.ctx);
  if (W.fd >= 0) close(W.fd);
  W.ctx = NULL;
  W.fd = -1;
  W.st = WS_IDLE;
  gadget_event_t ev = {.type = GADGET_EV_WS_CLOSED};
  ev.u.closed.code = code;
  sim_post_event(&ev);
}

/* ---- wslay callbacks ----------------------------------------------------------- */

static ssize_t on_recv(wslay_event_context_ptr ctx, uint8_t *buf, size_t len, int flags, void *ud) {
  (void)flags;
  (void)ud;
  if (W.extra_pos < W.extra_len) { /* frames that arrived with the upgrade response */
    size_t n = W.extra_len - W.extra_pos < len ? W.extra_len - W.extra_pos : len;
    memcpy(buf, W.extra + W.extra_pos, n);
    W.extra_pos += n;
    return (ssize_t)n;
  }
  ssize_t r;
  while ((r = recv(W.fd, buf, len, 0)) < 0 && errno == EINTR) {
  }
  if (r < 0) {
    wslay_event_set_error(ctx, (errno == EAGAIN || errno == EWOULDBLOCK) ? WSLAY_ERR_WOULDBLOCK
                                                                         : WSLAY_ERR_CALLBACK_FAILURE);
    return -1;
  }
  if (r == 0) {
    wslay_event_set_error(ctx, WSLAY_ERR_CALLBACK_FAILURE);
    return -1;
  }
  return r;
}

static ssize_t on_send(wslay_event_context_ptr ctx, const uint8_t *data, size_t len, int flags, void *ud) {
  (void)flags;
  (void)ud;
  ssize_t r;
#if defined(MSG_NOSIGNAL)
  while ((r = send(W.fd, data, len, MSG_NOSIGNAL)) < 0 && errno == EINTR) {
  }
#else
  while ((r = send(W.fd, data, len, 0)) < 0 && errno == EINTR) {
  }
#endif
  if (r < 0) {
    wslay_event_set_error(ctx, (errno == EAGAIN || errno == EWOULDBLOCK) ? WSLAY_ERR_WOULDBLOCK
                                                                         : WSLAY_ERR_CALLBACK_FAILURE);
    return -1;
  }
  return r;
}

static int on_mask(wslay_event_context_ptr ctx, uint8_t *buf, size_t len, void *ud) {
  (void)ctx;
  (void)ud;
  return hal_crypto_random(buf, len) == GADGET_OK ? 0 : -1;
}

static void on_msg(wslay_event_context_ptr ctx, const struct wslay_event_on_msg_recv_arg *a, void *ud) {
  (void)ctx;
  (void)ud;
  gadget_event_t ev = {.type = GADGET_EV_WS_CONTROL};
  switch (a->opcode) {
    case WSLAY_TEXT_FRAME:
    case WSLAY_BINARY_FRAME:
      if (a->opcode == WSLAY_BINARY_FRAME && a->msg_length > GADGET_BINARY_FRAME_MAX) {
        hal_log(GADGET_LOG_WARN, TAG, "dropped a %zu-byte binary message", a->msg_length);
        break; /* the HAL delivers binary messages of at most 8 KiB */
      }
      ev.type = a->opcode == WSLAY_TEXT_FRAME ? GADGET_EV_WS_TEXT : GADGET_EV_WS_BINARY;
      ev.u.ws.data = a->msg;
      ev.u.ws.len = a->msg_length;
      sim_post_event(&ev);
      break;
    case WSLAY_CONNECTION_CLOSE:
      W.close_code = a->status_code; /* wslay answers the close; finish() reports it */
      break;
    default: /* ping (wslay has queued the pong) or pong: liveness only */
      sim_post_event(&ev);
      break;
  }
}

/* ---- the upgrade -------------------------------------------------------------------- */

static const char *header(const char *name) {
  size_t n = strlen(name);
  for (const char *p = strstr(W.resp, "\r\n"); p != NULL; p = strstr(p + 2, "\r\n")) {
    if (strncasecmp(p + 2, name, n) == 0 && p[2 + n] == ':') {
      const char *v = p + 3 + n;
      while (*v == ' ') v++;
      return v;
    }
  }
  return NULL;
}

static bool header_is(const char *name, const char *want) {
  const char *v = header(name);
  return v != NULL && strncasecmp(v, want, strlen(want)) == 0 &&
         (v[strlen(want)] == '\r' || v[strlen(want)] == ' ');
}

static bool response_ok(void) {
  if (strncmp(W.resp, "HTTP/1.1 101", 12) != 0) return false;
  char text[96];
  snprintf(text, sizeof text, "%s%s", W.key, WS_GUID);
  uint8_t sha1[20];
  size_t n = 0;
  if (psa_hash_compute(PSA_ALG_SHA_1, (const uint8_t *)text, strlen(text), sha1, sizeof sha1, &n) != PSA_SUCCESS) {
    return false;
  }
  char accept[32];
  gadget_b64_encode(accept, sizeof accept, sha1, sizeof sha1);
  return header_is("Upgrade", "websocket") && header_is("Sec-WebSocket-Accept", accept) &&
         header_is("Sec-WebSocket-Protocol", GADGET_SUBPROTOCOL);
}

static void start_ws(void) {
  struct wslay_event_callbacks cbs = {on_recv, on_send, on_mask, NULL, NULL, NULL, on_msg};
  if (wslay_event_context_client_init(&W.ctx, &cbs, NULL) != 0) {
    finish(0);
    return;
  }
  wslay_event_config_set_max_recv_msg_length(W.ctx, GADGET_TEXT_FRAME_MAX);
  W.st = WS_OPEN;
  W.close_code = 0;
  post_simple(GADGET_EV_WS_OPEN);
}

static void handshake_io(short revents) {
  if ((revents & POLLOUT) && W.req_sent < W.req_len) {
    ssize_t r = send(W.fd, W.req + W.req_sent, W.req_len - W.req_sent, 0);
    if (r < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
      finish(0);
      return;
    }
    if (r > 0) W.req_sent += (size_t)r;
  }
  if (!(revents & (POLLIN | POLLHUP | POLLERR))) return;
  ssize_t r = recv(W.fd, W.resp + W.resp_len, RESPONSE_MAX - W.resp_len, 0);
  if (r == 0 || (r < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)) {
    hal_log(GADGET_LOG_WARN, TAG, "the host closed during the upgrade");
    finish(0);
    return;
  }
  if (r < 0) return;
  W.resp_len += (size_t)r;
  W.resp[W.resp_len] = '\0';
  char *end = strstr(W.resp, "\r\n\r\n");
  if (end == NULL) {
    if (W.resp_len >= RESPONSE_MAX) finish(0);
    return;
  }
  size_t head = (size_t)(end - W.resp) + 4;
  W.extra_len = W.resp_len - head;
  W.extra_pos = 0;
  memcpy(W.extra, W.resp + head, W.extra_len);
  W.resp[head] = '\0';
  if (!response_ok()) {
    char first[96];
    snprintf(first, sizeof first, "%.*s", (int)strcspn(W.resp, "\r"), W.resp);
    hal_log(GADGET_LOG_WARN, TAG, "upgrade refused: %s", first);
    finish(0);
    return;
  }
  start_ws();
}

/* ---- the HAL ------------------------------------------------------------------------------ */

gadget_status_t hal_ws_open(const char *host, uint16_t port) {
  if (sim_net_scripted()) return sim_net_script_ws_open();
  if (W.st != WS_IDLE) return GADGET_ERR_BUSY;
  struct addrinfo hints, *res = NULL;
  memset(&hints, 0, sizeof hints);
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  char port_s[12];
  snprintf(port_s, sizeof port_s, "%u", (unsigned)port);
  if (getaddrinfo(host, port_s, &hints, &res) != 0 || res == NULL) {
    hal_log(GADGET_LOG_WARN, TAG, "cannot resolve %s", host);
    return GADGET_ERR_IO;
  }
  int fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
  if (fd < 0) {
    freeaddrinfo(res);
    return GADGET_ERR_IO;
  }
  fcntl(fd, F_SETFD, FD_CLOEXEC); /* never leaks into the restart's execv */
  fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
#if defined(SO_NOSIGPIPE)
  int one = 1;
  setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);
#endif
  int rc = connect(fd, res->ai_addr, res->ai_addrlen);
  freeaddrinfo(res);
  if (rc != 0 && errno != EINPROGRESS) {
    close(fd);
    return GADGET_ERR_IO;
  }
  uint8_t nonce[16];
  hal_crypto_random(nonce, sizeof nonce);
  gadget_b64_encode(W.key, sizeof W.key, nonce, sizeof nonce);
  W.req_len = (size_t)snprintf(W.req, sizeof W.req,
                               "GET " GADGET_WS_PATH " HTTP/1.1\r\nHost: %s:%u\r\nUpgrade: websocket\r\n"
                               "Connection: Upgrade\r\nSec-WebSocket-Key: %s\r\nSec-WebSocket-Version: 13\r\n"
                               "Sec-WebSocket-Protocol: " GADGET_SUBPROTOCOL "\r\n\r\n",
                               host, (unsigned)port, W.key);
  W.req_sent = W.resp_len = W.extra_len = W.extra_pos = 0;
  W.fd = fd;
  W.st = WS_CONNECTING;
  W.deadline = hal_now_ms() + CONNECT_TIMEOUT_MS;
  return GADGET_OK;
}

static gadget_status_t queue_msg(uint8_t opcode, const uint8_t *data, size_t len) {
  if (W.st != WS_OPEN) return GADGET_ERR_BUSY;
  struct wslay_event_msg m = {opcode, data, len};
  if (wslay_event_queue_msg(W.ctx, &m) != 0) return GADGET_ERR_IO;
  wslay_event_send(W.ctx);
  return GADGET_OK;
}

gadget_status_t hal_ws_send_text(const char *data, size_t len) {
  if (sim_net_scripted()) return sim_net_script_ws_send();
  return queue_msg(WSLAY_TEXT_FRAME, (const uint8_t *)data, len);
}

gadget_status_t hal_ws_send_binary(const uint8_t *data, size_t len) {
  if (sim_net_scripted()) return sim_net_script_ws_send();
  return queue_msg(WSLAY_BINARY_FRAME, data, len);
}

void hal_ws_close(uint16_t code) {
  if (sim_net_scripted()) {
    sim_net_script_ws_close(code);
    return;
  }
  if (W.st == WS_OPEN) {
    wslay_event_queue_close(W.ctx, code, NULL, 0);
    wslay_event_send(W.ctx);
    W.st = WS_CLOSING;
    W.close_code = code;
    W.deadline = hal_now_ms() + CLOSE_TIMEOUT_MS;
  } else if (W.st == WS_CONNECTING || W.st == WS_HANDSHAKE) {
    finish(0);
  }
}

void sim_net_poll(uint32_t wait_ms) {
  if (sim_net_scripted() || W.st == WS_IDLE) {
    if (wait_ms) poll(NULL, 0, (int)wait_ms);
    return;
  }
  if ((W.st == WS_CONNECTING || W.st == WS_HANDSHAKE || W.st == WS_CLOSING) && hal_now_ms() >= W.deadline) {
    hal_log(GADGET_LOG_WARN, TAG, W.st == WS_CLOSING ? "close timed out" : "connect timed out");
    finish(W.st == WS_CLOSING ? W.close_code : 0);
    return;
  }
  struct pollfd p = {.fd = W.fd, .events = 0};
  if (W.st == WS_CONNECTING) p.events = POLLOUT;
  else if (W.st == WS_HANDSHAKE) p.events = (short)(POLLIN | (W.req_sent < W.req_len ? POLLOUT : 0));
  else p.events = (short)((wslay_event_want_read(W.ctx) ? POLLIN : 0) | (wslay_event_want_write(W.ctx) ? POLLOUT : 0));
  bool buffered = W.extra_pos < W.extra_len;
  if (poll(&p, 1, buffered ? 0 : (int)wait_ms) < 0 && errno != EINTR) {
    finish(0);
    return;
  }
  if (buffered) p.revents |= POLLIN;
  if (W.st == WS_CONNECTING) {
    if (!(p.revents & (POLLOUT | POLLERR | POLLHUP))) return;
    int err = 0;
    socklen_t elen = sizeof err;
    getsockopt(W.fd, SOL_SOCKET, SO_ERROR, &err, &elen);
    if (err != 0) {
      hal_log(GADGET_LOG_WARN, TAG, "connect failed: %s", strerror(err));
      finish(0);
      return;
    }
    W.st = WS_HANDSHAKE;
    handshake_io(POLLOUT);
    return;
  }
  if (W.st == WS_HANDSHAKE) {
    handshake_io(p.revents);
    return;
  }
  if ((p.revents & (POLLIN | POLLHUP | POLLERR)) && wslay_event_recv(W.ctx) != 0) {
    finish(W.close_code);
    return;
  }
  if ((p.revents & POLLOUT) && wslay_event_send(W.ctx) != 0) {
    finish(W.close_code);
    return;
  }
  if (!wslay_event_want_read(W.ctx) && !wslay_event_want_write(W.ctx)) finish(W.close_code); /* closed both ways */
}
```

- [ ] **Step 4: Run the scenarios and the whole suite**

Run: `cmake --build build/host -j10 && npm run test:e2e`
Expected: `PASS ask`, `PASS bargein`, `PASS bargein_after_done`, `PASS enroll`, `PASS ota`, `PASS rollback`, `PASS voice` (about 25 s in all), then `7/7 scenarios passed`.

Run: `ctest --test-dir build/host --output-on-failure -L "unit|vectors|e2e"`
Expected: `100% tests passed, 0 tests failed out of 27`.

If a scenario fails, the runner prints the last simulator stdout and stderr lines and the fake host's events. Run a single scenario with `node firmware/tests/e2e/run.ts firmware/tests/e2e/voice.txt`, and add `sim-args --trace` to its header to see every frame.

- [ ] **Step 5: Commit**

```bash
git add firmware/cmake/deps.cmake firmware/ports/sim firmware/tests package.json
git commit -m "firmware: simulator WebSocket client (wslay) and sim <-> fake-host scenarios" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 17: CI job `host-c`

**Files:**
- Modify: `.github/workflows/ci.yml` (append the `host-c` job; P1 owns the file and its other jobs, contract §1.6)

**Interfaces:**
- Consumes: the commands of Tasks 1–16 (with 15a and 15b).
- Produces: job `host-c`. Its matrix runs `ubuntu-24.04` and `macos-14` against mbedTLS `3.6.7` and `4.2.0`. Its steps are Node 24, `npm ci`, configure with `-DGADGET_WITH_LVGL=OFF`, build, and `ctest -L "unit|vectors|e2e"`.

- [ ] **Step 1: Append the job**

Append this at the end of the `jobs:` map (two-space indentation, after P1's `fake-host` job). Do not edit P1's jobs.

Change `.github/workflows/ci.yml` exactly as this diff shows (`-` lines go, `+` lines come; the rest is context):

```diff
--- a/.github/workflows/ci.yml
+++ b/.github/workflows/ci.yml
@@ -29,3 +29,29 @@ jobs:
           cache: npm
       - run: npm ci
       - run: npm run test:fake-host
+
+  host-c:
+    # P2a: core, the headless simulator and the C tests on both mbedTLS lines
+    # (3.6.x like ESP-IDF 5.5, 4.x like ESP-IDF 6.0), on Linux and macOS.
+    strategy:
+      fail-fast: false
+      matrix:
+        os: [ubuntu-24.04, macos-14]
+        mbedtls: ["3.6.7", "4.2.0"]
+    runs-on: ${{ matrix.os }}
+    steps:
+      - uses: actions/checkout@v7.0.1
+      - uses: actions/setup-node@v7.0.0
+        with:
+          node-version: 24
+          cache: npm
+      # the e2e label runs firmware/tests/e2e/run.ts, whose fake host imports ws
+      - run: npm ci
+      - name: Configure
+        run: >-
+          cmake -S firmware -B build/host -DCMAKE_BUILD_TYPE=Debug -DGADGET_WITH_LVGL=OFF
+          -DGADGET_MBEDTLS_VERSION=${{ matrix.mbedtls }}
+      - name: Build
+        run: cmake --build build/host -j4
+      - name: Test
+        run: ctest --test-dir build/host --output-on-failure -L "unit|vectors|e2e"
```

- [ ] **Step 2: Check the workflow parses and the job is there**

Run: `ruby -ryaml -e 'puts YAML.load_file(".github/workflows/ci.yml")["jobs"].keys.inspect'`
Expected: `["protocol", "fake-host", "host-c"]` (P1's job ids first). macOS ships Ruby; this needs no install.

- [ ] **Step 3: Commit**

```bash
git add .github/workflows/ci.yml
git commit -m "ci: host-c job for core, the headless simulator and C tests" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 18: Branch-level verification and hand-off

**Files:** none (verification only)

**Interfaces:**
- Consumes: the whole branch.
- Produces: a branch ready for Omkar, and these notes.

- [ ] **Step 1: Build everything from scratch and run every label**

```bash
rm -rf build/host && cmake -S firmware -B build/host -DCMAKE_BUILD_TYPE=Debug -DGADGET_WITH_LVGL=OFF \
  && cmake --build build/host -j10 2>&1 | grep -cE 'warning:|error:' ; \
  ctest --test-dir build/host --output-on-failure -L "unit|vectors|e2e"
```

Expected: `0` (no warning or error lines), then `100% tests passed, 0 tests failed out of 27`. The labels split 19 `unit` (13 `core.*` + 6 `sim.*`), 1 `vectors` and 7 `e2e`.

- [ ] **Step 2: Run the mbedTLS 4.2.0 leg**

Run: `cmake -S firmware -B build/host-mbedtls4 -DCMAKE_BUILD_TYPE=Debug -DGADGET_WITH_LVGL=OFF -DGADGET_MBEDTLS_VERSION=4.2.0 -DCMAKE_EXPORT_COMPILE_COMMANDS=ON && cmake --build build/host-mbedtls4 -j10 && ctest --test-dir build/host-mbedtls4 --output-on-failure -L "unit|vectors|e2e"`
Expected: `100% tests passed, 0 tests failed out of 27`.

- [ ] **Step 3: Run the sanitizers over everything, the simulator included**

Run: `cmake -S firmware -B build/asan -DCMAKE_BUILD_TYPE=Debug -DGADGET_WITH_LVGL=OFF -DGADGET_SANITIZE=ON && cmake --build build/asan -j10 && ctest --test-dir build/asan --output-on-failure -L "unit|vectors|e2e"`
Expected: `100% tests passed, 0 tests failed out of 27`, and no `AddressSanitizer` or `runtime error` text in the output.

- [ ] **Step 4: Run the gnu23 check on core (ESP-IDF 6's default standard), against both mbedTLS header sets**

The same check as Task 14, Step 4:

```bash
cmake -S firmware -B build/host-mbedtls4 -DCMAKE_BUILD_TYPE=Debug -DGADGET_WITH_LVGL=OFF \
  -DGADGET_MBEDTLS_VERSION=4.2.0 -DCMAKE_EXPORT_COMPILE_COMMANDS=ON > /dev/null
printf '%s\n' -Ifirmware/core/include -Ibuild/host/_deps/cjson-src -Ibuild/host/_deps/mbedtls-src/include \
  > build/host/gnu23.rsp
node -e '
const cc = require(require("node:path").resolve(process.argv[1]));
const e = cc.find((x) => x.file.endsWith("core/src/crypto_psa.c"));
console.log((e.command ?? e.arguments.join(" ")).split(/\s+/).filter((a) => a.startsWith("-I")).join("\n"));
' build/host-mbedtls4/compile_commands.json > build/host-mbedtls4/gnu23.rsp
for rsp in build/host/gnu23.rsp build/host-mbedtls4/gnu23.rsp; do
  for f in firmware/core/src/*.c; do
    cc -std=gnu23 -Wall -Wextra -Wpedantic -Werror -fsyntax-only -DGADGET_TEST_KEYS=1 "@$rsp" "$f" || echo "FAIL $f"
  done; echo "gnu23 done ($rsp)"
done
```

Expected: `gnu23 done (build/host/gnu23.rsp)`, then `gnu23 done (build/host-mbedtls4/gnu23.rsp)`, and no `FAIL` line.

- [ ] **Step 5: Check P1's own checks still pass, and that the tree is clean**

Run: `npm run test:protocol && npm run test:fake-host && git status --short -- firmware package.json package-lock.json .github protocol tools keys`
Expected: P1's suites pass and the scoped `git status` prints nothing (`build/` and `node_modules/` are ignored). Untracked `docs/plans/*` files elsewhere in the checkout are expected and are not part of this branch.

- [ ] **Step 6: Stop here**

Do not push, open a PR or merge. Report the branch `p2a-core` and the results above to Omkar. Plan P2b branches `p2b-ui` from this branch. If you worked in the worktree of Task 1, Step 1, remove it now; the branch stays.

**Hand-off notes for the next plans:**

- **P2b:**
  - Replace `ui_stub.c` and `sim_display_null.c` through the seams.
  - `option(GADGET_WITH_LVGL … OFF)` is declared before `include(cmake/deps.cmake)`, and `-DGADGET_WITH_LVGL=ON` already configures cleanly here (it has no effect yet). P2b appends its LVGL/SDL block to `deps.cmake`, adds `add_subdirectory(ui)` right after `add_subdirectory(core)`, and flips the default.
  - `gadget-sim` lists its sources directly in `add_executable`, so P2b's block can filter `sim_display_null.c` and `ui_stub.c` out of the target's `SOURCES`.
  - Draw ask buttons on `m->ask.options[i].rect`.
  - The `snapshot` script command already calls `sim_display_snapshot()`.
  - `net_text` replaces every `${turn}` (contract D27; P2b's plan now spells it `${turn}` too and has closed its deviation 2) with the `turn` of the gadget's last `voice.begin` or `say`, so Thinking, Speaking and Reply can be snapshotted end to end on `--host script`; `sim_voice.txt` shows the sequence. P2b's fixture snapshots stay valid.
  - Regenerate the `setup_bad_code` and `setup_device_limit` goldens on every board. Core now keeps the last challenge's `host_name` in the model after that connection closes (Task 8, `test_setup_keeps_the_challenge_host_name_after_bad_code`), which answers P2b's open question 4: both screens show the host name ("Omkar's computer" in P2b's script) on its own dim line above the device id. Only a new challenge, `host`, `host auto` and `forget` drop it; a new `pair` keeps it.
- **P2c:**
  - Implement every `hal_*` except crypto. Deliver events on the core thread, and call `psa_crypto_init()` before `core_init()`.
  - The ESP-IDF branch of `firmware/core/CMakeLists.txt` lists every core source and the `CONFIG_GADGET_TEST_KEYS` block; correct it if the component manager needs a different `REQUIRES`.
  - Register board-specific actions between `core_init()` and the first `core_tick()`.
- **P2d:**
  - Fill `keys_release.c` once `keys/release-r1.pub.b64` exists.
  - The console error texts and `@omb` field order are pinned in Tasks 6 and 10.
  - The simulator path for `AGENTS.md` is `build/host/ports/sim/gadget-sim`, and the end-to-end command is `npm run test:e2e`.
  - List cJSON 1.7.19 (MIT), mbedTLS (Apache-2.0), Unity 2.7.0 (MIT, test-only) and wslay 1.1.1 (MIT) in `THIRD_PARTY.md`.
  - Document `gadget_event_send` in `AGENTS.md` (P2d Task 14, a "Send an event" paragraph under "Add an action"): the name follows the action-name rule; the data is at most 1 KiB once serialized; it returns `GADGET_ERR_BUSY` without a ready session and queues nothing; and events are informational, so MausBot only lists the last 10 to bots as `recent_events` (Contract deviations, item 9).
  - `host auto` prints its `hosts` line once per run of misses, so the installer asks for an address once; a gadget that was paired before keeps browsing in the background (contract D28).
  - `status` carries `host_name` as soon as a challenge has arrived, so it is there after `bad_code` or `device_limit` on a gadget that was never paired; `host`, `host auto` and `forget` drop it until the next challenge (Task 10).
  - `device_limit` on `status` (contract §2.11, status rule 2; Tasks 8 and 10): `"pair":"error","error":"device_limit"` for the whole retry window, from the first `device_limit` until 120 s after the `pair` command. That includes the moments a retry is connecting, so the installer's and `omb_console.py`'s `device_limit` handling sees it on every poll. When the window closes, the gadget drops the code and a gadget that was never paired reports `"pair":"unpaired"` with no `error`. A `pairAndWait` timeout longer than 120 s therefore ends on `unpaired`, not on `device_limit`. Treat `unpaired` after a `device_limit` notice as the `device_limit` result, not as a timeout.
- **P4b:** `fw.fail busy` also comes back while the person is recording or a turn is in flight. Surface it as "the gadget is busy; try again in a moment" (contract D22).

**Not verifiable without hardware or other plans (and how each is covered):**

- **The build on Linux and GCC:** not run here (no Docker). It is designed for it (`_POSIX_C_SOURCE`, `MSG_NOSIGNAL`, `/proc/self/exe`, libm for cJSON, no clang-only flags), and buffers were sized to keep GCC's `-Wformat-truncation` quiet. The first run of the `host-c` job on `ubuntu-24.04` is the check; fix any GCC-only warning there.
- **P1's real fake host and vectors:** now verified (see "How this plan was verified"). Task 4 and Task 16 stay the interop checks; a mismatch there is a contract question for review, not a quiet firmware change.
- **Live `host auto` discovery:** `sim_mdns.c` compiles and its browse runs (an empty `hosts` line when nothing advertises). Discovery of a real MausBot also needs P3a's TXT `id=` record, which today's companion lacks. Manual check after P3a: turn on Remote access, run `gadget-sim --board lcd-154`, type `pair <code>` and `host auto`, and expect a `hosts` line or a connection within about 6 s.
- **Everything on the device, for P2c's `docs/hardware-checklist.md`** (each item exercises code from this plan):
  - press timing on real touch, so a press under 300 ms is ignored and a hold records;
  - swipe-down detection at 1/8 of the screen height;
  - mic level and the 60 s countdown with a real microphone;
  - the speaker jitter buffer under Wi-Fi jitter, with no gaps at a 1 s buffer;
  - the mouth level following speech on the panel;
  - the chime volume;
  - battery `sense` on the AXP2101 and on lcd-154's ADC;
  - an OTA with `esp_ota_*`, including a power cut mid-write, a power cut during probation, the 5-minute probation rollback, and `fw.installed` after the first ready;
  - `forget` erasing NVS;
  - `log off` silencing ESP-IDF's log while `@omb` lines still print.

---

## Contract deviations

None of these renames or retypes anything in the contract. Each is an addition, or a decision the contract or the spec left open, listed for review.

**Review gate (contract §0 item 2): cleared for items 1–4 and 6–8.** These items change pinned behaviour, the script grammar or a pinned file format, so each needed review before the task that builds it: item 8 before Task 8, item 1 before Task 14, and items 2, 3, 4, 6 and 7 before Task 15a. The cross-plan review folded all seven into `00-interfaces.md` §6: item 1 is D22, item 2 is D23, item 3 is D24, item 4 is D25, item 6 is D26, item 7 is D27 and item 8 is D28. Tasks 8, 14 and 15a therefore start without a stop. If §6 lacks one of these rows when its task begins, stop on that item as contract §0 item 2 says. Items 5 and 9 need no stop: item 5 stays within D3, and contract §2.10 already declares item 9's `gadget_event_send`, byte for byte as Task 1 copies it. If Omkar has declined item 9 when Task 13 begins, build Task 13 without it, as item 9 says.

1. **`fw.fail busy` while talking** (contract D22). On top of "an update already running", the gadget answers `busy` while a recording is live or a turn is in flight (Review Focus 2). P4b's Update flow should treat `busy` as "try again later".
2. **The script `expect <op>`** (contract D23) matches the first such frame since the previous `expect` matched, including frames that crossed before the line was reached. This makes scripts independent of network timing. The contract text ("waits until a frame with that op crosses") reads as "from now on"; P2b's snapshot scripts behave the same either way.
3. **The script `net_open [timeout_ms]`** (contract D24) waits up to `timeout_ms` (default 5 s) for the gadget to ask for a connection, instead of failing at once. Contract §2.16's grammar gives `net_open` no argument; `sim_handshake.txt` uses `net_open 3000` to wait out the 2 s reconnect backoff.
4. **`otadata.json` gains `"booted": true`** (contract D25; the §4.6 format is otherwise unchanged): a pending image that starts twice without confirmation rolls back, as the device's bootloader does.
5. **More `@omb error` lines:** the console prints `@omb {"op":"error","cmd":"say","message":"not connected to MausBot"}` when `say` has no session, `{"op":"error","cmd":"","message":"line too long"}` for an over-long line, and `{"op":"error","cmd":"wifi","message":"could not start Wi-Fi"}` when `hal_wifi_connect` refuses to start (the SSID and password are still stored for the next boot). These are additions in the spirit of contract D3. `scan` never prints an error line: when `hal_wifi_scan` refuses to start, it prints `@omb {"op":"scan","networks":[]}`, the same line as a scan that failed later, so `scan` always ends in exactly one `@omb scan` line as contract §2.11 says. P2d's installer should expect the `wifi` error line and treat it as "Wi-Fi did not start; check the board".
6. **Simulator additions** (contract D26): CTest names `sim.<script>` (label `unit`); a real-clock console mode without `--headless` in builds without SDL; the default `--snapshot-dir firmware/tests/snapshots/<board>`. In the simulator, `host auto` resolves only in real-clock runs; headless runs pass `--host`.
7. **The script's `${turn}`** (contract D27, additive to §2.16 and D4). In `net_text`, every `${turn}` becomes the `turn` of the last `voice.begin` or `say` the gadget sent, as core's tap reports it to `main.c`; before the first one, `net_text` fails with `no turn yet`. Turn ids carry a per-boot random prefix (contract §2.12) that `--seed` does not fix, so without this a `--host script` run could never answer a turn, and Speaking and a normal Reply could not be reached headless, which D4 ("UI snapshots are deterministic without a socket") and spec §5.7 and §10 (headless snapshots of every screen) need. A script without `${turn}` behaves exactly as before. P2b's plan (Contract deviations, item 2) first asked for this as `$turn`; it now spells it `${turn}` and has closed that item. Its fixture snapshots stay valid.
8. **`host auto` on a paired gadget** (contract D28; spec §5.6 against §4.3). Spec §5.6's "waits for `host <address>`" applies when no `host_id` is stored. A paired gadget whose MausBot is not among the services found keeps browsing with the §4.3 backoff, because its stored `host_id` picks its own MausBot as soon as it appears. It prints the `hosts` line only on the first miss of a run of misses (`ready`, `pair` and `host` start a new run), so P2d's installer prompts once.
9. **`gadget_event_send`, so makers can send `event`** (contract §2.10; spec §4.7 and §7). Without it no firmware ever sends `event`, so the `recent_events` that P3a's hub keeps and P4a's `gadget_devices` lists would always be empty. `gadget_actions.h` declares `gadget_status_t gadget_event_send(const char *name, const cJSON *data);` (Task 1 copies it from §2.10), and Task 13 builds it in `actions.c` on `gp_encode_event`. The name follows the action-name rule (`GADGET_ERR_ARG`), and the data is at most 1 KiB once serialized compactly (`GADGET_ERR_LIMIT`). Without a ready session it returns `GADGET_ERR_BUSY` and queues nothing, and the name and size checks come first. Tests: `test_event_send_while_ready_and_busy_otherwise` and `test_event_send_checks_the_name_and_the_data_size` in `test_actions.c`. P2d's Task 14 documents it in `AGENTS.md`, as a "Send an event" paragraph under "Add an action" with a `docs.test.ts` assertion. The contract marks it "adopted only if Omkar accepts it". **If Omkar declines:**
   - delete the declaration from Task 1's `gadget_actions.h`;
   - in Task 13, drop `gadget_event_send`, `EVENT_DATA_MAX` and the two tests (`test_actions` then prints `7 Tests`, and the fail-first link misses only `gadget_action_register`), so core never sends `event` and `gp_encode_event` serves only the codec and its tests;
   - P2d does not document events, and `recent_events` comes out of spec §7, contract §3.15's `GadgetDirectoryEntry` and P4a's Tasks 7 and 10, so no tool advertises a field that cannot fill.

## Deviations recorded during the build

Review of Tasks 1–5 (commit `fix(P2a): address review of tasks 1-5`). Where these differ from the code blocks in Tasks 2, 3 and 5, the repository files are authoritative. From that commit on, `test_proto` prints `13 Tests 0 Failures 0 Ignored` (Task 2 expects 10 at its own commit); `test_util` stays at 11, `test_crypto` at 7 and `test_layout` at 5. CTest counts are unchanged. None of these changes the contract.

1. **JSON depth cap (Task 2, `proto.c`).** `gp_decode` rejects a document nested more than 32 levels deep, the outer object included, with `GADGET_ERR_PARSE` before cJSON parses it (`GP_JSON_DEPTH_MAX`, `depth_ok()`, which skips brackets inside strings and honours escapes). cJSON parses recursively and allows 1000 levels (`CJSON_NESTING_LIMIT`). On a 16 KiB thread stack, the size P2c gives the gadget task, a 640-byte `heard` frame with 300 nested arrays crashed the decoder (SIGBUS) at `-O0` and `-O2`, so one small frame from any host could reboot the gadget. With the cap, depths 300, 999 and 7000 return `GADGET_ERR_PARSE` on that stack. Nothing in the protocol nests deeply, and `gp_decode` already returned `GADGET_ERR_PARSE` for bad input, so the contract is unchanged. Test: `test_decode_rejects_deep_nesting` (1000, 300 and 32 nested arrays are refused, 31 are accepted, brackets inside a string do not count, also after an escaped quote, an escaped backslash ends a string, and `act` args 20 levels deep decode).
2. **Lifetimes are clamped, not refused (Task 2, `proto.c`).** `ask.expires_s`, `card.ttl_s` and `image.begin.ttl_s` accept any non-negative integer up to `UINT32_MAX` and are clamped to 86400 s (`GP_LIFETIME_MAX_S`, `lifetime_field()`). Task 2 refused anything over 86400, a limit that `PROTOCOL.md`, the spec and the contract do not set (the fake host accepts any non-negative integer), so a card with `ttl_s` 86401 or a permission ask with `expires_s` 172800 was dropped whole. Negative and fractional values are still refused. Test: `test_decode_clamps_lifetimes_to_a_day`.
3. **Encoders fail whole (Task 2, `proto.c`).** Every `cJSON_Add*`/`Create*` result in the encoders and `caps_json` is checked: `begin()` returns NULL when it cannot add `op`, each encoder ANDs its adds into `ok`, `attach()`/`push()` delete an item they could not add, `caps_json` returns NULL on any failure, and `finish()` returns `GADGET_ERR_NO_MEM` unless every add succeeded. Before, a failed allocation on the device just left a field out and the encoder still succeeded (a `prove` without `sig`, a `hello` without `caps`). A NULL where a string is required now fails the same way instead of leaving the field out. An action's `params_json` that cJSON cannot parse is still `GADGET_ERR_ARG`; cJSON cannot tell a short heap from bad JSON there. Test: `test_encoders_fail_whole_when_out_of_memory` installs cJSON hooks that fail exactly one allocation (the 1st, 2nd, 3rd … in turn) for every encoder and checks each result is `GADGET_ERR_NO_MEM` or the exact frame; `tearDown` restores the default hooks.
4. **UTF-8 per RFC 3629 (Task 1, `util.c`).** `gadget_utf8_len` counts overlong forms (`E0 80..9F`, `F0 80..8F`), surrogates (`ED A0..BF`) and sequences above U+10FFFF (`F4 90..BF`) as invalid, one per byte, as the contract says; before, each counted as one code point. `test_utf8_len` gains those inputs (3, 3, 4 and 4) and the valid edges U+0800, U+D7FF, U+E000, U+10000 and U+10FFFF.
5. **`hal_crypto_sha256_begin` on a live context (Task 3, `crypto_psa.c`).** `begin` returns `GADGET_ERR_ARG` for a NULL `ctx` and aborts a live operation before starting a new one, instead of leaking it (256 bytes each time on the desktop; a hardware SHA driver on ESP-IDF can hold the accelerator until the operation is aborted). So `ctx` starts zeroed (`hal_sha256_t ctx = {0}`) or comes from `finish` or `abort`, as `test_crypto` and Task 14's static `O.hash` already do. `test_sha256_one_shot_and_multi_part` checks begin, update, begin, update, finish gives the right digest and begin(NULL). LeakSanitizer does not run on macOS arm64, so the leak itself was checked once with `malloc_zone_statistics` (256000 bytes left after 1000 rounds before the fix, 0 after); on Linux an `-DGADGET_SANITIZE=ON` build reports it.
6. **`psa_crypto_init` outside Unity (Task 3, `test_crypto.c`).** `main` checks `psa_crypto_init()` with a plain `if`, prints `psa_crypto_init failed` and returns 3, as `test_vectors.c` does. A `TEST_ASSERT` before `UNITY_BEGIN` and outside `RUN_TEST` would longjmp to an unset frame.
7. **Board table test (Task 5, `test_layout.c`).** `test_board_table_matches_contract` compares every field of every row of `gadget_board_at(i)`, in order, against an expected copy of contract §2.3's table (all 11 columns), checks that `gadget_board_by_id` returns the same row, and that there are exactly four. Before, it spot-checked some fields and missed, for example, `lcd-154`'s `input_mask` and `round`, `devkit`'s `display_name` and three `speaker_rate`s.
8. **`gadget_hal.h` follows the contract (Task 1).** The committed header is contract §2.5 byte for byte. Task 1's copy above was out of date in two comments and now matches: `hal_spk_stop` (silent at once with a codec mute, within one DMA ring, at most 60 ms on the devkit, without one) and `hal_wifi_connect`, which adds a requirement: `hal_wifi_state()` reports `GADGET_WIFI_CONNECTING` (or `CONNECTED`) before `hal_wifi_connect` returns. Task 7's `fake_hal.c` now sets `CONNECTING` there unless it is already `CONNECTED` (so the tests that start from `CONNECTED` keep their connection and `test_wifi_drop_and_return` still starts from `CONNECTING`). Task 15a's `sim_wifi.c` already meets it, since its `hal_wifi_state()` is always `CONNECTED`, and so does P2c's port, whose `pl_wifi_connect()` reports `CONNECTING` before `hal_wifi_connect` returns.

Review of Tasks 6–9 (commit `fix(P2a): address review of tasks 6, 7, 8, 9`). Where these differ from the code blocks in Tasks 6–9, the repository files are authoritative. From that commit on, `test_core_init` prints `9 Tests 0 Failures 0 Ignored` (Task 7 expects 8 at its own commit), `test_session` prints `25` (Task 8 expects 24; Task 10's expected line now says 25) and `test_turns` prints `19` (Task 9 expects 17). Task 12 gains one test, so `test_display` prints `13`. CTest counts are unchanged. Every later diff of `core.c` (Tasks 10–14) now starts 7 lines further down, and so does the fifth hunk of Task 11's `interaction.c` diff, by 3 lines. Their hunk headers were moved to match, and no context line changed. Tasks 10–14 were replayed onto the fixed tree with `patch -p1 -F0`, and every hunk applied at offset 0. The result passed 14 of 14 CTest tests. None of these changes the contract.

9. **A swipe down from raw touch events reaches the turn (Task 9, `interaction.c`).** Ports send only raw touch events (contract §2.5), and `GADGET_IN_SWIPE` comes only from the simulator script. The swipe's own `TOUCH_DOWN` starts a press, so `cancel_action()` only ended that press and returned. A swipe never sent `stop` or cleared a reply, and from Task 12 on it never reached `display_dismiss()`. On amoled-175, which has no CANCEL button, nothing could stop a running turn. In the `GADGET_IN_TOUCH_MOVE` branch, a swipe down now first calls `end_press()` when no recording is live: `if (!I.rec) end_press(); cancel_action();`. A swipe during a live recording still drops it with `voice.drop`. Otherwise `cancel_action()` sends `stop` once, clears the reply or, from Task 12 on, dismisses the image, card or toast. The `cancel_action()` hunks of Tasks 11 and 12 are unchanged; their early return now applies only to a recording or a TALK press. Tests: `test_a_touch_swipe_down_stops_the_turn_and_clears_the_reply` in `test_turns.c` (amoled-175, TOUCH_DOWN, a 110 px TOUCH_MOVE within 300 ms, TOUCH_UP) and Task 12's `test_a_touch_swipe_dismisses_the_image_then_the_card` in `test_display.c`. Both fail on the old branch: `Expected 1 Was 0` (no `stop`) and `Expected 9 Was 10` (the image stays up).
10. **The model holds only the gadget charset (Task 7, `core.c`).** `fold_charset` kept DEL (U+007F) and the C1 controls U+0080–U+009F, which contract §2.1 and §2.8 leave out and the fonts cannot draw. It now writes `?` for both: the ASCII branch maps `0x7F` to `?`, and the two-byte branch keeps only `C3 xx` and `C2 A0..BF`. `test_text_outside_the_charset_is_folded` adds `"a\u007fb\u0085c\u009fd\u00a0e\u00ff"`, which gives `a?b?c?d`, then U+00A0, `e` and U+00FF.
11. **A failed key read keeps the identity (Task 7, `core.c`).** `load_identity` made a new key after any failed `hal_storage_get_blob`, so one transient NVS read error at boot overwrote `dev_key` and forced a re-pair (spec §4.2). Now it makes a new key only when the read returns `GADGET_ERR_NOT_FOUND` or `GADGET_ERR_LIMIT`, when the length is not 32, or when `hal_crypto_pubkey` refuses the scalar. Any other error is logged and returned from `core_init`, and `dev_key` is not written. The fake HAL gains `fake_storage_read_error(key, err)`. Tests: `test_a_failed_key_read_keeps_the_stored_key` (an injected `GADGET_ERR_IO` makes `core_init` return it with no storage write, and the next boot gives `FAKE_RFC_ID`). `test_unusable_key_is_replaced` also checks a 40-byte blob (LIMIT) and an all-zero scalar, and each is replaced with exactly one write.
12. **`done` while still recording ends the turn (Task 9, `interaction.c`).** A turn is in flight from its `voice.begin` (spec §4.4), and the host may end it early, for example with `Unsupported mic rate` or a hub-side failure. Before, a `done` that arrived while the turn was still recording was ignored. The release then sent `voice.end` and stayed on Thinking. `GP_OP_DONE` now also accepts `I.rec` with the recording's turn. It stops the mic through `end_press()` without sending `voice.end`, then makes the same failed or ok model update and sets `reply_until`. Test: `test_done_while_recording_ends_the_turn` (no `voice.end`, no `voice.drop`, no frames after `done`, mic stopped once, and Reply showing the reason).
13. **Only a browse that worked can miss (Task 8, `session.c`).** A browse that failed (`ok` false) or ended because Wi-Fi dropped used to count as "found none". A gadget with a code and no `host_id` then halted in `HALT_NO_HOST` and never browsed again. Now `on_mdns` and `host_not_found` first check `hal_wifi_state()`. Without Wi-Fi, they go to `SS_WAIT_WIFI` without printing `hosts`, and the gadget browses again once Wi-Fi is back. A failed browse is a failed attempt (`schedule_retry`), not a miss. A browse that worked and found none or several still halts or retries as deviation 8 and contract D28 say. The fake HAL gains `fake_mdns_fail()`. Test: `test_host_auto_tries_again_after_a_failed_browse_or_a_wifi_drop`.
14. **`hal_mic_stop` once per press (Task 9, `interaction.c`).** `finish_recording()` and the `end_press()` that follows it both stopped the mic, on release and at the 60 s limit. Contract §2.5 does not say a second stop is harmless. `finish_recording()` no longer stops the mic, and `end_press()` stops it once. The fake HAL gains `fake_mic_stops()`. `test_a_held_talk_records_and_sends` and `test_recording_stops_at_60_s_with_a_countdown` assert one stop.
15. **The console splitter's corner cases (Task 6, `console.c`; a comment only).** `gadget_console_split` follows contract §2.11's `esp_console_split_argv` rules except in two corner cases, and both ports share the splitter. A quote inside a word groups as in a shell: `x"y z"` gives `xy z`, where ESP-IDF keeps that quote literally. An unknown escape keeps its backslash: `a\qb` stays `a\qb`, where ESP-IDF drops the pair. The contract header stays verbatim, so the note sits above the function in `console.c`. P2d's installer and console helper should always quote whole arguments and escape only `\\` and `\"`, which both splitters read the same way. P2d's tests already run `quoteArg` output through the real `gadget_console_split`.

Review of Tasks 10–13 (commit `fix(P2a): address review of tasks 10, 11, 12, 13`). Where these differ from the code blocks in Tasks 10–13, the repository files are authoritative. From that commit on, `test_console` prints `14 Tests 0 Failures 0 Ignored` (Task 10 expects 12), `test_audio` prints `13` (Task 11 expects 12), `test_display` prints `19` (Task 12 expects 13 after the review of Tasks 6–9) and `test_actions` prints `11` (Task 13 expects 9). CTest counts are unchanged. `core_internal.h` gains one line above Task 14's hunk, whose header now reads `@@ -147,6 +147,16 @@`; no context line changed. The result passed 13 of 13 CTest tests on mbedTLS 3.6.7, on 4.2.0 and under ASan + UBSan, and every core source passed the gnu23 check against both header sets. Each new test fails on the previous code, and each new guard was removed once to check that a test catches it. None of these changes the contract.

16. **An ask never takes a press it did not start (Tasks 9 and 12, `interaction.c`, `display.c`).** `interaction_input` handed every input to `display_ask_input` whenever an ask was open, even while TALK or a touch was held or a recording was live. Listening outranks Ask (contract §2.7), so the person never saw that ask. On lcd-154, a TALK_UP after an ask arrived mid-recording was swallowed: no `voice.end`, and the mic streamed on until the 60 s limit. A CANCEL while recording sent `answer` `deny` for an ask the person never saw and did not drop the recording. On amoled-175c, the TOUCH_UP was swallowed the same way, and a finger lifted at 100 ms with an ask arriving in between started a recording at 300 ms with no finger down. Now `interaction_input` offers input to the ask only when no press is held and no recording runs (`I.press == PRESS_NONE && !I.rec`), and `display_ask_input` also requires that the ask is the screen shown (`g_core.model.screen == UI_SCREEN_ASK`), so an ask hidden behind Listening or, from Task 14 on, Update never takes TALK or CANCEL. Tests in `test_display.c`: `test_an_ask_never_takes_a_held_talk_release`, `test_cancel_while_recording_drops_it_and_never_answers`, `test_an_ask_never_takes_a_held_touch_release` and `test_a_touch_lifted_before_300_ms_never_records_under_an_ask`. On the old code they fail with `Expected 1 Was 0` (no `voice.end` or `voice.drop`) and `Expected 0 Was 1` (a `voice.begin`). The screen check has no test before Task 14, since only Update can hide an ask from a gadget with no press held.
17. **The 0.6 s lock starts when an ask appears (Task 12 with deviation 16, `screens.c`).** Spec §5.4 ignores presses in the first 0.6 s after an ask appears. `show_head` started the lock when the ask arrived, so an ask that waited behind Listening appeared with its lock already spent, and a TALK pressed again right after the release answered `allow` at once. `screens_update` now moves `ask.locked_until_ms` to at least `now + GADGET_ASK_LOCK_MS` whenever the screen becomes Ask from another screen. An ask that arrives on an idle screen is unchanged (`show_head` already set the same time). Test: `test_an_ask_shown_after_listening_ignores_presses_for_0_6_s` (TALK 0.1 s after the ask appears sends no `answer` and starts no mic; 0.6 s after, TALK answers `allow`). It fails on the code with deviation 16 alone (`Expected 0 Was 1`).
18. **Exactly one `act.result`, even when it cannot be encoded (Task 13, `actions.c`).** Contract §2.10 says core sends exactly one `act.result` per `act`. When a handler's `data` made the frame larger than 16 KiB, `gp_encode_act_result` returned `GADGET_ERR_LIMIT`, `session_send` returned it, and nothing was sent; out of memory gave `GADGET_ERR_NO_MEM` the same way. MausBot then waited out its 15 s and the tool returned a timeout. `send_result` now logs the failure and sends `{"ok":false,"error":"result too large"}` (or `"out of memory"` for `GADGET_ERR_NO_MEM`) for the same id. Tests in `test_actions.c`: `test_an_oversized_result_still_answers` (a 17000-character string in `data` gives exactly `{"op":"act.result","id":"b1","ok":false,"error":"result too large"}`) and `test_a_result_without_memory_still_answers` (cJSON hooks fail the first allocation after the handler returns; the frame is `{"op":"act.result","id":"m1","ok":false,"error":"out of memory"}`). `tearDown` restores the default cJSON hooks. Both fail on the old code with `Expected 1 Was 0`.
19. **Never play into a live mic (Tasks 9, 11 and 12, `core_internal.h`, `interaction.c`, `audio.c`).** The guards checked only `g_core.f.recording`. In the 300 ms before a held press becomes a recording, the mic already runs and pre-buffers, so a post's chime or speech that started then played on, and `start_recording` never stopped it (19040 speaker samples while the screen showed Listening). `core_t.f` gains `mic_live`, which `publish()` sets to `I.press != PRESS_NONE || I.rec`. `audio_on_msg` (`speak.begin`) and `audio_play_chime` check `mic_live` instead of `recording`, and `start_recording` calls `audio_stop_local()` first. A `speak.begin` for the current turn that arrives during a held press is dropped too, as it already was during a recording: the press is about to replace that turn. `f.recording` still drives Listening and Task 14's `fw.fail busy`. Test: `test_no_playback_into_a_live_mic` in `test_display.c` (lcd-154, a spoken post and its `speak.begin` 100 ms into a held TALK, then 25 frames of 40 ms between mic frames: no sample reaches the speaker; then the same 500 ms in, while recording). It fails on the old code with `Expected 0 Was 19040`. Removing the `speak.begin` guard gives `Expected 0 Was 3200`, and removing the chime guard gives `Expected 0 Was 4800`.
20. **Audio that belongs to no turn leaves the reply alone (Tasks 9 and 11, `audio.c`, `interaction.c`).** Contract §2.8 defines `reply.speak_elapsed_ms` and `reply.speak_total_ms` as the reply's speech stream. `begin_stream` zeroed both for any stream, `audio_tick` overwrote `speak_elapsed_ms`, `speak.end` set `speak_total_ms`, and `interaction_tick` kept a Reply up for 20 s after any `audio_active()`. A post that arrived while a Reply was shown rewrote that reply's speech fields and kept it up for 20 s after the chime. Now `begin_stream` zeroes the fields only for a stream with a turn, `speak.end` and `audio_tick` write them only for the current turn's speech (`reply_speech()`, the same test `f.speaking` uses), and the Reply's idle timer follows `g_core.f.speaking` instead of `audio_active()`. `audio_active()` has no caller left and stays declared. Test: `test_a_post_leaves_the_reply_speech_alone` in `test_audio.c` (after a 1 s spoken reply, a spoken post 5 s later leaves `speak_total_ms` and `speak_elapsed_ms` at 1000, and the Reply goes Idle 20 s after the reply's speech ended). It fails on the old code with `Expected 1000 Was 400`, and with only the timer change undone it fails with `Expected 3 Was 7` (still Reply at 20.1 s).
21. **`scan` always prints one `@omb scan` line (Task 10, `console_cmd.c`).** Contract §2.11 says `scan` runs `hal_wifi_scan` and then prints one `@omb scan` line, and it lists every case that prints an error line. When `hal_wifi_scan()` failed at once, the console printed `{"op":"error","cmd":"scan","message":"scan failed"}` and no scan line, while a scan that failed later printed `{"networks":[]}`. Now a scan that cannot start calls `console_on_scan` with `ok` false and prints `@omb {"op":"scan","networks":[]}`. The `wifi` command's `could not start Wi-Fi` error stays and is now listed in Contract deviations, item 5. The fake HAL gains `fake_wifi_start_fails(on)`, which makes `hal_wifi_connect` and `hal_wifi_scan` return `GADGET_ERR_IO`. Tests in `test_console.c`: `test_a_scan_that_cannot_start_prints_no_networks` (it fails on the old code with the `scan failed` line) and `test_wifi_that_cannot_start_prints_an_error`, which pins the existing line for P2d.

## Self-review

- **Spec coverage:**
  - §5.1 core layout and dependencies: Tasks 1–3, 7, 14.
  - §5.2 HAL, crypto adapter and threading: Tasks 1, 3, 7, 15a.
  - §5.4 interaction: Tasks 9, 11, 12 (presses, swipes, barge-in, approvals on touch and buttons, the 0.6 s lock, the 60 s limit and countdown). Task 16 checks barge-in end to end in both orders: `bargein` (before `done`, so the gadget sends `stop`) and `bargein_after_done` (after `done` with P1's `--done-before-speech`, so playback stops and no `stop` is sent).
  - §5.6 console: Tasks 6, 10.
  - §5.7 headless simulator, every backend, OTA and re-exec: Tasks 15a, 15b, 16.
  - §4.2 identity: Task 7. §4.3 handshake and its reaction table: Task 8.
  - §4.4: Tasks 9, 11, 16. §4.5: Task 12. §4.6: Task 12. §4.7: Tasks 12, 13. §4.8: Task 14. §4.9 vectors: Task 4.
  - §10's firmware-core, protocol and simulator end-to-end rows: Tasks 2–16, including every handshake error reaction in Task 8 (`proto_unsupported` among them).
  - Owned elsewhere:
    - LVGL UI, art, fonts, SDL window mode and snapshot goldens: P2b.
    - ESP-IDF port, boards, partition table, NVS encryption Kconfig and the hardware checklist: P2c.
    - Installer, release, pages, `AGENTS.md`, `NOTICE` and `THIRD_PARTY.md`: P2d.
    - `PROTOCOL.md`, vectors and the fake host: P1.
    - The host side: P3a, P3b, P4a and P4b.
- **Placeholders:** none. Every code step carries the full file or the exact diff from the verified commits. The release key table is intentionally empty (contract §2.13), and P2d fills it in.
- **Type consistency:** the plan was generated from compiled code, so names match across tasks by construction. Every contract §2 header is included verbatim.
- **Review Focus:** each of the five lines names its test, and each test runs in its owning task.
