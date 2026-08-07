#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#include "esp_log.h"
#include "esp_err.h"

#include "user_config.h"
#include "peripherals/gpba02b.h"

/* Minimum press time to accept one valid click. */
#define KEYBOARD_CLICK_DEBOUNCE_MS (20)
/* Press time threshold to report hold event. */
#define KEYBOARD_HOLD_MS (800U)
/* Release stable time before shape state resets to idle. */
#define KEYBOARD_RELEASE_MS (40U)

typedef enum
{
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

typedef enum
{
    Btn_Level_None = 0,
    Btn_Level_Up,
    Btn_Level_Down,
    Btn_Level_Both,
} btn_level_e;

typedef struct
{
    btn_level_e prev_level;
    uint8_t step;
    uint16_t debounce;
    uint16_t hold_period;
} btn_scan_s;

esp_err_t keyboard_init_obj(void);
btn_status_e keyboard_scan_event(btn_scan_s *scan, uint8_t ms);
