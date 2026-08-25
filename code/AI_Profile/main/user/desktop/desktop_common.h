#pragma once

#include "esp_log.h"
#include "lvgl.h"

#include "user/peripherals/st7365p.h"

#ifdef __cplusplus
extern "C" {
#endif

#define DESKTOP_COMMON_LVGL_TICK_PERIOD_MS (10U)

#define RGB565_BLACK (0x0000U)
#define RGB565_WHITE (0xFFFFU)
#define RGB565_RED (0xF800U)
#define RGB565_GREEN (0x07E0U)
#define RGB565_BLUE (0x001FU)
#define RGB565_YELLOW (0xFFE0U)
#define RGB565_CYAN (0x07FFU)
#define RGB565_MAGENTA (0xF81FU)
#define RGB565_ORANGE (0xFD20U)
#define RGB565_GRAY (0x8410U)

/* Advance LVGL internal time base by one desktop tick period. */
void desktop_common_lvgl_tick_cb(void* arg);
/* Flush one LVGL dirty area to the ST7365 panel driver. */
void desktop_common_lvgl_flush_cb(lv_display_t* disp, const lv_area_t* area, uint8_t* px_map);
/* Generate a high-contrast inverse color for selected-state rendering. */
lv_color_t desktop_common_invert_color(lv_color_t color);

#ifdef __cplusplus
}
#endif
