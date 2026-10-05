// SPDX-License-Identifier: Apache-2.0
import assert from "node:assert/strict";
import { test } from "node:test";
import { bundledPackages } from "../scripts/build.ts";

test("licenses come from each bundled package's own directory, nested and scoped ones included", () => {
  const inputs = [
    "src/main.ts",
    "node_modules/esptool-js/lib/index.js",
    "node_modules/esptool-js/lib/targets/esp32s3.js",
    "node_modules/pako/dist/pako.esm.mjs",
    // A dependency npm could not hoist: its notice must be its own, not esptool-js's.
    "node_modules/esptool-js/node_modules/pako/dist/pako.esm.mjs",
    "node_modules/@scope/pkg/index.js",
    "node_modules/a/node_modules/@scope/pkg/index.js",
  ];
  assert.deepEqual(bundledPackages(inputs), [
    { name: "@scope/pkg", dir: "node_modules/@scope/pkg" },
    { name: "@scope/pkg", dir: "node_modules/a/node_modules/@scope/pkg" },
    { name: "esptool-js", dir: "node_modules/esptool-js" },
    { name: "pako", dir: "node_modules/esptool-js/node_modules/pako" },
    { name: "pako", dir: "node_modules/pako" },
  ]);
  assert.deepEqual(bundledPackages(["src/main.ts", "src/console.ts"]), []);
});
