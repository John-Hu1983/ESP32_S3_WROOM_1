#pragma once

#include <stdbool.h>
#include <stdint.h>

#include <esp_err.h>

#include "bsp/gpba02b.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    gpba02b_port_t port;
    uint8_t pin;
} bsp_env_pin_t;

typedef struct {
    bsp_env_pin_t power_lock_pin;
    bsp_env_pin_t pdm_enable_pin;
    bsp_env_pin_t i2s_enable_pin;
    bsp_env_pin_t pidm_enable_pin;

    bsp_env_pin_t button_up_pin;
    bsp_env_pin_t button_down_pin;

    bsp_env_pin_t lcd_reset_pin;

    bsp_env_pin_t camera_reset_pin;
    bsp_env_pin_t camera_pwdn_pin;
    bsp_env_pin_t camera_light_pin;

    bsp_env_pin_t rc522_reset_pin;
    bsp_env_pin_t rc522_enable_pin;

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
} bsp_env_config_t;

typedef struct {
    bsp_env_config_t config;
} bsp_env_t;

enum {
    BSP_ENV_INVALID_PORT = 0xFF,
    BSP_ENV_INVALID_PIN = 0xFF,
};

bsp_env_pin_t bsp_env_invalid_pin_config(void);
void bsp_env_get_default_config(bsp_env_config_t* config);

void bsp_env_init(bsp_env_t* env, const bsp_env_config_t* config);

esp_err_t bsp_env_initialize(bsp_env_t* env);
esp_err_t bsp_env_lcd_reset(bsp_env_t* env);
esp_err_t bsp_env_camera_reset(bsp_env_t* env);
esp_err_t bsp_env_rc522_reset(bsp_env_t* env);

#ifdef __cplusplus
}
#endif
