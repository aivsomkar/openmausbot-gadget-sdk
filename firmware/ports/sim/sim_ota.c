/* firmware/ports/sim/sim_ota.c */
/* SPDX-License-Identifier: Apache-2.0 */
/* OTA in the simulator (spec §5.7, contract §4.6): images are opaque bytes
 * written to slot0.bin / slot1.bin; otadata.json records
 *   {"active": 0|1, "state": "valid"|"pending"|"invalid", "version": "<fw>",
 *    "previous": {"slot": 0|1, "version": "<fw>"}, "booted": true}
 * ("booted" is this file's own addition: a pending image that boots a second
 * time without being confirmed rolls back, like a crash on the device). */
#include <stdlib.h>
#include <string.h>
#include "cJSON.h"
#include "gadget_hal.h"
#include "sim_hal.h"
#include "sim_internal.h"

#ifndef GADGET_SIM_VERSION
#define GADGET_SIM_VERSION "0.0.0-dev"
#endif

static struct {
  char dir[1024];
  int active;
  char state[16];
  char version[GADGET_VERSION_MAX + 1];
  bool has_prev;
  int prev_slot;
  char prev_version[GADGET_VERSION_MAX + 1];
  bool booted;
  hal_ota_img_state_t running;
  FILE *slot;
  int target;
  uint32_t size, written;
} O;

static void save(void) {
  cJSON *o = cJSON_CreateObject();
  cJSON_AddNumberToObject(o, "active", O.active);
  cJSON_AddStringToObject(o, "state", O.state);
  cJSON_AddStringToObject(o, "version", O.version);
  if (O.has_prev) {
    cJSON *p = cJSON_AddObjectToObject(o, "previous");
    cJSON_AddNumberToObject(p, "slot", O.prev_slot);
    cJSON_AddStringToObject(p, "version", O.prev_version);
  }
  if (O.booted) cJSON_AddTrueToObject(o, "booted");
  char *text = cJSON_Print(o);
  cJSON_Delete(o);
  char path[1100];
  snprintf(path, sizeof path, "%s/otadata.json", O.dir);
  if (text == NULL || sim_write_atomic(path, text) != 0) fprintf(stderr, "gadget-sim: cannot write %s\n", path);
  cJSON_free(text);
}

/* <dir>/slot<n>.bin, or its temp name while the image is written (contract §4.6). */
static void slot_path(char *out, size_t cap, int slot, bool tmp) {
  snprintf(out, cap, "%s/slot%d.bin%s", O.dir, slot, tmp ? ".tmp" : "");
}

static void roll_back(void) {
  fprintf(stderr, "gadget-sim: image %s was not confirmed; back to %s\n", O.version,
          O.has_prev ? O.prev_version : GADGET_SIM_VERSION);
  int bad_slot = O.active;
  char bad_version[GADGET_VERSION_MAX + 1];
  snprintf(bad_version, sizeof bad_version, "%s", O.version);
  O.active = O.has_prev ? O.prev_slot : 0;
  snprintf(O.version, sizeof O.version, "%s", O.has_prev ? O.prev_version : GADGET_SIM_VERSION);
  O.prev_slot = bad_slot;
  snprintf(O.prev_version, sizeof O.prev_version, "%s", bad_version);
  O.has_prev = true;
  snprintf(O.state, sizeof O.state, "valid");
  O.booted = false;
  save();
}

int sim_ota_open(const char *dir, char *fw, size_t cap) {
  memset(&O, 0, sizeof O);
  snprintf(O.dir, sizeof O.dir, "%s", dir);
  snprintf(O.state, sizeof O.state, "valid");
  snprintf(O.version, sizeof O.version, "%s", GADGET_SIM_VERSION);
  char path[1100];
  snprintf(path, sizeof path, "%s/otadata.json", dir);
  FILE *f = fopen(path, "rb");
  if (f != NULL) {
    char buf[1024];
    size_t n = fread(buf, 1, sizeof buf - 1, f);
    fclose(f);
    buf[n] = '\0';
    cJSON *o = cJSON_Parse(buf);
    const cJSON *a = cJSON_GetObjectItemCaseSensitive(o, "active");
    const cJSON *s = cJSON_GetObjectItemCaseSensitive(o, "state");
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, "version");
    const cJSON *p = cJSON_GetObjectItemCaseSensitive(o, "previous");
    if (cJSON_IsNumber(a) && cJSON_IsString(s) && cJSON_IsString(v)) {
      O.active = a->valueint == 1 ? 1 : 0;
      snprintf(O.state, sizeof O.state, "%s", s->valuestring);
      snprintf(O.version, sizeof O.version, "%s", v->valuestring);
      O.booted = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(o, "booted"));
      const cJSON *ps = cJSON_GetObjectItemCaseSensitive(p, "slot");
      const cJSON *pv = cJSON_GetObjectItemCaseSensitive(p, "version");
      if (cJSON_IsNumber(ps) && cJSON_IsString(pv)) {
        O.has_prev = true;
        O.prev_slot = ps->valueint == 1 ? 1 : 0;
        snprintf(O.prev_version, sizeof O.prev_version, "%s", pv->valuestring);
      }
    }
    cJSON_Delete(o);
  }
  O.running = HAL_OTA_IMG_VALID;
  if (strcmp(O.state, "invalid") == 0 || (strcmp(O.state, "pending") == 0 && O.booted)) {
    roll_back(); /* the bootloader's job on the device */
  } else if (strcmp(O.state, "pending") == 0) {
    O.booted = true;
    O.running = HAL_OTA_IMG_PENDING_VERIFY;
    save();
  }
  snprintf(fw, cap, "%s", O.version);
  return 0;
}

gadget_status_t hal_ota_begin(uint32_t size) {
  if (size == 0 || size > g_sim.board->ota_max) return GADGET_ERR_LIMIT;
  hal_ota_abort(); /* an unfinished earlier image */
  O.target = 1 - O.active;
  char path[1100];
  slot_path(path, sizeof path, O.target, true);
  O.slot = fopen(path, "wb");
  if (O.slot == NULL) return GADGET_ERR_IO;
  O.size = size;
  O.written = 0;
  return GADGET_OK;
}

gadget_status_t hal_ota_write(uint32_t offset, const uint8_t *data, size_t len) {
  if (O.slot == NULL || offset != O.written || offset + len > O.size) return GADGET_ERR_STATE;
  gadget_event_t ev;
  if (fwrite(data, 1, len, O.slot) != len) {
    ev.type = GADGET_EV_OTA_ERROR;
    ev.u.ota_error.err = GADGET_ERR_IO;
  } else {
    O.written += (uint32_t)len;
    ev.type = GADGET_EV_OTA_WRITTEN;
    ev.u.ota_written.written = O.written;
  }
  sim_post_event(&ev);
  return GADGET_OK;
}

gadget_status_t hal_ota_finalize(void) {
  if (O.slot == NULL || O.written != O.size) return GADGET_ERR_STATE;
  int rc = fclose(O.slot);
  O.slot = NULL;
  char tmp[1100], path[1100];
  slot_path(tmp, sizeof tmp, O.target, true);
  slot_path(path, sizeof path, O.target, false);
  if (rc != 0 || rename(tmp, path) != 0) {
    remove(tmp);
    return GADGET_ERR_IO;
  }
  return GADGET_OK;
}

gadget_status_t hal_ota_set_boot(const char *version) {
  O.prev_slot = O.active;
  snprintf(O.prev_version, sizeof O.prev_version, "%s", O.version);
  O.has_prev = true;
  O.active = O.target;
  snprintf(O.version, sizeof O.version, "%s", version);
  snprintf(O.state, sizeof O.state, "pending");
  O.booted = false;
  save();
  return GADGET_OK;
}

void hal_ota_abort(void) {
  if (O.slot != NULL) {
    fclose(O.slot);
    char tmp[1100];
    slot_path(tmp, sizeof tmp, O.target, true);
    remove(tmp);
  }
  O.slot = NULL;
  O.size = O.written = 0;
}

hal_ota_img_state_t hal_ota_running_state(void) { return O.running; }

gadget_status_t hal_ota_mark_valid(void) {
  snprintf(O.state, sizeof O.state, "valid");
  O.booted = false;
  O.running = HAL_OTA_IMG_VALID;
  save();
  return GADGET_OK;
}

_Noreturn void hal_ota_mark_invalid_and_reboot(void) {
  snprintf(O.state, sizeof O.state, "invalid");
  save();
  fprintf(stderr, "gadget-sim: probation failed; restarting into the previous image\n");
  sim_restart();
}
