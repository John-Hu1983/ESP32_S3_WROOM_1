#include "service/ui_wifi.h"

static const service_key_binding_t k_bindings[] = {
    {0, SERVICE_KEY_EVENT_CLICK, SERVICE_CMD_NONE, "WiFi: previous"},
    {1, SERVICE_KEY_EVENT_CLICK, SERVICE_CMD_NONE, "WiFi: next"},
    {0, SERVICE_KEY_EVENT_LONG_PRESS, SERVICE_CMD_ENTER_NETWORK_CONFIG, "Enter WiFi config"},
    {1, SERVICE_KEY_EVENT_LONG_PRESS, SERVICE_CMD_NONE, "WiFi: scan"},
};

const service_item_t g_service_wifi = {
    2,
    "WiFi",
    "WiFi Service",
    "WiFi tools",
    k_bindings,
    4,
};
