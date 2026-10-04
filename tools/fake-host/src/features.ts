// SPDX-License-Identifier: Apache-2.0
// The features every fake host runs. A later task adds OTA here.
import type { Feature } from "./context.ts";
import { displayFeature } from "./display.ts";
import { voiceFeature } from "./voice.ts";

export const FEATURES: Feature[] = [voiceFeature, displayFeature];
