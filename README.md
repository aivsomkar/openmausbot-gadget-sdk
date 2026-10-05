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
