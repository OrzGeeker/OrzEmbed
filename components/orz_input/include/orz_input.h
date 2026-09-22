// 多按键扫描引擎:内部上拉(按下接 GND),支持短按 / 长按回调
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// index = 按键序号(0..count-1);is_long = true 表示长按
typedef void (*orz_input_cb_t)(int index, bool is_long);

// pins: GPIO 数组(按下 = 低);long_ms: 长按阈值(毫秒)
esp_err_t orz_input_init(const int *pins, int count, uint32_t long_ms, orz_input_cb_t cb);
// 需在主循环中周期调用(建议 10~20ms 一次)
void orz_input_poll(void);

#ifdef __cplusplus
}
#endif
