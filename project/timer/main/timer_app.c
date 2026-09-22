// 独立计时器:番茄钟 / 倒计时 / 秒表
// 硬件:ESP32-C6-LCD-1.47(ST7789 172x320 + RGB LED + BOOT 键)
// 交互:仅板载 BOOT(GPIO9) 单击/双击/长按;提示用 RGB 灯,无蜂鸣器
// 插电即运行,不依赖网络/主机
#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "driver/rmt_tx.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "font6x9.h"

// ---------------- 引脚 ----------------
#define PIN_MOSI     6
#define PIN_SCLK     7
#define PIN_MISO     5
#define PIN_LCD_CS   14
#define PIN_LCD_DC   15
#define PIN_LCD_RST  21
#define PIN_LCD_BL   22
#define PIN_RGB      8
#define PIN_BTN      9
#define LCD_H        172
#define LCD_V        320

// 颜色 RGB565
#define C_BLACK   0x0000
#define C_WHITE   0xFFFF
#define C_GRAY    0x8410
#define C_CYAN    0x07FF
#define C_GREEN   0x07E0
#define C_RED     0xF800
#define C_YELLOW  0xFE00
#define C_ORANGE  0xFD20
#define C_BLUE    0x001F
#define C_PURPLE  0x780F
#define C_DARK    0x1082

static const char *TAG = "timer";

// ---------------- 参数 ----------------
#define FOCUS_S   (25 * 60)
#define SHORT_S   (5  * 60)
#define LONG_S    (15 * 60)
static const int CD_PRESETS[] = {1, 3, 5, 10, 15, 25, 45, 60};
#define CD_NPRESET (sizeof(CD_PRESETS)/sizeof(CD_PRESETS[0]))

typedef enum { M_POMO = 0, M_CD, M_SW, M_NUM } Mode;
typedef enum { S_IDLE = 0, S_RUN, S_PAUSE, S_DONE } RState;
typedef enum { PH_FOCUS = 0, PH_SHORT, PH_LONG } Phase;

static Mode   s_mode   = M_POMO;
static RState s_state  = S_IDLE;
static Phase  s_phase  = PH_FOCUS;
static int    s_cycle  = 1;
static int64_t s_elapsed_us = 0;
static int64_t s_last_tick  = 0;
static int    s_cd_idx = 3;            // 默认 10 分钟
static bool   s_adjusting = false;

// ---------------- LCD ----------------
static esp_lcd_panel_handle_t s_panel;
static esp_lcd_panel_io_handle_t s_io;
static SemaphoreHandle_t s_sem;
static bool s_ready;

static bool lcd_done_cb(esp_lcd_panel_io_handle_t io, esp_lcd_panel_io_event_data_t *e, void *ctx)
{
    (void)io; (void)e; (void)ctx;
    BaseType_t hp = pdFALSE;
    if (s_sem) xSemaphoreGiveFromISR(s_sem, &hp);
    return hp == pdTRUE;
}
static void lcd_wait(void) { if (s_sem) xSemaphoreTake(s_sem, portMAX_DELAY); }

static void fill_rect(int x, int y, int w, int h, uint16_t color)
{
    if (!s_ready) return;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > LCD_H) w = LCD_H - x;
    if (y + h > LCD_V) h = LCD_V - y;
    if (w <= 0 || h <= 0) return;
    static uint16_t b[LCD_H * 16];
    int cap = 16;
    for (int i = 0; i < w * cap; i++) b[i] = color;
    for (int yy = 0; yy < h; yy += cap) {
        int rh = (yy + cap <= h) ? cap : (h - yy);
        esp_lcd_panel_draw_bitmap(s_panel, x, y + yy, x + w, y + yy + rh, b);
        lcd_wait();
    }
}

static void draw_text(int x0, int y0, const char *s, uint16_t fg, uint16_t bg, int scale)
{
    if (!s_ready) return;
    int n = (int)strlen(s);
    int cw = FONT_W * scale, ch = FONT_H * scale;
    int w = n * cw;
    if (x0 + w > LCD_H) w = LCD_H - x0;
    if (w <= 0 || y0 < 0 || y0 + ch > LCD_V) return;
    static uint16_t buf[LCD_H * 40];
    for (int i = 0; i < w * ch; i++) buf[i] = bg;
    for (int ci = 0; ci < n; ci++) {
        unsigned char c = (unsigned char)s[ci];
        if (c < 32 || c > 126) c = '?';
        const uint8_t *gly = font6x9[c - 32];
        for (int r = 0; r < FONT_H; r++) {
            uint8_t bits = gly[r];
            if (!bits) continue;
            for (int col = 0; col < FONT_W; col++) {
                if (!(bits & (1 << (FONT_W - 1 - col)))) continue;
                int px = ci * cw + col * scale, py = r * scale;
                for (int dy = 0; dy < scale; dy++)
                    for (int dx = 0; dx < scale; dx++) {
                        int X = px + dx, Y = py + dy;
                        if (X < w && Y < ch) buf[Y * w + X] = fg;
                    }
            }
        }
    }
    esp_lcd_panel_draw_bitmap(s_panel, x0, y0, x0 + w, y0 + ch, buf);
    lcd_wait();
}

static void draw_text_c(int y0, const char *s, uint16_t fg, uint16_t bg, int scale)
{
    int w = (int)strlen(s) * FONT_W * scale;
    int x = (LCD_H - w) / 2;
    if (x < 0) x = 0;
    draw_text(x, y0, s, fg, bg, scale);
}

static void lcd_init(void)
{
    spi_bus_config_t bus = {
        .mosi_io_num = PIN_MOSI, .miso_io_num = PIN_MISO, .sclk_io_num = PIN_SCLK,
        .quadwp_io_num = -1, .quadhd_io_num = -1,
        .max_transfer_sz = LCD_H * 64 * sizeof(uint16_t),
    };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO));
    esp_lcd_panel_io_spi_config_t iocfg = {
        .cs_gpio_num = PIN_LCD_CS, .dc_gpio_num = PIN_LCD_DC, .spi_mode = 0,
        .pclk_hz = 40 * 1000 * 1000, .trans_queue_depth = 10,
        .lcd_cmd_bits = 8, .lcd_param_bits = 8,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI2_HOST, &iocfg, &s_io));
    s_sem = xSemaphoreCreateBinary();
    esp_lcd_panel_io_callbacks_t cbs = { .on_color_trans_done = lcd_done_cb };
    esp_lcd_panel_io_register_event_callbacks(s_io, &cbs, NULL);
    esp_lcd_panel_dev_config_t pcfg = {
        .reset_gpio_num = PIN_LCD_RST, .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(s_io, &pcfg, &s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(s_panel));
    esp_lcd_panel_invert_color(s_panel, true);
    esp_lcd_panel_set_gap(s_panel, 34, 0);
    esp_lcd_panel_disp_on_off(s_panel, true);
    gpio_config_t bl = { .pin_bit_mask = 1ULL << PIN_LCD_BL, .mode = GPIO_MODE_OUTPUT };
    gpio_config(&bl);
    gpio_set_level(PIN_LCD_BL, 1);
    s_ready = true;
}

// ---------------- RGB (WS2812 @ GPIO8) ----------------
static rmt_channel_handle_t s_rgb;
static rmt_encoder_handle_t s_rgb_enc;

static void rgb_init(void)
{
    rmt_tx_channel_config_t txc = {
        .clk_src = RMT_CLK_SRC_DEFAULT, .gpio_num = PIN_RGB, .mem_block_symbols = 64,
        .resolution_hz = 10 * 1000 * 1000, .trans_queue_depth = 4,
    };
    if (rmt_new_tx_channel(&txc, &s_rgb) != ESP_OK) return;
    rmt_bytes_encoder_config_t bec = {
        .bit0 = { .level0 = 1, .duration0 = 4, .level1 = 0, .duration1 = 8 },
        .bit1 = { .level0 = 1, .duration0 = 8, .level1 = 0, .duration1 = 4 },
        .flags.msb_first = 1,
    };
    rmt_new_bytes_encoder(&bec, &s_rgb_enc);
    rmt_enable(s_rgb);
}

static void rgb_set(uint8_t r, uint8_t g, uint8_t b)
{
    if (!s_rgb) return;
    uint8_t d[3] = { g, r, b };   // WS2812 是 GRB
    rmt_transmit_config_t tc = { .loop_count = 0 };
    rmt_transmit(s_rgb, s_rgb_enc, d, sizeof(d), &tc);
    rmt_tx_wait_all_done(s_rgb, 100);
}

// ---------------- NVS ----------------
static void nvs_load(void)
{
    nvs_handle_t h;
    if (nvs_open("timer", NVS_READONLY, &h) == ESP_OK) {
        int32_t v = 3;
        if (nvs_get_i32(h, "cd_idx", &v) == ESP_OK && v >= 0 && v < (int)CD_NPRESET) s_cd_idx = v;
        nvs_close(h);
    }
}
static void nvs_save_cd(void)
{
    nvs_handle_t h;
    if (nvs_open("timer", NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_i32(h, "cd_idx", s_cd_idx);
        nvs_commit(h);
        nvs_close(h);
    }
}

// ---------------- 按键手势 ----------------
enum { EV_NONE = 0, EV_SINGLE, EV_DOUBLE, EV_LONG };
static bool s_btn_down;
static int64_t s_press_us;
static int s_clicks;
static int64_t s_last_click_us;

static int btn_poll(void)
{
    int lvl = gpio_get_level(PIN_BTN);       // 按下 = 0
    int64_t now = esp_timer_get_time();
    if (lvl == 0 && !s_btn_down) {
        s_btn_down = true;
        s_press_us = now;
    } else if (lvl == 1 && s_btn_down) {
        s_btn_down = false;
        int64_t d = now - s_press_us;
        if (d >= 1200000) { s_clicks = 0; return EV_LONG; }   // 长按 1.2s
        s_clicks++;
        s_last_click_us = now;
    }
    if (s_clicks > 0 && (now - s_last_click_us) > 320000) {   // 双击窗口 320ms
        int n = s_clicks;
        s_clicks = 0;
        return (n == 1) ? EV_SINGLE : EV_DOUBLE;
    }
    return EV_NONE;
}

// ---------------- 计时逻辑 ----------------
static int phase_seconds(void)
{
    switch (s_phase) {
        case PH_FOCUS: return FOCUS_S;
        case PH_SHORT: return SHORT_S;
        default:       return LONG_S;
    }
}
static int64_t total_us(void)
{
    if (s_mode == M_POMO) return (int64_t)phase_seconds() * 1000000;
    if (s_mode == M_CD)   return (int64_t)CD_PRESETS[s_cd_idx] * 60 * 1000000;
    return 0;
}
static const char *mode_name(void)
{
    return s_mode == M_POMO ? "POMODORO" : (s_mode == M_CD ? "COUNTDOWN" : "STOPWATCH");
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
    if (s_mode == M_POMO) { s_phase = PH_FOCUS; s_cycle = 1; }
}

static void on_event(int ev)
{
    if (ev == EV_DOUBLE) {
        if (s_adjusting) return;
        s_mode = (Mode)((s_mode + 1) % M_NUM);
        reset_run();
        return;
    }
    if (ev == EV_LONG) {
        if (s_adjusting) { s_adjusting = false; nvs_save_cd(); return; }
        if (s_mode == M_CD && s_state == S_IDLE && s_elapsed_us == 0) { s_adjusting = true; return; }
        reset_run();
        return;
    }
    if (ev == EV_SINGLE) {
        if (s_adjusting) { s_cd_idx = (s_cd_idx + 1) % (int)CD_NPRESET; return; }
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
}

static void tick_timer(void)
{
    int64_t now = esp_timer_get_time();
    if (s_state == S_RUN) s_elapsed_us += now - s_last_tick;
    s_last_tick = now;

    if ((s_mode == M_POMO || s_mode == M_CD) && s_state == S_RUN && s_elapsed_us >= total_us()) {
        s_elapsed_us = total_us();
        s_state = S_DONE;
        if (s_mode == M_POMO) {                       // 自动切换到下一阶段并待启动
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
    int m = t / 60, s = t % 60;
    snprintf(out, n, "%02d:%02d", m, s);
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
    if (s_adjusting) return "SET MINUTES";
    switch (s_state) {
        case S_IDLE:  return "READY";
        case S_RUN:   return "RUN";
        case S_PAUSE: return "PAUSE";
        default:      return "DONE";
    }
}

// ---------------- 渲染 ----------------
static int s_last_mode = -1;
static char s_last_time[16] = "\xff";
static char s_last_status[20] = "\xff";
static char s_last_info[24] = "\xff";
static float s_last_frac = -2;
static int s_last_cycle = -1, s_last_phase = -1;

#define C_FRAME  0x2965          // 细边框 / 分隔线(暗灰蓝)
#define MARGIN   12
#define Y_TITLE  14
#define Y_SEP    36
#define Y_TIME   70
#define Y_BAR    140
#define BAR_H    12
#define Y_STATUS 170
#define Y_INFO   198
#define Y_DOTS   230
#define Y_HINT   286

static void hline(int x, int y, int w, uint16_t c) { fill_rect(x, y, w, 1, c); }
static void frame(int x, int y, int w, int h, uint16_t c)
{
    fill_rect(x, y, w, 1, c);
    fill_rect(x, y + h - 1, w, 1, c);
    fill_rect(x, y, 1, h, c);
    fill_rect(x + w - 1, y, 1, h, c);
}

static void draw_dots(void)
{
    const int n = 4, sz = 12, gap = 12;
    int total = n * sz + (n - 1) * gap;
    int x = (LCD_H - total) / 2;
    for (int i = 0; i < n; i++) {
        uint16_t c;
        if (i < s_cycle - 1)        c = C_GREEN;        // 已完成轮次
        else if (i == s_cycle - 1)  c = mode_color();   // 当前轮次
        else                        c = C_DARK;
        fill_rect(x + i * (sz + gap), Y_DOTS, sz, sz, c);
    }
}

static void draw_static(void)
{
    fill_rect(0, 0, LCD_H, LCD_V, C_BLACK);
    frame(0, 0, LCD_H, LCD_V, C_FRAME);                   // 外框:可判断四周是否被裁切
    draw_text_c(Y_TITLE, mode_name(), C_CYAN, C_BLACK, 2);
    hline(MARGIN + 6, Y_SEP, LCD_H - 2 * (MARGIN + 6), C_FRAME);
    fill_rect(0, Y_HINT - 8, LCD_H, LCD_V - (Y_HINT - 8), C_BLACK);
    if (s_mode == M_SW) {
        draw_text_c(Y_HINT,      "1x  start / pause", C_GRAY, C_BLACK, 1);
        draw_text_c(Y_HINT + 14, "2x  mode    hold  reset", C_GRAY, C_BLACK, 1);
    } else if (s_mode == M_CD) {
        draw_text_c(Y_HINT,      "1x  start / pause", C_GRAY, C_BLACK, 1);
        draw_text_c(Y_HINT + 14, "2x mode   hold: edit / reset", C_GRAY, C_BLACK, 1);
    } else {
        draw_text_c(Y_HINT,      "1x  start / pause", C_GRAY, C_BLACK, 1);
        draw_text_c(Y_HINT + 14, "2x mode    hold: reset", C_GRAY, C_BLACK, 1);
    }
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

    // 大号时间(番茄钟/倒计时用 scale5;秒表含十分位用 scale4)
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
        fill_rect(0, Y_TIME, LCD_H, FONT_H * 5, C_BLACK);
        draw_text_c(Y_TIME, t, col, C_BLACK, tscale);
        strcpy(s_last_time, t);
    }

    // 进度条(带边框)
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
        const int bx = MARGIN + 6, bw = LCD_H - 2 * (MARGIN + 6), bh = BAR_H;
        fill_rect(bx, Y_BAR, bw, bh, C_DARK);
        int fw = (int)((bw - 2) * frac);
        if (fw > 0) fill_rect(bx + 1, Y_BAR + 1, fw, bh - 2, mode_color());
        frame(bx, Y_BAR, bw, bh, C_FRAME);
        s_last_frac = frac;
    }

    // 状态
    const char *st = state_text();
    if (strcmp(st, s_last_status) != 0) {
        uint16_t c = C_GRAY;
        if (s_adjusting) c = C_PURPLE;
        else if (s_state == S_RUN) c = mode_color();
        else if (s_state == S_DONE) c = C_RED;
        else if (s_state == S_PAUSE) c = C_YELLOW;
        fill_rect(0, Y_STATUS, LCD_H, FONT_H * 2, C_BLACK);
        draw_text_c(Y_STATUS, st, c, C_BLACK, 2);
        strcpy(s_last_status, st);
    }

    // 信息行
    char info[24];
    if (s_adjusting) {
        snprintf(info, sizeof info, "PRESET %d MIN", CD_PRESETS[s_cd_idx]);
    } else if (s_mode == M_POMO) {
        const char *ph = s_phase == PH_FOCUS ? "FOCUS" : (s_phase == PH_SHORT ? "S-BREAK" : "L-BREAK");
        snprintf(info, sizeof info, "%s %d/4", ph, s_cycle);
    } else if (s_mode == M_CD) {
        snprintf(info, sizeof info, "PRESET %d MIN", CD_PRESETS[s_cd_idx]);
    } else {
        snprintf(info, sizeof info, "ELAPSED");
    }
    if (strcmp(info, s_last_info) != 0) {
        fill_rect(0, Y_INFO, LCD_H, FONT_H * 2, C_BLACK);
        draw_text_c(Y_INFO, info, C_GRAY, C_BLACK, 2);
        strcpy(s_last_info, info);
    }

    // 番茄钟轮次指示点
    if (s_mode == M_POMO && (s_cycle != s_last_cycle || s_phase != s_last_phase)) {
        draw_dots();
        s_last_cycle = s_cycle;
        s_last_phase = s_phase;
    }
}

// ---------------- RGB 指示 ----------------
static void rgb_update(void)
{
    static int64_t last_blink;
    static uint8_t r, g, b;
    uint8_t nr, ng, nb;
    if (s_state == S_DONE) {
        int64_t now = esp_timer_get_time();
        bool on = ((now / 250000) & 1);
        nr = on ? 60 : 0; ng = 0; nb = 0;
    } else if (s_adjusting) {
        nr = 30; ng = 0; nb = 40;
    } else if (s_state == S_RUN) {
        if (s_mode == M_POMO && s_phase == PH_FOCUS) { nr = 50; ng = 0; nb = 0; }
        else if (s_mode == M_POMO) { nr = 0; ng = 50; nb = 0; }
        else if (s_mode == M_CD) { nr = 0; ng = 40; nb = 40; }
        else { nr = 0; ng = 50; nb = 0; }
    } else if (s_state == S_PAUSE) {
        nr = 40; ng = 40; nb = 0;
    } else {
        nr = 0; ng = 0; nb = 16;      // IDLE 暗蓝
    }
    if (nr != r || ng != g || nb != b) {
        r = nr; g = ng; b = nb;
        rgb_set(r, g, b);
    }
    (void)last_blink;
}

// ---------------- main ----------------
void app_main(void)
{
    ESP_LOGI(TAG, "== standalone timer (pomodoro/countdown/stopwatch) ==");
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase(); nvs_flash_init();
    }
    nvs_load();
    lcd_init();
    rgb_init();
    gpio_config_t bc = { .pin_bit_mask = 1ULL << PIN_BTN, .mode = GPIO_MODE_INPUT,
                         .pull_up_en = GPIO_PULLUP_ENABLE, .pull_down_en = GPIO_PULLDOWN_DISABLE,
                         .intr_type = GPIO_INTR_DISABLE };
    gpio_config(&bc);
    s_last_tick = esp_timer_get_time();
    draw_static();
    render();

    while (1) {
        int ev = btn_poll();
        if (ev) {
            ESP_LOGI(TAG, "event=%d mode=%d state=%d", ev, s_mode, s_state);
            on_event(ev);
        }
        tick_timer();
        render();
        rgb_update();
        vTaskDelay(pdMS_TO_TICKS(25));
    }
}
