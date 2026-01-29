/*
 * SPDX-FileCopyrightText: 2015-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include "esp_log.h"

static const char *TAG = "main";

void app_main(void)
{
    ESP_LOGE(TAG, "This is an ERROR log");
    ESP_LOGW(TAG, "This is a WARN log");
    ESP_LOGI(TAG, "This is an INFO log");
    ESP_LOGD(TAG, "This is a DEBUG log");
    ESP_LOGV(TAG, "This is a VERBOSE log");
    
    printf("Hello world!\n");
}
