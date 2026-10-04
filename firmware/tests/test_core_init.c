/* firmware/tests/test_core_init.c */
/* SPDX-License-Identifier: Apache-2.0 */
/* core_init(): identity, storage, the @omb boot line, the model. */
#include <string.h>
#include "fake_hal.h"
#include "gadget_util.h"
#include "psa/crypto.h"
#include "unity.h"

void setUp(void) { fake_reset(); }
void tearDown(void) { core_deinit(); }

static void test_first_boot_generates_and_stores_a_key(void) {
  fake_boot("amoled-175c");
  TEST_ASSERT_EQUAL_size_t(32, fake_storage_blob_len(GADGET_KEY_DEV_KEY));
  const char *id = core_device_id();
  TEST_ASSERT_EQUAL_size_t(GADGET_ID_LEN, strlen(id));
  TEST_ASSERT_EQUAL_MEMORY("gad_", id, 4);
  cJSON *boot = fake_omb("boot");
  TEST_ASSERT_NOT_NULL(boot);
  TEST_ASSERT_EQUAL_STRING("amoled-175c", cJSON_GetObjectItem(boot, "board")->valuestring);
  TEST_ASSERT_EQUAL_STRING("1.0.0", cJSON_GetObjectItem(boot, "fw")->valuestring);
  TEST_ASSERT_EQUAL_STRING(id, cJSON_GetObjectItem(boot, "id")->valuestring);
  cJSON_Delete(boot);
  char line[96];
  snprintf(line, sizeof line, "@omb {\"op\":\"boot\",\"board\":\"amoled-175c\",\"fw\":\"1.0.0\",\"id\":\"%s\"}", id);
  TEST_ASSERT_EQUAL_STRING(line, fake_console_line(0));
}

static void test_reboot_keeps_the_identity(void) {
  fake_boot("lcd-154");
  char first[GADGET_ID_LEN + 1];
  strcpy(first, core_device_id());
  int writes = fake_storage_writes();
  fake_boot("lcd-154");
  TEST_ASSERT_EQUAL_STRING(first, core_device_id());
  TEST_ASSERT_EQUAL_INT(writes, fake_storage_writes()); /* nothing new written */
}

static void test_stored_key_gives_the_contract_id(void) {
  uint8_t priv[32];
  size_t n = 0;
  gadget_hex_decode(FAKE_RFC_PRIV_HEX, priv, sizeof priv, &n);
  fake_storage_put_blob(GADGET_KEY_DEV_KEY, priv, sizeof priv);
  fake_boot("amoled-175c");
  TEST_ASSERT_EQUAL_STRING(FAKE_RFC_ID, core_device_id());
  TEST_ASSERT_EQUAL_STRING(FAKE_RFC_ID, core_ui_model()->device_id);
}

static void test_unusable_key_is_replaced(void) {
  fake_storage_put_blob(GADGET_KEY_DEV_KEY, "short", 5);
  fake_boot("amoled-175c");
  TEST_ASSERT_EQUAL_size_t(32, fake_storage_blob_len(GADGET_KEY_DEV_KEY));
}

static void test_names(void) {
  fake_store_paired();
  fake_boot("amoled-175c");
  TEST_ASSERT_EQUAL_STRING("Maus b18b", core_ui_model()->device_name); /* "Maus " + 4 hex of the id */
  TEST_ASSERT_EQUAL_STRING("Jev", core_ui_model()->bot_name);
  TEST_ASSERT_EQUAL_STRING("Mac", core_ui_model()->host_name);
  core_config_t cfg = {.board = gadget_board_by_id("amoled-175c"), .fw_version = "1.0.0", .default_name = "Kitchen"};
  fake_boot_cfg(&cfg);
  TEST_ASSERT_EQUAL_STRING("Kitchen", core_ui_model()->device_name);
  fake_storage_put(GADGET_KEY_NAME, "Desk Maus");
  fake_boot_cfg(&cfg);
  TEST_ASSERT_EQUAL_STRING("Desk Maus", core_ui_model()->device_name);
  /* spec §4.3: a name has at most 32 characters, the last one "…" when cut */
  fake_storage_put(GADGET_KEY_NAME, "");
  cfg.default_name = "Kitchen counter Maus by the window, left"; /* 40 */
  fake_boot_cfg(&cfg);
  TEST_ASSERT_EQUAL_STRING("Kitchen counter Maus by the win\xE2\x80\xA6", core_ui_model()->device_name);
  TEST_ASSERT_EQUAL_size_t(32, gadget_utf8_len(core_ui_model()->device_name));
  static char wide[2 * 40 + 1];
  for (int i = 0; i < 40; i++) memcpy(wide + 2 * i, "\xC3\xA9", 2); /* 40 x U+00E9 */
  cfg.default_name = wide;
  fake_boot_cfg(&cfg);
  TEST_ASSERT_EQUAL_size_t(32, gadget_utf8_len(core_ui_model()->device_name));
}

static void test_stored_wifi_is_joined_at_boot(void) {
  fake_storage_put(GADGET_KEY_WIFI_SSID, "Home");
  fake_storage_put(GADGET_KEY_WIFI_PASS, "secret123");
  fake_boot("lcd-154");
  TEST_ASSERT_EQUAL_INT(1, fake_wifi_connects());
  TEST_ASSERT_EQUAL_STRING("Home", fake_wifi_ssid());
}

static void test_bad_config_is_refused(void) {
  core_config_t cfg = {.board = NULL, .fw_version = "1.0.0"};
  TEST_ASSERT_EQUAL_INT(GADGET_ERR_ARG, core_init(&cfg));
  cfg.board = gadget_board_by_id("devkit");
  cfg.fw_version = "123456789012345678901234567890123"; /* 33 bytes */
  TEST_ASSERT_EQUAL_INT(GADGET_ERR_ARG, core_init(&cfg));
  TEST_ASSERT_EQUAL_INT(GADGET_ERR_ARG, core_init(NULL));
}

static void test_rev_changes_only_with_the_model(void) {
  fake_boot("devkit");
  uint32_t rev = core_ui_model()->rev;
  fake_run(500);
  TEST_ASSERT_EQUAL_UINT32(rev, core_ui_model()->rev); /* time alone is not a change */
  TEST_ASSERT_EQUAL_UINT64(fake_now(), core_ui_model()->now_ms);
  TEST_ASSERT_EQUAL_STRING("1.0.0", core_fw_version());
}

int main(void) {
  if (psa_crypto_init() != PSA_SUCCESS) return 3;
  UNITY_BEGIN();
  RUN_TEST(test_first_boot_generates_and_stores_a_key);
  RUN_TEST(test_reboot_keeps_the_identity);
  RUN_TEST(test_stored_key_gives_the_contract_id);
  RUN_TEST(test_unusable_key_is_replaced);
  RUN_TEST(test_names);
  RUN_TEST(test_stored_wifi_is_joined_at_boot);
  RUN_TEST(test_bad_config_is_refused);
  RUN_TEST(test_rev_changes_only_with_the_model);
  return UNITY_END();
}
