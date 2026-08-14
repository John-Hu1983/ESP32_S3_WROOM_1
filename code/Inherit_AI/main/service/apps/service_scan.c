#include "service/apps/service_items.h"

static const service_key_binding_t k_bindings[] = {
    {0, SERVICE_KEY_EVENT_CLICK, SERVICE_CMD_NONE, "Scan: filter"},
    {1, SERVICE_KEY_EVENT_CLICK, SERVICE_CMD_NONE, "Scan: start"},
    {1, SERVICE_KEY_EVENT_LONG_PRESS, SERVICE_CMD_NONE, "Scan: deep mode"},
};

const service_item_t g_service_scan = {
    2,
    "Scan",
    "Scan Service",
    "Scan tools",
    k_bindings,
    3,
};
