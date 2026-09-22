/*
 * BLE workflow map (NimBLE enabled)
 *
 * 1) Startup intent:
 *    - ble_start_nimble() sets start_req=true.
 *    - If needed, it calls ble_init_nimble() to boot NimBLE and register GATT.
 *
 * 2) Stack sync gate:
 *    - _ble_on_sync() runs after host/controller sync.
 *    - Only after sync_ready=true can advertising actually start.
 *
 * 3) Advertising:
 *    - _ble_start_adv() publishes device name + service UUID.
 *    - It skips safely when start is not requested, already connected, or
 *      advertising is already running.
 *
 * 4) Connection lifecycle (GAP events):
 *    - CONNECT success: set connected=true and save conn_handle.
 *    - DISCONNECT: clear runtime link flags and auto restart advertising.
 *    - SUBSCRIBE on TX char: update notify_enabled.
 *
 * 5) Data path (GATT access callback):
 *    - RX characteristic write -> _ble_gatt_access() receives text,
 *      trims CR/LF, notifies optional app callback, and generates reply text.
 *    - TX characteristic read returns the latest cached response.
 *
 * 6) Outbound notify:
 *    - ble_send_text() caches latest TX text for read operations.
 *    - _ble_push_notify() sends notify chunks when connected and
 *      notifications are enabled by the central.
 *
 * 7) Shutdown:
 *    - ble_stop_nimble() clears start intent, terminates active link,
 *      stops advertising, then stops/deinitializes NimBLE cleanly.
 */
#include "ble.h"

#include "sdkconfig.h"

#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"

#include "user/communication/protocol/at_cmd.h"

#include "esp_bt.h"

#include "host/ble_hs.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "os/os_mbuf.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

#define TAG "ble"

static ble_runtime_s* s_ble_ctx;
static portMUX_TYPE s_message_lock = portMUX_INITIALIZER_UNLOCKED;

/*
 * brief  : _ble_ctx_ensure.
 * input  : none.
 * output : ESP_OK when runtime context is ready.
 * type   : private
 * theory : move BLE state from static DRAM to dynamic PSRAM-backed storage.
 */
static esp_err_t _ble_ctx_ensure(void) {
    size_t ctx_bytes = sizeof(ble_runtime_s);

    if (s_ble_ctx != NULL) {
        return ESP_OK;
    }

    s_ble_ctx = (ble_runtime_s*)heap_caps_calloc(1,
                                                 ctx_bytes,
                                                 MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (s_ble_ctx != NULL) {
        s_ble_ctx->conn_handle = BLE_HS_CONN_HANDLE_NONE;
        ESP_LOGI(TAG, "BLE runtime context in PSRAM, bytes=%u", (unsigned)ctx_bytes);
        return ESP_OK;
    }

    s_ble_ctx = (ble_runtime_s*)heap_caps_calloc(1,
                                                 ctx_bytes,
                                                 MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (s_ble_ctx != NULL) {
        s_ble_ctx->conn_handle = BLE_HS_CONN_HANDLE_NONE;
        ESP_LOGW(TAG,
                 "BLE runtime context fallback to internal RAM, bytes=%u",
                 (unsigned)ctx_bytes);
        return ESP_OK;
    }

    ESP_LOGE(TAG, "BLE runtime context alloc failed, bytes=%u", (unsigned)ctx_bytes);
    return ESP_ERR_NO_MEM;
}

static int _ble_gatt_access(uint16_t conn_handle,
                            uint16_t attr_handle,
                            struct ble_gatt_access_ctxt* ctxt,
                            void* arg);

static struct ble_gatt_chr_def s_ble_chrs[] = {
    {
        .uuid = BLE_UUID16_DECLARE(BLE_RX_CHAR_UUID16),
        .access_cb = _ble_gatt_access,
        .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
    },
    {
        .uuid = BLE_UUID16_DECLARE(BLE_TX_CHAR_UUID16),
        .access_cb = _ble_gatt_access,
        .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
        .val_handle = NULL,
    },
    { 0 }
};

static const struct ble_gatt_svc_def s_ble_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = BLE_UUID16_DECLARE(BLE_SERVICE_UUID16),
        .characteristics = s_ble_chrs,
    },
    { 0 }
};

/*
 * brief  : _ble_message_fifo_init.
 * input  : none.
 * output : none.
 * type   : private
 * theory : allocate a large FIFO in PSRAM for BLE RX/TX message traces and
 *          fall back to internal RAM only if PSRAM allocation is unavailable.
 */
static void _ble_message_fifo_init(void) {
    size_t fifo_bytes = sizeof(ble_message_item_s) * BLE_MESSAGE_FIFO_CAPACITY;

    if (s_ble_ctx->message_fifo != NULL) {
        return;
    }

    s_ble_ctx->message_fifo =
        (ble_message_item_s*)heap_caps_malloc(fifo_bytes,
                                              MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (s_ble_ctx->message_fifo != NULL) {
        s_ble_ctx->fifo_ready = true;
        ESP_LOGI(TAG,
                 "BLE message FIFO ready in PSRAM, items=%u bytes=%u",
                 (unsigned)BLE_MESSAGE_FIFO_CAPACITY,
                 (unsigned)fifo_bytes);
        return;
    }

    s_ble_ctx->message_fifo =
        (ble_message_item_s*)heap_caps_malloc(fifo_bytes,
                                              MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (s_ble_ctx->message_fifo != NULL) {
        s_ble_ctx->fifo_ready = true;
        ESP_LOGW(TAG,
                 "BLE message FIFO fallback to internal RAM, items=%u bytes=%u",
                 (unsigned)BLE_MESSAGE_FIFO_CAPACITY,
                 (unsigned)fifo_bytes);
        return;
    }

    s_ble_ctx->fifo_ready = false;
    ESP_LOGE(TAG, "BLE message FIFO alloc failed, bytes=%u", (unsigned)fifo_bytes);
}

/*
 * brief  : _ble_message_capture.
 * input  : dir indicates RX/TX; text points to payload bytes; len is text size.
 * output : none.
 * type   : private
 * theory : capture BLE traffic into a FIFO and track latest RX/TX snapshots for
 *          message UI without affecting transport behavior.
 */
static void _ble_message_capture(ble_message_dir_e dir,
                                 const char* text,
                                 uint16_t len) {
    ble_message_item_s item;
    uint16_t copy_len = len;

    if (text == NULL) {
        return;
    }

    if (copy_len > BLE_MESSAGE_TEXT_MAX_LEN) {
        copy_len = BLE_MESSAGE_TEXT_MAX_LEN;
    }

    memset(&item, 0, sizeof(item));
    item.timestamp_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
    item.len = copy_len;
    item.dir = dir;
    memcpy(item.text, text, copy_len);
    item.text[copy_len] = '\0';

    taskENTER_CRITICAL(&s_message_lock);

    if (dir == Ble_Message_Dir_Rx) {
        s_ble_ctx->rx_total++;
        s_ble_ctx->last_rx = item;
        s_ble_ctx->has_last_rx = true;
    }
    else {
        s_ble_ctx->tx_total++;
        s_ble_ctx->last_tx = item;
        s_ble_ctx->has_last_tx = true;
    }

    s_ble_ctx->seq++;
    item.seq = s_ble_ctx->seq;

    if (s_ble_ctx->fifo_ready && (s_ble_ctx->message_fifo != NULL)) {
        if (s_ble_ctx->used >= BLE_MESSAGE_FIFO_CAPACITY) {
            s_ble_ctx->tail =
                (uint16_t)((s_ble_ctx->tail + 1U) % BLE_MESSAGE_FIFO_CAPACITY);
            s_ble_ctx->used--;
            s_ble_ctx->dropped_total++;
        }

        s_ble_ctx->message_fifo[s_ble_ctx->head] = item;
        s_ble_ctx->head =
            (uint16_t)((s_ble_ctx->head + 1U) % BLE_MESSAGE_FIFO_CAPACITY);
        s_ble_ctx->used++;
    }
    else {
        s_ble_ctx->dropped_total++;
    }

    taskEXIT_CRITICAL(&s_message_lock);
}

/*
 * brief  : _ble_trim_eol.
 * input  : text is a writable string buffer; len is the current payload length.
 * output : trimmed length after removing trailing CR/LF bytes.
 * type   : private
 * theory : normalize incoming BLE text so command parsing stays stable across
 *          clients that append different line endings.
 */
static uint16_t _ble_trim_eol(char* text, uint16_t len) {
    if (text == NULL) {
        return 0U;
    }

    while ((len > 0U) && ((text[len - 1U] == '\r') || (text[len - 1U] == '\n'))) {
        text[len - 1U] = '\0';
        len--;
    }

    return len;
}

/*
 * brief  : _ble_start_adv_with_gap_cb.
 * input  : gap_cb is GAP event callback; other inputs come from module state.
 * output : ESP_OK when advertising is started or intentionally skipped.
 * type   : private
 * theory : keep all advertising preconditions and payload configuration in one
 *          place to avoid inconsistent GAP state transitions.
 */
static esp_err_t _ble_start_adv_with_gap_cb(int (*gap_cb)(struct ble_gap_event* event,
                                                          void* arg)) {
    int rc = 0;
    struct ble_hs_adv_fields fields;
    struct ble_hs_adv_fields rsp_fields;
    struct ble_gap_adv_params adv_params;
    ble_uuid16_t service_uuid = BLE_UUID16_INIT(BLE_SERVICE_UUID16);
    uint8_t name_len = (uint8_t)strlen(BLE_DEVICE_NAME);

    if (gap_cb == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_ble_ctx->start_req || !s_ble_ctx->sync_ready || s_ble_ctx->connected
        || s_ble_ctx->adv_running) {
        ESP_LOGI(
            TAG,
            "Skip adv start: start_req=%d sync_ready=%d connected=%d adv_running=%d",
            s_ble_ctx->start_req ? 1 : 0,
            s_ble_ctx->sync_ready ? 1 : 0,
            s_ble_ctx->connected ? 1 : 0,
            s_ble_ctx->adv_running ? 1 : 0);
        return ESP_OK;
    }

    memset(&fields, 0, sizeof(fields));
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.name = (const uint8_t*)BLE_DEVICE_NAME;
    fields.name_len = name_len;
    fields.name_is_complete = 1;

    rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gap_adv_set_fields failed: %d", rc);
        return ESP_FAIL;
    }

    memset(&rsp_fields, 0, sizeof(rsp_fields));
    rsp_fields.uuids16 = &service_uuid;
    rsp_fields.num_uuids16 = 1;
    rsp_fields.uuids16_is_complete = 1;

    rc = ble_gap_adv_rsp_set_fields(&rsp_fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gap_adv_rsp_set_fields failed: %d", rc);
        return ESP_FAIL;
    }

    memset(&adv_params, 0, sizeof(adv_params));
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    adv_params.itvl_min = BLE_ADV_ITVL_MIN;
    adv_params.itvl_max = BLE_ADV_ITVL_MAX;

    rc = ble_gap_adv_start(s_ble_ctx->addr_type,
                           NULL,
                           BLE_HS_FOREVER,
                           &adv_params,
                           gap_cb,
                           NULL);
    if (rc == BLE_HS_EBUSY) {
        ESP_LOGW(TAG, "adv start busy, stop and retry");
        (void)ble_gap_adv_stop();
        rc = ble_gap_adv_start(s_ble_ctx->addr_type,
                               NULL,
                               BLE_HS_FOREVER,
                               &adv_params,
                               gap_cb,
                               NULL);
    }
    if (rc == BLE_HS_EALREADY) {
        s_ble_ctx->adv_running = true;
        ESP_LOGI(TAG, "Advertising already running");
        return ESP_OK;
    }
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gap_adv_start failed: %d", rc);
        return ESP_FAIL;
    }

    s_ble_ctx->adv_running = true;
    ESP_LOGI(TAG,
             "Advertising started, name=%s interval=[%u,%u]",
             BLE_DEVICE_NAME,
             (unsigned)BLE_ADV_ITVL_MIN,
             (unsigned)BLE_ADV_ITVL_MAX);
    return ESP_OK;
}

/*
 * brief  : _ble_push_notify.
 * input  : data points to bytes to send; len is the payload length.
 * output : ESP_OK on success/ignored state, otherwise an ESP error code.
 * type   : private
 * theory : hide ATT fragmentation details and send notifications in safe-sized
 *          chunks so upper layers can send text with a simple API.
 */
static esp_err_t _ble_push_notify(const uint8_t* data, uint16_t len) {
    int rc = 0;
    uint16_t offset = 0U;
    uint16_t chunk = 0U;
    struct os_mbuf* om = NULL;

    if ((data == NULL) || (len == 0U)) {
        return ESP_OK;
    }

    if (!s_ble_ctx->connected || !s_ble_ctx->notify_enabled
        || (s_ble_ctx->conn_handle == BLE_HS_CONN_HANDLE_NONE)) {
        return ESP_OK;
    }

    while (offset < len) {
        chunk = (uint16_t)(len - offset);
        if (chunk > BLE_NOTIFY_CHUNK_LEN) {
            chunk = BLE_NOTIFY_CHUNK_LEN;
        }

        om = ble_hs_mbuf_from_flat(&data[offset], chunk);
        if (om == NULL) {
            return ESP_ERR_NO_MEM;
        }

        rc = ble_gatts_notify_custom(s_ble_ctx->conn_handle,
                                     s_ble_ctx->tx_val_handle,
                                     om);
        if (rc != 0) {
            ESP_LOGE(TAG, "ble_gatts_notify_custom failed: %d", rc);
            return ESP_FAIL;
        }

        offset = (uint16_t)(offset + chunk);
    }

    return ESP_OK;
}

/*
 * brief  : _ble_gap_event.
 * input  : event is the NimBLE GAP event; arg is unused callback context.
 * output : NimBLE callback status code, always 0 for handled events.
 * type   : private
 * theory : translate connect/disconnect/subscribe events into module runtime
 *          flags and automatic re-advertise behavior.
 */
static int _ble_gap_event(struct ble_gap_event* event, void* arg) {
    (void)arg;

    if (event == NULL) {
        return 0;
    }

    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        s_ble_ctx->adv_running = false;
        if (event->connect.status == 0) {
            s_ble_ctx->connected = true;
            s_ble_ctx->conn_handle = event->connect.conn_handle;
            ESP_LOGI(TAG,
                     "BLE connected, conn_handle=%u",
                     (unsigned)s_ble_ctx->conn_handle);
        }
        else {
            s_ble_ctx->connected = false;
            s_ble_ctx->conn_handle = BLE_HS_CONN_HANDLE_NONE;
            ESP_LOGW(TAG, "BLE connect failed, status=%d", event->connect.status);
            _ble_start_adv_with_gap_cb(_ble_gap_event);
        }
        return 0;

    case BLE_GAP_EVENT_DISCONNECT:
        s_ble_ctx->connected = false;
        s_ble_ctx->notify_enabled = false;
        s_ble_ctx->conn_handle = BLE_HS_CONN_HANDLE_NONE;
        s_ble_ctx->adv_running = false;
        ESP_LOGI(TAG, "BLE disconnected, reason=%d", event->disconnect.reason);
        _ble_start_adv_with_gap_cb(_ble_gap_event);
        return 0;

    case BLE_GAP_EVENT_ADV_COMPLETE:
        s_ble_ctx->adv_running = false;
        _ble_start_adv_with_gap_cb(_ble_gap_event);
        return 0;

    case BLE_GAP_EVENT_SUBSCRIBE:
        if (event->subscribe.attr_handle == s_ble_ctx->tx_val_handle) {
            s_ble_ctx->notify_enabled = (event->subscribe.cur_notify != 0);
            ESP_LOGI(TAG,
                     "Notify %s, conn_handle=%u",
                     s_ble_ctx->notify_enabled ? "enabled" : "disabled",
                     (unsigned)event->subscribe.conn_handle);
        }
        return 0;

    default:
        return 0;
    }
}

/*
 * brief  : _ble_gatt_access.
 * input  : NimBLE GATT access context for read/write operations.
 * output : BLE ATT status code for the current characteristic operation.
 * type   : private
 * theory : provide one data path for BLE RX writes and TX reads, including
 *          command parsing and default response policy.
 */
static int _ble_gatt_access(uint16_t conn_handle,
                            uint16_t attr_handle,
                            struct ble_gatt_access_ctxt* ctxt,
                            void* arg) {
    int rc = 0;
    esp_err_t at_ret = ESP_OK;
    uint16_t rx_len = 0U;
    (void)arg;

    (void)conn_handle;
    (void)attr_handle;

    if (ctxt == NULL) {
        return BLE_ATT_ERR_UNLIKELY;
    }

    if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
        rx_len = OS_MBUF_PKTLEN(ctxt->om);
        if (rx_len > BLE_MAX_TEXT_LEN) {
            rx_len = BLE_MAX_TEXT_LEN;
        }

        rc = os_mbuf_copydata(ctxt->om, 0, rx_len, s_ble_ctx->rx_text);
        if (rc != 0) {
            return BLE_ATT_ERR_UNLIKELY;
        }

        s_ble_ctx->rx_text[rx_len] = '\0';
        rx_len = _ble_trim_eol(s_ble_ctx->rx_text, rx_len);
        ESP_LOGI(TAG, "RX: %s", s_ble_ctx->rx_text);
        _ble_message_capture(Ble_Message_Dir_Rx, s_ble_ctx->rx_text, rx_len);

        if (s_ble_ctx->rx_cb != NULL) {
            s_ble_ctx->rx_cb((const uint8_t*)s_ble_ctx->rx_text,
                             rx_len,
                             s_ble_ctx->rx_cb_ctx);
        }

        at_ret = at_cmd_parse(s_ble_ctx->rx_text);
        if (at_ret == ESP_OK) {
            return 0;
        }
        if (at_ret != ESP_ERR_NOT_FOUND) {
            ESP_LOGW(TAG, "AT parse status=%d", (int)at_ret);
        }

        // if ((rx_len == 4U) && (memcmp(s_ble_ctx->rx_text, "PING", 4U) == 0)) {
        //     ble_send_text("PONG");
        // }
        // else if ((rx_len > 5U) && (memcmp(s_ble_ctx->rx_text, "ECHO ", 5U) == 0)) {
        //     ble_send_text(&s_ble_ctx->rx_text[5]);
        // }
        // else {
        //     ble_send_text("ACK");
        // }

        return 0;
    }

    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        rc = os_mbuf_append(ctxt->om, s_ble_ctx->last_tx_text, s_ble_ctx->last_tx_len);
        return (rc == 0) ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    }

    return BLE_ATT_ERR_UNLIKELY;
}

/*
 * brief  : _ble_start_adv.
 * input  : none.
 * output : ESP_OK when advertising is started or intentionally skipped.
 * type   : private
 * theory : keep a fixed entry point for normal advertising startup while
 *          delegating real policy and GAP callback wiring to one helper.
 */
static esp_err_t _ble_start_adv(void) {
    return _ble_start_adv_with_gap_cb(_ble_gap_event);
}

/*
 * brief  : _ble_on_reset.
 * input  : reason is the NimBLE reset reason code.
 * output : none.
 * type   : private
 * theory : clear sync/advertise state after host reset so recovery starts from
 *          a known baseline.
 */
static void _ble_on_reset(int reason) {
    s_ble_ctx->sync_ready = false;
    s_ble_ctx->adv_running = false;
    ESP_LOGW(TAG, "NimBLE reset, reason=%d", reason);
}

/*
 * brief  : _ble_on_sync.
 * input  : none, called by NimBLE when host and controller are synchronized.
 * output : none.
 * type   : private
 * theory : acquire address identity and gate first advertising until BLE stack
 *          synchronization is fully complete.
 */
static void _ble_on_sync(void) {
    int rc = ble_hs_id_infer_auto(0, &s_ble_ctx->addr_type);
    uint8_t addr_val[6] = { 0 };
    esp_err_t adv_err = ESP_OK;

    rc = ble_hs_util_ensure_addr(0);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_hs_util_ensure_addr failed: %d", rc);
        return;
    }

    rc = ble_hs_id_infer_auto(0, &s_ble_ctx->addr_type);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_hs_id_infer_auto failed: %d", rc);
        return;
    }

    rc = ble_hs_id_copy_addr(s_ble_ctx->addr_type, addr_val, NULL);
    if (rc == 0) {
        ESP_LOGI(TAG,
                 "BLE addr type=%d addr=%02X:%02X:%02X:%02X:%02X:%02X",
                 (int)s_ble_ctx->addr_type,
                 addr_val[5],
                 addr_val[4],
                 addr_val[3],
                 addr_val[2],
                 addr_val[1],
                 addr_val[0]);
    }

    s_ble_ctx->sync_ready = true;
    ESP_LOGI(TAG, "BLE sync done, start_req=%d", s_ble_ctx->start_req ? 1 : 0);
    if (s_ble_ctx->start_req) {
        adv_err = _ble_start_adv();
        if (adv_err != ESP_OK) {
            ESP_LOGE(TAG, "start adv after sync failed: %s", esp_err_to_name(adv_err));
        }
    }
}

/*
 * brief  : _ble_host_task.
 * input  : param is unused task context.
 * output : none.
 * type   : private
 * theory : run NimBLE host loop in its own FreeRTOS task to keep BLE protocol
 *          processing isolated from application control flow.
 */
static void _ble_host_task(void* param) {
    (void)param;
    nimble_port_run();
    nimble_port_freertos_deinit();
}

/*
 * brief  : ble_init_nimble.
 * input  : none.
 * output : ESP_OK on success, otherwise an ESP error code.
 * type   : public
 * theory : perform one-time NimBLE initialization and GATT database setup so
 *          all later BLE operations run on deterministic service definitions.
 */
esp_err_t ble_init_nimble(void) {
    int rc = 0;
    esp_err_t err = ESP_OK;
    esp_bt_controller_status_t bt_status = ESP_BT_CONTROLLER_STATUS_IDLE;

    err = _ble_ctx_ensure();
    if (err != ESP_OK) {
        return err;
    }

    _ble_message_fifo_init();

    if (s_ble_ctx->init_done) {
        return ESP_OK;
    }

    // Reduce high-frequency NimBLE procedure logs (e.g. notify/att_handle) noise.
    esp_log_level_set("NimBLE", ESP_LOG_WARN);

    bt_status = esp_bt_controller_get_status();
    ESP_LOGI(TAG, "BT controller status before init: %d", (int)bt_status);

    /* Reset runtime state before starting NimBLE host task to avoid on_sync race. */
    s_ble_ctx->sync_ready = false;
    s_ble_ctx->adv_running = false;
    s_ble_ctx->connected = false;
    s_ble_ctx->notify_enabled = false;
    s_ble_ctx->conn_handle = BLE_HS_CONN_HANDLE_NONE;
    s_ble_ctx->last_tx_len = 0U;
    s_ble_ctx->last_tx_text[0] = '\0';
    s_ble_ctx->rx_text[0] = '\0';

    err = nimble_port_init();
    if (err != ESP_OK) {
        bt_status = esp_bt_controller_get_status();
        ESP_LOGE(TAG,
                 "nimble_port_init failed: %s, bt_status=%d",
                 esp_err_to_name(err),
                 (int)bt_status);
        return err;
    }

    ble_hs_cfg.reset_cb = _ble_on_reset;
    ble_hs_cfg.sync_cb = _ble_on_sync;

    ble_svc_gap_init();
    ble_svc_gatt_init();

    rc = ble_svc_gap_device_name_set(BLE_DEVICE_NAME);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_svc_gap_device_name_set failed: %d", rc);
        nimble_port_deinit();
        return ESP_FAIL;
    }

    s_ble_chrs[1].val_handle = &s_ble_ctx->tx_val_handle;

    rc = ble_gatts_count_cfg(s_ble_svcs);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gatts_count_cfg failed: %d", rc);
        nimble_port_deinit();
        return ESP_FAIL;
    }

    rc = ble_gatts_add_svcs(s_ble_svcs);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gatts_add_svcs failed: %d", rc);
        nimble_port_deinit();
        return ESP_FAIL;
    }

    s_ble_ctx->init_done = true;

    nimble_port_freertos_init(_ble_host_task);

    ESP_LOGI(TAG, "BLE init done");
    return ESP_OK;
}

/*
 * brief  : ble_start_nimble.
 * input  : none.
 * output : ESP_OK when start request is accepted; otherwise an ESP error code.
 * type   : public
 * theory : provide an idempotent API that records start intent and starts
 *          advertising immediately or right after BLE sync.
 */
esp_err_t ble_start_nimble(void) {
    esp_err_t adv_err = ESP_OK;
    esp_err_t ctx_err = ESP_OK;

    ctx_err = _ble_ctx_ensure();
    if (ctx_err != ESP_OK) {
        return ctx_err;
    }

    s_ble_ctx->start_req = true;

    if (!s_ble_ctx->init_done) {
        esp_err_t err = ble_init_nimble();
        if (err != ESP_OK) {
            return err;
        }
    }

    adv_err = _ble_start_adv();
    if ((adv_err == ESP_OK) && !s_ble_ctx->sync_ready) {
        ESP_LOGI(TAG, "Start requested, waiting for BLE sync");
    }

    return adv_err;
}

/*
 * brief  : ble_stop_nimble.
 * input  : none.
 * output : none.
 * type   : public
 * theory : shut down BLE in a controlled order (terminate link, stop adv,
 *          stop/deinit host) to avoid stale runtime state.
 */
void ble_stop_nimble(void) {
    int stop_rc = 0;

    if ((s_ble_ctx == NULL) || !s_ble_ctx->init_done) {
        return;
    }

    s_ble_ctx->start_req = false;

    if (s_ble_ctx->connected && (s_ble_ctx->conn_handle != BLE_HS_CONN_HANDLE_NONE)) {
        ble_gap_terminate(s_ble_ctx->conn_handle, BLE_ERR_REM_USER_CONN_TERM);
    }

    if (s_ble_ctx->adv_running) {
        ble_gap_adv_stop();
        s_ble_ctx->adv_running = false;
    }

    stop_rc = nimble_port_stop();
    if (stop_rc != 0) {
        ESP_LOGW(TAG, "nimble_port_stop rc=%d", stop_rc);
    }
    nimble_port_deinit();

    s_ble_ctx->init_done = false;
    s_ble_ctx->sync_ready = false;
    s_ble_ctx->connected = false;
    s_ble_ctx->notify_enabled = false;
    s_ble_ctx->conn_handle = BLE_HS_CONN_HANDLE_NONE;

    ESP_LOGI(TAG, "BLE stopped");
}

/*
 * brief  : ble_is_ready.
 * input  : none.
 * output : true when BLE init and stack sync are both complete.
 * type   : public
 * theory : expose a simple readiness gate for callers before sending data or
 *          depending on advertising behavior.
 */
bool ble_is_ready(void) {
    if (s_ble_ctx == NULL) {
        return false;
    }

    return s_ble_ctx->init_done && s_ble_ctx->sync_ready;
}

/*
 * brief  : ble_is_connected.
 * input  : none.
 * output : true when a BLE central is currently connected.
 * type   : public
 * theory : let upper modules query link status without direct dependency on
 *          NimBLE connection handles.
 */
bool ble_is_connected(void) {
    if (s_ble_ctx == NULL) {
        return false;
    }

    return s_ble_ctx->connected;
}

/*
 * brief  : ble_set_rx_callback.
 * input  : cb is the receive callback; user_ctx is forwarded user context.
 * output : none.
 * type   : public
 * theory : decouple transport and application logic by registering a callback
 *          that consumes BLE RX text payloads.
 */
void ble_set_rx_callback(ble_rx_cb_t cb, void* user_ctx) {
    esp_err_t ctx_err = _ble_ctx_ensure();

    if (ctx_err != ESP_OK) {
        ESP_LOGW(TAG, "set rx callback failed: %s", esp_err_to_name(ctx_err));
        return;
    }

    s_ble_ctx->rx_cb = cb;
    s_ble_ctx->rx_cb_ctx = user_ctx;
}

/*
 * brief  : ble_send_text.
 * input  : text points to a null-terminated string to transmit.
 * output : ESP_OK on success/ignored notify state, otherwise an ESP error code.
 * type   : public
 * theory : unify outbound text handling by caching the latest value for GATT
 *          reads and pushing notifications when the link allows it.
 */
esp_err_t ble_send_text(const char* text) {
    esp_err_t ctx_err = ESP_OK;
    size_t text_len = 0U;

    if (text == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    ctx_err = _ble_ctx_ensure();
    if (ctx_err != ESP_OK) {
        return ctx_err;
    }

    text_len = strnlen(text, BLE_MAX_TEXT_LEN);
    memcpy(s_ble_ctx->last_tx_text, text, text_len);
    s_ble_ctx->last_tx_text[text_len] = '\0';
    s_ble_ctx->last_tx_len = (uint16_t)text_len;
    _ble_message_capture(Ble_Message_Dir_Tx,
                         s_ble_ctx->last_tx_text,
                         s_ble_ctx->last_tx_len);

    return _ble_push_notify((const uint8_t*)s_ble_ctx->last_tx_text,
                            s_ble_ctx->last_tx_len);
}

/*
 * brief  : ble_message_fifo_pop.
 * input  : item receives one popped message record.
 * output : true when one record is popped; false when queue is empty/invalid.
 * type   : public
 * theory : expose a lock-protected single-consumer pop interface so UI/tasks
 *          can drain the trace FIFO without touching internal head/tail rules.
 */
bool ble_message_fifo_pop(ble_message_item_s* item) {
    bool has_item = false;

    if ((item == NULL) || (s_ble_ctx == NULL)) {
        return false;
    }

    taskENTER_CRITICAL(&s_message_lock);
    if (s_ble_ctx->fifo_ready && (s_ble_ctx->message_fifo != NULL)
        && (s_ble_ctx->used > 0U)) {
        *item = s_ble_ctx->message_fifo[s_ble_ctx->tail];
        s_ble_ctx->tail =
            (uint16_t)((s_ble_ctx->tail + 1U) % BLE_MESSAGE_FIFO_CAPACITY);
        s_ble_ctx->used--;
        has_item = true;
    }
    taskEXIT_CRITICAL(&s_message_lock);

    return has_item;
}

/*
 * brief  : ble_message_get_last_rx.
 * input  : item receives the latest RX snapshot.
 * output : true when RX snapshot exists; false otherwise.
 * type   : public
 * theory : provide O(1) access to the most recent RX message so callers do not
 *          need to scan or depend on FIFO retention to show latest state.
 */
bool ble_message_get_last_rx(ble_message_item_s* item) {
    bool has_item = false;

    if ((item == NULL) || (s_ble_ctx == NULL)) {
        return false;
    }

    taskENTER_CRITICAL(&s_message_lock);
    if (s_ble_ctx->has_last_rx) {
        *item = s_ble_ctx->last_rx;
        has_item = true;
    }
    taskEXIT_CRITICAL(&s_message_lock);

    return has_item;
}

/*
 * brief  : ble_message_get_last_tx.
 * input  : item receives the latest TX snapshot.
 * output : true when TX snapshot exists; false otherwise.
 * type   : public
 * theory : provide O(1) access to the most recent TX message for quick status
 *          display and diagnostics independent of FIFO consumption order.
 */
bool ble_message_get_last_tx(ble_message_item_s* item) {
    bool has_item = false;

    if ((item == NULL) || (s_ble_ctx == NULL)) {
        return false;
    }

    taskENTER_CRITICAL(&s_message_lock);
    if (s_ble_ctx->has_last_tx) {
        *item = s_ble_ctx->last_tx;
        has_item = true;
    }
    taskEXIT_CRITICAL(&s_message_lock);

    return has_item;
}

/*
 * brief  : ble_message_get_stats.
 * input  : stats receives aggregated FIFO and traffic counters.
 * output : none.
 * type   : public
 * theory : snapshot counters under one critical section so callers read a
 *          coherent view of queue usage and dropped/traffic totals.
 */
void ble_message_get_stats(ble_message_stats_s* stats) {
    if (stats == NULL) {
        return;
    }

    if (s_ble_ctx == NULL) {
        memset(stats, 0, sizeof(*stats));
        stats->fifo_capacity = BLE_MESSAGE_FIFO_CAPACITY;
        return;
    }

    taskENTER_CRITICAL(&s_message_lock);
    stats->fifo_ready = s_ble_ctx->fifo_ready;
    stats->fifo_used = s_ble_ctx->used;
    stats->fifo_capacity = BLE_MESSAGE_FIFO_CAPACITY;
    stats->rx_total = s_ble_ctx->rx_total;
    stats->tx_total = s_ble_ctx->tx_total;
    stats->dropped_total = s_ble_ctx->dropped_total;
    taskEXIT_CRITICAL(&s_message_lock);
}
