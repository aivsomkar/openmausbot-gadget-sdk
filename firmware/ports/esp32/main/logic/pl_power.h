/* SPDX-License-Identifier: Apache-2.0 */
/* Battery decoding: the AXP2101 PMU (amoled-175c, amoled-175) and a
 * voltage-based estimate for a bare 1-cell Li-ion (lcd-154). */
#ifndef PL_POWER_H
#define PL_POWER_H

#include "gadget_types.h"

#define PL_AXP2101_ADDR 0x34u
#define PL_AXP2101_REG_STATUS1 0x00u   /* bit3: battery present */
#define PL_AXP2101_REG_STATUS2 0x01u   /* bits6:5: 01 charging, 10 discharging, 00 standby */
#define PL_AXP2101_REG_GAUGE_CTRL 0x18u /* bit3: fuel gauge enable */
#define PL_AXP2101_REG_BATT_PCT 0xA4u  /* 0..100 */

/* false when no battery is fitted. */
bool pl_axp2101_decode(uint8_t status1, uint8_t status2, uint8_t pct, gadget_battery_t *out);
/* Resting cell voltage in millivolts → 0..100, piecewise linear. */
uint8_t pl_lipo_pct(uint32_t mv);
/* Below this the BAT net reads as "no cell fitted" (a real Li-ion cell
 * protects itself near 2.5–3.0 V, an empty holder reads near 0 V). */
#define PL_LIPO_NO_CELL_MV 3000u
bool pl_lipo_present(uint32_t mv);

#endif /* PL_POWER_H */
