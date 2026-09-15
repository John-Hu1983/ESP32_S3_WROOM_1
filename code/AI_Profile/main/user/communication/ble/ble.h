#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "sdkconfig.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// clang-format off

#if !CONFIG_BT_ENABLED || !CONFIG_BT_NIMBLE_ENABLED
    #error "BLE module requires CONFIG_BT_ENABLED=y and CONFIG_BT_NIMBLE_ENABLED=y"
#endif

#define BLE_NOTIFY_CHUNK_LEN (20U)

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
#define BLE_DEBUG_FIFO_CAPACITY (512U)
#define BLE_DEBUG_TEXT_MAX_LEN (BLE_MAX_TEXT_LEN)
// clang-format on

typedef void (*ble_rx_cb_t)(const uint8_t* data, uint16_t len, void* user_ctx);

typedef enum {
    Ble_Chr_Rx = 1,
    Ble_Chr_Tx = 2,
} ble_chr_e;

typedef enum {
    Ble_Debug_Dir_Rx = 1,
    Ble_Debug_Dir_Tx = 2,
} ble_debug_dir_e;

typedef struct {
    uint32_t seq;
    uint32_t timestamp_ms;
    uint16_t len;
    uint16_t reserved;
    ble_debug_dir_e dir;
    char text[BLE_DEBUG_TEXT_MAX_LEN + 1U];
} ble_debug_item_s;

typedef struct {
    bool fifo_ready;
    uint16_t fifo_used;
    uint16_t fifo_capacity;
    uint32_t rx_total;
    uint32_t tx_total;
    uint32_t dropped_total;
} ble_debug_stats_s;

esp_err_t ble_init(void);
esp_err_t ble_start(void);
void ble_stop(void);

bool ble_is_ready(void);
bool ble_is_connected(void);

void ble_set_rx_callback(ble_rx_cb_t cb, void* user_ctx);
esp_err_t ble_send_text(const char* text);

bool ble_debug_fifo_pop(ble_debug_item_s* item);
bool ble_debug_get_last_rx(ble_debug_item_s* item);
bool ble_debug_get_last_tx(ble_debug_item_s* item);
void ble_debug_get_stats(ble_debug_stats_s* stats);

#ifdef __cplusplus
}
#endif


