#include "service/ui_debug.h"

static const service_key_binding_t k_bindings[] = {
    {0, SERVICE_KEY_EVENT_CLICK, SERVICE_CMD_NONE, "Debug: previous"},
    {1, SERVICE_KEY_EVENT_CLICK, SERVICE_CMD_NONE, "Debug: run"},
    {1, SERVICE_KEY_EVENT_LONG_PRESS, SERVICE_CMD_NONE, "Debug: dump"},
};

const service_item_t g_service_debug = {
    9,
    "Debug",
    "Debug Service",
    "Debug tools",
    k_bindings,
    3,
};
