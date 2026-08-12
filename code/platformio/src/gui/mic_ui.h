#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "bsp/delay.h"
#include "lvgl.h"
#include "peripherals/mic_drv.h"
#include "service/asr_service.h"
#include "service/system_service.h"
#include "user_config.h"

#define MIC_MARGIN_X 6
#define MIC_LAYOUT_GAP 8

#define MIC_SAMPLE_RATE_HZ USER_AUDIO_SAMPLE_RATE_HZ
#define MIC_READ_FRAME_SAMPLES 256U
#define MIC_READ_TIMEOUT_MS 20U
#define MIC_UI_UPDATE_PERIOD_MS 100U

#define MIC_SAMPLE_TASK_STACK_SIZE 4096U
#define MIC_SAMPLE_TASK_PRIORITY 4U
#define MIC_TASK_STOP_WAIT_RETRY 20U
#define MIC_TASK_STOP_WAIT_DELAY_MS 5U

#define MIC_VAD_START_THRESHOLD_PERCENT 8U
#define MIC_VAD_END_THRESHOLD_PERCENT 5U
#define MIC_VAD_START_FRAMES 4U
#define MIC_VAD_END_FRAMES 8U

#define MIC_RECOGNIZED_TEXT_MAX_LEN 96U
#define MIC_SEGMENT_MAX_MS 3500U

typedef struct
{
    lv_obj_t *screen;
    lv_obj_t *card;
    lv_obj_t *status_label;
    lv_obj_t *level_bar;
    lv_obj_t *level_label;
    lv_obj_t *speech_label;
    lv_obj_t *recognized_title_label;
    lv_obj_t *recognized_label;
    lv_timer_t *ui_timer;
    TaskHandle_t sample_task_handle;
    volatile bool sample_task_stop;
    portMUX_TYPE state_lock;
    esp_err_t mic_state_ret;
    uint16_t level_percent;
    uint8_t speech_active;
    uint16_t speech_seq;
    asr_mode_e asr_mode;
    int16_t *segment_buf;
    size_t segment_capacity;
    size_t segment_samples;
    char recognized_text[MIC_RECOGNIZED_TEXT_MAX_LEN];
} mic_app_ctx_t;

/* Create microphone monitor app screen and runtime resources. */
lv_obj_t *mic_create_screen(lv_coord_t lcd_w, lv_coord_t lcd_h);
/* Update recognized text shown on Mic UI from external ASR pipeline. */
void mic_ui_set_recognized_text(const char *text);
