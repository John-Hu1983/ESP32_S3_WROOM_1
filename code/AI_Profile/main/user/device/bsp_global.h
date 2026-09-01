#pragma once

#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "user/device/dev_gpba02b.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _lv_obj_t lv_obj_t;

void delay_ms(uint32_t ms);
esp_err_t desktop_start_task(void);
lv_obj_t* desktop_get_cpu_label(void);
lv_obj_t* desktop_get_net_label(void);

esp_err_t bg_start_task(lv_obj_t* cpu_label, lv_obj_t* net_label);

void bsp_reset_lcd(void);
void bsp_set_audio_ctrl(bool enable);
void bsp_init_total(void);

#ifdef __cplusplus
}
#endif