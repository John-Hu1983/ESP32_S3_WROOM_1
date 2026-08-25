#pragma once

#include <esp_err.h>
#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * brief : Start background service to update desktop top-bar stats every 1 second.
 * input : cpu_label - top-bar status text label; net_label - top-bar network icon label.
 * output: ESP_OK on success; otherwise error code.
 * type  : public
 */
esp_err_t bg_start_task(lv_obj_t* cpu_label, lv_obj_t* net_label);

#ifdef __cplusplus
}
#endif


