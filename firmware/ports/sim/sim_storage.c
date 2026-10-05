/* firmware/ports/sim/sim_storage.c */
/* SPDX-License-Identifier: Apache-2.0 */
/* The storage HAL as <state>/storage.json (contract §4.6):
 *   {"version": 1, "entries": {"<key>": {"str": "..."} | {"blob": "<hex>"}}}
 * Every write rewrites the file through a temp name and rename(). */
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "cJSON.h"
#include "gadget_hal.h"
#include "gadget_util.h"
#include "sim_internal.h"

static cJSON *s_root;     /* the "entries" object lives inside */
static char s_path[1100];

static int mkdirs(const char *dir) {
  char tmp[1024];
  snprintf(tmp, sizeof tmp, "%s", dir);
  for (char *p = tmp + 1; *p; p++) {
    if (*p == '/') {
      *p = '\0';
      if (mkdir(tmp, 0700) != 0 && errno != EEXIST) return -1;
      *p = '/';
    }
  }
  return (mkdir(tmp, 0700) == 0 || errno == EEXIST) ? 0 : -1;
}

static char *slurp(const char *path) {
  FILE *f = fopen(path, "rb");
  if (f == NULL) return NULL;
  fseek(f, 0, SEEK_END);
  long n = ftell(f);
  fseek(f, 0, SEEK_SET);
  char *buf = malloc((size_t)n + 1);
  size_t got = buf ? fread(buf, 1, (size_t)n, f) : 0;
  fclose(f);
  if (buf) buf[got] = '\0';
  return buf;
}

/* Writes text to path atomically (temp file + rename). */
int sim_write_atomic(const char *path, const char *text) {
  char tmp[1200];
  snprintf(tmp, sizeof tmp, "%s.tmp", path);
  FILE *f = fopen(tmp, "wb");
  if (f == NULL) return -1;
  size_t n = strlen(text);
  bool ok = fwrite(text, 1, n, f) == n;
  ok = (fclose(f) == 0) && ok;
  if (!ok || rename(tmp, path) != 0) return -1;
  return 0;
}

static cJSON *entries(void) { return cJSON_GetObjectItemCaseSensitive(s_root, "entries"); }

static gadget_status_t save(void) {
  char *text = cJSON_Print(s_root);
  if (text == NULL) return GADGET_ERR_NO_MEM;
  int rc = sim_write_atomic(s_path, text);
  cJSON_free(text);
  return rc == 0 ? GADGET_OK : GADGET_ERR_IO;
}

int sim_storage_open(const char *dir) {
  if (mkdirs(dir) != 0) {
    fprintf(stderr, "gadget-sim: cannot create %s\n", dir);
    return -1;
  }
  snprintf(s_path, sizeof s_path, "%s/storage.json", dir);
  char *text = slurp(s_path);
  s_root = text ? cJSON_Parse(text) : NULL;
  free(text);
  if (!cJSON_IsObject(s_root) || !cJSON_IsObject(entries())) {
    cJSON_Delete(s_root);
    s_root = cJSON_CreateObject();
    cJSON_AddNumberToObject(s_root, "version", 1);
    cJSON_AddObjectToObject(s_root, "entries");
  }
  return 0;
}

static gadget_status_t put(const char *key, cJSON *value) {
  if (strlen(key) > 15) {
    cJSON_Delete(value);
    return GADGET_ERR_ARG;
  }
  cJSON_DeleteItemFromObjectCaseSensitive(entries(), key);
  cJSON_AddItemToObject(entries(), key, value);
  return save();
}

gadget_status_t hal_storage_get_str(const char *key, char *buf, size_t cap) {
  const cJSON *e = cJSON_GetObjectItemCaseSensitive(entries(), key);
  const cJSON *v = cJSON_GetObjectItemCaseSensitive(e, "str");
  if (!cJSON_IsString(v)) return GADGET_ERR_NOT_FOUND;
  if (strlen(v->valuestring) + 1 > cap) return GADGET_ERR_LIMIT;
  memcpy(buf, v->valuestring, strlen(v->valuestring) + 1);
  return GADGET_OK;
}

gadget_status_t hal_storage_set_str(const char *key, const char *value) {
  cJSON *e = cJSON_CreateObject();
  cJSON_AddStringToObject(e, "str", value);
  return put(key, e);
}

gadget_status_t hal_storage_get_blob(const char *key, void *buf, size_t cap, size_t *len) {
  const cJSON *e = cJSON_GetObjectItemCaseSensitive(entries(), key);
  const cJSON *v = cJSON_GetObjectItemCaseSensitive(e, "blob");
  if (!cJSON_IsString(v)) return GADGET_ERR_NOT_FOUND;
  gadget_status_t st = gadget_hex_decode(v->valuestring, buf, cap, len);
  return st == GADGET_ERR_PARSE ? GADGET_ERR_IO : st;
}

gadget_status_t hal_storage_set_blob(const char *key, const void *data, size_t len) {
  char *hex = malloc(2 * len + 1);
  if (hex == NULL) return GADGET_ERR_NO_MEM;
  gadget_hex_encode(hex, data, len);
  cJSON *e = cJSON_CreateObject();
  cJSON_AddStringToObject(e, "blob", hex);
  free(hex);
  return put(key, e);
}

gadget_status_t hal_storage_erase(const char *key) {
  if (cJSON_GetObjectItemCaseSensitive(entries(), key) == NULL) return GADGET_OK;
  cJSON_DeleteItemFromObjectCaseSensitive(entries(), key);
  return save();
}

gadget_status_t hal_storage_erase_all(void) {
  cJSON_DeleteItemFromObjectCaseSensitive(s_root, "entries");
  cJSON_AddObjectToObject(s_root, "entries");
  return save();
}
