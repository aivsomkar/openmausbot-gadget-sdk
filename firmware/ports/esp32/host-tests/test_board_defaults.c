/* SPDX-License-Identifier: Apache-2.0 */
#define _POSIX_C_SOURCE 200809L
/* boards/<id>/sdkconfig.defaults agree with core's board table, every board
 * directory has a table row, and the shared defaults keep rollback, the
 * USB console and the test key off (contract §2.13, §2.17). */
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gadget_board.h"
#include "unity.h"

static const char *g_esp_dir;

void setUp(void) {}
void tearDown(void) {}

static char *read_file(const char *path) {
  FILE *f = fopen(path, "rb");
  TEST_ASSERT_NOT_NULL_MESSAGE(f, path);
  fseek(f, 0, SEEK_END);
  long n = ftell(f);
  fseek(f, 0, SEEK_SET);
  char *buf = malloc((size_t)n + 1);
  TEST_ASSERT_NOT_NULL(buf);
  TEST_ASSERT_EQUAL_size_t((size_t)n, fread(buf, 1, (size_t)n, f));
  buf[n] = '\0';
  fclose(f);
  return buf;
}

/* true when `line` appears as a whole line of text */
static bool has_line(const char *text, const char *line) {
  size_t n = strlen(line);
  for (const char *p = text; (p = strstr(p, line)) != NULL; p += n) {
    bool starts = p == text || p[-1] == '\n';
    bool ends = p[n] == '\n' || p[n] == '\0' || p[n] == '\r';
    if (starts && ends) return true;
  }
  return false;
}

static void test_each_board_defaults_match_the_table(void) {
  for (size_t i = 0; gadget_board_at(i) != NULL; i++) {
    const gadget_board_t *b = gadget_board_at(i);
    char path[512], want[128];
    snprintf(path, sizeof(path), "%s/boards/%s/sdkconfig.defaults", g_esp_dir, b->id);
    char *text = read_file(path);
    snprintf(want, sizeof(want), "CONFIG_GADGET_BOARD_ID=\"%s\"", b->id);
    TEST_ASSERT_TRUE_MESSAGE(has_line(text, want), want);
    snprintf(want, sizeof(want), "CONFIG_GADGET_ART_PROFILE=\"%s\"", b->art_profile == GADGET_ART_S240 ? "s240" : "s150");
    TEST_ASSERT_TRUE_MESSAGE(has_line(text, want), want);
    TEST_ASSERT_TRUE_MESSAGE(has_line(text, "CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y"), b->id);
    TEST_ASSERT_TRUE_MESSAGE(has_line(text, "CONFIG_PARTITION_TABLE_CUSTOM_FILENAME=\"partitions/16mb.csv\""), b->id);
    TEST_ASSERT_TRUE_MESSAGE(has_line(text, "# CONFIG_GADGET_TEST_KEYS is not set"), b->id);
    TEST_ASSERT_NULL_MESSAGE(strstr(text, "CONFIG_GADGET_TEST_KEYS=y"), b->id);
    /* a board file wins over the shared one: it must not touch the LVGL
     * pins that test_shared_defaults checks */
    TEST_ASSERT_NULL_MESSAGE(strstr(text, "CONFIG_LV_CONF_SKIP"), b->id);
    TEST_ASSERT_NULL_MESSAGE(strstr(text, "CONFIG_LV_CACHE_DEF_SIZE"), b->id);
    TEST_ASSERT_NULL_MESSAGE(strstr(text, "CONFIG_LV_IMAGE_HEADER_CACHE_DEF_CNT"), b->id);
    /* nor the optimisation level */
    TEST_ASSERT_NULL_MESSAGE(strstr(text, "CONFIG_COMPILER_OPTIMIZATION"), b->id);
    free(text);
  }
}

static void test_every_board_dir_has_a_table_row(void) {
  char path[512];
  snprintf(path, sizeof(path), "%s/boards", g_esp_dir);
  DIR *d = opendir(path);
  TEST_ASSERT_NOT_NULL_MESSAGE(d, path);
  int dirs = 0;
  for (struct dirent *e; (e = readdir(d)) != NULL;) {
    if (e->d_name[0] == '.') continue;
    dirs++;
    TEST_ASSERT_NOT_NULL_MESSAGE(gadget_board_by_id(e->d_name), e->d_name);
  }
  closedir(d);
  size_t rows = 0;
  while (gadget_board_at(rows) != NULL) rows++;
  TEST_ASSERT_EQUAL_INT((int)rows, dirs);
}

static void test_shared_defaults(void) {
  char path[512];
  snprintf(path, sizeof(path), "%s/sdkconfig.defaults", g_esp_dir);
  char *text = read_file(path);
  TEST_ASSERT_TRUE(has_line(text, "CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y"));
  TEST_ASSERT_TRUE(has_line(text, "# CONFIG_BOOTLOADER_APP_ANTI_ROLLBACK is not set"));
  TEST_ASSERT_TRUE(has_line(text, "CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y"));
  TEST_ASSERT_TRUE(has_line(text, "CONFIG_PARTITION_TABLE_CUSTOM=y"));
  TEST_ASSERT_TRUE(has_line(text, "# CONFIG_GADGET_TEST_KEYS is not set"));
  TEST_ASSERT_TRUE(has_line(text, "# CONFIG_GADGET_NVS_ENCRYPT is not set"));
  TEST_ASSERT_NULL(strstr(text, "CONFIG_GADGET_TEST_KEYS=y"));
  /* LVGL from Kconfig only (firmware/ui/lv_conf.h is the simulator's) and
   * both image caches off, as P2b's ui_lv_requirements.h demands. They equal
   * LVGL's Kconfig defaults; pinning them here makes this test, not the first
   * idf.py build, the first check. */
  TEST_ASSERT_TRUE(has_line(text, "CONFIG_LV_CONF_SKIP=y"));
  TEST_ASSERT_NULL(strstr(text, "# CONFIG_LV_CONF_SKIP is not set"));
  TEST_ASSERT_TRUE(has_line(text, "CONFIG_LV_CACHE_DEF_SIZE=0"));
  TEST_ASSERT_TRUE(has_line(text, "CONFIG_LV_IMAGE_HEADER_CACHE_DEF_CNT=0"));
  /* -O2 for every board and release image: ESP-IDF's default is Debug (-Og),
   * which slows LVGL's software rendering on the gadget task. */
  TEST_ASSERT_TRUE(has_line(text, "CONFIG_COMPILER_OPTIMIZATION_PERF=y"));
  free(text);
}

int main(int argc, char **argv) {
  if (argc < 2) {
    fprintf(stderr, "usage: %s <firmware/ports/esp32>\n", argv[0]);
    return 2;
  }
  g_esp_dir = argv[1];
  UNITY_BEGIN();
  RUN_TEST(test_each_board_defaults_match_the_table);
  RUN_TEST(test_every_board_dir_has_a_table_row);
  RUN_TEST(test_shared_defaults);
  return UNITY_END();
}
