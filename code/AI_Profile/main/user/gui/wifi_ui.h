#pragma once

#include "user/desktop/desktop_app.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WIFI_UI_TASK_STACK_SIZE (4096U)
#define WIFI_UI_TASK_PERIOD_MS (10U)

typedef struct {
	TaskHandle_t task_handle;
	ui_menu_home_cb_t home_cb;
	void* home_user_ctx;
	btn_scan_s button_scan;
} wifi_ui_runtime_s;

lv_obj_t* wifi_create_screen(lv_obj_t* parent, lv_coord_t area_w, lv_coord_t area_h,
							 ui_menu_home_cb_t home_cb, void* home_user_ctx);
void wifi_destroy_screen(lv_obj_t* screen);

#ifdef __cplusplus
}
#endif
