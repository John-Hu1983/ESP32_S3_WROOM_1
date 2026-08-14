#include "service/apps/service_items.h"

static const service_key_binding_t k_bindings[] = {
    {0, SERVICE_KEY_EVENT_CLICK, SERVICE_CMD_TOGGLE_CHAT, 0},
    {0, SERVICE_KEY_EVENT_LONG_PRESS, SERVICE_CMD_ENTER_DESKTOP, "Desktop"},
    {1, SERVICE_KEY_EVENT_PRESS_DOWN, SERVICE_CMD_START_LISTENING, 0},
    {1, SERVICE_KEY_EVENT_PRESS_UP, SERVICE_CMD_STOP_LISTENING, 0},
};

const service_item_t g_service_ai = {
    0,
    "AI",
    "AI Service",
    "AI service ready",
    k_bindings,
    4,
};
