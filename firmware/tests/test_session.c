/* firmware/tests/test_session.c */
/* SPDX-License-Identifier: Apache-2.0 */
/* The connection state machine (core/src/session.c, spec §4.3) and screen
 * selection without a session (core/src/screens.c). */
#include <string.h>
#include "fake_hal.h"
#include "gadget_util.h"
#include "psa/crypto.h"
#include "unity.h"

void setUp(void) { fake_reset(); }
void tearDown(void) { core_deinit(); }

static const char *READY =
    "{\"op\":\"ready\",\"session\":\"s_0123456789ab\",\"bot\":{\"id\":\"b_jev\",\"name\":\"Jev\"},"
    "\"settings\":{\"speak_pushes\":true}}";
static const char *CHALLENGE =
    "{\"op\":\"challenge\",\"nonce\":\"" FAKE_NONCE "\",\"host_id\":\"" FAKE_HOST_ID "\",\"host_name\":\"Omkar's Mac\"}";

static void store_code_only(void) {
  fake_storage_put(GADGET_KEY_HOST_ADDR, "192.168.1.20:8810");
  fake_storage_put(GADGET_KEY_PAIR_CODE, "123456");
}

/* Open, hello, challenge; returns with the prove sent. */
static void to_prove(void) {
  TEST_ASSERT_TRUE(fake_ws_live());
  fake_ws_accept();
  fake_ws_in(CHALLENGE);
}

static void host_error(const char *code) {
  char json[96];
  snprintf(json, sizeof json, "{\"op\":\"error\",\"code\":\"%s\",\"message\":\"x\"}", code);
  fake_ws_in(json);
  fake_run(10); /* the close core asked for is delivered */
}

static void test_unpaired_gadget_waits_on_setup(void) {
  fake_boot("amoled-175c");
  fake_run(5000);
  TEST_ASSERT_EQUAL_INT(0, fake_ws_opens());
  TEST_ASSERT_EQUAL_INT(CORE_PAIR_UNPAIRED, core_pair_state());
  const ui_model_t *m = core_ui_model();
  TEST_ASSERT_EQUAL_INT(UI_SCREEN_SETUP, m->screen);
  TEST_ASSERT_EQUAL_INT(UI_MAUS_CURIOUS, m->maus);
  TEST_ASSERT_EQUAL_INT(UI_SETUP_NEED_CODE, m->setup.step);
}

static void test_setup_asks_for_wifi_first(void) {
  fake_wifi_set(GADGET_WIFI_OFF);
  fake_boot("lcd-154");
  TEST_ASSERT_EQUAL_INT(UI_SETUP_NEED_WIFI, core_ui_model()->setup.step);
}

static void test_paired_gadget_connects_and_becomes_ready(void) {
  fake_store_paired();
  fake_boot("amoled-175c");
  TEST_ASSERT_EQUAL_INT(1, fake_ws_opens());
  TEST_ASSERT_EQUAL_STRING("127.0.0.1", fake_ws_host());
  TEST_ASSERT_EQUAL_UINT16(8810, fake_ws_port());
  TEST_ASSERT_EQUAL_INT(CORE_PAIR_CONNECTING, core_pair_state());
  TEST_ASSERT_EQUAL_INT(UI_SCREEN_OFFLINE, core_ui_model()->screen);
  TEST_ASSERT_EQUAL_INT(UI_OFFLINE_HOST_LOOKUP, core_ui_model()->offline.reason);
  fake_ws_accept();
  cJSON *hello = fake_ws_last("hello");
  TEST_ASSERT_EQUAL_INT(1, cJSON_GetObjectItem(hello, "proto")->valueint);
  TEST_ASSERT_EQUAL_STRING(FAKE_RFC_ID, cJSON_GetObjectItem(hello, "id")->valuestring);
  TEST_ASSERT_EQUAL_STRING("Maus b18b", cJSON_GetObjectItem(hello, "name")->valuestring);
  TEST_ASSERT_EQUAL_STRING("amoled-175c", cJSON_GetObjectItem(hello, "board")->valuestring);
  TEST_ASSERT_EQUAL_STRING("1.0.0", cJSON_GetObjectItem(hello, "fw")->valuestring);
  TEST_ASSERT_TRUE(cJSON_IsObject(cJSON_GetObjectItem(hello, "caps")));
  cJSON_Delete(hello);
  fake_ws_in(CHALLENGE);
  cJSON *prove = fake_ws_last("prove");
  TEST_ASSERT_NULL(cJSON_GetObjectItem(prove, "enroll")); /* a paired gadget holds no code */
  cJSON_Delete(prove);
  fake_ws_in(READY);
  fake_run(10);
  TEST_ASSERT_EQUAL_INT(CORE_PAIR_PAIRED, core_pair_state());
  TEST_ASSERT_EQUAL_INT(UI_SCREEN_IDLE, core_ui_model()->screen);
  TEST_ASSERT_EQUAL_INT(UI_MAUS_IDLE, core_ui_model()->maus);
  TEST_ASSERT_EQUAL_STRING("Omkar's Mac", core_ui_model()->host_name);
  TEST_ASSERT_EQUAL_STRING("1", fake_storage_str(GADGET_KEY_SPEAK_PUSH));
}

static void test_handshake_helper_verifies_the_prove_signature(void) {
  fake_ready("lcd-154"); /* asserts internally that the signature verifies */
  TEST_ASSERT_EQUAL_INT(CORE_PAIR_PAIRED, core_pair_state());
}

static void test_enrollment_sends_the_code_and_stores_the_host(void) {
  store_code_only();
  fake_boot("lcd-154");
  TEST_ASSERT_EQUAL_INT(CORE_PAIR_CONNECTING, core_pair_state());
  TEST_ASSERT_EQUAL_STRING("192.168.1.20", fake_ws_host());
  to_prove();
  cJSON *prove = fake_ws_last("prove");
  TEST_ASSERT_EQUAL_STRING("123456", cJSON_GetObjectItem(prove, "enroll")->valuestring);
  cJSON_Delete(prove);
  TEST_ASSERT_EQUAL_INT(UI_SCREEN_SETUP, core_ui_model()->screen);
  TEST_ASSERT_EQUAL_INT(UI_SETUP_PAIRING, core_ui_model()->setup.step);
  TEST_ASSERT_EQUAL_STRING("Omkar's Mac", core_ui_model()->host_name); /* shown once a challenge arrived */
  fake_ws_in(READY);
  fake_run(10);
  TEST_ASSERT_EQUAL_STRING(FAKE_HOST_ID, fake_storage_str(GADGET_KEY_HOST_ID));
  TEST_ASSERT_EQUAL_STRING("Omkar's Mac", fake_storage_str(GADGET_KEY_HOST_NAME));
  TEST_ASSERT_FALSE(fake_storage_has(GADGET_KEY_PAIR_CODE));
  TEST_ASSERT_EQUAL_STRING("b_jev", fake_storage_str(GADGET_KEY_BOT_ID));
  TEST_ASSERT_EQUAL_STRING("Jev", fake_storage_str(GADGET_KEY_BOT_NAME));
  TEST_ASSERT_EQUAL_STRING("Jev", core_ui_model()->bot_name);
}

static void test_challenge_with_a_bad_host_id_closes_and_backs_off(void) {
  fake_store_paired();
  fake_boot("amoled-175c");
  fake_ws_accept();
  fake_ws_in("{\"op\":\"challenge\",\"nonce\":\"" FAKE_NONCE "\",\"host_id\":\"h_0123456789abcdef\",\"host_name\":\"x\"}");
  TEST_ASSERT_EQUAL_size_t(0, fake_ws_count("prove"));
  TEST_ASSERT_EQUAL_UINT16(1002, fake_ws_close_code());
  fake_run(10);
  TEST_ASSERT_EQUAL_INT(UI_OFFLINE_PROTOCOL, core_ui_model()->offline.reason);
  TEST_ASSERT_TRUE(core_ui_model()->offline.retry_at_ms > 0);
  fake_run(1970);
  TEST_ASSERT_EQUAL_INT(1, fake_ws_opens());
  fake_run(30);
  TEST_ASSERT_EQUAL_INT(2, fake_ws_opens());
}

static void test_bad_code_clears_the_code_and_stops(void) {
  store_code_only();
  fake_boot("lcd-154");
  to_prove();
  host_error("bad_code");
  TEST_ASSERT_FALSE(fake_storage_has(GADGET_KEY_PAIR_CODE));
  TEST_ASSERT_EQUAL_INT(CORE_PAIR_ERROR, core_pair_state());
  TEST_ASSERT_EQUAL_STRING("bad_code", core_last_error());
  TEST_ASSERT_EQUAL_INT(UI_SETUP_BAD_CODE, core_ui_model()->setup.step);
  fake_run(130000);
  TEST_ASSERT_EQUAL_INT(1, fake_ws_opens()); /* no retry until a new pair command */
}

/* Spec 5.5: Setup shows host_name once a challenge has arrived, also after
 * the connection that brought it has closed. */
static void test_setup_keeps_the_challenge_host_name_after_bad_code(void) {
  store_code_only();
  fake_boot("lcd-154");
  TEST_ASSERT_EQUAL_STRING("", core_ui_model()->host_name); /* no challenge yet */
  fake_ws_accept();
  fake_ws_in("{\"op\":\"challenge\",\"nonce\":\"" FAKE_NONCE "\",\"host_id\":\"" FAKE_HOST_ID "\",\"host_name\":\"Mac\"}");
  host_error("bad_code");
  TEST_ASSERT_FALSE(fake_ws_live()); /* the connection that brought the name is closed */
  TEST_ASSERT_EQUAL_INT(UI_SCREEN_SETUP, core_ui_model()->screen);
  TEST_ASSERT_EQUAL_INT(UI_SETUP_BAD_CODE, core_ui_model()->setup.step);
  TEST_ASSERT_EQUAL_STRING("Mac", core_ui_model()->host_name);
  TEST_ASSERT_FALSE(fake_storage_has(GADGET_KEY_HOST_NAME)); /* stored only on ready */
}

static void test_enroll_required_and_revoked_unpair(void) {
  fake_store_paired();
  fake_boot("amoled-175c");
  to_prove();
  host_error("enroll_required");
  TEST_ASSERT_FALSE(fake_storage_has(GADGET_KEY_HOST_ID));
  TEST_ASSERT_EQUAL_INT(CORE_PAIR_UNPAIRED, core_pair_state());
  TEST_ASSERT_EQUAL_INT(UI_SCREEN_SETUP, core_ui_model()->screen);
  fake_run(70000);
  TEST_ASSERT_EQUAL_INT(1, fake_ws_opens());

  fake_reset();
  fake_ready("amoled-175c");
  fake_ws_in("{\"op\":\"error\",\"code\":\"revoked\"}");
  fake_run(10);
  TEST_ASSERT_FALSE(fake_storage_has(GADGET_KEY_HOST_ID));
  TEST_ASSERT_EQUAL_INT(CORE_PAIR_UNPAIRED, core_pair_state());
  TEST_ASSERT_EQUAL_INT(UI_SCREEN_SETUP, core_ui_model()->screen);
  fake_run(70000);
  TEST_ASSERT_EQUAL_INT(1, fake_ws_opens()); /* no automatic reconnect */
}

static void test_device_limit_retries_every_10_s_for_120_s(void) {
  store_code_only();
  fake_boot("lcd-154");
  to_prove();
  host_error("device_limit");
  TEST_ASSERT_TRUE(fake_storage_has(GADGET_KEY_PAIR_CODE));
  TEST_ASSERT_EQUAL_INT(UI_SETUP_DEVICE_LIMIT, core_ui_model()->setup.step);
  TEST_ASSERT_EQUAL_STRING("Omkar's Mac", core_ui_model()->host_name); /* kept after the close */
  /* pair "error" with "device_limit" for the whole window (contract §2.11), so
   * P2d's installer can say why instead of timing out */
  TEST_ASSERT_EQUAL_INT(CORE_PAIR_ERROR, core_pair_state());
  TEST_ASSERT_EQUAL_STRING("device_limit", core_last_error());
  int opens = fake_ws_opens();
  fake_run(9900);
  TEST_ASSERT_EQUAL_INT(opens, fake_ws_opens());
  fake_run(200);
  TEST_ASSERT_EQUAL_INT(opens + 1, fake_ws_opens());
  TEST_ASSERT_EQUAL_INT(CORE_PAIR_ERROR, core_pair_state()); /* also while a retry connects */
  /* keep answering device_limit until the window closes */
  uint64_t gave_up = 0;
  while (gave_up == 0 && fake_now() < 200000) {
    if (fake_ws_live()) {
      to_prove();
      host_error("device_limit");
    }
    fake_run(100);
    if (!fake_storage_has(GADGET_KEY_PAIR_CODE)) {
      gave_up = fake_now();
    } else {
      TEST_ASSERT_EQUAL_INT(CORE_PAIR_ERROR, core_pair_state());
      TEST_ASSERT_EQUAL_STRING("device_limit", core_last_error());
    }
  }
  TEST_ASSERT_TRUE(gave_up >= 120000 && gave_up <= 120200); /* 120 s after the code was stored */
  TEST_ASSERT_EQUAL_INT(CORE_PAIR_UNPAIRED, core_pair_state());
  opens = fake_ws_opens();
  fake_run(60000);
  TEST_ASSERT_EQUAL_INT(opens, fake_ws_opens());
}

static void test_replaced_shows_in_use_and_stays_offline(void) {
  fake_ready("amoled-175c");
  fake_ws_in("{\"op\":\"error\",\"code\":\"replaced\"}");
  fake_run(10);
  const ui_model_t *m = core_ui_model();
  TEST_ASSERT_EQUAL_INT(UI_SCREEN_OFFLINE, m->screen);
  TEST_ASSERT_EQUAL_INT(UI_MAUS_SLEEPING, m->maus);
  TEST_ASSERT_EQUAL_INT(UI_OFFLINE_IN_USE_ELSEWHERE, m->offline.reason);
  TEST_ASSERT_EQUAL_UINT64(0, m->offline.retry_at_ms);
  fake_run(120000);
  TEST_ASSERT_EQUAL_INT(1, fake_ws_opens());
}

static void test_protocol_errors_back_off_2_4_8_up_to_60_s(void) {
  fake_store_paired();
  fake_boot("amoled-175c");
  to_prove();
  host_error("bad_sig");
  TEST_ASSERT_EQUAL_INT(CORE_PAIR_ERROR, core_pair_state());
  TEST_ASSERT_EQUAL_STRING("bad_sig", core_last_error());
  static const uint32_t gaps[] = {2000, 4000, 8000, 16000, 32000, 60000, 60000};
  for (size_t i = 0; i < sizeof gaps / sizeof gaps[0]; i++) {
    int opens = fake_ws_opens();
    fake_run(gaps[i] - 20);
    TEST_ASSERT_EQUAL_INT_MESSAGE(opens, fake_ws_opens(), "retried too early");
    fake_run(20);
    TEST_ASSERT_EQUAL_INT_MESSAGE(opens + 1, fake_ws_opens(), "did not retry on time");
    fake_ws_drop(0); /* the connect fails */
  }
}

static void test_proto_unsupported_errors_and_backs_off(void) {
  fake_store_paired();
  fake_boot("amoled-175c");
  to_prove();
  host_error("proto_unsupported");
  TEST_ASSERT_EQUAL_INT(CORE_PAIR_ERROR, core_pair_state());
  TEST_ASSERT_EQUAL_STRING("proto_unsupported", core_last_error());
  TEST_ASSERT_EQUAL_INT(UI_OFFLINE_PROTOCOL, core_ui_model()->offline.reason);
  fake_run(1970);
  TEST_ASSERT_EQUAL_INT(1, fake_ws_opens());
  fake_run(30);
  TEST_ASSERT_EQUAL_INT(2, fake_ws_opens());
}

static void test_ready_resets_the_backoff(void) {
  fake_store_paired();
  fake_boot("amoled-175c");
  fake_ws_drop(0);
  fake_run(2000);
  fake_ws_drop(0);
  fake_run(4000);
  TEST_ASSERT_EQUAL_INT(3, fake_ws_opens());
  fake_handshake();
  fake_ws_drop(1006); /* a dropped session */
  TEST_ASSERT_EQUAL_INT(UI_OFFLINE_HOST_UNREACHABLE, core_ui_model()->offline.reason);
  fake_run(2000);
  TEST_ASSERT_EQUAL_INT(4, fake_ws_opens());
}

static void test_45_s_of_silence_closes_the_session(void) {
  fake_ready("amoled-175c");
  fake_run(30000);
  fake_ws_ping_in(); /* a ping counts as inbound */
  fake_run(44000);
  TEST_ASSERT_EQUAL_UINT16(0, fake_ws_close_code());
  fake_run(1100);
  TEST_ASSERT_EQUAL_UINT16(1001, fake_ws_close_code());
  TEST_ASSERT_EQUAL_INT(UI_SCREEN_OFFLINE, core_ui_model()->screen);
  fake_run(2000);
  TEST_ASSERT_EQUAL_INT(2, fake_ws_opens());
}

static void test_host_auto_picks_the_stored_host_id(void) {
  fake_store_paired();
  fake_storage_put(GADGET_KEY_HOST_ADDR, "");
  fake_boot("amoled-175c");
  TEST_ASSERT_EQUAL_INT(1, fake_mdns_browses());
  TEST_ASSERT_EQUAL_INT(0, fake_ws_opens());
  gadget_mdns_host_t hosts[2] = {
      {.name = "Other Mac", .address = "192.168.1.9:8810", .id = "ffffffffffffffffffffffffffffffff"},
      {.name = "Omkar's Mac", .address = "192.168.1.20:8810", .id = FAKE_HOST_ID},
  };
  fake_mdns_result(hosts, 2);
  TEST_ASSERT_EQUAL_INT(1, fake_ws_opens());
  TEST_ASSERT_EQUAL_STRING("192.168.1.20", fake_ws_host());
  /* a failed connect forgets the resolved address and browses again */
  fake_ws_drop(0);
  fake_run(2000);
  TEST_ASSERT_EQUAL_INT(2, fake_mdns_browses());
  /* a paired gadget whose MausBot is not advertised keeps looking, with backoff */
  gadget_mdns_host_t other = {.name = "Other Mac", .address = "192.168.1.9:8810", .id = "ffffffffffffffffffffffffffffffff"};
  fake_console_clear();
  fake_mdns_result(&other, 1);
  TEST_ASSERT_EQUAL_INT(1, fake_ws_opens());
  cJSON *line = fake_omb("hosts");
  TEST_ASSERT_NOT_NULL(line);
  cJSON_Delete(line);
  fake_run(4000);
  TEST_ASSERT_EQUAL_INT(3, fake_mdns_browses());
  /* the next miss of the same run prints no second hosts line */
  fake_console_clear();
  fake_mdns_result(&other, 1);
  TEST_ASSERT_NULL(fake_omb("hosts"));
  fake_run(8000);
  TEST_ASSERT_EQUAL_INT(4, fake_mdns_browses());
}

static void test_host_auto_before_pairing(void) {
  fake_storage_put(GADGET_KEY_PAIR_CODE, "123456");
  fake_boot("lcd-154");
  TEST_ASSERT_EQUAL_INT(CORE_PAIR_CODE_STORED, core_pair_state());
  gadget_mdns_host_t two[2] = {
      {.name = "A", .address = "10.0.0.1:8810", .id = "0123456789abcdef0123456789abcdef"},
      {.name = "B", .address = "10.0.0.2:8810", .id = ""},
  };
  fake_mdns_result(two, 2);
  cJSON *line = fake_omb("hosts");
  TEST_ASSERT_NOT_NULL(line);
  TEST_ASSERT_EQUAL_INT(2, cJSON_GetArraySize(cJSON_GetObjectItem(line, "hosts")));
  cJSON *first = cJSON_GetArrayItem(cJSON_GetObjectItem(line, "hosts"), 0);
  TEST_ASSERT_EQUAL_STRING("A", cJSON_GetObjectItem(first, "name")->valuestring);
  TEST_ASSERT_EQUAL_STRING("10.0.0.1:8810", cJSON_GetObjectItem(first, "address")->valuestring);
  TEST_ASSERT_EQUAL_STRING("0123456789abcdef0123456789abcdef", cJSON_GetObjectItem(first, "id")->valuestring);
  cJSON_Delete(line);
  TEST_ASSERT_EQUAL_INT(UI_SETUP_HOST_NOT_FOUND, core_ui_model()->setup.step);
  TEST_ASSERT_EQUAL_INT(0, fake_ws_opens());
  fake_run(30000);
  TEST_ASSERT_EQUAL_INT(1, fake_mdns_browses()); /* it waits for host <address> (spec §5.6) */
  TEST_ASSERT_EQUAL_INT(CORE_PAIR_CODE_STORED, core_pair_state());
  /* after a restart: nothing found within 5 s prints an empty list, and waits again */
  fake_boot("lcd-154");
  TEST_ASSERT_EQUAL_INT(2, fake_mdns_browses());
  fake_console_clear();
  fake_run(6100);
  line = fake_omb("hosts");
  TEST_ASSERT_NOT_NULL(line);
  TEST_ASSERT_EQUAL_INT(0, cJSON_GetArraySize(cJSON_GetObjectItem(line, "hosts")));
  cJSON_Delete(line);
  fake_run(30000);
  TEST_ASSERT_EQUAL_INT(2, fake_mdns_browses());
  /* exactly one service: use it */
  fake_boot("lcd-154");
  gadget_mdns_host_t one = {.name = "A", .address = "10.0.0.1:9000", .id = ""};
  fake_mdns_result(&one, 1);
  TEST_ASSERT_EQUAL_STRING("10.0.0.1", fake_ws_host());
  TEST_ASSERT_EQUAL_UINT16(9000, fake_ws_port());
  TEST_ASSERT_EQUAL_INT(CORE_PAIR_CONNECTING, core_pair_state());
}

static void test_host_auto_unsupported_prints_empty_hosts_and_waits(void) {
  fake_mdns_unsupported(true);
  fake_storage_put(GADGET_KEY_PAIR_CODE, "123456");
  fake_boot("devkit");
  cJSON *line = fake_omb("hosts");
  TEST_ASSERT_NOT_NULL(line);
  cJSON_Delete(line);
  TEST_ASSERT_EQUAL_INT(UI_SETUP_HOST_NOT_FOUND, core_ui_model()->setup.step);
  fake_console_clear();
  fake_run(30000);
  TEST_ASSERT_NULL(fake_omb("hosts")); /* halted: waits for host <address> */
}

static void test_wifi_drop_and_return(void) {
  fake_store_paired();
  fake_storage_put(GADGET_KEY_WIFI_SSID, "Home");
  fake_wifi_set(GADGET_WIFI_CONNECTING);
  fake_boot("lcd-154");
  TEST_ASSERT_EQUAL_INT(0, fake_ws_opens());
  TEST_ASSERT_EQUAL_INT(UI_OFFLINE_WIFI_CONNECTING, core_ui_model()->offline.reason);
  fake_wifi_set(GADGET_WIFI_FAILED);
  fake_run(10);
  TEST_ASSERT_EQUAL_INT(UI_OFFLINE_WIFI_FAILED, core_ui_model()->offline.reason);
  TEST_ASSERT_EQUAL_STRING("Home", core_ui_model()->offline.ssid);
  fake_wifi_set(GADGET_WIFI_CONNECTED);
  fake_run(10);
  TEST_ASSERT_EQUAL_INT(1, fake_ws_opens());
  fake_handshake();
  fake_wifi_set(GADGET_WIFI_CONNECTING); /* losing Wi-Fi closes the session */
  fake_run(10);
  TEST_ASSERT_FALSE(fake_ws_live());
  TEST_ASSERT_EQUAL_INT(UI_SCREEN_OFFLINE, core_ui_model()->screen);
}

static void test_settings_update_bot_name_and_speak_pushes(void) {
  fake_ready("amoled-175c");
  fake_ws_in("{\"op\":\"settings\",\"bot\":{\"id\":\"b_ada\",\"name\":\"Ada\"},\"settings\":{\"speak_pushes\":true},"
             "\"name\":\"Desk Maus\"}");
  fake_run(10);
  TEST_ASSERT_EQUAL_STRING("Ada", core_ui_model()->bot_name);
  TEST_ASSERT_EQUAL_STRING("b_ada", fake_storage_str(GADGET_KEY_BOT_ID));
  TEST_ASSERT_EQUAL_STRING("1", fake_storage_str(GADGET_KEY_SPEAK_PUSH));
  TEST_ASSERT_EQUAL_STRING("Desk Maus", fake_storage_str(GADGET_KEY_NAME));
  TEST_ASSERT_EQUAL_STRING("Desk Maus", core_ui_model()->device_name);
  /* the next hello carries the new name */
  fake_ws_drop(1006);
  fake_run(2000);
  fake_ws_accept();
  cJSON *hello = fake_ws_last("hello");
  TEST_ASSERT_EQUAL_STRING("Desk Maus", cJSON_GetObjectItem(hello, "name")->valuestring);
  cJSON_Delete(hello);
}

static void test_names_are_cut_to_32_characters(void) {
  fake_store_paired();
  core_config_t cfg = {.board = gadget_board_by_id("amoled-175c"), .fw_version = "1.0.0", .prng_seed = 1,
                       .default_name = "Kitchen counter Maus by the window, left"}; /* 40 */
  fake_boot_cfg(&cfg);
  fake_ws_accept();
  cJSON *hello = fake_ws_last("hello");
  const char *name = cJSON_GetObjectItem(hello, "name")->valuestring;
  TEST_ASSERT_EQUAL_size_t(32, gadget_utf8_len(name)); /* spec §4.3 */
  TEST_ASSERT_EQUAL_STRING("Kitchen counter Maus by the win\xE2\x80\xA6", name);
  cJSON_Delete(hello);
  fake_ws_in(CHALLENGE);
  fake_ws_in(READY);
  fake_ws_in("{\"op\":\"settings\",\"name\":\"A desktop rename that is far too long\"}"); /* 37 */
  fake_run(10);
  TEST_ASSERT_EQUAL_STRING("A desktop rename that is far to\xE2\x80\xA6", core_ui_model()->device_name);
  TEST_ASSERT_EQUAL_STRING(core_ui_model()->device_name, fake_storage_str(GADGET_KEY_NAME));
}

static void test_unknown_and_malformed_frames_are_ignored(void) {
  fake_ready("amoled-175c");
  fake_ws_in("{\"op\":\"future.op\",\"x\":1}");
  fake_ws_in("not json");
  fake_ws_in("{\"op\":\"reply\"}");
  fake_run(10);
  TEST_ASSERT_EQUAL_INT(CORE_PAIR_PAIRED, core_pair_state());
  TEST_ASSERT_FALSE(fake_ws_close_code() != 0);
}

static void test_frames_over_the_size_limits_are_dropped(void) {
  fake_ready("amoled-175c");
  static uint8_t big[GADGET_BINARY_FRAME_MAX + 2000];
  memset(big, 0x11, sizeof big);
  big[0] = 0x02; /* speaker audio, stream 1 */
  big[1] = 1;
  fake_ws_in("{\"op\":\"speak.begin\",\"stream\":1,\"rate\":16000}"); /* a stream that would take it */
  fake_ws_bin_in(big, sizeof big);
  static char text[GADGET_TEXT_FRAME_MAX + 100];
  memset(text, ' ', sizeof text - 1);
  static const char head[] = "{\"op\":\"card\",\"id\":\"c\",\"title\":\"t\",\"body\":\"";
  memcpy(text, head, strlen(head)); /* a valid card, just too big */
  memcpy(text + sizeof text - 4, "\"}", 2);
  text[sizeof text - 2] = '\0';
  fake_ws_in(text);
  fake_run(10);
  TEST_ASSERT_EQUAL_INT(CORE_PAIR_PAIRED, core_pair_state()); /* still fine, nothing shown */
  TEST_ASSERT_EQUAL_INT(UI_SCREEN_IDLE, core_ui_model()->screen);
  fake_ws_in("{\"op\":\"speak.end\",\"stream\":1}");
  fake_run(500);
  TEST_ASSERT_EQUAL_size_t(0, fake_spk_accepted()); /* the big frame never reached the speaker */
  TEST_ASSERT_EQUAL_STRING("", core_ui_model()->card.title); /* nor the big card the screen */
}

static int g_taps;
static char g_tap_ops[16][16];
static void tap(core_tap_dir_t dir, const char *op, const char *json, size_t len, void *ctx) {
  (void)json;
  (void)len;
  (void)ctx;
  if (g_taps < 16) snprintf(g_tap_ops[g_taps], 16, "%s%s", dir == CORE_TAP_TX ? ">" : "<", op);
  g_taps++;
}

static void test_tap_sees_every_text_frame(void) {
  g_taps = 0;
  core_set_tap(tap, NULL);
  fake_ready("amoled-175c");
  fake_ws_in("{\"op\":\"future.op\"}");
  core_set_tap(NULL, NULL);
  TEST_ASSERT_EQUAL_INT(5, g_taps);
  TEST_ASSERT_EQUAL_STRING(">hello", g_tap_ops[0]);
  TEST_ASSERT_EQUAL_STRING("<challenge", g_tap_ops[1]);
  TEST_ASSERT_EQUAL_STRING(">prove", g_tap_ops[2]);
  TEST_ASSERT_EQUAL_STRING("<ready", g_tap_ops[3]);
  TEST_ASSERT_EQUAL_STRING("<future.op", g_tap_ops[4]);
}

int main(void) {
  if (psa_crypto_init() != PSA_SUCCESS) return 3;
  UNITY_BEGIN();
  RUN_TEST(test_unpaired_gadget_waits_on_setup);
  RUN_TEST(test_setup_asks_for_wifi_first);
  RUN_TEST(test_paired_gadget_connects_and_becomes_ready);
  RUN_TEST(test_handshake_helper_verifies_the_prove_signature);
  RUN_TEST(test_enrollment_sends_the_code_and_stores_the_host);
  RUN_TEST(test_challenge_with_a_bad_host_id_closes_and_backs_off);
  RUN_TEST(test_bad_code_clears_the_code_and_stops);
  RUN_TEST(test_setup_keeps_the_challenge_host_name_after_bad_code);
  RUN_TEST(test_enroll_required_and_revoked_unpair);
  RUN_TEST(test_device_limit_retries_every_10_s_for_120_s);
  RUN_TEST(test_replaced_shows_in_use_and_stays_offline);
  RUN_TEST(test_protocol_errors_back_off_2_4_8_up_to_60_s);
  RUN_TEST(test_proto_unsupported_errors_and_backs_off);
  RUN_TEST(test_ready_resets_the_backoff);
  RUN_TEST(test_45_s_of_silence_closes_the_session);
  RUN_TEST(test_host_auto_picks_the_stored_host_id);
  RUN_TEST(test_host_auto_before_pairing);
  RUN_TEST(test_host_auto_unsupported_prints_empty_hosts_and_waits);
  RUN_TEST(test_wifi_drop_and_return);
  RUN_TEST(test_settings_update_bot_name_and_speak_pushes);
  RUN_TEST(test_names_are_cut_to_32_characters);
  RUN_TEST(test_unknown_and_malformed_frames_are_ignored);
  RUN_TEST(test_frames_over_the_size_limits_are_dropped);
  RUN_TEST(test_tap_sees_every_text_frame);
  return UNITY_END();
}
