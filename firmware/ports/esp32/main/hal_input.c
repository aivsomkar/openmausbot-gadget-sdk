/* SPDX-License-Identifier: Apache-2.0 */
/* HAL input group: the gadget task polls buttons and touch every 10 ms and
 * hands raw edges to core (core derives holds, taps and swipes). The same
 * touch point drives LVGL's pointer through display.c. */
#include "esp_lcd_touch.h"
#include "gadget_core.h"
#include "pl_input.h"
#include "port.h"

static const gadget_board_t *s_board;
static esp_lcd_touch_handle_t s_touch;
static pl_button_t s_talk;
static pl_button_t s_cancel;
static pl_touch_t s_tp;

void port_input_init(const gadget_board_t *board, esp_lcd_touch_handle_t touch) {
  s_board = board;
  s_touch = touch;
  pl_button_init(&s_talk, board_talk_pressed());
  pl_button_init(&s_cancel, board_cancel_pressed());
  pl_touch_init(&s_tp);
}

static void send_input(const gadget_input_t *in) {
  gadget_event_t ev = {.type = GADGET_EV_INPUT};
  ev.u.input = *in;
  core_event(&ev);
}

static void button(pl_button_t *b, bool raw, gadget_input_type_t down, gadget_input_type_t up) {
  int edge = pl_button_poll(b, raw);
  if (edge != 0) {
    gadget_input_t in = {.type = edge > 0 ? down : up};
    send_input(&in);
  }
}

void port_input_poll(void) {
  if (s_board == NULL) {
    return;
  }
  if (s_board->input_mask & GADGET_INPUT_TALK) {
    button(&s_talk, board_talk_pressed(), GADGET_IN_TALK_DOWN, GADGET_IN_TALK_UP);
  }
  if (s_board->input_mask & GADGET_INPUT_CANCEL) {
    button(&s_cancel, board_cancel_pressed(), GADGET_IN_CANCEL_DOWN, GADGET_IN_CANCEL_UP);
  }
  if (s_touch == NULL) {
    return;
  }
  bool pressed = false;
  int32_t x = 0, y = 0;
  if (esp_lcd_touch_read_data(s_touch) == ESP_OK) {
    esp_lcd_touch_point_data_t pt[1];
    uint8_t count = 0;
    if (esp_lcd_touch_get_data(s_touch, pt, &count, 1) == ESP_OK && count > 0) {
      pressed = true;
      x = pt[0].x;
      y = pt[0].y;
    }
  }
  gadget_input_t in;
  if (pl_touch_poll(&s_tp, pressed, x, y, s_board->screen_w, s_board->screen_h, &in)) {
    send_input(&in);
  }
  port_display_touch(s_tp.down, s_tp.x, s_tp.y);
}
