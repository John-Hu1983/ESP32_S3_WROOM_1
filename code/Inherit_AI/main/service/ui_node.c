#include "service/ui_node.h"

static const service_key_binding_t k_bindings[] = {
    {0, SERVICE_KEY_EVENT_CLICK, SERVICE_CMD_NONE, "Node: previous"},
    {1, SERVICE_KEY_EVENT_CLICK, SERVICE_CMD_NONE, "Node: select"},
    {1, SERVICE_KEY_EVENT_LONG_PRESS, SERVICE_CMD_NONE, "Node: inspect"},
};

const service_item_t g_service_node = {
    8,
    "Node",
    "Node Service",
    "Node tools",
    k_bindings,
    3,
};
