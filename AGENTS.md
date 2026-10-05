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
5. Create `firmware/ports/esp32/boards/<board>/`: `board.h` (pins, from the vendor's schematic), `board.c` (the functions in `main/board_api.h`: `board_early_init`, `board_display_init`, `board_touch_init`, `board_audio_init`, `board_buttons_init`, `board_talk_pressed`, `board_cancel_pressed`, `board_battery_read`, `board_set_brightness`) and `sdkconfig.defaults` with `CONFIG_GADGET_BOARD_ID`, `CONFIG_GADGET_ART_PROFILE`, `CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y`, `CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions/16mb.csv"` and `# CONFIG_GADGET_TEST_KEYS is not set`. An optional `board.cmake` sets `BOARD_DRIVER_SRCS` to the drivers in `main/drivers/` that `board.c` uses. The first build writes `firmware/ports/esp32/dependencies.lock.<board>`: commit it, because CI fails when it drifts.
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
