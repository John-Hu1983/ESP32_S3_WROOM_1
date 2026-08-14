#include "service/apps/service_items.h"

static const service_key_binding_t k_bindings[] = {
    {0, SERVICE_KEY_EVENT_CLICK, SERVICE_CMD_NONE, "Link: previous"},
    {1, SERVICE_KEY_EVENT_CLICK, SERVICE_CMD_NONE, "Link: connect"},
    {1, SERVICE_KEY_EVENT_LONG_PRESS, SERVICE_CMD_NONE, "Link: test"},
};

const service_item_t g_service_link = {
    3,
    "Link",
    "Link Service",
    "Link tools",
    k_bindings,
    3,
};
