#pragma once

#include <stdint.h>
#include <stdio.h>
#include <time.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_err.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "lvgl.h"

#include "desktop_common.h"


#include "bsp/bsp.h"
 
#include "../apps/alerts_app.h"
#include "../apps/app_home_nav.h"
#include "../apps/app_status_bar.h"
#include "../apps/apps_idle_task.h"
#include "../apps/battery_app.h"
#include "../apps/bt_app.h"
#include "../apps/camera_app.h"
#include "../apps/gallery_app.h"
#include "../apps/music_app.h"
#include "peripherals/ht517.h"
#include "peripherals/st7365p.h"
#include "../apps/oscilloscope_app.h"
#include "../apps/power_app.h"
#include "../apps/sd_app.h"
#include "../apps/setting_app.h"
#include "../apps/tools_app.h"
#include "../apps/wifi_app.h"

#define LVGL_TICK_PERIOD_MS (DESKTOP_COMMON_LVGL_TICK_PERIOD_MS)
#define LVGL_TASK_PERIOD_MS 10U
#define LVGL_DRAW_BUF_LINES 40U

#define DESKTOP_ICON_COLS 3U
#define DESKTOP_ICON_ROWS 4U
#define DESKTOP_ICON_COUNT (DESKTOP_ICON_COLS * DESKTOP_ICON_ROWS)

#define DESKTOP_MARGIN_X 12
#define DESKTOP_BAR_HEIGHT 24
#define DESKTOP_GRID_TOP_GAP 8
#define DESKTOP_GRID_BOTTOM_GAP 8
#define DESKTOP_ICON_GAP_X 10
#define DESKTOP_ICON_GAP_Y 10

#if LV_FONT_MONTSERRAT_22
#define DESKTOP_FONT_ICON (&lv_font_montserrat_22)
#else
#define DESKTOP_FONT_ICON LV_FONT_DEFAULT
#endif

#if LV_FONT_MONTSERRAT_22
#define DESKTOP_FONT_ICON_NAME (&lv_font_montserrat_22)
#elif LV_FONT_MONTSERRAT_18
#define DESKTOP_FONT_ICON_NAME (&lv_font_montserrat_18)
#else
#define DESKTOP_FONT_ICON_NAME LV_FONT_DEFAULT
#endif

#if LV_FONT_MONTSERRAT_18
#define DESKTOP_FONT_TEXT (&lv_font_montserrat_18)
#else
#define DESKTOP_FONT_TEXT LV_FONT_DEFAULT
#endif

typedef lv_obj_t *(*desktop_app_create_screen_cb_t)(lv_coord_t lcd_w, lv_coord_t lcd_h);
typedef void (*desktop_app_release_cb_t)(void);

typedef struct
{
    const char *symbol;
    const char *name;
    lv_color_t color;
    desktop_app_create_screen_cb_t create_screen_cb;
    desktop_app_release_cb_t release_cb;
} desktop_icon_s;

typedef struct
{
    lv_obj_t *icon_btns[DESKTOP_ICON_COUNT];
    lv_obj_t *icon_symbols[DESKTOP_ICON_COUNT];
    lv_obj_t *icon_names[DESKTOP_ICON_COUNT];
    lv_obj_t *desktop_screen;
    lv_obj_t *active_app_screen;
    uint32_t icon_selected_idx;
    uint32_t active_app_idx;
    uint32_t pending_app_idx;
    uint8_t app_switching;
} desktop_app_select_s;

/* Initialize ST7365 panel, start LVGL task, and create desktop UI. */
esp_err_t desktop_app_start(void);
/* Destroy current child app screen and return to desktop main screen. */
void desktop_app_return_to_home(void);
