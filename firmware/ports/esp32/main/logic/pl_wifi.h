/* SPDX-License-Identifier: Apache-2.0 */
/* Wi-Fi connection policy for the ESP32 port, as a pure state machine.
 * hal_wifi.c feeds it requests from core and events from the Wi-Fi driver
 * (on the main thread) and carries out the returned actions in order:
 * disconnect, then connect or scan, then post the state. */
#ifndef PL_WIFI_H
#define PL_WIFI_H

#include "gadget_events.h"

#define PL_WIFI_FAILS_BEFORE_FAILED 3u
#define PL_WIFI_ATTEMPT_TIMEOUT_MS 20000u
#define PL_WIFI_SCAN_WAIT_MS 3000u

typedef struct {
  bool configured;          /* a network was set with hal_wifi_connect() */
  bool connected;           /* the station has an IPv4 address */
  bool attempting;          /* esp_wifi_connect() issued, result pending */
  bool scanning;            /* esp_wifi_scan_start() issued */
  bool scan_pending;        /* a scan waits for our own disconnect */
  uint8_t failures;         /* consecutive failed attempts */
  gadget_wifi_state_t reported;
  uint64_t retry_at_ms;     /* 0 = no retry scheduled */
  uint64_t attempt_deadline_ms;
  uint64_t scan_deadline_ms;
} pl_wifi_t;

typedef struct {
  bool disconnect;          /* call esp_wifi_disconnect() first */
  bool connect;             /* call esp_wifi_connect() */
  bool scan;                /* call esp_wifi_scan_start() */
  bool post;                /* post GADGET_EV_WIFI_STATE with `state` */
  gadget_wifi_state_t state;
} pl_wifi_act_t;

void pl_wifi_init(pl_wifi_t *w);
/* hal_wifi_connect(): the caller has stored the new SSID/password. */
pl_wifi_act_t pl_wifi_connect(pl_wifi_t *w, uint64_t now_ms);
/* hal_wifi_disconnect(): forget the network until the next connect. */
pl_wifi_act_t pl_wifi_disconnect(pl_wifi_t *w);
/* IP_EVENT_STA_GOT_IP */
pl_wifi_act_t pl_wifi_got_ip(pl_wifi_t *w);
/* WIFI_EVENT_STA_DISCONNECTED. local: reason WIFI_REASON_ASSOC_LEAVE (our
 * own esp_wifi_disconnect()); auth_failed: a wrong password or handshake failure. */
pl_wifi_act_t pl_wifi_sta_disconnected(pl_wifi_t *w, bool local, bool auth_failed, uint64_t now_ms);
/* hal_wifi_scan() */
pl_wifi_act_t pl_wifi_scan(pl_wifi_t *w, uint64_t now_ms);
/* WIFI_EVENT_SCAN_DONE, or esp_wifi_scan_start() failed */
pl_wifi_act_t pl_wifi_scan_done(pl_wifi_t *w, uint64_t now_ms);
/* Every main-loop iteration. */
pl_wifi_act_t pl_wifi_tick(pl_wifi_t *w, uint64_t now_ms);
/* Delay before the next attempt after `failures` consecutive failures. */
uint32_t pl_wifi_retry_delay_ms(uint8_t failures, bool auth_failed);

#endif /* PL_WIFI_H */
