# firmware/core/CMakeLists.txt
# SPDX-License-Identifier: Apache-2.0
# Dual-use: an ESP-IDF component when ESP_PLATFORM is set (plan P2c may
# correct that branch), a static library for the desktop build otherwise.
set(GADGET_CORE_SRCS
  src/util.c
  src/proto.c
  src/crypto_psa.c
  src/boards.c
  src/ui_layout.c
  src/console.c
  src/core.c
  src/session.c
  src/screens.c
)

if(ESP_PLATFORM)
  idf_component_register(SRCS ${GADGET_CORE_SRCS}
                         INCLUDE_DIRS include
                         REQUIRES espressif__cjson mbedtls)
  return()
endif()

add_library(gadget_core STATIC ${GADGET_CORE_SRCS})
target_include_directories(gadget_core PUBLIC include)
target_link_libraries(gadget_core PUBLIC cjson gadget_mbedcrypto)
gadget_warnings(gadget_core)
