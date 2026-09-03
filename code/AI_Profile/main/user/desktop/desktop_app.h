#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_wifi.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <material_symbols.h>
#include "lvgl.h"

#include "user/device/dev_button.h"
#include "user/device/dev_st7365p.h"
#include "user/inc/user_config.h"
#ifdef __cplusplus
extern "C" {
#endif

// clang-format off
#define LVGL_TICK_PERIOD_MS (10U)
#define LVGL_TASK_PERIOD_MS (10U)
#define LVGL_DRAW_BUF_LINES (72U)

#define DESKTOP_ICON_SELECT_TIMEOUT_MS (3000U)

#define DESKTOP_TOP_BAR_HEIGHT    (26U)
#define DESKTOP_BOTTOM_BAR_HEIGHT (26U)
#define DESKTOP_TOOLBAR_COLOR_HEX (0x87CEEBU)

#define DESKTOP_APP_MSG_TEXT_LEN      (768U)
#define DESKTOP_APP_MSG_KEEP_MS       (5000U)
#define DESKTOP_SYS_INFO_REFRESH_MS   (1000U)
#define DESKTOP_TIME_REFRESH_MS       (1000U)
#define DESKTOP_TIME_SYNC_CHECK_MS    (10000U)
#define DESKTOP_WEATHER_REFRESH_MS    (3000U)
#define DESKTOP_REALTIME_MIN_UNIX_SEC (1704067200ULL)

#define DESKTOP_ICON_COLS  (3U)
#define DESKTOP_ICON_ROWS  (4U)
#define DESKTOP_ICON_COUNT (DESKTOP_ICON_COLS * DESKTOP_ICON_ROWS)

#define DESKTOP_MARGIN_X   (4U)
#define DESKTOP_MARGIN_Y   (4U)
#define DESKTOP_ICON_GAP_X (8U)
#define DESKTOP_ICON_GAP_Y (8U)

#define RGB565_BLACK   (0x0000U)
#define RGB565_WHITE   (0xFFFFU)
#define RGB565_RED     (0xF800U)
#define RGB565_GREEN   (0x07E0U)
#define RGB565_BLUE    (0x001FU)
#define RGB565_YELLOW  (0xFFE0U)
#define RGB565_CYAN    (0x07FFU)
#define RGB565_MAGENTA (0xF81FU)
#define RGB565_ORANGE  (0xFD20U)
#define RGB565_GRAY    (0x8410U)

#ifndef DESKTOP_TEXT_FONT
#define DESKTOP_TEXT_FONT BUILTIN_TEXT_FONT
#endif

#ifndef DESKTOP_SYMBOL_FONT
#define DESKTOP_SYMBOL_FONT BUILTIN_ICON_FONT
#endif
// clang-format on

LV_FONT_DECLARE(DESKTOP_TEXT_FONT);
LV_FONT_DECLARE(DESKTOP_SYMBOL_FONT);

typedef void (*ui_menu_home_cb_t)(void* user_ctx);

typedef lv_obj_t* (*ui_menu_create_fn_t)(
    lv_obj_t* parent,
    lv_coord_t area_w,
    lv_coord_t area_h,
    ui_menu_home_cb_t home_cb,
    void* home_user_ctx
);
typedef void (*ui_menu_destroy_fn_t)(lv_obj_t* screen);

typedef ui_menu_create_fn_t desktop_ui_create_fn_t;
typedef ui_menu_destroy_fn_t desktop_ui_destroy_fn_t;

typedef struct {
    const char* symbol;
    const char* name;
    uint32_t color_hex;
    desktop_ui_create_fn_t create_screen;
    desktop_ui_destroy_fn_t destroy_screen;
} desktop_icon_s;

typedef struct {
    int switching;
    uint32_t sel_timout;
    lv_obj_t* icon_btn[DESKTOP_ICON_COUNT];
    lv_obj_t* icon_symbol_label[DESKTOP_ICON_COUNT];
    lv_obj_t* icon_name_label[DESKTOP_ICON_COUNT];
    bool ui_active;
    uint8_t active_ui_index;
    lv_obj_t* content_area;
    lv_obj_t* desktop_grid;
    lv_obj_t* active_ui_root;
} desktop_icon_op_s;

void desktop_tick_event(void* arg);
void desktop_flush_event(lv_display_t* disp, const lv_area_t* area, uint8_t* px_map);
lv_color_t desktop_invert_color(lv_color_t color);

esp_err_t desktop_start_task(void);
void desktop_post_message(const char* msg);

#ifdef __cplusplus
}
#endif
