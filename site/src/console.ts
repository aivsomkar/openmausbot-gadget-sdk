// SPDX-License-Identifier: Apache-2.0
// The gadget's USB console as the installer sees it (spec §5.6, contract §2.11, §4.8).
// Pure functions only: no Web Serial here, so Node tests cover all of it.

export type WifiState = "off" | "connecting" | "connected" | "failed";
export type PairState = "unpaired" | "code_stored" | "connecting" | "paired" | "error";
export type WifiAuth = "open" | "wep" | "wpa" | "wpa2" | "wpa3" | "wpa2-ent" | "other";

export type OmbMessage =
  | { op: "boot"; board: string; fw: string; id: string }
  | {
      op: "status";
      wifi: WifiState;
      ssid?: string;
      host?: string;
      id: string;
      pair: PairState;
      error?: string;
      fw: string;
      battery?: { pct: number; charging: boolean };
      board?: string;
      name?: string;
      host_name?: string;
    }
  | { op: "scan"; networks: Array<{ ssid: string; rssi: number; auth: WifiAuth }> }
  | { op: "hosts"; hosts: Array<{ name: string; address: string; id: string }> }
  | { op: "say"; turn: string }
  | { op: "error"; cmd: string; message: string };

const OMB_PREFIX = "@omb ";
// CSI sequences (colours, cursor moves) that ESP-IDF's log adds around lines.
const ANSI_RE = /\u001b\[[0-9;?]*[ -/]*[@-~]/g;
const WIFI: readonly string[] = ["off", "connecting", "connected", "failed"];
const PAIR: readonly string[] = ["unpaired", "code_stored", "connecting", "paired", "error"];
const AUTH: readonly string[] = ["open", "wep", "wpa", "wpa2", "wpa3", "wpa2-ent", "other"];

function isObj(v: unknown): v is Record<string, unknown> {
  return typeof v === "object" && v !== null && !Array.isArray(v);
}
const isStr = (v: unknown): v is string => typeof v === "string";
const optStr = (v: unknown): boolean => v === undefined || typeof v === "string";

/** One `@omb` line → message. Strips ANSI first; null for every other line, bad JSON and unknown ops. */
export function parseOmbLine(line: string): OmbMessage | null {
  const clean = line.replace(ANSI_RE, "").replace(/\r$/, "");
  if (!clean.startsWith(OMB_PREFIX)) return null;
  let v: unknown;
  try {
    v = JSON.parse(clean.slice(OMB_PREFIX.length));
  } catch {
    return null;
  }
  if (!isObj(v) || !isStr(v.op)) return null;
  switch (v.op) {
    case "boot":
      return isStr(v.board) && isStr(v.fw) && isStr(v.id) ? (v as OmbMessage) : null;
    case "status": {
      if (!isStr(v.wifi) || !WIFI.includes(v.wifi) || !isStr(v.id) || !isStr(v.pair) || !PAIR.includes(v.pair) || !isStr(v.fw)) return null;
      if (!optStr(v.ssid) || !optStr(v.host) || !optStr(v.error) || !optStr(v.board) || !optStr(v.name) || !optStr(v.host_name)) return null;
      if (v.battery !== undefined && !(isObj(v.battery) && typeof v.battery.pct === "number" && typeof v.battery.charging === "boolean")) return null;
      return v as OmbMessage;
    }
    case "scan":
      return Array.isArray(v.networks) &&
        v.networks.every((n) => isObj(n) && isStr(n.ssid) && typeof n.rssi === "number" && isStr(n.auth) && AUTH.includes(n.auth))
        ? (v as OmbMessage)
        : null;
    case "hosts":
      return Array.isArray(v.hosts) && v.hosts.every((h) => isObj(h) && isStr(h.name) && isStr(h.address) && isStr(h.id))
        ? (v as OmbMessage)
        : null;
    case "say":
      return isStr(v.turn) ? (v as OmbMessage) : null;
    case "error":
      return isStr(v.cmd) && isStr(v.message) ? (v as OmbMessage) : null;
    default:
      return null;
  }
}

/** Returns a feeder: give it raw text chunks, get back complete lines. CR, LF and CRLF each end one line. */
export function createLineSplitter(): (chunk: string) => string[] {
  let buf = "";
  let lastCr = false;
  return (chunk: string): string[] => {
    const lines: string[] = [];
    for (const ch of chunk) {
      if (ch === "\n") {
        if (lastCr) {
          lastCr = false;
          continue;
        }
        lines.push(buf);
        buf = "";
      } else if (ch === "\r") {
        lines.push(buf);
        buf = "";
        lastCr = true;
        continue;
      } else {
        buf += ch;
      }
      lastCr = false;
    }
    return lines;
  };
}

/**
 * One console argument in esp_console_split_argv quoting: `"…"` with `\\` and `\"`.
 * Throws on CR, LF or NUL (the firmware's line buffer is a C string, so a NUL would cut the line).
 */
export function quoteArg(value: string): string {
  if (/[\r\n\u0000]/.test(value)) throw new RangeError("console arguments cannot contain line breaks or NUL characters");
  return `"${value.replace(/\\/g, "\\\\").replace(/"/g, '\\"')}"`;
}

/** In this order: pair <code>, wifi "<ssid>" "<password>", host auto (or host <address>). */
export function setupCommands(input: { code: string; ssid: string; password: string; address?: string }): string[] {
  if (!/^\d{6}$/.test(input.code)) throw new RangeError("the pairing code is six digits");
  const problem = wifiProblem(input.ssid, input.password);
  if (problem !== null) throw new RangeError(problem);
  const host = input.address === undefined ? "host auto" : `host ${checkedAddress(input.address)}`;
  return [`pair ${input.code}`, `wifi ${quoteArg(input.ssid)} ${quoteArg(input.password)}`, host];
}

const utf8Length = (s: string): number => new TextEncoder().encode(s).length;
/** firmware/core/src/console.c parse_host: the address before `:port` is at most 57 bytes (ASCII only here). */
const MAX_HOST_BYTES = 57;

/** The console's `wifi` rules (contract §2.11) as a sentence for the person, or null when fine. */
export function wifiProblem(ssid: string, password: string): string | null {
  if (/[\r\n\u0000]/.test(ssid) || /[\r\n\u0000]/.test(password)) return "Network names and passwords can't contain line breaks or NUL characters.";
  const s = utf8Length(ssid);
  if (s < 1 || s > 32) return "The network name must be 1 to 32 bytes long.";
  if (password === "") return null;
  if (/^[0-9a-fA-F]{64}$/.test(password)) return null;
  const p = utf8Length(password);
  if (p < 8 || p > 63) return "The password must be 8 to 63 characters, 64 hex digits, or empty for an open network.";
  return null;
}

/** Six digits from what the person typed ("123 456", " 123-456 "), or null. */
export function normalizePairCode(text: string): string | null {
  const digits = text.replace(/[\s-]/g, "");
  return /^\d{6}$/.test(digits) ? digits : null;
}

/**
 * `host` argument from what the person typed, or null: hostname or IPv4 of at
 * most 57 bytes (the firmware's limit, so `addr:port` fits 64), optional port 1–65535.
 */
export function normalizeHostAddress(text: string): string | null {
  let t = text.trim().replace(/^(?:https?|wss?):\/\//i, "");
  t = t.replace(/\/.*$/, "");
  const m = /^([A-Za-z0-9](?:[A-Za-z0-9.-]{0,251}[A-Za-z0-9])?)(?::(\d{1,5}))?$/.exec(t);
  const host = m?.[1];
  if (m === null || host === undefined || host.length > MAX_HOST_BYTES) return null;
  const port = m[2];
  if (port === undefined) return host;
  const n = Number(port);
  return n >= 1 && n <= 65535 ? `${host}:${n}` : null;
}

function checkedAddress(address: string): string {
  const a = normalizeHostAddress(address);
  if (a === null) throw new RangeError(`not a host address: ${address}`);
  return a;
}

/** The ROM's download-mode banner: the chip did not start the app. */
export function isDownloadModeLine(line: string): boolean {
  return /waiting for download/i.test(line) || /boot:0x[0-9a-f]+ \(DOWNLOAD/i.test(line);
}
