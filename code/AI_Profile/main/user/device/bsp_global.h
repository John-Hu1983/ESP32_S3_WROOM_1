#pragma once

#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "user/device/dev_gpba02b.h"

#ifdef __cplusplus
extern "C" {
#endif

void delay_ms(uint32_t ms);
esp_err_t desktop_start_task(void);

void bsp_reset_lcd(void);
void bsp_set_audio_ctrl(bool enable);
void bsp_init_total(void);

#ifdef __cplusplus
}
#endif