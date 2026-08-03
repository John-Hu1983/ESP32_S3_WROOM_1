#include "apps_idle_task.h"

#define TAG "APPS_IDLE"
#define APPS_IDLE_PERIOD_MS 1000U
#define APPS_IDLE_TASK_STACK_SIZE 4096U
#define APPS_IDLE_TASK_PRIORITY 2U

static TaskHandle_t s_idle_task_handle = NULL;
static volatile uint32_t s_idle_hits[APPS_IDLE_CORE_COUNT] = {0};
static uint32_t s_prev_idle_hits[APPS_IDLE_CORE_COUNT] = {0};
static uint64_t s_idle_hits_peak = 0;
static network_status_t s_network = {
    .connected = false,
    .rssi_dbm = 0,
};

/*
 * brief: Idle hook callback on CPU0; counts how often CPU0 reaches idle state.
 * input: none.
 * output: true, so callback runs once per idle cycle.
 */
static bool apps_idle_hook_cpu0(void)
{
    s_idle_hits[0]++;
    return true;
}

#if (APPS_IDLE_CORE_COUNT > 1)
/*
 * brief: Idle hook callback on CPU1; counts how often CPU1 reaches idle state.
 * input: none.
 * output: true, so callback runs once per idle cycle.
 */
static bool apps_idle_hook_cpu1(void)
{
    s_idle_hits[1]++;
    return true;
}
#endif

/*
 * brief: Convert total/free bytes into a usage percent value.
 * input: total - total memory bytes; free - currently free bytes.
 * output: usage percent in range 0..100.
 */
static uint8_t apps_idle_calc_usage_percent(size_t total, size_t free)
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
 * brief: Estimate CPU usage from idle-hook hit deltas across all cores.
 * input: none.
 * output: estimated CPU usage percent in range 0..100.
 */
static uint8_t apps_idle_calc_cpu_usage_percent(void)
{
    uint64_t idle_delta_sum = 0;
    uint32_t i;

    for (i = 0; i < APPS_IDLE_CORE_COUNT; i++)
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
 * brief: Format current wall time as HH:MM, with uptime fallback if RTC time is unavailable.
 * input: out_time - output buffer with size 6 bytes.
 * output: none.
 */
static void apps_idle_fill_time_hhmm(char out_time[6])
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
 * brief: Periodic worker that samples runtime stats and pushes snapshots to status bar.
 * input: param - unused task argument.
 * output: none.
 */
static void apps_idle_task(void *param)
{
    (void)param;

    while (1)
    {
        app_status_snapshot_t snapshot;
        size_t ram_total;
        size_t ram_free;
        size_t psram_total;
        size_t psram_free;

        snapshot.battery_percent = 100U;
        snapshot.network_connected = s_network.connected;
        snapshot.network_rssi_dbm = s_network.rssi_dbm;

        snapshot.cpu_usage_percent = apps_idle_calc_cpu_usage_percent();

        ram_total = heap_caps_get_total_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        ram_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        snapshot.ram_usage_percent = apps_idle_calc_usage_percent(ram_total, ram_free);

        psram_total = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
        psram_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
        snapshot.psram_usage_percent = apps_idle_calc_usage_percent(psram_total, psram_free);

        apps_idle_fill_time_hhmm(snapshot.time_hhmm);

        app_status_bar_submit_snapshot(&snapshot);

        vTaskDelay(pdMS_TO_TICKS(APPS_IDLE_PERIOD_MS));
    }
}

/*
 * brief: Register idle hooks and start the periodic runtime-stats task once.
 * input: none.
 * output: ESP_OK on success, otherwise ESP-IDF error code.
 */
esp_err_t apps_idle_task_start(void)
{
    BaseType_t task_ok;
    esp_err_t ret;

    if (s_idle_task_handle != NULL)
    {
        return ESP_OK;
    }

    ret = esp_register_freertos_idle_hook_for_cpu(apps_idle_hook_cpu0, 0);
    if ((ret != ESP_OK) && (ret != ESP_ERR_INVALID_STATE))
    {
        ESP_LOGE(TAG, "register idle hook CPU0 failed: %d", (int)ret);
        return ret;
    }

#if (APPS_IDLE_CORE_COUNT > 1)
    ret = esp_register_freertos_idle_hook_for_cpu(apps_idle_hook_cpu1, 1);
    if ((ret != ESP_OK) && (ret != ESP_ERR_INVALID_STATE))
    {
        ESP_LOGE(TAG, "register idle hook CPU1 failed: %d", (int)ret);
        return ret;
    }
#endif

    task_ok = xTaskCreate(apps_idle_task,
                          "apps_idle",
                          APPS_IDLE_TASK_STACK_SIZE,
                          NULL,
                          APPS_IDLE_TASK_PRIORITY,
                          &s_idle_task_handle);
    if (task_ok != pdPASS)
    {
        s_idle_task_handle = NULL;
        ESP_LOGE(TAG, "xTaskCreate apps_idle failed");
        return ESP_FAIL;
    }

    return ESP_OK;
}

/*
 * brief: Update network status source used by periodic snapshot reporting.
 * input: connected - link state; rssi_dbm - signal strength in dBm.
 * output: none.
 */
void apps_idle_task_set_network_state(bool connected, int8_t rssi_dbm)
{
    s_network.connected = connected;
    s_network.rssi_dbm = rssi_dbm;
    app_status_bar_set_network_state(connected, rssi_dbm);
}
