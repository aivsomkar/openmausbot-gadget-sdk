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
