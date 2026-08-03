#pragma once

#include <stdio.h>
#include <time.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_err.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "lvgl.h"

#include "../apps/app_status_bar.h"
#include "../apps/apps_idle_task.h"
#include "peripherals/st7365p.h"
#include "../apps/oscilloscope_app.h"

#define LVGL_TICK_PERIOD_MS 2U
#define LVGL_TASK_PERIOD_MS 5U
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

typedef struct
{
    const char *symbol;
    const char *name;
    lv_color_t color;
} desktop_icon_s;
/* Initialize ST7365 panel, start LVGL task, and create desktop UI. */
esp_err_t desktop_app_start(void);
