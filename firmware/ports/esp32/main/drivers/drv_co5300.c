/* SPDX-License-Identifier: Apache-2.0 */
#include "drv_co5300.h"

#include "driver/spi_common.h"
#include "esp_check.h"
#include "esp_lcd_co5300.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"

static const char *TAG = "co5300";

esp_err_t drv_co5300_init(const drv_co5300_cfg_t *cfg, esp_lcd_panel_io_handle_t *io, esp_lcd_panel_handle_t *panel) {
  const spi_bus_config_t bus = CO5300_PANEL_BUS_QSPI_CONFIG(cfg->sclk, cfg->d0, cfg->d1, cfg->d2, cfg->d3,
                                                           (int)cfg->max_transfer_bytes);
  ESP_RETURN_ON_ERROR(spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO), TAG, "spi bus");
  const esp_lcd_panel_io_spi_config_t io_cfg = CO5300_PANEL_IO_QSPI_CONFIG(cfg->cs, NULL, NULL);
  ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI2_HOST, &io_cfg, io), TAG, "panel io");
  co5300_vendor_config_t vendor = {
    .init_cmds = cfg->init_cmds, /* NULL: the component's built-in table */
    .init_cmds_size = cfg->init_cmds != NULL ? cfg->init_cmds_size : 0,
    .flags = {.use_qspi_interface = 1},
  };
  const esp_lcd_panel_dev_config_t dev = {
    .reset_gpio_num = cfg->rst,
    .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
    .bits_per_pixel = 16,
    .vendor_config = &vendor,
  };
  ESP_RETURN_ON_ERROR(esp_lcd_new_panel_co5300(*io, &dev, panel), TAG, "panel");
  ESP_RETURN_ON_ERROR(esp_lcd_panel_set_gap(*panel, cfg->x_gap, 0), TAG, "gap");
  ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(*panel), TAG, "reset");
  ESP_RETURN_ON_ERROR(esp_lcd_panel_init(*panel), TAG, "init");
  ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(*panel, true), TAG, "on");
  return ESP_OK;
}

void drv_co5300_set_brightness(esp_lcd_panel_handle_t panel, uint8_t pct) {
  esp_lcd_panel_co5300_set_brightness(panel, pct > 100 ? 100 : pct);
}
