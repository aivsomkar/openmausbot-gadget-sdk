# firmware/cmake/deps.cmake
# SPDX-License-Identifier: Apache-2.0
# Pinned third-party sources for the desktop build (contract §1.4).
include(FetchContent)

# cJSON 1.7.19 (MIT). Populate only: its own CMakeLists needs CMake < 3.5
# compatibility, which CMake 4 removed. Included as "cJSON.h", the same
# spelling as espressif/cjson on ESP-IDF.
FetchContent_Declare(cjson
  URL https://github.com/DaveGamble/cJSON/archive/refs/tags/v1.7.19.tar.gz
  URL_HASH SHA256=7fa616e3046edfa7a28a32d5f9eacfd23f92900fe1f8ccd988c1662f30454562
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
  SOURCE_SUBDIR _populate_only_)

# Unity 2.7.0 (MIT), the C test framework. Target unity::framework.
FetchContent_Declare(unity
  URL https://github.com/ThrowTheSwitch/Unity/archive/refs/tags/v2.7.0.tar.gz
  URL_HASH SHA256=e84eb301ca7967831e68b1728f911e87fa2d345d8ddb64f897bc2f2ee24a321c
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE)

# mbedTLS (Apache-2.0), PSA Crypto only. 3.6.7 matches ESP-IDF 5.5's 3.6.x;
# the 4.2.0 CI leg matches ESP-IDF 6.0's 4.x. Target mbedcrypto (3.6) or
# tfpsacrypto (4.x), wrapped as gadget_mbedcrypto.
if(GADGET_MBEDTLS_VERSION STREQUAL "3.6.7")
  set(_gadget_mbedtls_sha a7e8bcbec0e6f761b4af24f25677626b35f762f68eef79c08677a363212d11f6)
elseif(GADGET_MBEDTLS_VERSION STREQUAL "4.2.0")
  set(_gadget_mbedtls_sha 2bed9d713b4668f76553b097e72b8aa30bc8f112a940d7ae228d524bbde6ffea)
else()
  message(FATAL_ERROR "GADGET_MBEDTLS_VERSION must be 3.6.7 or 4.2.0, not ${GADGET_MBEDTLS_VERSION}")
endif()
set(ENABLE_PROGRAMS OFF CACHE BOOL "" FORCE)
set(ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(GEN_FILES OFF CACHE BOOL "" FORCE)
set(MBEDTLS_FATAL_WARNINGS OFF CACHE BOOL "" FORCE)
set(TF_PSA_CRYPTO_FATAL_WARNINGS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(mbedtls
  URL https://github.com/Mbed-TLS/mbedtls/releases/download/mbedtls-${GADGET_MBEDTLS_VERSION}/mbedtls-${GADGET_MBEDTLS_VERSION}.tar.bz2
  URL_HASH SHA256=${_gadget_mbedtls_sha}
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE)

# wslay 1.1.1 (MIT), the simulator's WebSocket framing. Populate only: its
# CMakeLists needs CMake < 3.5 compatibility; we compile its five sources.
FetchContent_Declare(wslay
  URL https://github.com/tatsuhiro-t/wslay/archive/refs/tags/release-1.1.1.tar.gz
  URL_HASH SHA256=7b9f4b9df09adaa6e07ec309b68ab376c0db2cfd916613023b52a47adfda224a
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
  SOURCE_SUBDIR _populate_only_)

FetchContent_MakeAvailable(cjson unity mbedtls wslay)

add_library(gadget_mbedcrypto INTERFACE)
if(TARGET tfpsacrypto)
  target_link_libraries(gadget_mbedcrypto INTERFACE tfpsacrypto)
else()
  target_link_libraries(gadget_mbedcrypto INTERFACE mbedcrypto)
endif()

add_library(cjson STATIC ${cjson_SOURCE_DIR}/cJSON.c)
target_include_directories(cjson PUBLIC ${cjson_SOURCE_DIR})
set_target_properties(cjson PROPERTIES C_EXTENSIONS OFF)
if(UNIX AND NOT APPLE)
  target_link_libraries(cjson PUBLIC m)   # cJSON uses fabs/floor; macOS has libm in libSystem
endif()

set(_wslay_gen ${CMAKE_BINARY_DIR}/wslay_gen)
file(MAKE_DIRECTORY ${_wslay_gen}/wslay)
file(WRITE ${_wslay_gen}/wslay/wslayver.h "#ifndef WSLAYVER_H\n#define WSLAYVER_H\n#define WSLAY_VERSION \"1.1.1\"\n#endif\n")
file(WRITE ${_wslay_gen}/config.h "#define HAVE_ARPA_INET_H 1\n#define HAVE_NETINET_IN_H 1\n")
add_library(wslay STATIC
  ${wslay_SOURCE_DIR}/lib/wslay_event.c
  ${wslay_SOURCE_DIR}/lib/wslay_frame.c
  ${wslay_SOURCE_DIR}/lib/wslay_net.c
  ${wslay_SOURCE_DIR}/lib/wslay_queue.c
  ${wslay_SOURCE_DIR}/lib/wslay_stack.c)
target_include_directories(wslay PUBLIC ${wslay_SOURCE_DIR}/lib/includes ${_wslay_gen} PRIVATE ${wslay_SOURCE_DIR}/lib)
target_compile_definitions(wslay PRIVATE HAVE_CONFIG_H _POSIX_C_SOURCE=200809L)
if(APPLE)
  target_compile_definitions(wslay PRIVATE _DARWIN_C_SOURCE)
endif()
