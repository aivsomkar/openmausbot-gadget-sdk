/* firmware/core/include/gadget_board.h */
/* SPDX-License-Identifier: Apache-2.0 */
/* Board descriptor: everything core and UI need to know about a board.
 * The four descriptors live in core/src/boards.c (one table for the
 * simulator and the ESP32 port); pins live in ports/esp32/boards/<id>/board.h. */
#ifndef GADGET_BOARD_H
#define GADGET_BOARD_H

#include "gadget_types.h"

typedef enum {
  GADGET_ART_S240 = 0,  /* body 201x240: amoled-175c, amoled-175 */
  GADGET_ART_S150 = 1   /* body 125x150: lcd-154, devkit */
} gadget_art_profile_t;

#define GADGET_INPUT_TOUCH  (1u << 0)
#define GADGET_INPUT_TALK   (1u << 1)
#define GADGET_INPUT_CANCEL (1u << 2)

typedef struct gadget_board {
  const char *id;              /* /^[a-z0-9-]{1,32}$/, e.g. "amoled-175c"; hello.board */
  const char *display_name;    /* "Waveshare ESP32-S3-Touch-AMOLED-1.75C" */
  uint16_t screen_w, screen_h; /* caps.screen.w/h */
  bool screen_round;           /* caps.screen.round */
  uint16_t image_w, image_h;   /* caps.image.w/h */
  uint32_t mic_rate;           /* caps.mic.rate: 16000 */
  uint32_t speaker_rate;       /* caps.speaker.rate: 16000 or 24000; 0 = no speaker (omitted) */
  uint32_t input_mask;         /* GADGET_INPUT_*; caps.input in order touch, talk, cancel */
  bool has_battery;            /* caps.battery (omitted when false) */
  uint32_t ota_max;            /* caps.ota.max = OTA slot size in bytes */
  gadget_art_profile_t art_profile;
} gadget_board_t;

/* Lookup over the four built-in descriptors; NULL for an unknown id. */
const gadget_board_t *gadget_board_by_id(const char *id);
/* Iteration: index 0..n-1, NULL past the end. */
const gadget_board_t *gadget_board_at(size_t index);

#endif /* GADGET_BOARD_H */
