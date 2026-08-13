#include "bsp/bsp_env.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>

#define TAG "BspEnv"

namespace {
constexpr uint8_t kDefaultPwmDuty10Percent = 26;
constexpr uint8_t kDefaultPwmClockDivFor1Khz = 3;
constexpr uint32_t kDefaultEnableStepDelayMs = 2;
constexpr uint32_t kDefaultResetPulseMs = 10;
constexpr uint32_t kDefaultResetReleaseDelayMs = 20;
constexpr uint32_t kDefaultLcdResetReleaseDelayMs = 120;
}  // namespace

BspEnv::Pin BspEnv::InvalidPinConfig() {
    return {kInvalidPort, kInvalidPin};
}

void BspEnv::GetDefaultConfig(BspEnv::Config* config) {
    if (config == nullptr) {
        return;
    }

    config->power_lock_pin = InvalidPinConfig();
    config->pdm_enable_pin = InvalidPinConfig();
    config->i2s_enable_pin = InvalidPinConfig();
    config->pidm_enable_pin = InvalidPinConfig();
    config->button_up_pin = InvalidPinConfig();
    config->button_down_pin = InvalidPinConfig();
    config->lcd_reset_pin = InvalidPinConfig();
    config->camera_reset_pin = InvalidPinConfig();
    config->camera_pwdn_pin = InvalidPinConfig();
    config->camera_light_pin = InvalidPinConfig();
    config->rc522_reset_pin = InvalidPinConfig();
    config->rc522_enable_pin = InvalidPinConfig();

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

BspEnv::BspEnv(const BspEnv::Config& config) : config_(config) {
}

bool BspEnv::IsValidPin(const BspEnv::Pin& pin) const {
    return pin.port <= Gpba02b::kPortC && pin.pin <= 7;
}

void BspEnv::DelayMs(uint32_t delay_ms) const {
    if (delay_ms == 0) {
        return;
    }
    vTaskDelay(pdMS_TO_TICKS(delay_ms));
}

esp_err_t BspEnv::ConfigureOutput(const BspEnv::Pin& pin, bool level) {
    if (!IsValidPin(pin)) {
        return ESP_OK;
    }
    return Gpba02b::Instance().config_io_output(pin.port, pin.pin, false, level);
}

esp_err_t BspEnv::ConfigureInputPullHigh(const BspEnv::Pin& pin) {
    if (!IsValidPin(pin)) {
        return ESP_OK;
    }
    return Gpba02b::Instance().config_io_input(pin.port, pin.pin, Gpba02b::kIoInputPullHigh);
}

esp_err_t BspEnv::WritePin(const BspEnv::Pin& pin, bool level) {
    if (!IsValidPin(pin)) {
        return ESP_OK;
    }
    return Gpba02b::Instance().write_io(pin.port, pin.pin, level);
}

esp_err_t BspEnv::PulseReset(const BspEnv::Pin& pin, uint32_t pulse_ms, uint32_t settle_ms) {
    if (!IsValidPin(pin)) {
        return ESP_OK;
    }

    esp_err_t err = WritePin(pin, false);
    if (err != ESP_OK) {
        return err;
    }
    DelayMs(pulse_ms);

    err = WritePin(pin, true);
    if (err != ESP_OK) {
        return err;
    }
    DelayMs(settle_ms);

    return ESP_OK;
}

esp_err_t BspEnv::ConfigurePwm() {
    if (config_.pwm_enable_mask_port_a == 0 && config_.pwm_enable_mask_port_c == 0) {
        return ESP_OK;
    }

    Gpba02b& gpba02b = Gpba02b::Instance();

    esp_err_t err = gpba02b.PwmSetClockDiv(config_.pwm_clock_div_port_a,
                                           config_.pwm_clock_div_port_c);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set GPBA02B PWM clock divider: %s", esp_err_to_name(err));
        return err;
    }

    if (config_.pwm_enable_mask_port_a != 0) {
        for (uint8_t channel = 0; channel < 8; ++channel) {
            if ((config_.pwm_enable_mask_port_a & (1U << channel)) == 0) {
                continue;
            }
            err = gpba02b.PwmSetChannelDuty(Gpba02b::kPortA, channel, config_.pwm_duty);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Failed to set PWM duty on PortA channel %u: %s", channel,
                         esp_err_to_name(err));
                return err;
            }
        }

        err = gpba02b.PwmEnableChannels(Gpba02b::kPortA, config_.pwm_enable_mask_port_a);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to enable GPBA02B PWM PortA channels: %s",
                     esp_err_to_name(err));
            return err;
        }
    }

    if (config_.pwm_enable_mask_port_c != 0) {
        for (uint8_t channel = 0; channel < 8; ++channel) {
            if ((config_.pwm_enable_mask_port_c & (1U << channel)) == 0) {
                continue;
            }
            err = gpba02b.PwmSetChannelDuty(Gpba02b::kPortC, channel, config_.pwm_duty);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Failed to set PWM duty on PortC channel %u: %s", channel,
                         esp_err_to_name(err));
                return err;
            }
        }

        err = gpba02b.PwmEnableChannels(Gpba02b::kPortC, config_.pwm_enable_mask_port_c);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to enable GPBA02B PWM PortC channels: %s",
                     esp_err_to_name(err));
            return err;
        }
    }

    return ESP_OK;
}

esp_err_t BspEnv::ApplyEnableSequence() {
    esp_err_t err = WritePin(config_.power_lock_pin, true);
    if (err != ESP_OK) {
        return err;
    }
    DelayMs(config_.enable_step_delay_ms);

    err = WritePin(config_.pdm_enable_pin, true);
    if (err != ESP_OK) {
        return err;
    }
    DelayMs(config_.enable_step_delay_ms);

    err = WritePin(config_.i2s_enable_pin, true);
    if (err != ESP_OK) {
        return err;
    }
    DelayMs(config_.enable_step_delay_ms);

    err = WritePin(config_.pidm_enable_pin, true);
    if (err != ESP_OK) {
        return err;
    }
    DelayMs(config_.enable_step_delay_ms);

    if (config_.camera_present) {
        err = WritePin(config_.camera_pwdn_pin, false);
        if (err != ESP_OK) {
            return err;
        }
        DelayMs(config_.enable_step_delay_ms);
    }

    if (config_.rc522_present) {
        err = WritePin(config_.rc522_enable_pin, true);
        if (err != ESP_OK) {
            return err;
        }
        DelayMs(config_.enable_step_delay_ms);
    }

    return ESP_OK;
}

esp_err_t BspEnv::Initialize() {
    esp_err_t err = ConfigureOutput(config_.power_lock_pin, true);
    if (err != ESP_OK) {
        return err;
    }

    err = ConfigureOutput(config_.pdm_enable_pin, false);
    if (err != ESP_OK) {
        return err;
    }

    err = ConfigureOutput(config_.i2s_enable_pin, false);
    if (err != ESP_OK) {
        return err;
    }

    err = ConfigureOutput(config_.pidm_enable_pin, false);
    if (err != ESP_OK) {
        return err;
    }

    err = ConfigureInputPullHigh(config_.button_up_pin);
    if (err != ESP_OK) {
        return err;
    }

    err = ConfigureInputPullHigh(config_.button_down_pin);
    if (err != ESP_OK) {
        return err;
    }

    err = ConfigureOutput(config_.lcd_reset_pin, true);
    if (err != ESP_OK) {
        return err;
    }

    if (config_.camera_present) {
        err = ConfigureOutput(config_.camera_reset_pin, true);
        if (err != ESP_OK) {
            return err;
        }

        err = ConfigureOutput(config_.camera_pwdn_pin, true);
        if (err != ESP_OK) {
            return err;
        }

        err = ConfigureOutput(config_.camera_light_pin, false);
        if (err != ESP_OK) {
            return err;
        }
    }

    if (config_.rc522_present) {
        err = ConfigureOutput(config_.rc522_reset_pin, true);
        if (err != ESP_OK) {
            return err;
        }

        err = ConfigureOutput(config_.rc522_enable_pin, false);
        if (err != ESP_OK) {
            return err;
        }
    }

    err = ConfigurePwm();
    if (err != ESP_OK) {
        return err;
    }

    err = ApplyEnableSequence();
    if (err != ESP_OK) {
        return err;
    }

    err = LcdReset();
    if (err != ESP_OK) {
        return err;
    }

    if (config_.camera_present) {
        err = CameraReset();
        if (err != ESP_OK) {
            return err;
        }
    }

    if (config_.rc522_present) {
        err = Rc522Reset();
        if (err != ESP_OK) {
            return err;
        }
    }

    ESP_LOGI(TAG, "GPBA02B peripheral environment initialized");
    return ESP_OK;
}

esp_err_t BspEnv::LcdReset() {
    return PulseReset(config_.lcd_reset_pin,
                      config_.reset_pulse_ms,
                      config_.lcd_reset_release_delay_ms);
}

esp_err_t BspEnv::CameraReset() {
    if (!config_.camera_present) {
        return ESP_OK;
    }

    esp_err_t err = WritePin(config_.camera_pwdn_pin, false);
    if (err != ESP_OK) {
        return err;
    }
    DelayMs(config_.enable_step_delay_ms);

    return PulseReset(config_.camera_reset_pin,
                      config_.reset_pulse_ms,
                      config_.reset_release_delay_ms);
}

esp_err_t BspEnv::Rc522Reset() {
    if (!config_.rc522_present) {
        return ESP_OK;
    }

    esp_err_t err = WritePin(config_.rc522_enable_pin, true);
    if (err != ESP_OK) {
        return err;
    }
    DelayMs(config_.enable_step_delay_ms);

    return PulseReset(config_.rc522_reset_pin,
                      config_.reset_pulse_ms,
                      config_.reset_release_delay_ms);
}
