# SPDX-License-Identifier: Apache-2.0
# The ESP32 port: HAL implementations, the main loop, LVGL glue, the pure
# logic shared with host-tests/ (logic/), the board's own glue
# (boards/<board>/board.c) and the drivers its optional boards/<board>/board.cmake
# lists (every driver in drivers/ when it has none).
idf_build_get_property(gadget_board GADGET_BOARD)
set(board_dir "${CMAKE_CURRENT_LIST_DIR}/../boards/${gadget_board}")
set(BOARD_DRIVER_SRCS "")
# board.cmake is optional (a size optimisation): without it every shared driver
# compiles, and --gc-sections drops the ones the board never calls.
include("${board_dir}/board.cmake" OPTIONAL RESULT_VARIABLE board_cmake)
if(NOT board_cmake)
  file(GLOB BOARD_DRIVER_SRCS RELATIVE "${CMAKE_CURRENT_LIST_DIR}/drivers" "${CMAKE_CURRENT_LIST_DIR}/drivers/*.c")
endif()
list(TRANSFORM BOARD_DRIVER_SRCS PREPEND "drivers/")

set(port_srcs
  main.c
  port_events.c
  hal_system.c
  console_usj.c
  hal_storage.c
  hal_wifi.c
  hal_ws.c)

set(logic_srcs
  logic/pl_event.c
  logic/pl_util.c
  logic/pl_scan.c
  logic/pl_wifi.c
  logic/pl_wsasm.c)

set(port_include_dirs "." "logic" "${board_dir}")

idf_component_register(
  SRCS ${port_srcs} ${logic_srcs} ${BOARD_DRIVER_SRCS}
  INCLUDE_DIRS ${port_include_dirs}
  REQUIRES core ui lvgl__lvgl espressif__cjson mbedtls nvs_flash esp_wifi esp_netif esp_event esp_timer
           esp_driver_gpio esp_driver_i2c esp_driver_i2s esp_driver_spi esp_driver_ledc esp_driver_usb_serial_jtag
           esp_adc esp_lcd app_update spi_flash esp_app_format
           espressif__esp_websocket_client espressif__mdns espressif__esp_codec_dev
           espressif__esp_lcd_co5300 waveshare__esp_lcd_touch_cst9217 espressif__esp_lcd_touch)

if(NOT CMAKE_BUILD_EARLY_EXPANSION AND NOT "${CONFIG_GADGET_BOARD_ID}" STREQUAL "${gadget_board}")
  message(FATAL_ERROR "sdkconfig says board '${CONFIG_GADGET_BOARD_ID}' but GADGET_BOARD is '${gadget_board}'. "
                      "Use -D SDKCONFIG=build/${gadget_board}/sdkconfig (one sdkconfig per board).")
endif()
