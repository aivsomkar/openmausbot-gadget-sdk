// SPDX-License-Identifier: Apache-2.0
// Console steps after flashing (spec §5.8 steps 3–6): find the app, list
// Wi-Fi networks, send pair → wifi → host, and poll status until paired.
import { isDownloadModeLine, type OmbMessage } from "./console.ts";
import type { ConsoleIO, ConsoleSession } from "./serial-console.ts";

export type StatusMsg = Extract<OmbMessage, { op: "status" }>;
export type ScanMsg = Extract<OmbMessage, { op: "scan" }>;
export type HostsMsg = Extract<OmbMessage, { op: "hosts" }>;

export interface Clock {
  now(): number;
  sleep(ms: number): Promise<void>;
}

export const realClock: Clock = {
  now: () => Date.now(),
  sleep: (ms) => new Promise((resolve) => setTimeout(resolve, ms)),
};

export type AppCheck =
  | { kind: "app"; status: StatusMsg | null }
  | { kind: "download_mode" }
  | { kind: "silent" }
  | { kind: "lost" };

/** Sends `status` every second; "silent" after waitMs with no app output (spec: ~5 s → press RST). */
export async function detectApp(io: ConsoleIO, clock: Clock, waitMs = 5000): Promise<AppCheck> {
  const end = clock.now() + waitMs;
  let booted = false;
  let downloadMode = false;
  for (;;) {
    for (const e of io.drain()) {
      if (e.msg?.op === "status") return { kind: "app", status: e.msg };
      if (e.msg?.op === "boot") booted = true;
      if (isDownloadModeLine(e.line)) downloadMode = true;
    }
    if (io.lost) return { kind: "lost" };
    if (clock.now() >= end) {
      if (booted) return { kind: "app", status: null };
      return downloadMode ? { kind: "download_mode" } : { kind: "silent" };
    }
    try {
      await io.send("status");
    } catch {
      if (io.lost) return { kind: "lost" };
    }
    await clock.sleep(1000);
  }
}

export type WaitPrompt = "waiting" | "press_rst";

/**
 * Waits for the app on the console `open()` returns (spec §5.8 step 3). After
 * ~5 s with no app output it shows the RST prompt and keeps it up while it
 * keeps asking the same console. It reopens only when the port is lost:
 * pressing RST or replugging drops the USB device, while a silent board that
 * is still plugged in keeps its port. `open()` is called again for each reopen.
 */
export async function waitForApp(
  open: () => Promise<ConsoleSession>,
  clock: Clock,
  onPrompt: (prompt: WaitPrompt) => void,
): Promise<{ io: ConsoleSession; status: StatusMsg | null }> {
  let io = await open();
  let shown: WaitPrompt = "waiting";
  onPrompt(shown);
  for (;;) {
    const check = await detectApp(io, clock);
    if (check.kind === "app") return { io, status: check.status };
    if (shown !== "press_rst") {
      shown = "press_rst";
      onPrompt(shown);
    }
    if (check.kind !== "lost") continue;
    await io.close();
    io = await open();
  }
}

export type ExistingState = "paired" | "reconnecting" | "needs_setup";

/** A reinstall keeps NVS, so a reflashed gadget may still be paired. */
export function classifyStatus(status: StatusMsg | null): ExistingState {
  if (status === null) return "needs_setup";
  if (status.pair === "paired") return "paired";
  if (status.pair === "connecting" && status.ssid !== undefined) return "reconnecting";
  return "needs_setup";
}

/** Polls status until `pair` is `paired` (→ that status) or waitMs passes (→ null). */
export async function waitForPaired(io: ConsoleIO, clock: Clock, waitMs = 20_000): Promise<StatusMsg | null> {
  const end = clock.now() + waitMs;
  for (;;) {
    for (const e of io.drain()) if (e.msg?.op === "status" && e.msg.pair === "paired") return e.msg;
    if (io.lost || clock.now() >= end) return null;
    try {
      await io.send("status");
    } catch {
      return null;
    }
    await clock.sleep(1000);
  }
}

/** `log off`, then `scan`; resolves with the gadget's own network list (strongest first). */
export async function scanNetworks(io: ConsoleIO, clock: Clock, waitMs = 15_000): Promise<ScanMsg["networks"]> {
  io.drain();
  await io.send("log off");
  await io.send("scan");
  const end = clock.now() + waitMs;
  for (;;) {
    for (const e of io.drain()) {
      if (e.msg?.op === "scan") return e.msg.networks;
      if (e.msg?.op === "error" && e.msg.cmd === "scan") throw new Error(e.msg.message);
    }
    if (io.lost) throw new Error("The board's port closed.");
    if (clock.now() >= end) throw new Error("The gadget didn't list any Wi-Fi networks.");
    await clock.sleep(250);
  }
}

export type PairingResult =
  | { kind: "paired"; status: StatusMsg }
  | { kind: "need_host"; hosts: HostsMsg["hosts"] }
  | { kind: "pair_error"; code: string; status: StatusMsg }
  | { kind: "wifi_failed"; status: StatusMsg }
  | { kind: "device_limit"; status: StatusMsg }
  | { kind: "command_error"; cmd: string; message: string }
  | { kind: "timeout"; status: StatusMsg | null }
  | { kind: "lost" };

/** Something the person should know while pairing goes on. */
export type PairingNotice = "device_limit";

const SETUP_CMDS = new Set(["pair", "wifi", "host"]);
const PAIR_PROGRESS: readonly string[] = ["code_stored", "connecting", "paired"];
const WIFI_PROGRESS: readonly string[] = ["connecting", "connected"];
/** Status replies after the commands that may still describe the previous attempt. */
export const STALE_POLLS = 3;

/**
 * Sends `commands`, then polls `status` every pollMs until paired, a failure,
 * or timeoutMs. A `pair: "error"` or `wifi: "failed"` left over from the
 * previous attempt is ignored until that side shows progress or STALE_POLLS
 * replies have passed. `device_limit` keeps polling (spec §4.3).
 */
export async function pairAndWait(
  io: ConsoleIO,
  commands: readonly string[],
  clock: Clock,
  options: { pollMs?: number; timeoutMs?: number; onNotice?: (notice: PairingNotice) => void } = {},
): Promise<PairingResult> {
  const pollMs = options.pollMs ?? 1000;
  const end = clock.now() + (options.timeoutMs ?? 150_000);
  io.drain();
  try {
    for (const c of commands) await io.send(c);
  } catch {
    return { kind: "lost" };
  }
  let last: StatusMsg | null = null;
  let polls = 0;
  let pairMoved = false; // pair has been code_stored/connecting/paired since the commands: the new code is in use
  let wifiMoved = false; // wifi has been connecting/connected since the commands: the new network is in use
  let noticed = false;
  for (;;) {
    for (const e of io.drain()) {
      const m = e.msg;
      if (m === null) continue;
      if (m.op === "error" && SETUP_CMDS.has(m.cmd)) return { kind: "command_error", cmd: m.cmd, message: m.message };
      if (m.op === "hosts") {
        const only = m.hosts.length === 1 ? m.hosts[0] : undefined;
        if (only !== undefined) {
          try {
            await io.send(`host ${only.address}`);
          } catch {
            return { kind: "lost" };
          }
          continue;
        }
        return { kind: "need_host", hosts: m.hosts };
      }
      if (m.op === "status") {
        last = m;
        polls++;
        if (PAIR_PROGRESS.includes(m.pair)) pairMoved = true;
        if (WIFI_PROGRESS.includes(m.wifi)) wifiMoved = true;
        const settled = polls > STALE_POLLS;
        if (m.pair === "paired") return { kind: "paired", status: m };
        if (m.pair === "error" && m.error === "device_limit") {
          if (!noticed) options.onNotice?.("device_limit");
          noticed = true;
          continue;
        }
        if (m.pair === "error" && m.error !== undefined && (pairMoved || settled)) return { kind: "pair_error", code: m.error, status: m };
        if (m.wifi === "failed" && (wifiMoved || settled)) return { kind: "wifi_failed", status: m };
      }
    }
    if (io.lost) return { kind: "lost" };
    if (clock.now() >= end) {
      return last?.pair === "error" && last.error === "device_limit" ? { kind: "device_limit", status: last } : { kind: "timeout", status: last };
    }
    try {
      await io.send("status");
    } catch {
      return { kind: "lost" };
    }
    await clock.sleep(pollMs);
  }
}
