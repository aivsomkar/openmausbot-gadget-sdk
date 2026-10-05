/* SPDX-License-Identifier: Apache-2.0 */
#include "ui_copy.h"

#include <stdio.h>

uint32_t ui_retry_seconds(uint64_t now_ms, uint64_t retry_at_ms) {
  if (retry_at_ms <= now_ms) return 1;
  uint64_t s = (retry_at_ms - now_ms + 999u) / 1000u;
  return s < 1 ? 1u : (uint32_t)s;
}

void ui_copy_idle(const ui_model_t *m, char *out, size_t cap) {
  if (m->bot_name[0]) snprintf(out, cap, "Hi, I'm %s", m->bot_name);
  else snprintf(out, cap, "Hi");
}

void ui_copy_setup(const ui_model_t *m, ui_copy_t *out) {
  const char *c = NULL; /* an unknown step shows only the device id */
  out->caption[0] = '\0';
  out->host[0] = '\0';
  switch (m->setup.step) {
  case UI_SETUP_NEED_WIFI:
    c = "Connect me to Wi-Fi with the installer.";
    break;
  case UI_SETUP_NEED_CODE:
    c = "Pair me: MausBot " UI_ARROW " Settings " UI_ARROW " Remote access " UI_ARROW " Pair a gadget\n"
        "Enter the code in the installer.\n"
        "Remote access must be on.";
    break;
  case UI_SETUP_PAIRING: /* the host name is part of this caption */
    if (m->host_name[0]) snprintf(out->caption, sizeof out->caption, "Pairing with %s" UI_ELLIPSIS, m->host_name);
    else snprintf(out->caption, sizeof out->caption, "Pairing" UI_ELLIPSIS);
    break;
  case UI_SETUP_HOST_NOT_FOUND:
    c = "Can't find MausBot. Enter the address shown under Pair a gadget in the installer.";
    break;
  case UI_SETUP_BAD_CODE:
    c = "That code didn't work. Get a new one from Pair a gadget.";
    break;
  case UI_SETUP_DEVICE_LIMIT:
    c = "MausBot has too many devices. Remove one in Remote access. Retrying" UI_ELLIPSIS;
    break;
  }
  if (c) {
    snprintf(out->caption, sizeof out->caption, "%s", c);
    snprintf(out->host, sizeof out->host, "%s", m->host_name); /* "" until a challenge has arrived */
  }
  snprintf(out->status, sizeof out->status, "%s", m->device_id);
}

void ui_copy_offline(const ui_model_t *m, ui_copy_t *out) {
  const char *host = m->host_name[0] ? m->host_name : "MausBot";
  bool retry = false;
  bool host_line = true; /* the stored host name on its own line, unless the caption names it */
  out->caption[0] = '\0';
  out->host[0] = '\0';
  out->status[0] = '\0';
  switch (m->offline.reason) {
  case UI_OFFLINE_WIFI_CONNECTING:
    snprintf(out->caption, sizeof out->caption, "Connecting to Wi-Fi" UI_ELLIPSIS);
    break;
  case UI_OFFLINE_WIFI_FAILED:
    snprintf(out->caption, sizeof out->caption, "Can't join %s.", m->offline.ssid[0] ? m->offline.ssid : "Wi-Fi");
    break;
  case UI_OFFLINE_HOST_LOOKUP:
    snprintf(out->caption, sizeof out->caption, "Looking for MausBot" UI_ELLIPSIS);
    break;
  case UI_OFFLINE_HOST_UNREACHABLE:
    snprintf(out->caption, sizeof out->caption,
             "Can't reach %s.\nIs Remote access on in MausBot?\nOn Windows, set this network to Private.", host);
    retry = true;
    host_line = false;
    break;
  case UI_OFFLINE_IN_USE_ELSEWHERE:
    snprintf(out->caption, sizeof out->caption, "In use elsewhere\nPress TALK to use it here.");
    break;
  case UI_OFFLINE_PROTOCOL:
    snprintf(out->caption, sizeof out->caption, "MausBot didn't accept me.");
    retry = true;
    break;
  }
  if (!out->caption[0]) return; /* an unknown reason shows nothing */
  if (host_line) snprintf(out->host, sizeof out->host, "%s", m->host_name);
  if (retry && m->offline.retry_at_ms != 0) {
    snprintf(out->status, sizeof out->status, "Retrying in %u s",
             (unsigned)ui_retry_seconds(m->now_ms, m->offline.retry_at_ms));
  }
}

void ui_copy_update(const ui_model_t *m, char *out, size_t cap) {
  if (cap) out[0] = '\0';
  switch (m->update.phase) {
  case UI_UPDATE_RECEIVING:
    snprintf(out, cap, "Updating" UI_ELLIPSIS " %u%%", (unsigned)(m->update.pct > 100 ? 100 : m->update.pct));
    break;
  case UI_UPDATE_VERIFYING:
    snprintf(out, cap, "Checking update" UI_ELLIPSIS);
    break;
  case UI_UPDATE_RESTARTING:
    snprintf(out, cap, "Restarting" UI_ELLIPSIS);
    break;
  }
}

void ui_copy_countdown(const ui_model_t *m, char *out, size_t cap) {
  if (m->listening.countdown_s >= 1 && m->listening.countdown_s <= 5) snprintf(out, cap, "%u", (unsigned)m->listening.countdown_s);
  else if (cap) out[0] = '\0';
}
