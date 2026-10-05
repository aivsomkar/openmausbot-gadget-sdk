/* firmware/ports/sim/sim_console.c */
/* SPDX-License-Identifier: Apache-2.0 */
/* The stdin console: the same commands as the USB serial console. */
#include <poll.h>
#include <unistd.h>
#include "gadget_console.h"
#include "sim_hal.h"
#include "sim_internal.h"

static gadget_linebuf_t s_lb;
static bool s_started, s_eof;

static void on_line(const char *line, void *ctx) {
  (void)ctx;
  gadget_event_t ev = {.type = GADGET_EV_CONSOLE_LINE};
  ev.u.console.line = line; /* NULL: an over-long line was dropped */
  sim_post_event(&ev);
}

void sim_console_poll(void) {
  if (s_eof) return;
  if (!s_started) {
    gadget_linebuf_init(&s_lb);
    s_started = true;
  }
  struct pollfd p = {.fd = STDIN_FILENO, .events = POLLIN};
  while (poll(&p, 1, 0) > 0 && (p.revents & (POLLIN | POLLHUP))) {
    char buf[512];
    ssize_t n = read(STDIN_FILENO, buf, sizeof buf);
    if (n <= 0) {
      s_eof = true; /* CI passes /dev/null */
      return;
    }
    gadget_linebuf_feed(&s_lb, buf, (size_t)n, on_line, NULL);
  }
}
