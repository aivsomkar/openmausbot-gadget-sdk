# P2c — ESP32 port, boards and OTA slot: Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build `firmware/ports/esp32/`: an ESP-IDF v6.0.3 application that implements every HAL group of `gadget_hal.h` (except crypto, which core provides) on the ESP32-S3, for four boards (amoled-175c, amoled-175, lcd-154, devkit), with a shared 16 MB partition table, single-threaded LVGL glue, a CI job that builds all four boards and checks the app size, and a manual hardware checklist.

**Architecture:** One "gadget" FreeRTOS task runs core and the UI exactly like the simulator: every 10 ms it drains one queue of events, calls `core_tick`, `ui_render`, `ui_tick`. Driver work that blocks (Wi-Fi driver events, the WebSocket client, mDNS queries, I2S audio, OTA flash writes, USB console reads) runs on its own tasks and only ever posts deep-copied events into that queue. Every decision that can be written as pure C (Wi-Fi retry policy, WebSocket reassembly, scan merging, mDNS answers, debouncing, battery decoding, OTA queue accounting, panel area rounding, audio ring) lives in `main/logic/pl_*.c`, is compiled into the firmware unchanged, and is unit-tested on the host. Boards differ only in `boards/<id>/` (pin map, glue, driver list, Kconfig defaults) on top of shared drivers in `main/drivers/`.

**Tech Stack:** ESP-IDF v6.0.3 (C, gnu23 default, warnings as errors; kept buildable on v5.5.5), FreeRTOS, managed components `lvgl/lvgl 9.6.0~1`, `espressif/esp_websocket_client ^1.8.0`, `espressif/mdns ^1.14.0`, `espressif/cjson ^1.7.19`, `espressif/esp_codec_dev ^1.6.2`, `espressif/esp_lcd_co5300 ^2.2.0`, `waveshare/esp_lcd_touch_cst9217 ^2.0.0`, `espressif/esp_lcd_touch ^1.2.1`; IDF drivers `i2s_std`, `i2c_master`, `esp_lcd` (ST7789), `ledc`, `adc_oneshot`, `usb_serial_jtag`, `app_update`, `nvs_flash`; host tests with CMake ≥ 3.24 and Unity 2.7.0; GitHub Actions in `espressif/idf:v6.0.3`.

**Spec:** `/Users/omkar/Desktop/openmaus/openmausbot-gadget-sdk/docs/specs/2026-10-04-openmausbot-gadget-design.md` (v1.1, amendments A1–A38 folded in) and the binding interface contract `/Users/omkar/Desktop/openmaus/openmausbot-gadget-sdk/docs/plans/00-interfaces.md` (names, types, paths, ownership; §2.5 HAL, §2.13 OTA, §2.14 storage keys, §2.15 art, §2.17 ESP32 seams, §1.4–§1.6 pins, commands and CI job ids). Executors read both.

## Global Constraints

- Repository `/Users/omkar/Desktop/openmaus/openmausbot-gadget-sdk`, branch `p2c-esp32` created from `p2b-ui` (contract §1.3). No commit to `main`, no push, no PR, no release: publishing belongs to Omkar. End every commit message with the attribution line your session requires.
- This plan touches no OpenMausBot file. Never checkout, stash, reset, edit or fetch in `/Users/omkar/Desktop/openmaus/OpenGrokBot`.
- Original-work rule (spec §11): never open, fetch, quote or cite other gadget SDKs or voice-assistant firmware projects. Pins and init sequences come from the vendors' schematics, datasheets and published driver components (Espressif, Waveshare, X-Powers), used as references. Driver components are fetched at build time under their own licenses and never copied into this repository.
- ESP-IDF **v6.0.3**; CI image `espressif/idf:v6.0.3`; component manifest `idf: ">=6.0.3,<6.1"`; the optional `esp32-idf55` job relaxes it to `>=5.5.5,<6.1` on `espressif/idf:v5.5.5`. ESP project `cmake_minimum_required(VERSION 3.22)`.
- Install on this Mac (contract §1.4, plus EIM's macOS prerequisites, which EIM checks but does not install on POSIX systems): `brew install libgcrypt glib pixman sdl2 libslirp dfu-util ninja && brew tap espressif/eim && brew install eim && eim install -i v6.0.3 -t esp32s3`, then `. ~/.espressif/tools/activate_idf_v6.0.3.sh`. Fallback: `git clone -b v6.0.3 --depth 1 --recursive --shallow-submodules https://github.com/espressif/esp-idf.git ~/esp/esp-idf-v6.0.3 && cd ~/esp/esp-idf-v6.0.3 && ./install.sh esp32s3 && . ./export.sh` (then use `. ~/esp/esp-idf-v6.0.3/export.sh` wherever this plan activates IDF). Python 3.10–3.14 (local 3.14.6).
- **LVGL 9.6.0** (`lvgl/lvgl: "9.6.0~1"`), configured on the device only through `CONFIG_LV_*` in `sdkconfig.defaults`. That file pins `CONFIG_LV_CONF_SKIP=y` (the simulator's `firmware/ui/lv_conf.h` is on the ui component's include path and must never be read on the device) and both LVGL image caches off, `CONFIG_LV_CACHE_DEF_SIZE=0` and `CONFIG_LV_IMAGE_HEADER_CACHE_DEF_CNT=0` (P2b's `ui_lv_requirements.h` `#error`s otherwise). All three equal LVGL's Kconfig defaults; they are written out so the host test `test_shared_defaults` checks them before any `idf.py build`. **No Waveshare BSP, no `esp_lvgl_port`, no `esp_lvgl_adapter`.** cJSON only from `espressif/cjson`, included as `"cJSON.h"`; never `REQUIRES json`.
- Crypto is core's (PSA only). The port only calls `psa_crypto_init()` at boot, before `core_init()` (contract §2.5).
- Build command (spec §5.1), from `firmware/ports/esp32/`: `idf.py -B build/<board> -D GADGET_BOARD=<board> -D SDKCONFIG=build/<board>/sdkconfig [-D PROJECT_VER=<version>] build`. `PROJECT_VER` defaults to `0.0.0-dev`. The project fails when `GADGET_BOARD` is unset or not a directory under `boards/`, and when `-D GADGET_TEST_KEYS=1` or `-D GADGET_NVS_ENCRYPT=1` is used in the standard `build/<board>` directory (those variants get their own `-B` directory). App image: `build/<board>/openmausbot-gadget.bin`.
- Partition table `partitions/16mb.csv`, identical for all boards and stable across releases: nvs 0x9000/0x6000, otadata 0xF000/0x2000, phy_init 0x11000/0x1000, ota_0 0x20000/0x600000, ota_1 0x620000/0x600000, coredump 0xC20000/0x10000. Every board's `caps.ota.max` = 6291456 = the slot size; CI asserts `app.bin` ≤ 6291456.
- Shared `sdkconfig.defaults` contains `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y` and `# CONFIG_BOOTLOADER_APP_ANTI_ROLLBACK is not set`. Anti-rollback is never enabled. Probation (5 min) is core's; the port only reports `PENDING_VERIFY` and marks valid/invalid.
- Every board's `sdkconfig.defaults` sets `CONFIG_GADGET_BOARD_ID`, `CONFIG_GADGET_ART_PROFILE`, `CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y`, `CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions/16mb.csv"` and `# CONFIG_GADGET_TEST_KEYS is not set`, and never sets the three LVGL pins above (a board file wins over the shared one).
- NVS: namespace `gadget`, keys exactly the `GADGET_KEY_*` names, committed on every write. NVS encryption is opt-in: Kconfig `GADGET_NVS_ENCRYPT` = HMAC scheme with `CONFIG_NVS_SEC_HMAC_EFUSE_KEY_ID=5`; its help text warns about the permanent eFuse burn.
- Audio: codec boards (amoled-175c, amoled-175, lcd-154) run ES8311 + ES7210 duplex at **16 kHz** on one I2S clock; devkit speaker at **24 kHz** on a separate controller. MIC1 only, mono, no echo cancellation in v1.
- amoled-175c PWR is GPIO3, active high (AXP2101 power key mirror; 6 s hold powers off). lcd-154 `board_early_init()` drives BAT_EN (GPIO2) high first; lcd-154 battery is supported (BAT_ADC GPIO1 ×3, CHG_STAT GPIO3 low = charging).
- Console: primary console on USB-Serial-JTAG (`CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y`); CR, LF and CRLF end a line (core's `gadget_linebuf`). devkit uses its USB-C port labelled **USB**, not **UART**.
- Threading (contract §2.1, §2.5): core, UI and every `hal_*` call run on one task; driver tasks never call core. Every `hal_*` returns within a few milliseconds except `hal_ota_finalize` (≤ 2 s).
- C: code compiles with IDF 6's gnu23 and warnings-as-errors and stays valid C11; `/* SPDX-License-Identifier: Apache-2.0 */` (or `#` form) first in every source, script and data file that allows comments.
- Host tests: Unity **2.7.0** (`https://github.com/ThrowTheSwitch/Unity/archive/refs/tags/v2.7.0.tar.gz`, SHA256 `e84eb301ca7967831e68b1728f911e87fa2d345d8ddb64f897bc2f2ee24a321c`), CMake ≥ 3.24, `-Wall -Wextra -Werror`, C11 without extensions.
- CI job ids owned here: `esp32` and `esp32-idf55` only (contract §1.6); no other plan's job is edited.

## Review Focus

These are the five inputs most likely to hurt someone using a board; each has a test in the task that owns the code.

1. **A stale or foreign `sdkconfig`** — building board B in board A's build directory, or with A's sdkconfig, must stop with a clear error instead of producing a mixed image (wrong pins, wrong art, wrong board id in `hello`). A test-key or NVS-encryption build must never land in the standard `build/<board>` directory. Tests: Task 2, Step 8 (the variant-directory guard) and Step 9 (both mismatch cases), and the `esp32` CI job's amoled-175c leg (Task 14), which runs both mismatch cases on every push.
2. **Wi-Fi with a wrong stored password, or the router down, while the installer runs `scan`** — the scan must still list networks (the ESP-IDF driver refuses to scan while a station is connecting), the status must reach `failed` and stay there without flapping, and the board must keep retrying so it recovers when the router returns. Tests: Task 5, `test_scan_while_connecting_aborts_attempt_then_resumes`, `test_three_failures_report_failed_and_keep_retrying`, `test_wrong_password_fails_at_once_and_backs_off`, `test_attempt_timeout_counts_as_failure`.
3. **WebSocket frames bigger than the client's receive buffer, fragmented messages with a ping in between, and oversize messages** — each message reaches core whole and exactly once; an oversize one closes the connection with 1009 instead of being truncated or merged with the next. Tests: Task 6, `test_pl_wsasm.c` (split frame, fragmented with ping, exactly 16 KiB, 8 KiB+1 binary, protocol errors).
4. **A button held at power-on and contact bounce** — the 1.75C's PWR press that switches the board on, or BOOT held, must not produce a phantom CANCEL/TALK; a bouncing press must produce exactly one edge. Tests: Task 9, `test_button_held_at_boot_is_ignored_until_released`, `test_button_debounces_bounce`.
5. **OTA chunks arriving faster than flash erases, replays, gaps, and a chunk that cannot be queued** — back-pressure with `GADGET_ERR_BUSY`, `GADGET_ERR_ARG` for a gap or replay, `GADGET_ERR_LIMIT` past the image size, and the byte accounting rolled back when queueing fails; nothing is written twice or lost. Tests: Task 11, `test_pl_otaq.c` (including `test_unadmit_rolls_back`).

## What was verified while writing this plan

Real hardware and an ESP-IDF installation were not available (ESP-IDF is not installed on this Mac and the Docker daemon is not running), so an actual `idf.py build` was **not** run. Everything else below was run under `/private/tmp`:

- All eleven `main/logic/pl_*.c` modules and the thirteen host tests in this plan were compiled with Apple clang 21 (`-std=c11 -Wall -Wextra -Werror`, CMake 4.3.4, Unity 2.7.0 from the pinned tarball) and pass: 13/13 CTest tests, also with `-fsanitize=address,undefined`, and each module also compiles with `-std=gnu2x -Wpedantic -Wshadow -Wformat=2 -Werror`.
- `tools/check-size.sh`, `tools/check-art-profile.sh` and their test scripts pass; `tools/build-all.sh` was smoke-tested with a fake `idf.py` and fake `nm`.
- Every ESP-side file in this plan (`main/*.c`, `main/drivers/*.c`, all four `boards/*/board.c`) passes `clang -fsyntax-only -std=gnu2x -Wall -Wextra -Werror` (and `-std=gnu17` for the v5.5.5 leg) against the **real** headers of `esp_lcd_co5300` 2.2.0, `esp_lcd_touch_cst9217` 2.0.0, `esp_lcd_touch` 1.2.1, `esp_codec_dev` 1.6.2, `esp_websocket_client` 1.8.0, `mdns` 1.14.0, LVGL 9.6.0 and the contract's C headers, plus stub headers transcribed from the ESP-IDF **v6.0.3** headers fetched from Espressif's repository at that tag (`i2s_std.h`, `i2s_common.h`, `i2c_master.h`, `esp_lcd_io_spi.h`, `esp_lcd_io_i2c.h`, `esp_lcd_panel_ops.h`, `esp_lcd_panel_st7789.h`, `usb_serial_jtag.h`, `usb_serial_jtag_vfs.h`, `esp_ota_ops.h`, `nvs.h`, `esp_wifi.h`, `esp_wifi_types_generic.h`, `adc_oneshot.h`, `adc_cali_scheme.h`, `ledc.h`, `gpio.h`, `esp_flash.h`, `esp_log.h`). Every non-crypto function in `gadget_hal.h` has exactly one definition in `main/`.
- Every `CONFIG_*` symbol in `sdkconfig.defaults` was looked up in the v6.0.3 Kconfig files (bootloader, esptool_py, partition_table, esp_psram, esp_stdio, espcoredump, freertos, mbedtls, nvs_flash, nvs_sec_provider, log), in LVGL 9.6.0's Kconfig and in `esp_websocket_client` 1.8.0's Kconfig.
- Behaviour read from vendor sources: `esp_websocket_client` 1.8.0 posts `WEBSOCKET_EVENT_DATA` for ping, pong and close frames, splits a frame larger than `buffer_size` into pieces (`payload_offset`), dispatches `WEBSOCKET_EVENT_FINISH` exactly once when its task ends, and `esp_websocket_client_destroy()` after that is safe; IDF v6.0.3 `build.cmake` passes build properties to the early-expansion script (so `main/CMakeLists.txt` may read `GADGET_BOARD` there) and reads `DEPENDENCIES_LOCK` inside `project()`; the USB-Serial-JTAG VFS in driver mode drops output after 50 ms when no host reads (no stall on battery); LVGL 9.6 renamed `lv_draw_sw_rgb565_swap` to `lv_draw_rgb565_swap`; ESP-IDF 6's default libc is Picolibc, so the code avoids `flockfile`; `esp_codec_dev` refuses different TX/RX rates on one I2S port (hence 16 kHz duplex).
- The project `CMakeLists.txt` board guard was run with `cmake -P` for `""`, `nope`, `../main` (all fail with the messages quoted in Task 2) and for valid boards (pass).
- The CI jobs parse as YAML (Ruby Psych) and the matrix yields four plain builds plus one `nvs-encrypt` build.

Re-checked after review (2026-10-04, same machine, under `/private/tmp`):

- ESP-IDF v6.0.3's early expansion was simulated in CMake script mode with a stand-in for `tools/cmake/scripts/component_get_requirements.cmake` (its `idf_component_register` macro records the requirements and returns). P2b's finished `firmware/ui/CMakeLists.txt`, which opens with P2b's early-expansion guard (contract §2.17), passes there and reports `REQUIRES lvgl__lvgl;core` and `INCLUDE_DIRS .;art;fonts`; the same file with the guard's six lines removed fails with `CONFIGURE_DEPENDS is invalid for script and find package modes` (re-run 2026-10-05). Task 2 Step 6b's grep prints its four expected lines for P2b's file and only `set(UI_SRCS` for the copy without the guard. `main/CMakeLists.txt` passes the same simulation and the normal pass, with `board.cmake` (its drivers only) and without it (all seven drivers).
- The variant-directory guard of the project file was run with `cmake -P` from inside `build/amoled-175c` (refuses `GADGET_TEST_KEYS=1` and `GADGET_NVS_ENCRYPT=1`, accepts `=0` and none, and refuses through a `/tmp` symlink too) and from `build/ota-a` (accepts). A small CMake project confirmed that a failed configure still caches `-D` values, hence the "delete it" hint.
- The changed C (`pl_mdns`, `pl_power`, `hal_mdns.c`, `hal_audio.c`, `drv_es_codec.c`, `drv_i2s_simplex.c`, `drv_co5300.{h,c}`, `drv_lipo_adc.c`, the four `board.c` with their `board.h`) passes the same header-backed `clang -fsyntax-only` check for every board, as gnu2x and gnu17; all 13 host tests pass, also with `-fsanitize=address,undefined`; `pl_mdns.c` and `pl_power.c` also compile with `-std=c11`/`-std=gnu2x -Wpedantic -Wshadow -Wformat=2 -Werror`.
- `build-all.sh` was run with a fake `idf.py` and `nm`: four boards pass; a standard build whose sdkconfig has `CONFIG_GADGET_TEST_KEYS=y` stops with `build-all: devkit has test keys or NVS encryption on`.
- The appended CI block parses with Ruby's YAML loader and lists the step conditions Task 14 Step 3 expects; every `run:` block passes `bash -n`, and the guard step, run with a fake `idf.py`, passes when the guard fires and exits 1 when it does not.
- The extracted `docs/hardware-checklist.md` passes Task 15 Step 2 (sixteen `ok`) and Task 16 Step 5's SPDX, link and placeholder checks in a scratch git repository; every table row has the right number of columns.
- `idf.py efuse-summary` exists in ESP-IDF v6.0.3 (`tools/idf_py_actions/serial_ext.py` at the tag); `project.cmake` at v6.0.3 prints `Component ${build_component} will be linked with -Wl,--whole-archive` with the `idf::` alias; idf-component-manager 3.1.2 writes the lock with `YAML(typ='safe')`.
- LVGL pins for P2b's `ui_lv_requirements.h` (2026-10-05): LVGL 9.6.0's Kconfig defines `LV_CACHE_DEF_SIZE` and `LV_IMAGE_HEADER_CACHE_DEF_CNT` (menu "Image Settings", `int`, default 0) and `LV_CONF_SKIP` (menu "Build", `bool`, default y) with no `depends on`; `include/lvgl/config/lv_conf_internal.h` maps `CONFIG_LV_CACHE_DEF_SIZE` and `CONFIG_LV_IMAGE_HEADER_CACHE_DEF_CNT` to the macros the header checks and `CONFIG_LV_CONF_SKIP` to `LV_CONF_SKIP`. `test_board_defaults.c`, the shared and the four board `sdkconfig.defaults`, extracted from Task 2, were built with `clang -std=c11 -Wall -Wextra -Werror` against core's `boards.c` and Unity 2.7.0: with no files it prints Step 2's three failures (the first at `test_board_defaults.c:21`), with Step 3's files all three tests pass, and each of these fails it: deleting any of the three LVGL lines from the shared file, `# CONFIG_LV_CONF_SKIP is not set` (in place of the `=y` line or after it), `CONFIG_LV_CACHE_DEF_SIZE=65536`, and a board file that sets any of the three.

## Scope

**In this plan (spec §5.3 and the ESP32 side of §4.2, §4.8, §5.1, §5.2, §5.4, §5.6, §10):** ESP-IDF install, the ESP-IDF project and its guards, shared and per-board Kconfig defaults, the 16 MB partition table, every HAL group except crypto (storage + opt-in encryption, Wi-Fi + scan, WebSocket, mDNS, mic + speaker, input, battery, OTA slot, system/log/console), the four board directories with pin maps from the vendors' schematics, LVGL display and touch glue, the main loop, `check-size.sh`, CI jobs `esp32` and `esp32-idf55`, and `docs/hardware-checklist.md`.

**Owned elsewhere (not built here):**
- Core logic, the HAL/core/UI headers, the console grammar and `@omb` lines, probation timing, the OTA state machine, PSA crypto, key tables (`keys_release.c`, `keys_test.c`), `core/CMakeLists.txt` — **P2a** (P2c may only correct the ESP branch, contract §2.17).
- LVGL screens, Maus art, fonts, `ui_lv_requirements.h`, `lv_conf.h`, `ui/CMakeLists.txt` (including compiling only the board's art profile on ESP, and the ESP-IDF early-expansion guard the file opens with, contract §2.17) — **P2b**.
- Browser installer, `release.yml` (it reuses this plan's build command, `check-size.sh` and asset names), `pages.yml`, `AGENTS.md` (documents `board_api.h` and "add a board"), README, NOTICE, THIRD_PARTY.md, filling `keys_release.c` — **P2d**.
- Protocol text, vectors and the fake host — **P1**. Everything in OpenMausBot — **P3a, P3b, P4a, P4b**.

## Before you start

- [ ] **P2b is merged into the history you branch from.** Run from the repo root:

```bash
cd /Users/omkar/Desktop/openmaus/openmausbot-gadget-sdk
git status --short
git rev-parse --verify p2b-ui
ls firmware/core/src/boards.c firmware/core/CMakeLists.txt firmware/ui/CMakeLists.txt .github/workflows/ci.yml
```

Expected: `git status --short` prints nothing; `git rev-parse` prints a commit hash; `ls` lists the four files. If any is missing, stop: P2c needs P1, P2a and P2b first.

## File Structure

All paths are relative to the repository root. Everything under `firmware/ports/esp32/` and `docs/hardware-checklist.md` is owned by this plan (contract §5.1); `.github/workflows/ci.yml` gets two appended jobs.

```
firmware/ports/esp32/
  CMakeLists.txt              ESP-IDF project: board guard, per-board defaults, PROJECT_VER, lock file per board
  sdkconfig.defaults          shared Kconfig defaults (flash, rollback, PSRAM, console, LVGL from Kconfig only with image caches off, websocket TX lock)
  sdkconfig.nvs-encrypt       opt-in overlay (-D GADGET_NVS_ENCRYPT=1): HMAC NVS encryption, eFuse block 5
  sdkconfig.test-keys         bench-only overlay (-D GADGET_TEST_KEYS=1): trust test key t1 for bench OTA (fake host or MausBot's Update button)
  partitions/16mb.csv         the one partition table
  dependencies.lock.<board>   component-manager lock files, generated by the first build, committed (4 files)
  main/
    CMakeLists.txt            component: sources, optional board.cmake driver list, REQUIRES, WHOLE_ARCHIVE, sdkconfig/board check
    idf_component.yml         managed component pins
    Kconfig.projbuild         GADGET_BOARD_ID, GADGET_ART_PROFILE, GADGET_TEST_KEYS, GADGET_NVS_ENCRYPT
    board_api.h               what each boards/<id>/board.c implements; board_audio_t
    port.h                    port internals: the event queue, init functions of each HAL file
    main.c                    app_main boot order (contract §2.17) and the 10 ms loop on the "gadget" task
    port_events.c             the one queue from driver tasks to the gadget task; port_drain()
    hal_system.c              clock, restart, log, `log off`, @omb console output
    console_usj.c             USB-Serial-JTAG driver + reader task → GADGET_EV_CONSOLE_LINE
    hal_storage.c             NVS namespace "gadget"; GADGET_NVS_ENCRYPT consistency check
    hal_wifi.c                station, scan, retry policy (pl_wifi) driven on the gadget task
    hal_ws.c                  esp_websocket_client behind a worker task; reassembly; exactly one CLOSED per open
    hal_mdns.c                _openmausbot._tcp browse on its own task
    hal_audio.c               mic task + speaker task + 1.5 s speaker ring
    hal_input.c               10 ms polling of TALK/CANCEL and touch → GADGET_EV_INPUT; LVGL pointer state
    hal_battery.c             board battery read, cached for 5 s
    hal_ota.c                 OTA worker task: esp_ota_begin/write/end; slot state; mark valid/invalid
    display.c                 LVGL 9 display (2 internal DMA buffers, partial mode), flush, CO5300 rounder, touch indev
    logic/                    pure C, compiled into the firmware and into host-tests/
      pl_event.{h,c}          deep copy / free of gadget events
      pl_util.{h,c}           NVS key check, ws:// URI builder, UTF-8-safe truncation
      pl_scan.{h,c}           scan merge: drop hidden, dedupe by SSID, strongest first, cap 20
      pl_wifi.{h,c}           Wi-Fi state machine: retries, FAILED after 3, scan arbitration
      pl_wsasm.{h,c}          WebSocket message reassembly with limits
      pl_mdns.{h,c}           mDNS answer → gadget_mdns_host_t
      pl_audio.{h,c}          PCM16 ring, 32→16-bit conversion, software volume
      pl_input.{h,c}          button debounce (ignore a press held at boot), touch down/move/up
      pl_power.{h,c}          AXP2101 register decode, Li-ion voltage → percent
      pl_display.{h,c}        CO5300 even/odd area rounding
      pl_otaq.{h,c}           OTA queue byte accounting
    drivers/                  shared drivers; a board's board.cmake may list the ones it uses (without one, all compile)
      drv_co5300.{h,c}        CO5300 QSPI AMOLED (the board's init table, gap, brightness)
      drv_cst9217.{h,c}       CST9217 touch on the shared I2C bus
      drv_st7789.{h,c}        ST7789 SPI LCD + LEDC backlight
      drv_es_codec.{h,c}      ES8311 + ES7210 on one I2S port via esp_codec_dev, 16 kHz duplex
      drv_i2s_simplex.{h,c}   INMP441 mic (I2S1, 16 kHz) + MAX98357A amp (I2S0, 24 kHz)
      drv_axp2101.{h,c}       AXP2101 fuel gauge reads
      drv_lipo_adc.{h,c}      battery voltage on ADC1 + charger status pin
  boards/<id>/                amoled-175c, amoled-175, lcd-154, devkit
    board.h                   pin map and panel init table (vendor sources; devkit: our wiring)
    board.c                   board_api.h for this board
    board.cmake               optional: BOARD_DRIVER_SRCS for this board (a size optimisation)
    sdkconfig.defaults        board id, art profile, 16 MB flash, partition file, test keys off
  tools/
    check-size.sh             app ≤ 6291456 bytes and rollback lines present (contract §2.17)
    test-check-size.sh        tests for check-size.sh
    check-art-profile.sh      the image links only its board's Maus art profile
    test-check-art-profile.sh tests for check-art-profile.sh (fake nm)
    build-all.sh              build every board (or the ones named) and run both checks
  host-tests/
    CMakeLists.txt            standalone CMake project: Unity + main/logic + core/src/boards.c
    test_partitions.c  test_board_defaults.c  test_pl_event.c  test_pl_util.c  test_pl_scan.c
    test_pl_wifi.c  test_pl_wsasm.c  test_pl_mdns.c  test_pl_audio.c  test_pl_input.c
    test_pl_power.c  test_pl_display.c  test_pl_otaq.c
docs/hardware-checklist.md    manual on-device checks per board
.github/workflows/ci.yml      + jobs esp32 (4 boards + nvs-encrypt variant, host tests, build-all.sh in one checkout) and esp32-idf55
```

Prefixes: `hal_` the contract's HAL, `board_` board glue (contract §2.1), `port_` port internals, `pl_` port logic (pure C, private to this plan), `drv_` drivers (private to this plan).

**Commands used throughout** (from the repository root unless a step says otherwise):

- Host tests: `cmake -S firmware/ports/esp32/host-tests -B build/esp32-host && cmake --build build/esp32-host -j10 && ctest --test-dir build/esp32-host --output-on-failure`
- Activate ESP-IDF in a shell: `. ~/.espressif/tools/activate_idf_v6.0.3.sh`
- Build every board: `firmware/ports/esp32/tools/build-all.sh` (until Task 13 links the UI, run it as `SKIP_ART_CHECK=1 firmware/ports/esp32/tools/build-all.sh`). On success it prints, per board, `== <board>` then `check-size: <board> ok, <n> of 6291456 bytes (<p>%)` (and from Task 13 `check-art-profile: <board> links only <profile>`). On failure it prints the last 60 lines of `firmware/ports/esp32/build/<board>.log` and exits 1.

**If a managed component (a file under `firmware/ports/esp32/managed_components/`) fails to compile only because IDF 6 turns one of its warnings into an error**, do not touch the component and do not disable errors globally. Add these lines at the end of `firmware/ports/esp32/CMakeLists.txt`, after `project(...)`, naming that component (`lvgl__lvgl` shown):

```cmake
idf_component_get_property(lvgl_lib lvgl__lvgl COMPONENT_LIB)
target_compile_options(${lvgl_lib} PRIVATE -Wno-error)
```

**If core or ui sources fail to compile under IDF 6**, stop and report it: those files belong to P2a and P2b (contract §5.1); this plan may only correct the `if(ESP_PLATFORM)` branches of their `CMakeLists.txt` (contract §2.17). The ESP-IDF early-expansion guard that opens `firmware/ui/CMakeLists.txt` sits outside those branches and is P2b's (contract §2.17): Task 2 Step 6b checks that it is there and stops if it is not; this plan never adds or edits it.

---

### Task 1: Host test harness, partition table and the size check

**Files:**
- Create: `firmware/ports/esp32/host-tests/CMakeLists.txt`
- Create: `firmware/ports/esp32/host-tests/test_partitions.c`
- Create: `firmware/ports/esp32/partitions/16mb.csv`
- Create: `firmware/ports/esp32/tools/check-size.sh`
- Create: `firmware/ports/esp32/tools/test-check-size.sh`

**Interfaces:**
- Consumes: `const gadget_board_t *gadget_board_at(size_t index)` and `gadget_board_t.ota_max` (contract §2.3, P2a), from `firmware/core/src/boards.c`. That file is pure C; it is the only core source the host tests link (core's `util.c` needs PSA crypto).
- Produces: the host-test project — CMake function `port_test(<name> [args...])` (adds executable `<name>` from `<name>.c` and CTest `esp32.<name>`, label `unit`), list `PORT_LOGIC_SRCS` that later tasks extend; `partitions/16mb.csv`; `tools/check-size.sh <board> [build-dir]` (exit 0 only when `openmausbot-gadget.bin` ≤ 6291456 bytes and `sdkconfig` has both exact rollback lines; build dir defaults to `firmware/ports/esp32/build/<board>`).

- [ ] **Step 1: Create the branch**

```bash
cd /Users/omkar/Desktop/openmaus/openmausbot-gadget-sdk
git switch -c p2c-esp32 p2b-ui
```

Expected: `Switched to a new branch 'p2c-esp32'`.

- [ ] **Step 2: Write the host-test project and the failing partition test**

`firmware/ports/esp32/host-tests/CMakeLists.txt`:

```cmake
# SPDX-License-Identifier: Apache-2.0
# Host-side unit tests for the ESP32 port's pure logic (main/logic/) and for
# the port's data files. Runs on macOS and Linux with any C11 compiler; no
# ESP-IDF needed.
cmake_minimum_required(VERSION 3.24)
project(gadget_esp32_host_tests C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

include(FetchContent)
FetchContent_Declare(unity
  URL https://github.com/ThrowTheSwitch/Unity/archive/refs/tags/v2.7.0.tar.gz
  URL_HASH SHA256=e84eb301ca7967831e68b1728f911e87fa2d345d8ddb64f897bc2f2ee24a321c)
FetchContent_MakeAvailable(unity)

set(ESP_DIR ${CMAKE_CURRENT_LIST_DIR}/..)
set(CORE_DIR ${ESP_DIR}/../../core)

# Port logic under test. Each task appends its files here.
set(PORT_LOGIC_SRCS
  ${CORE_DIR}/src/boards.c
)

add_library(port_logic STATIC ${PORT_LOGIC_SRCS})
target_include_directories(port_logic PUBLIC ${ESP_DIR}/main/logic ${CORE_DIR}/include)
target_compile_options(port_logic PRIVATE -Wall -Wextra -Werror)

enable_testing()
function(port_test name)
  add_executable(${name} ${name}.c)
  target_link_libraries(${name} PRIVATE port_logic unity::framework)
  target_compile_options(${name} PRIVATE -Wall -Wextra -Werror)
  add_test(NAME esp32.${name} COMMAND ${name} ${ARGN})
  set_tests_properties(esp32.${name} PROPERTIES LABELS unit TIMEOUT 30)
endfunction()

port_test(test_partitions ${ESP_DIR}/partitions/16mb.csv)
```

`firmware/ports/esp32/host-tests/test_partitions.c`:

```c
/* SPDX-License-Identifier: Apache-2.0 */
/* partitions/16mb.csv matches spec §5.3 and every board's caps.ota.max. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gadget_board.h"
#include "unity.h"

typedef struct {
  char name[16], type[8], subtype[16];
  unsigned long offset, size;
} part_t;

static const char *g_csv;
static part_t g_parts[16];
static int g_n;

static char *trim(char *s) {
  while (*s == ' ' || *s == '\t') s++;
  char *e = s + strlen(s);
  while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r' || e[-1] == '\n')) *--e = '\0';
  return s;
}

void setUp(void) {
  FILE *f = fopen(g_csv, "r");
  TEST_ASSERT_NOT_NULL_MESSAGE(f, g_csv);
  char line[256];
  g_n = 0;
  while (fgets(line, sizeof(line), f) != NULL) {
    char *s = trim(line);
    if (*s == '\0' || *s == '#') continue;
    char *field[6] = {0};
    int k = 0;
    for (char *tok = strtok(s, ","); tok != NULL && k < 6; tok = strtok(NULL, ",")) field[k++] = trim(tok);
    TEST_ASSERT_TRUE_MESSAGE(k >= 5, "a partition row needs 5 fields");
    TEST_ASSERT_TRUE(g_n < 16);
    part_t *p = &g_parts[g_n++];
    snprintf(p->name, sizeof(p->name), "%s", field[0]);
    snprintf(p->type, sizeof(p->type), "%s", field[1]);
    snprintf(p->subtype, sizeof(p->subtype), "%s", field[2]);
    p->offset = strtoul(field[3], NULL, 0);
    p->size = strtoul(field[4], NULL, 0);
  }
  fclose(f);
}
void tearDown(void) {}

static void test_rows_match_spec(void) {
  static const part_t want[] = {
    {"nvs", "data", "nvs", 0x9000, 0x6000},         {"otadata", "data", "ota", 0xF000, 0x2000},
    {"phy_init", "data", "phy", 0x11000, 0x1000},   {"ota_0", "app", "ota_0", 0x20000, 0x600000},
    {"ota_1", "app", "ota_1", 0x620000, 0x600000},  {"coredump", "data", "coredump", 0xC20000, 0x10000},
  };
  TEST_ASSERT_EQUAL_INT(6, g_n);
  for (int i = 0; i < 6; i++) {
    TEST_ASSERT_EQUAL_STRING(want[i].name, g_parts[i].name);
    TEST_ASSERT_EQUAL_STRING(want[i].type, g_parts[i].type);
    TEST_ASSERT_EQUAL_STRING(want[i].subtype, g_parts[i].subtype);
    TEST_ASSERT_EQUAL_HEX32(want[i].offset, g_parts[i].offset);
    TEST_ASSERT_EQUAL_HEX32(want[i].size, g_parts[i].size);
  }
}

static void test_layout_is_valid_for_16mb(void) {
  TEST_ASSERT_TRUE(g_parts[0].offset >= 0x9000); /* after the table at 0x8000 */
  for (int i = 0; i < g_n; i++) {
    if (strcmp(g_parts[i].type, "app") == 0) {
      TEST_ASSERT_EQUAL_HEX32(0, g_parts[i].offset % 0x10000);
    }
    TEST_ASSERT_TRUE(g_parts[i].offset + g_parts[i].size <= 0x1000000);
    if (i > 0) {
      TEST_ASSERT_TRUE(g_parts[i - 1].offset + g_parts[i - 1].size <= g_parts[i].offset);
    }
  }
}

static void test_every_board_ota_max_is_the_slot_size(void) {
  size_t n = 0;
  for (const gadget_board_t *b; (b = gadget_board_at(n)) != NULL; n++) {
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(g_parts[3].size, b->ota_max, b->id);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(g_parts[4].size, b->ota_max, b->id);
  }
  TEST_ASSERT_EQUAL_size_t(4, n);
}

int main(int argc, char **argv) {
  if (argc < 2) {
    fprintf(stderr, "usage: %s <partitions/16mb.csv>\n", argv[0]);
    return 2;
  }
  g_csv = argv[1];
  UNITY_BEGIN();
  RUN_TEST(test_rows_match_spec);
  RUN_TEST(test_layout_is_valid_for_16mb);
  RUN_TEST(test_every_board_ota_max_is_the_slot_size);
  return UNITY_END();
}
```

- [ ] **Step 3: Run it to verify it fails**

Run: `cmake -S firmware/ports/esp32/host-tests -B build/esp32-host && cmake --build build/esp32-host -j10 && ctest --test-dir build/esp32-host --output-on-failure`

Expected: FAIL — three lines like `.../host-tests/test_partitions.c:28:test_rows_match_spec:FAIL:.../firmware/ports/esp32/partitions/16mb.csv` (the file does not exist yet) and `0% tests passed, 1 tests failed out of 1`.

- [ ] **Step 4: Write the partition table**

`firmware/ports/esp32/partitions/16mb.csv`:

```text
# SPDX-License-Identifier: Apache-2.0
# One 16 MB layout for every board. Keep offsets and sizes stable across
# releases: the installer flashes separate parts and keeps nvs (identity
# key, Wi-Fi, pairing). caps.ota.max = the ota_0/ota_1 size (6291456).
# Name,   Type, SubType,  Offset,   Size,     Flags
nvs,      data, nvs,      0x9000,   0x6000,
otadata,  data, ota,      0xf000,   0x2000,
phy_init, data, phy,      0x11000,  0x1000,
ota_0,    app,  ota_0,    0x20000,  0x600000,
ota_1,    app,  ota_1,    0x620000, 0x600000,
coredump, data, coredump, 0xc20000, 0x10000,
```

- [ ] **Step 5: Run the test to verify it passes**

Run: `cmake -S firmware/ports/esp32/host-tests -B build/esp32-host && cmake --build build/esp32-host -j10 && ctest --test-dir build/esp32-host --output-on-failure`

Expected: `100% tests passed, 0 tests failed out of 1`.

- [ ] **Step 6: Write the failing test for the size check**

`firmware/ports/esp32/tools/test-check-size.sh`:

```bash
#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# Tests for check-size.sh with fake build directories. Prints "ok" lines and
# exits non-zero on the first wrong result.
set -uo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

good_cfg() {
  printf '%s\n' 'CONFIG_IDF_TARGET="esp32s3"' 'CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y' \
    '# CONFIG_BOOTLOADER_APP_ANTI_ROLLBACK is not set' >"$1/sdkconfig"
}
make_build() { # <dir> <app bytes>
  mkdir -p "$1"
  head -c "$2" /dev/zero >"$1/openmausbot-gadget.bin"
  good_cfg "$1"
}
expect() { # <want exit> <name> <args...>
  local want="$1" name="$2"
  shift 2
  "$here/check-size.sh" "$@" >"$tmp/out" 2>&1
  local got=$?
  if [[ "$got" != "$want" ]]; then
    echo "FAIL $name: exit $got, want $want"
    cat "$tmp/out"
    exit 1
  fi
  echo "ok $name"
}

make_build "$tmp/exact" 6291456
expect 0 "app exactly the slot size" amoled-175c "$tmp/exact"
grep -q 'check-size: amoled-175c ok, 6291456 of 6291456 bytes (100%)' "$tmp/out" || { echo "FAIL summary line"; exit 1; }

make_build "$tmp/big" 6291457
expect 1 "app one byte over the slot" amoled-175c "$tmp/big"

make_build "$tmp/norollback" 1000
printf '%s\n' '# CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE is not set' >"$tmp/norollback/sdkconfig"
expect 1 "rollback off" lcd-154 "$tmp/norollback"

make_build "$tmp/antirollback" 1000
printf '%s\n' 'CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y' 'CONFIG_BOOTLOADER_APP_ANTI_ROLLBACK=y' >"$tmp/antirollback/sdkconfig"
expect 1 "anti-rollback on" devkit "$tmp/antirollback"

mkdir -p "$tmp/nobin"
good_cfg "$tmp/nobin"
expect 1 "no app image" amoled-175 "$tmp/nobin"

expect 1 "no board argument"
echo "all check-size tests passed"
```

Run: `chmod +x firmware/ports/esp32/tools/test-check-size.sh && firmware/ports/esp32/tools/test-check-size.sh`

Expected: FAIL — `FAIL app exactly the slot size: exit 127, want 0` followed by `...check-size.sh: No such file or directory`.

- [ ] **Step 7: Write the size check**

`firmware/ports/esp32/tools/check-size.sh`:

```bash
#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# Usage: tools/check-size.sh <board> [build-dir]
# Fails when build/<board>/openmausbot-gadget.bin does not fit the 6 MiB OTA
# slot (caps.ota.max), or when build/<board>/sdkconfig lost app rollback or
# turned on anti-rollback (spec §4.8, contract §2.17). The build directory
# defaults to build/<board> next to this script's parent directory.
set -euo pipefail

SLOT_BYTES=6291456
board="${1:?usage: check-size.sh <board> [build-dir]}"
esp_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="${2:-$esp_dir/build/$board}"
bin="$build_dir/openmausbot-gadget.bin"
cfg="$build_dir/sdkconfig"

if [[ ! -f "$bin" ]]; then
  echo "check-size: $bin not found; build $board first" >&2
  exit 1
fi
if [[ ! -f "$cfg" ]]; then
  echo "check-size: $cfg not found" >&2
  exit 1
fi

fail=0
size=$(wc -c <"$bin" | tr -d ' ')
if ((size > SLOT_BYTES)); then
  echo "check-size: $board app is $size bytes, over the $SLOT_BYTES-byte OTA slot" >&2
  fail=1
fi
for line in 'CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y' '# CONFIG_BOOTLOADER_APP_ANTI_ROLLBACK is not set'; do
  if ! grep -qxF "$line" "$cfg"; then
    echo "check-size: $cfg lacks the line: $line" >&2
    fail=1
  fi
done
if ((fail == 0)); then
  echo "check-size: $board ok, $size of $SLOT_BYTES bytes ($((size * 100 / SLOT_BYTES))%)"
fi
exit "$fail"
```

- [ ] **Step 8: Run the script tests to verify they pass**

Run: `chmod +x firmware/ports/esp32/tools/check-size.sh && firmware/ports/esp32/tools/test-check-size.sh`

Expected:

```text
ok app exactly the slot size
ok app one byte over the slot
ok rollback off
ok anti-rollback on
ok no app image
ok no board argument
all check-size tests passed
```

- [ ] **Step 9: Commit**

```bash
git add firmware/ports/esp32/host-tests/CMakeLists.txt \
  firmware/ports/esp32/host-tests/test_partitions.c \
  firmware/ports/esp32/partitions/16mb.csv \
  firmware/ports/esp32/tools/check-size.sh \
  firmware/ports/esp32/tools/test-check-size.sh
git commit -m "feat(esp32): 16 MB partition table, size check and host test harness"
```
### Task 2: ESP-IDF v6.0.3, the ESP-IDF project, board defaults and a build of all four boards

**Files:**
- Create: `firmware/ports/esp32/host-tests/test_board_defaults.c`
- Modify: `firmware/ports/esp32/host-tests/CMakeLists.txt` (one `port_test` line)
- Create: `firmware/ports/esp32/sdkconfig.defaults`, `firmware/ports/esp32/boards/{amoled-175c,amoled-175,lcd-154,devkit}/sdkconfig.defaults`
- Create: `firmware/ports/esp32/CMakeLists.txt`, `firmware/ports/esp32/sdkconfig.nvs-encrypt`, `firmware/ports/esp32/sdkconfig.test-keys`
- Create: `firmware/ports/esp32/main/CMakeLists.txt` (first version), `firmware/ports/esp32/main/idf_component.yml`, `firmware/ports/esp32/main/Kconfig.projbuild`, `firmware/ports/esp32/main/main.c` (smoke app, replaced in Task 13)
- Create: `firmware/ports/esp32/boards/{amoled-175c,amoled-175,lcd-154,devkit}/board.h`, `.../board.cmake` (empty driver lists)
- Create: `firmware/ports/esp32/tools/build-all.sh`
- Create (generated by the first build, then committed): `firmware/ports/esp32/dependencies.lock.{amoled-175c,amoled-175,lcd-154,devkit}`
- Modify (only if Step 6 finds it missing): `firmware/core/CMakeLists.txt`, inside `if(ESP_PLATFORM)` only (contract §2.17)
- Check, never modify: `firmware/ui/CMakeLists.txt`'s early-expansion guard (Step 6b; P2b owns it, contract §2.17)

**Interfaces:**
- Consumes: `gadget_board_by_id()`, `gadget_board_t.display_name` (P2a); the `if(ESP_PLATFORM)` branches of `firmware/core/CMakeLists.txt` (`REQUIRES espressif__cjson mbedtls`, `CONFIG_GADGET_TEST_KEYS` → `GADGET_TEST_KEYS=1`) and `firmware/ui/CMakeLists.txt` (`REQUIRES lvgl__lvgl core`, compiles only `art/${CONFIG_GADGET_ART_PROFILE}`), contract §2.15 and §2.17; P2b's early-expansion guard at the top of `firmware/ui/CMakeLists.txt` (`if(ESP_PLATFORM AND CMAKE_BUILD_EARLY_EXPANSION)` → `idf_component_register(INCLUDE_DIRS . art fonts REQUIRES lvgl__lvgl core)` → `return()` → `endif()`, contract §2.17).
- Produces: Kconfig symbols `CONFIG_GADGET_BOARD_ID` (string), `CONFIG_GADGET_ART_PROFILE` (string `s240`/`s150`), `CONFIG_GADGET_TEST_KEYS` (bool, default n), `CONFIG_GADGET_NVS_ENCRYPT` (bool, default n); build property `GADGET_BOARD`; CMake switches `-D GADGET_NVS_ENCRYPT=1` and `-D GADGET_TEST_KEYS=1` (each appends an overlay to `SDKCONFIG_DEFAULTS`); the board pin macros `BOARD_*` in each `board.h` (used by `display.c` and `board.c`); an optional `boards/<id>/board.cmake` setting `BOARD_DRIVER_SRCS` (file names in `main/drivers/`; a board without one compiles every shared driver and the linker's `--gc-sections` drops the unused ones, so the only required board files stay `board.h`, `board.c` and `sdkconfig.defaults`, as spec §5.1 and contract §1.1 list them); `tools/build-all.sh [board...]` (env `IDF_ARGS`, `SKIP_ART_CHECK=1`; fails when a standard `build/<board>/sdkconfig` has test keys or NVS encryption on); the project refuses `-D GADGET_TEST_KEYS=1` or `-D GADGET_NVS_ENCRYPT=1` in the standard `build/<board>` directory.

- [ ] **Step 1: Write the failing board-defaults test**

`firmware/ports/esp32/host-tests/test_board_defaults.c`:

```c
/* SPDX-License-Identifier: Apache-2.0 */
#define _POSIX_C_SOURCE 200809L
/* boards/<id>/sdkconfig.defaults agree with core's board table, every board
 * directory has a table row, and the shared defaults keep rollback, the
 * USB console and the test key off (contract §2.13, §2.17). */
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gadget_board.h"
#include "unity.h"

static const char *g_esp_dir;

void setUp(void) {}
void tearDown(void) {}

static char *read_file(const char *path) {
  FILE *f = fopen(path, "rb");
  TEST_ASSERT_NOT_NULL_MESSAGE(f, path);
  fseek(f, 0, SEEK_END);
  long n = ftell(f);
  fseek(f, 0, SEEK_SET);
  char *buf = malloc((size_t)n + 1);
  TEST_ASSERT_NOT_NULL(buf);
  TEST_ASSERT_EQUAL_size_t((size_t)n, fread(buf, 1, (size_t)n, f));
  buf[n] = '\0';
  fclose(f);
  return buf;
}

/* true when `line` appears as a whole line of text */
static bool has_line(const char *text, const char *line) {
  size_t n = strlen(line);
  for (const char *p = text; (p = strstr(p, line)) != NULL; p += n) {
    bool starts = p == text || p[-1] == '\n';
    bool ends = p[n] == '\n' || p[n] == '\0' || p[n] == '\r';
    if (starts && ends) return true;
  }
  return false;
}

static void test_each_board_defaults_match_the_table(void) {
  for (size_t i = 0; gadget_board_at(i) != NULL; i++) {
    const gadget_board_t *b = gadget_board_at(i);
    char path[512], want[128];
    snprintf(path, sizeof(path), "%s/boards/%s/sdkconfig.defaults", g_esp_dir, b->id);
    char *text = read_file(path);
    snprintf(want, sizeof(want), "CONFIG_GADGET_BOARD_ID=\"%s\"", b->id);
    TEST_ASSERT_TRUE_MESSAGE(has_line(text, want), want);
    snprintf(want, sizeof(want), "CONFIG_GADGET_ART_PROFILE=\"%s\"", b->art_profile == GADGET_ART_S240 ? "s240" : "s150");
    TEST_ASSERT_TRUE_MESSAGE(has_line(text, want), want);
    TEST_ASSERT_TRUE_MESSAGE(has_line(text, "CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y"), b->id);
    TEST_ASSERT_TRUE_MESSAGE(has_line(text, "CONFIG_PARTITION_TABLE_CUSTOM_FILENAME=\"partitions/16mb.csv\""), b->id);
    TEST_ASSERT_TRUE_MESSAGE(has_line(text, "# CONFIG_GADGET_TEST_KEYS is not set"), b->id);
    TEST_ASSERT_NULL_MESSAGE(strstr(text, "CONFIG_GADGET_TEST_KEYS=y"), b->id);
    /* a board file wins over the shared one: it must not touch the LVGL
     * pins that test_shared_defaults checks */
    TEST_ASSERT_NULL_MESSAGE(strstr(text, "CONFIG_LV_CONF_SKIP"), b->id);
    TEST_ASSERT_NULL_MESSAGE(strstr(text, "CONFIG_LV_CACHE_DEF_SIZE"), b->id);
    TEST_ASSERT_NULL_MESSAGE(strstr(text, "CONFIG_LV_IMAGE_HEADER_CACHE_DEF_CNT"), b->id);
    free(text);
  }
}

static void test_every_board_dir_has_a_table_row(void) {
  char path[512];
  snprintf(path, sizeof(path), "%s/boards", g_esp_dir);
  DIR *d = opendir(path);
  TEST_ASSERT_NOT_NULL_MESSAGE(d, path);
  int dirs = 0;
  for (struct dirent *e; (e = readdir(d)) != NULL;) {
    if (e->d_name[0] == '.') continue;
    dirs++;
    TEST_ASSERT_NOT_NULL_MESSAGE(gadget_board_by_id(e->d_name), e->d_name);
  }
  closedir(d);
  size_t rows = 0;
  while (gadget_board_at(rows) != NULL) rows++;
  TEST_ASSERT_EQUAL_INT((int)rows, dirs);
}

static void test_shared_defaults(void) {
  char path[512];
  snprintf(path, sizeof(path), "%s/sdkconfig.defaults", g_esp_dir);
  char *text = read_file(path);
  TEST_ASSERT_TRUE(has_line(text, "CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y"));
  TEST_ASSERT_TRUE(has_line(text, "# CONFIG_BOOTLOADER_APP_ANTI_ROLLBACK is not set"));
  TEST_ASSERT_TRUE(has_line(text, "CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y"));
  TEST_ASSERT_TRUE(has_line(text, "CONFIG_PARTITION_TABLE_CUSTOM=y"));
  TEST_ASSERT_TRUE(has_line(text, "# CONFIG_GADGET_TEST_KEYS is not set"));
  TEST_ASSERT_TRUE(has_line(text, "# CONFIG_GADGET_NVS_ENCRYPT is not set"));
  TEST_ASSERT_NULL(strstr(text, "CONFIG_GADGET_TEST_KEYS=y"));
  /* LVGL from Kconfig only (firmware/ui/lv_conf.h is the simulator's) and
   * both image caches off, as P2b's ui_lv_requirements.h demands. They equal
   * LVGL's Kconfig defaults; pinning them here makes this test, not the first
   * idf.py build, the first check. */
  TEST_ASSERT_TRUE(has_line(text, "CONFIG_LV_CONF_SKIP=y"));
  TEST_ASSERT_NULL(strstr(text, "# CONFIG_LV_CONF_SKIP is not set"));
  TEST_ASSERT_TRUE(has_line(text, "CONFIG_LV_CACHE_DEF_SIZE=0"));
  TEST_ASSERT_TRUE(has_line(text, "CONFIG_LV_IMAGE_HEADER_CACHE_DEF_CNT=0"));
  free(text);
}

int main(int argc, char **argv) {
  if (argc < 2) {
    fprintf(stderr, "usage: %s <firmware/ports/esp32>\n", argv[0]);
    return 2;
  }
  g_esp_dir = argv[1];
  UNITY_BEGIN();
  RUN_TEST(test_each_board_defaults_match_the_table);
  RUN_TEST(test_every_board_dir_has_a_table_row);
  RUN_TEST(test_shared_defaults);
  return UNITY_END();
}
```

Append to `firmware/ports/esp32/host-tests/CMakeLists.txt`:

```cmake
port_test(test_board_defaults ${ESP_DIR})
```

- [ ] **Step 2: Run it to verify it fails**

Run: `cmake -S firmware/ports/esp32/host-tests -B build/esp32-host && cmake --build build/esp32-host -j10 && ctest --test-dir build/esp32-host --output-on-failure`

Expected: FAIL — `test_board_defaults.c:21:test_each_board_defaults_match_the_table:FAIL:.../boards/amoled-175c/sdkconfig.defaults`, `test_every_board_dir_has_a_table_row:FAIL:.../boards`, `test_shared_defaults:FAIL:.../sdkconfig.defaults`; `50% tests passed, 1 tests failed out of 2`.

- [ ] **Step 3: Write the shared and per-board Kconfig defaults**

`firmware/ports/esp32/sdkconfig.defaults`:

```text
# SPDX-License-Identifier: Apache-2.0
# Shared ESP-IDF defaults for every board. CMakeLists.txt applies
# boards/<board>/sdkconfig.defaults after this file, so a board file wins.
CONFIG_IDF_TARGET="esp32s3"

# Flash: one 16 MB layout (boards set CONFIG_ESPTOOLPY_FLASHSIZE_16MB)
CONFIG_ESPTOOLPY_FLASHMODE_QIO=y
CONFIG_ESPTOOLPY_FLASHFREQ_80M=y
CONFIG_PARTITION_TABLE_CUSTOM=y
CONFIG_PARTITION_TABLE_OFFSET=0x8000

# OTA: app rollback on, anti-rollback never (spec §4.8, contract §2.17)
CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y
# CONFIG_BOOTLOADER_APP_ANTI_ROLLBACK is not set

# CPU, RTOS and the 8 MB octal PSRAM every board carries
CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ_240=y
CONFIG_FREERTOS_HZ=1000
CONFIG_SPIRAM=y
CONFIG_SPIRAM_MODE_OCT=y
CONFIG_SPIRAM_SPEED_80M=y
CONFIG_SPIRAM_USE_MALLOC=y
CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP=y
# Code keeps running from PSRAM while OTA and NVS write flash
CONFIG_SPIRAM_XIP_FROM_PSRAM=y

# Console on the native USB-Serial-JTAG port (installer, idf.py monitor)
CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y

# Crash dumps go to the coredump partition
CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH=y

# PSA crypto: deterministic ECDSA over P-256 (IDF defaults, pinned)
CONFIG_MBEDTLS_ECDSA_DETERMINISTIC=y
CONFIG_MBEDTLS_ECP_DP_SECP256R1_ENABLED=y

# esp_websocket_client: sends do not wait behind receives
CONFIG_ESP_WS_CLIENT_SEPARATE_TX_LOCK=y

# LVGL 9.6 on the device, from Kconfig only: firmware/ui/lv_conf.h is the
# simulator's and is on the ui include path, so it must never be read here.
# firmware/ui/ui_lv_requirements.h checks both builds.
CONFIG_LV_CONF_SKIP=y
CONFIG_LV_COLOR_FORMAT_RGB565=y
CONFIG_LV_OS_NONE=y
CONFIG_LV_USE_CLIB_MALLOC=y
CONFIG_LV_USE_CLIB_STRING=y
CONFIG_LV_DEF_REFR_PERIOD=33
CONFIG_LV_DRAW_SW_SUPPORT_RGB565A8=y
CONFIG_LV_DRAW_SW_SUPPORT_A8=y
CONFIG_LV_DRAW_SW_COMPLEX=y
CONFIG_LV_USE_ANIMIMG=y
CONFIG_LV_FONT_MONTSERRAT_14=y
# Image caches off: the Image screen reuses two lv_image_dsc_t slots and both
# caches are keyed by that address (ui_lv_requirements.h #errors otherwise).
# 0 is LVGL's Kconfig default; pinned so host-tests can check it.
CONFIG_LV_CACHE_DEF_SIZE=0
CONFIG_LV_IMAGE_HEADER_CACHE_DEF_CNT=0
# CONFIG_LV_BUILD_EXAMPLES is not set
# CONFIG_LV_BUILD_DEMOS is not set

# Gadget options (main/Kconfig.projbuild). The test signing key never ships
# in a board build (A37); NVS encryption is opt-in (sdkconfig.nvs-encrypt).
# CONFIG_GADGET_TEST_KEYS is not set
# CONFIG_GADGET_NVS_ENCRYPT is not set
```

`firmware/ports/esp32/boards/amoled-175c/sdkconfig.defaults`:

```text
# SPDX-License-Identifier: Apache-2.0
# amoled-175c: applied after ../../sdkconfig.defaults (see ../../CMakeLists.txt).
CONFIG_GADGET_BOARD_ID="amoled-175c"
CONFIG_GADGET_ART_PROFILE="s240"
CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y
CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions/16mb.csv"
# CONFIG_GADGET_TEST_KEYS is not set
```

`firmware/ports/esp32/boards/amoled-175/sdkconfig.defaults`:

```text
# SPDX-License-Identifier: Apache-2.0
# amoled-175: applied after ../../sdkconfig.defaults (see ../../CMakeLists.txt).
CONFIG_GADGET_BOARD_ID="amoled-175"
CONFIG_GADGET_ART_PROFILE="s240"
CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y
CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions/16mb.csv"
# CONFIG_GADGET_TEST_KEYS is not set
```

`firmware/ports/esp32/boards/lcd-154/sdkconfig.defaults`:

```text
# SPDX-License-Identifier: Apache-2.0
# lcd-154: applied after ../../sdkconfig.defaults (see ../../CMakeLists.txt).
CONFIG_GADGET_BOARD_ID="lcd-154"
CONFIG_GADGET_ART_PROFILE="s150"
CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y
CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions/16mb.csv"
# CONFIG_GADGET_TEST_KEYS is not set
```

`firmware/ports/esp32/boards/devkit/sdkconfig.defaults`:

```text
# SPDX-License-Identifier: Apache-2.0
# devkit: applied after ../../sdkconfig.defaults (see ../../CMakeLists.txt).
CONFIG_GADGET_BOARD_ID="devkit"
CONFIG_GADGET_ART_PROFILE="s150"
CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y
CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions/16mb.csv"
# CONFIG_GADGET_TEST_KEYS is not set
```

- [ ] **Step 4: Run the test to verify it passes**

Run: `cmake -S firmware/ports/esp32/host-tests -B build/esp32-host && cmake --build build/esp32-host -j10 && ctest --test-dir build/esp32-host --output-on-failure`

Expected: `100% tests passed, 0 tests failed out of 2`.

- [ ] **Step 5: Install ESP-IDF v6.0.3**

```bash
brew install libgcrypt glib pixman sdl2 libslirp dfu-util ninja
brew tap espressif/eim
brew install eim
eim install -i v6.0.3 -t esp32s3
. ~/.espressif/tools/activate_idf_v6.0.3.sh
idf.py --version
```

Expected: the last command prints `ESP-IDF v6.0.3`. Plan for about 5 GB of disk. The first line installs EIM's macOS prerequisites (Espressif's EIM documentation: on POSIX systems the installer only checks them, and the installation does not proceed when one is missing; automatic prerequisite installation is Windows-only). Packages that are already installed (this Mac has `sdl2`) are skipped. If EIM is not usable, use the `install.sh` fallback in Global Constraints and activate with `. ~/esp/esp-idf-v6.0.3/export.sh` from then on.

- [ ] **Step 6: Check the ESP branches of core and ui (contract §2.15, §2.17)**

```bash
grep -n 'if(ESP_PLATFORM)' firmware/core/CMakeLists.txt firmware/ui/CMakeLists.txt
grep -n 'REQUIRES espressif__cjson mbedtls' firmware/core/CMakeLists.txt
grep -n 'keys_release.c\|keys_test.c' firmware/core/CMakeLists.txt
grep -n 'CONFIG_GADGET_TEST_KEYS' firmware/core/CMakeLists.txt
grep -n 'lvgl__lvgl' firmware/ui/CMakeLists.txt
grep -n 'CONFIG_GADGET_ART_PROFILE' firmware/ui/CMakeLists.txt
```

Expected: every command prints at least one line. If only the `CONFIG_GADGET_TEST_KEYS` grep prints nothing, insert this block in `firmware/core/CMakeLists.txt` directly after the closing `)` of the `idf_component_register(...)` call inside `if(ESP_PLATFORM)` (before its `return()`), exactly as contract §2.17 pins it, and re-run the grep:

```cmake
  if(CONFIG_GADGET_TEST_KEYS)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE GADGET_TEST_KEYS=1)
  endif()
```

If any other grep prints nothing, stop and report it as a gap in P2a's or P2b's ESP branch; do not invent the missing mechanism (in particular, the macro `maus_art.c` uses to know which art profile is linked belongs to P2b).

- [ ] **Step 6b: Check P2b's early-expansion guard in `firmware/ui/CMakeLists.txt` (contract §2.17)**

ESP-IDF v6.0.3 first reads every component's `CMakeLists.txt` in CMake script mode (`cmake -P tools/cmake/scripts/component_get_requirements.cmake`, with `ESP_PLATFORM=1` and `CMAKE_BUILD_EARLY_EXPANSION=1`) to collect its requirements. That script loads the build and component properties but never `sdkconfig.cmake`, and in script mode the ui file's top-level `file(GLOB UI_FONT_SRCS CONFIGURE_DEPENDS ...)` is an error (`CONFIGURE_DEPENDS is invalid for script and find package modes`), so without a guard the first `idf.py` run stops there. Contract §2.17 settles who fixes it: `firmware/ui/CMakeLists.txt` opens with P2b's early-expansion guard (`if(ESP_PLATFORM AND CMAKE_BUILD_EARLY_EXPANSION)` → `idf_component_register(INCLUDE_DIRS . art fonts REQUIRES lvgl__lvgl core)` → `return()` → `endif()`), which reports the ui component's requirements and returns before that glob, and P2b owns it. P2b's ESP-branch art-profile check also skips early expansion, where `CONFIG_GADGET_ART_PROFILE` is still empty. The guard changes nothing in a desktop build (`ESP_PLATFORM` is unset there) or in IDF's real configure pass (`CMAKE_BUILD_EARLY_EXPANSION` is unset there). core's file needs no guard (its ESP branch registers before reading any `CONFIG_*`), and neither does this plan's `main/CMakeLists.txt`. This step only checks the guard; it never adds or edits it.

```bash
grep -nxF -e 'if(ESP_PLATFORM AND CMAKE_BUILD_EARLY_EXPANSION)' \
  -e '  idf_component_register(INCLUDE_DIRS . art fonts REQUIRES lvgl__lvgl core)' \
  -e '  return()' \
  -e 'set(UI_SRCS' firmware/ui/CMakeLists.txt
```

Expected (the line numbers are those of P2b's file as P2b's plan writes it; a change to its header comment shifts them, which is fine):

```
7:if(ESP_PLATFORM AND CMAKE_BUILD_EARLY_EXPANSION)
8:  idf_component_register(INCLUDE_DIRS . art fonts REQUIRES lvgl__lvgl core)
9:  return()
11:set(UI_SRCS
```

The first three lines must be present, consecutive and above `set(UI_SRCS`. If only `set(UI_SRCS` prints, or the guard lines are split up or come after it, stop and report it to P2b as a missing early-expansion guard (contract §2.17). Do not add or change the guard here: outside its `if(ESP_PLATFORM)` branches the file is P2b's, and Task 16 Step 5 fails on any edit to it. Step 8 proves the guard with a real `idf.py reconfigure`.

- [ ] **Step 7: Write the ESP-IDF project**

`firmware/ports/esp32/CMakeLists.txt`:

```cmake
# SPDX-License-Identifier: Apache-2.0
# ESP-IDF project for the gadget firmware (spec §5.1). Build one board at a
# time, each with its own build directory and sdkconfig:
#   idf.py -B build/<board> -D GADGET_BOARD=<board> -D SDKCONFIG=build/<board>/sdkconfig build
# Optional, each in its own build directory: -D PROJECT_VER=<version>
# (release CI), -D GADGET_NVS_ENCRYPT=1 (opt-in NVS encryption, burns an eFuse
# on first boot: sdkconfig.nvs-encrypt), -D GADGET_TEST_KEYS=1 (bench OTA
# tests only, never published: sdkconfig.test-keys).
cmake_minimum_required(VERSION 3.22)

if(NOT DEFINED GADGET_BOARD OR "${GADGET_BOARD}" STREQUAL "")
  message(FATAL_ERROR "GADGET_BOARD is not set. Pass -D GADGET_BOARD=<board>, one of the directories in boards/.")
endif()
if(NOT GADGET_BOARD MATCHES "^[a-z0-9-]+$" OR NOT IS_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}/boards/${GADGET_BOARD}")
  message(FATAL_ERROR "Unknown board '${GADGET_BOARD}': there is no directory boards/${GADGET_BOARD}.")
endif()
# Test-key and NVS-encryption builds stay out of the standard build directory:
# their cache entries and CONFIG_ lines would stick to every later build there
# (build-all.sh, check-size.sh, release builds).
get_filename_component(_gadget_bin "${CMAKE_BINARY_DIR}" REALPATH)
get_filename_component(_gadget_std "${CMAKE_CURRENT_LIST_DIR}/build/${GADGET_BOARD}" REALPATH)
if((GADGET_TEST_KEYS OR GADGET_NVS_ENCRYPT) AND _gadget_bin STREQUAL _gadget_std)
  message(FATAL_ERROR "Build test-key or NVS-encryption variants in their own -B directory, not build/${GADGET_BOARD} "
                      "(if build/${GADGET_BOARD} already has them cached, delete it and build again).")
endif()

# Board defaults come last so they win over the shared file.
set(SDKCONFIG_DEFAULTS "sdkconfig.defaults;boards/${GADGET_BOARD}/sdkconfig.defaults")
if(GADGET_NVS_ENCRYPT)
  list(APPEND SDKCONFIG_DEFAULTS "sdkconfig.nvs-encrypt")
endif()
if(GADGET_TEST_KEYS)
  list(APPEND SDKCONFIG_DEFAULTS "sdkconfig.test-keys")
endif()

# A local build never takes version.txt or `git describe`: custom builds are -dev.
if(NOT DEFINED PROJECT_VER)
  set(PROJECT_VER "0.0.0-dev")
endif()

set(EXTRA_COMPONENT_DIRS
  "${CMAKE_CURRENT_LIST_DIR}/../../core"
  "${CMAKE_CURRENT_LIST_DIR}/../../ui")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
# One lock file per board, so the four builds never rewrite each other's.
idf_build_set_property(DEPENDENCIES_LOCK "${CMAKE_CURRENT_LIST_DIR}/dependencies.lock.${GADGET_BOARD}")
# main/CMakeLists.txt reads the board id (build properties reach every component).
idf_build_set_property(GADGET_BOARD "${GADGET_BOARD}")
project(openmausbot-gadget)
```

`firmware/ports/esp32/sdkconfig.nvs-encrypt`:

```text
# SPDX-License-Identifier: Apache-2.0
# Opt-in NVS encryption (spec §4.2, A16). Applied only when building with
# -D GADGET_NVS_ENCRYPT=1, in its own build directory.
#
# WARNING: on its first boot the firmware generates an HMAC key and burns it
# into eFuse key block 5. That is permanent. Afterwards a build without
# these options cannot read this board's NVS: NVS is erased, the identity
# key is lost and the gadget has to pair again (remove the old entry in
# MausBot -> Settings -> Remote access).
CONFIG_GADGET_NVS_ENCRYPT=y
CONFIG_NVS_ENCRYPTION=y
CONFIG_NVS_SEC_KEY_PROTECT_USING_HMAC=y
CONFIG_NVS_SEC_HMAC_EFUSE_KEY_ID=5
```

`firmware/ports/esp32/sdkconfig.test-keys`:

```text
# SPDX-License-Identifier: Apache-2.0
# Bench OTA tests only (docs/hardware-checklist.md): tools/fake-host, or
# MausBot's Update button against a local, never-published dev release.
# Applied only with -D GADGET_TEST_KEYS=1, in its own build directory. Such an
# image trusts the test key t1, whose private half is committed in keys/:
# never publish it and never leave it on a gadget you use. Release CI rejects
# a key table with anything but r* keys.
CONFIG_GADGET_TEST_KEYS=y
```

`firmware/ports/esp32/main/idf_component.yml`:

```yaml
## SPDX-License-Identifier: Apache-2.0
## Managed components for the gadget firmware (contract §1.4). Fetched at
## build time under their own licenses; never copied into this repository.
## No Waveshare BSP and no esp_lvgl_port (single-threaded LVGL, spec §5.1).
dependencies:
  idf: ">=6.0.3,<6.1"
  lvgl/lvgl: "9.6.0~1"
  espressif/cjson: "^1.7.19"
  espressif/esp_websocket_client: "^1.8.0"
  espressif/mdns: "^1.14.0"
  espressif/esp_codec_dev: "^1.6.2"
  espressif/esp_lcd_co5300: "^2.2.0"
  waveshare/esp_lcd_touch_cst9217: "^2.0.0"
  espressif/esp_lcd_touch: "^1.2.1"
```

`firmware/ports/esp32/main/Kconfig.projbuild`:

```text
# SPDX-License-Identifier: Apache-2.0
menu "OpenMausBot gadget"

    config GADGET_BOARD_ID
        string "Board id"
        default ""
        help
            Set by boards/<id>/sdkconfig.defaults; must equal the GADGET_BOARD
            the project was configured with and a row of core's board table.

    config GADGET_ART_PROFILE
        string "Maus art profile (s240 or s150)"
        default "s240"
        help
            Which generated art profile firmware/ui compiles. Set by
            boards/<id>/sdkconfig.defaults from the board's art profile.

    config GADGET_TEST_KEYS
        bool "Trust the test signing key t1 for OTA (never in board builds)"
        default n
        help
            Compiles the test key t1 into core's key table. Only the simulator
            and tests use it. Every board's sdkconfig.defaults keeps it off and
            release CI checks that the shipped key table holds only r* keys.

    config GADGET_NVS_ENCRYPT
        bool "Encrypt NVS with an HMAC key in eFuse block 5 (permanent)"
        default n
        help
            Encrypts the NVS partition (identity key, Wi-Fi, pairing) with
            ESP-IDF's HMAC scheme. On first boot this BURNS an HMAC key into
            eFuse key block 5, which cannot be undone; a build without this
            option can no longer read the board's NVS. Needs NVS_ENCRYPTION,
            NVS_SEC_KEY_PROTECT_USING_HMAC and NVS_SEC_HMAC_EFUSE_KEY_ID=5:
            build with -D GADGET_NVS_ENCRYPT=1 to apply sdkconfig.nvs-encrypt.

endmenu
```

First version of the component (later tasks extend the source lists; Task 13 gives the final file):

`firmware/ports/esp32/main/CMakeLists.txt`:

```cmake
# SPDX-License-Identifier: Apache-2.0
# The ESP32 port: HAL implementations, the main loop, LVGL glue, the pure
# logic shared with host-tests/ (logic/), the board's own glue
# (boards/<board>/board.c) and the drivers its optional boards/<board>/board.cmake
# lists (every driver in drivers/ when it has none).
idf_build_get_property(gadget_board GADGET_BOARD)
set(board_dir "${CMAKE_CURRENT_LIST_DIR}/../boards/${gadget_board}")
set(BOARD_DRIVER_SRCS "")
# board.cmake is optional (a size optimisation): without it every shared driver
# compiles, and --gc-sections drops the ones the board never calls.
include("${board_dir}/board.cmake" OPTIONAL RESULT_VARIABLE board_cmake)
if(NOT board_cmake)
  file(GLOB BOARD_DRIVER_SRCS RELATIVE "${CMAKE_CURRENT_LIST_DIR}/drivers" "${CMAKE_CURRENT_LIST_DIR}/drivers/*.c")
endif()
list(TRANSFORM BOARD_DRIVER_SRCS PREPEND "drivers/")

set(port_srcs
  main.c)

set(logic_srcs)

set(port_include_dirs "." "${board_dir}")

idf_component_register(
  SRCS ${port_srcs} ${logic_srcs} ${BOARD_DRIVER_SRCS}
  INCLUDE_DIRS ${port_include_dirs}
  REQUIRES core ui lvgl__lvgl espressif__cjson mbedtls nvs_flash esp_wifi esp_netif esp_event esp_timer
           esp_driver_gpio esp_driver_i2c esp_driver_i2s esp_driver_spi esp_driver_ledc esp_driver_usb_serial_jtag
           esp_adc esp_lcd app_update spi_flash esp_app_format
           espressif__esp_websocket_client espressif__mdns espressif__esp_codec_dev
           espressif__esp_lcd_co5300 waveshare__esp_lcd_touch_cst9217 espressif__esp_lcd_touch)

if(NOT CMAKE_BUILD_EARLY_EXPANSION AND NOT "${CONFIG_GADGET_BOARD_ID}" STREQUAL "${gadget_board}")
  message(FATAL_ERROR "sdkconfig says board '${CONFIG_GADGET_BOARD_ID}' but GADGET_BOARD is '${gadget_board}'. "
                      "Use -D SDKCONFIG=build/${gadget_board}/sdkconfig (one sdkconfig per board).")
endif()
```

Smoke app, replaced in Task 13:

`firmware/ports/esp32/main/main.c`:

```c
/* SPDX-License-Identifier: Apache-2.0 */
/* Toolchain smoke app: proves that ESP-IDF, the managed components, core
 * and ui build for this board, and prints what it runs on. */
#include <inttypes.h>

#include "esp_app_desc.h"
#include "esp_flash.h"
#include "esp_log.h"
#include "gadget_board.h"
#include "sdkconfig.h"

static const char *TAG = "main";

void app_main(void) {
  const gadget_board_t *board = gadget_board_by_id(CONFIG_GADGET_BOARD_ID);
  uint32_t bytes = 0;
  esp_flash_get_physical_size(esp_flash_default_chip, &bytes);
  ESP_LOGI(TAG, "%s (%s), firmware %s, flash %" PRIu32 " MB fitted", board != NULL ? board->display_name : "unknown board",
           CONFIG_GADGET_BOARD_ID, esp_app_get_description()->version, bytes / (1024u * 1024u));
}
```

The four pin maps (from the Waveshare schematics of each board; the devkit wiring is ours and avoids flash/PSRAM, USB, UART0, strapping and LED pins). The two AMOLED boards also carry their CO5300 register init table here (spec §5.3 keeps init sequences in `board.h`); Task 12's `board.c` passes it to the shared driver:

`firmware/ports/esp32/boards/amoled-175c/board.h`:

```c
/* SPDX-License-Identifier: Apache-2.0 */
/* Waveshare ESP32-S3-Touch-AMOLED-1.75C. Pins from the Waveshare schematic
 * for this board; GPIO numbers are plain ints so this header needs no
 * includes. 8 MB octal PSRAM; built with the 16 MB flash layout (some units
 * carry 32 MB; main.c logs the fitted size). */
#ifndef BOARD_H
#define BOARD_H

/* Display: CO5300 466x466 round AMOLED on QSPI (SPI2) */
#define BOARD_LCD_SCLK 38
#define BOARD_LCD_D0 4
#define BOARD_LCD_D1 5
#define BOARD_LCD_D2 6
#define BOARD_LCD_D3 7
#define BOARD_LCD_CS 12
#define BOARD_LCD_RST 1
#define BOARD_LCD_TE 13              /* tearing-effect output, unused in v1 */
#define BOARD_LCD_X_GAP 6            /* visible columns start at 6 */
#define BOARD_LCD_EVEN_AREAS 1       /* CO5300: areas start even and end odd */
#define BOARD_LCD_BUF_LINES 40       /* two 466 x 40 x 2 B internal DMA buffers */

/* CO5300 register writes for this 466x466 panel, from the vendor's published
 * panel bring-up: {command, data, data bytes, delay ms after}. The column
 * window 6..471 matches BOARD_LCD_X_GAP. board.c turns this into a
 * co5300_lcd_init_cmd_t array; NULL there selects the component's table. */
#define BOARD_CO5300_INIT_CMDS {                                                       \
    {0xFE, (const uint8_t[]){0x20}, 1, 0},                     /* manufacturer page */ \
    {0x19, (const uint8_t[]){0x10}, 1, 0},                                             \
    {0x1C, (const uint8_t[]){0xA0}, 1, 0},                                             \
    {0xFE, (const uint8_t[]){0x00}, 1, 0},                     /* user command page */ \
    {0xC4, (const uint8_t[]){0x80}, 1, 0},                     /* QSPI interface */    \
    {0x3A, (const uint8_t[]){0x55}, 1, 0},                     /* 16 bits per pixel */ \
    {0x35, (const uint8_t[]){0x00}, 1, 0},                     /* tearing effect on */ \
    {0x53, (const uint8_t[]){0x20}, 1, 0},                     /* brightness control */\
    {0x51, (const uint8_t[]){0xFF}, 1, 0},                     /* brightness */        \
    {0x63, (const uint8_t[]){0xFF}, 1, 0},                     /* HBM level */         \
    {0x2A, (const uint8_t[]){0x00, 0x06, 0x01, 0xD7}, 4, 0},   /* columns 6..471 */    \
    {0x2B, (const uint8_t[]){0x00, 0x00, 0x01, 0xD1}, 4, 600}, /* rows 0..465 */       \
    {0x11, NULL, 0, 600},                                      /* sleep out */         \
    {0x29, NULL, 0, 0},                                        /* display on */        \
  }

/* Touch: CST9217 (0x5A) on the I2C bus */
#define BOARD_TP_RST 2
#define BOARD_TP_INT 11
#define BOARD_TP_MIRROR_X 1
#define BOARD_TP_MIRROR_Y 1

/* I2C: ES8311 0x18, ES7210 0x40, AXP2101 0x34, QMI8658 0x6B, CST9217 0x5A; 2.2 k pull-ups */
#define BOARD_I2C_PORT 0
#define BOARD_I2C_SDA 15
#define BOARD_I2C_SCL 14

/* Audio: ES8311 DAC + ES7210 ADC on one I2S port, shared clock → 16 kHz duplex */
#define BOARD_I2S_PORT 0
#define BOARD_I2S_MCLK 16
#define BOARD_I2S_BCLK 9
#define BOARD_I2S_WS 45
#define BOARD_I2S_DOUT 8             /* to ES8311 DSDIN */
#define BOARD_I2S_DIN 10             /* from ES7210 SDOUT1 */
#define BOARD_PA_EN 46               /* NS4150B amplifier, active high */
#define BOARD_MIC_CHANNEL_MASK 0x1   /* MIC1, left slot. MIC3 is the echo reference (v2) */
#define BOARD_MIC_GAIN_DB 30.0f

/* Buttons. PWR is the AXP2101 power key, mirrored to GPIO3 through a
 * transistor: GPIO3 reads high while PWR is pressed. Holding PWR for 6 s
 * makes the AXP2101 cut power (spec §5.3). */
#define BOARD_BTN_TALK 0             /* BOOT, active low, external pull-up */
#define BOARD_BTN_CANCEL 3           /* PWR mirror, active high */

#endif /* BOARD_H */
```

`firmware/ports/esp32/boards/amoled-175/board.h`:

```c
/* SPDX-License-Identifier: Apache-2.0 */
/* Waveshare ESP32-S3-Touch-AMOLED-1.75. Pins from the Waveshare schematic
 * for this board. 16 MB flash, 8 MB octal PSRAM. Differs from the 1.75C in
 * the LCD reset, touch reset and MCLK pins; the PWR key is only readable
 * through the TCA9554 expander (EXIO4), so v1 has no CANCEL button here. */
#ifndef BOARD_H
#define BOARD_H

/* Display: CO5300 466x466 round AMOLED on QSPI (SPI2) */
#define BOARD_LCD_SCLK 38
#define BOARD_LCD_D0 4
#define BOARD_LCD_D1 5
#define BOARD_LCD_D2 6
#define BOARD_LCD_D3 7
#define BOARD_LCD_CS 12
#define BOARD_LCD_RST 39
#define BOARD_LCD_TE 13
#define BOARD_LCD_X_GAP 6
#define BOARD_LCD_EVEN_AREAS 1
#define BOARD_LCD_BUF_LINES 40

/* CO5300 register writes for this 466x466 panel, from the vendor's published
 * panel bring-up (the same panel as the 1.75C): {command, data, data bytes,
 * delay ms after}. Columns 6..471 match BOARD_LCD_X_GAP. */
#define BOARD_CO5300_INIT_CMDS {                                                       \
    {0xFE, (const uint8_t[]){0x20}, 1, 0},                     /* manufacturer page */ \
    {0x19, (const uint8_t[]){0x10}, 1, 0},                                             \
    {0x1C, (const uint8_t[]){0xA0}, 1, 0},                                             \
    {0xFE, (const uint8_t[]){0x00}, 1, 0},                     /* user command page */ \
    {0xC4, (const uint8_t[]){0x80}, 1, 0},                     /* QSPI interface */    \
    {0x3A, (const uint8_t[]){0x55}, 1, 0},                     /* 16 bits per pixel */ \
    {0x35, (const uint8_t[]){0x00}, 1, 0},                     /* tearing effect on */ \
    {0x53, (const uint8_t[]){0x20}, 1, 0},                     /* brightness control */\
    {0x51, (const uint8_t[]){0xFF}, 1, 0},                     /* brightness */        \
    {0x63, (const uint8_t[]){0xFF}, 1, 0},                     /* HBM level */         \
    {0x2A, (const uint8_t[]){0x00, 0x06, 0x01, 0xD7}, 4, 0},   /* columns 6..471 */    \
    {0x2B, (const uint8_t[]){0x00, 0x00, 0x01, 0xD1}, 4, 600}, /* rows 0..465 */       \
    {0x11, NULL, 0, 600},                                      /* sleep out */         \
    {0x29, NULL, 0, 0},                                        /* display on */        \
  }

/* Touch: CST9217 (0x5A) */
#define BOARD_TP_RST 40
#define BOARD_TP_INT 11
#define BOARD_TP_MIRROR_X 1
#define BOARD_TP_MIRROR_Y 1

/* I2C: AXP2101 0x34, ES8311 0x18, ES7210 0x40, QMI8658 0x6B, PCF85063 0x51, TCA9554 0x20 */
#define BOARD_I2C_PORT 0
#define BOARD_I2C_SDA 15
#define BOARD_I2C_SCL 14

/* Audio: ES8311 + ES7210, shared I2S clock → 16 kHz duplex; speaker on the MX1.25 connector */
#define BOARD_I2S_PORT 0
#define BOARD_I2S_MCLK 42
#define BOARD_I2S_BCLK 9
#define BOARD_I2S_WS 45
#define BOARD_I2S_DOUT 8
#define BOARD_I2S_DIN 10
#define BOARD_PA_EN 46
#define BOARD_MIC_CHANNEL_MASK 0x1
#define BOARD_MIC_GAIN_DB 30.0f

/* Buttons */
#define BOARD_BTN_TALK 0             /* BOOT, active low */

#endif /* BOARD_H */
```

`firmware/ports/esp32/boards/lcd-154/board.h`:

```c
/* SPDX-License-Identifier: Apache-2.0 */
/* Waveshare ESP32-S3-LCD-1.54. Pins from the Waveshare schematic for this
 * board. 16 MB flash, 8 MB octal PSRAM, no touch. The board has a power
 * latch: BAT_EN must be driven high at once or it switches off on battery
 * when PWR is released (spec §5.3). */
#ifndef BOARD_H
#define BOARD_H

/* Power and battery */
#define BOARD_BAT_EN 2               /* latch, drive high in board_early_init() */
#define BOARD_BAT_ADC 1              /* ADC1 channel 0, behind a 200 k / 100 k divider (x3) */
#define BOARD_BAT_DIVIDER_X1000 3000
#define BOARD_CHG_STAT 3             /* low while charging */

/* Display: ST7789 240x240 on SPI2, mode 3, colours inverted (IPS) */
#define BOARD_LCD_SCLK 38
#define BOARD_LCD_MOSI 39
#define BOARD_LCD_CS 21
#define BOARD_LCD_DC 45
#define BOARD_LCD_RST 40
#define BOARD_LCD_BL 46              /* LEDC PWM, active high */
#define BOARD_LCD_SPI_MODE 3
#define BOARD_LCD_PCLK_HZ 40000000
#define BOARD_LCD_X_GAP 0
#define BOARD_LCD_Y_GAP 0            /* set to 80 if the picture shows shifted (checklist) */
#define BOARD_LCD_BUF_LINES 40

/* I2C: ES8311 0x18, ES7210 0x40, QMI8658 0x6B */
#define BOARD_I2C_PORT 0
#define BOARD_I2C_SDA 42
#define BOARD_I2C_SCL 41

/* Audio: ES8311 + ES7210, shared I2S clock → 16 kHz duplex; onboard speaker */
#define BOARD_I2S_PORT 0
#define BOARD_I2S_MCLK 8
#define BOARD_I2S_BCLK 9
#define BOARD_I2S_WS 10
#define BOARD_I2S_DOUT 12            /* to ES8311 DSDIN */
#define BOARD_I2S_DIN 11             /* from ES7210 ASDOUT */
#define BOARD_PA_EN 7                /* NS4150B, active high */
#define BOARD_MIC_CHANNEL_MASK 0x1
#define BOARD_MIC_GAIN_DB 30.0f

/* Buttons, all active low: BOOT (KEY_MINUS) = TALK, PLUS = CANCEL. PWR
 * (GPIO5) switches the board on and is not an input in v1. */
#define BOARD_BTN_TALK 0
#define BOARD_BTN_CANCEL 4

#endif /* BOARD_H */
```

`firmware/ports/esp32/boards/devkit/board.h`:

```c
/* SPDX-License-Identifier: Apache-2.0 */
/* Breadboard build: ESP32-S3-DevKitC-1-N16R8 (16 MB flash, 8 MB octal
 * PSRAM), a 2" ST7789 240x320 module used as 320x240 landscape, an INMP441
 * I2S microphone and a MAX98357A I2S amplifier. The wiring is ours; it
 * avoids flash/PSRAM pins (26-37), USB (19/20), UART0 (43/44), strapping
 * pins 3/45/46 and the RGB LED (38/48). Use the USB-C port labelled USB
 * (native USB-Serial-JTAG), not the one labelled UART. */
#ifndef BOARD_H
#define BOARD_H

/* Display: ST7789 on SPI2 */
#define BOARD_LCD_SCLK 12
#define BOARD_LCD_MOSI 11
#define BOARD_LCD_CS 10
#define BOARD_LCD_DC 9
#define BOARD_LCD_RST 14
#define BOARD_LCD_BL 21
#define BOARD_LCD_SPI_MODE 0
#define BOARD_LCD_PCLK_HZ 40000000
#define BOARD_LCD_SWAP_XY 1          /* landscape */
#define BOARD_LCD_MIRROR_X 1
#define BOARD_LCD_MIRROR_Y 0
#define BOARD_LCD_X_GAP 0
#define BOARD_LCD_Y_GAP 0
#define BOARD_LCD_BUF_LINES 40

/* INMP441 microphone on I2S1 (L/R tied to GND → left slot) */
#define BOARD_MIC_I2S_PORT 1
#define BOARD_MIC_BCLK 4
#define BOARD_MIC_WS 5
#define BOARD_MIC_SD 6
#define BOARD_MIC_SHIFT 14

/* MAX98357A amplifier on I2S0 (separate clock → 24 kHz speaker) */
#define BOARD_SPK_I2S_PORT 0
#define BOARD_SPK_BCLK 15
#define BOARD_SPK_LRC 16
#define BOARD_SPK_DIN 17
#define BOARD_SPK_RATE 24000

/* Buttons, active low with internal pull-ups */
#define BOARD_BTN_TALK 0             /* BOOT */
#define BOARD_BTN_CANCEL 1           /* push button from GPIO1 to GND */

#endif /* BOARD_H */
```

Write this same file four times, as `boards/amoled-175c/board.cmake`, `boards/amoled-175/board.cmake`, `boards/lcd-154/board.cmake` and `boards/devkit/board.cmake` (Tasks 8–10 add each board's drivers). The file is optional: it keeps each image free of other boards' drivers, but a board directory without it still configures and builds, so a maker who adds a board with only `board.h`, `board.c` and `sdkconfig.defaults` (P2d's "add a board") gets a working build:

`firmware/ports/esp32/boards/<board>/board.cmake`:

```cmake
# SPDX-License-Identifier: Apache-2.0
# Drivers from main/drivers/ that this board's board.c uses (none yet).
set(BOARD_DRIVER_SRCS)
```

`firmware/ports/esp32/tools/build-all.sh`:

```bash
#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# Usage: tools/build-all.sh [board...]     (default: every directory in boards/)
# Builds each board in build/<board> with its own sdkconfig (spec §5.1), then
# runs check-size.sh and check-art-profile.sh, and fails when a standard build
# has test keys or NVS encryption on (those belong in their own -B directory).
# Needs ESP-IDF v6.0.3 active (idf.py on PATH). Extra idf.py arguments can be
# passed in IDF_ARGS.
set -euo pipefail

esp_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$esp_dir"
if (($# == 0)); then
  # shellcheck disable=SC2046
  set -- $(ls boards)
fi
mkdir -p build
for board in "$@"; do
  log="build/$board.log"
  echo "== $board"
  # shellcheck disable=SC2086
  if ! idf.py -B "build/$board" -D GADGET_BOARD="$board" -D SDKCONFIG="build/$board/sdkconfig" ${IDF_ARGS:-} build >"$log" 2>&1; then
    tail -n 60 "$log"
    echo "build-all: $board failed; full log in $esp_dir/$log" >&2
    exit 1
  fi
  tools/check-size.sh "$board"
  grep -qxF '# CONFIG_GADGET_TEST_KEYS is not set' "build/$board/sdkconfig" &&
    grep -qxF '# CONFIG_GADGET_NVS_ENCRYPT is not set' "build/$board/sdkconfig" ||
    { echo "build-all: $board has test keys or NVS encryption on" >&2; exit 1; }
  if [[ "${SKIP_ART_CHECK:-0}" != "1" ]]; then
    tools/check-art-profile.sh "$board"
  fi
done
```

Run: `chmod +x firmware/ports/esp32/tools/build-all.sh`

- [ ] **Step 8: Check the project's guards and the early expansion of core and ui**

```bash
. ~/.espressif/tools/activate_idf_v6.0.3.sh
cd firmware/ports/esp32
idf.py -B build/guard-none -D SDKCONFIG=build/guard-none/sdkconfig reconfigure; echo "exit $?"
idf.py -B build/guard-nope -D GADGET_BOARD=nope -D SDKCONFIG=build/guard-nope/sdkconfig reconfigure; echo "exit $?"
idf.py -B build/lcd-154 -D GADGET_BOARD=lcd-154 -D SDKCONFIG=build/lcd-154/sdkconfig -D GADGET_TEST_KEYS=1 reconfigure; echo "exit $?"
rm -rf build/guard-none build/guard-nope build/lcd-154
idf.py -B build/amoled-175c -D GADGET_BOARD=amoled-175c -D SDKCONFIG=build/amoled-175c/sdkconfig reconfigure; echo "exit $?"
cd ../../..
```

Expected: the first run stops with `CMake Error ... GADGET_BOARD is not set.  Pass -D GADGET_BOARD=<board>, one of the directories in boards/.`, the second with `Unknown board 'nope': there is no directory boards/nope.`, the third with `Build test-key or NVS-encryption variants in their own -B directory, not build/lcd-154` (the failed configure still caches `GADGET_TEST_KEYS`, which is why the `rm -rf` removes `build/lcd-154`); those three print a non-zero `exit` status. The last run downloads the managed components and prints `exit 0`: ESP-IDF's early expansion read core, ui (through P2b's guard, checked in Step 6b) and main without an error. If it stops with `CONFIGURE_DEPENDS is invalid for script and find package modes` or `CONFIG_GADGET_ART_PROFILE must be s240 or s150, not ''`, P2b's early-expansion guard in `firmware/ui/CMakeLists.txt` is missing or changed: stop and report it to P2b (contract §2.17); do not edit that file.

- [ ] **Step 9: Build all four boards**

Run:

```bash
. ~/.espressif/tools/activate_idf_v6.0.3.sh
SKIP_ART_CHECK=1 firmware/ports/esp32/tools/build-all.sh
```

Expected, for each of `amoled-175`, `amoled-175c`, `devkit`, `lcd-154` (alphabetical): `== <board>` followed by `check-size: <board> ok, <n> of 6291456 bytes (<p>%)`; exit status 0. The first build downloads the managed components into `firmware/ports/esp32/managed_components/` (gitignored) and compiles core and ui for the device for the first time. If core or ui fail to compile, follow "If core or ui sources fail" above. One exception is ours, not P2b's: if the ui build stops with `LVGL config: LV_CACHE_DEF_SIZE and LV_IMAGE_HEADER_CACHE_DEF_CNT must be 0 (image caches off)`, the generated `build/<board>/sdkconfig` does not carry Step 3's LVGL pins (Step 4's host test proves the defaults files do; an existing sdkconfig wins over them), so it is stale or was changed with `menuconfig`: delete `build/<board>` and run the build again.

Then check that a stale sdkconfig from another board is refused, both as a copied file and as board A's own build directory reused for board B (Review Focus 1), and put `build/amoled-175c` back:

```bash
cd firmware/ports/esp32
mkdir -p build/guard-mix && cp build/amoled-175c/sdkconfig build/guard-mix/sdkconfig
idf.py -B build/guard-mix -D GADGET_BOARD=lcd-154 -D SDKCONFIG=build/guard-mix/sdkconfig reconfigure; echo "exit $?"
rm -rf build/guard-mix
idf.py -B build/amoled-175c -D GADGET_BOARD=lcd-154 reconfigure; echo "exit $?"
idf.py -B build/amoled-175c -D GADGET_BOARD=amoled-175c -D SDKCONFIG=build/amoled-175c/sdkconfig reconfigure; echo "exit $?"
cd ../../..
```

Expected: the first two runs stop with `CMake Error ... sdkconfig says board 'amoled-175c' but GADGET_BOARD is 'lcd-154'.` and a non-zero `exit` status (the second reuses the `SDKCONFIG` cached in `build/amoled-175c`); the last prints `exit 0`.

- [ ] **Step 10: Check the lock files**

```bash
ls firmware/ports/esp32/dependencies.lock.*
test ! -e firmware/ports/esp32/dependencies.lock && echo "no shared lock file"
grep -A10 '^  lvgl/lvgl:' firmware/ports/esp32/dependencies.lock.amoled-175c | grep -m1 '^    version:'
git status --short firmware/ports/esp32
```

Expected: four files `dependencies.lock.amoled-175`, `dependencies.lock.amoled-175c`, `dependencies.lock.devkit`, `dependencies.lock.lcd-154`; `no shared lock file`; `    version: 9.6.0~1` (the component manager writes each entry's keys sorted, so `version:` comes sixth, after `component_hash`, `dependencies`, `source`, `registry_url` and `type`); `git status` lists only files this task created (no `build/`, `managed_components/` or `sdkconfig`, which P1's `.gitignore` covers).

- [ ] **Step 11: Commit**

```bash
git add firmware/ports/esp32/CMakeLists.txt \
  firmware/ports/esp32/sdkconfig.defaults \
  firmware/ports/esp32/sdkconfig.nvs-encrypt \
  firmware/ports/esp32/sdkconfig.test-keys \
  firmware/ports/esp32/dependencies.lock.amoled-175c \
  firmware/ports/esp32/dependencies.lock.amoled-175 \
  firmware/ports/esp32/dependencies.lock.lcd-154 \
  firmware/ports/esp32/dependencies.lock.devkit \
  firmware/ports/esp32/main \
  firmware/ports/esp32/boards \
  firmware/ports/esp32/tools/build-all.sh \
  firmware/ports/esp32/host-tests
git commit -m "feat(esp32): ESP-IDF v6.0.3 project, board defaults and pin maps for four boards"
```
If Step 6 changed `firmware/core/CMakeLists.txt`, add it to the same commit.

### Task 3: Event queue, system HAL and the USB console

**Files:**
- Create: `firmware/ports/esp32/main/logic/pl_event.h`, `firmware/ports/esp32/main/logic/pl_event.c`
- Create: `firmware/ports/esp32/host-tests/test_pl_event.c`
- Create: `firmware/ports/esp32/main/board_api.h`, `firmware/ports/esp32/main/port.h`
- Create: `firmware/ports/esp32/main/port_events.c`, `firmware/ports/esp32/main/hal_system.c`, `firmware/ports/esp32/main/console_usj.c`
- Modify: `firmware/ports/esp32/main/CMakeLists.txt` (source lists, include dirs), `firmware/ports/esp32/host-tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `gadget_event_t` and payload types (contract §2.4); `core_event()` (§2.7); `gadget_linebuf_t`, `gadget_linebuf_init()`, `gadget_linebuf_feed()` (§2.11); `hal_*` system prototypes (§2.5).
- Produces:
  - `gadget_status_t pl_event_copy(gadget_event_t *dst, const gadget_event_t *src)` (deep copy; on failure `*dst` is zeroed) and `void pl_event_free(gadget_event_t *ev)`.
  - `board_api.h`: `board_audio_t {mic_rate, spk_rate, spk_latency_ms, mic_read, spk_write, spk_set_volume, spk_set_mute}` and the nine `board_*` functions of contract §2.17.
  - `port.h`: `port_wifi_msg_t {kind, local, auth_failed, ip[16]}` with `PORT_WIFI_GOT_IP/DISCONNECTED/SCAN_DONE`; `port_events_init()`, `port_set_main_task()`, `bool port_post_event(const gadget_event_t*)`, `bool port_post_wifi(const port_wifi_msg_t*)`, `void port_drain(void)`; `port_system_init()`, `port_set_board()`, `port_board()`; `port_console_start()`; and the init/hook functions later tasks define: `port_wifi_start/on_msg/tick` (Task 5), `port_ws_init/note` (Task 6), `port_mdns_init` (Task 7), `port_audio_start` (Task 8), `port_input_init/poll` (Task 9), `port_display_init/touch` (Task 10), `port_ota_init` (Task 11).
  - HAL: `hal_now_ms`, `hal_restart`, `hal_log`, `hal_vlog`, `hal_log_set_enabled`, `hal_console_write`.
  - `port_drain()` calls `port_ws_note()` before core sees `GADGET_EV_WS_OPEN`/`GADGET_EV_WS_CLOSED`, and hands `port_wifi_msg_t` to `port_wifi_on_msg()`. These objects are compiled now and linked from Task 13.

- [ ] **Step 1: Write the failing test**

`firmware/ports/esp32/host-tests/test_pl_event.c`:

```c
/* SPDX-License-Identifier: Apache-2.0 */
#include <string.h>

#include "pl_event.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

static void test_mic_frame_is_deep_copied(void) {
  int16_t pcm[GADGET_MIC_FRAME_SAMPLES];
  for (unsigned i = 0; i < GADGET_MIC_FRAME_SAMPLES; i++) pcm[i] = (int16_t)(i * 3 - 400);
  gadget_event_t src = {.type = GADGET_EV_MIC_FRAME};
  src.u.mic.pcm = pcm;
  src.u.mic.samples = GADGET_MIC_FRAME_SAMPLES;
  gadget_event_t dst;
  TEST_ASSERT_EQUAL(GADGET_OK, pl_event_copy(&dst, &src));
  TEST_ASSERT_TRUE(dst.u.mic.pcm != pcm);
  pcm[5] = 0; /* the copy must not follow later changes to the source */
  TEST_ASSERT_EQUAL_INT16(-385, dst.u.mic.pcm[5]);
  TEST_ASSERT_EQUAL_INT16_ARRAY(pcm + 6, dst.u.mic.pcm + 6, GADGET_MIC_FRAME_SAMPLES - 6);
  pl_event_free(&dst);
  TEST_ASSERT_NULL(dst.u.mic.pcm);
}

static void test_ws_text_including_empty(void) {
  const uint8_t text[] = "{\"op\":\"ready\"}";
  gadget_event_t src = {.type = GADGET_EV_WS_TEXT};
  src.u.ws.data = text;
  src.u.ws.len = sizeof(text) - 1;
  gadget_event_t dst;
  TEST_ASSERT_EQUAL(GADGET_OK, pl_event_copy(&dst, &src));
  TEST_ASSERT_TRUE(dst.u.ws.data != text);
  TEST_ASSERT_EQUAL_size_t(sizeof(text) - 1, dst.u.ws.len);
  TEST_ASSERT_EQUAL_MEMORY(text, dst.u.ws.data, sizeof(text) - 1);
  pl_event_free(&dst);

  gadget_event_t empty = {.type = GADGET_EV_WS_BINARY};
  empty.u.ws.data = NULL;
  empty.u.ws.len = 0;
  TEST_ASSERT_EQUAL(GADGET_OK, pl_event_copy(&dst, &empty));
  TEST_ASSERT_NOT_NULL(dst.u.ws.data);
  TEST_ASSERT_EQUAL_size_t(0, dst.u.ws.len);
  pl_event_free(&dst);
}

static void test_scan_mdns_console_payloads(void) {
  gadget_wifi_ap_t aps[2] = {{"Home", -40, GADGET_AUTH_WPA2}, {"Cafe", -80, GADGET_AUTH_OPEN}};
  gadget_event_t src = {.type = GADGET_EV_WIFI_SCAN};
  src.u.scan.aps = aps;
  src.u.scan.count = 2;
  src.u.scan.ok = true;
  gadget_event_t dst;
  TEST_ASSERT_EQUAL(GADGET_OK, pl_event_copy(&dst, &src));
  TEST_ASSERT_TRUE(dst.u.scan.aps != aps);
  TEST_ASSERT_EQUAL_STRING("Cafe", dst.u.scan.aps[1].ssid);
  TEST_ASSERT_TRUE(dst.u.scan.ok);
  pl_event_free(&dst);

  gadget_mdns_host_t hosts[1] = {{"Omkar's computer", "192.168.1.20:8810", "000102030405060708090a0b0c0d0e0f"}};
  gadget_event_t m = {.type = GADGET_EV_MDNS};
  m.u.mdns.hosts = hosts;
  m.u.mdns.count = 1;
  m.u.mdns.ok = true;
  TEST_ASSERT_EQUAL(GADGET_OK, pl_event_copy(&dst, &m));
  TEST_ASSERT_EQUAL_STRING("192.168.1.20:8810", dst.u.mdns.hosts[0].address);
  pl_event_free(&dst);

  char line[] = "wifi \"My Net\" \"pass word\"";
  gadget_event_t c = {.type = GADGET_EV_CONSOLE_LINE};
  c.u.console.line = line;
  TEST_ASSERT_EQUAL(GADGET_OK, pl_event_copy(&dst, &c));
  line[0] = 'X';
  TEST_ASSERT_EQUAL_STRING("wifi \"My Net\" \"pass word\"", dst.u.console.line);
  pl_event_free(&dst);
}

static void test_value_events_need_no_free(void) {
  gadget_event_t src = {.type = GADGET_EV_WIFI_STATE};
  src.u.wifi.state = GADGET_WIFI_CONNECTED;
  strcpy(src.u.wifi.ip, "10.0.0.7");
  gadget_event_t dst;
  TEST_ASSERT_EQUAL(GADGET_OK, pl_event_copy(&dst, &src));
  TEST_ASSERT_EQUAL_STRING("10.0.0.7", dst.u.wifi.ip);
  pl_event_free(&dst);
  TEST_ASSERT_EQUAL(0, dst.type);
  pl_event_free(&dst); /* a zeroed event frees nothing */
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_mic_frame_is_deep_copied);
  RUN_TEST(test_ws_text_including_empty);
  RUN_TEST(test_scan_mdns_console_payloads);
  RUN_TEST(test_value_events_need_no_free);
  return UNITY_END();
}
```

Append to `firmware/ports/esp32/host-tests/CMakeLists.txt`:

```cmake
port_test(test_pl_event)
```

- [ ] **Step 2: Run it to verify it fails**

Run: `cmake -S firmware/ports/esp32/host-tests -B build/esp32-host && cmake --build build/esp32-host -j10 && ctest --test-dir build/esp32-host --output-on-failure`

Expected: FAIL — the build stops with `fatal error: 'pl_event.h' file not found` (clang; GCC prints `pl_event.h: No such file or directory`).

- [ ] **Step 3: Write the event copy**

`firmware/ports/esp32/main/logic/pl_event.h`:

```c
/* SPDX-License-Identifier: Apache-2.0 */
/* Deep copies of gadget events. Driver tasks copy an event before queueing
 * it for the main thread, because pointers inside a gadget_event_t are only
 * borrowed for one core_event() call (gadget_events.h). */
#ifndef PL_EVENT_H
#define PL_EVENT_H

#include "gadget_events.h"

/* Copy *src into *dst and duplicate every pointer payload (mic.pcm, ws.data,
 * scan.aps, mdns.hosts, console.line). On GADGET_ERR_NO_MEM *dst is zeroed
 * and owns nothing. */
gadget_status_t pl_event_copy(gadget_event_t *dst, const gadget_event_t *src);
/* Free what pl_event_copy() allocated and zero *ev. Safe on a zeroed event. */
void pl_event_free(gadget_event_t *ev);

#endif /* PL_EVENT_H */
```

`firmware/ports/esp32/main/logic/pl_event.c`:

```c
/* SPDX-License-Identifier: Apache-2.0 */
#include "pl_event.h"

#include <stdlib.h>
#include <string.h>

static void *dup_bytes(const void *src, size_t len) {
  void *p = malloc(len ? len : 1);
  if (p != NULL && len > 0) {
    memcpy(p, src, len);
  }
  return p;
}

gadget_status_t pl_event_copy(gadget_event_t *dst, const gadget_event_t *src) {
  void *p = NULL;
  *dst = *src;
  switch (src->type) {
    case GADGET_EV_MIC_FRAME:
      p = dup_bytes(src->u.mic.pcm, (size_t)src->u.mic.samples * sizeof(int16_t));
      dst->u.mic.pcm = p;
      break;
    case GADGET_EV_WS_TEXT:
    case GADGET_EV_WS_BINARY:
      p = dup_bytes(src->u.ws.data, src->u.ws.len);
      dst->u.ws.data = p;
      break;
    case GADGET_EV_WIFI_SCAN:
      p = dup_bytes(src->u.scan.aps, (size_t)src->u.scan.count * sizeof(gadget_wifi_ap_t));
      dst->u.scan.aps = p;
      break;
    case GADGET_EV_MDNS:
      p = dup_bytes(src->u.mdns.hosts, (size_t)src->u.mdns.count * sizeof(gadget_mdns_host_t));
      dst->u.mdns.hosts = p;
      break;
    case GADGET_EV_CONSOLE_LINE:
      p = dup_bytes(src->u.console.line, strlen(src->u.console.line) + 1);
      dst->u.console.line = p;
      break;
    default:
      return GADGET_OK;
  }
  if (p == NULL) {
    memset(dst, 0, sizeof(*dst));
    return GADGET_ERR_NO_MEM;
  }
  return GADGET_OK;
}

void pl_event_free(gadget_event_t *ev) {
  switch (ev->type) {
    case GADGET_EV_MIC_FRAME: free((void *)ev->u.mic.pcm); break;
    case GADGET_EV_WS_TEXT:
    case GADGET_EV_WS_BINARY: free((void *)ev->u.ws.data); break;
    case GADGET_EV_WIFI_SCAN: free((void *)ev->u.scan.aps); break;
    case GADGET_EV_MDNS: free((void *)ev->u.mdns.hosts); break;
    case GADGET_EV_CONSOLE_LINE: free((void *)ev->u.console.line); break;
    default: break;
  }
  memset(ev, 0, sizeof(*ev));
}
```

In `firmware/ports/esp32/host-tests/CMakeLists.txt`, replace the `set(PORT_LOGIC_SRCS ...)` block with:

```cmake
set(PORT_LOGIC_SRCS
  ${CORE_DIR}/src/boards.c
  ${ESP_DIR}/main/logic/pl_event.c
)
```

- [ ] **Step 4: Run the tests to verify they pass**

Run: `cmake -S firmware/ports/esp32/host-tests -B build/esp32-host && cmake --build build/esp32-host -j10 && ctest --test-dir build/esp32-host --output-on-failure`

Expected: `100% tests passed, 0 tests failed out of 3`.

- [ ] **Step 5: Write the port core: board API, port header, event queue, system HAL, console**

`firmware/ports/esp32/main/board_api.h`:

```c
/* SPDX-License-Identifier: Apache-2.0 */
/* What each boards/<id>/board.c implements (contract §2.17). main.c calls
 * these once at boot in the order of contract §2.17; hal_input.c and
 * hal_battery.c call the polling ones from the main thread. */
#ifndef BOARD_API_H
#define BOARD_API_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "esp_lcd_touch.h"
#include "esp_lcd_types.h"
#include "gadget_types.h"

/* The board's audio path, filled by board_audio_init(). hal_audio.c runs
 * one mic task and one speaker task on top of it. */
typedef struct board_audio {
  uint32_t mic_rate;          /* 16000 */
  uint32_t spk_rate;          /* 16000 on codec boards (shared I2S clock), 24000 on devkit */
  uint32_t spk_latency_ms;    /* audio the TX DMA ring holds once it is full */
  /* Read exactly `samples` mono PCM16 samples. Blocks; mic task only. */
  esp_err_t (*mic_read)(int16_t *pcm, size_t samples);
  /* Write mono PCM16. Blocks until the DMA ring accepted it; speaker task only. */
  esp_err_t (*spk_write)(const int16_t *pcm, size_t samples);
  /* Codec volume 0..100, or NULL: hal_audio.c scales samples in software. */
  void (*spk_set_volume)(uint8_t pct);
  /* Codec output mute, or NULL. hal_spk_stop() mutes so the audio still in
   * the TX DMA ring is not heard; the speaker task unmutes once that ring
   * holds only silence. */
  void (*spk_set_mute)(bool mute);
} board_audio_t;

/* First call in app_main: power latches and the I2C bus. */
esp_err_t board_early_init(void);
/* Panel IO + panel, reset, initialised and switched on. */
esp_err_t board_display_init(esp_lcd_panel_io_handle_t *io, esp_lcd_panel_handle_t *panel);
/* ESP_ERR_NOT_SUPPORTED on boards without touch. */
esp_err_t board_touch_init(esp_lcd_touch_handle_t *tp);
esp_err_t board_audio_init(board_audio_t *out);
esp_err_t board_buttons_init(void);
/* Raw levels, true = pressed; debounced by hal_input.c. */
bool board_talk_pressed(void);
bool board_cancel_pressed(void);
/* false when the board has no battery or none is fitted. */
bool board_battery_read(gadget_battery_t *out);
void board_set_brightness(uint8_t pct);

#endif /* BOARD_API_H */
```

`firmware/ports/esp32/main/port.h`:

```c
/* SPDX-License-Identifier: Apache-2.0 */
/* ESP32 port internals shared by main.c, display.c and the hal_*.c files.
 * Threading (gadget_hal.h): core, the UI and every hal_* call run on the
 * "gadget" task. Driver tasks hand results to that task through the port
 * queue (port_post_event / port_post_wifi) and never call core. */
#ifndef PORT_H
#define PORT_H

#include <stdbool.h>
#include <stdint.h>

#include "board_api.h"
#include "esp_err.h"
#include "gadget_board.h"
#include "gadget_events.h"

typedef enum { PORT_WIFI_GOT_IP = 1, PORT_WIFI_DISCONNECTED, PORT_WIFI_SCAN_DONE } port_wifi_kind_t;

typedef struct {
  port_wifi_kind_t kind;
  bool local;        /* DISCONNECTED: reason WIFI_REASON_ASSOC_LEAVE, our own disconnect */
  bool auth_failed;  /* DISCONNECTED: wrong password or handshake failure */
  char ip[16];       /* GOT_IP: dotted IPv4 */
} port_wifi_msg_t;

/* port_events.c */
esp_err_t port_events_init(void);
/* Remember the gadget task: posts from it never wait on a full queue. */
void port_set_main_task(void);
/* Any task. Deep-copies ev (pl_event_copy). Mic frames are dropped at once
 * when the queue is full; other events wait up to 2 s. */
bool port_post_event(const gadget_event_t *ev);
bool port_post_wifi(const port_wifi_msg_t *m);
/* Gadget task: deliver every queued message (core_event / hal_wifi.c). */
void port_drain(void);

/* hal_system.c */
esp_err_t port_system_init(void);   /* first call in app_main: console output lock */
void port_set_board(const gadget_board_t *board);
const gadget_board_t *port_board(void);

/* console_usj.c: USB-Serial-JTAG driver + reader task */
esp_err_t port_console_start(void);

/* hal_wifi.c */
esp_err_t port_wifi_start(void);   /* netif, event loop, STA mode, esp_wifi_start() */
void port_wifi_on_msg(const port_wifi_msg_t *m);
void port_wifi_tick(uint64_t now_ms);

/* hal_ws.c */
esp_err_t port_ws_init(void);
/* port_drain() calls this before core sees WS_OPEN / WS_CLOSED. */
void port_ws_note(gadget_event_type_t type);

/* hal_mdns.c */
esp_err_t port_mdns_init(void);

/* hal_audio.c: starts the mic and speaker tasks; without it the mic and
 * speaker HAL report GADGET_ERR_UNSUPPORTED / accept nothing. */
esp_err_t port_audio_start(const board_audio_t *audio);

/* hal_input.c: polled by the gadget task every 10 ms; calls core_event(). */
void port_input_init(const gadget_board_t *board, esp_lcd_touch_handle_t touch);
void port_input_poll(void);

/* hal_ota.c */
esp_err_t port_ota_init(void);

/* display.c: lv_init, the display, flush to the panel, the touch pointer */
esp_err_t port_display_init(const gadget_board_t *board, esp_lcd_panel_io_handle_t io, esp_lcd_panel_handle_t panel,
                            bool has_touch);
void port_display_touch(bool pressed, int16_t x, int16_t y);

#endif /* PORT_H */
```

`firmware/ports/esp32/main/port_events.c`:

```c
/* SPDX-License-Identifier: Apache-2.0 */
/* The one queue between driver tasks and the gadget task. */
#include <stdatomic.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "gadget_core.h"
#include "pl_event.h"
#include "port.h"

#define PORT_QUEUE_LEN 128
#define PORT_POST_WAIT_MS 2000

typedef enum { PORT_MSG_GADGET = 1, PORT_MSG_WIFI } port_msg_kind_t;

typedef struct {
  port_msg_kind_t kind;
  union {
    gadget_event_t ev;       /* owns its payload (pl_event_copy) */
    port_wifi_msg_t wifi;
  } u;
} port_msg_t;

static const char *TAG = "port";
static QueueHandle_t s_q;
static TaskHandle_t s_main_task;
static atomic_uint s_dropped;

esp_err_t port_events_init(void) {
  s_q = xQueueCreate(PORT_QUEUE_LEN, sizeof(port_msg_t));
  return s_q != NULL ? ESP_OK : ESP_ERR_NO_MEM;
}

void port_set_main_task(void) { s_main_task = xTaskGetCurrentTaskHandle(); }

static bool post(const port_msg_t *m, bool may_wait) {
  TickType_t wait = (may_wait && xTaskGetCurrentTaskHandle() != s_main_task) ? pdMS_TO_TICKS(PORT_POST_WAIT_MS) : 0;
  if (xQueueSend(s_q, m, wait) == pdTRUE) {
    return true;
  }
  atomic_fetch_add(&s_dropped, 1u);
  return false;
}

bool port_post_event(const gadget_event_t *ev) {
  port_msg_t m = {.kind = PORT_MSG_GADGET};
  if (pl_event_copy(&m.u.ev, ev) != GADGET_OK) {
    atomic_fetch_add(&s_dropped, 1u);
    return false;
  }
  if (!post(&m, ev->type != GADGET_EV_MIC_FRAME)) {
    pl_event_free(&m.u.ev);
    return false;
  }
  return true;
}

bool port_post_wifi(const port_wifi_msg_t *w) {
  port_msg_t m = {.kind = PORT_MSG_WIFI};
  m.u.wifi = *w;
  return post(&m, true);
}

void port_drain(void) {
  port_msg_t m;
  while (xQueueReceive(s_q, &m, 0) == pdTRUE) {
    if (m.kind == PORT_MSG_GADGET) {
      if (m.u.ev.type == GADGET_EV_WS_OPEN || m.u.ev.type == GADGET_EV_WS_CLOSED) {
        port_ws_note(m.u.ev.type);
      }
      core_event(&m.u.ev);
      pl_event_free(&m.u.ev);
    } else if (m.kind == PORT_MSG_WIFI) {
      port_wifi_on_msg(&m.u.wifi);
    }
  }
  unsigned dropped = atomic_exchange(&s_dropped, 0u);
  if (dropped > 0) {
    ESP_LOGW(TAG, "dropped %u queued events (queue full or out of memory)", dropped);
  }
}
```

`firmware/ports/esp32/main/hal_system.c`:

```c
/* SPDX-License-Identifier: Apache-2.0 */
/* HAL system group: clock, restart, log, console output. */
#include <stdarg.h>
#include <stdio.h>

#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "gadget_hal.h"
#include "port.h"
#include "sdkconfig.h"

static const gadget_board_t *s_board;
static SemaphoreHandle_t s_out_lock;

esp_err_t port_system_init(void) {
  s_out_lock = xSemaphoreCreateMutex();
  return s_out_lock != NULL ? ESP_OK : ESP_ERR_NO_MEM;
}

void port_set_board(const gadget_board_t *board) { s_board = board; }
const gadget_board_t *port_board(void) { return s_board; }

uint64_t hal_now_ms(void) { return (uint64_t)(esp_timer_get_time() / 1000); }

_Noreturn void hal_restart(void) {
  fflush(stdout);
  esp_restart();
}

void hal_vlog(gadget_log_level_t level, const char *tag, const char *fmt, va_list ap) {
  char buf[256];
  vsnprintf(buf, sizeof(buf), fmt, ap);
  switch (level) {
    case GADGET_LOG_ERROR: ESP_LOGE(tag, "%s", buf); break;
    case GADGET_LOG_WARN: ESP_LOGW(tag, "%s", buf); break;
    case GADGET_LOG_INFO: ESP_LOGI(tag, "%s", buf); break;
    default: ESP_LOGD(tag, "%s", buf); break;
  }
}

void hal_log(gadget_log_level_t level, const char *tag, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  hal_vlog(level, tag, fmt, ap);
  va_end(ap);
}

void hal_log_set_enabled(bool enabled) {
  esp_log_level_set("*", enabled ? (esp_log_level_t)CONFIG_LOG_DEFAULT_LEVEL : ESP_LOG_NONE);
}

/* @omb lines bypass the log system, so `log off` never hides them. The
 * mutex keeps two @omb lines from interleaving (the gadget task and the
 * console task both write); installer sessions send `log off` first, so
 * log lines do not interleave with them either. */
void hal_console_write(const char *line) {
  if (s_out_lock != NULL) {
    xSemaphoreTake(s_out_lock, portMAX_DELAY);
  }
  fputs(line, stdout);
  fputc('\n', stdout);
  fflush(stdout);
  if (s_out_lock != NULL) {
    xSemaphoreGive(s_out_lock);
  }
}
```

`firmware/ports/esp32/main/console_usj.c`:

```c
/* SPDX-License-Identifier: Apache-2.0 */
/* Console input over the native USB-Serial-JTAG port (spec §5.6). The
 * reader task splits bytes into lines (CR, LF or CRLF, gadget_console.h)
 * and posts each one to core as GADGET_EV_CONSOLE_LINE. Output goes through
 * stdout, which this switches to the interrupt-driven driver. */
#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "gadget_console.h"
#include "gadget_hal.h"
#include "port.h"

static gadget_linebuf_t s_lb; /* 8 KiB: static, not on the task stack */

static void on_line(const char *line, void *ctx) {
  (void)ctx;
  if (line == NULL) {
    hal_console_write("@omb {\"op\":\"error\",\"cmd\":\"\",\"message\":\"line too long\"}");
    return;
  }
  gadget_event_t ev = {.type = GADGET_EV_CONSOLE_LINE};
  ev.u.console.line = line;
  port_post_event(&ev);
}

static void console_task(void *arg) {
  (void)arg;
  char buf[128];
  for (;;) {
    int n = usb_serial_jtag_read_bytes(buf, sizeof(buf), pdMS_TO_TICKS(100));
    if (n > 0) {
      gadget_linebuf_feed(&s_lb, buf, (size_t)n, on_line, NULL);
    }
  }
}

esp_err_t port_console_start(void) {
  usb_serial_jtag_driver_config_t cfg = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
  cfg.rx_buffer_size = 1024;
  cfg.tx_buffer_size = 4096;
  esp_err_t err = usb_serial_jtag_driver_install(&cfg);
  if (err != ESP_OK) {
    return err;
  }
  usb_serial_jtag_vfs_use_driver();
  gadget_linebuf_init(&s_lb);
  BaseType_t ok = xTaskCreatePinnedToCore(console_task, "console", 4096, NULL, 2, NULL, 0);
  return ok == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}
```

In `firmware/ports/esp32/main/CMakeLists.txt`, replace the `set(port_srcs ...)` and `set(logic_srcs ...)` blocks with:

```cmake
set(port_srcs
  main.c
  port_events.c
  hal_system.c
  console_usj.c)

set(logic_srcs
  logic/pl_event.c)
```

and the `set(port_include_dirs ...)` line with:

```cmake
set(port_include_dirs "." "logic" "${board_dir}")
```

- [ ] **Step 6: Build all four boards**

Run:

```bash
. ~/.espressif/tools/activate_idf_v6.0.3.sh
SKIP_ART_CHECK=1 firmware/ports/esp32/tools/build-all.sh
```

Expected, for each of `amoled-175`, `amoled-175c`, `devkit`, `lcd-154` (alphabetical): `== <board>` followed by `check-size: <board> ok, <n> of 6291456 bytes (<p>%)`; exit status 0.

- [ ] **Step 7: Commit**

```bash
git add firmware/ports/esp32/main \
  firmware/ports/esp32/host-tests
git commit -m "feat(esp32): event queue, system HAL and USB-Serial-JTAG console"
```
### Task 4: Storage HAL on NVS, with opt-in encryption

**Files:**
- Create: `firmware/ports/esp32/main/logic/pl_util.h`, `firmware/ports/esp32/main/logic/pl_util.c`
- Create: `firmware/ports/esp32/host-tests/test_pl_util.c`
- Create: `firmware/ports/esp32/main/hal_storage.c`
- Modify: `firmware/ports/esp32/main/CMakeLists.txt`, `firmware/ports/esp32/host-tests/CMakeLists.txt`

**Interfaces:**
- Consumes: storage HAL prototypes and semantics (contract §2.5: `GADGET_ERR_NOT_FOUND` when missing, `GADGET_ERR_LIMIT` when `cap` is too small, durable on return, `erase` is OK when missing, `erase_all` clears only namespace `gadget`); storage keys `GADGET_KEY_*` (§2.7, §2.14). `nvs_flash_init()` is called by `main.c` (Task 13) before `core_init()`.
- Produces: `bool pl_storage_key_ok(const char *key)` (1..15 bytes); `gadget_status_t pl_ws_uri(char *out, size_t cap, const char *host, uint16_t port)` (`ws://<host>:<port>/gadget`, `GADGET_ERR_ARG` for an empty, too long or non-`[A-Za-z0-9._-]` host or port 0, `GADGET_ERR_LIMIT` when `cap` is short — used by Task 6); `void pl_utf8_trunc(char *dst, size_t cap, const char *src)` (used by Task 7); the six `hal_storage_*` functions.

- [ ] **Step 1: Write the failing test**

`firmware/ports/esp32/host-tests/test_pl_util.c`:

```c
/* SPDX-License-Identifier: Apache-2.0 */
#include <string.h>

#include "pl_util.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

static void test_storage_keys(void) {
  TEST_ASSERT_TRUE(pl_storage_key_ok("dev_key"));
  TEST_ASSERT_TRUE(pl_storage_key_ok("123456789012345"));
  TEST_ASSERT_FALSE(pl_storage_key_ok("1234567890123456"));
  TEST_ASSERT_FALSE(pl_storage_key_ok(""));
  TEST_ASSERT_FALSE(pl_storage_key_ok(NULL));
}

static void test_ws_uri(void) {
  char uri[300];
  TEST_ASSERT_EQUAL(GADGET_OK, pl_ws_uri(uri, sizeof(uri), "192.168.1.20", 8810));
  TEST_ASSERT_EQUAL_STRING("ws://192.168.1.20:8810/gadget", uri);
  TEST_ASSERT_EQUAL(GADGET_OK, pl_ws_uri(uri, sizeof(uri), "omkars-mac.local", 9000));
  TEST_ASSERT_EQUAL_STRING("ws://omkars-mac.local:9000/gadget", uri);
  TEST_ASSERT_EQUAL(GADGET_ERR_ARG, pl_ws_uri(uri, sizeof(uri), "fe80::1", 8810));
  TEST_ASSERT_EQUAL(GADGET_ERR_ARG, pl_ws_uri(uri, sizeof(uri), "a b", 8810));
  TEST_ASSERT_EQUAL(GADGET_ERR_ARG, pl_ws_uri(uri, sizeof(uri), "host/evil", 8810));
  TEST_ASSERT_EQUAL(GADGET_ERR_ARG, pl_ws_uri(uri, sizeof(uri), "", 8810));
  TEST_ASSERT_EQUAL(GADGET_ERR_ARG, pl_ws_uri(uri, sizeof(uri), "10.0.0.1", 0));
  TEST_ASSERT_EQUAL(GADGET_ERR_LIMIT, pl_ws_uri(uri, 10, "10.0.0.1", 8810));
}

static void test_utf8_trunc(void) {
  char out[8];
  pl_utf8_trunc(out, sizeof(out), "abc");
  TEST_ASSERT_EQUAL_STRING("abc", out);
  pl_utf8_trunc(out, sizeof(out), "abcdefghij");
  TEST_ASSERT_EQUAL_STRING("abcdefg", out);
  /* "abcde" + U+00E9 (2 bytes) + "z": byte 7 would split the é */
  pl_utf8_trunc(out, 7, "abcde\xC3\xA9z");
  TEST_ASSERT_EQUAL_STRING("abcde", out);
  pl_utf8_trunc(out, 8, "abcde\xC3\xA9z");
  TEST_ASSERT_EQUAL_STRING("abcde\xC3\xA9", out);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_storage_keys);
  RUN_TEST(test_ws_uri);
  RUN_TEST(test_utf8_trunc);
  return UNITY_END();
}
```

Append to `firmware/ports/esp32/host-tests/CMakeLists.txt`:

```cmake
port_test(test_pl_util)
```

- [ ] **Step 2: Run it to verify it fails**

Run: `cmake -S firmware/ports/esp32/host-tests -B build/esp32-host && cmake --build build/esp32-host -j10 && ctest --test-dir build/esp32-host --output-on-failure`

Expected: FAIL — the build stops with `fatal error: 'pl_util.h' file not found` (clang; GCC prints `pl_util.h: No such file or directory`).

- [ ] **Step 3: Write the helpers**

`firmware/ports/esp32/main/logic/pl_util.h`:

```c
/* SPDX-License-Identifier: Apache-2.0 */
/* Small pure helpers for the ESP32 port. */
#ifndef PL_UTIL_H
#define PL_UTIL_H

#include "gadget_types.h"

/* NVS keys are 1..15 bytes (gadget_hal.h storage group). */
bool pl_storage_key_ok(const char *key);

/* "ws://<host>:<port>/gadget". host is a DNS name or dotted IPv4:
 * 1..253 bytes of [A-Za-z0-9.-_]; no IPv6 literals. port 1..65535.
 * GADGET_ERR_ARG for a bad host or port, GADGET_ERR_LIMIT when cap is too small. */
gadget_status_t pl_ws_uri(char *out, size_t cap, const char *host, uint16_t port);

/* Copy at most cap-1 bytes of src, cutting on a UTF-8 code point boundary. */
void pl_utf8_trunc(char *dst, size_t cap, const char *src);

#endif /* PL_UTIL_H */
```

`firmware/ports/esp32/main/logic/pl_util.c`:

```c
/* SPDX-License-Identifier: Apache-2.0 */
#include "pl_util.h"

#include <stdio.h>
#include <string.h>

bool pl_storage_key_ok(const char *key) {
  if (key == NULL) {
    return false;
  }
  size_t n = strlen(key);
  return n >= 1 && n <= 15;
}

static bool host_char_ok(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' ||
         c == '-' || c == '_';
}

gadget_status_t pl_ws_uri(char *out, size_t cap, const char *host, uint16_t port) {
  if (host == NULL || port == 0) {
    return GADGET_ERR_ARG;
  }
  size_t n = strlen(host);
  if (n == 0 || n > 253) {
    return GADGET_ERR_ARG;
  }
  for (size_t i = 0; i < n; i++) {
    if (!host_char_ok(host[i])) {
      return GADGET_ERR_ARG;
    }
  }
  int w = snprintf(out, cap, "ws://%s:%u%s", host, (unsigned)port, GADGET_WS_PATH);
  if (w < 0 || (size_t)w >= cap) {
    return GADGET_ERR_LIMIT;
  }
  return GADGET_OK;
}

void pl_utf8_trunc(char *dst, size_t cap, const char *src) {
  if (cap == 0) {
    return;
  }
  size_t n = strlen(src);
  if (n >= cap) {
    n = cap - 1;
    while (n > 0 && ((unsigned char)src[n] & 0xC0u) == 0x80u) {
      n--;
    }
  }
  memcpy(dst, src, n);
  dst[n] = '\0';
}
```

In `firmware/ports/esp32/host-tests/CMakeLists.txt`, replace the `set(PORT_LOGIC_SRCS ...)` block with:

```cmake
set(PORT_LOGIC_SRCS
  ${CORE_DIR}/src/boards.c
  ${ESP_DIR}/main/logic/pl_event.c
  ${ESP_DIR}/main/logic/pl_util.c
)
```

- [ ] **Step 4: Run the tests to verify they pass**

Run: `cmake -S firmware/ports/esp32/host-tests -B build/esp32-host && cmake --build build/esp32-host -j10 && ctest --test-dir build/esp32-host --output-on-failure`

Expected: `100% tests passed, 0 tests failed out of 4`.

- [ ] **Step 5: Write the storage HAL**

`firmware/ports/esp32/main/hal_storage.c`:

```c
/* SPDX-License-Identifier: Apache-2.0 */
/* HAL storage group on NVS: namespace "gadget", committed on every write
 * (contract §2.14). With CONFIG_GADGET_NVS_ENCRYPT the whole default NVS
 * partition is encrypted by ESP-IDF's HMAC scheme (nvs_flash_init in
 * main.c does the work); nothing here changes. */
#include <string.h>

#include "esp_idf_version.h"
#include "gadget_hal.h"
#include "nvs.h"
#include "pl_util.h"
#include "sdkconfig.h"

#if CONFIG_GADGET_NVS_ENCRYPT
#if !CONFIG_NVS_ENCRYPTION || !CONFIG_NVS_SEC_KEY_PROTECT_USING_HMAC || CONFIG_NVS_SEC_HMAC_EFUSE_KEY_ID != 5
#error "GADGET_NVS_ENCRYPT needs NVS_ENCRYPTION, NVS_SEC_KEY_PROTECT_USING_HMAC and NVS_SEC_HMAC_EFUSE_KEY_ID=5 (sdkconfig.nvs-encrypt)"
#endif
#endif

#define STORAGE_NS "gadget"

static gadget_status_t map_err(esp_err_t e) {
  switch (e) {
    case ESP_OK: return GADGET_OK;
    case ESP_ERR_NVS_NOT_FOUND: return GADGET_ERR_NOT_FOUND;
    case ESP_ERR_NVS_INVALID_LENGTH: return GADGET_ERR_LIMIT;
    case ESP_ERR_NO_MEM: return GADGET_ERR_NO_MEM;
    case ESP_ERR_NVS_KEY_TOO_LONG:
    case ESP_ERR_NVS_INVALID_NAME:
    case ESP_ERR_NVS_VALUE_TOO_LONG:
    case ESP_ERR_INVALID_ARG: return GADGET_ERR_ARG;
    default: return GADGET_ERR_IO;
  }
}

/* A read-only open of a namespace that was never written reports
 * ESP_ERR_NVS_NOT_FOUND, which is the same as "key missing" here. */
static gadget_status_t open_ns(nvs_open_mode_t mode, nvs_handle_t *h) { return map_err(nvs_open(STORAGE_NS, mode, h)); }

static gadget_status_t commit_close(nvs_handle_t h, esp_err_t e) {
  if (e == ESP_OK) {
    e = nvs_commit(h);
  }
  nvs_close(h);
  return map_err(e);
}

gadget_status_t hal_storage_get_str(const char *key, char *buf, size_t cap) {
  if (!pl_storage_key_ok(key) || buf == NULL || cap == 0) {
    return GADGET_ERR_ARG;
  }
  nvs_handle_t h;
  gadget_status_t st = open_ns(NVS_READONLY, &h);
  if (st != GADGET_OK) {
    return st;
  }
  size_t len = cap;
  st = map_err(nvs_get_str(h, key, buf, &len));
  nvs_close(h);
  return st;
}

gadget_status_t hal_storage_set_str(const char *key, const char *value) {
  if (!pl_storage_key_ok(key) || value == NULL) {
    return GADGET_ERR_ARG;
  }
  nvs_handle_t h;
  gadget_status_t st = open_ns(NVS_READWRITE, &h);
  if (st != GADGET_OK) {
    return st;
  }
  return commit_close(h, nvs_set_str(h, key, value));
}

gadget_status_t hal_storage_get_blob(const char *key, void *buf, size_t cap, size_t *len) {
  if (!pl_storage_key_ok(key) || buf == NULL || len == NULL) {
    return GADGET_ERR_ARG;
  }
  nvs_handle_t h;
  gadget_status_t st = open_ns(NVS_READONLY, &h);
  if (st != GADGET_OK) {
    return st;
  }
  size_t n = cap;
  st = map_err(nvs_get_blob(h, key, buf, &n));
  nvs_close(h);
  if (st == GADGET_OK) {
    *len = n;
  }
  return st;
}

gadget_status_t hal_storage_set_blob(const char *key, const void *data, size_t len) {
  if (!pl_storage_key_ok(key) || (data == NULL && len > 0)) {
    return GADGET_ERR_ARG;
  }
  nvs_handle_t h;
  gadget_status_t st = open_ns(NVS_READWRITE, &h);
  if (st != GADGET_OK) {
    return st;
  }
  return commit_close(h, nvs_set_blob(h, key, data, len));
}

gadget_status_t hal_storage_erase(const char *key) {
  if (!pl_storage_key_ok(key)) {
    return GADGET_ERR_ARG;
  }
  nvs_handle_t h;
  gadget_status_t st = open_ns(NVS_READWRITE, &h);
  if (st != GADGET_OK) {
    return st;
  }
  esp_err_t e = nvs_erase_key(h, key);
  if (e == ESP_ERR_NVS_NOT_FOUND) {
    e = ESP_OK;
  }
  return commit_close(h, e);
}

gadget_status_t hal_storage_erase_all(void) {
  nvs_handle_t h;
  gadget_status_t st = open_ns(NVS_READWRITE, &h);
  if (st != GADGET_OK) {
    return st;
  }
  esp_err_t e = nvs_erase_all(h);
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 0, 0)
  if (e == ESP_OK) {
    e = nvs_purge_all(h); /* overwrite the erased entries, so the old key is gone from flash */
  }
#endif
  return commit_close(h, e);
}
```

In `firmware/ports/esp32/main/CMakeLists.txt`, replace the `set(port_srcs ...)` and `set(logic_srcs ...)` blocks with:

```cmake
set(port_srcs
  main.c
  port_events.c
  hal_system.c
  console_usj.c
  hal_storage.c)

set(logic_srcs
  logic/pl_event.c
  logic/pl_util.c)
```

- [ ] **Step 6: Build all four boards, then the encrypted variant**

Run:

```bash
. ~/.espressif/tools/activate_idf_v6.0.3.sh
SKIP_ART_CHECK=1 firmware/ports/esp32/tools/build-all.sh
```

Expected, for each of `amoled-175`, `amoled-175c`, `devkit`, `lcd-154` (alphabetical): `== <board>` followed by `check-size: <board> ok, <n> of 6291456 bytes (<p>%)`; exit status 0.

Then build the opt-in variant and check that both configurations resolved:

```bash
cd firmware/ports/esp32
idf.py -B build/amoled-175c-nvs-encrypt -D GADGET_BOARD=amoled-175c -D SDKCONFIG=build/amoled-175c-nvs-encrypt/sdkconfig -D GADGET_NVS_ENCRYPT=1 build > build/amoled-175c-nvs-encrypt.log 2>&1; echo "exit $?"
grep -xF 'CONFIG_GADGET_NVS_ENCRYPT=y' build/amoled-175c-nvs-encrypt/sdkconfig
grep -xF 'CONFIG_NVS_SEC_KEY_PROTECT_USING_HMAC=y' build/amoled-175c-nvs-encrypt/sdkconfig
grep -xF 'CONFIG_NVS_SEC_HMAC_EFUSE_KEY_ID=5' build/amoled-175c-nvs-encrypt/sdkconfig
grep -xF '# CONFIG_GADGET_NVS_ENCRYPT is not set' build/amoled-175c/sdkconfig
tools/check-size.sh amoled-175c build/amoled-175c-nvs-encrypt
cd ../../..
```

Expected: `exit 0`, each `grep` prints its line, and `check-size: amoled-175c ok, ...`.

- [ ] **Step 7: Check that a half-configured encryption build is refused**

```bash
cd firmware/ports/esp32
mkdir -p build/guard-enc && printf 'CONFIG_GADGET_NVS_ENCRYPT=y\n' > build/guard-enc/sdkconfig
idf.py -B build/guard-enc -D GADGET_BOARD=amoled-175c -D SDKCONFIG=build/guard-enc/sdkconfig build > build/guard-enc.log 2>&1; echo "exit $?"
grep -m1 'GADGET_NVS_ENCRYPT needs' build/guard-enc.log
rm -rf build/guard-enc build/guard-enc.log
cd ../../..
```

Expected: a non-zero `exit`, and the log line contains `#error "GADGET_NVS_ENCRYPT needs NVS_ENCRYPTION, NVS_SEC_KEY_PROTECT_USING_HMAC and NVS_SEC_HMAC_EFUSE_KEY_ID=5 (sdkconfig.nvs-encrypt)"`.

- [ ] **Step 8: Commit**

```bash
git add firmware/ports/esp32/main \
  firmware/ports/esp32/host-tests
git commit -m "feat(esp32): NVS storage HAL with opt-in HMAC encryption"
```
### Task 5: Wi-Fi station, retry policy and scan

**Files:**
- Create: `firmware/ports/esp32/main/logic/pl_scan.h`, `pl_scan.c`, `pl_wifi.h`, `pl_wifi.c` (in `firmware/ports/esp32/main/logic/`)
- Create: `firmware/ports/esp32/host-tests/test_pl_scan.c`, `firmware/ports/esp32/host-tests/test_pl_wifi.c`
- Create: `firmware/ports/esp32/main/hal_wifi.c`
- Modify: `firmware/ports/esp32/main/CMakeLists.txt`, `firmware/ports/esp32/host-tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `gadget_wifi_ap_t`, `gadget_wifi_auth_t`, `gadget_wifi_state_t`, `GADGET_EV_WIFI_STATE`, `GADGET_EV_WIFI_SCAN` (contract §2.4: at most 20 APs, strongest first, de-duplicated by SSID); Wi-Fi HAL prototypes (§2.5: the radio is started before `core_init()`); `port_post_event`, `port_post_wifi`, `port_wifi_msg_t` (Task 3).
- Produces: `uint8_t pl_scan_merge(const gadget_wifi_ap_t *in, size_t n, gadget_wifi_ap_t *out, uint8_t cap)`; the `pl_wifi_t` state machine (`pl_wifi_init/connect/disconnect/got_ip/sta_disconnected/scan/scan_done/tick`, actions `pl_wifi_act_t {disconnect, connect, scan, post, state}`, `pl_wifi_retry_delay_ms`): retries after 1, 2, 5 then every 10 s, reports `GADGET_WIFI_FAILED` after 3 failed attempts or at once on an authentication failure (then retries every 30 s), never posts the same state twice, pauses an attempt to scan; `port_wifi_start()`, `port_wifi_on_msg()`, `port_wifi_tick()`; `hal_wifi_connect`, `hal_wifi_disconnect`, `hal_wifi_state`, `hal_wifi_scan`.

- [ ] **Step 1: Write the failing tests**

`firmware/ports/esp32/host-tests/test_pl_scan.c`:

```c
/* SPDX-License-Identifier: Apache-2.0 */
#include <stdio.h>
#include <string.h>

#include "pl_scan.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

static gadget_wifi_ap_t ap(const char *ssid, int8_t rssi, gadget_wifi_auth_t auth) {
  gadget_wifi_ap_t a;
  memset(&a, 0, sizeof(a));
  snprintf(a.ssid, sizeof(a.ssid), "%s", ssid);
  a.rssi = rssi;
  a.auth = auth;
  return a;
}

static void test_dedupes_keeps_strongest_and_sorts(void) {
  gadget_wifi_ap_t in[] = {
    ap("Cafe", -80, GADGET_AUTH_OPEN), ap("Home", -60, GADGET_AUTH_WPA2), ap("", -30, GADGET_AUTH_WPA2),
    ap("Home", -42, GADGET_AUTH_WPA3), ap("Office", -42, GADGET_AUTH_WPA2_ENT), ap("Cafe", -90, GADGET_AUTH_OPEN),
  };
  gadget_wifi_ap_t out[PL_SCAN_MAX];
  uint8_t n = pl_scan_merge(in, sizeof(in) / sizeof(in[0]), out, PL_SCAN_MAX);
  TEST_ASSERT_EQUAL_UINT8(3, n);
  TEST_ASSERT_EQUAL_STRING("Home", out[0].ssid); /* -42, "Home" < "Office" */
  TEST_ASSERT_EQUAL_INT8(-42, out[0].rssi);
  TEST_ASSERT_EQUAL(GADGET_AUTH_WPA3, out[0].auth);
  TEST_ASSERT_EQUAL_STRING("Office", out[1].ssid);
  TEST_ASSERT_EQUAL_STRING("Cafe", out[2].ssid);
  TEST_ASSERT_EQUAL_INT8(-80, out[2].rssi);
}

static void test_caps_at_twenty_strongest(void) {
  gadget_wifi_ap_t in[30];
  for (int i = 0; i < 30; i++) {
    char name[8];
    snprintf(name, sizeof(name), "n%02d", i);
    in[i] = ap(name, (int8_t)(-90 + i), GADGET_AUTH_WPA2); /* n29 strongest */
  }
  gadget_wifi_ap_t out[PL_SCAN_MAX];
  uint8_t n = pl_scan_merge(in, 30, out, 200);
  TEST_ASSERT_EQUAL_UINT8(PL_SCAN_MAX, n);
  TEST_ASSERT_EQUAL_STRING("n29", out[0].ssid);
  TEST_ASSERT_EQUAL_STRING("n10", out[19].ssid);
}

static void test_late_stronger_duplicate_of_evicted_ssid(void) {
  gadget_wifi_ap_t in[4] = {ap("A", -50, 0), ap("B", -60, 0), ap("C", -70, 0), ap("C", -40, 0)};
  gadget_wifi_ap_t out[2];
  uint8_t n = pl_scan_merge(in, 4, out, 2);
  TEST_ASSERT_EQUAL_UINT8(2, n);
  TEST_ASSERT_EQUAL_STRING("C", out[0].ssid);
  TEST_ASSERT_EQUAL_STRING("A", out[1].ssid);
}

static void test_unterminated_ssid_is_cut(void) {
  gadget_wifi_ap_t in[1];
  memset(&in[0], 'x', sizeof(in[0].ssid));
  in[0].rssi = -50;
  in[0].auth = GADGET_AUTH_OPEN;
  gadget_wifi_ap_t out[1];
  TEST_ASSERT_EQUAL_UINT8(1, pl_scan_merge(in, 1, out, 1));
  TEST_ASSERT_EQUAL_size_t(32, strlen(out[0].ssid));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_dedupes_keeps_strongest_and_sorts);
  RUN_TEST(test_caps_at_twenty_strongest);
  RUN_TEST(test_late_stronger_duplicate_of_evicted_ssid);
  RUN_TEST(test_unterminated_ssid_is_cut);
  return UNITY_END();
}
```

`firmware/ports/esp32/host-tests/test_pl_wifi.c`:

```c
/* SPDX-License-Identifier: Apache-2.0 */
#include "pl_wifi.h"
#include "unity.h"

static pl_wifi_t w;

void setUp(void) { pl_wifi_init(&w); }
void tearDown(void) {}

static void test_connect_posts_connecting_then_connected(void) {
  pl_wifi_act_t a = pl_wifi_connect(&w, 0);
  TEST_ASSERT_FALSE(a.disconnect);
  TEST_ASSERT_TRUE(a.connect);
  TEST_ASSERT_TRUE(a.post);
  TEST_ASSERT_EQUAL(GADGET_WIFI_CONNECTING, a.state);
  a = pl_wifi_got_ip(&w);
  TEST_ASSERT_TRUE(a.post);
  TEST_ASSERT_EQUAL(GADGET_WIFI_CONNECTED, a.state);
  TEST_ASSERT_EQUAL(GADGET_WIFI_CONNECTED, w.reported);
}

static void test_drop_after_connected_retries_at_once(void) {
  pl_wifi_connect(&w, 0);
  pl_wifi_got_ip(&w);
  pl_wifi_act_t a = pl_wifi_sta_disconnected(&w, false, false, 5000);
  TEST_ASSERT_TRUE(a.connect);
  TEST_ASSERT_TRUE(a.post);
  TEST_ASSERT_EQUAL(GADGET_WIFI_CONNECTING, a.state);
}

static void test_three_failures_report_failed_and_keep_retrying(void) {
  pl_wifi_connect(&w, 0);
  pl_wifi_act_t a = pl_wifi_sta_disconnected(&w, false, false, 100);
  TEST_ASSERT_FALSE(a.post);
  TEST_ASSERT_FALSE(a.connect);
  a = pl_wifi_tick(&w, 1099);
  TEST_ASSERT_FALSE(a.connect);
  a = pl_wifi_tick(&w, 1100);
  TEST_ASSERT_TRUE(a.connect);
  a = pl_wifi_sta_disconnected(&w, false, false, 1200);
  TEST_ASSERT_FALSE(a.post);
  a = pl_wifi_tick(&w, 3200);
  TEST_ASSERT_TRUE(a.connect);
  a = pl_wifi_sta_disconnected(&w, false, false, 3300);
  TEST_ASSERT_TRUE(a.post);
  TEST_ASSERT_EQUAL(GADGET_WIFI_FAILED, a.state);
  a = pl_wifi_tick(&w, 8300);
  TEST_ASSERT_TRUE(a.connect);
  a = pl_wifi_sta_disconnected(&w, false, false, 8400);
  TEST_ASSERT_FALSE(a.post); /* already FAILED: no repeat */
  TEST_ASSERT_EQUAL_UINT32(10000, pl_wifi_retry_delay_ms(4, false));
  a = pl_wifi_tick(&w, 18400);
  TEST_ASSERT_TRUE(a.connect);
  a = pl_wifi_got_ip(&w);
  TEST_ASSERT_EQUAL(GADGET_WIFI_CONNECTED, a.state);
}

static void test_wrong_password_fails_at_once_and_backs_off(void) {
  pl_wifi_connect(&w, 0);
  pl_wifi_act_t a = pl_wifi_sta_disconnected(&w, false, true, 100);
  TEST_ASSERT_TRUE(a.post);
  TEST_ASSERT_EQUAL(GADGET_WIFI_FAILED, a.state);
  TEST_ASSERT_FALSE(pl_wifi_tick(&w, 30099).connect);
  TEST_ASSERT_TRUE(pl_wifi_tick(&w, 30100).connect);
}

static void test_scan_while_connecting_aborts_attempt_then_resumes(void) {
  pl_wifi_connect(&w, 0);
  pl_wifi_act_t a = pl_wifi_scan(&w, 10);
  TEST_ASSERT_TRUE(a.disconnect);
  TEST_ASSERT_FALSE(a.scan);
  a = pl_wifi_sta_disconnected(&w, true, false, 20); /* our own disconnect */
  TEST_ASSERT_TRUE(a.scan);
  TEST_ASSERT_FALSE(a.post);
  TEST_ASSERT_FALSE(pl_wifi_tick(&w, 5000).connect); /* no retry while scanning */
  a = pl_wifi_scan_done(&w, 2000);
  TEST_ASSERT_TRUE(a.connect);
}

static void test_scan_pending_gives_up_waiting_after_three_seconds(void) {
  pl_wifi_connect(&w, 0);
  pl_wifi_scan(&w, 10);
  TEST_ASSERT_FALSE(pl_wifi_tick(&w, 3009).scan);
  TEST_ASSERT_TRUE(pl_wifi_tick(&w, 3010).scan);
}

static void test_scan_when_idle_or_connected_starts_at_once(void) {
  TEST_ASSERT_TRUE(pl_wifi_scan(&w, 0).scan);
  TEST_ASSERT_FALSE(pl_wifi_scan(&w, 1).scan); /* already scanning */
  pl_wifi_scan_done(&w, 2);
  TEST_ASSERT_FALSE(pl_wifi_tick(&w, 3).connect); /* not configured */
  pl_wifi_connect(&w, 10);
  pl_wifi_got_ip(&w);
  pl_wifi_act_t a = pl_wifi_scan(&w, 20);
  TEST_ASSERT_TRUE(a.scan);
  TEST_ASSERT_FALSE(a.disconnect);
}

static void test_attempt_timeout_counts_as_failure(void) {
  pl_wifi_connect(&w, 0);
  pl_wifi_act_t a = pl_wifi_tick(&w, PL_WIFI_ATTEMPT_TIMEOUT_MS);
  TEST_ASSERT_TRUE(a.disconnect);
  TEST_ASSERT_EQUAL_UINT8(1, w.failures);
  a = pl_wifi_sta_disconnected(&w, false, false, PL_WIFI_ATTEMPT_TIMEOUT_MS + 5);
  TEST_ASSERT_EQUAL_UINT8(1, w.failures); /* late report for the abandoned attempt */
}

static void test_disconnect_request_reports_off_and_stops(void) {
  pl_wifi_connect(&w, 0);
  pl_wifi_act_t a = pl_wifi_disconnect(&w);
  TEST_ASSERT_TRUE(a.disconnect);
  TEST_ASSERT_EQUAL(GADGET_WIFI_OFF, a.state);
  a = pl_wifi_sta_disconnected(&w, false, false, 100);
  TEST_ASSERT_FALSE(a.connect);
  TEST_ASSERT_FALSE(pl_wifi_tick(&w, 60000).connect);
  TEST_ASSERT_FALSE(pl_wifi_got_ip(&w).post);
}

static void test_new_network_while_connected_disconnects_first(void) {
  pl_wifi_connect(&w, 0);
  pl_wifi_got_ip(&w);
  pl_wifi_act_t a = pl_wifi_connect(&w, 100);
  TEST_ASSERT_TRUE(a.disconnect);
  TEST_ASSERT_TRUE(a.connect);
  TEST_ASSERT_EQUAL(GADGET_WIFI_CONNECTING, a.state);
  a = pl_wifi_sta_disconnected(&w, true, false, 110); /* our disconnect is not a failure */
  TEST_ASSERT_EQUAL_UINT8(0, w.failures);
  TEST_ASSERT_TRUE(w.attempting);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_connect_posts_connecting_then_connected);
  RUN_TEST(test_drop_after_connected_retries_at_once);
  RUN_TEST(test_three_failures_report_failed_and_keep_retrying);
  RUN_TEST(test_wrong_password_fails_at_once_and_backs_off);
  RUN_TEST(test_scan_while_connecting_aborts_attempt_then_resumes);
  RUN_TEST(test_scan_pending_gives_up_waiting_after_three_seconds);
  RUN_TEST(test_scan_when_idle_or_connected_starts_at_once);
  RUN_TEST(test_attempt_timeout_counts_as_failure);
  RUN_TEST(test_disconnect_request_reports_off_and_stops);
  RUN_TEST(test_new_network_while_connected_disconnects_first);
  return UNITY_END();
}
```

Append to `firmware/ports/esp32/host-tests/CMakeLists.txt`:

```cmake
port_test(test_pl_scan)
port_test(test_pl_wifi)
```

- [ ] **Step 2: Run them to verify they fail**

Run: `cmake -S firmware/ports/esp32/host-tests -B build/esp32-host && cmake --build build/esp32-host -j10 && ctest --test-dir build/esp32-host --output-on-failure`

Expected: FAIL — the build stops with `fatal error: 'pl_scan.h' file not found` (or the same for `pl_wifi.h`, whichever compiles first) (clang; GCC prints `pl_scan.h: No such file or directory`).

- [ ] **Step 3: Write the scan merge and the Wi-Fi state machine**

`firmware/ports/esp32/main/logic/pl_scan.h`:

```c
/* SPDX-License-Identifier: Apache-2.0 */
/* Wi-Fi scan result merging for the @omb scan line (gadget_events.h). */
#ifndef PL_SCAN_H
#define PL_SCAN_H

#include "gadget_events.h"

#define PL_SCAN_MAX 20u

/* Merge raw scan records into out[0..cap): hidden networks (empty SSID) are
 * dropped, each SSID keeps its strongest record, the result is sorted
 * strongest first (equal RSSI: SSID byte order). in and out must not
 * overlap; cap <= PL_SCAN_MAX. Returns the number written. */
uint8_t pl_scan_merge(const gadget_wifi_ap_t *in, size_t n, gadget_wifi_ap_t *out, uint8_t cap);

#endif /* PL_SCAN_H */
```

`firmware/ports/esp32/main/logic/pl_scan.c`:

```c
/* SPDX-License-Identifier: Apache-2.0 */
#include "pl_scan.h"

#include <string.h>

static bool before(const gadget_wifi_ap_t *a, const gadget_wifi_ap_t *b) {
  if (a->rssi != b->rssi) {
    return a->rssi > b->rssi;
  }
  return strcmp(a->ssid, b->ssid) < 0;
}

/* Move out[i] towards the front until the order holds again. */
static void bubble_up(gadget_wifi_ap_t *out, uint8_t i) {
  while (i > 0 && before(&out[i], &out[i - 1])) {
    gadget_wifi_ap_t t = out[i - 1];
    out[i - 1] = out[i];
    out[i] = t;
    i--;
  }
}

uint8_t pl_scan_merge(const gadget_wifi_ap_t *in, size_t n, gadget_wifi_ap_t *out, uint8_t cap) {
  uint8_t count = 0;
  if (cap > PL_SCAN_MAX) {
    cap = PL_SCAN_MAX;
  }
  for (size_t k = 0; k < n; k++) {
    gadget_wifi_ap_t ap = in[k];
    ap.ssid[sizeof(ap.ssid) - 1] = '\0';
    if (ap.ssid[0] == '\0') {
      continue;
    }
    uint8_t found = count;
    for (uint8_t i = 0; i < count; i++) {
      if (strcmp(out[i].ssid, ap.ssid) == 0) {
        found = i;
        break;
      }
    }
    if (found < count) {
      if (ap.rssi > out[found].rssi) {
        out[found] = ap;
        bubble_up(out, found);
      }
      continue;
    }
    if (count < cap) {
      out[count] = ap;
      bubble_up(out, count);
      count++;
    } else if (count > 0 && before(&ap, &out[count - 1])) {
      out[count - 1] = ap;
      bubble_up(out, (uint8_t)(count - 1));
    }
  }
  return count;
}
```

`firmware/ports/esp32/main/logic/pl_wifi.h`:

```c
/* SPDX-License-Identifier: Apache-2.0 */
/* Wi-Fi connection policy for the ESP32 port, as a pure state machine.
 * hal_wifi.c feeds it requests from core and events from the Wi-Fi driver
 * (on the main thread) and carries out the returned actions in order:
 * disconnect, then connect or scan, then post the state. */
#ifndef PL_WIFI_H
#define PL_WIFI_H

#include "gadget_events.h"

#define PL_WIFI_FAILS_BEFORE_FAILED 3u
#define PL_WIFI_ATTEMPT_TIMEOUT_MS 20000u
#define PL_WIFI_SCAN_WAIT_MS 3000u

typedef struct {
  bool configured;          /* a network was set with hal_wifi_connect() */
  bool connected;           /* the station has an IPv4 address */
  bool attempting;          /* esp_wifi_connect() issued, result pending */
  bool scanning;            /* esp_wifi_scan_start() issued */
  bool scan_pending;        /* a scan waits for our own disconnect */
  uint8_t failures;         /* consecutive failed attempts */
  gadget_wifi_state_t reported;
  uint64_t retry_at_ms;     /* 0 = no retry scheduled */
  uint64_t attempt_deadline_ms;
  uint64_t scan_deadline_ms;
} pl_wifi_t;

typedef struct {
  bool disconnect;          /* call esp_wifi_disconnect() first */
  bool connect;             /* call esp_wifi_connect() */
  bool scan;                /* call esp_wifi_scan_start() */
  bool post;                /* post GADGET_EV_WIFI_STATE with `state` */
  gadget_wifi_state_t state;
} pl_wifi_act_t;

void pl_wifi_init(pl_wifi_t *w);
/* hal_wifi_connect(): the caller has stored the new SSID/password. */
pl_wifi_act_t pl_wifi_connect(pl_wifi_t *w, uint64_t now_ms);
/* hal_wifi_disconnect(): forget the network until the next connect. */
pl_wifi_act_t pl_wifi_disconnect(pl_wifi_t *w);
/* IP_EVENT_STA_GOT_IP */
pl_wifi_act_t pl_wifi_got_ip(pl_wifi_t *w);
/* WIFI_EVENT_STA_DISCONNECTED. local: reason WIFI_REASON_ASSOC_LEAVE (our
 * own esp_wifi_disconnect()); auth_failed: a wrong password or handshake failure. */
pl_wifi_act_t pl_wifi_sta_disconnected(pl_wifi_t *w, bool local, bool auth_failed, uint64_t now_ms);
/* hal_wifi_scan() */
pl_wifi_act_t pl_wifi_scan(pl_wifi_t *w, uint64_t now_ms);
/* WIFI_EVENT_SCAN_DONE, or esp_wifi_scan_start() failed */
pl_wifi_act_t pl_wifi_scan_done(pl_wifi_t *w, uint64_t now_ms);
/* Every main-loop iteration. */
pl_wifi_act_t pl_wifi_tick(pl_wifi_t *w, uint64_t now_ms);
/* Delay before the next attempt after `failures` consecutive failures. */
uint32_t pl_wifi_retry_delay_ms(uint8_t failures, bool auth_failed);

#endif /* PL_WIFI_H */
```

`firmware/ports/esp32/main/logic/pl_wifi.c`:

```c
/* SPDX-License-Identifier: Apache-2.0 */
#include "pl_wifi.h"

#include <string.h>

static void report(pl_wifi_t *w, pl_wifi_act_t *a, gadget_wifi_state_t s) {
  if (w->reported != s) {
    w->reported = s;
    a->post = true;
    a->state = s;
  }
}

static void start_attempt(pl_wifi_t *w, pl_wifi_act_t *a, uint64_t now) {
  w->retry_at_ms = 0;
  if (w->scanning || w->scan_pending) {
    return; /* pl_wifi_scan_done() starts it */
  }
  a->connect = true;
  w->attempting = true;
  w->attempt_deadline_ms = now + PL_WIFI_ATTEMPT_TIMEOUT_MS;
}

static void fail_attempt(pl_wifi_t *w, pl_wifi_act_t *a, bool auth_failed, uint64_t now) {
  w->attempting = false;
  if (w->failures < 255) {
    w->failures++;
  }
  if (auth_failed || w->failures >= PL_WIFI_FAILS_BEFORE_FAILED) {
    report(w, a, GADGET_WIFI_FAILED);
  }
  w->retry_at_ms = now + pl_wifi_retry_delay_ms(w->failures, auth_failed);
}

uint32_t pl_wifi_retry_delay_ms(uint8_t failures, bool auth_failed) {
  if (auth_failed) {
    return 30000u;
  }
  switch (failures) {
    case 0:
    case 1: return 1000u;
    case 2: return 2000u;
    case 3: return 5000u;
    default: return 10000u;
  }
}

void pl_wifi_init(pl_wifi_t *w) {
  memset(w, 0, sizeof(*w));
  w->reported = GADGET_WIFI_OFF;
}

pl_wifi_act_t pl_wifi_connect(pl_wifi_t *w, uint64_t now) {
  pl_wifi_act_t a = {0};
  a.disconnect = w->connected || w->attempting;
  w->configured = true;
  w->connected = false;
  w->attempting = false;
  w->failures = 0;
  report(w, &a, GADGET_WIFI_CONNECTING);
  start_attempt(w, &a, now);
  return a;
}

pl_wifi_act_t pl_wifi_disconnect(pl_wifi_t *w) {
  pl_wifi_act_t a = {0};
  a.disconnect = w->connected || w->attempting;
  w->configured = false;
  w->connected = false;
  w->attempting = false;
  w->failures = 0;
  w->retry_at_ms = 0;
  report(w, &a, GADGET_WIFI_OFF);
  return a;
}

pl_wifi_act_t pl_wifi_got_ip(pl_wifi_t *w) {
  pl_wifi_act_t a = {0};
  if (!w->configured) {
    return a;
  }
  w->connected = true;
  w->attempting = false;
  w->failures = 0;
  w->retry_at_ms = 0;
  w->reported = GADGET_WIFI_OFF; /* force a post: the IP may have changed */
  report(w, &a, GADGET_WIFI_CONNECTED);
  return a;
}

pl_wifi_act_t pl_wifi_sta_disconnected(pl_wifi_t *w, bool local, bool auth_failed, uint64_t now) {
  pl_wifi_act_t a = {0};
  if (local) {
    if (w->scan_pending) {
      w->scan_pending = false;
      w->scanning = true;
      w->attempting = false;
      a.scan = true;
    }
    return a;
  }
  if (!w->configured) {
    return a;
  }
  if (w->connected) {
    w->connected = false;
    w->failures = 0;
    report(w, &a, GADGET_WIFI_CONNECTING);
    start_attempt(w, &a, now);
    return a;
  }
  if (!w->attempting) {
    return a; /* a late report for an attempt we already gave up on */
  }
  fail_attempt(w, &a, auth_failed, now);
  return a;
}

pl_wifi_act_t pl_wifi_scan(pl_wifi_t *w, uint64_t now) {
  pl_wifi_act_t a = {0};
  if (w->scanning || w->scan_pending) {
    return a;
  }
  if (w->attempting) {
    a.disconnect = true;
    w->scan_pending = true;
    w->scan_deadline_ms = now + PL_WIFI_SCAN_WAIT_MS;
    return a;
  }
  w->scanning = true;
  a.scan = true;
  return a;
}

pl_wifi_act_t pl_wifi_scan_done(pl_wifi_t *w, uint64_t now) {
  pl_wifi_act_t a = {0};
  w->scanning = false;
  if (w->configured && !w->connected && !w->attempting) {
    start_attempt(w, &a, now);
  }
  return a;
}

pl_wifi_act_t pl_wifi_tick(pl_wifi_t *w, uint64_t now) {
  pl_wifi_act_t a = {0};
  if (w->scan_pending && now >= w->scan_deadline_ms) {
    w->scan_pending = false;
    w->scanning = true;
    w->attempting = false;
    a.scan = true;
    return a;
  }
  if (w->attempting && now >= w->attempt_deadline_ms) {
    a.disconnect = true;
    fail_attempt(w, &a, false, now);
    return a;
  }
  if (w->configured && !w->connected && !w->attempting && w->retry_at_ms != 0 && now >= w->retry_at_ms) {
    start_attempt(w, &a, now);
  }
  return a;
}
```

In `firmware/ports/esp32/host-tests/CMakeLists.txt`, replace the `set(PORT_LOGIC_SRCS ...)` block with:

```cmake
set(PORT_LOGIC_SRCS
  ${CORE_DIR}/src/boards.c
  ${ESP_DIR}/main/logic/pl_event.c
  ${ESP_DIR}/main/logic/pl_util.c
  ${ESP_DIR}/main/logic/pl_scan.c
  ${ESP_DIR}/main/logic/pl_wifi.c
)
```

- [ ] **Step 4: Run the tests to verify they pass**

Run: `cmake -S firmware/ports/esp32/host-tests -B build/esp32-host && cmake --build build/esp32-host -j10 && ctest --test-dir build/esp32-host --output-on-failure`

Expected: `100% tests passed, 0 tests failed out of 6`.

- [ ] **Step 5: Write the Wi-Fi HAL**

The authentication modes map with enumerators that exist in both v5.5.5 and v6.0.3 (`WIFI_AUTH_WPA_WPA2_PSK` → `wpa2`, `WIFI_AUTH_WPA2_WPA3_PSK` → `wpa3`, enterprise WPA2 → `wpa2-ent`, the rest → `other`). The station credentials stay in our NVS keys (`esp_wifi_set_storage(WIFI_STORAGE_RAM)`).

`firmware/ports/esp32/main/hal_wifi.c`:

```c
/* SPDX-License-Identifier: Apache-2.0 */
/* HAL Wi-Fi group. The policy (retries, FAILED after 3 failures, pausing a
 * connection attempt for a scan) is the pure pl_wifi state machine; this
 * file feeds it on the gadget task and carries out its actions. Wi-Fi
 * driver events arrive on the event-loop task and are queued as
 * port_wifi_msg_t, so pl_wifi is only ever touched by one task. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "gadget_hal.h"
#include "pl_scan.h"
#include "pl_wifi.h"
#include "port.h"

#define SCAN_RECORDS_MAX 40

static const char *TAG = "wifi";
static pl_wifi_t s_w;
static wifi_config_t s_cfg;
static char s_ip[16];

static gadget_wifi_auth_t map_auth(wifi_auth_mode_t m) {
  switch (m) {
    case WIFI_AUTH_OPEN: return GADGET_AUTH_OPEN;
    case WIFI_AUTH_WEP: return GADGET_AUTH_WEP;
    case WIFI_AUTH_WPA_PSK: return GADGET_AUTH_WPA;
    case WIFI_AUTH_WPA2_PSK:
    case WIFI_AUTH_WPA_WPA2_PSK: return GADGET_AUTH_WPA2;
    case WIFI_AUTH_WPA3_PSK:
    case WIFI_AUTH_WPA2_WPA3_PSK: return GADGET_AUTH_WPA3;
    case WIFI_AUTH_WPA2_ENTERPRISE: return GADGET_AUTH_WPA2_ENT;
    default: return GADGET_AUTH_OTHER;
  }
}

static void post_state(gadget_wifi_state_t state) {
  gadget_event_t ev = {.type = GADGET_EV_WIFI_STATE};
  ev.u.wifi.state = state;
  if (state == GADGET_WIFI_CONNECTED) {
    snprintf(ev.u.wifi.ip, sizeof(ev.u.wifi.ip), "%s", s_ip);
  }
  port_post_event(&ev);
}

static void post_scan(const gadget_wifi_ap_t *aps, uint8_t count, bool ok) {
  gadget_event_t ev = {.type = GADGET_EV_WIFI_SCAN};
  ev.u.scan.aps = aps;
  ev.u.scan.count = count;
  ev.u.scan.ok = ok;
  port_post_event(&ev);
}

static void apply(const pl_wifi_act_t *a);

static void scan_failed(void) {
  post_scan(NULL, 0, false);
  pl_wifi_act_t b = pl_wifi_scan_done(&s_w, hal_now_ms());
  apply(&b);
}

static void apply(const pl_wifi_act_t *a) {
  if (a->disconnect) {
    esp_wifi_disconnect(); /* not connected: returns an error we can ignore */
  }
  if (a->scan) {
    const wifi_scan_config_t sc = {.show_hidden = false};
    esp_err_t e = esp_wifi_scan_start(&sc, false);
    if (e != ESP_OK) {
      ESP_LOGW(TAG, "scan start: %s", esp_err_to_name(e));
      scan_failed();
    }
  }
  if (a->connect) {
    esp_err_t e = esp_wifi_set_config(WIFI_IF_STA, &s_cfg);
    if (e == ESP_OK) {
      e = esp_wifi_connect();
    }
    if (e != ESP_OK) {
      ESP_LOGW(TAG, "connect: %s", esp_err_to_name(e));
      pl_wifi_act_t b = pl_wifi_sta_disconnected(&s_w, false, false, hal_now_ms());
      apply(&b);
    }
  }
  if (a->post) {
    post_state(a->state);
  }
}

static void deliver_scan(void) {
  uint16_t n = 0;
  wifi_ap_record_t *recs = NULL;
  gadget_wifi_ap_t *raw = NULL;
  gadget_wifi_ap_t out[PL_SCAN_MAX];
  uint8_t count = 0;
  bool ok = esp_wifi_scan_get_ap_num(&n) == ESP_OK;
  if (ok && n > 0) {
    if (n > SCAN_RECORDS_MAX) {
      n = SCAN_RECORDS_MAX;
    }
    recs = calloc(n, sizeof(*recs));
    raw = calloc(n, sizeof(*raw));
    ok = recs != NULL && raw != NULL && esp_wifi_scan_get_ap_records(&n, recs) == ESP_OK;
    if (ok) {
      for (uint16_t i = 0; i < n; i++) {
        memcpy(raw[i].ssid, recs[i].ssid, sizeof(raw[i].ssid) - 1);
        raw[i].ssid[sizeof(raw[i].ssid) - 1] = '\0';
        raw[i].rssi = recs[i].rssi;
        raw[i].auth = map_auth(recs[i].authmode);
      }
      count = pl_scan_merge(raw, n, out, PL_SCAN_MAX);
    }
  }
  esp_wifi_clear_ap_list(); /* frees whatever get_ap_records did not take */
  free(recs);
  free(raw);
  post_scan(out, count, ok);
}

void port_wifi_on_msg(const port_wifi_msg_t *m) {
  uint64_t now = hal_now_ms();
  pl_wifi_act_t a = {0};
  switch (m->kind) {
    case PORT_WIFI_GOT_IP:
      snprintf(s_ip, sizeof(s_ip), "%s", m->ip);
      a = pl_wifi_got_ip(&s_w);
      break;
    case PORT_WIFI_DISCONNECTED:
      a = pl_wifi_sta_disconnected(&s_w, m->local, m->auth_failed, now);
      break;
    case PORT_WIFI_SCAN_DONE:
      deliver_scan();
      a = pl_wifi_scan_done(&s_w, now);
      break;
  }
  apply(&a);
}

void port_wifi_tick(uint64_t now_ms) {
  pl_wifi_act_t a = pl_wifi_tick(&s_w, now_ms);
  apply(&a);
}

/* Event-loop task: copy what matters and queue it for the gadget task. */
static void on_event(void *arg, esp_event_base_t base, int32_t id, void *data) {
  (void)arg;
  port_wifi_msg_t m = {0};
  if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
    const wifi_event_sta_disconnected_t *d = data;
    m.kind = PORT_WIFI_DISCONNECTED;
    m.local = d->reason == WIFI_REASON_ASSOC_LEAVE;
    m.auth_failed = d->reason == WIFI_REASON_AUTH_FAIL || d->reason == WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT ||
                    d->reason == WIFI_REASON_HANDSHAKE_TIMEOUT;
  } else if (base == WIFI_EVENT && id == WIFI_EVENT_SCAN_DONE) {
    m.kind = PORT_WIFI_SCAN_DONE;
  } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
    const ip_event_got_ip_t *g = data;
    m.kind = PORT_WIFI_GOT_IP;
    snprintf(m.ip, sizeof(m.ip), IPSTR, IP2STR(&g->ip_info.ip));
  } else {
    return;
  }
  port_post_wifi(&m);
}

esp_err_t port_wifi_start(void) {
  pl_wifi_init(&s_w);
  ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "netif init");
  ESP_RETURN_ON_ERROR(esp_event_loop_create_default(), TAG, "event loop");
  if (esp_netif_create_default_wifi_sta() == NULL) {
    return ESP_FAIL;
  }
  wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
  ESP_RETURN_ON_ERROR(esp_wifi_init(&init), TAG, "wifi init");
  ESP_RETURN_ON_ERROR(esp_wifi_set_storage(WIFI_STORAGE_RAM), TAG, "storage"); /* credentials live in our NVS keys */
  ESP_RETURN_ON_ERROR(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_event, NULL, NULL), TAG, "wifi events");
  ESP_RETURN_ON_ERROR(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_event, NULL, NULL), TAG, "ip events");
  ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "sta mode");
  ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "wifi start"); /* the RNG is now truly random (spec §4.2) */
  esp_wifi_set_ps(WIFI_PS_NONE);                              /* low latency for audio */
  return ESP_OK;
}

gadget_status_t hal_wifi_connect(const char *ssid, const char *password) {
  if (ssid == NULL || password == NULL) {
    return GADGET_ERR_ARG;
  }
  size_t sl = strlen(ssid), pl = strlen(password);
  if (sl == 0 || sl > sizeof(s_cfg.sta.ssid) || pl > sizeof(s_cfg.sta.password)) {
    return GADGET_ERR_ARG;
  }
  memset(&s_cfg, 0, sizeof(s_cfg));
  memcpy(s_cfg.sta.ssid, ssid, sl);
  memcpy(s_cfg.sta.password, password, pl);
  s_cfg.sta.threshold.authmode = pl == 0 ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA_PSK;
  s_cfg.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;
  pl_wifi_act_t a = pl_wifi_connect(&s_w, hal_now_ms());
  apply(&a);
  return GADGET_OK;
}

void hal_wifi_disconnect(void) {
  pl_wifi_act_t a = pl_wifi_disconnect(&s_w);
  memset(&s_cfg, 0, sizeof(s_cfg));
  apply(&a);
}

gadget_wifi_state_t hal_wifi_state(void) { return s_w.reported; }

gadget_status_t hal_wifi_scan(void) {
  pl_wifi_act_t a = pl_wifi_scan(&s_w, hal_now_ms());
  apply(&a);
  return GADGET_OK;
}
```

In `firmware/ports/esp32/main/CMakeLists.txt`, replace the `set(port_srcs ...)` and `set(logic_srcs ...)` blocks with:

```cmake
set(port_srcs
  main.c
  port_events.c
  hal_system.c
  console_usj.c
  hal_storage.c
  hal_wifi.c)

set(logic_srcs
  logic/pl_event.c
  logic/pl_util.c
  logic/pl_scan.c
  logic/pl_wifi.c)
```

- [ ] **Step 6: Build all four boards**

Run:

```bash
. ~/.espressif/tools/activate_idf_v6.0.3.sh
SKIP_ART_CHECK=1 firmware/ports/esp32/tools/build-all.sh
```

Expected, for each of `amoled-175`, `amoled-175c`, `devkit`, `lcd-154` (alphabetical): `== <board>` followed by `check-size: <board> ok, <n> of 6291456 bytes (<p>%)`; exit status 0.

- [ ] **Step 7: Commit**

```bash
git add firmware/ports/esp32/main \
  firmware/ports/esp32/host-tests
git commit -m "feat(esp32): Wi-Fi HAL with retry policy and scan that works while retrying"
```
### Task 6: WebSocket client HAL

**Files:**
- Create: `firmware/ports/esp32/main/logic/pl_wsasm.h`, `firmware/ports/esp32/main/logic/pl_wsasm.c`
- Create: `firmware/ports/esp32/host-tests/test_pl_wsasm.c`
- Create: `firmware/ports/esp32/main/hal_ws.c`
- Modify: `firmware/ports/esp32/main/CMakeLists.txt`, `firmware/ports/esp32/host-tests/CMakeLists.txt`

**Interfaces:**
- Consumes: WebSocket HAL prototypes and rules (contract §2.5: one connection, `ws://<host>:<port>/gadget`, subprotocol `openmausbot-gadget.1`, no Origin, 5 s connect timeout, no automatic reconnect, whole messages up to 16 KiB text / 8 KiB binary, exactly one `GADGET_EV_WS_CLOSED` after every successful `hal_ws_open()`, `BUSY` when not open; ESP32 notes: `buffer_size = 16*1024 + 64`, `disable_auto_reconnect = true`, `subprotocol`); `GADGET_TEXT_FRAME_MAX`, `GADGET_BINARY_FRAME_MAX`, `GADGET_SUBPROTOCOL`; `pl_ws_uri` (Task 4); `port_post_event` (Task 3).
- Produces: `pl_wsasm_t`, `pl_wsasm_init/reset/feed` returning `PL_WS_NONE/TEXT/BINARY/CONTROL/TOO_BIG/PROTOCOL`; `port_ws_init()`; `void port_ws_note(gadget_event_type_t)` (called by `port_drain()`); `hal_ws_open`, `hal_ws_send_text`, `hal_ws_send_binary`, `hal_ws_close`. `hal_ws_open()` returns `GADGET_ERR_BUSY` until core has seen the previous connection's `GADGET_EV_WS_CLOSED`, and `GADGET_ERR_ARG` for a host that is not a DNS name or dotted IPv4. An oversize message closes with 1009, a fragment-order error with 1002; pings and pongs become `GADGET_EV_WS_CONTROL`.

- [ ] **Step 1: Write the failing test**

`firmware/ports/esp32/host-tests/test_pl_wsasm.c`:

```c
/* SPDX-License-Identifier: Apache-2.0 */
#include <string.h>

#include "pl_wsasm.h"
#include "unity.h"

static uint8_t buf[GADGET_TEXT_FRAME_MAX];
static pl_wsasm_t a;
static const uint8_t *msg;
static size_t msg_len;

void setUp(void) {
  pl_wsasm_init(&a, buf, GADGET_TEXT_FRAME_MAX, GADGET_BINARY_FRAME_MAX);
  msg = NULL;
  msg_len = 0;
}
void tearDown(void) {}

static pl_ws_out_t feed(uint8_t op, bool fin, size_t plen, size_t off, const char *s) {
  return pl_wsasm_feed(&a, op, fin, plen, off, (const uint8_t *)s, strlen(s), &msg, &msg_len);
}

static void test_single_piece_text(void) {
  TEST_ASSERT_EQUAL(PL_WS_TEXT, feed(PL_WS_OP_TEXT, true, 12, 0, "{\"op\":\"x\"}ab"));
  TEST_ASSERT_EQUAL_size_t(12, msg_len);
}

static void test_frame_split_across_events(void) {
  TEST_ASSERT_EQUAL(PL_WS_NONE, feed(PL_WS_OP_TEXT, true, 10, 0, "hello"));
  TEST_ASSERT_EQUAL(PL_WS_TEXT, feed(PL_WS_OP_TEXT, true, 10, 5, "world"));
  TEST_ASSERT_EQUAL_size_t(10, msg_len);
  TEST_ASSERT_EQUAL_MEMORY("helloworld", msg, 10);
}

static void test_fragmented_message_with_ping_between(void) {
  TEST_ASSERT_EQUAL(PL_WS_NONE, feed(PL_WS_OP_BINARY, false, 3, 0, "\x02\x01""a"));
  TEST_ASSERT_EQUAL(PL_WS_CONTROL, feed(PL_WS_OP_PING, true, 0, 0, ""));
  TEST_ASSERT_EQUAL(PL_WS_NONE, feed(PL_WS_OP_CONT, false, 2, 0, "bc"));
  TEST_ASSERT_EQUAL(PL_WS_BINARY, feed(PL_WS_OP_CONT, true, 2, 0, "de"));
  TEST_ASSERT_EQUAL_size_t(7, msg_len);
  TEST_ASSERT_EQUAL_MEMORY("\x02\x01""abcde", msg, 7);
}

static void test_empty_text_message(void) {
  TEST_ASSERT_EQUAL(PL_WS_TEXT, pl_wsasm_feed(&a, PL_WS_OP_TEXT, true, 0, 0, NULL, 0, &msg, &msg_len));
  TEST_ASSERT_EQUAL_size_t(0, msg_len);
}

static void test_binary_over_8k_is_too_big_and_sticks(void) {
  static uint8_t big[GADGET_BINARY_FRAME_MAX + 1];
  TEST_ASSERT_EQUAL(PL_WS_TOO_BIG,
                    pl_wsasm_feed(&a, PL_WS_OP_BINARY, true, sizeof(big), 0, big, sizeof(big), &msg, &msg_len));
  TEST_ASSERT_EQUAL(PL_WS_NONE, feed(PL_WS_OP_TEXT, true, 2, 0, "ok"));
  pl_wsasm_reset(&a);
  TEST_ASSERT_EQUAL(PL_WS_TEXT, feed(PL_WS_OP_TEXT, true, 2, 0, "ok"));
}

static void test_text_of_exactly_16k_is_accepted(void) {
  static uint8_t big[GADGET_TEXT_FRAME_MAX];
  memset(big, 'a', sizeof(big));
  TEST_ASSERT_EQUAL(PL_WS_NONE, pl_wsasm_feed(&a, PL_WS_OP_TEXT, true, sizeof(big), 0, big, 8000, &msg, &msg_len));
  TEST_ASSERT_EQUAL(PL_WS_TEXT, pl_wsasm_feed(&a, PL_WS_OP_TEXT, true, sizeof(big), 8000, big + 8000,
                                              sizeof(big) - 8000, &msg, &msg_len));
  TEST_ASSERT_EQUAL_size_t(GADGET_TEXT_FRAME_MAX, msg_len);
}

static void test_protocol_errors(void) {
  TEST_ASSERT_EQUAL(PL_WS_PROTOCOL, feed(PL_WS_OP_CONT, true, 1, 0, "x"));
  pl_wsasm_reset(&a);
  TEST_ASSERT_EQUAL(PL_WS_NONE, feed(PL_WS_OP_TEXT, false, 1, 0, "x"));
  TEST_ASSERT_EQUAL(PL_WS_PROTOCOL, feed(PL_WS_OP_TEXT, true, 1, 0, "y"));
  pl_wsasm_reset(&a);
  TEST_ASSERT_EQUAL(PL_WS_PROTOCOL, feed(0x3, true, 1, 0, "z"));
}

static void test_close_frame_is_ignored(void) {
  TEST_ASSERT_EQUAL(PL_WS_NONE, feed(PL_WS_OP_CLOSE, true, 2, 0, "\x03\xe8"));
  TEST_ASSERT_EQUAL(PL_WS_TEXT, feed(PL_WS_OP_TEXT, true, 1, 0, "k"));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_single_piece_text);
  RUN_TEST(test_frame_split_across_events);
  RUN_TEST(test_fragmented_message_with_ping_between);
  RUN_TEST(test_empty_text_message);
  RUN_TEST(test_binary_over_8k_is_too_big_and_sticks);
  RUN_TEST(test_text_of_exactly_16k_is_accepted);
  RUN_TEST(test_protocol_errors);
  RUN_TEST(test_close_frame_is_ignored);
  return UNITY_END();
}
```

Append to `firmware/ports/esp32/host-tests/CMakeLists.txt`:

```cmake
port_test(test_pl_wsasm)
```

- [ ] **Step 2: Run it to verify it fails**

Run: `cmake -S firmware/ports/esp32/host-tests -B build/esp32-host && cmake --build build/esp32-host -j10 && ctest --test-dir build/esp32-host --output-on-failure`

Expected: FAIL — the build stops with `fatal error: 'pl_wsasm.h' file not found` (clang; GCC prints `pl_wsasm.h: No such file or directory`).

- [ ] **Step 3: Write the reassembly**

`firmware/ports/esp32/main/logic/pl_wsasm.h`:

```c
/* SPDX-License-Identifier: Apache-2.0 */
/* Reassembles esp_websocket_client WEBSOCKET_EVENT_DATA pieces into whole
 * messages. A frame larger than the client's buffer arrives as several
 * pieces (payload_offset, payload_len), and a fragmented message arrives as
 * a TEXT or BINARY frame followed by CONTINUATION frames until FIN. */
#ifndef PL_WSASM_H
#define PL_WSASM_H

#include "gadget_types.h"

#define PL_WS_OP_CONT 0x0u
#define PL_WS_OP_TEXT 0x1u
#define PL_WS_OP_BINARY 0x2u
#define PL_WS_OP_CLOSE 0x8u
#define PL_WS_OP_PING 0x9u
#define PL_WS_OP_PONG 0xAu

typedef enum {
  PL_WS_NONE = 0,   /* nothing complete yet */
  PL_WS_TEXT,       /* msg, msg_len: one whole text message */
  PL_WS_BINARY,     /* msg, msg_len: one whole binary message */
  PL_WS_CONTROL,    /* a ping or pong arrived */
  PL_WS_TOO_BIG,    /* message over its limit: close with 1009 */
  PL_WS_PROTOCOL    /* bad opcode or fragment order: close with 1002 */
} pl_ws_out_t;

typedef struct {
  uint8_t *buf;          /* at least max(text_max, binary_max) bytes */
  size_t text_max;
  size_t binary_max;
  size_t len;            /* bytes of the message in progress */
  uint8_t kind;          /* 0, PL_WS_OP_TEXT or PL_WS_OP_BINARY */
  bool failed;           /* a TOO_BIG/PROTOCOL was reported; ignore input until reset */
} pl_wsasm_t;

void pl_wsasm_init(pl_wsasm_t *a, uint8_t *buf, size_t text_max, size_t binary_max);
void pl_wsasm_reset(pl_wsasm_t *a);
/* Feed one piece. On PL_WS_TEXT/PL_WS_BINARY, *msg points into a->buf and
 * stays valid until the next call. */
pl_ws_out_t pl_wsasm_feed(pl_wsasm_t *a, uint8_t op, bool fin, size_t payload_len, size_t payload_offset,
                          const uint8_t *data, size_t len, const uint8_t **msg, size_t *msg_len);

#endif /* PL_WSASM_H */
```

`firmware/ports/esp32/main/logic/pl_wsasm.c`:

```c
/* SPDX-License-Identifier: Apache-2.0 */
#include "pl_wsasm.h"

#include <string.h>

void pl_wsasm_init(pl_wsasm_t *a, uint8_t *buf, size_t text_max, size_t binary_max) {
  a->buf = buf;
  a->text_max = text_max;
  a->binary_max = binary_max;
  pl_wsasm_reset(a);
}

void pl_wsasm_reset(pl_wsasm_t *a) {
  a->len = 0;
  a->kind = 0;
  a->failed = false;
}

static pl_ws_out_t fail(pl_wsasm_t *a, pl_ws_out_t why) {
  a->failed = true;
  a->kind = 0;
  a->len = 0;
  return why;
}

pl_ws_out_t pl_wsasm_feed(pl_wsasm_t *a, uint8_t op, bool fin, size_t payload_len, size_t payload_offset,
                          const uint8_t *data, size_t len, const uint8_t **msg, size_t *msg_len) {
  if (a->failed) {
    return PL_WS_NONE;
  }
  if (op == PL_WS_OP_PING || op == PL_WS_OP_PONG) {
    return payload_offset == 0 ? PL_WS_CONTROL : PL_WS_NONE;
  }
  if (op == PL_WS_OP_CLOSE) {
    return PL_WS_NONE; /* the client reports closure through WEBSOCKET_EVENT_FINISH */
  }
  if (op == PL_WS_OP_TEXT || op == PL_WS_OP_BINARY) {
    if (payload_offset == 0) {
      if (a->kind != 0) {
        return fail(a, PL_WS_PROTOCOL); /* a new message before the last one finished */
      }
      a->kind = op;
      a->len = 0;
    } else if (a->kind != op) {
      return fail(a, PL_WS_PROTOCOL);
    }
  } else if (op == PL_WS_OP_CONT) {
    if (a->kind == 0) {
      return fail(a, PL_WS_PROTOCOL);
    }
  } else {
    return fail(a, PL_WS_PROTOCOL);
  }
  size_t max = a->kind == PL_WS_OP_TEXT ? a->text_max : a->binary_max;
  if (len > max - a->len) {
    return fail(a, PL_WS_TOO_BIG);
  }
  if (len > 0) {
    memcpy(a->buf + a->len, data, len);
  }
  a->len += len;
  bool frame_done = payload_offset + len >= payload_len;
  if (!(frame_done && fin)) {
    return PL_WS_NONE;
  }
  pl_ws_out_t out = a->kind == PL_WS_OP_TEXT ? PL_WS_TEXT : PL_WS_BINARY;
  *msg = a->buf;
  *msg_len = a->len;
  a->kind = 0;
  return out;
}
```

In `firmware/ports/esp32/host-tests/CMakeLists.txt`, replace the `set(PORT_LOGIC_SRCS ...)` block with:

```cmake
set(PORT_LOGIC_SRCS
  ${CORE_DIR}/src/boards.c
  ${ESP_DIR}/main/logic/pl_event.c
  ${ESP_DIR}/main/logic/pl_util.c
  ${ESP_DIR}/main/logic/pl_scan.c
  ${ESP_DIR}/main/logic/pl_wifi.c
  ${ESP_DIR}/main/logic/pl_wsasm.c
)
```

- [ ] **Step 4: Run the tests to verify they pass**

Run: `cmake -S firmware/ports/esp32/host-tests -B build/esp32-host && cmake --build build/esp32-host -j10 && ctest --test-dir build/esp32-host --output-on-failure`

Expected: `100% tests passed, 0 tests failed out of 7`.

- [ ] **Step 5: Write the WebSocket HAL**

All blocking client calls (`init`, `start`, `send_*`, `close_with_code`, `stop`, `destroy`) run on the `ws` worker task in the order the gadget task queued them, so a send queued before a close always goes out first. `CONFIG_ESP_WS_CLIENT_SEPARATE_TX_LOCK=y` (Task 2 defaults) keeps sends from waiting behind the client's receive loop.

`firmware/ports/esp32/main/hal_ws.c`:

```c
/* SPDX-License-Identifier: Apache-2.0 */
/* HAL WebSocket group on espressif/esp_websocket_client (contract §2.5).
 *
 * One worker task owns the client: it opens, sends, closes and destroys in
 * the order the gadget task asked, so no hal_ws_* call ever blocks on the
 * network. The client's own task runs on_ws_event(), which reassembles
 * messages (pl_wsasm) and queues them for core. WEBSOCKET_EVENT_FINISH is
 * dispatched exactly once when the client task ends, whatever the reason
 * (connect failure, peer close, our close, network error); it becomes the
 * one GADGET_EV_WS_CLOSED that follows every successful hal_ws_open(). */
#include <stdlib.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_websocket_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "gadget_hal.h"
#include "pl_util.h"
#include "pl_wsasm.h"
#include "port.h"

#define WS_CMD_QUEUE_LEN 48
#define WS_SEND_TIMEOUT_MS 5000
#define WS_CLOSE_TIMEOUT_MS 2000
#define WS_CONNECT_TIMEOUT_MS 5000
#define WS_URI_MAX 300

typedef enum { WS_CMD_OPEN = 1, WS_CMD_TEXT, WS_CMD_BINARY, WS_CMD_CLOSE, WS_CMD_REAP } ws_cmd_kind_t;

typedef struct {
  ws_cmd_kind_t kind;
  uint32_t gen;      /* the hal_ws_open() this belongs to */
  uint16_t code;     /* CLOSE */
  char *uri;         /* OPEN, malloc'd */
  uint8_t *data;     /* TEXT/BINARY, malloc'd */
  size_t len;
} ws_cmd_t;

typedef struct {
  uint32_t gen;
  pl_wsasm_t asm_;
  uint8_t *buf;
  uint16_t close_code;  /* from the peer's close frame */
} ws_conn_t;

static const char *TAG = "ws";
static QueueHandle_t s_cmds;

/* worker task only */
static esp_websocket_client_handle_t s_client;
static ws_conn_t *s_conn;

/* gadget task only */
static uint32_t s_gen;
static bool s_active; /* from hal_ws_open() until core saw GADGET_EV_WS_CLOSED */
static bool s_open;   /* between GADGET_EV_WS_OPEN and GADGET_EV_WS_CLOSED */

static void post_closed(uint16_t code) {
  gadget_event_t ev = {.type = GADGET_EV_WS_CLOSED};
  ev.u.closed.code = code;
  port_post_event(&ev);
}

static void free_conn(ws_conn_t *c) {
  if (c != NULL) {
    free(c->buf);
    free(c);
  }
}

/* Runs on the client's task. */
static void on_ws_event(void *arg, esp_event_base_t base, int32_t id, void *data) {
  (void)base;
  ws_conn_t *c = arg;
  const esp_websocket_event_data_t *d = data;
  switch (id) {
    case WEBSOCKET_EVENT_CONNECTED: {
      gadget_event_t ev = {.type = GADGET_EV_WS_OPEN};
      port_post_event(&ev);
      break;
    }
    case WEBSOCKET_EVENT_DATA: {
      if (d->op_code == PL_WS_OP_CLOSE && d->payload_offset == 0 && d->data_len >= 2) {
        c->close_code = (uint16_t)(((uint8_t)d->data_ptr[0] << 8) | (uint8_t)d->data_ptr[1]);
      }
      const uint8_t *msg = NULL;
      size_t len = 0;
      pl_ws_out_t out = pl_wsasm_feed(&c->asm_, d->op_code, d->fin, (size_t)d->payload_len, (size_t)d->payload_offset,
                                      (const uint8_t *)d->data_ptr, (size_t)d->data_len, &msg, &len);
      if (out == PL_WS_TEXT || out == PL_WS_BINARY) {
        gadget_event_t ev = {.type = out == PL_WS_TEXT ? GADGET_EV_WS_TEXT : GADGET_EV_WS_BINARY};
        ev.u.ws.data = msg;
        ev.u.ws.len = len;
        port_post_event(&ev);
      } else if (out == PL_WS_CONTROL) {
        gadget_event_t ev = {.type = GADGET_EV_WS_CONTROL};
        port_post_event(&ev);
      } else if (out == PL_WS_TOO_BIG || out == PL_WS_PROTOCOL) {
        ESP_LOGW(TAG, "closing: %s", out == PL_WS_TOO_BIG ? "message too big" : "protocol error");
        ws_cmd_t cmd = {.kind = WS_CMD_CLOSE, .gen = c->gen, .code = out == PL_WS_TOO_BIG ? 1009 : 1002};
        xQueueSend(s_cmds, &cmd, 0);
      }
      break;
    }
    case WEBSOCKET_EVENT_FINISH: {
      post_closed(c->close_code);
      ws_cmd_t cmd = {.kind = WS_CMD_REAP, .gen = c->gen};
      xQueueSend(s_cmds, &cmd, pdMS_TO_TICKS(WS_SEND_TIMEOUT_MS));
      break;
    }
    default:
      break;
  }
}

static void reap(void) {
  if (s_client != NULL) {
    esp_websocket_client_destroy(s_client); /* waits for the client task to stop */
    s_client = NULL;
  }
  free_conn(s_conn);
  s_conn = NULL;
}

static void do_open(const ws_cmd_t *cmd) {
  reap();
  ws_conn_t *c = calloc(1, sizeof(*c));
  uint8_t *buf = heap_caps_malloc(GADGET_TEXT_FRAME_MAX, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (buf == NULL) {
    buf = malloc(GADGET_TEXT_FRAME_MAX);
  }
  if (c == NULL || buf == NULL) {
    free(c);
    free(buf);
    post_closed(0);
    return;
  }
  c->gen = cmd->gen;
  c->buf = buf;
  pl_wsasm_init(&c->asm_, buf, GADGET_TEXT_FRAME_MAX, GADGET_BINARY_FRAME_MAX);

  const esp_websocket_client_config_t cfg = {
    .uri = cmd->uri,
    .subprotocol = GADGET_SUBPROTOCOL,
    .buffer_size = GADGET_TEXT_FRAME_MAX + 64,
    .disable_auto_reconnect = true,    /* core drives reconnects (spec §4.3) */
    .disable_pingpong_discon = true,   /* core's 45 s liveness timer decides */
    .network_timeout_ms = WS_CONNECT_TIMEOUT_MS,
    .task_stack = 6144,
    .task_prio = 5,
  };
  esp_websocket_client_handle_t h = esp_websocket_client_init(&cfg);
  if (h == NULL) {
    free_conn(c);
    post_closed(0);
    return;
  }
  if (esp_websocket_register_events(h, WEBSOCKET_EVENT_ANY, on_ws_event, c) != ESP_OK ||
      esp_websocket_client_start(h) != ESP_OK) {
    esp_websocket_client_destroy(h);
    free_conn(c);
    post_closed(0);
    return;
  }
  s_client = h;
  s_conn = c;
}

static bool current(uint32_t gen) { return s_client != NULL && s_conn != NULL && s_conn->gen == gen; }

static void ws_task(void *arg) {
  (void)arg;
  ws_cmd_t cmd;
  for (;;) {
    if (xQueueReceive(s_cmds, &cmd, portMAX_DELAY) != pdTRUE) {
      continue;
    }
    switch (cmd.kind) {
      case WS_CMD_OPEN:
        do_open(&cmd);
        free(cmd.uri);
        break;
      case WS_CMD_TEXT:
      case WS_CMD_BINARY:
        if (current(cmd.gen) && esp_websocket_client_is_connected(s_client)) {
          int r = cmd.kind == WS_CMD_TEXT
                      ? esp_websocket_client_send_text(s_client, (const char *)cmd.data, (int)cmd.len,
                                                       pdMS_TO_TICKS(WS_SEND_TIMEOUT_MS))
                      : esp_websocket_client_send_bin(s_client, (const char *)cmd.data, (int)cmd.len,
                                                      pdMS_TO_TICKS(WS_SEND_TIMEOUT_MS));
          if (r < 0) {
            ESP_LOGW(TAG, "send failed; the client drops the connection"); /* FINISH follows */
          }
        }
        free(cmd.data);
        break;
      case WS_CMD_CLOSE:
        if (current(cmd.gen)) {
          esp_err_t e = ESP_FAIL;
          if (esp_websocket_client_is_connected(s_client)) {
            e = esp_websocket_client_close_with_code(s_client, cmd.code, NULL, 0, pdMS_TO_TICKS(WS_CLOSE_TIMEOUT_MS));
          }
          if (e != ESP_OK) {
            esp_websocket_client_stop(s_client); /* still connecting, or the close handshake failed */
          }
        }
        break;
      case WS_CMD_REAP:
        if (current(cmd.gen)) {
          reap();
        }
        break;
    }
  }
}

esp_err_t port_ws_init(void) {
  s_cmds = xQueueCreate(WS_CMD_QUEUE_LEN, sizeof(ws_cmd_t));
  if (s_cmds == NULL) {
    return ESP_ERR_NO_MEM;
  }
  return xTaskCreatePinnedToCore(ws_task, "ws", 4096, NULL, 5, NULL, 0) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}

void port_ws_note(gadget_event_type_t type) {
  if (type == GADGET_EV_WS_OPEN) {
    s_open = true;
  } else if (type == GADGET_EV_WS_CLOSED) {
    s_open = false;
    s_active = false;
  }
}

gadget_status_t hal_ws_open(const char *host, uint16_t port) {
  if (s_active) {
    return GADGET_ERR_BUSY; /* the previous connection has not reported CLOSED yet */
  }
  char uri[WS_URI_MAX];
  gadget_status_t st = pl_ws_uri(uri, sizeof(uri), host, port);
  if (st != GADGET_OK) {
    return st;
  }
  ws_cmd_t cmd = {.kind = WS_CMD_OPEN, .gen = s_gen + 1, .uri = strdup(uri)};
  if (cmd.uri == NULL) {
    return GADGET_ERR_NO_MEM;
  }
  if (xQueueSend(s_cmds, &cmd, 0) != pdTRUE) {
    free(cmd.uri);
    return GADGET_ERR_BUSY;
  }
  s_gen = cmd.gen;
  s_active = true;
  s_open = false;
  return GADGET_OK;
}

static gadget_status_t queue_send(ws_cmd_kind_t kind, const void *data, size_t len, size_t max) {
  if (!s_open) {
    return GADGET_ERR_BUSY;
  }
  if (len > max) {
    return GADGET_ERR_LIMIT;
  }
  ws_cmd_t cmd = {.kind = kind, .gen = s_gen, .len = len, .data = malloc(len > 0 ? len : 1)};
  if (cmd.data == NULL) {
    return GADGET_ERR_NO_MEM;
  }
  if (len > 0) {
    memcpy(cmd.data, data, len);
  }
  if (xQueueSend(s_cmds, &cmd, 0) != pdTRUE) {
    free(cmd.data);
    return GADGET_ERR_BUSY;
  }
  return GADGET_OK;
}

gadget_status_t hal_ws_send_text(const char *data, size_t len) {
  return queue_send(WS_CMD_TEXT, data, len, GADGET_TEXT_FRAME_MAX);
}

gadget_status_t hal_ws_send_binary(const uint8_t *data, size_t len) {
  return queue_send(WS_CMD_BINARY, data, len, GADGET_BINARY_FRAME_MAX);
}

void hal_ws_close(uint16_t code) {
  if (!s_active) {
    return;
  }
  ws_cmd_t cmd = {.kind = WS_CMD_CLOSE, .gen = s_gen, .code = code};
  if (xQueueSend(s_cmds, &cmd, pdMS_TO_TICKS(1000)) != pdTRUE) {
    ESP_LOGE(TAG, "close request dropped: command queue full");
  }
}
```

In `firmware/ports/esp32/main/CMakeLists.txt`, replace the `set(port_srcs ...)` and `set(logic_srcs ...)` blocks with:

```cmake
set(port_srcs
  main.c
  port_events.c
  hal_system.c
  console_usj.c
  hal_storage.c
  hal_wifi.c
  hal_ws.c)

set(logic_srcs
  logic/pl_event.c
  logic/pl_util.c
  logic/pl_scan.c
  logic/pl_wifi.c
  logic/pl_wsasm.c)
```

- [ ] **Step 6: Build all four boards**

Run:

```bash
. ~/.espressif/tools/activate_idf_v6.0.3.sh
SKIP_ART_CHECK=1 firmware/ports/esp32/tools/build-all.sh
```

Expected, for each of `amoled-175`, `amoled-175c`, `devkit`, `lcd-154` (alphabetical): `== <board>` followed by `check-size: <board> ok, <n> of 6291456 bytes (<p>%)`; exit status 0.

- [ ] **Step 7: Commit**

```bash
git add firmware/ports/esp32/main \
  firmware/ports/esp32/host-tests
git commit -m "feat(esp32): WebSocket HAL on esp_websocket_client with message reassembly"
```
### Task 7: mDNS browse HAL

**Files:**
- Create: `firmware/ports/esp32/main/logic/pl_mdns.h`, `firmware/ports/esp32/main/logic/pl_mdns.c`
- Create: `firmware/ports/esp32/host-tests/test_pl_mdns.c`
- Create: `firmware/ports/esp32/main/hal_mdns.c`
- Modify: `firmware/ports/esp32/main/CMakeLists.txt`, `firmware/ports/esp32/host-tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `hal_mdns_browse(uint32_t timeout_ms)` (contract §2.5: browse `_openmausbot._tcp`, one `GADGET_EV_MDNS` with every instance and its TXT `id=`); `gadget_mdns_host_t {name[64], address[48], id[33]}`, at most 8 hosts (§2.4); TXT `id` = 32 lowercase hex (spec §4.1); `pl_utf8_trunc` (Task 4).
- Produces: `pl_mdns_in_t`, `bool pl_mdns_txt_is_id(const char*)`, `bool pl_mdns_host(const pl_mdns_in_t*, gadget_mdns_host_t*)` (skips answers without IPv4 or with port 0; `id` is `""` unless it is canonical), `uint8_t pl_mdns_add(list, count, cap, h)` (dedupe by address), `uint32_t pl_mdns_ptr_ms(timeout_ms)` and `uint32_t pl_mdns_a_ms(now, deadline)` (the browse's time budget); `port_mdns_init()`; `hal_mdns_browse()` (`GADGET_ERR_BUSY` while a browse runs). The whole browse ends within `timeout_ms` (contract §2.5; core gives up 1 s after it): the PTR query gets `timeout_ms − 1000` when `timeout_ms` > 2000 (else all of it), and when an answer carries no A record the port asks for the host's address (`mdns_query_a`, at most 1 s) only while 200 ms or more remain before the deadline.

- [ ] **Step 1: Write the failing test**

`firmware/ports/esp32/host-tests/test_pl_mdns.c`:

```c
/* SPDX-License-Identifier: Apache-2.0 */
#include <string.h>

#include "pl_mdns.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

static const char *ID = "000102030405060708090a0b0c0d0e0f";

static void test_full_answer(void) {
  pl_mdns_in_t in = {"Omkar's computer", "omkar-mac", 8810, true, {192, 168, 1, 20}, ID, 32};
  gadget_mdns_host_t h;
  TEST_ASSERT_TRUE(pl_mdns_host(&in, &h));
  TEST_ASSERT_EQUAL_STRING("Omkar's computer", h.name);
  TEST_ASSERT_EQUAL_STRING("192.168.1.20:8810", h.address);
  TEST_ASSERT_EQUAL_STRING(ID, h.id);
}

static void test_bad_or_missing_id_is_empty(void) {
  gadget_mdns_host_t h;
  pl_mdns_in_t in = {"x", NULL, 8810, true, {10, 0, 0, 2}, "000102030405060708090A0B0C0D0E0F", 32};
  TEST_ASSERT_TRUE(pl_mdns_host(&in, &h));
  TEST_ASSERT_EQUAL_STRING("", h.id); /* uppercase is not the canonical form */
  in.txt_id = ID;
  in.txt_id_len = 31;
  TEST_ASSERT_TRUE(pl_mdns_host(&in, &h));
  TEST_ASSERT_EQUAL_STRING("", h.id);
  in.txt_id = NULL;
  in.txt_id_len = 0;
  TEST_ASSERT_TRUE(pl_mdns_host(&in, &h));
  TEST_ASSERT_EQUAL_STRING("", h.id);
}

static void test_name_fallbacks_and_cut(void) {
  gadget_mdns_host_t h;
  pl_mdns_in_t in = {NULL, "omkar-mac", 8810, true, {10, 0, 0, 2}, NULL, 0};
  TEST_ASSERT_TRUE(pl_mdns_host(&in, &h));
  TEST_ASSERT_EQUAL_STRING("omkar-mac", h.name);
  in.hostname = "";
  TEST_ASSERT_TRUE(pl_mdns_host(&in, &h));
  TEST_ASSERT_EQUAL_STRING("MausBot", h.name);
  char longname[100];
  memset(longname, 'n', 99);
  longname[99] = '\0';
  in.instance = longname;
  TEST_ASSERT_TRUE(pl_mdns_host(&in, &h));
  TEST_ASSERT_EQUAL_size_t(63, strlen(h.name));
}

static void test_unusable_answers(void) {
  gadget_mdns_host_t h;
  pl_mdns_in_t in = {"x", NULL, 8810, false, {0, 0, 0, 0}, NULL, 0};
  TEST_ASSERT_FALSE(pl_mdns_host(&in, &h));
  in.has_ipv4 = true;
  in.port = 0;
  TEST_ASSERT_FALSE(pl_mdns_host(&in, &h));
}

static void test_txt_key_and_list_dedupe(void) {
  TEST_ASSERT_TRUE(pl_mdns_txt_is_id("id"));
  TEST_ASSERT_TRUE(pl_mdns_txt_is_id("ID"));
  TEST_ASSERT_FALSE(pl_mdns_txt_is_id("idx"));
  TEST_ASSERT_FALSE(pl_mdns_txt_is_id("v"));
  gadget_mdns_host_t list[2], a = {"A", "10.0.0.1:8810", ""}, b = {"B", "10.0.0.2:8810", ""}, c = {"C", "10.0.0.3:8810", ""};
  uint8_t n = 0;
  n = pl_mdns_add(list, n, 2, &a);
  n = pl_mdns_add(list, n, 2, &a);
  TEST_ASSERT_EQUAL_UINT8(1, n);
  n = pl_mdns_add(list, n, 2, &b);
  n = pl_mdns_add(list, n, 2, &c);
  TEST_ASSERT_EQUAL_UINT8(2, n);
  TEST_ASSERT_EQUAL_STRING("B", list[1].name);
}

/* The whole browse fits in timeout_ms: core gives up 1 s after it. */
static void test_browse_budget(void) {
  TEST_ASSERT_EQUAL_UINT32(4000, pl_mdns_ptr_ms(5000)); /* GADGET_HOST_AUTO_TIMEOUT_MS */
  TEST_ASSERT_EQUAL_UINT32(1001, pl_mdns_ptr_ms(2001));
  TEST_ASSERT_EQUAL_UINT32(2000, pl_mdns_ptr_ms(2000));
  TEST_ASSERT_EQUAL_UINT32(500, pl_mdns_ptr_ms(500));
  TEST_ASSERT_EQUAL_UINT32(PL_MDNS_A_TIMEOUT_MS, pl_mdns_a_ms(10000, 15000));
  TEST_ASSERT_EQUAL_UINT32(500, pl_mdns_a_ms(14500, 15000));
  TEST_ASSERT_EQUAL_UINT32(201, pl_mdns_a_ms(14799, 15000));
  TEST_ASSERT_EQUAL_UINT32(0, pl_mdns_a_ms(14800, 15000)); /* under 200 ms left: no lookup */
  TEST_ASSERT_EQUAL_UINT32(0, pl_mdns_a_ms(16000, 15000));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_full_answer);
  RUN_TEST(test_bad_or_missing_id_is_empty);
  RUN_TEST(test_name_fallbacks_and_cut);
  RUN_TEST(test_unusable_answers);
  RUN_TEST(test_txt_key_and_list_dedupe);
  RUN_TEST(test_browse_budget);
  return UNITY_END();
}
```

Append to `firmware/ports/esp32/host-tests/CMakeLists.txt`:

```cmake
port_test(test_pl_mdns)
```

- [ ] **Step 2: Run it to verify it fails**

Run: `cmake -S firmware/ports/esp32/host-tests -B build/esp32-host && cmake --build build/esp32-host -j10 && ctest --test-dir build/esp32-host --output-on-failure`

Expected: FAIL — the build stops with `fatal error: 'pl_mdns.h' file not found` (clang; GCC prints `pl_mdns.h: No such file or directory`).

- [ ] **Step 3: Write the answer conversion**

`firmware/ports/esp32/main/logic/pl_mdns.h`:

```c
/* SPDX-License-Identifier: Apache-2.0 */
/* Turns one mDNS browse answer for _openmausbot._tcp into a
 * gadget_mdns_host_t (gadget_events.h). */
#ifndef PL_MDNS_H
#define PL_MDNS_H

#include "gadget_events.h"

#define PL_MDNS_MAX_HOSTS 8u
#define PL_MDNS_A_TIMEOUT_MS 1000u   /* longest single address lookup */

typedef struct {
  const char *instance;     /* service instance name, NULL when absent */
  const char *hostname;     /* target host, NULL when absent */
  uint16_t port;
  bool has_ipv4;
  uint8_t ipv4[4];          /* network order: a.b.c.d */
  const char *txt_id;       /* value of TXT key "id", NULL when absent */
  size_t txt_id_len;
} pl_mdns_in_t;

/* DNS-SD TXT keys compare case-insensitively. */
bool pl_mdns_txt_is_id(const char *key);
/* false when the answer cannot be used (no IPv4 address or port 0).
 * name = instance, else hostname, else "MausBot" (cut to 63 bytes on a
 * UTF-8 boundary); address = "a.b.c.d:port"; id = the TXT id when it is
 * exactly 32 lowercase hex characters, else "". */
bool pl_mdns_host(const pl_mdns_in_t *in, gadget_mdns_host_t *out);
/* Append h unless its address is already listed or count == cap.
 * Returns the new count. */
uint8_t pl_mdns_add(gadget_mdns_host_t *list, uint8_t count, uint8_t cap, const gadget_mdns_host_t *h);
/* A browse of timeout_ms ends by its deadline (contract §2.5). The PTR query
 * gets timeout_ms - 1000 when timeout_ms > 2000 (the rest is for address
 * lookups), else all of it. */
uint32_t pl_mdns_ptr_ms(uint32_t timeout_ms);
/* Timeout for one address lookup started at `now`: at most
 * PL_MDNS_A_TIMEOUT_MS and never past `deadline`; 0 (skip the lookup) when
 * fewer than 200 ms are left. */
uint32_t pl_mdns_a_ms(uint64_t now, uint64_t deadline);

#endif /* PL_MDNS_H */
```

`firmware/ports/esp32/main/logic/pl_mdns.c`:

```c
/* SPDX-License-Identifier: Apache-2.0 */
#include "pl_mdns.h"

#include <stdio.h>
#include <string.h>

#include "pl_util.h"

bool pl_mdns_txt_is_id(const char *key) {
  return key != NULL && (key[0] == 'i' || key[0] == 'I') && (key[1] == 'd' || key[1] == 'D') && key[2] == '\0';
}

static bool hex32(const char *s, size_t n) {
  if (s == NULL || n != GADGET_HOST_ID_LEN) {
    return false;
  }
  for (size_t i = 0; i < n; i++) {
    char c = s[i];
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) {
      return false;
    }
  }
  return true;
}

bool pl_mdns_host(const pl_mdns_in_t *in, gadget_mdns_host_t *out) {
  if (!in->has_ipv4 || in->port == 0) {
    return false;
  }
  memset(out, 0, sizeof(*out));
  const char *name = in->instance != NULL && in->instance[0] != '\0' ? in->instance
                     : in->hostname != NULL && in->hostname[0] != '\0' ? in->hostname
                                                                        : "MausBot";
  pl_utf8_trunc(out->name, sizeof(out->name), name);
  snprintf(out->address, sizeof(out->address), "%u.%u.%u.%u:%u", (unsigned)in->ipv4[0], (unsigned)in->ipv4[1],
           (unsigned)in->ipv4[2], (unsigned)in->ipv4[3], (unsigned)in->port);
  if (hex32(in->txt_id, in->txt_id_len)) {
    memcpy(out->id, in->txt_id, GADGET_HOST_ID_LEN);
    out->id[GADGET_HOST_ID_LEN] = '\0';
  }
  return true;
}

uint8_t pl_mdns_add(gadget_mdns_host_t *list, uint8_t count, uint8_t cap, const gadget_mdns_host_t *h) {
  for (uint8_t i = 0; i < count; i++) {
    if (strcmp(list[i].address, h->address) == 0) {
      return count;
    }
  }
  if (count >= cap) {
    return count;
  }
  list[count] = *h;
  return (uint8_t)(count + 1);
}

uint32_t pl_mdns_ptr_ms(uint32_t timeout_ms) { return timeout_ms > 2000u ? timeout_ms - 1000u : timeout_ms; }

uint32_t pl_mdns_a_ms(uint64_t now, uint64_t deadline) {
  if (now + 200u >= deadline) {
    return 0;
  }
  uint64_t left = deadline - now;
  return left < PL_MDNS_A_TIMEOUT_MS ? (uint32_t)left : PL_MDNS_A_TIMEOUT_MS;
}
```

In `firmware/ports/esp32/host-tests/CMakeLists.txt`, replace the `set(PORT_LOGIC_SRCS ...)` block with:

```cmake
set(PORT_LOGIC_SRCS
  ${CORE_DIR}/src/boards.c
  ${ESP_DIR}/main/logic/pl_event.c
  ${ESP_DIR}/main/logic/pl_util.c
  ${ESP_DIR}/main/logic/pl_scan.c
  ${ESP_DIR}/main/logic/pl_wifi.c
  ${ESP_DIR}/main/logic/pl_wsasm.c
  ${ESP_DIR}/main/logic/pl_mdns.c
)
```

- [ ] **Step 4: Run the tests to verify they pass**

Run: `cmake -S firmware/ports/esp32/host-tests -B build/esp32-host && cmake --build build/esp32-host -j10 && ctest --test-dir build/esp32-host --output-on-failure`

Expected: `100% tests passed, 0 tests failed out of 8`.

- [ ] **Step 5: Write the mDNS HAL**

`firmware/ports/esp32/main/hal_mdns.c`:

```c
/* SPDX-License-Identifier: Apache-2.0 */
/* HAL mDNS group: browse _openmausbot._tcp (spec §5.6 `host auto`). The
 * queries block, so they run on their own task, all within timeout_ms
 * (core stops waiting 1 s later), and the result reaches core as one
 * GADGET_EV_MDNS. */
#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "gadget_hal.h"
#include "mdns.h"
#include "pl_mdns.h"
#include "port.h"

#define MDNS_RESULTS_MAX 16

static const char *TAG = "mdns";
static QueueHandle_t s_req;
static volatile bool s_busy;

/* hal_now_ms()'s clock. Driver tasks call no hal_* function (contract §2.1). */
static uint64_t now_ms(void) { return (uint64_t)(esp_timer_get_time() / 1000); }

static void fill_ipv4(const mdns_result_t *r, pl_mdns_in_t *in, uint64_t deadline) {
  for (const mdns_ip_addr_t *a = r->addr; a != NULL; a = a->next) {
    if (a->addr.type == ESP_IPADDR_TYPE_V4) {
      memcpy(in->ipv4, &a->addr.u_addr.ip4.addr, 4); /* network order: a.b.c.d */
      in->has_ipv4 = true;
      return;
    }
  }
  /* No A record in the answer: ask for one, but only inside the deadline. */
  uint32_t wait_ms = pl_mdns_a_ms(now_ms(), deadline);
  esp_ip4_addr_t ip;
  if (r->hostname != NULL && wait_ms > 0 && mdns_query_a(r->hostname, wait_ms, &ip) == ESP_OK) {
    memcpy(in->ipv4, &ip.addr, 4);
    in->has_ipv4 = true;
  }
}

static void mdns_task(void *arg) {
  (void)arg;
  uint32_t timeout_ms;
  for (;;) {
    if (xQueueReceive(s_req, &timeout_ms, portMAX_DELAY) != pdTRUE) {
      continue;
    }
    const uint64_t deadline = now_ms() + timeout_ms;
    gadget_mdns_host_t hosts[PL_MDNS_MAX_HOSTS];
    uint8_t count = 0;
    mdns_result_t *res = NULL;
    esp_err_t err = mdns_query_ptr("_openmausbot", "_tcp", pl_mdns_ptr_ms(timeout_ms), MDNS_RESULTS_MAX, &res);
    if (err != ESP_OK) {
      ESP_LOGW(TAG, "browse failed: %s", esp_err_to_name(err));
    }
    for (const mdns_result_t *r = res; err == ESP_OK && r != NULL; r = r->next) {
      pl_mdns_in_t in = {.instance = r->instance_name, .hostname = r->hostname, .port = r->port};
      fill_ipv4(r, &in, deadline);
      for (size_t i = 0; i < r->txt_count; i++) {
        if (pl_mdns_txt_is_id(r->txt[i].key) && r->txt[i].value != NULL) {
          in.txt_id = r->txt[i].value;
          in.txt_id_len = r->txt_value_len != NULL ? r->txt_value_len[i] : strlen(r->txt[i].value);
        }
      }
      gadget_mdns_host_t h;
      if (pl_mdns_host(&in, &h)) {
        count = pl_mdns_add(hosts, count, PL_MDNS_MAX_HOSTS, &h);
      }
    }
    if (res != NULL) {
      mdns_query_results_free(res);
    }
    /* Posted by the deadline: every query above stopped inside it. */
    gadget_event_t ev = {.type = GADGET_EV_MDNS};
    ev.u.mdns.hosts = hosts;
    ev.u.mdns.count = count;
    ev.u.mdns.ok = err == ESP_OK;
    port_post_event(&ev);
    s_busy = false;
  }
}

esp_err_t port_mdns_init(void) {
  esp_err_t err = mdns_init();
  if (err != ESP_OK) {
    return err;
  }
  s_req = xQueueCreate(1, sizeof(uint32_t));
  if (s_req == NULL) {
    return ESP_ERR_NO_MEM;
  }
  return xTaskCreatePinnedToCore(mdns_task, "mdns_browse", 4096, NULL, 3, NULL, 0) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}

gadget_status_t hal_mdns_browse(uint32_t timeout_ms) {
  if (s_busy) {
    return GADGET_ERR_BUSY;
  }
  s_busy = true;
  if (xQueueSend(s_req, &timeout_ms, 0) != pdTRUE) {
    s_busy = false;
    return GADGET_ERR_BUSY;
  }
  return GADGET_OK;
}
```

In `firmware/ports/esp32/main/CMakeLists.txt`, replace the `set(port_srcs ...)` and `set(logic_srcs ...)` blocks with:

```cmake
set(port_srcs
  main.c
  port_events.c
  hal_system.c
  console_usj.c
  hal_storage.c
  hal_wifi.c
  hal_ws.c
  hal_mdns.c)

set(logic_srcs
  logic/pl_event.c
  logic/pl_util.c
  logic/pl_scan.c
  logic/pl_wifi.c
  logic/pl_wsasm.c
  logic/pl_mdns.c)
```

- [ ] **Step 6: Build all four boards**

Run:

```bash
. ~/.espressif/tools/activate_idf_v6.0.3.sh
SKIP_ART_CHECK=1 firmware/ports/esp32/tools/build-all.sh
```

Expected, for each of `amoled-175`, `amoled-175c`, `devkit`, `lcd-154` (alphabetical): `== <board>` followed by `check-size: <board> ok, <n> of 6291456 bytes (<p>%)`; exit status 0.

- [ ] **Step 7: Commit**

```bash
git add firmware/ports/esp32/main \
  firmware/ports/esp32/host-tests
git commit -m "feat(esp32): mDNS browse HAL for host auto"
```
### Task 8: Audio — mic and speaker HAL, codec and I2S drivers

**Files:**
- Create: `firmware/ports/esp32/main/logic/pl_audio.h`, `firmware/ports/esp32/main/logic/pl_audio.c`
- Create: `firmware/ports/esp32/host-tests/test_pl_audio.c`
- Create: `firmware/ports/esp32/main/drivers/drv_es_codec.h`, `drv_es_codec.c`, `drv_i2s_simplex.h`, `drv_i2s_simplex.c` (in `firmware/ports/esp32/main/drivers/`)
- Create: `firmware/ports/esp32/main/hal_audio.c`
- Modify: `firmware/ports/esp32/main/CMakeLists.txt`, `firmware/ports/esp32/host-tests/CMakeLists.txt`, `firmware/ports/esp32/boards/*/board.cmake`

**Interfaces:**
- Consumes: mic and speaker HAL (contract §2.5: `hal_mic_start(16000)` else `GADGET_ERR_UNSUPPORTED`, 20 ms `GADGET_EV_MIC_FRAME`s of `GADGET_MIC_FRAME_SAMPLES` = 320; `hal_spk_write` never blocks and returns samples accepted; `hal_spk_buffered_ms`; `hal_spk_stop` drops what is queued; codec boards accept 16000 only); `board_audio_t` (Task 3); board pins from `board.h` (Task 2).
- Produces: `pl_ring_t` + `pl_ring_init/write/read/count/clear`, `pl_samples_to_ms`, `pl_pcm_from_i32(in, out, n, shift)` (saturating), `pl_pcm_volume(pcm, n, pct)` (gain (pct/100)²); `esp_err_t drv_es_codec_init(const drv_es_codec_cfg_t*, board_audio_t*)` (16 kHz duplex, MIC1 via `mic_channel_mask`, `spk_latency_ms` = 90, `spk_set_mute` = `esp_codec_dev_set_out_mute`); `esp_err_t drv_i2s_simplex_init(const drv_i2s_simplex_cfg_t*, board_audio_t*)` (mic 16 kHz on its own port, speaker at `spk_rate`, no codec mute); `port_audio_start(const board_audio_t*)`; the seven mic/speaker HAL functions. `hal_spk_buffered_ms` = ring + the part of the last write still inside the DMA ring. `hal_spk_stop` empties the ring and, on the codec boards, mutes the codec at once (contract §2.5: "go silent now"); the speaker task then pushes one DMA ring plus one chunk of silence so the stale audio drains while muted, and unmutes. The devkit has no codec mute, so up to its `spk_latency_ms` (60 ms) still plays out (Contract deviations, item 1).

- [ ] **Step 1: Write the failing test**

`firmware/ports/esp32/host-tests/test_pl_audio.c`:

```c
/* SPDX-License-Identifier: Apache-2.0 */
#include "pl_audio.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

static void test_ring_wraps_and_reports_partial_writes(void) {
  int16_t store[5];
  pl_ring_t r;
  pl_ring_init(&r, store, 5);
  const int16_t a[4] = {1, 2, 3, 4};
  TEST_ASSERT_EQUAL_size_t(4, pl_ring_write(&r, a, 4));
  int16_t out[5] = {0};
  TEST_ASSERT_EQUAL_size_t(3, pl_ring_read(&r, out, 3));
  TEST_ASSERT_EQUAL_INT16(3, out[2]);
  const int16_t b[5] = {5, 6, 7, 8, 9};
  TEST_ASSERT_EQUAL_size_t(4, pl_ring_write(&r, b, 5)); /* room for 4 */
  TEST_ASSERT_EQUAL_size_t(5, pl_ring_count(&r));
  TEST_ASSERT_EQUAL_size_t(5, pl_ring_read(&r, out, 9));
  const int16_t want[5] = {4, 5, 6, 7, 8};
  TEST_ASSERT_EQUAL_INT16_ARRAY(want, out, 5);
  TEST_ASSERT_EQUAL_size_t(0, pl_ring_read(&r, out, 1));
}

static void test_ring_clear(void) {
  int16_t store[8];
  pl_ring_t r;
  pl_ring_init(&r, store, 8);
  const int16_t a[6] = {1, 2, 3, 4, 5, 6};
  pl_ring_write(&r, a, 6);
  pl_ring_clear(&r);
  TEST_ASSERT_EQUAL_size_t(0, pl_ring_count(&r));
  TEST_ASSERT_EQUAL_size_t(6, pl_ring_write(&r, a, 6));
}

static void test_samples_to_ms(void) {
  TEST_ASSERT_EQUAL_UINT32(20, pl_samples_to_ms(320, 16000));
  TEST_ASSERT_EQUAL_UINT32(1000, pl_samples_to_ms(24000, 24000));
  TEST_ASSERT_EQUAL_UINT32(0, pl_samples_to_ms(15, 16000));
  TEST_ASSERT_EQUAL_UINT32(0, pl_samples_to_ms(100, 0));
}

static void test_i32_to_i16_saturates(void) {
  const int32_t in[4] = {(int32_t)0x7fffff00, (int32_t)0x80000000, 1 << 14, -(1 << 15)};
  int16_t out[4];
  pl_pcm_from_i32(in, out, 4, 14);
  TEST_ASSERT_EQUAL_INT16(32767, out[0]);
  TEST_ASSERT_EQUAL_INT16(-32768, out[1]);
  TEST_ASSERT_EQUAL_INT16(1, out[2]);
  TEST_ASSERT_EQUAL_INT16(-2, out[3]);
}

static void test_volume_curve(void) {
  int16_t pcm[3] = {20000, -20000, 7};
  pl_pcm_volume(pcm, 3, 100);
  TEST_ASSERT_EQUAL_INT16(20000, pcm[0]);
  pl_pcm_volume(pcm, 3, 50);
  TEST_ASSERT_EQUAL_INT16(5000, pcm[0]);
  TEST_ASSERT_EQUAL_INT16(-5000, pcm[1]);
  pl_pcm_volume(pcm, 3, 0);
  TEST_ASSERT_EQUAL_INT16(0, pcm[0]);
  TEST_ASSERT_EQUAL_INT16(0, pcm[1]);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_ring_wraps_and_reports_partial_writes);
  RUN_TEST(test_ring_clear);
  RUN_TEST(test_samples_to_ms);
  RUN_TEST(test_i32_to_i16_saturates);
  RUN_TEST(test_volume_curve);
  return UNITY_END();
}
```

Append to `firmware/ports/esp32/host-tests/CMakeLists.txt`:

```cmake
port_test(test_pl_audio)
```

- [ ] **Step 2: Run it to verify it fails**

Run: `cmake -S firmware/ports/esp32/host-tests -B build/esp32-host && cmake --build build/esp32-host -j10 && ctest --test-dir build/esp32-host --output-on-failure`

Expected: FAIL — the build stops with `fatal error: 'pl_audio.h' file not found` (clang; GCC prints `pl_audio.h: No such file or directory`).

- [ ] **Step 3: Write the ring, conversion and volume helpers**

`firmware/ports/esp32/main/logic/pl_audio.h`:

```c
/* SPDX-License-Identifier: Apache-2.0 */
/* Audio helpers for the ESP32 port: the speaker ring buffer (not locked;
 * hal_audio.c holds a mutex around every call), sample conversion and
 * software volume. */
#ifndef PL_AUDIO_H
#define PL_AUDIO_H

#include "gadget_types.h"

typedef struct {
  int16_t *buf;
  size_t cap;
  size_t head;    /* next read index */
  size_t count;   /* samples stored */
} pl_ring_t;

void pl_ring_init(pl_ring_t *r, int16_t *storage, size_t cap);
/* Store as many of n samples as fit; returns how many were stored. */
size_t pl_ring_write(pl_ring_t *r, const int16_t *pcm, size_t n);
/* Take up to n samples; returns how many were taken. */
size_t pl_ring_read(pl_ring_t *r, int16_t *out, size_t n);
size_t pl_ring_count(const pl_ring_t *r);
void pl_ring_clear(pl_ring_t *r);

/* samples at rate Hz → whole milliseconds (rounded down). */
uint32_t pl_samples_to_ms(size_t samples, uint32_t rate);
/* 32-bit I2S samples (24-bit microphones put data in the top bits) to
 * PCM16: arithmetic shift right by `shift`, saturated. */
void pl_pcm_from_i32(const int32_t *in, int16_t *out, size_t n, unsigned shift);
/* Software volume, in place: gain = (pct/100)^2, so 50 % is -12 dB. */
void pl_pcm_volume(int16_t *pcm, size_t n, uint8_t pct);

#endif /* PL_AUDIO_H */
```

`firmware/ports/esp32/main/logic/pl_audio.c`:

```c
/* SPDX-License-Identifier: Apache-2.0 */
#include "pl_audio.h"

#include <string.h>

void pl_ring_init(pl_ring_t *r, int16_t *storage, size_t cap) {
  r->buf = storage;
  r->cap = cap;
  r->head = 0;
  r->count = 0;
}

size_t pl_ring_write(pl_ring_t *r, const int16_t *pcm, size_t n) {
  size_t room = r->cap - r->count;
  if (n > room) {
    n = room;
  }
  size_t tail = (r->head + r->count) % r->cap;
  size_t first = r->cap - tail < n ? r->cap - tail : n;
  memcpy(r->buf + tail, pcm, first * sizeof(int16_t));
  memcpy(r->buf, pcm + first, (n - first) * sizeof(int16_t));
  r->count += n;
  return n;
}

size_t pl_ring_read(pl_ring_t *r, int16_t *out, size_t n) {
  if (n > r->count) {
    n = r->count;
  }
  size_t first = r->cap - r->head < n ? r->cap - r->head : n;
  memcpy(out, r->buf + r->head, first * sizeof(int16_t));
  memcpy(out + first, r->buf, (n - first) * sizeof(int16_t));
  r->head = (r->head + n) % r->cap;
  r->count -= n;
  return n;
}

size_t pl_ring_count(const pl_ring_t *r) { return r->count; }

void pl_ring_clear(pl_ring_t *r) {
  r->head = 0;
  r->count = 0;
}

uint32_t pl_samples_to_ms(size_t samples, uint32_t rate) {
  if (rate == 0) {
    return 0;
  }
  return (uint32_t)(((uint64_t)samples * 1000u) / rate);
}

void pl_pcm_from_i32(const int32_t *in, int16_t *out, size_t n, unsigned shift) {
  for (size_t i = 0; i < n; i++) {
    int32_t v = in[i] >> shift;
    if (v > INT16_MAX) {
      v = INT16_MAX;
    } else if (v < INT16_MIN) {
      v = INT16_MIN;
    }
    out[i] = (int16_t)v;
  }
}

void pl_pcm_volume(int16_t *pcm, size_t n, uint8_t pct) {
  if (pct >= 100) {
    return;
  }
  int32_t gain = (int32_t)pct * (int32_t)pct * 32768 / 10000; /* Q15 */
  for (size_t i = 0; i < n; i++) {
    pcm[i] = (int16_t)(((int32_t)pcm[i] * gain) >> 15);
  }
}
```

In `firmware/ports/esp32/host-tests/CMakeLists.txt`, replace the `set(PORT_LOGIC_SRCS ...)` block with:

```cmake
set(PORT_LOGIC_SRCS
  ${CORE_DIR}/src/boards.c
  ${ESP_DIR}/main/logic/pl_event.c
  ${ESP_DIR}/main/logic/pl_util.c
  ${ESP_DIR}/main/logic/pl_scan.c
  ${ESP_DIR}/main/logic/pl_wifi.c
  ${ESP_DIR}/main/logic/pl_wsasm.c
  ${ESP_DIR}/main/logic/pl_mdns.c
  ${ESP_DIR}/main/logic/pl_audio.c
)
```

- [ ] **Step 4: Run the tests to verify they pass**

Run: `cmake -S firmware/ports/esp32/host-tests -B build/esp32-host && cmake --build build/esp32-host -j10 && ctest --test-dir build/esp32-host --output-on-failure`

Expected: `100% tests passed, 0 tests failed out of 9`.

- [ ] **Step 5: Write the audio drivers and the HAL**

`drv_es_codec` opens both codecs at 16 kHz, 16 bit, one channel, on one I2S port (the ES8311 and ES7210 share MCLK/BCLK/LRCK, and `esp_codec_dev` refuses two rates on one port). Only ES7210 MIC1 is selected; the MIC3 echo reference stays unused in v1 (A18).

`firmware/ports/esp32/main/drivers/drv_es_codec.h`:

```c
/* SPDX-License-Identifier: Apache-2.0 */
/* ES8311 (speaker DAC) + ES7210 (mic ADC) on one I2S port and one I2C bus
 * (amoled-175c, amoled-175, lcd-154), on espressif/esp_codec_dev. The two
 * codecs share MCLK/BCLK/LRCK, so both directions run at 16 kHz (A13). */
#ifndef DRV_ES_CODEC_H
#define DRV_ES_CODEC_H

#include <stdint.h>

#include "board_api.h"
#include "driver/i2c_master.h"

typedef struct {
  i2c_master_bus_handle_t bus;
  int i2c_port;
  int i2s_port;
  int mclk, bclk, ws, dout, din, pa;
  uint16_t mic_channel_mask;  /* 0x1 = MIC1 in the left slot; MIC3 (echo reference) is a v2 item */
  float mic_gain_db;
  uint8_t volume;             /* 0..100 at boot */
} drv_es_codec_cfg_t;

esp_err_t drv_es_codec_init(const drv_es_codec_cfg_t *cfg, board_audio_t *out);

#endif /* DRV_ES_CODEC_H */
```

`firmware/ports/esp32/main/drivers/drv_es_codec.c`:

```c
/* SPDX-License-Identifier: Apache-2.0 */
#include "drv_es_codec.h"

#include "driver/i2s_std.h"
#include "esp_check.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"

#define CODEC_RATE 16000u
#define DMA_DESC_NUM 6u
#define DMA_FRAME_NUM 240u

static const char *TAG = "es_codec";
static esp_codec_dev_handle_t s_spk;
static esp_codec_dev_handle_t s_mic;

static esp_err_t mic_read(int16_t *pcm, size_t samples) {
  int r = esp_codec_dev_read(s_mic, pcm, (int)(samples * sizeof(int16_t)));
  return r == ESP_CODEC_DEV_OK ? ESP_OK : ESP_FAIL;
}

static esp_err_t spk_write(const int16_t *pcm, size_t samples) {
  int r = esp_codec_dev_write(s_spk, (void *)pcm, (int)(samples * sizeof(int16_t)));
  return r == ESP_CODEC_DEV_OK ? ESP_OK : ESP_FAIL;
}

static void spk_volume(uint8_t pct) { esp_codec_dev_set_out_vol(s_spk, pct); }

static void spk_mute(bool mute) { esp_codec_dev_set_out_mute(s_spk, mute); }

esp_err_t drv_es_codec_init(const drv_es_codec_cfg_t *c, board_audio_t *out) {
  i2s_chan_handle_t tx = NULL, rx = NULL;
  i2s_chan_config_t chan = I2S_CHANNEL_DEFAULT_CONFIG(c->i2s_port, I2S_ROLE_MASTER);
  chan.dma_desc_num = DMA_DESC_NUM;
  chan.dma_frame_num = DMA_FRAME_NUM;
  chan.auto_clear = true; /* silence on underrun */
  ESP_RETURN_ON_ERROR(i2s_new_channel(&chan, &tx, &rx), TAG, "i2s channels");
  const i2s_std_config_t std = {
    .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(CODEC_RATE),
    .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
    .gpio_cfg = {.mclk = c->mclk, .bclk = c->bclk, .ws = c->ws, .dout = c->dout, .din = c->din},
  };
  ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(tx, &std), TAG, "tx std");
  ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(rx, &std), TAG, "rx std");
  ESP_RETURN_ON_ERROR(i2s_channel_enable(tx), TAG, "tx enable");
  ESP_RETURN_ON_ERROR(i2s_channel_enable(rx), TAG, "rx enable");

  audio_codec_i2s_cfg_t i2s_cfg = {.port = (uint8_t)c->i2s_port, .rx_handle = rx, .tx_handle = tx};
  const audio_codec_data_if_t *data_if = audio_codec_new_i2s_data(&i2s_cfg);
  audio_codec_i2c_cfg_t spk_i2c = {.port = (uint8_t)c->i2c_port, .addr = ES8311_CODEC_DEFAULT_ADDR, .bus_handle = c->bus};
  audio_codec_i2c_cfg_t mic_i2c = {.port = (uint8_t)c->i2c_port, .addr = ES7210_CODEC_DEFAULT_ADDR, .bus_handle = c->bus};
  const audio_codec_ctrl_if_t *spk_ctrl = audio_codec_new_i2c_ctrl(&spk_i2c);
  const audio_codec_ctrl_if_t *mic_ctrl = audio_codec_new_i2c_ctrl(&mic_i2c);
  const audio_codec_gpio_if_t *gpio_if = audio_codec_new_gpio();
  ESP_RETURN_ON_FALSE(data_if && spk_ctrl && mic_ctrl && gpio_if, ESP_FAIL, TAG, "codec interfaces");

  es8311_codec_cfg_t es8311 = {
    .ctrl_if = spk_ctrl,
    .gpio_if = gpio_if,
    .codec_mode = ESP_CODEC_DEV_WORK_MODE_DAC,
    .pa_pin = (int16_t)c->pa,
    .pa_reverted = false,
    .master_mode = false,
    .use_mclk = true,
    .hw_gain = {.pa_voltage = 5.0f, .codec_dac_voltage = 3.3f},
  };
  es7210_codec_cfg_t es7210 = {.ctrl_if = mic_ctrl, .master_mode = false, .mic_selected = ES7210_SEL_MIC1};
  const audio_codec_if_t *spk_if = es8311_codec_new(&es8311);
  const audio_codec_if_t *mic_if = es7210_codec_new(&es7210);
  ESP_RETURN_ON_FALSE(spk_if && mic_if, ESP_FAIL, TAG, "codecs (check the I2C addresses)");

  esp_codec_dev_cfg_t spk_dev = {.dev_type = ESP_CODEC_DEV_TYPE_OUT, .codec_if = spk_if, .data_if = data_if};
  esp_codec_dev_cfg_t mic_dev = {.dev_type = ESP_CODEC_DEV_TYPE_IN, .codec_if = mic_if, .data_if = data_if};
  s_spk = esp_codec_dev_new(&spk_dev);
  s_mic = esp_codec_dev_new(&mic_dev);
  ESP_RETURN_ON_FALSE(s_spk && s_mic, ESP_FAIL, TAG, "codec devices");

  esp_codec_dev_sample_info_t out_fs = {.bits_per_sample = 16, .channel = 1, .channel_mask = 0, .sample_rate = CODEC_RATE};
  esp_codec_dev_sample_info_t in_fs = {
    .bits_per_sample = 16, .channel = 1, .channel_mask = c->mic_channel_mask, .sample_rate = CODEC_RATE};
  ESP_RETURN_ON_FALSE(esp_codec_dev_open(s_spk, &out_fs) == ESP_CODEC_DEV_OK, ESP_FAIL, TAG, "open speaker");
  ESP_RETURN_ON_FALSE(esp_codec_dev_open(s_mic, &in_fs) == ESP_CODEC_DEV_OK, ESP_FAIL, TAG, "open mic");
  esp_codec_dev_set_in_gain(s_mic, c->mic_gain_db);
  esp_codec_dev_set_out_vol(s_spk, c->volume);

  *out = (board_audio_t){
    .mic_rate = CODEC_RATE,
    .spk_rate = CODEC_RATE,
    .spk_latency_ms = DMA_DESC_NUM * DMA_FRAME_NUM * 1000u / CODEC_RATE,
    .mic_read = mic_read,
    .spk_write = spk_write,
    .spk_set_volume = spk_volume,
    .spk_set_mute = spk_mute,
  };
  return ESP_OK;
}
```

`firmware/ports/esp32/main/drivers/drv_i2s_simplex.h`:

```c
/* SPDX-License-Identifier: Apache-2.0 */
/* devkit audio: an INMP441 I2S microphone and a MAX98357A I2S amplifier on
 * two separate I2S controllers, so the speaker may run at 24 kHz while the
 * mic runs at 16 kHz (spec §4.3 rate rule). */
#ifndef DRV_I2S_SIMPLEX_H
#define DRV_I2S_SIMPLEX_H

#include <stdint.h>

#include "board_api.h"

typedef struct {
  int mic_port, mic_bclk, mic_ws, mic_din;
  int spk_port, spk_bclk, spk_ws, spk_dout;
  uint32_t spk_rate;     /* 24000 */
  unsigned mic_shift;    /* 32-bit slot → PCM16: 16 is unity; 14 adds ~12 dB */
} drv_i2s_simplex_cfg_t;

esp_err_t drv_i2s_simplex_init(const drv_i2s_simplex_cfg_t *cfg, board_audio_t *out);

#endif /* DRV_I2S_SIMPLEX_H */
```

`firmware/ports/esp32/main/drivers/drv_i2s_simplex.c`:

```c
/* SPDX-License-Identifier: Apache-2.0 */
#include "drv_i2s_simplex.h"

#include "driver/i2s_std.h"
#include "esp_check.h"
#include "gadget_types.h"
#include "pl_audio.h"

#define MIC_RATE 16000u
#define DMA_DESC_NUM 6u
#define DMA_FRAME_NUM 240u
#define IO_TIMEOUT_MS 1000u

static const char *TAG = "i2s_simplex";
static i2s_chan_handle_t s_rx;
static i2s_chan_handle_t s_tx;
static unsigned s_shift;
static int32_t s_raw[GADGET_MIC_FRAME_SAMPLES];

static esp_err_t mic_read(int16_t *pcm, size_t samples) {
  if (samples > GADGET_MIC_FRAME_SAMPLES) {
    return ESP_ERR_INVALID_SIZE;
  }
  size_t got = 0;
  esp_err_t e = i2s_channel_read(s_rx, s_raw, samples * sizeof(int32_t), &got, IO_TIMEOUT_MS);
  if (e != ESP_OK || got != samples * sizeof(int32_t)) {
    return e != ESP_OK ? e : ESP_FAIL;
  }
  pl_pcm_from_i32(s_raw, pcm, samples, s_shift);
  return ESP_OK;
}

static esp_err_t spk_write(const int16_t *pcm, size_t samples) {
  size_t written = 0;
  return i2s_channel_write(s_tx, pcm, samples * sizeof(int16_t), &written, IO_TIMEOUT_MS);
}

esp_err_t drv_i2s_simplex_init(const drv_i2s_simplex_cfg_t *c, board_audio_t *out) {
  s_shift = c->mic_shift;
  i2s_chan_config_t rx_chan = I2S_CHANNEL_DEFAULT_CONFIG(c->mic_port, I2S_ROLE_MASTER);
  ESP_RETURN_ON_ERROR(i2s_new_channel(&rx_chan, NULL, &s_rx), TAG, "mic channel");
  i2s_std_config_t rx_cfg = {
    .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(MIC_RATE),
    .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_MONO),
    .gpio_cfg = {.mclk = I2S_GPIO_UNUSED, .bclk = c->mic_bclk, .ws = c->mic_ws, .dout = I2S_GPIO_UNUSED, .din = c->mic_din},
  };
  rx_cfg.slot_cfg.slot_mask = I2S_STD_SLOT_LEFT; /* INMP441 L/R pin tied to GND */
  ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(s_rx, &rx_cfg), TAG, "mic std");
  ESP_RETURN_ON_ERROR(i2s_channel_enable(s_rx), TAG, "mic enable");

  i2s_chan_config_t tx_chan = I2S_CHANNEL_DEFAULT_CONFIG(c->spk_port, I2S_ROLE_MASTER);
  tx_chan.dma_desc_num = DMA_DESC_NUM;
  tx_chan.dma_frame_num = DMA_FRAME_NUM;
  tx_chan.auto_clear = true;
  ESP_RETURN_ON_ERROR(i2s_new_channel(&tx_chan, &s_tx, NULL), TAG, "amp channel");
  const i2s_std_config_t tx_cfg = {
    .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(c->spk_rate),
    .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
    .gpio_cfg = {.mclk = I2S_GPIO_UNUSED, .bclk = c->spk_bclk, .ws = c->spk_ws, .dout = c->spk_dout, .din = I2S_GPIO_UNUSED},
  };
  ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(s_tx, &tx_cfg), TAG, "amp std");
  ESP_RETURN_ON_ERROR(i2s_channel_enable(s_tx), TAG, "amp enable");

  *out = (board_audio_t){
    .mic_rate = MIC_RATE,
    .spk_rate = c->spk_rate,
    .spk_latency_ms = DMA_DESC_NUM * DMA_FRAME_NUM * 1000u / c->spk_rate,
    .mic_read = mic_read,
    .spk_write = spk_write,
    .spk_set_volume = NULL, /* hal_audio.c scales in software */
    .spk_set_mute = NULL,   /* no codec: up to spk_latency_ms plays out after hal_spk_stop() */
  };
  return ESP_OK;
}
```

`firmware/ports/esp32/main/hal_audio.c`:

```c
/* SPDX-License-Identifier: Apache-2.0 */
/* HAL mic and speaker groups. The board's codec (or I2S parts) runs all the
 * time: the mic task reads 20 ms frames and forwards them only while core
 * is recording, so every frame is fresh; the speaker task drains a ring of
 * up to 1.5 s and writes silence when it is empty, so the shared I2S clock
 * of the codec boards never stops. hal_spk_stop() mutes the codec at once;
 * the speaker task drains the stale DMA ring with silence, then unmutes.
 * No echo cancellation in v1 (A18). */
#include <stdlib.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "gadget_hal.h"
#include "pl_audio.h"
#include "port.h"

#define SPK_RING_MS 1500u
#define SPK_CHUNK 320u
#define AUDIO_CORE 1

static const char *TAG = "audio";
static board_audio_t s_audio;
static bool s_have_audio;
static SemaphoreHandle_t s_lock;
static pl_ring_t s_ring;
static volatile bool s_mic_on;
static volatile bool s_spk_open;
static volatile uint8_t s_volume = 80;
static volatile uint32_t s_play_end_ms; /* when the last written audio leaves the DMA ring */
static volatile bool s_unmute_pending;  /* hal_spk_stop() muted the codec */
static uint32_t s_flush_samples;        /* one TX DMA ring plus one chunk */

static uint32_t now32(void) { return (uint32_t)(esp_timer_get_time() / 1000); }

/* After hal_spk_stop(): write silence until everything that was already in
 * the TX DMA ring has played (muted), then unmute. A stop that arrives while
 * this runs leaves s_unmute_pending set, so the next loop drains again. */
static void spk_drain_then_unmute(int16_t *chunk) {
  s_unmute_pending = false;
  memset(chunk, 0, SPK_CHUNK * sizeof(int16_t));
  for (uint32_t left = s_flush_samples; left > 0;) {
    size_t k = left < SPK_CHUNK ? left : SPK_CHUNK;
    s_audio.spk_write(chunk, k);
    left -= (uint32_t)k;
  }
  if (!s_unmute_pending) {
    s_audio.spk_set_mute(false);
  }
}

static void mic_task(void *arg) {
  (void)arg;
  static int16_t frame[GADGET_MIC_FRAME_SAMPLES];
  for (;;) {
    if (s_audio.mic_read(frame, GADGET_MIC_FRAME_SAMPLES) != ESP_OK) {
      vTaskDelay(pdMS_TO_TICKS(20));
      continue;
    }
    if (!s_mic_on) {
      continue; /* keep the RX DMA drained */
    }
    gadget_event_t ev = {.type = GADGET_EV_MIC_FRAME};
    ev.u.mic.pcm = frame;
    ev.u.mic.samples = GADGET_MIC_FRAME_SAMPLES;
    port_post_event(&ev);
  }
}

static void spk_task(void *arg) {
  (void)arg;
  static int16_t chunk[SPK_CHUNK];
  for (;;) {
    if (s_unmute_pending) {
      spk_drain_then_unmute(chunk);
      continue;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    size_t n = pl_ring_read(&s_ring, chunk, SPK_CHUNK);
    xSemaphoreGive(s_lock);
    if (n == 0) {
      memset(chunk, 0, sizeof(chunk));
      s_audio.spk_write(chunk, SPK_CHUNK);
      continue;
    }
    if (s_audio.spk_set_volume == NULL) {
      pl_pcm_volume(chunk, n, s_volume);
    }
    s_audio.spk_write(chunk, n);
    s_play_end_ms = now32() + s_audio.spk_latency_ms;
  }
}

esp_err_t port_audio_start(const board_audio_t *audio) {
  s_audio = *audio;
  size_t cap = (size_t)s_audio.spk_rate * SPK_RING_MS / 1000u;
  int16_t *store = heap_caps_malloc(cap * sizeof(int16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (store == NULL) {
    store = malloc(cap * sizeof(int16_t));
  }
  s_lock = xSemaphoreCreateMutex();
  if (store == NULL || s_lock == NULL) {
    return ESP_ERR_NO_MEM;
  }
  pl_ring_init(&s_ring, store, cap);
  s_flush_samples = s_audio.spk_rate * s_audio.spk_latency_ms / 1000u + SPK_CHUNK;
  if (s_audio.spk_set_volume != NULL) {
    s_audio.spk_set_volume(s_volume);
  }
  if (xTaskCreatePinnedToCore(mic_task, "mic", 4096, NULL, 7, NULL, AUDIO_CORE) != pdPASS ||
      xTaskCreatePinnedToCore(spk_task, "spk", 4096, NULL, 7, NULL, AUDIO_CORE) != pdPASS) {
    return ESP_ERR_NO_MEM;
  }
  s_have_audio = true;
  ESP_LOGI(TAG, "mic %u Hz, speaker %u Hz", (unsigned)s_audio.mic_rate, (unsigned)s_audio.spk_rate);
  return ESP_OK;
}

gadget_status_t hal_mic_start(uint32_t rate) {
  if (!s_have_audio || rate != GADGET_MIC_RATE || rate != s_audio.mic_rate) {
    return GADGET_ERR_UNSUPPORTED;
  }
  s_mic_on = true;
  return GADGET_OK;
}

void hal_mic_stop(void) { s_mic_on = false; }

gadget_status_t hal_spk_open(uint32_t rate) {
  if (!s_have_audio || rate != s_audio.spk_rate) {
    return GADGET_ERR_UNSUPPORTED;
  }
  s_spk_open = true;
  return GADGET_OK;
}

size_t hal_spk_write(const int16_t *pcm, size_t samples) {
  if (!s_spk_open || pcm == NULL) {
    return 0;
  }
  xSemaphoreTake(s_lock, portMAX_DELAY);
  size_t n = pl_ring_write(&s_ring, pcm, samples);
  xSemaphoreGive(s_lock);
  return n;
}

uint32_t hal_spk_buffered_ms(void) {
  if (!s_have_audio) {
    return 0;
  }
  xSemaphoreTake(s_lock, portMAX_DELAY);
  size_t queued = pl_ring_count(&s_ring);
  xSemaphoreGive(s_lock);
  uint32_t ms = pl_samples_to_ms(queued, s_audio.spk_rate);
  int32_t dma = (int32_t)(s_play_end_ms - now32());
  return ms + (dma > 0 ? (uint32_t)dma : 0u);
}

void hal_spk_stop(void) {
  if (!s_have_audio) {
    return;
  }
  if (s_audio.spk_set_mute != NULL) {
    s_audio.spk_set_mute(true); /* silent now, although the DMA ring still holds audio */
    s_unmute_pending = true;    /* the speaker task drains that ring, then unmutes */
  }
  xSemaphoreTake(s_lock, portMAX_DELAY);
  pl_ring_clear(&s_ring);
  xSemaphoreGive(s_lock);
  /* Without a codec mute (devkit) up to spk_latency_ms still plays out. */
  s_play_end_ms = now32();
}

void hal_spk_set_volume(uint8_t pct) {
  s_volume = pct > 100 ? 100 : pct;
  if (s_have_audio && s_audio.spk_set_volume != NULL) {
    s_audio.spk_set_volume(s_volume);
  }
}
```

In `firmware/ports/esp32/main/CMakeLists.txt`, replace the `set(port_srcs ...)` and `set(logic_srcs ...)` blocks with:

```cmake
set(port_srcs
  main.c
  port_events.c
  hal_system.c
  console_usj.c
  hal_storage.c
  hal_wifi.c
  hal_ws.c
  hal_mdns.c
  hal_audio.c)

set(logic_srcs
  logic/pl_event.c
  logic/pl_util.c
  logic/pl_scan.c
  logic/pl_wifi.c
  logic/pl_wsasm.c
  logic/pl_mdns.c
  logic/pl_audio.c)
```

and the `set(port_include_dirs ...)` line with:

```cmake
set(port_include_dirs "." "logic" "drivers" "${board_dir}")
```

And the board driver lists:

`firmware/ports/esp32/boards/amoled-175c/board.cmake`:

```cmake
# SPDX-License-Identifier: Apache-2.0
# Drivers from main/drivers/ that boards/amoled-175c/board.c uses.
set(BOARD_DRIVER_SRCS drv_es_codec.c)
```

`firmware/ports/esp32/boards/amoled-175/board.cmake`:

```cmake
# SPDX-License-Identifier: Apache-2.0
# Drivers from main/drivers/ that boards/amoled-175/board.c uses.
set(BOARD_DRIVER_SRCS drv_es_codec.c)
```

`firmware/ports/esp32/boards/lcd-154/board.cmake`:

```cmake
# SPDX-License-Identifier: Apache-2.0
# Drivers from main/drivers/ that boards/lcd-154/board.c uses.
set(BOARD_DRIVER_SRCS drv_es_codec.c)
```

`firmware/ports/esp32/boards/devkit/board.cmake`:

```cmake
# SPDX-License-Identifier: Apache-2.0
# Drivers from main/drivers/ that boards/devkit/board.c uses.
set(BOARD_DRIVER_SRCS drv_i2s_simplex.c)
```

- [ ] **Step 6: Build all four boards**

Run:

```bash
. ~/.espressif/tools/activate_idf_v6.0.3.sh
SKIP_ART_CHECK=1 firmware/ports/esp32/tools/build-all.sh
```

Expected, for each of `amoled-175`, `amoled-175c`, `devkit`, `lcd-154` (alphabetical): `== <board>` followed by `check-size: <board> ok, <n> of 6291456 bytes (<p>%)`; exit status 0.

- [ ] **Step 7: Commit**

```bash
git add firmware/ports/esp32/main \
  firmware/ports/esp32/boards \
  firmware/ports/esp32/host-tests
git commit -m "feat(esp32): mic and speaker HAL, ES8311/ES7210 and I2S drivers"
```
### Task 9: Input and battery — debouncing, touch edges, AXP2101, Li-ion ADC

**Files:**
- Create: `firmware/ports/esp32/main/logic/pl_input.h`, `pl_input.c`, `pl_power.h`, `pl_power.c` (in `firmware/ports/esp32/main/logic/`)
- Create: `firmware/ports/esp32/host-tests/test_pl_input.c`, `firmware/ports/esp32/host-tests/test_pl_power.c`
- Create: `firmware/ports/esp32/main/drivers/drv_axp2101.h`, `drv_axp2101.c`, `drv_lipo_adc.h`, `drv_lipo_adc.c` (in `firmware/ports/esp32/main/drivers/`)
- Create: `firmware/ports/esp32/main/hal_input.c`, `firmware/ports/esp32/main/hal_battery.c`
- Modify: `firmware/ports/esp32/main/CMakeLists.txt`, `firmware/ports/esp32/host-tests/CMakeLists.txt`, `firmware/ports/esp32/boards/{amoled-175c,amoled-175,lcd-154}/board.cmake`

**Interfaces:**
- Consumes: input events (contract §2.4: `GADGET_IN_TALK_DOWN/UP`, `CANCEL_DOWN/UP`, `TOUCH_DOWN/MOVE/UP` with screen coordinates; core derives holds, taps, swipes); `gadget_board_t.input_mask`, `screen_w/h`, `has_battery`; `hal_battery_read` (§2.5: cheap, core polls it); `board_talk_pressed`, `board_cancel_pressed`, `board_battery_read` (Task 3, implemented in Task 12); `esp_lcd_touch_read_data` + `esp_lcd_touch_get_data` (esp_lcd_touch 1.2.1; the older `get_coordinates` is deprecated); `port_display_touch` (Task 10).
- Produces: `pl_button_t` + `pl_button_init(b, raw_now)` / `int pl_button_poll(b, raw)` (+1 press, −1 release, 2 stable polls = 20 ms, a press held at boot is ignored until released); `pl_touch_t` + `pl_touch_init` / `int pl_touch_poll(t, pressed, x, y, w, h, out)` (move threshold 2 px, clamped); `bool pl_axp2101_decode(status1, status2, pct, out)`, `uint8_t pl_lipo_pct(mv)`, `bool pl_lipo_present(mv)` (false below `PL_LIPO_NO_CELL_MV` = 3000), register constants `PL_AXP2101_*`; `drv_axp2101_init(bus)` / `drv_axp2101_read(out)`; `drv_lipo_adc_init(cfg)` / `drv_lipo_adc_read(out)` (false when no cell is fitted, so `hal_battery_read()` reports no battery as `gadget_hal.h` requires); `port_input_init(board, touch)` / `port_input_poll()` (calls `core_event()` directly on the gadget task); `hal_battery_read()` (reads the board at most every 5 s).

- [ ] **Step 1: Write the failing tests**

`firmware/ports/esp32/host-tests/test_pl_input.c`:

```c
/* SPDX-License-Identifier: Apache-2.0 */
#include "pl_input.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

static void test_button_debounces_bounce(void) {
  pl_button_t b;
  pl_button_init(&b, false);
  TEST_ASSERT_EQUAL_INT(0, pl_button_poll(&b, true));  /* 1st pressed sample */
  TEST_ASSERT_EQUAL_INT(0, pl_button_poll(&b, false)); /* bounce */
  TEST_ASSERT_EQUAL_INT(0, pl_button_poll(&b, true));
  TEST_ASSERT_EQUAL_INT(1, pl_button_poll(&b, true));  /* stable for 2 polls */
  TEST_ASSERT_EQUAL_INT(0, pl_button_poll(&b, true));
  TEST_ASSERT_EQUAL_INT(0, pl_button_poll(&b, false));
  TEST_ASSERT_EQUAL_INT(-1, pl_button_poll(&b, false));
}

static void test_button_held_at_boot_is_ignored_until_released(void) {
  pl_button_t b;
  pl_button_init(&b, true);
  for (int i = 0; i < 300; i++) {
    TEST_ASSERT_EQUAL_INT(0, pl_button_poll(&b, true));
  }
  TEST_ASSERT_EQUAL_INT(0, pl_button_poll(&b, false));
  TEST_ASSERT_EQUAL_INT(0, pl_button_poll(&b, false)); /* armed, no edge */
  TEST_ASSERT_EQUAL_INT(0, pl_button_poll(&b, true));
  TEST_ASSERT_EQUAL_INT(1, pl_button_poll(&b, true));
}

static void test_touch_down_move_up(void) {
  pl_touch_t t;
  gadget_input_t ev;
  pl_touch_init(&t);
  TEST_ASSERT_EQUAL_INT(0, pl_touch_poll(&t, false, 0, 0, 466, 466, &ev));
  TEST_ASSERT_EQUAL_INT(1, pl_touch_poll(&t, true, 100, 200, 466, 466, &ev));
  TEST_ASSERT_EQUAL(GADGET_IN_TOUCH_DOWN, ev.type);
  TEST_ASSERT_EQUAL_INT16(200, ev.y);
  TEST_ASSERT_EQUAL_INT(0, pl_touch_poll(&t, true, 101, 200, 466, 466, &ev)); /* 1 px jitter */
  TEST_ASSERT_EQUAL_INT(1, pl_touch_poll(&t, true, 101, 230, 466, 466, &ev));
  TEST_ASSERT_EQUAL(GADGET_IN_TOUCH_MOVE, ev.type);
  TEST_ASSERT_EQUAL_INT(1, pl_touch_poll(&t, false, 0, 0, 466, 466, &ev));
  TEST_ASSERT_EQUAL(GADGET_IN_TOUCH_UP, ev.type);
  TEST_ASSERT_EQUAL_INT16(101, ev.x);
  TEST_ASSERT_EQUAL_INT16(230, ev.y);
}

static void test_touch_clamps_to_screen(void) {
  pl_touch_t t;
  gadget_input_t ev;
  pl_touch_init(&t);
  TEST_ASSERT_EQUAL_INT(1, pl_touch_poll(&t, true, 470, -3, 466, 466, &ev));
  TEST_ASSERT_EQUAL_INT16(465, ev.x);
  TEST_ASSERT_EQUAL_INT16(0, ev.y);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_button_debounces_bounce);
  RUN_TEST(test_button_held_at_boot_is_ignored_until_released);
  RUN_TEST(test_touch_down_move_up);
  RUN_TEST(test_touch_clamps_to_screen);
  return UNITY_END();
}
```

`firmware/ports/esp32/host-tests/test_pl_power.c`:

```c
/* SPDX-License-Identifier: Apache-2.0 */
#include "pl_power.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

static void test_axp2101_decode(void) {
  gadget_battery_t b;
  TEST_ASSERT_FALSE(pl_axp2101_decode(0x00, 0x00, 55, &b)); /* no battery */
  TEST_ASSERT_TRUE(pl_axp2101_decode(0x08, 0x20, 82, &b)); /* bits6:5 = 01 */
  TEST_ASSERT_EQUAL_UINT8(82, b.pct);
  TEST_ASSERT_TRUE(b.charging);
  TEST_ASSERT_TRUE(pl_axp2101_decode(0x28, 0x40, 140, &b)); /* discharging, bogus pct */
  TEST_ASSERT_EQUAL_UINT8(100, b.pct);
  TEST_ASSERT_FALSE(b.charging);
  TEST_ASSERT_TRUE(pl_axp2101_decode(0x08, 0x04, 100, &b)); /* standby, charge done */
  TEST_ASSERT_FALSE(b.charging);
}

static void test_lipo_curve(void) {
  TEST_ASSERT_EQUAL_UINT8(100, pl_lipo_pct(4250));
  TEST_ASSERT_EQUAL_UINT8(100, pl_lipo_pct(4180));
  TEST_ASSERT_EQUAL_UINT8(50, pl_lipo_pct(3830));
  TEST_ASSERT_EQUAL_UINT8(45, pl_lipo_pct(3810));
  TEST_ASSERT_EQUAL_UINT8(0, pl_lipo_pct(3300));
  TEST_ASSERT_EQUAL_UINT8(0, pl_lipo_pct(2900));
  uint8_t last = 0;
  for (uint32_t mv = 3300; mv <= 4180; mv += 10) { /* never decreases with voltage */
    uint8_t p = pl_lipo_pct(mv);
    TEST_ASSERT_TRUE(p >= last);
    last = p;
  }
}

/* lcd-154 without a cell: the divider sits near ground, so report no battery
 * (hal_battery_read() false) instead of a made-up 0 %. */
static void test_lipo_no_cell(void) {
  TEST_ASSERT_FALSE(pl_lipo_present(0));
  TEST_ASSERT_FALSE(pl_lipo_present(PL_LIPO_NO_CELL_MV - 1));
  TEST_ASSERT_TRUE(pl_lipo_present(PL_LIPO_NO_CELL_MV));
  TEST_ASSERT_TRUE(pl_lipo_present(3300));
  TEST_ASSERT_TRUE(pl_lipo_present(4200));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_axp2101_decode);
  RUN_TEST(test_lipo_curve);
  RUN_TEST(test_lipo_no_cell);
  return UNITY_END();
}
```

Append to `firmware/ports/esp32/host-tests/CMakeLists.txt`:

```cmake
port_test(test_pl_input)
port_test(test_pl_power)
```

- [ ] **Step 2: Run them to verify they fail**

Run: `cmake -S firmware/ports/esp32/host-tests -B build/esp32-host && cmake --build build/esp32-host -j10 && ctest --test-dir build/esp32-host --output-on-failure`

Expected: FAIL — the build stops with `fatal error: 'pl_input.h' file not found` (or the same for `pl_power.h`, whichever compiles first) (clang; GCC prints `pl_input.h: No such file or directory`).

- [ ] **Step 3: Write the input and power helpers**

The Li-ion curve is a generic light-load curve for one cell; the lcd-154 percentage is an estimate from voltage (spec §5.3).

`firmware/ports/esp32/main/logic/pl_input.h`:

```c
/* SPDX-License-Identifier: Apache-2.0 */
/* Button debouncing and touch edge detection. hal_input.c polls the board
 * every 10 ms and turns the results into GADGET_EV_INPUT events; core
 * derives holds, taps and swipes from them (gadget_hal.h input group). */
#ifndef PL_INPUT_H
#define PL_INPUT_H

#include "gadget_events.h"

#define PL_BUTTON_STABLE_POLLS 2u   /* 20 ms at the 10 ms poll */
#define PL_TOUCH_MOVE_MIN_PX 2

typedef struct {
  bool pressed;   /* debounced state */
  bool raw;       /* last raw sample */
  uint8_t same;   /* consecutive polls with this raw value */
  bool armed;     /* false while a press that began before boot is still held */
} pl_button_t;

/* raw_now: the level at boot. A button already held at boot (for example
 * the PWR press that switched the board on) reports nothing until it has
 * been released once. */
void pl_button_init(pl_button_t *b, bool raw_now);
/* Returns +1 on a debounced press, -1 on a debounced release, else 0. */
int pl_button_poll(pl_button_t *b, bool raw);

typedef struct {
  bool down;
  int16_t x, y;
} pl_touch_t;

void pl_touch_init(pl_touch_t *t);
/* One poll of the touch controller. Writes at most one event (TOUCH_DOWN,
 * TOUCH_MOVE when the point moved by >= PL_TOUCH_MOVE_MIN_PX, TOUCH_UP at
 * the last point) and returns 1, else returns 0. Coordinates are clamped
 * to the w x h screen. */
int pl_touch_poll(pl_touch_t *t, bool pressed, int32_t x, int32_t y, uint16_t w, uint16_t h, gadget_input_t *out);

#endif /* PL_INPUT_H */
```

`firmware/ports/esp32/main/logic/pl_input.c`:

```c
/* SPDX-License-Identifier: Apache-2.0 */
#include "pl_input.h"

#include <string.h>

void pl_button_init(pl_button_t *b, bool raw_now) {
  b->pressed = false;
  b->raw = raw_now;
  b->same = PL_BUTTON_STABLE_POLLS;
  b->armed = !raw_now;
}

int pl_button_poll(pl_button_t *b, bool raw) {
  if (raw == b->raw) {
    if (b->same < 255) {
      b->same++;
    }
  } else {
    b->raw = raw;
    b->same = 1;
  }
  if (b->same < PL_BUTTON_STABLE_POLLS) {
    return 0;
  }
  if (!b->armed) {
    if (!raw) {
      b->armed = true;
    }
    return 0;
  }
  if (raw != b->pressed) {
    b->pressed = raw;
    return raw ? 1 : -1;
  }
  return 0;
}

void pl_touch_init(pl_touch_t *t) { memset(t, 0, sizeof(*t)); }

static int16_t clamp(int32_t v, uint16_t size) {
  if (v < 0) {
    return 0;
  }
  if (size > 0 && v > (int32_t)size - 1) {
    return (int16_t)(size - 1);
  }
  return (int16_t)v;
}

int pl_touch_poll(pl_touch_t *t, bool pressed, int32_t x, int32_t y, uint16_t w, uint16_t h, gadget_input_t *out) {
  memset(out, 0, sizeof(*out));
  if (!pressed) {
    if (!t->down) {
      return 0;
    }
    t->down = false;
    out->type = GADGET_IN_TOUCH_UP;
    out->x = t->x;
    out->y = t->y;
    return 1;
  }
  int16_t cx = clamp(x, w);
  int16_t cy = clamp(y, h);
  if (!t->down) {
    t->down = true;
    t->x = cx;
    t->y = cy;
    out->type = GADGET_IN_TOUCH_DOWN;
    out->x = cx;
    out->y = cy;
    return 1;
  }
  int dx = cx > t->x ? cx - t->x : t->x - cx;
  int dy = cy > t->y ? cy - t->y : t->y - cy;
  if (dx + dy < PL_TOUCH_MOVE_MIN_PX) {
    return 0;
  }
  t->x = cx;
  t->y = cy;
  out->type = GADGET_IN_TOUCH_MOVE;
  out->x = cx;
  out->y = cy;
  return 1;
}
```

`firmware/ports/esp32/main/logic/pl_power.h`:

```c
/* SPDX-License-Identifier: Apache-2.0 */
/* Battery decoding: the AXP2101 PMU (amoled-175c, amoled-175) and a
 * voltage-based estimate for a bare 1-cell Li-ion (lcd-154). */
#ifndef PL_POWER_H
#define PL_POWER_H

#include "gadget_types.h"

#define PL_AXP2101_ADDR 0x34u
#define PL_AXP2101_REG_STATUS1 0x00u   /* bit3: battery present */
#define PL_AXP2101_REG_STATUS2 0x01u   /* bits6:5: 01 charging, 10 discharging, 00 standby */
#define PL_AXP2101_REG_GAUGE_CTRL 0x18u /* bit3: fuel gauge enable */
#define PL_AXP2101_REG_BATT_PCT 0xA4u  /* 0..100 */

/* false when no battery is fitted. */
bool pl_axp2101_decode(uint8_t status1, uint8_t status2, uint8_t pct, gadget_battery_t *out);
/* Resting cell voltage in millivolts → 0..100, piecewise linear. */
uint8_t pl_lipo_pct(uint32_t mv);
/* Below this the BAT net reads as "no cell fitted" (a real Li-ion cell
 * protects itself near 2.5–3.0 V, an empty holder reads near 0 V). */
#define PL_LIPO_NO_CELL_MV 3000u
bool pl_lipo_present(uint32_t mv);

#endif /* PL_POWER_H */
```

`firmware/ports/esp32/main/logic/pl_power.c`:

```c
/* SPDX-License-Identifier: Apache-2.0 */
#include "pl_power.h"

bool pl_axp2101_decode(uint8_t status1, uint8_t status2, uint8_t pct, gadget_battery_t *out) {
  if ((status1 & 0x08u) == 0) {
    return false;
  }
  out->pct = pct > 100 ? 100 : pct;
  out->charging = ((status2 >> 5) & 0x03u) == 0x01u;
  return true;
}

/* A typical 1-cell Li-ion discharge curve at light load. */
static const struct {
  uint16_t mv;
  uint8_t pct;
} k_curve[] = {
  {4180, 100}, {4100, 90}, {4020, 80}, {3950, 70}, {3880, 60}, {3830, 50},
  {3790, 40}, {3760, 30}, {3730, 20}, {3690, 10}, {3600, 5}, {3300, 0},
};

uint8_t pl_lipo_pct(uint32_t mv) {
  const size_t n = sizeof(k_curve) / sizeof(k_curve[0]);
  if (mv >= k_curve[0].mv) {
    return 100;
  }
  if (mv <= k_curve[n - 1].mv) {
    return 0;
  }
  for (size_t i = 1; i < n; i++) {
    if (mv >= k_curve[i].mv) {
      uint32_t hi_mv = k_curve[i - 1].mv, lo_mv = k_curve[i].mv;
      uint32_t hi_p = k_curve[i - 1].pct, lo_p = k_curve[i].pct;
      return (uint8_t)(lo_p + (mv - lo_mv) * (hi_p - lo_p) / (hi_mv - lo_mv));
    }
  }
  return 0;
}

bool pl_lipo_present(uint32_t mv) { return mv >= PL_LIPO_NO_CELL_MV; }
```

In `firmware/ports/esp32/host-tests/CMakeLists.txt`, replace the `set(PORT_LOGIC_SRCS ...)` block with:

```cmake
set(PORT_LOGIC_SRCS
  ${CORE_DIR}/src/boards.c
  ${ESP_DIR}/main/logic/pl_event.c
  ${ESP_DIR}/main/logic/pl_util.c
  ${ESP_DIR}/main/logic/pl_scan.c
  ${ESP_DIR}/main/logic/pl_wifi.c
  ${ESP_DIR}/main/logic/pl_wsasm.c
  ${ESP_DIR}/main/logic/pl_mdns.c
  ${ESP_DIR}/main/logic/pl_audio.c
  ${ESP_DIR}/main/logic/pl_input.c
  ${ESP_DIR}/main/logic/pl_power.c
)
```

- [ ] **Step 4: Run the tests to verify they pass**

Run: `cmake -S firmware/ports/esp32/host-tests -B build/esp32-host && cmake --build build/esp32-host -j10 && ctest --test-dir build/esp32-host --output-on-failure`

Expected: `100% tests passed, 0 tests failed out of 11`.

- [ ] **Step 5: Write the battery drivers and the input and battery HAL**

The AXP2101 registers come from the X-Powers AXP2101 datasheet: 0x00 bit 3 battery present, 0x01 bits 6:5 current direction (01 = charging), 0x18 bit 3 fuel-gauge enable, 0xA4 battery percent.

`firmware/ports/esp32/main/drivers/drv_axp2101.h`:

```c
/* SPDX-License-Identifier: Apache-2.0 */
/* AXP2101 PMU fuel gauge (amoled-175c, amoled-175), registers from the
 * X-Powers AXP2101 datasheet. Only reads battery state; power rails keep
 * their power-on defaults. */
#ifndef DRV_AXP2101_H
#define DRV_AXP2101_H

#include <stdbool.h>

#include "driver/i2c_master.h"
#include "esp_err.h"
#include "gadget_types.h"

/* Adds the device at 0x34 and makes sure the fuel gauge is on. */
esp_err_t drv_axp2101_init(i2c_master_bus_handle_t bus);
/* false when no battery is fitted or the read failed. */
bool drv_axp2101_read(gadget_battery_t *out);

#endif /* DRV_AXP2101_H */
```

`firmware/ports/esp32/main/drivers/drv_axp2101.c`:

```c
/* SPDX-License-Identifier: Apache-2.0 */
#include "drv_axp2101.h"

#include "esp_check.h"
#include "pl_power.h"

#define I2C_TIMEOUT_MS 50

static const char *TAG = "axp2101";
static i2c_master_dev_handle_t s_dev;

static esp_err_t read_reg(uint8_t reg, uint8_t *val) {
  return i2c_master_transmit_receive(s_dev, &reg, 1, val, 1, I2C_TIMEOUT_MS);
}

static esp_err_t write_reg(uint8_t reg, uint8_t val) {
  const uint8_t buf[2] = {reg, val};
  return i2c_master_transmit(s_dev, buf, sizeof(buf), I2C_TIMEOUT_MS);
}

esp_err_t drv_axp2101_init(i2c_master_bus_handle_t bus) {
  const i2c_device_config_t dev = {
    .dev_addr_length = I2C_ADDR_BIT_LEN_7,
    .device_address = PL_AXP2101_ADDR,
    .scl_speed_hz = 400000,
  };
  ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(bus, &dev, &s_dev), TAG, "add device");
  uint8_t ctrl = 0;
  ESP_RETURN_ON_ERROR(read_reg(PL_AXP2101_REG_GAUGE_CTRL, &ctrl), TAG, "read gauge control");
  if ((ctrl & 0x08u) == 0) {
    ESP_RETURN_ON_ERROR(write_reg(PL_AXP2101_REG_GAUGE_CTRL, (uint8_t)(ctrl | 0x08u)), TAG, "enable gauge");
  }
  return ESP_OK;
}

bool drv_axp2101_read(gadget_battery_t *out) {
  uint8_t s1 = 0, s2 = 0, pct = 0;
  if (s_dev == NULL || read_reg(PL_AXP2101_REG_STATUS1, &s1) != ESP_OK ||
      read_reg(PL_AXP2101_REG_STATUS2, &s2) != ESP_OK || read_reg(PL_AXP2101_REG_BATT_PCT, &pct) != ESP_OK) {
    return false;
  }
  return pl_axp2101_decode(s1, s2, pct, out);
}
```

`firmware/ports/esp32/main/drivers/drv_lipo_adc.h`:

```c
/* SPDX-License-Identifier: Apache-2.0 */
/* Battery voltage through a resistor divider on an ADC1 pin plus a charger
 * status pin (lcd-154: BAT_ADC GPIO1 behind x3, CHG_STAT GPIO3 low while
 * charging). The percentage comes from the voltage (pl_lipo_pct). */
#ifndef DRV_LIPO_ADC_H
#define DRV_LIPO_ADC_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "gadget_types.h"

typedef struct {
  int adc_gpio;
  int chg_gpio;            /* reads low while charging */
  uint32_t divider_x1000;  /* 3000 = x3 */
} drv_lipo_adc_cfg_t;

esp_err_t drv_lipo_adc_init(const drv_lipo_adc_cfg_t *cfg);
bool drv_lipo_adc_read(gadget_battery_t *out);

#endif /* DRV_LIPO_ADC_H */
```

`firmware/ports/esp32/main/drivers/drv_lipo_adc.c`:

```c
/* SPDX-License-Identifier: Apache-2.0 */
#include "drv_lipo_adc.h"

#include "driver/gpio.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_check.h"
#include "pl_power.h"

#define SAMPLES 8

static const char *TAG = "lipo_adc";
static adc_oneshot_unit_handle_t s_unit;
static adc_cali_handle_t s_cali;
static adc_channel_t s_chan;
static drv_lipo_adc_cfg_t s_cfg;

esp_err_t drv_lipo_adc_init(const drv_lipo_adc_cfg_t *cfg) {
  s_cfg = *cfg;
  adc_unit_t unit;
  ESP_RETURN_ON_ERROR(adc_oneshot_io_to_channel(cfg->adc_gpio, &unit, &s_chan), TAG, "adc pin");
  const adc_oneshot_unit_init_cfg_t unit_cfg = {.unit_id = unit};
  ESP_RETURN_ON_ERROR(adc_oneshot_new_unit(&unit_cfg, &s_unit), TAG, "adc unit");
  const adc_oneshot_chan_cfg_t chan_cfg = {.atten = ADC_ATTEN_DB_12, .bitwidth = ADC_BITWIDTH_DEFAULT};
  ESP_RETURN_ON_ERROR(adc_oneshot_config_channel(s_unit, s_chan, &chan_cfg), TAG, "adc channel");
  const adc_cali_curve_fitting_config_t cali = {
    .unit_id = unit, .chan = s_chan, .atten = ADC_ATTEN_DB_12, .bitwidth = ADC_BITWIDTH_DEFAULT};
  ESP_RETURN_ON_ERROR(adc_cali_create_scheme_curve_fitting(&cali, &s_cali), TAG, "adc calibration");
  const gpio_config_t chg = {
    .pin_bit_mask = 1ULL << cfg->chg_gpio,
    .mode = GPIO_MODE_INPUT,
    .pull_up_en = GPIO_PULLUP_ENABLE,
  };
  return gpio_config(&chg);
}

bool drv_lipo_adc_read(gadget_battery_t *out) {
  if (s_unit == NULL) {
    return false;
  }
  int sum = 0;
  for (int i = 0; i < SAMPLES; i++) {
    int raw = 0, mv = 0;
    if (adc_oneshot_read(s_unit, s_chan, &raw) != ESP_OK || adc_cali_raw_to_voltage(s_cali, raw, &mv) != ESP_OK) {
      return false;
    }
    sum += mv;
  }
  uint32_t cell_mv = (uint32_t)(sum / SAMPLES) * s_cfg.divider_x1000 / 1000u;
  if (!pl_lipo_present(cell_mv)) {
    return false; /* no cell: the divider sits at ground (hardware checklist B8a) */
  }
  out->pct = pl_lipo_pct(cell_mv);
  out->charging = gpio_get_level(s_cfg.chg_gpio) == 0;
  return true;
}
```

`firmware/ports/esp32/main/hal_input.c`:

```c
/* SPDX-License-Identifier: Apache-2.0 */
/* HAL input group: the gadget task polls buttons and touch every 10 ms and
 * hands raw edges to core (core derives holds, taps and swipes). The same
 * touch point drives LVGL's pointer through display.c. */
#include "esp_lcd_touch.h"
#include "gadget_core.h"
#include "pl_input.h"
#include "port.h"

static const gadget_board_t *s_board;
static esp_lcd_touch_handle_t s_touch;
static pl_button_t s_talk;
static pl_button_t s_cancel;
static pl_touch_t s_tp;

void port_input_init(const gadget_board_t *board, esp_lcd_touch_handle_t touch) {
  s_board = board;
  s_touch = touch;
  pl_button_init(&s_talk, board_talk_pressed());
  pl_button_init(&s_cancel, board_cancel_pressed());
  pl_touch_init(&s_tp);
}

static void send_input(const gadget_input_t *in) {
  gadget_event_t ev = {.type = GADGET_EV_INPUT};
  ev.u.input = *in;
  core_event(&ev);
}

static void button(pl_button_t *b, bool raw, gadget_input_type_t down, gadget_input_type_t up) {
  int edge = pl_button_poll(b, raw);
  if (edge != 0) {
    gadget_input_t in = {.type = edge > 0 ? down : up};
    send_input(&in);
  }
}

void port_input_poll(void) {
  if (s_board == NULL) {
    return;
  }
  if (s_board->input_mask & GADGET_INPUT_TALK) {
    button(&s_talk, board_talk_pressed(), GADGET_IN_TALK_DOWN, GADGET_IN_TALK_UP);
  }
  if (s_board->input_mask & GADGET_INPUT_CANCEL) {
    button(&s_cancel, board_cancel_pressed(), GADGET_IN_CANCEL_DOWN, GADGET_IN_CANCEL_UP);
  }
  if (s_touch == NULL) {
    return;
  }
  bool pressed = false;
  int32_t x = 0, y = 0;
  if (esp_lcd_touch_read_data(s_touch) == ESP_OK) {
    esp_lcd_touch_point_data_t pt[1];
    uint8_t count = 0;
    if (esp_lcd_touch_get_data(s_touch, pt, &count, 1) == ESP_OK && count > 0) {
      pressed = true;
      x = pt[0].x;
      y = pt[0].y;
    }
  }
  gadget_input_t in;
  if (pl_touch_poll(&s_tp, pressed, x, y, s_board->screen_w, s_board->screen_h, &in)) {
    send_input(&in);
  }
  port_display_touch(s_tp.down, s_tp.x, s_tp.y);
}
```

`firmware/ports/esp32/main/hal_battery.c`:

```c
/* SPDX-License-Identifier: Apache-2.0 */
/* HAL battery group. Core may poll every tick; the board is read at most
 * every 5 s (an I2C or ADC read) and the last reading is returned between. */
#include "gadget_hal.h"
#include "port.h"

#define BATTERY_READ_EVERY_MS 5000u

static uint64_t s_next_ms;
static bool s_valid;
static gadget_battery_t s_last;

bool hal_battery_read(gadget_battery_t *out) {
  const gadget_board_t *b = port_board();
  if (b == NULL || !b->has_battery || out == NULL) {
    return false;
  }
  uint64_t now = hal_now_ms();
  if (s_next_ms == 0 || now >= s_next_ms) {
    s_valid = board_battery_read(&s_last);
    s_next_ms = now + BATTERY_READ_EVERY_MS;
  }
  if (s_valid) {
    *out = s_last;
  }
  return s_valid;
}
```

In `firmware/ports/esp32/main/CMakeLists.txt`, replace the `set(port_srcs ...)` and `set(logic_srcs ...)` blocks with:

```cmake
set(port_srcs
  main.c
  port_events.c
  hal_system.c
  console_usj.c
  hal_storage.c
  hal_wifi.c
  hal_ws.c
  hal_mdns.c
  hal_audio.c
  hal_input.c
  hal_battery.c)

set(logic_srcs
  logic/pl_event.c
  logic/pl_util.c
  logic/pl_scan.c
  logic/pl_wifi.c
  logic/pl_wsasm.c
  logic/pl_mdns.c
  logic/pl_audio.c
  logic/pl_input.c
  logic/pl_power.c)
```

And the board driver lists (devkit has no battery and keeps `drv_i2s_simplex.c`):

`firmware/ports/esp32/boards/amoled-175c/board.cmake`:

```cmake
# SPDX-License-Identifier: Apache-2.0
# Drivers from main/drivers/ that boards/amoled-175c/board.c uses.
set(BOARD_DRIVER_SRCS drv_axp2101.c drv_es_codec.c)
```

`firmware/ports/esp32/boards/amoled-175/board.cmake`:

```cmake
# SPDX-License-Identifier: Apache-2.0
# Drivers from main/drivers/ that boards/amoled-175/board.c uses.
set(BOARD_DRIVER_SRCS drv_axp2101.c drv_es_codec.c)
```

`firmware/ports/esp32/boards/lcd-154/board.cmake`:

```cmake
# SPDX-License-Identifier: Apache-2.0
# Drivers from main/drivers/ that boards/lcd-154/board.c uses.
set(BOARD_DRIVER_SRCS drv_es_codec.c drv_lipo_adc.c)
```

- [ ] **Step 6: Build all four boards**

Run:

```bash
. ~/.espressif/tools/activate_idf_v6.0.3.sh
SKIP_ART_CHECK=1 firmware/ports/esp32/tools/build-all.sh
```

Expected, for each of `amoled-175`, `amoled-175c`, `devkit`, `lcd-154` (alphabetical): `== <board>` followed by `check-size: <board> ok, <n> of 6291456 bytes (<p>%)`; exit status 0.

- [ ] **Step 7: Commit**

```bash
git add firmware/ports/esp32/main \
  firmware/ports/esp32/boards \
  firmware/ports/esp32/host-tests
git commit -m "feat(esp32): input polling, AXP2101 and Li-ion battery HAL"
```
### Task 10: Display — panel and touch drivers, LVGL glue

**Files:**
- Create: `firmware/ports/esp32/main/logic/pl_display.h`, `firmware/ports/esp32/main/logic/pl_display.c`
- Create: `firmware/ports/esp32/host-tests/test_pl_display.c`
- Create: `firmware/ports/esp32/main/drivers/drv_co5300.h`, `drv_co5300.c`, `drv_cst9217.h`, `drv_cst9217.c`, `drv_st7789.h`, `drv_st7789.c` (in `firmware/ports/esp32/main/drivers/`)
- Create: `firmware/ports/esp32/main/display.c`
- Modify: `firmware/ports/esp32/main/CMakeLists.txt`, `firmware/ports/esp32/host-tests/CMakeLists.txt`, `firmware/ports/esp32/boards/*/board.cmake`

**Interfaces:**
- Consumes: `ui_init()` precondition (contract §2.9: the port has called `lv_init()` and created the display and, for touch, the pointer indev; `ui_tick()` calls `lv_timer_handler()` exactly once); `BOARD_LCD_BUF_LINES`, `BOARD_LCD_EVEN_AREAS` from `board.h`; esp_lcd panel IO callbacks (`esp_lcd_panel_io_register_event_callbacks`, `on_color_trans_done`); LVGL 9.6 display API.
- Produces: `void pl_round_even_area(&x1, &y1, &x2, &y2, w, h)`; `drv_co5300_init(cfg, &io, &panel)` (`cfg.init_cmds`/`init_cmds_size` carry the board's register table from `board.h`; `NULL` selects the component's default) + `drv_co5300_set_brightness(panel, pct)`; `drv_cst9217_init(cfg, &tp)`; `drv_st7789_init(cfg, &io, &panel)` + `drv_st7789_set_brightness(pct)`; `port_display_init(board, io, panel, has_touch)` (two internal DMA buffers of `screen_w × BOARD_LCD_BUF_LINES` pixels, partial rendering, RGB565 byte-swapped in `flush_cb` for the panels, the CO5300 rounder on `LV_EVENT_INVALIDATE_AREA`, one pointer indev); `port_display_touch(pressed, x, y)` (the state LVGL's pointer reads; fed by Task 9's `port_input_poll`).

- [ ] **Step 1: Write the failing test**

`firmware/ports/esp32/host-tests/test_pl_display.c`:

```c
/* SPDX-License-Identifier: Apache-2.0 */
#include "pl_display.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

static void test_round_even_area(void) {
  int32_t x1 = 3, y1 = 4, x2 = 10, y2 = 465;
  pl_round_even_area(&x1, &y1, &x2, &y2, 466, 466);
  TEST_ASSERT_EQUAL_INT32(2, x1);
  TEST_ASSERT_EQUAL_INT32(4, y1);
  TEST_ASSERT_EQUAL_INT32(11, x2);
  TEST_ASSERT_EQUAL_INT32(465, y2);
  x1 = 0; y1 = 0; x2 = 0; y2 = 0;
  pl_round_even_area(&x1, &y1, &x2, &y2, 466, 466);
  TEST_ASSERT_EQUAL_INT32(1, x2);
  TEST_ASSERT_EQUAL_INT32(1, y2);
}

static void test_area_already_aligned_is_unchanged(void) {
  int32_t x1 = 10, y1 = 20, x2 = 101, y2 = 41;
  pl_round_even_area(&x1, &y1, &x2, &y2, 240, 240);
  TEST_ASSERT_EQUAL_INT32(10, x1);
  TEST_ASSERT_EQUAL_INT32(20, y1);
  TEST_ASSERT_EQUAL_INT32(101, x2);
  TEST_ASSERT_EQUAL_INT32(41, y2);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_round_even_area);
  RUN_TEST(test_area_already_aligned_is_unchanged);
  return UNITY_END();
}
```

Append to `firmware/ports/esp32/host-tests/CMakeLists.txt`:

```cmake
port_test(test_pl_display)
```

- [ ] **Step 2: Run it to verify it fails**

Run: `cmake -S firmware/ports/esp32/host-tests -B build/esp32-host && cmake --build build/esp32-host -j10 && ctest --test-dir build/esp32-host --output-on-failure`

Expected: FAIL — the build stops with `fatal error: 'pl_display.h' file not found` (clang; GCC prints `pl_display.h: No such file or directory`).

- [ ] **Step 3: Write the area rounding**

`firmware/ports/esp32/main/logic/pl_display.h`:

```c
/* SPDX-License-Identifier: Apache-2.0 */
/* Display helpers that do not need LVGL or ESP-IDF. */
#ifndef PL_DISPLAY_H
#define PL_DISPLAY_H

#include <stdint.h>

/* CO5300 panels accept only areas that start on an even column/row and end
 * on an odd one. Widens the inclusive area [x1..x2] x [y1..y2] to that rule
 * and keeps it inside a w x h screen (w and h even). */
void pl_round_even_area(int32_t *x1, int32_t *y1, int32_t *x2, int32_t *y2, int32_t w, int32_t h);

#endif /* PL_DISPLAY_H */
```

`firmware/ports/esp32/main/logic/pl_display.c`:

```c
/* SPDX-License-Identifier: Apache-2.0 */
#include "pl_display.h"

void pl_round_even_area(int32_t *x1, int32_t *y1, int32_t *x2, int32_t *y2, int32_t w, int32_t h) {
  *x1 &= ~(int32_t)1;
  *y1 &= ~(int32_t)1;
  *x2 |= 1;
  *y2 |= 1;
  if (*x2 > w - 1) {
    *x2 = w - 1;
  }
  if (*y2 > h - 1) {
    *y2 = h - 1;
  }
}
```

In `firmware/ports/esp32/host-tests/CMakeLists.txt`, replace the `set(PORT_LOGIC_SRCS ...)` block with:

```cmake
set(PORT_LOGIC_SRCS
  ${CORE_DIR}/src/boards.c
  ${ESP_DIR}/main/logic/pl_event.c
  ${ESP_DIR}/main/logic/pl_util.c
  ${ESP_DIR}/main/logic/pl_scan.c
  ${ESP_DIR}/main/logic/pl_wifi.c
  ${ESP_DIR}/main/logic/pl_wsasm.c
  ${ESP_DIR}/main/logic/pl_mdns.c
  ${ESP_DIR}/main/logic/pl_audio.c
  ${ESP_DIR}/main/logic/pl_input.c
  ${ESP_DIR}/main/logic/pl_power.c
  ${ESP_DIR}/main/logic/pl_display.c
)
```

- [ ] **Step 4: Run the tests to verify they pass**

Run: `cmake -S firmware/ports/esp32/host-tests -B build/esp32-host && cmake --build build/esp32-host -j10 && ctest --test-dir build/esp32-host --output-on-failure`

Expected: `100% tests passed, 0 tests failed out of 12`.

- [ ] **Step 5: Write the panel and touch drivers and the LVGL glue**

The CO5300 register writes are the panel bring-up Waveshare publishes for these boards (vendor reference, spec §5.3). They live in each board's `board.h` as `BOARD_CO5300_INIT_CMDS` (spec §5.3: pins and init sequences from vendor sources live in the board's `board.h`), and `board.c` hands them to the shared driver through `drv_co5300_cfg_t`, so a future CO5300 board with another panel or gap changes only its own directory. A `NULL` table selects the component's built-in one; if a panel stays dark on hardware, check 2 of `docs/hardware-checklist.md` says how to try it. `lv_draw_rgb565_swap` is LVGL 9.6's name; the 9.5 fallback (spec §5.1) uses `lv_draw_sw_rgb565_swap`, selected by the version check in `display.c`.

`firmware/ports/esp32/main/drivers/drv_co5300.h`:

```c
/* SPDX-License-Identifier: Apache-2.0 */
/* CO5300 AMOLED panel on QSPI (amoled-175c, amoled-175), on top of the
 * espressif/esp_lcd_co5300 driver component. The register init table is the
 * board's (BOARD_CO5300_INIT_CMDS in boards/<id>/board.h). */
#ifndef DRV_CO5300_H
#define DRV_CO5300_H

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "esp_lcd_co5300.h"
#include "esp_lcd_types.h"

typedef struct {
  int sclk, d0, d1, d2, d3, cs, rst;
  int x_gap;                   /* first visible column (6 on the 1.75 panels) */
  size_t max_transfer_bytes;   /* one LVGL draw buffer */
  const co5300_lcd_init_cmd_t *init_cmds; /* the board's table; NULL = the component's default */
  uint16_t init_cmds_size;     /* entries in init_cmds (0 with NULL) */
} drv_co5300_cfg_t;

esp_err_t drv_co5300_init(const drv_co5300_cfg_t *cfg, esp_lcd_panel_io_handle_t *io, esp_lcd_panel_handle_t *panel);
void drv_co5300_set_brightness(esp_lcd_panel_handle_t panel, uint8_t pct);

#endif /* DRV_CO5300_H */
```

`firmware/ports/esp32/main/drivers/drv_co5300.c`:

```c
/* SPDX-License-Identifier: Apache-2.0 */
#include "drv_co5300.h"

#include "driver/spi_common.h"
#include "esp_check.h"
#include "esp_lcd_co5300.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"

static const char *TAG = "co5300";

esp_err_t drv_co5300_init(const drv_co5300_cfg_t *cfg, esp_lcd_panel_io_handle_t *io, esp_lcd_panel_handle_t *panel) {
  const spi_bus_config_t bus = CO5300_PANEL_BUS_QSPI_CONFIG(cfg->sclk, cfg->d0, cfg->d1, cfg->d2, cfg->d3,
                                                           (int)cfg->max_transfer_bytes);
  ESP_RETURN_ON_ERROR(spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO), TAG, "spi bus");
  const esp_lcd_panel_io_spi_config_t io_cfg = CO5300_PANEL_IO_QSPI_CONFIG(cfg->cs, NULL, NULL);
  ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI2_HOST, &io_cfg, io), TAG, "panel io");
  co5300_vendor_config_t vendor = {
    .init_cmds = cfg->init_cmds, /* NULL: the component's built-in table */
    .init_cmds_size = cfg->init_cmds != NULL ? cfg->init_cmds_size : 0,
    .flags = {.use_qspi_interface = 1},
  };
  const esp_lcd_panel_dev_config_t dev = {
    .reset_gpio_num = cfg->rst,
    .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
    .bits_per_pixel = 16,
    .vendor_config = &vendor,
  };
  ESP_RETURN_ON_ERROR(esp_lcd_new_panel_co5300(*io, &dev, panel), TAG, "panel");
  ESP_RETURN_ON_ERROR(esp_lcd_panel_set_gap(*panel, cfg->x_gap, 0), TAG, "gap");
  ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(*panel), TAG, "reset");
  ESP_RETURN_ON_ERROR(esp_lcd_panel_init(*panel), TAG, "init");
  ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(*panel, true), TAG, "on");
  return ESP_OK;
}

void drv_co5300_set_brightness(esp_lcd_panel_handle_t panel, uint8_t pct) {
  esp_lcd_panel_co5300_set_brightness(panel, pct > 100 ? 100 : pct);
}
```

`firmware/ports/esp32/main/drivers/drv_cst9217.h`:

```c
/* SPDX-License-Identifier: Apache-2.0 */
/* CST9217 touch controller (amoled-175c, amoled-175) on the shared I2C bus,
 * on top of waveshare/esp_lcd_touch_cst9217. */
#ifndef DRV_CST9217_H
#define DRV_CST9217_H

#include <stdbool.h>
#include <stdint.h>

#include "driver/i2c_master.h"
#include "esp_err.h"
#include "esp_lcd_touch.h"

typedef struct {
  i2c_master_bus_handle_t bus;
  int rst, intr;
  uint16_t width, height;
  bool mirror_x, mirror_y;
} drv_cst9217_cfg_t;

esp_err_t drv_cst9217_init(const drv_cst9217_cfg_t *cfg, esp_lcd_touch_handle_t *tp);

#endif /* DRV_CST9217_H */
```

`firmware/ports/esp32/main/drivers/drv_cst9217.c`:

```c
/* SPDX-License-Identifier: Apache-2.0 */
#include "drv_cst9217.h"

#include "esp_check.h"
#include "esp_lcd_io_i2c.h"
#include "esp_lcd_touch_cst9217.h"

static const char *TAG = "cst9217";

esp_err_t drv_cst9217_init(const drv_cst9217_cfg_t *cfg, esp_lcd_touch_handle_t *tp) {
  esp_lcd_panel_io_handle_t tio = NULL;
  esp_lcd_panel_io_i2c_config_t io_cfg = ESP_LCD_TOUCH_IO_I2C_CST9217_CONFIG();
  io_cfg.scl_speed_hz = 400000;
  ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_i2c(cfg->bus, &io_cfg, &tio), TAG, "touch io");
  const esp_lcd_touch_config_t tp_cfg = {
    .x_max = cfg->width,
    .y_max = cfg->height,
    .rst_gpio_num = cfg->rst,
    .int_gpio_num = cfg->intr,
    .levels = {.reset = 0, .interrupt = 0},
    .flags = {.swap_xy = 0, .mirror_x = cfg->mirror_x, .mirror_y = cfg->mirror_y},
  };
  return esp_lcd_touch_new_i2c_cst9217(tio, &tp_cfg, tp);
}
```

`firmware/ports/esp32/main/drivers/drv_st7789.h`:

```c
/* SPDX-License-Identifier: Apache-2.0 */
/* ST7789 LCD on SPI with an LEDC-dimmed backlight (lcd-154, devkit), on
 * ESP-IDF's built-in esp_lcd ST7789 driver. */
#ifndef DRV_ST7789_H
#define DRV_ST7789_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "esp_lcd_types.h"

typedef struct {
  int sclk, mosi, cs, dc, rst, backlight;
  int spi_mode;
  uint32_t pclk_hz;
  int x_gap, y_gap;
  bool swap_xy, mirror_x, mirror_y, invert;
  size_t max_transfer_bytes;
} drv_st7789_cfg_t;

esp_err_t drv_st7789_init(const drv_st7789_cfg_t *cfg, esp_lcd_panel_io_handle_t *io, esp_lcd_panel_handle_t *panel);
void drv_st7789_set_brightness(uint8_t pct);

#endif /* DRV_ST7789_H */
```

`firmware/ports/esp32/main/drivers/drv_st7789.c`:

```c
/* SPDX-License-Identifier: Apache-2.0 */
#include "drv_st7789.h"

#include "driver/ledc.h"
#include "driver/spi_common.h"
#include "esp_check.h"
#include "esp_lcd_io_spi.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_st7789.h"

#define BL_MODE LEDC_LOW_SPEED_MODE
#define BL_TIMER LEDC_TIMER_0
#define BL_CHANNEL LEDC_CHANNEL_0
#define BL_BITS LEDC_TIMER_10_BIT
#define BL_MAX_DUTY 1023u

static const char *TAG = "st7789";
static bool s_bl_ready;

static esp_err_t backlight_init(int gpio) {
  const ledc_timer_config_t timer = {
    .speed_mode = BL_MODE,
    .duty_resolution = BL_BITS,
    .timer_num = BL_TIMER,
    .freq_hz = 5000,
    .clk_cfg = LEDC_AUTO_CLK,
  };
  ESP_RETURN_ON_ERROR(ledc_timer_config(&timer), TAG, "backlight timer");
  const ledc_channel_config_t ch = {
    .gpio_num = gpio,
    .speed_mode = BL_MODE,
    .channel = BL_CHANNEL,
    .timer_sel = BL_TIMER,
    .duty = 0,
    .hpoint = 0,
  };
  ESP_RETURN_ON_ERROR(ledc_channel_config(&ch), TAG, "backlight channel");
  s_bl_ready = true;
  return ESP_OK;
}

void drv_st7789_set_brightness(uint8_t pct) {
  if (!s_bl_ready) {
    return;
  }
  uint32_t duty = (uint32_t)(pct > 100 ? 100 : pct) * BL_MAX_DUTY / 100u;
  ledc_set_duty(BL_MODE, BL_CHANNEL, duty);
  ledc_update_duty(BL_MODE, BL_CHANNEL);
}

esp_err_t drv_st7789_init(const drv_st7789_cfg_t *cfg, esp_lcd_panel_io_handle_t *io, esp_lcd_panel_handle_t *panel) {
  ESP_RETURN_ON_ERROR(backlight_init(cfg->backlight), TAG, "backlight");
  const spi_bus_config_t bus = {
    .sclk_io_num = cfg->sclk,
    .mosi_io_num = cfg->mosi,
    .miso_io_num = -1,
    .quadwp_io_num = -1,
    .quadhd_io_num = -1,
    .max_transfer_sz = (int)cfg->max_transfer_bytes,
  };
  ESP_RETURN_ON_ERROR(spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO), TAG, "spi bus");
  const esp_lcd_panel_io_spi_config_t io_cfg = {
    .cs_gpio_num = cfg->cs,
    .dc_gpio_num = cfg->dc,
    .spi_mode = cfg->spi_mode,
    .pclk_hz = cfg->pclk_hz,
    .trans_queue_depth = 10,
    .lcd_cmd_bits = 8,
    .lcd_param_bits = 8,
  };
  ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI2_HOST, &io_cfg, io), TAG, "panel io");
  const esp_lcd_panel_dev_config_t dev = {
    .reset_gpio_num = cfg->rst,
    .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
    .bits_per_pixel = 16,
  };
  ESP_RETURN_ON_ERROR(esp_lcd_new_panel_st7789(*io, &dev, panel), TAG, "panel");
  ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(*panel), TAG, "reset");
  ESP_RETURN_ON_ERROR(esp_lcd_panel_init(*panel), TAG, "init");
  ESP_RETURN_ON_ERROR(esp_lcd_panel_invert_color(*panel, cfg->invert), TAG, "invert");
  ESP_RETURN_ON_ERROR(esp_lcd_panel_swap_xy(*panel, cfg->swap_xy), TAG, "swap");
  ESP_RETURN_ON_ERROR(esp_lcd_panel_mirror(*panel, cfg->mirror_x, cfg->mirror_y), TAG, "mirror");
  ESP_RETURN_ON_ERROR(esp_lcd_panel_set_gap(*panel, cfg->x_gap, cfg->y_gap), TAG, "gap");
  ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(*panel, true), TAG, "on");
  return ESP_OK;
}
```

`firmware/ports/esp32/main/display.c`:

```c
/* SPDX-License-Identifier: Apache-2.0 */
/* LVGL 9 display and touch glue, single-threaded (no esp_lvgl_port, which
 * runs LVGL on its own task). The gadget task calls lv_timer_handler()
 * through ui_tick(); LVGL renders into two internal DMA buffers of
 * BOARD_LCD_BUF_LINES lines and flush_cb() hands each area to the panel.
 * The panel IO's transfer-done callback tells LVGL the buffer is free. */
#include "board.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "lvgl.h"
#include "pl_display.h"
#include "port.h"

#ifndef BOARD_LCD_EVEN_AREAS
#define BOARD_LCD_EVEN_AREAS 0
#endif

static const char *TAG = "display";
static esp_lcd_panel_handle_t s_panel;
static lv_display_t *s_disp;
static struct {
  bool pressed;
  int16_t x, y;
} s_touch;

static uint32_t tick_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }

/* Runs in the SPI driver's interrupt context. */
static bool on_trans_done(esp_lcd_panel_io_handle_t io, esp_lcd_panel_io_event_data_t *edata, void *ctx) {
  (void)io;
  (void)edata;
  lv_display_flush_ready((lv_display_t *)ctx);
  return false;
}

static void flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px) {
  uint32_t n = (uint32_t)lv_area_get_width(area) * (uint32_t)lv_area_get_height(area);
#if LVGL_VERSION_MAJOR > 9 || (LVGL_VERSION_MAJOR == 9 && LVGL_VERSION_MINOR >= 6)
  lv_draw_rgb565_swap(px, n); /* the panels take big-endian RGB565 */
#else
  lv_draw_sw_rgb565_swap(px, n);
#endif
  if (esp_lcd_panel_draw_bitmap(s_panel, area->x1, area->y1, area->x2 + 1, area->y2 + 1, px) != ESP_OK) {
    lv_display_flush_ready(disp); /* no transfer started, so no done callback will come */
  }
}

static void rounder_cb(lv_event_t *e) {
  lv_area_t *a = lv_event_get_param(e);
  int32_t x1 = a->x1, y1 = a->y1, x2 = a->x2, y2 = a->y2;
  pl_round_even_area(&x1, &y1, &x2, &y2, lv_display_get_horizontal_resolution(s_disp),
                     lv_display_get_vertical_resolution(s_disp));
  a->x1 = x1;
  a->y1 = y1;
  a->x2 = x2;
  a->y2 = y2;
}

static void touch_read_cb(lv_indev_t *indev, lv_indev_data_t *data) {
  (void)indev;
  data->point.x = s_touch.x;
  data->point.y = s_touch.y;
  data->state = s_touch.pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

void port_display_touch(bool pressed, int16_t x, int16_t y) {
  s_touch.pressed = pressed;
  s_touch.x = x;
  s_touch.y = y;
}

esp_err_t port_display_init(const gadget_board_t *board, esp_lcd_panel_io_handle_t io, esp_lcd_panel_handle_t panel,
                            bool has_touch) {
  s_panel = panel;
  lv_init();
  lv_tick_set_cb(tick_ms);
  s_disp = lv_display_create(board->screen_w, board->screen_h);
  if (s_disp == NULL) {
    return ESP_ERR_NO_MEM;
  }
  size_t bytes = (size_t)board->screen_w * BOARD_LCD_BUF_LINES * sizeof(uint16_t);
  void *buf1 = heap_caps_malloc(bytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
  void *buf2 = heap_caps_malloc(bytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
  if (buf1 == NULL || buf2 == NULL) {
    ESP_LOGE(TAG, "no internal DMA memory for 2 x %u bytes", (unsigned)bytes);
    return ESP_ERR_NO_MEM;
  }
  lv_display_set_color_format(s_disp, LV_COLOR_FORMAT_RGB565);
  lv_display_set_buffers(s_disp, buf1, buf2, (uint32_t)bytes, LV_DISPLAY_RENDER_MODE_PARTIAL);
  lv_display_set_flush_cb(s_disp, flush_cb);
  const esp_lcd_panel_io_callbacks_t cbs = {.on_color_trans_done = on_trans_done};
  ESP_RETURN_ON_ERROR(esp_lcd_panel_io_register_event_callbacks(io, &cbs, s_disp), TAG, "panel io callbacks");
  if (BOARD_LCD_EVEN_AREAS) {
    lv_display_add_event_cb(s_disp, rounder_cb, LV_EVENT_INVALIDATE_AREA, NULL);
  }
  if (has_touch) {
    lv_indev_t *indev = lv_indev_create();
    if (indev == NULL) {
      return ESP_ERR_NO_MEM;
    }
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, touch_read_cb);
    lv_indev_set_display(indev, s_disp);
  }
  return ESP_OK;
}
```

In `firmware/ports/esp32/main/CMakeLists.txt`, replace the `set(port_srcs ...)` and `set(logic_srcs ...)` blocks with:

```cmake
set(port_srcs
  main.c
  port_events.c
  hal_system.c
  console_usj.c
  hal_storage.c
  hal_wifi.c
  hal_ws.c
  hal_mdns.c
  hal_audio.c
  hal_input.c
  hal_battery.c
  display.c)

set(logic_srcs
  logic/pl_event.c
  logic/pl_util.c
  logic/pl_scan.c
  logic/pl_wifi.c
  logic/pl_wsasm.c
  logic/pl_mdns.c
  logic/pl_audio.c
  logic/pl_input.c
  logic/pl_power.c
  logic/pl_display.c)
```

And the board driver lists (final):

`firmware/ports/esp32/boards/amoled-175c/board.cmake`:

```cmake
# SPDX-License-Identifier: Apache-2.0
# Drivers from main/drivers/ that boards/amoled-175c/board.c uses.
set(BOARD_DRIVER_SRCS drv_axp2101.c drv_co5300.c drv_cst9217.c drv_es_codec.c)
```

`firmware/ports/esp32/boards/amoled-175/board.cmake`:

```cmake
# SPDX-License-Identifier: Apache-2.0
# Drivers from main/drivers/ that boards/amoled-175/board.c uses.
set(BOARD_DRIVER_SRCS drv_axp2101.c drv_co5300.c drv_cst9217.c drv_es_codec.c)
```

`firmware/ports/esp32/boards/lcd-154/board.cmake`:

```cmake
# SPDX-License-Identifier: Apache-2.0
# Drivers from main/drivers/ that boards/lcd-154/board.c uses.
set(BOARD_DRIVER_SRCS drv_es_codec.c drv_lipo_adc.c drv_st7789.c)
```

`firmware/ports/esp32/boards/devkit/board.cmake`:

```cmake
# SPDX-License-Identifier: Apache-2.0
# Drivers from main/drivers/ that boards/devkit/board.c uses.
set(BOARD_DRIVER_SRCS drv_i2s_simplex.c drv_st7789.c)
```

- [ ] **Step 6: Build all four boards**

Run:

```bash
. ~/.espressif/tools/activate_idf_v6.0.3.sh
SKIP_ART_CHECK=1 firmware/ports/esp32/tools/build-all.sh
```

Expected, for each of `amoled-175`, `amoled-175c`, `devkit`, `lcd-154` (alphabetical): `== <board>` followed by `check-size: <board> ok, <n> of 6291456 bytes (<p>%)`; exit status 0.

- [ ] **Step 7: Commit**

```bash
git add firmware/ports/esp32/main \
  firmware/ports/esp32/boards \
  firmware/ports/esp32/host-tests
git commit -m "feat(esp32): CO5300, ST7789 and CST9217 drivers with single-threaded LVGL glue"
```
### Task 11: OTA slot HAL

**Files:**
- Create: `firmware/ports/esp32/main/logic/pl_otaq.h`, `firmware/ports/esp32/main/logic/pl_otaq.c`
- Create: `firmware/ports/esp32/host-tests/test_pl_otaq.c`
- Create: `firmware/ports/esp32/main/hal_ota.c`
- Modify: `firmware/ports/esp32/main/CMakeLists.txt`, `firmware/ports/esp32/host-tests/CMakeLists.txt`

**Interfaces:**
- Consumes: OTA HAL (contract §2.5: `hal_ota_begin(size ≤ ota_max)`, `hal_ota_write` copies into a queue of at least 64 KiB + 4 KiB and returns at once, `BUSY` when full, contiguous offsets, progress as `GADGET_EV_OTA_WRITTEN {written}`, failures as `GADGET_EV_OTA_ERROR`; `hal_ota_finalize` may block ≤ 2 s; `hal_ota_set_boot` does not restart; `hal_ota_running_state`, `hal_ota_mark_valid`, `hal_ota_mark_invalid_and_reboot`); ESP32 notes (worker task, `esp_ota_begin(part, OTA_WITH_SEQUENTIAL_WRITES, …)`); `GADGET_FW_CHUNK_MAX`; `port_board()` (Task 3).
- Produces: `pl_otaq_t` + `pl_otaq_init(q, size, cap)`, `pl_otaq_admit(q, offset, len)` (`OK`, `BUSY`, `ARG` for empty/oversize/non-contiguous, `LIMIT` past the size), `pl_otaq_unadmit(q, len)`, `pl_otaq_done(q, len)`, `PL_OTAQ_CAP` = 69632; `port_ota_init()`; the eight `hal_ota_*` functions. A flash-written image that `esp_ota_end()` rejects returns `GADGET_ERR_IO` from `hal_ota_finalize`. An image flashed over USB (no otadata entry) reports `HAL_OTA_IMG_VALID`.

- [ ] **Step 1: Write the failing test**

`firmware/ports/esp32/host-tests/test_pl_otaq.c`:

```c
/* SPDX-License-Identifier: Apache-2.0 */
#include "pl_otaq.h"
#include "unity.h"

static pl_otaq_t q;

void setUp(void) { pl_otaq_init(&q, 10000, PL_OTAQ_CAP); }
void tearDown(void) {}

static void test_contiguous_writes_until_full(void) {
  pl_otaq_init(&q, 200000, PL_OTAQ_CAP);
  uint32_t off = 0;
  for (int i = 0; i < 17; i++) {
    TEST_ASSERT_EQUAL(GADGET_OK, pl_otaq_admit(&q, off, 4096));
    off += 4096;
  }
  TEST_ASSERT_EQUAL_UINT32(69632, q.queued);
  TEST_ASSERT_EQUAL(GADGET_ERR_BUSY, pl_otaq_admit(&q, off, 1));
  TEST_ASSERT_EQUAL_UINT32(off, q.next); /* BUSY does not advance */
  pl_otaq_done(&q, 4096);
  TEST_ASSERT_EQUAL(GADGET_OK, pl_otaq_admit(&q, off, 4096));
}

static void test_gaps_overlaps_and_sizes(void) {
  TEST_ASSERT_EQUAL(GADGET_ERR_ARG, pl_otaq_admit(&q, 4, 10));   /* gap */
  TEST_ASSERT_EQUAL(GADGET_OK, pl_otaq_admit(&q, 0, 4096));
  TEST_ASSERT_EQUAL(GADGET_ERR_ARG, pl_otaq_admit(&q, 0, 4096)); /* replay */
  TEST_ASSERT_EQUAL(GADGET_ERR_ARG, pl_otaq_admit(&q, 4096, 0));
  TEST_ASSERT_EQUAL(GADGET_ERR_ARG, pl_otaq_admit(&q, 4096, 4097));
  TEST_ASSERT_EQUAL(GADGET_OK, pl_otaq_admit(&q, 4096, 4096));
  TEST_ASSERT_EQUAL(GADGET_ERR_LIMIT, pl_otaq_admit(&q, 8192, 4096)); /* past 10000 */
  TEST_ASSERT_EQUAL(GADGET_OK, pl_otaq_admit(&q, 8192, 1808));
  pl_otaq_done(&q, 999999);
  TEST_ASSERT_EQUAL_UINT32(0, q.queued);
}

static void test_unadmit_rolls_back(void) {
  TEST_ASSERT_EQUAL(GADGET_OK, pl_otaq_admit(&q, 0, 4096));
  TEST_ASSERT_EQUAL(GADGET_OK, pl_otaq_admit(&q, 4096, 100));
  pl_otaq_unadmit(&q, 100);
  TEST_ASSERT_EQUAL_UINT32(4096, q.next);
  TEST_ASSERT_EQUAL_UINT32(4096, q.queued);
  TEST_ASSERT_EQUAL(GADGET_OK, pl_otaq_admit(&q, 4096, 100)); /* the same chunk again */
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_contiguous_writes_until_full);
  RUN_TEST(test_gaps_overlaps_and_sizes);
  RUN_TEST(test_unadmit_rolls_back);
  return UNITY_END();
}
```

Append to `firmware/ports/esp32/host-tests/CMakeLists.txt`:

```cmake
port_test(test_pl_otaq)
```

- [ ] **Step 2: Run it to verify it fails**

Run: `cmake -S firmware/ports/esp32/host-tests -B build/esp32-host && cmake --build build/esp32-host -j10 && ctest --test-dir build/esp32-host --output-on-failure`

Expected: FAIL — the build stops with `fatal error: 'pl_otaq.h' file not found` (clang; GCC prints `pl_otaq.h: No such file or directory`).

- [ ] **Step 3: Write the queue accounting**

`firmware/ports/esp32/main/logic/pl_otaq.h`:

```c
/* SPDX-License-Identifier: Apache-2.0 */
/* Byte accounting for the OTA write queue (gadget_hal.h: hal_ota_write
 * copies into a queue of at least 64 KiB + 4 KiB and returns BUSY when it
 * is full). hal_ota.c guards a pl_otaq_t with a spinlock. */
#ifndef PL_OTAQ_H
#define PL_OTAQ_H

#include "gadget_types.h"

#define PL_OTAQ_CAP (64u * 1024u + GADGET_FW_CHUNK_MAX)

typedef struct {
  uint32_t size;     /* image size from hal_ota_begin() */
  uint32_t cap;      /* bytes the queue may hold */
  uint32_t queued;   /* accepted, not yet durably written */
  uint32_t next;     /* next expected offset */
} pl_otaq_t;

void pl_otaq_init(pl_otaq_t *q, uint32_t size, uint32_t cap);
/* GADGET_OK (accepted), GADGET_ERR_BUSY (queue full, try again),
 * GADGET_ERR_ARG (empty or > GADGET_FW_CHUNK_MAX, or offset != next),
 * GADGET_ERR_LIMIT (past the image size). */
gadget_status_t pl_otaq_admit(pl_otaq_t *q, uint32_t offset, size_t len);
/* Undo the last successful admit of len bytes (the chunk could not be queued). */
void pl_otaq_unadmit(pl_otaq_t *q, size_t len);
/* The worker wrote (or dropped) len bytes. */
void pl_otaq_done(pl_otaq_t *q, size_t len);

#endif /* PL_OTAQ_H */
```

`firmware/ports/esp32/main/logic/pl_otaq.c`:

```c
/* SPDX-License-Identifier: Apache-2.0 */
#include "pl_otaq.h"

void pl_otaq_init(pl_otaq_t *q, uint32_t size, uint32_t cap) {
  q->size = size;
  q->cap = cap;
  q->queued = 0;
  q->next = 0;
}

gadget_status_t pl_otaq_admit(pl_otaq_t *q, uint32_t offset, size_t len) {
  if (len == 0 || len > GADGET_FW_CHUNK_MAX || offset != q->next) {
    return GADGET_ERR_ARG;
  }
  if ((uint64_t)offset + len > q->size) {
    return GADGET_ERR_LIMIT;
  }
  if (q->queued + len > q->cap) {
    return GADGET_ERR_BUSY;
  }
  q->queued += (uint32_t)len;
  q->next += (uint32_t)len;
  return GADGET_OK;
}

void pl_otaq_unadmit(pl_otaq_t *q, size_t len) {
  q->next -= (uint32_t)len;
  pl_otaq_done(q, len);
}

void pl_otaq_done(pl_otaq_t *q, size_t len) {
  q->queued = len >= q->queued ? 0 : q->queued - (uint32_t)len;
}
```

In `firmware/ports/esp32/host-tests/CMakeLists.txt`, replace the `set(PORT_LOGIC_SRCS ...)` block with:

```cmake
set(PORT_LOGIC_SRCS
  ${CORE_DIR}/src/boards.c
  ${ESP_DIR}/main/logic/pl_event.c
  ${ESP_DIR}/main/logic/pl_util.c
  ${ESP_DIR}/main/logic/pl_scan.c
  ${ESP_DIR}/main/logic/pl_wifi.c
  ${ESP_DIR}/main/logic/pl_wsasm.c
  ${ESP_DIR}/main/logic/pl_mdns.c
  ${ESP_DIR}/main/logic/pl_audio.c
  ${ESP_DIR}/main/logic/pl_input.c
  ${ESP_DIR}/main/logic/pl_power.c
  ${ESP_DIR}/main/logic/pl_display.c
  ${ESP_DIR}/main/logic/pl_otaq.c
)
```

- [ ] **Step 4: Run the tests to verify they pass**

Run: `cmake -S firmware/ports/esp32/host-tests -B build/esp32-host && cmake --build build/esp32-host -j10 && ctest --test-dir build/esp32-host --output-on-failure`

Expected: `100% tests passed, 0 tests failed out of 13`.

- [ ] **Step 5: Write the OTA HAL**

`firmware/ports/esp32/main/hal_ota.c`:

```c
/* SPDX-License-Identifier: Apache-2.0 */
/* HAL OTA slot group (contract §2.5). Flash erases and writes can take
 * hundreds of milliseconds, so a worker task does every esp_ota_* call on
 * the handle; hal_ota_write() only copies the chunk into the worker's queue
 * (at most PL_OTAQ_CAP bytes in flight) and returns. Probation is core's
 * (core_config.probation_ms = 0 → 5 min); this file only reports the image
 * state and marks it valid or invalid. Anti-rollback is never enabled. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "gadget_hal.h"
#include "pl_otaq.h"
#include "port.h"

#define OTA_QUEUE_LEN 64
#define OTA_FINALIZE_WAIT_MS 2000

typedef enum { OTA_MSG_BEGIN = 1, OTA_MSG_CHUNK, OTA_MSG_FINALIZE, OTA_MSG_ABORT } ota_msg_kind_t;

typedef struct {
  ota_msg_kind_t kind;
  uint32_t gen;
  uint32_t len;
  uint8_t *data;
} ota_msg_t;

static const char *TAG = "ota";
static QueueHandle_t s_q;
static SemaphoreHandle_t s_finalized;
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static pl_otaq_t s_acct;          /* guarded by s_mux */
static uint32_t s_acct_gen;       /* guarded by s_mux */
static const esp_partition_t *s_part;

/* gadget task only */
static bool s_busy;
static uint32_t s_gen;

/* worker task only */
static esp_ota_handle_t s_handle;
static bool s_open;
static bool s_failed;
static uint32_t s_written;
static uint32_t s_wgen;
static volatile esp_err_t s_final_err;

static void post_error(gadget_status_t err) {
  gadget_event_t ev = {.type = GADGET_EV_OTA_ERROR};
  ev.u.ota_error.err = err;
  port_post_event(&ev);
}

static void account_done(uint32_t gen, uint32_t len) {
  taskENTER_CRITICAL(&s_mux);
  if (gen == s_acct_gen) {
    pl_otaq_done(&s_acct, len);
  }
  taskEXIT_CRITICAL(&s_mux);
}

static void close_handle(void) {
  if (s_open) {
    esp_ota_abort(s_handle);
    s_open = false;
  }
}

static void ota_task(void *arg) {
  (void)arg;
  ota_msg_t m;
  for (;;) {
    if (xQueueReceive(s_q, &m, portMAX_DELAY) != pdTRUE) {
      continue;
    }
    switch (m.kind) {
      case OTA_MSG_BEGIN: {
        close_handle();
        s_wgen = m.gen;
        s_written = 0;
        s_failed = false;
        esp_err_t e = esp_ota_begin(s_part, OTA_WITH_SEQUENTIAL_WRITES, &s_handle);
        if (e == ESP_OK) {
          s_open = true;
        } else {
          ESP_LOGE(TAG, "begin: %s", esp_err_to_name(e));
          s_failed = true;
          post_error(GADGET_ERR_IO);
        }
        break;
      }
      case OTA_MSG_CHUNK:
        if (m.gen == s_wgen && s_open && !s_failed) {
          esp_err_t e = esp_ota_write(s_handle, m.data, m.len);
          if (e == ESP_OK) {
            s_written += m.len;
            gadget_event_t ev = {.type = GADGET_EV_OTA_WRITTEN};
            ev.u.ota_written.written = s_written;
            port_post_event(&ev);
          } else {
            ESP_LOGE(TAG, "write at %u: %s", (unsigned)s_written, esp_err_to_name(e));
            s_failed = true;
            post_error(GADGET_ERR_IO);
          }
        }
        account_done(m.gen, m.len);
        free(m.data);
        break;
      case OTA_MSG_FINALIZE:
        if (m.gen == s_wgen && s_open && !s_failed) {
          s_final_err = esp_ota_end(s_handle); /* checks the image; frees the handle either way */
          s_open = false;
        } else {
          s_final_err = ESP_ERR_INVALID_STATE;
        }
        xSemaphoreGive(s_finalized);
        break;
      case OTA_MSG_ABORT:
        close_handle();
        break;
    }
  }
}

esp_err_t port_ota_init(void) {
  s_q = xQueueCreate(OTA_QUEUE_LEN, sizeof(ota_msg_t));
  s_finalized = xSemaphoreCreateBinary();
  if (s_q == NULL || s_finalized == NULL) {
    return ESP_ERR_NO_MEM;
  }
  return xTaskCreatePinnedToCore(ota_task, "ota", 4096, NULL, 3, NULL, 0) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}

static bool send_msg(const ota_msg_t *m) { return xQueueSend(s_q, m, pdMS_TO_TICKS(100)) == pdTRUE; }

gadget_status_t hal_ota_begin(uint32_t size) {
  if (s_busy) {
    return GADGET_ERR_BUSY;
  }
  const gadget_board_t *b = port_board();
  s_part = esp_ota_get_next_update_partition(NULL);
  if (s_part == NULL) {
    return GADGET_ERR_IO;
  }
  if (size == 0 || size > s_part->size || (b != NULL && size > b->ota_max)) {
    return GADGET_ERR_LIMIT;
  }
  uint32_t gen = s_gen + 1;
  taskENTER_CRITICAL(&s_mux);
  pl_otaq_init(&s_acct, size, PL_OTAQ_CAP);
  s_acct_gen = gen;
  taskEXIT_CRITICAL(&s_mux);
  ota_msg_t m = {.kind = OTA_MSG_BEGIN, .gen = gen};
  if (!send_msg(&m)) {
    return GADGET_ERR_BUSY;
  }
  s_gen = gen;
  s_busy = true;
  ESP_LOGI(TAG, "receiving %u bytes into %s", (unsigned)size, s_part->label);
  return GADGET_OK;
}

gadget_status_t hal_ota_write(uint32_t offset, const uint8_t *data, size_t len) {
  if (!s_busy) {
    return GADGET_ERR_STATE;
  }
  if (data == NULL) {
    return GADGET_ERR_ARG;
  }
  uint8_t *copy = heap_caps_malloc(len > 0 ? len : 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (copy == NULL) {
    copy = malloc(len > 0 ? len : 1);
  }
  if (copy == NULL) {
    return GADGET_ERR_NO_MEM;
  }
  taskENTER_CRITICAL(&s_mux);
  gadget_status_t st = pl_otaq_admit(&s_acct, offset, len);
  taskEXIT_CRITICAL(&s_mux);
  if (st != GADGET_OK) {
    free(copy);
    return st;
  }
  memcpy(copy, data, len);
  ota_msg_t m = {.kind = OTA_MSG_CHUNK, .gen = s_gen, .len = (uint32_t)len, .data = copy};
  if (xQueueSend(s_q, &m, 0) != pdTRUE) {
    taskENTER_CRITICAL(&s_mux);
    pl_otaq_unadmit(&s_acct, len);
    taskEXIT_CRITICAL(&s_mux);
    free(copy);
    return GADGET_ERR_BUSY;
  }
  return GADGET_OK;
}

gadget_status_t hal_ota_finalize(void) {
  if (!s_busy) {
    return GADGET_ERR_STATE;
  }
  xSemaphoreTake(s_finalized, 0); /* drop a stale signal */
  ota_msg_t m = {.kind = OTA_MSG_FINALIZE, .gen = s_gen};
  if (!send_msg(&m)) {
    return GADGET_ERR_BUSY;
  }
  if (xSemaphoreTake(s_finalized, pdMS_TO_TICKS(OTA_FINALIZE_WAIT_MS)) != pdTRUE) {
    return GADGET_ERR_TIMEOUT;
  }
  if (s_final_err != ESP_OK) {
    ESP_LOGE(TAG, "image check failed: %s", esp_err_to_name(s_final_err));
    s_busy = false;
    return GADGET_ERR_IO;
  }
  return GADGET_OK;
}

gadget_status_t hal_ota_set_boot(const char *version) {
  (void)version; /* the simulator records it; the bootloader reads the image header */
  if (s_part == NULL) {
    return GADGET_ERR_STATE;
  }
  esp_err_t e = esp_ota_set_boot_partition(s_part);
  s_busy = false;
  if (e != ESP_OK) {
    ESP_LOGE(TAG, "set boot: %s", esp_err_to_name(e));
    return GADGET_ERR_IO;
  }
  return GADGET_OK;
}

void hal_ota_abort(void) {
  if (!s_busy) {
    return;
  }
  s_gen++;
  taskENTER_CRITICAL(&s_mux);
  s_acct_gen = s_gen; /* chunks still queued no longer count */
  pl_otaq_init(&s_acct, 0, PL_OTAQ_CAP);
  taskEXIT_CRITICAL(&s_mux);
  ota_msg_t m = {.kind = OTA_MSG_ABORT, .gen = s_gen};
  if (!send_msg(&m)) {
    ESP_LOGE(TAG, "abort request dropped: queue full");
  }
  s_busy = false;
}

hal_ota_img_state_t hal_ota_running_state(void) {
  esp_ota_img_states_t st;
  esp_err_t e = esp_ota_get_state_partition(esp_ota_get_running_partition(), &st);
  if (e == ESP_ERR_NOT_SUPPORTED || e == ESP_ERR_NOT_FOUND) {
    return HAL_OTA_IMG_VALID; /* flashed over USB: no otadata entry yet */
  }
  if (e != ESP_OK) {
    return HAL_OTA_IMG_UNKNOWN;
  }
  switch (st) {
    case ESP_OTA_IMG_PENDING_VERIFY: return HAL_OTA_IMG_PENDING_VERIFY;
    case ESP_OTA_IMG_VALID:
    case ESP_OTA_IMG_UNDEFINED:
    case ESP_OTA_IMG_NEW: return HAL_OTA_IMG_VALID;
    default: return HAL_OTA_IMG_UNKNOWN;
  }
}

gadget_status_t hal_ota_mark_valid(void) {
  esp_err_t e = esp_ota_mark_app_valid_cancel_rollback();
  return e == ESP_OK ? GADGET_OK : GADGET_ERR_IO;
}

_Noreturn void hal_ota_mark_invalid_and_reboot(void) {
  esp_err_t e = esp_ota_mark_app_invalid_rollback_and_reboot(); /* returns only on failure */
  ESP_LOGE(TAG, "rollback failed (%s); restarting", esp_err_to_name(e));
  fflush(stdout);
  esp_restart();
}
```

In `firmware/ports/esp32/main/CMakeLists.txt`, replace the `set(port_srcs ...)` and `set(logic_srcs ...)` blocks with:

```cmake
set(port_srcs
  main.c
  port_events.c
  hal_system.c
  console_usj.c
  hal_storage.c
  hal_wifi.c
  hal_ws.c
  hal_mdns.c
  hal_audio.c
  hal_input.c
  hal_battery.c
  hal_ota.c
  display.c)

set(logic_srcs
  logic/pl_event.c
  logic/pl_util.c
  logic/pl_scan.c
  logic/pl_wifi.c
  logic/pl_wsasm.c
  logic/pl_mdns.c
  logic/pl_audio.c
  logic/pl_input.c
  logic/pl_power.c
  logic/pl_display.c
  logic/pl_otaq.c)
```

- [ ] **Step 6: Build all four boards and the bench-only test-key variant**

Run:

```bash
. ~/.espressif/tools/activate_idf_v6.0.3.sh
SKIP_ART_CHECK=1 firmware/ports/esp32/tools/build-all.sh
```

Expected, for each of `amoled-175`, `amoled-175c`, `devkit`, `lcd-154` (alphabetical): `== <board>` followed by `check-size: <board> ok, <n> of 6291456 bytes (<p>%)`; exit status 0.

Then:

```bash
cd firmware/ports/esp32
idf.py -B build/amoled-175c-test-keys -D GADGET_BOARD=amoled-175c -D SDKCONFIG=build/amoled-175c-test-keys/sdkconfig -D GADGET_TEST_KEYS=1 build > build/amoled-175c-test-keys.log 2>&1; echo "exit $?"
grep -xF 'CONFIG_GADGET_TEST_KEYS=y' build/amoled-175c-test-keys/sdkconfig
grep -xF '# CONFIG_GADGET_TEST_KEYS is not set' build/amoled-175c/sdkconfig
tools/check-size.sh amoled-175c build/amoled-175c-test-keys
cd ../../..
```

Expected: `exit 0`, both `grep` lines print, `check-size: amoled-175c ok, ...`. The normal board build keeps the test key out (A37).

- [ ] **Step 7: Commit**

```bash
git add firmware/ports/esp32/main \
  firmware/ports/esp32/host-tests
git commit -m "feat(esp32): OTA slot HAL with a worker task and queue back-pressure"
```
### Task 12: Board glue for the four boards

**Files:**
- Create: `firmware/ports/esp32/boards/amoled-175c/board.c`, `firmware/ports/esp32/boards/amoled-175/board.c`, `firmware/ports/esp32/boards/lcd-154/board.c`, `firmware/ports/esp32/boards/devkit/board.c`
- Modify: `firmware/ports/esp32/main/CMakeLists.txt` (adds `${board_dir}/board.c`)

**Interfaces:**
- Consumes: `board_api.h` (Task 3); the `BOARD_*` pin macros and, on the AMOLED boards, `BOARD_CO5300_INIT_CMDS` (Task 2); `drv_co5300_*` (the board passes its table in `drv_co5300_cfg_t.init_cmds`), `drv_cst9217_init`, `drv_st7789_*` (Task 10); `drv_es_codec_init`, `drv_i2s_simplex_init` (Task 8); `drv_axp2101_*`, `drv_lipo_adc_*` (Task 9); IDF `i2c_new_master_bus`, `gpio_config`, `gpio_get_level`, `gpio_set_level`.
- Produces: the nine `board_*` functions for each board, behaving as spec §5.3/§5.4 and A18 require: amoled-175c TALK = BOOT (GPIO0, low), CANCEL = PWR mirror (GPIO3, high), battery from the AXP2101; amoled-175 TALK = BOOT, no CANCEL, AXP2101 battery; lcd-154 BAT_EN (GPIO2) driven high as the very first action, TALK = BOOT (GPIO0), CANCEL = PLUS (GPIO4), battery from BAT_ADC ×3 and CHG_STAT; devkit TALK = BOOT, CANCEL = GPIO1, no battery, `ESP_ERR_NOT_SUPPORTED` from `board_touch_init` on the two boards without touch.

There is no new pure logic in this task (each `board.c` only wires drivers to pins), so its check is the build of every board; the on-device checks are in `docs/hardware-checklist.md` (Task 15, B1–B14 and B8a).

- [ ] **Step 1: Write the four board files**

`firmware/ports/esp32/boards/amoled-175c/board.c`:

```c
/* SPDX-License-Identifier: Apache-2.0 */
/* Board glue for amoled-175c (main/board_api.h). */
#include "board.h"

#include "board_api.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "drv_axp2101.h"
#include "drv_co5300.h"
#include "drv_cst9217.h"
#include "drv_es_codec.h"
#include "esp_check.h"
#include "esp_log.h"

#define SCREEN 466

static const char *TAG = "board";
static i2c_master_bus_handle_t s_i2c;
static esp_lcd_panel_handle_t s_panel;
static bool s_pmu;
static const co5300_lcd_init_cmd_t k_panel_init[] = BOARD_CO5300_INIT_CMDS;

esp_err_t board_early_init(void) {
  const i2c_master_bus_config_t bus = {
    .i2c_port = BOARD_I2C_PORT,
    .sda_io_num = BOARD_I2C_SDA,
    .scl_io_num = BOARD_I2C_SCL,
    .clk_source = I2C_CLK_SRC_DEFAULT,
    .glitch_ignore_cnt = 7,
    .flags.enable_internal_pullup = true,
  };
  ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bus, &s_i2c), TAG, "i2c bus");
  s_pmu = drv_axp2101_init(s_i2c) == ESP_OK;
  if (!s_pmu) {
    ESP_LOGW(TAG, "AXP2101 not answering; no battery level");
  }
  return ESP_OK;
}

esp_err_t board_display_init(esp_lcd_panel_io_handle_t *io, esp_lcd_panel_handle_t *panel) {
  const drv_co5300_cfg_t cfg = {
    .sclk = BOARD_LCD_SCLK, .d0 = BOARD_LCD_D0, .d1 = BOARD_LCD_D1, .d2 = BOARD_LCD_D2, .d3 = BOARD_LCD_D3,
    .cs = BOARD_LCD_CS, .rst = BOARD_LCD_RST, .x_gap = BOARD_LCD_X_GAP,
    .max_transfer_bytes = SCREEN * BOARD_LCD_BUF_LINES * 2,
    .init_cmds = k_panel_init, /* NULL here selects the component's own table (checklist check 2) */
    .init_cmds_size = sizeof(k_panel_init) / sizeof(k_panel_init[0]),
  };
  ESP_RETURN_ON_ERROR(drv_co5300_init(&cfg, io, panel), TAG, "co5300");
  s_panel = *panel;
  return ESP_OK;
}

esp_err_t board_touch_init(esp_lcd_touch_handle_t *tp) {
  const drv_cst9217_cfg_t cfg = {
    .bus = s_i2c, .rst = BOARD_TP_RST, .intr = BOARD_TP_INT, .width = SCREEN, .height = SCREEN,
    .mirror_x = BOARD_TP_MIRROR_X, .mirror_y = BOARD_TP_MIRROR_Y,
  };
  return drv_cst9217_init(&cfg, tp);
}

esp_err_t board_audio_init(board_audio_t *out) {
  const drv_es_codec_cfg_t cfg = {
    .bus = s_i2c, .i2c_port = BOARD_I2C_PORT, .i2s_port = BOARD_I2S_PORT,
    .mclk = BOARD_I2S_MCLK, .bclk = BOARD_I2S_BCLK, .ws = BOARD_I2S_WS, .dout = BOARD_I2S_DOUT,
    .din = BOARD_I2S_DIN, .pa = BOARD_PA_EN, .mic_channel_mask = BOARD_MIC_CHANNEL_MASK,
    .mic_gain_db = BOARD_MIC_GAIN_DB, .volume = 80,
  };
  return drv_es_codec_init(&cfg, out);
}

esp_err_t board_buttons_init(void) {
  const gpio_config_t talk = {.pin_bit_mask = 1ULL << BOARD_BTN_TALK, .mode = GPIO_MODE_INPUT, .pull_up_en = GPIO_PULLUP_ENABLE};
  const gpio_config_t cancel = {.pin_bit_mask = 1ULL << BOARD_BTN_CANCEL, .mode = GPIO_MODE_INPUT};
  ESP_RETURN_ON_ERROR(gpio_config(&talk), TAG, "talk");
  return gpio_config(&cancel);
}

bool board_talk_pressed(void) { return gpio_get_level(BOARD_BTN_TALK) == 0; }
bool board_cancel_pressed(void) { return gpio_get_level(BOARD_BTN_CANCEL) == 1; }
bool board_battery_read(gadget_battery_t *out) { return s_pmu && drv_axp2101_read(out); }

void board_set_brightness(uint8_t pct) {
  if (s_panel != NULL) {
    drv_co5300_set_brightness(s_panel, pct);
  }
}
```

`firmware/ports/esp32/boards/amoled-175/board.c`:

```c
/* SPDX-License-Identifier: Apache-2.0 */
/* Board glue for amoled-175 (main/board_api.h). */
#include "board.h"

#include "board_api.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "drv_axp2101.h"
#include "drv_co5300.h"
#include "drv_cst9217.h"
#include "drv_es_codec.h"
#include "esp_check.h"
#include "esp_log.h"

#define SCREEN 466

static const char *TAG = "board";
static i2c_master_bus_handle_t s_i2c;
static esp_lcd_panel_handle_t s_panel;
static bool s_pmu;
static const co5300_lcd_init_cmd_t k_panel_init[] = BOARD_CO5300_INIT_CMDS;

esp_err_t board_early_init(void) {
  const i2c_master_bus_config_t bus = {
    .i2c_port = BOARD_I2C_PORT,
    .sda_io_num = BOARD_I2C_SDA,
    .scl_io_num = BOARD_I2C_SCL,
    .clk_source = I2C_CLK_SRC_DEFAULT,
    .glitch_ignore_cnt = 7,
    .flags.enable_internal_pullup = true,
  };
  ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bus, &s_i2c), TAG, "i2c bus");
  s_pmu = drv_axp2101_init(s_i2c) == ESP_OK;
  if (!s_pmu) {
    ESP_LOGW(TAG, "AXP2101 not answering; no battery level");
  }
  return ESP_OK;
}

esp_err_t board_display_init(esp_lcd_panel_io_handle_t *io, esp_lcd_panel_handle_t *panel) {
  const drv_co5300_cfg_t cfg = {
    .sclk = BOARD_LCD_SCLK, .d0 = BOARD_LCD_D0, .d1 = BOARD_LCD_D1, .d2 = BOARD_LCD_D2, .d3 = BOARD_LCD_D3,
    .cs = BOARD_LCD_CS, .rst = BOARD_LCD_RST, .x_gap = BOARD_LCD_X_GAP,
    .max_transfer_bytes = SCREEN * BOARD_LCD_BUF_LINES * 2,
    .init_cmds = k_panel_init, /* NULL here selects the component's own table (checklist check 2) */
    .init_cmds_size = sizeof(k_panel_init) / sizeof(k_panel_init[0]),
  };
  ESP_RETURN_ON_ERROR(drv_co5300_init(&cfg, io, panel), TAG, "co5300");
  s_panel = *panel;
  return ESP_OK;
}

esp_err_t board_touch_init(esp_lcd_touch_handle_t *tp) {
  const drv_cst9217_cfg_t cfg = {
    .bus = s_i2c, .rst = BOARD_TP_RST, .intr = BOARD_TP_INT, .width = SCREEN, .height = SCREEN,
    .mirror_x = BOARD_TP_MIRROR_X, .mirror_y = BOARD_TP_MIRROR_Y,
  };
  return drv_cst9217_init(&cfg, tp);
}

esp_err_t board_audio_init(board_audio_t *out) {
  const drv_es_codec_cfg_t cfg = {
    .bus = s_i2c, .i2c_port = BOARD_I2C_PORT, .i2s_port = BOARD_I2S_PORT,
    .mclk = BOARD_I2S_MCLK, .bclk = BOARD_I2S_BCLK, .ws = BOARD_I2S_WS, .dout = BOARD_I2S_DOUT,
    .din = BOARD_I2S_DIN, .pa = BOARD_PA_EN, .mic_channel_mask = BOARD_MIC_CHANNEL_MASK,
    .mic_gain_db = BOARD_MIC_GAIN_DB, .volume = 80,
  };
  return drv_es_codec_init(&cfg, out);
}

esp_err_t board_buttons_init(void) {
  const gpio_config_t talk = {.pin_bit_mask = 1ULL << BOARD_BTN_TALK, .mode = GPIO_MODE_INPUT, .pull_up_en = GPIO_PULLUP_ENABLE};
  return gpio_config(&talk);
}

bool board_talk_pressed(void) { return gpio_get_level(BOARD_BTN_TALK) == 0; }
bool board_cancel_pressed(void) { return false; }
bool board_battery_read(gadget_battery_t *out) { return s_pmu && drv_axp2101_read(out); }

void board_set_brightness(uint8_t pct) {
  if (s_panel != NULL) {
    drv_co5300_set_brightness(s_panel, pct);
  }
}
```

`firmware/ports/esp32/boards/lcd-154/board.c`:

```c
/* SPDX-License-Identifier: Apache-2.0 */
/* Board glue for lcd-154 (main/board_api.h). */
#include "board.h"

#include "board_api.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "drv_es_codec.h"
#include "drv_lipo_adc.h"
#include "drv_st7789.h"
#include "esp_check.h"
#include "esp_log.h"

#define SCREEN 240

static const char *TAG = "board";
static i2c_master_bus_handle_t s_i2c;
static bool s_battery;

esp_err_t board_early_init(void) {
  /* First: hold the power latch, or the board turns off when PWR is released. */
  const gpio_config_t latch = {.pin_bit_mask = 1ULL << BOARD_BAT_EN, .mode = GPIO_MODE_OUTPUT};
  ESP_RETURN_ON_ERROR(gpio_config(&latch), TAG, "latch");
  ESP_RETURN_ON_ERROR(gpio_set_level(BOARD_BAT_EN, 1), TAG, "latch on");
  const i2c_master_bus_config_t bus = {
    .i2c_port = BOARD_I2C_PORT,
    .sda_io_num = BOARD_I2C_SDA,
    .scl_io_num = BOARD_I2C_SCL,
    .clk_source = I2C_CLK_SRC_DEFAULT,
    .glitch_ignore_cnt = 7,
    .flags.enable_internal_pullup = true,
  };
  ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bus, &s_i2c), TAG, "i2c bus");
  const drv_lipo_adc_cfg_t bat = {
    .adc_gpio = BOARD_BAT_ADC, .chg_gpio = BOARD_CHG_STAT, .divider_x1000 = BOARD_BAT_DIVIDER_X1000};
  s_battery = drv_lipo_adc_init(&bat) == ESP_OK;
  if (!s_battery) {
    ESP_LOGW(TAG, "battery ADC unavailable");
  }
  return ESP_OK;
}

esp_err_t board_display_init(esp_lcd_panel_io_handle_t *io, esp_lcd_panel_handle_t *panel) {
  const drv_st7789_cfg_t cfg = {
    .sclk = BOARD_LCD_SCLK, .mosi = BOARD_LCD_MOSI, .cs = BOARD_LCD_CS, .dc = BOARD_LCD_DC, .rst = BOARD_LCD_RST,
    .backlight = BOARD_LCD_BL, .spi_mode = BOARD_LCD_SPI_MODE, .pclk_hz = BOARD_LCD_PCLK_HZ,
    .x_gap = BOARD_LCD_X_GAP, .y_gap = BOARD_LCD_Y_GAP,
    .swap_xy = false, .mirror_x = false, .mirror_y = false, .invert = true,
    .max_transfer_bytes = SCREEN * BOARD_LCD_BUF_LINES * 2,
  };
  return drv_st7789_init(&cfg, io, panel);
}

esp_err_t board_touch_init(esp_lcd_touch_handle_t *tp) {
  (void)tp;
  return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t board_audio_init(board_audio_t *out) {
  const drv_es_codec_cfg_t cfg = {
    .bus = s_i2c, .i2c_port = BOARD_I2C_PORT, .i2s_port = BOARD_I2S_PORT,
    .mclk = BOARD_I2S_MCLK, .bclk = BOARD_I2S_BCLK, .ws = BOARD_I2S_WS, .dout = BOARD_I2S_DOUT,
    .din = BOARD_I2S_DIN, .pa = BOARD_PA_EN, .mic_channel_mask = BOARD_MIC_CHANNEL_MASK,
    .mic_gain_db = BOARD_MIC_GAIN_DB, .volume = 80,
  };
  return drv_es_codec_init(&cfg, out);
}

esp_err_t board_buttons_init(void) {
  const gpio_config_t keys = {
    .pin_bit_mask = (1ULL << BOARD_BTN_TALK) | (1ULL << BOARD_BTN_CANCEL),
    .mode = GPIO_MODE_INPUT,
    .pull_up_en = GPIO_PULLUP_ENABLE,
  };
  return gpio_config(&keys);
}

bool board_talk_pressed(void) { return gpio_get_level(BOARD_BTN_TALK) == 0; }
bool board_cancel_pressed(void) { return gpio_get_level(BOARD_BTN_CANCEL) == 0; }
bool board_battery_read(gadget_battery_t *out) { return s_battery && drv_lipo_adc_read(out); }
void board_set_brightness(uint8_t pct) { drv_st7789_set_brightness(pct); }
```

`firmware/ports/esp32/boards/devkit/board.c`:

```c
/* SPDX-License-Identifier: Apache-2.0 */
/* Board glue for devkit (main/board_api.h). */
#include "board.h"

#include "board_api.h"
#include "driver/gpio.h"
#include "drv_i2s_simplex.h"
#include "drv_st7789.h"
#include "esp_check.h"

#define SCREEN_W 320

static const char *TAG = "board";

esp_err_t board_early_init(void) { return ESP_OK; }

esp_err_t board_display_init(esp_lcd_panel_io_handle_t *io, esp_lcd_panel_handle_t *panel) {
  const drv_st7789_cfg_t cfg = {
    .sclk = BOARD_LCD_SCLK, .mosi = BOARD_LCD_MOSI, .cs = BOARD_LCD_CS, .dc = BOARD_LCD_DC, .rst = BOARD_LCD_RST,
    .backlight = BOARD_LCD_BL, .spi_mode = BOARD_LCD_SPI_MODE, .pclk_hz = BOARD_LCD_PCLK_HZ,
    .x_gap = BOARD_LCD_X_GAP, .y_gap = BOARD_LCD_Y_GAP,
    .swap_xy = BOARD_LCD_SWAP_XY, .mirror_x = BOARD_LCD_MIRROR_X, .mirror_y = BOARD_LCD_MIRROR_Y, .invert = true,
    .max_transfer_bytes = SCREEN_W * BOARD_LCD_BUF_LINES * 2,
  };
  return drv_st7789_init(&cfg, io, panel);
}

esp_err_t board_touch_init(esp_lcd_touch_handle_t *tp) {
  (void)tp;
  return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t board_audio_init(board_audio_t *out) {
  const drv_i2s_simplex_cfg_t cfg = {
    .mic_port = BOARD_MIC_I2S_PORT, .mic_bclk = BOARD_MIC_BCLK, .mic_ws = BOARD_MIC_WS, .mic_din = BOARD_MIC_SD,
    .spk_port = BOARD_SPK_I2S_PORT, .spk_bclk = BOARD_SPK_BCLK, .spk_ws = BOARD_SPK_LRC, .spk_dout = BOARD_SPK_DIN,
    .spk_rate = BOARD_SPK_RATE, .mic_shift = BOARD_MIC_SHIFT,
  };
  return drv_i2s_simplex_init(&cfg, out);
}

esp_err_t board_buttons_init(void) {
  const gpio_config_t keys = {
    .pin_bit_mask = (1ULL << BOARD_BTN_TALK) | (1ULL << BOARD_BTN_CANCEL),
    .mode = GPIO_MODE_INPUT,
    .pull_up_en = GPIO_PULLUP_ENABLE,
  };
  ESP_RETURN_ON_ERROR(gpio_config(&keys), TAG, "buttons");
  return ESP_OK;
}

bool board_talk_pressed(void) { return gpio_get_level(BOARD_BTN_TALK) == 0; }
bool board_cancel_pressed(void) { return gpio_get_level(BOARD_BTN_CANCEL) == 0; }

bool board_battery_read(gadget_battery_t *out) {
  (void)out;
  return false;
}

void board_set_brightness(uint8_t pct) { drv_st7789_set_brightness(pct); }
```

In `firmware/ports/esp32/main/CMakeLists.txt`, replace the `set(port_srcs ...)` block with:

```cmake
set(port_srcs
  main.c
  port_events.c
  hal_system.c
  console_usj.c
  hal_storage.c
  hal_wifi.c
  hal_ws.c
  hal_mdns.c
  hal_audio.c
  hal_input.c
  hal_battery.c
  hal_ota.c
  display.c
  "${board_dir}/board.c")
```

- [ ] **Step 2: Build all four boards**

Run:

```bash
. ~/.espressif/tools/activate_idf_v6.0.3.sh
SKIP_ART_CHECK=1 firmware/ports/esp32/tools/build-all.sh
```

Expected, for each of `amoled-175`, `amoled-175c`, `devkit`, `lcd-154` (alphabetical): `== <board>` followed by `check-size: <board> ok, <n> of 6291456 bytes (<p>%)`; exit status 0. Each board compiles only its own `board.c` and the drivers its `board.cmake` lists (a board without `board.cmake` would compile all of them).

- [ ] **Step 3: Commit**

```bash
git add firmware/ports/esp32/boards \
  firmware/ports/esp32/main/CMakeLists.txt
git commit -m "feat(esp32): board glue for amoled-175c, amoled-175, lcd-154 and devkit"
```
### Task 13: The main loop, the full link and the art-profile check

**Files:**
- Create: `firmware/ports/esp32/tools/check-art-profile.sh`, `firmware/ports/esp32/tools/test-check-art-profile.sh`
- Modify: `firmware/ports/esp32/main/main.c` (replace the smoke app), `firmware/ports/esp32/main/CMakeLists.txt` (final: `WHOLE_ARCHIVE`)

**Interfaces:**
- Consumes: everything above; `core_init(const core_config_t*)` with `board`, `fw_version = esp_app_get_description()->version`, `prng_seed = 0`, `probation_ms = 0` (contract §2.7); `core_event`, `core_tick`, `core_ui_model`; `ui_init(board, seed)`, `ui_render`, `ui_tick` (§2.9); boot order of §2.17: `board_early_init()` → NVS init → `psa_crypto_init()` → Wi-Fi start (STA) → display/touch/audio/buttons → `core_init()` → `ui_init()` → the 10 ms loop.
- Produces: the firmware image; `tools/check-art-profile.sh <board> [build-dir]` (exit 0 only when the ELF defines `maus_<profile>_body` and no `maus_<other profile>_*` symbol; `NM` overrides the symbol lister). Core calls the `hal_*` functions defined in `main/`; `WHOLE_ARCHIVE` links every object of the component so they resolve whatever the link order (ESP-IDF build-system docs, component property `WHOLE_ARCHIVE`).

- [ ] **Step 1: Write the failing test for the art-profile check**

`firmware/ports/esp32/tools/test-check-art-profile.sh`:

```bash
#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# Tests for check-art-profile.sh with a fake nm and fake build directories.
set -uo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

cat >"$tmp/fake-nm" <<'NM'
#!/usr/bin/env bash
cat "$1.syms"
NM
chmod +x "$tmp/fake-nm"

make_build() { # <dir> <profile> <symbol lines...>
  local dir="$1" profile="$2"
  shift 2
  mkdir -p "$dir"
  : >"$dir/openmausbot-gadget.elf"
  printf 'CONFIG_GADGET_ART_PROFILE="%s"\n' "$profile" >"$dir/sdkconfig"
  printf '%s\n' "$@" >"$dir/openmausbot-gadget.elf.syms"
}
expect() { # <want exit> <name> <args...>
  local want="$1" name="$2"
  shift 2
  NM="$tmp/fake-nm" "$here/check-art-profile.sh" "$@" >"$tmp/out" 2>&1
  local got=$?
  if [[ "$got" != "$want" ]]; then
    echo "FAIL $name: exit $got, want $want"
    cat "$tmp/out"
    exit 1
  fi
  echo "ok $name"
}

make_build "$tmp/good" s240 "3c0a1000 R maus_s240_body" "3c0a2000 R maus_s240_eye_6_0" "42001000 T app_main"
expect 0 "only the board's profile" amoled-175c "$tmp/good"

make_build "$tmp/both" s150 "3c0a1000 R maus_s150_body" "3c0b1000 R maus_s240_body"
expect 1 "both profiles linked" lcd-154 "$tmp/both"

make_build "$tmp/none" s150 "42001000 T app_main"
expect 1 "own profile missing" devkit "$tmp/none"

make_build "$tmp/noprofile" "" "3c0a1000 R maus_s240_body"
expect 1 "no profile in sdkconfig" amoled-175 "$tmp/noprofile"
echo "all check-art-profile tests passed"
```

Run: `chmod +x firmware/ports/esp32/tools/test-check-art-profile.sh && firmware/ports/esp32/tools/test-check-art-profile.sh`

Expected: FAIL — `FAIL only the board's profile: exit 127, want 0` followed by `...check-art-profile.sh: No such file or directory`.

- [ ] **Step 2: Write the art-profile check**

`firmware/ports/esp32/tools/check-art-profile.sh`:

```bash
#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# Usage: tools/check-art-profile.sh <board> [build-dir]
# An ESP32 image links only its board's Maus art profile (contract §2.15):
# the ELF must define maus_<profile>_body and no maus_<other>_* symbol.
# NM overrides the symbol lister (default: xtensa-esp32s3-elf-nm).
set -euo pipefail

board="${1:?usage: check-art-profile.sh <board> [build-dir]}"
esp_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="${2:-$esp_dir/build/$board}"
nm_tool="${NM:-xtensa-esp32s3-elf-nm}"
elf="$build_dir/openmausbot-gadget.elf"
cfg="$build_dir/sdkconfig"

[[ -f "$elf" ]] || { echo "check-art-profile: $elf not found" >&2; exit 1; }
[[ -f "$cfg" ]] || { echo "check-art-profile: $cfg not found" >&2; exit 1; }
profile=$(sed -n 's/^CONFIG_GADGET_ART_PROFILE="\(s[0-9]*\)"$/\1/p' "$cfg")
case "$profile" in
  s240) other=s150 ;;
  s150) other=s240 ;;
  *) echo "check-art-profile: no CONFIG_GADGET_ART_PROFILE in $cfg" >&2; exit 1 ;;
esac

symbols=$("$nm_tool" "$elf")
if ! grep -q " maus_${profile}_body\$" <<<"$symbols"; then
  echo "check-art-profile: $board does not link maus_${profile}_body" >&2
  exit 1
fi
stray=$(grep -c " maus_${other}_" <<<"$symbols" || true)
if [[ "$stray" != "0" ]]; then
  echo "check-art-profile: $board links $stray maus_${other}_* symbols; only $profile belongs in this image" >&2
  exit 1
fi
echo "check-art-profile: $board links only $profile"
```

Run: `chmod +x firmware/ports/esp32/tools/check-art-profile.sh && firmware/ports/esp32/tools/test-check-art-profile.sh`

Expected:

```text
ok only the board's profile
ok both profiles linked
ok own profile missing
ok no profile in sdkconfig
all check-art-profile tests passed
```

- [ ] **Step 3: Write the real entry point and the final component file**

`firmware/ports/esp32/main/main.c`:

```c
/* SPDX-License-Identifier: Apache-2.0 */
/* ESP32 entry point. app_main brings the board up in the order of contract
 * §2.17 (board, NVS, PSA, Wi-Fi), then the "gadget" task brings up display,
 * touch, audio and buttons, starts core and the UI, and runs the one loop:
 * every 10 ms deliver queued events, core_tick, ui_render, ui_tick. */
#include <inttypes.h>
#include <stdio.h>

#include "board_api.h"
#include "esp_app_desc.h"
#include "esp_flash.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "gadget_core.h"
#include "gadget_hal.h"
#include "gadget_ui.h"
#include "nvs_flash.h"
#include "port.h"
#include "psa/crypto.h"
#include "sdkconfig.h"

#define LOOP_PERIOD_MS 10
#define GADGET_TASK_STACK 16384
#define GADGET_TASK_PRIO 4
#define GADGET_TASK_CORE 1
#define BOOT_BRIGHTNESS_PCT 80

static const char *TAG = "main";

static void log_flash_size(void) {
  uint32_t bytes = 0;
  if (esp_flash_get_physical_size(esp_flash_default_chip, &bytes) == ESP_OK) {
    ESP_LOGI(TAG, "flash: %" PRIu32 " MB fitted, 16 MB used", bytes / (1024u * 1024u));
  }
}

static esp_err_t nvs_start(void) {
  esp_err_t err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_LOGW(TAG, "NVS unreadable (%s): erasing it; the gadget must pair again", esp_err_to_name(err));
    ESP_ERROR_CHECK(nvs_flash_erase());
    err = nvs_flash_init();
  }
  return err;
}

static _Noreturn void fail_restart(const char *what, int code) {
  ESP_LOGE(TAG, "%s failed (%d); restarting in 10 s", what, code);
  vTaskDelay(pdMS_TO_TICKS(10000));
  esp_restart();
}

static void gadget_task(void *arg) {
  (void)arg;
  const gadget_board_t *board = port_board();
  port_set_main_task();

  esp_lcd_panel_io_handle_t io = NULL;
  esp_lcd_panel_handle_t panel = NULL;
  ESP_ERROR_CHECK(board_display_init(&io, &panel));
  esp_lcd_touch_handle_t touch = NULL;
  esp_err_t err = board_touch_init(&touch);
  if (err != ESP_OK) {
    touch = NULL;
    if (err != ESP_ERR_NOT_SUPPORTED) {
      ESP_LOGE(TAG, "touch: %s; continuing without touch", esp_err_to_name(err));
    }
  }
  ESP_ERROR_CHECK(port_display_init(board, io, panel, touch != NULL));
  board_set_brightness(BOOT_BRIGHTNESS_PCT);

  board_audio_t audio = {0};
  err = board_audio_init(&audio);
  if (err == ESP_OK) {
    err = port_audio_start(&audio);
  }
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "audio: %s; continuing without mic and speaker", esp_err_to_name(err));
  }
  ESP_ERROR_CHECK(board_buttons_init());
  port_input_init(board, touch);
  ESP_ERROR_CHECK(port_ws_init());
  ESP_ERROR_CHECK(port_mdns_init());
  ESP_ERROR_CHECK(port_ota_init());

  const core_config_t cfg = {
    .board = board,
    .fw_version = esp_app_get_description()->version,
    .prng_seed = 0,
    .fail_probation = false,
    .default_name = NULL,
    .probation_ms = 0,
  };
  gadget_status_t st = core_init(&cfg);
  if (st != GADGET_OK) {
    fail_restart("core_init", (int)st);
  }
  st = ui_init(board, esp_random());
  if (st != GADGET_OK) {
    fail_restart("ui_init", (int)st);
  }

  TickType_t last = xTaskGetTickCount();
  for (;;) {
    uint64_t now = hal_now_ms();
    port_drain();
    port_wifi_tick(now);
    port_input_poll();
    core_tick(now);
    ui_render(core_ui_model());
    ui_tick(now);
    vTaskDelayUntil(&last, pdMS_TO_TICKS(LOOP_PERIOD_MS));
  }
}

void app_main(void) {
  ESP_ERROR_CHECK(board_early_init());
  ESP_ERROR_CHECK(port_system_init());
  const gadget_board_t *board = gadget_board_by_id(CONFIG_GADGET_BOARD_ID);
  if (board == NULL) {
    ESP_LOGE(TAG, "CONFIG_GADGET_BOARD_ID \"%s\" is not in core's board table", CONFIG_GADGET_BOARD_ID);
    abort();
  }
  port_set_board(board);
  ESP_ERROR_CHECK(port_events_init());
  ESP_ERROR_CHECK(port_console_start());
  ESP_LOGI(TAG, "%s, firmware %s", board->display_name, esp_app_get_description()->version);
  log_flash_size();
  ESP_ERROR_CHECK(nvs_start());
  psa_status_t ps = psa_crypto_init();
  if (ps != PSA_SUCCESS) {
    fail_restart("psa_crypto_init", (int)ps);
  }
  ESP_ERROR_CHECK(port_wifi_start());
  if (xTaskCreatePinnedToCore(gadget_task, "gadget", GADGET_TASK_STACK, NULL, GADGET_TASK_PRIO, NULL,
                              GADGET_TASK_CORE) != pdPASS) {
    fail_restart("gadget task", 0);
  }
}
```

`firmware/ports/esp32/main/CMakeLists.txt`:

```cmake
# SPDX-License-Identifier: Apache-2.0
# The ESP32 port: HAL implementations, the main loop, LVGL glue, the pure
# logic shared with host-tests/ (logic/), the board's own glue
# (boards/<board>/board.c) and the drivers its optional boards/<board>/board.cmake
# lists (every driver in drivers/ when it has none).
idf_build_get_property(gadget_board GADGET_BOARD)
set(board_dir "${CMAKE_CURRENT_LIST_DIR}/../boards/${gadget_board}")
set(BOARD_DRIVER_SRCS "")
# board.cmake is optional (a size optimisation): without it every shared driver
# compiles, and --gc-sections drops the ones the board never calls.
include("${board_dir}/board.cmake" OPTIONAL RESULT_VARIABLE board_cmake)
if(NOT board_cmake)
  file(GLOB BOARD_DRIVER_SRCS RELATIVE "${CMAKE_CURRENT_LIST_DIR}/drivers" "${CMAKE_CURRENT_LIST_DIR}/drivers/*.c")
endif()
list(TRANSFORM BOARD_DRIVER_SRCS PREPEND "drivers/")

set(port_srcs
  main.c
  port_events.c
  hal_system.c
  console_usj.c
  hal_storage.c
  hal_wifi.c
  hal_ws.c
  hal_mdns.c
  hal_audio.c
  hal_input.c
  hal_battery.c
  hal_ota.c
  display.c
  "${board_dir}/board.c")

set(logic_srcs
  logic/pl_event.c
  logic/pl_util.c
  logic/pl_scan.c
  logic/pl_wifi.c
  logic/pl_wsasm.c
  logic/pl_mdns.c
  logic/pl_audio.c
  logic/pl_input.c
  logic/pl_power.c
  logic/pl_display.c
  logic/pl_otaq.c)

set(port_include_dirs "." "logic" "drivers" "${board_dir}")

idf_component_register(
  SRCS ${port_srcs} ${logic_srcs} ${BOARD_DRIVER_SRCS}
  INCLUDE_DIRS ${port_include_dirs}
  REQUIRES core ui lvgl__lvgl espressif__cjson mbedtls nvs_flash esp_wifi esp_netif esp_event esp_timer
           esp_driver_gpio esp_driver_i2c esp_driver_i2s esp_driver_spi esp_driver_ledc esp_driver_usb_serial_jtag
           esp_adc esp_lcd app_update spi_flash esp_app_format
           espressif__esp_websocket_client espressif__mdns espressif__esp_codec_dev
           espressif__esp_lcd_co5300 waveshare__esp_lcd_touch_cst9217 espressif__esp_lcd_touch
  # core calls the hal_* functions defined here; link every object of this
  # component so those symbols resolve whatever the link order.
  WHOLE_ARCHIVE)

if(NOT CMAKE_BUILD_EARLY_EXPANSION AND NOT "${CONFIG_GADGET_BOARD_ID}" STREQUAL "${gadget_board}")
  message(FATAL_ERROR "sdkconfig says board '${CONFIG_GADGET_BOARD_ID}' but GADGET_BOARD is '${gadget_board}'. "
                      "Use -D SDKCONFIG=build/${gadget_board}/sdkconfig (one sdkconfig per board).")
endif()
```

- [ ] **Step 4: Build and link all four boards, with both checks**

Run:

```bash
. ~/.espressif/tools/activate_idf_v6.0.3.sh
firmware/ports/esp32/tools/build-all.sh
```

Expected, for each board: `== <board>`, `check-size: <board> ok, <n> of 6291456 bytes (<p>%)`, then `check-art-profile: <board> links only s240` (amoled-175c, amoled-175) or `... only s150` (lcd-154, devkit); exit status 0. `grep -m1 'Component idf::main will be linked with -Wl,--whole-archive' firmware/ports/esp32/build/amoled-175c.log` prints `-- Component idf::main will be linked with -Wl,--whole-archive` (ESP-IDF's own confirmation of `WHOLE_ARCHIVE`; v6.0.3's `project.cmake` prints the component alias, `idf::<name>`). A link error `undefined reference to 'hal_...'` means a HAL function is missing from `main/`; `undefined reference to 'ui_...'` or `maus_...` points at P2b's ESP branch (stop and report).

- [ ] **Step 5: Check that the component defines every port HAL function exactly once**

The link in Step 4 already proves that every HAL function core calls exists. This step also catches a function that is defined twice or missing but not yet called; it reads the component archive, before the linker drops unused functions:

```bash
cd firmware/ports/esp32
for f in $(grep -oE '\bhal_[a-z0-9_]+\(' ../../core/include/gadget_hal.h | tr -d '(' | sort -u | grep -v '^hal_crypto_'); do
  n=$(xtensa-esp32s3-elf-nm --defined-only build/amoled-175c/esp-idf/main/libmain.a | grep -c " T $f$")
  [ "$n" = 1 ] || echo "PROBLEM $f: $n definitions"
done; echo "hal symbol check done"
idf.py -B build/amoled-175c -D GADGET_BOARD=amoled-175c -D SDKCONFIG=build/amoled-175c/sdkconfig size | grep -i 'total image size'
cd ../../..
```

Expected: only `hal symbol check done` from the loop (no `PROBLEM` line; the `hal_crypto_*` group is core's and is skipped), and a `Total image size` line well under 6291456 bytes.

- [ ] **Step 6: Commit**

```bash
git add firmware/ports/esp32/main/main.c \
  firmware/ports/esp32/main/CMakeLists.txt \
  firmware/ports/esp32/tools/check-art-profile.sh \
  firmware/ports/esp32/tools/test-check-art-profile.sh
git commit -m "feat(esp32): gadget task main loop and full link with the art-profile check"
```
### Task 14: CI jobs `esp32` and `esp32-idf55`

**Files:**
- Modify: `.github/workflows/ci.yml` (append two jobs under `jobs:`; contract §1.6 — never edit another plan's job)

**Interfaces:**
- Consumes: `tools/build-all.sh` (the amoled-175c leg runs it, so CI builds all four boards one after another in one checkout, as spec §10 asks); `tools/check-size.sh`, `tools/check-art-profile.sh`, the two script tests, the host-test project, `dependencies.lock.<board>`.
- Produces: job `esp32` (container `espressif/idf:v6.0.3`, the contract §1.6 matrix of the four boards plus an `nvs-encrypt` build of amoled-175c, `. $IDF_PATH/export.sh` in every step because container jobs skip the image entrypoint, app ≤ slot and art-profile checks, committed lock unchanged and no untracked lock, host tests and script tests once). Its amoled-175c/plain leg runs `tools/build-all.sh` instead of the single-board build: amoled-175, amoled-175c, devkit and lcd-154 one after another in one tree (shared `managed_components/`, four lock files, four `build/<board>` directories, each with its own sdkconfig), with both checks per board. That is spec §10's sequential one-checkout build, the check that proves the per-board sdkconfig and lock rules of spec §5.1. The same leg then runs Review Focus 1's two foreign-sdkconfig cases against the board guard. The other three plain legs and the `nvs-encrypt` leg each build one board; job `esp32-idf55` (container `espressif/idf:v5.5.5`, `continue-on-error: true`, relaxes the manifest's `idf` range to `>=5.5.5,<6.1` and drops the lock before building amoled-175c). The `variant` matrix dimension is what makes the `include` entry a fifth job instead of a change to the plain amoled-175c job.

- [ ] **Step 1: Look at the end of the workflow**

Run: `tail -n 15 .github/workflows/ci.yml && grep -n '^jobs:' .github/workflows/ci.yml`

Expected: `jobs:` is the last top-level key and the file ends inside the last job (P1's, P2a's and P2b's jobs). If another top-level key follows `jobs:`, insert the block below at the end of the `jobs:` map instead of the end of the file.

- [ ] **Step 2: Append the two jobs**

```bash
cat >> .github/workflows/ci.yml <<'EOF'

  esp32:
    runs-on: ubuntu-24.04
    container: espressif/idf:v6.0.3
    strategy:
      fail-fast: false
      matrix:
        board: [amoled-175c, amoled-175, lcd-154, devkit]
        variant: [plain]
        include:
          - board: amoled-175c
            variant: nvs-encrypt
    defaults:
      run:
        shell: bash
    steps:
      - uses: actions/checkout@v4
      - name: Port logic host tests and script tests
        if: matrix.board == 'amoled-175c' && matrix.variant == 'plain'
        run: |
          . "$IDF_PATH/export.sh"
          cmake -S firmware/ports/esp32/host-tests -B build/esp32-host
          cmake --build build/esp32-host -j"$(nproc)"
          ctest --test-dir build/esp32-host --output-on-failure
          firmware/ports/esp32/tools/test-check-size.sh
          firmware/ports/esp32/tools/test-check-art-profile.sh
      # Spec §10: all four boards one after another in one checkout, each with
      # its own sdkconfig, plus check-size.sh and check-art-profile.sh per board.
      - name: All four boards in one checkout (tools/build-all.sh)
        if: matrix.board == 'amoled-175c' && matrix.variant == 'plain'
        run: |
          . "$IDF_PATH/export.sh"
          git config --global --add safe.directory '*'
          cd firmware/ports/esp32 && tools/build-all.sh
      # Review Focus 1: a foreign sdkconfig stops the configure, both as a
      # copied file and as amoled-175c's own build directory reused for lcd-154.
      - name: Board guard refuses another board's sdkconfig
        if: matrix.board == 'amoled-175c' && matrix.variant == 'plain'
        run: |
          . "$IDF_PATH/export.sh"
          git config --global --add safe.directory '*'
          cd firmware/ports/esp32
          want="sdkconfig says board 'amoled-175c' but GADGET_BOARD is 'lcd-154'"
          mkdir -p build/guard-mix
          cp build/amoled-175c/sdkconfig build/guard-mix/sdkconfig
          if idf.py -B build/guard-mix -D GADGET_BOARD=lcd-154 -D SDKCONFIG=build/guard-mix/sdkconfig reconfigure >build/guard-mix.log 2>&1; then
            echo "the board guard accepted amoled-175c's sdkconfig for lcd-154" >&2
            exit 1
          fi
          grep -qF "$want" build/guard-mix.log
          if idf.py -B build/amoled-175c -D GADGET_BOARD=lcd-154 reconfigure >build/guard-reuse.log 2>&1; then
            echo "the board guard accepted build/amoled-175c for lcd-154" >&2
            exit 1
          fi
          grep -qF "$want" build/guard-reuse.log
      # build-all.sh already built and checked amoled-175c in its plain leg.
      - name: Build ${{ matrix.board }} (${{ matrix.variant }})
        if: matrix.board != 'amoled-175c' || matrix.variant != 'plain'
        run: |
          . "$IDF_PATH/export.sh"
          git config --global --add safe.directory '*'
          cd firmware/ports/esp32
          dir="build/${{ matrix.board }}"
          extra=""
          if [ "${{ matrix.variant }}" = "nvs-encrypt" ]; then
            dir="build/${{ matrix.board }}-nvs-encrypt"
            extra="-D GADGET_NVS_ENCRYPT=1"
          fi
          idf.py -B "$dir" -D GADGET_BOARD=${{ matrix.board }} -D SDKCONFIG="$dir/sdkconfig" $extra build
          tools/check-size.sh ${{ matrix.board }} "$dir"
          tools/check-art-profile.sh ${{ matrix.board }} "$dir"
      # Contract §1.4: git diff ignores a file that was never committed, so a
      # missing lock regenerated by the build is caught by git status.
      - name: Committed dependency lock is current
        if: matrix.variant == 'plain'
        run: |
          git config --global --add safe.directory '*'
          git diff --exit-code -- firmware/ports/esp32/dependencies.lock.${{ matrix.board }} && test -z "$(git status --porcelain -- firmware/ports/esp32/dependencies.lock.${{ matrix.board }})"

  esp32-idf55:
    runs-on: ubuntu-24.04
    container: espressif/idf:v5.5.5
    continue-on-error: true
    defaults:
      run:
        shell: bash
    steps:
      - uses: actions/checkout@v4
      - name: Build amoled-175c on ESP-IDF v5.5.5
        run: |
          . "$IDF_PATH/export.sh"
          git config --global --add safe.directory '*'
          cd firmware/ports/esp32
          sed -i 's/idf: ">=6.0.3,<6.1"/idf: ">=5.5.5,<6.1"/' main/idf_component.yml
          rm -f dependencies.lock.amoled-175c
          idf.py -B build/amoled-175c -D GADGET_BOARD=amoled-175c -D SDKCONFIG=build/amoled-175c/sdkconfig build
          tools/check-size.sh amoled-175c
EOF
```

- [ ] **Step 3: Check the workflow parses and has the jobs**

```bash
ruby -ryaml -e 'j = YAML.load_file(".github/workflows/ci.yml")["jobs"]; abort("esp32 jobs missing") unless j.key?("esp32") && j.key?("esp32-idf55"); m = j["esp32"]["strategy"]["matrix"]; puts j.keys.join(" "); puts m["board"].join(" ") + " | " + m["include"].inspect; puts j["esp32-idf55"]["continue-on-error"]; j["esp32"]["steps"].each { |s| puts "#{s["name"] || s["uses"]} <- #{s["if"] || "always"}" }'
```

Expected: the first line lists every job id including `esp32 esp32-idf55` (with P1, P2a and P2b's `protocol fake-host host-c ui art-drift` before them); the second `amoled-175c amoled-175 lcd-154 devkit | [{"board" => "amoled-175c", "variant" => "nvs-encrypt"}]`; the third `true`; then the `esp32` steps with their conditions:

```text
actions/checkout@v4 <- always
Port logic host tests and script tests <- matrix.board == 'amoled-175c' && matrix.variant == 'plain'
All four boards in one checkout (tools/build-all.sh) <- matrix.board == 'amoled-175c' && matrix.variant == 'plain'
Board guard refuses another board's sdkconfig <- matrix.board == 'amoled-175c' && matrix.variant == 'plain'
Build ${{ matrix.board }} (${{ matrix.variant }}) <- matrix.board != 'amoled-175c' || matrix.variant != 'plain'
Committed dependency lock is current <- matrix.variant == 'plain'
```

So the amoled-175c/plain leg runs `build-all.sh` (all four boards, one checkout) and the guard checks, and every other leg, including amoled-175c's `nvs-encrypt` leg, runs the single-board build.

- [ ] **Step 4: Commit**

```bash
git add .github/workflows/ci.yml
git commit -m "ci: build all four ESP32 boards on ESP-IDF v6.0.3, optional v5.5.5 leg"
```
### Task 15: Hardware checklist

**Files:**
- Create: `docs/hardware-checklist.md` (contract §5.1, owner P2c)

**Interfaces:**
- Consumes: spec §10 "Hardware" row (boot log, touch and buttons, PWR hold, mic level, speaker, battery including the lcd-154 power latch, installer reflash keeping the pairing, pairing, a voice turn, an approval, OTA, power loss during OTA, rollback); the device checks P2a's and P2b's hand-off notes route here (spec §5.4's 60 s limit and countdown, touch hold/tap/swipe, the 1 s jitter buffer, the speaking mouth, white A8 eyes, Latin-1 glyphs with `…` and `→`); `firmware/ui/README.md`'s "Checks that need hardware" (P2b) and `docs/installer-checklist.md` (P2d), both linked; the fake-host CLI and commands (contract §4.7); the devkit USB-port rule (§2.17).
- Produces: the per-board manual checklist that every release and every change to `firmware/ports/esp32/` runs on hardware; P2d's AGENTS.md and README may link it.

- [ ] **Step 1: Write the checklist**

`docs/hardware-checklist.md`:

````markdown
<!-- SPDX-License-Identifier: Apache-2.0 -->
# Hardware checklist

The ESP32 port is built and unit-tested in CI, but nothing in CI runs on a
real board. Run this checklist on each board before a release, and after any
change to `firmware/ports/esp32/`. Record the result per board in the PR or
release notes (copy the table at the end).

Boards: `amoled-175c` (Waveshare ESP32-S3-Touch-AMOLED-1.75C), `amoled-175`
(ESP32-S3-Touch-AMOLED-1.75), `lcd-154` (ESP32-S3-LCD-1.54), `devkit`
(ESP32-S3-DevKitC-1-N16R8 breadboard build).

## What you need

- The board, a data-capable USB-C cable, and for the devkit the parts and
  wiring in `firmware/ports/esp32/boards/devkit/board.h` (2" ST7789 module,
  INMP441 microphone, MAX98357A amplifier with a small speaker, a push button
  from GPIO1 to GND).
- ESP-IDF v6.0.3 active in your shell:
  `. ~/.espressif/tools/activate_idf_v6.0.3.sh`. To install it on a Mac,
  install EIM's prerequisites first (EIM checks them but does not install
  them): `brew install libgcrypt glib pixman sdl2 libslirp dfu-util ninja && brew tap espressif/eim && brew install eim && eim install -i v6.0.3 -t esp32s3`
- Either MausBot with **Remote access** on (Settings → Remote access), or the
  fake host from this repository on the same Wi-Fi (run `npm ci` at the
  repository root once):
  `node tools/fake-host/src/main.ts --bind 0.0.0.0 --port 8810 --code 123456`
  (commands are JSON lines on its stdin, for example `{"cmd":"ask","kind":"permission","title":"Run a command?","body":"ls"}`).
- A 2.4 GHz Wi-Fi network. Note the Mac's LAN address (`ipconfig getifaddr en0`).

Related checklists: [docs/installer-checklist.md](installer-checklist.md)
covers the browser installer (separate parts at their flasher offsets, the
reset into the app, USB re-enumeration, "Erase everything"); row 21 below
repeats its reinstall check because it is part of every board's run. The
on-device UI checks live in
[firmware/ui/README.md](../firmware/ui/README.md), section "Checks that need
hardware"; row 28 runs them.

**devkit:** plug the cable into the USB-C port labelled **USB** (native
USB-Serial-JTAG, VID/PID 0x303A/0x1001). The port labelled **UART** goes
through a bridge chip; the console and the installer do not work on it.

## Build, flash, monitor

From `firmware/ports/esp32/`:

```
idf.py -B build/<board> -D GADGET_BOARD=<board> -D SDKCONFIG=build/<board>/sdkconfig build
idf.py -B build/<board> -D GADGET_BOARD=<board> -D SDKCONFIG=build/<board>/sdkconfig -p /dev/cu.usbmodem* flash monitor
```

`flash` writes the bootloader, partition table, `ota_data_initial` and the
app; it does not touch NVS, so the identity key, Wi-Fi and pairing survive.
`idf.py erase-flash` wipes everything (the gadget must pair again). Leave
the monitor with `Ctrl-]`. Type console commands into the monitor.

## Checks (every board)

| # | Check | How | Expected |
|---|---|---|---|
| 1 | Boot log | Flash, watch the monitor from reset | `@omb {"op":"boot","board":"<board>",...}` with the right board id; a `flash: N MB fitted, 16 MB used` line; no panic or watchdog reset in the first minute |
| 2 | Display | Look at the screen after boot | The green Maus is drawn upright, colours right (green body, not purple or inverted), no stripe of garbage or offset band at any edge. amoled boards: the circle is centred. amoled boards dark: in `boards/<board>/board.c` pass `.init_cmds = NULL, .init_cmds_size = 0` to `drv_co5300_init` (the component's default table instead of `BOARD_CO5300_INIT_CMDS`), rebuild, and report which table works |
| 3 | Brightness | Watch the first second after boot | The screen comes up at a readable brightness (80 %), no flicker |
| 4 | Console status | Type `status` | One `@omb {"op":"status",...}` line with `"board":"<board>"`, `"fw":"0.0.0-dev"` and `"wifi":"off"` before Wi-Fi is set |
| 5 | Console scan | Type `scan` | One `@omb {"op":"scan","networks":[...]}` line listing nearby networks, strongest first, at most 20 |
| 6 | Quoting | `wifi "My Net" "pass word"` with a network whose name or password has a space (or type it and check the stored values with `status`) | `status` shows `"ssid":"My Net"`; no `@omb error` |
| 7 | Wrong Wi-Fi password | `wifi "<your ssid>" "wrongpassword"`, wait 15 s, then `scan` | `status` shows `"wifi":"failed"`; `scan` still prints networks |
| 8 | Wi-Fi | `wifi "<ssid>" "<password>"` | Within 10 s `status` shows `"wifi":"connected"`; restart the router: the gadget reconnects by itself |
| 9 | log off | `log off`, then `status` | No more `I (...)` log lines; the `@omb status` line still prints. `log on` restores logs |
| 10 | Long line | Paste a line longer than 8192 characters | `@omb {"op":"error","cmd":"","message":"line too long"}`; the next command works |
| 11 | Pair | Open a pairing code (MausBot: Settings → Remote access → Pair a gadget; fake host: `{"cmd":"code","code":"123456"}`), then `pair <code>` and `host auto` (or `host <mac-ip>:8810`) | `status` reaches `"pair":"paired"` with `host_name`; the Idle screen shows the bot name |
| 12 | mDNS | After pairing, `host auto`, `reboot` | It reconnects to the same host without `host <ip>`. On Windows hosts this may fail (spec §6.5); `host <ip>` then works |
| 13 | TALK button | Hold TALK (BOOT) for 2 s while paired, release | Listening screen while held, then Thinking/Reply; a press under 300 ms does nothing |
| 14 | Mic level | Hold TALK and speak | The level ring follows your voice; silence keeps it low. If it stays flat, set `BOARD_MIC_CHANNEL_MASK` to `0x2` in `board.h` and rebuild (codec boards) |
| 15 | Speaker | Fake host: let a voice turn finish (`--tone-ms 800`); MausBot: a voice reply | The tone or the reply is clearly audible, no distortion at 80 % volume; no loud hiss when idle |
| 16 | Barge-in | Press TALK while the reply plays | Playback stops at once and listening starts |
| 17 | Approval | Fake host `{"cmd":"ask","kind":"permission","title":"Run?","body":"ls"}` | Touch boards: tap Allow or Deny, the host logs `answer`. Button boards: TALK = Allow, CANCEL = Deny. Presses in the first 0.6 s are ignored |
| 18 | Post | Fake host `{"cmd":"post","kind":"message","text":"Build finished","speak":true}` | Toast with a chime over the current screen; spoken when `speak` |
| 19 | Reflash keeps pairing | `idf.py ... flash` again (no erase) | After boot `status` shows `"pair":"paired"` again without a new code |
| 20 | Forget | `forget` | The board restarts, `status` shows `"pair":"unpaired"` and a new `id`; MausBot still lists the old entry until you Remove it |
| 21 | Installer reinstall | Reinstall from the browser installer without Erase everything, then with it | Without: `status` shows `"pair":"paired"` and the same `id`. With Erase everything: `"pair":"unpaired"` and a new `id` |
| 22 | 60 s limit | Hold TALK for 65 s while paired | A countdown 5…1 shows in the last 5 s, then the recording sends itself (the fake host logs the utterance) |
| 23 | Touch gestures (touch boards) | Hold anywhere on the screen 2 s; tap while a reply is speaking; swipe down while recording | Hold = talk; the tap stops playback (the fake host logs `stop` if `done` has not arrived yet); the swipe cancels the recording |
| 24 | Speech without gaps | Restart the fake host with `--tone-ms 10000`, send `{"cmd":"reply","text":"<a long reply>"}`, then hold TALK for a voice turn (MausBot: ask for a long spoken answer) | The 10 s of speech plays with no gaps or clicks (the 1 s jitter buffer under real Wi-Fi jitter) |
| 25 | Speaking mouth | MausBot voice reply (real speech; the fake host's steady tone holds one level) | The mouth opens and closes with the audio and closes when playback stops |
| 26 | White eyes | Look at the Maus on any screen | The eyes are white, not black |
| 27 | Latin-1 text | Fake host `{"cmd":"post","kind":"message","text":"Café… → ok"}` | Every glyph renders: é, `…` and `→`, no boxes or blanks |
| 28 | UI rendering | Run the 7 checks under "Checks that need hardware" in `firmware/ui/README.md` (rows 25–27 are three of them; record them once) | All pass (white eyes, no red/blue or byte swap, smooth motion, text inside the round safe area, accents and …/→ render, mouth follows audio, battery indicator follows the charger) |

## Board-specific checks

| # | Board | Check | Expected |
|---|---|---|---|
| B1 | amoled-175c, amoled-175 | Touch: tap the four corners of the Ask screen buttons; swipe down on a card | The touched button reacts (coordinates are not mirrored); swipe down dismisses. If taps land mirrored, flip `BOARD_TP_MIRROR_X/Y` in `board.h` |
| B2 | amoled-175c | Press PWR briefly | CANCEL: cancels a recording or dismisses a card |
| B3 | amoled-175c | Hold PWR at power-on until the screen lights, then release | No CANCEL action fires for that press |
| B4 | amoled-175c | Hold PWR for 6 s | The board powers off (AXP2101 behaviour, documented in spec §5.3) |
| B5 | amoled-175c, amoled-175 | Battery fitted, unplug USB, `status` | `"battery":{"pct":N,"charging":false}` with N within about 10 % of the real charge; plug USB: `"charging":true` within 5 s |
| B6 | amoled-175c, amoled-175 | No battery fitted | `status` has no `battery` field and the screen shows no battery arc |
| B7 | lcd-154 | Battery fitted, USB unplugged: press PWR to switch on, release | The board stays on (BAT_EN latch) |
| B8 | lcd-154 | Battery: `status` on battery, then plug USB | `pct` plausible (a full cell reads 90–100); `charging` turns true while CHG_STAT is low |
| B8a | lcd-154 | No battery fitted, on USB | `status` has no `battery` field. If it shows a level, note the BAT_ADC voltage seen on USB so the no-cell rule (`PL_LIPO_NO_CELL_MV`, 3000 mV at the cell) can be tuned |
| B9 | lcd-154 | Picture | No 80-pixel offset band; if shifted, set `BOARD_LCD_Y_GAP` to 80 in `board.h` and rebuild |
| B10 | lcd-154 | PLUS button | CANCEL |
| B11 | devkit | Picture | Landscape 320×240, not mirrored; if mirrored, flip `BOARD_LCD_MIRROR_X` in `board.h`; if colours are inverted, set `.invert = false` in `boards/devkit/board.c` |
| B12 | devkit | GPIO1 button | CANCEL |
| B13 | devkit | Speaker | The fake host's `rx` event for `hello` shows `"speaker":{"rate":24000}`; the tone plays at the right pitch (440 Hz, not 293 Hz or 660 Hz) |
| B14 | devkit | `status` | No `battery` field |

## OTA and rollback (one touch board and one button board at least)

Board builds never trust the test key. For these checks build two test images
that do; they are for your bench only and must never be published. Build them
only in their own directories (`build/ota-a`, `build/ota-b`): the project
refuses `-D GADGET_TEST_KEYS=1` in the standard `build/<board>`, where the
setting would stick to every later build. Their versions end in `-dev`, so
MausBot treats them as custom builds and never as an official release:

```
idf.py -B build/ota-a -D GADGET_BOARD=<board> -D SDKCONFIG=build/ota-a/sdkconfig -D GADGET_TEST_KEYS=1 -D PROJECT_VER=1.0.0-dev build
idf.py -B build/ota-b -D GADGET_BOARD=<board> -D SDKCONFIG=build/ota-b/sdkconfig -D GADGET_TEST_KEYS=1 -D PROJECT_VER=1.0.1-dev build
idf.py -B build/ota-a -D GADGET_BOARD=<board> -D SDKCONFIG=build/ota-a/sdkconfig -D GADGET_TEST_KEYS=1 -D PROJECT_VER=1.0.0-dev -p /dev/cu.usbmodem* flash monitor
```

For the MausBot Update-button path, use the same variant directories with
non-dev versions (1.0.0/1.0.1); never publish them.

Pair image A with the fake host, then:

| # | Check | How | Expected |
|---|---|---|---|
| O1 | Update | `{"cmd":"ota","image":"firmware/ports/esp32/build/ota-b/openmausbot-gadget.bin","version":"1.0.1-dev"}` | Update screen with progress; fake host `ota` events `offered`, `ready`, `progress`…, `committed`, then after the restart `installed` with `1.0.1-dev`; `status` shows `"fw":"1.0.1-dev"` |
| O2 | Same version | Send the same `ota` command again | `ota` event `failed` with `code` `same_version` |
| O3 | Tampered image | `{"cmd":"ota","image":"…/ota-a/openmausbot-gadget.bin","version":"1.0.0-dev","tamper":"sig"}` | `failed` with `bad_sig`; the gadget keeps running 1.0.1-dev |
| O4 | Power loss while receiving | Start an update to A, unplug at about 50 % | After power returns the gadget boots 1.0.1-dev and pairs normally |
| O5 | Power loss during probation | Update to A; when it restarts, unplug before it reaches `ready` (stop the fake host first so `ready` cannot happen) | After power returns it boots 1.0.1-dev again (rollback) |
| O6 | Probation timeout | Update to A with the fake host stopped right after `committed`; wait 5 minutes | The firmware rolls back by itself and reboots into 1.0.1-dev (`@omb boot` shows `"fw":"1.0.1-dev"`) |
| O7 | Back to a normal build | Flash a normal board build | Boots, still paired |

## Optional, permanent: NVS encryption (sacrificial board only)

Building with `-D GADGET_NVS_ENCRYPT=1` burns an HMAC key into eFuse key block
5 on first boot. This cannot be undone. Only on a board you can dedicate, and
only in its own build directory (the project refuses it in `build/<board>`):

1. `idf.py -B build/<board>-nvs-encrypt -D GADGET_BOARD=<board> -D SDKCONFIG=build/<board>-nvs-encrypt/sdkconfig -D GADGET_NVS_ENCRYPT=1 build flash monitor`
2. Pair it, reboot: still paired. `idf.py -p /dev/cu.usbmodem* efuse-summary` shows `BLOCK_KEY5` with purpose `HMAC_UP`.
3. Flash a normal build: NVS cannot be read, the firmware erases it and the gadget must pair again (expected; Remove the old entry in MausBot).

## Result table (copy into the PR or release notes)

| Check | amoled-175c | amoled-175 | lcd-154 | devkit |
|---|---|---|---|---|
| 1–28 | | | | |
| B1–B14, B8a (where they apply) | | | | |
| O1–O7 | | | | |
| NVS encryption (optional) | | | | |
````

- [ ] **Step 2: Check it covers spec §10's hardware row**

```bash
for k in 'Boot log' 'Touch' 'PWR' 'Mic level' 'Speaker' 'Battery' 'latch' 'Reflash keeps pairing' 'Installer reinstall' 'Pair' 'TALK button' 'Approval' 'Update' 'Power loss while receiving' 'Probation timeout' 'ui/README.md'; do grep -q "$k" docs/hardware-checklist.md && echo "ok $k" || echo "MISSING $k"; done
head -n 1 docs/hardware-checklist.md
```

Expected: sixteen `ok` lines, no `MISSING`, then `<!-- SPDX-License-Identifier: Apache-2.0 -->`.

- [ ] **Step 3: Commit**

```bash
git add docs/hardware-checklist.md
git commit -m "docs: manual hardware checklist for the four ESP32 boards"
```
### Task 16: Branch verification

**Files:** none (verification only; fix anything it finds in the task that owns the file, then re-run this task).

**Interfaces:**
- Consumes: every deliverable of Tasks 1–15 and the repository's existing suites (contract §1.5).
- Produces: a clean `p2c-esp32` branch ready for Omkar to review and publish; the hand-off summary below.

- [ ] **Step 1: Host tests from a clean build directory**

Run: `rm -rf build/esp32-host && cmake -S firmware/ports/esp32/host-tests -B build/esp32-host && cmake --build build/esp32-host -j10 && ctest --test-dir build/esp32-host --output-on-failure`

Expected: `100% tests passed, 0 tests failed out of 13`.

- [ ] **Step 2: Script tests and shell syntax**

```bash
firmware/ports/esp32/tools/test-check-size.sh | tail -n 1
firmware/ports/esp32/tools/test-check-art-profile.sh | tail -n 1
for s in firmware/ports/esp32/tools/*.sh; do bash -n "$s" || echo "SYNTAX $s"; done; echo "shell syntax ok"
```

Expected: `all check-size tests passed`, `all check-art-profile tests passed`, `shell syntax ok` and no `SYNTAX` line.

- [ ] **Step 3: Every board from scratch, plus both variants**

```bash
. ~/.espressif/tools/activate_idf_v6.0.3.sh
rm -rf firmware/ports/esp32/build
firmware/ports/esp32/tools/build-all.sh
cd firmware/ports/esp32
idf.py -B build/amoled-175c-nvs-encrypt -D GADGET_BOARD=amoled-175c -D SDKCONFIG=build/amoled-175c-nvs-encrypt/sdkconfig -D GADGET_NVS_ENCRYPT=1 build > build/nvs.log 2>&1 && tools/check-size.sh amoled-175c build/amoled-175c-nvs-encrypt
idf.py -B build/lcd-154-test-keys -D GADGET_BOARD=lcd-154 -D SDKCONFIG=build/lcd-154-test-keys/sdkconfig -D GADGET_TEST_KEYS=1 build > build/keys.log 2>&1 && tools/check-size.sh lcd-154 build/lcd-154-test-keys
idf.py -B build/devkit-release -D GADGET_BOARD=devkit -D SDKCONFIG=build/devkit-release/sdkconfig -D PROJECT_VER=1.2.3 build > build/release.log 2>&1 && grep -m1 -F 'App "openmausbot-gadget" version: 1.2.3' build/release.log
cd ../../..
git diff --exit-code -- firmware/ports/esp32/dependencies.lock.amoled-175c firmware/ports/esp32/dependencies.lock.amoled-175 firmware/ports/esp32/dependencies.lock.lcd-154 firmware/ports/esp32/dependencies.lock.devkit && test -z "$(git status --porcelain -- 'firmware/ports/esp32/dependencies.lock*')" && echo "locks unchanged"
```

Expected: four boards with `check-size ... ok` and `check-art-profile ... links only ...` (and no `build-all: ... has test keys or NVS encryption on`: the two variants live in their own directories); `check-size: amoled-175c ok` and `check-size: lcd-154 ok` for the variants; `-- App "openmausbot-gadget" version: 1.2.3` for the release-style build (`PROJECT_VER` from `-D` wins over the `0.0.0-dev` default); `locks unchanged`.

- [ ] **Step 4: The rest of the repository still passes**

```bash
npm ci && npm test
cmake -S firmware -B build/host -DCMAKE_BUILD_TYPE=Debug && cmake --build build/host -j10 && ctest --test-dir build/host --output-on-failure -L "unit|vectors|e2e|snapshot"
(cd tools/art && npm ci && npm run budget)
```

Expected: P1's protocol and fake-host tests pass; the desktop build passes all `unit`, `vectors`, `e2e` and `snapshot` tests (this plan does not touch them; the only shared file it may have edited is the `if(ESP_PLATFORM)` branch of `firmware/core/CMakeLists.txt`, which is inert in a desktop build because `ESP_PLATFORM` is unset there); the art budget check passes.

- [ ] **Step 5: Hygiene**

```bash
git diff --check p2b-ui...HEAD && echo "no whitespace errors"
git diff --name-only p2b-ui...HEAD -- firmware/ports/esp32 docs/hardware-checklist.md | grep -v 'dependencies.lock' | xargs grep -L 'SPDX-License-Identifier' ; echo "spdx check done"
git grep -n 'github.com' -- firmware/ports/esp32 docs/hardware-checklist.md ':!firmware/ports/esp32/host-tests/CMakeLists.txt' ; echo "no third-party project links above"
git grep -n -E 'TBD|TODO|FIXME' -- firmware/ports/esp32 docs/hardware-checklist.md ; echo "no placeholders above"
git diff --name-only p2b-ui...HEAD | grep -v -E '^(firmware/ports/esp32/|docs/hardware-checklist.md|\.github/workflows/ci.yml|firmware/core/CMakeLists.txt)' ; echo "no files outside this plan's scope above"
git status --short
```

Expected: `no whitespace errors`; nothing printed before `spdx check done` (`docs/hardware-checklist.md` starts with its `<!-- SPDX-License-Identifier: Apache-2.0 -->` line), `no third-party project links above` (the pinned Unity tarball URL in `host-tests/CMakeLists.txt` is a dependency pin, not a project link, so that file is excluded), `no placeholders above` and `no files outside this plan's scope above` (`firmware/ui/CMakeLists.txt` is not on the allowed list: its early-expansion guard is P2b's, contract §2.17, so any edit to it shows up here); `git status --short` prints nothing.

- [ ] **Step 6: Hand off**

Do not push, open a PR or create a release. Report to Omkar: the branch name `p2c-esp32`, the commit list (`git log --oneline p2b-ui..HEAD`), the four `check-size` lines with their sizes, and the summary below.

**Not verifiable without hardware** (each is a row of `docs/hardware-checklist.md`):

- That each panel lights up with the right orientation, colours and offset: the CO5300 init table in each AMOLED `board.h` (or the component's default, check 2) and x-gap 6, the ST7789 inversion, gaps and the devkit's swap/mirror (checks 2, B9, B11); the on-device UI checks of `firmware/ui/README.md` (checks 25–28).
- Touch coordinates and mirroring on the CST9217 (B1); that PWR on the 1.75C reads high when pressed, is ignored when held at power-on and powers off after 6 s (B2–B4).
- Audio: ES8311/ES7210 at 16 kHz duplex through `esp_codec_dev`, the MIC1 slot (left) and its gain, speaker level and idle hiss, the codec mute that silences a stopped reply at once, the devkit's INMP441 shift of 14 and MAX98357A at 24 kHz, the 60 s limit, touch gestures and gap-free speech (checks 14–16, 22–24, B13).
- Battery numbers: the AXP2101 fuel gauge and charging bit, the lcd-154 voltage curve, its no-cell reading, CHG_STAT polarity and the BAT_EN latch on battery (B5–B8a).
- Wi-Fi behaviour against a real access point: the wrong-password path, scanning while retrying, automatic recovery after a router restart (checks 7–8); mDNS discovery of a real MausBot, including from Windows hosts (check 12).
- The USB-Serial-JTAG console as an input device, `log off`, and the 8 KiB line limit (checks 4–6, 9–10); reflashing without losing NVS, with `idf.py` and with the browser installer (checks 19, 21).
- The full ESP-IDF configure and build itself: P2b's early-expansion guard (checked by Task 2 Step 6b) was run only against a script-mode simulation of v6.0.3's `component_get_requirements.cmake`, and nothing here ran `idf.py` (Task 2 Steps 8–9 and the `esp32` CI job are the first real runs).
- OTA on flash: write speed with code executing from PSRAM, `esp_ota_end` image verification, rollback after power loss and after the 5-minute probation (O1–O7).
- That HMAC NVS encryption really burns eFuse key block 5 and keeps the pairing (optional section; permanent).
- Runtime limits: internal DMA memory for the two LVGL buffers (2 × 37,280 bytes on the AMOLED boards), the gadget task's 16 KiB stack, frame rate of the round 466×466 screen.

---

## Spec coverage

| Spec / amendment / contract item | Where |
|---|---|
| §2, §5.1, A14: ESP-IDF v6.0.3, buildable on v5.5.5, manifest `>=6.0.3,<6.1`, optional relaxed v5.5.5 CI leg | Task 2 (manifest, install), Task 14 (`esp32-idf55`) |
| §5.1: build command, one sdkconfig per board, `SDKCONFIG_DEFAULTS` order, failure on unset/unknown board, `PROJECT_VER` = `0.0.0-dev` by default | Task 2 (project file, guard checks Steps 8–9), Task 16 Step 3 (release-style `PROJECT_VER`) |
| §5.1, A14: LVGL 9.6.0 configured with `CONFIG_LV_*`; fonts via styles (P2b); no Waveshare BSP, no `esp_lvgl_port` | Task 2 (defaults, manifest; `CONFIG_LV_CONF_SKIP=y` and both image caches off pinned and asserted by `test_shared_defaults`), Task 10 (own single-threaded glue) |
| §5.1: own driver glue on `esp_lcd_co5300`, `esp_lcd_touch_cst9217`, `esp_codec_dev`, IDF ST7789, `i2s_std`, `i2c_master` | Tasks 8, 9, 10, 12 |
| §5.1: `esp_websocket_client` with `buffer_size` 16 KiB + 64 and no auto-reconnect; `mdns ^1.14.0` | Task 6, Task 7 |
| §5.1: cJSON only from `espressif/cjson` | Task 2 (manifest; core's ESP branch check in Step 6) |
| §5.2: every HAL group — Mic and Speaker | Task 8 |
| §5.2: Input | Task 9 |
| §5.2: Storage (NVS) | Task 4 |
| §5.2: Net — Wi-Fi state and scan / WebSocket / mDNS | Tasks 5 / 6 / 7 |
| §5.2: OTA slot (open, write, finalize, set boot, mark valid, rollback state) | Task 11 |
| §5.2: Battery | Task 9 |
| §5.2: System (clock, restart, log, console) | Task 3 |
| §5.2: driver tasks post to a FreeRTOS queue the main task drains; OTA writes on a worker task; core/UI single-threaded | Task 3 (queue), Tasks 6, 7, 8, 11 (worker tasks), Task 13 (loop) |
| §5.2, A15: PSA crypto is core's; the port calls `psa_crypto_init()` before `core_init()` | Task 13 (`main.c`) |
| §5.3, A17: four boards, 16 MB flash config for all, DevKitC-1-N16R8, one partition table, `caps.ota.max` = slot, CI asserts `app.bin` ≤ slot; physical flash size logged | Tasks 1, 2, 13, 14 |
| §5.3, A13: codec boards duplex at 16 kHz, devkit speaker 24 kHz | Task 8, Task 12 |
| §5.3, A18: MIC1 only, no echo cancellation | Task 8 (`mic_selected = ES7210_SEL_MIC1`, channel mask) |
| §5.3, A18: amoled-175c PWR = GPIO3 active high, 6 s hold powers off (documented) | Task 2 (`board.h`), Task 12, Task 15 (B2–B4) |
| §5.3, A18: lcd-154 BAT_EN high first, battery supported (BAT_ADC ×3, CHG_STAT low = charging) | Task 9 (driver), Task 12, Task 15 (B7–B8) |
| §5.3: pins and init sequences from vendor sources in `board.h` (the CO5300 table is `BOARD_CO5300_INIT_CMDS`); vendor components fetched at build time | Task 2, Task 10, Task 12 |
| §5.1, contract §1.1: a board directory is `board.h`, `board.c`, `sdkconfig.defaults` (`board.cmake` optional) | Task 2 (`main/CMakeLists.txt` globs every driver when `board.cmake` is absent) |
| §5.2, contract §2.5: `hal_spk_stop` goes silent now; `hal_mdns_browse` ends within `timeout_ms`; `hal_battery_read` false without a cell | Task 8 (codec mute; devkit tail is Contract deviation 1), Task 7 (`pl_mdns_ptr_ms`/`pl_mdns_a_ms`), Task 9 (`pl_lipo_present`) |
| §5.4: TALK/CANCEL/touch raw edges, button-board mapping (core derives holds/taps/swipes) | Task 9, Task 12 |
| §5.6, A22: USB-Serial-JTAG console, CR/LF/CRLF, `log off/on` never hides `@omb` lines | Task 3 (`console_usj.c`, `hal_system.c`), Task 15 (checks 4–10) |
| §4.2, A16: key in NVS namespace `gadget`; opt-in `GADGET_NVS_ENCRYPT` with HMAC eFuse block 5 and a warning; key generated after Wi-Fi starts | Task 4, Task 2 (Kconfig help, overlay), Task 13 (Wi-Fi started before `core_init()`) |
| §4.8, A19: OTA into the inactive slot, firmware-run probation, rollback on any early reboot, anti-rollback never | Task 11, Task 2 (rollback lines), Task 1 (`check-size.sh` checks them) |
| §8, A37: test key off in every board build; bench-only test-key builds are explicit, live in their own build directory and are never published; the fake-host images carry `-dev` versions so they never look official, and the MausBot Update-button images use non-dev versions in the same variant directories | Task 2 (defaults + test, the project's variant-directory guard, `build-all.sh`'s sdkconfig check), Task 11 (variant check), Task 15 (fake-host bench images `1.0.0-dev`/`1.0.1-dev`; Update-button images `1.0.0`/`1.0.1` in `build/ota-a`/`build/ota-b`) |
| §10 "Firmware builds": all four boards in one checkout, each with its own sdkconfig, app ≤ slot, optional v5.5.5 job | Task 14 Step 2: the `esp32` job's amoled-175c/plain leg runs `tools/build-all.sh` (amoled-175, amoled-175c, devkit, lcd-154 one after another in one checkout, each with its own sdkconfig and lock file, `check-size.sh` and `check-art-profile.sh` per board); the other legs build one board each; `esp32-idf55`. Locally: `build-all.sh` in Tasks 2–13 and Task 16 Step 3 |
| §10 "Hardware": manual checklist per board, including installer reflash keeping the pairing; P2a's and P2b's device checks | Task 15 (checks 1–28, B1–B14, B8a, O1–O7; links `docs/installer-checklist.md` and `firmware/ui/README.md`) |
| Contract §2.17: project file, shared defaults, Kconfig symbols, `main.c` order, `board_api.h`, `check-size.sh`, devkit USB port; P2b's early-expansion guard at the top of `firmware/ui/CMakeLists.txt` (checked, never edited) | Tasks 1, 2 (guard: Step 6b, Step 8), 3, 13, 15; Task 16 Step 5 (no edit to the ui file) |
| Contract §1.6: CI job ids `esp32`, `esp32-idf55`; `esp32` keeps the board matrix | Task 14 |
| Contract §1.4: every drift check over generated files ends with `test -z "$(git status --porcelain -- <path>)"` | Task 14 (lock check), Task 16 Step 3 |

Not in this plan (owners named in Scope): core behaviour (P2a), screens, art and fonts (P2b), installer, release and Pages workflows, AGENTS.md, README, NOTICE, THIRD_PARTY.md, release key table (P2d), every OpenMausBot change (P3a, P3b, P4a, P4b).

## Contract notes

**Additions** (no pinned name, type, path or format changes; things only this plan reads, or that other plans may use without depending on them):

1. `tools/check-size.sh <board> [build-dir]` — the optional second argument lets CI and the variant builds check a build directory other than `build/<board>`; the contract's one-argument form behaves as pinned.
2. New P2c-owned files and switches: `sdkconfig.nvs-encrypt` + `-D GADGET_NVS_ENCRYPT=1`, `sdkconfig.test-keys` + `-D GADGET_TEST_KEYS=1` (bench OTA tests only; the project refuses both in the standard `build/<board>` directory), `tools/build-all.sh` (also fails when a standard build has either on), `tools/check-art-profile.sh`, `host-tests/`. `boards/<id>/board.cmake` is an optional size optimisation that no other plan needs: without it `main/CMakeLists.txt` compiles every shared driver and `--gc-sections` drops the unused ones, so a board directory still needs only the three files spec §5.1 and contract §1.1 list (`board.h`, `board.c`, `sdkconfig.defaults`), and P2d's "add a board" stays correct as written.
3. `hal_ws_open()` returns `GADGET_ERR_BUSY` while the previous connection's `GADGET_EV_WS_CLOSED` has not reached core yet, which keeps "one connection at a time" and "exactly one CLOSED per open" both true.
4. `hal_spk_stop()` empties the speaker ring and, on the three codec boards, mutes the ES8311 at once (`board_audio_t.spk_set_mute`), so it goes silent as contract §2.5 pins; the speaker task unmutes once the TX DMA ring holds only silence. `hal_spk_buffered_ms()` reports 0 right after the stop. The devkit's remaining tail is Contract deviation 1.
5. For a console line longer than `GADGET_CONSOLE_LINE_MAX` (reported by `gadget_linebuf_feed` as `on_line(NULL)`), the port prints `@omb {"op":"error","cmd":"","message":"line too long"}`, which fits the installer's `error` message type (contract §4.8).
6. `esp32` keeps the §1.6 board matrix; its amoled-175c leg also runs `build-all.sh` so spec §10's sequential one-checkout build runs in CI (contract §0.1: the spec wins on behaviour; the matrix shape is unchanged).
7. ESP-IDF install on macOS: the plan puts `brew install libgcrypt glib pixman sdl2 libslirp dfu-util ninja` in front of contract §1.4's EIM line, because EIM only checks its POSIX prerequisites and stops when one is missing. The EIM commands themselves are as pinned.

**Contract deviations** (each keeps the pinned shape and needs review, contract §0 item 2):

1. **`hal_spk_stop()` on the devkit** (contract §2.5: "Drop everything queued and go silent now"). The MAX98357A has no mute register and no control bus, so after a stop up to the devkit's `spk_latency_ms` (60 ms: 6 DMA descriptors × 240 frames at 24 kHz) still plays out of the I2S DMA ring; on barge-in that tail can reach the start of the new recording. The codec boards meet the pinned behaviour through the codec mute. Proposed change: none to the API; one of: accept "within 60 ms" for boards without a codec mute in the contract's comment; shrink the devkit's DMA ring (for example 4 × 120 frames = 20 ms) at the cost of more underrun risk; or wire the amplifier's SD (shutdown) pin to a free GPIO in the devkit wiring and give the devkit an `spk_set_mute` that drives it. Review needed.
2. **Closed: `firmware/ui/CMakeLists.txt` outside its ESP branch.** An earlier draft had Task 2 Step 6b add the ESP-IDF early-expansion guard to P2b's file, outside the `if(ESP_PLATFORM)` branches that contract §2.17 lets P2c correct. P2b's file now opens with that exact guard, and contract §2.17 names P2b its owner (`firmware/ui/CMakeLists.txt` opens with P2b's early-expansion guard, `if(ESP_PLATFORM AND CMAKE_BUILD_EARLY_EXPANSION) idf_component_register(INCLUDE_DIRS . art fonts REQUIRES lvgl__lvgl core) return() endif()`). This plan no longer edits the file: Step 6b only checks that the guard is there and stops if it is not, and Task 16 Step 5 fails on any edit to it. Nothing left to review.

**Seams to confirm with other plans** (each is checked by a step here, none is guessed):

- **P2b** — `firmware/ui/CMakeLists.txt`'s ESP branch compiles only `art/${CONFIG_GADGET_ART_PROFILE}` and `maus_art_for()` returns NULL for the other profile (contract §2.15). Task 2 Step 6 greps for it and stops if it is missing; Task 13's `check-art-profile.sh` proves the image links one profile. The same file opens with P2b's early-expansion guard, which contract §2.17 makes P2b's to keep in every later edit; Task 2 Step 6b greps for it and stops if it is missing (Contract deviation 2 is closed). `docs/hardware-checklist.md` links `firmware/ui/README.md`'s "Checks that need hardware" (row 28), as P2b's hand-off asks. P2b's hand-off also asks to keep `CONFIG_LV_CONF_SKIP=y` and both LVGL image caches off (`ui_lv_requirements.h` `#error`s unless `LV_CACHE_DEF_SIZE` and `LV_IMAGE_HEADER_CACHE_DEF_CNT` are 0): Task 2 Step 3 writes all three lines into the shared `sdkconfig.defaults` instead of relying on Kconfig defaults, `test_shared_defaults` asserts them and `test_each_board_defaults_match_the_table` fails if a board file (which wins over the shared one) touches them, so the host tests catch a missing pin before the first `idf.py build`.
- **P2a** — `firmware/core/CMakeLists.txt`'s ESP branch has `REQUIRES espressif__cjson mbedtls`, both key-table files and the `CONFIG_GADGET_TEST_KEYS` block (contract §2.17). Task 2 Step 6 inserts only that block if it is missing. Core must not call `hal_ws_open()` again before it has seen the previous `GADGET_EV_WS_CLOSED`. The device checks P2a hands to this checklist (60 s countdown, swipe-down, jitter buffer, mouth level) are rows 22–25.
- **P2d** — AGENTS.md's "add a board" is correct with its three files (`board.h`, `board.c`, `sdkconfig.defaults`); it may mention the optional `board.cmake` as a way to compile only that board's drivers. It should link `docs/hardware-checklist.md`, which in turn links `docs/installer-checklist.md` (row 21 repeats its reinstall check). `release.yml` can reuse `tools/check-size.sh <board> <dir>` and `tools/check-art-profile.sh <board> <dir>` and must never pass `-D GADGET_TEST_KEYS=1` or `-D GADGET_NVS_ENCRYPT=1`. AGENTS.md's ESP-IDF install should include EIM's macOS prerequisites (addition 7).

## Self-review

- **Spec coverage:** every row of spec §5.3 and the ESP32 sides of §4.2, §4.8, §5.1, §5.2, §5.4, §5.6 and §10 maps to a task in the table above; items owned elsewhere are named in Scope.
- **Placeholder scan:** every code step carries complete file contents; every command has its expected output; the two conditional instructions (the IDF-6 warning workaround for a managed component, the core ESP-branch insertion) give the exact lines to add. The plan makes no unconditional edit to another plan's file: Task 2 Step 6b only checks P2b's early-expansion guard.
- **Type and name consistency:** `pl_*` signatures in Interfaces match the headers in the code blocks (including `pl_mdns_ptr_ms`, `pl_mdns_a_ms`, `pl_lipo_present`); `board_api.h` (with `spk_set_mute`) is used unchanged by all four `board.c` files and by `drv_es_codec`/`drv_i2s_simplex`; `drv_co5300_cfg_t.init_cmds` is filled from `BOARD_CO5300_INIT_CMDS` by both AMOLED `board.c` files; the HAL functions are exactly those of contract §2.5 (checked by a script while writing this plan and again in Task 13 Step 5); `port.h` declares every `port_*` function the tasks define.
- **Review Focus:** each of the five lines has its test in the owning task (Task 2 Steps 8–9 and the CI leg of Task 14, Task 5, Task 6, Task 9, Task 11).

## Deviations recorded during the build

Review of Tasks 5–8 (SDK commit `fix(P2c): address review of tasks 5, 6, 7, 8`, on `main`). Where these differ from the code blocks in Tasks 3, 5 and 6, the repository files are authoritative. `test_pl_wifi` grows from 10 to 12 tests and `test_pl_wsasm` from 8 to 9. The ctest count stays 9 after Task 8 (13 after Task 11), because each binary is one ctest entry. Each new Wi-Fi test failed against the Task 5 code and passes after the fix. The new `test_pl_wsasm` test is a coverage test: it passes on the Task 6 code, and it is the only test that fails when `len > max - a->len` is mutated to `len > max`.

1. **A GOT_IP that races the port's own disconnect no longer leaves the station CONNECTED with no link (Task 5, `logic/pl_wifi.c`).** The port calls `esp_wifi_disconnect()` only to end an attempt: an attempt timeout in `pl_wifi_tick()`, a scan pause in `pl_wifi_scan()`, or a new network in `pl_wifi_connect()`. A GOT_IP already queued behind that call set `connected = true` and cleared `retry_at_ms`. Then the local `ASSOC_LEAVE` disconnect was ignored, or, after a scan pause, `pl_wifi_scan_done()` skipped the reconnect because `connected` was true. Either way the state stayed `GADGET_WIFI_CONNECTED` with no link and no retry until a reboot or a new `wifi` command, which breaks Review Focus #2. The plan's own code block had this bug. Now the `local` branch of `pl_wifi_sta_disconnected()` clears `connected`. When it was set and a network is configured, the branch resets `failures`, reports `GADGET_WIFI_CONNECTING` and starts an attempt. While a scan runs, `start_attempt()` defers the attempt to `pl_wifi_scan_done()`. Tests: `test_got_ip_racing_attempt_timeout_reconnects` and `test_got_ip_racing_scan_pause_resumes_after_scan`.
2. **`GADGET_EV_WS_CLOSED` is never dropped (Task 6, `hal_ws.c`).** `post_closed()` ignored `port_post_event()`'s result. If the port queue stayed full for its 2 s wait, the one CLOSED event was lost, `port_ws_note()` never cleared `s_active`, and every later `hal_ws_open()` returned `GADGET_ERR_BUSY` until a reboot. That broke contract §2.5's "exactly one `GADGET_EV_WS_CLOSED` after every successful `hal_ws_open()`" and Contract notes, addition 3. `post_closed()` now retries every 100 ms until the event is queued. Its callers are the ws worker task (`do_open()` failures) and the client task (`WEBSOCKET_EVENT_FINISH`), never the gadget task, so blocking cannot stall the drain that empties the queue. This needs hardware to exercise, so there is no host test. Both files compile with the v6.0.3 toolchain from `build/amoled-175c` and `build/devkit` `compile_commands.json`.
3. **A failed scan is reported as failed (Tasks 3 and 5, `port.h` and `hal_wifi.c`).** `on_event()` ignored `wifi_event_sta_scan_done_t.status`, which is 0 on success and 1 on failure in ESP-IDF v6.0.3 (`esp_wifi_types_generic.h`). A failed scan was posted as `GADGET_EV_WIFI_SCAN` with `ok = true` and 0 networks, so the console printed an empty list instead of a failure (contract §2.4: `ok = false` means the scan failed). `port_wifi_msg_t` (a Task 3 file) gains `bool scan_failed`, which `on_event()` sets for `SCAN_DONE` when `status != 0`. `deliver_scan()` becomes `deliver_scan(bool driver_ok)` and computes `ok = driver_ok && esp_wifi_scan_get_ap_num(&n) == ESP_OK`. It still calls `esp_wifi_clear_ap_list()`, and `pl_wifi_scan_done()` still resumes a paused attempt. No host test covers this, because the changed code is IDF-only. It compiles as in item 2.
4. **The size limit across continuation frames is under test (Task 6, `host-tests/test_pl_wsasm.c`).** No test fed an oversize message in fragments, which is the only case where `len > max - a->len` uses an accumulated `a->len` (Review Focus #3: close with 1009, never truncate or merge). New test `test_fragments_over_16k_are_too_big`: 8 KiB of TEXT plus 8 KiB of CONT reach exactly `GADGET_TEXT_FRAME_MAX`, and 1 more byte gives `PL_WS_TOO_BIG`. No code change.
