/* SPDX-License-Identifier: Apache-2.0 */
/* A caption that shows whole lines only: a clipping box holding one wrapped
 * label, moved up a page at a time, plus up to two optional one-line labels
 * stacked on the bottom of the status area (status at the bottom, sub above
 * it). Private to firmware/ui. */
#ifndef UI_PAGER_H
#define UI_PAGER_H

#include "lvgl.h"
#include "gadget_types.h"

#define UI_PAGE_ROTATE_MS 4000u /* Setup/Offline copy that does not fit turns its page this often */
#define UI_MS_PER_LINE 2000u    /* speaking: page time per line while the stream's length is unknown */

typedef struct {
  lv_obj_t *box;
  lv_obj_t *label;
  lv_obj_t *sub;    /* one line above the status (the host name on Setup and Offline) */
  lv_obj_t *status;
  gadget_rect_t area;
  gadget_rect_t status_area; /* status and sub sit on its bottom edge */
  bool center_v;           /* centre text that fits (landscape layout) */
  int32_t line_h;
  int32_t lines_per_page;
  int32_t total_lines;
  int32_t text_h;
  uint16_t page;
} ui_pager_t;

void ui_pager_create(ui_pager_t *p, lv_obj_t *parent, gadget_rect_t area, gadget_rect_t status_area, bool center_v);
/* sub and status may each be NULL or "" (no line). Both are one line in
 * status_font and status_color: status on the bottom edge of status_area,
 * sub just above it (or on that edge without a status). The text gets the
 * whole area, or stops 4 px above the topmost line where they overlap. */
void ui_pager_set(ui_pager_t *p, const char *text, const lv_font_t *font, uint32_t color, lv_text_align_t align,
                  const char *sub, const char *status, const lv_font_t *status_font, uint32_t status_color);
uint16_t ui_pager_pages(const ui_pager_t *p);
void ui_pager_show_page(ui_pager_t *p, uint16_t page);
void ui_pager_show_rotating(ui_pager_t *p, uint64_t now_ms);
void ui_pager_show_tail(ui_pager_t *p);
/* total_ms 0 = unknown: UI_MS_PER_LINE per line of a page. */
void ui_pager_show_progress(ui_pager_t *p, uint32_t elapsed_ms, uint32_t total_ms);
void ui_pager_set_hidden(ui_pager_t *p, bool hidden);
/* Lowest y of anything the caption shows (its text on the current page, or
 * the sub and status lines when one of them shows). */
int32_t ui_pager_bottom(const ui_pager_t *p);

#endif /* UI_PAGER_H */
