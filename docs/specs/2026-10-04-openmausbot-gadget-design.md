# OpenMausBot Gadget: design

- **Status:** approved in design review, awaiting written-spec review
- **Date:** 2026-10-04
- **Scope:** this repository (firmware, simulator, installer, protocol) and the matching host support in the OpenMausBot desktop app

## 1. What we are building

A small ESP32 board with a screen, a microphone and a speaker becomes a desk terminal for your own MausBot. Hold to talk, and your words go to the bot running on your Mac. The bot's reply appears on the screen and is spoken back. The gadget also shows the bot's approval questions so you can answer them with a tap, shows results that routines and other devices push to it, and gives bots a few tools to drive it.

This repository is an **open-source SDK** for people who build their own: firmware for four boards, a desktop simulator that runs the same UI, a browser installer, and the protocol spec. The host side ships inside OpenMausBot itself. Users install MausBot, flash a board and pair it. There is nothing else to run.

### Goals

1. Talking to a bot from a $40 board feels as quick and natural as a voice note on the phone.
2. A gadget is a first-class MausBot device. It pairs with the same six-digit code as the companion apps, appears in Settings → Devices, and Remove revokes it.
3. Makers can build it with no prior ESP32 experience: browser installer, simulator, `AGENTS.md` for coding agents.
4. Everything works on the local network with no cloud service beyond what MausBot already uses.

### Non-goals for v1

- Pairing or setup from the phone apps, over BLE or otherwise. The phones are already companion apps; gadgets pair with the desktop's six-digit Wi-Fi pairing code.
- Self-hosted Docker servers and Cloud home. v1 targets the desktop app's companion.
- Reaching the gadget from outside the LAN.
- Wake word, always-listening mode, on-device speech recognition.
- Automatic firmware updates, and over-the-air updates of custom builds.
- A Linux or Raspberry Pi gadget.

## 2. Decisions

| Topic | Decision |
|---|---|
| Code origin | All code, docs and art are original work for this project. See [§11](#11-original-work-rule). |
| Host | A gadget hub inside the MausBot companion (port 8810, path `/gadget`). The companion already owns pairing, the device registry and network exposure. |
| Protocol | `openmausbot-gadget/1`, defined here: one WebSocket, JSON control frames plus binary media frames. |
| Identity | Each gadget holds a P-256 key pair. The Mac stores only public keys. |
| Pairing | The six-digit code from MausBot → Settings → Remote access → Pair on this Wi-Fi, entered in the browser installer, the serial console or the simulator. |
| Firmware | C11 on ESP-IDF 5.x, LVGL 9 UI with an animated color Maus. |
| Boards | Waveshare ESP32-S3-Touch-AMOLED-1.75C (the hero board), ESP32-S3-Touch-AMOLED-1.75, ESP32-S3-LCD-1.54, and an ESP32-S3 DevKit breadboard build. |
| Simulator | The same core and UI on the desktop through LVGL's SDL backend. |
| Speech-to-text | On the Mac: Apple's on-device recognizer; ElevenLabs Scribe as the fallback and on Windows. |
| Text-to-speech | MausBot's existing providers, returned as 16 or 24 kHz PCM. |
| Routing | Each gadget talks to one bot (chosen in Devices) in that bot's main thread. |
| v1 features | Talk and listen, approvals on the gadget, push, bot tools, battery level, over-the-air updates of official releases. |
| License | Apache-2.0, matching OpenMausBot. The OpenMausBot name, the MausBot name and the Maus mascot are trademarks of Supamaus Software Private Limited. |

## 3. Architecture

```
 ┌──── Gadget (ESP32 board or desktop simulator) ───┐        ┌──────────── Your Mac: OpenMausBot ──────────────────┐
 │                                                  │        │                                                     │
 │  core/ (C)  session · interaction · audio ·      │  WS    │  companion :8810              harness :8799         │
 │             OTA · console                        │◀──────▶│   /gadget hub ── device ────▶  bots, threads, SSE,  │
 │  ui/ (LVGL) Maus · captions · ask · cards        │ JSON + │   enrollment     identity     approvals, STT, TTS   │
 │  HAL ── display · mic · speaker · input ·        │ PCM    │   devices.json                                      │
 │         battery · Wi-Fi · storage · OTA slot     │        │   ◀── control port :8811 ── gadget_* bot tools      │
 └──────────────────────────────────────────────────┘        └─────────────────────────────────────────────────────┘
```

The gadget is deliberately thin. Speech recognition, synthesis, Markdown shaping and image conversion all happen on the Mac, so better voices or models never need a firmware update.

The hub talks to the harness exactly as a paired phone does: through the companion's allowlisted routes, with the gadget's registry id in the companion identity headers. A gadget gets no authority a phone doesn't already have.

### Sub-projects and order

| # | Sub-project | Where | Depends on |
|---|---|---|---|
| 1 | Protocol: [§4](#4-protocol-openmausbot-gadget1) as `protocol/PROTOCOL.md` plus test vectors | this repo | none |
| 2 | Firmware, simulator, installer, fake host: [§5](#5-firmware-and-simulator) | this repo | 1 |
| 3 | MausBot host: hub, enrollment, STT, PCM TTS, Devices UI: [§6](#6-mausbot-host) | OpenMausBot | 1 |
| 4 | Bot tools and OTA delivery: [§7](#7-bot-tools), [§8](#8-firmware-releases-and-ota-delivery) | both | 2, 3 |

Sub-projects 2 and 3 run in parallel once the protocol and its vectors exist. Each sub-project gets its own implementation plan and branch.

## 4. Protocol `openmausbot-gadget/1`

### 4.1 Transport

- One WebSocket per gadget: `ws://<host>:8810/gadget`, subprotocol `openmausbot-gadget.1`. LAN only in v1.
- The server rejects any upgrade that carries an `Origin` header, so a web page cannot open a gadget session.
- **Text frames** are UTF-8 JSON objects with an `op` field. Optional fields are omitted, never sent as `null`. Receivers ignore unknown ops and unknown fields.
- **Binary frames** start with a 2-byte header:

  | Byte | Field |
  |---|---|
  | 0 | kind: `0x01` mic audio (gadget → host), `0x02` speaker audio, `0x03` image rows, `0x04` firmware (all host → gadget) |
  | 1 | stream id, 1–255, assigned by whoever sent the matching `*.begin` or `fw.offer` |
  | 2… | payload |

  - Audio: mono PCM16 little-endian at the rate given in its `begin` message. Mic frames carry 20 ms each; speaker frames carry about 40 ms each.
  - Image: RGB565 little-endian, row-major, continuing where the previous frame stopped.
  - Firmware: a u32 little-endian byte offset, then up to 4096 bytes of the image.
- **Liveness:** the host sends a WebSocket ping every 15 s. Either side treats 45 s without any inbound frame as a dead connection.
- **Limits:** text frames ≤ 16 KiB, binary frames ≤ 8 KiB, an utterance ≤ 60 s, one conversation turn in flight per gadget.

### 4.2 Identity

- On first boot the gadget generates a P-256 key pair and keeps it in encrypted NVS.
- `pubkey` is the base64 SEC1 uncompressed point (65 bytes).
- `id` is `gad_` followed by the first 16 lowercase hex characters of SHA-256(pubkey bytes).
- Because the id derives from the key, no device can claim another's id.

### 4.3 Handshake

```
gadget → hello      {proto, id, pubkey, name, board, fw, caps, actions, sensors}
host   → challenge  {nonce, host_id, host_name}
gadget → prove      {sig, enroll?}
host   → ready      {session, bot, settings}          or   error {code, message}, then close
```

- **`hello`**:

  ```json
  {"op": "hello", "proto": 1, "id": "gad_3f9a0c2b7e41d856", "pubkey": "BHx…",
   "name": "Desk Maus", "board": "amoled-175c", "fw": "1.0.0",
   "caps": {"screen": {"w": 466, "h": 466, "round": true, "text": "latin1"},
            "image": {"w": 300, "h": 300},
            "mic": {"rate": 16000}, "speaker": {"rate": 24000},
            "input": ["touch", "talk", "cancel"], "battery": true,
            "ota": {"max": 6291456}},
   "actions": [{"name": "chime", "description": "Play a short chime.",
                "params": {"type": "object", "properties": {}}, "risk": "safe"}],
   "sensors": {"battery_pct": 82, "charging": false}}
  ```

  Every `caps` member is optional. A gadget without a speaker omits `speaker` and never receives speech. `risk` is `safe` or `confirm`, and a missing value means `confirm`.

- **`challenge`**: `nonce` is 32 random bytes in base64. `host_id` is the companion's stable random id, the same value it advertises in mDNS.
- **`prove`**: `sig` is base64 DER ECDSA-P256-SHA256 over the UTF-8 text

  ```
  openmausbot-gadget/1
  prove
  <id>
  <nonce>
  <host_id>
  ```

  with `\n` line endings and no trailing newline. Signing `host_id` binds the proof to one MausBot. `enroll` is the six-digit pairing code. It is sent only while the gadget holds a code that has not yet been used.
- **Host rules:**
  1. Reject if `proto` is unsupported (`proto_unsupported`), or if `pubkey` does not hash to `id`, or if `sig` does not verify (`bad_sig`).
  2. **Known id:** `pubkey` must equal the stored key. Any `enroll` is ignored. Go to `ready`.
  3. **Unknown id with `enroll`:** validate and consume the code against the companion's existing pairing window (the same one `POST /api/pair` uses: 120 s, 5 attempts, single use). On success, create a gadget device record. The bot defaults to the person's default bot. Then go to `ready`. A bad code returns `bad_code`.
  4. **Unknown id without `enroll`:** `enroll_required`.
  5. If the device cap is reached: `device_limit`.
- **`ready`**:

  ```json
  {"op": "ready", "session": "s_81c2", "bot": {"id": "b_jev", "name": "Jev"},
   "settings": {"speak_pushes": false}}
  ```

  The gadget clears its stored code once it receives `ready`.
- **`settings {bot, settings}`** (host → gadget): sent whenever the person changes the gadget's bot or its settings in Devices. It has the same shape as those two fields in `ready`.
- **`error` codes:** `proto_unsupported`, `enroll_required`, `bad_code`, `bad_sig`, `revoked`, `device_limit`, `replaced` (another connection with the same id took over). The host closes the socket after sending one.

### 4.4 Conversation

| Message | Direction | Meaning |
|---|---|---|
| `voice.begin {turn, stream, rate}` | gadget → host | An utterance starts; mic frames follow on `stream`. `turn` is a gadget-chosen id, ≤ 32 chars |
| `voice.end {turn, ms}` | gadget → host | The utterance is complete |
| `voice.drop {turn}` | gadget → host | Discard it |
| `say {turn, text}` | gadget → host | A typed message from the console or simulator, ≤ 2000 chars |
| `stop {turn?}` | gadget → host | Stop the current turn |
| `heard {turn, text}` | host → gadget | What speech-to-text heard |
| `working {turn, text}` | host → gadget | A short live phrase about what the bot is doing; empty text clears it |
| `reply {turn, text, final}` | host → gadget | The bot's reply so far. `text` is cumulative; `final: true` marks the finished text |
| `done {turn, outcome, reason?}` | host → gadget | The turn is over. `outcome` is `ok`, `failed` or `stopped`; `reason` is short text for the screen |
| `speak.begin {stream, rate, turn?}` | host → gadget | Speech follows on `stream` |
| `speak.end {stream}` | host → gadget | All speech frames sent; play out the buffer |
| `speak.stop {stream}` | host → gadget | Stop playing now |

- Reply text is already shaped for the screen: Markdown is stripped, code blocks become `[code]`, emoji are dropped, and the text is folded to the gadget's `caps.screen.text` charset.
- The host paces speech frames to real time, at most 0.5 s ahead, so a 1 s jitter buffer on the gadget is enough.
- A new `speak.begin` replaces any stream that is playing.

### 4.5 Approvals

| Message | Direction |
|---|---|
| `ask {id, kind, title, body, options, expires_s?}` | host → gadget |
| `answer {id, option}` | gadget → host |
| `ask.close {id, reason}` | host → gadget |

- `kind` is `permission` (a tool wants to run) or `question` (the bot asks the person something).
- `options` holds 1–4 `{id, label, style?}` entries. `style` is `allow`, `deny` or `neutral` and drives the button color.
- The gadget answers at most once. `ask.close` reasons are `answered` (here or on another device), `expired` and `withdrawn`.
- The host sends one ask at a time per gadget, oldest first. Asks that are still open are sent again on reconnect.

### 4.6 Push

`post {id, bot, kind, text, speak}`, host → gadget:

- Something the bot produced on its own, shown as a toast with a chime. Replies to messages the person sent from the desktop or a phone are **not** pushed; the person is already looking at that screen.
- `kind` is `routine` (a routine's result), `digest`, or `message` (the bot wrote unprompted, for example when a background task or a teammate's handoff finished).
- `speak` follows the gadget's "Read pushes aloud" setting.

### 4.7 Display, actions and sensors

| Message | Direction | Meaning |
|---|---|---|
| `card {id, title, body, ttl_s}` | host → gadget | Show a card; `ttl_s: 0` keeps it until dismissed |
| `card.close {id}` | host → gadget | Remove it |
| `image.begin {stream, w, h, ttl_s}` / rows / `image.end {stream}` | host → gadget | An image no larger than `caps.image`, already converted to RGB565 |
| `act {id, name, args}` | host → gadget | Run a declared action |
| `act.result {id, ok, data?, error?}` | gadget → host | Exactly one per `act`; the host gives up after 15 s |
| `sense {…}` | gadget → host | Latest readings: `battery_pct`, `charging`, plus board-specific values. At most one every 10 s |
| `event {name, data?}` | gadget → host | Something happened on the gadget, for example `button.long_press` |

### 4.8 Firmware updates

```
host   → fw.offer     {stream, version, size, sha256, sig, key_id}
gadget → fw.ready     {stream}                      or  fw.fail {code}
host   → chunks on binary kind 0x04
gadget → fw.progress  {stream, offset}              every 16 KiB and at the end
host   → fw.commit    {stream}
gadget → (checks size, SHA-256 and signature, marks the new slot, restarts)
gadget → fw.installed {version}                     after its first `ready` on the new image
```

- `sig` is base64 DER ECDSA-P256-SHA256, made with the project release key, over:

  ```
  openmausbot-gadget/1
  firmware
  <board>
  <version>
  <size>
  <sha256 lowercase hex>
  ```

- `key_id` names one of the release public keys compiled into the firmware. Firmware may hold several keys, so the key can be rotated.
- **Accepting an offer:** only a session that passed `prove` may offer. The gadget answers with `fw.fail` if the image is too big for `caps.ota.max`, the board doesn't match, `key_id` is unknown, or `sig` is invalid.
- **Flow control:** the host keeps at most 64 KiB unacknowledged. Offsets must be contiguous, or the update fails with `sequence`.
- **Probation:** a new image boots on probation. If it doesn't reach `ready` within 5 minutes, or crashes first, the bootloader returns to the previous image.
- **Failure codes:** `too_large`, `wrong_board`, `unknown_key`, `bad_sig`, `busy`, `flash`, `sequence`, `checksum`, `timeout`.

### 4.9 Versioning and test vectors

- `proto` is an integer. Adding an op or an optional field does not change it; changing what an existing field means does.
- `protocol/vectors/` holds JSON vectors that both sides must pass:
  - id derivation from a fixed key;
  - the exact `prove` text and a signature that must verify;
  - a deterministic (RFC 6979) signature the firmware must reproduce;
  - binary frame encodings;
  - the firmware signature text.
- The firmware's C tests and OpenMausBot's TypeScript tests read the same files. OpenMausBot vendors a copy that its tests compare by hash.

## 5. Firmware and simulator

### 5.1 Repository layout

| Path | What |
|---|---|
| `protocol/` | `PROTOCOL.md` (§4 as the normative reference) and `vectors/` |
| `firmware/core/` | Portable C11: protocol codec (cJSON), session, interaction state, audio buffers, OTA state machine, console commands. Includes only `gadget_hal.h` |
| `firmware/ui/` | LVGL 9 screens and the Maus animation. Depends only on LVGL and core |
| `firmware/ports/esp32/` | The ESP-IDF application plus `boards/<board-id>/`: `board.h`, `board.c`, `sdkconfig.defaults` |
| `firmware/ports/sim/` | The desktop simulator: SDL2, LVGL's SDL backend, host audio, a small MIT-licensed WebSocket client |
| `firmware/tests/` | Native unit tests (CTest) against a fake HAL |
| `tools/fake-host/` | A Node stand-in for MausBot, for CI and offline work |
| `tools/art/` | Renders the Maus frames from SVG into LVGL image sources |
| `site/` | The browser installer (static, GitHub Pages) |
| `AGENTS.md` | Build, flash, monitor and add-a-board instructions for coding agents |

### 5.2 The HAL

`gadget_hal.h` is the whole contract between core and a port:

| Group | Functions |
|---|---|
| Mic | start and stop capture at 16 kHz; frames delivered as events |
| Speaker | open at a rate, write PCM, level, stop |
| Input | events: `talk_down/up`, `cancel_down/up`, `touch_down/move/up(x, y)`, `swipe(dir)` |
| Storage | key/value get, set, erase (NVS on ESP32, a file in the simulator) |
| Net | Wi-Fi state; WebSocket open, send, close; mDNS lookup of `_openmausbot._tcp` by `id` |
| Crypto | P-256 keygen, sign, verify; SHA-256; random bytes (mbedTLS on both) |
| OTA | open slot, write, finalize, set boot, mark valid, rollback state |
| Battery | percent and charging, or "not present" |
| System | monotonic ms clock, restart, log |

- The port runs `core_tick()` every 10 ms and delivers every event on that same thread. Core and UI are single-threaded and hold no locks.
- On ESP32, driver tasks post to a FreeRTOS queue that the main task drains.
- The UI only reads a `ui_model` that core publishes, and LVGL draws from it.

### 5.3 Boards

| Board id | Hardware | Screen | Audio | Input | Battery |
|---|---|---|---|---|---|
| `amoled-175c` | Waveshare ESP32-S3-Touch-AMOLED-1.75C: aluminum case, 32 MB flash, 8 MB PSRAM | 466×466 round AMOLED (CO5300), CST9217 touch | ES8311 + ES7210 dual mic, built-in speaker, echo-cancellation reference | touch, BOOT, PWR | AXP2101, battery bay |
| `amoled-175` | Waveshare ESP32-S3-Touch-AMOLED-1.75: 16 MB flash, 8 MB PSRAM | same panel and touch | same codecs, speaker connector | touch, BOOT | AXP2101 |
| `lcd-154` | Waveshare ESP32-S3-LCD-1.54: 16 MB flash, 8 MB PSRAM | 240×240 ST7789 | ES8311 + ES7210, onboard speaker | BOOT, PLUS | not used in v1 |
| `devkit` | ESP32-S3-DevKitC-1 N8R8, 2" ST7789 320×240, INMP441 mic, MAX98357A amp | 320×240 | I2S mic and amp | BOOT, one extra button | none |

- Pins and init sequences come from the vendors' schematics, datasheets and board-support components. They live in each board's `board.h`.
- Waveshare's and Espressif's components are fetched at build time under their own licenses, never copied into this repository.

### 5.4 Interaction

- **Touch boards:**
  - Hold anywhere to talk, release to send. A press under 300 ms is ignored.
  - Tap while it is speaking to stop.
  - Swipe down to cancel a recording, dismiss a card or stop a turn.
  - On the 1.75C, BOOT also acts as TALK and PWR as CANCEL.
- **Button boards:** TALK and CANCEL work the same way.
- **Barge-in:** TALK during speech stops playback and starts listening. The host sends `stop` for the old turn.
- **Approvals:**
  - Touch boards show one button per option, styled by `style`.
  - Button boards map TALK to the `allow` option and CANCEL to the `deny` option. An ask with more than two options shows "Answer on your Mac or phone".
  - Presses in the first 0.6 s after an ask appears are ignored.
- **Recording limit:** 60 s, with a countdown in the last 5 s.

### 5.5 Screens

The animated Maus fills every screen that isn't showing text. It is rendered from the app's mascot SVG sources into LVGL image frames, colored with the green palette, and its states mirror the phone apps' mascot states.

| Screen | Shows |
|---|---|
| Idle | Maus breathing and blinking; "Hi, I'm <bot name>" |
| Listening | Maus listening, a ring that follows mic level, the countdown near 60 s |
| Thinking | Maus thinking, the `heard` text, then the `working` phrase |
| Speaking | Maus talking in time with the audio level; the reply text pages itself |
| Reply | The finished reply. It returns to idle after 20 s or a tap |
| Ask | Title, body, option buttons |
| Card / Image | What a bot put up, until `ttl_s` or dismissal |
| Post | A toast over any screen, with a chime |
| Setup | "Pair me: MausBot → Settings → Remote access → Pair on this Wi-Fi", then enter the code in the installer |
| Offline | Wi-Fi or MausBot unreachable, with the reason and retry timing |
| Update | Progress bar during OTA |

- Round layouts keep text inside the circle's safe area.
- A battery arc sits at the top edge on boards with a battery.

### 5.6 Console

The USB serial console (and stdin in the simulator) accepts:

| Command | Does |
|---|---|
| `wifi <ssid> <password>` | Saves the network and connects |
| `host auto` / `host <address>[:port]` | Finds MausBot over mDNS (the default), or pins an address |
| `pair <code>` | Stores the six-digit code for the next handshake |
| `name <text>` | Sets the name shown in Devices |
| `status` | Prints Wi-Fi, host, id, pairing state, firmware, battery |
| `forget` | Erases the pairing, key, Wi-Fi and name |
| `reboot` | Restarts |

`host auto` resolves `_openmausbot._tcp` and picks the service whose TXT `id` matches the stored `host_id`. Before the first pairing it picks the only service it finds, or lists several and asks for `host <address>`.

### 5.7 Simulator

- `gadget-sim --board amoled-175c [--host <addr>] [--pair <code>]` opens a window the size of the board's screen, masked round where the board is round.
  - Mouse is touch, Space is TALK, Esc is CANCEL.
  - The Mac's mic and speakers stand in for the board's.
  - The console reads stdin.
- Each `--name` keeps its own key and settings under `~/.openmausbot-gadget/sim/<name>/`, so several simulated gadgets can be paired at once.
- `--headless --script <file>` runs scripted input with SDL's dummy drivers and writes PNG snapshots. CI and the docs' screenshots use it.
- The simulator's OTA slot is a pair of files: it verifies the signature, "restarts" by re-execing itself, and honors probation.

### 5.8 Browser installer (`site/`)

A static page published with GitHub Pages; Chrome or Edge, because it uses Web Serial.

1. Pick a board.
2. Connect over USB. The page flashes the board's image from the latest GitHub Release using Espressif's `esptool-js`, a build-time dependency.
3. Choose a Wi-Fi network and enter its password.
4. Open MausBot → Settings → Remote access → Pair on this Wi-Fi and type the six-digit code.
5. The page sends `wifi`, `host auto` and `pair` over the console, then watches `status` until the gadget reports it is paired.

### 5.9 Fake host (`tools/fake-host/`)

A Node script that speaks the protocol from the host side:

- enrolls with any six-digit code it prints;
- answers a voice turn with fixed text and a test tone;
- sends an ask and a post on command;
- runs an OTA of a supplied image signed with a test key.

It is the server for CI's end-to-end tests and for work without MausBot.

### 5.10 `AGENTS.md`

Written for coding agents, MausBot's own bots first. It covers:

- installing ESP-IDF;
- building and flashing each board;
- reading the serial log;
- running the simulator and the fake host;
- adding a board;
- adding an action with `gadget_action_register(name, description, schema, risk, handler)`.

The README's headline path is "ask your MausBot to flash my 1.75C".

## 6. MausBot host

All paths are in the OpenMausBot repository.

### 6.1 Companion: `companion/src/gadget/`

| File | Responsibility |
|---|---|
| `hub.ts` | Handles the `upgrade` for `/gadget` on the device listener (`:8810`). Checks the subprotocol, refuses an `Origin` header, keeps one session per gadget id (a newer connection gets the id; the older one receives `error replaced`) |
| `enroll.ts` | Runs the handshake rules in §4.3 with Node `crypto` (P-256). Consumes pairing codes from the existing pairing window. Reads and writes gadget records through the device registry |
| `session.ts` | Turns gadget ops into harness calls and harness events into gadget ops (§6.2) |
| `shape.ts` | Reply text → screen text: Markdown stripped, `[code]`, emoji dropped, charset folding |
| `audio.ts` | WAV packing for STT; pacing and resampling of speaker PCM |

**Registry changes (`devices.ts`):**

- `DeviceRecord` gains `kind: "phone" | "gadget"`. A missing value reads as `"phone"`, so existing records need no migration.
- Gadget records add `publicKey`, `board`, `firmware`, `botId`, `speakPushes` and `lastSensors`. They hold no token.
- Gadgets count toward the existing 20-device cap.
- Removing a gadget closes its live session with `error revoked`.

**mDNS:** the `_openmausbot._tcp` TXT record gains `id=<host_id>`, a random id created once and kept in companion state.

### 6.2 Session mapping

Every harness call goes through the same allowlisted routes and companion identity headers a paired phone uses. The gadget's registry id is the device id.

| Gadget op / harness event | What the hub does |
|---|---|
| `voice.end` | Packs the PCM as WAV, `POST /api/stt`, sends `heard`, then posts the text to the bot's main thread with `POST /api/bots/:botId/messages` |
| `say` | Same, without STT |
| `stop` | `POST /api/bots/:botId/interrupt` |
| `answer` | `POST /api/bots/:botId/respond` with the card's `requestId` and the chosen behavior (`allow`, `deny`, or `answer` with the option's label) |
| SSE `message` / `message.patch` on the turn's thread | `reply` (shaped, cumulative) |
| SSE `bot` with busy tasks | `working` with the task's short label |
| SSE `bot` busy → idle after the turn | `reply {final: true}`, then speech, then `done` |
| SSE card with `requestId` (`permission` or `question`) | `ask`; the card resolving becomes `ask.close` |
| SSE assistant message in the bot's thread that answers no person's message (routine results, digests, finished background tasks, handoffs) | `post`. Replies to messages sent from the desktop or a phone are not pushed |

- The hub keeps **one** harness SSE subscription (`GET /api/events?screens=off`) and fans frames out to the connected gadgets by bot and thread. It resumes with `Last-Event-ID` after a harness restart.
- **Speech:** once a reply is final, the hub calls `POST /api/tts/prepare` to split it. It then calls `POST /api/tts/speak` with `format: "pcm_<rate>"` for each utterance and starts playing the first while the rest synthesize. It uses the bot's voice, falling back to the default voice. With no TTS configured, replies are text only, and the gadget shows a one-time notice.
- If the bot is busy with another turn, MausBot queues the message, and the hub sends `working "Queued behind another task"`.

### 6.3 Harness changes

- **`POST /api/stt`** (new route module `server/routes/stt.ts`; allowlisted in the companion):
  - **Input:** `audio/wav`, mono PCM16, at most 60 s.
  - **Output:** `{text, provider}`.
  - **macOS:** the Speech helper (`electron/resources/speech-helper.swift`) gains a **file mode**, `--file <path>`. It uses `SFSpeechURLRecognitionRequest`, on-device when supported, and prints one final NDJSON line.
  - **Fallback:** ElevenLabs Scribe, using the ElevenLabs key MausBot already has (the person's own or the Cloud Pro included key). It is used on Windows, when Speech permission is missing, or when the helper fails.
  - **Neither available:** `409 {error: "stt_unavailable"}`. The gadget then shows "Speech-to-text isn't set up: MausBot → Settings → Voice".
- **`POST /api/tts/speak`** accepts an optional `format`: `mp3` (default, unchanged), `pcm_16000` or `pcm_24000`.
  - ElevenLabs returns PCM natively.
  - Other providers' MP3 or WAV is decoded (a WASM MP3 decoder) and resampled in the harness.
  - The PCM response is `audio/L16; rate=<rate>; channels=1`.

### 6.4 Desktop UI: Settings → Devices

Gadget rows in `src/components/CompanionSection.tsx` show:

- a gadget icon, board name, firmware version and battery;
- an online dot;
- **Talks to [bot ▾]**;
- **Read pushes aloud**;
- **Update** or "Custom build" (§8);
- **Remove**.

Changing the bot or the toggle updates the record and reaches a live gadget as a `settings` op (§4.3).

The iOS and Android apps' device lists must render `kind: "gadget"` records, at minimum with a name and a gadget icon. This is checked for platform parity before the pull request.

### 6.5 Verify before implementing

Each item below has a default. The sub-project 3 plan confirms it against the code first and records the outcome.

1. **Reaching the Speech helper from the harness.** Default: Electron gives the harness the helper's path, and the harness launches it in file mode the same way `electron/speech.mjs` does (`open -n -g -W`), so the app's Speech permission applies.
2. **"Main thread".** Default: the thread a phone opens for that bot when it pins no thread id.
3. **Control port authentication for bot tools (§7).** Default: a random secret the companion generates at start, passed to the harness by Electron the way the companion's existing relay token is.
4. **Image conversion for `gadget_display`.** Default: the harness's existing image handling if it decodes PNG and JPEG; otherwise `pngjs` and `jpeg-js` (MIT) plus a box-filter resize.
5. **The default bot for a new gadget.** Default: the bot at the top of the desktop sidebar.
6. **The pairing window.** Default: the code shown by Settings → Remote access → Pair on this Wi-Fi comes from the same window `POST /api/pair` consumes. Confirm that consuming it from the hub needs no change to the HTTPS or Tailscale flows.
7. **Telling pushes from replies.** Default: an assistant message counts as a reply when its turn began with a person's message (from any device). Confirm which fields on the message or the bot's busy frames carry that.

## 7. Bot tools

Bots get three tools in `server/drivers/agents-catalog.ts`. They are visible to every bot while at least one gadget is paired, through a new `gadgets` profile flag. The catalog's wire goldens and size budget are updated with them.

| Tool | Arguments | Does |
|---|---|---|
| `gadget_devices` | none | Lists gadgets: id, name, board, online, battery, screen and image size, actions with their schemas and risk, latest sensors |
| `gadget_display` | `device?`, then either `title` + `body` + `ttl_s?` or `image_path` + `ttl_s?` | Shows a card or an image. `device` defaults to the only gadget, else the one talking to this bot, else it errors and lists the choices |
| `gadget_action` | `device?`, `name`, `args` | Runs a declared action and returns its `act.result` |

- **Path:** the harness's tool executor calls the companion's loopback control port (`:8811`, authenticated per §6.5 item 3), and the companion forwards to the gadget's live session. An offline gadget returns an error the bot can relay.
- **Safety:** `gadget_action` on a `confirm` action goes through MausBot's normal permission card before it runs. Display tools and `safe` actions don't ask.

## 8. Firmware releases and OTA delivery

- **Release:** pushing a `v*` tag runs CI. It builds every board, signs each image (§4.8) with the project release key, and publishes a GitHub Release with the images and `manifest.json`:

  ```json
  {"version": "1.1.0",
   "boards": {"amoled-175c": {"url": "…/openmausbot-gadget-amoled-175c-1.1.0.bin",
                              "size": 1234567, "sha256": "…", "sig": "…", "key_id": "r1"}}}
  ```

- **Keys:** the release private key exists only as a GitHub Actions secret. Its public half, with `key_id`, is compiled into the firmware and into OpenMausBot.
- **Checking:** while at least one gadget is paired, MausBot fetches `releases/latest/download/manifest.json` from this repository once a day and when Settings → Devices opens. A gadget whose board has a newer version shows **Update available**.
- **Updating** (a manual button in v1):
  1. The companion downloads the image.
  2. It checks the size, SHA-256 and signature.
  3. It runs §4.8 with the gadget.
  4. The row shows progress, then the new version once `fw.installed` arrives.
- **Custom builds:** firmware built locally carries a version ending in `-dev`. Its row shows **Custom build** with no Update button, so official releases never overwrite a maker's firmware. Custom builds update over USB.

## 9. Security model

| Threat | Mitigation |
|---|---|
| A web page opens a gadget socket | `Origin` header refused |
| A device impersonates a paired gadget | id derives from the public key; every session proves possession of the private key over a fresh nonce bound to `host_id` |
| A recorded session is replayed | Fresh 32-byte nonce per connection |
| Someone on the LAN pairs a gadget | Needs a valid six-digit code from the desktop's pairing window (120 s, 5 attempts, single use) |
| A gadget does more than a phone could | The hub calls the harness only through the companion's allowlisted routes with the gadget's own device identity |
| A bot flips a relay unexpectedly | `confirm` actions go through the permission card; unknown risk means confirm |
| A malicious host or image flashes the gadget | The image must be signed by an embedded release key *and* offered by the enrolled host; probation and rollback |
| Conversation content on the LAN | Plain `ws://` on the home network in v1, like the companion's existing LAN HTTP. Encrypted transport is out of scope for v1 |

## 10. Testing

| Layer | Tests |
|---|---|
| Protocol | Vectors (§4.9) pass in the firmware's C tests and OpenMausBot's TypeScript tests |
| Firmware core | CTest unit tests against a fake HAL: handshake and enrollment, every op, jitter buffer, OTA state machine, console parsing |
| UI | Headless simulator snapshots of every screen at 466 round, 240×240 and 320×240, compared to committed PNGs |
| Simulator end to end | Scripted runs against `tools/fake-host`: enroll, voice turn, barge-in, ask/answer, post, card, action, OTA, forced rollback |
| Firmware builds | CI builds all four boards in the ESP-IDF container and checks image size against the OTA slot |
| Companion | Enrollment and signature tests on the vectors; hub ↔ fake-harness tests for every row of §6.2 |
| Harness | `/api/stt` with a fixed WAV through both providers (the cloud one mocked); `/api/tts/speak` PCM formats; catalog goldens |
| End to end | The simulator against an isolated development MausBot instance with a fake engine: talk, approval, push, stop, a bot tool, an update |
| UI | Playwright for the Devices gadget row |
| Hardware | A manual checklist per board: boot log, touch and buttons, mic level, speaker, battery, pairing, a voice turn, an approval, OTA, power loss during OTA, rollback |

## 11. Original-work rule

Everything in this repository is written for this project. Contributors and coding agents must not copy code, documentation, art or protocol text from other gadget SDKs or device firmware projects, and the repository does not refer to them.

Hardware bring-up uses vendor datasheets, schematics and the vendors' published board-support components, which are fetched at build time under their own licenses. Third-party libraries (ESP-IDF and its components, LVGL, SDL2, cJSON, mbedTLS, esptool-js, the simulator's WebSocket client) are dependencies, listed with their licenses in `THIRD_PARTY.md`.
