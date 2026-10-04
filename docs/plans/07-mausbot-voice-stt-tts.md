# P3b — MausBot voice: Speech helper file mode, `/api/stt` and PCM speech for gadgets — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A paired gadget can talk and listen. Its utterance becomes text on the Mac, through Apple's recognizer in the Speech helper's new file mode, or through ElevenLabs Scribe v2 as the fallback. The bot's final reply comes back as paced 16 or 24 kHz PCM on one speech stream, with barge-in and a one-time "Voice is off" notice.

**Architecture:**
- **Harness.** A new route module, `server/routes/stt.ts` (registered with `ROUTES.push`), takes a 16 kHz WAV. It tries `server/stt/apple.ts` first, which launches `OpenMausBot Speech.app --file …` through LaunchServices, then `server/stt/scribe.ts`.
- **TTS.** `POST /api/tts/speak` gains `format: pcm_16000 | pcm_24000`. Each TTS provider is asked for PCM or WAV natively, and `server/tts/pcm.ts` walks the WAV chunks and resamples.
- **Companion.** P3b supplies the hub's `VoiceProvider` seam (contract §3.10):
  - `stt-client.ts`: PCM → WAV → `/api/stt`;
  - `speech.ts`: raw reply → `/api/tts/prepare` → `/api/tts/speak` PCM per utterance → one paced stream;
  - `audio.ts`: WAV packing and the real-time pacer.

  It then wires `voice: createGadgetVoice` into the hub P3a built.

**Tech Stack:**
- TypeScript on Node 24: the harness runs on `--experimental-strip-types`; the companion is dependency-free `tsc` output.
- vitest 4, and node:test for Electron.
- Swift 6 with the Speech framework, built with `swiftc -target <arch>-apple-macos12`.
- pnpm 10.33.0. No new dependencies.

**Spec:** `/Users/omkar/Desktop/openmaus/openmausbot-gadget-sdk/docs/specs/2026-10-04-openmausbot-gadget-design.md`. This plan implements v1.1 §6.3, §6.2 "Speech" and the voice rows of the §6.2 table, §6.5 item 1, and the STT/TTS parts of §10.
- **Binding contract:** `/Users/omkar/Desktop/openmaus/openmausbot-gadget-sdk/docs/plans/00-interfaces.md`, sections §0, §1.3, §1.5, §3.1, §3.7, §3.10, §3.12, §3.14, §3.19 and §5.2.
- **Amendments** (folded into v1.1): A11, A12, A29 (raw reply text to `/api/tts/prepare`) and A30.
- **Research** (scratchpad `research/`): R1 §1a–1d, R3 (TTS, STT, routes, tests) and R9 (speaker rate; shape vs speakable).

## Global Constraints

**Workspace and publishing**
- **Node and pnpm.** Every app command runs in the worktree with Node 24 first: `export PATH="$HOME/.nvm/versions/node/v24.14.1/bin:$PATH"`. `pnpm --version` must print `10.33.0`. If it does not, use `corepack pnpm@10.33.0 <args>` wherever this plan says `pnpm <args>` (contract §0 item 5).
- **Hands off the main checkout.** Never checkout, stash, reset, edit or fetch in `/Users/omkar/Desktop/openmaus/OpenGrokBot`; another session owns it. Read it only with `git -C /Users/omkar/Desktop/openmaus/OpenGrokBot show origin/main:<path>`. All work happens in the worktree from Task 1.
- **Never in P3a's worktree by accident.** P3b never creates `/Users/omkar/Desktop/openmaus/OpenGrokBot-gadget` (P3a's path, contract §1.3). Every command block starts with the line from Task 1 Step 1 that changes into whichever worktree holds `feat/gadget-voice` and stops anywhere else.
- **No publishing.** No `git fetch`, no push, no PR, no release. Omkar publishes.
- **Original-work rule** (spec §11). Nothing from third-party gadget SDKs or voice-assistant firmware projects: no code, no text, no references. Use vendor sources only: Apple's Speech framework, and the ElevenLabs, Fish Audio and xAI API references.

**Code rules**
- **Companion code.** `companion/src/gadget/*` uses Node built-ins and relative `.ts` imports only. It cannot import `shared/` or `server/`, and it gains no dependencies (contract §3.1).
- **Harness routes.**
  - New routes are modules registered with `ROUTES.push`. `server/index.ts` gains no path guard.
  - The ratchet counts stay at `'path === "/'` 164, `path.match(` 88, `path.startsWith(` 11, `.exec(path)` 18, `.test(path)` 1 and `.includes(path)` 3. Comments count too, so never write those needles in a comment.
  - Type-only imports use `import type`, because the harness runs on type stripping. No enums, no parameter properties.
- **No MP3 decoder and no new harness dependency** (A12). Every provider is asked for PCM or WAV natively. A voice that can only produce MP3 leaves the gadget text-only.
- **PCM content type.** Speech PCM goes out as exactly `audio/pcm;rate=<rate>;channels=1;bits=16;endian=little`, with a raw PCM16LE mono body. Never `audio/L16`.
- **Names and signatures** follow contract §3.14 exactly. Additions are optional parameters and private files only (see "Contract deviations" and "Contract notes" at the end). P3b edits no P3a-owned file except the one pinned line in `companion/src/index.ts` (contract §3.12).

**Copy** (exact, Latin-1)
- **STT done reasons** must stay ≤ 159 bytes, because the gadget keeps `UI_REASON_MAX` = 160 bytes including the NUL.
  - `STT_UNAVAILABLE_MESSAGE` = `Speech-to-text isn't set up. On a Mac, allow Speech Recognition for OpenMausBot (try dictation once), or add an ElevenLabs key in a bot's voice settings.`
  - `STT_KEY_FORBIDDEN_MESSAGE` = `Your ElevenLabs key can't use Speech to Text. Turn on its Speech to Text permission, or use a key without restrictions.`
  - An empty transcript ends the turn with `Didn't catch that` (P3a's session).
- **Voice-off card:** `{id: "notice-voice", title: "Voice is off", body: "Add a voice in this bot's voice settings to hear replies.", ttl_s: 8}`.
- There is no "Settings → Voice" anywhere in the app.

**Speech-to-text**
- **Scribe v2.** `POST {api}/speech-to-text?enable_logging=false` (as contract §3.14 pins it), with `model_id=scribe_v2`, `file_format=pcm_s16le_16` and `tag_audio_events=false`. The body is raw PCM16LE, 16 kHz, mono. The key is `voiceCredential(cfg.tts?.key)`, and the route takes no bot id. Whether to keep `enable_logging=false` is deviation D-P3b-1, a STOP checkpoint at the start of Task 5: until Omkar answers, the request is exactly the pinned one, sent once.
- **The Speech helper's file mode never prompts.** That path calls `authorizationStatus()` only, never `requestAuthorization()`. The helper is launched only through `/usr/bin/open -n -g -W`. It stays universal (arm64 + x86_64) at the macOS 12 floor, and every path passed to it is absolute.

**Timing and limits**
- **Time budget** (contract §3.7):
  - `/api/stt`: the helper runs with `--timeout-ms 20000` plus a 5 s harness grace, then Scribe gets 30 s, so at most 55 s. That fits inside the hub's 60 s `raw()` call.
  - `/api/tts/speak`: 30 s `raw()`. `/api/tts/prepare`: 10 s `json()`.
- **Limits:**
  - `/api/stt` bodies are at most 2 MiB (`STT_MAX_BYTES`); beyond that the route answers 413 `too_large`. An utterance is mono, 16-bit, 16 kHz and at most 60 s.
  - Speak text is at most 500 characters.
  - Speaker frames are exactly 40 ms (contract §2.12): 1280 bytes at 16 kHz, 1920 at 24 kHz. A reply's last frame is padded with silence. Frames are never more than 500 ms ahead of real time.
  - Queued speech (a spoken push behind a reply) starts 300 ms (`SPEECH_GAP_MS`) after the earlier stream has played out on the host's clock, so the gadget can finish its tail.
  - Speaker rates are 16000 or 24000 only. Anything else means no speaker, text only.

**Housekeeping**
- **No renderer strings.** P3b adds no `src/locales/*.json` keys, and `pnpm i18n:check` stays green.
- **Commits** end with the trailer `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`, passed as a second `-m`.

## Scope

| Requirement (spec section, amendment, contract) | Task |
|---|---|
| §6.3 `server/tts/pcm.ts`: walk the RIFF chunks (`say` writes JUNK and FLLR), resample PCM16 | 1 |
| §6.3 TTS provider table and A12: ElevenLabs `pcm_*`, Fish `wav` + `sample_rate`, xAI `wav` + `sample_rate`, `say --data-format=LEI16@<rate>`, Chatterbox WAV | 2 |
| §6.3 `/api/tts/speak` `format` (`mp3`, `pcm_16000`, `pcm_24000`); an MP3-only voice gets 415 `pcm_unsupported`; the PCM content type | 3 |
| §6.3 and A11 helper file mode: `--file`, `--timeout-ms`, `--stop-file`, `SFSpeechURLRecognitionRequest`, on-device recognition, punctuation (13+), one NDJSON line, `authorizationStatus()` without a prompt | 4 |
| §6.3 and A11 Scribe v2 fallback, the pinned request sent once; 401/403 named as the key's Speech to Text permission; D-P3b-1 (`enable_logging=false`) as a STOP checkpoint, with the code for each of Omkar's possible answers | 5 |
| §6.3 and A30 `/api/stt` module: raw body read with a content-length check, 2 MiB cap and 413, 60 s limit, `{text, provider}`; Apple → Scribe → 409 `stt_unavailable`; empty transcript or no-speech → `{text: ""}`; a crashed helper is `stt_failed`, not "isn't set up" | 6 |
| §6.3 and §6.5 item 1 helper resolution (`OMB_SPEECH_HELPER_PATH` → `OMB_RESOURCES_PATH` → dev path); `open -n -g -W … --args --file --stop-file --timeout-ms`; reading the NDJSON from the stdout file | 7 |
| A30 and contract §3.14: `ROUTES.push`, `apple` undefined off macOS or without a helper bundle (resolved per request), the companion `ALLOWED` entry, the `CLIENT_ALLOW` entry, `voiceCredential(cfg.tts?.key)`, the time budget | 8 |
| §6.1 `audio.ts`, §4.4 and contract §2.12: WAV packing; real-time pacing in frames of exactly 40 ms (the last one padded with silence), ≤ 0.5 s ahead | 9 |
| §6.2 `voice.end` row and contract §3.14: WAV → `/api/stt`; 409 → set-up copy; 502 forbidden key → the harness's message | 10 |
| §6.2 Speech items 1–8: raw text to prepare (A29), one PCM request per utterance, the first playing while the rest synthesize, one stream per reply, the bot's voice, 16000/24000 only, pushes queued after reply speech (plus a 300 ms gap for the gadget's buffer), `speak.stop` on stop or a new turn, first 409 → one notice card and speech off, 415 → text only | 11 |
| Contract §3.12 hub wiring (`voice: createGadgetVoice`) and the whole chain: voice.begin/frames/end → `/api/stt` → `heard` → message → final reply → speech. Barge-in three ways, each with `speak.stop` and the old turn's `done` before the new turn's first message: `say` mid-turn, TALK (`stop` then `voice.begin`) mid-turn, and TALK after `done` while speech still plays (§6.2 Speech item 6). Text-only for no speaker or a 22.05 kHz speaker (Speech item 4); a spoken push without `turn` (item 5); Voice is off once per session (item 7) | 12 |
| §10 Harness row (STT and TTS) and Companion row (barge-in `speak.stop`, through Task 12's three barge-in cases); whole-branch checks against a baseline; what needs hardware | 13 |

**Out of scope, owned by another plan:**
- **P3a** (`feat/gadget-hub`) owns `session.ts` and its §6.2 mapping:
  - buffering mic frames per `voice.begin` stream, "Unsupported mic rate", the 60 s cap on the gadget stream, `voice.drop` and disconnects;
  - the five Stop cases, folding `heard`, the `sendId` and the message send, `reply` and `done`;
  - calling `SpeechOut.replyFinal`, `post`, `stop` and `close`;
  - computing `SpeechContext.rate` from `caps.speaker.rate` (A13).

  P3a also owns the hub, `ws.ts`, `harness-client.ts`, `shape.ts`, `textOnlyVoice`, the test helpers, the desktop UI and the control token. P3b consumes them through contract §3.7, §3.8, §3.10, §3.11 and §3.19, and edits none of them.
- **P4a** owns the bot tools, `/api/internal/gadgets*`, the control routes and the presence notice. **P4b** owns OTA.
- **P2a, P2b and P2c** own the gadget's mic capture, jitter buffer and speaker playback, the speaking-state art, and the per-board hardware checklist (`docs/hardware-checklist.md` in the SDK). P3b has no firmware work: no C, no CMake, no ESP-IDF.
- **Phones and the desktop renderer:** no changes (spec §6.4). P3b adds no Electron IPC, because the packaged harness already gets `OMB_RESOURCES_PATH`.

## Review Focus

The five uncovered inputs most likely to bite a person, each with the test that now pins it in its owning task:

1. **A button tap, or a recording of silence.** Someone presses TALK and says nothing, or lets go at once. **Expected:** "Didn't catch that", with no paid provider call and no cloud round trip, while quiet real speech still reaches the recognizer. **Tests:** Task 6, "answers a silent or too-short WAV with empty text, without calling a provider" and "sends a quiet clip that peaks at exactly 200 to the provider…"; Task 12, "ends the turn with Didn't catch that on an empty transcript". Task 13 adds the per-board mic-level check.
2. **Barge-in while speech is playing, or still being synthesized.** The common case is TALK while the reply is still speaking, after its `done` has arrived, because speech outlives `done`. **Expected:** `speak.stop` for the playing stream and the old turn's `done` before the new turn's messages, and no stale frames. The old stream never gets `speak.end`, and late TTS audio from the old turn is dropped. **Tests:**
   - The ordering test is Task 11's "barge-in: stop() sends speak.stop for the playing stream before it returns…": `stop()` sends `speak.stop` synchronously, so P3a's session can then send `done` and the new turn's messages in that order.
   - Task 11, "barge-in while synthesizing…" and "barge-in during the gap…".
   - Task 12, through the real hub, with the new turn's speech-to-text held on a deferred so nothing of the new turn can arrive early: "barge-in by TALK: stop, then voice.begin, while the old turn is in flight" (`speak.stop` and `done {stopped}` arrive while the STT is held) and "barge-in after done: a new voice.begin stops speech that outlived its turn" (`speak.stop` while the STT is held, and no `speak.end` for the old stream). Also "barge-in by say…".
3. **A Mac where the helper was never allowed Speech Recognition, or where the helper bundle is missing** (a fresh dev checkout, or Windows). **Expected:** never a permission dialog. Scribe takes over when a key exists; otherwise the turn ends with the exact set-up copy, even for a silent clip. **Tests:** Task 4's node test (file mode never calls `requestAuthorization`); Task 6, "falls back to Scribe v2…", "answers 409 stt_unavailable…" and "answers 409 for a silent clip too…"; Task 7, "passes the helper's known errors through…" and the resolution tests; Task 8's registration check (`apple` is undefined without a bundle).
4. **Provider audio the gadget cannot play as it is:** `say`'s JUNK and FLLR chunks, a streamed WAV size, a Chatterbox server that answers with MP3, or an older harness that answers a `pcm_*` request with MP3. **Expected:** parsed correctly, or the reply stays text-only. Never noise. **Tests:** Task 1 (JUNK/FLLR, streamed sizes, MP3 and `audio/pcm` without a rate refused); Task 2, the Chatterbox case; Task 3, 415; Task 11, "never plays a 200 that is not PCM at its rate".
5. **A full 60 s utterance.** Its 1,920,044 bytes are past `readBody`'s 1 MB limit. **Expected:** accepted. Only a body beyond 2 MiB is answered 413, whether the size comes from `content-length` or from a chunked upload. **Tests:** Task 6, "accepts a full 60 s utterance…" and "answers 413 above the cap…".

## What was verified while writing this plan

All of this ran on this Mac (macOS 26.6.2, Node 24.14.1, vitest 4.1.11, TypeScript 5.9, oxlint 1.80.0, Swift 6.4), in `/private/tmp/claude-501/p3b-verify`. Every TS file and test in Tasks 1–3, 5–11 and the wiring checks of Task 8 is the exact text below. The server files they edit were copied from origin/main 6dd4403. P3a's modules were replaced by stand-ins generated from contract §3.2, §3.3, §3.5, §3.7, §3.9, §3.10, §3.11 and §3.19.

**Tests, typecheck, lint** (after the review revision below)
- 87 new P3b tests pass: every P3b test file except Task 12's hub test and Task 8's one new `routes.test.ts` row.
- `tsc --strict` is clean with the server tsconfig's flags plus `erasableSyntaxOnly`.
- `oxlint --deny-warnings` 1.80.0 is clean.
- The Task 12 hub test (11 cases) type-checks against the stand-ins but needs P3a to run.
- The existing `server/tts/tts.test.ts` and `grok.test.ts` still pass against the edited providers: 44 of 45. The 45th needs the real `config.ts` (`loadConfig`), which the scratch copy does not have.

**The edits**
- A script applied every old→new edit in this plan to the origin/main files: each "old" block occurs exactly once, and the result equals the tested file.
- The `server/index.ts` edits leave the ratchet counts unchanged: 164, 88, 11, 18, 1 and 3.

**The Swift helper**
- File mode compiles for arm64 and x86_64 at `-target …-apple-macos12`.
- A bundle with a never-granted bundle id was launched with `open -n -g -W -o … --args --file <absolute wav> --stop-file … --timeout-ms 20000`. It printed `{"error":"speech-not-authorized"}` in about 80 ms, with no dialog.
- A relative `--file` printed `file-unreadable`, because LaunchServices starts the helper in `/`.
- `open -W` sometimes prints "Unable to block on application" and returns before the helper's line can be read. That is why `apple.ts` waits for the line, not for `open`.
- A main-run-loop `Timer` keeps `RunLoop.main.run()` running, and main-queue blocks are still serviced (checked with a 10-line program).
- Task 4's node test fails 3 of 4 on the unmodified helper and passes 4 of 4 on the new one.

**Audio formats and vendor docs**
- `say -o x.wav --data-format=LEI16@16000` writes `JUNK(28) fmt(16: PCM, mono, 16000 Hz, 16-bit) FLLR(4008) data`.
- The vendor docs were read for:
  - **ElevenLabs Speech to Text:** `file_format` `pcm_s16le_16`, a 100 ms minimum; `enable_logging=false` is zero-retention mode, which "may only be used by enterprise customers".
  - **xAI TTS:** `output_format.codec` `wav` or `pcm`; `sample_rate` 16000 and 24000 allowed; WAV comes back as `audio/wav`.
  - **Fish TTS:** `format` `wav` or `pcm`, 16-bit mono at 16 or 24 kHz.

**Re-verified after review 1 and review 2** (in a copy of the same scratch tree):
- The padded last frame, the 300 ms gap, the cycling stream ids in the fake session, `audio/pcm` without a rate, the route's `apple` read once with `helper-failed` → 502, the new route cases, and the pinned Scribe request each pass as written below.
- D-P3b-1: answers A and B in Task 5 Step 7 were each applied to the pinned files, and `scribe.test.ts` plus `stt.test.ts` pass after each (7 and 15 tests).
- `server/index.ts` with the getter registration type-checks, and the ratchet counts stay 164, 88, 11, 18, 1 and 3.
- `resolveSpeechHelper`'s expectations hold under `path.win32` semantics, and the Electron node test's three source checks pass on a CRLF copy of the helper.
- The worktree line in Task 1 Step 1 was run in bash and zsh against a scratch repo whose worktree path has a space: it lands in the `feat/gadget-voice` worktree, and with no such worktree it prints `not in the feat/gadget-voice worktree` and exits 1.

**Not verifiable here.** Task 13 lists these:
- the Speech permission when the packaged harness launches the helper;
- the recognizer's error for a silent WAV on a Mac that has granted Speech Recognition;
- live ElevenLabs, Fish and xAI calls;
- anything on gadget hardware.

## File Structure

| File | Responsibility | Task |
|---|---|---|
| `server/tts/pcm.ts` (new) | WAV chunk walker, PCM16 parse/downmix/resample, provider audio → PCM16LE, the PCM content type | 1 |
| `server/tts/pcm.test.ts` (new) | WAV layouts (say, streamed, extensible, stereo), resampling, refusals (MP3, `audio/pcm` without a rate) | 1 |
| `server/tts/index.ts` (edit) | `type Audio` re-export (T1); `SpeechFormat`, `SPEECH_FORMATS`, `pcmRate`, `speak(…, format)` (T2) | 1, 2 |
| `server/tts/elevenlabs.ts`, `fish.ts`, `grok.ts`, `system-voices.ts` (edit) | ask each provider for PCM or WAV at the gadget rate | 2 |
| `server/tts/pcm-format.test.ts` (new) | every provider asked natively, against a local stub | 2 |
| `server/tts/speak-reply.ts` (new, private) | the `/api/tts/speak` answer: validation, mp3 unchanged, PCM, 409/415/502 | 3 |
| `server/tts/speak-reply.test.ts` (new) | the answer's rules and its wiring in `server/index.ts` | 3 |
| `server/index.ts` (edit) | `/api/tts/speak` block calls `ttsSpeakReply` (T3); STT imports and `ROUTES.push(createSttRoutes…)` (T8) | 3, 8 |
| `electron/resources/speech-helper.swift` (edit) | `--file`/`--timeout-ms` file mode that never prompts | 4 |
| `electron/speech-helper-file-mode.node-test.mjs` (new) | pins the never-prompt rule; type-checks both slices on macOS | 4 |
| `server/stt/scribe.ts` (new) | ElevenLabs Scribe v2 request (pinned, sent once; D-P3b-1 decides the query) and its refusals | 5 |
| `server/stt/scribe.test.ts` (new) | Scribe fields, header, one request per refusal, forbidden, deadline | 5 |
| `server/routes/stt.ts` (new) | `POST /api/stt`: body cap, WAV check, silence gate, Apple → Scribe → 409 or 502, copy | 6 |
| `server/routes/stt.test.ts` (new) | the route on a real HTTP server, including the silence gate's boundary; registration check added in T8 | 6, 8 |
| `server/stt/apple.ts` (new) | Speech helper resolution and file-mode launch through LaunchServices | 7 |
| `server/stt/apple.test.ts` (new) | launcher argv, NDJSON mapping, races, timeout, resolution order (OS-neutral paths) | 7 |
| `server/request-auth.ts`, `server/request-auth.test.ts` (edit) | `CLIENT_ALLOW` gains `POST /api/stt` | 8 |
| `companion/src/routes.ts`, `companion/test/routes.test.ts` (edit) | `ALLOWED` gains `POST /api/stt` | 8 |
| `companion/src/gadget/audio.ts` (new) | `wavFromPcm16`, `createSpeechPacer` | 9 |
| `companion/test/gadget/audio.test.ts` (new) | WAV header; pacing on a fake clock; whole 40 ms frames | 9 |
| `companion/src/gadget/stt-client.ts` (new) | `createSttClient`, `STT_UNAVAILABLE_COPY` | 10 |
| `companion/test/gadget/helpers/voice-fakes.ts` (new, private) | recording session (stream ids cycle as contract §2.12 says) and scriptable harness client for voice unit tests | 10 |
| `companion/test/gadget/stt-client.test.ts` (new) | STT client mapping | 10 |
| `companion/src/gadget/speech.ts` (new) | `createSpeechOut`, `createGadgetVoice`, `VOICE_OFF_CARD` | 11 |
| `companion/test/gadget/speech.test.ts` (new) | spoken replies, pushes and the gap before them, barge-in, voice-off, 415, rates | 11 |
| `companion/src/index.ts` (edit, the §3.12 P3b point) | `voice: createGadgetVoice` | 12 |
| `companion/test/gadget/voice-hub.test.ts` (new) | a whole voice turn through the real hub: three barge-in paths, text-only speakers, a spoken push, Voice is off once; wiring check | 12 |

---

### Task 1: Workspace, and the WAV/PCM toolkit (`server/tts/pcm.ts`)

**Files:**
- Create: `server/tts/pcm.ts`
- Create: `server/tts/pcm.test.ts`
- Modify: `server/tts/index.ts:164` (origin/main): re-export `type Audio`

**Interfaces:**
- Consumes: `Audio` = `{ bytes: Uint8Array; mime: string }`, declared in `server/tts/elevenlabs.ts:30-33` (existing).
- Produces (`server/tts/pcm.ts`), used by Tasks 2, 3 and 6:
  - `interface Pcm16 { rate: number; samples: Int16Array }`
  - `interface WavInfo { format; channels; rate; bits; data: Uint8Array }`
  - `readWav(bytes: Uint8Array): WavInfo`: throws `Error` for a file that is not a WAV.
  - `parseWav(bytes: Uint8Array): Pcm16`: throws `PcmUnsupported` for a valid WAV that is not PCM16.
  - `resamplePcm16(pcm: Pcm16, rate: number): Pcm16`
  - `toPcm16le(audio: Audio, rate: 16000 | 24000): Buffer`: `audio/pcm` must carry its `rate=` (else `PcmUnsupported`); WAV is parsed and resampled; anything else throws `PcmUnsupported`.
  - `class PcmUnsupported extends Error`
  - `pcmMime(rate: 16000 | 24000): string`
  - `server/tts/index.ts` re-exports `type Audio`.

- [ ] **Step 1: Choose the worktree and branch**

P3b works on `feat/gadget-voice`, normally in its own worktree at `/Users/omkar/Desktop/openmaus/OpenGrokBot-gadget-voice`. The shared path `/Users/omkar/Desktop/openmaus/OpenGrokBot-gadget` belongs to P3a: contract §1.3 has P3a create it with `git worktree add -b feat/gadget-hub`, so this plan never creates that path, and never switches its branch while P3a may still be running. Look first:

```bash
export PATH="$HOME/.nvm/versions/node/v24.14.1/bin:$PATH"
APP=/Users/omkar/Desktop/openmaus/OpenGrokBot
git -C "$APP" worktree list
git -C "$APP" rev-parse --verify --quiet refs/heads/feat/gadget-hub && echo "HUB=yes" || echo "HUB=no"
git -C "$APP" rev-parse --verify --quiet refs/heads/feat/gadget-voice && echo "VOICE=yes" || echo "VOICE=no"
git -C "$APP" rev-parse origin/main
```

Expected: the last line prints `6dd4403d8fbbbd5c17169724cb2a529f11d7543e`. If it prints anything else, another session has fetched. Do not fetch or rebase: use `6dd4403d8fbbbd5c17169724cb2a529f11d7543e` wherever this step says `origin/main`. The line numbers in this plan come from 6dd4403 and are hints only; every edit matches by its exact old text.

Then take the first case that applies:

1. **A worktree already has `feat/gadget-voice` checked out** (a resumed run; `git worktree list` shows `[feat/gadget-voice]`). Use it as it is.
2. **VOICE=yes, but no worktree has it checked out** (its worktree was removed). Check it out again in P3b's own worktree:

   ```bash
   git -C /Users/omkar/Desktop/openmaus/OpenGrokBot worktree add /Users/omkar/Desktop/openmaus/OpenGrokBot-gadget-voice feat/gadget-voice
   ```

3. **Whoever dispatched this plan (the orchestrator, or Omkar) has confirmed that P3a is finished.** If `/Users/omkar/Desktop/openmaus/OpenGrokBot-gadget` is then on `feat/gadget-hub` and `git -C /Users/omkar/Desktop/openmaus/OpenGrokBot-gadget status --porcelain` prints nothing, the shared worktree is idle, and contract §1.3 has the app plans take turns in it:

   ```bash
   git -C /Users/omkar/Desktop/openmaus/OpenGrokBot-gadget switch -c feat/gadget-voice feat/gadget-hub
   ```

   A clean tree on `feat/gadget-hub` is not enough on its own. P3a commits after every task, so its tree is clean between tasks, and switching it then would put a running P3a's next commits on `feat/gadget-voice`. Without that confirmation, use case 4.
4. **Every other situation, including HUB=no.** Create P3b's own worktree. When HUB=yes, the base is `feat/gadget-hub`:

   ```bash
   git -C /Users/omkar/Desktop/openmaus/OpenGrokBot worktree add -b feat/gadget-voice /Users/omkar/Desktop/openmaus/OpenGrokBot-gadget-voice feat/gadget-hub
   ```

   When HUB=no (P3a has not created its branch yet), the base is `origin/main`. This is contract deviation D-P3b-2; Task 9 Step 1 rebases the branch onto `feat/gadget-hub`:

   ```bash
   git -C /Users/omkar/Desktop/openmaus/OpenGrokBot worktree add -b feat/gadget-voice /Users/omkar/Desktop/openmaus/OpenGrokBot-gadget-voice origin/main
   ```

   If `/Users/omkar/Desktop/openmaus/OpenGrokBot-gadget-voice` already exists with another branch on it, stop and report it.

Every later command block in this plan starts with the line below. It changes into whichever worktree holds `feat/gadget-voice`, wherever that is, and refuses to go on anywhere else. That way no command needs editing by hand, and a subagent that runs a single task never edits or commits in P3a's or another plan's worktree. Run it now:

```bash
cd "$(git -C /Users/omkar/Desktop/openmaus/OpenGrokBot worktree list --porcelain | awk '/^worktree /{w=substr($0,10)} $0=="branch refs/heads/feat/gadget-voice"{print w}')" && test "$(git branch --show-current)" = feat/gadget-voice || { echo 'not in the feat/gadget-voice worktree'; exit 1; }
git branch --show-current
```

Expected: `feat/gadget-voice`.

When the base is `origin/main`, Tasks 1–8 still work, because they touch only the harness, the helper and the allowlists. Task 9 Step 1 moves the branch onto `feat/gadget-hub` before any companion work.

- [ ] **Step 2: Install and take a baseline**

```bash
cd "$(git -C /Users/omkar/Desktop/openmaus/OpenGrokBot worktree list --porcelain | awk '/^worktree /{w=substr($0,10)} $0=="branch refs/heads/feat/gadget-voice"{print w}')" && test "$(git branch --show-current)" = feat/gadget-voice || { echo 'not in the feat/gadget-voice worktree'; exit 1; }
export PATH="$HOME/.nvm/versions/node/v24.14.1/bin:$PATH"
pnpm --version
pnpm install --frozen-lockfile
pnpm exec vitest run server/tts scripts/testing/index-route-ratchet.test.ts server/request-auth.test.ts companion/test/routes.test.ts
pnpm exec vitest run 2>&1 | tail -8 > /private/tmp/omb-p3b-baseline.txt; pnpm test:electron 2>&1 | tail -8 >> /private/tmp/omb-p3b-baseline.txt
cat /private/tmp/omb-p3b-baseline.txt
```

Expected:
- `pnpm --version` prints `10.33.0` (otherwise switch to `corepack pnpm@10.33.0` as Global Constraints say).
- The install finishes, and the first vitest run (the files this plan touches) reports every test file passed with 0 failed. If it already fails, stop and report it; do not fix unrelated tests.
- The full `vitest run` takes several minutes. `/private/tmp/omb-p3b-baseline.txt` then holds the last lines of the whole vitest suite and of `pnpm test:electron` on this Mac before any P3b change. Task 13 Step 2 compares against it, so a failure that already exists here is not blamed on P3b. Note any failures it lists; do not fix them.

- [ ] **Step 3: Write the failing test**

Create `server/tts/pcm.test.ts`:

```ts
// The WAV walker and resampler behind gadget speech (spec §6.3). Fixtures are
// built here byte by byte, laid out the way real writers lay them out: macOS
// `say --data-format=LEI16@16000` writes RIFF/WAVE, JUNK(28), fmt(16),
// FLLR(4008), data — checked on macOS 26.6 while writing this plan.
import { describe, expect, it } from "vitest";

import { PcmUnsupported, parseWav, pcmMime, readWav, resamplePcm16, toPcm16le } from "./pcm.ts";

function chunk(id: string, body: Buffer, declaredSize = body.byteLength): Buffer {
  const head = Buffer.alloc(8);
  head.write(id, 0, "ascii");
  head.writeUInt32LE(declaredSize, 4);
  return Buffer.concat([head, body, body.byteLength % 2 ? Buffer.alloc(1) : Buffer.alloc(0)]);
}

function fmtChunk(rate: number, channels = 1, bits = 16, format = 1): Buffer {
  const b = Buffer.alloc(16);
  b.writeUInt16LE(format, 0);
  b.writeUInt16LE(channels, 2);
  b.writeUInt32LE(rate, 4);
  b.writeUInt32LE((rate * channels * bits) / 8, 8);
  b.writeUInt16LE((channels * bits) / 8, 12);
  b.writeUInt16LE(bits, 14);
  return chunk("fmt ", b);
}

function wav(chunks: Buffer[]): Buffer {
  const body = Buffer.concat([Buffer.from("WAVE", "ascii"), ...chunks]);
  const head = Buffer.alloc(8);
  head.write("RIFF", 0, "ascii");
  head.writeUInt32LE(body.byteLength, 4);
  return Buffer.concat([head, body]);
}

function pcmBytes(samples: number[]): Buffer {
  const b = Buffer.alloc(samples.length * 2);
  samples.forEach((s, i) => b.writeInt16LE(s, i * 2));
  return b;
}

const sayLike = (rate: number, samples: number[]) =>
  wav([chunk("JUNK", Buffer.alloc(28)), fmtChunk(rate), chunk("FLLR", Buffer.alloc(4008)), chunk("data", pcmBytes(samples))]);

describe("readWav / parseWav", () => {
  it("walks past JUNK and FLLR to the data chunk, the way say writes it", () => {
    const pcm = parseWav(sayLike(16000, [1, -2, 300, -32768, 32767]));
    expect(pcm.rate).toBe(16000);
    expect(Array.from(pcm.samples)).toEqual([1, -2, 300, -32768, 32767]);
  });

  it("averages stereo to mono", () => {
    const stereo = wav([fmtChunk(24000, 2), chunk("data", pcmBytes([100, 300, -100, -300]))]);
    expect(Array.from(parseWav(stereo).samples)).toEqual([200, -200]);
  });

  it("reads a streamed data size (0xFFFFFFFF or 0) to the end of the file", () => {
    for (const declared of [0xffffffff, 0]) {
      const streamed = wav([fmtChunk(16000), chunk("data", pcmBytes([5, 6, 7]), declared)]);
      expect(Array.from(parseWav(streamed).samples)).toEqual([5, 6, 7]);
    }
  });

  it("resolves WAVE_FORMAT_EXTENSIBLE to its PCM sub-format", () => {
    const ext = Buffer.alloc(40);
    ext.writeUInt16LE(0xfffe, 0);
    ext.writeUInt16LE(1, 2);
    ext.writeUInt32LE(16000, 4);
    ext.writeUInt32LE(32000, 8);
    ext.writeUInt16LE(2, 12);
    ext.writeUInt16LE(16, 14);
    ext.writeUInt16LE(22, 16);
    ext.writeUInt16LE(1, 24); // KSDATAFORMAT_SUBTYPE_PCM starts with 0x0001
    const file = wav([chunk("fmt ", ext), chunk("data", pcmBytes([9]))]);
    expect(readWav(file).format).toBe(1);
    expect(Array.from(parseWav(file).samples)).toEqual([9]);
  });

  it("calls a valid WAV in another sample format unsupported, and a broken file an error", () => {
    expect(() => parseWav(wav([fmtChunk(16000, 1, 32, 3), chunk("data", Buffer.alloc(8))]))).toThrow(PcmUnsupported);
    expect(() => parseWav(wav([fmtChunk(16000, 1, 8), chunk("data", Buffer.alloc(8))]))).toThrow(PcmUnsupported);
    expect(() => parseWav(Buffer.from("ID3\u0004 not a wav at all"))).toThrow("not a WAV file");
    expect(() => parseWav(wav([chunk("data", pcmBytes([1]))]))).toThrow("no fmt chunk");
    expect(() => parseWav(wav([fmtChunk(16000)]))).toThrow("no data chunk");
    expect(() => parseWav(wav([fmtChunk(16000)]))).not.toThrow(PcmUnsupported);
  });
});

describe("resamplePcm16", () => {
  const sine = (rate: number, hz: number, seconds: number) => {
    const s = new Int16Array(Math.round(rate * seconds));
    for (let i = 0; i < s.length; i++) s[i] = Math.round(12000 * Math.sin((2 * Math.PI * hz * i) / rate));
    return { rate, samples: s };
  };
  const crossings = (s: Int16Array) => {
    let n = 0;
    for (let i = 1; i < s.length; i++) if ((s[i - 1]! < 0) !== (s[i]! < 0)) n++;
    return n;
  };

  it("keeps duration and pitch from say's 22.05 kHz to 16 kHz and 24 kHz", () => {
    const src = sine(22050, 440, 1);
    for (const rate of [16000, 24000]) {
      const out = resamplePcm16(src, rate);
      expect(out.rate).toBe(rate);
      expect(out.samples.length).toBe(rate);
      expect(Math.abs(crossings(out.samples) - 880)).toBeLessThanOrEqual(2);
    }
  });

  it("returns the input unchanged at the same rate", () => {
    const src = sine(16000, 300, 0.1);
    expect(resamplePcm16(src, 16000).samples).toBe(src.samples);
  });
});

describe("toPcm16le", () => {
  it("passes ElevenLabs raw PCM through, dropping a stray odd byte", () => {
    const raw = Buffer.concat([pcmBytes([1, 2, 3]), Buffer.from([0x7f])]);
    const out = toPcm16le({ bytes: raw, mime: "audio/pcm;rate=16000" }, 16000);
    expect(out).toEqual(pcmBytes([1, 2, 3]));
  });

  it("resamples raw PCM whose rate differs", () => {
    const out = toPcm16le({ bytes: pcmBytes(Array.from({ length: 240 }, () => 1000)), mime: "audio/pcm;rate=24000" }, 16000);
    expect(out.byteLength).toBe(160 * 2);
    expect(out.readInt16LE(0)).toBe(1000);
  });

  it("parses and resamples a WAV (say at 22.05 kHz, Chatterbox, Fish, xAI)", () => {
    const out = toPcm16le({ bytes: sayLike(22050, Array.from({ length: 2205 }, () => -500)), mime: "audio/wav" }, 16000);
    expect(out.byteLength).toBe(1600 * 2);
    expect(out.readInt16LE(100)).toBe(-500);
    expect(toPcm16le({ bytes: sayLike(16000, [42]), mime: "audio/x-wav" }, 16000)).toEqual(pcmBytes([42]));
  });

  it("refuses MP3, unknown audio and PCM without a rate as unsupported, never as a crash", () => {
    expect(() => toPcm16le({ bytes: Buffer.from([0xff, 0xfb, 0x90, 0x00]), mime: "audio/mpeg" }, 16000)).toThrow(PcmUnsupported);
    expect(() => toPcm16le({ bytes: Buffer.alloc(4), mime: "" }, 24000)).toThrow(PcmUnsupported);
    expect(() => toPcm16le({ bytes: Buffer.alloc(4), mime: "audio/pcm" }, 16000)).toThrow(PcmUnsupported);
  });
});

describe("pcmMime", () => {
  it("labels little-endian PCM explicitly, never audio/L16", () => {
    expect(pcmMime(16000)).toBe("audio/pcm;rate=16000;channels=1;bits=16;endian=little");
    expect(pcmMime(24000)).toBe("audio/pcm;rate=24000;channels=1;bits=16;endian=little");
  });
});
```

- [ ] **Step 4: Run it to see it fail**

```bash
cd "$(git -C /Users/omkar/Desktop/openmaus/OpenGrokBot worktree list --porcelain | awk '/^worktree /{w=substr($0,10)} $0=="branch refs/heads/feat/gadget-voice"{print w}')" && test "$(git branch --show-current)" = feat/gadget-voice || { echo 'not in the feat/gadget-voice worktree'; exit 1; }
export PATH="$HOME/.nvm/versions/node/v24.14.1/bin:$PATH"
pnpm exec vitest run server/tts/pcm.test.ts
```

Expected: FAIL with `Error: Cannot find module './pcm.ts' imported from …/server/tts/pcm.test.ts` and `Tests  no tests`.

- [ ] **Step 5: Implement `server/tts/pcm.ts`**

```ts
// Speech as raw PCM, for gadgets (spec §6.3). No decoder: every provider is
// asked for PCM or WAV at the gadget's rate (index.ts speak(format)), so all
// this file does is walk a WAV container, resample PCM16 when a provider
// could not hit the rate exactly, and label the result. MP3 cannot be turned
// into PCM here on purpose; that reply stays text-only on the gadget.
import { Buffer } from "node:buffer";
import type { Audio } from "./index.ts";

export interface Pcm16 { rate: number; samples: Int16Array }   // mono

/** A WAV's fmt fields and its data bytes, before any conversion. */
export interface WavInfo {
  format: number;        // 1 = PCM (WAVE_FORMAT_EXTENSIBLE resolved to its sub-format)
  channels: number;
  rate: number;
  bits: number;
  data: Uint8Array;
}

/** The WAV is fine but not PCM16 (or the audio is MP3): text-only, no error. */
export class PcmUnsupported extends Error {
  constructor(message: string) {
    super(message);
    this.name = "PcmUnsupported";
  }
}

const WAVE_FORMAT_PCM = 1;
const WAVE_FORMAT_EXTENSIBLE = 0xfffe;
const STREAMED_SIZE = 0xffffffff;

const ascii = (bytes: Uint8Array, at: number) => String.fromCharCode(bytes[at]!, bytes[at + 1]!, bytes[at + 2]!, bytes[at + 3]!);

/** Walk the RIFF chunks. macOS `say` writes JUNK and FLLR chunks before
 * `data`, so a fixed 44-byte header would play ~4 KB of filler as noise.
 * A data size of 0xFFFFFFFF or 0 (streamed writers), or one that runs past
 * the end, means "to the end". Throws on anything that is not a WAV. */
export function readWav(bytes: Uint8Array): WavInfo {
  if (bytes.byteLength < 12 || ascii(bytes, 0) !== "RIFF" || ascii(bytes, 8) !== "WAVE") throw new Error("not a WAV file");
  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  let fmt: Omit<WavInfo, "data"> | null = null;
  let data: Uint8Array | null = null;
  let at = 12;
  while (at + 8 <= bytes.byteLength) {
    const id = ascii(bytes, at);
    const size = view.getUint32(at + 4, true);
    const body = at + 8;
    if (id === "fmt ") {
      if (size < 16 || body + 16 > bytes.byteLength) throw new Error("WAV fmt chunk is too short");
      let format = view.getUint16(body, true);
      const bits = view.getUint16(body + 14, true);
      if (format === WAVE_FORMAT_EXTENSIBLE && size >= 40 && body + 26 <= bytes.byteLength) format = view.getUint16(body + 24, true);
      fmt = { format, channels: view.getUint16(body + 2, true), rate: view.getUint32(body + 4, true), bits };
    } else if (id === "data") {
      const toEnd = size === STREAMED_SIZE || size === 0 || body + size > bytes.byteLength;
      data = bytes.subarray(body, toEnd ? bytes.byteLength : body + size);
      if (toEnd) break;
    }
    at = body + size + (size & 1);
  }
  if (!fmt) throw new Error("WAV has no fmt chunk");
  if (!data) throw new Error("WAV has no data chunk");
  if (fmt.channels < 1 || fmt.rate < 1) throw new Error("WAV fmt chunk is invalid");
  return { ...fmt, data };
}

/** PCM16 only; several channels are averaged to mono. Throws PcmUnsupported
 * for a valid WAV in another sample format, and Error for a broken file. */
export function parseWav(bytes: Uint8Array): Pcm16 {
  const wav = readWav(bytes);
  if (wav.format !== WAVE_FORMAT_PCM || wav.bits !== 16) {
    throw new PcmUnsupported(`WAV is format ${wav.format} with ${wav.bits}-bit samples; gadgets need 16-bit PCM`);
  }
  const frameBytes = wav.channels * 2;
  const frames = Math.floor(wav.data.byteLength / frameBytes);
  const view = new DataView(wav.data.buffer, wav.data.byteOffset, wav.data.byteLength);
  const samples = new Int16Array(frames);
  for (let i = 0; i < frames; i++) {
    let sum = 0;
    for (let c = 0; c < wav.channels; c++) sum += view.getInt16(i * frameBytes + c * 2, true);
    samples[i] = Math.round(sum / wav.channels);
  }
  return { rate: wav.rate, samples };
}

/** Linear resampler for PCM16. Speech only: no anti-alias filter, which is
 * inaudible for the 22.05 kHz → 16 kHz case it mostly runs on. */
export function resamplePcm16(pcm: Pcm16, rate: number): Pcm16 {
  if (pcm.rate === rate || pcm.samples.length === 0) return { rate, samples: pcm.samples };
  const n = pcm.samples.length;
  const out = new Int16Array(Math.max(1, Math.round((n * rate) / pcm.rate)));
  const step = pcm.rate / rate;
  for (let i = 0; i < out.length; i++) {
    const pos = i * step;
    const i0 = Math.min(n - 1, Math.floor(pos));
    const i1 = Math.min(n - 1, i0 + 1);
    const frac = pos - i0;
    const v = pcm.samples[i0]! * (1 - frac) + pcm.samples[i1]! * frac;
    out[i] = Math.max(-32768, Math.min(32767, Math.round(v)));
  }
  return { rate, samples: out };
}

function pcmToBuffer(samples: Int16Array): Buffer {
  const out = Buffer.alloc(samples.length * 2);
  for (let i = 0; i < samples.length; i++) out.writeInt16LE(samples[i]!, i * 2);
  return out;
}

function mimeRate(mime: string): number | null {
  const m = /;\s*rate=(\d+)/i.exec(mime);
  return m ? Number(m[1]) : null;
}

/** Provider audio → raw PCM16LE mono at `rate`: audio/pcm with its `rate=`
 * passes through (or is resampled when that rate differs), audio/wav is
 * parsed and resampled; audio/pcm without a rate, audio/mpeg or anything
 * else throws PcmUnsupported. */
export function toPcm16le(audio: Audio, rate: 16000 | 24000): Buffer {
  const mime = audio.mime.toLowerCase();
  const base = mime.split(";")[0]!.trim();
  if (base === "audio/pcm") {
    // Only ElevenLabs answers audio/pcm here, always labelled with its rate.
    // An unlabelled one (a Chatterbox server passing its own type through)
    // could be at any rate and would play at the wrong pitch: text only.
    const source = mimeRate(mime);
    if (source === null) throw new PcmUnsupported("audio/pcm without a rate");
    const even = audio.bytes.subarray(0, audio.bytes.byteLength - (audio.bytes.byteLength % 2));
    if (source === rate) return Buffer.from(even);
    const view = new DataView(even.buffer, even.byteOffset, even.byteLength);
    const samples = new Int16Array(even.byteLength / 2);
    for (let i = 0; i < samples.length; i++) samples[i] = view.getInt16(i * 2, true);
    return pcmToBuffer(resamplePcm16({ rate: source, samples }, rate).samples);
  }
  if (base === "audio/wav" || base === "audio/x-wav" || base === "audio/wave" || base === "audio/vnd.wave") {
    return pcmToBuffer(resamplePcm16(parseWav(audio.bytes), rate).samples);
  }
  throw new PcmUnsupported(`${base || "this audio"} cannot be sent to a gadget; ask the provider for WAV or PCM`);
}

/** Little-endian PCM16 mono. Not audio/L16, which is big-endian by definition. */
export function pcmMime(rate: 16000 | 24000): string {
  return `audio/pcm;rate=${rate};channels=1;bits=16;endian=little`;
}
```

In `server/tts/index.ts`, replace the last line (`:164`):

```ts
export type { Voice } from "./elevenlabs.ts";
```

with:

```ts
export type { Audio, Voice } from "./elevenlabs.ts";
```

- [ ] **Step 6: Run the tests and the server typecheck**

```bash
cd "$(git -C /Users/omkar/Desktop/openmaus/OpenGrokBot worktree list --porcelain | awk '/^worktree /{w=substr($0,10)} $0=="branch refs/heads/feat/gadget-voice"{print w}')" && test "$(git branch --show-current)" = feat/gadget-voice || { echo 'not in the feat/gadget-voice worktree'; exit 1; }
export PATH="$HOME/.nvm/versions/node/v24.14.1/bin:$PATH"
pnpm exec vitest run server/tts
pnpm exec tsc -p tsconfig.server.json
```

Expected:
- vitest prints `Test Files  4 passed (4)` and `Tests  79 passed (79)`: `pcm.test.ts` (12) with `tts.test.ts`, `grok.test.ts` and `speech-text.test.ts`, which still pass.
- `tsc` prints nothing and exits 0.

- [ ] **Step 7: Commit**

```bash
cd "$(git -C /Users/omkar/Desktop/openmaus/OpenGrokBot worktree list --porcelain | awk '/^worktree /{w=substr($0,10)} $0=="branch refs/heads/feat/gadget-voice"{print w}')" && test "$(git branch --show-current)" = feat/gadget-voice || { echo 'not in the feat/gadget-voice worktree'; exit 1; }
git add server/tts/pcm.ts server/tts/pcm.test.ts server/tts/index.ts
git commit -m "feat(tts): walk WAV chunks and resample PCM16 for gadget speech" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 2: Ask every TTS provider for PCM or WAV natively

**Files:**
- Modify: `server/tts/index.ts:9` (add the format types after `VoiceProvider`); `:114-116` (the `speak` signature); and the four provider calls inside `speak` (`:123`, `:131`, `:155`, `:161`)
- Modify: `server/tts/elevenlabs.ts:18` (import) and `:101-110` (`synthesize`)
- Modify: `server/tts/fish.ts:3` (import), `:144-165` (request) and `:169-171` (return)
- Modify: `server/tts/grok.ts:3` (import), `:52-60` (request and type check) and `:70-71` (return)
- Modify: `server/tts/system-voices.ts:60-64` (signature) and `:70` (`say` args)
- Create: `server/tts/pcm-format.test.ts`

**Interfaces:**
- Consumes: `toPcm16le` and `PcmUnsupported` from Task 1, in the tests only.
- Produces (`server/tts/index.ts`), used by Task 3:
  - `type SpeechFormat = "mp3" | "pcm_16000" | "pcm_24000"`
  - `SPEECH_FORMATS: ReadonlySet<SpeechFormat>`
  - `pcmRate(format: Exclude<SpeechFormat, "mp3">): 16000 | 24000`
  - `speak(cfg: AppConfig, text: string, voiceId?: string, run?: systemVoices.Runner, format: SpeechFormat = "mp3"): Promise<Audio>`. It still throws `NoVoiceConfigured` synchronously, and every existing caller is unchanged.
- Provider signatures gain a trailing optional parameter:
  - `elevenlabs.synthesize(…, api, format = "mp3")`
  - `fish.synthesize(…, model, format = "mp3")`
  - `grok.synthesize(…, key, format = "mp3")`
  - `synthesizeSystem(…, run, rate = 22050)`
- The audio each provider returns for a `pcm_*` format:

  | Provider | `mime` returned for `pcm_*` |
  |---|---|
  | ElevenLabs | `audio/pcm;rate=<rate>` |
  | Fish | `audio/wav` |
  | xAI | `audio/wav` |
  | `say` | `audio/wav` |
  | Chatterbox | unchanged (its server's own type) |

- [ ] **Step 1: Write the failing test**

Create `server/tts/pcm-format.test.ts`:

```ts
// speak(format: "pcm_*") asks every provider for PCM or WAV natively (spec
// §6.3), driven against a local stub like tts.test.ts: what we send is the
// thing that breaks. Each case also runs the answer through toPcm16le, the
// step /api/tts/speak takes next.
import { Buffer } from "node:buffer";
import { createServer, type Server } from "node:http";
import { afterAll, beforeAll, describe, expect, it } from "vitest";

import type { AppConfig } from "../config.ts";

let server: Server;
let stubBase = "";
const seen: Array<{ url: string; headers: Record<string, string | string[] | undefined>; body: string }> = [];

const PCM = Buffer.from([1, 0, 2, 0, 3, 0, 4, 0]);
const MP3 = Buffer.from([0xff, 0xfb, 0x90, 0x00]);

/** A minimal PCM16 mono WAV at `rate` holding samples 10, 20, 30. */
function wavAt(rate: number): Buffer {
  const data = Buffer.alloc(6);
  [10, 20, 30].forEach((s, i) => data.writeInt16LE(s, i * 2));
  const fmt = Buffer.alloc(16);
  fmt.writeUInt16LE(1, 0);
  fmt.writeUInt16LE(1, 2);
  fmt.writeUInt32LE(rate, 4);
  fmt.writeUInt32LE(rate * 2, 8);
  fmt.writeUInt16LE(2, 12);
  fmt.writeUInt16LE(16, 14);
  const head = (id: string, size: number) => {
    const b = Buffer.alloc(8);
    b.write(id, 0, "ascii");
    b.writeUInt32LE(size, 4);
    return b;
  };
  return Buffer.concat([head("RIFF", 4 + 8 + 16 + 8 + 6), Buffer.from("WAVE"), head("fmt ", 16), fmt, head("data", 6), data]);
}

beforeAll(async () => {
  server = createServer((req, res) => {
    let body = "";
    req.on("data", (c) => (body += c));
    req.on("end", () => {
      seen.push({ url: req.url ?? "", headers: req.headers, body });
      const path = (req.url ?? "").split("?")[0];
      if (path.startsWith("/v1/text-to-speech/")) {
        const pcm = (req.url ?? "").includes("output_format=pcm_");
        res.writeHead(200, { "content-type": pcm ? "application/octet-stream" : "audio/mpeg" });
        return res.end(pcm ? PCM : MP3);
      }
      if (path === "/v1/tts") {
        // Fish: the WAV it was asked for, at the rate it was asked for
        const asked = JSON.parse(body) as { format: string; sample_rate: number };
        res.writeHead(200, { "content-type": asked.format === "wav" ? "audio/wav" : "audio/mpeg" });
        return res.end(asked.format === "wav" ? wavAt(asked.sample_rate) : MP3);
      }
      if (path === "/xai/v1/tts") {
        const asked = JSON.parse(body) as { output_format: { codec: string; sample_rate?: number } };
        const wav = asked.output_format.codec === "wav";
        res.writeHead(200, { "content-type": wav ? "audio/wav; charset=binary" : "audio/mpeg" });
        return res.end(wav ? wavAt(asked.output_format.sample_rate ?? 24000) : MP3);
      }
      if (path === "/v1/audio/speech") {
        res.writeHead(200, { "content-type": "audio/mpeg" });
        return res.end(MP3);
      }
      res.writeHead(404, { "content-type": "application/json" });
      res.end("{}");
    });
  });
  await new Promise<void>((r) => server.listen(0, "127.0.0.1", r));
  stubBase = `http://127.0.0.1:${(server.address() as { port: number }).port}`;
  process.env.OMB_ELEVENLABS_API = `${stubBase}/v1`;
  process.env.OMB_FISH_AUDIO_API = stubBase;
  process.env.OMB_XAI_TTS_API = `${stubBase}/xai/v1`;
});

afterAll(() => new Promise<void>((r) => server.close(() => r())));

/** Fish and xAI read their base URL at import time: import after listen. */
const voice = () => import("./index.ts");
const pcm = () => import("./pcm.ts");

describe("speak(format) asks each provider for PCM or WAV natively", () => {
  it("ElevenLabs: output_format=pcm_24000, raw PCM labelled with its rate", async () => {
    seen.length = 0;
    const { speak } = await voice();
    const audio = await speak({ tts: { key: "el-key", voice: "v-1" } }, "hello", undefined, undefined, "pcm_24000");
    expect(seen.at(-1)!.url).toContain("output_format=pcm_24000");
    expect(seen.at(-1)!.url).not.toContain("mp3");
    expect(audio.mime).toBe("audio/pcm;rate=24000");
    expect((await pcm()).toPcm16le(audio, 24000)).toEqual(PCM);
  });

  it("ElevenLabs: mp3 stays the default for every existing caller", async () => {
    seen.length = 0;
    const { speak } = await voice();
    const audio = await speak({ tts: { key: "el-key", voice: "v-1" } }, "hello");
    expect(seen.at(-1)!.url).toContain("output_format=mp3_44100_64");
    expect(audio.mime).toBe("audio/mpeg");
  });

  it("Fish: format wav with sample_rate, no mp3_bitrate", async () => {
    seen.length = 0;
    const { speak } = await voice();
    const cfg: AppConfig = { tts: { provider: "fish", fishKey: "fish-key", voice: "fish-1" } };
    const audio = await speak(cfg, "hello", undefined, undefined, "pcm_16000");
    expect(JSON.parse(seen.at(-1)!.body)).toEqual({ text: "hello", reference_id: "fish-1", format: "wav", sample_rate: 16000, latency: "normal" });
    expect(audio.mime).toBe("audio/wav");
    expect(Array.from(new Int16Array(new Uint8Array((await pcm()).toPcm16le(audio, 16000)).buffer))).toEqual([10, 20, 30]);
  });

  it("xAI: output_format {codec: wav, sample_rate}, WAV content types with parameters accepted", async () => {
    seen.length = 0;
    const { speak } = await voice();
    const cfg: AppConfig = { xai: { key: "xai-key" }, tts: { provider: "xai", voice: "ara" } };
    const audio = await speak(cfg, "hello", undefined, undefined, "pcm_24000");
    expect(JSON.parse(seen.at(-1)!.body).output_format).toEqual({ codec: "wav", sample_rate: 24000 });
    expect(audio.mime).toBe("audio/wav");
    expect((await pcm()).toPcm16le(audio, 24000).byteLength).toBe(6);
  });

  it("macOS say: --data-format=LEI16@<rate>", async () => {
    const { speak } = await voice();
    const argv: string[][] = [];
    const fakeSay = async (_file: string, args: string[]) => {
      argv.push(args);
      const { writeFile } = await import("node:fs/promises");
      await writeFile(args[args.indexOf("-o") + 1]!, wavAt(16000));
      return { stdout: "" };
    };
    const audio = await speak({ tts: { provider: "system" } }, "hello", "Albert", fakeSay, "pcm_16000");
    expect(argv[0]).toContain("--data-format=LEI16@16000");
    expect((await pcm()).toPcm16le(audio, 16000).byteLength).toBe(6);
  });

  it("Chatterbox: a server that answers MP3 leaves the gadget text-only", async () => {
    const { speak } = await voice();
    const { toPcm16le, PcmUnsupported } = await pcm();
    const audio = await speak({ tts: { provider: "chatterbox", baseUrl: stubBase, voice: "alex" } }, "hello", undefined, undefined, "pcm_16000");
    expect(audio.mime).toBe("audio/mpeg");
    expect(() => toPcm16le(audio, 16000)).toThrow(PcmUnsupported);
  });

  it("still throws NoVoiceConfigured synchronously for a pcm format", async () => {
    const { speak, NoVoiceConfigured } = await voice();
    expect(() => speak({}, "hello", undefined, undefined, "pcm_16000")).toThrow(NoVoiceConfigured);
  });
});
```

- [ ] **Step 2: Run it to see it fail**

```bash
cd "$(git -C /Users/omkar/Desktop/openmaus/OpenGrokBot worktree list --porcelain | awk '/^worktree /{w=substr($0,10)} $0=="branch refs/heads/feat/gadget-voice"{print w}')" && test "$(git branch --show-current)" = feat/gadget-voice || { echo 'not in the feat/gadget-voice worktree'; exit 1; }
export PATH="$HOME/.nvm/versions/node/v24.14.1/bin:$PATH"
pnpm exec vitest run server/tts/pcm-format.test.ts
```

Expected: FAIL, `Tests  4 failed | 3 passed (7)`, with:
- `expected '/v1/text-to-speech/v-1?output_format=…' to contain 'output_format=pcm_24000'`
- `expected { text: 'hello', …(5) } to deeply equal { text: 'hello', …(4) }` (Fish)
- `expected { codec: 'mp3' } to deeply equal { codec: 'wav', sample_rate: 24000 }` (xAI)
- `expected [ '-o', …(5) ] to include '--data-format=LEI16@16000'` (say)

- [ ] **Step 3: Add the format types and pass `format` through `speak`**

In `server/tts/index.ts`, replace:

```ts
export type VoiceProvider = "elevenlabs" | "fish" | "system" | "chatterbox" | "xai";
```

with:

```ts
export type VoiceProvider = "elevenlabs" | "fish" | "system" | "chatterbox" | "xai";

/** What speak() asks a provider for: MP3 (the app's default, unchanged) or
 * 16-bit mono PCM at a gadget speaker's rate (spec §6.3). For pcm_* every
 * provider is asked for PCM or WAV natively; server/tts/pcm.ts turns that
 * into raw PCM16LE. Nothing is decoded from MP3. */
export type SpeechFormat = "mp3" | "pcm_16000" | "pcm_24000";
export const SPEECH_FORMATS: ReadonlySet<SpeechFormat> = new Set<SpeechFormat>(["mp3", "pcm_16000", "pcm_24000"]);
/** The sample rate a pcm_* format asks for. */
export const pcmRate = (format: Exclude<SpeechFormat, "mp3">): 16000 | 24000 => (format === "pcm_24000" ? 24000 : 16000);
```

Replace:

```ts
/** Synthesize one utterance. Throws NoVoiceConfigured when there is nothing
 * to speak with, which the route turns into a 409 the client can explain. */
export function speak(cfg: AppConfig, text: string, voiceId?: string, run?: systemVoices.Runner) {
```

with:

```ts
/** Synthesize one utterance. Throws NoVoiceConfigured when there is nothing
 * to speak with, which the route turns into a 409 the client can explain.
 * `format` defaults to mp3, so existing positional callers are unchanged. */
export function speak(cfg: AppConfig, text: string, voiceId?: string, run?: systemVoices.Runner, format: SpeechFormat = "mp3") {
```

Then change the four provider calls inside `speak`. Each is one whole line:

```ts
    return grok.synthesize(text, voice, key);
```
→
```ts
    return grok.synthesize(text, voice, key, format);
```

```ts
    return systemVoices.synthesizeSystem(text, voice, run);
```
→
```ts
    return systemVoices.synthesizeSystem(text, voice, run, format === "mp3" ? undefined : pcmRate(format));
```

```ts
    return fish.synthesize(text, voice, key, cfg.tts?.fishModel);
```
→
```ts
    return fish.synthesize(text, voice, key, cfg.tts?.fishModel, format);
```

```ts
  return elevenlabs.synthesize(text, voice, credential.token, credential.api);
```
→
```ts
  return elevenlabs.synthesize(text, voice, credential.token, credential.api, format);
```

- [ ] **Step 4: ElevenLabs asks for `pcm_16000` / `pcm_24000` by name**

In `server/tts/elevenlabs.ts`, replace:

```ts
import { elevenLabsProviderApi } from "../included-services.ts";
```

with:

```ts
import { elevenLabsProviderApi } from "../included-services.ts";
import type { SpeechFormat } from "./index.ts";
```

and replace `synthesize` (`:101-110`):

```ts
export async function synthesize(text: string, voiceId: string, key: string, api: string): Promise<Audio> {
  const res = await fetch(`${api}/text-to-speech/${encodeURIComponent(voiceId)}?output_format=${FORMAT}`, {
    method: "POST",
    headers: { "xi-api-key": key, "content-type": "application/json", accept: "audio/mpeg" },
    body: JSON.stringify({ text, model_id: MODEL }),
    signal: AbortSignal.timeout(60_000),
  });
  if (!res.ok) throw new Error(message(res.status, "speaking", await safeJson(res)));
  return { bytes: new Uint8Array(await res.arrayBuffer()), mime: "audio/mpeg" };
}
```

with:

```ts
/** mp3 by default; pcm_16000 / pcm_24000 are ElevenLabs' own raw output
 * formats (16-bit mono little-endian), asked for by name. */
export async function synthesize(text: string, voiceId: string, key: string, api: string, format: SpeechFormat = "mp3"): Promise<Audio> {
  const pcm = format !== "mp3";
  const res = await fetch(`${api}/text-to-speech/${encodeURIComponent(voiceId)}?output_format=${pcm ? format : FORMAT}`, {
    method: "POST",
    headers: { "xi-api-key": key, "content-type": "application/json", accept: pcm ? "*/*" : "audio/mpeg" },
    body: JSON.stringify({ text, model_id: MODEL }),
    signal: AbortSignal.timeout(60_000),
  });
  if (!res.ok) throw new Error(message(res.status, "speaking", await safeJson(res)));
  const bytes = new Uint8Array(await res.arrayBuffer());
  return { bytes, mime: pcm ? `audio/pcm;rate=${format === "pcm_24000" ? 24000 : 16000}` : "audio/mpeg" };
}
```

- [ ] **Step 5: Fish asks for WAV at the rate**

In `server/tts/fish.ts`, replace:

```ts
import type { FishTtsModel } from "../config.ts";
```

with:

```ts
import type { FishTtsModel } from "../config.ts";
import type { SpeechFormat } from "./index.ts";
```

replace (`:144-165`):

```ts
  model: FishTtsModel = DEFAULT_FISH_MODEL,
): Promise<Audio> {
  let res: Response;
  try {
    res = await fetch(`${API}/v1/tts`, {
      method: "POST",
      headers: {
        authorization: `Bearer ${key}`,
        "content-type": "application/json",
        accept: "audio/mpeg",
        model,
      },
      body: JSON.stringify({
        text,
        reference_id: voiceId,
        format: "mp3",
        sample_rate: 44_100,
        mp3_bitrate: 64,
        latency: "normal",
      }),
      signal: AbortSignal.timeout(60_000),
    });
```

with:

```ts
  model: FishTtsModel = DEFAULT_FISH_MODEL,
  format: SpeechFormat = "mp3",
): Promise<Audio> {
  // For a gadget, 16-bit mono WAV at its speaker rate (Fish's WAV is PCM16
  // mono at 8/16/24/32/44.1 kHz); server/tts/pcm.ts strips the container.
  const wav = format !== "mp3";
  let res: Response;
  try {
    res = await fetch(`${API}/v1/tts`, {
      method: "POST",
      headers: {
        authorization: `Bearer ${key}`,
        "content-type": "application/json",
        accept: wav ? "audio/wav" : "audio/mpeg",
        model,
      },
      body: JSON.stringify(
        wav
          ? { text, reference_id: voiceId, format: "wav", sample_rate: format === "pcm_24000" ? 24_000 : 16_000, latency: "normal" }
          : { text, reference_id: voiceId, format: "mp3", sample_rate: 44_100, mp3_bitrate: 64, latency: "normal" },
      ),
      signal: AbortSignal.timeout(60_000),
    });
```

and replace:

```ts
  if (!res.ok) throw new Error(errorMessage(res.status, "speaking", await safeBody(res)));
  return { bytes: new Uint8Array(await res.arrayBuffer()), mime: "audio/mpeg" };
}
```

with:

```ts
  if (!res.ok) throw new Error(errorMessage(res.status, "speaking", await safeBody(res)));
  return { bytes: new Uint8Array(await res.arrayBuffer()), mime: wav ? "audio/wav" : "audio/mpeg" };
}
```

- [ ] **Step 6: xAI asks for WAV at the rate and accepts WAV content types**

In `server/tts/grok.ts`, replace:

```ts
import type { Audio, Voice } from "./elevenlabs.ts";
```

with:

```ts
import type { Audio, Voice } from "./elevenlabs.ts";
import type { SpeechFormat } from "./index.ts";
```

replace (`:52-60`):

```ts
export async function synthesize(text: string, voiceId: string, key: string): Promise<Audio> {
  const response = await request("/tts", {
    method: "POST",
    headers: { authorization: `Bearer ${key}`, "content-type": "application/json", accept: "audio/mpeg" },
    body: JSON.stringify({ text, voice_id: voiceId, language: "auto", output_format: { codec: "mp3" } }),
    signal: AbortSignal.timeout(60_000),
  });
  const mime = (response.headers.get("content-type") ?? "").split(";")[0].trim().toLowerCase();
  if (mime !== "audio/mpeg" && mime !== "audio/mp3") {
```

with:

```ts
const WAV_TYPES = new Set(["audio/wav", "audio/x-wav", "audio/wave"]);

/** mp3 by default; for a gadget, WAV at its speaker rate
 * (output_format {codec: "wav", sample_rate}), parsed by server/tts/pcm.ts. */
export async function synthesize(text: string, voiceId: string, key: string, format: SpeechFormat = "mp3"): Promise<Audio> {
  const wav = format !== "mp3";
  const response = await request("/tts", {
    method: "POST",
    headers: { authorization: `Bearer ${key}`, "content-type": "application/json", accept: wav ? "audio/wav" : "audio/mpeg" },
    body: JSON.stringify({
      text,
      voice_id: voiceId,
      language: "auto",
      output_format: wav ? { codec: "wav", sample_rate: format === "pcm_24000" ? 24000 : 16000 } : { codec: "mp3" },
    }),
    signal: AbortSignal.timeout(60_000),
  });
  const mime = (response.headers.get("content-type") ?? "").split(";")[0].trim().toLowerCase();
  if (wav ? !WAV_TYPES.has(mime) : mime !== "audio/mpeg" && mime !== "audio/mp3") {
```

and replace:

```ts
  if (!bytes.byteLength) throw new Error("Grok returned empty audio. Try again.");
  return { bytes, mime: "audio/mpeg" };
```

with:

```ts
  if (!bytes.byteLength) throw new Error("Grok returned empty audio. Try again.");
  return { bytes, mime: wav ? "audio/wav" : "audio/mpeg" };
```

- [ ] **Step 7: macOS `say` writes the gadget's rate**

In `server/tts/system-voices.ts`, replace (`:60-64`):

```ts
/** Synthesize one utterance to 22 kHz mono WAV — small, and every browser
 * plays it. `say` writes the container itself; no conversion step needed.
 * The voice id is a system voice name; empty falls back to the system's
 * own default, which is what a fresh Mac already sounds like. */
export async function synthesizeSystem(text: string, voiceId: string | undefined, run: Runner = defaultRun): Promise<Audio> {
```

with:

```ts
/** Synthesize one utterance to 22 kHz mono WAV — small, and every browser
 * plays it. `say` writes the container itself; no conversion step needed.
 * The voice id is a system voice name; empty falls back to the system's
 * own default, which is what a fresh Mac already sounds like. A gadget asks
 * for its speaker's rate instead (16000 or 24000). */
export async function synthesizeSystem(text: string, voiceId: string | undefined, run: Runner = defaultRun, rate = 22050): Promise<Audio> {
```

and replace:

```ts
    const args = ["-o", out, "--data-format=LEI16@22050"];
```

with:

```ts
    const args = ["-o", out, `--data-format=LEI16@${rate}`];
```

Chatterbox (`server/tts/chatterbox.ts`) stays unchanged: it already asks for WAV, and `toPcm16le` refuses its MP3 answer.

- [ ] **Step 8: Run the TTS tests and the typecheck**

```bash
cd "$(git -C /Users/omkar/Desktop/openmaus/OpenGrokBot worktree list --porcelain | awk '/^worktree /{w=substr($0,10)} $0=="branch refs/heads/feat/gadget-voice"{print w}')" && test "$(git branch --show-current)" = feat/gadget-voice || { echo 'not in the feat/gadget-voice worktree'; exit 1; }
export PATH="$HOME/.nvm/versions/node/v24.14.1/bin:$PATH"
pnpm exec vitest run server/tts
pnpm exec tsc -p tsconfig.server.json
```

Expected:
- vitest prints `Test Files  5 passed (5)` and `Tests  86 passed (86)`: the 79 from Task 1 plus `pcm-format.test.ts` (7).
- `tts.test.ts` and `grok.test.ts` are among them. They pin the MP3 defaults: `output_format=mp3`, Fish `format: "mp3"`, xAI `{codec: "mp3"}` and `LEI16@22050`.
- `tsc` exits 0.

- [ ] **Step 9: Commit**

```bash
cd "$(git -C /Users/omkar/Desktop/openmaus/OpenGrokBot worktree list --porcelain | awk '/^worktree /{w=substr($0,10)} $0=="branch refs/heads/feat/gadget-voice"{print w}')" && test "$(git branch --show-current)" = feat/gadget-voice || { echo 'not in the feat/gadget-voice worktree'; exit 1; }
git add server/tts/index.ts server/tts/elevenlabs.ts server/tts/fish.ts server/tts/grok.ts server/tts/system-voices.ts server/tts/pcm-format.test.ts
git commit -m "feat(tts): ask each voice provider for PCM or WAV at a gadget's rate" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 3: `POST /api/tts/speak` takes `format`

**Files:**
- Create: `server/tts/speak-reply.ts`
- Create: `server/tts/speak-reply.test.ts`
- Modify: `server/index.ts:343` (import) and `:24313-24335` (the inline `/api/tts/speak` block, edited in place as contract §3.14 says)

**Interfaces:**
- Consumes:
  - From Task 2: `SPEECH_FORMATS`, `pcmRate`, `NoVoiceConfigured`, `type Audio`, `type SpeechFormat`, and `tts.speak(cfg, text, voiceId, undefined, format)`.
  - From Task 1: `toPcm16le`, `PcmUnsupported`, `pcmMime`.
- Produces (`server/tts/speak-reply.ts`, private to P3b):
  - `SPEAK_MAX_CHARS = 500`
  - `type SpeakReply`
  - `type Synthesize = (text, voiceId, format) => Promise<Audio>`
  - `ttsSpeakReply(body: unknown, synthesize: Synthesize): Promise<SpeakReply>`
- The HTTP contract of `POST /api/tts/speak` (contract §3.14), used by Task 11:

  | Request | Answer |
  |---|---|
  | `format` absent or `mp3` | unchanged |
  | `pcm_16000` / `pcm_24000` | 200, `content-type: audio/pcm;rate=<rate>;channels=1;bits=16;endian=little`, raw PCM16LE mono |
  | a voice that can only produce MP3 | 415 `{error: "pcm_unsupported"}` |
  | any other `format` | 400 `{error: "format must be mp3, pcm_16000 or pcm_24000"}` |
  | no voice set up | 409 (unchanged) |
  | a provider failure | 502 (unchanged) |

- [ ] **Step 1: Write the failing test**

Create `server/tts/speak-reply.test.ts`:

```ts
// POST /api/tts/speak's answer (spec §6.3): mp3 unchanged, pcm_* as raw
// little-endian PCM with an explicit content type, 415 for an MP3-only
// voice, 409 when no voice is set up.
import { Buffer } from "node:buffer";
import { readFileSync } from "node:fs";
import { describe, expect, it, vi } from "vitest";

import { NoVoiceConfigured, type Audio } from "./index.ts";
import { ttsSpeakReply } from "./speak-reply.ts";

const mp3: Audio = { bytes: new Uint8Array([0xff, 0xfb, 0x90, 0x00]), mime: "audio/mpeg" };
const pcm24: Audio = { bytes: new Uint8Array([1, 0, 2, 0, 3, 0]), mime: "audio/pcm;rate=24000" };

describe("ttsSpeakReply", () => {
  it("keeps the mp3 answer exactly as before when format is absent or mp3", async () => {
    for (const body of [{ text: " hi " }, { text: "hi", format: "mp3" }]) {
      const synth = vi.fn(async () => mp3);
      const reply = await ttsSpeakReply(body, synth);
      expect(synth).toHaveBeenCalledWith("hi", undefined, "mp3");
      expect(reply).toEqual({
        kind: "audio", status: 200,
        headers: { "content-type": "audio/mpeg", "content-length": "4", "cache-control": "no-store" },
        body: Buffer.from(mp3.bytes),
      });
    }
  });

  it("answers pcm_24000 as raw PCM16LE with the explicit little-endian type", async () => {
    const synth = vi.fn(async () => pcm24);
    const reply = await ttsSpeakReply({ text: "hello", voiceId: "v-1", format: "pcm_24000" }, synth);
    expect(synth).toHaveBeenCalledWith("hello", "v-1", "pcm_24000");
    expect(reply).toEqual({
      kind: "audio", status: 200,
      headers: { "content-type": "audio/pcm;rate=24000;channels=1;bits=16;endian=little", "content-length": "6", "cache-control": "no-store" },
      body: Buffer.from([1, 0, 2, 0, 3, 0]),
    });
  });

  it("resamples to pcm_16000 when the provider answered at another rate", async () => {
    const reply = await ttsSpeakReply({ text: "hello", format: "pcm_16000" }, async () => ({ bytes: new Uint8Array(480 * 2), mime: "audio/pcm;rate=24000" }));
    expect(reply.kind).toBe("audio");
    if (reply.kind === "audio") expect(reply.body.byteLength).toBe(320 * 2);
  });

  it("answers 415 pcm_unsupported for an MP3-only voice", async () => {
    expect(await ttsSpeakReply({ text: "hello", format: "pcm_16000" }, async () => mp3)).toEqual({ kind: "json", status: 415, body: { error: "pcm_unsupported" } });
  });

  it("keeps 409 for no voice (thrown synchronously) and 502 for provider failures", async () => {
    const noVoice = () => { throw new NoVoiceConfigured("voice"); };
    expect(await ttsSpeakReply({ text: "hello", format: "pcm_16000" }, noVoice)).toEqual({ kind: "json", status: 409, body: { error: "Pick a voice in the agent profile." } });
    expect(await ttsSpeakReply({ text: "hello", format: "pcm_16000" }, async () => { throw new Error("ElevenLabs says no"); })).toEqual({ kind: "json", status: 502, body: { error: "ElevenLabs says no" } });
    // a WAV that is not a WAV is a provider failure, not an unsupported format
    expect((await ttsSpeakReply({ text: "hello", format: "pcm_16000" }, async () => ({ bytes: new Uint8Array(4), mime: "audio/wav" }))).status).toBe(502);
  });

  it("refuses bad input before calling the provider", async () => {
    const synth = vi.fn(async () => mp3);
    expect(await ttsSpeakReply({ text: "  " }, synth)).toEqual({ kind: "json", status: 400, body: { error: "text required" } });
    expect((await ttsSpeakReply({ text: "x".repeat(501) }, synth)).status).toBe(413);
    for (const format of ["pcm_44100", "wav", 16000, null]) {
      expect(await ttsSpeakReply({ text: "hi", format }, synth)).toEqual({ kind: "json", status: 400, body: { error: "format must be mp3, pcm_16000 or pcm_24000" } });
    }
    expect(await ttsSpeakReply(null, synth)).toEqual({ kind: "json", status: 400, body: { error: "text required" } });
    expect(synth).not.toHaveBeenCalled();
  });

  it("is what POST /api/tts/speak answers (server/index.ts)", () => {
    const index = readFileSync(new URL("../index.ts", import.meta.url), "utf8");
    expect(index).toContain("const reply = await ttsSpeakReply(await readBody(req), (text, voiceId, format) => tts.speak(cfg, text, voiceId, undefined, format));");
  });
});
```

- [ ] **Step 2: Run it to see it fail**

```bash
cd "$(git -C /Users/omkar/Desktop/openmaus/OpenGrokBot worktree list --porcelain | awk '/^worktree /{w=substr($0,10)} $0=="branch refs/heads/feat/gadget-voice"{print w}')" && test "$(git branch --show-current)" = feat/gadget-voice || { echo 'not in the feat/gadget-voice worktree'; exit 1; }
export PATH="$HOME/.nvm/versions/node/v24.14.1/bin:$PATH"
pnpm exec vitest run server/tts/speak-reply.test.ts
```

Expected: FAIL with `Error: Cannot find module './speak-reply.ts' imported from …/server/tts/speak-reply.test.ts`.

- [ ] **Step 3: Implement `server/tts/speak-reply.ts`**

```ts
// The answer to POST /api/tts/speak, kept out of server/index.ts so it can
// be tested without booting the harness. The inline route in index.ts reads
// the body, calls this, and writes what it returns.
//
// `format` is optional: absent or "mp3" is the app's existing behavior byte
// for byte; "pcm_16000" / "pcm_24000" is a gadget's speaker (spec §6.3).
import { Buffer } from "node:buffer";

import { NoVoiceConfigured, SPEECH_FORMATS, pcmRate, type Audio, type SpeechFormat } from "./index.ts";
import { PcmUnsupported, pcmMime, toPcm16le } from "./pcm.ts";

/** The normal client sends <=320-character utterances. A hard ceiling
 * prevents an arbitrary local request from turning the user's hosted voice
 * account into an unbounded, billable synthesis job. */
export const SPEAK_MAX_CHARS = 500;

export type SpeakReply =
  | { kind: "audio"; status: 200; headers: Record<string, string>; body: Buffer }
  | { kind: "json"; status: number; body: { error: string } };

export type Synthesize = (text: string, voiceId: string | undefined, format: SpeechFormat) => Promise<Audio>;

export async function ttsSpeakReply(body: unknown, synthesize: Synthesize): Promise<SpeakReply> {
  const input = (body && typeof body === "object" ? body : {}) as { text?: unknown; voiceId?: unknown; format?: unknown };
  const text = String(input.text ?? "").trim();
  if (!text) return { kind: "json", status: 400, body: { error: "text required" } };
  if (text.length > SPEAK_MAX_CHARS) return { kind: "json", status: 413, body: { error: "voice utterances are limited to 500 characters" } };
  const format = input.format === undefined ? "mp3" : input.format;
  if (typeof format !== "string" || !SPEECH_FORMATS.has(format as SpeechFormat)) {
    return { kind: "json", status: 400, body: { error: "format must be mp3, pcm_16000 or pcm_24000" } };
  }
  const voiceId = typeof input.voiceId === "string" ? input.voiceId : undefined;
  try {
    // tts.speak throws NoVoiceConfigured synchronously; the await inside
    // this try catches that too.
    const audio = await synthesize(text, voiceId, format as SpeechFormat);
    if (format === "mp3") {
      const bytes = Buffer.from(audio.bytes);
      return { kind: "audio", status: 200, headers: { "content-type": audio.mime, "content-length": String(bytes.byteLength), "cache-control": "no-store" }, body: bytes };
    }
    const rate = pcmRate(format as Exclude<SpeechFormat, "mp3">);
    const pcm = toPcm16le(audio, rate);
    return { kind: "audio", status: 200, headers: { "content-type": pcmMime(rate), "content-length": String(pcm.byteLength), "cache-control": "no-store" }, body: pcm };
  } catch (e) {
    // "you haven't set this up yet" is not a provider failure — 409 so the
    // client can point at App Settings instead of showing a 502.
    if (e instanceof NoVoiceConfigured) return { kind: "json", status: 409, body: { error: e.message } };
    // MP3-only voice: the gadget stays text-only for this reply, no notice.
    if (e instanceof PcmUnsupported) return { kind: "json", status: 415, body: { error: "pcm_unsupported" } };
    return { kind: "json", status: 502, body: { error: e instanceof Error ? e.message : String(e) } };
  }
}
```

- [ ] **Step 4: Answer the route with it**

In `server/index.ts`, replace (`:343`):

```ts
import { narrateTool, toUtterances } from "./tts/speech-text.ts";
```

with:

```ts
import { narrateTool, toUtterances } from "./tts/speech-text.ts";
import { ttsSpeakReply } from "./tts/speak-reply.ts";
```

and replace the block at `:24313-24335`:

```ts
    if (method === "POST" && path === "/api/tts/speak") {
      const body = await readBody(req);
      const text = String(body.text ?? "").trim();
      if (!text) return json(res, 400, { error: "text required" });
      // The normal client sends <=320-character utterances. A hard ceiling
      // prevents an arbitrary local request from turning the user's hosted
      // voice account into an unbounded, billable synthesis job.
      if (text.length > 500) return json(res, 413, { error: "voice utterances are limited to 500 characters" });
      try {
        const audio = await tts.speak(cfg, text, typeof body.voiceId === "string" ? body.voiceId : undefined);
        res.writeHead(200, {
          "content-type": audio.mime,
          "content-length": String(audio.bytes.byteLength),
          "cache-control": "no-store",
        });
        return res.end(Buffer.from(audio.bytes));
      } catch (e) {
        // "you haven't set this up yet" is not a provider failure — 409 so
        // the client can point at App Settings instead of showing a 502
        if (e instanceof tts.NoVoiceConfigured) return json(res, 409, { error: e.message });
        return json(res, 502, { error: e instanceof Error ? e.message : String(e) });
      }
    }
```

with:

```ts
    if (method === "POST" && path === "/api/tts/speak") {
      // mp3 by default; format pcm_16000 / pcm_24000 is a gadget's speaker
      // (server/tts/speak-reply.ts has the rules and the status codes).
      const reply = await ttsSpeakReply(await readBody(req), (text, voiceId, format) => tts.speak(cfg, text, voiceId, undefined, format));
      if (reply.kind === "json") return json(res, reply.status, reply.body);
      res.writeHead(reply.status, reply.headers);
      return res.end(reply.body);
    }
```

The block keeps its one `path === "/api/tts/speak"` guard, so the route ratchet does not move.

- [ ] **Step 5: Run the tests, the ratchet and the typecheck**

```bash
cd "$(git -C /Users/omkar/Desktop/openmaus/OpenGrokBot worktree list --porcelain | awk '/^worktree /{w=substr($0,10)} $0=="branch refs/heads/feat/gadget-voice"{print w}')" && test "$(git branch --show-current)" = feat/gadget-voice || { echo 'not in the feat/gadget-voice worktree'; exit 1; }
export PATH="$HOME/.nvm/versions/node/v24.14.1/bin:$PATH"
pnpm exec vitest run server/tts/speak-reply.test.ts scripts/testing/index-route-ratchet.test.ts
pnpm exec tsc -p tsconfig.server.json
```

Expected: vitest prints `Test Files  2 passed (2)` and `Tests  8 passed (8)` (`speak-reply.test.ts` has 7, the ratchet 1), and `tsc` exits 0.

- [ ] **Step 6: Commit**

```bash
cd "$(git -C /Users/omkar/Desktop/openmaus/OpenGrokBot worktree list --porcelain | awk '/^worktree /{w=substr($0,10)} $0=="branch refs/heads/feat/gadget-voice"{print w}')" && test "$(git branch --show-current)" = feat/gadget-voice || { echo 'not in the feat/gadget-voice worktree'; exit 1; }
git add server/tts/speak-reply.ts server/tts/speak-reply.test.ts server/index.ts
git commit -m "feat(tts): /api/tts/speak answers pcm_16000 and pcm_24000 for gadgets" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 4: Speech helper file mode that never prompts

**Files:**
- Modify: `electron/resources/speech-helper.swift`:
  - the header comment, `:9-10`;
  - two new flags after `--finish-file`, `:48-52`;
  - the dictation entry point, `:130-141`: the locale choice becomes `preferredRecognizer()`, and file mode goes in front of it.
- Create: `electron/speech-helper-file-mode.node-test.mjs` (`pnpm test:electron` runs `electron/*.node-test.mjs`)

**Interfaces:**
- Produces, for Task 7: the CLI `speech-helper --file <absolute path> [--timeout-ms N] [--stop-file P]`. `N` defaults to 30000 and is clamped to 1000–120000. It prints exactly one NDJSON line on stdout:
  - `{"partial":false,"text":"…"}` and exit 0, or
  - `{"error": E}` and exit 1, where `E` is one of `speech-not-authorized`, `recognizer-unavailable`, `dictation-disabled`, `recognition-error`, `no-speech`, `timeout`, `file-unreadable`.
- Dictation is unchanged: no `--file`, the same flags, the same NDJSON stream.

- [ ] **Step 1: Write the failing test**

Create `electron/speech-helper-file-mode.node-test.mjs`:

```js
// The Speech helper's file mode is what gadget turns use (server/stt/apple.ts,
// spec §6.3). Its one hard rule: it never asks for Speech Recognition
// permission, so a gadget turn can never pop a dialog on the Mac. These pin
// that rule in the source, and type-check the helper where Swift exists.
import assert from "node:assert/strict";
import { execFileSync } from "node:child_process";
import { readFileSync } from "node:fs";
import { test } from "node:test";
import { fileURLToPath } from "node:url";

const SOURCE = fileURLToPath(new URL("./resources/speech-helper.swift", import.meta.url));
// CRLF on a Windows checkout (the .swift file has no eol=lf attribute)
const source = readFileSync(SOURCE, "utf8").replace(/\r\n/g, "\n");

function hasSwiftc() {
  try {
    execFileSync("swiftc", ["--version"], { stdio: "ignore", timeout: 30_000 });
    return true;
  } catch {
    return false;
  }
}

test("file mode runs, and exits, before the dictation path asks for permission", () => {
  const fileMode = source.indexOf("if let inputFile {");
  const prompt = source.indexOf("SFSpeechRecognizer.requestAuthorization");
  assert.ok(fileMode > 0, "file mode entry point");
  assert.ok(prompt > fileMode, "file mode must come first");
  assert.match(source, /func runFileMode\(_ path: String\) -> Never/);
});

test("file mode checks the existing grant and never requests one or opens the mic", () => {
  const start = source.indexOf("func runFileMode(");
  const end = source.indexOf("// end file mode");
  assert.ok(start > 0 && end > start);
  const body = source.slice(start, end);
  assert.match(body, /SFSpeechRecognizer\.authorizationStatus\(\) == \.authorized/);
  assert.match(body, /fail\("speech-not-authorized"\)/);
  assert.doesNotMatch(body, /requestAuthorization/);
  assert.doesNotMatch(body, /AVAudioEngine/);
  assert.match(body, /SFSpeechURLRecognitionRequest/);
  assert.match(body, /shouldReportPartialResults = false/);
  assert.match(body, /if #available\(macOS 13, \*\) \{\s*request\.addsPunctuation = true/);
  assert.match(body, /fail\("no-speech"\)/);
  assert.match(body, /fail\("timeout"\)/);
});

test("--file and --timeout-ms parse like the existing flags", () => {
  assert.match(source, /args\.firstIndex\(of: "--file"\)/);
  assert.match(source, /args\.firstIndex\(of: "--timeout-ms"\)/);
  assert.match(source, /else \{ return 30_000 \}\n  return min\(120_000, max\(1_000, value\)\)/);
});

test("it type-checks for both app slices at the macOS 12 floor", { skip: process.platform !== "darwin" || !hasSwiftc() }, () => {
  for (const arch of ["arm64", "x86_64"]) {
    execFileSync("swiftc", ["-typecheck", "-target", `${arch}-apple-macos12`, SOURCE], { stdio: "pipe", timeout: 180_000 });
  }
});
```

- [ ] **Step 2: Run it to see it fail**

```bash
cd "$(git -C /Users/omkar/Desktop/openmaus/OpenGrokBot worktree list --porcelain | awk '/^worktree /{w=substr($0,10)} $0=="branch refs/heads/feat/gadget-voice"{print w}')" && test "$(git branch --show-current)" = feat/gadget-voice || { echo 'not in the feat/gadget-voice worktree'; exit 1; }
export PATH="$HOME/.nvm/versions/node/v24.14.1/bin:$PATH"
node --test electron/speech-helper-file-mode.node-test.mjs
```

Expected: `ℹ pass 1`, `ℹ fail 3`. The three source checks fail. The swiftc type-check passes on macOS and is skipped elsewhere.

- [ ] **Step 3: Document the new flags**

In `electron/resources/speech-helper.swift`, replace:

```swift
// `--endpoint-ms N` ends the audio stream after N milliseconds without a
// transcript change.
```

with:

```swift
// `--file PATH` is file mode (gadget turns, server/stt/apple.ts): transcribe
// one WAV instead of the microphone, print exactly one line, and exit. It
// never asks for Speech Recognition permission; without a grant it prints
// {"error":"speech-not-authorized"} so the harness can use its fallback.
// `--timeout-ms N` (file mode only, default 30000, 1000..120000) bounds it.
//
// `--endpoint-ms N` ends the audio stream after N milliseconds without a
// transcript change.
```

- [ ] **Step 4: Parse `--file` and `--timeout-ms` like the existing flags**

Replace (`:48-52`):

```swift
let finishFile: String? = {
  let args = CommandLine.arguments
  guard let index = args.firstIndex(of: "--finish-file"), index + 1 < args.count else { return nil }
  return args[index + 1]
}()
```

with:

```swift
let finishFile: String? = {
  let args = CommandLine.arguments
  guard let index = args.firstIndex(of: "--finish-file"), index + 1 < args.count else { return nil }
  return args[index + 1]
}()

let inputFile: String? = {
  let args = CommandLine.arguments
  guard let index = args.firstIndex(of: "--file"), index + 1 < args.count else { return nil }
  return args[index + 1]
}()

let timeoutMs: Int = {
  let args = CommandLine.arguments
  guard
    let index = args.firstIndex(of: "--timeout-ms"),
    index + 1 < args.count,
    let value = Int(args[index + 1])
  else { return 30_000 }
  return min(120_000, max(1_000, value))
}()
```

- [ ] **Step 5: Add file mode in front of the permission request**

Replace (`:130-141`):

```swift
SFSpeechRecognizer.requestAuthorization { status in
  guard status == .authorized else { fail("speech-not-authorized") }
  // Recognize in the user's language: a hardcoded en-US recognizer
  // transcribes everyone else into nonsense. First preference that has an
  // available recognizer wins, with en-US as the last resort.
  let candidates =
    Locale.preferredLanguages.map { Locale(identifier: $0) }
    + [Locale.current, Locale(identifier: "en-US")]
  guard
    let recognizer = candidates.lazy.compactMap({ SFSpeechRecognizer(locale: $0) })
      .first(where: { $0.isAvailable })
  else { fail("recognizer-unavailable") }
```

with:

```swift
/// Recognize in the user's language: a hardcoded en-US recognizer
/// transcribes everyone else into nonsense. First preference that has an
/// available recognizer wins, with en-US as the last resort.
func preferredRecognizer() -> SFSpeechRecognizer? {
  let candidates =
    Locale.preferredLanguages.map { Locale(identifier: $0) }
    + [Locale.current, Locale(identifier: "en-US")]
  return candidates.lazy.compactMap({ SFSpeechRecognizer(locale: $0) })
    .first(where: { $0.isAvailable })
}

// MARK: file mode
var fileModeTask: SFSpeechRecognitionTask?

/// One WAV in, one NDJSON line out. Checks the existing grant with
/// authorizationStatus() and never calls requestAuthorization(), so it
/// cannot raise a permission prompt. Needs no microphone.
func runFileMode(_ path: String) -> Never {
  guard FileManager.default.isReadableFile(atPath: path) else { fail("file-unreadable") }
  guard SFSpeechRecognizer.authorizationStatus() == .authorized else { fail("speech-not-authorized") }
  guard let recognizer = preferredRecognizer() else { fail("recognizer-unavailable") }

  let request = SFSpeechURLRecognitionRequest(url: URL(fileURLWithPath: path))
  request.shouldReportPartialResults = false
  if recognizer.supportsOnDeviceRecognition {
    request.requiresOnDeviceRecognition = true
  }
  if #available(macOS 13, *) {
    request.addsPunctuation = true
  }

  // A main run loop timer: the watchdog, and also the source that keeps
  // RunLoop.main.run() below from returning before the result arrives.
  RunLoop.main.add(
    Timer(timeInterval: Double(timeoutMs) / 1_000, repeats: false) { _ in fail("timeout") },
    forMode: .common)

  fileModeTask = recognizer.recognitionTask(with: request) { result, error in
    if let result, result.isFinal {
      emit(["partial": false, "text": result.bestTranscription.formattedString])
      exit(0)
    }
    if let error {
      let nsError = error as NSError
      if nsError.domain == "kLSRErrorDomain" && nsError.code == 201 {
        fail("dictation-disabled")
      }
      // "No speech detected": a silent or empty recording, not a failure.
      if nsError.domain == "kAFAssistantErrorDomain" && nsError.code == 1110 {
        fail("no-speech")
      }
      fail("recognition-error")
    }
  }
  RunLoop.main.run()
  fail("recognition-error")
}

if let inputFile {
  runFileMode(inputFile)
}
// end file mode

SFSpeechRecognizer.requestAuthorization { status in
  guard status == .authorized else { fail("speech-not-authorized") }
  guard let recognizer = preferredRecognizer() else { fail("recognizer-unavailable") }
```

How this behaves:
- **Top-level order.** The stop-file timer is already armed above (`:57-66`), so `--stop-file` keeps working in file mode.
- **File mode never returns into dictation.** `runFileMode` is `-> Never`, so the `requestAuthorization` call below never runs in file mode.
- **The watchdog** is a main-run-loop `Timer`. That also keeps `RunLoop.main.run()` from returning before the result arrives.
- **Silence.** `kAFAssistantErrorDomain` code 1110 is the recognizer's "No speech detected". Task 7 maps `no-speech` to an empty transcript.

- [ ] **Step 6: Run the test, rebuild the helper, and check the no-prompt path for real (macOS)**

```bash
cd "$(git -C /Users/omkar/Desktop/openmaus/OpenGrokBot worktree list --porcelain | awk '/^worktree /{w=substr($0,10)} $0=="branch refs/heads/feat/gadget-voice"{print w}')" && test "$(git branch --show-current)" = feat/gadget-voice || { echo 'not in the feat/gadget-voice worktree'; exit 1; }
export PATH="$HOME/.nvm/versions/node/v24.14.1/bin:$PATH"
node --test electron/speech-helper-file-mode.node-test.mjs
pnpm build:speech
lipo -archs "electron/resources/OpenMausBot Speech.app/Contents/MacOS/speech-helper"
D=$(mktemp -d /private/tmp/omb-p3b-helper.XXXX)
say -o "$D/hello.wav" --data-format=LEI16@16000 "What is on my calendar today"
: > "$D/out.ndjson"
/usr/bin/open -n -g -W -o "$D/out.ndjson" --stderr "$D/err.log" "$PWD/electron/resources/OpenMausBot Speech.app" --args --file "$D/hello.wav" --stop-file "$D/stop" --timeout-ms 20000
cat "$D/out.ndjson"
: > "$D/out.ndjson"
/usr/bin/open -n -g -W -o "$D/out.ndjson" --stderr "$D/err.log" "$PWD/electron/resources/OpenMausBot Speech.app" --args --file "$D/missing.wav" --stop-file "$D/stop"
cat "$D/out.ndjson"
```

Expected:
- `ℹ pass 4`, `ℹ fail 0`.
- `pnpm build:speech` ends with codesign output and no error, and `lipo -archs` prints `x86_64 arm64`.
- The first helper run prints exactly one line: either `{"partial":false,"text":"What is on my calendar today?"}` (this Mac already lets the dev helper use Speech Recognition) or `{"error":"speech-not-authorized"}`.
- No permission dialog appears in either case. `open` may print "Unable to block on application"; that is expected and harmless.
- The second run prints `{"error":"file-unreadable"}`.
- Write down which first line you saw; Task 13 reports it.

The bundle under `electron/resources/` is gitignored.

- [ ] **Step 7: Commit**

```bash
cd "$(git -C /Users/omkar/Desktop/openmaus/OpenGrokBot worktree list --porcelain | awk '/^worktree /{w=substr($0,10)} $0=="branch refs/heads/feat/gadget-voice"{print w}')" && test "$(git branch --show-current)" = feat/gadget-voice || { echo 'not in the feat/gadget-voice worktree'; exit 1; }
git add electron/resources/speech-helper.swift electron/speech-helper-file-mode.node-test.mjs
git commit -m "feat(speech): helper file mode for gadget turns that never prompts" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 5: ElevenLabs Scribe v2 (`server/stt/scribe.ts`)

**Files:**
- Create: `server/stt/scribe.ts`
- Create: `server/stt/scribe.test.ts`

**Interfaces:**
- Consumes: `ServiceCredential` = `{ token; api; included }` (`server/included-services.ts:26-32`).
- Produces, used by Task 6:
  - `type ScribeResult = { ok: true; text: string } | { ok: false; status: number; forbidden: boolean; message: string }`
  - `transcribeWithScribe(pcm: Uint8Array, cred: ServiceCredential, options?: { fetch?: typeof fetch; timeoutMs?: number }): Promise<ScribeResult>`
- Behavior:
  - It sends exactly the pinned request, `POST ${cred.api}/speech-to-text?enable_logging=false` (contract §3.14), once. Any refusal is a failure, and the audio is never sent twice.
  - D-P3b-1 may change that, but only after Omkar answers (Step 1 asks; Step 7 applies the answer).
  - `forbidden` is true for a 401 or 403.
  - The message never echoes the provider's body.

- [ ] **Step 1: STOP checkpoint for D-P3b-1 (contract §0 item 2): ask Omkar**

Contract §3.14 pins `?enable_logging=false` on the Scribe request. ElevenLabs documents zero-retention mode as something that "may only be used by enterprise customers", so another account may have the pinned request refused, and Scribe is the only fallback off a Mac. Contract §0 item 2 says a deviation stops for review on that item, so ask Omkar now: through whoever dispatched this plan, or in a direct session with `mcp__spokenly__ask_user_dictation`, as his CLAUDE.md requires. Ask exactly:

> D-P3b-1: ElevenLabs says zero-retention mode (`enable_logging=false`) is for enterprise accounts only. For a gadget's speech-to-text fallback, should P3b (A) keep `enable_logging=false` and, only when ElevenLabs refuses it by name, retry once without it; or (B) drop the parameter and send the audio with normal retention, like the TTS text the app already sends; or (C) keep exactly the pinned request? Until you answer, P3b sends the pinned request, once.

Then build only the pinned request (Steps 2–6): no retry, no other query. Do not guess an answer. Step 7 applies his answer whenever it arrives, even after later tasks, and Task 13 Step 5 checks that it was applied or lists it as open.

- [ ] **Step 2: Write the failing test**

Create `server/stt/scribe.test.ts`:

```ts
// ElevenLabs Scribe v2 (spec §6.3, amendment A11), through the fetch seam:
// what we send, and how each refusal is reported.
import { describe, expect, it } from "vitest";

import type { ServiceCredential } from "../included-services.ts";
import { transcribeWithScribe } from "./scribe.ts";

const cred: ServiceCredential = { token: "el-key", api: "https://api.elevenlabs.io/v1", included: false };
const pcm = new Uint8Array([1, 0, 2, 0, 3, 0]);

type Sent = { url: string; key: string | null; fields: Record<string, string>; file: Uint8Array | null };

function fakeFetch(answer: (sent: Sent, n: number) => Response | Promise<Response>) {
  const sent: Sent[] = [];
  const fn = (async (input: string | URL | Request, init?: RequestInit) => {
    const form = init!.body as FormData;
    const fields: Record<string, string> = {};
    let file: Uint8Array | null = null;
    for (const [name, value] of form.entries()) {
      if (typeof value === "string") fields[name] = value;
      else file = new Uint8Array(await value.arrayBuffer());
    }
    const call = { url: String(input), key: new Headers(init!.headers).get("xi-api-key"), fields, file };
    sent.push(call);
    return answer(call, sent.length);
  }) as typeof fetch;
  return { fn, sent };
}

const json = (status: number, body: unknown) => new Response(JSON.stringify(body), { status, headers: { "content-type": "application/json" } });

describe("transcribeWithScribe", () => {
  it("sends raw 16 kHz PCM to scribe_v2 with the key as a header", async () => {
    const f = fakeFetch(() => json(200, { text: " Hello there. ", language_code: "en" }));
    expect(await transcribeWithScribe(pcm, cred, { fetch: f.fn })).toEqual({ ok: true, text: "Hello there." });
    expect(f.sent[0]).toEqual({
      url: "https://api.elevenlabs.io/v1/speech-to-text?enable_logging=false",
      key: "el-key",
      fields: { model_id: "scribe_v2", file_format: "pcm_s16le_16", tag_audio_events: "false" },
      file: pcm,
    });
    expect(f.sent[0]!.url).not.toContain("el-key");
  });

  it("reports a plain refusal as a failure after exactly one request", async () => {
    const f = fakeFetch(() => json(400, { detail: "bad request" }));
    expect(await transcribeWithScribe(pcm, cred, { fetch: f.fn })).toEqual({ ok: false, status: 400, forbidden: false, message: "ElevenLabs Speech to Text failed (400)" });
    expect(f.sent).toHaveLength(1);
  });

  it("sends the pinned zero-retention request once, even when it is refused (D-P3b-1)", async () => {
    const f = fakeFetch(() => json(400, { detail: { message: "Zero retention mode is only available to enterprise customers" } }));
    expect(await transcribeWithScribe(pcm, cred, { fetch: f.fn })).toMatchObject({ ok: false, status: 400, forbidden: false });
    expect(f.sent.map((s) => s.url)).toEqual([`${cred.api}/speech-to-text?enable_logging=false`]);
  });

  it("reports a key without the Speech to Text permission as forbidden, without retrying", async () => {
    for (const status of [401, 403]) {
      const f = fakeFetch(() => json(status, { detail: { status: "missing_permissions" } }));
      expect(await transcribeWithScribe(pcm, cred, { fetch: f.fn })).toEqual({ ok: false, status, forbidden: true, message: `ElevenLabs Speech to Text failed (${status})` });
      expect(f.sent).toHaveLength(1);
    }
  });

  it("never echoes the provider's error body", async () => {
    const f = fakeFetch(() => json(500, { detail: "el-key leaked in an error" }));
    const out = await transcribeWithScribe(pcm, cred, { fetch: f.fn });
    expect(out).toEqual({ ok: false, status: 500, forbidden: false, message: "ElevenLabs Speech to Text failed (500)" });
  });

  it("calls a 200 without a transcript a failure", async () => {
    const f = fakeFetch(() => new Response("<html>", { status: 200 }));
    expect(await transcribeWithScribe(pcm, cred, { fetch: f.fn })).toMatchObject({ ok: false, status: 502, forbidden: false });
  });

  it("gives up at its deadline", async () => {
    const hang = (async (_input: string | URL | Request, init?: RequestInit) => new Promise<Response>((_resolve, reject) => {
      init!.signal!.addEventListener("abort", () => reject(init!.signal!.reason));
    })) as typeof fetch;
    expect(await transcribeWithScribe(pcm, cred, { fetch: hang, timeoutMs: 20 })).toEqual({ ok: false, status: 0, forbidden: false, message: "ElevenLabs Speech to Text timed out" });
  });
});
```

- [ ] **Step 3: Run it to see it fail**

```bash
cd "$(git -C /Users/omkar/Desktop/openmaus/OpenGrokBot worktree list --porcelain | awk '/^worktree /{w=substr($0,10)} $0=="branch refs/heads/feat/gadget-voice"{print w}')" && test "$(git branch --show-current)" = feat/gadget-voice || { echo 'not in the feat/gadget-voice worktree'; exit 1; }
export PATH="$HOME/.nvm/versions/node/v24.14.1/bin:$PATH"
pnpm exec vitest run server/stt/scribe.test.ts
```

Expected: FAIL with `Error: Cannot find module './scribe.ts' imported from …/server/stt/scribe.test.ts`.

- [ ] **Step 4: Implement `server/stt/scribe.ts`**

```ts
// ElevenLabs Scribe v2 (spec §6.3): the fallback when Apple's recognizer is
// missing, not allowed, or fails, and the only provider off macOS. The key
// is the person's own (voiceCredential(cfg.tts?.key)); the gadget's mic
// format is exactly Scribe's pcm_s16le_16, so the PCM goes up as is.
import type { ServiceCredential } from "../included-services.ts";

export type ScribeResult = { ok: true; text: string } | { ok: false; status: number; forbidden: boolean; message: string };

/** Zero-retention mode, as contract §3.14 pins it. ElevenLabs documents it
 * for enterprise accounts only; what to do for other accounts is D-P3b-1,
 * Omkar's call (Task 5 Step 1). Until he answers, this is the pinned
 * request, sent once. */
const SCRIBE_QUERY = "?enable_logging=false";

export async function transcribeWithScribe(
  pcm: Uint8Array,
  cred: ServiceCredential,
  options: { fetch?: typeof fetch; timeoutMs?: number } = {},
): Promise<ScribeResult> {
  const doFetch = options.fetch ?? fetch;
  // one deadline for the whole call, so it stays inside /api/stt's budget
  const signal = AbortSignal.timeout(options.timeoutMs ?? 30_000);
  const send = (query: string) => {
    const form = new FormData();
    form.set("model_id", "scribe_v2");
    form.set("file_format", "pcm_s16le_16");
    form.set("tag_audio_events", "false");
    form.set("file", new Blob([new Uint8Array(pcm)], { type: "application/octet-stream" }), "utterance.pcm");
    return doFetch(`${cred.api}/speech-to-text${query}`, { method: "POST", headers: { "xi-api-key": cred.token }, body: form, signal });
  };
  try {
    const res = await send(SCRIBE_QUERY);
    if (!res.ok) return failure(res.status);
    const parsed = (await res.json().catch(() => null)) as { text?: unknown } | null;
    if (typeof parsed?.text !== "string") return { ok: false, status: 502, forbidden: false, message: "ElevenLabs Speech to Text returned no transcript" };
    return { ok: true, text: parsed.text.trim() };
  } catch (e) {
    const timedOut = e instanceof Error && (e.name === "TimeoutError" || e.name === "AbortError");
    return { ok: false, status: 0, forbidden: false, message: timedOut ? "ElevenLabs Speech to Text timed out" : "Couldn't reach ElevenLabs Speech to Text" };
  }
}

/** Never echoes the provider's body: errors can repeat request details. */
function failure(status: number): ScribeResult {
  return { ok: false, status, forbidden: status === 401 || status === 403, message: `ElevenLabs Speech to Text failed (${status})` };
}
```

- [ ] **Step 5: Run the tests and the typecheck**

```bash
cd "$(git -C /Users/omkar/Desktop/openmaus/OpenGrokBot worktree list --porcelain | awk '/^worktree /{w=substr($0,10)} $0=="branch refs/heads/feat/gadget-voice"{print w}')" && test "$(git branch --show-current)" = feat/gadget-voice || { echo 'not in the feat/gadget-voice worktree'; exit 1; }
export PATH="$HOME/.nvm/versions/node/v24.14.1/bin:$PATH"
pnpm exec vitest run server/stt/scribe.test.ts
pnpm exec tsc -p tsconfig.server.json
```

Expected: `Tests  7 passed (7)`, and `tsc` exits 0.

- [ ] **Step 6: Commit**

```bash
cd "$(git -C /Users/omkar/Desktop/openmaus/OpenGrokBot worktree list --porcelain | awk '/^worktree /{w=substr($0,10)} $0=="branch refs/heads/feat/gadget-voice"{print w}')" && test "$(git branch --show-current)" = feat/gadget-voice || { echo 'not in the feat/gadget-voice worktree'; exit 1; }
git add server/stt/scribe.ts server/stt/scribe.test.ts
git commit -m "feat(stt): ElevenLabs Scribe v2 for gadget speech" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

- [ ] **Step 7: Apply Omkar's D-P3b-1 answer (when it arrives)**

Skip this step until Omkar has answered Step 1's question. Then apply exactly one of these.

**(C) Keep exactly the pinned request.** Change nothing. Write his answer in the Task 13 hand-off.

**(A) Keep `enable_logging=false`, with one retry when the refusal names it.** The retry runs only for a 400, 401, 403 or 422 whose body matches `/retention|enable_logging|logging/i`, inside the same 30 s deadline. A refusal that has nothing to do with retention never re-sends the audio, so an account that can use zero retention never loses it by accident.

In `server/stt/scribe.ts`, replace:

```ts
/** Zero-retention mode, as contract §3.14 pins it. ElevenLabs documents it
 * for enterprise accounts only; what to do for other accounts is D-P3b-1,
 * Omkar's call (Task 5 Step 1). Until he answers, this is the pinned
 * request, sent once. */
const SCRIBE_QUERY = "?enable_logging=false";
```

with:

```ts
/** Zero-retention mode, as contract §3.14 pins it. ElevenLabs documents it
 * for enterprise accounts only, so (D-P3b-1, approved by Omkar) a refusal
 * that names it gets one retry without the parameter. */
const SCRIBE_QUERY = "?enable_logging=false";
const RETRY_STATUSES = new Set([400, 401, 403, 422]);
const NAMES_RETENTION = /retention|enable_logging|logging/i;

async function bodyText(res: Response): Promise<string> {
  try {
    return (await res.text()).slice(0, 2_000);
  } catch {
    return "";
  }
}
```

In `server/stt/scribe.ts`, replace:

```ts
    const res = await send(SCRIBE_QUERY);
    if (!res.ok) return failure(res.status);
```

with:

```ts
    let res = await send(SCRIBE_QUERY);
    // Only a refusal that names retention or logging is retried, once, inside
    // the same deadline. Any other refusal never re-sends the audio.
    if (!res.ok && RETRY_STATUSES.has(res.status) && NAMES_RETENTION.test(await bodyText(res))) res = await send("");
    if (!res.ok) return failure(res.status);
```

In `server/stt/scribe.test.ts`, replace:

```ts
  it("sends the pinned zero-retention request once, even when it is refused (D-P3b-1)", async () => {
    const f = fakeFetch(() => json(400, { detail: { message: "Zero retention mode is only available to enterprise customers" } }));
    expect(await transcribeWithScribe(pcm, cred, { fetch: f.fn })).toMatchObject({ ok: false, status: 400, forbidden: false });
    expect(f.sent.map((s) => s.url)).toEqual([`${cred.api}/speech-to-text?enable_logging=false`]);
  });
```

with:

```ts
  it("retries once without zero-retention mode when the refusal names it (D-P3b-1)", async () => {
    for (const refusal of [
      json(400, { detail: { message: "Zero retention mode is only available to enterprise customers" } }),
      json(401, { detail: { message: "enable_logging=false requires an enterprise plan" } }),
      json(403, { detail: { message: "Zero retention mode is enterprise only" } }),
      json(422, { detail: [{ loc: ["query", "enable_logging"], msg: "zero retention is not available" }] }),
    ]) {
      const f = fakeFetch((_s, n) => (n === 1 ? refusal.clone() : json(200, { text: "ok" })));
      expect(await transcribeWithScribe(pcm, cred, { fetch: f.fn })).toEqual({ ok: true, text: "ok" });
      expect(f.sent.map((s) => s.url)).toEqual([`${cred.api}/speech-to-text?enable_logging=false`, `${cred.api}/speech-to-text`]);
    }
  });
```

**(B) Drop the parameter.** This changes a request the contract pins, so also tell Omkar that contract §3.14's Scribe line needs the same change; this plan does not edit the contract.

In `server/stt/scribe.ts`, replace:

```ts
/** Zero-retention mode, as contract §3.14 pins it. ElevenLabs documents it
 * for enterprise accounts only; what to do for other accounts is D-P3b-1,
 * Omkar's call (Task 5 Step 1). Until he answers, this is the pinned
 * request, sent once. */
const SCRIBE_QUERY = "?enable_logging=false";
```

with:

```ts
/** D-P3b-1, decided by Omkar: normal retention, the same as the TTS text
 * the app already sends, so no account refuses the request over its
 * retention mode. (Contract §3.14 had pinned enable_logging=false.) */
const SCRIBE_QUERY = "";
```

In `server/stt/scribe.test.ts`, replace:

```ts
      url: "https://api.elevenlabs.io/v1/speech-to-text?enable_logging=false",
```

with:

```ts
      url: "https://api.elevenlabs.io/v1/speech-to-text",
```

In `server/stt/scribe.test.ts`, replace:

```ts
  it("sends the pinned zero-retention request once, even when it is refused (D-P3b-1)", async () => {
    const f = fakeFetch(() => json(400, { detail: { message: "Zero retention mode is only available to enterprise customers" } }));
    expect(await transcribeWithScribe(pcm, cred, { fetch: f.fn })).toMatchObject({ ok: false, status: 400, forbidden: false });
    expect(f.sent.map((s) => s.url)).toEqual([`${cred.api}/speech-to-text?enable_logging=false`]);
  });
```

with:

```ts
  it("never asks for zero-retention mode (D-P3b-1)", async () => {
    const f = fakeFetch(() => json(200, { text: "ok" }));
    expect(await transcribeWithScribe(pcm, cred, { fetch: f.fn })).toEqual({ ok: true, text: "ok" });
    expect(f.sent.map((s) => s.url)).toEqual([`${cred.api}/speech-to-text`]);
  });
```

Also make this change in `server/routes/stt.test.ts`: in the committed file when Task 6 is done, or in Task 6 Step 1's text as you create the file when it is not:

In `server/routes/stt.test.ts`, replace:

```ts
    expect(call.url).toBe("/v1/speech-to-text?enable_logging=false");
```

with:

```ts
    expect(call.url).toBe("/v1/speech-to-text");
```

Run the checks (leave out `server/routes/stt.test.ts` if Task 6 is not done yet):

```bash
cd "$(git -C /Users/omkar/Desktop/openmaus/OpenGrokBot worktree list --porcelain | awk '/^worktree /{w=substr($0,10)} $0=="branch refs/heads/feat/gadget-voice"{print w}')" && test "$(git branch --show-current)" = feat/gadget-voice || { echo 'not in the feat/gadget-voice worktree'; exit 1; }
export PATH="$HOME/.nvm/versions/node/v24.14.1/bin:$PATH"
pnpm exec vitest run server/stt/scribe.test.ts server/routes/stt.test.ts
pnpm exec tsc -p tsconfig.server.json
```

Expected: 0 failed, and `scribe.test.ts` still has 7 tests. With both files after Task 8: `Test Files  2 passed (2)` and `Tests  22 passed (22)`. `tsc` exits 0.

Commit (for A or B):

```bash
cd "$(git -C /Users/omkar/Desktop/openmaus/OpenGrokBot worktree list --porcelain | awk '/^worktree /{w=substr($0,10)} $0=="branch refs/heads/feat/gadget-voice"{print w}')" && test "$(git branch --show-current)" = feat/gadget-voice || { echo 'not in the feat/gadget-voice worktree'; exit 1; }
git add server/stt/scribe.ts server/stt/scribe.test.ts
test -f server/routes/stt.test.ts && git add server/routes/stt.test.ts
git commit -m "feat(stt): apply D-P3b-1 to the Scribe request, as Omkar decided" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 6: The `POST /api/stt` route module

**Files:**
- Create: `server/routes/stt.ts`
- Create: `server/routes/stt.test.ts` (Task 8 appends a registration check)

**Interfaces:**
- Consumes:
  - `readWav` (Task 1) and `transcribeWithScribe` (Task 5);
  - `PASS`, `RouteHandler` and `dispatchRoutes` (`server/routes/table.ts`);
  - `json` and `readBody` (`server/harness/http.ts`, used in the test);
  - `ServiceCredential`.
- Produces (contract §3.14), used by Tasks 7 and 8:
  - `STT_MAX_BYTES = 2 * 1024 * 1024`
  - `type SttProvider = "apple" | "elevenlabs"`
  - `type AppleSttResult`
  - `interface SttRouteDeps { elevenLabs(); apple?; fetch?; tmpDir? }`
  - `createSttRoutes(deps: SttRouteDeps): RouteHandler`
  - `STT_UNAVAILABLE_MESSAGE` and `STT_KEY_FORBIDDEN_MESSAGE`
  - `pcmFromWav16k(wav: Uint8Array): Buffer | null`
- Answers:

  | Case | Answer |
  |---|---|
  | transcribed | 200 `{text, provider}` |
  | not a mono 16-bit 16 kHz WAV of ≤ 60 s, or the wrong content type | 400 `{error: "bad_audio", message}` |
  | body over 2 MiB | 413 `{error: "too_large"}` |
  | neither provider set up (no `apple` and no key, even for a silent clip) | 409 `{error: "stt_unavailable", message: STT_UNAVAILABLE_MESSAGE}` |
  | the ElevenLabs key lacks Speech to Text | 502 `{error: "stt_key_forbidden", message: STT_KEY_FORBIDDEN_MESSAGE}` |
  | a set-up provider failed this time | 502 `{error: "stt_failed", message}` |

- Rules:
  - **`apple` is read once per request** (`const apple = deps.apple;` at the top of the handler). Task 8 registers it as a getter that is undefined off macOS or without a helper bundle, as contract §3.14 says.
  - **Silence gate.** Once some provider is set up, audio shorter than 100 ms (Scribe's documented minimum), or with a peak below 200 (about -44 dBFS), is answered `{text: ""}` before any provider runs. A peak of exactly 200 reaches the provider (a boundary test pins it), and Task 13 adds a per-board check that real speech peaks well above it.
  - **Which Apple errors mean "not set up".** `speech-not-authorized`, `recognizer-unavailable` and `dictation-disabled` with no key → 409. `timeout`, `recognition-error`, `file-unreadable` and `helper-failed` (a helper that crashed) with no key → 502 `stt_failed`. A missing bundle never reaches here: `apple` is undefined then.

- [ ] **Step 1: Write the failing test**

Create `server/routes/stt.test.ts`:

```ts
// POST /api/stt (spec §6.3), the way a request reaches it: through the route
// table on a real HTTP server. Apple's helper is a stand-in function (its
// own test drives the launcher); ElevenLabs is a local stub, as in
// server/tts/tts.test.ts.
import { Buffer } from "node:buffer";
import { existsSync, readFileSync } from "node:fs";
import { createServer, request as httpRequest, type Server } from "node:http";
import type { AddressInfo } from "node:net";
import { afterEach, beforeAll, describe, expect, it, vi } from "vitest";

import { json, readBody } from "../harness/http.ts";
import type { ServiceCredential } from "../included-services.ts";
import { createSttRoutes, pcmFromWav16k, STT_KEY_FORBIDDEN_MESSAGE, STT_MAX_BYTES, STT_UNAVAILABLE_MESSAGE, type AppleSttResult, type SttRouteDeps } from "./stt.ts";
import { dispatchRoutes } from "./table.ts";

const servers: Server[] = [];
afterEach(async () => {
  await Promise.all(servers.splice(0).map((server) => new Promise((done) => server.close(done))));
});

async function listen(server: Server): Promise<string> {
  servers.push(server);
  await new Promise<void>((ready) => server.listen(0, "127.0.0.1", ready));
  return `http://127.0.0.1:${(server.address() as AddressInfo).port}`;
}

async function serve(deps: SttRouteDeps): Promise<string> {
  const routes = [createSttRoutes(deps)];
  return listen(createServer(async (req, res) => {
    const url = new URL(req.url ?? "/", "http://localhost");
    const handled = await dispatchRoutes(routes, {
      req, res, url, path: url.pathname, method: req.method ?? "GET",
      auth: { kind: "loopback", scopes: ["admin", "client"] }, json, readBody,
    });
    if (!handled) json(res, 404, { from: "inline routes" });
  }));
}

/** Mono PCM16 WAV: a 440 Hz tone (speech-like level) or silence. */
function wav16k(seconds: number, amplitude = 8000, rate = 16000, channels = 1): Buffer {
  const frames = Math.round(rate * seconds);
  const data = Buffer.alloc(frames * channels * 2);
  for (let i = 0; i < frames; i++) {
    const v = Math.round(amplitude * Math.sin((2 * Math.PI * 440 * i) / rate));
    for (let c = 0; c < channels; c++) data.writeInt16LE(v, (i * channels + c) * 2);
  }
  const head = Buffer.alloc(44);
  head.write("RIFF", 0, "ascii");
  head.writeUInt32LE(36 + data.byteLength, 4);
  head.write("WAVEfmt ", 8, "ascii");
  head.writeUInt32LE(16, 16);
  head.writeUInt16LE(1, 20);
  head.writeUInt16LE(channels, 22);
  head.writeUInt32LE(rate, 24);
  head.writeUInt32LE(rate * channels * 2, 28);
  head.writeUInt16LE(channels * 2, 32);
  head.writeUInt16LE(16, 34);
  head.write("data", 36, "ascii");
  head.writeUInt32LE(data.byteLength, 40);
  return Buffer.concat([head, data]);
}

const SPEECH = wav16k(0.5);

async function post(base: string, body: Uint8Array, type = "audio/wav"): Promise<{ status: number; body: any }> {
  const res = await fetch(`${base}/api/stt`, { method: "POST", headers: { "content-type": type }, body });
  return { status: res.status, body: await res.json() };
}

// ── the ElevenLabs stub ─────────────────────────────────────────────────
type ScribeCall = { url: string; key: string | undefined; fields: Record<string, string>; file: Buffer | null };
const scribeCalls: ScribeCall[] = [];
let scribeAnswer: (call: ScribeCall) => { status: number; body: unknown } = () => ({ status: 200, body: { text: " what's on my calendar " } });
let scribeBase = "";

beforeAll(async () => {
  const stub = createServer((req, res) => {
    const chunks: Buffer[] = [];
    req.on("data", (c: Buffer) => chunks.push(c));
    req.on("end", async () => {
      const form = await new Response(Buffer.concat(chunks), { headers: { "content-type": String(req.headers["content-type"]) } }).formData();
      const fields: Record<string, string> = {};
      let file: Buffer | null = null;
      for (const [name, value] of form.entries()) {
        if (typeof value === "string") fields[name] = value;
        else file = Buffer.from(await value.arrayBuffer());
      }
      const call = { url: req.url ?? "", key: req.headers["xi-api-key"] as string | undefined, fields, file };
      scribeCalls.push(call);
      const answer = scribeAnswer(call);
      res.writeHead(answer.status, { "content-type": "application/json" });
      res.end(JSON.stringify(answer.body));
    });
  });
  await new Promise<void>((ready) => stub.listen(0, "127.0.0.1", ready));
  scribeBase = `http://127.0.0.1:${(stub.address() as AddressInfo).port}/v1`;
  // the stub lives for the whole file; it is not in `servers`
});

const ownKey = (): ServiceCredential => ({ token: "el-key", api: scribeBase, included: false });
const apple = (result: AppleSttResult) => vi.fn(async (_wavPath: string) => result);

afterEach(() => {
  scribeCalls.length = 0;
  scribeAnswer = () => ({ status: 200, body: { text: " what's on my calendar " } });
});

describe("POST /api/stt", () => {
  it("uses Apple's recognizer first, on the WAV exactly as uploaded, and cleans up the file", async () => {
    let seenPath = "";
    let seenBytes = Buffer.alloc(0);
    const helper = vi.fn(async (wavPath: string): Promise<AppleSttResult> => {
      seenPath = wavPath;
      seenBytes = readFileSync(wavPath);
      return { ok: true, text: "What's on my calendar?" };
    });
    const base = await serve({ elevenLabs: ownKey, apple: helper });
    expect(await post(base, SPEECH)).toEqual({ status: 200, body: { text: "What's on my calendar?", provider: "apple" } });
    expect(seenBytes).toEqual(SPEECH);
    expect(existsSync(seenPath)).toBe(false);
    expect(scribeCalls).toHaveLength(0);
  });

  it("falls back to Scribe v2 with raw 16 kHz PCM when Speech Recognition is not allowed", async () => {
    const base = await serve({ elevenLabs: ownKey, apple: apple({ ok: false, error: "speech-not-authorized" }) });
    expect(await post(base, SPEECH)).toEqual({ status: 200, body: { text: "what's on my calendar", provider: "elevenlabs" } });
    const call = scribeCalls[0]!;
    expect(call.url).toBe("/v1/speech-to-text?enable_logging=false");
    expect(call.key).toBe("el-key");
    expect(call.fields).toEqual({ model_id: "scribe_v2", file_format: "pcm_s16le_16", tag_audio_events: "false" });
    expect(call.file).toEqual(SPEECH.subarray(44));       // the PCM, not the WAV
  });

  it("uses Scribe alone off macOS (no apple dependency)", async () => {
    const base = await serve({ elevenLabs: ownKey });
    expect((await post(base, SPEECH)).body).toEqual({ text: "what's on my calendar", provider: "elevenlabs" });
  });

  it("names the key's Speech to Text permission on a 401 or 403", async () => {
    for (const status of [401, 403]) {
      scribeAnswer = () => ({ status, body: { detail: { status: "missing_permissions" } } });
      const base = await serve({ elevenLabs: ownKey });
      expect(await post(base, SPEECH)).toEqual({ status: 502, body: { error: "stt_key_forbidden", message: STT_KEY_FORBIDDEN_MESSAGE } });
    }
  });

  it("answers 409 stt_unavailable when neither provider is set up", async () => {
    const none = await serve({ elevenLabs: () => null });
    expect(await post(none, SPEECH)).toEqual({ status: 409, body: { error: "stt_unavailable", message: STT_UNAVAILABLE_MESSAGE } });
    const notAllowed = await serve({ elevenLabs: () => null, apple: apple({ ok: false, error: "speech-not-authorized" }) });
    expect((await post(notAllowed, SPEECH)).status).toBe(409);
    const relay = await serve({ elevenLabs: () => ({ token: "t", api: scribeBase, included: true }) });
    scribeAnswer = () => ({ status: 404, body: {} });
    expect((await post(relay, SPEECH)).body.error).toBe("stt_unavailable");
  });

  it("answers 409 for a silent clip too when neither provider is set up (no helper bundle, no key)", async () => {
    const none = await serve({ elevenLabs: () => null });
    expect(await post(none, wav16k(1, 0))).toEqual({ status: 409, body: { error: "stt_unavailable", message: STT_UNAVAILABLE_MESSAGE } });
    expect(await post(none, wav16k(0.05))).toEqual({ status: 409, body: { error: "stt_unavailable", message: STT_UNAVAILABLE_MESSAGE } });
  });

  it("answers 502 stt_failed when a set-up provider fails this time", async () => {
    const timedOut = await serve({ elevenLabs: () => null, apple: apple({ ok: false, error: "timeout" }) });
    expect(await post(timedOut, SPEECH)).toEqual({ status: 502, body: { error: "stt_failed", message: "Speech-to-text failed. Try again." } });
    // a helper that crashed failed this time; it is not "isn't set up"
    const crashed = await serve({ elevenLabs: () => null, apple: apple({ ok: false, error: "helper-failed" }) });
    expect(await post(crashed, SPEECH)).toEqual({ status: 502, body: { error: "stt_failed", message: "Speech-to-text failed. Try again." } });
    scribeAnswer = () => ({ status: 500, body: { detail: "boom el-key" } });
    const scribeDown = await serve({ elevenLabs: ownKey });
    const res = await post(scribeDown, SPEECH);
    expect(res.body.error).toBe("stt_failed");
    expect(JSON.stringify(res.body)).not.toContain("el-key");
  });

  it("answers a silent or too-short WAV with empty text, without calling a provider", async () => {
    const helper = apple({ ok: true, text: "should not run" });
    const base = await serve({ elevenLabs: ownKey, apple: helper });
    expect(await post(base, wav16k(1, 0))).toEqual({ status: 200, body: { text: "", provider: "apple" } });
    expect(await post(base, wav16k(1, 150))).toEqual({ status: 200, body: { text: "", provider: "apple" } });
    expect(await post(base, wav16k(0.05))).toEqual({ status: 200, body: { text: "", provider: "apple" } });
    expect(helper).not.toHaveBeenCalled();
    expect(scribeCalls).toHaveLength(0);
  });

  it("sends a quiet clip that peaks at exactly 200 to the provider, and gates one that peaks at 199", async () => {
    const helper = apple({ ok: true, text: "quiet words" });
    const base = await serve({ elevenLabs: ownKey, apple: helper });
    expect(await post(base, wav16k(1, 199))).toEqual({ status: 200, body: { text: "", provider: "apple" } });
    expect(helper).not.toHaveBeenCalled();
    expect(await post(base, wav16k(1, 200))).toEqual({ status: 200, body: { text: "quiet words", provider: "apple" } });
    expect(helper).toHaveBeenCalledTimes(1);
  });

  it("accepts a full 60 s utterance, well past readBody's 1 MB", async () => {
    const minute = wav16k(60);
    expect(minute.byteLength).toBe(1_920_044);
    const helper = apple({ ok: true, text: "a long one" });
    const base = await serve({ elevenLabs: () => null, apple: helper });
    expect(await post(base, minute)).toEqual({ status: 200, body: { text: "a long one", provider: "apple" } });
  });

  it("refuses anything but mono 16-bit 16 kHz WAV up to 60 s", async () => {
    const base = await serve({ elevenLabs: ownKey });
    for (const [body, type] of [
      [SPEECH, "audio/mpeg"],
      [Buffer.from("not a wav"), "audio/wav"],
      [wav16k(0.5, 8000, 24000), "audio/wav"],
      [wav16k(0.5, 8000, 16000, 2), "audio/wav"],
      [wav16k(60.01), "audio/wav"],
    ] as const) {
      const res = await post(base, body, type);
      expect(res.status, `${type} ${body.byteLength}`).toBe(400);
      expect(res.body.error).toBe("bad_audio");
    }
    expect(scribeCalls).toHaveLength(0);
  });

  it("answers 413 above the cap, by content-length and when streamed", async () => {
    const base = await serve({ elevenLabs: ownKey });
    expect(await post(base, Buffer.alloc(STT_MAX_BYTES + 1))).toEqual({ status: 413, body: { error: "too_large" } });
    // chunked: no content-length, so the cap is enforced while reading
    const status = await new Promise<number>((resolve, reject) => {
      const req = httpRequest(`${base}/api/stt`, { method: "POST", headers: { "content-type": "audio/wav", "transfer-encoding": "chunked" } }, (res) => {
        res.resume();
        resolve(res.statusCode ?? 0);
      });
      req.on("error", reject);
      const chunk = Buffer.alloc(256 * 1024);
      for (let i = 0; i < 10; i++) req.write(chunk);
      req.end();
    });
    expect(status).toBe(413);
  });

  it("keeps its copy short enough and Latin-1 for the gadget's done reason", () => {
    for (const copy of [STT_UNAVAILABLE_MESSAGE, STT_KEY_FORBIDDEN_MESSAGE]) {
      expect(Buffer.byteLength(copy, "utf8")).toBeLessThanOrEqual(159);
      expect(/^[\x20-\x7e]*$/.test(copy)).toBe(true);
    }
  });
});

describe("pcmFromWav16k", () => {
  it("returns the PCM of a valid WAV and null for anything else", () => {
    expect(pcmFromWav16k(SPEECH)).toEqual(SPEECH.subarray(44));
    expect(pcmFromWav16k(wav16k(0.1, 8000, 22050))).toBeNull();
    expect(pcmFromWav16k(Buffer.from("RIFF"))).toBeNull();
  });
});
```

- [ ] **Step 2: Run it to see it fail**

```bash
cd "$(git -C /Users/omkar/Desktop/openmaus/OpenGrokBot worktree list --porcelain | awk '/^worktree /{w=substr($0,10)} $0=="branch refs/heads/feat/gadget-voice"{print w}')" && test "$(git branch --show-current)" = feat/gadget-voice || { echo 'not in the feat/gadget-voice worktree'; exit 1; }
export PATH="$HOME/.nvm/versions/node/v24.14.1/bin:$PATH"
pnpm exec vitest run server/routes/stt.test.ts
```

Expected: FAIL with `Error: Cannot find module './stt.ts' imported from …/server/routes/stt.test.ts`.

- [ ] **Step 3: Implement `server/routes/stt.ts`**

```ts
// POST /api/stt: one utterance from a gadget in, its transcript out (spec
// §6.3). The companion's gadget hub is the caller (allowlisted in
// companion/src/routes.ts). On a Mac, Apple's on-device recognizer goes
// first through the Speech helper's file mode; ElevenLabs Scribe v2, with
// the person's own key, is the fallback and the only provider elsewhere.
//
// The body is raw audio/wav (mono PCM16, 16 kHz, at most 60 s), so it is
// read here with its own capped reader rather than readBody (JSON, 1 MB).
import { Buffer } from "node:buffer";
import { mkdtempSync, rmSync, writeFileSync } from "node:fs";
import type { IncomingMessage } from "node:http";
import { tmpdir } from "node:os";
import { join } from "node:path";

import type { ServiceCredential } from "../included-services.ts";
import { transcribeWithScribe } from "../stt/scribe.ts";
import { readWav } from "../tts/pcm.ts";
import { PASS, type RouteHandler } from "./table.ts";

/** 60 s × 16 kHz × 2 bytes = 1,920,000 bytes of PCM, plus headers. */
export const STT_MAX_BYTES = 2 * 1024 * 1024;
const MAX_PCM_BYTES = 60 * 16_000 * 2;
/** Scribe's floor is 100 ms; anything shorter is nothing said. */
const MIN_PCM_BYTES = (16_000 * 2) / 10;
/** Peak below about -44 dBFS: silence. Decided here, before any provider,
 * so a silent recording is "Didn't catch that" whichever provider is set up. */
const SILENCE_PEAK = 200;
const SCRIBE_TIMEOUT_MS = 30_000;

export type SttProvider = "apple" | "elevenlabs";
export type AppleSttResult =
  | { ok: true; text: string }                   // "" for no speech
  | { ok: false; error: "speech-not-authorized" | "recognizer-unavailable" | "dictation-disabled" | "recognition-error" | "timeout" | "file-unreadable" | "helper-failed" };

export interface SttRouteDeps {
  /** voiceCredential(cfg.tts?.key): the person's own ElevenLabs key (included only on a Cloud home). */
  elevenLabs(): ServiceCredential | null;
  /** macOS file-mode helper; undefined off darwin or when no helper bundle
   * resolves. Read once per request, so a getter can resolve it per request. */
  apple?: (wavPath: string) => Promise<AppleSttResult>;
  fetch?: typeof fetch;                         // test seam for Scribe
  tmpDir?: () => string;                        // where the WAV is written for the helper
}

/** Exact Latin-1 copy; the hub shows it as the turn's done reason (≤ 159 bytes). */
export const STT_UNAVAILABLE_MESSAGE =
  "Speech-to-text isn't set up. On a Mac, allow Speech Recognition for OpenMausBot (try dictation once), or add an ElevenLabs key in a bot's voice settings.";
export const STT_KEY_FORBIDDEN_MESSAGE =
  "Your ElevenLabs key can't use Speech to Text. Turn on its Speech to Text permission, or use a key without restrictions.";
const STT_FAILED_MESSAGE = "Speech-to-text failed. Try again.";

/** Apple errors that mean "not set up here", as opposed to "failed this time".
 * A missing helper bundle never gets here: `apple` is then undefined. A
 * helper that crashed (helper-failed) failed this time. */
const APPLE_NOT_SET_UP = new Set(["speech-not-authorized", "recognizer-unavailable", "dictation-disabled"]);

const AUDIO_WAV = new Set(["audio/wav", "audio/x-wav", "audio/wave", "audio/vnd.wave"]);

/** Parses a mono PCM16 16 kHz WAV (walking RIFF chunks) of at most 60 s;
 * null when it is anything else. */
export function pcmFromWav16k(wav: Uint8Array): Buffer | null {
  try {
    const info = readWav(wav);
    if (info.format !== 1 || info.channels !== 1 || info.bits !== 16 || info.rate !== 16_000) return null;
    const bytes = info.data.byteLength - (info.data.byteLength % 2);
    if (bytes > MAX_PCM_BYTES) return null;
    return Buffer.from(info.data.buffer, info.data.byteOffset, bytes);
  } catch {
    return null;
  }
}

function peak(pcm: Buffer): number {
  let max = 0;
  for (let i = 0; i + 1 < pcm.byteLength; i += 2) {
    const v = Math.abs(pcm.readInt16LE(i));
    if (v > max) max = v;
  }
  return max;
}

const header = (req: IncomingMessage, name: string): string | undefined => {
  const value = req.headers[name];
  return Array.isArray(value) ? value[0] : value;
};

type BodyRead = { ok: true; body: Buffer } | { ok: false; status: 400 | 413 };

/** content-length is checked before reading; a chunked body is cut off at
 * the cap. Either way the rest is drained, never buffered. */
function readCapped(req: IncomingMessage): Promise<BodyRead> {
  return new Promise((resolve) => {
    const chunks: Buffer[] = [];
    let received = 0;
    let settled = false;
    const settle = (result: BodyRead) => {
      if (settled) return;
      settled = true;
      if (!result.ok) req.resume();
      resolve(result);
    };
    req.on("data", (chunk: Buffer) => {
      if (settled) return;
      received += chunk.byteLength;
      if (received > STT_MAX_BYTES) return settle({ ok: false, status: 413 });
      chunks.push(chunk);
    });
    req.on("end", () => settle({ ok: true, body: Buffer.concat(chunks) }));
    req.on("error", () => settle({ ok: false, status: 400 }));
  });
}

export function createSttRoutes(deps: SttRouteDeps): RouteHandler {
  return async ({ req, res, path, method, json }) => {
    if (path !== "/api/stt" || method !== "POST") return PASS;
    const apple = deps.apple;               // once: server/index.ts resolves the helper in a getter

    const type = header(req, "content-type")?.split(";")[0]?.trim().toLowerCase();
    if (!type || !AUDIO_WAV.has(type)) {
      req.resume();
      return json(res, 400, { error: "bad_audio", message: "content-type must be audio/wav" });
    }
    const rawLength = header(req, "content-length");
    const declared = rawLength === undefined ? undefined : Number(rawLength);
    if (declared !== undefined && (!Number.isSafeInteger(declared) || declared < 0)) {
      req.resume();
      return json(res, 400, { error: "bad_audio", message: "content-length must be a non-negative integer" });
    }
    if (declared !== undefined && declared > STT_MAX_BYTES) {
      req.resume();
      return json(res, 413, { error: "too_large" });
    }
    const read = await readCapped(req);
    if (!read.ok) return read.status === 413 ? json(res, 413, { error: "too_large" }) : json(res, 400, { error: "bad_audio", message: "the upload failed" });
    const pcm = pcmFromWav16k(read.body);
    if (!pcm) return json(res, 400, { error: "bad_audio", message: "expected a mono 16-bit 16 kHz WAV of at most 60 s" });

    const cred = deps.elevenLabs();
    if (!apple && !cred) return json(res, 409, { error: "stt_unavailable", message: STT_UNAVAILABLE_MESSAGE });
    const firstProvider: SttProvider = apple ? "apple" : "elevenlabs";
    if (pcm.byteLength < MIN_PCM_BYTES || peak(pcm) < SILENCE_PEAK) {
      return json(res, 200, { text: "", provider: firstProvider });
    }

    let appleError: string | null = null;
    if (apple) {
      const dir = mkdtempSync(join(deps.tmpDir?.() ?? tmpdir(), "omb-stt-wav-"));
      const wavPath = join(dir, "utterance.wav");
      try {
        writeFileSync(wavPath, read.body, { mode: 0o600 });
        const heard = await apple(wavPath);
        if (heard.ok) return json(res, 200, { text: heard.text, provider: "apple" });
        appleError = heard.error;
      } finally {
        rmSync(dir, { recursive: true, force: true });
      }
    }

    if (cred) {
      const scribe = await transcribeWithScribe(pcm, cred, { fetch: deps.fetch, timeoutMs: SCRIBE_TIMEOUT_MS });
      if (scribe.ok) return json(res, 200, { text: scribe.text, provider: "elevenlabs" });
      if (scribe.forbidden) return json(res, 502, { error: "stt_key_forbidden", message: STT_KEY_FORBIDDEN_MESSAGE });
      // Cloud Pro's relay may not carry speech-to-text at all.
      if (cred.included && (scribe.status === 404 || scribe.status === 405)) {
        return json(res, 409, { error: "stt_unavailable", message: STT_UNAVAILABLE_MESSAGE });
      }
      return json(res, 502, { error: "stt_failed", message: STT_FAILED_MESSAGE });
    }

    if (appleError && APPLE_NOT_SET_UP.has(appleError)) return json(res, 409, { error: "stt_unavailable", message: STT_UNAVAILABLE_MESSAGE });
    return json(res, 502, { error: "stt_failed", message: STT_FAILED_MESSAGE });
  };
}
```

- [ ] **Step 4: Run the tests and the typecheck**

```bash
cd "$(git -C /Users/omkar/Desktop/openmaus/OpenGrokBot worktree list --porcelain | awk '/^worktree /{w=substr($0,10)} $0=="branch refs/heads/feat/gadget-voice"{print w}')" && test "$(git branch --show-current)" = feat/gadget-voice || { echo 'not in the feat/gadget-voice worktree'; exit 1; }
export PATH="$HOME/.nvm/versions/node/v24.14.1/bin:$PATH"
pnpm exec vitest run server/routes/stt.test.ts
pnpm exec tsc -p tsconfig.server.json
```

Expected: `Tests  14 passed (14)`, and `tsc` exits 0.

- [ ] **Step 5: Commit**

```bash
cd "$(git -C /Users/omkar/Desktop/openmaus/OpenGrokBot worktree list --porcelain | awk '/^worktree /{w=substr($0,10)} $0=="branch refs/heads/feat/gadget-voice"{print w}')" && test "$(git branch --show-current)" = feat/gadget-voice || { echo 'not in the feat/gadget-voice worktree'; exit 1; }
git add server/routes/stt.ts server/routes/stt.test.ts
git commit -m "feat(stt): POST /api/stt route module with Apple first and Scribe fallback" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 7: Launch the helper from the harness (`server/stt/apple.ts`)

**Files:**
- Create: `server/stt/apple.ts`
- Create: `server/stt/apple.test.ts`

**Interfaces:**
- Consumes: `type AppleSttResult` (Task 6) and the helper CLI (Task 4).
- Produces, used by Task 8:
  - `SPEECH_HELPER_BUNDLE = "OpenMausBot Speech.app"`
  - `resolveSpeechHelper(env = process.env, options?: { platform?; devBundle?; exists? }): string | null`. The order is `OMB_SPEECH_HELPER_PATH`, then `join(OMB_RESOURCES_PATH, "OpenMausBot Speech.app")`, then the dev bundle `electron/resources/OpenMausBot Speech.app`. The first whose `Contents/MacOS/speech-helper` exists wins. It is null off darwin.
  - `interface AppleOptions { timeoutMs?; graceMs?; exitGraceMs?; open? }`
  - `transcribeWithApple(bundle: string, wavPath: string, options?: AppleOptions): Promise<AppleSttResult>`. It runs `/usr/bin/open -n -g -W -o <out> --stderr <err> <bundle> --args --file <wav> --stop-file <stop> --timeout-ms <ms>` and waits for the helper's line. `no-speech` becomes `{ok: true, text: ""}`. After `timeoutMs + graceMs` it writes the stop file and answers `timeout`.

- [ ] **Step 1: Write the failing test**

Create `server/stt/apple.test.ts`:

```ts
// The Speech helper's file mode, seen from the harness (spec §6.3). A fake
// launcher stands in for /usr/bin/open: it records argv and plays the helper
// (one NDJSON line into the -o file), so this runs on any OS. The real
// helper's own checks are electron/speech-helper-file-mode.node-test.mjs.
import { existsSync, mkdtempSync, readFileSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { dirname, join, resolve } from "node:path";
import { afterAll, describe, expect, it } from "vitest";

import { resolveSpeechHelper, transcribeWithApple } from "./apple.ts";

const work = mkdtempSync(join(tmpdir(), "omb-apple-test-"));
afterAll(() => rmSync(work, { recursive: true, force: true }));

/** The "WAV" holds a script word for the fake helper instead of audio. */
const LAUNCHER = join(work, "fake-open.mjs");
writeFileSync(LAUNCHER, `#!${process.execPath}
import { appendFileSync, existsSync, readFileSync, writeFileSync } from "node:fs";
import { spawn } from "node:child_process";
const argv = process.argv.slice(2);
const out = argv[argv.indexOf("-o") + 1];
const rest = argv.slice(argv.indexOf("--args") + 1);
const arg = (name) => rest[rest.indexOf(name) + 1];
writeFileSync(${JSON.stringify(join(work, "argv.json"))}, JSON.stringify(argv));
const mode = readFileSync(arg("--file"), "utf8");
const line = (obj) => appendFileSync(out, JSON.stringify(obj) + "\\n");
if (mode.startsWith("text:")) line({ partial: false, text: mode.slice(5) });
else if (mode.startsWith("error:")) line({ error: mode.slice(6) });
else if (mode === "garbage") appendFileSync(out, "not json\\n");
else if (mode === "detached") {
  // open could not block on the helper: it returns at once, the helper answers later
  spawn(process.execPath, ["-e", "setTimeout(() => require('fs').appendFileSync(" + JSON.stringify(out) + ", JSON.stringify({ partial: false, text: 'late' }) + '\\\\n'), 200)"], { detached: true, stdio: "ignore" }).unref();
} else if (mode === "hang") {
  const stop = arg("--stop-file");
  const timer = setInterval(() => { if (existsSync(stop)) { clearInterval(timer); process.exit(0); } }, 20);
}
`, { mode: 0o755 });

let n = 0;
function wavWith(script: string): string {
  const path = join(work, `utterance-${n++}.wav`);
  writeFileSync(path, script);
  return path;
}

const BUNDLE = "/Applications/OpenMausBot.app/Contents/Resources/OpenMausBot Speech.app";
const run = (script: string, extra: Parameters<typeof transcribeWithApple>[2] = {}) =>
  transcribeWithApple(BUNDLE, wavWith(script), { open: LAUNCHER, exitGraceMs: 300, ...extra });

// The helper is macOS-only, and Windows cannot run a shebang launcher.
describe.skipIf(process.platform === "win32")("transcribeWithApple", () => {
  it("launches the bundle through open in file mode with a stop file and the timeout", async () => {
    const wav = wavWith("text:Hello there.");
    expect(await transcribeWithApple(BUNDLE, wav, { open: LAUNCHER, timeoutMs: 20_000 })).toEqual({ ok: true, text: "Hello there." });
    const argv = JSON.parse(readFileSync(join(work, "argv.json"), "utf8")) as string[];
    const out = argv[4]!;
    expect(argv).toEqual([
      "-n", "-g", "-W", "-o", out, "--stderr", join(dirname(out), "stderr.log"), BUNDLE,
      "--args", "--file", wav, "--stop-file", join(dirname(out), "stop"), "--timeout-ms", "20000",
    ]);
    expect(existsSync(dirname(out))).toBe(false);         // its temp folder is gone
  });

  it("maps the helper's no-speech error to an empty transcript", async () => {
    expect(await run("error:no-speech")).toEqual({ ok: true, text: "" });
  });

  it("passes the helper's known errors through and folds unknown ones", async () => {
    for (const error of ["speech-not-authorized", "dictation-disabled", "recognizer-unavailable", "timeout", "file-unreadable"]) {
      expect(await run(`error:${error}`)).toEqual({ ok: false, error });
    }
    expect(await run("error:mic-failed")).toEqual({ ok: false, error: "recognition-error" });
    expect(await run("garbage")).toEqual({ ok: false, error: "helper-failed" });
  });

  it("waits for the line even when open returns before the helper is done", async () => {
    expect(await run("detached", { exitGraceMs: 2_000 })).toEqual({ ok: true, text: "late" });
  });

  it("calls a helper that exits without a word a failure", async () => {
    expect(await run("silent-exit")).toEqual({ ok: false, error: "helper-failed" });
  });

  it("writes the stop file and reports a timeout past its own deadline", async () => {
    expect(await run("hang", { timeoutMs: 100, graceMs: 100 })).toEqual({ ok: false, error: "timeout" });
  });

  it("reports a launcher that cannot start as helper-failed", async () => {
    expect(await transcribeWithApple(BUNDLE, wavWith("text:x"), { open: join(work, "no-such-open") })).toEqual({ ok: false, error: "helper-failed" });
  });
});

describe("resolveSpeechHelper", () => {
  const present = (paths: string[]) => (p: string) => paths.includes(p);
  const binary = (bundle: string) => join(bundle, "Contents", "MacOS", "speech-helper");

  it("prefers OMB_SPEECH_HELPER_PATH, then the packaged Resources, then the dev bundle", () => {
    const all = present([binary("/override/Speech.app"), binary("/res/OpenMausBot Speech.app"), binary("/repo/dev.app")]);
    const options = { platform: "darwin" as const, devBundle: "/repo/dev.app", exists: all };
    // resolve() and join(): the same expectations hold on the Windows CI shard
    expect(resolveSpeechHelper({ OMB_SPEECH_HELPER_PATH: "/override/Speech.app", OMB_RESOURCES_PATH: "/res" }, options)).toBe(resolve("/override/Speech.app"));
    expect(resolveSpeechHelper({ OMB_RESOURCES_PATH: "/res" }, options)).toBe(resolve(join("/res", "OpenMausBot Speech.app")));
    expect(resolveSpeechHelper({}, options)).toBe(resolve("/repo/dev.app"));
  });

  it("skips a bundle without its binary, and is null off macOS", () => {
    const options = { platform: "darwin" as const, devBundle: "/repo/dev.app", exists: present([binary("/repo/dev.app")]) };
    expect(resolveSpeechHelper({ OMB_SPEECH_HELPER_PATH: "/empty.app" }, options)).toBe(resolve("/repo/dev.app"));
    expect(resolveSpeechHelper({}, { ...options, exists: present([]) })).toBeNull();
    expect(resolveSpeechHelper({ OMB_SPEECH_HELPER_PATH: "/override/Speech.app" }, { platform: "win32", exists: () => true })).toBeNull();
  });
});
```

- [ ] **Step 2: Run it to see it fail**

```bash
cd "$(git -C /Users/omkar/Desktop/openmaus/OpenGrokBot worktree list --porcelain | awk '/^worktree /{w=substr($0,10)} $0=="branch refs/heads/feat/gadget-voice"{print w}')" && test "$(git branch --show-current)" = feat/gadget-voice || { echo 'not in the feat/gadget-voice worktree'; exit 1; }
export PATH="$HOME/.nvm/versions/node/v24.14.1/bin:$PATH"
pnpm exec vitest run server/stt/apple.test.ts
```

Expected: FAIL with `Error: Cannot find module './apple.ts' imported from …/server/stt/apple.test.ts`.

- [ ] **Step 3: Implement `server/stt/apple.ts`**

```ts
// Apple's on-device speech recognizer for gadget turns (spec §6.3), through
// the Speech helper's file mode (electron/resources/speech-helper.swift).
//
// The helper must be launched through LaunchServices (`open -n -g -W`), the
// way electron/speech.mjs does: a direct spawn loses the app-bundle identity
// and TCC kills it. That also means `open` can return before the helper is
// done ("Unable to block on application" when the helper exits first), so
// the answer is the helper's one NDJSON line in the -o file, never `open`'s
// exit code. File mode never prompts for permission; without a grant it
// answers speech-not-authorized and /api/stt falls back to Scribe.
import { spawn } from "node:child_process";
import { existsSync, mkdtempSync, readFileSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join, resolve } from "node:path";
import { fileURLToPath } from "node:url";

import type { AppleSttResult } from "../routes/stt.ts";

export const SPEECH_HELPER_BUNDLE = "OpenMausBot Speech.app";
/** `pnpm dev:server` runs from the repo, where `pnpm build:speech` (or the
 * first dictation in a dev app) builds the bundle. */
const DEV_BUNDLE = fileURLToPath(new URL("../../electron/resources/OpenMausBot Speech.app", import.meta.url));

/** OMB_SPEECH_HELPER_PATH → join(OMB_RESOURCES_PATH, "OpenMausBot Speech.app")
 * (packaged builds; Electron already sets it for the harness) → the dev
 * bundle; the first whose Contents/MacOS/speech-helper exists. null off macOS. */
export function resolveSpeechHelper(
  env: NodeJS.ProcessEnv = process.env,
  options: { platform?: NodeJS.Platform; devBundle?: string; exists?: (path: string) => boolean } = {},
): string | null {
  if ((options.platform ?? process.platform) !== "darwin") return null;
  const exists = options.exists ?? existsSync;
  const candidates = [
    env.OMB_SPEECH_HELPER_PATH,
    env.OMB_RESOURCES_PATH ? join(env.OMB_RESOURCES_PATH, SPEECH_HELPER_BUNDLE) : undefined,
    options.devBundle ?? DEV_BUNDLE,
  ];
  for (const candidate of candidates) {
    if (candidate && exists(join(candidate, "Contents", "MacOS", "speech-helper"))) return resolve(candidate);
  }
  return null;
}

const HELPER_ERRORS = new Set([
  "speech-not-authorized", "recognizer-unavailable", "dictation-disabled", "recognition-error", "timeout", "file-unreadable",
]);

/** The helper's last line → a result. `no-speech` (a silent file) is an
 * empty transcript, not a failure. */
function readResult(text: string): AppleSttResult | null {
  const lines = text.split("\n");
  if (lines.length < 2) return null;          // no complete line yet
  const last = lines.slice(0, -1).reverse().find((line) => line.trim());
  if (!last) return null;
  try {
    const parsed = JSON.parse(last) as { partial?: unknown; text?: unknown; error?: unknown };
    if (typeof parsed.text === "string" && parsed.partial === false) return { ok: true, text: parsed.text.trim() };
    if (parsed.error === "no-speech") return { ok: true, text: "" };
    if (typeof parsed.error === "string") {
      return { ok: false, error: HELPER_ERRORS.has(parsed.error) ? (parsed.error as Extract<AppleSttResult, { ok: false }>["error"]) : "recognition-error" };
    }
  } catch {
    return { ok: false, error: "helper-failed" };
  }
  return null;
}

export interface AppleOptions {
  /** Passed to the helper as --timeout-ms (it clamps to 1000..120000). Default 20000. */
  timeoutMs?: number;
  /** The harness gives up this long after timeoutMs and writes the stop file. Default 5000. */
  graceMs?: number;
  /** How long to keep reading after `open` exits without a line. Default 1000. */
  exitGraceMs?: number;
  /** Test seam: the launcher. Default /usr/bin/open. */
  open?: string;
}

export function transcribeWithApple(bundle: string, wavPath: string, options: AppleOptions = {}): Promise<AppleSttResult> {
  const timeoutMs = options.timeoutMs ?? 20_000;
  const dir = mkdtempSync(join(tmpdir(), "omb-stt-"));
  const out = join(dir, "stdout.ndjson");
  const err = join(dir, "stderr.log");
  const stop = join(dir, "stop");
  writeFileSync(out, "");
  writeFileSync(err, "");
  return new Promise<AppleSttResult>((done) => {
    let settled = false;
    let exitedAt: number | null = null;
    const startedAt = Date.now();
    const deadline = startedAt + timeoutMs + (options.graceMs ?? 5_000);
    const exitGrace = options.exitGraceMs ?? 1_000;
    const child = spawn(
      options.open ?? "/usr/bin/open",
      ["-n", "-g", "-W", "-o", out, "--stderr", err, bundle, "--args", "--file", wavPath, "--stop-file", stop, "--timeout-ms", String(timeoutMs)],
      { stdio: "ignore" },
    );
    const finish = (result: AppleSttResult, keepDirMs = 0) => {
      if (settled) return;
      settled = true;
      clearInterval(poll);
      if (child.exitCode === null && child.signalCode === null) child.kill();
      // After a timeout the helper may still be polling for the stop file.
      if (keepDirMs) setTimeout(() => rmSync(dir, { recursive: true, force: true }), keepDirMs).unref();
      else rmSync(dir, { recursive: true, force: true });
      done(result);
    };
    child.on("error", () => finish({ ok: false, error: "helper-failed" }));
    child.on("exit", () => {
      exitedAt = Date.now();
    });
    const poll = setInterval(() => {
      let text = "";
      try {
        text = readFileSync(out, "utf8");
      } catch {
        return finish({ ok: false, error: "helper-failed" });
      }
      const result = readResult(text);
      if (result) return finish(result);
      const now = Date.now();
      if (exitedAt !== null && now - exitedAt >= exitGrace) return finish({ ok: false, error: "helper-failed" });
      if (now >= deadline) {
        try {
          writeFileSync(stop, "");
        } catch {
          // the directory is ours; nothing useful to do if this fails
        }
        finish({ ok: false, error: "timeout" }, 1_000);
      }
    }, 50);
  });
}
```

- [ ] **Step 4: Run the tests and the typecheck**

```bash
cd "$(git -C /Users/omkar/Desktop/openmaus/OpenGrokBot worktree list --porcelain | awk '/^worktree /{w=substr($0,10)} $0=="branch refs/heads/feat/gadget-voice"{print w}')" && test "$(git branch --show-current)" = feat/gadget-voice || { echo 'not in the feat/gadget-voice worktree'; exit 1; }
export PATH="$HOME/.nvm/versions/node/v24.14.1/bin:$PATH"
pnpm exec vitest run server/stt
pnpm exec tsc -p tsconfig.server.json
```

Expected: vitest prints `Test Files  2 passed (2)` and `Tests  16 passed (16)`: `apple.test.ts` (9, in about 2 s of real launcher processes) and `scribe.test.ts` (7). `tsc` exits 0.

- [ ] **Step 5: Commit**

```bash
cd "$(git -C /Users/omkar/Desktop/openmaus/OpenGrokBot worktree list --porcelain | awk '/^worktree /{w=substr($0,10)} $0=="branch refs/heads/feat/gadget-voice"{print w}')" && test "$(git branch --show-current)" = feat/gadget-voice || { echo 'not in the feat/gadget-voice worktree'; exit 1; }
git add server/stt/apple.ts server/stt/apple.test.ts
git commit -m "feat(stt): launch the Speech helper's file mode from the harness" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 8: Register `/api/stt` and allowlist it

**Files:**
- Modify `server/index.ts` (origin/main line numbers):
  - `:316`: the `included-services.ts` import;
  - `:620`: the route module imports;
  - after `:15477-15480`: `ROUTES.push` right after the Live routes.
- Modify: `server/request-auth.ts:359` (the voice group of `CLIENT_ALLOW`)
- Modify: `server/request-auth.test.ts:106`
- Modify: `companion/src/routes.ts:234` (the voice group of `ALLOWED`)
- Modify: `companion/test/routes.test.ts:129-132`
- Modify: `server/routes/stt.test.ts` (append the registration check)

**Interfaces:**
- Consumes: `createSttRoutes` (Task 6), `resolveSpeechHelper` and `transcribeWithApple` (Task 7), and `voiceCredential(own)` (`server/included-services.ts:84-86`).
- Produces: the live `POST /api/stt` in the harness. Its scope is `client`, and it is allowed for the companion hub through `denyReason` (contract §3.7). Task 10 calls it.
- `apple` is registered as a getter, in the pinned `{elevenLabs, apple}` shape. The route reads it once per request, so a dev bundle built after the harness started is still found. It is `undefined` off macOS or when no helper bundle resolves (contract §3.14), so a Mac with no helper and no key gets the set-up copy, never "Didn't catch that".

- [ ] **Step 1: Write the failing checks**

Append to `server/routes/stt.test.ts`:

```ts
describe("registration (server/index.ts)", () => {
  it("goes through the route table with Apple first and the person's own ElevenLabs key", () => {
    const index = readFileSync(new URL("../index.ts", import.meta.url), "utf8");
    expect(index).toContain("ROUTES.push(createSttRoutes({");
    expect(index).toContain("elevenLabs: () => voiceCredential(cfg.tts?.key),");
    expect(index).toContain("get apple() {");                // resolved per request; undefined with no helper bundle
    expect(index).toContain("transcribeWithApple(bundle, wavPath, { timeoutMs: 20_000 })");
  });
});
```

In `server/request-auth.test.ts`, replace (`:106`):

```ts
      ["GET", "/api/config"], ["GET", "/api/webhooks"], ["POST", "/api/tts/speak"],
```

with:

```ts
      ["GET", "/api/config"], ["GET", "/api/webhooks"], ["POST", "/api/tts/speak"], ["POST", "/api/stt"],
```

In `companion/test/routes.test.ts`, replace (`:129-132`):

```ts
  for (const [method, path] of calls) {
    it(`allows ${method} ${path}`, () => expect(ask(method, path)).toBeNull());
  }
});
```

with:

```ts
  for (const [method, path] of calls) {
    it(`allows ${method} ${path}`, () => expect(ask(method, path)).toBeNull());
  }

  // Not a phone call: the gadget hub's speech-to-text (companion/src/gadget/
  // stt-client.ts). The hub checks this same list before every harness call.
  it("allows POST /api/stt for gadget speech, and nothing else on that path", () => {
    expect(ask("POST", "/api/stt")).toBeNull();
    expect(ask("GET", "/api/stt")?.status).toBe(404);
    expect(ask("POST", "/api/stt/extra")?.status).toBe(404);
  });
});
```

- [ ] **Step 2: Run them to see them fail**

```bash
cd "$(git -C /Users/omkar/Desktop/openmaus/OpenGrokBot worktree list --porcelain | awk '/^worktree /{w=substr($0,10)} $0=="branch refs/heads/feat/gadget-voice"{print w}')" && test "$(git branch --show-current)" = feat/gadget-voice || { echo 'not in the feat/gadget-voice worktree'; exit 1; }
export PATH="$HOME/.nvm/versions/node/v24.14.1/bin:$PATH"
pnpm exec vitest run server/routes/stt.test.ts server/request-auth.test.ts companion/test/routes.test.ts
```

Expected: 3 failures, one in each file:
- the registration check: `… to contain 'ROUTES.push(createSttRoutes({'`;
- the client-scope row: `POST /api/stt: expected 'admin' to be 'client'`;
- the companion case: `expected { status: 404, …(1) } to be null`.

- [ ] **Step 3: Wire the route in `server/index.ts`**

Replace (`:316`):

```ts
import { holdIncludedServices } from "./included-services.ts";
```

with:

```ts
import { holdIncludedServices, voiceCredential } from "./included-services.ts";
```

Replace (`:620`, inside the "keep these last" route import block):

```ts
import { createLiveRoutes } from "./routes/live.ts";
```

with:

```ts
import { createLiveRoutes } from "./routes/live.ts";
import { createSttRoutes } from "./routes/stt.ts";
import { resolveSpeechHelper, transcribeWithApple } from "./stt/apple.ts";
```

Then replace the end of the Live routes registration (`:15477-15480`):

```ts
    broadcast({ kind: "config", ...configStatus() });
    return liveSettingsFor(cfg);
  },
}));
```

with:

```ts
    broadcast({ kind: "config", ...configStatus() });
    return liveSettingsFor(cfg);
  },
}));
// Gadget speech-to-text (spec §6.3): Apple's recognizer through the Speech
// helper's file mode on a Mac, then Scribe v2 with the person's own
// ElevenLabs key. `apple` is a getter that the route reads once per request:
// a dev bundle built after the harness started is still found, and it is
// undefined off macOS or with no helper bundle (contract §3.14). Budget:
// 20 s helper + 5 s grace, then 30 s Scribe, inside the hub's 60 s.
ROUTES.push(createSttRoutes({
  elevenLabs: () => voiceCredential(cfg.tts?.key),
  get apple() {
    const bundle = resolveSpeechHelper();
    return bundle ? (wavPath: string) => transcribeWithApple(bundle, wavPath, { timeoutMs: 20_000 }) : undefined;
  },
}));
```

- [ ] **Step 4: Allowlist the route on both sides**

In `server/request-auth.ts`, replace (`:359`):

```ts
  { methods: ["POST"], path: /^\/api\/tts\/speak$/ },
```

with:

```ts
  { methods: ["POST"], path: /^\/api\/tts\/speak$/ },
  { methods: ["POST"], path: /^\/api\/stt$/ }, // a gadget's utterance in, its text out (the companion hub)
```

In `companion/src/routes.ts`, replace (`:234`):

```ts
  { method: "POST", path: /^\/api\/tts\/speak$/ },
```

with:

```ts
  { method: "POST", path: /^\/api\/tts\/speak$/ },
  // A gadget's utterance (companion/src/gadget/stt-client.ts): WAV in, its
  // text out. The hub runs in this process and checks this list before
  // every harness call, so its routes are listed like a device's.
  { method: "POST", path: /^\/api\/stt$/ },
```

`resolveSpeechHelper()` already answers null off macOS, so the getter needs no platform check. The harness also checks companion-token requests against this list (`companionDenial`, `server/request-auth.ts`), so this one entry lets the hub's calls through on both sides.

- [ ] **Step 5: Run the checks, the ratchet and the typecheck**

```bash
cd "$(git -C /Users/omkar/Desktop/openmaus/OpenGrokBot worktree list --porcelain | awk '/^worktree /{w=substr($0,10)} $0=="branch refs/heads/feat/gadget-voice"{print w}')" && test "$(git branch --show-current)" = feat/gadget-voice || { echo 'not in the feat/gadget-voice worktree'; exit 1; }
export PATH="$HOME/.nvm/versions/node/v24.14.1/bin:$PATH"
pnpm exec vitest run server/routes/stt.test.ts
pnpm exec vitest run server/routes/stt.test.ts server/request-auth.test.ts companion/test/routes.test.ts scripts/testing/index-route-ratchet.test.ts
pnpm exec tsc -p tsconfig.server.json
```

Expected:
- The first run prints `Tests  15 passed (15)`: Task 6's 14 and the registration check.
- The second prints `Test Files  4 passed (4)` with 0 failed. That covers `request-auth.test.ts`, `routes.test.ts`, and the ratchet with its counts 164, 88, 11, 18, 1 and 3.
- `tsc` exits 0.

- [ ] **Step 6: Commit**

```bash
cd "$(git -C /Users/omkar/Desktop/openmaus/OpenGrokBot worktree list --porcelain | awk '/^worktree /{w=substr($0,10)} $0=="branch refs/heads/feat/gadget-voice"{print w}')" && test "$(git branch --show-current)" = feat/gadget-voice || { echo 'not in the feat/gadget-voice worktree'; exit 1; }
git add server/index.ts server/request-auth.ts server/request-auth.test.ts companion/src/routes.ts companion/test/routes.test.ts server/routes/stt.test.ts
git commit -m "feat(stt): register /api/stt and allow it for the gadget hub" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 9: Companion audio: WAV packing and the speech pacer (`companion/src/gadget/audio.ts`)

**Files:**
- Create: `companion/src/gadget/audio.ts`
- Create: `companion/test/gadget/audio.test.ts`

**Interfaces:**
- Consumes (P3a, `companion/src/gadget/protocol.ts`, contract §3.2): `SPEAK_FRAME_MS = 40` and `SPEAK_AHEAD_MS = 500`.
- Produces (contract §3.14), used by Tasks 10 and 11:
  - `wavFromPcm16(pcm: Uint8Array, rate: number): Buffer`
  - `interface SpeechPacer { push(pcm); end(); drained(): Promise<void>; stop() }`
  - `createSpeechPacer({ rate: 16000 | 24000, send: (frame) => Promise<boolean>, now?: () => number, onFlushed?: () => void }): SpeechPacer`

  `onFlushed` is a P3b-only optional field: it fires once, after `end()`, when the last frame has been handed to `send`.
- Frame sizes (contract §2.12, "Speaker frames carry 40 ms"): every frame is exactly 1280 bytes at 16 kHz or 1920 bytes at 24 kHz. After `end()`, a short last frame is padded with silence to that size, so a reply ends with up to 40 ms of silence. A gadget core written to the contract never sees an odd-sized speaker frame.

- [ ] **Step 1: Make sure the branch sits on P3a**

From here on the code imports P3a's modules.

```bash
cd "$(git -C /Users/omkar/Desktop/openmaus/OpenGrokBot worktree list --porcelain | awk '/^worktree /{w=substr($0,10)} $0=="branch refs/heads/feat/gadget-voice"{print w}')" && test "$(git branch --show-current)" = feat/gadget-voice || { echo 'not in the feat/gadget-voice worktree'; exit 1; }
export PATH="$HOME/.nvm/versions/node/v24.14.1/bin:$PATH"
git merge-base --is-ancestor feat/gadget-hub HEAD && echo "on feat/gadget-hub" || echo "NOT on feat/gadget-hub"
```

- **If it prints `NOT on feat/gadget-hub`** (Task 1 started from `origin/main`, contract deviation D-P3b-2) and `git rev-parse --verify --quiet refs/heads/feat/gadget-hub` succeeds, move the branch. This rebase is what brings P3b back to contract §1.3's base. Do it once P3a is finished (the dispatcher confirms); a `feat/gadget-hub` that is still growing fails the seam check below, and then this task is blocked:

  ```bash
  cd "$(git -C /Users/omkar/Desktop/openmaus/OpenGrokBot worktree list --porcelain | awk '/^worktree /{w=substr($0,10)} $0=="branch refs/heads/feat/gadget-voice"{print w}')" && test "$(git branch --show-current)" = feat/gadget-voice || { echo 'not in the feat/gadget-voice worktree'; exit 1; }
  export PATH="$HOME/.nvm/versions/node/v24.14.1/bin:$PATH"
  git rebase feat/gadget-hub
  pnpm install --frozen-lockfile
  pnpm exec vitest run server/tts server/stt server/routes/stt.test.ts server/request-auth.test.ts companion/test/routes.test.ts scripts/testing/index-route-ratchet.test.ts
  ```

  Expected: the rebase finishes with no conflicts; P3a does not touch these files, and its `server/index.ts` lines are elsewhere. If a conflict does come up, keep both sides' lines. All listed tests then pass.
- **If `feat/gadget-hub` does not exist,** stop here and report "P3b Tasks 9–12 are blocked on P3a (feat/gadget-hub)". Tasks 1–8 are complete and committed.

Then confirm the pinned seams exist:

```bash
cd "$(git -C /Users/omkar/Desktop/openmaus/OpenGrokBot worktree list --porcelain | awk '/^worktree /{w=substr($0,10)} $0=="branch refs/heads/feat/gadget-voice"{print w}')" && test "$(git branch --show-current)" = feat/gadget-voice || { echo 'not in the feat/gadget-voice worktree'; exit 1; }
grep -n "export const SPEAK_AHEAD_MS\|export const SPEAK_FRAME_MS\|export const BinaryKind" companion/src/gadget/protocol.ts
grep -n "export type SttFn\|export type SpeechOutFactory\|export type VoiceProvider\|export const textOnlyVoice" companion/src/gadget/session.ts
grep -n "export function foldLatin1\|export interface HarnessClient\|export interface HarnessRaw" companion/src/gadget/shape.ts companion/src/gadget/harness-client.ts
ls companion/test/gadget/helpers/fake-harness.ts companion/test/gadget/helpers/gadget-client.ts
grep -n "voice: undefined" companion/src/index.ts
```

Expected: every grep prints at least one line, and `ls` lists both helpers. A missing seam is a contract deviation in P3a: report it and stop.

- [ ] **Step 2: Write the failing test**

Create `companion/test/gadget/audio.test.ts`:

```ts
// WAV packing for /api/stt and the speaker pacer (spec §4.4: 40 ms frames,
// at most 0.5 s ahead of real time). The pacer runs on vitest's fake clock,
// so "real time" here is exact and the tests never sleep.
import { Buffer } from "node:buffer";
import { afterEach, beforeEach, describe, expect, it, vi } from "vitest";

import { createSpeechPacer, wavFromPcm16 } from "../../src/gadget/audio.ts";

describe("wavFromPcm16", () => {
  it("writes a 44-byte PCM16 mono header around the samples", () => {
    const pcm = Buffer.from([1, 0, 0xff, 0x7f, 0x00, 0x80]);
    const wav = wavFromPcm16(pcm, 16000);
    expect(wav.byteLength).toBe(50);
    expect(wav.toString("ascii", 0, 4)).toBe("RIFF");
    expect(wav.readUInt32LE(4)).toBe(42);
    expect(wav.toString("ascii", 8, 16)).toBe("WAVEfmt ");
    expect([wav.readUInt32LE(16), wav.readUInt16LE(20), wav.readUInt16LE(22)]).toEqual([16, 1, 1]);
    expect([wav.readUInt32LE(24), wav.readUInt32LE(28), wav.readUInt16LE(32), wav.readUInt16LE(34)]).toEqual([16000, 32000, 2, 16]);
    expect(wav.toString("ascii", 36, 40)).toBe("data");
    expect(wav.readUInt32LE(40)).toBe(6);
    expect(wav.subarray(44)).toEqual(pcm);
  });

  it("drops half a sample rather than writing an odd data chunk", () => {
    const wav = wavFromPcm16(Buffer.from([1, 2, 3]), 16000);
    expect(wav.readUInt32LE(40)).toBe(2);
    expect(wav.byteLength).toBe(46);
  });
});

describe("createSpeechPacer", () => {
  beforeEach(() => {
    vi.useFakeTimers();
    vi.setSystemTime(1_000_000);
  });
  afterEach(() => vi.useRealTimers());

  /** Records every frame with the fake time it was sent at. */
  function harness(rate: 16000 | 24000) {
    const frames: Array<{ at: number; bytes: number }> = [];
    let flushedAt: number | null = null;
    const start = Date.now();
    const pacer = createSpeechPacer({
      rate,
      send: async (frame) => {
        frames.push({ at: Date.now() - start, bytes: frame.byteLength });
        return true;
      },
      now: () => Date.now(),
      onFlushed: () => { flushedAt = Date.now() - start; },
    });
    const sentMs = () => frames.reduce((ms, f) => ms + (f.bytes / (rate * 2)) * 1000, 0);
    return { pacer, frames, sentMs, flushedAt: () => flushedAt };
  }

  const audio = (rate: number, ms: number) => Buffer.alloc((rate * 2 * ms) / 1000, 1);

  it("sends 40 ms frames, 0.5 s ahead at most, then keeps real time", async () => {
    const { pacer, frames, sentMs } = harness(16000);
    pacer.push(audio(16000, 1000));
    await vi.advanceTimersByTimeAsync(0);
    expect(frames).toHaveLength(12);                       // 480 ms: a 13th would be 520 ms ahead
    expect(frames.every((f) => f.bytes === 1280)).toBe(true);
    for (let t = 20; t <= 500; t += 20) {
      await vi.advanceTimersByTimeAsync(20);
      expect(sentMs() - t).toBeLessThanOrEqual(500);       // never more than 0.5 s ahead
    }
    expect(frames).toHaveLength(25);
  });

  it("uses 1920-byte frames at 24 kHz", async () => {
    const { pacer, frames } = harness(24000);
    pacer.push(audio(24000, 200));
    pacer.end();
    await vi.advanceTimersByTimeAsync(0);
    expect(frames.map((f) => f.bytes)).toEqual([1920, 1920, 1920, 1920, 1920]);
  });

  it("queues several utterances on one stream and pads the short tail to a whole frame after end()", async () => {
    const { pacer, frames, flushedAt } = harness(16000);
    pacer.push(Buffer.alloc(1000, 1));
    pacer.push(Buffer.alloc(1001, 2));                     // 2001 bytes: one frame, 720 + a stray byte
    await vi.advanceTimersByTimeAsync(0);
    expect(frames.map((f) => f.bytes)).toEqual([1280]);
    expect(flushedAt()).toBeNull();
    pacer.end();
    await vi.advanceTimersByTimeAsync(0);
    expect(frames.map((f) => f.bytes)).toEqual([1280, 1280]);   // 720 bytes of speech + 560 of silence
    expect(flushedAt()).toBe(0);
  });

  it("calls onFlushed when the last frame is sent and drains once it has played", async () => {
    const { pacer, flushedAt } = harness(16000);
    let drained = false;
    pacer.push(audio(16000, 1000));
    pacer.end();
    void pacer.drained().then(() => { drained = true; });
    await vi.advanceTimersByTimeAsync(499);
    expect(flushedAt()).toBeNull();
    await vi.advanceTimersByTimeAsync(1);
    expect(flushedAt()).toBe(500);                         // all sent 0.5 s before playout ends
    await vi.advanceTimersByTimeAsync(499);
    expect(drained).toBe(false);
    await vi.advanceTimersByTimeAsync(1);
    expect(drained).toBe(true);                            // playout of 1 s ended at t = 1000
  });

  it("restarts the clock after an underrun instead of bursting", async () => {
    const { pacer, frames } = harness(16000);
    pacer.push(audio(16000, 200));
    await vi.advanceTimersByTimeAsync(2000);               // played out long ago; the next utterance is late
    pacer.push(audio(16000, 1000));
    await vi.advanceTimersByTimeAsync(0);
    expect(frames).toHaveLength(5 + 12);                   // again 480 ms ahead, not 1 s
  });

  it("stops at once: nothing more is sent and drained() resolves", async () => {
    const { pacer, frames, flushedAt } = harness(16000);
    pacer.push(audio(16000, 2000));
    pacer.end();
    await vi.advanceTimersByTimeAsync(0);
    pacer.stop();
    const before = frames.length;
    await vi.advanceTimersByTimeAsync(3000);
    expect(frames).toHaveLength(before);
    expect(flushedAt()).toBeNull();
    await expect(pacer.drained()).resolves.toBeUndefined();
    pacer.push(audio(16000, 100));
    await vi.advanceTimersByTimeAsync(100);
    expect(frames).toHaveLength(before);
  });

  it("stops when the connection is gone", async () => {
    let sends = 0;
    const pacer = createSpeechPacer({ rate: 16000, send: async () => (++sends < 3), now: () => Date.now() });
    pacer.push(audio(16000, 1000));
    await vi.advanceTimersByTimeAsync(1000);
    expect(sends).toBe(3);
    await expect(pacer.drained()).resolves.toBeUndefined();
  });
});
```

- [ ] **Step 3: Run it to see it fail**

```bash
cd "$(git -C /Users/omkar/Desktop/openmaus/OpenGrokBot worktree list --porcelain | awk '/^worktree /{w=substr($0,10)} $0=="branch refs/heads/feat/gadget-voice"{print w}')" && test "$(git branch --show-current)" = feat/gadget-voice || { echo 'not in the feat/gadget-voice worktree'; exit 1; }
export PATH="$HOME/.nvm/versions/node/v24.14.1/bin:$PATH"
pnpm exec vitest run companion/test/gadget/audio.test.ts
```

Expected: FAIL with `Error: Cannot find module '../../src/gadget/audio.ts' imported from …/companion/test/gadget/audio.test.ts`.

- [ ] **Step 4: Implement `companion/src/gadget/audio.ts`**

```ts
// Gadget audio on the host side (spec §6.1 audio.ts): WAV packing for
// speech-to-text, and real-time pacing of speaker PCM (spec §4.4: speech
// frames are paced to real time, at most 0.5 s ahead, so a 1 s jitter
// buffer on the gadget is enough). Node built-ins only.
import { Buffer } from "node:buffer";

import { SPEAK_AHEAD_MS, SPEAK_FRAME_MS } from "./protocol.ts";

/** RIFF/WAVE, fmt PCM (1), mono, 16-bit, `rate`, data = pcm (little-endian).
 * A stray odd byte (half a sample) is dropped. */
export function wavFromPcm16(pcm: Uint8Array, rate: number): Buffer {
  const dataBytes = pcm.byteLength - (pcm.byteLength % 2);
  const out = Buffer.alloc(44 + dataBytes);
  out.write("RIFF", 0, "ascii");
  out.writeUInt32LE(36 + dataBytes, 4);
  out.write("WAVE", 8, "ascii");
  out.write("fmt ", 12, "ascii");
  out.writeUInt32LE(16, 16);
  out.writeUInt16LE(1, 20);                 // PCM
  out.writeUInt16LE(1, 22);                 // mono
  out.writeUInt32LE(rate, 24);
  out.writeUInt32LE(rate * 2, 28);          // byte rate
  out.writeUInt16LE(2, 32);                 // block align
  out.writeUInt16LE(16, 34);                // bits per sample
  out.write("data", 36, "ascii");
  out.writeUInt32LE(dataBytes, 40);
  out.set(pcm.subarray(0, dataBytes), 44);
  return out;
}

/** Real-time pacer for speaker PCM: frames of SPEAK_FRAME_MS (40 ms), never
 *  more than SPEAK_AHEAD_MS (500 ms) ahead of the playout clock; several
 *  utterances queue on one stream. Every frame is exactly 40 ms (contract
 *  §2.12): after end(), a short last frame is padded with silence. */
export interface SpeechPacer {
  push(pcm: Uint8Array): void;          // append one utterance's PCM16LE
  end(): void;                          // no more audio for this stream
  /** Resolves when everything pushed has been handed to `send` and its playout time has passed. */
  drained(): Promise<void>;
  stop(): void;                         // drop everything now
}

export function createSpeechPacer(options: {
  rate: 16000 | 24000;
  send: (frame: Uint8Array) => Promise<boolean>;   // session.sendBinary(BinaryKind.speaker, stream, frame)
  now?: () => number;
  /** P3b addition: called once, after end(), when the last frame has been
   *  handed to `send` (speech.ts sends speak.end then, before playout ends). */
  onFlushed?: () => void;
}): SpeechPacer {
  const now = options.now ?? Date.now;
  const frameBytes = (options.rate * SPEAK_FRAME_MS * 2) / 1000;   // 1280 at 16 kHz, 1920 at 24 kHz
  let pending: Buffer[] = [];
  let pendingBytes = 0;
  let ended = false;
  let stopped = false;
  let flushed = false;
  let sending = false;
  /** When everything sent so far has played out (ms on the `now` clock). */
  let playEnd = 0;
  let wake: ReturnType<typeof setTimeout> | null = null;
  let drainTimer: ReturnType<typeof setTimeout> | null = null;
  let isDrained = false;
  const drainWaiters: Array<() => void> = [];

  const settleDrained = () => {
    if (isDrained) return;
    isDrained = true;
    for (const resolve of drainWaiters.splice(0)) resolve();
  };

  /** The next `size` bytes, copying only when a frame spans two utterances. */
  const take = (size: number): Buffer => {
    const head = pending[0]!;
    let frame: Buffer;
    if (head.byteLength >= size) {
      frame = head.subarray(0, size);
      if (head.byteLength === size) pending.shift();
      else pending[0] = head.subarray(size);
    } else {
      frame = Buffer.alloc(size);
      let filled = 0;
      while (filled < size) {
        const part = pending[0]!;
        const n = Math.min(size - filled, part.byteLength);
        part.copy(frame, filled, 0, n);
        filled += n;
        if (n === part.byteLength) pending.shift();
        else pending[0] = part.subarray(n);
      }
    }
    pendingBytes -= size;
    return frame;
  };

  const schedule = (delayMs: number) => {
    if (wake) return;
    wake = setTimeout(() => {
      wake = null;
      void pump();
    }, Math.max(1, Math.ceil(delayMs)));
  };

  const afterFlush = () => {
    flushed = true;
    options.onFlushed?.();
    drainTimer = setTimeout(settleDrained, Math.max(0, playEnd - now()));
  };

  async function pump(): Promise<void> {
    if (sending || stopped) return;
    sending = true;
    try {
      while (!stopped) {
        // a whole frame, or after end() the tail (whole samples only)
        const size = pendingBytes >= frameBytes ? frameBytes : ended ? pendingBytes - (pendingBytes % 2) : 0;
        if (size === 0) break;
        const t = now();
        const ahead = Math.max(0, playEnd - t);
        const duration = SPEAK_FRAME_MS;     // every frame on the wire is frameBytes long
        if (ahead + duration > SPEAK_AHEAD_MS) {
          schedule(ahead + duration - SPEAK_AHEAD_MS);
          return;
        }
        let frame = take(size);
        if (size < frameBytes) {
          // the reply's last frame: pad with up to 40 ms of silence
          const padded = Buffer.alloc(frameBytes);
          frame.copy(padded);
          frame = padded;
        }
        playEnd = Math.max(playEnd, t) + duration;
        if (!(await options.send(frame))) {
          stop();
          return;
        }
      }
    } finally {
      sending = false;
    }
    if (!stopped && ended && !flushed && pendingBytes < 2) {
      pending = [];
      pendingBytes = 0;
      afterFlush();
    }
  }

  function stop(): void {
    if (stopped) return;
    stopped = true;
    pending = [];
    pendingBytes = 0;
    if (wake) clearTimeout(wake);
    if (drainTimer) clearTimeout(drainTimer);
    wake = null;
    drainTimer = null;
    settleDrained();
  }

  return {
    push(pcm) {
      if (stopped || ended || pcm.byteLength === 0) return;
      pending.push(Buffer.from(pcm.buffer, pcm.byteOffset, pcm.byteLength));
      pendingBytes += pcm.byteLength;
      void pump();
    },
    end() {
      if (stopped || ended) return;
      ended = true;
      void pump();
    },
    drained() {
      return isDrained ? Promise.resolve() : new Promise<void>((resolve) => drainWaiters.push(resolve));
    },
    stop,
  };
}
```

- [ ] **Step 5: Run the tests and the typecheck**

```bash
cd "$(git -C /Users/omkar/Desktop/openmaus/OpenGrokBot worktree list --porcelain | awk '/^worktree /{w=substr($0,10)} $0=="branch refs/heads/feat/gadget-voice"{print w}')" && test "$(git branch --show-current)" = feat/gadget-voice || { echo 'not in the feat/gadget-voice worktree'; exit 1; }
export PATH="$HOME/.nvm/versions/node/v24.14.1/bin:$PATH"
pnpm exec vitest run companion/test/gadget/audio.test.ts
pnpm exec tsc -p tsconfig.server.json
```

Expected: `Tests  9 passed (9)`, and `tsc` exits 0.

- [ ] **Step 6: Commit**

```bash
cd "$(git -C /Users/omkar/Desktop/openmaus/OpenGrokBot worktree list --porcelain | awk '/^worktree /{w=substr($0,10)} $0=="branch refs/heads/feat/gadget-voice"{print w}')" && test "$(git branch --show-current)" = feat/gadget-voice || { echo 'not in the feat/gadget-voice worktree'; exit 1; }
git add companion/src/gadget/audio.ts companion/test/gadget/audio.test.ts
git commit -m "feat(companion): WAV packing and real-time speech pacing for gadgets" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 10: The hub's STT client (`companion/src/gadget/stt-client.ts`)

**Files:**
- Create: `companion/src/gadget/stt-client.ts`
- Create: `companion/test/gadget/helpers/voice-fakes.ts` (a private P3b test helper, also used by Task 11)
- Create: `companion/test/gadget/stt-client.test.ts`

**Interfaces:**
- Consumes:
  - `wavFromPcm16` (Task 9);
  - from P3a: `HarnessClient` and `HarnessRaw` (`harness-client.ts`, contract §3.7), `SttFn` (`session.ts`, §3.10) and `foldLatin1` (`shape.ts`, §3.8).
- Produces (contract §3.14), used by Tasks 11 and 12:
  - `createSttClient(harness: HarnessClient): SttFn` and `STT_UNAVAILABLE_COPY`.
  - It calls `harness.raw("POST", "/api/stt", deviceId, wav, "audio/wav", 60_000)`.
  - The `SttOutcome` it returns:

    | Harness answer | Outcome |
    |---|---|
    | 200 with `provider` `apple` or `elevenlabs` | `{ok: true, text (trimmed), provider}` |
    | 409 `stt_unavailable` | `{ok: false, reason: STT_UNAVAILABLE_COPY}` |
    | 502 `stt_key_forbidden` | `{ok: false, reason: folded body.message}` |
    | anything else, or a thrown error | `{ok: false, reason: "Speech-to-text failed"}` |

- Test helpers (`helpers/voice-fakes.ts`):
  - `fakeSession(deviceId?)` returns a `FakeSession`: a `GadgetSessionHandle` plus `log`, `texts()`, `bytesOn(stream)`, `streamsInUse()` and `disconnect()`. Its `allocStream()` behaves like P3a's session as contract §2.12 pins it: host stream ids cycle 1→255→1 and skip ids still in use, so a just-released id is not handed straight back.
  - `deferred<T>()` is also used by Task 12 to hold an `/api/stt` answer.
  - `fakeHarnessClient({json?, raw?})` returns a `FakeHarnessClient` with `jsonCalls` and `rawCalls`.
  - Also `deferred<T>()`, `pcmType(rate)` and `pcmOf(rate, ms)`.
  - If P3a's `GadgetSessionHandle` or `HarnessClient` gained required members beyond contract §3.7 and §3.10, add trivial versions to the fakes. They are test-only.

- [ ] **Step 1: Write the test helper and the failing test**

Create `companion/test/gadget/helpers/voice-fakes.ts`:

```ts
// Stand-ins for one gadget session and the hub's harness client, for the
// voice unit tests (P3b). The session records every text and binary frame in
// one ordered log, so tests can assert what reached the gadget in what order.
import { Buffer } from "node:buffer";
import type { IncomingHttpHeaders } from "node:http";

import type { NormalizedHello } from "../../../src/gadget/enroll.ts";
import type { HarnessClient, HarnessJson, HarnessMethod, HarnessRaw } from "../../../src/gadget/harness-client.ts";
import type { BinaryKindValue, HostToGadget } from "../../../src/gadget/protocol.ts";
import type { GadgetSessionHandle } from "../../../src/gadget/session.ts";

export type Logged =
  | { type: "text"; msg: HostToGadget }
  | { type: "binary"; kind: BinaryKindValue; stream: number; bytes: number };

export interface FakeSession extends GadgetSessionHandle {
  readonly log: Logged[];
  /** Only the text frames, in order. */
  texts(): HostToGadget[];
  /** Bytes sent on one binary stream. */
  bytesOn(stream: number): number;
  streamsInUse(): number[];
  disconnect(): void;
}

export function fakeSession(deviceId = "gad_b18b86ce1389e46d"): FakeSession {
  const log: Logged[] = [];
  const inUse = new Set<number>();
  let next = 1;
  let closed = false;
  return {
    deviceId,
    sessionId: "s_0123456789ab",
    hello: { id: deviceId } as unknown as NormalizedHello,
    connectedAt: 0,
    get closed() { return closed; },
    log,
    send(msg) {
      if (closed) return false;
      log.push({ type: "text", msg });
      return true;
    },
    async sendBinary(kind, stream, payload) {
      if (closed) return false;
      log.push({ type: "binary", kind, stream, bytes: payload.byteLength });
      return true;
    },
    // host streams cycle 1→255→1 and skip ids still in use (contract §2.12)
    allocStream() {
      while (inUse.has(next)) next = (next % 255) + 1;
      const id = next;
      inUse.add(id);
      next = (next % 255) + 1;
      return id;
    },
    releaseStream(stream) { inUse.delete(stream); },
    on() { return () => {}; },
    onClose() { return () => {}; },
    texts: () => log.flatMap((entry) => (entry.type === "text" ? [entry.msg] : [])),
    bytesOn: (stream) => log.reduce((sum, e) => sum + (e.type === "binary" && e.stream === stream ? e.bytes : 0), 0),
    streamsInUse: () => [...inUse],
    disconnect() { closed = true; },
  };
}

export interface JsonCall { method: HarnessMethod; path: string; deviceId: string | null; body: unknown }
export interface RawCall { method: "POST"; path: string; deviceId: string; body: Buffer; json: unknown; contentType: string; timeoutMs?: number }

export interface FakeHarnessClient extends HarnessClient {
  readonly jsonCalls: JsonCall[];
  readonly rawCalls: RawCall[];
}

type Reply<T> = T | Promise<T>;

export function fakeHarnessClient(handlers: {
  json?: (call: JsonCall) => Reply<{ status: number; body: unknown }>;
  raw?: (call: RawCall) => Reply<{ status: number; body?: Uint8Array; headers?: IncomingHttpHeaders }>;
}): FakeHarnessClient {
  const jsonCalls: JsonCall[] = [];
  const rawCalls: RawCall[] = [];
  return {
    jsonCalls,
    rawCalls,
    async json<T>(method: HarnessMethod, path: string, deviceId: string | null, body?: unknown): Promise<HarnessJson<T>> {
      const call = { method, path, deviceId, body };
      jsonCalls.push(call);
      const out = await (handlers.json ?? (() => ({ status: 404, body: { error: "no route" } })))(call);
      return { status: out.status, body: out.body as T, headers: {} };
    },
    async raw(method, path, deviceId, body, contentType, timeoutMs): Promise<HarnessRaw> {
      const buf = Buffer.from(body);
      let json: unknown;
      try { json = JSON.parse(buf.toString("utf8")); } catch { json = undefined; }
      const call = { method, path, deviceId, body: buf, json, contentType, timeoutMs };
      rawCalls.push(call);
      const out: { status: number; body?: Uint8Array; headers?: IncomingHttpHeaders } = await (handlers.raw ?? (() => ({ status: 404 })))(call);
      return { status: out.status, body: Buffer.from(out.body ?? new Uint8Array()), headers: out.headers ?? {} };
    },
    events() { return { close() {} }; },
  };
}

/** A promise the test resolves by hand. */
export function deferred<T>() {
  let resolve!: (value: T) => void;
  const promise = new Promise<T>((r) => { resolve = r; });
  return { promise, resolve };
}

export const pcmType = (rate: number) => ({ "content-type": `audio/pcm;rate=${rate};channels=1;bits=16;endian=little` });
/** `ms` of 16-bit mono PCM at `rate`. */
export const pcmOf = (rate: number, ms: number) => Buffer.alloc((rate * 2 * ms) / 1000, 3);
```

Create `companion/test/gadget/stt-client.test.ts`:

```ts
// The hub's STT call (spec §6.2 voice.end, §6.3): PCM in, WAV to /api/stt
// with the gadget's identity, transcript or a done reason out.
import { Buffer } from "node:buffer";
import { describe, expect, it } from "vitest";

import { HarnessRefused } from "../../src/gadget/harness-client.ts";
import { createSttClient, STT_UNAVAILABLE_COPY } from "../../src/gadget/stt-client.ts";
import { fakeHarnessClient } from "./helpers/voice-fakes.ts";

const pcm = Buffer.from([1, 0, 2, 0, 3, 0, 4, 0]);
const answer = (status: number, body: unknown) => () => ({ status, body: Buffer.from(JSON.stringify(body)) });

describe("createSttClient", () => {
  it("posts the utterance as a 16 kHz WAV with the gadget's id and a 60 s budget", async () => {
    const harness = fakeHarnessClient({ raw: answer(200, { text: " What's on today? ", provider: "apple" }) });
    const result = await createSttClient(harness)({ deviceId: "gad_b18b86ce1389e46d", pcm, rate: 16000 });
    expect(result).toEqual({ ok: true, text: "What's on today?", provider: "apple" });
    const call = harness.rawCalls[0]!;
    expect([call.method, call.path, call.deviceId, call.contentType, call.timeoutMs]).toEqual(["POST", "/api/stt", "gad_b18b86ce1389e46d", "audio/wav", 60_000]);
    expect(call.body.toString("ascii", 0, 4)).toBe("RIFF");
    expect(call.body.readUInt32LE(24)).toBe(16000);
    expect(call.body.subarray(44)).toEqual(pcm);
  });

  it("passes an empty transcript through (the session says Didn't catch that)", async () => {
    const harness = fakeHarnessClient({ raw: answer(200, { text: "", provider: "elevenlabs" }) });
    expect(await createSttClient(harness)({ deviceId: "gad_1", pcm, rate: 16000 })).toEqual({ ok: true, text: "", provider: "elevenlabs" });
  });

  it("turns stt_unavailable into the set-up copy and a forbidden key into the harness's message", async () => {
    const unavailable = fakeHarnessClient({ raw: answer(409, { error: "stt_unavailable", message: "whatever" }) });
    expect(await createSttClient(unavailable)({ deviceId: "gad_1", pcm, rate: 16000 })).toEqual({ ok: false, reason: STT_UNAVAILABLE_COPY });
    const forbidden = fakeHarnessClient({ raw: answer(502, { error: "stt_key_forbidden", message: "Your ElevenLabs key can’t use Speech to Text." }) });
    // folded to the gadget's Latin-1: the curly apostrophe becomes '
    expect(await createSttClient(forbidden)({ deviceId: "gad_1", pcm, rate: 16000 })).toEqual({ ok: false, reason: "Your ElevenLabs key can't use Speech to Text." });
  });

  it("says Speech-to-text failed for anything else, including a refused or unreachable harness", async () => {
    for (const harness of [
      fakeHarnessClient({ raw: answer(502, { error: "stt_failed", message: "Speech-to-text failed. Try again." }) }),
      fakeHarnessClient({ raw: answer(400, { error: "bad_audio" }) }),
      fakeHarnessClient({ raw: () => ({ status: 200, body: Buffer.from("not json") }) }),
      fakeHarnessClient({ raw: answer(200, { text: "hi", provider: "someone-else" }) }),
      fakeHarnessClient({ raw: () => { throw new HarnessRefused(503, "token pending"); } }),
    ]) {
      expect(await createSttClient(harness)({ deviceId: "gad_1", pcm, rate: 16000 })).toEqual({ ok: false, reason: "Speech-to-text failed" });
    }
  });

  it("keeps the set-up copy within the gadget's 159-byte done reason", () => {
    expect(Buffer.byteLength(STT_UNAVAILABLE_COPY)).toBeLessThanOrEqual(159);
  });
});
```

- [ ] **Step 2: Run it to see it fail**

```bash
cd "$(git -C /Users/omkar/Desktop/openmaus/OpenGrokBot worktree list --porcelain | awk '/^worktree /{w=substr($0,10)} $0=="branch refs/heads/feat/gadget-voice"{print w}')" && test "$(git branch --show-current)" = feat/gadget-voice || { echo 'not in the feat/gadget-voice worktree'; exit 1; }
export PATH="$HOME/.nvm/versions/node/v24.14.1/bin:$PATH"
pnpm exec vitest run companion/test/gadget/stt-client.test.ts
```

Expected: FAIL with `Error: Cannot find module '../../src/gadget/stt-client.ts' imported from …/companion/test/gadget/stt-client.test.ts`.

- [ ] **Step 3: Implement `companion/src/gadget/stt-client.ts`**

```ts
// The hub's speech-to-text (spec §6.2 voice.end, §6.3): the utterance's PCM
// is packed as WAV and sent to the harness's POST /api/stt with the
// gadget's own device identity. Failures become the turn's done reason,
// folded to the gadget's Latin-1 screen text.
import { Buffer } from "node:buffer";

import { wavFromPcm16 } from "./audio.ts";
import type { HarnessClient, HarnessRaw } from "./harness-client.ts";
import type { SttFn } from "./session.ts";
import { foldLatin1 } from "./shape.ts";

const STT_TIMEOUT_MS = 60_000;

/** Same text as the harness's STT_UNAVAILABLE_MESSAGE (server/routes/stt.ts);
 * the companion cannot import server/. */
export const STT_UNAVAILABLE_COPY =
  "Speech-to-text isn't set up. On a Mac, allow Speech Recognition for OpenMausBot (try dictation once), or add an ElevenLabs key in a bot's voice settings.";
const STT_FAILED_COPY = "Speech-to-text failed";

function parse(body: Uint8Array): Record<string, unknown> | null {
  try {
    const value = JSON.parse(Buffer.from(body).toString("utf8")) as unknown;
    return value && typeof value === "object" ? (value as Record<string, unknown>) : null;
  } catch {
    return null;
  }
}

/** wavFromPcm16 → harness.raw("POST", "/api/stt", deviceId, wav, "audio/wav", 60_000).
 *  200 {text, provider} → ok; 409 stt_unavailable → STT_UNAVAILABLE_COPY; 502 stt_key_forbidden →
 *  body.message; anything else → "Speech-to-text failed". All reasons folded to Latin-1. */
export function createSttClient(harness: HarnessClient): SttFn {
  return async ({ deviceId, pcm, rate }) => {
    let res: HarnessRaw;
    try {
      res = await harness.raw("POST", "/api/stt", deviceId, wavFromPcm16(pcm, rate), "audio/wav", STT_TIMEOUT_MS);
    } catch {
      // HarnessRefused (token pending, route refused), a timeout, the harness gone
      return { ok: false, reason: STT_FAILED_COPY };
    }
    const body = parse(res.body);
    if (res.status === 200 && typeof body?.text === "string" && (body.provider === "apple" || body.provider === "elevenlabs")) {
      return { ok: true, text: body.text.trim(), provider: body.provider };
    }
    if (res.status === 409 && body?.error === "stt_unavailable") return { ok: false, reason: foldLatin1(STT_UNAVAILABLE_COPY) };
    if (res.status === 502 && body?.error === "stt_key_forbidden" && typeof body.message === "string" && body.message.trim()) {
      return { ok: false, reason: foldLatin1(body.message.trim()) };
    }
    return { ok: false, reason: STT_FAILED_COPY };
  };
}
```

- [ ] **Step 4: Run the tests and the typecheck**

```bash
cd "$(git -C /Users/omkar/Desktop/openmaus/OpenGrokBot worktree list --porcelain | awk '/^worktree /{w=substr($0,10)} $0=="branch refs/heads/feat/gadget-voice"{print w}')" && test "$(git branch --show-current)" = feat/gadget-voice || { echo 'not in the feat/gadget-voice worktree'; exit 1; }
export PATH="$HOME/.nvm/versions/node/v24.14.1/bin:$PATH"
pnpm exec vitest run companion/test/gadget/stt-client.test.ts
pnpm exec tsc -p tsconfig.server.json
```

Expected: `Tests  5 passed (5)`, and `tsc` exits 0.

- [ ] **Step 5: Commit**

```bash
cd "$(git -C /Users/omkar/Desktop/openmaus/OpenGrokBot worktree list --porcelain | awk '/^worktree /{w=substr($0,10)} $0=="branch refs/heads/feat/gadget-voice"{print w}')" && test "$(git branch --show-current)" = feat/gadget-voice || { echo 'not in the feat/gadget-voice worktree'; exit 1; }
git add companion/src/gadget/stt-client.ts companion/test/gadget/helpers/voice-fakes.ts companion/test/gadget/stt-client.test.ts
git commit -m "feat(companion): gadget speech-to-text through /api/stt" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 11: Spoken replies and pushes (`companion/src/gadget/speech.ts`)

**Files:**
- Create: `companion/src/gadget/speech.ts`
- Create: `companion/test/gadget/speech.test.ts`

**Interfaces:**
- Consumes:
  - `createSpeechPacer` and `SpeechPacer` (Task 9), `createSttClient` (Task 10), and the test helpers (Task 10);
  - from P3a: `BinaryKind` and `CardMsg` (`protocol.ts`), `SpeechOut`, `SpeechOutFactory` and `VoiceProvider` (`session.ts`), and `foldLatin1` (`shape.ts`).
- Produces (contract §3.14), used by Task 12:
  - `VOICE_OFF_CARD: CardMsg`
  - `createSpeechOut: SpeechOutFactory`
  - `createGadgetVoice: VoiceProvider`, which is `(harness) => ({ stt: createSttClient(harness), speech: createSpeechOut })`
- The `SpeechOut` it returns:
  - `replyFinal({turn, rawText, voiceId})` goes ahead of pushes that have not started.
  - `post({rawText, voiceId})` queues behind everything.
  - Queued speech starts `SPEECH_GAP_MS` (300 ms) after the earlier stream has played out on the host's clock (`pacer.drained()`). The gadget plays later than that, by its jitter-buffer prefill plus the network, and a new `speak.begin` replaces what is playing (spec §4.4), so without the gap a push could clip the reply's last syllables. The earlier stream stays allocated through the gap.
  - `stop()` sends `speak.stop {stream}` synchronously through `session.send` when a stream is open (also during the gap), then drops all queued audio and late answers and cancels the gap's timer.
  - `close()` drops everything without sending, and cancels the gap's timer.
  - With `rate === null`, every method is a no-op.

- [ ] **Step 1: Write the failing test**

Create `companion/test/gadget/speech.test.ts`:

```ts
// Spoken replies and pushes (spec §6.2 Speech). A recording session and a
// scripted harness client stand in for the hub; time is vitest's fake clock,
// so pacing is exact and nothing sleeps.
import { Buffer } from "node:buffer";
import { afterEach, beforeEach, describe, expect, it, vi } from "vitest";

import { BinaryKind } from "../../src/gadget/protocol.ts";
import { HarnessRefused } from "../../src/gadget/harness-client.ts";
import { createGadgetVoice, createSpeechOut, VOICE_OFF_CARD } from "../../src/gadget/speech.ts";
import { deferred, fakeHarnessClient, fakeSession, pcmOf, pcmType, type JsonCall, type RawCall } from "./helpers/voice-fakes.ts";

beforeEach(() => {
  vi.useFakeTimers();
  vi.setSystemTime(5_000_000);
});
afterEach(() => vi.useRealTimers());

const settle = (ms = 0) => vi.advanceTimersByTimeAsync(ms);

const prepared = (utterances: string[], ready = true) => (call: JsonCall) =>
  call.path === "/api/tts/prepare" ? { status: 200, body: { ready, utterances } } : { status: 404, body: {} };
/** Every utterance becomes `ms` of PCM at `rate`. */
const speaks = (rate: number, ms: number) => (_call: RawCall) => ({ status: 200, body: pcmOf(rate, ms), headers: pcmType(rate) });

function setup(options: { rate?: 16000 | 24000 | null; json?: Parameters<typeof fakeHarnessClient>[0]["json"]; raw?: Parameters<typeof fakeHarnessClient>[0]["raw"] } = {}) {
  const session = fakeSession();
  const harness = fakeHarnessClient({ json: options.json ?? prepared(["Hello there.", "Second part."]), raw: options.raw ?? speaks(16000, 100) });
  const speech = createSpeechOut({ session, rate: options.rate === undefined ? 16000 : options.rate, harness });
  return { session, harness, speech };
}

const ops = (session: ReturnType<typeof fakeSession>) => session.texts().map((m) => m.op);

describe("createSpeechOut", () => {
  it("speaks a final reply: raw text to prepare, one pcm request per utterance, one stream", async () => {
    const { session, harness, speech } = setup();
    speech.replyFinal({ turn: "t1-1", rawText: "Hello **there**.\n\nSecond part.", voiceId: "v-1" });
    await settle(1_000);
    expect(harness.jsonCalls).toEqual([{ method: "POST", path: "/api/tts/prepare", deviceId: session.deviceId, body: { text: "Hello **there**.\n\nSecond part.", voiceId: "v-1" } }]);
    expect(harness.rawCalls.map((c) => [c.path, c.deviceId, c.contentType, c.timeoutMs, c.json])).toEqual([
      ["/api/tts/speak", session.deviceId, "application/json", 30_000, { text: "Hello there.", voiceId: "v-1", format: "pcm_16000" }],
      ["/api/tts/speak", session.deviceId, "application/json", 30_000, { text: "Second part.", voiceId: "v-1", format: "pcm_16000" }],
    ]);
    expect(session.texts()).toEqual([
      { op: "speak.begin", stream: 1, rate: 16000, turn: "t1-1" },
      { op: "speak.end", stream: 1 },
    ]);
    expect(session.bytesOn(1)).toBe(2 * 3200);
    expect(session.log.filter((e) => e.type === "binary").every((e) => e.type === "binary" && e.kind === BinaryKind.speaker && e.bytes === 1280)).toBe(true);
    expect(session.streamsInUse()).toEqual([]);               // released once played out
  });

  it("starts playing the first utterance while the next one is still synthesizing", async () => {
    const second = deferred<{ status: number; body: Buffer; headers: Record<string, string> }>();
    let n = 0;
    const { session, speech } = setup({ raw: () => (++n === 1 ? { status: 200, body: pcmOf(16000, 100), headers: pcmType(16000) } : second.promise) });
    speech.replyFinal({ turn: "t1", rawText: "Two parts." });
    await settle(0);
    expect(ops(session)).toEqual(["speak.begin"]);
    // the first utterance is playing: two whole 40 ms frames are out, and its
    // last 20 ms waits to be joined with the next utterance's audio
    expect(session.bytesOn(1)).toBe(2560);
    second.resolve({ status: 200, body: pcmOf(16000, 100), headers: pcmType(16000) });
    await settle(500);
    expect(ops(session)).toEqual(["speak.begin", "speak.end"]);
    expect(session.bytesOn(1)).toBe(6400);
  });

  it("uses pcm_24000 and 24 kHz frames for a 24 kHz speaker", async () => {
    const { session, harness, speech } = setup({ rate: 24000, raw: speaks(24000, 80) });
    speech.replyFinal({ turn: "t1", rawText: "Hi." });
    await settle(500);
    expect((harness.rawCalls[0]!.json as { format: string }).format).toBe("pcm_24000");
    expect(session.texts()[0]).toEqual({ op: "speak.begin", stream: 1, rate: 24000, turn: "t1" });
    expect(session.log.filter((e) => e.type === "binary").map((e) => (e.type === "binary" ? e.bytes : 0))).toContain(1920);
  });

  it("does nothing at all for a gadget without a speaker", async () => {
    const { session, harness, speech } = setup({ rate: null });
    speech.replyFinal({ turn: "t1", rawText: "Hi." });
    speech.post({ rawText: "Routine done." });
    speech.stop();
    await settle(1_000);
    expect(harness.jsonCalls).toHaveLength(0);
    expect(harness.rawCalls).toHaveLength(0);
    expect(session.log).toHaveLength(0);
  });

  it("shows one Voice is off card on the first 409 and asks for no more speech in the session", async () => {
    const { session, harness, speech } = setup({ raw: () => ({ status: 409, body: Buffer.from('{"error":"Pick a voice"}') }) });
    speech.replyFinal({ turn: "t1", rawText: "One." });
    await settle(100);
    expect(session.texts()).toEqual([VOICE_OFF_CARD]);
    expect(VOICE_OFF_CARD).toEqual({ op: "card", id: "notice-voice", title: "Voice is off", body: "Add a voice in this bot's voice settings to hear replies.", ttl_s: 8 });
    speech.replyFinal({ turn: "t2", rawText: "Two." });
    speech.post({ rawText: "A routine finished." });
    await settle(100);
    expect(harness.jsonCalls).toHaveLength(1);
    expect(harness.rawCalls).toHaveLength(1);
    expect(session.texts()).toEqual([VOICE_OFF_CARD]);
  });

  it("treats prepare's ready:false like the 409, without a speak call", async () => {
    const { session, harness, speech } = setup({ json: prepared(["One."], false) });
    speech.replyFinal({ turn: "t1", rawText: "One." });
    await settle(100);
    expect(harness.rawCalls).toHaveLength(0);
    expect(session.texts()).toEqual([VOICE_OFF_CARD]);
  });

  it("keeps a reply text-only on 415 pcm_unsupported, with no card, and still speaks the next one", async () => {
    let n = 0;
    const { session, speech } = setup({
      raw: () => (++n === 1
        ? { status: 415, body: Buffer.from('{"error":"pcm_unsupported"}') }
        : { status: 200, body: pcmOf(16000, 40), headers: pcmType(16000) }),
    });
    speech.replyFinal({ turn: "t1", rawText: "MP3 voice." });
    await settle(100);
    expect(session.log).toHaveLength(0);
    speech.replyFinal({ turn: "t2", rawText: "Next." });
    await settle(500);
    expect(session.texts()).toEqual([
      { op: "speak.begin", stream: 1, rate: 16000, turn: "t2" },
      { op: "speak.end", stream: 1 },
    ]);
  });

  it("never plays a 200 that is not PCM at its rate (an older harness answering MP3)", async () => {
    const { session, speech } = setup({ raw: () => ({ status: 200, body: Buffer.from([0xff, 0xfb, 0x90, 0x00]), headers: { "content-type": "audio/mpeg" } }) });
    speech.replyFinal({ turn: "t1", rawText: "Hi." });
    await settle(100);
    expect(session.log).toHaveLength(0);
  });

  it("stays quiet when prepare fails or the harness refuses the call", async () => {
    const failing = setup({ json: () => ({ status: 500, body: {} }) });
    failing.speech.replyFinal({ turn: "t1", rawText: "Hi." });
    const refused = setup({ raw: () => { throw new HarnessRefused(503, "token pending"); } });
    refused.speech.replyFinal({ turn: "t1", rawText: "Hi." });
    await settle(100);
    expect(failing.session.log).toHaveLength(0);
    expect(refused.session.log).toHaveLength(0);
  });

  it("barge-in: stop() sends speak.stop for the playing stream before it returns, and nothing follows", async () => {
    const { session, speech } = setup({ raw: speaks(16000, 3_000) });
    speech.replyFinal({ turn: "t1", rawText: "A long answer." });
    await settle(200);
    expect(ops(session)).toEqual(["speak.begin"]);
    speech.stop();
    expect(session.log.at(-1)).toEqual({ type: "text", msg: { op: "speak.stop", stream: 1 } });
    const after = session.log.length;
    await settle(10_000);
    expect(session.log).toHaveLength(after);                   // no frames, no speak.end
    expect(session.streamsInUse()).toEqual([]);
  });

  it("barge-in while synthesizing: the late audio is dropped and the next reply plays at once", async () => {
    const late = deferred<{ status: number; body: Buffer; headers: Record<string, string> }>();
    let n = 0;
    const { session, speech } = setup({ raw: () => (++n === 1 ? late.promise : { status: 200, body: pcmOf(16000, 40), headers: pcmType(16000) }) });
    speech.replyFinal({ turn: "t1", rawText: "Old." });
    await settle(0);
    speech.stop();                                             // nothing playing yet: no speak.stop
    speech.replyFinal({ turn: "t2", rawText: "New." });
    await settle(200);
    late.resolve({ status: 200, body: pcmOf(16000, 1_000), headers: pcmType(16000) });
    await settle(2_000);
    expect(session.texts()).toEqual([
      { op: "speak.begin", stream: 1, rate: 16000, turn: "t2" },
      { op: "speak.end", stream: 1 },
    ]);
  });

  it("queues a spoken push after the reply's speech has played, plus a gap for the gadget's buffer", async () => {
    const { session, speech } = setup({ raw: speaks(16000, 1_000) });
    speech.replyFinal({ turn: "t1", rawText: "Reply." });
    speech.post({ rawText: "Your routine finished." });
    // two utterances of 1 s each: the reply plays until t = 2 s on the host's
    // clock; the push may begin 300 ms later, on the next stream id
    await settle(2_250);
    expect(session.texts().filter((m) => m.op === "speak.begin")).toEqual([{ op: "speak.begin", stream: 1, rate: 16000, turn: "t1" }]);
    expect(session.streamsInUse()).toEqual([1]);              // held through the gap
    await settle(100);
    expect(session.texts().filter((m) => m.op === "speak.begin")).toEqual([
      { op: "speak.begin", stream: 1, rate: 16000, turn: "t1" },
      { op: "speak.begin", stream: 2, rate: 16000 },
    ]);
  });

  it("barge-in during the gap: speak.stop for the reply's stream, and the queued push never starts", async () => {
    const { session, speech } = setup({ raw: speaks(16000, 1_000) });
    speech.replyFinal({ turn: "t1", rawText: "Reply." });
    speech.post({ rawText: "Your routine finished." });
    await settle(2_100);                                       // played out on the host's clock, inside the gap
    speech.stop();
    expect(session.log.at(-1)).toEqual({ type: "text", msg: { op: "speak.stop", stream: 1 } });
    await settle(2_000);
    expect(ops(session)).toEqual(["speak.begin", "speak.end", "speak.stop"]);
    expect(session.streamsInUse()).toEqual([]);
  });

  it("lets a reply go ahead of pushes that have not started", async () => {
    const { session, harness, speech } = setup({ json: (call) => ({ status: 200, body: { ready: true, utterances: [(call.body as { text: string }).text] } }), raw: speaks(16000, 200) });
    speech.post({ rawText: "Push A." });
    speech.post({ rawText: "Push B." });
    speech.replyFinal({ turn: "t1", rawText: "Reply." });
    await settle(2_000);
    expect(harness.jsonCalls.map((c) => (c.body as { text: string }).text)).toEqual(["Push A.", "Reply.", "Push B."]);
    expect(session.texts().filter((m) => m.op === "speak.begin").map((m) => ("turn" in m ? m.turn : undefined))).toEqual([undefined, "t1", undefined]);
  });

  it("close() drops everything without a word to the gone gadget", async () => {
    const { session, speech } = setup({ raw: speaks(16000, 3_000) });
    speech.replyFinal({ turn: "t1", rawText: "Long." });
    await settle(100);
    const before = session.log.length;
    speech.close();
    speech.replyFinal({ turn: "t2", rawText: "After close." });
    await settle(5_000);
    expect(session.log).toHaveLength(before);
    expect(session.streamsInUse()).toEqual([]);
  });
});

describe("createGadgetVoice", () => {
  it("pairs the STT client with the speech factory for the hub", async () => {
    const harness = fakeHarnessClient({ raw: () => ({ status: 200, body: Buffer.from('{"text":"hi","provider":"apple"}') }) });
    const voice = createGadgetVoice(harness);
    expect(voice.speech).toBe(createSpeechOut);
    expect(await voice.stt({ deviceId: "gad_1", pcm: Buffer.alloc(4), rate: 16000 })).toEqual({ ok: true, text: "hi", provider: "apple" });
    expect(harness.rawCalls[0]!.path).toBe("/api/stt");
  });
});
```

- [ ] **Step 2: Run it to see it fail**

```bash
cd "$(git -C /Users/omkar/Desktop/openmaus/OpenGrokBot worktree list --porcelain | awk '/^worktree /{w=substr($0,10)} $0=="branch refs/heads/feat/gadget-voice"{print w}')" && test "$(git branch --show-current)" = feat/gadget-voice || { echo 'not in the feat/gadget-voice worktree'; exit 1; }
export PATH="$HOME/.nvm/versions/node/v24.14.1/bin:$PATH"
pnpm exec vitest run companion/test/gadget/speech.test.ts
```

Expected: FAIL with `Error: Cannot find module '../../src/gadget/speech.ts' imported from …/companion/test/gadget/speech.test.ts`.

- [ ] **Step 3: Implement `companion/src/gadget/speech.ts`**

```ts
// Spoken replies and pushes on a gadget (spec §6.2 Speech, §6.3).
//
// 1. The RAW final reply goes to POST /api/tts/prepare, which already makes
//    it speakable (speakable() → utterances); it is never pre-shaped here.
// 2. Each utterance goes to POST /api/tts/speak with format pcm_<rate>; the
//    first one plays while the rest synthesize.
// 3. One stream per reply: speak.begin {stream, rate, turn?}, the PCM of
//    every utterance in order, one speak.end once the last frame is sent.
// 4. A post with speak:true queues behind reply speech instead of replacing
//    it; a reply goes ahead of posts that have not started. Queued speech
//    starts SPEECH_GAP_MS after the earlier stream has played out on the
//    host's clock, because the gadget plays later than that (its jitter
//    buffer and the network) and a new speak.begin replaces what is playing
//    (spec §4.4). Nothing playing is ever cut except by stop(), which the
//    session calls when a turn is stopped or a new one begins (barge-in).
// 5. The first "no voice" (409, or prepare's ready:false) in a session shows
//    one notice card and turns speech off for that session. A 415
//    pcm_unsupported (an MP3-only voice) leaves that reply text-only.
import { Buffer } from "node:buffer";

import { createSpeechPacer, type SpeechPacer } from "./audio.ts";
import { BinaryKind, type CardMsg } from "./protocol.ts";
import type { SpeechOut, SpeechOutFactory, VoiceProvider } from "./session.ts";
import { foldLatin1 } from "./shape.ts";
import { createSttClient } from "./stt-client.ts";

const SPEAK_TIMEOUT_MS = 30_000;
/** Extra wait after a stream has played out on the host's clock before the
 *  next queued speech may begin, so the gadget can finish the tail. */
const SPEECH_GAP_MS = 300;

export const VOICE_OFF_CARD: CardMsg = {
  op: "card",
  id: "notice-voice",
  title: foldLatin1("Voice is off"),
  body: foldLatin1("Add a voice in this bot's voice settings to hear replies."),
  ttl_s: 8,
};

interface Job { kind: "reply" | "post"; rawText: string; voiceId?: string; turn?: string }
type Prepared = { kind: "ok"; utterances: string[] } | { kind: "voice-off" } | { kind: "failed" };
type Synth = { kind: "pcm"; pcm: Buffer } | { kind: "voice-off" } | { kind: "unsupported" } | { kind: "failed" };

const TEXT_ONLY: SpeechOut = { replyFinal() {}, post() {}, stop() {}, close() {} };

export const createSpeechOut: SpeechOutFactory = (context) => {
  if (context.rate === null) return TEXT_ONLY;     // no speaker, or a rate the host cannot serve
  const rate = context.rate;
  const { session, harness } = context;
  const log = context.log ?? (() => {});
  const format = `pcm_${rate}` as const;

  const jobs: Job[] = [];
  let generation = 0;
  let running = false;
  let closed = false;
  let voiceOff = false;
  let active: { stream: number; pacer: SpeechPacer } | null = null;
  let gap: { timer: ReturnType<typeof setTimeout>; done: () => void } | null = null;

  /** SPEECH_GAP_MS, cut short by stop() and close(). */
  const waitGap = () =>
    new Promise<void>((resolve) => {
      gap = { timer: setTimeout(() => { gap = null; resolve(); }, SPEECH_GAP_MS), done: resolve };
    });
  const cancelGap = () => {
    if (!gap) return;
    clearTimeout(gap.timer);
    gap.done();
    gap = null;
  };

  const turnVoiceOff = () => {
    if (voiceOff) return;
    voiceOff = true;
    jobs.length = 0;
    session.send(VOICE_OFF_CARD);
  };

  async function prepare(job: Job): Promise<Prepared> {
    try {
      const res = await harness.json<{ ready?: unknown; utterances?: unknown }>(
        "POST", "/api/tts/prepare", session.deviceId, { text: job.rawText, voiceId: job.voiceId },
      );
      if (res.status !== 200) return { kind: "failed" };
      if (res.body.ready === false) return { kind: "voice-off" };
      const utterances = Array.isArray(res.body.utterances) ? res.body.utterances.filter((u): u is string => typeof u === "string" && u.trim() !== "") : [];
      return { kind: "ok", utterances };
    } catch {
      return { kind: "failed" };
    }
  }

  async function synth(text: string, voiceId: string | undefined): Promise<Synth> {
    try {
      const res = await harness.raw(
        "POST", "/api/tts/speak", session.deviceId,
        Buffer.from(JSON.stringify({ text, voiceId, format })), "application/json", SPEAK_TIMEOUT_MS,
      );
      if (res.status === 409) return { kind: "voice-off" };
      if (res.status === 415) return { kind: "unsupported" };
      if (res.status !== 200) return { kind: "failed" };
      // Only raw little-endian PCM at our rate; anything else (an MP3 from
      // a harness that ignored `format`) would play as noise.
      const type = String(res.headers["content-type"] ?? "").toLowerCase();
      if (!type.startsWith("audio/pcm") || !type.includes(`rate=${rate}`)) return { kind: "unsupported" };
      return { kind: "pcm", pcm: res.body.subarray(0, res.body.byteLength - (res.body.byteLength % 2)) };
    } catch {
      return { kind: "failed" };
    }
  }

  /** Stop what is playing: speak.stop now (when `tell`), drop its audio. */
  function dropActive(tell: boolean): void {
    if (!active) return;
    const { stream, pacer } = active;
    active = null;
    if (tell) session.send({ op: "speak.stop", stream });
    pacer.stop();
    session.releaseStream(stream);
  }

  async function run(job: Job, mine: number): Promise<void> {
    const stale = () => mine !== generation || closed;
    const prepared = await prepare(job);
    if (stale()) return;
    if (prepared.kind === "voice-off") return turnVoiceOff();
    if (prepared.kind === "failed") return;
    let pacer: SpeechPacer | null = null;
    let stream = 0;
    for (const text of prepared.utterances) {
      const out = await synth(text, job.voiceId);
      if (stale()) return;
      if (out.kind === "voice-off") {
        turnVoiceOff();
        break;
      }
      if (out.kind !== "pcm") {
        if (out.kind === "failed") log("gadget speech: an utterance failed; the rest of this reply is text only");
        break;
      }
      if (out.pcm.byteLength === 0) continue;
      if (!pacer) {
        const s = session.allocStream();
        session.send({ op: "speak.begin", stream: s, rate, turn: job.turn });
        const p = createSpeechPacer({
          rate,
          send: (frame) => session.sendBinary(BinaryKind.speaker, s, frame),
          onFlushed: () => {
            if (!stale()) session.send({ op: "speak.end", stream: s });
          },
        });
        stream = s;
        pacer = p;
        active = { stream: s, pacer: p };
      }
      pacer.push(out.pcm);
    }
    if (!pacer) return;
    pacer.end();
    await pacer.drained();                      // played out on the host's clock
    if (stale()) return;                        // stop() or close() already let go of it
    if (jobs.length > 0) {
      await waitGap();                          // the gadget is still playing the tail
      if (stale()) return;
    }
    if (active?.stream === stream) {
      active = null;
      session.releaseStream(stream);
    }
  }

  function pump(): void {
    if (running || closed || voiceOff) return;
    const job = jobs.shift();
    if (!job) return;
    running = true;
    const mine = generation;
    void run(job, mine)
      .catch((error: unknown) => {
        log(`gadget speech: ${error instanceof Error ? error.message : String(error)}`);
        if (mine === generation) dropActive(true);
      })
      .finally(() => {
        if (mine !== generation) return;       // stop() already reset the queue
        running = false;
        pump();
      });
  }

  return {
    replyFinal({ turn, rawText, voiceId }) {
      if (closed || voiceOff || !rawText.trim()) return;
      const firstPost = jobs.findIndex((job) => job.kind === "post");
      jobs.splice(firstPost < 0 ? jobs.length : firstPost, 0, { kind: "reply", rawText, voiceId, turn });
      pump();
    },
    post({ rawText, voiceId }) {
      if (closed || voiceOff || !rawText.trim()) return;
      jobs.push({ kind: "post", rawText, voiceId });
      pump();
    },
    stop() {
      generation++;
      running = false;
      jobs.length = 0;
      cancelGap();
      dropActive(true);
    },
    close() {
      closed = true;
      generation++;
      running = false;
      jobs.length = 0;
      cancelGap();
      dropActive(false);
    },
  };
};

/** The hub's voice option: { stt: createSttClient(harness), speech: createSpeechOut }. */
export const createGadgetVoice: VoiceProvider = (harness) => ({ stt: createSttClient(harness), speech: createSpeechOut });
```

- [ ] **Step 4: Run the tests and the typecheck**

```bash
cd "$(git -C /Users/omkar/Desktop/openmaus/OpenGrokBot worktree list --porcelain | awk '/^worktree /{w=substr($0,10)} $0=="branch refs/heads/feat/gadget-voice"{print w}')" && test "$(git branch --show-current)" = feat/gadget-voice || { echo 'not in the feat/gadget-voice worktree'; exit 1; }
export PATH="$HOME/.nvm/versions/node/v24.14.1/bin:$PATH"
pnpm exec vitest run companion/test/gadget/speech.test.ts companion/test/gadget/stt-client.test.ts companion/test/gadget/audio.test.ts
pnpm exec tsc -p tsconfig.server.json
```

Expected: `Test Files  3 passed (3)` and `Tests  30 passed (30)` (16 + 5 + 9), and `tsc` exits 0.

- [ ] **Step 5: Commit**

```bash
cd "$(git -C /Users/omkar/Desktop/openmaus/OpenGrokBot worktree list --porcelain | awk '/^worktree /{w=substr($0,10)} $0=="branch refs/heads/feat/gadget-voice"{print w}')" && test "$(git branch --show-current)" = feat/gadget-voice || { echo 'not in the feat/gadget-voice worktree'; exit 1; }
git add companion/src/gadget/speech.ts companion/test/gadget/speech.test.ts
git commit -m "feat(companion): speak gadget replies and pushes as paced PCM" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 12: Give the hub its real voice, and prove a whole voice turn

**Files:**
- Modify: `companion/src/index.ts`, at the P3b column of contract §3.12: P3a's `createGadgetHub({…, voice: undefined, …})`, written after `connectedDevices` (origin/main `:155`). Find the line with `grep -n "voice: undefined" companion/src/index.ts`.
- Create: `companion/test/gadget/voice-hub.test.ts`

**Interfaces:**
- Consumes:
  - `createGadgetVoice` and `VOICE_OFF_CARD` (Task 11), and `STT_UNAVAILABLE_COPY` (Task 10);
  - `deferred` (Task 10's `helpers/voice-fakes.ts`);
  - from P3a: `createGadgetHub`, `GadgetHub` and `GadgetHub.settingsChanged` (`hub.ts`, contract §3.11), `DeviceRegistry.openPairing(botId)` and `DeviceRegistry.updateGadget(id, {speakPushes})` (§3.5), `DATA_DIR` (`companion/src/state.ts`), `startFakeHarness`, `FakeHarness`, `FakeRoute` (which may answer with a Promise) and `RecordedRequest` (`helpers/fake-harness.ts`, §3.19), and `connectTestGadget` and `TestGadget` (`helpers/gadget-client.ts`, §3.19).
- Produces: a companion whose hub runs with `voice: createGadgetVoice`.

The hub test drives P3a's real session mapping, and it emits the harness frames spec §6.2 names:
- the user message;
- `runtime turn.started`;
- the bot's message, with `requestMessageId` pointing at the user message;
- its `message.patch` with `turnTerminal: true`;
- `runtime turn.completed`;
- for the spoken push, a `routine.run` frame entering `completed`.

The barge-in cases hold the new turn's `/api/stt` answer on a deferred (`holdStt`). Nothing of the new turn can reach the gadget before that answer, so every frame the test awaits before releasing it provably came first. `TestGadget.next(op)` filters by op, so the order between `speak.stop` and `done` themselves is pinned in Task 11 ("barge-in: stop() sends speak.stop…": `stop()` sends it synchronously, before P3a's session sends `done`).

If a case fails because P3a's session or hub behaves differently from spec §4.4 or §6.2 (not because of P3b code), do not edit P3a's files. Record it under "Contract deviations" below and stop for review.

- [ ] **Step 1: Write the failing test**

Create `companion/test/gadget/voice-hub.test.ts`:

```ts
// A whole voice turn through the real hub (spec §6.2, §6.3): a test gadget
// talks over the WebSocket, the hub uses P3b's real voice (createGadgetVoice),
// and P3a's fake harness plays the harness. The harness's own frames are
// emitted the way server/index.ts broadcasts them: the user message, the
// bot's message, its turnTerminal patch, then turn.completed.
import { Buffer } from "node:buffer";
import { readFileSync, rmSync } from "node:fs";
import { createServer, type Server } from "node:http";
import type { AddressInfo } from "node:net";
import { afterEach, beforeEach, describe, expect, it } from "vitest";

import { DeviceRegistry } from "../../src/devices.ts";
import { createGadgetHub, type GadgetHub } from "../../src/gadget/hub.ts";
import { BinaryKind, type GadgetCaps } from "../../src/gadget/protocol.ts";
import { createGadgetVoice, VOICE_OFF_CARD } from "../../src/gadget/speech.ts";
import { STT_UNAVAILABLE_COPY } from "../../src/gadget/stt-client.ts";
import type { WireBotLite } from "../../src/gadget/types.ts";
import { DATA_DIR } from "../../src/state.ts";
import { startFakeHarness, type FakeHarness, type FakeRoute, type RecordedRequest } from "./helpers/fake-harness.ts";
import { connectTestGadget, type TestGadget } from "./helpers/gadget-client.ts";
import { deferred } from "./helpers/voice-fakes.ts";

const BOT: WireBotLite = { id: "b_jev", name: "Jev", threadId: "th_1", voice: "v-1", tasks: [{ threadId: "th_1", title: "Main" }] };
const CAPS: GadgetCaps = { screen: { w: 466, h: 466, round: true, text: "latin1" }, mic: { rate: 16000 }, speaker: { rate: 16000 }, input: ["touch", "talk", "cancel"] };
const PCM_TYPE = "audio/pcm;rate=16000;channels=1;bits=16;endian=little";

let harness: FakeHarness;
let devices: DeviceRegistry;
let hub: GadgetHub;
let server: Server;
let gadget: TestGadget;

/** Calls to one fake-harness route, with a way to wait for the n-th. */
function recorder(answer: FakeRoute) {
  const calls: RecordedRequest[] = [];
  const waiting: Array<{ n: number; resolve: (req: RecordedRequest) => void }> = [];
  const route: FakeRoute = (req) => {
    calls.push(req);
    for (const w of waiting.splice(0)) {
      if (calls.length > w.n) w.resolve(calls[w.n]!);
      else waiting.push(w);
    }
    return answer(req);
  };
  const nth = (n: number) => (calls.length > n ? Promise.resolve(calls[n]!) : new Promise<RecordedRequest>((resolve) => waiting.push({ n, resolve })));
  return { calls, route, nth };
}

const stt = { answer: { status: 200, json: { text: "what's on my calendar", provider: "apple" } } as { status: number; json: unknown } };
/** /api/stt answers held back by a test, by call index (0 = the first call). */
let sttHeld: Map<number, Promise<void>>;
let sttCalls: ReturnType<typeof recorder>;
let messages: ReturnType<typeof recorder>;
let prepares: ReturnType<typeof recorder>;
let speaks: ReturnType<typeof recorder>;
/** ms of speech per utterance text; anything unlisted is 100 ms. */
let speechMs: Record<string, number> = {};
let speakStatus = 200;

beforeEach(async () => {
  rmSync(DATA_DIR, { recursive: true, force: true });
  speechMs = {};
  speakStatus = 200;
  stt.answer = { status: 200, json: { text: "what's on my calendar", provider: "apple" } };
  harness = await startFakeHarness();
  harness.bots = [BOT];
  sttHeld = new Map();
  sttCalls = recorder(async () => {
    await sttHeld.get(sttCalls.calls.length - 1);
    return stt.answer;
  });
  messages = recorder((req) => {
    const body = req.json as { text: string; threadId: string; sendId: string };
    return { status: 202, json: { ok: true, threadId: body.threadId, message: { id: `m_user${messages.calls.length}`, role: "user", kind: "text", text: body.text, sendId: body.sendId, at: Date.now() } } };
  });
  prepares = recorder((req) => {
    const text = (req.json as { text: string }).text;
    return { status: 200, json: { ready: true, utterances: text.split("\n\n").map((part) => part.replace(/\*\*/g, "")) } };
  });
  speaks = recorder((req) => {
    if (speakStatus !== 200) return { status: speakStatus, json: { error: speakStatus === 409 ? "Pick a voice in the agent profile." : "pcm_unsupported" } };
    const text = (req.json as { text: string }).text;
    return { status: 200, body: Buffer.alloc((16000 * 2 * (speechMs[text] ?? 100)) / 1000, 5), contentType: PCM_TYPE };
  });
  harness.route("POST", /^\/api\/stt$/, sttCalls.route);
  harness.route("POST", /^\/api\/bots\/b_jev\/messages$/, messages.route);
  harness.route("POST", /^\/api\/tts\/prepare$/, prepares.route);
  harness.route("POST", /^\/api\/tts\/speak$/, speaks.route);

  devices = new DeviceRegistry();
  hub = createGadgetHub({
    devices,
    harnessPort: harness.port,
    hostId: "000102030405060708090a0b0c0d0e0f",
    hostName: () => "Test Mac",
    connected: () => () => {},
    voice: createGadgetVoice,
  });
  server = createServer((_req, res) => {
    res.writeHead(404);
    res.end();
  });
  server.on("upgrade", (req, socket, head) => (hub.isGadgetPath(req.url) ? hub.handleUpgrade(req, socket, head) : socket.destroy()));
  await new Promise<void>((ready) => server.listen(0, "127.0.0.1", ready));
  gadget = await connect(CAPS);
  // the hub's single SSE subscription is up before the harness says anything
  if (!harness.requests.some((r) => r.method === "GET" && r.path.startsWith("/api/events"))) await harness.waitFor("GET", /^\/api\/events/);
  harness.emit({ kind: "bot", bot: BOT });
});

afterEach(async () => {
  // Guarded, then cleared: when a beforeEach fails part-way, its own error is
  // the one reported, not a TypeError from here.
  gadget?.close();
  await hub?.close();
  if (server?.listening) await new Promise<void>((done) => server.close(() => done()));
  await harness?.close();
  gadget = undefined!;
  hub = undefined!;
  server = undefined!;
  harness = undefined!;
});

/** Pairs and connects a test gadget to Jev with these caps. */
async function connect(caps: GadgetCaps): Promise<TestGadget> {
  const window = devices.openPairing("b_jev");
  const connected = await connectTestGadget({ port: (server.address() as AddressInfo).port, enroll: window.code, caps });
  expect(connected.ready?.bot).toEqual({ id: "b_jev", name: "Jev" });
  return connected;
}

/** Holds the n-th /api/stt answer (0-based) until the returned function is called. */
function holdStt(n: number): () => void {
  const gate = deferred<void>();
  sttHeld.set(n, gate.promise);
  return () => gate.resolve();
}

/** True when no `op` frame arrives within `ms`. */
const none = (op: "speak.begin" | "speak.end" | "speak.stop" | "card", ms = 200) => gadget.next(op, ms).then(() => false, () => true);

/** 0.5 s of a tone, as 25 mic frames of 20 ms. */
function micFrames(): Buffer[] {
  const frames: Buffer[] = [];
  for (let f = 0; f < 25; f++) {
    const frame = Buffer.alloc(640);
    for (let i = 0; i < 320; i++) frame.writeInt16LE(Math.round(8000 * Math.sin((2 * Math.PI * 440 * (f * 320 + i)) / 16000)), i * 2);
    frames.push(frame);
  }
  return frames;
}

function talk(turn: string, frames = micFrames()) {
  gadget.send({ op: "voice.begin", turn, stream: 7, rate: 16000 });
  for (const frame of frames) gadget.sendBinary(BinaryKind.mic, 7, frame);
  gadget.send({ op: "voice.end", turn, ms: frames.length * 20 });
  return frames;
}

/** The bot answers the n-th message the hub sent, as the harness broadcasts it. */
function botAnswers(n: number, text: string, options: { complete?: boolean } = {}) {
  const userId = `m_user${n}`;
  const turnId = `turn_${n}`;
  const sent = messages.calls[n - 1]!.json as { text: string; sendId: string };
  const bot = { id: `m_bot${n}`, role: "bot", kind: "text", text, turnId, requestMessageId: userId, at: Date.now() };
  harness.emit({ kind: "message", threadId: "th_1", message: { id: userId, role: "user", kind: "text", text: sent.text, sendId: sent.sendId, at: Date.now() } });
  harness.emit({ kind: "runtime", event: { type: "turn.started", threadId: "th_1", turnId } });
  harness.emit({ kind: "message", threadId: "th_1", message: bot });
  harness.emit({ kind: "message.patch", threadId: "th_1", message: { ...bot, turnTerminal: true, turnSucceeded: true } });
  if (options.complete !== false) harness.emit({ kind: "runtime", event: { type: "turn.completed", threadId: "th_1", turnId, ok: true, stopReason: null } });
}

async function finalReply(turn: string) {
  for (;;) {
    const reply = await gadget.next("reply");
    if (reply.turn === turn && reply.final) return reply;
  }
}

/** Speaker frames until `bytes` have arrived on `stream`. */
async function speakerBytes(stream: number, bytes: number) {
  let total = 0;
  while (total < bytes) {
    const frame = await gadget.nextBinary(BinaryKind.speaker);
    expect(frame.stream).toBe(stream);
    expect(frame.payload.byteLength).toBe(1280);                // every speaker frame is 40 ms (contract §2.12)
    total += frame.payload.byteLength;
  }
  return total;
}

describe("gadget voice through the hub", () => {
  it("hears, sends, answers and speaks a whole turn", async () => {
    const frames = talk("t1-1");
    const sttReq = await sttCalls.nth(0);
    expect(sttReq.headers["content-type"]).toBe("audio/wav");
    expect(sttReq.headers["x-openmausbot-companion-device"]).toBe(gadget.id);
    expect(sttReq.body.toString("ascii", 0, 4)).toBe("RIFF");
    expect(sttReq.body.readUInt32LE(24)).toBe(16000);
    expect(sttReq.body.subarray(44)).toEqual(Buffer.concat(frames));

    expect(await gadget.next("heard")).toEqual({ op: "heard", turn: "t1-1", text: "what's on my calendar" });
    const sent = (await messages.nth(0)).json as { text: string; threadId: string; sendId: string };
    expect(sent.text).toBe("what's on my calendar");
    expect(sent.threadId).toBe("th_1");
    expect(sent.sendId).toMatch(/^gdt[A-Za-z0-9_-]{40}$/);

    botAnswers(1, "You have **two meetings**.\n\nLunch at noon.");
    await finalReply("t1-1");
    const prepare = await prepares.nth(0);
    expect(prepare.json).toEqual({ text: "You have **two meetings**.\n\nLunch at noon.", voiceId: "v-1" });   // raw, never pre-shaped
    expect(prepare.headers["x-openmausbot-companion-device"]).toBe(gadget.id);
    const begin = await gadget.next("speak.begin");
    expect(begin).toMatchObject({ rate: 16000, turn: "t1-1" });
    await speaks.nth(1);
    expect(speaks.calls.map((c) => c.json)).toEqual([
      { text: "You have two meetings.", voiceId: "v-1", format: "pcm_16000" },
      { text: "Lunch at noon.", voiceId: "v-1", format: "pcm_16000" },
    ]);
    expect(await speakerBytes(begin.stream, 6400)).toBe(6400);
    expect(await gadget.next("speak.end")).toEqual({ op: "speak.end", stream: begin.stream });
    expect(await gadget.next("done")).toMatchObject({ turn: "t1-1", outcome: "ok" });
  });

  it("ends the turn with the set-up copy when no speech-to-text is set up, and sends the bot nothing", async () => {
    stt.answer = { status: 409, json: { error: "stt_unavailable", message: "Speech-to-text isn't set up." } };
    talk("t1-1");
    expect(await gadget.next("done")).toEqual({ op: "done", turn: "t1-1", outcome: "failed", reason: STT_UNAVAILABLE_COPY });
    expect(messages.calls).toHaveLength(0);
  });

  it("ends the turn with Didn't catch that on an empty transcript", async () => {
    stt.answer = { status: 200, json: { text: "", provider: "apple" } };
    talk("t1-1");
    expect(await gadget.next("done")).toEqual({ op: "done", turn: "t1-1", outcome: "failed", reason: "Didn't catch that" });
    expect(messages.calls).toHaveLength(0);
  });

  it("barge-in by say: a new turn stops the playing speech, and only the new stream is ever ended", async () => {
    speechMs = { "A long answer.": 3_000 };
    talk("t1-1");
    await messages.nth(0);
    botAnswers(1, "A long answer.", { complete: false });     // still in flight while it speaks
    const first = await gadget.next("speak.begin");
    await gadget.nextBinary(BinaryKind.speaker);
    gadget.send({ op: "say", turn: "t1-2", text: "stop talking" });
    expect(await gadget.next("speak.stop")).toEqual({ op: "speak.stop", stream: first.stream });
    expect(await gadget.next("done")).toMatchObject({ turn: "t1-1", outcome: "stopped" });
    expect(((await messages.nth(1)).json as { text: string }).text).toBe("stop talking");
    botAnswers(2, "Okay.");
    const second = await gadget.next("speak.begin");
    expect(second.turn).toBe("t1-2");
    // the old stream never got a speak.end: the first one is the new stream's
    expect(await gadget.next("speak.end")).toEqual({ op: "speak.end", stream: second.stream });
  });

  it("barge-in by TALK: stop, then voice.begin, while the old turn is in flight", async () => {
    speechMs = { "A long answer.": 3_000 };
    const releaseStt = holdStt(1);                             // the new turn's speech-to-text
    talk("t1-1");
    expect(await gadget.next("heard")).toMatchObject({ turn: "t1-1" });
    await messages.nth(0);
    botAnswers(1, "A long answer.", { complete: false });      // still in flight while it speaks
    const first = await gadget.next("speak.begin");
    await gadget.nextBinary(BinaryKind.speaker);
    gadget.send({ op: "stop", turn: "t1-1" });                 // TALK: the gadget stops the old turn first
    talk("t1-2");
    // both arrive while the new turn's STT is still held: before its first message
    expect(await gadget.next("speak.stop")).toEqual({ op: "speak.stop", stream: first.stream });
    expect(await gadget.next("done")).toMatchObject({ turn: "t1-1", outcome: "stopped" });
    expect(messages.calls).toHaveLength(1);
    releaseStt();
    expect(await gadget.next("heard")).toMatchObject({ op: "heard", turn: "t1-2" });
    await messages.nth(1);
  });

  it("barge-in after done: a new voice.begin stops speech that outlived its turn", async () => {
    speechMs = { "A long answer.": 3_000 };
    const releaseStt = holdStt(1);
    talk("t1-1");
    expect(await gadget.next("heard")).toMatchObject({ turn: "t1-1" });
    await messages.nth(0);
    botAnswers(1, "A long answer.");                           // turn.completed: done comes while speech plays
    const first = await gadget.next("speak.begin");
    expect(await gadget.next("done")).toMatchObject({ turn: "t1-1", outcome: "ok" });
    await gadget.nextBinary(BinaryKind.speaker);
    talk("t1-2");
    expect(await gadget.next("speak.stop")).toEqual({ op: "speak.stop", stream: first.stream });
    expect(messages.calls).toHaveLength(1);                    // STT still held: speak.stop came first
    releaseStt();
    expect(await gadget.next("heard")).toMatchObject({ turn: "t1-2" });
    await messages.nth(1);
    botAnswers(2, "Okay.");
    const second = await gadget.next("speak.begin");
    expect(second.turn).toBe("t1-2");
    // the old stream never got a speak.end: the first one is the new stream's
    expect(await gadget.next("speak.end")).toEqual({ op: "speak.end", stream: second.stream });
  });

  it("shows Voice is off once when the bot has no voice, and later turns still answer in text", async () => {
    speakStatus = 409;
    talk("t1-1");
    await messages.nth(0);
    botAnswers(1, "Text only.");
    expect(await gadget.next("card")).toEqual(VOICE_OFF_CARD);
    expect(await gadget.next("done")).toMatchObject({ turn: "t1-1", outcome: "ok" });
    talk("t1-2");
    await messages.nth(1);
    botAnswers(2, "Again.");
    expect(await gadget.next("done")).toMatchObject({ turn: "t1-2", outcome: "ok" });
    expect(await none("card")).toBe(true);
    // no further speech requests in this session
    expect(speaks.calls).toHaveLength(1);
    expect(prepares.calls).toHaveLength(1);
  });

  for (const [label, caps] of [
    ["no speaker", { screen: CAPS.screen, mic: CAPS.mic, input: CAPS.input }],
    ["a 22.05 kHz speaker", { ...CAPS, speaker: { rate: 22050 } }],
  ] as Array<[string, GadgetCaps]>) {
    it(`keeps every reply text-only for a gadget with ${label}`, async () => {
      gadget.close();
      gadget = await connect(caps);
      talk("t1-1");
      await messages.nth(0);
      botAnswers(1, "You have two meetings.");
      await finalReply("t1-1");
      expect(await gadget.next("done")).toMatchObject({ turn: "t1-1", outcome: "ok" });
      for (const op of ["speak.begin", "speak.end", "speak.stop"] as const) expect(await none(op)).toBe(true);
      expect(prepares.calls).toHaveLength(0);
      expect(speaks.calls).toHaveLength(0);
    });
  }

  it("speaks a routine result when the gadget reads pushes aloud, on a stream without a turn", async () => {
    devices.updateGadget(gadget.id, { speakPushes: true });
    hub.settingsChanged(gadget.id, { nameChanged: false });     // what PATCH /devices/:id/gadget does next
    let settings = await gadget.next("settings");
    while (!settings.settings.speak_pushes) settings = await gadget.next("settings");
    harness.emit({ kind: "routine.run", run: { id: "run_1", botId: "b_jev", routineName: "Morning brief", status: "completed", output: "Your morning brief is ready." } });
    expect(await gadget.next("post")).toMatchObject({ kind: "routine", speak: true });
    const begin = await gadget.next("speak.begin");
    expect(begin).toMatchObject({ op: "speak.begin", rate: 16000 });
    expect(begin).not.toHaveProperty("turn");
    expect(((await prepares.nth(0)).json as { text: string }).text).toBe("Your morning brief is ready.");
    expect(await gadget.next("speak.end")).toEqual({ op: "speak.end", stream: begin.stream });
  });
});

describe("companion wiring", () => {
  it("passes the real voice to the hub", () => {
    const source = readFileSync(new URL("../../src/index.ts", import.meta.url), "utf8");
    expect(source).toMatch(/voice: createGadgetVoice\b/);
    expect(source).not.toMatch(/voice: undefined/);
    expect(source).toMatch(/import \{ createGadgetVoice \} from "\.\/gadget\/speech\.ts";/);
  });
});
```

- [ ] **Step 2: Run it to see the wiring check fail**

```bash
cd "$(git -C /Users/omkar/Desktop/openmaus/OpenGrokBot worktree list --porcelain | awk '/^worktree /{w=substr($0,10)} $0=="branch refs/heads/feat/gadget-voice"{print w}')" && test "$(git branch --show-current)" = feat/gadget-voice || { echo 'not in the feat/gadget-voice worktree'; exit 1; }
export PATH="$HOME/.nvm/versions/node/v24.14.1/bin:$PATH"
pnpm exec vitest run companion/test/gadget/voice-hub.test.ts
```

Expected: `companion wiring > passes the real voice to the hub` FAILS with `expected '…' to match /voice: createGadgetVoice\b/`. The ten hub cases build their own hub with the real voice, so they pass already: `Tests  1 failed | 10 passed (11)`.

- [ ] **Step 3: Pass the real voice to the hub**

In `companion/src/index.ts`, add this import directly below P3a's `import { createGadgetHub } from "./gadget/hub.ts";`:

```ts
import { createGadgetVoice } from "./gadget/speech.ts";
```

In the `createGadgetHub({…})` call, replace the property

```ts
voice: undefined
```

with

```ts
voice: createGadgetVoice
```

Keep the surrounding punctuation as it is. This is the only change to P3a's file.

- [ ] **Step 4: Run the companion gadget suite, build the companion and typecheck**

```bash
cd "$(git -C /Users/omkar/Desktop/openmaus/OpenGrokBot worktree list --porcelain | awk '/^worktree /{w=substr($0,10)} $0=="branch refs/heads/feat/gadget-voice"{print w}')" && test "$(git branch --show-current)" = feat/gadget-voice || { echo 'not in the feat/gadget-voice worktree'; exit 1; }
export PATH="$HOME/.nvm/versions/node/v24.14.1/bin:$PATH"
pnpm exec vitest run companion/test/gadget/voice-hub.test.ts
pnpm exec vitest run companion/test/gadget
pnpm build:companion
grep -hn "^import" dist-companion/gadget/audio.js dist-companion/gadget/stt-client.js dist-companion/gadget/speech.js
pnpm exec tsc -p tsconfig.server.json
```

Expected:
- The first run prints `Tests  11 passed (11)`.
- The second prints `Test Files  N passed (N)` with 0 failed, where N counts P3a's files in `companion/test/gadget` plus P3b's four (`audio`, `stt-client`, `speech`, `voice-hub`).
- `pnpm build:companion` exits 0.
- The grep shows only `node:` and `./…js` imports.
- `tsc` exits 0.

- [ ] **Step 5: Commit**

```bash
cd "$(git -C /Users/omkar/Desktop/openmaus/OpenGrokBot worktree list --porcelain | awk '/^worktree /{w=substr($0,10)} $0=="branch refs/heads/feat/gadget-voice"{print w}')" && test "$(git branch --show-current)" = feat/gadget-voice || { echo 'not in the feat/gadget-voice worktree'; exit 1; }
git add companion/src/index.ts companion/test/gadget/voice-hub.test.ts
git commit -m "feat(companion): gadget hub uses real speech-to-text and speech" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 13: Branch verification, and what needs hardware

**Files:** none changed, unless a check fails. A fix then goes in the task that owns the file, as a new commit.

- [ ] **Step 1: Every P3b suite together**

```bash
cd "$(git -C /Users/omkar/Desktop/openmaus/OpenGrokBot worktree list --porcelain | awk '/^worktree /{w=substr($0,10)} $0=="branch refs/heads/feat/gadget-voice"{print w}')" && test "$(git branch --show-current)" = feat/gadget-voice || { echo 'not in the feat/gadget-voice worktree'; exit 1; }
export PATH="$HOME/.nvm/versions/node/v24.14.1/bin:$PATH"
pnpm exec vitest run server/tts server/stt server/routes/stt.test.ts server/request-auth.test.ts companion/test/routes.test.ts companion/test/gadget scripts/testing/index-route-ratchet.test.ts
```

Expected: vitest prints `Test Files  N passed (N)` with 0 failed. The P3b files alone contribute 99 tests:

| File | Tests |
|---|---|
| `pcm.test.ts` | 12 |
| `pcm-format.test.ts` | 7 |
| `speak-reply.test.ts` | 7 |
| `scribe.test.ts` | 7 |
| `stt.test.ts` | 15 |
| `apple.test.ts` | 9 |
| `audio.test.ts` | 9 |
| `stt-client.test.ts` | 5 |
| `speech.test.ts` | 16 |
| `voice-hub.test.ts` | 11 |
| new row in `routes.test.ts` | 1 |

- [ ] **Step 2: The whole suite, Electron, and the static checks**

```bash
cd "$(git -C /Users/omkar/Desktop/openmaus/OpenGrokBot worktree list --porcelain | awk '/^worktree /{w=substr($0,10)} $0=="branch refs/heads/feat/gadget-voice"{print w}')" && test "$(git branch --show-current)" = feat/gadget-voice || { echo 'not in the feat/gadget-voice worktree'; exit 1; }
export PATH="$HOME/.nvm/versions/node/v24.14.1/bin:$PATH"
pnpm exec vitest run
pnpm test:electron
pnpm typecheck
pnpm lint
pnpm i18n:check
```

Expected:
- vitest (several minutes) and `pnpm test:electron` show no failures beyond those recorded in `/private/tmp/omb-p3b-baseline.txt` (Task 1 Step 2). `test:electron` includes `speech-helper-file-mode.node-test.mjs`: 4 pass on macOS with Swift, and the type-check case is skipped elsewhere.
- A failure that is not in the baseline is P3b's to fix, unless it is in one of P3a's own files (the baseline predates the Task 9 rebase when Task 1 started from `origin/main`). Report a P3a failure; do not fix it.
- `pnpm typecheck`, `pnpm lint` (oxlint `--deny-warnings`) and `pnpm i18n:check` each exit 0.

- [ ] **Step 3: The packaged harness still starts with no node_modules**

```bash
cd "$(git -C /Users/omkar/Desktop/openmaus/OpenGrokBot worktree list --porcelain | awk '/^worktree /{w=substr($0,10)} $0=="branch refs/heads/feat/gadget-voice"{print w}')" && test "$(git branch --show-current)" = feat/gadget-voice || { echo 'not in the feat/gadget-voice worktree'; exit 1; }
export PATH="$HOME/.nvm/versions/node/v24.14.1/bin:$PATH"
pnpm test:packaged-server
```

Expected: the build and the smoke both pass. P3b adds no dependency. `server/stt/apple.ts` computes its dev path with `new URL(…, import.meta.url)`, like `server/message-search-worker.ts`; in the bundle that path simply does not exist.

- [ ] **Step 4: A real harness answers `/api/stt` and PCM speech (macOS)**

This is an isolated fixture, as the repo's `AGENTS.md` and `docs/verification/README.md` require: a throwaway data folder, a throwaway `HOME`, and port 18899, never the real `~/.openmausbot`. It also unsets every ElevenLabs key and relay variable the shell may export, so `/api/stt` can never make a paid Scribe call here:

```bash
cd "$(git -C /Users/omkar/Desktop/openmaus/OpenGrokBot worktree list --porcelain | awk '/^worktree /{w=substr($0,10)} $0=="branch refs/heads/feat/gadget-voice"{print w}')" && test "$(git branch --show-current)" = feat/gadget-voice || { echo 'not in the feat/gadget-voice worktree'; exit 1; }
export PATH="$HOME/.nvm/versions/node/v24.14.1/bin:$PATH"
SMOKE=$(mktemp -d /private/tmp/omb-p3b-smoke.XXXX)
mkdir -p "$SMOKE/home"
printf '{"tts":{"provider":"system","voice":"Samantha"}}' > "$SMOKE/config.json"
say -o "$SMOKE/hello.wav" --data-format=LEI16@16000 "What is on my calendar today"
(env -u OMB_TTS_KEY -u OMB_ELEVENLABS_API -u OMB_CLOUD_VOICE_TOKEN HOME="$SMOKE/home" OMB_DATA_DIR="$SMOKE" OMB_PORT=18899 node --experimental-strip-types server/index.ts > "$SMOKE/harness.log" 2>&1 & echo $! > "$SMOKE/pid")
curl -sf --retry 60 --retry-connrefused --retry-delay 1 http://127.0.0.1:18899/api/health > /dev/null && echo "harness up"
curl -s -w ' %{http_code}\n' -X POST -H 'content-type: audio/wav' --data-binary @"$SMOKE/hello.wav" http://127.0.0.1:18899/api/stt
curl -s -D "$SMOKE/h.txt" -o "$SMOKE/out.pcm" -X POST -H 'content-type: application/json' -d '{"text":"Hello from the gadget.","format":"pcm_16000"}' http://127.0.0.1:18899/api/tts/speak
grep -i '^content-type' "$SMOKE/h.txt"; wc -c < "$SMOKE/out.pcm"
curl -s -w ' %{http_code}\n' -X POST -H 'content-type: application/json' -d '{"text":"Hi","format":"pcm_44100"}' http://127.0.0.1:18899/api/tts/speak
curl -s -o /dev/null -w '%{http_code} %{content_type}\n' -X POST -H 'content-type: application/json' -d '{"text":"Hi"}' http://127.0.0.1:18899/api/tts/speak
kill "$(cat "$SMOKE/pid")"
```

Expected:
1. `harness up`.
2. `/api/stt` answers either `{"text":"What is on my calendar today?","provider":"apple"} 200`, or `{"error":"stt_unavailable","message":"Speech-to-text isn't set up. On a Mac, allow Speech Recognition for OpenMausBot (try dictation once), or add an ElevenLabs key in a bot's voice settings."} 409`. The throwaway folder has no ElevenLabs key and the key variables are unset, so the answer depends only on whether the dev helper is allowed Speech Recognition. The provider must never be `elevenlabs` here. It must match what Task 4 Step 6 printed, and no dialog appears.
3. `content-type: audio/pcm;rate=16000;channels=1;bits=16;endian=little`, and more than 20000 bytes of PCM.
4. `{"error":"format must be mp3, pcm_16000 or pcm_24000"} 400`.
5. `200 audio/wav`: the default (mp3 format) is unchanged; the system voice answers WAV, as before.

If "Samantha" is not installed, use the first name from `say -v '?'`.

- [ ] **Step 5: The branch is clean and complete**

```bash
cd "$(git -C /Users/omkar/Desktop/openmaus/OpenGrokBot worktree list --porcelain | awk '/^worktree /{w=substr($0,10)} $0=="branch refs/heads/feat/gadget-voice"{print w}')" && test "$(git branch --show-current)" = feat/gadget-voice || { echo 'not in the feat/gadget-voice worktree'; exit 1; }
git status --porcelain
git log --oneline feat/gadget-hub..HEAD
```

Expected:
- `git status` prints nothing.
- The log lists the 12 P3b commits (one per Task 1–12), plus the D-P3b-1 commit when Task 5 Step 7 applied answer A or B, plus any Task 13 fix commits. Starting at `feat/gadget-hub` also shows the branch is on P3a's (D-P3b-2 closed).
- D-P3b-1: if Omkar has answered, Task 5 Step 7 is applied (C needs no commit). If he has not, list D-P3b-1 as open in the hand-off.

Do not push and do not open a PR: Omkar publishes. When Omkar has the branch, and `feat/gadget-voice` is checked out at `/Users/omkar/Desktop/openmaus/OpenGrokBot-gadget-voice`, remove that worktree; the branch stays. Never remove `/Users/omkar/Desktop/openmaus/OpenGrokBot-gadget`, which is P3a's:

```bash
git -C /Users/omkar/Desktop/openmaus/OpenGrokBot worktree remove /Users/omkar/Desktop/openmaus/OpenGrokBot-gadget-voice
```

- [ ] **Step 6: Hand-off summary (paste into the final report)**

Verified on the branch:
- every P3b test, the full vitest suite and `test:electron` against the baseline, `typecheck`, `lint`, `i18n:check`, `build:companion` and `test:packaged-server`;
- the helper's file mode through LaunchServices (Task 4 Step 6);
- a live dev harness answering `/api/stt` and PCM speech in an isolated fixture (Step 4).

Decisions to report:
- D-P3b-1: Omkar's answer (A, B or C) and whether Task 5 Step 7 applied it, or "open". With B, contract §3.14's Scribe line needs the same change.
- D-P3b-2: whether Task 1 started from `origin/main`, and that Task 9 Step 1 rebased the branch onto `feat/gadget-hub`.

Not verifiable without hardware or a packaged build. These belong on the packaged-build and hardware checklists:
1. **Speech permission in the packaged app.** Does the Speech grant for dictation apply when the packaged harness (a utilityProcess) launches the helper through `open`? Check on an OMB2 side-by-side build (the `test-locally` skill): grant dictation once, then run a gadget turn. Expect `provider: "apple"`. The fallback is built in: `speech-not-authorized`, then Scribe or the set-up copy.
2. **The recognizer on a silent WAV, on a Mac that has granted Speech Recognition.** The route's silence gate already answers `{text: ""}` for near-silence. Check that a quiet-but-not-silent clip returns `no-speech` (mapped to `""`) and not `recognition-error`.
3. **Real ElevenLabs Scribe v2 with a real key.** With the request as D-P3b-1 left it: is `enable_logging=false` refused for a non-enterprise account, and with what status and body? With answer A, does the body name retention or logging, so the one retry runs, and does it succeed? Does a key without the Speech to Text permission get a 401 or 403 whose body does not name retention?
4. **Live TTS answers.** Fish (`format: "wav"`, 16/24 kHz) and xAI (`codec: "wav"`, 16/24 kHz) really return 16-bit PCM WAV. ElevenLabs `pcm_16000` and `pcm_24000` play at the right pitch.
5. **On a gadget** (the SDK's `docs/hardware-checklist.md`, owned by P2c):
   - 16 kHz speech on amoled-175c, amoled-175 and lcd-154, and 24 kHz on the devkit;
   - no underruns with the 0.5 s lead and the gadget's 1 s jitter buffer;
   - barge-in cuts the audio at once;
   - a spoken push that follows a reply does not clip the reply's last syllables (the 300 ms `SPEECH_GAP_MS`; raise it if the tail is cut);
   - a reply's padded last frame (up to 40 ms of silence) ends without a click;
   - the silence gate: on each board, normal speech at arm's length must peak well above 200 (about -44 dBFS) in the WAV that reaches `/api/stt`, and a whispered question must still come back as text, not "Didn't catch that". If a board's mic gain cannot clear it, lower `SILENCE_PEAK` in `server/routes/stt.ts` or drop the amplitude check and keep only the 100 ms floor;
   - the Voice is off card appears once;
   - a full 60 s utterance completes.
6. **End to end, simulator ↔ dev MausBot:** a voice turn with the simulator's `--mic-file` WAV, as in spec §10 "End to end".

---

## Contract deviations

Contract §0 item 2: each item keeps the pinned shape until it is reviewed. D-P3b-1 stops for Omkar's answer (Task 5 Step 1) and builds only the pinned request meanwhile. D-P3b-2 is undone before any work that depends on P3a (Task 9 Step 1), and Omkar reviews it in the hand-off.

- **D-P3b-1, Scribe `?enable_logging=false`** (contract §3.14, `transcribeWithScribe`). **STOP checkpoint at the start of Task 5.**
  - **Problem.** ElevenLabs documents zero-retention mode (`enable_logging=false`) as something that "may only be used by enterprise customers". What it does for other accounts is not documented. If it refuses the request, the only fallback off a Mac is broken.
  - **What this plan does until Omkar answers.** It builds exactly the pinned request and sends it once. Any refusal is a failure, and the audio is never sent twice. Task 5 Step 1 asks him; Task 5 Step 7 has the exact code for each answer.
  - **The question.** (A) Keep the parameter, and retry once without it only when the refusal names it: status 400, 401, 403 or 422 with a body matching `/retention|enable_logging|logging/i`, inside the same 30 s deadline. (B) Drop the parameter: the audio gets normal retention, the same as the TTS text the app already sends. (C) Keep exactly the pinned request.
  - **Why A never retries on a bare 400 or 422.** A refusal that has nothing to do with retention would then re-send the audio with ElevenLabs logging on, a silent privacy downgrade for accounts that can use zero retention. Task 5 pins this with "reports a plain refusal as a failure after exactly one request".
- **D-P3b-2, the branch base** (contract §1.3 says P3b's branch is created from `feat/gadget-hub`). P3b may start from `origin/main` when `feat/gadget-hub` does not exist yet, and it is then rebased onto `feat/gadget-hub` in Task 9 Step 1, before any companion work.
  - Tasks 1–8 touch only the harness, the helper and the allowlists, none of P3a's files.
  - It never takes P3a's path: P3b's own worktree is `/Users/omkar/Desktop/openmaus/OpenGrokBot-gadget-voice`.
  - **Review point.** Omkar sees it in the hand-off. Task 13 Step 5's `feat/gadget-hub..HEAD` log shows the finished branch sits on P3a's, as §1.3 wants.

## Contract notes

These are additive and need no review stop:

- **`createSpeechPacer` options** gain `onFlushed?: () => void`. Only `speech.ts` reads it; it is the moment to send `speak.end` before playout ends.
- **Speaker frames** are exactly 40 ms, as contract §2.12 pins them. A reply's last frame is padded with up to 40 ms of silence.
- **`resolveSpeechHelper(env, options?)`** gains an optional `{platform, devBundle, exists}`. **`transcribeWithApple(…, options?)`** gains `graceMs`, `exitGraceMs` and `open` next to `timeoutMs`. Both are test seams with production defaults.
- **`toPcm16le`** refuses `audio/pcm` that carries no `rate=` (`PcmUnsupported`, so 415 and text only). Contract §3.14's comment says "audio/pcm passes through". Only ElevenLabs answers `audio/pcm`, and Task 2 always labels its rate. An unlabelled answer (a Chatterbox server passing its own type through) could be at any rate, and it would play at the wrong pitch.
- **New exports:**
  - `server/tts/pcm.ts`: `readWav` and `WavInfo` (used by `stt.ts`);
  - `server/tts/index.ts`: `pcmRate`;
  - `server/routes/stt.ts`: the 502 `stt_failed` message `Speech-to-text failed. Try again.`
- **New private files:** `server/tts/speak-reply.ts` (so `/api/tts/speak` is unit-testable without booting the harness), `companion/test/gadget/helpers/voice-fakes.ts`, and the test files listed in File Structure.
- **The silence and too-short gate in `/api/stt`.** It is not in the contract. It makes "a silent WAV gives `{text: ""}`" (spec §10) deterministic for both providers, and it avoids paying Scribe for a button tap.
  - The 100 ms floor is Scribe's documented minimum.
  - The peak threshold (200, about -44 dBFS) has a boundary test in Task 6.
  - Task 13 has the per-board check that real speech clears it and a whisper still reaches speech-to-text. If a board cannot clear it, the hand-off says to lower it or keep only the floor.
- **`ready: false` from `/api/tts/prepare`** is treated like the 409 "no voice": it shows one notice and turns speech off for the session. Both mean `voiceReady(cfg, voiceId)` is false.
- **`SPEECH_GAP_MS` (300 ms)** is private to `speech.ts`. Queued speech waits that long after the earlier stream's host-side playout end, so a spoken push never cuts a reply's tail (spec §6.2 Speech item 5).

## Self-review

- **Spec coverage.**
  - Every §6.3 bullet maps to a task in the Scope table: the route module, the allowlist, the input limits, the output, file mode (flags, URL request, on-device recognition, punctuation, one line, no prompt), helper resolution, the launch, empty and no-speech, Scribe (request, key, when it is used, 401/403), 409 with its copy, the TTS `format`, the provider table, `pcm.ts`, 415, and the content type.
  - Every §6.2 Speech item 1–8 maps to Task 11. Through the real hub, Task 12 covers the chain, items 4 (text only for no speaker or another rate), 5 (a spoken push without `turn`), 6 (barge-in by `say`, by `stop` + TALK, and by TALK after `done`) and 7 (Voice is off once per session).
  - A11, A12, A29 and A30 are covered. A13 (the speaker rate) is honored by `rate: null` → text only; P3a computes the rate.
  - Out-of-scope items are named with their owners.
- **Placeholders.** None: every code step carries the complete file or the exact old and new text, and every command shows its expected output.
- **Type consistency.**
  - These names match contract §3.14 and are used the same way in every task: `SpeechFormat`, `SPEECH_FORMATS`, `pcmRate`, `toPcm16le`, `PcmUnsupported`, `pcmMime`, `ttsSpeakReply`, `transcribeWithScribe`, `createSttRoutes`, `AppleSttResult`, `transcribeWithApple`, `resolveSpeechHelper`, `wavFromPcm16`, `createSpeechPacer`, `createSttClient`, `STT_UNAVAILABLE_COPY`, `createSpeechOut`, `createGadgetVoice` and `VOICE_OFF_CARD`.
  - The test helpers' names are the same in Tasks 10, 11 and 12.
- **Review Focus.** Each of the five lines has its test in the owning task; the Review Focus section names them.
- **Contract deviations.** D-P3b-1 is a STOP checkpoint with all three answers written out. D-P3b-2 is closed by Task 9 Step 1's rebase. Nothing pinned is renamed or retyped.
