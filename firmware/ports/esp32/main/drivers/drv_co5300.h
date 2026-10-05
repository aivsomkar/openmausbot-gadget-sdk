/* SPDX-License-Identifier: Apache-2.0 */
/* CO5300 AMOLED panel on QSPI (amoled-175c, amoled-175), on top of the
 * espressif/esp_lcd_co5300 driver component. The register init table is the
 * board's (BOARD_CO5300_INIT_CMDS in boards/<id>/board.h). */
#ifndef DRV_CO5300_H
#define DRV_CO5300_H

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "esp_lcd_co5300.h"
#include "esp_lcd_types.h"

typedef struct {
  int sclk, d0, d1, d2, d3, cs, rst;
  int x_gap;                   /* first visible column (6 on the 1.75 panels) */
  size_t max_transfer_bytes;   /* one LVGL draw buffer */
  const co5300_lcd_init_cmd_t *init_cmds; /* the board's table; NULL = the component's default */
  uint16_t init_cmds_size;     /* entries in init_cmds (0 with NULL) */
} drv_co5300_cfg_t;

esp_err_t drv_co5300_init(const drv_co5300_cfg_t *cfg, esp_lcd_panel_io_handle_t *io, esp_lcd_panel_handle_t *panel);
void drv_co5300_set_brightness(esp_lcd_panel_handle_t panel, uint8_t pct);

#endif /* DRV_CO5300_H */
