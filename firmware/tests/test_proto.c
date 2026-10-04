/* firmware/tests/test_proto.c */
/* SPDX-License-Identifier: Apache-2.0 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gadget_proto.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) { cJSON_InitHooks(NULL); } /* a failed OOM check must not leave its allocator behind */

/* A local copy of the amoled-175c descriptor (contract §2.3), so this test
 * does not depend on boards.c. */
static const gadget_board_t AMOLED = {
    .id = "amoled-175c", .display_name = "Waveshare ESP32-S3-Touch-AMOLED-1.75C",
    .screen_w = 466, .screen_h = 466, .screen_round = true, .image_w = 300, .image_h = 300,
    .mic_rate = 16000, .speaker_rate = 16000,
    .input_mask = GADGET_INPUT_TOUCH | GADGET_INPUT_TALK | GADGET_INPUT_CANCEL,
    .has_battery = true, .ota_max = 6291456, .art_profile = GADGET_ART_S240};

static const gadget_board_t DEVKIT_NO_SPK = {
    .id = "devkit", .display_name = "x", .screen_w = 320, .screen_h = 240, .screen_round = false,
    .image_w = 280, .image_h = 200, .mic_rate = 16000, .speaker_rate = 0,
    .input_mask = GADGET_INPUT_TALK | GADGET_INPUT_CANCEL, .has_battery = false, .ota_max = 6291456,
    .art_profile = GADGET_ART_S150};

static gp_msg_t decode_ok(const char *json) {
  gp_msg_t m;
  TEST_ASSERT_EQUAL_INT_MESSAGE(GADGET_OK, gp_decode(json, strlen(json), &m), json);
  return m;
}

static void test_op_names_round_trip(void) {
  for (int op = 1; op < GP_OP__COUNT; op++) {
    const char *name = gp_op_name((gp_op_t)op);
    TEST_ASSERT_NOT_NULL(name);
    TEST_ASSERT_EQUAL_INT(op, gp_op_from_name(name));
  }
  TEST_ASSERT_NULL(gp_op_name(GP_OP_UNKNOWN));
  TEST_ASSERT_EQUAL_INT(GP_OP_UNKNOWN, gp_op_from_name("nope"));
  TEST_ASSERT_EQUAL_STRING("fw.offer", gp_op_name(GP_OP_FW_OFFER));
  TEST_ASSERT_EQUAL_STRING("speak.begin", gp_op_name(GP_OP_SPEAK_BEGIN));
}

static void test_decode_handshake_ops(void) {
  gp_msg_t m = decode_ok("{\"op\":\"challenge\",\"nonce\":\"AAEC\",\"host_id\":\"000102030405060708090a0b0c0d0e0f\",\"host_name\":\"Mac\",\"extra\":1}");
  TEST_ASSERT_EQUAL_INT(GP_OP_CHALLENGE, m.op);
  TEST_ASSERT_EQUAL_STRING("AAEC", m.m.challenge.nonce);
  TEST_ASSERT_EQUAL_STRING("000102030405060708090a0b0c0d0e0f", m.m.challenge.host_id);
  TEST_ASSERT_EQUAL_STRING("Mac", m.m.challenge.host_name);
  gp_msg_free(&m);

  m = decode_ok("{\"op\":\"ready\",\"session\":\"s_81c2\",\"bot\":{\"id\":\"b_jev\",\"name\":\"Jev\"},\"settings\":{\"speak_pushes\":true}}");
  TEST_ASSERT_EQUAL_INT(GP_OP_READY, m.op);
  TEST_ASSERT_EQUAL_STRING("s_81c2", m.m.ready.session);
  TEST_ASSERT_EQUAL_STRING("b_jev", m.m.ready.bot.id);
  TEST_ASSERT_EQUAL_STRING("Jev", m.m.ready.bot.name);
  TEST_ASSERT_TRUE(m.m.ready.settings.speak_pushes);
  gp_msg_free(&m);

  m = decode_ok("{\"op\":\"error\",\"code\":\"bad_code\",\"message\":\"Wrong code\"}");
  TEST_ASSERT_EQUAL_STRING("bad_code", m.m.error.code);
  TEST_ASSERT_EQUAL_STRING("Wrong code", m.m.error.message);
  gp_msg_free(&m);

  m = decode_ok("{\"op\":\"settings\",\"name\":\"Desk\"}");
  TEST_ASSERT_FALSE(m.m.settings.has_bot);
  TEST_ASSERT_FALSE(m.m.settings.has_settings);
  TEST_ASSERT_EQUAL_STRING("Desk", m.m.settings.name);
  gp_msg_free(&m);
  m = decode_ok("{\"op\":\"settings\",\"bot\":{\"id\":\"b\",\"name\":\"B\"},\"settings\":{\"speak_pushes\":false}}");
  TEST_ASSERT_TRUE(m.m.settings.has_bot);
  TEST_ASSERT_TRUE(m.m.settings.has_settings);
  TEST_ASSERT_NULL(m.m.settings.name);
  gp_msg_free(&m);
}

static void test_decode_conversation_ops(void) {
  gp_msg_t m = decode_ok("{\"op\":\"reply\",\"turn\":\"t1-1\",\"text\":\"Hi\",\"final\":true}");
  TEST_ASSERT_EQUAL_INT(GP_OP_REPLY, m.op);
  TEST_ASSERT_TRUE(m.m.reply.final);
  gp_msg_free(&m);
  m = decode_ok("{\"op\":\"reply\",\"turn\":\"t1-1\",\"text\":\"Hi\"}");
  TEST_ASSERT_FALSE(m.m.reply.final);
  gp_msg_free(&m);
  m = decode_ok("{\"op\":\"done\",\"turn\":\"t1-1\",\"outcome\":\"failed\",\"reason\":\"Didn't catch that\"}");
  TEST_ASSERT_EQUAL_INT(GP_OUTCOME_FAILED, m.m.done.outcome);
  TEST_ASSERT_EQUAL_STRING("Didn't catch that", m.m.done.reason);
  gp_msg_free(&m);
  m = decode_ok("{\"op\":\"done\",\"turn\":\"t1-1\",\"outcome\":\"stopped\"}");
  TEST_ASSERT_EQUAL_INT(GP_OUTCOME_STOPPED, m.m.done.outcome);
  TEST_ASSERT_NULL(m.m.done.reason);
  gp_msg_free(&m);
  m = decode_ok("{\"op\":\"speak.begin\",\"stream\":3,\"rate\":24000}");
  TEST_ASSERT_EQUAL_UINT8(3, m.m.speak_begin.stream);
  TEST_ASSERT_EQUAL_UINT32(24000, m.m.speak_begin.rate);
  TEST_ASSERT_NULL(m.m.speak_begin.turn);
  gp_msg_free(&m);
  m = decode_ok("{\"op\":\"heard\",\"turn\":\"t\",\"text\":\"x\"}");
  gp_msg_free(&m);
  m = decode_ok("{\"op\":\"working\",\"turn\":\"t\",\"text\":\"\"}");
  TEST_ASSERT_EQUAL_STRING("", m.m.working.text);
  gp_msg_free(&m);
}

static void test_decode_display_ops(void) {
  gp_msg_t m = decode_ok(
      "{\"op\":\"ask\",\"id\":\"a_1\",\"kind\":\"permission\",\"title\":\"Run?\",\"body\":\"ls\","
      "\"options\":[{\"id\":\"allow\",\"label\":\"Allow\",\"style\":\"allow\"},{\"id\":\"deny\",\"label\":\"Deny\",\"style\":\"deny\"}],"
      "\"expires_s\":30}");
  TEST_ASSERT_EQUAL_INT(GP_ASK_PERMISSION, m.m.ask.kind);
  TEST_ASSERT_EQUAL_UINT8(2, m.m.ask.n_options);
  TEST_ASSERT_EQUAL_INT(GP_STYLE_ALLOW, m.m.ask.options[0].style);
  TEST_ASSERT_EQUAL_INT(GP_STYLE_DENY, m.m.ask.options[1].style);
  TEST_ASSERT_EQUAL_STRING("deny", m.m.ask.options[1].id);
  TEST_ASSERT_EQUAL_UINT32(30, m.m.ask.expires_s);
  gp_msg_free(&m);
  m = decode_ok("{\"op\":\"ask\",\"id\":\"a_2\",\"kind\":\"question\",\"title\":\"Q\",\"body\":\"B\",\"options\":[]}");
  TEST_ASSERT_EQUAL_INT(GP_ASK_QUESTION, m.m.ask.kind);
  TEST_ASSERT_EQUAL_UINT8(0, m.m.ask.n_options);
  gp_msg_free(&m);
  m = decode_ok("{\"op\":\"post\",\"id\":\"p1\",\"bot\":{\"id\":\"b\",\"name\":\"Jev\"},\"kind\":\"routine\",\"text\":\"Done\",\"speak\":true}");
  TEST_ASSERT_EQUAL_INT(GP_POST_ROUTINE, m.m.post.kind);
  TEST_ASSERT_TRUE(m.m.post.speak);
  gp_msg_free(&m);
  m = decode_ok("{\"op\":\"card\",\"id\":\"c1\",\"title\":\"T\",\"body\":\"B\",\"ttl_s\":0}");
  TEST_ASSERT_EQUAL_UINT32(0, m.m.card.ttl_s);
  gp_msg_free(&m);
  m = decode_ok("{\"op\":\"image.begin\",\"id\":\"i1\",\"stream\":7,\"w\":300,\"h\":200,\"ttl_s\":10}");
  TEST_ASSERT_EQUAL_UINT16(300, m.m.image_begin.w);
  TEST_ASSERT_EQUAL_UINT16(200, m.m.image_begin.h);
  gp_msg_free(&m);
  m = decode_ok("{\"op\":\"act\",\"id\":\"x1\",\"name\":\"chime\"}");
  TEST_ASSERT_NULL(m.m.act.args);
  gp_msg_free(&m);
  m = decode_ok("{\"op\":\"act\",\"id\":\"x1\",\"name\":\"relay\",\"args\":{\"on\":true}}");
  TEST_ASSERT_TRUE(cJSON_IsObject(m.m.act.args));
  gp_msg_free(&m);
  m = decode_ok("{\"op\":\"fw.offer\",\"stream\":1,\"board\":\"amoled-175c\",\"version\":\"1.1.0\",\"size\":1234567,"
                "\"sha256\":\"e3b0\",\"sig\":\"MEYC\",\"key_id\":\"t1\"}");
  TEST_ASSERT_EQUAL_UINT32(1234567, m.m.fw_offer.size);
  TEST_ASSERT_EQUAL_STRING("t1", m.m.fw_offer.key_id);
  gp_msg_free(&m);
}

static void test_decode_rejects_and_ignores(void) {
  gp_msg_t m;
  TEST_ASSERT_EQUAL_INT(GADGET_ERR_PARSE, gp_decode("{", 1, &m));
  TEST_ASSERT_EQUAL_INT(GADGET_ERR_PARSE, gp_decode("[]", 2, &m));
  TEST_ASSERT_EQUAL_INT(GADGET_ERR_PARSE, gp_decode("{\"x\":1}", 7, &m));
  const char *missing = "{\"op\":\"reply\",\"text\":\"no turn\"}";
  TEST_ASSERT_EQUAL_INT(GADGET_ERR_PARSE, gp_decode(missing, strlen(missing), &m));
  const char *bad_stream = "{\"op\":\"speak.end\",\"stream\":0}";
  TEST_ASSERT_EQUAL_INT(GADGET_ERR_PARSE, gp_decode(bad_stream, strlen(bad_stream), &m));
  const char *five = "{\"op\":\"ask\",\"id\":\"a\",\"kind\":\"question\",\"title\":\"t\",\"body\":\"b\",\"options\":["
                     "{\"id\":\"1\",\"label\":\"1\"},{\"id\":\"2\",\"label\":\"2\"},{\"id\":\"3\",\"label\":\"3\"},"
                     "{\"id\":\"4\",\"label\":\"4\"},{\"id\":\"5\",\"label\":\"5\"}]}";
  TEST_ASSERT_EQUAL_INT(GADGET_ERR_PARSE, gp_decode(five, strlen(five), &m));
  m = decode_ok("{\"op\":\"future.thing\",\"a\":1}");
  TEST_ASSERT_EQUAL_INT(GP_OP_UNKNOWN, m.op);
  TEST_ASSERT_EQUAL_STRING("future.thing", cJSON_GetObjectItemCaseSensitive(m.root, "op")->valuestring);
  gp_msg_free(&m);
  /* a gadget → host op arriving from the host is ignored like an unknown op */
  m = decode_ok("{\"op\":\"hello\"}");
  TEST_ASSERT_EQUAL_INT(GP_OP_UNKNOWN, m.op);
  gp_msg_free(&m);
}

/* {"op":"heard","turn":"t","text":<text>,"a":[[…]]} with n nested arrays in "a". */
static char *heard_nested(const char *text_json, size_t n) {
  char head[128];
  int h = snprintf(head, sizeof head, "{\"op\":\"heard\",\"turn\":\"t\",\"text\":%s,\"a\":", text_json);
  TEST_ASSERT_TRUE(h > 0 && (size_t)h < sizeof head);
  char *s = malloc((size_t)h + 2 * n + 2);
  TEST_ASSERT_NOT_NULL(s);
  memcpy(s, head, (size_t)h);
  memset(s + h, '[', n);
  memset(s + h + n, ']', n);
  s[(size_t)h + 2 * n] = '}';
  s[(size_t)h + 2 * n + 1] = '\0';
  return s;
}

static gadget_status_t decode_status(const char *json) {
  gp_msg_t m;
  gadget_status_t st = gp_decode(json, strlen(json), &m);
  if (st == GADGET_OK) gp_msg_free(&m);
  return st;
}

/* cJSON parses recursively and allows 1000 levels; on the gadget task's
 * 16 KiB stack 300 levels overflow it. gp_decode caps the whole document at
 * 32 levels, the outer object included, before cJSON sees it. */
static void test_decode_rejects_deep_nesting(void) {
  static const size_t too_deep[] = {1000, 300, 32};
  for (size_t i = 0; i < sizeof too_deep / sizeof too_deep[0]; i++) {
    char *s = heard_nested("\"x\"", too_deep[i]);
    char what[48];
    snprintf(what, sizeof what, "%zu nested arrays", too_deep[i]);
    TEST_ASSERT_EQUAL_INT_MESSAGE(GADGET_ERR_PARSE, decode_status(s), what);
    free(s);
  }
  char *s = heard_nested("\"x\"", 31); /* 32 levels: the cap */
  TEST_ASSERT_EQUAL_INT(GADGET_OK, decode_status(s));
  free(s);
  /* an escaped backslash ends the string: the brackets after it still count */
  s = heard_nested("\"x\\\\\"", 32);
  TEST_ASSERT_EQUAL_INT(GADGET_ERR_PARSE, decode_status(s));
  free(s);

  /* brackets and braces inside a string are text, not nesting, also after
   * an escaped quote */
  char text[160] = "{\"op\":\"heard\",\"turn\":\"t\",\"text\":\"";
  size_t o = strlen(text);
  memset(text + o, '[', 50);
  o += 50;
  strcpy(text + o, "\\\"");
  o += 2;
  memset(text + o, '[', 50);
  o += 50;
  strcpy(text + o, "{{]\"}");
  gp_msg_t m = decode_ok(text);
  TEST_ASSERT_EQUAL_size_t(104, strlen(m.m.heard.text));
  TEST_ASSERT_EQUAL_CHAR('"', m.m.heard.text[50]);
  TEST_ASSERT_EQUAL_CHAR('[', m.m.heard.text[100]);
  gp_msg_free(&m);

  /* act args nested 20 deep are fine */
  char act[256] = "{\"op\":\"act\",\"id\":\"x1\",\"name\":\"relay\",\"args\":";
  o = strlen(act);
  for (int i = 0; i < 19; i++) o += (size_t)snprintf(act + o, sizeof act - o, "{\"a\":");
  o += (size_t)snprintf(act + o, sizeof act - o, "{\"on\":true");
  for (int i = 0; i < 20; i++) act[o++] = '}';
  act[o++] = '}';
  act[o] = '\0';
  m = decode_ok(act);
  const cJSON *v = m.m.act.args;
  for (int i = 0; i < 19; i++) v = cJSON_GetObjectItemCaseSensitive(v, "a");
  TEST_ASSERT_TRUE(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(v, "on")));
  gp_msg_free(&m);
}

/* PROTOCOL.md sets no upper bound on expires_s or ttl_s: a longer lifetime
 * is kept for a day instead of dropping the whole message. */
static void test_decode_clamps_lifetimes_to_a_day(void) {
  gp_msg_t m = decode_ok("{\"op\":\"card\",\"id\":\"c1\",\"title\":\"T\",\"body\":\"B\",\"ttl_s\":100000}");
  TEST_ASSERT_EQUAL_UINT32(86400, m.m.card.ttl_s);
  gp_msg_free(&m);
  m = decode_ok("{\"op\":\"card\",\"id\":\"c1\",\"title\":\"T\",\"body\":\"B\",\"ttl_s\":86399}");
  TEST_ASSERT_EQUAL_UINT32(86399, m.m.card.ttl_s);
  gp_msg_free(&m);
  m = decode_ok("{\"op\":\"ask\",\"id\":\"a_1\",\"kind\":\"permission\",\"title\":\"Run?\",\"body\":\"ls\",\"expires_s\":172800}");
  TEST_ASSERT_EQUAL_UINT32(86400, m.m.ask.expires_s);
  gp_msg_free(&m);
  m = decode_ok("{\"op\":\"image.begin\",\"id\":\"i1\",\"stream\":7,\"w\":300,\"h\":200,\"ttl_s\":4294967295}");
  TEST_ASSERT_EQUAL_UINT32(86400, m.m.image_begin.ttl_s);
  gp_msg_free(&m);
  /* still integers and never negative */
  TEST_ASSERT_EQUAL_INT(GADGET_ERR_PARSE, decode_status("{\"op\":\"card\",\"id\":\"c\",\"title\":\"T\",\"ttl_s\":-1}"));
  TEST_ASSERT_EQUAL_INT(GADGET_ERR_PARSE, decode_status("{\"op\":\"card\",\"id\":\"c\",\"title\":\"T\",\"ttl_s\":1.5}"));
  TEST_ASSERT_EQUAL_INT(GADGET_ERR_PARSE,
                        decode_status("{\"op\":\"ask\",\"id\":\"a\",\"kind\":\"question\",\"title\":\"t\",\"expires_s\":-5}"));
}

static void test_encode_hello_exact(void) {
  static const gp_action_decl_t chime = {"chime", "Play a short chime.", NULL, GADGET_RISK_SAFE};
  gp_hello_t h = {.id = "gad_3f9a0c2b7e41d856", .pubkey_b64 = "BHx=", .name = "Desk Maus", .fw = "1.0.0",
                  .board = &AMOLED, .actions = &chime, .n_actions = 1,
                  .battery_valid = true, .battery_pct = 82, .charging = false};
  char buf[GADGET_TEXT_FRAME_MAX];
  int n = gp_encode_hello(buf, sizeof buf, &h);
  const char *want =
      "{\"op\":\"hello\",\"proto\":1,\"id\":\"gad_3f9a0c2b7e41d856\",\"pubkey\":\"BHx=\",\"name\":\"Desk Maus\","
      "\"board\":\"amoled-175c\",\"fw\":\"1.0.0\","
      "\"caps\":{\"screen\":{\"w\":466,\"h\":466,\"round\":true,\"text\":\"latin1\"},\"image\":{\"w\":300,\"h\":300},"
      "\"mic\":{\"rate\":16000},\"speaker\":{\"rate\":16000},\"input\":[\"touch\",\"talk\",\"cancel\"],"
      "\"battery\":true,\"ota\":{\"max\":6291456}},"
      "\"actions\":[{\"name\":\"chime\",\"description\":\"Play a short chime.\","
      "\"params\":{\"type\":\"object\",\"properties\":{}},\"risk\":\"safe\"}],"
      "\"sensors\":{\"battery_pct\":82,\"charging\":false}}";
  TEST_ASSERT_EQUAL_STRING(want, buf);
  TEST_ASSERT_EQUAL_INT((int)strlen(want), n);
}

static void test_encode_hello_without_speaker_or_battery(void) {
  gp_hello_t h = {.id = "gad_x", .pubkey_b64 = "B", .name = "n", .fw = "0.0.0-dev", .board = &DEVKIT_NO_SPK};
  char buf[GADGET_TEXT_FRAME_MAX];
  TEST_ASSERT_GREATER_THAN_INT(0, gp_encode_hello(buf, sizeof buf, &h));
  TEST_ASSERT_NULL(strstr(buf, "\"speaker\""));
  TEST_ASSERT_NULL(strstr(buf, "\"battery\""));
  TEST_ASSERT_NOT_NULL(strstr(buf, "\"input\":[\"talk\",\"cancel\"]"));
  TEST_ASSERT_NOT_NULL(strstr(buf, "\"actions\":[],\"sensors\":{}"));
}

static void test_encode_small_ops(void) {
  char buf[256];
  gp_prove_t p = {.sig_b64 = "MEYC", .enroll = NULL};
  gp_encode_prove(buf, sizeof buf, &p);
  TEST_ASSERT_EQUAL_STRING("{\"op\":\"prove\",\"sig\":\"MEYC\"}", buf);
  p.enroll = "123456";
  gp_encode_prove(buf, sizeof buf, &p);
  TEST_ASSERT_EQUAL_STRING("{\"op\":\"prove\",\"sig\":\"MEYC\",\"enroll\":\"123456\"}", buf);
  gp_voice_begin_t vb = {"t1a2b3c4d-1", 4, 16000};
  gp_encode_voice_begin(buf, sizeof buf, &vb);
  TEST_ASSERT_EQUAL_STRING("{\"op\":\"voice.begin\",\"turn\":\"t1a2b3c4d-1\",\"stream\":4,\"rate\":16000}", buf);
  gp_voice_end_t ve = {"t", 1240};
  gp_encode_voice_end(buf, sizeof buf, &ve);
  TEST_ASSERT_EQUAL_STRING("{\"op\":\"voice.end\",\"turn\":\"t\",\"ms\":1240}", buf);
  gp_voice_drop_t vd = {"t"};
  gp_encode_voice_drop(buf, sizeof buf, &vd);
  TEST_ASSERT_EQUAL_STRING("{\"op\":\"voice.drop\",\"turn\":\"t\"}", buf);
  gp_say_t s = {"t", "caf\xc3\xa9 \"x\""};
  gp_encode_say(buf, sizeof buf, &s);
  TEST_ASSERT_EQUAL_STRING("{\"op\":\"say\",\"turn\":\"t\",\"text\":\"caf\xc3\xa9 \\\"x\\\"\"}", buf);
  gp_stop_t st = {NULL};
  gp_encode_stop(buf, sizeof buf, &st);
  TEST_ASSERT_EQUAL_STRING("{\"op\":\"stop\"}", buf);
  gp_answer_t a = {"a_1", "allow"};
  gp_encode_answer(buf, sizeof buf, &a);
  TEST_ASSERT_EQUAL_STRING("{\"op\":\"answer\",\"id\":\"a_1\",\"option\":\"allow\"}", buf);
  gp_act_result_t r = {"x1", false, NULL, "unknown action"};
  gp_encode_act_result(buf, sizeof buf, &r);
  TEST_ASSERT_EQUAL_STRING("{\"op\":\"act.result\",\"id\":\"x1\",\"ok\":false,\"error\":\"unknown action\"}", buf);
  cJSON *data = cJSON_CreateObject();
  cJSON_AddNumberToObject(data, "n", 3);
  gp_act_result_t r2 = {"x2", true, data, NULL};
  gp_encode_act_result(buf, sizeof buf, &r2);
  TEST_ASSERT_EQUAL_STRING("{\"op\":\"act.result\",\"id\":\"x2\",\"ok\":true,\"data\":{\"n\":3}}", buf);
  cJSON_Delete(data);
  gp_sense_t se = {true, 81, true};
  gp_encode_sense(buf, sizeof buf, &se);
  TEST_ASSERT_EQUAL_STRING("{\"op\":\"sense\",\"battery_pct\":81,\"charging\":true}", buf);
  gp_event_msg_t ev = {"button.long_press", NULL};
  gp_encode_event(buf, sizeof buf, &ev);
  TEST_ASSERT_EQUAL_STRING("{\"op\":\"event\",\"name\":\"button.long_press\"}", buf);
  gp_fw_ready_t fr = {2};
  gp_encode_fw_ready(buf, sizeof buf, &fr);
  TEST_ASSERT_EQUAL_STRING("{\"op\":\"fw.ready\",\"stream\":2}", buf);
  gp_fw_fail_t ff = {2, "bad_sig"};
  gp_encode_fw_fail(buf, sizeof buf, &ff);
  TEST_ASSERT_EQUAL_STRING("{\"op\":\"fw.fail\",\"stream\":2,\"code\":\"bad_sig\"}", buf);
  gp_fw_progress_t fp = {2, 6291456};
  gp_encode_fw_progress(buf, sizeof buf, &fp);
  TEST_ASSERT_EQUAL_STRING("{\"op\":\"fw.progress\",\"stream\":2,\"offset\":6291456}", buf);
  gp_fw_installed_t fi = {"1.1.0"};
  gp_encode_fw_installed(buf, sizeof buf, &fi);
  TEST_ASSERT_EQUAL_STRING("{\"op\":\"fw.installed\",\"version\":\"1.1.0\"}", buf);
  /* too small a buffer */
  TEST_ASSERT_EQUAL_INT(GADGET_ERR_LIMIT, gp_encode_fw_installed(buf, 10, &fi));
}

/* ---- out of memory: every allocation cJSON makes can fail on the device ---- */

/* Fails only the allocation numbered s_fail_at (0-based), as a fragmented
 * heap does: one request fails and smaller ones after it still succeed. */
static int s_fail_at, s_alloc_count;
static bool s_alloc_failed;

static void *one_failing_malloc(size_t n) {
  if (s_alloc_count++ == s_fail_at) {
    s_alloc_failed = true;
    return NULL;
  }
  return malloc(n);
}

static cJSON *s_data; /* act.result and event data, built before the limit applies */

static int encode_case(int i, char *buf, size_t cap) {
  static const gp_action_decl_t acts[2] = {
      {"chime", "Play a short chime.", NULL, GADGET_RISK_SAFE},
      {"relay", "Switch the relay.", "{\"type\":\"object\",\"properties\":{\"on\":{\"type\":\"boolean\"}}}",
       GADGET_RISK_CONFIRM}};
  const gp_hello_t hello = {.id = "gad_3f9a0c2b7e41d856", .pubkey_b64 = "BHx=", .name = "Desk Maus", .fw = "1.0.0",
                            .board = &AMOLED, .actions = acts, .n_actions = 1,
                            .battery_valid = true, .battery_pct = 82, .charging = true};
  gp_hello_t hello_params = hello;
  hello_params.n_actions = 2;
  const gp_hello_t hello_devkit = {.id = "gad_x", .pubkey_b64 = "B", .name = "n", .fw = "0.0.0-dev",
                                   .board = &DEVKIT_NO_SPK};
  switch (i) {
    case 0: return gp_encode_hello(buf, cap, &hello);
    case 1: return gp_encode_hello(buf, cap, &hello_devkit);
    case 2: return gp_encode_prove(buf, cap, &(gp_prove_t){"MEYC", "123456"});
    case 3: return gp_encode_voice_begin(buf, cap, &(gp_voice_begin_t){"t-1", 4, 16000});
    case 4: return gp_encode_voice_end(buf, cap, &(gp_voice_end_t){"t-1", 1240});
    case 5: return gp_encode_voice_drop(buf, cap, &(gp_voice_drop_t){"t-1"});
    case 6: return gp_encode_say(buf, cap, &(gp_say_t){"t-1", "hello"});
    case 7: return gp_encode_stop(buf, cap, &(gp_stop_t){"t-1"});
    case 8: return gp_encode_answer(buf, cap, &(gp_answer_t){"a_1", "allow"});
    case 9: return gp_encode_act_result(buf, cap, &(gp_act_result_t){"x1", true, s_data, NULL});
    case 10: return gp_encode_act_result(buf, cap, &(gp_act_result_t){"x1", false, NULL, "nope"});
    case 11: return gp_encode_sense(buf, cap, &(gp_sense_t){true, 81, true});
    case 12: return gp_encode_event(buf, cap, &(gp_event_msg_t){"button.long_press", s_data});
    case 13: return gp_encode_fw_ready(buf, cap, &(gp_fw_ready_t){2});
    case 14: return gp_encode_fw_fail(buf, cap, &(gp_fw_fail_t){2, "bad_sig"});
    case 15: return gp_encode_fw_progress(buf, cap, &(gp_fw_progress_t){2, 65536});
    case 16: return gp_encode_fw_installed(buf, cap, &(gp_fw_installed_t){"1.1.0"});
    case 17: return gp_encode_hello(buf, cap, &hello_params);
    default: return 0;
  }
}

/* For each encoder, fail its 1st, 2nd, 3rd … allocation in turn: the result
 * is GADGET_ERR_NO_MEM or the exact frame, never a frame missing a field.
 * Case 17 parses an action's params_json, and cJSON_Parse cannot tell a
 * short heap from bad JSON, so a failure inside that parse reports
 * GADGET_ERR_ARG (bad params) instead; it still sends nothing. */
static void test_encoders_fail_whole_when_out_of_memory(void) {
  s_data = cJSON_CreateObject();
  cJSON_AddNumberToObject(cJSON_AddObjectToObject(s_data, "level"), "n", 3);
  cJSON_AddStringToObject(s_data, "s", "x");
  cJSON_Hooks hooks = {.malloc_fn = one_failing_malloc, .free_fn = free};
  static char want[GADGET_TEXT_FRAME_MAX], got[GADGET_TEXT_FRAME_MAX];
  for (int i = 0; i <= 17; i++) {
    int want_n = encode_case(i, want, sizeof want);
    TEST_ASSERT_GREATER_THAN_INT(0, want_n);
    cJSON_InitHooks(&hooks);
    int failures = 0;
    for (int k = 0;; k++) {
      TEST_ASSERT_LESS_THAN_INT_MESSAGE(2000, k, want);
      s_fail_at = k;
      s_alloc_count = 0;
      s_alloc_failed = false;
      memset(got, 0, sizeof got);
      int n = encode_case(i, got, sizeof got);
      if (!s_alloc_failed) {
        TEST_ASSERT_EQUAL_INT_MESSAGE(want_n, n, want);
        TEST_ASSERT_EQUAL_STRING(want, got);
        break;
      }
      failures++;
      if (i == 17 && n == GADGET_ERR_ARG) continue;
      TEST_ASSERT_EQUAL_INT_MESSAGE(GADGET_ERR_NO_MEM, n, want);
    }
    TEST_ASSERT_GREATER_THAN_INT(2, failures);
    cJSON_InitHooks(NULL);
  }
  cJSON_Delete(s_data);
  s_data = NULL;
}

static void test_binary_frames(void) {
  uint8_t frame[16];
  const uint8_t pay[3] = {9, 8, 7};
  TEST_ASSERT_EQUAL_size_t(5, gp_bin_encode(frame, sizeof frame, GP_BIN_MIC, 5, pay, 3));
  TEST_ASSERT_EQUAL_HEX8(0x01, frame[0]);
  TEST_ASSERT_EQUAL_HEX8(5, frame[1]);
  TEST_ASSERT_EQUAL_size_t(0, gp_bin_encode(frame, 4, GP_BIN_MIC, 5, pay, 3));
  TEST_ASSERT_EQUAL_size_t(0, gp_bin_encode(frame, sizeof frame, GP_BIN_MIC, 0, pay, 3));
  gp_bin_kind_t kind;
  uint8_t stream;
  const uint8_t *p;
  size_t plen;
  TEST_ASSERT_EQUAL_INT(GADGET_OK, gp_bin_decode(frame, 5, &kind, &stream, &p, &plen));
  TEST_ASSERT_EQUAL_INT(GP_BIN_MIC, kind);
  TEST_ASSERT_EQUAL_size_t(3, plen);
  const uint8_t bad_kind[] = {0x09, 1, 0};
  TEST_ASSERT_EQUAL_INT(GADGET_ERR_PARSE, gp_bin_decode(bad_kind, 3, &kind, &stream, &p, &plen));
  TEST_ASSERT_EQUAL_INT(GADGET_ERR_PARSE, gp_bin_decode(frame, 1, &kind, &stream, &p, &plen));
  const uint8_t chunk[] = {0x00, 0x00, 0x01, 0x00, 0xAA, 0xBB};
  uint32_t off;
  const uint8_t *data;
  size_t dlen;
  TEST_ASSERT_EQUAL_INT(GADGET_OK, gp_fw_chunk_decode(chunk, sizeof chunk, &off, &data, &dlen));
  TEST_ASSERT_EQUAL_UINT32(65536, off);
  TEST_ASSERT_EQUAL_size_t(2, dlen);
  TEST_ASSERT_EQUAL_INT(GADGET_ERR_PARSE, gp_fw_chunk_decode(chunk, 4, &off, &data, &dlen));
}

static void test_signed_texts(void) {
  char out[256];
  int n = gp_prove_text(out, sizeof out, "gad_b18b86ce1389e46d", "AAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8=",
                        "000102030405060708090a0b0c0d0e0f");
  const char *want = "openmausbot-gadget/1\nprove\ngad_b18b86ce1389e46d\nAAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8=\n"
                     "000102030405060708090a0b0c0d0e0f";
  TEST_ASSERT_EQUAL_STRING(want, out);
  TEST_ASSERT_EQUAL_INT((int)strlen(want), n);
  gp_firmware_text(out, sizeof out, "amoled-175c", "1.1.0", 1234567,
                   "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
  TEST_ASSERT_EQUAL_STRING("openmausbot-gadget/1\nfirmware\namoled-175c\n1.1.0\n1234567\n"
                           "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
                           out);
  TEST_ASSERT_EQUAL_INT(GADGET_ERR_LIMIT, gp_prove_text(out, 10, "a", "b", "c"));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_op_names_round_trip);
  RUN_TEST(test_decode_handshake_ops);
  RUN_TEST(test_decode_conversation_ops);
  RUN_TEST(test_decode_display_ops);
  RUN_TEST(test_decode_rejects_and_ignores);
  RUN_TEST(test_decode_rejects_deep_nesting);
  RUN_TEST(test_decode_clamps_lifetimes_to_a_day);
  RUN_TEST(test_encode_hello_exact);
  RUN_TEST(test_encode_hello_without_speaker_or_battery);
  RUN_TEST(test_encode_small_ops);
  RUN_TEST(test_encoders_fail_whole_when_out_of_memory);
  RUN_TEST(test_binary_frames);
  RUN_TEST(test_signed_texts);
  return UNITY_END();
}
