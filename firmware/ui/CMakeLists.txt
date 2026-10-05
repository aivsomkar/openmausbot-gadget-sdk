# SPDX-License-Identifier: Apache-2.0
# firmware/ui: LVGL screens, the Maus animation, generated art and fonts.
# Dual-use like core: an ESP-IDF component (built by P2c's project) or a
# desktop static library (GADGET_WITH_LVGL builds).
# ESP-IDF early expansion runs this file in script mode: no sdkconfig values and no
# file(GLOB CONFIGURE_DEPENDS). Report the requirements only.
if(ESP_PLATFORM AND CMAKE_BUILD_EARLY_EXPANSION)
  idf_component_register(INCLUDE_DIRS . art fonts REQUIRES lvgl__lvgl core)
  return()
endif()
set(UI_SRCS
  ui.c ui_screens.c ui_maus.c ui_maus_engine.c ui_pager.c ui_copy.c ui_metrics.c
  art/maus_art.c)
file(GLOB UI_FONT_SRCS CONFIGURE_DEPENDS ${CMAKE_CURRENT_LIST_DIR}/fonts/*.c)

if(ESP_PLATFORM)
  # One art profile per board (contract 2.15): CONFIG_GADGET_ART_PROFILE is s240 or s150.
  set(_profile "${CONFIG_GADGET_ART_PROFILE}")
  if(NOT CMAKE_BUILD_EARLY_EXPANSION AND NOT _profile MATCHES "^(s240|s150)$")
    message(FATAL_ERROR "CONFIG_GADGET_ART_PROFILE must be s240 or s150, not '${_profile}'")
  endif()
  file(GLOB UI_ART_SRCS CONFIGURE_DEPENDS ${CMAKE_CURRENT_LIST_DIR}/art/${_profile}/*.c)
  idf_component_register(SRCS ${UI_SRCS} ${UI_FONT_SRCS} ${UI_ART_SRCS}
                         INCLUDE_DIRS . art fonts
                         REQUIRES lvgl__lvgl core)
  string(TOUPPER "${_profile}" _profile_upper)
  target_compile_definitions(${COMPONENT_LIB} PRIVATE MAUS_ART_HAS_${_profile_upper}=1)
else()
  file(GLOB UI_ART_SRCS CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_LIST_DIR}/art/s240/*.c ${CMAKE_CURRENT_LIST_DIR}/art/s150/*.c)
  add_library(gadget_ui STATIC ${UI_SRCS} ${UI_FONT_SRCS} ${UI_ART_SRCS})
  target_include_directories(gadget_ui PUBLIC ${CMAKE_CURRENT_LIST_DIR} ${CMAKE_CURRENT_LIST_DIR}/art ${CMAKE_CURRENT_LIST_DIR}/fonts)
  target_link_libraries(gadget_ui PUBLIC gadget_core lvgl::lvgl)
  target_compile_definitions(gadget_ui PRIVATE MAUS_ART_HAS_S240=1 MAUS_ART_HAS_S150=1)
  set_target_properties(gadget_ui PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
  # Our sources (hand-written and tools/art output): the contract's warning set.
  # lv_font_conv output is third-party generated code: default warnings only.
  set_source_files_properties(${UI_SRCS} ${UI_ART_SRCS} PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra;-Wpedantic;-Werror")
  if(GADGET_SANITIZE)
    # PUBLIC: every test and gadget-sim linking gadget_ui runs sanitized too.
    # -fno-sanitize-recover as in cmake/warnings.cmake: a UBSan finding aborts
    # and fails its test instead of printing and passing.
    target_compile_options(gadget_ui PUBLIC -fsanitize=address,undefined
      -fno-sanitize-recover=undefined -fno-omit-frame-pointer)
    target_link_options(gadget_ui PUBLIC -fsanitize=address,undefined -fno-sanitize-recover=undefined)
  endif()
endif()
