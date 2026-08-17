#pragma once

#include <esp_err.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

#include <esp_log.h>
#include <esp_lvgl_port.h>
#include <esp_timer.h>
#include <lvgl.h>
#include <material_symbols.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#define DESKTOP_PRIMARY_KEY 0
#define DESKTOP_SECONDARY_KEY 1
#define DESKTOP_AI_SERVICE_INDEX 1
#define DESKTOP_NO_SELECTION -1

#define DESKTOP_GRID_COLS 3
#define DESKTOP_GRID_ROWS 4
#define DESKTOP_CONTROL_COUNT (DESKTOP_GRID_COLS * DESKTOP_GRID_ROWS)

#define DESKTOP_LAYER_MARGIN_X 0
#define DESKTOP_LAYER_PAD 2
#define DESKTOP_GRID_GAP 8
#define DESKTOP_TILE_MARGIN 3

#define UBUNTU_SURFACE_HEX 0x1D1526
#define UBUNTU_CARD_HEX 0x5A3A57
#define UBUNTU_ACCENT_HEX 0xE95420
#define UBUNTU_TEXT_HEX 0xF7F7F7

LV_FONT_DECLARE(BUILTIN_TEXT_FONT);
LV_FONT_DECLARE(BUILTIN_ICON_FONT);

#define DESKTOP_SELECT_NOTIFICATION_MS 800
#define DESKTOP_ACTION_NOTIFICATION_MS 900
#define DESKTOP_DUAL_CLICK_WINDOW_MS 260

#define DESKTOP_KEY_QUEUE_DEPTH 16
#define DESKTOP_TASK_STACK_SIZE 4096
#define DESKTOP_TASK_PRIORITY 4


#ifdef __cplusplus
extern "C" {
#endif

#define SERVICE_APP_COUNT 13

typedef enum {
    SERVICE_KEY_EVENT_PRESS_DOWN = 0,
    SERVICE_KEY_EVENT_PRESS_UP = 1,
    SERVICE_KEY_EVENT_CLICK = 2,
    SERVICE_KEY_EVENT_LONG_PRESS = 3,
} service_key_event_t;

typedef enum {
    SERVICE_CMD_NONE = 0,
    SERVICE_CMD_TOGGLE_CHAT,
    SERVICE_CMD_START_LISTENING,
    SERVICE_CMD_STOP_LISTENING,
    SERVICE_CMD_ENTER_DESKTOP,
    SERVICE_CMD_ENTER_NETWORK_CONFIG,
} service_command_t;

typedef struct {
    uint8_t key_index;
    uint8_t event_type;
    service_command_t command;
    const char* notification;
} service_key_binding_t;

typedef struct {
    int index;
    const char* name;
    const char* status;
    const char* prompt;
    const service_key_binding_t* bindings;
    uint8_t binding_count;
} service_item_t;

typedef struct {
    bool consumed;
    service_command_t command;
    const char* notification;
} service_key_result_t;

typedef struct {
    uint8_t key_index;
    uint8_t event_type;
} desktop_key_event_t;

typedef struct {
    int current_service_index;
    int selected_service_index;
    uint64_t last_click_ms[2];
} desktop_state_t;

typedef struct {
    void* ctx;
    void (*enter_service)(void* ctx, int service_index);
    void (*enter_desktop)(void* ctx, bool show_notification);
    void (*show_notification)(void* ctx, const char* text, uint32_t duration_ms);
    void (*run_command)(void* ctx, service_command_t command);
} desktop_ops_t;

typedef struct {
    QueueHandle_t key_queue;
    TaskHandle_t task_handle;
    desktop_ops_t ops;
    desktop_state_t state;
    void* desktop_layer;
    void* desktop_tiles[SERVICE_APP_COUNT - 1];
    uint8_t desktop_tile_count;
} desktop_runtime_t;

extern const service_item_t g_desktop;

void desktop_runtime_init(desktop_runtime_t* runtime);
esp_err_t desktop_task_start(desktop_runtime_t* runtime,
                                     const desktop_ops_t* ops);
bool desktop_post_key_event(desktop_runtime_t* runtime, uint8_t key_index,
                                    uint8_t event_type);

int desktop_get_count(void);
const service_item_t* desktop_get_item(int service_index);
bool desktop_is_home(const desktop_runtime_t* runtime);

#ifdef __cplusplus
}
#endif
