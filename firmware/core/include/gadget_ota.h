/* firmware/core/include/gadget_ota.h */
/* SPDX-License-Identifier: Apache-2.0 */
/* Firmware update state machine and key tables (spec §4.8, §8). Core-
 * internal, public for tests and for release CI's key check. */
#ifndef GADGET_OTA_H
#define GADGET_OTA_H

#include "gadget_types.h"

typedef struct {
  const char *id;                      /* "r1", "r2", … (release) or "t1" (test) */
  uint8_t pub[GADGET_PUBKEY_LEN];      /* SEC1 uncompressed */
} gadget_release_key_t;

/* Every key table has at least one element (C11 has no empty initializer
 * or zero-length array). An empty table is exactly
 *   const gadget_release_key_t gadget_release_keys[] = {{NULL, {0}}};
 *   const size_t gadget_release_keys_count = 0;
 * (the same for gadget_test_keys). Code loops i < *_count and never uses
 * sizeof on a table. */
/* core/src/keys_release.c: release keys only (ids /^r[0-9]+$/). Empty until
 * Omkar commits keys/release-r1.pub.b64; P2d fills it in. */
extern const gadget_release_key_t gadget_release_keys[];
extern const size_t gadget_release_keys_count;
/* core/src/keys_test.c: ALWAYS compiled. Holds t1 under
 * #if defined(GADGET_TEST_KEYS), otherwise the empty table above (count 0).
 * GADGET_TEST_KEYS is a private compile definition on the core library
 * (§2.17 ESP, §2.18 desktop); core sources never read sdkconfig.h. */
extern const gadget_release_key_t gadget_test_keys[];
extern const size_t gadget_test_keys_count;
/* Release table first, then the test table (count 0 unless GADGET_TEST_KEYS).
 * Skips entries whose id is NULL. NULL if unknown. */
const gadget_release_key_t *gadget_key_find(const char *key_id);

typedef enum {
  GADGET_OTA_IDLE = 0,
  GADGET_OTA_RECEIVING,     /* fw.ready sent, chunks arriving */
  GADGET_OTA_WAIT_COMMIT,   /* all bytes durably written */
  GADGET_OTA_FINALIZING,    /* checking size + SHA-256, hal_ota_finalize, set boot */
  GADGET_OTA_RESTARTING
} gadget_ota_state_t;

/* fw.fail codes (PROTOCOL.md §4.8), as sent on the wire. */
#define GADGET_FW_TOO_LARGE    "too_large"
#define GADGET_FW_WRONG_BOARD  "wrong_board"
#define GADGET_FW_SAME_VERSION "same_version"
#define GADGET_FW_UNKNOWN_KEY  "unknown_key"
#define GADGET_FW_BAD_SIG      "bad_sig"
#define GADGET_FW_BUSY         "busy"
#define GADGET_FW_FLASH        "flash"
#define GADGET_FW_SEQUENCE     "sequence"
#define GADGET_FW_CHECKSUM     "checksum"
#define GADGET_FW_TIMEOUT      "timeout"

gadget_ota_state_t core_ota_state(void);

#endif /* GADGET_OTA_H */
