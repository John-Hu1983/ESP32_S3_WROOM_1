#include "desktop_common.h"

#define TAG "DESKTOP_COMMON"

/*
 * brief: Advance LVGL internal tick counter.
 * input: arg - unused callback argument from esp_timer.
 * output: None.
 */
void desktop_common_lvgl_tick_cb(void *arg)
{
    (void)arg;
    lv_tick_inc(DESKTOP_COMMON_LVGL_TICK_PERIOD_MS);
}

/*
 * brief: Forward LVGL flush area to panel driver and notify flush completion.
 * input: disp_drv - LVGL display driver instance; area - dirty rectangle; color_p - source pixel buffer.
 * output: None.
 */
void desktop_common_lvgl_flush_cb(lv_disp_drv_t *disp_drv, const lv_area_t *area, lv_color_t *color_p)
{
    esp_err_t ret;

    ret = st7365p_lvgl_flush(area->x1, area->y1, area->x2, area->y2, color_p);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "st7365p_lvgl_flush failed: %d", (int)ret);
    }

    lv_disp_flush_ready(disp_drv);
}

/*
 * brief: Convert one color to its inverted RGB counterpart.
 * input: color - source LVGL color value.
 * output: Inverted LVGL color.
 */
lv_color_t desktop_common_invert_color(lv_color_t color)
{
    lv_color32_t color32;

    color32.full = lv_color_to32(color);

    return lv_color_make((uint8_t)(0xFFU - color32.ch.red),
                         (uint8_t)(0xFFU - color32.ch.green),
                         (uint8_t)(0xFFU - color32.ch.blue));
}
