#pragma once

#include <stdio.h>
#include <string.h>

#include "esp_heap_caps.h"

#include "user/desktop/desktop_app.h"
#include "user/device/dev_pidm.h"

#ifdef __cplusplus
extern "C" {
#endif

// clang-format off
#define PIDM_UI_TASK_STACK_SIZE       (4096U)
#define PIDM_UI_TASK_PERIOD_MS        (10U)
#define PIDM_UI_REFRESH_PERIOD_MS     (50U)
#define PIDM_UI_DETECTION_PERIOD_MS   (100U)
#define PIDM_UI_CARD_GAP_PX           (6)
#define PIDM_UI_CARD_RADIUS_PX        (6)
#define PIDM_UI_SCOPE_GRID_H_LINE_CNT (4U)
#define PIDM_UI_SCOPE_GRID_V_LINE_CNT (8U)
#define PIDM_UI_SCOPE_POINT_COUNT     (96U)
#define PIDM_UI_SCOPE_Y_MIN_ADC       (0)
#define PIDM_UI_SCOPE_Y_MAX_ADC       (4095)
// clang-format on

#if CONFIG_LV_FONT_MONTSERRAT_12
#define PIDM_UI_TEXT_FONT lv_font_montserrat_12
#else
#define PIDM_UI_TEXT_FONT DESKTOP_TEXT_FONT
#endif

typedef struct {
	TaskHandle_t task_handle;
	lv_timer_t* ui_sync_timer;
	ui_menu_home_cb_t home_cb;
	void* home_user_ctx;
	btn_scan_s button_scan;

	lv_obj_t* latest_edit;
	lv_obj_t* average_edit;
	lv_obj_t* samples_edit;
	lv_obj_t* triggers_edit;
	lv_obj_t* pulse_edit;
	lv_obj_t* status_edit;

#if LV_USE_LINE != 0
	lv_obj_t* scope_line;
	lv_coord_t scope_plot_w;
	lv_coord_t scope_plot_h;
#endif
} pidm_ui_runtime_s;

lv_obj_t* pidm_open_screen(lv_obj_t* parent,
						   lv_coord_t area_w,
						   lv_coord_t area_h,
						   ui_menu_home_cb_t home_cb,
						   void* home_user_ctx);
void pidm_close_screen(lv_obj_t* screen);

#ifdef __cplusplus
}
#endif
