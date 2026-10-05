/* firmware/core/src/actions.c */
/* SPDX-License-Identifier: Apache-2.0 */
/* Actions a gadget declares in hello and runs on `act`, and the events it
 * sends (gadget_actions.h, spec §4.3 limits, §4.7, §5.10). The built-in
 * `chime` is registered by core_init(). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "core_internal.h"
#include "gadget_actions.h"

#define TAG "actions"
#define EVENT_DATA_MAX 1024u /* an event's data, serialized compactly (gadget_actions.h) */

typedef struct {
  char name[GADGET_ACTION_NAME_MAX + 1];
  char description[GADGET_ACTION_DESC_MAX * 4 + 1]; /* 200 code points, up to 4 bytes each */
  char *params;                                      /* compact schema JSON (malloc), NULL = the default */
  gadget_risk_t risk;
  gadget_action_handler_t handler;
} action_t;

static action_t s_actions[GADGET_ACTIONS_MAX];
static uint8_t s_count;
static gp_action_decl_t s_decls[GADGET_ACTIONS_MAX];

static bool valid_name(const char *n) {
  size_t len = n ? strlen(n) : 0;
  if (len < 1 || len > GADGET_ACTION_NAME_MAX || n[0] < 'a' || n[0] > 'z') return false;
  for (size_t i = 1; i < len; i++) {
    char c = n[i];
    bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '.' || c == '-';
    if (!ok) return false;
  }
  return true;
}

static const action_t *find(const char *name) {
  for (uint8_t i = 0; i < s_count; i++) {
    if (strcmp(s_actions[i].name, name) == 0) return &s_actions[i];
  }
  return NULL;
}

const gp_action_decl_t *actions_decls(uint8_t *count) {
  for (uint8_t i = 0; i < s_count; i++) {
    s_decls[i].name = s_actions[i].name;
    s_decls[i].description = s_actions[i].description;
    s_decls[i].params_json = s_actions[i].params;
    s_decls[i].risk = s_actions[i].risk;
  }
  *count = s_count;
  return s_decls;
}

/* The hello with the current actions fits the text frame limit whatever the
 * name becomes later (console `name`, a desktop rename): 32 x "…" (96 bytes)
 * is longer than any name core_set_name keeps, and battery_pct 100 with
 * charging false is the longest battery. */
static bool hello_fits(void) {
  char longest[3 * GADGET_NAME_MAX + 1];
  for (size_t i = 0; i < GADGET_NAME_MAX; i++) memcpy(longest + 3 * i, "\xE2\x80\xA6", 3);
  longest[3 * GADGET_NAME_MAX] = '\0';
  uint8_t n = 0;
  const gp_action_decl_t *decls = actions_decls(&n);
  gp_hello_t h = {.id = g_core.id, .pubkey_b64 = g_core.pub_b64, .name = longest, .fw = g_core.fw,
                  .board = g_core.board, .actions = decls, .n_actions = n,
                  .battery_valid = g_core.board->has_battery, .battery_pct = 100, .charging = false};
  return gp_encode_hello(g_core_tx, sizeof g_core_tx, &h) > 0;
}

gadget_status_t gadget_action_register(const char *name, const char *description, const char *params_schema_json,
                                       gadget_risk_t risk, gadget_action_handler_t handler) {
  if (!g_core.initialized) return GADGET_ERR_STATE;
  if (!valid_name(name) || handler == NULL || (risk != GADGET_RISK_SAFE && risk != GADGET_RISK_CONFIRM)) {
    return GADGET_ERR_ARG;
  }
  size_t dlen = description ? gadget_utf8_len(description) : 0;
  if (dlen == 0) return GADGET_ERR_ARG;
  if (dlen > GADGET_ACTION_DESC_MAX || strlen(description) >= sizeof s_actions[0].description) return GADGET_ERR_LIMIT;
  if (find(name) != NULL) return GADGET_ERR_STATE;
  if (s_count >= GADGET_ACTIONS_MAX) return GADGET_ERR_LIMIT;
  char *params = NULL;
  if (params_schema_json != NULL) {
    cJSON *schema = cJSON_Parse(params_schema_json);
    if (!cJSON_IsObject(schema)) {
      cJSON_Delete(schema);
      return GADGET_ERR_ARG;
    }
    params = cJSON_PrintUnformatted(schema);
    cJSON_Delete(schema);
    if (params == NULL) return GADGET_ERR_NO_MEM;
    if (strlen(params) > GADGET_ACTION_PARAMS_MAX) {
      cJSON_free(params);
      return GADGET_ERR_LIMIT;
    }
  }
  action_t *a = &s_actions[s_count++];
  snprintf(a->name, sizeof a->name, "%s", name);
  snprintf(a->description, sizeof a->description, "%s", description);
  a->params = params;
  a->risk = risk;
  a->handler = handler;
  if (!hello_fits()) {
    s_count--;
    cJSON_free(a->params);
    memset(a, 0, sizeof *a);
    return GADGET_ERR_LIMIT;
  }
  return GADGET_OK;
}

/* Exactly one act.result per act (contract §2.10): a result that cannot be
 * encoded (over 16 KiB, or no memory) is sent as a failure instead, so the
 * host gets a clear error rather than waiting out its timeout. */
static void send_result(const char *id, bool ok, const cJSON *data, const char *error) {
  gp_act_result_t r = {.id = id, .ok = ok, .data = data, .error = error};
  int n = gp_encode_act_result(g_core_tx, sizeof g_core_tx, &r);
  if (n < 0) {
    hal_log(GADGET_LOG_WARN, TAG, "act.result %s not encoded (%d); sending a failure", id, n);
    gp_act_result_t f = {.id = id, .ok = false, .error = n == GADGET_ERR_NO_MEM ? "out of memory" : "result too large"};
    n = gp_encode_act_result(g_core_tx, sizeof g_core_tx, &f);
  }
  session_send("act.result", g_core_tx, n);
}

bool actions_on_msg(const gp_msg_t *m) {
  if (m->op != GP_OP_ACT) return false;
  const gp_act_t *act = &m->m.act;
  const action_t *a = find(act->name);
  if (a == NULL) {
    send_result(act->id, false, NULL, "unknown action");
    return true;
  }
  cJSON *empty = act->args == NULL ? cJSON_CreateObject() : NULL;
  cJSON *data = cJSON_CreateObject();
  char error[128] = "";
  bool ok = a->handler(act->args ? act->args : empty, data, error, sizeof error);
  if (ok) {
    send_result(act->id, true, data, NULL);
  } else {
    char folded[128];
    core_text_copy(folded, sizeof folded, error[0] ? error : "failed");
    send_result(act->id, false, NULL, folded);
  }
  cJSON_Delete(data);
  cJSON_Delete(empty);
  return true;
}

gadget_status_t gadget_event_send(const char *name, const cJSON *data) {
  if (!valid_name(name)) return GADGET_ERR_ARG;
  if (data != NULL) {
    char *json = cJSON_PrintUnformatted(data);
    if (json == NULL) return GADGET_ERR_NO_MEM;
    size_t len = strlen(json);
    cJSON_free(json);
    if (len > EVENT_DATA_MAX) return GADGET_ERR_LIMIT;
  }
  if (!g_core.initialized || !session_ready()) return GADGET_ERR_BUSY;
  gp_event_msg_t ev = {.name = name, .data = data};
  return session_send("event", g_core_tx, gp_encode_event(g_core_tx, sizeof g_core_tx, &ev));
}

static bool chime_handler(const cJSON *args, cJSON *data, char *error, size_t error_cap) {
  (void)args;
  (void)data;
  (void)error;
  (void)error_cap;
  audio_play_chime(); /* a nicety: skipped without a speaker or over speech, still ok */
  return true;
}

void actions_init(void) {
  s_count = 0;
  gadget_status_t st = gadget_action_register("chime", "Play a short chime.", NULL, GADGET_RISK_SAFE, chime_handler);
  if (st != GADGET_OK) hal_log(GADGET_LOG_ERROR, TAG, "chime not registered: %d", (int)st);
}

void actions_deinit(void) {
  for (uint8_t i = 0; i < s_count; i++) cJSON_free(s_actions[i].params);
  memset(s_actions, 0, sizeof s_actions);
  s_count = 0;
}
