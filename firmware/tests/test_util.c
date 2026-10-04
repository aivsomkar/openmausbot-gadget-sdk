/* firmware/tests/test_util.c */
/* SPDX-License-Identifier: Apache-2.0 */
#include <string.h>
#include "gadget_util.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

static void test_b64_encode_known_values(void) {
  char out[16];
  TEST_ASSERT_EQUAL_size_t(0, gadget_b64_encode(out, sizeof out, (const uint8_t *)"", 0));
  TEST_ASSERT_EQUAL_STRING("", out);
  TEST_ASSERT_EQUAL_size_t(4, gadget_b64_encode(out, sizeof out, (const uint8_t *)"\x00", 1));
  TEST_ASSERT_EQUAL_STRING("AA==", out);
  TEST_ASSERT_EQUAL_size_t(4, gadget_b64_encode(out, sizeof out, (const uint8_t *)"\x00\x01", 2));
  TEST_ASSERT_EQUAL_STRING("AAE=", out);
  TEST_ASSERT_EQUAL_size_t(8, gadget_b64_encode(out, sizeof out, (const uint8_t *)"foobar", 6));
  TEST_ASSERT_EQUAL_STRING("Zm9vYmFy", out);
  /* cap too small: needs 9 bytes for 8 chars + NUL */
  TEST_ASSERT_EQUAL_size_t(0, gadget_b64_encode(out, 8, (const uint8_t *)"foobar", 6));
}

static void test_b64_decode_is_strict(void) {
  uint8_t buf[8];
  size_t len = 99;
  TEST_ASSERT_EQUAL_INT(GADGET_OK, gadget_b64_decode("Zm9vYg==", buf, sizeof buf, &len));
  TEST_ASSERT_EQUAL_size_t(4, len);
  TEST_ASSERT_EQUAL_MEMORY("foob", buf, 4);
  TEST_ASSERT_EQUAL_INT(GADGET_OK, gadget_b64_decode("", buf, sizeof buf, &len));
  TEST_ASSERT_EQUAL_size_t(0, len);
  const char *bad[] = {"AA", "AA=", "AB==", "AAE", "AA==\n", " AA==", "-_8=", "AA==AA==", "A===", "=AAA", "AA=A"};
  for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
    TEST_ASSERT_EQUAL_INT_MESSAGE(GADGET_ERR_PARSE, gadget_b64_decode(bad[i], buf, sizeof buf, &len), bad[i]);
  }
  TEST_ASSERT_EQUAL_INT(GADGET_ERR_LIMIT, gadget_b64_decode("Zm9vYmFy", buf, 5, &len));
}

static void test_hex_round_trip_and_lowercase_only(void) {
  char out[9];
  const uint8_t in[4] = {0x00, 0xab, 0x7f, 0xff};
  gadget_hex_encode(out, in, 4);
  TEST_ASSERT_EQUAL_STRING("00ab7fff", out);
  uint8_t back[4];
  size_t len = 0;
  TEST_ASSERT_EQUAL_INT(GADGET_OK, gadget_hex_decode("00ab7fff", back, sizeof back, &len));
  TEST_ASSERT_EQUAL_size_t(4, len);
  TEST_ASSERT_EQUAL_MEMORY(in, back, 4);
  TEST_ASSERT_EQUAL_INT(GADGET_ERR_PARSE, gadget_hex_decode("00AB", back, sizeof back, &len));
  TEST_ASSERT_EQUAL_INT(GADGET_ERR_PARSE, gadget_hex_decode("abc", back, sizeof back, &len));
  TEST_ASSERT_EQUAL_INT(GADGET_ERR_PARSE, gadget_hex_decode("zz", back, sizeof back, &len));
  TEST_ASSERT_EQUAL_INT(GADGET_ERR_LIMIT, gadget_hex_decode("0011223344", back, sizeof back, &len));
}

/* RFC 6979 A.2.5 "sample": 72-byte DER, high S. */
static const char *SAMPLE_DER =
    "3046022100efd48b2aacb6a8fd1140dd9cd45e81d69d2c877b56aaf991c34d0ea84eaf3716"
    "022100f7cb1c942d657c41d436c7a1b6e29f65f3e900dbb9aff4064dc4ab2f843acda8";
/* Contract §1.7 short-DER case: 69 bytes, s has 31 significant bytes. */
static const char *SHORT_DER =
    "3043022052c1af44f658bb58a5b434a96d609052855835feca85b011cdfcb4159f52c37e"
    "021f2d466a13caea297fa0c97498ce12ed77ea25e9895415c4bb6ebe64b2a049cb";

static void der_round_trip(const char *der_hex, size_t expect_len) {
  uint8_t der[80], raw[64], again[80];
  size_t der_len = 0, again_len = 0;
  TEST_ASSERT_EQUAL_INT(GADGET_OK, gadget_hex_decode(der_hex, der, sizeof der, &der_len));
  TEST_ASSERT_EQUAL_size_t(expect_len, der_len);
  TEST_ASSERT_EQUAL_INT(GADGET_OK, gadget_der_to_raw(der, der_len, raw));
  TEST_ASSERT_EQUAL_INT(GADGET_OK, gadget_der_from_raw(raw, again, sizeof again, &again_len));
  TEST_ASSERT_EQUAL_size_t(der_len, again_len);
  TEST_ASSERT_EQUAL_MEMORY(der, again, der_len);
}

static void test_der_round_trips(void) {
  der_round_trip(SAMPLE_DER, 72);
  der_round_trip(SHORT_DER, 69);
  uint8_t der[80], raw[64];
  size_t len = 0;
  gadget_hex_decode(SHORT_DER, der, sizeof der, &len);
  TEST_ASSERT_EQUAL_INT(GADGET_OK, gadget_der_to_raw(der, len, raw));
  TEST_ASSERT_EQUAL_HEX8(0x00, raw[32]); /* s right-aligned into 32 bytes */
  TEST_ASSERT_EQUAL_HEX8(0x2d, raw[33]);
}

static void test_der_rejects_non_canonical(void) {
  uint8_t der[80], raw[64];
  size_t len = 0;
  gadget_hex_decode(SAMPLE_DER, der, sizeof der, &len);
  der[0] = 0x31; /* wrong tag */
  TEST_ASSERT_EQUAL_INT(GADGET_ERR_PARSE, gadget_der_to_raw(der, len, raw));
  gadget_hex_decode(SAMPLE_DER, der, sizeof der, &len);
  der[len] = 0x00; /* trailing byte */
  TEST_ASSERT_EQUAL_INT(GADGET_ERR_PARSE, gadget_der_to_raw(der, len + 1, raw));
  /* negative integer: r without its 0x00 pad */
  static const uint8_t neg[] = {0x30, 0x06, 0x02, 0x01, 0x80, 0x02, 0x01, 0x01};
  TEST_ASSERT_EQUAL_INT(GADGET_ERR_PARSE, gadget_der_to_raw(neg, sizeof neg, raw));
  /* non-minimal integer: 0x00 pad before a byte whose high bit is clear */
  static const uint8_t nonmin[] = {0x30, 0x07, 0x02, 0x02, 0x00, 0x01, 0x02, 0x01, 0x01};
  TEST_ASSERT_EQUAL_INT(GADGET_ERR_PARSE, gadget_der_to_raw(nonmin, sizeof nonmin, raw));
  /* integer longer than 32 bytes after the pad */
  uint8_t big[2 + 2 + 34 + 3] = {0x30, 2 + 34 + 3, 0x02, 34, 0x00, 0x80};
  big[2 + 2 + 34] = 0x02;
  big[2 + 2 + 34 + 1] = 0x01;
  big[2 + 2 + 34 + 2] = 0x01;
  TEST_ASSERT_EQUAL_INT(GADGET_ERR_PARSE, gadget_der_to_raw(big, sizeof big, raw));
  /* truncated */
  gadget_hex_decode(SAMPLE_DER, der, sizeof der, &len);
  TEST_ASSERT_EQUAL_INT(GADGET_ERR_PARSE, gadget_der_to_raw(der, len - 2, raw));
}

static void test_der_from_raw_minimal(void) {
  uint8_t raw[64] = {0};
  raw[31] = 0x01;      /* r = 1 */
  raw[32] = 0x80;      /* s has its high bit set: needs a 0x00 pad */
  uint8_t der[80];
  size_t len = 0;
  TEST_ASSERT_EQUAL_INT(GADGET_OK, gadget_der_from_raw(raw, der, sizeof der, &len));
  TEST_ASSERT_EQUAL_size_t(2 + 3 + 35, len);
  static const uint8_t head[] = {0x30, 38, 0x02, 0x01, 0x01, 0x02, 0x21, 0x00, 0x80};
  TEST_ASSERT_EQUAL_MEMORY(head, der, sizeof head);
  TEST_ASSERT_EQUAL_INT(GADGET_ERR_LIMIT, gadget_der_from_raw(raw, der, 10, &len));
}

static void test_validators(void) {
  TEST_ASSERT_TRUE(gadget_host_id_valid("000102030405060708090a0b0c0d0e0f"));
  TEST_ASSERT_FALSE(gadget_host_id_valid("000102030405060708090A0B0C0D0E0F"));
  TEST_ASSERT_FALSE(gadget_host_id_valid("h_0123456789abcdef"));
  TEST_ASSERT_FALSE(gadget_host_id_valid("000102030405060708090a0b0c0d0e0f0"));
  TEST_ASSERT_FALSE(gadget_host_id_valid(""));
  TEST_ASSERT_FALSE(gadget_host_id_valid(NULL));
  TEST_ASSERT_TRUE(gadget_pair_code_valid("123456"));
  TEST_ASSERT_TRUE(gadget_pair_code_valid("000000"));
  TEST_ASSERT_FALSE(gadget_pair_code_valid("12345"));
  TEST_ASSERT_FALSE(gadget_pair_code_valid("1234567"));
  TEST_ASSERT_FALSE(gadget_pair_code_valid("12a456"));
  TEST_ASSERT_FALSE(gadget_pair_code_valid(NULL));
}

static void test_utf8_copy_cuts_on_code_points(void) {
  char out[8];
  TEST_ASSERT_EQUAL_size_t(3, gadget_utf8_copy(out, sizeof out, "abc"));
  TEST_ASSERT_EQUAL_STRING("abc", out);
  /* "aé" is 3 bytes; 7 bytes fit exactly */
  TEST_ASSERT_EQUAL_size_t(7, gadget_utf8_copy(out, sizeof out, "abcdefg"));
  /* 8 bytes do not fit in cap 8: keep 4 bytes + "…" (3 bytes) */
  TEST_ASSERT_EQUAL_size_t(7, gadget_utf8_copy(out, sizeof out, "abcdefgh"));
  TEST_ASSERT_EQUAL_STRING("abcd\xe2\x80\xa6", out);
  /* never split "é" (C3 A9) */
  TEST_ASSERT_EQUAL_size_t(6, gadget_utf8_copy(out, sizeof out, "abc\xc3\xa9xyz"));
  TEST_ASSERT_EQUAL_STRING("abc\xe2\x80\xa6", out);
  char tiny[3];
  TEST_ASSERT_EQUAL_size_t(2, gadget_utf8_copy(tiny, sizeof tiny, "abcdef"));
  TEST_ASSERT_EQUAL_STRING("ab", tiny);
}

static void test_utf8_copy_tail_keeps_the_end(void) {
  char out[8];
  TEST_ASSERT_EQUAL_size_t(7, gadget_utf8_copy_tail(out, sizeof out, "abcdefghij"));
  TEST_ASSERT_EQUAL_STRING("\xe2\x80\xa6ghij", out);
  /* the cut would start inside "é" (C3 A9): skip forward to "xyz" */
  TEST_ASSERT_EQUAL_size_t(6, gadget_utf8_copy_tail(out, sizeof out, "abcde\xc3\xa9xyz"));
  TEST_ASSERT_EQUAL_STRING("\xe2\x80\xa6xyz", out);
  TEST_ASSERT_EQUAL_size_t(2, gadget_utf8_copy_tail(out, sizeof out, "hi"));
  TEST_ASSERT_EQUAL_STRING("hi", out);
}

static void test_utf8_len(void) {
  TEST_ASSERT_EQUAL_size_t(0, gadget_utf8_len(""));
  TEST_ASSERT_EQUAL_size_t(3, gadget_utf8_len("a\xc3\xa9\xe2\x80\xa6"));
  TEST_ASSERT_EQUAL_size_t(2, gadget_utf8_len("\xf0\x9f\x98\x80z"));  /* 4-byte sequence */
  TEST_ASSERT_EQUAL_size_t(3, gadget_utf8_len("\xff\xc3z"));          /* invalid: one per byte */
  /* overlong, surrogate and above U+10FFFF are invalid too (RFC 3629 §4) */
  TEST_ASSERT_EQUAL_size_t(3, gadget_utf8_len("\xe0\x80\x80"));       /* overlong U+0000 */
  TEST_ASSERT_EQUAL_size_t(3, gadget_utf8_len("\xed\xa0\x80"));       /* surrogate U+D800 */
  TEST_ASSERT_EQUAL_size_t(4, gadget_utf8_len("\xf4\x90\x80\x80"));   /* U+110000 */
  TEST_ASSERT_EQUAL_size_t(4, gadget_utf8_len("\xf0\x80\x80\x80"));   /* overlong 4-byte */
  /* the edges of those ranges stay valid */
  TEST_ASSERT_EQUAL_size_t(1, gadget_utf8_len("\xe0\xa0\x80"));       /* U+0800 */
  TEST_ASSERT_EQUAL_size_t(1, gadget_utf8_len("\xed\x9f\xbf"));       /* U+D7FF */
  TEST_ASSERT_EQUAL_size_t(1, gadget_utf8_len("\xee\x80\x80"));       /* U+E000 */
  TEST_ASSERT_EQUAL_size_t(1, gadget_utf8_len("\xf0\x90\x80\x80"));   /* U+10000 */
  TEST_ASSERT_EQUAL_size_t(1, gadget_utf8_len("\xf4\x8f\xbf\xbf"));   /* U+10FFFF */
}

static void test_prng_is_xorshift32(void) {
  gadget_prng_t p;
  gadget_prng_seed(&p, 1);
  TEST_ASSERT_EQUAL_HEX32(270369u, gadget_prng_next(&p));
  TEST_ASSERT_EQUAL_HEX32(67634689u, gadget_prng_next(&p));
  gadget_prng_seed(&p, 0);
  TEST_ASSERT_EQUAL_HEX32(0x9E3779B9u, p.s);
  gadget_prng_seed(&p, 7);
  for (int i = 0; i < 1000; i++) {
    uint32_t v = gadget_prng_range(&p, 10, 20);
    TEST_ASSERT_TRUE(v >= 10 && v <= 20);
  }
  TEST_ASSERT_EQUAL_UINT32(5, gadget_prng_range(&p, 5, 5));
  TEST_ASSERT_EQUAL_UINT32(9, gadget_prng_range(&p, 9, 3));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_b64_encode_known_values);
  RUN_TEST(test_b64_decode_is_strict);
  RUN_TEST(test_hex_round_trip_and_lowercase_only);
  RUN_TEST(test_der_round_trips);
  RUN_TEST(test_der_rejects_non_canonical);
  RUN_TEST(test_der_from_raw_minimal);
  RUN_TEST(test_validators);
  RUN_TEST(test_utf8_copy_cuts_on_code_points);
  RUN_TEST(test_utf8_copy_tail_keeps_the_end);
  RUN_TEST(test_utf8_len);
  RUN_TEST(test_prng_is_xorshift32);
  return UNITY_END();
}
