/* firmware/core/include/gadget_ui_model.h */
/* SPDX-License-Identifier: Apache-2.0 */
/* What core publishes and the UI draws (spec §5.2, §5.5). Core owns the
 * one instance (core_ui_model()); the UI only reads it. All strings are
 * NUL-terminated UTF-8 already limited to the gadget charset (Latin-1 plus
 * U+2026 and U+2192); core truncates with gadget_utf8_copy(). */
#ifndef GADGET_UI_MODEL_H
#define GADGET_UI_MODEL_H

#include "gadget_types.h"

#define UI_ID_MAX 64          /* ask/card/image ids (host ids are ≤ 40 ASCII) */
#define UI_NAME_MAX 96        /* bot name, host name, device name */
#define UI_HEARD_MAX 512
#define UI_WORKING_MAX 256
#define UI_REPLY_MAX 8192     /* reply tail kept by core */
#define UI_REASON_MAX 160
#define UI_TITLE_MAX 192
#define UI_BODY_MAX 1536
#define UI_LABEL_MAX 64
#define UI_TOAST_MAX 512
#define UI_ASK_OPTIONS_MAX 4

typedef enum {
  UI_SCREEN_BOOT = 0,    /* before core_init() finishes */
  UI_SCREEN_SETUP,
  UI_SCREEN_OFFLINE,
  UI_SCREEN_IDLE,
  UI_SCREEN_LISTENING,
  UI_SCREEN_THINKING,
  UI_SCREEN_SPEAKING,
  UI_SCREEN_REPLY,
  UI_SCREEN_ASK,
  UI_SCREEN_CARD,
  UI_SCREEN_IMAGE,
  UI_SCREEN_UPDATE,
  UI_SCREEN__COUNT
} ui_screen_t;               /* script/`model` names: boot setup offline idle listening thinking speaking reply ask card image update */

typedef enum {
  UI_MAUS_NONE = 0,      /* text screens: ask, card, image, update */
  UI_MAUS_IDLE,
  UI_MAUS_LISTENING,
  UI_MAUS_THINKING,
  UI_MAUS_WORKING,
  UI_MAUS_SPEAKING,      /* gadget-only state, 3 open-mouth levels */
  UI_MAUS_SLEEPING,      /* offline */
  UI_MAUS_CURIOUS,       /* setup */
  UI_MAUS_NOTIFYING,     /* while a post toast shows over a Maus screen */
  UI_MAUS_ALERTING,      /* reply screen after a failed done */
  UI_MAUS__COUNT
} ui_maus_state_t;           /* names: none idle listening thinking working speaking sleeping curious notifying alerting */

typedef enum { UI_STYLE_NEUTRAL = 0, UI_STYLE_ALLOW, UI_STYLE_DENY } ui_option_style_t;

typedef enum {
  UI_SETUP_NEED_WIFI = 0,    /* no Wi-Fi stored */
  UI_SETUP_NEED_CODE,        /* Wi-Fi ok, never paired, no code stored */
  UI_SETUP_PAIRING,          /* code stored, connecting / waiting for ready */
  UI_SETUP_HOST_NOT_FOUND,   /* `host auto` found nothing (or several) */
  UI_SETUP_BAD_CODE,         /* last attempt: bad_code */
  UI_SETUP_DEVICE_LIMIT      /* last attempt: device_limit, retrying every 10 s */
} ui_setup_step_t;

typedef enum {
  UI_OFFLINE_WIFI_CONNECTING = 0,
  UI_OFFLINE_WIFI_FAILED,
  UI_OFFLINE_HOST_LOOKUP,        /* resolving MausBot over mDNS */
  UI_OFFLINE_HOST_UNREACHABLE,   /* connect failed or session dropped; retry_at_ms set */
  UI_OFFLINE_IN_USE_ELSEWHERE,   /* error replaced; no retry until reboot or TALK */
  UI_OFFLINE_PROTOCOL            /* proto_unsupported / bad_sig / bad host_id; retry_at_ms set */
} ui_offline_reason_t;

typedef enum { UI_UPDATE_RECEIVING = 0, UI_UPDATE_VERIFYING, UI_UPDATE_RESTARTING } ui_update_phase_t;
typedef enum { UI_POST_ROUTINE = 0, UI_POST_MESSAGE } ui_post_kind_t;

typedef struct {
  char id[UI_ID_MAX];
  char label[UI_LABEL_MAX];
  ui_option_style_t style;
  gadget_rect_t rect;         /* touch boards: from ui_layout_ask(); zeros on button boards */
} ui_ask_option_t;

typedef struct ui_model {
  uint32_t rev;               /* incremented on every change; ui_render() may skip an unchanged rev */
  uint64_t now_ms;            /* time of the last core_tick() */
  ui_screen_t screen;
  ui_maus_state_t maus;
  uint8_t speak_level;        /* 0–3: mouth level while speaking (0 = closed) */
  uint8_t mic_level;          /* 0–255: input level while listening */
  char device_id[GADGET_ID_LEN + 1];
  char device_name[UI_NAME_MAX];
  char bot_name[UI_NAME_MAX];   /* from ready/settings; "" before the first ready */
  char host_name[UI_NAME_MAX];  /* from challenge (live) or storage */

  struct { bool present; uint8_t pct; bool charging; } battery;

  struct {
    uint64_t started_ms;
    uint8_t countdown_s;      /* 5..1 during the last 5 s of the 60 s limit, else 0 */
  } listening;

  struct {
    char heard[UI_HEARD_MAX];
    char working[UI_WORKING_MAX];   /* "" when cleared */
  } thinking;

  struct {
    char text[UI_REPLY_MAX];  /* cumulative shaped reply (tail kept) */
    bool final;
    bool failed;              /* a failed done: show reason, maus alerting */
    char reason[UI_REASON_MAX];
    uint32_t speak_elapsed_ms;  /* audio played so far in the reply's speech stream */
    uint32_t speak_total_ms;    /* 0 until speak.end; then the stream's total */
  } reply;

  struct {
    char id[UI_ID_MAX];
    bool question;            /* kind question (false = permission) */
    char title[UI_TITLE_MAX];
    char body[UI_BODY_MAX];
    uint8_t n_options;
    ui_ask_option_t options[UI_ASK_OPTIONS_MAX];
    bool answerable;          /* false → "Answer on your computer or phone" */
    uint64_t locked_until_ms; /* presses before this are ignored (0.6 s) */
    int8_t chosen;            /* -1, or the option index sent in `answer` */
    uint8_t queued;           /* further asks waiting behind this one */
  } ask;

  struct {
    char id[UI_ID_MAX];
    char title[UI_TITLE_MAX];
    char body[UI_BODY_MAX];
    uint64_t expires_ms;      /* 0 = until dismissed */
  } card;

  struct {
    char id[UI_ID_MAX];
    uint16_t w, h;
    const uint16_t *pixels;   /* RGB565 little-endian, row-major, w*h; NULL until image.end; owned by core */
    uint32_t pixels_rev;      /* changes when pixels change */
    uint64_t expires_ms;
  } image;

  struct {
    bool visible;
    ui_post_kind_t kind;
    char bot_name[UI_NAME_MAX];
    char text[UI_TOAST_MAX];
    uint64_t until_ms;
  } toast;

  struct {
    ui_setup_step_t step;
    bool wifi_set;
    bool code_stored;
  } setup;

  struct {
    ui_offline_reason_t reason;
    char ssid[33];
    uint64_t retry_at_ms;     /* 0 = no automatic retry */
  } offline;

  struct {
    ui_update_phase_t phase;
    uint8_t pct;              /* 0–100 */
    char version[GADGET_VERSION_MAX + 1];
  } update;
} ui_model_t;

#endif /* GADGET_UI_MODEL_H */
