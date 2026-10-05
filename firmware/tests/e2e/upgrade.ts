// firmware/tests/e2e/upgrade.ts
// SPDX-License-Identifier: Apache-2.0
// The simulator's upgrade checks (RFC 6455 §4.1; spec §4.1 uses no
// extensions) against a raw TCP "host" that answers the upgrade with a 101
// response of each case's making. The client must fail a response without
// `Connection: upgrade` or with `Sec-WebSocket-Extensions`: it sends no frame
// and logs "upgrade refused". A good response gets the gadget's hello, a text
// frame.
//
//   node firmware/tests/e2e/upgrade.ts --sim <gadget-sim>
import { spawn } from "node:child_process";
import { createHash } from "node:crypto";
import { mkdtempSync, rmSync, writeFileSync } from "node:fs";
import { createServer } from "node:net";
import type { AddressInfo, Socket } from "node:net";
import { tmpdir } from "node:os";
import { join } from "node:path";

const GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
const CASES: { name: string; headers: string; accept: boolean }[] = [
  { name: "a good response", headers: "Upgrade: websocket\r\nConnection: Upgrade\r\n", accept: true },
  { name: "upgrade in a Connection list", headers: "Upgrade: websocket\r\nConnection: keep-alive, UPGRADE\r\n", accept: true },
  { name: "no Connection", headers: "Upgrade: websocket\r\n", accept: false },
  { name: "Connection without upgrade", headers: "Upgrade: websocket\r\nConnection: keep-alive\r\n", accept: false },
  {
    name: "an extension",
    headers: "Upgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Extensions: permessage-deflate\r\n",
    accept: false,
  },
];

type Outcome = { firstByte: number | null; refused: boolean; code: number | null };

function runCase(sim: string, tmp: string, i: number, headers: string): Promise<Outcome> {
  return new Promise((resolve, reject) => {
    let firstByte: number | null = null;
    let answered = false;
    const server = createServer((sock: Socket) => {
      if (answered) {
        sock.destroy(); /* only the first connection is answered */
        return;
      }
      answered = true;
      let req = Buffer.alloc(0);
      let upgraded = false;
      sock.on("error", () => {});
      sock.on("data", (d: Buffer) => {
        if (upgraded) {
          if (firstByte === null && d.length > 0) firstByte = d[0];
          return;
        }
        req = Buffer.concat([req, d]);
        const end = req.indexOf("\r\n\r\n");
        if (end < 0) return;
        const key = /^sec-websocket-key:\s*(\S+)/im.exec(req.subarray(0, end).toString("latin1"))?.[1] ?? "";
        const accept = createHash("sha1").update(key + GUID).digest("base64");
        upgraded = true;
        sock.write(
          "HTTP/1.1 101 Switching Protocols\r\n" + headers + `Sec-WebSocket-Accept: ${accept}\r\n` +
            "Sec-WebSocket-Protocol: openmausbot-gadget.1\r\n\r\n",
        );
        const rest = req.subarray(end + 4);
        if (rest.length > 0) firstByte = rest[0];
      });
    });
    server.listen(0, "127.0.0.1", () => {
      const port = (server.address() as AddressInfo).port;
      const script = join(tmp, "wait.txt");
      writeFileSync(script, "wait 1500\n");
      const gadget = spawn(sim, ["--board", "lcd-154", "--headless", "--host", `127.0.0.1:${port}`, "--pair", "123456",
        "--state-dir", join(tmp, `state-${i}`), "--script", script], { stdio: ["ignore", "ignore", "pipe"] });
      let err = "";
      gadget.stderr!.on("data", (d: Buffer) => (err += d.toString()));
      const timer = setTimeout(() => gadget.kill("SIGKILL"), 20000);
      gadget.on("error", reject);
      gadget.on("close", (code: number | null) => {
        clearTimeout(timer);
        server.close();
        resolve({ firstByte, refused: err.includes("upgrade refused"), code });
      });
    });
  });
}

async function main(): Promise<number> {
  const i = process.argv.indexOf("--sim");
  if (i < 0 || !process.argv[i + 1]) {
    console.error("usage: upgrade.ts --sim <gadget-sim>");
    return 2;
  }
  const sim = process.argv[i + 1];
  const tmp = mkdtempSync(join(tmpdir(), "gadget-e2e-upgrade-"));
  let failed = 0;
  try {
    for (const [n, c] of CASES.entries()) {
      const o = await runCase(sim, tmp, n, c.headers);
      const ok = o.code === 0 && (c.accept ? o.firstByte === 0x81 && !o.refused : o.firstByte === null && o.refused);
      const got = `exit ${o.code}, ${o.firstByte === null ? "no frame" : `first byte 0x${o.firstByte.toString(16)}`}` +
        (o.refused ? ", upgrade refused" : "");
      console.log(`${ok ? "ok  " : "FAIL"} ${c.name}: ${c.accept ? "accepted" : "refused"} expected; ${got}`);
      if (!ok) failed++;
    }
  } finally {
    rmSync(tmp, { recursive: true, force: true });
  }
  return failed === 0 ? 0 : 1;
}

process.exitCode = await main();
