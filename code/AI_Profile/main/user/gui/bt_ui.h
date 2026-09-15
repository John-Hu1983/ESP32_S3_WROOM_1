#pragma once

#include "user/desktop/desktop_app.h"

#ifdef __cplusplus
extern "C" {
#endif

// clang-format off
#define BT_UI_TASK_STACK_SIZE      	(4096U)
#define BT_UI_TASK_PERIOD_MS       	(10U)
#define BT_UI_TASK_PRIO            	(3U)
#define BT_UI_DEBUG_REFRESH_MS     	(200U)
#define BT_UI_DEBUG_POP_BATCH      	(8U)
#define BT_UI_IO_LINE_COUNT        	(8U)
#define BT_UI_IO_LINE_LEN          	(64U)
#define BT_UI_PARAM_TEXT_LEN       	(256U)
#define BT_UI_IO_TEXT_LEN          	((BT_UI_IO_LINE_COUNT * BT_UI_IO_LINE_LEN) + BT_UI_IO_LINE_COUNT)

#define BT_UI_BG_COLOR        		(0x000000U)
#define BT_UI_TEXT_COLOR      		(0xFFFFFFU)
#define BT_UI_BORDER_COLOR    		(0x7A7A7AU)
#define BT_UI_TITLE_COLOR     		(0xE8D7C1U)
#define BT_UI_LABEL_HEIGHT    		(14U)
#define BT_UI_LABEL_TEXT_GAP  		(2U)
#define BT_UI_SECTION_MARGIN  		(8U)
#define BT_UI_SECTION_GAP     		(6U)
#define BT_UI_TEXT_PAD        		(4U)
// clang-format on

typedef struct {
	TaskHandle_t task_handle;
	lv_timer_t* ui_sync_timer;
	ui_menu_home_cb_t home_cb;
	void* home_user_ctx;
	btn_scan_s button_scan;
	lv_obj_t* param_edit;
	lv_obj_t* rx_edit;
	lv_obj_t* tx_edit;
	uint32_t debug_refresh_elapsed_ms;
	bool param_dirty;
	bool rx_dirty;
	bool tx_dirty;
	char param_text[BT_UI_PARAM_TEXT_LEN];
	char rx_lines[BT_UI_IO_LINE_COUNT][BT_UI_IO_LINE_LEN];
	char tx_lines[BT_UI_IO_LINE_COUNT][BT_UI_IO_LINE_LEN];
	char rx_text[BT_UI_IO_TEXT_LEN];
	char tx_text[BT_UI_IO_TEXT_LEN];
} bt_ui_runtime_s;

lv_obj_t* bt_open_screen(lv_obj_t* parent, lv_coord_t area_w, lv_coord_t area_h,
						   ui_menu_home_cb_t home_cb, void* home_user_ctx);
void bt_close_screen(lv_obj_t* screen);

#ifdef __cplusplus
}
#endif
