#pragma once

#include "user/desktop/desktop_app.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MIC_UI_TASK_STACK_SIZE (4096U)
#define MIC_UI_TASK_PERIOD_MS (10U)

typedef struct {
	TaskHandle_t task_handle;
	ui_menu_home_cb_t home_cb;
	void* home_user_ctx;
	btn_scan_s button_scan;
} mic_ui_runtime_s;

/*
 * brief : Create the mic page root screen object.
 * input : lcd_w - LCD width; lcd_h - LCD height.
 * output: Created LVGL screen object.
 * type  : public
 */
lv_obj_t* mic_create_screen(lv_obj_t* parent, lv_coord_t area_w, lv_coord_t area_h,
							ui_menu_home_cb_t home_cb, void* home_user_ctx);
void mic_destroy_screen(lv_obj_t* screen);

#ifdef __cplusplus
}
#endif
