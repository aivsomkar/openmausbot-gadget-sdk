/* firmware/core/src/util.c */
/* SPDX-License-Identifier: Apache-2.0 */
/* Pure helpers (gadget_util.h): base64, hex, DER, validators, UTF-8, PRNG. */
#include <string.h>
#include "gadget_hal.h"
#include "gadget_util.h"

/* ---- base64 (RFC 4648 §4, standard alphabet, padded) ------------------- */

static const char B64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

size_t gadget_b64_encode(char *out, size_t cap, const uint8_t *in, size_t len) {
  size_t need = ((len + 2) / 3) * 4;
  if (out == NULL || cap < need + 1) return 0;
  size_t o = 0;
  for (size_t i = 0; i < len; i += 3) {
    uint32_t v = (uint32_t)in[i] << 16;
    if (i + 1 < len) v |= (uint32_t)in[i + 1] << 8;
    if (i + 2 < len) v |= in[i + 2];
    out[o++] = B64[(v >> 18) & 63];
    out[o++] = B64[(v >> 12) & 63];
    out[o++] = (i + 1 < len) ? B64[(v >> 6) & 63] : '=';
    out[o++] = (i + 2 < len) ? B64[v & 63] : '=';
  }
  out[o] = '\0';
  return o;
}

static int b64_value(char c) {
  if (c >= 'A' && c <= 'Z') return c - 'A';
  if (c >= 'a' && c <= 'z') return c - 'a' + 26;
  if (c >= '0' && c <= '9') return c - '0' + 52;
  if (c == '+') return 62;
  if (c == '/') return 63;
  return -1;
}

gadget_status_t gadget_b64_decode(const char *in, uint8_t *out, size_t cap, size_t *len) {
  if (in == NULL || len == NULL) return GADGET_ERR_ARG;
  size_t n = strlen(in);
  if (n % 4 != 0) return GADGET_ERR_PARSE;
  size_t pad = 0;
  if (n >= 1 && in[n - 1] == '=') pad++;
  if (n >= 2 && in[n - 2] == '=') pad++;
  size_t out_len = n / 4 * 3 - pad;
  for (size_t i = 0; i < n - pad; i++) {
    if (b64_value(in[i]) < 0) return GADGET_ERR_PARSE; /* also rejects '=' before the end */
  }
  if (pad > 0) {
    /* the bits that padding hides must be zero, or re-encoding would differ */
    int last = b64_value(in[n - pad - 1]);
    if (pad == 1 && (last & 0x03) != 0) return GADGET_ERR_PARSE;
    if (pad == 2 && (last & 0x0f) != 0) return GADGET_ERR_PARSE;
  }
  if (out_len > cap) return GADGET_ERR_LIMIT;
  size_t o = 0;
  for (size_t i = 0; i < n; i += 4) {
    uint32_t v = 0;
    for (size_t j = 0; j < 4; j++) {
      int d = (in[i + j] == '=') ? 0 : b64_value(in[i + j]);
      v = (v << 6) | (uint32_t)d;
    }
    if (o < out_len) out[o++] = (uint8_t)(v >> 16);
    if (o < out_len) out[o++] = (uint8_t)(v >> 8);
    if (o < out_len) out[o++] = (uint8_t)v;
  }
  *len = out_len;
  return GADGET_OK;
}

/* ---- hex (lowercase only, both ways) ------------------------------------ */

void gadget_hex_encode(char *out, const uint8_t *in, size_t len) {
  static const char H[] = "0123456789abcdef";
  for (size_t i = 0; i < len; i++) {
    out[2 * i] = H[in[i] >> 4];
    out[2 * i + 1] = H[in[i] & 15];
  }
  out[2 * len] = '\0';
}

static int hex_value(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  return -1;
}

gadget_status_t gadget_hex_decode(const char *in, uint8_t *out, size_t cap, size_t *len) {
  if (in == NULL || len == NULL) return GADGET_ERR_ARG;
  size_t n = strlen(in);
  if (n % 2 != 0) return GADGET_ERR_PARSE;
  for (size_t i = 0; i < n; i++) {
    if (hex_value(in[i]) < 0) return GADGET_ERR_PARSE;
  }
  if (n / 2 > cap) return GADGET_ERR_LIMIT;
  for (size_t i = 0; i < n / 2; i++) {
    out[i] = (uint8_t)((hex_value(in[2 * i]) << 4) | hex_value(in[2 * i + 1]));
  }
  *len = n / 2;
  return GADGET_OK;
}

/* ---- ECDSA signature DER <-> raw r||s ----------------------------------- */

/* Writes one DER INTEGER for a 32-byte big-endian unsigned value. */
static size_t der_put_int(uint8_t *out, const uint8_t v[32]) {
  size_t skip = 0;
  while (skip < 31 && v[skip] == 0) skip++;
  size_t n = 32 - skip;
  bool pad = (v[skip] & 0x80) != 0;
  out[0] = 0x02;
  out[1] = (uint8_t)(n + (pad ? 1 : 0));
  size_t o = 2;
  if (pad) out[o++] = 0x00;
  memcpy(out + o, v + skip, n);
  return o + n;
}

gadget_status_t gadget_der_from_raw(const uint8_t raw[64], uint8_t *der, size_t cap, size_t *der_len) {
  if (raw == NULL || der == NULL || der_len == NULL) return GADGET_ERR_ARG;
  uint8_t tmp[GADGET_SIG_DER_MAX];
  size_t o = 2;
  o += der_put_int(tmp + o, raw);
  o += der_put_int(tmp + o, raw + 32);
  tmp[0] = 0x30;
  tmp[1] = (uint8_t)(o - 2);
  if (o > cap) return GADGET_ERR_LIMIT;
  memcpy(der, tmp, o);
  *der_len = o;
  return GADGET_OK;
}

/* Reads one canonical DER INTEGER into a right-aligned 32-byte value. */
static gadget_status_t der_get_int(const uint8_t *p, size_t avail, size_t *used, uint8_t out[32]) {
  if (avail < 3 || p[0] != 0x02) return GADGET_ERR_PARSE;
  size_t n = p[1];
  if (n < 1 || n > 33 || n + 2 > avail) return GADGET_ERR_PARSE;
  const uint8_t *v = p + 2;
  if (v[0] & 0x80) return GADGET_ERR_PARSE;                     /* negative */
  if (n > 1 && v[0] == 0x00 && (v[1] & 0x80) == 0) return GADGET_ERR_PARSE; /* non-minimal */
  if (v[0] == 0x00 && n > 1) {
    v++;
    n--;
  }
  if (n > 32) return GADGET_ERR_PARSE;
  memset(out, 0, 32);
  memcpy(out + 32 - n, v, n);
  *used = (size_t)(p[1]) + 2;
  return GADGET_OK;
}

gadget_status_t gadget_der_to_raw(const uint8_t *der, size_t der_len, uint8_t raw[64]) {
  if (der == NULL || raw == NULL) return GADGET_ERR_ARG;
  if (der_len < 8 || der[0] != 0x30 || der[1] >= 0x80) return GADGET_ERR_PARSE;
  if ((size_t)der[1] + 2 != der_len) return GADGET_ERR_PARSE;
  size_t used = 0, off = 2;
  gadget_status_t st = der_get_int(der + off, der_len - off, &used, raw);
  if (st != GADGET_OK) return st;
  off += used;
  st = der_get_int(der + off, der_len - off, &used, raw + 32);
  if (st != GADGET_OK) return st;
  off += used;
  return off == der_len ? GADGET_OK : GADGET_ERR_PARSE;
}

/* ---- identity ----------------------------------------------------------- */

gadget_status_t gadget_id_from_pubkey(const uint8_t pub[GADGET_PUBKEY_LEN], char out[GADGET_ID_LEN + 1]) {
  if (pub == NULL || out == NULL) return GADGET_ERR_ARG;
  uint8_t hash[GADGET_SHA256_LEN];
  gadget_status_t st = hal_crypto_sha256(pub, GADGET_PUBKEY_LEN, hash);
  if (st != GADGET_OK) return st;
  memcpy(out, "gad_", 4);
  gadget_hex_encode(out + 4, hash, 8); /* 16 hex characters */
  return GADGET_OK;
}

/* ---- validators --------------------------------------------------------- */

bool gadget_host_id_valid(const char *host_id) {
  if (host_id == NULL || strlen(host_id) != GADGET_HOST_ID_LEN) return false;
  for (size_t i = 0; i < GADGET_HOST_ID_LEN; i++) {
    if (hex_value(host_id[i]) < 0) return false;
  }
  return true;
}

bool gadget_pair_code_valid(const char *code) {
  if (code == NULL || strlen(code) != GADGET_PAIR_CODE_LEN) return false;
  for (size_t i = 0; i < GADGET_PAIR_CODE_LEN; i++) {
    if (code[i] < '0' || code[i] > '9') return false;
  }
  return true;
}

/* ---- UTF-8 -------------------------------------------------------------- */

static const char ELLIPSIS[] = "\xe2\x80\xa6"; /* U+2026 */

static bool is_cont(unsigned char c) { return (c & 0xC0) == 0x80; }

size_t gadget_utf8_copy(char *dst, size_t cap, const char *src) {
  if (dst == NULL || cap == 0) return 0;
  if (src == NULL) src = "";
  size_t n = strlen(src);
  if (n < cap) {
    memcpy(dst, src, n + 1);
    return n;
  }
  bool ell = cap >= 4;
  size_t keep = ell ? cap - 4 : cap - 1;
  while (keep > 0 && is_cont((unsigned char)src[keep])) keep--;
  memcpy(dst, src, keep);
  size_t o = keep;
  if (ell) {
    memcpy(dst + o, ELLIPSIS, 3);
    o += 3;
  }
  dst[o] = '\0';
  return o;
}

size_t gadget_utf8_copy_tail(char *dst, size_t cap, const char *src) {
  if (dst == NULL || cap == 0) return 0;
  if (src == NULL) src = "";
  size_t n = strlen(src);
  if (n < cap) {
    memcpy(dst, src, n + 1);
    return n;
  }
  bool ell = cap >= 4;
  size_t keep = ell ? cap - 4 : cap - 1;
  size_t start = n - keep;
  while (start < n && is_cont((unsigned char)src[start])) start++;
  size_t o = 0;
  if (ell) {
    memcpy(dst, ELLIPSIS, 3);
    o = 3;
  }
  memcpy(dst + o, src + start, n - start);
  o += n - start;
  dst[o] = '\0';
  return o;
}

/* Length of the valid UTF-8 sequence at s, or 0 when it is invalid. */
static size_t utf8_seq_len(const unsigned char *s) {
  size_t n;
  if (s[0] < 0x80) return 1;
  if (s[0] >= 0xC2 && s[0] <= 0xDF) n = 2;
  else if (s[0] >= 0xE0 && s[0] <= 0xEF) n = 3;
  else if (s[0] >= 0xF0 && s[0] <= 0xF4) n = 4;
  else return 0;
  for (size_t i = 1; i < n; i++) {
    if (!is_cont(s[i])) return 0;
  }
  return n;
}

size_t gadget_utf8_len(const char *s) {
  if (s == NULL) return 0;
  size_t count = 0;
  const unsigned char *p = (const unsigned char *)s;
  while (*p) {
    size_t n = utf8_seq_len(p);
    p += n ? n : 1;
    count++;
  }
  return count;
}

/* ---- xorshift32 --------------------------------------------------------- */

void gadget_prng_seed(gadget_prng_t *p, uint32_t seed) { p->s = seed ? seed : 0x9E3779B9u; }

uint32_t gadget_prng_next(gadget_prng_t *p) {
  uint32_t s = p->s;
  s ^= s << 13;
  s ^= s >> 17;
  s ^= s << 5;
  p->s = s;
  return s;
}

uint32_t gadget_prng_range(gadget_prng_t *p, uint32_t lo, uint32_t hi) {
  if (hi <= lo) return lo;
  uint32_t span = hi - lo + 1u;
  uint32_t v = gadget_prng_next(p);
  return span == 0 ? v : lo + v % span;
}
