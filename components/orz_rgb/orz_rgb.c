// 板载 WS2812 RGB 灯驱动(单灯,RMT 实现)
#include "orz_rgb.h"
#include "driver/rmt_tx.h"

static rmt_channel_handle_t s_chan;
static rmt_encoder_handle_t s_enc;

esp_err_t orz_rgb_init(int gpio)
{
    rmt_tx_channel_config_t txc = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .gpio_num = gpio,
        .mem_block_symbols = 64,
        .resolution_hz = 10 * 1000 * 1000,   // 0.1us/tick
        .trans_queue_depth = 4,
    };
    esp_err_t err = rmt_new_tx_channel(&txc, &s_chan);
    if (err != ESP_OK) return err;

    rmt_bytes_encoder_config_t bec = {
        .bit0 = { .level0 = 1, .duration0 = 4, .level1 = 0, .duration1 = 8 },
        .bit1 = { .level0 = 1, .duration0 = 8, .level1 = 0, .duration1 = 4 },
        .flags.msb_first = 1,
    };
    err = rmt_new_bytes_encoder(&bec, &s_enc);
    if (err != ESP_OK) return err;

    return rmt_enable(s_chan);
}

void orz_rgb_set(uint8_t r, uint8_t g, uint8_t b)
{
    if (!s_chan) return;
    uint8_t d[3] = { g, r, b };   // WS2812 顺序为 GRB
    rmt_transmit_config_t tc = { .loop_count = 0 };
    rmt_transmit(s_chan, s_enc, d, sizeof(d), &tc);
    rmt_tx_wait_all_done(s_chan, 100);
}
