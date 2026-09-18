#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "sdkconfig.h"

#ifdef __cplusplus
extern "C" {
#endif

// clang-format off

#if !CONFIG_BT_ENABLED || !CONFIG_BT_NIMBLE_ENABLED
    #error "BLE module requires CONFIG_BT_ENABLED=y and CONFIG_BT_NIMBLE_ENABLED=y"
#endif

#define BLE_NOTIFY_CHUNK_LEN        (20U)

#define BLE_DEVICE_NAME             "Spin_John_BLE"
#define BLE_SERVICE_UUID            "0000fff0-0000-1000-8000-00805f9b34fb"
#define BLE_RX_CHAR_UUID            "0000fff1-0000-1000-8000-00805f9b34fb"
#define BLE_TX_CHAR_UUID            "0000fff2-0000-1000-8000-00805f9b34fb"
#define BLE_SERVICE_UUID16          (0xFFF0U)
#define BLE_RX_CHAR_UUID16          (0xFFF1U)
#define BLE_TX_CHAR_UUID16          (0xFFF2U)
#define BLE_MAX_TEXT_LEN            (180U)
#define BLE_ADV_ITVL_MIN            (160U)
#define BLE_ADV_ITVL_MAX            (240U)
#define BLE_MESSAGE_FIFO_CAPACITY   (512U)
#define BLE_MESSAGE_TEXT_MAX_LEN    (BLE_MAX_TEXT_LEN)
// clang-format on

typedef void (*ble_rx_cb_t)(const uint8_t* data, uint16_t len, void* user_ctx);

typedef enum {
    Ble_Chr_Rx = 1,
    Ble_Chr_Tx = 2,
} ble_chr_e;

typedef enum {
    Ble_Message_Dir_Rx = 1,
    Ble_Message_Dir_Tx = 2,
} ble_message_dir_e;

typedef struct {
    uint32_t seq;
    uint32_t timestamp_ms;
    uint16_t len;
    uint16_t reserved;
    ble_message_dir_e dir;
    char text[BLE_MESSAGE_TEXT_MAX_LEN + 1U];
} ble_message_item_s;

typedef struct {
    bool fifo_ready;
    uint16_t fifo_used;
    uint16_t fifo_capacity;
    uint32_t rx_total;
    uint32_t tx_total;
    uint32_t dropped_total;
} ble_message_stats_s;

typedef struct {
    bool init_done;
    bool start_req;
    bool sync_ready;
    bool adv_running;
    bool connected;
    bool notify_enabled;

    uint8_t addr_type;
    uint16_t conn_handle;
    uint16_t tx_val_handle;

    char rx_text[BLE_MAX_TEXT_LEN + 1U];
    char last_tx_text[BLE_MAX_TEXT_LEN + 1U];
    uint16_t last_tx_len;

    ble_rx_cb_t rx_cb;
    void* rx_cb_ctx;

    ble_message_item_s* message_fifo;
    uint16_t head;
    uint16_t tail;
    uint16_t used;
    uint32_t seq;
    uint32_t rx_total;
    uint32_t tx_total;
    uint32_t dropped_total;
    bool fifo_ready;
    bool has_last_rx;
    bool has_last_tx;
    ble_message_item_s last_rx;
    ble_message_item_s last_tx;
} ble_runtime_s;

esp_err_t ble_init_nimble(void);
esp_err_t ble_start_nimble(void);
void ble_stop_nimble(void);

bool ble_is_ready(void);
bool ble_is_connected(void);

void ble_set_rx_callback(ble_rx_cb_t cb, void* user_ctx);
esp_err_t ble_send_text(const char* text);

bool ble_message_fifo_pop(ble_message_item_s* item);
bool ble_message_get_last_rx(ble_message_item_s* item);
bool ble_message_get_last_tx(ble_message_item_s* item);
void ble_message_get_stats(ble_message_stats_s* stats);

#ifdef __cplusplus
}
#endif
