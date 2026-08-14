#include "service/service_mute.h"

static const service_key_binding_t k_bindings[] = {
    {0, SERVICE_KEY_EVENT_CLICK, SERVICE_CMD_NONE, "Mute: volume -"},
    {1, SERVICE_KEY_EVENT_CLICK, SERVICE_CMD_NONE, "Mute: volume +"},
    {1, SERVICE_KEY_EVENT_LONG_PRESS, SERVICE_CMD_NONE, "Mute: toggle"},
};

const service_item_t g_service_mute = {
    11,
    "Mute",
    "Mute Service",
    "Audio tools",
    k_bindings,
    3,
};
