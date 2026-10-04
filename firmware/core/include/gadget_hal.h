/* firmware/core/include/gadget_hal.h */
/* SPDX-License-Identifier: Apache-2.0 */
/* The whole contract between core and a port (spec §5.2).
 *
 * Threading: core and UI are single-threaded. The port calls core_tick()
 * every 10 ms and delivers every event (gadget_events.h) on that same thread.
 * Every hal_* function below is called only from that thread and must not
 * block for more than a few milliseconds, except where noted.
 *
 * The crypto group is implemented by core (core/src/crypto_psa.c) for both
 * ports, on the PSA Crypto API. Ports only call psa_crypto_init() at boot,
 * before core_init(). */
#ifndef GADGET_HAL_H
#define GADGET_HAL_H

#include <stdarg.h>
#include "gadget_types.h"
#include "gadget_events.h"

#if defined(__GNUC__) || defined(__clang__)
#define GADGET_PRINTF(f, a) __attribute__((format(printf, f, a)))
#else
#define GADGET_PRINTF(f, a)
#endif

/* ---- Mic ---------------------------------------------------------------- */
/* Start capture at `rate` (16000 in v1; anything else → GADGET_ERR_UNSUPPORTED).
 * Frames arrive as GADGET_EV_MIC_FRAME, 20 ms each, until hal_mic_stop(). */
gadget_status_t hal_mic_start(uint32_t rate);
void hal_mic_stop(void);

/* ---- Speaker ------------------------------------------------------------ */
/* Open (or keep open) playback at `rate`. Codec boards accept 16000 only. */
gadget_status_t hal_spk_open(uint32_t rate);
/* Queue mono PCM16 (host byte order). Never blocks; returns the number of
 * samples accepted (0 when the port's buffer is full). */
size_t hal_spk_write(const int16_t *pcm, size_t samples);
/* Milliseconds of audio queued but not yet played. */
uint32_t hal_spk_buffered_ms(void);
/* Drop everything queued and go silent: at once on boards with a codec
 * mute; within one DMA ring (≤ 60 ms on the devkit) on boards without one.
 * The device stays open. */
void hal_spk_stop(void);
/* Output level 0–100. */
void hal_spk_set_volume(uint8_t pct);

/* ---- Input -------------------------------------------------------------- */
/* Events only (GADGET_EV_INPUT). Ports report raw talk/cancel edges and raw
 * touch down/move/up; core derives holds, taps and swipes. The board's
 * input_mask says which sources exist. */

/* ---- Storage ------------------------------------------------------------ */
/* One key/value namespace "gadget" (NVS on ESP32, a JSON file in the
 * simulator). Keys are the GADGET_KEY_* names in gadget_core.h (≤ 15 bytes).
 * Writes are durable when the call returns. */
gadget_status_t hal_storage_get_str(const char *key, char *buf, size_t cap);   /* NOT_FOUND, LIMIT if cap too small */
gadget_status_t hal_storage_set_str(const char *key, const char *value);
gadget_status_t hal_storage_get_blob(const char *key, void *buf, size_t cap, size_t *len);
gadget_status_t hal_storage_set_blob(const char *key, const void *data, size_t len);
gadget_status_t hal_storage_erase(const char *key);   /* GADGET_OK when missing */
gadget_status_t hal_storage_erase_all(void);          /* the "gadget" namespace only */

/* ---- Net: Wi-Fi --------------------------------------------------------- */
/* The port starts the radio (ESP32: esp_wifi_start() in STA mode) before
 * core_init(), so the RNG is truly random when core generates the key.
 * The simulator always reports GADGET_WIFI_CONNECTED. */
/* "" = open network; result via GADGET_EV_WIFI_STATE. Before it returns,
 * hal_wifi_state() reports GADGET_WIFI_CONNECTING (or CONNECTED), so a
 * console `status` right after `wifi` shows "connecting". */
gadget_status_t hal_wifi_connect(const char *ssid, const char *password);
void hal_wifi_disconnect(void);
gadget_wifi_state_t hal_wifi_state(void);
gadget_status_t hal_wifi_scan(void);   /* result via GADGET_EV_WIFI_SCAN */

/* ---- Net: WebSocket ----------------------------------------------------- */
/* One connection at a time to ws://<host>:<port>/gadget with subprotocol
 * openmausbot-gadget.1, no Origin header, no extensions, 5 s connect
 * timeout, no automatic reconnect (core drives reconnects). The port
 * answers pings, reassembles fragments and delivers whole messages
 * (text ≤ 16 KiB, binary ≤ 8 KiB). Exactly one GADGET_EV_WS_CLOSED follows
 * every successful hal_ws_open(). */
gadget_status_t hal_ws_open(const char *host, uint16_t port);
gadget_status_t hal_ws_send_text(const char *data, size_t len);        /* BUSY when not open */
gadget_status_t hal_ws_send_binary(const uint8_t *data, size_t len);   /* BUSY when not open */
void hal_ws_close(uint16_t code);

/* ---- Net: mDNS ---------------------------------------------------------- */
/* Browse _openmausbot._tcp for up to timeout_ms; one GADGET_EV_MDNS with
 * every instance found (TXT id= included). Linux simulator:
 * GADGET_ERR_UNSUPPORTED (no event). */
gadget_status_t hal_mdns_browse(uint32_t timeout_ms);

/* ---- Crypto (implemented by core on PSA; ports do not implement) -------- */
/* P-256 key pair; priv = 32-byte scalar, pub = 65-byte SEC1 uncompressed. */
gadget_status_t hal_crypto_keygen(uint8_t priv[GADGET_PRIVKEY_LEN], uint8_t pub[GADGET_PUBKEY_LEN]);
gadget_status_t hal_crypto_pubkey(const uint8_t priv[GADGET_PRIVKEY_LEN], uint8_t pub[GADGET_PUBKEY_LEN]);
/* SHA-256 of msg, then PSA_ALG_DETERMINISTIC_ECDSA(PSA_ALG_SHA_256) via
 * psa_sign_hash; S is never normalized; output is DER (≤ 72 bytes). */
gadget_status_t hal_crypto_sign(const uint8_t priv[GADGET_PRIVKEY_LEN], const uint8_t *msg, size_t len,
                                uint8_t der[GADGET_SIG_DER_MAX], size_t *der_len);
/* SHA-256 of msg, then psa_verify_hash with PSA_ALG_ECDSA(PSA_ALG_SHA_256).
 * Accepts high-S. GADGET_OK or GADGET_ERR_BAD_SIG (also for malformed DER). */
gadget_status_t hal_crypto_verify(const uint8_t pub[GADGET_PUBKEY_LEN], const uint8_t *msg, size_t len,
                                  const uint8_t *der, size_t der_len);
gadget_status_t hal_crypto_sha256(const void *data, size_t len, uint8_t out[GADGET_SHA256_LEN]);
/* Multi-part SHA-256 for OTA images (psa_hash_setup/update/finish/abort).
 * begin mallocs a psa_hash_operation_t, sets it to PSA_HASH_OPERATION_INIT
 * and stores it in op (GADGET_ERR_NO_MEM when malloc fails); finish and
 * abort free it and set op to NULL; abort with op == NULL does nothing.
 * The struct's size never depends on the PSA implementation. */
typedef struct {
  void *op;   /* psa_hash_operation_t *, owned by crypto_psa.c */
} hal_sha256_t;
gadget_status_t hal_crypto_sha256_begin(hal_sha256_t *ctx);
gadget_status_t hal_crypto_sha256_update(hal_sha256_t *ctx, const void *data, size_t len);
gadget_status_t hal_crypto_sha256_finish(hal_sha256_t *ctx, uint8_t out[GADGET_SHA256_LEN]);
void hal_crypto_sha256_abort(hal_sha256_t *ctx);
gadget_status_t hal_crypto_random(void *buf, size_t len);

/* ---- OTA slot ----------------------------------------------------------- */
typedef enum {
  HAL_OTA_IMG_VALID = 0,         /* running image is confirmed (or OTA never used) */
  HAL_OTA_IMG_PENDING_VERIFY,    /* first boot of a new image: probation */
  HAL_OTA_IMG_UNKNOWN
} hal_ota_img_state_t;

/* Open the inactive slot for an image of `size` bytes (≤ the board's ota_max). */
gadget_status_t hal_ota_begin(uint32_t size);
/* Copy `len` bytes for `offset` into the port's write queue and return at
 * once. Offsets are contiguous. The port reports durable progress with
 * GADGET_EV_OTA_WRITTEN and failures with GADGET_EV_OTA_ERROR. The queue
 * holds at least 64 KiB + 4 KiB; BUSY when it is full. */
gadget_status_t hal_ota_write(uint32_t offset, const uint8_t *data, size_t len);
/* All bytes written: validate the image (ESP32: esp_ota_end). May block ≤ 2 s. */
gadget_status_t hal_ota_finalize(void);
/* Make the new slot the boot slot, in probation. `version` is recorded by
 * the simulator in otadata.json. Does not restart. */
gadget_status_t hal_ota_set_boot(const char *version);
void hal_ota_abort(void);
hal_ota_img_state_t hal_ota_running_state(void);
gadget_status_t hal_ota_mark_valid(void);               /* esp_ota_mark_app_valid_cancel_rollback */
_Noreturn void hal_ota_mark_invalid_and_reboot(void);   /* esp_ota_mark_app_invalid_rollback_and_reboot */

/* ---- Battery ------------------------------------------------------------ */
/* false when the board has no battery or none is fitted. Cheap; core polls it. */
bool hal_battery_read(gadget_battery_t *out);

/* ---- System, clock, log, console ---------------------------------------- */
typedef enum { GADGET_LOG_ERROR = 0, GADGET_LOG_WARN, GADGET_LOG_INFO, GADGET_LOG_DEBUG } gadget_log_level_t;

/* Monotonic milliseconds since boot. Core never calls it: core's only clock
 * is the argument of core_tick(). Ports use it to drive core_tick(). */
uint64_t hal_now_ms(void);
_Noreturn void hal_restart(void);    /* simulator: re-exec itself with --boot <n+1> (spec §5.7, contract §2.16) */
void hal_log(gadget_log_level_t level, const char *tag, const char *fmt, ...) GADGET_PRINTF(3, 4);
void hal_vlog(gadget_log_level_t level, const char *tag, const char *fmt, va_list ap);
/* `log off|on`: silences or restores all log output (ESP: esp_log_level_set("*", …)). */
void hal_log_set_enabled(bool enabled);
/* Write one line plus "\n" to the console output. Never silenced. Used for @omb lines. */
void hal_console_write(const char *line);

#endif /* GADGET_HAL_H */
