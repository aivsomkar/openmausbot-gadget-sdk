/* firmware/tests/test_ota.c */
/* SPDX-License-Identifier: Apache-2.0 */
/* Firmware updates and probation (core/src/ota.c, spec §4.8). */
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include "fake_hal.h"
#include "gadget_ota.h"
#include "gadget_proto.h"
#include "gadget_util.h"
#include "psa/crypto.h"
#include "unity.h"

void setUp(void) { fake_reset(); }
void tearDown(void) { core_deinit(); }

#define T1_PRIV "274b871e29523ce21208177b90a0a3bd6a169cb84cf867236adc68f3d90fc0c8"
#define T1_PUB "BIUTH1UeVJw2VXn0Ehdhd8apNOHmB6VvjV512SxIbCCXfUhvlLRTuOTLHtrsVAbIx06JhMoOMbC3ic73hZpxMwg="
#define IMG_SIZE 70000u

static uint8_t g_img[IMG_SIZE];

static void make_image(void) {
  for (uint32_t i = 0; i < IMG_SIZE; i++) g_img[i] = (uint8_t)(i * 31u + 7u);
}

/* A fw.offer whose sha256 field is hex, signed with t1 over the text built with
 * sign_board and that same hex. */
static void offer_hex(const char *board, const char *sign_board, const char *version, uint32_t size,
                      const char *key_id, const char *hex) {
  char text[256];
  int tn = gp_firmware_text(text, sizeof text, sign_board, version, size, hex);
  uint8_t priv[32], der[GADGET_SIG_DER_MAX];
  size_t n = 0, der_len = 0;
  gadget_hex_decode(T1_PRIV, priv, sizeof priv, &n);
  hal_crypto_sign(priv, (const uint8_t *)text, (size_t)tn, der, &der_len);
  char sig[GADGET_SIG_B64_MAX + 1];
  gadget_b64_encode(sig, sizeof sig, der, der_len);
  char json[640];
  snprintf(json, sizeof json,
           "{\"op\":\"fw.offer\",\"stream\":3,\"board\":\"%s\",\"version\":\"%s\",\"size\":%u,\"sha256\":\"%s\","
           "\"sig\":\"%s\",\"key_id\":\"%s\"}",
           board, version, (unsigned)size, hex, sig, key_id);
  fake_ws_in(json);
}

/* A fw.offer for g_img (or for sha_of when given), signed with t1 over the text
 * built with sign_board. */
static void offer(const char *board, const char *sign_board, const char *version, uint32_t size, const char *key_id,
                  const uint8_t *sha_of) {
  uint8_t sha[32];
  hal_crypto_sha256(sha_of ? sha_of : g_img, IMG_SIZE, sha);
  char hex[65];
  gadget_hex_encode(hex, sha, 32);
  offer_hex(board, sign_board, version, size, key_id, hex);
}

static void chunk(uint32_t offset, uint32_t len) {
  static uint8_t frame[2 + 4 + 4096];
  frame[0] = 0x04;
  frame[1] = 3;
  frame[2] = (uint8_t)offset;
  frame[3] = (uint8_t)(offset >> 8);
  frame[4] = (uint8_t)(offset >> 16);
  frame[5] = (uint8_t)(offset >> 24);
  memcpy(frame + 6, g_img + offset, len);
  fake_ws_bin_in(frame, 6 + len);
}

static void send_all(void) {
  for (uint32_t off = 0; off < IMG_SIZE; off += 4096) {
    chunk(off, IMG_SIZE - off < 4096 ? IMG_SIZE - off : 4096);
    fake_run(10);
  }
}

static const char *last_fail(void) {
  static char code[32];
  cJSON *f = fake_ws_last("fw.fail");
  if (f == NULL) return "";
  snprintf(code, sizeof code, "%s", cJSON_GetObjectItem(f, "code")->valuestring);
  cJSON_Delete(f);
  return code;
}

static void test_key_tables(void) {
  const gadget_release_key_t *k = gadget_key_find("t1");
  TEST_ASSERT_NOT_NULL(k);
  uint8_t pub[65];
  size_t n = 0;
  gadget_b64_decode(T1_PUB, pub, sizeof pub, &n);
  TEST_ASSERT_EQUAL_MEMORY(pub, k->pub, 65);
  TEST_ASSERT_EQUAL_size_t(0, gadget_release_keys_count); /* P2d adds r1 */
  TEST_ASSERT_NULL(gadget_key_find("r1"));
  TEST_ASSERT_NULL(gadget_key_find(NULL));
  TEST_ASSERT_NULL(gadget_key_find(""));
}

static void test_a_full_update(void) {
  make_image();
  fake_ready("amoled-175c");
  offer("amoled-175c", "amoled-175c", "1.1.0", IMG_SIZE, "t1", NULL);
  TEST_ASSERT_EQUAL_STRING("{\"op\":\"fw.ready\",\"stream\":3}", fake_ws_text(fake_ws_sent() - 1));
  TEST_ASSERT_EQUAL_UINT32(IMG_SIZE, fake_ota_size());
  fake_run(10);
  TEST_ASSERT_EQUAL_INT(UI_SCREEN_UPDATE, core_ui_model()->screen);
  TEST_ASSERT_EQUAL_STRING("1.1.0", core_ui_model()->update.version);
  fake_input(GADGET_IN_TALK_DOWN, 0, 0); /* no talking during an update */
  fake_mic_frames(20, 3000);
  fake_input(GADGET_IN_TALK_UP, 0, 0);
  TEST_ASSERT_EQUAL_size_t(0, fake_ws_count("voice.begin"));
  send_all();
  TEST_ASSERT_EQUAL_MEMORY(g_img, fake_ota_image(), IMG_SIZE);
  /* progress every 16 KiB and at the end */
  TEST_ASSERT_EQUAL_size_t(5, fake_ws_count("fw.progress"));
  cJSON *p = fake_ws_last("fw.progress");
  TEST_ASSERT_EQUAL_INT((int)IMG_SIZE, cJSON_GetObjectItem(p, "offset")->valueint);
  cJSON_Delete(p);
  TEST_ASSERT_EQUAL_INT(UI_UPDATE_VERIFYING, core_ui_model()->update.phase);
  TEST_ASSERT_EQUAL_UINT8(100, core_ui_model()->update.pct);
  fake_ws_in("{\"op\":\"fw.commit\",\"stream\":3}");
  TEST_ASSERT_TRUE(fake_ota_finalized());
  TEST_ASSERT_EQUAL_STRING("1.1.0", fake_ota_boot_version());
  fake_run(10);
  TEST_ASSERT_EQUAL_INT(UI_UPDATE_RESTARTING, core_ui_model()->update.phase);
  FAKE_EXPECT_RESTART(fake_run(2000));
  TEST_ASSERT_EQUAL_INT(1, fake_restarts());
}

static void expect_fail(const char *board, const char *sign_board, const char *version, uint32_t size,
                        const char *key_id, const uint8_t *sha_of, const char *code) {
  fake_ws_clear();
  offer(board, sign_board, version, size, key_id, sha_of);
  TEST_ASSERT_EQUAL_STRING_MESSAGE(code, last_fail(), code);
  TEST_ASSERT_EQUAL_size_t(0, fake_ws_count("fw.ready"));
  fake_run(10);
  TEST_ASSERT_EQUAL_INT(UI_SCREEN_IDLE, core_ui_model()->screen);
}

static void test_offers_are_checked_in_order(void) {
  make_image();
  fake_ready("amoled-175c");
  expect_fail("lcd-154", "lcd-154", "1.1.0", IMG_SIZE, "t1", NULL, "wrong_board");
  expect_fail("amoled-175c", "amoled-175c", "1.0.0", IMG_SIZE, "t1", NULL, "same_version");
  expect_fail("amoled-175c", "amoled-175c", "1.1.0", 6291457u, "t1", NULL, "too_large");
  expect_fail("amoled-175c", "amoled-175c", "1.1.0", IMG_SIZE, "r9", NULL, "unknown_key");
  /* the signature is checked over the gadget's own board id */
  expect_fail("amoled-175c", "lcd-154", "1.1.0", IMG_SIZE, "t1", NULL, "bad_sig");
  /* with several defects, the first in that order wins */
  expect_fail("lcd-154", "lcd-154", "1.0.0", 6291457u, "r9", NULL, "wrong_board");
  expect_fail("amoled-175c", "amoled-175c", "1.0.0", 6291457u, "r9", NULL, "same_version");
  expect_fail("amoled-175c", "lcd-154", "1.1.0", 6291457u, "r9", NULL, "too_large");
  expect_fail("amoled-175c", "lcd-154", "1.1.0", IMG_SIZE, "r9", NULL, "unknown_key");
  /* an older signed version is accepted: anti-rollback is off */
  fake_ws_clear();
  offer("amoled-175c", "amoled-175c", "0.9.0", IMG_SIZE, "t1", NULL);
  TEST_ASSERT_EQUAL_size_t(1, fake_ws_count("fw.ready"));
  /* a second offer while one runs */
  fake_ws_clear();
  offer("amoled-175c", "amoled-175c", "1.2.0", IMG_SIZE, "t1", NULL);
  TEST_ASSERT_EQUAL_STRING("busy", last_fail());
}

static void test_busy_while_the_person_is_talking(void) {
  make_image();
  fake_ready("amoled-175c");
  fake_console_in("say \"hi\"");
  cJSON *say = fake_ws_last("say");
  char done[160];
  snprintf(done, sizeof done, "{\"op\":\"done\",\"turn\":\"%s\",\"outcome\":\"ok\"}",
           cJSON_GetObjectItem(say, "turn")->valuestring);
  cJSON_Delete(say);
  offer("amoled-175c", "amoled-175c", "1.1.0", IMG_SIZE, "t1", NULL);
  TEST_ASSERT_EQUAL_STRING("busy", last_fail()); /* a turn is in flight */
  fake_ws_in(done);
  fake_input(GADGET_IN_TALK_DOWN, 0, 0);
  fake_mic_frames(20, 3000);
  fake_ws_clear();
  offer("amoled-175c", "amoled-175c", "1.1.0", IMG_SIZE, "t1", NULL);
  TEST_ASSERT_EQUAL_STRING("busy", last_fail()); /* recording */
  fake_input(GADGET_IN_CANCEL_DOWN, 0, 0);
  fake_ws_clear();
  offer("amoled-175c", "amoled-175c", "1.1.0", IMG_SIZE, "t1", NULL);
  TEST_ASSERT_EQUAL_size_t(1, fake_ws_count("fw.ready")); /* idle again: accepted */
}

/* D22 counts a held press as talking: the mic is live from the press, 300 ms
 * before the press becomes a recording. */
static void test_busy_while_a_press_is_held(void) {
  make_image();
  fake_ready("amoled-175c");
  fake_ws_clear();
  fake_input(GADGET_IN_TALK_DOWN, 0, 0);
  fake_mic_frames(5, 3000); /* 100 ms: held, not recording yet */
  offer("amoled-175c", "amoled-175c", "1.1.0", IMG_SIZE, "t1", NULL);
  TEST_ASSERT_EQUAL_STRING("busy", last_fail());
  TEST_ASSERT_EQUAL_size_t(0, fake_ws_count("fw.ready"));
  TEST_ASSERT_EQUAL_UINT32(0, fake_ota_size());
  /* the press goes on to record and send its turn */
  fake_mic_frames(20, 3000);
  TEST_ASSERT_EQUAL_size_t(1, fake_ws_count("voice.begin"));
  TEST_ASSERT_EQUAL_INT(UI_SCREEN_LISTENING, core_ui_model()->screen);
  fake_input(GADGET_IN_TALK_UP, 0, 0);
  TEST_ASSERT_EQUAL_size_t(1, fake_ws_count("voice.end"));
  cJSON *vb = fake_ws_last("voice.begin");
  char done[160];
  snprintf(done, sizeof done, "{\"op\":\"done\",\"turn\":\"%s\",\"outcome\":\"ok\"}",
           cJSON_GetObjectItem(vb, "turn")->valuestring);
  cJSON_Delete(vb);
  fake_ws_in(done);
  /* a held touch, 100 ms in */
  fake_input(GADGET_IN_TOUCH_DOWN, 233, 233);
  fake_run(100);
  fake_ws_clear();
  offer("amoled-175c", "amoled-175c", "1.1.0", IMG_SIZE, "t1", NULL);
  TEST_ASSERT_EQUAL_STRING("busy", last_fail());
  TEST_ASSERT_EQUAL_size_t(0, fake_ws_count("fw.ready"));
  fake_input(GADGET_IN_TOUCH_UP, 233, 233);
  fake_ws_clear();
  offer("amoled-175c", "amoled-175c", "1.1.0", IMG_SIZE, "t1", NULL);
  TEST_ASSERT_EQUAL_size_t(1, fake_ws_count("fw.ready")); /* nothing held: accepted */
}

static void test_tampered_offer_fields(void) {
  make_image();
  fake_ready("amoled-175c");
  /* the uppercase digest, with a good t1 signature over that uppercase text */
  uint8_t sha[32];
  hal_crypto_sha256(g_img, IMG_SIZE, sha);
  char upper[65];
  gadget_hex_encode(upper, sha, 32);
  for (char *c = upper; *c; c++) *c = (char)toupper((unsigned char)*c);
  offer_hex("amoled-175c", "amoled-175c", "1.1.0", IMG_SIZE, "t1", upper);
  TEST_ASSERT_EQUAL_STRING("bad_sig", last_fail()); /* the signed text always has lowercase hex */
  TEST_ASSERT_EQUAL_size_t(0, fake_ws_count("fw.ready"));
  fake_ws_in("{\"op\":\"fw.offer\",\"stream\":3,\"board\":\"amoled-175c\",\"version\":\"1.1.0\",\"size\":70000,"
             "\"sha256\":\"e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855\",\"sig\":\"not base64!\","
             "\"key_id\":\"t1\"}");
  TEST_ASSERT_EQUAL_STRING("bad_sig", last_fail());
}

static void test_out_of_order_chunk_fails_sequence(void) {
  make_image();
  fake_ready("amoled-175c");
  offer("amoled-175c", "amoled-175c", "1.1.0", IMG_SIZE, "t1", NULL);
  chunk(0, 4096);
  chunk(8192, 4096);
  TEST_ASSERT_EQUAL_STRING("sequence", last_fail());
  TEST_ASSERT_EQUAL_INT(1, fake_ota_aborts());
  fake_run(10);
  TEST_ASSERT_EQUAL_INT(UI_SCREEN_IDLE, core_ui_model()->screen);
}

static void test_wrong_bytes_fail_checksum(void) {
  make_image();
  static uint8_t other[IMG_SIZE];
  memcpy(other, g_img, IMG_SIZE);
  other[1234] ^= 0xff;
  fake_ready("amoled-175c");
  offer("amoled-175c", "amoled-175c", "1.1.0", IMG_SIZE, "t1", other); /* signed for different bytes */
  send_all();
  fake_ws_in("{\"op\":\"fw.commit\",\"stream\":3}");
  TEST_ASSERT_EQUAL_STRING("checksum", last_fail());
  TEST_ASSERT_FALSE(fake_ota_boot_version()[0] != '\0');
}

/* A commit right behind the last chunk, before the writes report: it waits for
 * GADGET_EV_OTA_WRITTEN, then finalizes. */
static void test_a_commit_behind_the_last_chunk_waits_for_the_writes(void) {
  make_image();
  fake_ready("amoled-175c");
  offer("amoled-175c", "amoled-175c", "1.1.0", IMG_SIZE, "t1", NULL);
  for (uint32_t off = 0; off < IMG_SIZE; off += 4096) chunk(off, IMG_SIZE - off < 4096 ? IMG_SIZE - off : 4096);
  fake_ws_in("{\"op\":\"fw.commit\",\"stream\":3}");
  TEST_ASSERT_FALSE(fake_ota_finalized());
  TEST_ASSERT_EQUAL_STRING("", last_fail());
  fake_run(10);
  TEST_ASSERT_TRUE(fake_ota_finalized());
  TEST_ASSERT_EQUAL_STRING("1.1.0", fake_ota_boot_version());
  TEST_ASSERT_EQUAL_STRING("", last_fail());
  fake_run(10);
  TEST_ASSERT_EQUAL_INT(UI_UPDATE_RESTARTING, core_ui_model()->update.phase);
  FAKE_EXPECT_RESTART(fake_run(2000));
  TEST_ASSERT_EQUAL_INT(1, fake_restarts());
}

static void test_early_commit_fails_checksum(void) {
  make_image();
  fake_ready("amoled-175c");
  offer("amoled-175c", "amoled-175c", "1.1.0", IMG_SIZE, "t1", NULL);
  chunk(0, 4096);
  fake_ws_in("{\"op\":\"fw.commit\",\"stream\":3}"); /* 4096 of 70000 bytes */
  TEST_ASSERT_EQUAL_STRING("checksum", last_fail());
  TEST_ASSERT_EQUAL_INT(1, fake_ota_aborts());
}

static void test_timeouts(void) {
  make_image();
  fake_ready("amoled-175c");
  offer("amoled-175c", "amoled-175c", "1.1.0", IMG_SIZE, "t1", NULL);
  chunk(0, 4096);
  for (int i = 0; i < 2; i++) {
    fake_run(14000);
    fake_ws_ping_in();
  }
  TEST_ASSERT_EQUAL_STRING("", last_fail());
  fake_run(2100);
  TEST_ASSERT_EQUAL_STRING("timeout", last_fail()); /* no chunk for 30 s */
  /* all bytes, then no commit within 30 s */
  fake_ws_clear();
  offer("amoled-175c", "amoled-175c", "1.1.0", IMG_SIZE, "t1", NULL);
  send_all();
  for (int i = 0; i < 3; i++) {
    fake_run(10000);
    fake_ws_ping_in();
  }
  TEST_ASSERT_EQUAL_STRING("timeout", last_fail());
}

static void test_flash_errors_and_disconnects(void) {
  make_image();
  fake_ready("amoled-175c");
  fake_ota_fail_writes(true);
  offer("amoled-175c", "amoled-175c", "1.1.0", IMG_SIZE, "t1", NULL);
  chunk(0, 4096);
  fake_run(10);
  TEST_ASSERT_EQUAL_STRING("flash", last_fail());
  fake_ota_fail_writes(false);
  offer("amoled-175c", "amoled-175c", "1.1.0", IMG_SIZE, "t1", NULL);
  chunk(0, 4096);
  int aborts = fake_ota_aborts();
  fake_ws_drop(1006);
  fake_run(10);
  TEST_ASSERT_EQUAL_INT(aborts + 1, fake_ota_aborts());
  fake_run(2000);
  fake_handshake();
  TEST_ASSERT_EQUAL_INT(UI_SCREEN_IDLE, core_ui_model()->screen);
}

static void test_probation_confirms_on_the_first_ready(void) {
  fake_ota_set_running(HAL_OTA_IMG_PENDING_VERIFY);
  fake_store_paired();
  fake_boot("amoled-175c");
  fake_run(10);
  fake_ws_accept();
  fake_ws_in("{\"op\":\"challenge\",\"nonce\":\"" FAKE_NONCE "\",\"host_id\":\"" FAKE_HOST_ID "\",\"host_name\":\"Mac\"}");
  fake_ws_in("{\"op\":\"ready\",\"session\":\"s_1\",\"bot\":{\"id\":\"b\",\"name\":\"B\"}}");
  TEST_ASSERT_TRUE(fake_ota_marked_valid());
  TEST_ASSERT_EQUAL_STRING("{\"op\":\"fw.installed\",\"version\":\"1.0.0\"}", fake_ws_text(fake_ws_sent() - 1));
  for (int i = 0; i < 40; i++) {
    fake_run(10000);
    fake_ws_ping_in();
  }
  TEST_ASSERT_FALSE(fake_ota_invalidated());
}

/* hal_ota_mark_valid failing (the bootloader would still roll back) is not an
 * install: no fw.installed, and probation runs out as without a ready. */
static void test_a_failed_confirm_keeps_probation(void) {
  fake_ota_set_running(HAL_OTA_IMG_PENDING_VERIFY);
  fake_ota_mark_valid_fails(true);
  fake_store_paired();
  fake_boot("amoled-175c");
  fake_run(10);
  fake_ws_accept();
  fake_ws_in("{\"op\":\"challenge\",\"nonce\":\"" FAKE_NONCE "\",\"host_id\":\"" FAKE_HOST_ID "\",\"host_name\":\"Mac\"}");
  fake_ws_in("{\"op\":\"ready\",\"session\":\"s_1\",\"bot\":{\"id\":\"b\",\"name\":\"B\"}}");
  TEST_ASSERT_FALSE(fake_ota_marked_valid());
  TEST_ASSERT_EQUAL_size_t(0, fake_ws_count("fw.installed"));
  fake_run(290000);
  TEST_ASSERT_FALSE(fake_ota_invalidated());
  FAKE_EXPECT_RESTART(fake_run(15000));
  TEST_ASSERT_TRUE(fake_ota_invalidated());
}

static void test_probation_rolls_back_without_a_ready(void) {
  fake_ota_set_running(HAL_OTA_IMG_PENDING_VERIFY);
  fake_store_paired();
  fake_boot("amoled-175c");
  fake_run(299000);
  TEST_ASSERT_FALSE(fake_ota_invalidated());
  FAKE_EXPECT_RESTART(fake_run(2000));
  TEST_ASSERT_TRUE(fake_ota_invalidated());
}

static void test_fail_probation_ignores_the_first_ready(void) {
  fake_ota_set_running(HAL_OTA_IMG_PENDING_VERIFY);
  fake_store_paired();
  core_config_t cfg = {.board = gadget_board_by_id("amoled-175c"), .fw_version = "1.1.0", .prng_seed = 1,
                       .fail_probation = true, .probation_ms = 3000};
  fake_boot_cfg(&cfg);
  fake_run(10);
  fake_handshake();
  TEST_ASSERT_FALSE(fake_ota_marked_valid());
  TEST_ASSERT_EQUAL_size_t(0, fake_ws_count("fw.installed"));
  FAKE_EXPECT_RESTART(fake_run(4000));
  TEST_ASSERT_TRUE(fake_ota_invalidated());
}

int main(void) {
  if (psa_crypto_init() != PSA_SUCCESS) return 3;
  UNITY_BEGIN();
  RUN_TEST(test_key_tables);
  RUN_TEST(test_a_full_update);
  RUN_TEST(test_offers_are_checked_in_order);
  RUN_TEST(test_busy_while_the_person_is_talking);
  RUN_TEST(test_busy_while_a_press_is_held);
  RUN_TEST(test_tampered_offer_fields);
  RUN_TEST(test_out_of_order_chunk_fails_sequence);
  RUN_TEST(test_wrong_bytes_fail_checksum);
  RUN_TEST(test_a_commit_behind_the_last_chunk_waits_for_the_writes);
  RUN_TEST(test_early_commit_fails_checksum);
  RUN_TEST(test_timeouts);
  RUN_TEST(test_flash_errors_and_disconnects);
  RUN_TEST(test_probation_confirms_on_the_first_ready);
  RUN_TEST(test_a_failed_confirm_keeps_probation);
  RUN_TEST(test_probation_rolls_back_without_a_ready);
  RUN_TEST(test_fail_probation_ignores_the_first_ready);
  return UNITY_END();
}
