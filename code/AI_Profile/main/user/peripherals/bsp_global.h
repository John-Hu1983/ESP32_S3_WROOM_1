#pragma once

#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "user/peripherals/gpba02b.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _lv_obj_t lv_obj_t;

/*
 * brief : Start desktop app runtime.
 * input : none.
 * output: ESP_OK on success; otherwise error code.
 * type  : public
 */
esp_err_t desktop_start(void);
/*
 * brief : Get top-bar CPU status label handle.
 * input : none.
 * output: LVGL object handle or NULL.
 * type  : public
 */
lv_obj_t* desktop_get_cpu_label(void);
/*
 * brief : Get top-bar network icon label handle.
 * input : none.
 * output: LVGL object handle or NULL.
 * type  : public
 */
lv_obj_t* desktop_get_net_label(void);

/*
 * brief : Start background service that updates top-bar status.
 * input : cpu_label - status label; net_label - network label.
 * output: ESP_OK on success; otherwise error code.
 * type  : public
 */
esp_err_t bg_start_task(lv_obj_t* cpu_label, lv_obj_t* net_label);

/*
 * brief : Execute LCD reset pulse sequence.
 * input : none.
 * output: none.
 * type  : public
 */
void bsp_reset_lcd(void);
/*
 * brief : Enable or disable audio power-control GPIOs.
 * input : enable - true to enable audio hardware, false to disable.
 * output: none.
 * type  : public
 */
void bsp_set_audio_ctrl(bool enable);
/*
 * brief : Initialize board-level peripheral chain and user desktop services.
 * input : none.
 * output: none.
 * type  : public
 */
void bsp_init_total(void);

#ifdef __cplusplus
}
#endif