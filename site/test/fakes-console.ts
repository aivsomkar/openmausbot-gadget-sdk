// SPDX-License-Identifier: Apache-2.0
// A scripted gadget console that answers commands like the firmware (contract §2.11).
import { parseOmbLine } from "../src/console.ts";
import type { ConsoleEvent, ConsoleSession } from "../src/serial-console.ts";

export interface GadgetScript {
  status?: Record<string, unknown>;
  onCommand?: (line: string, gadget: FakeGadget) => void;
}

export class FakeGadget implements ConsoleSession {
  lost = false;
  closed = false;
  sent: string[] = [];
  status: Record<string, unknown> = { op: "status", wifi: "off", id: "gad_3f9a0c2b7e41d856", pair: "unpaired", fw: "1.0.0" };
  onCommand: (line: string, gadget: FakeGadget) => void;
  private queue: ConsoleEvent[] = [];
  constructor(script: GadgetScript = {}) {
    if (script.status) this.status = { ...this.status, ...script.status };
    this.onCommand = script.onCommand ?? (() => undefined);
  }
  async send(line: string): Promise<void> {
    if (this.lost) throw new Error("lost");
    this.sent.push(line);
    this.onCommand(line, this);
    if (line === "status") this.emit(`@omb ${JSON.stringify(this.status)}`);
  }
  emit(line: string): void {
    this.queue.push({ line, msg: parseOmbLine(line) });
  }
  /** Merge fields into the status; a field set to undefined disappears from the @omb line. */
  set(fields: Record<string, unknown>): void {
    this.status = { ...this.status, ...fields };
  }
  drain(): ConsoleEvent[] {
    const q = this.queue;
    this.queue = [];
    return q;
  }
  async close(): Promise<void> {
    this.closed = true;
  }
}
