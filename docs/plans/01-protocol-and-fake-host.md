# P1: Protocol, test vectors and fake host — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Give the gadget SDK its normative protocol document, byte-stable test vectors that the firmware and MausBot both run, and a Node fake host that speaks the host side of `openmausbot-gadget/1`, on branch `p1-protocol`.

**Architecture:** `protocol/PROTOCOL.md` restates spec §4 as the normative reference, with every encoding and wire detail the contract pins. A small TypeScript library (`protocol/lib/`, Node built-ins only) implements those encodings; `protocol/tools/gen-vectors.ts` produces the vectors with `@noble/curves` (deterministic, high-S kept), and `protocol/test/` checks every vector independently with `node:crypto`. `tools/fake-host/` builds on the same library and the `ws` package: an HTTP server whose only WebSocket path is `/gadget`, one `GadgetSession` per connection for the handshake, and three features (voice, display, OTA) driven by a JSON-lines control interface on stdin and stdout.

**Tech Stack:** Node ≥ 22.18 running `.ts` directly (erasable TypeScript only), `node:test`, `node:crypto`, `@noble/curves` 2.4.0 (generator only), `ws` 8.22.0 (fake host only), npm, GitHub Actions.

**Spec:** `/Users/omkar/Desktop/openmaus/openmausbot-gadget-sdk/docs/specs/2026-10-04-openmausbot-gadget-design.md` (v1.1): all of §4, §4.9 vectors and §5.9 fake host, plus §5.1 and §11 where they touch this scope. Names, types, paths and formats come from the binding contract `/Users/omkar/Desktop/openmaus/openmausbot-gadget-sdk/docs/plans/00-interfaces.md` (§1.1, §1.3–§1.7, §2.12–§2.13, §3.2, §4.4–§4.7, §5, §6). Executors read both.

## Global Constraints

- **Repository and branch:** work in `/Users/omkar/Desktop/openmaus/openmausbot-gadget-sdk` on branch `p1-protocol`, created from `main` (contract §1.3). No plan commits to `main`.
- **Publishing belongs to Omkar:** never push, open a PR or create a release. Prepare the branch, run the tests and stop.
- **OpenMausBot is read-only:** the checkout at `/Users/omkar/Desktop/openmaus/OpenGrokBot` belongs to another session. Never checkout, stash, reset, edit or fetch there. P1 only reads one file from it with `git -C /Users/omkar/Desktop/openmaus/OpenGrokBot show origin/main:LICENSE`. P1 changes nothing in OpenMausBot.
- **Original-work rule (spec §11):** everything here is written for this project. Never open, fetch, quote or cite third-party gadget SDKs or voice-assistant firmware projects. Vendor and primary sources only (RFCs, Node and npm docs, Apache).
- **Node:** the SDK's tools need Node ≥ 22.18, which runs `.ts` files directly; CI uses Node 24 (spec §5.1). This Mac's default `node` is 22.22.3 (npm 10.9.8); Node 24.14.1 is at `~/.nvm/versions/node/v24.14.1/bin` for the second run in Task 13.
- **Erasable TypeScript only:** no `enum`, no parameter properties, no namespaces; relative imports end in `.ts` (contract §1.4). Every source file starts with `// SPDX-License-Identifier: Apache-2.0`.
- **Dependencies:** the root `package.json` has exactly the contract §1.4 shape: `"@noble/curves": "2.4.0"` and `"ws": "8.22.0"` as devDependencies and nothing else. Install with `npm install` once (Task 1), then `npm ci`.
- **`@noble/curves`** is imported only by `protocol/tools/gen-vectors.ts`, exactly as `import { p256 } from "@noble/curves/nist.js";`, always with `{lowS: false, format: "der"}` and `getPublicKey(sk, false)` (spec §4.9). Its defaults (low-S, compressed keys) produce vectors the firmware cannot match.
- **Verifiers** use Node's built-in `crypto`, which accepts high-S signatures. Nothing ever normalizes S.
- **`ws`** is used only under `tools/fake-host/` (the fake host and its tests).
- **Encodings (spec §4.1):** base64 per RFC 4648 §4 with padding, canonical (decode then re-encode must give the same text); `pubkey` exactly 65 bytes starting `0x04`; `host_id` matches `/^[0-9a-f]{32}$/`; `<size>` base-10 without leading zeros; SHA-256 lowercase hex everywhere; firmware version = the tag without `v`; `turn` ≤ 32 characters.
- **Vector byte stability (spec §4.9, contract §4.4):** `.gitattributes` has `protocol/vectors/** -text`; every vector file is `JSON.stringify(value, null, 2) + "\n"`; `SHA256SUMS` lists `<sha256>  <file>` (two spaces) for every `*.json`, sorted, LF-terminated.
- **PROTOCOL.md keeps the spec's numbering** §4.1–§4.9 (contract §0 item 4).
- **Transport values:** port 8810, path `/gadget`, subprotocol `openmausbot-gadget.1`; text frames ≤ 16 KiB, binary frames ≤ 8 KiB; ping every 15 s; 45 s idle; 10 s to `ready`.
- **License:** Apache-2.0. The README carries exactly: "The OpenMausBot name, the MausBot name and the Maus mascot are trademarks of Supamaus Software Private Limited."
- **Ownership (contract §5.1):** P1 owns `LICENSE`, `.gitignore`, `.gitattributes`, the root `package.json` and lockfile, the `README.md` stub, `.github/workflows/ci.yml` (jobs `protocol` and `fake-host` only), `protocol/**`, `keys/test-t1.*` and `tools/fake-host/**`. Nothing else.

## Review Focus

Five inputs the spec implies but does not spell out, most likely to bite first. Each has a test in the task that owns the code.

1. **A stop or barge-in while reply frames and speech are still scheduled.** Expected: `speak.stop` for the old stream, then `done stopped`, both before anything for the new turn, and no text or speaker frame for the old turn after its `done`. The same holds when `done ok` came before the speech (the order MausBot usually produces): a new turn still gets `speak.stop` for that speech first. Pinned by "stop during speech…", "barge-in…", "barge-in during speech…" and "done before speech…" in `tools/fake-host/test/voice.test.ts` (Task 9).
2. **Text outside Latin-1 in anything sent to a screen** (emoji, curly quotes, dashes, CJK, letters such as Ł). Expected: folded to Latin-1 plus `…` and `→`, never sent raw, and a reply too long for one frame is cut from the start behind `…`. Pinned by the fold tests in `options.test.ts` (Task 7), the `host_name` check in `enroll.test.ts` (Task 7), "no speaker: text only; replies are folded…" (Task 9) and the ask and post tests in `display.test.ts` (Task 10).
3. **A gadget that reconnects with the same key while its old socket is still open** (reboot, Wi-Fi flap), or reconnects with an ask still open. Expected: the old session gets `error replaced` only after the new `prove` verifies, a forged hello for that id is refused, a replay of the gadget's public hello with a prove it cannot sign leaves the old session live and untouched, and the open ask is sent again. Pinned by "a second connection with the same key replaces the first only after its prove" (Task 8) and "an open ask is sent again on reconnect" (Task 10).
4. **Vector files whose bytes change on a Windows checkout** (CRLF conversion), which would break OpenMausBot's hash comparison. Expected: `-text` keeps them byte-exact and the verifier fails on any CR or reformatting. Pinned by "every vector file is 2-space JSON + LF…" and the `SHA256SUMS` test in `protocol/test/vectors.test.ts` (Task 6), and the `git check-attr` step in Tasks 1 and 13.
5. **The process driving the fake host dies or writes a malformed line.** Expected: stdin closing makes the fake host exit 0 instead of lingering on the port, and a bad line gets an `ack` with `"cmd": null` rather than a crash. Pinned by the CLI tests in `tools/fake-host/test/cli.test.ts` (Task 12).

---

## Scope

Every requirement of spec §4 and §5.9 maps to a task. Items the spec describes in this area but another plan builds are named with their owner.

| Requirement | Delivered by | Task |
|---|---|---|
| Repository bootstrap: `LICENSE`, `.gitignore`, `.gitattributes`, `README.md` stub, root `package.json` + lockfile (contract §1.1, §1.4) | repo root | 1 |
| §4.1 transport, binary frames, liveness, limits, encodings, close codes | `PROTOCOL.md` §4.1; `protocol/lib/encoding.ts`, `frames.ts`; fake host transport | 2, 3, 5, 8 |
| §4.2 identity (id from SHA-256 of the 65-byte key) | `PROTOCOL.md` §4.2; `protocol/lib/identity.ts`; `identity.json` | 2, 4, 6 |
| §4.3 handshake: hello limits, challenge, `prove` text, host rules 1–4, `replaced`, `ready`, `settings`, name last-writer-wins, error codes, gadget reactions | `PROTOCOL.md` §4.3; fake host `enroll.ts`, `session.ts`, `settings` command | 2, 7, 8, 10 |
| §4.4 conversation: ops, one turn in flight, stop and barge-in, screen folding, reply pacing and cut, speech pacing, post speech only after playout, `done` before speech (`--done-before-speech`), failure reasons | `PROTOCOL.md` §4.4; fake host `voice.ts`, `speech.ts` | 2, 9 |
| §4.5 approvals: permission Allow/Deny only, ≤ 4 question options, unsupported asks, one at a time, resent on reconnect | `PROTOCOL.md` §4.5; fake host `display.ts` | 2, 10 |
| §4.6 push: `post {kind: routine \| message}`, speech follows the setting | `PROTOCOL.md` §4.6; fake host `post` | 2, 10 |
| §4.7 cards, images (RGB565 rows), `act` with a 15 s timeout, `sense`, `event` | `PROTOCOL.md` §4.7; fake host `display.ts` | 2, 10 |
| §4.8 firmware updates: signed text, offer checks, chunks, 64 KiB window, timeouts, commit | `PROTOCOL.md` §4.8; `firmwareText`; `firmware.json`; fake host `ota.ts` | 2, 4, 6, 11 |
| §4.9 vectors, generator rules, Node verifiers, byte stability, CI regeneration | `gen-vectors.ts`, `vectors/`, `vectors.test.ts`, `.gitattributes`, `ci.yml` | 1, 6, 12 |
| §5.9 fake host: prints one code, enrolls only with it, voice turn with fixed text and a test tone, ask and post on command, OTA of a supplied image signed with the test key, JSON-lines control for tests | `tools/fake-host/` | 7–12 |
| §8 test key: `t1` committed for tests, never in release firmware | `keys/test-t1.*` | 4 |

Out of scope here, with the owning plan (contract §5):

| Item | Owner |
|---|---|
| Gadget-side codec (`gadget_proto.h`), handshake reactions and backoff, OTA state machine and probation in core, C vector tests, the simulator ↔ fake-host e2e runner (`firmware/tests/e2e/run.ts`) and its `test:e2e` script | P2a |
| Screens, Maus art, fonts, snapshots | P2b |
| ESP32 key storage in NVS, opt-in NVS encryption, OTA partitions, ESP probation calls, hardware checklist | P2c |
| `NOTICE`, `THIRD_PARTY.md` (it must list this plan's `@noble/curves` 2.4.0 (MIT) and `ws` 8.22.0 (MIT)), `AGENTS.md`, the full README, `release.yml`, `pages.yml`, release keys, `keys_release.c` | P2d |
| MausBot hub (`ws.ts`, `enroll.ts`, registry, `host_id` and the mDNS TXT record, session mapping, Markdown shaping in `shape.ts`), the vendored vector copy in OpenMausBot | P3a |
| Speech-to-text and PCM text-to-speech | P3b |
| Bot tools, the `recent_events` ring, control routes | P4a |
| OTA delivery, `manifest.json` checks, `tools/release/dev-release.ts` (imports `protocol/lib`) | P4b |

The fake host does not advertise mDNS: gadgets reach it with `host <address>` on the console or `--host` in the simulator (CI always passes `--host`, spec §5.7). It does not shape Markdown either; its scripted texts are plain.

## Contract notes

Nothing pinned by the contract is renamed, retyped or moved, so there is no "Contract deviations" section. These are additive (contract §0 item 3) or are protocol text that P1 owns (contract §2.12 "P1 owns the text"):

- **Private files:** `protocol/lib/der.ts` (strict DER ↔ raw, used by tests), `protocol/test/fixed-values.ts`, and in the fake host `options.ts`, `fold.ts`, `state.ts`, `enroll.ts`, `context.ts`, `features.ts`, `speech.ts`, `display.ts`.
- **Extra exports in `protocol/lib/types.ts`:** `OPS`, `ERROR_CODES`, `FW_FAIL_CODES`, `HostOp`, and the constants `MIC_RATE`, `FW_PROGRESS_EVERY`, `IMAGE_FRAME_PAYLOAD_MAX`, `ASK_OPTIONS_MAX`, `HOST_SENT_ID_RE`.
- **Extra vector fields:** a `name` on every `versions.json` entry (the contract's "every case has a unique name"), and `private_key_hex` + `message_utf8` on the invalid `der.json` cases so verifiers can show `node:crypto` rejects them too.
- **Fake host additions:** CLI option `--max-devices` (default 20, to exercise `device_limit`); CLI option `--done-before-speech` (`FakeHostOptions.doneBeforeSpeech`, default off so the §4.7 order `speak.end` → `done ok` stays the default) for the order MausBot usually produces, `done ok` and then the speech; a permission `ask` accepts `options` that are exactly Allow and Deny as well as none; `refused` events carry `gadget`; acks carry details such as `id`; `ota` failure codes from the host side (`ready_timeout`, `progress_timeout`, `disconnected`); the `tamper` values are defined (`sig` → `bad_sig`, `sha256` → `checksum`, `size` → `too_large`).
- **PROTOCOL.md wording beyond the spec text** (each is the natural reading of spec §4 and §6.2; P2a and P3a should confirm, see the open questions in Task 13 Step 7): `voice.drop` ends its turn with `done stopped`; frames that are not JSON objects with a string `op` are ignored; binary frames for an inactive stream are ignored; a `hello` that fails rule 1 gets `error` instead of `challenge`, and a bad `board` id is `bad_sig` (as contract §3.9 `checkHello` does); a host cuts `say` text over 2000 characters; `answer.option` must be one of the ask's option ids; `fw.progress` reports bytes durably written at each 16 KiB crossing and at `size`, and the host sends `fw.commit` after the progress that equals `size`; `busy` and `flash` meanings; `post.id` is a host-sent id; `settings.bot.name` is folded; `done` may come before the turn's speech (spec §6.2 sends `done` on `turn.completed`, independently of speech) and a new turn sends `speak.stop` for that speech first; post speech starts only after the earlier stream has played out (spec §6.2 Speech item 5).

## Verified while writing this plan

The whole deliverable was built first in a scratch git repository (`/private/tmp/claude-501/-Users-omkar-Desktop-openmaus/a3c1a9fe-51f5-48e3-81db-dcefb7528bee/scratchpad/p1exp`, revised after review in `…/scratchpad/p1r2/repo`), and every code block below is copied from those files after their tests passed.

- `npm install` resolved exactly `@noble/curves` 2.4.0 (with `@noble/hashes` 2.4.0) and `ws` 8.22.0.
- `npm test`: protocol 33 pass, fake host 58 pass, five runs on Node 22.22.3 and three on Node 24.14.1, all green. The intermediate states at the ends of Tasks 7, 8, 9, 10 and 11 pass 13, 25, 37, 48 and 54 fake-host tests.
- Each test added in review fails on a deliberately broken copy of the fake host and passes on the plan's code: no 64 KiB window in `ota.ts` (the window test sees 200000 bytes); `replaced` sent on `hello`, or before the signature check (the replacement test); `done` sent before `speak.stop` (the stop and barge-in-during-speech tests); no idle-timer refresh (the timing test); no playout wait in `speech.ts` (the post's `speak.begin` came 2 ms after the reply's instead of about 300 ms); the old stream still sending after `speak.stop` (barge-in during speech); no `speak.stop` for a turn that already got `done ok`, or `doneBeforeSpeech` ignored (the done-before-speech test); explicit Allow/Deny refused (the permission-ask test).
- The generator reproduces every contract §1.7 value byte for byte. `npm run vectors:check` passes in a fresh clone after `npm ci`; a committed change to a vector makes it exit 1; regenerating into an empty folder gives identical bytes.
- `@noble/curves` 2.4.0: `p256.Signature.fromBytes(der, "der").hasHighS()`, `.toBytes("compact")`, `p256.Point.BASE.multiply(k)` and `p256.Point.CURVE().n` exist; `(k·G).x mod n = r` for the RFC "sample" and "test" vectors.
- `node:crypto` accepts high-S DER signatures and rejects the non-minimal, negative, trailing-bytes and wrong-tag DER forms; `createECDH("prime256v1").setPrivateKey(k)` gives `k·G`.
- `node --check` exits 0 for a `.ts` file on Node 22.22.3 and 24.14.1 even when it has a syntax error (`export const x: number = ;`), so it cannot gate `.ts` syntax. `tools/fake-host/test/modules.test.ts` imports every module instead.
- `ws` 8.22.0 in `noServer` mode: the planned 403/400/404/426 refusals, `permessage-deflate` declined (the client sees `extensions === ""`), pings, `maxPayload` closing with 1009 (the server must handle the socket's `error` event or Node crashes), and invalid UTF-8 closing with 1007.
- OpenMausBot's `LICENSE` on `origin/main` is byte-identical to the Apache text at apache.org (SHA-256 `cfc7749b96f63bd31c3c42b5c471bf756814053e847c10f3eb003417bc523d30`).
- `.github/workflows/ci.yml` parses with Homebrew Ruby 4.0.5's YAML loader. The GitHub-hosted run itself was not executed (Task 13 lists it).
- The plan text itself was replayed: extracting every file it creates into an empty folder, running its key, license, install and generator commands, and appending its CI job reproduced the verified repository byte for byte (including `package-lock.json` and the vectors), and all 91 tests passed on Node 22.22.3 and 24.14.1.
- Agent tooling gotcha: when files are written through an agent's tool input, a backslash-u escape is decoded into the character itself. The sources here use no such escapes (the fold table uses numeric code points), and every non-ASCII character in them is visible and intentional.

## File Structure

```
.gitattributes .gitignore LICENSE README.md package.json package-lock.json      Task 1
.github/workflows/ci.yml                     protocol job (Task 6), fake-host job (Task 12)
keys/test-t1.key.hex  keys/test-t1.pub.b64   the test signing key t1 (Task 4)
protocol/
  PROTOCOL.md                                normative protocol text, spec §4 numbering (Task 2)
  lib/types.ts                               op types, constants, OPS table (Task 2)
  lib/encoding.ts                            base64 (canonical), hex, canonical JSON (Task 3)
  lib/identity.ts                            gadget id, prove text, firmware text (Task 4)
  lib/der.ts                                 strict DER <-> raw r||s (Task 4)
  lib/verify.ts                              node:crypto P-256 verify, public key, random-k sign (Task 4)
  lib/frames.ts                              binary frames and firmware chunks (Task 5)
  lib/version.ts                             SemVer precedence, custom builds (Task 5)
  tools/gen-vectors.ts                       the only @noble/curves user (Task 6)
  vectors/*.json  vectors/SHA256SUMS         generated, committed (Task 6)
  test/fixed-values.ts                       contract §1.7 values (Task 2)
  test/protocol-doc.test.ts                  PROTOCOL.md <-> types.ts (Task 2)
  test/encoding.test.ts  identity.test.ts  frames.test.ts  vectors.test.ts       (Tasks 3–6)
tools/fake-host/
  README.md                                  usage, options, commands, events (Task 12)
  src/options.ts                             CLI parsing and defaults (Task 7)
  src/fold.ts                                Latin-1 screen fold and cuts (Task 7)
  src/state.ts                               host_id, enrolled gadgets, pairing window, --state file (Task 7)
  src/enroll.ts                              §4.3 host rules: hello check, challenge, prove decision (Task 7)
  src/context.ts                             shared types: events, commands, HostContext, Feature (Task 8)
  src/session.ts                             one connection: liveness, handshake, dispatch (Task 8)
  src/server.ts                              HTTP + ws upgrade, sessions, built-in commands (Task 8)
  src/features.ts                            the feature list (Tasks 8–11)
  src/speech.ts                              paced 440 Hz test-tone speech (Task 9)
  src/voice.ts                               scripted turns (Task 9)
  src/display.ts                             asks, post, card, image, act, settings (Task 10)
  src/ota.ts                                 firmware update flow (Task 11)
  src/control.ts  src/main.ts                JSON-lines control and the CLI (Task 12)
  test/gadget-client.ts                      the gadget side for tests (Task 8)
  test/options.test.ts enroll.test.ts handshake.test.ts voice.test.ts display.test.ts ota.test.ts cli.test.ts modules.test.ts
```

## Conventions for every task

- Run every command from `/Users/omkar/Desktop/openmaus/openmausbot-gadget-sdk` with the default `node` (22.22.3) unless a step says otherwise.
- Test summaries: Node 22 prints `# pass N` / `# fail N`; Node 24 prints `ℹ pass N` / `ℹ fail N`. "Expected: PASS (N)" means `pass N` and `fail 0`.
- Create files exactly as shown (they are complete). "Replace the file" means overwrite it with the block shown.
- Each commit message ends with the attribution trailer shown in the commit step.

---

### Task 1: Bootstrap the branch and the repository files

**Files:**
- Commit: `docs/specs/2026-10-04-openmausbot-gadget-design.md`, `docs/plans/*` (already on disk)
- Create: `LICENSE`, `.gitignore`, `.gitattributes`, `README.md`, `package.json`, `package-lock.json` (generated)

**Interfaces:**
- Consumes: nothing.
- Produces: branch `p1-protocol`; npm scripts `vectors`, `vectors:check`, `test:protocol`, `fake-host`, `test:fake-host`, `test` (contract §1.4, exact); devDependencies `@noble/curves` 2.4.0 and `ws` 8.22.0; `.gitattributes` rules `protocol/vectors/** -text`, `keys/** -text`, `*.png binary`.

- [ ] **Step 1: Create the branch and commit the docs**

```bash
cd /Users/omkar/Desktop/openmaus/openmausbot-gadget-sdk
git status --short
git switch -c p1-protocol
git add docs/specs/*.md docs/plans/*.md
git commit -m "docs: spec v1.1 and implementation plans" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

Expected: `git status --short` lists `?? docs/plans/` (and possibly a modified spec); the commit records the Markdown files only (`.gitignore` does not exist yet, so a Finder `.DS_Store` must not be swept in). If `git commit` says "nothing to commit", the docs are already committed: continue. Other plan sessions may still be writing into `docs/plans/` in this working tree; whatever they write later is theirs to commit, not P1's.

- [ ] **Step 2: Add the Apache-2.0 license text**

```bash
git -C /Users/omkar/Desktop/openmaus/OpenGrokBot show origin/main:LICENSE > LICENSE
shasum -a 256 LICENSE
```

Expected: `cfc7749b96f63bd31c3c42b5c471bf756814053e847c10f3eb003417bc523d30  LICENSE`. (The same bytes come from `curl -fsSL https://www.apache.org/licenses/LICENSE-2.0.txt -o LICENSE` if the OpenMausBot checkout is unavailable.)

- [ ] **Step 3: Create `.gitignore`**

```gitignore
node_modules/
/build/
firmware/ports/esp32/build/
firmware/ports/esp32/sdkconfig
firmware/ports/esp32/sdkconfig.old
firmware/ports/esp32/managed_components/
site/dist/
tools/art/out/
*_err.png
.DS_Store
```

- [ ] **Step 4: Create `.gitattributes`**

```gitattributes
protocol/vectors/** -text
keys/** -text
*.png binary
```

- [ ] **Step 5: Create the `README.md` stub** (P2d replaces it with the full README)

````markdown
# OpenMausBot Gadget SDK

Turn a small ESP32 board with a screen, a microphone and a speaker into a desk terminal for your own MausBot: hold to talk, and the bot running on your Mac answers on the screen and out loud.

**Status:** in development. This repository will hold the firmware for four boards, a desktop simulator, a browser installer and the protocol. Today it has the protocol and its tools.

- Remote access must be on in MausBot: the gadget hub runs inside MausBot's companion, which only runs while Remote access is on.
- Pairing starts at **MausBot → Settings → Remote access → Pair a gadget**.

## What is here

| Path | What |
|---|---|
| [`protocol/PROTOCOL.md`](protocol/PROTOCOL.md) | The `openmausbot-gadget/1` protocol (normative) |
| `protocol/vectors/` | Test vectors every implementation must pass, with `SHA256SUMS` |
| `protocol/tools/gen-vectors.ts` | Regenerates the vectors |
| [`tools/fake-host/`](tools/fake-host/README.md) | A stand-in for MausBot that speaks the host side, for tests and offline work |
| `keys/test-t1.*` | The test signing key (never in release firmware) |

## Commands

Node 22.18 or newer (it runs the `.ts` files directly):

```
npm ci
npm test                 # protocol and fake-host tests
npm run vectors:check    # regenerate the vectors and fail on any difference
npm run fake-host -- --code 123456
```

## License

Apache-2.0, see [`LICENSE`](LICENSE). The OpenMausBot name, the MausBot name and the Maus mascot are trademarks of Supamaus Software Private Limited.
````

- [ ] **Step 6: Create `package.json`** (the contract §1.4 shape, exactly)

```json
{
  "name": "openmausbot-gadget-sdk-tools",
  "private": true,
  "type": "module",
  "license": "Apache-2.0",
  "engines": { "node": ">=22.18" },
  "scripts": {
    "vectors": "node protocol/tools/gen-vectors.ts",
    "vectors:check": "node protocol/tools/gen-vectors.ts && git diff --exit-code protocol/vectors && test -z \"$(git status --porcelain -- protocol/vectors)\"",
    "test:protocol": "node --test \"protocol/test/**/*.test.ts\"",
    "fake-host": "node tools/fake-host/src/main.ts",
    "test:fake-host": "node --test \"tools/fake-host/test/**/*.test.ts\"",
    "test": "npm run test:protocol && npm run test:fake-host"
  },
  "devDependencies": { "@noble/curves": "2.4.0", "ws": "8.22.0" }
}
```

- [ ] **Step 7: Install and check the pins**

```bash
npm install
npm ls
rm -rf node_modules && npm ci
```

Expected: `npm ls` prints

```
openmausbot-gadget-sdk-tools@ /Users/omkar/Desktop/openmaus/openmausbot-gadget-sdk
├── @noble/curves@2.4.0
└── ws@8.22.0
```

and `npm ci` ends with `added 3 packages` (the third is `@noble/hashes` 2.4.0).

- [ ] **Step 8: Check the attributes**

```bash
git check-attr text -- protocol/vectors/prove.json keys/test-t1.key.hex
```

Expected:

```
protocol/vectors/prove.json: text: unset
keys/test-t1.key.hex: text: unset
```

- [ ] **Step 9: Commit**

```bash
git add LICENSE .gitignore .gitattributes README.md package.json package-lock.json
git commit -m "chore: bootstrap the SDK tools package, license and repo files" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 2: PROTOCOL.md and the op types

**Files:**
- Create: `protocol/test/fixed-values.ts`, `protocol/test/protocol-doc.test.ts`, `protocol/lib/types.ts`, `protocol/PROTOCOL.md`

**Interfaces:**
- Consumes: nothing.
- Produces:
  - `protocol/lib/types.ts`: every op interface of contract §3.2 with the same names (`HelloMsg` … `FwInstalledMsg`, `GadgetToHost`, `HostToGadget`, `GadgetOp`), plus `HostOp`; `BinaryKind = {mic: 1, speaker: 2, image: 3, firmware: 4}`; the §3.2 constants (`GADGET_PATH`, `GADGET_SUBPROTOCOL`, `PROTO_VERSION`, `TEXT_FRAME_MAX`, `BINARY_FRAME_MAX`, `PING_INTERVAL_MS`, `IDLE_TIMEOUT_MS`, `HANDSHAKE_TIMEOUT_MS`, `UTTERANCE_MAX_MS`, `SAY_MAX_CHARS`, `REPLY_MIN_INTERVAL_MS`, `ACT_TIMEOUT_MS`, `SPEAK_AHEAD_MS`, `SPEAK_FRAME_MS`, `FW_CHUNK_BYTES`, `FW_WINDOW_BYTES`, `FW_READY_TIMEOUT_MS`, `NAME_MAX_CHARS`, `ACTIONS_MAX`, `ACTION_DESCRIPTION_MAX`, `ACTION_PARAMS_MAX_BYTES`, the `*_RE` patterns) and `MIC_RATE`, `FW_PROGRESS_EVERY`, `IMAGE_FRAME_PAYLOAD_MAX`, `ASK_OPTIONS_MAX`, `HOST_SENT_ID_RE`; `OPS: ReadonlyArray<{op, dir: "g2h" | "h2g", section: string}>`; `ERROR_CODES`, `FW_FAIL_CODES`.
  - `protocol/test/fixed-values.ts`: the contract §1.7 values as constants (`RFC_PRIVATE_HEX`, `RFC_PUBKEY_B64`, `RFC_ID`, `RFC_SAMPLE_DER_HEX`, `PINNED_NONCE_B64`, `PINNED_HOST_ID`, `PROVE_TEXT`, `PROVE_DER_HEX`, `PROVE_SIG_B64`, `LOW_S_HOST_ID`, `SHORT_DER_MESSAGE`, `SHORT_DER_HEX`, `T1_SEED_TEXT`, `T1_PRIVATE_HEX`, `T1_PUBKEY_B64`, `FIRMWARE_TEXT`, `FIRMWARE_T1_DER_HEX`, `P256_N`).
  - `protocol/PROTOCOL.md`: the normative text every other plan reads.

- [ ] **Step 1: Create `protocol/test/fixed-values.ts`**

```ts
// SPDX-License-Identifier: Apache-2.0
// Fixed values from docs/plans/00-interfaces.md §1.7. Computed with @noble/curves 2.4.0
// (lowS: false) and checked with node:crypto. The generator must reproduce them byte for byte.
export const RFC_PRIVATE_HEX = "c9afa9d845ba75166b5c215767b1d6934e50c3db36e89b127b8a622b120f6721";
export const RFC_PUBKEY_B64 = "BGD+1LolWp0xyWHrdMY1bWjASbiSO2H6bOZpYi5g8p+2eQP+EAi4vJmkGunpVii8ZPLxsgwtfp9Rd6PClNRGIpk=";
export const RFC_ID = "gad_b18b86ce1389e46d";
export const RFC_SAMPLE_DER_HEX =
  "3046022100efd48b2aacb6a8fd1140dd9cd45e81d69d2c877b56aaf991c34d0ea84eaf3716022100f7cb1c942d657c41d436c7a1b6e29f65f3e900dbb9aff4064dc4ab2f843acda8";
export const PINNED_NONCE_B64 = "AAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8=";
export const PINNED_HOST_ID = "000102030405060708090a0b0c0d0e0f";
export const PROVE_TEXT =
  "openmausbot-gadget/1\nprove\ngad_b18b86ce1389e46d\nAAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8=\n000102030405060708090a0b0c0d0e0f";
export const PROVE_DER_HEX =
  "3046022100f0c4fbe24029d797b16b36dcc0d05fb7a8b9df8c5ca4b27ad99d820d4d8a664202210087d087e1c83ab59e8feebee63a40b423c5af81956ceca8033264098abf44e35a";
export const PROVE_SIG_B64 = "MEYCIQDwxPviQCnXl7FrNtzA0F+3qLnfjFyksnrZnYINTYpmQgIhAIfQh+HIOrWej+6+5jpAtCPFr4GVbOyoAzJkCYq/RONa";
export const LOW_S_HOST_ID = "0123456789abcdef0123456789abcdef";
export const SHORT_DER_MESSAGE = "openmausbot-gadget/1 der-short 13";
export const SHORT_DER_HEX =
  "3043022052c1af44f658bb58a5b434a96d609052855835feca85b011cdfcb4159f52c37e021f2d466a13caea297fa0c97498ce12ed77ea25e9895415c4bb6ebe64b2a049cb";
export const T1_SEED_TEXT = "openmausbot-gadget/1 test release key t1";
export const T1_PRIVATE_HEX = "274b871e29523ce21208177b90a0a3bd6a169cb84cf867236adc68f3d90fc0c8";
export const T1_PUBKEY_B64 = "BIUTH1UeVJw2VXn0Ehdhd8apNOHmB6VvjV512SxIbCCXfUhvlLRTuOTLHtrsVAbIx06JhMoOMbC3ic73hZpxMwg=";
export const FIRMWARE_TEXT =
  "openmausbot-gadget/1\nfirmware\namoled-175c\n1.1.0\n1234567\ne3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855";
export const FIRMWARE_T1_DER_HEX =
  "3046022100de2dfa125231a739f0b1b27e2ec085e4027b59e4486978457ffccb95b3895f2d022100ef00bd5c4177cb97ba4a4f4036046f6a20b3c896c5ab1124720b54afe6179744";
/** P-256 group order n, for low-S/high-S conversions in tests and the generator. */
export const P256_N = 0xffffffff00000000ffffffffffffffffbce6faada7179e84f3b9cac2fc632551n;
```

- [ ] **Step 2: Write the failing test `protocol/test/protocol-doc.test.ts`**

```ts
// SPDX-License-Identifier: Apache-2.0
// PROTOCOL.md and protocol/lib/types.ts describe the same protocol.
import { test } from "node:test";
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { ERROR_CODES, FW_FAIL_CODES, OPS } from "../lib/types.ts";
import * as F from "./fixed-values.ts";

const doc = readFileSync(new URL("../PROTOCOL.md", import.meta.url), "utf8");
const HEADINGS = [
  "## 4.1 Transport", "## 4.2 Identity", "## 4.3 Handshake", "## 4.4 Conversation", "## 4.5 Approvals",
  "## 4.6 Push", "## 4.7 Display, actions and sensors", "## 4.8 Firmware updates", "## 4.9 Versioning and test vectors",
];
function section(n: string): string {
  const start = doc.indexOf(`## ${n} `);
  assert.ok(start >= 0, `missing section ${n}`);
  const next = doc.indexOf("\n## ", start + 1);
  return next < 0 ? doc.slice(start) : doc.slice(start, next);
}

test("PROTOCOL.md keeps the spec's section numbering, in order", () => {
  const at = HEADINGS.map((h) => doc.indexOf("\n" + h + "\n"));
  assert.ok(at.every((i) => i > 0), `missing: ${HEADINGS.filter((_, i) => at[i] < 0).join(", ")}`);
  assert.deepEqual([...at].sort((a, b) => a - b), at);
});

test("the op index lists exactly the ops in types.ts, with direction and section", () => {
  const rows = [...doc.matchAll(/^\| `([a-z.]+)` \| (g→h|h→g) \| (4\.\d) \|$/gm)].map((m) => ({
    op: m[1], dir: m[2] === "g→h" ? "g2h" : "h2g", section: m[3],
  }));
  assert.deepEqual(rows, OPS.map((o) => ({ op: o.op, dir: o.dir, section: o.section })));
});

test("every op is documented in its own section", () => {
  for (const { op, section: n } of OPS) {
    const text = section(n);
    const found = text.includes("`" + op + "`") || text.includes("`" + op + " ") || text.includes("→ " + op + " ");
    assert.ok(found, `${op} is not documented in §${n}`);
  }
});

test("error codes and fw.fail codes match types.ts", () => {
  const s43 = section("4.3");
  const errorTable = s43.slice(s43.indexOf("### `error`"), s43.indexOf("### How the gadget reacts"));
  const errors = [...errorTable.matchAll(/^\| `([a-z_]+)` \| /gm)].map((m) => m[1]);
  assert.deepEqual(errors, [...ERROR_CODES]);
  const line = section("4.8").split("\n").find((l) => l.startsWith("- **Failure codes:**"));
  assert.ok(line);
  assert.deepEqual([...line.matchAll(/`([a-z_]+)`/g)].map((m) => m[1]), [...FW_FAIL_CODES]);
});

test("the pinned vector inputs in §4.9 match the fixed values", () => {
  const s = section("4.9");
  for (const v of [F.RFC_PRIVATE_HEX, F.PINNED_NONCE_B64, F.PINNED_HOST_ID, F.LOW_S_HOST_ID, F.T1_SEED_TEXT]) {
    assert.ok(s.includes(v), `§4.9 does not mention ${v}`);
  }
});

test("prove and firmware texts in PROTOCOL.md have the pinned line layout", () => {
  assert.ok(section("4.3").includes("```\nopenmausbot-gadget/1\nprove\n<id>\n<nonce>\n<host_id>\n```"));
  assert.ok(section("4.8").includes("openmausbot-gadget/1\n  firmware\n  <board>\n  <version>\n  <size>\n  <sha256 lowercase hex>\n"));
});
```

- [ ] **Step 3: Run it to see it fail**

Run: `node --test protocol/test/protocol-doc.test.ts`
Expected: FAIL with `ERR_MODULE_NOT_FOUND` for `protocol/lib/types.ts`.

- [ ] **Step 4: Create `protocol/lib/types.ts`**

```ts
// SPDX-License-Identifier: Apache-2.0
// openmausbot-gadget/1 message types (protocol/PROTOCOL.md). The names match
// companion/src/gadget/protocol.ts in OpenMausBot (contract 00-interfaces.md §3.2).
// Erasable TypeScript only: interfaces, type aliases and plain consts.

export const GADGET_PATH = "/gadget";
export const GADGET_SUBPROTOCOL = "openmausbot-gadget.1";
export const PROTO_VERSION = 1;
export const TEXT_FRAME_MAX = 16 * 1024;
export const BINARY_FRAME_MAX = 8 * 1024;
export const PING_INTERVAL_MS = 15_000;
export const IDLE_TIMEOUT_MS = 45_000;
export const HANDSHAKE_TIMEOUT_MS = 10_000;
export const UTTERANCE_MAX_MS = 60_000;
export const SAY_MAX_CHARS = 2000;
export const REPLY_MIN_INTERVAL_MS = 250;
export const ACT_TIMEOUT_MS = 15_000;
export const SPEAK_AHEAD_MS = 500;
export const SPEAK_FRAME_MS = 40;
export const MIC_RATE = 16000;
export const FW_CHUNK_BYTES = 4096;
export const FW_PROGRESS_EVERY = 16 * 1024;
export const FW_WINDOW_BYTES = 64 * 1024;
export const FW_READY_TIMEOUT_MS = 10_000;
export const IMAGE_FRAME_PAYLOAD_MAX = BINARY_FRAME_MAX - 2;
export const NAME_MAX_CHARS = 32;
export const ACTIONS_MAX = 16;
export const ACTION_DESCRIPTION_MAX = 200;
export const ACTION_PARAMS_MAX_BYTES = 1024;
export const ASK_OPTIONS_MAX = 4;
export const HOST_ID_RE = /^[0-9a-f]{32}$/;
export const GADGET_ID_RE = /^gad_[0-9a-f]{16}$/;
export const PAIR_CODE_RE = /^\d{6}$/;
export const BOARD_ID_RE = /^[a-z0-9-]{1,32}$/;
export const ACTION_NAME_RE = /^[a-z][a-z0-9_.-]{0,31}$/;
export const SHA256_HEX_RE = /^[0-9a-f]{64}$/;
export const RELEASE_VERSION_RE = /^\d+\.\d+\.\d+(-[0-9A-Za-z.]+)?$/;
export const HOST_SENT_ID_RE = /^[A-Za-z0-9_.:-]{1,40}$/;

export const BinaryKind = { mic: 0x01, speaker: 0x02, image: 0x03, firmware: 0x04 } as const;
export type BinaryKindValue = (typeof BinaryKind)[keyof typeof BinaryKind];

export type GadgetErrorCode =
  | "proto_unsupported" | "enroll_required" | "bad_code" | "bad_sig" | "revoked" | "device_limit" | "replaced";
export type FwFailCode =
  | "too_large" | "wrong_board" | "same_version" | "unknown_key" | "bad_sig"
  | "busy" | "flash" | "sequence" | "checksum" | "timeout";
export const ERROR_CODES: readonly GadgetErrorCode[] = [
  "proto_unsupported", "enroll_required", "bad_code", "bad_sig", "revoked", "device_limit", "replaced",
];
export const FW_FAIL_CODES: readonly FwFailCode[] = [
  "too_large", "wrong_board", "same_version", "unknown_key", "bad_sig", "busy", "flash", "sequence", "checksum", "timeout",
];
export type Risk = "safe" | "confirm";
export type OptionStyle = "allow" | "deny" | "neutral";
export type AskCloseReason = "answered" | "expired" | "withdrawn";
export type TurnOutcome = "ok" | "failed" | "stopped";
export type PostKind = "routine" | "message";
export type ScreenCharset = "latin1";

export interface GadgetCaps {
  screen?: { w: number; h: number; round?: boolean; text?: ScreenCharset };
  image?: { w: number; h: number };
  mic?: { rate: number };
  speaker?: { rate: number };
  input?: string[];
  battery?: boolean;
  ota?: { max: number };
}
export interface GadgetActionDecl { name: string; description: string; params: Record<string, unknown>; risk?: Risk }
export interface GadgetSensors { battery_pct?: number; charging?: boolean; [key: string]: unknown }
export interface BotRef { id: string; name: string }
export interface GadgetSettings { speak_pushes: boolean }
export interface AskOption { id: string; label: string; style?: OptionStyle }

// ---- gadget → host
export interface HelloMsg {
  op: "hello"; proto: number; id: string; pubkey: string; name: string; board: string; fw: string;
  caps: GadgetCaps; actions?: GadgetActionDecl[]; sensors?: GadgetSensors;
}
export interface ProveMsg { op: "prove"; sig: string; enroll?: string }
export interface VoiceBeginMsg { op: "voice.begin"; turn: string; stream: number; rate: number }
export interface VoiceEndMsg { op: "voice.end"; turn: string; ms: number }
export interface VoiceDropMsg { op: "voice.drop"; turn: string }
export interface SayMsg { op: "say"; turn: string; text: string }
export interface StopMsg { op: "stop"; turn?: string }
export interface AnswerMsg { op: "answer"; id: string; option: string }
export interface ActResultMsg { op: "act.result"; id: string; ok: boolean; data?: unknown; error?: string }
export interface SenseMsg { op: "sense"; battery_pct?: number; charging?: boolean; [key: string]: unknown }
export interface EventMsg { op: "event"; name: string; data?: unknown }
export interface FwReadyMsg { op: "fw.ready"; stream: number }
export interface FwFailMsg { op: "fw.fail"; stream: number; code: FwFailCode | string }
export interface FwProgressMsg { op: "fw.progress"; stream: number; offset: number }
export interface FwInstalledMsg { op: "fw.installed"; version: string }

// ---- host → gadget
export interface ChallengeMsg { op: "challenge"; nonce: string; host_id: string; host_name: string }
export interface ReadyMsg { op: "ready"; session: string; bot: BotRef; settings: GadgetSettings }
export interface ErrorMsg { op: "error"; code: GadgetErrorCode; message: string }
export interface SettingsMsg { op: "settings"; bot: BotRef; settings: GadgetSettings; name?: string }
export interface HeardMsg { op: "heard"; turn: string; text: string }
export interface WorkingMsg { op: "working"; turn: string; text: string }
export interface ReplyMsg { op: "reply"; turn: string; text: string; final: boolean }
export interface DoneMsg { op: "done"; turn: string; outcome: TurnOutcome; reason?: string }
export interface SpeakBeginMsg { op: "speak.begin"; stream: number; rate: 16000 | 24000; turn?: string }
export interface SpeakEndMsg { op: "speak.end"; stream: number }
export interface SpeakStopMsg { op: "speak.stop"; stream: number }
export interface AskMsg {
  op: "ask"; id: string; kind: "permission" | "question"; title: string; body: string;
  options: AskOption[]; expires_s?: number;
}
export interface AskCloseMsg { op: "ask.close"; id: string; reason: AskCloseReason }
export interface PostMsg { op: "post"; id: string; bot: BotRef; kind: PostKind; text: string; speak: boolean }
export interface CardMsg { op: "card"; id: string; title: string; body: string; ttl_s: number }
export interface CardCloseMsg { op: "card.close"; id: string }
export interface ImageBeginMsg { op: "image.begin"; id: string; stream: number; w: number; h: number; ttl_s: number }
export interface ImageEndMsg { op: "image.end"; stream: number }
export interface ActMsg { op: "act"; id: string; name: string; args: Record<string, unknown> }
export interface FwOfferMsg {
  op: "fw.offer"; stream: number; board: string; version: string; size: number;
  sha256: string; sig: string; key_id: string;
}
export interface FwCommitMsg { op: "fw.commit"; stream: number }

export type GadgetToHost =
  | HelloMsg | ProveMsg | VoiceBeginMsg | VoiceEndMsg | VoiceDropMsg | SayMsg | StopMsg | AnswerMsg
  | ActResultMsg | SenseMsg | EventMsg | FwReadyMsg | FwFailMsg | FwProgressMsg | FwInstalledMsg;
export type HostToGadget =
  | ChallengeMsg | ReadyMsg | ErrorMsg | SettingsMsg | HeardMsg | WorkingMsg | ReplyMsg | DoneMsg
  | SpeakBeginMsg | SpeakEndMsg | SpeakStopMsg | AskMsg | AskCloseMsg | PostMsg | CardMsg | CardCloseMsg
  | ImageBeginMsg | ImageEndMsg | ActMsg | FwOfferMsg | FwCommitMsg;
export type GadgetOp = GadgetToHost["op"];
export type HostOp = HostToGadget["op"];

/** Every op with its direction and its PROTOCOL.md section. protocol/test/protocol-doc.test.ts
 *  checks that PROTOCOL.md documents exactly these ops. */
export const OPS: ReadonlyArray<{ op: GadgetOp | HostOp; dir: "g2h" | "h2g"; section: string }> = [
  { op: "hello", dir: "g2h", section: "4.3" },
  { op: "challenge", dir: "h2g", section: "4.3" },
  { op: "prove", dir: "g2h", section: "4.3" },
  { op: "ready", dir: "h2g", section: "4.3" },
  { op: "error", dir: "h2g", section: "4.3" },
  { op: "settings", dir: "h2g", section: "4.3" },
  { op: "voice.begin", dir: "g2h", section: "4.4" },
  { op: "voice.end", dir: "g2h", section: "4.4" },
  { op: "voice.drop", dir: "g2h", section: "4.4" },
  { op: "say", dir: "g2h", section: "4.4" },
  { op: "stop", dir: "g2h", section: "4.4" },
  { op: "heard", dir: "h2g", section: "4.4" },
  { op: "working", dir: "h2g", section: "4.4" },
  { op: "reply", dir: "h2g", section: "4.4" },
  { op: "done", dir: "h2g", section: "4.4" },
  { op: "speak.begin", dir: "h2g", section: "4.4" },
  { op: "speak.end", dir: "h2g", section: "4.4" },
  { op: "speak.stop", dir: "h2g", section: "4.4" },
  { op: "ask", dir: "h2g", section: "4.5" },
  { op: "answer", dir: "g2h", section: "4.5" },
  { op: "ask.close", dir: "h2g", section: "4.5" },
  { op: "post", dir: "h2g", section: "4.6" },
  { op: "card", dir: "h2g", section: "4.7" },
  { op: "card.close", dir: "h2g", section: "4.7" },
  { op: "image.begin", dir: "h2g", section: "4.7" },
  { op: "image.end", dir: "h2g", section: "4.7" },
  { op: "act", dir: "h2g", section: "4.7" },
  { op: "act.result", dir: "g2h", section: "4.7" },
  { op: "sense", dir: "g2h", section: "4.7" },
  { op: "event", dir: "g2h", section: "4.7" },
  { op: "fw.offer", dir: "h2g", section: "4.8" },
  { op: "fw.ready", dir: "g2h", section: "4.8" },
  { op: "fw.fail", dir: "g2h", section: "4.8" },
  { op: "fw.progress", dir: "g2h", section: "4.8" },
  { op: "fw.commit", dir: "h2g", section: "4.8" },
  { op: "fw.installed", dir: "g2h", section: "4.8" },
];
```

- [ ] **Step 5: Create `protocol/PROTOCOL.md`**

````markdown
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
| Firmware version | The git tag without the leading `v`, for example `1.1.0`. Custom builds end in `-dev`; a local build without a version reports `0.0.0-dev`. Versions compare by SemVer 2.0.0 precedence. A `fw` that ends in `-dev`, or does not match `/^\d+\.\d+\.\d+(-[0-9A-Za-z.]+)?$/`, is a custom build (`versions.json`). |
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
  | `versions.json` | SemVer 2.0.0 precedence and the custom-build rule |

- **Pinned inputs.** The `prove` vectors use the RFC 6979 A.2.5 P-256 private key `c9afa9d845ba75166b5c215767b1d6934e50c3db36e89b127b8a622b120f6721`, `nonce` = `AAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8=` (bytes 0x00–0x1f) and `host_id` = `000102030405060708090a0b0c0d0e0f`. With these inputs the RFC "sample" signature and the `prove` signature are both **high-S**. (Whether a `prove` signature is high-S depends on `host_id`: `0123456789abcdef0123456789abcdef` gives a low-S one, so the inputs are pinned.)
- **Deterministic signatures.** A case with `"deterministic": true` is the RFC 6979 signature the firmware must reproduce byte for byte. The firmware never normalizes S.
- **Verifiers** must accept high-S signatures. The host and the fake host verify with Node's built-in `crypto`, which does.
- **Generator:** `protocol/tools/gen-vectors.ts` uses `@noble/curves` with `{lowS: false, format: "der"}` and `getPublicKey(sk, false)`. The library's defaults (low-S, compressed keys) would produce vectors the firmware cannot match. The generator asserts `Signature.fromBytes(der, "der").hasHighS()` for every case marked `"high_s": true`.
- **Test key `t1`:** private scalar = SHA-256 of the UTF-8 text `openmausbot-gadget/1 test release key t1` (`keys/test-t1.key.hex`, public key in `keys/test-t1.pub.b64`). It exists for simulators, tests and `tools/fake-host`, and is never compiled into release firmware.
- **Byte stability:** `.gitattributes` has `protocol/vectors/** -text`, in this repository and in any vendored copy. `SHA256SUMS` lists `<sha256>  <file>` for every vector file over its exact bytes. CI regenerates the vectors and fails on any difference.
- The firmware's C tests and OpenMausBot's TypeScript tests read the same files. OpenMausBot vendors a copy that its tests compare by hash.
````

- [ ] **Step 6: Run the test to see it pass**

Run: `node --test protocol/test/protocol-doc.test.ts`
Expected: PASS (6).

- [ ] **Step 7: Commit**

```bash
git add protocol/PROTOCOL.md protocol/lib/types.ts protocol/test/fixed-values.ts protocol/test/protocol-doc.test.ts
git commit -m "docs(protocol): PROTOCOL.md and the op types" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 3: Canonical encodings

**Files:**
- Create: `protocol/test/encoding.test.ts`, `protocol/lib/encoding.ts`

**Interfaces:**
- Consumes: nothing.
- Produces (`protocol/lib/encoding.ts`, contract §4.4.1):
  - `b64Encode(bytes: Uint8Array): string`
  - `b64DecodeCanonical(text: string): Uint8Array | null` (null unless decode → encode round-trips exactly)
  - `hexEncode(bytes: Uint8Array): string`
  - `hexDecode(hex: string): Uint8Array` (lowercase, even length; throws otherwise)
  - `canonicalJson(value: unknown): string` (keys sorted at every depth, no whitespace)

- [ ] **Step 1: Write the failing test `protocol/test/encoding.test.ts`**

```ts
// SPDX-License-Identifier: Apache-2.0
import { test } from "node:test";
import assert from "node:assert/strict";
import { b64DecodeCanonical, b64Encode, canonicalJson, hexDecode, hexEncode } from "../lib/encoding.ts";

test("b64Encode uses the standard alphabet with padding", () => {
  assert.equal(b64Encode(new Uint8Array([])), "");
  assert.equal(b64Encode(new Uint8Array([0])), "AA==");
  assert.equal(b64Encode(new Uint8Array([0, 1])), "AAE=");
  assert.equal(b64Encode(new Uint8Array([0xfb, 0xff])), "+/8=");
});

test("b64DecodeCanonical accepts only text that re-encodes unchanged", () => {
  assert.deepEqual(b64DecodeCanonical("AAEC"), new Uint8Array([0, 1, 2]));
  assert.deepEqual(b64DecodeCanonical(""), new Uint8Array([]));
  for (const bad of ["AA", "AA=", "AB==", "AAE", "AA==\n", " AA==", "-_8=", "AA==AA==", "A===", "@@@@"]) {
    assert.equal(b64DecodeCanonical(bad), null, JSON.stringify(bad));
  }
});

test("hexEncode and hexDecode round-trip lowercase hex", () => {
  assert.equal(hexEncode(new Uint8Array([0, 0xab, 0xff])), "00abff");
  assert.deepEqual(hexDecode("00abff"), new Uint8Array([0, 0xab, 0xff]));
  assert.throws(() => hexDecode("0"), /hex/);
  assert.throws(() => hexDecode("zz"), /hex/);
  assert.throws(() => hexDecode("AB"), /hex/);
});

test("canonicalJson sorts keys at every depth and has no whitespace", () => {
  assert.equal(canonicalJson({ b: 1, a: [{ d: 2, c: "x" }], c: null }), '{"a":[{"c":"x","d":2}],"b":1,"c":null}');
  assert.equal(canonicalJson("é"), '"é"');
  assert.equal(canonicalJson({ z: undefined, y: 1 }), '{"y":1}');
});
```

- [ ] **Step 2: Run it to see it fail**

Run: `node --test protocol/test/encoding.test.ts`
Expected: FAIL with `ERR_MODULE_NOT_FOUND` for `protocol/lib/encoding.ts`.

- [ ] **Step 3: Create `protocol/lib/encoding.ts`**

```ts
// SPDX-License-Identifier: Apache-2.0
// Pinned encodings (PROTOCOL.md §4.1): RFC 4648 §4 base64 with padding, lowercase hex,
// and canonical JSON for hashing.
import { Buffer } from "node:buffer";

const B64_RE = /^(?:[A-Za-z0-9+/]{4})*(?:[A-Za-z0-9+/]{2}==|[A-Za-z0-9+/]{3}=)?$/;
const HEX_RE = /^(?:[0-9a-f]{2})*$/;

export function b64Encode(bytes: Uint8Array): string {
  return Buffer.from(bytes.buffer, bytes.byteOffset, bytes.byteLength).toString("base64");
}

/** Decodes RFC 4648 §4 base64 with padding. Returns null unless the text re-encodes to exactly itself
 *  (Node's own decoder is lenient: it accepts the URL alphabet, missing padding and junk). */
export function b64DecodeCanonical(text: string): Uint8Array | null {
  if (!B64_RE.test(text)) return null;
  const bytes = new Uint8Array(Buffer.from(text, "base64"));
  return b64Encode(bytes) === text ? bytes : null;
}

export function hexEncode(bytes: Uint8Array): string {
  return Buffer.from(bytes.buffer, bytes.byteOffset, bytes.byteLength).toString("hex");
}

/** Lowercase, even-length hex only; throws otherwise. */
export function hexDecode(hex: string): Uint8Array {
  if (!HEX_RE.test(hex)) throw new Error(`not lowercase hex: ${JSON.stringify(hex.slice(0, 16))}`);
  return new Uint8Array(Buffer.from(hex, "hex"));
}

/** JSON with object keys sorted (UTF-16 code unit order) at every depth and no whitespace.
 *  Object members whose value is undefined are dropped, as JSON.stringify does. */
export function canonicalJson(value: unknown): string {
  if (Array.isArray(value)) return "[" + value.map((v) => canonicalJson(v === undefined ? null : v)).join(",") + "]";
  if (value !== null && typeof value === "object") {
    const obj = value as Record<string, unknown>;
    const keys = Object.keys(obj).filter((k) => obj[k] !== undefined).sort();
    return "{" + keys.map((k) => JSON.stringify(k) + ":" + canonicalJson(obj[k])).join(",") + "}";
  }
  return JSON.stringify(value);
}
```

- [ ] **Step 4: Run the test to see it pass**

Run: `node --test protocol/test/encoding.test.ts`
Expected: PASS (4).

- [ ] **Step 5: Commit**

```bash
git add protocol/lib/encoding.ts protocol/test/encoding.test.ts
git commit -m "feat(protocol): canonical base64, hex and JSON encodings" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 4: Identity, signed texts, DER and node:crypto verification; the test key t1

**Files:**
- Create: `protocol/test/identity.test.ts`, `protocol/lib/identity.ts`, `protocol/lib/der.ts`, `protocol/lib/verify.ts`, `keys/test-t1.key.hex`, `keys/test-t1.pub.b64`

**Interfaces:**
- Consumes: `b64Encode`, `b64DecodeCanonical`, `hexEncode`, `hexDecode` (Task 3); the fixed values (Task 2).
- Produces (contract §4.4.1, §4.5):
  - `protocol/lib/identity.ts`: `HOST_ID_RE`; `gadgetIdFromPubkey(pub65: Uint8Array): string`; `proveText(id: string, nonceB64: string, hostId: string): string`; `firmwareText(board: string, version: string, size: number, sha256Hex: string): string` (throws on a non-integer size or non-lowercase hash).
  - `protocol/lib/verify.ts`: `verifyP256(pub65: Uint8Array, text: string, der: Uint8Array): boolean`; `publicKeyFromPrivate(privateKeyHex: string): Uint8Array`; `signP256(privateKeyHex: string, text: string): Uint8Array` (DER, random k).
  - `protocol/lib/der.ts`: `derToRaw(der: Uint8Array): Uint8Array | null`; `rawToDer(raw: Uint8Array): Uint8Array`.
  - `keys/test-t1.key.hex` (64 hex + LF) and `keys/test-t1.pub.b64` (base64 + LF).

- [ ] **Step 1: Write the failing test `protocol/test/identity.test.ts`**

```ts
// SPDX-License-Identifier: Apache-2.0
import { test } from "node:test";
import assert from "node:assert/strict";
import { createHash } from "node:crypto";
import { readFileSync } from "node:fs";
import { b64DecodeCanonical, b64Encode, hexDecode, hexEncode } from "../lib/encoding.ts";
import { HOST_ID_RE, firmwareText, gadgetIdFromPubkey, proveText } from "../lib/identity.ts";
import { publicKeyFromPrivate, signP256, verifyP256 } from "../lib/verify.ts";
import { derToRaw, rawToDer } from "../lib/der.ts";
import * as F from "./fixed-values.ts";

const rfcPub = (): Uint8Array => b64DecodeCanonical(F.RFC_PUBKEY_B64)!;

test("publicKeyFromPrivate gives the 65-byte SEC1 point", () => {
  const pub = publicKeyFromPrivate(F.RFC_PRIVATE_HEX);
  assert.equal(pub.length, 65);
  assert.equal(pub[0], 0x04);
  assert.equal(b64Encode(pub), F.RFC_PUBKEY_B64);
  assert.equal(b64Encode(publicKeyFromPrivate(F.T1_PRIVATE_HEX)), F.T1_PUBKEY_B64);
});

test("gadgetIdFromPubkey is gad_ + 16 hex of sha256(pubkey)", () => {
  assert.equal(gadgetIdFromPubkey(rfcPub()), F.RFC_ID);
  assert.throws(() => gadgetIdFromPubkey(rfcPub().slice(0, 33)), /65-byte/);
});

test("proveText and firmwareText build the pinned texts with no trailing newline", () => {
  assert.equal(proveText(F.RFC_ID, F.PINNED_NONCE_B64, F.PINNED_HOST_ID), F.PROVE_TEXT);
  assert.equal(firmwareText("amoled-175c", "1.1.0", 1234567, createHash("sha256").update("").digest("hex")), F.FIRMWARE_TEXT);
  assert.throws(() => firmwareText("amoled-175c", "1.1.0", 12.5, "e3".repeat(32)), /size/);
  assert.throws(() => firmwareText("amoled-175c", "1.1.0", 1, "E3".repeat(32)), /sha256/);
  assert.ok(HOST_ID_RE.test(F.PINNED_HOST_ID));
  assert.ok(!HOST_ID_RE.test("h_0123456789abcdef"));
});

test("verifyP256 accepts the high-S prove and firmware signatures (node:crypto)", () => {
  assert.equal(verifyP256(rfcPub(), F.PROVE_TEXT, hexDecode(F.PROVE_DER_HEX)), true);
  assert.equal(b64Encode(hexDecode(F.PROVE_DER_HEX)), F.PROVE_SIG_B64);
  assert.equal(verifyP256(b64DecodeCanonical(F.T1_PUBKEY_B64)!, F.FIRMWARE_TEXT, hexDecode(F.FIRMWARE_T1_DER_HEX)), true);
  assert.equal(verifyP256(rfcPub(), "sample", hexDecode(F.RFC_SAMPLE_DER_HEX)), true);
  assert.equal(verifyP256(rfcPub(), F.SHORT_DER_MESSAGE, hexDecode(F.SHORT_DER_HEX)), true);
});

test("verifyP256 also accepts the low-S twin (n - s) and rejects tampering", () => {
  const raw = derToRaw(hexDecode(F.PROVE_DER_HEX))!;
  const s = BigInt("0x" + hexEncode(raw.slice(32)));
  const twin = new Uint8Array(64);
  twin.set(raw.slice(0, 32), 0);
  twin.set(hexDecode((F.P256_N - s).toString(16).padStart(64, "0")), 32);
  assert.equal(verifyP256(rfcPub(), F.PROVE_TEXT, rawToDer(twin)), true);
  assert.equal(verifyP256(rfcPub(), F.PROVE_TEXT.replace(F.PINNED_HOST_ID, "000102030405060708090a0b0c0d0e0e"), hexDecode(F.PROVE_DER_HEX)), false);
  assert.equal(verifyP256(rfcPub(), F.PROVE_TEXT, hexDecode(F.PROVE_DER_HEX).slice(0, 70)), false);
  assert.equal(verifyP256(rfcPub().slice(0, 64), F.PROVE_TEXT, hexDecode(F.PROVE_DER_HEX)), false);
  const offCurve = rfcPub().slice();
  offCurve[64] ^= 1;
  assert.equal(verifyP256(offCurve, F.PROVE_TEXT, hexDecode(F.PROVE_DER_HEX)), false);
});

test("signP256 (random k) produces signatures verifyP256 accepts", () => {
  const der = signP256(F.T1_PRIVATE_HEX, F.FIRMWARE_TEXT);
  assert.ok(der.length >= 8 && der.length <= 72);
  assert.equal(verifyP256(publicKeyFromPrivate(F.T1_PRIVATE_HEX), F.FIRMWARE_TEXT, der), true);
});

test("derToRaw and rawToDer convert between DER and r||s", () => {
  const sample = hexDecode(F.RFC_SAMPLE_DER_HEX);
  const raw = derToRaw(sample)!;
  assert.equal(hexEncode(raw), F.RFC_SAMPLE_DER_HEX.slice(10, 74) + F.RFC_SAMPLE_DER_HEX.slice(80));
  assert.deepEqual(rawToDer(raw), sample);
  const short = hexDecode(F.SHORT_DER_HEX);
  assert.equal(short.length, 69);
  assert.deepEqual(rawToDer(derToRaw(short)!), short);
  assert.equal(derToRaw(hexDecode("3006020100020101")), null, "zero r is not a valid signature integer");
  assert.equal(derToRaw(new Uint8Array([0x30, 0x00])), null);
});

test("keys/test-t1.* hold the t1 test key derived from its seed text", () => {
  const keyHex = readFileSync(new URL("../../keys/test-t1.key.hex", import.meta.url), "utf8");
  const pubB64 = readFileSync(new URL("../../keys/test-t1.pub.b64", import.meta.url), "utf8");
  assert.equal(keyHex, createHash("sha256").update(F.T1_SEED_TEXT, "utf8").digest("hex") + "\n");
  assert.equal(keyHex, F.T1_PRIVATE_HEX + "\n");
  assert.equal(pubB64, F.T1_PUBKEY_B64 + "\n");
});
```

- [ ] **Step 2: Run it to see it fail**

Run: `node --test protocol/test/identity.test.ts`
Expected: FAIL with `ERR_MODULE_NOT_FOUND` for `protocol/lib/identity.ts`.

- [ ] **Step 3: Create `protocol/lib/identity.ts`**

```ts
// SPDX-License-Identifier: Apache-2.0
// Gadget identity and the two signed texts (PROTOCOL.md §4.2, §4.3, §4.8).
import { createHash } from "node:crypto";

export const HOST_ID_RE = /^[0-9a-f]{32}$/;
const SHA256_HEX_RE = /^[0-9a-f]{64}$/;

/** "gad_" + the first 16 lowercase hex characters of SHA-256(pubkey bytes). */
export function gadgetIdFromPubkey(pub65: Uint8Array): string {
  if (pub65.length !== 65 || pub65[0] !== 0x04) throw new Error("expected a 65-byte SEC1 uncompressed public key");
  return "gad_" + createHash("sha256").update(pub65).digest("hex").slice(0, 16);
}

/** The text a gadget signs in `prove`: the nonce is the exact base64 string from `challenge`. */
export function proveText(id: string, nonceB64: string, hostId: string): string {
  return ["openmausbot-gadget/1", "prove", id, nonceB64, hostId].join("\n");
}

/** The text the release key signs for a firmware image. size is base-10 without leading zeros. */
export function firmwareText(board: string, version: string, size: number, sha256Hex: string): string {
  if (!Number.isSafeInteger(size) || size < 0) throw new Error(`size must be a non-negative integer, got ${size}`);
  if (!SHA256_HEX_RE.test(sha256Hex)) throw new Error("sha256 must be 64 lowercase hex characters");
  return ["openmausbot-gadget/1", "firmware", board, version, String(size), sha256Hex].join("\n");
}
```

- [ ] **Step 4: Create `protocol/lib/der.ts`**

```ts
// SPDX-License-Identifier: Apache-2.0
// Strict DER <-> raw (r||s, 32 + 32 bytes) for P-256 ECDSA signatures. The firmware does the
// same conversion in C (spec §5.2); protocol/vectors/der.json pins both directions.

function readInt(der: Uint8Array, at: number): { value: Uint8Array; next: number } | null {
  if (at + 2 > der.length || der[at] !== 0x02) return null;
  const len = der[at + 1];
  if (len < 1 || len > 33 || at + 2 + len > der.length) return null;
  const bytes = der.subarray(at + 2, at + 2 + len);
  if (bytes[0] & 0x80) return null;                                  // negative
  if (bytes[0] === 0x00 && (len === 1 || !(bytes[1] & 0x80))) return null; // zero or non-minimal
  const value = bytes[0] === 0x00 ? bytes.subarray(1) : bytes;
  if (value.length > 32) return null;
  return { value, next: at + 2 + len };
}

/** Returns the 64-byte r||s, or null for anything that is not a minimal DER SEQUENCE of two
 *  positive INTEGERs of at most 32 value bytes with nothing after it. */
export function derToRaw(der: Uint8Array): Uint8Array | null {
  if (der.length < 8 || der.length > 72 || der[0] !== 0x30 || der[1] !== der.length - 2) return null;
  const r = readInt(der, 2);
  if (!r) return null;
  const s = readInt(der, r.next);
  if (!s || s.next !== der.length) return null;
  const raw = new Uint8Array(64);
  raw.set(r.value, 32 - r.value.length);
  raw.set(s.value, 64 - s.value.length);
  return raw;
}

function encodeInt(v: Uint8Array): Uint8Array {
  let i = 0;
  while (i < v.length - 1 && v[i] === 0) i++;
  const trimmed = v.subarray(i);
  const pad = trimmed[0] & 0x80 ? 1 : 0;
  const out = new Uint8Array(2 + pad + trimmed.length);
  out[0] = 0x02;
  out[1] = pad + trimmed.length;
  out.set(trimmed, 2 + pad);
  return out;
}

/** Minimal DER for a 64-byte r||s. */
export function rawToDer(raw: Uint8Array): Uint8Array {
  if (raw.length !== 64) throw new Error("expected 64-byte r||s");
  const r = encodeInt(raw.subarray(0, 32));
  const s = encodeInt(raw.subarray(32));
  const out = new Uint8Array(2 + r.length + s.length);
  out[0] = 0x30;
  out[1] = r.length + s.length;
  out.set(r, 2);
  out.set(s, 2 + r.length);
  return out;
}
```

- [ ] **Step 5: Create `protocol/lib/verify.ts`**

```ts
// SPDX-License-Identifier: Apache-2.0
// P-256 ECDSA with node:crypto only. node:crypto accepts high-S signatures, which the
// firmware produces about half the time (it never normalizes S; PROTOCOL.md §4.9).
import { Buffer } from "node:buffer";
import { createECDH, createPrivateKey, createPublicKey, sign, verify } from "node:crypto";

const SPKI_P256_PREFIX = Buffer.from("3059301306072a8648ce3d020106082a8648ce3d030107034200", "hex");
const PRIVATE_HEX_RE = /^[0-9a-f]{64}$/;

/** True only when `der` is a valid ECDSA-P256-SHA256 signature over the UTF-8 text. False on any
 *  parse error: wrong key length, a point not on the curve, malformed DER. */
export function verifyP256(pub65: Uint8Array, text: string, der: Uint8Array): boolean {
  if (pub65.length !== 65 || pub65[0] !== 0x04) return false;
  try {
    const key = createPublicKey({ key: Buffer.concat([SPKI_P256_PREFIX, pub65]), format: "der", type: "spki" });
    return verify("sha256", Buffer.from(text, "utf8"), { key, dsaEncoding: "der" }, der);
  } catch {
    return false;
  }
}

/** The 65-byte SEC1 uncompressed public key for a 32-byte private scalar given as 64 lowercase hex. */
export function publicKeyFromPrivate(privateKeyHex: string): Uint8Array {
  if (!PRIVATE_HEX_RE.test(privateKeyHex)) throw new Error("private key must be 64 lowercase hex characters");
  const ecdh = createECDH("prime256v1");
  ecdh.setPrivateKey(Buffer.from(privateKeyHex, "hex"));
  return new Uint8Array(ecdh.getPublicKey());
}

/** DER ECDSA-P256-SHA256 with a random k (node:crypto). For the fake host and dev tools; the
 *  deterministic vectors come from protocol/tools/gen-vectors.ts. */
export function signP256(privateKeyHex: string, text: string): Uint8Array {
  const pub = publicKeyFromPrivate(privateKeyHex);
  const b64u = (b: Uint8Array): string => Buffer.from(b).toString("base64url");
  const key = createPrivateKey({
    key: { kty: "EC", crv: "P-256", d: b64u(Buffer.from(privateKeyHex, "hex")), x: b64u(pub.subarray(1, 33)), y: b64u(pub.subarray(33)) },
    format: "jwk",
  });
  return new Uint8Array(sign("sha256", Buffer.from(text, "utf8"), { key, dsaEncoding: "der" }));
}
```

- [ ] **Step 6: Create the test key files**

The private scalar is SHA-256 of the text `openmausbot-gadget/1 test release key t1` (contract D12):

```bash
mkdir -p keys
printf 'openmausbot-gadget/1 test release key t1' | shasum -a 256
printf '%s\n' 274b871e29523ce21208177b90a0a3bd6a169cb84cf867236adc68f3d90fc0c8 > keys/test-t1.key.hex
printf '%s\n' 'BIUTH1UeVJw2VXn0Ehdhd8apNOHmB6VvjV512SxIbCCXfUhvlLRTuOTLHtrsVAbIx06JhMoOMbC3ic73hZpxMwg=' > keys/test-t1.pub.b64
```

Expected: the `shasum` line is `274b871e29523ce21208177b90a0a3bd6a169cb84cf867236adc68f3d90fc0c8  -`.

- [ ] **Step 7: Run the test to see it pass**

Run: `node --test protocol/test/identity.test.ts`
Expected: PASS (8). This proves Node's `crypto` accepts the high-S `prove` and firmware signatures and their low-S twin.

- [ ] **Step 8: Commit**

```bash
git add protocol/lib/identity.ts protocol/lib/der.ts protocol/lib/verify.ts protocol/test/identity.test.ts keys/test-t1.key.hex keys/test-t1.pub.b64
git commit -m "feat(protocol): identity, signed texts, DER and node:crypto verification; test key t1" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 5: Binary frames and firmware versions

**Files:**
- Create: `protocol/test/frames.test.ts`, `protocol/lib/frames.ts`, `protocol/lib/version.ts`

**Interfaces:**
- Consumes: nothing.
- Produces (contract §4.4.1):
  - `protocol/lib/frames.ts`: `type Kind = 1 | 2 | 3 | 4`; `encodeBinary(kind: Kind, stream: number, payload: Uint8Array): Uint8Array` (throws on stream outside 1–255 or a frame over 8192 bytes); `decodeBinary(frame: Uint8Array): {kind, stream, payload} | null`; `encodeFwChunk(stream: number, offset: number, data: Uint8Array): Uint8Array`; `decodeFwChunk(payload: Uint8Array): {offset, data} | null`.
  - `protocol/lib/version.ts`: `RELEASE_VERSION_RE`; `compareVersions(a: string, b: string): number` (−1, 0, 1; throws on non-release versions); `isCustomBuild(fw: string): boolean`.

- [ ] **Step 1: Write the failing test `protocol/test/frames.test.ts`**

```ts
// SPDX-License-Identifier: Apache-2.0
import { test } from "node:test";
import assert from "node:assert/strict";
import { decodeBinary, decodeFwChunk, encodeBinary, encodeFwChunk } from "../lib/frames.ts";
import { compareVersions, isCustomBuild } from "../lib/version.ts";

test("encodeBinary writes [kind][stream][payload]", () => {
  assert.deepEqual(encodeBinary(1, 7, new Uint8Array([0xaa, 0xbb])), new Uint8Array([1, 7, 0xaa, 0xbb]));
  assert.throws(() => encodeBinary(2, 0, new Uint8Array(1)), /stream/);
  assert.throws(() => encodeBinary(2, 256, new Uint8Array(1)), /stream/);
  assert.throws(() => encodeBinary(3, 1, new Uint8Array(8191)), /8192/);
  assert.equal(encodeBinary(3, 1, new Uint8Array(8190)).length, 8192);
});

test("decodeBinary rejects short frames, stream 0, unknown kinds and oversize frames", () => {
  assert.deepEqual(decodeBinary(new Uint8Array([2, 9, 1, 2])), { kind: 2, stream: 9, payload: new Uint8Array([1, 2]) });
  assert.equal(decodeBinary(new Uint8Array([1])), null);
  assert.equal(decodeBinary(new Uint8Array([1, 0, 5])), null);
  assert.equal(decodeBinary(new Uint8Array([5, 1, 5])), null);
  assert.equal(decodeBinary(new Uint8Array([0, 1, 5])), null);
  assert.equal(decodeBinary(new Uint8Array(8193).fill(1)), null);
});

test("firmware chunks carry a u32 little-endian offset and 1..4096 bytes", () => {
  const frame = encodeFwChunk(3, 65536, new Uint8Array([9, 8, 7]));
  assert.deepEqual(frame, new Uint8Array([4, 3, 0x00, 0x00, 0x01, 0x00, 9, 8, 7]));
  const decoded = decodeBinary(frame)!;
  assert.deepEqual(decodeFwChunk(decoded.payload), { offset: 65536, data: new Uint8Array([9, 8, 7]) });
  assert.equal(decodeFwChunk(new Uint8Array([0, 0, 0, 0])), null, "no data bytes");
  assert.equal(decodeFwChunk(new Uint8Array(4 + 4097)), null, "more than 4096 data bytes");
  assert.throws(() => encodeFwChunk(1, 0, new Uint8Array(4097)), /4096/);
  assert.throws(() => encodeFwChunk(1, 2 ** 32, new Uint8Array(1)), /offset/);
});

test("compareVersions follows SemVer 2.0.0 precedence", () => {
  assert.equal(compareVersions("1.1.0", "1.0.9"), 1);
  assert.equal(compareVersions("1.9.9", "1.10.0"), -1);
  assert.equal(compareVersions("1.1.0", "1.1.0-rc.1"), 1);
  assert.equal(compareVersions("1.1.0-rc.10", "1.1.0-rc.9"), 1);
  assert.equal(compareVersions("1.1.0-beta", "1.1.0-rc"), -1);
  assert.equal(compareVersions("1.1.0-rc", "1.1.0-rc.1"), -1);
  assert.equal(compareVersions("1.1.0-1", "1.1.0-a"), -1);
  assert.equal(compareVersions("2.0.0", "2.0.0"), 0);
  assert.throws(() => compareVersions("v1.0.0", "1.0.0"), /version/);
});

test("isCustomBuild flags -dev and anything that is not a release version", () => {
  for (const fw of ["0.0.0-dev", "1.2.0-dev", "v1.2.0", "1.2", ""]) assert.equal(isCustomBuild(fw), true, fw);
  for (const fw of ["1.2.0", "1.2.0-rc.1"]) assert.equal(isCustomBuild(fw), false, fw);
});
```

- [ ] **Step 2: Run it to see it fail**

Run: `node --test protocol/test/frames.test.ts`
Expected: FAIL with `ERR_MODULE_NOT_FOUND` for `protocol/lib/frames.ts`.

- [ ] **Step 3: Create `protocol/lib/frames.ts`**

```ts
// SPDX-License-Identifier: Apache-2.0
// Binary frames (PROTOCOL.md §4.1): byte 0 kind, byte 1 stream id (1–255), then the payload.

export type Kind = 1 | 2 | 3 | 4;
const BINARY_FRAME_MAX = 8192;
const FW_DATA_MAX = 4096;

export function encodeBinary(kind: Kind, stream: number, payload: Uint8Array): Uint8Array {
  if (![1, 2, 3, 4].includes(kind)) throw new Error(`unknown kind ${kind}`);
  if (!Number.isInteger(stream) || stream < 1 || stream > 255) throw new Error(`stream must be 1-255, got ${stream}`);
  if (payload.length + 2 > BINARY_FRAME_MAX) throw new Error(`binary frame would exceed 8192 bytes (${payload.length + 2})`);
  const out = new Uint8Array(payload.length + 2);
  out[0] = kind;
  out[1] = stream;
  out.set(payload, 2);
  return out;
}

export function decodeBinary(frame: Uint8Array): { kind: Kind; stream: number; payload: Uint8Array } | null {
  if (frame.length < 2 || frame.length > BINARY_FRAME_MAX) return null;
  const kind = frame[0];
  if (kind !== 1 && kind !== 2 && kind !== 3 && kind !== 4) return null;
  if (frame[1] === 0) return null;
  return { kind, stream: frame[1], payload: frame.slice(2) };
}

/** A kind-4 frame: u32 little-endian byte offset, then 1–4096 bytes of the image. */
export function encodeFwChunk(stream: number, offset: number, data: Uint8Array): Uint8Array {
  if (!Number.isInteger(offset) || offset < 0 || offset > 0xffffffff) throw new Error(`offset must fit a u32, got ${offset}`);
  if (data.length < 1 || data.length > FW_DATA_MAX) throw new Error(`a firmware chunk carries 1-4096 bytes, got ${data.length}`);
  const payload = new Uint8Array(4 + data.length);
  new DataView(payload.buffer).setUint32(0, offset, true);
  payload.set(data, 4);
  return encodeBinary(4, stream, payload);
}

export function decodeFwChunk(payload: Uint8Array): { offset: number; data: Uint8Array } | null {
  if (payload.length < 5 || payload.length > 4 + FW_DATA_MAX) return null;
  const offset = new DataView(payload.buffer, payload.byteOffset, payload.byteLength).getUint32(0, true);
  return { offset, data: payload.slice(4) };
}
```

- [ ] **Step 4: Create `protocol/lib/version.ts`**

```ts
// SPDX-License-Identifier: Apache-2.0
// Firmware versions (PROTOCOL.md §4.1; spec §8): the tag without "v"; SemVer 2.0.0 precedence.

export const RELEASE_VERSION_RE = /^\d+\.\d+\.\d+(-[0-9A-Za-z.]+)?$/;

function parse(v: string): { core: number[]; pre: string[] } {
  if (!RELEASE_VERSION_RE.test(v)) throw new Error(`not a release version: ${JSON.stringify(v)}`);
  const dash = v.indexOf("-");
  const core = (dash < 0 ? v : v.slice(0, dash)).split(".").map(Number);
  const pre = dash < 0 ? [] : v.slice(dash + 1).split(".");
  return { core, pre };
}

function cmpIdent(a: string, b: string): number {
  const an = /^\d+$/.test(a);
  const bn = /^\d+$/.test(b);
  if (an && bn) return Math.sign(Number(a) - Number(b));
  if (an !== bn) return an ? -1 : 1;
  return a < b ? -1 : a > b ? 1 : 0;
}

/** -1, 0 or 1 by SemVer 2.0.0 precedence. Throws unless both match RELEASE_VERSION_RE. */
export function compareVersions(a: string, b: string): number {
  const x = parse(a);
  const y = parse(b);
  for (let i = 0; i < 3; i++) if (x.core[i] !== y.core[i]) return x.core[i] < y.core[i] ? -1 : 1;
  if (x.pre.length === 0 || y.pre.length === 0) return x.pre.length === y.pre.length ? 0 : x.pre.length === 0 ? 1 : -1;
  for (let i = 0; i < Math.min(x.pre.length, y.pre.length); i++) {
    const c = cmpIdent(x.pre[i], y.pre[i]);
    if (c !== 0) return c;
  }
  return Math.sign(x.pre.length - y.pre.length);
}

/** A custom build: ends in "-dev", or is not a release version at all. */
export function isCustomBuild(fw: string): boolean {
  return fw.endsWith("-dev") || !RELEASE_VERSION_RE.test(fw);
}
```

- [ ] **Step 5: Run the test to see it pass**

Run: `node --test protocol/test/frames.test.ts`
Expected: PASS (5).

- [ ] **Step 6: Commit**

```bash
git add protocol/lib/frames.ts protocol/lib/version.ts protocol/test/frames.test.ts
git commit -m "feat(protocol): binary frames and firmware versions" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 6: Test vectors, generator and verifier; the CI protocol job

**Files:**
- Create: `protocol/test/vectors.test.ts`, `protocol/tools/gen-vectors.ts`, `.github/workflows/ci.yml`
- Generate and commit: `protocol/vectors/{base64,der,firmware,frames,identity,prove,rfc6979,versions}.json`, `protocol/vectors/SHA256SUMS`

**Interfaces:**
- Consumes: everything in `protocol/lib/` (Tasks 2–5) and the fixed values (Task 2).
- Produces: the eight vector files of contract §4.4 with exactly the case names listed there, and `SHA256SUMS`. Consumers: P2a (C tests), P3a (vendored copy), P4b (`firmware.json`, `versions.json`). CI job `protocol`.

- [ ] **Step 1: Write the failing verifier `protocol/test/vectors.test.ts`**

It uses `node:crypto` and `protocol/lib` only, never `@noble/curves`, so the generator and the verifier are independent.

```ts
// SPDX-License-Identifier: Apache-2.0
// Verifies every file in protocol/vectors with node:crypto and protocol/lib (never @noble/curves,
// so the generator and the verifier are independent implementations).
import { test } from "node:test";
import assert from "node:assert/strict";
import { Buffer } from "node:buffer";
import { createECDH, createHash } from "node:crypto";
import { readFileSync, readdirSync } from "node:fs";
import { b64DecodeCanonical, b64Encode, hexDecode, hexEncode } from "../lib/encoding.ts";
import { HOST_ID_RE, firmwareText, gadgetIdFromPubkey, proveText } from "../lib/identity.ts";
import { publicKeyFromPrivate, verifyP256 } from "../lib/verify.ts";
import { derToRaw, rawToDer } from "../lib/der.ts";
import { decodeBinary, decodeFwChunk, encodeBinary, encodeFwChunk, type Kind } from "../lib/frames.ts";
import { compareVersions, isCustomBuild } from "../lib/version.ts";
import * as F from "./fixed-values.ts";

const DIR = new URL("../vectors/", import.meta.url);
const EXPECTED_FILES = [
  "base64.json", "der.json", "firmware.json", "frames.json", "identity.json", "prove.json", "rfc6979.json", "versions.json",
];
type Case = Record<string, any>;
const read = (name: string): Buffer => readFileSync(new URL(name, DIR));
const load = (name: string): Case => JSON.parse(read(name).toString("utf8"));
const cases = (name: string): Case[] => load(name).cases;
const isHighS = (raw: Uint8Array): boolean => BigInt("0x" + hexEncode(raw.slice(32))) > F.P256_N / 2n;

test("the vector folder holds exactly the expected files and SHA256SUMS covers their bytes", () => {
  const files = readdirSync(DIR).filter((f) => f !== "SHA256SUMS").sort();
  assert.deepEqual(files, EXPECTED_FILES);
  const sums = read("SHA256SUMS").toString("utf8");
  assert.ok(sums.endsWith("\n") && !sums.includes("\r"));
  const expected = EXPECTED_FILES.map((f) => `${createHash("sha256").update(read(f)).digest("hex")}  ${f}`).join("\n") + "\n";
  assert.equal(sums, expected);
});

test("every vector file is 2-space JSON + LF with an envelope and unique case names", () => {
  for (const file of EXPECTED_FILES) {
    const text = read(file).toString("utf8");
    assert.ok(!text.includes("\r"), file);
    const value = JSON.parse(text);
    assert.equal(text, JSON.stringify(value, null, 2) + "\n", file);
    assert.equal(value.vectors, file.replace(/\.json$/, ""));
    assert.equal(value.version, 1);
    const list: Case[] = file === "versions.json" ? [...value.compare, ...value.custom] : value.cases;
    const names = list.map((c) => c.name);
    assert.equal(new Set(names).size, names.length, `${file} has duplicate names`);
  }
});

test("identity.json: pubkey and id derive from the private key", () => {
  const list = cases("identity.json");
  assert.deepEqual(list.map((c) => c.name), ["rfc6979-a25", "test-t1"]);
  for (const c of list) {
    const pub = publicKeyFromPrivate(c.private_key_hex);
    assert.equal(hexEncode(pub), c.pubkey_hex, c.name);
    assert.equal(b64Encode(pub), c.pubkey_b64, c.name);
    assert.equal(createHash("sha256").update(pub).digest("hex"), c.pubkey_sha256_hex, c.name);
    assert.equal(gadgetIdFromPubkey(pub), c.id, c.name);
  }
  assert.equal(list[0].id, F.RFC_ID);
  assert.equal(list[1].pubkey_b64, F.T1_PUBKEY_B64);
});

test("rfc6979.json: RFC 6979 A.2.5 values verify, k·G gives r, and high_s is right", () => {
  const list = cases("rfc6979.json");
  assert.deepEqual(list.map((c) => [c.name, c.high_s]), [["sample", true], ["test", false]]);
  for (const c of list) {
    assert.equal(createHash("sha256").update(c.message_utf8, "utf8").digest("hex"), c.message_sha256_hex);
    assert.equal(c.raw_hex, c.r_hex + c.s_hex);
    const der = hexDecode(c.der_hex);
    assert.equal(hexEncode(derToRaw(der)!), c.raw_hex);
    assert.deepEqual(rawToDer(hexDecode(c.raw_hex)), der);
    assert.equal(verifyP256(publicKeyFromPrivate(c.private_key_hex), c.message_utf8, der), true, c.name);
    const ecdh = createECDH("prime256v1");
    ecdh.setPrivateKey(Buffer.from(c.k_hex, "hex"));
    const kx = BigInt("0x" + ecdh.getPublicKey().subarray(1, 33).toString("hex"));
    assert.equal(kx % F.P256_N, BigInt("0x" + c.r_hex), `${c.name}: r = (k·G).x mod n`);
    assert.equal(isHighS(hexDecode(c.raw_hex)), c.high_s);
  }
  assert.equal(list[0].der_hex, F.RFC_SAMPLE_DER_HEX);
});

/** The host's checks in PROTOCOL.md §4.3 order, plus the gadget's host_id check on `challenge`. */
function proveVerdict(c: Case): string {
  if (!HOST_ID_RE.test(c.host_id)) return "reject_host_id";
  const pub = b64DecodeCanonical(c.pubkey_b64);
  if (!pub) return "reject_base64";
  if (pub.length !== 65 || pub[0] !== 0x04) return "reject_pubkey";
  if (gadgetIdFromPubkey(pub) !== c.id) return "reject_id";
  const sig = b64DecodeCanonical(c.sig_b64);
  if (!sig || !verifyP256(pub, proveText(c.id, c.nonce_b64, c.host_id), sig)) return "reject_sig";
  return "accept";
}

test("prove.json: every case gets its expected verdict from the host rules", () => {
  const list = cases("prove.json");
  assert.deepEqual(list.map((c) => c.name), [
    "pinned", "pinned-low-s", "low-s-host", "changed-host-id", "pubkey-not-id",
    "pubkey-non-canonical", "pubkey-compressed", "sig-truncated", "host-id-format",
  ]);
  for (const c of list) {
    assert.equal(proveVerdict(c), c.expect, c.name);
    assert.equal(c.text, proveText(c.id, c.nonce_b64, c.host_id), c.name);
    assert.equal(c.sig_b64, b64Encode(hexDecode(c.sig_der_hex)), c.name);
    const raw = derToRaw(hexDecode(c.sig_der_hex));
    assert.equal(raw ? isHighS(raw) : false, c.high_s, c.name);
  }
  const byName = Object.fromEntries(list.map((c) => [c.name, c]));
  const pinned = byName["pinned"];
  assert.equal(pinned.text, F.PROVE_TEXT);
  assert.equal(pinned.sig_der_hex, F.PROVE_DER_HEX);
  assert.equal(pinned.sig_b64, F.PROVE_SIG_B64);
  assert.deepEqual([pinned.deterministic, pinned.high_s], [true, true]);
  assert.deepEqual([byName["pinned-low-s"].deterministic, byName["pinned-low-s"].high_s], [false, false]);
  assert.deepEqual([byName["low-s-host"].host_id, byName["low-s-host"].high_s], [F.LOW_S_HOST_ID, false]);
  // Signature-valid negatives: only the named check may reject them.
  for (const name of ["pubkey-not-id", "pubkey-compressed", "host-id-format"]) {
    const c = byName[name];
    const pub = new Uint8Array(Buffer.from(c.pubkey_b64, "base64"));
    const der = hexDecode(c.sig_der_hex);
    if (name === "pubkey-compressed") assert.equal(pub.length, 33);
    else assert.equal(verifyP256(pub, c.text, der), true, `${name} signature itself is valid`);
  }
  // A lenient base64 decoder turns the non-canonical key into the real key: only the canonical check rejects it.
  assert.equal(Buffer.from(byName["pubkey-non-canonical"].pubkey_b64, "base64").toString("base64"), F.RFC_PUBKEY_B64);
});

test("der.json: strict DER parsing and raw conversion, and node:crypto rejects the invalid ones", () => {
  const list = cases("der.json");
  assert.deepEqual(list.map((c) => [c.name, c.valid]), [
    ["rfc-sample", true], ["short-der-69", true], ["prove-pinned", true],
    ["non-minimal-int", false], ["negative-int", false], ["trailing-bytes", false], ["wrong-tag", false],
  ]);
  for (const c of list) {
    const der = hexDecode(c.der_hex);
    const pub = publicKeyFromPrivate(c.private_key_hex);
    if (c.valid) {
      assert.equal(der.length, c.der_len, c.name);
      assert.equal(hexEncode(derToRaw(der)!), c.raw_hex, c.name);
      assert.deepEqual(rawToDer(hexDecode(c.raw_hex)), der, c.name);
      assert.equal(verifyP256(pub, c.message_utf8, der), true, c.name);
    } else {
      assert.equal(derToRaw(der), null, c.name);
      assert.equal(verifyP256(pub, c.message_utf8, der), false, c.name);
    }
  }
  assert.equal(list[1].der_len, 69);
});

test("firmware.json: the gadget's verdict uses its own board id", () => {
  const list = cases("firmware.json");
  assert.deepEqual(list.map((c) => [c.name, c.expect]), [
    ["t1-amoled", "accept"], ["other-board", "bad_sig"], ["size-changed", "bad_sig"], ["sha-uppercase", "bad_sig"],
  ]);
  for (const c of list) {
    const pub = b64DecodeCanonical(c.pubkey_b64)!;
    assert.deepEqual(pub, publicKeyFromPrivate(c.private_key_hex), c.name);
    const text = firmwareText(c.gadget_board, c.version, c.size, c.sha256);
    assert.equal(c.text, text, c.name);
    assert.equal(c.sig_b64, b64Encode(hexDecode(c.sig_der_hex)), c.name);
    assert.equal(verifyP256(pub, text, hexDecode(c.sig_der_hex)) ? "accept" : "bad_sig", c.expect, c.name);
  }
  assert.equal(list[0].text, F.FIRMWARE_TEXT);
  assert.equal(list[0].sig_der_hex, F.FIRMWARE_T1_DER_HEX);
  assert.equal(list[0].key_id, "t1");
});

test("base64.json: only canonical RFC 4648 §4 text decodes", () => {
  for (const c of cases("base64.json")) {
    const bytes = b64DecodeCanonical(c.input);
    assert.equal(bytes !== null, c.canonical, c.name);
    if (c.canonical) assert.equal(hexEncode(bytes!), c.bytes_hex, c.name);
  }
});

test("frames.json: binary frames decode and re-encode byte for byte", () => {
  for (const c of cases("frames.json")) {
    const frame = hexDecode(c.frame_hex);
    const d = decodeBinary(frame);
    const chunk = d && d.kind === 4 ? decodeFwChunk(d.payload) : null;
    const valid = d !== null && (d.kind !== 4 || chunk !== null);
    assert.equal(valid, c.valid, c.name);
    if (!c.valid) continue;
    assert.deepEqual([d!.kind, d!.stream, hexEncode(d!.payload)], [c.kind, c.stream, c.payload_hex], c.name);
    assert.deepEqual(encodeBinary(c.kind as Kind, c.stream, hexDecode(c.payload_hex)), frame, c.name);
    if (c.kind === 4) {
      assert.deepEqual([chunk!.offset, hexEncode(chunk!.data)], [c.offset, c.data_hex], c.name);
      assert.deepEqual(encodeFwChunk(c.stream, c.offset, hexDecode(c.data_hex)), frame, c.name);
    }
  }
});

test("versions.json: SemVer precedence and the custom-build rule", () => {
  const v = load("versions.json");
  for (const c of v.compare) {
    assert.equal(compareVersions(c.a, c.b), c.cmp, c.name);
    assert.equal(compareVersions(c.b, c.a), -c.cmp || 0, c.name);
  }
  for (const c of v.custom) assert.equal(isCustomBuild(c.fw), c.custom, c.name);
});
```

- [ ] **Step 2: Run it to see it fail**

Run: `node --test protocol/test/vectors.test.ts`
Expected: FAIL (10): every test fails with `ENOENT` because `protocol/vectors/` does not exist yet.

- [ ] **Step 3: Create the generator `protocol/tools/gen-vectors.ts`**

```ts
// SPDX-License-Identifier: Apache-2.0
// Writes protocol/vectors/*.json and SHA256SUMS (PROTOCOL.md §4.9; contract 00-interfaces.md §4.4).
// The only importer of @noble/curves. Its p256 defaults are low-S signatures and compressed keys,
// which the firmware cannot match, so every call passes lowS: false and getPublicKey(sk, false).
// Run: node protocol/tools/gen-vectors.ts
import { p256 } from "@noble/curves/nist.js";
import { Buffer } from "node:buffer";
import { createHash } from "node:crypto";
import { mkdirSync, readdirSync, rmSync, writeFileSync } from "node:fs";
import { b64Encode, hexDecode, hexEncode } from "../lib/encoding.ts";
import { firmwareText, gadgetIdFromPubkey, proveText } from "../lib/identity.ts";
import { encodeBinary, encodeFwChunk } from "../lib/frames.ts";
import { compareVersions, isCustomBuild } from "../lib/version.ts";
import * as F from "../test/fixed-values.ts";

type Json = Record<string, unknown>;
const OUT = new URL("../vectors/", import.meta.url);
const utf8 = (s: string): Uint8Array => new TextEncoder().encode(s);
const sha256Hex = (b: Uint8Array | string): string => createHash("sha256").update(b).digest("hex");

function check(cond: boolean, what: string): void {
  if (!cond) throw new Error(`gen-vectors: ${what}`);
}

/** RFC 6979 deterministic DER over SHA-256(text), S never normalized. */
function signDet(skHex: string, text: string): Uint8Array {
  return p256.sign(utf8(text), hexDecode(skHex), { lowS: false, format: "der" });
}
function pubOf(skHex: string): Uint8Array {
  const pub = p256.getPublicKey(hexDecode(skHex), false);
  check(pub.length === 65 && pub[0] === 0x04, "getPublicKey(sk, false) must give 65 bytes");
  return pub;
}
function rawOf(der: Uint8Array): Uint8Array {
  return p256.Signature.fromBytes(der, "der").toBytes("compact");
}
function highS(der: Uint8Array): boolean {
  return p256.Signature.fromBytes(der, "der").hasHighS();
}
/** high_s for a case: false when the bytes are not a valid DER signature at all. */
function highSOrFalse(der: Uint8Array): boolean {
  try {
    return highS(der);
  } catch {
    return false;
  }
}
function equalBytes(a: Uint8Array, b: Uint8Array): boolean {
  return a.length === b.length && a.every((x, i) => x === b[i]);
}
function lowSTwin(der: Uint8Array): Uint8Array {
  const sig = p256.Signature.fromBytes(der, "der");
  return new p256.Signature(sig.r, p256.Point.CURVE().n - sig.s).toBytes("der");
}

// ---- identity.json -----------------------------------------------------------------------------
function identity(): Json {
  const one = (name: string, sk: string): Json => {
    const pub = pubOf(sk);
    return {
      name, private_key_hex: sk, pubkey_hex: hexEncode(pub), pubkey_b64: b64Encode(pub),
      pubkey_sha256_hex: sha256Hex(pub), id: gadgetIdFromPubkey(pub),
    };
  };
  const cases = [one("rfc6979-a25", F.RFC_PRIVATE_HEX), one("test-t1", F.T1_PRIVATE_HEX)];
  check(cases[0].pubkey_b64 === F.RFC_PUBKEY_B64 && cases[0].id === F.RFC_ID, "RFC key identity differs from §1.7");
  check(cases[1].pubkey_b64 === F.T1_PUBKEY_B64, "t1 public key differs from §1.7");
  check(F.T1_PRIVATE_HEX === sha256Hex(F.T1_SEED_TEXT), "t1 is SHA-256 of its seed text");
  return { vectors: "identity", version: 1, cases };
}

// ---- rfc6979.json ------------------------------------------------------------------------------
const RFC_K = {
  sample: "a6e3c57dd01abe90086538398355dd4c3b17aa873382b0f24d6129493d8aad60",
  test: "d16b6ae827f17175e040871a1c7ec3500192c4c92677336ec2537acaee0008e0",
};
function rfc6979(): Json {
  const one = (message: "sample" | "test"): Json => {
    const der = signDet(F.RFC_PRIVATE_HEX, message);
    const raw = rawOf(der);
    const sig = p256.Signature.fromBytes(der, "der");
    const kG = p256.Point.BASE.multiply(BigInt("0x" + RFC_K[message]));
    check(kG.x % p256.Point.CURVE().n === sig.r, `RFC 6979 k for "${message}" must give r`);
    return {
      name: message, private_key_hex: F.RFC_PRIVATE_HEX, message_utf8: message, message_sha256_hex: sha256Hex(message),
      k_hex: RFC_K[message], r_hex: hexEncode(raw.slice(0, 32)), s_hex: hexEncode(raw.slice(32)),
      raw_hex: hexEncode(raw), der_hex: hexEncode(der), high_s: highS(der),
    };
  };
  const cases = [one("sample"), one("test")];
  check(cases[0].der_hex === F.RFC_SAMPLE_DER_HEX && cases[0].high_s === true, "RFC sample must be the §1.7 high-S DER");
  check(cases[1].high_s === false, "RFC test is low-S");
  return { vectors: "rfc6979", version: 1, cases };
}

// ---- prove.json --------------------------------------------------------------------------------
function prove(): Json {
  const sk = F.RFC_PRIVATE_HEX;
  const pub = pubOf(sk);
  const pubB64 = b64Encode(pub);
  const make = (name: string, expect: string, f: { pubkey_b64?: string; id?: string; host_id?: string; der?: Uint8Array }): Json => {
    const id = f.id ?? F.RFC_ID;
    const hostId = f.host_id ?? F.PINNED_HOST_ID;
    const text = proveText(id, F.PINNED_NONCE_B64, hostId);
    const der = f.der ?? signDet(sk, text);
    return {
      name, expect, private_key_hex: sk, pubkey_b64: f.pubkey_b64 ?? pubB64, id, nonce_b64: F.PINNED_NONCE_B64,
      host_id: hostId, text, sig_der_hex: hexEncode(der), sig_b64: b64Encode(der),
      deterministic: equalBytes(der, signDet(sk, text)), high_s: highSOrFalse(der),
    };
  };
  const pinnedDer = signDet(sk, proveText(F.RFC_ID, F.PINNED_NONCE_B64, F.PINNED_HOST_ID));
  const nonCanonical = pubB64.slice(0, -2) + String.fromCharCode(pubB64.charCodeAt(pubB64.length - 2) + 1) + "=";
  check(Buffer.from(nonCanonical, "base64").toString("base64") === pubB64 && nonCanonical !== pubB64, "pad-bit variant");
  const compressed = p256.getPublicKey(hexDecode(sk), true);
  const compressedId = "gad_" + sha256Hex(compressed).slice(0, 16);
  const cases = [
    make("pinned", "accept", {}),
    make("pinned-low-s", "accept", { der: lowSTwin(pinnedDer) }),
    make("low-s-host", "accept", { host_id: F.LOW_S_HOST_ID }),
    make("changed-host-id", "reject_sig", { host_id: "000102030405060708090a0b0c0d0e0e", der: pinnedDer }),
    make("pubkey-not-id", "reject_id", { id: "gad_0000000000000000" }),
    make("pubkey-non-canonical", "reject_base64", { pubkey_b64: nonCanonical }),
    make("pubkey-compressed", "reject_pubkey", { pubkey_b64: b64Encode(compressed), id: compressedId }),
    make("sig-truncated", "reject_sig", { der: pinnedDer.slice(0, pinnedDer.length - 1) }),
    make("host-id-format", "reject_host_id", { host_id: "h_0123456789abcdef" }),
  ];
  const [pinned, twin, lowHost] = cases;
  check(pinned.text === F.PROVE_TEXT && pinned.sig_der_hex === F.PROVE_DER_HEX && pinned.sig_b64 === F.PROVE_SIG_B64, "pinned prove must equal §1.7");
  check(pinned.high_s === true && pinned.deterministic === true, "pinned prove is deterministic and high-S");
  check(twin.high_s === false && twin.deterministic === false, "pinned-low-s is the low-S twin");
  check(lowHost.high_s === false && lowHost.deterministic === true, "low-s-host is the low-S control");
  for (const c of cases) if (c.high_s === true) check(highS(hexDecode(c.sig_der_hex as string)), `${c.name} hasHighS()`);
  return { vectors: "prove", version: 1, cases };
}

// ---- der.json ----------------------------------------------------------------------------------
function der(): Json {
  const sk = F.RFC_PRIVATE_HEX;
  const valid = (name: string, message: string): Json => {
    const d = signDet(sk, message);
    return { name, valid: true, der_hex: hexEncode(d), raw_hex: hexEncode(rawOf(d)), der_len: d.length, private_key_hex: sk, message_utf8: message };
  };
  const invalid = (name: string, d: Uint8Array, message: string): Json => ({
    name, valid: false, der_hex: hexEncode(d), private_key_hex: sk, message_utf8: message,
  });
  const sample = signDet(sk, "sample");
  const short = signDet(sk, F.SHORT_DER_MESSAGE);
  check(hexEncode(short) === F.SHORT_DER_HEX && short.length === 69, "short DER must equal §1.7 (69 bytes)");
  const r = short.slice(4, 36);
  const s31 = short.slice(38);
  check(short[36] === 0x02 && short[37] === 0x1f && s31.length === 31, "short DER layout");
  const nonMinimal = new Uint8Array([0x30, 68, 0x02, 0x20, ...r, 0x02, 0x20, 0x00, ...s31]);
  const negative = new Uint8Array([0x30, 2 + 32 + (sample.length - 37), 0x02, 0x20, ...sample.slice(5, 37), ...sample.slice(37)]);
  const trailing = new Uint8Array([...short, 0x00]);
  const wrongTag = sample.slice();
  wrongTag[0] = 0x31;
  const cases = [
    valid("rfc-sample", "sample"),
    valid("short-der-69", F.SHORT_DER_MESSAGE),
    valid("prove-pinned", F.PROVE_TEXT),
    invalid("non-minimal-int", nonMinimal, F.SHORT_DER_MESSAGE),
    invalid("negative-int", negative, "sample"),
    invalid("trailing-bytes", trailing, F.SHORT_DER_MESSAGE),
    invalid("wrong-tag", wrongTag, "sample"),
  ];
  for (const c of cases.slice(3)) {
    let parsed = true;
    try {
      p256.Signature.fromBytes(hexDecode(c.der_hex as string), "der");
    } catch {
      parsed = false;
    }
    check(!parsed, `${c.name} must not parse as DER`);
  }
  return { vectors: "der", version: 1, cases };
}

// ---- firmware.json -----------------------------------------------------------------------------
function firmware(): Json {
  const sk = F.T1_PRIVATE_HEX;
  const pubB64 = b64Encode(pubOf(sk));
  const board = "amoled-175c";
  const version = "1.1.0";
  const size = 1234567;
  const sha = sha256Hex("");
  const signed = signDet(sk, firmwareText(board, version, size, sha));
  const make = (name: string, expect: string, f: { gadget_board?: string; size?: number; der?: Uint8Array }): Json => {
    const gadgetBoard = f.gadget_board ?? board;
    const sz = f.size ?? size;
    const text = firmwareText(gadgetBoard, version, sz, sha);
    const d = f.der ?? signDet(sk, text);
    return {
      name, expect, key_id: "t1", private_key_hex: sk, pubkey_b64: pubB64, board, gadget_board: gadgetBoard,
      version, size: sz, sha256: sha, text, sig_der_hex: hexEncode(d), sig_b64: b64Encode(d),
      deterministic: equalBytes(d, signDet(sk, text)),
    };
  };
  const upperText = ["openmausbot-gadget/1", "firmware", board, version, String(size), sha.toUpperCase()].join("\n");
  const cases = [
    make("t1-amoled", "accept", {}),
    make("other-board", "bad_sig", { gadget_board: "lcd-154", der: signed }),
    make("size-changed", "bad_sig", { size: size + 1, der: signed }),
    make("sha-uppercase", "bad_sig", { der: signDet(sk, upperText) }),
  ];
  check(cases[0].text === F.FIRMWARE_TEXT && cases[0].sig_der_hex === F.FIRMWARE_T1_DER_HEX, "t1-amoled must equal §1.7");
  check(highS(signed), "t1-amoled is high-S");
  return { vectors: "firmware", version: 1, cases };
}

// ---- base64.json -------------------------------------------------------------------------------
function base64(): Json {
  const ok = (name: string, input: string): Json => ({ name, input, canonical: true, bytes_hex: hexEncode(new Uint8Array(Buffer.from(input, "base64"))) });
  const bad = (name: string, input: string): Json => ({ name, input, canonical: false });
  const cases = [
    ok("empty", ""), ok("one-byte", "AA=="), ok("two-bytes", "AAE="), ok("three-bytes", "AAEC"),
    ok("pinned-nonce", F.PINNED_NONCE_B64), ok("rfc-pubkey", F.RFC_PUBKEY_B64),
    bad("no-padding", "AA"), bad("short-padding", "AA="), bad("pad-bits", "AB=="), bad("missing-padding-3", "AAE"),
    bad("trailing-newline", "AA==\n"), bad("leading-space", " AA=="), bad("url-alphabet", "-_8="), bad("concatenated", "AA==AA=="),
  ];
  for (const c of cases) check((Buffer.from(c.input as string, "base64").toString("base64") === c.input) === c.canonical, `${c.name} round-trip`);
  return { vectors: "base64", version: 1, cases };
}

// ---- frames.json -------------------------------------------------------------------------------
function pcm(samples: number): Uint8Array {
  const out = new Uint8Array(samples * 2);
  const view = new DataView(out.buffer);
  for (let i = 0; i < samples; i++) view.setInt16(i * 2, ((i * 37) % 2000) - 1000, true);
  return out;
}
function pattern(len: number, mul: number, add: number): Uint8Array {
  const out = new Uint8Array(len);
  for (let i = 0; i < len; i++) out[i] = (i * mul + add) & 0xff;
  return out;
}
function frames(): Json {
  const frame = (name: string, kind: 1 | 2 | 3, stream: number, payload: Uint8Array): Json => ({
    name, valid: true, frame_hex: hexEncode(encodeBinary(kind, stream, payload)), kind, stream, payload_hex: hexEncode(payload),
  });
  const fw = (name: string, stream: number, offset: number, data: Uint8Array): Json => {
    const f = encodeFwChunk(stream, offset, data);
    return { name, valid: true, frame_hex: hexEncode(f), kind: 4, stream, payload_hex: hexEncode(f.slice(2)), offset, data_hex: hexEncode(data) };
  };
  const bad = (name: string, hex: string): Json => ({ name, valid: false, frame_hex: hex });
  const cases = [
    frame("mic-20ms", 1, 1, pcm(320)),
    frame("speaker-40ms-16k", 2, 1, pcm(640)),
    frame("image-rows", 3, 2, pattern(1200, 5, 1)),
    fw("fw-chunk", 3, 65536, pattern(4096, 31, 7)),
    fw("fw-chunk-last", 3, 1232896, pattern(1671, 31, 7)),
    bad("too-short", "01"),
    bad("stream-zero", "010000"),
    bad("unknown-kind", "050100"),
    bad("fw-no-offset", "0401aabbcc"),
  ];
  check((cases[0].payload_hex as string).length === 1280 && (cases[1].payload_hex as string).length === 2560, "audio frame sizes");
  return { vectors: "frames", version: 1, cases };
}

// ---- versions.json -----------------------------------------------------------------------------
function versions(): Json {
  const compare = [
    ["patch-vs-minor", "1.1.0", "1.0.9", 1], ["numeric-minor", "1.10.0", "1.9.9", 1], ["release-vs-rc", "1.1.0", "1.1.0-rc.1", 1],
    ["rc2-vs-rc1", "1.1.0-rc.2", "1.1.0-rc.1", 1], ["rc10-vs-rc9", "1.1.0-rc.10", "1.1.0-rc.9", 1], ["beta-vs-rc", "1.1.0-beta", "1.1.0-rc", -1],
    ["shorter-prerelease", "1.1.0-rc", "1.1.0-rc.1", -1], ["numeric-vs-alpha", "1.1.0-1", "1.1.0-a", -1],
    ["equal-release", "1.1.0", "1.1.0", 0], ["equal-rc", "1.1.0-rc.1", "1.1.0-rc.1", 0],
  ].map(([name, a, b, cmp]) => ({ name, a, b, cmp }));
  const custom = [
    ["dev-zero", "0.0.0-dev", true], ["dev-tagged", "1.2.0-dev", true], ["release", "1.2.0", false], ["prerelease", "1.2.0-rc.1", false],
    ["leading-v", "v1.2.0", true], ["two-parts", "1.2", true], ["empty", "", true],
  ].map(([name, fw, isCustom]) => ({ name, fw, custom: isCustom }));
  for (const c of compare) check(compareVersions(c.a as string, c.b as string) === c.cmp, `compare ${c.name}`);
  for (const c of custom) check(isCustomBuild(c.fw as string) === c.custom, `custom ${c.name}`);
  return { vectors: "versions", version: 1, compare, custom };
}

// ---- writing -----------------------------------------------------------------------------------
const BUILDERS: Record<string, () => Json> = {
  "base64.json": base64, "der.json": der, "firmware.json": firmware, "frames.json": frames,
  "identity.json": identity, "prove.json": prove, "rfc6979.json": rfc6979, "versions.json": versions,
};
const names = Object.keys(BUILDERS).sort();
mkdirSync(OUT, { recursive: true });
for (const stale of readdirSync(OUT)) if (stale.endsWith(".json") && !names.includes(stale)) rmSync(new URL(stale, OUT));
const sums: string[] = [];
for (const name of names) {
  const bytes = utf8(JSON.stringify(BUILDERS[name](), null, 2) + "\n");
  writeFileSync(new URL(name, OUT), bytes);
  sums.push(`${sha256Hex(bytes)}  ${name}`);
}
writeFileSync(new URL("SHA256SUMS", OUT), sums.join("\n") + "\n");
console.log(`wrote ${names.length} vector files and SHA256SUMS to protocol/vectors/`);
```

- [ ] **Step 4: Generate the vectors**

Run: `npm run vectors`
Expected: `wrote 8 vector files and SHA256SUMS to protocol/vectors/`. The generator throws (and writes nothing more) if any contract §1.7 value differs or a `high_s: true` case is not high-S.

Then: `cat protocol/vectors/SHA256SUMS`
Expected:

```
54e883d6fce3e4a71656127f22796208f41ed6c4002e29f9b147613459d9144e  base64.json
644b5d124d3b4d372bdaf30b760fcdb200ea13891fd6074a5f4b8c1c5e66e30a  der.json
dbabf94543ad2030449b7bb54c726c848a215d70cfc7fc4071229f80b595777c  firmware.json
a8323d132bcc44615de163b3693cfb55bafee15b56e1e5929f5045b0568ced51  frames.json
e45d6ae1de919f94ccf315b795455e13ffc0305ec134a8d8b2bd40f15fc4201a  identity.json
a6b0d8e474793b0eeabb2615a0bb0b57673f1779072eedb966644b9f8c3b171c  prove.json
3a4f2676f240b1417d889840b19daa245b98c25e2e29d4f4c71d3d92eb9f6e55  rfc6979.json
05a7d8aa923810ac13333412ede4450f1f5aa7049d2200db93e81ceec77cd618  versions.json
```

- [ ] **Step 5: Run every protocol test**

Run: `npm run test:protocol`
Expected: PASS (33).

- [ ] **Step 6: Create `.github/workflows/ci.yml`** with the `protocol` job (Task 12 appends `fake-host`; later plans append theirs, contract §1.6)

```yaml
# SPDX-License-Identifier: Apache-2.0
# One workflow; each plan appends its own job and never edits another plan's job
# (docs/plans/00-interfaces.md §1.6). P1 owns the `protocol` and `fake-host` jobs.
name: ci
on: [push, pull_request]
permissions:
  contents: read

jobs:
  protocol:
    runs-on: ubuntu-24.04
    steps:
      - uses: actions/checkout@v7.0.1
      - uses: actions/setup-node@v7.0.0
        with:
          node-version: 24
          cache: npm
      - run: npm ci
      - run: npm run vectors:check
      - run: npm run test:protocol
```

Check that it parses: `ruby -e 'require "yaml"; puts YAML.load_file(".github/workflows/ci.yml")["jobs"].keys.inspect'`
Expected: `["protocol"]`.

- [ ] **Step 7: Commit**

```bash
git add protocol/tools/gen-vectors.ts protocol/test/vectors.test.ts protocol/vectors .github/workflows/ci.yml
git commit -m "feat(protocol): test vectors, generator and verifier; CI protocol job" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

- [ ] **Step 8: Check that regeneration is byte-stable**

Run: `npm run vectors:check; echo "exit $?"`
Expected: the generator line, no diff output, then `exit 0`.

---

### Task 7: Fake host options, Latin-1 fold, host state and enrollment rules

**Files:**
- Create: `tools/fake-host/test/options.test.ts`, `tools/fake-host/test/enroll.test.ts`, `tools/fake-host/src/options.ts`, `tools/fake-host/src/fold.ts`, `tools/fake-host/src/state.ts`, `tools/fake-host/src/enroll.ts`

**Interfaces:**
- Consumes: `protocol/lib/types.ts` constants and types, `b64Encode`, `b64DecodeCanonical`, `gadgetIdFromPubkey`, `proveText`, `verifyP256`, `publicKeyFromPrivate`, `signP256`.
- Produces:
  - `options.ts`: `interface FakeHostOptions {port, bind, code: string | null, codeTtlS, stateDir: string | null, hostId: string | null, hostName, bot: BotRef, heard, reply, toneMs, otaKeyFile, otaKeyId, quiet, maxDevices, doneBeforeSpeech: boolean, pingMs, idleMs, handshakeMs, replyIntervalMs, actTimeoutMs, fwReadyTimeoutMs}`; `DEFAULT_OPTIONS`; `DEFAULT_OTA_KEY_FILE`; `parseBot(value: string): BotRef`; `parseCli(argv: string[]): FakeHostOptions` (throws `Error` with a message).
  - `fold.ts`: `foldLatin1(text: string): string`; `cutChars(text: string, max: number): string`; `cutUtf8(text: string, maxBytes: number): string`.
  - `state.ts`: `interface GadgetRecord {id, pubkey, name, board, fw, bot, settings, namePending, createdAt, lastSeenAt}`; `interface PairingWindow`; `type CodeCheck`; `PAIR_ATTEMPTS = 5`; `class HostState {hostId, hostName, gadgets: Map<string, GadgetRecord>, window, file; static load({stateDir, hostId, hostName}); save(); openWindow(code, ttlS, now); checkCode(code, now): CodeCheck; consumeWindow()}`.
  - `enroll.ts`: `interface NormalizedHello`; `type HelloCheck`; `normalizeName(name: unknown, id: string): string`; `checkHello(msg: Record<string, unknown>): HelloCheck`; `makeChallenge(hostId: string, hostName: string): ChallengeMsg`; `type ProveDecision = {ok: true; record; enrolled; sendName} | {ok: false; code; message}`; `decideProve({hello, challenge, prove, state, bot, maxDevices, now}): ProveDecision`. `bad_code` messages are `pairing code wrong`, `pairing code expired` and `pairing code used up`.

- [ ] **Step 1: Write the failing test `tools/fake-host/test/options.test.ts`**

```ts
// SPDX-License-Identifier: Apache-2.0
import { test } from "node:test";
import assert from "node:assert/strict";
import { DEFAULT_OPTIONS, parseBot, parseCli } from "../src/options.ts";
import { cutChars, cutUtf8, foldLatin1 } from "../src/fold.ts";

test("parseCli defaults match the contract's CLI table", () => {
  const o = parseCli([]);
  assert.equal(o.port, 8810);
  assert.equal(o.bind, "127.0.0.1");
  assert.equal(o.code, null);
  assert.equal(o.codeTtlS, 120);
  assert.equal(o.hostName, "Fake MausBot");
  assert.deepEqual(o.bot, { id: "b_fake", name: "Fake Bot" });
  assert.equal(o.heard, "What's on my calendar today?");
  assert.equal(o.toneMs, 800);
  assert.equal(o.otaKeyId, "t1");
  assert.ok(o.otaKeyFile.endsWith("keys/test-t1.key.hex"));
  assert.equal(o.doneBeforeSpeech, false);
  assert.equal(o.pingMs, 15000);
  assert.equal(o.idleMs, 45000);
  assert.deepEqual(o, DEFAULT_OPTIONS);
});

test("parseCli reads every option and validates them", () => {
  const o = parseCli([
    "--port", "0", "--bind", "0.0.0.0", "--code", "123456", "--code-ttl", "30", "--host-id", "0123456789abcdef0123456789abcdef",
    "--host-name", "Desk Mac", "--bot", "b_jev:Jev: the bot", "--heard", "", "--reply", "Hi", "--tone-ms", "0", "--quiet", "--max-devices", "1",
    "--done-before-speech",
  ]);
  assert.equal(o.port, 0);
  assert.equal(o.code, "123456");
  assert.equal(o.codeTtlS, 30);
  assert.equal(o.hostId, "0123456789abcdef0123456789abcdef");
  assert.deepEqual(o.bot, { id: "b_jev", name: "Jev: the bot" });
  assert.equal(o.heard, "");
  assert.equal(o.toneMs, 0);
  assert.equal(o.quiet, true);
  assert.equal(o.maxDevices, 1);
  assert.equal(o.doneBeforeSpeech, true);
  assert.throws(() => parseCli(["--code", "12345"]), /six digits/);
  assert.throws(() => parseCli(["--port", "70000"]), /--port/);
  assert.throws(() => parseCli(["--host-id", "h_0123"]), /--host-id/);
  assert.throws(() => parseCli(["--nope"]), /Unknown option/);
  assert.deepEqual(parseBot(":"), { id: "", name: "" });
});

test("foldLatin1 keeps Latin-1, … and →, maps lookalikes and drops the rest", () => {
  assert.equal(foldLatin1("Café “quoted” — ok… → next 🎉"), 'Café "quoted" - ok… → next ');
  assert.equal(foldLatin1("Łódź\tŠtěpán"), "Lódz Stepán");
  assert.equal(foldLatin1("line1\nline2\r"), "line1\nline2");
  assert.equal(foldLatin1("日本"), "");
});

test("cutChars and cutUtf8 cut on code-point boundaries", () => {
  assert.equal(cutChars("ab😀cd", 3), "ab😀");
  assert.equal(cutUtf8("é".repeat(40), 64), "é".repeat(32));
  assert.equal(cutUtf8("a😀", 4), "a");
});
```

- [ ] **Step 2: Write the failing test `tools/fake-host/test/enroll.test.ts`**

```ts
// SPDX-License-Identifier: Apache-2.0
import { test } from "node:test";
import assert from "node:assert/strict";
import { mkdtempSync, readFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { b64Encode } from "../../../protocol/lib/encoding.ts";
import { gadgetIdFromPubkey, proveText } from "../../../protocol/lib/identity.ts";
import { publicKeyFromPrivate, signP256 } from "../../../protocol/lib/verify.ts";
import type { ChallengeMsg } from "../../../protocol/lib/types.ts";
import { checkHello, decideProve, makeChallenge, normalizeName, type NormalizedHello } from "../src/enroll.ts";
import { HostState } from "../src/state.ts";

const KEY = "c9afa9d845ba75166b5c215767b1d6934e50c3db36e89b127b8a622b120f6721";
const PUB = publicKeyFromPrivate(KEY);
const ID = gadgetIdFromPubkey(PUB);
const BOT = { id: "b_fake", name: "Fake Bot" };
const hello = (over: Record<string, unknown> = {}): Record<string, unknown> => ({
  op: "hello", proto: 1, id: ID, pubkey: b64Encode(PUB), name: "Desk Maus", board: "amoled-175c", fw: "1.0.0",
  caps: { speaker: { rate: 16000 } }, actions: [], sensors: {}, ...over,
});
function normalized(): NormalizedHello {
  const r = checkHello(hello());
  assert.ok(r.ok);
  return r.hello;
}
function prove(challenge: ChallengeMsg, enroll?: string, key = KEY): Record<string, unknown> {
  const sig = b64Encode(signP256(key, proveText(ID, challenge.nonce, challenge.host_id)));
  return enroll === undefined ? { op: "prove", sig } : { op: "prove", sig, enroll };
}
function freshState(): HostState {
  const s = new HostState("000102030405060708090a0b0c0d0e0f", "Fake MausBot", null);
  s.openWindow("123456", 120, 1000);
  return s;
}

test("checkHello applies rule 1 and normalizes the hello", () => {
  assert.equal(normalized().id, "gad_b18b86ce1389e46d");
  assert.deepEqual(checkHello(hello({ proto: 2 })), { ok: false, code: "proto_unsupported", message: "proto 2 is not supported" });
  const pubB64 = b64Encode(PUB);
  const nonCanonical = pubB64.slice(0, -2) + String.fromCharCode(pubB64.charCodeAt(pubB64.length - 2) + 1) + "=";
  for (const bad of [{ pubkey: nonCanonical }, { pubkey: b64Encode(PUB.slice(0, 33)) }, { id: "gad_0000000000000000" }, { board: "Bad Board" }]) {
    const r = checkHello(hello(bad));
    assert.equal(r.ok ? "ok" : r.code, "bad_sig", JSON.stringify(bad));
  }
});

test("checkHello cuts the name, drops actions that break a limit and defaults risk to confirm", () => {
  const long = "x".repeat(40);
  const ok = { name: "chime", description: "Play a short chime.", params: { type: "object" } };
  const actions = [
    ok, { ...ok, name: "safe_one", risk: "safe" }, { ...ok, name: "Bad Name" }, { ...ok, name: "long_desc", description: "d".repeat(201) },
    { ...ok, name: "big_params", params: { type: "object", description: "p".repeat(1100) } },
    ...Array.from({ length: 20 }, (_, i) => ({ ...ok, name: `extra_${i}` })),
  ];
  const r = checkHello(hello({ name: long, actions }));
  assert.ok(r.ok);
  assert.equal(r.hello.name, "x".repeat(32));
  assert.equal(r.hello.actions.length, 16);
  assert.deepEqual(r.hello.actions.slice(0, 2).map((a) => [a.name, a.risk]), [["chime", "confirm"], ["safe_one", "safe"]]);
  assert.ok(!r.hello.actions.some((a) => ["Bad Name", "long_desc", "big_params"].includes(a.name)));
  assert.equal(normalizeName("", ID), "Maus b18b");
  assert.equal(normalizeName("a" + String.fromCharCode(7) + "b", ID), "ab");
});

test("makeChallenge sends 32 fresh random bytes and a folded host name", () => {
  const a = makeChallenge("000102030405060708090a0b0c0d0e0f", "Omkar’s Mac 🖥");
  const b = makeChallenge("000102030405060708090a0b0c0d0e0f", "x");
  assert.equal(a.nonce.length, 44);
  assert.notEqual(a.nonce, b.nonce);
  assert.equal(a.host_name, "Omkar's Mac ");
});

test("rule 3: the right code enrolls once and consumes the window", () => {
  const state = freshState();
  const h = normalized();
  const ch = makeChallenge(state.hostId, state.hostName);
  const r = decideProve({ hello: h, challenge: ch, prove: prove(ch, "123456"), state, bot: BOT, maxDevices: 20, now: 2000 });
  assert.ok(r.ok && r.enrolled);
  assert.deepEqual(r.record.bot, BOT);
  assert.equal(state.window!.used, true);
  const other = checkHello(hello({ id: gadgetIdFromPubkey(publicKeyFromPrivate("11".repeat(32))), pubkey: b64Encode(publicKeyFromPrivate("11".repeat(32))) }));
  assert.ok(other.ok);
  const ch2 = makeChallenge(state.hostId, state.hostName);
  const sig = b64Encode(signP256("11".repeat(32), proveText(other.hello.id, ch2.nonce, ch2.host_id)));
  const r2 = decideProve({ hello: other.hello, challenge: ch2, prove: { sig, enroll: "123456" }, state, bot: BOT, maxDevices: 20, now: 2000 });
  assert.deepEqual(r2, { ok: false, code: "bad_code", message: "pairing code used up" });
});

test("rule 3: wrong codes use attempts, then the window is used up; expiry is reported", () => {
  const state = freshState();
  const h = normalized();
  const ch = makeChallenge(state.hostId, state.hostName);
  for (let i = 0; i < 5; i++) {
    const r = decideProve({ hello: h, challenge: ch, prove: prove(ch, i === 0 ? "12345" : "654321"), state, bot: BOT, maxDevices: 20, now: 2000 });
    assert.deepEqual(r, { ok: false, code: "bad_code", message: "pairing code wrong" });
  }
  const used = decideProve({ hello: h, challenge: ch, prove: prove(ch, "123456"), state, bot: BOT, maxDevices: 20, now: 2000 });
  assert.deepEqual(used, { ok: false, code: "bad_code", message: "pairing code used up" });
  const late = freshState();
  const r = decideProve({ hello: h, challenge: ch, prove: prove(ch, "123456"), state: late, bot: BOT, maxDevices: 20, now: 1000 + 120_000 });
  assert.deepEqual(r, { ok: false, code: "bad_code", message: "pairing code expired" });
});

test("rule 3: device_limit is checked only after the code matches and keeps the window open", () => {
  const state = freshState();
  const h = normalized();
  const ch = makeChallenge(state.hostId, state.hostName);
  const wrong = decideProve({ hello: h, challenge: ch, prove: prove(ch, "000000"), state, bot: BOT, maxDevices: 0, now: 2000 });
  assert.equal(wrong.ok ? "ok" : wrong.code, "bad_code");
  const full = decideProve({ hello: h, challenge: ch, prove: prove(ch, "123456"), state, bot: BOT, maxDevices: 0, now: 2000 });
  assert.deepEqual(full, { ok: false, code: "device_limit", message: "device limit reached" });
  assert.equal(state.window!.used, false);
  const ok = decideProve({ hello: h, challenge: ch, prove: prove(ch, "123456"), state, bot: BOT, maxDevices: 1, now: 2000 });
  assert.ok(ok.ok && ok.enrolled);
});

test("rules 2 and 4: a known id ignores enroll; an unknown id without enroll is refused; bad signatures fail", () => {
  const state = freshState();
  const h = normalized();
  const ch = makeChallenge(state.hostId, state.hostName);
  assert.deepEqual(decideProve({ hello: h, challenge: ch, prove: prove(ch), state, bot: BOT, maxDevices: 20, now: 2000 }),
    { ok: false, code: "enroll_required", message: "this gadget is not paired" });
  assert.ok(decideProve({ hello: h, challenge: ch, prove: prove(ch, "123456"), state, bot: BOT, maxDevices: 20, now: 2000 }).ok);
  const again = decideProve({ hello: h, challenge: ch, prove: prove(ch, "999999"), state, bot: BOT, maxDevices: 20, now: 3000 });
  assert.ok(again.ok && !again.enrolled);
  const forged = decideProve({ hello: h, challenge: ch, prove: prove(ch, undefined, "22".repeat(32)), state, bot: BOT, maxDevices: 20, now: 3000 });
  assert.deepEqual(forged, { ok: false, code: "bad_sig", message: "signature does not verify" });
  const otherHost = { ...ch, host_id: "000102030405060708090a0b0c0d0e0e" };
  const replayed = decideProve({ hello: h, challenge: otherHost, prove: prove(ch), state, bot: BOT, maxDevices: 20, now: 3000 });
  assert.equal(replayed.ok ? "ok" : replayed.code, "bad_sig");
  assert.equal(decideProve({ hello: h, challenge: ch, prove: { sig: "not base64" }, state, bot: BOT, maxDevices: 20, now: 3000 }).ok, false);
});

test("name: last writer wins, except a pending host-side rename", () => {
  const state = freshState();
  const h = normalized();
  const ch = makeChallenge(state.hostId, state.hostName);
  const first = decideProve({ hello: h, challenge: ch, prove: prove(ch, "123456"), state, bot: BOT, maxDevices: 20, now: 2000 });
  assert.ok(first.ok);
  first.record.name = "Kitchen";
  first.record.namePending = true;
  const next = decideProve({ hello: h, challenge: ch, prove: prove(ch), state, bot: BOT, maxDevices: 20, now: 3000 });
  assert.ok(next.ok && next.sendName);
  assert.equal(next.record.name, "Kitchen");
  const after = decideProve({ hello: { ...h, name: "Desk 2" }, challenge: ch, prove: prove(ch), state, bot: BOT, maxDevices: 20, now: 4000 });
  assert.ok(after.ok && !after.sendName);
  assert.equal(after.record.name, "Desk 2");
});

test("HostState persists host_id and gadgets, never the window", () => {
  const dir = mkdtempSync(join(tmpdir(), "fake-host-"));
  const a = HostState.load({ stateDir: dir, hostId: null, hostName: "x" });
  a.openWindow("123456", 120, 0);
  const h = normalized();
  const ch = makeChallenge(a.hostId, a.hostName);
  assert.ok(decideProve({ hello: h, challenge: ch, prove: prove(ch, "123456"), state: a, bot: BOT, maxDevices: 20, now: 1 }).ok);
  const b = HostState.load({ stateDir: dir, hostId: null, hostName: "x" });
  assert.equal(b.hostId, a.hostId);
  assert.ok(b.gadgets.has(ID));
  assert.equal(b.window, null);
  assert.match(a.hostId, /^[0-9a-f]{32}$/);
  assert.equal(JSON.parse(readFileSync(join(dir, "fake-host.json"), "utf8")).version, 1);
});
```

- [ ] **Step 3: Run them to see them fail**

Run: `node --test tools/fake-host/test/options.test.ts tools/fake-host/test/enroll.test.ts`
Expected: FAIL with `ERR_MODULE_NOT_FOUND` for `tools/fake-host/src/options.ts` and `tools/fake-host/src/enroll.ts`.

- [ ] **Step 4: Create `tools/fake-host/src/options.ts`**

```ts
// SPDX-License-Identifier: Apache-2.0
// Fake host options: the CLI of docs/plans/00-interfaces.md §4.7, plus timing knobs that only
// in-process tests set (the CLI always uses the protocol's real timings).
import { resolve } from "node:path";
import { fileURLToPath } from "node:url";
import { parseArgs } from "node:util";
import type { BotRef } from "../../../protocol/lib/types.ts";
import {
  ACT_TIMEOUT_MS, FW_READY_TIMEOUT_MS, HANDSHAKE_TIMEOUT_MS, HOST_ID_RE, IDLE_TIMEOUT_MS, PAIR_CODE_RE,
  PING_INTERVAL_MS, REPLY_MIN_INTERVAL_MS,
} from "../../../protocol/lib/types.ts";

export interface FakeHostOptions {
  port: number;
  bind: string;
  code: string | null;          // null = a random six-digit code
  codeTtlS: number;
  stateDir: string | null;      // null = memory only
  hostId: string | null;        // null = random (or the one in the state file)
  hostName: string;
  bot: BotRef;
  heard: string;
  reply: string;
  toneMs: number;
  otaKeyFile: string;
  otaKeyId: string;
  quiet: boolean;
  maxDevices: number;
  doneBeforeSpeech: boolean;    // send done ok right after the final reply, then the speech (MausBot's usual order)
  // In-process only (tests): protocol timings.
  pingMs: number;
  idleMs: number;
  handshakeMs: number;
  replyIntervalMs: number;
  actTimeoutMs: number;
  fwReadyTimeoutMs: number;
}

export const DEFAULT_OTA_KEY_FILE = fileURLToPath(new URL("../../../keys/test-t1.key.hex", import.meta.url));

export const DEFAULT_OPTIONS: FakeHostOptions = {
  port: 8810,
  bind: "127.0.0.1",
  code: null,
  codeTtlS: 120,
  stateDir: null,
  hostId: null,
  hostName: "Fake MausBot",
  bot: { id: "b_fake", name: "Fake Bot" },
  heard: "What's on my calendar today?",
  reply: "You have two meetings today: design review at 10 and lunch with Sam at 1.",
  toneMs: 800,
  otaKeyFile: DEFAULT_OTA_KEY_FILE,
  otaKeyId: "t1",
  quiet: false,
  maxDevices: 20,
  doneBeforeSpeech: false,
  pingMs: PING_INTERVAL_MS,
  idleMs: IDLE_TIMEOUT_MS,
  handshakeMs: HANDSHAKE_TIMEOUT_MS,
  replyIntervalMs: REPLY_MIN_INTERVAL_MS,
  actTimeoutMs: ACT_TIMEOUT_MS,
  fwReadyTimeoutMs: FW_READY_TIMEOUT_MS,
};

function int(name: string, value: string, min: number, max: number): number {
  const n = Number(value);
  if (!/^\d+$/.test(value) || n < min || n > max) throw new Error(`--${name} must be an integer from ${min} to ${max}`);
  return n;
}

/** "<id>:<name>"; the name may contain ":". "" before the colon means an unbound gadget. */
export function parseBot(value: string): BotRef {
  const at = value.indexOf(":");
  if (at < 0) throw new Error("--bot must be <id>:<name>");
  return { id: value.slice(0, at), name: value.slice(at + 1) };
}

/** Parses argv (without node and the script). Throws Error with a message for bad input. */
export function parseCli(argv: string[]): FakeHostOptions {
  const { values } = parseArgs({
    args: argv,
    strict: true,
    allowPositionals: false,
    options: {
      port: { type: "string" }, bind: { type: "string" }, code: { type: "string" }, "code-ttl": { type: "string" },
      state: { type: "string" }, "host-id": { type: "string" }, "host-name": { type: "string" }, bot: { type: "string" },
      heard: { type: "string" }, reply: { type: "string" }, "tone-ms": { type: "string" }, "ota-key": { type: "string" },
      "ota-key-id": { type: "string" }, quiet: { type: "boolean" }, "max-devices": { type: "string" },
      "done-before-speech": { type: "boolean" },
    },
  });
  const o: FakeHostOptions = { ...DEFAULT_OPTIONS };
  if (values.port !== undefined) o.port = int("port", values.port, 0, 65535);
  if (values.bind !== undefined) o.bind = values.bind;
  if (values.code !== undefined) {
    if (!PAIR_CODE_RE.test(values.code)) throw new Error("--code must be six digits");
    o.code = values.code;
  }
  if (values["code-ttl"] !== undefined) o.codeTtlS = int("code-ttl", values["code-ttl"], 1, 86400);
  if (values.state !== undefined) o.stateDir = resolve(values.state);
  if (values["host-id"] !== undefined) {
    if (!HOST_ID_RE.test(values["host-id"])) throw new Error("--host-id must be 32 lowercase hex characters");
    o.hostId = values["host-id"];
  }
  if (values["host-name"] !== undefined) o.hostName = values["host-name"];
  if (values.bot !== undefined) o.bot = parseBot(values.bot);
  if (values.heard !== undefined) o.heard = values.heard;
  if (values.reply !== undefined) o.reply = values.reply;
  if (values["tone-ms"] !== undefined) o.toneMs = int("tone-ms", values["tone-ms"], 0, 60000);
  if (values["ota-key"] !== undefined) o.otaKeyFile = resolve(values["ota-key"]);
  if (values["ota-key-id"] !== undefined) o.otaKeyId = values["ota-key-id"];
  if (values.quiet) o.quiet = true;
  if (values["max-devices"] !== undefined) o.maxDevices = int("max-devices", values["max-devices"], 0, 1000);
  if (values["done-before-speech"]) o.doneBeforeSpeech = true;
  return o;
}
```

- [ ] **Step 5: Create `tools/fake-host/src/fold.ts`**

```ts
// SPDX-License-Identifier: Apache-2.0
// Screen text for caps.screen.text "latin1" (PROTOCOL.md §4.4): U+0020–U+007E, U+00A0–U+00FF and
// newline, plus U+2026 and U+2192. Other characters map to a close equivalent or are dropped.
// The fake host does not shape Markdown: its scripted texts are plain.
import { Buffer } from "node:buffer";

// Code point -> replacement. Curly quotes and primes, dashes and minus, bullet, thin and wide spaces,
// tab, and letters with no Unicode decomposition (L/l with stroke, D/d with stroke, OE, dotless i).
const MAP = new Map<number, string>([
  [0x2018, "'"], [0x2019, "'"], [0x201a, "'"], [0x201b, "'"], [0x2032, "'"],
  [0x201c, '"'], [0x201d, '"'], [0x201e, '"'], [0x201f, '"'], [0x2033, '"'],
  [0x2010, "-"], [0x2011, "-"], [0x2012, "-"], [0x2013, "-"], [0x2014, "-"], [0x2015, "-"], [0x2212, "-"],
  [0x2022, String.fromCodePoint(0xb7)], [0x2002, " "], [0x2003, " "], [0x2009, " "], [0x200a, " "], [0x202f, " "], [0x09, " "],
  [0x0141, "L"], [0x0142, "l"], [0x0110, "D"], [0x0111, "d"], [0x0152, "OE"], [0x0153, "oe"], [0x0131, "i"],
]);

function allowed(cp: number): boolean {
  return cp === 0x0a || (cp >= 0x20 && cp <= 0x7e) || (cp >= 0xa0 && cp <= 0xff) || cp === 0x2026 || cp === 0x2192;
}

/** Folds text to the latin1 screen charset. */
export function foldLatin1(text: string): string {
  let out = "";
  for (const ch of text.normalize("NFC")) {
    const cp = ch.codePointAt(0)!;
    if (allowed(cp)) out += ch;
    else if (MAP.has(cp)) out += MAP.get(cp);
    else {
      const base = ch.normalize("NFKD").replace(/\p{M}/gu, "");
      if (base.length > 0 && [...base].every((c) => allowed(c.codePointAt(0)!))) out += base;
    }
  }
  return out;
}

/** Cuts to at most `max` characters (code points). */
export function cutChars(text: string, max: number): string {
  const chars = [...text];
  return chars.length <= max ? text : chars.slice(0, max).join("");
}

/** Cuts to at most `maxBytes` UTF-8 bytes on a code-point boundary, keeping the start. */
export function cutUtf8(text: string, maxBytes: number): string {
  let out = "";
  let bytes = 0;
  for (const ch of text) {
    const n = Buffer.byteLength(ch, "utf8");
    if (bytes + n > maxBytes) break;
    out += ch;
    bytes += n;
  }
  return out;
}
```

- [ ] **Step 6: Create `tools/fake-host/src/state.ts`**

```ts
// SPDX-License-Identifier: Apache-2.0
// What the fake host remembers: its host_id, enrolled gadgets and the one pairing window.
// With --state <dir>, host_id and gadgets persist in <dir>/fake-host.json (the window never does).
import { randomBytes, randomInt } from "node:crypto";
import { existsSync, mkdirSync, readFileSync, renameSync, writeFileSync } from "node:fs";
import { join } from "node:path";
import type { BotRef, GadgetSettings } from "../../../protocol/lib/types.ts";
import { HOST_ID_RE } from "../../../protocol/lib/types.ts";

export interface GadgetRecord {
  id: string;
  pubkey: string;          // canonical base64, as sent in hello
  name: string;
  board: string;
  fw: string;
  bot: BotRef;
  settings: GadgetSettings;
  namePending: boolean;    // renamed here while offline: keep this name at the next hello
  createdAt: number;
  lastSeenAt: number;
}

export interface PairingWindow {
  code: string;
  expiresAt: number;       // epoch ms
  attemptsLeft: number;
  used: boolean;
}

export type CodeCheck = { ok: true } | { ok: false; reason: "wrong" | "expired" | "used up" };

export const PAIR_ATTEMPTS = 5;

export class HostState {
  hostId: string;
  hostName: string;
  gadgets = new Map<string, GadgetRecord>();
  window: PairingWindow | null = null;
  readonly file: string | null;

  constructor(hostId: string, hostName: string, file: string | null) {
    this.hostId = hostId;
    this.hostName = hostName;
    this.file = file;
  }

  /** Loads <dir>/fake-host.json when stateDir is set. An explicit hostId wins over the file's. */
  static load(opts: { stateDir: string | null; hostId: string | null; hostName: string }): HostState {
    const file = opts.stateDir ? join(opts.stateDir, "fake-host.json") : null;
    let saved: { host_id?: string; gadgets?: GadgetRecord[] } = {};
    if (file && existsSync(file)) saved = JSON.parse(readFileSync(file, "utf8"));
    const fromFile = typeof saved.host_id === "string" && HOST_ID_RE.test(saved.host_id) ? saved.host_id : null;
    const state = new HostState(opts.hostId ?? fromFile ?? randomBytes(16).toString("hex"), opts.hostName, file);
    for (const g of saved.gadgets ?? []) state.gadgets.set(g.id, g);
    state.save();
    return state;
  }

  save(): void {
    if (!this.file) return;
    mkdirSync(join(this.file, ".."), { recursive: true });
    const tmp = this.file + ".tmp";
    writeFileSync(tmp, JSON.stringify({ version: 1, host_id: this.hostId, gadgets: [...this.gadgets.values()] }, null, 2) + "\n");
    renameSync(tmp, this.file);
  }

  /** Opens a new window, replacing any old one. A null code picks a random six-digit code. */
  openWindow(code: string | null, ttlS: number, now: number): PairingWindow {
    const c = code ?? String(randomInt(0, 1_000_000)).padStart(6, "0");
    this.window = { code: c, expiresAt: now + ttlS * 1000, attemptsLeft: PAIR_ATTEMPTS, used: false };
    return this.window;
  }

  /** Checks a code against the window. A wrong code (including one that is not six digits) uses an
   *  attempt. A matching code does not consume the window: the caller checks the device cap first. */
  checkCode(code: string, now: number): CodeCheck {
    const w = this.window;
    if (!w || w.used || w.attemptsLeft <= 0) return { ok: false, reason: "used up" };
    if (now >= w.expiresAt) return { ok: false, reason: "expired" };
    if (!/^\d{6}$/.test(code) || code !== w.code) {
      w.attemptsLeft -= 1;
      return { ok: false, reason: "wrong" };
    }
    return { ok: true };
  }

  consumeWindow(): void {
    if (this.window) this.window.used = true;
  }
}
```

- [ ] **Step 7: Create `tools/fake-host/src/enroll.ts`**

```ts
// SPDX-License-Identifier: Apache-2.0
// The host rules of PROTOCOL.md §4.3, with node:crypto verification (accepts high-S signatures).
import { Buffer } from "node:buffer";
import { randomBytes } from "node:crypto";
import { b64DecodeCanonical, b64Encode } from "../../../protocol/lib/encoding.ts";
import { gadgetIdFromPubkey, proveText } from "../../../protocol/lib/identity.ts";
import { verifyP256 } from "../../../protocol/lib/verify.ts";
import type {
  BotRef, ChallengeMsg, GadgetActionDecl, GadgetCaps, GadgetErrorCode, GadgetSensors, Risk,
} from "../../../protocol/lib/types.ts";
import {
  ACTION_DESCRIPTION_MAX, ACTION_NAME_RE, ACTION_PARAMS_MAX_BYTES, ACTIONS_MAX, BOARD_ID_RE, NAME_MAX_CHARS, PROTO_VERSION,
} from "../../../protocol/lib/types.ts";
import { cutChars, cutUtf8, foldLatin1 } from "./fold.ts";
import type { GadgetRecord, HostState } from "./state.ts";

export interface NormalizedHello {
  proto: number;
  id: string;
  pubkey: string;
  pubkeyBytes: Uint8Array;
  name: string;
  board: string;
  fw: string;
  caps: GadgetCaps;
  actions: Array<GadgetActionDecl & { risk: Risk }>;
  sensors: GadgetSensors;
}

export type HelloCheck =
  | { ok: true; hello: NormalizedHello }
  | { ok: false; code: "proto_unsupported" | "bad_sig"; message: string };

const isObject = (v: unknown): v is Record<string, unknown> => typeof v === "object" && v !== null && !Array.isArray(v);

/** Display name: control characters removed, cut to 32 characters; "" → "Maus " + 4 hex of the id. */
export function normalizeName(name: unknown, id: string): string {
  const clean = typeof name === "string" ? cutChars(name.replace(/\p{Cc}/gu, "").trim(), NAME_MAX_CHARS) : "";
  return clean === "" ? "Maus " + id.slice(4, 8) : clean;
}

function normalizeActions(list: unknown): Array<GadgetActionDecl & { risk: Risk }> {
  if (!Array.isArray(list)) return [];
  const out: Array<GadgetActionDecl & { risk: Risk }> = [];
  for (const a of list) {
    if (out.length >= ACTIONS_MAX) break;
    if (!isObject(a) || typeof a.name !== "string" || !ACTION_NAME_RE.test(a.name)) continue;
    if (typeof a.description !== "string" || [...a.description].length > ACTION_DESCRIPTION_MAX) continue;
    if (!isObject(a.params) || Buffer.byteLength(JSON.stringify(a.params), "utf8") > ACTION_PARAMS_MAX_BYTES) continue;
    out.push({ name: a.name, description: a.description, params: a.params, risk: a.risk === "safe" ? "safe" : "confirm" });
  }
  return out;
}

/** Rule 1 on `hello`: proto, and a canonical 65-byte SEC1 pubkey that hashes to id. */
export function checkHello(msg: Record<string, unknown>): HelloCheck {
  if (msg.proto !== PROTO_VERSION) return { ok: false, code: "proto_unsupported", message: `proto ${String(msg.proto)} is not supported` };
  const pub = typeof msg.pubkey === "string" ? b64DecodeCanonical(msg.pubkey) : null;
  if (!pub || pub.length !== 65 || pub[0] !== 0x04) {
    return { ok: false, code: "bad_sig", message: "pubkey must be canonical base64 of a 65-byte SEC1 point" };
  }
  const id = gadgetIdFromPubkey(pub);
  if (msg.id !== id) return { ok: false, code: "bad_sig", message: "id does not match pubkey" };
  if (typeof msg.board !== "string" || !BOARD_ID_RE.test(msg.board)) return { ok: false, code: "bad_sig", message: "bad board id" };
  return {
    ok: true,
    hello: {
      proto: PROTO_VERSION, id, pubkey: msg.pubkey as string, pubkeyBytes: pub,
      name: normalizeName(msg.name, id), board: msg.board, fw: typeof msg.fw === "string" ? msg.fw : "",
      caps: isObject(msg.caps) ? (msg.caps as GadgetCaps) : {}, actions: normalizeActions(msg.actions),
      sensors: isObject(msg.sensors) ? (msg.sensors as GadgetSensors) : {},
    },
  };
}

/** A fresh 32-byte nonce per connection; host_name folded and cut to 64 UTF-8 bytes. */
export function makeChallenge(hostId: string, hostName: string): ChallengeMsg {
  return { op: "challenge", nonce: b64Encode(randomBytes(32)), host_id: hostId, host_name: cutUtf8(foldLatin1(hostName), 64) };
}

export type ProveDecision =
  | { ok: true; record: GadgetRecord; enrolled: boolean; sendName: boolean }
  | { ok: false; code: GadgetErrorCode; message: string };

/** Rules 1 (signature) to 4 on `prove`. Mutates state only on success. */
export function decideProve(input: {
  hello: NormalizedHello;
  challenge: ChallengeMsg;
  prove: Record<string, unknown>;
  state: HostState;
  bot: BotRef;
  maxDevices: number;
  now: number;
}): ProveDecision {
  const { hello, challenge, prove, state, now } = input;
  const sig = typeof prove.sig === "string" ? b64DecodeCanonical(prove.sig) : null;
  if (!sig || !verifyP256(hello.pubkeyBytes, proveText(hello.id, challenge.nonce, challenge.host_id), sig)) {
    return { ok: false, code: "bad_sig", message: "signature does not verify" };
  }
  const known = state.gadgets.get(hello.id);
  if (known) {
    if (known.pubkey !== hello.pubkey) return { ok: false, code: "bad_sig", message: "pubkey differs from the enrolled key" };
    const sendName = known.namePending;
    if (!sendName) known.name = hello.name;
    known.namePending = false;
    known.board = hello.board;
    known.fw = hello.fw;
    known.lastSeenAt = now;
    state.save();
    return { ok: true, record: known, enrolled: false, sendName };
  }
  if (typeof prove.enroll !== "string") return { ok: false, code: "enroll_required", message: "this gadget is not paired" };
  const check = state.checkCode(prove.enroll, now);
  if (!check.ok) return { ok: false, code: "bad_code", message: `pairing code ${check.reason}` };
  if (state.gadgets.size >= input.maxDevices) return { ok: false, code: "device_limit", message: "device limit reached" };
  const record: GadgetRecord = {
    id: hello.id, pubkey: hello.pubkey, name: hello.name, board: hello.board, fw: hello.fw,
    bot: { ...input.bot }, settings: { speak_pushes: false }, namePending: false, createdAt: now, lastSeenAt: now,
  };
  state.gadgets.set(record.id, record);
  state.consumeWindow();
  state.save();
  return { ok: true, record, enrolled: true, sendName: false };
}
```

- [ ] **Step 8: Run the tests to see them pass**

Run: `node --test tools/fake-host/test/options.test.ts tools/fake-host/test/enroll.test.ts`
Expected: PASS (13).

- [ ] **Step 9: Commit**

```bash
git add tools/fake-host/src/options.ts tools/fake-host/src/fold.ts tools/fake-host/src/state.ts tools/fake-host/src/enroll.ts tools/fake-host/test/options.test.ts tools/fake-host/test/enroll.test.ts
git commit -m "feat(fake-host): options, Latin-1 fold, host state and enrollment rules" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 8: Fake host WebSocket server, handshake and built-in commands

**Files:**
- Create: `tools/fake-host/test/gadget-client.ts`, `tools/fake-host/test/handshake.test.ts`, `tools/fake-host/src/context.ts`, `tools/fake-host/src/session.ts`, `tools/fake-host/src/server.ts`, `tools/fake-host/src/features.ts`

**Interfaces:**
- Consumes: Task 7's `FakeHostOptions`, `DEFAULT_OPTIONS`, `HostState`, `GadgetRecord`, `checkHello`, `decideProve`, `makeChallenge`, `NormalizedHello`, `foldLatin1`; `protocol/lib` `decodeBinary`, `encodeBinary`, `Kind`, constants.
- Produces:
  - `context.ts`: `HostEvent {event, …}`, `Command {cmd, gadget?, …}`, `Ack {event: "ack", cmd: string | null, ok, error?, …}`, `Script {heard, reply}`, `HostContext {options, state, script, emit(e), log(line), live(gadgetId): GadgetSession | null}`, `CommandCall {cmd, host, gadgetId(), session()}`, `CommandResult`, `CommandHandler`, `Feature {attach(session, host): void; commands: Record<string, CommandHandler>}`.
  - `session.ts`: `class GadgetSession` with `remote`, `host`, `phase`, `hello`, `record`, `sessionId`, `gadgetId: string | null`, `closed: boolean`, `send(msg: HostToGadget): boolean`, `sendBinary(kind: Kind, stream: number, payload: Uint8Array): Promise<boolean>`, `allocStream(): number`, `releaseStream(stream)`, `onOp(op, listener): () => void`, `onBinary(listener): () => void`, `onClose(listener): () => void`, `fail(code: GadgetErrorCode, message: string)`, `close(code?, reason?)`, `terminate()`.
  - `server.ts`: `interface FakeHost {port, hostId, state, script, on(listener): () => void, command(cmd: Command): Promise<Ack>, close(): Promise<void>}`; `interface StartOptions {features?: Feature[]; listener?}`; `startFakeHost(partial?: Partial<FakeHostOptions>, start?: StartOptions): Promise<FakeHost>`. It emits `listening` and then `code` before resolving. Built-in commands: `code`, `revoke`, `replace`, `drop`, `close`, `quit`.
  - `features.ts`: `FEATURES: Feature[]` (empty in this task).
  - `test/gadget-client.ts`: `CAPS`, `randomKey()`, `GadgetOptions`, `BinaryFrame`, `class TestGadget {id, key, pub, inbox, binInbox, all, closed, pings, send, sendRaw, sendBinary, next(op, where?, timeoutMs?), nextOf(ops, where?, timeoutMs?), nextBinary(kind?, timeoutMs?), nextPing(timeoutMs?), ops(), close(code?)}`, `openSocket(port, key?)`, `helloFor(g, o)`, `connectGadget(o): Promise<{gadget, challenge, result}>`, `nextEvent(host, match, timeoutMs?)`, `tryUpgrade(port, opts)`, `delay(ms)`.

- [ ] **Step 1: Create the test helper `tools/fake-host/test/gadget-client.ts`**

```ts
// SPDX-License-Identifier: Apache-2.0
// The gadget side of openmausbot-gadget/1, for the fake host's tests: handshake with node:crypto
// signatures, and an inbox that tests wait on (no sleeps).
import { Buffer } from "node:buffer";
import { generateKeyPairSync } from "node:crypto";
import { WebSocket } from "ws";
import { b64Encode } from "../../../protocol/lib/encoding.ts";
import { encodeBinary, decodeBinary, type Kind } from "../../../protocol/lib/frames.ts";
import { gadgetIdFromPubkey, proveText } from "../../../protocol/lib/identity.ts";
import { publicKeyFromPrivate, signP256 } from "../../../protocol/lib/verify.ts";
import type { GadgetCaps } from "../../../protocol/lib/types.ts";
import type { FakeHost } from "../src/server.ts";
import type { HostEvent } from "../src/context.ts";

export const CAPS: GadgetCaps = {
  screen: { w: 466, h: 466, round: true, text: "latin1" }, image: { w: 300, h: 300 },
  mic: { rate: 16000 }, speaker: { rate: 16000 }, input: ["touch", "talk", "cancel"], battery: true, ota: { max: 6291456 },
};

export function randomKey(): string {
  const { privateKey } = generateKeyPairSync("ec", { namedCurve: "prime256v1" });
  return Buffer.from(privateKey.export({ format: "jwk" }).d as string, "base64url").toString("hex").padStart(64, "0");
}

export interface GadgetOptions {
  port: number;
  key?: string;               // 64 hex; random when absent
  enroll?: string;            // pairing code sent in prove
  name?: string;
  board?: string;
  fw?: string;
  caps?: GadgetCaps;
  actions?: unknown[];
  proto?: number;
}

export interface BinaryFrame { kind: Kind; stream: number; payload: Uint8Array }
type Msg = Record<string, any>;
interface Waiter<T> { match: (x: T) => boolean; resolve: (x: T) => void }

export class TestGadget {
  readonly ws: WebSocket;
  readonly key: string;
  readonly pub: Uint8Array;
  readonly id: string;
  readonly inbox: Msg[] = [];
  readonly binInbox: BinaryFrame[] = [];
  readonly all: Msg[] = [];          // every text frame received, in order
  readonly closed: Promise<{ code: number; reason: string }>;
  pings = 0;
  private readonly waiters: Waiter<Msg>[] = [];
  private readonly binWaiters: Waiter<BinaryFrame>[] = [];
  private pingWaiters: Array<() => void> = [];

  constructor(ws: WebSocket, key: string) {
    this.ws = ws;
    this.key = key;
    this.pub = publicKeyFromPrivate(key);
    this.id = gadgetIdFromPubkey(this.pub);
    ws.on("message", (data: Buffer, isBinary: boolean) => {
      if (isBinary) {
        const f = decodeBinary(new Uint8Array(data));
        if (!f) return;
        const w = this.binWaiters.findIndex((x) => x.match(f));
        if (w >= 0) this.binWaiters.splice(w, 1)[0].resolve(f);
        else this.binInbox.push(f);
        return;
      }
      const msg = JSON.parse(data.toString("utf8")) as Msg;
      this.all.push(msg);
      const w = this.waiters.findIndex((x) => x.match(msg));
      if (w >= 0) this.waiters.splice(w, 1)[0].resolve(msg);
      else this.inbox.push(msg);
    });
    ws.on("ping", () => {
      this.pings++;
      const waiting = this.pingWaiters;
      this.pingWaiters = [];
      for (const w of waiting) w();
    });
    this.closed = new Promise((resolve) => ws.on("close", (code: number, reason: Buffer) => resolve({ code, reason: reason.toString() })));
  }

  send(msg: Msg): void {
    this.ws.send(JSON.stringify(msg));
  }
  sendRaw(data: string | Uint8Array): void {
    this.ws.send(data);
  }
  sendBinary(kind: Kind, stream: number, payload: Uint8Array): void {
    this.ws.send(encodeBinary(kind, stream, payload));
  }

  /** The first unread text frame with this op (and matching `where`), waiting up to timeoutMs. */
  next(op: string, where: (m: Msg) => boolean = () => true, timeoutMs = 3000): Promise<Msg> {
    return this.nextOf([op], where, timeoutMs);
  }

  /** The first unread text frame whose op is one of `ops`. */
  nextOf(ops: string[], where: (m: Msg) => boolean = () => true, timeoutMs = 3000): Promise<Msg> {
    const match = (m: Msg): boolean => ops.includes(m.op) && where(m);
    const i = this.inbox.findIndex(match);
    if (i >= 0) return Promise.resolve(this.inbox.splice(i, 1)[0]);
    return new Promise((resolve, reject) => {
      const waiter: Waiter<Msg> = { match, resolve: (m) => { clearTimeout(t); resolve(m); } };
      const t = setTimeout(() => {
        this.waiters.splice(this.waiters.indexOf(waiter), 1);
        reject(new Error(`timed out waiting for ${ops.join("|")}; inbox: ${JSON.stringify(this.inbox.map((m) => m.op))}`));
      }, timeoutMs);
      this.waiters.push(waiter);
    });
  }

  nextBinary(kind?: Kind, timeoutMs = 3000): Promise<BinaryFrame> {
    const match = (f: BinaryFrame): boolean => kind === undefined || f.kind === kind;
    const i = this.binInbox.findIndex(match);
    if (i >= 0) return Promise.resolve(this.binInbox.splice(i, 1)[0]);
    return new Promise((resolve, reject) => {
      const waiter: Waiter<BinaryFrame> = { match, resolve: (f) => { clearTimeout(t); resolve(f); } };
      const t = setTimeout(() => {
        this.binWaiters.splice(this.binWaiters.indexOf(waiter), 1);
        reject(new Error(`timed out waiting for a binary frame of kind ${kind}`));
      }, timeoutMs);
      this.binWaiters.push(waiter);
    });
  }

  /** Resolves on the next WebSocket ping from the host. */
  nextPing(timeoutMs = 3000): Promise<void> {
    return new Promise((resolve, reject) => {
      const t = setTimeout(() => reject(new Error("timed out waiting for a ping")), timeoutMs);
      this.pingWaiters.push(() => {
        clearTimeout(t);
        resolve();
      });
    });
  }

  /** The ops received so far, in order. */
  ops(): string[] {
    return this.all.map((m) => m.op);
  }

  close(code = 1000): Promise<{ code: number; reason: string }> {
    this.ws.close(code);
    return this.closed;
  }
}

/** Opens the socket (no handshake). */
export function openSocket(port: number, key = randomKey()): Promise<TestGadget> {
  return new Promise((resolve, reject) => {
    const ws = new WebSocket(`ws://127.0.0.1:${port}/gadget`, "openmausbot-gadget.1");
    const g = new TestGadget(ws, key);
    ws.once("open", () => resolve(g));
    ws.once("error", reject);
  });
}

export function helloFor(g: TestGadget, o: GadgetOptions): Msg {
  return {
    op: "hello", proto: o.proto ?? 1, id: g.id, pubkey: b64Encode(g.pub), name: o.name ?? "Test Maus",
    board: o.board ?? "amoled-175c", fw: o.fw ?? "1.0.0", caps: o.caps ?? CAPS, actions: o.actions ?? [],
    sensors: { battery_pct: 82, charging: false },
  };
}

/** Opens a socket and runs hello / challenge / prove. Resolves with the gadget and the host's
 *  answer to prove: the `ready` frame, or the `error` frame. */
export async function connectGadget(o: GadgetOptions): Promise<{ gadget: TestGadget; challenge: Msg; result: Msg }> {
  const gadget = await openSocket(o.port, o.key ?? randomKey());
  gadget.send(helloFor(gadget, o));
  const first = await gadget.nextOf(["challenge", "error"]);
  if (first.op === "error") return { gadget, challenge: {}, result: first };
  const sig = b64Encode(signP256(gadget.key, proveText(gadget.id, first.nonce, first.host_id)));
  gadget.send(o.enroll === undefined ? { op: "prove", sig } : { op: "prove", sig, enroll: o.enroll });
  const result = await gadget.nextOf(["ready", "error"]);
  return { gadget, challenge: first, result };
}

/** Waits for the first host event matching `match`, recorded from now on. */
export function nextEvent(host: FakeHost, match: (e: HostEvent) => boolean, timeoutMs = 3000): Promise<HostEvent> {
  return new Promise((resolve, reject) => {
    const off = host.on((e) => {
      if (!match(e)) return;
      off();
      clearTimeout(t);
      resolve(e);
    });
    const t = setTimeout(() => {
      off();
      reject(new Error("timed out waiting for a host event"));
    }, timeoutMs);
  });
}

/** Tries an upgrade and reports the HTTP status, or "open" with the negotiated extensions. */
export function tryUpgrade(port: number, opts: { path?: string; protocol?: string | null; headers?: Record<string, string>; deflate?: boolean }): Promise<string> {
  return new Promise((resolve) => {
    const protocols = opts.protocol === null ? undefined : (opts.protocol ?? "openmausbot-gadget.1");
    const ws = new WebSocket(`ws://127.0.0.1:${port}${opts.path ?? "/gadget"}`, protocols, {
      headers: opts.headers, perMessageDeflate: opts.deflate ?? false,
    });
    ws.on("unexpected-response", (_req: unknown, res: { statusCode: number }) => resolve(`status ${res.statusCode}`));
    ws.on("error", (e: Error) => resolve(`error ${e.message}`));
    ws.on("open", () => {
      resolve(`open ext=${JSON.stringify(ws.extensions)}`);
      ws.close();
    });
  });
}

/** For the few negative checks that must let time pass ("nothing more arrives"). */
export const delay = (ms: number): Promise<void> => new Promise((r) => setTimeout(r, ms));
```

- [ ] **Step 2: Write the failing test `tools/fake-host/test/handshake.test.ts`**

```ts
// SPDX-License-Identifier: Apache-2.0
import { test, type TestContext } from "node:test";
import assert from "node:assert/strict";
import { Buffer } from "node:buffer";
import net from "node:net";
import { mkdtempSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { b64Encode } from "../../../protocol/lib/encoding.ts";
import { proveText } from "../../../protocol/lib/identity.ts";
import { signP256 } from "../../../protocol/lib/verify.ts";
import { startFakeHost, type FakeHost } from "../src/server.ts";
import type { FakeHostOptions } from "../src/options.ts";
import type { HostEvent } from "../src/context.ts";
import { connectGadget, delay, helloFor, nextEvent, openSocket, randomKey, tryUpgrade } from "./gadget-client.ts";

async function host(t: TestContext, o: Partial<FakeHostOptions> = {}): Promise<{ h: FakeHost; events: HostEvent[] }> {
  const events: HostEvent[] = [];
  const h = await startFakeHost({ port: 0, quiet: true, code: "123456", ...o }, { listener: (e) => events.push(e) });
  t.after(() => h.close());
  return { h, events };
}

test("upgrades: Origin 403, no subprotocol 400, other paths 404, bad version 426, no extensions", async (t) => {
  const { h } = await host(t);
  assert.equal(await tryUpgrade(h.port, { headers: { Origin: "http://example.test" } }), "status 403");
  assert.equal(await tryUpgrade(h.port, { protocol: null }), "status 400");
  assert.equal(await tryUpgrade(h.port, { protocol: "other.1" }), "status 400");
  assert.equal(await tryUpgrade(h.port, { path: "/other" }), "status 404");
  assert.equal(await tryUpgrade(h.port, { deflate: true }), 'open ext=""');
  const raw = await new Promise<string>((resolve) => {
    const s = net.connect(h.port, "127.0.0.1", () => {
      s.write("GET /gadget HTTP/1.1\r\nHost: x\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
        + "Sec-WebSocket-Version: 8\r\nSec-WebSocket-Protocol: openmausbot-gadget.1\r\n\r\n");
    });
    let buf = "";
    s.on("data", (d) => (buf += d.toString()));
    s.on("close", () => resolve(buf));
  });
  assert.match(raw, /^HTTP\/1\.1 426 /);
  assert.match(raw, /Sec-WebSocket-Version: 13/);
});

test("the listening and code events come first", async (t) => {
  const { h, events } = await host(t);
  assert.deepEqual(events.slice(0, 2).map((e) => e.event), ["listening", "code"]);
  assert.equal(events[0].port, h.port);
  assert.match(String(events[0].host_id), /^[0-9a-f]{32}$/);
  assert.equal(events[1].code, "123456");
});

test("enrolls with the code, then reconnects as a known gadget without one", async (t) => {
  const { h, events } = await host(t, { hostName: "Desk Mac" });
  const key = randomKey();
  const { gadget, challenge, result } = await connectGadget({ port: h.port, key, enroll: "123456" });
  assert.equal(challenge.host_id, h.hostId);
  assert.equal(challenge.host_name, "Desk Mac");
  assert.equal(challenge.nonce.length, 44);
  assert.equal(result.op, "ready");
  assert.match(result.session, /^s_[0-9a-f]{12}$/);
  assert.deepEqual(result.bot, { id: "b_fake", name: "Fake Bot" });
  assert.deepEqual(result.settings, { speak_pushes: false });
  assert.ok(events.some((e) => e.event === "enrolled" && e.gadget === gadget.id));
  assert.ok(events.some((e) => e.event === "ready" && e.gadget === gadget.id && e.session === result.session));
  assert.ok(events.some((e) => e.event === "rx" && e.gadget === null && String(e.msg).includes('"op":"hello"')));
  await gadget.close();
  const again = await connectGadget({ port: h.port, key, enroll: "999999" });
  assert.equal(again.result.op, "ready");
  assert.notEqual(again.challenge.nonce, challenge.nonce);
  assert.notEqual(again.result.session, result.session);
});

test("refusals: enroll_required, bad_code (wrong / used up), proto_unsupported, bad_sig; then close 1000", async (t) => {
  const { h, events } = await host(t);
  const none = await connectGadget({ port: h.port });
  assert.deepEqual([none.result.op, none.result.code], ["error", "enroll_required"]);
  assert.equal((await none.gadget.closed).code, 1000);
  assert.ok(events.some((e) => e.event === "refused" && e.code === "enroll_required"));
  const wrong = await connectGadget({ port: h.port, enroll: "000000" });
  assert.deepEqual([wrong.result.code, wrong.result.message], ["bad_code", "pairing code wrong"]);
  assert.equal((await connectGadget({ port: h.port, enroll: "123456" })).result.op, "ready");
  const used = await connectGadget({ port: h.port, enroll: "123456" });
  assert.deepEqual([used.result.code, used.result.message], ["bad_code", "pairing code used up"]);
  assert.equal((await connectGadget({ port: h.port, enroll: "123456", proto: 2 })).result.code, "proto_unsupported");
  const g = await openSocket(h.port);
  g.send({ ...helloFor(g, { port: h.port }), id: "gad_0000000000000000" });
  assert.equal((await g.next("error")).code, "bad_sig");
});

test("a device cap of 0 answers device_limit and keeps the window open", async (t) => {
  const { h } = await host(t, { maxDevices: 0 });
  assert.equal((await connectGadget({ port: h.port, enroll: "123456" })).result.code, "device_limit");
  assert.equal(h.state.window!.used, false);
});

test("a second connection with the same key replaces the first only after its prove", async (t) => {
  const { h } = await host(t);
  const key = randomKey();
  const first = await connectGadget({ port: h.port, key, enroll: "123456" });
  const intruder = await openSocket(h.port, randomKey());
  intruder.send({ ...helloFor(intruder, { port: h.port }), id: first.gadget.id });
  assert.equal((await intruder.next("error")).code, "bad_sig");
  // A forger replays the victim's public hello (same id and pubkey) but cannot sign its prove.
  const forger = await openSocket(h.port, key);
  forger.send(helloFor(forger, { port: h.port }));
  const ch = await forger.next("challenge");
  forger.send({ op: "prove", sig: b64Encode(signP256(randomKey(), proveText(forger.id, ch.nonce, ch.host_id))) });
  assert.equal((await forger.next("error")).code, "bad_sig");
  // The old session is still the live one: the close command reaches it, and it never saw an error.
  assert.equal((await h.command({ cmd: "close", gadget: first.gadget.id, code: 4002 })).ok, true);
  assert.equal((await first.gadget.closed).code, 4002, "the old session stays untouched until a prove verifies");
  assert.ok(!first.gadget.all.some((m) => m.op === "error"), first.gadget.ops().join(" "));
  const back = await connectGadget({ port: h.port, key });
  assert.equal(back.result.op, "ready");
  const second = await connectGadget({ port: h.port, key });
  assert.equal(second.result.op, "ready");
  const err = await back.gadget.next("error");
  assert.deepEqual([err.code, (await back.gadget.closed).code], ["replaced", 1000]);
});

test("revoke sends error revoked, forgets the gadget, and replace / drop / close act on the live socket", async (t) => {
  const { h } = await host(t);
  const key = randomKey();
  const a = await connectGadget({ port: h.port, key, enroll: "123456" });
  assert.deepEqual(await h.command({ cmd: "revoke" }), { event: "ack", cmd: "revoke", ok: true });
  assert.equal((await a.gadget.next("error")).code, "revoked");
  assert.equal((await connectGadget({ port: h.port, key })).result.code, "enroll_required");
  assert.equal((await h.command({ cmd: "code", code: "222222" })).code, "222222");
  const b = await connectGadget({ port: h.port, key, enroll: "222222" });
  assert.equal(b.result.op, "ready");
  await h.command({ cmd: "replace" });
  assert.equal((await b.gadget.next("error")).code, "replaced");
  const c = await connectGadget({ port: h.port, key });
  await h.command({ cmd: "close", code: 4000 });
  assert.equal((await c.gadget.closed).code, 4000);
  const d = await connectGadget({ port: h.port, key });
  await h.command({ cmd: "drop" });
  assert.equal((await d.gadget.closed).code, 1006);
  const ack = await h.command({ cmd: "drop" });
  assert.deepEqual([ack.ok, ack.error], [false, `gadget ${d.gadget.id} is not connected`]);
});

test("commands: unknown names, a missing gadget and bad codes are refused with an ack", async (t) => {
  const { h } = await host(t);
  assert.deepEqual(await h.command({ cmd: "nope" }), { event: "ack", cmd: "nope", ok: false, error: "unknown command nope" });
  assert.equal((await h.command({ cmd: "drop" })).error, "no gadget has connected yet");
  assert.equal((await h.command({ cmd: "code", code: "12" })).error, "code must be six digits");
});

test("ops before ready, unknown ops and invalid JSON are ignored", async (t) => {
  const { h } = await host(t);
  const g = await openSocket(h.port);
  g.send({ op: "say", turn: "t00000000-1", text: "too early" });
  g.sendRaw("not json");
  g.send(helloFor(g, { port: h.port }));
  assert.equal((await g.next("challenge")).op, "challenge");
  const { gadget } = await connectGadget({ port: h.port, enroll: "123456" });
  const lastJunk = nextEvent(h, (e) => e.event === "rx" && e.msg === '{"op": 5}');
  gadget.send({ op: "nope", x: 1 });
  gadget.sendRaw("[1,2]");
  gadget.sendRaw('{"op": 5}');
  await lastJunk;
  await h.command({ cmd: "revoke" });
  assert.equal((await gadget.next("error")).code, "revoked", "the session survived the junk");
});

test("timing: handshake deadline closes 1008, pings keep a gadget alive, and an idle gadget is dropped", async (t) => {
  const { h, events } = await host(t, { handshakeMs: 100, pingMs: 30, idleMs: 400 });
  const silent = await openSocket(h.port);
  assert.equal((await silent.closed).code, 1008);
  const { gadget } = await connectGadget({ port: h.port, enroll: "123456" });
  await gadget.nextPing();
  await gadget.nextPing();
  await delay(600);
  assert.ok(!events.some((e) => e.event === "closed" && e.gadget === gadget.id), "a gadget that answers pings is never idle");
  // Stop reading: the client no longer answers pings, so the host sees no inbound frame.
  const ws = gadget.ws as unknown as { _socket: net.Socket };
  ws._socket.pause();
  const closed = await nextEvent(h, (e) => e.event === "closed" && e.gadget === gadget.id, 3000);
  assert.equal(closed.event, "closed");
});

test("frame limits: text over 16 KiB and binary over 8 KiB close with 1009, invalid UTF-8 with 1007", async (t) => {
  const { h } = await host(t);
  const a = await connectGadget({ port: h.port, enroll: "123456" });
  a.gadget.sendRaw(JSON.stringify({ op: "event", name: "x", data: "y".repeat(17000) }));
  assert.equal((await a.gadget.closed).code, 1009);
  const b = await connectGadget({ port: h.port, key: a.gadget.key });
  b.gadget.sendRaw(new Uint8Array(8193).fill(1));
  assert.equal((await b.gadget.closed).code, 1009);
  const c = await connectGadget({ port: h.port, key: a.gadget.key });
  c.gadget.ws.send(Buffer.from([0x7b, 0xff, 0xfe, 0x7d]), { binary: false });
  assert.equal((await c.gadget.closed).code, 1007);
});

test("--state keeps host_id and enrolled gadgets across restarts", async (t) => {
  const dir = mkdtempSync(join(tmpdir(), "fake-host-state-"));
  const key = randomKey();
  const one = await startFakeHost({ port: 0, quiet: true, code: "123456", stateDir: dir });
  const first = await connectGadget({ port: one.port, key, enroll: "123456" });
  assert.equal(first.result.op, "ready");
  await one.close();
  const two = await startFakeHost({ port: 0, quiet: true, stateDir: dir });
  t.after(() => two.close());
  assert.equal(two.hostId, one.hostId);
  assert.equal((await connectGadget({ port: two.port, key })).result.op, "ready");
});
```

- [ ] **Step 3: Run it to see it fail**

Run: `node --test tools/fake-host/test/handshake.test.ts`
Expected: FAIL with `ERR_MODULE_NOT_FOUND` for `tools/fake-host/src/server.ts`.

- [ ] **Step 4: Create `tools/fake-host/src/context.ts`**

```ts
// SPDX-License-Identifier: Apache-2.0
// Types shared by the fake host's server, sessions and features.
import type { FakeHostOptions } from "./options.ts";
import type { HostState } from "./state.ts";
import type { GadgetSession } from "./session.ts";

/** One JSON line on stdout. */
export interface HostEvent { event: string; [key: string]: unknown }
/** One JSON line on stdin. */
export interface Command { cmd: string; gadget?: string; [key: string]: unknown }
export interface Ack { event: "ack"; cmd: string | null; ok: boolean; error?: string; [key: string]: unknown }
/** The scripted speech-to-text result and reply; the `heard` and `reply` commands change them. */
export interface Script { heard: string; reply: string }

export interface HostContext {
  readonly options: FakeHostOptions;
  readonly state: HostState;
  readonly script: Script;
  emit(event: HostEvent): void;
  log(line: string): void;
  /** The live (ready) session of a gadget, or null. */
  live(gadgetId: string): GadgetSession | null;
}

export interface CommandCall {
  readonly cmd: Command;
  readonly host: HostContext;
  /** cmd.gadget, else the most recently ready gadget. Throws when there is none. */
  gadgetId(): string;
  /** The target's live session. Throws "gadget <id> is not connected". */
  session(): GadgetSession;
}
export type CommandResult = Record<string, unknown> | void;
export type CommandHandler = (call: CommandCall) => CommandResult | Promise<CommandResult>;

/** A slice of host behaviour (voice, display, OTA). server.ts attaches every feature to each
 *  session right after `ready`, and routes commands by name. */
export interface Feature {
  attach(session: GadgetSession, host: HostContext): void;
  commands: Record<string, CommandHandler>;
}
```

- [ ] **Step 5: Create `tools/fake-host/src/session.ts`**

```ts
// SPDX-License-Identifier: Apache-2.0
// One gadget connection: liveness, the §4.3 handshake, and op/binary dispatch after `ready`.
import { Buffer } from "node:buffer";
import { randomBytes } from "node:crypto";
import { decodeBinary, encodeBinary, type Kind } from "../../../protocol/lib/frames.ts";
import type { ChallengeMsg, GadgetErrorCode, HostToGadget } from "../../../protocol/lib/types.ts";
import { BINARY_FRAME_MAX, TEXT_FRAME_MAX } from "../../../protocol/lib/types.ts";
import type { HostContext } from "./context.ts";
import { checkHello, decideProve, makeChallenge, type NormalizedHello } from "./enroll.ts";
import { foldLatin1 } from "./fold.ts";
import type { GadgetRecord } from "./state.ts";

/** The parts of a `ws` WebSocket the session uses. */
export interface WsLike {
  send(data: string | Uint8Array, cb?: (err?: Error) => void): void;
  close(code?: number, reason?: string): void;
  terminate(): void;
  ping(): void;
  on(event: string, listener: (...args: any[]) => void): void;
  readonly bufferedAmount: number;
}

export interface SessionHooks {
  /** prove verified: replace any older session with this id and make this one live. */
  takeOver(session: GadgetSession): void;
  /** ready (and any pending settings) sent: attach features. */
  attach(session: GadgetSession): void;
  closed(session: GadgetSession, code: number): void;
}

type OpListener = (msg: any) => void;
type BinaryListener = (kind: Kind, stream: number, payload: Uint8Array) => void;

const SEND_BUFFER_MAX = 256 * 1024;

export class GadgetSession {
  readonly remote: string;
  readonly host: HostContext;
  phase: "hello" | "challenged" | "ready" | "closed" = "hello";
  hello: NormalizedHello | null = null;
  record: GadgetRecord | null = null;
  sessionId = "";
  private readonly ws: WsLike;
  private readonly hooks: SessionHooks;
  private challenge: ChallengeMsg | null = null;
  private readonly ops = new Map<string, Set<OpListener>>();
  private readonly binaries = new Set<BinaryListener>();
  private readonly closers = new Set<(code: number) => void>();
  private readonly streams = new Set<number>();
  private nextStream = 1;
  private readonly pingTimer: NodeJS.Timeout;
  private readonly idleTimer: NodeJS.Timeout;
  private readonly handshakeTimer: NodeJS.Timeout;

  constructor(ws: WsLike, remote: string, host: HostContext, hooks: SessionHooks) {
    this.ws = ws;
    this.remote = remote;
    this.host = host;
    this.hooks = hooks;
    const o = host.options;
    this.pingTimer = setInterval(() => this.ws.ping(), o.pingMs);
    this.idleTimer = setTimeout(() => {
      this.host.log(`${this.label()}: no inbound frame for ${o.idleMs} ms, dropping`);
      this.terminate();
    }, o.idleMs);
    this.handshakeTimer = setTimeout(() => {
      if (this.phase !== "ready") this.close(1008, "handshake timeout");
    }, o.handshakeMs);
    ws.on("message", (data: Buffer, isBinary: boolean) => {
      this.idleTimer.refresh();
      if (isBinary) this.onBinaryFrame(new Uint8Array(data.buffer, data.byteOffset, data.byteLength));
      else this.onText(data.toString("utf8"));
    });
    ws.on("ping", () => this.idleTimer.refresh());
    ws.on("pong", () => this.idleTimer.refresh());
    ws.on("error", (err: Error) => this.host.log(`${this.label()}: ${err.message}`));
    ws.on("close", (code: number) => this.onClosed(code));
  }

  get gadgetId(): string | null {
    return this.phase === "ready" && this.record ? this.record.id : null;
  }
  get closed(): boolean {
    return this.phase === "closed";
  }
  private label(): string {
    return this.record?.id ?? this.hello?.id ?? this.remote;
  }

  /** Sends one text frame. Strings for the screen must already be folded. */
  send(msg: HostToGadget): boolean {
    if (this.phase === "closed") return false;
    const text = JSON.stringify(msg);
    if (Buffer.byteLength(text, "utf8") > TEXT_FRAME_MAX) {
      this.host.log(`${this.label()}: refusing to send a ${msg.op} frame over 16 KiB`);
      return false;
    }
    this.ws.send(text);
    this.host.emit({ event: "tx", gadget: this.gadgetId, msg: text });
    return true;
  }

  /** Sends one binary frame and resolves once it is written to the socket (false if closed). */
  sendBinary(kind: Kind, stream: number, payload: Uint8Array): Promise<boolean> {
    if (this.phase === "closed") return Promise.resolve(false);
    if (this.ws.bufferedAmount > SEND_BUFFER_MAX) {
      this.close(1008, "send buffer full");
      return Promise.resolve(false);
    }
    const frame = encodeBinary(kind, stream, payload);
    return new Promise((resolve) => this.ws.send(frame, (err) => resolve(!err)));
  }

  /** A host-assigned stream id (1–255) that is not in use. */
  allocStream(): number {
    for (let i = 0; i < 255; i++) {
      const s = this.nextStream;
      this.nextStream = s === 255 ? 1 : s + 1;
      if (!this.streams.has(s)) {
        this.streams.add(s);
        return s;
      }
    }
    throw new Error("all 255 host streams are in use");
  }
  releaseStream(stream: number): void {
    this.streams.delete(stream);
  }

  onOp(op: string, listener: OpListener): () => void {
    const set = this.ops.get(op) ?? new Set<OpListener>();
    set.add(listener);
    this.ops.set(op, set);
    return () => set.delete(listener);
  }
  onBinary(listener: BinaryListener): () => void {
    this.binaries.add(listener);
    return () => this.binaries.delete(listener);
  }
  onClose(listener: (code: number) => void): () => void {
    this.closers.add(listener);
    return () => this.closers.delete(listener);
  }

  /** Sends `error {code, message}` and closes with 1000 (PROTOCOL.md §4.3). */
  fail(code: GadgetErrorCode, message: string): void {
    if (this.phase === "closed") return;
    this.ws.send(JSON.stringify({ op: "error", code, message }));
    this.host.emit({ event: "tx", gadget: this.gadgetId, msg: JSON.stringify({ op: "error", code, message }) });
    this.close(1000, code);
  }
  close(code = 1000, reason = ""): void {
    if (this.phase !== "closed") this.ws.close(code, reason);
  }
  terminate(): void {
    this.ws.terminate();
  }

  private onText(text: string): void {
    this.host.emit({ event: "rx", gadget: this.gadgetId, msg: text });
    let msg: unknown;
    try {
      msg = JSON.parse(text);
    } catch {
      return;
    }
    if (typeof msg !== "object" || msg === null || Array.isArray(msg)) return;
    const m = msg as Record<string, unknown>;
    if (typeof m.op !== "string") return;
    if (this.phase === "hello") {
      if (m.op === "hello") this.onHello(m);
    } else if (this.phase === "challenged") {
      if (m.op === "prove") this.onProve(m);
    } else if (this.phase === "ready") {
      for (const listener of [...(this.ops.get(m.op) ?? [])]) listener(m);
    }
  }

  private refuse(code: GadgetErrorCode, message: string): void {
    this.host.emit({ event: "refused", gadget: this.hello?.id ?? null, code, message });
    this.host.log(`${this.label()}: refused ${code} (${message})`);
    this.fail(code, message);
  }

  private onHello(m: Record<string, unknown>): void {
    const r = checkHello(m);
    if (!r.ok) return this.refuse(r.code, r.message);
    this.hello = r.hello;
    this.challenge = makeChallenge(this.host.state.hostId, this.host.state.hostName);
    this.phase = "challenged";
    this.send(this.challenge);
  }

  private onProve(m: Record<string, unknown>): void {
    const d = decideProve({
      hello: this.hello!, challenge: this.challenge!, prove: m, state: this.host.state,
      bot: this.host.options.bot, maxDevices: this.host.options.maxDevices, now: Date.now(),
    });
    if (!d.ok) return this.refuse(d.code, d.message);
    clearTimeout(this.handshakeTimer);
    this.record = d.record;
    this.sessionId = "s_" + randomBytes(6).toString("hex");
    this.phase = "ready";
    if (d.enrolled) this.host.emit({ event: "enrolled", gadget: d.record.id });
    this.hooks.takeOver(this);
    const bot = { id: d.record.bot.id, name: foldLatin1(d.record.bot.name) };
    this.send({ op: "ready", session: this.sessionId, bot, settings: { ...d.record.settings } });
    if (d.sendName) this.send({ op: "settings", bot, settings: { ...d.record.settings }, name: foldLatin1(d.record.name) });
    this.host.emit({ event: "ready", gadget: d.record.id, session: this.sessionId });
    this.host.log(`${d.record.id} ready (${d.record.name}, ${d.record.board}, fw ${d.record.fw})`);
    this.hooks.attach(this);
  }

  private onBinaryFrame(frame: Uint8Array): void {
    if (frame.length > BINARY_FRAME_MAX) return this.close(1009, "binary frame over 8 KiB");
    const d = decodeBinary(frame);
    if (!d) return;
    this.host.emit({ event: "rx_binary", gadget: this.gadgetId, kind: d.kind, stream: d.stream, bytes: d.payload.length });
    if (this.phase !== "ready") return;
    for (const listener of [...this.binaries]) listener(d.kind, d.stream, d.payload);
  }

  private onClosed(code: number): void {
    if (this.phase === "closed") return;
    this.phase = "closed";
    clearInterval(this.pingTimer);
    clearTimeout(this.idleTimer);
    clearTimeout(this.handshakeTimer);
    for (const listener of [...this.closers]) listener(code);
    this.hooks.closed(this, code);
  }
}
```

- [ ] **Step 6: Create `tools/fake-host/src/features.ts`** (empty list; Tasks 9–11 add to it)

```ts
// SPDX-License-Identifier: Apache-2.0
// The features every fake host runs. Later tasks add voice, display and OTA here.
import type { Feature } from "./context.ts";

export const FEATURES: Feature[] = [];
```

- [ ] **Step 7: Create `tools/fake-host/src/server.ts`**

```ts
// SPDX-License-Identifier: Apache-2.0
// The fake host: an HTTP server whose only WebSocket path is /gadget (PROTOCOL.md §4.1), the
// sessions on it, the event stream and the command interface.
import { Buffer } from "node:buffer";
import { createServer, type IncomingMessage } from "node:http";
import type { AddressInfo } from "node:net";
import type { Duplex } from "node:stream";
import { WebSocketServer } from "ws";
import { GADGET_PATH, GADGET_SUBPROTOCOL, PAIR_CODE_RE, TEXT_FRAME_MAX } from "../../../protocol/lib/types.ts";
import type { Ack, Command, CommandCall, CommandResult, Feature, HostContext, HostEvent, Script } from "./context.ts";
import { FEATURES } from "./features.ts";
import { DEFAULT_OPTIONS, type FakeHostOptions } from "./options.ts";
import { GadgetSession } from "./session.ts";
import { HostState } from "./state.ts";

export interface FakeHost {
  readonly port: number;
  readonly hostId: string;
  readonly state: HostState;
  readonly script: Script;
  on(listener: (event: HostEvent) => void): () => void;
  /** Runs one control command; the ack is also emitted as an `ack` event. */
  command(cmd: Command): Promise<Ack>;
  /** Close code 1001 to every gadget, then stop listening. */
  close(): Promise<void>;
}

export interface StartOptions {
  features?: Feature[];
  /** Attached before the first event, so it sees `listening` and `code`. */
  listener?: (event: HostEvent) => void;
}

function refuseUpgrade(socket: Duplex, status: number, text: string, extra = ""): void {
  socket.end(`HTTP/1.1 ${status} ${text}\r\n${extra}Connection: close\r\nContent-Length: 0\r\n\r\n`);
}

export async function startFakeHost(partial: Partial<FakeHostOptions> = {}, start: StartOptions = {}): Promise<FakeHost> {
  const options: FakeHostOptions = { ...DEFAULT_OPTIONS, ...partial };
  const features = start.features ?? FEATURES;
  const state = HostState.load({ stateDir: options.stateDir, hostId: options.hostId, hostName: options.hostName });
  const script: Script = { heard: options.heard, reply: options.reply };
  const listeners = new Set<(event: HostEvent) => void>();
  if (start.listener) listeners.add(start.listener);
  const sessions = new Set<GadgetSession>();
  const live = new Map<string, GadgetSession>();
  let lastReady: string | null = null;

  const ctx: HostContext = {
    options, state, script,
    emit(event) {
      for (const l of [...listeners]) l(event);
    },
    log(line) {
      if (!options.quiet) process.stderr.write(`fake-host: ${line}\n`);
    },
    live: (id) => live.get(id) ?? null,
  };

  const wss = new WebSocketServer({
    noServer: true,
    perMessageDeflate: false,
    maxPayload: TEXT_FRAME_MAX,
    handleProtocols: (protocols: Set<string>) => (protocols.has(GADGET_SUBPROTOCOL) ? GADGET_SUBPROTOCOL : false),
  });
  const http = createServer((_req, res) => {
    res.writeHead(404, { "content-type": "text/plain" }).end("openmausbot fake host: gadgets connect to ws://<host>/gadget\n");
  });
  http.on("upgrade", (req: IncomingMessage, socket: Duplex, head: Buffer) => {
    const path = new URL(req.url ?? "/", "http://fake-host.invalid").pathname;
    if (path !== GADGET_PATH) return refuseUpgrade(socket, 404, "Not Found");
    if (req.headers.origin !== undefined) return refuseUpgrade(socket, 403, "Forbidden");
    if (req.method !== "GET") return refuseUpgrade(socket, 400, "Bad Request");
    if (req.headers["sec-websocket-version"] !== "13") return refuseUpgrade(socket, 426, "Upgrade Required", "Sec-WebSocket-Version: 13\r\n");
    const protocols = String(req.headers["sec-websocket-protocol"] ?? "").split(",").map((p) => p.trim());
    if (!protocols.includes(GADGET_SUBPROTOCOL)) return refuseUpgrade(socket, 400, "Bad Request");
    wss.handleUpgrade(req, socket, head, (ws) => {
      const remote = `${req.socket.remoteAddress}:${req.socket.remotePort}`;
      ctx.emit({ event: "connected", remote });
      const session = new GadgetSession(ws, remote, ctx, {
        takeOver(s) {
          const id = s.record!.id;
          const old = live.get(id);
          if (old && old !== s) old.fail("replaced", "another connection with the same id took over");
          live.set(id, s);
          lastReady = id;
        },
        attach(s) {
          for (const f of features) f.attach(s, ctx);
        },
        closed(s, code) {
          sessions.delete(s);
          const id = s.record?.id ?? null;
          if (id && live.get(id) === s) live.delete(id);
          ctx.emit({ event: "closed", gadget: id, code });
        },
      });
      sessions.add(session);
    });
  });

  await new Promise<void>((resolve, reject) => {
    http.once("error", reject);
    http.listen(options.port, options.bind, () => resolve());
  });
  const port = (http.address() as AddressInfo).port;

  function openWindow(code: string | null): { code: string; expires_at: number } {
    const w = state.openWindow(code, options.codeTtlS, Date.now());
    ctx.emit({ event: "code", code: w.code, expires_at: w.expiresAt });
    ctx.log(`pairing code ${w.code} (valid ${options.codeTtlS} s, 5 attempts, single use)`);
    return { code: w.code, expires_at: w.expiresAt };
  }

  async function close(): Promise<void> {
    for (const s of sessions) s.close(1001, "host shutting down");
    const deadline = Date.now() + 1000;
    while (sessions.size > 0 && Date.now() < deadline) await new Promise((r) => setTimeout(r, 10));
    for (const s of sessions) s.terminate();
    wss.close();
    await new Promise<void>((resolve) => http.close(() => resolve()));
  }

  const builtins: Record<string, (call: CommandCall) => CommandResult | Promise<CommandResult>> = {
    code(call) {
      const c = call.cmd.code;
      if (c !== undefined && (typeof c !== "string" || !PAIR_CODE_RE.test(c))) throw new Error("code must be six digits");
      return openWindow((c as string | undefined) ?? null);
    },
    revoke(call) {
      const id = call.gadgetId();
      if (!state.gadgets.delete(id)) throw new Error(`unknown gadget ${id}`);
      state.save();
      live.get(id)?.fail("revoked", "this gadget was removed");
    },
    replace(call) {
      call.session().fail("replaced", "another connection with the same id took over");
    },
    drop(call) {
      call.session().terminate();
    },
    close(call) {
      const code = call.cmd.code ?? 1000;
      if (typeof code !== "number" || !Number.isInteger(code)) throw new Error("code must be an integer");
      call.session().close(code);
    },
    async quit() {
      await close();
    },
  };

  async function command(cmd: Command): Promise<Ack> {
    const name = typeof cmd?.cmd === "string" ? cmd.cmd : null;
    const call: CommandCall = {
      cmd,
      host: ctx,
      gadgetId() {
        const id = typeof cmd.gadget === "string" ? cmd.gadget : lastReady;
        if (!id) throw new Error("no gadget has connected yet");
        return id;
      },
      session() {
        const id = this.gadgetId();
        const s = live.get(id);
        if (!s) throw new Error(`gadget ${id} is not connected`);
        return s;
      },
    };
    let ack: Ack;
    try {
      if (name === null) throw new Error("missing cmd");
      const handler = builtins[name] ?? features.find((f) => name in f.commands)?.commands[name];
      if (!handler) throw new Error(`unknown command ${name}`);
      const result = await handler(call);
      ack = { event: "ack", cmd: name, ok: true, ...(result ?? {}) };
    } catch (err) {
      ack = { event: "ack", cmd: name, ok: false, error: (err as Error).message };
    }
    ctx.emit(ack);
    return ack;
  }

  ctx.emit({ event: "listening", port, host_id: state.hostId });
  ctx.log(`listening on ws://${options.bind}:${port}${GADGET_PATH} (host_id ${state.hostId})`);
  openWindow(options.code);

  return {
    port,
    hostId: state.hostId,
    state,
    script,
    on(listener) {
      listeners.add(listener);
      return () => listeners.delete(listener);
    },
    command,
    close,
  };
}
```

- [ ] **Step 8: Run the fake-host tests to see them pass**

Run: `npm run test:fake-host`
Expected: PASS (25): options 4, enroll 9, handshake 12.

- [ ] **Step 9: Commit**

```bash
git add tools/fake-host/src/context.ts tools/fake-host/src/session.ts tools/fake-host/src/server.ts tools/fake-host/src/features.ts tools/fake-host/test/gadget-client.ts tools/fake-host/test/handshake.test.ts
git commit -m "feat(fake-host): WebSocket server, handshake and built-in commands" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 9: Scripted voice turns and paced test-tone speech

**Files:**
- Create: `tools/fake-host/test/voice.test.ts`, `tools/fake-host/src/speech.ts`, `tools/fake-host/src/voice.ts`
- Modify: `tools/fake-host/src/features.ts` (replace the file)

**Interfaces:**
- Consumes: `GadgetSession` (`send`, `sendBinary`, `allocStream`, `releaseStream`, `onOp`, `onBinary`, `onClose`, `hello`, `record`, `closed`), `Feature`, `HostContext.script`, `HostContext.options` (`replyIntervalMs`, `toneMs`, `doneBeforeSpeech`), `foldLatin1`; constants `BinaryKind`, `MIC_RATE`, `SPEAK_AHEAD_MS`, `SPEAK_FRAME_MS`, `TEXT_FRAME_MAX`, `UTTERANCE_MAX_MS`.
- Produces:
  - `speech.ts`: `toneSamples(rate: number, from: number, count: number): Uint8Array`; `speakerRate(session): 16000 | 24000 | null`; `class SpeechPlayer {play(turn: string | undefined, ms: number): Promise<boolean>; stopTurn(turn: string): void}`; `speechOf(session): SpeechPlayer`. Reply speech (with a turn) replaces what plays; post speech (no turn) waits. A stream holds its slot until its audio has played out in real time (`play` resolves then), so a post's `speak.begin` never cuts the reply's buffered tail.
  - `voice.ts`: `WORKING_TEXT`, `NOTHING_HEARD`, `BAD_MIC_RATE`, `NO_BOT`, `cumulativeParts(text): [string, string, string]`, `fitReply(turn, text, final): ReplyMsg`, `voiceFeature: Feature` with commands `heard` and `reply` (`{text}`). Events: `turn {gadget, turn, phase, outcome?}`. With `options.doneBeforeSpeech`, `done ok` follows the final reply at once and the speech comes after it (MausBot's usual order); a new turn then sends `speak.stop` for that speech first.

- [ ] **Step 1: Write the failing test `tools/fake-host/test/voice.test.ts`**

```ts
// SPDX-License-Identifier: Apache-2.0
import { test, type TestContext } from "node:test";
import assert from "node:assert/strict";
import { Buffer } from "node:buffer";
import { startFakeHost, type FakeHost } from "../src/server.ts";
import type { FakeHostOptions } from "../src/options.ts";
import type { HostEvent } from "../src/context.ts";
import { cumulativeParts, fitReply } from "../src/voice.ts";
import { toneSamples } from "../src/speech.ts";
import { CAPS, connectGadget, delay, type GadgetOptions, type TestGadget } from "./gadget-client.ts";

async function setup(t: TestContext, o: Partial<FakeHostOptions> = {}, g: Partial<GadgetOptions> = {}): Promise<{ h: FakeHost; gadget: TestGadget; events: HostEvent[] }> {
  const events: HostEvent[] = [];
  const h = await startFakeHost({ port: 0, quiet: true, code: "123456", replyIntervalMs: 20, toneMs: 200, ...o }, { listener: (e) => events.push(e) });
  t.after(() => h.close());
  const { gadget, result } = await connectGadget({ port: h.port, enroll: "123456", ...g });
  assert.equal(result.op, "ready");
  return { h, gadget, events };
}
const mic = (ms: number): Uint8Array => new Uint8Array((16000 * 2 * ms) / 1000);
function talk(g: TestGadget, turn: string, stream = 1): void {
  g.send({ op: "voice.begin", turn, stream, rate: 16000 });
  for (let i = 0; i < 5; i++) g.sendBinary(1, stream, mic(20));
  g.send({ op: "voice.end", turn, ms: 100 });
}

test("a voice turn: heard, working, three cumulative replies, final, paced speech, done ok", async (t) => {
  const { gadget, events } = await setup(t, {}, {});
  talk(gadget, "t0a0b0c0d-1");
  const done = await gadget.next("done");
  assert.deepEqual(done, { op: "done", turn: "t0a0b0c0d-1", outcome: "ok" });
  assert.deepEqual(gadget.ops(), [
    "challenge", "ready", "heard", "working", "reply", "reply", "reply", "reply", "speak.begin", "speak.end", "done",
  ]);
  const by = (op: string) => gadget.all.filter((m) => m.op === op);
  assert.deepEqual(by("heard")[0], { op: "heard", turn: "t0a0b0c0d-1", text: "What's on my calendar today?" });
  assert.deepEqual(by("working")[0], { op: "working", turn: "t0a0b0c0d-1", text: "checking your calendar" });
  const replies = by("reply");
  const full = "You have two meetings today: design review at 10 and lunch with Sam at 1.";
  assert.deepEqual(replies.map((r) => r.final), [false, false, false, true]);
  assert.equal(replies[3].text, full);
  assert.ok(full.startsWith(replies[0].text) && full.startsWith(replies[1].text) && replies[0].text.length < replies[1].text.length);
  const begin = by("speak.begin")[0];
  assert.deepEqual([begin.rate, begin.turn], [16000, "t0a0b0c0d-1"]);
  const frames = gadget.binInbox.filter((f) => f.kind === 2);
  assert.ok(frames.every((f) => f.stream === begin.stream));
  assert.equal(frames.reduce((n, f) => n + f.payload.length, 0), 200 * 32, "200 ms at 16 kHz, 2 bytes per sample");
  assert.ok(frames.slice(0, -1).every((f) => f.payload.length === 1280), "40 ms frames");
  assert.deepEqual(frames[0].payload, toneSamples(16000, 0, 640));
  assert.equal(by("speak.end")[0].stream, begin.stream);
  assert.deepEqual(events.filter((e) => e.event === "turn").map((e) => e.phase), ["started", "heard", "reply", "speech", "done"]);
  assert.ok(events.some((e) => e.event === "rx_binary" && e.kind === 1 && e.bytes === 640));
});

test("speech is paced to real time, never more than 0.5 s ahead", async (t) => {
  const { h, gadget } = await setup(t, { toneMs: 1200 });
  // Time from the host's own speak.begin send: the tx event is emitted synchronously, before
  // the host starts its pacing clock, so a stall in this test's event loop cannot skew it.
  let t0 = 0;
  h.on((e) => {
    if (e.event === "tx" && typeof e.msg === "string" && JSON.parse(e.msg).op === "speak.begin") t0 = Date.now();
  });
  gadget.send({ op: "say", turn: "t00000001-1", text: "hi" });
  await gadget.next("speak.begin", () => true, 5000);
  assert.ok(t0 > 0, "the host emitted tx for speak.begin");
  let audioMs = 0;
  let worst = 0;
  for (;;) {
    const f = await gadget.nextBinary(2, 5000);
    audioMs += f.payload.length / 32;
    worst = Math.max(worst, audioMs - (Date.now() - t0));
    if (audioMs >= 1200) break;
  }
  assert.ok(worst <= 500, `audio ran ${worst} ms ahead`);
  assert.ok(Date.now() - t0 >= 700, "1.2 s of audio took at least 0.7 s to arrive");
  await gadget.next("done");
});

test("say skips heard; an empty STT result fails with Didn't catch that; a 24 kHz speaker gets 960-sample frames", async (t) => {
  const { h, gadget } = await setup(t, {}, { caps: { ...CAPS, speaker: { rate: 24000 } } });
  gadget.send({ op: "say", turn: "t00000001-1", text: "hello" });
  await gadget.next("done");
  assert.ok(!gadget.ops().includes("heard"));
  assert.equal(gadget.all.find((m) => m.op === "speak.begin")!.rate, 24000);
  assert.equal(gadget.binInbox.find((f) => f.kind === 2)!.payload.length, 1920);
  await h.command({ cmd: "heard", text: "" });
  talk(gadget, "t00000001-2");
  assert.deepEqual(await gadget.next("done"), { op: "done", turn: "t00000001-2", outcome: "failed", reason: "Didn't catch that" });
});

test("voice.begin at 8 kHz fails with Unsupported mic rate", async (t) => {
  const { gadget } = await setup(t);
  gadget.send({ op: "voice.begin", turn: "t00000002-1", stream: 1, rate: 8000 });
  assert.deepEqual(await gadget.next("done"), { op: "done", turn: "t00000002-1", outcome: "failed", reason: "Unsupported mic rate" });
});

test("stop during speech: speak.stop then done stopped, and nothing more for that turn", async (t) => {
  const { gadget } = await setup(t, { toneMs: 3000 });
  talk(gadget, "t00000003-1");
  const begin = await gadget.next("speak.begin", () => true, 5000);
  gadget.send({ op: "stop", turn: "t00000003-1" });
  const stop = await gadget.next("speak.stop");
  assert.equal(stop.stream, begin.stream);
  assert.deepEqual(await gadget.next("done"), { op: "done", turn: "t00000003-1", outcome: "stopped" });
  assert.ok(gadget.ops().indexOf("speak.stop") < gadget.ops().indexOf("done"), `speak.stop comes before done: ${gadget.ops().join(" ")}`);
  const seen = gadget.all.length;
  await delay(300);
  assert.deepEqual(gadget.all.slice(seen), [], "no frames after done");
  assert.ok(!gadget.ops().includes("speak.end"));
});

test("barge-in: a new voice.begin stops the old turn before the new turn's first message", async (t) => {
  const { gadget } = await setup(t, { replyIntervalMs: 200 });
  gadget.send({ op: "say", turn: "t00000004-1", text: "first" });
  await gadget.next("working");
  talk(gadget, "t00000004-2");
  const old = await gadget.next("done", (m) => m.turn === "t00000004-1");
  assert.equal(old.outcome, "stopped");
  await gadget.next("done", (m) => m.turn === "t00000004-2", 5000);
  const ops = gadget.all.map((m) => `${m.op}:${m.turn ?? ""}`);
  const stoppedAt = ops.indexOf("done:t00000004-1");
  const firstNew = ops.findIndex((o) => o.endsWith(":t00000004-2"));
  assert.ok(stoppedAt >= 0 && stoppedAt < firstNew, ops.join(" "));
  assert.ok(!ops.slice(stoppedAt + 1).some((o) => o.endsWith(":t00000004-1")), "nothing for the old turn after its done");
});

test("barge-in during speech: speak.stop for the old stream, then done stopped, before the new turn's first message", async (t) => {
  const { gadget } = await setup(t, { toneMs: 3000 });
  const seq: string[] = [];   // text frames and speaker frames, in arrival order
  gadget.ws.on("message", (data: Buffer, isBinary: boolean) => {
    if (isBinary) {
      if (data[0] === 2) seq.push(`speaker:${data[1]}`);
      return;
    }
    const m = JSON.parse(data.toString("utf8"));
    seq.push(m.op === "speak.stop" ? `speak.stop:${m.stream}` : `${m.op}:${m.turn ?? ""}`);
  });
  gadget.send({ op: "say", turn: "t00000009-1", text: "first" });
  const old = await gadget.next("speak.begin", () => true, 5000);
  gadget.send({ op: "say", turn: "t00000009-2", text: "second" });
  // The new turn's speak.begin is its last frame before 3 s of tone; the old stream had its chance.
  await gadget.next("speak.begin", (m) => m.turn === "t00000009-2", 5000);
  const stopAt = seq.indexOf(`speak.stop:${old.stream}`);
  const doneAt = seq.indexOf("done:t00000009-1");
  const firstNew = seq.findIndex((s) => s.endsWith(":t00000009-2"));
  assert.ok(stopAt >= 0 && stopAt < doneAt && doneAt < firstNew, seq.join(" "));
  assert.ok(!seq.slice(doneAt + 1).includes(`speaker:${old.stream}`), "no speaker frame for the old stream after its done");
});

test("done before speech: done ok precedes speak.begin, speech continues, and a new say sends speak.stop before the new turn's first message", async (t) => {
  const { gadget, events } = await setup(t, { doneBeforeSpeech: true, toneMs: 3000 });
  gadget.send({ op: "say", turn: "t0000000a-1", text: "first" });
  assert.deepEqual(await gadget.next("done", () => true, 5000), { op: "done", turn: "t0000000a-1", outcome: "ok" });
  const begin = await gadget.next("speak.begin", () => true, 5000);
  assert.equal(begin.turn, "t0000000a-1");
  assert.equal((await gadget.nextBinary(2)).stream, begin.stream, "speech continues after done");
  gadget.send({ op: "say", turn: "t0000000a-2", text: "second" });
  assert.equal((await gadget.next("speak.stop")).stream, begin.stream);
  await gadget.next("done", (m) => m.turn === "t0000000a-2", 5000);
  const seq = gadget.all.map((m) => (m.op === "speak.stop" ? `speak.stop:${m.stream}` : `${m.op}:${m.turn ?? ""}`));
  const doneAt = seq.indexOf("done:t0000000a-1");
  const beginAt = seq.indexOf("speak.begin:t0000000a-1");
  const stopAt = seq.indexOf(`speak.stop:${begin.stream}`);
  const firstNew = seq.findIndex((s) => s.endsWith(":t0000000a-2"));
  assert.ok(doneAt >= 0 && doneAt < beginAt && beginAt < stopAt && stopAt < firstNew, seq.join(" "));
  assert.ok(!gadget.ops().includes("speak.end"), "the first stream was stopped, not ended");
  const phases = events.filter((e) => e.event === "turn" && e.turn === "t0000000a-1").map((e) => e.phase);
  assert.deepEqual(phases, ["started", "reply", "done", "speech"]);
});

test("voice.drop ends the turn as stopped; stop for a turn not in flight is ignored", async (t) => {
  const { gadget } = await setup(t);
  gadget.send({ op: "voice.begin", turn: "t00000005-1", stream: 3, rate: 16000 });
  gadget.sendBinary(1, 3, mic(20));
  gadget.send({ op: "voice.drop", turn: "t00000005-1" });
  assert.deepEqual(await gadget.next("done"), { op: "done", turn: "t00000005-1", outcome: "stopped" });
  gadget.send({ op: "stop", turn: "t00000005-1" });
  gadget.send({ op: "say", turn: "t00000005-2", text: "x" });
  const next = await gadget.next("done", () => true, 5000);
  assert.equal(next.turn, "t00000005-2", "the stray stop produced no done");
});

test("no speaker: text only; replies are folded to Latin-1 and cut to the frame limit", async (t) => {
  const { h, gadget } = await setup(t, {}, { caps: { ...CAPS, speaker: undefined } });
  await h.command({ cmd: "reply", text: "Café “quoted” — done 🎉" });
  gadget.send({ op: "say", turn: "t00000006-1", text: "x" });
  await gadget.next("done");
  assert.ok(!gadget.ops().includes("speak.begin"));
  assert.equal(gadget.all.filter((m) => m.op === "reply").at(-1)!.text, 'Café "quoted" - done ');
  const long = "x".repeat(20000) + " END";
  const r = fitReply("t1", long, true);
  assert.ok(Buffer.byteLength(JSON.stringify(r)) <= 16384);
  assert.ok(r.text.startsWith("…") && r.text.endsWith(" END"));
  assert.deepEqual(cumulativeParts("one two three four five six"), ["one two", "one two three four", "one two three four five six"]);
});

test("an unbound gadget's turn fails with the Remote access pointer", async (t) => {
  const { gadget } = await setup(t, { bot: { id: "", name: "" } });
  gadget.send({ op: "say", turn: "t00000007-1", text: "x" });
  assert.deepEqual(await gadget.next("done"), {
    op: "done", turn: "t00000007-1", outcome: "failed", reason: "Pick a bot for this gadget in MausBot → Settings → Remote access",
  });
});

test("a disconnect mid-turn cancels the turn's timers", async (t) => {
  const { gadget, events } = await setup(t, { replyIntervalMs: 50 });
  gadget.send({ op: "say", turn: "t00000008-1", text: "x" });
  await gadget.next("working");
  await gadget.close();
  await delay(300);
  assert.ok(!events.some((e) => e.event === "turn" && e.phase === "reply"), "no reply after the socket closed");
});
```

- [ ] **Step 2: Run it to see it fail**

Run: `node --test tools/fake-host/test/voice.test.ts`
Expected: FAIL with `ERR_MODULE_NOT_FOUND` for `tools/fake-host/src/voice.ts`.

- [ ] **Step 3: Create `tools/fake-host/src/speech.ts`**

```ts
// SPDX-License-Identifier: Apache-2.0
// Speech out (PROTOCOL.md §4.4): a 440 Hz test tone instead of synthesized speech, sent as
// 40 ms PCM16 frames paced to real time and never more than 0.5 s ahead.
import { BinaryKind, SPEAK_AHEAD_MS, SPEAK_FRAME_MS } from "../../../protocol/lib/types.ts";
import type { GadgetSession } from "./session.ts";

const TONE_HZ = 440;
const TONE_AMPLITUDE = 8000;

/** PCM16 little-endian samples [from, from + count) of the test tone at `rate`. */
export function toneSamples(rate: number, from: number, count: number): Uint8Array {
  const out = new Uint8Array(count * 2);
  const view = new DataView(out.buffer);
  for (let i = 0; i < count; i++) {
    view.setInt16(i * 2, Math.round(TONE_AMPLITUDE * Math.sin((2 * Math.PI * TONE_HZ * (from + i)) / rate)), true);
  }
  return out;
}

/** caps.speaker.rate when it is 16000 or 24000, else null (no speaker). */
export function speakerRate(session: GadgetSession): 16000 | 24000 | null {
  const rate = session.hello?.caps.speaker?.rate;
  return rate === 16000 || rate === 24000 ? rate : null;
}

interface Playing { stream: number; turn?: string; cancelled: boolean }
interface Job { turn?: string; ms: number; done: (played: boolean) => void }
const sleep = (ms: number): Promise<void> => new Promise((r) => setTimeout(r, ms));

export class SpeechPlayer {
  private readonly session: GadgetSession;
  private playing: Playing | null = null;
  private readonly queue: Job[] = [];

  constructor(session: GadgetSession) {
    this.session = session;
    session.onClose(() => this.cancelAll());
  }

  /** Reply speech (with a turn) replaces whatever plays; post speech (no turn) waits until the
   *  earlier stream has played out (PROTOCOL.md §4.4). Resolves true once the stream has played
   *  out in real time, false when it is stopped or replaced first, or there is no speaker. */
  play(turn: string | undefined, ms: number): Promise<boolean> {
    if (speakerRate(this.session) === null || ms <= 0) return Promise.resolve(false);
    return new Promise((done) => {
      const job: Job = { turn, ms, done };
      if (turn !== undefined && this.playing) {
        this.playing.cancelled = true;
        this.queue.unshift(job);
      } else {
        this.queue.push(job);
      }
      if (!this.playing || this.playing.cancelled) void this.pump();
    });
  }

  /** speak.stop for the turn's stream if it plays, and drops its queued speech. */
  stopTurn(turn: string): void {
    for (let i = this.queue.length - 1; i >= 0; i--) {
      if (this.queue[i].turn === turn) this.queue.splice(i, 1)[0].done(false);
    }
    const p = this.playing;
    if (p && !p.cancelled && p.turn === turn) {
      p.cancelled = true;
      this.session.send({ op: "speak.stop", stream: p.stream });
    }
  }

  private cancelAll(): void {
    if (this.playing) this.playing.cancelled = true;
    for (const job of this.queue.splice(0)) job.done(false);
  }

  private pumping = false;
  private async pump(): Promise<void> {
    if (this.pumping) return;
    this.pumping = true;
    try {
      while (this.queue.length > 0 && !this.session.closed) {
        const job = this.queue.shift()!;
        job.done(await this.stream(job));
      }
    } finally {
      this.pumping = false;
    }
  }

  private async stream(job: Job): Promise<boolean> {
    const rate = speakerRate(this.session);
    if (rate === null) return false;
    const stream = this.session.allocStream();
    const p: Playing = { stream, turn: job.turn, cancelled: false };
    this.playing = p;
    try {
      this.session.send(job.turn === undefined ? { op: "speak.begin", stream, rate } : { op: "speak.begin", stream, rate, turn: job.turn });
      const total = Math.round((rate * job.ms) / 1000);
      const perFrame = (rate * SPEAK_FRAME_MS) / 1000;
      const start = Date.now();
      const playoutEnd = start + (total * 1000) / rate;
      let sent = 0;
      while (sent < total && !p.cancelled) {
        const n = Math.min(perFrame, total - sent);
        const aheadMs = ((sent + n) * 1000) / rate - (Date.now() - start);
        if (aheadMs > SPEAK_AHEAD_MS) {
          await sleep(Math.max(1, Math.ceil(aheadMs - SPEAK_AHEAD_MS)));
          continue;
        }
        if (!(await this.session.sendBinary(BinaryKind.speaker, stream, toneSamples(rate, sent, n)))) return false;
        sent += n;
      }
      if (p.cancelled) return false;
      this.session.send({ op: "speak.end", stream });
      // The gadget still has up to 0.5 s buffered: keep the slot until it has played out, so a
      // post's speak.begin never cuts it. A stop or a reply's play() still cancels it at once.
      while (!p.cancelled && Date.now() < playoutEnd) await sleep(Math.min(20, playoutEnd - Date.now()));
      return !p.cancelled;
    } finally {
      this.session.releaseStream(stream);
      if (this.playing === p) this.playing = null;
    }
  }
}

const players = new WeakMap<GadgetSession, SpeechPlayer>();
/** The session's one speech player. */
export function speechOf(session: GadgetSession): SpeechPlayer {
  let p = players.get(session);
  if (!p) {
    p = new SpeechPlayer(session);
    players.set(session, p);
  }
  return p;
}
```

- [ ] **Step 4: Create `tools/fake-host/src/voice.ts`**

```ts
// SPDX-License-Identifier: Apache-2.0
// Conversation turns (PROTOCOL.md §4.4) with a scripted speech-to-text result and reply:
// heard → working → three cumulative replies 250 ms apart → final reply → test-tone speech → done
// (with doneBeforeSpeech: final reply → done → test-tone speech).
import { Buffer } from "node:buffer";
import type { ReplyMsg, TurnOutcome } from "../../../protocol/lib/types.ts";
import { BinaryKind, MIC_RATE, TEXT_FRAME_MAX, UTTERANCE_MAX_MS } from "../../../protocol/lib/types.ts";
import type { Feature, HostContext } from "./context.ts";
import { foldLatin1 } from "./fold.ts";
import type { GadgetSession } from "./session.ts";
import { speakerRate, speechOf } from "./speech.ts";

export const WORKING_TEXT = "checking your calendar";
export const NOTHING_HEARD = "Didn't catch that";
export const BAD_MIC_RATE = "Unsupported mic rate";
export const NO_BOT = "Pick a bot for this gadget in MausBot → Settings → Remote access";
const MIC_BYTES_MAX = (MIC_RATE * 2 * UTTERANCE_MAX_MS) / 1000;

/** The reply in three cumulative parts, cut at word boundaries (the last part is the whole text). */
export function cumulativeParts(text: string): [string, string, string] {
  const words = text.split(" ");
  const at = (k: number): string => words.slice(0, Math.ceil((words.length * k) / 3)).join(" ");
  return [at(1), at(2), text];
}

/** A reply frame within the text frame limit: a longer text is cut from the start behind "…". */
export function fitReply(turn: string, text: string, final: boolean): ReplyMsg {
  const fits = (t: string): boolean => Buffer.byteLength(JSON.stringify({ op: "reply", turn, text: t, final }), "utf8") <= TEXT_FRAME_MAX;
  if (fits(text)) return { op: "reply", turn, text, final };
  const chars = [...text];
  let lo = 1;
  let hi = chars.length;
  while (lo < hi) {
    const mid = (lo + hi) >> 1;
    if (fits("…" + chars.slice(mid).join(""))) hi = mid;
    else lo = mid + 1;
  }
  return { op: "reply", turn, text: "…" + chars.slice(lo).join(""), final };
}

interface Turn { id: string; recording: boolean; stream: number | null; audioBytes: number; timers: Set<NodeJS.Timeout> }

class VoiceSession {
  private readonly session: GadgetSession;
  private readonly host: HostContext;
  private turn: Turn | null = null;
  /** A turn that already got `done ok` while its speech may still play (doneBeforeSpeech). */
  private speakingTurn: string | null = null;

  constructor(session: GadgetSession, host: HostContext) {
    this.session = session;
    this.host = host;
    session.onOp("voice.begin", (m) => this.onVoiceBegin(m));
    session.onOp("voice.end", (m) => this.onVoiceEnd(m));
    session.onOp("voice.drop", (m) => {
      if (this.turn && m.turn === this.turn.id) this.end(this.turn, "stopped");
    });
    session.onOp("say", (m) => this.onSay(m));
    session.onOp("stop", (m) => {
      if (this.turn && (m.turn === undefined || m.turn === this.turn.id)) this.end(this.turn, "stopped");
    });
    session.onBinary((kind, stream, payload) => {
      const t = this.turn;
      if (kind !== BinaryKind.mic || !t || !t.recording || stream !== t.stream) return;
      t.audioBytes = Math.min(MIC_BYTES_MAX, t.audioBytes + payload.length);
    });
    session.onClose(() => {
      if (this.turn) for (const timer of this.turn.timers) clearTimeout(timer);
      this.turn = null;
      this.speakingTurn = null;
    });
  }

  private event(turn: string, phase: string, outcome?: TurnOutcome): void {
    this.host.emit({ event: "turn", gadget: this.session.record!.id, turn, phase, ...(outcome ? { outcome } : {}) });
  }

  private start(id: string, stream: number | null): Turn {
    if (this.turn) this.end(this.turn, "stopped");
    else if (this.speakingTurn !== null) speechOf(this.session).stopTurn(this.speakingTurn);
    this.speakingTurn = null;
    const t: Turn = { id, recording: stream !== null, stream, audioBytes: 0, timers: new Set() };
    this.turn = t;
    this.event(id, "started");
    return t;
  }

  /** Ends the turn: speak.stop for its speech when stopping, then done. Nothing follows for it. */
  private end(t: Turn, outcome: TurnOutcome, reason?: string): void {
    if (this.turn !== t) return;
    this.turn = null;
    for (const timer of t.timers) clearTimeout(timer);
    if (outcome !== "ok") speechOf(this.session).stopTurn(t.id);
    this.session.send(reason === undefined ? { op: "done", turn: t.id, outcome } : { op: "done", turn: t.id, outcome, reason: foldLatin1(reason) });
    this.event(t.id, "done", outcome);
  }

  private later(t: Turn, ms: number, fn: () => void): void {
    const timer = setTimeout(() => {
      t.timers.delete(timer);
      if (this.turn === t) fn();
    }, ms);
    t.timers.add(timer);
  }

  private onVoiceBegin(m: Record<string, unknown>): void {
    if (typeof m.turn !== "string" || m.turn === "" || m.turn.length > 32) return;
    if (typeof m.stream !== "number" || !Number.isInteger(m.stream) || m.stream < 1 || m.stream > 255) return;
    const t = this.start(m.turn, m.stream);
    if (m.rate !== MIC_RATE) this.end(t, "failed", BAD_MIC_RATE);
  }

  private onVoiceEnd(m: Record<string, unknown>): void {
    const t = this.turn;
    if (!t || !t.recording || m.turn !== t.id) return;
    t.recording = false;
    if (this.session.record!.bot.id === "") return this.end(t, "failed", NO_BOT);
    const heard = foldLatin1(this.host.script.heard);
    if (heard.trim() === "") return this.end(t, "failed", NOTHING_HEARD);
    this.session.send({ op: "heard", turn: t.id, text: heard });
    this.event(t.id, "heard");
    this.respond(t);
  }

  private onSay(m: Record<string, unknown>): void {
    if (typeof m.turn !== "string" || m.turn === "" || m.turn.length > 32 || typeof m.text !== "string") return;
    const t = this.start(m.turn, null);
    if (this.session.record!.bot.id === "") return this.end(t, "failed", NO_BOT);
    this.respond(t);
  }

  private respond(t: Turn): void {
    const every = this.host.options.replyIntervalMs;
    const parts = cumulativeParts(foldLatin1(this.host.script.reply));
    this.session.send({ op: "working", turn: t.id, text: WORKING_TEXT });
    parts.forEach((text, i) => this.later(t, every * (i + 1), () => this.session.send(fitReply(t.id, text, false))));
    this.later(t, every * 4, () => {
      this.session.send(fitReply(t.id, parts[2], true));
      this.event(t.id, "reply");
      const toneMs = this.host.options.toneMs;
      const speech = speechOf(this.session);
      if (toneMs <= 0 || speakerRate(this.session) === null) return this.end(t, "ok");
      if (this.host.options.doneBeforeSpeech) {
        // MausBot's usual order: done when the bot's turn completes, the speech after it (§4.4).
        this.end(t, "ok");
        this.speakingTurn = t.id;
        this.event(t.id, "speech");
        void speech.play(t.id, toneMs).then(() => {
          if (this.speakingTurn === t.id) this.speakingTurn = null;
        });
        return;
      }
      this.event(t.id, "speech");
      void speech.play(t.id, toneMs).then((played) => {
        if (played) this.end(t, "ok");
      });
    });
  }
}

function setText(field: "heard" | "reply"): Feature["commands"][string] {
  return (call) => {
    if (typeof call.cmd.text !== "string") throw new Error("text must be a string");
    call.host.script[field] = call.cmd.text;
  };
}

export const voiceFeature: Feature = {
  attach(session, host) {
    new VoiceSession(session, host);
  },
  commands: { heard: setText("heard"), reply: setText("reply") },
};
```

- [ ] **Step 5: Replace `tools/fake-host/src/features.ts`**

```ts
// SPDX-License-Identifier: Apache-2.0
// The features every fake host runs. Later tasks add display and OTA here.
import type { Feature } from "./context.ts";
import { voiceFeature } from "./voice.ts";

export const FEATURES: Feature[] = [voiceFeature];
```

- [ ] **Step 6: Run the fake-host tests to see them pass**

Run: `npm run test:fake-host`
Expected: PASS (37).

- [ ] **Step 7: Commit**

```bash
git add tools/fake-host/src/speech.ts tools/fake-host/src/voice.ts tools/fake-host/src/features.ts tools/fake-host/test/voice.test.ts
git commit -m "feat(fake-host): scripted voice turns and paced test-tone speech" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 10: Asks, posts, cards, images, actions and settings

**Files:**
- Create: `tools/fake-host/test/display.test.ts`, `tools/fake-host/src/display.ts`
- Modify: `tools/fake-host/src/features.ts` (replace the file)

**Interfaces:**
- Consumes: `GadgetSession`, `CommandCall` (`session()`, `gadgetId()`), `HostContext` (`live`, `state`, `emit`, `options.toneMs`, `options.actTimeoutMs`), `speechOf`, `foldLatin1`, `cutChars`, `canonicalJson`; constants `ASK_OPTIONS_MAX`, `BinaryKind`, `HOST_SENT_ID_RE`, `IMAGE_FRAME_PAYLOAD_MAX`.
- Produces (`display.ts`): `PERMISSION_OPTIONS`; `imagePixels(w: number, h: number, pattern: string): Uint8Array`; `displayFeature: Feature` with commands `ask`, `ask.close`, `post`, `card`, `card.close`, `image`, `act`, `settings` (fields in the README table, Task 12). Events: `answer {gadget, id, option}`, `act.result {gadget, id, ok, data?, error?}` or `{…, timeout: true}`. Ask queues live per host and per gadget id, so they survive reconnects.

- [ ] **Step 1: Write the failing test `tools/fake-host/test/display.test.ts`**

```ts
// SPDX-License-Identifier: Apache-2.0
import { test, type TestContext } from "node:test";
import assert from "node:assert/strict";
import { Buffer } from "node:buffer";
import { startFakeHost, type FakeHost } from "../src/server.ts";
import type { FakeHostOptions } from "../src/options.ts";
import type { HostEvent } from "../src/context.ts";
import { imagePixels } from "../src/display.ts";
import { CAPS, connectGadget, nextEvent, randomKey, type GadgetOptions, type TestGadget } from "./gadget-client.ts";

async function setup(t: TestContext, o: Partial<FakeHostOptions> = {}, g: Partial<GadgetOptions> = {}): Promise<{ h: FakeHost; gadget: TestGadget; events: HostEvent[]; key: string }> {
  const events: HostEvent[] = [];
  const h = await startFakeHost({ port: 0, quiet: true, code: "123456", toneMs: 120, ...o }, { listener: (e) => events.push(e) });
  t.after(() => h.close());
  const key = randomKey();
  const { gadget, result } = await connectGadget({ port: h.port, enroll: "123456", key, ...g });
  assert.equal(result.op, "ready");
  return { h, gadget, events, key };
}

test("a permission ask carries exactly Allow and Deny; answering closes it as answered", async (t) => {
  const { h, gadget, events } = await setup(t);
  const ack = await h.command({ cmd: "ask", kind: "permission", title: "Run “rm”?", body: "In ~/Downloads" });
  assert.equal(ack.ok, true);
  const ask = await gadget.next("ask");
  assert.deepEqual(ask, {
    op: "ask", id: ack.id, kind: "permission", title: 'Run "rm"?', body: "In ~/Downloads",
    options: [{ id: "allow", label: "Allow", style: "allow" }, { id: "deny", label: "Deny", style: "deny" }],
  });
  gadget.send({ op: "answer", id: ask.id, option: "deny" });
  assert.deepEqual(await gadget.next("ask.close"), { op: "ask.close", id: ask.id, reason: "answered" });
  assert.ok(events.some((e) => e.event === "answer" && e.id === ask.id && e.option === "deny"));
  const second = nextEvent(h, (e) => e.event === "rx" && String(e.msg).includes('"option":"allow"'));
  gadget.send({ op: "answer", id: ask.id, option: "allow" });
  await second;
  assert.equal(events.filter((e) => e.event === "answer").length, 1, "a second answer is ignored");
  assert.equal((await h.command({ cmd: "ask", kind: "permission", title: "x", options: [] })).error, "permission asks always carry Allow and Deny");
  const explicit = [{ label: "Allow", id: "allow", style: "allow" }, { id: "deny", style: "deny", label: "Deny" }];
  assert.equal((await h.command({ cmd: "ask", kind: "permission", title: "y", options: explicit })).ok, true, "explicit Allow and Deny are accepted");
  assert.deepEqual((await gadget.next("ask")).options, [{ id: "allow", label: "Allow", style: "allow" }, { id: "deny", label: "Deny", style: "deny" }]);
});

test("question asks: up to 4 neutral options, none for unsupported asks, more than 4 refused", async (t) => {
  const { h, gadget } = await setup(t);
  const opts = [{ id: "1", label: "Tea", style: "neutral" }, { id: "2", label: "Coffee" }];
  await h.command({ cmd: "ask", kind: "question", title: "Drink?", body: "", options: opts });
  const ask = await gadget.next("ask");
  assert.deepEqual(ask.options, [{ id: "1", label: "Tea", style: "neutral" }, { id: "2", label: "Coffee" }]);
  gadget.send({ op: "answer", id: ask.id, option: "9" });
  gadget.send({ op: "answer", id: ask.id, option: "2" });
  assert.equal((await gadget.next("ask.close")).reason, "answered");
  await h.command({ cmd: "ask", kind: "question", title: "Long form", body: "" });
  assert.deepEqual((await gadget.next("ask")).options, []);
  const five = Array.from({ length: 5 }, (_, i) => ({ id: String(i), label: String(i) }));
  assert.equal((await h.command({ cmd: "ask", kind: "question", title: "x", options: five })).error, "options must be an array of at most 4 entries");
});

test("asks go one at a time, oldest first; expiry and withdrawal close them", async (t) => {
  const { h, gadget } = await setup(t);
  const a = await h.command({ cmd: "ask", kind: "permission", title: "A", expires_s: 1 });
  const b = await h.command({ cmd: "ask", kind: "permission", title: "B" });
  const c = await h.command({ cmd: "ask", kind: "permission", title: "C" });
  assert.deepEqual([a.queued, b.queued, c.queued], [false, true, true]);
  assert.equal((await gadget.next("ask")).id, a.id);
  assert.deepEqual(await gadget.next("ask.close", () => true, 3000), { op: "ask.close", id: a.id, reason: "expired" });
  assert.equal((await gadget.next("ask")).id, b.id);
  assert.deepEqual(await h.command({ cmd: "ask.close", id: c.id }), { event: "ack", cmd: "ask.close", ok: true });
  assert.equal((await h.command({ cmd: "ask.close", id: b.id, reason: "withdrawn" })).ok, true);
  assert.deepEqual(await gadget.next("ask.close"), { op: "ask.close", id: b.id, reason: "withdrawn" });
  assert.equal(gadget.all.filter((m) => m.op === "ask").length, 2, "C was withdrawn before it was shown");
});

test("an open ask is sent again on reconnect", async (t) => {
  const { h, gadget, key } = await setup(t);
  const a = await h.command({ cmd: "ask", kind: "permission", title: "Still open" });
  await gadget.next("ask");
  await gadget.close();
  const { gadget: back } = await connectGadget({ port: h.port, key });
  assert.equal((await back.next("ask")).id, a.id);
});

test("post: routine and message kinds, bot from the record, speech only when asked", async (t) => {
  const { h, gadget } = await setup(t);
  const p = await h.command({ cmd: "post", kind: "routine", text: "Backups done ✅" });
  assert.deepEqual(await gadget.next("post"), {
    op: "post", id: p.id, bot: { id: "b_fake", name: "Fake Bot" }, kind: "routine", text: "Backups done ", speak: false,
  });
  await h.command({ cmd: "post", kind: "message", text: "Handoff finished", speak: true });
  assert.equal((await gadget.next("post")).speak, true);
  const begin = await gadget.next("speak.begin");
  assert.equal(begin.turn, undefined, "post speech has no turn");
  assert.equal((await gadget.next("speak.end")).stream, begin.stream);
  assert.equal((await h.command({ cmd: "post", kind: "digest", text: "x" })).error, "kind must be routine or message");
});

test("post speech starts only after the reply's speech has played out", async (t) => {
  const { h, gadget } = await setup(t, { toneMs: 300, replyIntervalMs: 10 });
  const begins: number[] = [];   // arrival times of speak.begin frames
  gadget.ws.on("message", (data: Buffer, isBinary: boolean) => {
    if (!isBinary && JSON.parse(data.toString("utf8")).op === "speak.begin") begins.push(Date.now());
  });
  gadget.send({ op: "say", turn: "t00000001-1", text: "x" });
  const replyBegin = await gadget.next("speak.begin", () => true, 5000);
  await h.command({ cmd: "post", kind: "message", text: "later", speak: true });
  const replyEnd = await gadget.next("speak.end", () => true, 5000);
  const postBegin = await gadget.next("speak.begin", () => true, 5000);
  assert.equal(replyEnd.stream, replyBegin.stream);
  assert.equal(postBegin.turn, undefined);
  const ops = gadget.ops();
  assert.ok(ops.indexOf("speak.end") < ops.lastIndexOf("speak.begin"));
  // Frames run up to 0.5 s ahead, so speak.end alone does not mean the reply has played out.
  assert.ok(begins[1] - begins[0] >= 300 - 60, `the post's speak.begin came ${begins[1] - begins[0]} ms after the reply's (300 ms of audio)`);
});

test("card and card.close; titles and bodies are cut to 80 and 600 characters", async (t) => {
  const { h, gadget } = await setup(t);
  const c = await h.command({ cmd: "card", title: "T".repeat(100), body: "B".repeat(700) });
  const card = await gadget.next("card");
  assert.deepEqual([card.id, card.title.length, card.body.length, card.ttl_s], [c.id, 80, 600, 30]);
  await h.command({ cmd: "card", id: "notice-voice", title: "Voice is off", body: "x", ttl_s: 8 });
  assert.equal((await gadget.next("card")).ttl_s, 8);
  await h.command({ cmd: "card.close", id: "notice-voice" });
  assert.deepEqual(await gadget.next("card.close"), { op: "card.close", id: "notice-voice" });
  assert.match(String((await h.command({ cmd: "card", id: "bad id!", title: "x" })).error), /id must be/);
});

test("image: begin, RGB565 rows of at most 8190 bytes, end; larger than caps.image is refused", async (t) => {
  const { h, gadget } = await setup(t);
  const ack = await h.command({ cmd: "image", w: 300, h: 300, pattern: "bars" });
  assert.equal(ack.ok, true);
  const begin = await gadget.next("image.begin");
  assert.deepEqual([begin.w, begin.h, begin.ttl_s], [300, 300, 30]);
  const end = await gadget.next("image.end");
  assert.equal(end.stream, begin.stream);
  const rows = gadget.binInbox.filter((f) => f.kind === 3);
  assert.ok(rows.every((f) => f.stream === begin.stream && f.payload.length <= 8190));
  const bytes = Buffer.concat(rows.map((f) => f.payload));
  assert.deepEqual(new Uint8Array(bytes), imagePixels(300, 300, "bars"));
  assert.equal(new DataView(bytes.buffer, bytes.byteOffset).getUint16(0, true), 0xffff, "first bar is white");
  assert.equal((await h.command({ cmd: "image", w: 301, h: 10 })).error, "image must fit 300x300");
  assert.deepEqual(imagePixels(1, 1, "#ff0000"), new Uint8Array([0x00, 0xf8]));
});

test("act: one act.result per act, timeout after the act timeout", async (t) => {
  const { h, gadget } = await setup(t, { actTimeoutMs: 200 }, { actions: [{ name: "chime", description: "Chime.", params: { type: "object" }, risk: "safe" }] });
  const a = await h.command({ cmd: "act", name: "chime" });
  const act = await gadget.next("act");
  assert.deepEqual(act, { op: "act", id: a.id, name: "chime", args: {} });
  const result = nextEvent(h, (e) => e.event === "act.result" && e.id === a.id);
  gadget.send({ op: "act.result", id: act.id, ok: true, data: { played: 1 } });
  const r = await result;
  assert.deepEqual(r, { event: "act.result", gadget: gadget.id, id: a.id, ok: true, data: { played: 1 } });
  const b = await h.command({ cmd: "act", name: "chime", args: { times: 2 } });
  assert.deepEqual((await gadget.next("act")).args, { times: 2 });
  const timeout = await nextEvent(h, (e) => e.event === "act.result" && e.id === b.id, 2000);
  assert.equal(timeout.timeout, true);
});

test("settings reach a live gadget; a rename while offline is sent right after the next ready", async (t) => {
  const { h, gadget, key } = await setup(t);
  await h.command({ cmd: "settings", bot: { id: "b_jev", name: "Jév" }, speak_pushes: true });
  assert.deepEqual(await gadget.next("settings"), { op: "settings", bot: { id: "b_jev", name: "Jév" }, settings: { speak_pushes: true } });
  const id = gadget.id;
  const offline = nextEvent(h, (e) => e.event === "closed" && e.gadget === id);
  await gadget.close();
  await offline;
  await h.command({ cmd: "settings", gadget: id, name: "Kitchen" });
  const { gadget: back, result } = await connectGadget({ port: h.port, key, name: "Desk" });
  assert.deepEqual(result.bot, { id: "b_jev", name: "Jév" });
  assert.deepEqual(await back.next("settings"), { op: "settings", bot: { id: "b_jev", name: "Jév" }, settings: { speak_pushes: true }, name: "Kitchen" });
  assert.equal(h.state.gadgets.get(id)!.name, "Kitchen");
  const { result: third } = await connectGadget({ port: h.port, key, name: "Desk 2" });
  assert.equal(third.op, "ready");
  assert.equal(h.state.gadgets.get(id)!.name, "Desk 2", "the gadget's own rename wins once nothing is pending");
});

test("sense and event frames are reported as rx events and need no answer", async (t) => {
  const { h, gadget, events } = await setup(t, {}, { caps: { ...CAPS } });
  const last = nextEvent(h, (e) => e.event === "rx" && String(e.msg).includes('"op":"event"'));
  gadget.send({ op: "sense", battery_pct: 80, charging: true });
  gadget.send({ op: "event", name: "button.long_press" });
  await last;
  const rx = events.filter((e) => e.event === "rx" && e.gadget === gadget.id).map((e) => JSON.parse(String(e.msg)).op);
  assert.deepEqual(rx.slice(-2), ["sense", "event"]);
});
```

- [ ] **Step 2: Run it to see it fail**

Run: `node --test tools/fake-host/test/display.test.ts`
Expected: FAIL with `ERR_MODULE_NOT_FOUND` for `tools/fake-host/src/display.ts`.

- [ ] **Step 3: Create `tools/fake-host/src/display.ts`**

```ts
// SPDX-License-Identifier: Apache-2.0
// Approvals (§4.5), push (§4.6), cards, images and actions (§4.7), and settings (§4.3), driven by
// control commands.
import { randomBytes } from "node:crypto";
import { canonicalJson } from "../../../protocol/lib/encoding.ts";
import type { AskMsg, AskOption, BotRef, PostKind } from "../../../protocol/lib/types.ts";
import { ASK_OPTIONS_MAX, BinaryKind, HOST_SENT_ID_RE, IMAGE_FRAME_PAYLOAD_MAX } from "../../../protocol/lib/types.ts";
import type { CommandCall, Feature, HostContext } from "./context.ts";
import { cutChars, foldLatin1 } from "./fold.ts";
import type { GadgetSession } from "./session.ts";
import { speechOf } from "./speech.ts";

export const PERMISSION_OPTIONS: AskOption[] = [
  { id: "allow", label: "Allow", style: "allow" },
  { id: "deny", label: "Deny", style: "deny" },
];
const CARD_TITLE_MAX = 80;
const CARD_BODY_MAX = 600;
const DEFAULT_TTL_S = 30;

const newId = (prefix: string): string => prefix + randomBytes(6).toString("hex");
function hostId(call: CommandCall, prefix: string): string {
  const id = call.cmd.id ?? newId(prefix);
  if (typeof id !== "string" || !HOST_SENT_ID_RE.test(id)) throw new Error("id must be 1-40 characters from [A-Za-z0-9_.:-]");
  return id;
}
function text(call: CommandCall, field: string, required = true): string {
  const v = call.cmd[field];
  if (v === undefined && !required) return "";
  if (typeof v !== "string") throw new Error(`${field} must be a string`);
  return v;
}
function ttl(call: CommandCall): number {
  const v = call.cmd.ttl_s ?? DEFAULT_TTL_S;
  if (typeof v !== "number" || !Number.isInteger(v) || v < 0) throw new Error("ttl_s must be a non-negative integer");
  return v;
}

// ---- asks: one at a time per gadget, oldest first, resent on reconnect -------------------------
interface AskQueue { current: AskMsg | null; pending: AskMsg[]; timer: NodeJS.Timeout | null }
const queuesByHost = new WeakMap<HostContext, Map<string, AskQueue>>();
function queueOf(host: HostContext, gadgetId: string): AskQueue {
  let queues = queuesByHost.get(host);
  if (!queues) {
    queues = new Map();
    queuesByHost.set(host, queues);
  }
  let q = queues.get(gadgetId);
  if (!q) {
    q = { current: null, pending: [], timer: null };
    queues.set(gadgetId, q);
  }
  return q;
}

function showNext(host: HostContext, gadgetId: string): void {
  const q = queueOf(host, gadgetId);
  if (q.current || q.pending.length === 0) return;
  const ask = q.pending.shift()!;
  q.current = ask;
  if (ask.expires_s !== undefined) {
    q.timer = setTimeout(() => closeAsk(host, gadgetId, ask.id, "expired"), ask.expires_s * 1000);
  }
  host.live(gadgetId)?.send(ask);
}

function closeAsk(host: HostContext, gadgetId: string, id: string, reason: "answered" | "expired" | "withdrawn"): boolean {
  const q = queueOf(host, gadgetId);
  if (q.current?.id !== id) return false;
  if (q.timer) clearTimeout(q.timer);
  q.timer = null;
  q.current = null;
  host.live(gadgetId)?.send({ op: "ask.close", id, reason });
  showNext(host, gadgetId);
  return true;
}

function askCommand(call: CommandCall): Record<string, unknown> {
  const gadgetId = call.session().record!.id;
  const kind = call.cmd.kind;
  if (kind !== "permission" && kind !== "question") throw new Error("kind must be permission or question");
  let options: AskOption[];
  if (kind === "permission") {
    // Omitted, or exactly Allow and Deny (contract §4.7 lets a script pass them explicitly).
    if (call.cmd.options !== undefined && canonicalJson(call.cmd.options) !== canonicalJson(PERMISSION_OPTIONS)) {
      throw new Error("permission asks always carry Allow and Deny");
    }
    options = PERMISSION_OPTIONS;
  } else {
    const raw = call.cmd.options ?? [];
    if (!Array.isArray(raw) || raw.length > ASK_OPTIONS_MAX) throw new Error("options must be an array of at most 4 entries");
    options = raw.map((o: any) => {
      if (typeof o?.id !== "string" || o.id === "" || typeof o.label !== "string") throw new Error("each option needs an id and a label");
      if (o.style !== undefined && !["allow", "deny", "neutral"].includes(o.style)) throw new Error("style must be allow, deny or neutral");
      return o.style === undefined ? { id: o.id, label: foldLatin1(o.label) } : { id: o.id, label: foldLatin1(o.label), style: o.style };
    });
  }
  const expires = call.cmd.expires_s;
  if (expires !== undefined && (typeof expires !== "number" || !Number.isInteger(expires) || expires < 1)) {
    throw new Error("expires_s must be a positive integer");
  }
  const ask: AskMsg = {
    op: "ask", id: hostId(call, "a_"), kind, title: foldLatin1(text(call, "title")), body: foldLatin1(text(call, "body", false)), options,
  };
  if (expires !== undefined) ask.expires_s = expires;
  const q = queueOf(call.host, gadgetId);
  q.pending.push(ask);
  const queued = q.current !== null;
  showNext(call.host, gadgetId);
  return { id: ask.id, queued };
}

function askCloseCommand(call: CommandCall): void {
  const gadgetId = call.gadgetId();
  const id = text(call, "id");
  const reason = call.cmd.reason ?? "withdrawn";
  if (reason !== "answered" && reason !== "expired" && reason !== "withdrawn") throw new Error("reason must be answered, expired or withdrawn");
  const q = queueOf(call.host, gadgetId);
  const i = q.pending.findIndex((a) => a.id === id);
  if (i >= 0) {
    q.pending.splice(i, 1);
    return;
  }
  if (!closeAsk(call.host, gadgetId, id, reason)) throw new Error(`no open ask ${id}`);
}

// ---- post, card, image, act, settings -----------------------------------------------------------
function postCommand(call: CommandCall): Record<string, unknown> {
  const s = call.session();
  const record = s.record!;
  const kind = call.cmd.kind as PostKind;
  if (kind !== "routine" && kind !== "message") throw new Error("kind must be routine or message");
  const speak = call.cmd.speak ?? record.settings.speak_pushes;
  if (typeof speak !== "boolean") throw new Error("speak must be a boolean");
  const id = hostId(call, "p_");
  const bot: BotRef = { id: record.bot.id, name: foldLatin1(record.bot.name) };
  s.send({ op: "post", id, bot, kind, text: foldLatin1(text(call, "text")), speak });
  if (speak) void speechOf(s).play(undefined, call.host.options.toneMs);
  return { id };
}

function cardCommand(call: CommandCall): Record<string, unknown> {
  const s = call.session();
  const id = hostId(call, "c_");
  s.send({
    op: "card", id, title: cutChars(foldLatin1(text(call, "title")), CARD_TITLE_MAX),
    body: cutChars(foldLatin1(text(call, "body", false)), CARD_BODY_MAX), ttl_s: ttl(call),
  });
  return { id };
}

/** RGB565 little-endian pixels: "bars" (eight vertical colour bars) or a "#rrggbb" fill. */
export function imagePixels(w: number, h: number, pattern: string): Uint8Array {
  const rgb565 = (r: number, g: number, b: number): number => ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
  const BARS = [[255, 255, 255], [255, 255, 0], [0, 255, 255], [0, 255, 0], [255, 0, 255], [255, 0, 0], [0, 0, 255], [0, 0, 0]];
  let solid: number | null = null;
  if (pattern !== "bars") {
    const m = /^#([0-9a-fA-F]{2})([0-9a-fA-F]{2})([0-9a-fA-F]{2})$/.exec(pattern);
    if (!m) throw new Error('pattern must be "bars" or "#rrggbb"');
    solid = rgb565(parseInt(m[1], 16), parseInt(m[2], 16), parseInt(m[3], 16));
  }
  const out = new Uint8Array(w * h * 2);
  const view = new DataView(out.buffer);
  for (let y = 0; y < h; y++) {
    for (let x = 0; x < w; x++) {
      const c = solid ?? rgb565(...(BARS[Math.floor((x * 8) / w)] as [number, number, number]));
      view.setUint16((y * w + x) * 2, c, true);
    }
  }
  return out;
}

async function imageCommand(call: CommandCall): Promise<Record<string, unknown>> {
  const s = call.session();
  const cap = s.hello!.caps.image;
  const { w, h } = call.cmd;
  if (!cap) throw new Error("this gadget has no image caps");
  if (typeof w !== "number" || typeof h !== "number" || !Number.isInteger(w) || !Number.isInteger(h) || w < 1 || h < 1) {
    throw new Error("w and h must be positive integers");
  }
  if (w > cap.w || h > cap.h) throw new Error(`image must fit ${cap.w}x${cap.h}`);
  const pixels = imagePixels(w, h, typeof call.cmd.pattern === "string" ? call.cmd.pattern : "bars");
  const id = hostId(call, "i_");
  const stream = s.allocStream();
  try {
    s.send({ op: "image.begin", id, stream, w, h, ttl_s: ttl(call) });
    for (let at = 0; at < pixels.length; at += IMAGE_FRAME_PAYLOAD_MAX) {
      if (!(await s.sendBinary(BinaryKind.image, stream, pixels.subarray(at, at + IMAGE_FRAME_PAYLOAD_MAX)))) {
        throw new Error("the gadget disconnected");
      }
    }
    s.send({ op: "image.end", stream });
  } finally {
    s.releaseStream(stream);
  }
  return { id, stream };
}

function actCommand(call: CommandCall): Record<string, unknown> {
  const s = call.session();
  const gadget = s.record!.id;
  const name = text(call, "name");
  const args = call.cmd.args ?? {};
  if (typeof args !== "object" || args === null || Array.isArray(args)) throw new Error("args must be an object");
  const id = hostId(call, "x_");
  const off = s.onOp("act.result", (m) => {
    if (m.id !== id) return;
    finish();
    const e: Record<string, unknown> = { event: "act.result", gadget, id, ok: m.ok === true };
    if (m.data !== undefined) e.data = m.data;
    if (m.error !== undefined) e.error = m.error;
    call.host.emit(e as { event: string });
  });
  const timer = setTimeout(() => {
    finish();
    call.host.emit({ event: "act.result", gadget, id, timeout: true });
  }, call.host.options.actTimeoutMs);
  const offClose = s.onClose(() => clearTimeout(timer));
  function finish(): void {
    off();
    offClose();
    clearTimeout(timer);
  }
  s.send({ op: "act", id, name, args: args as Record<string, unknown> });
  return { id };
}

function settingsCommand(call: CommandCall): void {
  const id = call.gadgetId();
  const record = call.host.state.gadgets.get(id);
  if (!record) throw new Error(`unknown gadget ${id}`);
  const { bot, speak_pushes: speak, name } = call.cmd as { bot?: unknown; speak_pushes?: unknown; name?: unknown };
  if (bot !== undefined) {
    const b = bot as Record<string, unknown>;
    if (typeof b !== "object" || b === null || typeof b.id !== "string" || typeof b.name !== "string") throw new Error("bot must be {id, name}");
    record.bot = { id: b.id, name: b.name };
  }
  if (speak !== undefined) {
    if (typeof speak !== "boolean") throw new Error("speak_pushes must be a boolean");
    record.settings = { speak_pushes: speak };
  }
  if (name !== undefined) {
    if (typeof name !== "string" || name.trim() === "") throw new Error("name must be a non-empty string");
    record.name = cutChars(name, 32);
  }
  const s = call.host.live(id);
  if (!s && name !== undefined) record.namePending = true;
  call.host.state.save();
  if (s) {
    const msg = { op: "settings" as const, bot: { id: record.bot.id, name: foldLatin1(record.bot.name) }, settings: { ...record.settings } };
    s.send(name === undefined ? msg : { ...msg, name: foldLatin1(record.name) });
  }
}

export const displayFeature: Feature = {
  attach(session: GadgetSession, host: HostContext) {
    const gadgetId = session.record!.id;
    const q = queueOf(host, gadgetId);
    if (q.current) session.send(q.current);
    session.onOp("answer", (m) => {
      const cur = q.current;
      if (!cur || m.id !== cur.id || !cur.options.some((o) => o.id === m.option)) return;
      host.emit({ event: "answer", gadget: gadgetId, id: m.id, option: m.option });
      closeAsk(host, gadgetId, cur.id, "answered");
    });
  },
  commands: {
    ask: askCommand,
    "ask.close": askCloseCommand,
    post: postCommand,
    card: cardCommand,
    "card.close": (call) => {
      call.session().send({ op: "card.close", id: text(call, "id") });
    },
    image: imageCommand,
    act: actCommand,
    settings: settingsCommand,
  },
};
```

- [ ] **Step 4: Replace `tools/fake-host/src/features.ts`**

```ts
// SPDX-License-Identifier: Apache-2.0
// The features every fake host runs. A later task adds OTA here.
import type { Feature } from "./context.ts";
import { displayFeature } from "./display.ts";
import { voiceFeature } from "./voice.ts";

export const FEATURES: Feature[] = [voiceFeature, displayFeature];
```

- [ ] **Step 5: Run the fake-host tests to see them pass**

Run: `npm run test:fake-host`
Expected: PASS (48).

- [ ] **Step 6: Commit**

```bash
git add tools/fake-host/src/display.ts tools/fake-host/src/features.ts tools/fake-host/test/display.test.ts
git commit -m "feat(fake-host): asks, posts, cards, images, actions and settings" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 11: Firmware updates signed with the test key

**Files:**
- Create: `tools/fake-host/test/ota.test.ts`, `tools/fake-host/src/ota.ts`
- Modify: `tools/fake-host/src/features.ts` (replace the file)

**Interfaces:**
- Consumes: `GadgetSession` (`allocStream`, `sendBinary`, `onOp`, `onClose`, `hello.caps.ota`), `HostContext.options` (`otaKeyFile`, `otaKeyId`, `fwReadyTimeoutMs`), `signP256`, `firmwareText`, `b64Encode`, `encodeFwChunk`; constants `BOARD_ID_RE`, `FW_CHUNK_BYTES`, `FW_WINDOW_BYTES`.
- Produces (`ota.ts`): `type Tamper = "sig" | "sha256" | "size"`; `interface Offer {board, version, size, sha256, sig, key_id}`; `buildOffer({image, board, version, keyHex, keyId, otaMax, tamper?}): Offer`; `otaFeature: Feature` with command `ota {image, version, board?, tamper?}`. Events: `ota {gadget, phase: offered | ready | progress | committed | installed | failed, …}`. P2a's e2e runner drives this.

- [ ] **Step 1: Write the failing test `tools/fake-host/test/ota.test.ts`**

It carries a minimal gadget-side OTA (checks in the §4.8 order, contiguous chunks, progress at each 16 KiB crossing and at the end, size and SHA-256 at commit). That gadget acknowledges every 16 KiB as soon as it reads it, so it can never see the host overrun the window; the last test withholds `fw.progress` instead and checks that the host stops after exactly 64 KiB and that each acknowledgement lets exactly that many bytes more through.

```ts
// SPDX-License-Identifier: Apache-2.0
import { test, type TestContext } from "node:test";
import assert from "node:assert/strict";
import { createHash, randomBytes } from "node:crypto";
import { mkdtempSync, readFileSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { b64DecodeCanonical } from "../../../protocol/lib/encoding.ts";
import { decodeFwChunk } from "../../../protocol/lib/frames.ts";
import { firmwareText } from "../../../protocol/lib/identity.ts";
import { verifyP256 } from "../../../protocol/lib/verify.ts";
import { startFakeHost, type FakeHost } from "../src/server.ts";
import type { HostEvent } from "../src/context.ts";
import { buildOffer } from "../src/ota.ts";
import { CAPS, connectGadget, nextEvent, randomKey, type TestGadget } from "./gadget-client.ts";

const T1_PUB = b64DecodeCanonical(readFileSync(new URL("../../../keys/test-t1.pub.b64", import.meta.url), "utf8").trim())!;
const T1_KEY = readFileSync(new URL("../../../keys/test-t1.key.hex", import.meta.url), "utf8").trim();

/** The gadget side of §4.8, as the firmware does it: checks in order, contiguous chunks,
 *  progress every 16 KiB and at the end, size + SHA-256 at commit. */
async function gadgetOta(g: TestGadget, own: { board: string; fw: string; otaMax: number }): Promise<{ code?: string; image?: Uint8Array; maxUnacked: number }> {
  const offer = await g.next("fw.offer");
  const fail = (code: string) => {
    g.send({ op: "fw.fail", stream: offer.stream, code });
    return { code, maxUnacked: 0 };
  };
  if (offer.board !== own.board) return fail("wrong_board");
  if (offer.version === own.fw) return fail("same_version");
  if (offer.size > own.otaMax) return fail("too_large");
  if (offer.key_id !== "t1") return fail("unknown_key");
  const sig = b64DecodeCanonical(offer.sig);
  if (!sig || !verifyP256(T1_PUB, firmwareText(own.board, offer.version, offer.size, offer.sha256), sig)) return fail("bad_sig");
  g.send({ op: "fw.ready", stream: offer.stream });
  const image = new Uint8Array(offer.size);
  let written = 0;
  let acked = 0;
  let maxUnacked = 0;
  while (written < offer.size) {
    const f = await g.nextBinary(4);
    const chunk = decodeFwChunk(f.payload)!;
    if (f.stream !== offer.stream || chunk.offset !== written) return fail("sequence");
    image.set(chunk.data, written);
    written += chunk.data.length;
    maxUnacked = Math.max(maxUnacked, written - acked);
    if (Math.floor(written / 16384) > Math.floor(acked / 16384) || written === offer.size) {
      g.send({ op: "fw.progress", stream: offer.stream, offset: written });
      acked = written;
    }
  }
  await g.next("fw.commit");
  if (createHash("sha256").update(image).digest("hex") !== offer.sha256) return { ...fail("checksum"), maxUnacked };
  return { image, maxUnacked };
}

async function setup(t: TestContext, o: Record<string, unknown> = {}): Promise<{ h: FakeHost; events: HostEvent[]; key: string; gadget: TestGadget; imagePath: string; image: Uint8Array }> {
  const events: HostEvent[] = [];
  const h = await startFakeHost({ port: 0, quiet: true, code: "123456", ...o }, { listener: (e) => events.push(e) });
  t.after(() => h.close());
  const key = randomKey();
  const { gadget } = await connectGadget({ port: h.port, enroll: "123456", key, fw: "1.0.0" });
  const image = new Uint8Array(randomBytes(200_000));
  const imagePath = join(mkdtempSync(join(tmpdir(), "fake-host-ota-")), "app.bin");
  writeFileSync(imagePath, image);
  return { h, events, key, gadget, imagePath, image };
}

test("buildOffer signs the §4.8 text with t1, and each tamper breaks exactly one check", () => {
  const image = new Uint8Array([1, 2, 3]);
  const base = { image, board: "amoled-175c", version: "1.1.0", keyHex: T1_KEY, keyId: "t1", otaMax: 6291456 };
  const ok = buildOffer(base);
  assert.equal(ok.size, 3);
  assert.equal(ok.sha256, createHash("sha256").update(image).digest("hex"));
  assert.ok(verifyP256(T1_PUB, firmwareText("amoled-175c", "1.1.0", 3, ok.sha256), b64DecodeCanonical(ok.sig)!));
  const sig = buildOffer({ ...base, tamper: "sig" });
  assert.ok(!verifyP256(T1_PUB, firmwareText("amoled-175c", "1.1.0", 3, sig.sha256), b64DecodeCanonical(sig.sig)!));
  const sha = buildOffer({ ...base, tamper: "sha256" });
  assert.notEqual(sha.sha256, ok.sha256);
  assert.ok(verifyP256(T1_PUB, firmwareText("amoled-175c", "1.1.0", 3, sha.sha256), b64DecodeCanonical(sha.sig)!));
  const size = buildOffer({ ...base, tamper: "size" });
  assert.equal(size.size, 6291457);
  assert.ok(verifyP256(T1_PUB, firmwareText("amoled-175c", "1.1.0", 6291457, size.sha256), b64DecodeCanonical(size.sig)!));
});

test("a full update: offer, ready, windowed chunks, progress, commit, then fw.installed after the restart", async (t) => {
  const { h, events, key, gadget, imagePath, image } = await setup(t);
  const ack = await h.command({ cmd: "ota", image: imagePath, version: "1.1.0" });
  assert.equal(ack.ok, true);
  assert.equal(ack.size, image.length);
  const r = await gadgetOta(gadget, { board: "amoled-175c", fw: "1.0.0", otaMax: 6291456 });
  assert.equal(r.code, undefined);
  assert.deepEqual(r.image, image);
  assert.ok(r.maxUnacked <= 65536, `host kept ${r.maxUnacked} bytes unacknowledged`);
  const offer = gadget.all.find((m) => m.op === "fw.offer")!;
  assert.deepEqual([offer.board, offer.version, offer.key_id, offer.size], ["amoled-175c", "1.1.0", "t1", image.length]);
  await gadget.close();
  const { gadget: rebooted } = await connectGadget({ port: h.port, key, fw: "1.1.0" });
  const installed = nextEvent(h, (e) => e.event === "ota" && e.phase === "installed");
  rebooted.send({ op: "fw.installed", version: "1.1.0" });
  assert.deepEqual(await installed, { event: "ota", gadget: rebooted.id, phase: "installed", version: "1.1.0" });
  const phases = events.filter((e) => e.event === "ota").map((e) => e.phase);
  assert.deepEqual([phases[0], phases[1], phases.at(-2), phases.at(-1)], ["offered", "ready", "committed", "installed"]);
  assert.ok(phases.filter((p) => p === "progress").length >= Math.floor(image.length / 16384));
});

test("refused offers: bad_sig, too_large, wrong_board, same_version are reported as failed", async (t) => {
  const { h, gadget, imagePath } = await setup(t);
  const own = { board: "amoled-175c", fw: "1.0.0", otaMax: 6291456 };
  const cases: Array<[Record<string, unknown>, string]> = [
    [{ tamper: "sig" }, "bad_sig"], [{ tamper: "size" }, "too_large"], [{ board: "lcd-154" }, "wrong_board"], [{ version: "1.0.0" }, "same_version"],
  ];
  for (const [extra, code] of cases) {
    const failed = nextEvent(h, (e) => e.event === "ota" && e.phase === "failed");
    assert.equal((await h.command({ cmd: "ota", image: imagePath, version: "1.1.0", ...extra })).ok, true);
    assert.equal((await gadgetOta(gadget, own)).code, code);
    assert.equal((await failed).code, code);
  }
});

test("a tampered sha256 is accepted at offer time and fails with checksum at commit", async (t) => {
  const { h, gadget, imagePath } = await setup(t);
  const failed = nextEvent(h, (e) => e.event === "ota" && e.phase === "failed", 5000);
  await h.command({ cmd: "ota", image: imagePath, version: "1.1.0", tamper: "sha256" });
  assert.equal((await gadgetOta(gadget, { board: "amoled-175c", fw: "1.0.0", otaMax: 6291456 })).code, "checksum");
  assert.equal((await failed).code, "checksum");
});

test("no fw.ready within the timeout: the host gives up; only one update at a time; ota needs caps", async (t) => {
  const { h, gadget, imagePath } = await setup(t, { fwReadyTimeoutMs: 150 });
  const failed = nextEvent(h, (e) => e.event === "ota" && e.phase === "failed");
  await h.command({ cmd: "ota", image: imagePath, version: "1.1.0" });
  assert.equal((await h.command({ cmd: "ota", image: imagePath, version: "1.1.0" })).error, "an update is already running on this gadget");
  await gadget.next("fw.offer");
  assert.equal((await failed).code, "ready_timeout");
  const { gadget: plain } = await connectGadget({ port: h.port, enroll: undefined, key: gadget.key, caps: { ...CAPS, ota: undefined } });
  assert.equal((await h.command({ cmd: "ota", gadget: plain.id, image: imagePath, version: "1.1.0" })).error, "this gadget has no ota caps");
});

test("the host keeps at most 64 KiB unacknowledged and each fw.progress opens the window again", async (t) => {
  const { h, gadget, imagePath } = await setup(t);
  await h.command({ cmd: "ota", image: imagePath, version: "1.1.0" });
  const offer = await gadget.next("fw.offer");
  gadget.send({ op: "fw.ready", stream: offer.stream });
  const drain = async (): Promise<number> => {
    let bytes = 0;
    for (;;) {
      try {
        bytes += decodeFwChunk((await gadget.nextBinary(4, 300)).payload)!.data.length;
      } catch {
        return bytes;
      }
    }
  };
  assert.equal(await drain(), 65536, "without fw.progress the host stops after one 64 KiB window");
  gadget.send({ op: "fw.progress", stream: offer.stream, offset: 16384 });
  assert.equal(await drain(), 16384, "acknowledging 16 KiB lets exactly 16 KiB more through");
});
```

- [ ] **Step 2: Run it to see it fail**

Run: `node --test tools/fake-host/test/ota.test.ts`
Expected: FAIL with `ERR_MODULE_NOT_FOUND` for `tools/fake-host/src/ota.ts`.

- [ ] **Step 3: Create `tools/fake-host/src/ota.ts`**

`fw.*` replies are queued as they arrive, so a `fw.progress` that lands while the host is still sending chunks is never lost (losing one can stall a full window).

```ts
// SPDX-License-Identifier: Apache-2.0
// Firmware updates (PROTOCOL.md §4.8): offer an image signed with the test key, stream it in
// 4 KiB chunks with at most 64 KiB unacknowledged, commit, and report fw.installed.
import { createHash } from "node:crypto";
import { readFileSync } from "node:fs";
import { b64Encode } from "../../../protocol/lib/encoding.ts";
import { firmwareText } from "../../../protocol/lib/identity.ts";
import { signP256 } from "../../../protocol/lib/verify.ts";
import { encodeFwChunk } from "../../../protocol/lib/frames.ts";
import { BOARD_ID_RE, FW_CHUNK_BYTES, FW_WINDOW_BYTES } from "../../../protocol/lib/types.ts";
import type { CommandCall, Feature, HostContext } from "./context.ts";
import type { GadgetSession } from "./session.ts";

export type Tamper = "sig" | "sha256" | "size";

export interface Offer { board: string; version: string; size: number; sha256: string; sig: string; key_id: string }

/** Builds the fw.offer fields. tamper "sig" flips the signature's last byte (gadget: bad_sig);
 *  "sha256" signs the hash of a different image (gadget: checksum at commit); "size" offers
 *  caps.ota.max + 1 bytes, correctly signed (gadget: too_large). */
export function buildOffer(input: {
  image: Uint8Array; board: string; version: string; keyHex: string; keyId: string; otaMax: number; tamper?: Tamper;
}): Offer {
  const { image, board, version, tamper } = input;
  let sha256 = createHash("sha256").update(image).digest("hex");
  let size = image.length;
  if (tamper === "sha256") {
    const other = Uint8Array.from(image);
    other[0] ^= 0xff;
    sha256 = createHash("sha256").update(other).digest("hex");
  }
  if (tamper === "size") size = input.otaMax + 1;
  const der = signP256(input.keyHex, firmwareText(board, version, size, sha256));
  if (tamper === "sig") der[der.length - 1] ^= 0x01;
  return { board, version, size, sha256, sig: b64Encode(der), key_id: input.keyId };
}

const running = new WeakSet<GadgetSession>();

async function runOta(host: HostContext, s: GadgetSession, image: Uint8Array, offer: Offer): Promise<void> {
  const gadget = s.record!.id;
  const emit = (phase: string, extra: Record<string, unknown> = {}): void => host.emit({ event: "ota", gadget, phase, ...extra });
  const stream = s.allocStream();
  running.add(s);
  // Every fw.* frame for this stream, and a "closed" marker, queue up here until read.
  const inbox: any[] = [];
  let waiter: ((m: any) => void) | null = null;
  const deliver = (m: any): void => {
    if (waiter) {
      const w = waiter;
      waiter = null;
      w(m);
    } else inbox.push(m);
  };
  const offs = ["fw.ready", "fw.fail", "fw.progress"].map((op) => s.onOp(op, (m) => {
    if (m.stream === stream) deliver(m);
  }));
  let closed = false;
  const offClose = s.onClose(() => {
    closed = true;
    deliver({ op: "closed" });
  });
  const next = (timeoutMs: number): Promise<any> => {
    if (inbox.length > 0) return Promise.resolve(inbox.shift());
    return new Promise((resolve) => {
      const t = setTimeout(() => {
        waiter = null;
        resolve({ op: "timeout" });
      }, timeoutMs);
      waiter = (m) => {
        clearTimeout(t);
        resolve(m);
      };
    });
  };
  try {
    s.send({ op: "fw.offer", stream, ...offer });
    emit("offered", { size: offer.size, version: offer.version });
    const answer = await next(host.options.fwReadyTimeoutMs);
    if (answer.op === "fw.fail") return emit("failed", { code: answer.code });
    if (answer.op === "timeout") return emit("failed", { code: "ready_timeout" });
    if (answer.op === "closed") return emit("failed", { code: "disconnected" });
    if (answer.op !== "fw.ready") return emit("failed", { code: "unexpected" });
    emit("ready");
    let sent = 0;
    let acked = 0;
    const total = image.length;
    while (acked < total) {
      while (sent < total && sent - acked < FW_WINDOW_BYTES) {
        const n = Math.min(FW_CHUNK_BYTES, total - sent, FW_WINDOW_BYTES - (sent - acked));
        const frame = encodeFwChunk(stream, sent, image.subarray(sent, sent + n));
        if (!(await s.sendBinary(4, stream, frame.subarray(2)))) return emit("failed", { code: "disconnected" });
        sent += n;
      }
      const m = await next(30_000);
      if (m.op === "fw.fail") return emit("failed", { code: m.code });
      if (m.op === "closed") return emit("failed", { code: "disconnected" });
      if (m.op === "timeout") return emit("failed", { code: "progress_timeout" });
      if (m.op === "fw.progress" && typeof m.offset === "number" && m.offset > acked && m.offset <= sent) {
        acked = m.offset;
        emit("progress", { offset: acked });
      }
    }
    s.send({ op: "fw.commit", stream });
    emit("committed");
    const after = await next(30_000);
    if (after.op === "fw.fail") emit("failed", { code: after.code });
  } finally {
    for (const off of offs) off();
    offClose();
    s.releaseStream(stream);
    running.delete(s);
    if (closed) host.log(`${gadget}: connection closed during or after the update`);
  }
}

function otaCommand(call: CommandCall): Record<string, unknown> {
  const s = call.session();
  const caps = s.hello!.caps;
  if (!caps.ota || typeof caps.ota.max !== "number") throw new Error("this gadget has no ota caps");
  if (running.has(s)) throw new Error("an update is already running on this gadget");
  const { image: path, version } = call.cmd;
  if (typeof path !== "string") throw new Error("image must be a file path");
  if (typeof version !== "string" || version === "") throw new Error("version must be a non-empty string");
  const board = call.cmd.board ?? s.hello!.board;
  if (typeof board !== "string" || !BOARD_ID_RE.test(board)) throw new Error("board must be a board id");
  const tamper = call.cmd.tamper as Tamper | undefined;
  if (tamper !== undefined && !["sig", "sha256", "size"].includes(tamper)) throw new Error("tamper must be sig, sha256 or size");
  const image = new Uint8Array(readFileSync(path));
  if (image.length === 0) throw new Error("the image is empty");
  const keyHex = readFileSync(call.host.options.otaKeyFile, "utf8").trim();
  const offer = buildOffer({ image, board, version, keyHex, keyId: call.host.options.otaKeyId, otaMax: caps.ota.max, tamper });
  void runOta(call.host, s, image, offer);
  return { size: offer.size, sha256: offer.sha256 };
}

export const otaFeature: Feature = {
  attach(session, host) {
    session.onOp("fw.installed", (m) => {
      host.emit({ event: "ota", gadget: session.record!.id, phase: "installed", version: m.version });
    });
  },
  commands: { ota: otaCommand },
};
```

- [ ] **Step 4: Replace `tools/fake-host/src/features.ts`**

```ts
// SPDX-License-Identifier: Apache-2.0
// The features every fake host runs.
import type { Feature } from "./context.ts";
import { displayFeature } from "./display.ts";
import { otaFeature } from "./ota.ts";
import { voiceFeature } from "./voice.ts";

export const FEATURES: Feature[] = [voiceFeature, displayFeature, otaFeature];
```

- [ ] **Step 5: Run the fake-host tests to see them pass**

Run: `npm run test:fake-host`
Expected: PASS (54).

- [ ] **Step 6: Commit**

```bash
git add tools/fake-host/src/ota.ts tools/fake-host/src/features.ts tools/fake-host/test/ota.test.ts
git commit -m "feat(fake-host): firmware updates signed with the test key" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 12: JSON-lines CLI, README and the CI fake-host job

**Files:**
- Create: `tools/fake-host/test/cli.test.ts`, `tools/fake-host/test/modules.test.ts`, `tools/fake-host/src/control.ts`, `tools/fake-host/src/main.ts`, `tools/fake-host/README.md`
- Modify: `.github/workflows/ci.yml` (append the `fake-host` job)

**Interfaces:**
- Consumes: `startFakeHost`, `FakeHost`, `parseCli`, `Ack`, `Command`, `HostEvent`.
- Produces: the CLI `node tools/fake-host/src/main.ts [options]` of contract §4.7 (exit 0 after `quit`, on stdin end or SIGINT/SIGTERM; 2 on bad options; 1 when it cannot listen), and in `control.ts`: `type ParsedLine`, `parseCommandLine(line: string): ParsedLine`, `formatEvent(event: HostEvent): string`, `runControl(host, input: Readable, write): Promise<"quit" | "eof">`. P2a's e2e runner and P2d's `AGENTS.md` use the CLI and README.

- [ ] **Step 1: Write the failing test `tools/fake-host/test/cli.test.ts`**

```ts
// SPDX-License-Identifier: Apache-2.0
import { test } from "node:test";
import assert from "node:assert/strict";
import { spawn, type ChildProcessWithoutNullStreams } from "node:child_process";
import { createInterface } from "node:readline";
import { fileURLToPath } from "node:url";
import { parseCommandLine, formatEvent } from "../src/control.ts";
import { connectGadget } from "./gadget-client.ts";

const MAIN = fileURLToPath(new URL("../src/main.ts", import.meta.url));

/** Spawns the CLI and collects its stdout events; `next` waits for the first unread match. */
function spawnHost(args: string[]): { child: ChildProcessWithoutNullStreams; next: (m: (e: any) => boolean) => Promise<any>; send: (line: string) => void; exit: Promise<number | null> } {
  const child = spawn(process.execPath, [MAIN, ...args], { stdio: ["pipe", "pipe", "pipe"] });
  const seen: any[] = [];
  const waiting: Array<{ m: (e: any) => boolean; resolve: (e: any) => void }> = [];
  createInterface({ input: child.stdout }).on("line", (line) => {
    const e = JSON.parse(line);
    const w = waiting.findIndex((x) => x.m(e));
    if (w >= 0) waiting.splice(w, 1)[0].resolve(e);
    else seen.push(e);
  });
  const exit = new Promise<number | null>((resolve) => child.on("exit", (code) => resolve(code)));
  return {
    child,
    exit,
    send: (line) => child.stdin.write(line + "\n"),
    next: (m) => {
      const i = seen.findIndex(m);
      if (i >= 0) return Promise.resolve(seen.splice(i, 1)[0]);
      return new Promise((resolve, reject) => {
        const entry = { m, resolve: (e: any) => { clearTimeout(t); resolve(e); } };
        const t = setTimeout(() => reject(new Error("timed out waiting for a CLI event")), 5000);
        waiting.push(entry);
      });
    },
  };
}

test("parseCommandLine accepts command objects only", () => {
  assert.equal(parseCommandLine("   "), null);
  assert.deepEqual(parseCommandLine('{"cmd":"post","kind":"message","text":"hi"}'), { ok: true, cmd: { cmd: "post", kind: "message", text: "hi" } });
  assert.deepEqual(parseCommandLine("nope"), { ok: false, error: "not JSON" });
  assert.deepEqual(parseCommandLine("[1]"), { ok: false, error: "not a JSON object" });
  assert.deepEqual(parseCommandLine('{"x":1}'), { ok: false, error: "missing cmd" });
  assert.deepEqual(parseCommandLine('{"cmd":"card","gadget":5}'), { ok: false, error: "gadget must be a string" });
  assert.equal(formatEvent({ event: "ack", cmd: "x", ok: true }), '{"event":"ack","cmd":"x","ok":true}\n');
});

test("the CLI prints listening and code, enrolls a gadget, runs stdin commands and quits with 0", async () => {
  const h = spawnHost(["--port", "0", "--code", "123456", "--quiet"]);
  const listening = await h.next((e) => e.event === "listening");
  assert.match(listening.host_id, /^[0-9a-f]{32}$/);
  assert.equal((await h.next((e) => e.event === "code")).code, "123456");
  const { gadget, result } = await connectGadget({ port: listening.port, enroll: "123456" });
  assert.equal(result.op, "ready");
  assert.equal((await h.next((e) => e.event === "enrolled")).gadget, gadget.id);
  assert.equal((await h.next((e) => e.event === "ready")).gadget, gadget.id);
  h.send('{"cmd":"card","title":"From stdin","body":"ok"}');
  assert.equal((await gadget.next("card")).title, "From stdin");
  assert.equal((await h.next((e) => e.event === "ack" && e.cmd === "card")).ok, true);
  h.send("garbage");
  assert.deepEqual(await h.next((e) => e.event === "ack" && e.cmd === null), { event: "ack", cmd: null, ok: false, error: "not JSON" });
  h.send('{"cmd":"quit"}');
  assert.equal((await h.next((e) => e.event === "ack" && e.cmd === "quit")).ok, true);
  assert.equal((await gadget.closed).code, 1001);
  assert.equal(await h.exit, 0);
});

test("the CLI exits 0 when stdin ends and 2 on bad options", async () => {
  const h = spawnHost(["--port", "0", "--quiet"]);
  assert.match(String((await h.next((e) => e.event === "code")).code), /^\d{6}$/);
  h.child.stdin.end();
  assert.equal(await h.exit, 0);
  const bad = spawn(process.execPath, [MAIN, "--code", "12"], { stdio: ["pipe", "pipe", "pipe"] });
  let stderr = "";
  bad.stderr.on("data", (d) => (stderr += d));
  assert.equal(await new Promise((r) => bad.on("exit", r)), 2);
  assert.match(stderr, /--code must be six digits/);
});
```

- [ ] **Step 2: Write `tools/fake-host/test/modules.test.ts`**

This is the syntax gate for erasable TypeScript: it imports every library and fake-host module (there is no TypeScript compiler in the pinned dependency set, and `node --check` exits 0 for a `.ts` file even when it has a syntax error, so it gates nothing).

```ts
// SPDX-License-Identifier: Apache-2.0
// Every module loads under Node's type stripping (erasable TypeScript only) without side effects.
// main.ts and gen-vectors.ts are programs; cli.test.ts and `npm run vectors:check` run them.
import { test } from "node:test";
import assert from "node:assert/strict";
import { readdirSync } from "node:fs";

const dirs = ["../../../protocol/lib/", "../src/"];

test("every protocol/lib and fake-host module imports cleanly", async () => {
  let n = 0;
  for (const dir of dirs) {
    for (const f of readdirSync(new URL(dir, import.meta.url))) {
      if (!f.endsWith(".ts") || f === "main.ts") continue;
      await import(new URL(dir + f, import.meta.url).href);
      n++;
    }
  }
  assert.ok(n >= 20, `imported ${n} modules`);
});
```

- [ ] **Step 3: Run the CLI test to see it fail**

Run: `node --test tools/fake-host/test/cli.test.ts`
Expected: FAIL with `ERR_MODULE_NOT_FOUND` for `tools/fake-host/src/control.ts`. (`modules.test.ts` would also fail now, with `imported 19 modules`: it expects at least 20 once `control.ts` exists.)

- [ ] **Step 4: Create `tools/fake-host/src/control.ts`**

```ts
// SPDX-License-Identifier: Apache-2.0
// The JSON-lines control interface (contract 00-interfaces.md §4.7): commands on stdin, events on
// stdout, one JSON object per line.
import { createInterface } from "node:readline";
import type { Readable } from "node:stream";
import type { Ack, Command, HostEvent } from "./context.ts";
import type { FakeHost } from "./server.ts";

export type ParsedLine = { ok: true; cmd: Command } | { ok: false; error: string } | null;

/** null for a blank line; otherwise a command object with a string `cmd`, or why not. */
export function parseCommandLine(line: string): ParsedLine {
  if (line.trim() === "") return null;
  let value: unknown;
  try {
    value = JSON.parse(line);
  } catch {
    return { ok: false, error: "not JSON" };
  }
  if (typeof value !== "object" || value === null || Array.isArray(value)) return { ok: false, error: "not a JSON object" };
  const cmd = value as Command;
  if (typeof cmd.cmd !== "string") return { ok: false, error: "missing cmd" };
  if (cmd.gadget !== undefined && typeof cmd.gadget !== "string") return { ok: false, error: "gadget must be a string" };
  return { ok: true, cmd };
}

export function formatEvent(event: HostEvent): string {
  return JSON.stringify(event) + "\n";
}

/** Runs commands from `input` one at a time, in order. Resolves "quit" after a `quit` command and
 *  "eof" when the input ends. Lines that are not commands get an ack with cmd null, through `write`. */
export async function runControl(host: FakeHost, input: Readable, write: (event: HostEvent) => void): Promise<"quit" | "eof"> {
  const lines = createInterface({ input, crlfDelay: Infinity });
  for await (const line of lines) {
    const parsed = parseCommandLine(line);
    if (parsed === null) continue;
    if (!parsed.ok) {
      const ack: Ack = { event: "ack", cmd: null, ok: false, error: parsed.error };
      write(ack);
      continue;
    }
    const ack = await host.command(parsed.cmd);
    if (parsed.cmd.cmd === "quit" && ack.ok) {
      lines.close();
      return "quit";
    }
  }
  return "eof";
}
```

- [ ] **Step 5: Create `tools/fake-host/src/main.ts`**

```ts
// SPDX-License-Identifier: Apache-2.0
// node tools/fake-host/src/main.ts [options]   (see tools/fake-host/README.md)
// Events go to stdout as JSON lines, logs to stderr; commands are read from stdin. The process
// exits 0 after `quit`, when stdin ends, or on SIGINT/SIGTERM; 2 on bad options; 1 if it cannot start.
import { formatEvent, runControl } from "./control.ts";
import { parseCli, type FakeHostOptions } from "./options.ts";
import { startFakeHost, type FakeHost } from "./server.ts";

const USAGE = "usage: node tools/fake-host/src/main.ts [--port n] [--bind addr] [--code 6digits] [--code-ttl s] [--state dir]\n"
  + "  [--host-id 32hex] [--host-name text] [--bot id:name] [--heard text] [--reply text] [--tone-ms n]\n"
  + "  [--ota-key file] [--ota-key-id id] [--max-devices n] [--done-before-speech] [--quiet]\n";

let options: FakeHostOptions;
try {
  options = parseCli(process.argv.slice(2));
} catch (err) {
  process.stderr.write(`fake-host: ${(err as Error).message}\n${USAGE}`);
  process.exit(2);
}

const write = (event: Parameters<typeof formatEvent>[0]): void => {
  process.stdout.write(formatEvent(event));
};

let host: FakeHost;
try {
  host = await startFakeHost(options, { listener: write });
} catch (err) {
  process.stderr.write(`fake-host: cannot start: ${(err as Error).message}\n`);
  process.exit(1);
}

let stopping = false;
async function stop(): Promise<never> {
  if (!stopping) {
    stopping = true;
    await host.close();
  }
  process.exit(0);
}
process.on("SIGINT", () => void stop());
process.on("SIGTERM", () => void stop());

await runControl(host, process.stdin, write);
await stop();
```

- [ ] **Step 6: Run the fake-host tests to see them pass**

Run: `npm run test:fake-host`
Expected: PASS (58).

- [ ] **Step 7: Try the CLI by hand**

```bash
printf '%s\n' '{"cmd":"code","code":"654321"}' '{"cmd":"nope"}' '{"cmd":"quit"}' | node tools/fake-host/src/main.ts --port 0 --code 123456 --quiet
```

Expected (the port and `host_id` vary):

```
{"event":"listening","port":…,"host_id":"…"}
{"event":"code","code":"123456","expires_at":…}
{"event":"code","code":"654321","expires_at":…}
{"event":"ack","cmd":"code","ok":true,"code":"654321","expires_at":…}
{"event":"ack","cmd":"nope","ok":false,"error":"unknown command nope"}
{"event":"ack","cmd":"quit","ok":true}
```

- [ ] **Step 8: Create `tools/fake-host/README.md`**

````markdown
# Fake host

A Node stand-in for MausBot that speaks the host side of [`openmausbot-gadget/1`](../../protocol/PROTOCOL.md). It is the server for the simulator's end-to-end tests and for gadget work without MausBot.

```
npm ci
node tools/fake-host/src/main.ts --port 8810 --code 123456
```

It prints one six-digit pairing code and enrolls only with that code (120 s, 5 attempts, single use, like MausBot). Any other code gets `bad_code`. Point a gadget at it with the console commands `host 127.0.0.1:8810` and `pair 123456` (or the simulator's `--host` and `--pair`). To serve a real board on your network, add `--bind 0.0.0.0`.

## What it does

- Refuses upgrades with an `Origin` header (403) or without the `openmausbot-gadget.1` subprotocol (400), declines every extension, pings every 15 s and drops a gadget after 45 s of silence.
- Runs the §4.3 handshake with Node's `crypto` (high-S signatures verify): enrollment with the one code, known gadgets without a code, `enroll_required`, `bad_code`, `bad_sig`, `proto_unsupported`, `device_limit` (with `--max-devices`) and `replaced`.
- Answers a voice turn or a `say` with `heard` (voice only), `working "checking your calendar"`, three cumulative `reply` frames 250 ms apart, the final `reply`, then a 440 Hz test tone as speech (40 ms frames, paced to real time, at most 0.5 s ahead), then `done ok` once the tone has played out. With `--done-before-speech` it sends `done ok` right after the final `reply` and the tone follows, which is the order MausBot usually produces; a new turn then gets `speak.stop` for that tone before its first message. An empty `--heard` gives `done failed "Didn't catch that"`; a mic rate other than 16000 gives `done failed "Unsupported mic rate"`; `stop`, `voice.drop` or a new turn stop the old one with `speak.stop` and `done stopped`.
- Folds every screen string to Latin-1 (plus `…` and `→`). It does not shape Markdown: its scripted texts are plain.
- Sends asks one at a time (and again on reconnect), posts, cards, images, actions and settings on command, and runs a full firmware update signed with the test key `t1`.

## Options

| Option | Default | Meaning |
|---|---|---|
| `--port <n>` | 8810 | 0 = any free port (reported in `listening`) |
| `--bind <addr>` | 127.0.0.1 | 0.0.0.0 to serve a real board on the LAN |
| `--code <6 digits>` | random | the one valid pairing code |
| `--code-ttl <s>` | 120 | how long the code is valid |
| `--state <dir>` | memory only | keeps `host_id` and enrolled gadgets in `<dir>/fake-host.json` |
| `--host-id <32 hex>` | random | |
| `--host-name <text>` | `Fake MausBot` | sent in `challenge` |
| `--bot <id>:<name>` | `b_fake:Fake Bot` | the bot in `ready`; `--bot :` makes gadgets unbound |
| `--heard <text>` | `What's on my calendar today?` | the speech-to-text result; `""` → "Didn't catch that" |
| `--reply <text>` | `You have two meetings today: design review at 10 and lunch with Sam at 1.` | |
| `--tone-ms <n>` | 800 | test tone length; 0 = no speech |
| `--ota-key <file>` / `--ota-key-id <id>` | `keys/test-t1.key.hex` / `t1` | OTA signing key |
| `--max-devices <n>` | 20 | enrolled-gadget cap (`device_limit`) |
| `--done-before-speech` | off | send `done ok` before the speech instead of after it (MausBot's usual order, PROTOCOL.md §4.4) |
| `--quiet` | off | no logs on stderr |

## Control: JSON lines

Commands go to stdin, one JSON object per line. Events come out on stdout, one JSON object per line; logs go to stderr. Every command may name `"gadget": "<id>"` (default: the most recently ready gadget) and gets exactly one `{"event": "ack", "cmd": …, "ok": true}` or `{"event": "ack", "cmd": …, "ok": false, "error": "…"}`. A line that is not a command gets an ack with `"cmd": null`. The process exits 0 after `quit` or when stdin ends.

| Command | Fields | Effect |
|---|---|---|
| `code` | `code?` | open a new pairing window; emits `code` |
| `ask` | `id?`, `kind`, `title`, `body?`, `options?` (permission: omitted or exactly Allow/Deny; question: at most 4), `expires_s?` | queue an ask; a permission ask always carries Allow and Deny. The ack has `id` and `queued` |
| `ask.close` | `id`, `reason?` (default `withdrawn`) | close the open ask, or drop a queued one |
| `post` | `kind` (`routine` or `message`), `text`, `speak?` (default: the gadget's setting) | send `post`; speech follows when `speak` and a speaker exist, once any earlier speech has played out |
| `card` / `card.close` | `id?`, `title`, `body?`, `ttl_s?` (default 30) / `id` | |
| `image` | `id?`, `w`, `h`, `ttl_s?`, `pattern?` (`bars` or `#rrggbb`) | `image.begin`, RGB565 rows, `image.end` |
| `act` | `id?`, `name`, `args?` | send `act`; emits `act.result` when it arrives, or `{"timeout": true}` after 15 s |
| `settings` | `bot?` (`{id, name}`), `speak_pushes?`, `name?` | send `settings`; a rename while offline is sent after the next `ready` |
| `heard` / `reply` | `text` | change the scripted speech-to-text result / reply |
| `ota` | `image` (path), `version`, `board?`, `tamper?` | the full §4.8 flow with a 64 KiB window. `tamper`: `sig` (gadget answers `bad_sig`), `sha256` (`checksum` at commit), `size` (`too_large`) |
| `revoke` | | forget the gadget and send `error revoked` if it is connected |
| `replace` | | send `error replaced` and close |
| `drop` | | destroy the socket without a close frame |
| `close` | `code?` | close frame (default 1000) |
| `quit` | | close every gadget (1001) and exit 0 |

| Event | Fields |
|---|---|
| `listening` | `port`, `host_id` |
| `code` | `code`, `expires_at` (epoch ms) |
| `connected` / `closed` | `remote` / `gadget`, `code` |
| `rx` / `tx` | `gadget` (null before `prove`), `msg` (the text frame) |
| `rx_binary` | `gadget`, `kind`, `stream`, `bytes` (payload length) |
| `enrolled` / `ready` / `refused` | `gadget` / `gadget`, `session` / `gadget`, `code`, `message` |
| `turn` | `gadget`, `turn`, `phase` (`started`, `heard`, `reply`, `speech`, `done`), `outcome?` |
| `answer` | `gadget`, `id`, `option` |
| `act.result` | `gadget`, `id`, `ok`, `data?`, `error?` (or `timeout: true`) |
| `ota` | `gadget`, `phase` (`offered`, `ready`, `progress`, `committed`, `installed`, `failed`), `offset?`, `size?`, `version?`, `code?` |
| `ack` | `cmd`, `ok`, `error?`, plus `id` and other details for some commands |

Example session:

```
$ node tools/fake-host/src/main.ts --port 8810 --code 123456 --quiet
{"event":"listening","port":8810,"host_id":"…"}
{"event":"code","code":"123456","expires_at":1791131434536}
{"event":"connected","remote":"127.0.0.1:53122"}
…
{"event":"ready","gadget":"gad_b18b86ce1389e46d","session":"s_0c1d2e3f4a5b"}
{"cmd":"ask","kind":"permission","title":"Run the backup?","body":"It takes about a minute."}
{"event":"tx","gadget":"gad_b18b86ce1389e46d","msg":"{\"op\":\"ask\",…}"}
{"event":"ack","cmd":"ask","ok":true,"id":"a_5f0e1d2c3b4a","queued":false}
```

`ota` events with `failed` carry the gadget's `fw.fail` code, or the host's own: `ready_timeout` (no `fw.ready` within 10 s), `progress_timeout`, `disconnected`.
````

- [ ] **Step 9: Append the `fake-host` job to `.github/workflows/ci.yml`**

```bash
cat >> .github/workflows/ci.yml <<'EOF'

  fake-host:
    runs-on: ubuntu-24.04
    steps:
      - uses: actions/checkout@v7.0.1
      - uses: actions/setup-node@v7.0.0
        with:
          node-version: 24
          cache: npm
      - run: npm ci
      - run: npm run test:fake-host
EOF
ruby -e 'require "yaml"; puts YAML.load_file(".github/workflows/ci.yml")["jobs"].keys.inspect'
```

Expected: `["protocol", "fake-host"]`.

- [ ] **Step 10: Commit**

```bash
git add tools/fake-host/src/control.ts tools/fake-host/src/main.ts tools/fake-host/test/cli.test.ts tools/fake-host/test/modules.test.ts tools/fake-host/README.md .github/workflows/ci.yml
git commit -m "feat(fake-host): JSON-lines CLI, README and CI job" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 13: Branch verification

There is no build step (Node runs the `.ts` files), no TypeScript compiler and no linter in the pinned dependency set; the type and syntax gate is `modules.test.ts` plus every test importing its modules. This task runs everything CI runs, on both Node versions, from a clean clone.

**Files:** none (fix-ups only if a check fails, each in the task that owns the file, then rerun this task).

- [ ] **Step 1: Clean tree and commit list**

```bash
git status --short -- . ':(exclude)docs'
git log --oneline main..p1-protocol
```

Expected: no output from `git status`; the log shows the commits of Tasks 1–12 (13 commits counting the docs commit). Changes under `docs/` written by other plan sessions are expected; P1 does not commit them.

- [ ] **Step 2: Everything on Node 22**

```bash
rm -rf node_modules && npm ci
npm run vectors:check; echo "exit $?"
npm test
```

Expected: `exit 0` after the generator line; `npm test` reports PASS (33) for the protocol tests and then PASS (58) for the fake host.

- [ ] **Step 3: Everything on Node 24 (the CI version)**

```bash
PATH="$HOME/.nvm/versions/node/v24.14.1/bin:$PATH" node --version
PATH="$HOME/.nvm/versions/node/v24.14.1/bin:$PATH" npm run vectors:check
PATH="$HOME/.nvm/versions/node/v24.14.1/bin:$PATH" npm test
```

Expected: `v24.14.1`; no diff; `ℹ pass 33` / `ℹ fail 0`, then `ℹ pass 58` / `ℹ fail 0`.

- [ ] **Step 4: A fresh clone behaves like CI**

```bash
D=$(mktemp -d /private/tmp/p1-verify.XXXXXX) && git clone -q --branch p1-protocol . "$D" && (cd "$D" && npm ci && npm run vectors:check && npm test && git status --porcelain | wc -l); rm -rf "$D"
```

Expected: all green and `0` changed files at the end. The folder name is unique, so concurrent sessions running this step never delete each other's clone.

- [ ] **Step 5: Hygiene checks**

```bash
git check-attr text -- protocol/vectors/prove.json keys/test-t1.key.hex
git diff --check main...p1-protocol -- . ':(exclude)docs' && echo "no whitespace errors"
ruby -e 'require "yaml"; puts YAML.load_file(".github/workflows/ci.yml")["jobs"].keys.inspect'
for f in $(git ls-files '*.ts' '*.json' README.md protocol tools); do LC_ALL=C perl -ne 'print "$ARGV:$.: invisible or control character\n" if /\xe2\x80[\x8b-\x8f\xa8-\xaf]|\xc2\xa0|[\x00-\x08\x0b\x0c\x0e-\x1f\x7f]/' "$f"; done
grep -n '"@noble/curves' $(git ls-files '*.ts')
```

Expected: both paths `text: unset`; `no whitespace errors`; `["protocol", "fake-host"]`; no invisible- or control-character lines; the only `@noble/curves` import is in `protocol/tools/gen-vectors.ts`. The whitespace and invisible-character checks cover P1's own files only: `docs/` holds plans that other sessions own.

- [ ] **Step 6: Original-work review**

Read `protocol/PROTOCOL.md`, both READMEs and the source comments once more and confirm that nothing names, quotes or links any third-party gadget SDK or voice-assistant firmware project (spec §11), and that the root README carries the exact trademark sentence:

```bash
grep -c "The OpenMausBot name, the MausBot name and the Maus mascot are trademarks of Supamaus Software Private Limited." README.md
```

Expected: `1`.

- [ ] **Step 7: Stop and hand off**

Do not push. Report to Omkar:

- the branch `p1-protocol` and its commits, ready for review and publishing;
- that P2a can branch `p2a-core` from it, and that P3a can vendor `protocol/vectors/` (every `*.json` plus `SHA256SUMS`) with the SDK commit in its `SOURCE` file;
- for P3a's vendored `frames.json` test: `valid` covers both layers (PROTOCOL.md §4.9), the 2-byte header and, for kind `0x04`, the firmware payload. `fw-no-offset` (`0401aabbcc`) has a valid header, so the header-only `decodeBinary` of contract §3.2 returns a frame for it; the test must also run the kind-4 payload check (a u32 little-endian offset and 1–4096 bytes) before it expects `valid: false`, as `protocol/test/vectors.test.ts` does;
- for P4b's comparator: `versions.json` now pins numeric identifiers past 2^53 (`big-numeric-prerelease`, `big-numeric-core`), which compare by exact integer value, so compare digit strings (leading zeros dropped, then length, then digits), not JavaScript numbers;
- that P2d's `THIRD_PARTY.md` must list `@noble/curves` 2.4.0 (MIT, vector generator only) and `ws` 8.22.0 (MIT, fake host only);
- open questions for P2a and P3a (PROTOCOL.md wording beyond spec §4; confirm or raise before implementing):
  - `voice.drop` ends its turn with `done stopped`;
  - frames that are not JSON objects with a string `op` are ignored; binary frames for an inactive stream are ignored;
  - a `hello` that fails rule 1 is answered with `error` instead of `challenge`, and a bad `board` id is `bad_sig`;
  - hosts cut `say` to 2000 characters;
  - `answer.option` must be one of the ask's option ids;
  - `fw.progress` reports durably written bytes at each 16 KiB crossing and at `size`, and the host sends `fw.commit` after the progress that equals `size`;
  - the `busy` and `flash` meanings;
  - `post.id` is a host-sent id (at most 40 characters of `[A-Za-z0-9_.:-]`);
  - `settings.bot.name` is folded;
  - `done` may precede the turn's speech, and a new turn sends `speak.stop` for it first (the fake host's `--done-before-speech` produces that order, so P2a's e2e runner should run at least one voice turn with it);
  - post speech waits until earlier speech has played out;
- what was **not** verified here:
  - the GitHub-hosted CI run (ubuntu-24.04 runners, `actions/checkout@v7.0.1`, `actions/setup-node@v7.0.0`; the action versions come from plan research and were not exercised);
  - a Windows checkout (the `-text` attribute is set and the verifier rejects CR bytes, but no Windows clone was made);
  - interoperation with the C firmware and the simulator, which P2a's C vector tests and its simulator ↔ fake-host e2e runner cover;
  - a real board talking to the fake host over the LAN (`--bind 0.0.0.0`, the gadget's `host <address>`; the fake host has no mDNS). P1 itself needs no hardware; on-device protocol checks belong to P2c's hardware checklist.

## Deviations recorded during the build

Review of Tasks 5–8 (commit `fix(P1): address review of tasks 5-8`). Where these differ from the code blocks in Tasks 5, 6 and 8, the repository files are authoritative. The expected fake-host counts rise by 4: 29, 41, 52 and 58 at the ends of Tasks 8, 9, 10 and 11, and `ℹ pass 62` in Task 13's full run (protocol stays at 33).

1. **`refuseUpgrade` (Task 8, `server.ts`)** adds `socket.on("error", () => socket.destroy())` and `socket.once("finish", () => socket.destroy())` before `socket.end(...)`, as `ws` does when it aborts a handshake. Node's HTTP server removes its socket error listener before emitting `upgrade`, so a client that sent a refused upgrade (for example with an `Origin` header) and then reset the connection crashed the host with an unhandled `EPIPE`, and a client that kept its half of a refused connection open made `FakeHost.close()` (and so `quit`, stdin EOF and SIGTERM in Task 12) hang. Tests: "a refused upgrade whose client resets the connection does not crash the host" and "a refused upgrade whose client keeps its half open does not hold up close()".
2. **`GadgetSession.closing` (Task 8, `session.ts`).** `ws` keeps emitting `message` while a socket is CLOSING, so after `error` the session still processed frames: a second `prove` sent in the same burst as a refused one enrolled the gadget and used up the pairing window, and a `hello` after `proto_unsupported` got a `challenge`. That breaks §4.3 ("until `ready` the host ignores every op other than the first `hello` and one `prove`", and `error` is followed by a close). `close()` and `terminate()` now set `closing`; the message handler drops every frame once it is set (after refreshing the idle timer), and `fail()`, `send()` and `sendBinary()` do nothing. Test: "after error the host ignores every frame the gadget still sends".
3. **Command lookup (Task 8, `server.ts`)** uses `Object.hasOwn` on the built-ins and on each feature's `commands`, so `toString`, `constructor`, `__proto__`, `valueOf` and `hasOwnProperty` answer `unknown command <name>` as contract §4.7 requires, instead of running an inherited function. Test: "commands: names inherited from Object.prototype are unknown commands".
4. **`RELEASE_VERSION_RE` (Task 5, `version.ts`)** is imported from `types.ts` and re-exported instead of being defined twice.
5. **Vectors (Task 6).** The verifier pins the case names of `base64.json`, `frames.json` and `versions.json` too. `versions.json` gains four `compare` cases (`leading-zero-core`, `leading-zero-prerelease`, `empty-ident-vs-numeric`, `empty-ident-vs-alpha`) and two `custom` cases (`leading-zero`, `empty-ident`) for inputs the spec's pattern accepts but SemVer 2.0.0 forbids, so the C comparator (P2a) and `protocol/lib` must agree on them. The spec pattern is unchanged; `PROTOCOL.md` §4.1 (where the firmware version encoding lives, and which §4.8 refers to) states the behaviour.
6. **`decideProve` doc comment (Task 7, `enroll.ts`)** now says that a wrong pairing code uses one of the window's attempts, which the code always did.

Final review (commit `fix(P1): address final review`). Where these differ from the code blocks in Tasks 5, 6, 8, 9, 10 and 12, the repository files are authoritative, except the Task 9 pacing test, whose code block is updated. The fake host gains five tests (one in `cli.test.ts`, two in `handshake.test.ts`, two in `display.test.ts`): Task 13's full run now expects `ℹ pass 33` for the protocol and `ℹ pass 67` for the fake host.

7. **Speech pacing test (Task 9, `voice.test.ts`)** now times from the host's own `tx` event for `speak.begin`, not from the moment the test's `await gadget.next("speak.begin")` resumes, and asserts a lead of at most 500 ms (was 540) and at least 700 ms for 1.2 s of audio (was 600). `tx` is emitted synchronously inside `session.send()`, before `speech.ts` starts its pacing clock, so a stall in the test's event loop can no longer add to the measured lead (an 80 ms stall made the old test fail every time with `audio ran 583 ms ahead`), and the measured lead is an upper bound on the host's real one. Only the test changed; the event table of contract §4.7 is untouched. A mutated `speech.ts` that allows 580 ms of lead fails the new test.
8. **CLI stdin (Task 12, `main.ts`)** reads the JSON-lines control channel only when fd 0 is a FIFO, a socket or a regular file (`fstatSync(0)`). From a terminal, `/dev/null` or a background job it leaves `process.stdin` alone and serves until SIGINT/SIGTERM. Before, `node tools/fake-host/src/main.ts --port 8810 --code 123456 &` (as P2b and P2d start it) exited 0 at once in a script (a background job's stdin is `/dev/null`) and was stopped by SIGTTIN in an interactive shell. A driver's pipe that closes still means EOF and exit 0 (Review Focus 5). The README says so, and shows `cat | node …` for typing commands by hand. Test: "with no stdin pipe (a background job's /dev/null) the CLI keeps serving until SIGTERM, then exits 0".
9. **`close` command (Task 8, `server.ts` and `session.ts`)** accepts only 1000 and 3000–4999 (`code must be 1000 or 3000-4999`), and `GadgetSession.close` calls `ws.close()` before it sets `closing`. Before, `{"cmd":"close","code":1006}` (or any code `ws` refuses) set `closing`, then `ws.close()` threw: the socket stayed open while every frame from the gadget was dropped and later commands acked `ok: true` without sending anything. Tests: "close accepts only 1000 and 3000-4999; a refused code leaves the session working" and "GadgetSession.close leaves the session open and sending when the socket refuses the close code".
10. **Unsent frames (Task 10, `display.ts`)**: `post`, `card`, `card.close`, `act`, `image` and live `settings` ack `ok: false` with `not sent: frame over 16 KiB or gadget closing` when `GadgetSession.send` returns false (an `act` that was not sent also drops its listener and timeout, and a `post` that was not sent plays no speech). `ask` checks its frame against the 16 KiB limit before queueing (`ask exceeds the 16 KiB text frame limit`), so an ask that can never be sent no longer becomes the current ask and blocks every later one. `settings` builds and size-checks its frame before it changes the record (`settings exceed the 16 KiB text frame limit`, record unchanged); a gadget that is live but closing gets `not sent: the gadget is closing`, and a rename in it is kept pending for the next `ready`, as for an offline gadget. Tests: "post, card.close, act and settings that cannot be sent ack an error instead of ok" and "an ask over 16 KiB is refused and does not hold up the asks after it".
11. **Version comparison (Task 5, `version.ts`; Task 6, vectors)** compares numeric identifiers, in the core and in the pre-release, as digit strings by exact integer value (leading zeros dropped, then length, then digits) instead of as JavaScript numbers, which lost precision past 2^53. `versions.json` gains `big-numeric-prerelease` (`1.0.0-rc.9007199254740993` > `1.0.0-rc.9007199254740992`) and `big-numeric-core` (`9007199254740993.0.0` > `9007199254740992.0.0`), pinned in `vectors.test.ts`; `frames.test.ts` adds values past 2^64 and leading zeros. PROTOCOL.md §4.1 says "numeric identifiers compare by exact integer value, whatever their length".
12. **`frames.json` meaning of `valid` (Task 2, PROTOCOL.md §4.9)**: the table row now says that `valid` covers both the 2-byte header and, for kind `0x04`, the firmware payload, and that `fw-no-offset` has a valid header and an invalid firmware payload. The vector bytes are unchanged. Task 13's hand-off tells P3a.
13. **Fake-host README (Task 12)**: a real board is pointed at the computer's LAN address (`host <this computer's LAN address>:8810` with `--bind 0.0.0.0`), not `127.0.0.1`, and the README says the fake host advertises no mDNS, so `host auto` does not find it. The `close` row lists the accepted codes, and the control section says that a command whose frame is over 16 KiB, or whose gadget is closing, gets `ok: false` and sends nothing.

## Self-review

- **Spec coverage:** every bullet of spec §4.1–§4.9 and §5.9 is in the Scope table with a task; the items the spec describes here but other plans build are in the out-of-scope table with their owner.
- **Placeholders:** every code step has the complete file; every command has its expected output; there are no "similar to" references between tasks.
- **Type consistency:** names used across tasks were checked against the code they come from (`FakeHost.command` returns `Ack`; `GadgetSession.sendBinary(kind, stream, payload)`; `speechOf(session).play(turn, ms)`; `CommandCall.session()` / `gadgetId()`; `Feature.attach(session, host)`), and the library names match contract §3.2 and §4.4.1.
- **Review Focus:** each of the five lines names the test that pins it, in the task that owns the code.
