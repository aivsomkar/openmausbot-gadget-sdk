/* SPDX-License-Identifier: Apache-2.0 */
#include "ui_maus.h"

#include "ui_lv_compat.h"

static lv_obj_t *layer_image(lv_obj_t *parent) {
  lv_obj_t *img = lv_image_create(parent);
  ui_set_clickable(img, false);
  return img;
}

static void set_layer(lv_obj_t *img, const maus_layer_t *layer, const void **last) {
  if (*last != layer->img) {
    lv_image_set_src(img, layer->img);
    *last = layer->img;
  }
  lv_obj_set_pos(img, layer->x, layer->y);
}

void ui_maus_create(ui_maus_t *w, lv_obj_t *parent, const maus_art_t *art, uint32_t seed, int16_t x, int16_t y) {
  maus_engine_init(&w->engine, art, seed);
  w->box = ui_box(parent);
  lv_obj_set_size(w->box, art->w, art->h);
  w->base_x = x;
  w->base_y = y;
  w->x = x;
  w->y = y;
  lv_obj_set_pos(w->box, x, y);
  w->body = layer_image(w->box);
  lv_image_set_src(w->body, art->body);
  lv_obj_set_pos(w->body, 0, 0);
  w->eyes = layer_image(w->box);
  /* A8 eye frames draw in the recolor colour (contract 2.15). recolor_opa stays
   * 0, so RGB565A8 eyes (the white-eye fallback) keep LVGL's fast path. The
   * body and mouth layers never set recolor. */
  lv_obj_set_style_image_recolor(w->eyes, lv_color_white(), 0);
  w->mouth = layer_image(w->box);
  w->eyes_src = NULL;
  w->mouth_src = NULL;
  ui_set_hidden(w->box, true);
}

void ui_maus_set_state(ui_maus_t *w, ui_maus_state_t state, uint64_t now_ms) {
  if ((unsigned)state >= UI_MAUS__COUNT) state = UI_MAUS_NONE; /* never index past the art's state table */
  maus_engine_set_state(&w->engine, state, now_ms);
  ui_set_hidden(w->box, state == UI_MAUS_NONE);
}

void ui_maus_tick(ui_maus_t *w, uint64_t now_ms, uint8_t speak_level) {
  if (w->engine.state == UI_MAUS_NONE) return;
  const maus_art_t *art = w->engine.art;
  maus_frame_t f = maus_engine_step(&w->engine, now_ms, speak_level);
  set_layer(w->eyes, &art->eyes[f.expr][f.blink_step], &w->eyes_src);
  if (f.speak >= 0) set_layer(w->mouth, &art->speak[f.speak][f.speak_level - 1], &w->mouth_src);
  else set_layer(w->mouth, &art->mouth[f.expr], &w->mouth_src);
  int16_t x = (int16_t)(w->base_x + f.dx);
  int16_t y = (int16_t)(w->base_y + f.dy);
  if (x != w->x || y != w->y) {
    lv_obj_set_pos(w->box, x, y);
    w->x = x;
    w->y = y;
  }
}
