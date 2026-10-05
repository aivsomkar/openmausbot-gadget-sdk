/* SPDX-License-Identifier: Apache-2.0 */
/* Waveshare ESP32-S3-LCD-1.54. Pins from the Waveshare schematic for this
 * board. 16 MB flash, 8 MB octal PSRAM, no touch. The board has a power
 * latch: BAT_EN must be driven high at once or it switches off on battery
 * when PWR is released (spec §5.3). */
#ifndef BOARD_H
#define BOARD_H

/* Power and battery */
#define BOARD_BAT_EN 2               /* latch, drive high in board_early_init() */
#define BOARD_BAT_ADC 1              /* ADC1 channel 0, behind a 200 k / 100 k divider (x3) */
#define BOARD_BAT_DIVIDER_X1000 3000
#define BOARD_CHG_STAT 3             /* low while charging */

/* Display: ST7789 240x240 on SPI2, mode 3, colours inverted (IPS) */
#define BOARD_LCD_SCLK 38
#define BOARD_LCD_MOSI 39
#define BOARD_LCD_CS 21
#define BOARD_LCD_DC 45
#define BOARD_LCD_RST 40
#define BOARD_LCD_BL 46              /* LEDC PWM, active high */
#define BOARD_LCD_SPI_MODE 3
#define BOARD_LCD_PCLK_HZ 40000000
#define BOARD_LCD_X_GAP 0
#define BOARD_LCD_Y_GAP 0            /* set to 80 if the picture shows shifted (checklist) */
#define BOARD_LCD_BUF_LINES 40

/* I2C: ES8311 0x18, ES7210 0x40, QMI8658 0x6B */
#define BOARD_I2C_PORT 0
#define BOARD_I2C_SDA 42
#define BOARD_I2C_SCL 41

/* Audio: ES8311 + ES7210, shared I2S clock → 16 kHz duplex; onboard speaker */
#define BOARD_I2S_PORT 0
#define BOARD_I2S_MCLK 8
#define BOARD_I2S_BCLK 9
#define BOARD_I2S_WS 10
#define BOARD_I2S_DOUT 12            /* to ES8311 DSDIN */
#define BOARD_I2S_DIN 11             /* from ES7210 ASDOUT */
#define BOARD_PA_EN 7                /* NS4150B, active high */
#define BOARD_MIC_CHANNEL_MASK 0x1
#define BOARD_MIC_GAIN_DB 30.0f

/* Buttons, all active low: BOOT (KEY_MINUS) = TALK, PLUS = CANCEL. PWR
 * (GPIO5) switches the board on and is not an input in v1. */
#define BOARD_BTN_TALK 0
#define BOARD_BTN_CANCEL 4

#endif /* BOARD_H */
