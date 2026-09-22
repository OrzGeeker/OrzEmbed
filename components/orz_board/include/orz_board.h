// OrzEmbed 板级定义(ESP32-C6-LCD-1.47, Waveshare)
// 所有工程共用这一份引脚/尺寸定义,改板只改这里。
#pragma once

// ---- LCD (ST7789, 172x320, SPI) ----
#define BOARD_LCD_MOSI   6
#define BOARD_LCD_SCLK   7
#define BOARD_LCD_MISO   5
#define BOARD_LCD_CS     14
#define BOARD_LCD_DC     15
#define BOARD_LCD_RST    21
#define BOARD_LCD_BL     22
#define BOARD_LCD_H      172
#define BOARD_LCD_V      320

// ---- microSD(SPI,与 LCD 共享 MOSI/SCLK) ----
#define BOARD_SD_CS      4
#define BOARD_SD_MISO    5

// ---- 板载外设 ----
#define BOARD_RGB_GPIO   8      // WS2812
#define BOARD_BTN_GPIO   9      // BOOT 按键(低有效)
