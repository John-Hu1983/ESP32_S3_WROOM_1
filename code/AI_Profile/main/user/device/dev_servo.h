#pragma once

#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_log.h"

#include "user/algorithm/algo_pid.h"
#include "user/communication/ble/ble.h"
#include "user/device/dev_button.h"
#include "user/device/dev_gpba02b.h"
#include "user/gui/servo_ui.h"
#include "user/hal/hal_adc.h"
#include "user/inc/bsp_config.h"

#ifdef __cplusplus
extern "C" {
#endif

// clang-format off

#define SERVO_PWM_FREQUENCY                  (GPBA02B_PWM_FREQ_671HZ_DIV64)

#if !defined(SERVO_PWMA_PORT) || !defined(SERVO_PWMA_PIN) || !defined(SERVO_PWMB_PORT) \
    || !defined(SERVO_PWMB_PIN)
#error "SERVO_PWMA_PORT/SERVO_PWMA_PIN/SERVO_PWMB_PORT/SERVO_PWMB_PIN must be defined in board config"
#endif


#define SERVO_PID_DUTY_MAX          (255.0f)
#define SERVO_PID_DUTY_MIN          (10.0f)
#define SERVO_PID_TOL_EXIT_ADC      (25.0f)
#define SERVO_PID_BRAKE_DUTY        (255U)
#define SERVO_BLE_REPORT_PERIOD_MS  (100U)

// clang-format on

typedef enum {
    MOTOR_DIRECTION_STOP = 0,
    MOTOR_DIRECTION_CW = 1,
    MOTOR_DIRECTION_CCW = 2,
    MOTOR_DIRECTION_BRAKE = 3,
} e_motor_direction;

typedef struct {
    uint8_t pwm_a;
    uint8_t pwm_b;
    uint16_t vr;
    e_motor_direction dir;
    hal_adc_cfg_t adc_cfg;
} t_servo_ctr;

esp_err_t servo_init_hw(void);
esp_err_t servo_deinit_hw(void);
algo_pid_s* servo_read_pid_profile(void);
t_servo_ctr* servo_read_motor_profile(void);
void servo_set_target(float target);
void servo_set_pid_para(float kp, float ki, float kd);
void servo_compute_via_pid(uint16_t ms);
int servo_debug_profile(btn_status_e btn);
void servo_run_motor(e_motor_direction dir, uint8_t duty);
uint16_t servo_get_adc_value(void);
#ifdef __cplusplus
}
#endif
