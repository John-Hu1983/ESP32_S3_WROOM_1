#pragma once

#include "user/desktop/desktop_app.h"

#ifdef __cplusplus
extern "C" {
#endif

// clang-format off
#define PRINT_UI_TASK_STACK_SIZE   (6144U)
#define PRINT_UI_TASK_PERIOD_MS    (80U)
#define PRINT_UI_STATUS_PERIOD_MS  (1000U)
#define PRINT_UI_RETRY_INIT_MS     (2000U)

#define PRINT_UI_CMD_COUNT         (5U)
#define PRINT_UI_TEXT_LEN          (96U)
// clang-format on

typedef struct {
    TaskHandle_t task_handle;
    lv_timer_t* ui_sync_timer;
    ui_menu_home_cb_t home_cb;
    void* home_user_ctx;
    btn_scan_s button_scan;

    lv_obj_t* root;
    lv_obj_t* info_table;

    bool printer_ready;
    bool status_valid;
    bool dirty;
    uint8_t selected_cmd;
    uint8_t cmd_count;
    uint8_t temperature_celsius;
    uint16_t paper_detect_raw;
    uint16_t working_voltage_raw;
    esp_err_t last_status_err;
    esp_err_t last_cmd_err;

    char status_text[PRINT_UI_TEXT_LEN];
    char action_text[PRINT_UI_TEXT_LEN];
} print_ui_runtime_s;

lv_obj_t* print_create_screen(lv_obj_t* parent, lv_coord_t area_w, lv_coord_t area_h,
                              ui_menu_home_cb_t home_cb, void* home_user_ctx);
void print_destroy_screen(lv_obj_t* screen);

#ifdef __cplusplus
}
#endif