# MausBot Companion Gadget Hub Implementation Plan (P3a)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Gadgets pair with MausBot through the companion's existing six-digit window, prove their P-256 key on every connection, and talk to their bot in its selected thread, with approvals, pushes, presence, removal and desktop UI, all inside the dependency-free companion sidecar.

**Architecture:** A hand-rolled RFC 6455 server (`companion/src/gadget/ws.ts`) serves `/gadget` on the companion's `0.0.0.0:8810` listener only. `hub.ts` runs the `hello → challenge → prove → ready` handshake (`enroll.ts`) against the gadget-aware `DeviceRegistry`, keeps one session per gadget, and keeps one shared `GET /api/events?screens=off` stream to the harness. Each `session.ts` maps gadget ops to the same allowlisted harness routes a paired phone uses (`harness-client.ts` checks `denyReason()` itself and sends the gadget's identity headers), with turn, ask and push rules split into `turns.ts`, `asks.ts` and `push.ts`. Electron mints the gadget control token and delivers it over the existing parent ports; the desktop renderer gains a "Pair a gadget" panel and gadget rows in Settings → Remote access.

**Tech Stack:** TypeScript (Node 24 built-ins only in the companion: `node:http`, `node:crypto`, `node:net`), vitest ^4.1.10, React 19 + `renderToStaticMarkup` tests, Electron main/preload (`node:test` + `vm` slice tests), pnpm 10.33.0.

**Spec:** `/Users/omkar/Desktop/openmaus/openmausbot-gadget-sdk/docs/specs/2026-10-04-openmausbot-gadget-design.md` (v1.1: §6.1, §6.2 except voice in/out, §6.4, §6.5, amendments A2–A10, A24, A26–A29, A31 token plumbing, A35). The binding contract for every name, type, path and route is `/Users/omkar/Desktop/openmaus/openmausbot-gadget-sdk/docs/plans/00-interfaces.md` (§3.1–§3.13, §3.17–§3.19, §5.2, §6 D1, D8, D9, D13, D14, D15, D19, D20). Read both before Task 1.

## Global Constraints

- Every app command runs in `/Users/omkar/Desktop/openmaus/OpenGrokBot-gadget` with Node 24 first on `PATH`; `pnpm --version` must print `10.33.0` (else use `corepack pnpm@10.33.0 <args>`).
- **Shell state does not persist between Bash calls.** Subagents get a fresh working directory and environment on every call, and this Mac's default `node` is v22.22.3 (`~/.local/bin`). So every runnable block below starts with this two-line preamble, and every one-line `Run:` command carries the same `cd … &&` and `PATH=…` prefix. Keep them whenever you copy a command:
  ```bash
  cd /Users/omkar/Desktop/openmaus/OpenGrokBot-gadget || exit 1
  export PATH="$HOME/.nvm/versions/node/v24.14.1/bin:$PATH"
  ```
  Every commit is a single command, `git add <files> && git commit -m "…"`. If `git add` fails (a wrong directory, a missing path), `git commit` never runs, so it cannot commit whatever is already staged in some other checkout.
- **Long commands.** `pnpm exec tsc -p tsconfig.server.json` takes 40–150 s and `pnpm typecheck` about 3 minutes, so give those Bash calls a timeout of at least 10 minutes (600000 ms). The full `pnpm exec vitest run` in Task 19 takes about 52 minutes and must run in the background (`run_in_background`). A command that seems to hang is usually one of these.
- Never checkout, stash, reset, edit or fetch in `/Users/omkar/Desktop/openmaus/OpenGrokBot` (another session owns it); read it only with `git -C … show origin/main:<path>`. No `git fetch`, no push, no PR: Omkar publishes.
- Branch `feat/gadget-hub` from commit `6dd4403d8fbbbd5c17169724cb2a529f11d7543e` (`origin/main` when this plan was written), named by its SHA, because other sessions may fetch and every patch below is line-exact against it; P3b, P4a and P4b branch from it.
- Merge order (contract §1.3): P3a, then P3b, then P4a, then P4b. Before Omkar merges each later branch, it is rebased onto the one merged just before it. P3a goes first and needs no rebase. Once the other three have branched, a P3a fix must not move or rewrite their anchor lines in `companion/src/index.ts` and `companion/src/control.ts` (listed under "Notes for the other app plans").
- The companion ships as plain `tsc` output with no `node_modules`: `companion/src/**` uses Node built-ins and relative `.ts` imports only, never `shared/` or `server/`, erasable TypeScript syntax only (no `enum`, no parameter properties). No new dependencies anywhere in this plan.
- `/gadget` is attached only to the `0.0.0.0:8810` server's `upgrade` listener, before `proxy.upgrade`, never to the managed (hosted HTTPS) origin.
- Transport: subprotocol `openmausbot-gadget.1`; any `Origin` header is refused; no extensions; text frames ≤ 16 KiB, binary ≤ 8 KiB; ping every 15 s; 45 s without an inbound frame drops the socket.
- Encodings (A24): base64 is RFC 4648 §4 with padding and must round-trip; `pubkey` is 65 bytes starting `0x04`; the signed `<nonce>` is the exact base64 string sent; `host_id` is `/^[0-9a-f]{32}$/`, stored in `DATA_DIR/host.json` and advertised as TXT `id=` (keep `v=1`); `sendId = "gdt" + base64url(sha256(gadgetId + ":" + session + ":" + turn)).slice(0, 40)`.
- Pairing (A28): the same window as phones (120 s, 5 attempts, single use); gadgets accept `/^\d{6}$/` only; the 20-device cap is checked only after the code matches.
- Routing (A4): a gadget talks in its bot's currently selected thread, read at the start of each turn, pinned for the turn, and always sent as `threadId`.
- Asks (A9): permission cards get exactly Allow (`allow`) and Deny (`deny`); the hub never calls `/always-allow`; question cards are offered only with no `questionRequest` or one single-select question, and ≤ 4 choices; anything else is `ask {kind: "question", options: []}`.
- Push (A6): `post.kind` is `routine` or `message`; replies to a person's message from any device are never pushed; nor digests, activities, or anything in a routine's execution thread.
- Every string sent for the screen is folded to Latin-1 plus `…` and `→` (`shape.ts`), including `challenge.host_name` (D20).
- Copy for gadget pairing says "MausBot → Settings → Remote access → Pair a gadget". There is no "Settings → Devices".
- The gadget control token never appears in any child environment: Electron sends it over `parentPort` only; `OMB_GADGET_CONTROL_TOKEN` (dev, tests) is read once and deleted from `process.env` (A31).
- Tests: vitest, companion gadget tests in `companion/test/gadget/`, no sleeps (wait on events). i18n keys in `src/locales/en.json` only; never the words "workspace" or "organisation".
- Original-work rule (spec §11): no code, text or names from third-party gadget SDKs or voice-assistant firmware; vendor and primary sources only.
- Commit messages end with `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.

## Review Focus

These are the inputs most likely to bite a person that no requirement-driven test would otherwise exercise. Each has a test in the task that owns the code:

1. **The harness is down or restarting when a gadget connects or talks.** The gadget still gets `ready` (with an empty bot name), and a turn ends at once with "MausBot is not answering. Try again in a moment." instead of hanging. Test: Task 9, `hub-turns.test.ts` "still sends ready while the harness is down…".
2. **A reply longer than one 16 KiB text frame** (a long essay, or Latin-1 text whose UTF-8 is twice as long). The gadget gets the tail, cut from the start with `…`, in a frame within the limit. Test: Task 9, `hub-turns.test.ts` "cuts a reply longer than one frame from the start".
3. **Garbage after `ready`**: an unknown op, a second `hello`, an `answer` for an ask that does not exist. All are ignored and the session keeps working. Test: Task 9, `hub.test.ts` "ignores garbage, unknown ops and a second hello after ready".
4. **Two gadgets on the same bot.** A reply to gadget A's own turn must never reach gadget B as a push. B only sees unprompted posts. Test: Task 11, `hub-push.test.ts` "never pushes one gadget's reply to another gadget on the same bot".
5. **The gadget's bot was deleted between pairing and talking.** The turn fails with a reason that points to Remote access, and no message is sent. Test: Task 9, `hub-turns.test.ts` "fails a turn whose bot was deleted, pointing at Remote access".
6. **The person presses Steer on the desktop for a line the gadget queued.** `POST /api/bots/:id/queue/:queueId/steer` (origin/main `server/index.ts:21893-21951`) appends the words with `steered: true` and starts no turn of their own. The gadget's turn takes the running turn's terminal reply and ends with `done ok`, instead of hanging or binding to the next, unrelated turn, and Stop leaves that turn running. Test: Task 9, `hub-turns.test.ts` "binds a queued send that the person steered from the desktop to the running turn".

## What was verified while writing this plan

Every code block and patch below was run, not just written. A scratch mirror of `origin/main` 6dd4403 was built under `/private/tmp` with `git show`, and the repo's own `node_modules`, vitest 4.1.10, TypeScript 5.9.3 and oxlint were used read-only.

- **Companion:** all companion tests passed, including the existing suites: 35 files and 488 tests after Task 13, 36 files and 495 tests with Task 18. (A full checkout measured 484 and 491 before this revision added four gadget tests. `proxy.test.ts` and `tailscale-cli.test.ts` need the real harness and Tailscale's CLI, so they were counted on that full checkout, not in the scratch mirror.) `tsc -p tsconfig.server.json` was clean with `--erasableSyntaxOnly` added. `oxlint --deny-warnings` was clean. A plain `tsc -p tsconfig.companion.build.json` build was copied outside the repo, with no `node_modules`, and it started and printed `gadgets    ws://0.0.0.0:<port>/gadget`.
- **Shutdown test:** the SIGTERM test in Task 13 was shown to fail (hang) with `await gadgetHub.close()` removed.
- **Staged task boundaries:** Task 9 alone (session without asks, hub without push) passed 10 files and 129 tests. Task 10 brought the total to 145, and Task 11 to 154.
- **Review fixes:** every test this revision added or tightened failed against the code before its fix, and passed after it. That covers the queued send steered from the desktop, a drain bound by its queue id alone, speech stopped before a new turn, `speak.stop` before `done` in Stop cases 1–4 and on barge-in, the final reply's 250 ms gap, the device header on the push lookup, the fake harness's ping, the asks rebuild after a harness restart, the control page's gadget rows, and the gadget name field. A mutant `asks.ts` that answers no-button asks now fails all five no-button cases. The hub tests passed six runs in a row and four runs in parallel.
- **Patches:** every patch was applied in task order with `git apply` to the `origin/main` files, and each one reproduced the verified file byte for byte.
- **Renderer:** 8 files and 69 tests passed with the app's React plugin, and `tsc` was clean for the touched files. Task 16 alone passed 7 files and 65 tests.
- **Electron:** the `node:test` files passed, including the existing `companion-browser.node-test.mjs`. Its slice-to-end-of-file pattern is why the new helpers sit before `companionCloudDesktopAccess`.
- **Fixed values:** the contract §1.7 values were re-verified with `node:crypto`. They include the RFC key id, the high-S prove signature, the 69-byte short DER, and the firmware signature with `t1`.
- **`server/index.ts`:** syntax-checked with esbuild. The full app typecheck, the packaged-server smoke, `check:electron`, `i18n:check` and the full vitest suite run in Task 19.
- **Merge anchors:** Task 12's and Task 13's patches were applied to the 6dd4403 `control.ts` and `index.ts` in a scratch git repo. P3b's, P4a's and P4b's edits were then made on three branches cut from that commit, at the anchors listed under "Notes for the other app plans". The three merged in order with no conflict, and the P3b → P4a → P4b rebase chain gave the same files. The previous order, with `voice: undefined,` directly above `onDevicesChanged: undefined,`, conflicted when P3b and P4a were merged.
- **Vectors:** `vectors.test.ts` passed 7 of 7 against P1's real files, copied with Task 18's commands from the SDK's `main` (all 8 files `OK` under `shasum -c`); the gadget folder was then 15 files and 178 tests.

---

## File Structure

New, in `companion/src/` (owner P3a unless noted):

| File | Responsibility |
|---|---|
| `gadget/protocol.ts` | Constants, op types, `parseGadgetMessage`, frame codecs, canonical base64, id derivation, prove/firmware text, P-256 verify, `actionEntryHash` (contract §3.2) |
| `gadget/types.ts` | Local copies of wire types and of `isPersistentQuestionCard`, `formatQuestionAnswers`, plus `gadgetAskKind` and `defaultGadgetBot` (§3.3) |
| `gadget/shape.ts` | Reply Markdown → screen text, Latin-1 fold, frame-fitting cut (§3.8) |
| `gadget/ws.ts` | RFC 6455 server: handshake checks, masked frames, fragments, caps, ping/idle, drain-aware sender, backpressure (§3.4) |
| `gadget/harness-client.ts` | The hub's only path to the harness: allowlist + token checks, identity headers, JSON/raw calls, one SSE stream with resume (§3.7) |
| `gadget/enroll.ts` | `checkHello`, `createChallenge`, `completeProve` (§3.9) |
| `gadget/directory.ts` | *Private.* Bots, thread → bot map, running turns, recent user messages, from frames and `GET /api/bots` |
| `gadget/turns.ts` | *Private.* One gadget's turns: say/voice/stop, binding, working/reply/done, steered and queued sends |
| `gadget/asks.ts` | *Private.* Card → ask, one at a time, answers through `/respond`, ask.close, reconnect rebuild |
| `gadget/push.ts` | *Private.* Routine and unprompted-message posts, never replies to a person |
| `gadget/session.ts` | One ready connection: seams for P3b/P4a/P4b (`GadgetSessionHandle`, `VoiceProvider`, `textOnlyVoice`) (§3.10) |
| `gadget/hub.ts` | Upgrade handling, handshake, sessions, presence, revoke, settings, events, shared SSE, close (§3.11) |
| `host-id.ts` | `loadOrCreateHostId()` and the TXT entries (§3.6) |

Modified: `companion/src/devices.ts` (§3.5), `companion/src/control.ts` (§3.13 P3a rows), `companion/src/index.ts` (§3.12 P3a column), `server/index.ts` + new private `server/gadget-control-token.ts` (token parse), `electron/main.mjs`, `electron/companion.mjs`, `electron/preload.cjs` (§3.17 P3a rows), renderer `src/components/PhoneSetupFlow.tsx`, `CompanionSection.tsx`, `SidebarPhoneButton.tsx`, `SettingsModal.tsx`, `src/lib/phone-setup.ts`, `src/locales/en.json`; new `src/lib/gadgets.ts`, `src/components/PairGadgetPanel.tsx`, `src/components/GadgetRow.tsx` (§3.18).

Tests: `companion/test/gadget/*.test.ts` with shared helpers in `companion/test/gadget/helpers/` (`fixed-values.ts`, `fake-harness.ts`, `gadget-client.ts`, `hub-rig.ts`); the vendored vectors in `companion/test/fixtures/gadget-vectors/`; `server/gadget-control-token.test.ts`; `electron/companion-gadget.node-test.mjs`; renderer tests next to their components.

**Out of scope, owned by other plans** (spec §3 ownership, contract §5.2):

- P1: `protocol/PROTOCOL.md`, the vector files and the fake host (this plan vendors the vectors in Task 18).
- P3b (voice in/out): `gadget/audio.ts`, `stt-client.ts`, `speech.ts` (`createGadgetVoice`), `/api/stt`, the Speech helper file mode, PCM TTS, `speak.*` frames and the `notice-voice` card. P3a ships the seams (`SttFn`, `SpeechOut`, `VoiceProvider`, `textOnlyVoice`) and ends voice turns with the stt_unavailable copy.
- P4a (bot tools): the `/gadget/*` control routes and their token check (`control-routes.ts`), the presence notice (`presence.ts`, `/api/gadgets/presence`), `/api/internal/gadgets*`, the catalog overlay, the harness side of A10 (`confirm` actions always ask via `PeerAction "gadget_action"`), images, and the Codex and Claude timeouts. P3a delivers the token to both processes and gives gadgets exactly Allow/Deny on peer-approval cards.
- P4b (OTA): `releases.ts`, `release-keys.ts`, `ota.ts`, `firmware.ts`, the firmware control routes, `gadgetFirmware` state and the Update / Custom build cell (P3a's `GadgetRow` takes `updateCell?: ReactNode`).
- P2a–P2d: firmware, simulator, installer and docs. The phones need no work (A3).

---

### Task 1: Worktree and the protocol module

**Files:**
- Create: `companion/src/gadget/protocol.ts`
- Create: `companion/test/gadget/helpers/fixed-values.ts`
- Test: `companion/test/gadget/protocol.test.ts`

**Interfaces:**
- Consumes: nothing.
- Produces (contract §3.2, exact names): constants `GADGET_PATH`, `GADGET_SUBPROTOCOL`, `PROTO_VERSION`, `TEXT_FRAME_MAX`, `BINARY_FRAME_MAX`, `PING_INTERVAL_MS`, `IDLE_TIMEOUT_MS`, `HANDSHAKE_TIMEOUT_MS`, `UTTERANCE_MAX_MS`, `SAY_MAX_CHARS`, `REPLY_MIN_INTERVAL_MS`, `ACT_TIMEOUT_MS`, `SPEAK_AHEAD_MS`, `SPEAK_FRAME_MS`, `FW_*`, `NAME_MAX_CHARS`, `ACTIONS_MAX`, `ACTION_DESCRIPTION_MAX`, `ACTION_PARAMS_MAX_BYTES`, regexes `HOST_ID_RE`, `GADGET_ID_RE`, `PAIR_CODE_RE`, `BOARD_ID_RE`, `ACTION_NAME_RE`, `SHA256_HEX_RE`, `RELEASE_VERSION_RE`, `BinaryKind`; every op type (`HelloMsg` … `FwCommitMsg`, `GadgetToHost`, `HostToGadget`, `GadgetOp`); `parseGadgetMessage(text): GadgetToHost | null`, `encodeHostMessage(msg): string`, `encodeBinary(kind, stream, payload): Buffer`, `decodeBinary(frame)`, `encodeFwChunk(stream, offset, data): Buffer`, `isCanonicalBase64(value): boolean`, `gadgetIdFromPubkey(pub): string`, `decodePubkey(b64): Buffer | null`, `proveText(id, nonceB64, hostId): string`, `firmwareText(board, version, size, sha256Hex): string`, `verifyP256(pub65, text, derSig): boolean`, `canonicalJson(value): string`, `actionEntryHash(action): string`.

- [ ] **Step 1: Create the worktree (only if it does not exist)**

The worktree is created from the SHA, never from `origin/main`: other sessions use this repository and may fetch at any time, and every patch below is line-exact against 6dd4403d. This block needs no `cd`; every path in it is absolute.

```bash
export PATH="$HOME/.nvm/versions/node/v24.14.1/bin:$PATH"
if [ -d /Users/omkar/Desktop/openmaus/OpenGrokBot-gadget ]; then
  git -C /Users/omkar/Desktop/openmaus/OpenGrokBot-gadget rev-parse --abbrev-ref HEAD
else
  git -C /Users/omkar/Desktop/openmaus/OpenGrokBot worktree add -b feat/gadget-hub /Users/omkar/Desktop/openmaus/OpenGrokBot-gadget 6dd4403d8fbbbd5c17169724cb2a529f11d7543e
fi
git -C /Users/omkar/Desktop/openmaus/OpenGrokBot-gadget merge-base --is-ancestor 6dd4403d8fbbbd5c17169724cb2a529f11d7543e HEAD || { echo 'worktree is not based on 6dd4403d'; exit 1; }
```

Expected: either `feat/gadget-hub` (it already exists), or `Preparing worktree (new branch 'feat/gadget-hub')` followed by `HEAD is now at 6dd4403d Delete routes no client calls: checkpoint list and restore (#2280)`; the base check then prints nothing. A branch made from a SHA tracks no upstream, so git prints no tracking line. If the existing worktree is on another branch, or the base check prints `worktree is not based on 6dd4403d`, stop and ask.

- [ ] **Step 2: Install and check the toolchain**

`OMB_SKIP_HOOKS=1` matters here. The `prepare` script (`scripts/install-git-hooks.mjs`) otherwise runs `git config core.hooksPath scripts/git-hooks`, because the shared `.git/config` holds the other checkout's absolute hooks path and `extensions.worktreeConfig` is unset. That would rewrite the other session's repository config. The variable makes the script exit at once.

```bash
cd /Users/omkar/Desktop/openmaus/OpenGrokBot-gadget || exit 1
export PATH="$HOME/.nvm/versions/node/v24.14.1/bin:$PATH"
node --version && pnpm --version
OMB_SKIP_HOOKS=1 pnpm install --frozen-lockfile
pnpm exec vitest run companion/test/devices.test.ts
```

Expected: `v24.14.1`, `10.33.0`, the install finishes without lockfile changes (and prints no `git hooks:` line), and `Tests  29 passed (29)`.

- [ ] **Step 3: Write the fixed values and the failing test**

Create `companion/test/gadget/helpers/fixed-values.ts`:

````ts
// Fixed values from the gadget SDK interface contract §1.7, computed with
// @noble/curves 2.4.0 (lowS: false) and checked with node:crypto. The C
// firmware tests and the SDK's fake host pin the same bytes.
export const RFC_PRIVATE_KEY_HEX = "c9afa9d845ba75166b5c215767b1d6934e50c3db36e89b127b8a622b120f6721";
export const RFC_PUBKEY = "BGD+1LolWp0xyWHrdMY1bWjASbiSO2H6bOZpYi5g8p+2eQP+EAi4vJmkGunpVii8ZPLxsgwtfp9Rd6PClNRGIpk=";
export const RFC_ID = "gad_b18b86ce1389e46d";
export const RFC_SAMPLE_DER_HEX = "3046022100efd48b2aacb6a8fd1140dd9cd45e81d69d2c877b56aaf991c34d0ea84eaf3716022100f7cb1c942d657c41d436c7a1b6e29f65f3e900dbb9aff4064dc4ab2f843acda8";
export const PINNED_NONCE = "AAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8=";
export const PINNED_HOST_ID = "000102030405060708090a0b0c0d0e0f";
export const PROVE_SIG_B64 = "MEYCIQDwxPviQCnXl7FrNtzA0F+3qLnfjFyksnrZnYINTYpmQgIhAIfQh+HIOrWej+6+5jpAtCPFr4GVbOyoAzJkCYq/RONa";
export const SHORT_DER_HEX = "3043022052c1af44f658bb58a5b434a96d609052855835feca85b011cdfcb4159f52c37e021f2d466a13caea297fa0c97498ce12ed77ea25e9895415c4bb6ebe64b2a049cb";
export const T1_PUBKEY = "BIUTH1UeVJw2VXn0Ehdhd8apNOHmB6VvjV512SxIbCCXfUhvlLRTuOTLHtrsVAbIx06JhMoOMbC3ic73hZpxMwg=";
export const FIRMWARE_TEXT_SHA = "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855";
export const FIRMWARE_DER_T1_HEX = "3046022100de2dfa125231a739f0b1b27e2ec085e4027b59e4486978457ffccb95b3895f2d022100ef00bd5c4177cb97ba4a4f4036046f6a20b3c896c5ab1124720b54afe6179744";
````

Create `companion/test/gadget/protocol.test.ts`:

````ts
// openmausbot-gadget/1 codecs and identity, pinned to the fixed values in
// the gadget SDK's interface contract §1.7 (computed with @noble/curves
// 2.4.0, lowS:false, and checked with node:crypto). The vendored vector
// files (vectors.test.ts) run the same functions over the full vector set.
import { Buffer } from "node:buffer";
import { describe, expect, it } from "vitest";

import {
  actionEntryHash,
  BinaryKind,
  canonicalJson,
  decodeBinary,
  decodePubkey,
  encodeBinary,
  encodeFwChunk,
  encodeHostMessage,
  firmwareText,
  gadgetIdFromPubkey,
  isCanonicalBase64,
  parseGadgetMessage,
  proveText,
  verifyP256,
} from "../../src/gadget/protocol.ts";

import {
  FIRMWARE_DER_T1_HEX,
  FIRMWARE_TEXT_SHA,
  PINNED_HOST_ID,
  PINNED_NONCE,
  PROVE_SIG_B64,
  RFC_ID,
  RFC_PUBKEY,
  RFC_SAMPLE_DER_HEX,
  SHORT_DER_HEX,
  T1_PUBKEY,
} from "./helpers/fixed-values.ts";

describe("identity", () => {
  it("derives the id from the SEC1 public key", () => {
    const pub = decodePubkey(RFC_PUBKEY);
    expect(pub?.length).toBe(65);
    expect(gadgetIdFromPubkey(pub!)).toBe(RFC_ID);
  });

  it("refuses keys that are not canonical base64 of a 65-byte uncompressed point", () => {
    const bytes = Buffer.from(RFC_PUBKEY, "base64");
    expect(decodePubkey(bytes.subarray(0, 33).toString("base64"))).toBeNull(); // compressed length
    expect(decodePubkey(Buffer.concat([Buffer.from([0x02]), bytes.subarray(1)]).toString("base64"))).toBeNull();
    // a non-zero pad bit: the last character before "=" changes, the bytes do not
    const last = RFC_PUBKEY.at(-2)!;
    const tweaked = RFC_PUBKEY.slice(0, -2) + String.fromCharCode(last.charCodeAt(0) + 1) + "=";
    expect(Buffer.from(tweaked, "base64").equals(bytes)).toBe(true);
    expect(decodePubkey(tweaked)).toBeNull();
    expect(decodePubkey(RFC_PUBKEY.replace(/=$/, ""))).toBeNull();
  });

  it("checks base64 canonically", () => {
    for (const value of ["", "AA==", "AAE=", "AAEC", PINNED_NONCE, RFC_PUBKEY]) expect(isCanonicalBase64(value)).toBe(true);
    for (const value of ["AA", "AA=", "AB==", "AAE", "AA==\n", " AA==", "-_8=", "AA==AA=="]) expect(isCanonicalBase64(value)).toBe(false);
  });
});

describe("signatures", () => {
  const pub = decodePubkey(RFC_PUBKEY)!;
  const text = proveText(RFC_ID, PINNED_NONCE, PINNED_HOST_ID);

  it("builds the exact prove text with no trailing newline", () => {
    expect(text).toBe(
      "openmausbot-gadget/1\nprove\ngad_b18b86ce1389e46d\nAAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8=\n000102030405060708090a0b0c0d0e0f",
    );
  });

  it("verifies the pinned high-S prove signature and rejects a changed host id", () => {
    const sig = Buffer.from(PROVE_SIG_B64, "base64");
    expect(sig.length).toBe(72);
    expect(verifyP256(pub, text, sig)).toBe(true);
    expect(verifyP256(pub, proveText(RFC_ID, PINNED_NONCE, "000102030405060708090a0b0c0d0e0e"), sig)).toBe(false);
    expect(verifyP256(pub, text, sig.subarray(0, 70))).toBe(false);
  });

  it("verifies the RFC 6979 sample (high-S) and a 69-byte short DER", () => {
    expect(verifyP256(pub, "sample", Buffer.from(RFC_SAMPLE_DER_HEX, "hex"))).toBe(true);
    const short = Buffer.from(SHORT_DER_HEX, "hex");
    expect(short.length).toBe(69);
    expect(verifyP256(pub, "openmausbot-gadget/1 der-short 13", short)).toBe(true);
  });

  it("builds and verifies the firmware text with the test key t1", () => {
    const fwText = firmwareText("amoled-175c", "1.1.0", 1234567, FIRMWARE_TEXT_SHA);
    expect(fwText).toBe(
      "openmausbot-gadget/1\nfirmware\namoled-175c\n1.1.0\n1234567\ne3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
    );
    expect(verifyP256(decodePubkey(T1_PUBKEY)!, fwText, Buffer.from(FIRMWARE_DER_T1_HEX, "hex"))).toBe(true);
  });

  it("is false, never a throw, on garbage", () => {
    expect(verifyP256(Buffer.alloc(65, 4), text, Buffer.from(PROVE_SIG_B64, "base64"))).toBe(false);
    expect(verifyP256(Buffer.alloc(10), text, Buffer.alloc(8))).toBe(false);
  });
});

describe("frames", () => {
  it("round-trips binary frames and refuses malformed ones", () => {
    const mic = encodeBinary(BinaryKind.mic, 7, Buffer.alloc(640, 1));
    expect(mic.subarray(0, 2)).toEqual(Buffer.from([1, 7]));
    expect(decodeBinary(mic)).toMatchObject({ kind: 1, stream: 7 });
    expect(decodeBinary(mic)?.payload.length).toBe(640);
    expect(decodeBinary(Buffer.from([1]))).toBeNull();
    expect(decodeBinary(Buffer.from([1, 0, 5]))).toBeNull();
    expect(decodeBinary(Buffer.from([9, 1, 5]))).toBeNull();
    expect(decodeBinary(Buffer.from([4, 1, 0, 0, 1]))).toBeNull();
    expect(() => encodeBinary(BinaryKind.mic, 0, Buffer.alloc(1))).toThrow(RangeError);
  });

  it("puts a little-endian offset in front of firmware chunks", () => {
    const chunk = encodeFwChunk(3, 65536, Buffer.alloc(4096, 0xab));
    expect(chunk.length).toBe(2 + 4 + 4096);
    expect(chunk.readUInt32LE(2)).toBe(65536);
    expect(decodeBinary(chunk)).toMatchObject({ kind: 4, stream: 3 });
    expect(() => encodeFwChunk(3, 0, Buffer.alloc(4097))).toThrow(RangeError);
  });
});

describe("text frames", () => {
  it("accepts well-formed gadget ops and rejects the rest", () => {
    expect(parseGadgetMessage('{"op":"say","turn":"t3f9a0c2b-7","text":"hi"}')).toEqual({ op: "say", turn: "t3f9a0c2b-7", text: "hi" });
    expect(parseGadgetMessage('{"op":"stop"}')).toEqual({ op: "stop" });
    expect(parseGadgetMessage("not json")).toBeNull();
    expect(parseGadgetMessage('{"op":"warp"}')).toBeNull();
    expect(parseGadgetMessage('{"op":"say","turn":"' + "t".repeat(33) + '","text":"x"}')).toBeNull();
    expect(parseGadgetMessage('{"op":"voice.begin","turn":"t1-1","stream":0,"rate":16000}')).toBeNull();
    expect(parseGadgetMessage('{"op":"answer","id":"a_1"}')).toBeNull();
    expect(parseGadgetMessage("[1,2]")).toBeNull();
  });

  it("never writes null or undefined fields", () => {
    const text = encodeHostMessage({ op: "done", turn: "t1-1", outcome: "ok", reason: undefined });
    expect(text).toBe('{"op":"done","turn":"t1-1","outcome":"ok"}');
  });
});

describe("action entry hash", () => {
  it("is stable under key order and defaults risk to confirm", () => {
    expect(canonicalJson({ b: 1, a: { d: [1, { z: 1, y: 2 }], c: "x" } })).toBe('{"a":{"c":"x","d":[1,{"y":2,"z":1}]},"b":1}');
    const a = actionEntryHash({ name: "chime", description: "Play a chime.", params: { type: "object", properties: {} }, risk: "safe" });
    const b = actionEntryHash({ name: "chime", description: "Play a chime.", params: { properties: {}, type: "object" }, risk: "safe" });
    expect(a).toBe(b);
    expect(a).toMatch(/^[0-9a-f]{16}$/);
    expect(actionEntryHash({ name: "chime", description: "Play a chime.", params: {}, risk: "confirm" }))
      .not.toBe(actionEntryHash({ name: "chime", description: "Play a chime.", params: {}, risk: "safe" }));
  });
});
````

- [ ] **Step 4: Run it to see it fail**

Run: `cd /Users/omkar/Desktop/openmaus/OpenGrokBot-gadget && PATH="$HOME/.nvm/versions/node/v24.14.1/bin:$PATH" pnpm exec vitest run companion/test/gadget/protocol.test.ts`
Expected: FAIL — `Error: Cannot find module '../../src/gadget/protocol.ts' imported from …/protocol.test.ts`.

- [ ] **Step 5: Write `companion/src/gadget/protocol.ts`**

````ts
// openmausbot-gadget/1 on the host side: constants, op types, frame codecs
// and identity helpers. The protocol itself is specified in the gadget SDK
// (protocol/PROTOCOL.md, written from the design spec §4); this file is the
// companion's copy of the names, with the same type names the SDK uses.
//
// Node built-ins only. The companion ships as plain tsc output with no
// node_modules, and it cannot import shared/ or server/.
import { Buffer } from "node:buffer";
import { createHash, createPublicKey, verify as cryptoVerify } from "node:crypto";

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
export const FW_CHUNK_BYTES = 4096;
export const FW_WINDOW_BYTES = 64 * 1024;
export const FW_READY_TIMEOUT_MS = 10_000;
export const NAME_MAX_CHARS = 32;
export const ACTIONS_MAX = 16;
export const ACTION_DESCRIPTION_MAX = 200;
export const ACTION_PARAMS_MAX_BYTES = 1024;
export const HOST_ID_RE = /^[0-9a-f]{32}$/;
export const GADGET_ID_RE = /^gad_[0-9a-f]{16}$/;
export const PAIR_CODE_RE = /^\d{6}$/;
export const BOARD_ID_RE = /^[a-z0-9-]{1,32}$/;
export const ACTION_NAME_RE = /^[a-z][a-z0-9_.-]{0,31}$/;
export const SHA256_HEX_RE = /^[0-9a-f]{64}$/;
export const RELEASE_VERSION_RE = /^\d+\.\d+\.\d+(-[0-9A-Za-z.]+)?$/;

export const BinaryKind = { mic: 0x01, speaker: 0x02, image: 0x03, firmware: 0x04 } as const;
export type BinaryKindValue = (typeof BinaryKind)[keyof typeof BinaryKind];

export type GadgetErrorCode =
  | "proto_unsupported" | "enroll_required" | "bad_code" | "bad_sig" | "revoked" | "device_limit" | "replaced";
export type FwFailCode =
  | "too_large" | "wrong_board" | "same_version" | "unknown_key" | "bad_sig"
  | "busy" | "flash" | "sequence" | "checksum" | "timeout";
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

const isRecord = (value: unknown): value is Record<string, unknown> =>
  typeof value === "object" && value !== null && !Array.isArray(value);
const isString = (value: unknown): value is string => typeof value === "string";
const isOptionalString = (value: unknown): boolean => value === undefined || typeof value === "string";
const isInt = (value: unknown): value is number => typeof value === "number" && Number.isInteger(value);
const isStream = (value: unknown): value is number => isInt(value) && value >= 1 && value <= 255;
/** `turn` is chosen by the gadget: unique per boot, at most 32 characters. */
const isTurn = (value: unknown): value is string => isString(value) && value.length >= 1 && value.length <= 32;

/** Parse + shape-check one gadget → host text frame; null for invalid JSON,
 *  a missing op, an unknown op, or a known op with a wrong required field. */
export function parseGadgetMessage(text: string): GadgetToHost | null {
  let raw: unknown;
  try {
    raw = JSON.parse(text);
  } catch {
    return null;
  }
  if (!isRecord(raw) || !isString(raw.op)) return null;
  const m = raw;
  const ok = (valid: boolean): GadgetToHost | null => (valid ? (m as unknown as GadgetToHost) : null);
  switch (m.op) {
    case "hello":
      return ok(isInt(m.proto) && isString(m.id) && isString(m.pubkey) && isString(m.name) && isString(m.board) &&
        isString(m.fw) && isRecord(m.caps) && (m.actions === undefined || Array.isArray(m.actions)) &&
        (m.sensors === undefined || isRecord(m.sensors)));
    case "prove":
      return ok(isString(m.sig) && isOptionalString(m.enroll));
    case "voice.begin":
      return ok(isTurn(m.turn) && isStream(m.stream) && isInt(m.rate));
    case "voice.end":
      return ok(isTurn(m.turn) && typeof m.ms === "number" && Number.isFinite(m.ms));
    case "voice.drop":
      return ok(isTurn(m.turn));
    case "say":
      return ok(isTurn(m.turn) && isString(m.text));
    case "stop":
      return ok(m.turn === undefined || isTurn(m.turn));
    case "answer":
      return ok(isString(m.id) && isString(m.option));
    case "act.result":
      return ok(isString(m.id) && typeof m.ok === "boolean" && isOptionalString(m.error));
    case "sense":
      return ok(true);
    case "event":
      return ok(isString(m.name) && m.name.length <= 64);
    case "fw.ready":
      return ok(isStream(m.stream));
    case "fw.fail":
      return ok(isStream(m.stream) && isString(m.code));
    case "fw.progress":
      return ok(isStream(m.stream) && isInt(m.offset) && m.offset >= 0);
    case "fw.installed":
      return ok(isString(m.version));
    default:
      return null;
  }
}

/** JSON.stringify that drops undefined fields (never writes null). */
export function encodeHostMessage(msg: HostToGadget): string {
  return JSON.stringify(msg, (_key, value: unknown) => (value === null ? undefined : value));
}

const BINARY_KINDS = new Set<number>(Object.values(BinaryKind));

export function encodeBinary(kind: BinaryKindValue, stream: number, payload: Uint8Array): Buffer {
  if (!BINARY_KINDS.has(kind)) throw new RangeError(`unknown binary kind ${kind}`);
  if (!isStream(stream)) throw new RangeError(`stream must be 1-255, got ${stream}`);
  return Buffer.concat([Buffer.from([kind, stream]), payload]);
}

/** Null for a frame with no payload, stream 0, an unknown kind, or a
 *  firmware frame without its 4-byte offset and at least one data byte. */
export function decodeBinary(frame: Uint8Array): { kind: BinaryKindValue; stream: number; payload: Buffer } | null {
  if (frame.length < 3) return null;
  const kind = frame[0];
  const stream = frame[1];
  if (!BINARY_KINDS.has(kind) || stream === 0) return null;
  if (kind === BinaryKind.firmware && frame.length < 2 + 4 + 1) return null;
  const payload = Buffer.from(frame.buffer, frame.byteOffset + 2, frame.length - 2);
  return { kind: kind as BinaryKindValue, stream, payload };
}

export function encodeFwChunk(stream: number, offset: number, data: Uint8Array): Buffer {
  if (data.length < 1 || data.length > FW_CHUNK_BYTES) throw new RangeError(`firmware chunk must be 1-${FW_CHUNK_BYTES} bytes`);
  if (!isInt(offset) || offset < 0 || offset > 0xffffffff) throw new RangeError("firmware offset out of range");
  const header = Buffer.alloc(4);
  header.writeUInt32LE(offset, 0);
  return encodeBinary(BinaryKind.firmware, stream, Buffer.concat([header, data]));
}

/** RFC 4648 §4 with padding; true only when decode→encode gives the same text. */
export function isCanonicalBase64(value: string): boolean {
  if (typeof value !== "string" || value.length % 4 !== 0 || !/^[A-Za-z0-9+/]*={0,2}$/.test(value)) return false;
  return Buffer.from(value, "base64").toString("base64") === value;
}

/** "gad_" + first 16 hex of sha256(pub). */
export function gadgetIdFromPubkey(pub: Uint8Array): string {
  return `gad_${createHash("sha256").update(pub).digest("hex").slice(0, 16)}`;
}

/** 65 bytes starting 0x04 decoded from canonical base64, else null. */
export function decodePubkey(b64: string): Buffer | null {
  if (!isCanonicalBase64(b64)) return null;
  const bytes = Buffer.from(b64, "base64");
  return bytes.length === 65 && bytes[0] === 0x04 ? bytes : null;
}

export function proveText(id: string, nonceB64: string, hostId: string): string {
  return ["openmausbot-gadget/1", "prove", id, nonceB64, hostId].join("\n");
}

export function firmwareText(board: string, version: string, size: number, sha256Hex: string): string {
  return ["openmausbot-gadget/1", "firmware", board, version, String(size), sha256Hex].join("\n");
}

/** node:crypto verify (accepts high-S); false on any parse error. */
export function verifyP256(pub65: Uint8Array, text: string, derSig: Uint8Array): boolean {
  if (pub65.length !== 65 || pub65[0] !== 0x04) return false;
  try {
    const point = Buffer.from(pub65);
    const key = createPublicKey({
      key: {
        kty: "EC",
        crv: "P-256",
        x: point.subarray(1, 33).toString("base64url"),
        y: point.subarray(33, 65).toString("base64url"),
      },
      format: "jwk",
    });
    return cryptoVerify("sha256", Buffer.from(text, "utf8"), { key, dsaEncoding: "der" }, derSig);
  } catch {
    return false;
  }
}

/** JSON with object keys sorted (UTF-16 order) at every depth, no whitespace. */
export function canonicalJson(value: unknown): string {
  if (Array.isArray(value)) return `[${value.map((item) => (item === undefined ? "null" : canonicalJson(item))).join(",")}]`;
  if (isRecord(value)) {
    const keys = Object.keys(value).filter((key) => value[key] !== undefined).sort();
    return `{${keys.map((key) => `${JSON.stringify(key)}:${canonicalJson(value[key])}`).join(",")}}`;
  }
  return JSON.stringify(value) ?? "null";
}

/** First 16 hex of sha256(canonicalJson({name, description, params, risk})) with risk defaulted to "confirm". */
export function actionEntryHash(action: { name: string; description: string; params: unknown; risk: Risk }): string {
  const entry = { name: action.name, description: action.description, params: action.params, risk: action.risk ?? "confirm" };
  return createHash("sha256").update(canonicalJson(entry)).digest("hex").slice(0, 16);
}
````

- [ ] **Step 6: Run the test, the typecheck and the linter**

```bash
cd /Users/omkar/Desktop/openmaus/OpenGrokBot-gadget || exit 1
export PATH="$HOME/.nvm/versions/node/v24.14.1/bin:$PATH"
pnpm exec vitest run companion/test/gadget/protocol.test.ts
pnpm exec tsc -p tsconfig.server.json  # 40-150 s: give this Bash call a timeout of at least 10 minutes
pnpm exec oxlint --deny-warnings companion
```

Expected: `Tests  13 passed (13)`; `tsc` prints nothing; oxlint exits 0 with no diagnostics.

- [ ] **Step 7: Commit**

```bash
cd /Users/omkar/Desktop/openmaus/OpenGrokBot-gadget || exit 1
export PATH="$HOME/.nvm/versions/node/v24.14.1/bin:$PATH"
git add companion/src/gadget/protocol.ts companion/test/gadget/helpers/fixed-values.ts companion/test/gadget/protocol.test.ts && git commit -m "feat(companion): gadget protocol types, frames and P-256 identity

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 2: Local wire types and the copied card helpers

**Files:**
- Create: `companion/src/gadget/types.ts`
- Test: `companion/test/gadget/types.test.ts`

**Interfaces:**
- Consumes: nothing.
- Produces (contract §3.3): `WireTaskLite`, `WireBotLite`, `AskQuestionLite`, `OptionCardLite`, `WireMessageLite`, `RoutineRunStatusLite`, `RoutineRunLite`, `RuntimeEventLite` (additively also `message?: string; terminal?: boolean` for `runtime.error`), `NotificationLite`, `ServerFrameLite`, `DirectSendReceiptLite`, `QUESTION_DISMISS_MESSAGE`, `ANSWER_PREAMBLE`, `isPersistentQuestionCard(card)`, `shouldSettleRequestCard(card, source)` (additive), `gadgetAskKind(card): "permission" | "question" | "unsupported"`, `formatQuestionAnswers(questions, answers)`, `defaultGadgetBot(bots): WireBotLite | null`.

- [ ] **Step 1: Write the failing test**

The first two `describe` blocks repeat the original cases of `shared/ask-question.test.ts` (origin/main :174-195 and :456-474), so the copies stay pinned to the originals.

````ts
// The local copies in types.ts must behave exactly like the shared/ originals
// they were copied from. The first two describe blocks repeat the original
// cases from shared/ask-question.test.ts (origin/main 6dd4403) verbatim.
import { describe, expect, it } from "vitest";

import {
  defaultGadgetBot,
  formatQuestionAnswers,
  gadgetAskKind,
  isPersistentQuestionCard,
  shouldSettleRequestCard,
  type AskQuestionLite,
  type OptionCardLite,
  type WireBotLite,
} from "../../src/gadget/types.ts";

const card = (extra: Partial<OptionCardLite>): OptionCardLite => ({ title: "t", subtitle: "s", options: [], ...extra });

describe("isPersistentQuestionCard (copy of shared/ask-question.ts)", () => {
  it("keeps explicit and legacy questions open while excluding approval/proposal cards", () => {
    expect(isPersistentQuestionCard(card({ requestType: "question" }))).toBe(true);
    expect(isPersistentQuestionCard(card({ questionRequest: { version: 1, questions: [] } }))).toBe(true);
    expect(isPersistentQuestionCard(card({}))).toBe(true);
    expect(isPersistentQuestionCard(card({ requestType: "permission", tool: "Bash" }))).toBe(false);
    expect(isPersistentQuestionCard(card({ routineRequest: {} }))).toBe(false);
    expect(isPersistentQuestionCard(card({ modelRequest: {} }))).toBe(false);
  });

  it("settles a question only for an explicit user answer", () => {
    const question = card({ requestType: "question" });
    expect(shouldSettleRequestCard(question, "timeout")).toBe(false);
    expect(shouldSettleRequestCard(question, "system")).toBe(false);
    expect(shouldSettleRequestCard(question, "unavailable")).toBe(false);
    expect(shouldSettleRequestCard(question, "user")).toBe(true);
    expect(shouldSettleRequestCard(card({ requestType: "permission", tool: "Bash" }), "timeout")).toBe(true);
  });
});

describe("formatQuestionAnswers (copy of shared/ask-question.ts)", () => {
  const questions: AskQuestionLite[] = [
    { question: "Which model?", options: [{ label: "Opus" }] },
    { question: "Which stores?", multiSelect: true, options: [{ label: "Instamart" }] },
  ];

  it("names each question beside its answer — the model sees only this text", () => {
    expect(formatQuestionAnswers(questions, [["Opus"], ["Instamart", "Blinkit"]])).toBe(
      "The user answered your questions.\n\nQ: Which model?\nA: Opus\n\nQ: Which stores?\nA: Instamart, Blinkit",
    );
  });

  it("omits a question that was left unanswered instead of implying one", () => {
    expect(formatQuestionAnswers(questions, [["Opus"], ["  "]])).toBe(
      "The user answered your questions.\n\nQ: Which model?\nA: Opus",
    );
  });

  it("is empty when nothing was answered, so nothing is sent", () => {
    expect(formatQuestionAnswers(questions, [[], []])).toBe("");
  });
});

describe("gadgetAskKind (contract D19)", () => {
  it("never offers a proposal card, whatever else it carries", () => {
    for (const proposal of ["routineRequest", "skillRequest", "profileRequest", "modelRequest", "teamSetupRequest"] as const) {
      expect(gadgetAskKind(card({ [proposal]: {}, tool: "propose" }))).toBe("unsupported");
    }
  });

  it("treats tool cards and the harness's peer-approval cards as permissions", () => {
    expect(gadgetAskKind(card({ requestType: "permission", tool: "Bash", options: ["Allow", "Deny", "Always allow"] }))).toBe("permission");
    // peer approvals carry a tool and no requestType
    expect(gadgetAskKind(card({ tool: "ask_bot", options: ["Allow", "Deny", "Always allow"] }))).toBe("permission");
  });

  it("treats question cards as questions and a tool-less permission as unsupported", () => {
    expect(gadgetAskKind(card({ requestType: "question", options: ["A", "B"] }))).toBe("question");
    expect(gadgetAskKind(card({ questionRequest: { version: 1, questions: [] } }))).toBe("question");
    expect(gadgetAskKind(card({ requestType: "permission" }))).toBe("unsupported");
  });
});

describe("defaultGadgetBot (spec §4.3 rule 3)", () => {
  const bot = (id: string, extra: Partial<WireBotLite> = {}): WireBotLite => ({ id, name: id, threadId: `${id}-t`, ...extra });

  it("prefers an unsectioned chief of staff, then a pinned bot, then an unsectioned bot, then any visible bot", () => {
    expect(defaultGadgetBot([bot("a"), bot("chief", { chiefOfStaff: true }), bot("p", { pinned: true })])?.id).toBe("chief");
    expect(defaultGadgetBot([bot("s", { section: "x" }), bot("chief", { chiefOfStaff: true, section: "x" }), bot("p", { pinned: true }), bot("u")])?.id).toBe("p");
    expect(defaultGadgetBot([bot("s", { section: "x" }), bot("chief", { chiefOfStaff: true, section: "x" }), bot("u")])?.id).toBe("u");
    expect(defaultGadgetBot([bot("s", { section: "x" }), bot("t", { section: "y" })])?.id).toBe("s");
  });

  it("skips hidden bots and answers null for an empty list", () => {
    expect(defaultGadgetBot([bot("h", { hidden: true, chiefOfStaff: true }), bot("v")])?.id).toBe("v");
    expect(defaultGadgetBot([bot("h", { hidden: true })])).toBeNull();
    expect(defaultGadgetBot([])).toBeNull();
  });
});
````

- [ ] **Step 2: Run it to see it fail**

Run: `cd /Users/omkar/Desktop/openmaus/OpenGrokBot-gadget && PATH="$HOME/.nvm/versions/node/v24.14.1/bin:$PATH" pnpm exec vitest run companion/test/gadget/types.test.ts`
Expected: FAIL — `Cannot find module '../../src/gadget/types.ts'`.

- [ ] **Step 3: Write `companion/src/gadget/types.ts`**

````ts
// Local copies of the few OpenMausBot wire types and helpers the gadget hub
// needs. The companion cannot import shared/ (tsc rootDir is companion/src),
// so these are field subsets of origin/main 6dd4403 shared/wire.ts,
// shared/runtime-events.ts, shared/routines.ts, shared/ask-question.ts and
// shared/notification.ts. Unknown fields are ignored. The helper copies are
// pinned to the originals' own test cases in companion/test/gadget/types.test.ts.

export interface WireTaskLite {
  threadId: string;
  title: string;
  routineRunId?: string;
  activity?: "working" | "waiting-on-you" | "idle" | "no-signal" | "dead" | "parked.computer";
  busy?: boolean;
}
export interface WireBotLite {
  id: string;
  name: string;
  threadId: string;          // the selected thread (the gadget's thread, pinned per turn)
  tasks?: WireTaskLite[];
  hidden?: boolean;
  pinned?: boolean;
  section?: string;
  chiefOfStaff?: boolean;
  voice?: string;            // passed as voiceId to /api/tts/*
  busy?: boolean;
}
export interface AskQuestionLite {
  question: string;
  header?: string;
  multiSelect?: boolean;
  options: Array<{ label: string; description?: string }>;
}
export interface OptionCardLite {
  title: string;
  subtitle: string;
  options: string[];
  requestType?: "permission" | "question";
  requestId?: string;
  tool?: string;
  answered?: string;
  dismissed?: boolean;
  expired?: boolean;
  allowKey?: string;
  questionRequest?: { version: 1; questions: AskQuestionLite[] };
  routineRequest?: unknown;
  skillRequest?: unknown;
  profileRequest?: unknown;
  modelRequest?: unknown;
  teamSetupRequest?: unknown;
}
export interface WireMessageLite {
  id: string;
  role: "bot" | "user";
  kind: "text" | "options" | "activity" | "screen" | "connector" | "secret" | "routine.run" | "goal.run" | "digest" | "compaction";
  text?: string;
  card?: OptionCardLite;
  tool?: { name: string; spoken?: string };
  turnId?: string;
  requestMessageId?: string;
  turnTerminal?: boolean;
  turnSucceeded?: boolean;
  sendId?: string;
  peerAsk?: { botId: string; name: string; unattended?: boolean };
  steered?: boolean;
  queued?: boolean;
  queueId?: string;
  at: number;
}
export type RoutineRunStatusLite = "queued" | "running" | "waiting" | "completed" | "failed" | "cancelled" | "missed";
export interface RoutineRunLite {
  id: string;
  botId: string;
  routineName: string;
  status: RoutineRunStatusLite;
  output?: string;
  error?: string;
  attention?: string;
  executionThreadId?: string;
}
export interface RuntimeEventLite {
  type: string;              // "turn.started" | "turn.completed" | "content.delta" | "request.opened" | "request.resolved" | …
  threadId: string;
  turnId?: string;
  requestId?: string;
  ok?: boolean;              // turn.completed
  stopReason?: string | null;
  streamKind?: "assistant_text" | "reasoning_text";  // content.delta
  delta?: string;
  behavior?: "allow" | "deny" | "answer";            // request.resolved
  source?: "user" | "auto" | "timeout" | "system" | "unavailable" | "peer";
  message?: string;          // runtime.error
  terminal?: boolean;        // runtime.error
}
export interface NotificationLite { kind: string; botId: string; threadId: string }
export type ServerFrameLite =
  | { kind: "hello"; cursor: string; resumed: boolean }
  | { kind: "ping" }
  | { kind: "message"; threadId: string; message: WireMessageLite }
  | { kind: "message.patch"; threadId: string; message: WireMessageLite }
  | { kind: "bot"; bot: WireBotLite }
  | { kind: "bot.queued"; queues: Record<string, Array<{ queueId: string; text: string; reason?: "capacity" | "group-turn" }>> }
  | { kind: "bot.deleted"; botId: string }
  | { kind: "notify"; notification: NotificationLite }
  | { kind: "routine.run"; run: RoutineRunLite }
  | { kind: "runtime"; event: RuntimeEventLite }
  | { kind: "other"; raw: { kind: string } };

/** 202 body of POST /api/bots/:id/messages (server/index.ts:8691). */
export type DirectSendReceiptLite =
  | { ok: true; threadId: string; message: WireMessageLite; steered?: true }
  | { ok: true; queued: true; queueId: string; threadId: string; reason?: "capacity" | "group-turn" };

/** Copy of shared/ask-question.ts QUESTION_DISMISS_MESSAGE. */
export const QUESTION_DISMISS_MESSAGE = "The user closed this question without answering. Use your best judgment and continue.";
/** Copy of shared/ask-question.ts ANSWER_PREAMBLE. */
export const ANSWER_PREAMBLE = "The user answered your questions.";

/** Copy of shared/ask-question.ts isPersistentQuestionCard. */
export function isPersistentQuestionCard(card?: OptionCardLite | null): boolean {
  return Boolean(card && (
    card.requestType === "question" || card.questionRequest ||
    (!card.requestType && !card.tool && !card.routineRequest && !card.skillRequest && !card.profileRequest &&
      !card.modelRequest && !card.teamSetupRequest)
  ));
}

/** Copy of shared/ask-question.ts shouldSettleRequestCard: a resolution ends a
 * persistent question card only when the person answered it. */
export function shouldSettleRequestCard(card: OptionCardLite | null | undefined, source: string): boolean {
  return !isPersistentQuestionCard(card) || source === "user";
}

/** How a card reaches the gadget (spec §6.2 Asks, contract D19). */
export function gadgetAskKind(card: OptionCardLite): "permission" | "question" | "unsupported" {
  if (card.routineRequest || card.skillRequest || card.profileRequest || card.modelRequest || card.teamSetupRequest) {
    return "unsupported";
  }
  if (isPersistentQuestionCard(card)) return "question";
  if (typeof card.tool === "string" && card.tool.length > 0) return "permission";
  return "unsupported";
}

/** Copy of shared/ask-question.ts formatQuestionAnswers. */
export function formatQuestionAnswers(questions: readonly AskQuestionLite[], answers: readonly (readonly string[])[]): string {
  const blocks: string[] = [];
  questions.forEach((question, index) => {
    const picked = (answers[index] ?? []).map((value) => value.trim()).filter(Boolean);
    if (!picked.length) return;
    blocks.push(`Q: ${question.question}\nA: ${picked.join(", ")}`);
  });
  if (!blocks.length) return "";
  return `${ANSWER_PREAMBLE}\n\n${blocks.join("\n\n")}`;
}

/** §4.3 rule 3 default bot over GET /api/bots (visible bots, server order):
 *  first chiefOfStaff && !section; else first pinned; else first !section && !chiefOfStaff; else first. */
export function defaultGadgetBot(bots: readonly WireBotLite[]): WireBotLite | null {
  const visible = bots.filter((bot) => !bot.hidden);
  return visible.find((bot) => bot.chiefOfStaff && !bot.section)
    ?? visible.find((bot) => bot.pinned === true)
    ?? visible.find((bot) => !bot.section && !bot.chiefOfStaff)
    ?? visible[0]
    ?? null;
}
````

- [ ] **Step 4: Run the test and the checks**

```bash
cd /Users/omkar/Desktop/openmaus/OpenGrokBot-gadget || exit 1
export PATH="$HOME/.nvm/versions/node/v24.14.1/bin:$PATH"
pnpm exec vitest run companion/test/gadget/types.test.ts
pnpm exec tsc -p tsconfig.server.json  # 40-150 s: give this Bash call a timeout of at least 10 minutes
pnpm exec oxlint --deny-warnings companion
```

Expected: `Tests  10 passed (10)`; no `tsc` output; oxlint exits 0.

- [ ] **Step 5: Commit**

```bash
cd /Users/omkar/Desktop/openmaus/OpenGrokBot-gadget || exit 1
export PATH="$HOME/.nvm/versions/node/v24.14.1/bin:$PATH"
git add companion/src/gadget/types.ts companion/test/gadget/types.test.ts && git commit -m "feat(companion): local wire types and card helpers for the gadget hub

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 3: Screen text

**Files:**
- Create: `companion/src/gadget/shape.ts`
- Test: `companion/test/gadget/shape.test.ts`

**Interfaces:**
- Consumes: nothing.
- Produces (contract §3.8): `shapeReply(markdown): string`, `foldLatin1(text): string`, `screenText(text, {markdown, charset?}): string`, `cutFromStart(text, maxBytes): string` (the JSON-encoded string without quotes stays ≤ `maxBytes`), plus additive `clampChars(text, max): string`.

Note on escapes: write non-ASCII code points in source as `\u{…}` (brace form) exactly as shown; the arrows block U+2190–21FF is not stripped as emoji because `→` is in the gadget font and `←` folds to `<-`.

- [ ] **Step 1: Write the failing test**

````ts
// Screen text for a gadget: Markdown shaped for reading on a small screen,
// then folded to the Latin-1 fonts the firmware ships (spec §4.4).
import { describe, expect, it } from "vitest";

import { clampChars, cutFromStart, foldLatin1, screenText, shapeReply } from "../../src/gadget/shape.ts";

describe("shapeReply", () => {
  it("replaces code blocks, images and bare links, and keeps link labels", () => {
    const md = "Here:\n```ts\nconst a = 1;\n```\n![chart](x.png) see [the docs](https://example.com/a) or https://example.com/b";
    expect(shapeReply(md)).toBe("Here:\n\n[code]\n\n[image] see the docs or [link]");
  });

  it("strips heading, list, quote and emphasis markers but keeps the lines", () => {
    const md = "# Plan\n\n- **first** step\n- _second_ step\n1. third\n> quoted\n---\n- [x] done";
    expect(shapeReply(md)).toBe("Plan\n\nfirst step\nsecond step\nthird\nquoted\n\ndone");
  });

  it("joins table cells and drops the separator row", () => {
    expect(shapeReply("| a | b |\n|---|---|\n| 1 | 2 |")).toBe("a, b\n\n1, 2");
  });

  it("keeps short inline code and replaces long inline code", () => {
    expect(shapeReply("run `pnpm test` then `" + "x".repeat(41) + "`")).toBe("run pnpm test then [code]");
  });

  it("drops emoji but keeps the arrow the font has", () => {
    expect(shapeReply("Done 🎉 ✅ next → deploy 👍🏽")).toBe("Done next → deploy");
  });

  it("collapses three or more newlines to one blank line", () => {
    expect(shapeReply("a\n\n\n\nb")).toBe("a\n\nb");
  });
});

describe("foldLatin1", () => {
  it("keeps Latin-1, the ellipsis and the right arrow", () => {
    expect(foldLatin1("Café · 25 °C … → ÿ")).toBe("Café · 25 °C … → ÿ");
  });

  it("spells smart punctuation plainly and removes invisible characters", () => {
    expect(foldLatin1("\u{201C}Hi\u{201D} \u{2013} it\u{2019}s \u{2022} fine \u{2190} back\u{A0}x\u{200B}y\tz")).toBe("\"Hi\" - it's \u{B7} fine <- back xy z");
  });

  it("falls back to base letters and drops what has none", () => {
    expect(foldLatin1("Łódź ő ﬁ 你好 €")).toBe("Lódz o fi  ");
  });
});

describe("screenText, cutFromStart and clampChars", () => {
  it("shapes only Markdown text", () => {
    expect(screenText("**hi** 😀", { markdown: true })).toBe("hi");
    expect(screenText("**hi** 😀", { markdown: false })).toBe("**hi** ");
  });

  it("keeps the tail within the byte budget and marks the cut", () => {
    const text = "é".repeat(100) + "end";
    const cut = cutFromStart(text, 50);
    expect(cut.startsWith("…")).toBe(true);
    expect(cut.endsWith("end")).toBe(true);
    expect(Buffer.byteLength(JSON.stringify(cut)) - 2).toBeLessThanOrEqual(50);
    expect(cutFromStart("short", 50)).toBe("short");
    expect(cutFromStart("abc", 2)).toBe("");
  });

  it("counts escaped characters against the budget", () => {
    const cut = cutFromStart('"'.repeat(30), 20);
    expect(Buffer.byteLength(JSON.stringify(cut)) - 2).toBeLessThanOrEqual(20);
  });

  it("clamps from the end", () => {
    expect(clampChars("abcdef", 4)).toBe("abc…");
    expect(clampChars("abc", 4)).toBe("abc");
  });
});
````

- [ ] **Step 2: Run it to see it fail**

Run: `cd /Users/omkar/Desktop/openmaus/OpenGrokBot-gadget && PATH="$HOME/.nvm/versions/node/v24.14.1/bin:$PATH" pnpm exec vitest run companion/test/gadget/shape.test.ts`
Expected: FAIL — `Cannot find module '../../src/gadget/shape.ts'`.

- [ ] **Step 3: Write `companion/src/gadget/shape.ts`**

````ts
// Reply text → screen text for a gadget (spec §4.4).
//
// A companion-local, screen-oriented adaptation of the harness's speakable()
// (server/tts/speech-text.ts, which the companion cannot import): the same
// ordered replacements, except that this one is for reading, so paragraph
// breaks stay newlines, a code block becomes "[code]" and a bare URL
// "[link]". Text for speech is NOT shaped here: the hub sends the raw reply
// to /api/tts/prepare, which runs speakable() itself.

/** The emoji and pictographic ranges speakable() strips, minus the arrows
 * block (U+2190–21FF): "→" is in the gadget font and "←" folds to "<-". */
// oxlint-disable-next-line no-misleading-character-class -- variation selectors and ZWJ are stripped on their own, not as part of a grapheme
const EMOJI = /[\u{1F000}-\u{1FAFF}\u{2600}-\u{27BF}\u{FE00}-\u{FE0F}\u{2B00}-\u{2BFF}\u{200D}\u{20E3}]/gu;

/** Markdown (or any agent output) → plain text for a small screen. */
export function shapeReply(markdown: string): string {
  if (!markdown) return "";
  let text = markdown.replace(/\r\n?/g, "\n");

  // fenced code first — nothing inside must survive the rules below
  text = text.replace(/```[^\n]*\n[\s\S]*?(?:```|$)/g, "\n[code]\n");
  text = text.replace(/~~~[^\n]*\n[\s\S]*?(?:~~~|$)/g, "\n[code]\n");

  // images before links — the syntax differs by one character
  text = text.replace(/!\[[^\]]*\]\([^)]*\)/g, "[image]");
  text = text.replace(/\[([^\]]+)\]\([^)]*\)/g, "$1");
  text = text.replace(/<https?:\/\/[^>\s]+>/g, "[link]");
  text = text.replace(/\bhttps?:\/\/\S+/g, "[link]");

  // tables: drop the separator row, join each row's cells
  text = text.replace(/^[ \t]*\|?[ \t:-]*\|[ \t|:-]*$/gm, "");
  text = text.replace(/^[ \t]*\|(.+)\|[ \t]*$/gm, (_m, row: string) =>
    row.split("|").map((cell) => cell.trim()).filter(Boolean).join(", "),
  );

  // inline code: short identifiers carry meaning, long ones are noise
  text = text.replace(/`([^`\n]+)`/g, (_m, code: string) => (code.length <= 40 ? code : "[code]"));

  // heading, list, quote and rule markers
  text = text.replace(/^[ \t]{0,3}#{1,6}[ \t]+(.*)$/gm, "$1");
  text = text.replace(/^[ \t]*[-*+][ \t]+/gm, "");
  text = text.replace(/^[ \t]*\d+[.)][ \t]+/gm, "");
  text = text.replace(/^[ \t]*>[ \t]?/gm, "");
  text = text.replace(/^[ \t]*(?:[-*_][ \t]*){3,}$/gm, "");

  // emphasis, strikethrough and checkboxes
  text = text.replace(/(\*\*|__)(.*?)\1/g, "$2");
  text = text.replace(/(\*|_)(?=\S)(.*?)(?<=\S)\1/g, "$2");
  text = text.replace(/~~(.*?)~~/g, "$1");
  text = text.replace(/\[[ xX]\][ \t]*/g, "");

  text = text.replace(EMOJI, "");

  // tidy: no trailing spaces, single spaces, at most one blank line
  text = text.replace(/[ \t]+$/gm, "");
  text = text.replace(/[ \t]{2,}/g, " ");
  text = text.replace(/\n{3,}/g, "\n\n");
  return text.trim();
}

/** Characters with a fixed screen spelling, checked before the keep rule
 * (NBSP is Latin-1 but shows as a plain space on the gadget). */
const FOLD: Readonly<Record<string, string>> = {
  "\u{2018}": "'", "\u{2019}": "'", "\u{201A}": "'",
  "\u{201C}": '"', "\u{201D}": '"', "\u{201E}": '"',
  "\u{2013}": "-", "\u{2014}": "-", "\u{2212}": "-",
  "\u{2022}": "\u{B7}",
  "\u{2190}": "<-",
  "\u{141}": "L", "\u{142}": "l", "\u{110}": "D", "\u{111}": "d", "\u{152}": "OE", "\u{153}": "oe", "\u{131}": "i",
  "\u{A0}": " ",
  "\t": " ",
};
const ZERO_WIDTH = /^[\u{200B}-\u{200D}\u{2060}\u{FEFF}]$/u;
/** U+000A, U+0020–U+007E, U+00A0–U+00FF, U+2026 and U+2192: the gadget fonts. */
const inCharset = (codePoint: number): boolean =>
  codePoint === 0x0a || (codePoint >= 0x20 && codePoint <= 0x7e) || (codePoint >= 0xa0 && codePoint <= 0xff) ||
  codePoint === 0x2026 || codePoint === 0x2192;

/** Fold to the gadget charset; anything without a Latin-1 spelling is dropped. */
export function foldLatin1(text: string): string {
  let out = "";
  for (const ch of text.normalize("NFC")) {
    const mapped = FOLD[ch];
    if (mapped !== undefined) {
      out += mapped;
      continue;
    }
    if (ZERO_WIDTH.test(ch)) continue;
    const codePoint = ch.codePointAt(0) ?? 0;
    if (inCharset(codePoint)) {
      out += ch;
      continue;
    }
    const base = ch.normalize("NFKD").replace(/\p{M}/gu, "");
    if (base && [...base].every((c) => inCharset(c.codePointAt(0) ?? 0))) out += base;
  }
  return out;
}

/** shapeReply (when markdown) then foldLatin1, per caps.screen.text (absent = "latin1"). */
export function screenText(text: string, options: { markdown: boolean; charset?: "latin1" }): string {
  return foldLatin1(options.markdown ? shapeReply(text) : text);
}

/** UTF-8 bytes of `text` once it is a JSON string, without the quotes. */
const encodedBytes = (text: string): number => Buffer.byteLength(JSON.stringify(text)) - 2;

/** Keep the tail so the encoded frame stays ≤ maxBytes: cut from the start and prefix "\u{2026}". */
export function cutFromStart(text: string, maxBytes: number): string {
  if (encodedBytes(text) <= maxBytes) return text;
  const chars = Array.from(text);
  let low = 1;
  let high = chars.length;
  let best = -1;
  while (low <= high) {
    const mid = (low + high) >> 1;
    if (encodedBytes(`\u{2026}${chars.slice(mid).join("")}`) <= maxBytes) {
      best = mid;
      high = mid - 1;
    } else {
      low = mid + 1;
    }
  }
  return best < 0 ? "" : `\u{2026}${chars.slice(best).join("")}`;
}

/** Cut from the end to at most `max` characters, ending in "\u{2026}". */
export function clampChars(text: string, max: number): string {
  const chars = Array.from(text);
  return chars.length <= max ? text : `${chars.slice(0, Math.max(0, max - 1)).join("").trimEnd()}\u{2026}`;
}
````

- [ ] **Step 4: Run the test and the checks**

```bash
cd /Users/omkar/Desktop/openmaus/OpenGrokBot-gadget || exit 1
export PATH="$HOME/.nvm/versions/node/v24.14.1/bin:$PATH"
pnpm exec vitest run companion/test/gadget/shape.test.ts
pnpm exec tsc -p tsconfig.server.json  # 40-150 s: give this Bash call a timeout of at least 10 minutes
pnpm exec oxlint --deny-warnings companion
```

Expected: `Tests  13 passed (13)`; no `tsc` output; oxlint exits 0.

- [ ] **Step 5: Commit**

```bash
cd /Users/omkar/Desktop/openmaus/OpenGrokBot-gadget || exit 1
export PATH="$HOME/.nvm/versions/node/v24.14.1/bin:$PATH"
git add companion/src/gadget/shape.ts companion/test/gadget/shape.test.ts && git commit -m "feat(companion): shape bot replies for a gadget's Latin-1 screen

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 4: The RFC 6455 server

**Files:**
- Create: `companion/src/gadget/ws.ts`
- Test: `companion/test/gadget/ws.test.ts`

**Interfaces:**
- Consumes: `PING_INTERVAL_MS`, `IDLE_TIMEOUT_MS` from Task 1.
- Produces (contract §3.4): `WsServerOptions`, `UpgradeCheck`, `checkUpgrade(req, subprotocol)`, `WsHandlers`, `WsConnection` (`attach`, `sendText`, `sendBinary`, `sendBinaryDrained`, `close`, `terminate`, `closed`, `bufferedAmount`, `remoteAddress`), `acceptUpgrade(req, socket, head, options): WsConnection | null`. Close codes: 1000, 1001, 1002, 1007, 1008 (backpressure above 256 KiB or handshake deadline), 1009, 1011.

- [ ] **Step 1: Write the failing test**

It covers the spec §10 companion row for `ws.ts`: Node 24's global `WebSocket` (no Origin, offers permessage-deflate), raw `node:net` upgrades for Origin, missing subprotocol, bad key, non-GET and version 12, malformed frames, fragments with interleaved control frames, ping/idle, and the drain-aware sender with a full 466×466 RGB565 image to a reader that stalls until Node itself buffers more than 64 KiB.

````ts
// The hand-rolled RFC 6455 server under the gadget hub. Node 24's global
// WebSocket is the well-behaved client (it sends no Origin and offers
// permessage-deflate, which must be declined); raw node:net sockets play
// the badly behaved ones.
import { Buffer } from "node:buffer";
import { randomBytes } from "node:crypto";
import { createServer, type Server } from "node:http";
import { createConnection, type Socket } from "node:net";
import { afterEach, describe, expect, it } from "vitest";

import { acceptUpgrade, type WsConnection, type WsServerOptions } from "../../src/gadget/ws.ts";

const SUBPROTOCOL = "openmausbot-gadget.1";
const servers: Server[] = [];
const sockets: Socket[] = [];

interface Harness {
  port: number;
  connections: WsConnection[];
  texts: string[];
  binaries: Buffer[];
  closes: Array<{ code: number; reason: string }>;
  nextConnection(): Promise<WsConnection>;
  nextClose(): Promise<{ code: number; reason: string }>;
}

/** A server that accepts every upgrade and records what each connection sees.
 *  `echo` sends text back upper-cased and binary back unchanged. */
async function serve(overrides: Partial<WsServerOptions> = {}, opts: { echo?: boolean; attachLater?: boolean } = {}): Promise<Harness> {
  const waiters: { conn: Array<(c: WsConnection) => void>; close: Array<(c: { code: number; reason: string }) => void> } = { conn: [], close: [] };
  const harness: Harness = {
    port: 0,
    connections: [],
    texts: [],
    binaries: [],
    closes: [],
    nextConnection: () => new Promise((resolve) => {
      const ready = harness.connections.at(-1);
      if (ready && !claimed.has(ready)) { claimed.add(ready); resolve(ready); } else waiters.conn.push(resolve);
    }),
    nextClose: () => new Promise((resolve) => {
      const done = harness.closes.at(-1);
      if (done && !seenCloses.has(done)) { seenCloses.add(done); resolve(done); } else waiters.close.push(resolve);
    }),
  };
  const claimed = new Set<WsConnection>();
  const seenCloses = new Set<object>();
  const server = createServer();
  server.on("upgrade", (req, socket, head) => {
    const conn = acceptUpgrade(req, socket, head, { subprotocol: SUBPROTOCOL, maxText: 16 * 1024, maxBinary: 8 * 1024, ...overrides });
    if (!conn) return;
    harness.connections.push(conn);
    const attach = () => conn.attach({
      onText: (text) => { harness.texts.push(text); if (opts.echo) conn.sendText(text.toUpperCase()); },
      onBinary: (data) => { harness.binaries.push(data); if (opts.echo) conn.sendBinary(data); },
      onClose: (code, reason) => {
        const entry = { code, reason };
        harness.closes.push(entry);
        const waiter = waiters.close.shift();
        if (waiter) { seenCloses.add(entry); waiter(entry); }
      },
    });
    if (opts.attachLater) setImmediate(attach); else attach();
    const waiter = waiters.conn.shift();
    if (waiter) { claimed.add(conn); waiter(conn); }
  });
  servers.push(server);
  harness.port = await new Promise<number>((resolve) => server.listen(0, "127.0.0.1", () => resolve((server.address() as { port: number }).port)));
  return harness;
}

/** A raw upgrade request; resolves with the socket and the response head. */
function rawUpgrade(port: number, headers: Record<string, string>, method = "GET"): Promise<{ socket: Socket; head: string; rest: Buffer }> {
  return new Promise((resolve, reject) => {
    const socket = createConnection(port, "127.0.0.1");
    sockets.push(socket);
    let data = Buffer.alloc(0);
    const onData = (chunk: Buffer) => {
      data = Buffer.concat([data, chunk]);
      const end = data.indexOf("\r\n\r\n");
      if (end < 0) return;
      socket.off("data", onData);
      resolve({ socket, head: data.subarray(0, end).toString("latin1"), rest: data.subarray(end + 4) });
    };
    socket.on("data", onData);
    socket.on("error", reject);
    socket.on("connect", () => {
      const lines = [`${method} /gadget HTTP/1.1`, `Host: 127.0.0.1:${port}`, ...Object.entries(headers).map(([k, v]) => `${k}: ${v}`)];
      socket.write(`${lines.join("\r\n")}\r\n\r\n`);
    });
  });
}

const goodHeaders = (): Record<string, string> => ({
  Upgrade: "websocket",
  Connection: "Upgrade",
  "Sec-WebSocket-Version": "13",
  "Sec-WebSocket-Key": randomBytes(16).toString("base64"),
  "Sec-WebSocket-Protocol": SUBPROTOCOL,
});

/** A client frame: masked unless told otherwise. */
function clientFrame(opcode: number, payload: Buffer, opts: { fin?: boolean; mask?: boolean; rsv?: number } = {}): Buffer {
  const fin = opts.fin ?? true;
  const mask = opts.mask ?? true;
  const b0 = (fin ? 0x80 : 0) | (opts.rsv ?? 0) | opcode;
  const len = payload.length;
  const head = len < 126 ? Buffer.from([b0, (mask ? 0x80 : 0) | len])
    : len < 65536 ? Buffer.from([b0, (mask ? 0x80 : 0) | 126, len >> 8, len & 0xff])
      : Buffer.concat([Buffer.from([b0, (mask ? 0x80 : 0) | 127, 0, 0, 0, 0]), (() => { const b = Buffer.alloc(4); b.writeUInt32BE(len); return b; })()]);
  if (!mask) return Buffer.concat([head, payload]);
  const key = randomBytes(4);
  const body = Buffer.from(payload);
  for (let i = 0; i < body.length; i++) body[i] ^= key[i & 3];
  return Buffer.concat([head, key, body]);
}

/** Collect server frames (unmasked) from a raw socket. */
function frameReader(socket: Socket, initial: Buffer = Buffer.alloc(0)) {
  let data = initial;
  const frames: Array<{ opcode: number; payload: Buffer }> = [];
  const waiters: Array<() => void> = [];
  const parse = () => {
    for (;;) {
      if (data.length < 2) return;
      let len = data[1] & 0x7f;
      let offset = 2;
      if (len === 126) { if (data.length < 4) return; len = data.readUInt16BE(2); offset = 4; }
      else if (len === 127) { if (data.length < 10) return; len = data.readUInt32BE(6); offset = 10; }
      if (data.length < offset + len) return;
      frames.push({ opcode: data[0] & 0x0f, payload: data.subarray(offset, offset + len) });
      data = data.subarray(offset + len);
      for (const w of waiters.splice(0)) w();
    }
  };
  socket.on("data", (chunk: Buffer) => { data = Buffer.concat([data, chunk]); parse(); });
  parse();
  return {
    frames,
    next(opcode: number): Promise<{ opcode: number; payload: Buffer }> {
      return new Promise((resolve) => {
        const check = () => {
          const index = frames.findIndex((f) => f.opcode === opcode);
          if (index >= 0) { resolve(frames.splice(index, 1)[0]!); return true; }
          return false;
        };
        if (!check()) { const loop = () => { if (!check()) waiters.push(loop); }; waiters.push(loop); }
      });
    },
  };
}

const closeCode = (payload: Buffer) => (payload.length >= 2 ? payload.readUInt16BE(0) : 1005);
const socketClosed = (socket: Socket) => new Promise<void>((resolve) => (socket.destroyed ? resolve() : socket.once("close", () => resolve())));

afterEach(async () => {
  for (const socket of sockets.splice(0)) socket.destroy();
  await Promise.all(servers.splice(0).map((server) => new Promise<void>((resolve) => { server.closeAllConnections(); server.close(() => resolve()); })));
});

describe("handshake", () => {
  it("negotiates the gadget subprotocol and declines permessage-deflate", async () => {
    const h = await serve({}, { echo: true });
    const ws = new WebSocket(`ws://127.0.0.1:${h.port}/gadget`, SUBPROTOCOL);
    await new Promise((resolve, reject) => { ws.onopen = resolve; ws.onerror = reject; });
    expect(ws.protocol).toBe(SUBPROTOCOL);
    expect(ws.extensions).toBe("");
    const reply = new Promise<string>((resolve) => { ws.onmessage = (event) => resolve(String(event.data)); });
    ws.send("hello");
    expect(await reply).toBe("HELLO");
    ws.close(1000);
    expect((await h.nextClose()).code).toBe(1000);
  });

  it("refuses any Origin, a missing subprotocol, a bad key, a non-GET and an old version", async () => {
    const h = await serve();
    const cases: Array<[Record<string, string>, string, RegExp]> = [
      [{ ...goodHeaders(), Origin: "http://evil.example" }, "GET", /^HTTP\/1\.1 403/],
      [{ ...goodHeaders(), "Sec-WebSocket-Protocol": "chat" }, "GET", /^HTTP\/1\.1 400/],
      [{ ...goodHeaders(), "Sec-WebSocket-Key": "short" }, "GET", /^HTTP\/1\.1 400/],
      [goodHeaders(), "POST", /^HTTP\/1\.1 400/],
      [{ ...goodHeaders(), "Sec-WebSocket-Version": "12" }, "GET", /^HTTP\/1\.1 426[\s\S]*Sec-WebSocket-Version: 13/],
    ];
    for (const [headers, method, expected] of cases) {
      const { socket, head } = await rawUpgrade(h.port, headers, method);
      expect(head).toMatch(expected);
      await socketClosed(socket);
    }
    expect(h.connections).toHaveLength(0);
  });

  it("buffers frames that arrive before handlers attach", async () => {
    const h = await serve({}, { attachLater: true });
    const { socket } = await rawUpgrade(h.port, goodHeaders());
    socket.write(clientFrame(0x1, Buffer.from("early")));
    const conn = await h.nextConnection();
    expect(conn.closed).toBe(false);
    await expect.poll(() => h.texts).toEqual(["early"]);
  });
});

describe("frames", () => {
  it("reassembles fragments, answers pings and accepts control frames between fragments", async () => {
    const h = await serve();
    const { socket, rest } = await rawUpgrade(h.port, goodHeaders());
    const reader = frameReader(socket, rest);
    socket.write(clientFrame(0x1, Buffer.from("hel"), { fin: false }));
    socket.write(clientFrame(0x9, Buffer.from("p1")));
    socket.write(clientFrame(0x0, Buffer.from("lo"), { fin: true }));
    const pong = await reader.next(0xa);
    expect(pong.payload.toString()).toBe("p1");
    await expect.poll(() => h.texts).toEqual(["hello"]);
  });

  it.each([
    ["an unmasked frame", () => clientFrame(0x1, Buffer.from("x"), { mask: false }), 1002],
    ["a reserved bit", () => clientFrame(0x1, Buffer.from("x"), { rsv: 0x40 }), 1002],
    ["an unknown opcode", () => clientFrame(0x3, Buffer.from("x")), 1002],
    ["a text frame over 16 KiB", () => clientFrame(0x1, Buffer.alloc(16 * 1024 + 1, 0x61)), 1009],
    ["a binary frame over 8 KiB", () => clientFrame(0x2, Buffer.alloc(8 * 1024 + 1)), 1009],
    ["invalid UTF-8", () => clientFrame(0x1, Buffer.from([0xc3, 0x28])), 1007],
    ["an unexpected continuation", () => clientFrame(0x0, Buffer.from("x")), 1002],
  ])("closes on %s", async (_label, build, code) => {
    const h = await serve();
    const { socket, rest } = await rawUpgrade(h.port, goodHeaders());
    const reader = frameReader(socket, rest);
    socket.write(build());
    const close = await reader.next(0x8);
    expect(closeCode(close.payload)).toBe(code);
    expect((await h.nextClose()).code).toBe(code);
  });

  it("counts fragments against the cap", async () => {
    const h = await serve();
    const { socket, rest } = await rawUpgrade(h.port, goodHeaders());
    const reader = frameReader(socket, rest);
    socket.write(clientFrame(0x2, Buffer.alloc(6000), { fin: false }));
    socket.write(clientFrame(0x0, Buffer.alloc(3000), { fin: true }));
    expect(closeCode((await reader.next(0x8)).payload)).toBe(1009);
  });
});

describe("liveness and shutdown", () => {
  it("pings on the interval and drops a gadget that sends nothing", async () => {
    const h = await serve({ pingIntervalMs: 20, idleTimeoutMs: 150 });
    const { socket, rest } = await rawUpgrade(h.port, goodHeaders());
    const reader = frameReader(socket, rest);
    expect((await reader.next(0x9)).payload.length).toBe(0);
    const closed = await h.nextClose();
    expect(closed.code).toBe(1006);
    await socketClosed(socket);
  });

  it("sends the code it closes with", async () => {
    const h = await serve();
    const ws = new WebSocket(`ws://127.0.0.1:${h.port}/gadget`, SUBPROTOCOL);
    await new Promise((resolve) => { ws.onopen = resolve; });
    const conn = await h.nextConnection();
    const clientClose = new Promise<number>((resolve) => { ws.onclose = (event) => resolve(event.code); });
    conn.close(1001, "shutting down");
    expect(conn.sendText("late")).toBe(false);
    expect(await clientClose).toBe(1001);
  });
});

describe("backpressure", () => {
  const IMAGE_BYTES = 466 * 466 * 2;
  const CHUNK = 8190;

  it("streams a full 466x466 RGB565 image through the drain-aware sender to a slow reader", async () => {
    const h = await serve();
    const { socket, rest } = await rawUpgrade(h.port, goodHeaders());
    socket.pause();
    const conn = await h.nextConnection();
    let received = rest.length;
    socket.on("data", (chunk: Buffer) => { received += chunk.length; });
    // Fill the kernel buffers until Node itself holds more than 64 KiB.
    let filler = 0;
    while (conn.bufferedAmount <= 64 * 1024) {
      expect(conn.sendBinary(Buffer.alloc(CHUNK))).toBe(true);
      filler += 1;
    }
    const image = randomBytes(IMAGE_BYTES);
    const first = conn.sendBinaryDrained(image.subarray(0, CHUNK));
    let settled = false;
    void first.then(() => { settled = true; });
    await Promise.resolve();
    expect(settled).toBe(false); // waiting for 'drain', not writing
    socket.resume();
    expect(await first).toBe(true);
    let maxBuffered = 0;
    for (let offset = CHUNK; offset < image.length; offset += CHUNK) {
      expect(await conn.sendBinaryDrained(image.subarray(offset, offset + CHUNK))).toBe(true);
      maxBuffered = Math.max(maxBuffered, conn.bufferedAmount);
    }
    expect(conn.closed).toBe(false);
    expect(maxBuffered).toBeLessThan(256 * 1024);
    const frames = filler + Math.ceil(IMAGE_BYTES / CHUNK);
    await expect.poll(() => received, { timeout: 10_000 }).toBe(filler * CHUNK + IMAGE_BYTES + frames * 4);
  });

  it("closes a session whose send buffer passes 256 KiB", async () => {
    const h = await serve();
    const { socket } = await rawUpgrade(h.port, goodHeaders());
    socket.pause();
    const conn = await h.nextConnection();
    let sent = 0;
    while (conn.sendBinary(Buffer.alloc(CHUNK))) sent += CHUNK;
    expect(sent).toBeGreaterThan(256 * 1024);
    expect((await h.nextClose()).code).toBe(1008);
  });
});
````

- [ ] **Step 2: Run it to see it fail**

Run: `cd /Users/omkar/Desktop/openmaus/OpenGrokBot-gadget && PATH="$HOME/.nvm/versions/node/v24.14.1/bin:$PATH" pnpm exec vitest run companion/test/gadget/ws.test.ts`
Expected: FAIL — `Cannot find module '../../src/gadget/ws.ts'`.

- [ ] **Step 3: Write `companion/src/gadget/ws.ts`**

````ts
// A small RFC 6455 WebSocket server for the gadget hub.
//
// Hand-rolled rather than the `ws` package because the companion ships as
// plain tsc output with no node_modules (electron-builder excludes them),
// and it stays dependency-free on purpose. The gadget protocol needs only
// the unextended core: masked client frames; text, binary, ping, pong and
// close; fragments up to small caps; no extensions (permessage-deflate is
// declined by never echoing Sec-WebSocket-Extensions).
import { Buffer } from "node:buffer";
import { createHash } from "node:crypto";
import type { IncomingMessage } from "node:http";
import type { Duplex } from "node:stream";

import { IDLE_TIMEOUT_MS, PING_INTERVAL_MS } from "./protocol.ts";

const GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
const DEFAULT_DRAIN_THRESHOLD = 64 * 1024;
const DEFAULT_MAX_BUFFERED = 256 * 1024;
const OP_CONTINUATION = 0x0;
const OP_TEXT = 0x1;
const OP_BINARY = 0x2;
const OP_CLOSE = 0x8;
const OP_PING = 0x9;
const OP_PONG = 0xa;

export interface WsServerOptions {
  subprotocol: string;
  maxText: number;
  maxBinary: number;
  pingIntervalMs?: number;
  idleTimeoutMs?: number;
  drainThresholdBytes?: number;
  maxBufferedBytes?: number;
}

export type UpgradeCheck = { ok: true; key: string } | { ok: false; status: 400 | 403 | 426; reason: string };

const header = (req: IncomingMessage, name: string): string => {
  const value = req.headers[name];
  return Array.isArray(value) ? value.join(",") : (value ?? "");
};

/** Refusals: 403 any Origin header; 400 not a GET upgrade, bad key, missing
 *  subprotocol; 426 a version other than 13. Extensions are never accepted. */
export function checkUpgrade(req: IncomingMessage, subprotocol: string): UpgradeCheck {
  // A browser always sends Origin and a gadget never does, so its mere
  // presence means a web page is trying to open a gadget session.
  if (req.headers.origin !== undefined) return { ok: false, status: 403, reason: "origin not allowed" };
  if (req.method !== "GET") return { ok: false, status: 400, reason: "not a GET" };
  if (header(req, "upgrade").toLowerCase() !== "websocket") return { ok: false, status: 400, reason: "not a websocket upgrade" };
  const connection = header(req, "connection").toLowerCase().split(",").map((token) => token.trim());
  if (!connection.includes("upgrade")) return { ok: false, status: 400, reason: "connection is not upgrade" };
  if (header(req, "sec-websocket-version").trim() !== "13") return { ok: false, status: 426, reason: "websocket version 13 only" };
  const key = header(req, "sec-websocket-key").trim();
  if (!/^[A-Za-z0-9+/]{22}==$/.test(key) || Buffer.from(key, "base64").length !== 16) {
    return { ok: false, status: 400, reason: "bad websocket key" };
  }
  const offered = header(req, "sec-websocket-protocol").split(",").map((value) => value.trim());
  if (!offered.includes(subprotocol)) return { ok: false, status: 400, reason: `subprotocol ${subprotocol} required` };
  return { ok: true, key };
}

const STATUS_TEXT: Record<400 | 403 | 426, string> = { 400: "Bad Request", 403: "Forbidden", 426: "Upgrade Required" };

type SocketLike = Duplex & { setNoDelay?: (noDelay?: boolean) => unknown; remoteAddress?: string; destroySoon?: () => void };

function refuse(socket: SocketLike, status: 400 | 403 | 426, reason: string): void {
  if (socket.destroyed) return;
  socket.end(
    `HTTP/1.1 ${status} ${STATUS_TEXT[status]}\r\n` +
      "Connection: close\r\n" +
      "Content-Type: text/plain; charset=utf-8\r\n" +
      (status === 426 ? "Sec-WebSocket-Version: 13\r\n" : "") +
      `Content-Length: ${Buffer.byteLength(reason)}\r\n\r\n${reason}`,
  );
  socket.destroySoon?.();
}

export interface WsHandlers {
  onText(text: string): void;
  onBinary(data: Buffer): void;
  onClose(code: number, reason: string): void;
}

export interface WsConnection {
  attach(handlers: WsHandlers): void;
  sendText(text: string): boolean;
  sendBinary(data: Uint8Array): boolean;
  sendBinaryDrained(data: Uint8Array): Promise<boolean>;
  close(code?: number, reason?: string): void;
  terminate(): void;
  readonly closed: boolean;
  readonly bufferedAmount: number;
  readonly remoteAddress: string;
}

/** Validate with checkUpgrade, write the 101 (echoing only the subprotocol),
 *  feed `head` to the parser and start ping/idle timers. null when refused. */
export function acceptUpgrade(req: IncomingMessage, socket: Duplex, head: Buffer, options: WsServerOptions): WsConnection | null {
  const check = checkUpgrade(req, options.subprotocol);
  if (!check.ok) {
    refuse(socket as SocketLike, check.status, check.reason);
    return null;
  }
  const accept = createHash("sha1").update(check.key + GUID).digest("base64");
  socket.write(
    "HTTP/1.1 101 Switching Protocols\r\n" +
      "Upgrade: websocket\r\n" +
      "Connection: Upgrade\r\n" +
      `Sec-WebSocket-Accept: ${accept}\r\n` +
      `Sec-WebSocket-Protocol: ${options.subprotocol}\r\n\r\n`,
  );
  (socket as SocketLike).setNoDelay?.(true);
  return openConnection(socket as SocketLike, head, options);
}

function frame(opcode: number, payload: Uint8Array): Buffer {
  const length = payload.length;
  let head: Buffer;
  if (length < 126) {
    head = Buffer.from([0x80 | opcode, length]);
  } else if (length < 65_536) {
    head = Buffer.alloc(4);
    head[0] = 0x80 | opcode;
    head[1] = 126;
    head.writeUInt16BE(length, 2);
  } else {
    head = Buffer.alloc(10);
    head[0] = 0x80 | opcode;
    head[1] = 127;
    head.writeUInt32BE(0, 2);
    head.writeUInt32BE(length, 6);
  }
  return Buffer.concat([head, payload]);
}

function closePayload(code: number, reason: string): Buffer {
  const text = Buffer.from(reason, "utf8").subarray(0, 123);
  const out = Buffer.alloc(2 + text.length);
  out.writeUInt16BE(code, 0);
  text.copy(out, 2);
  return out;
}

function openConnection(socket: SocketLike, head: Buffer, options: WsServerOptions): WsConnection {
  const pingIntervalMs = options.pingIntervalMs ?? PING_INTERVAL_MS;
  const idleTimeoutMs = options.idleTimeoutMs ?? IDLE_TIMEOUT_MS;
  const drainThreshold = options.drainThresholdBytes ?? DEFAULT_DRAIN_THRESHOLD;
  const maxBuffered = options.maxBufferedBytes ?? DEFAULT_MAX_BUFFERED;
  const decoder = new TextDecoder("utf-8", { fatal: true });

  let handlers: WsHandlers | null = null;
  const pending: Array<(h: WsHandlers) => void> = [];
  let buffer: Buffer = Buffer.alloc(0);
  let fragments: { opcode: number; parts: Buffer[]; size: number } | null = null;
  let closing = false;
  let finished = false;
  let closeCode = 1006;
  let closeReason = "";
  let lastInbound = Date.now();

  const deliver = (event: (h: WsHandlers) => void) => {
    if (handlers) event(handlers);
    else pending.push(event);
  };

  const finish = () => {
    if (finished) return;
    finished = true;
    closing = true;
    clearInterval(pingTimer);
    clearTimeout(idleTimer);
    const code = closeCode;
    const reason = closeReason;
    deliver((h) => h.onClose(code, reason));
  };

  const write = (opcode: number, payload: Uint8Array): boolean => {
    if (closing || socket.destroyed) return false;
    socket.write(frame(opcode, payload));
    if (socket.writableLength > maxBuffered) {
      // A gadget that stopped reading must not grow this process without bound.
      closeCode = 1008;
      closeReason = "send buffer full";
      closing = true;
      socket.destroy();
      finish();
      return false;
    }
    return true;
  };

  const startClose = (code: number, reason: string) => {
    if (closing) return;
    if (!socket.destroyed) socket.write(frame(OP_CLOSE, closePayload(code, reason)));
    closing = true;
    closeCode = code;
    closeReason = reason;
    socket.end();
    // A peer that never answers the close frame must not keep the socket.
    setTimeout(() => socket.destroy(), 1_000).unref?.();
  };

  const fail = (code: number, reason: string) => {
    startClose(code, reason);
  };

  const handleMessage = (opcode: number, payload: Buffer) => {
    if (opcode === OP_TEXT) {
      let text: string;
      try {
        text = decoder.decode(payload);
      } catch {
        fail(1007, "invalid UTF-8");
        return;
      }
      deliver((h) => h.onText(text));
    } else {
      deliver((h) => h.onBinary(payload));
    }
  };

  const handleFrame = (fin: boolean, opcode: number, payload: Buffer) => {
    if (opcode === OP_CLOSE) {
      if (payload.length === 1) {
        fail(1002, "bad close frame");
        return;
      }
      const code = payload.length >= 2 ? payload.readUInt16BE(0) : 1005;
      const reason = payload.length > 2 ? payload.subarray(2).toString("utf8") : "";
      if (!closing) {
        // Echo the peer's code, as RFC 6455 §5.5.1 asks, then let TCP close.
        if (!socket.destroyed) socket.write(frame(OP_CLOSE, payload.length >= 2 ? closePayload(code, "") : Buffer.alloc(0)));
        closing = true;
        closeCode = code;
        closeReason = reason;
        socket.end();
      }
      return;
    }
    if (opcode === OP_PING) {
      if (!closing && !socket.destroyed) socket.write(frame(OP_PONG, payload));
      return;
    }
    if (opcode === OP_PONG) return;
    if (opcode === OP_CONTINUATION) {
      fragments!.parts.push(payload);
      fragments!.size += payload.length;
      if (fin) {
        const whole = Buffer.concat(fragments!.parts, fragments!.size);
        const first = fragments!.opcode;
        fragments = null;
        handleMessage(first, whole);
      }
      return;
    }
    if (!fin) {
      fragments = { opcode, parts: [payload], size: payload.length };
      return;
    }
    handleMessage(opcode, payload);
  };

  const parse = () => {
    while (!closing) {
      if (buffer.length < 2) return;
      const b0 = buffer[0];
      const b1 = buffer[1];
      const fin = (b0 & 0x80) !== 0;
      const opcode = b0 & 0x0f;
      if ((b0 & 0x70) !== 0) return fail(1002, "reserved bits set");
      if ((b1 & 0x80) === 0) return fail(1002, "client frames must be masked");
      if (![OP_CONTINUATION, OP_TEXT, OP_BINARY, OP_CLOSE, OP_PING, OP_PONG].includes(opcode)) {
        return fail(1002, "unknown opcode");
      }
      const control = opcode >= 0x8;
      let length = b1 & 0x7f;
      let offset = 2;
      if (control && (!fin || length > 125)) return fail(1002, "bad control frame");
      if (length === 126) {
        if (buffer.length < 4) return;
        length = buffer.readUInt16BE(2);
        offset = 4;
      } else if (length === 127) {
        if (buffer.length < 10) return;
        if (buffer.readUInt32BE(2) !== 0) return fail(1009, "message too big");
        length = buffer.readUInt32BE(6);
        offset = 10;
      }
      if (!control) {
        if (opcode === OP_CONTINUATION && !fragments) return fail(1002, "unexpected continuation");
        if (opcode !== OP_CONTINUATION && fragments) return fail(1002, "expected a continuation");
        const kind = opcode === OP_CONTINUATION ? fragments!.opcode : opcode;
        const limit = kind === OP_TEXT ? options.maxText : options.maxBinary;
        if ((fragments?.size ?? 0) + length > limit) return fail(1009, "message too big");
      }
      if (buffer.length < offset + 4 + length) return;
      const mask = buffer.subarray(offset, offset + 4);
      const payload = Buffer.from(buffer.subarray(offset + 4, offset + 4 + length));
      for (let i = 0; i < payload.length; i++) payload[i] ^= mask[i & 3];
      buffer = buffer.subarray(offset + 4 + length);
      handleFrame(fin, opcode, payload);
    }
  };

  const pingTimer = setInterval(() => {
    if (!closing && !socket.destroyed) socket.write(frame(OP_PING, Buffer.alloc(0)));
  }, pingIntervalMs);
  pingTimer.unref?.();

  let idleTimer: ReturnType<typeof setTimeout>;
  const armIdle = (delay: number) => {
    idleTimer = setTimeout(() => {
      const quiet = Date.now() - lastInbound;
      if (quiet < idleTimeoutMs) return armIdle(idleTimeoutMs - quiet);
      closeCode = 1006;
      closeReason = "no frames for too long";
      closing = true;
      socket.destroy();
      finish();
    }, delay);
    idleTimer.unref?.();
  };
  armIdle(idleTimeoutMs);

  socket.on("data", (chunk: Buffer) => {
    lastInbound = Date.now();
    buffer = buffer.length ? Buffer.concat([buffer, chunk]) : chunk;
    parse();
  });
  socket.on("error", () => {
    /* 'close' follows and reports the end once */
  });
  socket.on("end", () => {
    if (!socket.destroyed) socket.end();
  });
  socket.on("close", finish);

  if (head.length) {
    lastInbound = Date.now();
    buffer = Buffer.from(head);
    parse();
  }

  const waitForDrain = (): Promise<void> =>
    new Promise((resolve) => {
      const done = () => {
        socket.off("drain", done);
        socket.off("close", done);
        resolve();
      };
      socket.on("drain", done);
      socket.on("close", done);
    });

  return {
    attach(next: WsHandlers) {
      if (handlers) throw new Error("handlers already attached");
      handlers = next;
      for (const event of pending.splice(0)) event(next);
    },
    sendText: (text: string) => write(OP_TEXT, Buffer.from(text, "utf8")),
    sendBinary: (data: Uint8Array) => write(OP_BINARY, data),
    async sendBinaryDrained(data: Uint8Array): Promise<boolean> {
      while (!closing && !socket.destroyed && socket.writableLength > drainThreshold) await waitForDrain();
      return write(OP_BINARY, data);
    },
    close(code = 1000, reason = "") {
      startClose(code, reason);
    },
    terminate() {
      closing = true;
      if (closeCode === 1006 && !closeReason) closeReason = "terminated";
      socket.destroy();
      finish();
    },
    get closed() {
      return closing;
    },
    get bufferedAmount() {
      return socket.writableLength;
    },
    get remoteAddress() {
      return socket.remoteAddress ?? "";
    },
  };
}
````

- [ ] **Step 4: Run the test three times (it exercises real sockets) and the checks**

```bash
cd /Users/omkar/Desktop/openmaus/OpenGrokBot-gadget || exit 1
export PATH="$HOME/.nvm/versions/node/v24.14.1/bin:$PATH"
for i in 1 2 3; do pnpm exec vitest run companion/test/gadget/ws.test.ts; done
pnpm exec tsc -p tsconfig.server.json  # 40-150 s: give this Bash call a timeout of at least 10 minutes
pnpm exec oxlint --deny-warnings companion
```

Expected: `Tests  16 passed (16)` three times; no `tsc` output; oxlint exits 0.

- [ ] **Step 5: Commit**

```bash
cd /Users/omkar/Desktop/openmaus/OpenGrokBot-gadget || exit 1
export PATH="$HOME/.nvm/versions/node/v24.14.1/bin:$PATH"
git add companion/src/gadget/ws.ts companion/test/gadget/ws.test.ts && git commit -m "feat(companion): dependency-free RFC 6455 server for gadgets

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 5: Gadget records in the device registry

**Files:**
- Modify: `companion/src/devices.ts` (origin/main :12-16 imports, :18-38 record types, :47-52 `PairingWindow`, :61-74 replay type and constants, :99-130 names and `normalizeDevice`, :150-165 loader, :174-182 `list`/`count`, :191-209 `openPairing`, :227-263 `redeem`'s window check, :316-338 `authenticate`, :351-382 the two grants)
- Modify: `companion/test/devices.test.ts` (:9-15 import, :78, :400, :413, :423, :439-440, :448) and `companion/test/control.test.ts` (:11, :103): narrow `list()` to phone rows, since `PublicDevice` becomes a union
- Test: `companion/test/gadget/devices-gadget.test.ts`

**Interfaces:**
- Consumes: `GadgetSensors` (type) from Task 1.
- Produces (contract §3.5): `PhoneDeviceRecord`, `GadgetDeviceRecord`, `DeviceRecord` (union), `PublicPhoneDevice`, `PublicDevice`, `PairingWindow.botId?`, `GadgetEnrollment`, `EnrollGadgetError`, `GadgetSettingsPatch`, `cleanGadgetName(raw, id)` (additive), and on `DeviceRegistry`: `openPairing(botId?)`, `setPairingBot(botId | null, expectedToken): boolean`, `redeem(...)` returning `PublicPhoneDevice`, `authenticate(token): PhoneDeviceRecord | null`, `gadget(id)`, `gadgets()`, `enrollGadget(code, gadget)`, `updateGadget(id, patch & {namePending?})`, `noteGadgetHello(id, hello)`, `touchGadget(id, sensors?)`. Enrollment messages: `wrong code`, `the code has expired or was already used`, `too many incorrect codes, so the code is used up`.

- [ ] **Step 1: Write the failing test**

````ts
// Gadget records in the device registry (spec §6.1 registry changes, A28):
// token-less records survive a restart and never authenticate a bearer,
// enrollment shares the pairing window with phones, and the cap is checked
// only after a code matches.
import { readFileSync, rmSync, writeFileSync } from "node:fs";
import { join } from "node:path";
import { beforeEach, describe, expect, it } from "vitest";

import { DeviceRegistry, MAX_DEVICES, MAX_PAIRING_ATTEMPTS, type GadgetEnrollment } from "../../src/devices.ts";
import { DATA_DIR } from "../../src/state.ts";
import { RFC_ID, RFC_PUBKEY } from "./helpers/fixed-values.ts";

const enrollment = (extra: Partial<GadgetEnrollment> = {}): GadgetEnrollment => ({
  id: RFC_ID,
  publicKey: RFC_PUBKEY,
  name: "Desk Maus",
  board: "amoled-175c",
  firmware: "1.0.0",
  botId: "b_jev",
  ...extra,
});

/** Another well-formed gadget id: the registry checks the shape, enroll.ts the key. */
const gadgetId = (n: number) => `gad_${n.toString(16).padStart(16, "0")}`;

const pairPhone = (registry: DeviceRegistry) => {
  const { code } = registry.openPairing();
  const result = registry.redeem(code, "iPhone");
  if ("error" in result) throw new Error(result.error);
  return result;
};

beforeEach(() => {
  rmSync(DATA_DIR, { recursive: true, force: true });
});

describe("enrollGadget", () => {
  it("enrolls with the six digits, consumes the window and survives a restart", () => {
    const registry = new DeviceRegistry();
    const { code } = registry.openPairing("b_picked");
    const result = registry.enrollGadget(code, enrollment({ botId: registry.pairing()?.botId ?? null }));
    expect(result).toMatchObject({ device: { kind: "gadget", id: RFC_ID, botId: "b_picked", speakPushes: false } });
    expect(registry.pairing()).toBeNull();

    const reloaded = new DeviceRegistry();
    expect(reloaded.gadget(RFC_ID)).toMatchObject({ name: "Desk Maus", board: "amoled-175c", firmware: "1.0.0", publicKey: RFC_PUBKEY });
    expect(reloaded.list()).toEqual([expect.objectContaining({ kind: "gadget", id: RFC_ID })]);
    const raw = JSON.parse(readFileSync(join(DATA_DIR, "devices.json"), "utf8"));
    expect(raw.devices[0]).not.toHaveProperty("tokenHash");
  });

  it("never accepts the QR token, and refuses non-digits without burning an attempt", () => {
    const registry = new DeviceRegistry();
    const window = registry.openPairing();
    expect(registry.enrollGadget(window.token, enrollment())).toMatchObject({ error: "bad_code", message: "wrong code" });
    expect(registry.pairing()?.attemptsLeft).toBe(MAX_PAIRING_ATTEMPTS);
    expect(registry.enrollGadget("12345", enrollment())).toMatchObject({ error: "bad_code" });
    expect(registry.pairing()?.attemptsLeft).toBe(MAX_PAIRING_ATTEMPTS);
  });

  it("burns the same five attempts phones use, then says the code is used up", () => {
    const registry = new DeviceRegistry();
    const { code } = registry.openPairing();
    const wrong = code === "000000" ? "111111" : "000000";
    for (let i = 1; i < MAX_PAIRING_ATTEMPTS; i++) {
      expect(registry.enrollGadget(wrong, enrollment())).toMatchObject({ error: "bad_code", message: "wrong code" });
    }
    expect(registry.enrollGadget(wrong, enrollment())).toMatchObject({ error: "too_many", message: expect.stringContaining("used up") });
    expect(registry.enrollGadget(code, enrollment())).toMatchObject({ error: "no_window", message: expect.stringContaining("expired") });
  });

  it("checks the device cap only after the code matches, and keeps the window open", () => {
    const registry = new DeviceRegistry();
    for (let i = 0; i < MAX_DEVICES; i++) pairPhone(registry);
    const { code } = registry.openPairing();
    const wrong = code === "000000" ? "111111" : "000000";
    expect(registry.enrollGadget(wrong, enrollment())).toMatchObject({ error: "bad_code" });
    expect(registry.enrollGadget(code, enrollment())).toMatchObject({ error: "device_limit" });
    expect(registry.pairing()?.code).toBe(code);
    const first = registry.list()[0]!;
    expect(registry.revoke(first.id)).toBe(true);
    expect(registry.enrollGadget(code, enrollment())).toMatchObject({ device: { id: RFC_ID } });
  });

  it("rolls the record back when it cannot be saved", () => {
    const registry = new DeviceRegistry();
    const { code } = registry.openPairing();
    const writable = registry as unknown as { persist: () => void };
    const persist = writable.persist;
    writable.persist = () => { throw new Error("disk full"); };
    try {
      expect(registry.enrollGadget(code, enrollment())).toMatchObject({ error: "save_failed" });
      expect(registry.gadget(RFC_ID)).toBeNull();
    } finally {
      writable.persist = persist;
    }
  });
});

describe("gadget records", () => {
  it("never authenticates a bearer and refuses phone-only grants", () => {
    const registry = new DeviceRegistry();
    const phone = pairPhone(registry);
    const { code } = registry.openPairing();
    registry.enrollGadget(code, enrollment());
    // the loop over records must skip the gadget before comparing digests
    expect(registry.authenticate(phone.token)?.id).toBe(phone.device.id);
    expect(registry.authenticate("")).toBeNull();
    expect(registry.authenticate("omb_anything")).toBeNull();
    expect(registry.setCloudDesktopAccess(RFC_ID, true)).toBe(false);
    expect(registry.setBrowserControlAccess(RFC_ID, true)).toBe(false);
  });

  it("keeps gadget fields across a restart and drops malformed gadget records", () => {
    const registry = new DeviceRegistry();
    pairPhone(registry);
    const file = join(DATA_DIR, "devices.json");
    const stored = JSON.parse(readFileSync(file, "utf8"));
    stored.devices.push(
      { kind: "gadget", id: RFC_ID, publicKey: RFC_PUBKEY, name: "Desk", board: "lcd-154", firmware: "1.0.0-dev", botId: "b_1", speakPushes: true, lastSensors: { battery_pct: 50 }, createdAt: 1, lastSeenAt: 2 },
      { kind: "gadget", id: "gad_short", publicKey: RFC_PUBKEY },
      { kind: "gadget", id: gadgetId(9) },
    );
    writeFileSync(file, JSON.stringify(stored));
    const reloaded = new DeviceRegistry();
    expect(reloaded.count()).toBe(2);
    expect(reloaded.gadgets()).toEqual([
      { kind: "gadget", id: RFC_ID, publicKey: RFC_PUBKEY, name: "Desk", board: "lcd-154", firmware: "1.0.0-dev", botId: "b_1", speakPushes: true, lastSensors: { battery_pct: 50 }, createdAt: 1, lastSeenAt: 2 },
    ]);
  });

  it("updates settings with rollback", () => {
    const registry = new DeviceRegistry();
    registry.enrollGadget(registry.openPairing().code, enrollment());
    expect(registry.updateGadget(RFC_ID, { botId: "b_two", speakPushes: true, name: "  Kitchen\u{7} Maus  " }))
      .toMatchObject({ botId: "b_two", speakPushes: true, name: "Kitchen  Maus" });
    expect(registry.updateGadget(RFC_ID, { botId: null })).toMatchObject({ botId: null });
    expect(registry.updateGadget("gad_0000000000000000", { speakPushes: true })).toBeNull();
    const writable = registry as unknown as { persist: () => void };
    const persist = writable.persist;
    writable.persist = () => { throw new Error("read-only"); };
    try {
      expect(() => registry.updateGadget(RFC_ID, { speakPushes: false, namePending: true })).toThrow("read-only");
      expect(registry.gadget(RFC_ID)).toMatchObject({ speakPushes: true });
      expect(registry.gadget(RFC_ID)).not.toHaveProperty("namePending");
    } finally {
      writable.persist = persist;
    }
  });

  it("lets the last writer win the name, with an offline desktop rename winning once", () => {
    const registry = new DeviceRegistry();
    registry.enrollGadget(registry.openPairing().code, enrollment());
    expect(registry.noteGadgetHello(RFC_ID, { name: "Renamed on console", board: "amoled-175c", firmware: "1.0.1" }))
      .toEqual({ record: expect.objectContaining({ name: "Renamed on console", firmware: "1.0.1" }) });
    registry.updateGadget(RFC_ID, { name: "Desk", namePending: true });
    expect(registry.noteGadgetHello(RFC_ID, { name: "Old console name", board: "amoled-175c", firmware: "1.0.1" }))
      .toEqual({ record: expect.objectContaining({ name: "Desk" }), sendName: "Desk" });
    expect(registry.gadget(RFC_ID)).not.toHaveProperty("namePending");
    expect(registry.noteGadgetHello(RFC_ID, { name: "", board: "amoled-175c", firmware: "1.0.1" })?.record.name).toBe("Maus b18b");
    expect(registry.noteGadgetHello("gad_0000000000000000", { name: "x", board: "devkit", firmware: "1" })).toBeNull();
  });

  it("keeps sensor readings in memory and writes them at most once a minute", () => {
    const registry = new DeviceRegistry();
    registry.enrollGadget(registry.openPairing().code, enrollment());
    registry.touchGadget(RFC_ID, { battery_pct: 81, charging: false, nested: { no: 1 } as unknown as number });
    expect(registry.gadget(RFC_ID)?.lastSensors).toEqual({ battery_pct: 81, charging: false });
    const onDisk = JSON.parse(readFileSync(join(DATA_DIR, "devices.json"), "utf8")).devices[0];
    expect(onDisk.lastSensors).toBeUndefined(); // enrolled just now: the write is throttled
  });
});

describe("pairing window bot", () => {
  it("stores the picked bot and changes it only for the expected window", () => {
    const registry = new DeviceRegistry();
    const window = registry.openPairing("b_one");
    expect(registry.pairing()?.botId).toBe("b_one");
    expect(registry.setPairingBot("b_two", "omb_pair_other")).toBe(false);
    expect(registry.setPairingBot("b_two", window.token)).toBe(true);
    expect(registry.pairing()?.botId).toBe("b_two");
    expect(registry.setPairingBot(null, window.token)).toBe(true);
    expect(registry.pairing()).not.toHaveProperty("botId");
    expect(registry.setPairingBot("../x", window.token)).toBe(false);
    expect(registry.openPairing("bad id!").botId).toBeUndefined();
  });
});
````

- [ ] **Step 2: Run it to see it fail**

Run: `cd /Users/omkar/Desktop/openmaus/OpenGrokBot-gadget && PATH="$HOME/.nvm/versions/node/v24.14.1/bin:$PATH" pnpm exec vitest run companion/test/gadget/devices-gadget.test.ts`
Expected: FAIL — `Tests  11 failed (11)` with `TypeError: registry.enrollGadget is not a function`.

- [ ] **Step 3: Apply the registry change**

The loader accepts `kind: "gadget"` records with a `gad_` id and a string key (and keeps their fields), `authenticate()` skips records without a token before any digest compare (today a token-less record makes `sameDigest` throw on `undefined.length`, crashing every phone request), the two phone grants refuse gadgets (the control routes then answer 404), and `redeem()` and `enrollGadget()` share one window check.

````bash
cd /Users/omkar/Desktop/openmaus/OpenGrokBot-gadget || exit 1
export PATH="$HOME/.nvm/versions/node/v24.14.1/bin:$PATH"
git apply <<'PATCH'
--- a/companion/src/devices.ts
+++ b/companion/src/devices.ts
@@ -13,16 +13,22 @@
 import { readFileSync } from "node:fs";
 import { join } from "node:path";
 
+import type { GadgetSensors } from "./gadget/protocol.ts";
 import { DATA_DIR, ensureDataDir, writeFileAtomic } from "./state.ts";
 
-/** One paired phone, as it is written to disk. */
-export interface DeviceRecord {
+interface DeviceRecordBase {
   id: string;
   name: string;
-  /** sha256 of the bearer token — never the token itself */
-  tokenHash: string;
   createdAt: number;
   lastSeenAt: number;
+}
+
+/** One paired phone, as it is written to disk. A record with no `kind` on
+ * disk is a phone: every record written before gadgets existed. */
+export interface PhoneDeviceRecord extends DeviceRecordBase {
+  kind?: "phone";
+  /** sha256 of the bearer token — never the token itself */
+  tokenHash: string;
   /** Full interactive access to a bot's cloud desktop. Deliberately off on
    * every new and migrated device until the computer owner enables it. */
   cloudDesktopAccess: boolean;
@@ -34,9 +40,49 @@
   browserControlAccess: boolean;
 }
 
-/** What the UI is allowed to see: a device without its secret. */
-export type PublicDevice = Omit<DeviceRecord, "tokenHash">;
+/** One paired gadget. It holds no token: it proves possession of its
+ * private key on every connection (gadget/enroll.ts), and its id is derived
+ * from that key, so no other device can claim it. */
+export interface GadgetDeviceRecord extends DeviceRecordBase {
+  kind: "gadget";
+  /** canonical base64 of the 65-byte SEC1 point; id = "gad_" + first 16 hex of its sha256 */
+  publicKey: string;
+  board: string;
+  /** last hello.fw, at most 32 characters */
+  firmware: string;
+  /** the bot this gadget talks to; null until one is chosen or found */
+  botId: string | null;
+  /** "Read pushes aloud" */
+  speakPushes: boolean;
+  lastSensors?: GadgetSensors;
+  /** A desktop rename made while the gadget was offline (last writer wins). */
+  namePending?: true;
+}
 
+export type DeviceRecord = PhoneDeviceRecord | GadgetDeviceRecord;
+
+/** What the UI is allowed to see: a phone without its secret. Gadget records
+ * carry no secret, so they pass through whole. */
+export type PublicPhoneDevice = Omit<PhoneDeviceRecord, "tokenHash">;
+export type PublicDevice = PublicPhoneDevice | GadgetDeviceRecord;
+
+/** What the hub knows about a gadget when it enrolls. */
+export interface GadgetEnrollment {
+  id: string;
+  publicKey: string;
+  name: string;
+  board: string;
+  firmware: string;
+  botId: string | null;
+}
+export type EnrollGadgetError = "no_window" | "bad_code" | "too_many" | "device_limit" | "save_failed";
+
+export interface GadgetSettingsPatch {
+  botId?: string | null;
+  speakPushes?: boolean;
+  name?: string;
+}
+
 /** A pairing window: two short-lived credentials, deliberately single-use.
  *
  * `token` is the primary path carried inside the QR code. It has enough
@@ -49,6 +95,8 @@
   token: string;
   expiresAt: number;
   attemptsLeft: number;
+  /** Bot chosen in "Pair a gadget"; absent for the phone flows. */
+  botId?: string;
 }
 
 /** A successful redemption kept only long enough for the *same* phone request
@@ -62,7 +110,7 @@
   requestId: string;
   credentialHash: string;
   expiresAt: number;
-  result: { device: PublicDevice; token: string };
+  result: { device: PublicPhoneDevice; token: string };
 }
 
 const DEVICES_FILE = join(DATA_DIR, "devices.json");
@@ -72,6 +120,20 @@
 export const MAX_DEVICES = 20;
 /** lastSeen is a UI nicety, not an audit log — don't write on every request. */
 const LAST_SEEN_WRITE_MS = 60_000;
+const GADGET_ID = /^gad_[0-9a-f]{16}$/;
+const PAIR_CODE = /^\d{6}$/;
+const BOT_ID = /^[\w-]{1,120}$/;
+const GADGET_NAME_MAX = 32;
+const REDEEM_MESSAGES: Record<"no_window" | "bad_code" | "too_many", string> = {
+  no_window: "no pairing is in progress — open Phone settings on your computer",
+  too_many: "too many incorrect codes — start pairing again",
+  bad_code: "that pairing credential is not right",
+};
+const ENROLL_MESSAGES: Record<"no_window" | "bad_code" | "too_many", string> = {
+  bad_code: "wrong code",
+  no_window: "the code has expired or was already used",
+  too_many: "too many incorrect codes, so the code is used up",
+};
 
 /** Hex digest. Tokens live on disk as one of these and never in the clear. */
 const sha256 = (value: string) => createHash("sha256").update(value).digest("hex");
@@ -105,8 +167,32 @@
     .trim()
     .slice(0, 60);
   return name || "Companion";
+}
+
+/** A gadget's name: display text from the device, at most 32 characters,
+ * with the firmware's own default when it is empty. */
+export function cleanGadgetName(raw: unknown, id: string): string {
+  const name = Array.from(
+    String(raw ?? "")
+      // oxlint-disable-next-line no-control-regex -- strips control characters from a display name
+      .replace(/[\u0000-\u001f\u007f]/g, " ")
+      .trim(),
+  ).slice(0, GADGET_NAME_MAX).join("").trim();
+  return name || `Maus ${id.slice(4, 8)}`;
 }
 
+/** Sensor readings worth keeping: at most 16 keys of numbers, booleans and short strings. */
+function cleanSensors(raw: unknown): GadgetSensors | undefined {
+  if (!raw || typeof raw !== "object" || Array.isArray(raw)) return undefined;
+  const out: GadgetSensors = {};
+  for (const [key, value] of Object.entries(raw).slice(0, 16)) {
+    if (!/^[a-z][a-z0-9_]{0,31}$/.test(key)) continue;
+    if ((typeof value === "number" && Number.isFinite(value)) || typeof value === "boolean") out[key] = value;
+    else if (typeof value === "string") out[key] = value.slice(0, 64);
+  }
+  return out;
+}
+
 /** A timestamp we are willing to render, or a stand-in. `0` and the negatives
  * are as wrong as a missing field and read worse: they date a device to 1970
  * in the UI, where "now" is at least true of when we learned of it. */
@@ -116,7 +202,7 @@
 /** Complete a stored record, whatever shape the file had. `lastSeenAt` falls
  * back to `createdAt` rather than to the clock: a device we have never heard
  * from since pairing was last seen when it paired. */
-function normalizeDevice(record: Partial<DeviceRecord> & { id: string; tokenHash: string }): DeviceRecord {
+function normalizeDevice(record: Partial<PhoneDeviceRecord> & { id: string; tokenHash: string }): PhoneDeviceRecord {
   const createdAt = timestamp(record.createdAt, Date.now());
   return {
     id: record.id,
@@ -129,6 +215,29 @@
   };
 }
 
+/** The same for a gadget record: its key and id decide whether it is a
+ * device; everything else gets a default. */
+function normalizeGadget(record: Partial<GadgetDeviceRecord> & { id: string; publicKey: string }): GadgetDeviceRecord {
+  const createdAt = timestamp(record.createdAt, Date.now());
+  const sensors = cleanSensors(record.lastSensors);
+  return {
+    kind: "gadget",
+    id: record.id,
+    name: cleanGadgetName(record.name, record.id),
+    publicKey: record.publicKey,
+    board: typeof record.board === "string" && /^[a-z0-9-]{1,32}$/.test(record.board) ? record.board : "unknown",
+    firmware: typeof record.firmware === "string" ? record.firmware.slice(0, 32) : "",
+    botId: typeof record.botId === "string" && BOT_ID.test(record.botId) ? record.botId : null,
+    speakPushes: record.speakPushes === true,
+    createdAt,
+    lastSeenAt: timestamp(record.lastSeenAt, createdAt),
+    ...(sensors ? { lastSensors: sensors } : {}),
+    ...(record.namePending === true ? { namePending: true as const } : {}),
+  };
+}
+
+const isGadgetRecord = (record: DeviceRecord): record is GadgetDeviceRecord => record.kind === "gadget";
+
 /** The paired fleet: who may reach the harness through the sidecar, and the
  * one short-lived window in which a new phone may join it. Backed by a file,
  * loaded once at construction and written on every change. */
@@ -151,13 +260,20 @@
     try {
       const parsed = JSON.parse(readFileSync(DEVICES_FILE, "utf8"));
       if (Array.isArray(parsed?.devices)) {
-        this.devices = parsed.devices
-          .filter(
-            (d: unknown): d is Partial<DeviceRecord> & { id: string; tokenHash: string } =>
-              typeof (d as DeviceRecord)?.id === "string" &&
-              typeof (d as DeviceRecord)?.tokenHash === "string",
-          )
-          .map(normalizeDevice);
+        this.devices = (parsed.devices as unknown[]).flatMap((d): DeviceRecord[] => {
+          if (!d || typeof d !== "object") return [];
+          const record = d as Record<string, unknown>;
+          if (record.kind === "gadget") {
+            // A gadget is its key: without a well-formed id and key it can
+            // never prove itself, so it is not a device.
+            return typeof record.id === "string" && GADGET_ID.test(record.id) && typeof record.publicKey === "string"
+              ? [normalizeGadget(record as Partial<GadgetDeviceRecord> & { id: string; publicKey: string })]
+              : [];
+          }
+          return typeof record.id === "string" && typeof record.tokenHash === "string"
+            ? [normalizeDevice(record as Partial<PhoneDeviceRecord> & { id: string; tokenHash: string })]
+            : [];
+        });
       }
     } catch {
       /* first run, or a file we can't read — start with no paired devices */
@@ -173,10 +289,14 @@
 
   /** Every paired device, without the hash — this is what the page renders. */
   list(): PublicDevice[] {
-    return this.devices.map(({ tokenHash: _tokenHash, ...rest }) => rest);
+    return this.devices.map((device): PublicDevice => {
+      if (isGadgetRecord(device)) return { ...device };
+      const { tokenHash: _tokenHash, ...rest } = device;
+      return rest;
+    });
   }
 
-  /** How many phones are paired, against MAX_DEVICES. */
+  /** How many devices (phones and gadgets) are paired, against MAX_DEVICES. */
   count(): number {
     return this.devices.length;
   }
@@ -190,17 +310,32 @@
 
   /** Open a fresh window, replacing any that was already open. The code is
    * from `randomInt`, not `Math.random` — it is a credential for two minutes. */
-  openPairing(): PairingWindow {
+  openPairing(botId?: string): PairingWindow {
     this.clearReplay();
     this.window = {
       code: String(randomInt(0, 1_000_000)).padStart(6, "0"),
       token: `omb_pair_${randomBytes(32).toString("base64url")}`,
       expiresAt: Date.now() + PAIRING_TTL_MS,
       attemptsLeft: MAX_PAIRING_ATTEMPTS,
+      ...(botId && BOT_ID.test(botId) ? { botId } : {}),
     };
     return this.window;
   }
 
+  /** Change the bot the open window gives a gadget ("Talks to" in Pair a
+   * gadget). False when no window is open or it is not the expected one. */
+  setPairingBot(botId: string | null, expectedToken: string): boolean {
+    const window = this.pairing();
+    if (!window || window.token !== expectedToken) return false;
+    if (botId === null) {
+      delete window.botId;
+      return true;
+    }
+    if (!BOT_ID.test(botId)) return false;
+    window.botId = botId;
+    return true;
+  }
+
   closePairing(expectedToken?: string): boolean {
     if (expectedToken !== undefined && this.pairing()?.token !== expectedToken) return false;
     this.window = null;
@@ -228,7 +363,7 @@
     credential: string,
     name: unknown,
     pairRequestId?: unknown,
-  ): { device: PublicDevice; token: string } | { error: string } {
+  ): { device: PublicPhoneDevice; token: string } | { error: string } {
     const presented = String(credential ?? "");
     const requestId =
       typeof pairRequestId === "string" && /^[A-Za-z0-9._-]{16,128}$/.test(pairRequestId)
@@ -249,18 +384,9 @@
       return this.replay.result;
     }
 
-    const window = this.pairing();
-    if (!window) return { error: "no pairing is in progress — open Phone settings on your computer" };
-    if (!sameCredential(window.code, presented) && !sameCredential(window.token, presented)) {
-      window.attemptsLeft -= 1;
-      // A burned window is the whole point: without this, six digits is a
-      // few seconds of guessing.
-      if (window.attemptsLeft <= 0) {
-        this.closePairing();
-        return { error: "too many incorrect codes — start pairing again" };
-      }
-      return { error: "that pairing credential is not right" };
-    }
+    const matched = this.matchWindow(presented, true);
+    if (matched.error) return { error: REDEEM_MESSAGES[matched.error] };
+    const window = matched.window;
     // After the code, not before. Checked first, a full fleet answers every
     // wrong guess with "too many paired devices" — which tells a guesser
     // something about this machine, and costs them none of their five
@@ -272,7 +398,7 @@
     this.window = null;
 
     const token = `omb_${randomBytes(32).toString("base64url")}`;
-    const device: DeviceRecord = {
+    const device: PhoneDeviceRecord = {
       id: randomUUID(),
       name: cleanDeviceName(name),
       tokenHash: sha256(token),
@@ -313,11 +439,159 @@
     return result;
   }
 
-  /** Resolve a bearer token to its device, or null. */
-  authenticate(token: string | undefined): DeviceRecord | null {
+  /** The shared half of redeem() and enrollGadget(): check a credential
+   * against the open window, burning an attempt on a miss and the window
+   * when the attempts run out. Phones may present the QR token too. */
+  private matchWindow(
+    presented: string,
+    acceptToken: boolean,
+  ): { window: PairingWindow; error?: undefined } | { window?: undefined; error: "no_window" | "bad_code" | "too_many" } {
+    const window = this.pairing();
+    if (!window) return { error: "no_window" };
+    if (!sameCredential(window.code, presented) && !(acceptToken && sameCredential(window.token, presented))) {
+      window.attemptsLeft -= 1;
+      // A burned window is the whole point: without this, six digits is a
+      // few seconds of guessing.
+      if (window.attemptsLeft <= 0) {
+        this.closePairing();
+        return { error: "too_many" };
+      }
+      return { error: "bad_code" };
+    }
+    return { window };
+  }
+
+  /** The gadget with this id, or null. A copy: change it through updateGadget. */
+  gadget(id: string): GadgetDeviceRecord | null {
+    const found = this.devices.find((device): device is GadgetDeviceRecord => isGadgetRecord(device) && device.id === id);
+    return found ? { ...found } : null;
+  }
+
+  /** Every paired gadget. */
+  gadgets(): GadgetDeviceRecord[] {
+    return this.devices.filter(isGadgetRecord).map((device) => ({ ...device }));
+  }
+
+  /** Enroll a gadget with the six-digit code from the shared pairing window.
+   *
+   * The same window, attempts and consumption as redeem(), with three
+   * differences: only the six digits count (never the QR token), anything
+   * that is not six digits is refused without burning an attempt, and the
+   * device cap is checked only once the code matched, so a full fleet tells
+   * a guesser nothing and the person can remove a device and retry. */
+  enrollGadget(code: string, gadget: GadgetEnrollment):
+    | { device: GadgetDeviceRecord }
+    | { error: EnrollGadgetError; message: string } {
+    const presented = String(code ?? "");
+    if (!PAIR_CODE.test(presented)) return { error: "bad_code", message: ENROLL_MESSAGES.bad_code };
+    const matched = this.matchWindow(presented, false);
+    if (matched.error) return { error: matched.error, message: ENROLL_MESSAGES[matched.error] };
+    if (this.devices.length >= MAX_DEVICES) {
+      return { error: "device_limit", message: "too many paired devices; remove one in MausBot → Settings → Remote access" };
+    }
+    this.window = null;
+    const now = Date.now();
+    const device: GadgetDeviceRecord = {
+      kind: "gadget",
+      id: gadget.id,
+      name: cleanGadgetName(gadget.name, gadget.id),
+      publicKey: gadget.publicKey,
+      board: gadget.board,
+      firmware: gadget.firmware.slice(0, 32),
+      botId: gadget.botId && BOT_ID.test(gadget.botId) ? gadget.botId : null,
+      speakPushes: false,
+      createdAt: now,
+      lastSeenAt: now,
+    };
+    this.devices.push(device);
+    try {
+      this.persist();
+    } catch (e) {
+      this.devices.pop();
+      return { error: "save_failed", message: `could not save the pairing: ${(e as Error).message}` };
+    }
+    this.lastSeenWrites.set(device.id, now);
+    return { device: { ...device } };
+  }
+
+  /** Change a gadget's bot, push setting, name or pending-rename mark.
+   * Persists with rollback; null when there is no such gadget; throws when
+   * saving fails, like setCloudDesktopAccess. */
+  updateGadget(id: string, patch: GadgetSettingsPatch & { namePending?: boolean }): GadgetDeviceRecord | null {
+    const device = this.devices.find((candidate): candidate is GadgetDeviceRecord => isGadgetRecord(candidate) && candidate.id === id);
+    if (!device) return null;
+    const before = { ...device };
+    if (patch.botId !== undefined) device.botId = patch.botId !== null && BOT_ID.test(patch.botId) ? patch.botId : null;
+    if (patch.speakPushes !== undefined) device.speakPushes = patch.speakPushes;
+    if (patch.name !== undefined) device.name = cleanGadgetName(patch.name, id);
+    if (patch.namePending === true) device.namePending = true;
+    if (patch.namePending === false) delete device.namePending;
+    try {
+      this.persist();
+    } catch (error) {
+      Object.assign(device, before);
+      if (!before.namePending) delete device.namePending;
+      throw error;
+    }
+    return { ...device };
+  }
+
+  /** On every hello: the gadget's name wins unless a desktop rename is
+   * pending (then the record's name wins once, and the caller sends it in
+   * `settings` right after `ready`); board, firmware and lastSeenAt follow
+   * the hello. A failed write is not a reason to refuse the connection. */
+  noteGadgetHello(id: string, hello: { name: string; board: string; firmware: string }): { record: GadgetDeviceRecord; sendName?: string } | null {
+    const device = this.devices.find((candidate): candidate is GadgetDeviceRecord => isGadgetRecord(candidate) && candidate.id === id);
+    if (!device) return null;
+    let sendName: string | undefined;
+    const before = JSON.stringify([device.name, device.board, device.firmware, device.namePending]);
+    if (device.namePending) {
+      sendName = device.name;
+      delete device.namePending;
+    } else {
+      device.name = cleanGadgetName(hello.name, id);
+    }
+    device.board = hello.board;
+    device.firmware = hello.firmware.slice(0, 32);
+    const now = Date.now();
+    device.lastSeenAt = now;
+    const changed = before !== JSON.stringify([device.name, device.board, device.firmware, device.namePending]);
+    if (changed || now - (this.lastSeenWrites.get(id) ?? 0) > LAST_SEEN_WRITE_MS) {
+      this.lastSeenWrites.set(id, now);
+      try {
+        this.persist();
+      } catch {
+        /* the gadget is still paired; the labels can wait */
+      }
+    }
+    return { record: { ...device }, ...(sendName !== undefined ? { sendName } : {}) };
+  }
+
+  /** lastSeenAt and lastSensors in memory; written at most every 60 s. */
+  touchGadget(id: string, sensors?: GadgetSensors): void {
+    const device = this.devices.find((candidate): candidate is GadgetDeviceRecord => isGadgetRecord(candidate) && candidate.id === id);
+    if (!device) return;
+    const now = Date.now();
+    device.lastSeenAt = now;
+    const cleaned = sensors === undefined ? undefined : cleanSensors(sensors);
+    if (cleaned) device.lastSensors = { ...device.lastSensors, ...cleaned };
+    if (now - (this.lastSeenWrites.get(id) ?? 0) <= LAST_SEEN_WRITE_MS) return;
+    this.lastSeenWrites.set(id, now);
+    try {
+      this.persist();
+    } catch {
+      /* readings are a UI nicety */
+    }
+  }
+
+  /** Resolve a bearer token to its phone, or null. Gadgets hold no token,
+   * so they are skipped before any digest is compared. */
+  authenticate(token: string | undefined): PhoneDeviceRecord | null {
     if (!token) return null;
     const hash = sha256(token);
-    const device = this.devices.find((d) => sameDigest(d.tokenHash, hash));
+    const device = this.devices.find(
+      (d): d is PhoneDeviceRecord => !isGadgetRecord(d) && sameDigest(d.tokenHash, hash),
+    );
     if (!device) return null;
     const now = Date.now();
     if (now - (this.lastSeenWrites.get(device.id) ?? 0) > LAST_SEEN_WRITE_MS) {
@@ -352,7 +626,8 @@
    * into full desktop control. This is per device so a watch-only phone does
    * not inherit a different phone's permission. */
   setCloudDesktopAccess(id: string, allowed: boolean): boolean {
-    const device = this.devices.find((candidate) => candidate.id === id);
+    // Gadgets never get either grant: false here makes the control route 404.
+    const device = this.devices.find((candidate): candidate is PhoneDeviceRecord => !isGadgetRecord(candidate) && candidate.id === id);
     if (!device) return false;
     const previous = device.cloudDesktopAccess;
     device.cloudDesktopAccess = allowed;
@@ -368,7 +643,7 @@
   /** Grant or remove the capability to watch and drive a bot's browser.
    * Separate from the desktop grant above on purpose: see the field. */
   setBrowserControlAccess(id: string, allowed: boolean): boolean {
-    const device = this.devices.find((candidate) => candidate.id === id);
+    const device = this.devices.find((candidate): candidate is PhoneDeviceRecord => !isGadgetRecord(candidate) && candidate.id === id);
     if (!device) return false;
     const previous = device.browserControlAccess;
     device.browserControlAccess = allowed;
PATCH
````

- [ ] **Step 4: Narrow the two existing tests to phone rows**

````bash
cd /Users/omkar/Desktop/openmaus/OpenGrokBot-gadget || exit 1
export PATH="$HOME/.nvm/versions/node/v24.14.1/bin:$PATH"
git apply <<'PATCH'
--- a/companion/test/devices.test.ts
+++ b/companion/test/devices.test.ts
@@ -12,8 +12,13 @@
   DeviceRegistry,
   MAX_PAIRING_ATTEMPTS,
   PAIRING_TTL_MS,
+  type PublicPhoneDevice,
 } from "../src/devices.ts";
 
+/** The phone rows of list(): gadgets carry no phone grants. */
+const phoneList = (registry: DeviceRegistry): PublicPhoneDevice[] =>
+  registry.list().filter((device): device is PublicPhoneDevice => device.kind !== "gadget");
+
 const pair = (registry: DeviceRegistry, name = "iPhone") => {
   const { code } = registry.openPairing();
   const result = registry.redeem(code, name);
@@ -75,7 +80,7 @@
     writeFileSync(file, JSON.stringify(stored));
 
     const reloaded = new DeviceRegistry();
-    const [listed] = reloaded.list();
+    const [listed] = phoneList(reloaded);
     expect(listed.id).toBe(device.id);
     expect(listed.name).toBe("Companion");
     expect(Number.isFinite(listed.lastSeenAt)).toBe(true);
@@ -397,7 +402,7 @@
     const { device } = pair(registry);
 
     expect(device.browserControlAccess).toBe(false);
-    expect(registry.list()[0].browserControlAccess).toBe(false);
+    expect(phoneList(registry)[0].browserControlAccess).toBe(false);
   });
 
   it("reads a record written before the capability existed as off", () => {
@@ -410,7 +415,7 @@
     delete stored.devices[0].browserControlAccess;
     writeFileSync(path, JSON.stringify(stored));
 
-    expect(new DeviceRegistry().list().find((d) => d.id === device.id)?.browserControlAccess).toBe(false);
+    expect(phoneList(new DeviceRegistry()).find((d) => d.id === device.id)?.browserControlAccess).toBe(false);
   });
 
   it("grants and revokes per device, leaving the other one alone", () => {
@@ -420,7 +425,7 @@
 
     expect(registry.setBrowserControlAccess(first.id, true)).toBe(true);
 
-    const byId = (id: string) => registry.list().find((d) => d.id === id)!;
+    const byId = (id: string) => phoneList(registry).find((d) => d.id === id)!;
     expect(byId(first.id).browserControlAccess).toBe(true);
     expect(byId(second.id).browserControlAccess).toBe(false);
 
@@ -436,8 +441,8 @@
 
     // Trusting a device with a throwaway VM must never hand it the person's
     // signed-in browser as a side effect.
-    expect(registry.list()[0].cloudDesktopAccess).toBe(true);
-    expect(registry.list()[0].browserControlAccess).toBe(false);
+    expect(phoneList(registry)[0].cloudDesktopAccess).toBe(true);
+    expect(phoneList(registry)[0].browserControlAccess).toBe(false);
   });
 
   it("survives a restart and refuses an unknown device", () => {
@@ -445,7 +450,7 @@
     const { device } = pair(registry);
     registry.setBrowserControlAccess(device.id, true);
 
-    expect(new DeviceRegistry().list()[0].browserControlAccess).toBe(true);
+    expect(phoneList(new DeviceRegistry())[0].browserControlAccess).toBe(true);
     expect(registry.setBrowserControlAccess("nobody", true)).toBe(false);
   });
 });
PATCH
````

````bash
cd /Users/omkar/Desktop/openmaus/OpenGrokBot-gadget || exit 1
export PATH="$HOME/.nvm/versions/node/v24.14.1/bin:$PATH"
git apply <<'PATCH'
--- a/companion/test/control.test.ts
+++ b/companion/test/control.test.ts
@@ -8,7 +8,7 @@
 import { afterAll, beforeAll, describe, expect, it } from "vitest";
 
 import { createControlServer, hostCandidates, originIsLoopback } from "../src/control.ts";
-import { DeviceRegistry } from "../src/devices.ts";
+import { DeviceRegistry, type PublicPhoneDevice } from "../src/devices.ts";
 
 let control: Server;
 let port = 0;
@@ -100,7 +100,7 @@
         status: 500,
         body: { error: "could not save cloud desktop access" },
       });
-      expect(devices.list().find((candidate) => candidate.id === device.id)?.cloudDesktopAccess).toBe(false);
+      expect((devices.list().find((candidate) => candidate.id === device.id) as PublicPhoneDevice | undefined)?.cloudDesktopAccess).toBe(false);
       expect((await ask("GET", "/state")).status).toBe(200);
     } finally {
       writable.persist = persist;
PATCH
````

- [ ] **Step 5: Run the new and the existing tests and the checks**

```bash
cd /Users/omkar/Desktop/openmaus/OpenGrokBot-gadget || exit 1
export PATH="$HOME/.nvm/versions/node/v24.14.1/bin:$PATH"
pnpm exec vitest run companion/test/gadget/devices-gadget.test.ts companion/test/devices.test.ts companion/test/control.test.ts companion/test/proxy.test.ts
pnpm exec tsc -p tsconfig.server.json  # 40-150 s: give this Bash call a timeout of at least 10 minutes
pnpm exec oxlint --deny-warnings companion
```

Expected: all four files pass (`devices-gadget` 11, `devices` 29, `control` 20, plus `proxy.test.ts`, which spawns the real harness); no `tsc` output; oxlint exits 0.

- [ ] **Step 6: Commit**

```bash
cd /Users/omkar/Desktop/openmaus/OpenGrokBot-gadget || exit 1
export PATH="$HOME/.nvm/versions/node/v24.14.1/bin:$PATH"
git add companion/src/devices.ts companion/test/devices.test.ts companion/test/control.test.ts companion/test/gadget/devices-gadget.test.ts && git commit -m "feat(companion): gadget records and code-only enrollment in the registry

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 6: The host id

**Files:**
- Create: `companion/src/host-id.ts`
- Test: `companion/test/gadget/host-id.test.ts`

**Interfaces:**
- Consumes: `clampBytes` (`companion/src/mdns.ts:433`), `DATA_DIR`, `ensureDataDir`, `writeFileAtomic` (`companion/src/state.ts`).
- Produces (contract §3.6): `loadOrCreateHostId(): string` (`DATA_DIR/host.json` = `{"hostId":"<32 hex>"}`, 0600), plus additive `hostIdFile(): string` and `serviceTxt(machineName, hostId): string[]` = `["v=1", "name=<clamped>", "id=<hostId>"]` (Task 13 uses it for the TXT record).

- [ ] **Step 1: Write the failing test**

````ts
// The companion's host id (spec §4.1 encodings, §6.1 mDNS and host id).
import { readFileSync, rmSync, statSync, writeFileSync } from "node:fs";
import { beforeEach, describe, expect, it } from "vitest";

import { hostIdFile, loadOrCreateHostId, serviceTxt } from "../../src/host-id.ts";
import { DATA_DIR } from "../../src/state.ts";

beforeEach(() => {
  rmSync(DATA_DIR, { recursive: true, force: true });
});

describe("loadOrCreateHostId", () => {
  it("creates 32 lowercase hex once and returns the same id after a restart", () => {
    const first = loadOrCreateHostId();
    expect(first).toMatch(/^[0-9a-f]{32}$/);
    expect(loadOrCreateHostId()).toBe(first);
    expect(JSON.parse(readFileSync(hostIdFile(), "utf8"))).toEqual({ hostId: first });
    if (process.platform !== "win32") expect(statSync(hostIdFile()).mode & 0o777).toBe(0o600);
  });

  it("replaces a malformed file", () => {
    loadOrCreateHostId();
    writeFileSync(hostIdFile(), JSON.stringify({ hostId: "h_0123456789abcdef" }));
    const replaced = loadOrCreateHostId();
    expect(replaced).toMatch(/^[0-9a-f]{32}$/);
    writeFileSync(hostIdFile(), "{not json");
    expect(loadOrCreateHostId()).toMatch(/^[0-9a-f]{32}$/);
  });

  it("still answers when the data directory cannot be written", () => {
    writeFileSync(DATA_DIR, "a file where the directory should be");
    try {
      expect(loadOrCreateHostId()).toMatch(/^[0-9a-f]{32}$/);
    } finally {
      rmSync(DATA_DIR, { force: true });
    }
  });
});

describe("serviceTxt", () => {
  it("keeps v=1 and the name and adds id=", () => {
    expect(serviceTxt("Ada's computer", "0123456789abcdef0123456789abcdef")).toEqual([
      "v=1",
      "name=Ada's computer",
      "id=0123456789abcdef0123456789abcdef",
    ]);
    expect(Buffer.byteLength(serviceTxt("é".repeat(300), "0".repeat(32))[1]!)).toBeLessThanOrEqual(205);
  });
});
````

- [ ] **Step 2: Run it to see it fail**

Run: `cd /Users/omkar/Desktop/openmaus/OpenGrokBot-gadget && PATH="$HOME/.nvm/versions/node/v24.14.1/bin:$PATH" pnpm exec vitest run companion/test/gadget/host-id.test.ts`
Expected: FAIL — `Cannot find module '../../src/host-id.ts'`.

- [ ] **Step 3: Write `companion/src/host-id.ts`**

````ts
// This companion's stable identity, as gadgets know it.
//
// A gadget signs `host_id` into every proof (gadget/enroll.ts), which binds
// a session to one MausBot, and it is advertised as `id=` in the Bonjour TXT
// record so a gadget can tell computers apart before it connects. Created
// once and kept in its own file, apart from devices.json, so a downgrade
// that rewrites the device list cannot change it.
import { randomBytes } from "node:crypto";
import { readFileSync } from "node:fs";
import { join } from "node:path";

import { clampBytes } from "./mdns.ts";
import { DATA_DIR, ensureDataDir, writeFileAtomic } from "./state.ts";

const HOST_ID = /^[0-9a-f]{32}$/;

/** Where the id lives: DATA_DIR/host.json, {"hostId":"<32 lowercase hex>"}. */
export const hostIdFile = (): string => join(DATA_DIR, "host.json");

/** The stored id, or a new one (16 random bytes as lowercase hex). An
 * unreadable or malformed file is replaced; a failed save logs and still
 * returns the new id, so the companion starts either way. */
export function loadOrCreateHostId(): string {
  try {
    const stored: unknown = JSON.parse(readFileSync(hostIdFile(), "utf8"));
    const hostId = (stored as { hostId?: unknown } | null)?.hostId;
    if (typeof hostId === "string" && HOST_ID.test(hostId)) return hostId;
  } catch {
    /* first run, or a file worth replacing */
  }
  const hostId = randomBytes(16).toString("hex");
  try {
    ensureDataDir();
    writeFileAtomic(hostIdFile(), `${JSON.stringify({ hostId }, null, 2)}\n`);
  } catch (error) {
    console.warn(`companion: host id could not be saved — ${(error as Error).message}`);
  }
  return hostId;
}

/** The _openmausbot._tcp TXT entries: version, display name, host id. */
export function serviceTxt(machineName: string, hostId: string): string[] {
  // TXT entries cap at 255 bytes, and the name is user-supplied — measured in
  // bytes, since that is the unit the wire format counts in.
  return ["v=1", `name=${clampBytes(machineName, 200)}`, `id=${hostId}`];
}
````

- [ ] **Step 4: Run the test and the checks**

```bash
cd /Users/omkar/Desktop/openmaus/OpenGrokBot-gadget || exit 1
export PATH="$HOME/.nvm/versions/node/v24.14.1/bin:$PATH"
pnpm exec vitest run companion/test/gadget/host-id.test.ts
pnpm exec tsc -p tsconfig.server.json  # 40-150 s: give this Bash call a timeout of at least 10 minutes
pnpm exec oxlint --deny-warnings companion
```

Expected: `Tests  4 passed (4)`; no `tsc` output; oxlint exits 0.

- [ ] **Step 5: Commit**

```bash
cd /Users/omkar/Desktop/openmaus/OpenGrokBot-gadget || exit 1
export PATH="$HOME/.nvm/versions/node/v24.14.1/bin:$PATH"
git add companion/src/host-id.ts companion/test/gadget/host-id.test.ts && git commit -m "feat(companion): stable host id for gadget proofs and Bonjour

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 7: The hub's harness client and the fake harness

**Files:**
- Create: `companion/src/gadget/harness-client.ts`
- Create: `companion/test/gadget/helpers/fake-harness.ts` (shared with P3b, P4a, P4b)
- Test: `companion/test/gadget/harness-client.test.ts`

**Interfaces:**
- Consumes: `companionIdentityHeaders(deviceId?, token?)` (`companion/src/proxy.ts:224`), `denyReason({path, method, authenticated})` (`companion/src/routes.ts:327`), `ServerFrameLite` (Task 2).
- Produces (contract §3.7): `HarnessClientOptions`, `HarnessRefused` (`status`), `HarnessJson<T>`, `HarnessRaw`, `HarnessMethod`, `HarnessClient` (`json`, `raw`, `events`), `createHarnessClient(options)`. `json()` with `deviceId === null` is GET-only and sends the marker header only; per-gadget calls fail with `HarnessRefused(503)` while the Electron relay token is pending. The test helper exports `startFakeHarness(): Promise<FakeHarness>` with the contract's members plus additive `dropStreams()`, `waitForSubscriber(timeoutMs?)` and `subscribers()`. Its event stream opens with `hello` and then a `ping` with no event id, as contract §3.19 pins and as the real harness's keep-alive is written (origin/main `server/index.ts:18895`).

- [ ] **Step 1: Write the fake harness**

````ts
// A stand-in harness for gadget tests (P3a; reused by P3b, P4a, P4b). It
// records every request, answers the routes the hub uses with plausible
// bodies, and serves one SSE stream that tests drive with emit().
import { Buffer } from "node:buffer";
import { randomBytes } from "node:crypto";
import { createServer, type IncomingHttpHeaders, type ServerResponse } from "node:http";

import type { WireBotLite } from "../../../src/gadget/types.ts";

export interface RecordedRequest { method: string; path: string; headers: IncomingHttpHeaders; body: Buffer; json?: unknown }
export type FakeReply = { status: number; json?: unknown; body?: Uint8Array; contentType?: string };
export type FakeRoute = (req: RecordedRequest) => FakeReply | Promise<FakeReply>;

export interface FakeHarness {
  readonly port: number;
  readonly requests: RecordedRequest[];
  bots: WireBotLite[];
  route(method: string, path: RegExp, handler: FakeRoute): void;
  emit(frame: object): void;
  restart(): void;
  /** End every open event stream without a restart, so a reconnect can resume. */
  dropStreams(): void;
  waitFor(method: string, path: RegExp, timeoutMs?: number): Promise<RecordedRequest>;
  /** Resolves once at least one GET /api/events stream is open. */
  waitForSubscriber(timeoutMs?: number): Promise<void>;
  subscribers(): number;
  close(): Promise<void>;
}

export async function startFakeHarness(): Promise<FakeHarness> {
  const requests: RecordedRequest[] = [];
  const consumed = new Set<RecordedRequest>();
  const routes: Array<{ method: string; path: RegExp; handler: FakeRoute }> = [];
  const streams = new Set<ServerResponse>();
  const waiters: Array<() => void> = [];
  let streamId = randomBytes(4).toString("hex");
  let seq = 0;
  let replay: Array<{ seq: number; text: string }> = [];
  let sent = 0;

  const wake = () => { for (const waiter of waiters.splice(0)) waiter(); };

  const harness: FakeHarness = {
    port: 0,
    requests,
    bots: [],
    route(method, path, handler) {
      routes.unshift({ method, path, handler });
    },
    emit(frame) {
      seq += 1;
      const text = `id: ${streamId}:${seq}\ndata: ${JSON.stringify({ ...frame, seq })}\n\n`;
      replay.push({ seq, text });
      for (const stream of streams) stream.write(text);
    },
    restart() {
      streamId = randomBytes(4).toString("hex");
      seq = 0;
      replay = [];
      for (const stream of streams) stream.end();
      streams.clear();
    },
    dropStreams() {
      for (const stream of streams) stream.end();
      streams.clear();
    },
    waitFor(method, path, timeoutMs = 5_000) {
      return new Promise((resolve, reject) => {
        const find = () => requests.find((r) => !consumed.has(r) && r.method === method && path.test(r.path));
        const timer = setTimeout(() => reject(new Error(`no ${method} ${path} within ${timeoutMs} ms`)), timeoutMs);
        const check = () => {
          const found = find();
          if (!found) {
            waiters.push(check);
            return;
          }
          clearTimeout(timer);
          consumed.add(found);
          resolve(found);
        };
        check();
      });
    },
    waitForSubscriber(timeoutMs = 5_000) {
      return new Promise((resolve, reject) => {
        const timer = setTimeout(() => reject(new Error("no event subscriber")), timeoutMs);
        const check = () => {
          if (streams.size > 0) {
            clearTimeout(timer);
            resolve();
          } else {
            waiters.push(check);
          }
        };
        check();
      });
    },
    subscribers: () => streams.size,
    close: () => new Promise((resolve) => {
      for (const stream of streams) stream.end();
      server.closeAllConnections();
      server.close(() => resolve());
    }),
  };

  // Default routes; tests add their own with route(), which win.
  harness.route("GET", /^\/api\/bots(\?|$)/, () => ({ status: 200, json: { bots: harness.bots, botQueuedMessages: {} } }));
  harness.route("POST", /^\/api\/bots\/[\w-]+\/messages$/, (req) => {
    const body = req.json as { text: string; threadId: string; sendId: string };
    sent += 1;
    return {
      status: 202,
      json: { ok: true, threadId: body.threadId, message: { id: `m_user_${sent}`, role: "user", kind: "text", text: body.text, sendId: body.sendId, at: Date.now() } },
    };
  });
  harness.route("POST", /^\/api\/bots\/[\w-]+\/interrupt$/, () => ({ status: 200, json: { ok: true } }));
  harness.route("DELETE", /^\/api\/bots\/[\w-]+\/queue\/[\w-]+$/, () => ({ status: 200, json: { ok: true } }));
  harness.route("POST", /^\/api\/threads\/[\w-]+\/respond$/, () => ({ status: 200, json: { ok: true, outcome: "answered" } }));
  harness.route("GET", /^\/api\/threads\/[\w-]+\/messages(\?|$)/, () => ({ status: 200, json: { messages: [], hasMore: false, activeLeafId: null } }));

  const server = createServer((req, res) => {
    const chunks: Buffer[] = [];
    req.on("data", (chunk: Buffer) => chunks.push(chunk));
    req.on("end", () => {
      const body = Buffer.concat(chunks);
      const path = req.url ?? "/";
      const method = req.method ?? "GET";
      let json: unknown;
      try {
        json = body.length ? JSON.parse(body.toString("utf8")) : undefined;
      } catch {
        json = undefined;
      }
      const recorded: RecordedRequest = { method, path, headers: req.headers, body, json };
      if (method === "GET" && path.split("?")[0] === "/api/events") {
        res.writeHead(200, { "content-type": "text/event-stream", "cache-control": "no-store" });
        const last = String(req.headers["last-event-id"] ?? "");
        const [lastStream, lastSeq] = last.split(":");
        const resumed = lastStream === streamId;
        res.write(`id: ${streamId}:${seq}\ndata: ${JSON.stringify({ kind: "hello", cursor: `${streamId}:${seq}`, resumed })}\n\n`);
        // Contract §3.19: the default stream is hello + ping. The ping has no
        // id, as a keep-alive does not move the resume cursor.
        res.write('data: {"kind":"ping"}\n\n');
        if (resumed) for (const entry of replay) if (entry.seq > Number(lastSeq)) res.write(entry.text);
        streams.add(res);
        res.on("close", () => streams.delete(res));
        requests.push(recorded);
        wake();
        return;
      }
      requests.push(recorded);
      wake();
      const route = routes.find((r) => r.method === method && r.path.test(path));
      void Promise.resolve(route ? route.handler(recorded) : { status: 404, json: { error: `no route: ${method} ${path}` } }).then((reply) => {
        if (reply.json !== undefined) {
          const text = JSON.stringify(reply.json);
          res.writeHead(reply.status, { "content-type": "application/json", "content-length": Buffer.byteLength(text) });
          res.end(text);
        } else {
          const bytes = Buffer.from(reply.body ?? new Uint8Array());
          res.writeHead(reply.status, { "content-type": reply.contentType ?? "application/octet-stream", "content-length": bytes.length });
          res.end(bytes);
        }
      });
    });
  });
  (harness as { port: number }).port = await new Promise<number>((resolve) =>
    server.listen(0, "127.0.0.1", () => resolve((server.address() as { port: number }).port)),
  );
  return harness;
}
````

- [ ] **Step 2: Write the failing test**

`/api/internal/gadgets` (404, never reachable from a phone), `/api/devices` (403) and `POST /api/config` (403) prove the in-process client refuses what a phone could not reach, without sending anything.

````ts
// The hub's harness client (spec §6.2 harness client rules, A29): the phone
// allowlist and the relay token are checked before anything leaves, every
// per-gadget call carries the gadget's identity, and the one shared event
// stream carries only the companion marker and resumes with Last-Event-ID.
import { afterEach, beforeEach, describe, expect, it } from "vitest";

import { createHarnessClient, HarnessRefused } from "../../src/gadget/harness-client.ts";
import type { ServerFrameLite } from "../../src/gadget/types.ts";
import { startFakeHarness, type FakeHarness } from "./helpers/fake-harness.ts";

let harness: FakeHarness;
beforeEach(async () => { harness = await startFakeHarness(); });
afterEach(async () => { await harness.close(); });

describe("json()", () => {
  it("sends the gadget's identity and the relay token, and parses the reply", async () => {
    const client = createHarnessClient({ harnessPort: harness.port, mutationToken: () => "t".repeat(43) });
    const result = await client.json<{ ok: boolean; threadId: string }>("POST", "/api/bots/b1/messages", "gad_b18b86ce1389e46d", { text: "hi", threadId: "th1", sendId: "gdt0123456789abcdef" });
    expect(result.status).toBe(202);
    expect(result.body).toMatchObject({ ok: true, threadId: "th1" });
    const seen = await harness.waitFor("POST", /\/messages$/);
    expect(seen.headers["x-openmausbot-companion"]).toBe("1");
    expect(seen.headers["x-openmausbot-companion-device"]).toBe("gad_b18b86ce1389e46d");
    expect(seen.headers["x-openmausbot-companion-auth"]).toBe("t".repeat(43));
    expect(seen.json).toEqual({ text: "hi", threadId: "th1", sendId: "gdt0123456789abcdef" });
  });

  it("refuses, without a request, any route a phone could not reach", async () => {
    const client = createHarnessClient({ harnessPort: harness.port });
    await expect(client.json("GET", "/api/internal/gadgets", "gad_b18b86ce1389e46d")).rejects.toMatchObject({ status: 404 });
    await expect(client.json("GET", "/api/devices", null)).rejects.toMatchObject({ status: 403 });
    await expect(client.json("POST", "/api/config", "gad_b18b86ce1389e46d", {})).rejects.toMatchObject({ status: 403 });
    await expect(client.json("POST", "/api/bots/b1/messages", null, {})).rejects.toBeInstanceOf(HarnessRefused);
    expect(harness.requests).toHaveLength(0);
  });

  it("fails closed while the desktop has not delivered the relay token", async () => {
    let token: string | null = null;
    const client = createHarnessClient({ harnessPort: harness.port, mutationToken: () => token });
    await expect(client.json("POST", "/api/bots/b1/interrupt", "gad_b18b86ce1389e46d", { threadId: "th1" })).rejects.toMatchObject({ status: 503 });
    // Marker-only GETs never needed the token.
    expect((await client.json("GET", "/api/bots?messages=0", null)).status).toBe(200);
    token = "t".repeat(43);
    expect((await client.json("POST", "/api/bots/b1/interrupt", "gad_b18b86ce1389e46d", { threadId: "th1" })).status).toBe(200);
  });

  it("returns raw bodies for raw()", async () => {
    harness.route("POST", /^\/api\/tts\/speak$/, () => ({ status: 200, body: new Uint8Array([1, 2, 3]), contentType: "audio/pcm;rate=16000;channels=1;bits=16;endian=little" }));
    const client = createHarnessClient({ harnessPort: harness.port });
    const result = await client.raw("POST", "/api/tts/speak", "gad_b18b86ce1389e46d", Buffer.from("{}"), "application/json");
    expect([...result.body]).toEqual([1, 2, 3]);
    expect(result.headers["content-type"]).toContain("audio/pcm");
  });
});

describe("events()", () => {
  /** Collects frames and lets a test wait for a condition on them. */
  const collector = () => {
    const frames: ServerFrameLite[] = [];
    const hellos: boolean[] = [];
    let wake: () => void = () => {};
    return {
      frames,
      hellos,
      handlers: {
        onFrame: (frame: ServerFrameLite) => { frames.push(frame); wake(); },
        onHello: (resumed: boolean) => { hellos.push(resumed); wake(); },
      },
      until: (predicate: () => boolean) => new Promise<void>((resolve) => {
        if (predicate()) return resolve();
        wake = () => { if (predicate()) resolve(); };
      }),
    };
  };

  it("subscribes with the companion marker only and passes unknown kinds through as other", async () => {
    const client = createHarnessClient({ harnessPort: harness.port, mutationToken: () => "t".repeat(43) });
    const c = collector();
    const stream = client.events(c.handlers);
    await harness.waitForSubscriber();
    const subscribe = await harness.waitFor("GET", /^\/api\/events\?screens=off$/);
    expect(subscribe.headers["x-openmausbot-companion"]).toBe("1");
    expect(subscribe.headers["x-openmausbot-companion-device"]).toBeUndefined();
    expect(subscribe.headers["x-openmausbot-companion-auth"]).toBeUndefined();
    harness.emit({ kind: "bot", bot: { id: "b1", name: "Jev", threadId: "th1" } });
    harness.emit({ kind: "webhook", id: "w1" });
    await c.until(() => c.frames.length >= 4);
    expect(c.frames.map((f) => f.kind)).toEqual(["hello", "ping", "bot", "other"]);
    expect(c.frames[3]).toEqual({ kind: "other", raw: { kind: "webhook" } });
    stream.close();
  });

  it("resumes a dropped stream with Last-Event-ID and gets what it missed", async () => {
    const client = createHarnessClient({ harnessPort: harness.port });
    const c = collector();
    const stream = client.events(c.handlers);
    await harness.waitForSubscriber();
    harness.emit({ kind: "ping" });
    await c.until(() => c.frames.length === 3); // hello, the stream's own ping, this ping
    harness.dropStreams();
    harness.emit({ kind: "bot.deleted", botId: "b9" });
    await c.until(() => c.hellos.length === 2 && c.frames.some((f) => f.kind === "bot.deleted"));
    expect(c.hellos).toEqual([false, true]);
    const resubscribe = harness.requests.filter((r) => r.path.startsWith("/api/events"))[1]!;
    expect(resubscribe.headers["last-event-id"]).toMatch(/^[0-9a-f]{8}:1$/);
    stream.close();
  });

  it("reports resumed:false after a harness restart and keeps reconnecting", async () => {
    const client = createHarnessClient({ harnessPort: harness.port });
    const c = collector();
    const stream = client.events(c.handlers);
    await c.until(() => c.hellos.length === 1);
    harness.emit({ kind: "ping" });
    harness.restart();
    await c.until(() => c.hellos.length === 2);
    expect(c.hellos).toEqual([false, false]);
    stream.close();
  });
});
````

- [ ] **Step 3: Run it to see it fail**

Run: `cd /Users/omkar/Desktop/openmaus/OpenGrokBot-gadget && PATH="$HOME/.nvm/versions/node/v24.14.1/bin:$PATH" pnpm exec vitest run companion/test/gadget/harness-client.test.ts`
Expected: FAIL — `Cannot find module '../../src/gadget/harness-client.ts'`.

- [ ] **Step 4: Write `companion/src/gadget/harness-client.ts`**

````ts
// The gadget hub's only way to reach the harness (spec §6.2 harness client
// rules, A29). The proxy enforces the phone allowlist for requests that
// arrive over the network; the hub runs in-process and never passes through
// the proxy, so it checks the same allowlist itself before every call, and
// it speaks with the same identity headers a paired phone's relayed request
// carries. A gadget gets no authority a phone does not already have.
import { Buffer } from "node:buffer";
import { request as httpRequest, type ClientRequest, type IncomingHttpHeaders, type IncomingMessage } from "node:http";

import { companionIdentityHeaders } from "../proxy.ts";
import { denyReason } from "../routes.ts";
import type { ServerFrameLite } from "./types.ts";

export interface HarnessClientOptions {
  harnessPort: number;
  /** The same getter the proxy gets: present under Electron; null while the token is pending. */
  mutationToken?: () => string | null;
  timeoutMs?: number;
}

/** Thrown without contacting the harness: token pending under Electron
 *  (status 503), or denyReason() refused the route (its status). */
export class HarnessRefused extends Error {
  readonly status: number;
  constructor(status: number, message: string) {
    super(message);
    this.name = "HarnessRefused";
    this.status = status;
  }
}

export interface HarnessJson<T> { status: number; body: T; headers: IncomingHttpHeaders }
export interface HarnessRaw { status: number; body: Buffer; headers: IncomingHttpHeaders }
export type HarnessMethod = "GET" | "POST" | "PATCH" | "DELETE";

export interface HarnessClient {
  json<T>(method: HarnessMethod, path: string, deviceId: string | null, body?: unknown): Promise<HarnessJson<T>>;
  raw(method: "POST", path: string, deviceId: string, body: Uint8Array, contentType: string, timeoutMs?: number): Promise<HarnessRaw>;
  events(handlers: {
    onFrame(frame: ServerFrameLite): void;
    onHello?(resumed: boolean): void;
    onDown?(error: Error): void;
  }): { close(): void };
}

const DEFAULT_TIMEOUT_MS = 10_000;
const MAX_RESPONSE_BYTES = 16 * 1024 * 1024;
/** The harness caps one SSE event at 4 MiB (companion/src/wire.ts). */
const MAX_EVENT_BYTES = 4 * 1024 * 1024;
const RETRY_DELAYS_MS = [500, 1_000, 2_000, 5_000];
const FRAME_KINDS = new Set([
  "hello", "ping", "message", "message.patch", "bot", "bot.queued", "bot.deleted", "notify", "routine.run", "runtime",
]);

export function createHarnessClient(options: HarnessClientOptions): HarnessClient {
  const defaultTimeout = options.timeoutMs ?? DEFAULT_TIMEOUT_MS;

  /** The allowlist and token rules every request passes before it leaves. */
  const identity = (method: HarnessMethod, path: string, deviceId: string | null): Record<string, string> => {
    const plainPath = path.split("?")[0] ?? path;
    const denial = denyReason({ path: plainPath, method, authenticated: true });
    if (denial) throw new HarnessRefused(denial.status, denial.error);
    if (deviceId === null) {
      if (method !== "GET") throw new HarnessRefused(400, `${method} ${plainPath} needs a gadget id`);
      return companionIdentityHeaders();
    }
    if (!/^[\w-]{1,128}$/.test(deviceId)) throw new HarnessRefused(400, "invalid gadget id");
    let token: string | undefined;
    if (options.mutationToken) {
      const current = options.mutationToken();
      // Mirrors proxy.ts: under Electron, nothing is relayed until the
      // desktop has handed over the relay token.
      if (!current) throw new HarnessRefused(503, "The desktop connection is starting. Please try again shortly.");
      token = current;
    }
    return companionIdentityHeaders(deviceId, token);
  };

  const send = (
    method: HarnessMethod,
    path: string,
    headers: Record<string, string>,
    body: Uint8Array | undefined,
    timeoutMs: number,
  ): Promise<HarnessRaw> =>
    new Promise((resolve, reject) => {
      const request = httpRequest(
        {
          hostname: "127.0.0.1",
          port: options.harnessPort,
          path,
          method,
          headers: { ...headers, "content-length": String(body?.length ?? 0) },
          timeout: timeoutMs,
        },
        (response: IncomingMessage) => {
          const chunks: Buffer[] = [];
          let size = 0;
          response.on("data", (chunk: Buffer) => {
            size += chunk.length;
            if (size > MAX_RESPONSE_BYTES) {
              response.destroy(new Error("harness response too large"));
              return;
            }
            chunks.push(chunk);
          });
          response.on("error", reject);
          response.on("end", () =>
            resolve({ status: response.statusCode ?? 500, body: Buffer.concat(chunks, size), headers: response.headers }),
          );
        },
      );
      request.on("timeout", () => request.destroy(new Error(`the harness did not answer ${method} ${path.split("?")[0]}`)));
      request.on("error", reject);
      request.end(body);
    });

  return {
    async json<T>(method: HarnessMethod, path: string, deviceId: string | null, body?: unknown): Promise<HarnessJson<T>> {
      const headers: Record<string, string> = { accept: "application/json", ...identity(method, path, deviceId) };
      let payload: Buffer | undefined;
      if (body !== undefined) {
        payload = Buffer.from(JSON.stringify(body), "utf8");
        headers["content-type"] = "application/json";
      }
      const response = await send(method, path, headers, payload, defaultTimeout);
      const text = response.body.toString("utf8");
      let parsed: unknown = {};
      if (text) {
        try {
          parsed = JSON.parse(text);
        } catch {
          parsed = { error: text.slice(0, 200) };
        }
      }
      return { status: response.status, body: parsed as T, headers: response.headers };
    },

    async raw(method: "POST", path: string, deviceId: string, body: Uint8Array, contentType: string, timeoutMs?: number): Promise<HarnessRaw> {
      const headers = { accept: "*/*", "content-type": contentType, ...identity(method, path, deviceId) };
      return send(method, path, headers, body, timeoutMs ?? defaultTimeout);
    },

    events(handlers) {
      let closed = false;
      let current: ClientRequest | null = null;
      let retry: ReturnType<typeof setTimeout> | null = null;
      let attempt = 0;
      let lastEventId: string | null = null;

      const dispatch = (id: string | null, data: string[]) => {
        if (id !== null) lastEventId = id;
        if (!data.length) return;
        let raw: unknown;
        try {
          raw = JSON.parse(data.join("\n"));
        } catch {
          return;
        }
        if (!raw || typeof raw !== "object" || typeof (raw as { kind?: unknown }).kind !== "string") return;
        const kind = (raw as { kind: string }).kind;
        const frame = (FRAME_KINDS.has(kind) ? raw : { kind: "other", raw: { kind } }) as ServerFrameLite;
        if (frame.kind === "hello") handlers.onHello?.(frame.resumed === true);
        handlers.onFrame(frame);
      };

      const scheduleRetry = (error: Error) => {
        if (closed || retry) return;
        current = null;
        handlers.onDown?.(error);
        const delay = RETRY_DELAYS_MS[Math.min(attempt, RETRY_DELAYS_MS.length - 1)]!;
        attempt += 1;
        retry = setTimeout(() => {
          retry = null;
          connect();
        }, delay);
        retry.unref?.();
      };

      const connect = () => {
        if (closed) return;
        let headers: Record<string, string>;
        try {
          headers = { accept: "text/event-stream", ...identity("GET", "/api/events", null) };
        } catch (error) {
          scheduleRetry(error as Error);
          return;
        }
        if (lastEventId) headers["last-event-id"] = lastEventId;
        let ended = false;
        const end = (error: Error) => {
          if (ended) return;
          ended = true;
          scheduleRetry(error);
        };
        const request = httpRequest({
          hostname: "127.0.0.1",
          port: options.harnessPort,
          path: "/api/events?screens=off",
          method: "GET",
          headers,
        });
        current = request;
        request.on("response", (response: IncomingMessage) => {
          if (response.statusCode !== 200) {
            response.resume();
            end(new Error(`the harness answered ${response.statusCode} to /api/events`));
            return;
          }
          attempt = 0;
          response.setEncoding("utf8");
          let buffered = "";
          let data: string[] = [];
          let id: string | null = null;
          response.on("data", (chunk: string) => {
            buffered += chunk;
            if (buffered.length > MAX_EVENT_BYTES) {
              response.destroy(new Error("harness event too large"));
              return;
            }
            let newline = buffered.indexOf("\n");
            while (newline >= 0) {
              let line = buffered.slice(0, newline);
              buffered = buffered.slice(newline + 1);
              if (line.endsWith("\r")) line = line.slice(0, -1);
              if (line === "") {
                dispatch(id, data);
                data = [];
                id = null;
              } else if (!line.startsWith(":")) {
                const colon = line.indexOf(":");
                const field = colon < 0 ? line : line.slice(0, colon);
                let value = colon < 0 ? "" : line.slice(colon + 1);
                if (value.startsWith(" ")) value = value.slice(1);
                if (field === "data") data.push(value);
                else if (field === "id") id = value;
              }
              newline = buffered.indexOf("\n");
            }
          });
          response.on("error", (error: Error) => end(error));
          response.on("end", () => end(new Error("the harness closed the event stream")));
          response.on("close", () => end(new Error("the harness closed the event stream")));
        });
        request.on("error", (error: Error) => end(error));
        request.end();
      };

      connect();
      return {
        close() {
          closed = true;
          if (retry) clearTimeout(retry);
          retry = null;
          current?.destroy();
          current = null;
        },
      };
    },
  };
}
````

- [ ] **Step 5: Run the test and the checks**

```bash
cd /Users/omkar/Desktop/openmaus/OpenGrokBot-gadget || exit 1
export PATH="$HOME/.nvm/versions/node/v24.14.1/bin:$PATH"
pnpm exec vitest run companion/test/gadget/harness-client.test.ts
pnpm exec tsc -p tsconfig.server.json  # 40-150 s: give this Bash call a timeout of at least 10 minutes
pnpm exec oxlint --deny-warnings companion
```

Expected: `Tests  7 passed (7)`; no `tsc` output; oxlint exits 0.

- [ ] **Step 6: Commit**

```bash
cd /Users/omkar/Desktop/openmaus/OpenGrokBot-gadget || exit 1
export PATH="$HOME/.nvm/versions/node/v24.14.1/bin:$PATH"
git add companion/src/gadget/harness-client.ts companion/test/gadget/helpers/fake-harness.ts companion/test/gadget/harness-client.test.ts && git commit -m "feat(companion): allowlisted harness client and shared event stream for gadgets

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 8: The handshake host rules

**Files:**
- Create: `companion/src/gadget/enroll.ts`
- Test: `companion/test/gadget/enroll.test.ts`

**Interfaces:**
- Consumes: Task 1 helpers; `DeviceRegistry`, `GadgetDeviceRecord`, `cleanGadgetName` (Task 5).
- Produces (contract §3.9): `NormalizedAction`, `NormalizedHello`, `HelloCheck`, `checkHello(msg)`, `createChallenge(hostId, hostName)`, `ProveResult`, `completeProve({hello, challenge, prove, registry, resolveBot})`. A record that ends up with `botId === null` (plain Wi-Fi pairing, or an earlier failed lookup) is given `resolveBot()`'s answer before `ready` (contract D13). `bad_code` messages say "wrong", "expired" or "used up"; `enroll_required` says "MausBot → Settings → Remote access → Pair a gadget".

- [ ] **Step 1: Write the failing test**

It signs nothing: it uses the contract §1.7 pinned prove signature (RFC 6979 A.2.5 key, nonce bytes 0x00–0x1f, `host_id` 000102…0f), which is high-S.

````ts
// Handshake host rules (spec §4.3) over the pinned signature from the
// interface contract §1.7: the RFC 6979 A.2.5 key signing the prove text
// for nonce 0x00..0x1f and host_id 000102…0f.
import { Buffer } from "node:buffer";
import { rmSync } from "node:fs";
import { beforeEach, describe, expect, it } from "vitest";

import { DeviceRegistry, MAX_DEVICES } from "../../src/devices.ts";
import { checkHello, completeProve, createChallenge, type NormalizedHello } from "../../src/gadget/enroll.ts";
import type { ChallengeMsg, HelloMsg } from "../../src/gadget/protocol.ts";
import { DATA_DIR } from "../../src/state.ts";
import { PINNED_HOST_ID, PINNED_NONCE, PROVE_SIG_B64, RFC_ID, RFC_PUBKEY } from "./helpers/fixed-values.ts";

const hello = (extra: Partial<HelloMsg> = {}): HelloMsg => ({
  op: "hello", proto: 1, id: RFC_ID, pubkey: RFC_PUBKEY, name: "Desk Maus", board: "amoled-175c", fw: "1.0.0",
  caps: { screen: { w: 466, h: 466, round: true, text: "latin1" }, speaker: { rate: 16000 } },
  actions: [{ name: "chime", description: "Play a short chime.", params: { type: "object", properties: {} }, risk: "safe" }],
  sensors: { battery_pct: 82, charging: false },
  ...extra,
});
const pinnedChallenge: ChallengeMsg = { op: "challenge", nonce: PINNED_NONCE, host_id: PINNED_HOST_ID, host_name: "Test" };

const normalized = (extra: Partial<HelloMsg> = {}): NormalizedHello => {
  const check = checkHello(hello(extra));
  if (!check.ok) throw new Error(check.message);
  return check.hello;
};

beforeEach(() => {
  rmSync(DATA_DIR, { recursive: true, force: true });
});

describe("checkHello", () => {
  it("accepts the RFC key and normalizes name, actions and sensors", () => {
    const h = normalized({ name: "  A very long gadget name that goes past thirty-two  " });
    expect(h.name).toBe("A very long gadget name that goe");
    expect(h.pubkeyBytes.length).toBe(65);
    expect(h.actions).toEqual([expect.objectContaining({ name: "chime", risk: "safe", entryHash: expect.stringMatching(/^[0-9a-f]{16}$/) })]);
    expect(normalized({ name: "" }).name).toBe("Maus b18b");
  });

  it("drops actions that break a limit and defaults a missing risk to confirm", () => {
    const actions = [
      { name: "relay.on", description: "x", params: {} },
      { name: "Bad Name", description: "x", params: {} },
      { name: "long", description: "d".repeat(201), params: {} },
      { name: "big", description: "x", params: { blob: "p".repeat(1100) } },
      { name: "odd", description: "x", params: {}, risk: "maybe" },
      { name: "relay.on", description: "duplicate", params: {} },
      ...Array.from({ length: 20 }, (_, i) => ({ name: `a${i}`, description: "x", params: {} })),
    ] as HelloMsg["actions"];
    const h = normalized({ actions });
    expect(h.actions[0]).toMatchObject({ name: "relay.on", risk: "confirm", description: "x" });
    expect(h.actions.map((a) => a.name)).not.toContain("Bad Name");
    expect(h.actions).toHaveLength(16);
  });

  it.each([
    ["an unsupported proto", { proto: 2 }, "proto_unsupported"],
    ["a pubkey that does not hash to the id", { id: "gad_0000000000000000" }, "bad_sig"],
    ["a non-canonical pubkey", { pubkey: RFC_PUBKEY.slice(0, -2) + "l=" }, "bad_sig"],
    ["a compressed pubkey", { pubkey: Buffer.from(RFC_PUBKEY, "base64").subarray(0, 33).toString("base64") }, "bad_sig"],
    ["a bad board id", { board: "Board 1" }, "bad_sig"],
  ] as const)("refuses %s", (_label, extra, code) => {
    expect(checkHello(hello(extra as Partial<HelloMsg>))).toMatchObject({ ok: false, code });
  });
});

describe("createChallenge", () => {
  it("uses a fresh 32-byte nonce every time", () => {
    const a = createChallenge("0".repeat(32), "Ada's computer");
    const b = createChallenge("0".repeat(32), "Ada's computer");
    expect(Buffer.from(a.nonce, "base64").length).toBe(32);
    expect(a.nonce).not.toBe(b.nonce);
    expect(a).toMatchObject({ op: "challenge", host_id: "0".repeat(32), host_name: "Ada's computer" });
  });
});

describe("completeProve", () => {
  const prove = (enroll?: string) => ({ op: "prove" as const, sig: PROVE_SIG_B64, ...(enroll ? { enroll } : {}) });
  const noBot = async () => null;

  it("enrolls an unknown id with the window's code and the window's bot", async () => {
    const registry = new DeviceRegistry();
    const { code } = registry.openPairing("b_picked");
    const result = await completeProve({ hello: normalized(), challenge: pinnedChallenge, prove: prove(code), registry, resolveBot: noBot });
    expect(result).toMatchObject({ ok: true, enrolled: true, device: { id: RFC_ID, botId: "b_picked", name: "Desk Maus" } });
  });

  it("asks for the default bot when the code came from plain Wi-Fi pairing", async () => {
    const registry = new DeviceRegistry();
    const { code } = registry.openPairing();
    const result = await completeProve({ hello: normalized(), challenge: pinnedChallenge, prove: prove(code), registry, resolveBot: async () => "b_default" });
    expect(result).toMatchObject({ ok: true, device: { botId: "b_default" } });
    expect(registry.gadget(RFC_ID)?.botId).toBe("b_default");
  });

  it("rejects a proof made for another host id", async () => {
    const registry = new DeviceRegistry();
    const { code } = registry.openPairing();
    const other = { ...pinnedChallenge, host_id: "000102030405060708090a0b0c0d0e0e" };
    expect(await completeProve({ hello: normalized(), challenge: other, prove: prove(code), registry, resolveBot: noBot }))
      .toMatchObject({ ok: false, code: "bad_sig" });
    expect(registry.pairing()?.code).toBe(code); // nothing consumed
  });

  it("rejects a non-canonical or truncated signature", async () => {
    const registry = new DeviceRegistry();
    for (const sig of [PROVE_SIG_B64.replace(/\+/g, "-"), Buffer.from(PROVE_SIG_B64, "base64").subarray(0, 70).toString("base64")]) {
      expect(await completeProve({ hello: normalized(), challenge: pinnedChallenge, prove: { op: "prove", sig }, registry, resolveBot: noBot }))
        .toMatchObject({ ok: false, code: "bad_sig" });
    }
  });

  it("answers enroll_required for an unknown id without a code", async () => {
    const registry = new DeviceRegistry();
    expect(await completeProve({ hello: normalized(), challenge: pinnedChallenge, prove: prove(), registry, resolveBot: noBot }))
      .toMatchObject({ ok: false, code: "enroll_required", message: expect.stringContaining("Pair a gadget") });
  });

  it("maps wrong, expired and used-up codes to bad_code, and a full fleet to device_limit", async () => {
    const registry = new DeviceRegistry();
    expect(await completeProve({ hello: normalized(), challenge: pinnedChallenge, prove: prove("123456"), registry, resolveBot: noBot }))
      .toMatchObject({ ok: false, code: "bad_code", message: expect.stringContaining("expired") });
    const { code } = registry.openPairing();
    const wrong = code === "000000" ? "111111" : "000000";
    expect(await completeProve({ hello: normalized(), challenge: pinnedChallenge, prove: prove(wrong), registry, resolveBot: noBot }))
      .toMatchObject({ ok: false, code: "bad_code", message: "wrong code" });
    for (let i = 0; i < MAX_DEVICES; i++) {
      const window = registry.openPairing();
      registry.redeem(window.code, `phone ${i}`);
    }
    const full = registry.openPairing();
    expect(await completeProve({ hello: normalized(), challenge: pinnedChallenge, prove: prove(full.code), registry, resolveBot: noBot }))
      .toMatchObject({ ok: false, code: "device_limit" });
  });

  it("ignores a code from a known gadget and applies last-writer-wins to its name", async () => {
    const registry = new DeviceRegistry();
    const { code } = registry.openPairing("b1");
    await completeProve({ hello: normalized(), challenge: pinnedChallenge, prove: prove(code), registry, resolveBot: noBot });
    const window = registry.openPairing();
    registry.updateGadget(RFC_ID, { name: "Kitchen", namePending: true });
    const again = await completeProve({ hello: normalized({ name: "Console name", fw: "1.0.1" }), challenge: pinnedChallenge, prove: prove(window.code), registry, resolveBot: noBot });
    expect(again).toMatchObject({ ok: true, enrolled: false, sendName: "Kitchen", device: { name: "Kitchen", firmware: "1.0.1" } });
    expect(registry.pairing()?.code).toBe(window.code); // a known gadget never consumes a window
  });
});
````

- [ ] **Step 2: Run it to see it fail**

Run: `cd /Users/omkar/Desktop/openmaus/OpenGrokBot-gadget && PATH="$HOME/.nvm/versions/node/v24.14.1/bin:$PATH" pnpm exec vitest run companion/test/gadget/enroll.test.ts`
Expected: FAIL — `Cannot find module '../../src/gadget/enroll.ts'`.

- [ ] **Step 3: Write `companion/src/gadget/enroll.ts`**

````ts
// The handshake host rules (spec §4.2–§4.3), with Node's own crypto.
//
//   gadget → hello      {proto, id, pubkey, name, board, fw, caps, actions, sensors}
//   host   → challenge  {nonce, host_id, host_name}
//   gadget → prove      {sig, enroll?}
//   host   → ready | error
//
// A gadget's id is derived from its public key, and every session signs a
// fresh nonce bound to this computer's host_id, so no device can claim
// another's id and a recorded session cannot be replayed.
import { Buffer } from "node:buffer";
import { randomBytes } from "node:crypto";

import type { DeviceRegistry, GadgetDeviceRecord } from "../devices.ts";
import { cleanGadgetName } from "../devices.ts";
import {
  ACTION_DESCRIPTION_MAX,
  ACTION_NAME_RE,
  ACTION_PARAMS_MAX_BYTES,
  ACTIONS_MAX,
  actionEntryHash,
  BOARD_ID_RE,
  decodePubkey,
  gadgetIdFromPubkey,
  isCanonicalBase64,
  PROTO_VERSION,
  proveText,
  verifyP256,
  type ChallengeMsg,
  type GadgetCaps,
  type GadgetErrorCode,
  type GadgetSensors,
  type HelloMsg,
  type ProveMsg,
  type Risk,
} from "./protocol.ts";

export interface NormalizedAction {
  name: string;
  description: string;
  params: Record<string, unknown>;
  risk: Risk;
  entryHash: string;
}
export interface NormalizedHello {
  proto: number;
  id: string;
  pubkey: string;
  pubkeyBytes: Buffer;
  name: string;
  board: string;
  fw: string;
  caps: GadgetCaps;
  actions: NormalizedAction[];
  sensors: GadgetSensors;
}
export type HelloCheck =
  | { ok: true; hello: NormalizedHello }
  | { ok: false; code: "proto_unsupported" | "bad_sig"; message: string };

const isRecord = (value: unknown): value is Record<string, unknown> =>
  typeof value === "object" && value !== null && !Array.isArray(value);

/** Device-declared actions are untrusted: any entry that breaks a limit is
 * dropped (never cut down), duplicates keep the first, at most 16 survive. */
function normalizeActions(raw: unknown): NormalizedAction[] {
  if (!Array.isArray(raw)) return [];
  const out: NormalizedAction[] = [];
  const seen = new Set<string>();
  for (const entry of raw) {
    if (out.length >= ACTIONS_MAX) break;
    if (!isRecord(entry)) continue;
    const { name, description, params, risk } = entry;
    if (typeof name !== "string" || !ACTION_NAME_RE.test(name) || seen.has(name)) continue;
    if (typeof description !== "string" || Array.from(description).length > ACTION_DESCRIPTION_MAX) continue;
    if (!isRecord(params) || Buffer.byteLength(JSON.stringify(params)) > ACTION_PARAMS_MAX_BYTES) continue;
    if (risk !== undefined && risk !== "safe" && risk !== "confirm") continue;
    const normalized: Risk = risk ?? "confirm";
    seen.add(name);
    out.push({ name, description, params, risk: normalized, entryHash: actionEntryHash({ name, description, params, risk: normalized }) });
  }
  return out;
}

/** proto must be 1; pubkey canonical base64 of 65 bytes starting 0x04 hashing to id; board BOARD_ID_RE. */
export function checkHello(msg: HelloMsg): HelloCheck {
  if (msg.proto !== PROTO_VERSION) {
    return { ok: false, code: "proto_unsupported", message: `this MausBot speaks openmausbot-gadget/${PROTO_VERSION}` };
  }
  const pubkeyBytes = decodePubkey(msg.pubkey);
  if (!pubkeyBytes) return { ok: false, code: "bad_sig", message: "the public key is not a 65-byte P-256 point in base64" };
  if (gadgetIdFromPubkey(pubkeyBytes) !== msg.id) return { ok: false, code: "bad_sig", message: "the id does not match the public key" };
  if (!BOARD_ID_RE.test(msg.board)) return { ok: false, code: "bad_sig", message: "the board id is not valid" };
  return {
    ok: true,
    hello: {
      proto: msg.proto,
      id: msg.id,
      pubkey: msg.pubkey,
      pubkeyBytes,
      name: cleanGadgetName(msg.name, msg.id),
      board: msg.board,
      fw: Array.from(msg.fw).slice(0, 32).join(""),
      caps: isRecord(msg.caps) ? (msg.caps as GadgetCaps) : {},
      actions: normalizeActions(msg.actions),
      sensors: isRecord(msg.sensors) ? (msg.sensors as GadgetSensors) : {},
    },
  };
}

/** A fresh 32-byte nonce per connection. hostName is used as given: the
 *  hub folds it to Latin-1 and cuts it to 64 bytes first (contract D20). */
export function createChallenge(hostId: string, hostName: string): ChallengeMsg {
  return { op: "challenge", nonce: randomBytes(32).toString("base64"), host_id: hostId, host_name: hostName };
}

export type ProveResult =
  | { ok: true; device: GadgetDeviceRecord; enrolled: boolean; sendName?: string }
  | { ok: false; code: GadgetErrorCode; message: string }
  | { ok: false; code: "internal"; message: string };

/** Verify the proof over this connection's challenge, then apply rule 2
 *  (known id), 3 (unknown id with a code) or 4 (unknown id without one).
 *  A record left without a bot is given the default bot when one exists. */
export async function completeProve(input: {
  hello: NormalizedHello;
  challenge: ChallengeMsg;
  prove: ProveMsg;
  registry: DeviceRegistry;
  resolveBot: () => Promise<string | null>;
}): Promise<ProveResult> {
  const { hello, challenge, prove, registry } = input;
  if (!isCanonicalBase64(prove.sig)) return { ok: false, code: "bad_sig", message: "the signature is not canonical base64" };
  const text = proveText(hello.id, challenge.nonce, challenge.host_id);
  if (!verifyP256(hello.pubkeyBytes, text, Buffer.from(prove.sig, "base64"))) {
    return { ok: false, code: "bad_sig", message: "the signature does not verify" };
  }

  let device: GadgetDeviceRecord;
  let enrolled = false;
  let sendName: string | undefined;
  const known = registry.gadget(hello.id);
  if (known) {
    // Rule 2. A matching id with another key would mean a hash collision,
    // but the stored key is what the record trusts.
    if (known.publicKey !== hello.pubkey) return { ok: false, code: "bad_sig", message: "this id is paired with a different key" };
    const noted = registry.noteGadgetHello(hello.id, { name: hello.name, board: hello.board, firmware: hello.fw });
    if (!noted) return { ok: false, code: "enroll_required", message: "pair this gadget again" };
    device = noted.record;
    sendName = noted.sendName;
  } else if (prove.enroll !== undefined) {
    // Rule 3: the same pairing window the phones use.
    const result = registry.enrollGadget(prove.enroll, {
      id: hello.id,
      publicKey: hello.pubkey,
      name: hello.name,
      board: hello.board,
      firmware: hello.fw,
      botId: registry.pairing()?.botId ?? null,
    });
    if ("error" in result) {
      if (result.error === "device_limit") return { ok: false, code: "device_limit", message: result.message };
      if (result.error === "save_failed") return { ok: false, code: "internal", message: result.message };
      return { ok: false, code: "bad_code", message: result.message };
    }
    device = result.device;
    enrolled = true;
  } else {
    // Rule 4.
    return { ok: false, code: "enroll_required", message: "pair me: MausBot → Settings → Remote access → Pair a gadget" };
  }

  // A gadget enrolled through plain Wi-Fi pairing, or whose bot lookup
  // failed before, gets the default bot now (contract D13).
  if (device.botId === null) {
    let botId: string | null = null;
    try {
      botId = await input.resolveBot();
    } catch {
      botId = null;
    }
    if (botId) {
      try {
        device = registry.updateGadget(device.id, { botId }) ?? device;
      } catch {
        /* unbound for now; the next connection tries again */
      }
    }
  }
  return { ok: true, device, enrolled, ...(sendName !== undefined ? { sendName } : {}) };
}
````

- [ ] **Step 4: Run the test and the checks**

```bash
cd /Users/omkar/Desktop/openmaus/OpenGrokBot-gadget || exit 1
export PATH="$HOME/.nvm/versions/node/v24.14.1/bin:$PATH"
pnpm exec vitest run companion/test/gadget/enroll.test.ts
pnpm exec tsc -p tsconfig.server.json  # 40-150 s: give this Bash call a timeout of at least 10 minutes
pnpm exec oxlint --deny-warnings companion
```

Expected: `Tests  15 passed (15)`; no `tsc` output; oxlint exits 0.

- [ ] **Step 5: Commit**

```bash
cd /Users/omkar/Desktop/openmaus/OpenGrokBot-gadget || exit 1
export PATH="$HOME/.nvm/versions/node/v24.14.1/bin:$PATH"
git add companion/src/gadget/enroll.ts companion/test/gadget/enroll.test.ts && git commit -m "feat(companion): gadget handshake host rules with node:crypto

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 9: The hub, sessions and turns

This is the core of the plan and its largest task. It adds four modules: the bot directory, the turn engine, the session (without asks yet) and the hub (without pushes yet). It also adds two test helpers and two test files. Tasks 10 and 11 then add asks and pushes as small patches.

**Files:**
- Create: `companion/src/gadget/directory.ts`, `companion/src/gadget/turns.ts`, `companion/src/gadget/session.ts`, `companion/src/gadget/hub.ts`
- Create: `companion/test/gadget/helpers/gadget-client.ts` (shared with P3b, P4a, P4b), `companion/test/gadget/helpers/hub-rig.ts`
- Test: `companion/test/gadget/hub.test.ts`, `companion/test/gadget/hub-turns.test.ts`

**Interfaces:**
- Consumes: Tasks 1–8 (`acceptUpgrade`, `checkHello`, `completeProve`, `createChallenge`, `createHarnessClient`, `HarnessRefused`, `screenText`, `foldLatin1`, `cutFromStart`, `clampChars`, `defaultGadgetBot`, `DeviceRegistry` methods), `createConnectedDeviceTracker` (`companion/src/connected-devices.ts:4`).
- Produces (contract §3.10, §3.11): `GadgetSessionHandle` (`deviceId`, `sessionId` = `s_` + 12 hex, `hello`, `connectedAt`, `closed`, `send`, `sendBinary`, `allocStream`, `releaseStream`, `on(op, listener)`, `onClose`), `SttOutcome`, `SttFn`, `SpeechOut`, `SpeechContext`, `SpeechOutFactory`, `VoiceProvider`, `textOnlyVoice`; `GadgetEventRecord`, `GadgetHubOptions` (plus additive test seam `timing?: {handshakeMs?, terminalGraceMs?, replyIntervalMs?, closeGraceMs?}`), `GadgetHub` (`isGadgetPath`, `handleUpgrade`, `session`, `online`, `onSessionReady`, `settingsChanged`, `revoke`, `recentEvents`, `botName`, `close`), `createGadgetHub(options)`. Private: `gadgetSendId(gadgetId, sessionId, turn)`, `UNBOUND_COPY`, `QUEUED_COPY` (exported from `turns.ts` for tests). Test helpers: `connectTestGadget(options): Promise<TestGadget>` (contract §3.19 plus additive `frames()`), `DEFAULT_CAPS`, `startRig(overrides?, bots?)`, `JEV`, `HOST_ID`.

Behaviour to keep in mind while reading the code:

- The harness broadcasts `runtime turn.completed` just before it patches `turnTerminal` onto the last reply (origin/main `server/index.ts:7218` vs `:7640-7643`). So `done` waits a short grace (`terminalGraceMs`, default 1 s) for the terminal patch, and falls back to the streamed text.
- `turn.started` carries no request id. A send that started a turn binds to the next `turn.started` on the pinned thread. A steered send binds to the thread's running turn. A queued send binds once its drained user message appears, matched by `sendId` or by `queueId`. When the person presses Steer on that queued line on the desktop (`POST /api/bots/:id/queue/:queueId/steer`, origin/main `server/index.ts:21893-21951`), the message carries `steered: true` and starts no turn of its own, so the send binds to the thread's running turn like a steered receipt, and Stop leaves that turn alone. Late binds replay that turn's buffered deltas, error and outcome.
- Every new turn stops speech first: through `stopCurrent()` when a turn is in flight, and with a direct `SpeechOut.stop()` otherwise, because a finished turn's reply (or a spoken post) can still be playing while the gadget records (spec §6.2 Speech item 6; v1 has no echo cancellation).
- The 250 ms cap covers the final `reply` too: when a partial went out less than 250 ms earlier, the final reply waits for the rest of the interval, and the `done` that follows it waits with it. `clearTimers()` cancels that wait when the turn is stopped or the socket closes.
- The shared SSE stream opens with the first ready gadget and closes with the last one. The first `hello` hydrates the bot directory. A later `hello` with `resumed: false` means the harness restarted, so in-flight turns end with "MausBot restarted. Try again." (and, once Task 10 lands, open asks are withdrawn and read again).
- `hub.revoke(id)` only reports `removed` for `gad_` ids, because index.ts calls it for every removed device.

- [ ] **Step 1: Write the test gadget and the rig**

Create `companion/test/gadget/helpers/gadget-client.ts`:

````ts
// A TypeScript gadget for tests (P3a; reused by P3b, P4a, P4b). It speaks
// openmausbot-gadget/1 over Node 24's global WebSocket — which sends no
// Origin header — and signs its proof with node:crypto, exactly as the
// firmware does with PSA: P-256, SHA-256, DER.
import { Buffer } from "node:buffer";
import { createECDH, createPrivateKey, randomBytes, sign } from "node:crypto";

import {
  decodeBinary,
  encodeBinary,
  gadgetIdFromPubkey,
  proveText,
  type BinaryKindValue,
  type GadgetActionDecl,
  type GadgetCaps,
  type GadgetToHost,
  type HostToGadget,
} from "../../../src/gadget/protocol.ts";

export interface TestGadget {
  readonly id: string;
  readonly pubkey: string;
  readonly ready: Extract<HostToGadget, { op: "ready" }> | null;
  readonly error: Extract<HostToGadget, { op: "error" }> | null;
  /** Every text frame received so far, in arrival order (never consumed). */
  frames(): HostToGadget[];
  send(msg: GadgetToHost): void;
  sendBinary(kind: BinaryKindValue, stream: number, payload: Uint8Array): void;
  next<K extends HostToGadget["op"]>(op: K, timeoutMs?: number): Promise<Extract<HostToGadget, { op: K }>>;
  nextBinary(kind?: BinaryKindValue, timeoutMs?: number): Promise<{ kind: BinaryKindValue; stream: number; payload: Uint8Array }>;
  closed(): Promise<{ code: number; reason: string }>;
  close(code?: number): void;
}

export interface TestGadgetOptions {
  port: number;
  privateKeyHex?: string;
  enroll?: string;
  name?: string;
  board?: string;
  fw?: string;
  caps?: GadgetCaps;
  actions?: GadgetActionDecl[];
  tamper?: "sig" | "host_id";
}

export const DEFAULT_CAPS: GadgetCaps = {
  screen: { w: 466, h: 466, round: true, text: "latin1" },
  image: { w: 300, h: 300 },
  mic: { rate: 16000 },
  speaker: { rate: 16000 },
  input: ["touch", "talk", "cancel"],
  battery: true,
  ota: { max: 6291456 },
};

export async function connectTestGadget(options: TestGadgetOptions): Promise<TestGadget> {
  const scalar = Buffer.from(options.privateKeyHex ?? randomBytes(32).toString("hex"), "hex");
  const ecdh = createECDH("prime256v1");
  ecdh.setPrivateKey(scalar);
  const pub = ecdh.getPublicKey();
  const key = createPrivateKey({
    key: { kty: "EC", crv: "P-256", d: scalar.toString("base64url"), x: pub.subarray(1, 33).toString("base64url"), y: pub.subarray(33).toString("base64url") },
    format: "jwk",
  });
  const id = gadgetIdFromPubkey(pub);
  const pubkey = pub.toString("base64");

  const all: HostToGadget[] = [];
  const unread: HostToGadget[] = [];
  const binaries: Array<{ kind: BinaryKindValue; stream: number; payload: Uint8Array }> = [];
  const wakers = new Set<() => void>();
  const wake = () => { for (const waker of Array.from(wakers)) waker(); };
  let closeInfo: { code: number; reason: string } | null = null;
  let resolveClosed: (info: { code: number; reason: string }) => void = () => {};
  const closedPromise = new Promise<{ code: number; reason: string }>((resolve) => { resolveClosed = resolve; });

  const ws = new WebSocket(`ws://127.0.0.1:${options.port}/gadget`, "openmausbot-gadget.1");
  ws.binaryType = "arraybuffer";
  ws.onmessage = (event) => {
    if (typeof event.data === "string") {
      const msg = JSON.parse(event.data) as HostToGadget;
      all.push(msg);
      unread.push(msg);
    } else {
      const decoded = decodeBinary(new Uint8Array(event.data as ArrayBuffer));
      if (decoded) binaries.push({ kind: decoded.kind, stream: decoded.stream, payload: new Uint8Array(decoded.payload) });
    }
    wake();
  };
  ws.onclose = (event) => {
    closeInfo = { code: event.code, reason: event.reason };
    resolveClosed(closeInfo);
    wake();
  };
  await new Promise<void>((resolve, reject) => {
    ws.onopen = () => resolve();
    ws.onerror = () => reject(new Error("the hub refused the upgrade"));
  });

  const waitFor = <T>(take: () => T | undefined, what: string, timeoutMs = 5_000): Promise<T> =>
    new Promise((resolve, reject) => {
      const timer = setTimeout(() => {
        wakers.delete(check);
        reject(new Error(`no ${what} within ${timeoutMs} ms; got ${JSON.stringify(all.map((m) => m.op))}`));
      }, timeoutMs);
      const check = () => {
        const found = take();
        if (found !== undefined) {
          clearTimeout(timer);
          wakers.delete(check);
          resolve(found);
        } else if (closeInfo && !unread.length) {
          clearTimeout(timer);
          wakers.delete(check);
          reject(new Error(`closed (${closeInfo.code}) before ${what}`));
        }
      };
      wakers.add(check);
      check();
    });

  const nextOp = <K extends HostToGadget["op"]>(op: K, timeoutMs?: number) =>
    waitFor(() => {
      const index = unread.findIndex((msg) => msg.op === op);
      return index < 0 ? undefined : (unread.splice(index, 1)[0] as Extract<HostToGadget, { op: K }>);
    }, op, timeoutMs);

  const sendJson = (msg: object) => ws.send(JSON.stringify(msg));

  sendJson({
    op: "hello", proto: 1, id, pubkey, name: options.name ?? "Desk Maus", board: options.board ?? "amoled-175c",
    fw: options.fw ?? "1.0.0", caps: options.caps ?? DEFAULT_CAPS, actions: options.actions ?? [],
    sensors: { battery_pct: 82, charging: false },
  });
  const first = await waitFor(() => {
    const index = unread.findIndex((msg) => msg.op === "challenge" || msg.op === "error");
    return index < 0 ? undefined : unread.splice(index, 1)[0];
  }, "challenge");

  let ready: Extract<HostToGadget, { op: "ready" }> | null = null;
  let error: Extract<HostToGadget, { op: "error" }> | null = first.op === "error" ? first : null;
  if (first.op === "challenge") {
    const hostId = options.tamper === "host_id" ? "f".repeat(32) : first.host_id;
    const sig = Buffer.from(sign("sha256", Buffer.from(proveText(id, first.nonce, hostId), "utf8"), { key, dsaEncoding: "der" }));
    if (options.tamper === "sig") sig[sig.length - 1] ^= 0x01;
    sendJson({ op: "prove", sig: sig.toString("base64"), ...(options.enroll ? { enroll: options.enroll } : {}) });
    const answer = await waitFor(() => {
      const index = unread.findIndex((msg) => msg.op === "ready" || msg.op === "error");
      return index < 0 ? undefined : unread.splice(index, 1)[0];
    }, "ready");
    if (answer.op === "ready") ready = answer;
    else if (answer.op === "error") error = answer;
  }

  return {
    id,
    pubkey,
    ready,
    error,
    frames: () => [...all],
    send: (msg) => sendJson(msg),
    sendBinary: (kind, stream, payload) => ws.send(encodeBinary(kind, stream, payload)),
    next: nextOp,
    nextBinary: (kind, timeoutMs) =>
      waitFor(() => {
        const index = binaries.findIndex((frame) => kind === undefined || frame.kind === kind);
        return index < 0 ? undefined : binaries.splice(index, 1)[0];
      }, `binary ${kind ?? "frame"}`, timeoutMs),
    closed: () => closedPromise,
    close: (code = 1000) => ws.close(code),
  };
}
````

Create `companion/test/gadget/helpers/hub-rig.ts`:

````ts
// A hub on a real HTTP server in front of a fake harness: the shape the
// companion wires in index.ts, small enough for one test file.
import { rmSync } from "node:fs";
import { createServer, type Server } from "node:http";

import { createConnectedDeviceTracker } from "../../../src/connected-devices.ts";
import { DeviceRegistry } from "../../../src/devices.ts";
import { createGadgetHub, type GadgetHub, type GadgetHubOptions } from "../../../src/gadget/hub.ts";
import type { WireBotLite } from "../../../src/gadget/types.ts";
import { DATA_DIR } from "../../../src/state.ts";
import { startFakeHarness, type FakeHarness } from "./fake-harness.ts";
import { connectTestGadget, type TestGadget, type TestGadgetOptions } from "./gadget-client.ts";

export const HOST_ID = "0123456789abcdef0123456789abcdef";
export const JEV: WireBotLite = { id: "b1", name: "Jev", threadId: "th1", tasks: [{ threadId: "th1", title: "Main" }] };

export interface Rig {
  harness: FakeHarness;
  devices: DeviceRegistry;
  hub: GadgetHub;
  tracker: ReturnType<typeof createConnectedDeviceTracker>;
  port: number;
  changes: Array<{ kind: "enrolled" | "removed"; deviceId: string }>;
  /** Open a pairing window (optionally with a bot) and enroll a new test gadget. */
  enroll(options?: Partial<TestGadgetOptions> & { botId?: string }): Promise<TestGadget>;
  connect(options?: Partial<TestGadgetOptions>): Promise<TestGadget>;
  close(): Promise<void>;
}

export async function startRig(overrides: Partial<GadgetHubOptions> = {}, bots: WireBotLite[] = [JEV]): Promise<Rig> {
  rmSync(DATA_DIR, { recursive: true, force: true });
  const harness = await startFakeHarness();
  harness.bots = bots;
  const devices = new DeviceRegistry();
  const tracker = createConnectedDeviceTracker();
  const changes: Rig["changes"] = [];
  const hub = createGadgetHub({
    devices,
    harnessPort: harness.port,
    hostId: HOST_ID,
    hostName: () => "Ada's computer",
    connected: tracker.open,
    onDevicesChanged: (change) => changes.push(change),
    log: () => {},
    timing: { handshakeMs: 2_000, terminalGraceMs: 50, closeGraceMs: 200 },
    ...overrides,
  });
  const server: Server = createServer((_req, res) => { res.writeHead(404).end(); });
  server.on("upgrade", (req, socket, head) => {
    if (hub.isGadgetPath(req.url)) hub.handleUpgrade(req, socket, head);
    else socket.destroy();
  });
  const port = await new Promise<number>((resolve) => server.listen(0, "127.0.0.1", () => resolve((server.address() as { port: number }).port)));
  const gadgets: TestGadget[] = [];
  const connect = async (options: Partial<TestGadgetOptions> = {}) => {
    const gadget = await connectTestGadget({ port, ...options });
    gadgets.push(gadget);
    return gadget;
  };
  return {
    harness,
    devices,
    hub,
    tracker,
    port,
    changes,
    connect,
    async enroll(options = {}) {
      const { botId, ...rest } = options;
      const window = devices.openPairing(botId ?? "b1");
      return connect({ enroll: window.code, ...rest });
    },
    async close() {
      for (const gadget of gadgets) gadget.close();
      await hub.close();
      server.closeAllConnections();
      await new Promise<void>((resolve) => server.close(() => resolve()));
      await harness.close();
    },
  };
}
````

- [ ] **Step 2: Write the failing handshake and lifecycle test**

It includes Review Focus item 3 (`robustness`).

Create `companion/test/gadget/hub.test.ts`:

````ts
// The hub's handshake and lifecycle (spec §4.3, §6.1): enrollment through the
// shared pairing window, one session per id replaced only after a verified
// proof, presence and revocation through the connected-device tracker, the
// handshake deadline, name last-writer-wins and a clean shutdown.
import { randomBytes } from "node:crypto";
import { createConnection } from "node:net";
import { afterEach, describe, expect, it } from "vitest";

import { RFC_PRIVATE_KEY_HEX } from "./helpers/fixed-values.ts";
import { HOST_ID, startRig, type Rig } from "./helpers/hub-rig.ts";

let rig: Rig;
afterEach(async () => { await rig?.close(); });

describe("handshake", () => {
  it("enrolls with the six digits, picks the window's bot and registers presence", async () => {
    rig = await startRig();
    const gadget = await rig.enroll({ privateKeyHex: RFC_PRIVATE_KEY_HEX, name: "Desk Maus" });
    expect(gadget.id).toBe("gad_b18b86ce1389e46d");
    expect(gadget.ready).toMatchObject({ op: "ready", bot: { id: "b1", name: "Jev" }, settings: { speak_pushes: false } });
    expect(gadget.ready!.session).toMatch(/^s_[0-9a-f]{12}$/);
    const challenge = gadget.frames().find((m) => m.op === "challenge");
    expect(challenge).toMatchObject({ host_id: HOST_ID, host_name: "Ada's computer" });
    expect(rig.devices.gadget(gadget.id)).toMatchObject({ botId: "b1", name: "Desk Maus", board: "amoled-175c" });
    expect(rig.devices.pairing()).toBeNull();
    expect(rig.tracker.ids()).toEqual([gadget.id]);
    expect(rig.hub.online()).toEqual([gadget.id]);
    expect(rig.changes).toEqual([{ kind: "enrolled", deviceId: gadget.id }]);
  });

  it("lets a known gadget back in without a code and ignores a stale one", async () => {
    rig = await startRig();
    const first = await rig.enroll({ privateKeyHex: RFC_PRIVATE_KEY_HEX });
    first.close();
    await first.closed();
    const again = await rig.connect({ privateKeyHex: RFC_PRIVATE_KEY_HEX, enroll: "000000" });
    expect(again.ready).not.toBeNull();
    expect(rig.changes).toHaveLength(1);
  });

  it.each<[string, { code: "right" | "wrong" | "none"; tamper?: "sig" | "host_id" }, string]>([
    ["an unknown gadget without a code", { code: "none" }, "enroll_required"],
    ["a wrong code", { code: "wrong" }, "bad_code"],
    ["a bad signature", { code: "right", tamper: "sig" }, "bad_sig"],
    ["a proof for another host", { code: "right", tamper: "host_id" }, "bad_sig"],
  ])("refuses %s with an error op, then closes", async (_label, options, code) => {
    rig = await startRig();
    const window = rig.devices.openPairing();
    const wrong = window.code === "000000" ? "111111" : "000000";
    const enroll = options.code === "right" ? window.code : options.code === "wrong" ? wrong : undefined;
    const gadget = await rig.connect({ ...(enroll ? { enroll } : {}), ...(options.tamper ? { tamper: options.tamper } : {}) });
    expect(gadget.ready).toBeNull();
    expect(gadget.error).toMatchObject({ op: "error", code });
    expect((await gadget.closed()).code).toBe(1000);
    expect(rig.tracker.ids()).toEqual([]);
  });

  it("closes a socket that never finishes the handshake", async () => {
    rig = await startRig({ timing: { handshakeMs: 100, terminalGraceMs: 50, closeGraceMs: 200 } });
    const socket = createConnection(rig.port, "127.0.0.1");
    socket.write(
      "GET /gadget HTTP/1.1\r\nHost: x\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Version: 13\r\n" +
        `Sec-WebSocket-Key: ${randomBytes(16).toString("base64")}\r\nSec-WebSocket-Protocol: openmausbot-gadget.1\r\n\r\n`,
    );
    const data: Buffer[] = [];
    socket.on("data", (chunk: Buffer) => data.push(chunk));
    await new Promise<void>((resolve) => socket.on("close", () => resolve()));
    const all = Buffer.concat(data);
    const closeAt = all.indexOf(Buffer.from([0x88]));
    expect(closeAt).toBeGreaterThan(0);
    expect(all.readUInt16BE(closeAt + 2)).toBe(1008);
  });

  it("folds the computer's name for the gadget and cuts it to 64 bytes", async () => {
    rig = await startRig({ hostName: () => `Zoë’s computer 😀 ${"é".repeat(60)}` });
    const gadget = await rig.enroll();
    const challenge = gadget.frames().find((m) => m.op === "challenge") as { host_name: string };
    expect(challenge.host_name.startsWith("Zoë's computer ")).toBe(true);
    expect(challenge.host_name).not.toContain("😀");
    expect(Buffer.byteLength(challenge.host_name)).toBeLessThanOrEqual(64);
  });
});

describe("one session per gadget", () => {
  it("replaces the old session only after the new one proves itself", async () => {
    rig = await startRig();
    const first = await rig.enroll({ privateKeyHex: RFC_PRIVATE_KEY_HEX });
    // A connection that fails its proof leaves the first one alone.
    const impostor = await rig.connect({ privateKeyHex: RFC_PRIVATE_KEY_HEX, tamper: "sig" });
    expect(impostor.error?.code).toBe("bad_sig");
    expect(rig.hub.session(first.id)).not.toBeNull();
    const second = await rig.connect({ privateKeyHex: RFC_PRIVATE_KEY_HEX });
    expect(second.ready).not.toBeNull();
    expect(await first.next("error")).toMatchObject({ code: "replaced" });
    expect((await first.closed()).code).toBe(1000);
    expect(rig.hub.session(first.id)?.sessionId).toBe(second.ready!.session);
    expect(rig.tracker.ids()).toEqual([first.id]);
  });
});

describe("removal and settings", () => {
  it("sends error revoked on Remove and reports the removal once", async () => {
    rig = await startRig();
    const gadget = await rig.enroll();
    rig.devices.revoke(gadget.id);
    rig.tracker.disconnect(gadget.id); // what control.ts's disconnectDevice does
    rig.hub.revoke(gadget.id); // what index.ts's revoked() does
    rig.hub.revoke(gadget.id);
    expect(await gadget.next("error")).toMatchObject({ code: "revoked" });
    expect((await gadget.closed()).code).toBe(1000);
    expect(rig.changes.filter((c) => c.kind === "removed")).toEqual([{ kind: "removed", deviceId: gadget.id }]);
    rig.hub.revoke("8c1f0a64-phone-uuid");
    expect(rig.changes.filter((c) => c.kind === "removed")).toHaveLength(1);
  });

  it("pushes settings to a live gadget and holds an offline rename until its next hello", async () => {
    rig = await startRig({}, [
      { id: "b1", name: "Jev", threadId: "th1" },
      { id: "b2", name: "Ops", threadId: "th9" },
    ]);
    const gadget = await rig.enroll({ privateKeyHex: RFC_PRIVATE_KEY_HEX });
    rig.devices.updateGadget(gadget.id, { botId: "b2", speakPushes: true });
    rig.hub.settingsChanged(gadget.id, { nameChanged: false });
    expect(await gadget.next("settings")).toEqual({ op: "settings", bot: { id: "b2", name: "Ops" }, settings: { speak_pushes: true } });

    gadget.close();
    await gadget.closed();
    await expect.poll(() => rig.hub.online()).toEqual([]);
    rig.devices.updateGadget(gadget.id, { name: "Kitchen" });
    rig.hub.settingsChanged(gadget.id, { nameChanged: true });
    expect(rig.devices.gadget(gadget.id)?.namePending).toBe(true);
    const back = await rig.connect({ privateKeyHex: RFC_PRIVATE_KEY_HEX, name: "Old name" });
    expect(await back.next("settings")).toMatchObject({ name: "Kitchen", bot: { id: "b2" } });
    expect(rig.devices.gadget(gadget.id)).toMatchObject({ name: "Kitchen" });
  });

  it("keeps sensor readings and the last ten events", async () => {
    rig = await startRig();
    const gadget = await rig.enroll();
    gadget.send({ op: "sense", battery_pct: 64, charging: true });
    for (let i = 0; i < 12; i++) gadget.send({ op: "event", name: `button.${i}` });
    await expect.poll(() => rig.hub.recentEvents(gadget.id).length).toBe(10);
    expect(rig.hub.recentEvents(gadget.id)[0]!.name).toBe("button.2");
    expect(rig.devices.gadget(gadget.id)?.lastSensors).toMatchObject({ battery_pct: 64, charging: true });
  });
});

describe("robustness", () => {
  it("ignores garbage, unknown ops and a second hello after ready", async () => {
    rig = await startRig();
    const gadget = await rig.enroll();
    gadget.send({ op: "warp" } as never);
    gadget.send({ op: "hello" } as never);
    gadget.send({ op: "answer", id: "a_nothing", option: "allow" });
    gadget.send({ op: "say", turn: "t1-1", text: "still here" });
    expect((await rig.harness.waitFor("POST", /\/messages$/)).json).toMatchObject({ text: "still here" });
    expect(rig.hub.online()).toEqual([gadget.id]);
  });
});

describe("unbound gadgets", () => {
  it("gets an empty bot when none can be found, and the default bot on a later connection", async () => {
    rig = await startRig({}, []);
    const window = rig.devices.openPairing();
    const gadget = await rig.connect({ privateKeyHex: RFC_PRIVATE_KEY_HEX, enroll: window.code });
    expect(gadget.ready?.bot).toEqual({ id: "", name: "" });
    gadget.close();
    await gadget.closed();
    rig.harness.bots = [{ id: "b_sec", name: "Sectioned", threadId: "t2", section: "x" }, { id: "b_pin", name: "Pinned", threadId: "t3", pinned: true }];
    const back = await rig.connect({ privateKeyHex: RFC_PRIVATE_KEY_HEX });
    expect(back.ready?.bot).toEqual({ id: "b_pin", name: "Pinned" });
    expect(rig.devices.gadget(gadget.id)?.botId).toBe("b_pin");
  });
});

describe("shutdown", () => {
  it("closes every gadget with 1001 and resolves", async () => {
    rig = await startRig();
    const gadget = await rig.enroll();
    await rig.hub.close();
    expect((await gadget.closed()).code).toBe(1001);
    expect(rig.hub.online()).toEqual([]);
  });

  it("is a path check only for /gadget", () => {
    return startRig().then((r) => {
      rig = r;
      expect(rig.hub.isGadgetPath("/gadget")).toBe(true);
      expect(rig.hub.isGadgetPath("/gadget?x=1")).toBe(true);
      expect(rig.hub.isGadgetPath("/gadgets")).toBe(false);
      expect(rig.hub.isGadgetPath("/vps-viewer/x")).toBe(false);
      expect(rig.hub.isGadgetPath(undefined)).toBe(false);
    });
  });
});
````

- [ ] **Step 3: Write the failing turn test**

It has one test per Stop case (spec §6.2). Those run with `stopVoice`, a `SpeechOut` whose `stop()` sends `speak.stop`, so each test can check that `speak.stop` goes out before `done stopped` (and that a stop for an ended turn sends neither). It also covers:

- barge-in, with `speak.stop` and the old turn's `done` before anything of the new turn;
- speech stopped before a turn that starts after `done` (a recording `SpeechOut`);
- steered and queued sends: a drain bound by its queue id alone, and a queued send the person steers from the desktop (Review Focus item 6);
- thread pinning;
- the 250 ms reply cap, measured between consecutive `reply` frames as they arrive, the final one included (10 ms of slack covers timer rounding and socket delivery);
- voice turns through the STT seam;
- Review Focus items 1, 2 and 5 (`when things go wrong`).

Create `companion/test/gadget/hub-turns.test.ts`:

````ts
// Conversation turns through the hub (spec §4.4, §6.2): the pinned thread,
// the send id, live replies from content.delta, the final reply from the
// terminal patch, working phrases, steered and queued sends, every Stop
// case, barge-in, voice turns through the STT seam, and disconnects.
import { afterEach, describe, expect, it } from "vitest";

import { REPLY_MIN_INTERVAL_MS, TEXT_FRAME_MAX, type HostToGadget } from "../../src/gadget/protocol.ts";
import type { VoiceProvider } from "../../src/gadget/session.ts";
import { gadgetSendId, QUEUED_COPY, UNBOUND_COPY } from "../../src/gadget/turns.ts";
import type { TestGadget } from "./helpers/gadget-client.ts";
import { JEV, startRig, type Rig } from "./helpers/hub-rig.ts";

let rig: Rig;
afterEach(async () => { await rig?.close(); });

const runtime = (event: Record<string, unknown>) => ({ kind: "runtime", event: { threadId: "th1", ...event } });
const botText = (extra: Record<string, unknown>) => ({
  kind: "message.patch", threadId: "th1",
  message: { id: "m_bot_1", role: "bot", kind: "text", text: "", at: Date.now(), ...extra },
});
/** The ops a gadget received after a point, in order. */
const opsSince = (gadget: TestGadget, start: number) => gadget.frames().slice(start).map((m) => m.op);

/** A SpeechOut whose stop() sends speak.stop, as P3b's does while a stream
 *  plays, so tests can see where the turn engine stops speech. */
const stopVoice: VoiceProvider = () => ({
  stt: async () => ({ ok: false, reason: "no speech-to-text in this test" }),
  speech: ({ session }) => ({ replyFinal() {}, post() {}, close() {}, stop() { session.send({ op: "speak.stop", stream: 7 }); } }),
});

/** Stop cases 1–4 (spec §6.2): speak.stop, then `done stopped`. Only the
 *  `working` that clears a shown phrase may sit between them. */
const expectSpeakStopThenDone = (gadget: TestGadget, start: number) =>
  expect(opsSince(gadget, start).filter((op) => op !== "working")).toEqual(["speak.stop", "done"]);

async function ready(bots = [JEV], voice?: VoiceProvider) {
  rig = await startRig(voice ? { voice } : {}, bots);
  const gadget = await rig.enroll();
  await rig.harness.waitForSubscriber();
  return gadget;
}

describe("a typed turn", () => {
  it("pins the thread, streams the reply, shows working, then ends with the final text", async () => {
    const gadget = await ready();
    gadget.send({ op: "say", turn: "t1a2b3c4d-1", text: "what's on today?" });
    const sent = await rig.harness.waitFor("POST", /^\/api\/bots\/b1\/messages$/);
    expect(sent.json).toEqual({ text: "what's on today?", threadId: "th1", sendId: gadgetSendId(gadget.id, gadget.ready!.session, "t1a2b3c4d-1") });
    expect((sent.json as { sendId: string }).sendId).toMatch(/^gdt[A-Za-z0-9_-]{40}$/);
    expect(sent.headers["x-openmausbot-companion-device"]).toBe(gadget.id);

    rig.harness.emit(runtime({ type: "turn.started", turnId: "tu1" }));
    rig.harness.emit({ kind: "message", threadId: "th1", message: { id: "m_act", role: "bot", kind: "activity", turnId: "tu1", tool: { name: "web_search", spoken: "searching the web" }, at: Date.now() } });
    expect(await gadget.next("working")).toEqual({ op: "working", turn: "t1a2b3c4d-1", text: "searching the web" });
    rig.harness.emit(runtime({ type: "content.delta", turnId: "tu1", streamKind: "assistant_text", delta: "You have **two** meetings" }));
    expect(await gadget.next("reply")).toEqual({ op: "reply", turn: "t1a2b3c4d-1", text: "You have two meetings", final: false });

    const before = gadget.frames().length;
    rig.harness.emit(runtime({ type: "turn.completed", turnId: "tu1", ok: true }));
    rig.harness.emit(botText({ turnId: "tu1", turnTerminal: true, text: "You have **two** meetings 📅" }));
    expect(await gadget.next("done")).toEqual({ op: "done", turn: "t1a2b3c4d-1", outcome: "ok" });
    expect(opsSince(gadget, before)).toEqual(["reply", "working", "done"]);
    expect(gadget.frames().filter((m) => m.op === "reply").at(-1)).toEqual({ op: "reply", turn: "t1a2b3c4d-1", text: "You have two meetings", final: true });
  });

  it("sends at most one reply per 250 ms, the final one included", async () => {
    const gadget = await ready();
    // When each reply arrives: next() resolves as its frame lands.
    const arrivals: Array<{ final: boolean; at: number }> = [];
    const collecting = (async () => {
      for (;;) {
        const reply = await gadget.next("reply");
        arrivals.push({ final: reply.final, at: performance.now() });
        if (reply.final) return;
      }
    })();
    gadget.send({ op: "say", turn: "t1-1", text: "count" });
    await rig.harness.waitFor("POST", /\/messages$/);
    rig.harness.emit(runtime({ type: "turn.started", turnId: "tu1" }));
    for (const word of ["one ", "two ", "three ", "four "]) rig.harness.emit(runtime({ type: "content.delta", turnId: "tu1", streamKind: "assistant_text", delta: word }));
    rig.harness.emit(runtime({ type: "turn.completed", turnId: "tu1", ok: true }));
    rig.harness.emit(botText({ turnId: "tu1", turnTerminal: true, text: "one two three four" }));
    await gadget.next("done");
    await collecting;
    expect(arrivals.map((r) => r.final)).toEqual([false, true]);
    // Every pair of consecutive replies, the final one included, is 250 ms
    // apart; 10 ms of slack covers timer rounding and socket delivery.
    for (let i = 1; i < arrivals.length; i++) {
      expect(arrivals[i]!.at - arrivals[i - 1]!.at).toBeGreaterThanOrEqual(REPLY_MIN_INTERVAL_MS - 10);
    }
  });

  it("follows the person's thread switch on the next turn, while the pinned turn finishes", async () => {
    const gadget = await ready([{ ...JEV, tasks: [{ threadId: "th1", title: "Main" }, { threadId: "th2", title: "Other" }] }]);
    gadget.send({ op: "say", turn: "t1-1", text: "first" });
    await rig.harness.waitFor("POST", /\/messages$/);
    rig.harness.emit(runtime({ type: "turn.started", turnId: "tu1" }));
    rig.harness.emit({ kind: "bot", bot: { ...JEV, threadId: "th2", tasks: [{ threadId: "th1", title: "Main" }, { threadId: "th2", title: "Other" }] } });
    rig.harness.emit(runtime({ type: "turn.completed", turnId: "tu1", ok: true }));
    rig.harness.emit(botText({ turnId: "tu1", turnTerminal: true, text: "done in th1" }));
    expect(await gadget.next("done")).toMatchObject({ turn: "t1-1", outcome: "ok" });
    gadget.send({ op: "say", turn: "t1-2", text: "second" });
    expect((await rig.harness.waitFor("POST", /\/messages$/)).json).toMatchObject({ threadId: "th2" });
  });

  it("maps failed and interrupted turns, with the runtime error as the reason", async () => {
    const gadget = await ready();
    gadget.send({ op: "say", turn: "t1-1", text: "go" });
    await rig.harness.waitFor("POST", /\/messages$/);
    rig.harness.emit(runtime({ type: "turn.started", turnId: "tu1" }));
    rig.harness.emit(runtime({ type: "runtime.error", turnId: "tu1", message: "The model is overloaded — try again", terminal: true }));
    rig.harness.emit(runtime({ type: "turn.completed", turnId: "tu1", ok: false, stopReason: "error" }));
    expect(await gadget.next("done")).toEqual({ op: "done", turn: "t1-1", outcome: "failed", reason: "The model is overloaded - try again" });
    gadget.send({ op: "say", turn: "t1-2", text: "again" });
    await rig.harness.waitFor("POST", /\/messages$/);
    rig.harness.emit(runtime({ type: "turn.started", turnId: "tu2" }));
    rig.harness.emit(runtime({ type: "turn.completed", turnId: "tu2", ok: false, stopReason: "interrupted" }));
    expect(await gadget.next("done")).toMatchObject({ turn: "t1-2", outcome: "stopped" });
  });

  it("fails a turn on an unbound gadget with a pointer to Remote access", async () => {
    rig = await startRig({}, []);
    const window = rig.devices.openPairing();
    const gadget = await rig.connect({ enroll: window.code });
    gadget.send({ op: "say", turn: "t1-1", text: "hello" });
    expect(await gadget.next("done")).toEqual({ op: "done", turn: "t1-1", outcome: "failed", reason: UNBOUND_COPY });
    expect(rig.harness.requests.some((r) => r.path.endsWith("/messages"))).toBe(false);
  });
});

describe("busy bots", () => {
  it("treats a steered send's running turn as the gadget's answer", async () => {
    const gadget = await ready();
    rig.harness.emit(runtime({ type: "turn.started", turnId: "tu0" }));
    rig.harness.route("POST", /\/messages$/, (req) => ({
      status: 202,
      json: { ok: true, steered: true, threadId: "th1", message: { id: "m_user_s", role: "user", kind: "text", sendId: (req.json as { sendId: string }).sendId, at: Date.now() } },
    }));
    gadget.send({ op: "say", turn: "t1-1", text: "also check the weather" });
    await rig.harness.waitFor("POST", /\/messages$/);
    rig.harness.emit(runtime({ type: "content.delta", turnId: "tu0", streamKind: "assistant_text", delta: "Sunny" }));
    expect(await gadget.next("reply")).toMatchObject({ text: "Sunny", final: false });
    rig.harness.emit(runtime({ type: "turn.completed", turnId: "tu0", ok: true }));
    rig.harness.emit(botText({ turnId: "tu0", requestMessageId: "m_person", turnTerminal: true, text: "Sunny, 24 °C" }));
    expect(await gadget.next("done")).toMatchObject({ outcome: "ok" });
    expect(gadget.frames().filter((m) => m.op === "reply").at(-1)).toMatchObject({ text: "Sunny, 24 °C", final: true });
  });

  it("says a queued send is queued, then binds when it is drained, by its queue id", async () => {
    const gadget = await ready();
    rig.harness.route("POST", /\/messages$/, () => ({ status: 202, json: { ok: true, queued: true, queueId: "q1", threadId: "th1" } }));
    gadget.send({ op: "say", turn: "t1-1", text: "later" });
    expect(await gadget.next("working")).toEqual({ op: "working", turn: "t1-1", text: QUEUED_COPY });
    // No sendId here on purpose: the queue id alone binds the drained words.
    rig.harness.emit({ kind: "message", threadId: "th1", message: { id: "m_drained", role: "user", kind: "text", text: "later", queueId: "q1", at: Date.now() } });
    expect(await gadget.next("working")).toEqual({ op: "working", turn: "t1-1", text: "" });
    rig.harness.emit(runtime({ type: "turn.started", turnId: "tu5" }));
    rig.harness.emit(runtime({ type: "turn.completed", turnId: "tu5", ok: true }));
    rig.harness.emit(botText({ turnId: "tu5", requestMessageId: "m_drained", turnTerminal: true, text: "Done later" }));
    expect(await gadget.next("done")).toMatchObject({ turn: "t1-1", outcome: "ok" });
  });

  it("binds a queued send that the person steered from the desktop to the running turn", async () => {
    const gadget = await ready();
    rig.harness.emit(runtime({ type: "turn.started", turnId: "tu0" }));
    let queued = 0;
    const sendIds: string[] = [];
    rig.harness.route("POST", /\/messages$/, (req) => {
      sendIds.push((req.json as { sendId: string }).sendId);
      queued += 1;
      return { status: 202, json: { ok: true, queued: true, queueId: `q${queued}`, threadId: "th1" } };
    });
    gadget.send({ op: "say", turn: "t1-1", text: "and the weather" });
    expect(await gadget.next("working")).toEqual({ op: "working", turn: "t1-1", text: QUEUED_COPY });
    // Steer on the desktop (POST /api/bots/:id/queue/:queueId/steer) appends
    // the queued words with steered: true; they start no turn of their own.
    rig.harness.emit({ kind: "message", threadId: "th1", message: { id: "m_steered", role: "user", kind: "text", text: "and the weather", sendId: sendIds[0], queueId: "q1", steered: true, at: Date.now() } });
    expect(await gadget.next("working")).toEqual({ op: "working", turn: "t1-1", text: "" });
    rig.harness.emit(runtime({ type: "content.delta", turnId: "tu0", streamKind: "assistant_text", delta: "Sunny" }));
    expect(await gadget.next("reply")).toEqual({ op: "reply", turn: "t1-1", text: "Sunny", final: false });
    rig.harness.emit(runtime({ type: "turn.completed", turnId: "tu0", ok: true }));
    rig.harness.emit(botText({ turnId: "tu0", requestMessageId: "m_person", turnTerminal: true, text: "Sunny all day" }));
    expect(await gadget.next("done")).toEqual({ op: "done", turn: "t1-1", outcome: "ok" });
    expect(gadget.frames().filter((m) => m.op === "reply").at(-1)).toEqual({ op: "reply", turn: "t1-1", text: "Sunny all day", final: true });

    // Stopping a queued send that was steered leaves the running turn alone.
    rig.harness.emit(runtime({ type: "turn.started", turnId: "tu1" }));
    gadget.send({ op: "say", turn: "t1-2", text: "and tomorrow" });
    await gadget.next("working");
    rig.harness.emit({ kind: "message", threadId: "th1", message: { id: "m_steered2", role: "user", kind: "text", text: "and tomorrow", sendId: sendIds[1], queueId: "q2", steered: true, at: Date.now() } });
    expect(await gadget.next("working")).toEqual({ op: "working", turn: "t1-2", text: "" });
    gadget.send({ op: "stop" });
    expect(await gadget.next("done")).toEqual({ op: "done", turn: "t1-2", outcome: "stopped" });
    gadget.send({ op: "say", turn: "t1-3", text: "next" });
    await expect.poll(() => sendIds.length).toBe(3);
    expect(rig.harness.requests.some((r) => /\/interrupt$|\/queue\//.test(r.path))).toBe(false);
  });
});

describe("stop", () => {
  // These run with stopVoice, so every SpeechOut.stop() shows up as a
  // speak.stop frame. A new turn also stops whatever speech is playing, so
  // each test first takes the speak.stop that its own say or voice.begin
  // caused before it records where the stop starts.
  it("drops audio that was never sent", async () => {
    const gadget = await ready([JEV], stopVoice);
    gadget.send({ op: "voice.begin", turn: "t1-1", stream: 3, rate: 16000 });
    await gadget.next("speak.stop");
    const before = gadget.frames().length;
    gadget.sendBinary(1, 3, new Uint8Array(640));
    gadget.send({ op: "stop", turn: "t1-1" });
    expect(await gadget.next("done")).toEqual({ op: "done", turn: "t1-1", outcome: "stopped" });
    expectSpeakStopThenDone(gadget, before);
    expect(rig.harness.requests.some((r) => r.method === "POST")).toBe(false);
  });

  it("interrupts the gadget's own running turn in the pinned thread", async () => {
    const gadget = await ready([JEV], stopVoice);
    gadget.send({ op: "say", turn: "t1-1", text: "long job" });
    await gadget.next("speak.stop");
    await rig.harness.waitFor("POST", /\/messages$/);
    const before = gadget.frames().length;
    gadget.send({ op: "stop" });
    expect(await gadget.next("done")).toMatchObject({ turn: "t1-1", outcome: "stopped" });
    expectSpeakStopThenDone(gadget, before);
    expect((await rig.harness.waitFor("POST", /^\/api\/bots\/b1\/interrupt$/)).json).toEqual({ threadId: "th1" });
  });

  it("deletes a queued send that has not been drained", async () => {
    const gadget = await ready([JEV], stopVoice);
    rig.harness.route("POST", /\/messages$/, () => ({ status: 202, json: { ok: true, queued: true, queueId: "q7", threadId: "th1" } }));
    gadget.send({ op: "say", turn: "t1-1", text: "later" });
    await gadget.next("speak.stop");
    await gadget.next("working");
    const before = gadget.frames().length;
    gadget.send({ op: "stop", turn: "t1-1" });
    expect(await gadget.next("done")).toMatchObject({ outcome: "stopped" });
    expectSpeakStopThenDone(gadget, before);
    const deleted = await rig.harness.waitFor("DELETE", /^\/api\/bots\/b1\/queue\/q7$/);
    expect(deleted.json).toEqual({ threadId: "th1" });
  });

  it("leaves a steered turn running, since it belongs to someone else", async () => {
    const gadget = await ready([JEV], stopVoice);
    rig.harness.route("POST", /\/messages$/, () => ({ status: 202, json: { ok: true, steered: true, threadId: "th1", message: { id: "m_s", role: "user", kind: "text", at: 1 } } }));
    gadget.send({ op: "say", turn: "t1-1", text: "and this" });
    await gadget.next("speak.stop");
    await rig.harness.waitFor("POST", /\/messages$/);
    const before = gadget.frames().length;
    gadget.send({ op: "stop" });
    expect(await gadget.next("done")).toMatchObject({ outcome: "stopped" });
    expectSpeakStopThenDone(gadget, before);
    gadget.send({ op: "say", turn: "t1-2", text: "next" });
    await rig.harness.waitFor("POST", /\/messages$/);
    expect(rig.harness.requests.some((r) => /interrupt|queue/.test(r.path))).toBe(false);
  });

  it("ignores a stop for a turn that already ended", async () => {
    const gadget = await ready([JEV], stopVoice);
    gadget.send({ op: "say", turn: "t1-1", text: "hi" });
    await rig.harness.waitFor("POST", /\/messages$/);
    rig.harness.emit(runtime({ type: "turn.started", turnId: "tu1" }));
    rig.harness.emit(runtime({ type: "turn.completed", turnId: "tu1", ok: true }));
    rig.harness.emit(botText({ turnId: "tu1", turnTerminal: true, text: "hello" }));
    await gadget.next("done");
    const count = gadget.frames().length;
    gadget.send({ op: "stop", turn: "t1-1" });
    // A barrier: a gadget's frames are handled in order, so once the hub has
    // recorded this event it has handled the stop. The settings frame sent
    // after that reaches the gadget after anything the stop sent.
    gadget.send({ op: "event", name: "barrier" });
    await expect.poll(() => rig.hub.recentEvents(gadget.id).some((e) => e.name === "barrier")).toBe(true);
    rig.hub.settingsChanged(gadget.id, { nameChanged: false });
    await gadget.next("settings");
    expect(opsSince(gadget, count)).toEqual(["settings"]); // no speak.stop, no done
    gadget.send({ op: "say", turn: "t1-2", text: "next" });
    await rig.harness.waitFor("POST", /\/messages$/);
    expect(gadget.frames().slice(count).some((m) => m.op === "done")).toBe(false);
    expect(rig.harness.requests.some((r) => r.path.endsWith("/interrupt"))).toBe(false);
  });

  it("stops the old turn's speech and ends it before the new one on barge-in", async () => {
    const gadget = await ready([JEV], stopVoice);
    gadget.send({ op: "say", turn: "t1-1", text: "first" });
    await gadget.next("speak.stop");
    await rig.harness.waitFor("POST", /\/messages$/);
    await expect.poll(() => rig.harness.requests.filter((r) => r.path.endsWith("/messages")).length).toBe(1);
    const start = gadget.frames().length;
    gadget.send({ op: "voice.begin", turn: "t1-2", stream: 4, rate: 24000 });
    const done = await gadget.next("done");
    expect(done).toMatchObject({ turn: "t1-1", outcome: "stopped" });
    const next = await gadget.next("done");
    expect(next).toMatchObject({ turn: "t1-2", outcome: "failed", reason: "Unsupported mic rate" });
    const after = gadget.frames().slice(start);
    expect(after.map((m) => m.op).slice(0, 2)).toEqual(["speak.stop", "done"]);
    expect(after[1]).toEqual({ op: "done", turn: "t1-1", outcome: "stopped" });
    // The old turn's done goes out before any frame of the new turn.
    expect(after.findIndex((m) => "turn" in m && m.turn === "t1-2")).toBeGreaterThan(1);
    const order = after.filter((m): m is Extract<HostToGadget, { op: "done" }> => m.op === "done").map((m) => m.turn);
    expect(order).toEqual(["t1-1", "t1-2"]);
    await rig.harness.waitFor("POST", /\/interrupt$/);
  });

  it("stops the last reply's speech before a new turn that starts after done", async () => {
    const calls: string[] = [];
    const recording: VoiceProvider = () => ({
      stt: async () => ({ ok: false, reason: "no speech-to-text in this test" }),
      speech: () => ({
        replyFinal: ({ turn }) => void calls.push(`replyFinal ${turn}`),
        post: () => void calls.push("post"),
        stop: () => void calls.push("stop"),
        close: () => void calls.push("close"),
      }),
    });
    const gadget = await ready([JEV], recording);
    let sent = 0;
    rig.harness.route("POST", /\/messages$/, (req) => {
      const body = req.json as { text: string; threadId: string; sendId: string };
      calls.push(`POST ${body.text}`);
      sent += 1;
      return { status: 202, json: { ok: true, threadId: body.threadId, message: { id: `m_user_r${sent}`, role: "user", kind: "text", text: body.text, sendId: body.sendId, at: Date.now() } } };
    });
    gadget.send({ op: "say", turn: "t1-1", text: "first" });
    await rig.harness.waitFor("POST", /\/messages$/);
    rig.harness.emit(runtime({ type: "turn.started", turnId: "tu1" }));
    rig.harness.emit(runtime({ type: "turn.completed", turnId: "tu1", ok: true }));
    rig.harness.emit(botText({ turnId: "tu1", turnTerminal: true, text: "A long answer that is still being spoken" }));
    expect(await gadget.next("done")).toMatchObject({ turn: "t1-1", outcome: "ok" });
    gadget.send({ op: "say", turn: "t1-2", text: "second" });
    await rig.harness.waitFor("POST", /\/messages$/);
    expect(calls.slice(calls.indexOf("replyFinal t1-1"))).toEqual(["replyFinal t1-1", "stop", "POST second"]);
  });
});

describe("voice turns through the STT seam", () => {
  const fakeVoice = (text: string): VoiceProvider => () => ({
    stt: async ({ pcm }) => ({ ok: true, text: pcm.length === 1280 ? text : "wrong size", provider: "apple" }),
    speech: () => ({ replyFinal() {}, post() {}, stop() {}, close() {} }),
  });

  it("ends with the stt_unavailable copy when no voice is wired (P3a default)", async () => {
    const gadget = await ready();
    gadget.send({ op: "voice.begin", turn: "t1-1", stream: 2, rate: 16000 });
    gadget.sendBinary(1, 2, new Uint8Array(640));
    gadget.send({ op: "voice.end", turn: "t1-1", ms: 20 });
    expect(await gadget.next("done")).toMatchObject({ outcome: "failed", reason: expect.stringContaining("Speech-to-text isn't set up") });
  });

  it("sends heard, then the transcript, for a voice turn", async () => {
    rig = await startRig({ voice: fakeVoice("turn on the lights") });
    const gadget = await rig.enroll();
    gadget.send({ op: "voice.begin", turn: "t1-1", stream: 2, rate: 16000 });
    gadget.sendBinary(1, 2, new Uint8Array(640));
    gadget.sendBinary(1, 9, new Uint8Array(640)); // another stream: ignored
    gadget.sendBinary(1, 2, new Uint8Array(640));
    gadget.send({ op: "voice.end", turn: "t1-1", ms: 40 });
    expect(await gadget.next("heard")).toEqual({ op: "heard", turn: "t1-1", text: "turn on the lights" });
    expect((await rig.harness.waitFor("POST", /\/messages$/)).json).toMatchObject({ text: "turn on the lights" });
  });

  it("ends with Didn't catch that when nothing was heard, and sends nothing", async () => {
    rig = await startRig({ voice: fakeVoice("  ") });
    const gadget = await rig.enroll();
    gadget.send({ op: "voice.begin", turn: "t1-1", stream: 2, rate: 16000 });
    gadget.sendBinary(1, 2, new Uint8Array(1280));
    gadget.send({ op: "voice.end", turn: "t1-1", ms: 40 });
    expect(await gadget.next("done")).toEqual({ op: "done", turn: "t1-1", outcome: "failed", reason: "Didn't catch that" });
    expect(rig.harness.requests.some((r) => r.path.endsWith("/messages"))).toBe(false);
  });

  it("ends a dropped utterance as stopped", async () => {
    const gadget = await ready();
    gadget.send({ op: "voice.begin", turn: "t1-1", stream: 2, rate: 16000 });
    gadget.send({ op: "voice.drop", turn: "t1-1" });
    expect(await gadget.next("done")).toEqual({ op: "done", turn: "t1-1", outcome: "stopped" });
  });
});

describe("when things go wrong", () => {
  it("still sends ready while the harness is down, and fails the turn with a plain reason", async () => {
    rig = await startRig();
    rig.harness.route("GET", /^\/api\/bots/, () => ({ status: 503, json: { error: "starting" } }));
    const gadget = await rig.enroll();
    expect(gadget.ready?.bot).toEqual({ id: "b1", name: "" });
    gadget.send({ op: "say", turn: "t1-1", text: "hi" });
    expect(await gadget.next("done")).toEqual({ op: "done", turn: "t1-1", outcome: "failed", reason: "MausBot is not answering. Try again in a moment." });
  });

  it("cuts a reply longer than one frame from the start", async () => {
    rig = await startRig();
    const gadget = await rig.enroll();
    await rig.harness.waitForSubscriber();
    gadget.send({ op: "say", turn: "t1-1", text: "essay" });
    await rig.harness.waitFor("POST", /\/messages$/);
    rig.harness.emit({ kind: "runtime", event: { type: "turn.started", threadId: "th1", turnId: "tu1" } });
    rig.harness.emit({ kind: "runtime", event: { type: "turn.completed", threadId: "th1", turnId: "tu1", ok: true } });
    const long = `${"é".repeat(12_000)} the end`;
    rig.harness.emit({ kind: "message.patch", threadId: "th1", message: { id: "m1", role: "bot", kind: "text", text: long, turnId: "tu1", turnTerminal: true, at: 1 } });
    const reply = await gadget.next("reply");
    expect(reply.final).toBe(true);
    expect(reply.text.startsWith("\u{2026}")).toBe(true);
    expect(reply.text.endsWith("the end")).toBe(true);
    expect(Buffer.byteLength(JSON.stringify(reply))).toBeLessThanOrEqual(TEXT_FRAME_MAX);
    await gadget.next("done");
  });

  it("fails a turn whose bot was deleted, pointing at Remote access", async () => {
    rig = await startRig({}, [JEV]);
    const gadget = await rig.enroll();
    await rig.harness.waitForSubscriber();
    rig.harness.bots = [];
    rig.harness.emit({ kind: "bot.deleted", botId: "b1" });
    await expect.poll(() => rig.hub.botName("b1")).toBeNull();
    gadget.send({ op: "say", turn: "t1-1", text: "hello?" });
    expect(await gadget.next("done")).toMatchObject({ outcome: "failed", reason: expect.stringContaining("Remote access") });
  });
});

describe("connection loss", () => {
  it("unbinds a turn whose gadget disconnected, without interrupting the bot", async () => {
    const gadget = await ready();
    gadget.send({ op: "say", turn: "t1-1", text: "keep going" });
    await rig.harness.waitFor("POST", /\/messages$/);
    gadget.close();
    await gadget.closed();
    await expect.poll(() => rig.hub.online()).toEqual([]);
    rig.harness.emit(runtime({ type: "turn.started", turnId: "tu1" }));
    rig.harness.emit(runtime({ type: "turn.completed", turnId: "tu1", ok: true }));
    expect(rig.harness.requests.some((r) => r.path.endsWith("/interrupt"))).toBe(false);
  });

  it("fails an in-flight turn when the harness restarts", async () => {
    const gadget = await ready();
    gadget.send({ op: "say", turn: "t1-1", text: "hi" });
    await rig.harness.waitFor("POST", /\/messages$/);
    rig.harness.emit(runtime({ type: "turn.started", turnId: "tu1" }));
    rig.harness.restart();
    expect(await gadget.next("done", 10_000)).toMatchObject({ turn: "t1-1", outcome: "failed", reason: "MausBot restarted. Try again." });
  });
});
````

- [ ] **Step 4: Run them to see them fail**

Run: `cd /Users/omkar/Desktop/openmaus/OpenGrokBot-gadget && PATH="$HOME/.nvm/versions/node/v24.14.1/bin:$PATH" pnpm exec vitest run companion/test/gadget/hub.test.ts companion/test/gadget/hub-turns.test.ts`
Expected: FAIL — `Cannot find module '../../../src/gadget/hub.ts' imported from …/helpers/hub-rig.ts`.

- [ ] **Step 5: Write `companion/src/gadget/directory.ts`**

````ts
// What the hub knows about bots and threads, kept current from the one
// shared event stream (spec §6.2). Runtime events carry a thread id but no
// bot id, so the thread → bot map is built from `bot` frames and from
// GET /api/bots?messages=0 after a (re)subscribe.
import type { HarnessClient } from "./harness-client.ts";
import type { ServerFrameLite, WireBotLite, WireMessageLite, WireTaskLite } from "./types.ts";

export interface UserMessageNote { peerAsk: boolean; sendId?: string }

export interface BotDirectory {
  /** Replace the bot list from GET /api/bots?messages=0. Throws when the harness is unreachable. */
  refresh(): Promise<void>;
  apply(frame: ServerFrameLite): void;
  bot(id: string): WireBotLite | null;
  visibleBots(): WireBotLite[];
  botForThread(threadId: string): string | null;
  task(threadId: string): WireTaskLite | null;
  runningTurn(threadId: string): string | null;
  userMessage(threadId: string, messageId: string): UserMessageNote | null;
  rememberMessages(threadId: string, messages: readonly WireMessageLite[]): void;
}

const MAX_THREADS = 500;
const MAX_USER_MESSAGES_PER_THREAD = 100;

export function createBotDirectory(harness: HarnessClient): BotDirectory {
  let bots = new Map<string, WireBotLite>();
  const threadBot = new Map<string, string>();
  const tasks = new Map<string, WireTaskLite>();
  const running = new Map<string, string>();
  const users = new Map<string, Map<string, UserMessageNote>>();

  const index = (bot: WireBotLite) => {
    bots.set(bot.id, bot);
    if (bot.threadId) threadBot.set(bot.threadId, bot.id);
    for (const task of bot.tasks ?? []) {
      threadBot.set(task.threadId, bot.id);
      tasks.set(task.threadId, task);
    }
  };

  const remember = (threadId: string, message: WireMessageLite) => {
    if (message.role !== "user" || !message.id) return;
    let notes = users.get(threadId);
    if (!notes) {
      if (users.size >= MAX_THREADS) users.delete(users.keys().next().value!);
      notes = new Map();
      users.set(threadId, notes);
    }
    notes.set(message.id, { peerAsk: Boolean(message.peerAsk), ...(message.sendId ? { sendId: message.sendId } : {}) });
    if (notes.size > MAX_USER_MESSAGES_PER_THREAD) notes.delete(notes.keys().next().value!);
  };

  return {
    async refresh() {
      const response = await harness.json<{ bots?: WireBotLite[] }>("GET", "/api/bots?messages=0", null);
      if (response.status !== 200 || !Array.isArray(response.body.bots)) {
        throw new Error(`the harness answered ${response.status} to GET /api/bots`);
      }
      bots = new Map();
      threadBot.clear();
      tasks.clear();
      for (const bot of response.body.bots) if (bot && typeof bot.id === "string") index(bot);
    },
    apply(frame) {
      switch (frame.kind) {
        case "bot":
          if (frame.bot && typeof frame.bot.id === "string") index(frame.bot);
          return;
        case "bot.deleted":
          bots.delete(frame.botId);
          return;
        case "message":
        case "message.patch":
          if (frame.message) remember(frame.threadId, frame.message);
          return;
        case "runtime": {
          const event = frame.event;
          if (!event?.threadId || !event.turnId) return;
          if (event.type === "turn.started") running.set(event.threadId, event.turnId);
          if (event.type === "turn.completed" && running.get(event.threadId) === event.turnId) running.delete(event.threadId);
          return;
        }
        default:
      }
    },
    bot: (id) => bots.get(id) ?? null,
    visibleBots: () => [...bots.values()].filter((bot) => !bot.hidden),
    botForThread: (threadId) => threadBot.get(threadId) ?? null,
    task: (threadId) => tasks.get(threadId) ?? null,
    runningTurn: (threadId) => running.get(threadId) ?? null,
    userMessage: (threadId, messageId) => users.get(threadId)?.get(messageId) ?? null,
    rememberMessages(threadId, messages) {
      for (const message of messages) remember(threadId, message);
    },
  };
}
````

- [ ] **Step 6: Write `companion/src/gadget/turns.ts`**

````ts
// One gadget's conversation turns (spec §4.4 and the §6.2 mapping).
//
// A turn is in flight from `voice.begin` or `say` until the hub sends its
// `done`. It talks in the bot's selected thread, read once at the start and
// pinned, and every request names that thread explicitly. Replies are bound
// to the turn the send started (or steered into, or was queued behind) by
// the harness's turn id, which is what runtime events carry.
import { Buffer } from "node:buffer";
import { createHash } from "node:crypto";

import type { DeviceRegistry } from "../devices.ts";
import type { BotDirectory } from "./directory.ts";
import { HarnessRefused, type HarnessClient } from "./harness-client.ts";
import {
  encodeHostMessage,
  REPLY_MIN_INTERVAL_MS,
  SAY_MAX_CHARS,
  TEXT_FRAME_MAX,
  UTTERANCE_MAX_MS,
  type HostToGadget,
  type ReplyMsg,
  type SayMsg,
  type StopMsg,
  type TurnOutcome,
  type VoiceBeginMsg,
  type VoiceDropMsg,
  type VoiceEndMsg,
} from "./protocol.ts";
import { clampChars, cutFromStart, screenText } from "./shape.ts";
import type { SpeechOut, SttFn } from "./session.ts";
import type { DirectSendReceiptLite, ServerFrameLite, WireMessageLite } from "./types.ts";

export const UNBOUND_COPY = "Pick a bot for this gadget in MausBot → Settings → Remote access";
export const QUEUED_COPY = "Queued behind another task";
const MIC_BYTES_PER_MS = 32; // 16 kHz mono PCM16
const MAX_UTTERANCE_BYTES = UTTERANCE_MAX_MS * MIC_BYTES_PER_MS;

/** "gdt" + base64url(sha256(gadgetId:session:turn)).slice(0, 40): 43 characters
 *  in the harness's sendId alphabet; the session keeps a reboot from ever
 *  colliding with an old turn, and the prefix marks gadget sends. */
export function gadgetSendId(gadgetId: string, sessionId: string, turn: string): string {
  return `gdt${createHash("sha256").update(`${gadgetId}:${sessionId}:${turn}`).digest("base64url").slice(0, 40)}`;
}

type Phase = "recording" | "transcribing" | "posting" | "queued" | "running" | "steered";

/** What the pinned thread's runtime events said about one harness turn,
 * kept so a turn bound late (its user message or reply arrived after its
 * events) still gets its streamed text and its outcome. */
interface SeenTurn { text: string; completed?: { ok: boolean; stopReason?: string | null }; error?: string }
const SEEN_TURNS_MAX = 8;

interface Turn {
  id: string;
  phase: Phase;
  ended: boolean;
  micStream?: number;
  pcm: Buffer[];
  pcmBytes: number;
  botId?: string;
  voiceId?: string;
  threadId?: string;
  sendId?: string;
  userMessageId?: string;
  queueId?: string;
  boundTurnId: string | null;
  /** The next turn.started on the pinned thread is this turn's. */
  awaitStart: boolean;
  /** Steered into a running turn whose id the hub never saw start. */
  steerAny: boolean;
  /** stop (or barge-in) arrived while the send was in flight. */
  stopRequested: boolean;
  replyRaw: string;
  replySent: string;
  lastReplyAt: number;
  replyTimer: ReturnType<typeof setTimeout> | null;
  /** The final reply waits out the 250 ms after a partial one (spec §4.4). */
  finalTimer: ReturnType<typeof setTimeout> | null;
  workingShown: boolean;
  finalSent: boolean;
  completed: { outcome: TurnOutcome; reason?: string } | null;
  graceTimer: ReturnType<typeof setTimeout> | null;
  lastError?: string;
  seen: Map<string, SeenTurn>;
}

export interface TurnContext {
  deviceId: string;
  sessionId: string;
  send(msg: HostToGadget): boolean;
  harness: HarnessClient;
  devices: DeviceRegistry;
  directory: BotDirectory;
  stt: SttFn;
  speech: SpeechOut;
  log(line: string): void;
  now(): number;
  terminalGraceMs: number;
  replyIntervalMs?: number;
}

export interface TurnEngine {
  say(msg: SayMsg): void;
  voiceBegin(msg: VoiceBeginMsg): void;
  voiceAudio(stream: number, pcm: Buffer): void;
  voiceEnd(msg: VoiceEndMsg): void;
  voiceDrop(msg: VoiceDropMsg): void;
  stop(msg: StopMsg): void;
  onFrame(frame: ServerFrameLite): void;
  /** A reply that this engine consumed as a turn's answer (never a push). */
  ownsMessage(messageId: string): boolean;
  /** The harness restarted under an in-flight turn: it is gone. */
  harnessRestarted(): void;
  /** The socket closed: drop everything, send nothing. */
  close(): void;
}

const fold = (text: string) => screenText(text, { markdown: false });

export function createTurnEngine(ctx: TurnContext): TurnEngine {
  const replyInterval = ctx.replyIntervalMs ?? REPLY_MIN_INTERVAL_MS;
  let current: Turn | null = null;
  const consumed = new Set<string>();

  const newTurn = (id: string, phase: Phase): Turn => ({
    id, phase, ended: false, pcm: [], pcmBytes: 0, boundTurnId: null, awaitStart: false, steerAny: false,
    stopRequested: false, replyRaw: "", replySent: "", lastReplyAt: 0, replyTimer: null, finalTimer: null, workingShown: false,
    finalSent: false, completed: null, graceTimer: null, seen: new Map(),
  });

  const clearTimers = (turn: Turn) => {
    if (turn.replyTimer) clearTimeout(turn.replyTimer);
    if (turn.graceTimer) clearTimeout(turn.graceTimer);
    if (turn.finalTimer) clearTimeout(turn.finalTimer);
    turn.replyTimer = null;
    turn.graceTimer = null;
    turn.finalTimer = null;
  };

  /** End a turn on the gadget: clear `working`, then `done`. */
  const finish = (turn: Turn, outcome: TurnOutcome, reason?: string) => {
    if (turn.ended) return;
    turn.ended = true;
    turn.pcm = [];
    clearTimers(turn);
    if (turn.workingShown) ctx.send({ op: "working", turn: turn.id, text: "" });
    const shown = reason ? clampChars(fold(reason), 160) : undefined;
    ctx.send({ op: "done", turn: turn.id, outcome, ...(shown ? { reason: shown } : {}) });
    if (current === turn) current = null;
  };

  /** The reply text that fits one frame: cut from the start with "…". */
  const fitReply = (turn: Turn, text: string, final: boolean): ReplyMsg => {
    const envelope = Buffer.byteLength(encodeHostMessage({ op: "reply", turn: turn.id, text: "", final }));
    return { op: "reply", turn: turn.id, text: cutFromStart(text, TEXT_FRAME_MAX - envelope - 16), final };
  };

  const flushReply = (turn: Turn) => {
    turn.replyTimer = null;
    if (turn.ended || turn.finalSent) return;
    const text = screenText(turn.replyRaw, { markdown: true });
    if (!text || text === turn.replySent) return;
    turn.replySent = text;
    turn.lastReplyAt = ctx.now();
    ctx.send(fitReply(turn, text, false));
  };

  /** At most one `reply` every 250 ms; the last delta always gets out. */
  const scheduleReply = (turn: Turn) => {
    if (turn.replyTimer) return;
    const wait = turn.lastReplyAt + replyInterval - ctx.now();
    if (wait <= 0) flushReply(turn);
    else {
      turn.replyTimer = setTimeout(() => flushReply(turn), wait);
      turn.replyTimer.unref?.();
    }
  };

  /** The final reply, then its speech, then `done` once the turn has
   *  completed. The 250 ms cap holds for the final reply too: when a partial
   *  one went out less than 250 ms ago, the final reply (and the `done`
   *  after it) waits for the rest of the interval. */
  const sendFinal = (turn: Turn, rawText: string, messageId?: string) => {
    if (turn.finalSent || turn.ended) return;
    if (turn.replyTimer) clearTimeout(turn.replyTimer);
    turn.replyTimer = null;
    turn.finalSent = true;
    if (messageId) consumed.add(messageId);
    const text = screenText(rawText, { markdown: true });
    const emit = () => {
      turn.finalTimer = null;
      turn.lastReplyAt = ctx.now();
      ctx.send(fitReply(turn, text, true));
      // Speech gets the RAW text: /api/tts/prepare makes it speakable itself.
      if (rawText.trim()) ctx.speech.replyFinal({ turn: turn.id, rawText, ...(turn.voiceId ? { voiceId: turn.voiceId } : {}) });
      if (turn.completed) finish(turn, turn.completed.outcome, turn.completed.reason);
    };
    const wait = turn.lastReplyAt + replyInterval - ctx.now();
    if (turn.lastReplyAt === 0 || wait <= 0) return emit();
    turn.finalTimer = setTimeout(emit, wait);
    turn.finalTimer.unref?.();
  };

  const complete = (turn: Turn, outcome: TurnOutcome) => {
    const reason = outcome === "failed" ? turn.lastError ?? "Something went wrong" : undefined;
    turn.completed = { outcome, ...(reason ? { reason } : {}) };
    if (turn.finalSent) {
      // A final reply still waiting out the 250 ms ends the turn itself.
      if (!turn.finalTimer) finish(turn, outcome, reason);
      return;
    }
    // The harness broadcasts turn.completed a moment before it marks the
    // terminal reply; give that patch a short grace before falling back to
    // the streamed text.
    turn.graceTimer = setTimeout(() => {
      if (turn.ended) return;
      if (turn.replyRaw.trim()) sendFinal(turn, turn.replyRaw);
      if (!turn.finalTimer) finish(turn, outcome, reason);
    }, ctx.terminalGraceMs);
    turn.graceTimer.unref?.();
  };

  /** What stop means depends on how far the turn got (spec §6.2 Stop). */
  const stopCurrent = () => {
    const turn = current;
    if (!turn) return;
    const call = (method: "POST" | "DELETE", path: string) =>
      void ctx.harness.json(method, path, ctx.deviceId, { threadId: turn.threadId }).catch((error: Error) =>
        ctx.log(`stop ${turn.id}: ${error.message}`),
      );
    if (turn.phase === "posting") turn.stopRequested = true;
    else if (turn.phase === "running") call("POST", `/api/bots/${turn.botId}/interrupt`);
    else if (turn.phase === "queued") {
      if (turn.userMessageId) call("POST", `/api/bots/${turn.botId}/interrupt`);
      else call("DELETE", `/api/bots/${turn.botId}/queue/${turn.queueId}`);
    }
    // recording/transcribing: nothing reached the bot; steered: the running
    // turn belongs to someone else, so it is left alone.
    ctx.speech.stop();
    finish(turn, "stopped");
  };

  const outcomeOf = (completed: { ok: boolean; stopReason?: string | null }): TurnOutcome =>
    completed.ok ? "ok" : completed.stopReason === "interrupted" ? "stopped" : "failed";

  const bind = (turn: Turn, turnId: string) => {
    turn.boundTurnId = turnId;
    turn.awaitStart = false;
    turn.steerAny = false;
    const seen = turn.seen.get(turnId);
    if (!seen) return;
    if (seen.error) turn.lastError = seen.error;
    if (seen.text) {
      turn.replyRaw = seen.text;
      scheduleReply(turn);
    }
    if (seen.completed) complete(turn, outcomeOf(seen.completed));
  };

  const note = (turn: Turn, turnId: string): SeenTurn => {
    let seen = turn.seen.get(turnId);
    if (!seen) {
      if (turn.seen.size >= SEEN_TURNS_MAX) turn.seen.delete(turn.seen.keys().next().value!);
      seen = { text: "" };
      turn.seen.set(turnId, seen);
    }
    return seen;
  };

  const post = async (turn: Turn, text: string) => {
    const record = ctx.devices.gadget(ctx.deviceId);
    if (!record?.botId) return finish(turn, "failed", UNBOUND_COPY);
    let bot = ctx.directory.bot(record.botId);
    if (!bot) {
      try {
        await ctx.directory.refresh();
      } catch {
        return finish(turn, "failed", "MausBot is not answering. Try again in a moment.");
      }
      bot = ctx.directory.bot(record.botId);
    }
    if (turn.ended) return;
    if (!bot) return finish(turn, "failed", "This gadget's bot is gone. Pick another in MausBot → Settings → Remote access");
    turn.botId = bot.id;
    turn.voiceId = bot.voice;
    turn.threadId = bot.threadId;
    turn.sendId = gadgetSendId(ctx.deviceId, ctx.sessionId, turn.id);
    turn.phase = "posting";
    turn.awaitStart = true;
    let status: number;
    let receipt: DirectSendReceiptLite | { error?: string };
    try {
      const response = await ctx.harness.json<DirectSendReceiptLite | { error?: string }>(
        "POST", `/api/bots/${bot.id}/messages`, ctx.deviceId, { text, threadId: turn.threadId, sendId: turn.sendId },
      );
      status = response.status;
      receipt = response.body;
    } catch (error) {
      if (turn.ended) return;
      return finish(turn, "failed", error instanceof HarnessRefused && error.status === 503
        ? "MausBot is starting. Try again in a moment."
        : "MausBot is not answering. Try again in a moment.");
    }
    if (status !== 202 || !("ok" in receipt)) {
      if (turn.ended) return;
      const message = "error" in receipt && typeof receipt.error === "string" ? receipt.error : "MausBot could not take that message";
      return finish(turn, "failed", message);
    }
    if ("queued" in receipt) {
      turn.phase = "queued";
      turn.queueId = receipt.queueId;
      turn.awaitStart = false;
      turn.boundTurnId = null;
      turn.replyRaw = "";
    } else if (receipt.steered) {
      turn.phase = "steered";
      turn.userMessageId = receipt.message.id;
      turn.awaitStart = false;
      turn.boundTurnId = ctx.directory.runningTurn(turn.threadId) ?? null;
      turn.steerAny = turn.boundTurnId === null;
    } else {
      turn.phase = "running";
      turn.userMessageId = receipt.message.id;
    }
    if (turn.stopRequested) {
      // The gadget already heard `done stopped`; finish the stop here.
      if (turn.phase === "queued") {
        void ctx.harness.json("DELETE", `/api/bots/${turn.botId}/queue/${turn.queueId}`, ctx.deviceId, { threadId: turn.threadId })
          .catch((error: Error) => ctx.log(`stop ${turn.id}: ${error.message}`));
      } else if (turn.phase === "running") {
        void ctx.harness.json("POST", `/api/bots/${turn.botId}/interrupt`, ctx.deviceId, { threadId: turn.threadId })
          .catch((error: Error) => ctx.log(`stop ${turn.id}: ${error.message}`));
      }
      return;
    }
    if (turn.phase === "queued" && !turn.ended) {
      turn.workingShown = true;
      ctx.send({ op: "working", turn: turn.id, text: fold(QUEUED_COPY) });
    }
  };

  const start = (id: string, phase: Phase): Turn => {
    // A new turn while one is in flight stops the old one first: speak.stop
    // and its done go out before anything of the new turn. With none in
    // flight, the last reply's speech (or a spoken post) may still be
    // playing: it stops too, so the gadget never records over its own
    // speaker (spec §6.2 Speech item 6; v1 has no echo cancellation).
    if (current) stopCurrent();
    else ctx.speech.stop();
    const turn = newTurn(id, phase);
    current = turn;
    return turn;
  };

  const onMessage = (turn: Turn, threadId: string, message: WireMessageLite) => {
    if (threadId !== turn.threadId) return;
    if (message.role === "user") {
      // The gadget's own words: by send id, or by queue id once a queued
      // send is drained or steered (spec §6.2 Busy bot).
      const mine = (turn.sendId !== undefined && message.sendId === turn.sendId) ||
        (turn.queueId !== undefined && message.queueId === turn.queueId);
      if (!mine) return;
      turn.userMessageId = message.id;
      if (turn.phase !== "queued") return;
      if (message.steered === true) {
        // The person pressed Steer on the desktop (POST …/queue/:queueId/steer):
        // the words joined the running turn and start none of their own, so
        // that turn's terminal reply is the gadget's answer.
        turn.phase = "steered";
        turn.awaitStart = false;
        turn.boundTurnId = ctx.directory.runningTurn(threadId) ?? null;
        turn.steerAny = turn.boundTurnId === null;
      } else {
        // Drained: the queued words now start their own turn.
        turn.phase = "running";
        turn.awaitStart = turn.boundTurnId === null;
      }
      if (turn.workingShown) ctx.send({ op: "working", turn: turn.id, text: "" });
      turn.workingShown = false;
      return;
    }
    const ours = (message.turnId !== undefined && message.turnId === turn.boundTurnId) ||
      (turn.userMessageId !== undefined && message.requestMessageId === turn.userMessageId) ||
      (turn.steerAny && turn.boundTurnId === null);
    if (!ours) return;
    if (turn.boundTurnId === null && message.turnId && turn.phase !== "queued") bind(turn, message.turnId);
    if (message.kind === "activity" && message.tool?.spoken) {
      turn.workingShown = true;
      ctx.send({ op: "working", turn: turn.id, text: clampChars(fold(message.tool.spoken), 80) });
      return;
    }
    if (message.kind === "text" && message.turnTerminal === true) {
      // sendFinal also ends a turn that already completed, once its final reply is out.
      sendFinal(turn, message.text ?? turn.replyRaw, message.id);
    }
  };

  return {
    say(msg) {
      const turn = start(msg.turn, "posting");
      const text = msg.text.trim();
      if (!text) return finish(turn, "failed", "Nothing to send");
      if (Array.from(text).length > SAY_MAX_CHARS) return finish(turn, "failed", "That message is too long");
      void post(turn, text);
    },
    voiceBegin(msg) {
      const turn = start(msg.turn, "recording");
      if (msg.rate !== 16_000) return finish(turn, "failed", "Unsupported mic rate");
      turn.micStream = msg.stream;
    },
    voiceAudio(stream, pcm) {
      const turn = current;
      if (!turn || turn.phase !== "recording" || turn.micStream !== stream) return;
      if (turn.pcmBytes + pcm.length > MAX_UTTERANCE_BYTES) return finish(turn, "failed", "That was longer than 60 seconds");
      turn.pcm.push(Buffer.from(pcm));
      turn.pcmBytes += pcm.length;
    },
    voiceEnd(msg) {
      const turn = current;
      if (!turn || turn.id !== msg.turn || turn.phase !== "recording") return;
      turn.phase = "transcribing";
      const pcm = Buffer.concat(turn.pcm, turn.pcmBytes);
      turn.pcm = [];
      void ctx.stt({ deviceId: ctx.deviceId, pcm, rate: 16_000 }).then(
        (outcome) => {
          if (turn.ended) return;
          if (!outcome.ok) return finish(turn, "failed", outcome.reason);
          const text = outcome.text.trim();
          if (!text) return finish(turn, "failed", "Didn't catch that");
          ctx.send({ op: "heard", turn: turn.id, text: clampChars(fold(text), 400) });
          void post(turn, text);
        },
        (error: Error) => {
          ctx.log(`stt ${turn.id}: ${error.message}`);
          finish(turn, "failed", "Speech-to-text failed");
        },
      );
    },
    voiceDrop(msg) {
      const turn = current;
      if (!turn || turn.id !== msg.turn || (turn.phase !== "recording" && turn.phase !== "transcribing")) return;
      finish(turn, "stopped");
    },
    stop(msg) {
      if (!current || (msg.turn !== undefined && msg.turn !== current.id)) return;
      stopCurrent();
    },
    onFrame(frame) {
      const turn = current;
      if (!turn || turn.ended || !turn.threadId) return;
      if (frame.kind === "message" || frame.kind === "message.patch") {
        if (frame.message) onMessage(turn, frame.threadId, frame.message);
        return;
      }
      if (frame.kind !== "runtime" || frame.event?.threadId !== turn.threadId || !frame.event.turnId) return;
      const event = frame.event;
      const turnId = event.turnId!;
      if (event.type === "turn.started") {
        if (turn.awaitStart && turn.boundTurnId === null && turn.phase !== "queued") bind(turn, turnId);
        return;
      }
      // Remember every turn on the pinned thread, so a late bind catches up.
      const seen = note(turn, turnId);
      const delta = event.type === "content.delta" && event.streamKind === "assistant_text" && typeof event.delta === "string" ? event.delta : null;
      if (delta !== null) seen.text += delta;
      if (event.type === "runtime.error" && typeof event.message === "string") seen.error = clampChars(fold(event.message), 120);
      if (event.type === "turn.completed") seen.completed = { ok: event.ok === true, stopReason: event.stopReason ?? null };
      if (turn.boundTurnId === null) {
        // Steered into a turn the hub never saw start: the next turn on the
        // thread is the one the gadget's words joined.
        if (turn.steerAny && (delta !== null || event.type === "turn.completed")) bind(turn, turnId);
        return;
      }
      if (turnId !== turn.boundTurnId) return;
      if (delta !== null) {
        turn.replyRaw += delta;
        scheduleReply(turn);
      } else if (event.type === "runtime.error" && seen.error) {
        turn.lastError = seen.error;
      } else if (event.type === "turn.completed" && !turn.completed) {
        complete(turn, outcomeOf(seen.completed!));
      }
    },
    ownsMessage: (messageId) => consumed.has(messageId),
    harnessRestarted() {
      const turn = current;
      if (!turn || turn.phase === "recording" || turn.phase === "transcribing") return;
      ctx.speech.stop();
      finish(turn, "failed", "MausBot restarted. Try again.");
    },
    close() {
      const turn = current;
      current = null;
      if (!turn) return;
      // The socket is gone: nothing more is sent. A message that already
      // reached the bot keeps running and is simply unbound (spec §4.4).
      turn.ended = true;
      turn.pcm = [];
      clearTimers(turn);
    },
  };
}
````

- [ ] **Step 7: Write `companion/src/gadget/session.ts`**

````ts
// One live gadget connection after `ready` (spec §6.2): gadget ops become
// harness calls, harness events become gadget ops. The turn rules live in
// turns.ts; this file owns the connection, its streams and the seams the
// voice, tools and OTA plans plug into.
import { Buffer } from "node:buffer";
import { randomBytes } from "node:crypto";

import type { DeviceRegistry, GadgetDeviceRecord } from "../devices.ts";
import type { BotDirectory } from "./directory.ts";
import type { NormalizedHello } from "./enroll.ts";
import type { HarnessClient } from "./harness-client.ts";
import {
  BinaryKind,
  decodeBinary,
  encodeBinary,
  encodeHostMessage,
  parseGadgetMessage,
  type BinaryKindValue,
  type BotRef,
  type GadgetOp,
  type GadgetSensors,
  type GadgetToHost,
  type HostToGadget,
  type PostMsg,
} from "./protocol.ts";
import { clampChars, screenText } from "./shape.ts";
import { createTurnEngine, type TurnEngine } from "./turns.ts";
import type { ServerFrameLite } from "./types.ts";
import type { WsConnection } from "./ws.ts";

export interface GadgetSessionHandle {
  readonly deviceId: string;
  readonly sessionId: string;
  readonly hello: NormalizedHello;
  readonly connectedAt: number;
  readonly closed: boolean;
  send(msg: HostToGadget): boolean;
  sendBinary(kind: BinaryKindValue, stream: number, payload: Uint8Array): Promise<boolean>;
  allocStream(): number;
  releaseStream(stream: number): void;
  on<K extends GadgetOp>(op: K, listener: (msg: Extract<GadgetToHost, { op: K }>) => void): () => void;
  onClose(listener: (code: number) => void): () => void;
}

// ---- voice seams: P3a ships the defaults, P3b supplies the real ones -------
export type SttOutcome =
  | { ok: true; text: string; provider: "apple" | "elevenlabs" }
  | { ok: false; reason: string };
export type SttFn = (input: { deviceId: string; pcm: Buffer; rate: 16000 }) => Promise<SttOutcome>;

export interface SpeechOut {
  replyFinal(input: { turn: string; rawText: string; voiceId?: string }): void;
  post(input: { rawText: string; voiceId?: string }): void;
  stop(): void;
  close(): void;
}
export interface SpeechContext {
  session: GadgetSessionHandle;
  rate: 16000 | 24000 | null;
  harness: HarnessClient;
  log?: (line: string) => void;
}
export type SpeechOutFactory = (context: SpeechContext) => SpeechOut;
export type VoiceProvider = (harness: HarnessClient) => { stt: SttFn; speech: SpeechOutFactory };

/** The copy a voice turn ends with until speech-to-text exists (spec §6.3). */
const STT_UNAVAILABLE =
  "Speech-to-text isn't set up. On a Mac, allow Speech Recognition for OpenMausBot (try dictation once), or add an ElevenLabs key in a bot's voice settings.";

/** P3a default: every voice turn ends with the stt_unavailable copy; replies are text only. */
export const textOnlyVoice: VoiceProvider = () => ({
  stt: async () => ({ ok: false, reason: STT_UNAVAILABLE }),
  speech: () => ({ replyFinal() {}, post() {}, stop() {}, close() {} }),
});

export interface GadgetSessionOptions {
  conn: WsConnection;
  hello: NormalizedHello;
  devices: DeviceRegistry;
  directory: BotDirectory;
  harness: HarnessClient;
  voice: { stt: SttFn; speech: SpeechOutFactory };
  recordEvent(deviceId: string, name: string, data: unknown): void;
  log(line: string): void;
  now(): number;
  terminalGraceMs: number;
  replyIntervalMs?: number;
}

/** The handle plus what only the hub calls. */
export interface GadgetSession extends GadgetSessionHandle {
  start(input: { device: GadgetDeviceRecord; sendName?: string }): Promise<void>;
  onText(text: string): void;
  onBinary(data: Buffer): void;
  /** The hub routes the socket's close here (the handshake attached the handlers). */
  handleClose(code: number): void;
  onFrame(frame: ServerFrameLite): void;
  /** The gadget's record changed on the desktop: send `settings`. */
  settingsChanged(device: GadgetDeviceRecord, nameChanged: boolean): void;
  /** A push for this gadget's bot (push.ts decides which). */
  push(post: Omit<PostMsg, "op" | "speak">, rawText: string): void;
  ownsMessage(messageId: string): boolean;
  harnessRestarted(): void;
  revoke(): void;
  replace(): void;
  shutdown(): void;
  terminate(): void;
}

const fold = (text: string) => screenText(text, { markdown: false });

export function createGadgetSession(options: GadgetSessionOptions): GadgetSession {
  const { conn, hello, devices, directory, harness } = options;
  const deviceId = hello.id;
  const sessionId = `s_${randomBytes(6).toString("hex")}`;
  const connectedAt = options.now();
  const listeners = new Map<GadgetOp, Set<(msg: GadgetToHost) => void>>();
  const closeListeners = new Set<(code: number) => void>();
  const streamsInUse = new Set<number>();
  let nextStream = 1;
  let closed = false;
  let botId: string | null = null;

  const send = (msg: HostToGadget): boolean => !closed && conn.sendText(encodeHostMessage(msg));

  const botRef = (id: string | null): BotRef => {
    if (!id) return { id: "", name: "" };
    return { id, name: clampChars(fold(directory.bot(id)?.name ?? ""), 64) };
  };

  const handle: GadgetSessionHandle = {
    deviceId,
    sessionId,
    hello,
    connectedAt,
    get closed() {
      return closed || conn.closed;
    },
    send,
    sendBinary: (kind, stream, payload) =>
      closed ? Promise.resolve(false) : conn.sendBinaryDrained(encodeBinary(kind, stream, payload)),
    allocStream() {
      for (let i = 0; i < 255; i++) {
        const candidate = nextStream;
        nextStream = nextStream === 255 ? 1 : nextStream + 1;
        if (!streamsInUse.has(candidate)) {
          streamsInUse.add(candidate);
          return candidate;
        }
      }
      throw new Error("no free stream id");
    },
    releaseStream: (stream) => void streamsInUse.delete(stream),
    on(op, listener) {
      const set = listeners.get(op) ?? new Set();
      set.add(listener as (msg: GadgetToHost) => void);
      listeners.set(op, set);
      return () => void set.delete(listener as (msg: GadgetToHost) => void);
    },
    onClose(listener) {
      closeListeners.add(listener);
      return () => void closeListeners.delete(listener);
    },
  };

  const rate = hello.caps.speaker?.rate === 16_000 || hello.caps.speaker?.rate === 24_000 ? hello.caps.speaker.rate : null;
  const speech = options.voice.speech({ session: handle, rate, harness, log: options.log });
  const turns: TurnEngine = createTurnEngine({
    deviceId,
    sessionId,
    send,
    harness,
    devices,
    directory,
    stt: options.voice.stt,
    speech,
    log: options.log,
    now: options.now,
    terminalGraceMs: options.terminalGraceMs,
    ...(options.replyIntervalMs !== undefined ? { replyIntervalMs: options.replyIntervalMs } : {}),
  });

  const sensorsOf = (msg: GadgetToHost): GadgetSensors => {
    const { op: _op, ...rest } = msg as Record<string, unknown>;
    return rest as GadgetSensors;
  };

  const session: GadgetSession = {
    ...handle,
    get closed() {
      return closed || conn.closed;
    },
    async start({ device, sendName }) {
      botId = device.botId;
      if (botId && !directory.bot(botId)) {
        try {
          await directory.refresh();
        } catch {
          /* the name is a label; the next bot frame fills it in */
        }
      }
      const bot = botRef(botId);
      const settings = { speak_pushes: device.speakPushes };
      send({ op: "ready", session: sessionId, bot, settings });
      if (sendName !== undefined) send({ op: "settings", bot, settings, name: fold(sendName) });
    },
    onText(text) {
      const msg = parseGadgetMessage(text);
      if (!msg) return; // receivers ignore unknown ops (spec §4.1)
      switch (msg.op) {
        case "say": turns.say(msg); break;
        case "voice.begin": turns.voiceBegin(msg); break;
        case "voice.end": turns.voiceEnd(msg); break;
        case "voice.drop": turns.voiceDrop(msg); break;
        case "stop": turns.stop(msg); break;
        case "sense": devices.touchGadget(deviceId, sensorsOf(msg)); break;
        case "event": options.recordEvent(deviceId, msg.name, msg.data); break;
        default:
      }
      for (const listener of listeners.get(msg.op) ?? []) {
        try {
          listener(msg);
        } catch (error) {
          options.log(`listener for ${msg.op}: ${(error as Error).message}`);
        }
      }
    },
    onBinary(data) {
      const frame = decodeBinary(data);
      if (frame?.kind === BinaryKind.mic) turns.voiceAudio(frame.stream, frame.payload);
    },
    handleClose(code) {
      if (closed) return;
      closed = true;
      turns.close();
      speech.close();
      for (const listener of closeListeners) listener(code);
    },
    onFrame(frame) {
      turns.onFrame(frame);
    },
    settingsChanged(device, nameChanged) {
      botId = device.botId;
      const bot = botRef(botId);
      send({ op: "settings", bot, settings: { speak_pushes: device.speakPushes }, ...(nameChanged ? { name: fold(device.name) } : {}) });
    },
    push(post, rawText) {
      const device = devices.gadget(deviceId);
      if (!device) return;
      const speak = device.speakPushes;
      send({ ...post, op: "post", speak });
      const voiceId = directory.bot(post.bot.id)?.voice;
      if (speak) speech.post({ rawText, ...(voiceId ? { voiceId } : {}) });
    },
    ownsMessage: (messageId) => turns.ownsMessage(messageId),
    harnessRestarted() {
      turns.harnessRestarted();
    },
    revoke() {
      if (closed) return;
      send({ op: "error", code: "revoked", message: "This gadget was removed in MausBot → Settings → Remote access" });
      conn.close(1000, "revoked");
    },
    replace() {
      if (closed) return;
      send({ op: "error", code: "replaced", message: "Another connection with this gadget's id took over" });
      conn.close(1000, "replaced");
    },
    shutdown() {
      if (!closed) conn.close(1001, "MausBot is stopping");
    },
    terminate() {
      conn.terminate();
    },
  };
  return session;
}
````

- [ ] **Step 8: Write `companion/src/gadget/hub.ts`**

````ts
// The gadget hub: `/gadget` on the companion's LAN listener (spec §6.1).
//
// It is attached only to the 0.0.0.0:8810 server's upgrade listener, before
// the viewer relay, and never to the managed (hosted HTTPS) origin, so a
// gadget is reachable on the home network and nowhere else. One session per
// gadget id; a new connection replaces an old one only after its proof
// verifies. Ready sessions register with the connected-device tracker, which
// gives the desktop its online dot and lets Remove close the socket.
import type { IncomingMessage } from "node:http";
import type { Duplex } from "node:stream";

import type { DeviceRegistry, GadgetDeviceRecord } from "../devices.ts";
import { createBotDirectory } from "./directory.ts";
import { checkHello, completeProve, createChallenge, type NormalizedHello } from "./enroll.ts";
import { createHarnessClient } from "./harness-client.ts";
import {
  BINARY_FRAME_MAX,
  encodeHostMessage,
  GADGET_ID_RE,
  GADGET_PATH,
  GADGET_SUBPROTOCOL,
  HANDSHAKE_TIMEOUT_MS,
  parseGadgetMessage,
  TEXT_FRAME_MAX,
  type ChallengeMsg,
  type GadgetErrorCode,
} from "./protocol.ts";
import { createGadgetSession, textOnlyVoice, type GadgetSession, type GadgetSessionHandle, type VoiceProvider } from "./session.ts";
import { foldLatin1, screenText } from "./shape.ts";
import { defaultGadgetBot, type ServerFrameLite } from "./types.ts";
import { acceptUpgrade, type WsConnection } from "./ws.ts";

export interface GadgetEventRecord { name: string; data?: unknown; at: number }

export interface GadgetHubOptions {
  devices: DeviceRegistry;
  harnessPort: number;
  mutationToken?: () => string | null;
  hostId: string;
  hostName: () => string;
  connected: (deviceId: string, terminate: () => void) => () => void;
  voice?: VoiceProvider;
  onDevicesChanged?: (change: { kind: "enrolled" | "removed"; deviceId: string }) => void;
  log?: (line: string) => void;
  now?: () => number;
  /** Test seams (P3a only): shorter timers. */
  timing?: { handshakeMs?: number; terminalGraceMs?: number; replyIntervalMs?: number; closeGraceMs?: number };
}

export interface GadgetHub {
  isGadgetPath(rawUrl: string | undefined): boolean;
  handleUpgrade(req: IncomingMessage, socket: Duplex, head: Buffer): void;
  session(deviceId: string): GadgetSessionHandle | null;
  online(): string[];
  onSessionReady(listener: (session: GadgetSessionHandle) => void): () => void;
  settingsChanged(deviceId: string, change: { nameChanged: boolean }): void;
  revoke(deviceId: string): void;
  recentEvents(deviceId: string): GadgetEventRecord[];
  botName(botId: string): string | null;
  close(): Promise<void>;
}

const MAX_PENDING_HANDSHAKES = 16;
const RECENT_EVENTS = 10;
const HOST_NAME_MAX_BYTES = 64;

/** Cut to `max` UTF-8 bytes on a code-point boundary, keeping the start. */
function cutBytes(text: string, max: number): string {
  let out = "";
  let size = 0;
  for (const ch of text) {
    const bytes = Buffer.byteLength(ch);
    if (size + bytes > max) break;
    out += ch;
    size += bytes;
  }
  return out;
}

export function createGadgetHub(options: GadgetHubOptions): GadgetHub {
  const log = options.log ?? ((line: string) => console.log(`gadget: ${line}`));
  const now = options.now ?? Date.now;
  const timing = options.timing ?? {};
  const harness = createHarnessClient({ harnessPort: options.harnessPort, ...(options.mutationToken ? { mutationToken: options.mutationToken } : {}) });
  const voice = (options.voice ?? textOnlyVoice)(harness);
  const directory = createBotDirectory(harness);
  const sessions = new Map<string, GadgetSession>();
  const pending = new Set<WsConnection>();
  /** Every accepted socket, handshaking or ready, until it closes. */
  const live = new Set<WsConnection>();
  const closeWaiters = new Set<() => void>();
  const events = new Map<string, Array<{ name: string; data?: unknown; at: number }>>();
  const readyListeners = new Set<(session: GadgetSessionHandle) => void>();
  const removed = new Set<string>();
  let stream: { close(): void } | null = null;
  let streamHellos = 0;
  let closing = false;

  const onFrame = (frame: ServerFrameLite) => {
    directory.apply(frame);
    for (const session of sessions.values()) session.onFrame(frame);
  };

  const onHello = (resumed: boolean) => {
    streamHellos += 1;
    const first = streamHellos === 1;
    if (resumed) return;
    // The first hello hydrates; a later one that cannot resume means the
    // harness restarted under us, and every in-flight turn went with it.
    void directory.refresh().then(
      () => {
        if (!first) for (const session of sessions.values()) session.harnessRestarted();
      },
      (error: Error) => log(`could not read the bots: ${error.message}`),
    );
  };

  const ensureStream = () => {
    if (stream || closing) return;
    streamHellos = 0;
    stream = harness.events({ onFrame, onHello, onDown: (error) => log(`event stream: ${error.message}`) });
  };
  const dropStreamIfIdle = () => {
    if (sessions.size || !stream) return;
    stream.close();
    stream = null;
  };

  const resolveBot = async (): Promise<string | null> => {
    await directory.refresh();
    return defaultGadgetBot(directory.visibleBots())?.id ?? null;
  };

  const sendError = (conn: WsConnection, code: GadgetErrorCode, message: string) => {
    conn.sendText(encodeHostMessage({ op: "error", code, message: foldLatin1(message) }));
    conn.close(1000, code);
  };

  const hostName = () => cutBytes(screenText(options.hostName(), { markdown: false }), HOST_NAME_MAX_BYTES);

  const recordEvent = (deviceId: string, name: string, data: unknown) => {
    const list = events.get(deviceId) ?? [];
    list.push({ name, ...(data !== undefined ? { data } : {}), at: now() });
    if (list.length > RECENT_EVENTS) list.splice(0, list.length - RECENT_EVENTS);
    events.set(deviceId, list);
  };

  /** hello → challenge → prove → ready, within the handshake deadline. */
  const handshake = (conn: WsConnection) => {
    let phase: "hello" | "prove" | "proving" | "ready" | "failed" = "hello";
    let hello: NormalizedHello | null = null;
    let challenge: ChallengeMsg | null = null;
    let session: GadgetSession | null = null;
    const deadline = setTimeout(() => {
      if (phase !== "ready") conn.close(1008, "handshake timeout");
    }, timing.handshakeMs ?? HANDSHAKE_TIMEOUT_MS);
    deadline.unref?.();

    const becomeReady = async (result: { device: GadgetDeviceRecord; enrolled: boolean; sendName?: string }) => {
      const id = hello!.id;
      clearTimeout(deadline);
      pending.delete(conn);
      if (result.enrolled) {
        removed.delete(id);
        options.onDevicesChanged?.({ kind: "enrolled", deviceId: id });
      }
      // Only now may a new connection take over: its proof verified.
      sessions.get(id)?.replace();
      const created = createGadgetSession({
        conn,
        hello: hello!,
        devices: options.devices,
        directory,
        harness,
        voice,
        recordEvent,
        log: (line) => log(`${id} ${line}`),
        now,
        terminalGraceMs: timing.terminalGraceMs ?? 1_000,
        ...(timing.replyIntervalMs !== undefined ? { replyIntervalMs: timing.replyIntervalMs } : {}),
      });
      session = created;
      sessions.set(id, created);
      phase = "ready";
      const dispose = options.connected(id, () => created.revoke());
      created.onClose(() => {
        dispose();
        if (sessions.get(id) === created) sessions.delete(id);
        dropStreamIfIdle();
      });
      ensureStream();
      options.devices.touchGadget(id, hello!.sensors);
      await created.start(result);
      for (const listener of readyListeners) {
        try {
          listener(created);
        } catch (error) {
          log(`ready listener: ${(error as Error).message}`);
        }
      }
    };

    conn.attach({
      onText(text) {
        if (phase === "ready") return session?.onText(text);
        const msg = parseGadgetMessage(text);
        if (!msg) return;
        if (phase === "hello" && msg.op === "hello") {
          const check = checkHello(msg);
          if (!check.ok) {
            phase = "failed";
            return sendError(conn, check.code, check.message);
          }
          hello = check.hello;
          challenge = createChallenge(options.hostId, hostName());
          phase = "prove";
          conn.sendText(encodeHostMessage(challenge));
          return;
        }
        if (phase === "prove" && msg.op === "prove") {
          phase = "proving";
          void completeProve({ hello: hello!, challenge: challenge!, prove: msg, registry: options.devices, resolveBot }).then(
            async (result) => {
              if (conn.closed) return;
              if (!result.ok) {
                phase = "failed";
                if (result.code === "internal") {
                  log(`${hello!.id} enrollment could not be saved: ${result.message}`);
                  conn.close(1011, "internal error");
                } else {
                  sendError(conn, result.code, result.message);
                }
                return;
              }
              await becomeReady(result);
            },
            (error: Error) => {
              log(`prove failed: ${error.message}`);
              conn.close(1011, "internal error");
            },
          );
        }
      },
      onBinary(data) {
        if (phase === "ready") session?.onBinary(data);
      },
      onClose(code) {
        clearTimeout(deadline);
        pending.delete(conn);
        live.delete(conn);
        session?.handleClose(code);
        for (const waiter of Array.from(closeWaiters)) waiter();
      },
    });
  };

  const hub: GadgetHub = {
    isGadgetPath(rawUrl) {
      try {
        return new URL(rawUrl ?? "/", "http://companion.invalid").pathname === GADGET_PATH;
      } catch {
        return false;
      }
    },
    handleUpgrade(req, socket, head) {
      if (closing || pending.size >= MAX_PENDING_HANDSHAKES) {
        socket.end("HTTP/1.1 503 Service Unavailable\r\nConnection: close\r\nContent-Length: 0\r\n\r\n");
        socket.destroy();
        return;
      }
      const conn = acceptUpgrade(req, socket, head, { subprotocol: GADGET_SUBPROTOCOL, maxText: TEXT_FRAME_MAX, maxBinary: BINARY_FRAME_MAX });
      if (!conn) return;
      pending.add(conn);
      live.add(conn);
      handshake(conn);
    },
    session: (deviceId) => sessions.get(deviceId) ?? null,
    online: () => [...sessions.keys()],
    onSessionReady(listener) {
      readyListeners.add(listener);
      return () => void readyListeners.delete(listener);
    },
    settingsChanged(deviceId, change) {
      const device = options.devices.gadget(deviceId);
      if (!device) return;
      const current = sessions.get(deviceId);
      if (current && !current.closed) {
        current.settingsChanged(device, change.nameChanged);
        return;
      }
      if (change.nameChanged) {
        try {
          options.devices.updateGadget(deviceId, { namePending: true });
        } catch (error) {
          log(`${deviceId} rename could not be marked pending: ${(error as Error).message}`);
        }
      }
    },
    revoke(deviceId) {
      sessions.get(deviceId)?.revoke();
      if (!GADGET_ID_RE.test(deviceId) || removed.has(deviceId)) return;
      removed.add(deviceId);
      events.delete(deviceId);
      options.onDevicesChanged?.({ kind: "removed", deviceId });
    },
    recentEvents: (deviceId) => (events.get(deviceId) ?? []).map((event) => ({ ...event })),
    botName: (botId) => directory.bot(botId)?.name ?? null,
    async close() {
      closing = true;
      stream?.close();
      stream = null;
      for (const session of sessions.values()) session.shutdown();
      for (const conn of pending) conn.close(1001, "MausBot is stopping");
      if (!live.size) return;
      // Upgraded sockets keep server.close() pending forever, so the
      // companion's shutdown waits here: a close frame first, then the cut.
      await new Promise<void>((resolve) => {
        const timer = setTimeout(() => {
          for (const conn of Array.from(live)) conn.terminate();
          resolve();
        }, timing.closeGraceMs ?? 1_000);
        timer.unref?.();
        closeWaiters.add(() => {
          if (live.size) return;
          clearTimeout(timer);
          resolve();
        });
      });
    },
  };
  return hub;
}
````

- [ ] **Step 9: Run the tests (three times, real sockets), the whole gadget folder and the checks**

```bash
cd /Users/omkar/Desktop/openmaus/OpenGrokBot-gadget || exit 1
export PATH="$HOME/.nvm/versions/node/v24.14.1/bin:$PATH"
for i in 1 2 3; do pnpm exec vitest run companion/test/gadget/hub.test.ts companion/test/gadget/hub-turns.test.ts; done
pnpm exec vitest run companion/test/gadget
pnpm exec tsc -p tsconfig.server.json  # 40-150 s: give this Bash call a timeout of at least 10 minutes
pnpm exec oxlint --deny-warnings companion
```

Expected: `hub.test.ts` 16 passed and `hub-turns.test.ts` 24 passed each time; the folder run shows `Test Files  10 passed (10)` and `Tests  129 passed (129)`; no `tsc` output; oxlint exits 0.

- [ ] **Step 10: Commit**

```bash
cd /Users/omkar/Desktop/openmaus/OpenGrokBot-gadget || exit 1
export PATH="$HOME/.nvm/versions/node/v24.14.1/bin:$PATH"
git add companion/src/gadget/directory.ts companion/src/gadget/turns.ts companion/src/gadget/session.ts companion/src/gadget/hub.ts companion/test/gadget/helpers/gadget-client.ts companion/test/gadget/helpers/hub-rig.ts companion/test/gadget/hub.test.ts companion/test/gadget/hub-turns.test.ts && git commit -m "feat(companion): gadget hub with sessions, presence and conversation turns

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 10: Approvals and questions on a gadget

**Files:**
- Create: `companion/src/gadget/asks.ts`
- Modify: `companion/src/gadget/session.ts` (Task 9: import, queue creation, `answer` dispatch, `onFrame`, `start`, `settingsChanged`, `harnessRestarted`, `handleClose`)
- Test: `companion/test/gadget/hub-asks.test.ts`

**Interfaces:**
- Consumes: `gadgetAskKind`, `formatQuestionAnswers`, `shouldSettleRequestCard` (Task 2); `screenText`, `clampChars` (Task 3); `BotDirectory` (Task 9).
- Produces: `askIdFor(threadId, requestId): string` = `"a_"` + 16 hex of `sha256(threadId + ":" + requestId)` (contract §2.12 / §3.11); `AskQueue` (`onFrame`, `answer`, `rebuild`, `reset`, `close`) and `createAskQueue(ctx)`. Titles are cut to 80 characters, bodies to 600 and labels to 40, all folded. Option ids are `allow`/`deny` for permissions and `o0`–`o3` for question choices.

- [ ] **Step 1: Write the failing test**

Its `afterEach` asserts that no request ever went to `/always-allow`. Two more things to note:

- The no-button cases send a `say` after the two answers and wait for its `POST /messages`. A gadget's frames are handled in order, so that POST proves the answers were already handled, and that they sent nothing.
- The last test restarts the harness (`hello.resumed: false`). The open ask must close as `withdrawn` and come back after the hub re-reads the thread (spec §6.2 One SSE stream).

````ts
// Approvals and questions on a gadget (spec §4.5, §6.2 Asks, A9, D19).
import { afterEach, describe, expect, it } from "vitest";

import { askIdFor } from "../../src/gadget/asks.ts";
import type { OptionCardLite } from "../../src/gadget/types.ts";
import { startRig, type Rig } from "./helpers/hub-rig.ts";

let rig: Rig;
afterEach(async () => {
  // Whatever happened, a gadget never created a standing grant.
  expect(rig.harness.requests.some((r) => r.path.includes("always-allow"))).toBe(false);
  await rig?.close();
});

let at = 1_000;
const card = (requestId: string, extra: Partial<OptionCardLite>, threadId = "th1") => ({
  kind: "message", threadId,
  message: { id: `m_${requestId}`, role: "bot", kind: "options", at: (at += 1), card: { title: "Approval needed", subtitle: "Run `rm -rf build`", options: ["Allow", "Deny", "Always allow"], requestId, ...extra } },
});

async function ready() {
  rig = await startRig();
  const gadget = await rig.enroll();
  await rig.harness.waitForSubscriber();
  return gadget;
}

describe("permission cards", () => {
  it("offers exactly Allow and Deny and answers with allow", async () => {
    const gadget = await ready();
    rig.harness.emit(card("r1", { requestType: "permission", tool: "Bash" }));
    const ask = await gadget.next("ask");
    expect(ask).toEqual({
      op: "ask", id: askIdFor("th1", "r1"), kind: "permission", title: "Approval needed", body: "Run rm -rf build",
      options: [{ id: "allow", label: "Allow", style: "allow" }, { id: "deny", label: "Deny", style: "deny" }],
    });
    gadget.send({ op: "answer", id: ask.id, option: "allow" });
    expect((await rig.harness.waitFor("POST", /^\/api\/threads\/th1\/respond$/)).json).toEqual({ requestId: "r1", behavior: "allow" });
    expect(await gadget.next("ask.close")).toEqual({ op: "ask.close", id: ask.id, reason: "answered" });
  });

  it("treats the harness's peer-approval cards (no requestType) as permissions", async () => {
    const gadget = await ready();
    rig.harness.emit(card("r2", { tool: "gadget_action" }));
    expect(await gadget.next("ask")).toMatchObject({ kind: "permission" });
  });

  it("answers at most once", async () => {
    const gadget = await ready();
    rig.harness.emit(card("r3", { requestType: "permission", tool: "Bash" }));
    const ask = await gadget.next("ask");
    gadget.send({ op: "answer", id: ask.id, option: "deny" });
    gadget.send({ op: "answer", id: ask.id, option: "allow" });
    expect((await rig.harness.waitFor("POST", /\/respond$/)).json).toMatchObject({ behavior: "deny" });
    await gadget.next("ask.close");
    expect(rig.harness.requests.filter((r) => r.path.endsWith("/respond"))).toHaveLength(1);
  });
});

describe("question cards", () => {
  it("offers a plain card's choices and answers with the label", async () => {
    const gadget = await ready();
    rig.harness.emit(card("q1", { title: "Your bot has a question", subtitle: "Which one?", options: ["Red", "Blue"], requestType: "question" }));
    const ask = await gadget.next("ask");
    expect(ask).toMatchObject({ kind: "question", options: [{ id: "o0", label: "Red", style: "neutral" }, { id: "o1", label: "Blue", style: "neutral" }] });
    gadget.send({ op: "answer", id: ask.id, option: "o1" });
    expect((await rig.harness.waitFor("POST", /\/respond$/)).json).toEqual({ requestId: "q1", behavior: "answer", message: "Blue" });
  });

  it("offers one single-select question and answers in the app's format", async () => {
    const gadget = await ready();
    const question = { question: "Which model?", options: [{ label: "Opus" }, { label: "Sonnet" }] };
    rig.harness.emit(card("q2", { requestType: "question", options: [], questionRequest: { version: 1, questions: [question] } }));
    const ask = await gadget.next("ask");
    expect(ask).toMatchObject({ body: "Which model?", options: [{ label: "Opus" }, { label: "Sonnet" }] });
    gadget.send({ op: "answer", id: ask.id, option: "o0" });
    expect((await rig.harness.waitFor("POST", /\/respond$/)).json).toEqual({
      requestId: "q2", behavior: "answer", message: "The user answered your questions.\n\nQ: Which model?\nA: Opus",
    });
  });

  it.each([
    ["two questions", { requestType: "question" as const, questionRequest: { version: 1 as const, questions: [{ question: "a", options: [{ label: "x" }] }, { question: "b", options: [{ label: "y" }] }] } }],
    ["a multi-select question", { requestType: "question" as const, questionRequest: { version: 1 as const, questions: [{ question: "a", multiSelect: true, options: [{ label: "x" }] }] } }],
    ["five choices", { requestType: "question" as const, options: ["1", "2", "3", "4", "5"] }],
    ["a free-text question", { requestType: "question" as const, options: [] }],
    ["a routine proposal", { routineRequest: {}, tool: "propose_routine" }],
  ])("shows %s as Answer on your computer or phone, with no buttons", async (_label, extra) => {
    const gadget = await ready();
    rig.harness.emit(card("u1", extra));
    const ask = await gadget.next("ask");
    expect(ask).toMatchObject({ kind: "question", options: [] });
    gadget.send({ op: "answer", id: ask.id, option: "o0" });
    gadget.send({ op: "answer", id: ask.id, option: "allow" });
    // A barrier: a gadget's frames are handled in order, so once this say's
    // POST arrives, the two answers above were handled (and sent nothing).
    gadget.send({ op: "say", turn: "t1-1", text: "barrier" });
    await rig.harness.waitFor("POST", /\/messages$/);
    expect(rig.harness.requests.some((r) => r.path.endsWith("/respond"))).toBe(false);
  });
});

describe("queue and closing", () => {
  it("sends one ask at a time, oldest first", async () => {
    const gadget = await ready();
    rig.harness.emit(card("a1", { requestType: "permission", tool: "Bash" }));
    rig.harness.emit(card("a2", { requestType: "permission", tool: "Read" }));
    const first = await gadget.next("ask");
    expect(first.id).toBe(askIdFor("th1", "a1"));
    rig.harness.emit({ kind: "runtime", event: { type: "request.resolved", threadId: "th1", requestId: "a1", behavior: "allow", source: "user" } });
    expect(await gadget.next("ask.close")).toEqual({ op: "ask.close", id: first.id, reason: "answered" });
    expect((await gadget.next("ask")).id).toBe(askIdFor("th1", "a2"));
  });

  it("closes as expired on a timeout and as withdrawn when dismissed", async () => {
    const gadget = await ready();
    rig.harness.emit(card("e1", { requestType: "permission", tool: "Bash" }));
    const ask = await gadget.next("ask");
    rig.harness.emit({ kind: "runtime", event: { type: "request.resolved", threadId: "th1", requestId: "e1", behavior: "deny", source: "timeout" } });
    expect(await gadget.next("ask.close")).toMatchObject({ id: ask.id, reason: "expired" });
    rig.harness.emit(card("e2", { requestType: "permission", tool: "Bash" }));
    const second = await gadget.next("ask");
    const dismissed = card("e2", { requestType: "permission", tool: "Bash", answered: "deny", dismissed: true });
    rig.harness.emit({ ...dismissed, kind: "message.patch" });
    expect(await gadget.next("ask.close")).toMatchObject({ id: second.id, reason: "withdrawn" });
  });

  it("keeps a persistent question open when the turn ends without an answer", async () => {
    const gadget = await ready();
    rig.harness.emit(card("p1", { requestType: "question", options: ["Yes", "No"] }));
    const ask = await gadget.next("ask");
    rig.harness.emit({ kind: "runtime", event: { type: "request.resolved", threadId: "th1", requestId: "p1", behavior: "answer", source: "system" } });
    rig.harness.emit(card("p2", { requestType: "permission", tool: "Bash" }));
    gadget.send({ op: "answer", id: ask.id, option: "o0" });
    expect((await rig.harness.waitFor("POST", /\/respond$/)).json).toMatchObject({ requestId: "p1", message: "Yes" });
  });

  it("ignores cards in threads of other bots", async () => {
    rig = await startRig({}, [{ id: "b1", name: "Jev", threadId: "th1" }, { id: "b2", name: "Ops", threadId: "th2" }]);
    const gadget = await rig.enroll();
    await rig.harness.waitForSubscriber();
    rig.harness.emit(card("x1", { requestType: "permission", tool: "Bash" }, "th2"));
    rig.harness.emit(card("x2", { requestType: "permission", tool: "Bash" }, "th1"));
    expect((await gadget.next("ask")).id).toBe(askIdFor("th1", "x2"));
  });

  it("re-sends asks that are still open when the gadget reconnects", async () => {
    rig = await startRig({}, [{ id: "b1", name: "Jev", threadId: "th1", tasks: [{ threadId: "th1", title: "Main" }, { threadId: "th7", title: "Deploy", activity: "waiting-on-you" }] }]);
    const open = card("w1", { requestType: "permission", tool: "Bash" }, "th7");
    rig.harness.route("GET", /^\/api\/threads\/th7\/messages/, () => ({ status: 200, json: { messages: [open.message], hasMore: false, activeLeafId: null } }));
    const gadget = await rig.enroll();
    expect(await gadget.next("ask")).toMatchObject({ id: askIdFor("th7", "w1"), kind: "permission" });
    expect(rig.harness.requests.some((r) => r.path === "/api/threads/th1/messages?limit=50")).toBe(true);
  });

  it("withdraws open asks after a harness restart and reads them again", async () => {
    const gadget = await ready();
    const open = card("h1", { requestType: "permission", tool: "Bash" });
    rig.harness.emit(open);
    const ask = await gadget.next("ask");
    rig.harness.route("GET", /^\/api\/threads\/th1\/messages/, () => ({ status: 200, json: { messages: [open.message], hasMore: false, activeLeafId: null } }));
    const before = rig.harness.requests.length;
    // The stream comes back with hello.resumed: false (spec §6.2 One SSE stream).
    rig.harness.restart();
    expect(await gadget.next("ask.close", 10_000)).toEqual({ op: "ask.close", id: ask.id, reason: "withdrawn" });
    expect(await gadget.next("ask", 10_000)).toEqual(ask);
    expect(rig.harness.requests.slice(before).some((r) => r.method === "GET" && r.path === "/api/threads/th1/messages?limit=50")).toBe(true);
  });
});
````

- [ ] **Step 2: Run it to see it fail**

Run: `cd /Users/omkar/Desktop/openmaus/OpenGrokBot-gadget && PATH="$HOME/.nvm/versions/node/v24.14.1/bin:$PATH" pnpm exec vitest run companion/test/gadget/hub-asks.test.ts`
Expected: FAIL — `Cannot find module '../../src/gadget/asks.ts'`.

- [ ] **Step 3: Write `companion/src/gadget/asks.ts`**

````ts
// Approvals and questions on a gadget (spec §4.5, §6.2 Asks, A9, D19).
//
// The hub watches the threads of the gadget's bot for open option cards and
// sends them one at a time, oldest first. Permission cards get exactly Allow
// and Deny — never Always allow, so a desk device can never create a
// standing grant — and the hub never calls /always-allow. Question cards are
// offered only when they fit four buttons; anything else is shown as
// "Answer on your computer or phone".
import { createHash } from "node:crypto";

import type { DeviceRegistry } from "../devices.ts";
import type { BotDirectory } from "./directory.ts";
import type { HarnessClient } from "./harness-client.ts";
import type { AnswerMsg, AskCloseReason, AskMsg, AskOption, HostToGadget } from "./protocol.ts";
import { clampChars, screenText } from "./shape.ts";
import {
  formatQuestionAnswers,
  gadgetAskKind,
  shouldSettleRequestCard,
  type AskQuestionLite,
  type OptionCardLite,
  type ServerFrameLite,
  type WireMessageLite,
} from "./types.ts";

const MAX_OPTIONS = 4;
const TITLE_MAX = 80;
const BODY_MAX = 600;
const LABEL_MAX = 40;

interface AskEntry {
  askId: string;
  threadId: string;
  requestId: string;
  card: OptionCardLite;
  at: number;
  ask: AskMsg;
  labels: Map<string, string>;
  question?: AskQuestionLite;
  sent: boolean;
  answered: boolean;
}

export interface AskQueue {
  onFrame(frame: ServerFrameLite): void;
  answer(msg: AnswerMsg): void;
  /** Read open cards from the bot's selected thread and every thread waiting on the person. */
  rebuild(): Promise<void>;
  /** Close everything (bot changed, harness restarted) and read it again. */
  reset(): void;
  close(): void;
}

/** "a_" + 16 hex of sha256(threadId:requestId): engine ids never reach the gadget. */
export function askIdFor(threadId: string, requestId: string): string {
  return `a_${createHash("sha256").update(`${threadId}:${requestId}`).digest("hex").slice(0, 16)}`;
}

const isOpenCard = (message: WireMessageLite): boolean =>
  message.kind === "options" && Boolean(message.card?.requestId) &&
  !message.card?.answered && !message.card?.dismissed && !message.card?.expired;

const fold = (text: string, max: number, markdown = false) => clampChars(screenText(text, { markdown }), max);

/** The ask a card becomes on the gadget. */
function buildAsk(threadId: string, card: OptionCardLite): { ask: AskMsg; labels: Map<string, string>; question?: AskQuestionLite } {
  const id = askIdFor(threadId, card.requestId!);
  const title = fold(card.title || "MausBot", TITLE_MAX);
  const body = fold(card.subtitle ?? "", BODY_MAX, true);
  const labels = new Map<string, string>();
  const unsupported = (): { ask: AskMsg; labels: Map<string, string> } => ({
    ask: { op: "ask", id, kind: "question", title, body, options: [] },
    labels,
  });
  const kind = gadgetAskKind(card);
  if (kind === "unsupported") return unsupported();
  if (kind === "permission") {
    return {
      ask: {
        op: "ask", id, kind: "permission", title, body,
        options: [{ id: "allow", label: "Allow", style: "allow" }, { id: "deny", label: "Deny", style: "deny" }],
      },
      labels,
    };
  }
  let choices: string[];
  let question: AskQuestionLite | undefined;
  let questionBody = body;
  if (!card.questionRequest) {
    choices = card.options ?? [];
  } else {
    const questions = card.questionRequest.questions ?? [];
    if (questions.length !== 1 || questions[0]!.multiSelect) return unsupported();
    question = questions[0]!;
    choices = question.options.map((option) => option.label);
    questionBody = fold(question.question, BODY_MAX, true);
  }
  // More than four choices is unsupported: choices are never cut down.
  if (choices.length < 1 || choices.length > MAX_OPTIONS) return unsupported();
  const options: AskOption[] = choices.map((label, index) => {
    labels.set(`o${index}`, label);
    return { id: `o${index}`, label: fold(label, LABEL_MAX), style: "neutral" };
  });
  return { ask: { op: "ask", id, kind: "question", title, body: questionBody, options }, labels, ...(question ? { question } : {}) };
}

export function createAskQueue(ctx: {
  deviceId: string;
  send(msg: HostToGadget): boolean;
  harness: HarnessClient;
  devices: DeviceRegistry;
  directory: BotDirectory;
  log(line: string): void;
}): AskQueue {
  const entries = new Map<string, AskEntry>();
  let closed = false;

  const botId = () => ctx.devices.gadget(ctx.deviceId)?.botId ?? null;
  const ownsThread = (threadId: string) => {
    const bot = botId();
    return bot !== null && ctx.directory.botForThread(threadId) === bot;
  };

  /** Send the oldest open ask when none is showing. */
  const pump = () => {
    if (closed) return;
    const ordered = [...entries.values()].sort((a, b) => a.at - b.at);
    if (ordered.some((entry) => entry.sent)) return;
    const head = ordered[0];
    if (!head) return;
    head.sent = true;
    ctx.send(head.ask);
  };

  const closeEntry = (entry: AskEntry, reason: AskCloseReason) => {
    if (!entries.delete(entry.askId)) return;
    if (entry.sent && !closed) ctx.send({ op: "ask.close", id: entry.askId, reason });
    pump();
  };

  const upsert = (threadId: string, message: WireMessageLite) => {
    const card = message.card!;
    const askId = askIdFor(threadId, card.requestId!);
    if (entries.has(askId)) return;
    const built = buildAsk(threadId, card);
    entries.set(askId, {
      askId, threadId, requestId: card.requestId!, card, at: message.at ?? Date.now(),
      ask: built.ask, labels: built.labels, ...(built.question ? { question: built.question } : {}), sent: false, answered: false,
    });
    pump();
  };

  const entryFor = (threadId: string, requestId: string | undefined) =>
    requestId ? entries.get(askIdFor(threadId, requestId)) : undefined;

  const readThread = async (threadId: string): Promise<WireMessageLite[]> => {
    const response = await ctx.harness.json<{ messages?: WireMessageLite[] }>(
      "GET", `/api/threads/${threadId}/messages?limit=50`, ctx.deviceId,
    );
    if (response.status !== 200 || !Array.isArray(response.body.messages)) return [];
    ctx.directory.rememberMessages(threadId, response.body.messages);
    return response.body.messages;
  };

  const rescan = async (threadId: string) => {
    try {
      const messages = await readThread(threadId);
      if (closed || !ownsThread(threadId)) return;
      for (const message of messages) if (isOpenCard(message)) upsert(threadId, message);
    } catch (error) {
      ctx.log(`asks: could not read ${threadId}: ${(error as Error).message}`);
    }
  };

  const queue: AskQueue = {
    onFrame(frame) {
      if (closed) return;
      if (frame.kind === "message" || frame.kind === "message.patch") {
        const message = frame.message;
        if (!message?.card?.requestId || message.kind !== "options") return;
        const existing = entryFor(frame.threadId, message.card.requestId);
        if (isOpenCard(message)) {
          if (!existing && ownsThread(frame.threadId)) upsert(frame.threadId, message);
          return;
        }
        if (existing) {
          const card = message.card;
          closeEntry(existing, card.expired ? "expired" : card.answered && !card.dismissed ? "answered" : "withdrawn");
        }
        return;
      }
      if (frame.kind === "runtime" && frame.event?.type === "request.resolved") {
        const existing = entryFor(frame.event.threadId, frame.event.requestId);
        if (!existing || !shouldSettleRequestCard(existing.card, frame.event.source ?? "system")) return;
        const source = frame.event.source;
        closeEntry(existing, source === "user" ? "answered" : source === "timeout" ? "expired" : "withdrawn");
        return;
      }
      if (frame.kind === "notify" && (frame.notification?.kind === "approval" || frame.notification?.kind === "question")) {
        if (frame.notification.botId === botId()) void rescan(frame.notification.threadId);
      }
    },
    answer(msg) {
      const entry = entries.get(msg.id);
      // At most once, and only for the ask the gadget is showing.
      if (!entry || !entry.sent || entry.answered || entry.ask.options.length === 0) return;
      let body: { requestId: string; behavior: "allow" | "deny" | "answer"; message?: string };
      if (entry.ask.kind === "permission") {
        if (msg.option !== "allow" && msg.option !== "deny") return;
        body = { requestId: entry.requestId, behavior: msg.option };
      } else {
        const label = entry.labels.get(msg.option);
        if (label === undefined) return;
        const message = entry.question ? formatQuestionAnswers([entry.question], [[label]]) : label;
        body = { requestId: entry.requestId, behavior: "answer", message };
      }
      entry.answered = true;
      void ctx.harness.json<{ ok?: boolean; error?: string }>("POST", `/api/threads/${entry.threadId}/respond`, ctx.deviceId, body).then(
        (response) => {
          if (response.status < 200 || response.status >= 300) ctx.log(`asks: respond ${response.status} ${response.body.error ?? ""}`);
          closeEntry(entry, response.status >= 200 && response.status < 300 ? "answered" : "withdrawn");
        },
        (error: Error) => {
          ctx.log(`asks: respond failed: ${error.message}`);
          closeEntry(entry, "withdrawn");
        },
      );
    },
    async rebuild() {
      const id = botId();
      if (!id || closed) return;
      let bot = ctx.directory.bot(id);
      if (!bot) {
        try {
          await ctx.directory.refresh();
        } catch {
          return;
        }
        bot = ctx.directory.bot(id);
      }
      if (!bot) return;
      const threads = new Set([bot.threadId, ...(bot.tasks ?? []).filter((task) => task.activity === "waiting-on-you").map((task) => task.threadId)]);
      for (const threadId of threads) await rescan(threadId);
    },
    reset() {
      for (const entry of entries.values()) {
        if (entry.sent && !closed) ctx.send({ op: "ask.close", id: entry.askId, reason: "withdrawn" });
      }
      entries.clear();
      void queue.rebuild();
    },
    close() {
      closed = true;
      entries.clear();
    },
  };
  return queue;
}
````

- [ ] **Step 4: Wire the queue into the session**

````bash
cd /Users/omkar/Desktop/openmaus/OpenGrokBot-gadget || exit 1
export PATH="$HOME/.nvm/versions/node/v24.14.1/bin:$PATH"
git apply <<'PATCH'
--- a/companion/src/gadget/session.ts
+++ b/companion/src/gadget/session.ts
@@ -1,11 +1,12 @@
 // One live gadget connection after `ready` (spec §6.2): gadget ops become
 // harness calls, harness events become gadget ops. The turn rules live in
-// turns.ts; this file owns the connection, its streams and the seams the
-// voice, tools and OTA plans plug into.
+// turns.ts, the approval rules in asks.ts; this file owns the connection,
+// its streams and the seams the voice, tools and OTA plans plug into.
 import { Buffer } from "node:buffer";
 import { randomBytes } from "node:crypto";
 
 import type { DeviceRegistry, GadgetDeviceRecord } from "../devices.ts";
+import { createAskQueue } from "./asks.ts";
 import type { BotDirectory } from "./directory.ts";
 import type { NormalizedHello } from "./enroll.ts";
 import type { HarnessClient } from "./harness-client.ts";
@@ -179,6 +180,7 @@
     terminalGraceMs: options.terminalGraceMs,
     ...(options.replyIntervalMs !== undefined ? { replyIntervalMs: options.replyIntervalMs } : {}),
   });
+  const asks = createAskQueue({ deviceId, send, harness, devices, directory, log: options.log });
 
   const sensorsOf = (msg: GadgetToHost): GadgetSensors => {
     const { op: _op, ...rest } = msg as Record<string, unknown>;
@@ -203,6 +205,7 @@
       const settings = { speak_pushes: device.speakPushes };
       send({ op: "ready", session: sessionId, bot, settings });
       if (sendName !== undefined) send({ op: "settings", bot, settings, name: fold(sendName) });
+      void asks.rebuild();
     },
     onText(text) {
       const msg = parseGadgetMessage(text);
@@ -213,6 +216,7 @@
         case "voice.end": turns.voiceEnd(msg); break;
         case "voice.drop": turns.voiceDrop(msg); break;
         case "stop": turns.stop(msg); break;
+        case "answer": asks.answer(msg); break;
         case "sense": devices.touchGadget(deviceId, sensorsOf(msg)); break;
         case "event": options.recordEvent(deviceId, msg.name, msg.data); break;
         default:
@@ -233,16 +237,20 @@
       if (closed) return;
       closed = true;
       turns.close();
+      asks.close();
       speech.close();
       for (const listener of closeListeners) listener(code);
     },
     onFrame(frame) {
       turns.onFrame(frame);
+      asks.onFrame(frame);
     },
     settingsChanged(device, nameChanged) {
+      const botChanged = device.botId !== botId;
       botId = device.botId;
       const bot = botRef(botId);
       send({ op: "settings", bot, settings: { speak_pushes: device.speakPushes }, ...(nameChanged ? { name: fold(device.name) } : {}) });
+      if (botChanged) asks.reset();
     },
     push(post, rawText) {
       const device = devices.gadget(deviceId);
@@ -255,6 +263,7 @@
     ownsMessage: (messageId) => turns.ownsMessage(messageId),
     harnessRestarted() {
       turns.harnessRestarted();
+      asks.reset();
     },
     revoke() {
       if (closed) return;
PATCH
````

- [ ] **Step 5: Run the tests and the checks**

```bash
cd /Users/omkar/Desktop/openmaus/OpenGrokBot-gadget || exit 1
export PATH="$HOME/.nvm/versions/node/v24.14.1/bin:$PATH"
pnpm exec vitest run companion/test/gadget
pnpm exec tsc -p tsconfig.server.json  # 40-150 s: give this Bash call a timeout of at least 10 minutes
pnpm exec oxlint --deny-warnings companion
```

Expected: `hub-asks.test.ts` 16 passed; the folder shows `Test Files  11 passed (11)` and `Tests  145 passed (145)`; no `tsc` output; oxlint exits 0.

- [ ] **Step 6: Commit**

```bash
cd /Users/omkar/Desktop/openmaus/OpenGrokBot-gadget || exit 1
export PATH="$HOME/.nvm/versions/node/v24.14.1/bin:$PATH"
git add companion/src/gadget/asks.ts companion/src/gadget/session.ts companion/test/gadget/hub-asks.test.ts && git commit -m "feat(companion): approvals and questions on gadgets, Allow and Deny only

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 11: Pushes

**Files:**
- Create: `companion/src/gadget/push.ts`
- Modify: `companion/src/gadget/hub.ts` (Task 9: import, the router next to the directory, `onFrame`)
- Test: `companion/test/gadget/hub-push.test.ts`

**Interfaces:**
- Consumes: `BotDirectory.userMessage/rememberMessages/task/botForThread` (Task 9), `session.push(post, rawText)` and `session.ownsMessage(id)` (Task 9).
- Produces: `PushTarget`, `PushRouter`, `routinePushText(run)`, `createPushRouter(ctx)`. The post id is `"p_"` + 16 hex of `sha256(kind + ":" + key)`. Routine posts are deduped by `runId:status`, message posts by message id. Post text is folded and cut to 600 characters. `speak` follows the gadget's "Read pushes aloud", and the raw text goes to `SpeechOut.post` (a no-op until P3b). When a reply's user message is not in the directory, the lookup `GET /api/threads/:threadId/messages?limit=50` carries the id of a connected gadget on that bot (contract §3.7 pins that call to the gadget id). With no gadget on the bot connected, nothing is looked up or pushed.

- [ ] **Step 1: Write the failing test**

It covers every push row of spec §6.2 and §10: a 3-item unprompted turn gives exactly one post, a routine run gives exactly one post per status, a late reply to an ended gadget turn gives none, plus Review Focus item 4. The lookup test also checks that the thread read carries the gadget's id.

````ts
// Pushes (spec §4.6, §6.2, A6): routine results and unprompted messages
// reach the gadget once per bot turn; replies to a person's message from
// any device — including late replies to the gadget's own turns — never do.
import { afterEach, describe, expect, it } from "vitest";

import type { HostToGadget } from "../../src/gadget/protocol.ts";
import type { TestGadget } from "./helpers/gadget-client.ts";
import { startRig, type Rig } from "./helpers/hub-rig.ts";

let rig: Rig;
afterEach(async () => { await rig?.close(); });

const BOTS = [{
  id: "b1", name: "Jev", threadId: "th1",
  tasks: [{ threadId: "th1", title: "Main" }, { threadId: "th_run", title: "Daily brief", routineRunId: "run_1" }],
}];
const posts = (gadget: TestGadget) => gadget.frames().filter((m): m is Extract<HostToGadget, { op: "post" }> => m.op === "post");
const userMsg = (id: string, extra: Record<string, unknown> = {}, threadId = "th1") =>
  ({ kind: "message", threadId, message: { id, role: "user", kind: "text", text: "hi", at: 1, ...extra } });
const terminal = (id: string, extra: Record<string, unknown> = {}, threadId = "th1") =>
  ({ kind: "message.patch", threadId, message: { id, role: "bot", kind: "text", text: "Here you go", turnId: `tu_${id}`, turnTerminal: true, at: 2, ...extra } });
/** A frame after which nothing else is pending: proves earlier frames were handled. */
async function settle(gadget: TestGadget, rigRef: Rig) {
  rigRef.harness.emit({ kind: "routine.run", run: { id: `sentinel_${Math.random()}`, botId: "b1", routineName: "Sentinel", status: "completed", output: "sentinel" } });
  await expect.poll(() => posts(gadget).some((p) => p.text === "sentinel")).toBe(true);
}

async function ready(speakPushes = false) {
  rig = await startRig({}, BOTS);
  const gadget = await rig.enroll();
  if (speakPushes) rig.devices.updateGadget(gadget.id, { speakPushes: true });
  await rig.harness.waitForSubscriber();
  return gadget;
}

describe("routine pushes", () => {
  it("posts a routine run once per status with its output, else error, else attention", async () => {
    const gadget = await ready(true);
    const run = { id: "run_1", botId: "b1", routineName: "Daily brief", status: "completed", output: "**3** meetings today" };
    rig.harness.emit({ kind: "routine.run", run });
    rig.harness.emit({ kind: "routine.run", run });
    rig.harness.emit({ kind: "routine.run", run: { ...run, status: "running" } });
    rig.harness.emit({ kind: "routine.run", run: { id: "run_2", botId: "b1", routineName: "Backup", status: "failed", error: "disk full" } });
    rig.harness.emit({ kind: "routine.run", run: { id: "run_3", botId: "b1", routineName: "Review", status: "waiting", attention: "Needs your approval" } });
    rig.harness.emit({ kind: "routine.run", run: { id: "run_4", botId: "b1", routineName: "Sync", status: "missed" } });
    rig.harness.emit({ kind: "routine.run", run: { id: "run_5", botId: "b_other", routineName: "Other", status: "completed", output: "not yours" } });
    await settle(gadget, rig);
    expect(posts(gadget).map((p) => [p.kind, p.text, p.speak])).toEqual([
      ["routine", "3 meetings today", true],
      ["routine", "disk full", true],
      ["routine", "Needs your approval", true],
      ["routine", "Sync was missed", true],
      ["routine", "sentinel", true],
    ]);
    expect(posts(gadget)[0]).toMatchObject({ bot: { id: "b1", name: "Jev" }, id: expect.stringMatching(/^p_[0-9a-f]{16}$/) });
  });
});

describe("message pushes", () => {
  it("posts an unprompted turn exactly once, however many items it had", async () => {
    const gadget = await ready();
    rig.harness.emit({ kind: "message", threadId: "th1", message: { id: "i1", role: "bot", kind: "text", text: "Step one", turnId: "tu9", at: 1 } });
    rig.harness.emit({ kind: "message", threadId: "th1", message: { id: "i2", role: "bot", kind: "text", text: "Step two", turnId: "tu9", at: 2 } });
    rig.harness.emit({ kind: "message", threadId: "th1", message: { id: "i3", role: "bot", kind: "text", text: "All done", turnId: "tu9", at: 3 } });
    rig.harness.emit(terminal("i3", { text: "All done", turnId: "tu9" }));
    rig.harness.emit(terminal("i3", { text: "All done", turnId: "tu9", turnSucceeded: true }));
    await settle(gadget, rig);
    expect(posts(gadget).filter((p) => p.kind === "message").map((p) => p.text)).toEqual(["All done"]);
  });

  it("never posts a reply to a person's message from the desktop or a phone", async () => {
    const gadget = await ready();
    rig.harness.emit(userMsg("u_person"));
    rig.harness.emit(terminal("b_reply", { requestMessageId: "u_person" }));
    await settle(gadget, rig);
    expect(posts(gadget).filter((p) => p.kind === "message")).toEqual([]);
  });

  it("posts a reply to another bot's ask", async () => {
    const gadget = await ready();
    rig.harness.emit(userMsg("u_peer", { peerAsk: { botId: "b_ops", name: "Ops" } }));
    rig.harness.emit(terminal("b_peer_reply", { requestMessageId: "u_peer", text: "Handed off" }));
    await expect.poll(() => posts(gadget).map((p) => p.text)).toEqual(["Handed off"]);
  });

  it("never posts a late reply to the gadget's own ended turn", async () => {
    const gadget = await ready();
    gadget.send({ op: "say", turn: "t1-1", text: "hi" });
    const sent = await rig.harness.waitFor("POST", /\/messages$/);
    gadget.send({ op: "stop" });
    await gadget.next("done");
    rig.harness.emit(userMsg("m_user_1", { sendId: (sent.json as { sendId: string }).sendId }));
    rig.harness.emit(terminal("late", { requestMessageId: "m_user_1" }));
    await settle(gadget, rig);
    expect(posts(gadget).filter((p) => p.kind === "message")).toEqual([]);
  });

  it("never posts from a routine's execution thread, a digest or an activity", async () => {
    const gadget = await ready();
    rig.harness.emit(terminal("r_text", {}, "th_run"));
    rig.harness.emit({ kind: "message", threadId: "th1", message: { id: "d1", role: "bot", kind: "digest", turnTerminal: true, at: 1 } });
    rig.harness.emit({ kind: "message", threadId: "th1", message: { id: "a1", role: "bot", kind: "activity", turnTerminal: true, at: 1 } });
    await settle(gadget, rig);
    expect(posts(gadget).filter((p) => p.kind === "message")).toEqual([]);
  });

  it("looks a missing user message up in the thread before deciding", async () => {
    const gadget = await ready();
    rig.harness.route("GET", /^\/api\/threads\/th1\/messages/, () => ({
      status: 200,
      json: { messages: [{ id: "u_old_peer", role: "user", kind: "text", peerAsk: { botId: "b9", name: "Nine" }, at: 1 }], hasMore: false, activeLeafId: null },
    }));
    rig.harness.emit(terminal("b_unknown", { requestMessageId: "u_missing" }));
    rig.harness.emit(terminal("b_known", { requestMessageId: "u_old_peer", text: "Peer answer" }));
    await expect.poll(() => posts(gadget).map((p) => p.text)).toEqual(["Peer answer"]);
    // A per-gadget call: it carries the gadget's id, like asks.ts's reads (contract §3.7).
    const lookups = rig.harness.requests.filter((r) => r.path === "/api/threads/th1/messages?limit=50");
    expect(lookups.length).toBeGreaterThan(0);
    for (const r of lookups) expect(r.headers["x-openmausbot-companion-device"]).toBe(gadget.id);
  });

  it("does not post the gadget's own steered reply", async () => {
    const gadget = await ready();
    rig.harness.emit(userMsg("u_peer2", { peerAsk: { botId: "b_ops", name: "Ops" } }));
    rig.harness.emit({ kind: "runtime", event: { type: "turn.started", threadId: "th1", turnId: "tu0" } });
    rig.harness.route("POST", /\/messages$/, () => ({ status: 202, json: { ok: true, steered: true, threadId: "th1", message: { id: "m_s", role: "user", kind: "text", at: 1 } } }));
    gadget.send({ op: "say", turn: "t1-1", text: "and also" });
    await rig.harness.waitFor("POST", /\/messages$/);
    await expect.poll(() => rig.harness.requests.filter((r) => r.path.endsWith("/messages")).length).toBe(1);
    rig.harness.emit({ kind: "runtime", event: { type: "turn.completed", threadId: "th1", turnId: "tu0", ok: true } });
    rig.harness.emit(terminal("steered_reply", { requestMessageId: "u_peer2", turnId: "tu0" }));
    await gadget.next("done");
    await settle(gadget, rig);
    expect(posts(gadget).filter((p) => p.kind === "message")).toEqual([]);
  });

  it("never pushes one gadget's reply to another gadget on the same bot", async () => {
    rig = await startRig({}, BOTS);
    const a = await rig.enroll();
    const b = await rig.enroll();
    await rig.harness.waitForSubscriber();
    a.send({ op: "say", turn: "t1-1", text: "mine" });
    const sent = await rig.harness.waitFor("POST", /\/messages$/);
    rig.harness.emit({ kind: "message", threadId: "th1", message: { id: "m_user_1", role: "user", kind: "text", text: "mine", sendId: (sent.json as { sendId: string }).sendId, at: 1 } });
    rig.harness.emit({ kind: "runtime", event: { type: "turn.started", threadId: "th1", turnId: "tu1" } });
    rig.harness.emit({ kind: "runtime", event: { type: "turn.completed", threadId: "th1", turnId: "tu1", ok: true } });
    rig.harness.emit({ kind: "message.patch", threadId: "th1", message: { id: "m_r", role: "bot", kind: "text", text: "for a", turnId: "tu1", requestMessageId: "m_user_1", turnTerminal: true, at: 2 } });
    await a.next("done");
    rig.harness.emit({ kind: "routine.run", run: { id: "s", botId: "b1", routineName: "S", status: "completed", output: "sentinel" } });
    await b.next("post");
    expect(b.frames().filter((m) => m.op === "post").map((m) => (m as { text: string }).text)).toEqual(["sentinel"]);
  });
});
````

- [ ] **Step 2: Run it to see it fail**

Run: `cd /Users/omkar/Desktop/openmaus/OpenGrokBot-gadget && PATH="$HOME/.nvm/versions/node/v24.14.1/bin:$PATH" pnpm exec vitest run companion/test/gadget/hub-push.test.ts`
Expected: FAIL — `Tests  9 failed (9)`: eight with `Error: Matcher did not succeed in time.`, and one ("never pushes one gadget's reply to another gadget on the same bot") with `no post within 5000 ms`. No `post` frames arrive yet.

- [ ] **Step 3: Write `companion/src/gadget/push.ts`**

````ts
// Pushes (spec §4.6, §6.2, A6): what a bot produced on its own reaches the
// gadgets on that bot as one `post` per bot turn. Replies to a person's
// message — from the desktop, a phone or a gadget, early or late — are never
// pushed; the person is already looking at that screen.
import { createHash } from "node:crypto";

import type { BotDirectory } from "./directory.ts";
import type { HarnessClient } from "./harness-client.ts";
import type { BotRef, PostKind, PostMsg } from "./protocol.ts";
import { clampChars, screenText } from "./shape.ts";
import type { RoutineRunLite, ServerFrameLite, WireMessageLite } from "./types.ts";

const POST_TEXT_MAX = 600;
const DEDUPE_MAX = 1000;
const PUSHED_STATUSES = new Set(["completed", "failed", "missed", "waiting"]);

export interface PushTarget {
  readonly deviceId: string;
  botId(): string | null;
  push(post: Omit<PostMsg, "op" | "speak">, rawText: string): void;
  ownsMessage(messageId: string): boolean;
}

export interface PushRouter {
  onFrame(frame: ServerFrameLite): void;
}

const postId = (kind: PostKind, key: string) => `p_${createHash("sha256").update(`${kind}:${key}`).digest("hex").slice(0, 16)}`;

/** The text a routine run pushes: its output, else its error, else what it needs. */
export function routinePushText(run: RoutineRunLite): string {
  const own = run.output?.trim() || run.error?.trim() || run.attention?.trim();
  if (own) return own;
  if (run.status === "completed") return `${run.routineName} finished`;
  if (run.status === "failed") return `${run.routineName} failed`;
  if (run.status === "missed") return `${run.routineName} was missed`;
  return `${run.routineName} needs you`;
}

export function createPushRouter(ctx: {
  directory: BotDirectory;
  harness: HarnessClient;
  targets: () => Iterable<PushTarget>;
  log(line: string): void;
}): PushRouter {
  const seen = new Set<string>();
  const once = (key: string): boolean => {
    if (seen.has(key)) return false;
    seen.add(key);
    if (seen.size > DEDUPE_MAX) seen.delete(seen.values().next().value!);
    return true;
  };

  const deliver = (botId: string, kind: PostKind, key: string, rawText: string) => {
    const bot: BotRef = { id: botId, name: clampChars(screenText(ctx.directory.bot(botId)?.name ?? "", { markdown: false }), 64) };
    const text = clampChars(screenText(rawText, { markdown: true }), POST_TEXT_MAX);
    if (!text) return;
    for (const target of ctx.targets()) {
      if (target.botId() === botId) target.push({ id: postId(kind, key), bot, kind, text }, rawText);
    }
  };

  /** True when the message a reply answers is not a person's message:
   *  a peer ask from another bot. Absent from the cache and from the
   *  thread's last 50 messages → not pushed. The lookup speaks for
   *  `deviceId`, a gadget on that bot (contract §3.7: per-gadget calls carry
   *  the gadget id; only the shared stream and GET /api/bots are marker-only). */
  const answersPeerAsk = async (threadId: string, requestMessageId: string, deviceId: string): Promise<boolean> => {
    let note = ctx.directory.userMessage(threadId, requestMessageId);
    if (!note) {
      try {
        const response = await ctx.harness.json<{ messages?: WireMessageLite[] }>("GET", `/api/threads/${threadId}/messages?limit=50`, deviceId);
        if (response.status === 200 && Array.isArray(response.body.messages)) ctx.directory.rememberMessages(threadId, response.body.messages);
      } catch (error) {
        ctx.log(`push: could not read ${threadId}: ${(error as Error).message}`);
      }
      note = ctx.directory.userMessage(threadId, requestMessageId);
    }
    return note?.peerAsk === true;
  };

  const onMessage = async (threadId: string, message: WireMessageLite) => {
    if (message.role !== "bot" || message.kind !== "text" || message.turnTerminal !== true) return;
    const botId = ctx.directory.botForThread(threadId);
    if (!botId) return;
    // Routine results arrive through routine.run, never from their thread.
    if (ctx.directory.task(threadId)?.routineRunId) return;
    if (!once(`message:${message.id}`)) return;
    const targets = [...ctx.targets()];
    if (targets.some((target) => target.ownsMessage(message.id))) return;
    // No gadget on this bot is connected: nobody to push to, nobody to ask as.
    const target = targets.find((candidate) => candidate.botId() === botId);
    if (!target) return;
    if (message.requestMessageId && !(await answersPeerAsk(threadId, message.requestMessageId, target.deviceId))) return;
    deliver(botId, "message", message.id, message.text ?? "");
  };

  return {
    onFrame(frame) {
      if (frame.kind === "routine.run") {
        const run = frame.run;
        if (!run?.id || !PUSHED_STATUSES.has(run.status)) return;
        const key = `${run.id}:${run.status}`;
        if (!once(`routine:${key}`)) return;
        deliver(run.botId, "routine", key, routinePushText(run));
        return;
      }
      if ((frame.kind === "message" || frame.kind === "message.patch") && frame.message) {
        void onMessage(frame.threadId, frame.message);
      }
    },
  };
}
````

- [ ] **Step 4: Route frames through it in the hub**

````bash
cd /Users/omkar/Desktop/openmaus/OpenGrokBot-gadget || exit 1
export PATH="$HOME/.nvm/versions/node/v24.14.1/bin:$PATH"
git apply <<'PATCH'
--- a/companion/src/gadget/hub.ts
+++ b/companion/src/gadget/hub.ts
@@ -25,6 +25,7 @@
   type ChallengeMsg,
   type GadgetErrorCode,
 } from "./protocol.ts";
+import { createPushRouter, type PushTarget } from "./push.ts";
 import { createGadgetSession, textOnlyVoice, type GadgetSession, type GadgetSessionHandle, type VoiceProvider } from "./session.ts";
 import { foldLatin1, screenText } from "./shape.ts";
 import { defaultGadgetBot, type ServerFrameLite } from "./types.ts";
@@ -96,9 +97,23 @@
   let streamHellos = 0;
   let closing = false;
 
+  const push = createPushRouter({
+    directory,
+    harness,
+    log,
+    targets: (): Iterable<PushTarget> =>
+      [...sessions.values()].map((session) => ({
+        deviceId: session.deviceId,
+        botId: () => options.devices.gadget(session.deviceId)?.botId ?? null,
+        push: (post, rawText) => session.push(post, rawText),
+        ownsMessage: (id) => session.ownsMessage(id),
+      })),
+  });
+
   const onFrame = (frame: ServerFrameLite) => {
     directory.apply(frame);
     for (const session of sessions.values()) session.onFrame(frame);
+    push.onFrame(frame);
   };
 
   const onHello = (resumed: boolean) => {
PATCH
````

- [ ] **Step 5: Run the tests and the checks**

```bash
cd /Users/omkar/Desktop/openmaus/OpenGrokBot-gadget || exit 1
export PATH="$HOME/.nvm/versions/node/v24.14.1/bin:$PATH"
pnpm exec vitest run companion/test/gadget
pnpm exec tsc -p tsconfig.server.json  # 40-150 s: give this Bash call a timeout of at least 10 minutes
pnpm exec oxlint --deny-warnings companion
```

Expected: `hub-push.test.ts` 9 passed; the folder shows `Test Files  12 passed (12)` and `Tests  154 passed (154)`; no `tsc` output; oxlint exits 0.

- [ ] **Step 6: Commit**

```bash
cd /Users/omkar/Desktop/openmaus/OpenGrokBot-gadget || exit 1
export PATH="$HOME/.nvm/versions/node/v24.14.1/bin:$PATH"
git add companion/src/gadget/push.ts companion/src/gadget/hub.ts companion/test/gadget/hub-push.test.ts && git commit -m "feat(companion): push routine results and unprompted messages to gadgets

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 12: Control-port routes for gadgets

**Files:**
- Modify: `companion/src/control.ts` (origin/main :17 import, :50-54 `ControlOptions`, :153 before `currentHostedUrl` (body reader and patch validation), :227 `pairing` in the state, :299-309 `POST /pairing`, :357 before the revoke route (`PATCH /devices/:id/gadget`), :460-467 the control page's device rows)
- Test: `companion/test/gadget/control-gadget.test.ts`

**Interfaces:**
- Consumes: `DeviceRegistry.openPairing(botId)`, `setPairingBot`, `gadget`, `updateGadget`, `cleanGadgetName`, `GadgetSettingsPatch` (Task 5); `GadgetHub.settingsChanged` (Task 9, type only).
- Produces (contract §3.13, P3a rows): `ControlOptions.hostId?: string` and `ControlOptions.gadgetHub?: GadgetHub`; state gains `hostId` and `pairing.botId`; `POST /pairing` takes an optional `{botId}` (≤ 4096 bytes, empty body allowed, 400 on a bad id); `PUT /pairing/bot {botId | null, expectedToken}` answers 200 state or 409 `{error: "no matching pairing window"}`; `PATCH /devices/:id/gadget` takes at least one of `botId` (`/^[\w-]{1,120}$/` or null), `speakPushes` (boolean) and `name` (1–32 characters), and answers 200 state, 400 `{error}`, 404 `{error: "no such gadget"}` or 500 `{error: "could not save gadget settings"}`, then calls `gadgetHub.settingsChanged(id, {nameChanged})`. The cloud-desktop and browser-control routes answer 404 for gadgets (Task 5). The control port's own page (`page()`, the other surface that lists devices) draws a gadget row with its name, "Gadget" and Remove only, without the phone-only grant buttons (spec §6.4). The A35 known limitation is recorded in a comment at `POST /pairing`.

- [ ] **Step 1: Write the failing test**

````ts
// Control-port routes Electron uses for gadgets (contract §3.13): the bot
// picked in Pair a gadget, changing it while the code shows, gadget
// settings that reach a live gadget, and phone-only grants refused.
import { rmSync } from "node:fs";
import { type Server } from "node:http";
import vm from "node:vm";
import { afterEach, beforeEach, describe, expect, it } from "vitest";

import { createControlServer } from "../../src/control.ts";
import { DeviceRegistry } from "../../src/devices.ts";
import type { GadgetHub } from "../../src/gadget/hub.ts";
import { DATA_DIR } from "../../src/state.ts";
import { RFC_ID, RFC_PUBKEY } from "./helpers/fixed-values.ts";

let control: Server;
let port = 0;
let devices: DeviceRegistry;
let changed: Array<[string, { nameChanged: boolean }]>;

const ask = async (method: string, path: string, body?: unknown, raw?: string) => {
  const res = await fetch(`http://127.0.0.1:${port}${path}`, {
    method,
    ...(body !== undefined || raw !== undefined ? { body: raw ?? JSON.stringify(body), headers: { "content-type": "application/json" } } : {}),
  });
  return { status: res.status, body: (await res.json()) as Record<string, any> };
};

beforeEach(async () => {
  rmSync(DATA_DIR, { recursive: true, force: true });
  devices = new DeviceRegistry();
  changed = [];
  const hub = { settingsChanged: (id: string, change: { nameChanged: boolean }) => changed.push([id, change]) } as unknown as GadgetHub;
  control = createControlServer({
    devices,
    companionPort: 8810,
    discovery: () => ({ advertising: false, name: "Test" }),
    hostId: "0123456789abcdef0123456789abcdef",
    gadgetHub: hub,
    publicNetworks: () => new Set(),
  });
  port = await new Promise<number>((resolve) => control.listen(0, "127.0.0.1", () => resolve((control.address() as { port: number }).port)));
});

afterEach(async () => {
  await new Promise<void>((resolve) => control.close(() => resolve()));
});

const enrollGadget = () => {
  const result = devices.enrollGadget(devices.openPairing().code, { id: RFC_ID, publicKey: RFC_PUBKEY, name: "Desk", board: "amoled-175c", firmware: "1.0.0", botId: "b1" });
  if ("error" in result) throw new Error(result.message);
};

describe("pairing with a bot", () => {
  it("opens a window with the picked bot and shows it, with the host id, in the state", async () => {
    const opened = await ask("POST", "/pairing", { botId: "b_jev" });
    expect(opened.status).toBe(201);
    expect(opened.body.code).toMatch(/^\d{6}$/);
    expect(opened.body.pairing).toMatchObject({ botId: "b_jev" });
    expect(opened.body.hostId).toBe("0123456789abcdef0123456789abcdef");
    expect((await ask("POST", "/pairing")).body.pairing).not.toHaveProperty("botId");
  });

  it("refuses a malformed bot id or body", async () => {
    expect((await ask("POST", "/pairing", { botId: "../x" })).status).toBe(400);
    expect((await ask("POST", "/pairing", undefined, "{not json")).status).toBe(400);
    expect((await ask("POST", "/pairing", [1])).status).toBe(400);
  });

  it("changes the bot only for the window the desktop is showing", async () => {
    const opened = await ask("POST", "/pairing", { botId: "b1" });
    expect((await ask("PUT", "/pairing/bot", { botId: "b2", expectedToken: "omb_pair_other" })).status).toBe(409);
    const changedBot = await ask("PUT", "/pairing/bot", { botId: "b2", expectedToken: opened.body.token });
    expect(changedBot.status).toBe(200);
    expect(changedBot.body.pairing.botId).toBe("b2");
    expect((await ask("PUT", "/pairing/bot", { botId: null, expectedToken: opened.body.token })).body.pairing).not.toHaveProperty("botId");
    expect((await ask("PUT", "/pairing/bot", { expectedToken: opened.body.token })).status).toBe(400);
  });
});

describe("gadget settings", () => {
  it("saves the bot, the push setting and the name, and tells the hub", async () => {
    enrollGadget();
    const res = await ask("PATCH", `/devices/${RFC_ID}/gadget`, { botId: "b2", speakPushes: true });
    expect(res.status).toBe(200);
    expect(res.body.devices).toEqual([expect.objectContaining({ kind: "gadget", id: RFC_ID, botId: "b2", speakPushes: true })]);
    await ask("PATCH", `/devices/${RFC_ID}/gadget`, { name: "Kitchen" });
    await ask("PATCH", `/devices/${RFC_ID}/gadget`, { name: "Kitchen" });
    expect(changed).toEqual([[RFC_ID, { nameChanged: false }], [RFC_ID, { nameChanged: true }], [RFC_ID, { nameChanged: false }]]);
  });

  it.each([
    [{}, "nothing to change"],
    [{ botId: 5 }, "botId must be a bot id or null"],
    [{ speakPushes: "yes" }, "speakPushes must be true or false"],
    [{ name: " " }, "name must be 1 to 32 characters"],
    [{ name: "x".repeat(33) }, "name must be 1 to 32 characters"],
    [{ cloudDesktopAccess: true }, "unknown field cloudDesktopAccess"],
  ])("refuses %j", async (body, error) => {
    enrollGadget();
    expect(await ask("PATCH", `/devices/${RFC_ID}/gadget`, body)).toEqual({ status: 400, body: { error } });
    expect(changed).toEqual([]);
  });

  it("answers 404 for a phone or an unknown id", async () => {
    const { code } = devices.openPairing();
    const phone = devices.redeem(code, "iPhone");
    if ("error" in phone) throw new Error(phone.error);
    expect((await ask("PATCH", `/devices/${phone.device.id}/gadget`, { speakPushes: true })).status).toBe(404);
    expect((await ask("PATCH", "/devices/gad_0000000000000000/gadget", { speakPushes: true })).status).toBe(404);
  });

  it("never grants a gadget computer view or browser control", async () => {
    enrollGadget();
    expect((await ask("POST", `/devices/${RFC_ID}/cloud-desktop`)).status).toBe(404);
    expect((await ask("POST", `/devices/${RFC_ID}/browser-control`)).status).toBe(404);
  });

  it("removes a gadget like any device", async () => {
    enrollGadget();
    expect((await ask("DELETE", `/devices/${RFC_ID}`)).status).toBe(200);
    expect(devices.gadgets()).toEqual([]);
  });
});

describe("the control page", () => {
  it("lists a gadget with Remove only, while a phone keeps its grant buttons", async () => {
    enrollGadget();
    const { code } = devices.openPairing();
    const phone = devices.redeem(code, "iPhone");
    if ("error" in phone) throw new Error(phone.error);
    // Run the page's own script against a stub document and the live routes.
    const html = await (await fetch(`http://127.0.0.1:${port}/`)).text();
    const script = /<script type="module">([\s\S]*)<\/script>/.exec(html)![1]!;
    const nodes = new Map<string, { innerHTML: string; textContent: string; addEventListener(): void }>();
    const document = {
      getElementById(id: string) {
        if (!nodes.has(id)) nodes.set(id, { innerHTML: "", textContent: "", addEventListener() {} });
        return nodes.get(id)!;
      },
      querySelectorAll: () => [],
    };
    const context = vm.createContext({
      document,
      fetch: (path: string, init?: RequestInit) => fetch(`http://127.0.0.1:${port}${path}`, init),
      setTimeout: () => 0,
    });
    await vm.runInContext(`(async () => {${script}})()`, context);
    const rows = nodes.get("devices")!.innerHTML.split("<li>").slice(1);
    const gadgetRow = rows.find((row) => row.includes(RFC_ID)) ?? "";
    const phoneRow = rows.find((row) => row.includes(phone.device.id)) ?? "";
    expect(gadgetRow).toContain(`data-revoke='${RFC_ID}'`);
    expect(gadgetRow).toContain("Gadget, last seen");
    expect(gadgetRow).not.toContain("data-cloud");
    expect(gadgetRow).not.toContain("data-browser");
    expect(phoneRow).toContain("data-cloud");
    expect(phoneRow).toContain("data-browser");
  });
});
````

- [ ] **Step 2: Run it to see it fail**

Run: `cd /Users/omkar/Desktop/openmaus/OpenGrokBot-gadget && PATH="$HOME/.nvm/versions/node/v24.14.1/bin:$PATH" pnpm exec vitest run companion/test/gadget/control-gadget.test.ts`
Expected: FAIL — `11 failed | 3 passed`, for example `expected { code: '…', … } to match object { botId: 'b_jev' }`, `expected 201 to be 400`, and the page test's `expected '<div class=\'grow\'>…' to contain 'Gadget, last seen'`.

- [ ] **Step 3: Apply the control change**

````bash
cd /Users/omkar/Desktop/openmaus/OpenGrokBot-gadget || exit 1
export PATH="$HOME/.nvm/versions/node/v24.14.1/bin:$PATH"
git apply <<'PATCH'
--- a/companion/src/control.ts
+++ b/companion/src/control.ts
@@ -14,9 +14,10 @@
 // withhold.
 import { createServer, type Server, type ServerResponse } from "node:http";
 
-import type { DeviceRegistry } from "./devices.ts";
+import { cleanGadgetName, type DeviceRegistry, type GadgetSettingsPatch } from "./devices.ts";
 import { companionEndpointCandidates, hostedCompanionUrl } from "./endpoints.ts";
 import { lanAddresses, lanInterfaces, tailnetName, tailscaleAddress } from "./listener.ts";
+import type { GadgetHub } from "./gadget/hub.ts";
 import { defaultHostName } from "./mdns.ts";
 import { publicNetworks } from "./windows-network.ts";
 
@@ -51,6 +52,10 @@
   /** The adapters Windows has on a Public network (windows-network.ts). Tests
    * pass their own; the sidecar uses the real check. */
   publicNetworks?: () => ReadonlySet<string>;
+  /** This companion's host id (host-id.ts), shown in the state. */
+  hostId?: string;
+  /** The gadget hub: a settings change reaches a live gadget through it. */
+  gadgetHub?: GadgetHub;
 }
 
 /** The host out of a `Host` header, port removed.
@@ -150,6 +155,61 @@
     });
   });
 
+/** A small optional JSON body: undefined when empty, an Error when it is
+ * too large or not JSON. Shape checks belong to the route. */
+const readJsonBody = (req: import("node:http").IncomingMessage, limit = 4096): Promise<unknown> =>
+  new Promise((resolve, reject) => {
+    let size = 0;
+    const chunks: Buffer[] = [];
+    req.on("data", (chunk: Buffer) => {
+      size += chunk.length;
+      if (size > limit) {
+        reject(new Error("body too large"));
+        req.destroy();
+        return;
+      }
+      chunks.push(chunk);
+    });
+    req.on("error", reject);
+    req.on("end", () => {
+      const text = Buffer.concat(chunks).toString("utf8").trim();
+      if (!text) return resolve(undefined);
+      try {
+        resolve(JSON.parse(text));
+      } catch {
+        reject(new Error("invalid JSON body"));
+      }
+    });
+  });
+
+const BOT_ID = /^[\w-]{1,120}$/;
+const isPlainObject = (value: unknown): value is Record<string, unknown> =>
+  value !== null && typeof value === "object" && !Array.isArray(value);
+
+/** PATCH /devices/:id/gadget: at least one known field, each well-formed. */
+function gadgetPatch(body: unknown): GadgetSettingsPatch | string {
+  if (!isPlainObject(body)) return "expected a JSON object";
+  const keys = Object.keys(body);
+  if (!keys.length) return "nothing to change";
+  const patch: GadgetSettingsPatch = {};
+  for (const key of keys) {
+    const value = body[key];
+    if (key === "botId") {
+      if (value !== null && (typeof value !== "string" || !BOT_ID.test(value))) return "botId must be a bot id or null";
+      patch.botId = value as string | null;
+    } else if (key === "speakPushes") {
+      if (typeof value !== "boolean") return "speakPushes must be true or false";
+      patch.speakPushes = value;
+    } else if (key === "name") {
+      if (typeof value !== "string" || !value.trim() || Array.from(value.trim()).length > 32) return "name must be 1 to 32 characters";
+      patch.name = value;
+    } else {
+      return `unknown field ${key}`;
+    }
+  }
+  return patch;
+}
+
 const currentHostedUrl = (options: ControlOptions): string | null =>
   options.hostedUrl?.() ?? null;
 
@@ -224,7 +284,10 @@
       currentHostedUrl(options),
     ),
     ...(secretPublicKey ? { secretPublicKey } : {}),
-    pairing: pairing ? { code: pairing.code, token: pairing.token, expiresAt: pairing.expiresAt } : null,
+    pairing: pairing
+      ? { code: pairing.code, token: pairing.token, expiresAt: pairing.expiresAt, ...(pairing.botId ? { botId: pairing.botId } : {}) }
+      : null,
+    ...(options.hostId ? { hostId: options.hostId } : {}),
     devices: options.devices.list(),
     connectedDeviceIds: options.connectedDeviceIds?.() ?? [],
     discovery: options.discovery(),
@@ -297,16 +360,48 @@
       return;
     }
     if (method === "POST" && path === "/pairing") {
-      const window = options.devices.openPairing();
-      // Keep the freshly issued credentials at the top level as well as in
-      // `pairing`, matching the existing code response and making this write
-      // sufficient for native control clients that do not immediately poll.
-      return json(res, 201, {
-        ...companionState(options),
-        code: window.code,
-        token: window.token,
-      });
+      // An optional {botId}: "Pair a gadget" names the bot the new gadget
+      // talks to. The page and the phone flows send no body at all.
+      // Known limitation (spec §9, A35), unchanged by gadgets: this route and
+      // GET /state stay unauthenticated on loopback, so a local process could
+      // read an open code. A follow-up should require the Electron-minted
+      // token here, together with the page below, which calls both.
+      readJsonBody(req).then(
+        (body) => {
+          if (body !== undefined && (!isPlainObject(body) || (body.botId !== undefined && (typeof body.botId !== "string" || !BOT_ID.test(body.botId))))) {
+            return json(res, 400, { error: "botId must be a bot id" });
+          }
+          const botId = isPlainObject(body) && typeof body.botId === "string" ? body.botId : undefined;
+          const window = options.devices.openPairing(botId);
+          // Keep the freshly issued credentials at the top level as well as in
+          // `pairing`, matching the existing code response and making this write
+          // sufficient for native control clients that do not immediately poll.
+          return json(res, 201, {
+            ...companionState(options),
+            code: window.code,
+            token: window.token,
+          });
+        },
+        (error: Error) => json(res, 400, { error: error.message }),
+      );
+      return;
     }
+    if (method === "PUT" && path === "/pairing/bot") {
+      readJsonBody(req).then(
+        (body) => {
+          if (!isPlainObject(body) || typeof body.expectedToken !== "string" ||
+            (body.botId !== null && (typeof body.botId !== "string" || !BOT_ID.test(body.botId)))) {
+            return json(res, 400, { error: "expected {botId, expectedToken}" });
+          }
+          if (!options.devices.setPairingBot(body.botId as string | null, body.expectedToken)) {
+            return json(res, 409, { error: "no matching pairing window" });
+          }
+          return json(res, 200, companionState(options));
+        },
+        (error: Error) => json(res, 400, { error: error.message }),
+      );
+      return;
+    }
     if (method === "DELETE" && path === "/pairing") {
       const expectedToken = requestUrl.searchParams.get("expectedToken") ?? undefined;
       options.devices.closePairing(expectedToken);
@@ -354,6 +449,28 @@
       options.disconnectDevice?.(browserControl[1]);
       return json(res, 200, companionState(options));
     }
+    const gadgetSettings = path.match(/^\/devices\/([\w-]+)\/gadget$/);
+    if (gadgetSettings && method === "PATCH") {
+      const id = gadgetSettings[1]!;
+      readJsonBody(req).then(
+        (body) => {
+          const patch = gadgetPatch(body);
+          if (typeof patch === "string") return json(res, 400, { error: patch });
+          const before = options.devices.gadget(id);
+          if (!before) return json(res, 404, { error: "no such gadget" });
+          try {
+            options.devices.updateGadget(id, patch);
+          } catch {
+            return json(res, 500, { error: "could not save gadget settings" });
+          }
+          const nameChanged = patch.name !== undefined && cleanGadgetName(patch.name, id) !== before.name;
+          options.gadgetHub?.settingsChanged(id, { nameChanged });
+          return json(res, 200, companionState(options));
+        },
+        (error: Error) => json(res, 400, { error: error.message }),
+      );
+      return;
+    }
     const revoke = path.match(/^\/devices\/([\w-]+)$/);
     if (revoke && method === "DELETE") {
       if (!options.devices.revoke(revoke[1])) return json(res, 404, { error: "no such device" });
@@ -458,13 +575,18 @@
     "<h2>Paired devices</h2>" +
     (s.devices.length
       ? "<ul>" + s.devices.map((d) =>
-          "<li><div class='grow'><div class=name>" + esc(d.name) + "</div>" +
-          "<div class=dim>Last seen " + ago(d.lastSeenAt) + "</div>" +
-          "<button data-cloud='" + esc(d.id) + "' data-allowed='" + (d.cloudDesktopAccess ? "1" : "0") + "'>" +
-          (d.cloudDesktopAccess ? "Cloud desktop on" : "Allow cloud desktop") + "</button>" +
-          "<button data-browser='" + esc(d.id) + "' data-browser-allowed='" + (d.browserControlAccess ? "1" : "0") + "'>" +
-          (d.browserControlAccess ? "Browser control on" : "Allow browser control") + "</button></div>" +
-          "<button data-revoke='" + esc(d.id) + "'>Remove</button></li>").join("") + "</ul>"
+          // A gadget has none of the phone-only grants: its name and Remove.
+          d.kind === "gadget"
+            ? "<li><div class='grow'><div class=name>" + esc(d.name) + "</div>" +
+              "<div class=dim>Gadget, last seen " + ago(d.lastSeenAt) + "</div></div>" +
+              "<button data-revoke='" + esc(d.id) + "'>Remove</button></li>"
+            : "<li><div class='grow'><div class=name>" + esc(d.name) + "</div>" +
+              "<div class=dim>Last seen " + ago(d.lastSeenAt) + "</div>" +
+              "<button data-cloud='" + esc(d.id) + "' data-allowed='" + (d.cloudDesktopAccess ? "1" : "0") + "'>" +
+              (d.cloudDesktopAccess ? "Cloud desktop on" : "Allow cloud desktop") + "</button>" +
+              "<button data-browser='" + esc(d.id) + "' data-browser-allowed='" + (d.browserControlAccess ? "1" : "0") + "'>" +
+              (d.browserControlAccess ? "Browser control on" : "Allow browser control") + "</button></div>" +
+              "<button data-revoke='" + esc(d.id) + "'>Remove</button></li>").join("") + "</ul>"
       : "<p class=dim>No phones are paired yet.</p>");
 
   el("start")?.addEventListener("click", async () => render(await api("/pairing", "POST")));
PATCH
````

- [ ] **Step 4: Run the new and existing control tests and the checks**

```bash
cd /Users/omkar/Desktop/openmaus/OpenGrokBot-gadget || exit 1
export PATH="$HOME/.nvm/versions/node/v24.14.1/bin:$PATH"
pnpm exec vitest run companion/test/gadget/control-gadget.test.ts companion/test/control.test.ts
pnpm exec tsc -p tsconfig.server.json  # 40-150 s: give this Bash call a timeout of at least 10 minutes
pnpm exec oxlint --deny-warnings companion
```

Expected: `control-gadget` 14 passed and `control` 20 passed; no `tsc` output; oxlint exits 0.

- [ ] **Step 5: Commit**

```bash
cd /Users/omkar/Desktop/openmaus/OpenGrokBot-gadget || exit 1
export PATH="$HOME/.nvm/versions/node/v24.14.1/bin:$PATH"
git add companion/src/control.ts companion/test/gadget/control-gadget.test.ts && git commit -m "feat(companion): control routes to pair a gadget with a bot and change its settings

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 13: Wire the hub into the companion, end to end

**Files:**
- Modify: `companion/src/index.ts` (origin/main :27 imports, :30-37 the mdns import loses `clampBytes`, :45-56 the parentPort listener, :126 `HOST_ID`, :152 the TXT record, :155 the hub, :175 the LAN listener's `upgrade`, :187-188 and :197-200 the control options, :296 a startup line, :313-314 shutdown)
- Test: `companion/test/gadget/companion-e2e.test.ts`

**Interfaces:**
- Consumes: `createGadgetHub` (Tasks 9–11), `loadOrCreateHostId`, `serviceTxt` (Task 6), the control options (Task 12).
- Produces (contract §3.12, P3a column): `const HOST_ID = loadOrCreateHostId()`; TXT `["v=1", "name=…", "id=<HOST_ID>"]`; `const gadgetHub = createGadgetHub({devices, voice: undefined, harnessPort: HARNESS_PORT, mutationToken: parentPort ? () => mutationToken : undefined, hostId: HOST_ID, hostName: machineName, connected: connectedDevices.open, onDevicesChanged: undefined})`, which are the insertion points P3b (`voice`) and P4a (`onDevicesChanged`) fill in. Write the properties in exactly this order, one per line: `voice: undefined,` directly after `devices,`, and `onDevicesChanged: undefined,` last, directly before `});`. P3b and P4a each replace one of those two lines on branches cut from `feat/gadget-hub`, and git reports a conflict when two branches change adjacent lines. Five unchanged lines between them let the two merge cleanly (checked with `git merge` and `git rebase` in a scratch repo). Also: the `upgrade` dispatcher on the LAN listener only (`managedOrigin` keeps `proxy.upgrade`); `hostId` and `gadgetHub` in the control options; `gadgetHub.revoke(deviceId)` before the mutation-token early return; `await gadgetHub.close()` before `closeAllConnections`. The parentPort listener now parses `message.gadgetControlToken` (43-character base64url) into `export let gadgetControlToken: string | null`. A standalone sidecar reads `OMB_GADGET_CONTROL_TOKEN` once, then deletes it from `process.env`. `export function onMutationToken(cb)` runs `cb` once, when the relay token is first known. Both are exported only so this branch typechecks under `noUnusedLocals` before P4a reads them; the names and types are the contract's.

- [ ] **Step 1: Write the failing end-to-end test**

It spawns the real `companion/src/index.ts` (as `companion/test/ports.test.ts` does) in front of the fake harness. A TypeScript gadget then enrolls with the code from `POST /pairing {botId}`, proves its key, says something, gets the final reply and `done`, answers an approval, receives `settings` from `PATCH /devices/:id/gadget`, and is removed with `error revoked`. Two more tests check that SIGTERM exits 0 with a gadget connected (the gadget sees close code 1001), and that `/gadget` on the managed origin socket is handed to the viewer relay (401), never upgraded.

````ts
// The real companion process, end to end: a TypeScript gadget pairs with the
// code from the control port's Pair a gadget window, proves its key, talks
// to a fake harness, answers an approval, gets its settings, is removed, and
// a gadget still connected does not keep SIGTERM from stopping the sidecar.
// The /gadget path is checked to be absent from the managed (hosted HTTPS)
// origin, which must keep handing every upgrade to the viewer relay.
import { spawn, type ChildProcess } from "node:child_process";
import { randomBytes } from "node:crypto";
import { mkdtempSync, rmSync } from "node:fs";
import { createServer, request } from "node:http";
import { tmpdir } from "node:os";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";
import { afterEach, describe, expect, it } from "vitest";

import { askIdFor } from "../../src/gadget/asks.ts";
import { startFakeHarness, type FakeHarness } from "./helpers/fake-harness.ts";
import { connectTestGadget } from "./helpers/gadget-client.ts";
import { JEV } from "./helpers/hub-rig.ts";

const ENTRY = join(dirname(fileURLToPath(import.meta.url)), "..", "..", "src", "index.ts");

/** Two consecutive ports below every platform's ephemeral range, verified free
 *  on the interfaces the companion binds (the pattern in proxy.test.ts). */
async function freePorts(): Promise<number> {
  const hold = (port: number, host: string) => new Promise<() => Promise<void>>((resolve, reject) => {
    const server = createServer();
    server.once("error", reject);
    server.listen(port, host, () => resolve(() => new Promise((r) => server.close(() => r()))));
  });
  for (let attempt = 0; attempt < 40; attempt++) {
    const base = 20_000 + Math.floor(Math.random() * 9_000);
    try {
      const a = await hold(base, "0.0.0.0");
      try {
        const b = await hold(base + 1, "127.0.0.1");
        await b();
      } finally {
        await a();
      }
      return base;
    } catch {
      /* taken; try another */
    }
  }
  throw new Error("no free ports");
}

interface Running { child: ChildProcess; companionPort: number; controlPort: number; stdout: () => string; originDir: string | null }
let harness: FakeHarness | null = null;
let running: Running | null = null;

async function startCompanion(withOrigin = false): Promise<Running> {
  harness = await startFakeHarness();
  harness.bots = [JEV];
  const companionPort = await freePorts();
  const originDir = withOrigin ? mkdtempSync(join(process.platform === "darwin" ? "/tmp" : tmpdir(), "omb-companion-origin-")) : null;
  const child = spawn(process.execPath, [ENTRY], {
    env: {
      ...(process.env.PATH ? { PATH: process.env.PATH } : {}),
      ...(process.env.SystemRoot ? { SystemRoot: process.env.SystemRoot } : {}),
      ...(process.env.HOME ? { HOME: process.env.HOME } : {}),
      ...(process.env.USERPROFILE ? { USERPROFILE: process.env.USERPROFILE } : {}),
      ...(process.env.OMB_COMPANION_DIR ? { OMB_COMPANION_DIR: process.env.OMB_COMPANION_DIR } : {}),
      OMB_PORT: String(harness.port),
      OMB_COMPANION_PORT: String(companionPort),
      OMB_CONTROL_PORT: String(companionPort + 1),
      OMB_COMPANION_NAME: "Test computer",
      ...(originDir ? { OMB_COMPANION_INTERNAL_ORIGIN: join(originDir, "origin.sock") } : {}),
    },
    stdio: ["ignore", "pipe", "pipe"],
  });
  let out = "";
  child.stdout!.on("data", (chunk) => { out += chunk; });
  child.stderr!.on("data", (chunk) => { out += chunk; });
  await new Promise<void>((resolve, reject) => {
    const check = () => { if (out.includes("gadgets    ws://")) resolve(); };
    child.stdout!.on("data", check);
    child.once("exit", (code) => reject(new Error(`companion exited ${code}:\n${out}`)));
    check();
  });
  running = { child, companionPort, controlPort: companionPort + 1, stdout: () => out, originDir };
  return running;
}

const control = async (r: Running, method: string, path: string, body?: unknown) => {
  const res = await fetch(`http://127.0.0.1:${r.controlPort}${path}`, {
    method,
    ...(body !== undefined ? { body: JSON.stringify(body), headers: { "content-type": "application/json" } } : {}),
  });
  return (await res.json()) as Record<string, any>;
};

afterEach(async () => {
  if (running && running.child.exitCode === null) {
    running.child.kill("SIGKILL");
    await new Promise((resolve) => running!.child.once("close", resolve));
  }
  if (running?.originDir) rmSync(running.originDir, { recursive: true, force: true });
  running = null;
  await harness?.close();
  harness = null;
});

describe("the companion with a gadget", () => {
  it("pairs, talks, answers an approval, follows settings and is removed", async () => {
    const r = await startCompanion();
    const opened = await control(r, "POST", "/pairing", { botId: "b1" });
    const gadget = await connectTestGadget({ port: r.companionPort, enroll: opened.code, name: "Desk Maus" });
    expect(gadget.ready).toMatchObject({ bot: { id: "b1", name: "Jev" } });

    const state = await control(r, "GET", "/state");
    expect(state.hostId).toMatch(/^[0-9a-f]{32}$/);
    expect(gadget.frames().find((m) => m.op === "challenge")).toMatchObject({ host_id: state.hostId, host_name: "Test computer" });
    expect(state.devices).toEqual([expect.objectContaining({ kind: "gadget", id: gadget.id, name: "Desk Maus", botId: "b1" })]);
    expect(state.connectedDeviceIds).toEqual([gadget.id]);
    expect(state.pairing).toBeNull();

    await harness!.waitForSubscriber();
    gadget.send({ op: "say", turn: "t0a0b0c0d-1", text: "hello" });
    const sent = await harness!.waitFor("POST", /^\/api\/bots\/b1\/messages$/);
    expect(sent.headers["x-openmausbot-companion-device"]).toBe(gadget.id);
    expect(sent.json).toMatchObject({ text: "hello", threadId: "th1" });
    harness!.emit({ kind: "runtime", event: { type: "turn.started", threadId: "th1", turnId: "tu1" } });
    harness!.emit({ kind: "runtime", event: { type: "turn.completed", threadId: "th1", turnId: "tu1", ok: true } });
    harness!.emit({ kind: "message.patch", threadId: "th1", message: { id: "m1", role: "bot", kind: "text", text: "Hi **there**", turnId: "tu1", turnTerminal: true, at: 1 } });
    expect(await gadget.next("reply")).toMatchObject({ text: "Hi there", final: true });
    expect(await gadget.next("done")).toMatchObject({ outcome: "ok" });

    harness!.emit({ kind: "message", threadId: "th1", message: { id: "m2", role: "bot", kind: "options", at: 2, card: { title: "Approval needed", subtitle: "Run ls", options: ["Allow", "Deny", "Always allow"], requestType: "permission", tool: "Bash", requestId: "r1" } } });
    const ask = await gadget.next("ask");
    expect(ask).toMatchObject({ id: askIdFor("th1", "r1"), kind: "permission" });
    gadget.send({ op: "answer", id: ask.id, option: "allow" });
    expect((await harness!.waitFor("POST", /^\/api\/threads\/th1\/respond$/)).json).toEqual({ requestId: "r1", behavior: "allow" });

    await control(r, "PATCH", `/devices/${gadget.id}/gadget`, { speakPushes: true, name: "Kitchen" });
    expect(await gadget.next("settings")).toEqual({ op: "settings", bot: { id: "b1", name: "Jev" }, settings: { speak_pushes: true }, name: "Kitchen" });

    await control(r, "DELETE", `/devices/${gadget.id}`);
    expect(await gadget.next("error")).toMatchObject({ code: "revoked" });
    expect((await gadget.closed()).code).toBe(1000);
    expect((await control(r, "GET", "/state")).devices).toEqual([]);
  });

  // Windows has no SIGTERM handler to run: Electron's utilityProcess.kill()
  // terminates the sidecar outright there, and gadgets see the TCP reset.
  it.skipIf(process.platform === "win32")("stops on SIGTERM with a gadget connected", async () => {
    const r = await startCompanion();
    const opened = await control(r, "POST", "/pairing", {});
    const gadget = await connectTestGadget({ port: r.companionPort, enroll: opened.code });
    expect(gadget.ready).not.toBeNull();
    const exited = new Promise<number | null>((resolve) => r.child.once("exit", (code) => resolve(code)));
    const killer = setTimeout(() => r.child.kill("SIGKILL"), 8_000);
    r.child.kill("SIGTERM");
    expect(await exited).toBe(0);
    clearTimeout(killer);
    expect((await gadget.closed()).code).toBe(1001);
  }, 20_000);

  it.skipIf(process.platform === "win32")("never serves /gadget on the managed origin", async () => {
    const r = await startCompanion(true);
    const socketPath = join(r.originDir!, "origin.sock");
    const status = await new Promise<number>((resolve, reject) => {
      const req = request({
        socketPath,
        path: "/gadget",
        headers: {
          Connection: "Upgrade", Upgrade: "websocket", "Sec-WebSocket-Version": "13",
          "Sec-WebSocket-Key": randomBytes(16).toString("base64"), "Sec-WebSocket-Protocol": "openmausbot-gadget.1",
        },
      });
      req.on("upgrade", () => resolve(101));
      req.on("response", (res) => { res.resume(); resolve(res.statusCode ?? 0); });
      req.on("error", reject);
      req.end();
    });
    expect(status).toBe(401);
  });
});
````

- [ ] **Step 2: Run it to see it fail**

Run: `cd /Users/omkar/Desktop/openmaus/OpenGrokBot-gadget && PATH="$HOME/.nvm/versions/node/v24.14.1/bin:$PATH" pnpm exec vitest run companion/test/gadget/companion-e2e.test.ts`
Expected: FAIL — the first two tests time out (`Test timed out`) waiting for the `gadgets    ws://` startup line, which the unwired companion never prints.

- [ ] **Step 3: Wire the hub**

````bash
cd /Users/omkar/Desktop/openmaus/OpenGrokBot-gadget || exit 1
export PATH="$HOME/.nvm/versions/node/v24.14.1/bin:$PATH"
git apply <<'PATCH'
--- a/companion/src/index.ts
+++ b/companion/src/index.ts
@@ -25,11 +25,12 @@
 import { createControlServer, hostCandidates } from "./control.ts";
 import { createConnectedDeviceTracker } from "./connected-devices.ts";
 import { DeviceRegistry } from "./devices.ts";
+import { createGadgetHub } from "./gadget/hub.ts";
+import { loadOrCreateHostId, serviceTxt } from "./host-id.ts";
 import { companionEndpointCandidates, hostedCompanionUrl } from "./endpoints.ts";
 import { lanAddresses, refreshTailnetName, tailnetName, tailscaleAddress } from "./listener.ts";
 import {
   advertisableAddresses,
-  clampBytes,
   defaultHostName,
   dnsLabel,
   MdnsResponder,
@@ -46,15 +47,37 @@
   parentPort?: { on(event: "message", listener: (event: { data?: unknown }) => void): void };
 }).parentPort;
 let mutationToken: string | null = null;
+const TOKEN_SHAPE = /^[A-Za-z0-9_-]{43}$/;
+// The gadget control token guards the loopback /gadget/* routes the harness
+// calls for bot tools. Electron sends it with the relay token; a standalone
+// sidecar may take it from OMB_GADGET_CONTROL_TOKEN for dev and tests, read
+// once here and removed so nothing this process starts can inherit it.
+const envGadgetControlToken = process.env.OMB_GADGET_CONTROL_TOKEN ?? "";
+delete process.env.OMB_GADGET_CONTROL_TOKEN;
+/** Exported for the bot-tools wiring (control routes), which reads it. */
+export let gadgetControlToken: string | null =
+  !parentPort && TOKEN_SHAPE.test(envGadgetControlToken) ? envGadgetControlToken : null;
+const mutationTokenWaiters: Array<() => void> = [];
 parentPort?.on("message", ({ data }) => {
   if (!data || typeof data !== "object") return;
   const message = data as Record<string, unknown>;
   if (message.type !== "openmausbot:companion-mutation-token") return;
-  if (typeof message.token === "string" && /^[A-Za-z0-9_-]{43}$/.test(message.token)) {
+  if (typeof message.gadgetControlToken === "string" && TOKEN_SHAPE.test(message.gadgetControlToken)) {
+    gadgetControlToken = message.gadgetControlToken;
+  }
+  if (typeof message.token === "string" && TOKEN_SHAPE.test(message.token)) {
     mutationToken = message.token;
+    for (const waiter of mutationTokenWaiters.splice(0)) waiter();
   }
 });
 
+/** Run `cb` once, the first time the relay token is known — at once when it
+ * already is. Exported for notices that must wait for it under Electron. */
+export function onMutationToken(cb: () => void): void {
+  if (mutationToken) cb();
+  else mutationTokenWaiters.push(cb);
+}
+
 /** A port from the environment, or the default. Anything that is not a whole
  * number in range is the default — a typo'd port must not become port 0. */
 const num = (value: string | undefined, fallback: number): number => {
@@ -124,6 +147,7 @@
 }
 
 const devices = new DeviceRegistry();
+const HOST_ID = loadOrCreateHostId();
 const mdns = new MdnsResponder();
 
 /** Keeps the Bonjour record matching the interface table: advertise when a
@@ -146,13 +170,22 @@
   port: COMPANION_PORT,
   host: defaultHostName(),
   addresses: advertisableAddresses(),
-  // TXT entries cap at 255 bytes, and this one is user-supplied — measured in
-  // bytes, since that is the unit the wire format actually counts in, and
-  // `slice` counts UTF-16 code units.
-  txt: ["v=1", `name=${clampBytes(machineName(), 200)}`],
+  // v=1, the name (clamped in bytes: TXT entries cap at 255) and the host
+  // id a gadget pins its proofs to.
+  txt: serviceTxt(machineName(), HOST_ID),
 });
 
 const connectedDevices = createConnectedDeviceTracker();
+const gadgetHub = createGadgetHub({
+  devices,
+  voice: undefined,
+  harnessPort: HARNESS_PORT,
+  mutationToken: parentPort ? () => mutationToken : undefined,
+  hostId: HOST_ID,
+  hostName: machineName,
+  connected: connectedDevices.open,
+  onDevicesChanged: undefined,
+});
 const proxy = createProxyHandler({
     harnessPort: HARNESS_PORT,
     mutationToken: parentPort ? () => mutationToken : undefined,
@@ -172,7 +205,11 @@
 // `createServer(proxy)` handles ordinary HTTP only. noVNC switches its
 // connection to WebSocket, so every server exposing this proxy must also
 // forward Node's separate `upgrade` event to the viewer relay.
-companion.on("upgrade", proxy.upgrade);
+// /gadget is served here, on the LAN listener, and nowhere else: the
+// managed origin below is the hosted HTTPS route and keeps proxy.upgrade.
+companion.on("upgrade", (req, socket, head) =>
+  gadgetHub.isGadgetPath(req.url) ? gadgetHub.handleUpgrade(req, socket, head) : proxy.upgrade(req, socket, head),
+);
 const managedOrigin = PRIVATE_ORIGIN ? createServer(proxy) : null;
 managedOrigin?.on("upgrade", proxy.upgrade);
 
@@ -185,6 +222,8 @@
     hostedUrl = next;
   },
   discovery: () => ({ advertising: mdns.advertising, name: service().name }),
+  hostId: HOST_ID,
+  gadgetHub,
   connectedDeviceIds: connectedDevices.ids,
   disconnectDevice: (deviceId) => {
     connectedDevices.disconnect(deviceId);
@@ -195,6 +234,8 @@
   // idle hang-up. Under the desktop app the notice needs the relay token; a
   // sidecar still waiting for it has no call to protect yet either.
   revoked: (deviceId) => {
+    // A removed gadget's session ends here whatever the relay token's state.
+    gadgetHub.revoke(deviceId);
     if (parentPort && !mutationToken) return;
     void notifyDeviceRevoked({ harnessPort: HARNESS_PORT, deviceId, mutationToken: mutationToken ?? undefined });
   },
@@ -294,6 +335,7 @@
   const reach = tailnetName() ?? tailscale ?? addresses[0];
   console.log(`companion  http://0.0.0.0:${COMPANION_PORT}  →  harness 127.0.0.1:${HARNESS_PORT}`);
   console.log(`pair here  http://127.0.0.1:${CONTROL_PORT}`);
+  console.log(`gadgets    ws://0.0.0.0:${COMPANION_PORT}/gadget`);
   if (reach) console.log(`on your phone, enter  ${reach}:${COMPANION_PORT}`);
   if (tailscale && !tailnetName()) {
     // Do not tell someone to turn on MagicDNS when they may well have it on
@@ -312,6 +354,9 @@
   // line just withdrew
   watcher.stop();
   await mdns.stop().catch(() => {});
+  // Upgraded gadget sockets are invisible to closeAllConnections and would
+  // keep companion.close() pending forever: close them first.
+  await gadgetHub.close();
   // close() waits for open connections, and an SSE stream never ends on its
   // own — drop the sockets so "stop" means stopped, now.
   companion.closeAllConnections?.();
PATCH
````

- [ ] **Step 4: Run the end-to-end test, the whole companion suite and the build**

```bash
cd /Users/omkar/Desktop/openmaus/OpenGrokBot-gadget || exit 1
export PATH="$HOME/.nvm/versions/node/v24.14.1/bin:$PATH"
pnpm exec vitest run companion/test/gadget/companion-e2e.test.ts
pnpm exec vitest run companion/test
pnpm exec tsc -p tsconfig.server.json  # 40-150 s: give this Bash call a timeout of at least 10 minutes
pnpm exec oxlint --deny-warnings companion
pnpm build:companion
ls dist-companion/gadget
```

Expected: `Tests  3 passed (3)` (on Windows the SIGTERM and managed-origin tests are skipped); the companion suite passes completely (on this Mac 35 files and 488 tests, including `proxy.test.ts` and `tailscale-cli.test.ts`); no `tsc` output; oxlint exits 0; `build:companion` succeeds and lists `asks.js directory.js enroll.js harness-client.js hub.js protocol.js push.js session.js shape.js turns.js types.js ws.js`.

- [ ] **Step 5: Smoke the built companion outside the repo (no node_modules, as packaged)**

```bash
cd /Users/omkar/Desktop/openmaus/OpenGrokBot-gadget || exit 1
export PATH="$HOME/.nvm/versions/node/v24.14.1/bin:$PATH"
SMOKE=$(mktemp -d) && cp -R dist-companion "$SMOKE/companion"
(cd "$SMOKE" && HOME="$SMOKE" OMB_COMPANION_DIR="$SMOKE/data" OMB_PORT=29700 OMB_COMPANION_PORT=29710 OMB_CONTROL_PORT=29711 OMB_COMPANION_NAME=Smoke node companion/index.js > smoke.log 2>&1 & echo $! > pid)
for i in 1 2 3 4 5 6 7 8 9 10; do grep -q "gadgets    ws://" "$SMOKE/smoke.log" && break; sleep 1; done
cat "$SMOKE/smoke.log"; cat "$SMOKE/data/host.json"; kill "$(cat "$SMOKE/pid")"
```

Expected: the log includes `pair here  http://127.0.0.1:29711` and `gadgets    ws://0.0.0.0:29710/gadget`; `host.json` holds `{"hostId": "<32 lowercase hex>"}`. (This is a one-off smoke command, not a test, so the poll loop is acceptable here.)

- [ ] **Step 6: Commit**

```bash
cd /Users/omkar/Desktop/openmaus/OpenGrokBot-gadget || exit 1
export PATH="$HOME/.nvm/versions/node/v24.14.1/bin:$PATH"
git add companion/src/index.ts companion/test/gadget/companion-e2e.test.ts && git commit -m "feat(companion): serve /gadget on the LAN listener only, with host id and clean shutdown

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 14: The harness receives the gadget control token

**Files:**
- Create: `server/gadget-control-token.ts`
- Modify: `server/index.ts` (origin/main :224 import, :785 the variable, :1975-1979 `applyDesktopMutationTokenMessage`)
- Test: `server/gadget-control-token.test.ts`

**Interfaces:**
- Consumes: Electron's `openmausbot:desktop-mutation-token` message, which gains `gadgetControlToken` (Task 15).
- Produces: `gadgetControlTokenFrom(message): string | null` and `takeGadgetControlTokenFromEnv(env): string | null` (reads and always deletes `OMB_GADGET_CONTROL_TOKEN`); in `server/index.ts`, `export let gadgetControlToken: string | null` (null under Electron until the message arrives; the dev env value otherwise). P4a's `gadgetControl` wiring reads it, and `OMB_COMPANION_CONTROL_PORT` (Task 15) says where to call. No route is added: the index-route ratchet stays unchanged.

- [ ] **Step 1: Write the failing test**

````ts
import { describe, expect, it } from "vitest";

import { gadgetControlTokenFrom, takeGadgetControlTokenFromEnv } from "./gadget-control-token.ts";

const TOKEN = "A".repeat(43);

describe("gadget control token", () => {
  it("reads a well-formed token from the desktop message only", () => {
    expect(gadgetControlTokenFrom({ type: "openmausbot:desktop-mutation-token", token: "x", gadgetControlToken: TOKEN })).toBe(TOKEN);
    expect(gadgetControlTokenFrom({ gadgetControlToken: "short" })).toBeNull();
    expect(gadgetControlTokenFrom({ gadgetControlToken: 42 })).toBeNull();
    expect(gadgetControlTokenFrom({})).toBeNull();
  });

  it("takes the dev token from the environment and always removes it", () => {
    const env: NodeJS.ProcessEnv = { OMB_GADGET_CONTROL_TOKEN: TOKEN, PATH: "/bin" };
    expect(takeGadgetControlTokenFromEnv(env)).toBe(TOKEN);
    expect(env).toEqual({ PATH: "/bin" });
    const bad: NodeJS.ProcessEnv = { OMB_GADGET_CONTROL_TOKEN: "not-a-token" };
    expect(takeGadgetControlTokenFromEnv(bad)).toBeNull();
    expect("OMB_GADGET_CONTROL_TOKEN" in bad).toBe(false);
    expect(takeGadgetControlTokenFromEnv({})).toBeNull();
  });
});
````

- [ ] **Step 2: Run it to see it fail**

Run: `cd /Users/omkar/Desktop/openmaus/OpenGrokBot-gadget && PATH="$HOME/.nvm/versions/node/v24.14.1/bin:$PATH" pnpm exec vitest run server/gadget-control-token.test.ts`
Expected: FAIL — `Cannot find module './gadget-control-token.ts'`.

- [ ] **Step 3: Write `server/gadget-control-token.ts`**

````ts
// The gadget control token on the harness side (P3a delivers it; the gadget
// tool routes use it to call the companion's loopback /gadget/* routes).
//
// Electron mints it and sends it beside the relay tokens on the private
// utility-process port. A dev harness (pnpm dev:server) may take it from
// OMB_GADGET_CONTROL_TOKEN instead, which is read once and removed, so no
// engine or agents proxy this process starts can inherit it: a bot can read
// its proxy's environment, and this token must never reach a bot.
const TOKEN = /^[A-Za-z0-9_-]{43}$/;

/** The token in Electron's "openmausbot:desktop-mutation-token" message, or null. */
export function gadgetControlTokenFrom(message: Record<string, unknown>): string | null {
  const value = message.gadgetControlToken;
  return typeof value === "string" && TOKEN.test(value) ? value : null;
}

/** OMB_GADGET_CONTROL_TOKEN when well-formed, else null; removed from `env` either way. */
export function takeGadgetControlTokenFromEnv(env: NodeJS.ProcessEnv): string | null {
  const value = env.OMB_GADGET_CONTROL_TOKEN ?? "";
  delete env.OMB_GADGET_CONTROL_TOKEN;
  return TOKEN.test(value) ? value : null;
}
````

- [ ] **Step 4: Read it in the harness**

````bash
cd /Users/omkar/Desktop/openmaus/OpenGrokBot-gadget || exit 1
export PATH="$HOME/.nvm/versions/node/v24.14.1/bin:$PATH"
git apply <<'PATCH'
--- a/server/index.ts
+++ b/server/index.ts
@@ -222,6 +222,7 @@
 import { RETRY_MAX_ATTEMPTS } from "./drivers/retry.ts";
 import { recoveryCapabilityError } from "./automatic-recovery.ts";
 import { decodeGeneratedImage } from "./generated-image.ts";
+import { gadgetControlTokenFrom, takeGadgetControlTokenFromEnv } from "./gadget-control-token.ts";
 import {
   addDisabledMcpServer,
   MAX_MCP_SERVERS,
@@ -783,6 +784,13 @@
 // utility-process port can replace it with the per-launch owner capability.
 let desktopMutationToken: string | undefined = DESKTOP_MANAGED ? "" : undefined;
 let companionMutationToken: string | undefined = DESKTOP_MANAGED ? "" : undefined;
+// The companion's /gadget/* control routes (gadget bot tools) take this
+// token. Electron sends it beside the relay tokens; a dev harness may take
+// OMB_GADGET_CONTROL_TOKEN, which is removed from the environment here either
+// way so no engine or agents proxy started later can inherit it.
+const envGadgetControlToken = takeGadgetControlTokenFromEnv(process.env);
+/** Read by the gadget tool routes, which call the companion's control port. */
+export let gadgetControlToken: string | null = DESKTOP_MANAGED ? null : envGadgetControlToken;
 // Where remote clients reach this server (a proxy's public address); pairing URLs use it.
 const FALLBACK_PUBLIC_URL = process.env.OMB_PUBLIC_URL?.trim().replace(/\/+$/, "") || null;
 const cfg = loadConfig();
@@ -1976,6 +1984,8 @@
   if (typeof message.companionToken === "string" && /^[A-Za-z0-9_-]{43}$/.test(message.companionToken)) {
     companionMutationToken = message.companionToken;
   }
+  const gadgetToken = gadgetControlTokenFrom(message);
+  if (gadgetToken) gadgetControlToken = gadgetToken;
   return true;
 }
 // Browser data of a deleted bot or profile: the engine's saved session
PATCH
````

- [ ] **Step 5: Run the tests, the ratchet and the checks**

```bash
cd /Users/omkar/Desktop/openmaus/OpenGrokBot-gadget || exit 1
export PATH="$HOME/.nvm/versions/node/v24.14.1/bin:$PATH"
pnpm exec vitest run server/gadget-control-token.test.ts scripts/testing/index-route-ratchet.test.ts server/request-auth.test.ts
pnpm exec tsc -p tsconfig.server.json  # 40-150 s: give this Bash call a timeout of at least 10 minutes
pnpm exec oxlint --deny-warnings server
```

Expected: all pass (`gadget-control-token` 2 tests); no `tsc` output; oxlint exits 0.

- [ ] **Step 6: Commit**

```bash
cd /Users/omkar/Desktop/openmaus/OpenGrokBot-gadget || exit 1
export PATH="$HOME/.nvm/versions/node/v24.14.1/bin:$PATH"
git add server/gadget-control-token.ts server/gadget-control-token.test.ts server/index.ts && git commit -m "feat(server): receive the gadget control token from the desktop

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 15: Electron mints and delivers the token; gadget IPC

**Files:**
- Modify: `electron/main.mjs` (origin/main :312 the token, :510-526 the import list, :729-741 `companionLaunchOptions`, :1238-1248 `syncDesktopMutationToken`, :1300-1320 the packaged harness env, :2700-2702 three IPC handlers after `companion:revoke`)
- Modify: `electron/companion.mjs` (:190 `start()`, :223-231 child env, :252-260 the parent-port message, :466 three helpers between `companionRevoke` and `companionCloudDesktopAccess`)
- Modify: `electron/preload.cjs` (:102-112 three bridge methods), `electron/preload.node-test.mjs` (:59-67 a new invocation test)
- Test: `electron/companion-gadget.node-test.mjs`

**Interfaces:**
- Consumes: the control routes (Task 12); `managedRemoteAccessRefusal()`, `decorateDesktopCompanionState()`, `desktopCompanionState()`, `localOnly()` (main.mjs).
- Produces (contract §3.17): `gadgetControlToken` (env `OMB_GADGET_CONTROL_TOKEN` preferred when it matches `/^[A-Za-z0-9_-]{43}$/`, else `randomBytes(32).toString("base64url")`; deleted from `process.env` either way). It is sent in `{type: "openmausbot:companion-mutation-token", token, gadgetControlToken}` and in `{type: "openmausbot:desktop-mutation-token", token, companionToken, gadgetControlToken}`. The packaged harness env gains `OMB_COMPANION_CONTROL_PORT: "8811"`, and `OMB_GADGET_CONTROL_TOKEN` is deleted from both child envs. New helpers: `companionGadgetSettings(deviceId, patch)`, `companionPairGadget(botId)`, `companionPairingBot(botId, expectedToken)`. New IPC: `companion:gadget`, `companion:pair-gadget` (refused under the org remote-access policy, like `companion:pairing`), `companion:pairing-bot`. Preload: `window.ogb.companion.gadget/pairGadget/pairingBot`.
- Note for P4b: `electron/companion-browser.node-test.mjs` slices `companion.mjs` from `companionBrowserControlAccess` to the end of the file and strips only the first `export`. A new exported helper appended at the end of the file breaks that test, so P3a places its helpers before `companionCloudDesktopAccess`, and P4b's helpers must go there too.

- [ ] **Step 1: Write the failing Electron test and the preload test rows**

Create `electron/companion-gadget.node-test.mjs`:

````js
// Gadget plumbing in the desktop main process: the gadget control token
// (minted here, sent only over the children's private parent ports), the
// three gadget IPC channels, and the companion.mjs helpers behind them.
// Production source runs in vm slices, as companion-browser.node-test.mjs does.
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import test from "node:test";
import vm from "node:vm";
import localOrigin from "./local-origin.cjs";

const mainSource = readFileSync(new URL("./main.mjs", import.meta.url), "utf8").replace(/\r\n/g, "\n");
const companionSource = readFileSync(new URL("./companion.mjs", import.meta.url), "utf8").replace(/\r\n/g, "\n");
/** vm objects come from another realm; compare their JSON. */
const plain = (value) => JSON.parse(JSON.stringify(value));
function slice(source, start, end, inclusive = false) {
  const from = source.indexOf(start);
  const to = source.indexOf(end, from + start.length);
  assert.ok(from >= 0 && to > from, `test section moved: ${start}`);
  return source.slice(from, inclusive ? to + end.length : to);
}

test("prefers a well-formed OMB_GADGET_CONTROL_TOKEN, otherwise mints one, and never leaves it in the environment", () => {
  const snippet = slice(mainSource, "const gadgetControlToken =", "delete process.env.OMB_GADGET_CONTROL_TOKEN;", true);
  const run = (env) => {
    const context = vm.createContext({ process: { env }, randomBytes: () => Buffer.alloc(32, 7) });
    vm.runInContext(`${snippet}\nglobalThis.token = gadgetControlToken;`, context);
    return { token: context.token, env };
  };
  const supplied = "A".repeat(43);
  assert.deepEqual(run({ OMB_GADGET_CONTROL_TOKEN: supplied, OTHER: "1" }), { token: supplied, env: { OTHER: "1" } });
  const minted = run({ OMB_GADGET_CONTROL_TOKEN: "too-short" });
  assert.equal(minted.token, Buffer.alloc(32, 7).toString("base64url"));
  assert.match(minted.token, /^[A-Za-z0-9_-]{43}$/);
  assert.equal("OMB_GADGET_CONTROL_TOKEN" in minted.env, false);
});

test("the harness gets the token with the relay tokens over its parent port", () => {
  const fn = slice(mainSource, "function syncDesktopMutationToken(proc) {", "function installDesktopMutationHeader()");
  const context = vm.createContext({ desktopMutationToken: "d", companionMutationToken: "c", gadgetControlToken: "g", slog: () => {} });
  vm.runInContext(fn, context);
  const sent = [];
  context.syncDesktopMutationToken({ postMessage: (message) => sent.push(message) });
  assert.deepEqual(plain(sent), [{ type: "openmausbot:desktop-mutation-token", token: "d", companionToken: "c", gadgetControlToken: "g" }]);
});

test("the packaged harness learns the control port and never inherits the token", () => {
  const fork = slice(mainSource, "async function startServerOn(port) {", "const proc = utilityProcess.fork(entry");
  assert.match(fork, /OMB_COMPANION_CONTROL_PORT: "8811",/);
  assert.match(fork, /delete childEnv\.OMB_GADGET_CONTROL_TOKEN;/);
});

test("the companion gets the token only in its parent-port message", () => {
  const start = slice(companionSource, "async function start({", "export async function companionState()");
  assert.match(start, /gadgetControlToken = null/);
  assert.match(start, /delete childEnvironment\.OMB_GADGET_CONTROL_TOKEN;/);
  assert.match(start, /postMessage\(\{ type: "openmausbot:companion-mutation-token", token: mutationToken, gadgetControlToken \}\)/);
  assert.match(slice(mainSource, "function companionLaunchOptions(", "function ensureManagedCompanionConnector()"), /gadgetControlToken,/);
});

test("the gadget IPC channels are local-only, keep their arguments, and Pair a gadget honours the policy", async () => {
  const handlers = new Map();
  const calls = [];
  let refusal = null;
  localOrigin.setLocalOrigin("http://127.0.0.1:49210");
  vm.runInNewContext(slice(mainSource, 'ipcMain.handle("companion:gadget"', "function publicDesktopRemoteState()"), {
    ipcMain: { handle: (channel, handler) => handlers.set(channel, handler) },
    localOnly: localOrigin.localOnly,
    managedRemoteAccessRefusal: () => refusal,
    companionGadgetSettings: (...args) => { calls.push(["gadget", ...args]); return Promise.resolve(); },
    companionPairGadget: (...args) => { calls.push(["pair", ...args]); return Promise.resolve({ pairing: { code: "123456" } }); },
    companionPairingBot: (...args) => { calls.push(["bot", ...args]); return Promise.resolve({ pairing: null }); },
    desktopCompanionState: () => ({ enabled: true }),
    decorateDesktopCompanionState: (state) => ({ ...state, decorated: true }),
  });
  assert.deepEqual([...handlers.keys()], ["companion:gadget", "companion:pair-gadget", "companion:pairing-bot"]);
  const local = { senderFrame: { url: "http://127.0.0.1:49210/" } };
  assert.deepEqual(await handlers.get("companion:gadget")(local, "gad_1", { speakPushes: true }), { enabled: true });
  assert.deepEqual(await handlers.get("companion:pair-gadget")(local, "b1"), { pairing: { code: "123456" }, decorated: true });
  assert.deepEqual(await handlers.get("companion:pairing-bot")(local, "b2", "omb_pair_x"), { pairing: null, decorated: true });
  assert.deepEqual(calls, [["gadget", "gad_1", { speakPushes: true }], ["pair", "b1"], ["bot", "b2", "omb_pair_x"]]);
  assert.throws(() => handlers.get("companion:gadget")({ senderFrame: { url: "https://remote.invalid/" } }, "gad_1", {}), /only available/);
  refusal = "Acme does not allow remote access to this computer.";
  assert.throws(() => handlers.get("companion:pair-gadget")(local, "b1"), /does not allow remote access/);
  assert.equal(calls.length, 3);
});

function companionHelpers(fail = false) {
  const calls = [];
  const context = vm.createContext({
    proc: {},
    companionState: async () => ({ enabled: true }),
    companionKeepAwakeAtRest: () => false,
    control: async (...args) => { if (fail) throw new Error("down"); calls.push(args); return { pairing: { code: "123456" } }; },
  });
  // The helpers sit between companionRevoke and the phone grants, so the
  // browser test's slice (companionBrowserControlAccess to the end) is untouched.
  const helpers = slice(companionSource, "const GADGET_ID =", "/** Enable or remove interactive cloud-desktop access");
  vm.runInContext(helpers.replaceAll("export async function", "async function"), context);
  return { context, calls };
}

test("gadget settings reach only the fixed path, with only well-formed fields", async () => {
  const { context, calls } = companionHelpers();
  await context.companionGadgetSettings("gad_b18b86ce1389e46d", { botId: "b2", speakPushes: false, name: "  Kitchen ", cloudDesktopAccess: true });
  await context.companionGadgetSettings("gad_b18b86ce1389e46d", { botId: null });
  await context.companionGadgetSettings("gad_b18b86ce1389e46d", { botId: "../x", speakPushes: "yes", name: "x".repeat(33) });
  for (const id of ["../other", "gad?all=1", "a".repeat(65), "", undefined]) await context.companionGadgetSettings(id, { speakPushes: true });
  assert.deepEqual(plain(calls), [
    ["PATCH", "/devices/gad_b18b86ce1389e46d/gadget", { botId: "b2", speakPushes: false, name: "Kitchen" }],
    ["PATCH", "/devices/gad_b18b86ce1389e46d/gadget", { botId: null }],
  ]);
});

test("Pair a gadget opens the window with a valid bot id only, and reports a failure as state", async () => {
  const { context, calls } = companionHelpers();
  assert.deepEqual(plain(await context.companionPairGadget("b_jev")), { enabled: true, keepAwake: false, pairing: { code: "123456" } });
  await context.companionPairGadget("../x");
  await context.companionPairGadget(null);
  assert.deepEqual(plain(calls), [["POST", "/pairing", { botId: "b_jev" }], ["POST", "/pairing", {}], ["POST", "/pairing", {}]]);
  const failing = companionHelpers(true);
  assert.equal((await failing.context.companionPairGadget("b1")).error, "Gadget pairing could not be started.");
});

test("changing the window's bot needs a bot id (or null) and the window's token", async () => {
  const { context, calls } = companionHelpers();
  const token = `omb_pair_${"a".repeat(43)}`;
  await context.companionPairingBot("b2", token);
  await context.companionPairingBot(null, token);
  await context.companionPairingBot("b2", "omb_pair_short");
  await context.companionPairingBot("../x", token);
  assert.deepEqual(plain(calls), [["PUT", "/pairing/bot", { botId: "b2", expectedToken: token }], ["PUT", "/pairing/bot", { botId: null, expectedToken: token }]]);
});
````

````bash
cd /Users/omkar/Desktop/openmaus/OpenGrokBot-gadget || exit 1
export PATH="$HOME/.nvm/versions/node/v24.14.1/bin:$PATH"
git apply <<'PATCH'
--- a/electron/preload.node-test.mjs
+++ b/electron/preload.node-test.mjs
@@ -66,6 +66,20 @@
   ]);
 });
 
+test("gadget settings and Pair a gadget reach their own channels with the exact arguments", async () => {
+  const before = invocations.length;
+  await exposed.api.companion.gadget("gad_b18b86ce1389e46d", { botId: "b1", speakPushes: true });
+  await exposed.api.companion.pairGadget("b1");
+  await exposed.api.companion.pairGadget(null);
+  await exposed.api.companion.pairingBot("b2", "omb_pair_token");
+  assert.deepEqual(invocations.slice(before), [
+    ["companion:gadget", "gad_b18b86ce1389e46d", { botId: "b1", speakPushes: true }],
+    ["companion:pair-gadget", "b1"],
+    ["companion:pair-gadget", null],
+    ["companion:pairing-bot", "b2", "omb_pair_token"],
+  ]);
+});
+
 test("onOpenAppSettings subscribes to the exact app:open-settings channel, forwards every emit, and unsubscribes cleanly", () => {
   // Channel contract: electron/main.mjs answers the Preferences… item with
   // webContents.send("app:open-settings"). Listening on any other name would
PATCH
````

- [ ] **Step 2: Run them to see them fail**

Run: `cd /Users/omkar/Desktop/openmaus/OpenGrokBot-gadget && PATH="$HOME/.nvm/versions/node/v24.14.1/bin:$PATH" node --test electron/companion-gadget.node-test.mjs electron/preload.node-test.mjs`
Expected: FAIL — `test section moved: const gadgetControlToken =` and `TypeError: exposed.api.companion.gadget is not a function`.

- [ ] **Step 3: Mint and deliver the token, add the IPC (main.mjs)**

````bash
cd /Users/omkar/Desktop/openmaus/OpenGrokBot-gadget || exit 1
export PATH="$HOME/.nvm/versions/node/v24.14.1/bin:$PATH"
git apply <<'PATCH'
--- a/electron/main.mjs
+++ b/electron/main.mjs
@@ -310,6 +310,14 @@
 const trustedApprovalMode = createTrustedApprovalModeCoordinator({ randomId: randomUUID });
 const desktopMutationToken = randomBytes(32).toString("base64url");
 const companionMutationToken = randomBytes(32).toString("base64url");
+// Guards the companion's loopback /gadget/* routes, which the harness calls
+// for gadget bot tools. Sent to both children over their private parent
+// ports, never in an environment: the bot can read the agents proxy's env.
+// OMB_GADGET_CONTROL_TOKEN lets a dev sidecar and a dev harness share one.
+const gadgetControlToken = /^[A-Za-z0-9_-]{43}$/.test(process.env.OMB_GADGET_CONTROL_TOKEN ?? "")
+  ? process.env.OMB_GADGET_CONTROL_TOKEN
+  : randomBytes(32).toString("base64url");
+delete process.env.OMB_GADGET_CONTROL_TOKEN;
 const serverSupervisor = createServerSupervisor({
   restart: () => startServerOn(SERVER_PORT),
   stop: stopUtilityServer,
@@ -514,6 +522,9 @@
   companionRefreshTailscale,
   companionCloudDesktopAccess,
   companionBrowserControlAccess,
+  companionGadgetSettings,
+  companionPairGadget,
+  companionPairingBot,
   companionRevoke,
   companionRunning,
   companionState,
@@ -731,6 +742,7 @@
     resourcesPath: process.resourcesPath,
     harnessPort: SERVER_PORT,
     mutationToken: companionMutationToken,
+    gadgetControlToken,
     hostedUrl,
     // Only an embedded server receives the private half over its utility
     // port. A dev server launched in another terminal cannot decrypt, so it
@@ -1241,6 +1253,7 @@
       type: "openmausbot:desktop-mutation-token",
       token: desktopMutationToken,
       companionToken: companionMutationToken,
+      gadgetControlToken,
     });
   } catch (error) {
     slog(`desktop mutation capability sync failed: ${error?.message ?? error}`);
@@ -1304,6 +1317,8 @@
     OMB_RESOURCES_PATH: process.resourcesPath,
     OMB_SKILLS_DIR: path.join(process.resourcesPath, "skills"),
     OMB_PORT: String(port),
+    // where the harness reaches the companion's /gadget/* control routes
+    OMB_COMPANION_CONTROL_PORT: "8811",
     // the server advertises this to remote clients so version skew is visible
     OMB_APP_VERSION: app.getVersion(),
     OMB_USER_DATA: app.getPath("userData"),
@@ -1318,6 +1333,7 @@
     ...workspaceCredentialEnv(secureCredentials),
   });
   delete childEnv.OMB_BROWSER_CONNECTION;
+  delete childEnv.OMB_GADGET_CONTROL_TOKEN;
   slog(`fork ${entry} port=${port}`);
   const proc = utilityProcess.fork(entry, [], {
     env: childEnv,
@@ -2699,6 +2715,19 @@
 ));
 ipcMain.handle("companion:revoke", localOnly("companion:revoke", (_event, deviceId) =>
   companionRevoke(deviceId).then(() => desktopCompanionState()),
+));
+// Gadgets: their bot, Read pushes aloud and name; and "Pair a gadget", which
+// opens the shared pairing window with the bot the new gadget will talk to.
+ipcMain.handle("companion:gadget", localOnly("companion:gadget", (_event, deviceId, patch) =>
+  companionGadgetSettings(deviceId, patch).then(() => desktopCompanionState()),
+));
+ipcMain.handle("companion:pair-gadget", localOnly("companion:pair-gadget", (_event, botId) => {
+  const refusal = managedRemoteAccessRefusal();
+  if (refusal) throw new Error(refusal);
+  return companionPairGadget(botId).then(decorateDesktopCompanionState);
+}));
+ipcMain.handle("companion:pairing-bot", localOnly("companion:pairing-bot", (_event, botId, expectedToken) =>
+  companionPairingBot(botId, expectedToken).then(decorateDesktopCompanionState),
 ));
 
 function publicDesktopRemoteState() {
PATCH
````

- [ ] **Step 4: Pass the token to the companion and add the helpers (companion.mjs)**

````bash
cd /Users/omkar/Desktop/openmaus/OpenGrokBot-gadget || exit 1
export PATH="$HOME/.nvm/versions/node/v24.14.1/bin:$PATH"
git apply <<'PATCH'
--- a/electron/companion.mjs
+++ b/electron/companion.mjs
@@ -187,7 +187,7 @@
 }
 
 /** startCompanion's body, run inside the transition queue. */
-async function start({ resourcesPath, harnessPort, mutationToken, hostedUrl = null, secretPublicKey = null, log }) {
+async function start({ resourcesPath, harnessPort, mutationToken, gadgetControlToken = null, hostedUrl = null, secretPublicKey = null, log }) {
   if (proc) return companionState();
   lastError = null;
   const resolved = entryPoint(resourcesPath);
@@ -224,6 +224,8 @@
   delete childEnvironment.OMB_COMPANION_HOSTED_URL;
   delete childEnvironment.OMB_COMPANION_INTERNAL_ORIGIN;
   delete childEnvironment.OMB_PHONE_SECRET_PUBLIC_KEY;
+  // The gadget control token travels only by the private parent port below.
+  delete childEnvironment.OMB_GADGET_CONTROL_TOKEN;
   if (hostedUrl) childEnvironment.OMB_COMPANION_HOSTED_URL = hostedUrl;
   childEnvironment.OMB_COMPANION_INTERNAL_ORIGIN = allocatedOrigin.socketPath;
   if (/^[A-Za-z0-9_-]{87}$/.test(String(secretPublicKey ?? ""))) {
@@ -252,7 +254,7 @@
   child.once("spawn", () => {
     // Never expose this capability in argv, environment, logs or the renderer.
     try {
-      child.postMessage({ type: "openmausbot:companion-mutation-token", token: mutationToken });
+      child.postMessage({ type: "openmausbot:companion-mutation-token", token: mutationToken, gadgetControlToken });
     } catch {
       log?.("companion authorization could not be initialized");
       child.kill();
@@ -464,6 +466,45 @@
   return companionState();
 }
 
+const GADGET_ID = /^[\w-]{1,64}$/;
+const BOT_ID = /^[\w-]{1,120}$/;
+
+/** Change a gadget's bot, Read pushes aloud or name. Only well-formed
+ * fields reach the sidecar; anything else from the renderer is dropped. */
+export async function companionGadgetSettings(deviceId, patch) {
+  if (!proc) return companionState();
+  if (!GADGET_ID.test(String(deviceId ?? ""))) return companionState();
+  const body = {};
+  if (patch?.botId === null || (typeof patch?.botId === "string" && BOT_ID.test(patch.botId))) body.botId = patch.botId;
+  if (typeof patch?.speakPushes === "boolean") body.speakPushes = patch.speakPushes;
+  if (typeof patch?.name === "string" && patch.name.trim() && [...patch.name.trim()].length <= 32) body.name = patch.name.trim();
+  if (!Object.keys(body).length) return companionState();
+  await control("PATCH", `/devices/${deviceId}/gadget`, body);
+  return companionState();
+}
+
+/** "Pair a gadget": open the shared pairing window with the picked bot. */
+export async function companionPairGadget(botId) {
+  if (!proc) return companionState();
+  const body = typeof botId === "string" && BOT_ID.test(botId) ? { botId } : {};
+  try {
+    const state = await control("POST", "/pairing", body);
+    return { enabled: true, keepAwake: companionKeepAwakeAtRest(), ...state };
+  } catch {
+    const state = await companionState();
+    return { ...state, error: state.error ?? "Gadget pairing could not be started." };
+  }
+}
+
+/** Change the bot of the pairing window the panel is showing. */
+export async function companionPairingBot(botId, expectedToken) {
+  if (!proc) return companionState();
+  if (botId !== null && !(typeof botId === "string" && BOT_ID.test(botId))) return companionState();
+  if (!/^omb_pair_[A-Za-z0-9_-]{43}$/.test(String(expectedToken ?? ""))) return companionState();
+  await control("PUT", "/pairing/bot", { botId, expectedToken }).catch(() => {});
+  return companionState();
+}
+
 /** Enable or remove interactive cloud-desktop access for one paired phone. */
 export async function companionCloudDesktopAccess(deviceId, allowed) {
   if (!proc) return companionState();
PATCH
````

- [ ] **Step 5: Expose the bridge methods (preload.cjs)**

````bash
cd /Users/omkar/Desktop/openmaus/OpenGrokBot-gadget || exit 1
export PATH="$HOME/.nvm/versions/node/v24.14.1/bin:$PATH"
git apply <<'PATCH'
--- a/electron/preload.cjs
+++ b/electron/preload.cjs
@@ -109,6 +109,9 @@
     cloudDesktop: (deviceId, allowed) => ipcRenderer.invoke("companion:cloud-desktop", deviceId, allowed),
     browserControl: (deviceId, allowed) => ipcRenderer.invoke("companion:browser-control", deviceId, allowed),
     revoke: (deviceId) => ipcRenderer.invoke("companion:revoke", deviceId),
+    gadget: (deviceId, patch) => ipcRenderer.invoke("companion:gadget", deviceId, patch),
+    pairGadget: (botId) => ipcRenderer.invoke("companion:pair-gadget", botId),
+    pairingBot: (botId, expectedToken) => ipcRenderer.invoke("companion:pairing-bot", botId, expectedToken),
   },
   /** Keep this computer awake for scheduled routines. The hold itself lives
    * in the main process; the page reads its state and flips the toggle. */
PATCH
````

- [ ] **Step 6: Run all Electron tests and checks**

```bash
cd /Users/omkar/Desktop/openmaus/OpenGrokBot-gadget || exit 1
export PATH="$HOME/.nvm/versions/node/v24.14.1/bin:$PATH"
pnpm test:electron
pnpm check:electron
pnpm exec oxlint --deny-warnings electron
```

Expected: `pnpm test:electron` reports `fail 0` (the new file has 8 tests; `preload.node-test.mjs` now has 8; `companion-browser.node-test.mjs` and `local-only-hoist.node-test.mjs` still pass); `check:electron` and oxlint exit 0.

- [ ] **Step 7: Commit**

```bash
cd /Users/omkar/Desktop/openmaus/OpenGrokBot-gadget || exit 1
export PATH="$HOME/.nvm/versions/node/v24.14.1/bin:$PATH"
git add electron/main.mjs electron/companion.mjs electron/preload.cjs electron/preload.node-test.mjs electron/companion-gadget.node-test.mjs && git commit -m "feat(electron): mint the gadget control token and add gadget pairing IPC

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 16: Renderer types, success copy and counting by kind

**Files:**
- Create: `src/lib/gadgets.ts`
- Modify: `src/components/PhoneSetupFlow.tsx` (origin/main :63-71 `PhoneDevice`, :73-94 `CompanionState`, :96-106 `CompanionBridge`, :193-232 `PhoneSetupController`, :751-755 the paired effect, :852-858 the controller value, :1206-1209 the success copy)
- Modify: `src/lib/phone-setup.ts` (:7-15 state, :17-25 events, :78-79 reducer)
- Modify: `src/components/SidebarPhoneButton.tsx` (:4 import, :48, :71, :93 count phones only)
- Modify: `src/components/CompanionSection.tsx` (:25 import, :107-118 `deriveCompanionPanelStatus`, :175 phones, :249 phone rows)
- Modify: `src/locales/en.json` (after :2077: status and success keys)
- Modify tests: `src/components/PhoneSetupFlow.publicNetwork.test.ts` (fixture gains `pairedGadgetName`), `src/components/SidebarPhoneButton.test.ts`, `src/lib/phone-setup.test.ts`, `src/components/CompanionSection.test.ts`
- Test: `src/lib/gadgets.test.ts`, `src/components/PhoneSetupFlow.gadget.test.ts`

**Interfaces:**
- Consumes: the companion state shape (Tasks 5, 12) and the bridge methods (Task 15).
- Produces (contract §3.18): `PhoneDevice.kind?`, `GadgetSensorsView`, `GadgetDevice`, `PairedDevice`, `GadgetSettingsPatch`, `CompanionState.devices: PairedDevice[]`, `pairing.botId?`, `hostId?`, `CompanionBridge.gadget/pairGadget/pairingBot`; `src/lib/gadgets.ts`: `isGadget(device)`, `boardLabel(board)`, `countDevicesByKind(devices, connectedIds)`, `defaultGadgetBotId(bots)`. Additive: `PhoneSetupController.pairedGadgetName: string | null`, `PhoneSetupFlowState.pairedDeviceKind?`, `{type: "paired"; deviceKind?}`, keys `remote.gadgets.pairedToast`, `remote.gadgets.pairedDetail`, `remote.status.devicesOne|devicesMany|gadgetsOne|gadgetsMany`.

- [ ] **Step 1: Write the failing tests**

Create `src/lib/gadgets.test.ts`:

````ts
import { describe, expect, it } from "vitest";

import type { GadgetDevice, PhoneDevice } from "../components/PhoneSetupFlow";
import { boardLabel, countDevicesByKind, defaultGadgetBotId, isGadget } from "./gadgets";

const phone = (id: string): PhoneDevice => ({ id, name: id, createdAt: 1, lastSeenAt: 1, cloudDesktopAccess: false });
const gadget = (id: string): GadgetDevice => ({
  kind: "gadget", id, name: id, createdAt: 1, lastSeenAt: 1, publicKey: "B", board: "amoled-175c", firmware: "1.0.0", botId: "b1", speakPushes: false,
});

describe("gadgets", () => {
  it("tells gadgets from phones, including phone records with no kind", () => {
    expect(isGadget(gadget("gad_1"))).toBe(true);
    expect(isGadget(phone("p1"))).toBe(false);
    expect(isGadget({ ...phone("p2"), kind: "phone" })).toBe(false);
  });

  it("names the boards and passes unknown ids through", () => {
    expect(boardLabel("amoled-175c")).toBe("ESP32-S3 AMOLED 1.75C");
    expect(boardLabel("devkit")).toBe("ESP32-S3 DevKitC");
    expect(boardLabel("my-board")).toBe("my-board");
  });

  it("counts paired and connected devices by kind", () => {
    expect(countDevicesByKind([phone("p1"), phone("p2"), gadget("g1"), gadget("g2")], ["p2", "g1", "nobody"])).toEqual({
      phones: 2, phonesOnline: 1, gadgets: 2, gadgetsOnline: 1,
    });
  });

  it("picks the default bot by the companion's rule", () => {
    expect(defaultGadgetBotId([{ id: "a" }, { id: "c", chiefOfStaff: true }, { id: "p", pinned: true }])).toBe("c");
    expect(defaultGadgetBotId([{ id: "s", section: "x" }, { id: "p", pinned: true }])).toBe("p");
    expect(defaultGadgetBotId([{ id: "s", section: "x" }, { id: "u" }])).toBe("u");
    expect(defaultGadgetBotId([{ id: "h", hidden: true }, { id: "s", section: "x" }])).toBe("s");
    expect(defaultGadgetBotId([])).toBeNull();
  });
});
````

Create `src/components/PhoneSetupFlow.gadget.test.ts`:

````ts
import { createElement } from "react";
import { renderToStaticMarkup } from "react-dom/server";
import { expect, it } from "vitest";

import { PhoneSetupFlowView, type CompanionState, type PhoneSetupController } from "./PhoneSetupFlow";

// A gadget can use the shared pairing window while the phone flow is open;
// the success screen then names the gadget instead of "Your device".
const state: CompanionState = { enabled: true, keepAwake: false, port: 8810, devices: [], pairing: null };
const noop = () => {};
const controller = (pairedGadgetName: string | null): PhoneSetupController => ({
  state, account: null, phase: "success", email: "", code: "", codeSent: false, busy: false, accountBusy: false,
  error: null, accountError: null, pairingLink: null, secondsLeft: 0, address: undefined, pairingPort: 8810,
  addressText: undefined, hostedReady: false, localFallback: false, tailscaleFallback: false, tailscaleAvailable: false,
  pairedGadgetName, pairingExpired: false, setupTimedOut: false, setEmail: noop, setCode: noop, changeEmail: noop, start: noop,
  useLocal: noop, useTailscale: noop, refreshTailscale: noop, requestCode: noop, verifyCode: noop, retryAccount: noop,
  cancel: noop, refreshCode: noop, finish: noop, skip: noop, act: async () => {}, accountAct: async () => {},
});

it("names a gadget that paired during the phone flow", () => {
  const gadget = renderToStaticMarkup(createElement(PhoneSetupFlowView, { controller: controller("Desk Maus"), variant: "settings" }));
  expect(gadget).toContain("Desk Maus is paired");
  expect(gadget).not.toContain("Your device is ready");
  const phone = renderToStaticMarkup(createElement(PhoneSetupFlowView, { controller: controller(null), variant: "settings" }));
  expect(phone).toContain("Your device is ready");
});
````

Add the new cases to the existing tests:

````bash
cd /Users/omkar/Desktop/openmaus/OpenGrokBot-gadget || exit 1
export PATH="$HOME/.nvm/versions/node/v24.14.1/bin:$PATH"
git apply <<'PATCH'
--- a/src/components/SidebarPhoneButton.test.ts
+++ b/src/components/SidebarPhoneButton.test.ts
@@ -21,6 +21,23 @@
 describe("sidebar phone status", () => {
   const now = 1_900_000_000_000;
 
+  it("never counts a gadget as a phone", () => {
+    const gadget = {
+      kind: "gadget" as const, id: "gad_1", name: "Desk Maus", createdAt: now, lastSeenAt: now,
+      publicKey: "B", board: "amoled-175c", firmware: "1.0.0", botId: "b1", speakPushes: false,
+    };
+    expect(deriveSidebarPhoneStatus({ enabled: true, devices: [gadget], connectedDeviceIds: ["gad_1"] }, now)).toMatchObject({
+      kind: "unpaired",
+      pairedCount: 0,
+    });
+    expect(deriveSidebarPhoneStatus({ enabled: true, devices: [device(now), gadget], connectedDeviceIds: ["gad_1"] }, now)).toMatchObject({
+      kind: "disconnected",
+      label: "Device paired — not connected",
+      pairedCount: 1,
+      connectedCount: 0,
+    });
+  });
+
   it("uses neutral plus semantics when no device is paired", () => {
     expect(deriveSidebarPhoneStatus({ enabled: true, devices: [], connectedDeviceIds: [] }, now)).toEqual({
       kind: "unpaired",
PATCH
````

````bash
cd /Users/omkar/Desktop/openmaus/OpenGrokBot-gadget || exit 1
export PATH="$HOME/.nvm/versions/node/v24.14.1/bin:$PATH"
git apply <<'PATCH'
--- a/src/lib/phone-setup.test.ts
+++ b/src/lib/phone-setup.test.ts
@@ -402,6 +402,18 @@
         pairingOpen: false,
       }),
     ).toBe("success");
+  });
+
+  it("remembers that the device which paired during the flow was a gadget", () => {
+    const opened = phoneSetupReducer(phoneSetupReducer(initialPhoneSetupFlowState, { type: "start", deviceIds: [] }), {
+      type: "pairing-opened",
+      deviceIds: [],
+    });
+    expect(phoneSetupReducer(opened, { type: "paired", deviceName: "Desk Maus", deviceKind: "gadget" })).toMatchObject({
+      pairedDeviceName: "Desk Maus",
+      pairedDeviceKind: "gadget",
+    });
+    expect(phoneSetupReducer(opened, { type: "paired", deviceName: "iPhone" }).pairedDeviceKind).toBe("phone");
   });
 
   it("waits for the initial device snapshot before capturing the success baseline", () => {
PATCH
````

````bash
cd /Users/omkar/Desktop/openmaus/OpenGrokBot-gadget || exit 1
export PATH="$HOME/.nvm/versions/node/v24.14.1/bin:$PATH"
git apply <<'PATCH'
--- a/src/components/CompanionSection.test.ts
+++ b/src/components/CompanionSection.test.ts
@@ -49,6 +49,18 @@
     })).toBeNull();
   });
 
+  it("counts phones and gadgets apart, so a gadget never reads as a device", () => {
+    const phone = { id: "phone-1", name: "Ada", createdAt: 1, lastSeenAt: 1, cloudDesktopAccess: false };
+    const gadget = {
+      kind: "gadget" as const, id: "gad_1", name: "Desk Maus", createdAt: 1, lastSeenAt: 1,
+      publicKey: "B", board: "amoled-175c", firmware: "1.0.0", botId: "b1", speakPushes: false,
+    };
+    expect(deriveCompanionPanelStatus({ enabled: true, devices: [gadget] })).toEqual({ label: "1 gadget paired", good: true });
+    expect(deriveCompanionPanelStatus({ enabled: true, devices: [phone, { ...phone, id: "phone-2" }, gadget] }))
+      .toEqual({ label: "2 devices paired · 1 gadget paired", good: true });
+    expect(deriveCompanionPanelStatus({ enabled: true, devices: [phone] })).toEqual({ label: "1 device paired", good: true });
+  });
+
   it("does not show a healthy status when the enabled sidecar reports an error", () => {
     expect(deriveCompanionPanelStatus({
       enabled: true,
PATCH
````

- [ ] **Step 2: Run them to see them fail**

Run: `cd /Users/omkar/Desktop/openmaus/OpenGrokBot-gadget && PATH="$HOME/.nvm/versions/node/v24.14.1/bin:$PATH" pnpm exec vitest run src/lib/gadgets.test.ts src/components/PhoneSetupFlow.gadget.test.ts src/components/SidebarPhoneButton.test.ts src/lib/phone-setup.test.ts src/components/CompanionSection.test.ts`
Expected: FAIL — `Cannot find module './gadgets'`, the sidebar test counts the gadget as a paired phone, `pairedDeviceKind` is undefined, the success screen says "Your device is ready", and the status chip says `1 device paired` for a gadget.

- [ ] **Step 3: Write `src/lib/gadgets.ts`**

````ts
// Gadgets in Settings → Remote access: pure helpers for the panel, the rows
// and the status counts. A gadget is a paired device with kind "gadget"
// (companion/src/devices.ts); every other device is a phone or desktop app.
import type { GadgetDevice, PairedDevice } from "../components/PhoneSetupFlow";

export function isGadget(device: PairedDevice): device is GadgetDevice {
  return device.kind === "gadget";
}

const BOARD_LABELS: Readonly<Record<string, string>> = {
  "amoled-175c": "ESP32-S3 AMOLED 1.75C",
  "amoled-175": "ESP32-S3 AMOLED 1.75",
  "lcd-154": "ESP32-S3 LCD 1.54",
  devkit: "ESP32-S3 DevKitC",
};

/** The board's product name; an unknown board id passes through. */
export function boardLabel(board: string): string {
  return BOARD_LABELS[board] ?? board;
}

/** Paired and connected counts, phones and gadgets apart. */
export function countDevicesByKind(devices: readonly PairedDevice[], connectedIds: readonly string[]):
  { phones: number; phonesOnline: number; gadgets: number; gadgetsOnline: number } {
  const live = new Set(connectedIds);
  const counts = { phones: 0, phonesOnline: 0, gadgets: 0, gadgetsOnline: 0 };
  for (const device of devices) {
    if (isGadget(device)) {
      counts.gadgets += 1;
      if (live.has(device.id)) counts.gadgetsOnline += 1;
    } else {
      counts.phones += 1;
      if (live.has(device.id)) counts.phonesOnline += 1;
    }
  }
  return counts;
}

/** The bot a new gadget starts on: the same rule as the companion's
 *  defaultGadgetBot (spec §4.3 rule 3), over the visible bots in server order. */
export function defaultGadgetBotId(
  bots: ReadonlyArray<{ id: string; hidden?: boolean; pinned?: boolean; section?: string; chiefOfStaff?: boolean }>,
): string | null {
  const visible = bots.filter((bot) => !bot.hidden);
  return (
    visible.find((bot) => bot.chiefOfStaff && !bot.section) ??
    visible.find((bot) => bot.pinned === true) ??
    visible.find((bot) => !bot.section && !bot.chiefOfStaff) ??
    visible[0]
  )?.id ?? null;
}
````

- [ ] **Step 4: Apply the renderer changes**

````bash
cd /Users/omkar/Desktop/openmaus/OpenGrokBot-gadget || exit 1
export PATH="$HOME/.nvm/versions/node/v24.14.1/bin:$PATH"
git apply <<'PATCH'
--- a/src/components/PhoneSetupFlow.tsx
+++ b/src/components/PhoneSetupFlow.tsx
@@ -61,6 +61,7 @@
 import { brand } from "../lib/brand";
 
 export interface PhoneDevice {
+  kind?: "phone";
   id: string;
   name: string;
   createdAt: number;
@@ -70,13 +71,36 @@
   browserControlAccess?: boolean;
 }
 
+export interface GadgetSensorsView { battery_pct?: number; charging?: boolean; [key: string]: unknown }
+
+/** A paired gadget (companion/src/devices.ts GadgetDeviceRecord). */
+export interface GadgetDevice {
+  kind: "gadget";
+  id: string;
+  name: string;
+  createdAt: number;
+  lastSeenAt: number;
+  publicKey: string;
+  board: string;
+  firmware: string;
+  botId: string | null;
+  speakPushes: boolean;
+  lastSensors?: GadgetSensorsView;
+  namePending?: true;
+}
+
+export type PairedDevice = PhoneDevice | GadgetDevice;
+export interface GadgetSettingsPatch { botId?: string | null; speakPushes?: boolean; name?: string }
+
 export interface CompanionState {
   enabled: boolean;
   keepAwake: boolean;
   port: number;
-  devices: PhoneDevice[];
+  devices: PairedDevice[];
   connectedDeviceIds?: string[];
-  pairing: { code: string; token: string; expiresAt: number } | null;
+  pairing: { code: string; token: string; expiresAt: number; botId?: string } | null;
+  /** The companion's stable id, as gadgets know it (companion/src/host-id.ts). */
+  hostId?: string;
   addresses?: string[];
   tailscale?: string;
   tailnetName?: string;
@@ -103,6 +127,9 @@
   cloudDesktop: (deviceId: string, allowed: boolean) => Promise<CompanionState>;
   browserControl: (deviceId: string, allowed: boolean) => Promise<CompanionState>;
   revoke: (deviceId: string) => Promise<CompanionState>;
+  gadget: (deviceId: string, patch: GadgetSettingsPatch) => Promise<CompanionState>;
+  pairGadget: (botId: string | null) => Promise<CompanionState>;
+  pairingBot: (botId: string | null, expectedToken: string) => Promise<CompanionState>;
 };
 
 type AccountBridge = NonNullable<NonNullable<Window["ogb"]>["companionAccount"]>;
@@ -211,6 +238,8 @@
   localFallback: boolean;
   tailscaleFallback: boolean;
   tailscaleAvailable: boolean;
+  /** The name of a gadget that enrolled while this flow was open, else null. */
+  pairedGadgetName: string | null;
   pairingExpired: boolean;
   setupTimedOut: boolean;
   setEmail: (email: string) => void;
@@ -751,7 +780,7 @@
   useEffect(() => {
     if (!state) return;
     const device = newlyPairedDeviceForFlow(flow, state.devices);
-    if (device) dispatchFlow({ type: "paired", deviceName: device.name });
+    if (device) dispatchFlow({ type: "paired", deviceName: device.name, deviceKind: device.kind === "gadget" ? "gadget" : "phone" });
   }, [flow, state]);
 
   useEffect(() => {
@@ -859,6 +888,7 @@
     localFallback: flow.localFallback,
     tailscaleFallback: flow.tailscaleFallback,
     tailscaleAvailable: Boolean(state && companionPairingRoute(state, "tailscale")),
+    pairedGadgetName: flow.pairedDeviceKind === "gadget" ? flow.pairedDeviceName : null,
     pairingExpired: flow.pairingAttempted && !state?.pairing,
     setupTimedOut,
     setEmail: (next) => {
@@ -1203,9 +1233,11 @@
         <div className="flex size-14 items-center justify-center rounded-full bg-success/15 text-success">
           <Check size={28} />
         </div>
-        <h2 className="mt-4 text-[19px] font-semibold text-ink">{t("phone.success.title")}</h2>
+        <h2 className="mt-4 text-[19px] font-semibold text-ink">
+          {c.pairedGadgetName ? t("remote.gadgets.pairedToast", { name: c.pairedGadgetName }) : t("phone.success.title")}
+        </h2>
         <p className="mt-1.5 text-[13px] text-ink-secondary">
-          {t("phone.success.detail")}
+          {c.pairedGadgetName ? t("remote.gadgets.pairedDetail") : t("phone.success.detail")}
         </p>
         <button
           onClick={() => {
PATCH
````

````bash
cd /Users/omkar/Desktop/openmaus/OpenGrokBot-gadget || exit 1
export PATH="$HOME/.nvm/versions/node/v24.14.1/bin:$PATH"
git apply <<'PATCH'
--- a/src/lib/phone-setup.ts
+++ b/src/lib/phone-setup.ts
@@ -11,6 +11,8 @@
   baselineDeviceIds: string[];
   pairingAttempted: boolean;
   pairedDeviceName: string | null;
+  /** A gadget can enroll while the phone flow is open; the success copy names it. */
+  pairedDeviceKind?: "phone" | "gadget";
   skipped: boolean;
 }
 
@@ -20,7 +22,7 @@
   | { type: "use-local" }
   | { type: "use-tailscale" }
   | { type: "pairing-opened"; deviceIds: string[] }
-  | { type: "paired"; deviceName: string }
+  | { type: "paired"; deviceName: string; deviceKind?: "phone" | "gadget" }
   | { type: "skip" }
   | { type: "reset" };
 
@@ -76,7 +78,7 @@
         pairingAttempted: true,
       };
     case "paired":
-      return { ...state, active: true, pairedDeviceName: event.deviceName };
+      return { ...state, active: true, pairedDeviceName: event.deviceName, pairedDeviceKind: event.deviceKind ?? "phone" };
     case "skip":
       return { ...initialPhoneSetupFlowState, skipped: true };
     case "reset":
PATCH
````

````bash
cd /Users/omkar/Desktop/openmaus/OpenGrokBot-gadget || exit 1
export PATH="$HOME/.nvm/versions/node/v24.14.1/bin:$PATH"
git apply <<'PATCH'
--- a/src/components/SidebarPhoneButton.tsx
+++ b/src/components/SidebarPhoneButton.tsx
@@ -2,6 +2,7 @@
 import { Plus, TabletSmartphone } from "lucide-react";
 
 import { cn } from "@/lib/cn";
+import { isGadget } from "@/lib/gadgets";
 import { t } from "@/lib/i18n";
 import { phonePairingSettingsAction } from "@/lib/phone-pairing";
 import type { SidebarDensity } from "@/lib/sidebar-preferences";
@@ -45,7 +46,10 @@
     return { kind: "unavailable", label: t("sidebar.phone.unavailable"), pairedCount: 0, connectedCount: 0 };
   }
 
-  const pairedCount = snapshot.devices.length;
+  // This is the phone button: gadgets have their own rows in Remote access
+  // and never count as a phone here.
+  const phones = snapshot.devices.filter((device) => !isGadget(device));
+  const pairedCount = phones.length;
   if (snapshot.error) {
     return {
       kind: "unavailable",
@@ -68,7 +72,7 @@
 
   if (Array.isArray(snapshot.connectedDeviceIds)) {
     const live = new Set(snapshot.connectedDeviceIds);
-    const connectedCount = snapshot.devices.filter((device) => live.has(device.id)).length;
+    const connectedCount = phones.filter((device) => live.has(device.id)).length;
     if (connectedCount) {
       const label = pairedCount === 1
         ? t("sidebar.phone.connectedOne")
@@ -90,7 +94,7 @@
 
   // Compatibility with a sidecar from an older unpackaged development build.
   // Recent activity stays neutral because it is not proof of a live stream.
-  const recentCount = snapshot.devices.filter((device) => {
+  const recentCount = phones.filter((device) => {
     const age = now - device.lastSeenAt;
     return Number.isFinite(device.lastSeenAt) && age >= 0 && age <= SIDEBAR_PHONE_RECENT_MS;
   }).length;
PATCH
````

````bash
cd /Users/omkar/Desktop/openmaus/OpenGrokBot-gadget || exit 1
export PATH="$HOME/.nvm/versions/node/v24.14.1/bin:$PATH"
git apply <<'PATCH'
--- a/src/components/CompanionSection.tsx
+++ b/src/components/CompanionSection.tsx
@@ -23,6 +23,7 @@
 import { revealPhonePairing } from "../lib/phone-pairing";
 import { ConnectionDetail } from "./ConnectionDetail";
 import { Card, Switch } from "./SettingsPrimitives";
+import { countDevicesByKind, isGadget } from "../lib/gadgets";
 import { brand } from "../lib/brand";
 import { useStore } from "@/state/store";
 
@@ -109,12 +110,15 @@
 ): CompanionPanelStatus | null {
   if (state.error) return { label: t("remote.status.attention"), good: false };
   if (!state.enabled) return { label: t("remote.status.off"), good: false };
-  const pairedCount = state.devices.length;
-  if (!pairedCount) return null;
-  return {
-    label: `${pairedCount} ${pairedCount === 1 ? "device" : "devices"} paired`,
-    good: true,
-  };
+  // Phones (and desktop apps) and gadgets are counted apart, so a gadget is
+  // never reported as a device that should be a phone.
+  const { phones, gadgets } = countDevicesByKind(state.devices, []);
+  if (!phones && !gadgets) return null;
+  const parts = [
+    phones ? (phones === 1 ? t("remote.status.devicesOne") : t("remote.status.devicesMany", { count: phones })) : null,
+    gadgets ? (gadgets === 1 ? t("remote.status.gadgetsOne") : t("remote.status.gadgetsMany", { count: gadgets })) : null,
+  ].filter((part): part is string => part !== null);
+  return { label: parts.join(" · "), good: true };
 }
 
 const relative = (at: number) => {
@@ -172,7 +176,8 @@
     );
   }
 
-  const pairedCount = state.devices.length;
+  const phones = state.devices.filter((device) => !isGadget(device));
+  const pairedCount = phones.length;
   const panelStatus = deriveCompanionPanelStatus(state);
   const accountActionError = companionAccountActionError(c.account, c.accountError);
   const pairingCopy = pairingSurfaceCopy(c);
@@ -246,7 +251,7 @@
             <div className="mt-3">
               {pairedCount > 0 && (
                 <ul className="flex flex-col gap-2">
-                  {state.devices.map((device) => (
+                  {phones.map((device) => (
                     <li key={device.id} className="rounded-xl bg-inset px-3 py-3">
                       <div className="flex items-center gap-3">
                         <div className="flex size-8 shrink-0 items-center justify-center rounded-lg bg-control text-ink-secondary">
PATCH
````

````bash
cd /Users/omkar/Desktop/openmaus/OpenGrokBot-gadget || exit 1
export PATH="$HOME/.nvm/versions/node/v24.14.1/bin:$PATH"
git apply <<'PATCH'
--- a/src/locales/en.json
+++ b/src/locales/en.json
@@ -2075,6 +2075,12 @@
   "remote.devices.allowBrowser": "Allow browser control",
   "remote.devices.allowBrowserDetail": "View and control signed-in browser sessions from this device.",
   "remote.devices.browserAria": "Browser control access for {name}",
+  "remote.gadgets.pairedToast": "{name} is paired",
+  "remote.gadgets.pairedDetail": "It talks to the bot shown under Gadgets in Remote access.",
+  "remote.status.devicesOne": "1 device paired",
+  "remote.status.devicesMany": "{count} devices paired",
+  "remote.status.gadgetsOne": "1 gadget paired",
+  "remote.status.gadgetsMany": "{count} gadgets paired",
   "remote.time.justNow": "just now",
   "remote.time.minutes": "{count} min ago",
   "remote.time.hours": "{count} h ago",
PATCH
````

````bash
cd /Users/omkar/Desktop/openmaus/OpenGrokBot-gadget || exit 1
export PATH="$HOME/.nvm/versions/node/v24.14.1/bin:$PATH"
git apply <<'PATCH'
--- a/src/components/PhoneSetupFlow.publicNetwork.test.ts
+++ b/src/components/PhoneSetupFlow.publicNetwork.test.ts
@@ -38,6 +38,7 @@
   localFallback: true,
   tailscaleFallback: false,
   tailscaleAvailable: false,
+  pairedGadgetName: null,
   pairingExpired: false,
   setupTimedOut: false,
   setEmail: noop,
PATCH
````

- [ ] **Step 5: Run the tests and the checks**

```bash
cd /Users/omkar/Desktop/openmaus/OpenGrokBot-gadget || exit 1
export PATH="$HOME/.nvm/versions/node/v24.14.1/bin:$PATH"
pnpm exec vitest run src/lib/gadgets.test.ts src/components/PhoneSetupFlow.gadget.test.ts src/components/SidebarPhoneButton.test.ts src/lib/phone-setup.test.ts src/components/CompanionSection.test.ts src/components/CompanionSection.reveal.test.ts src/components/PhoneSetupFlow.publicNetwork.test.ts src/locales
pnpm typecheck  # about 3 minutes: give this Bash call a timeout of at least 10 minutes
pnpm exec oxlint --deny-warnings src
pnpm i18n:check
```

Expected: all pass (`gadgets` 4, `PhoneSetupFlow.gadget` 1, `SidebarPhoneButton` 10, `phone-setup` 26, `CompanionSection` 18, `reveal` 4, `publicNetwork` 2, plus the locale tests); `pnpm typecheck` prints nothing; oxlint and `i18n:check` exit 0.

- [ ] **Step 6: Commit**

```bash
cd /Users/omkar/Desktop/openmaus/OpenGrokBot-gadget || exit 1
export PATH="$HOME/.nvm/versions/node/v24.14.1/bin:$PATH"
git add src/lib/gadgets.ts src/lib/gadgets.test.ts src/components/PhoneSetupFlow.tsx src/components/PhoneSetupFlow.gadget.test.ts src/components/PhoneSetupFlow.publicNetwork.test.ts src/lib/phone-setup.ts src/lib/phone-setup.test.ts src/components/SidebarPhoneButton.tsx src/components/SidebarPhoneButton.test.ts src/components/CompanionSection.tsx src/components/CompanionSection.test.ts src/locales/en.json && git commit -m "feat(desktop): count gadgets apart from phones and name a paired gadget

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 17: The Pair a gadget panel and gadget rows

**Files:**
- Create: `src/components/PairGadgetPanel.tsx`, `src/components/GadgetRow.tsx`
- Modify: `src/components/CompanionSection.tsx` (Task 16 version: imports, `store.state.bots`, gadget lists, the Gadgets card after the pairing-flow card and before `<details>`)
- Modify: `src/components/SettingsModal.tsx` (origin/main :83 keywords)
- Modify: `src/locales/en.json` (the remaining `remote.gadgets.*` keys)
- Test: `src/components/CompanionSection.gadget.test.ts`

**Interfaces:**
- Consumes: `PhoneSetupController.act/secondsLeft/busy`, `CompanionBridge.pairGadget/pairingBot/gadget/revoke/pairing` (Tasks 15, 16), `defaultGadgetBotId`, `isGadget`, `boardLabel` (Task 16), `useStore().state.bots`.
- Produces (contract §3.18): `PairGadgetPanel({state, bots, busy, secondsLeft, act, blocked})`. Its open window is drawn by the exported `PairGadgetCode({pairing, state, secondsLeft, busy, onCancel})` with the six digits large, "Code expires in {seconds} s", the pairing body, the LAN address `lan:port`, the existing Public-network hint (`phone.code.publicNetwork`), and "Stop pairing"; a static render can test it on its own. The "Talks to" picker starts on `defaultGadgetBotId` and calls `pairingBot` while the panel's own window is open. The button is disabled with "Turn on Remote access first" while the companion is off, and also under the org policy. `GadgetRow({device, online, bots, busy, onBot, onSpeakPushes, onRename?, onRemove, updateCell?})` shows the icon, the name, `boardLabel · firmware`, battery or charging, the online dot, Talks to, Read pushes aloud, `updateCell` and Remove, with no phone grants. The name is an inline `<input>` (`maxLength` 32, aria-label "Name of {name}"), saved through `onRename` on Enter or blur when it changed and is not empty: spec §4.3 and §6.4 have the desktop rename a gadget through `PATCH /devices/:id/gadget`, which reaches a live gadget as `settings {name}` and an offline one at its next `hello`. `CompanionSection` wires `onRename` to `companion.gadget(id, {name})`. `GadgetBotOption` is the bot shape both components take. In `CompanionSection`, the Gadgets card sits directly under the phone pairing card, outside "Advanced & troubleshooting", and the phone rows stay in Advanced.

- [ ] **Step 1: Write the failing test**

Create `src/components/CompanionSection.gadget.test.ts`. Besides the rows and the picker, it checks the gadget's name field, and it renders `PairGadgetCode` on its own: the panel only shows a code after a click, which a static render cannot make, and spec §10 asks for a static test of the open panel.

````ts
import { createElement } from "react";
import { renderToStaticMarkup } from "react-dom/server";
import { beforeEach, describe, expect, it, vi } from "vitest";

import { t } from "@/lib/i18n";
import type { CompanionState, GadgetDevice } from "./PhoneSetupFlow";

// Settings → Remote access with gadgets (spec §6.4): a Pair a gadget button
// with a bot picker, a Gadgets group outside Advanced, gadget rows without
// phone-only grants, and status counts by kind.
const f = vi.hoisted(() => ({ state: null as object | null }));
vi.mock("../lib/phone-pairing", () => ({ revealPhonePairing: () => false }));
vi.mock("@/state/store", () => ({
  useStore: () => ({
    state: {
      config: {},
      bots: [
        { id: "b1", name: "Jev", hidden: false },
        { id: "b2", name: "Ops", chiefOfStaff: true },
        { id: "b3", name: "Hidden", hidden: true },
      ],
    },
  }),
}));
vi.mock("./PhoneSetupFlow", () => ({
  companionBridge: () => ({}),
  usePhoneSetupController: () => ({ state: f.state, busy: false, accountBusy: false, account: null, accountError: null, error: null, localFallback: false, tailscaleFallback: false, tailscaleAvailable: false, hostedReady: false, secondsLeft: 90, act: async () => {} }),
  PhoneSetupFlowView: () => null,
  companionAccountActionError: () => null,
  loadCompanionBridgeState: () => null,
  shouldHydrateCompanionEmail: () => false,
}));
import { CompanionSection } from "./CompanionSection";
import { PairGadgetCode } from "./PairGadgetPanel";

const gadget: GadgetDevice = {
  kind: "gadget", id: "gad_3f9a0c2b7e41d856", name: "Desk Maus", createdAt: 1, lastSeenAt: Date.now(),
  publicKey: "BHx", board: "amoled-175c", firmware: "1.0.0", botId: "b1", speakPushes: false, lastSensors: { battery_pct: 82 },
};
const phone = { id: "phone-1", name: "Ada", createdAt: 1, lastSeenAt: Date.now(), cloudDesktopAccess: false };
const base = (extra: Partial<CompanionState> = {}): CompanionState => ({ enabled: true, keepAwake: false, port: 8810, devices: [], pairing: null, ...extra });
const render = () => renderToStaticMarkup(createElement(() => CompanionSection({})));
const advanced = (markup: string) => markup.slice(markup.indexOf("<details"));
const outside = (markup: string) => markup.slice(0, markup.indexOf("<details"));

beforeEach(() => { f.state = null; });

describe("gadgets in Remote access", () => {
  it("lists a gadget outside Advanced, with its bot, board, battery and online dot, and no phone grants", () => {
    f.state = base({ devices: [gadget, phone], connectedDeviceIds: [gadget.id] });
    const markup = render();
    const top = outside(markup);
    expect(top).toContain(">Gadgets<");
    expect(top).toContain("Desk Maus");
    // The name can be changed here (spec §4.3, §6.4): an enabled field, saved on Enter or blur.
    const nameField = top.match(/<input[^>]*aria-label="Name of Desk Maus"[^>]*>/)?.[0] ?? "";
    expect(nameField).toContain('value="Desk Maus"');
    expect(nameField).toMatch(/maxLength="32"/i);
    expect(nameField).not.toContain("disabled");
    expect(top).toContain("ESP32-S3 AMOLED 1.75C · firmware 1.0.0");
    expect(top).toContain("Battery 82%");
    expect(top).toContain(">Online<");
    const select = top.match(/<select[^>]*aria-label="Bot that Desk Maus talks to"[^>]*>[\s\S]*?<\/select>/)?.[0] ?? "";
    expect(select).toMatch(/<option value="b1" selected="">Jev<\/option>/);
    expect(select).not.toContain("Hidden");
    expect(top).toMatch(/<button[^>]*aria-label="Read pushes aloud on Desk Maus"[^>]*aria-checked="false"/);
    expect(markup).not.toContain('aria-label="Computer view access for Desk Maus"');
    expect(markup).not.toContain('aria-label="Browser control access for Desk Maus"');
    // the phone row stays in Advanced with its grants
    expect(advanced(markup)).toContain('aria-label="Computer view access for Ada"');
    expect(advanced(markup)).not.toContain("Desk Maus");
  });

  it("starts the picker on the default bot and enables Pair a gadget only while Remote access is on", () => {
    f.state = base();
    const on = render();
    const picker = outside(on).match(/<select[^>]*>[\s\S]*?<\/select>/)?.[0] ?? "";
    expect(picker).toMatch(/<option value="b2" selected="">Ops<\/option>/);
    expect(on).toMatch(/<button(?![^>]*\sdisabled="")[^>]*>Pair a gadget<\/button>/);
    expect(on).toContain("No gadgets are paired yet.");
    f.state = base({ enabled: false });
    const off = render();
    expect(off).toMatch(/<button[^>]*\sdisabled=""[^>]*>Pair a gadget<\/button>/);
    expect(off).toContain("Turn on Remote access first");
  });

  it("marks an unknown or missing bot instead of showing a wrong one", () => {
    f.state = base({ devices: [{ ...gadget, botId: "b_gone" }] });
    const select = render().match(/<select[^>]*aria-label="Bot that Desk Maus talks to"[^>]*>[\s\S]*?<\/select>/)?.[0] ?? "";
    expect(select).toMatch(/<option value="b_gone" selected="">Unknown bot<\/option>/);
  });
});

describe("the pairing code", () => {
  const escapeHtml = (text: string) =>
    text.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;").replace(/"/g, "&quot;").replace(/'/g, "&#x27;");

  it("shows the six digits, the countdown, the address to type and the Public-network hint", () => {
    const markup = renderToStaticMarkup(createElement(PairGadgetCode, {
      pairing: { code: "123456" },
      state: { lan: "192.168.1.20", port: 8810, publicNetwork: "Wi-Fi" },
      secondsLeft: 90,
      busy: false,
      onCancel: () => {},
    }));
    expect(markup).toContain(">123456<");
    expect(markup).toContain("Code expires in 90 s");
    expect(markup).toContain("enter 192.168.1.20:8810");
    expect(markup).toContain(escapeHtml(t("phone.code.publicNetwork", { network: "Wi-Fi" })));
    expect(markup).toContain(">Stop pairing<");
  });
});
````

- [ ] **Step 2: Run it to see it fail**

Run: `cd /Users/omkar/Desktop/openmaus/OpenGrokBot-gadget && PATH="$HOME/.nvm/versions/node/v24.14.1/bin:$PATH" pnpm exec vitest run src/components/CompanionSection.gadget.test.ts`
Expected: FAIL — `Error: Cannot find module '/src/components/PairGadgetPanel' imported from …/CompanionSection.gadget.test.ts`. The test imports `PairGadgetCode` directly, so no test runs until Step 3 has created that file. After Steps 3–5 it finds `>Gadgets<`, the button and the gadget row.

- [ ] **Step 3: Write `src/components/PairGadgetPanel.tsx`**

````tsx
// "Pair a gadget" in Settings → Remote access (spec §6.4, A2): opens the
// shared pairing window with the bot the new gadget will talk to, and shows
// the six digits large, with the address to type when discovery fails.
import { useState } from "react";
import { t } from "@/lib/i18n";
import { defaultGadgetBotId, isGadget } from "../lib/gadgets";
import type { CompanionState, PhoneSetupController } from "./PhoneSetupFlow";

export interface GadgetBotOption {
  id: string;
  name: string;
  hidden?: boolean;
  pinned?: boolean;
  section?: string;
  chiefOfStaff?: boolean;
}

export function PairGadgetPanel({
  state,
  bots,
  busy,
  secondsLeft,
  act,
  blocked,
}: {
  state: CompanionState;
  bots: readonly GadgetBotOption[];
  busy: boolean;
  secondsLeft: number;
  act: PhoneSetupController["act"];
  blocked: string | null;
}) {
  const visible = bots.filter((bot) => !bot.hidden);
  const [botId, setBotId] = useState<string | null>(() => defaultGadgetBotId(visible));
  // The gadget ids present when this panel opened its window: a new id is
  // the gadget that just paired. Null until the button is pressed.
  const [baseline, setBaseline] = useState<string[] | null>(null);
  const gadgets = state.devices.filter(isGadget);
  const pairing = baseline && state.pairing ? state.pairing : null;
  const paired = baseline && !state.pairing ? gadgets.find((gadget) => !baseline.includes(gadget.id)) : undefined;
  const known = botId === null || visible.some((bot) => bot.id === botId);

  const choose = (next: string) => {
    const value = next || null;
    setBotId(value);
    if (pairing) void act((companion) => companion.pairingBot(value, pairing.token));
  };

  return (
    <div className="flex flex-col gap-3">
      <div className="flex flex-wrap items-center justify-between gap-3">
        <label className="flex min-w-0 items-center gap-2 text-[12.5px]">
          <span className="text-ink-secondary">{t("remote.gadgets.talksTo")}</span>
          <select
            value={botId ?? ""}
            disabled={busy}
            onChange={(event) => choose(event.target.value)}
            className="min-w-0 rounded-md border border-hairline/60 bg-panel px-2 py-1 text-ink outline-none"
          >
            {!known && <option value={botId ?? ""}>{t("remote.gadgets.unknownBot")}</option>}
            {visible.map((bot) => <option key={bot.id} value={bot.id}>{bot.name}</option>)}
          </select>
        </label>
        <button
          disabled={busy || !state.enabled || Boolean(blocked)}
          title={blocked ?? (state.enabled ? undefined : t("remote.gadgets.pairNeedsRemote"))}
          onClick={() => {
            setBaseline(gadgets.map((gadget) => gadget.id));
            void act((companion) => companion.pairGadget(botId));
          }}
          className="rounded-lg border border-hairline/40 px-3 py-1.5 text-[12px] text-ink hover:bg-control disabled:opacity-40"
        >
          {t("remote.gadgets.pair")}
        </button>
      </div>
      {!state.enabled && <div className="text-[11.5px] text-ink-secondary">{t("remote.gadgets.pairNeedsRemote")}</div>}
      {pairing && (
        <PairGadgetCode
          pairing={pairing}
          state={state}
          secondsLeft={secondsLeft}
          busy={busy}
          onCancel={() => void act((companion) => companion.pairing(false, pairing.token))}
        />
      )}
      {paired && (
        <div role="status" className="rounded-lg bg-success/10 px-3 py-2 text-[12px] text-success">
          {t("remote.gadgets.pairedToast", { name: paired.name })}
        </div>
      )}
    </div>
  );
}

/** The open window: the six digits large, the countdown, where to type them,
 *  the address for when discovery fails, the Public-network hint and Stop. */
export function PairGadgetCode({
  pairing,
  state,
  secondsLeft,
  busy,
  onCancel,
}: {
  pairing: { code: string };
  state: Pick<CompanionState, "lan" | "port" | "publicNetwork">;
  secondsLeft: number;
  busy: boolean;
  onCancel: () => void;
}) {
  return (
    <div className="rounded-xl bg-inset px-4 py-4 text-center" aria-live="polite">
      <div className="font-mono text-[34px] font-semibold tracking-[0.3em] text-ink" aria-label={pairing.code.split("").join(" ")}>
        {pairing.code}
      </div>
      <div className="mt-1 text-[11.5px] text-ink-secondary">{t("remote.gadgets.codeExpires", { seconds: secondsLeft })}</div>
      <p className="mt-2 text-[12.5px] text-ink">{t("remote.gadgets.pairBody")}</p>
      {state.lan && (
        <p className="mt-1 text-[11.5px] text-ink-secondary">
          {t("remote.gadgets.address", { address: `${state.lan}:${state.port}` })}
        </p>
      )}
      {state.publicNetwork && (
        <p className="mt-2 text-[11.5px] text-warning">{t("phone.code.publicNetwork", { network: state.publicNetwork })}</p>
      )}
      <button
        disabled={busy}
        onClick={onCancel}
        className="mt-3 text-[12px] text-ink-secondary hover:text-ink disabled:opacity-40"
      >
        {t("remote.gadgets.cancel")}
      </button>
    </div>
  );
}
````

- [ ] **Step 4: Write `src/components/GadgetRow.tsx`**

````tsx
// One paired gadget in Settings → Remote access (spec §6.4): its name (which
// can be changed here), what it is, if it is online, which bot it talks to,
// whether it reads pushes aloud, and Remove. It has none of the phone-only
// grants.
import type { ReactNode } from "react";
import { BatteryCharging, BatteryMedium, Cpu, Trash2 } from "lucide-react";
import { t } from "@/lib/i18n";
import { boardLabel } from "../lib/gadgets";
import type { GadgetBotOption } from "./PairGadgetPanel";
import type { GadgetDevice } from "./PhoneSetupFlow";
import { Switch } from "./SettingsPrimitives";

/** The companion's name limit (PATCH /devices/:id/gadget, NAME_MAX_CHARS). */
const NAME_MAX = 32;

export function GadgetRow({
  device,
  online,
  bots,
  busy,
  onBot,
  onSpeakPushes,
  onRename,
  onRemove,
  updateCell,
}: {
  device: GadgetDevice;
  online: boolean;
  bots: readonly GadgetBotOption[];
  busy: boolean;
  onBot: (botId: string | null) => void;
  onSpeakPushes: () => void;
  /** A desktop rename (spec §4.3, last writer wins): a live gadget gets
   *  `settings {name}` at once, an offline one at its next hello. */
  onRename?: (name: string) => void;
  onRemove: () => void;
  /** The firmware update cell (OTA delivery plan); rendered after the switches. */
  updateCell?: ReactNode;
}) {
  const visible = bots.filter((bot) => !bot.hidden);
  const known = device.botId !== null && visible.some((bot) => bot.id === device.botId);
  const battery = typeof device.lastSensors?.battery_pct === "number" ? Math.round(device.lastSensors.battery_pct) : null;
  const charging = device.lastSensors?.charging === true;
  return (
    <li className="rounded-xl bg-inset px-3 py-3">
      <div className="flex items-center gap-3">
        <div className="flex size-8 shrink-0 items-center justify-center rounded-lg bg-control text-ink-secondary">
          <Cpu size={15} />
        </div>
        <div className="min-w-0 flex-1">
          <div className="flex items-center gap-2">
            {/* Keyed by the saved name, so a rename from anywhere resets the field. */}
            <input
              key={device.name}
              defaultValue={device.name}
              maxLength={NAME_MAX}
              disabled={busy || !onRename}
              aria-label={t("remote.gadgets.nameAria", { name: device.name })}
              onKeyDown={(event) => {
                if (event.key === "Enter") event.currentTarget.blur();
              }}
              onBlur={(event) => {
                const next = event.currentTarget.value.trim();
                if (next && next !== device.name) onRename?.(next);
                else event.currentTarget.value = device.name;
              }}
              className="min-w-0 flex-1 truncate rounded bg-transparent text-[13.5px] font-medium text-ink outline-none focus:bg-panel focus:px-1"
            />
            <span className="flex shrink-0 items-center gap-1 text-[11px] text-ink-secondary">
              <span className={`size-1.5 rounded-full ${online ? "bg-success" : "bg-ink-secondary/50"}`} />
              {online ? t("remote.gadgets.online") : t("remote.gadgets.offline")}
            </span>
          </div>
          <div className="text-[11.5px] text-ink-secondary">
            {t("remote.gadgets.firmware", { board: boardLabel(device.board), version: device.firmware })}
          </div>
          {(battery !== null || charging) && (
            <div className="mt-0.5 flex items-center gap-1 text-[11.5px] text-ink-secondary">
              {charging ? <BatteryCharging size={13} /> : <BatteryMedium size={13} />}
              {charging ? t("remote.gadgets.charging") : t("remote.gadgets.battery", { percent: battery ?? 0 })}
            </div>
          )}
        </div>
        <button
          disabled={busy}
          onClick={onRemove}
          aria-label={t("remote.gadgets.remove", { name: device.name })}
          className="shrink-0 rounded p-1.5 text-ink-secondary hover:bg-control hover:text-danger disabled:opacity-40"
        >
          <Trash2 size={14} />
        </button>
      </div>
      <label className="mt-3 flex items-center justify-between gap-3 border-t border-hairline/30 pt-3">
        <span className="text-[12px] text-ink">{t("remote.gadgets.talksTo")}</span>
        <select
          value={device.botId ?? ""}
          disabled={busy}
          aria-label={t("remote.gadgets.talksToAria", { name: device.name })}
          onChange={(event) => onBot(event.target.value || null)}
          className="min-w-0 rounded-md border border-hairline/60 bg-panel px-2 py-1 text-[12px] text-ink outline-none"
        >
          {!known && <option value={device.botId ?? ""}>{t("remote.gadgets.unknownBot")}</option>}
          {visible.map((bot) => <option key={bot.id} value={bot.id}>{bot.name}</option>)}
        </select>
      </label>
      <div className="mt-3 flex items-center justify-between gap-3 border-t border-hairline/30 pt-3">
        <div>
          <div className="text-[12px] text-ink">{t("remote.gadgets.speakPushes")}</div>
          <div className="mt-0.5 text-[11px] text-ink-secondary">{t("remote.gadgets.speakPushesDetail")}</div>
        </div>
        <Switch
          checked={device.speakPushes}
          aria-label={t("remote.gadgets.speakPushesAria", { name: device.name })}
          disabled={busy}
          onClick={onSpeakPushes}
        />
      </div>
      {updateCell}
    </li>
  );
}
````

- [ ] **Step 5: Add the Gadgets card, the keywords and the copy**

````bash
cd /Users/omkar/Desktop/openmaus/OpenGrokBot-gadget || exit 1
export PATH="$HOME/.nvm/versions/node/v24.14.1/bin:$PATH"
git apply <<'PATCH'
--- a/src/components/CompanionSection.tsx
+++ b/src/components/CompanionSection.tsx
@@ -22,6 +22,8 @@
 import { companionPairingMode } from "../lib/phone-setup";
 import { revealPhonePairing } from "../lib/phone-pairing";
 import { ConnectionDetail } from "./ConnectionDetail";
+import { GadgetRow } from "./GadgetRow";
+import { PairGadgetPanel } from "./PairGadgetPanel";
 import { Card, Switch } from "./SettingsPrimitives";
 import { countDevicesByKind, isGadget } from "../lib/gadgets";
 import { brand } from "../lib/brand";
@@ -155,7 +157,9 @@
   }, [focusRequest, loaded]);
   // An enrolled organisation can turn remote access off; the desktop refuses
   // new pairing and turning the companion on. Existing devices are listed as before.
-  const managedPolicy = useStore().state.config?.managedPolicy;
+  const store = useStore();
+  const managedPolicy = store.state.config?.managedPolicy;
+  const bots = store.state.bots ?? [];
   const remoteBlocked = managedPolicy?.remoteAccess === false ? t("policy.remoteBlocked", { organization: managedPolicy.organizationName }) : null;
   const managedBy = managedPolicy?.remoteAccess === false ? t("policy.managedBy", { organization: managedPolicy.organizationName }) : null;
 
@@ -177,6 +181,8 @@
   }
 
   const phones = state.devices.filter((device) => !isGadget(device));
+  const gadgets = state.devices.filter(isGadget);
+  const connected = new Set(state.connectedDeviceIds ?? []);
   const pairedCount = phones.length;
   const panelStatus = deriveCompanionPanelStatus(state);
   const accountActionError = companionAccountActionError(c.account, c.accountError);
@@ -224,6 +230,29 @@
           <PhoneSetupFlowView controller={c} variant="settings" />
         </Card>
       </div>
+
+      <Card title={t("remote.gadgets.title")}>
+        <PairGadgetPanel state={state} bots={bots} busy={c.busy} secondsLeft={c.secondsLeft} act={c.act} blocked={remoteBlocked} />
+        {gadgets.length > 0 ? (
+          <ul className="mt-4 flex flex-col gap-2">
+            {gadgets.map((gadget) => (
+              <GadgetRow
+                key={gadget.id}
+                device={gadget}
+                online={connected.has(gadget.id)}
+                bots={bots}
+                busy={c.busy}
+                onBot={(botId) => void c.act((companion) => companion.gadget(gadget.id, { botId }))}
+                onSpeakPushes={() => void c.act((companion) => companion.gadget(gadget.id, { speakPushes: !gadget.speakPushes }))}
+                onRename={(name) => void c.act((companion) => companion.gadget(gadget.id, { name }))}
+                onRemove={() => void c.act((companion) => companion.revoke(gadget.id))}
+              />
+            ))}
+          </ul>
+        ) : (
+          <div className="mt-3 text-[12px] text-ink-secondary">{t("remote.gadgets.empty")}</div>
+        )}
+      </Card>
 
       <details className="rounded-xl border border-hairline/40 bg-card">
         <summary className="cursor-pointer px-4 py-3.5 text-[13px] font-medium text-ink">
PATCH
````

````bash
cd /Users/omkar/Desktop/openmaus/OpenGrokBot-gadget || exit 1
export PATH="$HOME/.nvm/versions/node/v24.14.1/bin:$PATH"
git apply <<'PATCH'
--- a/src/components/SettingsModal.tsx
+++ b/src/components/SettingsModal.tsx
@@ -80,7 +80,7 @@
 }> = [
   { id: "general", group: "you", labelKey: "settings.section.general", icon: User, keywords: ["profile", "name", "email", "about me", "about", "suggestions", "suggested", "memory", "analytics", "updates", "effort", "new bots", "reasoning", "threads", "parallel", "concurrency", "cleanup", "retention", "event log", "event-log", "log size", "automatic recovery", "backup model", "fallback", "routines", "conversation", "schedule"] },
   { id: "appearance", group: "you", labelKey: "settings.section.appearance", icon: Palette, keywords: ["skin", "theme", "appearance", "tools", "tool calls", "threads", "show threads", "hide threads", "sidebar", "density", "compact", "comfortable", "avatars", "display", "run", "this run", "run card", "commands", "notifications", "sound", "sounds", "mute", "silent", "chime", "pinned", "circles", "universal", "groups", "top"] },
-  { id: "companion", group: "you", labelKey: "settings.section.companion", icon: TabletSmartphone, keywords: ["companion", "device", "phone", "desktop", "client", "host", "pair", "pairing", "mobile", "https", "secure", "tailscale", "wifi", "remote", "advanced", "domain", "dns", "self-hosted", "server", "caddy"] },
+  { id: "companion", group: "you", labelKey: "settings.section.companion", icon: TabletSmartphone, keywords: ["companion", "device", "phone", "gadget", "gadgets", "esp32", "desktop", "client", "host", "pair", "pairing", "mobile", "https", "secure", "tailscale", "wifi", "remote", "advanced", "domain", "dns", "self-hosted", "server", "caddy"] },
   { id: "engines", group: "ai", labelKey: "settings.section.engines", icon: Terminal, keywords: ["models", "model providers", "engines", "claude", "codex", "grok", "providers", "cli", "sign in", "subscription"] },
   { id: "connections", group: "ai", labelKey: "settings.section.connections", icon: KeyRound, keywords: ["keys", "api", "api key", "api keys", "connections", "composio", "box", "xai", "mistral", "cerebras", "vps", "router", "openrouter", "base url", "openai", "anthropic", "groq", "opencode", "provider"] },
   { id: "decisionModel", group: "ai", labelKey: "settings.section.decisionModel", icon: Zap, keywords: ["decision", "jev", "typesafe", "routing", "auto", "rooms", "who answers"] },
PATCH
````

````bash
cd /Users/omkar/Desktop/openmaus/OpenGrokBot-gadget || exit 1
export PATH="$HOME/.nvm/versions/node/v24.14.1/bin:$PATH"
git apply <<'PATCH'
--- a/src/locales/en.json
+++ b/src/locales/en.json
@@ -2075,8 +2075,29 @@
   "remote.devices.allowBrowser": "Allow browser control",
   "remote.devices.allowBrowserDetail": "View and control signed-in browser sessions from this device.",
   "remote.devices.browserAria": "Browser control access for {name}",
+  "remote.gadgets.title": "Gadgets",
+  "remote.gadgets.pair": "Pair a gadget",
+  "remote.gadgets.pairNeedsRemote": "Turn on Remote access first",
+  "remote.gadgets.pairBody": "Enter this code in the gadget installer or on the gadget's console.",
+  "remote.gadgets.codeExpires": "Code expires in {seconds} s",
+  "remote.gadgets.address": "If the installer can't find this computer, enter {address}",
+  "remote.gadgets.talksTo": "Talks to",
+  "remote.gadgets.talksToAria": "Bot that {name} talks to",
+  "remote.gadgets.unknownBot": "Unknown bot",
+  "remote.gadgets.empty": "No gadgets are paired yet.",
+  "remote.gadgets.online": "Online",
+  "remote.gadgets.offline": "Offline",
+  "remote.gadgets.firmware": "{board} · firmware {version}",
+  "remote.gadgets.battery": "Battery {percent}%",
+  "remote.gadgets.charging": "Charging",
+  "remote.gadgets.speakPushes": "Read pushes aloud",
+  "remote.gadgets.speakPushesDetail": "Speak routine results and messages the bot sends on its own.",
+  "remote.gadgets.speakPushesAria": "Read pushes aloud on {name}",
+  "remote.gadgets.remove": "Remove {name}",
+  "remote.gadgets.nameAria": "Name of {name}",
   "remote.gadgets.pairedToast": "{name} is paired",
   "remote.gadgets.pairedDetail": "It talks to the bot shown under Gadgets in Remote access.",
+  "remote.gadgets.cancel": "Stop pairing",
   "remote.status.devicesOne": "1 device paired",
   "remote.status.devicesMany": "{count} devices paired",
   "remote.status.gadgetsOne": "1 gadget paired",
PATCH
````

- [ ] **Step 6: Run the tests and the checks**

```bash
cd /Users/omkar/Desktop/openmaus/OpenGrokBot-gadget || exit 1
export PATH="$HOME/.nvm/versions/node/v24.14.1/bin:$PATH"
pnpm exec vitest run src/components/CompanionSection.gadget.test.ts src/components/CompanionSection.reveal.test.ts src/components/CompanionSection.test.ts src/components/SettingsModal src/locales
pnpm typecheck  # about 3 minutes: give this Bash call a timeout of at least 10 minutes
pnpm exec oxlint --deny-warnings src
pnpm i18n:check
```

Expected: all pass (`CompanionSection.gadget` 4, `reveal` 4, `CompanionSection` 18, plus any SettingsModal and locale tests); `pnpm typecheck` prints nothing; oxlint and `i18n:check` exit 0.

- [ ] **Step 7: Look at it in the dev app (manual, Omkar or the executing agent)**

Unset `ELECTRON_RUN_AS_NODE` before launching Electron (the app otherwise exits at once). Use the repo's test-locally / OMB2 side-by-side recipe, never the installed `/Applications/OpenMausBot.app`. Then open Settings → Remote access and check five things:

- The Gadgets card sits under the phone pairing card.
- Pair a gadget is disabled with "Turn on Remote access first" while Remote access is off.
- With Remote access on, pressing it shows six large digits, a countdown, the `lan:8810` address and the picker.
- The phone list still sits inside Advanced & troubleshooting.
- With a gadget paired (the SDK simulator or a test gadget will do), typing a new name in its row and pressing Enter keeps the new name after the next refresh.

Record what you saw in the PR description draft.

- [ ] **Step 8: Commit**

```bash
cd /Users/omkar/Desktop/openmaus/OpenGrokBot-gadget || exit 1
export PATH="$HOME/.nvm/versions/node/v24.14.1/bin:$PATH"
git add src/components/PairGadgetPanel.tsx src/components/GadgetRow.tsx src/components/CompanionSection.tsx src/components/CompanionSection.gadget.test.ts src/components/SettingsModal.tsx src/locales/en.json && git commit -m "feat(desktop): Pair a gadget panel and gadget rows in Remote access

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 18: Vendor the protocol vectors (blocked on P1)

P1 writes `protocol/vectors/` in the SDK. Contract §1.3 puts P1 on branch `p1-protocol`, but when this plan was revised that branch did not exist and P1's vectors were committed on the SDK's `main` (b8fe3eb). So this task reads `p1-protocol` when the branch exists and `main` otherwise. It runs last; until it runs, Tasks 1 and 8 pin the contract §1.7 values directly.

**Files:**
- Create: `companion/test/fixtures/gadget-vectors/*.json`, `companion/test/fixtures/gadget-vectors/SHA256SUMS` (byte-exact copies), `companion/test/fixtures/gadget-vectors/SOURCE` (the SDK commit, 40 hex + LF)
- Modify: `.gitattributes` (append the `-text` rule)
- Test: `companion/test/gadget/vectors.test.ts`

**Interfaces:**
- Consumes: the vector files of contract §4.4 (`identity.json`, `rfc6979.json`, `prove.json`, `der.json`, `base64.json`, `frames.json`, `firmware.json`, `versions.json`, `SHA256SUMS`); `checkHello` (Task 8), Task 1 helpers.
- Produces: the vendored folder that P4b's tests read (`firmware.json`, `versions.json`).

- [ ] **Step 1: Check that P1's vectors exist**

```bash
cd /Users/omkar/Desktop/openmaus/OpenGrokBot-gadget || exit 1
export PATH="$HOME/.nvm/versions/node/v24.14.1/bin:$PATH"
SDK=/Users/omkar/Desktop/openmaus/openmausbot-gadget-sdk
REF=$(git -C "$SDK" rev-parse --verify -q p1-protocol || git -C "$SDK" rev-parse --verify -q main)
echo "vectors from ${REF:-nowhere}"
git -C "$SDK" cat-file -e "$REF:protocol/vectors/SHA256SUMS" && git -C "$SDK" show "$REF:protocol/vectors/SHA256SUMS"
```

Expected: `vectors from <40 hex>` and the eight `SHA256SUMS` lines. If `REF` is empty or `cat-file` fails, stop here and report "Task 18 blocked: the SDK has no protocol/vectors yet". Everything else in the branch is complete without it.

- [ ] **Step 2: Write the test**

````ts
// The SDK's protocol vectors, vendored byte for byte (contract §3.19, §4.4):
// the same files the firmware's C tests and the fake host read. SHA256SUMS
// proves the copy is exact; every case runs through the host's own code.
import { Buffer } from "node:buffer";
import { createECDH, createHash } from "node:crypto";
import { readdirSync, readFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";
import { describe, expect, it } from "vitest";

import { checkHello } from "../../src/gadget/enroll.ts";
import {
  decodeBinary,
  decodePubkey,
  firmwareText,
  gadgetIdFromPubkey,
  HOST_ID_RE,
  isCanonicalBase64,
  proveText,
  verifyP256,
  type HelloMsg,
} from "../../src/gadget/protocol.ts";

const DIR = join(dirname(fileURLToPath(import.meta.url)), "..", "fixtures", "gadget-vectors");
const bytes = (name: string) => readFileSync(join(DIR, name));
const load = <T>(name: string): { vectors: string; version: number; cases: T[] } => JSON.parse(bytes(name).toString("utf8"));
const hex = (value: string) => Buffer.from(value, "hex");
const pubFromPrivate = (privateKeyHex: string) => {
  const ecdh = createECDH("prime256v1");
  ecdh.setPrivateKey(hex(privateKeyHex));
  return ecdh.getPublicKey();
};
const hello = (id: string, pubkey: string): HelloMsg => ({ op: "hello", proto: 1, id, pubkey, name: "v", board: "amoled-175c", fw: "1.0.0", caps: {} });

describe("vendored vector files", () => {
  it("match SHA256SUMS byte for byte, and SHA256SUMS lists every file in order", () => {
    const sums = bytes("SHA256SUMS").toString("utf8");
    expect(sums.endsWith("\n")).toBe(true);
    const entries = sums.slice(0, -1).split("\n").map((line) => {
      const match = /^([0-9a-f]{64}) {2}([\w.-]+\.json)$/.exec(line);
      expect(match, `bad SHA256SUMS line: ${line}`).not.toBeNull();
      return { hash: match![1]!, name: match![2]! };
    });
    for (const { hash, name } of entries) expect(createHash("sha256").update(bytes(name)).digest("hex"), name).toBe(hash);
    const names = entries.map((entry) => entry.name);
    expect(names).toEqual([...names].sort());
    expect(names).toEqual(readdirSync(DIR).filter((name) => name.endsWith(".json")).sort());
    expect(bytes("SOURCE").toString("utf8")).toMatch(/^[0-9a-f]{40}\n$/);
  });
});

describe("identity.json", () => {
  it("derives every id from its key", () => {
    for (const c of load<{ name: string; private_key_hex: string; pubkey_hex: string; pubkey_b64: string; pubkey_sha256_hex: string; id: string }>("identity.json").cases) {
      const pub = hex(c.pubkey_hex);
      expect(pubFromPrivate(c.private_key_hex).equals(pub), c.name).toBe(true);
      expect(decodePubkey(c.pubkey_b64)?.equals(pub), c.name).toBe(true);
      expect(createHash("sha256").update(pub).digest("hex"), c.name).toBe(c.pubkey_sha256_hex);
      expect(gadgetIdFromPubkey(pub), c.name).toBe(c.id);
    }
  });
});

describe("rfc6979.json and der.json", () => {
  it("verifies every signed case with node:crypto, high-S included", () => {
    for (const c of load<{ name: string; private_key_hex: string; message_utf8: string; der_hex: string; r_hex: string; s_hex: string; raw_hex: string }>("rfc6979.json").cases) {
      expect(c.raw_hex, c.name).toBe(c.r_hex + c.s_hex);
      expect(verifyP256(pubFromPrivate(c.private_key_hex), c.message_utf8, hex(c.der_hex)), c.name).toBe(true);
    }
    for (const c of load<{ name: string; valid: boolean; der_hex: string; der_len?: number; private_key_hex?: string; message_utf8?: string }>("der.json").cases) {
      if (!c.valid || !c.private_key_hex || c.message_utf8 === undefined) continue;
      expect(hex(c.der_hex).length, c.name).toBe(c.der_len);
      expect(verifyP256(pubFromPrivate(c.private_key_hex), c.message_utf8, hex(c.der_hex)), c.name).toBe(true);
    }
  });
});

describe("prove.json", () => {
  it("accepts and rejects exactly as the host rules say", () => {
    for (const c of load<{ name: string; expect: string; pubkey_b64: string; id: string; nonce_b64: string; host_id: string; text: string; sig_b64: string }>("prove.json").cases) {
      const check = checkHello(hello(c.id, c.pubkey_b64));
      const verifies = () => check.ok && verifyP256(check.hello.pubkeyBytes, proveText(c.id, c.nonce_b64, c.host_id), Buffer.from(c.sig_b64, "base64"));
      switch (c.expect) {
        case "accept":
          expect(proveText(c.id, c.nonce_b64, c.host_id), c.name).toBe(c.text);
          expect(verifies(), c.name).toBe(true);
          break;
        case "reject_sig":
          expect(verifies(), c.name).toBe(false);
          break;
        case "reject_id":
          expect(check, c.name).toMatchObject({ ok: false, code: "bad_sig" });
          break;
        case "reject_base64":
          expect(isCanonicalBase64(c.pubkey_b64), c.name).toBe(false);
          expect(check, c.name).toMatchObject({ ok: false, code: "bad_sig" });
          break;
        case "reject_pubkey":
          expect(decodePubkey(c.pubkey_b64), c.name).toBeNull();
          break;
        case "reject_host_id":
          expect(HOST_ID_RE.test(c.host_id), c.name).toBe(false);
          break;
        default:
          throw new Error(`unknown expect ${c.expect} in ${c.name}`);
      }
    }
  });
});

describe("base64.json", () => {
  it("agrees on canonical base64", () => {
    for (const c of load<{ name: string; input: string; canonical: boolean; bytes_hex?: string }>("base64.json").cases) {
      expect(isCanonicalBase64(c.input), c.name).toBe(c.canonical);
      if (c.canonical) expect(Buffer.from(c.input, "base64").toString("hex"), c.name).toBe(c.bytes_hex);
    }
  });
});

describe("frames.json", () => {
  it("decodes every valid frame and refuses the rest", () => {
    for (const c of load<{ name: string; valid: boolean; frame_hex: string; kind?: number; stream?: number; payload_hex?: string; offset?: number; data_hex?: string }>("frames.json").cases) {
      const decoded = decodeBinary(hex(c.frame_hex));
      if (!c.valid) {
        expect(decoded, c.name).toBeNull();
        continue;
      }
      expect(decoded, c.name).toMatchObject({ kind: c.kind, stream: c.stream });
      expect(decoded!.payload.toString("hex"), c.name).toBe(c.payload_hex);
      if (c.kind === 4) {
        expect(decoded!.payload.readUInt32LE(0), c.name).toBe(c.offset);
        expect(decoded!.payload.subarray(4).toString("hex"), c.name).toBe(c.data_hex);
      }
    }
  });
});

describe("firmware.json", () => {
  it("verifies the firmware signature over the text built with the gadget's own board", () => {
    for (const c of load<{ name: string; expect: string; pubkey_b64: string; gadget_board: string; version: string; size: number; sha256: string; text: string; sig_b64: string }>("firmware.json").cases) {
      const text = firmwareText(c.gadget_board, c.version, c.size, c.sha256);
      if (c.expect === "accept") expect(text, c.name).toBe(c.text);
      expect(verifyP256(decodePubkey(c.pubkey_b64)!, text, Buffer.from(c.sig_b64, "base64")), c.name).toBe(c.expect === "accept");
    }
  });
});
````

- [ ] **Step 3: Run it to see it fail**

Run: `cd /Users/omkar/Desktop/openmaus/OpenGrokBot-gadget && PATH="$HOME/.nvm/versions/node/v24.14.1/bin:$PATH" pnpm exec vitest run companion/test/gadget/vectors.test.ts`
Expected: FAIL — `ENOENT: no such file or directory, open '…/companion/test/fixtures/gadget-vectors/SHA256SUMS'`.

- [ ] **Step 4: Copy the files byte for byte and record the source**

Shell state does not carry over from Step 1, so this block works out `REF` again.

```bash
cd /Users/omkar/Desktop/openmaus/OpenGrokBot-gadget || exit 1
export PATH="$HOME/.nvm/versions/node/v24.14.1/bin:$PATH"
SDK=/Users/omkar/Desktop/openmaus/openmausbot-gadget-sdk
REF=$(git -C "$SDK" rev-parse --verify -q p1-protocol || git -C "$SDK" rev-parse --verify -q main)
DEST=companion/test/fixtures/gadget-vectors
mkdir -p "$DEST"
git -C "$SDK" show "$REF:protocol/vectors/SHA256SUMS" > "$DEST/SHA256SUMS"
awk '{print $2}' "$DEST/SHA256SUMS" | while read -r name; do git -C "$SDK" show "$REF:protocol/vectors/$name" > "$DEST/$name"; done
git -C "$SDK" rev-parse "$REF" > "$DEST/SOURCE"
(cd "$DEST" && shasum -a 256 -c SHA256SUMS)
```

Expected: every file reports `OK` (`base64.json`, `der.json`, `firmware.json`, `frames.json`, `identity.json`, `prove.json`, `rfc6979.json`, `versions.json` with P1's vectors on `main`).

- [ ] **Step 5: Protect the bytes from line-ending rewrites**

````bash
cd /Users/omkar/Desktop/openmaus/OpenGrokBot-gadget || exit 1
export PATH="$HOME/.nvm/versions/node/v24.14.1/bin:$PATH"
git apply <<'PATCH'
--- a/.gitattributes
+++ b/.gitattributes
@@ -24,3 +24,6 @@
 server/model-catalog/models-dev.snapshot.json text eol=lf
 third_party/models-dev/LICENSE text eol=lf
 third_party/opencode/LICENSE text eol=lf
+# The gadget protocol vectors are compared byte for byte with the SDK's
+# SHA256SUMS (companion/test/gadget/vectors.test.ts): no line-ending rewrite.
+companion/test/fixtures/gadget-vectors/** -text
PATCH
````

- [ ] **Step 6: Run the test and the whole companion suite**

```bash
cd /Users/omkar/Desktop/openmaus/OpenGrokBot-gadget || exit 1
export PATH="$HOME/.nvm/versions/node/v24.14.1/bin:$PATH"
pnpm exec vitest run companion/test/gadget/vectors.test.ts
pnpm exec vitest run companion/test
```

Expected: `Tests  7 passed (7)` and the full companion suite passes (36 files and 495 tests). If a vector disagrees with the host code (for example a `frames.json` case the decoder treats differently), do not edit the vectors. Add a "Contract deviations" note naming the case, the host behaviour and the proposed change, and stop for review (contract §0 item 2).

- [ ] **Step 7: Commit**

```bash
cd /Users/omkar/Desktop/openmaus/OpenGrokBot-gadget || exit 1
export PATH="$HOME/.nvm/versions/node/v24.14.1/bin:$PATH"
git add .gitattributes companion/test/fixtures/gadget-vectors companion/test/gadget/vectors.test.ts && git commit -m "test(companion): vendor the gadget protocol vectors and run them through the host

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 19: Branch-level verification and hand-off

**Files:** none changed (unless a check fails; then fix in the task that owns the file and re-run this task).

- [ ] **Step 1: Static checks**

```bash
cd /Users/omkar/Desktop/openmaus/OpenGrokBot-gadget || exit 1
export PATH="$HOME/.nvm/versions/node/v24.14.1/bin:$PATH"
# typecheck alone takes about 3 minutes: give this Bash call a timeout of at least 10 minutes
pnpm typecheck && pnpm lint && pnpm i18n:check && pnpm check:electron
```

Expected: every command exits 0.

- [ ] **Step 2: Focused suites**

```bash
cd /Users/omkar/Desktop/openmaus/OpenGrokBot-gadget || exit 1
export PATH="$HOME/.nvm/versions/node/v24.14.1/bin:$PATH"
pnpm exec vitest run companion/test
pnpm exec vitest run scripts/testing/index-route-ratchet.test.ts server/request-auth.test.ts server/gadget-control-token.test.ts companion/test/routes.test.ts
pnpm exec vitest run src/components/CompanionSection src/components/SidebarPhoneButton src/components/PhoneSetupFlow src/lib/phone-setup src/lib/gadgets src/locales
pnpm test:electron
```

Expected: all pass. The companion suite is 36 files and 495 tests with Task 18. The gadget folder alone is 15 files and 178 tests with Task 18 (14 files and 171 tests while Task 18 is blocked).

- [ ] **Step 3: Builds and the packaged-server smoke**

```bash
cd /Users/omkar/Desktop/openmaus/OpenGrokBot-gadget || exit 1
export PATH="$HOME/.nvm/versions/node/v24.14.1/bin:$PATH"
pnpm build:companion
pnpm test:packaged-server
```

Expected: both exit 0 (`server/index.ts` imports the new `server/gadget-control-token.ts`, which esbuild must bundle).

- [ ] **Step 4: The full test suite**

This takes about 52 minutes (51.5 on this Mac; it spawns real harnesses), far past the 10-minute ceiling of a foreground Bash call. Run it with `run_in_background: true` and wait for the notice that it finished. Do not poll it with sleeps, and do not start it a second time while it runs.

```bash
cd /Users/omkar/Desktop/openmaus/OpenGrokBot-gadget || exit 1
export PATH="$HOME/.nvm/versions/node/v24.14.1/bin:$PATH"
pnpm exec vitest run
```

Expected: `Test Files  929 passed | 25 skipped (954)` and `Tests  12344 passed | 70 skipped | 1 todo`, with no failures. (A full run measured `12339 passed` before this revision added five tests; the file count is unchanged, since they went into existing files.)

Then check that everything is committed:

```bash
cd /Users/omkar/Desktop/openmaus/OpenGrokBot-gadget || exit 1
export PATH="$HOME/.nvm/versions/node/v24.14.1/bin:$PATH"
git status --short
```

Expected: no output.

- [ ] **Step 5: Hand-off summary (paste into the PR description draft; do not push or open a PR)**

Write `git log --oneline origin/main..feat/gadget-hub` and the summary below into the hand-off message for Omkar. Also state the merge order (contract §1.3). This branch merges first, then P3b (`feat/gadget-voice`), P4a (`feat/gadget-tools`) and P4b (`feat/gadget-ota`). Each of those is rebased onto the branch merged before it.

What this branch does:

- Gadgets pair with the six digits from Settings → Remote access → Pair a gadget, and prove their key on every connection.
- A gadget talks to its bot in the bot's selected thread, with live replies, `working`, Stop, steered and queued sends, approvals (Allow/Deny only), and routine and unprompted pushes.
- Gadgets appear in a Gadgets group with their bot, Read pushes aloud, battery and an online dot, and Remove works.
- The companion stops cleanly with gadgets connected.
- The gadget control token reaches both children over their parent ports only.

What could not be verified without hardware or a packaged build (each has a check written down):

- **A real gadget against this hub.** The firmware's C WebSocket client (wslay, P2a) and the ESP-IDF client (P2c) were only verified against the shared vectors and this plan's TypeScript client. Check: P2a's simulator e2e against an OMB2 dev instance (spec §10 "End to end").
- **mDNS discovery by an ESP32** (`id=` in TXT, Windows answering ESP32 queries). Check: P2c's hardware checklist (`host auto`). The Pair a gadget panel shows `lan:8810` as the fallback.
- **Windows Public networks.** The firewall drops :8810. The panel reuses the existing hint, but no Windows machine was used. Check: a Windows run of the packaged app with the network set to Public.
- **The packaged Electron app.** `utilityProcess` parent-port delivery of `gadgetControlToken` to the companion and the harness was covered by `vm`-slice tests, not a packaged build. Check: OMB2 build, then confirm that `/gadget/*` (once P4a lands) answers 403 without the header and 200 with it.
- **Live harness event ordering in long real turns.** Steering into an engine's running turn, coalesced queue drains and multi-item replies were exercised with a fake harness that follows the origin/main order (`turn.completed` before the terminal patch). Check: a talk-and-stop session with a real engine on the OMB2 instance.
- **Speech.** Voice turns end with the stt_unavailable copy until P3b lands. That is by design in this plan.

Known limitation (A35): the companion control port's `GET /state` and pairing routes stay unauthenticated on loopback. It is unchanged by gadgets, and a comment marks it at `POST /pairing`. The follow-up is to require the Electron-minted token there, together with the control page.

---

## Notes for the other app plans

- **Merge order and anchors (contract §1.3, §3.12, §3.13).** Omkar merges P3a, then P3b, then P4a, then P4b. Before each later branch is merged, it is rebased onto the branch merged just before it. P3b, P4a and P4b all edit P3a's `companion/src/index.ts` and `companion/src/control.ts`. Git reports a conflict when two branches change adjacent lines, so each plan's edit sits at least one unchanged line away from every other plan's edit. These are the anchors as they read on `feat/gadget-hub`. Find each one with `grep -n` and insert or replace exactly there:

  | Where | P3b | P4a | P4b |
  |---|---|---|---|
  | `index.ts` imports | below `import { createGadgetHub } from "./gadget/hub.ts";` | below `import { notifyDeviceRevoked } from "./harness-notice.ts";` | below `import { loadOrCreateHostId, serviceTxt } from "./host-id.ts";` |
  | `index.ts` `createGadgetHub({…})` | replace `voice: undefined,` (the line after `devices,`) | replace `onDevicesChanged: undefined,` (the last line before `});`) | the `const firmware = …` statement goes after the call's closing `});` |
  | `index.ts` `createControlServer({…})` | — | after `gadgetHub,` | `firmware,` after `hostId: HOST_ID,`. Use the line inside `createControlServer({`, because the `createGadgetHub` call has a line with the same text |
  | `index.ts` `shutdown()` | — | — | `firmware.close();` before `await gadgetHub.close();` |
  | `control.ts` imports | — | below `import { cleanGadgetName, type DeviceRegistry, type GadgetSettingsPatch } from "./devices.ts";`. Task 12 rewrote origin/main's `import type { DeviceRegistry } from "./devices.ts";` into this line, so the old text is not on the branch | below `import type { GadgetHub } from "./gadget/hub.ts";` |
  | `control.ts` `ControlOptions` | — | after `gadgetHub?: GadgetHub;` | `firmware?:` directly after `publicNetworks?: () => ReadonlySet<string>;`, before P3a's `hostId` comment |

- **P3b (voice):**
  - Pass `voice: createGadgetVoice` at the `createGadgetHub` call in `companion/src/index.ts`. It replaces the `voice: undefined,` line directly after `devices,`.
  - `SpeechOut.stop()` must send `speak.stop` synchronously, and only for a stream that is playing or queued. With nothing playing it must be a no-op that sends no frame, because the turn engine now calls it at the start of every new turn, not only on barge-in and Stop (spec §6.2 Speech item 6). On barge-in and Stop it runs before the old turn's `done`.
  - The final `reply`, the `replyFinal` call and the `done` after it can wait up to 250 ms after a partial reply (the spec §4.4 cap). Tests should wait for `done` or for the speech frames, not assume the final reply is immediate.
  - `replyFinal` gets the raw, unshaped text.
  - `textOnlyVoice` stays the default.
- **P4a (bot tools):**
  - Read `gadgetControlToken` and `onMutationToken` (exported) in `companion/src/index.ts`, and `gadgetControlToken` (exported) in `server/index.ts`.
  - `OMB_COMPANION_CONTROL_PORT` is set in the packaged harness env.
  - Use `hub.session(id)`, `recentEvents(id)` and `botName(id)`, plus `onDevicesChanged` (removals are reported only for `gad_` ids, once each).
  - Peer-approval cards (a `tool` and no `requestType`) already reach gadgets as Allow/Deny asks.
- **P4b (OTA):**
  - Use `onSessionReady`, `session.on("fw.*")`, `allocStream` and `releaseStream`.
  - `GadgetRow` takes `updateCell`.
  - Put new `companion.mjs` helpers before `companionCloudDesktopAccess` (see Task 15).
- **All:** the shared SSE stream exists only while at least one gadget is ready.

## Contract notes (additive, no stop needed)

Contract §0 item 3 allows these additions, because no other plan depends on them having a different shape:

- **Private files:** `companion/src/gadget/directory.ts`, `turns.ts`, `asks.ts`, `push.ts`, `server/gadget-control-token.ts`, and the test helpers `fixed-values.ts` and `hub-rig.ts`.
- **Additive members:** `GadgetHubOptions.timing`, `FakeHarness.dropStreams`, `waitForSubscriber` and `subscribers`, `TestGadget.frames()`, `RuntimeEventLite.message` and `terminal`, `PhoneSetupController.pairedGadgetName`, and `PhoneSetupFlowState.pairedDeviceKind`.
- **New i18n keys:** `remote.gadgets.pairedDetail`, `remote.gadgets.cancel`, `remote.gadgets.nameAria` (`Name of {name}`, the gadget name field), and `remote.status.devicesOne`, `devicesMany`, `gadgetsOne`, `gadgetsMany`.
- **Additive component members:** `GadgetRow`'s optional `onRename` prop (the desktop rename that spec §4.3 and §6.4 require; the contract's row list predates it), and the exported `PairGadgetCode`, the open-window view `PairGadgetPanel` renders. P4b's `updateCell` prop is unchanged, and the `onRename` line sits before the `onRemove={…}` line that P4b's plan adds `updateCell` after.
- **Additional files edited (no other plan touches them):** `src/lib/phone-setup.ts` (`pairedDeviceKind`), and existing tests narrowed or extended for the `PublicDevice` union: `companion/test/devices.test.ts`, `companion/test/control.test.ts`, `src/components/SidebarPhoneButton.test.ts`, `src/lib/phone-setup.test.ts`, `src/components/CompanionSection.test.ts` and `src/components/PhoneSetupFlow.publicNetwork.test.ts`. `electron/preload.node-test.mjs` is also edited, but contract §5.2 already pins it (P3a's three methods, P4b's two).
- **Control page:** the `page()` script in `companion/src/control.ts` (a P3a-owned file) draws gadget rows without the phone-only grant buttons.
- **`createGadgetHub({…})` property order:** `voice: undefined,` comes directly after `devices,`, and `onDevicesChanged: undefined,` comes last, so that P3b's and P4a's one-line replacements are five lines apart and merge without a conflict (contract §1.3). The names and values are the contract's §3.12 P3a cell. Only the line order is pinned here.
- **index.ts TXT record:** written through `serviceTxt(machineName(), HOST_ID)`. It gives the same `v=1` and `name=` entries as before, plus `id=`.
- **`export` keyword:** the pinned `let gadgetControlToken` and `function onMutationToken` (companion), and `let gadgetControlToken` (harness), are exported only to satisfy `noUnusedLocals` on this branch. Their names and types are unchanged.
- **Default bot (D13):** applied inside `completeProve` for both newly enrolled and known gadgets, which is the same rule in one place.

## Deviations recorded during the build

Review of Tasks 1–4 (app commit `f47b37a0`, `fix(P3a): address review of tasks 1-4`, on `feat/gadget-hub`). Where these differ from the code blocks in Tasks 1, 3 and 4, the repository files are authoritative. The tests grow by 23 (`protocol.test.ts` 13 → 14, `shape.test.ts` 13 → 16, `ws.test.ts` 16 → 35), so every later expected count for the gadget folder and the full companion suite is 23 higher: 152, 168 and 177 after Tasks 9, 10 and 11, and 511 and 518 companion tests after Tasks 13 and 18. Each new test that covers a behaviour change failed against the Task 1–4 code and passed after the fix.

1. **Inline code is kept verbatim (Task 3, `shape.ts`).** Contract §3.8 keeps inline code of 40 characters or fewer, but the Task 3 code replaced the span in place, so the later emphasis rules stripped its `_` and `*`, and the `_`/`__` rules also matched inside words: "Set `OMB_GADGET_CONTROL_TOKEN` in my_notes_v2.txt" came out as "Set OMBGADGETCONTROLTOKEN in mynotes_v2.txt". Kept spans now wait behind U+E000 *n* U+E001 placeholders (stripped from the input first) and are restored after the emoji rule. `_` and `__` never open or close emphasis inside a word (CommonMark): the lookarounds `(?<![\p{L}\p{N}_])` and `(?![\p{L}\p{N}_])`, with `**` and `*` rules of their own. Test: "keeps short inline code verbatim and leaves intraword underscores alone".
2. **Bare URLs leave trailing punctuation (Task 3).** The rule is `/\bhttps?:\/\/[^\s<>]*[^\s<>.,;:!?)\]'"]/g`, so "(see https://example.com/a)." becomes "(see [link])." instead of "(see [link]". Test: "leaves the punctuation after a bare link".
3. **`screenText` tidies after folding (Task 3).** The tidy rules (trailing spaces, runs of spaces, three or more newlines, trim) are a `tidy()` helper that `shapeReply` ends with and `screenText` runs again after `foldLatin1`, because folding can drop a whole word: "Done 你好 ok" gave "Done  ok". Test: "leaves no double space where folding drops a character".
4. **`encodeHostMessage` drops null only on the message's own fields (Task 1, `protocol.ts`).** The replacer dropped null at every depth, so `act.args` `{"color": null}` went out as `{}`. Spec §4.1's "never sent as null" is about the protocol's optional fields, so the replacer drops a null only when `this === msg`. Tests: "keeps null inside a payload such as act.args", and the existing null test also pins a top-level null.
5. **A throwing handler closes with 1011 (Task 4, `ws.ts`).** Handlers run inside socket listeners and the companion has no `uncaughtException` handler, so one throw in a hub or session handler ended the companion, phones' remote access included. `deliver` catches and calls `startClose(1011, "internal error")` (contract §3.4's internal-error code), and `attach` replays pending events through `deliver`. Tests: "closes with 1011 when a session handler throws, and the process lives on" and "… on a frame replayed at attach".
6. **Incoming close frames are validated, and a peer close has the 1 s destroy timer (Task 4).** RFC 6455 §7.4: a code below 1000, 1004–1006, 1015–2999 or 5000 and up fails with 1002; §8.1: a reason that is not valid UTF-8 fails with 1007. After echoing a close the gadget started, the server sets the same `setTimeout(() => socket.destroy(), 1_000)` as `startClose`, so a peer that keeps its side of TCP open no longer holds the socket until the 45 s idle timer (which reported 1006 instead of the peer's code). Tests: the `closes on %s` table rows for codes 1005, 999, 1015, 2999, 5000 and a non-UTF-8 reason, and "does not let a gadget that keeps its side of TCP open hold the socket after a close".
7. **Only complete frames reset the idle timer (Task 4).** Contract §3.4 drops a connection after `idleTimeoutMs` "without any inbound frame", but `lastInbound` was updated on every TCP chunk, so a peer that trickled bytes of an unfinished frame stayed connected forever. `lastInbound` is now set in `parse()` after each whole frame (pongs included). Test: "counts only complete frames as signs of life".
8. **`sendBinaryDrained` waits only while a `drain` is owed (Task 4).** Node emits `drain` only after a `write()` returned false (the buffer reached `writableHighWaterMark`, 64 KiB on Node 22 and later), so with `drainThresholdBytes` below the mark a sender waited forever. The loop also requires `socket.writableNeedDrain`; with the default 64 KiB threshold behaviour is unchanged. Test: "does not wait for a 'drain' that never comes when the threshold is below the socket's high-water mark".
9. **More error branches under test (Task 4).** New `closes on %s` rows for a fragmented ping, a ping over 125 bytes, a one-byte close (1002) and a 64-bit length with the high bits set (1009); new tests for a new data frame inside a fragmented message (1002), the text cap across fragments (10000 + 7000 bytes, 1009), `terminate()` (no close frame, reported as 1006 "terminated") and the echo of a close code the gadget starts (4000 "bye"). These pass on the Task 4 code too; they cover branches no test ran.

Review of Tasks 5–8 (app commit `f923a3a8`, `fix(P3a): address review of tasks 5, 6, 7, 8`, on `feat/gadget-hub`). Where these differ from the code blocks in Tasks 5, 7 and 8, the repository files are authoritative. The tests grow by 8 (`enroll.test.ts` 15 → 18, `devices-gadget.test.ts` 11 → 13, `harness-client.test.ts` 7 → 10), so every later expected count for the gadget folder and the full companion suite is 31 higher than the plan's figures (8 higher than the note above): 160, 176 and 185 after Tasks 9, 10 and 11, and 519 and 526 companion tests after Tasks 13 and 18. Each new test that covers a behaviour change failed against the Task 5–8 code and passed after the fix. A mutation pass confirmed that each coverage test fails when its branch is removed.

10. **The action-limit test pins the survivors (Task 8, `enroll.test.ts`).** The test "drops actions that break a limit and defaults a missing risk to confirm" asserted only `not.toContain("Bad Name")` and a length of 16. Its 20 filler actions filled the 16 slots anyway, so keeping "long" (201-character description), "big" (1100-byte params), "odd" (risk "maybe") or the duplicate "relay.on" still passed. It now asserts the exact list, `["relay.on", "a0" … "a14"]`, so any kept bad entry pushes a filler out. The `toMatchObject` on `h.actions[0]` still pins the first "relay.on"'s description. Removing any one of the four checks from `normalizeActions` now fails the test (spec §4.3, contract §3.9).
11. **Stored sensor readings stay at 16 keys (Task 5, `devices.ts`).** `touchGadget` merged every sense op into `lastSensors` with no limit. `cleanSensors` caps one op at 16 keys, but new keys from later ops kept adding up: 1000 ops of 16 new keys left 16000 keys in memory, in `list()` and in `devices.json`. A new constant, `SENSOR_KEYS_MAX = 16`, sets the per-op slice. The merge now always updates keys that are already stored, and adds a new key only while fewer than 16 are stored. Test: "keeps at most 16 sensor keys across many readings, and still updates the known ones".
12. **A gadget removed during the default-bot lookup is refused (Task 8, `enroll.ts`).** `completeProve` awaits `resolveBot()` (D13), which is `GET /api/bots` and can take up to 10 s. A gadget removed in that time still got `{ok: true}`, so Task 9 would then send `ready` after `hub.revoke()` had run. Remove is the spec §9 answer to a lost or stolen gadget. The D13 block now ends with: if `registry.gadget(device.id)` is null, return `{ ok: false, code: "revoked", message: "this gadget was removed in MausBot → Settings → Remote access" }`. Test: "answers revoked when the gadget is removed while the default bot is looked up", run with the lookup returning null and returning "b1".
13. **The harness client refuses computer and browser routes (Task 7, `harness-client.ts`).** `identity()` checked only `denyReason()`. The proxy also refuses `isCloudDesktopAccess()` and `isBrowserControlAccess()` routes to a phone without those grants, and the registry never gives a gadget either grant. `identity()` now throws `HarnessRefused(403, "a gadget has no computer or browser access")` for those routes, before any request (spec §9, "A gadget does more than a phone could"). No hub code calls these routes, so this is defense in depth. Test: the "refuses, without a request" test adds `POST /api/bots/b1/computer/control` and `GET /api/bots/b1/browser/live`, both rejected with 403.
14. **The fake harness's hello has no id, and the client resumes from the hello's cursor (Task 7, `fake-harness.ts` and `harness-client.ts`).** The real harness writes `hello` as a bare `data:` line (`server/index.ts`, `/api/events`), and only broadcast frames carry `id:`. The fake gave hello an id, so no test covered a stream that drops before its first broadcast. With the real harness, that stream reconnected without Last-Event-ID and got a full re-read. The fake now writes hello without an id. In `dispatch()`, a hello with `resumed !== true` and a string `cursor` sets `lastEventId` to that cursor. A resumed hello leaves it alone, because the replay that follows carries its own ids, and taking the hello's cursor (the head) would skip that replay after a mid-replay drop. After a harness restart this gives the same cursor the old fake's hello id gave, so the later hub tests behave the same. Test: "resumes from the hello's cursor when the stream drops before its first event".
15. **An event over 4 MiB is not replayed for ever (Task 7, `harness-client.ts`).** When an event passed `MAX_EVENT_BYTES`, the client dropped the connection but kept `lastEventId`. The resume then replayed the same event, which failed again, every 500 ms. The oversize path now clears `lastEventId` before `destroy()`. The reconnect gets `resumed: false`, the hub re-reads, and the new hello's cursor (item 14) moves past the event. With `screens=off` no such event is expected; the cap is a guard. Test: "drops an event over 4 MiB, then reconnects without replaying it" (expects hellos `[false, false]`, one `onDown` with "harness event too large", and the next frame delivered).
16. **More branches under test (Tasks 5, 7 and 8).** `completeProve` maps `save_failed` to `internal` and leaves nothing enrolled: "answers internal, with nothing enrolled, when the pairing cannot be saved". Rule 2's stored-key mismatch gives `bad_sig`: "refuses a known id whose stored key differs from the one in the hello". `noteGadgetHello` writes a hello that changes the name or board at once, inside the 60 s throttle: "writes a hello that changes the name or board at once, even inside the minute". `onDown` fires once on a dropped stream (asserted in the Last-Event-ID resume test). The 16 MiB response cap: "gives up on a response over 16 MiB" (the result is compared as a string, because a failing `expect` on a 16 MiB buffer runs the test worker out of memory). These pass on the Task 5–8 code too; they cover branches no test ran.
