#pragma once

#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "user/peripherals/gpba02b.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _lv_obj_t lv_obj_t;

/* Start desktop app runtime and get top-bar status label handle. */
esp_err_t desktop_start(void);
lv_obj_t* desktop_get_cpu_label(void);
lv_obj_t* desktop_get_net_label(void);

/* Start background service that updates top-bar status via the label handle. */
esp_err_t bg_start(lv_obj_t* cpu_label, lv_obj_t* net_label);

void bsp_lcd_reset_sequence(void);
void bsp_init_total(void);

#ifdef __cplusplus
}
#endif