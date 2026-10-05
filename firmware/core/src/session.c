/* firmware/core/src/session.c */
/* SPDX-License-Identifier: Apache-2.0 */
/* The connection to MausBot (spec §4.3): finding the host, the handshake,
 * the reaction to every handshake error, reconnect backoff and liveness. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "core_internal.h"

#define TAG "session"
#define CONNECT_GUARD_MS 10000u   /* the port posts WS_CLOSED within 5 s; this is a safety net */

typedef enum {
  SS_IDLE = 0,   /* before the first tick */
  SS_WAIT_WIFI,
  SS_RESOLVING,  /* hal_mdns_browse running */
  SS_CONNECTING, /* hal_ws_open called, waiting for WS_OPEN */
  SS_HANDSHAKE,  /* hello sent, waiting for challenge / ready */
  SS_READY,
  SS_CLOSING,    /* hal_ws_close called, waiting for WS_CLOSED */
  SS_BACKOFF,    /* waiting until retry_at */
  SS_HALTED      /* no automatic reconnect */
} sess_state_t;

typedef enum {
  HALT_NONE = 0,
  HALT_UNPAIRED,   /* no host_id and no code: enroll_required, revoked, device-limit window over */
  HALT_BAD_CODE,
  HALT_REPLACED,   /* until reboot or TALK */
  HALT_NO_MDNS,    /* host auto is not available on this port */
  HALT_NO_HOST     /* never paired, and host auto found none or several: wait for host <address> */
} halt_t;

typedef enum { AFTER_RETRY = 0, AFTER_NOW, AFTER_HALT, AFTER_DEVICE_LIMIT } after_t;

char g_core_tx[GADGET_TEXT_FRAME_MAX];

static struct {
  sess_state_t st;
  halt_t halt, pending_halt;
  after_t after;               /* what the next WS_CLOSED leads to */
  bool ws_live;                /* hal_ws_open succeeded and WS_CLOSED has not arrived */
  bool ws_opened;              /* WS_OPEN arrived on this connection */
  bool challenged;
  bool was_ready;
  uint64_t last_rx, connect_at, retry_at, mdns_deadline;
  uint32_t backoff_ms;
  char resolved[CORE_HOST_ADDR_MAX];
  char ch_host_id[GADGET_HOST_ID_LEN + 1];
  char ch_host_name[UI_NAME_MAX]; /* the last challenge's; outlives its connection (spec 5.5) */
  char last_error[24];
  bool error_flag;             /* @omb pair "error" */
  bool protocol_error;         /* Offline reason "protocol" */
  bool host_not_found;
  bool hosts_printed;          /* this run of host auto misses printed its hosts line */
  bool device_limit;
  bool window_known;
  uint64_t window_start;       /* when the stored code was given (device_limit window) */
} S;

/* ---- sending ------------------------------------------------------------------- */

gadget_status_t session_send(const char *op, const char *json, int len) {
  if (len < 0) return (gadget_status_t)len;
  if (!S.ws_live || !S.ws_opened) return GADGET_ERR_BUSY;
  core_tap(CORE_TAP_TX, op, json, (size_t)len);
  return hal_ws_send_text(json, (size_t)len);
}

gadget_status_t session_send_binary(const uint8_t *frame, size_t len) {
  if (S.st != SS_READY) return GADGET_ERR_BUSY;
  return hal_ws_send_binary(frame, len);
}

static void send_hello(void) {
  gp_hello_t h = {.id = g_core.id, .pubkey_b64 = g_core.pub_b64, .name = g_core.name, .fw = g_core.fw,
                  .board = g_core.board};
  h.actions = actions_decls(&h.n_actions);
  gadget_battery_t batt;
  if (g_core.board->has_battery && hal_battery_read(&batt)) {
    h.battery_valid = true;
    h.battery_pct = batt.pct > 100 ? 100 : batt.pct;
    h.charging = batt.charging;
  }
  int n = gp_encode_hello(g_core_tx, sizeof g_core_tx, &h);
  if (session_send("hello", g_core_tx, n) != GADGET_OK) hal_log(GADGET_LOG_ERROR, TAG, "hello not sent (%d)", n);
}

/* ---- console output ---------------------------------------------------------------- */

static void print_hosts(const gadget_mdns_host_t *hosts, uint8_t count) {
  cJSON *o = cJSON_CreateObject();
  cJSON_AddStringToObject(o, "op", "hosts");
  cJSON *arr = cJSON_AddArrayToObject(o, "hosts");
  for (uint8_t i = 0; i < count; i++) {
    cJSON *h = cJSON_CreateObject();
    cJSON_AddStringToObject(h, "name", hosts[i].name);
    cJSON_AddStringToObject(h, "address", hosts[i].address);
    cJSON_AddStringToObject(h, "id", hosts[i].id);
    cJSON_AddItemToArray(arr, h);
  }
  core_omb(o);
}

/* ---- state changes -------------------------------------------------------------------- */

static void schedule_retry(void) {
  S.retry_at = g_core.now + S.backoff_ms;
  S.backoff_ms = S.backoff_ms * 2 > GADGET_BACKOFF_MAX_MS ? GADGET_BACKOFF_MAX_MS : S.backoff_ms * 2;
  S.st = SS_BACKOFF;
}

static void halt(halt_t why) {
  S.st = SS_HALTED;
  S.halt = why;
}

static void close_conn(uint16_t code, after_t after, halt_t why) {
  S.after = after;
  S.pending_halt = why;
  if (S.ws_live) {
    S.st = SS_CLOSING;
    hal_ws_close(code);
  }
}

static void connect_to(const char *addr) {
  char host[CORE_HOST_ADDR_MAX];
  snprintf(host, sizeof host, "%s", addr);
  uint16_t port = GADGET_DEFAULT_PORT;
  char *colon = strrchr(host, ':');
  if (colon != NULL) {
    long p = strtol(colon + 1, NULL, 10);
    if (p > 0 && p <= 65535) port = (uint16_t)p;
    *colon = '\0';
  }
  if (hal_ws_open(host, port) != GADGET_OK) {
    hal_log(GADGET_LOG_WARN, TAG, "connect to %s:%u failed at once", host, (unsigned)port);
    schedule_retry();
    return;
  }
  hal_log(GADGET_LOG_INFO, TAG, "connecting to %s:%u", host, (unsigned)port);
  S.ws_live = true;
  S.ws_opened = false;
  S.challenged = false;
  S.connect_at = S.last_rx = g_core.now;
  S.st = SS_CONNECTING;
}

static void start_attempt(void) {
  if (g_core.host_id[0] == '\0' && g_core.pair_code[0] == '\0') {
    halt(HALT_UNPAIRED);
    return;
  }
  if (hal_wifi_state() != GADGET_WIFI_CONNECTED) {
    S.st = SS_WAIT_WIFI;
    return;
  }
  if (g_core.host_addr[0] != '\0') {
    connect_to(g_core.host_addr);
    return;
  }
  if (S.resolved[0] != '\0') {
    connect_to(S.resolved);
    return;
  }
  gadget_status_t st = hal_mdns_browse(GADGET_HOST_AUTO_TIMEOUT_MS);
  if (st == GADGET_ERR_UNSUPPORTED) {
    hal_log(GADGET_LOG_WARN, TAG, "host auto is not available here; use host <address>");
    print_hosts(NULL, 0);
    S.host_not_found = true;
    halt(HALT_NO_MDNS);
    return;
  }
  if (st != GADGET_OK) {
    schedule_retry();
    return;
  }
  S.mdns_deadline = g_core.now + GADGET_HOST_AUTO_TIMEOUT_MS + 1000u;
  S.st = SS_RESOLVING;
}

/* A browse that ended because Wi-Fi dropped is no miss: browse again once it is back. */
static bool browse_lost_wifi(void) {
  if (hal_wifi_state() == GADGET_WIFI_CONNECTED) return false;
  S.st = SS_WAIT_WIFI;
  return true;
}

/* host auto found none, or several and no stored host_id picks one (spec §5.6).
 * The hosts line prints once per run of misses, so the installer asks once. */
static void host_not_found(const gadget_mdns_host_t *hosts, uint8_t count) {
  if (browse_lost_wifi()) return;
  if (!S.hosts_printed) print_hosts(hosts, count);
  S.hosts_printed = true;
  S.host_not_found = true;
  if (g_core.host_id[0] == '\0') {
    halt(HALT_NO_HOST); /* the installer or the person answers with host <address> */
  } else {
    schedule_retry();   /* a paired gadget keeps looking for its own MausBot */
  }
}

static void on_mdns(const gadget_mdns_ev_t *r) {
  if (S.st != SS_RESOLVING || browse_lost_wifi()) return;
  if (!r->ok) {
    hal_log(GADGET_LOG_WARN, TAG, "host lookup failed; trying again");
    schedule_retry(); /* a failed browse is a failed attempt, not a miss */
    return;
  }
  const gadget_mdns_host_t *pick = NULL;
  uint8_t count = r->count;
  for (uint8_t i = 0; i < count; i++) {
    if (g_core.host_id[0] != '\0' && strcmp(r->hosts[i].id, g_core.host_id) == 0) pick = &r->hosts[i];
  }
  if (g_core.host_id[0] == '\0' && count == 1) pick = &r->hosts[0];
  if (pick == NULL) {
    host_not_found(r->hosts, count);
    return;
  }
  snprintf(S.resolved, sizeof S.resolved, "%s", pick->address);
  S.host_not_found = false;
  connect_to(S.resolved);
}

static void apply_bot(const gp_bot_t *bot) {
  if (strcmp(g_core.bot_id, bot->id) != 0) {
    snprintf(g_core.bot_id, sizeof g_core.bot_id, "%s", bot->id);
    core_store_str(GADGET_KEY_BOT_ID, g_core.bot_id);
  }
  char name[UI_NAME_MAX];
  core_text_copy(name, sizeof name, bot->name);
  if (strcmp(g_core.bot_name, name) != 0) {
    snprintf(g_core.bot_name, sizeof g_core.bot_name, "%s", name);
    core_store_str(GADGET_KEY_BOT_NAME, g_core.bot_name);
  }
}

static void apply_settings(const gp_settings_values_t *s) {
  if (g_core.speak_pushes != s->speak_pushes) {
    g_core.speak_pushes = s->speak_pushes;
    core_store_str(GADGET_KEY_SPEAK_PUSH, s->speak_pushes ? "1" : "0");
  }
}

static void on_challenge(const gp_challenge_t *c) {
  if (!gadget_host_id_valid(c->host_id) || c->nonce[0] == '\0' || strlen(c->nonce) > 64) {
    hal_log(GADGET_LOG_WARN, TAG, "challenge with a bad host_id or nonce; closing");
    S.protocol_error = true;
    close_conn(1002, AFTER_RETRY, HALT_NONE);
    return;
  }
  snprintf(S.ch_host_id, sizeof S.ch_host_id, "%s", c->host_id);
  core_text_copy(S.ch_host_name, sizeof S.ch_host_name, c->host_name ? c->host_name : "");
  S.challenged = true;
  char text[160];
  int tn = gp_prove_text(text, sizeof text, g_core.id, c->nonce, c->host_id);
  uint8_t der[GADGET_SIG_DER_MAX];
  size_t der_len = 0;
  char sig[GADGET_SIG_B64_MAX + 1];
  if (tn < 0 || hal_crypto_sign(g_core.priv, (const uint8_t *)text, (size_t)tn, der, &der_len) != GADGET_OK ||
      gadget_b64_encode(sig, sizeof sig, der, der_len) == 0) {
    hal_log(GADGET_LOG_ERROR, TAG, "signing the challenge failed");
    close_conn(1011, AFTER_RETRY, HALT_NONE);
    return;
  }
  gp_prove_t p = {.sig_b64 = sig, .enroll = g_core.pair_code[0] ? g_core.pair_code : NULL};
  session_send("prove", g_core_tx, gp_encode_prove(g_core_tx, sizeof g_core_tx, &p));
}

static void on_ready(const gp_ready_t *r) {
  S.st = SS_READY;
  S.was_ready = true;
  if (strcmp(g_core.host_id, S.ch_host_id) != 0) {
    snprintf(g_core.host_id, sizeof g_core.host_id, "%s", S.ch_host_id);
    core_store_str(GADGET_KEY_HOST_ID, g_core.host_id);
  }
  if (strcmp(g_core.host_name, S.ch_host_name) != 0) {
    snprintf(g_core.host_name, sizeof g_core.host_name, "%s", S.ch_host_name);
    core_store_str(GADGET_KEY_HOST_NAME, g_core.host_name);
  }
  if (g_core.pair_code[0] != '\0') {
    g_core.pair_code[0] = '\0';
    core_store_erase(GADGET_KEY_PAIR_CODE);
  }
  S.backoff_ms = GADGET_BACKOFF_MIN_MS;
  S.error_flag = S.protocol_error = S.host_not_found = S.hosts_printed = S.device_limit = false;
  S.last_error[0] = '\0';
  S.halt = HALT_NONE;
  apply_bot(&r->bot);
  apply_settings(&r->settings);
  hal_log(GADGET_LOG_INFO, TAG, "ready with %s (session %s)", g_core.host_name, r->session);
  core_on_ready();
}

static void on_error(const gp_error_t *e) {
  snprintf(S.last_error, sizeof S.last_error, "%s", e->code);
  hal_log(GADGET_LOG_WARN, TAG, "host error %s: %s", e->code, e->message ? e->message : "");
  if (strcmp(e->code, "bad_code") == 0) {
    g_core.pair_code[0] = '\0';
    core_store_erase(GADGET_KEY_PAIR_CODE);
    S.error_flag = true;
    close_conn(1000, AFTER_HALT, HALT_BAD_CODE);
  } else if (strcmp(e->code, "enroll_required") == 0 || strcmp(e->code, "revoked") == 0) {
    g_core.host_id[0] = '\0';
    core_store_erase(GADGET_KEY_HOST_ID);
    S.error_flag = false;
    close_conn(1000, AFTER_HALT, HALT_UNPAIRED);
  } else if (strcmp(e->code, "device_limit") == 0) {
    S.device_limit = true;
    close_conn(1000, AFTER_DEVICE_LIMIT, HALT_NONE);
  } else if (strcmp(e->code, "replaced") == 0) {
    close_conn(1000, AFTER_HALT, HALT_REPLACED);
  } else {
    /* proto_unsupported, bad_sig, or a code this firmware does not know */
    S.error_flag = true;
    S.protocol_error = true;
    close_conn(1000, AFTER_RETRY, HALT_NONE);
  }
}

static void on_settings(const gp_settings_t *s) {
  if (s->has_bot) apply_bot(&s->bot);
  if (s->has_settings) apply_settings(&s->settings);
  if (s->name != NULL && s->name[0] != '\0') {
    char old[UI_NAME_MAX];
    snprintf(old, sizeof old, "%s", g_core.name);
    core_set_name(s->name);
    if (strcmp(old, g_core.name) != 0) core_store_str(GADGET_KEY_NAME, g_core.name);
  }
}

static void on_text(const uint8_t *data, size_t len) {
  gp_msg_t m;
  if (gp_decode((const char *)data, len, &m) != GADGET_OK) {
    hal_log(GADGET_LOG_WARN, TAG, "ignored a malformed frame");
    return;
  }
  const char *op = gp_op_name(m.op);
  if (op == NULL) op = cJSON_GetObjectItemCaseSensitive(m.root, "op")->valuestring;
  core_tap(CORE_TAP_RX, op, (const char *)data, len);
  switch (m.op) {
    case GP_OP_UNKNOWN:
      break;
    case GP_OP_CHALLENGE:
      if (S.st == SS_HANDSHAKE && !S.challenged) on_challenge(&m.m.challenge);
      break;
    case GP_OP_READY:
      if (S.st == SS_HANDSHAKE && S.challenged) on_ready(&m.m.ready);
      break;
    case GP_OP_ERROR:
      if (S.st == SS_HANDSHAKE || S.st == SS_READY) on_error(&m.m.error);
      break;
    case GP_OP_SETTINGS:
      if (S.st == SS_READY) on_settings(&m.m.settings);
      break;
    default:
      if (S.st == SS_READY) core_on_msg(&m);
      break;
  }
  gp_msg_free(&m);
}

static void on_closed(uint16_t code) {
  if (!S.ws_live) return;
  hal_log(GADGET_LOG_INFO, TAG, "connection closed (%u)", (unsigned)code);
  S.ws_live = false;
  S.challenged = false;
  if (!S.ws_opened) S.resolved[0] = '\0'; /* connect failed: resolve again next time */
  if (S.was_ready) {
    S.was_ready = false;
    core_on_session_lost();
  }
  after_t after = S.after;
  S.after = AFTER_RETRY;
  switch (after) {
    case AFTER_NOW:
      S.st = SS_BACKOFF;
      S.retry_at = g_core.now;
      break;
    case AFTER_HALT:
      halt(S.pending_halt);
      break;
    case AFTER_DEVICE_LIMIT:
      S.st = SS_BACKOFF;
      S.retry_at = g_core.now + GADGET_DEVICE_LIMIT_RETRY_MS;
      break;
    case AFTER_RETRY:
    default:
      schedule_retry();
      break;
  }
}

/* ---- module API ------------------------------------------------------------------------- */

void session_init(void) {
  memset(&S, 0, sizeof S);
  S.backoff_ms = GADGET_BACKOFF_MIN_MS;
  S.st = SS_IDLE;
}

void session_deinit(void) {
  if (S.ws_live) hal_ws_close(1001);
  memset(&S, 0, sizeof S);
}

static void publish(void) {
  ui_model_t *m = &g_core.model;
  snprintf(m->device_name, sizeof m->device_name, "%s", g_core.name);
  core_text_copy(m->bot_name, sizeof m->bot_name, g_core.bot_name);
  core_text_copy(m->host_name, sizeof m->host_name, S.ch_host_name[0] ? S.ch_host_name : g_core.host_name);
  gadget_wifi_state_t wifi = hal_wifi_state();
  m->setup.wifi_set = g_core.wifi_ssid[0] != '\0' || wifi == GADGET_WIFI_CONNECTED;
  m->setup.code_stored = g_core.pair_code[0] != '\0';
  if (!m->setup.wifi_set) m->setup.step = UI_SETUP_NEED_WIFI;
  else if (S.error_flag && strcmp(S.last_error, "bad_code") == 0) m->setup.step = UI_SETUP_BAD_CODE;
  else if (S.device_limit) m->setup.step = UI_SETUP_DEVICE_LIMIT;
  else if (S.host_not_found) m->setup.step = UI_SETUP_HOST_NOT_FOUND;
  else if (m->setup.code_stored) m->setup.step = UI_SETUP_PAIRING;
  else m->setup.step = UI_SETUP_NEED_CODE;
  snprintf(m->offline.ssid, sizeof m->offline.ssid, "%s", g_core.wifi_ssid);
  m->offline.retry_at_ms = S.st == SS_BACKOFF ? S.retry_at : 0;
  if (wifi == GADGET_WIFI_CONNECTING) m->offline.reason = UI_OFFLINE_WIFI_CONNECTING;
  else if (wifi != GADGET_WIFI_CONNECTED) m->offline.reason = UI_OFFLINE_WIFI_FAILED;
  else if (S.st == SS_HALTED && S.halt == HALT_REPLACED) m->offline.reason = UI_OFFLINE_IN_USE_ELSEWHERE;
  else if (S.protocol_error) m->offline.reason = UI_OFFLINE_PROTOCOL;
  else if (S.host_not_found || S.st == SS_RESOLVING || S.st == SS_CONNECTING || S.st == SS_HANDSHAKE ||
           S.st == SS_IDLE)
    m->offline.reason = UI_OFFLINE_HOST_LOOKUP;
  else m->offline.reason = UI_OFFLINE_HOST_UNREACHABLE;
}

void session_event(const gadget_event_t *ev) {
  switch (ev->type) {
    case GADGET_EV_WIFI_STATE:
      if (ev->u.wifi.state != GADGET_WIFI_CONNECTED && S.ws_live) close_conn(1001, AFTER_RETRY, HALT_NONE);
      break;
    case GADGET_EV_WS_OPEN:
      if (!S.ws_live || S.st != SS_CONNECTING) break;
      S.ws_opened = true;
      S.last_rx = g_core.now;
      S.st = SS_HANDSHAKE;
      send_hello();
      break;
    case GADGET_EV_WS_TEXT:
      if (!S.ws_live) break;
      S.last_rx = g_core.now;
      if (ev->u.ws.len > GADGET_TEXT_FRAME_MAX) { /* over the protocol limit: never decoded */
        hal_log(GADGET_LOG_WARN, TAG, "dropped a %u-byte text frame", (unsigned)ev->u.ws.len);
        break;
      }
      on_text(ev->u.ws.data, ev->u.ws.len);
      break;
    case GADGET_EV_WS_BINARY:
      if (!S.ws_live) break;
      S.last_rx = g_core.now;
      if (ev->u.ws.len > GADGET_BINARY_FRAME_MAX) {
        hal_log(GADGET_LOG_WARN, TAG, "dropped a %u-byte binary frame", (unsigned)ev->u.ws.len);
        break;
      }
      if (S.st == SS_READY) {
        gp_bin_kind_t kind;
        uint8_t stream;
        const uint8_t *payload;
        size_t plen;
        if (gp_bin_decode(ev->u.ws.data, ev->u.ws.len, &kind, &stream, &payload, &plen) == GADGET_OK) {
          core_on_binary(kind, stream, payload, plen);
        }
      }
      break;
    case GADGET_EV_WS_CONTROL:
      if (S.ws_live) S.last_rx = g_core.now;
      break;
    case GADGET_EV_WS_CLOSED:
      on_closed(ev->u.closed.code);
      break;
    case GADGET_EV_MDNS:
      on_mdns(&ev->u.mdns);
      break;
    default:
      break;
  }
  publish();
}

void session_tick(void) {
  uint64_t now = g_core.now;
  if (g_core.pair_code[0] != '\0' && !S.window_known) {
    S.window_known = true;
    S.window_start = now;
  }
  switch (S.st) {
    case SS_IDLE:
      start_attempt();
      break;
    case SS_WAIT_WIFI:
      if (hal_wifi_state() == GADGET_WIFI_CONNECTED) start_attempt();
      break;
    case SS_BACKOFF:
      if (now >= S.retry_at) start_attempt();
      break;
    case SS_RESOLVING:
      if (now >= S.mdns_deadline) host_not_found(NULL, 0);
      break;
    case SS_CONNECTING:
      if (now - S.connect_at >= CONNECT_GUARD_MS) close_conn(1001, AFTER_RETRY, HALT_NONE);
      break;
    case SS_HANDSHAKE:
    case SS_READY:
      if (now - S.last_rx >= GADGET_IDLE_TIMEOUT_MS) {
        hal_log(GADGET_LOG_WARN, TAG, "nothing heard for 45 s; reconnecting");
        close_conn(1001, AFTER_RETRY, HALT_NONE);
      }
      break;
    default:
      break;
  }
  if (S.device_limit && now >= S.window_start + GADGET_DEVICE_LIMIT_WINDOW_MS) {
    hal_log(GADGET_LOG_WARN, TAG, "device limit: giving up on the pairing code");
    S.device_limit = false;
    g_core.pair_code[0] = '\0';
    core_store_erase(GADGET_KEY_PAIR_CODE);
    if (S.st == SS_BACKOFF && g_core.host_id[0] == '\0') halt(HALT_UNPAIRED);
  }
  publish();
}

bool session_ready(void) { return S.st == SS_READY; }

void session_reconnect_now(bool clear_error) {
  if (clear_error) {
    S.error_flag = false;
    S.last_error[0] = '\0';
    S.device_limit = false;
    S.window_known = false;
  }
  S.halt = HALT_NONE;
  S.backoff_ms = GADGET_BACKOFF_MIN_MS;
  S.protocol_error = false;
  S.host_not_found = false;
  S.hosts_printed = false;
  S.resolved[0] = '\0';
  if (S.ws_live) {
    close_conn(1000, AFTER_NOW, HALT_NONE);
  } else {
    S.st = SS_BACKOFF;
    S.retry_at = g_core.now;
  }
}

void session_wake(void) {
  if (S.st == SS_HALTED && S.halt == HALT_REPLACED) session_reconnect_now(false);
}

void session_clear_host_name(void) { S.ch_host_name[0] = '\0'; }

const char *session_host_in_use(void) {
  if (g_core.host_addr[0] != '\0') return g_core.host_addr;
  if (S.resolved[0] != '\0') return S.resolved;
  return NULL;
}

core_pair_state_t session_pair_state(void) {
  if (S.st == SS_READY) return CORE_PAIR_PAIRED;
  /* device_limit: "error" for its whole 120 s window, retries included; on_error
   * set last_error to "device_limit" (contract §2.11) */
  if (S.error_flag || S.device_limit) return CORE_PAIR_ERROR;
  if (g_core.host_id[0] == '\0' && g_core.pair_code[0] == '\0') return CORE_PAIR_UNPAIRED;
  if (session_host_in_use() != NULL) return CORE_PAIR_CONNECTING;
  if (g_core.pair_code[0] != '\0') return CORE_PAIR_CODE_STORED;
  return CORE_PAIR_CONNECTING;
}

const char *session_last_error(void) { return S.last_error; }
bool session_is_setup(void) { return g_core.host_id[0] == '\0'; }
