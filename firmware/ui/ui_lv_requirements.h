/* SPDX-License-Identifier: Apache-2.0 */
/* The LVGL options the UI needs, checked at compile time against whichever
 * configuration is active: firmware/ui/lv_conf.h (simulator, tests) or the
 * CONFIG_LV_* Kconfig values from sdkconfig.defaults (ESP32, P2c). */
#ifndef UI_LV_REQUIREMENTS_H
#define UI_LV_REQUIREMENTS_H

#include "lvgl.h"

#if LVGL_VERSION_MAJOR != 9 || LVGL_VERSION_MINOR < 5
#error "firmware/ui needs LVGL 9.5 or 9.6 (contract 1.4: 9.6.0)"
#endif
#if !LV_USE_ANIMIMG
#error "LVGL config: LV_USE_ANIMIMG must be 1"
#endif
#if !LV_DRAW_SW_SUPPORT_RGB565A8
#error "LVGL config: LV_DRAW_SW_SUPPORT_RGB565A8 must be 1 (Maus body and mouths)"
#endif
#if !LV_DRAW_SW_SUPPORT_A8
#error "LVGL config: LV_DRAW_SW_SUPPORT_A8 must be 1 (Maus eyes)"
#endif
#if LV_USE_OS != LV_OS_NONE
#error "LVGL config: LV_USE_OS must be LV_OS_NONE (core and UI are single-threaded)"
#endif
#if LV_COLOR_DEPTH != 16
#error "LVGL config: the display colour format must be RGB565 (16-bit)"
#endif
/* LVGL defaults the UI relies on (P2c keeps them on in sdkconfig.defaults) */
#if !LV_DRAW_SW_COMPLEX
#error "LVGL config: LV_DRAW_SW_COMPLEX must be 1 (arcs, rounded buttons, the round mask)"
#endif
#if !LV_USE_LABEL || !LV_USE_IMAGE || !LV_USE_ARC || !LV_USE_BAR
#error "LVGL config: the label, image, arc and bar widgets must be enabled"
#endif
/* The Image screen refills one of two lv_image_dsc_t slots per picture; LVGL's
 * caches are keyed by that pointer and would show a stale header or pixels. */
#if LV_CACHE_DEF_SIZE || LV_IMAGE_HEADER_CACHE_DEF_CNT
#error "LVGL config: LV_CACHE_DEF_SIZE and LV_IMAGE_HEADER_CACHE_DEF_CNT must be 0 (image caches off)"
#endif

#endif /* UI_LV_REQUIREMENTS_H */
