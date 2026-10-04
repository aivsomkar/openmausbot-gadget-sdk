# The OpenMausBot gadget protocol, `openmausbot-gadget/1`

- **Status:** normative, protocol version 1 (`proto: 1`).
- **Scope:** everything a gadget (an ESP32 board or the desktop simulator) and a host (the gadget hub inside the MausBot companion, or `tools/fake-host`) say to each other.
- **Numbering:** sections keep the numbers of the design spec (`docs/specs/2026-10-04-openmausbot-gadget-design.md` §4), so "PROTOCOL.md §4.n" and "spec §4.n" name the same section.
- **Test vectors:** `protocol/vectors/` (§4.9). Every implementation must pass them.

Words used here:

- **Gadget:** the device side. It holds a P-256 key pair and talks to one host.
- **Host:** the side that accepts gadget connections. In MausBot it runs only while **Remote access** is on.
- **Turn:** one conversation exchange, from the gadget's `voice.begin` or `say` until the host's `done`.
- **Stream:** a numbered sequence of binary frames that belongs to one `voice.begin`, `speak.begin`, `image.begin` or `fw.offer`.

## Op index

Every op in version 1. The direction is "g→h" (gadget to host) or "h→g" (host to gadget).

| op | Direction | Section |
|---|---|---|
| `hello` | g→h | 4.3 |
| `challenge` | h→g | 4.3 |
| `prove` | g→h | 4.3 |
| `ready` | h→g | 4.3 |
| `error` | h→g | 4.3 |
| `settings` | h→g | 4.3 |
| `voice.begin` | g→h | 4.4 |
| `voice.end` | g→h | 4.4 |
| `voice.drop` | g→h | 4.4 |
| `say` | g→h | 4.4 |
| `stop` | g→h | 4.4 |
| `heard` | h→g | 4.4 |
| `working` | h→g | 4.4 |
| `reply` | h→g | 4.4 |
| `done` | h→g | 4.4 |
| `speak.begin` | h→g | 4.4 |
| `speak.end` | h→g | 4.4 |
| `speak.stop` | h→g | 4.4 |
| `ask` | h→g | 4.5 |
| `answer` | g→h | 4.5 |
| `ask.close` | h→g | 4.5 |
| `post` | h→g | 4.6 |
| `card` | h→g | 4.7 |
| `card.close` | h→g | 4.7 |
| `image.begin` | h→g | 4.7 |
| `image.end` | h→g | 4.7 |
| `act` | h→g | 4.7 |
| `act.result` | g→h | 4.7 |
| `sense` | g→h | 4.7 |
| `event` | g→h | 4.7 |
| `fw.offer` | h→g | 4.8 |
| `fw.ready` | g→h | 4.8 |
| `fw.fail` | g→h | 4.8 |
| `fw.progress` | g→h | 4.8 |
| `fw.commit` | h→g | 4.8 |
| `fw.installed` | g→h | 4.8 |

## 4.1 Transport

- One WebSocket per gadget: `ws://<host>:8810/gadget`, subprotocol `openmausbot-gadget.1`. LAN only in v1: the host serves `/gadget` on its LAN listener and nowhere else.
- **Upgrade refusals.** The host answers with plain HTTP and closes the socket:

  | Request | Answer |
  |---|---|
  | Any `Origin` header (so a web page cannot open a gadget session) | `403` |
  | Not a `GET` upgrade, a bad `Sec-WebSocket-Key`, or `openmausbot-gadget.1` missing from `Sec-WebSocket-Protocol` | `400` |
  | `Sec-WebSocket-Version` other than `13` | `426` with `Sec-WebSocket-Version: 13` |

- The host accepts no extensions: it declines `permessage-deflate`. The gadget sends no `Origin` header.
- **Text frames** are UTF-8 JSON objects with an `op` field.
  - Optional fields are omitted, never sent as `null`.
  - Receivers ignore unknown ops and unknown fields. A text frame that is not a JSON object with a string `op` is ignored too.
  - A text frame that is not valid UTF-8 closes the connection with code 1007.
- **Binary frames** start with a 2-byte header:

  | Byte | Field |
  |---|---|
  | 0 | kind: `0x01` mic audio (gadget → host), `0x02` speaker audio, `0x03` image rows, `0x04` firmware (all host → gadget) |
  | 1 | stream id, 1–255, assigned by whoever sent the matching `*.begin` or `fw.offer` |
  | 2… | payload |

  - A frame shorter than 2 bytes, with stream id 0 or with an unknown kind is ignored.
  - **Audio:** mono PCM16 little-endian at the rate given in its `begin` message.
    - Mic frames carry 20 ms: 320 samples, a 640-byte payload.
    - Speaker frames carry 40 ms: 640 samples (1280 bytes) at 16 kHz, 960 samples (1920 bytes) at 24 kHz. The last frame of a stream may be shorter.
  - **Image:** RGB565 little-endian, row-major, continuing where the previous frame stopped. A frame carries at most 8190 payload bytes and may split a row.
  - **Firmware:** a u32 little-endian byte offset, then 1–4096 bytes of the image.
- **Streams.** Gadget-assigned streams (`voice.begin`) and host-assigned streams (`speak.begin`, `image.begin`, `fw.offer`) are separate number spaces. Each side cycles 1 → 255 → 1 and never reuses an id that is still active. A binary frame for a stream that is not active is ignored.
- **Liveness:** the host sends a WebSocket ping every 15 s. Either side treats 45 s without any inbound frame as a dead connection.
- **Limits:** text frames ≤ 16 KiB (16384 bytes), binary frames ≤ 8 KiB (8192 bytes, header included), an utterance ≤ 60 s, one conversation turn in flight per gadget. A larger frame closes the connection with code 1009.
- **Handshake deadline:** the host closes a connection that has not reached `ready` within 10 s, with code 1008.
- **Close codes** the host uses:

  | Code | When |
  |---|---|
  | 1000 | Normal close, and the close right after an `error` op |
  | 1001 | The host is shutting down |
  | 1002 | WebSocket protocol error |
  | 1007 | A text frame that is not valid UTF-8 |
  | 1008 | The handshake deadline passed, or the send buffer grew past 256 KiB |
  | 1009 | A frame over the limits above |
  | 1011 | Internal host error |

  The gadget reacts to the `error` op (§4.3), never to the close code.

**Encodings.** These are normative, and the vectors (§4.9) check them.

| Value | Encoding |
|---|---|
| `pubkey`, `nonce`, `sig` | Base64 per RFC 4648 §4: standard alphabet, with padding. Hosts reject non-canonical base64: the value must survive a decode and re-encode unchanged. |
| `pubkey` bytes | Exactly 65 bytes, starting `0x04` (SEC1 uncompressed). Anything else is rejected. |
| `<nonce>` in signed text | The exact base64 string sent in `challenge` (44 characters), not the decoded bytes. |
| `host_id` | 32 lowercase hex characters, `/^[0-9a-f]{32}$/`. The gadget rejects a `challenge` whose `host_id` fails this check. |
| `<size>` | Base-10, no leading zeros. |
| SHA-256 | Lowercase hex everywhere (`fw.offer`, the signed firmware text, `manifest.json`). |
| Firmware version | The git tag without the leading `v`, for example `1.1.0`. Custom builds end in `-dev`; a local build without a version reports `0.0.0-dev`. Versions compare by SemVer 2.0.0 precedence. A `fw` that ends in `-dev`, or does not match `/^\d+\.\d+\.\d+(-[0-9A-Za-z.]+)?$/`, is a custom build (`versions.json`). The pattern also accepts leading zeros and empty pre-release identifiers, which SemVer 2.0.0 forbids. Such a version is not a custom build. Numeric identifiers compare by value, so `1.01.0` equals `1.1.0`. An empty identifier counts as alphanumeric: it ranks above a numeric one and below any other alphanumeric one (`1.0.0-rc..1` > `1.0.0-rc.1`, `1.0.0-.` < `1.0.0-a`). |
| `turn` | Unique per boot, ≤ 32 characters: `t` + 8 lowercase hex chosen at boot from the random number generator + `-` + a decimal counter starting at 1 (for example `t3f9a0c2b-7`). |
| Host-sent ids | `ask.id`, `card.id`, `image.id`, `act.id` and `post.id` are 1–40 ASCII characters from `[A-Za-z0-9_.:-]`. `ready.session` is `s_` + 12 lowercase hex. |

**Screen text.** Every string the host sends for the screen is folded to the gadget's `caps.screen.text` charset (§4.4).

## 4.2 Identity

- On first boot, once its radio has started (so the hardware random number generator is truly random), the gadget generates a P-256 key pair.
- It keeps the 32-byte private scalar in its own storage (on ESP32: NVS namespace `gadget`, blob `dev_key`; the simulator keeps it in its state folder). Erasing that storage erases the key, and the gadget then has to pair again.
- NVS is not encrypted by default. The opt-in Kconfig option `GADGET_NVS_ENCRYPT` turns on HMAC-based NVS encryption with eFuse key block 5; this permanently burns an eFuse. The browser installer flashes separate parts, so NVS and the key survive a reinstall; only a full erase forces pairing again.
- `pubkey` is the base64 SEC1 uncompressed point (65 bytes).
- `id` is `gad_` followed by the first 16 lowercase hex characters of SHA-256(pubkey bytes). Because the id derives from the key, no device can claim another's id.
- A lost or stolen gadget is revoked on the host (in MausBot: **Remove** in Settings → Remote access). The host then answers its connections as in §4.3.

## 4.3 Handshake

```
gadget → hello      {proto, id, pubkey, name, board, fw, caps, actions, sensors}
host   → challenge  {nonce, host_id, host_name}       or   error {code, message}, then close
gadget → prove      {sig, enroll?}
host   → ready      {session, bot, settings}          or   error {code, message}, then close
```

Rule 1's `proto` and `pubkey`/`id`/`board` checks (Host rules below) run on `hello`. When one fails, the host answers `hello` with `error` instead of `challenge` and closes. The gadget handles `error` in every handshake state (waiting for `challenge` or for `ready`) with the reactions in "How the gadget reacts".

The gadget sends `hello` as its first frame. Until `ready`, the host ignores every op other than the first `hello` and one `prove` after its `challenge`.

### `hello`

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

- `board` matches `/^[a-z0-9-]{1,32}$/`.
- Every `caps` member is optional.
  - A gadget without a speaker omits `speaker` and never receives speech.
  - `speaker.rate` is 16000 or 24000. The host treats any other value as no speaker.
  - **Rate rule:** a board whose mic and speaker codecs share one I2S clock must advertise `speaker.rate` equal to `mic.rate` (16000). A board with separate I2S controllers may advertise 24000.
  - `mic.rate` is 16000 in v1, because the host's speech-to-text accepts only 16 kHz.
  - `ota.max` is the board's OTA slot size in bytes.
  - `screen.text` is the charset the screen can draw (§4.4). `"latin1"` is the only value in v1 and the default.
- Each action has a `name` matching `/^[a-z][a-z0-9_.-]{0,31}$/`, a `description`, a JSON Schema object `params` and a `risk`. `risk` is `safe` or `confirm`; a missing value means `confirm`.
- **Limits:** `name` ≤ 32 characters; at most 16 actions; each `description` ≤ 200 characters; each `params` ≤ 1 KiB serialized; the whole `hello` ≤ 16 KiB. The gadget enforces them when an action is registered. The host cuts `name` to 32 characters and drops any action that breaks a limit.
- **Default name:** a gadget with no stored name sends `"Maus "` + the first 4 hex characters of its id after `gad_` (for example `Maus b18b`).
- `sensors` holds the latest readings (§4.7). A board without a battery sends `"sensors": {}`.

### `challenge`

- `nonce` is 32 fresh random bytes in base64 (44 characters), new for every connection.
- `host_id` is the host's stable random id (§4.1 encodings). MausBot advertises the same value as `id=` in the TXT record of its `_openmausbot._tcp` mDNS service.
- `host_name` is the host's display name, folded to the screen charset (§4.4) and cut to 64 UTF-8 bytes. The gadget shows it on its Setup and Offline screens.

### `prove`

`sig` is base64 DER ECDSA-P256-SHA256 over the UTF-8 text

```
openmausbot-gadget/1
prove
<id>
<nonce>
<host_id>
```

with `\n` line endings and no trailing newline. `<nonce>` is the base64 string exactly as received. Signing `host_id` binds the proof to one host.

- `enroll` is the six-digit pairing code. The gadget sends it only while it holds a code that has not yet been used.
- Signatures are not normalized to low S. A verifier must accept both high-S and low-S signatures (§4.9).

### Host rules

1. Reject with `proto_unsupported` if `proto` is not 1. Reject with `bad_sig` if `pubkey` is not canonical base64 of 65 bytes starting `0x04`, if it does not hash to `id`, if `board` does not match `/^[a-z0-9-]{1,32}$/`, or if `sig` does not verify.
2. **Known id:** `pubkey` must equal the stored key (else `bad_sig`). Any `enroll` is ignored. Go to `ready`.
3. **Unknown id with `enroll`:** validate and consume the code against the host's pairing window: 120 s, 5 attempts, single use, shared with every other kind of device the host pairs. Only `/^\d{6}$/` is accepted.
   - A wrong, expired or used-up code returns `bad_code`, and `message` says which ("wrong", "expired" or "used up"). Wrong gadget codes use up the same 5 attempts as any other device.
   - The device cap is checked only after the code matches. If the host is full, it returns `device_limit` and the window stays open, so the person can remove a device and retry.
   - On success, the host creates the gadget's record, consumes the window and goes to `ready`.
   - In MausBot, the new gadget's bot is the one picked in Settings → Remote access → **Pair a gadget**. If the code came from a pairing flow without that picker, the host takes the first match over the visible bots: the first chief-of-staff bot without a section; else the first pinned bot; else the first unsectioned bot that is not chief of staff; else the first visible bot.
4. **Unknown id without `enroll`:** `enroll_required`.

A new connection replaces an older live session with the same id only after its `prove` verifies. The older one then receives `error replaced`. Until then the older session is untouched.

### `ready`

```json
{"op": "ready", "session": "s_81c2a3d4e5f6", "bot": {"id": "b_jev", "name": "Jev"},
 "settings": {"speak_pushes": false}}
```

- On `ready`, the gadget stores `host_id` and `host_name` and clears its stored code.
- A gadget with no bot gets `"bot": {"id": "", "name": ""}`. Its turns end with `done {outcome: "failed", reason: "Pick a bot for this gadget in MausBot → Settings → Remote access"}`.

### `settings`

`settings {bot, settings, name?}`, host → gadget. Sent whenever the person changes the gadget's bot, its settings or its name on the host. `bot` and `settings` have the same shape as in `ready`. The gadget stores `name` and sends it in later `hello`s.

**Name, last writer wins.** The name can be changed on the host and on the gadget (console `name`). On every `hello`, the host updates its record's `name`, `board`, `fw` and last-seen time from the hello. The one exception: a host-side rename made while the gadget was offline is marked pending, so at the next `hello` the host keeps its name and sends it in `settings` right after `ready`.

### `error`

`error {code, message}`, host → gadget, then the host closes with code 1000. The codes:

| Code | Meaning |
|---|---|
| `proto_unsupported` | `proto` is not supported |
| `enroll_required` | Unknown id and no `enroll` |
| `bad_code` | The pairing code is wrong, expired or used up |
| `bad_sig` | Bad `pubkey`, id mismatch, a bad `board` id, or the signature does not verify |
| `revoked` | The host removed this gadget |
| `device_limit` | The code matched but the host is full |
| `replaced` | Another connection with the same id took over |

### How the gadget reacts

The `pair` and `error` values are the console's `@omb` status fields.

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
- A `pair <code>` console command stores the code and reconnects at once.

## 4.4 Conversation

| Message | Direction | Meaning |
|---|---|---|
| `voice.begin {turn, stream, rate}` | gadget → host | An utterance starts; mic frames follow on `stream`. `turn` is gadget-chosen and unique per boot (§4.1). `rate` is 16000 in v1 |
| `voice.end {turn, ms}` | gadget → host | The utterance is complete; `ms` is its length |
| `voice.drop {turn}` | gadget → host | Discard the utterance |
| `say {turn, text}` | gadget → host | A typed message, ≤ 2000 characters |
| `stop {turn?}` | gadget → host | Stop the current turn |
| `heard {turn, text}` | host → gadget | What speech-to-text heard |
| `working {turn, text}` | host → gadget | A short live phrase about what the bot is doing; empty text clears it |
| `reply {turn, text, final}` | host → gadget | The bot's reply so far. `text` is cumulative; `final: true` marks the finished text |
| `done {turn, outcome, reason?}` | host → gadget | The turn is over. `outcome` is `ok`, `failed` or `stopped`; `reason` is short text for the screen |
| `speak.begin {stream, rate, turn?}` | host → gadget | Speech follows on `stream` at `rate` (16000 or 24000, the gadget's `caps.speaker.rate`) |
| `speak.end {stream}` | host → gadget | All speech frames are sent; play out the buffer |
| `speak.stop {stream}` | host → gadget | Stop playing now |

**Screen text.**

- Reply text is already shaped for the screen:
  - Markdown markers are stripped, while newlines and link labels are kept.
  - Code blocks become `[code]`, images `[image]` and bare URLs `[link]`.
  - Emoji are dropped, and the text is folded to the gadget's `caps.screen.text` charset.
- Every string the host sends for the screen is folded to `caps.screen.text`: `heard`, `working`, `reply`, the `done` reason, the `ask` title, body and labels, `post` text, the `card` title and body, `ready.bot.name`, `settings.bot.name` and `challenge.host_name`. `ask`, `post` and `card` bodies also get the reply shaping above.
- In v1 the only charset is `"latin1"`, which is also the default when `screen` or `text` is missing. It means U+0020–U+007E, U+00A0–U+00FF and newline, plus exactly two more code points: U+2026 `…` and U+2192 `→`.
- Folding maps a character outside the charset to a close equivalent where one exists (curly quotes to straight quotes, dashes to `-`, accented letters outside Latin-1 to their base letter) and drops it otherwise.

**Pacing and limits.**

- The host sends at most one `reply` every 250 ms. A `reply` that would exceed the text frame limit is cut from the start, and the cut text begins with `…`.
- The host paces speech frames to real time, at most 0.5 s ahead, so a 1 s jitter buffer on the gadget is enough.
- A new `speak.begin` replaces any stream that is playing.
- Speech for a `post` (§4.6) has no `turn`. The host never starts it while another stream is playing: it sends the post's `speak.begin` only after the earlier stream has played out in real time (at most 0.5 s after that stream's `speak.end`). A reply's `speak.begin` replaces a post stream that is playing; the gadget never queues streams itself.
- The host keeps at most 60 s of an utterance's audio. `say` text is at most 2000 characters; a host cuts longer text to 2000 characters.

**Turn rules.**

- If speech-to-text hears nothing, the turn ends with `done {outcome: "failed", reason: "Didn't catch that"}` and nothing is sent to the bot.
- A `voice.begin` whose `rate` is not 16000 ends the turn with `done {outcome: "failed", reason: "Unsupported mic rate"}`.
- **One turn in flight.** A turn is in flight from its `voice.begin` or `say` until the host sends its `done`.
  - The host ignores `stop` for a turn that is not in flight. `stop` without `turn` stops the turn in flight, if any.
  - A `voice.begin` or `say` that arrives while a turn is in flight stops the old turn the same way `stop` does.
  - Stopping a turn: the host sends `speak.stop` for the turn's speech stream if one has begun, drops any speech still queued for it, then sends `done {turn: <old>, outcome: "stopped"}`, all before the new turn's first message. Nothing else is sent for the stopped turn afterwards.
  - `voice.drop` stops its turn the same way: the host discards the audio, sends nothing to the bot and sends `done {turn, outcome: "stopped"}`.
- **`done` ends the turn, not its speech.** A host may send `done {outcome: "ok"}` before the turn's speech stream has begun or ended; MausBot sends `done` when the bot's turn completes, often while speech is still being synthesized or streamed.
  - The gadget keeps playing a stream until its `speak.end` has played out or a `speak.stop` arrives.
  - When a new `voice.begin` or `say` arrives while an earlier turn's speech is still playing, the host sends `speak.stop` for it before the new turn's first message.
  - The gadget may also stop playback locally at any time and ignore later frames for that stream.
- **Disconnects.** If the socket drops during a turn (while recording, during speech-to-text or while waiting for the bot), the gadget ends the turn locally with "Connection lost" and never resumes it.
  - The host drops any buffered mic audio, and nothing is sent to the bot if the message had not been sent yet.
  - If the message already reached the bot, the bot's turn keeps running and the host unbinds it. Its reply is not pushed (§4.6); the person reads it on the desktop or a phone.

## 4.5 Approvals

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
- `answer.option` is the `id` of one of the ask's options. The gadget answers at most once; the host ignores an answer for an ask that is not open.
- `ask.close` reasons are `answered` (here or on another device), `expired` and `withdrawn`. `expires_s`, when present, is how long the ask stays open.
- The host sends one ask at a time per gadget, oldest first. Asks that are still open are sent again on reconnect.

## 4.6 Push

`post {id, bot, kind, text, speak}`, host → gadget. `bot` is `{id, name}`, as in `ready`.

- Something the bot produced on its own, shown as a toast with a chime. Replies to a person's message from any device (desktop, phone or gadget) are **not** pushed; the person is already looking at that screen.
  - A **person's message** is a user message the person typed or spoke on any device. A message another bot delivered (a peer ask) and a routine's prompt are not a person's message.
  - A late reply to this gadget's own turn, for example after the turn ended or the gadget disconnected, answers a person's message, so it is not pushed either.
- At most one `post` per bot turn.
- `kind` is one of:
  - `routine`: a routine run for the gadget's bot completed, failed, was missed or is waiting on the person;
  - `message`: the bot wrote unprompted, for example when a background task or a teammate's handoff finished.
- `speak` follows the gadget's "Read pushes aloud" setting (`settings.speak_pushes`). When `speak` is true and the gadget has a speaker, speech without a `turn` follows (§4.4).

## 4.7 Display, actions and sensors

| Message | Direction | Meaning |
|---|---|---|
| `card {id, title, body, ttl_s}` | host → gadget | Show a card; `ttl_s: 0` keeps it until dismissed |
| `card.close {id}` | host → gadget | Remove the card or image with that `id` |
| `image.begin {id, stream, w, h, ttl_s}` / rows / `image.end {stream}` | host → gadget | An image no larger than `caps.image`, already converted to RGB565; `w * h * 2` bytes follow on `stream` |
| `act {id, name, args}` | host → gadget | Run a declared action; `args` is an object (`{}` when there are none) |
| `act.result {id, ok, data?, error?}` | gadget → host | Exactly one per `act`; the host gives up after 15 s |
| `sense {…}` | gadget → host | Latest readings: `battery_pct`, `charging`, plus board-specific values |
| `event {name, data?}` | gadget → host | Something happened on the gadget, for example `button.long_press` |

- Hosts cut card titles to 80 characters and bodies to 600 characters. The gadget keeps 192-byte titles and 1536-byte bodies and truncates longer text with `…`.
- A gadget sends `sense` when `battery_pct` changes by 1 or more or `charging` flips, at most once every 10 s. A board without a battery never sends it.
- `event` is **informational** in v1. The host keeps the last 10 events per gadget and may show them to bots; nothing else reacts to them.
- The host keeps `sense` values in memory and persists them at most every 60 s.

## 4.8 Firmware updates

```
host   → fw.offer     {stream, board, version, size, sha256, sig, key_id}
gadget → fw.ready     {stream}                      or  fw.fail {stream, code}
host   → chunks on binary kind 0x04
gadget → fw.progress  {stream, offset}              every 16 KiB and at the end
host   → fw.commit    {stream}
gadget → (checks size, SHA-256 and signature, marks the new slot, restarts)
gadget → fw.installed {version}                     after its first `ready` on the new image
```

- `sig` is base64 DER ECDSA-P256-SHA256, made with a release key, over the UTF-8 text:

  ```
  openmausbot-gadget/1
  firmware
  <board>
  <version>
  <size>
  <sha256 lowercase hex>
  ```

  with `\n` line endings and no trailing newline. `<size>`, `<sha256>` and `<version>` use the encodings in §4.1.
- `key_id` names one of the public keys compiled into the firmware. Firmware may hold several keys, so a key can be rotated. Release keys have ids starting with `r` (`r1`, …). Simulator and test builds also hold the test key `t1` (§4.9); release builds never do.
- **Accepting an offer.** Only a session that passed `prove` may offer. The gadget checks in this order and answers `fw.fail` with the first that applies:
  1. `wrong_board` when `board` is not its own board id;
  2. `same_version` when `version` equals the running version;
  3. `too_large` when `size` exceeds `caps.ota.max`;
  4. `unknown_key` when `key_id` is unknown;
  5. `bad_sig` when `sig` does not verify over the text built with the gadget's **own** board id.

  It answers `busy` when another update is in progress. An older signed version is accepted, since anti-rollback is off.
- **Chunks.** Each kind-0x04 frame carries a u32 little-endian offset and up to 4096 bytes. Offsets must be contiguous from 0, or the update fails with `sequence`.
- **Flow control.** The host keeps at most 64 KiB sent but not yet acknowledged by `fw.progress`. The gadget sends `fw.progress {offset}` with the number of bytes durably written, each time that number crosses a multiple of 16 KiB and when it reaches `size`. The host sends `fw.commit` after the `fw.progress` whose offset equals `size`.
- **Timeouts.** The host gives up if `fw.ready` doesn't arrive within 10 s. The gadget fails with `timeout` if no chunk arrives for 30 s, or if `fw.commit` doesn't arrive within 30 s of the last byte.
- **Commit.** The gadget checks that it received exactly `size` bytes whose SHA-256 equals `sha256`, else `checksum`. A storage write error is `flash`.
- **Probation:**
  - A new image boots on probation. The firmware runs its own 5-minute timer.
  - If the image hasn't reached `ready` when the timer fires, the firmware marks it invalid and reboots into the previous image (on ESP32: `esp_ota_mark_app_invalid_rollback_and_reboot()`).
  - On the first `ready` the firmware marks the image valid (on ESP32: `esp_ota_mark_app_valid_cancel_rollback()`) and then sends `fw.installed {version}`.
  - Any reboot before that point, including a crash, the watchdog or power loss, returns to the previous image.
  - Anti-rollback is never enabled.
- **Failure codes:** `too_large`, `wrong_board`, `same_version`, `unknown_key`, `bad_sig`, `busy`, `flash`, `sequence`, `checksum`, `timeout`.

## 4.9 Versioning and test vectors

- `proto` is an integer. Adding an op or an optional field does not change it; changing what an existing field means does.
- `protocol/vectors/` holds JSON vectors that both sides must pass. Every file is `JSON.stringify(value, null, 2)` plus a newline, with the envelope `{"vectors": "<file stem>", "version": 1, "cases": […]}` (`versions.json` has `compare` and `custom` instead of `cases`). Every case has a unique `name`.

  | File | Checks |
  |---|---|
  | `identity.json` | id derivation from a fixed key: the RFC 6979 A.2.5 P-256 key and the test key `t1` |
  | `rfc6979.json` | the RFC 6979 A.2.5 P-256/SHA-256 "sample" (high-S) and "test" (low-S) signatures, with `k`, `r`, `s`, raw and DER forms |
  | `prove.json` | the exact `prove` text and its deterministic signature; its low-S twin; a low-S control; and the negative cases: a changed `host_id`, a `pubkey` that does not hash to `id`, non-canonical base64, a compressed key, a truncated signature and a malformed `host_id` |
  | `der.json` | DER ↔ raw conversion: 72-byte signatures, a short 69-byte signature, and invalid DER (non-minimal or negative integers, trailing bytes, a wrong tag) |
  | `base64.json` | canonical and non-canonical base64 |
  | `firmware.json` | the firmware signature text and its `t1` signature. `board` is the board the image was signed for and `gadget_board` the verifying gadget's own id. `expect` is the result of the signature check (§4.8 step 5) alone, as when an offer's `board` field was altered to match the gadget; a different board, a changed size and uppercase SHA-256 hex must all fail that check with `bad_sig` |
  | `frames.json` | binary frame encodings: mic, speaker, image rows, firmware chunks, and invalid frames |
  | `versions.json` | SemVer 2.0.0 precedence and the custom-build rule, including the leading-zero and empty-identifier inputs of §4.1 |

- **Pinned inputs.** The `prove` vectors use the RFC 6979 A.2.5 P-256 private key `c9afa9d845ba75166b5c215767b1d6934e50c3db36e89b127b8a622b120f6721`, `nonce` = `AAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8=` (bytes 0x00–0x1f) and `host_id` = `000102030405060708090a0b0c0d0e0f`. With these inputs the RFC "sample" signature and the `prove` signature are both **high-S**. (Whether a `prove` signature is high-S depends on `host_id`: `0123456789abcdef0123456789abcdef` gives a low-S one, so the inputs are pinned.)
- **Deterministic signatures.** A case with `"deterministic": true` is the RFC 6979 signature the firmware must reproduce byte for byte. The firmware never normalizes S.
- **Verifiers** must accept high-S signatures. The host and the fake host verify with Node's built-in `crypto`, which does.
- **Generator:** `protocol/tools/gen-vectors.ts` uses `@noble/curves` with `{lowS: false, format: "der"}` and `getPublicKey(sk, false)`. The library's defaults (low-S, compressed keys) would produce vectors the firmware cannot match. The generator asserts `Signature.fromBytes(der, "der").hasHighS()` for every case marked `"high_s": true`.
- **Test key `t1`:** private scalar = SHA-256 of the UTF-8 text `openmausbot-gadget/1 test release key t1` (`keys/test-t1.key.hex`, public key in `keys/test-t1.pub.b64`). It exists for simulators, tests and `tools/fake-host`, and is never compiled into release firmware.
- **Byte stability:** `.gitattributes` has `protocol/vectors/** -text`, in this repository and in any vendored copy. `SHA256SUMS` lists `<sha256>  <file>` for every vector file over its exact bytes. CI regenerates the vectors and fails on any difference.
- The firmware's C tests and OpenMausBot's TypeScript tests read the same files. OpenMausBot vendors a copy that its tests compare by hash.
