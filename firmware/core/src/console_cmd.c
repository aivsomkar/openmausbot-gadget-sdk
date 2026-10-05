/* firmware/core/src/console_cmd.c */
/* SPDX-License-Identifier: Apache-2.0 */
/* Running console commands and printing @omb lines (spec §5.6, contract
 * §2.11). Parsing is in console.c. */
#include <stdio.h>
#include <string.h>
#include "core_internal.h"
#include "gadget_console.h"

#define TAG "console"

static const char *wifi_name(gadget_wifi_state_t st) {
  switch (st) {
    case GADGET_WIFI_CONNECTING: return "connecting";
    case GADGET_WIFI_CONNECTED: return "connected";
    case GADGET_WIFI_FAILED: return "failed";
    default: return "off";
  }
}

static const char *pair_name(core_pair_state_t st) {
  switch (st) {
    case CORE_PAIR_CODE_STORED: return "code_stored";
    case CORE_PAIR_CONNECTING: return "connecting";
    case CORE_PAIR_PAIRED: return "paired";
    case CORE_PAIR_ERROR: return "error";
    default: return "unpaired";
  }
}

static const char *auth_name(gadget_wifi_auth_t a) {
  switch (a) {
    case GADGET_AUTH_OPEN: return "open";
    case GADGET_AUTH_WEP: return "wep";
    case GADGET_AUTH_WPA: return "wpa";
    case GADGET_AUTH_WPA2: return "wpa2";
    case GADGET_AUTH_WPA3: return "wpa3";
    case GADGET_AUTH_WPA2_ENT: return "wpa2-ent";
    default: return "other";
  }
}

static cJSON *line(const char *op) {
  cJSON *o = cJSON_CreateObject();
  cJSON_AddStringToObject(o, "op", op);
  return o;
}

static void print_error(const char *cmd, const char *message) {
  cJSON *o = line("error");
  cJSON_AddStringToObject(o, "cmd", cmd);
  cJSON_AddStringToObject(o, "message", message);
  core_omb(o);
}

static void print_status(void) {
  cJSON *o = line("status");
  cJSON_AddStringToObject(o, "wifi", wifi_name(hal_wifi_state()));
  if (g_core.wifi_ssid[0] != '\0') cJSON_AddStringToObject(o, "ssid", g_core.wifi_ssid);
  const char *host = session_host_in_use();
  if (host != NULL) cJSON_AddStringToObject(o, "host", host);
  cJSON_AddStringToObject(o, "id", g_core.id);
  core_pair_state_t pair = session_pair_state();
  cJSON_AddStringToObject(o, "pair", pair_name(pair));
  if (pair == CORE_PAIR_ERROR) cJSON_AddStringToObject(o, "error", session_last_error());
  cJSON_AddStringToObject(o, "fw", g_core.fw);
  gadget_battery_t b;
  if (g_core.board->has_battery && hal_battery_read(&b)) {
    cJSON *batt = cJSON_AddObjectToObject(o, "battery");
    cJSON_AddNumberToObject(batt, "pct", b.pct > 100 ? 100 : b.pct);
    cJSON_AddBoolToObject(batt, "charging", b.charging);
  }
  cJSON_AddStringToObject(o, "board", g_core.board->id);
  cJSON_AddStringToObject(o, "name", g_core.name);
  if (g_core.model.host_name[0] != '\0') cJSON_AddStringToObject(o, "host_name", g_core.model.host_name);
  core_omb(o);
}

void console_on_scan(const gadget_wifi_scan_ev_t *scan) {
  cJSON *o = line("scan");
  cJSON *arr = cJSON_AddArrayToObject(o, "networks");
  uint8_t n = scan->ok ? scan->count : 0;
  for (uint8_t i = 0; i < n && i < 20; i++) {
    cJSON *ap = cJSON_CreateObject();
    cJSON_AddStringToObject(ap, "ssid", scan->aps[i].ssid);
    cJSON_AddNumberToObject(ap, "rssi", scan->aps[i].rssi);
    cJSON_AddStringToObject(ap, "auth", auth_name(scan->aps[i].auth));
    cJSON_AddItemToArray(arr, ap);
  }
  core_omb(o);
}

void console_exec_line(const char *text) {
  static char buf[GADGET_CONSOLE_LINE_MAX];
  if (text == NULL) {
    print_error("", "line too long");
    return;
  }
  snprintf(buf, sizeof buf, "%s", text);
  gadget_console_parsed_t p;
  switch (gadget_console_parse(buf, &p)) {
    case GC_EMPTY:
      break;
    case GC_UNKNOWN:
    case GC_BAD_ARGS:
      print_error(p.cmd_name, p.error);
      break;
    case GC_WIFI:
      snprintf(g_core.wifi_ssid, sizeof g_core.wifi_ssid, "%s", p.a);
      snprintf(g_core.wifi_pass, sizeof g_core.wifi_pass, "%s", p.b);
      core_store_str(GADGET_KEY_WIFI_SSID, g_core.wifi_ssid);
      core_store_str(GADGET_KEY_WIFI_PASS, g_core.wifi_pass);
      if (hal_wifi_connect(g_core.wifi_ssid, g_core.wifi_pass) != GADGET_OK) print_error("wifi", "could not start Wi-Fi");
      break;
    case GC_SCAN:
      /* one @omb scan line either way (contract §2.11): a scan that cannot start
       * prints the same empty list as a scan that failed later */
      if (hal_wifi_scan() != GADGET_OK) console_on_scan(&(gadget_wifi_scan_ev_t){.ok = false});
      break;
    case GC_HOST_AUTO:
      g_core.host_addr[0] = '\0';
      core_store_erase(GADGET_KEY_HOST_ADDR);
      session_clear_host_name();
      session_reconnect_now(false);
      break;
    case GC_HOST_SET:
      snprintf(g_core.host_addr, sizeof g_core.host_addr, "%s:%u", p.a, (unsigned)p.port);
      core_store_str(GADGET_KEY_HOST_ADDR, g_core.host_addr);
      session_clear_host_name();
      session_reconnect_now(false);
      break;
    case GC_PAIR:
      snprintf(g_core.pair_code, sizeof g_core.pair_code, "%s", p.a);
      core_store_str(GADGET_KEY_PAIR_CODE, g_core.pair_code);
      session_reconnect_now(true);
      break;
    case GC_NAME:
      core_set_name(p.a);
      core_store_str(GADGET_KEY_NAME, g_core.name);
      snprintf(g_core.model.device_name, sizeof g_core.model.device_name, "%s", g_core.name);
      break;
    case GC_SAY: {
      char turn[GADGET_TURN_MAX + 1];
      if (!interaction_say(p.a, turn)) {
        print_error("say", "not connected to MausBot");
        break;
      }
      cJSON *o = line("say");
      cJSON_AddStringToObject(o, "turn", turn);
      core_omb(o);
      break;
    }
    case GC_STATUS:
      print_status();
      break;
    case GC_LOG_OFF:
      hal_log_set_enabled(false);
      break;
    case GC_LOG_ON:
      hal_log_set_enabled(true);
      break;
    case GC_FORGET:
      hal_log(GADGET_LOG_WARN, TAG, "forget: erasing the pairing, key, Wi-Fi and name");
      session_clear_host_name();
      hal_storage_erase_all();
      hal_restart();
    case GC_REBOOT:
      hal_restart();
  }
}
