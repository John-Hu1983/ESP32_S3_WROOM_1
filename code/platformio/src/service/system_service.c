#include "system_service.h"

#include <time.h>

#include "bsp/delay.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "freertos/task.h"
#include "esp_freertos_hooks.h"

#define TAG "SYS_SERVICE"
#define SYSTEM_SERVICE_IDLE_PERIOD_MS 1000U
#define SYSTEM_SERVICE_IDLE_TASK_STACK_SIZE 4096U
#define SYSTEM_SERVICE_IDLE_TASK_PRIORITY 2U

#if defined(portNUM_PROCESSORS)
#define SYSTEM_SERVICE_CORE_COUNT portNUM_PROCESSORS
#elif defined(configNUMBER_OF_CORES)
#define SYSTEM_SERVICE_CORE_COUNT configNUMBER_OF_CORES
#else
#define SYSTEM_SERVICE_CORE_COUNT 1U
#endif

static lv_obj_t *s_top_bar = NULL;
static lv_obj_t *s_bottom_bar = NULL;
static lv_obj_t *s_top_battery_label = NULL;
static lv_obj_t *s_top_network_label = NULL;
static lv_obj_t *s_bottom_metrics_label = NULL;
static lv_obj_t *s_bottom_time_label = NULL;
static lv_timer_t *s_ui_timer = NULL;

static system_status_snapshot_t s_snapshot = {
    .battery_percent = 100,
    .network_connected = false,
    .network_rssi_dbm = 0,
    .cpu_usage_percent = 0,
    .ram_usage_percent = 0,
    .psram_usage_percent = 0,
    .time_hhmm = "--:--",
};

static system_network_status_t s_network = {
    .connected = false,
    .rssi_dbm = 0,
};

static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static uint32_t s_snapshot_version = 1U;
static uint32_t s_applied_version = 0U;

static lv_coord_t s_lcd_w = 0;
static lv_coord_t s_lcd_h = 0;

static TaskHandle_t s_idle_task_handle = NULL;
#if CONFIG_SPIRAM_ALLOW_STACK_EXTERNAL_MEMORY
static StaticTask_t s_idle_task_tcb;
static StackType_t *s_idle_task_stack = NULL;
#endif
static volatile uint32_t s_idle_hits[SYSTEM_SERVICE_CORE_COUNT] = {0};
static uint32_t s_prev_idle_hits[SYSTEM_SERVICE_CORE_COUNT] = {0};
static uint64_t s_idle_hits_peak = 0;

/*
 * brief: Apply latest snapshot to status bar labels from LVGL context.
 * input: none.
 * output: none.
 */
static void _system_service_apply_snapshot(void)
{
    system_status_snapshot_t local;
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

/*
 * brief: LVGL timer callback that flushes pending snapshot changes to labels.
 * input: timer - unused timer pointer.
 * output: none.
 */
static void _system_service_ui_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    _system_service_apply_snapshot();
}

/*
 * brief: Publish one new runtime snapshot to shared service state.
 * input: snapshot - latest status values.
 * output: none.
 */
static void _system_service_submit_snapshot(const system_status_snapshot_t *snapshot)
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

/*
 * brief: Create/fix top-bottom status bars on LVGL top layer.
 * input: lcd_w/lcd_h - current panel resolution.
 * output: ESP_OK on success; otherwise invalid-arg.
 */
static esp_err_t _system_service_bar_init(lv_coord_t lcd_w, lv_coord_t lcd_h)
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
    lv_obj_set_style_bg_color(s_top_bar, lv_color_hex(APP_THEME_BAR_HEX), 0);
    lv_obj_set_style_bg_opa(s_top_bar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_top_bar, 0, 0);
    lv_obj_set_style_radius(s_top_bar, 0, 0);
    lv_obj_set_style_pad_left(s_top_bar, 8, 0);
    lv_obj_set_style_pad_right(s_top_bar, 8, 0);
    lv_obj_set_style_pad_top(s_top_bar, 0, 0);
    lv_obj_set_style_pad_bottom(s_top_bar, 0, 0);

    s_top_battery_label = lv_label_create(s_top_bar);
    lv_obj_set_style_text_color(s_top_battery_label, lv_color_hex(APP_THEME_TEXT_PRIMARY_HEX), 0);
    lv_obj_align(s_top_battery_label, LV_ALIGN_LEFT_MID, 0, 0);

    s_top_network_label = lv_label_create(s_top_bar);
    lv_obj_set_style_text_color(s_top_network_label, lv_color_hex(APP_THEME_TEXT_PRIMARY_HEX), 0);
    lv_obj_align(s_top_network_label, LV_ALIGN_RIGHT_MID, 0, 0);

    s_bottom_bar = lv_obj_create(layer);
    lv_obj_set_size(s_bottom_bar, s_lcd_w, APP_STATUS_BAR_HEIGHT);
    lv_obj_align(s_bottom_bar, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_clear_flag(s_bottom_bar, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(s_bottom_bar, lv_color_hex(APP_THEME_BAR_HEX), 0);
    lv_obj_set_style_bg_opa(s_bottom_bar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_bottom_bar, 0, 0);
    lv_obj_set_style_radius(s_bottom_bar, 0, 0);
    lv_obj_set_style_pad_left(s_bottom_bar, 8, 0);
    lv_obj_set_style_pad_right(s_bottom_bar, 8, 0);
    lv_obj_set_style_pad_top(s_bottom_bar, 0, 0);
    lv_obj_set_style_pad_bottom(s_bottom_bar, 0, 0);

    s_bottom_metrics_label = lv_label_create(s_bottom_bar);
    lv_obj_set_style_text_color(s_bottom_metrics_label, lv_color_hex(APP_THEME_TEXT_PRIMARY_HEX), 0);
    lv_obj_align(s_bottom_metrics_label, LV_ALIGN_LEFT_MID, 0, 0);

    s_bottom_time_label = lv_label_create(s_bottom_bar);
    lv_obj_set_style_text_color(s_bottom_time_label, lv_color_hex(APP_THEME_TEXT_PRIMARY_HEX), 0);
    lv_obj_align(s_bottom_time_label, LV_ALIGN_RIGHT_MID, 0, 0);

    if (s_ui_timer == NULL)
    {
        s_ui_timer = lv_timer_create(_system_service_ui_timer_cb, 200, NULL);
    }

    _system_service_apply_snapshot();
    return ESP_OK;
}

/*
 * brief: Idle hook callback for CPU0; counts idle iterations.
 * input: none.
 * output: true to keep callback active.
 */
static bool _system_service_idle_hook_cpu0(void)
{
    s_idle_hits[0]++;
    return true;
}

#if (SYSTEM_SERVICE_CORE_COUNT > 1)
/*
 * brief: Idle hook callback for CPU1; counts idle iterations.
 * input: none.
 * output: true to keep callback active.
 */
static bool _system_service_idle_hook_cpu1(void)
{
    s_idle_hits[1]++;
    return true;
}
#endif

/*
 * brief: Convert total/free memory bytes into usage percent.
 * input: total - total bytes; free - free bytes.
 * output: usage percent within 0..100.
 */
static uint8_t _system_service_calc_usage_percent(size_t total, size_t free)
{
    size_t used;

    if (total == 0U)
    {
        return 0U;
    }

    if (free > total)
    {
        free = total;
    }

    used = total - free;
    return (uint8_t)((used * 100U) / total);
}

/*
 * brief: Estimate CPU usage from all-core idle hit deltas.
 * input: none.
 * output: estimated usage percent within 0..100.
 */
static uint8_t _system_service_calc_cpu_usage_percent(void)
{
    uint64_t idle_delta_sum = 0;
    uint32_t i;

    for (i = 0; i < SYSTEM_SERVICE_CORE_COUNT; i++)
    {
        uint32_t now_hits = s_idle_hits[i];
        uint32_t delta = now_hits - s_prev_idle_hits[i];
        s_prev_idle_hits[i] = now_hits;
        idle_delta_sum += (uint64_t)delta;
    }

    if (idle_delta_sum > s_idle_hits_peak)
    {
        s_idle_hits_peak = idle_delta_sum;
    }
    else if (s_idle_hits_peak > 0U)
    {
        uint64_t decayed_peak = (s_idle_hits_peak * 31ULL + idle_delta_sum) / 32ULL;
        if (decayed_peak < idle_delta_sum)
        {
            decayed_peak = idle_delta_sum;
        }
        s_idle_hits_peak = decayed_peak;
    }

    if (s_idle_hits_peak == 0U)
    {
        return 0U;
    }

    if (idle_delta_sum > s_idle_hits_peak)
    {
        idle_delta_sum = s_idle_hits_peak;
    }

    {
        uint8_t idle_percent = (uint8_t)((idle_delta_sum * 100ULL) / s_idle_hits_peak);
        if (idle_percent > 100U)
        {
            idle_percent = 100U;
        }
        return (uint8_t)(100U - idle_percent);
    }
}

/*
 * brief: Fill HH:MM text from RTC time; fallback to uptime clock.
 * input: out_time - output buffer, must be 6 bytes.
 * output: none.
 */
static void _system_service_fill_time_hhmm(char out_time[6])
{
    time_t now = time(NULL);
    struct tm local_tm;

    if ((now > 0) && (localtime_r(&now, &local_tm) != NULL) && (local_tm.tm_year >= (2020 - 1900)))
    {
        (void)snprintf(out_time, 6, "%02d:%02d", local_tm.tm_hour, local_tm.tm_min);
        return;
    }

    {
        uint64_t uptime_min = (uint64_t)(esp_timer_get_time() / 1000000ULL) / 60ULL;
        uint32_t hour = (uint32_t)((uptime_min / 60ULL) % 24ULL);
        uint32_t minute = (uint32_t)(uptime_min % 60ULL);
        (void)snprintf(out_time, 6, "%02u:%02u", (unsigned)hour, (unsigned)minute);
    }
}

/*
 * brief: Periodic worker collecting cpu/ram/psram/time metrics.
 * input: param - unused task argument.
 * output: none.
 */
static void _system_service_idle_task(void *param)
{
    (void)param;

    while (1)
    {
        system_status_snapshot_t snapshot;
        size_t ram_total;
        size_t ram_free;
        size_t psram_total;
        size_t psram_free;

        snapshot.battery_percent = 100U;
        portENTER_CRITICAL(&s_lock);
        snapshot.network_connected = s_network.connected;
        snapshot.network_rssi_dbm = s_network.rssi_dbm;
        portEXIT_CRITICAL(&s_lock);

        snapshot.cpu_usage_percent = _system_service_calc_cpu_usage_percent();

        ram_total = heap_caps_get_total_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        ram_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        snapshot.ram_usage_percent = _system_service_calc_usage_percent(ram_total, ram_free);

        psram_total = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
        psram_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
        snapshot.psram_usage_percent = _system_service_calc_usage_percent(psram_total, psram_free);

        _system_service_fill_time_hhmm(snapshot.time_hhmm);
        _system_service_submit_snapshot(&snapshot);

        delay_ms(SYSTEM_SERVICE_IDLE_PERIOD_MS);
    }
}

/*
 * brief: Register idle hooks and launch the periodic worker once.
 * input: none.
 * output: ESP_OK on success; otherwise ESP-IDF error.
 */
static esp_err_t _system_service_idle_task_start(void)
{
    BaseType_t task_ok;
    esp_err_t ret;

    if (s_idle_task_handle != NULL)
    {
        return ESP_OK;
    }

    ret = esp_register_freertos_idle_hook_for_cpu(_system_service_idle_hook_cpu0, 0);
    if ((ret != ESP_OK) && (ret != ESP_ERR_INVALID_STATE))
    {
        ESP_LOGE(TAG, "register idle hook CPU0 failed: %d", (int)ret);
        return ret;
    }

#if (SYSTEM_SERVICE_CORE_COUNT > 1)
    ret = esp_register_freertos_idle_hook_for_cpu(_system_service_idle_hook_cpu1, 1);
    if ((ret != ESP_OK) && (ret != ESP_ERR_INVALID_STATE))
    {
        ESP_LOGE(TAG, "register idle hook CPU1 failed: %d", (int)ret);
        return ret;
    }
#endif

#if CONFIG_SPIRAM_ALLOW_STACK_EXTERNAL_MEMORY
    if (s_idle_task_stack == NULL)
    {
        s_idle_task_stack = (StackType_t *)heap_caps_malloc(SYSTEM_SERVICE_IDLE_TASK_STACK_SIZE,
                                                             MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }

    if (s_idle_task_stack != NULL)
    {
        s_idle_task_handle = xTaskCreateStatic(_system_service_idle_task,
                                               "sys_service_idle",
                                               SYSTEM_SERVICE_IDLE_TASK_STACK_SIZE / sizeof(StackType_t),
                                               NULL,
                                               SYSTEM_SERVICE_IDLE_TASK_PRIORITY,
                                               s_idle_task_stack,
                                               &s_idle_task_tcb);
    }
#endif

    if (s_idle_task_handle == NULL)
    {
        task_ok = xTaskCreate(_system_service_idle_task,
                              "sys_service_idle",
                              SYSTEM_SERVICE_IDLE_TASK_STACK_SIZE,
                              NULL,
                              SYSTEM_SERVICE_IDLE_TASK_PRIORITY,
                              &s_idle_task_handle);
        if (task_ok != pdPASS)
        {
            s_idle_task_handle = NULL;
            ESP_LOGE(TAG, "xTaskCreate system service idle task failed");
            return ESP_FAIL;
        }
    }

    return ESP_OK;
}

esp_err_t system_service_start(lv_coord_t lcd_w, lv_coord_t lcd_h)
{
    esp_err_t ret;

    ret = _system_service_bar_init(lcd_w, lcd_h);
    if (ret != ESP_OK)
    {
        return ret;
    }

    ret = _system_service_idle_task_start();
    if (ret != ESP_OK)
    {
        return ret;
    }

    return ESP_OK;
}

void system_service_set_network_state(bool connected, int8_t rssi_dbm)
{
    portENTER_CRITICAL(&s_lock);
    s_network.connected = connected;
    s_network.rssi_dbm = rssi_dbm;
    s_snapshot.network_connected = connected;
    s_snapshot.network_rssi_dbm = rssi_dbm;
    s_snapshot_version++;
    if (s_snapshot_version == 0U)
    {
        s_snapshot_version = 1U;
    }
    portEXIT_CRITICAL(&s_lock);
}

esp_err_t system_service_get_network_state(system_network_status_t *status)
{
    if (status == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    portENTER_CRITICAL(&s_lock);
    *status = s_network;
    portEXIT_CRITICAL(&s_lock);
    return ESP_OK;
}

lv_coord_t system_service_content_top(void)
{
    return APP_STATUS_BAR_HEIGHT;
}

lv_coord_t system_service_content_bottom(void)
{
    return s_lcd_h - APP_STATUS_BAR_HEIGHT;
}
