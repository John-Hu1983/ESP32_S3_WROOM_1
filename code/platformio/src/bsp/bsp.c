#include "bsp.h"

#define TAG "BSP"

/*
 * brief: Drive the external power lock pin high to turn system power on.
 * input: None.
 * output: ESP_OK (current implementation always returns success).
 */
esp_err_t bsp_power_on(void)
{
    gpba02b_pin_set_mode(POWER_LOCK_IO_PORT, POWER_LOCK_IO_PIN, GPBA02B_PIN_MODE_OUTPUT);
    gpba02b_pin_write(POWER_LOCK_IO_PORT, POWER_LOCK_IO_PIN, true);
    return ESP_OK;
}

/*
 * brief: Drive the external power lock pin low to turn system power off.
 * input: None.
 * output: ESP_OK (current implementation always returns success).
 */
esp_err_t bsp_power_off(void)
{
    gpba02b_pin_set_mode(POWER_LOCK_IO_PORT, POWER_LOCK_IO_PIN, GPBA02B_PIN_MODE_OUTPUT);
    gpba02b_pin_write(POWER_LOCK_IO_PORT, POWER_LOCK_IO_PIN, false);
    return ESP_OK;
}

/*
 * brief: Print current PSRAM chip size and heap usage information when enabled.
 * input: None.
 * output: None.
 */
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

/*
 * brief: Delay execution for at least the requested millisecond duration.
 * input: ms - delay time in milliseconds.
 * output: None.
 */
void delay_ms(uint32_t ms)
{
    TickType_t ticks = pdMS_TO_TICKS(ms) < 1 ? 1 : pdMS_TO_TICKS(ms);
    vTaskDelay(ticks);
}

/*
 * brief: Initialize board services in startup order and enable default power.
 * input: None.
 * output: ESP_OK on success; otherwise propagated initialization error.
 */
esp_err_t bsp_init_whole(void)
{
    /*
    extend & power lock
    */
    USER_RETURN_ON_ERROR(gpba02b_init_device(), TAG, "gpba02b_init_device failed");
    USER_RETURN_ON_ERROR(bsp_power_on(), TAG, "bsp_power_on failed");

    /*
    resource usage info
    */
    print_heap_info();

    /*
    filesystem
    */
    USER_RETURN_ON_ERROR(usr_fs_init(), TAG, "usr_fs_init failed");

    /*
    buttons
    */
    USER_RETURN_ON_ERROR(gpba02b_pin_set_mode(BUTTON_UP_IO_PORT, BUTTON_UP_IO_PIN,
                                              GPBA02B_PIN_MODE_INPUT_PULLUP),
                         TAG, "gpba02b_pin_set_mode BUTTON_UP failed");
    USER_RETURN_ON_ERROR(gpba02b_pin_set_mode(BUTTON_DOWN_IO_PORT, BUTTON_DOWN_IO_PIN,
                                              GPBA02B_PIN_MODE_INPUT_PULLUP),
                         TAG, "gpba02b_pin_set_mode BUTTON_DOWN failed");

    /*
    pdm enable
    */
    USER_RETURN_ON_ERROR(gpba02b_pin_set_mode(PDM_EN_PORT, PDM_EN_PIN,
                                              GPBA02B_PIN_MODE_OUTPUT),
                         TAG, "gpba02b_pin_set_mode PDM_EN failed");
    USER_RETURN_ON_ERROR(gpba02b_pin_write(PDM_EN_PORT, PDM_EN_PIN, true),
                         TAG, "gpba02b_pin_write PDM_EN failed");

    /*
    I2S enable
    */
    USER_RETURN_ON_ERROR(gpba02b_pin_set_mode(I2S_EN_PORT, I2S_EN_PIN,
                                              GPBA02B_PIN_MODE_OUTPUT),
                         TAG, "gpba02b_pin_set_mode I2S_EN failed");
    USER_RETURN_ON_ERROR(gpba02b_pin_write(I2S_EN_PORT, I2S_EN_PIN, true),
                         TAG, "gpba02b_pin_write I2S_EN failed");

    /*
    amplifier
    */
    USER_RETURN_ON_ERROR(ht517_init_device(), TAG, "ht517_init_device failed");

    return ESP_OK;
}