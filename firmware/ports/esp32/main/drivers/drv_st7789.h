/* SPDX-License-Identifier: Apache-2.0 */
/* ST7789 LCD on SPI with an LEDC-dimmed backlight (lcd-154, devkit), on
 * ESP-IDF's built-in esp_lcd ST7789 driver. */
#ifndef DRV_ST7789_H
#define DRV_ST7789_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "esp_lcd_types.h"

typedef struct {
  int sclk, mosi, cs, dc, rst, backlight;
  int spi_mode;
  uint32_t pclk_hz;
  int x_gap, y_gap;
  bool swap_xy, mirror_x, mirror_y, invert;
  size_t max_transfer_bytes;
} drv_st7789_cfg_t;

esp_err_t drv_st7789_init(const drv_st7789_cfg_t *cfg, esp_lcd_panel_io_handle_t *io, esp_lcd_panel_handle_t *panel);
void drv_st7789_set_brightness(uint8_t pct);

#endif /* DRV_ST7789_H */
