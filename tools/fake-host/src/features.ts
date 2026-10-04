// SPDX-License-Identifier: Apache-2.0
// The features every fake host runs. Later tasks add display and OTA here.
import type { Feature } from "./context.ts";
import { voiceFeature } from "./voice.ts";

export const FEATURES: Feature[] = [voiceFeature];
