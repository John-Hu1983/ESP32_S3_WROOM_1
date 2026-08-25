#include "desktop_common.h"

#define TAG "desktop_common"

/*
 * brief : Advance LVGL internal tick counter.
 * input : arg - unused callback argument from esp_timer.
 * output: none.
 * type  : public
 */
void desktop_tick_event(void* arg) {
    (void)arg;
    lv_tick_inc(DESKTOP_COMMON_LVGL_TICK_PERIOD_MS);
}

/*
 * brief : Forward LVGL flush area to panel driver and notify flush completion.
 * input : disp - LVGL display instance; area - dirty rectangle; px_map - source pixel buffer.
 * output: none.
 * type  : public
 */
void desktop_flush_event(lv_display_t* disp, const lv_area_t* area, uint8_t* px_map) {
    esp_err_t ret = st7365p_lvgl_flush(area->x1, area->y1, area->x2, area->y2, px_map);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "st7365p_lvgl_flush failed: %d", (int)ret);
    }

    lv_display_flush_ready(disp);
}

/*
 * brief : Convert one color to its inverted RGB counterpart.
 * input : color - source LVGL color value.
 * output: Inverted LVGL color.
 * type  : public
 */
lv_color_t desktop_invert_color(lv_color_t color) {
    lv_color32_t color32 = lv_color_to_32(color, LV_OPA_COVER);

    return lv_color_make((uint8_t)(0xFFU - color32.red),
                         (uint8_t)(0xFFU - color32.green),
                         (uint8_t)(0xFFU - color32.blue));
}
