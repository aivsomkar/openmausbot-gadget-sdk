/* firmware/ports/sim/main.c */
/* SPDX-License-Identifier: Apache-2.0 */
/* gadget-sim: the same core on the desktop (spec §5.7, contract §2.16).
 * Headless runs advance a virtual clock 10 ms per loop iteration; with a
 * real socket each iteration also waits for real time to catch up, so the
 * virtual clock tracks the host's. Window mode (plan P2b) uses the real clock. */
#include <stdlib.h>
#include <string.h>
#include "cJSON.h"
#include "gadget_core.h"
#include "gadget_hal.h"
#include "gadget_ui.h"
#include "psa/crypto.h"
#include "sim_display.h"
#include "sim_hal.h"
#include "sim_internal.h"

static void on_tap(core_tap_dir_t dir, const char *op, const char *json, size_t len, void *ctx) {
  (void)ctx;
  sim_script_record(op);
  if (dir == CORE_TAP_TX && (strcmp(op, "voice.begin") == 0 || strcmp(op, "say") == 0)) {
    cJSON *o = cJSON_ParseWithLength(json, len);
    const cJSON *turn = cJSON_GetObjectItemCaseSensitive(o, "turn");
    if (cJSON_IsString(turn)) sim_script_note_turn(turn->valuestring); /* the script's ${turn} */
    cJSON_Delete(o);
  }
  if (g_sim.trace) fprintf(stderr, "%s %.*s\n", dir == CORE_TAP_TX ? ">>" : "<<", (int)len, json);
}

static void feed_console(const char *line) {
  gadget_event_t ev = {.type = GADGET_EV_CONSOLE_LINE};
  ev.u.console.line = line;
  sim_post_event(&ev);
}

static const sim_audio_backend_t *pick_audio(void) {
  if (g_sim.headless || g_sim.mic_file || g_sim.spk_file) {
    return sim_audio_file_backend(g_sim.mic_file, g_sim.spk_file);
  }
#if defined(GADGET_WITH_SDL)
  return sim_audio_sdl_backend();
#else
  return sim_audio_file_backend(NULL, NULL);
#endif
}

int main(int argc, char **argv) {
  int rc = sim_args_parse(argc, argv, &g_sim);
  if (rc == 1) return 0;
  if (rc != 0) return 2;
  /* read the script first: a script that cannot be read writes no state */
  if (g_sim.script != NULL && sim_script_load(g_sim.script, g_sim.boot) != 0) return 1;
  char fw[GADGET_VERSION_MAX + 1];
  if (sim_storage_open(g_sim.state_dir) != 0 || sim_ota_open(g_sim.state_dir, fw, sizeof fw) != 0) return 3;
  if (psa_crypto_init() != PSA_SUCCESS) {
    fprintf(stderr, "gadget-sim: psa_crypto_init failed\n");
    return 3;
  }
  const sim_audio_backend_t *audio = pick_audio();
  if (audio == NULL) return 3;
  sim_audio_use(audio);
  sim_display_opts_t dopts = {.headless = g_sim.headless, .zoom = g_sim.zoom, .snapshot_dir = g_sim.snapshot_dir};
  if (sim_display_init(g_sim.board, &dopts) != 0) return 3;

  core_config_t cfg = {.board = g_sim.board, .fw_version = fw, .prng_seed = g_sim.seed,
                       .fail_probation = g_sim.fail_probation, .default_name = g_sim.name,
                       .probation_ms = g_sim.probation_ms};
  core_set_tap(on_tap, NULL);
  if (core_init(&cfg) != GADGET_OK) {
    fprintf(stderr, "gadget-sim: core_init failed\n");
    return 3;
  }
  sim_wifi_start();
  if (g_sim.host != NULL) {
    char line[128];
    snprintf(line, sizeof line, "host %s", g_sim.host);
    feed_console(line);
  }
  if (g_sim.pair != NULL && g_sim.boot == 0) {
    char line[64];
    snprintf(line, sizeof line, "pair %s", g_sim.pair);
    feed_console(line);
  }
  if (ui_init(g_sim.board, g_sim.seed) != GADGET_OK) return 3;
  if (!g_sim.headless) fprintf(stderr, "gadget-sim: %s on the real clock; type console commands here\n", g_sim.board->id);

  bool paced = !g_sim.headless || (g_sim.host != NULL && !sim_net_scripted());
  uint64_t t0 = hal_now_ms(), vt = 0;
  for (;;) {
    vt += 10;
    if (paced) {
      /* wait (servicing the socket) until real time reaches this tick */
      for (;;) {
        uint64_t real = hal_now_ms() - t0;
        if (real >= vt) break;
        sim_net_poll((uint32_t)(vt - real));
      }
    } else {
      sim_net_poll(0);
    }
    uint64_t now = g_sim.headless ? vt : hal_now_ms() - t0 + 10;
    if (g_sim.headless) sim_display_advance(10);
    else sim_display_poll();
    sim_mdns_poll();
    sim_console_poll();
    sim_audio_pump(now);
    sim_events_deliver();
    core_tick(now);
    ui_render(core_ui_model());
    ui_tick(now);
    if (g_sim.script != NULL) {
      int s = sim_script_step(now);
      if (s > 0) {
        fprintf(stderr, "gadget-sim: script passed\n");
        break;
      }
      if (s < 0) {
        sim_display_deinit();
        return 1;
      }
    }
    if (sim_display_quit_requested()) break;
  }
  ui_deinit();
  sim_display_deinit();
  core_deinit();
  return 0;
}
