#include "service/apps/service_items.h"

static const service_key_binding_t k_bindings[] = {
    {0, SERVICE_KEY_EVENT_CLICK, SERVICE_CMD_NONE, "Offline: cache"},
    {1, SERVICE_KEY_EVENT_CLICK, SERVICE_CMD_NONE, "Offline: replay"},
    {1, SERVICE_KEY_EVENT_LONG_PRESS, SERVICE_CMD_NONE, "Offline: sync"},
};

const service_item_t g_service_offline = {
    4,
    "Offline",
    "Offline Service",
    "Offline tools",
    k_bindings,
    3,
};
