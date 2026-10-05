// SPDX-License-Identifier: Apache-2.0
import assert from "node:assert/strict";
import { test } from "node:test";
import { setupCommands } from "../src/console.ts";
import { classifyStatus, detectApp, pairAndWait, scanNetworks, waitForApp, waitForPaired, type PairingNotice, type StatusMsg, type WaitPrompt } from "../src/setup.ts";
import { FakeGadget } from "./fakes-console.ts";
import { fakeClock } from "./fakes-serial.ts";

const cmds = setupCommands({ code: "123456", ssid: "Home", password: "hunter22" });

/** Answers like a gadget that joins Wi-Fi after 2 polls and pairs after 4. */
function happyGadget(): FakeGadget {
  let polls = 0;
  return new FakeGadget({
    onCommand: (line, g) => {
      if (line.startsWith("pair ")) g.set({ pair: "code_stored" });
      if (line.startsWith("wifi ")) g.set({ wifi: "connecting", ssid: "Home" });
      if (line === "status") {
        polls++;
        if (polls === 2) g.set({ wifi: "connected", host: "192.168.1.20:8810", pair: "connecting" });
        if (polls === 4) g.set({ pair: "paired", host_name: "Omkar's computer" });
      }
    },
  });
}

test("pairAndWait sends pair, wifi, host auto in that order and returns once paired", async () => {
  const g = happyGadget();
  const r = await pairAndWait(g, cmds, fakeClock());
  assert.equal(r.kind, "paired");
  assert.equal(r.kind === "paired" && r.status.host_name, "Omkar's computer");
  assert.deepEqual(g.sent.slice(0, 3), ["pair 123456", 'wifi "Home" "hunter22"', "host auto"]);
});

test("a wrong code ends with pair_error bad_code", async () => {
  const g = new FakeGadget({
    onCommand: (line, gg) => {
      if (line === "host auto") gg.set({ wifi: "connected", pair: "error", error: "bad_code" });
    },
  });
  const r = await pairAndWait(g, cmds, fakeClock());
  assert.deepEqual(r.kind === "pair_error" && r.code, "bad_code");
});

test("a wrong Wi-Fi password ends with wifi_failed", async () => {
  const g = new FakeGadget({ onCommand: (line, gg) => line.startsWith("wifi ") && gg.set({ wifi: "failed", ssid: "Home" }) });
  assert.equal((await pairAndWait(g, cmds, fakeClock())).kind, "wifi_failed");
});

test("a retry after bad_code ignores the stale error status", async () => {
  // Left over from the wrong code: Wi-Fi is up, pair is still "error" until the new pair is applied.
  let polls = 0;
  const g = new FakeGadget({
    status: { wifi: "connected", ssid: "Home", host: "192.168.1.20:8810", pair: "error", error: "bad_code" },
    onCommand: (line, gg) => line === "status" && ++polls === 3 && gg.set({ pair: "paired", error: undefined, host_name: "Mac" }),
  });
  const r = await pairAndWait(g, cmds, fakeClock());
  assert.equal(r.kind, "paired");
});

test("a retry after wifi failed ignores the stale failure", async () => {
  let polls = 0;
  const g = new FakeGadget({
    status: { wifi: "failed", ssid: "Home", pair: "code_stored" },
    onCommand: (line, gg) => {
      if (line !== "status") return;
      polls++;
      if (polls === 3) gg.set({ wifi: "connected", pair: "connecting" });
      if (polls === 4) gg.set({ pair: "paired" });
    },
  });
  assert.equal((await pairAndWait(g, cmds, fakeClock())).kind, "paired");
});

test("device_limit keeps polling with one notice; it is a result only when time runs out", async () => {
  let polls = 0;
  const notices: PairingNotice[] = [];
  const g = new FakeGadget({
    onCommand: (line, gg) => {
      if (line === "host auto") gg.set({ wifi: "connected", pair: "error", error: "device_limit" });
      if (line === "status" && ++polls === 4) gg.set({ pair: "paired", error: undefined }); // a device was removed
    },
  });
  const r = await pairAndWait(g, cmds, fakeClock(), { onNotice: (n) => notices.push(n) });
  assert.equal(r.kind, "paired");
  assert.deepEqual(notices, ["device_limit"]);
  const stuck = new FakeGadget({ status: { wifi: "connected", pair: "error", error: "device_limit" } });
  assert.equal((await pairAndWait(stuck, cmds, fakeClock(), { timeoutMs: 5000 })).kind, "device_limit");
});

test("device_limit that the gadget clears after its 120 s window ends as device_limit, before the timeout", async () => {
  // Spec §4.3 and contract §2.11 rule 2: 120 s after `pair` the gadget drops the code and `status` shows `unpaired`.
  const clock = fakeClock();
  const notices: PairingNotice[] = [];
  let start = 0;
  const g = new FakeGadget({
    onCommand: (line, gg) => {
      if (line.startsWith("pair ")) {
        start = clock.now();
        gg.set({ pair: "code_stored" });
      }
      if (line === "host auto") gg.set({ wifi: "connected", pair: "error", error: "device_limit" });
      if (line === "status" && clock.now() - start >= 120_000) gg.set({ pair: "unpaired", error: undefined });
    },
  });
  const r = await pairAndWait(g, cmds, clock, { onNotice: (n) => notices.push(n) });
  assert.equal(r.kind, "device_limit");
  assert.equal(r.kind === "device_limit" && r.status.pair, "unpaired");
  assert.ok(clock.t < 150_000, `ended at ${clock.t} ms, after the 150 s timeout`);
  assert.deepEqual(notices, ["device_limit"]);
});

test("no MausBot found asks for an address; several ask the person to pick", async () => {
  const none = new FakeGadget({ onCommand: (line, g) => line === "host auto" && g.emit('@omb {"op":"hosts","hosts":[]}') });
  const r1 = await pairAndWait(none, cmds, fakeClock());
  assert.deepEqual(r1.kind === "need_host" && r1.hosts, []);
  const two = new FakeGadget({
    onCommand: (line, g) =>
      line === "host auto" &&
      g.emit('@omb {"op":"hosts","hosts":[{"name":"A","address":"10.0.0.2:8810","id":""},{"name":"B","address":"10.0.0.3:8810","id":""}]}'),
  });
  const r2 = await pairAndWait(two, cmds, fakeClock());
  assert.equal(r2.kind === "need_host" && r2.hosts.length, 2);
});

test("a single listed MausBot is chosen automatically", async () => {
  const g = new FakeGadget({
    onCommand: (line, gg) => {
      if (line === "host auto") gg.emit('@omb {"op":"hosts","hosts":[{"name":"A","address":"10.0.0.2:8810","id":""}]}');
      if (line === "host 10.0.0.2:8810") gg.set({ wifi: "connected", pair: "paired" });
    },
  });
  assert.equal((await pairAndWait(g, cmds, fakeClock())).kind, "paired");
  assert.ok(g.sent.includes("host 10.0.0.2:8810"));
});

test("a console error for one of the setup commands is reported", async () => {
  const g = new FakeGadget({
    onCommand: (line, gg) => line.startsWith("wifi ") && gg.emit('@omb {"op":"error","cmd":"wifi","message":"password must be 8-63 bytes"}'),
  });
  const r = await pairAndWait(g, cmds, fakeClock());
  assert.deepEqual(r.kind === "command_error" && [r.cmd, r.message], ["wifi", "password must be 8-63 bytes"]);
});

test("pairing gives up after the timeout with the last status", async () => {
  const g = new FakeGadget({ status: { wifi: "connected", pair: "connecting" } });
  const clock = fakeClock();
  const r = await pairAndWait(g, cmds, clock, { timeoutMs: 10_000 });
  assert.equal(r.kind, "timeout");
  assert.equal(r.kind === "timeout" && r.status?.pair, "connecting");
  assert.ok(clock.t >= 10_000 && clock.t <= 12_000);
});

test("an unplugged board ends pairing with lost", async () => {
  const g = new FakeGadget();
  g.lost = true;
  assert.equal((await pairAndWait(g, cmds, fakeClock())).kind, "lost");
});

test("detectApp: app, silent board, download mode, boot line only", async () => {
  assert.equal((await detectApp(new FakeGadget(), fakeClock())).kind, "app");
  const silent = new FakeGadget();
  silent.send = async () => undefined;
  assert.equal((await detectApp(silent, fakeClock())).kind, "silent");
  const rom = new FakeGadget();
  rom.send = async () => undefined;
  rom.emit("rst:0x15 (USB_UART_CHIP_RESET),boot:0x0 (DOWNLOAD(USB/UART0))");
  rom.emit("waiting for download");
  assert.equal((await detectApp(rom, fakeClock())).kind, "download_mode");
  const booted = new FakeGadget();
  booted.send = async () => undefined;
  booted.emit('@omb {"op":"boot","board":"lcd-154","fw":"1.0.0","id":"gad_3f9a0c2b7e41d856"}');
  const r = await detectApp(booted, fakeClock());
  assert.deepEqual(r, { kind: "app", status: null });
});

test("a silent board keeps the RST prompt up and is not reopened", async () => {
  // Spec §5.8 step 3: ~5 s without the app → "Press RST…". The board stays plugged in and silent for 20 s.
  const clock = fakeClock();
  const g = new FakeGadget();
  const answer = g.send.bind(g);
  g.send = async (line) => {
    if (clock.t >= 20_000) await answer(line);
  };
  let opens = 0;
  const prompts: WaitPrompt[] = [];
  const r = await waitForApp(
    async () => {
      opens++;
      return g;
    },
    clock,
    (p) => prompts.push(p),
  );
  assert.equal(r.status?.op, "status");
  assert.deepEqual(prompts, ["waiting", "press_rst"]); // the last prompt before the app answered is the RST prompt
  assert.equal(opens, 1);
  assert.equal(g.closed, false);
});

test("a board that drops off USB is reopened, and the app is found on the new console", async () => {
  const gone = new FakeGadget();
  gone.lost = true;
  const back = new FakeGadget();
  const sessions = [gone, back];
  const prompts: WaitPrompt[] = [];
  const r = await waitForApp(async () => sessions.shift() as FakeGadget, fakeClock(), (p) => prompts.push(p));
  assert.equal(r.io, back);
  assert.equal(gone.closed, true);
  assert.deepEqual(prompts, ["waiting", "press_rst"]);
});

test("a reflashed gadget that kept its pairing is recognised", async () => {
  const base = { op: "status", id: "gad_x", fw: "1.1.0" } as const;
  assert.equal(classifyStatus({ ...base, wifi: "connected", pair: "paired" } as StatusMsg), "paired");
  assert.equal(classifyStatus({ ...base, wifi: "connecting", ssid: "Home", pair: "connecting" } as StatusMsg), "reconnecting");
  assert.equal(classifyStatus({ ...base, wifi: "off", pair: "unpaired" } as StatusMsg), "needs_setup");
  assert.equal(classifyStatus(null), "needs_setup");
  let polls = 0;
  const g = new FakeGadget({
    status: { wifi: "connecting", ssid: "Home", pair: "connecting" },
    onCommand: (line, gg) => line === "status" && ++polls === 3 && gg.set({ pair: "paired", host_name: "Mac" }),
  });
  assert.equal((await waitForPaired(g, fakeClock()))?.host_name, "Mac");
  assert.equal(await waitForPaired(new FakeGadget(), fakeClock(), 3000), null);
});

test("scanNetworks silences the log, scans, and returns the gadget's list", async () => {
  const g = new FakeGadget({
    onCommand: (line, gg) => line === "scan" && gg.emit('@omb {"op":"scan","networks":[{"ssid":"Home","rssi":-50,"auth":"wpa2"}]}'),
  });
  assert.deepEqual(await scanNetworks(g, fakeClock()), [{ ssid: "Home", rssi: -50, auth: "wpa2" }]);
  assert.deepEqual(g.sent, ["log off", "scan"]);
  await assert.rejects(scanNetworks(new FakeGadget(), fakeClock(), 1000), /didn't list/);
});
