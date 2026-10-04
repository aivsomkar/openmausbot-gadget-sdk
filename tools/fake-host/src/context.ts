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
