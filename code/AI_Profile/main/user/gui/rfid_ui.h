#pragma once

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "user/desktop/desktop_app.h"
#include "user/device/dev_rfid.h"

#ifdef __cplusplus
extern "C" {
#endif

// clang-format off
#define RFID_UI_TASK_STACK_SIZE (7168U)
#define RFID_UI_TASK_PERIOD_MS  (80U)
#define RFID_UI_POLL_PERIOD_MS  (240U)
#define RFID_UI_RETRY_INIT_MS   (1200U)

#define RFID_UI_STATUS_TEXT_LEN (DEV_RFID_STATUS_TEXT_LEN)
#define RFID_UI_UID_TEXT_LEN    (DEV_RFID_UID_TEXT_LEN)
#define RFID_UI_DUMP_TEXT_LEN   (DEV_RFID_DUMP_TEXT_LEN)
// clang-format on

typedef struct {
    TaskHandle_t task_handle;
    lv_timer_t* ui_sync_timer;
    ui_menu_home_cb_t home_cb;
    void* home_user_ctx;
    btn_scan_s button_scan;

    lv_obj_t* root;
    lv_obj_t* status_label;
    lv_obj_t* uid_label;
    lv_obj_t* sector_label;
    lv_obj_t* state_indicator;
    lv_obj_t* dump_panel;
    lv_obj_t* dump_label;

    bool reader_ready;
    bool card_present;
    bool card_error;
    bool dirty;
    bool dump_need_scroll_top;
    uint8_t reader_version;
    uint8_t selected_sector;

    char status_text[RFID_UI_STATUS_TEXT_LEN];
    char uid_text[RFID_UI_UID_TEXT_LEN];
    char dump_text[RFID_UI_DUMP_TEXT_LEN];
} rfid_ui_runtime_s;

lv_obj_t* rfid_create_screen(
    lv_obj_t* parent,
    lv_coord_t area_w,
    lv_coord_t area_h,
    ui_menu_home_cb_t home_cb,
    void* home_user_ctx
);
void rfid_destroy_screen(lv_obj_t* screen);

#ifdef __cplusplus
}
#endif
