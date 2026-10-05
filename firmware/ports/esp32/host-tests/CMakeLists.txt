# SPDX-License-Identifier: Apache-2.0
# Host-side unit tests for the ESP32 port's pure logic (main/logic/) and for
# the port's data files. Runs on macOS and Linux with any C11 compiler; no
# ESP-IDF needed.
cmake_minimum_required(VERSION 3.24)
project(gadget_esp32_host_tests C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

include(FetchContent)
FetchContent_Declare(unity
  URL https://github.com/ThrowTheSwitch/Unity/archive/refs/tags/v2.7.0.tar.gz
  URL_HASH SHA256=e84eb301ca7967831e68b1728f911e87fa2d345d8ddb64f897bc2f2ee24a321c)
FetchContent_MakeAvailable(unity)

set(ESP_DIR ${CMAKE_CURRENT_LIST_DIR}/..)
set(CORE_DIR ${ESP_DIR}/../../core)

# Port logic under test. Each task appends its files here.
set(PORT_LOGIC_SRCS
  ${CORE_DIR}/src/boards.c
  ${ESP_DIR}/main/logic/pl_event.c
  ${ESP_DIR}/main/logic/pl_util.c
  ${ESP_DIR}/main/logic/pl_scan.c
  ${ESP_DIR}/main/logic/pl_wifi.c
  ${ESP_DIR}/main/logic/pl_wsasm.c
)

add_library(port_logic STATIC ${PORT_LOGIC_SRCS})
target_include_directories(port_logic PUBLIC ${ESP_DIR}/main/logic ${CORE_DIR}/include)
target_compile_options(port_logic PRIVATE -Wall -Wextra -Werror)

enable_testing()
function(port_test name)
  add_executable(${name} ${name}.c)
  target_link_libraries(${name} PRIVATE port_logic unity::framework)
  target_compile_options(${name} PRIVATE -Wall -Wextra -Werror)
  add_test(NAME esp32.${name} COMMAND ${name} ${ARGN})
  set_tests_properties(esp32.${name} PROPERTIES LABELS unit TIMEOUT 30)
endfunction()

port_test(test_partitions ${ESP_DIR}/partitions/16mb.csv)
port_test(test_board_defaults ${ESP_DIR})
port_test(test_pl_event)
port_test(test_pl_util)
port_test(test_pl_scan)
port_test(test_pl_wifi)
port_test(test_pl_wsasm)
