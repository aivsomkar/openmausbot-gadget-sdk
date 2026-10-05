/* SPDX-License-Identifier: Apache-2.0 */
/* ESP32 port internals shared by main.c, display.c and the hal_*.c files.
 * Threading (gadget_hal.h): core, the UI and every hal_* call run on the
 * "gadget" task. Driver tasks hand results to that task through the port
 * queue (port_post_event / port_post_wifi) and never call core. */
#ifndef PORT_H
#define PORT_H

#include <stdbool.h>
#include <stdint.h>

#include "board_api.h"
#include "esp_err.h"
#include "gadget_board.h"
#include "gadget_events.h"

typedef enum { PORT_WIFI_GOT_IP = 1, PORT_WIFI_DISCONNECTED, PORT_WIFI_SCAN_DONE } port_wifi_kind_t;

typedef struct {
  port_wifi_kind_t kind;
  bool local;        /* DISCONNECTED: reason WIFI_REASON_ASSOC_LEAVE, our own disconnect */
  bool auth_failed;  /* DISCONNECTED: wrong password or handshake failure */
  bool scan_failed;  /* SCAN_DONE: the driver reported status 1 */
  char ip[16];       /* GOT_IP: dotted IPv4 */
} port_wifi_msg_t;

/* port_events.c */
esp_err_t port_events_init(void);
/* Remember the gadget task: posts from it never wait on a full queue. */
void port_set_main_task(void);
/* Any task. Deep-copies ev (pl_event_copy). Mic frames are dropped at once
 * when the queue is full; other events wait up to 2 s. */
bool port_post_event(const gadget_event_t *ev);
bool port_post_wifi(const port_wifi_msg_t *m);
/* Gadget task: deliver every queued message (core_event / hal_wifi.c). */
void port_drain(void);

/* hal_system.c */
esp_err_t port_system_init(void);   /* first call in app_main: console output lock */
void port_set_board(const gadget_board_t *board);
const gadget_board_t *port_board(void);

/* console_usj.c: USB-Serial-JTAG driver + reader task */
esp_err_t port_console_start(void);

/* hal_wifi.c */
esp_err_t port_wifi_start(void);   /* netif, event loop, STA mode, esp_wifi_start() */
void port_wifi_on_msg(const port_wifi_msg_t *m);
void port_wifi_tick(uint64_t now_ms);

/* hal_ws.c */
esp_err_t port_ws_init(void);
/* port_drain() calls this before core sees WS_OPEN / WS_CLOSED. */
void port_ws_note(gadget_event_type_t type);

/* hal_mdns.c */
esp_err_t port_mdns_init(void);

/* hal_audio.c: starts the mic and speaker tasks; without it the mic and
 * speaker HAL report GADGET_ERR_UNSUPPORTED / accept nothing. */
esp_err_t port_audio_start(const board_audio_t *audio);

/* hal_input.c: polled by the gadget task every 10 ms; calls core_event(). */
void port_input_init(const gadget_board_t *board, esp_lcd_touch_handle_t touch);
void port_input_poll(void);

/* hal_ota.c */
esp_err_t port_ota_init(void);

/* display.c: lv_init, the display, flush to the panel, the touch pointer */
esp_err_t port_display_init(const gadget_board_t *board, esp_lcd_panel_io_handle_t io, esp_lcd_panel_handle_t panel,
                            bool has_touch);
void port_display_touch(bool pressed, int16_t x, int16_t y);

#endif /* PORT_H */
