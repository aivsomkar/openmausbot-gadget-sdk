/* SPDX-License-Identifier: Apache-2.0 */
#include "drv_st7789.h"

#include "driver/ledc.h"
#include "driver/spi_common.h"
#include "esp_check.h"
#include "esp_lcd_io_spi.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_st7789.h"

#define BL_MODE LEDC_LOW_SPEED_MODE
#define BL_TIMER LEDC_TIMER_0
#define BL_CHANNEL LEDC_CHANNEL_0
#define BL_BITS LEDC_TIMER_10_BIT
#define BL_MAX_DUTY 1023u

static const char *TAG = "st7789";
static bool s_bl_ready;

static esp_err_t backlight_init(int gpio) {
  const ledc_timer_config_t timer = {
    .speed_mode = BL_MODE,
    .duty_resolution = BL_BITS,
    .timer_num = BL_TIMER,
    .freq_hz = 5000,
    .clk_cfg = LEDC_AUTO_CLK,
  };
  ESP_RETURN_ON_ERROR(ledc_timer_config(&timer), TAG, "backlight timer");
  const ledc_channel_config_t ch = {
    .gpio_num = gpio,
    .speed_mode = BL_MODE,
    .channel = BL_CHANNEL,
    .timer_sel = BL_TIMER,
    .duty = 0,
    .hpoint = 0,
  };
  ESP_RETURN_ON_ERROR(ledc_channel_config(&ch), TAG, "backlight channel");
  s_bl_ready = true;
  return ESP_OK;
}

void drv_st7789_set_brightness(uint8_t pct) {
  if (!s_bl_ready) {
    return;
  }
  uint32_t duty = (uint32_t)(pct > 100 ? 100 : pct) * BL_MAX_DUTY / 100u;
  ledc_set_duty(BL_MODE, BL_CHANNEL, duty);
  ledc_update_duty(BL_MODE, BL_CHANNEL);
}

esp_err_t drv_st7789_init(const drv_st7789_cfg_t *cfg, esp_lcd_panel_io_handle_t *io, esp_lcd_panel_handle_t *panel) {
  ESP_RETURN_ON_ERROR(backlight_init(cfg->backlight), TAG, "backlight");
  const spi_bus_config_t bus = {
    .sclk_io_num = cfg->sclk,
    .mosi_io_num = cfg->mosi,
    .miso_io_num = -1,
    .quadwp_io_num = -1,
    .quadhd_io_num = -1,
    .max_transfer_sz = (int)cfg->max_transfer_bytes,
  };
  ESP_RETURN_ON_ERROR(spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO), TAG, "spi bus");
  const esp_lcd_panel_io_spi_config_t io_cfg = {
    .cs_gpio_num = cfg->cs,
    .dc_gpio_num = cfg->dc,
    .spi_mode = cfg->spi_mode,
    .pclk_hz = cfg->pclk_hz,
    .trans_queue_depth = 10,
    .lcd_cmd_bits = 8,
    .lcd_param_bits = 8,
  };
  ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI2_HOST, &io_cfg, io), TAG, "panel io");
  const esp_lcd_panel_dev_config_t dev = {
    .reset_gpio_num = cfg->rst,
    .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
    .bits_per_pixel = 16,
  };
  ESP_RETURN_ON_ERROR(esp_lcd_new_panel_st7789(*io, &dev, panel), TAG, "panel");
  ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(*panel), TAG, "reset");
  ESP_RETURN_ON_ERROR(esp_lcd_panel_init(*panel), TAG, "init");
  ESP_RETURN_ON_ERROR(esp_lcd_panel_invert_color(*panel, cfg->invert), TAG, "invert");
  ESP_RETURN_ON_ERROR(esp_lcd_panel_swap_xy(*panel, cfg->swap_xy), TAG, "swap");
  ESP_RETURN_ON_ERROR(esp_lcd_panel_mirror(*panel, cfg->mirror_x, cfg->mirror_y), TAG, "mirror");
  ESP_RETURN_ON_ERROR(esp_lcd_panel_set_gap(*panel, cfg->x_gap, cfg->y_gap), TAG, "gap");
  ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(*panel, true), TAG, "on");
  return ESP_OK;
}
