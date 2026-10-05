/* SPDX-License-Identifier: Apache-2.0 */
#include "pl_otaq.h"
#include "unity.h"

static pl_otaq_t q;

void setUp(void) { pl_otaq_init(&q, 10000, PL_OTAQ_CAP); }
void tearDown(void) {}

static void test_contiguous_writes_until_full(void) {
  pl_otaq_init(&q, 200000, PL_OTAQ_CAP);
  uint32_t off = 0;
  for (int i = 0; i < 17; i++) {
    TEST_ASSERT_EQUAL(GADGET_OK, pl_otaq_admit(&q, off, 4096));
    off += 4096;
  }
  TEST_ASSERT_EQUAL_UINT32(69632, q.queued);
  TEST_ASSERT_EQUAL(GADGET_ERR_BUSY, pl_otaq_admit(&q, off, 1));
  TEST_ASSERT_EQUAL_UINT32(off, q.next); /* BUSY does not advance */
  pl_otaq_done(&q, 4096);
  TEST_ASSERT_EQUAL(GADGET_OK, pl_otaq_admit(&q, off, 4096));
}

static void test_gaps_overlaps_and_sizes(void) {
  TEST_ASSERT_EQUAL(GADGET_ERR_ARG, pl_otaq_admit(&q, 4, 10));   /* gap */
  TEST_ASSERT_EQUAL(GADGET_OK, pl_otaq_admit(&q, 0, 4096));
  TEST_ASSERT_EQUAL(GADGET_ERR_ARG, pl_otaq_admit(&q, 0, 4096)); /* replay */
  TEST_ASSERT_EQUAL(GADGET_ERR_ARG, pl_otaq_admit(&q, 4096, 0));
  TEST_ASSERT_EQUAL(GADGET_ERR_ARG, pl_otaq_admit(&q, 4096, 4097));
  TEST_ASSERT_EQUAL(GADGET_OK, pl_otaq_admit(&q, 4096, 4096));
  TEST_ASSERT_EQUAL(GADGET_ERR_LIMIT, pl_otaq_admit(&q, 8192, 4096)); /* past 10000 */
  TEST_ASSERT_EQUAL(GADGET_OK, pl_otaq_admit(&q, 8192, 1808));
  pl_otaq_done(&q, 999999);
  TEST_ASSERT_EQUAL_UINT32(0, q.queued);
}

static void test_unadmit_rolls_back(void) {
  TEST_ASSERT_EQUAL(GADGET_OK, pl_otaq_admit(&q, 0, 4096));
  TEST_ASSERT_EQUAL(GADGET_OK, pl_otaq_admit(&q, 4096, 100));
  pl_otaq_unadmit(&q, 100);
  TEST_ASSERT_EQUAL_UINT32(4096, q.next);
  TEST_ASSERT_EQUAL_UINT32(4096, q.queued);
  TEST_ASSERT_EQUAL(GADGET_OK, pl_otaq_admit(&q, 4096, 100)); /* the same chunk again */
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_contiguous_writes_until_full);
  RUN_TEST(test_gaps_overlaps_and_sizes);
  RUN_TEST(test_unadmit_rolls_back);
  return UNITY_END();
}
