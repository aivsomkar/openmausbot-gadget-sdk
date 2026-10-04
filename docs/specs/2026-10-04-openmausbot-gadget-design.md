# OpenMausBot Gadget: design

- **Status:** v1.1 — amended after plan research and spec review (2026-10-04)
- **Date:** 2026-10-04
- **Scope:** this repository (firmware, simulator, installer, protocol) and the matching host support in the OpenMausBot desktop app

### Changes in v1.1

Plan research checked v1.0 against OpenMausBot `origin/main` (6dd4403) and vendor sources. These changes follow from it:

- **Remote access and pairing:** the hub runs only while Remote access is on. A new "Pair a gadget" button in Settings → Remote access shows the code and picks the bot. Gadget rows live in Remote access, in a Gadgets group under that button. The phones need no changes.
- **Routing and push:** a gadget talks in its bot's currently selected thread, pinned per turn. The default bot comes from the picker or a fixed rule. `post.kind` is `routine` or `message`. Steered and queued sends are both handled.
- **Approvals and safety:** a gadget answers permission cards with Allow or Deny only, never Always allow. `confirm` gadget actions are never auto-approved, also in Full access. They ask through the harness's own approval card unless the person chose Always allow for that action on the desktop.
- **Speech:** the STT fallback is ElevenLabs Scribe v2 with the person's own key. The Apple helper gains a file mode that never prompts. TTS asks each provider for PCM or WAV natively, with no MP3 decoding. Codec boards play speech at 16 kHz.
- **Toolchain:** ESP-IDF v6.0.3 (still buildable on v5.5.5), LVGL 9.6.0 everywhere, our own driver glue with no Waveshare BSP, cJSON from `espressif/cjson`, PSA-only crypto.
- **Hardware:** the key lives in plain NVS, and encryption is opt-in. The 1.75C builds as 16 MB. The devkit becomes the N16R8. v1 does not use echo cancellation. PWR and lcd-154 battery details are pinned. Firmware runs its own probation timer.
- **Art and fonts:** the Maus is generated from the app's mascot geometry as layered images, adds a new speaking state, and moves by translation only. Latin-1 fonts come from Montserrat.
- **Console and simulator:** new `scan` and `log` commands, machine-readable `@omb` lines and quoting rules. The headless simulator has no SDL and runs on a virtual clock. The simulator's WebSocket client is wslay.
- **Protocol pinning:** exact base64, `host_id`, number, hash and version encodings. Unique turn ids and hub `sendId`. Vector generator and verifier rules. `event` is informational.
- **Host internals:** a hand-rolled RFC 6455 server in the companion, a gadget-aware device registry, harness client rules, the `/api/stt` route module, a control token minted by Electron, harness-side tool routes, catalog overlays, image libraries, and a Codex timeout fix.
- **Installer and release:** firmware is served same-origin from the Pages site. Separate parts are flashed so NVS survives. The installer has its own reset-to-app step. Signing keys are kept apart, and prereleases are marked.
- **Licensing:** the trademark owner is Supamaus Software Private Limited, and the art's provenance is recorded.

## 1. What we are building

A small ESP32 board with a screen, a microphone and a speaker becomes a desk terminal for your own MausBot. Hold to talk, and your words go to the bot running on your Mac. The bot's reply appears on the screen and is spoken back. The gadget also shows the bot's approval questions so you can answer them with a tap, shows results that routines and background work push to it, and gives bots a few tools to drive it.

This repository is an **open-source SDK** for people who build their own: firmware for four boards, a desktop simulator that runs the same UI, a browser installer, and the protocol spec. The host side ships inside OpenMausBot itself. Users install MausBot, turn on Remote access, flash a board and pair it. There is nothing else to run.

### Goals

1. Talking to a bot from a $40 board feels as quick and natural as a voice note on the phone.
2. A gadget is a first-class MausBot device. It pairs with the same six-digit code as the companion apps, appears in Settings → Remote access next to paired phones, and Remove revokes it.
3. Makers can build it with no prior ESP32 experience: browser installer, simulator, `AGENTS.md` for coding agents.
4. Everything works on the local network with no cloud service beyond what MausBot already uses. Nothing else to run, as long as Remote access is on in MausBot.

### Non-goals for v1

- Pairing or setup from the phone apps, over BLE or otherwise. The phones are already companion apps; gadgets pair with the desktop's six-digit Wi-Fi pairing code.
- Device lists in the phone apps. The phones have none today, and gadgets add none.
- Self-hosted Docker servers and Cloud home. v1 targets the desktop app's companion.
- Reaching the gadget from outside the LAN.
- Wake word, always-listening mode, on-device speech recognition.
- Echo cancellation. The 1.75C's loopback reference is a v2 item.
- Automatic firmware updates, and over-the-air updates of custom builds.
- A Linux or Raspberry Pi gadget.

## 2. Decisions

| Topic | Decision |
|---|---|
| Code origin | All code, docs and art are original work for this project. See [§11](#11-original-work-rule). |
| Host | A gadget hub inside the MausBot companion (port 8810, path `/gadget`). The companion already owns pairing, the device registry and network exposure. It runs only while **Remote access** is on. |
| Protocol | `openmausbot-gadget/1`, defined here: one WebSocket, JSON control frames plus binary media frames. |
| Identity | Each gadget holds a P-256 key pair in NVS. The Mac stores only public keys. |
| Pairing | The six-digit code from MausBot → Settings → Remote access → **Pair a gadget**, entered in the browser installer, the serial console or the simulator. |
| Firmware | C11 on **ESP-IDF v6.0.3** (kept buildable on v5.5.5), **LVGL 9.6.0** UI with an animated color Maus. Our own driver glue on Espressif's and Waveshare's driver components; no Waveshare BSP and no `esp_lvgl_port`. |
| Boards | Waveshare ESP32-S3-Touch-AMOLED-1.75C (the hero board), ESP32-S3-Touch-AMOLED-1.75, ESP32-S3-LCD-1.54, and an **ESP32-S3-DevKitC-1-N16R8** breadboard build. |
| Simulator | The same core and UI on the desktop: an SDL2 window for people, and an in-memory display on a virtual clock for CI. |
| Speech-to-text | On the Mac: Apple's on-device recognizer through the Speech helper's file mode. ElevenLabs **Scribe v2** as the fallback and on Windows, with the person's own ElevenLabs key. |
| Text-to-speech | MausBot's existing providers, each asked for 16 or 24 kHz PCM or WAV natively. No MP3 decoding. |
| Routing | Each gadget talks to one bot (chosen when pairing, changeable in Remote access) in that bot's **currently selected thread**. |
| v1 features | Talk and listen, approvals on the gadget, push, bot tools, battery level, over-the-air updates of official releases. |
| License | Apache-2.0, matching OpenMausBot. The OpenMausBot name, the MausBot name and the Maus mascot are trademarks of Supamaus Software Private Limited. The README, `NOTICE` and `tools/art/source/README.md` repeat this sentence (§11). |

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

The hub lives in the companion, which is off by default. It runs only while Remote access is on in MausBot, and an organization policy can block it. The gadget's Setup and Offline screens, the installer and the README all say so.

The hub talks to the harness exactly as a paired phone does: through the companion's allowlisted routes, with the gadget's registry id in the companion identity headers. The hub runs in-process, so it checks the allowlist itself before every call (§6.2). A gadget gets no authority a phone doesn't already have.

### Sub-projects and order

| # | Sub-project | Where | Depends on |
|---|---|---|---|
| 1 | Protocol: [§4](#4-protocol-openmausbot-gadget1) as `protocol/PROTOCOL.md` plus test vectors | this repo | none |
| 2 | Firmware, simulator, installer, fake host: [§5](#5-firmware-and-simulator) | this repo | 1 |
| 3 | MausBot host: hub, enrollment, STT, PCM TTS, Remote access UI: [§6](#6-mausbot-host) | OpenMausBot | 1 |
| 4 | Bot tools and OTA delivery: [§7](#7-bot-tools), [§8](#8-firmware-releases-and-ota-delivery) | OpenMausBot | 2, 3 |

Sub-projects 2 and 3 run in parallel once the protocol and its vectors exist. Each sub-project gets its own implementation plan and branch.

**Ownership.** Some pieces are described in one section but built by another sub-project. Each has exactly one owner:

- **Sub-project 2** also owns `release.yml`, `pages.yml`, the `install.json` (§5.8) and `manifest.json` (§8) formats, the test signing key and the CI check of the release key table.
- **Sub-project 3** owns the hub, `ws.ts`, enrollment, the registry changes, `host_id` and the mDNS TXT record, `/api/stt`, PCM TTS, `PATCH /devices/:id/gadget`, `POST /pairing {botId}`, the Pair a gadget panel, and the gadget rows without the Update cell.
- **Sub-project 4** owns `gadgetControlToken` (§6.5 item 3), the `/gadget/*` control routes, the presence notice, `/api/internal/gadgets*`, the catalog overlay, the Codex and Claude tool timeouts, `releases.ts`, `POST /devices/:id/firmware-update`, `POST /firmware-updates/check` (Electron asks the companion to check for updates when Remote access opens), and the Update / Custom build cell.

## 4. Protocol `openmausbot-gadget/1`

### 4.1 Transport

- One WebSocket per gadget: `ws://<host>:8810/gadget`, subprotocol `openmausbot-gadget.1`. LAN only in v1: the host serves `/gadget` on its LAN listener and nowhere else.
- The server rejects any upgrade that carries an `Origin` header, so a web page cannot open a gadget session. It accepts no extensions (it declines `permessage-deflate`).
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

**Encodings.** These are normative, and the vectors (§4.9) check them.

| Value | Encoding |
|---|---|
| `pubkey`, `nonce`, `sig` | Base64 per RFC 4648 §4, standard alphabet, with padding. Hosts reject non-canonical base64: the value must survive a decode and re-encode unchanged. |
| `pubkey` bytes | Exactly 65 bytes, starting `0x04` (SEC1 uncompressed). Anything else is rejected. |
| `<nonce>` in signed text | The exact base64 string sent in `challenge` (44 characters), not the decoded bytes. |
| `host_id` | 32 lowercase hex characters, `/^[0-9a-f]{32}$/`. The gadget rejects a `challenge` whose `host_id` fails this check. |
| `<size>` | Base-10, no leading zeros. |
| SHA-256 | Lowercase hex everywhere (`fw.offer`, the signed firmware text, `manifest.json`). |
| Firmware version | The git tag without the leading `v`, for example `1.1.0`. Custom builds end in `-dev`, and a local build without `PROJECT_VER` reports `0.0.0-dev` (§5.1). Comparison and the custom-build rule are in §8. |
| `turn` | Unique per boot, ≤ 32 characters, for example `t` + 8 random hex + `-` + a counter (`t3f9a0c2b-7`). |

### 4.2 Identity

- On first boot, once Wi-Fi has started (so the hardware RNG is truly random), the gadget generates a P-256 key pair.
- It keeps the 32-byte private scalar in **NVS**: namespace `gadget`, blob `dev_key`. The simulator keeps it in its state file.
- NVS is not encrypted by default. The opt-in Kconfig option `GADGET_NVS_ENCRYPT` turns on HMAC-based NVS encryption with eFuse key block 5. The docs warn that this permanently burns an eFuse.
- Erasing flash erases the key, and the gadget then has to pair again. The installer flashes separate parts so NVS survives a reinstall (§5.8).
- `pubkey` is the base64 SEC1 uncompressed point (65 bytes).
- `id` is `gad_` followed by the first 16 lowercase hex characters of SHA-256(pubkey bytes).
- Because the id derives from the key, no device can claim another's id.
- A lost or stolen gadget is handled by **Remove** in Settings → Remote access (§9).

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
            "mic": {"rate": 16000}, "speaker": {"rate": 16000},
            "input": ["touch", "talk", "cancel"], "battery": true,
            "ota": {"max": 6291456}},
   "actions": [{"name": "chime", "description": "Play a short chime.",
                "params": {"type": "object", "properties": {}}, "risk": "safe"}],
   "sensors": {"battery_pct": 82, "charging": false}}
  ```

  - Every `caps` member is optional. A gadget without a speaker omits `speaker` and never receives speech.
  - `speaker.rate` is 16000 or 24000. The host treats any other value as no speaker.
  - **Rate rule:** a board whose mic and speaker codecs share one I2S clock must advertise `speaker.rate` equal to `mic.rate` (16000). The three Waveshare boards do. A board with separate I2S controllers, like the devkit, may advertise 24000.
  - `mic.rate` is 16000 in v1, because speech-to-text accepts only 16 kHz (§6.3).
  - `ota.max` is the board's OTA slot size in bytes.
  - `risk` is `safe` or `confirm`, and a missing value means `confirm`.
  - **Limits:** `name` ≤ 32 characters; at most 16 actions; each `description` ≤ 200 characters; each `params` ≤ 1 KiB serialized; the whole `hello` ≤ 16 KiB (the text frame limit). `gadget_action_register` enforces them in the firmware. The host cuts `name` to 32 characters and drops any action that breaks a limit.
  - The `name` the gadget sends follows the last-writer-wins rule below.

- **`challenge`**: `nonce` is 32 random bytes in base64. `host_id` is the companion's stable random id (§4.1 encodings), the same value it advertises as `id=` in mDNS. `host_name` is the Mac's display name. The gadget shows it on the Setup and Offline screens.
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
  1. Reject if `proto` is unsupported (`proto_unsupported`). Reject with `bad_sig` if `pubkey` is not canonical base64 of 65 bytes starting `0x04`, if it does not hash to `id`, or if `sig` does not verify.
  2. **Known id:** `pubkey` must equal the stored key. Any `enroll` is ignored. Go to `ready`.
  3. **Unknown id with `enroll`:** validate and consume the code against the companion's existing pairing window. It is the same window `POST /api/pair` uses: 120 s, 5 attempts, single use. Only `/^\d{6}$/` is accepted.
     - A wrong, expired or used-up code returns `bad_code`, and `message` says which. Wrong gadget codes use up the same 5 attempts as phones.
     - The device cap is checked only after the code matches. If the registry is full, the host returns `device_limit` and the window stays open, so the person can remove a device and retry.
     - On success, the host creates a gadget device record, consumes the window and goes to `ready`.
     - The new gadget's bot is the one picked in the desktop's "Pair a gadget" picker. If the code came from the plain Wi-Fi pairing flow (no picker), the hub takes the first match over the visible bots from `GET /api/bots`:
       1. the first chief-of-staff bot without a section;
       2. else the first pinned bot;
       3. else the first unsectioned bot that is not chief of staff;
       4. else the first visible bot.
  4. **Unknown id without `enroll`:** `enroll_required`.
- **`ready`**:

  ```json
  {"op": "ready", "session": "s_81c2", "bot": {"id": "b_jev", "name": "Jev"},
   "settings": {"speak_pushes": false}}
  ```

  On `ready`, the gadget stores `host_id` (storage key `host_id`) and `host_name`, and clears its stored code.
- **`settings {bot, settings, name?}`** (host → gadget): sent whenever the person changes the gadget's bot, its settings or its name in Settings → Remote access. `bot` and `settings` have the same shape as in `ready`. The gadget stores `name` and sends it in later `hello`s.
- **Name, last writer wins.** The name can be changed on the desktop (`PATCH /devices/:id/gadget`, which reaches a live gadget as `settings {name}`) and on the gadget (console `name`). On every `hello`, the hub updates the record's `name`, `board`, `firmware` and `lastSeenAt` from the hello. The one exception: a desktop rename made while the gadget was offline is marked pending, so at the next `hello` the hub keeps the record's name and sends it in `settings` right after `ready`.
- **`error` codes:** `proto_unsupported`, `enroll_required`, `bad_code`, `bad_sig`, `revoked`, `device_limit`, `replaced` (another connection with the same id took over). The host closes the socket after sending one.
- **How the gadget reacts.** The `pair` and `error` values are the console's `@omb` status fields (§5.6).

  | Event | Gadget |
  |---|---|
  | `bad_code` | Clears the stored code; `pair` = `error`. No retry until a new `pair` command |
  | `enroll_required` | `pair` = `unpaired`; Setup screen. No retry until `pair` |
  | `device_limit` | Keeps the code and retries every 10 s until 120 s after the `pair` command, then clears it |
  | `revoked` | Clears the stored `host_id`; `pair` = `unpaired`; Setup screen. No automatic reconnect |
  | `replaced` | Shows "In use elsewhere". No automatic reconnect until reboot or TALK |
  | `proto_unsupported`, `bad_sig` | `pair` = `error`; reconnects with backoff |
  | A `challenge` whose `host_id` fails the §4.1 check | Closes the socket; reconnects with backoff |
  | A failed connect or a dropped session | Reconnects with backoff |
  | `ready` | Stores `host_id`, clears the code, resets the backoff |

  - **Backoff:** 2, 4, 8 … s, capped at 60 s. The Offline screen shows the next retry.
  - A `pair <code>` command stores the code and reconnects at once.

### 4.4 Conversation

| Message | Direction | Meaning |
|---|---|---|
| `voice.begin {turn, stream, rate}` | gadget → host | An utterance starts; mic frames follow on `stream`. `turn` is a gadget-chosen id, unique per boot (§4.1). `rate` is 16000 in v1 |
| `voice.end {turn, ms}` | gadget → host | The utterance is complete |
| `voice.drop {turn}` | gadget → host | Discard it |
| `say {turn, text}` | gadget → host | A typed message from the console's `say` command (§5.6) or the simulator, ≤ 2000 chars |
| `stop {turn?}` | gadget → host | Stop the current turn (§6.2 says how the hub stops it) |
| `heard {turn, text}` | host → gadget | What speech-to-text heard |
| `working {turn, text}` | host → gadget | A short live phrase about what the bot is doing; empty text clears it |
| `reply {turn, text, final}` | host → gadget | The bot's reply so far. `text` is cumulative; `final: true` marks the finished text |
| `done {turn, outcome, reason?}` | host → gadget | The turn is over. `outcome` is `ok`, `failed` or `stopped`; `reason` is short text for the screen |
| `speak.begin {stream, rate, turn?}` | host → gadget | Speech follows on `stream` |
| `speak.end {stream}` | host → gadget | All speech frames sent; play out the buffer |
| `speak.stop {stream}` | host → gadget | Stop playing now |

- Reply text is already shaped for the screen:
  - Markdown markers are stripped, while newlines and link labels are kept.
  - Code blocks become `[code]`, images `[image]` and bare URLs `[link]`.
  - Emoji are dropped, and the text is folded to the gadget's `caps.screen.text` charset.
- **Every string the host sends for the screen is folded** to `caps.screen.text`: `heard`, `working`, `reply`, the `done` reason, the `ask` title, body and labels, `post` text, the `card` title and body, and `ready.bot.name`. `ask`, `post` and `card` bodies also get the reply shaping above.
  - In v1 the only value is `"latin1"`, which is also the default when `screen` or `text` is missing. It means Latin-1 plus exactly two more code points, U+2026 `…` and U+2192 `→`, which the fonts include (§5.5).
- The host sends at most one `reply` every 250 ms. A `reply` that would exceed the frame limit is cut from the start with `…`.
- If speech-to-text hears nothing, the turn ends with `done {outcome: "failed", reason: "Didn't catch that"}` and nothing is sent to the bot.
- A `voice.begin` whose `rate` is not 16000 ends the turn with `done {outcome: "failed", reason: "Unsupported mic rate"}`.
- The host paces speech frames to real time, at most 0.5 s ahead, so a 1 s jitter buffer on the gadget is enough.
- A new `speak.begin` replaces any stream that is playing.
- **One turn in flight.** A turn is in flight from its `voice.begin` or `say` until the host sends its `done`.
  - The host ignores `stop` for a turn that is not in flight.
  - A `voice.begin` or `say` that arrives while a turn is in flight stops the old turn the same way `stop` does. The host sends `speak.stop` for the old turn's speech stream and `done {turn: <old>, outcome: "stopped"}` before the new turn's first message.
- **Disconnects.** If the socket drops during a turn (while recording, during speech-to-text or while waiting for the bot), the gadget ends the turn locally with "Connection lost" and never resumes it.
  - The hub drops any buffered mic audio, and nothing is sent to the bot if the message had not been sent yet.
  - If the message already reached the bot, the bot's turn keeps running and the hub unbinds it. Its reply is not pushed (§4.6); the person reads it on the desktop or a phone.

### 4.5 Approvals

| Message | Direction |
|---|---|
| `ask {id, kind, title, body, options, expires_s?}` | host → gadget |
| `answer {id, option}` | gadget → host |
| `ask.close {id, reason}` | host → gadget |

- `kind` is `permission` (a tool wants to run) or `question` (the bot asks the person something).
- `options` holds 0–4 `{id, label, style?}` entries. `style` is `allow`, `deny` or `neutral` and drives the button color.
- **Permission asks always carry exactly two options:** `{id: "allow", label: "Allow", style: "allow"}` and `{id: "deny", label: "Deny", style: "deny"}`. "Always allow" is never offered on a gadget, so a gadget can never create a standing grant.
- **Question asks** carry options only for:
  - cards with no structured question list, where the options are the card's choices (`neutral`);
  - cards with exactly one single-select question, where the options are its choices (`neutral`).

  A card or single-select question with more than 4 choices is treated as unsupported. Choices are never cut down.
- **Unsupported asks.** Anything else is sent as `ask {id, kind: "question", title, body, options: []}`. The gadget shows the title and "Answer on your computer or phone" with no buttons, and `ask.close` removes it as usual. It takes its turn in the one-at-a-time order like any other ask.
- The gadget answers at most once. `ask.close` reasons are `answered` (here or on another device), `expired` and `withdrawn`.
- The host sends one ask at a time per gadget, oldest first. Asks that are still open are sent again on reconnect.

### 4.6 Push

`post {id, bot, kind, text, speak}`, host → gadget. `bot` is `{id, name}`, as in `ready`.

- Something the bot produced on its own, shown as a toast with a chime. Replies to a person's message from any device (desktop, phone or gadget) are **not** pushed; the person is already looking at that screen.
  - A **person's message** is a user message the person typed or spoke on any device. A message another bot delivered (a peer ask) and a routine's prompt are not a person's message.
  - A late reply to this gadget's own turn, for example after the turn ended or the gadget disconnected, answers a person's message, so it is not pushed either.
- At most one `post` per bot turn. §6.2 has the exact rule.
- `kind` is one of:
  - `routine`: a routine run for the gadget's bot completed, failed, was missed or is waiting on the person;
  - `message`: the bot wrote unprompted, for example when a background task or a teammate's handoff finished.
- `speak` follows the gadget's "Read pushes aloud" setting.

### 4.7 Display, actions and sensors

| Message | Direction | Meaning |
|---|---|---|
| `card {id, title, body, ttl_s}` | host → gadget | Show a card; `ttl_s: 0` keeps it until dismissed |
| `card.close {id}` | host → gadget | Remove the card or image with that `id` |
| `image.begin {id, stream, w, h, ttl_s}` / rows / `image.end {stream}` | host → gadget | An image no larger than `caps.image`, already converted to RGB565 |
| `act {id, name, args}` | host → gadget | Run a declared action |
| `act.result {id, ok, data?, error?}` | gadget → host | Exactly one per `act`; the host gives up after 15 s |
| `sense {…}` | gadget → host | Latest readings: `battery_pct`, `charging`, plus board-specific values. At most one every 10 s |
| `event {name, data?}` | gadget → host | Something happened on the gadget, for example `button.long_press` |

- `event` is **informational** in v1. The host keeps the last 10 events per gadget in memory and shows them to bots as `recent_events` in `gadget_devices` (§7). Nothing else reacts to them.
- The host keeps `sense` values in memory and persists them at most every 60 s.

### 4.8 Firmware updates

```
host   → fw.offer     {stream, board, version, size, sha256, sig, key_id}
gadget → fw.ready     {stream}                      or  fw.fail {stream, code}
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

  `<size>`, `<sha256>` and `<version>` use the encodings in §4.1.
- `key_id` names one of the release public keys compiled into the firmware. Firmware may hold several keys, so the key can be rotated. Release keys have ids starting with `r` (`r1`, …).
- **Accepting an offer:** only a session that passed `prove` may offer. The gadget answers with `fw.fail`:
  - `wrong_board` when `board` is not its own board id;
  - `same_version` when `version` equals the running version;
  - `too_large` when `size` exceeds `caps.ota.max`;
  - `unknown_key` when `key_id` is unknown;
  - `bad_sig` when `sig` does not verify over the text built with the gadget's **own** board id.

  An older signed version is accepted, since anti-rollback is off.
- **Flow control:** the host keeps at most 64 KiB unacknowledged. Offsets must be contiguous, or the update fails with `sequence`.
- **Timeouts:** the host gives up if `fw.ready` doesn't arrive within 10 s. The gadget fails with `timeout` if no chunk arrives for 30 s, or if `fw.commit` doesn't arrive within 30 s of the last byte.
- **Probation:**
  - A new image boots on probation. The firmware runs its own 5-minute timer; the bootloader has none.
  - If the image hasn't reached `ready` when the timer fires, the firmware calls `esp_ota_mark_app_invalid_rollback_and_reboot()`.
  - On the first `ready` it calls `esp_ota_mark_app_valid_cancel_rollback()` and then sends `fw.installed`.
  - Any reboot before that point, including a crash, the watchdog or power loss, returns to the previous image.
  - Anti-rollback is never enabled.
- **Failure codes:** `too_large`, `wrong_board`, `same_version`, `unknown_key`, `bad_sig`, `busy`, `flash`, `sequence`, `checksum`, `timeout`.

### 4.9 Versioning and test vectors

- `proto` is an integer. Adding an op or an optional field does not change it; changing what an existing field means does.
- `protocol/vectors/` holds JSON vectors that both sides must pass:
  - id derivation from a fixed key;
  - the exact `prove` text and its deterministic signature, with pinned inputs: the RFC 6979 A.2.5 P-256 private key, `nonce` = `AAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8=` (bytes 0x00–0x1f) and `host_id` = `000102030405060708090a0b0c0d0e0f`;
  - deterministic (RFC 6979) signatures the firmware must reproduce, including the RFC 6979 A.2.5 P-256/SHA-256 "sample" vector for cross-checking;
  - **high-S** signatures that must verify. With the inputs above, the RFC "sample" signature and the `prove` signature are both high-S. (Whether a `prove` signature is high-S depends on the `host_id` chosen: `0123456789abcdef0123456789abcdef` gives a low-S one, so the inputs are pinned.) The generator asserts `Signature.fromBytes(der, "der").hasHighS()` for every vector marked high-S;
  - a short-DER signature (69 bytes);
  - negative cases: a changed `host_id`, a `pubkey` that does not hash to `id`, non-canonical base64;
  - binary frame encodings;
  - the firmware signature text.
- **Generator:** `protocol/tools/gen-vectors.ts` uses `@noble/curves` with `{lowS: false, format: "der"}` and `getPublicKey(sk, false)`. The library's defaults (low-S, compressed keys) would produce vectors the firmware cannot match.
- **Verifiers:** the host and the fake host verify with Node's built-in `crypto`, which accepts high-S signatures. The firmware never normalizes S.
- **Byte stability:** `.gitattributes` has `protocol/vectors/** -text`, in this repo and in OpenMausBot's vendored copy. `SHA256SUMS` covers the exact bytes. CI regenerates the vectors and runs `git diff --exit-code protocol/vectors`.
- The firmware's C tests and OpenMausBot's TypeScript tests read the same files. OpenMausBot vendors a copy that its tests compare by hash.

## 5. Firmware and simulator

### 5.1 Repository layout

| Path | What |
|---|---|
| `protocol/` | `PROTOCOL.md` (§4 as the normative reference), `vectors/`, `tools/gen-vectors.ts` |
| `firmware/core/` | Portable C11: protocol codec (cJSON), session, interaction state, audio buffers, OTA state machine, console commands, PSA crypto adapter. Includes only `gadget_hal.h`, `cJSON.h` and `psa/crypto.h` (plus libc). The PSA adapter (`core/crypto_psa.c`) is the single implementation of the HAL crypto group for both ports; ports only call `psa_crypto_init()` at boot |
| `firmware/ui/` | LVGL screens, the Maus animation, the generated art (`art/`), Latin-1 fonts (`fonts/`) and `ui_lv_compat.h`. Depends only on LVGL and core |
| `firmware/ports/esp32/` | The ESP-IDF application, its shared `sdkconfig.defaults`, `partitions/16mb.csv`, plus `boards/<board-id>/`: `board.h`, `board.c`, `sdkconfig.defaults` |
| `firmware/ports/sim/` | The desktop simulator: SDL2 window, LVGL test display for headless runs, SDL or file audio, and the wslay-based WebSocket client (`sim_ws.c`) |
| `firmware/tests/` | Native unit tests (CTest) against a fake HAL |
| `tools/fake-host/` | A Node stand-in for MausBot, for CI and offline work |
| `tools/art/` | Generates the layered Maus art from the app's mascot geometry into LVGL image sources (§5.5) |
| `site/` | The browser installer (static, GitHub Pages) |
| `AGENTS.md` | Build, flash, monitor and add-a-board instructions for coding agents |
| `NOTICE` | The Apache-2.0 notice plus the trademark sentence (§11) |
| `THIRD_PARTY.md` | Every third-party dependency with its license (§11) |

**Toolchain and dependencies:**

- **ESP-IDF v6.0.3.** CI uses the Docker image `espressif/idf:v6.0.3`, and the component manifest says `idf: ">=6.0.3,<6.1"`. The code stays buildable on v5.5.5: new I2S, I2C and LCD drivers only, and C11 that also compiles as gnu23.
  - The v5.5.5 fallback is checked by an optional CI job that relaxes the manifest's `idf` range to `>=5.5.5,<6.1` before building. The committed manifest keeps `>=6.0.3,<6.1`.
- **Build command.** Boards are built from `firmware/ports/esp32/` with

  ```
  idf.py -B build/<board> -D GADGET_BOARD=<board> -D SDKCONFIG=build/<board>/sdkconfig [-D PROJECT_VER=<version>] build
  ```

  - Each board gets its own `sdkconfig`. ESP-IDF's default is one shared `sdkconfig` in the project folder, and `sdkconfig.defaults` only fill options missing from an existing one, so without `-D SDKCONFIG` every board after the first would inherit the first board's configuration.
  - `firmware/ports/esp32/CMakeLists.txt` sets `SDKCONFIG_DEFAULTS "sdkconfig.defaults;boards/${GADGET_BOARD}/sdkconfig.defaults"` before `project()`. It fails if `GADGET_BOARD` is unset or is not a directory under `boards/`. `sdkconfig` is gitignored.
  - The same file sets `PROJECT_VER` to `0.0.0-dev` when it is not passed with `-D`, so a local build never takes ESP-IDF's `version.txt` or `git describe` fallback. The simulator does the same. Release CI passes `-D PROJECT_VER=<tag without v>` and fails if that value ends in `-dev`.
- **LVGL 9.6.0** everywhere:
  - simulator: FetchContent tag `v9.6.0`, SHA256 `b20ee3acc1bba13c62d854f9ebd62e4c51e0b443b1e0225892e86442defa84df`;
  - ESP-IDF: `lvgl/lvgl: "9.6.0~1"`.

  If the device hits a 9.6 regression, both sides drop to 9.5.0 together. 9.6 deprecates `lv_obj_add_flag` and `lv_obj_remove_flag` in favor of new setters such as `lv_obj_set_hidden`, which 9.5 lacks. So UI code changes object flags only through `firmware/ui/ui_lv_compat.h`: for example, `ui_set_hidden()` calls `lv_obj_set_hidden` when `LVGL_VERSION_MINOR >= 6` and `lv_obj_add_flag` / `lv_obj_remove_flag` otherwise. The 9.5.0 fallback is then a version-pin change only.
- **LVGL configuration:**
  - The device sets `CONFIG_LV_*` options in `sdkconfig.defaults`.
  - The simulator uses `firmware/ui/lv_conf.h`.
  - A shared `firmware/ui/ui_lv_requirements.h` `#error`-checks that both have the options the UI needs: RGB565, no LVGL OS, `animimg`, RGB565A8 and A8 drawing.
- **Own driver glue** on driver components: `espressif/esp_lcd_co5300`, `waveshare/esp_lcd_touch_cst9217`, `espressif/esp_codec_dev`, IDF's `esp_lcd` ST7789, `i2s_std` and `i2c_master`. There is **no Waveshare BSP and no `esp_lvgl_port`**: their LVGL task conflicts with the single-threaded core.
- **Network components:** `espressif/esp_websocket_client ^1.8.0`, with `buffer_size` = 16 KiB + 64 and auto-reconnect off, because core drives reconnects (§4.3); and `espressif/mdns ^1.14.0`. Both have been managed components, outside ESP-IDF itself, since IDF 5.0.
- **cJSON** always comes from `espressif/cjson` on ESP-IDF, and from FetchContent or Homebrew on the desktop. Code includes it as `"cJSON.h"` and never requires IDF's old `json` component.
- **Crypto** is PSA-only (§5.2). The simulator pins mbedTLS 3.6.7 through FetchContent, and CI also builds against mbedTLS 4.2.0.
- **Node:** the SDK's tools (`protocol/tools`, `tools/fake-host`, `tools/art`, `site/`) need Node ≥ 22.18, which runs `.ts` files directly. CI uses Node 24.

### 5.2 The HAL

`gadget_hal.h` is the whole contract between core and a port:

| Group | Functions |
|---|---|
| Mic | start and stop capture at 16 kHz; frames delivered as events |
| Speaker | open at a rate, write PCM, level, stop |
| Input | events: `talk_down/up`, `cancel_down/up`, `touch_down/move/up(x, y)`, `swipe(dir)` |
| Storage | key/value get, set, erase (NVS on ESP32, a file in the simulator) |
| Net | Wi-Fi state and scan; WebSocket open, send, close; mDNS lookup of `_openmausbot._tcp` by `id` |
| Crypto | P-256 keygen, sign, verify; SHA-256; random bytes. **Provided by core** (`core/crypto_psa.c`, PSA API on both ports) |
| OTA | open slot, write, finalize, set boot, mark valid, rollback state |
| Battery | percent and charging, or "not present" |
| System | monotonic ms clock, restart, log |

- The port runs `core_tick()` every 10 ms and delivers every event on that same thread. Core and UI are single-threaded and hold no locks.
- On ESP32, driver tasks post to a FreeRTOS queue that the main task drains. OTA flash writes run in a worker task.
- The UI only reads a `ui_model` that core publishes, and LVGL draws from it.
- **Crypto adapter.** It uses only these PSA calls:
  - `psa_generate_key`, `psa_import_key` and `psa_export_key`;
  - `psa_sign_hash` with `PSA_ALG_DETERMINISTIC_ECDSA(PSA_ALG_SHA_256)`;
  - `psa_verify_hash` with `PSA_ALG_ECDSA(PSA_ALG_SHA_256)`;
  - `psa_hash_compute` and `psa_generate_random`.

  Our own code converts between DER and raw signatures. The same source builds on mbedTLS 3.6.x (ESP-IDF 5.5, the simulator) and 4.x (ESP-IDF 6.0). It never uses the legacy `mbedtls_ecdsa_*` API, which is private in 4.x.

### 5.3 Boards

| Board id | Hardware | Screen | Audio | Input | Battery |
|---|---|---|---|---|---|
| `amoled-175c` | Waveshare ESP32-S3-Touch-AMOLED-1.75C: aluminum case, 8 MB PSRAM, built with the 16 MB flash config | 466×466 round AMOLED (CO5300), CST9217 touch | ES8311 + ES7210, built-in speaker; MIC1 only in v1 | touch, BOOT, PWR (GPIO3) | AXP2101, battery bay |
| `amoled-175` | Waveshare ESP32-S3-Touch-AMOLED-1.75: 16 MB flash, 8 MB PSRAM | same panel and touch | same codecs, speaker connector | touch, BOOT | AXP2101 |
| `lcd-154` | Waveshare ESP32-S3-LCD-1.54: 16 MB flash, 8 MB PSRAM | 240×240 ST7789 | ES8311 + ES7210, onboard speaker | BOOT (TALK), PLUS (CANCEL) | BAT_ADC (GPIO1), CHG_STAT (GPIO3) |
| `devkit` | ESP32-S3-DevKitC-1-N16R8 (16 MB flash, 8 MB PSRAM), 2" ST7789 320×240, INMP441 mic, MAX98357A amp | 320×240 | I2S mic and amp on separate I2S controllers | BOOT (TALK), one extra button (CANCEL; its GPIO is in `board.h`) | none |

- **Flash and partitions.** All four boards use one 16 MB partition table, `partitions/16mb.csv`. It stays the same across releases.

  | Name | Offset | Size |
  |---|---|---|
  | nvs | 0x9000 | 0x6000 |
  | otadata | 0xF000 | 0x2000 |
  | phy_init | 0x11000 | 0x1000 |
  | ota_0 | 0x20000 | 0x600000 |
  | ota_1 | 0x620000 | 0x600000 |
  | coredump | 0xC20000 | 0x10000 |

  - Each board's `caps.ota.max` equals its OTA slot size (6291456). CI asserts that `app.bin` fits the slot.
  - Some 1.75C units carry 32 MB of flash. The firmware still uses 16 MB and logs the physical size at boot with `esp_flash_get_physical_size()`.
- **Speaker rate.**
  - On the three Waveshare boards, the ES8311 and ES7210 share one I2S clock. They run duplex at 16 kHz and advertise `speaker.rate` 16000.
  - The devkit has separate I2S controllers and may advertise 24000.
- **No echo cancellation in v1.** Firmware captures MIC1 only, mono at 16 kHz. Using the 1.75C's MIC3 loopback reference (ES7210 in TDM mode plus an echo canceller) is a v2 item.
- **amoled-175c PWR.**
  - PWR is the AXP2101's power key. Its level is mirrored to GPIO3, which reads high while pressed.
  - Holding it for 6 s cuts power. The docs say so, because PWR doubles as CANCEL (§5.4).
- **lcd-154 power and battery.**
  - `board_early_init()` drives BAT_EN (GPIO2) high immediately. Otherwise the board switches off on battery when PWR is released.
  - The battery is **supported**. BAT_ADC on GPIO1 sits behind a ×3 divider, and the percentage is computed from voltage. CHG_STAT on GPIO3 reads low while charging.
- **Sources.** Pins and init sequences come from the vendors' schematics, datasheets and published component sources, used as references. They live in each board's `board.h`.
- **Licenses.** Espressif's and Waveshare's driver components are fetched at build time under their own licenses, never copied into this repository.

### 5.4 Interaction

- **Touch boards:**
  - Hold anywhere to talk, release to send. A press under 300 ms is ignored.
  - Tap while it is speaking to stop. If the turn's `done` has not arrived yet, the tap also sends `stop {turn}`. Once `done` has arrived, a tap only stops playback.
  - Swipe down to cancel a recording, dismiss a card or stop a turn.
  - On the 1.75C, BOOT also acts as TALK and PWR as CANCEL. Holding PWR for 6 s powers the board off.
- **Button boards:** TALK and CANCEL work the same way. lcd-154: BOOT = TALK, PLUS = CANCEL. devkit: BOOT = TALK, the extra button (its GPIO is in `board.h`) = CANCEL.
- **Barge-in:** TALK during speech stops playback locally and starts listening. If the old turn has not received `done` yet, the gadget first sends `stop {turn: <old>}`.
- **Approvals:**
  - Touch boards show one button per option, styled by `style`.
  - Button boards: TALK picks option 1 (the `allow` option on a permission ask), and CANCEL picks option 2 (the `deny` option). A one-option ask maps only TALK. An ask with more than two options shows "Answer on your computer or phone".
  - An ask with no options shows "Answer on your computer or phone" on every board.
  - Presses in the first 0.6 s after an ask appears are ignored.
- **Recording limit:** 60 s, with a countdown in the last 5 s.

### 5.5 Screens and the Maus

The animated Maus fills every screen that isn't showing text.

**Art.**

- `tools/art` generates the art from the app's own mascot geometry, at a pinned OpenMausBot commit:
  - the cursor body from `shared/mascot-bodies.ts`;
  - the 25 expressions (eye outlines and mouths) from `src/components/cursor-face-data.ts`.
- The app keeps no mascot SVG sources in git. Older exported mascot stills are out of date (old gradient and face anchor) and are not used.
- **Layered images, not full frames:**
  - one RGB565A8 body image per size profile;
  - A8 eye frames for each expression × 4 blink steps;
  - small mouth images;
  - an x/y offset for each image.

  Profiles: `s240` for the two AMOLED boards (body 201×240, about 365 KiB in total) and `s150` for lcd-154 and the devkit (body 125×150, about 144 KiB).
- Expression changes happen under a blink.
- **Motion** is translation only in v1: bob and jitter. The app's "breathing" scale pulse becomes a small vertical bob. Scaling and rotation wait for frame-rate measurements on the device.
- **Palette.** The app's green: body gradient `#8cd1b3` / `#009957` / `#005932`, white eyes and mouth. Accents: `#2fd187` on the black screen, and `#007a45` on light backgrounds such as the docs.
- **Determinism.** Core and UI pick expressions and blink times with a seeded xorshift32 PRNG. Headless runs use a fixed seed.
- **Budget and checks.**
  - The generated C is committed.
  - Each size profile has an art byte budget, and CI checks the total image bytes: `s240` ≤ 512 KiB and `s150` ≤ 256 KiB. If the white-eye fallback in §6.5 is needed (eyes as RGB565A8, about 750 KiB and 295 KiB in total), the budgets become 800 KiB and 320 KiB. Either way the art is a small part of the 6 MiB OTA slot.
  - The art drift check (regenerate, then `git diff --exit-code firmware/ui/art`) runs on linux-x64 only.
- **Provenance.** `tools/art/source/README.md` records the pinned OpenMausBot commit the geometry comes from (Apache-2.0). It also carries the trademark sentence from §2.

**States.** Each screen maps to one Maus state. The state names are the app's, except `speaking`.

| Screen | Shows | Maus state |
|---|---|---|
| Idle | Maus bobbing gently and blinking; "Hi, I'm <bot name>" | `idle` |
| Listening | Maus listening, a ring that follows mic level, the countdown near 60 s | `listening` |
| Thinking | The `heard` text, then the `working` phrase | `thinking`, then `working` once a `working` phrase arrives |
| Speaking | The reply text pages itself while audio plays | `speaking` (gadget-only, below) |
| Reply | The finished reply. It returns to idle after 20 s or a tap. A failed `done` shows its `reason` | `idle`; `alerting` after a failed `done` |
| Ask | Title, body, option buttons | none (text screen) |
| Card / Image | What a bot put up, until `ttl_s` or dismissal | none |
| Post | A toast over any screen, with a chime | `notifying` |
| Setup | "Pair me: MausBot → Settings → Remote access → Pair a gadget", then enter the code in the installer. It notes that Remote access must be on. If `host auto` finds no MausBot, it says to enter the address shown under Pair a gadget in the installer. Shows `host_name` once a `challenge` has arrived | `curious` |
| Offline | Wi-Fi or MausBot unreachable, with the reason, the stored `host_name` and the next retry (§4.3 backoff). When MausBot can't be reached: "Is Remote access on in MausBot?", plus the Windows Public-network hint | `sleeping` |
| Update | Progress bar during OTA | none |

- **`speaking`** is new, original art. The app has no speaking state or open-mouth geometry.
  - It adds 3 open-mouth levels.
  - Playback level picks the level, frame by frame.
- Round layouts keep text inside the circle's safe area.
- A battery arc sits at the top edge on boards with a battery.

**Fonts.**

- Latin-1 fonts are generated with `lv_font_conv` (MIT) from Montserrat (OFL-1.1) at the sizes the UI uses. Ranges: 0x20–0x7E, 0xA0–0xFF, plus 0x2026 (`…`, the reply cut marker) and 0x2192 (`→`, in the Setup copy). The host's fold keeps exactly these two extra code points (§4.4).
- They are set through styles on the UI root, not through `LV_USE_CUSTOM_FONT_DEFAULT`. That way they work the same on the device and in the simulator.

### 5.6 Console

The USB serial console (and stdin in the simulator) accepts:

| Command | Does |
|---|---|
| `wifi "<ssid>" "<password>"` | Saves the network and connects. An open network uses `""` as the password |
| `scan` | Lists nearby Wi-Fi networks as one `@omb` line |
| `host auto` / `host <address>[:port]` | Finds MausBot over mDNS (the default), or pins an address |
| `pair <code>` | Stores the six-digit code and reconnects at once (§4.3) |
| `name "<text>"` | Sets the name shown in Settings → Remote access (≤ 32 characters) |
| `say "<text>"` | Sends a typed message (≤ 2000 chars) as a `say` op and prints `@omb {"op":"say","turn":"…"}` |
| `status` | Prints Wi-Fi, host, id, pairing state, firmware and battery as one `@omb` line |
| `log off` / `log on` | Silences or restores ESP log output, for installer sessions. It never hides `@omb` lines and lasts until reboot |
| `forget` | Erases the pairing, key, Wi-Fi and name |
| `reboot` | Restarts |

- **Quoting** follows `esp_console_split_argv`: an argument in `"…"` may contain spaces, with `\\` and `\"` as escapes. SSIDs and passwords may therefore contain spaces.
- Lines end with CR, LF or CRLF.
- **Machine-readable lines.** `status` and `scan` print one line each, starting `@omb ` and followed by JSON. `say` (table above) and `host auto` (below) print their own `@omb` lines.

  ```
  @omb {"op":"status","wifi":…,"host":…,"id":…,"pair":…,"error":…,"fw":…,"battery":…}
  @omb {"op":"scan","networks":[{"ssid":…,"rssi":…,"auth":…}]}
  ```

  - `wifi` is `"off"`, `"connecting"`, `"connected"` or `"failed"`. A separate `ssid` field is present when a network is set.
  - `host` is `"addr:port"`, omitted when unknown.
  - `id` is the gadget id; `fw` is the firmware version.
  - `pair` is `unpaired`, `code_stored`, `connecting`, `paired` or `error`. `paired` means a `ready` arrived on the current connection. `connecting` means a host is known and the gadget has a `host_id` or a code, but no live session. `code_stored` means a code is stored but no host is known yet (Wi-Fi down, or `host auto` still resolving).
  - `error` is present only when `pair` is `error`, and holds the last handshake `error` code, such as `bad_code`.
  - `battery` is `{"pct": <0–100>, "charging": <bool>}`, omitted on boards without a battery.
  - In `scan`, `rssi` is an integer in dBm, and `auth` is `open`, `wep`, `wpa`, `wpa2`, `wpa3`, `wpa2-ent` or `other`.
  - Tools read only `@omb` lines and ignore the rest of the log.
- `host auto` resolves `_openmausbot._tcp` once Wi-Fi is connected, and picks the service whose TXT `id` matches the stored `host_id`. Before the first pairing, it picks the only service it finds.
  - If it finds several, or none within 5 s, it prints `@omb {"op":"hosts","hosts":[{"name":…,"address":…,"id":…}]}` (an empty list for none) and waits for `host <address>`.
- The simulator's stdin console accepts the same commands, including `say`.

### 5.7 Simulator

**Window mode.** `gadget-sim --board amoled-175c [--host <addr>] [--pair <code>] [--name <name>]` opens a window the size of the board's screen. It is masked round where the board is round.

- Mouse is touch, Space is TALK, Esc is CANCEL.
  - An SDL event filter turns these into HAL events, ignoring key repeat.
  - It drops quit and window-close events, and `LV_SDL_DIRECT_EXIT` is 0.
  - LVGL reads touch through one custom pointer device that shares the HAL's touch state.
- The Mac's mic and speakers stand in for the board's.
  - The mic opens lazily on the first TALK.
  - After about 1 s of silent samples while talking, the simulator prints a microphone-permission hint. macOS TCC asks on behalf of the terminal app.
- The console reads stdin.

**Headless mode.** `--headless --script <file>` does **not** use SDL. CI and the docs' screenshots use it.

- The display is LVGL's test display (`lv_test_display_create`, RGB565).
- A virtual clock advances `lv_tick_inc` and `core_tick` together.
- Audio uses a null HAL, or WAV files with `--mic-file` / `--speaker-file`.
- Scripted touch goes through the same pointer device.
- Snapshots use `lv_test_screenshot_compare()` with LVGL's bundled lodepng.
- The PRNG uses a fixed seed, so runs are deterministic.
- **Script format:** one command per line; `#` starts a comment.

  | Command | Does |
  |---|---|
  | `wait <ms>` | Advances the virtual clock; socket I/O is still serviced on every tick |
  | `touch <x> <y>` / `release` | Presses or releases the pointer |
  | `swipe <dir>` | `up`, `down`, `left` or `right` |
  | `talk_down` / `talk_up` | Presses or releases TALK |
  | `cancel` | Presses and releases CANCEL |
  | `console <line>` | Feeds one console line (§5.6) |
  | `expect <op> [timeout_ms]` | Waits until a frame with that `op` crosses the socket in either direction; the run fails after `timeout_ms` (default 5000) |
  | `snapshot <name>` | Compares the screen to the committed `<name>.png` |

**Shared behavior:**

- **State:** each `--name` keeps its own key and settings under `~/.openmausbot-gadget/sim/<name>/`, so several simulated gadgets can be paired at once.
- **WebSocket:**
  - The client is **wslay 1.1.1** (MIT), compiled from its `.c` files and not from its own CMake build.
  - Our own `sim_ws.c` adds the TCP connect and the HTTP upgrade. It is non-blocking, with a 5 s timeout, and sends no `Origin` header.
- **mDNS:** `host auto` works on macOS only, through `dns_sd`. On Linux, `--host` is required, and CI always passes `--host 127.0.0.1:<port>`.
- **OTA:** the simulator cannot run an ESP32 image, so OTA images are opaque bytes to it. It runs the same core OTA state machine and checks.
  - It writes the image to one of two slot files plus `otadata.json {active, state, version}` in its state folder, verifies the signature, and "restarts" by re-execing itself.
  - After the re-exec it reports the new slot's `version` as `fw` and honors probation.
  - A test-only `--fail-probation` flag makes the new image ignore its first `ready`, so the probation timer rolls it back. The §10 forced-rollback test uses it.
  - Sockets are opened with `FD_CLOEXEC`.
  - `SDL_Quit` runs before `execv`.
  - The binary finds its own path through `_NSGetExecutablePath` on macOS or `/proc/self/exe` on Linux.

### 5.8 Browser installer (`site/`)

A static page published with GitHub Pages at `https://aivsomkar.github.io/openmausbot-gadget-sdk/`.

- The page needs Web Serial. It checks `'serial' in navigator` rather than the browser name. Chrome and Edge on the desktop are tested.
- **Firmware source.** GitHub release assets send no CORS headers, so the page cannot fetch them.
  - The Pages workflow downloads the latest release's assets server-side (`gh release download`) into the site build.
  - The page fetches the same-origin `firmware/install.json` and the parts it lists.
  - **`install.json`** is `{"version": "<version>", "boards": {"<board id>": {"parts": [{"path": "<file>", "offset": "0x…"}], "full": "<file>"}}}`. `path` is relative to `firmware/`. `offset` is the hex string from the board's `flasher_args.json`. `full` names the merged `-full.bin`. Before the first release the file is `{"version": null, "boards": {}}`, and the page says no firmware is published yet.
- Espressif's `esptool-js` is a build-time dependency.

Steps:

1. Pick a board.
2. Connect over USB. The page flashes the **separate parts** at the offsets from that board's `flasher_args.json`: bootloader, partition table, `ota_data_initial` and the app.
   - NVS (the identity key, Wi-Fi and pairing) survives a reinstall, so a reflashed gadget stays paired.
   - An explicit "Erase everything" option wipes the whole flash.
3. Reset into the app (`resetToApp()`):
   - clear FORCE_DOWNLOAD_BOOT with `writeReg(0x6000812C, 0, 1)`;
   - pulse RTS;
   - disconnect;
   - reopen raw Web Serial at 115200 with DTR and RTS false.

   The page handles the USB device (0x303A/0x1001) disconnecting and reconnecting. If no app output arrives within about 5 s, it tells the person to press RST or replug the board.
4. Send `log off`, then `scan`. The person picks a Wi-Fi network from the gadget's own list and enters its password.
5. Open MausBot → Settings → Remote access → Pair a gadget and type the six-digit code. Remote access must be on. On a Windows Public network, the page repeats the companion's Public-network hint.
6. The page sends `pair`, `wifi` and `host auto` over the console, in that order, so the code is stored before the gadget can connect. It then polls `status` until the `@omb` line reports `"pair":"paired"`.
   - If `host auto` prints an empty `hosts` line (no MausBot found within 5 s) or several, the page asks the person for the address shown in the Pair a gadget panel (§6.4), or to pick one, and sends `host <address>`.

Each release also has a merged `-full.bin`, for command-line recovery only. Flashing it at 0x0 erases NVS.

### 5.9 Fake host (`tools/fake-host/`)

A Node script that speaks the protocol from the host side. It may use the `ws` npm package, and it verifies signatures with Node's `crypto`. It:

- prints one six-digit code and enrolls only with that code; any other code gets `bad_code`;
- answers a voice turn with fixed text and a test tone;
- sends an ask and a post on command;
- runs an OTA of a supplied image signed with a test key.

It is the server for CI's end-to-end tests and for work without MausBot.

### 5.10 `AGENTS.md`

Written for coding agents, MausBot's own bots first. It covers:

- installing ESP-IDF v6.0.3 with Espressif's EIM installer or `install.sh`;
- building and flashing each board (`idf.py -B build/<board> -D GADGET_BOARD=<board> -D SDKCONFIG=build/<board>/sdkconfig build flash monitor`, §5.1);
- reading the serial log;
- running the simulator and the fake host;
- adding a board, including its OTA slot size, the speaker-rate rule (§4.3) and its art profile;
- adding an action with `gadget_action_register(name, description, schema, risk, handler)`, within the `hello` limits (§4.3). It tells makers not to register actions whose misuse is unsafe: in v1 the gadget does not authenticate the host, and `confirm` is enforced only on the Mac (§9).

The README's headline path is "ask your MausBot to flash my 1.75C". The README also says that Remote access must be on and that pairing starts at MausBot → Settings → Remote access → Pair a gadget.

## 6. MausBot host

All paths are in the OpenMausBot repository. Line-level references are from `origin/main` 6dd4403.

### 6.1 Companion: `companion/src/gadget/`

The companion ships as plain `tsc` output with no `node_modules`. It stays dependency-free, and its code cannot import `shared/`, so the few needed types and helpers are copied locally.

| File | Responsibility |
|---|---|
| `ws.ts` | A hand-rolled **RFC 6455 server** (§4.1 rules). Masked client frames; text, binary, ping, pong and close; fragment reassembly up to the frame caps; strict UTF-8; no extensions. Image rows, firmware chunks and speech go through a drain-aware sender that waits for `drain` whenever the socket's `writableLength` exceeds 64 KiB. On top of that, backpressure closes a session whose send buffer grows past 256 KiB |
| `hub.ts` | Handles the `upgrade` for `/gadget`. It is attached **only** to the `0.0.0.0:8810` server's upgrade listener, before `proxy.upgrade`, and **never** on the managed (hosted HTTPS) origin. Checks the subprotocol, refuses an `Origin` header, and keeps one session per gadget id. A new connection replaces the old session only after its `prove` verifies; the older one then receives `error replaced`. Until then the old session is untouched |
| `enroll.ts` | Runs the handshake rules in §4.3 with Node `crypto` (P-256). Enrolls through `DeviceRegistry.enrollGadget` |
| `session.ts` | Turns gadget ops into harness calls and harness events into gadget ops (§6.2) |
| `harness.ts` | The hub's harness client (§6.2 rules) and the single shared SSE subscription |
| `shape.ts` | Reply text → screen text. A companion-local, screen-oriented adaptation of the harness's `speakable()` (fenced code → `[code]`, newlines kept, link labels kept, emoji dropped, Latin-1 fold) |
| `audio.ts` | WAV packing for STT; real-time pacing of speaker PCM |
| `releases.ts` | Fetches the release manifest, downloads and checks images (§8) |
| `types.ts` | Local copies of the few shared types and helpers the hub needs, including `isPersistentQuestionCard` from `shared/ask-question.ts`, with a test pinned to the original's cases |

**Lifecycle:**

- Ready sessions register with `connectedDevices.open(id, terminate)`. That gives the desktop its online dot. Revoking a gadget calls `terminate`, which sends `error revoked` and closes the socket.
- `gadgetHub.close()` sends close code 1001 to every gadget and then terminates the sockets. It runs in the companion's `shutdown()` before `closeAllConnections()`, because upgraded sockets would otherwise keep `server.close()` pending. A test checks that shutdown completes with a gadget connected.

**Registry changes (`devices.ts`):**

- `DeviceRecord` becomes a discriminated union. A missing `kind` reads as `"phone"`, so existing records need no migration.
- Gadget records (`kind: "gadget"`) hold:
  - `id` (`gad_<16 hex>`), `name`, `publicKey`, `board`, `firmware`;
  - `botId`, `speakPushes`, `lastSensors`;
  - `createdAt`, `lastSeenAt`.

  They hold no `tokenHash`.
- The loader, `normalizeDevice` and `authenticate()` handle gadget records.
  - Today the loader drops records without a token and `authenticate()` would throw on them.
  - `authenticate()` skips records without a `tokenHash`, so an empty hash can never match a bearer.
- Cloud-desktop and browser-control grants are refused for gadgets.
- **`DeviceRegistry.enrollGadget(code, gadget)`** is new. It shares the attempt and consume logic of the pairing window with `redeem()`:
  - it accepts `/^\d{6}$/` only;
  - it checks the device cap only after the code matches;
  - it consumes the window;
  - it persists with rollback.
- The HTTPS, Tailscale and `POST /api/pair` flows are unchanged.
- Gadgets count toward the existing 20-device cap.
- `lastSensors` is kept in memory and persisted at most every 60 s, with the existing `lastSeenAt` throttle.

**mDNS and host id:**

- On first start, the companion creates `host_id`, 32 lowercase hex characters from 16 random bytes. It stores it in `DATA_DIR/host.json`, kept apart from `devices.json`.
- The `_openmausbot._tcp` TXT record keeps `v=1` and gains `id=<host_id>`.

**Control port additions (`control.ts`, loopback `:8811`):**

| Route | Caller | Auth |
|---|---|---|
| `GET /gadget/devices?botId=`, `POST /gadget/display`, `POST /gadget/act` | harness only (§7) | header `x-openmausbot-gadget-control`, compared with `timingSafeEqual`; fails closed: 503 with no token, 403 on mismatch |
| `PATCH /devices/:id/gadget {botId?, speakPushes?, name?}` | Electron (settings changes, which then reach a live gadget as a `settings` op) | the existing loopback checks (§9) |
| `POST /devices/:id/firmware-update` | Electron (the Update button, §8) | the existing loopback checks |
| `POST /firmware-updates/check` | Electron, when Settings → Remote access opens (§8) | the existing loopback checks |
| `POST /pairing` gains an optional `botId`, stored on the window | Electron ("Pair a gadget") | unchanged |

- Only the harness calls the `/gadget/*` routes. Electron calls the other rows on the same port.
- Electron sets the harness's `OMB_COMPANION_CONTROL_PORT` to the same value it passes the companion as `OMB_CONTROL_PORT` (8811).
- The companion also pushes a presence notice to the harness (`POST /api/gadgets/presence`) on enroll, on remove and at start (§7).

### 6.2 Session mapping

**Thread.** A gadget talks in its bot's **currently selected thread** (`WireBot.threadId`), the same rule Live calls use. MausBot has no separate "main thread".

- The hub resolves the thread at the start of each turn from the latest `bot` frame.
- It pins that thread for the whole turn, and always sends `threadId` explicitly. Companion requests without one get 409 once a bot has more than one thread.
- If the person switches threads on the desktop, the next gadget turn follows. Message and runtime frames are broadcast for every thread, so a pinned turn keeps getting its events.

**Send id.** Each send carries `sendId = "gdt" + base64url(sha256(gadgetId + ":" + session + ":" + turn)).slice(0, 40)`.

- The harness requires `/^[A-Za-z0-9_-]{16,80}$/` and dedupes on this id.
- Including the session means a gadget reboot can never collide with an old turn.
- The `gdt` prefix marks gadget-sent messages, even after a hub restart.

| Gadget op / harness event | What the hub does |
|---|---|
| `voice.end` | Packs the PCM as WAV and calls `POST /api/stt`. Empty text ends the turn (§4.4). Otherwise it sends `heard`, then `POST /api/bots/:botId/messages {text, threadId, sendId}` |
| `say` | Same, without STT |
| `stop`, or a new `voice.begin` or `say` while a turn is in flight (§4.4) | Depends on how the turn was sent; see **Stop** below |
| `answer` | `POST /api/threads/:threadId/respond {requestId, behavior, message?}` (see Asks below) |
| `message` frame with role `bot` and kind `activity` on the pinned thread | `working` with the activity's `tool.spoken` phrase (for example "searching the web"); cleared when the turn completes |
| `runtime` `content.delta` (`assistant_text`) for the pinned thread and turn | `reply` with cumulative, shaped text, at most one every 250 ms |
| `message.patch` with `turnTerminal: true` for the bound turn | `reply {final: true}`, then speech |
| `runtime` `turn.completed` for the bound turn | `done`: `ok` → `ok`; `stopReason: "interrupted"` → `stopped`; anything else → `failed` |
| `message` with kind `options` and an open card in one of the bot's threads | `ask` (see Asks below) |
| `runtime` `request.resolved`, or a card patch that is answered, dismissed or expired | `ask.close`: `answered` when the source is the user, `expired` on timeout, else `withdrawn` |
| `routine.run` for the gadget's bot entering `completed`, `failed`, `missed` or `waiting` | `post {kind: "routine"}` with the run's `output`, else `error`, else `attention`; deduped by run id and status |
| A bot text message or patch with `turnTerminal: true` in one of the gadget bot's threads, where the thread's task has no `routineRunId` and `requestMessageId` is absent or points to a peer ask | `post {kind: "message"}` to the gadgets on that bot; deduped by message id |

- **Turn binding.** The gadget's own user message is found by its `sendId`. Bot messages whose `requestMessageId` points to it belong to the turn. A late reply to a gadget turn that has already ended is not pushed. It answers a person's message (§4.6), so the hub drops it, and the person can read it on the desktop or a phone. The same holds after a disconnect (§4.4).
- **Stop.** How the hub stops a turn depends on how it was sent:
  1. Not sent yet (still recording, or in speech-to-text): the hub drops the audio and sends nothing to the bot.
  2. The gadget's own running turn: `POST /api/bots/:botId/interrupt {threadId}` with the pinned thread.
  3. Queued (the receipt had `queued: true`) and not yet drained: `DELETE /api/bots/:botId/queue/:queueId`, which is already allowlisted (`companion/src/routes.ts:146`).
  4. Steered (the receipt had `steered: true`): no interrupt, because the running turn belongs to another requester.
  5. A turn that already ended: ignored (§4.4).

  In cases 1–4 the hub sends `speak.stop` for the turn's speech and `done {outcome: "stopped"}` at once, and unbinds the turn, so later events for it are dropped.
- **Busy bot.** A message to a busy thread is either steered or queued:
  - **Steered** into the running turn (receipt `steered: true`): that turn's terminal reply is the gadget's answer.
  - **Queued** (`queued: true, queueId`): the hub sends `working "Queued behind another task"` and binds the turn when the queued message is drained, through its `queueId`.
- **What counts as a person's message.** A user message is a person's message unless it has `peerAsk` set or sits in a routine's execution thread (§4.6). The hub tells them apart from the user messages it sees on the stream: it keeps each thread's recent user message ids with their `peerAsk` flag and, on a miss, reads `GET /api/threads/:threadId/messages?limit=50`. A reply whose user message it still cannot find is not pushed.
- **Never pushed:**
  - replies to a person's message from any device, including late replies to the gadget's own turns;
  - bot messages without `turnTerminal: true`, so a turn with several items gives at most one post;
  - anything in a routine's execution thread (its task has `routineRunId`); routine results arrive only through `routine.run`;
  - MausBot's per-turn evidence messages;
  - activity messages;
  - routine lifecycle cards;
  - the `notify` stream, which also fires for person-started turns.
- **Asks.** The hub looks only at threads that belong to the gadget's bot, using a thread → bot map built from `bot` frames.
  - A card is a **permission** card when it is not a question card. The hub classifies it with a local copy of `isPersistentQuestionCard` from `shared/ask-question.ts` (companion code cannot import `shared/`, §6.1; the copy lives with `types.ts` and has a test pinned to the original's cases). The rule also covers the harness's own peer-approval cards, since they carry no request type.
  - Permission cards get exactly **Allow** and **Deny** (§4.5). The answer is `behavior: "allow"` or `"deny"`. The hub never calls `/always-allow`.
  - Question cards get options only without `questionRequest`, or with exactly one single-select question, and only with at most 4 choices. The answer is `behavior: "answer"` with the option's label as `message`. Single-question cards use the app's "The user answered your questions" format.
  - Every other card is sent with `options: []` (§4.5).
- **Reconnect.** On reconnect, open asks are rebuilt from `GET /api/threads/:threadId/messages?limit=50`. The hub reads the bot's selected thread and every thread whose task is waiting on the person.
- **Harness client rules (`harness.ts`).**
  - Before every request, the hub calls the companion's own `denyReason({path, method, authenticated: true})`, so it can never reach a route a phone could not.
  - It sends `companionIdentityHeaders(gadgetId, token)` with the gadget's registry id as the device id.
  - Under Electron, it fails closed while the mutation token is still pending.
- **One SSE stream.** The hub keeps **one** `GET /api/events?screens=off` subscription, sent with only the companion marker header and no device id. It fans frames out to the connected gadgets by bot and thread.
  - After a dropped connection it resumes with `Last-Event-ID`.
  - After a harness restart the stream cannot resume (`hello.resumed: false`). The hub then re-reads `GET /api/bots?messages=0` and the open asks.
- **Speech.**
  1. Once a reply is final, the hub sends the **raw** reply text to `POST /api/tts/prepare`. That route already makes text speakable, so the hub never pre-shapes it.
  2. It calls `POST /api/tts/speak` with `format: "pcm_<rate>"` for each utterance, and starts playing the first while the rest synthesize.
  3. **One stream per reply.** All utterances of one reply go on one stream: one `speak.begin {stream, rate, turn}`, the PCM of each utterance in order, then one `speak.end`.
  4. It uses the bot's voice. The speak format is used only for `speaker.rate` 16000 or 24000.
  5. For a `post` with `speak: true`, the hub runs the same steps on the post text without `turn`. It queues that speech after any reply speech instead of replacing it; the hub paces speech, so it knows when the earlier stream has played out.
  6. The hub sends `speak.stop` when the turn is stopped or a new turn begins.
  7. On the first 409 (no voice) in a session, the hub sends `card {id: "notice-voice", title: "Voice is off", body: "Add a voice in this bot's voice settings to hear replies.", ttl_s: 8}`. It then stops asking for speech for the rest of that session, and replies are text only.
  8. A 415 `pcm_unsupported` (§6.3) leaves that reply text-only, with no notice.

### 6.3 Harness changes

**`POST /api/stt`** is a new route module, `server/routes/stt.ts`.

- It is registered with `ROUTES.push`, because a ratchet test forbids inline routes in `index.ts`.
- It is allowlisted in `companion/src/routes.ts`, which the harness also enforces for companion-token requests.
- **Input:** `audio/wav`, mono PCM16 at 16 kHz, at most 60 s. The route reads the raw body itself, checks `content-length`, and caps the body at about 2 MiB (413 beyond).
- **Output:** `{text, provider}`.
- **macOS** uses the Speech helper (`electron/resources/speech-helper.swift`), which gains a **file mode**. File mode accepts `--file <path>`, `--timeout-ms N` and the existing `--stop-file <path>`.
  - It uses `SFSpeechURLRecognitionRequest`, on-device when supported, with punctuation on (macOS 13+). It prints one final NDJSON line.
  - It checks `SFSpeechRecognizer.authorizationStatus()` and fails with `speech-not-authorized` **without prompting**, so a gadget turn never pops a permission dialog on the Mac.
  - The harness resolves the helper as `OMB_SPEECH_HELPER_PATH`, then `join(OMB_RESOURCES_PATH, "OpenMausBot Speech.app")`, then the dev path `electron/resources/OpenMausBot Speech.app`.
  - It launches the helper with `open -n -g -W -o <stdout file> --stderr <stderr file> <helper> --args --file <wav> --stop-file <path> --timeout-ms N`, the same way `electron/speech.mjs` does, and reads the NDJSON line from the stdout file. The helper keeps its own bundle identity, so the Speech permission granted for dictation applies.
  - Both an empty transcript and the recognizer's no-speech error map to `{text: ""}`, which ends the turn with "Didn't catch that" (§4.4).
- **Fallback:** ElevenLabs **Scribe v2**.
  - Request: `POST /v1/speech-to-text`, `model_id: scribe_v2`, raw PCM with `file_format=pcm_s16le_16`.
  - It uses the harness's single ElevenLabs key, `voiceCredential(cfg.tts?.key)`, which is set from any bot's voice settings. The route takes no bot id. The included Cloud Pro voice credential exists only on a Cloud home, never on the desktop.
  - It is used on Windows, when the Speech permission is missing, or when the helper fails.
  - A 401 or 403 from ElevenLabs → `502 {error: "stt_key_forbidden", message}`. `message` is Latin-1 copy that names the key's Speech-to-Text permission, and the hub uses it as the `done` reason.
- **Neither available:** `409 {error: "stt_unavailable"}`. The hub ends the turn with this `done` reason, folded to Latin-1: "Speech-to-text isn't set up. On a Mac, allow Speech Recognition for OpenMausBot (try dictation once), or add an ElevenLabs key in a bot's voice settings."

**`POST /api/tts/speak`** accepts an optional `format`: `mp3` (default, unchanged), `pcm_16000` or `pcm_24000`.

- No MP3 decoder is added. Each provider is asked for PCM or WAV at the target rate natively:

  | Provider | Request |
  |---|---|
  | ElevenLabs | `output_format=pcm_16000` or `pcm_24000` (raw PCM) |
  | Fish | `format: "wav"` or `"pcm"`, with `sample_rate` |
  | xAI | `codec: "wav"` or `"pcm"`, with `sample_rate` |
  | macOS `say` | `--data-format=LEI16@<rate>` |
  | Chatterbox | WAV, as today |

- A new `server/tts/pcm.ts` parses WAV by walking the RIFF chunks (`say` writes filler chunks before `data`) and resamples PCM16.
- A provider that can only return MP3 gets no PCM path, and the gadget gets text only. With a `pcm_*` format, `/api/tts/speak` then answers `415 {error: "pcm_unsupported"}`; the hub stays text-only for that reply and shows no notice.
- The PCM response's content type is `audio/pcm;rate=<rate>;channels=1;bits=16;endian=little`. It is not `audio/L16`, which is big-endian by definition.

### 6.4 Desktop UI: Settings → Remote access

There is no Devices page. Everything lives in Settings → Remote access (`src/components/CompanionSection.tsx`).

**Pair a gadget.** A new **Pair a gadget** button in Settings → Remote access.

- It opens the existing pairing window and shows the six digits large.
- Next to the code is a **Talks to [bot ▾]** picker. The chosen bot becomes the new gadget's bot: the desktop passes its id with the pairing request (§6.1). The picker starts on the bot that §4.3 rule 3 would pick.
- The panel also shows the companion's LAN address and port (for example `192.168.1.20:8810`), for when `host auto` finds nothing (§5.8).
- All gadget-facing copy says "MausBot → Settings → Remote access → Pair a gadget": the gadget's Setup screen, the installer and the README.
- Remote access must be on. While it is off, the button is disabled and says "Turn on Remote access first".
- On Windows Public networks the firewall blocks :8810. The companion's existing Public-network hint is shown here too.

**Gadget rows.** Gadget rows appear in a "Gadgets" group directly under the Pair a gadget button, outside the collapsed "Advanced & troubleshooting" section, where the phones' paired-device list stays. Each row shows:

- a gadget icon, the name, board, firmware version and battery;
- an online dot (from the companion's connected-device tracker);
- **Talks to [bot ▾]**;
- **Read pushes aloud**;
- **Update** or "Custom build" (§8);
- **Remove**.

Gadget rows have none of the phone-only switches (computer view, browser control). Changing the bot, the toggle or the name goes through the Electron-only `PATCH /devices/:id/gadget` route and reaches a live gadget as a `settings` op (§4.3).

**Counting by kind:**

- `SidebarPhoneButton.tsx` and CompanionSection's status counts label devices by `kind`. Today they treat every device as a phone, so an online gadget would read "Device paired — not connected".
- PhoneSetupFlow's success copy handles a gadget that enrolls while the phone flow is open.

**Phones.** No phone work is needed.

- The iOS and Android apps have no device list.
- They are refused `/api/devices`, so gadget records never reach them.
- An optional later addition is a `via: "gadget"` label on messages, which would go through the usual platform-parity check.

### 6.5 Verify before implementing

Each item was checked against OpenMausBot `origin/main` (6dd4403, 2026-10-04) during plan research. The answers below are settled, and the plans build on them.

1. **Reaching the Speech helper from the harness.**
   - Packaged builds already fork the harness with `OMB_RESOURCES_PATH`, so no new Electron plumbing is needed.
   - The harness resolves the helper as `OMB_SPEECH_HELPER_PATH`, then `<OMB_RESOURCES_PATH>/OpenMausBot Speech.app`, then the dev path. It launches the helper with `open -n -g -W`, like `speech.mjs`.
   - LaunchServices runs the helper under its own bundle id, so the dictation grant applies.
   - File mode never prompts: it fails with `speech-not-authorized`, and the harness falls back to Scribe v2.
   - Dev harnesses (`pnpm dev:server`) use the dev path or the override.
2. **"Main thread".**
   - No such concept exists. The gadget uses the bot's currently selected thread (`WireBot.threadId`), as Live calls do.
   - The hub resolves it per turn, pins it, and always sends `threadId`. Without it, companion requests get 409 on multi-thread bots.
3. **Control port authentication for bot tools.**
   - The control port has no authentication today, and the companion has no channel to hand a secret to the harness.
   - Electron mints `gadgetControlToken` (32 random bytes, base64url) per launch and sends it on the existing `parentPort` messages to the companion and the harness. It also sets the harness's `OMB_COMPANION_CONTROL_PORT` to the same value it passes the companion as `OMB_CONTROL_PORT` (8811).
   - `/gadget/*` control routes require `x-openmausbot-gadget-control` and fail closed.
   - `OMB_GADGET_CONTROL_TOKEN` serves dev and tests.
   - The agents proxy never holds the token (§7).
4. **Image conversion for `gadget_display`.**
   - The harness has no PNG or JPEG decoder. `sharp` is native and cannot ship in the packaged app.
   - The harness adds `pngjs` 7.0.0 (MIT) and `jpeg-js` 0.4.4 (BSD-3-Clause, not MIT) as dependencies, which esbuild inlines.
   - Resizing (box filter) and RGB565 packing are our own code. GIF and WEBP are refused.
5. **The default bot for a new gadget.**
   - The server has no default-bot concept. Sidebar order lives in the renderer's local storage, and the server's bot list is newest-first.
   - The default is the bot picked in "Pair a gadget". Without a picker, the hub applies the rule in §4.3 rule 3.
6. **The pairing window.**
   - The HTTPS, Tailscale and Wi-Fi flows all open one shared window, and `POST /api/pair` consumes it.
   - No code-only consume method exists, so `enrollGadget` is added (§6.1). The HTTPS, Tailscale and `POST /api/pair` flows need no change.
   - Wrong gadget codes use up the same 5 attempts.
   - The registry must accept token-less records.
   - PhoneSetupFlow must word its success message for a gadget.
7. **Telling pushes from replies.**
   - Bot messages carry `requestMessageId`, the originating user message: a person's message, a peer ask or a routine prompt. Together with `peerAsk` and the hub's `gdt` send ids, that tells replies from pushes (§6.2).
   - Routine results come from `routine.run` frames. Routine prompts are plain user messages with no marker, but they run in an execution thread whose task carries `routineRunId`, so the hub skips those threads for `message` pushes.
   - MausBot's `digest` messages are per-turn evidence and are never pushed.
   - `notify` frames fire for person-started turns too, so they are not used.

Still to confirm on a packaged build or on hardware. Each item has a fallback that is part of the design, so no open item blocks a core flow:

| Open item | Fallback |
|---|---|
| The Speech permission attribution when the harness launches the helper, on a side-by-side packaged build | The helper fails with `speech-not-authorized` and the harness uses Scribe v2 (§6.3) |
| The error the recognizer returns for a silent WAV | `/api/stt` maps both an empty transcript and the recognizer's no-speech error to `{text: ""}`, which gives "Didn't catch that" |
| The `esptool-js` reset on a USB-Serial-JTAG board | The installer's own `resetToApp()` and the "press RST or replug" prompt (§5.8) |
| Windows mDNS answering ESP32 queries | The Pair a gadget panel shows the companion's LAN address and port. If `host auto` finds no service within 5 s, the installer asks for that address and sends `host <address>`, and the Setup screen says to enter it in the installer (§5.6, §5.8) |
| Claude Code's MCP tool timeout | `server/drivers/claude.ts` sets `MCP_TOOL_TIMEOUT=960000` in the engine environment unless the person already set it, matching the Codex fix (§7) |
| The Codex version actually bundled | The plan records it, and the `tool_timeout_sec` argv regression test stays |
| Whether A8 eye images with `image_recolor` draw white | `tools/art` emits white RGB565A8 eye frames instead, under the fallback art budget (§5.5) |

## 7. Bot tools

Bots get three tools in `server/drivers/agents-catalog.ts`, behind a new `gadgets` profile flag (`OMB_GADGETS`).

**Visibility:**

- The flag is on while at least one gadget is **paired**, whether or not it is online.
- The harness cannot read the device registry, so it caches the flag.
  - It pulls `GET /gadget/devices` when the control token arrives.
  - The companion pushes `POST /api/gadgets/presence` (a new companion notice route) on enroll, on remove and at start.
- The flag is forced off on a Cloud home and whenever the harness holds no control token.
- **Catalog goldens:** gadgets is an overlay.
  - New `+gadgets` profiles are added, and `FULL.direct` and `FULL.room` point at them.
  - Non-gadget profiles keep identical bytes.
  - The size budget gets the new entries.

| Tool | Arguments | Does |
|---|---|---|
| `gadget_devices` | none | Lists gadgets: id, name, board, online, battery, screen and image size, whether it talks to this bot, latest sensors, `recent_events`, and each action with its params schema and risk |
| `gadget_display` | `device?`, then either `title` + `body` + `ttl_s?` or `image_path` + `ttl_s?` | Shows a card or an image. `ttl_s` defaults to 30. `device` defaults to the only gadget, else the one talking to this bot, else it errors and lists the choices. `image_path` follows the same path rules as attaching a file (working folder, the bot's workspace, the Local VM workspace); PNG or JPEG only |
| `gadget_action` | `device?`, `name`, `args?` | Runs a declared action and returns its `act.result`. Only `name` is required; `args` defaults to `{}` |

- Per-action params schemas appear only in `gadget_devices` results, never in the static catalog.
- Device-supplied names, descriptions and schemas are untrusted data and are size-clamped.

**Path:**

1. The tool runs in the agents proxy. The proxy calls new harness routes with its per-turn bearer:
   - `GET /api/internal/gadgets`
   - `POST /api/internal/gadgets/display`
   - `POST /api/internal/gadgets/action`

   The bot can read the proxy's environment, so the proxy never holds the control token.
2. The harness checks the call, runs any approval and converts images. Only the harness calls the `/gadget/*` control routes: `/gadget/devices`, `/gadget/display` and `/gadget/act` on `:8811`, with the gadget control token (§6.1). Electron calls the control port's other routes.
3. The companion forwards to the gadget's live session. An offline gadget returns an error the bot can relay. `act` gives up after 15 s.

**Safety:**

- **`confirm` is never auto-approved.** A `confirm` or missing-risk action is never auto-approved by Full access or by an engine's pre-approval. It shows a peer-approval card unless the person earlier pressed **Always allow** on the desktop card for that exact `<deviceId>:<actionName>`.
  - The grant is keyed to the hash of the action's declared entry (name, description, params and risk), so it lapses when the gadget re-declares the action with a different risk, description or schema.
  - Gadgets themselves never offer Always allow (§4.5).
- The card comes from the harness's own peer-approval gate, not the engine permission flow. Engines pre-approve built-in agents tools, so the engine flow would never ask.
  - It uses a new `PeerAction "gadget_action"`.
  - The target is `{id: "<deviceId>:<actionName>", name: <gadget name>}`.
- The harness validates `args` against the action's schema (`compileToolSchema`) **before** showing the card, so the person approves exactly what runs.
- A turn can run at most 20 gadget actions.
- The approved risk and a hash of the gadget's declared actions travel with `/gadget/act`. The companion refuses if the action changed after approval, for example after a reconnect with new caps.
- `requireActiveInternalCapability()` runs after the approval wait and again after the action returns, so a stopped turn cannot act late.
- Display tools and `safe` actions don't ask.

**Images:**

- The harness decodes PNG with `pngjs` 7 (MIT) and JPEG with `jpeg-js` 0.4.4 (BSD-3-Clause).
- It resizes with our own box filter to fit `caps.image` and packs RGB565 little-endian.
- It sends raw RGB565 (base64) plus `w` and `h` over the control port. That route's body cap is about 1 MB.
- The companion streams the rows (§4.7).

**Engine tool timeouts:**

- Codex aborts an MCP tool call after 300 s by default, but an approval card can wait 15 minutes.
- In `server/drivers/codex.ts` `mountMcpServer`, the harness-owned agents server gets `tool_timeout_sec=960`.
- A regression test checks the argv.
- Claude Code's MCP tool timeout is not yet confirmed (§6.5), so `server/drivers/claude.ts` sets `MCP_TOOL_TIMEOUT=960000` in the engine environment unless the person already set it.

## 8. Firmware releases and OTA delivery

- **Release:** pushing a `v*` tag runs `release.yml`.
  1. A build job builds every board on `espressif/idf:v6.0.3` with the §5.1 command, each with its own `-D SDKCONFIG=build/<board>/sdkconfig`, and `-D PROJECT_VER=<version>`.
  2. A separate signing job signs each image (§4.8) with the project release key. It runs in the GitHub Actions environment `release`, with required reviewers, and never runs the ESP-IDF build or the component manager.
  3. The release publishes, for each board:
     - the app image;
     - its bootloader, partition table and `ota_data_initial` parts;
     - a merged `-full.bin`.

     It also publishes `manifest.json`, `install.json` and `SHA256SUMS`.
  - Tags containing `-` are published as prereleases, so they never become "latest".
  - The workflow ends with `gh workflow run pages.yml --ref main`, so the installer picks up the new firmware.

  ```json
  {"version": "1.1.0",
   "boards": {"amoled-175c": {"url": "https://github.com/aivsomkar/openmausbot-gadget-sdk/releases/download/v1.1.0/openmausbot-gadget-amoled-175c-1.1.0.bin",
                              "size": 1234567, "sha256": "…", "sig": "…", "key_id": "r1"}}}
  ```

  `manifest.json` uses absolute `https://github.com/aivsomkar/openmausbot-gadget-sdk/releases/download/<tag>/<file>` URLs.
- **Keys:**
  - The release private key exists only as a secret of the `release` environment.
  - Its public half, with `key_id`, is compiled into the firmware and into OpenMausBot.
  - The test signing key, which the fake host uses, is compiled only into simulator and test builds. Its Kconfig option is off in every board's `sdkconfig.defaults`.
  - Release CI asserts that the release build's key table holds only `r*` ids.
- **Checking:**
  - While at least one gadget is paired, the companion (`releases.ts`) fetches `https://github.com/aivsomkar/openmausbot-gadget-sdk/releases/latest/download/manifest.json`. It does so once a day, and when Settings → Remote access opens, through Electron's `POST /firmware-updates/check` (§6.1).
  - Versions compare by SemVer 2.0.0 precedence. A gadget whose board has a newer version shows **Update available**.
- **Updating** (a manual button in v1, through the Electron-only `POST /devices/:id/firmware-update`):
  1. The companion downloads the image.
  2. It checks the size, SHA-256 and signature.
  3. It runs §4.8 with the gadget, with the manifest's board id as `fw.offer.board`.
  4. The row shows progress, then the new version once `fw.installed` arrives.
- **Custom builds:** firmware built locally carries a version ending in `-dev` (`0.0.0-dev` unless the maker passes `PROJECT_VER`, §5.1). Any `fw` that ends in `-dev`, or does not match `/^\d+\.\d+\.\d+(-[0-9A-Za-z.]+)?$/`, counts as a custom build. Its row shows **Custom build** with no Update button, so official releases never overwrite a maker's firmware. Custom builds update over USB.

## 9. Security model

| Threat | Mitigation |
|---|---|
| A web page opens a gadget socket | `Origin` header refused |
| A gadget is reached from the internet | `/gadget` is attached only to the LAN listener, never to the hosted HTTPS origin |
| A device impersonates a paired gadget | id derives from the public key; every session proves possession of the private key over a fresh nonce bound to `host_id` |
| A recorded session is replayed | Fresh 32-byte nonce per connection |
| Someone on the LAN pairs a gadget | Needs a valid six-digit code from the desktop's pairing window (120 s, 5 attempts, single use); the device cap is checked only after a code matches |
| A gadget does more than a phone could | The hub checks the companion allowlist before every harness call and uses the gadget's own device identity |
| A gadget creates standing grants | Gadgets see only Allow and Deny; the hub never calls `/always-allow` |
| A bot flips a relay unexpectedly | `confirm` actions are never auto-approved, also in Full access: they ask every time unless the person chose Always allow for that action on that gadget on the desktop, and that grant lapses when the action's declared entry changes; unknown risk means confirm; args are validated before the card; at most 20 actions per turn; the companion refuses an action that changed after approval |
| A bot's shell calls the control port to skip the card | `/gadget/*` routes need the gadget control token, which only Electron, the harness and the companion hold; the agents proxy never sees it. Residual: a local process can still open a pairing window, read its code, enroll a device and answer cards (Known limitation below). This already applies to phones today |
| A malicious image flashes the gadget | The image must be signed by an embedded release key; probation and rollback; the test key never ships in release firmware |
| Someone on the LAN impersonates MausBot to a gadget | Not prevented in v1: the gadget does not authenticate the host (plain `ws://`, §4.1). An impostor can hear speech, show text and send `act`. `confirm` is enforced only on the Mac, so `AGENTS.md` (§5.10) tells makers not to register actions whose misuse is unsafe. OTA stays protected by signatures |
| A gadget is lost or stolen | **Remove** in Settings → Remote access revokes it. The private key sits in plain NVS unless the maker opts into `GADGET_NVS_ENCRYPT` |
| Conversation content on the LAN | Plain `ws://` on the home network in v1, like the companion's existing LAN HTTP. Encrypted transport is out of scope for v1 |

**Known limitation (existing, not new).**

- The companion control port's `GET /state` and pairing routes are unauthenticated on loopback. A local process could read an open pairing code there, and use it to enroll a device of its own, including approving its own cards through a device it enrolled itself.
- Gadgets don't make this worse: a phone enrolled the same way can do the same today. `POST /pairing {botId}` lets such a process choose the new gadget's bot.
- A follow-up should require the Electron-minted token on those routes too. The control port's own built-in page calls `/state` and `/pairing` without a token today (`companion/src/control.ts`), so that page has to change with it.
- Host authentication (a host key that the gadget pins at enrollment) is not part of v1.

## 10. Testing

| Layer | Tests |
|---|---|
| Protocol | Vectors (§4.9), including high-S, short-DER and negative cases, pass in the firmware's C tests and OpenMausBot's TypeScript tests; CI regenerates them and checks `git diff --exit-code` |
| Firmware core | CTest unit tests against a fake HAL: handshake and enrollment, the reaction to every handshake `error` and the backoff (§4.3), every op, one turn in flight and disconnects (§4.4), jitter buffer, OTA state machine, timeouts and probation, console parsing (quoting, `@omb` lines), PSA crypto on mbedTLS 3.6.7 and 4.2.0 |
| UI | Headless simulator snapshots (virtual clock, fixed seed) of every screen at 466 round, 240×240 and 320×240, compared to committed PNGs |
| Art | Total image bytes within each size profile's budget (§5.5); the art drift check on linux-x64 |
| Simulator end to end | Scripted runs against `tools/fake-host` with WAV mic and speaker files: enroll, voice turn, barge-in, ask/answer, post, card, action, OTA, forced rollback |
| Firmware builds | CI builds all four boards one after another in one checkout on `espressif/idf:v6.0.3`, each with its own `sdkconfig`, and asserts `app.bin` ≤ the OTA slot; release CI fails on a `-dev` `PROJECT_VER`; the optional v5.5.5 job (§5.1) |
| Companion | `ws.ts` with Node 24's built-in WebSocket client and raw `node:net` upgrades (Origin, missing subprotocol, malformed frames); the drain-aware sender with a full 466×466 image; shutdown completes with a gadget connected; enrollment and signature tests on the vectors; a new connection replaces a session only after `prove`; `enrollGadget` and token-less records; name last-writer-wins; hub ↔ fake-harness tests for every row of §6.2, including each push case. One test per **Stop** case (§6.2), and barge-in sending `speak.stop` and `done` before the new turn's first message. Push: an unprompted turn with 3 items gives exactly one post; a routine run gives exactly one post (`kind: routine`); a late reply to an ended gadget turn gives none |
| Harness | `/api/stt` with a fixed WAV through both providers (the cloud one mocked), the body cap and 413, a silent WAV giving `{text: ""}`, and a 401 from ElevenLabs giving `stt_key_forbidden`; `/api/tts/speak` PCM formats, `pcm_unsupported`, the WAV parser and resampling; the `/api/internal/gadgets*` routes and the approval gate, including an Always allow grant lapsing when the action's entry changes; catalog goldens with the `+gadgets` overlays; the Codex `tool_timeout_sec` argv and the Claude `MCP_TOOL_TIMEOUT` environment |
| Desktop UI | A `renderToStaticMarkup` component test for the "Pair a gadget" panel and the gadget row in Remote access; Electron node tests for the new IPC and control routes |
| End to end | The simulator against an isolated development MausBot instance with a fake engine: talk, approval, push, stop, a bot tool, an update |
| Hardware | A manual checklist per board: boot log, touch and buttons, PWR hold, mic level, speaker, battery (including the lcd-154 power latch), installer reflash keeping the pairing, pairing, a voice turn, an approval, OTA, power loss during OTA, rollback |

## 11. Original-work rule

Everything in this repository is written for this project. Contributors and coding agents must not copy code, documentation, art or protocol text from other gadget SDKs or device firmware projects, and the repository does not refer to them.

Hardware bring-up uses vendor datasheets, schematics and the vendors' published driver components, which are fetched at build time under their own licenses. Third-party libraries are dependencies, listed with their licenses in `THIRD_PARTY.md`:

- In the firmware or the simulator:
  - ESP-IDF and its components (Apache-2.0);
  - the managed components `espressif/esp_websocket_client`, `espressif/mdns`, `espressif/esp_lcd_co5300`, `espressif/esp_codec_dev` and `waveshare/esp_lcd_touch_cst9217`, which is not part of ESP-IDF (all Apache-2.0);
  - LVGL (MIT), with its bundled lodepng (zlib) for headless snapshots;
  - SDL2 (zlib);
  - cJSON (MIT);
  - mbedTLS (Apache-2.0);
  - wslay, the simulator's WebSocket client (MIT);
  - the Montserrat font (OFL-1.1).
- Build-time and tooling only, never shipped in firmware:
  - `@noble/curves` 2.4.0 (MIT), in the vector generator only;
  - `lv_font_conv` (MIT), for font conversion;
  - the `ws` npm package (MIT), in the fake host;
  - the SVG rasterizer `tools/art` uses, for example `@resvg/resvg-js` (MPL-2.0), and `pngjs` (MIT);
  - the installer's `esptool-js` (Apache-2.0), `esbuild` (MIT) and `spark-md5` (WTFPL or MIT).

**Trademark.** The README, `NOTICE` and `tools/art/source/README.md` each carry exactly this sentence: "The OpenMausBot name, the MausBot name and the Maus mascot are trademarks of Supamaus Software Private Limited."

The Maus art is generated from OpenMausBot's own mascot geometry, and `tools/art/source/README.md` records the source commit. Before the SDK is published, Omkar confirms that the expression geometry is project-owned. This is a pre-publish check, not a build blocker.
