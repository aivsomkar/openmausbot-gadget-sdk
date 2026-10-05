/* firmware/core/src/core.c */
/* SPDX-License-Identifier: Apache-2.0 */
/* Core entry points (gadget_core.h): init, events, ticks, the ui_model and
 * introspection. The modules (session, interaction, audio, display, ota,
 * actions, console commands) are wired in here. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "core_internal.h"

core_t g_core;

static core_tap_fn s_tap;
static void *s_tap_ctx;
static ui_model_t s_shadow; /* the model as of the last rev bump */

/* ---- storage ---------------------------------------------------------------- */

void core_store_str(const char *key, const char *value) {
  gadget_status_t st = hal_storage_set_str(key, value);
  if (st != GADGET_OK) hal_log(GADGET_LOG_ERROR, CORE_TAG, "storage set %s failed: %d", key, (int)st);
}

void core_store_erase(const char *key) {
  gadget_status_t st = hal_storage_erase(key);
  if (st != GADGET_OK) hal_log(GADGET_LOG_ERROR, CORE_TAG, "storage erase %s failed: %d", key, (int)st);
}

static void load_str(const char *key, char *buf, size_t cap) {
  if (hal_storage_get_str(key, buf, cap) != GADGET_OK) buf[0] = '\0';
}

/* ---- text ----------------------------------------------------------------------- */

/* Keep the gadget charset (contract §2.1, §2.8): U+0020..U+007E, U+00A0..U+00FF,
 * U+2026, U+2192 and newline. Other control characters become spaces; DEL, the
 * C1 controls, every other code point and invalid bytes become '?'. The result
 * is never longer than the input. */
static void fold_charset(char *s) {
  unsigned char *r = (unsigned char *)s, *w = (unsigned char *)s;
  while (*r) {
    unsigned char c = *r;
    if (c < 0x80) {
      *w++ = (c < 0x20 && c != '\n') ? ' ' : (c == 0x7F ? '?' : c);
      r++;
    } else if (((c == 0xC3) || (c == 0xC2 && r[1] >= 0xA0)) && (r[1] & 0xC0) == 0x80) {
      *w++ = r[0]; /* U+00A0..U+00FF; the C1 controls U+0080..U+009F become '?' below */
      *w++ = r[1];
      r += 2;
    } else if (c == 0xE2 && r[1] == 0x80 && r[2] == 0xA6) {
      memmove(w, r, 3); /* U+2026 */
      w += 3;
      r += 3;
    } else if (c == 0xE2 && r[1] == 0x86 && r[2] == 0x92) {
      memmove(w, r, 3); /* U+2192 */
      w += 3;
      r += 3;
    } else {
      size_t n = 1;
      if (c >= 0xC0 && c < 0xE0) n = 2;
      else if (c >= 0xE0 && c < 0xF0) n = 3;
      else if (c >= 0xF0 && c < 0xF8) n = 4;
      for (size_t i = 1; i < n; i++) {
        if ((r[i] & 0xC0) != 0x80) {
          n = i;
          break;
        }
      }
      *w++ = '?';
      r += n;
    }
  }
  *w = '\0';
}

size_t core_text_copy(char *dst, size_t cap, const char *src) {
  gadget_utf8_copy(dst, cap, src);
  fold_charset(dst);
  return strlen(dst);
}

size_t core_text_copy_tail(char *dst, size_t cap, const char *src) {
  gadget_utf8_copy_tail(dst, cap, src);
  fold_charset(dst);
  return strlen(dst);
}

void core_set_name(const char *src) {
  char tmp[UI_NAME_MAX + 4];
  core_text_copy(tmp, UI_NAME_MAX, src);
  size_t cps = 0;
  for (size_t i = 0; tmp[i] != '\0'; i++) {
    if (((unsigned char)tmp[i] & 0xC0u) == 0x80u) continue; /* a continuation byte */
    if (++cps == GADGET_NAME_MAX && gadget_utf8_len(tmp + i) > 1) {
      memcpy(tmp + i, "\xE2\x80\xA6", 4); /* the 32nd character becomes "…" */
      break;
    }
  }
  gadget_utf8_copy(g_core.name, sizeof g_core.name, tmp);
}

void core_next_turn_id(char out[GADGET_TURN_MAX + 1]) {
  snprintf(out, GADGET_TURN_MAX + 1, "%s-%lu", g_core.turn_prefix, (unsigned long)++g_core.turn_counter);
}

void core_omb(cJSON *obj) {
  if (obj == NULL) return;
  char *text = cJSON_PrintUnformatted(obj);
  cJSON_Delete(obj);
  if (text == NULL) return;
  size_t n = strlen(text);
  char *line = malloc(n + 6);
  if (line != NULL) {
    memcpy(line, "@omb ", 5);
    memcpy(line + 5, text, n + 1);
    hal_console_write(line);
    free(line);
  }
  cJSON_free(text);
}

/* ---- identity ------------------------------------------------------------------- */

static gadget_status_t load_identity(void) {
  size_t len = 0;
  gadget_status_t rd = hal_storage_get_blob(GADGET_KEY_DEV_KEY, g_core.priv, sizeof g_core.priv, &len);
  if (rd != GADGET_OK && rd != GADGET_ERR_NOT_FOUND && rd != GADGET_ERR_LIMIT) {
    /* a read that failed is not a missing key: never overwrite the identity (spec §4.2) */
    hal_log(GADGET_LOG_ERROR, CORE_TAG, "reading the device key failed: %d", (int)rd);
    return rd;
  }
  bool have = rd == GADGET_OK && len == GADGET_PRIVKEY_LEN && hal_crypto_pubkey(g_core.priv, g_core.pub) == GADGET_OK;
  if (!have) {
    /* First boot (or an unusable key): the port started Wi-Fi, so the RNG is truly random. */
    gadget_status_t st = hal_crypto_keygen(g_core.priv, g_core.pub);
    if (st != GADGET_OK) return st;
    st = hal_storage_set_blob(GADGET_KEY_DEV_KEY, g_core.priv, GADGET_PRIVKEY_LEN);
    if (st != GADGET_OK) return st;
    hal_log(GADGET_LOG_INFO, CORE_TAG, "generated a new device key");
  }
  gadget_b64_encode(g_core.pub_b64, sizeof g_core.pub_b64, g_core.pub, GADGET_PUBKEY_LEN);
  return gadget_id_from_pubkey(g_core.pub, g_core.id);
}

static void load_settings(void) {
  char name[UI_NAME_MAX];
  load_str(GADGET_KEY_NAME, name, sizeof name);
  if (name[0] == '\0') {
    if (g_core.cfg.default_name != NULL) {
      snprintf(name, sizeof name, "%s", g_core.cfg.default_name);
    } else {
      snprintf(name, sizeof name, "Maus %.4s", g_core.id + 4);
    }
  }
  core_set_name(name);
  load_str(GADGET_KEY_WIFI_SSID, g_core.wifi_ssid, sizeof g_core.wifi_ssid);
  load_str(GADGET_KEY_WIFI_PASS, g_core.wifi_pass, sizeof g_core.wifi_pass);
  load_str(GADGET_KEY_HOST_ID, g_core.host_id, sizeof g_core.host_id);
  if (!gadget_host_id_valid(g_core.host_id)) g_core.host_id[0] = '\0';
  load_str(GADGET_KEY_HOST_NAME, g_core.host_name, sizeof g_core.host_name);
  load_str(GADGET_KEY_HOST_ADDR, g_core.host_addr, sizeof g_core.host_addr);
  load_str(GADGET_KEY_PAIR_CODE, g_core.pair_code, sizeof g_core.pair_code);
  if (!gadget_pair_code_valid(g_core.pair_code)) g_core.pair_code[0] = '\0';
  load_str(GADGET_KEY_BOT_ID, g_core.bot_id, sizeof g_core.bot_id);
  load_str(GADGET_KEY_BOT_NAME, g_core.bot_name, sizeof g_core.bot_name);
  char flag[4];
  load_str(GADGET_KEY_SPEAK_PUSH, flag, sizeof flag);
  g_core.speak_pushes = strcmp(flag, "1") == 0;
}

/* ---- model ------------------------------------------------------------------------ */

/* Bump rev when anything but rev and now_ms changed since the last bump. */
static void model_commit(void) {
  static ui_model_t tmp;
  memcpy(&tmp, &g_core.model, sizeof tmp);
  tmp.rev = s_shadow.rev;
  tmp.now_ms = s_shadow.now_ms;
  if (memcmp(&tmp, &s_shadow, sizeof tmp) != 0) {
    g_core.model.rev++;
    memcpy(&s_shadow, &g_core.model, sizeof s_shadow);
  }
}

static void publish_identity(void) {
  ui_model_t *m = &g_core.model;
  snprintf(m->device_id, sizeof m->device_id, "%s", g_core.id);
  snprintf(m->device_name, sizeof m->device_name, "%s", g_core.name);
  core_text_copy(m->bot_name, sizeof m->bot_name, g_core.bot_name);
  core_text_copy(m->host_name, sizeof m->host_name, g_core.host_name);
}

/* ---- public API -------------------------------------------------------------------- */

gadget_status_t core_init(const core_config_t *cfg) {
  if (cfg == NULL || cfg->board == NULL || cfg->fw_version == NULL || cfg->fw_version[0] == '\0' ||
      strlen(cfg->fw_version) > GADGET_VERSION_MAX) {
    return GADGET_ERR_ARG;
  }
  memset(&g_core, 0, sizeof g_core);
  memset(&s_shadow, 0, sizeof s_shadow);
  g_core.board = cfg->board;
  g_core.cfg = *cfg;
  snprintf(g_core.fw, sizeof g_core.fw, "%s", cfg->fw_version);
  g_core.model.screen = UI_SCREEN_BOOT;

  gadget_status_t st = load_identity();
  if (st != GADGET_OK) {
    hal_log(GADGET_LOG_ERROR, CORE_TAG, "identity failed: %d", (int)st);
    return st;
  }
  load_settings();

  uint32_t seed = cfg->prng_seed;
  if (seed == 0) hal_crypto_random(&seed, sizeof seed);
  gadget_prng_seed(&g_core.prng, seed);
  uint8_t rnd[4];
  if (hal_crypto_random(rnd, sizeof rnd) != GADGET_OK) memset(rnd, 0, sizeof rnd);
  g_core.turn_prefix[0] = 't';
  gadget_hex_encode(g_core.turn_prefix + 1, rnd, sizeof rnd);

  publish_identity();
  session_init();
  interaction_init();
  audio_init();
  display_init();
  if (g_core.wifi_ssid[0] != '\0') hal_wifi_connect(g_core.wifi_ssid, g_core.wifi_pass);

  cJSON *boot = cJSON_CreateObject();
  cJSON_AddStringToObject(boot, "op", "boot");
  cJSON_AddStringToObject(boot, "board", g_core.board->id);
  cJSON_AddStringToObject(boot, "fw", g_core.fw);
  cJSON_AddStringToObject(boot, "id", g_core.id);
  core_omb(boot);
  hal_log(GADGET_LOG_INFO, CORE_TAG, "%s on %s, fw %s", g_core.id, g_core.board->id, g_core.fw);

  g_core.initialized = true;
  actions_init();
  model_commit();
  return GADGET_OK;
}

void core_event(const gadget_event_t *ev) {
  if (!g_core.initialized || ev == NULL) return;
  switch (ev->type) {
    case GADGET_EV_INPUT:
      interaction_input(&ev->u.input);
      break;
    case GADGET_EV_MIC_FRAME:
      interaction_mic(&ev->u.mic);
      break;
    case GADGET_EV_CONSOLE_LINE:
      console_exec_line(ev->u.console.line);
      break;
    case GADGET_EV_WIFI_SCAN:
      console_on_scan(&ev->u.scan);
      break;
    case GADGET_EV_WIFI_STATE:
    case GADGET_EV_WS_OPEN:
    case GADGET_EV_WS_TEXT:
    case GADGET_EV_WS_BINARY:
    case GADGET_EV_WS_CONTROL:
    case GADGET_EV_WS_CLOSED:
    case GADGET_EV_MDNS:
      session_event(ev);
      break;
    default:
      break;
  }
  screens_update();
  model_commit();
}

void core_tick(uint64_t now_ms) {
  if (!g_core.initialized) return;
  g_core.now = now_ms;
  g_core.model.now_ms = now_ms;
  g_core.ticked = true;
  session_tick();
  interaction_tick();
  audio_tick();
  display_tick();
  screens_update();
  model_commit();
}

const ui_model_t *core_ui_model(void) { return &g_core.model; }

void core_deinit(void) {
  if (g_core.initialized) {
    actions_deinit();
    display_deinit();
    audio_deinit();
    interaction_deinit();
    session_deinit();
  }
  memset(&g_core, 0, sizeof g_core);
  memset(&s_shadow, 0, sizeof s_shadow);
}

const char *core_device_id(void) { return g_core.id; }
core_pair_state_t core_pair_state(void) { return session_pair_state(); }
const char *core_last_error(void) { return session_last_error(); }
const char *core_fw_version(void) { return g_core.fw; }

void core_set_tap(core_tap_fn fn, void *ctx) {
  s_tap = fn;
  s_tap_ctx = ctx;
}

void core_tap(core_tap_dir_t dir, const char *op, const char *json, size_t len) {
  if (s_tap != NULL) s_tap(dir, op, json, len, s_tap_ctx);
}

/* ---- session hooks ------------------------------------------------------------------ */

void core_on_ready(void) { hal_log(GADGET_LOG_INFO, CORE_TAG, "talking to %s", g_core.bot_name); }

void core_on_session_lost(void) {
  hal_log(GADGET_LOG_INFO, CORE_TAG, "session lost");
  audio_stop_local();
  interaction_on_session_lost();
  display_on_session_lost();
}

void core_on_msg(const gp_msg_t *m) {
  if (interaction_on_msg(m)) return;
  if (audio_on_msg(m)) return;
  if (display_on_msg(m)) return;
  if (actions_on_msg(m)) return;
  switch (m->op) {
    default:
      hal_log(GADGET_LOG_DEBUG, CORE_TAG, "ignored %s", gp_op_name(m->op));
      break;
  }
}

void core_on_binary(gp_bin_kind_t kind, uint8_t stream, const uint8_t *payload, size_t len) {
  switch (kind) {
    case GP_BIN_SPEAKER:
      audio_on_binary(stream, payload, len);
      break;
    case GP_BIN_IMAGE:
      display_on_binary(stream, payload, len);
      break;
    default:
      hal_log(GADGET_LOG_DEBUG, CORE_TAG, "ignored binary kind %d stream %u (%u bytes)", (int)kind, (unsigned)stream,
              (unsigned)len);
      break;
  }
}
