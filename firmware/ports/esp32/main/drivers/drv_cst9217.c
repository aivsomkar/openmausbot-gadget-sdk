/* SPDX-License-Identifier: Apache-2.0 */
#include "drv_cst9217.h"

#include "esp_check.h"
#include "esp_lcd_io_i2c.h"
#include "esp_lcd_touch_cst9217.h"

static const char *TAG = "cst9217";

esp_err_t drv_cst9217_init(const drv_cst9217_cfg_t *cfg, esp_lcd_touch_handle_t *tp) {
  esp_lcd_panel_io_handle_t tio = NULL;
  esp_lcd_panel_io_i2c_config_t io_cfg = ESP_LCD_TOUCH_IO_I2C_CST9217_CONFIG();
  io_cfg.scl_speed_hz = 400000;
  ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_i2c(cfg->bus, &io_cfg, &tio), TAG, "touch io");
  const esp_lcd_touch_config_t tp_cfg = {
    .x_max = cfg->width,
    .y_max = cfg->height,
    .rst_gpio_num = cfg->rst,
    .int_gpio_num = cfg->intr,
    .levels = {.reset = 0, .interrupt = 0},
    .flags = {.swap_xy = 0, .mirror_x = cfg->mirror_x, .mirror_y = cfg->mirror_y},
  };
  return esp_lcd_touch_new_i2c_cst9217(tio, &tp_cfg, tp);
}
