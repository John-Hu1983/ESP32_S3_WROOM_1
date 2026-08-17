#include "service/ui_signal.h"

static const service_key_binding_t k_bindings[] = {
    {0, SERVICE_KEY_EVENT_CLICK, SERVICE_CMD_NONE, "Signal: history"},
    {1, SERVICE_KEY_EVENT_CLICK, SERVICE_CMD_NONE, "Signal: refresh"},
    {1, SERVICE_KEY_EVENT_LONG_PRESS, SERVICE_CMD_NONE, "Signal: monitor"},
};

const service_item_t g_service_signal = {
    7,
    "Signal",
    "Signal Service",
    "Signal tools",
    k_bindings,
    3,
};
