/* firmware/core/include/gadget_console.h */
/* SPDX-License-Identifier: Apache-2.0 */
/* Console line handling shared by both ports (spec §5.6). */
#ifndef GADGET_CONSOLE_H
#define GADGET_CONSOLE_H

#include "gadget_types.h"

/* Bytes incl. NUL; longer lines are rejected. Sized for the longest valid
 * line: `say` + a quoted 2000-code-point text at up to 4 UTF-8 bytes per
 * code point (escapes are 2 bytes for 1-byte characters) = 8007 bytes. */
#define GADGET_CONSOLE_LINE_MAX 8192u
#define GADGET_CONSOLE_ARGV_MAX 8u

/* Line assembler: feed raw console bytes; calls on_line for each complete
 * line. A line ends at CR, LF or CRLF (a CR followed by LF is one ending).
 * An over-long line is dropped up to its ending and reported as
 * on_line(NULL). Ports post each line as GADGET_EV_CONSOLE_LINE. */
typedef struct {
  char buf[GADGET_CONSOLE_LINE_MAX];
  size_t len;
  bool overflow;
  bool last_cr;
} gadget_linebuf_t;
typedef void (*gadget_line_fn)(const char *line, void *ctx);
void gadget_linebuf_init(gadget_linebuf_t *lb);
void gadget_linebuf_feed(gadget_linebuf_t *lb, const char *data, size_t n, gadget_line_fn on_line, void *ctx);

/* esp_console_split_argv rules: whitespace separates arguments; "…" groups;
 * inside or outside quotes, \\ → \, \" → ", "\ " → space. Splits in place.
 * Returns argc, or -1 for an unterminated quote. */
int gadget_console_split(char *line, char *argv[], int argv_max);

typedef enum {
  GC_EMPTY = 0, GC_WIFI, GC_SCAN, GC_HOST_AUTO, GC_HOST_SET, GC_PAIR, GC_NAME, GC_SAY,
  GC_STATUS, GC_LOG_OFF, GC_LOG_ON, GC_FORGET, GC_REBOOT,
  GC_UNKNOWN,    /* not a command → @omb error */
  GC_BAD_ARGS    /* a command with invalid arguments → @omb error */
} gadget_console_cmd_t;

typedef struct {
  gadget_console_cmd_t cmd;
  const char *cmd_name;   /* argv[0], for the error line */
  const char *a;          /* wifi ssid | host address | pair code | name | say text */
  const char *b;          /* wifi password */
  uint16_t port;          /* host <address>[:port]; default GADGET_DEFAULT_PORT */
  char error[96];         /* GC_UNKNOWN / GC_BAD_ARGS: Latin-1 message */
} gadget_console_parsed_t;

/* Parse one line (modified in place; out's pointers point into it). */
gadget_console_cmd_t gadget_console_parse(char *line, gadget_console_parsed_t *out);

#endif /* GADGET_CONSOLE_H */
