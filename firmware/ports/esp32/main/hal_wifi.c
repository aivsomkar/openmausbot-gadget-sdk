/* SPDX-License-Identifier: Apache-2.0 */
/* HAL Wi-Fi group. The policy (retries, FAILED after 3 failures, pausing a
 * connection attempt for a scan) is the pure pl_wifi state machine; this
 * file feeds it on the gadget task and carries out its actions. Wi-Fi
 * driver events arrive on the event-loop task and are queued as
 * port_wifi_msg_t, so pl_wifi is only ever touched by one task. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "gadget_hal.h"
#include "pl_scan.h"
#include "pl_wifi.h"
#include "port.h"

#define SCAN_RECORDS_MAX 40

static const char *TAG = "wifi";
static pl_wifi_t s_w;
static wifi_config_t s_cfg;
static char s_ip[16];

static gadget_wifi_auth_t map_auth(wifi_auth_mode_t m) {
  switch (m) {
    case WIFI_AUTH_OPEN: return GADGET_AUTH_OPEN;
    case WIFI_AUTH_WEP: return GADGET_AUTH_WEP;
    case WIFI_AUTH_WPA_PSK: return GADGET_AUTH_WPA;
    case WIFI_AUTH_WPA2_PSK:
    case WIFI_AUTH_WPA_WPA2_PSK: return GADGET_AUTH_WPA2;
    case WIFI_AUTH_WPA3_PSK:
    case WIFI_AUTH_WPA2_WPA3_PSK: return GADGET_AUTH_WPA3;
    case WIFI_AUTH_WPA2_ENTERPRISE: return GADGET_AUTH_WPA2_ENT;
    default: return GADGET_AUTH_OTHER;
  }
}

static void post_state(gadget_wifi_state_t state) {
  gadget_event_t ev = {.type = GADGET_EV_WIFI_STATE};
  ev.u.wifi.state = state;
  if (state == GADGET_WIFI_CONNECTED) {
    snprintf(ev.u.wifi.ip, sizeof(ev.u.wifi.ip), "%s", s_ip);
  }
  port_post_event(&ev);
}

static void post_scan(const gadget_wifi_ap_t *aps, uint8_t count, bool ok) {
  gadget_event_t ev = {.type = GADGET_EV_WIFI_SCAN};
  ev.u.scan.aps = aps;
  ev.u.scan.count = count;
  ev.u.scan.ok = ok;
  port_post_event(&ev);
}

static void apply(const pl_wifi_act_t *a);

static void scan_failed(void) {
  post_scan(NULL, 0, false);
  pl_wifi_act_t b = pl_wifi_scan_done(&s_w, hal_now_ms());
  apply(&b);
}

static void apply(const pl_wifi_act_t *a) {
  if (a->disconnect) {
    esp_wifi_disconnect(); /* not connected: returns an error we can ignore */
  }
  if (a->scan) {
    const wifi_scan_config_t sc = {.show_hidden = false};
    esp_err_t e = esp_wifi_scan_start(&sc, false);
    if (e != ESP_OK) {
      ESP_LOGW(TAG, "scan start: %s", esp_err_to_name(e));
      scan_failed();
    }
  }
  if (a->connect) {
    esp_err_t e = esp_wifi_set_config(WIFI_IF_STA, &s_cfg);
    if (e == ESP_OK) {
      e = esp_wifi_connect();
    }
    if (e != ESP_OK) {
      ESP_LOGW(TAG, "connect: %s", esp_err_to_name(e));
      pl_wifi_act_t b = pl_wifi_sta_disconnected(&s_w, false, false, hal_now_ms());
      apply(&b);
    }
  }
  if (a->post) {
    post_state(a->state);
  }
}

static void deliver_scan(bool driver_ok) {
  uint16_t n = 0;
  wifi_ap_record_t *recs = NULL;
  gadget_wifi_ap_t *raw = NULL;
  gadget_wifi_ap_t out[PL_SCAN_MAX];
  uint8_t count = 0;
  bool ok = driver_ok && esp_wifi_scan_get_ap_num(&n) == ESP_OK;
  if (ok && n > 0) {
    if (n > SCAN_RECORDS_MAX) {
      n = SCAN_RECORDS_MAX;
    }
    recs = calloc(n, sizeof(*recs));
    raw = calloc(n, sizeof(*raw));
    ok = recs != NULL && raw != NULL && esp_wifi_scan_get_ap_records(&n, recs) == ESP_OK;
    if (ok) {
      for (uint16_t i = 0; i < n; i++) {
        memcpy(raw[i].ssid, recs[i].ssid, sizeof(raw[i].ssid) - 1);
        raw[i].ssid[sizeof(raw[i].ssid) - 1] = '\0';
        raw[i].rssi = recs[i].rssi;
        raw[i].auth = map_auth(recs[i].authmode);
      }
      count = pl_scan_merge(raw, n, out, PL_SCAN_MAX);
    }
  }
  esp_wifi_clear_ap_list(); /* frees whatever get_ap_records did not take */
  free(recs);
  free(raw);
  post_scan(out, count, ok);
}

void port_wifi_on_msg(const port_wifi_msg_t *m) {
  uint64_t now = hal_now_ms();
  pl_wifi_act_t a = {0};
  switch (m->kind) {
    case PORT_WIFI_GOT_IP:
      snprintf(s_ip, sizeof(s_ip), "%s", m->ip);
      a = pl_wifi_got_ip(&s_w);
      break;
    case PORT_WIFI_DISCONNECTED:
      a = pl_wifi_sta_disconnected(&s_w, m->local, m->auth_failed, now);
      break;
    case PORT_WIFI_SCAN_DONE:
      deliver_scan(!m->scan_failed);
      a = pl_wifi_scan_done(&s_w, now);
      break;
  }
  apply(&a);
}

void port_wifi_tick(uint64_t now_ms) {
  pl_wifi_act_t a = pl_wifi_tick(&s_w, now_ms);
  apply(&a);
}

/* Event-loop task: copy what matters and queue it for the gadget task. */
static void on_event(void *arg, esp_event_base_t base, int32_t id, void *data) {
  (void)arg;
  port_wifi_msg_t m = {0};
  if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
    const wifi_event_sta_disconnected_t *d = data;
    m.kind = PORT_WIFI_DISCONNECTED;
    m.local = d->reason == WIFI_REASON_ASSOC_LEAVE;
    m.auth_failed = d->reason == WIFI_REASON_AUTH_FAIL || d->reason == WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT ||
                    d->reason == WIFI_REASON_HANDSHAKE_TIMEOUT;
  } else if (base == WIFI_EVENT && id == WIFI_EVENT_SCAN_DONE) {
    m.kind = PORT_WIFI_SCAN_DONE;
    m.scan_failed = data != NULL && ((const wifi_event_sta_scan_done_t *)data)->status != 0;
  } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
    const ip_event_got_ip_t *g = data;
    m.kind = PORT_WIFI_GOT_IP;
    snprintf(m.ip, sizeof(m.ip), IPSTR, IP2STR(&g->ip_info.ip));
  } else {
    return;
  }
  port_post_wifi(&m);
}

esp_err_t port_wifi_start(void) {
  pl_wifi_init(&s_w);
  ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "netif init");
  ESP_RETURN_ON_ERROR(esp_event_loop_create_default(), TAG, "event loop");
  if (esp_netif_create_default_wifi_sta() == NULL) {
    return ESP_FAIL;
  }
  wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
  ESP_RETURN_ON_ERROR(esp_wifi_init(&init), TAG, "wifi init");
  ESP_RETURN_ON_ERROR(esp_wifi_set_storage(WIFI_STORAGE_RAM), TAG, "storage"); /* credentials live in our NVS keys */
  ESP_RETURN_ON_ERROR(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_event, NULL, NULL), TAG, "wifi events");
  ESP_RETURN_ON_ERROR(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_event, NULL, NULL), TAG, "ip events");
  ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "sta mode");
  ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "wifi start"); /* the RNG is now truly random (spec §4.2) */
  esp_wifi_set_ps(WIFI_PS_NONE);                              /* low latency for audio */
  return ESP_OK;
}

gadget_status_t hal_wifi_connect(const char *ssid, const char *password) {
  if (ssid == NULL || password == NULL) {
    return GADGET_ERR_ARG;
  }
  size_t sl = strlen(ssid), pl = strlen(password);
  if (sl == 0 || sl > sizeof(s_cfg.sta.ssid) || pl > sizeof(s_cfg.sta.password)) {
    return GADGET_ERR_ARG;
  }
  memset(&s_cfg, 0, sizeof(s_cfg));
  memcpy(s_cfg.sta.ssid, ssid, sl);
  memcpy(s_cfg.sta.password, password, pl);
  s_cfg.sta.threshold.authmode = pl == 0 ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA_PSK;
  s_cfg.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;
  pl_wifi_act_t a = pl_wifi_connect(&s_w, hal_now_ms());
  apply(&a);
  return GADGET_OK;
}

void hal_wifi_disconnect(void) {
  pl_wifi_act_t a = pl_wifi_disconnect(&s_w);
  memset(&s_cfg, 0, sizeof(s_cfg));
  apply(&a);
}

gadget_wifi_state_t hal_wifi_state(void) { return s_w.reported; }

gadget_status_t hal_wifi_scan(void) {
  pl_wifi_act_t a = pl_wifi_scan(&s_w, hal_now_ms());
  apply(&a);
  return GADGET_OK;
}
