#pragma once

#include "service/desktop.h"

#include <stdint.h>

#define UI_PIDM_LOG_TAG "UiPidm"

#define PIDM_UI_POINT_COUNT 96
#define PIDM_UI_TEXT_COUNT 9
#define PIDM_UI_REFRESH_MS 220
#define PIDM_UI_MARGIN 3
#define PIDM_UI_GAP 4

#define PIDM_UI_BG_HEX 0x111418
#define PIDM_UI_SURFACE_HEX 0x1C232B
#define PIDM_UI_BORDER_HEX 0x3A4552
#define PIDM_UI_TEXT_HEX 0xE8EEF5
#define PIDM_UI_TEXT_SECONDARY_HEX 0xA5B2C0
#define PIDM_UI_ALERT_HEX 0xFF5630
#define PIDM_UI_DPK_HEX 0xE95420
#define PIDM_UI_SLOPE_HEX 0x2FB5E2

typedef struct {
	lv_obj_t* panel;
	lv_obj_t* plot_dpk;
	lv_obj_t* plot_slope;
	lv_obj_t* wave_dpk;
	lv_obj_t* wave_slope;
	lv_obj_t* text_edits[PIDM_UI_TEXT_COUNT];
	lv_timer_t* timer;
	int16_t dpk_points[PIDM_UI_POINT_COUNT];
	int16_t slope_points[PIDM_UI_POINT_COUNT];
	lv_point_precise_t dpk_line_points[PIDM_UI_POINT_COUNT];
	lv_point_precise_t slope_line_points[PIDM_UI_POINT_COUNT];
} pidm_ui_ctx_t;

#ifdef __cplusplus
extern "C" {
#endif

extern const service_item_t g_service_pidm;

void ui_pidm_on_enter(void);
void ui_pidm_on_leave(void);
void ui_pidm_on_key_event(uint8_t key_index, uint8_t event_type);

#ifdef __cplusplus
}
#endif
