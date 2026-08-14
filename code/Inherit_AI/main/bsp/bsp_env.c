#include "bsp/bsp_env.h"

#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#define TAG "BspEnv"

static const uint8_t kDefaultPwmDuty10Percent = 26;
static const uint8_t kDefaultPwmClockDivFor1Khz = 3;
static const uint32_t kDefaultEnableStepDelayMs = 2;
static const uint32_t kDefaultResetPulseMs = 10;
static const uint32_t kDefaultResetReleaseDelayMs = 20;
static const uint32_t kDefaultLcdResetReleaseDelayMs = 120;

static bool bsp_env_is_valid_pin(const bsp_env_pin_t* pin) {
    if (pin == NULL) {
        return false;
    }
    return pin->port <= GPBA02B_PORT_C && pin->pin <= 7;
}

static void bsp_env_delay_ms(uint32_t delay_ms) {
    if (delay_ms == 0) {
        return;
    }
    vTaskDelay(pdMS_TO_TICKS(delay_ms));
}

static esp_err_t bsp_env_configure_output(const bsp_env_pin_t* pin, bool level) {
    if (!bsp_env_is_valid_pin(pin)) {
        return ESP_OK;
    }
    return gpba02b_config_io_output(gpba02b_instance(), pin->port, pin->pin, false, level);
}

static esp_err_t bsp_env_configure_input_pull_high(const bsp_env_pin_t* pin) {
    if (!bsp_env_is_valid_pin(pin)) {
        return ESP_OK;
    }
    return gpba02b_config_io_input_mode(gpba02b_instance(), pin->port, pin->pin,
                                        GPBA02B_IO_INPUT_PULL_HIGH);
}

static esp_err_t bsp_env_write_pin(const bsp_env_pin_t* pin, bool level) {
    if (!bsp_env_is_valid_pin(pin)) {
        return ESP_OK;
    }
    return gpba02b_write_io(gpba02b_instance(), pin->port, pin->pin, level);
}

static esp_err_t bsp_env_pulse_reset(const bsp_env_pin_t* pin, uint32_t pulse_ms,
                                     uint32_t settle_ms) {
    if (!bsp_env_is_valid_pin(pin)) {
        return ESP_OK;
    }

    esp_err_t err = bsp_env_write_pin(pin, false);
    if (err != ESP_OK) {
        return err;
    }
    bsp_env_delay_ms(pulse_ms);

    err = bsp_env_write_pin(pin, true);
    if (err != ESP_OK) {
        return err;
    }
    bsp_env_delay_ms(settle_ms);

    return ESP_OK;
}

static esp_err_t bsp_env_configure_pwm(const bsp_env_t* env) {
    if (env->config.pwm_enable_mask_port_a == 0 && env->config.pwm_enable_mask_port_c == 0) {
        return ESP_OK;
    }

    gpba02b_t* gpba02b = gpba02b_instance();

    esp_err_t err = gpba02b_pwm_set_clock_div(gpba02b, env->config.pwm_clock_div_port_a,
                                              env->config.pwm_clock_div_port_c);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set GPBA02B PWM clock divider: %s", esp_err_to_name(err));
        return err;
    }

    if (env->config.pwm_enable_mask_port_a != 0) {
        for (uint8_t channel = 0; channel < 8; ++channel) {
            if ((env->config.pwm_enable_mask_port_a & (1U << channel)) == 0) {
                continue;
            }
            err = gpba02b_pwm_set_channel_duty(gpba02b, GPBA02B_PORT_A, channel,
                                               env->config.pwm_duty);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Failed to set PWM duty on PortA channel %u: %s", channel,
                         esp_err_to_name(err));
                return err;
            }
        }

        err = gpba02b_pwm_enable_channels(gpba02b, GPBA02B_PORT_A,
                                          env->config.pwm_enable_mask_port_a);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to enable GPBA02B PWM PortA channels: %s", esp_err_to_name(err));
            return err;
        }
    }

    if (env->config.pwm_enable_mask_port_c != 0) {
        for (uint8_t channel = 0; channel < 8; ++channel) {
            if ((env->config.pwm_enable_mask_port_c & (1U << channel)) == 0) {
                continue;
            }
            err = gpba02b_pwm_set_channel_duty(gpba02b, GPBA02B_PORT_C, channel,
                                               env->config.pwm_duty);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Failed to set PWM duty on PortC channel %u: %s", channel,
                         esp_err_to_name(err));
                return err;
            }
        }

        err = gpba02b_pwm_enable_channels(gpba02b, GPBA02B_PORT_C,
                                          env->config.pwm_enable_mask_port_c);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to enable GPBA02B PWM PortC channels: %s", esp_err_to_name(err));
            return err;
        }
    }

    return ESP_OK;
}

static esp_err_t bsp_env_apply_enable_sequence(const bsp_env_t* env) {
    esp_err_t err = bsp_env_write_pin(&env->config.power_lock_pin, true);
    if (err != ESP_OK) {
        return err;
    }
    bsp_env_delay_ms(env->config.enable_step_delay_ms);

    err = bsp_env_write_pin(&env->config.pdm_enable_pin, true);
    if (err != ESP_OK) {
        return err;
    }
    bsp_env_delay_ms(env->config.enable_step_delay_ms);

    err = bsp_env_write_pin(&env->config.i2s_enable_pin, true);
    if (err != ESP_OK) {
        return err;
    }
    bsp_env_delay_ms(env->config.enable_step_delay_ms);

    err = bsp_env_write_pin(&env->config.pidm_enable_pin, true);
    if (err != ESP_OK) {
        return err;
    }
    bsp_env_delay_ms(env->config.enable_step_delay_ms);

    if (env->config.camera_present) {
        err = bsp_env_write_pin(&env->config.camera_pwdn_pin, false);
        if (err != ESP_OK) {
            return err;
        }
        bsp_env_delay_ms(env->config.enable_step_delay_ms);
    }

    if (env->config.rc522_present) {
        err = bsp_env_write_pin(&env->config.rc522_enable_pin, true);
        if (err != ESP_OK) {
            return err;
        }
        bsp_env_delay_ms(env->config.enable_step_delay_ms);
    }

    return ESP_OK;
}

bsp_env_pin_t bsp_env_invalid_pin_config(void) {
    bsp_env_pin_t pin = {(gpba02b_port_t)BSP_ENV_INVALID_PORT, BSP_ENV_INVALID_PIN};
    return pin;
}

void bsp_env_get_default_config(bsp_env_config_t* config) {
    if (config == NULL) {
        return;
    }

    config->power_lock_pin = bsp_env_invalid_pin_config();
    config->pdm_enable_pin = bsp_env_invalid_pin_config();
    config->i2s_enable_pin = bsp_env_invalid_pin_config();
    config->pidm_enable_pin = bsp_env_invalid_pin_config();
    config->button_up_pin = bsp_env_invalid_pin_config();
    config->button_down_pin = bsp_env_invalid_pin_config();
    config->lcd_reset_pin = bsp_env_invalid_pin_config();
    config->camera_reset_pin = bsp_env_invalid_pin_config();
    config->camera_pwdn_pin = bsp_env_invalid_pin_config();
    config->camera_light_pin = bsp_env_invalid_pin_config();
    config->rc522_reset_pin = bsp_env_invalid_pin_config();
    config->rc522_enable_pin = bsp_env_invalid_pin_config();

    config->camera_present = false;
    config->rc522_present = false;

    config->pwm_enable_mask_port_a = 0;
    config->pwm_enable_mask_port_c = 0;
    config->pwm_clock_div_port_a = kDefaultPwmClockDivFor1Khz;
    config->pwm_clock_div_port_c = kDefaultPwmClockDivFor1Khz;
    config->pwm_duty = kDefaultPwmDuty10Percent;

    config->enable_step_delay_ms = kDefaultEnableStepDelayMs;
    config->reset_pulse_ms = kDefaultResetPulseMs;
    config->reset_release_delay_ms = kDefaultResetReleaseDelayMs;
    config->lcd_reset_release_delay_ms = kDefaultLcdResetReleaseDelayMs;
}

void bsp_env_init(bsp_env_t* env, const bsp_env_config_t* config) {
    if (env == NULL) {
        return;
    }

    if (config == NULL) {
        bsp_env_get_default_config(&env->config);
        return;
    }

    env->config = *config;
}

esp_err_t bsp_env_initialize(bsp_env_t* env) {
    if (env == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = bsp_env_configure_output(&env->config.power_lock_pin, true);
    if (err != ESP_OK) {
        return err;
    }

    err = bsp_env_configure_output(&env->config.pdm_enable_pin, false);
    if (err != ESP_OK) {
        return err;
    }

    err = bsp_env_configure_output(&env->config.i2s_enable_pin, false);
    if (err != ESP_OK) {
        return err;
    }

    err = bsp_env_configure_output(&env->config.pidm_enable_pin, false);
    if (err != ESP_OK) {
        return err;
    }

    err = bsp_env_configure_input_pull_high(&env->config.button_up_pin);
    if (err != ESP_OK) {
        return err;
    }

    err = bsp_env_configure_input_pull_high(&env->config.button_down_pin);
    if (err != ESP_OK) {
        return err;
    }

    err = bsp_env_configure_output(&env->config.lcd_reset_pin, true);
    if (err != ESP_OK) {
        return err;
    }

    if (env->config.camera_present) {
        err = bsp_env_configure_output(&env->config.camera_reset_pin, true);
        if (err != ESP_OK) {
            return err;
        }

        err = bsp_env_configure_output(&env->config.camera_pwdn_pin, true);
        if (err != ESP_OK) {
            return err;
        }

        err = bsp_env_configure_output(&env->config.camera_light_pin, false);
        if (err != ESP_OK) {
            return err;
        }
    }

    if (env->config.rc522_present) {
        err = bsp_env_configure_output(&env->config.rc522_reset_pin, true);
        if (err != ESP_OK) {
            return err;
        }

        err = bsp_env_configure_output(&env->config.rc522_enable_pin, false);
        if (err != ESP_OK) {
            return err;
        }
    }

    err = bsp_env_configure_pwm(env);
    if (err != ESP_OK) {
        return err;
    }

    err = bsp_env_apply_enable_sequence(env);
    if (err != ESP_OK) {
        return err;
    }

    err = bsp_env_lcd_reset(env);
    if (err != ESP_OK) {
        return err;
    }

    if (env->config.camera_present) {
        err = bsp_env_camera_reset(env);
        if (err != ESP_OK) {
            return err;
        }
    }

    if (env->config.rc522_present) {
        err = bsp_env_rc522_reset(env);
        if (err != ESP_OK) {
            return err;
        }
    }

    ESP_LOGI(TAG, "GPBA02B peripheral environment initialized");
    return ESP_OK;
}

esp_err_t bsp_env_lcd_reset(bsp_env_t* env) {
    if (env == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    return bsp_env_pulse_reset(&env->config.lcd_reset_pin, env->config.reset_pulse_ms,
                               env->config.lcd_reset_release_delay_ms);
}

esp_err_t bsp_env_camera_reset(bsp_env_t* env) {
    if (env == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!env->config.camera_present) {
        return ESP_OK;
    }

    esp_err_t err = bsp_env_write_pin(&env->config.camera_pwdn_pin, false);
    if (err != ESP_OK) {
        return err;
    }
    bsp_env_delay_ms(env->config.enable_step_delay_ms);

    return bsp_env_pulse_reset(&env->config.camera_reset_pin, env->config.reset_pulse_ms,
                               env->config.reset_release_delay_ms);
}

esp_err_t bsp_env_rc522_reset(bsp_env_t* env) {
    if (env == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!env->config.rc522_present) {
        return ESP_OK;
    }

    esp_err_t err = bsp_env_write_pin(&env->config.rc522_enable_pin, true);
    if (err != ESP_OK) {
        return err;
    }
    bsp_env_delay_ms(env->config.enable_step_delay_ms);

    return bsp_env_pulse_reset(&env->config.rc522_reset_pin, env->config.reset_pulse_ms,
                               env->config.reset_release_delay_ms);
}
