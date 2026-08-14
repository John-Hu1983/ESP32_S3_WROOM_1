#include "service/service_logic.h"

#include "service/apps/service_items.h"

static const service_item_t* k_services[SERVICE_APP_COUNT] = {
    &g_service_ai,      &g_service_wifi,  &g_service_scan,   &g_service_link,
    &g_service_offline, &g_service_cell,  &g_service_signal, &g_service_node,
    &g_service_debug,   &g_service_tools, &g_service_mute,   &g_service_power,
};

int service_get_count(void) { return (int)(sizeof(k_services) / sizeof(k_services[0])); }

const service_item_t* service_get_item(int service_index) {
    if (service_index < 0 || service_index >= service_get_count()) {
        return 0;
    }
    return k_services[service_index];
}

service_key_result_t service_handle_key(int service_index, uint8_t key_index, uint8_t event_type) {
    service_key_result_t result = {0};
    const service_item_t* item = service_get_item(service_index);
    uint8_t i;

    if (item == 0 || item->bindings == 0 || item->binding_count == 0) {
        return result;
    }

    for (i = 0; i < item->binding_count; ++i) {
        const service_key_binding_t* binding = &item->bindings[i];
        if (binding->key_index != key_index || binding->event_type != event_type) {
            continue;
        }

        result.consumed = true;
        result.command = binding->command;
        result.notification = binding->notification;
        return result;
    }

    return result;
}
