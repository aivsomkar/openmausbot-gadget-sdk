/* SPDX-License-Identifier: Apache-2.0 */
/* Screen colours (contract 2.15) and per-board layout metrics. Private to firmware/ui. */
#ifndef UI_THEME_H
#define UI_THEME_H

#include "lvgl.h"
#include "gadget_board.h"

#define UI_COLOR_BG 0x000000
#define UI_COLOR_ACCENT 0x2fd187
#define UI_COLOR_OK 0x3ddc84
#define UI_COLOR_BAD 0xff5a4f
#define UI_COLOR_WARN 0xffb020
#define UI_COLOR_INK 0xf2f4f8
#define UI_COLOR_MUTE 0x9aa2b2
#define UI_COLOR_DIM 0x6f7787

typedef struct {
  bool round, landscape, large;
  int16_t w, h;
  const lv_font_t *font_title, *font_body, *font_small, *font_tiny, *font_big;
  int16_t pad;
  gadget_rect_t maus;     /* Maus body (top-left, size) on Maus screens */
  gadget_rect_t caption;  /* text under (or beside) the Maus */
  gadget_rect_t status;   /* one status line (device id, retry, working); bottom-aligned in it */
  gadget_rect_t safe;     /* ui_safe_area(): text screens (ask, card, update) */
  gadget_rect_t toast;    /* post toast overlay */
  int16_t ring_d;         /* listening ring diameter, centred on the Maus body */
} ui_metrics_t;

/* Fills `out` for `board`; `maus_w`/`maus_h` are the art profile's body size. */
void ui_metrics_for(const gadget_board_t *board, uint16_t maus_w, uint16_t maus_h, ui_metrics_t *out);

/* Widest rectangle inside a circle of diameter `d` (top-left at 0,0) that
 * spans rows y0..y1 (inclusive), minus `margin` px on each side. */
gadget_rect_t ui_rect_in_circle(int16_t d, int16_t y0, int16_t y1, int16_t margin);

#endif /* UI_THEME_H */
