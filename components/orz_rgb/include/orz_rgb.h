// 板载 WS2812 RGB 灯驱动(单灯)
#pragma once

#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// 初始化(指定 WS2812 数据脚,如 BOARD_RGB_GPIO)
esp_err_t orz_rgb_init(int gpio);
// 设置颜色(0..255)
void orz_rgb_set(uint8_t r, uint8_t g, uint8_t b);

#ifdef __cplusplus
}
#endif
