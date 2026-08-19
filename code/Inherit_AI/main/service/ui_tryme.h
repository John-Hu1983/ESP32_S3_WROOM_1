#pragma once

#include "service/desktop.h"

#include <stdint.h>

#define UI_TRYME_LOG_TAG "UiTryMe"

#define TRYME_UI_WAVE_POINT_COUNT 96
#define TRYME_UI_TEXT_COUNT 9
#define TRYME_UI_REFRESH_MS 220
#define TRYME_UI_MARGIN 3
#define TRYME_UI_GAP 4

#define TRYME_UI_BG_HEX 0x10151A
#define TRYME_UI_SURFACE_HEX 0x1B2630
#define TRYME_UI_BORDER_HEX 0x32414F
#define TRYME_UI_TEXT_HEX 0xE8EEF5
#define TRYME_UI_TEXT_SECONDARY_HEX 0xA5B2C0
#define TRYME_UI_ALERT_HEX 0xFF5630
#define TRYME_UI_WAVE_HEX 0x2FB5E2

typedef struct {
    lv_obj_t* panel;
    lv_obj_t* plot;
    lv_obj_t* wave;
    lv_obj_t* metric_labels[TRYME_UI_TEXT_COUNT];
    lv_timer_t* timer;
    lv_point_precise_t line_points[TRYME_UI_WAVE_POINT_COUNT];
    uint32_t last_age_bucket;
    bool age_alert_valid;
    bool age_alert_active;
    bool wave_idle;
    bool monitor_started;
} tryme_ui_ctx_t;

#ifdef __cplusplus
extern "C" {
#endif

extern const service_item_t g_service_tryme;

void ui_tryme_on_enter(void);
void ui_tryme_on_leave(void);
void ui_tryme_on_key_event(uint8_t key_index, uint8_t event_type);

#ifdef __cplusplus
}
#endif
