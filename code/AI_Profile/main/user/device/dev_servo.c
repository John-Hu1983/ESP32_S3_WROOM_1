#include "dev_servo.h"

#define TAG "dev_servo"

#define SERVO_PID_DUTY_MAX (255.0f)
#define SERVO_PID_DUTY_MIN (10.0f)
#define SERVO_PID_TOL_EXIT_ADC (4.0f)
#define SERVO_PID_BRAKE_DUTY (255U)
#define SERVO_BLE_REPORT_PERIOD_MS (100U)
#define SERVO_PID_DEBUG_ENABLE (1)
#define SERVO_PID_DEBUG_PERIOD (5U)

static bool is_inited = false;
static t_servo_ctr* servo_ctr = NULL;
static algo_pid_s* servo_pid = NULL;
static uint16_t s_ble_report_elapsed_ms = SERVO_BLE_REPORT_PERIOD_MS;
static float s_pid_submin_acc = 0.0f;

#if SERVO_PID_DEBUG_ENABLE
static uint32_t s_pid_dbg_seq = 0U;
static e_motor_direction s_pid_dbg_prev_dir = MOTOR_DIRECTION_STOP;
static bool s_pid_dbg_prev_brake = false;
#endif

static void _servo_init_pid(uint16_t ms);
static void _servo_report_ble(float duty_f, uint16_t vr, uint16_t ms);

#if SERVO_PID_DEBUG_ENABLE
static const char* _servo_dir_name(e_motor_direction dir) {
    switch (dir) {
    case MOTOR_DIRECTION_STOP:
        return "STOP";
    case MOTOR_DIRECTION_CW:
        return "CW";
    case MOTOR_DIRECTION_CCW:
        return "CCW";
    case MOTOR_DIRECTION_BRAKE:
        return "BRAKE";
    default:
        return "UNKNOWN";
    }
}
#endif

/*
 * brief : servo_run_motor.
 * input : see parameters.
 * output: none.
 * type  : public
 * theory: map abstract motor direction to dual PWM outputs and apply raw duty immediately.
 */
void servo_run_motor(e_motor_direction dir, uint8_t duty) {
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
 * input : none.
 * output: return value from this function.
 * type  : public
 * theory: read the latest configured ADC channel and expose it as servo feedback sample.
 */
uint16_t servo_get_adc_value(void) {
    int adc_val = 0;
    if (servo_ctr == NULL) {
        return 0;
    }

    hal_adc_read_raw(&servo_ctr->adc_cfg, &adc_val);
    return (uint16_t)adc_val;
}

/*
 * brief : servo_debug_profile.
 * input : see parameters.
 * output: return value from this function.
 * type  : public
 * theory: execute a short directional pulse pattern then sample ADC for quick functional diagnostics.
 */
int servo_debug_profile(btn_status_e btn) {
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
    servo_ctr->vr = servo_get_adc_value();
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
        servo_ctr = (t_servo_ctr*)heap_caps_calloc(1,
                                                   sizeof(t_servo_ctr),
                                                   MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (servo_ctr == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }
    memset(servo_ctr, 0, sizeof(t_servo_ctr));

    if (servo_pid == NULL) {
        servo_pid = (algo_pid_s*)heap_caps_calloc(1,
                                                  sizeof(algo_pid_s),
                                                  MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (servo_pid == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }
    memset(servo_pid, 0, sizeof(algo_pid_s));

    // Initialize the ADC configuration for the servo control structure.
    servo_ctr->adc_cfg = (hal_adc_cfg_t){
        .unit = SERVO_ADC_UNIT,
        .channel = SERVO_ADC_CHANNEL,
        .atten = SERVO_ADC_ATTENUATION,
        .bitwidth = SERVO_ADC_BITWIDTH,
        .enable_cali = true,
    };
    hal_adc_init(&servo_ctr->adc_cfg);
    hal_adc_config_channel(&servo_ctr->adc_cfg);

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
 * theory: drive PWM to zero, release ADC runtime dependencies, and clear module ownership state.
 */
esp_err_t servo_deinit_hw(void) {
    esp_err_t ret_a = ESP_OK;
    esp_err_t ret_b = ESP_OK;

    ret_a = gpba02b_set_pwm_percent(SERVO_PWMA_PORT, SERVO_PWMA_PIN, 0U);
    ret_b = gpba02b_set_pwm_percent(SERVO_PWMB_PORT, SERVO_PWMB_PIN, 0U);

    if (servo_ctr != NULL) {
        hal_adc_deinit(&servo_ctr->adc_cfg);
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

static void _servo_init_pid(uint16_t ms) {
    if (servo_pid == NULL) {
        return;
    }

    servo_pid->cfg.kp = 0.1f;
    servo_pid->cfg.ki = 0.0f;
    servo_pid->cfg.kd = 0.0f;
    servo_pid->cfg.out_min = -SERVO_PID_DUTY_MAX;
    servo_pid->cfg.out_max = SERVO_PID_DUTY_MAX;
    servo_pid->cfg.i_min = -50.0f;
    servo_pid->cfg.i_max = 50.0f;
    servo_pid->cfg.dt_s = ((float)ms) / 1000.0f;
    servo_pid->cfg.err_deadband = SERVO_PID_TOL_EXIT_ADC;

    servo_pid->first_cycle = true;
    servo_pid->target = 0.0f;
    servo_pid->prev_err = 0.0f;
    servo_pid->i_acc = 0.0f;
    servo_pid->curr_err = 0.0f;
    servo_pid->inited = true;
    s_pid_submin_acc = 0.0f;
}

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

void servo_set_target(float target) {
    if (servo_pid == NULL) {
        return;
    }
    servo_pid->target = target;
    ESP_LOGI(TAG, "Servo target set to %.3f", target);

#if SERVO_PID_DEBUG_ENABLE
    ESP_LOGI(TAG,
             "PID_CFG target=%.3f kp=%.3f ki=%.3f kd=%.3f dt=%.4f deadband=%.3f out=[%.1f,%.1f] i=[%.1f,%.1f]",
             target,
             servo_pid->cfg.kp,
             servo_pid->cfg.ki,
             servo_pid->cfg.kd,
             servo_pid->cfg.dt_s,
             servo_pid->cfg.err_deadband,
             servo_pid->cfg.out_min,
             servo_pid->cfg.out_max,
             servo_pid->cfg.i_min,
             servo_pid->cfg.i_max);
#endif
}

void servo_set_pid_para(float kp, float ki, float kd) {
    if (servo_pid == NULL) {
        return;
    }

    servo_pid->cfg.kp = kp;
    servo_pid->cfg.ki = ki;
    servo_pid->cfg.kd = kd;
    ESP_LOGI(TAG, "Servo PID parameters set to Kp=%.3f, Ki=%.3f, Kd=%.3f", kp, ki, kd);

#if SERVO_PID_DEBUG_ENABLE
    ESP_LOGI(TAG,
             "PID_CFG target=%.3f kp=%.3f ki=%.3f kd=%.3f dt=%.4f deadband=%.3f out=[%.1f,%.1f] i=[%.1f,%.1f]",
             servo_pid->target,
             servo_pid->cfg.kp,
             servo_pid->cfg.ki,
             servo_pid->cfg.kd,
             servo_pid->cfg.dt_s,
             servo_pid->cfg.err_deadband,
             servo_pid->cfg.out_min,
             servo_pid->cfg.out_max,
             servo_pid->cfg.i_min,
             servo_pid->cfg.i_max);
#endif
}

void servo_compute_via_pid(uint16_t ms) {
    esp_err_t ret = ESP_OK;
    float feedback = 0.0f;
    float output = 0.0f;
    float duty_f = 0.0f;
#if SERVO_PID_DEBUG_ENABLE
    float err_raw = 0.0f;           // target - feedback before PID deadband handling.
    float err_used = 0.0f;          // curr_err used by PID after deadband handling.
    float i_before = 0.0f;          // i_acc before algo_pid_step.
    float i_after = 0.0f;           // i_acc after algo_pid_step.
    float prev_err_before = 0.0f;   // prev_err snapshot before algo_pid_step.
    float p_est = 0.0f;
    float d_est = 0.0f;
    float out_raw_est = 0.0f;
    bool first_cycle_before = false;
    bool is_brake_mode = false;
    bool is_lifted_min_duty = false;
    bool is_submin_skip = false;
    bool is_saturated_out = false;
    bool mode_changed = false;
    const char* mode_text = "RUN";
#endif
    uint8_t duty = 0U;
    e_motor_direction dir = MOTOR_DIRECTION_STOP;

    if ((!is_inited) || (servo_ctr == NULL) || (servo_pid == NULL)) {
        return;
    }

    servo_ctr->vr = servo_get_adc_value();
    feedback = (float)servo_ctr->vr;

#if SERVO_PID_DEBUG_ENABLE
    err_raw = servo_pid->target - feedback;
    i_before = servo_pid->i_acc;
    prev_err_before = servo_pid->prev_err;
    first_cycle_before = servo_pid->first_cycle;
#endif

    ret = algo_pid_step(servo_pid, feedback, &output);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "PID step failed with error: %d", ret);
        servo_run_motor(MOTOR_DIRECTION_STOP, 0U);
        return;
    }

#if SERVO_PID_DEBUG_ENABLE
    err_used = servo_pid->curr_err;
    i_after = servo_pid->i_acc;
    p_est = servo_pid->cfg.kp * err_used;
    d_est = first_cycle_before ? 0.0f : (servo_pid->cfg.kd * (err_used - prev_err_before) / servo_pid->cfg.dt_s);
    out_raw_est = p_est + i_after + d_est;
    is_saturated_out = ((output >= servo_pid->cfg.out_max) || (output <= servo_pid->cfg.out_min));
#endif

    if (servo_pid->curr_err == 0.0f) {
        s_pid_submin_acc = 0.0f;
        servo_run_motor(MOTOR_DIRECTION_BRAKE, SERVO_PID_BRAKE_DUTY);
        _servo_report_ble(0.0f, servo_ctr->vr, ms);

#if SERVO_PID_DEBUG_ENABLE
        is_brake_mode = true;
        mode_text = "BRAKE";
        mode_changed = (s_pid_dbg_prev_dir != MOTOR_DIRECTION_BRAKE) || !s_pid_dbg_prev_brake;
        if (mode_changed) {
            ESP_LOGI(TAG,
                     "PID_EVT mode=%s target=%.1f fb=%u err_raw=%.3f err=%.3f out=%.3f duty=%u",
                     mode_text,
                     servo_pid->target,
                     (unsigned)servo_ctr->vr,
                     err_raw,
                     err_used,
                     output,
                     (unsigned)SERVO_PID_BRAKE_DUTY);
        }

        s_pid_dbg_prev_dir = MOTOR_DIRECTION_BRAKE;
        s_pid_dbg_prev_brake = true;
        s_pid_dbg_seq++;
        if ((s_pid_dbg_seq % SERVO_PID_DEBUG_PERIOD) == 0U) {
            ESP_LOGI(TAG,
                     "PID_DBG seq=%lu mode=%s target=%.1f fb=%u err_raw=%.3f err=%.3f p=%.3f i0=%.3f i1=%.3f d=%.3f raw=%.3f out=%.3f sat=%d lift=%d dir=%s duty=%u",
                     (unsigned long)s_pid_dbg_seq,
                     mode_text,
                     servo_pid->target,
                     (unsigned)servo_ctr->vr,
                     err_raw,
                     err_used,
                     p_est,
                     i_before,
                     i_after,
                     d_est,
                     out_raw_est,
                     output,
                     is_saturated_out ? 1 : 0,
                     0,
                     _servo_dir_name(MOTOR_DIRECTION_BRAKE),
                     (unsigned)SERVO_PID_BRAKE_DUTY);
        }
#endif

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

#if SERVO_PID_DEBUG_ENABLE
            is_lifted_min_duty = true;
#endif
        }
        else {
            duty_f = 0.0f;

#if SERVO_PID_DEBUG_ENABLE
            is_submin_skip = true;
#endif
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

#if SERVO_PID_DEBUG_ENABLE
    mode_text = "RUN";
    mode_changed = (dir != s_pid_dbg_prev_dir) || s_pid_dbg_prev_brake || is_lifted_min_duty
        || is_submin_skip || is_saturated_out;
    if (mode_changed) {
        ESP_LOGI(TAG,
                 "PID_EVT mode=%s target=%.1f fb=%u err_raw=%.3f err=%.3f out=%.3f duty=%u dir=%s sat=%d lift=%d skip=%d",
                 mode_text,
                 servo_pid->target,
                 (unsigned)servo_ctr->vr,
                 err_raw,
                 err_used,
                 output,
                 (unsigned)duty,
                 _servo_dir_name(dir),
                 is_saturated_out ? 1 : 0,
                 is_lifted_min_duty ? 1 : 0,
                 is_submin_skip ? 1 : 0);
    }

    s_pid_dbg_prev_dir = dir;
    s_pid_dbg_prev_brake = false;
    s_pid_dbg_seq++;
    if ((s_pid_dbg_seq % SERVO_PID_DEBUG_PERIOD) == 0U) {
        ESP_LOGI(TAG,
                 "PID_DBG seq=%lu mode=%s target=%.1f fb=%u err_raw=%.3f err=%.3f p=%.3f i0=%.3f i1=%.3f d=%.3f raw=%.3f out=%.3f sat=%d lift=%d skip=%d dir=%s duty=%u",
                 (unsigned long)s_pid_dbg_seq,
                 mode_text,
                 servo_pid->target,
                 (unsigned)servo_ctr->vr,
                 err_raw,
                 err_used,
                 p_est,
                 i_before,
                 i_after,
                 d_est,
                 out_raw_est,
                 output,
                 is_saturated_out ? 1 : 0,
                 is_lifted_min_duty ? 1 : 0,
                 is_submin_skip ? 1 : 0,
                 _servo_dir_name(dir),
                 (unsigned)duty);
    }
#endif
}
