// 板载 LCD(ST7789 172x320)驱动 + 绘制 + 字库实现
#include <string.h>
#include "orz_lcd.h"
#include "orz_board.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "font6x9.h"
#include "cjk16.h"

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

esp_err_t orz_lcd_init(void)
{
    spi_bus_config_t bus = {
        .mosi_io_num = BOARD_LCD_MOSI, .miso_io_num = BOARD_LCD_MISO, .sclk_io_num = BOARD_LCD_SCLK,
        .quadwp_io_num = -1, .quadhd_io_num = -1,
        .max_transfer_sz = ORZ_LCD_H * 64 * sizeof(uint16_t),
    };
    esp_err_t err = spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO);
    if (err != ESP_OK) return err;

    esp_lcd_panel_io_spi_config_t iocfg = {
        .cs_gpio_num = BOARD_LCD_CS, .dc_gpio_num = BOARD_LCD_DC, .spi_mode = 0,
        .pclk_hz = 40 * 1000 * 1000, .trans_queue_depth = 10,
        .lcd_cmd_bits = 8, .lcd_param_bits = 8,
    };
    err = esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI2_HOST, &iocfg, &s_io);
    if (err != ESP_OK) return err;

    s_sem = xSemaphoreCreateBinary();
    esp_lcd_panel_io_callbacks_t cbs = { .on_color_trans_done = lcd_done_cb };
    esp_lcd_panel_io_register_event_callbacks(s_io, &cbs, NULL);

    esp_lcd_panel_dev_config_t pcfg = {
        .reset_gpio_num = BOARD_LCD_RST, .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
    };
    err = esp_lcd_new_panel_st7789(s_io, &pcfg, &s_panel);
    if (err != ESP_OK) return err;
    ESP_ERROR_CHECK(esp_lcd_panel_reset(s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(s_panel));
    esp_lcd_panel_invert_color(s_panel, true);
    esp_lcd_panel_set_gap(s_panel, 34, 0);      // 172 宽居中于 240 控制器
    esp_lcd_panel_disp_on_off(s_panel, true);

    gpio_config_t bl = { .pin_bit_mask = 1ULL << BOARD_LCD_BL, .mode = GPIO_MODE_OUTPUT };
    gpio_config(&bl);
    gpio_set_level(BOARD_LCD_BL, 1);
    s_ready = true;
    return ESP_OK;
}

void orz_lcd_fill(int x, int y, int w, int h, uint16_t color)
{
    if (!s_ready) return;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > ORZ_LCD_H) w = ORZ_LCD_H - x;
    if (y + h > ORZ_LCD_V) h = ORZ_LCD_V - y;
    if (w <= 0 || h <= 0) return;
    static uint16_t b[ORZ_LCD_H * 16];
    const int cap = 16;
    for (int i = 0; i < w * cap; i++) b[i] = color;
    for (int yy = 0; yy < h; yy += cap) {
        int rh = (yy + cap <= h) ? cap : (h - yy);
        esp_lcd_panel_draw_bitmap(s_panel, x, y + yy, x + w, y + yy + rh, b);
        lcd_wait();
    }
}

void orz_lcd_frame(int x, int y, int w, int h, uint16_t color)
{
    orz_lcd_fill(x, y, w, 1, color);
    orz_lcd_fill(x, y + h - 1, w, 1, color);
    orz_lcd_fill(x, y, 1, h, color);
    orz_lcd_fill(x + w - 1, y, 1, h, color);
}

void orz_lcd_hline(int x, int y, int w, uint16_t color)
{
    orz_lcd_fill(x, y, w, 1, color);
}

void orz_lcd_text(int x0, int y0, const char *s, uint16_t fg, uint16_t bg, int scale)
{
    if (!s_ready) return;
    int n = (int)strlen(s);
    int cw = FONT_W * scale, ch = FONT_H * scale;
    int w = n * cw;
    if (x0 + w > ORZ_LCD_H) w = ORZ_LCD_H - x0;
    if (w <= 0 || y0 < 0 || y0 + ch > ORZ_LCD_V) return;
    static uint16_t buf[ORZ_LCD_H * 40];
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

void orz_lcd_text_c(int y0, const char *s, uint16_t fg, uint16_t bg, int scale)
{
    int w = (int)strlen(s) * FONT_W * scale;
    int x = (ORZ_LCD_H - w) / 2;
    if (x < 0) x = 0;
    orz_lcd_text(x, y0, s, fg, bg, scale);
}

// ---- 中英混排 ----
static int utf8_next(const char *s, uint32_t *cp)
{
    const unsigned char *p = (const unsigned char *)s;
    if (p[0] < 0x80) { *cp = p[0]; return 1; }
    if ((p[0] & 0xE0) == 0xC0) { *cp = ((p[0] & 0x1F) << 6) | (p[1] & 0x3F); return 2; }
    if ((p[0] & 0xF0) == 0xE0) { *cp = ((p[0] & 0x0F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F); return 3; }
    if ((p[0] & 0xF8) == 0xF0) {
        *cp = ((p[0] & 0x07) << 18) | ((p[1] & 0x3F) << 12) | ((p[2] & 0x3F) << 6) | (p[3] & 0x3F);
        return 4;
    }
    *cp = '?'; return 1;
}
static const cjk_glyph_t *cjk_lookup(uint32_t cp)
{
    for (int i = 0; i < CJK16_COUNT; i++) if (cjk16[i].cp == cp) return &cjk16[i];
    return NULL;
}

int orz_lcd_mixed_width(const char *s, int scale)
{
    int w = 0;
    while (*s) { uint32_t cp; int n = utf8_next(s, &cp); w += (cp < 0x80 ? FONT_W : CJK_W) * scale; s += n; }
    return w;
}

void orz_lcd_mixed(int x0, int y0, const char *s, uint16_t fg, uint16_t bg, int scale)
{
    if (!s_ready) return;
    int w = orz_lcd_mixed_width(s, scale);
    if (x0 + w > ORZ_LCD_H) w = ORZ_LCD_H - x0;
    int h = CJK_H * scale;
    if (w <= 0 || y0 < 0 || y0 + h > ORZ_LCD_V) return;
    static uint16_t buf[ORZ_LCD_H * CJK_H * 2];
    for (int i = 0; i < w * h; i++) buf[i] = bg;
    int pen = x0;
    const char *p = s;
    while (*p) {
        uint32_t cp; int nb = utf8_next(p, &cp);
        if (cp < 0x80) {
            const uint8_t *g = font6x9[(cp >= 32 && cp <= 126) ? (int)cp - 32 : 0];
            int oy = ((CJK_H - FONT_H) / 2) * scale;     // ASCII 在行内垂直居中
            for (int r = 0; r < FONT_H; r++) {
                uint8_t bits = g[r];
                if (!bits) continue;
                for (int col = 0; col < FONT_W; col++) {
                    if (!(bits & (1 << (FONT_W - 1 - col)))) continue;
                    for (int dy = 0; dy < scale; dy++)
                        for (int dx = 0; dx < scale; dx++) {
                            int X = pen + col * scale + dx - x0, Y = oy + r * scale + dy;
                            if (X >= 0 && X < w && Y >= 0 && Y < h) buf[Y * w + X] = fg;
                        }
                }
            }
            pen += FONT_W * scale;
        } else {
            const cjk_glyph_t *g = cjk_lookup(cp);
            for (int r = 0; r < CJK_H; r++) {
                uint16_t bits = g ? g->rows[r] : 0;
                if (!bits) continue;
                for (int col = 0; col < CJK_W; col++) {
                    if (!(bits & (1 << (CJK_W - 1 - col)))) continue;
                    for (int dy = 0; dy < scale; dy++)
                        for (int dx = 0; dx < scale; dx++) {
                            int X = pen + col * scale + dx - x0, Y = r * scale + dy;
                            if (X >= 0 && X < w && Y >= 0 && Y < h) buf[Y * w + X] = fg;
                        }
                }
            }
            pen += CJK_W * scale;
        }
        p += nb;
    }
    esp_lcd_panel_draw_bitmap(s_panel, x0, y0, x0 + w, y0 + h, buf);
    lcd_wait();
}

void orz_lcd_mixed_c(int y0, const char *s, uint16_t fg, uint16_t bg, int scale)
{
    int w = orz_lcd_mixed_width(s, scale);
    int x = (ORZ_LCD_H - w) / 2;
    if (x < 0) x = 0;
    orz_lcd_mixed(x, y0, s, fg, bg, scale);
}
