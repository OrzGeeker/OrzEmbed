// ESP32-C6-LCD-1.47 板级自动自检 (hwtest)
// 一条固件跑完:芯片/Flash/NVS/SD/LCD/RGB/GPIO(桥接+卡死)/WiFi,打印 PASS/FAIL 汇总
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <inttypes.h>
#include <sys/stat.h>
#include <unistd.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_heap_caps.h"
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

// 暴露在排针上的 GPIO(排除 USB 12/13、控制台 UART 16/17)
static const int PINS[] = {0,1,2,3,4,5,6,7,8,9,10,11,14,15,18,19,20,21,22,23};
#define NP (sizeof(PINS)/sizeof(PINS[0]))
// 板上有外部上下拉的脚(自检时不算故障)
static const int KNOWN_PULLDN[] = {22};   // LCD 背光 10K 下拉
static const int KNOWN_PULLUP[] = {4,5,6,7,8,9};  // SD/SPI 总线 + RGB 数据 + BOOT 上拉

static int g_pass, g_fail, g_warn;

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
}

static bool in_list(const int *l, int n, int v) { for (int i=0;i<n;i++) if (l[i]==v) return true; return false; }

// ================= 1. 芯片 =================
static void test_chip(void)
{
    esp_chip_info_t ci;
    esp_chip_info(&ci);
    bool ok = (ci.model == CHIP_ESP32C6);
    report("chip", ok ? "PASS" : "FAIL", "ESP32-C6 cores=%d rev=v%d.%d",
           ci.cores, ci.revision/100, ci.revision%100);
    uint32_t id = 0;
    esp_flash_read_id(NULL, &id);
    report("flash_id", "INFO", "JEDEC=0x%06" PRIx32, id);
}

// ================= 2. Flash =================
static void test_flash(void)
{
    uint32_t sz = 0;
    esp_err_t e = esp_flash_get_size(NULL, &sz);
    report("flash", (e==ESP_OK && sz>=2*1024*1024) ? "PASS" : "FAIL",
           "size=%" PRIu32 " MB (err=%s)", sz/1048576, esp_err_to_name(e));
    report("heap", "INFO", "free=%u min=%u",
           (unsigned)esp_get_free_heap_size(), (unsigned)esp_get_minimum_free_heap_size());
    report("reset", "INFO", "reason=%d", (int)esp_reset_reason());
}

// ================= 3. NVS =================
static void test_nvs(void)
{
    esp_err_t e = nvs_flash_init();
    if (e == ESP_ERR_NVS_NO_FREE_PAGES || e == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        e = nvs_flash_init();
    }
    nvs_handle_t h = 0;
    uint32_t out = 0;
    bool ok = (e == ESP_OK) && (nvs_open("hwtest", NVS_READWRITE, &h) == ESP_OK);
    if (ok) {
        const uint32_t magic = 0xA5A55A5A;
        ok = (nvs_set_u32(h, "k", magic) == ESP_OK) &&
             (nvs_commit(h) == ESP_OK) &&
             (nvs_get_u32(h, "k", &out) == ESP_OK) &&
             (out == magic);
        nvs_erase_key(h, "k");
        nvs_commit(h);
        nvs_close(h);
    }
    report("nvs", ok ? "PASS" : "FAIL", "rw+commit+verify");
}

// ================= 4. GPIO 桥接/短路 自动检测 =================
// 原理:所有脚设输入下拉;先记录基线(外部上拉会读 1)。
// 逐个把某脚驱动为高,若"基线上不是 1"的脚变成 1 -> 两脚之间存在低阻连接(锡桥)。
static void test_gpio_bridge(void)
{
    int base[NP], bridges = 0;
    for (int i = 0; i < NP; i++) {
        gpio_config_t c = {.pin_bit_mask = 1ULL << PINS[i], .mode = GPIO_MODE_INPUT,
                           .pull_up_en = GPIO_PULLUP_DISABLE, .pull_down_en = GPIO_PULLDOWN_ENABLE,
                           .intr_type = GPIO_INTR_DISABLE};
        gpio_config(&c);
    }
    vTaskDelay(pdMS_TO_TICKS(20));
    for (int i = 0; i < NP; i++) base[i] = gpio_get_level(PINS[i]);

    for (int i = 0; i < NP; i++) {
        gpio_set_direction(PINS[i], GPIO_MODE_INPUT_OUTPUT);
        gpio_set_pull_mode(PINS[i], GPIO_FLOATING);
        gpio_set_level(PINS[i], 1);
        vTaskDelay(pdMS_TO_TICKS(3));
        for (int j = 0; j < NP; j++) {
            if (j == i || base[j] == 1) continue;
            if (gpio_get_level(PINS[j]) == 1) {
                ESP_LOGE("HWTEST", "  bridge: GPIO%d <-> GPIO%d", PINS[i], PINS[j]);
                bridges++;
            }
        }
        gpio_set_level(PINS[i], 0);
        gpio_set_direction(PINS[i], GPIO_MODE_INPUT);
        gpio_set_pull_mode(PINS[i], GPIO_PULLDOWN_ONLY);
    }
    report("gpio_bridge", bridges ? "FAIL" : "PASS", "%d bridge(s)", bridges);

    // 上/下拉基线:报告外部上下拉脚(非故障),统计"非已知"的异常
    char low[160] = "", high[160] = "";
    int susp = 0;
    for (int i = 0; i < NP; i++) {
        gpio_set_pull_mode(PINS[i], GPIO_PULLUP_ONLY);
        vTaskDelay(pdMS_TO_TICKS(2));
        if (gpio_get_level(PINS[i]) == 0) {
            bool k = in_list(KNOWN_PULLDN, 1, PINS[i]);
            snprintf(low+strlen(low), sizeof(low)-strlen(low), " %d%s", PINS[i], k ? "(*)" : "");
            if (!k) susp++;
        }
        gpio_set_pull_mode(PINS[i], GPIO_PULLDOWN_ONLY);
        vTaskDelay(pdMS_TO_TICKS(2));
        if (gpio_get_level(PINS[i]) == 1) {
            bool k = in_list(KNOWN_PULLUP, 6, PINS[i]);
            snprintf(high+strlen(high), sizeof(high)-strlen(high), " %d%s", PINS[i], k ? "(*)" : "");
            if (!k) susp++;
        }
    }
    report("gpio_pull", susp ? "WARN" : "PASS",
           "pullup=0:%s | pulldn=1:%s", low[0]?low:"(none)", high[0]?high:"(none)");
}

// ================= 5. LCD =================
static esp_lcd_panel_handle_t s_panel;
static bool test_lcd(void)
{
    spi_bus_config_t bus = {
        .mosi_io_num = PIN_MOSI, .miso_io_num = PIN_MISO, .sclk_io_num = PIN_SCLK,
        .quadwp_io_num = -1, .quadhd_io_num = -1,
        .max_transfer_sz = LCD_H * 40 * sizeof(uint16_t),
    };
    if (spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO) != ESP_OK) return false;

    esp_lcd_panel_io_handle_t io;
    esp_lcd_panel_io_spi_config_t iocfg = {
        .cs_gpio_num = PIN_LCD_CS, .dc_gpio_num = PIN_LCD_DC, .spi_mode = 0,
        .pclk_hz = 20*1000*1000, .trans_queue_depth = 10, .lcd_cmd_bits = 8, .lcd_param_bits = 8,
    };
    if (esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI2_HOST, &iocfg, &io) != ESP_OK) return false;
    esp_lcd_panel_dev_config_t pcfg = {
        .reset_gpio_num = PIN_LCD_RST, .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB, .bits_per_pixel = 16,
    };
    if (esp_lcd_new_panel_st7789(io, &pcfg, &s_panel) != ESP_OK) return false;
    if (esp_lcd_panel_reset(s_panel) != ESP_OK) return false;
    if (esp_lcd_panel_init(s_panel) != ESP_OK) return false;
    esp_lcd_panel_invert_color(s_panel, true);
    esp_lcd_panel_set_gap(s_panel, 34, 0);
    esp_lcd_panel_disp_on_off(s_panel, true);

    gpio_config_t bl = {.pin_bit_mask = 1ULL<<PIN_LCD_BL, .mode = GPIO_MODE_OUTPUT};
    gpio_config(&bl);
    gpio_set_level(PIN_LCD_BL, 1);

    static uint16_t buf[LCD_H*20];
    const uint16_t colors[4] = {0xF800, 0x07E0, 0x001F, 0xFFFF}; // R G B W
    for (int c = 0; c < 4; c++) {
        for (int i = 0; i < LCD_H*20; i++) buf[i] = colors[c];
        for (int y = 0; y < LCD_V; y += 20)
            esp_lcd_panel_draw_bitmap(s_panel, 0, y, LCD_H, y+20, buf);
    }
    return true;
}

// ================= 6. SD =================
static void test_sd(void)
{
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = SPI2_HOST;
    sdspi_device_config_t slot = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot.host_id = SPI2_HOST;
    slot.gpio_cs = PIN_SD_CS;
    esp_vfs_fat_mount_config_t mcfg = {.format_if_mount_failed = false, .max_files = 5,
                                       .allocation_unit_size = 16*1024};
    sdmmc_card_t *card = NULL;
    esp_err_t e = esp_vfs_fat_sdspi_mount("/sdcard", &host, &slot, &mcfg, &card);
    if (e != ESP_OK) {
        report("sd", "FAIL", "mount: %s", esp_err_to_name(e));
        return;
    }
    // 写 1MB + 读回校验
    const char *path = "/sdcard/hwtest.bin";
    static uint8_t w[4096], r[4096];
    for (int i = 0; i < 4096; i++) w[i] = (uint8_t)(i*7 + 13);
    FILE *f = fopen(path, "wb");
    bool ok = (f != NULL);
    if (ok) {
        for (int i = 0; i < 256; i++) if (fwrite(w, 1, 4096, f) != 4096) { ok = false; break; }
        fclose(f);
    }
    if (ok) {
        f = fopen(path, "rb");
        ok = (f != NULL);
        for (int i = 0; i < 256 && ok; i++) {
            if (fread(r, 1, 4096, f) != 4096 || memcmp(w, r, 4096) != 0) ok = false;
        }
        if (f) fclose(f);
    }
    struct stat st;
    long size = (stat(path, &st) == 0) ? (long)st.st_size : -1;
    unlink(path);
    report("sd", ok ? "PASS" : "FAIL", "%s size=%ld (1MB rw+verify)",
           card->cid.name, size);
    esp_vfs_fat_sdcard_unmount("/sdcard", card);
}

// ================= 7. RGB LED (WS2812 @ GPIO8) =================
static void test_rgb(void)
{
    rmt_channel_handle_t chan = NULL;
    rmt_tx_channel_config_t txc = {
        .clk_src = RMT_CLK_SRC_DEFAULT, .gpio_num = PIN_RGB, .mem_block_symbols = 64,
        .resolution_hz = 10*1000*1000, .trans_queue_depth = 4,
    };
    if (rmt_new_tx_channel(&txc, &chan) != ESP_OK) { report("rgb","FAIL","rmt chan"); return; }
    rmt_bytes_encoder_config_t bec = {
        .bit0 = {.level0=1,.duration0=4,.level1=0,.duration1=8},
        .bit1 = {.level0=1,.duration0=8,.level1=0,.duration1=4},
        .flags.msb_first = 1,
    };
    rmt_encoder_handle_t enc = NULL;
    rmt_new_bytes_encoder(&bec, &enc);
    rmt_enable(chan);
    rmt_transmit_config_t tc = {.loop_count = 0};
    const uint8_t seq[3][3] = {{0,80,0},{80,0,0},{0,0,80}}; // G R B
    bool ok = true;
    for (int i = 0; i < 3; i++) {
        if (rmt_transmit(chan, enc, seq[i], 3, &tc) != ESP_OK) ok = false;
        rmt_tx_wait_all_done(chan, 100);
        vTaskDelay(pdMS_TO_TICKS(400));
    }
    report("rgb", ok ? "PASS" : "FAIL", "WS2812 G/R/B sent (visual)");
}

// ================= 8. WiFi =================
static void test_wifi(void)
{
    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t wc = WIFI_INIT_CONFIG_DEFAULT();
    if (esp_wifi_init(&wc) != ESP_OK) { report("wifi","FAIL","init"); return; }
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_start();
    wifi_scan_config_t sc = {0};
    if (esp_wifi_scan_start(&sc, true) != ESP_OK) { report("wifi","FAIL","scan"); return; }
    uint16_t n = 0;
    esp_wifi_scan_get_ap_num(&n);
    if (n == 0) { report("wifi","WARN","0 AP found"); return; }
    wifi_ap_record_t *aps = calloc(n, sizeof(*aps));
    esp_wifi_scan_get_ap_records(&n, aps);
    int best = -127, bi = 0;
    for (int i = 0; i < n; i++) if (aps[i].rssi > best) { best = aps[i].rssi; bi = i; }
    for (int i = 0; i < n && i < 8; i++)
        ESP_LOGI("HWTEST", "  AP %-20s rssi=%d ch=%d", (char*)aps[i].ssid, aps[i].rssi, aps[i].primary);
    report("wifi", best > -80 ? "PASS" : "WARN", "%d AP, best=%d dBm (%s)",
           n, best, (char*)aps[bi].ssid);
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
    report("lcd", lcd ? "PASS" : "FAIL", "ST7789 %dx%d init+draw (visual)", LCD_H, LCD_V);
    test_sd();
    test_rgb();
    test_wifi();
    ESP_LOGI("HWTEST", "========== SUMMARY: PASS=%d FAIL=%d WARN=%d ==========",
             g_pass, g_fail, g_warn);
    ESP_LOGI("HWTEST", "HWTEST_DONE");

    // ---- 进入 GPIO 探针模式:供交互式"导通/虚焊"测试 ----
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
