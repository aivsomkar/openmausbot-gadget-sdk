/* firmware/core/include/gadget_core.h */
/* SPDX-License-Identifier: Apache-2.0 */
/* Core public API (spec §5.1, §5.2). Portable C11; single-threaded. */
#ifndef GADGET_CORE_H
#define GADGET_CORE_H

#include "gadget_board.h"
#include "gadget_events.h"
#include "gadget_ui_model.h"

/* Storage keys (namespace "gadget"; ≤ 15 bytes each). */
#define GADGET_KEY_DEV_KEY    "dev_key"     /* blob, 32-byte P-256 scalar */
#define GADGET_KEY_HOST_ID    "host_id"     /* str, 32 lowercase hex, written on ready */
#define GADGET_KEY_HOST_NAME  "host_name"   /* str, from challenge, written on ready */
#define GADGET_KEY_HOST_ADDR  "host_addr"   /* str "addr:port"; absent = host auto */
#define GADGET_KEY_PAIR_CODE  "pair_code"   /* str, 6 digits, cleared on ready / bad_code */
#define GADGET_KEY_WIFI_SSID  "wifi_ssid"   /* str */
#define GADGET_KEY_WIFI_PASS  "wifi_pass"   /* str ("" = open network) */
#define GADGET_KEY_NAME       "name"        /* str, ≤ 32 chars (console `name` or settings.name) */
#define GADGET_KEY_BOT_ID     "bot_id"      /* str, from ready/settings */
#define GADGET_KEY_BOT_NAME   "bot_name"    /* str, from ready/settings */
#define GADGET_KEY_SPEAK_PUSH "speak_push"  /* str "1"/"0", settings.speak_pushes */

typedef struct core_config {
  const gadget_board_t *board;   /* required */
  const char *fw_version;        /* required: PROJECT_VER, e.g. "1.1.0" or "0.0.0-dev" (≤ 32 bytes) */
  uint32_t prng_seed;            /* 0 = seed from hal_crypto_random(); headless sim passes --seed (default 1) */
  bool fail_probation;           /* test only (sim --fail-probation): ignore the first ready on a pending image */
  const char *default_name;      /* name used when storage has none; NULL = "Maus " + 4 hex of the id */
  uint32_t probation_ms;         /* test only (sim --probation-ms): probation length; 0 = GADGET_PROBATION_MS. ESP32 passes 0 */
} core_config_t;

typedef enum {
  CORE_PAIR_UNPAIRED = 0,   /* @omb pair "unpaired" */
  CORE_PAIR_CODE_STORED,    /* "code_stored" */
  CORE_PAIR_CONNECTING,     /* "connecting" */
  CORE_PAIR_PAIRED,         /* "paired": ready arrived on the current connection */
  CORE_PAIR_ERROR           /* "error": see core_last_error() */
} core_pair_state_t;

/* Load or create the identity (first boot: hal_crypto_keygen, then store
 * dev_key), read storage, register built-in actions ("chime"), start
 * probation if hal_ota_running_state() is PENDING_VERIFY, print the @omb
 * boot line. Call once, after the port initialized its HAL and called
 * psa_crypto_init(). */
gadget_status_t core_init(const core_config_t *cfg);
/* Deliver one event (main thread only). */
void core_event(const gadget_event_t *ev);
/* Run timers, reconnects, jitter-buffer feeding and screen selection.
 * now_ms is monotonic and is core's only clock. Call every 10 ms. */
void core_tick(uint64_t now_ms);
const ui_model_t *core_ui_model(void);
/* Tests and the simulator: free everything so core_init() can run again
 * (a simulated reboot inside one process). */
void core_deinit(void);

/* Introspection (console status, simulator `model`, tests). */
const char *core_device_id(void);              /* "gad_…" */
core_pair_state_t core_pair_state(void);
const char *core_last_error(void);             /* last handshake error code, "" when none */
const char *core_fw_version(void);

/* Protocol tap: called for every text frame crossing the socket, after
 * decode (rx) or before send (tx). Used by the simulator's `expect` and
 * --trace, and by tests. op is the frame's "op" string. */
typedef enum { CORE_TAP_RX = 0, CORE_TAP_TX } core_tap_dir_t;
typedef void (*core_tap_fn)(core_tap_dir_t dir, const char *op, const char *json, size_t len, void *ctx);
void core_set_tap(core_tap_fn fn, void *ctx);

#endif /* GADGET_CORE_H */
