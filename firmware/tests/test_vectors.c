/* firmware/tests/test_vectors.c */
/* SPDX-License-Identifier: Apache-2.0 */
/* The firmware side of protocol/vectors (contract §4.4): every case in the
 * files the gadget needs must pass with core's own code. argv[1] is the
 * vectors directory. versions.json is host-side only and is not read here. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cJSON.h"
#include "gadget_hal.h"
#include "gadget_proto.h"
#include "gadget_util.h"
#include "psa/crypto.h"
#include "unity.h"

static const char *g_dir = NULL;

void setUp(void) {}
void tearDown(void) {}

static char *read_file(const char *name, size_t *len_out) {
  char path[1024];
  snprintf(path, sizeof path, "%s/%s", g_dir, name);
  FILE *f = fopen(path, "rb");
  if (f == NULL) return NULL;
  fseek(f, 0, SEEK_END);
  long n = ftell(f);
  fseek(f, 0, SEEK_SET);
  char *buf = malloc((size_t)n + 1);
  size_t got = fread(buf, 1, (size_t)n, f);
  fclose(f);
  buf[got] = '\0';
  if (len_out) *len_out = got;
  return buf;
}

/* Loads <stem>.json and returns its "cases" array (the caller frees *root). */
static const cJSON *load_cases(const char *stem, cJSON **root) {
  char name[64];
  snprintf(name, sizeof name, "%s.json", stem);
  char *text = read_file(name, NULL);
  TEST_ASSERT_NOT_NULL_MESSAGE(text, name);
  *root = cJSON_Parse(text);
  free(text);
  TEST_ASSERT_NOT_NULL_MESSAGE(*root, name);
  const cJSON *cases = cJSON_GetObjectItemCaseSensitive(*root, "cases");
  TEST_ASSERT_TRUE_MESSAGE(cJSON_IsArray(cases) && cJSON_GetArraySize(cases) > 0, name);
  return cases;
}

static const char *s(const cJSON *c, const char *key) {
  const cJSON *v = cJSON_GetObjectItemCaseSensitive(c, key);
  TEST_ASSERT_TRUE_MESSAGE(cJSON_IsString(v), key);
  return v->valuestring;
}

static bool b(const cJSON *c, const char *key) {
  const cJSON *v = cJSON_GetObjectItemCaseSensitive(c, key);
  TEST_ASSERT_TRUE_MESSAGE(cJSON_IsBool(v), key);
  return cJSON_IsTrue(v);
}

static uint32_t u(const cJSON *c, const char *key) {
  const cJSON *v = cJSON_GetObjectItemCaseSensitive(c, key);
  TEST_ASSERT_TRUE_MESSAGE(cJSON_IsNumber(v), key);
  return (uint32_t)v->valuedouble;
}

static size_t unhex(const char *hex, uint8_t *out, size_t cap) {
  size_t n = 0;
  TEST_ASSERT_EQUAL_INT_MESSAGE(GADGET_OK, gadget_hex_decode(hex, out, cap, &n), hex);
  return n;
}

/* s > n/2 for P-256, on the raw big-endian s. */
static bool is_high_s(const uint8_t raw[64]) {
  static const uint8_t HALF_N[32] = {0x7f, 0xff, 0xff, 0xff, 0x80, 0x00, 0x00, 0x00, 0x7f, 0xff, 0xff,
                                     0xff, 0xff, 0xff, 0xff, 0xff, 0xde, 0x73, 0x7d, 0x56, 0xd3, 0x8b,
                                     0xcf, 0x42, 0x79, 0xdc, 0xe5, 0x61, 0x7e, 0x31, 0x92, 0xa8};
  return memcmp(raw + 32, HALF_N, 32) > 0;
}

static void sign_and_compare(const char *priv_hex, const char *text, const char *want_der_hex) {
  uint8_t priv[32], der[GADGET_SIG_DER_MAX];
  size_t der_len = 0;
  unhex(priv_hex, priv, sizeof priv);
  TEST_ASSERT_EQUAL_INT(GADGET_OK, hal_crypto_sign(priv, (const uint8_t *)text, strlen(text), der, &der_len));
  char hex[2 * GADGET_SIG_DER_MAX + 1];
  gadget_hex_encode(hex, der, der_len);
  TEST_ASSERT_EQUAL_STRING(want_der_hex, hex);
}

static void test_sha256sums_cover_the_exact_bytes(void) {
  char *sums = read_file("SHA256SUMS", NULL);
  TEST_ASSERT_NOT_NULL(sums);
  int lines = 0;
  for (char *line = strtok(sums, "\n"); line != NULL; line = strtok(NULL, "\n")) {
    char want[65], name[256];
    TEST_ASSERT_EQUAL_INT_MESSAGE(2, sscanf(line, "%64s  %255s", want, name), line);
    size_t len = 0;
    char *bytes = read_file(name, &len);
    TEST_ASSERT_NOT_NULL_MESSAGE(bytes, name);
    uint8_t h[32];
    char got[65];
    hal_crypto_sha256(bytes, len, h);
    gadget_hex_encode(got, h, 32);
    TEST_ASSERT_EQUAL_STRING_MESSAGE(want, got, name);
    free(bytes);
    lines++;
  }
  free(sums);
  TEST_ASSERT_GREATER_OR_EQUAL_INT(7, lines);
}

static void test_identity(void) {
  cJSON *root;
  const cJSON *c;
  cJSON_ArrayForEach(c, load_cases("identity", &root)) {
    uint8_t priv[32], pub[65];
    unhex(s(c, "private_key_hex"), priv, sizeof priv);
    TEST_ASSERT_EQUAL_INT(GADGET_OK, hal_crypto_pubkey(priv, pub));
    char hex[131], b64[GADGET_PUBKEY_B64_LEN + 1], id[GADGET_ID_LEN + 1], sha[65];
    gadget_hex_encode(hex, pub, 65);
    TEST_ASSERT_EQUAL_STRING_MESSAGE(s(c, "pubkey_hex"), hex, s(c, "name"));
    gadget_b64_encode(b64, sizeof b64, pub, 65);
    TEST_ASSERT_EQUAL_STRING(s(c, "pubkey_b64"), b64);
    uint8_t h[32];
    hal_crypto_sha256(pub, 65, h);
    gadget_hex_encode(sha, h, 32);
    TEST_ASSERT_EQUAL_STRING(s(c, "pubkey_sha256_hex"), sha);
    TEST_ASSERT_EQUAL_INT(GADGET_OK, gadget_id_from_pubkey(pub, id));
    TEST_ASSERT_EQUAL_STRING(s(c, "id"), id);
  }
  cJSON_Delete(root);
}

static void test_rfc6979(void) {
  cJSON *root;
  const cJSON *c;
  cJSON_ArrayForEach(c, load_cases("rfc6979", &root)) {
    sign_and_compare(s(c, "private_key_hex"), s(c, "message_utf8"), s(c, "der_hex"));
    uint8_t der[80], raw[64], want_raw[64];
    size_t n = unhex(s(c, "der_hex"), der, sizeof der);
    TEST_ASSERT_EQUAL_INT(GADGET_OK, gadget_der_to_raw(der, n, raw));
    unhex(s(c, "raw_hex"), want_raw, sizeof want_raw);
    TEST_ASSERT_EQUAL_MEMORY(want_raw, raw, 64);
    TEST_ASSERT_EQUAL_INT_MESSAGE(b(c, "high_s"), is_high_s(raw), s(c, "name"));
    uint8_t priv[32], pub[65];
    unhex(s(c, "private_key_hex"), priv, sizeof priv);
    hal_crypto_pubkey(priv, pub);
    const char *msg = s(c, "message_utf8");
    TEST_ASSERT_EQUAL_INT(GADGET_OK, hal_crypto_verify(pub, (const uint8_t *)msg, strlen(msg), der, n));
  }
  cJSON_Delete(root);
}

static void test_prove(void) {
  cJSON *root;
  const cJSON *c;
  cJSON_ArrayForEach(c, load_cases("prove", &root)) {
    const char *name = s(c, "name"), *expect = s(c, "expect");
    char text[512];
    gp_prove_text(text, sizeof text, s(c, "id"), s(c, "nonce_b64"), s(c, "host_id"));
    TEST_ASSERT_EQUAL_STRING_MESSAGE(s(c, "text"), text, name);
    uint8_t pub[80], der[80];
    size_t pub_len = 0, der_len = 0;
    gadget_status_t b64st = gadget_b64_decode(s(c, "pubkey_b64"), pub, sizeof pub, &pub_len);
    if (strcmp(expect, "reject_base64") == 0) {
      TEST_ASSERT_EQUAL_INT_MESSAGE(GADGET_ERR_PARSE, b64st, name);
      continue;
    }
    TEST_ASSERT_EQUAL_INT_MESSAGE(GADGET_OK, b64st, name);
    if (strcmp(expect, "reject_pubkey") == 0) {
      TEST_ASSERT_TRUE_MESSAGE(pub_len != GADGET_PUBKEY_LEN || pub[0] != 0x04, name);
      continue;
    }
    TEST_ASSERT_EQUAL_size_t(GADGET_PUBKEY_LEN, pub_len);
    char id[GADGET_ID_LEN + 1];
    gadget_id_from_pubkey(pub, id);
    if (strcmp(expect, "reject_id") == 0) {
      TEST_ASSERT_TRUE_MESSAGE(strcmp(id, s(c, "id")) != 0, name);
      continue;
    }
    TEST_ASSERT_EQUAL_STRING_MESSAGE(s(c, "id"), id, name);
    if (strcmp(expect, "reject_host_id") == 0) {
      TEST_ASSERT_FALSE_MESSAGE(gadget_host_id_valid(s(c, "host_id")), name);
      continue;
    }
    TEST_ASSERT_TRUE_MESSAGE(gadget_host_id_valid(s(c, "host_id")), name);
    TEST_ASSERT_EQUAL_INT(GADGET_OK, gadget_b64_decode(s(c, "sig_b64"), der, sizeof der, &der_len));
    uint8_t der2[80];
    TEST_ASSERT_EQUAL_size_t(der_len, unhex(s(c, "sig_der_hex"), der2, sizeof der2));
    TEST_ASSERT_EQUAL_MEMORY(der2, der, der_len);
    gadget_status_t v = hal_crypto_verify(pub, (const uint8_t *)text, strlen(text), der, der_len);
    if (strcmp(expect, "reject_sig") == 0) {
      TEST_ASSERT_EQUAL_INT_MESSAGE(GADGET_ERR_BAD_SIG, v, name);
      continue;
    }
    TEST_ASSERT_EQUAL_STRING_MESSAGE("accept", expect, name); /* unknown expect values fail here */
    TEST_ASSERT_EQUAL_INT_MESSAGE(GADGET_OK, v, name);
    uint8_t raw[64];
    TEST_ASSERT_EQUAL_INT(GADGET_OK, gadget_der_to_raw(der, der_len, raw));
    TEST_ASSERT_EQUAL_INT_MESSAGE(b(c, "high_s"), is_high_s(raw), name);
    if (b(c, "deterministic")) sign_and_compare(s(c, "private_key_hex"), text, s(c, "sig_der_hex"));
  }
  cJSON_Delete(root);
}

static void test_der(void) {
  cJSON *root;
  const cJSON *c;
  cJSON_ArrayForEach(c, load_cases("der", &root)) {
    uint8_t der[96], raw[64];
    size_t n = unhex(s(c, "der_hex"), der, sizeof der);
    if (!b(c, "valid")) {
      TEST_ASSERT_EQUAL_INT_MESSAGE(GADGET_ERR_PARSE, gadget_der_to_raw(der, n, raw), s(c, "name"));
      continue;
    }
    TEST_ASSERT_EQUAL_size_t(u(c, "der_len"), n);
    TEST_ASSERT_EQUAL_INT_MESSAGE(GADGET_OK, gadget_der_to_raw(der, n, raw), s(c, "name"));
    char hex[129];
    gadget_hex_encode(hex, raw, 64);
    TEST_ASSERT_EQUAL_STRING(s(c, "raw_hex"), hex);
    uint8_t again[GADGET_SIG_DER_MAX];
    size_t again_len = 0;
    TEST_ASSERT_EQUAL_INT(GADGET_OK, gadget_der_from_raw(raw, again, sizeof again, &again_len));
    TEST_ASSERT_EQUAL_size_t(n, again_len);
    TEST_ASSERT_EQUAL_MEMORY(der, again, n);
    const cJSON *msg = cJSON_GetObjectItemCaseSensitive(c, "message_utf8");
    if (cJSON_IsString(msg)) sign_and_compare(s(c, "private_key_hex"), msg->valuestring, s(c, "der_hex"));
  }
  cJSON_Delete(root);
}

static void test_base64(void) {
  cJSON *root;
  const cJSON *c;
  cJSON_ArrayForEach(c, load_cases("base64", &root)) {
    uint8_t buf[128];
    size_t n = 0;
    gadget_status_t st = gadget_b64_decode(s(c, "input"), buf, sizeof buf, &n);
    if (!b(c, "canonical")) {
      TEST_ASSERT_EQUAL_INT_MESSAGE(GADGET_ERR_PARSE, st, s(c, "name"));
      continue;
    }
    TEST_ASSERT_EQUAL_INT_MESSAGE(GADGET_OK, st, s(c, "name"));
    char hex[257], again[256];
    gadget_hex_encode(hex, buf, n);
    TEST_ASSERT_EQUAL_STRING(s(c, "bytes_hex"), hex);
    gadget_b64_encode(again, sizeof again, buf, n);
    TEST_ASSERT_EQUAL_STRING(s(c, "input"), again);
  }
  cJSON_Delete(root);
}

static void test_firmware(void) {
  cJSON *root;
  const cJSON *c;
  cJSON_ArrayForEach(c, load_cases("firmware", &root)) {
    const char *name = s(c, "name"), *expect = s(c, "expect");
    char text[512];
    gp_firmware_text(text, sizeof text, s(c, "gadget_board"), s(c, "version"), u(c, "size"), s(c, "sha256"));
    TEST_ASSERT_EQUAL_STRING_MESSAGE(s(c, "text"), text, name);
    uint8_t pub[80], der[80];
    size_t pub_len = 0, der_len = 0;
    TEST_ASSERT_EQUAL_INT(GADGET_OK, gadget_b64_decode(s(c, "pubkey_b64"), pub, sizeof pub, &pub_len));
    TEST_ASSERT_EQUAL_size_t(GADGET_PUBKEY_LEN, pub_len);
    TEST_ASSERT_EQUAL_INT(GADGET_OK, gadget_b64_decode(s(c, "sig_b64"), der, sizeof der, &der_len));
    gadget_status_t v = hal_crypto_verify(pub, (const uint8_t *)text, strlen(text), der, der_len);
    if (strcmp(expect, "bad_sig") == 0) {
      TEST_ASSERT_EQUAL_INT_MESSAGE(GADGET_ERR_BAD_SIG, v, name);
      continue;
    }
    TEST_ASSERT_EQUAL_STRING_MESSAGE("accept", expect, name);
    TEST_ASSERT_EQUAL_INT_MESSAGE(GADGET_OK, v, name);
    if (b(c, "deterministic")) sign_and_compare(s(c, "private_key_hex"), text, s(c, "sig_der_hex"));
  }
  cJSON_Delete(root);
}

static void test_frames(void) {
  cJSON *root;
  const cJSON *c;
  cJSON_ArrayForEach(c, load_cases("frames", &root)) {
    const char *name = s(c, "name");
    static uint8_t frame[GADGET_BINARY_FRAME_MAX + 16];
    size_t n = unhex(s(c, "frame_hex"), frame, sizeof frame);
    gp_bin_kind_t kind;
    uint8_t stream;
    const uint8_t *payload;
    size_t plen;
    gadget_status_t st = gp_bin_decode(frame, n, &kind, &stream, &payload, &plen);
    if (!b(c, "valid")) {
      bool chunk_bad = st == GADGET_OK && kind == GP_BIN_FIRMWARE;
      if (chunk_bad) {
        uint32_t off;
        const uint8_t *data;
        size_t dlen;
        st = gp_fw_chunk_decode(payload, plen, &off, &data, &dlen);
      }
      TEST_ASSERT_EQUAL_INT_MESSAGE(GADGET_ERR_PARSE, st, name);
      continue;
    }
    TEST_ASSERT_EQUAL_INT_MESSAGE(GADGET_OK, st, name);
    TEST_ASSERT_EQUAL_UINT32(u(c, "kind"), kind);
    TEST_ASSERT_EQUAL_UINT32(u(c, "stream"), stream);
    static uint8_t want[GADGET_BINARY_FRAME_MAX];
    size_t want_len = unhex(s(c, "payload_hex"), want, sizeof want);
    TEST_ASSERT_EQUAL_size_t(want_len, plen);
    TEST_ASSERT_EQUAL_MEMORY(want, payload, plen);
    static uint8_t again[GADGET_BINARY_FRAME_MAX];
    TEST_ASSERT_EQUAL_size_t(n, gp_bin_encode(again, sizeof again, kind, stream, payload, plen));
    TEST_ASSERT_EQUAL_MEMORY(frame, again, n);
    if (kind == GP_BIN_FIRMWARE) {
      uint32_t off;
      const uint8_t *data;
      size_t dlen;
      TEST_ASSERT_EQUAL_INT(GADGET_OK, gp_fw_chunk_decode(payload, plen, &off, &data, &dlen));
      TEST_ASSERT_EQUAL_UINT32(u(c, "offset"), off);
      size_t dwant = unhex(s(c, "data_hex"), want, sizeof want);
      TEST_ASSERT_EQUAL_size_t(dwant, dlen);
      TEST_ASSERT_EQUAL_MEMORY(want, data, dlen);
    }
  }
  cJSON_Delete(root);
}

int main(int argc, char **argv) {
  if (argc < 2) {
    fprintf(stderr, "usage: test_vectors <protocol/vectors dir>\n");
    return 2;
  }
  g_dir = argv[1];
  if (psa_crypto_init() != PSA_SUCCESS) return 3;
  UNITY_BEGIN();
  RUN_TEST(test_sha256sums_cover_the_exact_bytes);
  RUN_TEST(test_identity);
  RUN_TEST(test_rfc6979);
  RUN_TEST(test_prove);
  RUN_TEST(test_der);
  RUN_TEST(test_base64);
  RUN_TEST(test_firmware);
  RUN_TEST(test_frames);
  return UNITY_END();
}
