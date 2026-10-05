// SPDX-License-Identifier: Apache-2.0
// Leaving download mode and opening the app's console (spec §5.8 step 3,
// contract §4.8). esptool-js's own hard reset only lowers RTS, so the
// installer pulses it itself, the way Espressif's Python esptool does for the S3.
import { InstallerError } from "./errors.ts";

export const RTC_CNTL_OPTION1_REG = 0x6000812c; // bit 0: FORCE_DOWNLOAD_BOOT
export const USB_JTAG = { usbVendorId: 0x303a, usbProductId: 0x1001 } as const;
export const CONSOLE_BAUD = 115200;

/** The parts of a Web Serial SerialPort the installer uses (tests pass a fake). */
export interface SerialPortLike {
  readonly readable: ReadableStream<Uint8Array> | null;
  readonly writable: WritableStream<Uint8Array> | null;
  open(options: { baudRate: number }): Promise<void>;
  close(): Promise<void>;
  setSignals(signals: { dataTerminalReady?: boolean; requestToSend?: boolean }): Promise<void>;
  getInfo(): { usbVendorId?: number; usbProductId?: number };
}

export interface SerialLike {
  getPorts(): Promise<SerialPortLike[]>;
}

export interface ResetDeps {
  loader: { writeReg(addr: number, value: number, mask?: number): Promise<void> };
  transport: { setDTR(state: boolean): Promise<void>; setRTS(state: boolean): Promise<void>; disconnect(): Promise<void> };
  port: SerialPortLike;
  serial: SerialLike;
  sleep(ms: number): Promise<void>;
  /** How long to wait for the board to come back after the reset. Default 10 s. */
  reacquireMs?: number;
}

export function isUsbJtag(port: SerialPortLike): boolean {
  const info = port.getInfo();
  return info.usbVendorId === USB_JTAG.usbVendorId && info.usbProductId === USB_JTAG.usbProductId;
}

/** Reset into the app and return the open console port (the same port, or the re-enumerated one). */
export async function resetToApp(deps: ResetDeps): Promise<SerialPortLike> {
  try {
    await deps.loader.writeReg(RTC_CNTL_OPTION1_REG, 0, 1);
  } catch {
    // The stub may already be gone; the RTS pulse below still resets the chip.
  }
  await deps.transport.setDTR(false);
  await deps.transport.setRTS(true);
  await deps.sleep(100);
  await deps.transport.setRTS(false);
  try {
    await deps.transport.disconnect();
  } catch {
    // The USB device may already have dropped off the bus.
  }
  return openConsolePort(deps.port, deps.serial, deps.sleep, deps.reacquireMs ?? 10_000);
}

/**
 * Open a console port at 115200 with DTR and RTS low (either line high resets
 * or straps a USB-Serial-JTAG chip). Tries `port` first, then any granted
 * 0x303A/0x1001 port, until `waitMs` has passed.
 *
 * Chrome rejects open() with the same "Failed to open serial port." when
 * another program holds the port and when the device is gone, so the error
 * text cannot tell them apart. Presence can: getPorts() lists only connected
 * devices. A listed port that refused on the last pass → port_busy; nothing
 * listed → port_lost.
 */
export async function openConsolePort(port: SerialPortLike, serial: SerialLike, sleep: (ms: number) => Promise<void>, waitMs: number): Promise<SerialPortLike> {
  const step = 250;
  let presentButRefused = false;
  for (let waited = 0; waited <= waitMs; waited += step) {
    const listed = await serial.getPorts();
    const candidates = [port, ...listed.filter((p) => p !== port && isUsbJtag(p))];
    presentButRefused = false;
    for (const candidate of candidates) {
      try {
        await candidate.open({ baudRate: CONSOLE_BAUD });
      } catch {
        if (listed.includes(candidate)) presentButRefused = true;
        continue;
      }
      await candidate.setSignals({ dataTerminalReady: false, requestToSend: false });
      return candidate;
    }
    await sleep(step);
  }
  throw presentButRefused
    ? new InstallerError("port_busy", "Another program or tab is using the board's port.")
    : new InstallerError("port_lost", "The board did not come back after the reset.");
}
