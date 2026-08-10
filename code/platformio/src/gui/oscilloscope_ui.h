#pragma once

#include <math.h>
#include <stdbool.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "desktop/desktop_app.h"
#include "service/system_service.h"
#include "bsp/delay.h"
#include "lvgl.h"
#include "peripherals/keyboard.h"

#define SCOPE_POINT_COUNT 96U

typedef struct
{
	lv_obj_t *screen;
	lv_obj_t *chart;
	lv_chart_series_t *series;
	lv_timer_t *timer;
	float phase;
	int16_t points[SCOPE_POINT_COUNT];
} scope_app_ctx_t;

/* Create oscilloscope app screen and runtime resources. */
lv_obj_t *scope_create_screen(lv_coord_t lcd_w, lv_coord_t lcd_h);
/* Request return-to-home navigation from oscilloscope app. */
void scope_destroy_and_return(void);
