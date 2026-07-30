#include "bsp.h"

esp_err_t bsp_power_on(void)
{
    gpba02b_pin_set_mode(POWER_LOCK_IO_PORT, POWER_LOCK_IO_PIN, GPBA02B_PIN_MODE_OUTPUT);
    gpba02b_pin_write(POWER_LOCK_IO_PORT, POWER_LOCK_IO_PIN, true);
    return ESP_OK;
}

esp_err_t bsp_power_off(void)
{
    gpba02b_pin_set_mode(POWER_LOCK_IO_PORT, POWER_LOCK_IO_PIN, GPBA02B_PIN_MODE_OUTPUT);
    gpba02b_pin_write(POWER_LOCK_IO_PORT, POWER_LOCK_IO_PIN, false);
    return ESP_OK;
}