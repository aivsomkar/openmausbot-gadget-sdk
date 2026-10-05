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

static void test_split_fills_one_block_at_a_time(void) {
  pl_otaq_split_t s = pl_otaq_split(0, 4096, false);
  TEST_ASSERT_EQUAL_UINT32(4096, s.head);
  TEST_ASSERT_EQUAL_UINT32(0, s.tail);
  TEST_ASSERT_TRUE(s.send_head);
  TEST_ASSERT_FALSE(s.send_tail);

  s = pl_otaq_split(0, 512, false); /* stays staged */
  TEST_ASSERT_EQUAL_UINT32(512, s.head);
  TEST_ASSERT_EQUAL_UINT32(0, s.tail);
  TEST_ASSERT_FALSE(s.send_head);
  TEST_ASSERT_FALSE(s.send_tail);

  s = pl_otaq_split(3584, 512, false); /* exactly fills the block */
  TEST_ASSERT_EQUAL_UINT32(512, s.head);
  TEST_ASSERT_TRUE(s.send_head);
  TEST_ASSERT_FALSE(s.send_tail);

  s = pl_otaq_split(4000, 200, false); /* spills into a new block, which waits */
  TEST_ASSERT_EQUAL_UINT32(96, s.head);
  TEST_ASSERT_EQUAL_UINT32(104, s.tail);
  TEST_ASSERT_TRUE(s.send_head);
  TEST_ASSERT_FALSE(s.send_tail);

  s = pl_otaq_split(4000, 200, true); /* spills, and the image ends: both go */
  TEST_ASSERT_EQUAL_UINT32(96, s.head);
  TEST_ASSERT_EQUAL_UINT32(104, s.tail);
  TEST_ASSERT_TRUE(s.send_head);
  TEST_ASSERT_TRUE(s.send_tail);

  s = pl_otaq_split(100, 50, true); /* a short last block is flushed */
  TEST_ASSERT_EQUAL_UINT32(50, s.head);
  TEST_ASSERT_EQUAL_UINT32(0, s.tail);
  TEST_ASSERT_TRUE(s.send_head);
  TEST_ASSERT_FALSE(s.send_tail);

  s = pl_otaq_split(4000, 96, true); /* fills and ends at once: one block */
  TEST_ASSERT_EQUAL_UINT32(96, s.head);
  TEST_ASSERT_EQUAL_UINT32(0, s.tail);
  TEST_ASSERT_TRUE(s.send_head);
  TEST_ASSERT_FALSE(s.send_tail);
}

/* hal_ota_write()'s bookkeeping, with a worker that never drains: the
 * 64-entry message queue, the byte accounting and the staging block. */
#define SIM_QUEUE_LEN 64

typedef struct {
  uint32_t staged; /* bytes in the block being filled */
  int msgs;        /* messages in the worker's queue */
  uint32_t sent;   /* bytes in those messages */
} sim_t;

static void sim_send(sim_t *s, bool last) {
  TEST_ASSERT_TRUE(s->staged > 0);
  TEST_ASSERT_TRUE(s->staged == GADGET_FW_CHUNK_MAX || last); /* full blocks only, except the end */
  s->msgs++;
  s->sent += s->staged;
  s->staged = 0;
}

static gadget_status_t sim_write(sim_t *s, uint32_t offset, size_t len) {
  if (SIM_QUEUE_LEN - s->msgs < 2) {
    return GADGET_ERR_BUSY;
  }
  gadget_status_t st = pl_otaq_admit(&q, offset, len);
  if (st != GADGET_OK) {
    return st;
  }
  bool last = offset + len == q.size;
  pl_otaq_split_t sp = pl_otaq_split(s->staged, len, last);
  TEST_ASSERT_EQUAL_UINT32(len, sp.head + sp.tail);
  s->staged += sp.head;
  TEST_ASSERT_TRUE(s->staged <= GADGET_FW_CHUNK_MAX);
  if (sp.send_head) {
    sim_send(s, last);
  }
  TEST_ASSERT_TRUE(sp.tail == 0 || sp.send_head);
  s->staged += sp.tail;
  if (sp.send_tail) {
    sim_send(s, last);
  }
  TEST_ASSERT_TRUE(s->staged < GADGET_FW_CHUNK_MAX);
  TEST_ASSERT_TRUE(!last || s->staged == 0);
  return GADGET_OK;
}

/* PROTOCOL.md: the host keeps at most 64 KiB sent but not acknowledged; a
 * worker that never drains never acknowledges, so the host sends 64 KiB. */
static void send_window(size_t chunk) {
  pl_otaq_init(&q, 200000, PL_OTAQ_CAP);
  sim_t s = {0};
  uint32_t off = 0;
  while (off < 64u * 1024u) {
    size_t n = 64u * 1024u - off < chunk ? 64u * 1024u - off : chunk;
    TEST_ASSERT_EQUAL(GADGET_OK, sim_write(&s, off, n));
    off += (uint32_t)n;
  }
  TEST_ASSERT_EQUAL_UINT32(64u * 1024u, off);
  TEST_ASSERT_TRUE(s.msgs <= 18);
  TEST_ASSERT_EQUAL_UINT32(off, s.sent + s.staged);
}

static void test_small_chunks_fill_the_window_without_busy(void) {
  send_window(512); /* 128 chunks */
  send_window(1);   /* 65536 chunks */
  send_window(1000);
  send_window(4096);
}

static void test_busy_comes_from_the_byte_cap_only(void) {
  pl_otaq_init(&q, 200000, PL_OTAQ_CAP);
  sim_t s = {0};
  uint32_t off = 0;
  while (sim_write(&s, off, 1) == GADGET_OK) {
    off++;
  }
  TEST_ASSERT_EQUAL_UINT32(PL_OTAQ_CAP, off);
  TEST_ASSERT_EQUAL(GADGET_ERR_BUSY, pl_otaq_admit(&q, off, 1));
  TEST_ASSERT_TRUE(s.msgs <= (int)PL_OTAQ_MSGS_MAX);
  TEST_ASSERT_EQUAL_INT(18, (int)PL_OTAQ_MSGS_MAX);
}

static void test_the_last_short_block_is_flushed(void) {
  pl_otaq_init(&q, 65000, PL_OTAQ_CAP);
  sim_t s = {0};
  uint32_t off = 0;
  while (off < 65000) {
    size_t n = 65000 - off < 3000 ? 65000 - off : 3000;
    TEST_ASSERT_EQUAL(GADGET_OK, sim_write(&s, off, n));
    off += (uint32_t)n;
  }
  TEST_ASSERT_EQUAL_UINT32(65000, s.sent); /* durable progress can reach size */
  TEST_ASSERT_EQUAL_UINT32(0, s.staged);
  TEST_ASSERT_EQUAL_INT(16, s.msgs); /* 15 full blocks and the 3560-byte end */
  TEST_ASSERT_TRUE(s.msgs <= (int)PL_OTAQ_MSGS_MAX);

  pl_otaq_init(&q, 4196, PL_OTAQ_CAP); /* the last chunk spills over the block end */
  sim_t t = {0};
  TEST_ASSERT_EQUAL(GADGET_OK, sim_write(&t, 0, 4000));
  TEST_ASSERT_EQUAL(GADGET_OK, sim_write(&t, 4000, 196));
  TEST_ASSERT_EQUAL_INT(2, t.msgs);
  TEST_ASSERT_EQUAL_UINT32(4196, t.sent);
  TEST_ASSERT_EQUAL_UINT32(0, t.staged);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_contiguous_writes_until_full);
  RUN_TEST(test_gaps_overlaps_and_sizes);
  RUN_TEST(test_unadmit_rolls_back);
  RUN_TEST(test_split_fills_one_block_at_a_time);
  RUN_TEST(test_small_chunks_fill_the_window_without_busy);
  RUN_TEST(test_busy_comes_from_the_byte_cap_only);
  RUN_TEST(test_the_last_short_block_is_flushed);
  return UNITY_END();
}
