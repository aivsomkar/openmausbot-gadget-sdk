/* firmware/core/include/gadget_events.h */
/* SPDX-License-Identifier: Apache-2.0 */
/* Events a port delivers to core with core_event(), always on the main
 * thread (the thread that calls core_tick). Pointers inside an event are
 * valid only for the duration of the core_event() call; ports that queue
 * events across threads copy the payload first and free it afterwards. */
#ifndef GADGET_EVENTS_H
#define GADGET_EVENTS_H

#include "gadget_types.h"

typedef enum {
  GADGET_EV_INPUT = 1,      /* u.input */
  GADGET_EV_MIC_FRAME,      /* u.mic: GADGET_MIC_FRAME_SAMPLES samples at 16 kHz */
  GADGET_EV_WIFI_STATE,     /* u.wifi */
  GADGET_EV_WIFI_SCAN,      /* u.scan: result of hal_wifi_scan() */
  GADGET_EV_WS_OPEN,        /* the WebSocket upgrade completed */
  GADGET_EV_WS_TEXT,        /* u.ws: one complete text message (UTF-8, not NUL-terminated) */
  GADGET_EV_WS_BINARY,      /* u.ws: one complete binary message */
  GADGET_EV_WS_CONTROL,     /* a ping or pong arrived (liveness only; the port answers pings) */
  GADGET_EV_WS_CLOSED,      /* u.closed: once per hal_ws_open(), after failure or close */
  GADGET_EV_MDNS,           /* u.mdns: result of hal_mdns_browse() */
  GADGET_EV_CONSOLE_LINE,   /* u.console: one console line, line ending removed */
  GADGET_EV_OTA_WRITTEN,    /* u.ota_written: progress of hal_ota_write() */
  GADGET_EV_OTA_ERROR       /* u.ota_error: a queued OTA write failed */
} gadget_event_type_t;

typedef enum {
  GADGET_IN_TALK_DOWN = 1,
  GADGET_IN_TALK_UP,
  GADGET_IN_CANCEL_DOWN,
  GADGET_IN_CANCEL_UP,
  GADGET_IN_TOUCH_DOWN,     /* x, y in screen pixels */
  GADGET_IN_TOUCH_MOVE,
  GADGET_IN_TOUCH_UP,
  GADGET_IN_SWIPE           /* dir; only the simulator script sends this; core also derives swipes from touch */
} gadget_input_type_t;

typedef enum { GADGET_SWIPE_UP = 0, GADGET_SWIPE_DOWN, GADGET_SWIPE_LEFT, GADGET_SWIPE_RIGHT } gadget_swipe_dir_t;

typedef struct {
  gadget_input_type_t type;
  int16_t x, y;
  gadget_swipe_dir_t dir;
} gadget_input_t;

typedef struct {
  const int16_t *pcm;       /* mono PCM16, host byte order */
  uint16_t samples;         /* GADGET_MIC_FRAME_SAMPLES */
} gadget_mic_frame_t;

typedef enum {
  GADGET_WIFI_OFF = 0,      /* no network configured */
  GADGET_WIFI_CONNECTING,
  GADGET_WIFI_CONNECTED,
  GADGET_WIFI_FAILED
} gadget_wifi_state_t;

typedef struct {
  gadget_wifi_state_t state;
  char ip[16];              /* dotted IPv4 when CONNECTED, else "" */
} gadget_wifi_ev_t;

typedef enum {
  GADGET_AUTH_OPEN = 0, GADGET_AUTH_WEP, GADGET_AUTH_WPA, GADGET_AUTH_WPA2,
  GADGET_AUTH_WPA3, GADGET_AUTH_WPA2_ENT, GADGET_AUTH_OTHER
} gadget_wifi_auth_t;       /* @omb scan "auth": open wep wpa wpa2 wpa3 wpa2-ent other */

typedef struct {
  char ssid[33];
  int8_t rssi;              /* dBm */
  gadget_wifi_auth_t auth;
} gadget_wifi_ap_t;

typedef struct {
  const gadget_wifi_ap_t *aps;
  uint8_t count;            /* ≤ 20, strongest first, de-duplicated by ssid */
  bool ok;                  /* false: the scan failed */
} gadget_wifi_scan_ev_t;

typedef struct {
  const uint8_t *data;
  size_t len;
} gadget_ws_data_t;

typedef struct {
  uint16_t code;            /* close code from the peer, or 0 when the connect/upgrade failed or the socket dropped */
} gadget_ws_closed_t;

typedef struct {
  char name[64];            /* service instance name, e.g. "Omkar's computer" */
  char address[48];         /* "a.b.c.d:port" */
  char id[GADGET_HOST_ID_LEN + 1]; /* TXT id=, "" when absent */
} gadget_mdns_host_t;

typedef struct {
  const gadget_mdns_host_t *hosts;
  uint8_t count;            /* ≤ 8 */
  bool ok;                  /* false: browsing failed or is unsupported */
} gadget_mdns_ev_t;

typedef struct {
  const char *line;         /* NUL-terminated, ≤ GADGET_CONSOLE_LINE_MAX - 1 bytes, no CR/LF */
} gadget_console_ev_t;

typedef struct {
  uint32_t written;         /* contiguous bytes durably written since hal_ota_begin() */
} gadget_ota_written_t;

typedef struct {
  gadget_status_t err;
} gadget_ota_error_t;

typedef struct gadget_event {
  gadget_event_type_t type;
  union {
    gadget_input_t input;
    gadget_mic_frame_t mic;
    gadget_wifi_ev_t wifi;
    gadget_wifi_scan_ev_t scan;
    gadget_ws_data_t ws;
    gadget_ws_closed_t closed;
    gadget_mdns_ev_t mdns;
    gadget_console_ev_t console;
    gadget_ota_written_t ota_written;
    gadget_ota_error_t ota_error;
  } u;
} gadget_event_t;

#endif /* GADGET_EVENTS_H */
