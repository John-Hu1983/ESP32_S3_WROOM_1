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

#define PRINT_UI_CMD_COUNT         (6U)
#define PRINT_UI_CMD_GRID_COLS     (2U)
#define PRINT_UI_CMD_GRID_ROWS     (3U)
#define PRINT_UI_CMD_GRID_SLOTS    (PRINT_UI_CMD_GRID_COLS * PRINT_UI_CMD_GRID_ROWS)
#define PRINT_UI_TEXT_LEN          (96U)
// clang-format on

typedef esp_err_t (*print_ui_cmd_fn_t)(void);

typedef struct {
    const char* symbol;
    const char* name;
    uint32_t color_hex;
    print_ui_cmd_fn_t exec;
} print_ui_cmd_s;

typedef struct {
    TaskHandle_t task_handle;
    lv_timer_t* ui_sync_timer;
    ui_menu_home_cb_t home_cb;
    void* home_user_ctx;
    btn_scan_s button_scan;

    lv_obj_t* root;
    lv_obj_t* temp_value_label;
    lv_obj_t* paper_value_label;
    lv_obj_t* voltage_value_label;
    lv_obj_t* status_title_label;
    lv_obj_t* status_detail_label;
    lv_obj_t* action_label;
    lv_obj_t* command_btn[PRINT_UI_CMD_COUNT];
    lv_obj_t* command_symbol_label[PRINT_UI_CMD_COUNT];
    lv_obj_t* command_name_label[PRINT_UI_CMD_COUNT];

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

#define AUTO_PRINT_INTERVAL_MS    (5000U)
typedef struct {
    bool en;
    uint16_t interval_ms;

} print_auto_test_s;

lv_obj_t* print_create_screen(
    lv_obj_t* parent,
    lv_coord_t area_w,
    lv_coord_t area_h,
    ui_menu_home_cb_t home_cb,
    void* home_user_ctx
);
void print_destroy_screen(lv_obj_t* screen);

#ifdef __cplusplus
}
#endif