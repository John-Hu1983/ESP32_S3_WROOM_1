#include "bsp_global.h"

#define TAG "bsp_global"

static void bsp_set_power_lock(bool enable) {
    gpba02b_set_io_mode(POWER_LOCK_IO_PORT, POWER_LOCK_IO_PIN, GPBA02B_IO_STYLE_OUTPUT_CMOS);
    gpba02b_write_io_level(POWER_LOCK_IO_PORT, POWER_LOCK_IO_PIN, enable ? 1 : 0);
}

static void bsp_start_pwm_default(gpba02b_port_t port, uint8_t pin, gpba02b_pwm_freq_t fre,
                                  uint8_t percent) {
    gpba02b_config_pwm_mode(port, pin);
    gpba02b_set_pwm_frequency(port, fre);
    gpba02b_set_pwm_duty(port, pin, percent);
}

static void bsp_config_button_gpio(void) {
    gpba02b_set_io_mode(BUTTON_UP_IO_PORT, BUTTON_UP_IO_PIN, GPBA02B_IO_STYLE_INPUT_PULL_HIGH);
    gpba02b_set_io_mode(BUTTON_DOWN_IO_PORT, BUTTON_DOWN_IO_PIN, GPBA02B_IO_STYLE_INPUT_PULL_HIGH);
}

void bsp_lcd_reset_sequence(void) {
    gpba02b_set_io_mode(LCD_IO_RESET_PORT, LCD_IO_RESET_PIN, GPBA02B_IO_STYLE_OUTPUT_CMOS);
    gpba02b_write_io_level(LCD_IO_RESET_PORT, LCD_IO_RESET_PIN, 1);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpba02b_write_io_level(LCD_IO_RESET_PORT, LCD_IO_RESET_PIN, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpba02b_write_io_level(LCD_IO_RESET_PORT, LCD_IO_RESET_PIN, 1);
    vTaskDelay(pdMS_TO_TICKS(10));
}

void bsp_init_total(void) {
    ESP_LOGI(TAG, "Initializing BSP...");
    ESP_ERROR_CHECK(gpba02b_init_object());
    bsp_set_power_lock(true);
    bsp_start_pwm_default(GPBA02B_PORT_C, 1, GPBA02B_PWM_FREQ_1343HZ_DIV32, 12);
    bsp_config_button_gpio();
    bsp_lcd_reset_sequence();
    ESP_ERROR_CHECK(desktop_app_start());
}
