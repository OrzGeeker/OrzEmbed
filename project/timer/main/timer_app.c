// 独立计时器:番茄钟 / 倒计时 / 秒表
// 硬件:ESP32-C6-LCD-1.47(ST7789 172x320)
//   - 4 个开关:   GP0 / GP1 / GP2 / GP3   (对 GND,内部上拉)
//   - 4 个绿灯:   GP18 / GP19 / GP20 / GP23
//   - RGB 灯:     GP8 (WS2812)
// 交互(4 键):
//   K1(GP0) 短按 = 开始/暂停   长按 = 复位
//   K2(GP1) 短按 = 切换模式
//   K3(GP2) 短按 = -1 分钟
//   K4(GP3) 短按 = +1 分钟
// 提醒(4 灯):
//   就绪 = 慢速跑马灯   运行 = 剩余进度条(4→0)
//   暂停 = 进度条慢闪   结束 = 四灯快闪
// 依赖共享组件 orz_board / orz_lcd / orz_rgb / orz_input
// 插电即运行,不依赖网络/主机
#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "driver/gpio.h"
#include "orz_board.h"
#include "orz_lcd.h"
#include "orz_rgb.h"
#include "orz_input.h"

// ---------------- 应用引脚(外接) ----------------
static const int BTN_PINS[] = { 0, 1, 2, 3 };            // K1..K4 (GP0..GP3)
#define NBTN ((int)(sizeof(BTN_PINS) / sizeof(BTN_PINS[0])))
static const int LED_PINS[] = { 18, 19, 20, 23 };        // 4 个绿色 LED
#define NLED ((int)(sizeof(LED_PINS) / sizeof(LED_PINS[0])))
#define LED_ACTIVE_HIGH 1                                // 高电平点亮;低电平点亮改 0
#define RUN_LED_MARQUEE 0                                // 1=运行时跑马灯,0=运行时进度条

// 颜色 RGB565
#define C_BLACK   0x0000
#define C_WHITE   0xFFFF
#define C_GRAY    0x8410
#define C_CYAN    0x07FF
#define C_GREEN   0x07E0
#define C_RED     0xF800
#define C_YELLOW  0xFE00
#define C_PURPLE  0x780F
#define C_DARK    0x1082
#define C_FRAME   0x2965

static const char *TAG = "timer";

// ---------------- 参数 ----------------
#define FOCUS_DEF_MIN 25
#define SHORT_MIN     5
#define LBREAK_MIN    15
#define CD_DEF_MIN    10

typedef enum { M_POMO = 0, M_CD, M_SW, M_NUM } Mode;
typedef enum { S_IDLE = 0, S_RUN, S_PAUSE, S_DONE } RState;
typedef enum { PH_FOCUS = 0, PH_SHORT, PH_LONG } Phase;

static Mode   s_mode  = M_POMO;
static RState s_state = S_IDLE;
static Phase  s_phase = PH_FOCUS;
static int    s_cycle = 1;
static int64_t s_elapsed_us = 0;
static int64_t s_last_tick  = 0;
static int    s_focus_min  = FOCUS_DEF_MIN;
static int    s_cd_min     = CD_DEF_MIN;
static bool   s_adjusting  = false;
static int64_t s_adjust_at = 0;

// ---------------- 4 个 LED ----------------
static void led_init(void)
{
    uint64_t mask = 0;
    for (int i = 0; i < NLED; i++) mask |= (1ULL << LED_PINS[i]);
    gpio_config_t c = { .pin_bit_mask = mask, .mode = GPIO_MODE_OUTPUT, .intr_type = GPIO_INTR_DISABLE };
    gpio_config(&c);
    for (int i = 0; i < NLED; i++) gpio_set_level(LED_PINS[i], !LED_ACTIVE_HIGH);
}
static void led_bar(int lit)
{
    for (int i = 0; i < NLED; i++)
        gpio_set_level(LED_PINS[i], (i < lit) ? LED_ACTIVE_HIGH : !LED_ACTIVE_HIGH);
}
static void led_dot(int idx)
{
    for (int i = 0; i < NLED; i++)
        gpio_set_level(LED_PINS[i], (i == idx) ? LED_ACTIVE_HIGH : !LED_ACTIVE_HIGH);
}

static int64_t total_us(void);

static int led_level(void)
{
    if (s_mode == M_SW) {
        int sec = (int)((s_elapsed_us / 1000000) % 60);
        return (sec * NLED) / 60;                      // 秒表:每分钟填充
    }
    int64_t tot = total_us();
    if (tot <= 0) return 0;
    int64_t rem = tot - s_elapsed_us;
    if (rem < 0) rem = 0;
    int lit = (int)((rem * NLED + tot - 1) / tot);     // 向上取整
    if (lit < 0) lit = 0;
    if (lit > NLED) lit = NLED;
    return lit;
}

static void led_update(void)
{
    static int64_t last = 0;
    static int step = 0;
    static bool phase = false;
    int64_t now = esp_timer_get_time();
    int64_t iv = (s_state == S_DONE)  ? 125000 :
                 (s_state == S_PAUSE) ? 500000 :
                 (s_state == S_RUN)   ? 250000 : 400000;
    if (now - last < iv) return;
    last = now;
    phase = !phase;

#if RUN_LED_MARQUEE
    bool marquee = (s_state == S_RUN);
#else
    bool marquee = false;
#endif
    if (s_state == S_IDLE || s_adjusting || marquee) {
        step = (step + 1) % NLED;
        led_dot(step);
    } else if (s_state == S_RUN) {
        led_bar(led_level());
    } else if (s_state == S_PAUSE) {
        led_bar(phase ? led_level() : 0);
    } else {
        led_bar(phase ? NLED : 0);
    }
}

// ---------------- NVS ----------------
static void nvs_load(void)
{
    nvs_handle_t h;
    if (nvs_open("timer", NVS_READONLY, &h) == ESP_OK) {
        int32_t v;
        if (nvs_get_i32(h, "focus_min", &v) == ESP_OK && v >= 1 && v <= 180) s_focus_min = v;
        if (nvs_get_i32(h, "cd_min", &v) == ESP_OK && v >= 1 && v <= 180) s_cd_min = v;
        nvs_close(h);
    }
}
static void nvs_save(void)
{
    nvs_handle_t h;
    if (nvs_open("timer", NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_i32(h, "focus_min", s_focus_min);
        nvs_set_i32(h, "cd_min", s_cd_min);
        nvs_commit(h);
        nvs_close(h);
    }
}

// ---------------- 计时逻辑 ----------------
static int phase_seconds(void)
{
    switch (s_phase) {
        case PH_FOCUS: return s_focus_min * 60;
        case PH_SHORT: return SHORT_MIN * 60;
        default:       return LBREAK_MIN * 60;
    }
}
static int64_t total_us(void)
{
    if (s_mode == M_POMO) return (int64_t)phase_seconds() * 1000000;
    if (s_mode == M_CD)   return (int64_t)s_cd_min * 60 * 1000000;
    return 0;
}
static const char *mode_name(void)
{
    return s_mode == M_POMO ? "番茄钟" : (s_mode == M_CD ? "倒计时" : "秒表");
}
static uint16_t mode_color(void)
{
    if (s_mode == M_POMO) return (s_phase == PH_FOCUS) ? C_RED : C_GREEN;
    if (s_mode == M_CD)   return C_CYAN;
    return C_GREEN;
}
static void reset_run(void)
{
    s_state = S_IDLE;
    s_elapsed_us = 0;
    s_adjusting = false;
    if (s_mode == M_POMO) { s_phase = PH_FOCUS; s_cycle = 1; }
}

static void toggle_start_pause(void)
{
    switch (s_state) {
        case S_IDLE:  s_state = S_RUN; break;
        case S_RUN:   s_state = S_PAUSE; break;
        case S_PAUSE: s_state = S_RUN; break;
        case S_DONE:
            if (s_mode == M_CD) { s_state = S_IDLE; s_elapsed_us = 0; }
            else                { s_state = S_RUN;  s_elapsed_us = 0; }
            break;
    }
}
static void next_mode(void)
{
    s_mode = (Mode)((s_mode + 1) % M_NUM);
    reset_run();
}
static void adjust_minutes(int d)
{
    if (s_mode == M_CD) {
        s_cd_min += d;
        if (s_cd_min < 1) s_cd_min = 1;
        if (s_cd_min > 180) s_cd_min = 180;
        if (s_state == S_IDLE) s_elapsed_us = 0;
        nvs_save();
        s_adjusting = true;
        s_adjust_at = esp_timer_get_time();
    } else if (s_mode == M_POMO && s_phase == PH_FOCUS) {
        s_focus_min += d;
        if (s_focus_min < 1) s_focus_min = 1;
        if (s_focus_min > 180) s_focus_min = 180;
        if (s_state == S_IDLE) s_elapsed_us = 0;
        nvs_save();
        s_adjusting = true;
        s_adjust_at = esp_timer_get_time();
    }
}

// 按键回调(由 orz_input 调用)
static void on_button(int idx, bool is_long)
{
    ESP_LOGI(TAG, "K%d %s", idx + 1, is_long ? "LONG" : "short");
    switch (idx) {
        case 0:
            if (is_long) reset_run();
            else toggle_start_pause();
            break;
        case 1:
            if (!is_long) next_mode();
            break;
        case 2:
            if (!is_long) adjust_minutes(-1);
            break;
        case 3:
            if (!is_long) adjust_minutes(+1);
            break;
    }
}

static void tick_timer(void)
{
    int64_t now = esp_timer_get_time();
    if (s_state == S_RUN) s_elapsed_us += now - s_last_tick;
    s_last_tick = now;

    if ((s_mode == M_POMO || s_mode == M_CD) && s_state == S_RUN && s_elapsed_us >= total_us()) {
        s_elapsed_us = total_us();
        s_state = S_DONE;
        if (s_mode == M_POMO) {
            if (s_phase == PH_FOCUS) s_phase = (s_cycle % 4 == 0) ? PH_LONG : PH_SHORT;
            else { s_phase = PH_FOCUS; s_cycle++; }
            s_elapsed_us = 0;
        }
    }
}

static void fmt_clock(int64_t ms, char *out, size_t n)
{
    if (ms < 0) ms = 0;
    int t = (int)(ms / 1000);
    snprintf(out, n, "%02d:%02d", t / 60, t % 60);
}
static void fmt_clock_t(int64_t ms, char *out, size_t n)
{
    if (ms < 0) ms = 0;
    int t = (int)(ms / 1000);
    int tenth = (int)((ms % 1000) / 100);
    int m = t / 60, s = t % 60;
    if (m > 99) { m = 99; s = 59; tenth = 9; }
    snprintf(out, n, "%02d:%02d.%d", m, s, tenth);
}
static const char *state_text(void)
{
    if (s_adjusting && s_state == S_IDLE) return "调整";
    switch (s_state) {
        case S_IDLE:  return "就绪";
        case S_RUN:   return "运行";
        case S_PAUSE: return "暂停";
        default:      return "结束";
    }
}

// ---------------- 渲染 ----------------
static int s_last_mode = -1;
static char s_last_time[16] = "\xff";
static char s_last_status[20] = "\xff";
static char s_last_info[24] = "\xff";
static float s_last_frac = -2;
static int s_last_cycle = -1, s_last_phase = -1;

#define MARGIN   12
#define Y_TITLE  14
#define Y_SEP    52
#define Y_TIME   72
#define Y_BAR    140
#define BAR_H    12
#define Y_STATUS 164
#define Y_INFO   204
#define Y_DOTS   228
#define Y_HINT   264

static void draw_dots(void)
{
    const int n = 4, sz = 12, gap = 12;
    int total = n * sz + (n - 1) * gap;
    int x = (ORZ_LCD_H - total) / 2;
    for (int i = 0; i < n; i++) {
        uint16_t c;
        if (i < s_cycle - 1)       c = C_GREEN;
        else if (i == s_cycle - 1) c = mode_color();
        else                       c = C_DARK;
        orz_lcd_fill(x + i * (sz + gap), Y_DOTS, sz, sz, c);
    }
}

static void draw_static(void)
{
    orz_lcd_fill(0, 0, ORZ_LCD_H, ORZ_LCD_V, C_BLACK);
    orz_lcd_frame(0, 0, ORZ_LCD_H, ORZ_LCD_V, C_FRAME);
    orz_lcd_mixed_c(Y_TITLE, mode_name(), C_CYAN, C_BLACK, 2);
    orz_lcd_hline(MARGIN + 6, Y_SEP, ORZ_LCD_H - 2 * (MARGIN + 6), C_FRAME);
    orz_lcd_fill(0, Y_HINT - 6, ORZ_LCD_H, ORZ_LCD_V - (Y_HINT - 6), C_BLACK);
    orz_lcd_mixed_c(Y_HINT,      "K1 开始/暂停", C_GRAY, C_BLACK, 1);
    orz_lcd_mixed_c(Y_HINT + 16, "K2 模式 K3 减 K4 加", C_GRAY, C_BLACK, 1);
    orz_lcd_mixed_c(Y_HINT + 32, "长按 K1 复位", C_GRAY, C_BLACK, 1);
}

static void render(void)
{
    if (s_mode != s_last_mode) {
        draw_static();
        s_last_mode = s_mode;
        strcpy(s_last_time, "\xff");
        strcpy(s_last_status, "\xff");
        strcpy(s_last_info, "\xff");
        s_last_frac = -2;
        s_last_cycle = -1;
        s_last_phase = -1;
    }

    char t[16];
    int tscale = (s_mode == M_SW) ? 4 : 5;
    if (s_mode == M_SW) {
        fmt_clock_t(s_elapsed_us / 1000, t, sizeof t);
    } else {
        int64_t remain_ms = (total_us() - s_elapsed_us) / 1000;
        if (remain_ms < 0) remain_ms = 0;
        fmt_clock(remain_ms, t, sizeof t);
    }
    if (strcmp(t, s_last_time) != 0) {
        uint16_t col = (s_state == S_DONE) ? C_RED : C_WHITE;
        orz_lcd_fill(0, Y_TIME, ORZ_LCD_H, 9 * 5, C_BLACK);
        orz_lcd_text_c(Y_TIME, t, col, C_BLACK, tscale);
        strcpy(s_last_time, t);
    }

    float frac;
    if (s_mode == M_SW) {
        frac = (float)((s_elapsed_us / 1000000) % 60) / 60.0f;
    } else {
        int64_t tot = total_us();
        frac = tot > 0 ? (float)((double)s_elapsed_us / (double)tot) : 0.0f;
        if (frac < 0) frac = 0;
        if (frac > 1) frac = 1;
    }
    if (frac < 0.002f && s_last_frac > 0.002f) frac = 0;
    if ((frac - s_last_frac > 0.004f) || (s_last_frac - frac > 0.02f) || s_last_frac < 0) {
        const int bx = MARGIN + 6, bw = ORZ_LCD_H - 2 * (MARGIN + 6), bh = BAR_H;
        orz_lcd_fill(bx, Y_BAR, bw, bh, C_DARK);
        int fw = (int)((bw - 2) * frac);
        if (fw > 0) orz_lcd_fill(bx + 1, Y_BAR + 1, fw, bh - 2, mode_color());
        orz_lcd_frame(bx, Y_BAR, bw, bh, C_FRAME);
        s_last_frac = frac;
    }

    const char *st = state_text();
    if (strcmp(st, s_last_status) != 0) {
        uint16_t c = C_GRAY;
        if (s_adjusting && s_state == S_IDLE) c = C_PURPLE;
        else if (s_state == S_RUN) c = mode_color();
        else if (s_state == S_DONE) c = C_RED;
        else if (s_state == S_PAUSE) c = C_YELLOW;
        orz_lcd_fill(0, Y_STATUS, ORZ_LCD_H, 16 * 2, C_BLACK);
        orz_lcd_mixed_c(Y_STATUS, st, c, C_BLACK, 2);
        strcpy(s_last_status, st);
    }

    char info[24];
    if (s_mode == M_POMO) {
        const char *ph = s_phase == PH_FOCUS ? "专注" : (s_phase == PH_SHORT ? "短休" : "长休");
        snprintf(info, sizeof info, "%s %d/4", ph, s_cycle);
    } else if (s_mode == M_CD) {
        snprintf(info, sizeof info, "%d 分", s_cd_min);
    } else {
        snprintf(info, sizeof info, "计时");
    }
    if (strcmp(info, s_last_info) != 0) {
        orz_lcd_fill(0, Y_INFO, ORZ_LCD_H, 16, C_BLACK);
        orz_lcd_mixed_c(Y_INFO, info, C_GRAY, C_BLACK, 1);
        strcpy(s_last_info, info);
    }

    if (s_mode == M_POMO && (s_cycle != s_last_cycle || s_phase != s_last_phase)) {
        draw_dots();
        s_last_cycle = s_cycle;
        s_last_phase = s_phase;
    }
}

// ---------------- RGB 指示 ----------------
static void rgb_update(void)
{
    static uint8_t r, g, b;
    uint8_t nr, ng, nb;
    if (s_state == S_DONE) {
        bool on = ((esp_timer_get_time() / 250000) & 1);
        nr = on ? 60 : 0; ng = 0; nb = 0;
    } else if (s_state == S_RUN) {
        if (s_mode == M_POMO && s_phase == PH_FOCUS) { nr = 50; ng = 0; nb = 0; }
        else if (s_mode == M_POMO) { nr = 0; ng = 50; nb = 0; }
        else if (s_mode == M_CD) { nr = 0; ng = 40; nb = 40; }
        else { nr = 0; ng = 50; nb = 0; }
    } else if (s_state == S_PAUSE) {
        nr = 40; ng = 40; nb = 0;
    } else if (s_adjusting) {
        nr = 30; ng = 0; nb = 40;
    } else {
        nr = 0; ng = 0; nb = 16;
    }
    if (nr != r || ng != g || nb != b) {
        r = nr; g = ng; b = nb;
        orz_rgb_set(r, g, b);
    }
}

// ---------------- main ----------------
void app_main(void)
{
    ESP_LOGI(TAG, "== standalone timer (pomodoro/countdown/stopwatch) ==");
    ESP_LOGI(TAG, "buttons K1-K4 = GP%d/%d/%d/%d; LEDs = GP%d/%d/%d/%d",
             BTN_PINS[0], BTN_PINS[1], BTN_PINS[2], BTN_PINS[3],
             LED_PINS[0], LED_PINS[1], LED_PINS[2], LED_PINS[3]);

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase(); nvs_flash_init();
    }
    nvs_load();

    if (orz_lcd_init() != ESP_OK) ESP_LOGE(TAG, "LCD init failed");
    if (orz_rgb_init(BOARD_RGB_GPIO) != ESP_OK) ESP_LOGE(TAG, "RGB init failed");
    led_init();
    orz_input_init(BTN_PINS, NBTN, 800, on_button);

    s_last_tick = esp_timer_get_time();
    draw_static();
    render();

    while (1) {
        orz_input_poll();
        tick_timer();
        if (s_adjusting && s_adjust_at && esp_timer_get_time() - s_adjust_at > 1500000) {
            s_adjusting = false;
            s_adjust_at = 0;
        }
        render();
        rgb_update();
        led_update();
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
