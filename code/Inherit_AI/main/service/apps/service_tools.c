#include "service/apps/service_items.h"

static const service_key_binding_t k_bindings[] = {
    {0, SERVICE_KEY_EVENT_CLICK, SERVICE_CMD_NONE, "Tools: previous"},
    {1, SERVICE_KEY_EVENT_CLICK, SERVICE_CMD_NONE, "Tools: open"},
    {1, SERVICE_KEY_EVENT_LONG_PRESS, SERVICE_CMD_NONE, "Tools: run"},
};

const service_item_t g_service_tools = {
    9,
    "Tools",
    "Tools Service",
    "System tools",
    k_bindings,
    3,
};
