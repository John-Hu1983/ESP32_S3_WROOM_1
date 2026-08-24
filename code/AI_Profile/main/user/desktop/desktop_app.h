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

#ifdef __cplusplus
extern "C" {
#endif

#define LVGL_TICK_PERIOD_MS (DESKTOP_COMMON_LVGL_TICK_PERIOD_MS)
#define LVGL_TASK_PERIOD_MS (10U)
#define LVGL_DRAW_BUF_LINES (72U)

#define DESKTOP_ICON_COLS (3U)
#define DESKTOP_ICON_ROWS (4U)
#define DESKTOP_ICON_COUNT (DESKTOP_ICON_COLS * DESKTOP_ICON_ROWS)

/* Initialize ST7365 panel, start LVGL task, and create desktop UI. */
esp_err_t desktop_app_start(void);
/* Return to desktop main screen. */
void desktop_return_to_home(void);

#ifdef __cplusplus
}
#endif
