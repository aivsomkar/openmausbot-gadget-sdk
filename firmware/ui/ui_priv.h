/* SPDX-License-Identifier: Apache-2.0 */
/* State shared by ui.c and ui_screens.c. Private to firmware/ui. */
#ifndef UI_PRIV_H
#define UI_PRIV_H

#include "gadget_ui.h"
#include "ui_lv_requirements.h"
#include "ui_copy.h"
#include "ui_maus.h"
#include "ui_pager.h"
#include "ui_theme.h"

typedef struct {
  const gadget_board_t *board;
  const maus_art_t *art;
  ui_metrics_t mt;
  lv_obj_t *root;

  /* Maus screens */
  ui_maus_t maus;
  lv_obj_t *ring;
  ui_pager_t caption;
  lv_obj_t *countdown;

  /* text screens: ask, card, update, image */
  lv_obj_t *title;
  lv_obj_t *body;
  lv_obj_t *note;
  lv_obj_t *opt[UI_ASK_OPTIONS_MAX];
  lv_obj_t *opt_label[UI_ASK_OPTIONS_MAX];
  lv_obj_t *opt_hint[UI_ASK_OPTIONS_MAX];
  lv_obj_t *bar;
  lv_obj_t *image;
  lv_image_dsc_t image_dsc[2]; /* alternated so a new picture is a new image source */
  uint8_t image_slot;
  uint32_t image_rev;

  /* on every screen */
  lv_obj_t *battery;
  lv_obj_t *toast;
  lv_obj_t *toast_name;
  lv_obj_t *toast_text;

  /* render cache */
  bool rendered;
  uint32_t rev;
  uint32_t applies;   /* ui_screens_apply() calls, for the render-cache test */
  ui_screen_t screen;
  uint8_t speak_level;
  int16_t mic_level;  /* -1 = ring width not drawn yet */
  int16_t ask_locked; /* -1 unknown, 0 unlocked, 1 locked */
  char status[UI_COPY_MAX]; /* last status text drawn (retry countdown) */
  /* Setup and Offline pages turn every UI_PAGE_ROTATE_MS from the moment
   * their copy appeared, so a new message opens on its first line. */
  uint64_t caption_t0;
  char caption_key[UI_COPY_MAX]; /* the copy caption_t0 belongs to; "" on other screens */
} ui_state_t;

extern ui_state_t g_ui;

/* ui_screens.c */
void ui_screens_create(void);
void ui_screens_apply(const ui_model_t *m);     /* the model changed (rev) */
void ui_screens_apply_time(const ui_model_t *m); /* every render: time-driven parts */

#endif /* UI_PRIV_H */
