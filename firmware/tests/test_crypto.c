/* firmware/tests/test_crypto.c */
/* SPDX-License-Identifier: Apache-2.0 */
/* The PSA adapter (core/src/crypto_psa.c) against RFC 6979 A.2.5 and the
 * contract's fixed values (§1.7). Runs on mbedTLS 3.6.7 and 4.2.0. */
#include <stdio.h>
#include <string.h>
#include "gadget_hal.h"
#include "gadget_proto.h"
#include "gadget_util.h"
#include "psa/crypto.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

static const char *RFC_PRIV = "c9afa9d845ba75166b5c215767b1d6934e50c3db36e89b127b8a622b120f6721";
static const char *RFC_PUB_B64 = "BGD+1LolWp0xyWHrdMY1bWjASbiSO2H6bOZpYi5g8p+2eQP+EAi4vJmkGunpVii8ZPLxsgwtfp9Rd6PClNRGIpk=";
static const char *SAMPLE_DER =
    "3046022100efd48b2aacb6a8fd1140dd9cd45e81d69d2c877b56aaf991c34d0ea84eaf3716"
    "022100f7cb1c942d657c41d436c7a1b6e29f65f3e900dbb9aff4064dc4ab2f843acda8";
static const char *TEST_DER =
    "3045022100f1abb023518351cd71d881567b1ea663ed3efcf6c5132b354f28d3b0b7d38367"
    "0220019f4113742a2b14bd25926b49c649155f267e60d3814b4c0cc84250e46f0083";
static const char *PROVE_DER =
    "3046022100f0c4fbe24029d797b16b36dcc0d05fb7a8b9df8c5ca4b27ad99d820d4d8a6642"
    "02210087d087e1c83ab59e8feebee63a40b423c5af81956ceca8033264098abf44e35a";

static void unhex(const char *hex, uint8_t *out, size_t cap, size_t *len) {
  TEST_ASSERT_EQUAL_INT(GADGET_OK, gadget_hex_decode(hex, out, cap, len));
}

static void rfc_key(uint8_t priv[32], uint8_t pub[65]) {
  size_t n = 0;
  unhex(RFC_PRIV, priv, 32, &n);
  TEST_ASSERT_EQUAL_INT(GADGET_OK, hal_crypto_pubkey(priv, pub));
}

static void sign_hex(const uint8_t priv[32], const char *msg, char *out_hex) {
  uint8_t der[GADGET_SIG_DER_MAX];
  size_t der_len = 0;
  TEST_ASSERT_EQUAL_INT(GADGET_OK, hal_crypto_sign(priv, (const uint8_t *)msg, strlen(msg), der, &der_len));
  gadget_hex_encode(out_hex, der, der_len);
}

static void test_pubkey_and_id_from_rfc_key(void) {
  uint8_t priv[32], pub[65];
  rfc_key(priv, pub);
  char b64[GADGET_PUBKEY_B64_LEN + 1];
  gadget_b64_encode(b64, sizeof b64, pub, sizeof pub);
  TEST_ASSERT_EQUAL_STRING(RFC_PUB_B64, b64);
  char id[GADGET_ID_LEN + 1];
  TEST_ASSERT_EQUAL_INT(GADGET_OK, gadget_id_from_pubkey(pub, id));
  TEST_ASSERT_EQUAL_STRING("gad_b18b86ce1389e46d", id);
}

static void test_rfc6979_signatures_are_reproduced_without_s_normalization(void) {
  uint8_t priv[32], pub[65];
  rfc_key(priv, pub);
  char hex[2 * GADGET_SIG_DER_MAX + 1];
  sign_hex(priv, "sample", hex);
  TEST_ASSERT_EQUAL_STRING(SAMPLE_DER, hex); /* high S kept as is */
  sign_hex(priv, "test", hex);
  TEST_ASSERT_EQUAL_STRING(TEST_DER, hex);
}

static void test_prove_signature_matches_contract(void) {
  uint8_t priv[32], pub[65];
  rfc_key(priv, pub);
  char text[256];
  gp_prove_text(text, sizeof text, "gad_b18b86ce1389e46d", "AAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8=",
                "000102030405060708090a0b0c0d0e0f");
  char hex[2 * GADGET_SIG_DER_MAX + 1];
  sign_hex(priv, text, hex);
  TEST_ASSERT_EQUAL_STRING(PROVE_DER, hex);
}

static void test_verify_accepts_high_s_and_rejects_tampering(void) {
  uint8_t priv[32], pub[65], der[80];
  size_t len = 0;
  rfc_key(priv, pub);
  unhex(SAMPLE_DER, der, sizeof der, &len);
  TEST_ASSERT_EQUAL_INT(GADGET_OK, hal_crypto_verify(pub, (const uint8_t *)"sample", 6, der, len));
  TEST_ASSERT_EQUAL_INT(GADGET_ERR_BAD_SIG, hal_crypto_verify(pub, (const uint8_t *)"samplf", 6, der, len));
  der[len - 1] ^= 1;
  TEST_ASSERT_EQUAL_INT(GADGET_ERR_BAD_SIG, hal_crypto_verify(pub, (const uint8_t *)"sample", 6, der, len));
  der[len - 1] ^= 1;
  TEST_ASSERT_EQUAL_INT(GADGET_ERR_BAD_SIG, hal_crypto_verify(pub, (const uint8_t *)"sample", 6, der, len - 2));
  uint8_t bad_pub[65];
  memcpy(bad_pub, pub, 65);
  bad_pub[64] ^= 1; /* off the curve */
  TEST_ASSERT_EQUAL_INT(GADGET_ERR_BAD_SIG, hal_crypto_verify(bad_pub, (const uint8_t *)"sample", 6, der, len));
}

static void test_keygen_round_trip(void) {
  uint8_t priv[32], pub[65], pub2[65], der[GADGET_SIG_DER_MAX];
  size_t len = 0;
  TEST_ASSERT_EQUAL_INT(GADGET_OK, hal_crypto_keygen(priv, pub));
  TEST_ASSERT_EQUAL_HEX8(0x04, pub[0]);
  TEST_ASSERT_EQUAL_INT(GADGET_OK, hal_crypto_pubkey(priv, pub2));
  TEST_ASSERT_EQUAL_MEMORY(pub, pub2, 65);
  TEST_ASSERT_EQUAL_INT(GADGET_OK, hal_crypto_sign(priv, (const uint8_t *)"hi", 2, der, &len));
  TEST_ASSERT_TRUE(len >= 8 && len <= GADGET_SIG_DER_MAX);
  TEST_ASSERT_EQUAL_INT(GADGET_OK, hal_crypto_verify(pub, (const uint8_t *)"hi", 2, der, len));
  uint8_t other[32], other_pub[65];
  TEST_ASSERT_EQUAL_INT(GADGET_OK, hal_crypto_keygen(other, other_pub));
  TEST_ASSERT_TRUE(memcmp(priv, other, 32) != 0);
  TEST_ASSERT_EQUAL_INT(GADGET_ERR_BAD_SIG, hal_crypto_verify(other_pub, (const uint8_t *)"hi", 2, der, len));
}

static void test_sha256_one_shot_and_multi_part(void) {
  uint8_t a[32], b[32];
  char hex[65];
  TEST_ASSERT_EQUAL_INT(GADGET_OK, hal_crypto_sha256("abc", 3, a));
  gadget_hex_encode(hex, a, 32);
  TEST_ASSERT_EQUAL_STRING("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", hex);
  hal_sha256_t ctx = {0};
  TEST_ASSERT_EQUAL_INT(GADGET_OK, hal_crypto_sha256_begin(&ctx));
  TEST_ASSERT_NOT_NULL(ctx.op);
  TEST_ASSERT_EQUAL_INT(GADGET_OK, hal_crypto_sha256_update(&ctx, "a", 1));
  TEST_ASSERT_EQUAL_INT(GADGET_OK, hal_crypto_sha256_update(&ctx, "bc", 2));
  TEST_ASSERT_EQUAL_INT(GADGET_OK, hal_crypto_sha256_finish(&ctx, b));
  TEST_ASSERT_NULL(ctx.op);
  TEST_ASSERT_EQUAL_MEMORY(a, b, 32);
  hal_crypto_sha256_abort(&ctx); /* NULL op: nothing to do */
  TEST_ASSERT_EQUAL_INT(GADGET_OK, hal_crypto_sha256_begin(&ctx));
  hal_crypto_sha256_abort(&ctx);
  TEST_ASSERT_NULL(ctx.op);
  /* begin on a live context aborts the old operation and starts over; an
   * operation left behind shows up as a leak under -DGADGET_SANITIZE=ON on
   * Linux, where LeakSanitizer runs */
  TEST_ASSERT_EQUAL_INT(GADGET_OK, hal_crypto_sha256_begin(&ctx));
  TEST_ASSERT_EQUAL_INT(GADGET_OK, hal_crypto_sha256_update(&ctx, "stale", 5));
  TEST_ASSERT_EQUAL_INT(GADGET_OK, hal_crypto_sha256_begin(&ctx));
  TEST_ASSERT_EQUAL_INT(GADGET_OK, hal_crypto_sha256_update(&ctx, "abc", 3));
  memset(b, 0, sizeof b);
  TEST_ASSERT_EQUAL_INT(GADGET_OK, hal_crypto_sha256_finish(&ctx, b));
  TEST_ASSERT_EQUAL_MEMORY(a, b, 32);
  TEST_ASSERT_NULL(ctx.op);
  TEST_ASSERT_EQUAL_INT(GADGET_ERR_ARG, hal_crypto_sha256_begin(NULL));
}

static void test_random(void) {
  uint8_t a[16] = {0}, b[16] = {0};
  TEST_ASSERT_EQUAL_INT(GADGET_OK, hal_crypto_random(a, sizeof a));
  TEST_ASSERT_EQUAL_INT(GADGET_OK, hal_crypto_random(b, sizeof b));
  TEST_ASSERT_TRUE(memcmp(a, b, sizeof a) != 0);
}

int main(void) {
  /* not a TEST_ASSERT: outside RUN_TEST, Unity has no frame to jump back to */
  if (psa_crypto_init() != PSA_SUCCESS) {
    fprintf(stderr, "psa_crypto_init failed\n");
    return 3;
  }
  UNITY_BEGIN();
  RUN_TEST(test_pubkey_and_id_from_rfc_key);
  RUN_TEST(test_rfc6979_signatures_are_reproduced_without_s_normalization);
  RUN_TEST(test_prove_signature_matches_contract);
  RUN_TEST(test_verify_accepts_high_s_and_rejects_tampering);
  RUN_TEST(test_keygen_round_trip);
  RUN_TEST(test_sha256_one_shot_and_multi_part);
  RUN_TEST(test_random);
  return UNITY_END();
}
