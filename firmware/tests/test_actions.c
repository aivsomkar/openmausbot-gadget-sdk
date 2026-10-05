/* firmware/tests/test_actions.c */
/* SPDX-License-Identifier: Apache-2.0 */
/* Action declarations, limits, act/act.result and events (core/src/actions.c). */
#include <stdlib.h>
#include <string.h>
#include "fake_hal.h"
#include "gadget_actions.h"
#include "psa/crypto.h"
#include "unity.h"

void setUp(void) { fake_reset(); }
void tearDown(void) {
  core_deinit();
  cJSON_InitHooks(NULL); /* a failed out-of-memory check must not leave its allocator behind */
}

static int g_calls;
static bool relay(const cJSON *args, cJSON *data, char *error, size_t cap) {
  g_calls++;
  const cJSON *on = cJSON_GetObjectItem(args, "on");
  if (!cJSON_IsBool(on)) {
    snprintf(error, cap, "on must be true or false");
    return false;
  }
  cJSON_AddBoolToObject(data, "on", cJSON_IsTrue(on));
  return true;
}

static bool counts_args(const cJSON *args, cJSON *data, char *error, size_t cap) {
  (void)error;
  (void)cap;
  cJSON_AddNumberToObject(data, "n", cJSON_GetArraySize(args));
  return true;
}

/* A handler whose data alone is larger than a 16 KiB text frame. */
static bool too_big(const cJSON *args, cJSON *data, char *error, size_t cap) {
  (void)args;
  (void)error;
  (void)cap;
  static char text[17001];
  memset(text, 'x', 17000);
  text[17000] = '\0';
  cJSON_AddStringToObject(data, "text", text);
  return true;
}

/* cJSON's next allocation after the handler returns fails, once. */
static bool s_fail_next_alloc;
static void *fail_once_malloc(size_t n) {
  if (s_fail_next_alloc) {
    s_fail_next_alloc = false;
    return NULL;
  }
  return malloc(n);
}
static bool runs_out_of_memory(const cJSON *args, cJSON *data, char *error, size_t cap) {
  (void)args;
  (void)error;
  (void)cap;
  cJSON_AddBoolToObject(data, "on", true);
  s_fail_next_alloc = true;
  return true;
}

static void reconnect_and_get_hello(cJSON **hello) {
  fake_ws_drop(1006);
  fake_run(2000);
  fake_ws_accept();
  *hello = fake_ws_last("hello");
}

static void test_hello_declares_the_chime(void) {
  fake_store_paired();
  fake_boot("amoled-175c");
  fake_ws_accept();
  cJSON *hello = fake_ws_last("hello");
  char *actions = cJSON_PrintUnformatted(cJSON_GetObjectItem(hello, "actions"));
  TEST_ASSERT_EQUAL_STRING("[{\"name\":\"chime\",\"description\":\"Play a short chime.\","
                           "\"params\":{\"type\":\"object\",\"properties\":{}},\"risk\":\"safe\"}]",
                           actions);
  cJSON_free(actions);
  cJSON_Delete(hello);
}

static void test_registered_actions_are_declared(void) {
  fake_ready("amoled-175c");
  TEST_ASSERT_EQUAL_INT(GADGET_OK, gadget_action_register("relay.on", "Switch the desk lamp.",
                                                          "{ \"type\": \"object\", \"properties\": { \"on\": {\"type\":\"boolean\"} } }",
                                                          GADGET_RISK_CONFIRM, relay));
  cJSON *hello;
  reconnect_and_get_hello(&hello);
  cJSON *list = cJSON_GetObjectItem(hello, "actions");
  TEST_ASSERT_EQUAL_INT(2, cJSON_GetArraySize(list));
  char *second = cJSON_PrintUnformatted(cJSON_GetArrayItem(list, 1));
  TEST_ASSERT_EQUAL_STRING("{\"name\":\"relay.on\",\"description\":\"Switch the desk lamp.\","
                           "\"params\":{\"type\":\"object\",\"properties\":{\"on\":{\"type\":\"boolean\"}}},\"risk\":\"confirm\"}",
                           second);
  cJSON_free(second);
  cJSON_Delete(hello);
}

static void test_registration_limits(void) {
  fake_boot("amoled-175c");
  TEST_ASSERT_EQUAL_INT(GADGET_ERR_ARG, gadget_action_register("Relay", "x", NULL, GADGET_RISK_SAFE, relay));
  TEST_ASSERT_EQUAL_INT(GADGET_ERR_ARG, gadget_action_register("1abc", "x", NULL, GADGET_RISK_SAFE, relay));
  TEST_ASSERT_EQUAL_INT(GADGET_ERR_ARG, gadget_action_register("a b", "x", NULL, GADGET_RISK_SAFE, relay));
  TEST_ASSERT_EQUAL_INT(GADGET_ERR_ARG, gadget_action_register("", "x", NULL, GADGET_RISK_SAFE, relay));
  TEST_ASSERT_EQUAL_INT(GADGET_ERR_ARG,
                        gadget_action_register("abcdefghijklmnopqrstuvwxyz0123456", "x", NULL, GADGET_RISK_SAFE, relay));
  TEST_ASSERT_EQUAL_INT(GADGET_ERR_ARG, gadget_action_register("ok", "", NULL, GADGET_RISK_SAFE, relay));
  TEST_ASSERT_EQUAL_INT(GADGET_ERR_ARG, gadget_action_register("ok", "x", NULL, GADGET_RISK_SAFE, NULL));
  TEST_ASSERT_EQUAL_INT(GADGET_ERR_ARG, gadget_action_register("ok", "x", "{not json", GADGET_RISK_SAFE, relay));
  TEST_ASSERT_EQUAL_INT(GADGET_ERR_ARG, gadget_action_register("ok", "x", "[1,2]", GADGET_RISK_SAFE, relay));
  char desc[202];
  memset(desc, 'd', 201);
  desc[201] = '\0';
  TEST_ASSERT_EQUAL_INT(GADGET_ERR_LIMIT, gadget_action_register("ok", desc, NULL, GADGET_RISK_SAFE, relay));
  desc[200] = '\0';
  TEST_ASSERT_EQUAL_INT(GADGET_OK, gadget_action_register("ok", desc, NULL, GADGET_RISK_SAFE, relay));
  static char big[1200];
  int n = snprintf(big, sizeof big, "{\"description\":\"");
  memset(big + n, 'p', 1010);
  strcpy(big + n + 1010, "\"}");
  TEST_ASSERT_EQUAL_INT(GADGET_ERR_LIMIT, gadget_action_register("big", "x", big, GADGET_RISK_SAFE, relay));
  TEST_ASSERT_EQUAL_INT(GADGET_ERR_STATE, gadget_action_register("chime", "again", NULL, GADGET_RISK_SAFE, relay));
  char name[16];
  int added = 2; /* chime and ok */
  for (int i = 0; i < 20; i++) {
    snprintf(name, sizeof name, "a%d", i);
    if (gadget_action_register(name, "x", NULL, GADGET_RISK_SAFE, relay) == GADGET_OK) added++;
  }
  TEST_ASSERT_EQUAL_INT(16, added); /* at most 16 actions */
}

static void test_the_hello_never_exceeds_16_kib(void) {
  fake_store_paired();
  fake_boot("amoled-175c");
  static char schema[1100];
  int n = snprintf(schema, sizeof schema, "{\"description\":\"");
  memset(schema + n, 's', 990);
  strcpy(schema + n + 990, "\"}");
  char desc[201];
  memset(desc, 'd', 200);
  desc[200] = '\0';
  int ok = 0;
  char name[16];
  for (int i = 0; i < 15; i++) {
    snprintf(name, sizeof name, "big%d", i);
    if (gadget_action_register(name, desc, schema, GADGET_RISK_CONFIRM, relay) == GADGET_OK) ok++;
  }
  TEST_ASSERT_TRUE(ok > 5 && ok < 15); /* the 16 KiB hello limit stops it first */
  fake_ws_accept(); /* the fake asserts every text frame is <= 16 KiB */
  TEST_ASSERT_EQUAL_size_t(1, fake_ws_count("hello"));
}

static void test_a_longer_name_still_fits_the_hello(void) {
  fake_store_paired();
  fake_boot("amoled-175c"); /* named "Maus b18b" */
  static char schema[1100];
  int n = snprintf(schema, sizeof schema, "{\"description\":\"");
  memset(schema + n, 's', 990);
  strcpy(schema + n + 990, "\"}");
  char name[16];
  for (int i = 0; i < 15; i++) {
    snprintf(name, sizeof name, "big%d", i);
    if (gadget_action_register(name, "x", schema, GADGET_RISK_SAFE, relay) != GADGET_OK) break;
  }
  /* fill what is left to the byte with one more action */
  for (int len = 990; len > 0; len--) {
    n = snprintf(schema, sizeof schema, "{\"description\":\"");
    memset(schema + n, 's', (size_t)len);
    strcpy(schema + n + len, "\"}");
    if (gadget_action_register("fill", "x", schema, GADGET_RISK_SAFE, relay) == GADGET_OK) break;
  }
  fake_ws_accept();
  TEST_ASSERT_EQUAL_size_t(1, fake_ws_count("hello"));
  /* the longest name a person can give still leaves room */
  char line[160] = "name \"";
  for (int i = 0; i < 32; i++) strcat(line, "\xE2\x80\xA6");
  strcat(line, "\"");
  fake_console_in(line);
  fake_ws_drop(1006);
  fake_run(2000);
  fake_ws_accept();
  TEST_ASSERT_EQUAL_size_t(2, fake_ws_count("hello"));
}

static void test_act_runs_the_handler(void) {
  fake_ready("amoled-175c");
  gadget_action_register("relay.on", "Switch the desk lamp.", NULL, GADGET_RISK_CONFIRM, relay);
  gadget_action_register("count", "Count args.", NULL, GADGET_RISK_SAFE, counts_args);
  g_calls = 0;
  fake_ws_in("{\"op\":\"act\",\"id\":\"x1\",\"name\":\"relay.on\",\"args\":{\"on\":true}}");
  TEST_ASSERT_EQUAL_INT(1, g_calls);
  TEST_ASSERT_EQUAL_STRING("{\"op\":\"act.result\",\"id\":\"x1\",\"ok\":true,\"data\":{\"on\":true}}",
                           fake_ws_text(fake_ws_sent() - 1));
  fake_ws_in("{\"op\":\"act\",\"id\":\"x2\",\"name\":\"relay.on\",\"args\":{}}");
  TEST_ASSERT_EQUAL_STRING("{\"op\":\"act.result\",\"id\":\"x2\",\"ok\":false,\"error\":\"on must be true or false\"}",
                           fake_ws_text(fake_ws_sent() - 1));
  fake_ws_in("{\"op\":\"act\",\"id\":\"x3\",\"name\":\"count\"}");
  TEST_ASSERT_EQUAL_STRING("{\"op\":\"act.result\",\"id\":\"x3\",\"ok\":true,\"data\":{\"n\":0}}",
                           fake_ws_text(fake_ws_sent() - 1));
  fake_ws_in("{\"op\":\"act\",\"id\":\"x4\",\"name\":\"self.destruct\"}");
  TEST_ASSERT_EQUAL_STRING("{\"op\":\"act.result\",\"id\":\"x4\",\"ok\":false,\"error\":\"unknown action\"}",
                           fake_ws_text(fake_ws_sent() - 1));
  TEST_ASSERT_EQUAL_size_t(4, fake_ws_count("act.result")); /* exactly one per act */
}

/* Contract §2.10: exactly one act.result per act, even when the handler's
 * result cannot be encoded. */
static void test_an_oversized_result_still_answers(void) {
  fake_ready("amoled-175c");
  TEST_ASSERT_EQUAL_INT(GADGET_OK, gadget_action_register("dump", "Dump a lot.", NULL, GADGET_RISK_SAFE, too_big));
  fake_ws_in("{\"op\":\"act\",\"id\":\"b1\",\"name\":\"dump\"}");
  TEST_ASSERT_EQUAL_size_t(1, fake_ws_count("act.result"));
  TEST_ASSERT_EQUAL_STRING("{\"op\":\"act.result\",\"id\":\"b1\",\"ok\":false,\"error\":\"result too large\"}",
                           fake_ws_text(fake_ws_sent() - 1));
}

static void test_a_result_without_memory_still_answers(void) {
  fake_ready("amoled-175c");
  TEST_ASSERT_EQUAL_INT(GADGET_OK, gadget_action_register("lamp", "Switch the lamp.", NULL, GADGET_RISK_SAFE,
                                                          runs_out_of_memory));
  cJSON_Hooks hooks = {.malloc_fn = fail_once_malloc, .free_fn = free};
  cJSON_InitHooks(&hooks);
  fake_ws_in("{\"op\":\"act\",\"id\":\"m1\",\"name\":\"lamp\"}");
  cJSON_InitHooks(NULL);
  TEST_ASSERT_FALSE(s_fail_next_alloc); /* the failure was spent on the result */
  TEST_ASSERT_EQUAL_size_t(1, fake_ws_count("act.result"));
  TEST_ASSERT_EQUAL_STRING("{\"op\":\"act.result\",\"id\":\"m1\",\"ok\":false,\"error\":\"out of memory\"}",
                           fake_ws_text(fake_ws_sent() - 1));
}

static void test_act_chime_plays_and_returns_ok(void) {
  fake_ready("lcd-154");
  fake_ws_in("{\"op\":\"act\",\"id\":\"c1\",\"name\":\"chime\",\"args\":{}}");
  TEST_ASSERT_EQUAL_STRING("{\"op\":\"act.result\",\"id\":\"c1\",\"ok\":true}", fake_ws_text(fake_ws_sent() - 1));
  fake_run(400);
  TEST_ASSERT_EQUAL_size_t(4800, fake_spk_accepted());
}

static void test_event_send_while_ready_and_busy_otherwise(void) {
  fake_store_paired();
  fake_boot("amoled-175c");
  TEST_ASSERT_EQUAL_INT(GADGET_ERR_BUSY, gadget_event_send("button.long_press", NULL)); /* not connected */
  fake_handshake();
  TEST_ASSERT_EQUAL_INT(GADGET_OK, gadget_event_send("button.long_press", NULL));
  TEST_ASSERT_EQUAL_STRING("{\"op\":\"event\",\"name\":\"button.long_press\"}", fake_ws_text(fake_ws_sent() - 1));
  cJSON *data = cJSON_Parse("{\"knob\":3,\"room\":\"Kitchen\"}");
  TEST_ASSERT_EQUAL_INT(GADGET_OK, gadget_event_send("knob.turn", data));
  TEST_ASSERT_EQUAL_STRING("{\"op\":\"event\",\"name\":\"knob.turn\",\"data\":{\"knob\":3,\"room\":\"Kitchen\"}}",
                           fake_ws_text(fake_ws_sent() - 1));
  fake_ws_drop(1006);
  TEST_ASSERT_EQUAL_INT(GADGET_ERR_BUSY, gadget_event_send("knob.turn", data)); /* the session is gone */
  fake_run(2000);
  fake_ws_accept();
  TEST_ASSERT_EQUAL_size_t(1, fake_ws_count("hello"));
  TEST_ASSERT_EQUAL_INT(GADGET_ERR_BUSY, gadget_event_send("knob.turn", data)); /* hello sent, no ready yet */
  TEST_ASSERT_EQUAL_size_t(2, fake_ws_count("event")); /* nothing was queued while busy */
  cJSON_Delete(data);
}

static void test_event_send_checks_the_name_and_the_data_size(void) {
  fake_ready("amoled-175c");
  TEST_ASSERT_EQUAL_INT(GADGET_ERR_ARG, gadget_event_send(NULL, NULL));
  TEST_ASSERT_EQUAL_INT(GADGET_ERR_ARG, gadget_event_send("", NULL));
  TEST_ASSERT_EQUAL_INT(GADGET_ERR_ARG, gadget_event_send("Button", NULL));
  TEST_ASSERT_EQUAL_INT(GADGET_ERR_ARG, gadget_event_send("abcdefghijklmnopqrstuvwxyz0123456", NULL));
  static char text[1024];
  memset(text, 'x', 1022);
  text[1022] = '\0';
  cJSON *fits = cJSON_CreateString(text); /* "xx…x" serializes to exactly 1024 bytes */
  text[1022] = 'x';
  text[1023] = '\0';
  cJSON *over = cJSON_CreateString(text); /* 1025 bytes */
  TEST_ASSERT_EQUAL_INT(GADGET_OK, gadget_event_send("note", fits));
  TEST_ASSERT_EQUAL_size_t(strlen("{\"op\":\"event\",\"name\":\"note\",\"data\":}") + 1024,
                           strlen(fake_ws_text(fake_ws_sent() - 1)));
  TEST_ASSERT_EQUAL_INT(GADGET_ERR_LIMIT, gadget_event_send("note", over));
  TEST_ASSERT_EQUAL_size_t(1, fake_ws_count("event"));
  fake_ws_drop(1006); /* offline, the name and size checks still come first */
  TEST_ASSERT_EQUAL_INT(GADGET_ERR_ARG, gadget_event_send("Button", NULL));
  TEST_ASSERT_EQUAL_INT(GADGET_ERR_LIMIT, gadget_event_send("note", over));
  TEST_ASSERT_EQUAL_INT(GADGET_ERR_BUSY, gadget_event_send("note", fits));
  cJSON_Delete(fits);
  cJSON_Delete(over);
}

int main(void) {
  if (psa_crypto_init() != PSA_SUCCESS) return 3;
  UNITY_BEGIN();
  RUN_TEST(test_hello_declares_the_chime);
  RUN_TEST(test_registered_actions_are_declared);
  RUN_TEST(test_registration_limits);
  RUN_TEST(test_the_hello_never_exceeds_16_kib);
  RUN_TEST(test_a_longer_name_still_fits_the_hello);
  RUN_TEST(test_act_runs_the_handler);
  RUN_TEST(test_an_oversized_result_still_answers);
  RUN_TEST(test_a_result_without_memory_still_answers);
  RUN_TEST(test_act_chime_plays_and_returns_ok);
  RUN_TEST(test_event_send_while_ready_and_busy_otherwise);
  RUN_TEST(test_event_send_checks_the_name_and_the_data_size);
  return UNITY_END();
}
