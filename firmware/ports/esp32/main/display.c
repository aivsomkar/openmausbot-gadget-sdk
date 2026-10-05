/* SPDX-License-Identifier: Apache-2.0 */
/* LVGL 9 display and touch glue, single-threaded (no esp_lvgl_port, which
 * runs LVGL on its own task). The gadget task calls lv_timer_handler()
 * through ui_tick(); LVGL renders into two internal DMA buffers of
 * BOARD_LCD_BUF_LINES lines and flush_cb() hands each area to the panel.
 * The panel IO's transfer-done callback tells LVGL the buffer is free. */
#include "board.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "lvgl.h"
#include "pl_display.h"
#include "port.h"

#ifndef BOARD_LCD_EVEN_AREAS
#define BOARD_LCD_EVEN_AREAS 0
#endif

static const char *TAG = "display";
static esp_lcd_panel_handle_t s_panel;
static lv_display_t *s_disp;
static struct {
  bool pressed;
  int16_t x, y;
} s_touch;

static uint32_t tick_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }

/* Runs in the SPI driver's interrupt context. */
static bool on_trans_done(esp_lcd_panel_io_handle_t io, esp_lcd_panel_io_event_data_t *edata, void *ctx) {
  (void)io;
  (void)edata;
  lv_display_flush_ready((lv_display_t *)ctx);
  return false;
}

static void flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px) {
  uint32_t n = (uint32_t)lv_area_get_width(area) * (uint32_t)lv_area_get_height(area);
#if LVGL_VERSION_MAJOR > 9 || (LVGL_VERSION_MAJOR == 9 && LVGL_VERSION_MINOR >= 6)
  lv_draw_rgb565_swap(px, n); /* the panels take big-endian RGB565 */
#else
  lv_draw_sw_rgb565_swap(px, n);
#endif
  if (esp_lcd_panel_draw_bitmap(s_panel, area->x1, area->y1, area->x2 + 1, area->y2 + 1, px) != ESP_OK) {
    lv_display_flush_ready(disp); /* no transfer started, so no done callback will come */
  }
}

static void rounder_cb(lv_event_t *e) {
  lv_area_t *a = lv_event_get_param(e);
  int32_t x1 = a->x1, y1 = a->y1, x2 = a->x2, y2 = a->y2;
  pl_round_even_area(&x1, &y1, &x2, &y2, lv_display_get_horizontal_resolution(s_disp),
                     lv_display_get_vertical_resolution(s_disp));
  a->x1 = x1;
  a->y1 = y1;
  a->x2 = x2;
  a->y2 = y2;
}

static void touch_read_cb(lv_indev_t *indev, lv_indev_data_t *data) {
  (void)indev;
  data->point.x = s_touch.x;
  data->point.y = s_touch.y;
  data->state = s_touch.pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

void port_display_touch(bool pressed, int16_t x, int16_t y) {
  s_touch.pressed = pressed;
  s_touch.x = x;
  s_touch.y = y;
}

esp_err_t port_display_init(const gadget_board_t *board, esp_lcd_panel_io_handle_t io, esp_lcd_panel_handle_t panel,
                            bool has_touch) {
  s_panel = panel;
  lv_init();
  lv_tick_set_cb(tick_ms);
  s_disp = lv_display_create(board->screen_w, board->screen_h);
  if (s_disp == NULL) {
    return ESP_ERR_NO_MEM;
  }
  size_t bytes = (size_t)board->screen_w * BOARD_LCD_BUF_LINES * sizeof(uint16_t);
  void *buf1 = heap_caps_malloc(bytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
  void *buf2 = heap_caps_malloc(bytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
  if (buf1 == NULL || buf2 == NULL) {
    ESP_LOGE(TAG, "no internal DMA memory for 2 x %u bytes", (unsigned)bytes);
    return ESP_ERR_NO_MEM;
  }
  lv_display_set_color_format(s_disp, LV_COLOR_FORMAT_RGB565);
  lv_display_set_buffers(s_disp, buf1, buf2, (uint32_t)bytes, LV_DISPLAY_RENDER_MODE_PARTIAL);
  lv_display_set_flush_cb(s_disp, flush_cb);
  const esp_lcd_panel_io_callbacks_t cbs = {.on_color_trans_done = on_trans_done};
  ESP_RETURN_ON_ERROR(esp_lcd_panel_io_register_event_callbacks(io, &cbs, s_disp), TAG, "panel io callbacks");
  if (BOARD_LCD_EVEN_AREAS) {
    lv_display_add_event_cb(s_disp, rounder_cb, LV_EVENT_INVALIDATE_AREA, NULL);
  }
  if (has_touch) {
    lv_indev_t *indev = lv_indev_create();
    if (indev == NULL) {
      return ESP_ERR_NO_MEM;
    }
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, touch_read_cb);
    lv_indev_set_display(indev, s_disp);
  }
  return ESP_OK;
}
