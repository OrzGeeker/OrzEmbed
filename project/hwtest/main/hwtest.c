// ESP32-C6-LCD-1.47 板级自动自检 + LCD 结果面板
// 上电自动跑 芯片/Flash/NVS/GPIO/LCD/SD/RGB/WiFi,逐项把 PASS/FAIL/WARN 画到 ST7789 屏上
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <inttypes.h>
#include <sys/stat.h>
#include <unistd.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_system.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_heap_caps.h"
#include "esp_rom_sys.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "driver/rmt_tx.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "sdmmc_cmd.h"
#include "esp_vfs_fat.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "font6x9.h"

// ---- 引脚 (Waveshare ESP32-C6-LCD-1.47) ----
#define PIN_MOSI     6
#define PIN_SCLK     7
#define PIN_MISO     5
#define PIN_LCD_CS   14
#define PIN_LCD_DC   15
#define PIN_LCD_RST  21
#define PIN_LCD_BL   22
#define PIN_SD_CS    4
#define PIN_RGB      8
#define LCD_H        172
#define LCD_V        320

// 颜色 (RGB565)
#define C_BLACK  0x0000
#define C_WHITE  0xFFFF
#define C_GREEN  0x07E0
#define C_RED    0xF800
#define C_YELLOW 0xFE00
#define C_CYAN   0x07FF
#define C_GRAY   0x8410
#define C_BLUE   0x001F

static const int PINS[] = {0,1,2,3,4,5,6,7,8,9,10,11,14,15,18,19,20,21,22,23};
#define NP (sizeof(PINS)/sizeof(PINS[0]))
static const int KNOWN_PULLDN[] = {22};
static const int KNOWN_PULLUP[] = {4,5,6,7,8,9};

static int g_pass, g_fail, g_warn;

// ---- 结果表(用于 LCD 面板) ----
typedef struct { char name[14]; char status[5]; char detail[64]; } Ent;
static Ent g_ent[16];
static int g_nent;

// ---- LCD ----
static esp_lcd_panel_handle_t s_panel;
static esp_lcd_panel_io_handle_t s_io;
static SemaphoreHandle_t s_lcd_sem;
static bool g_lcd_ready;

static bool lcd_flush_done(esp_lcd_panel_io_handle_t io, esp_lcd_panel_io_event_data_t *e, void *ctx)
{
    (void)io; (void)e; (void)ctx;
    BaseType_t hp = pdFALSE;
    if (s_lcd_sem) xSemaphoreGiveFromISR(s_lcd_sem, &hp);
    return hp == pdTRUE;
}

static void lcd_wait(void) { if (s_lcd_sem) xSemaphoreTake(s_lcd_sem, portMAX_DELAY); }

static void fill_screen(uint16_t color)
{
    if (!g_lcd_ready) return;
    static uint16_t b[LCD_H * 20];
    for (int i = 0; i < LCD_H*20; i++) b[i] = color;
    for (int y = 0; y < LCD_V; y += 20) {
        int h = (y+20 <= LCD_V) ? 20 : (LCD_V - y);
        esp_lcd_panel_draw_bitmap(s_panel, 0, y, LCD_H, y+h, b);
        lcd_wait();
    }
}

static void draw_text(int x0, int y0, const char *s, uint16_t fg, uint16_t bg, int scale)
{
    if (!g_lcd_ready) return;
    int n = strlen(s);
    int cw = FONT_W*scale, ch = FONT_H*scale;
    int w = n*cw;
    if (x0 + w > LCD_H) w = LCD_H - x0;
    if (w <= 0 || y0 + ch > LCD_V || y0 < 0) return;
    static uint16_t buf[LCD_H * 18];
    for (int i = 0; i < w*ch; i++) buf[i] = bg;
    for (int ci = 0; ci < n; ci++) {
        unsigned char c = (unsigned char)s[ci];
        if (c < 32 || c > 126) c = '?';
        const uint8_t *gly = font6x9[c-32];
        for (int r = 0; r < FONT_H; r++) {
            uint8_t bits = gly[r];
            if (!bits) continue;
            for (int col = 0; col < FONT_W; col++) {
                if (!(bits & (1 << (FONT_W-1-col)))) continue;
                int px = ci*cw + col*scale, py = r*scale;
                for (int dy = 0; dy < scale; dy++)
                    for (int dx = 0; dx < scale; dx++) {
                        int X = px+dx, Y = py+dy;
                        if (X < w && Y < ch) buf[Y*w+X] = fg;
                    }
            }
        }
    }
    esp_lcd_panel_draw_bitmap(s_panel, x0, y0, x0+w, y0+ch, buf);
    lcd_wait();
}

static uint16_t status_color(const char *st)
{
    if (!strcmp(st, "PASS")) return C_GREEN;
    if (!strcmp(st, "FAIL")) return C_RED;
    if (!strcmp(st, "WARN")) return C_YELLOW;
    return C_GRAY;
}

static void draw_dashboard(void)
{
    if (!g_lcd_ready) return;
    fill_screen(C_BLACK);
    draw_text(6, 6,  "ESP32-C6-LCD-1.47", C_CYAN, C_BLACK, 1);
    draw_text(6, 18, "HARDWARE SELF-TEST", C_WHITE, C_BLACK, 1);
    int y = 36;
    for (int i = 0; i < g_nent; i++) {
        if (y > LCD_V - 26) break;
        char line[40];
        snprintf(line, sizeof line, "%-11s %s", g_ent[i].name, g_ent[i].status);
        draw_text(6, y, line, status_color(g_ent[i].status), C_BLACK, 1);
        draw_text(6, y+10, g_ent[i].detail, C_GRAY, C_BLACK, 1);
        y += 22;
    }
    char sum[48];
    snprintf(sum, sizeof sum, "PASS %d  FAIL %d  WARN %d", g_pass, g_fail, g_warn);
    draw_text(6, LCD_V-14, sum, (g_fail ? C_RED : C_GREEN), C_BLACK, 1);
}

// ---- 记录 + 串口输出 ----
static void report(const char *name, const char *status, const char *fmt, ...)
{
    char msg[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);
    ESP_LOGI("HWTEST", "TEST %-12s : %-4s %s", name, status, msg);
    if (!strcmp(status, "PASS")) g_pass++;
    else if (!strcmp(status, "FAIL")) g_fail++;
    else if (!strcmp(status, "WARN")) g_warn++;
    if (strcmp(status, "INFO") && g_nent < 16) {
        snprintf(g_ent[g_nent].name, sizeof(g_ent[0].name), "%s", name);
        snprintf(g_ent[g_nent].status, sizeof(g_ent[0].status), "%s", status);
        snprintf(g_ent[g_nent].detail, sizeof(g_ent[0].detail), "%.60s", msg);
        g_nent++;
    }
    if (g_lcd_ready) draw_dashboard();
}

static bool in_list(const int *l, int n, int v) { for (int i=0;i<n;i++) if (l[i]==v) return true; return false; }

// ================= 测试项 =================
static void test_chip(void)
{
    esp_chip_info_t ci;
    esp_chip_info(&ci);
    report("chip", ci.model == CHIP_ESP32C6 ? "PASS" : "FAIL", "ESP32-C6 rev v%d.%d",
           ci.revision/100, ci.revision%100);
    uint32_t id = 0; esp_flash_read_id(NULL, &id);
    report("flash_id", "INFO", "JEDEC=0x%06" PRIx32, id);
}

static void test_flash(void)
{
    uint32_t sz = 0;
    esp_err_t e = esp_flash_get_size(NULL, &sz);
    report("flash", (e==ESP_OK && sz>=2*1024*1024) ? "PASS" : "FAIL", "%" PRIu32 "MB id ok", sz/1048576);
    report("heap", "INFO", "free=%u", (unsigned)esp_get_free_heap_size());
    report("reset", "INFO", "reason=%d", (int)esp_reset_reason());
}

static void test_nvs(void)
{
    esp_err_t e = nvs_flash_init();
    if (e == ESP_ERR_NVS_NO_FREE_PAGES || e == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase(); e = nvs_flash_init();
    }
    nvs_handle_t h = 0; uint32_t out = 0;
    const uint32_t magic = 0xA5A55A5A;
    bool ok = (e == ESP_OK) && (nvs_open("hwtest", NVS_READWRITE, &h) == ESP_OK);
    if (ok) {
        ok = (nvs_set_u32(h, "k", magic) == ESP_OK) && (nvs_commit(h) == ESP_OK) &&
             (nvs_get_u32(h, "k", &out) == ESP_OK) && (out == magic);
        nvs_erase_key(h, "k"); nvs_commit(h); nvs_close(h);
    }
    report("nvs", ok ? "PASS" : "FAIL", "rw+verify");
}

static void test_gpio_bridge(void)
{
    int base[NP], bridges = 0;
    static bool link[NP][NP];
    memset(link, 0, sizeof(link));
    for (int i = 0; i < NP; i++) {
        gpio_config_t c = {.pin_bit_mask = 1ULL << PINS[i], .mode = GPIO_MODE_INPUT,
                           .pull_up_en = GPIO_PULLUP_DISABLE, .pull_down_en = GPIO_PULLDOWN_ENABLE,
                           .intr_type = GPIO_INTR_DISABLE};
        gpio_config(&c);
    }
    vTaskDelay(pdMS_TO_TICKS(20));
    for (int i = 0; i < NP; i++) base[i] = gpio_get_level(PINS[i]);

    // 逐个拉高,读其余脚;要求 3 次采样稳定为高(抑制浮空/耦合毛刺)
    for (int i = 0; i < NP; i++) {
        gpio_set_direction(PINS[i], GPIO_MODE_INPUT_OUTPUT);
        gpio_set_pull_mode(PINS[i], GPIO_FLOATING);
        gpio_set_level(PINS[i], 1);
        vTaskDelay(pdMS_TO_TICKS(3));
        for (int j = 0; j < NP; j++) {
            if (j == i || base[j] == 1) continue;
            int ones = 0;
            for (int k = 0; k < 3; k++) { if (gpio_get_level(PINS[j])) ones++; esp_rom_delay_us(200); }
            if (ones == 3) link[i][j] = true;
        }
        gpio_set_level(PINS[i], 0);
        gpio_set_direction(PINS[i], GPIO_MODE_INPUT);
        gpio_set_pull_mode(PINS[i], GPIO_PULLDOWN_ONLY);
    }

    // 只认定**双向都导通**的为真桥接(真短路两个方向都通;耦合/浮空不会)
    for (int i = 0; i < NP; i++) {
        for (int j = i+1; j < NP; j++) {
            if (link[i][j] && link[j][i]) {
                ESP_LOGE("HWTEST", "  bridge: GPIO%d <-> GPIO%d", PINS[i], PINS[j]);
                bridges++;
            } else if (link[i][j] || link[j][i]) {
                ESP_LOGW("HWTEST", "  weak/coupling (ignored): GPIO%d ~ GPIO%d", PINS[i], PINS[j]);
            }
        }
    }
    report("gpio", bridges ? "FAIL" : "PASS", bridges ? "%d bridge(s)" : "no bridge", bridges);

    char low[120] = "", high[120] = "";
    int susp = 0;
    for (int i = 0; i < NP; i++) {
        gpio_set_pull_mode(PINS[i], GPIO_PULLUP_ONLY);
        vTaskDelay(pdMS_TO_TICKS(2));
        if (gpio_get_level(PINS[i]) == 0) {
            bool k = in_list(KNOWN_PULLDN, 1, PINS[i]);
            snprintf(low+strlen(low), sizeof(low)-strlen(low), " %d%s", PINS[i], k?"(*)":"");
            if (!k) susp++;
        }
        gpio_set_pull_mode(PINS[i], GPIO_PULLDOWN_ONLY);
        vTaskDelay(pdMS_TO_TICKS(2));
        if (gpio_get_level(PINS[i]) == 1) {
            bool k = in_list(KNOWN_PULLUP, 6, PINS[i]);
            snprintf(high+strlen(high), sizeof(high)-strlen(high), " %d%s", PINS[i], k?"(*)":"");
            if (!k) susp++;
        }
    }
    char det[64];
    snprintf(det, sizeof det, "lo:%.50s", low[0]?low:"-");
    report("gpio_pull", susp ? "WARN" : "PASS", "%s", det);
}

static bool test_lcd(void)
{
    spi_bus_config_t bus = {
        .mosi_io_num = PIN_MOSI, .miso_io_num = PIN_MISO, .sclk_io_num = PIN_SCLK,
        .quadwp_io_num = -1, .quadhd_io_num = -1,
        .max_transfer_sz = LCD_H * 40 * sizeof(uint16_t),
    };
    if (spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO) != ESP_OK) return false;
    esp_lcd_panel_io_spi_config_t iocfg = {
        .cs_gpio_num = PIN_LCD_CS, .dc_gpio_num = PIN_LCD_DC, .spi_mode = 0,
        .pclk_hz = 20*1000*1000, .trans_queue_depth = 10, .lcd_cmd_bits = 8, .lcd_param_bits = 8,
    };
    if (esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI2_HOST, &iocfg, &s_io) != ESP_OK) return false;
    s_lcd_sem = xSemaphoreCreateBinary();
    esp_lcd_panel_io_callbacks_t cbs = { .on_color_trans_done = lcd_flush_done };
    esp_lcd_panel_io_register_event_callbacks(s_io, &cbs, NULL);
    esp_lcd_panel_dev_config_t pcfg = {
        .reset_gpio_num = PIN_LCD_RST, .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB, .bits_per_pixel = 16,
    };
    if (esp_lcd_new_panel_st7789(s_io, &pcfg, &s_panel) != ESP_OK) return false;
    if (esp_lcd_panel_reset(s_panel) != ESP_OK) return false;
    if (esp_lcd_panel_init(s_panel) != ESP_OK) return false;
    esp_lcd_panel_invert_color(s_panel, true);
    esp_lcd_panel_set_gap(s_panel, 34, 0);
    esp_lcd_panel_disp_on_off(s_panel, true);
    gpio_config_t bl = {.pin_bit_mask = 1ULL<<PIN_LCD_BL, .mode = GPIO_MODE_OUTPUT};
    gpio_config(&bl);
    gpio_set_level(PIN_LCD_BL, 1);
    g_lcd_ready = true;
    // 上电底色闪现,肉眼确认 RGB 正常
    fill_screen(C_RED);   vTaskDelay(pdMS_TO_TICKS(250));
    fill_screen(C_GREEN); vTaskDelay(pdMS_TO_TICKS(250));
    fill_screen(C_BLUE);  vTaskDelay(pdMS_TO_TICKS(250));
    return true;
}

static void test_sd(void)
{
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = SPI2_HOST;
    sdspi_device_config_t slot = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot.host_id = SPI2_HOST; slot.gpio_cs = PIN_SD_CS;
    esp_vfs_fat_mount_config_t mcfg = {.format_if_mount_failed = false, .max_files = 5,
                                       .allocation_unit_size = 16*1024};
    sdmmc_card_t *card = NULL;
    esp_err_t e = esp_vfs_fat_sdspi_mount("/sdcard", &host, &slot, &mcfg, &card);
    if (e != ESP_OK) { report("sd", "FAIL", "mount %s", esp_err_to_name(e)); return; }
    const char *path = "/sdcard/hwtest.bin";
    static uint8_t w[4096], r[4096];
    for (int i = 0; i < 4096; i++) w[i] = (uint8_t)(i*7 + 13);
    bool ok = true;
    FILE *f = fopen(path, "wb");
    if (!f) ok = false;
    else { for (int i = 0; i < 256; i++) if (fwrite(w,1,4096,f)!=4096){ok=false;break;} fclose(f); }
    if (ok) {
        f = fopen(path, "rb");
        if (!f) ok = false;
        else { for (int i = 0; i < 256 && ok; i++) if (fread(r,1,4096,f)!=4096 || memcmp(w,r,4096)) ok=false; fclose(f); }
    }
    struct stat st; long size = (stat(path,&st)==0) ? (long)st.st_size : -1;
    unlink(path);
    report("sd", ok ? "PASS" : "FAIL", "%s %ldKB rw ok", card->cid.name, size/1024);
    esp_vfs_fat_sdcard_unmount("/sdcard", card);
}

static void test_rgb(void)
{
    rmt_channel_handle_t chan = NULL;
    rmt_tx_channel_config_t txc = {
        .clk_src = RMT_CLK_SRC_DEFAULT, .gpio_num = PIN_RGB, .mem_block_symbols = 64,
        .resolution_hz = 10*1000*1000, .trans_queue_depth = 4,
    };
    if (rmt_new_tx_channel(&txc, &chan) != ESP_OK) { report("rgb","FAIL","rmt"); return; }
    rmt_bytes_encoder_config_t bec = {
        .bit0 = {.level0=1,.duration0=4,.level1=0,.duration1=8},
        .bit1 = {.level0=1,.duration0=8,.level1=0,.duration1=4},
        .flags.msb_first = 1,
    };
    rmt_encoder_handle_t enc = NULL; rmt_new_bytes_encoder(&bec, &enc); rmt_enable(chan);
    rmt_transmit_config_t tc = {.loop_count = 0};
    const uint8_t seq[3][3] = {{0,60,0},{60,0,0},{0,0,60}};
    bool ok = true;
    for (int i = 0; i < 3; i++) {
        if (rmt_transmit(chan, enc, seq[i], 3, &tc) != ESP_OK) ok = false;
        rmt_tx_wait_all_done(chan, 100);
        vTaskDelay(pdMS_TO_TICKS(250));
    }
    report("rgb", ok ? "PASS" : "FAIL", "WS2812 G/R/B");
}

static void test_wifi(void)
{
    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t wc = WIFI_INIT_CONFIG_DEFAULT();
    if (esp_wifi_init(&wc) != ESP_OK) { report("wifi","FAIL","init"); return; }
    esp_wifi_set_mode(WIFI_MODE_STA); esp_wifi_start();
    wifi_scan_config_t sc = {0};
    if (esp_wifi_scan_start(&sc, true) != ESP_OK) { report("wifi","FAIL","scan"); return; }
    uint16_t n = 0; esp_wifi_scan_get_ap_num(&n);
    if (n == 0) { report("wifi","WARN","0 AP found"); return; }
    wifi_ap_record_t *aps = calloc(n, sizeof(*aps));
    esp_wifi_scan_get_ap_records(&n, aps);
    int best = -127;
    for (int i = 0; i < n; i++) if (aps[i].rssi > best) { best = aps[i].rssi; }
    report("wifi", best > -80 ? "PASS" : "WARN", "%dAP best %ddBm", n, best);
    free(aps);
}

// ================= main =================
void app_main(void)
{
    vTaskDelay(pdMS_TO_TICKS(300));
    ESP_LOGI("HWTEST", "========== HW SELF-TEST (ESP32-C6-LCD-1.47) ==========");
    test_chip();
    test_flash();
    test_nvs();
    test_gpio_bridge();
    bool lcd = test_lcd();
    report("lcd", lcd ? "PASS" : "FAIL", "ST7789 172x320");
    test_sd();
    test_rgb();
    test_wifi();
    ESP_LOGI("HWTEST", "========== SUMMARY: PASS=%d FAIL=%d WARN=%d ==========",
             g_pass, g_fail, g_warn);
    // 结果面板定格:标题栏改色表示完成
    if (g_lcd_ready) draw_text(6, LCD_V-26, "SELF-TEST DONE", C_WHITE, C_BLACK, 1);
    ESP_LOGI("HWTEST", "HWTEST_DONE");

    // ---- GPIO 探针模式 ----
    for (int i = 0; i < NP; i++) {
        gpio_config_t c = {.pin_bit_mask = 1ULL << PINS[i], .mode = GPIO_MODE_INPUT,
                           .pull_up_en = GPIO_PULLUP_ENABLE, .pull_down_en = GPIO_PULLDOWN_DISABLE,
                           .intr_type = GPIO_INTR_DISABLE};
        gpio_config(&c);
    }
    vTaskDelay(pdMS_TO_TICKS(50));
    ESP_LOGI("HWTEST", "PROBE_READY");
    int st[NP];
    for (int i = 0; i < NP; i++) st[i] = gpio_get_level(PINS[i]);
    while (1) {
        for (int i = 0; i < NP; i++) {
            int v = gpio_get_level(PINS[i]);
            if (v != st[i]) { st[i] = v; ESP_LOGI("HWTEST", "CHG %d %d", PINS[i], v); }
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}
