#include "desktop_app.h"

#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "nvs.h"

#include "user/gui/about_ui.h"
#include "user/gui/bt_ui.h"
#include "user/gui/camera_ui.h"
#include "user/gui/file_ui.h"
#include "user/gui/gallery_ui.h"
#include "user/gui/mic_ui.h"
#include "user/gui/oscilloscope_ui.h"
#include "user/gui/pidm_ui.h"
#include "user/gui/print_ui.h"
#include "user/gui/rfid_ui.h"
#include "user/gui/servo_ui.h"
#include "user/gui/setting_ui.h"

#define TAG "desktop"

// clang-format off
static const desktop_icon_s s_desktop_icons[DESKTOP_ICON_COUNT] = {
{ LV_SYMBOL_VIDEO,      "Camera",  0xE95420,  camera_open_screen,  camera_close_screen },
{ LV_SYMBOL_IMAGE,      "Gallery", 0xD94B3D,  gallery_open_screen, gallery_close_screen },
{ LV_SYMBOL_AUDIO,      "Print",   0x77216F,  print_open_screen,   print_close_screen },
{ LV_SYMBOL_LIST,       "Scope",   0xF27C38,  scope_open_screen,   scope_close_screen },
{ LV_SYMBOL_UPLOAD,     "Servo",   0xC0563F,  servo_open_screen,   servo_close_screen },
{ LV_SYMBOL_BLUETOOTH,  "BT",      0xB65C2C,  bt_open_screen,      bt_close_screen },
{ LV_SYMBOL_FILE,       "File",    0xE19A35,  file_open_screen,    file_close_screen },
{ LV_SYMBOL_PLAY,       "Mic",     0x8F6745,  mic_open_screen,     mic_close_screen },
{ LV_SYMBOL_BELL,       "PIDM",    0xC23B4A,  pidm_open_screen,    pidm_close_screen },
{ LV_SYMBOL_REFRESH,    "RFID",    0x8A3D5D,  rfid_open_screen,    rfid_close_screen },
{ LV_SYMBOL_SETTINGS,   "config",  0xA8703A,  config_open_screen,  config_close_screen },
{ LV_SYMBOL_WARNING,    "About",   0x6F4A34,  about_open_screen,   about_close_screen},
};
// clang-format on

static lv_display_t* s_lv_display;
static lv_color_t* s_lv_buf_1;
static lv_color_t* s_lv_buf_2;
static esp_timer_handle_t s_lv_tick_timer;
static TaskHandle_t s_lv_task_handle;
static uint16_t s_lcd_width;
static uint16_t s_lcd_height;
static lv_obj_t* s_desktop_screen;
static lv_obj_t* s_net_label;
static lv_obj_t* s_temp_label;
static lv_obj_t* s_weather_label;
static lv_obj_t* s_time_label;
static lv_obj_t* s_msg_label;
static desktop_icon_op_s s_icon_op;
static volatile bool s_home_request_pending;
static volatile bool s_net_online;
static portMUX_TYPE s_desktop_lock = portMUX_INITIALIZER_UNLOCKED;
static char* s_desktop_msg;
static bool s_desktop_msg_dirty;
static bool s_desktop_msg_active;
static uint32_t s_desktop_msg_age_ms;
static bool s_desktop_time_valid;
static uint64_t s_desktop_base_epoch_sec;
static uint64_t s_desktop_base_time_us;
static uint32_t s_sys_info_elapsed_ms;
static uint32_t s_time_elapsed_ms;
static uint32_t s_time_sync_elapsed_ms;
static uint32_t s_weather_elapsed_ms;
static TaskHandle_t s_weather_task_handle;
static bool s_city_temp_valid;
static float s_city_temp_c;
static volatile bool s_weather_force_refresh;
static char s_weather_city_label[DESKTOP_WEATHER_CITY_LABEL_LEN] =
    BSP_WEATHER_CITY_LABEL;
static char s_weather_latitude[DESKTOP_WEATHER_COORD_LEN] = BSP_WEATHER_LATITUDE;
static char s_weather_longitude[DESKTOP_WEATHER_COORD_LEN] = BSP_WEATHER_LONGITUDE;
#if (configUSE_TRACE_FACILITY == 1)
static bool s_cpu_has_prev;
static uint64_t s_cpu_prev_total_runtime;
static uint64_t s_cpu_prev_idle_runtime;
#endif

#define DESKTOP_WEATHER_NVS_NAMESPACE "desktop_weather"
#define DESKTOP_WEATHER_NVS_KEY_CITY  "city"
#define DESKTOP_WEATHER_NVS_KEY_LAT   "lat"
#define DESKTOP_WEATHER_NVS_KEY_LON   "lon"

typedef struct {
    char data[DESKTOP_WEATHER_HTTP_BUF_SIZE];
    size_t len;
    bool truncated;
} desktop_weather_http_buf_s;

/*
 * brief : Copy source text to destination buffer safely.
 * input : see parameters.
 * output: none.
 * type  : private
 */
static void _desktop_copy_text(char* out, size_t out_len, const char* in) {
    if ((out == NULL) || (out_len == 0U)) {
        return;
    }

    if (in == NULL) {
        out[0] = '\0';
        return;
    }

    snprintf(out, out_len, "%s", in);
}

/*
 * brief : Validate one weather location tuple.
 * input : see parameters.
 * output: return value from this function.
 * type  : private
 */
static bool _desktop_weather_location_valid(const char* city_label,
                                            const char* latitude,
                                            const char* longitude) {
    if ((city_label == NULL) || (latitude == NULL) || (longitude == NULL)) {
        return false;
    }
    if ((city_label[0] == '\0') || (latitude[0] == '\0') || (longitude[0] == '\0')) {
        return false;
    }

    if (strlen(city_label) >= DESKTOP_WEATHER_CITY_LABEL_LEN) {
        return false;
    }
    if (strlen(latitude) >= DESKTOP_WEATHER_COORD_LEN) {
        return false;
    }
    if (strlen(longitude) >= DESKTOP_WEATHER_COORD_LEN) {
        return false;
    }

    return true;
}

/*
 * brief : Update in-memory weather location under caller-owned lock.
 * input : see parameters.
 * output: return value from this function.
 * type  : private
 */
static bool _desktop_weather_set_locked(const char* city_label,
                                        const char* latitude,
                                        const char* longitude) {
    if (!_desktop_weather_location_valid(city_label, latitude, longitude)) {
        return false;
    }

    _desktop_copy_text(s_weather_city_label, sizeof(s_weather_city_label), city_label);
    _desktop_copy_text(s_weather_latitude, sizeof(s_weather_latitude), latitude);
    _desktop_copy_text(s_weather_longitude, sizeof(s_weather_longitude), longitude);
    return true;
}

/*
 * brief : Copy current weather location snapshot from shared state.
 * input : see parameters.
 * output: return value from this function.
 * type  : private
 */
static bool _desktop_weather_get_snapshot(char* city_label,
                                          size_t city_len,
                                          char* latitude,
                                          size_t latitude_len,
                                          char* longitude,
                                          size_t longitude_len) {
    bool valid = false;

    taskENTER_CRITICAL(&s_desktop_lock);
    valid = _desktop_weather_location_valid(
        s_weather_city_label, s_weather_latitude, s_weather_longitude
    );
    if (valid) {
        _desktop_copy_text(city_label, city_len, s_weather_city_label);
        _desktop_copy_text(latitude, latitude_len, s_weather_latitude);
        _desktop_copy_text(longitude, longitude_len, s_weather_longitude);
    }
    taskEXIT_CRITICAL(&s_desktop_lock);

    return valid;
}

/*
 * brief : Persist weather location to NVS.
 * input : none.
 * output: return value from this function.
 * type  : private
 */
static esp_err_t _desktop_weather_save_to_nvs(void) {
    char city_label[DESKTOP_WEATHER_CITY_LABEL_LEN];
    char latitude[DESKTOP_WEATHER_COORD_LEN];
    char longitude[DESKTOP_WEATHER_COORD_LEN];
    nvs_handle_t nvs_handle;
    esp_err_t ret = ESP_FAIL;

    if (!_desktop_weather_get_snapshot(city_label,
                                       sizeof(city_label),
                                       latitude,
                                       sizeof(latitude),
                                       longitude,
                                       sizeof(longitude))) {
        return ESP_ERR_INVALID_STATE;
    }

    ret = nvs_open(DESKTOP_WEATHER_NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = nvs_set_str(nvs_handle, DESKTOP_WEATHER_NVS_KEY_CITY, city_label);
    if (ret != ESP_OK) {
        nvs_close(nvs_handle);
        return ret;
    }

    ret = nvs_set_str(nvs_handle, DESKTOP_WEATHER_NVS_KEY_LAT, latitude);
    if (ret != ESP_OK) {
        nvs_close(nvs_handle);
        return ret;
    }

    ret = nvs_set_str(nvs_handle, DESKTOP_WEATHER_NVS_KEY_LON, longitude);
    if (ret != ESP_OK) {
        nvs_close(nvs_handle);
        return ret;
    }

    ret = nvs_commit(nvs_handle);
    nvs_close(nvs_handle);
    return ret;
}

/*
 * brief : Load weather location from NVS if stored.
 * input : none.
 * output: none.
 * type  : private
 */
static void _desktop_weather_load_from_nvs(void) {
    nvs_handle_t nvs_handle;
    char city_label[DESKTOP_WEATHER_CITY_LABEL_LEN];
    char latitude[DESKTOP_WEATHER_COORD_LEN];
    char longitude[DESKTOP_WEATHER_COORD_LEN];
    size_t city_len = sizeof(city_label);
    size_t latitude_len = sizeof(latitude);
    size_t longitude_len = sizeof(longitude);
    esp_err_t ret = ESP_FAIL;

    ret = nvs_open(DESKTOP_WEATHER_NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (ret != ESP_OK) {
        return;
    }

    ret = nvs_get_str(nvs_handle, DESKTOP_WEATHER_NVS_KEY_CITY, city_label, &city_len);
    if (ret != ESP_OK) {
        nvs_close(nvs_handle);
        return;
    }

    ret = nvs_get_str(nvs_handle, DESKTOP_WEATHER_NVS_KEY_LAT, latitude, &latitude_len);
    if (ret != ESP_OK) {
        nvs_close(nvs_handle);
        return;
    }

    ret = nvs_get_str(
        nvs_handle, DESKTOP_WEATHER_NVS_KEY_LON, longitude, &longitude_len
    );
    if (ret != ESP_OK) {
        nvs_close(nvs_handle);
        return;
    }

    taskENTER_CRITICAL(&s_desktop_lock);
    (void)_desktop_weather_set_locked(city_label, latitude, longitude);
    taskEXIT_CRITICAL(&s_desktop_lock);

    nvs_close(nvs_handle);
}

bool desktop_get_weather_location(char* city_label,
                                  size_t city_len,
                                  char* latitude,
                                  size_t latitude_len,
                                  char* longitude,
                                  size_t longitude_len) {
    return _desktop_weather_get_snapshot(city_label,
                                         city_len,
                                         latitude,
                                         latitude_len,
                                         longitude,
                                         longitude_len);
}

bool desktop_set_weather_location(const char* city_label,
                                  const char* latitude,
                                  const char* longitude) {
    bool updated = false;
    esp_err_t ret = ESP_FAIL;

    taskENTER_CRITICAL(&s_desktop_lock);
    updated = _desktop_weather_set_locked(city_label, latitude, longitude);
    if (updated) {
        s_city_temp_valid = false;
        s_weather_force_refresh = true;
    }
    taskEXIT_CRITICAL(&s_desktop_lock);

    if (!updated) {
        return false;
    }

    ret = _desktop_weather_save_to_nvs();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "weather location save failed: %s", esp_err_to_name(ret));
    }

    return true;
}

/*
 * brief : Collect HTTP response chunks into fixed weather buffer.
 * input : see parameters.
 * output: ESP_OK always.
 * type  : private
 */
static esp_err_t _desktop_weather_http_event(esp_http_client_event_t* evt) {
    desktop_weather_http_buf_s* buf = NULL;
    size_t free_len = 0U;
    size_t copy_len = 0U;

    if (evt == NULL) {
        return ESP_OK;
    }

    buf = (desktop_weather_http_buf_s*)evt->user_data;
    if (buf == NULL) {
        return ESP_OK;
    }

    if ((evt->event_id != HTTP_EVENT_ON_DATA) || (evt->data == NULL)
        || (evt->data_len <= 0)) {
        return ESP_OK;
    }

    if (buf->len >= (sizeof(buf->data) - 1U)) {
        buf->truncated = true;
        return ESP_OK;
    }

    free_len = (sizeof(buf->data) - 1U) - buf->len;
    copy_len = ((size_t)evt->data_len <= free_len) ? (size_t)evt->data_len : free_len;
    if (copy_len > 0U) {
        memcpy(&buf->data[buf->len], evt->data, copy_len);
        buf->len += copy_len;
        buf->data[buf->len] = '\0';
    }

    if ((size_t)evt->data_len > copy_len) {
        buf->truncated = true;
    }

    return ESP_OK;
}

/*
 * brief : Fetch city temperature from Open-Meteo service.
 * input : out_temp_c - output pointer for Celsius temperature.
 * output: ESP_OK on success, error code otherwise.
 * type  : private
 */
static esp_err_t _desktop_fetch_city_temp(float* out_temp_c) {
    char url[DESKTOP_WEATHER_URL_MAX_LEN];
    char latitude[DESKTOP_WEATHER_COORD_LEN];
    char longitude[DESKTOP_WEATHER_COORD_LEN];
    desktop_weather_http_buf_s response = { 0 };
    esp_http_client_config_t cfg = { 0 };
    esp_http_client_handle_t client = NULL;
    cJSON* root = NULL;
    cJSON* current = NULL;
    cJSON* temperature = NULL;
    esp_err_t ret = ESP_FAIL;
    int http_status = 0;

    if (out_temp_c == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!_desktop_weather_get_snapshot(NULL,
                                       0U,
                                       latitude,
                                       sizeof(latitude),
                                       longitude,
                                       sizeof(longitude))) {
        return ESP_ERR_INVALID_STATE;
    }

    snprintf(url,
             sizeof(url),
             DESKTOP_WEATHER_API_URL_FMT,
             latitude,
             longitude);

    cfg.url = url;
    cfg.method = HTTP_METHOD_GET;
    cfg.timeout_ms = DESKTOP_WEATHER_HTTP_TIMEOUT_MS;
    cfg.transport_type = HTTP_TRANSPORT_OVER_SSL;
    cfg.crt_bundle_attach = esp_crt_bundle_attach;
    cfg.event_handler = _desktop_weather_http_event;
    cfg.user_data = &response;

    client = esp_http_client_init(&cfg);
    if (client == NULL) {
        return ESP_ERR_NO_MEM;
    }

    ret = esp_http_client_perform(client);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "weather request failed: %s", esp_err_to_name(ret));
        goto exit;
    }

    http_status = esp_http_client_get_status_code(client);
    if (http_status != 200) {
        ESP_LOGW(TAG, "weather http status=%d", http_status);
        ret = ESP_FAIL;
        goto exit;
    }

    if ((response.len == 0U) || response.truncated) {
        ESP_LOGW(TAG,
                 "weather payload invalid len=%u truncated=%d",
                 (unsigned)response.len,
                 (int)response.truncated);
        ret = ESP_FAIL;
        goto exit;
    }

    root = cJSON_ParseWithLength(response.data, response.len);
    if (root == NULL) {
        ESP_LOGW(TAG, "weather json parse failed");
        ret = ESP_FAIL;
        goto exit;
    }

    current = cJSON_GetObjectItem(root, "current");
    if (!cJSON_IsObject(current)) {
        ESP_LOGW(TAG, "weather json current missing");
        ret = ESP_FAIL;
        goto exit;
    }

    temperature = cJSON_GetObjectItem(current, "temperature_2m");
    if (!cJSON_IsNumber(temperature)) {
        ESP_LOGW(TAG, "weather json temperature_2m missing");
        ret = ESP_FAIL;
        goto exit;
    }

    *out_temp_c = (float)temperature->valuedouble;
    ret = ESP_OK;

exit:
    if (root != NULL) {
        cJSON_Delete(root);
    }
    if (client != NULL) {
        esp_http_client_cleanup(client);
    }

    return ret;
}

/*
 * brief : Update cached city temperature for top bar rendering.
 * input : temp_c - temperature value; valid - cache validity flag.
 * output: none.
 * type  : private
 */
static void _desktop_set_city_temp_cache(float temp_c, bool valid) {
    taskENTER_CRITICAL(&s_desktop_lock);
    if (valid) {
        s_city_temp_c = temp_c;
    }
    s_city_temp_valid = valid;
    taskEXIT_CRITICAL(&s_desktop_lock);
}

/*
 * brief : Read cached city temperature.
 * input : out_temp_c - output pointer.
 * output: true if cache has valid value, false otherwise.
 * type  : private
 */
static bool _desktop_get_city_temp_cache(float* out_temp_c) {
    bool valid = false;

    taskENTER_CRITICAL(&s_desktop_lock);
    valid = s_city_temp_valid;
    if (valid && (out_temp_c != NULL)) {
        *out_temp_c = s_city_temp_c;
    }
    taskEXIT_CRITICAL(&s_desktop_lock);

    return valid;
}

/*
 * brief : Periodically fetch city weather without blocking LVGL task.
 * input : param - unused.
 * output: none.
 * type  : private
 */
static void _desktop_weather_task(void* param) {
    uint32_t elapsed_ms = DESKTOP_WEATHER_FETCH_INTERVAL_MS;
    float temp_c = 0.0f;
    bool force_refresh = false;
    esp_err_t ret = ESP_FAIL;

    (void)param;

    while (1) {
        delay_ms(DESKTOP_WEATHER_TASK_PERIOD_MS);

        if (!s_net_online) {
            continue;
        }

        taskENTER_CRITICAL(&s_desktop_lock);
        force_refresh = s_weather_force_refresh;
        if (force_refresh) {
            s_weather_force_refresh = false;
        }
        taskEXIT_CRITICAL(&s_desktop_lock);

        if (!force_refresh && (elapsed_ms < DESKTOP_WEATHER_FETCH_INTERVAL_MS)) {
            elapsed_ms += DESKTOP_WEATHER_TASK_PERIOD_MS;
            continue;
        }

        elapsed_ms = 0U;
        ret = _desktop_fetch_city_temp(&temp_c);
        if (ret == ESP_OK) {
            _desktop_set_city_temp_cache(temp_c, true);
        }
        else {
            _desktop_set_city_temp_cache(0.0f, false);
        }
    }
}

/*
 * brief : _desktop_alloc_buf.
 * input : see parameters.
 * output: return value from this function.
 * type  : private
 */
static void* _desktop_alloc_buf(size_t size) {
    void* ptr = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (ptr == NULL) {
        ptr = heap_caps_malloc(size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    return ptr;
}

/*
 * brief : _desktop_ensure_msg_buf.
 * input : none.
 * output: return value from this function.
 * type  : private
 */
static bool _desktop_ensure_msg_buf(void) {
    if (s_desktop_msg != NULL) {
        return true;
    }

    s_desktop_msg = (char*)_desktop_alloc_buf(DESKTOP_APP_MSG_TEXT_LEN);
    if (s_desktop_msg == NULL) {
        return false;
    }

    memset(s_desktop_msg, 0, DESKTOP_APP_MSG_TEXT_LEN);
    return true;
}

/*
 * brief : _desktop_obj_valid.
 * input : see parameters.
 * output: return value from this function.
 * type  : private
 */
static bool _desktop_obj_valid(lv_obj_t* obj) {
    return (obj != NULL) && lv_obj_is_valid(obj);
}

/*
 * brief : _desktop_del_obj.
 * input : see parameters.
 * output: none.
 * type  : private
 */
static void _desktop_del_obj(lv_obj_t** obj) {
    if ((obj != NULL) && _desktop_obj_valid(*obj)) {
        lv_obj_del(*obj);
    }

    if (obj != NULL) {
        *obj = NULL;
    }
}

/*
 * brief : _desktop_set_grid_hidden.
 * input : see parameters.
 * output: return value from this function.
 * type  : private
 */
static bool _desktop_set_grid_hidden(bool hidden) {
    if (!_desktop_obj_valid(s_icon_op.desktop_grid)) {
        return false;
    }

    if (hidden) {
        lv_obj_add_flag(s_icon_op.desktop_grid, LV_OBJ_FLAG_HIDDEN);
    }
    else {
        lv_obj_remove_flag(s_icon_op.desktop_grid, LV_OBJ_FLAG_HIDDEN);
    }

    return true;
}

/*
 * brief : Advance LVGL internal tick counter.
 * input : arg - unused callback argument from esp_timer.
 * output: none.
 * type  : public
 */
void desktop_tick_event(void* arg) {
    (void)arg;
    lv_tick_inc(LVGL_TICK_PERIOD_MS);
}

/*
 * brief : Forward LVGL flush area to panel driver and notify flush completion.
 * input : disp - LVGL display instance; area - dirty rectangle; px_map - source pixel buffer.
 * output: none.
 * type  : public
 */
void desktop_flush_event(lv_display_t* disp, const lv_area_t* area, uint8_t* px_map) {
    esp_err_t ret = st7365p_lvgl_flush(area->x1, area->y1, area->x2, area->y2, px_map);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "st7365p_lvgl_flush failed: %d", (int)ret);
    }

    lv_display_flush_ready(disp);
}

/*
 * brief : Convert one color to its inverted RGB counterpart.
 * input : color - source LVGL color value.
 * output: Inverted LVGL color.
 * type  : public
 */
lv_color_t desktop_invert_color(lv_color_t color) {
    lv_color32_t color32 = lv_color_to_32(color, LV_OPA_COVER);

    return lv_color_make((uint8_t)(0xFFU - color32.red),
                         (uint8_t)(0xFFU - color32.green),
                         (uint8_t)(0xFFU - color32.blue));
}

/*
 * brief : Reset cached icon widget handles in desktop runtime state.
 * input : none.
 * output: none.
 * type  : private
 */
static void _desktop_reset_icon_widgets(void) {
    memset(s_icon_op.icon_btn, 0, sizeof(s_icon_op.icon_btn));
    memset(s_icon_op.icon_symbol_label, 0, sizeof(s_icon_op.icon_symbol_label));
    memset(s_icon_op.icon_name_label, 0, sizeof(s_icon_op.icon_name_label));
    s_icon_op.desktop_grid = NULL;
}

/*
 * brief : Create icon grid in desktop middle content area.
 * input : content - middle content container object.
 * output: none.
 * type  : private
 */
static void _desktop_build_icon_grid(lv_obj_t* content) {
    if (!_desktop_obj_valid(content)) {
        return;
    }

    static lv_coord_t col_dsc[] = { LV_GRID_FR(1),
                                    LV_GRID_FR(1),
                                    LV_GRID_FR(1),
                                    LV_GRID_TEMPLATE_LAST };
    static lv_coord_t row_dsc[] = { LV_GRID_FR(1),
                                    LV_GRID_FR(1),
                                    LV_GRID_FR(1),
                                    LV_GRID_FR(1),
                                    LV_GRID_TEMPLATE_LAST };

    lv_obj_t* grid = lv_obj_create(content);
    lv_obj_set_size(grid, lv_pct(100), lv_pct(100));
    lv_obj_set_pos(grid, 0, 0);
    lv_obj_set_style_bg_opa(grid, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(grid, 0, 0);
    lv_obj_set_style_pad_all(grid, 0, 0);
    lv_obj_set_style_pad_row(grid, DESKTOP_ICON_GAP_Y, 0);
    lv_obj_set_style_pad_column(grid, DESKTOP_ICON_GAP_X, 0);
    lv_obj_set_grid_dsc_array(grid, col_dsc, row_dsc);

    for (uint32_t i = 0; i < DESKTOP_ICON_COUNT; i++) {
        lv_coord_t row = (lv_coord_t)(i / DESKTOP_ICON_COLS);
        lv_coord_t col = (lv_coord_t)(i % DESKTOP_ICON_COLS);

        lv_obj_t* btn = lv_btn_create(grid);
        lv_obj_set_grid_cell(btn,
                             LV_GRID_ALIGN_STRETCH,
                             col,
                             1,
                             LV_GRID_ALIGN_STRETCH,
                             row,
                             1);
        lv_obj_set_style_bg_color(btn, lv_color_hex(s_desktop_icons[i].color_hex), 0);
        lv_obj_set_style_radius(btn, 12, 0);
        lv_obj_set_style_border_width(btn, 0, 0);
        lv_obj_set_style_pad_all(btn, 6, 0);

        lv_obj_t* symbol = lv_label_create(btn);
        lv_label_set_text(symbol, s_desktop_icons[i].symbol);
        lv_obj_set_style_text_color(symbol, lv_color_white(), 0);
        lv_obj_align(symbol, LV_ALIGN_TOP_MID, 0, 2);

        lv_obj_t* name = lv_label_create(btn);
        lv_label_set_text(name, s_desktop_icons[i].name);
        lv_obj_set_style_text_color(name, lv_color_white(), 0);
        lv_obj_set_style_text_font(name, &DESKTOP_TEXT_FONT, 0);
        lv_obj_align(name, LV_ALIGN_BOTTOM_MID, 0, -4);

        s_icon_op.icon_btn[i] = btn;
        s_icon_op.icon_symbol_label[i] = symbol;
        s_icon_op.icon_name_label[i] = name;
    }

    s_icon_op.desktop_grid = grid;
}

/*
 * brief : Rebuild desktop middle content area to default icon grid state.
 * input : none.
 * output: none.
 * type  : private
 */
static void _desktop_rebuild_home_content(void) {
    if (!_desktop_obj_valid(s_icon_op.content_area)) {
        return;
    }

    _desktop_del_obj(&s_icon_op.desktop_grid);

    _desktop_reset_icon_widgets();
    _desktop_build_icon_grid(s_icon_op.content_area);

    s_icon_op.switching = -1;
    s_icon_op.sel_timout = 0U;
    s_icon_op.ui_active = false;
    s_icon_op.active_ui_index = 0xFFu;
    s_icon_op.active_ui_root = NULL;
}

/*
 * brief : Apply normal/selected visual state for one icon widget.
 * input : icon_index - icon index; selected - true to apply inverse color.
 * output: none.
 * type  : private
 */
static void _desktop_set_icon_state(uint32_t icon_index, bool selected) {
    if (icon_index >= DESKTOP_ICON_COUNT) {
        return;
    }

    lv_obj_t* btn = s_icon_op.icon_btn[icon_index];
    lv_obj_t* symbol = s_icon_op.icon_symbol_label[icon_index];
    lv_obj_t* name = s_icon_op.icon_name_label[icon_index];
    if (!_desktop_obj_valid(btn) || !_desktop_obj_valid(symbol)
        || !_desktop_obj_valid(name)) {
        return;
    }

    lv_color_t bg_color = lv_color_hex(s_desktop_icons[icon_index].color_hex);
    lv_color_t text_color = lv_color_white();
    if (selected) {
        bg_color = desktop_invert_color(bg_color);
        text_color = desktop_invert_color(text_color);

        lv_obj_set_style_border_width(btn, 2, 0);
        lv_obj_set_style_border_color(btn, lv_color_white(), 0);
        lv_obj_set_style_border_opa(btn, LV_OPA_COVER, 0);
    }
    else {
        lv_obj_set_style_border_width(btn, 0, 0);
    }

    lv_obj_set_style_bg_color(btn, bg_color, 0);
    lv_obj_set_style_text_color(symbol, text_color, 0);
    lv_obj_set_style_text_color(name, text_color, 0);
}

/*
 * brief : Clear current icon selection and restore normal color state.
 * input : none.
 * output: none.
 * type  : private
 */
static void _desktop_clear_icon_switching(void) {
    if ((s_icon_op.switching >= 0)
        && ((uint32_t)s_icon_op.switching < DESKTOP_ICON_COUNT)) {
        _desktop_set_icon_state((uint32_t)s_icon_op.switching, false);
    }

    s_icon_op.switching = -1;
    s_icon_op.sel_timout = 0U;
}

/*
 * brief : Select one icon and update UI states.
 * input : next_index - target icon index.
 * output: none.
 * type  : private
 */
static void _desktop_select_icon(uint32_t next_index) {
    if (next_index >= DESKTOP_ICON_COUNT) {
        return;
    }

    if (s_icon_op.switching == (int)next_index) {
        s_icon_op.sel_timout = 0U;
        return;
    }

    if ((s_icon_op.switching >= 0)
        && ((uint32_t)s_icon_op.switching < DESKTOP_ICON_COUNT)) {
        _desktop_set_icon_state((uint32_t)s_icon_op.switching, false);
    }

    s_icon_op.switching = (int)next_index;
    s_icon_op.sel_timout = 0U;
    _desktop_set_icon_state(next_index, true);
}

/*
 * brief : Destroy currently active sub-UI and reset runtime flags.
 * input : none.
 * output: none.
 * type  : private
 */
static void _desktop_close_active_ui(void) {
    if (!s_icon_op.ui_active) {
        return;
    }

    if (s_icon_op.active_ui_index < DESKTOP_ICON_COUNT) {
        const desktop_icon_s* icon = &s_desktop_icons[s_icon_op.active_ui_index];
        if ((icon->destroy_screen != NULL)
            && _desktop_obj_valid(s_icon_op.active_ui_root)) {
            icon->destroy_screen(s_icon_op.active_ui_root);
            s_icon_op.active_ui_root = NULL;
        }
        else {
            _desktop_del_obj(&s_icon_op.active_ui_root);
        }
    }
    else {
        _desktop_del_obj(&s_icon_op.active_ui_root);
    }

    s_icon_op.ui_active = false;
    s_icon_op.active_ui_index = 0xFFu;
    s_home_request_pending = false;
}

/*
 * brief : Receive home request callback from active sub-menu task.
 * input : user_ctx - callback context, unused.
 * output: none.
 * type  : private
 */
static void _desktop_request_home_callback(void* user_ctx) {
    (void)user_ctx;
    s_home_request_pending = true;
}

/*
 * brief : Leave active sub-UI and restore desktop screen.
 * input : none.
 * output: none.
 * type  : private
 */
static void _desktop_leave_subui(void) {
    if (!s_icon_op.ui_active) {
        return;
    }

    _desktop_close_active_ui();

    if (!_desktop_set_grid_hidden(false)) {
        _desktop_rebuild_home_content();
    }

    _desktop_clear_icon_switching();
}

/*
 * brief : Enter selected icon sub-UI by dispatching to registered module entry.
 * input : none.
 * output: none.
 * type  : private
 */
static void _desktop_enter_selected_ui(void) {
    if (s_icon_op.ui_active) {
        return;
    }
    if ((s_icon_op.switching < 0)
        || ((uint32_t)s_icon_op.switching >= DESKTOP_ICON_COUNT)) {
        return;
    }

    if (!_desktop_obj_valid(s_icon_op.content_area)) {
        return;
    }

    uint32_t ui_index = (uint32_t)s_icon_op.switching;

    const desktop_icon_s* icon = &s_desktop_icons[ui_index];
    if (icon->create_screen == NULL) {
        ESP_LOGE(TAG, "sub-ui create missing, index=%u", (unsigned)ui_index);
        return;
    }

    lv_coord_t area_w = lv_obj_get_width(s_icon_op.content_area);
    lv_coord_t area_h = lv_obj_get_height(s_icon_op.content_area);

    _desktop_set_grid_hidden(true);

    lv_obj_t* ui_root = icon->create_screen(s_icon_op.content_area,
                                            area_w,
                                            area_h,
                                            _desktop_request_home_callback,
                                            NULL);
    if (!_desktop_obj_valid(ui_root)) {
        _desktop_set_grid_hidden(false);
        ESP_LOGE(TAG, "sub-ui create failed, index=%u", (unsigned)ui_index);
        return;
    }

    s_icon_op.active_ui_root = ui_root;
    s_icon_op.ui_active = true;
    s_icon_op.active_ui_index = (uint8_t)ui_index;
    s_home_request_pending = false;
    _desktop_clear_icon_switching();
}

/*
 * brief : Handle icon selection movement and timeout by key event.
 * input : btn_val - keyboard scan result.
 * output: none.
 * type  : private
 */
static void _desktop_active_icons(btn_status_e btn_val) {
    bool is_up = (btn_val == Btn_Up_Click);
    bool is_down = (btn_val == Btn_Down_Click);
    bool is_enter_hold = (btn_val == Btn_Up_Hold_Enter)
        || (btn_val == Btn_Down_Hold_Enter) || (btn_val == Btn_Both_Hold_Enter);

    if (is_enter_hold) {
        _desktop_enter_selected_ui();
        return;
    }

    if (is_up || is_down) {
        int next_index = s_icon_op.switching;
        int step = is_up ? -1 : 1;

        if (next_index < 0) {
            next_index = is_up ? ((int)DESKTOP_ICON_COUNT - 1) : 0;
        }
        else {
            next_index += step;
            if (next_index < 0) {
                next_index = (int)DESKTOP_ICON_COUNT - 1;
            }
            else if (next_index >= (int)DESKTOP_ICON_COUNT) {
                next_index = 0;
            }
        }

        _desktop_select_icon((uint32_t)next_index);
        return;
    }

    if (s_icon_op.switching < 0) {
        return;
    }

    if (btn_val != Btn_Idle) {
        s_icon_op.sel_timout = 0U;
        return;
    }

    s_icon_op.sel_timout += LVGL_TASK_PERIOD_MS;
    if (s_icon_op.sel_timout >= DESKTOP_ICON_SELECT_TIMEOUT_MS) {
        _desktop_clear_icon_switching();
    }
}

/*
 * brief : _is_leave_desktop.
 * input : see parameters.
 * output: return value from this function.
 * type  : private
 */
static bool _is_leave_desktop(void) {
    if (!s_icon_op.ui_active) {
        return false;
    }

    if (s_home_request_pending) {
        _desktop_leave_subui();
    }

    return s_icon_op.ui_active;
}

/*
 * brief : Poll current network state and update top-bar icon when changed.
 * input : none.
 * output: none.
 * type  : private
 */
static void _probe_net_state(void) {
    static uint32_t net_poll_elapsed_ms = 0U;
    static const char* net_symbol = MATERIAL_SYMBOLS_WIFI_OFF;
    const char* net_symbol_next = MATERIAL_SYMBOLS_WIFI_OFF;
    bool net_online_next = false;
    esp_err_t ret;
    wifi_mode_t wifi_mode = WIFI_MODE_NULL;
    wifi_ap_record_t ap_info = { 0 };

    net_poll_elapsed_ms += LVGL_TASK_PERIOD_MS;
    if (net_poll_elapsed_ms < 250U) {
        return;
    }
    net_poll_elapsed_ms = 0U;

    ret = esp_wifi_sta_get_ap_info(&ap_info);
    if (ret == ESP_OK) {
        net_online_next = true;
        if (ap_info.rssi >= -65) {
            net_symbol_next = MATERIAL_SYMBOLS_WIFI;
        }
        else if (ap_info.rssi >= -75) {
            net_symbol_next = MATERIAL_SYMBOLS_WIFI_2_BAR;
        }
        else {
            net_symbol_next = MATERIAL_SYMBOLS_WIFI_1_BAR;
        }
    }
    else {
        ret = esp_wifi_get_mode(&wifi_mode);
        if ((ret == ESP_OK)
            && ((wifi_mode == WIFI_MODE_AP) || (wifi_mode == WIFI_MODE_APSTA))) {
            net_online_next = true;
            net_symbol_next = MATERIAL_SYMBOLS_WIFI;
        }
    }

    s_net_online = net_online_next;

    if (strcmp(net_symbol_next, net_symbol) == 0) {
        return;
    }

    net_symbol = net_symbol_next;
    if (_desktop_obj_valid(s_net_label)) {
        lv_label_set_text(s_net_label, net_symbol);
    }
}

/*
 * brief : _desktop_format_mem_text.
 * input : see parameters.
 * output: none.
 * type  : private
 */
static void _desktop_format_mem_text(char* out, size_t out_len, uint32_t caps) {
    size_t total;
    size_t free;
    size_t used;

    total = heap_caps_get_total_size(caps);
    if (total == 0U) {
        snprintf(out, out_len, "N/A");
        return;
    }

    free = heap_caps_get_free_size(caps);
    used = (free < total) ? (total - free) : 0U;
    snprintf(out,
             out_len,
             "%u/%uK",
             (unsigned)(used / 1024U),
             (unsigned)(total / 1024U));
}

#if (configUSE_TRACE_FACILITY == 1)
/*
 * brief : _desktop_read_cpu_usage.
 * input : none.
 * output: return value from this function.
 * type  : private
 */
static uint8_t _desktop_read_cpu_usage(void) {
    UBaseType_t alloc_task_count;
    TaskStatus_t* task_states;
    configRUN_TIME_COUNTER_TYPE run_time_counter = 0;
    UBaseType_t task_count;
    uint64_t idle_runtime = 0U;
    uint64_t total_runtime;
    uint64_t total_delta;
    uint64_t idle_delta;
    uint64_t total_capacity;
    uint64_t busy_delta;
    uint32_t usage = 0U;
    UBaseType_t i;
    const char* task_name;

    alloc_task_count = uxTaskGetNumberOfTasks() + 5U;
    task_states =
        (TaskStatus_t*)heap_caps_malloc(sizeof(TaskStatus_t) * alloc_task_count,
                                        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (task_states == NULL) {
        task_states =
            (TaskStatus_t*)heap_caps_malloc(sizeof(TaskStatus_t) * alloc_task_count,
                                            MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    if (task_states == NULL) {
        return 0U;
    }

    task_count = uxTaskGetSystemState(task_states, alloc_task_count, &run_time_counter);
    if (task_count == 0U) {
        heap_caps_free(task_states);
        return 0U;
    }

    for (i = 0; i < task_count; i++) {
        task_name = task_states[i].pcTaskName;
        if ((task_name != NULL) && (strncmp(task_name, "IDLE", 4) == 0)) {
            idle_runtime += (uint64_t)task_states[i].ulRunTimeCounter;
        }
    }

    heap_caps_free(task_states);
    total_runtime = (uint64_t)run_time_counter;

    if (!s_cpu_has_prev) {
        s_cpu_prev_total_runtime = total_runtime;
        s_cpu_prev_idle_runtime = idle_runtime;
        s_cpu_has_prev = true;
        return 0U;
    }

    total_delta = total_runtime - s_cpu_prev_total_runtime;
    idle_delta = idle_runtime - s_cpu_prev_idle_runtime;
    s_cpu_prev_total_runtime = total_runtime;
    s_cpu_prev_idle_runtime = idle_runtime;

    if (total_delta == 0U) {
        return 0U;
    }

    total_capacity = total_delta * (uint64_t)CONFIG_FREERTOS_NUMBER_OF_CORES;
    if (total_capacity == 0U) {
        return 0U;
    }

    busy_delta = (idle_delta < total_capacity) ? (total_capacity - idle_delta) : 0U;
    usage = (uint32_t)((busy_delta * 100U) / total_capacity);
    if (usage > 100U) {
        usage = 100U;
    }

    return (uint8_t)usage;
}
#else
/*
 * brief : _desktop_read_cpu_usage.
 * input : none.
 * output: return value from this function.
 * type  : private
 */
static uint8_t _desktop_read_cpu_usage(void) {
    return 0U;
}
#endif

/*
    * brief : _desktop_build_sys_info_text.
 * input : see parameters.
 * output: none.
 * type  : private
 */
static void _desktop_build_sys_info_text(char* out, size_t out_len) {
    char ram_text[32];
    char psram_text[32];
    uint8_t cpu_usage;

    cpu_usage = _desktop_read_cpu_usage();
    _desktop_format_mem_text(ram_text, sizeof(ram_text), MALLOC_CAP_INTERNAL);
    _desktop_format_mem_text(psram_text, sizeof(psram_text), MALLOC_CAP_SPIRAM);

    snprintf(out,
             out_len,
             "CPU:%u%% RAM:%s PSRAM:%s",
             (unsigned)cpu_usage,
             ram_text,
             psram_text);
}

/*
 * brief : desktop_post_message.
 * input : see parameters.
 * output: none.
 * type  : public
 */
void desktop_post_message(const char* msg) {
    if (!_desktop_ensure_msg_buf()) {
        return;
    }

    taskENTER_CRITICAL(&s_desktop_lock);

    if (msg == NULL) {
        s_desktop_msg[0] = '\0';
        s_desktop_msg_active = false;
        s_desktop_msg_dirty = false;
        s_desktop_msg_age_ms = 0U;
        taskEXIT_CRITICAL(&s_desktop_lock);
        return;
    }

    snprintf(s_desktop_msg, DESKTOP_APP_MSG_TEXT_LEN, "%s", msg);
    s_desktop_msg_active = (s_desktop_msg[0] != '\0');
    s_desktop_msg_dirty = s_desktop_msg_active;
    s_desktop_msg_age_ms = 0U;

    taskEXIT_CRITICAL(&s_desktop_lock);
}

/*
 * brief : _show_message_detail.
 * input : none.
 * output: none.
 * type  : private
 */
static void _show_message_detail(void) {
    char msg_text[DESKTOP_APP_MSG_TEXT_LEN];
    char sys_text[DESKTOP_APP_MSG_TEXT_LEN];
    bool has_msg = false;
    bool msg_dirty = false;
    bool msg_expired = false;

    if (!_desktop_obj_valid(s_msg_label)) {
        return;
    }

    taskENTER_CRITICAL(&s_desktop_lock);
    if (s_desktop_msg_active && (s_desktop_msg != NULL)) {
        if (s_desktop_msg_age_ms < (UINT32_MAX - LVGL_TASK_PERIOD_MS)) {
            s_desktop_msg_age_ms += LVGL_TASK_PERIOD_MS;
        }

        if (s_desktop_msg_age_ms <= DESKTOP_APP_MSG_KEEP_MS) {
            snprintf(msg_text, sizeof(msg_text), "%s", s_desktop_msg);
            has_msg = true;
            msg_dirty = s_desktop_msg_dirty;
            s_desktop_msg_dirty = false;
        }
        else {
            s_desktop_msg_active = false;
            s_desktop_msg_dirty = false;
            msg_expired = true;
        }
    }
    else if (s_desktop_msg_active) {
        s_desktop_msg_active = false;
        s_desktop_msg_dirty = false;
        msg_expired = true;
    }
    taskEXIT_CRITICAL(&s_desktop_lock);

    if (has_msg) {
        if (msg_dirty) {
            lv_label_set_text(s_msg_label, msg_text);
        }
        return;
    }

    if (msg_expired) {
        s_sys_info_elapsed_ms = DESKTOP_SYS_INFO_REFRESH_MS;
    }

    s_sys_info_elapsed_ms += LVGL_TASK_PERIOD_MS;
    if (s_sys_info_elapsed_ms < DESKTOP_SYS_INFO_REFRESH_MS) {
        return;
    }

    s_sys_info_elapsed_ms = 0U;
    _desktop_build_sys_info_text(sys_text, sizeof(sys_text));
    lv_label_set_text(s_msg_label, sys_text);
}

/*
 * brief : _gain_real_time.
 * input : none.
 * output: none.
 * type  : private
 */
static void _gain_real_time(void) {
    time_t now_sec;
    time_t cur_time;
    uint64_t now_us;
    uint64_t delta_sec;
    uint64_t cur_sec;
    uint64_t up_sec;
    uint32_t hh;
    uint32_t mm;
    struct tm tm_info;
    char hhmm[8];

    if (!_desktop_obj_valid(s_time_label)) {
        return;
    }

    s_time_sync_elapsed_ms += LVGL_TASK_PERIOD_MS;
    if (s_time_sync_elapsed_ms >= DESKTOP_TIME_SYNC_CHECK_MS) {
        s_time_sync_elapsed_ms = 0U;
        if (s_net_online) {
            now_sec = time(NULL);
            if ((now_sec > 0) && ((uint64_t)now_sec >= DESKTOP_REALTIME_MIN_UNIX_SEC)) {
                s_desktop_time_valid = true;
                s_desktop_base_epoch_sec = (uint64_t)now_sec;
                s_desktop_base_time_us = (uint64_t)esp_timer_get_time();
            }
        }
    }

    s_time_elapsed_ms += LVGL_TASK_PERIOD_MS;
    if (s_time_elapsed_ms < DESKTOP_TIME_REFRESH_MS) {
        return;
    }

    s_time_elapsed_ms = 0U;
    if (s_desktop_time_valid) {
        now_us = (uint64_t)esp_timer_get_time();
        delta_sec = (now_us >= s_desktop_base_time_us)
            ? ((now_us - s_desktop_base_time_us) / 1000000ULL)
            : 0ULL;
        cur_sec = s_desktop_base_epoch_sec + delta_sec;
        cur_time = (time_t)cur_sec;

        if (localtime_r(&cur_time, &tm_info) != NULL) {
            snprintf(hhmm, sizeof(hhmm), "%02d:%02d", tm_info.tm_hour, tm_info.tm_min);
        }
        else {
            snprintf(hhmm, sizeof(hhmm), "--:--");
        }
    }
    else {
        up_sec = (uint64_t)esp_timer_get_time() / 1000000ULL;
        hh = (uint32_t)((up_sec / 3600ULL) % 24ULL);
        mm = (uint32_t)((up_sec / 60ULL) % 60ULL);
        snprintf(hhmm, sizeof(hhmm), "%02u:%02u", (unsigned)hh, (unsigned)mm);
    }

    lv_label_set_text(s_time_label, hhmm);
}

/*
 * brief : _show_weather_detail.
 * input : none.
 * output: none.
 * type  : private
 */
static void _show_weather_detail(void) {
    const char* weather_symbol;
    char temp_text[48];
    char city_label[DESKTOP_WEATHER_CITY_LABEL_LEN];
    float temp_val = 0.0f;
    bool temp_ok = false;
    bool loc_ok = false;

    if (!_desktop_obj_valid(s_weather_label) || !_desktop_obj_valid(s_time_label)
        || !_desktop_obj_valid(s_temp_label)) {
        return;
    }

    s_weather_elapsed_ms += LVGL_TASK_PERIOD_MS;
    if (s_weather_elapsed_ms < DESKTOP_WEATHER_REFRESH_MS) {
        return;
    }

    s_weather_elapsed_ms = 0U;
    weather_symbol =
        s_net_online ? MATERIAL_SYMBOLS_BRIGHTNESS_6 : MATERIAL_SYMBOLS_CLOUD_OFF;
    lv_label_set_text(s_weather_label, weather_symbol);
    lv_obj_align_to(s_weather_label, s_time_label, LV_ALIGN_OUT_LEFT_MID, -6, 0);

    loc_ok = _desktop_weather_get_snapshot(
        city_label, sizeof(city_label), NULL, 0U, NULL, 0U
    );
    if (!loc_ok) {
        _desktop_copy_text(city_label, sizeof(city_label), BSP_WEATHER_CITY_LABEL);
    }

    temp_ok = _desktop_get_city_temp_cache(&temp_val);
    if (temp_ok) {
        snprintf(temp_text,
                 sizeof(temp_text),
                 "%s %.0fC",
                 city_label,
                 temp_val);
    }
    else {
        snprintf(temp_text,
                 sizeof(temp_text),
                 "%s --C",
                 city_label);
    }

    lv_label_set_text(s_temp_label, temp_text);
    lv_obj_align_to(s_temp_label, s_weather_label, LV_ALIGN_OUT_LEFT_MID, -6, 0);
}

/*
 * brief : Main desktop LVGL task loop.
 * input : param - unused task parameter.
 * output: none.
 * type  : private
 */
static void _desktop_lvgl_task(void* param) {
    (void)param;
    btn_scan_s btn = { 0 };
    btn_status_e btn_val;

    while (1) {
        delay_ms(LVGL_TASK_PERIOD_MS);

        lv_timer_handler();
        _probe_net_state();
        _gain_real_time();
        _show_weather_detail();
        _show_message_detail();

        if (_is_leave_desktop()) {
            continue;
        }

        btn_val = button_scan_state(&btn, LVGL_TASK_PERIOD_MS);
        _desktop_active_icons(btn_val);
    }
}

/*
 * brief : Start desktop subsystem including panel, LVGL, and task loop.
 * input : none.
 * output: ESP_OK on success; otherwise propagated startup error.
 * type  : public
 */
esp_err_t desktop_start_task(void) {
    esp_err_t ret;
    st7365p_cfg_t panel_cfg;
    size_t draw_buf_pixels;
    char city_temp_text[48];
    char city_label[DESKTOP_WEATHER_CITY_LABEL_LEN];
    esp_timer_create_args_t tick_timer_args = {
        .callback = desktop_tick_event,
        .arg = NULL,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "desktop_lvgl_tick",
    };
    lv_coord_t grid_x;
    lv_coord_t grid_y;
    lv_coord_t grid_w;
    lv_coord_t grid_h;
    lv_obj_t* scr;
    lv_obj_t* top_bar;
    lv_obj_t* bottom_bar;
    lv_obj_t* content;
    lv_coord_t msg_w;
    BaseType_t task_ok;

    if (s_weather_task_handle != NULL) {
        vTaskDelete(s_weather_task_handle);
        s_weather_task_handle = NULL;
    }

    st7365p_get_default_cfg(&panel_cfg);

    ret = st7365p_panel_init(&panel_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "st7365p_panel_init failed: %d", (int)ret);
        return ret;
    }

    ret = st7365p_set_rotation(2);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "st7365p_set_rotation failed: %d", (int)ret);
        return ret;
    }

    st7365p_get_resolution(&s_lcd_width, &s_lcd_height);
    if ((s_lcd_width == 0U) || (s_lcd_height == 0U)) {
        ESP_LOGE(TAG, "invalid LCD resolution");
        return ESP_ERR_INVALID_SIZE;
    }

    lv_init();

    draw_buf_pixels = (size_t)s_lcd_width * LVGL_DRAW_BUF_LINES;
    s_lv_buf_1 = (lv_color_t*)heap_caps_malloc(draw_buf_pixels * sizeof(lv_color_t),
                                               MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (s_lv_buf_1 == NULL) {
        ESP_LOGE(TAG, "LVGL buf1 PSRAM allocation failed");
        return ESP_ERR_NO_MEM;
    }

    s_lv_buf_2 = (lv_color_t*)heap_caps_malloc(draw_buf_pixels * sizeof(lv_color_t),
                                               MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (s_lv_buf_2 == NULL) {
        ESP_LOGE(TAG, "LVGL buf2 PSRAM allocation failed");
        heap_caps_free(s_lv_buf_1);
        s_lv_buf_1 = NULL;
        return ESP_ERR_NO_MEM;
    }

    s_lv_display = lv_display_create((int32_t)s_lcd_width, (int32_t)s_lcd_height);
    if (s_lv_display == NULL) {
        heap_caps_free(s_lv_buf_2);
        heap_caps_free(s_lv_buf_1);
        s_lv_buf_2 = NULL;
        s_lv_buf_1 = NULL;
        return ESP_FAIL;
    }

    lv_display_set_color_format(s_lv_display, LV_COLOR_FORMAT_RGB565_SWAPPED);
    lv_display_set_flush_cb(s_lv_display, desktop_flush_event);
    lv_display_set_buffers(s_lv_display,
                           s_lv_buf_1,
                           s_lv_buf_2,
                           draw_buf_pixels * sizeof(lv_color_t),
                           LV_DISPLAY_RENDER_MODE_PARTIAL);

    ret = esp_timer_create(&tick_timer_args, &s_lv_tick_timer);
    if (ret != ESP_OK) {
        heap_caps_free(s_lv_buf_2);
        heap_caps_free(s_lv_buf_1);
        s_lv_buf_2 = NULL;
        s_lv_buf_1 = NULL;
        return ret;
    }

    ret = esp_timer_start_periodic(s_lv_tick_timer, LVGL_TICK_PERIOD_MS * 1000U);
    if (ret != ESP_OK) {
        heap_caps_free(s_lv_buf_2);
        heap_caps_free(s_lv_buf_1);
        s_lv_buf_2 = NULL;
        s_lv_buf_1 = NULL;
        return ret;
    }

    grid_x = (lv_coord_t)DESKTOP_MARGIN_X;
    grid_y = (lv_coord_t)(DESKTOP_TOP_BAR_HEIGHT + DESKTOP_MARGIN_Y);
    grid_w = (lv_coord_t)((int32_t)s_lcd_width - (2 * DESKTOP_MARGIN_X));
    grid_h = (lv_coord_t)((int32_t)s_lcd_height - DESKTOP_TOP_BAR_HEIGHT
                          - DESKTOP_BOTTOM_BAR_HEIGHT - (2 * DESKTOP_MARGIN_Y));

    if (grid_w < 0) {
        grid_w = 0;
    }
    if (grid_h < 0) {
        grid_h = 0;
    }

    memset(&s_icon_op, 0, sizeof(s_icon_op));
    s_icon_op.switching = -1;
    s_icon_op.active_ui_index = 0xFFu;
    s_icon_op.ui_active = false;

    s_net_label = NULL;
    s_temp_label = NULL;
    s_weather_label = NULL;
    s_time_label = NULL;
    s_msg_label = NULL;
    s_weather_task_handle = NULL;
    s_city_temp_valid = false;
    s_city_temp_c = 0.0f;
    s_weather_force_refresh = true;
    s_net_online = false;
    s_desktop_time_valid = false;
    s_desktop_base_epoch_sec = 0U;
    s_desktop_base_time_us = 0U;
    s_sys_info_elapsed_ms = DESKTOP_SYS_INFO_REFRESH_MS;
    s_time_elapsed_ms = DESKTOP_TIME_REFRESH_MS;
    s_time_sync_elapsed_ms = DESKTOP_TIME_SYNC_CHECK_MS;
    s_weather_elapsed_ms = DESKTOP_WEATHER_REFRESH_MS;
#if (configUSE_TRACE_FACILITY == 1)
    s_cpu_has_prev = false;
    s_cpu_prev_total_runtime = 0U;
    s_cpu_prev_idle_runtime = 0U;
#endif

    _desktop_weather_load_from_nvs();

    if (!_desktop_ensure_msg_buf()) {
        ESP_LOGE(TAG, "desktop message buffer alloc failed");
        return ESP_ERR_NO_MEM;
    }

    taskENTER_CRITICAL(&s_desktop_lock);
    s_desktop_msg[0] = '\0';
    s_desktop_msg_dirty = false;
    s_desktop_msg_active = false;
    s_desktop_msg_age_ms = 0U;
    taskEXIT_CRITICAL(&s_desktop_lock);

    scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x1C1B1A), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    top_bar = lv_obj_create(scr);
    lv_obj_set_size(top_bar,
                    (lv_coord_t)s_lcd_width,
                    (lv_coord_t)DESKTOP_TOP_BAR_HEIGHT);
    lv_obj_set_pos(top_bar, 0, 0);
    lv_obj_set_style_bg_color(top_bar, lv_color_hex(DESKTOP_TOOLBAR_COLOR_HEX), 0);
    lv_obj_set_style_bg_opa(top_bar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(top_bar, 0, 0);
    lv_obj_set_style_radius(top_bar, 0, 0);
    lv_obj_set_style_pad_all(top_bar, 0, 0);

    s_net_label = lv_label_create(top_bar);
    lv_obj_set_style_text_color(s_net_label, lv_color_black(), 0);
    lv_obj_set_style_text_font(s_net_label, &DESKTOP_SYMBOL_FONT, 0);
    lv_obj_align(s_net_label, LV_ALIGN_LEFT_MID, 4, 0);
    lv_label_set_text(s_net_label, MATERIAL_SYMBOLS_WIFI_OFF);

    s_time_label = lv_label_create(top_bar);
    lv_obj_set_style_text_color(s_time_label, lv_color_black(), 0);
    lv_obj_set_style_text_font(s_time_label, &DESKTOP_TEXT_FONT, 0);
    lv_obj_align(s_time_label, LV_ALIGN_RIGHT_MID, -4, 0);
    lv_label_set_text(s_time_label, "--:--");

    s_weather_label = lv_label_create(top_bar);
    lv_obj_set_style_text_color(s_weather_label, lv_color_black(), 0);
    lv_obj_set_style_text_font(s_weather_label, &DESKTOP_SYMBOL_FONT, 0);
    lv_label_set_text(s_weather_label, MATERIAL_SYMBOLS_CLOUD_OFF);
    lv_obj_align_to(s_weather_label, s_time_label, LV_ALIGN_OUT_LEFT_MID, -6, 0);

    s_temp_label = lv_label_create(top_bar);
    lv_obj_set_style_text_color(s_temp_label, lv_color_black(), 0);
    lv_obj_set_style_text_font(s_temp_label, &DESKTOP_TEXT_FONT, 0);
    if (!_desktop_weather_get_snapshot(city_label,
                                       sizeof(city_label),
                                       NULL,
                                       0U,
                                       NULL,
                                       0U)) {
        _desktop_copy_text(city_label, sizeof(city_label), BSP_WEATHER_CITY_LABEL);
    }
    snprintf(city_temp_text,
             sizeof(city_temp_text),
             "%s --C",
             city_label);
    lv_label_set_text(s_temp_label, city_temp_text);
    lv_obj_align_to(s_temp_label, s_weather_label, LV_ALIGN_OUT_LEFT_MID, -6, 0);

    bottom_bar = lv_obj_create(scr);
    lv_obj_set_size(bottom_bar,
                    (lv_coord_t)s_lcd_width,
                    (lv_coord_t)DESKTOP_BOTTOM_BAR_HEIGHT);
    lv_obj_set_pos(bottom_bar,
                   0,
                   (lv_coord_t)((int32_t)s_lcd_height - DESKTOP_BOTTOM_BAR_HEIGHT));
    lv_obj_set_style_bg_color(bottom_bar, lv_color_hex(DESKTOP_TOOLBAR_COLOR_HEX), 0);
    lv_obj_set_style_bg_opa(bottom_bar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(bottom_bar, 0, 0);
    lv_obj_set_style_radius(bottom_bar, 0, 0);
    lv_obj_set_style_pad_all(bottom_bar, 0, 0);

    s_msg_label = lv_label_create(bottom_bar);
    lv_obj_set_style_text_color(s_msg_label, lv_color_black(), 0);
    lv_obj_set_style_text_font(s_msg_label, &DESKTOP_TEXT_FONT, 0);
    msg_w = (lv_coord_t)((s_lcd_width > 8U) ? (s_lcd_width - 8U) : s_lcd_width);
    lv_obj_set_width(s_msg_label, msg_w);
    lv_obj_align(s_msg_label, LV_ALIGN_LEFT_MID, 4, 0);
    lv_label_set_long_mode(s_msg_label, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_label_set_text(s_msg_label, "CPU:-- RAM:-- PSRAM:--");

    content = lv_obj_create(scr);
    lv_obj_set_size(content, grid_w, grid_h);
    lv_obj_set_pos(content, grid_x, grid_y);
    lv_obj_set_style_bg_color(content, lv_color_hex(0x1C1B1A), 0);
    lv_obj_set_style_bg_opa(content, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(content, 0, 0);
    lv_obj_set_style_radius(content, 0, 0);
    lv_obj_set_style_pad_all(content, 0, 0);

    s_icon_op.content_area = content;
    s_icon_op.active_ui_root = NULL;
    _desktop_reset_icon_widgets();
    _desktop_build_icon_grid(content);

    s_desktop_screen = scr;
    lv_scr_load(scr);

    task_ok = xTaskCreate(_desktop_lvgl_task,
                          "desktop_lvgl",
                          10240,
                          NULL,
                          5,
                          &s_lv_task_handle);
    if (task_ok != pdPASS) {
        ESP_LOGE(TAG, "xTaskCreate desktop_lvgl failed");
        return ESP_FAIL;
    }

    task_ok = xTaskCreate(_desktop_weather_task,
                          "desktop_weather",
                          DESKTOP_WEATHER_TASK_STACK_SIZE,
                          NULL,
                          4,
                          &s_weather_task_handle);
    if (task_ok != pdPASS) {
        s_weather_task_handle = NULL;
        ESP_LOGW(TAG, "xTaskCreate desktop_weather failed");
    }

    ESP_LOGI(TAG,
             "desktop init on %ux%u",
             (unsigned)s_lcd_width,
             (unsigned)s_lcd_height);
    return ESP_OK;
}
