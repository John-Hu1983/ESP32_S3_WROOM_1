#pragma once

#include "user/desktop/desktop_app.h"
#include "user/device/bf20a6_cam.h"

#ifdef __cplusplus
extern "C" {
#endif

// clang-format off
#define CAMERA_UI_TASK_STACK_SIZE   (8192U)
#define CAMERA_UI_TASK_PERIOD_MS    (10U)
#define CAMERA_UI_CAPTURE_PERIOD_MS (40U)
#define CAMERA_UI_SYNC_PERIOD_MS    (33U)
#define CAMERA_UI_STATUS_TEXT_LEN   (64U)
#define CAMERA_UI_PREVIEW_MAX_WIDTH (320U)
// clang-format on

typedef struct {
	TaskHandle_t task_handle;
	lv_timer_t* ui_sync_timer;
	ui_menu_home_cb_t home_cb;
	void* home_user_ctx;
	btn_scan_s button_scan;

	lv_obj_t* root;
	lv_obj_t* preview_canvas;
	lv_obj_t* status_label;

	uint16_t preview_w;
	uint16_t preview_h;
	uint16_t* preview_rgb565;

	volatile bool stop_requested;
	bool camera_ready;
	bool frame_dirty;
	bool status_dirty;
	char status_text[CAMERA_UI_STATUS_TEXT_LEN];
} camera_ui_runtime_s;

lv_obj_t* camera_create_screen(lv_obj_t* parent, lv_coord_t area_w, lv_coord_t area_h,
							   ui_menu_home_cb_t home_cb, void* home_user_ctx);
void camera_destroy_screen(lv_obj_t* screen);

#ifdef __cplusplus
}
#endif
