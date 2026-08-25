#pragma once

#include <esp_err.h>
#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Start background service to update desktop top-bar stats every 1 second. */
esp_err_t bg_start(lv_obj_t* cpu_label, lv_obj_t* net_label);

#ifdef __cplusplus
}
#endif


