// SPDX-License-Identifier: Apache-2.0
// The boards the installer offers (spec §5.3). Ids match install.json keys.
export interface BoardChoice {
  id: string;
  name: string;
  detail: string;
}

export const BOARD_CHOICES: readonly BoardChoice[] = [
  { id: "amoled-175c", name: "Waveshare ESP32-S3-Touch-AMOLED-1.75C", detail: "Round 1.75-inch AMOLED in an aluminum case, speaker and battery bay." },
  { id: "amoled-175", name: "Waveshare ESP32-S3-Touch-AMOLED-1.75", detail: "Round 1.75-inch AMOLED board with a speaker connector." },
  { id: "lcd-154", name: "Waveshare ESP32-S3-LCD-1.54", detail: "1.54-inch LCD with BOOT and PLUS buttons and a speaker." },
  { id: "devkit", name: "ESP32-S3-DevKitC-1-N16R8 breadboard build", detail: "2-inch ST7789, INMP441 mic and MAX98357A amp on a breadboard." },
];
