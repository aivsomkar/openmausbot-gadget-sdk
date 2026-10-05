/* SPDX-License-Identifier: Apache-2.0 */
/* Board glue for amoled-175 (main/board_api.h). */
#include "board.h"

#include "board_api.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "drv_axp2101.h"
#include "drv_co5300.h"
#include "drv_cst9217.h"
#include "drv_es_codec.h"
#include "esp_check.h"
#include "esp_log.h"

#define SCREEN 466

static const char *TAG = "board";
static i2c_master_bus_handle_t s_i2c;
static esp_lcd_panel_handle_t s_panel;
static bool s_pmu;
static const co5300_lcd_init_cmd_t k_panel_init[] = BOARD_CO5300_INIT_CMDS;

esp_err_t board_early_init(void) {
  const i2c_master_bus_config_t bus = {
    .i2c_port = BOARD_I2C_PORT,
    .sda_io_num = BOARD_I2C_SDA,
    .scl_io_num = BOARD_I2C_SCL,
    .clk_source = I2C_CLK_SRC_DEFAULT,
    .glitch_ignore_cnt = 7,
    .flags.enable_internal_pullup = true,
  };
  ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bus, &s_i2c), TAG, "i2c bus");
  s_pmu = drv_axp2101_init(s_i2c) == ESP_OK;
  if (!s_pmu) {
    ESP_LOGW(TAG, "AXP2101 not answering; no battery level");
  }
  return ESP_OK;
}

esp_err_t board_display_init(esp_lcd_panel_io_handle_t *io, esp_lcd_panel_handle_t *panel) {
  const drv_co5300_cfg_t cfg = {
    .sclk = BOARD_LCD_SCLK, .d0 = BOARD_LCD_D0, .d1 = BOARD_LCD_D1, .d2 = BOARD_LCD_D2, .d3 = BOARD_LCD_D3,
    .cs = BOARD_LCD_CS, .rst = BOARD_LCD_RST, .x_gap = BOARD_LCD_X_GAP,
    .max_transfer_bytes = SCREEN * BOARD_LCD_BUF_LINES * 2,
    .init_cmds = k_panel_init, /* NULL here selects the component's own table (checklist check 2) */
    .init_cmds_size = sizeof(k_panel_init) / sizeof(k_panel_init[0]),
  };
  ESP_RETURN_ON_ERROR(drv_co5300_init(&cfg, io, panel), TAG, "co5300");
  s_panel = *panel;
  return ESP_OK;
}

esp_err_t board_touch_init(esp_lcd_touch_handle_t *tp) {
  const drv_cst9217_cfg_t cfg = {
    .bus = s_i2c, .rst = BOARD_TP_RST, .intr = BOARD_TP_INT, .width = SCREEN, .height = SCREEN,
    .mirror_x = BOARD_TP_MIRROR_X, .mirror_y = BOARD_TP_MIRROR_Y,
  };
  return drv_cst9217_init(&cfg, tp);
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
  const gpio_config_t talk = {.pin_bit_mask = 1ULL << BOARD_BTN_TALK, .mode = GPIO_MODE_INPUT, .pull_up_en = GPIO_PULLUP_ENABLE};
  return gpio_config(&talk);
}

bool board_talk_pressed(void) { return gpio_get_level(BOARD_BTN_TALK) == 0; }
bool board_cancel_pressed(void) { return false; }
bool board_battery_read(gadget_battery_t *out) { return s_pmu && drv_axp2101_read(out); }

void board_set_brightness(uint8_t pct) {
  if (s_panel != NULL) {
    drv_co5300_set_brightness(s_panel, pct);
  }
}
