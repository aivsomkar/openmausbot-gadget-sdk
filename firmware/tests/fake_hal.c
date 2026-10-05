/* firmware/tests/fake_hal.c */
/* SPDX-License-Identifier: Apache-2.0 */
#include "fake_hal.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gadget_proto.h"
#include "gadget_util.h"
#include "unity.h"

#define MAX_KEYS 32
#define MAX_LINES 256
#define MAX_FRAMES 1024
#define MAX_BIN 4096
#define MAX_QUEUE 64
#define OTA_MAX (6u * 1024u * 1024u)

typedef struct {
  char key[16];
  bool used, blob;
  uint8_t data[512];
  size_t len;
} kv_t;

typedef struct {
  gadget_event_type_t type;
  uint16_t code;
  uint32_t written;
  gadget_status_t err;
} queued_t;

static struct {
  uint64_t now;
  kv_t kv[MAX_KEYS];
  int storage_writes;
  char read_error_key[16];
  gadget_status_t read_error;
  char *lines[MAX_LINES];
  size_t n_lines;
  bool log_enabled;
  /* wifi */
  gadget_wifi_state_t wifi;
  char wifi_ssid[33];
  int wifi_connects, wifi_scans;
  bool wifi_start_fails;
  /* ws */
  int ws_opens;
  char ws_host[64];
  uint16_t ws_port;
  bool ws_live;
  uint16_t ws_close_code;
  char *sent[MAX_FRAMES];
  size_t n_sent;
  uint8_t *bin[MAX_BIN];
  size_t bin_len[MAX_BIN];
  size_t n_bin;
  /* mdns */
  int mdns_browses;
  bool mdns_unsupported;
  /* audio */
  bool mic_running;
  int mic_stops;
  uint32_t spk_rate;
  size_t spk_accepted;
  size_t spk_buffered; /* samples queued but not yet "played" */
  int spk_stops;
  int16_t spk_peak;
  /* ota */
  hal_ota_img_state_t ota_running;
  uint8_t *ota_image;
  uint32_t ota_size, ota_written, ota_durable;
  bool ota_finalized, ota_valid, ota_invalidated, ota_fail_writes;
  char ota_boot_version[GADGET_VERSION_MAX + 1];
  int ota_aborts;
  /* battery */
  bool batt_present, batt_charging;
  uint8_t batt_pct;
  /* async results */
  queued_t queue[MAX_QUEUE];
  size_t n_queue;
  int restarts;
} F;

jmp_buf fake_restart_jmp;
volatile int fake_restart_armed;

static void queue_event(queued_t q) {
  TEST_ASSERT_TRUE_MESSAGE(F.n_queue < MAX_QUEUE, "fake event queue full");
  F.queue[F.n_queue++] = q;
}

static void deliver_queue(void) {
  queued_t q[MAX_QUEUE];
  size_t n = F.n_queue;
  memcpy(q, F.queue, n * sizeof q[0]);
  F.n_queue = 0;
  for (size_t i = 0; i < n; i++) {
    gadget_event_t ev = {.type = q[i].type};
    if (q[i].type == GADGET_EV_WS_CLOSED) {
      if (!F.ws_live) continue;
      F.ws_live = false;
      ev.u.closed.code = q[i].code;
    } else if (q[i].type == GADGET_EV_OTA_WRITTEN) {
      ev.u.ota_written.written = q[i].written;
    } else if (q[i].type == GADGET_EV_OTA_ERROR) {
      ev.u.ota_error.err = q[i].err;
    }
    core_event(&ev);
  }
}

void fake_reset(void) {
  for (size_t i = 0; i < F.n_lines; i++) free(F.lines[i]);
  for (size_t i = 0; i < F.n_sent; i++) free(F.sent[i]);
  for (size_t i = 0; i < F.n_bin; i++) free(F.bin[i]);
  free(F.ota_image);
  memset(&F, 0, sizeof F);
  F.log_enabled = true;
  F.wifi = GADGET_WIFI_CONNECTED;
  F.ota_running = HAL_OTA_IMG_VALID;
}

/* ---- clock ------------------------------------------------------------------ */

uint64_t fake_now(void) { return F.now; }

static void drain_speaker(uint32_t ms) {
  if (F.spk_rate == 0) return;
  size_t played = (size_t)F.spk_rate * ms / 1000u;
  F.spk_buffered = played >= F.spk_buffered ? 0 : F.spk_buffered - played;
}

void fake_run(uint32_t ms) {
  for (uint32_t t = 0; t < ms; t += 10) {
    deliver_queue();
    F.now += 10;
    drain_speaker(10);
    core_tick(F.now);
  }
  deliver_queue();
}

/* ---- boot helpers ------------------------------------------------------------- */

void fake_boot_cfg(const core_config_t *cfg) {
  core_deinit();
  TEST_ASSERT_EQUAL_INT(GADGET_OK, core_init(cfg));
  fake_run(10);
}

void fake_boot(const char *board_id) {
  core_config_t cfg = {.board = gadget_board_by_id(board_id), .fw_version = "1.0.0", .prng_seed = 1};
  TEST_ASSERT_NOT_NULL_MESSAGE(cfg.board, board_id);
  fake_boot_cfg(&cfg);
}

void fake_store_paired(void) {
  uint8_t priv[32];
  size_t n = 0;
  gadget_hex_decode(FAKE_RFC_PRIV_HEX, priv, sizeof priv, &n);
  fake_storage_put_blob(GADGET_KEY_DEV_KEY, priv, sizeof priv);
  fake_storage_put(GADGET_KEY_HOST_ADDR, "127.0.0.1:8810");
  fake_storage_put(GADGET_KEY_HOST_ID, FAKE_HOST_ID);
  fake_storage_put(GADGET_KEY_HOST_NAME, "Mac");
  fake_storage_put(GADGET_KEY_BOT_ID, "b_jev");
  fake_storage_put(GADGET_KEY_BOT_NAME, "Jev");
}

void fake_handshake(void) {
  TEST_ASSERT_TRUE_MESSAGE(fake_ws_live(), "no connection is pending");
  size_t base = fake_ws_sent();
  fake_ws_accept();
  TEST_ASSERT_EQUAL_size_t_MESSAGE(base + 1, fake_ws_sent(), "hello not sent on open");
  cJSON *hello = fake_ws_last("hello");
  TEST_ASSERT_NOT_NULL(hello);
  const char *id = cJSON_GetObjectItem(hello, "id")->valuestring;
  uint8_t pub[80];
  size_t pub_len = 0;
  TEST_ASSERT_EQUAL_INT(GADGET_OK, gadget_b64_decode(cJSON_GetObjectItem(hello, "pubkey")->valuestring, pub,
                                                     sizeof pub, &pub_len));
  TEST_ASSERT_EQUAL_size_t(GADGET_PUBKEY_LEN, pub_len);
  char text[256];
  gp_prove_text(text, sizeof text, id, FAKE_NONCE, FAKE_HOST_ID);
  cJSON_Delete(hello);
  fake_ws_in("{\"op\":\"challenge\",\"nonce\":\"" FAKE_NONCE "\",\"host_id\":\"" FAKE_HOST_ID
             "\",\"host_name\":\"Mac\"}");
  cJSON *prove = fake_ws_last("prove");
  TEST_ASSERT_NOT_NULL_MESSAGE(prove, "prove not sent after challenge");
  uint8_t der[96];
  size_t der_len = 0;
  TEST_ASSERT_EQUAL_INT(GADGET_OK, gadget_b64_decode(cJSON_GetObjectItem(prove, "sig")->valuestring, der,
                                                     sizeof der, &der_len));
  TEST_ASSERT_EQUAL_INT_MESSAGE(GADGET_OK, hal_crypto_verify(pub, (const uint8_t *)text, strlen(text), der, der_len),
                                "prove signature does not verify");
  cJSON_Delete(prove);
  fake_ws_in("{\"op\":\"ready\",\"session\":\"s_0123456789ab\",\"bot\":{\"id\":\"b_jev\",\"name\":\"Jev\"},"
             "\"settings\":{\"speak_pushes\":false}}");
  fake_run(10);
  fake_ws_clear();
}

void fake_ready(const char *board_id) {
  fake_store_paired();
  fake_boot(board_id);
  fake_run(10);
  fake_handshake();
}

/* ---- storage ------------------------------------------------------------------ */

static kv_t *kv_find(const char *key) {
  for (int i = 0; i < MAX_KEYS; i++) {
    if (F.kv[i].used && strcmp(F.kv[i].key, key) == 0) return &F.kv[i];
  }
  return NULL;
}

static kv_t *kv_put(const char *key) {
  kv_t *e = kv_find(key);
  if (e) return e;
  for (int i = 0; i < MAX_KEYS; i++) {
    if (!F.kv[i].used) {
      F.kv[i].used = true;
      TEST_ASSERT_TRUE_MESSAGE(strlen(key) <= 15, key);
      strcpy(F.kv[i].key, key);
      return &F.kv[i];
    }
  }
  TEST_FAIL_MESSAGE("fake storage full");
  return NULL;
}

void fake_storage_put(const char *key, const char *value) {
  kv_t *e = kv_put(key);
  e->blob = false;
  e->len = strlen(value);
  memcpy(e->data, value, e->len + 1);
}

void fake_storage_put_blob(const char *key, const void *data, size_t len) {
  kv_t *e = kv_put(key);
  e->blob = true;
  e->len = len;
  memcpy(e->data, data, len);
}

const char *fake_storage_str(const char *key) {
  kv_t *e = kv_find(key);
  return (e && !e->blob) ? (const char *)e->data : NULL;
}

bool fake_storage_has(const char *key) { return kv_find(key) != NULL; }
size_t fake_storage_blob_len(const char *key) {
  kv_t *e = kv_find(key);
  return (e && e->blob) ? e->len : 0;
}
int fake_storage_writes(void) { return F.storage_writes; }

void fake_storage_read_error(const char *key, gadget_status_t err) {
  snprintf(F.read_error_key, sizeof F.read_error_key, "%s", key);
  F.read_error = err;
}

static bool read_fails(const char *key) { return F.read_error != GADGET_OK && strcmp(key, F.read_error_key) == 0; }

gadget_status_t hal_storage_get_str(const char *key, char *buf, size_t cap) {
  if (read_fails(key)) return F.read_error;
  kv_t *e = kv_find(key);
  if (e == NULL || e->blob) return GADGET_ERR_NOT_FOUND;
  if (e->len + 1 > cap) return GADGET_ERR_LIMIT;
  memcpy(buf, e->data, e->len + 1);
  return GADGET_OK;
}

gadget_status_t hal_storage_set_str(const char *key, const char *value) {
  if (strlen(value) >= sizeof F.kv[0].data) return GADGET_ERR_LIMIT;
  F.storage_writes++;
  fake_storage_put(key, value);
  return GADGET_OK;
}

gadget_status_t hal_storage_get_blob(const char *key, void *buf, size_t cap, size_t *len) {
  if (read_fails(key)) return F.read_error;
  kv_t *e = kv_find(key);
  if (e == NULL || !e->blob) return GADGET_ERR_NOT_FOUND;
  if (e->len > cap) return GADGET_ERR_LIMIT;
  memcpy(buf, e->data, e->len);
  *len = e->len;
  return GADGET_OK;
}

gadget_status_t hal_storage_set_blob(const char *key, const void *data, size_t len) {
  if (len > sizeof F.kv[0].data) return GADGET_ERR_LIMIT;
  F.storage_writes++;
  fake_storage_put_blob(key, data, len);
  return GADGET_OK;
}

gadget_status_t hal_storage_erase(const char *key) {
  kv_t *e = kv_find(key);
  if (e) memset(e, 0, sizeof *e);
  return GADGET_OK;
}

gadget_status_t hal_storage_erase_all(void) {
  memset(F.kv, 0, sizeof F.kv);
  return GADGET_OK;
}

/* ---- console -------------------------------------------------------------------- */

void hal_console_write(const char *line) {
  TEST_ASSERT_TRUE_MESSAGE(F.n_lines < MAX_LINES, "fake console full");
  F.lines[F.n_lines] = malloc(strlen(line) + 1);
  strcpy(F.lines[F.n_lines++], line);
}

size_t fake_console_count(void) { return F.n_lines; }
const char *fake_console_line(size_t i) { return i < F.n_lines ? F.lines[i] : NULL; }

cJSON *fake_omb(const char *op) {
  for (size_t i = F.n_lines; i-- > 0;) {
    if (strncmp(F.lines[i], "@omb ", 5) != 0) continue;
    cJSON *j = cJSON_Parse(F.lines[i] + 5);
    const cJSON *o = cJSON_GetObjectItem(j, "op");
    if (cJSON_IsString(o) && strcmp(o->valuestring, op) == 0) return j;
    cJSON_Delete(j);
  }
  return NULL;
}

void fake_console_clear(void) {
  for (size_t i = 0; i < F.n_lines; i++) free(F.lines[i]);
  F.n_lines = 0;
}

void fake_console_in(const char *line) {
  gadget_event_t ev = {.type = GADGET_EV_CONSOLE_LINE};
  ev.u.console.line = line;
  core_event(&ev);
}

/* ---- input, mic, speaker ------------------------------------------------------------ */

void fake_input(gadget_input_type_t type, int16_t x, int16_t y) {
  gadget_event_t ev = {.type = GADGET_EV_INPUT};
  ev.u.input.type = type;
  ev.u.input.x = x;
  ev.u.input.y = y;
  core_event(&ev);
}

void fake_swipe(gadget_swipe_dir_t dir) {
  gadget_event_t ev = {.type = GADGET_EV_INPUT};
  ev.u.input.type = GADGET_IN_SWIPE;
  ev.u.input.dir = dir;
  core_event(&ev);
}

gadget_status_t hal_mic_start(uint32_t rate) {
  if (rate != GADGET_MIC_RATE) return GADGET_ERR_UNSUPPORTED;
  F.mic_running = true;
  return GADGET_OK;
}

void hal_mic_stop(void) {
  F.mic_running = false;
  F.mic_stops++;
}
bool fake_mic_running(void) { return F.mic_running; }
int fake_mic_stops(void) { return F.mic_stops; }

void fake_mic_frames(int n, int16_t amplitude) {
  int16_t pcm[GADGET_MIC_FRAME_SAMPLES];
  for (int i = 0; i < (int)GADGET_MIC_FRAME_SAMPLES; i++) pcm[i] = (i / 8) % 2 ? amplitude : (int16_t)-amplitude;
  for (int k = 0; k < n; k++) {
    gadget_event_t ev = {.type = GADGET_EV_MIC_FRAME};
    ev.u.mic.pcm = pcm;
    ev.u.mic.samples = GADGET_MIC_FRAME_SAMPLES;
    core_event(&ev);
    fake_run(20);
  }
}

gadget_status_t hal_spk_open(uint32_t rate) {
  if (rate != 16000 && rate != 24000) return GADGET_ERR_UNSUPPORTED;
  if (rate != F.spk_rate) F.spk_buffered = 0;
  F.spk_rate = rate;
  return GADGET_OK;
}

size_t hal_spk_write(const int16_t *pcm, size_t samples) {
  if (F.spk_rate == 0) return 0;
  size_t cap = F.spk_rate / 5; /* a 200 ms output buffer */
  size_t room = F.spk_buffered >= cap ? 0 : cap - F.spk_buffered;
  size_t n = samples < room ? samples : room;
  for (size_t i = 0; i < n; i++) {
    int16_t v = pcm[i] < 0 ? (int16_t)-pcm[i] : pcm[i];
    if (v > F.spk_peak) F.spk_peak = v;
  }
  F.spk_buffered += n;
  F.spk_accepted += n;
  return n;
}

uint32_t hal_spk_buffered_ms(void) { return F.spk_rate ? (uint32_t)(F.spk_buffered * 1000u / F.spk_rate) : 0; }
void hal_spk_stop(void) {
  F.spk_buffered = 0;
  F.spk_stops++;
}
void hal_spk_set_volume(uint8_t pct) { (void)pct; }
uint32_t fake_spk_rate(void) { return F.spk_rate; }
size_t fake_spk_accepted(void) { return F.spk_accepted; }
int fake_spk_stops(void) { return F.spk_stops; }
int16_t fake_spk_peak(void) { return F.spk_peak; }

/* ---- Wi-Fi --------------------------------------------------------------------------- */

gadget_status_t hal_wifi_connect(const char *ssid, const char *password) {
  (void)password;
  snprintf(F.wifi_ssid, sizeof F.wifi_ssid, "%s", ssid);
  F.wifi_connects++;
  if (F.wifi_start_fails) return GADGET_ERR_IO;
  /* gadget_hal.h: CONNECTING (or CONNECTED) before this returns; the result
   * event comes from fake_wifi_set() */
  if (F.wifi != GADGET_WIFI_CONNECTED) F.wifi = GADGET_WIFI_CONNECTING;
  return GADGET_OK;
}

void hal_wifi_disconnect(void) { F.wifi = GADGET_WIFI_OFF; }
gadget_wifi_state_t hal_wifi_state(void) { return F.wifi; }

gadget_status_t hal_wifi_scan(void) {
  F.wifi_scans++;
  return F.wifi_start_fails ? GADGET_ERR_IO : GADGET_OK;
}

void fake_wifi_start_fails(bool on) { F.wifi_start_fails = on; }

void fake_wifi_set(gadget_wifi_state_t st) {
  F.wifi = st;
  gadget_event_t ev = {.type = GADGET_EV_WIFI_STATE};
  ev.u.wifi.state = st;
  if (st == GADGET_WIFI_CONNECTED) strcpy(ev.u.wifi.ip, "192.168.1.50");
  core_event(&ev);
}

const char *fake_wifi_ssid(void) { return F.wifi_ssid; }
int fake_wifi_connects(void) { return F.wifi_connects; }
int fake_wifi_scans(void) { return F.wifi_scans; }

void fake_wifi_scan_result(const gadget_wifi_ap_t *aps, uint8_t count, bool ok) {
  gadget_event_t ev = {.type = GADGET_EV_WIFI_SCAN};
  ev.u.scan.aps = aps;
  ev.u.scan.count = count;
  ev.u.scan.ok = ok;
  core_event(&ev);
}

/* ---- WebSocket ------------------------------------------------------------------------- */

gadget_status_t hal_ws_open(const char *host, uint16_t port) {
  if (F.ws_live) return GADGET_ERR_BUSY;
  F.ws_opens++;
  snprintf(F.ws_host, sizeof F.ws_host, "%s", host);
  F.ws_port = port;
  F.ws_live = true;
  F.ws_close_code = 0;
  return GADGET_OK;
}

gadget_status_t hal_ws_send_text(const char *data, size_t len) {
  if (!F.ws_live) return GADGET_ERR_BUSY;
  TEST_ASSERT_TRUE_MESSAGE(len <= GADGET_TEXT_FRAME_MAX, "text frame over 16 KiB");
  TEST_ASSERT_TRUE_MESSAGE(F.n_sent < MAX_FRAMES, "fake sent log full");
  F.sent[F.n_sent] = malloc(len + 1);
  memcpy(F.sent[F.n_sent], data, len);
  F.sent[F.n_sent++][len] = '\0';
  return GADGET_OK;
}

gadget_status_t hal_ws_send_binary(const uint8_t *data, size_t len) {
  if (!F.ws_live) return GADGET_ERR_BUSY;
  TEST_ASSERT_TRUE_MESSAGE(len <= GADGET_BINARY_FRAME_MAX, "binary frame over 8 KiB");
  TEST_ASSERT_TRUE_MESSAGE(F.n_bin < MAX_BIN, "fake binary log full");
  F.bin[F.n_bin] = malloc(len);
  memcpy(F.bin[F.n_bin], data, len);
  F.bin_len[F.n_bin++] = len;
  return GADGET_OK;
}

void hal_ws_close(uint16_t code) {
  if (!F.ws_live) return;
  F.ws_close_code = code ? code : 1000;
  queue_event((queued_t){.type = GADGET_EV_WS_CLOSED, .code = code});
}

int fake_ws_opens(void) { return F.ws_opens; }
const char *fake_ws_host(void) { return F.ws_host; }
uint16_t fake_ws_port(void) { return F.ws_port; }
bool fake_ws_live(void) { return F.ws_live; }
uint16_t fake_ws_close_code(void) { return F.ws_close_code; }

void fake_ws_accept(void) {
  gadget_event_t ev = {.type = GADGET_EV_WS_OPEN};
  core_event(&ev);
}

void fake_ws_in(const char *json) {
  gadget_event_t ev = {.type = GADGET_EV_WS_TEXT};
  ev.u.ws.data = (const uint8_t *)json;
  ev.u.ws.len = strlen(json);
  core_event(&ev);
}

void fake_ws_bin_in(const uint8_t *data, size_t len) {
  gadget_event_t ev = {.type = GADGET_EV_WS_BINARY};
  ev.u.ws.data = data;
  ev.u.ws.len = len;
  core_event(&ev);
}

void fake_ws_ping_in(void) {
  gadget_event_t ev = {.type = GADGET_EV_WS_CONTROL};
  core_event(&ev);
}

void fake_ws_drop(uint16_t code) {
  if (!F.ws_live) return;
  F.ws_live = false;
  gadget_event_t ev = {.type = GADGET_EV_WS_CLOSED};
  ev.u.closed.code = code;
  core_event(&ev);
}

size_t fake_ws_sent(void) { return F.n_sent; }
const char *fake_ws_text(size_t i) { return i < F.n_sent ? F.sent[i] : NULL; }

static bool frame_has_op(const char *frame, const char *op) {
  cJSON *j = cJSON_Parse(frame);
  const cJSON *o = cJSON_GetObjectItem(j, "op");
  bool hit = cJSON_IsString(o) && strcmp(o->valuestring, op) == 0;
  cJSON_Delete(j);
  return hit;
}

size_t fake_ws_count(const char *op) {
  size_t n = 0;
  for (size_t i = 0; i < F.n_sent; i++) n += frame_has_op(F.sent[i], op);
  return n;
}

cJSON *fake_ws_last(const char *op) {
  for (size_t i = F.n_sent; i-- > 0;) {
    if (frame_has_op(F.sent[i], op)) return cJSON_Parse(F.sent[i]);
  }
  return NULL;
}

size_t fake_ws_bin_sent(void) { return F.n_bin; }
const uint8_t *fake_ws_bin(size_t i, size_t *len) {
  if (i >= F.n_bin) return NULL;
  *len = F.bin_len[i];
  return F.bin[i];
}

void fake_ws_clear(void) {
  for (size_t i = 0; i < F.n_sent; i++) free(F.sent[i]);
  for (size_t i = 0; i < F.n_bin; i++) free(F.bin[i]);
  F.n_sent = 0;
  F.n_bin = 0;
}

/* ---- mDNS -------------------------------------------------------------------------------- */

gadget_status_t hal_mdns_browse(uint32_t timeout_ms) {
  (void)timeout_ms;
  if (F.mdns_unsupported) return GADGET_ERR_UNSUPPORTED;
  F.mdns_browses++;
  return GADGET_OK;
}

int fake_mdns_browses(void) { return F.mdns_browses; }
void fake_mdns_unsupported(bool on) { F.mdns_unsupported = on; }

void fake_mdns_result(const gadget_mdns_host_t *hosts, uint8_t count) {
  gadget_event_t ev = {.type = GADGET_EV_MDNS};
  ev.u.mdns.hosts = hosts;
  ev.u.mdns.count = count;
  ev.u.mdns.ok = true;
  core_event(&ev);
}

void fake_mdns_fail(void) {
  gadget_event_t ev = {.type = GADGET_EV_MDNS};
  ev.u.mdns.ok = false;
  core_event(&ev);
}

/* ---- OTA ---------------------------------------------------------------------------------- */

gadget_status_t hal_ota_begin(uint32_t size) {
  if (size == 0 || size > OTA_MAX) return GADGET_ERR_LIMIT;
  free(F.ota_image);
  F.ota_image = calloc(1, size);
  F.ota_size = size;
  F.ota_written = F.ota_durable = 0;
  F.ota_finalized = false;
  return GADGET_OK;
}

gadget_status_t hal_ota_write(uint32_t offset, const uint8_t *data, size_t len) {
  if (F.ota_image == NULL || offset != F.ota_written || offset + len > F.ota_size) return GADGET_ERR_STATE;
  memcpy(F.ota_image + offset, data, len);
  F.ota_written += (uint32_t)len;
  if (F.ota_fail_writes) {
    queue_event((queued_t){.type = GADGET_EV_OTA_ERROR, .err = GADGET_ERR_IO});
  } else {
    queue_event((queued_t){.type = GADGET_EV_OTA_WRITTEN, .written = F.ota_written});
  }
  return GADGET_OK;
}

gadget_status_t hal_ota_finalize(void) {
  if (F.ota_image == NULL || F.ota_written != F.ota_size) return GADGET_ERR_STATE;
  F.ota_finalized = true;
  return GADGET_OK;
}

gadget_status_t hal_ota_set_boot(const char *version) {
  if (!F.ota_finalized) return GADGET_ERR_STATE;
  snprintf(F.ota_boot_version, sizeof F.ota_boot_version, "%s", version);
  return GADGET_OK;
}

void hal_ota_abort(void) {
  F.ota_aborts++;
  free(F.ota_image);
  F.ota_image = NULL;
  F.ota_size = F.ota_written = 0;
}

hal_ota_img_state_t hal_ota_running_state(void) { return F.ota_running; }

gadget_status_t hal_ota_mark_valid(void) {
  F.ota_valid = true;
  F.ota_running = HAL_OTA_IMG_VALID;
  return GADGET_OK;
}

void fake_ota_set_running(hal_ota_img_state_t st) { F.ota_running = st; }
uint32_t fake_ota_size(void) { return F.ota_size; }
uint32_t fake_ota_written(void) { return F.ota_written; }
const uint8_t *fake_ota_image(void) { return F.ota_image; }
bool fake_ota_finalized(void) { return F.ota_finalized; }
const char *fake_ota_boot_version(void) { return F.ota_boot_version; }
int fake_ota_aborts(void) { return F.ota_aborts; }
bool fake_ota_marked_valid(void) { return F.ota_valid; }
bool fake_ota_invalidated(void) { return F.ota_invalidated; }
void fake_ota_fail_writes(bool on) { F.ota_fail_writes = on; }

/* ---- battery, system ------------------------------------------------------------------------ */

bool hal_battery_read(gadget_battery_t *out) {
  if (!F.batt_present) return false;
  out->pct = F.batt_pct;
  out->charging = F.batt_charging;
  return true;
}

void fake_battery_set(bool present, uint8_t pct, bool charging) {
  F.batt_present = present;
  F.batt_pct = pct;
  F.batt_charging = charging;
}

uint64_t hal_now_ms(void) { return F.now; }

static _Noreturn void jump_restart(void) {
  F.restarts++;
  if (!fake_restart_armed) {
    fprintf(stderr, "fake_hal: unexpected restart\n");
    abort();
  }
  F.ws_live = false;
  F.n_queue = 0;
  longjmp(fake_restart_jmp, 1);
}

_Noreturn void hal_restart(void) { jump_restart(); }

_Noreturn void hal_ota_mark_invalid_and_reboot(void) {
  F.ota_invalidated = true;
  jump_restart();
}

int fake_restarts(void) { return F.restarts; }

void hal_vlog(gadget_log_level_t level, const char *tag, const char *fmt, va_list ap) {
  if (!F.log_enabled || getenv("FAKE_HAL_LOG") == NULL) return;
  fprintf(stderr, "[%d] %s: ", (int)level, tag);
  vfprintf(stderr, fmt, ap);
  fputc('\n', stderr);
}

void hal_log(gadget_log_level_t level, const char *tag, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  hal_vlog(level, tag, fmt, ap);
  va_end(ap);
}

void hal_log_set_enabled(bool enabled) { F.log_enabled = enabled; }
bool fake_log_enabled(void) { return F.log_enabled; }
