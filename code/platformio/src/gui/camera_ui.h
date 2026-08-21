#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "lvgl.h"

#define CAMERA_UI_FRAME_WIDTH (320U)
#define CAMERA_UI_FRAME_HEIGHT (240U)

typedef enum
{
	CAMERA_UI_CUT_STYLE_LEFT_TOP = 0,
	CAMERA_UI_CUT_STYLE_CENTER,
	CAMERA_UI_CUT_STYLE_MAX
} camera_ui_cut_style_e;

#define CAMERA_UI_CUT_STYLE_DEFAULT (CAMERA_UI_CUT_STYLE_CENTER)

typedef struct
{
	lv_obj_t *screen;
	lv_obj_t *frame_panel;
	lv_obj_t *img;
	lv_obj_t *status_label;
	lv_timer_t *ui_timer;

	TaskHandle_t capture_task_handle;
	TaskHandle_t input_task_handle;
	volatile bool capture_task_stop;
	volatile bool input_task_stop;

	portMUX_TYPE frame_lock;
	lv_color_t *frame_buf[2];
	uint8_t display_idx;
	uint8_t newest_idx;
	uint8_t frame_ready;
	uint8_t yuv422_order_cfg;
	uint8_t yuv422_order_detected;
	camera_ui_cut_style_e cut_style;
	uint16_t frame_w;
	uint16_t frame_h;
	uint32_t frame_seq;
	uint32_t frame_seq_shown;

	uint8_t camera_start_done;
	uint8_t camera_ready;
	esp_err_t camera_open_ret;

	lv_img_dsc_t img_dsc;
	char status_text[96];
} camera_app_ctx_t;

/* Create camera app screen with fixed BF20A6 configuration and live preview. */
lv_obj_t *camera_create_screen(lv_coord_t lcd_w, lv_coord_t lcd_h);
