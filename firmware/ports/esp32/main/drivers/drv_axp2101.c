/* SPDX-License-Identifier: Apache-2.0 */
#include "drv_axp2101.h"

#include "esp_check.h"
#include "pl_power.h"

#define I2C_TIMEOUT_MS 50

static const char *TAG = "axp2101";
static i2c_master_dev_handle_t s_dev;

static esp_err_t read_reg(uint8_t reg, uint8_t *val) {
  return i2c_master_transmit_receive(s_dev, &reg, 1, val, 1, I2C_TIMEOUT_MS);
}

static esp_err_t write_reg(uint8_t reg, uint8_t val) {
  const uint8_t buf[2] = {reg, val};
  return i2c_master_transmit(s_dev, buf, sizeof(buf), I2C_TIMEOUT_MS);
}

esp_err_t drv_axp2101_init(i2c_master_bus_handle_t bus) {
  const i2c_device_config_t dev = {
    .dev_addr_length = I2C_ADDR_BIT_LEN_7,
    .device_address = PL_AXP2101_ADDR,
    .scl_speed_hz = 400000,
  };
  ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(bus, &dev, &s_dev), TAG, "add device");
  uint8_t ctrl = 0;
  ESP_RETURN_ON_ERROR(read_reg(PL_AXP2101_REG_GAUGE_CTRL, &ctrl), TAG, "read gauge control");
  if ((ctrl & 0x08u) == 0) {
    ESP_RETURN_ON_ERROR(write_reg(PL_AXP2101_REG_GAUGE_CTRL, (uint8_t)(ctrl | 0x08u)), TAG, "enable gauge");
  }
  return ESP_OK;
}

bool drv_axp2101_read(gadget_battery_t *out) {
  uint8_t s1 = 0, s2 = 0, pct = 0;
  if (s_dev == NULL || read_reg(PL_AXP2101_REG_STATUS1, &s1) != ESP_OK ||
      read_reg(PL_AXP2101_REG_STATUS2, &s2) != ESP_OK || read_reg(PL_AXP2101_REG_BATT_PCT, &pct) != ESP_OK) {
    return false;
  }
  return pl_axp2101_decode(s1, s2, pct, out);
}
