/* firmware/ports/sim/sim_system.c */
/* SPDX-License-Identifier: Apache-2.0 */
/* Clock, log, console output and restarts for the simulator. */
#include <errno.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif
#include "gadget_hal.h"
#include "sim_display.h"
#include "sim_internal.h"

static bool s_log = true;

uint64_t hal_now_ms(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
}

void hal_vlog(gadget_log_level_t level, const char *tag, const char *fmt, va_list ap) {
  if (!s_log) return;
  static const char L[] = "EWID";
  fprintf(stderr, "[%c] %s: ", L[level <= GADGET_LOG_DEBUG ? level : GADGET_LOG_DEBUG], tag);
  vfprintf(stderr, fmt, ap);
  fputc('\n', stderr);
}

void hal_log(gadget_log_level_t level, const char *tag, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  hal_vlog(level, tag, fmt, ap);
  va_end(ap);
}

void hal_log_set_enabled(bool enabled) { s_log = enabled; }

void hal_console_write(const char *line) {
  fputs(line, stdout);
  fputc('\n', stdout);
  fflush(stdout);
}

static bool self_path(char *buf, size_t cap) {
#if defined(__APPLE__)
  uint32_t size = (uint32_t)cap;
  return _NSGetExecutablePath(buf, &size) == 0;
#else
  ssize_t n = readlink("/proc/self/exe", buf, cap - 1);
  if (n <= 0) return false;
  buf[n] = '\0';
  return true;
#endif
}

_Noreturn void sim_restart(void) {
  char path[4096];
  static char boot[16];
  snprintf(boot, sizeof boot, "%u", g_sim.boot + 1);
  char **argv = calloc((size_t)g_sim.argc + 3, sizeof *argv);
  int n = 0;
  argv[n++] = g_sim.argv[0];
  for (int i = 1; i < g_sim.argc; i++) {
    if (strcmp(g_sim.argv[i], "--pair") == 0 || strcmp(g_sim.argv[i], "--boot") == 0) {
      i++; /* drop the option and its value */
      continue;
    }
    argv[n++] = g_sim.argv[i];
  }
  argv[n++] = "--boot";
  argv[n++] = boot;
  argv[n] = NULL;
  sim_display_deinit();
  fflush(stdout);
  fflush(stderr);
  if (!self_path(path, sizeof path)) {
    fprintf(stderr, "gadget-sim: cannot find my own path to restart\n");
    exit(3);
  }
  execv(path, argv);
  fprintf(stderr, "gadget-sim: restart failed: %s\n", strerror(errno));
  exit(3);
}

_Noreturn void hal_restart(void) {
  fprintf(stderr, "gadget-sim: restarting (boot %u)\n", g_sim.boot + 1);
  sim_restart();
}
