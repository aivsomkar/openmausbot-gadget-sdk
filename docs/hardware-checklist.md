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
  them): `brew install libgcrypt glib pixman sdl2 libslirp dfu-util ninja && brew tap espressif/eim && brew install eim && eim install -i v6.0.3 -t esp32s3`.
  Without EIM, follow the manual installation in Espressif's ESP-IDF Get
  Started guide: clone the v6.0.3 tag into `~/esp/esp-idf-v6.0.3`, run
  `~/esp/esp-idf-v6.0.3/install.sh esp32s3`, then activate with
  `. ~/esp/esp-idf-v6.0.3/export.sh` instead.
- Either MausBot with **Remote access** on (Settings → Remote access), or the
  fake host from this repository on the same Wi-Fi (run `npm ci` at the
  repository root once):
  `cat | node tools/fake-host/src/main.ts --bind 0.0.0.0 --port 8810 --code 123456`
  (commands are JSON lines on its stdin, for example `{"cmd":"ask","kind":"permission","title":"Run a command?","body":"ls"}`;
  it reads stdin only from a pipe, hence the `cat |`). The fake host does not
  advertise mDNS: give the board `host <mac-ip>:8810`, never `host auto`.
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
| 11 | Pair | Open a pairing code (MausBot: Settings → Remote access → Pair a gadget; fake host: `{"cmd":"code","code":"123456"}`), then `pair <code>` and `host auto` (MausBot) or `host <mac-ip>:8810` (fake host) | `status` reaches `"pair":"paired"` with `host_name`; the Idle screen shows the bot name |
| 12 | mDNS | MausBot only (the fake host does not advertise mDNS): after pairing, `host auto`, `reboot` | It reconnects to the same host without `host <ip>`. On Windows hosts this may fail (spec §6.5); `host <ip>` then works |
| 13 | TALK button | Hold TALK (BOOT) for 2 s while paired, release | Listening screen while held, then Thinking/Reply; a press under 300 ms does nothing |
| 14 | Mic level | Hold TALK and speak | The level ring follows your voice; silence keeps it low. If it stays flat on a codec board, record a failure: MIC1 should be on the left slot of ES7210 SDOUT1 |
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
