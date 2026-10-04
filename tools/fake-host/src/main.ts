// SPDX-License-Identifier: Apache-2.0
// node tools/fake-host/src/main.ts [options]   (see tools/fake-host/README.md)
// Events go to stdout as JSON lines, logs to stderr; commands are read from stdin. The process
// exits 0 after `quit`, when stdin ends, or on SIGINT/SIGTERM; 2 on bad options; 1 if it cannot start.
import { formatEvent, runControl } from "./control.ts";
import { parseCli, type FakeHostOptions } from "./options.ts";
import { startFakeHost, type FakeHost } from "./server.ts";

const USAGE = "usage: node tools/fake-host/src/main.ts [--port n] [--bind addr] [--code 6digits] [--code-ttl s] [--state dir]\n"
  + "  [--host-id 32hex] [--host-name text] [--bot id:name] [--heard text] [--reply text] [--tone-ms n]\n"
  + "  [--ota-key file] [--ota-key-id id] [--max-devices n] [--done-before-speech] [--quiet]\n";

let options: FakeHostOptions;
try {
  options = parseCli(process.argv.slice(2));
} catch (err) {
  process.stderr.write(`fake-host: ${(err as Error).message}\n${USAGE}`);
  process.exit(2);
}

const write = (event: Parameters<typeof formatEvent>[0]): void => {
  process.stdout.write(formatEvent(event));
};

let host: FakeHost;
try {
  host = await startFakeHost(options, { listener: write });
} catch (err) {
  process.stderr.write(`fake-host: cannot start: ${(err as Error).message}\n`);
  process.exit(1);
}

let stopping = false;
async function stop(): Promise<never> {
  if (!stopping) {
    stopping = true;
    await host.close();
  }
  process.exit(0);
}
process.on("SIGINT", () => void stop());
process.on("SIGTERM", () => void stop());

await runControl(host, process.stdin, write);
await stop();
