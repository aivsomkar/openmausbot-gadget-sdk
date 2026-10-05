/* firmware/ports/sim/sim_net_script.c */
/* SPDX-License-Identifier: Apache-2.0 */
/* --host script: no socket. hal_ws_open() only marks a connection pending;
 * the script's net_open / net_text / net_binary / net_close commands play
 * the host. What the gadget sends is seen through core's tap (expect). */
#include <string.h>
#include "gadget_hal.h"
#include "sim_hal.h"
#include "sim_internal.h"

static bool s_pending, s_open;

static void post_closed(uint16_t code) {
  gadget_event_t ev = {.type = GADGET_EV_WS_CLOSED};
  ev.u.closed.code = code;
  sim_post_event(&ev);
  s_pending = s_open = false;
}

gadget_status_t sim_net_script_ws_open(void) {
  if (s_pending) return GADGET_ERR_BUSY;
  s_pending = true;
  s_open = false;
  return GADGET_OK;
}

gadget_status_t sim_net_script_ws_send(void) { return s_open ? GADGET_OK : GADGET_ERR_BUSY; }

void sim_net_script_ws_close(uint16_t code) {
  if (s_pending) post_closed(code);
}

bool sim_net_script_open(void) {
  if (!s_pending || s_open) return false;
  s_open = true;
  gadget_event_t ev = {.type = GADGET_EV_WS_OPEN};
  sim_post_event(&ev);
  return true;
}

static bool post_data(gadget_event_type_t type, const uint8_t *data, size_t len) {
  if (!s_open) return false;
  gadget_event_t ev = {.type = type};
  ev.u.ws.data = data;
  ev.u.ws.len = len;
  sim_post_event(&ev);
  return true;
}

bool sim_net_script_text(const char *json) { return post_data(GADGET_EV_WS_TEXT, (const uint8_t *)json, strlen(json)); }
bool sim_net_script_binary(const uint8_t *data, size_t len) { return post_data(GADGET_EV_WS_BINARY, data, len); }

bool sim_net_script_close(uint16_t code) {
  if (!s_pending) return false;
  post_closed(code);
  return true;
}
