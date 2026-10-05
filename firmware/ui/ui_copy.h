/* SPDX-License-Identifier: Apache-2.0 */
/* Screen copy (contract 2.15): pure functions over the model, no LVGL, so
 * the exact English strings are unit-tested on their own. */
#ifndef UI_COPY_H
#define UI_COPY_H

#include <stddef.h>
#include <stdint.h>
#include "gadget_ui_model.h"

#define UI_ELLIPSIS "\xE2\x80\xA6" /* U+2026 */
#define UI_ARROW "\xE2\x86\x92"    /* U+2192 */
#define UI_COPY_MAX 512
#define UI_COPY_ASK_ELSEWHERE "Answer on your computer or phone"

typedef struct {
  char caption[UI_COPY_MAX]; /* main text, lines separated by '\n' */
  char host[UI_NAME_MAX];    /* the host name on its own line (spec 5.5), "" for none */
  char status[UI_COPY_MAX];  /* one line at the bottom, "" for none */
} ui_copy_t;

/* Whole seconds until retry_at_ms, rounded up, never below 1. */
uint32_t ui_retry_seconds(uint64_t now_ms, uint64_t retry_at_ms);

void ui_copy_idle(const ui_model_t *m, char *out, size_t cap);
void ui_copy_setup(const ui_model_t *m, ui_copy_t *out);
void ui_copy_offline(const ui_model_t *m, ui_copy_t *out);
void ui_copy_update(const ui_model_t *m, char *out, size_t cap);
/* "5" … "1" during the last 5 s of a recording, "" otherwise. */
void ui_copy_countdown(const ui_model_t *m, char *out, size_t cap);

#endif /* UI_COPY_H */
