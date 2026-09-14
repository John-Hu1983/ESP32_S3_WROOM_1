#include "ble.h"

#include "sdkconfig.h"

#include <string.h>

#include "esp_log.h"

#if BLE_NIMBLE_AVAILABLE

#include "esp_bt.h"

#include "host/ble_hs.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "os/os_mbuf.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

#define TAG "ble"

static bool s_init_done;
static bool s_start_req;
static bool s_sync_ready;
static bool s_adv_running;
static bool s_connected;
static bool s_notify_enabled;

static uint8_t s_addr_type;
static uint16_t s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
static uint16_t s_tx_val_handle;

static char s_rx_text[BLE_MAX_TEXT_LEN + 1U];
static char s_last_tx_text[BLE_MAX_TEXT_LEN + 1U];
static uint16_t s_last_tx_len;

static ble_rx_cb_t s_rx_cb;
static void* s_rx_cb_ctx;

static int _ble_gatt_access(
    uint16_t conn_handle,
    uint16_t attr_handle,
    struct ble_gatt_access_ctxt* ctxt,
    void* arg
);

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
        .val_handle = &s_tx_val_handle,
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

static int _ble_gap_event(struct ble_gap_event* event, void* arg);

/*
 * BLE workflow map (NimBLE enabled)
 *
 * 1) Startup intent:
 *    - ble_start() sets s_start_req=true.
 *    - If needed, it calls ble_init() to boot NimBLE and register GATT.
 *
 * 2) Stack sync gate:
 *    - _ble_on_sync() runs after host/controller sync.
 *    - Only after s_sync_ready=true can advertising actually start.
 *
 * 3) Advertising:
 *    - _ble_start_adv() publishes device name + service UUID.
 *    - It skips safely when start is not requested, already connected, or
 *      advertising is already running.
 *
 * 4) Connection lifecycle (GAP events):
 *    - CONNECT success: set s_connected=true and save s_conn_handle.
 *    - DISCONNECT: clear runtime link flags and auto restart advertising.
 *    - SUBSCRIBE on TX char: update s_notify_enabled.
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
 *    - ble_stop() clears start intent, terminates active link,
 *      stops advertising, then stops/deinitializes NimBLE cleanly.
 */

/*
 * brief  : _ble_trim_eol.
 * input  : text is a writable string buffer; len is the current payload length.
 * output : trimmed length after removing trailing CR/LF bytes.
 * type   : private
 * theme  : normalize incoming BLE text so command parsing stays stable across
 *          clients that append different line endings.
 */
static uint16_t _ble_trim_eol(char* text, uint16_t len)
{
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
 * brief  : _ble_start_adv.
 * input  : none, uses internal BLE runtime flags and configured UUID/name.
 * output : ESP_OK when advertising is started or intentionally skipped.
 * type   : private
 * theme  : keep all advertising preconditions and payload configuration in one
 *          place to avoid inconsistent GAP state transitions.
 */
static esp_err_t _ble_start_adv(void)
{
    int rc = 0;
    struct ble_hs_adv_fields fields;
    struct ble_hs_adv_fields rsp_fields;
    struct ble_gap_adv_params adv_params;
    ble_uuid16_t service_uuid = BLE_UUID16_INIT(BLE_SERVICE_UUID16);
    uint8_t name_len = (uint8_t)strlen(BLE_DEVICE_NAME);

    if (!s_start_req || !s_sync_ready || s_connected || s_adv_running) {
        ESP_LOGI(
            TAG,
            "Skip adv start: start_req=%d sync_ready=%d connected=%d adv_running=%d",
            s_start_req ? 1 : 0,
            s_sync_ready ? 1 : 0,
            s_connected ? 1 : 0,
            s_adv_running ? 1 : 0
        );
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

    rc = ble_gap_adv_start(
        s_addr_type,
        NULL,
        BLE_HS_FOREVER,
        &adv_params,
        _ble_gap_event,
        NULL
    );
    if (rc == BLE_HS_EBUSY) {
        ESP_LOGW(TAG, "adv start busy, stop and retry");
        (void)ble_gap_adv_stop();
        rc = ble_gap_adv_start(
            s_addr_type,
            NULL,
            BLE_HS_FOREVER,
            &adv_params,
            _ble_gap_event,
            NULL
        );
    }
    if (rc == BLE_HS_EALREADY) {
        s_adv_running = true;
        ESP_LOGI(TAG, "Advertising already running");
        return ESP_OK;
    }
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gap_adv_start failed: %d", rc);
        return ESP_FAIL;
    }

    s_adv_running = true;
    ESP_LOGI(
        TAG,
        "Advertising started, name=%s interval=[%u,%u]",
        BLE_DEVICE_NAME,
        (unsigned)BLE_ADV_ITVL_MIN,
        (unsigned)BLE_ADV_ITVL_MAX
    );
    return ESP_OK;
}

/*
 * brief  : _ble_push_notify.
 * input  : data points to bytes to send; len is the payload length.
 * output : ESP_OK on success/ignored state, otherwise an ESP error code.
 * type   : private
 * theme  : hide ATT fragmentation details and send notifications in safe-sized
 *          chunks so upper layers can send text with a simple API.
 */
static esp_err_t _ble_push_notify(const uint8_t* data, uint16_t len)
{
    int rc = 0;
    uint16_t offset = 0U;
    uint16_t chunk = 0U;
    struct os_mbuf* om = NULL;

    if ((data == NULL) || (len == 0U)) {
        return ESP_OK;
    }

    if (!s_connected || !s_notify_enabled
        || (s_conn_handle == BLE_HS_CONN_HANDLE_NONE)) {
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

        rc = ble_gatts_notify_custom(s_conn_handle, s_tx_val_handle, om);
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
 * theme  : translate connect/disconnect/subscribe events into module runtime
 *          flags and automatic re-advertise behavior.
 */
static int _ble_gap_event(struct ble_gap_event* event, void* arg)
{
    (void)arg;

    if (event == NULL) {
        return 0;
    }

    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        s_adv_running = false;
        if (event->connect.status == 0) {
            s_connected = true;
            s_conn_handle = event->connect.conn_handle;
            ESP_LOGI(TAG, "BLE connected, conn_handle=%u", (unsigned)s_conn_handle);
        }
        else {
            s_connected = false;
            s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
            ESP_LOGW(TAG, "BLE connect failed, status=%d", event->connect.status);
            _ble_start_adv();
        }
        return 0;

    case BLE_GAP_EVENT_DISCONNECT:
        s_connected = false;
        s_notify_enabled = false;
        s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
        s_adv_running = false;
        ESP_LOGI(TAG, "BLE disconnected, reason=%d", event->disconnect.reason);
        _ble_start_adv();
        return 0;

    case BLE_GAP_EVENT_ADV_COMPLETE:
        s_adv_running = false;
        _ble_start_adv();
        return 0;

    case BLE_GAP_EVENT_SUBSCRIBE:
        if (event->subscribe.attr_handle == s_tx_val_handle) {
            s_notify_enabled = (event->subscribe.cur_notify != 0);
            ESP_LOGI(
                TAG,
                "Notify %s, conn_handle=%u",
                s_notify_enabled ? "enabled" : "disabled",
                (unsigned)event->subscribe.conn_handle
            );
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
 * theme  : provide one data path for BLE RX writes and TX reads, including
 *          command parsing and default response policy.
 */
static int _ble_gatt_access(
    uint16_t conn_handle,
    uint16_t attr_handle,
    struct ble_gatt_access_ctxt* ctxt,
    void* arg
)
{
    int rc = 0;
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

        rc = os_mbuf_copydata(ctxt->om, 0, rx_len, s_rx_text);
        if (rc != 0) {
            return BLE_ATT_ERR_UNLIKELY;
        }

        s_rx_text[rx_len] = '\0';
        rx_len = _ble_trim_eol(s_rx_text, rx_len);
        ESP_LOGI(TAG, "RX: %s", s_rx_text);

        if (s_rx_cb != NULL) {
            s_rx_cb((const uint8_t*)s_rx_text, rx_len, s_rx_cb_ctx);
        }

        if ((rx_len == 4U) && (memcmp(s_rx_text, "PING", 4U) == 0)) {
            ble_send_text("PONG");
        }
        else if ((rx_len > 5U) && (memcmp(s_rx_text, "ECHO ", 5U) == 0)) {
            ble_send_text(&s_rx_text[5]);
        }
        else {
            ble_send_text("ACK");
        }

        return 0;
    }

    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        rc = os_mbuf_append(ctxt->om, s_last_tx_text, s_last_tx_len);
        return (rc == 0) ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    }

    return BLE_ATT_ERR_UNLIKELY;
}

/*
 * brief  : _ble_on_reset.
 * input  : reason is the NimBLE reset reason code.
 * output : none.
 * type   : private
 * theme  : clear sync/advertise state after host reset so recovery starts from
 *          a known baseline.
 */
static void _ble_on_reset(int reason)
{
    s_sync_ready = false;
    s_adv_running = false;
    ESP_LOGW(TAG, "NimBLE reset, reason=%d", reason);
}

/*
 * brief  : _ble_on_sync.
 * input  : none, called by NimBLE when host and controller are synchronized.
 * output : none.
 * type   : private
 * theme  : acquire address identity and gate first advertising until BLE stack
 *          synchronization is fully complete.
 */
static void _ble_on_sync(void)
{
    int rc = ble_hs_id_infer_auto(0, &s_addr_type);
    uint8_t addr_val[6] = { 0 };
    esp_err_t adv_err = ESP_OK;

    rc = ble_hs_util_ensure_addr(0);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_hs_util_ensure_addr failed: %d", rc);
        return;
    }

    rc = ble_hs_id_infer_auto(0, &s_addr_type);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_hs_id_infer_auto failed: %d", rc);
        return;
    }

    rc = ble_hs_id_copy_addr(s_addr_type, addr_val, NULL);
    if (rc == 0) {
        ESP_LOGI(
            TAG,
            "BLE addr type=%d addr=%02X:%02X:%02X:%02X:%02X:%02X",
            (int)s_addr_type,
            addr_val[5],
            addr_val[4],
            addr_val[3],
            addr_val[2],
            addr_val[1],
            addr_val[0]
        );
    }

    s_sync_ready = true;
    ESP_LOGI(TAG, "BLE sync done, start_req=%d", s_start_req ? 1 : 0);
    if (s_start_req) {
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
 * theme  : run NimBLE host loop in its own FreeRTOS task to keep BLE protocol
 *          processing isolated from application control flow.
 */
static void _ble_host_task(void* param)
{
    (void)param;
    nimble_port_run();
    nimble_port_freertos_deinit();
}

/*
 * brief  : ble_init.
 * input  : none.
 * output : ESP_OK on success, otherwise an ESP error code.
 * type   : public
 * theme  : perform one-time NimBLE initialization and GATT database setup so
 *          all later BLE operations run on deterministic service definitions.
 */
esp_err_t ble_init(void)
{
    int rc = 0;
    esp_err_t err = ESP_OK;
    esp_bt_controller_status_t bt_status = ESP_BT_CONTROLLER_STATUS_IDLE;

    if (s_init_done) {
        return ESP_OK;
    }

    bt_status = esp_bt_controller_get_status();
    ESP_LOGI(TAG, "BT controller status before init: %d", (int)bt_status);

    /* Reset runtime state before starting NimBLE host task to avoid on_sync race. */
    s_sync_ready = false;
    s_adv_running = false;
    s_connected = false;
    s_notify_enabled = false;
    s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
    s_last_tx_len = 0U;
    s_last_tx_text[0] = '\0';
    s_rx_text[0] = '\0';

    err = nimble_port_init();
    if (err != ESP_OK) {
        bt_status = esp_bt_controller_get_status();
        ESP_LOGE(
            TAG,
            "nimble_port_init failed: %s, bt_status=%d",
            esp_err_to_name(err),
            (int)bt_status
        );
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

    s_init_done = true;

    nimble_port_freertos_init(_ble_host_task);

    ESP_LOGI(TAG, "BLE init done");
    return ESP_OK;
}

/*
 * brief  : ble_start.
 * input  : none.
 * output : ESP_OK when start request is accepted; otherwise an ESP error code.
 * type   : public
 * theme  : provide an idempotent API that records start intent and starts
 *          advertising immediately or right after BLE sync.
 */
esp_err_t ble_start(void)
{
    esp_err_t adv_err = ESP_OK;

    s_start_req = true;

    if (!s_init_done) {
        esp_err_t err = ble_init();
        if (err != ESP_OK) {
            return err;
        }
    }

    adv_err = _ble_start_adv();
    if ((adv_err == ESP_OK) && !s_sync_ready) {
        ESP_LOGI(TAG, "Start requested, waiting for BLE sync");
    }

    return adv_err;
}

/*
 * brief  : ble_stop.
 * input  : none.
 * output : none.
 * type   : public
 * theme  : shut down BLE in a controlled order (terminate link, stop adv,
 *          stop/deinit host) to avoid stale runtime state.
 */
void ble_stop(void)
{
    int stop_rc = 0;

    if (!s_init_done) {
        return;
    }

    s_start_req = false;

    if (s_connected && (s_conn_handle != BLE_HS_CONN_HANDLE_NONE)) {
        ble_gap_terminate(s_conn_handle, BLE_ERR_REM_USER_CONN_TERM);
    }

    if (s_adv_running) {
        ble_gap_adv_stop();
        s_adv_running = false;
    }

    stop_rc = nimble_port_stop();
    if (stop_rc != 0) {
        ESP_LOGW(TAG, "nimble_port_stop rc=%d", stop_rc);
    }
    nimble_port_deinit();

    s_init_done = false;
    s_sync_ready = false;
    s_connected = false;
    s_notify_enabled = false;
    s_conn_handle = BLE_HS_CONN_HANDLE_NONE;

    ESP_LOGI(TAG, "BLE stopped");
}

/*
 * brief  : ble_is_ready.
 * input  : none.
 * output : true when BLE init and stack sync are both complete.
 * type   : public
 * theme  : expose a simple readiness gate for callers before sending data or
 *          depending on advertising behavior.
 */
bool ble_is_ready(void)
{
    return s_init_done && s_sync_ready;
}

/*
 * brief  : ble_is_connected.
 * input  : none.
 * output : true when a BLE central is currently connected.
 * type   : public
 * theme  : let upper modules query link status without direct dependency on
 *          NimBLE connection handles.
 */
bool ble_is_connected(void)
{
    return s_connected;
}

/*
 * brief  : ble_set_rx_callback.
 * input  : cb is the receive callback; user_ctx is forwarded user context.
 * output : none.
 * type   : public
 * theme  : decouple transport and application logic by registering a callback
 *          that consumes BLE RX text payloads.
 */
void ble_set_rx_callback(ble_rx_cb_t cb, void* user_ctx)
{
    s_rx_cb = cb;
    s_rx_cb_ctx = user_ctx;
}

/*
 * brief  : ble_send_text.
 * input  : text points to a null-terminated string to transmit.
 * output : ESP_OK on success/ignored notify state, otherwise an ESP error code.
 * type   : public
 * theme  : unify outbound text handling by caching the latest value for GATT
 *          reads and pushing notifications when the link allows it.
 */
esp_err_t ble_send_text(const char* text)
{
    size_t text_len = 0U;

    if (text == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    text_len = strnlen(text, BLE_MAX_TEXT_LEN);
    memcpy(s_last_tx_text, text, text_len);
    s_last_tx_text[text_len] = '\0';
    s_last_tx_len = (uint16_t)text_len;

    return _ble_push_notify((const uint8_t*)s_last_tx_text, s_last_tx_len);
}

#else

#define TAG "ble"

/*
 * brief  : ble_init.
 * input  : none.
 * output : ESP_ERR_NOT_SUPPORTED.
 * type   : public
 * theme  : provide a stable API surface when BLE is disabled at build time,
 *          while clearly reporting unsupported capability.
 */
esp_err_t ble_init(void)
{
    ESP_LOGW(TAG, "BLE is disabled in sdkconfig");
    return ESP_ERR_NOT_SUPPORTED;
}

/*
 * brief  : ble_start.
 * input  : none.
 * output : ESP_ERR_NOT_SUPPORTED.
 * type   : public
 * theme  : keep caller flow predictable by returning a deterministic failure
 *          code instead of missing symbols or undefined behavior.
 */
esp_err_t ble_start(void)
{
    return ESP_ERR_NOT_SUPPORTED;
}

/*
 * brief  : ble_stop.
 * input  : none.
 * output : none.
 * type   : public
 * theme  : preserve stop-call compatibility in no-BLE builds as a no-op.
 */
void ble_stop(void)
{}

/*
 * brief  : ble_is_ready.
 * input  : none.
 * output : always false.
 * type   : public
 * theme  : expose explicit unavailable state for feature gating in upper
 *          modules when BLE support is compiled out.
 */
bool ble_is_ready(void)
{
    return false;
}

/*
 * brief  : ble_is_connected.
 * input  : none.
 * output : always false.
 * type   : public
 * theme  : keep connection-status checks safe and deterministic in no-BLE
 *          firmware variants.
 */
bool ble_is_connected(void)
{
    return false;
}

/*
 * brief  : ble_set_rx_callback.
 * input  : cb is ignored; user_ctx is ignored.
 * output : none.
 * type   : public
 * theme  : preserve API compatibility so application code can register hooks
 *          unconditionally across BLE and non-BLE builds.
 */
void ble_set_rx_callback(ble_rx_cb_t cb, void* user_ctx)
{
    (void)cb;
    (void)user_ctx;
}

/*
 * brief  : ble_send_text.
 * input  : text is ignored.
 * output : ESP_ERR_NOT_SUPPORTED.
 * type   : public
 * theme  : provide explicit runtime feedback when sending is requested in a
 *          firmware image without BLE capability.
 */
esp_err_t ble_send_text(const char* text)
{
    (void)text;
    return ESP_ERR_NOT_SUPPORTED;
}

#endif

