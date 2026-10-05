/* SPDX-License-Identifier: Apache-2.0 */
/* CST9217 touch controller (amoled-175c, amoled-175) on the shared I2C bus,
 * on top of waveshare/esp_lcd_touch_cst9217. */
#ifndef DRV_CST9217_H
#define DRV_CST9217_H

#include <stdbool.h>
#include <stdint.h>

#include "driver/i2c_master.h"
#include "esp_err.h"
#include "esp_lcd_touch.h"

typedef struct {
  i2c_master_bus_handle_t bus;
  int rst, intr;
  uint16_t width, height;
  bool mirror_x, mirror_y;
} drv_cst9217_cfg_t;

esp_err_t drv_cst9217_init(const drv_cst9217_cfg_t *cfg, esp_lcd_touch_handle_t *tp);

#endif /* DRV_CST9217_H */
