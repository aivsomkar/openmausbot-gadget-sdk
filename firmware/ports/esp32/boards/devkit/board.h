/* SPDX-License-Identifier: Apache-2.0 */
/* Breadboard build: ESP32-S3-DevKitC-1-N16R8 (16 MB flash, 8 MB octal
 * PSRAM), a 2" ST7789 240x320 module used as 320x240 landscape, an INMP441
 * I2S microphone and a MAX98357A I2S amplifier. The wiring is ours; it
 * avoids flash/PSRAM pins (26-37), USB (19/20), UART0 (43/44), strapping
 * pins 3/45/46 and the RGB LED (38/48). Use the USB-C port labelled USB
 * (native USB-Serial-JTAG), not the one labelled UART. */
#ifndef BOARD_H
#define BOARD_H

/* Display: ST7789 on SPI2 */
#define BOARD_LCD_SCLK 12
#define BOARD_LCD_MOSI 11
#define BOARD_LCD_CS 10
#define BOARD_LCD_DC 9
#define BOARD_LCD_RST 14
#define BOARD_LCD_BL 21
#define BOARD_LCD_SPI_MODE 0
#define BOARD_LCD_PCLK_HZ 40000000
#define BOARD_LCD_SWAP_XY 1          /* landscape */
#define BOARD_LCD_MIRROR_X 1
#define BOARD_LCD_MIRROR_Y 0
#define BOARD_LCD_X_GAP 0
#define BOARD_LCD_Y_GAP 0
#define BOARD_LCD_BUF_LINES 40

/* INMP441 microphone on I2S1 (L/R tied to GND → left slot) */
#define BOARD_MIC_I2S_PORT 1
#define BOARD_MIC_BCLK 4
#define BOARD_MIC_WS 5
#define BOARD_MIC_SD 6
#define BOARD_MIC_SHIFT 14

/* MAX98357A amplifier on I2S0 (separate clock → 24 kHz speaker) */
#define BOARD_SPK_I2S_PORT 0
#define BOARD_SPK_BCLK 15
#define BOARD_SPK_LRC 16
#define BOARD_SPK_DIN 17
#define BOARD_SPK_RATE 24000

/* Buttons, active low with internal pull-ups */
#define BOARD_BTN_TALK 0             /* BOOT */
#define BOARD_BTN_CANCEL 1           /* push button from GPIO1 to GND */

#endif /* BOARD_H */
