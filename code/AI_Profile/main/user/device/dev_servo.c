#include "dev_servo.h"

#define TAG "dev_servo"

static bool is_inited = false;
static Servo_Ctr_s* servo_ctr = NULL;
static algo_pid_s* servo_pid = NULL;
static uint16_t s_ble_report_elapsed_ms = SERVO_BLE_REPORT_PERIOD_MS;
static float s_pid_submin_acc = 0.0f;

static void _servo_init_pid(uint16_t ms);
static void _servo_report_ble(float duty_f, uint16_t vr, uint16_t ms);

/*
 * brief : servo_read_pid_profile.
 * input : none.
 * output: return value from this function.
 * type  : public
 * theory: expose PID runtime snapshot pointer for UI or diagnostics without copying state.
 */
algo_pid_s* servo_read_pid_profile(void) {
    return servo_pid;
}

/*
 * brief : servo_read_motor_profile.
 * input : none.
 * output: return value from this function.
 * type  : public
 * theory: expose motor runtime snapshot pointer so callers can read pwm and direction state.
 */
Servo_Ctr_s* servo_read_motor_profile(void) {
    return servo_ctr;
}

/*
 * brief : servo_run_motor.
 * input : see parameters.
 * output: none.
 * type  : public
 * theory: map abstract motor direction to dual PWM outputs and apply raw duty immediately.
 */
void servo_run_motor(Mot_Dir_e dir, uint8_t duty) {
    if (servo_ctr == NULL) {
        return;
    }

    servo_ctr->dir = dir;

    switch (dir) {
    case MOTOR_DIRECTION_STOP:
        servo_ctr->pwm_a = 0U;
        servo_ctr->pwm_b = 0U;
        break;
    case MOTOR_DIRECTION_CW:
        servo_ctr->pwm_a = duty;
        servo_ctr->pwm_b = 0U;

        break;
    case MOTOR_DIRECTION_CCW:
        servo_ctr->pwm_a = 0U;
        servo_ctr->pwm_b = duty;

        break;
    case MOTOR_DIRECTION_BRAKE:
        servo_ctr->pwm_a = duty;
        servo_ctr->pwm_b = duty;
        break;
    default:
        break;
    }
    gpba02b_set_pwm_raw(SERVO_PWMA_PORT, SERVO_PWMA_PIN, servo_ctr->pwm_a);
    gpba02b_set_pwm_raw(SERVO_PWMB_PORT, SERVO_PWMB_PIN, servo_ctr->pwm_b);
}

/*
 * brief : servo_get_adc_value.
 * input : see parameters.
 * output: return value from this function.
 * type  : public
 * theory: synchronously convert and average current feedback samples for the control loop.
 */
esp_err_t servo_get_adc_value(uint16_t* adc_value) {
     uint16_t raw = 0U;
     uint32_t sum = 0U;
     uint32_t i = 0U;
    esp_err_t ret = ESP_OK;

    if (adc_value == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (servo_ctr == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    for (i = 0U; i < SERVO_ADC_SAMPLE_COUNT; ++i) {
        ret = hal_adc_read(SERVO_ADC_UNIT, SERVO_ADC_CHANNEL, &raw);
        if (ret != ESP_OK) {
            return ret;
        }
        sum += raw;
    }

    *adc_value = (uint16_t)(sum / SERVO_ADC_SAMPLE_COUNT);

    return ESP_OK;
}

/*
 * brief : servo_debug_profile.
 * input : see parameters.
 * output: return value from this function.
 * type  : public
 * theory: execute a short directional pulse pattern then sample ADC for quick functional diagnostics.
 */
int servo_debug_profile(btn_status_e btn) {
    esp_err_t ret = ESP_OK;

    if (btn != Btn_Up_Click && btn != Btn_Down_Click) {
        return -1;
    }

    if (btn == Btn_Up_Click) {
        servo_run_motor(MOTOR_DIRECTION_CW, 64u);
    }
    else if (btn == Btn_Down_Click) {
        servo_run_motor(MOTOR_DIRECTION_CCW, 64u);
    }

    delay_ms(10);
    servo_run_motor(MOTOR_DIRECTION_BRAKE, 255u);
    delay_ms(10);
    ret = servo_get_adc_value(&servo_ctr->vr);
    if (ret != ESP_OK) {
        servo_run_motor(MOTOR_DIRECTION_STOP, 0U);
        return -1;
    }
    ESP_LOGI(TAG, "servo adc value: %u", servo_ctr->vr);
    return 0;
}

/*
 * brief : servo_init_hw.
 * input : none.
 * output: return value from this function.
 * type  : public
 * theory: allocate runtime state, configure ADC/PWM peripherals, and enter safe stopped output state.
 */
esp_err_t servo_init_hw(void) {
    esp_err_t ret = ESP_OK;

    // Check if the servo hardware is already initialized.
    if (is_inited) {
        return ESP_OK;
    }

    // Allocate memory for the servo control structure if it hasn't been allocated yet.
    if (servo_ctr == NULL) {
        servo_ctr = (Servo_Ctr_s*)heap_caps_calloc(1,
                                                   sizeof(Servo_Ctr_s),
                                                   MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (servo_ctr == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }
    memset(servo_ctr, 0, sizeof(Servo_Ctr_s));

    if (servo_pid == NULL) {
        servo_pid = (algo_pid_s*)heap_caps_calloc(1,
                                                  sizeof(algo_pid_s),
                                                  MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (servo_pid == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }
    memset(servo_pid, 0, sizeof(algo_pid_s));

    ret = hal_adc_init(SERVO_ADC_UNIT, SERVO_ADC_CHANNEL);
    if (ret != ESP_OK) {
        return ret;
    }

    // Initialize the PID controller for the servo.
    _servo_init_pid(SERVO_UI_TASK_PERIOD_MS);
    servo_set_target(2048);

    ret = gpba02b_init_object();
    if ((ret != ESP_OK) && (ret != ESP_ERR_INVALID_STATE)) {
        return ret;
    }

    ret = gpba02b_config_pwm_mode(SERVO_PWMA_PORT, SERVO_PWMA_PIN);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = gpba02b_config_pwm_mode(SERVO_PWMB_PORT, SERVO_PWMB_PIN);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = gpba02b_set_pwm_frequency(SERVO_PWMA_PORT, SERVO_PWM_FREQUENCY);
    if (ret != ESP_OK) {
        return ret;
    }

    if (SERVO_PWMB_PORT != SERVO_PWMA_PORT) {
        ret = gpba02b_set_pwm_frequency(SERVO_PWMB_PORT, SERVO_PWM_FREQUENCY);
        if (ret != ESP_OK) {
            return ret;
        }
    }

    ret = gpba02b_set_pwm_percent(SERVO_PWMA_PORT, SERVO_PWMA_PIN, 0U);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = gpba02b_set_pwm_percent(SERVO_PWMB_PORT, SERVO_PWMB_PIN, 0U);
    if (ret != ESP_OK) {
        return ret;
    }

    is_inited = true;

    ESP_LOGI(TAG,
             "hw init done pwm=(%d:%u,%d:%u)",
             (int)SERVO_PWMA_PORT,
             (unsigned)SERVO_PWMA_PIN,
             (int)SERVO_PWMB_PORT,
             (unsigned)SERVO_PWMB_PIN);

    return ESP_OK;
}

/*
 * brief : servo_deinit_hw.
 * input : none.
 * output: return value from this function.
 * type  : public
 * theory: drive PWM to zero and clear servo state while ADC ownership remains in the HAL.
 */
esp_err_t servo_deinit_hw(void) {
    esp_err_t ret_a = ESP_OK;
    esp_err_t ret_b = ESP_OK;

    ret_a = gpba02b_set_pwm_percent(SERVO_PWMA_PORT, SERVO_PWMA_PIN, 0U);
    ret_b = gpba02b_set_pwm_percent(SERVO_PWMB_PORT, SERVO_PWMB_PIN, 0U);

    if (servo_ctr != NULL) {
        heap_caps_free(servo_ctr);
        servo_ctr = NULL;
    }

    if (servo_pid != NULL) {
        heap_caps_free(servo_pid);
        servo_pid = NULL;
    }

    is_inited = false;
    s_pid_submin_acc = 0.0f;

    if (ret_a != ESP_OK) {
        return ret_a;
    }

    return ret_b;
}

/*
 * brief : _servo_init_pid.
 * input : see parameters.
 * output: none.
 * type  : private
 * theory: initialize PID gains, limits, and runtime accumulators into a deterministic startup state.
 */
static void _servo_init_pid(uint16_t ms) {
    if (servo_pid == NULL) {
        return;
    }

    // parameters for PID: kp, ki, kd
    // 0.2,   0.08 , 0.005
    // 0.18 , 0.07 , 0.015
    // 0.095，0.18 ，0.01
    servo_pid->cfg.kp = 0.095f;
    servo_pid->cfg.ki = 0.18f;
    servo_pid->cfg.kd = 0.01f;
    servo_pid->cfg.out_min = -SERVO_PID_DUTY_MAX;
    servo_pid->cfg.out_max = SERVO_PID_DUTY_MAX;
    servo_pid->cfg.i_min = -50.0f;
    servo_pid->cfg.i_max = 50.0f;
    servo_pid->cfg.dt_s = ((float)ms) / 1000.0f;
    servo_pid->cfg.err_deadband = SERVO_PID_TOL_COMPUTE;
    servo_pid->cfg.quiescent_deadband = SERVO_PID_TOL_QUIESCENT;

    servo_pid->first_cycle = true;
    servo_pid->update = false;
    servo_pid->target = 0.0f;
    servo_pid->feedback = 0.0f;
    servo_pid->prev_err = 0.0f;
    servo_pid->i_acc = 0.0f;
    servo_pid->curr_err = 0.0f;
    servo_pid->inited = true;
    s_pid_submin_acc = 0.0f;
}

/*
 * brief : _servo_report_ble.
 * input : see parameters.
 * output: none.
 * type  : private
 * theory: rate-limit BLE telemetry to a fixed period and emit paired duty/feedback messages.
 */
static void _servo_report_ble(float duty_f, uint16_t vr, uint16_t ms) {
    uint16_t next_elapsed = 0U;
    char tx_text[32] = { 0 };

    if (!ble_is_connected()) {
        s_ble_report_elapsed_ms = SERVO_BLE_REPORT_PERIOD_MS;
        return;
    }

    if (ms == 0U) {
        ms = 1U;
    }

    if (s_ble_report_elapsed_ms < SERVO_BLE_REPORT_PERIOD_MS) {
        next_elapsed = (uint16_t)(s_ble_report_elapsed_ms + ms);
        if (next_elapsed > SERVO_BLE_REPORT_PERIOD_MS) {
            next_elapsed = SERVO_BLE_REPORT_PERIOD_MS;
        }
        s_ble_report_elapsed_ms = next_elapsed;
    }

    if (s_ble_report_elapsed_ms < SERVO_BLE_REPORT_PERIOD_MS) {
        return;
    }

    s_ble_report_elapsed_ms = 0U;

    snprintf(tx_text, sizeof(tx_text), "AT+MOTPWM: %.3f", (double)duty_f);
    (void)ble_send_text(tx_text);
    snprintf(tx_text, sizeof(tx_text), "AT+MOTVR: %u", (unsigned)vr);
    (void)ble_send_text(tx_text);
}

/*
 * brief : servo_set_target.
 * input : see parameters.
 * output: none.
 * type  : public
 * theory: mark a changed target for precise convergence before switching to the wider idle deadband.
 */
void servo_set_target(float target) {
    if (servo_pid == NULL) {
        return;
    }
    if (servo_pid->target != target) {
        servo_pid->update = true;
        servo_pid->first_cycle = true;
    }
    servo_pid->target = target;
    ESP_LOGI(TAG, "Servo target set to %.3f", target);
}

/*
 * brief : servo_set_pid_para.
 * input : see parameters.
 * output: none.
 * type  : public
 * theory: support runtime gain retuning by directly replacing kp, ki, and kd in controller config.
 */
void servo_set_pid_para(float kp, float ki, float kd) {
    if (servo_pid == NULL) {
        return;
    }

    servo_pid->cfg.kp = kp;
    servo_pid->cfg.ki = ki;
    servo_pid->cfg.kd = kd;
    ESP_LOGI(TAG, "Servo PID parameters set to Kp=%.3f, Ki=%.3f, Kd=%.3f", kp, ki, kd);
}

/*
 * brief : servo_compute_via_pid.
 * input : see parameters.
 * output: none.
 * type  : public
 * theory: run one closed-loop control step (sample, pid solve, direction/duty shaping, and motor apply).
 */
void servo_compute_via_pid(uint16_t ms) {
    esp_err_t ret = ESP_OK;
    float feedback = 0.0f;
    float output = 0.0f;
    float duty_f = 0.0f;
    uint8_t duty = 0U;
    Mot_Dir_e dir = MOTOR_DIRECTION_STOP;

    if ((!is_inited) || (servo_ctr == NULL) || (servo_pid == NULL)) {
        return;
    }

    ret = servo_get_adc_value(&servo_ctr->vr);
    if (ret != ESP_OK) {
        s_pid_submin_acc = 0.0f;
        servo_pid->first_cycle = true;
        servo_run_motor(MOTOR_DIRECTION_STOP, 0U);
        return;
    }
    feedback = (float)servo_ctr->vr;

    ret = algo_pid_step(servo_pid, feedback, &output);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "PID step failed with error: %d", ret);
        servo_run_motor(MOTOR_DIRECTION_STOP, 0U);
        return;
    }

    if (servo_pid->curr_err == 0.0f) {
        s_pid_submin_acc = 0.0f;
        servo_run_motor(MOTOR_DIRECTION_BRAKE, SERVO_PID_BRAKE_DUTY);
        _servo_report_ble(0.0f, servo_ctr->vr, ms);

        return;
    }

    dir = (servo_pid->curr_err > 0.0f) ? MOTOR_DIRECTION_CW : MOTOR_DIRECTION_CCW;

    duty_f = output;
    if (duty_f < 0.0f) {
        duty_f = -duty_f;
    }

    // Convert sub-minimum demand into minimum-duty pulses to avoid always-on min duty limit cycle.
    if ((duty_f > 0.0f) && (duty_f < SERVO_PID_DUTY_MIN)) {
        s_pid_submin_acc += (duty_f / SERVO_PID_DUTY_MIN);
        if (s_pid_submin_acc >= 1.0f) {
            duty_f = SERVO_PID_DUTY_MIN;
            s_pid_submin_acc -= 1.0f;
        }
        else {
            duty_f = 0.0f;
        }
    }
    else {
        s_pid_submin_acc = 0.0f;
    }

    if (duty_f > SERVO_PID_DUTY_MAX) {
        duty_f = SERVO_PID_DUTY_MAX;
    }

    duty = (uint8_t)duty_f;
    if (duty == 0U) {
        servo_run_motor(MOTOR_DIRECTION_STOP, 0U);
        return;
    }

    servo_run_motor(dir, duty);
    _servo_report_ble(duty_f, servo_ctr->vr, ms);
}
