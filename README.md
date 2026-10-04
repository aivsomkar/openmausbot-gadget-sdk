# OpenMausBot Gadget SDK

Turn a small ESP32 board with a screen, a microphone and a speaker into a desk terminal for your own MausBot: hold to talk, and the bot running on your Mac answers on the screen and out loud.

**Status:** in development. This repository will hold the firmware for four boards, a desktop simulator, a browser installer and the protocol. Today it has the protocol and its tools.

- Remote access must be on in MausBot: the gadget hub runs inside MausBot's companion, which only runs while Remote access is on.
- Pairing starts at **MausBot → Settings → Remote access → Pair a gadget**.

## What is here

| Path | What |
|---|---|
| [`protocol/PROTOCOL.md`](protocol/PROTOCOL.md) | The `openmausbot-gadget/1` protocol (normative) |
| `protocol/vectors/` | Test vectors every implementation must pass, with `SHA256SUMS` |
| `protocol/tools/gen-vectors.ts` | Regenerates the vectors |
| [`tools/fake-host/`](tools/fake-host/README.md) | A stand-in for MausBot that speaks the host side, for tests and offline work |
| `keys/test-t1.*` | The test signing key (never in release firmware) |

## Commands

Node 22.18 or newer (it runs the `.ts` files directly):

```
npm ci
npm test                 # protocol and fake-host tests
npm run vectors:check    # regenerate the vectors and fail on any difference
npm run fake-host -- --code 123456
```

## License

Apache-2.0, see [`LICENSE`](LICENSE). The OpenMausBot name, the MausBot name and the Maus mascot are trademarks of Supamaus Software Private Limited.
