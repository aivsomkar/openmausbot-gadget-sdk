/* firmware/core/include/gadget_types.h */
/* SPDX-License-Identifier: Apache-2.0 */
/* Shared constants and small types. Values mirror protocol/PROTOCOL.md. */
#ifndef GADGET_TYPES_H
#define GADGET_TYPES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
  GADGET_OK = 0,
  GADGET_ERR = -1,             /* unspecified failure */
  GADGET_ERR_ARG = -2,         /* bad argument */
  GADGET_ERR_NO_MEM = -3,
  GADGET_ERR_NOT_FOUND = -4,   /* storage key missing, unknown id */
  GADGET_ERR_BUSY = -5,        /* try again later (queue full, socket not open) */
  GADGET_ERR_LIMIT = -6,       /* a size or count limit was exceeded */
  GADGET_ERR_IO = -7,          /* flash, file or socket error */
  GADGET_ERR_CRYPTO = -8,      /* PSA call failed */
  GADGET_ERR_BAD_SIG = -9,     /* signature did not verify */
  GADGET_ERR_UNSUPPORTED = -10,/* not available on this port or board */
  GADGET_ERR_STATE = -11,      /* not valid in the current state */
  GADGET_ERR_TIMEOUT = -12,
  GADGET_ERR_PARSE = -13       /* malformed JSON, base64, DER or argument */
} gadget_status_t;

/* Protocol limits (PROTOCOL.md §4.1–§4.3; firmware ones §4.8) */
#define GADGET_PROTO_VERSION 1
#define GADGET_SUBPROTOCOL "openmausbot-gadget.1"
#define GADGET_WS_PATH "/gadget"
#define GADGET_DEFAULT_PORT 8810u
#define GADGET_TEXT_FRAME_MAX 16384u
#define GADGET_BINARY_FRAME_MAX 8192u
#define GADGET_IDLE_TIMEOUT_MS 45000u
#define GADGET_MIC_RATE 16000u
#define GADGET_MIC_FRAME_SAMPLES 320u     /* 20 ms at 16 kHz */
#define GADGET_UTTERANCE_MAX_MS 60000u
#define GADGET_SAY_MAX 2000u              /* characters */
#define GADGET_TURN_MAX 32u               /* bytes, excluding NUL */
#define GADGET_NAME_MAX 32u               /* characters */
#define GADGET_ACTIONS_MAX 16u
#define GADGET_ACTION_NAME_MAX 32u
#define GADGET_ACTION_DESC_MAX 200u
#define GADGET_ACTION_PARAMS_MAX 1024u    /* serialized bytes */
#define GADGET_SENSE_MIN_INTERVAL_MS 10000u
#define GADGET_FW_CHUNK_MAX 4096u
#define GADGET_FW_PROGRESS_EVERY 16384u
#define GADGET_FW_CHUNK_TIMEOUT_MS 30000u
#define GADGET_FW_COMMIT_TIMEOUT_MS 30000u
#define GADGET_PROBATION_MS 300000u
#define GADGET_JITTER_BUFFER_MS 1000u

/* Interaction (spec §5.4, §4.3) */
#define GADGET_PRESS_MIN_MS 300u
#define GADGET_ASK_LOCK_MS 600u
#define GADGET_COUNTDOWN_MS 5000u
#define GADGET_REPLY_IDLE_MS 20000u
#define GADGET_BACKOFF_MIN_MS 2000u
#define GADGET_BACKOFF_MAX_MS 60000u
#define GADGET_DEVICE_LIMIT_RETRY_MS 10000u
#define GADGET_DEVICE_LIMIT_WINDOW_MS 120000u
#define GADGET_HOST_AUTO_TIMEOUT_MS 5000u

/* Identity and crypto sizes */
#define GADGET_ID_LEN 20u                 /* "gad_" + 16 lowercase hex */
#define GADGET_HOST_ID_LEN 32u            /* 32 lowercase hex */
#define GADGET_PAIR_CODE_LEN 6u
#define GADGET_PRIVKEY_LEN 32u
#define GADGET_PUBKEY_LEN 65u             /* SEC1 uncompressed, starts 0x04 */
#define GADGET_PUBKEY_B64_LEN 88u         /* base64 of 65 bytes, with padding */
#define GADGET_NONCE_B64_LEN 44u          /* base64 of 32 bytes */
#define GADGET_SIG_DER_MAX 72u
#define GADGET_SIG_B64_MAX 96u
#define GADGET_SHA256_LEN 32u
#define GADGET_VERSION_MAX 32u            /* firmware version string, bytes */
#define GADGET_BOARD_ID_MAX 32u

typedef struct {
  int16_t x, y, w, h;
} gadget_rect_t;

typedef struct {
  uint8_t pct;     /* 0–100 */
  bool charging;
} gadget_battery_t;

#endif /* GADGET_TYPES_H */
