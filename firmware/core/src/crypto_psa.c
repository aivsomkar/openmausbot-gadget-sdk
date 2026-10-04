/* firmware/core/src/crypto_psa.c */
/* SPDX-License-Identifier: Apache-2.0 */
/* The HAL crypto group for both ports, on the PSA Crypto API only (spec
 * §5.2). Builds unchanged on mbedTLS 3.6.x (ESP-IDF 5.5, the simulator) and
 * 4.x (ESP-IDF 6.0). Volatile keys only: the 32-byte scalar lives in
 * storage key dev_key. DER <-> raw conversion is ours (gadget_util.h).
 * Ports call psa_crypto_init() once before core_init(). */
#include <stdlib.h>
#include <string.h>
#include "gadget_hal.h"
#include "gadget_util.h"
#include "psa/crypto.h"

#define ALG_SIGN PSA_ALG_DETERMINISTIC_ECDSA(PSA_ALG_SHA_256)
#define ALG_VERIFY PSA_ALG_ECDSA(PSA_ALG_SHA_256)

static void pair_attributes(psa_key_attributes_t *a) {
  *a = psa_key_attributes_init();
  psa_set_key_type(a, PSA_KEY_TYPE_ECC_KEY_PAIR(PSA_ECC_FAMILY_SECP_R1));
  psa_set_key_bits(a, 256);
  psa_set_key_usage_flags(a, PSA_KEY_USAGE_SIGN_HASH | PSA_KEY_USAGE_EXPORT);
  psa_set_key_algorithm(a, ALG_SIGN);
}

static gadget_status_t import_pair(const uint8_t priv[GADGET_PRIVKEY_LEN], psa_key_id_t *id) {
  psa_key_attributes_t a;
  pair_attributes(&a);
  return psa_import_key(&a, priv, GADGET_PRIVKEY_LEN, id) == PSA_SUCCESS ? GADGET_OK : GADGET_ERR_CRYPTO;
}

static gadget_status_t export_public(psa_key_id_t id, uint8_t pub[GADGET_PUBKEY_LEN]) {
  size_t n = 0;
  if (psa_export_public_key(id, pub, GADGET_PUBKEY_LEN, &n) != PSA_SUCCESS || n != GADGET_PUBKEY_LEN) {
    return GADGET_ERR_CRYPTO;
  }
  return GADGET_OK;
}

gadget_status_t hal_crypto_keygen(uint8_t priv[GADGET_PRIVKEY_LEN], uint8_t pub[GADGET_PUBKEY_LEN]) {
  psa_key_attributes_t a;
  psa_key_id_t id = 0;
  pair_attributes(&a);
  if (psa_generate_key(&a, &id) != PSA_SUCCESS) return GADGET_ERR_CRYPTO;
  size_t n = 0;
  gadget_status_t st = GADGET_ERR_CRYPTO;
  if (psa_export_key(id, priv, GADGET_PRIVKEY_LEN, &n) == PSA_SUCCESS && n == GADGET_PRIVKEY_LEN) {
    st = export_public(id, pub);
  }
  psa_destroy_key(id);
  return st;
}

gadget_status_t hal_crypto_pubkey(const uint8_t priv[GADGET_PRIVKEY_LEN], uint8_t pub[GADGET_PUBKEY_LEN]) {
  psa_key_id_t id = 0;
  gadget_status_t st = import_pair(priv, &id);
  if (st != GADGET_OK) return st;
  st = export_public(id, pub);
  psa_destroy_key(id);
  return st;
}

gadget_status_t hal_crypto_sign(const uint8_t priv[GADGET_PRIVKEY_LEN], const uint8_t *msg, size_t len,
                                uint8_t der[GADGET_SIG_DER_MAX], size_t *der_len) {
  uint8_t hash[GADGET_SHA256_LEN];
  gadget_status_t st = hal_crypto_sha256(msg, len, hash);
  if (st != GADGET_OK) return st;
  psa_key_id_t id = 0;
  st = import_pair(priv, &id);
  if (st != GADGET_OK) return st;
  uint8_t raw[64];
  size_t raw_len = 0;
  psa_status_t ps = psa_sign_hash(id, ALG_SIGN, hash, sizeof hash, raw, sizeof raw, &raw_len);
  psa_destroy_key(id);
  if (ps != PSA_SUCCESS || raw_len != sizeof raw) return GADGET_ERR_CRYPTO;
  return gadget_der_from_raw(raw, der, GADGET_SIG_DER_MAX, der_len);
}

gadget_status_t hal_crypto_verify(const uint8_t pub[GADGET_PUBKEY_LEN], const uint8_t *msg, size_t len,
                                  const uint8_t *der, size_t der_len) {
  uint8_t raw[64];
  if (gadget_der_to_raw(der, der_len, raw) != GADGET_OK) return GADGET_ERR_BAD_SIG;
  uint8_t hash[GADGET_SHA256_LEN];
  if (hal_crypto_sha256(msg, len, hash) != GADGET_OK) return GADGET_ERR_CRYPTO;
  psa_key_attributes_t a = psa_key_attributes_init();
  psa_set_key_type(&a, PSA_KEY_TYPE_ECC_PUBLIC_KEY(PSA_ECC_FAMILY_SECP_R1));
  psa_set_key_bits(&a, 256);
  psa_set_key_usage_flags(&a, PSA_KEY_USAGE_VERIFY_HASH);
  psa_set_key_algorithm(&a, ALG_VERIFY);
  psa_key_id_t id = 0;
  if (psa_import_key(&a, pub, GADGET_PUBKEY_LEN, &id) != PSA_SUCCESS) return GADGET_ERR_BAD_SIG;
  psa_status_t ps = psa_verify_hash(id, ALG_VERIFY, hash, sizeof hash, raw, sizeof raw);
  psa_destroy_key(id);
  return ps == PSA_SUCCESS ? GADGET_OK : GADGET_ERR_BAD_SIG;
}

gadget_status_t hal_crypto_sha256(const void *data, size_t len, uint8_t out[GADGET_SHA256_LEN]) {
  size_t n = 0;
  psa_status_t ps = psa_hash_compute(PSA_ALG_SHA_256, (const uint8_t *)data, len, out, GADGET_SHA256_LEN, &n);
  return (ps == PSA_SUCCESS && n == GADGET_SHA256_LEN) ? GADGET_OK : GADGET_ERR_CRYPTO;
}

gadget_status_t hal_crypto_sha256_begin(hal_sha256_t *ctx) {
  psa_hash_operation_t *op = malloc(sizeof *op);
  if (op == NULL) return GADGET_ERR_NO_MEM;
  *op = psa_hash_operation_init();
  if (psa_hash_setup(op, PSA_ALG_SHA_256) != PSA_SUCCESS) {
    free(op);
    ctx->op = NULL;
    return GADGET_ERR_CRYPTO;
  }
  ctx->op = op;
  return GADGET_OK;
}

gadget_status_t hal_crypto_sha256_update(hal_sha256_t *ctx, const void *data, size_t len) {
  if (ctx->op == NULL) return GADGET_ERR_STATE;
  return psa_hash_update(ctx->op, (const uint8_t *)data, len) == PSA_SUCCESS ? GADGET_OK : GADGET_ERR_CRYPTO;
}

gadget_status_t hal_crypto_sha256_finish(hal_sha256_t *ctx, uint8_t out[GADGET_SHA256_LEN]) {
  if (ctx->op == NULL) return GADGET_ERR_STATE;
  size_t n = 0;
  psa_status_t ps = psa_hash_finish(ctx->op, out, GADGET_SHA256_LEN, &n);
  if (ps != PSA_SUCCESS) psa_hash_abort(ctx->op);
  free(ctx->op);
  ctx->op = NULL;
  return (ps == PSA_SUCCESS && n == GADGET_SHA256_LEN) ? GADGET_OK : GADGET_ERR_CRYPTO;
}

void hal_crypto_sha256_abort(hal_sha256_t *ctx) {
  if (ctx == NULL || ctx->op == NULL) return;
  psa_hash_abort(ctx->op);
  free(ctx->op);
  ctx->op = NULL;
}

gadget_status_t hal_crypto_random(void *buf, size_t len) {
  return psa_generate_random((uint8_t *)buf, len) == PSA_SUCCESS ? GADGET_OK : GADGET_ERR_CRYPTO;
}
