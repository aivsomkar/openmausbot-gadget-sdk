/* SPDX-License-Identifier: Apache-2.0 */
/* The UI API (gadget_ui.h) on LVGL 9: ui_init, ui_render, ui_tick, ui_deinit. */
#include <string.h>

#include "ui_lv_compat.h"
#include "ui_priv.h"

ui_state_t g_ui;

/* Render cache (contract 2.9: ui_render is cheap when nothing it draws has
 * changed). Core bumps rev on every model change, including the levels and
 * the speaking clock that move every tick. Those are drawn by
 * ui_screens_apply_time() and ui_maus_tick(), so they are zeroed in a copy
 * before comparing, and only a real change re-lays out the screen. On the
 * ESP32, P2c may place the two copies in PSRAM through UI_MODEL_COPY_ATTR. */
#ifndef UI_MODEL_COPY_ATTR
#define UI_MODEL_COPY_ATTR
#endif
static UI_MODEL_COPY_ATTR ui_model_t s_last, s_cur;

static void strip_time_fields(ui_model_t *c) {
  c->rev = 0;
  c->now_ms = 0;
  c->mic_level = 0;
  c->speak_level = 0;
  c->reply.speak_elapsed_ms = 0;
  c->reply.speak_total_ms = 0;
}

gadget_status_t ui_init(const gadget_board_t *board, uint32_t prng_seed) {
  if (!board) return GADGET_ERR_ARG;
  const maus_art_t *art = maus_art_for(board->art_profile);
  if (!art) return GADGET_ERR_UNSUPPORTED;
  memset(&g_ui, 0, sizeof g_ui);
  g_ui.board = board;
  g_ui.art = art;
  g_ui.ask_locked = -1;
  ui_metrics_for(board, art->w, art->h, &g_ui.mt);

  lv_obj_t *scr = lv_screen_active();
  lv_obj_set_style_bg_color(scr, lv_color_hex(UI_COLOR_BG), 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
  ui_set_scrollable(scr, false);

  g_ui.root = ui_box(scr);
  lv_obj_set_size(g_ui.root, board->screen_w, board->screen_h);
  lv_obj_set_pos(g_ui.root, 0, 0);
  lv_obj_set_style_bg_color(g_ui.root, lv_color_hex(UI_COLOR_BG), 0);
  lv_obj_set_style_bg_opa(g_ui.root, LV_OPA_COVER, 0);
  /* Latin-1 fonts through styles on the UI root (contract 2.15); widgets inherit. */
  lv_obj_set_style_text_font(g_ui.root, g_ui.mt.font_body, 0);
  lv_obj_set_style_text_color(g_ui.root, lv_color_hex(UI_COLOR_INK), 0);

  ui_maus_create(&g_ui.maus, g_ui.root, art, prng_seed, g_ui.mt.maus.x, g_ui.mt.maus.y);
  ui_screens_create();
  return GADGET_OK;
}

void ui_render(const ui_model_t *m) {
  if (!g_ui.root || !m) return;
  ui_maus_set_state(&g_ui.maus, m->maus, m->now_ms);
  g_ui.speak_level = m->speak_level;
  if (!g_ui.rendered || m->rev != g_ui.rev) {
    memcpy(&s_cur, m, sizeof s_cur);
    strip_time_fields(&s_cur);
    if (!g_ui.rendered || memcmp(&s_cur, &s_last, sizeof s_cur) != 0) {
      ui_screens_apply(m);
      memcpy(&s_last, &s_cur, sizeof s_last);
    }
    g_ui.rev = m->rev;
    g_ui.rendered = true;
  }
  ui_screens_apply_time(m);
}

void ui_tick(uint64_t now_ms) {
  if (g_ui.root) ui_maus_tick(&g_ui.maus, now_ms, g_ui.speak_level);
  lv_timer_handler();
}

void ui_deinit(void) {
  if (g_ui.root) lv_obj_delete(g_ui.root);
  memset(&g_ui, 0, sizeof g_ui);
}
