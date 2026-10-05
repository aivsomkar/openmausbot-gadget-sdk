/* SPDX-License-Identifier: Apache-2.0 */
#include "ui_pager.h"

#include <string.h>
#include "ui_lv_compat.h"

static lv_obj_t *one_line(lv_obj_t *parent, int32_t w) {
  lv_obj_t *l = lv_label_create(parent);
  lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_DOTS);
  lv_obj_set_width(l, w);
  lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
  ui_set_hidden(l, true);
  return l;
}

void ui_pager_create(ui_pager_t *p, lv_obj_t *parent, gadget_rect_t area, gadget_rect_t status_area, bool center_v) {
  memset(p, 0, sizeof *p);
  p->area = area;
  p->status_area = status_area;
  p->center_v = center_v;
  p->box = ui_box(parent);
  lv_obj_set_pos(p->box, area.x, area.y);
  lv_obj_set_size(p->box, area.w, area.h);
  p->label = lv_label_create(p->box);
  lv_label_set_long_mode(p->label, LV_LABEL_LONG_MODE_WRAP);
  lv_obj_set_width(p->label, area.w);
  lv_obj_set_pos(p->label, 0, 0);
  p->sub = one_line(parent, status_area.w);
  p->status = one_line(parent, status_area.w);
}

/* Shows `text` (or hides the line when it is NULL or "") with its top at y. */
static bool set_line(lv_obj_t *l, const char *text, int32_t x, int32_t y, int32_t h, const lv_font_t *font,
                     uint32_t color) {
  bool on = text && text[0];
  ui_set_hidden(l, !on);
  if (!on) return false;
  lv_obj_set_style_text_font(l, font, 0);
  lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
  lv_obj_set_height(l, h);
  lv_obj_set_pos(l, x, y);
  lv_label_set_text(l, text);
  return true;
}

void ui_pager_set(ui_pager_t *p, const char *text, const lv_font_t *font, uint32_t color, lv_text_align_t align,
                  const char *sub, const char *status, const lv_font_t *status_font, uint32_t status_color) {
  int32_t text_area_h = p->area.h;
  int32_t sh = status_font ? lv_font_get_line_height(status_font) : 0;
  int32_t top = p->status_area.y + p->status_area.h; /* lines stack up from the bottom edge */
  if (set_line(p->status, status, p->status_area.x, top - sh, sh, status_font, status_color)) top -= sh;
  if (set_line(p->sub, sub, p->status_area.x, top - sh, sh, status_font, status_color)) top -= sh;
  if (top < p->area.y + p->area.h) text_area_h = top - 4 - p->area.y;

  lv_obj_set_style_text_font(p->label, font, 0);
  lv_obj_set_style_text_color(p->label, lv_color_hex(color), 0);
  lv_obj_set_style_text_align(p->label, align, 0);
  lv_label_set_text(p->label, text);

  lv_point_t size;
  lv_text_get_size(&size, text, font, 0, 0, p->area.w, LV_TEXT_FLAG_NONE);
  p->line_h = lv_font_get_line_height(font);
  p->text_h = size.y;
  p->total_lines = (size.y + p->line_h - 1) / p->line_h;
  if (p->total_lines < 1) p->total_lines = 1;
  p->lines_per_page = text_area_h / p->line_h;
  if (p->lines_per_page < 1) p->lines_per_page = 1;
  lv_obj_set_height(p->box, p->lines_per_page * p->line_h);
  p->page = UINT16_MAX;
  ui_pager_show_page(p, 0);
}

uint16_t ui_pager_pages(const ui_pager_t *p) {
  return (uint16_t)((p->total_lines + p->lines_per_page - 1) / p->lines_per_page);
}

static void place(ui_pager_t *p, int32_t first_line) {
  int32_t box_h = p->lines_per_page * p->line_h;
  int32_t y = -first_line * p->line_h;
  if (p->total_lines <= p->lines_per_page && p->center_v) y = (box_h - p->text_h) / 2;
  lv_obj_set_y(p->label, y);
}

void ui_pager_show_page(ui_pager_t *p, uint16_t page) {
  uint16_t n = ui_pager_pages(p);
  if (page >= n) page = (uint16_t)(n - 1);
  if (page == p->page) return;
  p->page = page;
  place(p, (int32_t)page * p->lines_per_page);
}

void ui_pager_show_rotating(ui_pager_t *p, uint64_t age_ms) {
  uint16_t n = ui_pager_pages(p);
  ui_pager_show_page(p, (uint16_t)((age_ms / UI_PAGE_ROTATE_MS) % n));
}

void ui_pager_show_tail(ui_pager_t *p) {
  int32_t first = p->total_lines - p->lines_per_page;
  p->page = UINT16_MAX; /* the tail is not on a page boundary */
  place(p, first > 0 ? first : 0);
}

void ui_pager_show_progress(ui_pager_t *p, uint32_t elapsed_ms, uint32_t total_ms) {
  uint32_t n = ui_pager_pages(p);
  uint32_t page;
  if (total_ms > 0) page = (uint32_t)((uint64_t)elapsed_ms * n / total_ms);
  else page = elapsed_ms / ((uint32_t)p->lines_per_page * UI_MS_PER_LINE);
  ui_pager_show_page(p, (uint16_t)(page >= n ? n - 1 : page));
}

void ui_pager_set_hidden(ui_pager_t *p, bool hidden) {
  ui_set_hidden(p->box, hidden);
  if (hidden) {
    ui_set_hidden(p->sub, true);
    ui_set_hidden(p->status, true);
  }
}

int32_t ui_pager_bottom(const ui_pager_t *p) {
  int32_t box_h = p->lines_per_page * p->line_h;
  int32_t top = p->area.y;
  if (p->total_lines <= p->lines_per_page && p->center_v) top += (box_h - p->text_h) / 2;
  int32_t bottom = top + (p->text_h < box_h ? p->text_h : box_h);
  if (!ui_is_hidden(p->sub) || !ui_is_hidden(p->status)) {
    int32_t lines_bottom = p->status_area.y + p->status_area.h;
    if (lines_bottom > bottom) bottom = lines_bottom;
  }
  return bottom;
}
