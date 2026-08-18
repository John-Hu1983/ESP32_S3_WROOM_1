#pragma once

#include "bsp/gpba02b.h"

#include <esp_err.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <stdbool.h>
#include <stdint.h>

#include "active_board_config.h"


#ifdef __cplusplus
extern "C" {
#endif

#define KEYBOARD_CLICK_DEBOUNCE_MS (20)
#define KEYBOARD_HOLD_MS (800U)
#define KEYBOARD_RELEASE_MS (40U)
#define KEYBOARD_KEY_INDEX_BOTH (0xFFU)
#define KEYBOARD_TASK_STACK_SIZE (3072)
#define KEYBOARD_TASK_PRIORITY (2)

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
    Keyboard_Notify_Idle = 0,
    Keyboard_Notify_Up_Click,
    Keyboard_Notify_Up_Double,
    Keyboard_Notify_Up_Hold_Enter,
    Keyboard_Notify_Up_Hold_Continue,
    Keyboard_Notify_Down_Click,
    Keyboard_Notify_Down_Double,
    Keyboard_Notify_Down_Hold_Enter,
    Keyboard_Notify_Down_Hold_Continue,
    Keyboard_Notify_Both_Click,
    Keyboard_Notify_Both_Double,
    Keyboard_Notify_Both_Hold_Enter,
    Keyboard_Notify_Both_Hold_Continue,
} keyboard_notify_e;

typedef enum {
    Keyboard_App_Event_PressDown = 0,
    Keyboard_App_Event_PressUp,
    Keyboard_App_Event_Click,
    Keyboard_App_Event_LongPress,
    Keyboard_App_Event_DualClick,
} keyboard_app_event_e;

typedef void (*keyboard_notify_callback_t)(keyboard_notify_e notify, void* user_ctx);
typedef void (*keyboard_app_event_callback_t)(uint8_t key_index, uint8_t event_type,
                                              void* user_ctx);

typedef enum {
    Btn_Level_None = 0,
    Btn_Level_Up,
    Btn_Level_Down,
    Btn_Level_Both,
} btn_level_e;

typedef enum {
    Keyboard_Scan_Step_Enter = 0,
    Keyboard_Scan_Step_Debounce,
    Keyboard_Scan_Step_Hold,
} keyboard_scan_step_e;

typedef struct {
    btn_level_e prev_level;
    keyboard_scan_step_e step;
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
    btn_scan_s scan;
    TaskHandle_t task_handle;
    keyboard_notify_callback_t notify_callback;
    void* notify_callback_ctx;
    keyboard_app_event_callback_t app_event_callback;
    void* app_event_ctx;
    bool initialized;
    bool running;
} keyboard_t;

typedef bool (*keyboard_event_post_fn_t)(void* runtime, uint8_t key_index, uint8_t event_type);

typedef struct {
    void* event_runtime;
    keyboard_event_post_fn_t post_event;
    bool* enabled_flag;
    keyboard_app_event_callback_t app_event_callback;
    void* app_event_ctx;
} keyboard_event_router_t;

esp_err_t start_keyboard(keyboard_t* keyboard, const keyboard_config_t* config);
void keyboard_set_app_event_callback(keyboard_t* keyboard,
                                     keyboard_app_event_callback_t app_event_callback,
                                     void* user_ctx);
void keyboard_event_router_init(keyboard_event_router_t* router, void* event_runtime,
                                keyboard_event_post_fn_t post_event, bool* enabled_flag);
void keyboard_event_router_set_app_callback(keyboard_event_router_t* router,
                                            keyboard_app_event_callback_t app_event_callback,
                                            void* app_event_ctx);
void keyboard_event_router_callback(uint8_t key_index, uint8_t event_type, void* user_ctx);

esp_err_t keyboard_service_start_for_desktop(const keyboard_config_t* config);
void keyboard_service_set_app_event_callback(keyboard_app_event_callback_t app_event_callback,
                                             void* app_event_ctx);
void keyboard_service_stop(void);
void stop_keyboard(keyboard_t* keyboard);

#ifdef __cplusplus
}
#endif
