/* SPDX-License-Identifier: Apache-2.0 */
/* LVGL 9.6.0 builds with firmware/ui/lv_conf.h, meets ui_lv_requirements.h,
 * and the flag wrappers in ui_lv_compat.h do what they say. */
#include "ui_lv_compat.h"
#include "ui_lv_requirements.h"
#include "unity.h"

static lv_display_t *disp;

void setUp(void) {
  lv_init();
  disp = lv_test_display_create(240, 240);
  lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);
}
void tearDown(void) { lv_deinit(); }

static void test_pinned_lvgl_version(void) {
  TEST_ASSERT_EQUAL_INT(9, LVGL_VERSION_MAJOR);
  TEST_ASSERT_EQUAL_INT(6, LVGL_VERSION_MINOR);
  TEST_ASSERT_EQUAL_INT(0, LVGL_VERSION_PATCH);
}

static void test_display_is_rgb565(void) {
  TEST_ASSERT_EQUAL_INT(LV_COLOR_FORMAT_RGB565, lv_display_get_color_format(disp));
}

static void test_hidden_wrapper(void) {
  lv_obj_t *o = ui_box(lv_screen_active());
  TEST_ASSERT_FALSE(ui_is_hidden(o));
  ui_set_hidden(o, true);
  TEST_ASSERT_TRUE(ui_is_hidden(o));
  ui_set_hidden(o, false);
  TEST_ASSERT_FALSE(ui_is_hidden(o));
}

static void test_box_is_inert(void) {
  lv_obj_t *o = ui_box(lv_screen_active());
  TEST_ASSERT_FALSE(lv_obj_is_clickable(o));
  TEST_ASSERT_FALSE(lv_obj_is_scrollable(o));
  ui_set_clickable(o, true);
  ui_set_scrollable(o, true);
  TEST_ASSERT_TRUE(lv_obj_is_clickable(o));
  TEST_ASSERT_TRUE(lv_obj_is_scrollable(o));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_pinned_lvgl_version);
  RUN_TEST(test_display_is_rgb565);
  RUN_TEST(test_hidden_wrapper);
  RUN_TEST(test_box_is_inert);
  return UNITY_END();
}
