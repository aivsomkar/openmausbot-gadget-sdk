// SPDX-License-Identifier: Apache-2.0
// Every sentence the installer shows. Gadget-facing copy always names
// "MausBot → Settings → Remote access → Pair a gadget" (spec §6.4).
import type { PairingResult } from "./setup.ts";

export const PAIR_PATH = "MausBot → Settings → Remote access → Pair a gadget";

export const COPY = {
  title: "Install OpenMausBot Gadget",
  needSerial: "This browser can't reach USB serial devices. Open this page in Chrome or Edge on a computer.",
  noFirmware: "No firmware is published yet. Build it from source with AGENTS.md, or come back after the first release.",
  brokenIndex: "The published firmware list is broken. Try again later.",
  boardNotPublished: "This release has no firmware for this board.",
  devkitPort: "Plug the board in by the USB-C port labelled USB, not the one labelled UART.",
  noPortHint:
    "Don't see your board? Only ESP32-S3 boards on their native USB port show up here (a plain ESP32 or a USB-to-UART port won't). Use a USB-C cable that carries data, and if it still doesn't show, hold BOOT while you plug it in.",
  eraseLabel: "Erase everything",
  eraseHelp:
    "Wipes the whole flash, including the gadget's identity, Wi-Fi and pairing. You'll pair it again, and you can remove its old entry in MausBot → Settings → Remote access.",
  keepHelp: "A normal install keeps the gadget's identity, Wi-Fi and pairing.",
  installedPath: "Already installed? Set up Wi-Fi and pairing",
  pressRst: "Press RST or unplug and replug the board",
  waitingForBoard: "Waiting for the gadget to start…",
  stillPaired: (host: string | undefined) => (host ? `Still paired with ${host}. You're done.` : "Still paired. You're done."),
  reconnecting: "Reconnecting to MausBot…",
  wifiTitle: "Choose a Wi-Fi network",
  wifiOther: "Other network…",
  codeTitle: "Pair with MausBot",
  codeHow: `Open ${PAIR_PATH} and type the six-digit code here.`,
  remoteAccessOn: "Remote access must be on in MausBot.",
  windowsPublic:
    "If MausBot runs on Windows and this network is set to Public, Windows' firewall blocks the gadget. Open Windows Settings → Network & internet, choose this network and set its network profile type to Private.",
  hostTitle: "Find MausBot",
  hostNone: "The gadget couldn't find MausBot on this network. Type the address shown under Pair a gadget, for example 192.168.1.20:8810.",
  hostPick: "The gadget found more than one MausBot. Pick yours, or type the address shown under Pair a gadget.",
  deviceLimitWaiting:
    "MausBot has too many devices. Remove one in MausBot → Settings → Remote access; the gadget keeps trying for two minutes.",
  paired: (host: string | undefined) => (host ? `Paired with ${host}. Hold to talk!` : "Paired. Hold to talk!"),
  flashStages: {
    downloading: "Downloading firmware…",
    connecting: "Connecting to the board…",
    writing: "Installing…",
    done: "Installed.",
  },
} as const;

export function pairErrorMessage(code: string): string {
  switch (code) {
    case "bad_code":
      return "That code didn't work. Get a new one from Pair a gadget.";
    case "device_limit":
      return "MausBot still has too many devices. Remove one in MausBot → Settings → Remote access, then get a new code and try again.";
    case "proto_unsupported":
      return "This MausBot can't talk to this firmware. Update MausBot, then try again.";
    case "bad_sig":
      return "MausBot didn't accept the gadget's identity. Install again with Erase everything.";
    default:
      return `Pairing failed (${code}). Get a new code and try again.`;
  }
}

/** The sentence for every outcome except paired and need_host, which have their own screens. */
export function pairingFailure(result: PairingResult, ssid: string): string {
  switch (result.kind) {
    case "pair_error":
      return pairErrorMessage(result.code);
    case "device_limit":
      return pairErrorMessage("device_limit");
    case "wifi_failed":
      return `The gadget can't join ${ssid}. Check the password and try again.`;
    case "command_error":
      return `The gadget refused "${result.cmd}": ${result.message}`;
    case "timeout":
      return `Pairing didn't finish. Check that Remote access is on in MausBot, get a new code from Pair a gadget, and try again.`;
    case "lost":
      return `The board's port closed. ${COPY.pressRst}, then try again.`;
    case "paired":
    case "need_host":
      return "";
  }
}
