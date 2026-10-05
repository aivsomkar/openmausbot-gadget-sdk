/* SPDX-License-Identifier: Apache-2.0 */
/* AXP2101 PMU fuel gauge (amoled-175c, amoled-175), registers from the
 * X-Powers AXP2101 datasheet. Only reads battery state; power rails keep
 * their power-on defaults. */
#ifndef DRV_AXP2101_H
#define DRV_AXP2101_H

#include <stdbool.h>

#include "driver/i2c_master.h"
#include "esp_err.h"
#include "gadget_types.h"

/* Adds the device at 0x34 and makes sure the fuel gauge is on. */
esp_err_t drv_axp2101_init(i2c_master_bus_handle_t bus);
/* false when no battery is fitted or the read failed. */
bool drv_axp2101_read(gadget_battery_t *out);

#endif /* DRV_AXP2101_H */
