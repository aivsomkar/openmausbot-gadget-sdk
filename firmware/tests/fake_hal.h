/* firmware/tests/fake_hal.h */
/* SPDX-License-Identifier: Apache-2.0 */
/* An in-memory HAL for core's unit tests. It implements every hal_*
 * function except the crypto group (core's crypto_psa.c is real), records
 * what core asked for, and drives core_tick() on a virtual clock. Results
 * the real ports deliver asynchronously (WS_CLOSED after hal_ws_close,
 * OTA_WRITTEN after hal_ota_write, the Wi-Fi scan) are queued and delivered
 * before the next tick, never from inside a HAL call. */
#ifndef FAKE_HAL_H
#define FAKE_HAL_H

#include <setjmp.h>
#include "cJSON.h"
#include "gadget_core.h"
#include "gadget_hal.h"

/* Contract §1.7 pinned values. */
#define FAKE_RFC_PRIV_HEX "c9afa9d845ba75166b5c215767b1d6934e50c3db36e89b127b8a622b120f6721"
#define FAKE_RFC_ID "gad_b18b86ce1389e46d"
#define FAKE_HOST_ID "000102030405060708090a0b0c0d0e0f"
#define FAKE_NONCE "AAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8="

/* Forget everything (storage, frames, clock, flags). Call in setUp(). */
void fake_reset(void);

/* ---- clock: core_tick() every 10 ms ---------------------------------------- */
uint64_t fake_now(void);
void fake_run(uint32_t ms);                 /* deliver queued events, tick, repeat until ms elapsed */

/* ---- boot helpers ----------------------------------------------------------- */
/* core_deinit() then core_init() with fw "1.0.0", seed 1 and the given board,
 * then fake_run(10). Storage is kept, like a reboot. */
void fake_boot(const char *board_id);
void fake_boot_cfg(const core_config_t *cfg);   /* the same with a caller config */
/* Storage of a paired gadget: the RFC key as dev_key, host_addr
 * 127.0.0.1:8810, host_id FAKE_HOST_ID, host_name "Mac", bot b_jev/Jev. */
void fake_store_paired(void);
/* Accept the pending connection, send challenge (pinned nonce and host_id),
 * check that prove verifies against the gadget's own key, send ready with
 * bot {b_jev, Jev}, and clear the sent-frame log. Asserts on any deviation. */
void fake_handshake(void);
/* fake_store_paired(), fake_boot(board_id), fake_handshake(). */
void fake_ready(const char *board_id);

/* ---- storage ---------------------------------------------------------------- */
void fake_storage_put(const char *key, const char *value);
void fake_storage_put_blob(const char *key, const void *data, size_t len);
const char *fake_storage_str(const char *key);  /* NULL when absent or a blob */
bool fake_storage_has(const char *key);
size_t fake_storage_blob_len(const char *key);  /* 0 when absent */
int fake_storage_writes(void);                  /* set_* calls so far */

/* ---- console output (hal_console_write) ------------------------------------- */
size_t fake_console_count(void);
const char *fake_console_line(size_t i);
/* The last "@omb " line whose op equals op, parsed (caller cJSON_Delete()s), or NULL. */
cJSON *fake_omb(const char *op);
void fake_console_clear(void);
void fake_console_in(const char *line);         /* deliver GADGET_EV_CONSOLE_LINE now */

/* ---- input and mic ---------------------------------------------------------- */
void fake_input(gadget_input_type_t type, int16_t x, int16_t y);   /* deliver GADGET_EV_INPUT now */
void fake_swipe(gadget_swipe_dir_t dir);
bool fake_mic_running(void);
/* Deliver n 20 ms mic frames of a square wave with this amplitude, with
 * fake_run(20) after each. */
void fake_mic_frames(int n, int16_t amplitude);

/* ---- speaker ---------------------------------------------------------------- */
uint32_t fake_spk_rate(void);                   /* last hal_spk_open rate, 0 never */
size_t fake_spk_accepted(void);                 /* samples hal_spk_write accepted so far */
int fake_spk_stops(void);                       /* hal_spk_stop calls */
int16_t fake_spk_peak(void);                    /* largest |sample| accepted since fake_reset */

/* ---- Wi-Fi ------------------------------------------------------------------ */
void fake_wifi_set(gadget_wifi_state_t st);     /* hal_wifi_state() result + deliver GADGET_EV_WIFI_STATE */
const char *fake_wifi_ssid(void);               /* last hal_wifi_connect ssid, "" never */
int fake_wifi_connects(void);
int fake_wifi_scans(void);
void fake_wifi_scan_result(const gadget_wifi_ap_t *aps, uint8_t count, bool ok);  /* deliver GADGET_EV_WIFI_SCAN */

/* ---- WebSocket -------------------------------------------------------------- */
int fake_ws_opens(void);                        /* hal_ws_open calls */
const char *fake_ws_host(void);
uint16_t fake_ws_port(void);
bool fake_ws_live(void);                        /* opened and no WS_CLOSED delivered yet */
uint16_t fake_ws_close_code(void);              /* code of the last hal_ws_close, 0 none */
void fake_ws_accept(void);                      /* deliver GADGET_EV_WS_OPEN */
void fake_ws_in(const char *json);              /* deliver one text frame from the host */
void fake_ws_bin_in(const uint8_t *data, size_t len);
void fake_ws_ping_in(void);                     /* deliver GADGET_EV_WS_CONTROL */
void fake_ws_drop(uint16_t code);               /* deliver GADGET_EV_WS_CLOSED (host side close/drop) */
size_t fake_ws_sent(void);                      /* text frames sent since the last clear */
const char *fake_ws_text(size_t i);
size_t fake_ws_count(const char *op);
cJSON *fake_ws_last(const char *op);            /* the last sent frame with that op, parsed, or NULL */
size_t fake_ws_bin_sent(void);
const uint8_t *fake_ws_bin(size_t i, size_t *len);
void fake_ws_clear(void);

/* ---- mDNS ------------------------------------------------------------------- */
int fake_mdns_browses(void);
void fake_mdns_unsupported(bool on);            /* hal_mdns_browse returns GADGET_ERR_UNSUPPORTED */
void fake_mdns_result(const gadget_mdns_host_t *hosts, uint8_t count);   /* deliver GADGET_EV_MDNS */

/* ---- OTA -------------------------------------------------------------------- */
void fake_ota_set_running(hal_ota_img_state_t st);
uint32_t fake_ota_size(void);                   /* size given to hal_ota_begin, 0 none */
uint32_t fake_ota_written(void);                /* bytes accepted by hal_ota_write */
const uint8_t *fake_ota_image(void);
bool fake_ota_finalized(void);
const char *fake_ota_boot_version(void);        /* hal_ota_set_boot argument, "" none */
int fake_ota_aborts(void);
bool fake_ota_marked_valid(void);
bool fake_ota_invalidated(void);                /* hal_ota_mark_invalid_and_reboot ran */
void fake_ota_fail_writes(bool on);             /* queued writes report GADGET_EV_OTA_ERROR */

/* ---- battery, log ------------------------------------------------------------ */
void fake_battery_set(bool present, uint8_t pct, bool charging);
bool fake_log_enabled(void);

/* ---- restart ----------------------------------------------------------------- */
/* hal_restart() and hal_ota_mark_invalid_and_reboot() longjmp here. Use:
 *   FAKE_EXPECT_RESTART(fake_run(1000));
 * after which the test may fake_boot() again (storage survives). */
extern jmp_buf fake_restart_jmp;
extern volatile int fake_restart_armed;
int fake_restarts(void);
#define FAKE_EXPECT_RESTART(stmt)                              \
  do {                                                         \
    fake_restart_armed = 1;                                    \
    if (setjmp(fake_restart_jmp) == 0) {                       \
      stmt;                                                    \
      fake_restart_armed = 0;                                  \
      TEST_FAIL_MESSAGE("expected a restart");                 \
    }                                                          \
    fake_restart_armed = 0;                                    \
  } while (0)

#endif /* FAKE_HAL_H */
