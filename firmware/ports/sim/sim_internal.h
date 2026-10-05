/* firmware/ports/sim/sim_internal.h */
/* SPDX-License-Identifier: Apache-2.0 */
/* Private to the simulator: options and the calls between its files.
 * The seams other plans use are sim_hal.h and sim_display.h. */
#ifndef SIM_INTERNAL_H
#define SIM_INTERNAL_H

#include <stdio.h>
#include "gadget_board.h"
#include "gadget_core.h"

typedef struct {
  const gadget_board_t *board;
  const char *name;            /* --name, NULL = "default" */
  char state_dir[1024];        /* --state-dir or ~/.openmausbot-gadget/sim/<name> */
  const char *host;            /* --host; "script" = the scripted network */
  const char *pair;            /* --pair (boot 0 only) */
  unsigned boot;               /* --boot */
  bool headless;
  const char *script;
  const char *mic_file, *spk_file;
  uint32_t seed;
  bool seed_set;
  uint8_t battery_pct;
  bool battery_charging;
  float zoom;
  const char *snapshot_dir;
  bool fail_probation;
  uint32_t probation_ms;
  bool trace;
  int argc;                    /* the original command line, for restarts */
  char **argv;
} sim_args_t;

extern sim_args_t g_sim;

/* sim_args.c: 0 = run, 1 = printed --help/--version (exit 0), 2 = usage error */
int sim_args_parse(int argc, char **argv, sim_args_t *out);

/* sim_events.c: deliver everything sim_post_event() queued, in order. */
void sim_events_deliver(void);

/* sim_storage.c: load <dir>/storage.json (creating <dir>); 0 on success. */
int sim_storage_open(const char *dir);
/* Write text to path through a temp file and rename(); 0 on success. */
int sim_write_atomic(const char *path, const char *text);

/* sim_ota.c: load <dir>/otadata.json, finish a pending rollback, and write the
 * running image's version into fw (GADGET_SIM_VERSION before any OTA). */
int sim_ota_open(const char *dir, char *fw, size_t cap);

/* sim_ws.c: true when --host script selected the scripted network. */
bool sim_net_scripted(void);
/* Service the socket, waiting up to wait_ms for it (0 = just poll). */
void sim_net_poll(uint32_t wait_ms);
/* sim_net_script.c: the script's net_* commands; false when no connection is pending. */
bool sim_net_script_open(void);
bool sim_net_script_text(const char *json);
bool sim_net_script_binary(const uint8_t *data, size_t len);
bool sim_net_script_close(uint16_t code);
gadget_status_t sim_net_script_ws_open(void);
gadget_status_t sim_net_script_ws_send(void);
void sim_net_script_ws_close(uint16_t code);

/* sim_mdns.c */
void sim_mdns_poll(void);

/* sim_console.c: read stdin without blocking and post whole lines. */
void sim_console_poll(void);

/* sim_wifi.c: post the initial GADGET_WIFI_CONNECTED. */
void sim_wifi_start(void);

/* sim_battery.c */
void sim_battery_set(uint8_t pct, bool charging);

/* sim_audio.c: the selected backend's pump(now). */
void sim_audio_pump(uint64_t now_ms);

/* sim_script.c: 0 on success; prints the reason and returns non-zero otherwise. */
int sim_script_load(const char *path, unsigned boot);
/* One step: 0 = keep going, 1 = the script finished, -1 = it failed. */
int sim_script_step(uint64_t now_ms);
/* Every text frame op that crossed the socket (from core's tap). */
void sim_script_record(const char *op);
/* The turn of the gadget's last voice.begin or say (from core's tap): net_text's ${turn}. */
void sim_script_note_turn(const char *turn);

/* sim_system.c: re-exec with --boot <n+1>, dropping --pair (contract §2.16). */
_Noreturn void sim_restart(void);

#endif /* SIM_INTERNAL_H */
