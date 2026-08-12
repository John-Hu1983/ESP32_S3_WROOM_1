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
 
#include "../gui/pidm_ui.h"
#include "../gui/mic_ui.h"
#include "../gui/bt_ui.h"
#include "../gui/camera_ui.h"
#include "../gui/gallery_ui.h"
#include "../gui/music_ui.h"
#include "../service/system_service.h"
#include "peripherals/ht517.h"
#include "peripherals/st7365p.h"
#include "../gui/oscilloscope_ui.h"
#include "../gui/power_ui.h"
#include "../gui/file_ui.h"
#include "../gui/setting_ui.h"
#include "../gui/tools_ui.h"
#include "../gui/wifi_ui.h"

#define LVGL_TICK_PERIOD_MS (DESKTOP_COMMON_LVGL_TICK_PERIOD_MS)
#define LVGL_TASK_PERIOD_MS 10U
#define LVGL_DRAW_BUF_LINES 72U

#define DESKTOP_ICON_COLS 3U
#define DESKTOP_ICON_ROWS 4U
#define DESKTOP_ICON_COUNT (DESKTOP_ICON_COLS * DESKTOP_ICON_ROWS)

#define DESKTOP_MARGIN_X 12
#define DESKTOP_BAR_HEIGHT 24
#define DESKTOP_GRID_TOP_GAP 8
#define DESKTOP_GRID_BOTTOM_GAP 8
#define DESKTOP_ICON_GAP_X 10
#define DESKTOP_ICON_GAP_Y 10

/* Desktop icon colors */
#define IC_CAM LV_COLOR_MAKE(0xE9, 0x54, 0x20)
#define IC_GAL LV_COLOR_MAKE(0xD9, 0x4B, 0x3D)
#define IC_MUS LV_COLOR_MAKE(0x77, 0x21, 0x6F)
#define IC_SCP LV_COLOR_MAKE(0xF2, 0x7C, 0x38)
#define IC_WIF LV_COLOR_MAKE(0xC0, 0x56, 0x3F)
#define IC_BT LV_COLOR_MAKE(0xB6, 0x5C, 0x2C)
#define IC_FIL LV_COLOR_MAKE(0xE1, 0x9A, 0x35)
#define IC_MIC LV_COLOR_MAKE(0x8F, 0x67, 0x45)
#define IC_PIDM LV_COLOR_MAKE(0xC2, 0x3B, 0x4A)
#define IC_TLS LV_COLOR_MAKE(0x8A, 0x3D, 0x5D)
#define IC_SET LV_COLOR_MAKE(0xA8, 0x70, 0x3A)
#define IC_PWR LV_COLOR_MAKE(0x6F, 0x4A, 0x34)

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

typedef lv_obj_t *(*desktop_create_ui)(lv_coord_t lcd_w, lv_coord_t lcd_h);

typedef struct
{
    const char *symbol;
    const char *name;
    lv_color_t color;
    desktop_create_ui create_screen_cb;
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
void desktop_return_to_home(void);
