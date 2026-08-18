#pragma once

#include <esp_err.h>
#include <esp_log.h>
#include <esp_lvgl_port.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <lvgl.h>
#include <material_symbols.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DESKTOP_LOG_TAG "desktop"

#define SERVICE_APP_COUNT 13

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

#define DESKTOP_SELECT_NOTIFICATION_MS 800
#define DESKTOP_ACTION_NOTIFICATION_MS 900
#define DESKTOP_DUAL_CLICK_WINDOW_MS 260
#define DESKTOP_SELECTION_TIMEOUT_MS 3000

#define DESKTOP_KEY_QUEUE_DEPTH 16
#define DESKTOP_TASK_STACK_SIZE 4096
#define DESKTOP_TASK_PRIORITY 4

#define DESKTOP_LVGL_LOCK_TIMEOUT_MS 30000

LV_FONT_DECLARE(BUILTIN_TEXT_FONT);
LV_FONT_DECLARE(BUILTIN_ICON_FONT);

typedef enum {
    SERVICE_KEY_EVENT_PRESS_DOWN = 0,
    SERVICE_KEY_EVENT_PRESS_UP = 1,
    SERVICE_KEY_EVENT_CLICK = 2,
    SERVICE_KEY_EVENT_LONG_PRESS = 3,
    SERVICE_KEY_EVENT_DUAL_CLICK = 4,
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
    void* ctx;
    void (*set_status)(void* ctx, const char* status);
    void (*set_prompt)(void* ctx, const char* prompt);
    void (*show_notification)(void* ctx, const char* text, uint32_t duration_ms);
    void (*toggle_chat)(void* ctx);
    void (*start_listening)(void* ctx);
    void (*stop_listening)(void* ctx);
    void (*enter_network_config)(void* ctx);
} desktop_ops_t;

typedef struct {
    void* ctx;
    void (*set_status)(void* ctx, const char* status);
    void (*set_prompt)(void* ctx, const char* prompt);
    void (*show_notification)(void* ctx, const char* text, uint32_t duration_ms);
    void (*toggle_chat)(void* ctx);
    void (*start_listening)(void* ctx);
    void (*stop_listening)(void* ctx);
    void (*enter_network_config)(void* ctx);
} desktop_host_ops_t;

typedef struct {
    QueueHandle_t key_queue;
    TaskHandle_t task_handle;
    desktop_ops_t ops;
    struct {
        int current_service_index;
        int selected_service_index;
        uint64_t selected_since_ms;
        uint64_t last_click_ms[2];
    } state;
    void* desktop_layer;
    void* desktop_tiles[SERVICE_APP_COUNT - 1];
    uint8_t desktop_tile_count;
} desktop_runtime_t;

extern const service_item_t g_desktop;

void desktop_runtime_init(desktop_runtime_t* runtime);
void desktop_build_ops(desktop_ops_t* ops, desktop_host_ops_t* host_ops);
esp_err_t desktop_task_start(desktop_runtime_t* runtime,
                             const desktop_ops_t* ops);
void desktop_enter_home(desktop_runtime_t* runtime, bool show_notification);
bool desktop_post_key_event(desktop_runtime_t* runtime, uint8_t key_index,
                            uint8_t event_type);

void desktop_service_fill_default_host_ops(desktop_host_ops_t* host_ops);
esp_err_t desktop_service_start(desktop_host_ops_t* host_ops);
void desktop_service_enter_home(bool show_notification);
bool desktop_service_post_key_event(uint8_t key_index, uint8_t event_type);
bool desktop_service_is_started(void);
bool* desktop_service_started_flag(void);

int desktop_get_count(void);
const service_item_t* desktop_get_item(int service_index);
bool desktop_is_home(const desktop_runtime_t* runtime);

#ifdef __cplusplus
}
#endif
