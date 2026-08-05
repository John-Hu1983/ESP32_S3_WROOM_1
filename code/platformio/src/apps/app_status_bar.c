#include "app_status_bar.h"

#define TAG "APP_BAR"

static lv_obj_t *s_top_bar = NULL;
static lv_obj_t *s_bottom_bar = NULL;
static lv_obj_t *s_top_battery_label = NULL;
static lv_obj_t *s_top_network_label = NULL;
static lv_obj_t *s_bottom_metrics_label = NULL;
static lv_obj_t *s_bottom_time_label = NULL;
static lv_timer_t *s_ui_timer = NULL;

static app_status_snapshot_t s_snapshot = {
    .battery_percent = 100,
    .network_connected = false,
    .network_rssi_dbm = 0,
    .cpu_usage_percent = 0,
    .ram_usage_percent = 0,
    .psram_usage_percent = 0,
    .time_hhmm = "--:--",
};

static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static uint32_t s_snapshot_version = 1U;
static uint32_t s_applied_version = 0U;

static lv_coord_t s_lcd_w = 0;
static lv_coord_t s_lcd_h = 0;

static void _app_status_bar_apply_snapshot(void)
{
    app_status_snapshot_t local;
    char battery_text[20];
    char network_text[28];
    char metrics_text[64];

    if ((s_top_battery_label == NULL) || (s_top_network_label == NULL) ||
        (s_bottom_metrics_label == NULL) || (s_bottom_time_label == NULL))
    {
        return;
    }

    portENTER_CRITICAL(&s_lock);
    if (s_applied_version == s_snapshot_version)
    {
        portEXIT_CRITICAL(&s_lock);
        return;
    }

    local = s_snapshot;
    s_applied_version = s_snapshot_version;
    portEXIT_CRITICAL(&s_lock);

    (void)snprintf(battery_text, sizeof(battery_text), "%s %u%%", LV_SYMBOL_BATTERY_FULL,
                   (unsigned)local.battery_percent);

    if (local.network_connected)
    {
        (void)snprintf(network_text, sizeof(network_text), "%s %ddBm", LV_SYMBOL_WIFI,
                       (int)local.network_rssi_dbm);
    }
    else
    {
        (void)snprintf(network_text, sizeof(network_text), "%s OFF", LV_SYMBOL_WIFI);
    }

    (void)snprintf(metrics_text, sizeof(metrics_text),
                   "CPU:%3u%% RAM:%3u%% PS:%3u%%",
                   (unsigned)local.cpu_usage_percent,
                   (unsigned)local.ram_usage_percent,
                   (unsigned)local.psram_usage_percent);

    lv_label_set_text(s_top_battery_label, battery_text);
    lv_label_set_text(s_top_network_label, network_text);
    lv_label_set_text(s_bottom_metrics_label, metrics_text);
    lv_label_set_text(s_bottom_time_label, local.time_hhmm);
}

static void _app_status_bar_ui_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    _app_status_bar_apply_snapshot();
}

esp_err_t app_status_bar_init(lv_coord_t lcd_w, lv_coord_t lcd_h)
{
    lv_obj_t *layer;

    if ((lcd_w <= 0) || (lcd_h <= (2 * APP_STATUS_BAR_HEIGHT)))
    {
        return ESP_ERR_INVALID_ARG;
    }

    s_lcd_w = lcd_w;
    s_lcd_h = lcd_h;

    if ((s_top_bar != NULL) && lv_obj_is_valid(s_top_bar) &&
        (s_bottom_bar != NULL) && lv_obj_is_valid(s_bottom_bar))
    {
        lv_obj_set_size(s_top_bar, s_lcd_w, APP_STATUS_BAR_HEIGHT);
        lv_obj_set_size(s_bottom_bar, s_lcd_w, APP_STATUS_BAR_HEIGHT);
        lv_obj_align(s_top_bar, LV_ALIGN_TOP_MID, 0, 0);
        lv_obj_align(s_bottom_bar, LV_ALIGN_BOTTOM_MID, 0, 0);
        return ESP_OK;
    }

    layer = lv_layer_top();

    s_top_bar = lv_obj_create(layer);
    lv_obj_set_size(s_top_bar, s_lcd_w, APP_STATUS_BAR_HEIGHT);
    lv_obj_align(s_top_bar, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_clear_flag(s_top_bar, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(s_top_bar, lv_color_hex(0x1D4ED8), 0);
    lv_obj_set_style_bg_opa(s_top_bar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_top_bar, 0, 0);
    lv_obj_set_style_radius(s_top_bar, 0, 0);
    lv_obj_set_style_pad_left(s_top_bar, 8, 0);
    lv_obj_set_style_pad_right(s_top_bar, 8, 0);
    lv_obj_set_style_pad_top(s_top_bar, 0, 0);
    lv_obj_set_style_pad_bottom(s_top_bar, 0, 0);

    s_top_battery_label = lv_label_create(s_top_bar);
    lv_obj_set_style_text_color(s_top_battery_label, lv_color_hex(0xF8FAFC), 0);
    lv_obj_align(s_top_battery_label, LV_ALIGN_LEFT_MID, 0, 0);

    s_top_network_label = lv_label_create(s_top_bar);
    lv_obj_set_style_text_color(s_top_network_label, lv_color_hex(0xF8FAFC), 0);
    lv_obj_align(s_top_network_label, LV_ALIGN_RIGHT_MID, 0, 0);

    s_bottom_bar = lv_obj_create(layer);
    lv_obj_set_size(s_bottom_bar, s_lcd_w, APP_STATUS_BAR_HEIGHT);
    lv_obj_align(s_bottom_bar, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_clear_flag(s_bottom_bar, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(s_bottom_bar, lv_color_hex(0x1D4ED8), 0);
    lv_obj_set_style_bg_opa(s_bottom_bar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_bottom_bar, 0, 0);
    lv_obj_set_style_radius(s_bottom_bar, 0, 0);
    lv_obj_set_style_pad_left(s_bottom_bar, 8, 0);
    lv_obj_set_style_pad_right(s_bottom_bar, 8, 0);
    lv_obj_set_style_pad_top(s_bottom_bar, 0, 0);
    lv_obj_set_style_pad_bottom(s_bottom_bar, 0, 0);

    s_bottom_metrics_label = lv_label_create(s_bottom_bar);
    lv_obj_set_style_text_color(s_bottom_metrics_label, lv_color_hex(0xF8FAFC), 0);
    lv_obj_align(s_bottom_metrics_label, LV_ALIGN_LEFT_MID, 0, 0);

    s_bottom_time_label = lv_label_create(s_bottom_bar);
    lv_obj_set_style_text_color(s_bottom_time_label, lv_color_hex(0xF8FAFC), 0);
    lv_obj_align(s_bottom_time_label, LV_ALIGN_RIGHT_MID, 0, 0);

    if (s_ui_timer == NULL)
    {
        s_ui_timer = lv_timer_create(_app_status_bar_ui_timer_cb, 200, NULL);
    }

    _app_status_bar_apply_snapshot();
    return ESP_OK;
}

void app_status_bar_submit_snapshot(const app_status_snapshot_t *snapshot)
{
    if (snapshot == NULL)
    {
        return;
    }

    portENTER_CRITICAL(&s_lock);
    s_snapshot = *snapshot;
    s_snapshot_version++;
    if (s_snapshot_version == 0U)
    {
        s_snapshot_version = 1U;
    }
    portEXIT_CRITICAL(&s_lock);
}

void app_status_bar_set_network_state(bool connected, int8_t rssi_dbm)
{
    portENTER_CRITICAL(&s_lock);
    s_snapshot.network_connected = connected;
    s_snapshot.network_rssi_dbm = rssi_dbm;
    s_snapshot_version++;
    if (s_snapshot_version == 0U)
    {
        s_snapshot_version = 1U;
    }
    portEXIT_CRITICAL(&s_lock);
}

lv_coord_t app_status_bar_content_top(void)
{
    return APP_STATUS_BAR_HEIGHT;
}

lv_coord_t app_status_bar_content_bottom(void)
{
    return s_lcd_h - APP_STATUS_BAR_HEIGHT;
}
