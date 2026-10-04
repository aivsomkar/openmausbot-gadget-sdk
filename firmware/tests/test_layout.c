/* firmware/tests/test_layout.c */
/* SPDX-License-Identifier: Apache-2.0 */
/* Board table (core/src/boards.c) and the pure layout functions
 * (core/src/ui_layout.c) that core and the LVGL UI share. */
#include <string.h>
#include "gadget_board.h"
#include "gadget_ui.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

static bool inside(gadget_rect_t outer, gadget_rect_t r) {
  return r.x >= outer.x && r.y >= outer.y && r.x + r.w <= outer.x + outer.w && r.y + r.h <= outer.y + outer.h;
}

static bool overlap(gadget_rect_t a, gadget_rect_t b) {
  return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h;
}

static void test_board_table_matches_contract(void) {
  const gadget_board_t *b = gadget_board_by_id("amoled-175c");
  TEST_ASSERT_NOT_NULL(b);
  TEST_ASSERT_EQUAL_STRING("Waveshare ESP32-S3-Touch-AMOLED-1.75C", b->display_name);
  TEST_ASSERT_EQUAL_UINT16(466, b->screen_w);
  TEST_ASSERT_TRUE(b->screen_round);
  TEST_ASSERT_EQUAL_UINT32(GADGET_INPUT_TOUCH | GADGET_INPUT_TALK | GADGET_INPUT_CANCEL, b->input_mask);
  TEST_ASSERT_EQUAL_INT(GADGET_ART_S240, b->art_profile);
  b = gadget_board_by_id("amoled-175");
  TEST_ASSERT_EQUAL_UINT32(GADGET_INPUT_TOUCH | GADGET_INPUT_TALK, b->input_mask);
  b = gadget_board_by_id("lcd-154");
  TEST_ASSERT_EQUAL_UINT16(240, b->screen_w);
  TEST_ASSERT_EQUAL_UINT16(200, b->image_w);
  TEST_ASSERT_TRUE(b->has_battery);
  TEST_ASSERT_EQUAL_INT(GADGET_ART_S150, b->art_profile);
  b = gadget_board_by_id("devkit");
  TEST_ASSERT_EQUAL_UINT32(24000, b->speaker_rate);
  TEST_ASSERT_FALSE(b->has_battery);
  TEST_ASSERT_EQUAL_UINT16(280, b->image_w);
  TEST_ASSERT_EQUAL_UINT16(200, b->image_h);
  TEST_ASSERT_NULL(gadget_board_by_id("nope"));
  TEST_ASSERT_NULL(gadget_board_by_id(NULL));
  size_t n = 0;
  while (gadget_board_at(n) != NULL) {
    const gadget_board_t *x = gadget_board_at(n);
    TEST_ASSERT_EQUAL_UINT32(6291456, x->ota_max);
    TEST_ASSERT_EQUAL_UINT32(16000, x->mic_rate);
    TEST_ASSERT_TRUE(strlen(x->id) <= 32);
    n++;
  }
  TEST_ASSERT_EQUAL_size_t(4, n);
}

static void test_safe_area(void) {
  gadget_rect_t r = ui_safe_area(gadget_board_by_id("amoled-175c"));
  /* inscribed square 329 px (466 * 181 / 256), centred, minus an 8 px margin */
  TEST_ASSERT_EQUAL_INT16(76, r.x);
  TEST_ASSERT_EQUAL_INT16(76, r.y);
  TEST_ASSERT_EQUAL_INT16(313, r.w);
  TEST_ASSERT_EQUAL_INT16(313, r.h);
  r = ui_safe_area(gadget_board_by_id("devkit"));
  TEST_ASSERT_EQUAL_INT16(8, r.x);
  TEST_ASSERT_EQUAL_INT16(8, r.y);
  TEST_ASSERT_EQUAL_INT16(304, r.w);
  TEST_ASSERT_EQUAL_INT16(224, r.h);
}

static void test_ask_buttons_fit_and_do_not_overlap(void) {
  for (size_t bi = 0; gadget_board_at(bi) != NULL; bi++) {
    const gadget_board_t *b = gadget_board_at(bi);
    gadget_rect_t safe = ui_safe_area(b);
    for (uint8_t n = 1; n <= UI_ASK_OPTIONS_MAX; n++) {
      gadget_rect_t r[UI_ASK_OPTIONS_MAX];
      ui_layout_ask(b, n, r);
      for (uint8_t i = 0; i < n; i++) {
        TEST_ASSERT_TRUE(r[i].w >= 40 && r[i].h >= 40);
        TEST_ASSERT_TRUE_MESSAGE(inside(safe, r[i]), b->id);
        for (uint8_t j = 0; j < i; j++) TEST_ASSERT_FALSE(overlap(r[i], r[j]));
      }
      for (uint8_t i = n; i < UI_ASK_OPTIONS_MAX; i++) TEST_ASSERT_EQUAL_INT16(0, r[i].w);
    }
  }
}

static void test_ask_rows_fill_left_to_right(void) {
  gadget_rect_t r[UI_ASK_OPTIONS_MAX];
  const gadget_board_t *b = gadget_board_by_id("amoled-175c");
  ui_layout_ask(b, 2, r);
  TEST_ASSERT_EQUAL_INT16(r[0].y, r[1].y);
  TEST_ASSERT_TRUE(r[0].x < r[1].x);
  ui_layout_ask(b, 3, r);
  TEST_ASSERT_TRUE(r[2].y > r[0].y);              /* the odd one out sits alone on the bottom row */
  TEST_ASSERT_EQUAL_INT16(ui_safe_area(b).w, r[2].w);
  ui_layout_ask(b, 0, r);
  TEST_ASSERT_EQUAL_INT16(0, r[0].w);
}

static void test_hit_test(void) {
  static ui_model_t m;
  memset(&m, 0, sizeof m);
  m.screen = UI_SCREEN_ASK;
  m.ask.n_options = 2;
  m.ask.answerable = true;
  gadget_rect_t r[UI_ASK_OPTIONS_MAX];
  ui_layout_ask(gadget_board_by_id("amoled-175c"), 2, r);
  m.ask.options[0].rect = r[0];
  m.ask.options[1].rect = r[1];
  TEST_ASSERT_EQUAL_INT(0, ui_hit_test(&m, (int16_t)(r[0].x + r[0].w / 2), (int16_t)(r[0].y + r[0].h / 2)));
  TEST_ASSERT_EQUAL_INT(1, ui_hit_test(&m, (int16_t)(r[1].x + 1), (int16_t)(r[1].y + 1)));
  TEST_ASSERT_EQUAL_INT(-1, ui_hit_test(&m, 0, 0));
  m.ask.answerable = false;
  TEST_ASSERT_EQUAL_INT(-1, ui_hit_test(&m, (int16_t)(r[0].x + 1), (int16_t)(r[0].y + 1)));
  m.ask.answerable = true;
  m.screen = UI_SCREEN_IDLE;
  TEST_ASSERT_EQUAL_INT(-1, ui_hit_test(&m, (int16_t)(r[0].x + 1), (int16_t)(r[0].y + 1)));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_board_table_matches_contract);
  RUN_TEST(test_safe_area);
  RUN_TEST(test_ask_buttons_fit_and_do_not_overlap);
  RUN_TEST(test_ask_rows_fill_left_to_right);
  RUN_TEST(test_hit_test);
  return UNITY_END();
}
