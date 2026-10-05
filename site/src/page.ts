// SPDX-License-Identifier: Apache-2.0
// Pure helpers for main.ts, so the page's gates are unit-tested in Node (spec §5.8).
import { COPY } from "./copy.ts";
import type { InstallIndex } from "./install.ts";

/** Feature detection, never the browser's name: the page needs Web Serial. */
export function supportsWebSerial(nav: object): boolean {
  return "serial" in nav;
}

/** The line under the heading; before the first release it says no firmware is published. */
export function releaseLine(index: InstallIndex): string {
  return index.version === null ? COPY.noFirmware : `Firmware ${index.version}`;
}
