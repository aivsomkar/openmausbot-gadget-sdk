/* SPDX-License-Identifier: Apache-2.0 */
/* partitions/16mb.csv matches spec §5.3 and every board's caps.ota.max. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gadget_board.h"
#include "unity.h"

typedef struct {
  char name[16], type[8], subtype[16];
  unsigned long offset, size;
} part_t;

static const char *g_csv;
static part_t g_parts[16];
static int g_n;

static char *trim(char *s) {
  while (*s == ' ' || *s == '\t') s++;
  char *e = s + strlen(s);
  while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r' || e[-1] == '\n')) *--e = '\0';
  return s;
}

void setUp(void) {
  FILE *f = fopen(g_csv, "r");
  TEST_ASSERT_NOT_NULL_MESSAGE(f, g_csv);
  char line[256];
  g_n = 0;
  while (fgets(line, sizeof(line), f) != NULL) {
    char *s = trim(line);
    if (*s == '\0' || *s == '#') continue;
    char *field[6] = {0};
    int k = 0;
    for (char *tok = strtok(s, ","); tok != NULL && k < 6; tok = strtok(NULL, ",")) field[k++] = trim(tok);
    TEST_ASSERT_TRUE_MESSAGE(k >= 5, "a partition row needs 5 fields");
    TEST_ASSERT_TRUE(g_n < 16);
    part_t *p = &g_parts[g_n++];
    snprintf(p->name, sizeof(p->name), "%s", field[0]);
    snprintf(p->type, sizeof(p->type), "%s", field[1]);
    snprintf(p->subtype, sizeof(p->subtype), "%s", field[2]);
    p->offset = strtoul(field[3], NULL, 0);
    p->size = strtoul(field[4], NULL, 0);
  }
  fclose(f);
}
void tearDown(void) {}

static void test_rows_match_spec(void) {
  static const part_t want[] = {
    {"nvs", "data", "nvs", 0x9000, 0x6000},         {"otadata", "data", "ota", 0xF000, 0x2000},
    {"phy_init", "data", "phy", 0x11000, 0x1000},   {"ota_0", "app", "ota_0", 0x20000, 0x600000},
    {"ota_1", "app", "ota_1", 0x620000, 0x600000},  {"coredump", "data", "coredump", 0xC20000, 0x10000},
  };
  TEST_ASSERT_EQUAL_INT(6, g_n);
  for (int i = 0; i < 6; i++) {
    TEST_ASSERT_EQUAL_STRING(want[i].name, g_parts[i].name);
    TEST_ASSERT_EQUAL_STRING(want[i].type, g_parts[i].type);
    TEST_ASSERT_EQUAL_STRING(want[i].subtype, g_parts[i].subtype);
    TEST_ASSERT_EQUAL_HEX32(want[i].offset, g_parts[i].offset);
    TEST_ASSERT_EQUAL_HEX32(want[i].size, g_parts[i].size);
  }
}

static void test_layout_is_valid_for_16mb(void) {
  TEST_ASSERT_TRUE(g_parts[0].offset >= 0x9000); /* after the table at 0x8000 */
  for (int i = 0; i < g_n; i++) {
    if (strcmp(g_parts[i].type, "app") == 0) {
      TEST_ASSERT_EQUAL_HEX32(0, g_parts[i].offset % 0x10000);
    }
    TEST_ASSERT_TRUE(g_parts[i].offset + g_parts[i].size <= 0x1000000);
    if (i > 0) {
      TEST_ASSERT_TRUE(g_parts[i - 1].offset + g_parts[i - 1].size <= g_parts[i].offset);
    }
  }
}

static void test_every_board_ota_max_is_the_slot_size(void) {
  size_t n = 0;
  for (const gadget_board_t *b; (b = gadget_board_at(n)) != NULL; n++) {
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(g_parts[3].size, b->ota_max, b->id);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(g_parts[4].size, b->ota_max, b->id);
  }
  TEST_ASSERT_EQUAL_size_t(4, n);
}

int main(int argc, char **argv) {
  if (argc < 2) {
    fprintf(stderr, "usage: %s <partitions/16mb.csv>\n", argv[0]);
    return 2;
  }
  g_csv = argv[1];
  UNITY_BEGIN();
  RUN_TEST(test_rows_match_spec);
  RUN_TEST(test_layout_is_valid_for_16mb);
  RUN_TEST(test_every_board_ota_max_is_the_slot_size);
  return UNITY_END();
}
