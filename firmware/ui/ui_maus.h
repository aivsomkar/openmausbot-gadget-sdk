/* SPDX-License-Identifier: Apache-2.0 */
/* The Maus widget: three stacked images (body, eyes, mouth) in one
 * container that moves by whole pixels. Private to firmware/ui. */
#ifndef UI_MAUS_H
#define UI_MAUS_H

#include "ui_maus_engine.h"

typedef struct {
  maus_engine_t engine;
  lv_obj_t *box;
  lv_obj_t *body;
  lv_obj_t *eyes;
  lv_obj_t *mouth;
  int16_t base_x, base_y;
  const void *eyes_src;   /* last image set, to skip redundant updates */
  const void *mouth_src;
  int16_t x, y;
} ui_maus_t;

void ui_maus_create(ui_maus_t *w, lv_obj_t *parent, const maus_art_t *art, uint32_t seed, int16_t x, int16_t y);
/* Hide (UI_MAUS_NONE) or show a state; the engine keeps its own timing. */
void ui_maus_set_state(ui_maus_t *w, ui_maus_state_t state, uint64_t now_ms);
/* Advance the animation and move the images (call from ui_tick). */
void ui_maus_tick(ui_maus_t *w, uint64_t now_ms, uint8_t speak_level);

#endif /* UI_MAUS_H */
