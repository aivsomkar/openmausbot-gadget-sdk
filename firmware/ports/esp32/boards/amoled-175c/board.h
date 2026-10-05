/* SPDX-License-Identifier: Apache-2.0 */
/* Waveshare ESP32-S3-Touch-AMOLED-1.75C. Pins from the Waveshare schematic
 * for this board; GPIO numbers are plain ints so this header needs no
 * includes. 8 MB octal PSRAM; built with the 16 MB flash layout (some units
 * carry 32 MB; main.c logs the fitted size). */
#ifndef BOARD_H
#define BOARD_H

/* Display: CO5300 466x466 round AMOLED on QSPI (SPI2) */
#define BOARD_LCD_SCLK 38
#define BOARD_LCD_D0 4
#define BOARD_LCD_D1 5
#define BOARD_LCD_D2 6
#define BOARD_LCD_D3 7
#define BOARD_LCD_CS 12
#define BOARD_LCD_RST 1
#define BOARD_LCD_TE 13              /* tearing-effect output, unused in v1 */
#define BOARD_LCD_X_GAP 6            /* visible columns start at 6 */
#define BOARD_LCD_EVEN_AREAS 1       /* CO5300: areas start even and end odd */
#define BOARD_LCD_BUF_LINES 40       /* two 466 x 40 x 2 B internal DMA buffers */

/* CO5300 register writes for this 466x466 panel, from the vendor's published
 * panel bring-up: {command, data, data bytes, delay ms after}. The column
 * window 6..471 matches BOARD_LCD_X_GAP. board.c turns this into a
 * co5300_lcd_init_cmd_t array; NULL there selects the component's table. */
#define BOARD_CO5300_INIT_CMDS {                                                       \
    {0xFE, (const uint8_t[]){0x20}, 1, 0},                     /* manufacturer page */ \
    {0x19, (const uint8_t[]){0x10}, 1, 0},                                             \
    {0x1C, (const uint8_t[]){0xA0}, 1, 0},                                             \
    {0xFE, (const uint8_t[]){0x00}, 1, 0},                     /* user command page */ \
    {0xC4, (const uint8_t[]){0x80}, 1, 0},                     /* QSPI interface */    \
    {0x3A, (const uint8_t[]){0x55}, 1, 0},                     /* 16 bits per pixel */ \
    {0x35, (const uint8_t[]){0x00}, 1, 0},                     /* tearing effect on */ \
    {0x53, (const uint8_t[]){0x20}, 1, 0},                     /* brightness control */\
    {0x51, (const uint8_t[]){0xFF}, 1, 0},                     /* brightness */        \
    {0x63, (const uint8_t[]){0xFF}, 1, 0},                     /* HBM level */         \
    {0x2A, (const uint8_t[]){0x00, 0x06, 0x01, 0xD7}, 4, 0},   /* columns 6..471 */    \
    {0x2B, (const uint8_t[]){0x00, 0x00, 0x01, 0xD1}, 4, 600}, /* rows 0..465 */       \
    {0x11, NULL, 0, 600},                                      /* sleep out */         \
    {0x29, NULL, 0, 0},                                        /* display on */        \
  }

/* Touch: CST9217 (0x5A) on the I2C bus */
#define BOARD_TP_RST 2
#define BOARD_TP_INT 11
#define BOARD_TP_MIRROR_X 1
#define BOARD_TP_MIRROR_Y 1

/* I2C: ES8311 0x18, ES7210 0x40, AXP2101 0x34, QMI8658 0x6B, CST9217 0x5A; 2.2 k pull-ups */
#define BOARD_I2C_PORT 0
#define BOARD_I2C_SDA 15
#define BOARD_I2C_SCL 14

/* Audio: ES8311 DAC + ES7210 ADC on one I2S port, shared clock → 16 kHz duplex */
#define BOARD_I2S_PORT 0
#define BOARD_I2S_MCLK 16
#define BOARD_I2S_BCLK 9
#define BOARD_I2S_WS 45
#define BOARD_I2S_DOUT 8             /* to ES8311 DSDIN */
#define BOARD_I2S_DIN 10             /* from ES7210 SDOUT1: MIC1, left slot. MIC3 is the echo reference (v2) */
#define BOARD_PA_EN 46               /* NS4150B amplifier, active high */
#define BOARD_MIC_GAIN_DB 30.0f

/* Buttons. PWR is the AXP2101 power key, mirrored to GPIO3 through a
 * transistor: GPIO3 reads high while PWR is pressed. Holding PWR for 6 s
 * makes the AXP2101 cut power (spec §5.3). */
#define BOARD_BTN_TALK 0             /* BOOT, active low, external pull-up */
#define BOARD_BTN_CANCEL 3           /* PWR mirror, active high */

#endif /* BOARD_H */
