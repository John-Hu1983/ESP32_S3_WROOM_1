#include "service/ui_power.h"

static const service_key_binding_t k_bindings[] = {
    {0, SERVICE_KEY_EVENT_CLICK, SERVICE_CMD_NONE, "Power: profile -"},
    {1, SERVICE_KEY_EVENT_CLICK, SERVICE_CMD_NONE, "Power: profile +"},
    {1, SERVICE_KEY_EVENT_LONG_PRESS, SERVICE_CMD_NONE, "Power: apply"},
};

const service_item_t g_service_power = {
    12,
    "Power",
    "Power Service",
    "Power tools",
    k_bindings,
    3,
};
