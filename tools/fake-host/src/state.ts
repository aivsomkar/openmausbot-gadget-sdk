// SPDX-License-Identifier: Apache-2.0
// What the fake host remembers: its host_id, enrolled gadgets and the one pairing window.
// With --state <dir>, host_id and gadgets persist in <dir>/fake-host.json (the window never does).
import { randomBytes, randomInt } from "node:crypto";
import { existsSync, mkdirSync, readFileSync, renameSync, writeFileSync } from "node:fs";
import { join } from "node:path";
import type { BotRef, GadgetSettings } from "../../../protocol/lib/types.ts";
import { HOST_ID_RE } from "../../../protocol/lib/types.ts";

export interface GadgetRecord {
  id: string;
  pubkey: string;          // canonical base64, as sent in hello
  name: string;
  board: string;
  fw: string;
  bot: BotRef;
  settings: GadgetSettings;
  namePending: boolean;    // renamed here while offline: keep this name at the next hello
  createdAt: number;
  lastSeenAt: number;
}

export interface PairingWindow {
  code: string;
  expiresAt: number;       // epoch ms
  attemptsLeft: number;
  used: boolean;
}

export type CodeCheck = { ok: true } | { ok: false; reason: "wrong" | "expired" | "used up" };

export const PAIR_ATTEMPTS = 5;

export class HostState {
  hostId: string;
  hostName: string;
  gadgets = new Map<string, GadgetRecord>();
  window: PairingWindow | null = null;
  readonly file: string | null;

  constructor(hostId: string, hostName: string, file: string | null) {
    this.hostId = hostId;
    this.hostName = hostName;
    this.file = file;
  }

  /** Loads <dir>/fake-host.json when stateDir is set. An explicit hostId wins over the file's. */
  static load(opts: { stateDir: string | null; hostId: string | null; hostName: string }): HostState {
    const file = opts.stateDir ? join(opts.stateDir, "fake-host.json") : null;
    let saved: { host_id?: string; gadgets?: GadgetRecord[] } = {};
    if (file && existsSync(file)) saved = JSON.parse(readFileSync(file, "utf8"));
    const fromFile = typeof saved.host_id === "string" && HOST_ID_RE.test(saved.host_id) ? saved.host_id : null;
    const state = new HostState(opts.hostId ?? fromFile ?? randomBytes(16).toString("hex"), opts.hostName, file);
    for (const g of saved.gadgets ?? []) state.gadgets.set(g.id, g);
    state.save();
    return state;
  }

  save(): void {
    if (!this.file) return;
    mkdirSync(join(this.file, ".."), { recursive: true });
    const tmp = this.file + ".tmp";
    writeFileSync(tmp, JSON.stringify({ version: 1, host_id: this.hostId, gadgets: [...this.gadgets.values()] }, null, 2) + "\n");
    renameSync(tmp, this.file);
  }

  /** Opens a new window, replacing any old one. A null code picks a random six-digit code. */
  openWindow(code: string | null, ttlS: number, now: number): PairingWindow {
    const c = code ?? String(randomInt(0, 1_000_000)).padStart(6, "0");
    this.window = { code: c, expiresAt: now + ttlS * 1000, attemptsLeft: PAIR_ATTEMPTS, used: false };
    return this.window;
  }

  /** Checks a code against the window. A wrong code (including one that is not six digits) uses an
   *  attempt. A matching code does not consume the window: the caller checks the device cap first. */
  checkCode(code: string, now: number): CodeCheck {
    const w = this.window;
    if (!w || w.used || w.attemptsLeft <= 0) return { ok: false, reason: "used up" };
    if (now >= w.expiresAt) return { ok: false, reason: "expired" };
    if (!/^\d{6}$/.test(code) || code !== w.code) {
      w.attemptsLeft -= 1;
      return { ok: false, reason: "wrong" };
    }
    return { ok: true };
  }

  consumeWindow(): void {
    if (this.window) this.window.used = true;
  }
}
