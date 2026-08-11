#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "bsp/delay.h"
#include "desktop/desktop_app.h"
#include "lvgl.h"
#include "peripherals/keyboard.h"
#include "peripherals/pidm_det.h"
#include "service/system_service.h"
#include "voice/voice_common.h"

#define PIDM_MARGIN_X 2
#define PIDM_LAYOUT_GAP 4

#define PIDM_INPUT_SCAN_PERIOD_MS 10U
#define PIDM_INPUT_TASK_STACK_SIZE 4096U
#define PIDM_INPUT_TASK_PRIORITY 4U

#define PIDM_UPDATE_PERIOD_MS 100U
#define PIDM_PROBE_PULSE_US 100U
#define PIDM_PROBE_PERIOD_MS 300U
#define PIDM_PROBE_SCAN_PERIOD_MS 10U
#define PIDM_PROBE_TASK_STACK_SIZE 4096U
#define PIDM_PROBE_TASK_PRIORITY 4U

#define PIDM_BEEP_INTERVAL_MIN_MS 100U
#define PIDM_BEEP_INTERVAL_MAX_MS 2000U

#define PIDM_WAVE_POINT_COUNT 96U
#define PIDM_TEXTEDIT_COUNT 9U

typedef struct
{
	lv_obj_t *screen;
	lv_obj_t *chart_dpk;
	lv_obj_t *chart_slope;
	lv_chart_series_t *series_dpk;
	lv_chart_series_t *series_slope;
	lv_obj_t *text_edits[PIDM_TEXTEDIT_COUNT];
	lv_timer_t *update_timer;
	int16_t dpk_points[PIDM_WAVE_POINT_COUNT];
	int16_t slope_points[PIDM_WAVE_POINT_COUNT];
	pidm_det_feature_s latest_feature;
	esp_err_t latest_probe_ret;
	uint32_t feature_seq;
	uint32_t rendered_feature_seq;
	uint8_t latest_feature_valid;
	char di_ogg_path[USER_FS_PATH_MAX_LEN];
	int64_t last_beep_ts_ms;
	uint8_t beep_level;
} pidm_app_ctx_t;

/* Create PIDM monitor app screen and runtime resources. */
lv_obj_t *pidm_create_screen(lv_coord_t lcd_w, lv_coord_t lcd_h);
/* Request return-to-home navigation from PIDM app. */
void pidm_destroy_and_return(void);
