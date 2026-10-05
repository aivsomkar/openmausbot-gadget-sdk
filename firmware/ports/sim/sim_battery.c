/* firmware/ports/sim/sim_battery.c */
/* SPDX-License-Identifier: Apache-2.0 */
#include "gadget_hal.h"
#include "sim_internal.h"

void sim_battery_set(uint8_t pct, bool charging) {
  g_sim.battery_pct = pct > 100 ? 100 : pct;
  g_sim.battery_charging = charging;
}

bool hal_battery_read(gadget_battery_t *out) {
  if (g_sim.board == NULL || !g_sim.board->has_battery) return false;
  out->pct = g_sim.battery_pct;
  out->charging = g_sim.battery_charging;
  return true;
}
