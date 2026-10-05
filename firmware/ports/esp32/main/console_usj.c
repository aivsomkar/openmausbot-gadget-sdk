/* SPDX-License-Identifier: Apache-2.0 */
/* Console input over the native USB-Serial-JTAG port (spec §5.6). The
 * reader task splits bytes into lines (CR, LF or CRLF, gadget_console.h)
 * and posts each one to core as GADGET_EV_CONSOLE_LINE. Output goes through
 * stdout, which this switches to the interrupt-driven driver. */
#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "gadget_console.h"
#include "gadget_hal.h"
#include "port.h"

static gadget_linebuf_t s_lb; /* 8 KiB: static, not on the task stack */

static void on_line(const char *line, void *ctx) {
  (void)ctx;
  if (line == NULL) {
    hal_console_write("@omb {\"op\":\"error\",\"cmd\":\"\",\"message\":\"line too long\"}");
    return;
  }
  gadget_event_t ev = {.type = GADGET_EV_CONSOLE_LINE};
  ev.u.console.line = line;
  port_post_event(&ev);
}

static void console_task(void *arg) {
  (void)arg;
  char buf[128];
  for (;;) {
    int n = usb_serial_jtag_read_bytes(buf, sizeof(buf), pdMS_TO_TICKS(100));
    if (n > 0) {
      gadget_linebuf_feed(&s_lb, buf, (size_t)n, on_line, NULL);
    }
  }
}

esp_err_t port_console_start(void) {
  usb_serial_jtag_driver_config_t cfg = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
  cfg.rx_buffer_size = 1024;
  cfg.tx_buffer_size = 4096;
  esp_err_t err = usb_serial_jtag_driver_install(&cfg);
  if (err != ESP_OK) {
    return err;
  }
  usb_serial_jtag_vfs_use_driver();
  gadget_linebuf_init(&s_lb);
  BaseType_t ok = xTaskCreatePinnedToCore(console_task, "console", 4096, NULL, 2, NULL, 0);
  return ok == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}
