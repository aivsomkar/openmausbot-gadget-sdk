// SPDX-License-Identifier: Apache-2.0
// Page wiring for the installer. The logic lives in the tested modules; this
// file only connects them to the DOM and to Web Serial.
import { ESPLoader, Transport } from "esptool-js";
import SparkMD5 from "spark-md5";
import { BOARD_CHOICES } from "./boards.ts";
import { normalizeHostAddress, normalizePairCode, setupCommands, wifiProblem } from "./console.ts";
import { COPY, pairingFailure } from "./copy.ts";
import { InstallerError } from "./errors.ts";
import { flashBoard } from "./flash.ts";
import { fetchPart, loadInstallIndex, type InstallIndex } from "./install.ts";
import { releaseLine, supportsWebSerial } from "./page.ts";
import { USB_JTAG, openConsolePort, resetToApp, type SerialLike, type SerialPortLike } from "./reset.ts";
import { SerialConsole } from "./serial-console.ts";
import { classifyStatus, pairAndWait, realClock, scanNetworks, waitForApp, waitForPaired, type PairingResult } from "./setup.ts";

const el = <T extends HTMLElement = HTMLElement>(id: string): T => {
  const node = document.getElementById(id);
  if (node === null) throw new Error(`missing #${id}`);
  return node as T;
};
const show = (id: string, visible: boolean): void => {
  el(id).hidden = !visible;
};
const text = (id: string, value: string): void => {
  el(id).textContent = value;
};

let index: InstallIndex | null = null;
let board: string | null = null;
let con: SerialConsole | null = null;
let wifi = { ssid: "", password: "" };
/** The address typed after need_host. The next code reuses it; "Change Wi-Fi or pair again" forgets it. */
let pinnedHost: string | undefined;
let busy = false;
let pairing = false;

function log(line: string): void {
  const pre = el<HTMLPreElement>("log");
  pre.textContent = `${pre.textContent ?? ""}${line}\n`.slice(-20_000);
}

function friendly(err: unknown): string {
  if (err instanceof InstallerError) {
    switch (err.code) {
      case "port_busy":
        return `${err.message} Close it (a serial monitor or another tab) and try again.`;
      case "port_lost":
        return `${err.message} ${COPY.pressRst}.`;
      case "write_failed":
        return `${err.message} Click Connect and install to try again.`;
      case "flash_too_small":
        return `${err.message} Use an ESP32-S3-DevKitC-1-N16R8 or one of the Waveshare boards.`;
      default:
        return err.message;
    }
  }
  if (err instanceof DOMException && err.name === "NotFoundError") return COPY.noPortHint;
  return err instanceof Error ? err.message : String(err);
}

function step(name: "wifi" | "pair" | "done" | null): void {
  show("step-wifi", name === "wifi");
  show("step-pair", name === "pair");
  show("step-done", name === "done");
}

function setBusy(value: boolean): void {
  busy = value;
  el<HTMLButtonElement>("install").disabled = value || board === null || index?.boards[board] === undefined;
  el<HTMLButtonElement>("installed").disabled = value || board === null;
}

function setPairing(value: boolean): void {
  pairing = value;
  el<HTMLButtonElement>("code-submit").disabled = value;
  el<HTMLButtonElement>("host-submit").disabled = value;
}

function renderBoards(): void {
  const box = el("boards");
  box.replaceChildren();
  for (const b of BOARD_CHOICES) {
    const published = index?.boards[b.id] !== undefined;
    const label = document.createElement("label");
    label.className = published ? "board" : "board unpublished";
    const input = document.createElement("input");
    input.type = "radio";
    input.name = "board";
    input.value = b.id;
    input.addEventListener("change", () => {
      board = b.id;
      show("devkit-hint", b.id === "devkit");
      text("install-status", published ? "" : COPY.boardNotPublished);
      setBusy(busy);
    });
    const name = document.createElement("strong");
    name.textContent = b.name;
    const detail = document.createElement("span");
    detail.textContent = published ? b.detail : `${b.detail} ${COPY.boardNotPublished}`;
    label.append(input, name, detail);
    box.append(label);
  }
}

/** Web Serial refuses a second open() on a port that is already open, so every flow that opens the port closes our console first. */
async function closeConsole(): Promise<void> {
  const open = con;
  con = null;
  await open?.close();
}

async function install(): Promise<void> {
  if (index === null || board === null) return;
  setBusy(true);
  step(null);
  const progress = el<HTMLProgressElement>("flash-progress");
  let transport: Transport | null = null;
  try {
    await closeConsole();
    const port = await navigator.serial.requestPort({ filters: [{ ...USB_JTAG }] });
    transport = new Transport(port, false);
    const loader = new ESPLoader({
      transport,
      baudrate: 921600,
      romBaudrate: 115200,
      terminal: { clean: () => undefined, write: (s) => log(s), writeLine: (s) => log(s) },
    });
    progress.hidden = false;
    await flashBoard({
      loader,
      index,
      board,
      eraseAll: el<HTMLInputElement>("erase-all").checked,
      fetchPart: (path) => fetchPart(fetch, path),
      md5: (data) => SparkMD5.ArrayBuffer.hash(data.slice().buffer),
      onStage: (s) => text("install-status", COPY.flashStages[s]),
      onProgress: (f) => {
        progress.value = f;
      },
      onWarning: (message) => log(message),
    });
    text("install-status", COPY.waitingForBoard);
    const flashed = transport;
    transport = null; // resetToApp disconnects it
    const consolePort = await resetToApp({ loader, transport: flashed, port, serial: navigator.serial as SerialLike, sleep: realClock.sleep });
    await afterReset(consolePort);
  } catch (err) {
    // A failed flash leaves esptool-js holding the port; release it so "Connect and install" works again.
    if (transport !== null) await transport.disconnect().catch(() => undefined);
    text("install-status", friendly(err));
  } finally {
    progress.hidden = true;
    setBusy(false);
  }
}

async function setUpInstalled(): Promise<void> {
  setBusy(true);
  step(null);
  try {
    await closeConsole();
    const port = await navigator.serial.requestPort({ filters: [{ ...USB_JTAG }] });
    text("install-status", COPY.waitingForBoard);
    await afterReset(await openConsolePort(port, navigator.serial as SerialLike, realClock.sleep, 2000));
  } catch (err) {
    text("install-status", friendly(err));
  } finally {
    setBusy(false);
  }
}

/** Find the app on the console, then either finish (still paired) or start Wi-Fi setup. */
async function afterReset(port: SerialPortLike): Promise<void> {
  const serial = navigator.serial as SerialLike;
  let current = port;
  // The other boards, taken while `current` is open: a reopen never falls back to one of them, and the
  // board itself may already be listed under a new port when the reopen starts (Deviations, item 1).
  let others = (await serial.getPorts()).filter((p) => p !== current);
  let first = true;
  const { io, status } = await waitForApp(
    async () => {
      // The first console uses the port that is already open; later ones re-acquire it after RST or a replug.
      if (!first) {
        current = await openConsolePort(current, serial, realClock.sleep, 120_000, others);
        others = (await serial.getPorts()).filter((p) => p !== current);
      }
      first = false;
      const session = new SerialConsole(current);
      session.onEvent = (e) => log(e.line);
      con = session;
      return session;
    },
    realClock,
    (prompt) => text("install-status", prompt === "press_rst" ? `${COPY.pressRst}.` : COPY.waitingForBoard),
  );
  const state = classifyStatus(status);
  if (state === "paired") return done(COPY.stillPaired(status?.host_name));
  if (state === "reconnecting") {
    text("install-status", COPY.reconnecting);
    const again = await waitForPaired(io, realClock);
    if (again !== null) return done(COPY.stillPaired(again.host_name));
  }
  text("install-status", "");
  return startWifi();
}

/** The Wi-Fi step. `notice` explains why the page came back here (a wrong password). */
async function startWifi(notice?: string): Promise<void> {
  if (con === null) return;
  step("wifi");
  const list = el("networks");
  list.replaceChildren();
  show("wifi-error", false);
  if (notice !== undefined) {
    text("wifi-error", notice);
    show("wifi-error", true);
  }
  try {
    const networks = await scanNetworks(con, realClock);
    for (const n of networks) {
      const item = document.createElement("li");
      const button = document.createElement("button");
      button.type = "button";
      button.className = "network";
      button.textContent = `${n.ssid}${n.auth === "open" ? "" : " 🔒"}  ${n.rssi} dBm`;
      button.addEventListener("click", () => {
        el<HTMLInputElement>("ssid").value = n.ssid;
        el<HTMLInputElement>("password").focus();
      });
      item.append(button);
      list.append(item);
    }
  } catch (err) {
    text("wifi-error", `${notice === undefined ? "" : `${notice} `}${friendly(err)} Type the network name instead.`);
    show("wifi-error", true);
  }
}

function onWifiSubmit(event: SubmitEvent): void {
  event.preventDefault();
  const ssid = el<HTMLInputElement>("ssid").value;
  const password = el<HTMLInputElement>("password").value;
  const problem = wifiProblem(ssid, password);
  show("wifi-error", problem !== null);
  if (problem !== null) return text("wifi-error", problem);
  wifi = { ssid, password };
  step("pair");
  show("host-form", false);
  el<HTMLInputElement>("code").focus();
}

/** One pairing at a time: a second polling loop on the same console would steal half of its lines. */
async function runPairing(commands: string[]): Promise<void> {
  if (con === null || pairing) return;
  setPairing(true);
  text("pair-status", "Pairing…");
  try {
    handlePairing(await pairAndWait(con, commands, realClock, { onNotice: () => text("pair-status", COPY.deviceLimitWaiting) }));
  } finally {
    setPairing(false);
  }
}

async function onCodeSubmit(event: SubmitEvent): Promise<void> {
  event.preventDefault();
  if (pairing) return;
  const code = normalizePairCode(el<HTMLInputElement>("code").value);
  if (code === null) return text("pair-status", "The code is six digits.");
  // A typed address stays pinned: `host auto` would erase the gadget's stored host_addr (contract §2.11).
  await runPairing(setupCommands({ code, ...wifi, address: pinnedHost }));
}

async function onHostSubmit(event: SubmitEvent): Promise<void> {
  event.preventDefault();
  if (pairing) return;
  const address = normalizeHostAddress(el<HTMLInputElement>("host-address").value);
  if (address === null) return text("pair-status", "Type an address like 192.168.1.20:8810.");
  pinnedHost = address;
  await runPairing([`host ${address}`]);
}

function handlePairing(result: PairingResult): void {
  if (result.kind === "paired") return done(COPY.paired(result.status.host_name));
  if (result.kind === "need_host") {
    show("host-form", true);
    text("host-help", result.hosts.length === 0 ? COPY.hostNone : COPY.hostPick);
    const list = el("hosts");
    list.replaceChildren();
    for (const h of result.hosts) {
      const item = document.createElement("li");
      const button = document.createElement("button");
      button.type = "button";
      button.textContent = `${h.name} (${h.address})`;
      button.addEventListener("click", () => {
        el<HTMLInputElement>("host-address").value = h.address;
      });
      item.append(button);
      list.append(item);
    }
    text("pair-status", "");
    return;
  }
  // The Wi-Fi step hides #step-pair, so its reason goes to the Wi-Fi step's own message.
  if (result.kind === "wifi_failed") return void startWifi(pairingFailure(result, wifi.ssid));
  text("pair-status", pairingFailure(result, wifi.ssid));
}

function done(message: string): void {
  step("done");
  text("done-text", message);
  text("install-status", "");
}

async function main(): Promise<void> {
  text("erase-label", COPY.eraseLabel);
  text("erase-help", COPY.keepHelp);
  text("installed", COPY.installedPath);
  text("port-hint", COPY.noPortHint);
  text("devkit-hint", COPY.devkitPort);
  text("code-how", COPY.codeHow);
  text("remote-on", COPY.remoteAccessOn);
  text("windows-public", COPY.windowsPublic);
  el<HTMLInputElement>("erase-all").addEventListener("change", (e) => {
    text("erase-help", (e.target as HTMLInputElement).checked ? COPY.eraseHelp : COPY.keepHelp);
  });
  el("install").addEventListener("click", () => void install());
  el("installed").addEventListener("click", () => void setUpInstalled());
  el<HTMLFormElement>("wifi-form").addEventListener("submit", onWifiSubmit);
  el<HTMLFormElement>("code-form").addEventListener("submit", (e) => void onCodeSubmit(e));
  el<HTMLFormElement>("host-form").addEventListener("submit", (e) => void onHostSubmit(e));
  el("setup-again").addEventListener("click", () => {
    pinnedHost = undefined;
    void startWifi();
  });

  try {
    index = await loadInstallIndex(fetch);
    text("release", releaseLine(index));
  } catch {
    text("release", COPY.brokenIndex);
  }
  renderBoards();
  const supported = supportsWebSerial(navigator);
  if (!supported) {
    text("unsupported", COPY.needSerial);
    show("unsupported", true);
  }
  setBusy(!supported);
}

void main();
