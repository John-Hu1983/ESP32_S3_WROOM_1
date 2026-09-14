#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// clang-format off

#if defined(CONFIG_BT_ENABLED) && (CONFIG_BT_ENABLED == 1) && \
    defined(CONFIG_BT_NIMBLE_ENABLED) && (CONFIG_BT_NIMBLE_ENABLED == 1)
        #define BLE_NIMBLE_AVAILABLE (1)
        #define BLE_NOTIFY_CHUNK_LEN (20U)
#else
        #define BLE_NIMBLE_AVAILABLE (0)
#endif

#define BLE_DEVICE_NAME        "Spin_John_BLE"
#define BLE_SERVICE_UUID       "0000fff0-0000-1000-8000-00805f9b34fb"
#define BLE_RX_CHAR_UUID       "0000fff1-0000-1000-8000-00805f9b34fb"
#define BLE_TX_CHAR_UUID       "0000fff2-0000-1000-8000-00805f9b34fb"
#define BLE_SERVICE_UUID16     (0xFFF0U)
#define BLE_RX_CHAR_UUID16     (0xFFF1U)
#define BLE_TX_CHAR_UUID16     (0xFFF2U)
#define BLE_MAX_TEXT_LEN       (180U)
#define BLE_ADV_ITVL_MIN       (160U)
#define BLE_ADV_ITVL_MAX       (240U)
// clang-format on

typedef void (*ble_rx_cb_t)(const uint8_t* data, uint16_t len, void* user_ctx);

typedef enum {
    Ble_Chr_Rx = 1,
    Ble_Chr_Tx = 2,
} ble_chr_e;

esp_err_t ble_init(void);
esp_err_t ble_start(void);
void ble_stop(void);

bool ble_is_ready(void);
bool ble_is_connected(void);

void ble_set_rx_callback(ble_rx_cb_t cb, void* user_ctx);
esp_err_t ble_send_text(const char* text);

#ifdef __cplusplus
}
#endif


