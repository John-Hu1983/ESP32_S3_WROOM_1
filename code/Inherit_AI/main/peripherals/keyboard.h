#pragma once

#include "bsp/gpba02b.h"

#include <esp_err.h>

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define KEYBOARD_CLICK_DEBOUNCE_MS (20)
#define KEYBOARD_HOLD_MS (800U)
#define KEYBOARD_RELEASE_MS (40U)

typedef struct {
    gpba02b_port_t port;
    uint8_t pin;
} keyboard_key_pin_t;

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

typedef struct {
    btn_level_e prev_level;
    uint8_t step;
    uint16_t debounce;
    uint16_t hold_period;
} btn_scan_s;

typedef struct {
    keyboard_key_pin_t up_key;
    keyboard_key_pin_t down_key;
    bool active_low;
    uint32_t poll_interval_ms;
} keyboard_config_t;

typedef struct {
    keyboard_config_t config;
    bool initialized;
} keyboard_t;

void keyboard_get_default_config(keyboard_config_t* config);
esp_err_t keyboard_init_obj(keyboard_t* keyboard, const keyboard_config_t* config);
bool keyboard_read_level(const keyboard_t* keyboard, btn_level_e* level);
btn_status_e keyboard_scan_event(keyboard_t* keyboard, btn_scan_s* scan, uint8_t ms);

#ifdef __cplusplus
}
#endif
