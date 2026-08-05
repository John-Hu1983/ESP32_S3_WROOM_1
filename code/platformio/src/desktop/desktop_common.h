#pragma once

#include "esp_log.h"
#include "lvgl.h"

#include "peripherals/st7365p.h"

#define DESKTOP_COMMON_LVGL_TICK_PERIOD_MS (10U)

/* Advance LVGL internal time base by one desktop tick period. */
void desktop_common_lvgl_tick_cb(void *arg);
/* Flush one LVGL dirty area to the ST7365 panel driver. */
void desktop_common_lvgl_flush_cb(lv_disp_drv_t *disp_drv, const lv_area_t *area, lv_color_t *color_p);
/* Generate a high-contrast inverse color for selected-state rendering. */
lv_color_t desktop_common_invert_color(lv_color_t color);
