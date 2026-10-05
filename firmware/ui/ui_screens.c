/* SPDX-License-Identifier: Apache-2.0 */
/* Every screen of spec 5.5, drawn from the model. Objects are created once
 * in ui_screens_create() and shown, hidden and filled per render. */
#include <stdio.h>
#include <string.h>

#include "ui_copy.h"
#include "ui_lv_compat.h"
#include "ui_priv.h"

#define BATTERY_ARC_DEG 30 /* a short arc centred on the top edge of the screen */

static lv_obj_t *label(lv_obj_t *parent, const lv_font_t *font, uint32_t color, lv_label_long_mode_t mode) {
  lv_obj_t *l = lv_label_create(parent);
  ui_set_clickable(l, false);
  lv_label_set_long_mode(l, mode);
  lv_obj_set_style_text_font(l, font, 0);
  lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
  return l;
}

static int32_t line_h(const lv_font_t *f) { return lv_font_get_line_height(f); }

/* Height of `text` wrapped at width w, capped to max_lines lines. */
static int32_t text_h(const char *text, const lv_font_t *f, int32_t w, int32_t max_lines) {
  lv_point_t s;
  lv_text_get_size(&s, text, f, 0, 0, w, LV_TEXT_FLAG_NONE);
  int32_t cap = max_lines * line_h(f);
  return s.y < cap ? s.y : cap;
}

/* Spec 5.5: a battery arc at the top edge, on every board with a battery. It
 * is a slice of a circle of the screen's width, so on round boards it follows
 * the glass. */
static void create_battery(void) {
  ui_metrics_t *mt = &g_ui.mt;
  lv_obj_t *a = lv_arc_create(g_ui.root);
  lv_obj_remove_style(a, NULL, LV_PART_KNOB);
  ui_set_clickable(a, false);
  lv_obj_set_size(a, mt->w - 8, mt->w - 8);
  lv_obj_set_pos(a, 4, 4);
  lv_arc_set_bg_angles(a, 270 - BATTERY_ARC_DEG / 2, 270 + BATTERY_ARC_DEG / 2);
  lv_arc_set_range(a, 0, 100);
  lv_obj_set_style_arc_width(a, 4, LV_PART_MAIN);
  lv_obj_set_style_arc_width(a, 4, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(a, lv_color_hex(UI_COLOR_DIM), LV_PART_MAIN);
  lv_obj_set_style_arc_opa(a, LV_OPA_50, LV_PART_MAIN);
  lv_obj_set_style_arc_rounded(a, true, LV_PART_MAIN);
  lv_obj_set_style_arc_rounded(a, true, LV_PART_INDICATOR);
  g_ui.battery = a;
  ui_set_hidden(a, true);
}

static void create_toast(void) {
  ui_metrics_t *mt = &g_ui.mt;
  lv_obj_t *t = ui_box(g_ui.root);
  lv_obj_set_pos(t, mt->toast.x, mt->toast.y);
  lv_obj_set_size(t, mt->toast.w, mt->toast.h);
  lv_obj_set_style_radius(t, 14, 0);
  lv_obj_set_style_bg_color(t, lv_color_hex(UI_COLOR_DIM), 0);
  lv_obj_set_style_bg_opa(t, LV_OPA_40, 0);
  lv_obj_set_style_pad_all(t, 6, 0);
  g_ui.toast_name = label(t, mt->font_tiny, UI_COLOR_ACCENT, LV_LABEL_LONG_MODE_DOTS);
  lv_obj_set_size(g_ui.toast_name, mt->toast.w - 12, line_h(mt->font_tiny));
  lv_obj_set_pos(g_ui.toast_name, 0, 0);
  lv_obj_set_style_text_align(g_ui.toast_name, LV_TEXT_ALIGN_CENTER, 0);
  g_ui.toast_text = label(t, mt->font_small, UI_COLOR_INK, LV_LABEL_LONG_MODE_DOTS);
  int32_t th = mt->toast.h - 12 - line_h(mt->font_tiny);
  th -= th % line_h(mt->font_small);
  lv_obj_set_size(g_ui.toast_text, mt->toast.w - 12, th);
  lv_obj_set_pos(g_ui.toast_text, 0, line_h(mt->font_tiny));
  lv_obj_set_style_text_align(g_ui.toast_text, LV_TEXT_ALIGN_CENTER, 0);
  g_ui.toast = t;
  ui_set_hidden(t, true);
}

void ui_screens_create(void) {
  ui_metrics_t *mt = &g_ui.mt;

  /* listening ring, centred on the Maus body */
  lv_obj_t *r = lv_arc_create(g_ui.root);
  lv_obj_remove_style(r, NULL, LV_PART_KNOB);
  ui_set_clickable(r, false);
  lv_obj_set_size(r, mt->ring_d, mt->ring_d);
  lv_obj_set_pos(r, mt->maus.x + mt->maus.w / 2 - mt->ring_d / 2, mt->maus.y + mt->maus.h / 2 - mt->ring_d / 2);
  lv_arc_set_bg_angles(r, 0, 360);
  lv_arc_set_angles(r, 0, 360);
  lv_obj_set_style_arc_opa(r, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_arc_color(r, lv_color_hex(UI_COLOR_ACCENT), LV_PART_INDICATOR);
  g_ui.ring = r;
  ui_set_hidden(r, true);
  lv_obj_move_to_index(r, 0); /* behind the Maus */

  ui_pager_create(&g_ui.caption, g_ui.root, mt->caption, mt->status, mt->landscape);
  ui_pager_set_hidden(&g_ui.caption, true);

  g_ui.countdown = label(g_ui.root, mt->font_big, UI_COLOR_ACCENT, LV_LABEL_LONG_MODE_CLIP);
  lv_obj_set_size(g_ui.countdown, mt->caption.w, line_h(mt->font_big));
  lv_obj_set_pos(g_ui.countdown, mt->caption.x, mt->caption.y + (mt->caption.h - line_h(mt->font_big)) / 2);
  lv_obj_set_style_text_align(g_ui.countdown, LV_TEXT_ALIGN_CENTER, 0);
  ui_set_hidden(g_ui.countdown, true);

  g_ui.title = label(g_ui.root, mt->font_title, UI_COLOR_INK, LV_LABEL_LONG_MODE_DOTS);
  g_ui.body = label(g_ui.root, mt->font_small, UI_COLOR_MUTE, LV_LABEL_LONG_MODE_DOTS);
  g_ui.note = label(g_ui.root, mt->font_small, UI_COLOR_MUTE, LV_LABEL_LONG_MODE_WRAP);
  lv_obj_set_style_text_align(g_ui.title, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_align(g_ui.body, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_align(g_ui.note, LV_TEXT_ALIGN_CENTER, 0);
  for (int i = 0; i < UI_ASK_OPTIONS_MAX; i++) {
    lv_obj_t *o = ui_box(g_ui.root);
    lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_color(o, lv_color_hex(UI_COLOR_MUTE), 0);
    g_ui.opt[i] = o;
    g_ui.opt_label[i] = label(o, mt->font_body, UI_COLOR_INK, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_style_text_align(g_ui.opt_label[i], LV_TEXT_ALIGN_CENTER, 0);
    g_ui.opt_hint[i] = label(g_ui.root, mt->font_tiny, UI_COLOR_DIM, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_style_text_align(g_ui.opt_hint[i], LV_TEXT_ALIGN_CENTER, 0);
  }
  g_ui.bar = lv_bar_create(g_ui.root);
  ui_set_clickable(g_ui.bar, false);
  lv_bar_set_range(g_ui.bar, 0, 100);
  lv_obj_set_style_bg_color(g_ui.bar, lv_color_hex(UI_COLOR_DIM), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(g_ui.bar, LV_OPA_50, LV_PART_MAIN);
  lv_obj_set_style_bg_color(g_ui.bar, lv_color_hex(UI_COLOR_ACCENT), LV_PART_INDICATOR);
  g_ui.image = lv_image_create(g_ui.root);
  ui_set_clickable(g_ui.image, false);

  create_battery();
  create_toast();
}

static void hide_all(void) {
  ui_pager_set_hidden(&g_ui.caption, true);
  ui_set_hidden(g_ui.ring, true);
  ui_set_hidden(g_ui.countdown, true);
  ui_set_hidden(g_ui.title, true);
  ui_set_hidden(g_ui.body, true);
  ui_set_hidden(g_ui.note, true);
  for (int i = 0; i < UI_ASK_OPTIONS_MAX; i++) {
    ui_set_hidden(g_ui.opt[i], true);
    ui_set_hidden(g_ui.opt_hint[i], true);
  }
  ui_set_hidden(g_ui.bar, true);
  ui_set_hidden(g_ui.image, true);
}

static void show_caption(const char *text, const lv_font_t *f, uint32_t color, lv_text_align_t align,
                         const char *sub, const char *status, const lv_font_t *sf, uint32_t scolor) {
  ui_pager_set(&g_ui.caption, text, f, color, align, sub, status, sf, scolor);
  ui_pager_set_hidden(&g_ui.caption, false);
}

/* Title at the top of the safe area (≤ 2 lines); returns its bottom edge. */
static int32_t place_title(const char *text) {
  const gadget_rect_t s = g_ui.mt.safe;
  const lv_font_t *f = g_ui.mt.font_title;
  lv_label_set_text(g_ui.title, text);
  lv_obj_set_pos(g_ui.title, s.x, s.y);
  lv_obj_set_size(g_ui.title, s.w, text_h(text, f, s.w, 2));
  ui_set_hidden(g_ui.title, false);
  return s.y + text_h(text, f, s.w, 2);
}

/* Body between y0 and y1, whole lines only. */
static void place_body(const char *text, int32_t y0, int32_t y1) {
  const gadget_rect_t s = g_ui.mt.safe;
  const lv_font_t *f = g_ui.mt.font_small;
  int32_t lh = line_h(f);
  int32_t lines = (y1 - y0) / lh;
  if (!text[0] || lines < 1) return;
  int32_t h = text_h(text, f, s.w, lines);
  lv_label_set_text(g_ui.body, text);
  lv_obj_set_pos(g_ui.body, s.x, y0);
  lv_obj_set_size(g_ui.body, s.w, h);
  ui_set_hidden(g_ui.body, false);
}

static void place_note(const char *text) {
  const gadget_rect_t s = g_ui.mt.safe;
  int32_t h = text_h(text, g_ui.mt.font_small, s.w, 3);
  lv_label_set_text(g_ui.note, text);
  lv_obj_set_width(g_ui.note, s.w);
  lv_obj_set_pos(g_ui.note, s.x, s.y + s.h - h);
  ui_set_hidden(g_ui.note, false);
}

static void style_option(int i, const ui_ask_option_t *o, gadget_rect_t r) {
  lv_obj_t *b = g_ui.opt[i];
  lv_obj_t *l = g_ui.opt_label[i];
  lv_obj_set_pos(b, r.x, r.y);
  lv_obj_set_size(b, r.w, r.h);
  uint32_t bg = o->style == UI_STYLE_ALLOW ? UI_COLOR_OK : o->style == UI_STYLE_DENY ? UI_COLOR_BAD : UI_COLOR_BG;
  lv_obj_set_style_bg_color(b, lv_color_hex(bg), 0);
  lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(b, o->style == UI_STYLE_NEUTRAL ? 2 : 0, 0);
  lv_obj_set_style_text_color(l, lv_color_hex(o->style == UI_STYLE_NEUTRAL ? UI_COLOR_INK : UI_COLOR_BG), 0);
  lv_label_set_text(l, o->label);
  int32_t lh = line_h(g_ui.mt.font_body);
  lv_obj_set_size(l, r.w - 16, lh);
  lv_obj_set_pos(l, 8, (r.h - lh) / 2);
  ui_set_hidden(b, false);
}

/* Returns the top edge of the option row(s), or the safe-area bottom. */
static int32_t place_options(const ui_model_t *m) {
  const gadget_rect_t s = g_ui.mt.safe;
  if (!m->ask.answerable || m->ask.n_options == 0) {
    place_note(UI_COPY_ASK_ELSEWHERE);
    return s.y + s.h - text_h(UI_COPY_ASK_ELSEWHERE, g_ui.mt.font_small, s.w, 3);
  }
  int32_t top = s.y + s.h;
  bool touch = (g_ui.board->input_mask & GADGET_INPUT_TOUCH) != 0;
  if (touch) {
    for (int i = 0; i < m->ask.n_options && i < UI_ASK_OPTIONS_MAX; i++) {
      gadget_rect_t r = m->ask.options[i].rect;
      style_option(i, &m->ask.options[i], r);
      if (r.y < top) top = r.y;
    }
    return top;
  }
  /* Button boards: TALK answers option 1, CANCEL option 2 (spec 5.4). */
  static const char *const hints[2] = {"TALK", "CANCEL"};
  int n = m->ask.n_options > 2 ? 2 : m->ask.n_options;
  int32_t hint_h = line_h(g_ui.mt.font_tiny);
  int32_t pill_h = line_h(g_ui.mt.font_body) + 12;
  int32_t y = s.y + s.h - hint_h - 2 - pill_h;
  int32_t gap = g_ui.mt.pad;
  int32_t w = n == 1 ? s.w : (s.w - gap) / 2;
  for (int i = 0; i < n; i++) {
    gadget_rect_t r = {(int16_t)(s.x + i * (w + gap)), (int16_t)y, (int16_t)w, (int16_t)pill_h};
    style_option(i, &m->ask.options[i], r);
    lv_label_set_text(g_ui.opt_hint[i], hints[i]);
    lv_obj_set_size(g_ui.opt_hint[i], w, hint_h);
    lv_obj_set_pos(g_ui.opt_hint[i], r.x, y + pill_h + 2);
    ui_set_hidden(g_ui.opt_hint[i], false);
  }
  return y;
}

static void apply_ask(const ui_model_t *m) {
  int32_t pad = g_ui.mt.pad;
  int32_t y = place_title(m->ask.title) + pad;
  int32_t bottom = place_options(m) - pad;
  place_body(m->ask.body, y, bottom);
  g_ui.ask_locked = -1;
}

static void apply_card(const ui_model_t *m) {
  const gadget_rect_t s = g_ui.mt.safe;
  int32_t y = place_title(m->card.title) + g_ui.mt.pad;
  place_body(m->card.body, y, s.y + s.h);
}

static void apply_update(const ui_model_t *m) {
  const gadget_rect_t s = g_ui.mt.safe;
  char text[64];
  ui_copy_update(m, text, sizeof text);
  int32_t th = line_h(g_ui.mt.font_title);
  int32_t bar_h = g_ui.mt.large ? 12 : 8;
  int32_t y = s.y + (s.h - th - g_ui.mt.pad - bar_h) / 2;
  lv_label_set_text(g_ui.title, text);
  lv_obj_set_size(g_ui.title, s.w, th);
  lv_obj_set_pos(g_ui.title, s.x, y);
  ui_set_hidden(g_ui.title, false);
  lv_obj_set_size(g_ui.bar, s.w - 2 * g_ui.mt.pad, bar_h);
  lv_obj_set_pos(g_ui.bar, s.x + g_ui.mt.pad, y + th + g_ui.mt.pad);
  lv_bar_set_value(g_ui.bar, m->update.phase == UI_UPDATE_RECEIVING ? m->update.pct : 100, LV_ANIM_OFF);
  ui_set_hidden(g_ui.bar, false);
}

static void apply_image(const ui_model_t *m) {
  if (!m->image.pixels || m->image.w == 0 || m->image.h == 0) return;
  if (m->image.pixels_rev != g_ui.image_rev || lv_image_get_src(g_ui.image) == NULL) {
    g_ui.image_slot ^= 1u;
    lv_image_dsc_t *d = &g_ui.image_dsc[g_ui.image_slot];
    /* A slot is reused every second picture, maybe with another size. LVGL
     * keys its image and header caches by this pointer, so
     * ui_lv_requirements.h requires both caches off. */
    memset(d, 0, sizeof *d);
    d->header.magic = LV_IMAGE_HEADER_MAGIC;
    d->header.cf = LV_COLOR_FORMAT_RGB565;
    d->header.w = m->image.w;
    d->header.h = m->image.h;
    d->header.stride = (uint32_t)m->image.w * 2u;
    d->data_size = (uint32_t)m->image.w * m->image.h * 2u;
    d->data = (const uint8_t *)m->image.pixels;
    lv_image_set_src(g_ui.image, d);
    g_ui.image_rev = m->image.pixels_rev;
  }
  lv_obj_set_pos(g_ui.image, (g_ui.mt.w - m->image.w) / 2, (g_ui.mt.h - m->image.h) / 2);
  ui_set_hidden(g_ui.image, false);
}

static void apply_battery(const ui_model_t *m) {
  if (!g_ui.board->has_battery || !m->battery.present) {
    ui_set_hidden(g_ui.battery, true);
    return;
  }
  uint8_t pct = m->battery.pct > 100 ? 100 : m->battery.pct;
  uint32_t c = m->battery.charging ? UI_COLOR_ACCENT : pct <= 10 ? UI_COLOR_BAD : pct <= 25 ? UI_COLOR_WARN : UI_COLOR_OK;
  lv_arc_set_value(g_ui.battery, pct);
  lv_obj_set_style_arc_color(g_ui.battery, lv_color_hex(c), LV_PART_INDICATOR);
  ui_set_hidden(g_ui.battery, false);
}

static void apply_toast(const ui_model_t *m) {
  if (!m->toast.visible) {
    ui_set_hidden(g_ui.toast, true);
    return;
  }
  lv_label_set_text(g_ui.toast_name, m->toast.bot_name);
  lv_label_set_text(g_ui.toast_text, m->toast.text);
  ui_set_hidden(g_ui.toast, false);
}

void ui_screens_apply(const ui_model_t *m) {
  ui_metrics_t *mt = &g_ui.mt;
  ui_copy_t copy;
  char line[128];
  hide_all();
  g_ui.applies++;
  g_ui.screen = m->screen;
  g_ui.status[0] = '\0';
  g_ui.mic_level = -1;
  switch (m->screen) {
  case UI_SCREEN_BOOT:
  case UI_SCREEN__COUNT:
    break;
  case UI_SCREEN_SETUP:
    ui_copy_setup(m, &copy);
    show_caption(copy.caption, mt->font_small, UI_COLOR_INK, LV_TEXT_ALIGN_CENTER, copy.host, copy.status, mt->font_tiny,
                 UI_COLOR_DIM);
    break;
  case UI_SCREEN_OFFLINE:
    ui_copy_offline(m, &copy);
    show_caption(copy.caption, mt->font_small, UI_COLOR_INK, LV_TEXT_ALIGN_CENTER, copy.host, copy.status, mt->font_tiny,
                 UI_COLOR_MUTE);
    snprintf(g_ui.status, sizeof g_ui.status, "%s", copy.status);
    break;
  case UI_SCREEN_IDLE:
    ui_copy_idle(m, line, sizeof line);
    show_caption(line, mt->font_title, UI_COLOR_INK, LV_TEXT_ALIGN_CENTER, NULL, NULL, NULL, 0);
    break;
  case UI_SCREEN_LISTENING:
    ui_set_hidden(g_ui.ring, false);
    break;
  case UI_SCREEN_THINKING:
    show_caption(m->thinking.heard, mt->font_body, UI_COLOR_INK, LV_TEXT_ALIGN_CENTER, NULL,
                 m->thinking.working, mt->font_small, UI_COLOR_ACCENT);
    ui_pager_show_tail(&g_ui.caption);
    break;
  case UI_SCREEN_SPEAKING:
    show_caption(m->reply.text, mt->font_body, UI_COLOR_INK, LV_TEXT_ALIGN_LEFT, NULL, NULL, NULL, 0);
    break;
  case UI_SCREEN_REPLY:
    if (m->reply.failed) {
      show_caption(m->reply.reason, mt->font_body, UI_COLOR_BAD, LV_TEXT_ALIGN_CENTER, NULL, NULL, NULL, 0);
    } else {
      show_caption(m->reply.text, mt->font_body, UI_COLOR_INK, LV_TEXT_ALIGN_LEFT, NULL, NULL, NULL, 0);
      ui_pager_show_tail(&g_ui.caption);
    }
    break;
  case UI_SCREEN_ASK:
    apply_ask(m);
    break;
  case UI_SCREEN_CARD:
    apply_card(m);
    break;
  case UI_SCREEN_IMAGE:
    apply_image(m);
    break;
  case UI_SCREEN_UPDATE:
    apply_update(m);
    break;
  }
  apply_battery(m);
  apply_toast(m);
}

void ui_screens_apply_time(const ui_model_t *m) {
  switch (g_ui.screen) {
  case UI_SCREEN_SETUP:
    ui_pager_show_rotating(&g_ui.caption, m->now_ms);
    break;
  case UI_SCREEN_OFFLINE: {
    ui_copy_t copy;
    ui_copy_offline(m, &copy);
    if (strcmp(copy.status, g_ui.status) != 0 && copy.status[0] && g_ui.status[0]) {
      lv_label_set_text(g_ui.caption.status, copy.status);
      snprintf(g_ui.status, sizeof g_ui.status, "%s", copy.status);
    }
    ui_pager_show_rotating(&g_ui.caption, m->now_ms);
    break;
  }
  case UI_SCREEN_LISTENING: {
    char digit[4];
    ui_copy_countdown(m, digit, sizeof digit);
    ui_set_hidden(g_ui.countdown, digit[0] == '\0');
    if (digit[0] && strcmp(lv_label_get_text(g_ui.countdown), digit) != 0) lv_label_set_text(g_ui.countdown, digit);
    if ((int16_t)m->mic_level != g_ui.mic_level) {
      int32_t w = (g_ui.mt.large ? 4 : 3) + (int32_t)m->mic_level * (g_ui.mt.large ? 14 : 9) / 255;
      lv_obj_set_style_arc_width(g_ui.ring, w, LV_PART_INDICATOR);
      g_ui.mic_level = m->mic_level;
    }
    break;
  }
  case UI_SCREEN_SPEAKING:
    ui_pager_show_progress(&g_ui.caption, m->reply.speak_elapsed_ms, m->reply.speak_total_ms);
    break;
  case UI_SCREEN_ASK: {
    int16_t locked = m->now_ms < m->ask.locked_until_ms ? 1 : 0;
    if (locked != g_ui.ask_locked) {
      for (int i = 0; i < UI_ASK_OPTIONS_MAX; i++) {
        lv_opa_t opa = LV_OPA_COVER;
        if (locked) opa = LV_OPA_50;
        else if (m->ask.chosen >= 0 && m->ask.chosen != i) opa = LV_OPA_30;
        lv_obj_set_style_opa(g_ui.opt[i], opa, 0);
      }
      g_ui.ask_locked = locked;
    }
    break;
  }
  default:
    break;
  }
}
