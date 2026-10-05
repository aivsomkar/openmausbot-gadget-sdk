/* SPDX-License-Identifier: Apache-2.0 */
/* Board glue for devkit (main/board_api.h). */
#include "board.h"

#include "board_api.h"
#include "driver/gpio.h"
#include "drv_i2s_simplex.h"
#include "drv_st7789.h"
#include "esp_check.h"

#define SCREEN_W 320

static const char *TAG = "board";

esp_err_t board_early_init(void) { return ESP_OK; }

esp_err_t board_display_init(esp_lcd_panel_io_handle_t *io, esp_lcd_panel_handle_t *panel) {
  const drv_st7789_cfg_t cfg = {
    .sclk = BOARD_LCD_SCLK, .mosi = BOARD_LCD_MOSI, .cs = BOARD_LCD_CS, .dc = BOARD_LCD_DC, .rst = BOARD_LCD_RST,
    .backlight = BOARD_LCD_BL, .spi_mode = BOARD_LCD_SPI_MODE, .pclk_hz = BOARD_LCD_PCLK_HZ,
    .x_gap = BOARD_LCD_X_GAP, .y_gap = BOARD_LCD_Y_GAP,
    .swap_xy = BOARD_LCD_SWAP_XY, .mirror_x = BOARD_LCD_MIRROR_X, .mirror_y = BOARD_LCD_MIRROR_Y, .invert = true,
    .max_transfer_bytes = SCREEN_W * BOARD_LCD_BUF_LINES * 2,
  };
  return drv_st7789_init(&cfg, io, panel);
}

esp_err_t board_touch_init(esp_lcd_touch_handle_t *tp) {
  (void)tp;
  return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t board_audio_init(board_audio_t *out) {
  const drv_i2s_simplex_cfg_t cfg = {
    .mic_port = BOARD_MIC_I2S_PORT, .mic_bclk = BOARD_MIC_BCLK, .mic_ws = BOARD_MIC_WS, .mic_din = BOARD_MIC_SD,
    .spk_port = BOARD_SPK_I2S_PORT, .spk_bclk = BOARD_SPK_BCLK, .spk_ws = BOARD_SPK_LRC, .spk_dout = BOARD_SPK_DIN,
    .spk_rate = BOARD_SPK_RATE, .mic_shift = BOARD_MIC_SHIFT,
  };
  return drv_i2s_simplex_init(&cfg, out);
}

esp_err_t board_buttons_init(void) {
  const gpio_config_t keys = {
    .pin_bit_mask = (1ULL << BOARD_BTN_TALK) | (1ULL << BOARD_BTN_CANCEL),
    .mode = GPIO_MODE_INPUT,
    .pull_up_en = GPIO_PULLUP_ENABLE,
  };
  ESP_RETURN_ON_ERROR(gpio_config(&keys), TAG, "buttons");
  return ESP_OK;
}

bool board_talk_pressed(void) { return gpio_get_level(BOARD_BTN_TALK) == 0; }
bool board_cancel_pressed(void) { return gpio_get_level(BOARD_BTN_CANCEL) == 0; }

bool board_battery_read(gadget_battery_t *out) {
  (void)out;
  return false;
}

void board_set_brightness(uint8_t pct) { drv_st7789_set_brightness(pct); }
