#include "service/ui_cell.h"

static const service_key_binding_t k_bindings[] = {
    {0, SERVICE_KEY_EVENT_CLICK, SERVICE_CMD_NONE, "Cell: previous"},
    {1, SERVICE_KEY_EVENT_CLICK, SERVICE_CMD_NONE, "Cell: next"},
    {1, SERVICE_KEY_EVENT_LONG_PRESS, SERVICE_CMD_NONE, "Cell: diagnose"},
};

const service_item_t g_service_cell = {
    6,
    "Cell",
    "Cell Service",
    "Cell tools",
    k_bindings,
    3,
};
