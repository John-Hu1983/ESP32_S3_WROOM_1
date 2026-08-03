#pragma once

#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "lvgl.h"

#define APP_STATUS_BAR_HEIGHT 24

typedef struct
{
    uint8_t battery_percent;
    bool network_connected;
    int8_t network_rssi_dbm;
    uint8_t cpu_usage_percent;
    uint8_t ram_usage_percent;
    uint8_t psram_usage_percent;
    char time_hhmm[6];
} app_status_snapshot_t;

esp_err_t app_status_bar_init(lv_coord_t lcd_w, lv_coord_t lcd_h);
void app_status_bar_submit_snapshot(const app_status_snapshot_t *snapshot);
void app_status_bar_set_network_state(bool connected, int8_t rssi_dbm);
lv_coord_t app_status_bar_content_top(void);
lv_coord_t app_status_bar_content_bottom(void);
