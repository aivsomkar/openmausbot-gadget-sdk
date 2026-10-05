// SPDX-License-Identifier: Apache-2.0
// Test doubles for Web Serial: a port you can push output into, a
// navigator.serial stand-in and a virtual clock.
import type { SerialLike, SerialPortLike } from "../src/reset.ts";

/** A Web Serial port: push device output with emit(), read what the page wrote with sent(). */
export class FakePort implements SerialPortLike {
  readable: ReadableStream<Uint8Array> | null = null;
  writable: WritableStream<Uint8Array> | null = null;
  info: { usbVendorId?: number; usbProductId?: number };
  openFailures: Error[] = [];
  signals: Array<{ dataTerminalReady?: boolean; requestToSend?: boolean }> = [];
  opened = 0;
  private controller: ReadableStreamDefaultController<Uint8Array> | null = null;
  private written = "";
  constructor(info = { usbVendorId: 0x303a, usbProductId: 0x1001 }) {
    this.info = info;
  }
  async open(_options: { baudRate: number }): Promise<void> {
    const failure = this.openFailures.shift();
    if (failure) throw failure;
    this.opened++;
    this.readable = new ReadableStream<Uint8Array>({ start: (c) => void (this.controller = c) });
    this.writable = new WritableStream<Uint8Array>({ write: (chunk) => void (this.written += new TextDecoder().decode(chunk)) });
  }
  async close(): Promise<void> {
    this.readable = null;
    this.writable = null;
  }
  async setSignals(signals: { dataTerminalReady?: boolean; requestToSend?: boolean }): Promise<void> {
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
  sent(): string[] {
    return this.written.split("\n").filter((l) => l !== "");
  }
}

export function fakeSerial(ports: SerialPortLike[]): SerialLike {
  return { getPorts: async () => ports };
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
