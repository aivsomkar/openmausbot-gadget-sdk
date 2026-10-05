/* SPDX-License-Identifier: Apache-2.0 */
/* The LVGL display seam (contract 2.16) for GADGET_WITH_LVGL builds:
 * --headless: LVGL's in-memory test display (RGB565) on the virtual clock,
 *             snapshots through lv_test_screenshot_compare (bundled lodepng);
 * otherwise:  the SDL window (sim_sdl.c), zoomed, on SDL's real clock.
 * Both use one custom pointer device that reads the shared touch state the
 * script (`touch`/`release`) and the mouse both drive, and on round boards a
 * black ring on lv_layer_sys() masks the corners like the device's glass. */
#include "sim_display.h"

#include <stdatomic.h>
#include <stdio.h>
#include <unistd.h>

#include "lvgl.h"
#include "ui_lv_compat.h"
#if defined(GADGET_WITH_SDL)
#include "sim_sdl.h"
#endif

static struct {
  const gadget_board_t *board;
  bool headless;
  const char *snapshot_dir;
  lv_display_t *disp;
  lv_indev_t *pointer;
} s;

/* bit 31: pressed; bits 16..30: x; bits 0..15: y */
static atomic_uint_fast32_t s_touch;

static void pointer_read(lv_indev_t *indev, lv_indev_data_t *data) {
  (void)indev;
  uint32_t v = (uint32_t)atomic_load(&s_touch);
  data->point.x = (int32_t)((v >> 16) & 0x7FFFu);
  data->point.y = (int32_t)(v & 0xFFFFu);
  data->state = (v & 0x80000000u) ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

void sim_display_touch(bool pressed, int16_t x, int16_t y) {
  uint32_t v = ((uint32_t)(x < 0 ? 0 : x) & 0x7FFFu) << 16 | ((uint32_t)(y < 0 ? 0 : y) & 0xFFFFu);
  if (pressed) v |= 0x80000000u;
  atomic_store(&s_touch, v);
}

/* A full-screen black ring whose inner edge is the screen's circle. */
static void round_mask(int32_t w, int32_t h) {
  int32_t b = w * 45 / 200 + 2; /* > (sqrt(2) - 1) * r, so the corners are covered */
  lv_obj_t *m = lv_obj_create(lv_layer_sys());
  lv_obj_remove_style_all(m);
  lv_obj_set_size(m, w + 2 * b, h + 2 * b);
  lv_obj_set_pos(m, -b, -b);
  lv_obj_set_style_radius(m, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_opa(m, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(m, b, 0);
  lv_obj_set_style_border_color(m, lv_color_black(), 0);
  lv_obj_set_style_border_opa(m, LV_OPA_COVER, 0);
  ui_set_clickable(m, false);
  ui_set_scrollable(m, false);
}

int sim_display_init(const gadget_board_t *board, const sim_display_opts_t *opts) {
  s.board = board;
  s.headless = opts->headless;
  s.snapshot_dir = opts->snapshot_dir;
  atomic_store(&s_touch, 0);
  lv_init();
  if (s.headless) {
    s.disp = lv_test_display_create(board->screen_w, board->screen_h);
    if (s.disp) lv_display_set_color_format(s.disp, LV_COLOR_FORMAT_RGB565);
  } else {
#if defined(GADGET_WITH_SDL)
    s.disp = sim_sdl_window_create(board, opts->zoom);
#else
    fprintf(stderr, "gadget-sim: this build has no SDL window; use --headless\n");
    s.disp = NULL;
#endif
  }
  if (!s.disp) {
    lv_deinit();
    return -1;
  }
  s.pointer = lv_indev_create();
  lv_indev_set_type(s.pointer, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(s.pointer, pointer_read);
  lv_indev_set_display(s.pointer, s.disp);
  if (board->screen_round) round_mask(board->screen_w, board->screen_h);
  return 0;
}

void sim_display_advance(uint32_t elapsed_ms) {
  if (s.headless) lv_tick_inc(elapsed_ms);
}

void sim_display_poll(void) {
#if defined(GADGET_WITH_SDL)
  if (!s.headless && s.disp) sim_sdl_pump();
#endif
}

bool sim_display_quit_requested(void) {
#if defined(GADGET_WITH_SDL)
  if (!s.headless && s.disp) return sim_sdl_quit_requested();
#endif
  return false;
}

int sim_display_snapshot(const char *name) {
  if (!s.disp) return -1;
  char path[256];
  int n = snprintf(path, sizeof path, "%s/%s.png", s.snapshot_dir ? s.snapshot_dir : ".", name);
  if (n < 0 || (size_t)n >= sizeof path || (size_t)n > 120) {
    fprintf(stderr, "gadget-sim: snapshot path too long: %s/%s.png\n", s.snapshot_dir, name);
    return 0;
  }
#if defined(GADGET_SNAPSHOT_UPDATE)
  unlink(path); /* LVGL writes a reference only when none exists */
#endif
  switch (lv_test_screenshot_compare(path)) {
  case LV_TEST_SCREENSHOT_RESULT_PASSED:
    return 1;
  case LV_TEST_SCREENSHOT_RESULT_NO_REFERENCE_IMAGE:
    return 2;
  default:
    return 0;
  }
}

void sim_display_deinit(void) {
  if (!s.disp) return;
#if defined(GADGET_WITH_SDL)
  if (!s.headless) {
    sim_sdl_shutdown(s.disp);
    s.disp = NULL;
  }
#endif
  lv_deinit();
  s.disp = NULL;
  s.pointer = NULL;
}
