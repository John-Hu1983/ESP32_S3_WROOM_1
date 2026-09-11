#pragma once

#include "user/desktop/desktop_app.h"

#ifdef __cplusplus
extern "C" {
#endif

// clang-format off
#define FILE_UI_TASK_STACK_SIZE (4096U)
#define FILE_UI_TASK_PERIOD_MS  (10U)
// clang-format on

typedef struct {
	TaskHandle_t task_handle;
	ui_menu_home_cb_t home_cb;
	void* home_user_ctx;
	btn_scan_s button_scan;
} file_ui_runtime_s;

lv_obj_t* file_open_screen(lv_obj_t* parent, lv_coord_t area_w, lv_coord_t area_h,
							 ui_menu_home_cb_t home_cb, void* home_user_ctx);
void file_close_screen(lv_obj_t* screen);

#ifdef __cplusplus
}
#endif
