#include "user_ble.h"

#include "sdkconfig.h"

#include <string.h>

#include "esp_log.h"

#if defined(CONFIG_BT_ENABLED) && (CONFIG_BT_ENABLED == 1) && defined(CONFIG_BT_NIMBLE_ENABLED) && (CONFIG_BT_NIMBLE_ENABLED == 1)
#define USER_BLE_NIMBLE_AVAILABLE (1)
#else
#define USER_BLE_NIMBLE_AVAILABLE (0)
#endif

#if USER_BLE_NIMBLE_AVAILABLE

#include "esp_bt.h"

#include "host/ble_hs.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "os/os_mbuf.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "host/util/util.h"

#define TAG "user_ble"

#define USER_BLE_NOTIFY_CHUNK_LEN (20U)

typedef enum {
	User_Ble_Chr_Rx = 1,
	User_Ble_Chr_Tx = 2,
} user_ble_chr_e;

static bool s_init_done;
static bool s_start_req;
static bool s_sync_ready;
static bool s_adv_running;
static bool s_connected;
static bool s_notify_enabled;

static uint8_t s_addr_type;
static uint16_t s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
static uint16_t s_tx_val_handle;

static char s_rx_text[USER_BLE_MAX_TEXT_LEN + 1U];
static char s_last_tx_text[USER_BLE_MAX_TEXT_LEN + 1U];
static uint16_t s_last_tx_len;

static user_ble_rx_cb_t s_rx_cb;
static void* s_rx_cb_ctx;

static int _user_ble_gap_event(struct ble_gap_event* event, void* arg);

static uint16_t _user_ble_trim_eol(char* text, uint16_t len)
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

static esp_err_t _user_ble_start_adv(void)
{
	int rc = 0;
	struct ble_hs_adv_fields fields;
	struct ble_hs_adv_fields rsp_fields;
	struct ble_gap_adv_params adv_params;
	ble_uuid16_t service_uuid = BLE_UUID16_INIT(USER_BLE_SERVICE_UUID16);

	if (!s_start_req || !s_sync_ready || s_connected || s_adv_running) {
		ESP_LOGI(TAG,
				 "Skip adv start: start_req=%d sync_ready=%d connected=%d adv_running=%d",
				 s_start_req ? 1 : 0,
				 s_sync_ready ? 1 : 0,
				 s_connected ? 1 : 0,
				 s_adv_running ? 1 : 0);
		return ESP_OK;
	}

	memset(&fields, 0, sizeof(fields));
	fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
	fields.uuids16 = &service_uuid;
	fields.num_uuids16 = 1;
	fields.uuids16_is_complete = 1;
	fields.name = (const uint8_t*)USER_BLE_DEVICE_NAME;
	fields.name_len = (uint8_t)strlen(USER_BLE_DEVICE_NAME);
	fields.name_is_complete = 1;

	rc = ble_gap_adv_set_fields(&fields);
	if (rc != 0) {
		ESP_LOGE(TAG, "ble_gap_adv_set_fields failed: %d", rc);
		return ESP_FAIL;
	}

	memset(&rsp_fields, 0, sizeof(rsp_fields));
	rsp_fields.name = (const uint8_t*)USER_BLE_DEVICE_NAME;
	rsp_fields.name_len = (uint8_t)strlen(USER_BLE_DEVICE_NAME);
	rsp_fields.name_is_complete = 1;

	rc = ble_gap_adv_rsp_set_fields(&rsp_fields);
	if (rc != 0) {
		ESP_LOGE(TAG, "ble_gap_adv_rsp_set_fields failed: %d", rc);
		return ESP_FAIL;
	}

	memset(&adv_params, 0, sizeof(adv_params));
	adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
	adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;

	rc = ble_gap_adv_start(
		s_addr_type,
		NULL,
		BLE_HS_FOREVER,
		&adv_params,
		_user_ble_gap_event,
		NULL
	);
	if (rc != 0) {
		ESP_LOGE(TAG, "ble_gap_adv_start failed: %d", rc);
		return ESP_FAIL;
	}

	s_adv_running = true;
	ESP_LOGI(TAG, "Advertising started, name=%s", USER_BLE_DEVICE_NAME);
	return ESP_OK;
}

static esp_err_t _user_ble_push_notify(const uint8_t* data, uint16_t len)
{
	int rc = 0;
	uint16_t offset = 0U;
	uint16_t chunk = 0U;
	struct os_mbuf* om = NULL;

	if ((data == NULL) || (len == 0U)) {
		return ESP_OK;
	}

	if (!s_connected || !s_notify_enabled || (s_conn_handle == BLE_HS_CONN_HANDLE_NONE)) {
		return ESP_OK;
	}

	while (offset < len) {
		chunk = (uint16_t)(len - offset);
		if (chunk > USER_BLE_NOTIFY_CHUNK_LEN) {
			chunk = USER_BLE_NOTIFY_CHUNK_LEN;
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

static int _user_ble_gap_event(struct ble_gap_event* event, void* arg)
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
			_user_ble_start_adv();
		}
		return 0;

	case BLE_GAP_EVENT_DISCONNECT:
		s_connected = false;
		s_notify_enabled = false;
		s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
		s_adv_running = false;
		ESP_LOGI(TAG, "BLE disconnected, reason=%d", event->disconnect.reason);
		_user_ble_start_adv();
		return 0;

	case BLE_GAP_EVENT_ADV_COMPLETE:
		s_adv_running = false;
		_user_ble_start_adv();
		return 0;

	case BLE_GAP_EVENT_SUBSCRIBE:
		if (event->subscribe.attr_handle == s_tx_val_handle) {
			s_notify_enabled = (event->subscribe.cur_notify != 0);
			ESP_LOGI(TAG,
					 "Notify %s, conn_handle=%u",
					 s_notify_enabled ? "enabled" : "disabled",
					 (unsigned)event->subscribe.conn_handle);
		}
		return 0;

	default:
		return 0;
	}
}

static int _user_ble_gatt_access(uint16_t conn_handle,
								 uint16_t attr_handle,
								 struct ble_gatt_access_ctxt* ctxt,
								 void* arg)
{
	int rc = 0;
	uint16_t rx_len = 0U;
	user_ble_chr_e chr = (user_ble_chr_e)(uintptr_t)arg;

	(void)conn_handle;
	(void)attr_handle;

	if (ctxt == NULL) {
		return BLE_ATT_ERR_UNLIKELY;
	}

	if ((chr == User_Ble_Chr_Rx) && (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR)) {
		rx_len = OS_MBUF_PKTLEN(ctxt->om);
		if (rx_len > USER_BLE_MAX_TEXT_LEN) {
			rx_len = USER_BLE_MAX_TEXT_LEN;
		}

		rc = os_mbuf_copydata(ctxt->om, 0, rx_len, s_rx_text);
		if (rc != 0) {
			return BLE_ATT_ERR_UNLIKELY;
		}

		s_rx_text[rx_len] = '\0';
		rx_len = _user_ble_trim_eol(s_rx_text, rx_len);
		ESP_LOGI(TAG, "RX: %s", s_rx_text);

		if (s_rx_cb != NULL) {
			s_rx_cb((const uint8_t*)s_rx_text, rx_len, s_rx_cb_ctx);
		}

		if ((rx_len == 4U) && (memcmp(s_rx_text, "PING", 4U) == 0)) {
			user_ble_send_text("PONG");
		}
		else if ((rx_len > 5U) && (memcmp(s_rx_text, "ECHO ", 5U) == 0)) {
			user_ble_send_text(&s_rx_text[5]);
		}
		else {
			user_ble_send_text("ACK");
		}

		return 0;
	}

	if ((chr == User_Ble_Chr_Tx) && (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR)) {
		rc = os_mbuf_append(ctxt->om, s_last_tx_text, s_last_tx_len);
		return (rc == 0) ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
	}

	return BLE_ATT_ERR_UNLIKELY;
}

static struct ble_gatt_chr_def s_user_ble_chrs[] = {
	{
		.uuid = BLE_UUID16_DECLARE(USER_BLE_RX_CHAR_UUID16),
		.access_cb = _user_ble_gatt_access,
		.flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
		.arg = (void*)(uintptr_t)User_Ble_Chr_Rx,
	},
	{
		.uuid = BLE_UUID16_DECLARE(USER_BLE_TX_CHAR_UUID16),
		.access_cb = _user_ble_gatt_access,
		.flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
		.val_handle = &s_tx_val_handle,
		.arg = (void*)(uintptr_t)User_Ble_Chr_Tx,
	},
	{ 0 }
};

static const struct ble_gatt_svc_def s_user_ble_svcs[] = {
	{
		.type = BLE_GATT_SVC_TYPE_PRIMARY,
		.uuid = BLE_UUID16_DECLARE(USER_BLE_SERVICE_UUID16),
		.characteristics = s_user_ble_chrs,
	},
	{ 0 }
};

static void _user_ble_on_reset(int reason)
{
	s_sync_ready = false;
	s_adv_running = false;
	ESP_LOGW(TAG, "NimBLE reset, reason=%d", reason);
}

static void _user_ble_on_sync(void)
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
		ESP_LOGI(TAG,
				 "BLE addr type=%d addr=%02X:%02X:%02X:%02X:%02X:%02X",
				 (int)s_addr_type,
				 addr_val[5],
				 addr_val[4],
				 addr_val[3],
				 addr_val[2],
				 addr_val[1],
				 addr_val[0]);
	}

	s_sync_ready = true;
	ESP_LOGI(TAG, "BLE sync done, start_req=%d", s_start_req ? 1 : 0);
	if (s_start_req) {
		adv_err = _user_ble_start_adv();
		if (adv_err != ESP_OK) {
			ESP_LOGE(TAG, "start adv after sync failed: %s", esp_err_to_name(adv_err));
		}
	}
}

static void _user_ble_host_task(void* param)
{
	(void)param;
	nimble_port_run();
	nimble_port_freertos_deinit();
}

esp_err_t user_ble_init(void)
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
		ESP_LOGE(TAG,
				 "nimble_port_init failed: %s, bt_status=%d",
				 esp_err_to_name(err),
				 (int)bt_status);
		return err;
	}

	ble_hs_cfg.reset_cb = _user_ble_on_reset;
	ble_hs_cfg.sync_cb = _user_ble_on_sync;

	ble_svc_gap_init();
	ble_svc_gatt_init();

	rc = ble_svc_gap_device_name_set(USER_BLE_DEVICE_NAME);
	if (rc != 0) {
		ESP_LOGE(TAG, "ble_svc_gap_device_name_set failed: %d", rc);
		nimble_port_deinit();
		return ESP_FAIL;
	}

	rc = ble_gatts_count_cfg(s_user_ble_svcs);
	if (rc != 0) {
		ESP_LOGE(TAG, "ble_gatts_count_cfg failed: %d", rc);
		nimble_port_deinit();
		return ESP_FAIL;
	}

	rc = ble_gatts_add_svcs(s_user_ble_svcs);
	if (rc != 0) {
		ESP_LOGE(TAG, "ble_gatts_add_svcs failed: %d", rc);
		nimble_port_deinit();
		return ESP_FAIL;
	}

	s_init_done = true;

	nimble_port_freertos_init(_user_ble_host_task);

	ESP_LOGI(TAG, "BLE init done");
	return ESP_OK;
}

esp_err_t user_ble_start(void)
{
	esp_err_t adv_err = ESP_OK;

	s_start_req = true;

	if (!s_init_done) {
		esp_err_t err = user_ble_init();
		if (err != ESP_OK) {
			return err;
		}
	}

	adv_err = _user_ble_start_adv();
	if ((adv_err == ESP_OK) && !s_sync_ready) {
		ESP_LOGI(TAG, "Start requested, waiting for BLE sync");
	}

	return adv_err;
}

void user_ble_stop(void)
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

bool user_ble_is_ready(void)
{
	return s_init_done && s_sync_ready;
}

bool user_ble_is_connected(void)
{
	return s_connected;
}

void user_ble_set_rx_callback(user_ble_rx_cb_t cb, void* user_ctx)
{
	s_rx_cb = cb;
	s_rx_cb_ctx = user_ctx;
}

esp_err_t user_ble_send_text(const char* text)
{
	size_t text_len = 0U;

	if (text == NULL) {
		return ESP_ERR_INVALID_ARG;
	}

	text_len = strnlen(text, USER_BLE_MAX_TEXT_LEN);
	memcpy(s_last_tx_text, text, text_len);
	s_last_tx_text[text_len] = '\0';
	s_last_tx_len = (uint16_t)text_len;

	return _user_ble_push_notify((const uint8_t*)s_last_tx_text, s_last_tx_len);
}

#else

#define TAG "user_ble"

esp_err_t user_ble_init(void)
{
	ESP_LOGW(TAG, "BLE is disabled in sdkconfig");
	return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t user_ble_start(void)
{
	return ESP_ERR_NOT_SUPPORTED;
}

void user_ble_stop(void)
{
}

bool user_ble_is_ready(void)
{
	return false;
}

bool user_ble_is_connected(void)
{
	return false;
}

void user_ble_set_rx_callback(user_ble_rx_cb_t cb, void* user_ctx)
{
	(void)cb;
	(void)user_ctx;
}

esp_err_t user_ble_send_text(const char* text)
{
	(void)text;
	return ESP_ERR_NOT_SUPPORTED;
}

#endif
