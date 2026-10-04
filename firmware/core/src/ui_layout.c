/* firmware/core/src/ui_layout.c */
/* SPDX-License-Identifier: Apache-2.0 */
/* Pure layout shared by core (hit-testing) and the LVGL UI (drawing), so a
 * touch lands on exactly the button the UI draws (gadget_ui.h). */
#include <string.h>
#include "gadget_ui.h"

#define SAFE_MARGIN 8
#define BUTTON_GAP 8
#define BUTTON_MIN_H 40

gadget_rect_t ui_safe_area(const gadget_board_t *board) {
  gadget_rect_t r = {0, 0, (int16_t)board->screen_w, (int16_t)board->screen_h};
  if (board->screen_round) {
    /* the square inscribed in the circle: side = diameter / sqrt(2) */
    int16_t side = (int16_t)(((uint32_t)board->screen_w * 181u) / 256u);
    r.x = (int16_t)((board->screen_w - side) / 2);
    r.y = (int16_t)((board->screen_h - side) / 2);
    r.w = side;
    r.h = side;
  }
  r.x += SAFE_MARGIN;
  r.y += SAFE_MARGIN;
  r.w -= 2 * SAFE_MARGIN;
  r.h -= 2 * SAFE_MARGIN;
  return r;
}

/* Up to two buttons per row, rows anchored to the bottom of the safe area.
 * With an odd count the last option sits alone, full width, on the bottom row. */
void ui_layout_ask(const gadget_board_t *board, uint8_t n_options, gadget_rect_t out[UI_ASK_OPTIONS_MAX]) {
  memset(out, 0, sizeof(gadget_rect_t) * UI_ASK_OPTIONS_MAX);
  if (n_options == 0 || n_options > UI_ASK_OPTIONS_MAX) return;
  gadget_rect_t s = ui_safe_area(board);
  int16_t bh = (int16_t)(s.h / 6);
  if (bh < BUTTON_MIN_H) bh = BUTTON_MIN_H;
  int rows = (n_options + 1) / 2;
  int16_t half_w = (int16_t)((s.w - BUTTON_GAP) / 2);
  for (uint8_t i = 0; i < n_options; i++) {
    int row = i / 2;
    bool alone = (i == n_options - 1) && (n_options % 2 == 1);
    int16_t y = (int16_t)(s.y + s.h - (rows - row) * bh - (rows - 1 - row) * BUTTON_GAP);
    gadget_rect_t r;
    r.y = y;
    r.h = bh;
    if (alone) {
      r.x = s.x;
      r.w = s.w;
    } else {
      r.x = (int16_t)(s.x + (i % 2) * (half_w + BUTTON_GAP));
      r.w = half_w;
    }
    out[i] = r;
  }
}

int ui_hit_test(const ui_model_t *m, int16_t x, int16_t y) {
  if (m == NULL || m->screen != UI_SCREEN_ASK || !m->ask.answerable) return -1;
  for (uint8_t i = 0; i < m->ask.n_options && i < UI_ASK_OPTIONS_MAX; i++) {
    gadget_rect_t r = m->ask.options[i].rect;
    if (r.w > 0 && x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h) return i;
  }
  return -1;
}
