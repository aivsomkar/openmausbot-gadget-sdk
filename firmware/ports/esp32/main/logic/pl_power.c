/* SPDX-License-Identifier: Apache-2.0 */
#include "pl_power.h"

bool pl_axp2101_decode(uint8_t status1, uint8_t status2, uint8_t pct, gadget_battery_t *out) {
  if ((status1 & 0x08u) == 0) {
    return false;
  }
  out->pct = pct > 100 ? 100 : pct;
  out->charging = ((status2 >> 5) & 0x03u) == 0x01u;
  return true;
}

/* A typical 1-cell Li-ion discharge curve at light load. */
static const struct {
  uint16_t mv;
  uint8_t pct;
} k_curve[] = {
  {4180, 100}, {4100, 90}, {4020, 80}, {3950, 70}, {3880, 60}, {3830, 50},
  {3790, 40}, {3760, 30}, {3730, 20}, {3690, 10}, {3600, 5}, {3300, 0},
};

uint8_t pl_lipo_pct(uint32_t mv) {
  const size_t n = sizeof(k_curve) / sizeof(k_curve[0]);
  if (mv >= k_curve[0].mv) {
    return 100;
  }
  if (mv <= k_curve[n - 1].mv) {
    return 0;
  }
  for (size_t i = 1; i < n; i++) {
    if (mv >= k_curve[i].mv) {
      uint32_t hi_mv = k_curve[i - 1].mv, lo_mv = k_curve[i].mv;
      uint32_t hi_p = k_curve[i - 1].pct, lo_p = k_curve[i].pct;
      return (uint8_t)(lo_p + (mv - lo_mv) * (hi_p - lo_p) / (hi_mv - lo_mv));
    }
  }
  return 0;
}

bool pl_lipo_present(uint32_t mv) { return mv >= PL_LIPO_NO_CELL_MV; }
