/* firmware/core/src/proto.c */
/* SPDX-License-Identifier: Apache-2.0 */
/* openmausbot-gadget/1 codec (gadget_proto.h, protocol/PROTOCOL.md). */
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include "gadget_proto.h"

static const char *const OP_NAMES[GP_OP__COUNT] = {
    [GP_OP_UNKNOWN] = NULL,
    [GP_OP_CHALLENGE] = "challenge", [GP_OP_READY] = "ready", [GP_OP_ERROR] = "error",
    [GP_OP_SETTINGS] = "settings", [GP_OP_HEARD] = "heard", [GP_OP_WORKING] = "working",
    [GP_OP_REPLY] = "reply", [GP_OP_DONE] = "done", [GP_OP_SPEAK_BEGIN] = "speak.begin",
    [GP_OP_SPEAK_END] = "speak.end", [GP_OP_SPEAK_STOP] = "speak.stop", [GP_OP_ASK] = "ask",
    [GP_OP_ASK_CLOSE] = "ask.close", [GP_OP_POST] = "post", [GP_OP_CARD] = "card",
    [GP_OP_CARD_CLOSE] = "card.close", [GP_OP_IMAGE_BEGIN] = "image.begin",
    [GP_OP_IMAGE_END] = "image.end", [GP_OP_ACT] = "act", [GP_OP_FW_OFFER] = "fw.offer",
    [GP_OP_FW_COMMIT] = "fw.commit",
    [GP_OP_HELLO] = "hello", [GP_OP_PROVE] = "prove", [GP_OP_VOICE_BEGIN] = "voice.begin",
    [GP_OP_VOICE_END] = "voice.end", [GP_OP_VOICE_DROP] = "voice.drop", [GP_OP_SAY] = "say",
    [GP_OP_STOP] = "stop", [GP_OP_ANSWER] = "answer", [GP_OP_ACT_RESULT] = "act.result",
    [GP_OP_SENSE] = "sense", [GP_OP_EVENT] = "event", [GP_OP_FW_READY] = "fw.ready",
    [GP_OP_FW_FAIL] = "fw.fail", [GP_OP_FW_PROGRESS] = "fw.progress",
    [GP_OP_FW_INSTALLED] = "fw.installed",
};

const char *gp_op_name(gp_op_t op) {
  if (op <= GP_OP_UNKNOWN || op >= GP_OP__COUNT) return NULL;
  return OP_NAMES[op];
}

gp_op_t gp_op_from_name(const char *name) {
  if (name == NULL) return GP_OP_UNKNOWN;
  for (int op = 1; op < GP_OP__COUNT; op++) {
    if (strcmp(OP_NAMES[op], name) == 0) return (gp_op_t)op;
  }
  return GP_OP_UNKNOWN;
}

/* ---- decode helpers: each returns false when a required field is bad ---- */

static bool str_field(const cJSON *o, const char *key, bool required, const char **out) {
  const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, key);
  if (v == NULL) {
    *out = NULL;
    return !required;
  }
  if (!cJSON_IsString(v) || v->valuestring == NULL) return false;
  *out = v->valuestring;
  return true;
}

static bool u32_field(const cJSON *o, const char *key, bool required, uint32_t lo, uint32_t hi, uint32_t *out) {
  const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, key);
  if (v == NULL) {
    *out = 0;
    return !required;
  }
  if (!cJSON_IsNumber(v)) return false;
  double d = v->valuedouble;
  if (!(d >= (double)lo && d <= (double)hi)) return false; /* also rejects NaN */
  if ((double)(uint32_t)d != d) return false;            /* integers only */
  *out = (uint32_t)d;
  return true;
}

static bool bool_field(const cJSON *o, const char *key, bool *out) {
  const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, key);
  *out = false;
  if (v == NULL) return true;
  if (!cJSON_IsBool(v)) return false;
  *out = cJSON_IsTrue(v);
  return true;
}

static bool stream_field(const cJSON *o, uint8_t *out) {
  uint32_t v;
  if (!u32_field(o, "stream", true, 1, 255, &v)) return false;
  *out = (uint8_t)v;
  return true;
}

static bool bot_field(const cJSON *o, const char *key, bool required, gp_bot_t *out, bool *present) {
  const cJSON *b = cJSON_GetObjectItemCaseSensitive(o, key);
  if (present) *present = b != NULL;
  if (b == NULL) return !required;
  if (!cJSON_IsObject(b)) return false;
  return str_field(b, "id", true, &out->id) && str_field(b, "name", true, &out->name);
}

static bool settings_field(const cJSON *o, gp_settings_values_t *out, bool *present) {
  const cJSON *s = cJSON_GetObjectItemCaseSensitive(o, "settings");
  if (present) *present = s != NULL;
  if (s == NULL) return true;
  if (!cJSON_IsObject(s)) return false;
  return bool_field(s, "speak_pushes", &out->speak_pushes);
}

static gp_style_t style_from(const char *s) {
  if (s && strcmp(s, "allow") == 0) return GP_STYLE_ALLOW;
  if (s && strcmp(s, "deny") == 0) return GP_STYLE_DENY;
  return GP_STYLE_NEUTRAL;
}

static bool decode_ask(const cJSON *o, gp_ask_t *a) {
  const char *kind = NULL;
  if (!str_field(o, "id", true, &a->id) || !str_field(o, "kind", true, &kind) ||
      !str_field(o, "title", true, &a->title) || !str_field(o, "body", false, &a->body) ||
      !u32_field(o, "expires_s", false, 0, 86400, &a->expires_s)) {
    return false;
  }
  a->kind = strcmp(kind, "permission") == 0 ? GP_ASK_PERMISSION : GP_ASK_QUESTION;
  const cJSON *opts = cJSON_GetObjectItemCaseSensitive(o, "options");
  if (opts == NULL) return true;
  if (!cJSON_IsArray(opts) || cJSON_GetArraySize(opts) > 4) return false;
  const cJSON *it = NULL;
  cJSON_ArrayForEach(it, opts) {
    gp_option_t *op = &a->options[a->n_options];
    const char *style = NULL;
    if (!cJSON_IsObject(it) || !str_field(it, "id", true, &op->id) || !str_field(it, "label", true, &op->label) ||
        !str_field(it, "style", false, &style)) {
      return false;
    }
    op->style = style_from(style);
    a->n_options++;
  }
  return true;
}

static bool decode_fields(const cJSON *o, gp_msg_t *out) {
  switch (out->op) {
    case GP_OP_CHALLENGE: {
      gp_challenge_t *c = &out->m.challenge;
      return str_field(o, "nonce", true, &c->nonce) && str_field(o, "host_id", true, &c->host_id) &&
             str_field(o, "host_name", false, &c->host_name);
    }
    case GP_OP_READY: {
      gp_ready_t *r = &out->m.ready;
      return str_field(o, "session", true, &r->session) && bot_field(o, "bot", true, &r->bot, NULL) &&
             settings_field(o, &r->settings, NULL);
    }
    case GP_OP_ERROR:
      return str_field(o, "code", true, &out->m.error.code) && str_field(o, "message", false, &out->m.error.message);
    case GP_OP_SETTINGS: {
      gp_settings_t *s = &out->m.settings;
      return bot_field(o, "bot", false, &s->bot, &s->has_bot) && settings_field(o, &s->settings, &s->has_settings) &&
             str_field(o, "name", false, &s->name);
    }
    case GP_OP_HEARD:
      return str_field(o, "turn", true, &out->m.heard.turn) && str_field(o, "text", true, &out->m.heard.text);
    case GP_OP_WORKING:
      return str_field(o, "turn", true, &out->m.working.turn) && str_field(o, "text", true, &out->m.working.text);
    case GP_OP_REPLY:
      return str_field(o, "turn", true, &out->m.reply.turn) && str_field(o, "text", true, &out->m.reply.text) &&
             bool_field(o, "final", &out->m.reply.final);
    case GP_OP_DONE: {
      const char *outcome = NULL;
      gp_done_t *d = &out->m.done;
      if (!str_field(o, "turn", true, &d->turn) || !str_field(o, "outcome", true, &outcome) ||
          !str_field(o, "reason", false, &d->reason)) {
        return false;
      }
      d->outcome = strcmp(outcome, "ok") == 0        ? GP_OUTCOME_OK
                   : strcmp(outcome, "stopped") == 0 ? GP_OUTCOME_STOPPED
                                                     : GP_OUTCOME_FAILED;
      return true;
    }
    case GP_OP_SPEAK_BEGIN:
      return stream_field(o, &out->m.speak_begin.stream) &&
             u32_field(o, "rate", true, 8000, 48000, &out->m.speak_begin.rate) &&
             str_field(o, "turn", false, &out->m.speak_begin.turn);
    case GP_OP_SPEAK_END:
      return stream_field(o, &out->m.speak_end.stream);
    case GP_OP_SPEAK_STOP:
      return stream_field(o, &out->m.speak_stop.stream);
    case GP_OP_ASK:
      return decode_ask(o, &out->m.ask);
    case GP_OP_ASK_CLOSE:
      return str_field(o, "id", true, &out->m.ask_close.id) && str_field(o, "reason", false, &out->m.ask_close.reason);
    case GP_OP_POST: {
      gp_post_t *p = &out->m.post;
      const char *kind = NULL;
      if (!str_field(o, "id", true, &p->id) || !bot_field(o, "bot", true, &p->bot, NULL) ||
          !str_field(o, "kind", true, &kind) || !str_field(o, "text", true, &p->text) ||
          !bool_field(o, "speak", &p->speak)) {
        return false;
      }
      p->kind = strcmp(kind, "routine") == 0 ? GP_POST_ROUTINE : GP_POST_MESSAGE;
      return true;
    }
    case GP_OP_CARD: {
      gp_card_t *c = &out->m.card;
      return str_field(o, "id", true, &c->id) && str_field(o, "title", true, &c->title) &&
             str_field(o, "body", false, &c->body) && u32_field(o, "ttl_s", false, 0, 86400, &c->ttl_s);
    }
    case GP_OP_CARD_CLOSE:
      return str_field(o, "id", true, &out->m.card_close.id);
    case GP_OP_IMAGE_BEGIN: {
      gp_image_begin_t *b = &out->m.image_begin;
      uint32_t w = 0, h = 0;
      if (!str_field(o, "id", true, &b->id) || !stream_field(o, &b->stream) || !u32_field(o, "w", true, 1, 4096, &w) ||
          !u32_field(o, "h", true, 1, 4096, &h) || !u32_field(o, "ttl_s", false, 0, 86400, &b->ttl_s)) {
        return false;
      }
      b->w = (uint16_t)w;
      b->h = (uint16_t)h;
      return true;
    }
    case GP_OP_IMAGE_END:
      return stream_field(o, &out->m.image_end.stream);
    case GP_OP_ACT: {
      gp_act_t *a = &out->m.act;
      if (!str_field(o, "id", true, &a->id) || !str_field(o, "name", true, &a->name)) return false;
      const cJSON *args = cJSON_GetObjectItemCaseSensitive(o, "args");
      if (args != NULL && !cJSON_IsObject(args)) return false;
      a->args = args;
      return true;
    }
    case GP_OP_FW_OFFER: {
      gp_fw_offer_t *f = &out->m.fw_offer;
      return stream_field(o, &f->stream) && str_field(o, "board", true, &f->board) &&
             str_field(o, "version", true, &f->version) && u32_field(o, "size", true, 0, UINT32_MAX, &f->size) &&
             str_field(o, "sha256", true, &f->sha256) && str_field(o, "sig", true, &f->sig) &&
             str_field(o, "key_id", true, &f->key_id);
    }
    case GP_OP_FW_COMMIT:
      return stream_field(o, &out->m.fw_commit.stream);
    default:
      return true;
  }
}

gadget_status_t gp_decode(const char *json, size_t len, gp_msg_t *out) {
  if (json == NULL || out == NULL) return GADGET_ERR_ARG;
  memset(out, 0, sizeof *out);
  cJSON *root = cJSON_ParseWithLength(json, len);
  if (root == NULL) return GADGET_ERR_PARSE;
  const cJSON *op = cJSON_GetObjectItemCaseSensitive(root, "op");
  if (!cJSON_IsObject(root) || !cJSON_IsString(op)) {
    cJSON_Delete(root);
    return GADGET_ERR_PARSE;
  }
  gp_op_t code = gp_op_from_name(op->valuestring);
  /* gadget → host ops never come from a host: ignore them like unknown ops */
  out->op = (code >= GP_OP_HELLO) ? GP_OP_UNKNOWN : code;
  out->root = root;
  if (!decode_fields(root, out)) {
    gp_msg_free(out);
    return GADGET_ERR_PARSE;
  }
  return GADGET_OK;
}

void gp_msg_free(gp_msg_t *m) {
  if (m == NULL) return;
  cJSON_Delete(m->root);
  memset(m, 0, sizeof *m);
}

/* ---- encoders ------------------------------------------------------------ */

static cJSON *begin(const char *op) {
  cJSON *o = cJSON_CreateObject();
  if (o) cJSON_AddStringToObject(o, "op", op);
  return o;
}

/* Prints o compactly into buf and frees it; returns the length or an error. */
static int finish(cJSON *o, char *buf, size_t cap) {
  if (o == NULL) return GADGET_ERR_NO_MEM;
  char *text = cJSON_PrintUnformatted(o);
  cJSON_Delete(o);
  if (text == NULL) return GADGET_ERR_NO_MEM;
  size_t n = strlen(text);
  int rc = GADGET_ERR_LIMIT;
  if (n + 1 <= cap) {
    memcpy(buf, text, n + 1);
    rc = (int)n;
  }
  cJSON_free(text);
  return rc;
}

static cJSON *caps_json(const gadget_board_t *b) {
  cJSON *caps = cJSON_CreateObject();
  cJSON *screen = cJSON_AddObjectToObject(caps, "screen");
  cJSON_AddNumberToObject(screen, "w", b->screen_w);
  cJSON_AddNumberToObject(screen, "h", b->screen_h);
  cJSON_AddBoolToObject(screen, "round", b->screen_round);
  cJSON_AddStringToObject(screen, "text", "latin1");
  cJSON *image = cJSON_AddObjectToObject(caps, "image");
  cJSON_AddNumberToObject(image, "w", b->image_w);
  cJSON_AddNumberToObject(image, "h", b->image_h);
  cJSON *mic = cJSON_AddObjectToObject(caps, "mic");
  cJSON_AddNumberToObject(mic, "rate", b->mic_rate);
  if (b->speaker_rate != 0) {
    cJSON *spk = cJSON_AddObjectToObject(caps, "speaker");
    cJSON_AddNumberToObject(spk, "rate", b->speaker_rate);
  }
  cJSON *input = cJSON_AddArrayToObject(caps, "input");
  if (b->input_mask & GADGET_INPUT_TOUCH) cJSON_AddItemToArray(input, cJSON_CreateString("touch"));
  if (b->input_mask & GADGET_INPUT_TALK) cJSON_AddItemToArray(input, cJSON_CreateString("talk"));
  if (b->input_mask & GADGET_INPUT_CANCEL) cJSON_AddItemToArray(input, cJSON_CreateString("cancel"));
  if (b->has_battery) cJSON_AddTrueToObject(caps, "battery");
  cJSON *ota = cJSON_AddObjectToObject(caps, "ota");
  cJSON_AddNumberToObject(ota, "max", b->ota_max);
  return caps;
}

int gp_encode_hello(char *buf, size_t cap, const gp_hello_t *m) {
  cJSON *o = begin("hello");
  if (o == NULL) return GADGET_ERR_NO_MEM;
  cJSON_AddNumberToObject(o, "proto", GADGET_PROTO_VERSION);
  cJSON_AddStringToObject(o, "id", m->id);
  cJSON_AddStringToObject(o, "pubkey", m->pubkey_b64);
  cJSON_AddStringToObject(o, "name", m->name);
  cJSON_AddStringToObject(o, "board", m->board->id);
  cJSON_AddStringToObject(o, "fw", m->fw);
  cJSON_AddItemToObject(o, "caps", caps_json(m->board));
  cJSON *actions = cJSON_AddArrayToObject(o, "actions");
  for (uint8_t i = 0; i < m->n_actions; i++) {
    const gp_action_decl_t *a = &m->actions[i];
    cJSON *params = a->params_json ? cJSON_Parse(a->params_json) : NULL;
    if (params == NULL) {
      if (a->params_json != NULL) {
        cJSON_Delete(o);
        return GADGET_ERR_ARG;
      }
      params = cJSON_CreateObject();
      cJSON_AddStringToObject(params, "type", "object");
      cJSON_AddObjectToObject(params, "properties");
    }
    cJSON *it = cJSON_CreateObject();
    cJSON_AddStringToObject(it, "name", a->name);
    cJSON_AddStringToObject(it, "description", a->description);
    cJSON_AddItemToObject(it, "params", params);
    cJSON_AddStringToObject(it, "risk", a->risk == GADGET_RISK_SAFE ? "safe" : "confirm");
    cJSON_AddItemToArray(actions, it);
  }
  cJSON *sensors = cJSON_AddObjectToObject(o, "sensors");
  if (m->battery_valid) {
    cJSON_AddNumberToObject(sensors, "battery_pct", m->battery_pct);
    cJSON_AddBoolToObject(sensors, "charging", m->charging);
  }
  return finish(o, buf, cap);
}

int gp_encode_prove(char *buf, size_t cap, const gp_prove_t *m) {
  cJSON *o = begin("prove");
  if (o == NULL) return GADGET_ERR_NO_MEM;
  cJSON_AddStringToObject(o, "sig", m->sig_b64);
  if (m->enroll) cJSON_AddStringToObject(o, "enroll", m->enroll);
  return finish(o, buf, cap);
}

int gp_encode_voice_begin(char *buf, size_t cap, const gp_voice_begin_t *m) {
  cJSON *o = begin("voice.begin");
  if (o == NULL) return GADGET_ERR_NO_MEM;
  cJSON_AddStringToObject(o, "turn", m->turn);
  cJSON_AddNumberToObject(o, "stream", m->stream);
  cJSON_AddNumberToObject(o, "rate", m->rate);
  return finish(o, buf, cap);
}

int gp_encode_voice_end(char *buf, size_t cap, const gp_voice_end_t *m) {
  cJSON *o = begin("voice.end");
  if (o == NULL) return GADGET_ERR_NO_MEM;
  cJSON_AddStringToObject(o, "turn", m->turn);
  cJSON_AddNumberToObject(o, "ms", m->ms);
  return finish(o, buf, cap);
}

int gp_encode_voice_drop(char *buf, size_t cap, const gp_voice_drop_t *m) {
  cJSON *o = begin("voice.drop");
  if (o == NULL) return GADGET_ERR_NO_MEM;
  cJSON_AddStringToObject(o, "turn", m->turn);
  return finish(o, buf, cap);
}

int gp_encode_say(char *buf, size_t cap, const gp_say_t *m) {
  cJSON *o = begin("say");
  if (o == NULL) return GADGET_ERR_NO_MEM;
  cJSON_AddStringToObject(o, "turn", m->turn);
  cJSON_AddStringToObject(o, "text", m->text);
  return finish(o, buf, cap);
}

int gp_encode_stop(char *buf, size_t cap, const gp_stop_t *m) {
  cJSON *o = begin("stop");
  if (o == NULL) return GADGET_ERR_NO_MEM;
  if (m->turn) cJSON_AddStringToObject(o, "turn", m->turn);
  return finish(o, buf, cap);
}

int gp_encode_answer(char *buf, size_t cap, const gp_answer_t *m) {
  cJSON *o = begin("answer");
  if (o == NULL) return GADGET_ERR_NO_MEM;
  cJSON_AddStringToObject(o, "id", m->id);
  cJSON_AddStringToObject(o, "option", m->option);
  return finish(o, buf, cap);
}

int gp_encode_act_result(char *buf, size_t cap, const gp_act_result_t *m) {
  cJSON *o = begin("act.result");
  if (o == NULL) return GADGET_ERR_NO_MEM;
  cJSON_AddStringToObject(o, "id", m->id);
  cJSON_AddBoolToObject(o, "ok", m->ok);
  if (m->data != NULL && m->data->child != NULL) {
    cJSON_AddItemToObject(o, "data", cJSON_Duplicate(m->data, true));
  }
  if (m->error) cJSON_AddStringToObject(o, "error", m->error);
  return finish(o, buf, cap);
}

int gp_encode_sense(char *buf, size_t cap, const gp_sense_t *m) {
  cJSON *o = begin("sense");
  if (o == NULL) return GADGET_ERR_NO_MEM;
  if (m->battery_valid) {
    cJSON_AddNumberToObject(o, "battery_pct", m->battery_pct);
    cJSON_AddBoolToObject(o, "charging", m->charging);
  }
  return finish(o, buf, cap);
}

int gp_encode_event(char *buf, size_t cap, const gp_event_msg_t *m) {
  cJSON *o = begin("event");
  if (o == NULL) return GADGET_ERR_NO_MEM;
  cJSON_AddStringToObject(o, "name", m->name);
  if (m->data) cJSON_AddItemToObject(o, "data", cJSON_Duplicate(m->data, true));
  return finish(o, buf, cap);
}

int gp_encode_fw_ready(char *buf, size_t cap, const gp_fw_ready_t *m) {
  cJSON *o = begin("fw.ready");
  if (o == NULL) return GADGET_ERR_NO_MEM;
  cJSON_AddNumberToObject(o, "stream", m->stream);
  return finish(o, buf, cap);
}

int gp_encode_fw_fail(char *buf, size_t cap, const gp_fw_fail_t *m) {
  cJSON *o = begin("fw.fail");
  if (o == NULL) return GADGET_ERR_NO_MEM;
  cJSON_AddNumberToObject(o, "stream", m->stream);
  cJSON_AddStringToObject(o, "code", m->code);
  return finish(o, buf, cap);
}

int gp_encode_fw_progress(char *buf, size_t cap, const gp_fw_progress_t *m) {
  cJSON *o = begin("fw.progress");
  if (o == NULL) return GADGET_ERR_NO_MEM;
  cJSON_AddNumberToObject(o, "stream", m->stream);
  cJSON_AddNumberToObject(o, "offset", m->offset);
  return finish(o, buf, cap);
}

int gp_encode_fw_installed(char *buf, size_t cap, const gp_fw_installed_t *m) {
  cJSON *o = begin("fw.installed");
  if (o == NULL) return GADGET_ERR_NO_MEM;
  cJSON_AddStringToObject(o, "version", m->version);
  return finish(o, buf, cap);
}

/* ---- binary frames -------------------------------------------------------- */

size_t gp_bin_encode(uint8_t *out, size_t cap, gp_bin_kind_t kind, uint8_t stream, const uint8_t *payload, size_t len) {
  if (out == NULL || stream == 0 || len + 2 > cap || len + 2 > GADGET_BINARY_FRAME_MAX) return 0;
  out[0] = (uint8_t)kind;
  out[1] = stream;
  if (len) memcpy(out + 2, payload, len);
  return len + 2;
}

gadget_status_t gp_bin_decode(const uint8_t *frame, size_t len, gp_bin_kind_t *kind, uint8_t *stream,
                              const uint8_t **payload, size_t *payload_len) {
  if (frame == NULL || len < 2) return GADGET_ERR_PARSE;
  if (frame[0] < GP_BIN_MIC || frame[0] > GP_BIN_FIRMWARE || frame[1] == 0) return GADGET_ERR_PARSE;
  *kind = (gp_bin_kind_t)frame[0];
  *stream = frame[1];
  *payload = frame + 2;
  *payload_len = len - 2;
  return GADGET_OK;
}

gadget_status_t gp_fw_chunk_decode(const uint8_t *payload, size_t len, uint32_t *offset, const uint8_t **data,
                                   size_t *data_len) {
  if (payload == NULL || len < 5 || len - 4 > GADGET_FW_CHUNK_MAX) return GADGET_ERR_PARSE;
  *offset = (uint32_t)payload[0] | ((uint32_t)payload[1] << 8) | ((uint32_t)payload[2] << 16) |
            ((uint32_t)payload[3] << 24);
  *data = payload + 4;
  *data_len = len - 4;
  return GADGET_OK;
}

/* ---- signed texts ---------------------------------------------------------- */

int gp_prove_text(char *out, size_t cap, const char *id, const char *nonce_b64, const char *host_id) {
  int n = snprintf(out, cap, "openmausbot-gadget/1\nprove\n%s\n%s\n%s", id, nonce_b64, host_id);
  return (n < 0 || (size_t)n >= cap) ? GADGET_ERR_LIMIT : n;
}

int gp_firmware_text(char *out, size_t cap, const char *board, const char *version, uint32_t size,
                     const char *sha256_hex) {
  int n = snprintf(out, cap, "openmausbot-gadget/1\nfirmware\n%s\n%s\n%" PRIu32 "\n%s", board, version, size,
                   sha256_hex);
  return (n < 0 || (size_t)n >= cap) ? GADGET_ERR_LIMIT : n;
}
