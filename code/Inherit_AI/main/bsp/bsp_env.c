#include "bsp/bsp_env.h"

#include "bsp/gpba02b.h"
#include "../esp32-s3-wroom-1-n16r8/config.h"

static esp_err_t bsp_env_config_output(gpba02b_port_t port, uint8_t pin, bool level) {
    esp_err_t err =
        gpba02b_config_io_output_mode(port, pin, GPBA02B_IO_OUTPUT_PUSH_PULL, level);
    if (err != ESP_OK) {
        return err;
    }
    return gpba02b_write_io(port, pin, level);
}

static esp_err_t bsp_env_config_input_pull_up(gpba02b_port_t port, uint8_t pin) {
    return gpba02b_config_io_input(port, pin, true);
}

esp_err_t bsp_env_pwm_init(void) {
    esp_err_t err =
        gpba02b_pwm_set_clock_div(PWM_GPBA02B_PA_CLOCK_DIV, PWM_GPBA02B_PC_CLOCK_DIV);
    if (err != ESP_OK) {
        return err;
    }

    err = gpba02b_pwm_set_channel_duty(PWM_GPBA02B_07_PORT, PWM_GPBA02B_07_PIN,
                                       PWM_GPBA02B_DUTY_10_PERCENT);
    if (err != ESP_OK) {
        return err;
    }
    err = gpba02b_pwm_set_channel_duty(PWM_GPBA02B_08_PORT, PWM_GPBA02B_08_PIN,
                                       PWM_GPBA02B_DUTY_10_PERCENT);
    if (err != ESP_OK) {
        return err;
    }
    err = gpba02b_pwm_set_channel_duty(PWM_GPBA02B_09_PORT, PWM_GPBA02B_09_PIN,
                                       PWM_GPBA02B_DUTY_10_PERCENT);
    if (err != ESP_OK) {
        return err;
    }
    err = gpba02b_pwm_set_channel_duty(PWM_GPBA02B_10_PORT, PWM_GPBA02B_10_PIN,
                                       PWM_GPBA02B_DUTY_10_PERCENT);
    if (err != ESP_OK) {
        return err;
    }
    err = gpba02b_pwm_set_channel_duty(PWM_GPBA02B_11_PORT, PWM_GPBA02B_11_PIN,
                                       PWM_GPBA02B_DUTY_10_PERCENT);
    if (err != ESP_OK) {
        return err;
    }
    err = gpba02b_pwm_set_channel_duty(PWM_GPBA02B_12_PORT, PWM_GPBA02B_12_PIN,
                                       PWM_GPBA02B_DUTY_10_PERCENT);
    if (err != ESP_OK) {
        return err;
    }
    err = gpba02b_pwm_set_channel_duty(PWM_GPBA02B_13_PORT, PWM_GPBA02B_13_PIN,
                                       PWM_GPBA02B_DUTY_10_PERCENT);
    if (err != ESP_OK) {
        return err;
    }

    // Keep outputs disabled here. This only prepares clock and duty.
    return ESP_OK;
}

esp_err_t bsp_env_lcd_init(void) {
    return bsp_env_config_output(LCD_IO_RESET_PORT, LCD_IO_RESET_PIN, true);
}

esp_err_t bsp_env_camera_init(void) {
#if defined(CAM_IO_RESET_PORT) && defined(CAM_IO_RESET_PIN) && defined(CAM_IO_PWDN_PORT) && \
    defined(CAM_IO_PWDN_PIN)
    esp_err_t err = bsp_env_config_output(CAM_IO_RESET_PORT, CAM_IO_RESET_PIN, true);
    if (err != ESP_OK) {
        return err;
    }

    err = bsp_env_config_output(CAM_IO_PWDN_PORT, CAM_IO_PWDN_PIN, true);
    if (err != ESP_OK) {
        return err;
    }

#if defined(CAM_IO_LIGHT_PORT) && defined(CAM_IO_LIGHT_PIN)
    err = bsp_env_config_output(CAM_IO_LIGHT_PORT, CAM_IO_LIGHT_PIN, false);
    if (err != ESP_OK) {
        return err;
    }
#endif
#endif

    return ESP_OK;
}

esp_err_t bsp_env_initialize(void) {
    esp_err_t err = bsp_env_config_output(POWER_LOCK_IO_PORT, POWER_LOCK_IO_PIN, true);
    if (err != ESP_OK) {
        return err;
    }

    err = bsp_env_config_output(PDM_EN_PORT, PDM_EN_PIN, false);
    if (err != ESP_OK) {
        return err;
    }

    err = bsp_env_config_output(I2S_EN_PORT, I2S_EN_PIN, false);
    if (err != ESP_OK) {
        return err;
    }

    err = bsp_env_config_output(PIDM_EN_PORT, PIDM_EN_PIN, false);
    if (err != ESP_OK) {
        return err;
    }

    err = bsp_env_config_input_pull_up(BUTTON_UP_IO_PORT, BUTTON_UP_IO_PIN);
    if (err != ESP_OK) {
        return err;
    }

    err = bsp_env_config_input_pull_up(BUTTON_DOWN_IO_PORT, BUTTON_DOWN_IO_PIN);
    if (err != ESP_OK) {
        return err;
    }

#if defined(RC522_RST_PORT) && defined(RC522_RST_PIN)
    err = bsp_env_config_output(RC522_RST_PORT, RC522_RST_PIN, true);
    if (err != ESP_OK) {
        return err;
    }
#endif

    return ESP_OK;
}

esp_err_t bsp_env_lcd_reset(void) {
    esp_err_t err = gpba02b_write_io(LCD_IO_RESET_PORT, LCD_IO_RESET_PIN, false);
    if (err != ESP_OK) {
        return err;
    }

    return gpba02b_write_io(LCD_IO_RESET_PORT, LCD_IO_RESET_PIN, true);
}

esp_err_t bsp_env_camera_reset(void) {
#if defined(CAM_IO_RESET_PORT) && defined(CAM_IO_RESET_PIN) && defined(CAM_IO_PWDN_PORT) && \
    defined(CAM_IO_PWDN_PIN)
    esp_err_t err = gpba02b_write_io(CAM_IO_PWDN_PORT, CAM_IO_PWDN_PIN, false);
    if (err != ESP_OK) {
        return err;
    }

    err = gpba02b_write_io(CAM_IO_RESET_PORT, CAM_IO_RESET_PIN, false);
    if (err != ESP_OK) {
        return err;
    }

    return gpba02b_write_io(CAM_IO_RESET_PORT, CAM_IO_RESET_PIN, true);
#else
    return ESP_OK;
#endif
}

esp_err_t bsp_env_rc522_reset(void) {
#if defined(RC522_RST_PORT) && defined(RC522_RST_PIN)
#if defined(RC522_ENABLE_PORT) && defined(RC522_ENABLE_PIN)
    esp_err_t err = gpba02b_write_io(RC522_ENABLE_PORT, RC522_ENABLE_PIN, true);
    if (err != ESP_OK) {
        return err;
    }
#endif

    esp_err_t err = gpba02b_write_io(RC522_RST_PORT, RC522_RST_PIN, false);
    if (err != ESP_OK) {
        return err;
    }

    return gpba02b_write_io(RC522_RST_PORT, RC522_RST_PIN, true);
#else
    return ESP_OK;
#endif
}
