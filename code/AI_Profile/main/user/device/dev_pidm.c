#include "dev_pidm.h"

#define TAG "dev_pidm"

static bool s_pidm_ready = false;
static bool s_pidm_metal_detected = false;
static uint32_t s_pidm_trigger_count = 0U;
static uint32_t s_pidm_response_slope = 0U;
static uint32_t s_pidm_threshold_slope = 0U;
static int32_t s_pidm_baseline_q8 = 0;
static int32_t s_pidm_noise_q8 = 0;
static uint16_t s_pidm_calibration_count = 0U;
static uint8_t s_pidm_detect_hits = 0U;
static uint8_t s_pidm_release_hits = 0U;
static esp_err_t s_pidm_trigger_error = ESP_OK;
static portMUX_TYPE s_pidm_lock = portMUX_INITIALIZER_UNLOCKED;

/*
 * brief : Return the absolute value of one bounded signed detector term.
 * input : value - signed fixed-point term.
 * output: non-negative magnitude.
 * type  : private
 * theory: detector values are bounded by the 12-bit ADC range, so signed negation cannot overflow.
 */
static int32_t _pidm_abs_i32(int32_t value) {
    return (value < 0) ? -value : value;
}

/*
 * brief : Calculate the adaptive slope margin above baseline.
 * input : noise - current mean absolute slope deviation.
 * output: threshold margin in ADC counts per millisecond.
 * type  : private
 * theory: scale with measured noise while retaining a minimum margin in quiet conditions.
 */
static uint32_t _pidm_detection_margin(uint32_t noise) {
    uint32_t margin = noise * PIDM_DETECT_NOISE_MULTIPLIER;

    if (margin < PIDM_DETECT_MIN_MARGIN) {
        margin = PIDM_DETECT_MIN_MARGIN;
    }

    return margin;
}

/*
 * brief : Capture the post-charge ADC response slope.
 * input : slope - destination for the robust slope estimate.
 * output: ESP_OK on success; otherwise ADC or response error code.
 * type  : private
 * theory: synchronously sample the 200-1000 us rise, then use trimmed window amplitude as a slope proxy.
 */
static esp_err_t _pidm_capture_response_slope(uint32_t* slope) {
    uint16_t values[PIDM_ADC_SAMPLE_COUNT] = { 0 };
    uint16_t key = 0U;
    uint32_t low_average = 0U;
    uint32_t high_average = 0U;
    uint32_t i = 0U;
    uint32_t j = 0U;
    esp_err_t ret = ESP_OK;

    if (slope == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_rom_delay_us(PIDM_RESPONSE_START_US);
    for (i = 0U; i < PIDM_ADC_SAMPLE_COUNT; ++i) {
        ret = hal_adc_read(PIDM_ADC_UNIT, PIDM_ADC_CHANNEL, &values[i]);
        if (ret != ESP_OK) {
            return ret;
        }
    }

    for (i = 1U; i < PIDM_ADC_SAMPLE_COUNT; ++i) {
        key = values[i];
        j = i;
        while ((j > 0U) && (values[j - 1U] > key)) {
            values[j] = values[j - 1U];
            j--;
        }
        values[j] = key;
    }

    low_average = ((uint32_t)values[0] + values[1]) / 2U;
    high_average = ((uint32_t)values[PIDM_ADC_SAMPLE_COUNT - 1U]
                    + values[PIDM_ADC_SAMPLE_COUNT - 2U])
                   / 2U;
    if (high_average <= low_average) {
        *slope = 0U;
        return ESP_OK;
    }

    *slope = ((high_average - low_average) * 1000U)
             / (PIDM_RESPONSE_END_US - PIDM_RESPONSE_START_US);
    return ESP_OK;
}

/*
 * brief : Update adaptive PIDM metal detection state.
 * input : response_slope - latest robust ADC slope estimate.
 * output: none.
 * type  : private
 * theory: learn a no-metal baseline/noise floor, then apply consecutive-hit and release hysteresis.
 */
static void _pidm_update_detection(uint32_t response_slope) {
    bool was_detected = false;
    bool is_detected = false;
    bool calibration_completed = false;
    uint32_t baseline = 0U;
    uint32_t noise = 0U;
    uint32_t margin = 0U;
    uint32_t release_threshold = 0U;
    uint32_t divisor = 0U;
    int32_t sample_q8 = (int32_t)(response_slope << 8U);
    int32_t error_q8 = 0;
    int32_t deviation_q8 = 0;

    portENTER_CRITICAL(&s_pidm_lock);
    was_detected = s_pidm_metal_detected;
    s_pidm_response_slope = response_slope;

    if (s_pidm_calibration_count < PIDM_DETECT_CALIBRATION_COUNT) {
        divisor = (uint32_t)s_pidm_calibration_count + 1U;
        if (s_pidm_calibration_count == 0U) {
            s_pidm_baseline_q8 = sample_q8;
            s_pidm_noise_q8 = 0;
        }
        else {
            error_q8 = sample_q8 - s_pidm_baseline_q8;
            s_pidm_baseline_q8 += error_q8 / (int32_t)divisor;
            deviation_q8 = _pidm_abs_i32(sample_q8 - s_pidm_baseline_q8);
            s_pidm_noise_q8 += (deviation_q8 - s_pidm_noise_q8)
                               / (int32_t)divisor;
        }
        s_pidm_calibration_count++;
        s_pidm_detect_hits = 0U;
        s_pidm_release_hits = 0U;
        s_pidm_metal_detected = false;
        calibration_completed =
            (s_pidm_calibration_count == PIDM_DETECT_CALIBRATION_COUNT);
    }
    else {
        baseline = (uint32_t)(s_pidm_baseline_q8 >> 8U);
        noise = (uint32_t)(s_pidm_noise_q8 >> 8U);
        margin = _pidm_detection_margin(noise);
        s_pidm_threshold_slope = baseline + margin;

        if (!s_pidm_metal_detected) {
            if (response_slope > s_pidm_threshold_slope) {
                if (s_pidm_detect_hits < PIDM_DETECT_ASSERT_COUNT) {
                    s_pidm_detect_hits++;
                }
            }
            else {
                s_pidm_detect_hits = 0U;
                error_q8 = sample_q8 - s_pidm_baseline_q8;
                s_pidm_baseline_q8 += error_q8
                                      / (1 << PIDM_DETECT_BASELINE_FILTER_SHIFT);
                deviation_q8 = _pidm_abs_i32(sample_q8 - s_pidm_baseline_q8);
                s_pidm_noise_q8 += (deviation_q8 - s_pidm_noise_q8)
                                   / (1 << PIDM_DETECT_NOISE_FILTER_SHIFT);
            }

            if (s_pidm_detect_hits >= PIDM_DETECT_ASSERT_COUNT) {
                s_pidm_metal_detected = true;
                s_pidm_release_hits = 0U;
            }
        }
        else {
            release_threshold = baseline
                                + ((margin * PIDM_DETECT_RELEASE_HYST_PERCENT) / 100U);
            if (response_slope < release_threshold) {
                if (s_pidm_release_hits < PIDM_DETECT_RELEASE_COUNT) {
                    s_pidm_release_hits++;
                }
            }
            else {
                s_pidm_release_hits = 0U;
            }

            if (s_pidm_release_hits >= PIDM_DETECT_RELEASE_COUNT) {
                s_pidm_metal_detected = false;
                s_pidm_detect_hits = 0U;
            }
        }
    }

    baseline = (uint32_t)(s_pidm_baseline_q8 >> 8U);
    noise = (uint32_t)(s_pidm_noise_q8 >> 8U);
    s_pidm_threshold_slope = baseline + _pidm_detection_margin(noise);
    is_detected = s_pidm_metal_detected;
    portEXIT_CRITICAL(&s_pidm_lock);

    if (calibration_completed) {
        ESP_LOGI(TAG,
                 "calibrated: baseline=%lu threshold=%lu",
                 (unsigned long)baseline,
                 (unsigned long)s_pidm_threshold_slope);
    }
    if (was_detected != is_detected) {
        ESP_LOGI(TAG,
                 "metal %s: slope=%lu baseline=%lu threshold=%lu",
                 is_detected ? "detected" : "released",
                 (unsigned long)response_slope,
                 (unsigned long)baseline,
                 (unsigned long)s_pidm_threshold_slope);
    }
}

/*
 * brief : Initialize PIDM control outputs.
 * input : none.
 * output: ESP_OK on success; otherwise GPIO or GPBA02B error code.
 * type  : public
 * theory: keep the pulse output low and the active-low enable output high so PIDM starts disabled.
 */
esp_err_t pidm_init_runtime(void) {
    gpio_config_t io_cfg = { 0 };
    esp_err_t ret = ESP_OK;

    if (!GPIO_IS_VALID_OUTPUT_GPIO(PIDM_PULSE_IO)) {
        return ESP_ERR_INVALID_ARG;
    }

    ret = hal_adc_init(PIDM_ADC_UNIT, PIDM_ADC_CHANNEL);
    if (ret != ESP_OK) {
        return ret;
    }

    io_cfg.intr_type = GPIO_INTR_DISABLE;
    io_cfg.mode = GPIO_MODE_OUTPUT;
    io_cfg.pin_bit_mask = (1ULL << (uint32_t)PIDM_PULSE_IO);
    io_cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_cfg.pull_up_en = GPIO_PULLUP_DISABLE;

    ret = gpio_config(&io_cfg);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = gpio_set_level(PIDM_PULSE_IO, 0U);
    if (ret != ESP_OK) {
        (void)gpio_reset_pin(PIDM_PULSE_IO);
        return ret;
    }

    ret = gpba02b_init_object();
    if ((ret != ESP_OK) && (ret != ESP_ERR_INVALID_STATE)) {
        (void)gpio_reset_pin(PIDM_PULSE_IO);
        return ret;
    }

    ret = gpba02b_set_io_mode(PIDM_ENA_PORT,
                              PIDM_ENA_PIN,
                              GPBA02B_IO_STYLE_INPUT_PULL_HIGH);
    if (ret != ESP_OK) {
        (void)gpio_reset_pin(PIDM_PULSE_IO);
        return ret;
    }

    ret =
        gpba02b_set_io_mode(PIDM_ENA_PORT, PIDM_ENA_PIN, GPBA02B_IO_STYLE_OUTPUT_CMOS);
    if (ret != ESP_OK) {
        (void)gpba02b_set_io_mode(PIDM_ENA_PORT,
                                  PIDM_ENA_PIN,
                                  GPBA02B_IO_STYLE_INPUT_HIGH_Z);
        (void)gpio_reset_pin(PIDM_PULSE_IO);
        return ret;
    }

    ret = gpba02b_write_io_level(PIDM_ENA_PORT, PIDM_ENA_PIN, 1U);
    if (ret != ESP_OK) {
        (void)gpba02b_set_io_mode(PIDM_ENA_PORT,
                                  PIDM_ENA_PIN,
                                  GPBA02B_IO_STYLE_INPUT_HIGH_Z);
        (void)gpio_reset_pin(PIDM_PULSE_IO);
        return ret;
    }

    portENTER_CRITICAL(&s_pidm_lock);
    s_pidm_ready = true;
    s_pidm_metal_detected = false;
    s_pidm_trigger_count = 0U;
    s_pidm_response_slope = 0U;
    s_pidm_threshold_slope = 0U;
    s_pidm_baseline_q8 = 0;
    s_pidm_noise_q8 = 0;
    s_pidm_calibration_count = 0U;
    s_pidm_detect_hits = 0U;
    s_pidm_release_hits = 0U;
    s_pidm_trigger_error = ESP_OK;
    portEXIT_CRITICAL(&s_pidm_lock);

    return ESP_OK;
}

/*
 * brief : Release PIDM control outputs.
 * input : none.
 * output: ESP_OK on success; otherwise the first GPIO or GPBA02B error code.
 * type  : public
 * theory: drive PIDM to its disabled state before returning both control pins to high impedance.
 */
esp_err_t pidm_deinit_runtime(void) {
    esp_err_t ret = ESP_OK;
    esp_err_t release_ret = ESP_OK;

    ret = gpio_set_level(PIDM_PULSE_IO, 0U);

    release_ret = gpba02b_write_io_level(PIDM_ENA_PORT, PIDM_ENA_PIN, 0U);
    if ((ret == ESP_OK) && (release_ret != ESP_OK)) {
        ret = release_ret;
    }

    release_ret =
        gpba02b_set_io_mode(PIDM_ENA_PORT, PIDM_ENA_PIN, GPBA02B_IO_STYLE_INPUT_HIGH_Z);
    if ((ret == ESP_OK) && (release_ret != ESP_OK)) {
        ret = release_ret;
    }

    release_ret = gpio_reset_pin(PIDM_PULSE_IO);
    if ((ret == ESP_OK) && (release_ret != ESP_OK)) {
        ret = release_ret;
    }

    portENTER_CRITICAL(&s_pidm_lock);
    s_pidm_ready = false;
    portEXIT_CRITICAL(&s_pidm_lock);

    return ret;
}

/*
 * brief : Generate one PIDM detection pulse.
 * input : none.
 * output: ESP_OK on success; otherwise GPIO error code.
 * type  : public
 * theory: hold the pulse output high for the configured blocking interval, then return it low.
 */
esp_err_t pidm_trigger_detection(void) {
    bool ready = false;
    uint32_t response_slope = 0U;
    esp_err_t ret = ESP_OK;

    portENTER_CRITICAL(&s_pidm_lock);
    ready = s_pidm_ready;
    portEXIT_CRITICAL(&s_pidm_lock);
    if (!ready) {
        return ESP_ERR_INVALID_STATE;
    }

    ret = gpio_set_level(PIDM_PULSE_IO, 1U);
    if (ret != ESP_OK) {
        portENTER_CRITICAL(&s_pidm_lock);
        s_pidm_trigger_error = ret;
        portEXIT_CRITICAL(&s_pidm_lock);
        return ret;
    }

    esp_rom_delay_us(PIDM_TRIGGER_PULSE_US);

    ret = gpio_set_level(PIDM_PULSE_IO, 0U);
    portENTER_CRITICAL(&s_pidm_lock);
    s_pidm_trigger_error = ret;
    if (ret == ESP_OK) {
        s_pidm_trigger_count++;
    }
    portEXIT_CRITICAL(&s_pidm_lock);

    if (ret != ESP_OK) {
        return ret;
    }

    ret = _pidm_capture_response_slope(&response_slope);
    if (ret == ESP_OK) {
        _pidm_update_detection(response_slope);
    }

    portENTER_CRITICAL(&s_pidm_lock);
    s_pidm_trigger_error = ret;
    portEXIT_CRITICAL(&s_pidm_lock);

    return ret;
}

/*
 * brief : Read the PIDM runtime snapshot.
 * input : profile - destination snapshot.
 * output: ESP_OK when ADC data is valid; otherwise argument, state, or ADC error code.
 * type  : public
 * theory: take one current ADC conversion and copy the scalar control state into the snapshot.
 */
esp_err_t pidm_read_profile(pidm_profile_s* profile) {
     uint16_t raw = 0U;
    esp_err_t ret = ESP_OK;

    if (profile == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    ret = hal_adc_read(PIDM_ADC_UNIT, PIDM_ADC_CHANNEL, &raw);

    profile->adc_valid = (ret == ESP_OK);
    profile->adc_latest = raw;
    profile->adc_average = raw;
    profile->adc_sample_count = (ret == ESP_OK) ? 1U : 0U;
    profile->pulse_width_us = PIDM_TRIGGER_PULSE_US;

    portENTER_CRITICAL(&s_pidm_lock);
    profile->ready = s_pidm_ready;
    profile->calibrated =
        (s_pidm_calibration_count >= PIDM_DETECT_CALIBRATION_COUNT);
    profile->metal_detected = s_pidm_metal_detected;
    profile->trigger_count = s_pidm_trigger_count;
    profile->response_slope = s_pidm_response_slope;
    profile->baseline_slope = (uint32_t)(s_pidm_baseline_q8 >> 8U);
    profile->threshold_slope = s_pidm_threshold_slope;
    profile->calibration_count = s_pidm_calibration_count;
    profile->detect_hits = s_pidm_detect_hits;
    profile->trigger_error = s_pidm_trigger_error;
    portEXIT_CRITICAL(&s_pidm_lock);

    return ret;
}
