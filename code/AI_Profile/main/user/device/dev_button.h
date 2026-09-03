#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <material_symbols.h>
#include "lvgl.h"

#include "user/inc/user_config.h"
#include "user/device/dev_st7365p.h"

// clang-format off
#define KEYBOARD_CLICK_DEBOUNCE_MS (20)
#define KEYBOARD_HOLD_MS           (800U)
#define KEYBOARD_RELEASE_MS        (40U)
// clang-format on

typedef enum {
    Btn_Idle = 0,
    Btn_Up_Click,
    Btn_Up_Double,
    Btn_Up_Hold_Enter,
    Btn_Up_Hold_Continue,

    Btn_Down_Click,
    Btn_Down_Double,
    Btn_Down_Hold_Enter,
    Btn_Down_Hold_Continue,

    Btn_Both_Click,
    Btn_Both_Double,
    Btn_Both_Hold_Enter,
    Btn_Both_Hold_Continue,

} btn_status_e;

typedef enum {
    Btn_Level_None = 0,
    Btn_Level_Up,
    Btn_Level_Down,
    Btn_Level_Both,
} btn_level_e;

typedef enum {
    scan_step_enter = 0,
    scan_step_debounce,
    scan_step_hold,
} btn_scan_step_e;

typedef struct {
    btn_level_e prev_level;
    uint8_t step;
    uint16_t debounce;
    uint16_t hold_period;
} btn_scan_s;

btn_status_e button_scan_state(btn_scan_s* scan, uint8_t ms);
