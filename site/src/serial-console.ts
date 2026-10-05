// SPDX-License-Identifier: Apache-2.0
// Raw Web Serial console session. Writes go straight to port.writable, never
// through esptool-js's Transport.write, which SLIP-encodes (contract §4.8).
import { createLineSplitter, parseOmbLine, type OmbMessage } from "./console.ts";
import { InstallerError } from "./errors.ts";
import type { SerialPortLike } from "./reset.ts";

/**
 * Web Serial read errors that end only the current stream: the port stays
 * open and `port.readable` hands out a fresh stream. Every other error (the
 * device was lost) ends the session.
 */
const NON_FATAL_READ_ERRORS: readonly string[] = ["BufferOverrunError", "BreakError", "FramingError", "ParityError"];

function isNonFatal(e: unknown): boolean {
  return typeof e === "object" && e !== null && "name" in e && typeof e.name === "string" && NON_FATAL_READ_ERRORS.includes(e.name);
}

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
  /** The last queued write; each send() waits for the one before it, because a stream takes one writer at a time. */
  private tail: Promise<void> = Promise.resolve();

  constructor(port: SerialPortLike, options: { now?: () => number; writeTimeoutMs?: number } = {}) {
    this.port = port;
    this.now = options.now ?? Date.now;
    this.writeTimeoutMs = options.writeTimeoutMs ?? 3000;
    this.finished = this.readLoop();
  }

  private async readLoop(): Promise<void> {
    const decoder = new TextDecoder();
    const split = createLineSplitter();
    try {
      // One pass per stream: after a non-fatal error the port hands out a fresh `readable`.
      for (let previous: ReadableStream<Uint8Array> | null = null; !this.closed; ) {
        const readable = this.port.readable;
        // No stream (or the same errored one again) means the port is gone.
        if (readable === null || readable === previous) return;
        previous = readable;
        const reader = readable.getReader();
        this.reader = reader;
        try {
          for (;;) {
            const { value, done } = await reader.read();
            if (done) return;
            if (value !== undefined && value.length > 0) {
              this.lastOutputAt = this.now();
              for (const line of split(decoder.decode(value, { stream: true }))) this.push(line);
            }
          }
        } catch (e) {
          // A non-fatal error (buffer overrun, break, framing, parity) lost some bytes but not the board.
          // Anything else means the device dropped off the bus (reset, unplug); `lost` tells the caller.
          if (!isNonFatal(e)) return;
        } finally {
          reader.releaseLock();
        }
      }
    } finally {
      if (!this.closed) this.lost = true;
    }
  }

  private push(line: string): void {
    const event = { line, msg: parseOmbLine(line) };
    this.queue.push(event);
    try {
      this.onEvent?.(event);
    } catch {
      // A UI error must not end the console.
    }
  }

  drain(): ConsoleEvent[] {
    const events = this.queue;
    this.queue = [];
    return events;
  }

  /** Writes `line` and a newline. Overlapping calls are written one after another, in call order. */
  send(line: string): Promise<void> {
    const run = this.tail.then(() => this.write(line));
    this.tail = run.catch(() => undefined);
    return run;
  }

  private async write(line: string): Promise<void> {
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
