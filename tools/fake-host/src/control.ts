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
