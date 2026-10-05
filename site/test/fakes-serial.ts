// SPDX-License-Identifier: Apache-2.0
// Test doubles for Web Serial: a port you can push output into, a
// navigator.serial stand-in and a virtual clock.
import type { SerialLike, SerialPortLike } from "../src/reset.ts";

/** Read errors after which Web Serial hands out a fresh `port.readable` (the port stays open). */
const NON_FATAL_READ_ERRORS: readonly string[] = ["BufferOverrunError", "BreakError", "FramingError", "ParityError"];

function named(name: string, message: string): Error {
  const e = new Error(message);
  e.name = name;
  return e;
}

/** A Web Serial port: push device output with emit(), read what the page wrote with sent(). */
export class FakePort implements SerialPortLike {
  readable: ReadableStream<Uint8Array> | null = null;
  writable: WritableStream<Uint8Array> | null = null;
  info: { usbVendorId?: number; usbProductId?: number };
  openFailures: Error[] = [];
  /** setSignals() rejects with these first (the device vanished between open() and setSignals()). */
  signalFailures: Error[] = [];
  signals: Array<{ dataTerminalReady?: boolean; requestToSend?: boolean }> = [];
  opened = 0;
  isOpen = false;
  private controller: ReadableStreamDefaultController<Uint8Array> | null = null;
  private written = "";
  constructor(info = { usbVendorId: 0x303a, usbProductId: 0x1001 }) {
    this.info = info;
  }
  async open(_options: { baudRate: number }): Promise<void> {
    // Like Chrome: opening a port that is already open fails.
    if (this.isOpen) throw named("InvalidStateError", "The port is already open.");
    const failure = this.openFailures.shift();
    if (failure) throw failure;
    this.opened++;
    this.isOpen = true;
    this.readable = this.newReadable();
    this.writable = new WritableStream<Uint8Array>({ write: (chunk) => void (this.written += new TextDecoder().decode(chunk)) });
  }
  async close(): Promise<void> {
    this.isOpen = false;
    this.readable = null;
    this.writable = null;
  }
  async setSignals(signals: { dataTerminalReady?: boolean; requestToSend?: boolean }): Promise<void> {
    const failure = this.signalFailures.shift();
    if (failure) throw failure;
    this.signals.push(signals);
  }
  getInfo(): { usbVendorId?: number; usbProductId?: number } {
    return this.info;
  }
  /** One USB packet of raw bytes, which may end in the middle of a UTF-8 character. */
  emitBytes(bytes: Uint8Array): void {
    this.controller?.enqueue(bytes);
  }
  emit(text: string): void {
    this.emitBytes(new TextEncoder().encode(text));
  }
  unplug(): void {
    this.controller?.error(new Error("The device has been lost."));
  }
  /**
   * A read error named `name`. A non-fatal one ends only the current stream: the
   * port then hands out a fresh `readable` (pass `fresh: false` to keep the errored one).
   */
  readError(name: string, fresh = NON_FATAL_READ_ERRORS.includes(name)): void {
    this.controller?.error(named(name, `${name} while reading`));
    if (fresh) this.readable = this.newReadable();
  }
  sent(): string[] {
    return this.written.split("\n").filter((l) => l !== "");
  }
  private newReadable(): ReadableStream<Uint8Array> {
    return new ReadableStream<Uint8Array>({ start: (c) => void (this.controller = c) });
  }
}

/**
 * navigator.serial: the n-th getPorts() call lists `lists[n]`, and the last
 * list repeats, so a test can model a board that re-enumerates after a reset.
 */
export function fakeSerial(...lists: SerialPortLike[][]): SerialLike & { calls: number } {
  const serial = {
    calls: 0,
    getPorts: async () => {
      const list = lists[Math.min(serial.calls, lists.length - 1)] ?? [];
      serial.calls++;
      return list;
    },
  };
  return serial;
}

export function fakeClock(start = 0): { t: number; now(): number; sleep(ms: number): Promise<void> } {
  const clock = {
    t: start,
    now: () => clock.t,
    sleep: async (ms: number) => {
      clock.t += ms;
      await Promise.resolve();
    },
  };
  return clock;
}
