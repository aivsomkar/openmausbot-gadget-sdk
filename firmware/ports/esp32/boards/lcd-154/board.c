/* SPDX-License-Identifier: Apache-2.0 */
/* Board glue for lcd-154 (main/board_api.h). */
#include "board.h"

#include "board_api.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "drv_es_codec.h"
#include "drv_lipo_adc.h"
#include "drv_st7789.h"
#include "esp_check.h"
#include "esp_log.h"

#define SCREEN 240

static const char *TAG = "board";
static i2c_master_bus_handle_t s_i2c;
static bool s_battery;

esp_err_t board_early_init(void) {
  /* First: hold the power latch, or the board turns off when PWR is released. */
  const gpio_config_t latch = {.pin_bit_mask = 1ULL << BOARD_BAT_EN, .mode = GPIO_MODE_OUTPUT};
  ESP_RETURN_ON_ERROR(gpio_config(&latch), TAG, "latch");
  ESP_RETURN_ON_ERROR(gpio_set_level(BOARD_BAT_EN, 1), TAG, "latch on");
  const i2c_master_bus_config_t bus = {
    .i2c_port = BOARD_I2C_PORT,
    .sda_io_num = BOARD_I2C_SDA,
    .scl_io_num = BOARD_I2C_SCL,
    .clk_source = I2C_CLK_SRC_DEFAULT,
    .glitch_ignore_cnt = 7,
    .flags.enable_internal_pullup = true,
  };
  ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bus, &s_i2c), TAG, "i2c bus");
  const drv_lipo_adc_cfg_t bat = {
    .adc_gpio = BOARD_BAT_ADC, .chg_gpio = BOARD_CHG_STAT, .divider_x1000 = BOARD_BAT_DIVIDER_X1000};
  s_battery = drv_lipo_adc_init(&bat) == ESP_OK;
  if (!s_battery) {
    ESP_LOGW(TAG, "battery ADC unavailable");
  }
  return ESP_OK;
}

esp_err_t board_display_init(esp_lcd_panel_io_handle_t *io, esp_lcd_panel_handle_t *panel) {
  const drv_st7789_cfg_t cfg = {
    .sclk = BOARD_LCD_SCLK, .mosi = BOARD_LCD_MOSI, .cs = BOARD_LCD_CS, .dc = BOARD_LCD_DC, .rst = BOARD_LCD_RST,
    .backlight = BOARD_LCD_BL, .spi_mode = BOARD_LCD_SPI_MODE, .pclk_hz = BOARD_LCD_PCLK_HZ,
    .x_gap = BOARD_LCD_X_GAP, .y_gap = BOARD_LCD_Y_GAP,
    .swap_xy = false, .mirror_x = false, .mirror_y = false, .invert = true,
    .max_transfer_bytes = SCREEN * BOARD_LCD_BUF_LINES * 2,
  };
  return drv_st7789_init(&cfg, io, panel);
}

esp_err_t board_touch_init(esp_lcd_touch_handle_t *tp) {
  (void)tp;
  return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t board_audio_init(board_audio_t *out) {
  const drv_es_codec_cfg_t cfg = {
    .bus = s_i2c, .i2c_port = BOARD_I2C_PORT, .i2s_port = BOARD_I2S_PORT,
    .mclk = BOARD_I2S_MCLK, .bclk = BOARD_I2S_BCLK, .ws = BOARD_I2S_WS, .dout = BOARD_I2S_DOUT,
    .din = BOARD_I2S_DIN, .pa = BOARD_PA_EN,
    .mic_gain_db = BOARD_MIC_GAIN_DB, .volume = 80,
  };
  return drv_es_codec_init(&cfg, out);
}

esp_err_t board_buttons_init(void) {
  const gpio_config_t keys = {
    .pin_bit_mask = (1ULL << BOARD_BTN_TALK) | (1ULL << BOARD_BTN_CANCEL),
    .mode = GPIO_MODE_INPUT,
    .pull_up_en = GPIO_PULLUP_ENABLE,
  };
  return gpio_config(&keys);
}

bool board_talk_pressed(void) { return gpio_get_level(BOARD_BTN_TALK) == 0; }
bool board_cancel_pressed(void) { return gpio_get_level(BOARD_BTN_CANCEL) == 0; }
bool board_battery_read(gadget_battery_t *out) { return s_battery && drv_lipo_adc_read(out); }
void board_set_brightness(uint8_t pct) { drv_st7789_set_brightness(pct); }
