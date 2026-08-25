#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "lvgl.h"

#include "desktop_common.h"
#include "user/inc/user_config.h"

#ifdef __cplusplus
extern "C" {
#endif

#define LVGL_TICK_PERIOD_MS (DESKTOP_COMMON_LVGL_TICK_PERIOD_MS)
#define LVGL_TASK_PERIOD_MS (10U)
#define LVGL_DRAW_BUF_LINES (72U)

#define DESKTOP_APP_TAG "desktop"

#define DESKTOP_TOP_BAR_HEIGHT (26U)
#define DESKTOP_BOTTOM_BAR_HEIGHT (26U)
#define DESKTOP_TOOLBAR_COLOR_HEX (0x87CEEBU)

#define DESKTOP_ICON_COLS (3U)
#define DESKTOP_ICON_ROWS (4U)
#define DESKTOP_ICON_COUNT (DESKTOP_ICON_COLS * DESKTOP_ICON_ROWS)

#define DESKTOP_MARGIN_X (4U)
#define DESKTOP_MARGIN_Y (4U)
#define DESKTOP_ICON_GAP_X (8U)
#define DESKTOP_ICON_GAP_Y (8U)

#ifndef DESKTOP_TEXT_FONT
#define DESKTOP_TEXT_FONT BUILTIN_TEXT_FONT
#endif

#ifndef DESKTOP_SYMBOL_FONT
#define DESKTOP_SYMBOL_FONT BUILTIN_ICON_FONT
#endif

typedef struct {
    const char* symbol;
    const char* name;
    uint32_t color_hex;
} desktop_icon_s;

/*
 * brief : Initialize ST7365 panel, start LVGL task, and create desktop UI.
 * input : none.
 * output: ESP_OK on success; otherwise error code.
 * type  : public
 */
esp_err_t desktop_start(void);
/*
 * brief : Return to desktop main screen.
 * input : none.
 * output: none.
 * type  : public
 */
void desktop_return_to_home(void);
/*
 * brief : Get left status label handle in the desktop top bar.
 * input : none.
 * output: LVGL object handle or NULL.
 * type  : public
 */
lv_obj_t* desktop_get_cpu_label(void);
/*
 * brief : Get network icon label handle in the desktop top bar.
 * input : none.
 * output: LVGL object handle or NULL.
 * type  : public
 */
lv_obj_t* desktop_get_net_label(void);

#ifdef __cplusplus
}
#endif
