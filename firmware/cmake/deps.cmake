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

FetchContent_MakeAvailable(cjson unity)

add_library(cjson STATIC ${cjson_SOURCE_DIR}/cJSON.c)
target_include_directories(cjson PUBLIC ${cjson_SOURCE_DIR})
set_target_properties(cjson PROPERTIES C_EXTENSIONS OFF)
if(UNIX AND NOT APPLE)
  target_link_libraries(cjson PUBLIC m)   # cJSON uses fabs/floor; macOS has libm in libSystem
endif()
