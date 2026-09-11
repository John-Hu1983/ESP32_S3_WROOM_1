#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// clang-format off
#define USER_BLE_DEVICE_NAME        "ESP32S3-BLE-TEST"
#define USER_BLE_SERVICE_UUID       "0000fff0-0000-1000-8000-00805f9b34fb"
#define USER_BLE_RX_CHAR_UUID       "0000fff1-0000-1000-8000-00805f9b34fb"
#define USER_BLE_TX_CHAR_UUID       "0000fff2-0000-1000-8000-00805f9b34fb"
#define USER_BLE_SERVICE_UUID16     (0xFFF0U)
#define USER_BLE_RX_CHAR_UUID16     (0xFFF1U)
#define USER_BLE_TX_CHAR_UUID16     (0xFFF2U)
#define USER_BLE_MAX_TEXT_LEN       (180U)
// clang-format on

typedef void (*user_ble_rx_cb_t)(const uint8_t* data, uint16_t len, void* user_ctx);

esp_err_t user_ble_init(void);
esp_err_t user_ble_start(void);
void user_ble_stop(void);

bool user_ble_is_ready(void);
bool user_ble_is_connected(void);

void user_ble_set_rx_callback(user_ble_rx_cb_t cb, void* user_ctx);
esp_err_t user_ble_send_text(const char* text);

#ifdef __cplusplus
}
#endif
