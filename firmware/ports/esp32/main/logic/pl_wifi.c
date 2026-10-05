/* SPDX-License-Identifier: Apache-2.0 */
#include "pl_wifi.h"

#include <string.h>

static void report(pl_wifi_t *w, pl_wifi_act_t *a, gadget_wifi_state_t s) {
  if (w->reported != s) {
    w->reported = s;
    a->post = true;
    a->state = s;
  }
}

static void start_attempt(pl_wifi_t *w, pl_wifi_act_t *a, uint64_t now) {
  w->retry_at_ms = 0;
  if (w->scanning || w->scan_pending) {
    return; /* pl_wifi_scan_done() starts it */
  }
  a->connect = true;
  w->attempting = true;
  w->attempt_deadline_ms = now + PL_WIFI_ATTEMPT_TIMEOUT_MS;
}

static void fail_attempt(pl_wifi_t *w, pl_wifi_act_t *a, bool auth_failed, uint64_t now) {
  w->attempting = false;
  if (w->failures < 255) {
    w->failures++;
  }
  if (auth_failed || w->failures >= PL_WIFI_FAILS_BEFORE_FAILED) {
    report(w, a, GADGET_WIFI_FAILED);
  }
  w->retry_at_ms = now + pl_wifi_retry_delay_ms(w->failures, auth_failed);
}

uint32_t pl_wifi_retry_delay_ms(uint8_t failures, bool auth_failed) {
  if (auth_failed) {
    return 30000u;
  }
  switch (failures) {
    case 0:
    case 1: return 1000u;
    case 2: return 2000u;
    case 3: return 5000u;
    default: return 10000u;
  }
}

void pl_wifi_init(pl_wifi_t *w) {
  memset(w, 0, sizeof(*w));
  w->reported = GADGET_WIFI_OFF;
}

pl_wifi_act_t pl_wifi_connect(pl_wifi_t *w, uint64_t now) {
  pl_wifi_act_t a = {0};
  a.disconnect = w->connected || w->attempting;
  w->configured = true;
  w->connected = false;
  w->attempting = false;
  w->failures = 0;
  report(w, &a, GADGET_WIFI_CONNECTING);
  start_attempt(w, &a, now);
  return a;
}

pl_wifi_act_t pl_wifi_disconnect(pl_wifi_t *w) {
  pl_wifi_act_t a = {0};
  a.disconnect = w->connected || w->attempting;
  w->configured = false;
  w->connected = false;
  w->attempting = false;
  w->failures = 0;
  w->retry_at_ms = 0;
  report(w, &a, GADGET_WIFI_OFF);
  return a;
}

pl_wifi_act_t pl_wifi_got_ip(pl_wifi_t *w) {
  pl_wifi_act_t a = {0};
  if (!w->configured) {
    return a;
  }
  w->connected = true;
  w->attempting = false;
  w->failures = 0;
  w->retry_at_ms = 0;
  w->reported = GADGET_WIFI_OFF; /* force a post: the IP may have changed */
  report(w, &a, GADGET_WIFI_CONNECTED);
  return a;
}

pl_wifi_act_t pl_wifi_sta_disconnected(pl_wifi_t *w, bool local, bool auth_failed, uint64_t now) {
  pl_wifi_act_t a = {0};
  if (local) {
    if (w->scan_pending) {
      w->scan_pending = false;
      w->scanning = true;
      w->attempting = false;
      a.scan = true;
    }
    return a;
  }
  if (!w->configured) {
    return a;
  }
  if (w->connected) {
    w->connected = false;
    w->failures = 0;
    report(w, &a, GADGET_WIFI_CONNECTING);
    start_attempt(w, &a, now);
    return a;
  }
  if (!w->attempting) {
    return a; /* a late report for an attempt we already gave up on */
  }
  fail_attempt(w, &a, auth_failed, now);
  return a;
}

pl_wifi_act_t pl_wifi_scan(pl_wifi_t *w, uint64_t now) {
  pl_wifi_act_t a = {0};
  if (w->scanning || w->scan_pending) {
    return a;
  }
  if (w->attempting) {
    a.disconnect = true;
    w->scan_pending = true;
    w->scan_deadline_ms = now + PL_WIFI_SCAN_WAIT_MS;
    return a;
  }
  w->scanning = true;
  a.scan = true;
  return a;
}

pl_wifi_act_t pl_wifi_scan_done(pl_wifi_t *w, uint64_t now) {
  pl_wifi_act_t a = {0};
  w->scanning = false;
  if (w->configured && !w->connected && !w->attempting) {
    start_attempt(w, &a, now);
  }
  return a;
}

pl_wifi_act_t pl_wifi_tick(pl_wifi_t *w, uint64_t now) {
  pl_wifi_act_t a = {0};
  if (w->scan_pending && now >= w->scan_deadline_ms) {
    w->scan_pending = false;
    w->scanning = true;
    w->attempting = false;
    a.scan = true;
    return a;
  }
  if (w->attempting && now >= w->attempt_deadline_ms) {
    a.disconnect = true;
    fail_attempt(w, &a, false, now);
    return a;
  }
  if (w->configured && !w->connected && !w->attempting && w->retry_at_ms != 0 && now >= w->retry_at_ms) {
    start_attempt(w, &a, now);
  }
  return a;
}
