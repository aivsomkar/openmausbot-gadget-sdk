/* SPDX-License-Identifier: Apache-2.0 */
/* Battery voltage through a resistor divider on an ADC1 pin plus a charger
 * status pin (lcd-154: BAT_ADC GPIO1 behind x3, CHG_STAT GPIO3 low while
 * charging). The percentage comes from the voltage (pl_lipo_pct). */
#ifndef DRV_LIPO_ADC_H
#define DRV_LIPO_ADC_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "gadget_types.h"

typedef struct {
  int adc_gpio;
  int chg_gpio;            /* reads low while charging */
  uint32_t divider_x1000;  /* 3000 = x3 */
} drv_lipo_adc_cfg_t;

esp_err_t drv_lipo_adc_init(const drv_lipo_adc_cfg_t *cfg);
bool drv_lipo_adc_read(gadget_battery_t *out);

#endif /* DRV_LIPO_ADC_H */
