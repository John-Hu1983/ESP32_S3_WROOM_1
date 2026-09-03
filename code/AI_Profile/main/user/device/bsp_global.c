#include "bsp_global.h"

#define TAG "bsp_global"

/*
 * brief : Set power-lock output level for board power domain hold.
 * input : enable - true to lock power on, false to release.
 * output: none.
 * type  : private
 */
static void _bsp_set_power(bool enable) {
    gpba02b_set_io_mode(POWER_LOCK_IO_PORT, POWER_LOCK_IO_PIN, GPBA02B_IO_STYLE_OUTPUT_CMOS);
    gpba02b_write_io_level(POWER_LOCK_IO_PORT, POWER_LOCK_IO_PIN, enable ? 1 : 0);
}

/*
 * brief : Initialize one PWM output with default frequency and duty.
 * input : port - PWM port; pin - PWM pin; fre - frequency enum; percent - duty percent.
 * output: none.
 * type  : private
 */
static void _bsp_init_pwm(gpba02b_port_t port, uint8_t pin, gpba02b_pwm_freq_t fre,
                          uint8_t percent) {
    gpba02b_config_pwm_mode(port, pin);
    gpba02b_set_pwm_frequency(port, fre);
    gpba02b_set_pwm_duty(port, pin, percent);
}

/*
 * brief : Initialize board button GPIO pins as pull-up inputs.
 * input : none.
 * output: none.
 * type  : private
 */
static void _bsp_init_button(void) {
    gpba02b_set_io_mode(BUTTON_UP_IO_PORT, BUTTON_UP_IO_PIN, GPBA02B_IO_STYLE_INPUT_PULL_HIGH);
    gpba02b_set_io_mode(BUTTON_DOWN_IO_PORT, BUTTON_DOWN_IO_PIN, GPBA02B_IO_STYLE_INPUT_PULL_HIGH);
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
 * brief : Enable or disable audio power-control GPIOs.
 * input : enable - true to enable audio hardware, false to disable.
 * output: none.
 * type  : public
 */
void bsp_set_audio_ctrl(bool enable) {
    gpba02b_set_io_mode(PDM_ENABLE_PORT, PDM_ENABLE_PIN, GPBA02B_IO_STYLE_OUTPUT_CMOS);
    gpba02b_write_io_level(PDM_ENABLE_PORT, PDM_ENABLE_PIN, enable ? 1 : 0);

    gpba02b_set_io_mode(I2S_ENABLE_PORT, I2S_ENABLE_PIN, GPBA02B_IO_STYLE_OUTPUT_CMOS);
    gpba02b_write_io_level(I2S_ENABLE_PORT, I2S_ENABLE_PIN, enable ? 1 : 0);
}

/*
 * brief : Execute LCD reset pulse sequence.
 * input : none.
 * output: none.
 * type  : public
 */
void bsp_reset_lcd(void) {
    gpba02b_set_io_mode(LCD_IO_RESET_PORT, LCD_IO_RESET_PIN, GPBA02B_IO_STYLE_OUTPUT_CMOS);
    gpba02b_write_io_level(LCD_IO_RESET_PORT, LCD_IO_RESET_PIN, 1);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpba02b_write_io_level(LCD_IO_RESET_PORT, LCD_IO_RESET_PIN, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpba02b_write_io_level(LCD_IO_RESET_PORT, LCD_IO_RESET_PIN, 1);
    vTaskDelay(pdMS_TO_TICKS(10));
}

/*
 * brief : Initialize board-level peripheral chain and user desktop services.
 * input : none.
 * output: none.
 * type  : public
 */
void bsp_init_total(void) {
    ESP_LOGI(TAG, "Initializing BSP...");
    ESP_ERROR_CHECK(gpba02b_init_object());
    _bsp_set_power(true);
    bsp_set_audio_ctrl(true);
    _bsp_init_pwm(GPBA02B_PORT_C, 1, GPBA02B_PWM_FREQ_1343HZ_DIV32, 12);
    _bsp_init_button();
    bsp_reset_lcd();
    desktop_start_task();
}
