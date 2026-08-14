#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SERVICE_APP_COUNT 12

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

const service_item_t* service_get_item(int service_index);
int service_get_count(void);
service_key_result_t service_handle_key(int service_index, uint8_t key_index,
                                        uint8_t event_type);

#ifdef __cplusplus
}
#endif
