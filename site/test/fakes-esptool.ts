// SPDX-License-Identifier: Apache-2.0
// Test doubles for esptool-js: a loader that records calls and a transport.
import type { FlashOptions } from "esptool-js";
import type { LoaderLike } from "../src/flash.ts";

export class FakeLoader implements LoaderLike {
  chip = { CHIP_NAME: "ESP32-S3" };
  flashSize: string | undefined = "16MB";
  writeFlashError: Error | null = null;
  /** What readReg returns for RTC_CNTL_SWD_CONF_REG (0x600080B4); every other register reads 0. */
  swdConf = 0x12;
  calls: string[] = [];
  written: FlashOptions | null = null;
  md5s: string[] = [];
  constructor(log: string[] = []) {
    this.calls = log;
  }
  async main(mode?: string): Promise<string> {
    this.calls.push(`main ${mode ?? ""}`.trim());
    return this.chip.CHIP_NAME;
  }
  async detectFlashSize(): Promise<string | undefined> {
    this.calls.push("detectFlashSize");
    return this.flashSize;
  }
  async writeFlash(options: FlashOptions): Promise<void> {
    this.calls.push(`writeFlash eraseAll=${options.eraseAll}`);
    this.written = options;
    options.fileArray.forEach((f, i) => {
      options.reportProgress?.(i, 0, f.data.length);
      if (options.calculateMD5Hash) this.md5s.push(options.calculateMD5Hash(f.data));
      options.reportProgress?.(i, f.data.length, f.data.length);
    });
    if (this.writeFlashError) throw this.writeFlashError;
  }
  async writeReg(addr: number, value: number, mask?: number): Promise<void> {
    this.calls.push(`writeReg 0x${addr.toString(16)} ${value} ${mask ?? 0xffffffff}`);
  }
  async readReg(addr: number): Promise<number> {
    this.calls.push(`readReg 0x${addr.toString(16)}`);
    return addr === 0x600080b4 ? this.swdConf : 0;
  }
}

export class FakeTransport {
  readonly log: string[];
  constructor(log: string[]) {
    this.log = log;
  }
  async setDTR(state: boolean): Promise<void> {
    this.log.push(`setDTR ${state}`);
  }
  async setRTS(state: boolean): Promise<void> {
    this.log.push(`setRTS ${state}`);
  }
  async disconnect(): Promise<void> {
    this.log.push("disconnect");
  }
}
