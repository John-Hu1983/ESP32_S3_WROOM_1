#include "mic_ui.h"

#define TAG "mic_ui"

static mic_ui_runtime_s s_mic_runtime;

static void _mic_ui_task(void* param) {
    mic_ui_runtime_s* runtime = (mic_ui_runtime_s*)param;

    while (1) {
        btn_status_e btn_val = button_scan_state(&runtime->button_scan, MIC_UI_TASK_PERIOD_MS);
        if ((btn_val == Btn_Both_Click) && (runtime->home_cb != NULL)) {
            runtime->home_cb(runtime->home_user_ctx);
        }
        delay_ms(MIC_UI_TASK_PERIOD_MS);
    }
}

/*
 * brief : Create the mic page root screen object.
 * input : lcd_w - LCD width; lcd_h - LCD height.
 * output: Created LVGL screen object.
 * type  : public
 */
lv_obj_t* mic_create_screen(lv_obj_t* parent, lv_coord_t area_w, lv_coord_t area_h,
                            ui_menu_home_cb_t home_cb, void* home_user_ctx) {
    if (parent == NULL) {
        return NULL;
    }

    if (s_mic_runtime.task_handle != NULL) {
        vTaskDelete(s_mic_runtime.task_handle);
        s_mic_runtime.task_handle = NULL;
    }

    s_mic_runtime.home_cb = home_cb;
    s_mic_runtime.home_user_ctx = home_user_ctx;
    s_mic_runtime.button_scan = (btn_scan_s){0};

    lv_obj_t* screen = lv_obj_create(parent);
    lv_obj_set_size(screen, area_w, area_h);
    lv_obj_set_pos(screen, 0, 0);
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x1C1B1A), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(screen, 0, 0);
    lv_obj_set_style_radius(screen, 0, 0);
    lv_obj_set_style_pad_all(screen, 0, 0);

    BaseType_t task_ok =
        xTaskCreate(_mic_ui_task, "mic_ui", MIC_UI_TASK_STACK_SIZE, &s_mic_runtime, 5,
                    &s_mic_runtime.task_handle);
    if (task_ok != pdPASS) {
        s_mic_runtime.task_handle = NULL;
        lv_obj_del(screen);
        ESP_LOGE(TAG, "xTaskCreate failed");
        return NULL;
    }

    return screen;
}

void mic_destroy_screen(lv_obj_t* screen) {
    if (s_mic_runtime.task_handle != NULL) {
        vTaskDelete(s_mic_runtime.task_handle);
        s_mic_runtime.task_handle = NULL;
    }

    s_mic_runtime.home_cb = NULL;
    s_mic_runtime.home_user_ctx = NULL;
    s_mic_runtime.button_scan = (btn_scan_s){0};

    if ((screen != NULL) && lv_obj_is_valid(screen)) {
        lv_obj_del(screen);
    }
}
