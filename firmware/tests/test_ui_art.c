/* SPDX-License-Identifier: Apache-2.0 */
/* The generated art (tools/art) is internally consistent and within budget. */
#include <string.h>

#include "art/maus_art.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

static uint32_t layer_bytes(const maus_art_t *a, const maus_layer_t *l) {
  TEST_ASSERT_NOT_NULL(l->img);
  TEST_ASSERT_EQUAL_UINT32(LV_IMAGE_HEADER_MAGIC, l->img->header.magic);
  TEST_ASSERT_TRUE(l->x >= 0 && l->y >= 0);
  TEST_ASSERT_TRUE(l->x + (int)l->img->header.w <= a->w);
  TEST_ASSERT_TRUE(l->y + (int)l->img->header.h <= a->h);
  return l->img->data_size;
}

static void check_profile(const maus_art_t *a, const char *name, uint16_t w, uint16_t h, uint32_t budget,
                          uint32_t budget_fallback) {
  TEST_ASSERT_EQUAL_STRING(name, a->profile);
  TEST_ASSERT_EQUAL_UINT16(w, a->w);
  TEST_ASSERT_EQUAL_UINT16(h, a->h);
  TEST_ASSERT_EQUAL_UINT32(LV_COLOR_FORMAT_RGB565A8, a->body->header.cf);
  TEST_ASSERT_EQUAL_UINT32((uint32_t)w * h * 3u, a->body->data_size);
  uint32_t total = a->body->data_size;
  bool a8_eyes = a->eyes[0][0].img->header.cf == LV_COLOR_FORMAT_A8;
  for (int e = 0; e < MAUS_EXPR_COUNT; e++) {
    if (e > 0) TEST_ASSERT_TRUE(a->expr_ids[e] > a->expr_ids[e - 1]); /* sorted, unique */
    for (int s = 0; s < MAUS_BLINK_STEPS; s++) {
      const maus_layer_t *l = &a->eyes[e][s];
      TEST_ASSERT_EQUAL_UINT32(a8_eyes ? LV_COLOR_FORMAT_A8 : LV_COLOR_FORMAT_RGB565A8, l->img->header.cf);
      total += layer_bytes(a, l);
    }
    TEST_ASSERT_EQUAL_UINT32(LV_COLOR_FORMAT_RGB565A8, a->mouth[e].img->header.cf);
    total += layer_bytes(a, &a->mouth[e]);
  }
  for (int k = 0; k < MAUS_SPEAK_EXPRS; k++) {
    TEST_ASSERT_TRUE(a->speak_expr[k] < MAUS_EXPR_COUNT);
    for (int l = 0; l < MAUS_SPEAK_LEVELS; l++) total += layer_bytes(a, &a->speak[k][l]);
  }
  TEST_ASSERT_EQUAL_UINT32(total, a->total_bytes);
  TEST_ASSERT_TRUE_MESSAGE(a->total_bytes <= (a8_eyes ? budget : budget_fallback), "art over its byte budget");
  for (int s = UI_MAUS_IDLE; s < UI_MAUS__COUNT; s++) {
    const maus_state_def_t *d = &a->states[s];
    TEST_ASSERT_TRUE(d->pool_len >= 1);
    for (int i = 0; i < d->pool_len; i++) TEST_ASSERT_TRUE(d->pool[i] < MAUS_EXPR_COUNT);
    TEST_ASSERT_TRUE(d->cad_min_ms > 0 && d->cad_min_ms <= d->cad_max_ms);
    TEST_ASSERT_TRUE(d->blink_min_ms <= d->blink_max_ms);
  }
}

static void test_s240_profile(void) {
  check_profile(&maus_art_s240, "s240", 201, 240, 524288u, 819200u);
}

static void test_s150_profile(void) {
  check_profile(&maus_art_s150, "s150", 125, 150, 262144u, 327680u);
}

static void test_profiles_by_board_art_profile(void) {
  TEST_ASSERT_EQUAL_PTR(&maus_art_s240, maus_art_for(GADGET_ART_S240));
  TEST_ASSERT_EQUAL_PTR(&maus_art_s150, maus_art_for(GADGET_ART_S150));
  TEST_ASSERT_NULL(maus_art_for((gadget_art_profile_t)7));
}

static void test_state_pools_match_the_contract(void) {
  /* contract 2.15: app expression numbers per state */
  static const uint8_t idle[] = {6, 0, 8}, listening[] = {1, 10, 19}, thinking[] = {17, 8, 16, 14, 5},
                       working[] = {10, 7, 16, 11}, sleeping[] = {22, 13, 4}, curious[] = {21, 3, 0, 15},
                       notifying[] = {21, 3, 0}, alerting[] = {21, 3}, speaking[] = {19, 6};
  static const struct { ui_maus_state_t s; const uint8_t *ids; uint8_t n; } want[] = {
    {UI_MAUS_IDLE, idle, 3}, {UI_MAUS_LISTENING, listening, 3}, {UI_MAUS_THINKING, thinking, 5},
    {UI_MAUS_WORKING, working, 4}, {UI_MAUS_SLEEPING, sleeping, 3}, {UI_MAUS_CURIOUS, curious, 4},
    {UI_MAUS_NOTIFYING, notifying, 3}, {UI_MAUS_ALERTING, alerting, 2}, {UI_MAUS_SPEAKING, speaking, 2},
  };
  const maus_art_t *a = &maus_art_s240;
  for (size_t i = 0; i < sizeof want / sizeof want[0]; i++) {
    const maus_state_def_t *d = &a->states[want[i].s];
    TEST_ASSERT_EQUAL_UINT8(want[i].n, d->pool_len);
    for (uint8_t k = 0; k < want[i].n; k++) TEST_ASSERT_EQUAL_UINT8(want[i].ids[k], a->expr_ids[d->pool[k]]);
  }
  TEST_ASSERT_EQUAL_UINT8(19, a->expr_ids[a->speak_expr[0]]);
  TEST_ASSERT_EQUAL_UINT8(6, a->expr_ids[a->speak_expr[1]]);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_s240_profile);
  RUN_TEST(test_s150_profile);
  RUN_TEST(test_profiles_by_board_art_profile);
  RUN_TEST(test_state_pools_match_the_contract);
  return UNITY_END();
}
