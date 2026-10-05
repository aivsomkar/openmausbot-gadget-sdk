/* SPDX-License-Identifier: Apache-2.0 */
#include "drv_lipo_adc.h"

#include "driver/gpio.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_check.h"
#include "pl_power.h"

#define SAMPLES 8

static const char *TAG = "lipo_adc";
static adc_oneshot_unit_handle_t s_unit;
static adc_cali_handle_t s_cali;
static adc_channel_t s_chan;
static drv_lipo_adc_cfg_t s_cfg;

esp_err_t drv_lipo_adc_init(const drv_lipo_adc_cfg_t *cfg) {
  s_cfg = *cfg;
  adc_unit_t unit;
  ESP_RETURN_ON_ERROR(adc_oneshot_io_to_channel(cfg->adc_gpio, &unit, &s_chan), TAG, "adc pin");
  const adc_oneshot_unit_init_cfg_t unit_cfg = {.unit_id = unit};
  ESP_RETURN_ON_ERROR(adc_oneshot_new_unit(&unit_cfg, &s_unit), TAG, "adc unit");
  const adc_oneshot_chan_cfg_t chan_cfg = {.atten = ADC_ATTEN_DB_12, .bitwidth = ADC_BITWIDTH_DEFAULT};
  ESP_RETURN_ON_ERROR(adc_oneshot_config_channel(s_unit, s_chan, &chan_cfg), TAG, "adc channel");
  const adc_cali_curve_fitting_config_t cali = {
    .unit_id = unit, .chan = s_chan, .atten = ADC_ATTEN_DB_12, .bitwidth = ADC_BITWIDTH_DEFAULT};
  ESP_RETURN_ON_ERROR(adc_cali_create_scheme_curve_fitting(&cali, &s_cali), TAG, "adc calibration");
  const gpio_config_t chg = {
    .pin_bit_mask = 1ULL << cfg->chg_gpio,
    .mode = GPIO_MODE_INPUT,
    .pull_up_en = GPIO_PULLUP_ENABLE,
  };
  return gpio_config(&chg);
}

bool drv_lipo_adc_read(gadget_battery_t *out) {
  if (s_unit == NULL) {
    return false;
  }
  int sum = 0;
  for (int i = 0; i < SAMPLES; i++) {
    int raw = 0, mv = 0;
    if (adc_oneshot_read(s_unit, s_chan, &raw) != ESP_OK || adc_cali_raw_to_voltage(s_cali, raw, &mv) != ESP_OK) {
      return false;
    }
    sum += mv;
  }
  uint32_t cell_mv = (uint32_t)(sum / SAMPLES) * s_cfg.divider_x1000 / 1000u;
  if (!pl_lipo_present(cell_mv)) {
    return false; /* no cell: the divider sits at ground (hardware checklist B8a) */
  }
  out->pct = pl_lipo_pct(cell_mv);
  out->charging = gpio_get_level(s_cfg.chg_gpio) == 0;
  return true;
}
