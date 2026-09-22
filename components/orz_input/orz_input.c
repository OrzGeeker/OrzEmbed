// 多按键扫描引擎:内部上拉(按下接 GND),短按 / 长按
#include <string.h>
#include "orz_input.h"
#include "driver/gpio.h"
#include "esp_timer.h"

#define ORZ_INPUT_MAX 8

static int      s_pins[ORZ_INPUT_MAX];
static int      s_count;
static uint32_t s_long_ms = 800;
static orz_input_cb_t s_cb;
static bool     s_down[ORZ_INPUT_MAX];
static int64_t  s_press_us[ORZ_INPUT_MAX];

esp_err_t orz_input_init(const int *pins, int count, uint32_t long_ms, orz_input_cb_t cb)
{
    if (!pins || count <= 0 || count > ORZ_INPUT_MAX) return ESP_ERR_INVALID_ARG;
    s_count = count;
    s_long_ms = long_ms ? long_ms : 800;
    s_cb = cb;
    memset(s_down, 0, sizeof(s_down));
    uint64_t mask = 0;
    for (int i = 0; i < count; i++) {
        s_pins[i] = pins[i];
        mask |= (1ULL << pins[i]);
    }
    gpio_config_t cfg = {
        .pin_bit_mask = mask,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    return gpio_config(&cfg);
}

void orz_input_poll(void)
{
    int64_t now = esp_timer_get_time();
    for (int i = 0; i < s_count; i++) {
        int lvl = gpio_get_level(s_pins[i]);           // 按下 = 0
        if (lvl == 0 && !s_down[i]) {
            s_down[i] = true;
            s_press_us[i] = now;
        } else if (lvl == 1 && s_down[i]) {
            s_down[i] = false;
            int64_t d = now - s_press_us[i];
            if (!s_cb) continue;
            if (d >= (int64_t)s_long_ms * 1000) s_cb(i, true);        // 长按
            else if (d >= 30000)               s_cb(i, false);       // 去抖 30ms
        }
    }
}
