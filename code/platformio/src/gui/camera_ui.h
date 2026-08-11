#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "esp_camera.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_heap_caps.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "lvgl.h"

#include "desktop/desktop_app.h"
#include "bsp/delay.h"
#include "peripherals/gpba02b.h"
#include "peripherals/keyboard.h"
#ifdef CAMERA_OBJECT
#include "peripherals/ov2640.h"
#endif
#include "service/system_service.h"
#include "user_config.h"

#define CAMERA_MARGIN_X 0
#define CAMERA_INPUT_SCAN_PERIOD_MS 10U
#define CAMERA_INPUT_TASK_STACK_SIZE 4096U
#define CAMERA_INPUT_TASK_PRIORITY 4U
#define CAMERA_PREVIEW_PERIOD_MS 40U

typedef struct
{
	lv_obj_t *screen;
	lv_obj_t *view;
	lv_obj_t *img;
	lv_obj_t *hint_label;
	lv_timer_t *preview_timer;
	lv_img_dsc_t frame_dsc;
	uint8_t *frame_buf;
	size_t frame_buf_size;
	TaskHandle_t worker_task_handle;
	volatile bool worker_stop;
	volatile bool worker_busy;
	volatile bool worker_done;
	volatile bool worker_is_recovery;
	volatile esp_err_t worker_ret;
	uint8_t frame_timeout_streak;
	bool preview_started;
	bool camera_started;
} camera_app_ctx_t;

lv_obj_t *camera_create_screen(lv_coord_t lcd_w, lv_coord_t lcd_h);
