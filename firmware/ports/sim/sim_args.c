/* firmware/ports/sim/sim_args.c */
/* SPDX-License-Identifier: Apache-2.0 */
/* gadget-sim command line (contract §2.16). */
#include <stdlib.h>
#include <string.h>
#include "sim_internal.h"

#ifndef GADGET_SIM_VERSION
#define GADGET_SIM_VERSION "0.0.0-dev"
#endif

sim_args_t g_sim;

static void usage(FILE *f) {
  fprintf(f,
          "usage: gadget-sim --board <id> [options]\n"
          "  --board <id>            amoled-175c, amoled-175, lcd-154 or devkit (required)\n"
          "  --name <name>           state slot ~/.openmausbot-gadget/sim/<name>/ (default: default)\n"
          "  --state-dir <dir>       use this state folder instead\n"
          "  --host <addr[:port]>    same as the console command host <addr>; --host script = scripted network\n"
          "  --pair <code>           same as the console command pair <code> (first boot only)\n"
          "  --headless              no window: virtual clock, null display (requires --script)\n"
          "  --script <file>         run a script; exit 0 at its end, 1 on the first failure\n"
          "  --mic-file <wav>        16 kHz mono PCM16 WAV played into the mic on each TALK hold\n"
          "  --speaker-file <wav>    write everything played to this WAV\n"
          "  --seed <u32>            PRNG seed (default 1 with --headless, else random)\n"
          "  --battery <pct>[:charging]  simulated battery (default 100)\n"
          "  --zoom <n>              window zoom\n"
          "  --snapshot-dir <dir>    default firmware/tests/snapshots/<board>\n"
          "  --fail-probation        test only: ignore the first ready on a new image\n"
          "  --probation-ms <ms>     test only: probation length\n"
          "  --trace                 print every text frame on stderr\n"
          "  --boot <n>              set by the simulator's own restarts\n"
          "  --version, --help\n");
}

static bool parse_u32(const char *s, uint32_t *out) {
  if (s == NULL || *s == '\0') return false;
  char *end = NULL;
  unsigned long v = strtoul(s, &end, 10);
  if (*end != '\0' || v > 0xFFFFFFFFul) return false;
  *out = (uint32_t)v;
  return true;
}

int sim_args_parse(int argc, char **argv, sim_args_t *a) {
  memset(a, 0, sizeof *a);
  a->argc = argc;
  a->argv = argv;
  a->battery_pct = 100;
  a->zoom = 0.0f;
  const char *board = NULL, *state_dir = NULL;
  for (int i = 1; i < argc; i++) {
    const char *k = argv[i];
    const char *v = i + 1 < argc ? argv[i + 1] : NULL;
#define TAKE()                                                    \
  do {                                                            \
    if (v == NULL) {                                              \
      fprintf(stderr, "gadget-sim: %s needs a value\n", k);       \
      return 2;                                                   \
    }                                                             \
    i++;                                                          \
  } while (0)
    if (strcmp(k, "--help") == 0) {
      usage(stdout);
      return 1;
    } else if (strcmp(k, "--version") == 0) {
      printf("gadget-sim %s\n", GADGET_SIM_VERSION);
      return 1;
    } else if (strcmp(k, "--board") == 0) {
      TAKE();
      board = v;
    } else if (strcmp(k, "--name") == 0) {
      TAKE();
      a->name = v;
    } else if (strcmp(k, "--state-dir") == 0) {
      TAKE();
      state_dir = v;
    } else if (strcmp(k, "--host") == 0) {
      TAKE();
      a->host = v;
    } else if (strcmp(k, "--pair") == 0) {
      TAKE();
      a->pair = v;
    } else if (strcmp(k, "--boot") == 0) {
      TAKE();
      uint32_t n;
      if (!parse_u32(v, &n)) goto bad;
      a->boot = n;
    } else if (strcmp(k, "--headless") == 0) {
      a->headless = true;
    } else if (strcmp(k, "--script") == 0) {
      TAKE();
      a->script = v;
    } else if (strcmp(k, "--mic-file") == 0) {
      TAKE();
      a->mic_file = v;
    } else if (strcmp(k, "--speaker-file") == 0) {
      TAKE();
      a->spk_file = v;
    } else if (strcmp(k, "--seed") == 0) {
      TAKE();
      if (!parse_u32(v, &a->seed)) goto bad;
      a->seed_set = true;
    } else if (strcmp(k, "--battery") == 0) {
      TAKE();
      char tmp[32];
      snprintf(tmp, sizeof tmp, "%s", v);
      char *colon = strchr(tmp, ':');
      a->battery_charging = colon != NULL && strcmp(colon + 1, "charging") == 0;
      if (colon) *colon = '\0';
      uint32_t pct;
      if (!parse_u32(tmp, &pct) || pct > 100 || (colon && !a->battery_charging)) goto bad;
      a->battery_pct = (uint8_t)pct;
    } else if (strcmp(k, "--zoom") == 0) {
      TAKE();
      a->zoom = (float)atof(v);
    } else if (strcmp(k, "--snapshot-dir") == 0) {
      TAKE();
      a->snapshot_dir = v;
    } else if (strcmp(k, "--fail-probation") == 0) {
      a->fail_probation = true;
    } else if (strcmp(k, "--probation-ms") == 0) {
      TAKE();
      if (!parse_u32(v, &a->probation_ms)) goto bad;
    } else if (strcmp(k, "--trace") == 0) {
      a->trace = true;
    } else {
      fprintf(stderr, "gadget-sim: unknown option %s\n", k);
      usage(stderr);
      return 2;
    }
    continue;
  bad:
    fprintf(stderr, "gadget-sim: bad value for %s: %s\n", k, v);
    return 2;
#undef TAKE
  }
  a->board = gadget_board_by_id(board);
  if (a->board == NULL) {
    fprintf(stderr, "gadget-sim: --board must be amoled-175c, amoled-175, lcd-154 or devkit\n");
    return 2;
  }
  if (a->headless && a->script == NULL) {
    fprintf(stderr, "gadget-sim: --headless requires --script\n");
    return 2;
  }
  if (a->zoom <= 0.0f) a->zoom = a->board->screen_w <= 320 ? 2.0f : 1.0f;
  if (!a->seed_set) a->seed = a->headless ? 1u : 0u; /* 0: main() draws one random seed for core and the UI */
  if (a->snapshot_dir == NULL) {
    static char snap[256];
    snprintf(snap, sizeof snap, "firmware/tests/snapshots/%s", a->board->id);
    a->snapshot_dir = snap;
  }
  if (state_dir != NULL) {
    snprintf(a->state_dir, sizeof a->state_dir, "%s", state_dir);
  } else {
    const char *home = getenv("HOME");
    snprintf(a->state_dir, sizeof a->state_dir, "%s/.openmausbot-gadget/sim/%s", home ? home : ".",
             a->name ? a->name : "default");
  }
  return 0;
}
