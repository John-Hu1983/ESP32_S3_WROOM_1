#pragma once

#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "user/peripherals/gpba02b.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Start desktop app runtime (implemented in user/desktop). */
esp_err_t desktop_app_start(void);

void bsp_lcd_reset_sequence(void);
void bsp_init_total(void);

#ifdef __cplusplus
}
#endif