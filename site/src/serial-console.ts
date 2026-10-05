// SPDX-License-Identifier: Apache-2.0
// Raw Web Serial console session. Writes go straight to port.writable, never
// through esptool-js's Transport.write, which SLIP-encodes (contract §4.8).
import { createLineSplitter, parseOmbLine, type OmbMessage } from "./console.ts";
import { InstallerError } from "./errors.ts";
import type { SerialPortLike } from "./reset.ts";

export interface ConsoleEvent {
  line: string;
  msg: OmbMessage | null;
}

/** What the setup steps need from a console; tests pass a scripted fake. */
export interface ConsoleIO {
  send(line: string): Promise<void>;
  /** Every line received since the last drain, oldest first. */
  drain(): ConsoleEvent[];
  readonly lost: boolean;
}

/** A console the page can close (and later reopen on the same or a re-enumerated port). */
export interface ConsoleSession extends ConsoleIO {
  close(): Promise<void>;
}

export class SerialConsole implements ConsoleSession {
  readonly port: SerialPortLike;
  readonly finished: Promise<void>;
  lost = false;
  lastOutputAt: number | null = null;
  onEvent: ((event: ConsoleEvent) => void) | null = null;
  private readonly now: () => number;
  private readonly writeTimeoutMs: number;
  private reader: ReadableStreamDefaultReader<Uint8Array> | null = null;
  private queue: ConsoleEvent[] = [];
  private closed = false;

  constructor(port: SerialPortLike, options: { now?: () => number; writeTimeoutMs?: number } = {}) {
    this.port = port;
    this.now = options.now ?? Date.now;
    this.writeTimeoutMs = options.writeTimeoutMs ?? 3000;
    this.finished = this.readLoop();
  }

  private async readLoop(): Promise<void> {
    const readable = this.port.readable;
    if (readable === null) {
      this.lost = true;
      return;
    }
    const reader = readable.getReader();
    this.reader = reader;
    const decoder = new TextDecoder();
    const split = createLineSplitter();
    try {
      for (;;) {
        const { value, done } = await reader.read();
        if (done) break;
        if (value !== undefined && value.length > 0) {
          this.lastOutputAt = this.now();
          for (const line of split(decoder.decode(value, { stream: true }))) this.push(line);
        }
      }
    } catch {
      // The device dropped off the bus (reset, unplug); `lost` tells the caller.
    } finally {
      reader.releaseLock();
      if (!this.closed) this.lost = true;
    }
  }

  private push(line: string): void {
    const event = { line, msg: parseOmbLine(line) };
    this.queue.push(event);
    this.onEvent?.(event);
  }

  drain(): ConsoleEvent[] {
    const events = this.queue;
    this.queue = [];
    return events;
  }

  async send(line: string): Promise<void> {
    if (this.lost || this.closed || this.port.writable === null) throw new InstallerError("port_lost", "The board's port closed.");
    const writer = this.port.writable.getWriter();
    let timer: ReturnType<typeof setTimeout> | undefined;
    try {
      await Promise.race([
        writer.write(new TextEncoder().encode(`${line}\n`)),
        new Promise<never>((_, reject) => {
          timer = setTimeout(
            () => reject(new InstallerError("console_write_timeout", "The gadget isn't reading its console.")),
            this.writeTimeoutMs,
          );
        }),
      ]);
    } finally {
      clearTimeout(timer);
      writer.releaseLock();
    }
  }

  async close(): Promise<void> {
    this.closed = true;
    try {
      await this.reader?.cancel();
    } catch {
      // already gone
    }
    await this.finished;
    try {
      await this.port.close();
    } catch {
      // already closed
    }
  }
}
