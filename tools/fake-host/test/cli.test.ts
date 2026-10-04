// SPDX-License-Identifier: Apache-2.0
import { test } from "node:test";
import assert from "node:assert/strict";
import { spawn, type ChildProcessWithoutNullStreams } from "node:child_process";
import { createInterface } from "node:readline";
import { fileURLToPath } from "node:url";
import { parseCommandLine, formatEvent } from "../src/control.ts";
import { connectGadget } from "./gadget-client.ts";

const MAIN = fileURLToPath(new URL("../src/main.ts", import.meta.url));

/** Spawns the CLI and collects its stdout events; `next` waits for the first unread match. */
function spawnHost(args: string[]): { child: ChildProcessWithoutNullStreams; next: (m: (e: any) => boolean) => Promise<any>; send: (line: string) => void; exit: Promise<number | null> } {
  const child = spawn(process.execPath, [MAIN, ...args], { stdio: ["pipe", "pipe", "pipe"] });
  const seen: any[] = [];
  const waiting: Array<{ m: (e: any) => boolean; resolve: (e: any) => void }> = [];
  createInterface({ input: child.stdout }).on("line", (line) => {
    const e = JSON.parse(line);
    const w = waiting.findIndex((x) => x.m(e));
    if (w >= 0) waiting.splice(w, 1)[0].resolve(e);
    else seen.push(e);
  });
  const exit = new Promise<number | null>((resolve) => child.on("exit", (code) => resolve(code)));
  return {
    child,
    exit,
    send: (line) => child.stdin.write(line + "\n"),
    next: (m) => {
      const i = seen.findIndex(m);
      if (i >= 0) return Promise.resolve(seen.splice(i, 1)[0]);
      return new Promise((resolve, reject) => {
        const entry = { m, resolve: (e: any) => { clearTimeout(t); resolve(e); } };
        const t = setTimeout(() => reject(new Error("timed out waiting for a CLI event")), 5000);
        waiting.push(entry);
      });
    },
  };
}

test("parseCommandLine accepts command objects only", () => {
  assert.equal(parseCommandLine("   "), null);
  assert.deepEqual(parseCommandLine('{"cmd":"post","kind":"message","text":"hi"}'), { ok: true, cmd: { cmd: "post", kind: "message", text: "hi" } });
  assert.deepEqual(parseCommandLine("nope"), { ok: false, error: "not JSON" });
  assert.deepEqual(parseCommandLine("[1]"), { ok: false, error: "not a JSON object" });
  assert.deepEqual(parseCommandLine('{"x":1}'), { ok: false, error: "missing cmd" });
  assert.deepEqual(parseCommandLine('{"cmd":"card","gadget":5}'), { ok: false, error: "gadget must be a string" });
  assert.equal(formatEvent({ event: "ack", cmd: "x", ok: true }), '{"event":"ack","cmd":"x","ok":true}\n');
});

test("the CLI prints listening and code, enrolls a gadget, runs stdin commands and quits with 0", async () => {
  const h = spawnHost(["--port", "0", "--code", "123456", "--quiet"]);
  const listening = await h.next((e) => e.event === "listening");
  assert.match(listening.host_id, /^[0-9a-f]{32}$/);
  assert.equal((await h.next((e) => e.event === "code")).code, "123456");
  const { gadget, result } = await connectGadget({ port: listening.port, enroll: "123456" });
  assert.equal(result.op, "ready");
  assert.equal((await h.next((e) => e.event === "enrolled")).gadget, gadget.id);
  assert.equal((await h.next((e) => e.event === "ready")).gadget, gadget.id);
  h.send('{"cmd":"card","title":"From stdin","body":"ok"}');
  assert.equal((await gadget.next("card")).title, "From stdin");
  assert.equal((await h.next((e) => e.event === "ack" && e.cmd === "card")).ok, true);
  h.send("garbage");
  assert.deepEqual(await h.next((e) => e.event === "ack" && e.cmd === null), { event: "ack", cmd: null, ok: false, error: "not JSON" });
  h.send('{"cmd":"quit"}');
  assert.equal((await h.next((e) => e.event === "ack" && e.cmd === "quit")).ok, true);
  assert.equal((await gadget.closed).code, 1001);
  assert.equal(await h.exit, 0);
});

test("the CLI exits 0 when stdin ends and 2 on bad options", async () => {
  const h = spawnHost(["--port", "0", "--quiet"]);
  assert.match(String((await h.next((e) => e.event === "code")).code), /^\d{6}$/);
  h.child.stdin.end();
  assert.equal(await h.exit, 0);
  const bad = spawn(process.execPath, [MAIN, "--code", "12"], { stdio: ["pipe", "pipe", "pipe"] });
  let stderr = "";
  bad.stderr.on("data", (d) => (stderr += d));
  assert.equal(await new Promise((r) => bad.on("exit", r)), 2);
  assert.match(stderr, /--code must be six digits/);
});

test("with no stdin pipe (a background job's /dev/null) the CLI keeps serving until SIGTERM, then exits 0", async () => {
  const child = spawn(process.execPath, [MAIN, "--port", "0", "--code", "123456", "--quiet"], { stdio: ["ignore", "pipe", "pipe"] });
  const exit = new Promise<number | null>((resolve) => child.on("exit", (code) => resolve(code)));
  const listening = await new Promise<any>((resolve, reject) => {
    const t = setTimeout(() => reject(new Error("timed out waiting for listening")), 5000);
    createInterface({ input: child.stdout }).on("line", (line) => {
      const e = JSON.parse(line);
      if (e.event === "listening") {
        clearTimeout(t);
        resolve(e);
      }
    });
  });
  await new Promise((r) => setTimeout(r, 500));
  assert.equal(child.exitCode, null, "still running after 500 ms");
  const { gadget, result } = await connectGadget({ port: listening.port, enroll: "123456" });
  assert.equal(result.op, "ready");
  child.kill("SIGTERM");
  assert.equal(await exit, 0);
  assert.equal((await gadget.closed).code, 1001);
});
