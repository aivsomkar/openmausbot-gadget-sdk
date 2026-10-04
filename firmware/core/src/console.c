/* firmware/core/src/console.c */
/* SPDX-License-Identifier: Apache-2.0 */
/* Console line assembly, argument splitting and the command grammar
 * (gadget_console.h, spec §5.6, contract §2.11). Pure: no HAL calls. */
#include <stdio.h>
#include <string.h>
#include "gadget_console.h"
#include "gadget_util.h"

/* ---- line assembler -------------------------------------------------------- */

void gadget_linebuf_init(gadget_linebuf_t *lb) { memset(lb, 0, sizeof *lb); }

static void end_line(gadget_linebuf_t *lb, gadget_line_fn on_line, void *ctx) {
  if (lb->overflow) {
    on_line(NULL, ctx);
  } else {
    lb->buf[lb->len] = '\0';
    on_line(lb->buf, ctx);
  }
  lb->len = 0;
  lb->overflow = false;
}

void gadget_linebuf_feed(gadget_linebuf_t *lb, const char *data, size_t n, gadget_line_fn on_line, void *ctx) {
  for (size_t i = 0; i < n; i++) {
    char c = data[i];
    if (c == '\n') {
      if (lb->last_cr) {
        lb->last_cr = false; /* the LF of a CRLF: already ended */
        continue;
      }
      end_line(lb, on_line, ctx);
      continue;
    }
    lb->last_cr = false;
    if (c == '\r') {
      lb->last_cr = true;
      end_line(lb, on_line, ctx);
      continue;
    }
    if (lb->len + 1 >= GADGET_CONSOLE_LINE_MAX) {
      lb->overflow = true;
      continue;
    }
    lb->buf[lb->len++] = c;
  }
}

/* ---- argument splitting ------------------------------------------------------ */

static bool is_space(char c) { return c == ' ' || c == '\t'; }

int gadget_console_split(char *line, char *argv[], int argv_max) {
  int argc = 0;
  char *r = line;
  char *w = line;
  while (*r) {
    while (is_space(*r)) r++;
    if (*r == '\0') break;
    char *start = w;
    bool quoted = false;
    while (*r && (quoted || !is_space(*r))) {
      if (*r == '\\' && (r[1] == '\\' || r[1] == '"' || r[1] == ' ')) {
        *w++ = r[1];
        r += 2;
      } else if (*r == '"') {
        quoted = !quoted;
        r++;
      } else {
        *w++ = *r++;
      }
    }
    if (quoted) return -1;
    bool more = *r != '\0';
    if (more) r++;  /* skip the separating whitespace before writing the NUL */
    *w++ = '\0';
    if (argc < argv_max) argv[argc] = start;
    argc++;
    if (!more) break;
  }
  return argc > argv_max ? argv_max + 1 : argc;
}

/* ---- grammar ------------------------------------------------------------------ */

static gadget_console_cmd_t fail(gadget_console_parsed_t *out, const char *msg) {
  snprintf(out->error, sizeof out->error, "%s", msg);
  out->cmd = GC_BAD_ARGS;
  return GC_BAD_ARGS;
}

static char *trim(char *s) {
  while (is_space(*s)) s++;
  size_t n = strlen(s);
  while (n > 0 && is_space(s[n - 1])) s[--n] = '\0';
  return s;
}

static bool valid_password(const char *p) {
  size_t n = strlen(p);
  if (n == 0 || (n >= 8 && n <= 63)) return true;
  if (n != 64) return false;
  for (size_t i = 0; i < n; i++) {
    char c = p[i];
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) return false;
  }
  return true;
}

/* host <address>[:port]: a hostname or IPv4 address (≤ 57 bytes, so
 * "addr:port" fits a 64-byte buffer), port 1-65535. */
static gadget_console_cmd_t parse_host(char *arg, gadget_console_parsed_t *out) {
  static const char *usage = "host needs auto or <address>[:port]";
  char *colon = strchr(arg, ':');
  out->port = GADGET_DEFAULT_PORT;
  if (colon != NULL) {
    if (strchr(colon + 1, ':') != NULL) return fail(out, usage);
    *colon = '\0';
    const char *p = colon + 1;
    if (*p == '\0' || strlen(p) > 5) return fail(out, "the port must be 1-65535");
    unsigned long v = 0;
    for (; *p; p++) {
      if (*p < '0' || *p > '9') return fail(out, "the port must be 1-65535");
      v = v * 10 + (unsigned long)(*p - '0');
    }
    if (v < 1 || v > 65535) return fail(out, "the port must be 1-65535");
    out->port = (uint16_t)v;
  }
  size_t n = strlen(arg);
  if (n == 0 || n > 57) return fail(out, usage);
  for (size_t i = 0; i < n; i++) {
    char c = arg[i];
    bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '-';
    if (!ok) return fail(out, usage);
  }
  out->a = arg;
  out->cmd = GC_HOST_SET;
  return GC_HOST_SET;
}

gadget_console_cmd_t gadget_console_parse(char *line, gadget_console_parsed_t *out) {
  memset(out, 0, sizeof *out);
  out->cmd_name = "";
  char *argv[GADGET_CONSOLE_ARGV_MAX];
  int argc = gadget_console_split(line, argv, GADGET_CONSOLE_ARGV_MAX);
  if (argc < 0) return fail(out, "unterminated quote");
  if (argc == 0) {
    out->cmd = GC_EMPTY;
    return GC_EMPTY;
  }
  const char *cmd = argv[0];
  out->cmd_name = cmd;

  if (strcmp(cmd, "status") == 0 || strcmp(cmd, "scan") == 0 || strcmp(cmd, "forget") == 0 ||
      strcmp(cmd, "reboot") == 0) {
    if (argc != 1) {
      snprintf(out->error, sizeof out->error, "%s takes no arguments", cmd);
      out->cmd = GC_BAD_ARGS;
      return GC_BAD_ARGS;
    }
    out->cmd = cmd[0] == 's' ? (cmd[1] == 't' ? GC_STATUS : GC_SCAN) : (cmd[0] == 'f' ? GC_FORGET : GC_REBOOT);
    return out->cmd;
  }
  if (strcmp(cmd, "wifi") == 0) {
    if (argc != 3) return fail(out, "wifi needs \"<ssid>\" \"<password>\"");
    size_t n = strlen(argv[1]);
    if (n < 1 || n > 32) return fail(out, "the Wi-Fi name must be 1-32 bytes");
    if (!valid_password(argv[2])) return fail(out, "the password must be empty, 8-63 characters or 64 hex digits");
    out->a = argv[1];
    out->b = argv[2];
    out->cmd = GC_WIFI;
    return GC_WIFI;
  }
  if (strcmp(cmd, "host") == 0) {
    if (argc != 2) return fail(out, "host needs auto or <address>[:port]");
    if (strcmp(argv[1], "auto") == 0) {
      out->cmd = GC_HOST_AUTO;
      return GC_HOST_AUTO;
    }
    return parse_host(argv[1], out);
  }
  if (strcmp(cmd, "pair") == 0) {
    if (argc != 2 || !gadget_pair_code_valid(argv[1])) return fail(out, "pair needs a six-digit code");
    out->a = argv[1];
    out->cmd = GC_PAIR;
    return GC_PAIR;
  }
  if (strcmp(cmd, "name") == 0 || strcmp(cmd, "say") == 0) {
    bool is_name = cmd[0] == 'n';
    const char *msg = is_name ? "name needs 1-32 characters" : "say needs 1-2000 characters";
    if (argc != 2) return fail(out, msg);
    char *text = trim(argv[1]);
    size_t cps = gadget_utf8_len(text);
    if (cps < 1 || cps > (is_name ? GADGET_NAME_MAX : GADGET_SAY_MAX)) return fail(out, msg);
    out->a = text;
    out->cmd = is_name ? GC_NAME : GC_SAY;
    return out->cmd;
  }
  if (strcmp(cmd, "log") == 0) {
    if (argc == 2 && strcmp(argv[1], "off") == 0) {
      out->cmd = GC_LOG_OFF;
      return GC_LOG_OFF;
    }
    if (argc == 2 && strcmp(argv[1], "on") == 0) {
      out->cmd = GC_LOG_ON;
      return GC_LOG_ON;
    }
    return fail(out, "log needs on or off");
  }
  snprintf(out->error, sizeof out->error, "unknown command; try status");
  out->cmd = GC_UNKNOWN;
  return GC_UNKNOWN;
}
