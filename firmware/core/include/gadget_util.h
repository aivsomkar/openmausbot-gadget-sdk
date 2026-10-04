/* firmware/core/include/gadget_util.h */
/* SPDX-License-Identifier: Apache-2.0 */
/* Pure helpers shared by core, UI, ports and tests. No allocation. */
#ifndef GADGET_UTIL_H
#define GADGET_UTIL_H

#include "gadget_types.h"

/* Base64, RFC 4648 §4, standard alphabet, with padding. */
/* Writes a NUL-terminated string; returns its length, or 0 if cap is too small. */
size_t gadget_b64_encode(char *out, size_t cap, const uint8_t *in, size_t len);
/* Strict decode: rejects anything that does not re-encode to the same text
 * (missing or extra padding, non-zero pad bits, '-', '_', whitespace).
 * GADGET_ERR_PARSE or GADGET_ERR_LIMIT (cap too small). */
gadget_status_t gadget_b64_decode(const char *in, uint8_t *out, size_t cap, size_t *len);

/* Lowercase hex; out must hold 2*len + 1 bytes. */
void gadget_hex_encode(char *out, const uint8_t *in, size_t len);
gadget_status_t gadget_hex_decode(const char *in, uint8_t *out, size_t cap, size_t *len);

/* ECDSA P-256 signature conversion (spec §5.2: our own code, not mbedTLS'). */
/* raw = r||s, 32 bytes each, big-endian. DER = minimal canonical encoding. */
gadget_status_t gadget_der_from_raw(const uint8_t raw[64], uint8_t *der, size_t cap, size_t *der_len);
gadget_status_t gadget_der_to_raw(const uint8_t *der, size_t der_len, uint8_t raw[64]); /* PARSE on non-canonical or >32-byte integers */

/* "gad_" + first 16 lowercase hex chars of SHA-256(pub). out holds GADGET_ID_LEN + 1. */
gadget_status_t gadget_id_from_pubkey(const uint8_t pub[GADGET_PUBKEY_LEN], char out[GADGET_ID_LEN + 1]);
/* /^[0-9a-f]{32}$/ */
bool gadget_host_id_valid(const char *host_id);
/* /^\d{6}$/ */
bool gadget_pair_code_valid(const char *code);

/* UTF-8: copy src into dst (cap bytes incl. NUL), cutting on a code-point
 * boundary and ending with U+2026 "…" when cut. Returns bytes written. */
size_t gadget_utf8_copy(char *dst, size_t cap, const char *src);
/* Keep the END of src (reply tails), starting with "…" when cut. */
size_t gadget_utf8_copy_tail(char *dst, size_t cap, const char *src);
/* Number of code points; invalid sequences count one per byte. */
size_t gadget_utf8_len(const char *s);

/* xorshift32, the one PRNG core and UI use (spec §5.5 determinism):
 *   s ^= s << 13; s ^= s >> 17; s ^= s << 5;  seed 0 is replaced by 0x9E3779B9. */
typedef struct { uint32_t s; } gadget_prng_t;
void gadget_prng_seed(gadget_prng_t *p, uint32_t seed);
uint32_t gadget_prng_next(gadget_prng_t *p);
uint32_t gadget_prng_range(gadget_prng_t *p, uint32_t lo, uint32_t hi);  /* inclusive; lo when hi <= lo */

#endif /* GADGET_UTIL_H */
