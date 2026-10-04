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
