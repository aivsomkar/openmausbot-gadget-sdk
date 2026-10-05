# Third-party software

Every dependency of this repository, with its license. The **Identifier** column holds the exact name the build uses (npm package, ESP-IDF managed component or CMake FetchContent name); `node --test "tools/release/test/*.test.ts"` checks that every direct dependency appears here. All of these licenses allow distribution under the project's Apache-2.0 license.

## In the firmware and the simulator

| Component | Identifier | Version | License | Where |
|---|---|---|---|---|
| ESP-IDF | `esp-idf` | v6.0.3 (builds on v5.5.5) | Apache-2.0; it bundles further components (FreeRTOS, lwIP, newlib and others) under their own licenses, listed in ESP-IDF's "Copyrights and Licenses" page | Board firmware |
| ESP WebSocket client | `espressif/esp_websocket_client` | ^1.8.0 | Apache-2.0 | Board firmware |
| mDNS | `espressif/mdns` | ^1.14.0 | Apache-2.0 | Board firmware |
| cJSON (ESP-IDF component) | `espressif/cjson` | ^1.7.19 | MIT | Board firmware |
| Audio codec driver | `espressif/esp_codec_dev` | ^1.6.2 | Apache-2.0 | Board firmware |
| CO5300 AMOLED driver | `espressif/esp_lcd_co5300` | ^2.2.0 | Apache-2.0 | amoled-175c, amoled-175 |
| LCD touch framework | `espressif/esp_lcd_touch` | ^1.2.1 | Apache-2.0 | amoled-175c, amoled-175 |
| CST9217 touch driver | `waveshare/esp_lcd_touch_cst9217` | ^2.0.0 | Apache-2.0 | amoled-175c, amoled-175 |
| LVGL | `lvgl/lvgl`, `lvgl` | 9.6.0 | MIT | Firmware UI and simulator |
| lodepng (bundled with LVGL) | `lodepng` | as in LVGL 9.6.0 | Zlib | Simulator snapshots only |
| cJSON | `cjson` | 1.7.19 | MIT | Simulator and tests |
| Mbed TLS | `mbedtls` | 3.6.7 and 4.2.0 (desktop); the version ESP-IDF bundles (device) | Apache-2.0 (dual-licensed Apache-2.0 OR GPL-2.0-or-later; used under Apache-2.0) | PSA crypto |
| wslay | `wslay` | 1.1.1 | MIT | Simulator WebSocket client |
| SDL2 | `SDL2` | 2.x from the system | Zlib | Simulator window |
| Unity | `unity` | 2.7.0 | MIT | C unit tests only, never shipped |
| Montserrat | `Montserrat` | the copy in LVGL 9.6.0 | OFL-1.1 | Latin-1 fonts generated into `firmware/ui/fonts` |

Espressif's and Waveshare's components are fetched at build time by the ESP-IDF component manager and are never copied into this repository.

## Build-time and tooling only (never in the firmware)

| Component | Identifier | Version | License | Where |
|---|---|---|---|---|
| noble-curves | `@noble/curves` | 2.4.0 | MIT | Protocol vector generator |
| noble-hashes | `@noble/hashes` | 2.4.0 | MIT | Dependency of `@noble/curves` |
| ws | `ws` | 8.22.0 | MIT | Fake host |
| resvg-js | `@resvg/resvg-js` | 2.6.2 | MPL-2.0 | Art generator (`tools/art`) |
| pngjs | `pngjs` | 7.0.0 | MIT | Art generator |
| lv_font_conv | `lv_font_conv` | 1.5.3 | MIT | Font generator |
| esbuild | `esbuild` | 0.28.2 | MIT | Installer build |
| TypeScript | `typescript` | 5.9.3 | Apache-2.0 | Installer type check |
| Node.js type definitions | `@types/node` | 24.19.1 | MIT | Installer type check |
| Web Serial type definitions | `@types/w3c-web-serial` | 1.0.8 | MIT | Installer type check |
| SparkMD5 type definitions | `@types/spark-md5` | 3.0.5 | MIT | Installer type check |
| pyserial | `pyserial` | 3.5 (installed with esptool or ESP-IDF) | BSD-3-Clause | Console helper (`tools/console/omb_console.py`) |

## In the installer page

These are bundled into the installer's `app.js`; the page serves their full license texts as `licenses.txt`.

| Component | Identifier | Version | License |
|---|---|---|---|
| esptool-js | `esptool-js` | 0.7.0 | Apache-2.0 |
| pako (esptool-js dependency) | `pako` | 2.x, locked in `site/package-lock.json` | MIT AND Zlib |
| atob-lite (esptool-js dependency) | `atob-lite` | 2.0.0 | MIT |
| SparkMD5 | `spark-md5` | 3.0.2 | WTFPL OR MIT (used under MIT) |

## Source material

| Material | License | Notes |
|---|---|---|
| OpenMausBot mascot geometry (`shared/mascot-bodies.ts`, `src/components/cursor-face-data.ts`) | Apache-2.0 | Pinned commit `6dd4403d8fbbbd5c17169724cb2a529f11d7543e`; see `tools/art/source/README.md`. The OpenMausBot name, the MausBot name and the Maus mascot are trademarks of Supamaus Software Private Limited. |
