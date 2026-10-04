/* firmware/core/include/gadget_proto.h */
/* SPDX-License-Identifier: Apache-2.0 */
/* Protocol codec for openmausbot-gadget/1 (protocol/PROTOCOL.md). Core-
 * internal API, public for tests. Decoded strings point into the cJSON tree
 * owned by gp_msg_t and live until gp_msg_free(). Optional fields that are
 * absent decode as NULL (strings), false/0 (numbers) with has_* flags where
 * absence matters. Encoders write compact JSON, omit absent optional fields,
 * never write null, and return the length written (excluding NUL) or a
 * negative gadget_status_t. */
#ifndef GADGET_PROTO_H
#define GADGET_PROTO_H

#include "cJSON.h"
#include "gadget_actions.h"
#include "gadget_board.h"
#include "gadget_types.h"

typedef enum {
  GP_OP_UNKNOWN = 0,
  /* host → gadget */
  GP_OP_CHALLENGE, GP_OP_READY, GP_OP_ERROR, GP_OP_SETTINGS,
  GP_OP_HEARD, GP_OP_WORKING, GP_OP_REPLY, GP_OP_DONE,
  GP_OP_SPEAK_BEGIN, GP_OP_SPEAK_END, GP_OP_SPEAK_STOP,
  GP_OP_ASK, GP_OP_ASK_CLOSE, GP_OP_POST,
  GP_OP_CARD, GP_OP_CARD_CLOSE, GP_OP_IMAGE_BEGIN, GP_OP_IMAGE_END,
  GP_OP_ACT, GP_OP_FW_OFFER, GP_OP_FW_COMMIT,
  /* gadget → host */
  GP_OP_HELLO, GP_OP_PROVE, GP_OP_VOICE_BEGIN, GP_OP_VOICE_END, GP_OP_VOICE_DROP,
  GP_OP_SAY, GP_OP_STOP, GP_OP_ANSWER, GP_OP_ACT_RESULT, GP_OP_SENSE, GP_OP_EVENT,
  GP_OP_FW_READY, GP_OP_FW_FAIL, GP_OP_FW_PROGRESS, GP_OP_FW_INSTALLED,
  GP_OP__COUNT
} gp_op_t;

const char *gp_op_name(gp_op_t op);          /* "fw.offer"; NULL for UNKNOWN */
gp_op_t gp_op_from_name(const char *name);   /* GP_OP_UNKNOWN when not listed */

typedef enum { GP_STYLE_NEUTRAL = 0, GP_STYLE_ALLOW, GP_STYLE_DENY } gp_style_t;
typedef enum { GP_ASK_PERMISSION = 0, GP_ASK_QUESTION } gp_ask_kind_t;
typedef enum { GP_OUTCOME_OK = 0, GP_OUTCOME_FAILED, GP_OUTCOME_STOPPED } gp_outcome_t;
typedef enum { GP_POST_ROUTINE = 0, GP_POST_MESSAGE } gp_post_kind_t;

/* ---- host → gadget (decoded) -------------------------------------------- */
typedef struct { const char *id; const char *name; } gp_bot_t;
typedef struct { bool speak_pushes; } gp_settings_values_t;
typedef struct { const char *nonce; const char *host_id; const char *host_name; } gp_challenge_t;
typedef struct { const char *session; gp_bot_t bot; gp_settings_values_t settings; } gp_ready_t;
typedef struct { const char *code; const char *message; } gp_error_t;
typedef struct { bool has_bot; gp_bot_t bot; bool has_settings; gp_settings_values_t settings; const char *name; } gp_settings_t;
typedef struct { const char *turn; const char *text; } gp_heard_t;
typedef struct { const char *turn; const char *text; } gp_working_t;
typedef struct { const char *turn; const char *text; bool final; } gp_reply_t;
typedef struct { const char *turn; gp_outcome_t outcome; const char *reason; } gp_done_t;
typedef struct { uint8_t stream; uint32_t rate; const char *turn; } gp_speak_begin_t;
typedef struct { uint8_t stream; } gp_speak_end_t;
typedef struct { uint8_t stream; } gp_speak_stop_t;
typedef struct { const char *id; const char *label; gp_style_t style; } gp_option_t;
typedef struct {
  const char *id; gp_ask_kind_t kind; const char *title; const char *body;
  uint8_t n_options; gp_option_t options[4]; uint32_t expires_s; /* 0 = absent */
} gp_ask_t;
typedef struct { const char *id; const char *reason; /* answered | expired | withdrawn */ } gp_ask_close_t;
typedef struct { const char *id; gp_bot_t bot; gp_post_kind_t kind; const char *text; bool speak; } gp_post_t;
typedef struct { const char *id; const char *title; const char *body; uint32_t ttl_s; } gp_card_t;
typedef struct { const char *id; } gp_card_close_t;
typedef struct { const char *id; uint8_t stream; uint16_t w, h; uint32_t ttl_s; } gp_image_begin_t;
typedef struct { uint8_t stream; } gp_image_end_t;
typedef struct { const char *id; const char *name; const cJSON *args; /* object; NULL → {} */ } gp_act_t;
typedef struct {
  uint8_t stream; const char *board; const char *version; uint32_t size;
  const char *sha256; const char *sig; const char *key_id;
} gp_fw_offer_t;
typedef struct { uint8_t stream; } gp_fw_commit_t;

typedef struct gp_msg {
  gp_op_t op;
  union {
    gp_challenge_t challenge; gp_ready_t ready; gp_error_t error; gp_settings_t settings;
    gp_heard_t heard; gp_working_t working; gp_reply_t reply; gp_done_t done;
    gp_speak_begin_t speak_begin; gp_speak_end_t speak_end; gp_speak_stop_t speak_stop;
    gp_ask_t ask; gp_ask_close_t ask_close; gp_post_t post;
    gp_card_t card; gp_card_close_t card_close; gp_image_begin_t image_begin; gp_image_end_t image_end;
    gp_act_t act; gp_fw_offer_t fw_offer; gp_fw_commit_t fw_commit;
  } m;
  cJSON *root;   /* owns every string above */
} gp_msg_t;

/* Decode one host → gadget text frame. GADGET_OK with op == GP_OP_UNKNOWN
 * for an unknown op (caller ignores it); GADGET_ERR_PARSE for invalid JSON,
 * a missing "op", or a known op missing a required field. Unknown fields
 * are ignored. */
gadget_status_t gp_decode(const char *json, size_t len, gp_msg_t *out);
void gp_msg_free(gp_msg_t *m);

/* ---- gadget → host (encoded) -------------------------------------------- */
typedef struct {
  const char *name; const char *description; const char *params_json; gadget_risk_t risk;
} gp_action_decl_t;
typedef struct {
  const char *id; const char *pubkey_b64; const char *name; const char *fw;
  const gadget_board_t *board;           /* board, caps */
  const gp_action_decl_t *actions; uint8_t n_actions;
  bool battery_valid; uint8_t battery_pct; bool charging;   /* sensors */
} gp_hello_t;
typedef struct { const char *sig_b64; const char *enroll; /* NULL = omitted */ } gp_prove_t;
typedef struct { const char *turn; uint8_t stream; uint32_t rate; } gp_voice_begin_t;
typedef struct { const char *turn; uint32_t ms; } gp_voice_end_t;
typedef struct { const char *turn; } gp_voice_drop_t;
typedef struct { const char *turn; const char *text; } gp_say_t;
typedef struct { const char *turn; /* NULL = omitted */ } gp_stop_t;
typedef struct { const char *id; const char *option; } gp_answer_t;
typedef struct { const char *id; bool ok; const cJSON *data; const char *error; } gp_act_result_t;
typedef struct { bool battery_valid; uint8_t battery_pct; bool charging; } gp_sense_t;
typedef struct { const char *name; const cJSON *data; } gp_event_msg_t;
typedef struct { uint8_t stream; } gp_fw_ready_t;
typedef struct { uint8_t stream; const char *code; } gp_fw_fail_t;
typedef struct { uint8_t stream; uint32_t offset; } gp_fw_progress_t;
typedef struct { const char *version; } gp_fw_installed_t;

int gp_encode_hello(char *buf, size_t cap, const gp_hello_t *m);            /* cap ≥ GADGET_TEXT_FRAME_MAX */
int gp_encode_prove(char *buf, size_t cap, const gp_prove_t *m);
int gp_encode_voice_begin(char *buf, size_t cap, const gp_voice_begin_t *m);
int gp_encode_voice_end(char *buf, size_t cap, const gp_voice_end_t *m);
int gp_encode_voice_drop(char *buf, size_t cap, const gp_voice_drop_t *m);
int gp_encode_say(char *buf, size_t cap, const gp_say_t *m);
int gp_encode_stop(char *buf, size_t cap, const gp_stop_t *m);
int gp_encode_answer(char *buf, size_t cap, const gp_answer_t *m);
int gp_encode_act_result(char *buf, size_t cap, const gp_act_result_t *m);
int gp_encode_sense(char *buf, size_t cap, const gp_sense_t *m);
int gp_encode_event(char *buf, size_t cap, const gp_event_msg_t *m);
int gp_encode_fw_ready(char *buf, size_t cap, const gp_fw_ready_t *m);
int gp_encode_fw_fail(char *buf, size_t cap, const gp_fw_fail_t *m);
int gp_encode_fw_progress(char *buf, size_t cap, const gp_fw_progress_t *m);
int gp_encode_fw_installed(char *buf, size_t cap, const gp_fw_installed_t *m);

/* ---- binary frames (PROTOCOL.md §4.1) ----------------------------------- */
typedef enum { GP_BIN_MIC = 0x01, GP_BIN_SPEAKER = 0x02, GP_BIN_IMAGE = 0x03, GP_BIN_FIRMWARE = 0x04 } gp_bin_kind_t;
/* Writes [kind][stream][payload]; returns 2 + len, or 0 if cap is too small or the frame > 8 KiB. */
size_t gp_bin_encode(uint8_t *out, size_t cap, gp_bin_kind_t kind, uint8_t stream, const uint8_t *payload, size_t len);
gadget_status_t gp_bin_decode(const uint8_t *frame, size_t len, gp_bin_kind_t *kind, uint8_t *stream,
                              const uint8_t **payload, size_t *payload_len);   /* PARSE: len < 2, stream 0, unknown kind */
/* Firmware payload: u32 LE offset, then 1..4096 bytes. */
gadget_status_t gp_fw_chunk_decode(const uint8_t *payload, size_t len, uint32_t *offset,
                                   const uint8_t **data, size_t *data_len);

/* ---- signed texts (PROTOCOL.md §4.3 prove, §4.8 firmware) --------------- */
/* "openmausbot-gadget/1\nprove\n<id>\n<nonce_b64>\n<host_id>", no trailing newline. */
int gp_prove_text(char *out, size_t cap, const char *id, const char *nonce_b64, const char *host_id);
/* "openmausbot-gadget/1\nfirmware\n<board>\n<version>\n<size>\n<sha256 hex>", size base-10 without leading zeros. */
int gp_firmware_text(char *out, size_t cap, const char *board, const char *version, uint32_t size, const char *sha256_hex);

#endif /* GADGET_PROTO_H */
