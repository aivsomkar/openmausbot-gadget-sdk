# Fake host

A Node stand-in for MausBot that speaks the host side of [`openmausbot-gadget/1`](../../protocol/PROTOCOL.md). It is the server for the simulator's end-to-end tests and for gadget work without MausBot.

```
npm ci
node tools/fake-host/src/main.ts --port 8810 --code 123456
```

It prints one six-digit pairing code and enrolls only with that code (120 s, 5 attempts, single use, like MausBot). Any other code gets `bad_code`. The simulator uses `--host 127.0.0.1:8810 --pair 123456`. For a real board, start the fake host with `--bind 0.0.0.0` and give the board `host <this computer's LAN address>:8810` and `pair 123456` on its console. The fake host does not advertise mDNS, so `host auto` will not find it.

Commands are read from stdin when it is a pipe or file. From a terminal or in the background (`… &`) it leaves stdin alone and runs until Ctrl-C/SIGTERM, so a script can start it with `&` and stop it with `kill`.

## What it does

- Refuses upgrades with an `Origin` header (403) or without the `openmausbot-gadget.1` subprotocol (400), declines every extension, pings every 15 s and drops a gadget after 45 s of silence.
- Runs the §4.3 handshake with Node's `crypto` (high-S signatures verify): enrollment with the one code, known gadgets without a code, `enroll_required`, `bad_code`, `bad_sig`, `proto_unsupported`, `device_limit` (with `--max-devices`) and `replaced`.
- Answers a voice turn or a `say` with `heard` (voice only), `working "checking your calendar"`, three cumulative `reply` frames 250 ms apart, the final `reply`, then a 440 Hz test tone as speech (40 ms frames, paced to real time, at most 0.5 s ahead), then `done ok` once the tone has played out. With `--done-before-speech` it sends `done ok` right after the final `reply` and the tone follows, which is the order MausBot usually produces; a new turn then gets `speak.stop` for that tone before its first message. An empty `--heard` gives `done failed "Didn't catch that"`; a mic rate other than 16000 gives `done failed "Unsupported mic rate"`; `stop`, `voice.drop` or a new turn stop the old one with `speak.stop` and `done stopped`.
- Folds every screen string to Latin-1 (plus `…` and `→`). It does not shape Markdown: its scripted texts are plain.
- Sends asks one at a time (and again on reconnect), posts, cards, images, actions and settings on command, and runs a full firmware update signed with the test key `t1`.

## Options

| Option | Default | Meaning |
|---|---|---|
| `--port <n>` | 8810 | 0 = any free port (reported in `listening`) |
| `--bind <addr>` | 127.0.0.1 | 0.0.0.0 to serve a real board on the LAN |
| `--code <6 digits>` | random | the one valid pairing code |
| `--code-ttl <s>` | 120 | how long the code is valid |
| `--state <dir>` | memory only | keeps `host_id` and enrolled gadgets in `<dir>/fake-host.json` |
| `--host-id <32 hex>` | random | |
| `--host-name <text>` | `Fake MausBot` | sent in `challenge` |
| `--bot <id>:<name>` | `b_fake:Fake Bot` | the bot in `ready`; `--bot :` makes gadgets unbound |
| `--heard <text>` | `What's on my calendar today?` | the speech-to-text result; `""` → "Didn't catch that" |
| `--reply <text>` | `You have two meetings today: design review at 10 and lunch with Sam at 1.` | |
| `--tone-ms <n>` | 800 | test tone length; 0 = no speech |
| `--ota-key <file>` / `--ota-key-id <id>` | `keys/test-t1.key.hex` / `t1` | OTA signing key |
| `--max-devices <n>` | 20 | enrolled-gadget cap (`device_limit`) |
| `--done-before-speech` | off | send `done ok` before the speech instead of after it (MausBot's usual order, PROTOCOL.md §4.4) |
| `--quiet` | off | no logs on stderr |

## Control: JSON lines

Commands go to stdin, one JSON object per line, when stdin is a pipe or file; from a terminal or in the background it runs until Ctrl-C/SIGTERM (to type commands by hand, start it as `cat | node tools/fake-host/src/main.ts …`). Events come out on stdout, one JSON object per line; logs go to stderr. Every command may name `"gadget": "<id>"` (default: the most recently ready gadget) and gets exactly one `{"event": "ack", "cmd": …, "ok": true}` or `{"event": "ack", "cmd": …, "ok": false, "error": "…"}`. A command whose frame is over the 16 KiB text limit, or whose gadget is already closing, gets `ok: false` and sends nothing (an `ask` acked with `queued: true` is sent once the asks before it close; `settings` for an offline gadget arrive with its next `ready`). A line that is not a command gets an ack with `"cmd": null`. With a stdin pipe, the process exits 0 after `quit` or when stdin ends; it always exits 0 on SIGINT/SIGTERM.

| Command | Fields | Effect |
|---|---|---|
| `code` | `code?` | open a new pairing window; emits `code` |
| `ask` | `id?`, `kind`, `title`, `body?`, `options?` (permission: omitted or exactly Allow/Deny; question: at most 4), `expires_s?` | queue an ask; a permission ask always carries Allow and Deny. The ack has `id` and `queued` |
| `ask.close` | `id`, `reason?` (default `withdrawn`) | close the open ask, or drop a queued one |
| `post` | `kind` (`routine` or `message`), `text`, `speak?` (default: the gadget's setting) | send `post`; speech follows when `speak` and a speaker exist, once any earlier speech has played out |
| `card` / `card.close` | `id?`, `title`, `body?`, `ttl_s?` (default 30) / `id` | |
| `image` | `id?`, `w`, `h`, `ttl_s?`, `pattern?` (`bars` or `#rrggbb`) | `image.begin`, RGB565 rows, `image.end` |
| `act` | `id?`, `name`, `args?` | send `act`; emits `act.result` when it arrives, or `{"timeout": true}` after 15 s |
| `settings` | `bot?` (`{id, name}`), `speak_pushes?`, `name?` | send `settings`; a rename while offline is sent after the next `ready` |
| `heard` / `reply` | `text` | change the scripted speech-to-text result / reply |
| `ota` | `image` (path), `version`, `board?`, `tamper?` | the full §4.8 flow with a 64 KiB window. `tamper`: `sig` (gadget answers `bad_sig`), `sha256` (`checksum` at commit), `size` (`too_large`) |
| `revoke` | | forget the gadget and send `error revoked` if it is connected |
| `replace` | | send `error replaced` and close |
| `drop` | | destroy the socket without a close frame |
| `close` | `code?` | close frame: 1000 (the default) or 3000–4999 |
| `quit` | | close every gadget (1001) and exit 0 |

| Event | Fields |
|---|---|
| `listening` | `port`, `host_id` |
| `code` | `code`, `expires_at` (epoch ms) |
| `connected` / `closed` | `remote` / `gadget`, `code` |
| `rx` / `tx` | `gadget` (null before `prove`), `msg` (the text frame) |
| `rx_binary` | `gadget`, `kind`, `stream`, `bytes` (payload length) |
| `enrolled` / `ready` / `refused` | `gadget` / `gadget`, `session` / `gadget`, `code`, `message` |
| `turn` | `gadget`, `turn`, `phase` (`started`, `heard`, `reply`, `speech`, `done`), `outcome?` |
| `answer` | `gadget`, `id`, `option` |
| `act.result` | `gadget`, `id`, `ok`, `data?`, `error?` (or `timeout: true`) |
| `ota` | `gadget`, `phase` (`offered`, `ready`, `progress`, `committed`, `installed`, `failed`), `offset?`, `size?`, `version?`, `code?` |
| `ack` | `cmd`, `ok`, `error?`, plus `id` and other details for some commands |

Example session:

```
$ cat | node tools/fake-host/src/main.ts --port 8810 --code 123456 --quiet
{"event":"listening","port":8810,"host_id":"…"}
{"event":"code","code":"123456","expires_at":1791131434536}
{"event":"connected","remote":"127.0.0.1:53122"}
…
{"event":"ready","gadget":"gad_b18b86ce1389e46d","session":"s_0c1d2e3f4a5b"}
{"cmd":"ask","kind":"permission","title":"Run the backup?","body":"It takes about a minute."}
{"event":"tx","gadget":"gad_b18b86ce1389e46d","msg":"{\"op\":\"ask\",…}"}
{"event":"ack","cmd":"ask","ok":true,"id":"a_5f0e1d2c3b4a","queued":false}
```

`ota` events with `failed` carry the gadget's `fw.fail` code, or the host's own: `ready_timeout` (no `fw.ready` within 10 s), `progress_timeout`, `disconnected`.
