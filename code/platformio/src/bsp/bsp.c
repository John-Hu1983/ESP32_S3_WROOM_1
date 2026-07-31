#include "bsp.h"

#define TAG "BSP"

/* Drive the external power lock pin high to turn system power on. */
esp_err_t bsp_power_on(void)
{
    gpba02b_pin_set_mode(POWER_LOCK_IO_PORT, POWER_LOCK_IO_PIN, GPBA02B_PIN_MODE_OUTPUT);
    gpba02b_pin_write(POWER_LOCK_IO_PORT, POWER_LOCK_IO_PIN, true);
    return ESP_OK;
}

/* Drive the external power lock pin low to turn system power off. */
esp_err_t bsp_power_off(void)
{
    gpba02b_pin_set_mode(POWER_LOCK_IO_PORT, POWER_LOCK_IO_PIN, GPBA02B_PIN_MODE_OUTPUT);
    gpba02b_pin_write(POWER_LOCK_IO_PORT, POWER_LOCK_IO_PIN, false);
    return ESP_OK;
}

/* Print current PSRAM chip and heap status when available. */
static void print_heap_info(void)
{
#if CONFIG_SPIRAM
    size_t psram_chip_bytes = esp_psram_get_size();
    size_t psram_heap_total = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
    size_t psram_heap_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);

    ESP_LOGI(TAG, "PSRAM chip: %u bytes (%.2f MB)", (unsigned)psram_chip_bytes,
             (double)psram_chip_bytes / (1024.0 * 1024.0));
    ESP_LOGI(TAG, "PSRAM heap: total=%u, free=%u bytes",
             (unsigned)psram_heap_total, (unsigned)psram_heap_free);
#else
    ESP_LOGW(TAG, "CONFIG_SPIRAM is disabled.");
#endif
}

/* Initialize board services in the expected startup order. */
esp_err_t bsp_init_whole(void)
{
    esp_err_t ret;
    print_heap_info();

    ret = gpba02b_init_device();
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "gpba02b_init_device failed: %d", (int)ret);
        return ret;
    }

    ret = bsp_power_on();
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "bsp_power_on failed: %d", (int)ret);
        return ret;
    }

    return ESP_OK;
}