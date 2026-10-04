// SPDX-License-Identifier: Apache-2.0
// The features every fake host runs.
import type { Feature } from "./context.ts";
import { displayFeature } from "./display.ts";
import { otaFeature } from "./ota.ts";
import { voiceFeature } from "./voice.ts";

export const FEATURES: Feature[] = [voiceFeature, displayFeature, otaFeature];
