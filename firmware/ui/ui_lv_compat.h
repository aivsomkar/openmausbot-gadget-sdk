/* SPDX-License-Identifier: Apache-2.0 */
/* Every object-flag change in firmware/ui goes through these wrappers.
 * LVGL 9.6 deprecates lv_obj_add_flag/remove_flag/has_flag (compile warning
 * plus a runtime log line per call) in favour of per-flag setters that 9.5
 * lacks, so the 9.5.0 fallback (contract 1.4) stays a version-pin change. */
#ifndef UI_LV_COMPAT_H
#define UI_LV_COMPAT_H

#include <stdbool.h>
#include "lvgl.h"

#if LVGL_VERSION_MAJOR > 9 || (LVGL_VERSION_MAJOR == 9 && LVGL_VERSION_MINOR >= 6)
static inline void ui_set_hidden(lv_obj_t *obj, bool hidden) { lv_obj_set_hidden(obj, hidden); }
static inline void ui_set_clickable(lv_obj_t *obj, bool on) { lv_obj_set_clickable(obj, on); }
static inline void ui_set_scrollable(lv_obj_t *obj, bool on) { lv_obj_set_scrollable(obj, on); }
static inline bool ui_is_hidden(const lv_obj_t *obj) { return lv_obj_is_hidden(obj); }
#else
static inline void ui_set_hidden(lv_obj_t *obj, bool hidden) {
  if (hidden) lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
  else lv_obj_remove_flag(obj, LV_OBJ_FLAG_HIDDEN);
}
static inline void ui_set_clickable(lv_obj_t *obj, bool on) {
  if (on) lv_obj_add_flag(obj, LV_OBJ_FLAG_CLICKABLE);
  else lv_obj_remove_flag(obj, LV_OBJ_FLAG_CLICKABLE);
}
static inline void ui_set_scrollable(lv_obj_t *obj, bool on) {
  if (on) lv_obj_add_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
  else lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
}
static inline bool ui_is_hidden(const lv_obj_t *obj) { return lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN); }
#endif

/* A plain, inert container: no theme style, no click, no scroll. */
static inline lv_obj_t *ui_box(lv_obj_t *parent) {
  lv_obj_t *o = lv_obj_create(parent);
  lv_obj_remove_style_all(o);
  ui_set_clickable(o, false);
  ui_set_scrollable(o, false);
  return o;
}

#endif /* UI_LV_COMPAT_H */
