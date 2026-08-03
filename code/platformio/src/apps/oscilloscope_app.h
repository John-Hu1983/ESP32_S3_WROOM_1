#pragma once

#include <math.h>
#include <stdint.h>

#include "app_status_bar.h"
#include "lvgl.h"

#define SCOPE_POINT_COUNT 96U

typedef struct
{
	lv_obj_t *chart;
	lv_chart_series_t *series;
	lv_timer_t *timer;
	float phase;
	int16_t points[SCOPE_POINT_COUNT];
} scope_app_ctx_t;

lv_obj_t *scope_app_create_screen(lv_coord_t lcd_w, lv_coord_t lcd_h);
