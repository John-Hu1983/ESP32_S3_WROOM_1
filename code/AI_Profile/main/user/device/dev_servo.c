#include "dev_servo.h"

#define TAG "dev_servo"

static bool is_inited = false;
static t_servo_ctr* servo_ctr = NULL;

static void _servo_perform_motor(e_motor_direction dir, uint8_t duty) {
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

uint16_t servo_get_adc_value(void) {
    int adc_val = 0;
    if (servo_ctr == NULL) {
        return 0;
    }

    hal_adc_read_raw(&servo_ctr->adc_cfg, &adc_val);
    return (uint16_t)adc_val;
}

int servo_debug_profile(btn_status_e btn) {
    if (btn != Btn_Up_Click && btn != Btn_Down_Click) {
        return -1;
    }

    if (btn == Btn_Up_Click) {
        _servo_perform_motor(MOTOR_DIRECTION_CW, 64u);
    }
    else if (btn == Btn_Down_Click) {
        _servo_perform_motor(MOTOR_DIRECTION_CCW, 64u);
    }

    delay_ms(10);
    _servo_perform_motor(MOTOR_DIRECTION_BRAKE, 255u);
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
 */
esp_err_t servo_deinit_hw(void) {
    esp_err_t ret_a = ESP_OK;
    esp_err_t ret_b = ESP_OK;

    ret_a = gpba02b_set_pwm_percent(SERVO_PWMA_PORT, SERVO_PWMA_PIN, 0U);
    ret_b = gpba02b_set_pwm_percent(SERVO_PWMB_PORT, SERVO_PWMB_PIN, 0U);
    hal_adc_deinit(&servo_ctr->adc_cfg);

    heap_caps_free(servo_ctr);
    servo_ctr = NULL;
    is_inited = false;

    if (ret_a != ESP_OK) {
        return ret_a;
    }

    return ret_b;
}
