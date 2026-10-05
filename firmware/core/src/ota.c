/* firmware/core/src/ota.c */
/* SPDX-License-Identifier: Apache-2.0 */
/* Firmware updates (spec §4.8, contract §2.13): the offer checks, chunk
 * flow with progress, commit with size + SHA-256, and the firmware's own
 * probation timer. The same code runs on the ESP32 and in the simulator. */
#include <stdio.h>
#include <string.h>
#include "core_internal.h"
#include "gadget_ota.h"

#define TAG "ota"
#define RESTART_DELAY_MS 1000u

static struct {
  gadget_ota_state_t st;
  uint8_t stream;
  uint32_t size, received, written, reported;
  char version[GADGET_VERSION_MAX + 1];
  uint8_t sha[GADGET_SHA256_LEN];
  hal_sha256_t hash;
  uint64_t deadline;
  bool commit_pending;
  uint64_t restart_at;
  bool probation;          /* this boot is a new image on probation */
  bool armed;              /* the probation deadline is set (first tick) */
  bool validated;
  bool ignored_first_ready; /* --fail-probation */
  uint64_t probation_deadline;
} O;

const gadget_release_key_t *gadget_key_find(const char *key_id) {
  if (key_id == NULL) return NULL;
  for (size_t i = 0; i < gadget_release_keys_count; i++) {
    if (gadget_release_keys[i].id != NULL && strcmp(gadget_release_keys[i].id, key_id) == 0) {
      return &gadget_release_keys[i];
    }
  }
  for (size_t i = 0; i < gadget_test_keys_count; i++) {
    if (gadget_test_keys[i].id != NULL && strcmp(gadget_test_keys[i].id, key_id) == 0) return &gadget_test_keys[i];
  }
  return NULL;
}

gadget_ota_state_t core_ota_state(void) { return O.st; }

static void send_fail(uint8_t stream, const char *code) {
  hal_log(GADGET_LOG_WARN, TAG, "update failed: %s", code);
  gp_fw_fail_t f = {.stream = stream, .code = code};
  session_send("fw.fail", g_core_tx, gp_encode_fw_fail(g_core_tx, sizeof g_core_tx, &f));
}

static void reset(void) {
  hal_crypto_sha256_abort(&O.hash);
  O.st = GADGET_OTA_IDLE;
  O.commit_pending = false;
}

static void fail(const char *code) {
  send_fail(O.stream, code);
  hal_ota_abort();
  reset();
}

/* The offer checks, in the contract's order. NULL when the offer is fine. */
static const char *check_offer(const gp_fw_offer_t *o) {
  if (strcmp(o->board, g_core.board->id) != 0) return GADGET_FW_WRONG_BOARD;
  if (strcmp(o->version, g_core.fw) == 0) return GADGET_FW_SAME_VERSION;
  if (o->size > g_core.board->ota_max) return GADGET_FW_TOO_LARGE;
  const gadget_release_key_t *key = gadget_key_find(o->key_id);
  if (key == NULL) return GADGET_FW_UNKNOWN_KEY;
  size_t n = 0;
  uint8_t der[GADGET_SIG_DER_MAX + 8];
  size_t der_len = 0;
  char text[200];
  int tn = gp_firmware_text(text, sizeof text, g_core.board->id, o->version, o->size, o->sha256);
  if (strlen(o->sha256) != 64 || gadget_hex_decode(o->sha256, O.sha, sizeof O.sha, &n) != GADGET_OK || n != 32 ||
      gadget_b64_decode(o->sig, der, sizeof der, &der_len) != GADGET_OK || tn < 0 ||
      hal_crypto_verify(key->pub, (const uint8_t *)text, (size_t)tn, der, der_len) != GADGET_OK) {
    return GADGET_FW_BAD_SIG;
  }
  return NULL;
}

static void on_offer(const gp_fw_offer_t *o) {
  /* never cut off a person who is talking or waiting for an answer */
  if (O.st != GADGET_OTA_IDLE || g_core.f.recording || interaction_turn_in_flight()) {
    send_fail(o->stream, GADGET_FW_BUSY);
    return;
  }
  O.stream = o->stream;
  const char *code = check_offer(o);
  if (code == NULL && strlen(o->version) > GADGET_VERSION_MAX) code = GADGET_FW_FLASH;
  if (code == NULL && hal_ota_begin(o->size) != GADGET_OK) code = GADGET_FW_FLASH;
  if (code != NULL) {
    send_fail(o->stream, code);
    return;
  }
  if (hal_crypto_sha256_begin(&O.hash) != GADGET_OK) {
    fail(GADGET_FW_FLASH);
    return;
  }
  snprintf(O.version, sizeof O.version, "%s", o->version);
  O.size = o->size;
  O.received = O.written = O.reported = 0;
  O.commit_pending = false;
  O.deadline = g_core.now + GADGET_FW_CHUNK_TIMEOUT_MS;
  O.st = GADGET_OTA_RECEIVING;
  hal_log(GADGET_LOG_INFO, TAG, "receiving %s (%u bytes)", O.version, (unsigned)O.size);
  gp_fw_ready_t r = {.stream = O.stream};
  session_send("fw.ready", g_core_tx, gp_encode_fw_ready(g_core_tx, sizeof g_core_tx, &r));
}

static void do_commit(void) {
  O.st = GADGET_OTA_FINALIZING;
  uint8_t got[GADGET_SHA256_LEN];
  if (O.received != O.size) {
    fail(GADGET_FW_CHECKSUM); /* contract §2.13: size and SHA-256 are the commit's checks */
    return;
  }
  if (hal_crypto_sha256_finish(&O.hash, got) != GADGET_OK || memcmp(got, O.sha, sizeof got) != 0) {
    fail(GADGET_FW_CHECKSUM);
    return;
  }
  if (hal_ota_finalize() != GADGET_OK || hal_ota_set_boot(O.version) != GADGET_OK) {
    fail(GADGET_FW_FLASH);
    return;
  }
  hal_log(GADGET_LOG_INFO, TAG, "%s installed; restarting", O.version);
  O.st = GADGET_OTA_RESTARTING;
  O.restart_at = g_core.now + RESTART_DELAY_MS;
}

bool ota_on_msg(const gp_msg_t *m) {
  switch (m->op) {
    case GP_OP_FW_OFFER:
      on_offer(&m->m.fw_offer);
      return true;
    case GP_OP_FW_COMMIT:
      if (O.st == GADGET_OTA_IDLE || m->m.fw_commit.stream != O.stream) return true;
      if (O.st == GADGET_OTA_WAIT_COMMIT) {
        do_commit();
      } else if (O.st == GADGET_OTA_RECEIVING && O.received == O.size) {
        O.commit_pending = true; /* the last writes are still on their way to flash */
      } else if (O.st == GADGET_OTA_RECEIVING) {
        fail(GADGET_FW_CHECKSUM); /* a commit before all the offered bytes arrived */
      }
      return true;
    default:
      return false;
  }
}

void ota_on_binary(uint8_t stream, const uint8_t *payload, size_t len) {
  if (O.st != GADGET_OTA_RECEIVING || stream != O.stream) return;
  uint32_t offset;
  const uint8_t *data;
  size_t n;
  if (gp_fw_chunk_decode(payload, len, &offset, &data, &n) != GADGET_OK || offset != O.received ||
      (uint64_t)O.received + n > O.size) {
    fail(GADGET_FW_SEQUENCE);
    return;
  }
  if (hal_ota_write(offset, data, n) != GADGET_OK) {
    fail(GADGET_FW_FLASH);
    return;
  }
  hal_crypto_sha256_update(&O.hash, data, n);
  O.received += (uint32_t)n;
  O.deadline = g_core.now + (O.received == O.size ? GADGET_FW_COMMIT_TIMEOUT_MS : GADGET_FW_CHUNK_TIMEOUT_MS);
}

void ota_event(const gadget_event_t *ev) {
  if (O.st != GADGET_OTA_RECEIVING) return;
  if (ev->type == GADGET_EV_OTA_ERROR) {
    fail(GADGET_FW_FLASH);
    return;
  }
  uint32_t w = ev->u.ota_written.written;
  if (w <= O.written) return;
  O.written = w;
  bool boundary = w / GADGET_FW_PROGRESS_EVERY > O.reported / GADGET_FW_PROGRESS_EVERY;
  if (boundary || w == O.size) {
    gp_fw_progress_t p = {.stream = O.stream, .offset = w};
    session_send("fw.progress", g_core_tx, gp_encode_fw_progress(g_core_tx, sizeof g_core_tx, &p));
    O.reported = w;
  }
  if (w == O.size) {
    O.st = GADGET_OTA_WAIT_COMMIT;
    if (O.commit_pending) do_commit();
  }
}

void ota_on_ready(void) {
  if (!O.probation || O.validated) return;
  if (g_core.cfg.fail_probation && !O.ignored_first_ready) {
    O.ignored_first_ready = true;
    hal_log(GADGET_LOG_WARN, TAG, "test: ignoring the first ready, so probation runs out");
    return;
  }
  hal_ota_mark_valid();
  O.validated = true;
  hal_log(GADGET_LOG_INFO, TAG, "%s is good", g_core.fw);
  gp_fw_installed_t fi = {.version = g_core.fw};
  session_send("fw.installed", g_core_tx, gp_encode_fw_installed(g_core_tx, sizeof g_core_tx, &fi));
}

void ota_on_session_lost(void) {
  if (O.st == GADGET_OTA_RECEIVING || O.st == GADGET_OTA_WAIT_COMMIT || O.st == GADGET_OTA_FINALIZING) {
    hal_ota_abort();
    reset();
  }
}

void ota_tick(void) {
  uint64_t now = g_core.now;
  if ((O.st == GADGET_OTA_RECEIVING || O.st == GADGET_OTA_WAIT_COMMIT) && now >= O.deadline) fail(GADGET_FW_TIMEOUT);
  if (O.st == GADGET_OTA_RESTARTING && now >= O.restart_at) hal_restart();
  if (O.probation && !O.validated) {
    if (!O.armed) {
      O.armed = true;
      uint32_t ms = g_core.cfg.probation_ms ? g_core.cfg.probation_ms : GADGET_PROBATION_MS;
      O.probation_deadline = now + ms;
    } else if (now >= O.probation_deadline) {
      hal_log(GADGET_LOG_ERROR, TAG, "probation ran out before a ready; rolling back");
      hal_ota_mark_invalid_and_reboot();
    }
  }
  ui_model_t *m = &g_core.model;
  g_core.f.ota_active = O.st != GADGET_OTA_IDLE;
  if (O.st == GADGET_OTA_IDLE) {
    memset(&m->update, 0, sizeof m->update);
    return;
  }
  m->update.phase = O.st == GADGET_OTA_RESTARTING                                        ? UI_UPDATE_RESTARTING
                    : (O.st == GADGET_OTA_FINALIZING || O.st == GADGET_OTA_WAIT_COMMIT) ? UI_UPDATE_VERIFYING
                                                                                         : UI_UPDATE_RECEIVING;
  m->update.pct = O.size ? (uint8_t)((uint64_t)O.written * 100u / O.size) : 0;
  snprintf(m->update.version, sizeof m->update.version, "%s", O.version);
}

void ota_init(void) {
  memset(&O, 0, sizeof O);
  O.probation = hal_ota_running_state() == HAL_OTA_IMG_PENDING_VERIFY;
  if (O.probation) hal_log(GADGET_LOG_WARN, TAG, "new image on probation");
}

void ota_deinit(void) {
  hal_crypto_sha256_abort(&O.hash);
  memset(&O, 0, sizeof O);
}
