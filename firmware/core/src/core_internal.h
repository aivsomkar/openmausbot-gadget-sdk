/* firmware/core/src/core_internal.h */
/* SPDX-License-Identifier: Apache-2.0 */
/* Private to core: the shared state and the calls between core's modules.
 * Nothing outside firmware/core/src includes this file. */
#ifndef CORE_INTERNAL_H
#define CORE_INTERNAL_H

#include "cJSON.h"
#include "gadget_core.h"
#include "gadget_hal.h"
#include "gadget_proto.h"
#include "gadget_util.h"

#define CORE_TAG "core"
#define CORE_HOST_ADDR_MAX 64   /* "addr:port" */
#define CORE_WIFI_PASS_MAX 65

typedef struct {
  const gadget_board_t *board;
  core_config_t cfg;                  /* pointers borrowed from the port */
  char fw[GADGET_VERSION_MAX + 1];
  uint8_t priv[GADGET_PRIVKEY_LEN];
  uint8_t pub[GADGET_PUBKEY_LEN];
  char pub_b64[GADGET_PUBKEY_B64_LEN + 1];
  char id[GADGET_ID_LEN + 1];
  char name[UI_NAME_MAX];             /* stored name or the default */
  char wifi_ssid[33];
  char wifi_pass[CORE_WIFI_PASS_MAX];
  char host_id[GADGET_HOST_ID_LEN + 1];   /* "" until the first ready */
  char host_name[UI_NAME_MAX];
  char host_addr[CORE_HOST_ADDR_MAX];     /* "" = host auto */
  char pair_code[GADGET_PAIR_CODE_LEN + 1];
  char bot_id[UI_ID_MAX];
  char bot_name[UI_NAME_MAX];
  bool speak_pushes;
  gadget_prng_t prng;
  char turn_prefix[10];               /* "t" + 8 lowercase hex, chosen at boot */
  uint32_t turn_counter;
  uint64_t now;                       /* the last core_tick() argument */
  bool ticked;                        /* core_tick() ran at least once */
  bool initialized;
  /* Screen inputs: each module sets its own flags; screens.c reads them. */
  struct {
    bool recording;       /* interaction: a recording is live (Listening) */
    bool turn_active;     /* interaction: a turn is in flight and its reply is empty (Thinking) */
    bool speaking;        /* audio: the current turn's speech is playing (Speaking) */
    bool reply_visible;   /* interaction: reply text or a failed done to show (Reply) */
    bool ask_visible;     /* display: an ask is open (Ask) */
    bool card_visible;    /* display: a card is up (Card) */
    bool image_visible;   /* display: an image is up (Image) */
    bool ota_active;      /* ota: receiving, verifying or restarting (Update) */
    bool toast_visible;   /* display: a post toast is up */
  } f;
  ui_model_t model;
} core_t;

extern core_t g_core;

/* ---- core.c --------------------------------------------------------------- */
/* Storage writes that log on failure (the in-memory copy is the truth). */
void core_store_str(const char *key, const char *value);
void core_store_erase(const char *key);
/* gadget_utf8_copy / _tail, then every code point outside the gadget
 * charset (Latin-1 plus U+2026, U+2192) becomes '?'. */
size_t core_text_copy(char *dst, size_t cap, const char *src);
size_t core_text_copy_tail(char *dst, size_t cap, const char *src);
/* g_core.name = src through core_text_copy, cut to GADGET_NAME_MAX code points
 * (spec §4.3), the last one "…" when cut. */
void core_set_name(const char *src);
/* "t3f9a0c2b-7": the boot prefix plus a counter starting at 1. */
void core_next_turn_id(char out[GADGET_TURN_MAX + 1]);
/* Print "@omb " + the compact JSON of obj on the console; frees obj. */
void core_omb(cJSON *obj);
/* The protocol tap (core_set_tap) for one text frame. */
void core_tap(core_tap_dir_t dir, const char *op, const char *json, size_t len);
/* Session hooks: core.c hands these to the modules. */
void core_on_ready(void);                       /* a ready arrived */
void core_on_session_lost(void);                /* a ready session ended */
void core_on_msg(const gp_msg_t *m);            /* a host op after ready (not handshake or settings) */
void core_on_binary(gp_bin_kind_t kind, uint8_t stream, const uint8_t *payload, size_t len);

/* ---- session.c -------------------------------------------------------------- */
/* The shared encode buffer: encode into it, then session_send() it. */
extern char g_core_tx[GADGET_TEXT_FRAME_MAX];
void session_init(void);
void session_deinit(void);
void session_event(const gadget_event_t *ev);   /* Wi-Fi, WS_* and mDNS events */
void session_tick(void);
bool session_ready(void);
/* Tap and send one encoded text frame; len < 0 (an encoder error) is returned as is. */
gadget_status_t session_send(const char *op, const char *json, int len);
gadget_status_t session_send_binary(const uint8_t *frame, size_t len);
/* `pair`, `host`: drop any connection and connect again at once. */
void session_reconnect_now(bool clear_error);
/* TALK while halted by `replaced`: reconnect. */
void session_wake(void);
/* `host`, `host auto` and `forget`: drop the last challenge's host_name. */
void session_clear_host_name(void);
core_pair_state_t session_pair_state(void);
const char *session_last_error(void);
const char *session_host_in_use(void);          /* "addr:port", or NULL when unknown */
bool session_is_setup(void);                    /* never paired (no host_id) */

/* ---- screens.c -------------------------------------------------------------- */
void screens_update(void);

#endif /* CORE_INTERNAL_H */
