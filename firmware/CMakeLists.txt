# firmware/CMakeLists.txt
# SPDX-License-Identifier: Apache-2.0
# Desktop build only: core, the simulator and the C tests. ESP-IDF builds
# boards from firmware/ports/esp32 with its own project file (plan P2c).
cmake_minimum_required(VERSION 3.24)
project(openmausbot_gadget_host C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# firmware/ui and the LVGL/SDL simulator display (plan P2b), on by default
# since P2b; OFF builds core and the simulator without the LVGL UI.
option(GADGET_WITH_LVGL "Build the LVGL UI and the LVGL/SDL simulator display (plan P2b)" ON)
option(GADGET_BUILD_SIM "Build the gadget-sim simulator" ON)
option(GADGET_BUILD_TESTS "Build the C tests" ON)
option(GADGET_E2E "Register the simulator <-> fake-host e2e tests (needs node >= 22.18 and npm ci)" ON)
option(GADGET_TEST_KEYS "Compile the t1 test signing key into gadget_core" ON)
option(GADGET_SANITIZE "Build our targets with -fsanitize=address,undefined" OFF)
set(GADGET_MBEDTLS_VERSION "3.6.7" CACHE STRING "mbedTLS for the desktop build: 3.6.7 or 4.2.0")
set_property(CACHE GADGET_MBEDTLS_VERSION PROPERTY STRINGS 3.6.7 4.2.0)
set(GADGET_SIM_VERSION "0.0.0-dev" CACHE STRING "The simulator's fw version before any OTA")
set(GADGET_VECTORS_DIR "${CMAKE_SOURCE_DIR}/../protocol/vectors")
# The C tests sign OTA images with the t1 test key (contract §1.7).
if(GADGET_BUILD_TESTS AND NOT GADGET_TEST_KEYS)
  message(FATAL_ERROR "The C tests sign OTA images with the t1 test key: use -DGADGET_TEST_KEYS=ON or -DGADGET_BUILD_TESTS=OFF")
endif()

include(cmake/warnings.cmake)
include(cmake/deps.cmake)

add_subdirectory(core)
if(GADGET_WITH_LVGL)
  add_subdirectory(ui)
endif()

if(GADGET_BUILD_SIM)
  add_subdirectory(ports/sim)
endif()

if(GADGET_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
