#include "background_thread.h"

#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_wifi.h>
#include <material_symbols.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#define TAG "background_thread"

#define BACKGROUND_TASK_STACK_SIZE (4096U)
#define BACKGROUND_TASK_PRIORITY (4U)
#define BACKGROUND_TASK_PERIOD_MS (100U)
#define BACKGROUND_LABEL_UPDATE_PERIOD_MS (1000U)
#define BACKGROUND_STATUS_TEXT_LEN (32U)
#define BACKGROUND_NET_ICON_LEN (8U)

static TaskHandle_t s_background_task_handle;
static lv_obj_t* s_top_bar_status_label;
static lv_obj_t* s_top_bar_net_label;
static portMUX_TYPE s_status_text_lock = portMUX_INITIALIZER_UNLOCKED;
static char s_pending_status_text[BACKGROUND_STATUS_TEXT_LEN] = "CPU:--% | MI:--% | MP:--%";
static char s_pending_net_icon[BACKGROUND_NET_ICON_LEN] = MATERIAL_SYMBOLS_WIFI_OFF;
static bool s_status_text_dirty = true;
static bool s_net_icon_dirty = true;

static uint8_t _mem_pct(uint32_t caps) {
    size_t total = heap_caps_get_total_size(caps);
    if (total == 0U) {
        return 0U;
    }

    size_t free = heap_caps_get_free_size(caps);
    size_t used = (free < total) ? (total - free) : 0U;
    uint32_t percent = (uint32_t)((used * 100U) / total);
    if (percent > 100U) {
        percent = 100U;
    }
    return (uint8_t)percent;
}

#if (configUSE_TRACE_FACILITY == 1)
static bool _cpu_counters(uint64_t* total_runtime, uint64_t* idle_runtime) {
    const UBaseType_t alloc_task_count = uxTaskGetNumberOfTasks() + 5U;
    TaskStatus_t* task_states = (TaskStatus_t*)malloc(sizeof(TaskStatus_t) * alloc_task_count);
    if (task_states == NULL) {
        return false;
    }

    configRUN_TIME_COUNTER_TYPE run_time_counter = 0;
    const UBaseType_t actual_task_count =
        uxTaskGetSystemState(task_states, alloc_task_count, &run_time_counter);
    if (actual_task_count == 0U) {
        free(task_states);
        return false;
    }

    uint64_t idle_sum = 0U;
    for (UBaseType_t i = 0; i < actual_task_count; i++) {
        if (strncmp(task_states[i].pcTaskName, "IDLE", 4) == 0) {
            idle_sum += (uint64_t)task_states[i].ulRunTimeCounter;
        }
    }

    free(task_states);
    *total_runtime = (uint64_t)run_time_counter;
    *idle_runtime = idle_sum;
    return true;
}

static uint8_t _cpu_pct(void) {
    static bool has_prev_sample;
    static uint64_t prev_total_runtime;
    static uint64_t prev_idle_runtime;

    uint64_t cur_total_runtime = 0U;
    uint64_t cur_idle_runtime = 0U;
    if (!_cpu_counters(&cur_total_runtime, &cur_idle_runtime)) {
        return 0U;
    }

    if (!has_prev_sample) {
        prev_total_runtime = cur_total_runtime;
        prev_idle_runtime = cur_idle_runtime;
        has_prev_sample = true;
        return 0U;
    }

    uint64_t total_delta = cur_total_runtime - prev_total_runtime;
    uint64_t idle_delta = cur_idle_runtime - prev_idle_runtime;

    prev_total_runtime = cur_total_runtime;
    prev_idle_runtime = cur_idle_runtime;

    if (total_delta == 0U) {
        return 0U;
    }

    uint64_t total_capacity = total_delta * (uint64_t)CONFIG_FREERTOS_NUMBER_OF_CORES;
    if (total_capacity == 0U) {
        return 0U;
    }

    uint64_t busy_delta = (idle_delta < total_capacity) ? (total_capacity - idle_delta) : 0U;
    uint32_t percent = (uint32_t)((busy_delta * 100U) / total_capacity);
    if (percent > 100U) {
        percent = 100U;
    }
    return (uint8_t)percent;
}
#else
static uint8_t _cpu_pct(void) { return 0U; }
#endif

static void _status_set(const char* status_text) {
    taskENTER_CRITICAL(&s_status_text_lock);
    snprintf(s_pending_status_text, sizeof(s_pending_status_text), "%s", status_text);
    s_status_text_dirty = true;
    taskEXIT_CRITICAL(&s_status_text_lock);
}

static void _net_set(const char* icon_text) {
    taskENTER_CRITICAL(&s_status_text_lock);
    snprintf(s_pending_net_icon, sizeof(s_pending_net_icon), "%s", icon_text);
    s_net_icon_dirty = true;
    taskEXIT_CRITICAL(&s_status_text_lock);
}

static const char* _net_icon(void) {
    wifi_ap_record_t ap_info;
    if (esp_wifi_sta_get_ap_info(&ap_info) != ESP_OK) {
        return MATERIAL_SYMBOLS_WIFI_OFF;
    }

    if (ap_info.rssi >= -65) {
        return MATERIAL_SYMBOLS_WIFI;
    }
    if (ap_info.rssi >= -75) {
        return MATERIAL_SYMBOLS_WIFI_2_BAR;
    }
    return MATERIAL_SYMBOLS_WIFI_1_BAR;
}

static void _status_apply_async(void* param) {
    (void)param;

    if ((s_top_bar_status_label == NULL) || !lv_obj_is_valid(s_top_bar_status_label)) {
        return;
    }
    if ((s_top_bar_net_label == NULL) || !lv_obj_is_valid(s_top_bar_net_label)) {
        return;
    }

    char status_text[BACKGROUND_STATUS_TEXT_LEN];
    char net_icon_text[BACKGROUND_NET_ICON_LEN];
    bool should_apply = false;
    bool should_apply_icon = false;

    taskENTER_CRITICAL(&s_status_text_lock);
    if (s_status_text_dirty) {
        memcpy(status_text, s_pending_status_text, sizeof(status_text));
        s_status_text_dirty = false;
        should_apply = true;
    }
    if (s_net_icon_dirty) {
        memcpy(net_icon_text, s_pending_net_icon, sizeof(net_icon_text));
        s_net_icon_dirty = false;
        should_apply_icon = true;
    }
    taskEXIT_CRITICAL(&s_status_text_lock);

    if (should_apply) {
        lv_label_set_text(s_top_bar_status_label, status_text);
    }
    if (should_apply_icon) {
        lv_label_set_text(s_top_bar_net_label, net_icon_text);
    }
}

static void _status_refresh(void) {
    const uint8_t cpu_percent = _cpu_pct();
    const uint8_t mem_internal_percent = _mem_pct(MALLOC_CAP_INTERNAL);
    const uint8_t mem_psram_percent = _mem_pct(MALLOC_CAP_SPIRAM);
    const char* net_icon = _net_icon();
    char status_text[BACKGROUND_STATUS_TEXT_LEN];

    snprintf(status_text, sizeof(status_text), "c:%02u%% | m:%02u%% | p:%02u%%", cpu_percent,
             mem_internal_percent, mem_psram_percent);
    _status_set(status_text);
    _net_set(net_icon);

    lv_lock();
    lv_result_t lv_res = lv_async_call(_status_apply_async, NULL);
    lv_unlock();
    if (lv_res != LV_RESULT_OK) {
        ESP_LOGW(TAG, "lv_async_call failed while updating top-bar status");
    }
}

static void _thread_loop(void* param) {
    (void)param;

    uint32_t label_update_elapsed_ms = BACKGROUND_LABEL_UPDATE_PERIOD_MS;
    while (1) {
        if (label_update_elapsed_ms >= BACKGROUND_LABEL_UPDATE_PERIOD_MS) {
            label_update_elapsed_ms = 0U;
            _status_refresh();
        }

        vTaskDelay(pdMS_TO_TICKS(BACKGROUND_TASK_PERIOD_MS));
        label_update_elapsed_ms += BACKGROUND_TASK_PERIOD_MS;
    }
}

esp_err_t bg_start(lv_obj_t* cpu_label, lv_obj_t* net_label) {
    if (cpu_label == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (net_label == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    lv_lock();
    bool label_valid = lv_obj_is_valid(cpu_label);
    bool net_label_valid = lv_obj_is_valid(net_label);
    lv_unlock();
    if (!label_valid || !net_label_valid) {
        return ESP_ERR_INVALID_ARG;
    }

    if (s_background_task_handle != NULL) {
        return ESP_OK;
    }

    s_top_bar_status_label = cpu_label;
    s_top_bar_net_label = net_label;
    lv_lock();
    lv_result_t lv_res = lv_async_call(_status_apply_async, NULL);
    lv_unlock();
    if (lv_res != LV_RESULT_OK) {
        ESP_LOGW(TAG, "lv_async_call failed while setting initial top-bar status");
    }

    BaseType_t task_ok =
        xTaskCreate(_thread_loop, "background_thread", BACKGROUND_TASK_STACK_SIZE, NULL,
                    BACKGROUND_TASK_PRIORITY, &s_background_task_handle);
    if (task_ok != pdPASS) {
        s_background_task_handle = NULL;
        s_top_bar_status_label = NULL;
        s_top_bar_net_label = NULL;
        return ESP_FAIL;
    }

    return ESP_OK;
}
