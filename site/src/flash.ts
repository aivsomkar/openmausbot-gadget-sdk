// SPDX-License-Identifier: Apache-2.0
// Flashing one board with esptool-js (spec §5.8 step 2). Downloads every part
// first, then connects, checks the chip and flash size, turns the chip's
// watchdogs off, and writes the separate parts so NVS (identity, Wi-Fi,
// pairing) survives.
import type { FlashOptions } from "esptool-js";
import { InstallerError } from "./errors.ts";
import { flashPlan, type InstallIndex } from "./install.ts";

/** The slice of esptool-js's ESPLoader the installer uses (tests pass a mock). */
export interface LoaderLike {
  chip: { CHIP_NAME: string };
  main(mode?: "default_reset"): Promise<string>;
  detectFlashSize(): Promise<string | undefined>;
  writeFlash(options: FlashOptions): Promise<void>;
  writeReg(addr: number, value: number, mask?: number): Promise<void>;
  readReg(addr: number): Promise<number>;
}

export type FlashStage = "downloading" | "connecting" | "writing" | "done";

export interface FlashDeps {
  loader: LoaderLike;
  index: InstallIndex;
  board: string;
  eraseAll: boolean;
  fetchPart(path: string): Promise<Uint8Array>;
  md5(data: Uint8Array): string;
  onStage(stage: FlashStage): void;
  onProgress(fraction: number): void;
  /** Something went wrong that does not stop the install (shown in the console log). */
  onWarning?(message: string): void;
}

export const REQUIRED_FLASH_MB = 16;

// ESP32-S3 RTC watchdog and super watchdog registers (Espressif's esptool, esp32s3.py).
const RTC_CNTL_WDTCONFIG0_REG = 0x60008098;
const RTC_CNTL_WDTWPROTECT_REG = 0x600080b0;
const RTC_CNTL_SWD_CONF_REG = 0x600080b4;
const RTC_CNTL_SWD_WPROTECT_REG = 0x600080b8;
const RTC_CNTL_WDT_WKEY = 0x50d83aa1;
const RTC_CNTL_SWD_WKEY = 0x8f1d312a;
const RTC_CNTL_SWD_AUTO_FEED_EN = 0x80000000;

/**
 * Turns off the RTC watchdog and lets the super watchdog feed itself, as
 * Python esptool does before flashing an S3 over USB-Serial-JTAG: the core
 * reset into download mode can leave both running, and either one would reset
 * the chip in the middle of the write. esptool-js 0.7.0 does not do this.
 */
export async function disableWatchdogs(loader: LoaderLike): Promise<void> {
  await loader.writeReg(RTC_CNTL_WDTWPROTECT_REG, RTC_CNTL_WDT_WKEY);
  await loader.writeReg(RTC_CNTL_WDTCONFIG0_REG, 0);
  await loader.writeReg(RTC_CNTL_WDTWPROTECT_REG, 0);
  await loader.writeReg(RTC_CNTL_SWD_WPROTECT_REG, RTC_CNTL_SWD_WKEY);
  const swd = await loader.readReg(RTC_CNTL_SWD_CONF_REG);
  await loader.writeReg(RTC_CNTL_SWD_CONF_REG, (swd | RTC_CNTL_SWD_AUTO_FEED_EN) >>> 0);
  await loader.writeReg(RTC_CNTL_SWD_WPROTECT_REG, 0);
}

/** "16MB" → 16, "512KB" → 0.5, anything else → null. */
export function flashSizeMb(size: string | undefined): number | null {
  const m = /^(\d+)(KB|MB)$/.exec(size ?? "");
  if (!m) return null;
  return m[2] === "MB" ? Number(m[1]) : Number(m[1]) / 1024;
}

export async function flashBoard(deps: FlashDeps): Promise<{ bytes: number }> {
  let plan;
  try {
    plan = flashPlan(deps.index, deps.board);
  } catch {
    throw new InstallerError("board_not_published", `No firmware for ${deps.board} in this release.`);
  }
  deps.onStage("downloading");
  const fileArray: FlashOptions["fileArray"] = [];
  for (const part of plan) {
    try {
      fileArray.push({ address: part.address, data: await deps.fetchPart(part.path) });
    } catch (err) {
      throw new InstallerError("download_failed", `Couldn't download ${part.path}: ${(err as Error).message}`);
    }
  }
  const total = fileArray.reduce((n, f) => n + f.data.length, 0);

  deps.onStage("connecting");
  await deps.loader.main("default_reset");
  const chip = deps.loader.chip.CHIP_NAME;
  if (chip !== "ESP32-S3") throw new InstallerError("wrong_chip", `This board is an ${chip}, not an ESP32-S3.`);
  const mb = flashSizeMb(await deps.loader.detectFlashSize());
  if (mb !== null && mb < REQUIRED_FLASH_MB) {
    throw new InstallerError("flash_too_small", `This board has ${mb} MB of flash. The gadget firmware needs ${REQUIRED_FLASH_MB} MB.`);
  }
  try {
    await disableWatchdogs(deps.loader);
  } catch (err) {
    deps.onWarning?.(`Couldn't turn the chip's watchdogs off before writing (${(err as Error).message}); installing anyway.`);
  }

  deps.onStage("writing");
  const before = fileArray.map((_, i) => fileArray.slice(0, i).reduce((n, f) => n + f.data.length, 0));
  try {
    await deps.loader.writeFlash({
      fileArray,
      flashMode: "keep",
      flashFreq: "keep",
      flashSize: "keep",
      eraseAll: deps.eraseAll,
      compress: true,
      reportProgress: (fileIndex, written, size) => {
        const part = fileArray[fileIndex]?.data.length ?? 0;
        const done = (before[fileIndex] ?? 0) + (size > 0 ? (written / size) * part : 0);
        deps.onProgress(Math.min(1, total > 0 ? done / total : 1));
      },
      calculateMD5Hash: (image) => deps.md5(image),
    });
  } catch (err) {
    throw new InstallerError("write_failed", `Writing the flash failed: ${(err as Error).message}`);
  }
  deps.onProgress(1);
  deps.onStage("done");
  return { bytes: total };
}
