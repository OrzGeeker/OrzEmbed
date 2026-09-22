// 板载 LCD(ST7789 172x320)驱动 + 绘制 helper + 字库
// 引脚来自 orz_board.h;支持纯 ASCII 与中英混排(内置 6x9 / 16x16 点阵)
#pragma once

#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ORZ_LCD_H 172
#define ORZ_LCD_V 320

// 初始化(SPI2 + ST7789 + 背光)。失败返回非 ESP_OK
esp_err_t orz_lcd_init(void);

// 基本图元
void orz_lcd_fill(int x, int y, int w, int h, uint16_t color);
void orz_lcd_frame(int x, int y, int w, int h, uint16_t color);   // 1px 矩形边框
void orz_lcd_hline(int x, int y, int w, uint16_t color);

// 文本:纯 ASCII(6x9)。x 为左上角,y 为顶部;(x,y) 超出则裁剪
void orz_lcd_text(int x, int y, const char *s, uint16_t fg, uint16_t bg, int scale);
void orz_lcd_text_c(int y, const char *s, uint16_t fg, uint16_t bg, int scale);   // 水平居中

// 文本:中英混排(汉字 16x16 + ASCII 6x9),同一行垂直居中
int  orz_lcd_mixed_width(const char *s, int scale);
void orz_lcd_mixed(int x, int y, const char *s, uint16_t fg, uint16_t bg, int scale);
void orz_lcd_mixed_c(int y, const char *s, uint16_t fg, uint16_t bg, int scale); // 水平居中

#ifdef __cplusplus
}
#endif
