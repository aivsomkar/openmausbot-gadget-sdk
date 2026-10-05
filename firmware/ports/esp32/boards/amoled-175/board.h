/* SPDX-License-Identifier: Apache-2.0 */
/* Waveshare ESP32-S3-Touch-AMOLED-1.75. Pins from the Waveshare schematic
 * for this board. 16 MB flash, 8 MB octal PSRAM. Differs from the 1.75C in
 * the LCD reset, touch reset and MCLK pins; the PWR key is only readable
 * through the TCA9554 expander (EXIO4), so v1 has no CANCEL button here. */
#ifndef BOARD_H
#define BOARD_H

/* Display: CO5300 466x466 round AMOLED on QSPI (SPI2) */
#define BOARD_LCD_SCLK 38
#define BOARD_LCD_D0 4
#define BOARD_LCD_D1 5
#define BOARD_LCD_D2 6
#define BOARD_LCD_D3 7
#define BOARD_LCD_CS 12
#define BOARD_LCD_RST 39
#define BOARD_LCD_TE 13
#define BOARD_LCD_X_GAP 6
#define BOARD_LCD_EVEN_AREAS 1
#define BOARD_LCD_BUF_LINES 40

/* CO5300 register writes for this 466x466 panel, from the vendor's published
 * panel bring-up (the same panel as the 1.75C): {command, data, data bytes,
 * delay ms after}. Columns 6..471 match BOARD_LCD_X_GAP. */
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

/* Touch: CST9217 (0x5A) */
#define BOARD_TP_RST 40
#define BOARD_TP_INT 11
#define BOARD_TP_MIRROR_X 1
#define BOARD_TP_MIRROR_Y 1

/* I2C: AXP2101 0x34, ES8311 0x18, ES7210 0x40, QMI8658 0x6B, PCF85063 0x51, TCA9554 0x20 */
#define BOARD_I2C_PORT 0
#define BOARD_I2C_SDA 15
#define BOARD_I2C_SCL 14

/* Audio: ES8311 + ES7210, shared I2S clock → 16 kHz duplex; speaker on the MX1.25 connector */
#define BOARD_I2S_PORT 0
#define BOARD_I2S_MCLK 42
#define BOARD_I2S_BCLK 9
#define BOARD_I2S_WS 45
#define BOARD_I2S_DOUT 8
#define BOARD_I2S_DIN 10
#define BOARD_PA_EN 46
#define BOARD_MIC_CHANNEL_MASK 0x1
#define BOARD_MIC_GAIN_DB 30.0f

/* Buttons */
#define BOARD_BTN_TALK 0             /* BOOT, active low */

#endif /* BOARD_H */
