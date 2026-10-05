/* SPDX-License-Identifier: Apache-2.0 */
#include "pl_audio.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

static void test_ring_wraps_and_reports_partial_writes(void) {
  int16_t store[5];
  pl_ring_t r;
  pl_ring_init(&r, store, 5);
  const int16_t a[4] = {1, 2, 3, 4};
  TEST_ASSERT_EQUAL_size_t(4, pl_ring_write(&r, a, 4));
  int16_t out[5] = {0};
  TEST_ASSERT_EQUAL_size_t(3, pl_ring_read(&r, out, 3));
  TEST_ASSERT_EQUAL_INT16(3, out[2]);
  const int16_t b[5] = {5, 6, 7, 8, 9};
  TEST_ASSERT_EQUAL_size_t(4, pl_ring_write(&r, b, 5)); /* room for 4 */
  TEST_ASSERT_EQUAL_size_t(5, pl_ring_count(&r));
  TEST_ASSERT_EQUAL_size_t(5, pl_ring_read(&r, out, 9));
  const int16_t want[5] = {4, 5, 6, 7, 8};
  TEST_ASSERT_EQUAL_INT16_ARRAY(want, out, 5);
  TEST_ASSERT_EQUAL_size_t(0, pl_ring_read(&r, out, 1));
}

static void test_ring_clear(void) {
  int16_t store[8];
  pl_ring_t r;
  pl_ring_init(&r, store, 8);
  const int16_t a[6] = {1, 2, 3, 4, 5, 6};
  pl_ring_write(&r, a, 6);
  pl_ring_clear(&r);
  TEST_ASSERT_EQUAL_size_t(0, pl_ring_count(&r));
  TEST_ASSERT_EQUAL_size_t(6, pl_ring_write(&r, a, 6));
}

static void test_samples_to_ms(void) {
  TEST_ASSERT_EQUAL_UINT32(20, pl_samples_to_ms(320, 16000));
  TEST_ASSERT_EQUAL_UINT32(1000, pl_samples_to_ms(24000, 24000));
  TEST_ASSERT_EQUAL_UINT32(0, pl_samples_to_ms(15, 16000));
  TEST_ASSERT_EQUAL_UINT32(0, pl_samples_to_ms(100, 0));
}

static void test_i32_to_i16_saturates(void) {
  const int32_t in[4] = {(int32_t)0x7fffff00, (int32_t)0x80000000, 1 << 14, -(1 << 15)};
  int16_t out[4];
  pl_pcm_from_i32(in, out, 4, 14);
  TEST_ASSERT_EQUAL_INT16(32767, out[0]);
  TEST_ASSERT_EQUAL_INT16(-32768, out[1]);
  TEST_ASSERT_EQUAL_INT16(1, out[2]);
  TEST_ASSERT_EQUAL_INT16(-2, out[3]);
}

static void test_volume_curve(void) {
  int16_t pcm[3] = {20000, -20000, 7};
  pl_pcm_volume(pcm, 3, 100);
  TEST_ASSERT_EQUAL_INT16(20000, pcm[0]);
  pl_pcm_volume(pcm, 3, 50);
  TEST_ASSERT_EQUAL_INT16(5000, pcm[0]);
  TEST_ASSERT_EQUAL_INT16(-5000, pcm[1]);
  pl_pcm_volume(pcm, 3, 0);
  TEST_ASSERT_EQUAL_INT16(0, pcm[0]);
  TEST_ASSERT_EQUAL_INT16(0, pcm[1]);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_ring_wraps_and_reports_partial_writes);
  RUN_TEST(test_ring_clear);
  RUN_TEST(test_samples_to_ms);
  RUN_TEST(test_i32_to_i16_saturates);
  RUN_TEST(test_volume_curve);
  return UNITY_END();
}
