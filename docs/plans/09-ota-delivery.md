# P4b — OTA delivery: release keys, update checks and the Update button Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** MausBot looks for official gadget firmware while a gadget is paired, shows **Update available** or **Custom build** on each gadget row in Settings → Remote access, and on a click downloads, verifies and streams the signed image to the gadget over its live session, showing progress until the gadget comes back on the new version.

**Architecture:** Four new companion modules, Node built-ins only. `release-keys.ts` holds the trusted public keys. `releases.ts` fetches `manifest.json`, keeps only entries whose signature verifies, compares versions by SemVer and downloads images, checking size and SHA-256. `ota.ts` runs the host side of spec §4.8 on one `GadgetSessionHandle`: `fw.offer`, a 64 KiB window of 4 KiB chunks and `fw.commit`. `firmware.ts` ties them to P3a's hub and registry. It owns the check schedule, the Update button's refusals, the restart and rollback watch, `fw.installed`, and the `gadgetFirmware` block in the control port's `/state`. Two new loopback control routes back two new Electron IPC channels. The renderer adds pure helpers, a `GadgetUpdateCell` passed into P3a's `GadgetRow`, a release check when Remote access opens, and 1 s polling while an update runs. In the SDK, `tools/release/dev-release.ts` serves a test-signed release locally, so the whole path can be exercised against the simulator.

**Tech Stack:** TypeScript on Node ≥ 24 (type stripping in dev, plain `tsc` for the packaged companion), vitest 4.1, pnpm 10.33.0, Node `crypto` (P-256 ECDSA verify, SHA-256), global `fetch`, React 19 `renderToStaticMarkup` tests, Electron `node:test` + `vm` slice tests, oxlint 1.80. SDK tool: Node ≥ 22.18 type stripping, `node:test`, P1's `protocol/lib`.

**Spec:** `/Users/omkar/Desktop/openmaus/openmausbot-gadget-sdk/docs/specs/2026-10-04-openmausbot-gadget-design.md` (v1.1). This plan covers §8 on the MausBot side, the host side of §4.8, the §4.1 encodings it uses, the §6.1 control-port rows `POST /devices/:id/firmware-update` and `POST /firmware-updates/check`, the §6.4 Update / Custom build cell, §9's "A malicious image flashes the gadget" row on the host, and the §10 rows for companion, Desktop UI, end to end and hardware. The binding interface contract is `/Users/omkar/Desktop/openmaus/openmausbot-gadget-sdk/docs/plans/00-interfaces.md`: §3.16 OTA delivery, §3.12 companion wiring, §3.13 control routes, §3.17 Electron, §3.18 Renderer, §3.19 test helpers, §3.1 env vars, §4.1 `manifest.json`, §4.4/§4.4.1 vectors and `protocol/lib`, §4.5 keys, §1.7 fixed values, §5.2 ownership. Amendments A17, A19, A24 and A37 are folded into spec v1.1.

**Repos and branches:**
- App: OpenMausBot. The worktree is `/Users/omkar/Desktop/openmaus/OpenGrokBot-gadget`, or `/Users/omkar/Desktop/openmaus/OpenGrokBot-gadget-ota` when another plan holds the first one (Task 1 decides). Branch `feat/gadget-ota` is created from `feat/gadget-hub` (P3a).
- SDK: worktree `/Users/omkar/Desktop/openmaus/openmausbot-gadget-sdk-p4b`, branch `p4b-ota` created from `p2d-installer` (Task 12). It carries `tools/release/dev-release.ts`, and its test once Contract deviation 2 is approved.

Line numbers are from OpenMausBot `origin/main` `6dd4403d8fbbbd5c17169724cb2a529f11d7543e`. P3a shifts some of them in `companion/src/control.ts`, `companion/src/index.ts`, `electron/*` and `src/components/*`, so every edit there also names the exact anchor text to find with `grep -n`.

## Global Constraints

- Node ≥ 24: every app command runs after `export PATH="$HOME/.nvm/versions/node/v24.14.1/bin:$PATH"`. Inside the worktree `pnpm --version` must print `10.33.0`. If it does not, use `corepack pnpm@10.33.0 <args>` wherever this plan says `pnpm <args>` (contract §0 item 5).
- The main checkout `/Users/omkar/Desktop/openmaus/OpenGrokBot` belongs to another session. Never checkout, stash, reset, edit or fetch there. `git worktree add` from it (contract §1.3) is the only allowed write. No `git fetch` anywhere.
- Publishing belongs to Omkar: no push, no PR, no release, no workflow dispatch. Prepare both branches, run the tests and stop.
- Original-work rule (spec §11): never open, fetch, quote or cite third-party gadget SDKs or voice-assistant firmware projects.
- The companion "ships as plain `tsc` output with no `node_modules`. It stays dependency-free, and its code cannot import `shared/`" (spec §6.1). Companion code uses Node built-ins and relative `.ts` imports only, in erasable TypeScript syntax (contract §3.1).
- Checking (spec §8, verbatim): "While at least one gadget is paired, the companion (`releases.ts`) fetches `https://github.com/aivsomkar/openmausbot-gadget-sdk/releases/latest/download/manifest.json`. It does so once a day, and when Settings → Remote access opens, through Electron's `POST /firmware-updates/check`." The fetch also runs at companion start when a gadget is paired. There is no fetch at all while no gadget is paired (contract §3.16).
- "Versions compare by SemVer 2.0.0 precedence. A gadget whose board has a newer version shows **Update available**" (spec §8). A downgrade is never offered.
- Custom builds (spec §8, verbatim): "Any `fw` that ends in `-dev`, or does not match `/^\d+\.\d+\.\d+(-[0-9A-Za-z.]+)?$/`, counts as a custom build. Its row shows **Custom build** with no Update button."
- "Updating (a manual button in v1, through the Electron-only `POST /devices/:id/firmware-update`)" (spec §8). Automatic updates and updates of custom builds are non-goals (spec §1).
- Manifest (contract §4.1): `url` is exactly `<base>v<version>/openmausbot-gadget-<board>-<version>.bin` with `<base>` = `https://github.com/aivsomkar/openmausbot-gadget-sdk/releases/download/`, unless the manifest URL is overridden. `size` is an integer 1 … 6291456. `sha256` is "Lowercase hex everywhere" (spec §4.1). `sig` is canonical base64 DER. `key_id` matches `/^r[0-9]+$/`, and also `t1` only with `OMB_GADGET_TRUST_TEST_KEY=1`.
- Signed text (spec §4.8): `openmausbot-gadget/1\nfirmware\n<board>\n<version>\n<size>\n<sha256 lowercase hex>`, with the **manifest's board id** (spec §8 step 3).
- Streaming (spec §4.8 and §4.1, verbatim): "the host keeps at most 64 KiB unacknowledged. Offsets must be contiguous". A firmware frame is "a u32 little-endian byte offset, then up to 4096 bytes of the image". "the host gives up if `fw.ready` doesn't arrive within 10 s."
- Keys (spec §8): "Its public half, with `key_id`, is compiled into the firmware and into OpenMausBot." The test key is trusted only when `OMB_GADGET_TRUST_TEST_KEY` is exactly `"1"` (contract §3.1).
- IPC channels are registered after the `companion:revoke` handler (contract §3.17). New `electron/companion.mjs` helpers go **before** `companionCloudDesktopAccess`, because `electron/companion-browser.node-test.mjs:31-40` runs `source.slice(start)` from `companionBrowserControlAccess` to the end of the file in a `vm` script, and a second `export` there is a syntax error.
- i18n: new keys go in `src/locales/en.json` only (other packs fall back). They avoid the words "workspace" and "organisation" (`src/locales/words.test.ts`). Renderer tests are `*.test.ts`, not `.tsx` (`vite.config.ts:21-31` includes `src/**/*.test.ts`).
- Tests do not sleep. They wait on events, use `vi.waitFor` for a child process's port, or advance fake timers (`vi.advanceTimersByTimeAsync`) to show that a timer did not fire.
- Every commit message ends with the trailer line `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.

## Review Focus

These six inputs are implied by the spec but easy to miss. Each has a test in the task that owns the code.

1. **A manifest or image that is not genuinely signed.** Examples: a tampered or mirrored manifest, a key id the app has never seen (today that means every real release, because `RELEASE_KEYS` is empty until Omkar publishes `r1`), or bytes that differ from the signed hash. Such an entry never shows Update available, its image is never downloaded, and it is never offered. Tests: Task 4 "drops entries signed by a key it does not trust, and never fetches their images", "keeps only the boards that verify", "checks the signature before it downloads anything", and "refuses %s" (damaged, short and long bodies).
2. **The gadget drops in the middle of the transfer** (Wi-Fi, power, unplugged). The row says the gadget disconnected (or stopped responding, when the drop sends no FIN and the host's 35 s progress wait runs out first), the stream id is freed, and the next Update starts from zero. Tests: Task 5 "ends with closed when the gadget drops mid-stream, and frees the stream". Task 8 "fails cleanly when the gadget drops mid-transfer, and the next Update starts over".
3. **The new image never comes back, or rolls back** (probation fails, crash loop, power loss while restarting). The row says it went back to the old version, that the gadget has not come back, or that it came back on the new version without confirming it. It never sits on "Updating… 100%" forever. Tests: Task 6 "says the gadget went back when it returns on its old version", "fails when the gadget does not come back at all" and "says so when the gadget came back on the new version but never confirmed it". Task 8 "says the update did not stay when the gadget comes back on its old version".
4. **A click that cannot start an update**: a second Update while one runs, an offline gadget, a custom build, a gadget already current or ahead, or no OTA slot. Each refusal comes with a reason and never opens a second stream. The reason reaches the row. Tests: Task 6 "refuses %s" and "refuses a second Update while the first is running". Task 7 "passes a refusal through as {error, code} with its status". Task 9 "a refused update carries the sidecar's code and sentence". Task 11 "shows the companion's reason when a click did not start an update".
5. **The release server is unreachable, has no release yet, or answers garbage** (offline Mac, the 404 before the first release, a 503, invalid or huge JSON, a timeout). The companion never throws, the last good manifest stays, and nothing is fetched at all while no gadget is paired. Tests: Task 4 "never throws, and keeps the last good manifest through every kind of failure". Task 6 "never fetches while no gadget is paired". Task 8 "never asks the release server while no gadget is paired".
6. **The restarted gadget is ready before its old session closes.** An ESP32 restart sends no FIN (P2a's core calls `hal_restart()` 1 s after `fw.commit`, and P2c's `hal_restart()` is `esp_restart()` with no WebSocket close), so P3a's hub closes the old session only when the new one replaces it, and the new session's `ready` can reach the firmware service while the old session still waits after `fw.commit`. The outcome comes from the new session in either order: back on the old version is "came back on …", `fw.installed` is "Updated to …", and the old session's late close never overwrites either or starts the "has not come back" timer. A failed update also stops showing once the gadget comes back on other firmware (a USB flash, say). Tests: Task 6 "says the update did not stay when the gadget is ready on its old version before its old session closes", "keeps Updated when the restarted gadget confirms before its old session closes" and "forgets a failed update once the gadget comes back on other firmware". Task 8 "keeps Updated when the restarted gadget connects before its old connection closes" (P3a's `replace()` path). Hardware: Task 14 checklist steps 1 and 3 (11 minutes later, past the restart timeout, the row has not turned into "has not come back").

## Verified while writing this plan (2026-10-04; review fixes re-verified 2026-10-05)

All of this ran on this Mac under `/private/tmp`, with Node 24.14.1 (and 22.22.3 for the SDK tool). The app files were copied from `origin/main` 6dd4403 with `git archive`, and the main checkout's `node_modules` was used read-only. P3a's modules do not exist yet, so the companion tests ran against stand-ins written from contract §3.2, §3.5, §3.9, §3.10 and §3.11. Those stand-ins were `protocol.ts` (only the members this plan uses, with real implementations), `enroll.ts` and `session.ts` and `hub.ts` (types only), and an in-memory `enrollGadget`/`gadget`/`gadgets`/`revoke` on the real `DeviceRegistry`. The vectors were synthetic files with P1's field names and the contract §1.7 values.

- **Companion modules** (`release-keys.ts`, `releases.ts`, `ota.ts`, `firmware.ts`, the `control.ts` edit) with this plan's six test files: **85 tests passed** (re-run after the review fixes; 79 before them), and the existing `companion/test/control.test.ts` (20) still passed. `tsc` with `tsconfig.server.json`'s options was clean, and so was `oxlint --deny-warnings` with the app's `.oxlintrc.json`. `firmware.ts` loads under plain Node type stripping. `tsc -p tsconfig.companion.build.json` output loads from a copy outside the repo.
- **The tests bite.** Each of these code changes made exactly the matching test fail: widening the window to 128 KiB; sending a fresh 64 KiB after every acknowledgement (only "slides the window" fails; the fake gadget acknowledges every 16 KiB, so the other window checks cannot see it); removing the pre-download signature check; keeping every board once one verifies; and replacing `Object.hasOwn` with `in` in `updateFor`. The firmware service as first written, which read the restart only from the old session's close, fails five Task 6 tests: the two restart-order tests, the confirm test, the failed-entry test and the version clamp.
- **Restart order.** The reviewers' race (the restarted gadget ready before the old session closes, as on an ESP32) was replayed against `firmware.ts` with the fakes, in both orders. Back on 1.0.1 with `fw.installed` ends `installed`; back on 1.0.0 ends "came back on 1.0.0"; back on 1.0.1 without `fw.installed` ends "did not confirm the update" when the restart timeout runs out. No case ends on "has not come back".
- **Electron edits** were applied to copies of `origin/main`'s `companion.mjs`, `main.mjs`, `preload.cjs` and `preload.node-test.mjs`. **14 node tests passed**: the new `companion-firmware.node-test.mjs` (4), the unchanged `companion-browser.node-test.mjs` (2, proving the helper placement) and `preload.node-test.mjs` with its new case. `node --check electron/main.mjs` passed and oxlint was clean. Objects made inside a `vm` context fail `assert.deepEqual` on their prototype, so those tests compare a JSON round-trip.
- **Renderer**: `gadgets.ts`'s P4b helpers and `GadgetUpdateCell.tsx` ran with stand-in types and an English-only `t()`. **20 tests passed**, `tsc` with the app's renderer options (`tsconfig.json`) was clean, and so was oxlint.
- **`en.json`** round-trips exactly through `JSON.stringify(value, null, 2) + "\n"`. So Task 11's insertion script yields a diff of exactly the seven added lines, and it refuses to run without P3a's anchor key.
- **SDK `dev-release.ts`** ran against P1's `protocol/lib/{encoding,identity,verify,version}.ts`, copied verbatim from `01-protocol-and-fake-host.md`. Its 4 tests passed on Node 22.22.3 and 24.14.1, also when run from a scratch mirror of `protocol/lib`, `keys/test-t1.*` and `tools/release` under `/private/tmp` (Task 12's place for the test until Contract deviation 2 is approved).
- **Across the two repos**, a live `dev-release` server was fed to the companion's real `createReleaseChecker`. It refuses the t1-signed manifest without `OMB_GADGET_TRUST_TEST_KEY=1`. With the variable set, it offers 1.0.1 to a gadget on 1.0.0, offers nothing to `0.0.0-dev`, and downloads the image byte for byte.
- **Node's built-in WebSocket client** may send only close code 1000 or 3000–4999 (`close(1001)` throws `InvalidAccessError`), so the hub test closes with the default.

Not verified here, because each needs P3a's code, P2a's simulator or hardware: `ota.hub.test.ts` and `firmware-wiring.test.ts` (Task 8), `CompanionSection.firmware.test.ts` and the `CompanionSection.tsx`/`PhoneSetupFlow.tsx` edits in place (Tasks 10–11), the simulator end to end (Task 13) and every hardware check (Task 14). Task 14 runs all of the code checks.

## Out of scope (owned by another plan)

- **Firmware side of §4.8.** Offer checks in order, contiguous chunk writes, `fw.progress` every 16 KiB, size and SHA-256 at commit, `hal_ota_*`, the 5-minute probation and `fw.installed` after the first `ready` belong to **P2a** (core and simulator) and **P2c** (ESP32 `esp_ota_*`, the rollback sdkconfig, `check-size.sh`).
- **Release production.** `release.yml`, the signing job, `manifest.json` and `install.json` production, `SHA256SUMS`, prereleases, `pages.yml`, `keys/release-r1.*`, `docs/release-keys.md`, `keys_release.c` and the release-CI key-table check belong to **P2d** (key files committed by Omkar). This plan only consumes the manifest format of contract §4.1.
- **Hub and session.** The hub, sessions, `allocStream`, the WebSocket drain-aware sender, the registry's `firmware` update on every `hello` (`noteGadgetHello`), the gadget rows without the Update cell, the three pairing IPC channels and the test helpers `connectTestGadget`/`startFakeHarness` belong to **P3a**.
- The fake host's `ota` command (P1). The hardware checklist document `docs/hardware-checklist.md` (P2c). This plan's on-device checks are listed in Task 14 for Omkar to run or for P2c's file to absorb.

## Spec coverage

| Spec requirement | Task |
|---|---|
| §8 Keys: the release key's public half, with `key_id`, compiled into OpenMausBot; the test key never trusted in release use | 2 (and the hand-off step for `r1`) |
| §8 Checking: while ≥ 1 gadget is paired, fetch the manifest daily and when Remote access opens | 4, 6, 7, 9, 11 |
| §8 Checking: SemVer 2.0.0 precedence; **Update available** | 3, 10, 11 |
| §8 Updating 1–2: download the image; check size, SHA-256 and signature | 4 |
| §8 Updating 3: run §4.8 with the manifest's board id as `fw.offer.board` | 5, 6, 8 |
| §8 Updating 4: the row shows progress, then the new version once `fw.installed` arrives | 6, 10, 11 |
| §8 Custom builds: `-dev` or non-matching `fw` → **Custom build**, no Update button | 3, 6, 10, 11 |
| §8 `manifest.json` absolute release URLs (A37) | 3, 12 |
| §4.8 host: `fw.offer`, `fw.ready` within 10 s, 4 KiB chunks on kind `0x04` with a u32 LE offset, 64 KiB window, contiguous offsets, `fw.commit`, failure codes | 5, 8 |
| §4.8 `fw.installed {version}` after the first `ready` on the new image | 6, 8 |
| §4.1 encodings: lowercase SHA-256, base-10 size, version without `v`, canonical base64 | 3, 4 |
| §6.1 control port: `POST /devices/:id/firmware-update`, `POST /firmware-updates/check` (Electron, existing loopback checks) | 7, 9 |
| §6.1 `releases.ts` "Fetches the release manifest, downloads and checks images" | 3, 4 |
| §6.4 gadget row: **Update** or "Custom build" | 11 |
| §9 "A malicious image flashes the gadget": the host offers only images signed by an embedded release key | 4 (the firmware's own check is P2a/P2c's) |
| §10 Companion / Desktop UI: control-route and Electron node tests, a `renderToStaticMarkup` test for the gadget row | 7, 9, 11 |
| §10 End to end: the simulator against a development MausBot, an update | 13 (companion-level, automated) + 14 (full app, manual) |
| §10 Hardware: OTA, power loss during OTA, rollback | 14 (checklist) |
| Contract §3.16 SDK part: `tools/release/dev-release.ts` | 12 |

## Contract notes (additive; no pinned item changed)

These are additions under contract §0 item 3. Nothing pinned is renamed, retyped or moved. Two edits fall outside P4b's pinned points, so they are listed under "Contract deviations" at the end of this plan and wait for review there (contract §0 item 2).

1. `parseManifest(json, options)` takes an extra optional `allowTestKey?: boolean`. It admits `key_id` `t1`, and the checker sets it only when t1 is among its keys.
2. `createReleaseChecker` takes an extra optional `log`. `check()` also verifies every board entry's signature and drops entries that do not verify, so an unverifiable release never shows Update available. `download()` still verifies before it downloads.
3. `runFirmwareUpdate(session, image, onProgress, timeouts?)` takes an optional fourth parameter for tests. `ota.ts` exports `FW_FAIL_MESSAGES`, `releases.ts` exports `MANIFEST_IMAGE_MAX` and `MANIFEST_BODY_MAX`, and `firmware.ts` exports `INSTALLED_TTL_MS` and `RESTART_TIMEOUT_MS`.
4. `createGadgetFirmwareService` takes extra optional `now`, `checkIntervalMs`, `restartTimeoutMs`, `otaTimeouts` and `onChange` (tests only). `requestCheck()` keeps the pinned rule: it runs `check()` whenever ≥ 1 gadget is paired, and the checker already shares one fetch between overlapping checks.
5. The `no_update` refusal code also covers "this gadget has no `caps.ota`, or a slot smaller than the image". None of the five pinned codes fits better.
6. `src/lib/gadgets.ts` gains P4b-private helpers next to the pinned `isCustomBuild` and `updateCellState`: `compareFirmwareVersions`, `availableUpdate`, `updatePercent` and `hasActiveGadgetUpdate`.
7. The bridge methods are typed `Promise<unknown>` as pinned. The `c.act(...)` call site casts to `Promise<CompanionState>`.
8. P4b-private test files: `companion/test/gadget/helpers/test-release.ts` and `companion/test/gadget/helpers/ota-fakes.ts`. (The SDK's `tools/release/dev-release.test.ts` is Contract deviation 2.)
9. Besides the pinned "`installed` entries expire after 10 min", a `failed` entry is dropped when the gadget comes back on firmware other than the one the update started from. The firmware service keeps that starting version, and the session the image streamed on, in a private map; nothing else reads them.

## File Structure

**App (`$WT`):**

| Path | Action | Responsibility |
|---|---|---|
| `companion/src/gadget/release-keys.ts` | create | `RELEASE_KEYS` (empty until `r1`), `TEST_KEY_T1`, `trustedReleaseKeys(env)` |
| `companion/src/gadget/releases.ts` | create | manifest rules, SemVer, custom-build rule, `updateFor`; the checker: fetch, verify, keep last good, download with size and SHA-256 checks |
| `companion/src/gadget/ota.ts` | create | §4.8 host side on one session: offer, window, commit, restart wait, failure messages |
| `companion/src/gadget/firmware.ts` | create | check schedule, `startUpdate` refusals, background update, restart and rollback watch, `fw.installed`, `state()` |
| `companion/src/control.ts` | modify | `firmware` option; `gadgetFirmware` in `companionState()`; the two routes |
| `companion/src/index.ts` | modify | build the service and checker; pass `firmware` to the control server; `firmware.close()` on shutdown |
| `companion/test/gadget/helpers/test-release.ts` | create | t1-signed manifests and images, `fakeFetch` |
| `companion/test/gadget/helpers/ota-fakes.ts` | create | fake session and hub, scripted gadget OTA |
| `companion/test/gadget/release-keys.test.ts`, `releases.test.ts`, `release-checker.test.ts`, `ota.test.ts`, `firmware.test.ts`, `firmware-routes.test.ts`, `ota.hub.test.ts`, `firmware-wiring.test.ts` | create | as named |
| `electron/companion.mjs` | modify | `companionFirmwareUpdate`, `companionFirmwareCheck` |
| `electron/main.mjs` | modify | import both; IPC `companion:firmware-update`, `companion:firmware-check` |
| `electron/preload.cjs` | modify | `firmwareUpdate`, `firmwareCheck` on `window.ogb.companion` |
| `electron/preload.node-test.mjs` | modify | one case for the two channels |
| `electron/companion-firmware.node-test.mjs` | create | IPC and helper tests |
| `src/components/PhoneSetupFlow.tsx` | modify | `GadgetUpdateStatus`, `GadgetFirmwareState`, `CompanionState` fields, bridge methods; fast polling (Contract deviation 1) |
| `src/lib/gadgets.ts` | modify | P4b update helpers |
| `src/lib/gadgets.update.test.ts` | create | helper tests on the shared `versions.json` |
| `src/components/GadgetUpdateCell.tsx` (+ `.test.ts`) | create | the Update / Custom build cell |
| `src/components/CompanionSection.tsx` | modify | check when Remote access opens; `updateCell` on each `GadgetRow` |
| `src/components/CompanionSection.firmware.test.ts` | create | both wiring jobs |
| `src/locales/en.json` | modify | seven `remote.gadgets.*` keys |

**SDK (`$SDKWT`):** `tools/release/dev-release.ts` (create, P4b-owned) and its test `tools/release/dev-release.test.ts` (Contract deviation 2: it sits in P2d's folder, so it is committed only once that item is approved; until then it runs from a scratch mirror under `/private/tmp`).

## Conventions for every task

Every app shell starts like this. `WT` is the worktree Task 1 chose: P4b's own `OpenGrokBot-gadget-ota` exists only when Task 1 had to make it.

```bash
export PATH="$HOME/.nvm/versions/node/v24.14.1/bin:$PATH"
export WT=$(test -d /Users/omkar/Desktop/openmaus/OpenGrokBot-gadget-ota && echo /Users/omkar/Desktop/openmaus/OpenGrokBot-gadget-ota || echo /Users/omkar/Desktop/openmaus/OpenGrokBot-gadget)
cd "$WT"
```

Commit with `git add <files> && git commit -m "<subject>" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"`.

---

### Task 1: Worktree, branch and P3a prerequisites

**Files:** none (verification; may create a worktree).

**Interfaces:**
- Consumes from P3a, with the names in contract §3.2–§3.19:
  - `companion/src/gadget/protocol.ts`: `BinaryKind`, `FW_CHUNK_BYTES`, `FW_WINDOW_BYTES`, `FW_READY_TIMEOUT_MS`, `BOARD_ID_RE`, `SHA256_HEX_RE`, `RELEASE_VERSION_RE`, `encodeFwChunk`, `firmwareText`, `verifyP256`, `decodePubkey`, `isCanonicalBase64`, `FwFailCode`, `GadgetCaps`, `GadgetToHost`, `HostToGadget`, `GadgetOp`, `BinaryKindValue`.
  - `enroll.ts` `NormalizedHello`; `session.ts` `GadgetSessionHandle`; `hub.ts` `GadgetHub`, `createGadgetHub`.
  - `DeviceRegistry.gadget/gadgets/openPairing/enrollGadget/revoke`.
  - `connectTestGadget`, `startFakeHarness`.
  - `companion/test/fixtures/gadget-vectors/{firmware.json,versions.json}`.
  - `src/lib/gadgets.ts` `isGadget`; `GadgetRow`'s `updateCell` prop; P3a's `remote.gadgets.*` keys.
- Produces: `$WT` on branch `feat/gadget-ota` with dependencies installed and a green baseline.

- [ ] **Step 1: See what exists**

```bash
export PATH="$HOME/.nvm/versions/node/v24.14.1/bin:$PATH"
APP=/Users/omkar/Desktop/openmaus/OpenGrokBot
git -C "$APP" rev-parse --verify --quiet refs/heads/feat/gadget-hub >/dev/null && echo "hub branch: yes" || echo "hub branch: no"
git -C "$APP" rev-parse --verify --quiet refs/heads/feat/gadget-ota >/dev/null && echo "ota branch: yes" || echo "ota branch: no"
test -d /Users/omkar/Desktop/openmaus/OpenGrokBot-gadget && git -C /Users/omkar/Desktop/openmaus/OpenGrokBot-gadget status --short --branch | head -5 || echo "no OpenGrokBot-gadget worktree"
```

Expected: one line per question. On 2026-10-04 neither branch existed yet. **If `hub branch: no`, stop: P3a (`06-mausbot-companion-hub.md`) has not run.**

- [ ] **Step 2: Put P4b on `feat/gadget-ota`**

If `/Users/omkar/Desktop/openmaus/OpenGrokBot-gadget` exists, is clean (`git status --porcelain` prints nothing) and is on `feat/gadget-hub` or `feat/gadget-ota`, use it:

```bash
cd /Users/omkar/Desktop/openmaus/OpenGrokBot-gadget
git rev-parse --verify --quiet refs/heads/feat/gadget-ota >/dev/null && git switch feat/gadget-ota || git switch -c feat/gadget-ota feat/gadget-hub
export WT=/Users/omkar/Desktop/openmaus/OpenGrokBot-gadget
```

Otherwise another plan holds it, so make P4b's own worktree (contract §1.3) and use it for every later task:

```bash
APP=/Users/omkar/Desktop/openmaus/OpenGrokBot
git -C "$APP" rev-parse --verify --quiet refs/heads/feat/gadget-ota >/dev/null \
  && git -C "$APP" worktree add /Users/omkar/Desktop/openmaus/OpenGrokBot-gadget-ota feat/gadget-ota \
  || git -C "$APP" worktree add -b feat/gadget-ota /Users/omkar/Desktop/openmaus/OpenGrokBot-gadget-ota feat/gadget-hub
export WT=/Users/omkar/Desktop/openmaus/OpenGrokBot-gadget-ota
```

Expected: `git -C "$WT" branch --show-current` prints `feat/gadget-ota`.

- [ ] **Step 3: Gate on P3a's deliverables**

```bash
cd "$WT"
for f in companion/src/gadget/protocol.ts companion/src/gadget/enroll.ts companion/src/gadget/session.ts companion/src/gadget/hub.ts \
         companion/test/gadget/helpers/gadget-client.ts companion/test/gadget/helpers/fake-harness.ts \
         companion/test/fixtures/gadget-vectors/firmware.json companion/test/fixtures/gadget-vectors/versions.json \
         src/lib/gadgets.ts src/components/GadgetRow.tsx src/components/PairGadgetPanel.tsx; do
  test -f "$f" || echo "MISSING $f"
done
grep -cE "export (function|const) (encodeFwChunk|firmwareText|verifyP256|decodePubkey|isCanonicalBase64|FW_CHUNK_BYTES|FW_WINDOW_BYTES|FW_READY_TIMEOUT_MS|BOARD_ID_RE|SHA256_HEX_RE|RELEASE_VERSION_RE|BinaryKind)\b" companion/src/gadget/protocol.ts
grep -c "onSessionReady" companion/src/gadget/hub.ts
grep -cE "enrollGadget|gadgets\(\)" companion/src/devices.ts
grep -c "updateCell" src/components/GadgetRow.tsx
grep -c "export function isGadget" src/lib/gadgets.ts
grep -c "pairingBot" electron/preload.cjs
grep -c '"remote.gadgets.pairedToast"' src/locales/en.json
grep -c "createGadgetHub" companion/src/index.ts
grep -cE "export (async )?function connectTestGadget" companion/test/gadget/helpers/gadget-client.ts
```

Expected: no `MISSING` line. The protocol count is `12` (one line per name). Every other count is `1` or more. **If anything is missing, stop: P3a is not finished on `feat/gadget-hub`.** Do not build P4b on an incomplete hub. If the vectors are the only thing missing, P3a's last task (vendoring P1's vectors) is still blocked on P1.

- [ ] **Step 4: Install and take a baseline**

```bash
cd "$WT"
pnpm --version
pnpm install --frozen-lockfile
pnpm exec vitest run companion/test src/lib/gadgets.test.ts src/components/CompanionSection.gadget.test.ts
pnpm test:electron
```

Expected: `10.33.0` (else switch to `corepack pnpm@10.33.0` per Global Constraints). The install finishes, and every listed test passes on `feat/gadget-hub`'s code. A failure here belongs to P3a, not P4b: stop and report it.

---

### Task 2: Release keys

**Files:**
- Create: `companion/src/gadget/release-keys.ts`
- Test: `companion/test/gadget/release-keys.test.ts`

**Interfaces:**
- Consumes: `decodePubkey(b64: string): Buffer | null` (P3a `protocol.ts`).
- Produces (contract §3.16): `interface ReleaseKey { id: string; pubkey: string }`, `RELEASE_KEYS: readonly ReleaseKey[]`, `TEST_KEY_T1: ReleaseKey`, `trustedReleaseKeys(env?: NodeJS.ProcessEnv): readonly ReleaseKey[]`.

- [ ] **Step 1: Write the failing test**

`companion/test/gadget/release-keys.test.ts`:

```ts
// The keys a firmware image must be signed with before a gadget is offered it (spec §8 Keys).
import { createECDH } from "node:crypto";
import { describe, expect, it } from "vitest";

import { decodePubkey } from "../../src/gadget/protocol.ts";
import { RELEASE_KEYS, TEST_KEY_T1, trustedReleaseKeys } from "../../src/gadget/release-keys.ts";

/** t1's private scalar: SHA-256 of "openmausbot-gadget/1 test release key t1" (contract §1.7, D12). */
const T1_PRIVATE_HEX = "274b871e29523ce21208177b90a0a3bd6a169cb84cf867236adc68f3d90fc0c8";

describe("release keys", () => {
  it("lists only r* ids, each once, each a 65-byte SEC1 point", () => {
    const ids = RELEASE_KEYS.map((key) => key.id);
    expect(new Set(ids).size).toBe(ids.length);
    for (const key of RELEASE_KEYS) {
      expect(key.id).toMatch(/^r[0-9]+$/);
      expect(decodePubkey(key.pubkey)?.length).toBe(65);
    }
  });

  it("never ships the test key as a release key", () => {
    expect(RELEASE_KEYS.some((key) => key.id === "t1" || key.pubkey === TEST_KEY_T1.pubkey)).toBe(false);
  });

  it("t1 is the SDK's published test key (contract §1.7)", () => {
    expect(TEST_KEY_T1.id).toBe("t1");
    expect(TEST_KEY_T1.pubkey).toBe("BIUTH1UeVJw2VXn0Ehdhd8apNOHmB6VvjV512SxIbCCXfUhvlLRTuOTLHtrsVAbIx06JhMoOMbC3ic73hZpxMwg=");
    const ecdh = createECDH("prime256v1");
    ecdh.setPrivateKey(Buffer.from(T1_PRIVATE_HEX, "hex"));
    expect(ecdh.getPublicKey().toString("base64")).toBe(TEST_KEY_T1.pubkey);
  });

  it("trusts t1 only when OMB_GADGET_TRUST_TEST_KEY is exactly 1", () => {
    expect(trustedReleaseKeys({})).toEqual(RELEASE_KEYS);
    expect(trustedReleaseKeys({ OMB_GADGET_TRUST_TEST_KEY: "1" })).toEqual([...RELEASE_KEYS, TEST_KEY_T1]);
    for (const value of ["true", "yes", " 1", "1 ", "0", ""]) {
      expect(trustedReleaseKeys({ OMB_GADGET_TRUST_TEST_KEY: value }).some((key) => key.id === "t1")).toBe(false);
    }
  });
});
```

- [ ] **Step 2: Run it to see it fail**

Run: `pnpm exec vitest run companion/test/gadget/release-keys.test.ts`
Expected: FAIL. Vitest cannot load `../../src/gadget/release-keys.ts` because the file does not exist.

- [ ] **Step 3: Write `companion/src/gadget/release-keys.ts`**

```ts
// The firmware release keys this companion trusts (spec §8 Keys, contract
// §3.16). Public halves only.
//
// RELEASE_KEYS is copied byte for byte from the SDK's keys/release-<id>.pub.b64
// when Omkar publishes a release key. Until then it is empty: no manifest entry
// verifies, so no gadget is ever offered an update.
//
// TEST_KEY_T1 is the SDK's published test key (keys/test-t1.pub.b64). It signs
// the fake host's and tools/release/dev-release.ts's images. Anyone can sign
// with it, so it is trusted only when OMB_GADGET_TRUST_TEST_KEY is exactly "1"
// (development and tests), and release firmware never trusts it either.

export interface ReleaseKey {
  id: string;
  /** Canonical base64 of the 65-byte SEC1 uncompressed point. */
  pubkey: string;
}

/** Release keys, ids /^r\d+$/. Empty until keys/release-r1.pub.b64 exists. */
export const RELEASE_KEYS: readonly ReleaseKey[] = [];

/** SHA-256("openmausbot-gadget/1 test release key t1") as the private scalar (contract D12). */
export const TEST_KEY_T1: ReleaseKey = {
  id: "t1",
  pubkey: "BIUTH1UeVJw2VXn0Ehdhd8apNOHmB6VvjV512SxIbCCXfUhvlLRTuOTLHtrsVAbIx06JhMoOMbC3ic73hZpxMwg=",
};

/** The keys a release checker may verify with: the release keys, plus t1
 * only when OMB_GADGET_TRUST_TEST_KEY is exactly "1". */
export function trustedReleaseKeys(env: NodeJS.ProcessEnv = process.env): readonly ReleaseKey[] {
  return env.OMB_GADGET_TRUST_TEST_KEY === "1" ? [...RELEASE_KEYS, TEST_KEY_T1] : RELEASE_KEYS;
}
```

- [ ] **Step 4: Run it to see it pass**

Run: `pnpm exec vitest run companion/test/gadget/release-keys.test.ts`
Expected: PASS (4).

- [ ] **Step 5: Commit**

```bash
git add companion/src/gadget/release-keys.ts companion/test/gadget/release-keys.test.ts
git commit -m "feat(gadget): firmware release keys and the opt-in test key" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 3: Manifest, version and custom-build rules

**Files:**
- Create: `companion/src/gadget/releases.ts` (the rules; Task 4 adds the checker)
- Create: `companion/test/gadget/helpers/test-release.ts`
- Test: `companion/test/gadget/releases.test.ts`

**Interfaces:**
- Consumes: `BOARD_ID_RE`, `RELEASE_VERSION_RE`, `SHA256_HEX_RE`, `isCanonicalBase64`, `firmwareText(board, version, size, sha256Hex): string` (P3a `protocol.ts`). The vendored `versions.json` holds `{compare: [{name, a, b, cmp}], custom: [{name, fw, custom}]}` (contract §4.4; `name` per P1's extra vector fields).
- Produces (contract §3.16):
  - Constants `MANIFEST_URL`, `RELEASE_URL_PREFIX`, `CHECK_INTERVAL_MS`, `MANIFEST_IMAGE_MAX`.
  - `interface ManifestBoard { url; size; sha256; sig; key_id }` and `interface ReleaseManifest { version; boards: Record<string, ManifestBoard> }`.
  - `parseManifest(json: unknown, options?: { base?: string; allowTestKey?: boolean }): ReleaseManifest`.
  - `compareVersions(a: string, b: string): number`, `isCustomBuild(fw: string): boolean`.
  - `updateFor(manifest: ReleaseManifest | null, board: string, fw: string): ManifestBoard | null`.
- Test helper (`helpers/test-release.ts`):
  - Constants `T1_PRIVATE_HEX`, `STRANGER_PRIVATE_HEX`, `DEV_BASE`.
  - `publicKeyOf(hex): Buffer`, `signText(hex, text): string`, `sha256Hex(bytes): string`.
  - `imageUrl(base, board, version): string`, `signedEntry({...}): ManifestBoard`, `testManifest({...}): ReleaseManifest`, `testImage(size, seed?): Buffer`.
  - `fakeFetch(routes?): { fetch; calls; set }`, `jsonResponse(value)`, `bytesResponse(bytes)`.

- [ ] **Step 1: Write the test helper**

`companion/test/gadget/helpers/test-release.ts`:

```ts
// Test releases signed with the SDK's published test key t1 (contract §1.7,
// D12), and a fetch stand-in that serves them. P4b's tests only.
import { Buffer } from "node:buffer";
import { createECDH, createHash, createPrivateKey, sign } from "node:crypto";

import { firmwareText } from "../../../src/gadget/protocol.ts";
import type { ManifestBoard, ReleaseManifest } from "../../../src/gadget/releases.ts";

/** t1's private scalar: SHA-256 of "openmausbot-gadget/1 test release key t1". */
export const T1_PRIVATE_HEX = "274b871e29523ce21208177b90a0a3bd6a169cb84cf867236adc68f3d90fc0c8";
/** A key nobody trusts: SHA-256 of "a key nobody trusts". */
export const STRANGER_PRIVATE_HEX = createHash("sha256").update("a key nobody trusts").digest("hex");
export const DEV_BASE = "http://127.0.0.1:5555/";

/** The 65-byte SEC1 public key of a private scalar. */
export function publicKeyOf(privateHex: string): Buffer {
  const ecdh = createECDH("prime256v1");
  ecdh.setPrivateKey(Buffer.from(privateHex, "hex"));
  return ecdh.getPublicKey();
}

/** Base64 DER ECDSA-P256-SHA256 over the UTF-8 text (random k). */
export function signText(privateHex: string, text: string): string {
  const pub = publicKeyOf(privateHex);
  const key = createPrivateKey({
    key: {
      kty: "EC", crv: "P-256",
      d: Buffer.from(privateHex, "hex").toString("base64url"),
      x: pub.subarray(1, 33).toString("base64url"),
      y: pub.subarray(33).toString("base64url"),
    },
    format: "jwk",
  });
  return sign("sha256", Buffer.from(text, "utf8"), { key, dsaEncoding: "der" }).toString("base64");
}

export const sha256Hex = (bytes: Uint8Array): string => createHash("sha256").update(bytes).digest("hex");
export const imageUrl = (base: string, board: string, version: string): string =>
  `${base}v${version}/openmausbot-gadget-${board}-${version}.bin`;

/** One signed manifest entry for `image`, by default for t1 under DEV_BASE. */
export function signedEntry(input: {
  board: string; version: string; image: Uint8Array; base?: string; privateHex?: string; keyId?: string;
}): ManifestBoard {
  const size = input.image.length;
  const sha256 = sha256Hex(input.image);
  return {
    url: imageUrl(input.base ?? DEV_BASE, input.board, input.version),
    size,
    sha256,
    sig: signText(input.privateHex ?? T1_PRIVATE_HEX, firmwareText(input.board, input.version, size, sha256)),
    key_id: input.keyId ?? "t1",
  };
}

/** A manifest with one signed entry per image. */
export function testManifest(input: {
  version: string; images: Record<string, Uint8Array>; base?: string; privateHex?: string; keyId?: string;
}): ReleaseManifest {
  const boards: Record<string, ManifestBoard> = {};
  for (const [board, image] of Object.entries(input.images)) {
    boards[board] = signedEntry({ board, version: input.version, image, base: input.base, privateHex: input.privateHex, keyId: input.keyId });
  }
  return { version: input.version, boards };
}

/** Deterministic pseudo-random bytes, so failures print the same offsets every run. */
export function testImage(size: number, seed = 1): Buffer {
  const out = Buffer.alloc(size);
  let x = seed >>> 0 || 1;
  for (let i = 0; i < size; i += 1) {
    x ^= x << 13; x >>>= 0; x ^= x >>> 17; x ^= x << 5; x >>>= 0;
    out[i] = x & 0xff;
  }
  return out;
}

export interface FakeFetch {
  fetch: typeof fetch;
  /** Every URL asked for, in order. */
  calls: string[];
  /** Replace or add what a URL answers. */
  set(url: string, answer: () => Response | Promise<Response>): void;
}

/** A fetch that serves only the given URLs (404 for anything else) and records every call. */
export function fakeFetch(routes: Record<string, () => Response | Promise<Response>> = {}): FakeFetch {
  const table = new Map(Object.entries(routes));
  const calls: string[] = [];
  const impl = async (input: string | URL | Request): Promise<Response> => {
    const url = typeof input === "string" ? input : input instanceof URL ? input.href : input.url;
    calls.push(url);
    const answer = table.get(url);
    return answer ? answer() : new Response("not found", { status: 404 });
  };
  return { fetch: impl as typeof fetch, calls, set: (url, answer) => void table.set(url, answer) };
}

export const jsonResponse = (value: unknown): Response =>
  new Response(JSON.stringify(value), { status: 200, headers: { "content-type": "application/json" } });
export const bytesResponse = (bytes: Uint8Array): Response =>
  new Response(bytes, { status: 200, headers: { "content-type": "application/octet-stream" } });
```

- [ ] **Step 2: Write the failing test**

`companion/test/gadget/releases.test.ts`:

```ts
// Manifest rules, version precedence and the custom-build rule (spec §8, contract §4.1),
// checked against the SDK's own vectors where they exist.
import { readFileSync } from "node:fs";
import { describe, expect, it } from "vitest";

import {
  MANIFEST_IMAGE_MAX,
  RELEASE_URL_PREFIX,
  compareVersions,
  isCustomBuild,
  parseManifest,
  updateFor,
  type ReleaseManifest,
} from "../../src/gadget/releases.ts";
import { DEV_BASE, imageUrl, signedEntry, testImage } from "./helpers/test-release.ts";

const vectors = (name: string) =>
  JSON.parse(readFileSync(new URL(`../fixtures/gadget-vectors/${name}`, import.meta.url), "utf8"));

describe("versions.json (shared with the firmware and the fake host)", () => {
  const v = vectors("versions.json") as {
    compare: Array<{ name: string; a: string; b: string; cmp: number }>;
    custom: Array<{ name: string; fw: string; custom: boolean }>;
  };

  it("orders versions by SemVer 2.0.0 precedence, both ways round", () => {
    expect(v.compare.length).toBeGreaterThanOrEqual(10);
    for (const c of v.compare) {
      expect(compareVersions(c.a, c.b), c.name).toBe(c.cmp);
      expect(compareVersions(c.b, c.a), c.name).toBe(-c.cmp || 0);
    }
  });

  it("calls -dev and anything that is not a release version a custom build", () => {
    for (const c of v.custom) expect(isCustomBuild(c.fw), c.name).toBe(c.custom);
  });
});

/** A valid one-board manifest under RELEASE_URL_PREFIX, then `change` applied to a copy. */
function manifestJson(change: (m: Record<string, any>) => void = () => {}): unknown {
  const entry = signedEntry({ board: "amoled-175c", version: "1.1.0", image: testImage(100), base: RELEASE_URL_PREFIX, keyId: "r1" });
  const m: Record<string, any> = { version: "1.1.0", boards: { "amoled-175c": { ...entry } } };
  change(m);
  return JSON.parse(JSON.stringify(m));
}

describe("parseManifest", () => {
  it("accepts the release format and keeps only the pinned fields", () => {
    const parsed = parseManifest(manifestJson((m) => { m.extra = 1; m.boards["amoled-175c"].note = "x"; }));
    expect(parsed.version).toBe("1.1.0");
    expect(Object.keys(parsed.boards)).toEqual(["amoled-175c"]);
    expect(Object.keys(parsed.boards["amoled-175c"]!).sort()).toEqual(["key_id", "sha256", "sig", "size", "url"]);
    expect(parsed.boards["amoled-175c"]!.url).toBe(
      "https://github.com/aivsomkar/openmausbot-gadget-sdk/releases/download/v1.1.0/openmausbot-gadget-amoled-175c-1.1.0.bin",
    );
  });

  it.each([
    ["not an object", () => []],
    ["no version", () => manifestJson((m) => { delete m.version; })],
    ["a -dev version", () => manifestJson((m) => { m.version = "1.1.0-dev"; })],
    ["a v-prefixed version", () => manifestJson((m) => { m.version = "v1.1.0"; })],
    ["boards not an object", () => manifestJson((m) => { m.boards = []; })],
    ["no boards", () => manifestJson((m) => { m.boards = {}; })],
    ["an upper-case board id", () => manifestJson((m) => { m.boards = { "AMOLED": m.boards["amoled-175c"] }; })],
    ["a url on another host", () => manifestJson((m) => { m.boards["amoled-175c"].url = "https://example.com/v1.1.0/openmausbot-gadget-amoled-175c-1.1.0.bin"; })],
    ["a url for another version", () => manifestJson((m) => { m.boards["amoled-175c"].url = imageUrl(RELEASE_URL_PREFIX, "amoled-175c", "1.0.0"); })],
    ["a url for another board", () => manifestJson((m) => { m.boards["amoled-175c"].url = imageUrl(RELEASE_URL_PREFIX, "lcd-154", "1.1.0"); })],
    ["size 0", () => manifestJson((m) => { m.boards["amoled-175c"].size = 0; })],
    ["size above the OTA slot", () => manifestJson((m) => { m.boards["amoled-175c"].size = MANIFEST_IMAGE_MAX + 1; })],
    ["a fractional size", () => manifestJson((m) => { m.boards["amoled-175c"].size = 1.5; })],
    ["a size given as text", () => manifestJson((m) => { m.boards["amoled-175c"].size = "100"; })],
    ["upper-case sha256", () => manifestJson((m) => { m.boards["amoled-175c"].sha256 = m.boards["amoled-175c"].sha256.toUpperCase(); })],
    ["non-canonical base64 sig", () => manifestJson((m) => { m.boards["amoled-175c"].sig = "AB=="; })],
    ["an over-long sig", () => manifestJson((m) => { m.boards["amoled-175c"].sig = "A".repeat(100); })],
    ["key_id t1 without the test key", () => manifestJson((m) => { m.boards["amoled-175c"].key_id = "t1"; })],
    ["an unknown key_id shape", () => manifestJson((m) => { m.boards["amoled-175c"].key_id = "x1"; })],
  ])("rejects %s", (_name, make) => {
    expect(() => parseManifest(make())).toThrow(/The release manifest is invalid/);
  });

  it("admits t1 only when asked to, and checks URLs against an overriding base", () => {
    const json = { version: "1.0.1", boards: { "amoled-175c": signedEntry({ board: "amoled-175c", version: "1.0.1", image: testImage(10) }) } };
    expect(() => parseManifest(json, { base: DEV_BASE })).toThrow(/key_id/);
    expect(parseManifest(json, { base: DEV_BASE, allowTestKey: true }).boards["amoled-175c"]!.key_id).toBe("t1");
    expect(() => parseManifest(json, { allowTestKey: true })).toThrow(/url/);
  });
});

describe("updateFor", () => {
  const manifest: ReleaseManifest = parseManifest(manifestJson());
  const entry = manifest.boards["amoled-175c"]!;

  it("offers a newer release for the gadget's own board", () => {
    expect(updateFor(manifest, "amoled-175c", "1.0.0")).toBe(entry);
    expect(updateFor(manifest, "amoled-175c", "1.1.0-rc.1")).toBe(entry);
  });

  it("never offers the same version, a downgrade, another board, or a custom build", () => {
    expect(updateFor(manifest, "amoled-175c", "1.1.0")).toBeNull();
    expect(updateFor(manifest, "amoled-175c", "1.2.0")).toBeNull();
    expect(updateFor(manifest, "lcd-154", "1.0.0")).toBeNull();
    expect(updateFor(manifest, "amoled-175c", "0.0.0-dev")).toBeNull();
    expect(updateFor(manifest, "amoled-175c", "1.0")).toBeNull();
    expect(updateFor(null, "amoled-175c", "1.0.0")).toBeNull();
  });

  it("does not mistake an inherited property name for a board", () => {
    for (const board of ["constructor", "toString", "valueOf", "hasOwnProperty", "__proto__"]) {
      expect(updateFor(manifest, board, "1.0.0")).toBeNull();
    }
  });
});
```

- [ ] **Step 3: Run it to see it fail**

Run: `pnpm exec vitest run companion/test/gadget/releases.test.ts`
Expected: FAIL. Vitest cannot load `../../src/gadget/releases.ts`.

- [ ] **Step 4: Write `companion/src/gadget/releases.ts` (rules)**

```ts
// Official gadget firmware: the release manifest, version rules and verified
// downloads (spec §8, contract §3.16 and §4.1). Node built-ins only.
//
// Trust comes from signatures, never from where the bytes came from: every
// board entry must verify against a trusted release key before it can show
// "Update available", again before its image is downloaded, and the image
// must then match the signed size and SHA-256. GitHub's redirects, a mirror
// or a local dev server are all just transport.
import {
  BOARD_ID_RE,
  RELEASE_VERSION_RE,
  SHA256_HEX_RE,
  isCanonicalBase64,
} from "./protocol.ts";

export const MANIFEST_URL = "https://github.com/aivsomkar/openmausbot-gadget-sdk/releases/latest/download/manifest.json";
export const RELEASE_URL_PREFIX = "https://github.com/aivsomkar/openmausbot-gadget-sdk/releases/download/";
export const CHECK_INTERVAL_MS = 24 * 60 * 60 * 1000;

/** The largest image a manifest may describe: every board's OTA slot (contract §2.3, §4.1). */
export const MANIFEST_IMAGE_MAX = 6_291_456;
const RELEASE_KEY_ID_RE = /^r[0-9]+$/;
const TEST_KEY_ID = "t1";

export interface ManifestBoard { url: string; size: number; sha256: string; sig: string; key_id: string }
export interface ReleaseManifest { version: string; boards: Record<string, ManifestBoard> }

function isRecord(value: unknown): value is Record<string, unknown> {
  return value !== null && typeof value === "object" && !Array.isArray(value);
}

function invalid(what: string): never {
  throw new Error(`The release manifest is invalid: ${what}.`);
}

/** Validates every field (contract §4.1); throws on the first violation. The url
 *  rule is url === `${base}v${version}/openmausbot-gadget-${board}-${version}.bin`,
 *  where base defaults to RELEASE_URL_PREFIX. `allowTestKey` (P4b-private) admits
 *  key_id "t1" next to /^r[0-9]+$/; the checker sets it only when t1 is trusted. */
export function parseManifest(json: unknown, options: { base?: string; allowTestKey?: boolean } = {}): ReleaseManifest {
  const base = options.base ?? RELEASE_URL_PREFIX;
  if (!isRecord(json)) invalid("it is not a JSON object");
  const { version, boards } = json;
  if (typeof version !== "string" || !RELEASE_VERSION_RE.test(version) || version.endsWith("-dev")) {
    invalid("version is not a release version");
  }
  if (!isRecord(boards)) invalid("boards is not an object");
  const out: Record<string, ManifestBoard> = {};
  for (const [board, entry] of Object.entries(boards)) {
    if (!BOARD_ID_RE.test(board)) invalid(`board id ${JSON.stringify(board.slice(0, 40))}`);
    if (!isRecord(entry)) invalid(`boards.${board} is not an object`);
    const { url, size, sha256, sig, key_id: keyId } = entry;
    if (typeof url !== "string" || url !== `${base}v${version}/openmausbot-gadget-${board}-${version}.bin`) {
      invalid(`boards.${board}.url is not the release asset URL`);
    }
    if (typeof size !== "number" || !Number.isSafeInteger(size) || size < 1 || size > MANIFEST_IMAGE_MAX) {
      invalid(`boards.${board}.size is out of range`);
    }
    if (typeof sha256 !== "string" || !SHA256_HEX_RE.test(sha256)) invalid(`boards.${board}.sha256 is not lowercase hex`);
    if (typeof sig !== "string" || sig.length === 0 || sig.length > 96 || !isCanonicalBase64(sig)) {
      invalid(`boards.${board}.sig is not canonical base64`);
    }
    if (typeof keyId !== "string" || !(RELEASE_KEY_ID_RE.test(keyId) || (options.allowTestKey === true && keyId === TEST_KEY_ID))) {
      invalid(`boards.${board}.key_id is not a release key id`);
    }
    out[board] = { url, size, sha256, sig, key_id: keyId };
  }
  if (Object.keys(out).length === 0) invalid("it lists no boards");
  return { version, boards: out };
}

function semver(version: string): { core: number[]; pre: string[] } {
  if (!RELEASE_VERSION_RE.test(version)) throw new Error(`not a release version: ${JSON.stringify(version)}`);
  const dash = version.indexOf("-");
  return {
    core: (dash < 0 ? version : version.slice(0, dash)).split(".").map(Number),
    pre: dash < 0 ? [] : version.slice(dash + 1).split("."),
  };
}

function compareIdentifiers(a: string, b: string): number {
  const an = /^\d+$/.test(a);
  const bn = /^\d+$/.test(b);
  if (an && bn) return Math.sign(Number(a) - Number(b));
  if (an !== bn) return an ? -1 : 1;
  return a < b ? -1 : a > b ? 1 : 0;
}

/** SemVer 2.0.0 precedence: negative, 0, positive (−1, 0, 1). Throws unless both
 *  match RELEASE_VERSION_RE; callers rule out custom builds first. */
export function compareVersions(a: string, b: string): number {
  const x = semver(a);
  const y = semver(b);
  for (let i = 0; i < 3; i += 1) if (x.core[i] !== y.core[i]) return x.core[i]! < y.core[i]! ? -1 : 1;
  if (x.pre.length === 0 || y.pre.length === 0) {
    return x.pre.length === y.pre.length ? 0 : x.pre.length === 0 ? 1 : -1;
  }
  for (let i = 0; i < Math.min(x.pre.length, y.pre.length); i += 1) {
    const c = compareIdentifiers(x.pre[i]!, y.pre[i]!);
    if (c !== 0) return c;
  }
  return Math.sign(x.pre.length - y.pre.length);
}

/** fw ends in "-dev", or does not match RELEASE_VERSION_RE (spec §8 Custom builds). */
export function isCustomBuild(fw: string): boolean {
  return fw.endsWith("-dev") || !RELEASE_VERSION_RE.test(fw);
}

/** Update available: board in manifest, fw not custom, compareVersions(manifest.version, fw) > 0.
 *  Never a downgrade, and never for a board id that only names an inherited property. */
export function updateFor(manifest: ReleaseManifest | null, board: string, fw: string): ManifestBoard | null {
  if (!manifest || isCustomBuild(fw) || !Object.hasOwn(manifest.boards, board)) return null;
  return compareVersions(manifest.version, fw) > 0 ? manifest.boards[board]! : null;
}
```

- [ ] **Step 5: Run it to see it pass**

Run: `pnpm exec vitest run companion/test/gadget/releases.test.ts`
Expected: PASS (26).

- [ ] **Step 6: Commit**

```bash
git add companion/src/gadget/releases.ts companion/test/gadget/releases.test.ts companion/test/gadget/helpers/test-release.ts
git commit -m "feat(gadget): firmware manifest, SemVer and custom-build rules" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 4: The release checker: fetch, verify, download

**Files:**
- Modify: `companion/src/gadget/releases.ts` (import block; append the checker)
- Test: `companion/test/gadget/release-checker.test.ts`

**Interfaces:**
- Consumes: `ReleaseKey`, `TEST_KEY_T1` (Task 2); Task 3's rules; `decodePubkey`, `firmwareText`, `verifyP256(pub65, text, derSig): boolean` (P3a); the vendored `firmware.json` (fields `name`, `expect`, `gadget_board`, `version`, `size`, `sha256`, `sig_b64`, `key_id`).
- Produces (contract §3.16):
  - `MANIFEST_BODY_MAX`.
  - `interface VerifiedImage { board; version; image: Buffer; size; sha256; sig; keyId }`.
  - `interface ReleaseChecker { check(): Promise<void>; latest(): {...}; download(board): Promise<VerifiedImage> }`.
  - `createReleaseChecker(options: { keys; fetch?; manifestUrl?; now?; log? }): ReleaseChecker`.
  - Behaviour: `manifestUrl` falls back to `process.env.OMB_GADGET_MANIFEST_URL`, then `MANIFEST_URL`. When overridden, the URL base is `new URL("./", manifestUrl).href`.

- [ ] **Step 1: Write the failing test**

`companion/test/gadget/release-checker.test.ts`:

```ts
// Fetching the release manifest and downloading images (spec §8 Checking/Updating).
// Trust comes from signatures; the network is only transport.
import { readFileSync } from "node:fs";
import { afterEach, describe, expect, it } from "vitest";

import { TEST_KEY_T1, type ReleaseKey } from "../../src/gadget/release-keys.ts";
import { MANIFEST_URL, createReleaseChecker } from "../../src/gadget/releases.ts";
import {
  DEV_BASE,
  STRANGER_PRIVATE_HEX,
  bytesResponse,
  fakeFetch,
  imageUrl,
  jsonResponse,
  publicKeyOf,
  signedEntry,
  testImage,
  testManifest,
} from "./helpers/test-release.ts";

const MANIFEST = `${DEV_BASE}manifest.json`;
const T1: ReleaseKey[] = [TEST_KEY_T1];
const quiet = () => {};
const savedEnv = process.env.OMB_GADGET_MANIFEST_URL;
afterEach(() => {
  if (savedEnv === undefined) delete process.env.OMB_GADGET_MANIFEST_URL;
  else process.env.OMB_GADGET_MANIFEST_URL = savedEnv;
});

function served(images: Record<string, Uint8Array>, version = "1.0.1") {
  const manifest = testManifest({ version, images });
  const net = fakeFetch({ [MANIFEST]: () => jsonResponse(manifest) });
  for (const [board, image] of Object.entries(images)) net.set(imageUrl(DEV_BASE, board, version), () => bytesResponse(image));
  return { manifest, net };
}

describe("check()", () => {
  it("keeps a manifest whose entries verify, and says when", async () => {
    const { net } = served({ "amoled-175c": testImage(5000) });
    const checker = createReleaseChecker({ keys: T1, fetch: net.fetch, manifestUrl: MANIFEST, now: () => 1234, log: quiet });
    const pending = checker.check();
    expect(checker.latest().checking).toBe(true);
    await pending;
    const latest = checker.latest();
    expect(latest.checking).toBe(false);
    expect(latest.error).toBeUndefined();
    expect(latest.checkedAt).toBe(1234);
    expect(latest.manifest?.version).toBe("1.0.1");
    expect(Object.keys(latest.manifest!.boards)).toEqual(["amoled-175c"]);
    expect(net.calls).toEqual([MANIFEST]);
  });

  it("agrees with firmware.json: an entry survives exactly when the gadget would accept it", async () => {
    const cases = JSON.parse(readFileSync(new URL("../fixtures/gadget-vectors/firmware.json", import.meta.url), "utf8")).cases as Array<{
      name: string; expect: string; gadget_board: string; version: string; size: number; sha256: string; sig_b64: string; key_id: string;
    }>;
    // Contract §4.4 lists the minimum cases; P1 may add more, and every one must agree.
    expect(cases.map((c) => c.name)).toEqual(expect.arrayContaining(["t1-amoled", "other-board", "size-changed", "sha-uppercase"]));
    for (const c of cases) {
      expect(["accept", "bad_sig"], c.name).toContain(c.expect);
      // The host verifies with the manifest's board id, which is the board the gadget checks with.
      const entry = { url: imageUrl(DEV_BASE, c.gadget_board, c.version), size: c.size, sha256: c.sha256, sig: c.sig_b64, key_id: c.key_id };
      const net = fakeFetch({ [MANIFEST]: () => jsonResponse({ version: c.version, boards: { [c.gadget_board]: entry } }) });
      const checker = createReleaseChecker({ keys: T1, fetch: net.fetch, manifestUrl: MANIFEST, log: quiet });
      await checker.check();
      expect(checker.latest().manifest ? "accept" : "bad_sig", c.name).toBe(c.expect);
    }
  });

  it("drops entries signed by a key it does not trust, and never fetches their images", async () => {
    const image = testImage(4000);
    const strange = testManifest({ version: "1.0.1", images: { "amoled-175c": image }, privateHex: STRANGER_PRIVATE_HEX });
    const net = fakeFetch({ [MANIFEST]: () => jsonResponse(strange) });
    const checker = createReleaseChecker({ keys: T1, fetch: net.fetch, manifestUrl: MANIFEST, log: quiet });
    await checker.check();
    expect(checker.latest().manifest).toBeNull();
    expect(checker.latest().error).toBe("Firmware 1.0.1 is not signed by a key this app trusts.");
    await expect(checker.download("amoled-175c")).rejects.toThrow("No firmware is published for this board.");
    // A key id the app has never heard of (r1 before Omkar publishes it) is the same.
    const r1 = testManifest({ version: "1.0.1", images: { "amoled-175c": image }, keyId: "r1" });
    net.set(MANIFEST, () => jsonResponse(r1));
    await checker.check();
    expect(checker.latest().manifest).toBeNull();
    expect(net.calls).toEqual([MANIFEST, MANIFEST]);
  });

  it("keeps only the boards that verify", async () => {
    const good = signedEntry({ board: "amoled-175c", version: "1.0.1", image: testImage(10) });
    // Signed by a stranger, but claiming to be t1.
    const forged = signedEntry({ board: "lcd-154", version: "1.0.1", image: testImage(20), privateHex: STRANGER_PRIVATE_HEX, keyId: "t1" });
    const net = fakeFetch({ [MANIFEST]: () => jsonResponse({ version: "1.0.1", boards: { "amoled-175c": good, "lcd-154": forged } }) });
    const lines: string[] = [];
    const checker = createReleaseChecker({ keys: T1, fetch: net.fetch, manifestUrl: MANIFEST, log: (line) => void lines.push(line) });
    await checker.check();
    expect(Object.keys(checker.latest().manifest!.boards)).toEqual(["amoled-175c"]);
    expect(checker.latest().error).toBeUndefined();
    expect(lines.some((line) => line.includes("lcd-154"))).toBe(true);
    await expect(checker.download("lcd-154")).rejects.toThrow("No firmware is published for this board.");
    expect(net.calls).toEqual([MANIFEST]);
  });

  it("trusts a stranger's key only when it is one of the keys it was given", async () => {
    const strange = testManifest({ version: "1.0.1", images: { "amoled-175c": testImage(10) }, privateHex: STRANGER_PRIVATE_HEX, keyId: "r9" });
    const net = fakeFetch({ [MANIFEST]: () => jsonResponse(strange) });
    const keys = [{ id: "r9", pubkey: publicKeyOf(STRANGER_PRIVATE_HEX).toString("base64") }];
    const checker = createReleaseChecker({ keys, fetch: net.fetch, manifestUrl: MANIFEST, log: quiet });
    await checker.check();
    expect(checker.latest().manifest?.boards["amoled-175c"]?.key_id).toBe("r9");
  });

  it("never throws, and keeps the last good manifest through every kind of failure", async () => {
    const { net } = served({ "amoled-175c": testImage(10) });
    const checker = createReleaseChecker({ keys: T1, fetch: net.fetch, manifestUrl: MANIFEST, now: () => 7, log: quiet });
    await checker.check();
    const good = checker.latest().manifest;
    const failures: Array<[() => Response | Promise<Response>, string]> = [
      [() => new Response("nope", { status: 404 }), "No gadget firmware has been released yet."],
      [() => new Response("busy", { status: 503 }), "The release server answered 503."],
      [() => new Response("{not json", { status: 200 }), "The release manifest is not valid JSON."],
      [() => new Response("x".repeat(70 * 1024), { status: 200 }), "The release manifest is too large."],
      [() => jsonResponse({ version: "1.0.1" }), "The release manifest is invalid: boards is not an object."],
      [() => Promise.reject(new TypeError("fetch failed")), "Could not reach the release server."],
      [() => Promise.reject(new DOMException("timed out", "TimeoutError")), "The release server did not answer in time."],
    ];
    for (const [answer, message] of failures) {
      net.set(MANIFEST, answer);
      await expect(checker.check()).resolves.toBeUndefined();
      expect(checker.latest().error).toBe(message);
      expect(checker.latest().manifest).toBe(good);
      expect(checker.latest().checkedAt).toBe(7);
    }
  });

  it("shares one fetch between overlapping checks", async () => {
    const { net } = served({ "amoled-175c": testImage(10) });
    const checker = createReleaseChecker({ keys: T1, fetch: net.fetch, manifestUrl: MANIFEST, log: quiet });
    await Promise.all([checker.check(), checker.check(), checker.check()]);
    expect(net.calls).toEqual([MANIFEST]);
  });

  it("uses the GitHub URL by default and OMB_GADGET_MANIFEST_URL when set", async () => {
    delete process.env.OMB_GADGET_MANIFEST_URL;
    const net = fakeFetch();
    await createReleaseChecker({ keys: T1, fetch: net.fetch, log: quiet }).check();
    expect(net.calls).toEqual([MANIFEST_URL]);
    process.env.OMB_GADGET_MANIFEST_URL = MANIFEST;
    const dev = served({ "amoled-175c": testImage(10) });
    const checker = createReleaseChecker({ keys: T1, fetch: dev.net.fetch, log: quiet });
    await checker.check();
    expect(dev.net.calls).toEqual([MANIFEST]);
    expect(checker.latest().manifest?.version).toBe("1.0.1");
  });

  it("refuses a manifest URL that is not http or https without fetching it", async () => {
    const net = fakeFetch();
    const checker = createReleaseChecker({ keys: T1, fetch: net.fetch, manifestUrl: "file:///etc/passwd", log: quiet });
    await checker.check();
    expect(checker.latest().error).toBe("OMB_GADGET_MANIFEST_URL is not an http or https URL.");
    expect(net.calls).toEqual([]);
  });
});

describe("download()", () => {
  it("returns the verified image with everything fw.offer needs", async () => {
    const image = testImage(9000, 3);
    const { manifest, net } = served({ "amoled-175c": image });
    const checker = createReleaseChecker({ keys: T1, fetch: net.fetch, manifestUrl: MANIFEST, log: quiet });
    await checker.check();
    const verified = await checker.download("amoled-175c");
    const entry = manifest.boards["amoled-175c"]!;
    expect(verified).toMatchObject({ board: "amoled-175c", version: "1.0.1", size: 9000, sha256: entry.sha256, sig: entry.sig, keyId: "t1" });
    expect(Buffer.compare(verified.image, image)).toBe(0);
  });

  it("checks the signature before it downloads anything", async () => {
    const { net } = served({ "amoled-175c": testImage(100) });
    const checker = createReleaseChecker({ keys: T1, fetch: net.fetch, manifestUrl: MANIFEST, log: quiet });
    await checker.check();
    checker.latest().manifest!.boards["amoled-175c"]!.sha256 = "0".repeat(64);
    await expect(checker.download("amoled-175c")).rejects.toThrow("The update is not signed by a key this app trusts.");
    expect(net.calls).toEqual([MANIFEST]);
  });

  it.each([
    ["damaged bytes", (image: Uint8Array) => bytesResponse(testImage(image.length, 99)), "The download is damaged: its SHA-256 does not match the release."],
    ["a short body", (image: Uint8Array) => bytesResponse(image.subarray(1)), "The download is shorter than the release says."],
    ["a long body", (image: Uint8Array) => bytesResponse(new Uint8Array([...image, 0])), "The download is larger than the release says."],
    ["a declared length that is too long", (image: Uint8Array) => new Response(image, { headers: { "content-length": String(image.length + 1) } }), "The download is larger than the release says."],
    ["a server error", () => new Response("", { status: 502 }), "The release server answered 502."],
  ])("refuses %s", async (_name, answer, message) => {
    const image = testImage(3000);
    const { net } = served({ "amoled-175c": image });
    const checker = createReleaseChecker({ keys: T1, fetch: net.fetch, manifestUrl: MANIFEST, log: quiet });
    await checker.check();
    net.set(imageUrl(DEV_BASE, "amoled-175c", "1.0.1"), () => answer(image));
    await expect(checker.download("amoled-175c")).rejects.toThrow(message);
  });
});
```

- [ ] **Step 2: Run it to see it fail**

Run: `pnpm exec vitest run companion/test/gadget/release-checker.test.ts`
Expected: FAIL with `TypeError: createReleaseChecker is not a function`, which is how vitest reports a named export that does not exist yet (checked with vitest 4.1.10).

- [ ] **Step 3: Replace the import block of `companion/src/gadget/releases.ts`**

Replace the lines from `import {` through `} from "./protocol.ts";` (right after the header comment) with:

```ts
import { Buffer } from "node:buffer";
import { createHash } from "node:crypto";

import type { ReleaseKey } from "./release-keys.ts";
import {
  BOARD_ID_RE,
  RELEASE_VERSION_RE,
  SHA256_HEX_RE,
  decodePubkey,
  firmwareText,
  isCanonicalBase64,
  verifyP256,
} from "./protocol.ts";
```

- [ ] **Step 4: Append the checker to `companion/src/gadget/releases.ts`**

```ts

/** A manifest is a few hundred bytes per board; anything this big is not one. */
export const MANIFEST_BODY_MAX = 64 * 1024;
const CHECK_TIMEOUT_MS = 15_000;
const DOWNLOAD_TIMEOUT_MS = 5 * 60_000;

export interface VerifiedImage { board: string; version: string; image: Buffer; size: number; sha256: string; sig: string; keyId: string }
export interface ReleaseChecker {
  /** Fetch MANIFEST_URL (or OMB_GADGET_MANIFEST_URL); keeps the last good manifest. Never throws. */
  check(): Promise<void>;
  latest(): { manifest: ReleaseManifest | null; checkedAt: number | null; error?: string; checking: boolean };
  /** Verify sig (firmwareText with the manifest board id) before downloading, then size and sha256 after. */
  download(board: string): Promise<VerifiedImage>;
}

class BodyTooLarge extends Error {}

/** Read a response body, refusing more than `max` bytes without buffering them. */
async function readCapped(res: Response, max: number): Promise<Buffer> {
  const declared = Number(res.headers.get("content-length") ?? Number.NaN);
  if (Number.isFinite(declared) && declared > max) {
    await res.body?.cancel().catch(() => {});
    throw new BodyTooLarge("too large");
  }
  if (!res.body) return Buffer.alloc(0);
  const reader = res.body.getReader();
  const chunks: Buffer[] = [];
  let total = 0;
  for (;;) {
    const { done, value } = await reader.read();
    if (done) break;
    total += value.byteLength;
    if (total > max) {
      await reader.cancel().catch(() => {});
      throw new BodyTooLarge("too large");
    }
    chunks.push(Buffer.from(value));
  }
  return Buffer.concat(chunks, total);
}

/** One sentence for the Remote access row, whatever went wrong. */
function sentence(cause: unknown): string {
  if (cause instanceof Error) {
    if (cause.name === "TimeoutError" || cause.name === "AbortError") return "The release server did not answer in time.";
    if (cause.name === "TypeError" && /fetch failed/i.test(cause.message)) return "Could not reach the release server.";
    return cause.message;
  }
  return String(cause);
}

/** The folder a manifest URL lives in, which its image URLs must start with. */
function baseOf(url: string): string | null {
  try {
    const parsed = new URL(url);
    return parsed.protocol === "https:" || parsed.protocol === "http:" ? new URL("./", parsed).href : null;
  } catch {
    return null;
  }
}

export function createReleaseChecker(options: {
  keys: readonly ReleaseKey[];
  fetch?: typeof fetch;
  manifestUrl?: string;
  now?: () => number;
  /** P4b-private: where check() reports boards it dropped. */
  log?: (line: string) => void;
}): ReleaseChecker {
  const fetchImpl = options.fetch ?? fetch;
  const now = options.now ?? Date.now;
  const log = options.log ?? ((line: string) => console.log(`gadget firmware: ${line}`));
  const override = options.manifestUrl ?? process.env.OMB_GADGET_MANIFEST_URL;
  const manifestUrl = override ?? MANIFEST_URL;
  const base = override === undefined ? RELEASE_URL_PREFIX : baseOf(override);
  const allowTestKey = options.keys.some((key) => key.id === TEST_KEY_ID);
  const keys = new Map<string, Buffer>();
  for (const key of options.keys) {
    const pub = decodePubkey(key.pubkey);
    if (pub) keys.set(key.id, pub);
  }

  let manifest: ReleaseManifest | null = null;
  let checkedAt: number | null = null;
  let error: string | undefined;
  let inflight: Promise<void> | null = null;

  const verifies = (board: string, version: string, entry: ManifestBoard): boolean => {
    const pub = keys.get(entry.key_id);
    if (!pub) return false;
    return verifyP256(pub, firmwareText(board, version, entry.size, entry.sha256), Buffer.from(entry.sig, "base64"));
  };

  async function run(): Promise<void> {
    try {
      if (base === null) throw new Error("OMB_GADGET_MANIFEST_URL is not an http or https URL.");
      const res = await fetchImpl(manifestUrl, {
        redirect: "follow",
        headers: { accept: "application/json" },
        signal: AbortSignal.timeout(CHECK_TIMEOUT_MS),
      });
      if (res.status === 404) throw new Error("No gadget firmware has been released yet.");
      if (!res.ok) throw new Error(`The release server answered ${res.status}.`);
      let body: Buffer;
      try {
        body = await readCapped(res, MANIFEST_BODY_MAX);
      } catch (cause) {
        throw cause instanceof BodyTooLarge ? new Error("The release manifest is too large.") : cause;
      }
      let json: unknown;
      try {
        json = JSON.parse(body.toString("utf8"));
      } catch {
        throw new Error("The release manifest is not valid JSON.");
      }
      const parsed = parseManifest(json, { base, allowTestKey });
      const boards: Record<string, ManifestBoard> = {};
      const unsigned: string[] = [];
      for (const [board, entry] of Object.entries(parsed.boards)) {
        if (verifies(board, parsed.version, entry)) boards[board] = entry;
        else unsigned.push(board);
      }
      if (unsigned.length > 0) log(`firmware ${parsed.version} for ${unsigned.join(", ")} is not signed by a trusted key; ignored`);
      if (Object.keys(boards).length === 0) throw new Error(`Firmware ${parsed.version} is not signed by a key this app trusts.`);
      manifest = { version: parsed.version, boards };
      checkedAt = now();
      error = undefined;
    } catch (cause) {
      const message = sentence(cause);
      if (message !== error) log(`update check failed: ${message}`);
      error = message;
    }
  }

  return {
    check(): Promise<void> {
      if (!inflight) {
        inflight = run().finally(() => {
          inflight = null;
        });
      }
      return inflight;
    },
    latest() {
      return { manifest, checkedAt, checking: inflight !== null, ...(error ? { error } : {}) };
    },
    async download(board: string): Promise<VerifiedImage> {
      const current = manifest;
      if (!current || !Object.hasOwn(current.boards, board)) throw new Error("No firmware is published for this board.");
      const entry = current.boards[board]!;
      if (!verifies(board, current.version, entry)) throw new Error("The update is not signed by a key this app trusts.");
      let image: Buffer;
      try {
        const res = await fetchImpl(entry.url, { redirect: "follow", signal: AbortSignal.timeout(DOWNLOAD_TIMEOUT_MS) });
        if (!res.ok) throw new Error(`The release server answered ${res.status}.`);
        image = await readCapped(res, entry.size);
      } catch (cause) {
        throw new Error(cause instanceof BodyTooLarge ? "The download is larger than the release says." : sentence(cause));
      }
      if (image.length !== entry.size) throw new Error("The download is shorter than the release says.");
      const sha256 = createHash("sha256").update(image).digest("hex");
      if (sha256 !== entry.sha256) throw new Error("The download is damaged: its SHA-256 does not match the release.");
      return { board, version: current.version, image, size: entry.size, sha256, sig: entry.sig, keyId: entry.key_id };
    },
  };
}
```

- [ ] **Step 5: Run both release test files**

Run: `pnpm exec vitest run companion/test/gadget/releases.test.ts companion/test/gadget/release-checker.test.ts`
Expected: PASS (26 + 16).

- [ ] **Step 6: Commit**

```bash
git add companion/src/gadget/releases.ts companion/test/gadget/release-checker.test.ts
git commit -m "feat(gadget): fetch the firmware manifest and download verified images" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 5: The OTA sender (§4.8 host side)

**Files:**
- Create: `companion/src/gadget/ota.ts`
- Create: `companion/test/gadget/helpers/ota-fakes.ts`
- Test: `companion/test/gadget/ota.test.ts`

**Interfaces:**
- Consumes: `GadgetSessionHandle` with `send`, `sendBinary(kind, stream, payload)`, `allocStream`, `releaseStream`, `on(op, listener)`, `onClose`, `closed` and `hello` (P3a, contract §3.10). Also P3a's `BinaryKind.firmware`, `FW_CHUNK_BYTES`, `FW_WINDOW_BYTES`, `FW_READY_TIMEOUT_MS`, `encodeFwChunk(stream, offset, data): Buffer` (a whole frame with its 2-byte header) and `FwFailCode`; Task 4's `VerifiedImage`.
- Produces (contract §3.16):
  - `type FwPhase = "offering" | "sending" | "committing" | "restarting"`.
  - `type FwUpdateResult = { ok: true } | { ok: false; code: FwFailCode | "closed" | "ready_timeout"; message: string }`.
  - `runFirmwareUpdate(session, image, onProgress, timeouts?): Promise<FwUpdateResult>`.
  - P4b-private: `FW_FAIL_MESSAGES`, `FwUpdateTimeouts`.
- Test helper (`helpers/ota-fakes.ts`):
  - `fakeSession({deviceId, board?, fw?, ota?}): FakeSession` (each with its own `sessionId`), `fakeHello(...)`.
  - `actAsGadget(session, script?): Promise<GadgetOtaResult>`, with script options `refuse`, `silent`, `ackEvery16k`, `ackLimit`, `dropAfter`, `failCommit` and `onCommit` (run instead of closing the session after a good commit, so a test can bring the restarted gadget up before the old session closes).
  - `fakeHub(): FakeHub` (`connect(session)`, `readyListenerCount()`).

- [ ] **Step 1: Write the test helper**

`companion/test/gadget/helpers/ota-fakes.ts`:

```ts
// Stand-ins for one live gadget session and the hub, plus a scripted gadget
// that answers a firmware update the way the firmware does (spec §4.8).
// P4b's unit tests only; the hub test uses P3a's real hub instead.
import { Buffer } from "node:buffer";
import { createHash } from "node:crypto";

import type { NormalizedHello } from "../../../src/gadget/enroll.ts";
import type { GadgetHub } from "../../../src/gadget/hub.ts";
import type { BinaryKindValue, GadgetCaps, GadgetOp, GadgetToHost, HostToGadget } from "../../../src/gadget/protocol.ts";
import type { GadgetSessionHandle } from "../../../src/gadget/session.ts";

export interface SentBinary { kind: BinaryKindValue; stream: number; payload: Buffer }

export interface FakeSession extends GadgetSessionHandle {
  readonly sent: HostToGadget[];
  readonly binaries: SentBinary[];
  readonly released: number[];
  /** What sendBinary answers (false = the socket is gone). */
  binaryOk: boolean;
  /** Deliver a gadget → host op to the listeners. */
  emit(message: GadgetToHost): void;
  /** Close the session (the gadget restarted or dropped). */
  close(code?: number): void;
  /** The next text frame with this op that the host sent (frames are buffered). */
  nextSent<K extends HostToGadget["op"]>(op: K): Promise<Extract<HostToGadget, { op: K }>>;
  /** The next binary frame the host sent (frames are buffered). */
  nextBinary(): Promise<SentBinary>;
  listenerCount(): number;
}

export function fakeHello(input: { deviceId: string; board?: string; fw?: string; ota?: number | null }): NormalizedHello {
  const caps: GadgetCaps = { screen: { w: 466, h: 466, round: true, text: "latin1" } };
  if (input.ota !== null) caps.ota = { max: input.ota ?? 6_291_456 };
  return {
    proto: 1, id: input.deviceId, pubkey: "", pubkeyBytes: Buffer.alloc(65), name: "Desk Maus",
    board: input.board ?? "amoled-175c", fw: input.fw ?? "1.0.0", caps, actions: [], sensors: {},
  };
}

/** Every fake session gets its own id, as P3a's do ("s_" + 12 hex). */
let sessionCount = 0;

export function fakeSession(input: { deviceId: string; board?: string; fw?: string; ota?: number | null }): FakeSession {
  const listeners = new Map<string, Set<(message: GadgetToHost) => void>>();
  const closeListeners = new Set<(code: number) => void>();
  const sent: HostToGadget[] = [];
  const binaries: SentBinary[] = [];
  const released: number[] = [];
  const textCursor = new Map<string, number>();
  let binaryCursor = 0;
  const waiters: Array<() => void> = [];
  let streams = 0;
  let isClosed = false;
  const notify = (): void => {
    for (const wake of waiters.splice(0)) wake();
  };
  const waitForChange = (): Promise<void> => new Promise((resolve) => void waiters.push(resolve));

  const session: FakeSession = {
    deviceId: input.deviceId,
    sessionId: `s_${(sessionCount += 1).toString(16).padStart(12, "0")}`,
    hello: fakeHello(input),
    connectedAt: 0,
    get closed() { return isClosed; },
    sent,
    binaries,
    released,
    binaryOk: true,
    send(message) {
      if (isClosed) return false;
      sent.push(message);
      notify();
      return true;
    },
    async sendBinary(kind, stream, payload) {
      if (isClosed || !session.binaryOk) return false;
      binaries.push({ kind, stream, payload: Buffer.from(payload) });
      notify();
      return true;
    },
    allocStream() {
      streams = (streams % 255) + 1;
      return streams;
    },
    releaseStream(stream) {
      released.push(stream);
    },
    on<K extends GadgetOp>(op: K, listener: (message: Extract<GadgetToHost, { op: K }>) => void): () => void {
      const set = listeners.get(op) ?? new Set();
      const untyped = listener as (message: GadgetToHost) => void;
      set.add(untyped);
      listeners.set(op, set);
      return () => void set.delete(untyped);
    },
    onClose(listener) {
      closeListeners.add(listener);
      return () => void closeListeners.delete(listener);
    },
    emit(message) {
      for (const listener of Array.from(listeners.get(message.op) ?? [])) listener(message);
    },
    close(code = 1000) {
      if (isClosed) return;
      isClosed = true;
      for (const listener of Array.from(closeListeners)) listener(code);
      notify();
    },
    async nextSent<K extends HostToGadget["op"]>(op: K): Promise<Extract<HostToGadget, { op: K }>> {
      for (;;) {
        const from = textCursor.get(op) ?? 0;
        const index = sent.findIndex((m, i) => i >= from && m.op === op);
        if (index >= 0) {
          textCursor.set(op, index + 1);
          return sent[index] as Extract<HostToGadget, { op: K }>;
        }
        await waitForChange();
      }
    },
    async nextBinary(): Promise<SentBinary> {
      while (binaryCursor >= binaries.length) await waitForChange();
      return binaries[binaryCursor++]!;
    },
    listenerCount() {
      let n = closeListeners.size;
      for (const set of listeners.values()) n += set.size;
      return n;
    },
  };
  return session;
}

export interface GadgetOtaScript {
  /** Answer the offer with this fw.fail code instead of fw.ready. */
  refuse?: string;
  /** Never answer the offer. */
  silent?: boolean;
  /** Send fw.progress every 16 KiB (default) or only at the end. */
  ackEvery16k?: boolean;
  /** Stop acknowledging once this many bytes were acknowledged. */
  ackLimit?: number;
  /** Close the session after this many bytes. */
  dropAfter?: number;
  /** Answer fw.commit with this fw.fail code instead of restarting. */
  failCommit?: string;
  /** After a good commit, run this instead of closing the session. An ESP32 restart sends no
   *  FIN, so the restarted gadget can be ready before its old session closes; `() => {}`
   *  leaves the old session open. */
  onCommit?: () => void;
}
export interface GadgetOtaResult {
  offer: Extract<HostToGadget, { op: "fw.offer" }>;
  image?: Buffer;
  chunkSizes: number[];
}

/** The gadget side of §4.8 over a fake session: receives contiguous chunks,
 *  acknowledges every 16 KiB crossing and the end, then restarts on commit. */
export async function actAsGadget(session: FakeSession, script: GadgetOtaScript = {}): Promise<GadgetOtaResult> {
  const offer = await session.nextSent("fw.offer");
  if (script.silent) return { offer, chunkSizes: [] };
  if (script.refuse) {
    session.emit({ op: "fw.fail", stream: offer.stream, code: script.refuse });
    return { offer, chunkSizes: [] };
  }
  session.emit({ op: "fw.ready", stream: offer.stream });
  const image = Buffer.alloc(offer.size);
  const chunkSizes: number[] = [];
  let written = 0;
  let acked = 0;
  while (written < offer.size) {
    const frame = await session.nextBinary();
    if (frame.kind !== 0x04 || frame.stream !== offer.stream) throw new Error(`unexpected frame kind ${frame.kind} stream ${frame.stream}`);
    const offset = frame.payload.readUInt32LE(0);
    const data = frame.payload.subarray(4);
    if (offset !== written) throw new Error(`chunk at ${offset}, expected ${written}`);
    data.copy(image, written);
    written += data.length;
    chunkSizes.push(data.length);
    if (script.dropAfter !== undefined && written >= script.dropAfter) {
      session.close(1006);
      return { offer, chunkSizes };
    }
    const crossed = Math.floor(written / 16_384) > Math.floor(acked / 16_384);
    const stalled = script.ackLimit !== undefined && acked >= script.ackLimit;
    if (!stalled && ((script.ackEvery16k !== false && crossed) || written === offer.size)) {
      session.emit({ op: "fw.progress", stream: offer.stream, offset: written });
      acked = written;
    }
  }
  await session.nextSent("fw.commit");
  if (createHash("sha256").update(image).digest("hex") !== offer.sha256) {
    session.emit({ op: "fw.fail", stream: offer.stream, code: "checksum" });
  } else if (script.failCommit) {
    session.emit({ op: "fw.fail", stream: offer.stream, code: script.failCommit });
  } else if (script.onCommit) {
    script.onCommit();
  } else {
    session.close(1001);
  }
  return { offer, image, chunkSizes };
}

export interface FakeHub extends GadgetHub {
  /** Make a session live and fire onSessionReady, as the real hub does after `ready`. */
  connect(session: GadgetSessionHandle): void;
  readyListenerCount(): number;
}

export function fakeHub(): FakeHub {
  const sessions = new Map<string, GadgetSessionHandle>();
  const ready = new Set<(session: GadgetSessionHandle) => void>();
  return {
    isGadgetPath: () => false,
    handleUpgrade: () => {},
    session: (deviceId) => {
      const live = sessions.get(deviceId);
      return live && !live.closed ? live : null;
    },
    online: () => [...sessions.keys()].filter((id) => !sessions.get(id)!.closed),
    onSessionReady(listener) {
      ready.add(listener);
      return () => void ready.delete(listener);
    },
    settingsChanged: () => {},
    revoke: () => {},
    recentEvents: () => [],
    botName: () => null,
    close: async () => {},
    connect(session) {
      sessions.set(session.deviceId, session);
      for (const listener of Array.from(ready)) listener(session);
    },
    readyListenerCount: () => ready.size,
  };
}
```

- [ ] **Step 2: Write the failing test**

`companion/test/gadget/ota.test.ts`:

```ts
// The host side of §4.8 on one session: offer, window, commit, and every way it ends.
import { describe, expect, it } from "vitest";

import { FW_FAIL_MESSAGES, runFirmwareUpdate, type FwPhase } from "../../src/gadget/ota.ts";
import type { VerifiedImage } from "../../src/gadget/releases.ts";
import { actAsGadget, fakeSession } from "./helpers/ota-fakes.ts";
import { sha256Hex, testImage } from "./helpers/test-release.ts";

function verified(size: number, version = "1.0.1"): VerifiedImage {
  const image = testImage(size, 5);
  return { board: "amoled-175c", version, image, size, sha256: sha256Hex(image), sig: "MEUCIQ==", keyId: "t1" };
}
const fast = { readyMs: 50, progressMs: 50, commitMs: 50 };

describe("runFirmwareUpdate", () => {
  it("offers, streams contiguous 4 KiB chunks inside a 64 KiB window, commits, and waits for the restart", async () => {
    const session = fakeSession({ deviceId: "gad_0000000000000001" });
    const image = verified(200_001);
    const phases: Array<[FwPhase, number]> = [];
    const gadget = actAsGadget(session);
    const result = await runFirmwareUpdate(session, image, (phase, offset, size) => {
      expect(size).toBe(200_001);
      phases.push([phase, offset]);
    });
    const seen = await gadget;
    expect(result).toEqual({ ok: true });
    expect(seen.offer).toEqual({
      op: "fw.offer", stream: 1, board: "amoled-175c", version: "1.0.1", size: 200_001,
      sha256: image.sha256, sig: "MEUCIQ==", key_id: "t1",
    });
    expect(Buffer.compare(seen.image!, image.image)).toBe(0);
    expect(seen.chunkSizes.slice(0, -1).every((n) => n === 4096)).toBe(true);
    expect(seen.chunkSizes.at(-1)).toBe(200_001 % 4096);
    expect(session.sent.map((m) => m.op)).toEqual(["fw.offer", "fw.commit"]);
    expect(phases[0]).toEqual(["offering", 0]);
    expect(phases[1]).toEqual(["sending", 0]);
    expect(phases.filter(([p]) => p === "sending").map(([, o]) => o)).toEqual(
      [0, ...Array.from({ length: 12 }, (_, i) => (i + 1) * 16_384), 200_001],
    );
    expect(phases.slice(-2)).toEqual([["committing", 200_001], ["restarting", 200_001]]);
    expect(session.released).toEqual([1]);
    expect(session.listenerCount()).toBe(0);
  });

  it("never sends more than 64 KiB past the gadget's last fw.progress", async () => {
    const session = fakeSession({ deviceId: "gad_0000000000000002" });
    void actAsGadget(session, { ackEvery16k: false });
    const result = await runFirmwareUpdate(session, verified(300_000), () => {}, fast);
    expect(result).toEqual({ ok: false, code: "timeout", message: FW_FAIL_MESSAGES.timeout });
    expect(session.binaries.reduce((n, b) => n + b.payload.length - 4, 0)).toBe(65_536);
    expect(session.released).toEqual([1]);
  });

  it("slides the window: after an ack at 16 KiB it sends at most 64 KiB past that ack", async () => {
    const session = fakeSession({ deviceId: "gad_000000000000000b" });
    void actAsGadget(session, { ackLimit: 16_384 });
    expect(await runFirmwareUpdate(session, verified(300_000), () => {}, fast)).toMatchObject({ ok: false, code: "timeout" });
    expect(session.binaries.reduce((n, b) => n + b.payload.length - 4, 0)).toBe(16_384 + 65_536);
  });

  it("reports the gadget's refusal of the offer and sends no chunk", async () => {
    for (const code of ["wrong_board", "same_version", "too_large", "unknown_key", "bad_sig", "busy"] as const) {
      const session = fakeSession({ deviceId: "gad_0000000000000003" });
      void actAsGadget(session, { refuse: code });
      expect(await runFirmwareUpdate(session, verified(10_000), () => {}, fast)).toEqual({ ok: false, code, message: FW_FAIL_MESSAGES[code] });
      expect(session.binaries).toEqual([]);
    }
  });

  it("gives up when fw.ready does not come", async () => {
    const session = fakeSession({ deviceId: "gad_0000000000000004" });
    void actAsGadget(session, { silent: true });
    expect(await runFirmwareUpdate(session, verified(10_000), () => {}, fast)).toMatchObject({ ok: false, code: "ready_timeout" });
  });

  it("ends with closed when the gadget drops mid-stream, and frees the stream", async () => {
    const session = fakeSession({ deviceId: "gad_0000000000000005" });
    void actAsGadget(session, { dropAfter: 40_000 });
    expect(await runFirmwareUpdate(session, verified(200_000), () => {}, fast)).toEqual({ ok: false, code: "closed", message: FW_FAIL_MESSAGES.closed });
    expect(session.released).toEqual([1]);
    expect(session.listenerCount()).toBe(0);
  });

  it("ends with closed when the socket refuses a chunk", async () => {
    const session = fakeSession({ deviceId: "gad_0000000000000006" });
    session.binaryOk = false;
    void actAsGadget(session);
    expect(await runFirmwareUpdate(session, verified(10_000), () => {}, fast)).toMatchObject({ ok: false, code: "closed" });
  });

  it("reports a fw.fail after fw.commit (the gadget's own checksum or flash check)", async () => {
    const session = fakeSession({ deviceId: "gad_0000000000000007" });
    void actAsGadget(session, { failCommit: "flash" });
    expect(await runFirmwareUpdate(session, verified(20_000), () => {}, fast)).toEqual({ ok: false, code: "flash", message: FW_FAIL_MESSAGES.flash });
  });

  it("ignores progress offsets that go backwards or past what was sent", async () => {
    const session = fakeSession({ deviceId: "gad_0000000000000008" });
    const image = verified(50_000);
    const offsets: number[] = [];
    const run = runFirmwareUpdate(session, image, (phase, offset) => { if (phase === "sending") offsets.push(offset); }, fast);
    const offer = await session.nextSent("fw.offer");
    session.emit({ op: "fw.ready", stream: offer.stream });
    await session.nextBinary();
    session.emit({ op: "fw.progress", stream: offer.stream, offset: 999_999 });
    session.emit({ op: "fw.progress", stream: offer.stream, offset: 0 });
    session.emit({ op: "fw.progress", stream: 99, offset: 4096 });
    session.emit({ op: "fw.progress", stream: offer.stream, offset: 50_000 });
    await session.nextSent("fw.commit");
    session.close();
    expect(await run).toEqual({ ok: true });
    expect(offsets).toEqual([0, 50_000]);
  });

  it("names an unknown fw.fail code instead of inventing a meaning", async () => {
    const session = fakeSession({ deviceId: "gad_0000000000000009" });
    void actAsGadget(session, { refuse: "battery_low" });
    expect(await runFirmwareUpdate(session, verified(10), () => {}, fast)).toEqual({
      ok: false, code: "flash", message: "The gadget refused the update (battery_low).",
    });
  });

  it("does nothing on a session that is already closed", async () => {
    const session = fakeSession({ deviceId: "gad_000000000000000a" });
    session.close();
    expect(await runFirmwareUpdate(session, verified(10), () => {})).toMatchObject({ ok: false, code: "closed" });
    expect(session.sent).toEqual([]);
  });
});
```

- [ ] **Step 3: Run it to see it fail**

Run: `pnpm exec vitest run companion/test/gadget/ota.test.ts`
Expected: FAIL. Vitest cannot load `../../src/gadget/ota.ts`.

- [ ] **Step 4: Write `companion/src/gadget/ota.ts`**

```ts
// The host side of a firmware update on one live gadget session (spec §4.8,
// contract §3.16): fw.offer, wait for fw.ready, stream 4 KiB chunks with at
// most 64 KiB unacknowledged, fw.commit, then wait for the gadget to restart.
//
// Every fw.* reply is queued as it arrives, so a fw.progress that lands while
// chunks are still being written is never lost (losing one would stall a full
// window until the progress timeout).
import type { GadgetSessionHandle } from "./session.ts";
import type { VerifiedImage } from "./releases.ts";
import {
  BinaryKind,
  FW_CHUNK_BYTES,
  FW_READY_TIMEOUT_MS,
  FW_WINDOW_BYTES,
  encodeFwChunk,
  type FwFailCode,
} from "./protocol.ts";

export type FwPhase = "offering" | "sending" | "committing" | "restarting";
export type FwUpdateResult =
  | { ok: true }                                    // fw.commit sent; fw.installed arrives on a later session
  | { ok: false; code: FwFailCode | "closed" | "ready_timeout"; message: string };

/** P4b-private: shorter waits for tests. Defaults follow spec §4.8. */
export interface FwUpdateTimeouts {
  /** fw.ready after fw.offer (spec: 10 s). */
  readyMs?: number;
  /** fw.progress while chunks are outstanding. The gadget gives up after 30 s without a chunk. */
  progressMs?: number;
  /** After fw.commit: a fw.fail or the gadget's restart. The gadget checks SHA-256 and switches slots first. */
  commitMs?: number;
}
const PROGRESS_TIMEOUT_MS = 35_000;
const COMMIT_WAIT_MS = 60_000;

const FAIL_CODES: ReadonlySet<string> = new Set<FwFailCode>([
  "too_large", "wrong_board", "same_version", "unknown_key", "bad_sig",
  "busy", "flash", "sequence", "checksum", "timeout",
]);

/** What the Remote access row says for each outcome (Latin-1, one sentence). */
export const FW_FAIL_MESSAGES: Readonly<Record<FwFailCode | "closed" | "ready_timeout", string>> = {
  too_large: "The update is larger than this gadget's update slot.",
  wrong_board: "The update is for a different board.",
  same_version: "The gadget already runs this version.",
  unknown_key: "The gadget does not trust the key this update is signed with.",
  bad_sig: "The gadget could not verify the update's signature.",
  busy: "The gadget is busy. Try again when it is idle.",
  flash: "The gadget could not write the update to its flash.",
  sequence: "The update arrived out of order. Try again.",
  checksum: "The update was damaged on the way. Try again.",
  timeout: "The gadget stopped responding during the update.",
  closed: "The gadget disconnected during the update.",
  ready_timeout: "The gadget did not accept the update.",
};

type Inbound =
  | { op: "fw.ready" }
  | { op: "fw.fail"; code: string }
  | { op: "fw.progress"; offset: number }
  | { op: "closed" }
  | { op: "timeout" };

function failed(code: FwFailCode | "closed" | "ready_timeout"): FwUpdateResult {
  return { ok: false, code, message: FW_FAIL_MESSAGES[code] };
}

function fromGadget(code: string): FwUpdateResult {
  if (FAIL_CODES.has(code)) return failed(code as FwFailCode);
  return { ok: false, code: "flash", message: `The gadget refused the update (${code.slice(0, 32)}).` };
}

/** fw.offer (stream from allocStream, board = manifest board id) → fw.ready within 10 s →
 *  4096-byte chunks with ≤ 64 KiB unacknowledged (acked by fw.progress offsets) → fw.commit. */
export async function runFirmwareUpdate(
  session: GadgetSessionHandle,
  image: VerifiedImage,
  onProgress: (phase: FwPhase, offset: number, size: number) => void,
  timeouts: FwUpdateTimeouts = {},
): Promise<FwUpdateResult> {
  if (session.closed) return failed("closed");
  const stream = session.allocStream();
  const inbox: Inbound[] = [];
  let waiter: ((message: Inbound) => void) | null = null;
  const deliver = (message: Inbound): void => {
    if (waiter) {
      const wake = waiter;
      waiter = null;
      wake(message);
    } else {
      inbox.push(message);
    }
  };
  const unsubscribe = [
    session.on("fw.ready", (m) => { if (m.stream === stream) deliver({ op: "fw.ready" }); }),
    session.on("fw.fail", (m) => { if (m.stream === stream) deliver({ op: "fw.fail", code: String(m.code) }); }),
    session.on("fw.progress", (m) => { if (m.stream === stream) deliver({ op: "fw.progress", offset: m.offset }); }),
    session.onClose(() => deliver({ op: "closed" })),
  ];
  const next = (ms: number): Promise<Inbound> => {
    const queued = inbox.shift();
    if (queued) return Promise.resolve(queued);
    return new Promise((resolve) => {
      const timer = setTimeout(() => {
        waiter = null;
        resolve({ op: "timeout" });
      }, ms);
      waiter = (message) => {
        clearTimeout(timer);
        resolve(message);
      };
    });
  };

  try {
    const total = image.size;
    onProgress("offering", 0, total);
    const offered = session.send({
      op: "fw.offer", stream, board: image.board, version: image.version, size: total,
      sha256: image.sha256, sig: image.sig, key_id: image.keyId,
    });
    if (!offered) return failed("closed");

    const answer = await next(timeouts.readyMs ?? FW_READY_TIMEOUT_MS);
    if (answer.op === "fw.fail") return fromGadget(answer.code);
    if (answer.op === "closed") return failed("closed");
    if (answer.op !== "fw.ready") return failed("ready_timeout");

    let sent = 0;
    let acked = 0;
    onProgress("sending", 0, total);
    while (acked < total) {
      while (sent < total && sent - acked < FW_WINDOW_BYTES) {
        const n = Math.min(FW_CHUNK_BYTES, total - sent, FW_WINDOW_BYTES - (sent - acked));
        const payload = encodeFwChunk(stream, sent, image.image.subarray(sent, sent + n)).subarray(2);
        if (!(await session.sendBinary(BinaryKind.firmware, stream, payload))) return failed("closed");
        sent += n;
      }
      const message = await next(timeouts.progressMs ?? PROGRESS_TIMEOUT_MS);
      if (message.op === "fw.progress") {
        // Offsets only ever move forward and never past what was sent.
        if (Number.isSafeInteger(message.offset) && message.offset > acked && message.offset <= sent) {
          acked = message.offset;
          onProgress("sending", acked, total);
        }
        continue;
      }
      if (message.op === "fw.ready") continue;
      if (message.op === "fw.fail") return fromGadget(message.code);
      return failed(message.op === "closed" ? "closed" : "timeout");
    }

    onProgress("committing", total, total);
    if (!session.send({ op: "fw.commit", stream })) return failed("closed");
    for (;;) {
      const after = await next(timeouts.commitMs ?? COMMIT_WAIT_MS);
      if (after.op === "fw.fail") return fromGadget(after.code);
      // The gadget restarts on success, which closes this session. No answer
      // at all also counts: fw.installed (or a rollback) settles it later.
      if (after.op === "closed" || after.op === "timeout") break;
    }
    onProgress("restarting", total, total);
    return { ok: true };
  } finally {
    for (const off of unsubscribe) off();
    session.releaseStream(stream);
  }
}
```

- [ ] **Step 5: Run it to see it pass**

Run: `pnpm exec vitest run companion/test/gadget/ota.test.ts`
Expected: PASS (11).

- [ ] **Step 6: Commit**

```bash
git add companion/src/gadget/ota.ts companion/test/gadget/ota.test.ts companion/test/gadget/helpers/ota-fakes.ts
git commit -m "feat(gadget): stream signed firmware to a gadget with a 64 KiB window" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 6: The firmware service

**Files:**
- Create: `companion/src/gadget/firmware.ts`
- Test: `companion/test/gadget/firmware.test.ts`

**Interfaces:**
- Consumes:
  - `DeviceRegistry.gadget(id)`, `.gadgets()`, `.openPairing()`, `.enrollGadget(code, GadgetEnrollment)` and `.revoke(id)` (P3a, contract §3.5).
  - `GadgetHub.session(id)` and `.onSessionReady(listener)` (P3a, §3.11). `GadgetSessionHandle.on("fw.installed", …)`, `.sessionId` and `.hello` (§3.10).
  - `ReleaseChecker`, `VerifiedImage`, `isCustomBuild`, `updateFor`, `CHECK_INTERVAL_MS` (Tasks 3–4). `runFirmwareUpdate`, `FwUpdateTimeouts` (Task 5).
  - `DATA_DIR` (`companion/src/state.ts`).
- Produces (contract §3.16):
  - `GadgetUpdatePhase`, `GadgetUpdateStatus`, `GadgetFirmwareState`, `GadgetFirmwareService { state; requestCheck; startUpdate; close }`.
  - `createGadgetFirmwareService({ devices, hub, checker, log?, now?, checkIntervalMs?, restartTimeoutMs?, otaTimeouts?, onChange? })`.
  - P4b-private: `INSTALLED_TTL_MS`, `RESTART_TIMEOUT_MS`.

- [ ] **Step 1: Write the failing test**

`companion/test/gadget/firmware.test.ts`:

```ts
// The firmware service behind the Update button and /state (spec §8, contract §3.16).
import { rmSync } from "node:fs";
import { afterEach, beforeEach, describe, expect, it, vi } from "vitest";

import { DeviceRegistry } from "../../src/devices.ts";
import {
  INSTALLED_TTL_MS,
  createGadgetFirmwareService,
  type GadgetFirmwareService,
  type GadgetUpdatePhase,
  type GadgetUpdateStatus,
} from "../../src/gadget/firmware.ts";
import type { ReleaseChecker, ReleaseManifest, VerifiedImage } from "../../src/gadget/releases.ts";
import { DATA_DIR } from "../../src/state.ts";
import { actAsGadget, fakeHub, fakeSession, type FakeHub, type FakeSession } from "./helpers/ota-fakes.ts";
import { sha256Hex, testImage, testManifest } from "./helpers/test-release.ts";

// The RFC 6979 A.2.5 key's pubkey and id (contract §1.7): a real pair, as the registry stores it.
const ID = "gad_b18b86ce1389e46d";
const PUBKEY = "BGD+1LolWp0xyWHrdMY1bWjASbiSO2H6bOZpYi5g8p+2eQP+EAi4vJmkGunpVii8ZPLxsgwtfp9Rd6PClNRGIpk=";

/** A real registry with one enrolled gadget (contract §3.5 enrollGadget). */
function pairedRegistry(firmware = "1.0.0", id: string = ID): DeviceRegistry {
  const devices = new DeviceRegistry();
  const { code } = devices.openPairing();
  const enrolled = devices.enrollGadget(code, { id, publicKey: PUBKEY, name: "Desk Maus", board: "amoled-175c", firmware, botId: null });
  if ("error" in enrolled) throw new Error(enrolled.message);
  return devices;
}

interface StubChecker extends ReleaseChecker {
  checks: number;
  manifest: ReleaseManifest | null;
  downloadError: Error | null;
  checking: boolean;
  error?: string;
  checkedAt: number | null;
}
function stubChecker(manifest: ReleaseManifest | null, image?: Uint8Array): StubChecker {
  const stub: StubChecker = {
    checks: 0,
    manifest,
    downloadError: null,
    checking: false,
    checkedAt: manifest ? 0 : null,
    async check() { stub.checks += 1; },
    latest() { return { manifest: stub.manifest, checkedAt: stub.checkedAt, checking: stub.checking, ...(stub.error ? { error: stub.error } : {}) }; },
    async download(board): Promise<VerifiedImage> {
      if (stub.downloadError) throw stub.downloadError;
      const entry = stub.manifest!.boards[board]!;
      const bytes = Buffer.from(image!);
      return { board, version: stub.manifest!.version, image: bytes, size: bytes.length, sha256: sha256Hex(bytes), sig: entry.sig, keyId: entry.key_id };
    },
  };
  return stub;
}

let service: GadgetFirmwareService | null = null;
beforeEach(() => rmSync(DATA_DIR, { recursive: true, force: true }));
afterEach(() => {
  service?.close();
  service = null;
  vi.useRealTimers();
});

function start(input: { devices: DeviceRegistry; hub: FakeHub; checker: ReleaseChecker; now?: () => number; restartTimeoutMs?: number; checkIntervalMs?: number }) {
  const changes: Array<[string, GadgetUpdateStatus]> = [];
  const waiters: Array<() => void> = [];
  service = createGadgetFirmwareService({
    ...input,
    log: () => {},
    otaTimeouts: { readyMs: 50, progressMs: 50, commitMs: 50 },
    onChange: (id, status) => {
      changes.push([id, status]);
      for (const wake of waiters.splice(0)) wake();
    },
  });
  /** Resolves once the device reaches the phase (already reached counts). */
  const reach = async (phase: GadgetUpdatePhase): Promise<GadgetUpdateStatus> => {
    for (;;) {
      const hit = changes.find(([, s]) => s.phase === phase);
      if (hit) return hit[1];
      await new Promise<void>((resolve) => void waiters.push(resolve));
    }
  };
  return { service, changes, reach, phases: () => changes.map(([, s]) => s.phase) };
}

function online(hub: FakeHub, fw = "1.0.0", extra: { ota?: number | null; board?: string } = {}): FakeSession {
  const session = fakeSession({ deviceId: ID, fw, ...extra });
  hub.connect(session);
  return session;
}

describe("when it looks for a release", () => {
  it("never fetches while no gadget is paired", () => {
    vi.useFakeTimers();
    const checker = stubChecker(null);
    const { service } = start({ devices: new DeviceRegistry(), hub: fakeHub(), checker, checkIntervalMs: 1000 });
    service.requestCheck();
    vi.advanceTimersByTime(5000);
    expect(checker.checks).toBe(0);
  });

  it("checks on start and then on every interval while a gadget is paired", () => {
    vi.useFakeTimers();
    const checker = stubChecker(null);
    start({ devices: pairedRegistry(), hub: fakeHub(), checker, checkIntervalMs: 1000 });
    expect(checker.checks).toBe(1);
    vi.advanceTimersByTime(3000);
    expect(checker.checks).toBe(4);
  });

  it("checks every time Settings opens while a gadget is paired (the checker shares overlapping fetches)", () => {
    const checker = stubChecker(null);
    const { service } = start({ devices: pairedRegistry(), hub: fakeHub(), checker });
    expect(checker.checks).toBe(1);
    service.requestCheck();
    service.requestCheck();
    expect(checker.checks).toBe(3);
  });

  it("stops checking once closed", () => {
    vi.useFakeTimers();
    const checker = stubChecker(null);
    const hub = fakeHub();
    const { service } = start({ devices: pairedRegistry(), hub, checker, checkIntervalMs: 1000 });
    service.close();
    vi.advanceTimersByTime(5000);
    service.requestCheck();
    expect(checker.checks).toBe(1);
    expect(hub.readyListenerCount()).toBe(0);
  });
});

describe("startUpdate refusals", () => {
  const manifest = testManifest({ version: "1.0.1", images: { "amoled-175c": testImage(10_000) } });

  it.each([
    ["an unknown gadget", () => ({ devices: pairedRegistry(), id: "gad_ffffffffffffffff", fw: "1.0.0", connect: true }), 404, "no_gadget"],
    ["a custom build, even offline", () => ({ devices: pairedRegistry("0.0.0-dev"), id: ID, fw: "0.0.0-dev", connect: false }), 409, "custom_build"],
    ["an offline gadget", () => ({ devices: pairedRegistry(), id: ID, fw: "1.0.0", connect: false }), 409, "offline"],
    ["a gadget already on the latest version", () => ({ devices: pairedRegistry("1.0.1"), id: ID, fw: "1.0.1", connect: true }), 409, "no_update"],
    ["a gadget ahead of the latest version", () => ({ devices: pairedRegistry("1.2.0"), id: ID, fw: "1.2.0", connect: true }), 409, "no_update"],
  ])("refuses %s", (_name, make, status, code) => {
    const { devices, id, fw, connect } = make();
    const hub = fakeHub();
    const { service } = start({ devices, hub, checker: stubChecker(manifest) });
    if (connect) online(hub, fw);
    expect(service.startUpdate(id)).toMatchObject({ ok: false, status, code });
    expect(service.state().updates).toEqual({});
  });

  it("refuses when no release has been seen, or the gadget has no OTA slot big enough", () => {
    const hub = fakeHub();
    const checker = stubChecker(null);
    const { service } = start({ devices: pairedRegistry(), hub, checker });
    online(hub, "1.0.0");
    expect(service.startUpdate(ID)).toMatchObject({ ok: false, code: "no_update" });
    checker.manifest = manifest;
    online(hub, "1.0.0", { ota: null });
    expect(service.startUpdate(ID)).toMatchObject({ ok: false, code: "no_update", error: expect.stringMatching(/over USB/) });
    online(hub, "1.0.0", { ota: 9_999 });
    expect(service.startUpdate(ID)).toMatchObject({ ok: false, code: "no_update" });
  });

  it("refuses a second Update while the first is running", async () => {
    const hub = fakeHub();
    const { service, reach } = start({ devices: pairedRegistry(), hub, checker: stubChecker(manifest, testImage(10_000)) });
    const session = online(hub);
    expect(service.startUpdate(ID)).toEqual({ ok: true });
    expect(service.startUpdate(ID)).toMatchObject({ ok: false, status: 409, code: "busy" });
    await reach("offering");
    expect(service.startUpdate(ID)).toMatchObject({ code: "busy" });
    session.close();
    await reach("failed");
  });
});

describe("an update", () => {
  const image = testImage(70_000, 9);
  const manifest = testManifest({ version: "1.0.1", images: { "amoled-175c": image } });

  it("goes downloading → … → restarting, then installed when the gadget says so", async () => {
    let clock = 1_000;
    const hub = fakeHub();
    const devices = pairedRegistry();
    const { service, reach, phases } = start({ devices, hub, checker: stubChecker(manifest, image), now: () => clock });
    const session = online(hub);
    expect(service.startUpdate(ID)).toEqual({ ok: true });
    expect(service.state().updates[ID]).toMatchObject({ phase: "downloading", version: "1.0.1", at: 1_000 });
    const gadget = await actAsGadget(session);
    expect(Buffer.compare(gadget.image!, image)).toBe(0);
    await reach("restarting");
    expect(phases().slice(0, 4)).toEqual(["downloading", "verifying", "offering", "sending"]);
    expect(phases().slice(-2)).toEqual(["committing", "restarting"]);
    expect(service.state().updates[ID]).toMatchObject({ phase: "restarting", offset: 70_000, size: 70_000 });

    const rebooted = online(hub, "1.0.1");
    expect(service.state().updates[ID]!.phase).toBe("restarting");
    rebooted.emit({ op: "fw.installed", version: "1.0.1" });
    expect(service.state().updates[ID]).toMatchObject({ phase: "installed", version: "1.0.1" });
    clock += INSTALLED_TTL_MS + 1;
    expect(service.state().updates).toEqual({});
  });

  it("says the gadget went back when it returns on its old version", async () => {
    const hub = fakeHub();
    const { service, reach } = start({ devices: pairedRegistry(), hub, checker: stubChecker(manifest, image) });
    const session = online(hub);
    service.startUpdate(ID);
    await actAsGadget(session);
    await reach("restarting");
    online(hub, "1.0.0");
    expect(service.state().updates[ID]).toMatchObject({ phase: "failed", version: "1.0.1", error: expect.stringMatching(/came back on 1\.0\.0/) });
  });

  it("fails when the gadget does not come back at all", async () => {
    const hub = fakeHub();
    const { service, reach } = start({ devices: pairedRegistry(), hub, checker: stubChecker(manifest, image), restartTimeoutMs: 20 });
    const session = online(hub);
    service.startUpdate(ID);
    await actAsGadget(session);
    const failed = await reach("failed");
    expect(failed.error).toMatch(/has not come back/);
    expect(service.state().updates[ID]!.phase).toBe("failed");
  });

  it("reports a failed download and lets the person try again", async () => {
    const hub = fakeHub();
    const checker = stubChecker(manifest, image);
    checker.downloadError = new Error("The release server answered 502.");
    const { service, reach } = start({ devices: pairedRegistry(), hub, checker });
    const session = online(hub);
    service.startUpdate(ID);
    expect((await reach("failed")).error).toBe("Could not get the update. The release server answered 502.");
    checker.downloadError = null;
    expect(service.startUpdate(ID)).toEqual({ ok: true });
    await actAsGadget(session);
    await reach("restarting");
  });

  it("says the update did not stay when the gadget is ready on its old version before its old session closes", async () => {
    const hub = fakeHub();
    const { service, reach, phases } = start({ devices: pairedRegistry(), hub, checker: stubChecker(manifest, image) });
    const old = online(hub);
    service.startUpdate(ID);
    // An ESP32 restart sends no FIN: the hub closes the old session only once the new one replaces it.
    await actAsGadget(old, { onCommit: () => { online(hub, "1.0.0"); old.close(1006); } });
    expect((await reach("failed")).error).toMatch(/came back on 1\.0\.0/);
    await new Promise<void>((resolve) => setImmediate(resolve));
    expect(phases().at(-1)).toBe("failed");
  }, 2_000);

  it("keeps Updated when the restarted gadget confirms before its old session closes", async () => {
    vi.useFakeTimers();
    const hub = fakeHub();
    const { service, phases } = start({ devices: pairedRegistry(), hub, checker: stubChecker(manifest, image), restartTimeoutMs: 20 });
    const old = online(hub);
    service.startUpdate(ID);
    await actAsGadget(old, {
      onCommit: () => {
        online(hub, "1.0.1").emit({ op: "fw.installed", version: "1.0.1" });
        old.close(1006);
      },
    });
    await vi.advanceTimersByTimeAsync(1_000); // far past restartTimeoutMs: any restart watch has fired
    expect(service.state().updates[ID]).toMatchObject({ phase: "installed", version: "1.0.1" });
    expect(phases().slice(-2)).toEqual(["restarting", "installed"]);
  });

  it("says so when the gadget came back on the new version but never confirmed it", async () => {
    const hub = fakeHub();
    const { service, reach } = start({ devices: pairedRegistry(), hub, checker: stubChecker(manifest, image), restartTimeoutMs: 20 });
    const old = online(hub);
    service.startUpdate(ID);
    await actAsGadget(old, { onCommit: () => { online(hub, "1.0.1"); old.close(1006); } });
    expect((await reach("failed")).error).toBe("The gadget came back on 1.0.1 but did not confirm the update.");
  });

  it("forgets a failed update once the gadget comes back on other firmware", async () => {
    const hub = fakeHub();
    const { service, reach } = start({ devices: pairedRegistry(), hub, checker: stubChecker(manifest, image) });
    const session = online(hub);
    service.startUpdate(ID);
    await actAsGadget(session, { refuse: "busy" });
    await reach("failed");
    online(hub, "1.0.0");
    expect(service.state().updates[ID]!.phase).toBe("failed");
    online(hub, "1.0.1"); // updated over USB
    expect(service.state().updates[ID]).toBeUndefined();
  });

  it("reports the gadget's own refusal in words", async () => {
    const hub = fakeHub();
    const { service, reach } = start({ devices: pairedRegistry(), hub, checker: stubChecker(manifest, image) });
    const session = online(hub);
    service.startUpdate(ID);
    await actAsGadget(session, { refuse: "busy" });
    expect((await reach("failed")).error).toBe("The gadget is busy. Try again when it is idle.");
  });

  it("marks any fw.installed it hears, even for an update it did not start, and clamps its version", () => {
    const hub = fakeHub();
    const { service } = start({ devices: pairedRegistry(), hub, checker: stubChecker(null) });
    const session = online(hub, "1.0.1");
    session.emit({ op: "fw.installed", version: "1.0.1" });
    expect(service.state().updates[ID]).toMatchObject({ phase: "installed", version: "1.0.1" });
    session.emit({ op: "fw.installed", version: `1.0.1-${"x".repeat(16_000)}` });
    expect(service.state().updates[ID]!.version).toBe(`1.0.1-${"x".repeat(26)}`);
  });
});

describe("state()", () => {
  it("reports the latest release by board, the check's error, and drops removed gadgets", () => {
    const hub = fakeHub();
    const checker = stubChecker(testManifest({ version: "1.0.1", images: { "lcd-154": testImage(1), "amoled-175c": testImage(1) } }));
    checker.checkedAt = 42;
    checker.error = "The release server answered 503.";
    const devices = pairedRegistry();
    const { service } = start({ devices, hub, checker });
    online(hub, "1.0.1").emit({ op: "fw.installed", version: "1.0.1" });
    expect(service.state()).toEqual({
      latest: { version: "1.0.1", boards: ["amoled-175c", "lcd-154"], checkedAt: 42 },
      checking: false,
      error: "The release server answered 503.",
      updates: { [ID]: expect.objectContaining({ phase: "installed" }) },
    });
    devices.revoke(ID);
    expect(service.state().updates).toEqual({});
  });
});
```

- [ ] **Step 2: Run it to see it fail**

Run: `pnpm exec vitest run companion/test/gadget/firmware.test.ts`
Expected: FAIL. Vitest cannot load `../../src/gadget/firmware.ts`.

- [ ] **Step 3: Write `companion/src/gadget/firmware.ts`**

```ts
// Gadget firmware updates as the desktop sees them (spec §8, contract §3.16):
// when to look for a release, the Update button's checks, the update itself,
// and the per-gadget status the Remote access rows render from /state.
//
// Nothing here runs while no gadget is paired: no manifest fetch at start, on
// the daily timer or when Settings opens.
//
// The restart can arrive in either order. A simulator's exec closes its socket,
// so the old session closes before the new one is ready. An ESP32 restart sends
// no FIN, so the restarted gadget's new session can be ready while the old one
// is still open and waiting after fw.commit; P3a's hub closes the old session
// only when the new one replaces it. So the restart is read from the new
// session, and the old session's late close never overwrites what it reported.
import type { DeviceRegistry } from "../devices.ts";
import type { GadgetHub } from "./hub.ts";
import type { GadgetSessionHandle } from "./session.ts";
import { CHECK_INTERVAL_MS, isCustomBuild, updateFor, type ReleaseChecker, type VerifiedImage } from "./releases.ts";
import { runFirmwareUpdate, type FwUpdateTimeouts } from "./ota.ts";

export type GadgetUpdatePhase =
  | "downloading" | "verifying" | "offering" | "sending" | "committing" | "restarting" | "installed" | "failed";
export interface GadgetUpdateStatus {
  phase: GadgetUpdatePhase;
  version: string;              // the version being installed
  offset?: number;
  size?: number;
  error?: string;               // Latin-1/English message for the row
  at: number;
}
/** Exposed as companionState().gadgetFirmware. */
export interface GadgetFirmwareState {
  latest: { version: string; boards: string[]; checkedAt: number } | null;
  checking: boolean;
  error?: string;
  updates: Record<string, GadgetUpdateStatus>;   // by device id; "installed" entries expire after 10 min
}
export interface GadgetFirmwareService {
  state(): GadgetFirmwareState;
  /** POST /firmware-updates/check: runs check() in the background when ≥ 1 gadget is paired. */
  requestCheck(): void;
  /** POST /devices/:id/firmware-update. Starts in the background. */
  startUpdate(deviceId: string):
    | { ok: true }
    | { ok: false; status: 404 | 409; code: "no_gadget" | "offline" | "custom_build" | "no_update" | "busy"; error: string };
  close(): void;
}

/** How long "Updated to …" stays on the row. */
export const INSTALLED_TTL_MS = 10 * 60_000;
/** Probation is 5 minutes (spec §4.8); after twice that with no word, the update failed. */
export const RESTART_TIMEOUT_MS = 10 * 60_000;

const ACTIVE: ReadonlySet<GadgetUpdatePhase> = new Set<GadgetUpdatePhase>([
  "downloading", "verifying", "offering", "sending", "committing", "restarting",
]);
const NOT_BACK = "The gadget has not come back since the update. Check that it is on and connected to Wi-Fi.";

/** Private bookkeeping for a device's latest update, next to its public status. */
interface UpdateRun {
  /** The firmware the gadget ran when the update started. */
  from: string;
  /** The session the image streamed on. After fw.commit, any other ready session is the restart. */
  streamedOn?: string;
  /** A session came back on the new version; only fw.installed is still missing. */
  back: boolean;
}

/** Also: daily check while ≥ 1 gadget is paired; hub.onSessionReady → listen for fw.installed and
 *  mark the update "installed" (registry firmware comes from the new hello). */
export function createGadgetFirmwareService(options: {
  devices: DeviceRegistry;
  hub: GadgetHub;
  checker: ReleaseChecker;
  log?: (line: string) => void;
  // P4b-private, for tests:
  now?: () => number;
  checkIntervalMs?: number;
  restartTimeoutMs?: number;
  otaTimeouts?: FwUpdateTimeouts;
  onChange?: (deviceId: string, status: GadgetUpdateStatus) => void;
}): GadgetFirmwareService {
  const now = options.now ?? Date.now;
  const log = options.log ?? ((line: string) => console.log(`gadget firmware: ${line}`));
  const updates = new Map<string, GadgetUpdateStatus>();
  const runs = new Map<string, UpdateRun>();
  const restartTimers = new Map<string, ReturnType<typeof setTimeout>>();
  let closed = false;

  const paired = (): boolean => options.devices.gadgets().length > 0;
  const check = (): void => {
    if (!closed && paired()) void options.checker.check();
  };

  const set = (deviceId: string, status: Omit<GadgetUpdateStatus, "at">): void => {
    const next: GadgetUpdateStatus = { ...status, at: now() };
    updates.set(deviceId, next);
    options.onChange?.(deviceId, next);
  };
  /** True while this device's update still runs and no new session has settled it. */
  const unsettled = (deviceId: string): boolean => {
    const phase = updates.get(deviceId)?.phase;
    return !closed && phase !== undefined && ACTIVE.has(phase) && runs.get(deviceId)?.back !== true;
  };
  const stopRestartWatch = (deviceId: string): void => {
    const timer = restartTimers.get(deviceId);
    if (timer) clearTimeout(timer);
    restartTimers.delete(deviceId);
  };
  const fail = (deviceId: string, version: string, error: string): void => {
    stopRestartWatch(deviceId);
    set(deviceId, { phase: "failed", version, error });
    log(`${deviceId}: update to ${version} failed: ${error}`);
  };
  /** Fail with `error` unless fw.installed or a rollback settles the update first. */
  const watchRestart = (deviceId: string, version: string, error: string): void => {
    stopRestartWatch(deviceId);
    const timer = setTimeout(() => {
      restartTimers.delete(deviceId);
      if (updates.get(deviceId)?.phase === "restarting") fail(deviceId, version, error);
    }, options.restartTimeoutMs ?? RESTART_TIMEOUT_MS);
    timer.unref?.();
    restartTimers.set(deviceId, timer);
  };

  const offReady = options.hub.onSessionReady((session: GadgetSessionHandle) => {
    const deviceId = session.deviceId;
    session.on("fw.installed", (message) => {
      // Device-supplied text: clamp it like every other firmware string (registry firmware ≤ 32).
      const version = String(message.version).slice(0, 32);
      stopRestartWatch(deviceId);
      runs.delete(deviceId);
      set(deviceId, { phase: "installed", version });
      log(`${deviceId}: now runs ${version}`);
    });
    const current = updates.get(deviceId);
    const run = runs.get(deviceId);
    if (!current || !run) return;
    const fw = session.hello.fw;
    if (current.phase === "failed") {
      // A failed update stops showing once the gadget runs other firmware (flashed over USB, say).
      if (fw !== run.from) {
        updates.delete(deviceId);
        runs.delete(deviceId);
      }
      return;
    }
    // The restart: any ready session after fw.commit except the one the image streamed on,
    // whether or not that one's close has been seen yet.
    const restarted = current.phase === "restarting"
      || (current.phase === "committing" && run.streamedOn !== session.sessionId);
    if (!restarted) return;
    if (fw !== current.version) {
      fail(deviceId, current.version,
        `The gadget came back on ${fw.slice(0, 32)}, so the update did not stay. It went back to its previous firmware.`);
      return;
    }
    // Back on the new image. fw.installed follows its first ready (spec §4.8); an image that
    // fails probation comes back on the old version within 5 minutes, which the branch above reports.
    run.back = true;
    set(deviceId, { phase: "restarting", version: current.version, offset: current.offset, size: current.size });
    watchRestart(deviceId, current.version, `The gadget came back on ${current.version} but did not confirm the update.`);
  });

  async function run(deviceId: string, board: string, version: string): Promise<void> {
    let image: VerifiedImage;
    try {
      image = await options.checker.download(board);
    } catch (cause) {
      return fail(deviceId, version, `Could not get the update. ${cause instanceof Error ? cause.message : String(cause)}`);
    }
    if (closed) return;
    set(deviceId, { phase: "verifying", version: image.version, size: image.size });
    const session = options.hub.session(deviceId);
    if (!session || session.closed) return fail(deviceId, image.version, "The gadget went offline before the update could start.");
    const max = session.hello.caps.ota?.max;
    if (session.hello.board !== image.board || typeof max !== "number" || image.size > max) {
      return fail(deviceId, image.version, "The update does not fit this gadget.");
    }
    const record = runs.get(deviceId);
    if (record) record.streamedOn = session.sessionId;
    const result = await runFirmwareUpdate(
      session,
      image,
      (phase, offset, size) => {
        // Never overwrite what the restarted gadget's new session reported (back, installed, failed).
        if (unsettled(deviceId)) set(deviceId, { phase, version: image.version, offset, size });
      },
      options.otaTimeouts,
    );
    if (!unsettled(deviceId)) return;
    if (!result.ok) return fail(deviceId, image.version, result.message);
    if (updates.get(deviceId)?.phase === "restarting") watchRestart(deviceId, image.version, NOT_BACK);
  }

  const interval = setInterval(check, options.checkIntervalMs ?? CHECK_INTERVAL_MS);
  interval.unref?.();
  check(); // on companion start, when a gadget is paired

  return {
    state(): GadgetFirmwareState {
      const latest = options.checker.latest();
      const t = now();
      const out: Record<string, GadgetUpdateStatus> = {};
      for (const [deviceId, status] of updates) {
        const expired = status.phase === "installed" && t - status.at > INSTALLED_TTL_MS;
        if (expired || !options.devices.gadget(deviceId)) {
          updates.delete(deviceId);
          runs.delete(deviceId);
          continue;
        }
        out[deviceId] = { ...status };
      }
      return {
        latest: latest.manifest && latest.checkedAt !== null
          ? { version: latest.manifest.version, boards: Object.keys(latest.manifest.boards).sort(), checkedAt: latest.checkedAt }
          : null,
        checking: latest.checking,
        ...(latest.error ? { error: latest.error } : {}),
        updates: out,
      };
    },

    requestCheck(): void {
      // No floor of its own: check() shares one fetch between overlapping calls.
      check();
    },

    startUpdate(deviceId) {
      const record = options.devices.gadget(deviceId);
      if (!record) return { ok: false, status: 404, code: "no_gadget", error: "No paired gadget has that id." };
      const session = options.hub.session(deviceId);
      const fw = session?.hello.fw ?? record.firmware;
      if (isCustomBuild(fw)) {
        return { ok: false, status: 409, code: "custom_build", error: "This gadget runs a custom build. Update it over USB." };
      }
      if (!session || session.closed) {
        return { ok: false, status: 409, code: "offline", error: "The gadget is offline. Turn it on and try again." };
      }
      const current = updates.get(deviceId);
      if (current && ACTIVE.has(current.phase)) {
        return { ok: false, status: 409, code: "busy", error: "An update is already running on this gadget." };
      }
      const manifest = options.checker.latest().manifest;
      const entry = updateFor(manifest, session.hello.board, fw);
      if (!manifest || !entry) {
        return { ok: false, status: 409, code: "no_update", error: "No newer firmware is available for this gadget." };
      }
      const max = session.hello.caps.ota?.max;
      if (typeof max !== "number" || entry.size > max) {
        return { ok: false, status: 409, code: "no_update", error: "This gadget cannot take this update over Wi-Fi. Update it over USB." };
      }
      runs.set(deviceId, { from: fw, back: false });
      set(deviceId, { phase: "downloading", version: manifest.version });
      log(`${deviceId}: updating ${fw} → ${manifest.version}`);
      void run(deviceId, session.hello.board, manifest.version).catch((cause: unknown) => {
        fail(deviceId, manifest.version, cause instanceof Error ? cause.message : String(cause));
      });
      return { ok: true };
    },

    close(): void {
      if (closed) return;
      closed = true;
      clearInterval(interval);
      for (const timer of restartTimers.values()) clearTimeout(timer);
      restartTimers.clear();
      offReady();
    },
  };
}
```

- [ ] **Step 4: Run it to see it pass**

Run: `pnpm exec vitest run companion/test/gadget/firmware.test.ts`
Expected: PASS (22).

- [ ] **Step 5: Commit**

```bash
git add companion/src/gadget/firmware.ts companion/test/gadget/firmware.test.ts
git commit -m "feat(gadget): firmware service: checks, Update refusals, rollback watch" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 7: Control routes and `gadgetFirmware` in `/state`

**Files:**
- Modify: `companion/src/control.ts`. The import follows `import type { DeviceRegistry } from "./devices.ts";` (origin/main `:17`). The option follows `publicNetworks?: () => ReadonlySet<string>;` (`:53`). The state field follows `discovery: options.discovery(),` (`:230`). The routes go before `const revoke = path.match(/^\/devices\/([\w-]+)$/);` (`:357`).
- Test: `companion/test/gadget/firmware-routes.test.ts`

**Interfaces:**
- Consumes: `GadgetFirmwareService`, `GadgetFirmwareState` (Task 6).
- Produces (contract §3.13):
  - `ControlOptions.firmware?: GadgetFirmwareService`. `companionState(options).gadgetFirmware` appears only when `firmware` is set.
  - `POST /devices/:id/firmware-update` answers 202 with the state, or 404/409 with `{error, code}`.
  - `POST /firmware-updates/check` answers 202 with the state.

- [ ] **Step 1: Write the failing test**

`companion/test/gadget/firmware-routes.test.ts`:

```ts
// The control port's firmware routes (spec §6.1, contract §3.13): Electron only, loopback only.
import { type Server } from "node:http";
import { afterEach, describe, expect, it } from "vitest";

import { createControlServer } from "../../src/control.ts";
import { DeviceRegistry } from "../../src/devices.ts";
import type { GadgetFirmwareService, GadgetFirmwareState } from "../../src/gadget/firmware.ts";

const STATE: GadgetFirmwareState = {
  latest: { version: "1.0.1", boards: ["amoled-175c"], checkedAt: 5 },
  checking: false,
  updates: {},
};
let server: Server | null = null;
afterEach(async () => {
  await new Promise<void>((resolve) => (server ? server.close(() => resolve()) : resolve()));
  server = null;
});

async function control(firmware?: GadgetFirmwareService): Promise<number> {
  server = createControlServer({
    devices: new DeviceRegistry(),
    companionPort: 8810,
    discovery: () => ({ advertising: false, name: "Test" }),
    publicNetworks: () => new Set(),
    ...(firmware ? { firmware } : {}),
  });
  return new Promise((resolve) => server!.listen(0, "127.0.0.1", () => resolve((server!.address() as { port: number }).port)));
}

function stubFirmware(answer: ReturnType<GadgetFirmwareService["startUpdate"]>) {
  const calls: string[] = [];
  const firmware: GadgetFirmwareService = {
    state: () => STATE,
    requestCheck: () => void calls.push("check"),
    startUpdate: (id) => {
      calls.push(`update ${id}`);
      return answer;
    },
    close: () => {},
  };
  return { firmware, calls };
}

const body = async (res: Response): Promise<Record<string, unknown>> => (await res.json()) as Record<string, unknown>;
const post = (port: number, path: string, headers: Record<string, string> = {}) =>
  fetch(`http://127.0.0.1:${port}${path}`, { method: "POST", body: "{}", headers: { "content-type": "application/json", ...headers } });

describe("firmware control routes", () => {
  it("starts an update and answers 202 with the state", async () => {
    const { firmware, calls } = stubFirmware({ ok: true });
    const port = await control(firmware);
    const res = await post(port, "/devices/gad_3f9a0c2b7e41d856/firmware-update");
    expect(res.status).toBe(202);
    expect((await body(res)).gadgetFirmware).toEqual(STATE);
    expect(calls).toEqual(["update gad_3f9a0c2b7e41d856"]);
  });

  it("passes a refusal through as {error, code} with its status", async () => {
    for (const refusal of [
      { ok: false, status: 404, code: "no_gadget", error: "No paired gadget has that id." },
      { ok: false, status: 409, code: "offline", error: "The gadget is offline. Turn it on and try again." },
    ] as const) {
      const { firmware } = stubFirmware(refusal);
      const port = await control(firmware);
      const res = await post(port, "/devices/gad_3f9a0c2b7e41d856/firmware-update");
      expect(res.status).toBe(refusal.status);
      expect(await body(res)).toEqual({ error: refusal.error, code: refusal.code });
      await new Promise<void>((resolve) => server!.close(() => resolve()));
      server = null;
    }
  });

  it("asks for a check and answers 202 at once", async () => {
    const { firmware, calls } = stubFirmware({ ok: true });
    const port = await control(firmware);
    const res = await post(port, "/firmware-updates/check");
    expect(res.status).toBe(202);
    expect(calls).toEqual(["check"]);
  });

  it("puts gadgetFirmware in /state", async () => {
    const port = await control(stubFirmware({ ok: true }).firmware);
    expect((await body(await fetch(`http://127.0.0.1:${port}/state`))).gadgetFirmware).toEqual(STATE);
  });

  it("keeps the control port's loopback rules", async () => {
    const { firmware, calls } = stubFirmware({ ok: true });
    const port = await control(firmware);
    expect((await post(port, "/devices/gad_3f9a0c2b7e41d856/firmware-update", { origin: "https://evil.example" })).status).toBe(403);
    expect((await fetch(`http://127.0.0.1:${port}/devices/gad_3f9a0c2b7e41d856/firmware-update`)).status).toBe(404);
    expect((await post(port, "/devices/../firmware-update")).status).toBe(404);
    expect(calls).toEqual([]);
  });

  it("has neither route, nor gadgetFirmware, without a firmware service", async () => {
    const port = await control();
    expect((await post(port, "/devices/gad_3f9a0c2b7e41d856/firmware-update")).status).toBe(404);
    expect((await post(port, "/firmware-updates/check")).status).toBe(404);
    expect("gadgetFirmware" in (await body(await fetch(`http://127.0.0.1:${port}/state`)))).toBe(false);
  });
});
```

- [ ] **Step 2: Run it to see it fail**

Run: `pnpm exec vitest run companion/test/gadget/firmware-routes.test.ts`
Expected: FAIL (4 of 6). The three route tests get the 404 `no route` answer instead of 202 or `{error, code}`, and `gadgetFirmware` is `undefined` in `/state`. The two tests about the loopback rules and a missing service already pass. They pin behaviour the edit must keep.

- [ ] **Step 3: Edit `companion/src/control.ts`**

Find each anchor with `grep -n` first (P3a has added lines around them).

1. After `import type { DeviceRegistry } from "./devices.ts";`, add:

```ts
import type { GadgetFirmwareService } from "./gadget/firmware.ts";
```

2. In `export interface ControlOptions`, after the `publicNetworks?: () => ReadonlySet<string>;` member (and after any members P3a or P4a added below it, before the interface's closing `}`), add:

```ts
  /** Gadget firmware updates (spec §8): the Update button, the check when
   * Settings → Remote access opens, and `gadgetFirmware` in the state. */
  firmware?: GadgetFirmwareService;
```

3. In `companionState()`'s returned object, after `    discovery: options.discovery(),`, add:

```ts
    ...(options.firmware ? { gadgetFirmware: options.firmware.state() } : {}),
```

4. In `createControlServer`, immediately before the line `    const revoke = path.match(/^\/devices\/([\w-]+)$/);`, add:

```ts
    // Gadget firmware (spec §8). Electron only: the Update button and the
    // check when Settings → Remote access opens. Both start work in the
    // background and answer at once; /state carries the progress. The body
    // is `{}` and is not read.
    const firmware = options.firmware;
    if (method === "POST" && path === "/firmware-updates/check" && firmware) {
      req.resume();
      firmware.requestCheck();
      return json(res, 202, companionState(options));
    }
    const firmwareUpdate = path.match(/^\/devices\/([\w-]+)\/firmware-update$/);
    if (firmwareUpdate && method === "POST" && firmware) {
      req.resume();
      const started = firmware.startUpdate(firmwareUpdate[1]);
      if (!started.ok) return json(res, started.status, { error: started.error, code: started.code });
      return json(res, 202, companionState(options));
    }
```

- [ ] **Step 4: Run the new and the existing control tests**

Run: `pnpm exec vitest run companion/test/gadget/firmware-routes.test.ts companion/test/control.test.ts`
Expected: PASS: 6 new tests, plus every existing `control.test.ts` case (20 on origin/main, more after P3a and P4a).

- [ ] **Step 5: Commit**

```bash
git add companion/src/control.ts companion/test/gadget/firmware-routes.test.ts
git commit -m "feat(gadget): control routes to start an update and to check for firmware" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 8: Companion wiring, and the update through the real hub

**Files:**
- Modify: `companion/src/index.ts`, at the contract §3.12 points. The imports follow P3a's `import { createGadgetHub } from "./gadget/hub.ts";`. The service follows P3a's `const gadgetHub = createGadgetHub({ … });` statement, which sits after `const connectedDevices = createConnectedDeviceTracker();` (origin/main `:155`). `firmware,` goes after `gadgetHub,` inside `createControlServer({` (`:179`). `firmware.close();` goes before P3a's `await gadgetHub.close();` in `shutdown()` (`:309`).
- Test: `companion/test/gadget/ota.hub.test.ts`, `companion/test/gadget/firmware-wiring.test.ts`

**Interfaces:**
- Consumes:
  - `createGadgetHub(GadgetHubOptions)`, `connectTestGadget(TestGadgetOptions)` and `startFakeHarness()` (P3a, contract §3.11 and §3.19).
  - `createConnectedDeviceTracker` (`companion/src/connected-devices.ts`).
  - Tasks 2–7.
- Produces: the companion builds `createGadgetFirmwareService({devices, hub: gadgetHub, checker: createReleaseChecker({keys: trustedReleaseKeys()})})`, passes it to the control server and closes it on shutdown (contract §3.12, P4b column).

- [ ] **Step 1: Write the hub test**

`companion/test/gadget/ota.hub.test.ts`:

```ts
// A firmware update through P3a's real hub and WebSocket (spec §4.8, §8), with a TypeScript
// gadget on the other end playing the firmware's part.
import { createECDH } from "node:crypto";
import { rmSync } from "node:fs";
import { createServer, type Server } from "node:http";
import type { AddressInfo } from "node:net";
import { afterEach, beforeEach, expect, it } from "vitest";

import { createConnectedDeviceTracker } from "../../src/connected-devices.ts";
import { DeviceRegistry } from "../../src/devices.ts";
import {
  createGadgetFirmwareService,
  type GadgetFirmwareService,
  type GadgetUpdatePhase,
  type GadgetUpdateStatus,
} from "../../src/gadget/firmware.ts";
import { createGadgetHub, type GadgetHub } from "../../src/gadget/hub.ts";
import { FW_FAIL_MESSAGES } from "../../src/gadget/ota.ts";
import { BinaryKind, firmwareText, verifyP256, type GadgetCaps } from "../../src/gadget/protocol.ts";
import { TEST_KEY_T1 } from "../../src/gadget/release-keys.ts";
import { createReleaseChecker } from "../../src/gadget/releases.ts";
import { DATA_DIR } from "../../src/state.ts";
import { startFakeHarness, type FakeHarness } from "./helpers/fake-harness.ts";
import { connectTestGadget, type TestGadget } from "./helpers/gadget-client.ts";
import { DEV_BASE, bytesResponse, fakeFetch, imageUrl, jsonResponse, testImage, testManifest } from "./helpers/test-release.ts";

const CAPS: GadgetCaps = { screen: { w: 466, h: 466, round: true, text: "latin1" }, ota: { max: 6_291_456 } };
const IMAGE = testImage(150_000, 11);

let harness: FakeHarness;
let hub: GadgetHub;
let server: Server;
let port = 0;
let devices: DeviceRegistry;
let firmware: GadgetFirmwareService;
let changes: Array<[string, GadgetUpdateStatus]> = [];
let wake: Array<() => void> = [];

/** Resolves when the gadget's update reaches the phase (already reached counts). */
async function reach(deviceId: string, phase: GadgetUpdatePhase): Promise<GadgetUpdateStatus> {
  for (;;) {
    const hit = changes.find(([id, status]) => id === deviceId && status.phase === phase);
    if (hit) return hit[1];
    await new Promise<void>((resolve) => void wake.push(resolve));
  }
}

beforeEach(async () => {
  rmSync(DATA_DIR, { recursive: true, force: true });
  changes = [];
  wake = [];
  harness = await startFakeHarness();
  devices = new DeviceRegistry();
  hub = createGadgetHub({
    devices,
    harnessPort: harness.port,
    hostId: "000102030405060708090a0b0c0d0e0f",
    hostName: () => "Test Mac",
    connected: createConnectedDeviceTracker().open,
    log: () => {},
  });
  server = createServer((_req, res) => void res.writeHead(404).end());
  server.on("upgrade", (req, socket, head) => hub.handleUpgrade(req, socket, head));
  port = await new Promise<number>((resolve) => server.listen(0, "127.0.0.1", () => resolve((server.address() as AddressInfo).port)));
  const manifest = testManifest({ version: "1.0.1", images: { "amoled-175c": IMAGE } });
  const net = fakeFetch({
    [`${DEV_BASE}manifest.json`]: () => jsonResponse(manifest),
    [imageUrl(DEV_BASE, "amoled-175c", "1.0.1")]: () => bytesResponse(IMAGE),
  });
  const checker = createReleaseChecker({ keys: [TEST_KEY_T1], fetch: net.fetch, manifestUrl: `${DEV_BASE}manifest.json`, log: () => {} });
  firmware = createGadgetFirmwareService({
    devices,
    hub,
    checker,
    log: () => {},
    onChange: (id, status) => {
      changes.push([id, status]);
      for (const resolve of wake.splice(0)) resolve();
    },
  });
  await checker.check();
});

afterEach(async () => {
  firmware.close();
  await hub.close();
  await new Promise<void>((resolve) => server.close(() => resolve()));
  await harness.close();
});

/** A fresh gadget key (64 hex) and the gadget enrolled with it on firmware 1.0.0. */
async function pairGadget(): Promise<{ gadget: TestGadget; key: string }> {
  const ecdh = createECDH("prime256v1");
  ecdh.generateKeys();
  const key = ecdh.getPrivateKey("hex").padStart(64, "0");
  const { code } = devices.openPairing();
  const gadget = await connectTestGadget({ port, privateKeyHex: key, enroll: code, board: "amoled-175c", fw: "1.0.0", caps: CAPS });
  expect(gadget.error).toBeNull();
  return { gadget, key };
}

/** The firmware's part of §4.8 over the real socket: check the offer, take the chunks in order,
 *  acknowledge every 16 KiB crossing and the end, then wait for fw.commit. (Task 5's tests pin
 *  the 64 KiB window; this gadget acknowledges too often to see it.) */
async function receive(gadget: TestGadget, stopAfter = Number.POSITIVE_INFINITY): Promise<{ image: Buffer }> {
  const offer = await gadget.next("fw.offer");
  expect(offer).toMatchObject({ board: "amoled-175c", version: "1.0.1", size: IMAGE.length, key_id: "t1" });
  const signed = firmwareText("amoled-175c", "1.0.1", offer.size, offer.sha256);
  expect(verifyP256(Buffer.from(TEST_KEY_T1.pubkey, "base64"), signed, Buffer.from(offer.sig, "base64"))).toBe(true);
  gadget.send({ op: "fw.ready", stream: offer.stream });
  const image = Buffer.alloc(offer.size);
  let written = 0;
  let acked = 0;
  while (written < offer.size && written < stopAfter) {
    const frame = await gadget.nextBinary(BinaryKind.firmware);
    const payload = Buffer.from(frame.payload); // u32 LE offset, then the bytes (spec §4.1)
    expect(frame.stream).toBe(offer.stream);
    expect(payload.readUInt32LE(0)).toBe(written);
    payload.subarray(4).copy(image, written);
    written += payload.length - 4;
    if (Math.floor(written / 16_384) > Math.floor(acked / 16_384) || written === offer.size) {
      gadget.send({ op: "fw.progress", stream: offer.stream, offset: written });
      acked = written;
    }
  }
  if (written === offer.size) await gadget.next("fw.commit");
  return { image };
}

it("updates a gadget through the hub and marks it installed when it comes back on the new version", async () => {
  const { gadget, key } = await pairGadget();
  expect(firmware.startUpdate(gadget.id)).toEqual({ ok: true });
  const received = await receive(gadget);
  expect(Buffer.compare(received.image, IMAGE)).toBe(0);
  gadget.close(); // the firmware restarts into the new slot
  await reach(gadget.id, "restarting");
  const rebooted = await connectTestGadget({ port, privateKeyHex: key, board: "amoled-175c", fw: "1.0.1", caps: CAPS });
  expect(rebooted.ready).not.toBeNull();
  rebooted.send({ op: "fw.installed", version: "1.0.1" });
  expect(await reach(gadget.id, "installed")).toMatchObject({ version: "1.0.1" });
  expect(devices.gadget(gadget.id)?.firmware).toBe("1.0.1");
  expect(firmware.state().updates[gadget.id]?.phase).toBe("installed");
  rebooted.close();
});

it("says the update did not stay when the gadget comes back on its old version", async () => {
  const { gadget, key } = await pairGadget();
  firmware.startUpdate(gadget.id);
  await receive(gadget);
  gadget.close();
  await reach(gadget.id, "restarting");
  const rolledBack = await connectTestGadget({ port, privateKeyHex: key, board: "amoled-175c", fw: "1.0.0", caps: CAPS });
  expect((await reach(gadget.id, "failed")).error).toMatch(/came back on 1\.0\.0/);
  rolledBack.close();
});

it("keeps Updated when the restarted gadget connects before its old connection closes", async () => {
  // An ESP32 restart sends no FIN, so the old connection is still open when the restarted gadget
  // proves itself, and P3a's hub replaces it then. Either side may reach the firmware service first.
  const { gadget, key } = await pairGadget();
  const old = hub.session(gadget.id)!;
  const oldClosed = new Promise<void>((resolve) => void old.onClose(() => resolve()));
  firmware.startUpdate(gadget.id);
  await receive(gadget); // and never close it
  const rebooted = await connectTestGadget({ port, privateKeyHex: key, board: "amoled-175c", fw: "1.0.1", caps: CAPS });
  expect(rebooted.ready).not.toBeNull();
  expect(await gadget.next("error")).toMatchObject({ code: "replaced" });
  rebooted.send({ op: "fw.installed", version: "1.0.1" });
  expect(await reach(gadget.id, "installed")).toMatchObject({ version: "1.0.1" });
  await oldClosed;
  await new Promise<void>((resolve) => setImmediate(resolve)); // the old session's update run finishes
  expect(firmware.state().updates[gadget.id]?.phase).toBe("installed");
  expect(changes.filter(([id]) => id === gadget.id).at(-1)?.[1].phase).toBe("installed");
  rebooted.close();
});

it("shows the gadget's own refusal", async () => {
  const { gadget } = await pairGadget();
  firmware.startUpdate(gadget.id);
  const offer = await gadget.next("fw.offer");
  gadget.send({ op: "fw.fail", stream: offer.stream, code: "unknown_key" });
  expect((await reach(gadget.id, "failed")).error).toBe(FW_FAIL_MESSAGES.unknown_key);
  gadget.close();
});

it("fails cleanly when the gadget drops mid-transfer, and the next Update starts over", async () => {
  const { gadget, key } = await pairGadget();
  firmware.startUpdate(gadget.id);
  await receive(gadget, 40_000);
  gadget.close();
  expect((await reach(gadget.id, "failed")).error).toBe(FW_FAIL_MESSAGES.closed);
  const again = await connectTestGadget({ port, privateKeyHex: key, board: "amoled-175c", fw: "1.0.0", caps: CAPS });
  changes = [];
  expect(firmware.startUpdate(again.id)).toEqual({ ok: true });
  const received = await receive(again);
  expect(Buffer.compare(received.image, IMAGE)).toBe(0);
  again.close();
  await reach(again.id, "restarting");
});
```

Note: Node's built-in WebSocket client accepts only close code 1000 or 3000–4999, so the test gadget closes with the default (checked on Node 24.14.1). If P3a's `nextBinary` payload turns out to include the 2-byte frame header, the `readUInt32LE(0)` assertion fails on the first chunk. In that case, decode with P3a's `decodeBinary` from `protocol.ts` before reading the offset.

- [ ] **Step 2: Run it**

Run: `pnpm exec vitest run companion/test/gadget/ota.hub.test.ts`
Expected: PASS (5). This test exercises Tasks 3–6 against P3a's real hub, so it can pass before the wiring below. If it fails, the cause is in the interaction with P3a's hub. Read the failure and the frames before changing either side.

- [ ] **Step 3: Write the failing wiring test**

`companion/test/gadget/firmware-wiring.test.ts`:

```ts
// companion/src/index.ts wires the firmware service (contract §3.12): with a gadget paired the
// sidecar checks for firmware as it starts, /state carries gadgetFirmware, the check route
// answers, and SIGTERM still exits; with none paired it never asks the release server.
import { spawn, type ChildProcess } from "node:child_process";
import { rmSync } from "node:fs";
import { createServer, type Server } from "node:http";
import { createServer as createNetServer, type AddressInfo } from "node:net";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";
import { afterEach, expect, it, vi } from "vitest";

import { DeviceRegistry } from "../../src/devices.ts";
import { DATA_DIR } from "../../src/state.ts";
import { testImage, testManifest } from "./helpers/test-release.ts";

const ENTRY = join(dirname(fileURLToPath(import.meta.url)), "..", "..", "src", "index.ts");
// The RFC 6979 A.2.5 key's pubkey and id (contract §1.7).
const ID = "gad_b18b86ce1389e46d";
const PUBKEY = "BGD+1LolWp0xyWHrdMY1bWjASbiSO2H6bOZpYi5g8p+2eQP+EAi4vJmkGunpVii8ZPLxsgwtfp9Rd6PClNRGIpk=";

let child: ChildProcess | null = null;
let releases: Server | null = null;
afterEach(async () => {
  child?.kill("SIGKILL");
  child = null;
  await new Promise<void>((resolve) => (releases ? releases.close(() => resolve()) : resolve()));
  releases = null;
});

const freePort = (): Promise<number> =>
  new Promise((resolve) => {
    const probe = createNetServer();
    probe.listen(0, "127.0.0.1", () => {
      const { port } = probe.address() as AddressInfo;
      probe.close(() => resolve(port));
    });
  });

/** Three distinct ports, none of them the harness port + 1 (its webhook receiver). */
async function ports(): Promise<{ harness: number; companion: number; control: number }> {
  for (;;) {
    const [harness, companion, control] = [await freePort(), await freePort(), await freePort()];
    const all = new Set([harness, harness + 1, companion, control]);
    if (all.size === 4) return { harness, companion, control };
  }
}

/** A release server on 127.0.0.1 serving a t1-signed 1.0.1 manifest and counting requests. */
async function releaseServer(): Promise<{ url: string; hits: () => number; firstHit: Promise<void> }> {
  let hits = 0;
  let markHit = (): void => {};
  const firstHit = new Promise<void>((resolve) => (markHit = resolve));
  let base = "";
  releases = createServer((req, res) => {
    if (req.url !== "/manifest.json") return void res.writeHead(404).end();
    hits += 1;
    markHit();
    const manifest = testManifest({ version: "1.0.1", images: { "amoled-175c": testImage(1000) }, base });
    res.writeHead(200, { "content-type": "application/json" }).end(JSON.stringify(manifest));
  });
  const port = await new Promise<number>((resolve) => releases!.listen(0, "127.0.0.1", () => resolve((releases!.address() as AddressInfo).port)));
  base = `http://127.0.0.1:${port}/`;
  return { url: `${base}manifest.json`, hits: () => hits, firstHit };
}

async function startSidecar(manifestUrl: string): Promise<number> {
  const { harness, companion, control } = await ports();
  child = spawn(process.execPath, [ENTRY], {
    env: {
      ...(process.env.PATH ? { PATH: process.env.PATH } : {}),
      ...(process.env.SystemRoot ? { SystemRoot: process.env.SystemRoot } : {}),
      ...(process.env.HOME ? { HOME: process.env.HOME } : {}),
      ...(process.env.USERPROFILE ? { USERPROFILE: process.env.USERPROFILE } : {}),
      OMB_COMPANION_DIR: process.env.OMB_COMPANION_DIR ?? DATA_DIR,
      OMB_PORT: String(harness),
      OMB_COMPANION_PORT: String(companion),
      OMB_CONTROL_PORT: String(control),
      OMB_COMPANION_NAME: "Firmware wiring test",
      OMB_GADGET_MANIFEST_URL: manifestUrl,
      OMB_GADGET_TRUST_TEST_KEY: "1",
    },
    stdio: ["ignore", "ignore", "pipe"],
  });
  child.stderr?.resume();
  return control;
}

function pairOne(): void {
  rmSync(DATA_DIR, { recursive: true, force: true });
  const devices = new DeviceRegistry();
  const { code } = devices.openPairing();
  const enrolled = devices.enrollGadget(code, { id: ID, publicKey: PUBKEY, name: "Desk Maus", board: "amoled-175c", firmware: "1.0.0", botId: null });
  expect("device" in enrolled).toBe(true);
}

const state = async (control: number) =>
  (await (await fetch(`http://127.0.0.1:${control}/state`)).json()) as { gadgetFirmware?: { latest: { version: string } | null } };
const postCheck = (control: number) =>
  fetch(`http://127.0.0.1:${control}/firmware-updates/check`, { method: "POST", body: "{}", headers: { "content-type": "application/json" } });

it("checks at start with a gadget paired, reports it in /state, and answers the check route", async () => {
  pairOne();
  const server = await releaseServer();
  const control = await startSidecar(server.url);
  await server.firstHit;
  await vi.waitFor(async () => expect((await state(control)).gadgetFirmware?.latest?.version).toBe("1.0.1"), { timeout: 15_000, interval: 100 });
  expect((await postCheck(control)).status).toBe(202);
}, 30_000);

it.skipIf(process.platform === "win32")("still exits on SIGTERM with the firmware service running", async () => {
  pairOne();
  const server = await releaseServer();
  const control = await startSidecar(server.url);
  await vi.waitFor(async () => expect((await state(control)).gadgetFirmware).toBeDefined(), { timeout: 15_000, interval: 100 });
  const exited = new Promise<number | null>((resolve) => child!.once("exit", (code) => resolve(code)));
  child!.kill("SIGTERM");
  expect(await exited).toBe(0);
  child = null;
}, 30_000);

it("never asks the release server while no gadget is paired", async () => {
  rmSync(DATA_DIR, { recursive: true, force: true });
  const server = await releaseServer();
  const control = await startSidecar(server.url);
  await vi.waitFor(async () => expect((await state(control)).gadgetFirmware).toBeDefined(), { timeout: 15_000, interval: 100 });
  expect((await postCheck(control)).status).toBe(202);
  expect((await state(control)).gadgetFirmware?.latest).toBeNull();
  expect(server.hits()).toBe(0);
}, 30_000);
```

These start the real sidecar. Like any companion start, it briefly advertises `_openmausbot._tcp` on the local network and may look for the Tailscale CLI. The child is killed after each test.

- [ ] **Step 4: Run it to see it fail**

Run: `pnpm exec vitest run companion/test/gadget/firmware-wiring.test.ts`
Expected: FAIL. The first test times out on `server.firstHit` (nothing fetches the manifest). The other two time out in `vi.waitFor` (`gadgetFirmware` is `undefined`).

- [ ] **Step 5: Edit `companion/src/index.ts`**

1. After P3a's `import { createGadgetHub } from "./gadget/hub.ts";`, add:

```ts
import { createGadgetFirmwareService } from "./gadget/firmware.ts";
import { trustedReleaseKeys } from "./gadget/release-keys.ts";
import { createReleaseChecker } from "./gadget/releases.ts";
```

2. Immediately after P3a's statement `const gadgetHub = createGadgetHub({ … });` (find it with `grep -n "const gadgetHub = createGadgetHub" companion/src/index.ts`; it ends at the first following line that is exactly `});`), add:

```ts

// Official firmware for paired gadgets (spec §8): checks at start and daily
// while one is paired, and runs the Update button. The test key is trusted
// only with OMB_GADGET_TRUST_TEST_KEY=1 (development).
const firmware = createGadgetFirmwareService({
  devices,
  hub: gadgetHub,
  checker: createReleaseChecker({ keys: trustedReleaseKeys() }),
});
```

3. Inside `const control = createControlServer({`, on the line after `  gadgetHub,` (P3a), add:

```ts
  firmware,
```

4. In `shutdown()`, immediately before P3a's line `  await gadgetHub.close();`, add:

```ts
  firmware.close();
```

- [ ] **Step 6: Run the wiring test, then every companion test**

Run: `pnpm exec vitest run companion/test/gadget/firmware-wiring.test.ts`
Expected: PASS (3; 2 on Windows, where SIGTERM cannot be caught).

Run: `pnpm exec vitest run companion/test`
Expected: PASS, every file.

- [ ] **Step 7: Check that the packaged companion still builds dependency-free**

Run: `pnpm build:companion && ls dist-companion/gadget/ | grep -E "^(release-keys|releases|ota|firmware)\.js$"`
Expected: `tsc` exits 0 and prints the four file names.

- [ ] **Step 8: Commit**

```bash
git add companion/src/index.ts companion/test/gadget/ota.hub.test.ts companion/test/gadget/firmware-wiring.test.ts
git commit -m "feat(gadget): wire firmware updates into the companion" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 9: Electron bridge

**Files:**
- Modify: `electron/companion.mjs`. Insert immediately before `/** Enable or remove interactive cloud-desktop access for one paired phone. */` (origin/main `:467`), which comes after `companionRevoke` (`:459-465`) and any helpers P3a added there.
- Modify: `electron/main.mjs`. The import list is `:509-526`: add after `  companionBrowserControlAccess,` (`:516`). The handlers go immediately before `function publicDesktopRemoteState() {` (`:2704`), after the `companion:revoke` handler (`:2700-2702`) and P3a's three handlers.
- Modify: `electron/preload.cjs` (`companion: {` object, `:102-112`), `electron/preload.node-test.mjs` (before `test("onOpenAppSettings subscribes…"`, `:69`)
- Test: create `electron/companion-firmware.node-test.mjs`

**Interfaces:**
- Consumes: the control routes of Task 7. `control(method, urlPath, body)` (`companion.mjs:117-129`, throws on any status but 2xx and 404). `companionState()`, `desktopCompanionState()` and `localOnly` (`electron/local-origin.cjs`).
- Produces (contract §3.17):
  - `companionFirmwareUpdate(deviceId)` resolves to the state, or `{...state, firmwareRefusal: {deviceId, code, error}}`. `companionFirmwareCheck()` resolves to the state.
  - IPC `companion:firmware-update` and `companion:firmware-check`.
  - Preload `window.ogb.companion.firmwareUpdate(deviceId)` and `.firmwareCheck()`.

- [ ] **Step 1: Write the failing tests**

`electron/companion-firmware.node-test.mjs`:

```js
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import test from "node:test";
import vm from "node:vm";
import localOrigin from "./local-origin.cjs";

/** Values made inside a vm context have that context's prototypes; compare their data. */
const plain = (value) => JSON.parse(JSON.stringify(value));

// Gadget firmware IPC (spec §8): local-only, forwards the device id, and keeps
// the companion's refusal on the state so the Update cell can say why.
test("firmware IPC is local-only and returns the state, with any refusal", async () => {
  const source = readFileSync(new URL("./main.mjs", import.meta.url), "utf8");
  const start = source.indexOf('ipcMain.handle("companion:firmware-update"');
  const end = source.indexOf("function publicDesktopRemoteState()", start);
  assert.ok(start >= 0 && end > start);
  const handlers = new Map();
  const calls = [];
  let refusal = null;
  localOrigin.setLocalOrigin("http://127.0.0.1:49210");
  vm.runInNewContext(source.slice(start, end), {
    ipcMain: { handle: (channel, handler) => handlers.set(channel, handler) },
    localOnly: localOrigin.localOnly,
    companionFirmwareUpdate: async (...args) => { calls.push(["update", ...args]); return refusal ? { firmwareRefusal: refusal } : { enabled: true }; },
    companionFirmwareCheck: async () => { calls.push(["check"]); return { enabled: true }; },
    desktopCompanionState: async () => ({ enabled: true, devices: [] }),
  });
  assert.deepEqual(Array.from(handlers.keys()), ["companion:firmware-update", "companion:firmware-check"]);
  const local = { senderFrame: { url: "http://127.0.0.1:49210/" } };
  assert.deepEqual(await handlers.get("companion:firmware-update")(local, "gad_3f9a0c2b7e41d856"), { enabled: true, devices: [] });
  refusal = { deviceId: "gad_3f9a0c2b7e41d856", code: "offline", error: "The gadget is offline. Turn it on and try again." };
  assert.deepEqual(plain(await handlers.get("companion:firmware-update")(local, "gad_3f9a0c2b7e41d856")), { enabled: true, devices: [], firmwareRefusal: refusal });
  assert.deepEqual(await handlers.get("companion:firmware-check")(local), { enabled: true, devices: [] });
  assert.deepEqual(calls, [["update", "gad_3f9a0c2b7e41d856"], ["update", "gad_3f9a0c2b7e41d856"], ["check"]]);
  const remote = { senderFrame: { url: "https://remote.invalid/" } };
  assert.throws(() => handlers.get("companion:firmware-update")(remote, "gad_3f9a0c2b7e41d856"), /only available/);
  assert.throws(() => handlers.get("companion:firmware-check")(remote), /only available/);
  assert.equal(calls.length, 3);
});

/** The two helpers from companion.mjs, run against a stand-in control port. */
function helpers(answer) {
  const source = readFileSync(new URL("./companion.mjs", import.meta.url), "utf8");
  const start = source.indexOf("const FIRMWARE_REFUSALS");
  const end = source.indexOf("/** Enable or remove interactive cloud-desktop access", start);
  assert.ok(start >= 0 && end > start);
  const requests = [];
  const controls = [];
  const context = vm.createContext({
    proc: {},
    CONTROL_PORT: 8811,
    AbortSignal,
    companionState: async () => ({ enabled: true, devices: [] }),
    control: async (...args) => { controls.push(args); return {}; },
    fetch: async (url, init) => { requests.push([url, init.method, init.body]); return answer(); },
  });
  vm.runInContext(source.slice(start, end).replace(/export async function/g, "async function"), context);
  return { context, requests, controls };
}

test("an update that starts returns the plain state", async () => {
  const { context, requests } = helpers(() => new Response("{}", { status: 202 }));
  assert.deepEqual(await context.companionFirmwareUpdate("gad_3f9a0c2b7e41d856"), { enabled: true, devices: [] });
  assert.deepEqual(requests, [["http://127.0.0.1:8811/devices/gad_3f9a0c2b7e41d856/firmware-update", "POST", "{}"]]);
});

test("a refused update carries the sidecar's code and sentence; unknown codes are clamped", async () => {
  let body = { error: "The gadget is offline. Turn it on and try again.", code: "offline" };
  const { context } = helpers(() => new Response(JSON.stringify(body), { status: 409 }));
  assert.deepEqual(plain(await context.companionFirmwareUpdate("gad_3f9a0c2b7e41d856")), {
    enabled: true, devices: [],
    firmwareRefusal: { deviceId: "gad_3f9a0c2b7e41d856", code: "offline", error: "The gadget is offline. Turn it on and try again." },
  });
  body = { error: "x".repeat(500), code: "<script>" };
  const refused = await context.companionFirmwareUpdate("gad_3f9a0c2b7e41d856");
  assert.equal(refused.firmwareRefusal.code, "no_update");
  assert.equal(refused.firmwareRefusal.error.length, 200);
});

test("bad ids never reach the control port, other statuses throw, and the check is fire-and-forget", async () => {
  const { context, requests, controls } = helpers(() => new Response("", { status: 500 }));
  for (const id of ["../other", "gad?x=1", "a".repeat(65), "", undefined]) await context.companionFirmwareUpdate(id);
  assert.deepEqual(requests, []);
  await assert.rejects(context.companionFirmwareUpdate("gad_3f9a0c2b7e41d856"), /companion control 500/);
  assert.deepEqual(await context.companionFirmwareCheck(), { enabled: true, devices: [] });
  assert.deepEqual(plain(controls), [["POST", "/firmware-updates/check", {}]]);
});
```

In `electron/preload.node-test.mjs`, immediately before the line `test("onOpenAppSettings subscribes to the exact app:open-settings channel, forwards every emit, and unsubscribes cleanly", () => {`, add:

```js
test("gadget firmware update and check forward to their own channels", async () => {
  const before = invocations.length;
  await exposed.api.companion.firmwareUpdate("gad_3f9a0c2b7e41d856");
  await exposed.api.companion.firmwareCheck();
  assert.deepEqual(invocations.slice(before), [
    ["companion:firmware-update", "gad_3f9a0c2b7e41d856"],
    ["companion:firmware-check"],
  ]);
});

```

- [ ] **Step 2: Run them to see them fail**

Run: `node --test electron/companion-firmware.node-test.mjs electron/preload.node-test.mjs`
Expected: FAIL. The `assert.ok(start >= 0 && end > start)` lines fail, and the preload case fails with `TypeError: exposed.api.companion.firmwareUpdate is not a function`.

- [ ] **Step 3: Add the helpers to `electron/companion.mjs`**

Immediately before `/** Enable or remove interactive cloud-desktop access for one paired phone. */`, add:

```js
/** Start a firmware update on one gadget (spec §8). The sidecar answers 202
 * when the update started, or 404/409 with {error, code} when it did not.
 * That body is the reason the Update cell shows, so this reads it itself
 * instead of going through control(), which throws on a 409 unread. */
const FIRMWARE_REFUSALS = new Set(["no_gadget", "offline", "custom_build", "no_update", "busy"]);
export async function companionFirmwareUpdate(deviceId) {
  if (!proc) return companionState();
  if (!/^[\w-]{1,64}$/.test(String(deviceId ?? ""))) return companionState();
  const res = await fetch(`http://127.0.0.1:${CONTROL_PORT}/devices/${deviceId}/firmware-update`, {
    method: "POST",
    body: "{}",
    headers: { "content-type": "application/json" },
    signal: AbortSignal.timeout(4_000),
  });
  if (res.status === 202) return companionState();
  if (res.status === 404 || res.status === 409) {
    const body = await res.json().catch(() => ({}));
    return {
      ...(await companionState()),
      firmwareRefusal: {
        deviceId,
        code: FIRMWARE_REFUSALS.has(body?.code) ? body.code : "no_update",
        error: typeof body?.error === "string" ? body.error.slice(0, 200) : "The update could not start.",
      },
    };
  }
  throw new Error(`companion control ${res.status}`);
}

/** Look for new gadget firmware now: Settings → Remote access opened with a
 * gadget paired. The check runs in the background; the next poll shows it. */
export async function companionFirmwareCheck() {
  if (!proc) return companionState();
  await control("POST", "/firmware-updates/check", {}).catch(() => {});
  return companionState();
}

```

- [ ] **Step 4: Register the IPC in `electron/main.mjs`**

1. In the `import { … } from "./companion.mjs";` list (origin/main `:509-526`), after `  companionBrowserControlAccess,`, add:

```js
  companionFirmwareCheck,
  companionFirmwareUpdate,
```

2. Immediately before `function publicDesktopRemoteState() {`, add:

```js
// Gadget firmware (spec §8): the Update button, and the release check when
// Settings → Remote access opens with a gadget paired. A refused update comes
// back on the state as firmwareRefusal so the Update cell can say why.
ipcMain.handle("companion:firmware-update", localOnly("companion:firmware-update", async (_event, deviceId) => {
  const result = await companionFirmwareUpdate(deviceId);
  const state = await desktopCompanionState();
  return result?.firmwareRefusal ? { ...state, firmwareRefusal: result.firmwareRefusal } : state;
}));
ipcMain.handle("companion:firmware-check", localOnly("companion:firmware-check", () =>
  companionFirmwareCheck().then(() => desktopCompanionState()),
));

```

- [ ] **Step 5: Expose the two methods in `electron/preload.cjs`**

In the `companion: {` object, as its last two members (after `revoke:` and P3a's `gadget`, `pairGadget` and `pairingBot` lines, before the `  },` that closes the object), add:

```js
    firmwareUpdate: (deviceId) => ipcRenderer.invoke("companion:firmware-update", deviceId),
    firmwareCheck: () => ipcRenderer.invoke("companion:firmware-check"),
```

- [ ] **Step 6: Run the Electron tests**

Run: `pnpm test:electron`
Expected: PASS for every file. That includes the 4 new firmware cases, the new preload case, and `companion-browser.node-test.mjs` unchanged. A `SyntaxError: Unexpected token 'export'` there means the helpers were placed after `companionBrowserControlAccess`.

Run: `pnpm check:electron`
Expected: `Syntax-checked N Electron modules.`

- [ ] **Step 7: Commit**

```bash
git add electron/companion.mjs electron/main.mjs electron/preload.cjs electron/preload.node-test.mjs electron/companion-firmware.node-test.mjs
git commit -m "feat(gadget): Electron IPC for firmware updates and checks" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 10: Renderer state and update helpers

**Files:**
- Modify: `src/components/PhoneSetupFlow.tsx`:
  - the new interfaces go before `export interface CompanionState {` (origin/main `:73`);
  - two fields go before CompanionState's `  error?: string;` (`:93`);
  - two bridge members go before the closing `};` of `export type CompanionBridge = {` (`:96-106`);
  - `shouldPoll` is at `:772`; the import goes after `import { brand } from "../lib/brand";` (`:61`). Both are Contract deviation 1 (Step 5).
- Modify: `src/lib/gadgets.ts` (P3a's file): append the P4b helpers.
- Test: `src/lib/gadgets.update.test.ts`

**Interfaces:**
- Consumes: P3a's `GadgetDevice` and `PairedDevice` (in `PhoneSetupFlow.tsx`) and P3a's `isGadget`. The vendored `versions.json`.
- Produces:
  - Contract §3.18 types and fields: `GadgetUpdateStatus`, `GadgetFirmwareState`, `CompanionState.gadgetFirmware?`, `CompanionState.firmwareRefusal?`, `CompanionBridge.firmwareUpdate(deviceId): Promise<unknown>`, `CompanionBridge.firmwareCheck(): Promise<unknown>`.
  - Pinned helpers: `isCustomBuild(fw)`, `type GadgetUpdateCellState`, `updateCellState(device, firmware)`.
  - P4b-private helpers: `compareFirmwareVersions`, `availableUpdate`, `updatePercent`, `hasActiveGadgetUpdate`.

- [ ] **Step 1: Add the types to `src/components/PhoneSetupFlow.tsx`**

Immediately before `export interface CompanionState {`, add:

```ts
/** One gadget's firmware update, as the companion reports it (companion/src/gadget/firmware.ts). */
export interface GadgetUpdateStatus {
  phase: "downloading" | "verifying" | "offering" | "sending" | "committing" | "restarting" | "installed" | "failed";
  version: string;
  offset?: number;
  size?: number;
  error?: string;
  at: number;
}

/** companionState().gadgetFirmware: the latest release and every gadget's update (spec §8). */
export interface GadgetFirmwareState {
  latest: { version: string; boards: string[]; checkedAt: number } | null;
  checking: boolean;
  error?: string;
  updates: Record<string, GadgetUpdateStatus>;
}

```

In `export interface CompanionState`, immediately before its `  error?: string;` line, add:

```ts
  /** Gadget firmware: the latest release and per-gadget updates (spec §8). */
  gadgetFirmware?: GadgetFirmwareState;
  /** Present only on the answer to firmwareUpdate() when the companion refused (404/409). */
  firmwareRefusal?: { deviceId: string; code: "no_gadget" | "offline" | "custom_build" | "no_update" | "busy"; error: string };
```

In `export type CompanionBridge = {`, immediately before its closing `};`, add:

```ts
  firmwareUpdate: (deviceId: string) => Promise<unknown>;
  firmwareCheck: () => Promise<unknown>;
```

- [ ] **Step 2: Write the failing helper test**

`src/lib/gadgets.update.test.ts`:

```ts
// The renderer's half of spec §8: which gadget rows say Update, Custom build or Updating.
import { readFileSync } from "node:fs";
import { describe, expect, it } from "vitest";

import type { GadgetDevice, GadgetFirmwareState, GadgetUpdateStatus } from "../components/PhoneSetupFlow";
import {
  availableUpdate,
  compareFirmwareVersions,
  hasActiveGadgetUpdate,
  isCustomBuild,
  updateCellState,
  updatePercent,
} from "./gadgets";

const versions = JSON.parse(
  readFileSync(new URL("../../companion/test/fixtures/gadget-vectors/versions.json", import.meta.url), "utf8"),
) as { compare: Array<{ name: string; a: string; b: string; cmp: number }>; custom: Array<{ name: string; fw: string; custom: boolean }> };

const device = (firmware: string, board = "amoled-175c"): GadgetDevice => ({
  kind: "gadget", id: "gad_3f9a0c2b7e41d856", name: "Desk Maus", createdAt: 1, lastSeenAt: 1, publicKey: "BA==",
  board, firmware, botId: null, speakPushes: false,
});
const firmware = (updates: Record<string, GadgetUpdateStatus> = {}, version = "1.0.1"): GadgetFirmwareState => ({
  latest: { version, boards: ["amoled-175c", "lcd-154"], checkedAt: 1 }, checking: false, updates,
});
const status = (phase: GadgetUpdateStatus["phase"], extra: Partial<GadgetUpdateStatus> = {}): GadgetUpdateStatus => ({ phase, version: "1.0.1", at: 1, ...extra });

describe("the shared version vectors", () => {
  it("order versions exactly as the companion and the firmware do", () => {
    for (const c of versions.compare) {
      expect(compareFirmwareVersions(c.a, c.b), c.name).toBe(c.cmp);
      expect(compareFirmwareVersions(c.b, c.a), c.name).toBe(-c.cmp || 0);
    }
    for (const c of versions.custom) expect(isCustomBuild(c.fw), c.name).toBe(c.custom);
  });
});

describe("availableUpdate", () => {
  it("names the newer release for the gadget's own board only", () => {
    expect(availableUpdate(device("1.0.0"), firmware())).toBe("1.0.1");
    expect(availableUpdate(device("1.0.1-rc.2"), firmware())).toBe("1.0.1");
    expect(availableUpdate(device("1.0.1"), firmware())).toBeNull();
    expect(availableUpdate(device("1.2.0"), firmware())).toBeNull();
    expect(availableUpdate(device("1.0.0", "devkit"), firmware())).toBeNull();
    expect(availableUpdate(device("0.0.0-dev"), firmware())).toBeNull();
    expect(availableUpdate(device("1.0.0"), undefined)).toBeNull();
    expect(availableUpdate(device("1.0.0"), { ...firmware(), latest: null })).toBeNull();
  });
});

describe("updateCellState", () => {
  it.each([
    ["an older release", device("1.0.0"), firmware(), { kind: "available", version: "1.0.1" }],
    ["the latest release", device("1.0.1"), firmware(), { kind: "current" }],
    ["a custom build, even with a newer release out", device("0.0.0-dev"), firmware(), { kind: "custom" }],
    ["a gadget with no firmware state yet", device("1.0.0"), undefined, { kind: "current" }],
    ["an update in progress", device("1.0.0"), firmware({ gad_3f9a0c2b7e41d856: status("sending", { offset: 10, size: 20 }) }), { kind: "updating", status: status("sending", { offset: 10, size: 20 }) }],
    ["a gadget waiting to come back", device("1.0.0"), firmware({ gad_3f9a0c2b7e41d856: status("restarting") }), { kind: "updating", status: status("restarting") }],
    ["a finished update", device("1.0.1"), firmware({ gad_3f9a0c2b7e41d856: status("installed") }), { kind: "installed", version: "1.0.1" }],
    ["a failed update", device("1.0.0"), firmware({ gad_3f9a0c2b7e41d856: status("failed", { error: "The gadget is busy. Try again when it is idle." }) }), { kind: "failed", error: "The gadget is busy. Try again when it is idle." }],
  ] as const)("%s", (_name, gadget, state, expected) => {
    expect(updateCellState(gadget, state)).toEqual(expected);
  });

  it("ignores another gadget's update", () => {
    expect(updateCellState(device("1.0.0"), firmware({ gad_0000000000000000: status("sending") }))).toEqual({ kind: "available", version: "1.0.1" });
  });
});

describe("progress", () => {
  it("counts acknowledged bytes while sending and is full from the commit on", () => {
    expect(updatePercent(status("downloading"))).toBe(0);
    expect(updatePercent(status("sending", { offset: 0, size: 200 }))).toBe(0);
    expect(updatePercent(status("sending", { offset: 199, size: 200 }))).toBe(99);
    expect(updatePercent(status("sending", { offset: 200, size: 200 }))).toBe(100);
    expect(updatePercent(status("committing"))).toBe(100);
    expect(updatePercent(status("restarting"))).toBe(100);
  });

  it("asks for fast polling only while an update runs", () => {
    expect(hasActiveGadgetUpdate(null)).toBe(false);
    expect(hasActiveGadgetUpdate({})).toBe(false);
    expect(hasActiveGadgetUpdate({ gadgetFirmware: firmware({ a: status("installed"), b: status("failed") }) })).toBe(false);
    expect(hasActiveGadgetUpdate({ gadgetFirmware: firmware({ a: status("installed"), b: status("verifying") }) })).toBe(true);
  });
});
```

- [ ] **Step 3: Run it to see it fail**

Run: `pnpm exec vitest run src/lib/gadgets.update.test.ts`
Expected: FAIL with `TypeError: compareFirmwareVersions is not a function` (and likewise for the other new helpers).

- [ ] **Step 4: Append the helpers to `src/lib/gadgets.ts`**

First add `GadgetFirmwareState, GadgetUpdateStatus` to P3a's existing `import type { … } from "../components/PhoneSetupFlow"` line at the top of the file (keep its exact module specifier). Then append:

```ts

// ---- firmware updates (P4b; spec §8) ----------------------------------------

/** The release version shape (the companion's releases.ts and the SDK's protocol/lib/version.ts use the same). */
const RELEASE_VERSION_RE = /^\d+\.\d+\.\d+(-[0-9A-Za-z.]+)?$/;

/** fw ends in "-dev", or is not a release version at all: a maker's own build, updated over USB. */
export function isCustomBuild(fw: string): boolean {
  return fw.endsWith("-dev") || !RELEASE_VERSION_RE.test(fw);
}

function compareIdentifiers(a: string, b: string): number {
  const an = /^\d+$/.test(a);
  const bn = /^\d+$/.test(b);
  if (an && bn) return Math.sign(Number(a) - Number(b));
  if (an !== bn) return an ? -1 : 1;
  return a < b ? -1 : a > b ? 1 : 0;
}

/** SemVer 2.0.0 precedence (−1, 0, 1) of two release versions, as the companion orders them.
 *  Callers rule out custom builds first; anything else compares as equal. */
export function compareFirmwareVersions(a: string, b: string): number {
  if (!RELEASE_VERSION_RE.test(a) || !RELEASE_VERSION_RE.test(b)) return 0;
  const split = (v: string) => {
    const dash = v.indexOf("-");
    return { core: (dash < 0 ? v : v.slice(0, dash)).split(".").map(Number), pre: dash < 0 ? [] : v.slice(dash + 1).split(".") };
  };
  const x = split(a);
  const y = split(b);
  for (let i = 0; i < 3; i += 1) if (x.core[i] !== y.core[i]) return x.core[i]! < y.core[i]! ? -1 : 1;
  if (x.pre.length === 0 || y.pre.length === 0) return x.pre.length === y.pre.length ? 0 : x.pre.length === 0 ? 1 : -1;
  for (let i = 0; i < Math.min(x.pre.length, y.pre.length); i += 1) {
    const c = compareIdentifiers(x.pre[i]!, y.pre[i]!);
    if (c !== 0) return c;
  }
  return Math.sign(x.pre.length - y.pre.length);
}

export type GadgetUpdateCellState =
  | { kind: "custom" }
  | { kind: "current" }
  | { kind: "available"; version: string }
  | { kind: "updating"; status: GadgetUpdateStatus }
  | { kind: "installed"; version: string }
  | { kind: "failed"; error: string };

const UPDATING: ReadonlySet<GadgetUpdateStatus["phase"]> = new Set<GadgetUpdateStatus["phase"]>([
  "downloading", "verifying", "offering", "sending", "committing", "restarting",
]);

/** The newer official version for this gadget's board, or null: custom build, a board the
 *  release does not cover, already current or ahead, or no release seen yet. */
export function availableUpdate(device: GadgetDevice, firmware: GadgetFirmwareState | undefined): string | null {
  const latest = firmware?.latest;
  if (!latest || isCustomBuild(device.firmware) || isCustomBuild(latest.version) || !latest.boards.includes(device.board)) return null;
  return compareFirmwareVersions(latest.version, device.firmware) > 0 ? latest.version : null;
}

/** What the Update cell shows for one gadget row. */
export function updateCellState(device: GadgetDevice, firmware: GadgetFirmwareState | undefined): GadgetUpdateCellState {
  const status = firmware && Object.hasOwn(firmware.updates, device.id) ? firmware.updates[device.id] : undefined;
  if (status && UPDATING.has(status.phase)) return { kind: "updating", status };
  if (status?.phase === "installed") return { kind: "installed", version: status.version };
  if (isCustomBuild(device.firmware)) return { kind: "custom" };
  if (status?.phase === "failed") return { kind: "failed", error: status.error ?? "" };
  const version = availableUpdate(device, firmware);
  return version ? { kind: "available", version } : { kind: "current" };
}

/** 0–100 for "Updating… {percent}%": acknowledged bytes while sending, 100 from the commit on. */
export function updatePercent(status: GadgetUpdateStatus): number {
  if (status.phase === "committing" || status.phase === "restarting" || status.phase === "installed") return 100;
  if (status.phase !== "sending" || !status.size) return 0;
  return Math.max(0, Math.min(100, Math.floor(((status.offset ?? 0) / status.size) * 100)));
}

/** True while any gadget is mid-update; the Remote access panel then polls every second. */
export function hasActiveGadgetUpdate(state: { gadgetFirmware?: GadgetFirmwareState } | null | undefined): boolean {
  return Object.values(state?.gadgetFirmware?.updates ?? {}).some((status) => UPDATING.has(status.phase));
}
```

- [ ] **Step 5: Poll every second while an update runs (Contract deviation 1)**

Contract §5.2 pins P4b's edits to `PhoneSetupFlow.tsx` to the firmware types, so this edit waits for review. Check whether the contract lists it yet:

```bash
grep -c "shouldPoll" /Users/omkar/Desktop/openmaus/openmausbot-gadget-sdk/docs/plans/00-interfaces.md
```

If it prints `0`, skip the rest of this step: leave `shouldPoll` and the imports alone, and note the skip for the hand-off (Task 14 Step 6). The row then refreshes on the panel's normal 10 s poll. If it prints `1` or more, the deviation is approved; make the edit below.

In `src/components/PhoneSetupFlow.tsx`, after `import { brand } from "../lib/brand";`, add the import. If the file already imports from `"../lib/gadgets"` (P3a), add `hasActiveGadgetUpdate` to that import instead:

```ts
import { hasActiveGadgetUpdate } from "../lib/gadgets";
```

Replace:

```ts
  const shouldPoll = flow.active || Boolean(state?.pairing);
```

with:

```ts
  // A firmware update moves fast and ends in a gadget restart; poll every
  // second while one runs so the row's progress and result keep up.
  const shouldPoll = flow.active || Boolean(state?.pairing) || hasActiveGadgetUpdate(state);
```

- [ ] **Step 6: Run the tests and the type check**

Run: `pnpm exec vitest run src/lib/gadgets.update.test.ts src/lib/gadgets.test.ts`
Expected: PASS (13 new, plus P3a's).

Run: `pnpm typecheck`
Expected: exit 0.

- [ ] **Step 7: Commit**

```bash
git add src/components/PhoneSetupFlow.tsx src/lib/gadgets.ts src/lib/gadgets.update.test.ts
git commit -m "feat(gadget): renderer firmware state, update helpers and fast polling" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 11: The Update cell and Remote access

**Files:**
- Create: `src/components/GadgetUpdateCell.tsx`
- Test: `src/components/GadgetUpdateCell.test.ts`, `src/components/CompanionSection.firmware.test.ts`
- Modify:
  - `src/locales/en.json`: seven keys after P3a's `"remote.gadgets.pairedToast"`.
  - `src/components/CompanionSection.tsx`: imports at origin/main `:12-27`; the hooks go immediately before `  if (!companionBridge()) {` (`:158`); the `updateCell` prop goes on P3a's `<GadgetRow`.

**Interfaces:**
- Consumes: Task 10's helpers and types. `t(key, params)` (`@/lib/i18n`, keys typed from `en.json`). P3a's `GadgetRow` prop `updateCell?: ReactNode` (contract §3.18). `c.act(call)` and `c.busy` (`PhoneSetupController`, `PhoneSetupFlow.tsx:193-232`).
- Produces: `GadgetUpdateCell({device, firmware, refusal?, busy, onUpdate})` and `interface GadgetUpdateCellProps`. `CompanionSection` calls `firmwareCheck()` once per visit when a gadget is paired, and passes each gadget row its cell.

- [ ] **Step 1: Add the English strings**

Run from `$WT`:

```bash
node --input-type=module <<'EOF'
import { readFileSync, writeFileSync } from "node:fs";
const file = "src/locales/en.json";
const text = readFileSync(file, "utf8");
const en = JSON.parse(text);
if (JSON.stringify(en, null, 2) + "\n" !== text) throw new Error("en.json is not in JSON.stringify(value, null, 2) form");
const after = "remote.gadgets.pairedToast";
if (!Object.hasOwn(en, after)) throw new Error(`en.json has no ${after}; P3a adds the remote.gadgets.* keys first`);
const add = {
  "remote.gadgets.update": "Update to {version}",
  "remote.gadgets.updateAvailable": "Update available",
  "remote.gadgets.updating": "Updating… {percent}%",
  "remote.gadgets.updated": "Updated to {version}",
  "remote.gadgets.updateFailed": "Update failed: {reason}",
  "remote.gadgets.customBuild": "Custom build",
  "remote.gadgets.customBuildDetail": "Built from source. Update it over USB.",
};
for (const key of Object.keys(add)) if (Object.hasOwn(en, key)) throw new Error(`${key} is already in en.json`);
const out = {};
for (const [key, value] of Object.entries(en)) {
  out[key] = value;
  if (key === after) Object.assign(out, add);
}
writeFileSync(file, JSON.stringify(out, null, 2) + "\n");
console.log(`added ${Object.keys(add).length} keys after ${after}`);
EOF
git diff --stat src/locales/en.json
```

Expected: `added 7 keys after remote.gadgets.pairedToast`, then `1 file changed, 7 insertions(+)`.

- [ ] **Step 2: Write the failing cell test**

`src/components/GadgetUpdateCell.test.ts`:

```ts
// The Update / Custom build cell on a gadget row (spec §6.4, §8).
import { createElement } from "react";
import { renderToStaticMarkup } from "react-dom/server";
import { describe, expect, it } from "vitest";

import { GadgetUpdateCell, type GadgetUpdateCellProps } from "./GadgetUpdateCell";
import type { GadgetDevice, GadgetFirmwareState, GadgetUpdateStatus } from "./PhoneSetupFlow";

const ID = "gad_3f9a0c2b7e41d856";
const device = (firmware: string): GadgetDevice => ({
  kind: "gadget", id: ID, name: "Desk Maus", createdAt: 1, lastSeenAt: 1, publicKey: "BA==",
  board: "amoled-175c", firmware, botId: null, speakPushes: false,
});
const firmware = (update?: GadgetUpdateStatus): GadgetFirmwareState => ({
  latest: { version: "1.0.1", boards: ["amoled-175c"], checkedAt: 1 }, checking: false, updates: update ? { [ID]: update } : {},
});
const render = (props: Partial<GadgetUpdateCellProps>) =>
  renderToStaticMarkup(createElement(GadgetUpdateCell, { device: device("1.0.0"), firmware: firmware(), busy: false, onUpdate: () => {}, ...props }));
const button = (markup: string) => markup.match(/<button[^>]*>.*?<\/button>/)?.[0] ?? null;

describe("GadgetUpdateCell", () => {
  it("offers the newer release", () => {
    const markup = render({});
    expect(markup).toContain("Update available");
    expect(button(markup)).toContain("Update to 1.0.1");
    expect(button(markup)).not.toMatch(/ disabled=""/);
    expect(button(render({ busy: true }))).toMatch(/ disabled=""/);
  });

  it("shows nothing for a gadget on the latest release", () => {
    expect(render({ device: device("1.0.1") })).toBe("");
  });

  it("calls a custom build that, with no Update button", () => {
    const markup = render({ device: device("0.0.0-dev") });
    expect(markup).toContain("Custom build");
    expect(markup).toContain("Built from source. Update it over USB.");
    expect(button(markup)).toBeNull();
  });

  it("shows progress while updating, and no button", () => {
    const markup = render({ firmware: firmware({ phase: "sending", version: "1.0.1", offset: 32_768, size: 65_536, at: 1 }) });
    expect(markup).toContain("Updating… 50%");
    expect(markup).toMatch(/role="progressbar"[^>]*aria-valuenow="50"/);
    expect(button(markup)).toBeNull();
    expect(render({ firmware: firmware({ phase: "restarting", version: "1.0.1", at: 1 }) })).toContain("Updating… 100%");
  });

  it("says when the update landed", () => {
    expect(render({ device: device("1.0.1"), firmware: firmware({ phase: "installed", version: "1.0.1", at: 1 }) })).toContain("Updated to 1.0.1");
  });

  it("says why an update failed and offers it again", () => {
    const markup = render({ firmware: firmware({ phase: "failed", version: "1.0.1", error: "The gadget disconnected during the update.", at: 1 }) });
    expect(markup).toMatch(/role="alert"[^>]*>Update failed: The gadget disconnected during the update\./);
    expect(button(markup)).toContain("Update to 1.0.1");
  });

  it("shows the companion's reason when a click did not start an update", () => {
    const markup = render({ refusal: "The gadget is offline. Turn it on and try again." });
    expect(markup).toContain("Update failed: The gadget is offline. Turn it on and try again.");
    expect(button(markup)).toContain("Update to 1.0.1");
  });
});
```

- [ ] **Step 3: Run it to see it fail**

Run: `pnpm exec vitest run src/components/GadgetUpdateCell.test.ts`
Expected: FAIL. Vitest cannot load `./GadgetUpdateCell`.

- [ ] **Step 4: Write `src/components/GadgetUpdateCell.tsx`**

```tsx
// The Update / Custom build cell at the bottom of a gadget row in Settings →
// Remote access (spec §6.4, §8). Pure: everything it shows comes from the
// companion's /state (gadgetFirmware) and the last refused Update click.
import { Download, Loader2 } from "lucide-react";
import { t } from "@/lib/i18n";
import { availableUpdate, updateCellState, updatePercent } from "../lib/gadgets";
import type { GadgetDevice, GadgetFirmwareState } from "./PhoneSetupFlow";

export interface GadgetUpdateCellProps {
  device: GadgetDevice;
  firmware: GadgetFirmwareState | undefined;
  /** Why the last Update click did not start: the companion's 404/409 sentence. */
  refusal?: string;
  busy: boolean;
  onUpdate: () => void;
}

const ROW = "mt-3 flex items-center justify-between gap-3 border-t border-hairline/30 pt-3";

export function GadgetUpdateCell({ device, firmware, refusal, busy, onUpdate }: GadgetUpdateCellProps) {
  const cell = updateCellState(device, firmware);

  if (cell.kind === "updating") {
    const percent = updatePercent(cell.status);
    const label = t("remote.gadgets.updating", { percent });
    return (
      <div className={ROW} aria-live="polite">
        <div className="min-w-0 flex-1">
          <div className="text-[12px] text-ink">{label}</div>
          <div
            role="progressbar"
            aria-label={label}
            aria-valuemin={0}
            aria-valuemax={100}
            aria-valuenow={percent}
            className="mt-1.5 h-1 overflow-hidden rounded-full bg-control"
          >
            <div className="h-full rounded-full bg-accent" style={{ width: `${percent}%` }} />
          </div>
        </div>
        <Loader2 size={14} className="shrink-0 animate-spin text-ink-secondary" />
      </div>
    );
  }

  if (cell.kind === "custom") {
    return (
      <div className={ROW}>
        <div>
          <div className="text-[12px] text-ink">{t("remote.gadgets.customBuild")}</div>
          <div className="mt-0.5 text-[11px] text-ink-secondary">{t("remote.gadgets.customBuildDetail")}</div>
        </div>
      </div>
    );
  }

  if (cell.kind === "installed") {
    return (
      <div className={ROW} aria-live="polite">
        <div className="text-[12px] text-success">{t("remote.gadgets.updated", { version: cell.version })}</div>
      </div>
    );
  }

  // available, failed or current; a refused click shows its reason on any of them
  const reason = refusal ?? (cell.kind === "failed" ? cell.error : undefined);
  const version = cell.kind === "available" ? cell.version : availableUpdate(device, firmware);
  if (!reason && !version) return null;
  return (
    <div className={ROW}>
      <div className="min-w-0">
        {reason
          ? <div role="alert" className="text-[12px] text-danger">{t("remote.gadgets.updateFailed", { reason })}</div>
          : <div className="text-[12px] text-ink">{t("remote.gadgets.updateAvailable")}</div>}
      </div>
      {version && (
        <button
          type="button"
          disabled={busy}
          onClick={onUpdate}
          className="flex shrink-0 items-center gap-1.5 rounded-lg bg-control px-2.5 py-1.5 text-[12px] font-medium text-ink hover:bg-control/80 disabled:opacity-40"
        >
          <Download size={13} />
          {t("remote.gadgets.update", { version })}
        </button>
      )}
    </div>
  );
}
```

- [ ] **Step 5: Run it to see it pass**

Run: `pnpm exec vitest run src/components/GadgetUpdateCell.test.ts`
Expected: PASS (7).

- [ ] **Step 6: Write the failing CompanionSection test**

`src/components/CompanionSection.firmware.test.ts`:

```ts
// CompanionSection's two P4b jobs (spec §6.4, §8): ask the companion for a firmware check once
// per visit when a gadget is paired, and give every gadget row its own Update cell.
import { createElement, isValidElement, type EffectCallback, type ReactElement } from "react";
import { renderToStaticMarkup } from "react-dom/server";
import { beforeEach, expect, it, vi } from "vitest";

import type { GadgetUpdateCellProps } from "./GadgetUpdateCell";

const f = vi.hoisted(() => {
  const calls: Array<[string, unknown[]]> = [];
  // Every bridge method answers {} and is recorded, so effects P3a added keep working too.
  const bridge = new Proxy({}, {
    get: (_target, name) =>
      name === "then" ? undefined : (...args: unknown[]) => { calls.push([String(name), args]); return Promise.resolve({}); },
  });
  return {
    values: [] as unknown[],
    index: 0,
    effects: [] as EffectCallback[],
    state: null as Record<string, unknown> | null,
    rows: [] as Array<Record<string, unknown>>,
    acts: [] as Array<(companion: unknown) => Promise<unknown>>,
    calls,
    bridge,
  };
});
vi.mock("react", async (original) => ({
  ...await original<typeof import("react")>(),
  useRef: (initial: unknown) => { const index = f.index++; if (!(index in f.values)) f.values[index] = { current: initial }; return f.values[index]; },
  useEffect: (effect: EffectCallback) => { f.effects.push(effect); },
}));
vi.mock("../lib/phone-pairing", () => ({ revealPhonePairing: () => false }));
vi.mock("@/state/store", () => ({ useStore: () => ({ state: { config: {}, bots: [{ id: "b1", name: "Jev" }] } }) }));
vi.mock("./PhoneSetupFlow", () => ({
  companionBridge: () => f.bridge,
  usePhoneSetupController: () => ({
    state: f.state, busy: false, accountBusy: false, account: null, accountError: null, error: null,
    localFallback: false, tailscaleFallback: false, tailscaleAvailable: false, hostedReady: false,
    act: (call: (companion: unknown) => Promise<unknown>) => { f.acts.push(call); return Promise.resolve(); },
  }),
  PhoneSetupFlowView: () => null,
  companionAccountActionError: () => null,
  loadCompanionBridgeState: () => null,
  shouldHydrateCompanionEmail: () => false,
}));
vi.mock("./GadgetRow", () => ({ GadgetRow: (props: Record<string, unknown>) => { f.rows.push(props); return null; } }));
vi.mock("./PairGadgetPanel", () => ({ PairGadgetPanel: () => null }));
import { CompanionSection } from "./CompanionSection";
import { GadgetUpdateCell } from "./GadgetUpdateCell";

const GADGET = {
  kind: "gadget", id: "gad_3f9a0c2b7e41d856", name: "Desk Maus", createdAt: 1, lastSeenAt: 1, publicKey: "BA==",
  board: "amoled-175c", firmware: "1.0.0", botId: "b1", speakPushes: false,
};
const PHONE = { id: "phone-1", name: "Ada", createdAt: 1, lastSeenAt: 1, cloudDesktopAccess: false };
const FIRMWARE = { latest: { version: "1.0.1", boards: ["amoled-175c"], checkedAt: 1 }, checking: false, updates: {} };

function render(): void {
  f.index = 0;
  f.effects = [];
  f.rows = [];
  renderToStaticMarkup(createElement(() => CompanionSection({})));
  for (const effect of f.effects) {
    try {
      effect();
    } catch {
      // other components' effects are not under test here
    }
  }
}
const checks = () => f.calls.filter(([name]) => name === "firmwareCheck").length;
const cells = () =>
  f.rows.map((props) => props.updateCell).filter((cell): cell is ReactElement<GadgetUpdateCellProps> => isValidElement(cell));

beforeEach(() => {
  f.values = [];
  f.index = 0;
  f.acts = [];
  f.calls.length = 0;
  f.state = { enabled: true, keepAwake: false, port: 8810, devices: [PHONE, GADGET], connectedDeviceIds: [GADGET.id], pairing: null, gadgetFirmware: FIRMWARE };
});

it("asks for a firmware check once per visit when a gadget is paired", () => {
  render();
  render();
  render();
  expect(checks()).toBe(1);
});

it("never asks with only phones paired", () => {
  f.state = { ...f.state!, devices: [PHONE] };
  render();
  expect(checks()).toBe(0);
});

it("gives the gadget row an Update cell for that gadget, wired to firmwareUpdate", async () => {
  render();
  expect(cells()).toHaveLength(1);
  const cell = cells()[0]!;
  expect(cell.type).toBe(GadgetUpdateCell);
  expect(cell.props.device.id).toBe(GADGET.id);
  expect(cell.props.firmware).toEqual(FIRMWARE);
  expect(cell.props.refusal).toBeUndefined();
  cell.props.onUpdate();
  expect(f.acts).toHaveLength(1);
  await f.acts[0]!(f.bridge);
  expect(f.calls).toContainEqual(["firmwareUpdate", [GADGET.id]]);
});

it("hands a refusal only to the row it belongs to", () => {
  const refusal = { deviceId: GADGET.id, code: "offline", error: "The gadget is offline. Turn it on and try again." };
  f.state = { ...f.state!, firmwareRefusal: refusal };
  render();
  expect(cells()[0]!.props.refusal).toBe(refusal.error);
  f.state = { ...f.state!, firmwareRefusal: { ...refusal, deviceId: "gad_0000000000000000" } };
  render();
  expect(cells()[0]!.props.refusal).toBeUndefined();
});
```

P3a's `CompanionSection` may import more runtime values from `./PhoneSetupFlow`, or other modules this file does not mock. If loading fails with "… is not exported by the mocked module" or similar, copy the matching mock entries from P3a's `src/components/CompanionSection.gadget.test.ts` into the mocks above, unchanged. If P3a's `GadgetRow` or `PairGadgetPanel` export names differ, use P3a's names in the two `vi.mock` lines. Do not change the assertions.

- [ ] **Step 7: Run it to see it fail**

Run: `pnpm exec vitest run src/components/CompanionSection.firmware.test.ts`
Expected: FAIL. `checks()` is `0` where `1` is expected, and `cells()` has length `0`.

- [ ] **Step 8: Edit `src/components/CompanionSection.tsx`**

1. Add the cell import next to P3a's `GadgetRow` import:

```tsx
import { GadgetUpdateCell } from "./GadgetUpdateCell";
```

Make sure `isGadget` is imported from `"../lib/gadgets"` (P3a imports it to split the rows; if not, add `import { isGadget } from "../lib/gadgets";`), and that `type CompanionState` is in the `./PhoneSetupFlow` import (it is on origin/main `:18`).

2. Immediately before the line `  if (!companionBridge()) {`, which is after every existing hook, add:

```tsx
  // Spec §8: look for new gadget firmware when Remote access opens with a
  // gadget paired, and when the first one pairs while it is open. Once per visit.
  const gadgetCount = state ? state.devices.filter(isGadget).length : 0;
  const firmwareChecked = useRef(false);
  useEffect(() => {
    if (firmwareChecked.current || gadgetCount === 0) return;
    firmwareChecked.current = true;
    void companionBridge()?.firmwareCheck().catch(() => {});
  }, [gadgetCount]);
```

3. On P3a's gadget row element (`grep -n "<GadgetRow" src/components/CompanionSection.tsx`), add the prop after its `onRemove={…}` line. P3a's plan renders the rows as `gadgets.map((gadget) => (<GadgetRow key={gadget.id} device={gadget} …`, so the names below are P3a's:

```tsx
                updateCell={
                  <GadgetUpdateCell
                    device={gadget}
                    firmware={state.gadgetFirmware}
                    refusal={state.firmwareRefusal?.deviceId === gadget.id ? state.firmwareRefusal.error : undefined}
                    busy={c.busy}
                    onUpdate={() => void c.act((companion) => companion.firmwareUpdate(gadget.id) as Promise<CompanionState>)}
                  />
                }
```

Only if P3a's code names the loop variable differently, use its name in place of `gadget`.

- [ ] **Step 9: Run the renderer tests, the existing Remote access tests and the static checks**

Run: `pnpm exec vitest run src/components/GadgetUpdateCell.test.ts src/components/CompanionSection.firmware.test.ts src/components/CompanionSection.reveal.test.ts src/components/CompanionSection.gadget.test.ts src/locales`
Expected: PASS for every file (7 + 4 new).

Run: `pnpm typecheck && pnpm lint && pnpm i18n:check`
Expected: exit 0. `i18n:check` ends with `locale catalogs valid (…)`.

- [ ] **Step 10: Commit**

```bash
git add src/components/GadgetUpdateCell.tsx src/components/GadgetUpdateCell.test.ts src/components/CompanionSection.tsx src/components/CompanionSection.firmware.test.ts src/locales/en.json
git commit -m "feat(gadget): Update and Custom build on gadget rows in Remote access" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 12: SDK: `tools/release/dev-release.ts`

**Files (SDK):**
- Create: `tools/release/dev-release.ts` (P4b-owned, contract §5.1)
- Test: `tools/release/dev-release.test.ts`. This is Contract deviation 2: contract §5.1 gives the rest of `tools/release/` to P2d. Until the contract lists the test for P4b, it lives in a scratch mirror under `/private/tmp` and is not committed (Step 1 decides where).

**Interfaces:**
- Consumes P1's `protocol/lib` (contract §4.4.1): `b64Encode`, `b64DecodeCanonical`, `firmwareText`, `signP256(privateKeyHex, text): Uint8Array`, `verifyP256`, `publicKeyFromPrivate` and `isCustomBuild`, plus `keys/test-t1.key.hex` and `keys/test-t1.pub.b64`.
- Produces (contract §3.16):
  - The command `node tools/release/dev-release.ts --image <bin> --board <id> --version <v> --out <dir> [--port 0]`. It writes `<dir>/manifest.json` and `<dir>/v<v>/openmausbot-gadget-<board>-<v>.bin` and serves exactly those two paths at `http://127.0.0.1:<port>/`.
  - Exports `parseArgs`, `buildManifest`, `startDevRelease`, `assetPath`, `IMAGE_MAX`, `BOARD_ID_RE`.

- [ ] **Step 1: Make the SDK worktree**

```bash
SDK=/Users/omkar/Desktop/openmaus/openmausbot-gadget-sdk
git -C "$SDK" rev-parse --verify --quiet refs/heads/p2d-installer >/dev/null && echo "p2d: yes" || echo "p2d: no"
git -C "$SDK" rev-parse --verify --quiet refs/heads/p4b-ota >/dev/null \
  && git -C "$SDK" worktree add /Users/omkar/Desktop/openmaus/openmausbot-gadget-sdk-p4b p4b-ota \
  || git -C "$SDK" worktree add -b p4b-ota /Users/omkar/Desktop/openmaus/openmausbot-gadget-sdk-p4b p2d-installer
export SDKWT=/Users/omkar/Desktop/openmaus/openmausbot-gadget-sdk-p4b
cd "$SDKWT"
for f in protocol/lib/encoding.ts protocol/lib/identity.ts protocol/lib/verify.ts protocol/lib/version.ts keys/test-t1.key.hex keys/test-t1.pub.b64; do test -f "$f" || echo "MISSING $f"; done
test -e tools/release/dev-release.ts && echo "dev-release already exists" || true
# Where the test lives (Contract deviation 2): in the SDK once the contract lists it for P4b, otherwise
# in a scratch mirror of the three folders it reads. Later steps read the choice back from the .where file.
if grep -q "dev-release.test.ts" /Users/omkar/Desktop/openmaus/openmausbot-gadget-sdk/docs/plans/00-interfaces.md; then
  DRT="$SDKWT"
else
  DRT=/private/tmp/p4b-dev-release-test
  rm -rf "$DRT" && mkdir -p "$DRT/tools/release" "$DRT/protocol" "$DRT/keys"
  cp -R protocol/lib "$DRT/protocol/lib" && cp keys/test-t1.key.hex keys/test-t1.pub.b64 "$DRT/keys/"
  printf '{ "type": "module" }\n' > "$DRT/package.json"
fi
echo "$DRT" > /private/tmp/p4b-dev-release-test.where; echo "dev-release test lives in $DRT"
```

Expected: `p2d: yes`, then `Preparing worktree …`, no `MISSING` line, and `dev-release test lives in /private/tmp/p4b-dev-release-test` (or in `$SDKWT` once deviation 2 is approved). **If `p2d: no`, stop this task:** the contract (§1.3) branches `p4b-ota` from `p2d-installer`. The app tasks do not depend on it.

- [ ] **Step 2: Write the failing test**

`$DRT/tools/release/dev-release.test.ts`, with `DRT=$(cat /private/tmp/p4b-dev-release-test.where)`. Its imports are relative, so it runs the same in the SDK and in the mirror:

```ts
// SPDX-License-Identifier: Apache-2.0
// tools/release/dev-release.ts: a test-signed release the app accepts (contract §3.16, §4.1).
import { test } from "node:test";
import assert from "node:assert/strict";
import { spawn } from "node:child_process";
import { createHash, randomBytes } from "node:crypto";
import { existsSync, mkdtempSync, readFileSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { fileURLToPath } from "node:url";
import { b64DecodeCanonical } from "../../protocol/lib/encoding.ts";
import { firmwareText } from "../../protocol/lib/identity.ts";
import { verifyP256 } from "../../protocol/lib/verify.ts";
import { IMAGE_MAX, parseArgs, startDevRelease, type Manifest } from "./dev-release.ts";

const T1_PUB = b64DecodeCanonical(readFileSync(new URL("../../keys/test-t1.pub.b64", import.meta.url), "utf8").trim())!;
const TOOL = fileURLToPath(new URL("./dev-release.ts", import.meta.url));

function workdir(imageBytes: Uint8Array): { dir: string; image: string } {
  const dir = mkdtempSync(join(tmpdir(), "dev-release-"));
  const image = join(dir, "app.bin");
  writeFileSync(image, imageBytes);
  return { dir, image };
}

test("parseArgs takes the five flags and refuses what the app would refuse", () => {
  assert.deepEqual(parseArgs(["--image", "a.bin", "--board", "amoled-175c", "--version", "1.0.1", "--out", "o"]),
    { image: "a.bin", board: "amoled-175c", version: "1.0.1", out: "o", port: 0 });
  assert.equal(parseArgs(["--image", "a", "--board", "devkit", "--version", "2.0.0-rc.1", "--out", "o", "--port", "8123"]).port, 8123);
  const base = ["--image", "a", "--board", "amoled-175c", "--out", "o"];
  assert.throws(() => parseArgs([...base, "--version", "1.0.1-dev"]), /no -dev/);
  assert.throws(() => parseArgs([...base, "--version", "v1.0.1"]), /release version/);
  assert.throws(() => parseArgs(["--image", "a", "--board", "AMOLED", "--version", "1.0.1", "--out", "o"]), /board id/);
  assert.throws(() => parseArgs([...base, "--version", "1.0.1", "--port", "70000"]), /--port/);
  assert.throws(() => parseArgs([...base, "--version", "1.0.1", "--key", "x"]), /usage/);
  assert.throws(() => parseArgs(["--image", "a"]), /usage/);
});

test("serves a manifest in the release format, signed with t1, and the image it names", async (t) => {
  const bytes = new Uint8Array(randomBytes(10_000));
  const { dir, image } = workdir(bytes);
  const release = await startDevRelease({ image, board: "amoled-175c", version: "1.0.1", out: join(dir, "out"), port: 0, log: () => {} });
  t.after(() => release.close());
  const base = new URL("./", release.manifestUrl).href;
  assert.equal(base, `http://127.0.0.1:${release.port}/`);
  const manifest = (await (await fetch(release.manifestUrl)).json()) as Manifest;
  assert.deepEqual(Object.keys(manifest), ["version", "boards"]);
  assert.equal(manifest.version, "1.0.1");
  const entry = manifest.boards["amoled-175c"]!;
  // The app's parseManifest rule with an overridden manifest URL (contract §3.16).
  assert.equal(entry.url, `${base}v1.0.1/openmausbot-gadget-amoled-175c-1.0.1.bin`);
  assert.equal(entry.size, 10_000);
  assert.equal(entry.sha256, createHash("sha256").update(bytes).digest("hex"));
  assert.equal(entry.key_id, "t1");
  const sig = b64DecodeCanonical(entry.sig);
  assert.ok(sig, "sig is canonical base64");
  assert.ok(verifyP256(T1_PUB, firmwareText("amoled-175c", "1.0.1", 10_000, entry.sha256), sig));
  assert.deepEqual(new Uint8Array(await (await fetch(entry.url)).arrayBuffer()), bytes);
  assert.equal((await fetch(`${base}v1.0.1/`)).status, 404);
  assert.equal((await fetch(`${base}../keys/test-t1.key.hex`)).status, 404);
  assert.equal(readFileSync(join(dir, "out", "manifest.json"), "utf8"), JSON.stringify(manifest, null, 2) + "\n");
  assert.ok(existsSync(join(dir, "out", "v1.0.1", "openmausbot-gadget-amoled-175c-1.0.1.bin")));
});

test("refuses an empty or oversized image before it listens", async () => {
  for (const size of [0, IMAGE_MAX + 1]) {
    const { dir, image } = workdir(new Uint8Array(size));
    await assert.rejects(startDevRelease({ image, board: "devkit", version: "1.0.1", out: join(dir, "out"), port: 0, log: () => {} }), /1 to 6291456 bytes/);
  }
});

test("the command prints the manifest URL and the companion environment, and exits 2 on bad flags", async () => {
  const { dir, image } = workdir(new Uint8Array(randomBytes(64)));
  const child = spawn(process.execPath, [TOOL, "--image", image, "--board", "lcd-154", "--version", "1.0.1", "--out", join(dir, "out")], {
    stdio: ["ignore", "pipe", "pipe"],
  });
  let stdout = "";
  const printed = new Promise<string>((done) => {
    child.stdout.on("data", (chunk) => {
      stdout += chunk;
      if (stdout.includes("serving until Ctrl-C")) done(stdout);
    });
  });
  const text = await printed;
  const url = /manifest {2}(http:\/\/127\.0\.0\.1:\d+\/manifest\.json)/.exec(text)?.[1];
  assert.ok(url, text);
  assert.match(text, new RegExp(`OMB_GADGET_MANIFEST_URL=${url!.replaceAll(".", "\\.")} OMB_GADGET_TRUST_TEST_KEY=1`));
  assert.equal(((await (await fetch(url!)).json()) as Manifest).boards["lcd-154"]!.key_id, "t1");
  const exited = new Promise<number | null>((done) => child.on("close", (code) => done(code)));
  child.kill("SIGINT");
  assert.equal(await exited, 0);
  const bad = spawn(process.execPath, [TOOL, "--board", "lcd-154"], { stdio: ["ignore", "ignore", "pipe"] });
  let stderr = "";
  bad.stderr.on("data", (chunk) => (stderr += chunk));
  assert.equal(await new Promise((done) => bad.on("close", done)), 2);
  assert.match(stderr, /usage: node tools\/release\/dev-release\.ts/);
});
```

- [ ] **Step 3: Run it to see it fail**

Run: `node --test "$(cat /private/tmp/p4b-dev-release-test.where)/tools/release/dev-release.test.ts"`
Expected: FAIL with `ERR_MODULE_NOT_FOUND` for `tools/release/dev-release.ts`.

- [ ] **Step 4: Write `$SDKWT/tools/release/dev-release.ts`**

```ts
// SPDX-License-Identifier: Apache-2.0
// A local, test-signed firmware release, for trying an over-the-air update end to end without
// publishing anything (contract 00-interfaces.md §3.16). It signs an image with the published
// test key t1 (keys/test-t1.key.hex), writes manifest.json in the exact release format
// (contract §4.1) and serves both on 127.0.0.1. A development MausBot started with
//   OMB_GADGET_MANIFEST_URL=http://127.0.0.1:<port>/manifest.json OMB_GADGET_TRUST_TEST_KEY=1
// then offers the image to a gadget built with test keys (the simulator). Release firmware
// never trusts t1, so nothing served here can update a board running an official release.
//
//   node tools/release/dev-release.ts --image <bin> --board <id> --version <x.y.z> --out <dir> [--port 0]
import { createHash } from "node:crypto";
import { mkdirSync, readFileSync, realpathSync, writeFileSync } from "node:fs";
import { createServer } from "node:http";
import { join, resolve } from "node:path";
import { fileURLToPath } from "node:url";

import { b64Encode } from "../../protocol/lib/encoding.ts";
import { firmwareText } from "../../protocol/lib/identity.ts";
import { publicKeyFromPrivate, signP256, verifyP256 } from "../../protocol/lib/verify.ts";
import { isCustomBuild } from "../../protocol/lib/version.ts";

export const BOARD_ID_RE = /^[a-z0-9-]{1,32}$/;
/** Every board's OTA slot (contract §2.3); the app refuses a larger manifest entry. */
export const IMAGE_MAX = 6_291_456;
const T1_KEY_FILE = fileURLToPath(new URL("../../keys/test-t1.key.hex", import.meta.url));
const USAGE =
  "usage: node tools/release/dev-release.ts --image <bin> --board <id> --version <x.y.z> --out <dir> [--port <n>]";
const FLAGS = new Set(["image", "board", "version", "out", "port"]);

export interface DevReleaseOptions { image: string; board: string; version: string; out: string; port: number }
export interface ManifestBoard { url: string; size: number; sha256: string; sig: string; key_id: string }
export interface Manifest { version: string; boards: Record<string, ManifestBoard> }
export interface DevRelease { port: number; manifestUrl: string; manifest: Manifest; close(): Promise<void> }

/** Flags as `--name value` pairs; throws a message meant for the terminal. */
export function parseArgs(argv: readonly string[]): DevReleaseOptions {
  const values = new Map<string, string>();
  for (let i = 0; i < argv.length; i += 2) {
    const flag = argv[i] ?? "";
    const value = argv[i + 1];
    if (!flag.startsWith("--") || value === undefined || !FLAGS.has(flag.slice(2))) throw new Error(USAGE);
    values.set(flag.slice(2), value);
  }
  const image = values.get("image");
  const board = values.get("board");
  const version = values.get("version");
  const out = values.get("out");
  if (!image || !board || !version || !out) throw new Error(USAGE);
  if (!BOARD_ID_RE.test(board)) throw new Error("--board must be a board id such as amoled-175c");
  if (isCustomBuild(version)) {
    throw new Error("--version must be a release version such as 1.0.1 (no -dev): the app never updates a custom build");
  }
  const port = Number(values.get("port") ?? "0");
  if (!Number.isInteger(port) || port < 0 || port > 65535) throw new Error("--port must be 0 to 65535");
  return { image, board, version, out, port };
}

/** Where the image lives under the server root, the same shape as a GitHub release asset URL. */
export const assetPath = (board: string, version: string): string => `v${version}/openmausbot-gadget-${board}-${version}.bin`;

/** One board's manifest, signed with keyHex under key_id t1, its image URL under base. */
export function buildManifest(input: { image: Uint8Array; board: string; version: string; base: string; keyHex: string }): Manifest {
  const size = input.image.length;
  if (size < 1 || size > IMAGE_MAX) throw new Error(`the image must be 1 to ${IMAGE_MAX} bytes; it is ${size}`);
  const sha256 = createHash("sha256").update(input.image).digest("hex");
  const text = firmwareText(input.board, input.version, size, sha256);
  const der = signP256(input.keyHex, text);
  if (!verifyP256(publicKeyFromPrivate(input.keyHex), text, der)) throw new Error("the signature did not verify");
  const entry: ManifestBoard = { url: input.base + assetPath(input.board, input.version), size, sha256, sig: b64Encode(der), key_id: "t1" };
  return { version: input.version, boards: { [input.board]: entry } };
}

/** Write <out>/manifest.json and the image, and serve exactly those two paths on 127.0.0.1. */
export async function startDevRelease(
  options: DevReleaseOptions & { keyFile?: string; log?: (line: string) => void },
): Promise<DevRelease> {
  const image = new Uint8Array(readFileSync(options.image));
  if (image.length < 1 || image.length > IMAGE_MAX) throw new Error(`the image must be 1 to ${IMAGE_MAX} bytes; it is ${image.length}`);
  const keyHex = readFileSync(options.keyFile ?? T1_KEY_FILE, "utf8").trim();
  const log = options.log ?? ((line: string) => process.stderr.write(`${line}\n`));
  const out = resolve(options.out);
  const asset = assetPath(options.board, options.version);
  const server = createServer((req, res) => {
    const path = new URL(req.url ?? "/", "http://127.0.0.1").pathname;
    const file = path === "/manifest.json" ? join(out, "manifest.json") : path === `/${asset}` ? join(out, asset) : null;
    if (!file || (req.method !== "GET" && req.method !== "HEAD")) {
      res.writeHead(404).end();
      return;
    }
    const body = readFileSync(file);
    res.writeHead(200, {
      "content-type": file.endsWith(".json") ? "application/json" : "application/octet-stream",
      "content-length": body.length,
    });
    res.end(req.method === "HEAD" ? undefined : body);
    log(`dev-release: ${req.method} ${path} (${body.length} bytes)`);
  });
  await new Promise<void>((done, fail) => {
    server.once("error", fail);
    server.listen(options.port, "127.0.0.1", () => done());
  });
  const port = (server.address() as { port: number }).port;
  const base = `http://127.0.0.1:${port}/`;
  const manifest = buildManifest({ image, board: options.board, version: options.version, base, keyHex });
  mkdirSync(join(out, `v${options.version}`), { recursive: true });
  writeFileSync(join(out, asset), image);
  writeFileSync(join(out, "manifest.json"), JSON.stringify(manifest, null, 2) + "\n");
  return {
    port,
    manifestUrl: `${base}manifest.json`,
    manifest,
    close: () => new Promise<void>((done) => server.close(() => done())),
  };
}

async function main(): Promise<void> {
  let options: DevReleaseOptions;
  try {
    options = parseArgs(process.argv.slice(2));
  } catch (error) {
    console.error(error instanceof Error ? error.message : String(error));
    process.exit(2);
  }
  const release = await startDevRelease(options);
  const entry = release.manifest.boards[options.board]!;
  console.log(`dev release ${options.version} for ${options.board}: ${entry.size} bytes, sha256 ${entry.sha256}, signed with t1`);
  console.log(`manifest  ${release.manifestUrl}`);
  console.log("start the MausBot companion (or the desktop app) with:");
  console.log(`  OMB_GADGET_MANIFEST_URL=${release.manifestUrl} OMB_GADGET_TRUST_TEST_KEY=1`);
  console.log("serving until Ctrl-C");
  const stop = (): void => void release.close().then(() => process.exit(0));
  process.on("SIGINT", stop);
  process.on("SIGTERM", stop);
}

// Run as a command (node follows symlinks for the entry point, so compare real paths).
if (process.argv[1] && realpathSync(resolve(process.argv[1])) === fileURLToPath(import.meta.url)) {
  main().catch((error: unknown) => {
    console.error(error instanceof Error ? error.message : String(error));
    process.exit(1);
  });
}
```

- [ ] **Step 5: Run it on both supported Node lines**

```bash
cd "$SDKWT"
DRT=$(cat /private/tmp/p4b-dev-release-test.where)
[ "$DRT" = "$SDKWT" ] || cp tools/release/dev-release.ts "$DRT/tools/release/dev-release.ts"
node --version && node --test "$DRT/tools/release/dev-release.test.ts"
"$HOME/.nvm/versions/node/v24.14.1/bin/node" --test "$DRT/tools/release/dev-release.test.ts"
```

Expected: the default `node` is v22.22.3 on this Mac, and both runs end with `# pass 4` / `ℹ pass 4` and `fail 0`. In the mirror the tool signs with the mirror's copy of `keys/test-t1.key.hex`, which is the same file.

- [ ] **Step 6: Check that the protocol tests and vectors are unaffected**

Run: `npm ci && npm test && npm run vectors:check`
Expected: P1's suites pass and `vectors:check` prints no diff.

- [ ] **Step 7: Commit (SDK)**

```bash
cd "$SDKWT"
git add tools/release/dev-release.ts
if [ "$(cat /private/tmp/p4b-dev-release-test.where)" = "$SDKWT" ]; then git add tools/release/dev-release.test.ts; fi
git commit -m "feat(release): dev-release, a local test-signed release for OTA testing" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
git status --short
```

Expected: one commit, and an empty `git status`. Until Contract deviation 2 is approved, the commit holds `dev-release.ts` only and the test stays in `/private/tmp/p4b-dev-release-test`. Once the contract lists the test for P4b, copy it from there into `tools/release/` and commit it on `p4b-ota` as its own commit.

---

### Task 13: End to end: an update to the simulator through the real companion

No commit. This runs P2a's simulator, P4b's dev release and the app's companion together and records the outcome. It needs Tasks 1–12 and a finished P2a (the simulator is on `p2d-installer` and therefore in `$SDKWT`).

**Files:** none. Scratch files go under a fresh `/private/tmp/omb-ota-e2e.*` directory.

**Interfaces:**
- Consumes:
  - The simulator CLI (contract §2.16): `--board`, `--state-dir`, `--headless`, `--script`, `--host`, `--pair`, the `expect` and `boot` script commands, and re-exec on OTA commit with `--boot 1`.
  - `GADGET_SIM_VERSION` (contract §2.18), the companion's env vars (contract §3.1) and the control routes.
- Produces: a recorded pass of the §10 end-to-end "an update" row against a real (simulated) gadget, at the companion level. The same update through the desktop app (IPC, the Update cell, polling) is a required manual check for Omkar in Task 14 Step 6.

Each step may run in a fresh shell (an agent's commands do). So the long-running processes start under `nohup`, and their PIDs and the simulator's exit code go to files in `$E2E`. Step 1 writes `$E2E` to `/private/tmp/omb-ota-e2e.current`, and every later step reads it back, so each command block can be pasted as it is.

- [ ] **Step 1: Build the simulator at version 1.0.0**

```bash
export PATH="$HOME/.nvm/versions/node/v24.14.1/bin:$PATH"
export SDKWT=/Users/omkar/Desktop/openmaus/openmausbot-gadget-sdk-p4b
export E2E=$(mktemp -d /private/tmp/omb-ota-e2e.XXXXXX); echo "$E2E" > /private/tmp/omb-ota-e2e.current; echo "$E2E"
cmake -S "$SDKWT/firmware" -B "$SDKWT/build/ota-e2e" -DCMAKE_BUILD_TYPE=Debug -DGADGET_WITH_LVGL=OFF -DGADGET_BUILD_TESTS=OFF -DGADGET_SIM_VERSION=1.0.0
cmake --build "$SDKWT/build/ota-e2e" -j10 --target gadget-sim
find "$SDKWT/build/ota-e2e" -type f -name gadget-sim -perm -u+x | head -1 > "$E2E/sim.path"; SIM=$(cat "$E2E/sim.path"); echo "$SIM"; "$SIM" --version
```

Expected: the build ends with `[100%] Built target gadget-sim`, and `--version` prints a line containing `1.0.0`. A `-dev` version here means `GADGET_SIM_VERSION` did not reach the build, and the app would answer `custom_build`.

- [ ] **Step 2: Serve a test-signed 1.0.1**

```bash
export PATH="$HOME/.nvm/versions/node/v24.14.1/bin:$PATH"; export SDKWT=/Users/omkar/Desktop/openmaus/openmausbot-gadget-sdk-p4b; export E2E=$(cat /private/tmp/omb-ota-e2e.current)
head -c 1048576 /dev/urandom > "$E2E/app.bin"
nohup node "$SDKWT/tools/release/dev-release.ts" --image "$E2E/app.bin" --board amoled-175c --version 1.0.1 --out "$E2E/release" --port 18900 > "$E2E/release.log" 2>&1 &
echo $! > "$E2E/release.pid"
cat > "$E2E/wait-url.mjs" <<'EOF'
// node wait-url.mjs <url> <timeout s>: print the JSON body once the URL answers 200
const [url, seconds] = process.argv.slice(2);
const deadline = Date.now() + Number(seconds) * 1000;
for (;;) {
  try { const res = await fetch(url); if (res.ok) { console.log(JSON.stringify(await res.json())); process.exit(0); } } catch {}
  if (Date.now() > deadline) { console.error("timed out"); process.exit(1); }
  await new Promise((r) => setTimeout(r, 250));
}
EOF
node "$E2E/wait-url.mjs" http://127.0.0.1:18900/manifest.json 10 | node -e 'let s="";process.stdin.on("data",d=>s+=d).on("end",()=>{const m=JSON.parse(s);console.log(m.version, Object.keys(m.boards), m.boards["amoled-175c"].key_id)})'
```

Expected: `1.0.1 [ 'amoled-175c' ] t1`. The simulator re-execs itself on commit, so the image bytes are opaque. Any file up to 6 MiB works.

- [ ] **Step 3: Start the companion on its own, trusting t1**

```bash
export PATH="$HOME/.nvm/versions/node/v24.14.1/bin:$PATH"; export E2E=$(cat /private/tmp/omb-ota-e2e.current)
export WT=$(test -d /Users/omkar/Desktop/openmaus/OpenGrokBot-gadget-ota && echo /Users/omkar/Desktop/openmaus/OpenGrokBot-gadget-ota || echo /Users/omkar/Desktop/openmaus/OpenGrokBot-gadget)
cd "$WT"
# The `companion` package script, run directly so the PID is the companion's own.
OMB_COMPANION_DIR="$E2E/companion" OMB_PORT=18799 OMB_COMPANION_PORT=18810 OMB_CONTROL_PORT=18811 OMB_COMPANION_NAME="OTA e2e" \
OMB_GADGET_MANIFEST_URL=http://127.0.0.1:18900/manifest.json OMB_GADGET_TRUST_TEST_KEY=1 \
  nohup node --experimental-strip-types companion/src/index.ts > "$E2E/companion.log" 2>&1 &
echo $! > "$E2E/companion.pid"
cat > "$E2E/wait-state.mjs" <<'EOF'
// node wait-state.mjs <control port> <timeout s> '<JS expression over s (the /state body)>'
const [port, seconds, expression] = process.argv.slice(2);
const test = new Function("s", `return (${expression});`);
const deadline = Date.now() + Number(seconds) * 1000;
for (;;) {
  try {
    const value = test(await (await fetch(`http://127.0.0.1:${port}/state`)).json());
    if (value) { console.log(typeof value === "string" ? value : JSON.stringify(value)); process.exit(0); }
  } catch {}
  if (Date.now() > deadline) { console.error("timed out"); process.exit(1); }
  await new Promise((r) => setTimeout(r, 250));
}
EOF
node "$E2E/wait-state.mjs" 18811 20 's.port === 18810 && "companion up"'
```

Expected: `companion up`. No harness runs here, so the hub logs that the harness is unreachable and a new gadget stays unbound (contract D13). OTA does not need a bot.

- [ ] **Step 4: Pair and run the simulator**

```bash
export PATH="$HOME/.nvm/versions/node/v24.14.1/bin:$PATH"; export E2E=$(cat /private/tmp/omb-ota-e2e.current); SIM=$(cat "$E2E/sim.path")
CODE=$(curl -s -X POST http://127.0.0.1:18811/pairing | node -e 'let s="";process.stdin.on("data",d=>s+=d).on("end",()=>console.log(JSON.parse(s).code))'); echo "$CODE"
cat > "$E2E/ota.txt" <<'EOF'
# P4b e2e: pair, wait for the update and the restart into 1.0.1, then report fw.installed.
expect ready 30000
boot 1 300000
expect ready 30000
expect fw.installed 10000
EOF
nohup sh -c '"$0" --board amoled-175c --state-dir "$1/sim" --headless --script "$1/ota.txt" --host 127.0.0.1:18810 --pair "$2" > "$1/sim.log" 2>&1; echo $? > "$1/sim.exit"' "$SIM" "$E2E" "$CODE" > /dev/null 2>&1 &
node "$E2E/wait-state.mjs" 18811 60 's.devices.find((d) => d.kind === "gadget" && (s.connectedDeviceIds ?? []).includes(d.id))?.id' | tee "$E2E/gadget.id"
```

Expected: six digits, then `gad_` plus 16 hex (also saved to `$E2E/gadget.id`).

- [ ] **Step 5: Check, then update**

```bash
export PATH="$HOME/.nvm/versions/node/v24.14.1/bin:$PATH"; export E2E=$(cat /private/tmp/omb-ota-e2e.current); export ID=$(cat "$E2E/gadget.id")
curl -s -o /dev/null -w '%{http_code}\n' -X POST -H 'content-type: application/json' -d '{}' http://127.0.0.1:18811/firmware-updates/check
node "$E2E/wait-state.mjs" 18811 30 's.gadgetFirmware?.latest?.version'
curl -s -w '\n%{http_code}\n' -X POST -H 'content-type: application/json' -d '{}' "http://127.0.0.1:18811/devices/$ID/firmware-update" | tail -1
node "$E2E/wait-state.mjs" 18811 120 '["sending","committing","restarting","installed"].includes(s.gadgetFirmware.updates[process.env.ID]?.phase) && "streaming started"'
node "$E2E/wait-state.mjs" 18811 300 's.gadgetFirmware.updates[process.env.ID]?.phase === "installed" && s.gadgetFirmware.updates[process.env.ID]'
node -e 'const f=process.argv[1],t=Date.now()+30000;(async()=>{for(;;){try{console.log("sim exit="+require("fs").readFileSync(f,"utf8").trim());process.exit(0)}catch{}if(Date.now()>t){console.error("simulator still running");process.exit(1)}await new Promise(r=>setTimeout(r,250))}})()' "$E2E/sim.exit"
node "$E2E/wait-state.mjs" 18811 10 's.devices.find((d) => d.id === process.env.ID)?.firmware'
grep -c "updating 1.0.0 → 1.0.1" "$E2E/companion.log"
```

Expected, line by line:
1. `202`
2. `1.0.1`
3. `202`
4. `streaming started`. It accepts any phase from `sending` on: a 1 MiB image can stream to the simulator over loopback between two 250 ms polls.
5. `{"phase":"installed","version":"1.0.1","at":…}`
6. `sim exit=0`
7. `1.0.1`
8. `1` (the companion logged `gad_…: updating 1.0.0 → 1.0.1`)

`$E2E/sim.log` shows the second boot. `$E2E/release.log` shows one `GET /v1.0.1/openmausbot-gadget-amoled-175c-1.0.1.bin`.

- [ ] **Step 6: The refusals over the real route**

```bash
export E2E=$(cat /private/tmp/omb-ota-e2e.current); export ID=$(cat "$E2E/gadget.id")
curl -s -X POST -H 'content-type: application/json' -d '{}' "http://127.0.0.1:18811/devices/$ID/firmware-update"; echo
curl -s -X POST -H 'content-type: application/json' -d '{}' "http://127.0.0.1:18811/devices/gad_0000000000000000/firmware-update"; echo
```

Expected: the gadget is now on 1.0.1 and offline (its script ended), so the first line is `{"error":"The gadget is offline. Turn it on and try again.","code":"offline"}`. The second line is `{"error":"No paired gadget has that id.","code":"no_gadget"}`.

- [ ] **Step 7: Clean up**

```bash
export SDKWT=/Users/omkar/Desktop/openmaus/openmausbot-gadget-sdk-p4b; export E2E=$(cat /private/tmp/omb-ota-e2e.current)
for p in companion release; do kill "$(cat "$E2E/$p.pid")" 2>/dev/null; done
rm -rf "$SDKWT/build/ota-e2e"
rm -f /private/tmp/omb-ota-e2e.current
echo "logs kept in $E2E"
```

---

### Task 14: Branch verification, hardware checklist and hand-off

**Files:** none changed.

**Interfaces:** none. This task runs every check on both branches and records what only hardware can show.

- [ ] **Step 1: Static checks (app)**

```bash
cd "$WT"
pnpm i18n:check && pnpm typecheck && pnpm lint && pnpm check:electron && pnpm exec vite build
```

Expected: every command exits 0. `vite build` ends with `built in …`.

- [ ] **Step 2: P4b's suites and their neighbours**

```bash
pnpm exec vitest run companion/test src/lib src/components/GadgetUpdateCell.test.ts src/components/CompanionSection
pnpm test:electron
pnpm build:server && pnpm build:companion && unset ELECTRON_RUN_AS_NODE && pnpm exec electron scripts/smoke-phone-relay-auth.cjs
```

Expected: everything passes. The new app tests are release-keys 4, releases 26, release-checker 16, ota 11, firmware 22, firmware-routes 6, ota.hub 5, firmware-wiring 3 (2 on Windows), gadgets.update 13, GadgetUpdateCell 7, CompanionSection.firmware 4, companion-firmware (node) 4 and the preload case 1. The smoke prints its own success line, as on `origin/main`. `dist-server/` is built first because the smoke forks `dist-server/index.js` (its own header says `pnpm build:server && pnpm build:companion`); Step 1 ran only `vite build`. `ELECTRON_RUN_AS_NODE` is unset because Claude's shell may export it, which makes Electron exit at once.

- [ ] **Step 3: The whole app suite**

Run: `pnpm test`
Expected: exit 0. This runs `vitest run`, `broker:test`, `test:electron` and `test:packaged-server`. It takes tens of minutes: CI splits `vitest` into 4 shards with a 20-minute cap each. P4b touches no harness code, so a server-side failure here is not P4b's. Compare it against `feat/gadget-hub` before investigating.

- [ ] **Step 4: The SDK branch**

```bash
cd "$SDKWT"
DRT=$(cat /private/tmp/p4b-dev-release-test.where)
[ "$DRT" = "$SDKWT" ] || cp tools/release/dev-release.ts "$DRT/tools/release/dev-release.ts"
node --test "$DRT/tools/release/dev-release.test.ts" && npm test && npm run vectors:check
git log --oneline p2d-installer..p4b-ota
```

Expected: everything passes, and the log lists exactly the one commit from Task 12.

- [ ] **Step 5: Branch state for Omkar**

```bash
git -C "$WT" status --short && git -C "$WT" log --oneline feat/gadget-hub..feat/gadget-ota
git -C "$SDKWT" status --short
```

Expected: both status outputs are empty. The app log lists ten commits, one from each of Tasks 2–11.

Do not push, open a PR, create a release or dispatch a workflow. Leave both worktrees in place: the full-app check in Step 6 builds from them, and its last line removes them.

- [ ] **Step 6: Hand-off notes**

Give Omkar these points:

- **`r1` follow-up.** `RELEASE_KEYS` is empty, so the shipped app offers no update until Omkar generates `r1` (P2d's `docs/release-keys.md`). After `keys/release-r1.pub.b64` is committed in the SDK, add one entry and rerun the key test:

  ```bash
  cd "$WT"
  R1=$(tr -d '\n' < /Users/omkar/Desktop/openmaus/openmausbot-gadget-sdk/keys/release-r1.pub.b64)
  node -e 'const k=process.argv[1]; const b=Buffer.from(k,"base64"); if(b.length!==65||b[0]!==4||b.toString("base64")!==k) throw new Error("not a canonical 65-byte SEC1 key"); console.log(k)' "$R1"
  sed -i '' "s|^export const RELEASE_KEYS: readonly ReleaseKey\[\] = \[\];|export const RELEASE_KEYS: readonly ReleaseKey[] = [{ id: \"r1\", pubkey: \"$R1\" }];|" companion/src/gadget/release-keys.ts
  pnpm exec vitest run companion/test/gadget/release-keys.test.ts companion/test/gadget/release-checker.test.ts
  ```

  The node check prints the key. Both test files pass.
- **Contract deviations waiting for review.** Say whether Task 10 Step 5 was made or skipped (deviation 1, fast polling), and whether `dev-release.test.ts` is committed or still in `/private/tmp/p4b-dev-release-test` (deviation 2, which also runs in no SDK CI job). See "Contract deviations".
- **mDNS during tests.** The spawned-sidecar test briefly advertises `_openmausbot._tcp` on whatever network runs the tests, as any companion start does.
- **Full-app update (spec §10 end to end; required, for Omkar).** Task 13 drives the companion on its own. This runs the same update through the desktop app: the IPC channels, the Update cell and the polling. In one terminal, serve a test-signed 1.0.1 (it runs until Ctrl-C):

  ```bash
  export PATH="$HOME/.nvm/versions/node/v24.14.1/bin:$PATH"
  export SDKWT=/Users/omkar/Desktop/openmaus/openmausbot-gadget-sdk-p4b
  head -c 1048576 /dev/urandom > /private/tmp/omb-ota-app.bin
  node "$SDKWT/tools/release/dev-release.ts" --image /private/tmp/omb-ota-app.bin --board amoled-175c --version 1.0.1 --out /private/tmp/omb-ota-app-release --port 18900
  ```

  In a second terminal, build the window-mode simulator at 1.0.0 and this branch as the side-by-side OMB2 app (the `test-locally` skill's script; it ends with `== installed`):

  ```bash
  export PATH="$HOME/.nvm/versions/node/v24.14.1/bin:$PATH"
  export SDKWT=/Users/omkar/Desktop/openmaus/openmausbot-gadget-sdk-p4b
  export WT=$(test -d /Users/omkar/Desktop/openmaus/OpenGrokBot-gadget-ota && echo /Users/omkar/Desktop/openmaus/OpenGrokBot-gadget-ota || echo /Users/omkar/Desktop/openmaus/OpenGrokBot-gadget)
  cmake -S "$SDKWT/firmware" -B "$SDKWT/build/ota-app" -DCMAKE_BUILD_TYPE=Debug -DGADGET_WITH_LVGL=ON -DGADGET_BUILD_TESTS=OFF -DGADGET_SIM_VERSION=1.0.0
  cmake --build "$SDKWT/build/ota-app" -j10 --target gadget-sim
  find "$SDKWT/build/ota-app" -type f -name gadget-sim -perm -u+x | head -1 > /private/tmp/omb-ota-app.sim; "$(cat /private/tmp/omb-ota-app.sim)" --version
  ~/.claude/skills/test-locally/build-side-by-side.sh "$WT"
  ```

  Quit OpenMausBot or turn its Remote access off first (both companions listen on :8810), then start OMB2 as an isolated instance with the two variables, and leave it running:

  ```bash
  env -u ELECTRON_RUN_AS_NODE OMB_GADGET_MANIFEST_URL=http://127.0.0.1:18900/manifest.json OMB_GADGET_TRUST_TEST_KEY=1 \
    OMB_DATA_DIR="$HOME/.openmausbot-test" OMB_COMPANION_DIR="$HOME/.openmausbot-companion-test" \
    /Applications/OMB2.app/Contents/MacOS/OMB2
  ```

  In OMB2, turn on Remote access and click **Pair a gadget**. In a third terminal, pair the simulator with the six digits: `"$(cat /private/tmp/omb-ota-app.sim)" --board amoled-175c --name ota-check --host 127.0.0.1:8810 --pair <the six digits>`. Expected on its row in Settings → Remote access:
  1. "Update available", with a button "Update to 1.0.1";
  2. after the click, "Updating… N%", rising about once a second (every 10 s if Task 10 Step 5 was skipped);
  3. "Updated to 1.0.1", and the row's firmware line reads 1.0.1;
  4. a simulator built without `-DGADGET_SIM_VERSION` (so `0.0.0-dev`) and paired the same way shows "Custom build" and no button.

  Afterwards quit OMB2, stop the dev release, reopen OpenMausBot, and remove the worktrees (the branches stay): `rm -rf /Users/omkar/Desktop/openmaus/openmausbot-gadget-sdk-p4b/build/ota-app && git -C /Users/omkar/Desktop/openmaus/openmausbot-gadget-sdk worktree remove /Users/omkar/Desktop/openmaus/openmausbot-gadget-sdk-p4b`, and, only if Task 1 created it, `git -C /Users/omkar/Desktop/openmaus/OpenGrokBot worktree remove /Users/omkar/Desktop/openmaus/OpenGrokBot-gadget-ota`.

**Hardware checklist** (P4b's on-device checks; for P2c's `docs/hardware-checklist.md` or for Omkar to run). Use one Waveshare ESP32-S3-Touch-AMOLED-1.75C, then repeat steps 1–4 on the lcd-154 and the devkit.

1. **Signed update over Wi-Fi.** Build the board with `-D PROJECT_VER=1.0.0` and `CONFIG_GADGET_TEST_KEYS=y` (a local `sdkconfig` override; release builds keep it off), flash it over USB and pair it. Build the same board again at `PROJECT_VER=1.0.1` and serve `build/<board>/openmausbot-gadget.bin` with `dev-release.ts --board <board> --version 1.0.1`. Start the app with the two variables and click Update. Expected:
   - progress moves;
   - the gadget restarts and its boot log shows 1.0.1;
   - the row reads "Updated to 1.0.1";
   - 11 minutes later, past the 10-minute restart timeout, the row has no "Update failed" line: "Updated to 1.0.1" has gone (it shows for 10 minutes) and the firmware line still reads 1.0.1. The ESP32's restart sends no FIN, so this is the restart-order case of Review Focus 6;
   - the pairing survives.
2. **Power loss while sending.** Unplug at about 50%. Expected:
   - the row shows "Update failed: The gadget stopped responding during the update." (or "…disconnected during the update." if it was replugged within 35 s);
   - after replugging, the gadget boots 1.0.0;
   - a new Update completes.
3. **Power loss while restarting.** Unplug within 2 s after the progress reaches 100%. Expected:
   - after replugging, the gadget either reaches 1.0.1 with `fw.installed`, or boots 1.0.0;
   - in the second case the row says it "came back on 1.0.0";
   - in either case, 11 minutes later the row has not turned into "has not come back": it still says "came back on 1.0.0", or it has no "Update failed" line and the firmware line reads 1.0.1.
4. **Probation rollback.** Turn Remote access off the moment the progress reaches 100%, wait 6 minutes, then turn it on. Expected:
   - the gadget is back on 1.0.0, because it could not reach `ready` within 5 minutes;
   - the row shows Update available again.
5. **Release key only.** Run step 1 with a board built without `CONFIG_GADGET_TEST_KEYS`. Expected: the gadget answers `fw.fail unknown_key`, and the row reads "Update failed: The gadget does not trust the key this update is signed with."
6. **Official path, after Omkar's first two releases.** A board flashed from release N shows Update available for release N+1 from GitHub, with no variables set, and updates.

Not verifiable without hardware: the real flash write speed and timing; the bootloader's slot switch and rollback; power-loss behaviour; the release-key-only firmware refusing t1 on a device; and the ESP32's TCP flow control under a 64 KiB window.

## Contract deviations

Neither item renames or retypes anything pinned. Each is an edit outside P4b's pinned points, so each waits for review (contract §0 item 2). The plan runs without them: the step that needs each one first greps the contract and falls back when the item is not listed yet.

1. **`src/components/PhoneSetupFlow.tsx`: `shouldPoll` and its import.** Contract §5.2 pins P4b's edits to this file to the firmware types (`gadgetFirmware`, with §3.18's `firmwareRefusal` and the two bridge methods). Task 10 Step 5 also makes `shouldPoll` (origin/main `:772`) true while `hasActiveGadgetUpdate(state)`, and imports `hasActiveGadgetUpdate` from `../lib/gadgets`. Without it the panel polls every 10 s, so "Updating… N%" moves in 10 s steps and "Updated to …" can show up to 10 s late. **Proposed change:** in §5.2, the row for `CompanionSection.tsx`, `PhoneSetupFlow.tsx`, … adds to P4b's pinned points "`shouldPoll` also true while `hasActiveGadgetUpdate(state)`, with that import from `../lib/gadgets`". Until then Task 10 Step 5 is skipped.
2. **`tools/release/dev-release.test.ts` in the SDK.** Contract §5.1 gives `tools/release/**` to P2d, except `dev-release.ts`, so a P4b test there changes ownership; it is not a private file. It also runs in no CI job: contract §1.6 fixes the job ids, and the root `package.json` belongs to P1. **Proposed change:** in §5.1, "`tools/release/dev-release.ts`, `tools/release/dev-release.test.ts` | P4b (SDK branch `p4b-ota`)", plus a decision on CI: P2d's `site` job runs it, or P1 adds a `test:release` script that a job runs. Until then the test lives in `/private/tmp/p4b-dev-release-test`, a scratch mirror of `protocol/lib`, `keys/test-t1.*` and `tools/release` (Task 12), and is not committed.

## Self-review

- **Spec coverage:** every row of "Spec coverage" names a task, and the out-of-scope items name their owner plans. The §10 rows for companion, Desktop UI, end to end and hardware map to Tasks 7–9, 11, 13 (companion level), 14 (the full app, a required manual check) and 14's hardware checklist.
- **Placeholder scan:** no TBD, TODO or "similar to"; every code step carries complete code, and every command block can be pasted into a fresh shell as it is (Task 13 reads `$E2E` back from `/private/tmp/omb-ota-e2e.current`). Five places are conditional, each with a concrete action rather than a gap: P3a's exact anchors (found by `grep -n` on named text), P3a's `CompanionSection` mocks (copied from P3a's own test), the `nextBinary` payload form (fall back to `decodeBinary`), and the two contract-deviation gates (a `grep` on the contract, with a stated fallback).
- **Type consistency:** `VerifiedImage` (Task 4) carries `keyId`, which Task 5 sends as `key_id`. `GadgetUpdateStatus`/`GadgetFirmwareState` match field for field across `firmware.ts`, `PhoneSetupFlow.tsx` and the tests. `startUpdate`'s refusal shape matches the control route, `companionFirmwareUpdate`'s `firmwareRefusal` and `CompanionState.firmwareRefusal`. `FwPhase` values are a subset of `GadgetUpdatePhase`. `updateCellState`'s kinds match `GadgetUpdateCell`'s branches.
- **Review Focus:** all six lines have tests in their owning tasks (listed under each line).

## Open questions

1. `RELEASE_KEYS` ships empty, so no official update can be offered until `r1` exists. Should `feat/gadget-ota` wait for `r1`, or merge empty and take the one-line `r1` commit later (the hand-off command)?
2. The panel polls every second while an update runs (Contract deviation 1), including the up-to-10-minute restart wait. Should the restart wait poll at 10 s instead?
3. Is a 10-minute restart timeout (twice the 5-minute probation) the right point to call an update failed?
4. When the gadget comes back on the new version but no `fw.installed` arrives within the restart timeout, the row says "The gadget came back on … but did not confirm the update." An image still running after its 5-minute probation has marked itself valid, so if that session is still online when the timer fires, the update most likely landed and only the `fw.installed` frame was lost. Should that case show "Updated to …" instead? The row corrects itself either way: the next connection on the new firmware clears the failed entry.
