/* SPDX-License-Identifier: Apache-2.0 */
/* HAL storage group on NVS: namespace "gadget", committed on every write
 * (contract §2.14). With CONFIG_GADGET_NVS_ENCRYPT the whole default NVS
 * partition is encrypted by ESP-IDF's HMAC scheme (nvs_flash_init in
 * main.c does the work); nothing here changes. */
#include <string.h>

#include "esp_idf_version.h"
#include "gadget_hal.h"
#include "nvs.h"
#include "pl_util.h"
#include "sdkconfig.h"

#if CONFIG_GADGET_NVS_ENCRYPT
#if !CONFIG_NVS_ENCRYPTION || !CONFIG_NVS_SEC_KEY_PROTECT_USING_HMAC || CONFIG_NVS_SEC_HMAC_EFUSE_KEY_ID != 5
#error "GADGET_NVS_ENCRYPT needs NVS_ENCRYPTION, NVS_SEC_KEY_PROTECT_USING_HMAC and NVS_SEC_HMAC_EFUSE_KEY_ID=5 (sdkconfig.nvs-encrypt)"
#endif
#endif

#define STORAGE_NS "gadget"

static gadget_status_t map_err(esp_err_t e) {
  switch (e) {
    case ESP_OK: return GADGET_OK;
    case ESP_ERR_NVS_NOT_FOUND: return GADGET_ERR_NOT_FOUND;
    case ESP_ERR_NVS_INVALID_LENGTH: return GADGET_ERR_LIMIT;
    case ESP_ERR_NO_MEM: return GADGET_ERR_NO_MEM;
    case ESP_ERR_NVS_KEY_TOO_LONG:
    case ESP_ERR_NVS_INVALID_NAME:
    case ESP_ERR_NVS_VALUE_TOO_LONG:
    case ESP_ERR_INVALID_ARG: return GADGET_ERR_ARG;
    default: return GADGET_ERR_IO;
  }
}

/* A read-only open of a namespace that was never written reports
 * ESP_ERR_NVS_NOT_FOUND, which is the same as "key missing" here. */
static gadget_status_t open_ns(nvs_open_mode_t mode, nvs_handle_t *h) { return map_err(nvs_open(STORAGE_NS, mode, h)); }

static gadget_status_t commit_close(nvs_handle_t h, esp_err_t e) {
  if (e == ESP_OK) {
    e = nvs_commit(h);
  }
  nvs_close(h);
  return map_err(e);
}

gadget_status_t hal_storage_get_str(const char *key, char *buf, size_t cap) {
  if (!pl_storage_key_ok(key) || buf == NULL || cap == 0) {
    return GADGET_ERR_ARG;
  }
  nvs_handle_t h;
  gadget_status_t st = open_ns(NVS_READONLY, &h);
  if (st != GADGET_OK) {
    return st;
  }
  size_t len = cap;
  st = map_err(nvs_get_str(h, key, buf, &len));
  nvs_close(h);
  return st;
}

gadget_status_t hal_storage_set_str(const char *key, const char *value) {
  if (!pl_storage_key_ok(key) || value == NULL) {
    return GADGET_ERR_ARG;
  }
  nvs_handle_t h;
  gadget_status_t st = open_ns(NVS_READWRITE, &h);
  if (st != GADGET_OK) {
    return st;
  }
  return commit_close(h, nvs_set_str(h, key, value));
}

gadget_status_t hal_storage_get_blob(const char *key, void *buf, size_t cap, size_t *len) {
  if (!pl_storage_key_ok(key) || buf == NULL || len == NULL) {
    return GADGET_ERR_ARG;
  }
  nvs_handle_t h;
  gadget_status_t st = open_ns(NVS_READONLY, &h);
  if (st != GADGET_OK) {
    return st;
  }
  size_t n = cap;
  st = map_err(nvs_get_blob(h, key, buf, &n));
  nvs_close(h);
  if (st == GADGET_OK) {
    *len = n;
  }
  return st;
}

gadget_status_t hal_storage_set_blob(const char *key, const void *data, size_t len) {
  if (!pl_storage_key_ok(key) || (data == NULL && len > 0)) {
    return GADGET_ERR_ARG;
  }
  nvs_handle_t h;
  gadget_status_t st = open_ns(NVS_READWRITE, &h);
  if (st != GADGET_OK) {
    return st;
  }
  return commit_close(h, nvs_set_blob(h, key, data, len));
}

gadget_status_t hal_storage_erase(const char *key) {
  if (!pl_storage_key_ok(key)) {
    return GADGET_ERR_ARG;
  }
  nvs_handle_t h;
  gadget_status_t st = open_ns(NVS_READWRITE, &h);
  if (st != GADGET_OK) {
    return st;
  }
  esp_err_t e = nvs_erase_key(h, key);
  if (e == ESP_ERR_NVS_NOT_FOUND) {
    e = ESP_OK;
  }
  return commit_close(h, e);
}

gadget_status_t hal_storage_erase_all(void) {
  nvs_handle_t h;
  gadget_status_t st = open_ns(NVS_READWRITE, &h);
  if (st != GADGET_OK) {
    return st;
  }
  esp_err_t e = nvs_erase_all(h);
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 0, 0)
  if (e == ESP_OK) {
    e = nvs_purge_all(h); /* overwrite the erased entries, so the old key is gone from flash */
  }
#endif
  return commit_close(h, e);
}
