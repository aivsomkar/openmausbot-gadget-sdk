/* firmware/core/src/boards.c */
/* SPDX-License-Identifier: Apache-2.0 */
/* The four board descriptors (contract §2.3), shared by the simulator and
 * the ESP32 port. Pins live in ports/esp32/boards/<id>/board.h (plan P2c).
 * Adding a board: a row here, a directory under ports/esp32/boards/, and
 * its art profile. */
#include <string.h>
#include "gadget_board.h"

#define OTA_SLOT_BYTES 6291456u /* partitions/16mb.csv ota_0 and ota_1: 0x600000 */

static const gadget_board_t BOARDS[] = {
    {.id = "amoled-175c",
     .display_name = "Waveshare ESP32-S3-Touch-AMOLED-1.75C",
     .screen_w = 466, .screen_h = 466, .screen_round = true,
     .image_w = 300, .image_h = 300,
     .mic_rate = 16000, .speaker_rate = 16000,
     .input_mask = GADGET_INPUT_TOUCH | GADGET_INPUT_TALK | GADGET_INPUT_CANCEL,
     .has_battery = true, .ota_max = OTA_SLOT_BYTES, .art_profile = GADGET_ART_S240},
    {.id = "amoled-175",
     .display_name = "Waveshare ESP32-S3-Touch-AMOLED-1.75",
     .screen_w = 466, .screen_h = 466, .screen_round = true,
     .image_w = 300, .image_h = 300,
     .mic_rate = 16000, .speaker_rate = 16000,
     .input_mask = GADGET_INPUT_TOUCH | GADGET_INPUT_TALK,
     .has_battery = true, .ota_max = OTA_SLOT_BYTES, .art_profile = GADGET_ART_S240},
    {.id = "lcd-154",
     .display_name = "Waveshare ESP32-S3-LCD-1.54",
     .screen_w = 240, .screen_h = 240, .screen_round = false,
     .image_w = 200, .image_h = 200,
     .mic_rate = 16000, .speaker_rate = 16000,
     .input_mask = GADGET_INPUT_TALK | GADGET_INPUT_CANCEL,
     .has_battery = true, .ota_max = OTA_SLOT_BYTES, .art_profile = GADGET_ART_S150},
    {.id = "devkit",
     .display_name = "ESP32-S3-DevKitC-1-N16R8 + 2\" ST7789",
     .screen_w = 320, .screen_h = 240, .screen_round = false,
     .image_w = 280, .image_h = 200,
     .mic_rate = 16000, .speaker_rate = 24000,
     .input_mask = GADGET_INPUT_TALK | GADGET_INPUT_CANCEL,
     .has_battery = false, .ota_max = OTA_SLOT_BYTES, .art_profile = GADGET_ART_S150},
};

#define BOARD_COUNT (sizeof BOARDS / sizeof BOARDS[0])

const gadget_board_t *gadget_board_by_id(const char *id) {
  if (id == NULL) return NULL;
  for (size_t i = 0; i < BOARD_COUNT; i++) {
    if (strcmp(BOARDS[i].id, id) == 0) return &BOARDS[i];
  }
  return NULL;
}

const gadget_board_t *gadget_board_at(size_t index) { return index < BOARD_COUNT ? &BOARDS[index] : NULL; }
