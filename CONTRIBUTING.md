# Contributing

Thanks for helping. This file covers the rules every change follows, how to run the tests, and how to send a change.

## Original-work rule

Everything in this repository is written for this project. Contributors and coding agents must not copy code, documentation, art or protocol text from other gadget SDKs or device firmware projects, and the repository does not refer to them: no links, no names, no "inspired by" notes.

What you may use:

- Vendor datasheets, schematics and reference manuals (Espressif, Waveshare, the codec, display and power-chip makers).
- Espressif's and Waveshare's published driver components, fetched at build time by the ESP-IDF component manager under their own licenses. Never copy their source into this repository.
- The documentation and public APIs of the libraries listed in [THIRD_PARTY.md](THIRD_PARTY.md).
- OpenMausBot itself (Apache-2.0), which the Maus art is generated from.

If you are unsure whether a source is allowed, ask in the pull request before using it. A pull request that breaks this rule is closed, whatever else it does.

## Dependencies and licenses

Every new dependency (npm package, ESP-IDF managed component, CMake FetchContent download, font or generated asset source) needs a row in [THIRD_PARTY.md](THIRD_PARTY.md) with its identifier, version and license. `node --test "tools/release/test/*.test.ts"` fails when one is missing. Licenses must be compatible with Apache-2.0 distribution.

## Setting up

- Node.js 22.18 or newer (CI uses Node 24).
- CMake 3.24 or newer, a C11 compiler, and SDL2 for the simulator window.
- ESP-IDF v6.0.3 for the boards ([AGENTS.md](AGENTS.md) has the install steps).

## Tests

Run what your change touches; CI runs all of it on every push and pull request.

| Area | Command |
|---|---|
| Protocol vectors and fake host | `npm ci && npm run vectors:check && npm test` |
| Firmware core, simulator, end to end, snapshots | `cmake -S firmware -B build/host -DCMAKE_BUILD_TYPE=Debug && cmake --build build/host -j10 && ctest --test-dir build/host --output-on-failure -L "unit|vectors|e2e|snapshot"` |
| One board | see "Build and flash a board" in AGENTS.md, then `firmware/ports/esp32/tools/check-size.sh <board>` |
| Installer | `cd site && npm ci && npm test && npm run build` |
| Release tools, licensing and docs | `node --test "tools/release/test/*.test.ts"` |
| Console helper | `python3 -B -m unittest discover -s tools/console -p 'test_*.py'` |

Generated files are committed and checked for drift: protocol vectors (`npm run vectors`), the Maus art and fonts (`cd tools/art && npm run art && npm run fonts`) and snapshot goldens. Regenerate them with the tool, never by hand.

## Code style

- C is C11 and must also compile as gnu23 under ESP-IDF 6 with warnings as errors. Core and UI are single-threaded and include only what `firmware/core` allows.
- TypeScript runs directly on Node (type stripping): erasable syntax only (no `enum`, no parameter properties, no namespaces), and relative imports end in `.ts`.
- Every source file starts with `SPDX-License-Identifier: Apache-2.0`.
- User-facing text says "MausBot → Settings → Remote access → Pair a gadget" for pairing.

## Sending a change

1. Open an issue first for anything bigger than a fix.
2. Keep each pull request to one change, with its tests.
3. Describe what you tested on real hardware, if anything. Changes to board code need a run through [docs/hardware-checklist.md](docs/hardware-checklist.md) for that board.

By contributing, you agree that your contribution is licensed under the Apache License 2.0, the same license as the project.

## Security

Please report security problems privately through GitHub's "Report a vulnerability" button on the repository's Security tab, not in a public issue.
