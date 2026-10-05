/* firmware/ports/sim/sim_script.c */
/* SPDX-License-Identifier: Apache-2.0 */
/* Headless scripts (spec §5.7, contract §2.16): one command per line, '#'
 * starts a comment. A command that acts (input, console, net_*) runs once per
 * main-loop iteration; wait / expect / model / boot / net_open block until
 * they are met or time out. `expect <op>` matches the first frame with that op since the
 * previous expect matched, including frames that crossed before the line
 * was reached, so scripts do not race the network. `net_open [timeout_ms]`
 * waits (5 s by default) for the gadget to ask for a connection. In
 * `net_text`, every `${turn}` becomes the turn of the gadget's last
 * voice.begin or say, so a script can answer a turn whose prefix is random
 * at every boot (contract §2.12). */
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include "gadget_console.h"
#include "gadget_hal.h"
#include "gadget_util.h"
#include "sim_display.h"
#include "sim_hal.h"
#include "sim_internal.h"

#define MAX_LINES 4096
#define MAX_OPS 8192
#define DEFAULT_TIMEOUT_MS 5000u
#define BOOT_TIMEOUT_MS 10000u
#define LINE_BYTES (GADGET_CONSOLE_LINE_MAX + 64u) /* "console " + the longest console line */

static struct {
  const char *path;
  char *lines[MAX_LINES];
  int n, pc;            /* pc: index of the current line */
  bool started;         /* the current blocking command has its deadline */
  uint64_t deadline;
  char *ops[MAX_OPS];   /* every text frame op seen, in order */
  int n_ops, cursor;
  bool pressed;
  int16_t px, py;
  char turn[GADGET_TURN_MAX + 1];  /* "" until the gadget sends voice.begin or say */
} S;

void sim_script_record(const char *op) {
  if (S.n_ops < MAX_OPS) S.ops[S.n_ops++] = strdup(op);
}

void sim_script_note_turn(const char *turn) { snprintf(S.turn, sizeof S.turn, "%s", turn); }

static int fail(const char *fmt, ...) GADGET_PRINTF(1, 2);
static int fail(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  fprintf(stderr, "gadget-sim: %s:%d: ", S.path, S.pc + 1);
  vfprintf(stderr, fmt, ap);
  fputc('\n', stderr);
  va_end(ap);
  return -1;
}

int sim_script_load(const char *path, unsigned boot) {
  S.path = path;
  FILE *f = fopen(path, "r");
  if (f == NULL) {
    fprintf(stderr, "gadget-sim: cannot open script %s\n", path);
    return -1;
  }
  char buf[LINE_BYTES];
  while (S.n < MAX_LINES && fgets(buf, sizeof buf, f) != NULL) {
    size_t len = strlen(buf);
    while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r')) buf[--len] = '\0';
    S.lines[S.n++] = strdup(buf);
  }
  fclose(f);
  S.pc = 0;
  if (boot > 0) {
    char want[32];
    snprintf(want, sizeof want, "boot %u", boot);
    int found = -1;
    for (int i = 0; i < S.n; i++) {
      if (strncmp(S.lines[i], want, strlen(want)) == 0 &&
          (S.lines[i][strlen(want)] == '\0' || S.lines[i][strlen(want)] == ' ')) {
        found = i;
      }
    }
    if (found < 0) {
      fprintf(stderr, "gadget-sim: %s has no \"%s\" line to resume at\n", path, want);
      return -1;
    }
    S.pc = found + 1;
  }
  return 0;
}

/* ---- model fields ----------------------------------------------------------- */

static const char *const SCREENS[] = {"boot", "setup", "offline", "idle", "listening", "thinking",
                                      "speaking", "reply", "ask", "card", "image", "update"};
static const char *const MAUS[] = {"none", "idle", "listening", "thinking", "working", "speaking",
                                   "sleeping", "curious", "notifying", "alerting"};
static const char *const PAIR[] = {"unpaired", "code_stored", "connecting", "paired", "error"};
static const char *const SETUP[] = {"need_wifi", "need_code", "pairing", "host_not_found", "bad_code", "device_limit"};
static const char *const OFFLINE[] = {"wifi_connecting", "wifi_failed", "host_lookup", "host_unreachable",
                                      "in_use_elsewhere", "protocol"};
static const char *const UPDATE[] = {"receiving", "verifying", "restarting"};

/* The field's current value as text, or NULL for an unknown field. */
static const char *field(const char *name, char *num, size_t cap) {
  const ui_model_t *m = core_ui_model();
#define NUM(v) (snprintf(num, cap, "%lu", (unsigned long)(v)), num)
#define BOOL(v) ((v) ? "true" : "false")
  if (strcmp(name, "screen") == 0) return SCREENS[m->screen];
  if (strcmp(name, "maus") == 0) return MAUS[m->maus];
  if (strcmp(name, "pair") == 0) return PAIR[core_pair_state()];
  if (strcmp(name, "speak_level") == 0) return NUM(m->speak_level);
  if (strcmp(name, "bot_name") == 0) return m->bot_name;
  if (strcmp(name, "host_name") == 0) return m->host_name;
  if (strcmp(name, "thinking.heard") == 0) return m->thinking.heard;
  if (strcmp(name, "thinking.working") == 0) return m->thinking.working;
  if (strcmp(name, "reply.text") == 0) return m->reply.text;
  if (strcmp(name, "reply.final") == 0) return BOOL(m->reply.final);
  if (strcmp(name, "reply.failed") == 0) return BOOL(m->reply.failed);
  if (strcmp(name, "reply.reason") == 0) return m->reply.reason;
  if (strcmp(name, "ask.title") == 0) return m->ask.title;
  if (strcmp(name, "ask.body") == 0) return m->ask.body;
  if (strcmp(name, "ask.n_options") == 0) return NUM(m->ask.n_options);
  if (strcmp(name, "ask.answerable") == 0) return BOOL(m->ask.answerable);
  if (strcmp(name, "card.title") == 0) return m->card.title;
  if (strcmp(name, "card.body") == 0) return m->card.body;
  if (strcmp(name, "image.w") == 0) return NUM(m->image.w);
  if (strcmp(name, "image.h") == 0) return NUM(m->image.h);
  if (strcmp(name, "toast.visible") == 0) return BOOL(m->toast.visible);
  if (strcmp(name, "toast.text") == 0) return m->toast.text;
  if (strcmp(name, "setup.step") == 0) return SETUP[m->setup.step];
  if (strcmp(name, "offline.reason") == 0) return OFFLINE[m->offline.reason];
  if (strcmp(name, "update.pct") == 0) return NUM(m->update.pct);
  if (strcmp(name, "update.phase") == 0) return UPDATE[m->update.phase];
  if (strcmp(name, "battery.pct") == 0) return NUM(m->battery.pct);
  return NULL;
#undef NUM
#undef BOOL
}

/* ---- helpers ------------------------------------------------------------------ */

static bool all_digits(const char *s) {
  if (*s == '\0') return false;
  for (; *s; s++) {
    if (*s < '0' || *s > '9') return false;
  }
  return true;
}

static void input(gadget_input_type_t type, int16_t x, int16_t y) {
  gadget_event_t ev = {.type = GADGET_EV_INPUT};
  ev.u.input.type = type;
  ev.u.input.x = x;
  ev.u.input.y = y;
  sim_post_event(&ev);
}

/* Blocking commands: true while still waiting (and not timed out). */
static bool waiting(uint64_t now, uint32_t timeout_ms) {
  if (!S.started) {
    S.started = true;
    S.deadline = now + timeout_ms;
  }
  return now < S.deadline;
}

static void next_line(void) {
  S.pc++;
  S.started = false;
}

/* src with every ${turn} replaced by the last noted turn: 0 on success,
 * -1 when no turn was noted yet, -2 when the result does not fit. */
static int expand_turn(const char *src, char *dst, size_t cap) {
  static const char VAR[] = "${turn}";
  size_t n = 0, tl = strlen(S.turn);
  while (*src != '\0') {
    if (strncmp(src, VAR, sizeof VAR - 1) == 0) {
      if (tl == 0) return -1;
      if (n + tl >= cap) return -2;
      memcpy(dst + n, S.turn, tl);
      n += tl;
      src += sizeof VAR - 1;
    } else {
      if (n + 1 >= cap) return -2;
      dst[n++] = *src++;
    }
  }
  dst[n] = '\0';
  return 0;
}

/* ---- the step ------------------------------------------------------------------ */

int sim_script_step(uint64_t now) {
  /* skip blank lines and comments */
  while (S.pc < S.n) {
    const char *p = S.lines[S.pc];
    while (*p == ' ' || *p == '\t') p++;
    if (*p != '\0' && *p != '#') break;
    S.pc++;
  }
  if (S.pc >= S.n) return 1;

  char line[LINE_BYTES];
  snprintf(line, sizeof line, "%s", S.lines[S.pc]);
  char *save = NULL;
  char *cmd = strtok_r(line, " \t", &save);
  char *rest = save; /* the raw remainder (console, net_text, model values) */
  while (rest && (*rest == ' ' || *rest == '\t')) rest++;

  if (strcmp(cmd, "wait") == 0) {
    char *ms = strtok_r(NULL, " \t", &save);
    if (ms == NULL || !all_digits(ms)) return fail("wait needs milliseconds");
    if (waiting(now, (uint32_t)strtoul(ms, NULL, 10))) return 0;
    next_line();
    return 0;
  }
  if (strcmp(cmd, "expect") == 0) {
    char *op = strtok_r(NULL, " \t", &save);
    char *t = strtok_r(NULL, " \t", &save);
    if (op == NULL || (t != NULL && !all_digits(t))) return fail("expect needs <op> [timeout_ms]");
    for (int i = S.cursor; i < S.n_ops; i++) {
      if (strcmp(S.ops[i], op) == 0) {
        S.cursor = i + 1;
        next_line();
        return 0;
      }
    }
    if (waiting(now, t ? (uint32_t)strtoul(t, NULL, 10) : DEFAULT_TIMEOUT_MS)) return 0;
    return fail("expect %s: no such frame in time", op);
  }
  if (strcmp(cmd, "model") == 0) {
    char *name = strtok_r(NULL, " \t", &save);
    char *value = save;
    while (value && (*value == ' ' || *value == '\t')) value++;
    if (name == NULL || value == NULL) return fail("model needs <field> <value> [timeout_ms]");
    char want[1024];
    snprintf(want, sizeof want, "%s", value);
    uint32_t timeout = DEFAULT_TIMEOUT_MS;
    char *last = strrchr(want, ' ');
    if (last != NULL && all_digits(last + 1)) {
      timeout = (uint32_t)strtoul(last + 1, NULL, 10);
      *last = '\0';
    }
    char num[24];
    const char *got = field(name, num, sizeof num);
    if (got == NULL) return fail("model: unknown field %s", name);
    bool hit = want[0] == '~' ? strstr(got, want + 1) != NULL : strcmp(got, want) == 0;
    if (hit) {
      next_line();
      return 0;
    }
    if (waiting(now, timeout)) return 0;
    return fail("model %s is \"%s\"", name, got);
  }
  if (strcmp(cmd, "boot") == 0) {
    char *n = strtok_r(NULL, " \t", &save);
    char *t = strtok_r(NULL, " \t", &save);
    if (n == NULL || !all_digits(n)) return fail("boot needs <n> [timeout_ms]");
    if (strtoul(n, NULL, 10) <= g_sim.boot) return fail("boot %s: already at or past that boot", n);
    if (waiting(now, t ? (uint32_t)strtoul(t, NULL, 10) : BOOT_TIMEOUT_MS)) return 0; /* the restart re-execs */
    return fail("boot %s: the simulator did not restart in time", n);
  }

  /* acting commands: one per loop iteration */
  if (strcmp(cmd, "touch") == 0) {
    char *x = strtok_r(NULL, " \t", &save), *y = strtok_r(NULL, " \t", &save);
    if (x == NULL || y == NULL) return fail("touch needs <x> <y>");
    S.px = (int16_t)atoi(x);
    S.py = (int16_t)atoi(y);
    input(S.pressed ? GADGET_IN_TOUCH_MOVE : GADGET_IN_TOUCH_DOWN, S.px, S.py);
    sim_display_touch(true, S.px, S.py);
    S.pressed = true;
  } else if (strcmp(cmd, "release") == 0) {
    input(GADGET_IN_TOUCH_UP, S.px, S.py);
    sim_display_touch(false, S.px, S.py);
    S.pressed = false;
  } else if (strcmp(cmd, "swipe") == 0) {
    static const char *const DIRS[] = {"up", "down", "left", "right"};
    char *d = strtok_r(NULL, " \t", &save);
    int dir = -1;
    for (int i = 0; d && i < 4; i++) {
      if (strcmp(d, DIRS[i]) == 0) dir = i;
    }
    if (dir < 0) return fail("swipe needs up, down, left or right");
    gadget_event_t ev = {.type = GADGET_EV_INPUT};
    ev.u.input.type = GADGET_IN_SWIPE;
    ev.u.input.dir = (gadget_swipe_dir_t)dir;
    sim_post_event(&ev);
  } else if (strcmp(cmd, "talk_down") == 0) {
    input(GADGET_IN_TALK_DOWN, 0, 0);
  } else if (strcmp(cmd, "talk_up") == 0) {
    input(GADGET_IN_TALK_UP, 0, 0);
  } else if (strcmp(cmd, "cancel") == 0) {
    input(GADGET_IN_CANCEL_DOWN, 0, 0);
    input(GADGET_IN_CANCEL_UP, 0, 0);
  } else if (strcmp(cmd, "console") == 0) {
    gadget_event_t ev = {.type = GADGET_EV_CONSOLE_LINE};
    ev.u.console.line = rest ? rest : "";
    sim_post_event(&ev);
  } else if (strcmp(cmd, "snapshot") == 0) {
    char *name = strtok_r(NULL, " \t", &save);
    if (name == NULL) return fail("snapshot needs <name>");
    int r = sim_display_snapshot(name);
    if (r == -1) fprintf(stderr, "skip snapshot %s\n", name);
    else if (r == 0) return fail("snapshot %s differs (see %s_err.png)", name, name);
    else if (r == 2) return fail("snapshot %s has no reference image", name);
  } else if (strncmp(cmd, "net_", 4) == 0) {
    if (!sim_net_scripted()) return fail("%s needs --host script", cmd);
    bool ok = false;
    if (strcmp(cmd, "net_open") == 0) {
      char *t = strtok_r(NULL, " \t", &save);
      if (t != NULL && !all_digits(t)) return fail("net_open needs [timeout_ms]");
      /* blocks until the gadget has asked for a connection */
      if (!sim_net_script_open()) {
        if (waiting(now, t ? (uint32_t)strtoul(t, NULL, 10) : DEFAULT_TIMEOUT_MS)) return 0;
        return fail("net_open: the gadget did not connect in time");
      }
      ok = true;
    } else if (strcmp(cmd, "net_text") == 0) {
      static char json[GADGET_TEXT_FRAME_MAX + 1];
      int e = rest != NULL ? expand_turn(rest, json, sizeof json) : -2;
      if (e == -1) return fail("net_text: no turn yet");
      if (e != 0) return fail("net_text needs <json> of at most 16 KiB");
      ok = sim_net_script_text(json);
    } else if (strcmp(cmd, "net_binary") == 0) {
      static uint8_t bin[GADGET_BINARY_FRAME_MAX];
      size_t len = 0;
      char *hex = strtok_r(NULL, " \t", &save);
      if (hex == NULL || gadget_hex_decode(hex, bin, sizeof bin, &len) != GADGET_OK) {
        return fail("net_binary needs lowercase hex");
      }
      ok = sim_net_script_binary(bin, len);
    } else if (strcmp(cmd, "net_close") == 0) {
      char *code = strtok_r(NULL, " \t", &save);
      ok = sim_net_script_close(code ? (uint16_t)atoi(code) : 1006);
    } else {
      return fail("unknown command %s", cmd);
    }
    if (!ok) return fail("%s: no connection is open", cmd);
  } else if (strcmp(cmd, "battery") == 0) {
    char *pct = strtok_r(NULL, " \t", &save);
    char *ch = strtok_r(NULL, " \t", &save);
    if (pct == NULL || !all_digits(pct)) return fail("battery needs <pct> [charging]");
    sim_battery_set((uint8_t)atoi(pct), ch != NULL && strcmp(ch, "charging") == 0);
  } else {
    return fail("unknown command %s", cmd);
  }
  next_line();
  return 0;
}
