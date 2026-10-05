/* SPDX-License-Identifier: Apache-2.0 */
/* The headless LVGL display seam: virtual clock, round mask, the shared
 * pointer, snapshot results. No SDL is touched in headless mode. */
#include <string.h>

#include "gadget_board.h"
#include "gadget_ui.h"
#include "lvgl.h"
#include "sim_display.h"
#include "sim_hal.h"
#include "unity.h"

void sim_post_event(const gadget_event_t *ev) { (void)ev; }

static bool g_open;

static void open_headless(const char *id) {
  const gadget_board_t *b = gadget_board_by_id(id);
  TEST_ASSERT_NOT_NULL(b);
  sim_display_opts_t o = {true, 1.0f, "firmware/tests/snapshots/none"};
  TEST_ASSERT_EQUAL_INT(0, sim_display_init(b, &o));
  g_open = true;
}

void setUp(void) {}
void tearDown(void) {
  if (g_open) sim_display_deinit();
  g_open = false;
}

static void test_virtual_clock_moves_only_when_advanced(void) {
  open_headless("lcd-154");
  uint32_t t0 = lv_tick_get();
  sim_display_advance(10);
  sim_display_advance(10);
  TEST_ASSERT_EQUAL_UINT32(t0 + 20, lv_tick_get());
  TEST_ASSERT_FALSE(sim_display_quit_requested());
}

static void test_round_boards_get_a_mask_square_ones_do_not(void) {
  open_headless("amoled-175c");
  TEST_ASSERT_EQUAL_UINT32(1, lv_obj_get_child_count(lv_layer_sys()));
  sim_display_deinit();
  g_open = false;
  open_headless("devkit");
  TEST_ASSERT_EQUAL_UINT32(0, lv_obj_get_child_count(lv_layer_sys()));
}

static void test_script_touch_drives_the_pointer(void) {
  open_headless("amoled-175c");
  lv_indev_t *in = lv_indev_get_next(NULL);
  TEST_ASSERT_NOT_NULL(in);
  sim_display_touch(true, 120, 300);
  lv_indev_read(in);
  lv_point_t p;
  lv_indev_get_point(in, &p);
  TEST_ASSERT_EQUAL_INT32(120, p.x);
  TEST_ASSERT_EQUAL_INT32(300, p.y);
  TEST_ASSERT_EQUAL_INT(LV_INDEV_STATE_PRESSED, lv_indev_get_state(in));
  sim_display_touch(false, 120, 300);
  lv_indev_read(in);
  TEST_ASSERT_EQUAL_INT(LV_INDEV_STATE_RELEASED, lv_indev_get_state(in));
}

static void test_missing_reference_is_reported(void) {
  open_headless("lcd-154");
  TEST_ASSERT_EQUAL_INT(GADGET_OK, ui_init(gadget_board_by_id("lcd-154"), 1));
#if !defined(GADGET_SNAPSHOT_UPDATE)
  TEST_ASSERT_EQUAL_INT(2, sim_display_snapshot("no-such-golden"));
#endif
  ui_deinit();
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_virtual_clock_moves_only_when_advanced);
  RUN_TEST(test_round_boards_get_a_mask_square_ones_do_not);
  RUN_TEST(test_script_touch_drives_the_pointer);
  RUN_TEST(test_missing_reference_is_reported);
  return UNITY_END();
}
