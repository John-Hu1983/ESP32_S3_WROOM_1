#pragma once

#include <esp_err.h>
#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BACKGROUND_TASK_STACK_SIZE (4096U)
#define BACKGROUND_TASK_PRIORITY (4U)
#define BACKGROUND_TASK_PERIOD_MS (100U)
#define BACKGROUND_LABEL_UPDATE_PERIOD_MS (1000U)
#define BACKGROUND_STATUS_TEXT_LEN (32U)
#define BACKGROUND_NET_ICON_LEN (8U)

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


