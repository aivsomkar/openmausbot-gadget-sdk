/* SPDX-License-Identifier: Apache-2.0 */
/* HAL battery group. Core may poll every tick; the board is read at most
 * every 5 s (an I2C or ADC read) and the last reading is returned between. */
#include "gadget_hal.h"
#include "port.h"

#define BATTERY_READ_EVERY_MS 5000u

static uint64_t s_next_ms;
static bool s_valid;
static gadget_battery_t s_last;

bool hal_battery_read(gadget_battery_t *out) {
  const gadget_board_t *b = port_board();
  if (b == NULL || !b->has_battery || out == NULL) {
    return false;
  }
  uint64_t now = hal_now_ms();
  if (s_next_ms == 0 || now >= s_next_ms) {
    s_valid = board_battery_read(&s_last);
    s_next_ms = now + BATTERY_READ_EVERY_MS;
  }
  if (s_valid) {
    *out = s_last;
  }
  return s_valid;
}
