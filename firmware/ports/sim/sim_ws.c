/* firmware/ports/sim/sim_ws.c */
/* SPDX-License-Identifier: Apache-2.0 */
/* The WebSocket HAL. With --host script it is the scripted network
 * (sim_net_script.c); a real socket needs the client that the next task adds. */
#include <string.h>
#include "gadget_hal.h"
#include "sim_internal.h"

bool sim_net_scripted(void) { return g_sim.host != NULL && strcmp(g_sim.host, "script") == 0; }

gadget_status_t hal_ws_open(const char *host, uint16_t port) {
  (void)host;
  (void)port;
  if (sim_net_scripted()) return sim_net_script_ws_open();
  hal_log(GADGET_LOG_ERROR, "sim", "this build has no WebSocket client; use --host script");
  return GADGET_ERR_UNSUPPORTED;
}

gadget_status_t hal_ws_send_text(const char *data, size_t len) {
  (void)data;
  (void)len;
  return sim_net_scripted() ? sim_net_script_ws_send() : GADGET_ERR_BUSY;
}

gadget_status_t hal_ws_send_binary(const uint8_t *data, size_t len) {
  (void)data;
  (void)len;
  return sim_net_scripted() ? sim_net_script_ws_send() : GADGET_ERR_BUSY;
}

void hal_ws_close(uint16_t code) {
  if (sim_net_scripted()) sim_net_script_ws_close(code);
}

void sim_net_poll(uint32_t wait_ms) { (void)wait_ms; }
