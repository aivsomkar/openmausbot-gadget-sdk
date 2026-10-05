/* SPDX-License-Identifier: Apache-2.0 */
#include "ui_theme.h"

#include "gadget_ui.h"
#include "fonts/ui_fonts.h"

static int32_t isqrt32(int32_t v) {
  if (v <= 0) return 0;
  int32_t r = 0;
  while ((r + 1) * (r + 1) <= v) r++;
  return r;
}

gadget_rect_t ui_rect_in_circle(int16_t d, int16_t y0, int16_t y1, int16_t margin) {
  int32_t r = d / 2;
  int32_t far0 = y0 - r < 0 ? r - y0 : y0 - r;
  int32_t far1 = y1 - r < 0 ? r - y1 : y1 - r;
  int32_t far = far0 > far1 ? far0 : far1;
  int32_t half = isqrt32(r * r - far * far) - margin;
  if (half < 0) half = 0;
  gadget_rect_t out = {(int16_t)(r - half), y0, (int16_t)(2 * half), (int16_t)(y1 - y0 + 1)};
  return out;
}

void ui_metrics_for(const gadget_board_t *board, uint16_t maus_w, uint16_t maus_h, ui_metrics_t *m) {
  const int16_t w = (int16_t)board->screen_w;
  const int16_t h = (int16_t)board->screen_h;
  *m = (ui_metrics_t){0};
  m->w = w;
  m->h = h;
  m->round = board->screen_round;
  m->landscape = w > h;
  m->large = w >= 400;
  m->safe = ui_safe_area(board);
  if (m->large) {
    m->font_title = &font_latin1_28;
    m->font_body = &font_latin1_24;
    m->font_small = &font_latin1_20;
    m->font_tiny = &font_latin1_16;
    m->font_big = &font_latin1_40;
    m->pad = 10;
  } else {
    m->font_title = &font_latin1_20;
    m->font_body = &font_latin1_16;
    m->font_small = &font_latin1_14;
    m->font_tiny = &font_latin1_14;
    m->font_big = &font_latin1_28;
    m->pad = 6;
  }
  const int16_t mw = (int16_t)maus_w;
  const int16_t mh = (int16_t)maus_h;
  if (m->landscape) {
    /* Maus on the left, text column on the right. */
    m->maus = (gadget_rect_t){16, (int16_t)((h - mh) / 2), mw, mh};
    int16_t cx = (int16_t)(m->maus.x + mw + 12);
    m->caption = (gadget_rect_t){cx, 12, (int16_t)(w - cx - 8), (int16_t)(h - 24)};
    m->ring_d = (int16_t)(mh + 8);
    m->toast = (gadget_rect_t){8, (int16_t)(h - 8 - 60), (int16_t)(w - 16), 60};
    /* full-width line under everything: a device id does not fit the text column */
    int16_t sh = (int16_t)lv_font_get_line_height(m->font_small);
    m->status = (gadget_rect_t){8, (int16_t)(h - 4 - sh), (int16_t)(w - 16), sh};
    m->caption.h = (int16_t)(m->status.y - 4 - m->caption.y);
  } else if (m->round) {
    /* Maus near the top, caption in the circle below it. */
    m->maus = (gadget_rect_t){(int16_t)((w - mw) / 2), 30, mw, mh};
    int16_t y0 = (int16_t)(m->maus.y + mh + 8);
    m->caption = ui_rect_in_circle(w, y0, (int16_t)(h - 50), 12);
    m->ring_d = (int16_t)(mh + 24);
    m->toast = ui_rect_in_circle(w, (int16_t)(h - 140), (int16_t)(h - 46), 10);
    int16_t sh = (int16_t)lv_font_get_line_height(m->font_small);
    m->status = (gadget_rect_t){m->caption.x, (int16_t)(m->caption.y + m->caption.h - sh), m->caption.w, sh};
  } else {
    /* Square: Maus on top, caption underneath. */
    m->maus = (gadget_rect_t){(int16_t)((w - mw) / 2), 8, mw, mh};
    int16_t y0 = (int16_t)(m->maus.y + mh + 6);
    m->caption = (gadget_rect_t){8, y0, (int16_t)(w - 16), (int16_t)(h - y0 - 6)};
    m->ring_d = (int16_t)(mh + 16);
    m->toast = (gadget_rect_t){8, (int16_t)(h - 8 - 60), (int16_t)(w - 16), 60};
    int16_t sh = (int16_t)lv_font_get_line_height(m->font_small);
    m->status = (gadget_rect_t){m->caption.x, (int16_t)(m->caption.y + m->caption.h - sh), m->caption.w, sh};
  }
}
