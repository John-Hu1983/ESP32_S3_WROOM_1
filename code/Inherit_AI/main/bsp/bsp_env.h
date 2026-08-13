#pragma once

#include <stdint.h>

#include <esp_err.h>

#include "bsp/gpba02b.h"

class BspEnv {
public:
    struct Pin {
        Gpba02b::Port port;
        uint8_t pin;
    };

    struct Config {
        Pin power_lock_pin;
        Pin pdm_enable_pin;
        Pin i2s_enable_pin;
        Pin pidm_enable_pin;

        Pin lcd_reset_pin;

        Pin camera_reset_pin;
        Pin camera_pwdn_pin;
        Pin camera_light_pin;

        Pin rc522_reset_pin;
        Pin rc522_enable_pin;

        bool camera_present;
        bool rc522_present;

        uint8_t pwm_enable_mask_port_a;
        uint8_t pwm_enable_mask_port_c;
        uint8_t pwm_clock_div_port_a;
        uint8_t pwm_clock_div_port_c;
        uint8_t pwm_duty;

        uint32_t enable_step_delay_ms;
        uint32_t reset_pulse_ms;
        uint32_t reset_release_delay_ms;
        uint32_t lcd_reset_release_delay_ms;
    };

    static constexpr Gpba02b::Port kInvalidPort = static_cast<Gpba02b::Port>(0xFF);
    static constexpr uint8_t kInvalidPin = 0xFF;

    static Pin InvalidPinConfig();
    static void GetDefaultConfig(Config* config);

    explicit BspEnv(const Config& config);

    esp_err_t Initialize();
    esp_err_t LcdReset();
    esp_err_t CameraReset();
    esp_err_t Rc522Reset();

private:
    bool IsValidPin(const Pin& pin) const;
    void DelayMs(uint32_t delay_ms) const;
    esp_err_t ConfigureOutput(const Pin& pin, bool level);
    esp_err_t WritePin(const Pin& pin, bool level);
    esp_err_t PulseReset(const Pin& pin, uint32_t pulse_ms, uint32_t settle_ms);
    esp_err_t ConfigurePwm();
    esp_err_t ApplyEnableSequence();

    Config config_;
};
