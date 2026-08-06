#pragma once

#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "lvgl.h"

#define APP_STATUS_BAR_HEIGHT 24

#define APP_THEME_BG_HEX 0x2C001E
#define APP_THEME_SURFACE_HEX 0x3B102C
#define APP_THEME_BAR_HEX 0x5E2750
#define APP_THEME_ACCENT_HEX 0xE95420
#define APP_THEME_ACCENT_ACTIVE_HEX 0xDD4814
#define APP_THEME_TEXT_PRIMARY_HEX 0xF7F3EE
#define APP_THEME_TEXT_SECONDARY_HEX 0xD8CEC4
#define APP_THEME_BORDER_HEX 0xF7F3EE

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
