#pragma once

#include "user/desktop/desktop_app.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ABOUT_TASK_STACK_SIZE (6144U)
#define ABOUT_TASK_PERIOD_MS (100U)
#define ABOUT_REFRESH_PERIOD_MS (1000U)

#define ABOUT_INFO_TEXT_LEN (64U)
#define ABOUT_TASKLIST_MAX_ROWS (24U)
#define ABOUT_TASK_NAME_LEN (16U)

typedef struct {
    char name[ABOUT_TASK_NAME_LEN];
    char state[2];
    uint8_t priority;
    uint16_t stack_high_watermark;
} about_task_row_s;

typedef struct {
    TaskHandle_t task_handle;
    ui_menu_home_cb_t home_cb;
    void* home_user_ctx;
    btn_scan_s button_scan;

    lv_obj_t* root;
    lv_obj_t* idf_value_label;
    lv_obj_t* cpu_value_label;
    lv_obj_t* ram_value_label;
    lv_obj_t* psram_value_label;
    lv_obj_t* tasklist_table;

    bool dirty;
    char idf_text[ABOUT_INFO_TEXT_LEN];
    char cpu_text[ABOUT_INFO_TEXT_LEN];
    char ram_text[ABOUT_INFO_TEXT_LEN];
    char psram_text[ABOUT_INFO_TEXT_LEN];
    uint16_t task_row_count;
    about_task_row_s task_rows[ABOUT_TASKLIST_MAX_ROWS];
} about_ui_runtime_s;

lv_obj_t* about_create_screen(lv_obj_t* parent, lv_coord_t area_w, lv_coord_t area_h,
                              ui_menu_home_cb_t home_cb, void* home_user_ctx);

void about_destroy_screen(lv_obj_t* screen);

#ifdef __cplusplus
}
#endif
