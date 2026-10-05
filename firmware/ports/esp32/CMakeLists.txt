# SPDX-License-Identifier: Apache-2.0
# ESP-IDF project for the gadget firmware (spec §5.1). Build one board at a
# time, each with its own build directory and sdkconfig:
#   idf.py -B build/<board> -D GADGET_BOARD=<board> -D SDKCONFIG=build/<board>/sdkconfig build
# Optional, each in its own build directory: -D PROJECT_VER=<version>
# (release CI), -D GADGET_NVS_ENCRYPT=1 (opt-in NVS encryption, burns an eFuse
# on first boot: sdkconfig.nvs-encrypt), -D GADGET_TEST_KEYS=1 (bench OTA
# tests only, never published: sdkconfig.test-keys).
cmake_minimum_required(VERSION 3.22)

if(NOT DEFINED GADGET_BOARD OR "${GADGET_BOARD}" STREQUAL "")
  message(FATAL_ERROR "GADGET_BOARD is not set. Pass -D GADGET_BOARD=<board>, one of the directories in boards/.")
endif()
if(NOT GADGET_BOARD MATCHES "^[a-z0-9-]+$" OR NOT IS_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}/boards/${GADGET_BOARD}")
  message(FATAL_ERROR "Unknown board '${GADGET_BOARD}': there is no directory boards/${GADGET_BOARD}.")
endif()
# Test-key and NVS-encryption builds stay out of the standard build directory:
# their cache entries and CONFIG_ lines would stick to every later build there
# (build-all.sh, check-size.sh, release builds).
get_filename_component(_gadget_bin "${CMAKE_BINARY_DIR}" REALPATH)
get_filename_component(_gadget_std "${CMAKE_CURRENT_LIST_DIR}/build/${GADGET_BOARD}" REALPATH)
if((GADGET_TEST_KEYS OR GADGET_NVS_ENCRYPT) AND _gadget_bin STREQUAL _gadget_std)
  message(FATAL_ERROR "Build test-key or NVS-encryption variants in their own -B directory, not build/${GADGET_BOARD} "
                      "(if build/${GADGET_BOARD} already has them cached, delete it and build again).")
endif()

# Board defaults come last so they win over the shared file.
set(SDKCONFIG_DEFAULTS "sdkconfig.defaults;boards/${GADGET_BOARD}/sdkconfig.defaults")
if(GADGET_NVS_ENCRYPT)
  list(APPEND SDKCONFIG_DEFAULTS "sdkconfig.nvs-encrypt")
endif()
if(GADGET_TEST_KEYS)
  list(APPEND SDKCONFIG_DEFAULTS "sdkconfig.test-keys")
endif()

# A local build never takes version.txt or `git describe`: custom builds are -dev.
if(NOT DEFINED PROJECT_VER)
  set(PROJECT_VER "0.0.0-dev")
endif()

set(EXTRA_COMPONENT_DIRS
  "${CMAKE_CURRENT_LIST_DIR}/../../core"
  "${CMAKE_CURRENT_LIST_DIR}/../../ui")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
# One lock file per board, so the four builds never rewrite each other's.
idf_build_set_property(DEPENDENCIES_LOCK "${CMAKE_CURRENT_LIST_DIR}/dependencies.lock.${GADGET_BOARD}")
# main/CMakeLists.txt reads the board id (build properties reach every component).
idf_build_set_property(GADGET_BOARD "${GADGET_BOARD}")
project(openmausbot-gadget)
