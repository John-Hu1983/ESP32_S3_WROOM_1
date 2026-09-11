#include "bt_ui.h"

#include "user/communication/user_ble.h"

#define TAG "bt_ui"

static bt_ui_runtime_s s_bt_runtime;

/*
 * brief : _bt_ble_rx_cb.
 * input : see parameters.
 * output: none.
 * type  : private
 */
static void _bt_ble_rx_cb(const uint8_t* data, uint16_t len, void* user_ctx) {
    char text[49];
    char msg[96];
    uint16_t copy_len = len;

    (void)user_ctx;

    if (data == NULL) {
        return;
    }

    if (copy_len > (uint16_t)(sizeof(text) - 1U)) {
        copy_len = (uint16_t)(sizeof(text) - 1U);
    }

    memcpy(text, data, copy_len);
    text[copy_len] = '\0';

    snprintf(msg, sizeof(msg), "BLE RX: %s", text);
    desktop_post_message(msg);
    ESP_LOGI(TAG, "BLE RX len=%u", (unsigned)len);
}

/*
 * brief : _bt_ui_task.
 * input : see parameters.
 * output: none.
 * type  : private
 */
static void _bt_ui_task(void* param) {
    bt_ui_runtime_s* runtime = (bt_ui_runtime_s*)param;

    while (1) {
        btn_status_e btn_val = button_scan_state(&runtime->button_scan, BT_UI_TASK_PERIOD_MS);
        if ((btn_val == Btn_Both_Click) && (runtime->home_cb != NULL)) {
            runtime->home_cb(runtime->home_user_ctx);
        }
        delay_ms(BT_UI_TASK_PERIOD_MS);
    }
}

/*
 * brief : Create the Bluetooth page root screen object.
 * input : lcd_w - LCD width; lcd_h - LCD height.
 * output: Created LVGL screen object.
 * type  : public
 */
lv_obj_t* bt_open_screen(lv_obj_t* parent, lv_coord_t area_w, lv_coord_t area_h,
                           ui_menu_home_cb_t home_cb, void* home_user_ctx) {
    esp_err_t ble_ret = ESP_OK;
    lv_obj_t* title = NULL;
    lv_obj_t* info = NULL;

    if (parent == NULL) {
        return NULL;
    }

    if (s_bt_runtime.task_handle != NULL) {
        vTaskDelete(s_bt_runtime.task_handle);
        s_bt_runtime.task_handle = NULL;
    }

    s_bt_runtime.home_cb = home_cb;
    s_bt_runtime.home_user_ctx = home_user_ctx;
    s_bt_runtime.button_scan = (btn_scan_s){0};

    lv_obj_t* screen = lv_obj_create(parent);
    lv_obj_set_size(screen, area_w, area_h);
    lv_obj_set_pos(screen, 0, 0);
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x1C1B1A), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(screen, 0, 0);
    lv_obj_set_style_radius(screen, 0, 0);
    lv_obj_set_style_pad_all(screen, 0, 0);

    title = lv_label_create(screen);
    lv_label_set_text(title, "BLE Test Mode");
    lv_obj_set_style_text_color(title, lv_color_hex(0xE8D7C1), 0);
    lv_obj_set_style_text_font(title, &DESKTOP_TEXT_FONT, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 12);

    info = lv_label_create(screen);
    lv_label_set_text_fmt(
        info,
        "Name: %s\nService: %s\nRX: %s\nTX: %s\nSend PING from PC to test.",
        USER_BLE_DEVICE_NAME,
        USER_BLE_SERVICE_UUID,
        USER_BLE_RX_CHAR_UUID,
        USER_BLE_TX_CHAR_UUID
    );
    lv_label_set_long_mode(info, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(info, area_w - 20);
    lv_obj_set_style_text_color(info, lv_color_hex(0xCFCFCF), 0);
    lv_obj_set_style_text_font(info, &DESKTOP_TEXT_FONT, 0);
    lv_obj_align(info, LV_ALIGN_TOP_LEFT, 10, 42);

    user_ble_set_rx_callback(_bt_ble_rx_cb, NULL);
    ble_ret = user_ble_start();
    if (ble_ret != ESP_OK) {
        desktop_post_message("BLE start failed, please enable BT/NimBLE in sdkconfig.");
        ESP_LOGE(TAG, "user_ble_start failed: %s", esp_err_to_name(ble_ret));
    }
    else {
        desktop_post_message("BLE ready, open PC test tool and connect.");
    }

    BaseType_t task_ok = xTaskCreate(_bt_ui_task, "bt_ui", BT_UI_TASK_STACK_SIZE, &s_bt_runtime,
                                     5, &s_bt_runtime.task_handle);
    if (task_ok != pdPASS) {
        s_bt_runtime.task_handle = NULL;
        lv_obj_del(screen);
        ESP_LOGE(TAG, "xTaskCreate failed");
        return NULL;
    }

    return screen;
}

/*
 * brief : bt_close_screen.
 * input : see parameters.
 * output: none.
 * type  : public
 */
void bt_close_screen(lv_obj_t* screen) {
    if (s_bt_runtime.task_handle != NULL) {
        vTaskDelete(s_bt_runtime.task_handle);
        s_bt_runtime.task_handle = NULL;
    }

    user_ble_set_rx_callback(NULL, NULL);
    user_ble_stop();

    s_bt_runtime.home_cb = NULL;
    s_bt_runtime.home_user_ctx = NULL;
    s_bt_runtime.button_scan = (btn_scan_s){0};

    if ((screen != NULL) && lv_obj_is_valid(screen)) {
        lv_obj_del(screen);
    }
}
