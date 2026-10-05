/* firmware/ports/sim/sim_wifi.c */
/* SPDX-License-Identifier: Apache-2.0 */
/* The simulator is always on the network (gadget_hal.h). */
#include <string.h>
#include "gadget_hal.h"
#include "sim_hal.h"
#include "sim_internal.h"

static void post_connected(void) {
  gadget_event_t ev = {.type = GADGET_EV_WIFI_STATE};
  ev.u.wifi.state = GADGET_WIFI_CONNECTED;
  strcpy(ev.u.wifi.ip, "127.0.0.1");
  sim_post_event(&ev);
}

void sim_wifi_start(void) { post_connected(); }

gadget_status_t hal_wifi_connect(const char *ssid, const char *password) {
  (void)ssid;
  (void)password;
  post_connected();
  return GADGET_OK;
}

void hal_wifi_disconnect(void) {}

gadget_wifi_state_t hal_wifi_state(void) { return GADGET_WIFI_CONNECTED; }

gadget_status_t hal_wifi_scan(void) {
  static const gadget_wifi_ap_t aps[] = {{"SimNet", -42, GADGET_AUTH_WPA2}, {"Cafe", -78, GADGET_AUTH_OPEN}};
  gadget_event_t ev = {.type = GADGET_EV_WIFI_SCAN};
  ev.u.scan.aps = aps;
  ev.u.scan.count = 2;
  ev.u.scan.ok = true;
  sim_post_event(&ev);
  return GADGET_OK;
}
