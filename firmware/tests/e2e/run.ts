// firmware/tests/e2e/run.ts
// SPDX-License-Identifier: Apache-2.0
// Simulator <-> fake host end-to-end runner (spec §10). Each scenario file
// drives both sides at the protocol level: the [host] steps talk to
// tools/fake-host over its JSON-lines control (contract §4.7), the [sim]
// lines become a headless gadget-sim script on a real socket, and [check]
// lines inspect the results. No pixels are compared here.
//
//   node firmware/tests/e2e/run.ts --all
//   node firmware/tests/e2e/run.ts [--sim <gadget-sim>] [--fake-host <main.ts>] <scenario.txt>...
//
// Scenario format (one directive per line, '#' comments, ${TMP} = the run's temp dir):
//   board <id>                      gadget-sim --board (default amoled-175c)
//   sim-args <flags...>             extra gadget-sim flags
//   host-args <flags...>            extra fake-host flags
//   mic tone <ms>                   write ${TMP}/mic.wav (300 Hz) and pass --mic-file
//   speaker                         pass --speaker-file ${TMP}/spk.wav
//   ota-image <bytes>               write ${TMP}/ota.bin
//   timeout <ms>                    for the whole scenario (default 60000)
//   [host]
//   await <event> [k=v...] [timeout=<ms>]   wait for a fake-host event; for rx/tx, k is looked up in msg first
//   send <json>                     write one control command and wait for its ok ack
//   [sim]
//   <gadget-sim script lines>
//   [check]
//   wav-min-ms <file> <ms>          the WAV in ${TMP} holds at least that much audio
//   no-event <event> [k=v...]       no such fake-host event happened
import { spawn } from "node:child_process";
import type { ChildProcess } from "node:child_process";
import { mkdtempSync, readFileSync, readdirSync, rmSync, writeFileSync, existsSync, statSync } from "node:fs";
import { tmpdir } from "node:os";
import { dirname, join, resolve } from "node:path";
import { createInterface } from "node:readline";
import { fileURLToPath } from "node:url";

const HERE = dirname(fileURLToPath(import.meta.url));
const REPO = resolve(HERE, "../../..");
const CODE = "123456";

type Ev = Record<string, unknown> & { event: string };
type Scenario = {
  name: string;
  board: string;
  simArgs: string[];
  hostArgs: string[];
  micMs: number;
  speaker: boolean;
  otaBytes: number;
  timeoutMs: number;
  host: string[];
  sim: string[];
  check: string[];
};

function parseScenario(path: string): Scenario {
  const s: Scenario = {
    name: path.split("/").pop()!.replace(/\.txt$/, ""),
    board: "amoled-175c", simArgs: [], hostArgs: [], micMs: 0, speaker: false, otaBytes: 0,
    timeoutMs: 60000, host: [], sim: [], check: [],
  };
  let section = "";
  for (const raw of readFileSync(path, "utf8").split("\n")) {
    const line = raw.trim();
    if (line === "" || line.startsWith("#")) continue;
    if (/^\[(host|sim|check)\]$/.test(line)) {
      section = line.slice(1, -1);
      continue;
    }
    if (section === "host") s.host.push(line);
    else if (section === "sim") s.sim.push(line);
    else if (section === "check") s.check.push(line);
    else {
      const [k, ...rest] = line.split(/\s+/);
      if (k === "board") s.board = rest[0];
      else if (k === "sim-args") s.simArgs.push(...rest);
      else if (k === "host-args") s.hostArgs.push(...rest);
      else if (k === "mic" && rest[0] === "tone") s.micMs = Number(rest[1]);
      else if (k === "speaker") s.speaker = true;
      else if (k === "ota-image") s.otaBytes = Number(rest[0]);
      else if (k === "timeout") s.timeoutMs = Number(rest[0]);
      else throw new Error(`${path}: unknown directive ${k}`);
    }
  }
  return s;
}

function writeTone(path: string, ms: number): void {
  const n = Math.round((16000 * ms) / 1000);
  const b = Buffer.alloc(44 + n * 2);
  b.write("RIFF", 0); b.writeUInt32LE(36 + n * 2, 4); b.write("WAVE", 8); b.write("fmt ", 12);
  b.writeUInt32LE(16, 16); b.writeUInt16LE(1, 20); b.writeUInt16LE(1, 22); b.writeUInt32LE(16000, 24);
  b.writeUInt32LE(32000, 28); b.writeUInt16LE(2, 32); b.writeUInt16LE(16, 34); b.write("data", 36);
  b.writeUInt32LE(n * 2, 40);
  for (let i = 0; i < n; i++) b.writeInt16LE(Math.round(6000 * Math.sin((2 * Math.PI * 300 * i) / 16000)), 44 + 2 * i);
  writeFileSync(path, b);
}

function wavMs(path: string): number {
  const b = readFileSync(path);
  return (b.readUInt32LE(40) / 2 / b.readUInt32LE(24)) * 1000;
}

/* k=v conditions; for rx/tx events k is looked up in msg first. */
function matches(ev: Ev, conds: string[]): boolean {
  for (const c of conds) {
    const eq = c.indexOf("=");
    const key = c.slice(0, eq);
    const want = c.slice(eq + 1);
    let got: unknown;
    if ((ev.event === "rx" || ev.event === "tx") && ev.msg !== undefined) {
      const msg = typeof ev.msg === "string" ? JSON.parse(ev.msg) : ev.msg;
      got = key.split(".").reduce<unknown>((o, k) => (o as Record<string, unknown> | undefined)?.[k], msg);
    }
    if (got === undefined) got = key.split(".").reduce<unknown>((o, k) => (o as Record<string, unknown> | undefined)?.[k], ev);
    if (String(got) !== want) return false;
  }
  return true;
}

class EventLog {
  events: Ev[] = [];
  cursor = 0;
  waiters: Array<() => void> = [];
  push(ev: Ev): void {
    this.events.push(ev);
    for (const w of this.waiters.splice(0)) w();
  }
  /* The first matching event after the previous await matched (earlier arrivals count). */
  async next(event: string, conds: string[], timeoutMs: number, from?: number): Promise<Ev> {
    const deadline = Date.now() + timeoutMs;
    for (;;) {
      for (let i = from ?? this.cursor; i < this.events.length; i++) {
        const ev = this.events[i];
        if (ev.event === event && matches(ev, conds)) {
          if (from === undefined) this.cursor = i + 1;
          return ev;
        }
      }
      const left = deadline - Date.now();
      if (left <= 0) throw new Error(`no ${event} ${conds.join(" ")} within ${timeoutMs} ms`);
      await new Promise<void>((res) => {
        const t = setTimeout(res, left);
        this.waiters.push(() => { clearTimeout(t); res(); });
      });
    }
  }
}

function lines(child: ChildProcess, which: "stdout" | "stderr", sink: string[], onLine?: (l: string) => void): void {
  const rl = createInterface({ input: child[which]! });
  rl.on("line", (l) => {
    sink.push(l);
    if (onLine) onLine(l);
  });
}

async function runScenario(path: string, sim: string, fakeHost: string): Promise<boolean> {
  const s = parseScenario(path);
  const tmp = mkdtempSync(join(tmpdir(), `gadget-e2e-${s.name}-`));
  const sub = (l: string) => l.replaceAll("${TMP}", tmp);
  const hostOut: string[] = [], hostErr: string[] = [], simOut: string[] = [], simErr: string[] = [];
  const log = new EventLog();
  let host: ChildProcess | undefined, gadget: ChildProcess | undefined;
  const t0 = Date.now();
  try {
    if (s.micMs > 0) writeTone(join(tmp, "mic.wav"), s.micMs);
    if (s.otaBytes > 0) writeFileSync(join(tmp, "ota.bin"), Buffer.from(Array.from({ length: s.otaBytes }, (_, i) => (i * 131 + 7) & 0xff)));
    host = spawn(process.execPath, [fakeHost, "--port", "0", "--code", CODE, ...s.hostArgs.map(sub)], { stdio: ["pipe", "pipe", "pipe"] });
    lines(host, "stdout", hostOut, (l) => {
      try { log.push(JSON.parse(l) as Ev); } catch { /* not an event line */ }
    });
    lines(host, "stderr", hostErr);
    const listening = await log.next("listening", [], 15000, 0);
    writeFileSync(join(tmp, "sim.txt"), s.sim.map(sub).join("\n") + "\n");
    const args = ["--board", s.board, "--headless", "--script", join(tmp, "sim.txt"), "--host", `127.0.0.1:${listening.port}`,
      "--pair", CODE, "--state-dir", join(tmp, "state")];
    if (s.micMs > 0) args.push("--mic-file", join(tmp, "mic.wav"));
    if (s.speaker) args.push("--speaker-file", join(tmp, "spk.wav"));
    args.push(...s.simArgs.map(sub));
    gadget = spawn(sim, args, { stdio: ["ignore", "pipe", "pipe"] });
    lines(gadget, "stdout", simOut);
    lines(gadget, "stderr", simErr);
    const simExit = new Promise<number>((res) => gadget!.on("exit", (code) => res(code ?? 1)));

    const hostSteps = (async () => {
      for (const raw of s.host) {
        const line = sub(raw);
        const [verb, ...rest] = line.split(/\s+/);
        if (verb === "await") {
          const timeoutArg = rest.find((r) => r.startsWith("timeout="));
          const conds = rest.slice(1).filter((r) => !r.startsWith("timeout="));
          await log.next(rest[0], conds, timeoutArg ? Number(timeoutArg.slice(8)) : 15000);
        } else if (verb === "send") {
          const json = line.slice(5).trim();
          const cmd = JSON.parse(json).cmd as string;
          const mark = log.events.length;
          host!.stdin!.write(json + "\n");
          const ack = await log.next("ack", [`cmd=${cmd}`], 10000, mark);
          if (ack.ok !== true) throw new Error(`fake host refused ${cmd}: ${String(ack.error)}`);
        } else {
          throw new Error(`unknown host step ${verb}`);
        }
      }
    })();

    const timeout = new Promise<never>((_, rej) => setTimeout(() => rej(new Error(`scenario timed out after ${s.timeoutMs} ms`)), s.timeoutMs));
    const [code] = await Promise.race([Promise.all([simExit, hostSteps]), timeout]);
    if (code !== 0) throw new Error(`gadget-sim exited with ${code}`);
    for (const raw of s.check) {
      const [verb, ...rest] = sub(raw).split(/\s+/);
      if (verb === "wav-min-ms") {
        const file = join(tmp, rest[0]);
        if (!existsSync(file) || statSync(file).size <= 44) throw new Error(`${rest[0]} was not written`);
        const ms = wavMs(file);
        if (ms < Number(rest[1])) throw new Error(`${rest[0]} holds ${ms.toFixed(0)} ms, want >= ${rest[1]}`);
      } else if (verb === "no-event") {
        const hit = log.events.find((ev) => ev.event === rest[0] && matches(ev, rest.slice(1)));
        if (hit) throw new Error(`unexpected ${JSON.stringify(hit)}`);
      } else {
        throw new Error(`unknown check ${verb}`);
      }
    }
    console.log(`PASS ${s.name} (${((Date.now() - t0) / 1000).toFixed(1)} s)`);
    return true;
  } catch (e) {
    console.log(`FAIL ${s.name}: ${(e as Error).message}`);
    console.log("--- gadget-sim stdout ---\n" + simOut.slice(-30).join("\n"));
    console.log("--- gadget-sim stderr ---\n" + simErr.slice(-40).join("\n"));
    console.log("--- fake host events ---\n" + hostOut.slice(-40).map((l) => l.slice(0, 300)).join("\n"));
    console.log("--- fake host stderr ---\n" + hostErr.slice(-20).join("\n"));
    return false;
  } finally {
    gadget?.kill("SIGKILL");
    if (host && host.exitCode === null) {
      host.stdin?.write(JSON.stringify({ cmd: "quit" }) + "\n");
      setTimeout(() => host!.kill("SIGKILL"), 2000).unref();
    }
    rmSync(tmp, { recursive: true, force: true });
  }
}

async function main(): Promise<void> {
  const argv = process.argv.slice(2);
  let sim = join(REPO, "build/host/ports/sim/gadget-sim");
  let fakeHost = join(REPO, "tools/fake-host/src/main.ts");
  const files: string[] = [];
  for (let i = 0; i < argv.length; i++) {
    if (argv[i] === "--sim") sim = resolve(argv[++i]);
    else if (argv[i] === "--fake-host") fakeHost = resolve(argv[++i]);
    else if (argv[i] === "--all") {
      for (const f of readdirSync(HERE).filter((f) => f.endsWith(".txt")).sort()) files.push(join(HERE, f));
    } else files.push(resolve(argv[i]));
  }
  if (files.length === 0) {
    console.error("usage: run.ts [--sim <gadget-sim>] [--fake-host <main.ts>] (--all | <scenario.txt>...)");
    process.exit(2);
  }
  if (!existsSync(sim)) {
    console.error(`no simulator at ${sim}; build it first: cmake -S firmware -B build/host -DGADGET_WITH_LVGL=OFF && cmake --build build/host -j10`);
    process.exit(2);
  }
  let failed = 0;
  for (const f of files) {
    if (!(await runScenario(f, sim, fakeHost))) failed++;
  }
  console.log(`${files.length - failed}/${files.length} scenarios passed`);
  process.exit(failed === 0 ? 0 : 1);
}

await main();
