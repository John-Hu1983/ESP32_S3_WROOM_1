#include "dev_pidm.h"

#define TAG "dev_pidm"

static bool pidm_ready = false;
static uint32_t pidm_trigger_count = 0U;
static pidm_profile_s s_pidm_profile = { 0 };
static int32_t pidm_ref_peak_q8 = 0;
static int32_t pidm_ref_slope_q8 = 0;
static uint16_t pidm_ref_count = 0U;
static uint8_t pidm_assert_streak = 0U;
static uint8_t pidm_release_streak = 0U;
static bool pidm_ref_ready = false;
static bool pidm_event_present = false;
static esp_err_t pidm_trigger_error = ESP_OK;
static SemaphoreHandle_t pidm_probe_lock = NULL;
static TaskHandle_t pidm_task_handle = NULL;
static volatile bool pidm_task_stop = false;
static portMUX_TYPE pidm_lock = portMUX_INITIALIZER_UNLOCKED;

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
 * brief : Learn the initial no-metal peak and slope reference.
 * input : peak_excess - peak above baseline; slope - maximum rising slope.
 * output: none.
 * type  : private
 * theory: use an incremental Q8 mean so startup reference learning needs no sample history.
 */
static void _pidm_reference_learn(uint32_t peak_excess, uint32_t slope) {
    int32_t peak_q8 = (int32_t)(peak_excess << 8U);
    int32_t slope_q8 = (int32_t)(slope << 8U);
    int32_t divisor = 0;

    if (pidm_ref_count == 0U) {
        pidm_ref_peak_q8 = peak_q8;
        pidm_ref_slope_q8 = slope_q8;
    }
    else {
        divisor = (int32_t)pidm_ref_count + 1;
        pidm_ref_peak_q8 += (peak_q8 - pidm_ref_peak_q8) / divisor;
        pidm_ref_slope_q8 += (slope_q8 - pidm_ref_slope_q8) / divisor;
    }

    if (pidm_ref_count < UINT16_MAX) {
        pidm_ref_count++;
    }
    if (pidm_ref_count >= PIDM_REFERENCE_LEARN_PULSES) {
        pidm_ref_ready = true;
    }
}

/*
 * brief : Track slow no-metal drift after reference learning.
 * input : peak_excess - peak above baseline; slope - maximum rising slope.
 * output: none.
 * type  : private
 * theory: update only non-hit frames with a slow Q8 exponential moving average.
 */
static void _pidm_reference_update(uint32_t peak_excess, uint32_t slope) {
    int32_t peak_q8 = (int32_t)(peak_excess << 8U);
    int32_t slope_q8 = (int32_t)(slope << 8U);

    pidm_ref_peak_q8 += (peak_q8 - pidm_ref_peak_q8) >> PIDM_REFERENCE_EMA_SHIFT;
    pidm_ref_slope_q8 += (slope_q8 - pidm_ref_slope_q8) >> PIDM_REFERENCE_EMA_SHIFT;
}

/*
 * brief : Extract one PIDM pulse feature set from baseline and response samples.
 * input : baseline/wave - samples captured immediately before and after the pulse.
 * output: feature - extracted baseline, peak, slope, hold, and area values.
 * type  : private
 * theory: derive a CFAR threshold from current baseline noise, then measure response shape above it.
 */
static void _pidm_extract_feature(const uint16_t* baseline,
                                  const uint16_t* wave,
                                  pidm_profile_s* feature) {
    int32_t baseline_sum = 0;
    int32_t noise_sum = 0;
    int32_t wave_sum = 0;
    int32_t baseline_raw = 0;
    int32_t threshold_raw = 0;
    int32_t previous_raw = 0;
    int32_t peak_raw = 0;
    int32_t raw = 0;
    int32_t delta = 0;
    uint64_t area = 0U;
    uint32_t threshold_rise = 0U;
    uint32_t current_hold_us = 0U;
    uint32_t slope = 0U;
    uint32_t peak_index = 0U;
    uint32_t i = 0U;

    for (i = 0U; i < PIDM_BASELINE_SAMPLE_COUNT; ++i) {
        baseline_sum += baseline[i];
    }
    baseline_raw = baseline_sum / (int32_t)PIDM_BASELINE_SAMPLE_COUNT;

    for (i = 0U; i < PIDM_BASELINE_SAMPLE_COUNT; ++i) {
        noise_sum += _pidm_abs_i32((int32_t)baseline[i] - baseline_raw);
    }
    feature->baseline_noise =
        (uint32_t)(noise_sum / (int32_t)PIDM_BASELINE_SAMPLE_COUNT);
    threshold_rise =
        ((feature->baseline_noise * PIDM_THRESHOLD_NOISE_GAIN_Q4) + 8U) / 16U;
    if (threshold_rise < PIDM_THRESHOLD_MIN_RISE) {
        threshold_rise = PIDM_THRESHOLD_MIN_RISE;
    }

    threshold_raw = baseline_raw + (int32_t)threshold_rise;
    previous_raw = wave[0];
    peak_raw = baseline_raw;
    for (i = 0U; i < PIDM_WAVE_SAMPLE_COUNT; ++i) {
        raw = wave[i];
        wave_sum += raw;
        if (raw > peak_raw) {
            peak_raw = raw;
            peak_index = i;
        }

        delta = raw - previous_raw;
        if (delta > 0) {
            slope = ((uint32_t)delta * 1000U) / PIDM_WAVE_INTERVAL_US;

            if (slope > feature->response_slope) {
                feature->response_slope = slope;
            }
        }

        if (raw > threshold_raw) {
            area += (uint64_t)(raw - threshold_raw) * PIDM_WAVE_INTERVAL_US;
            if (area > UINT32_MAX) {
                area = UINT32_MAX;
            }
            current_hold_us += PIDM_WAVE_INTERVAL_US;
            if (current_hold_us > feature->high_hold_us) {
                feature->high_hold_us = current_hold_us;
            }
        }
        else {
            current_hold_us = 0U;
        }
        previous_raw = raw;
    }

    feature->adc_latest = wave[PIDM_WAVE_SAMPLE_COUNT - 1U];
    feature->adc_average = (uint32_t)(wave_sum / (int32_t)PIDM_WAVE_SAMPLE_COUNT);
    feature->adc_sample_count = PIDM_WAVE_SAMPLE_COUNT;
    feature->baseline_raw = (uint32_t)baseline_raw;
    feature->threshold_raw = (uint32_t)threshold_raw;
    feature->peak_raw = (uint32_t)peak_raw;
    feature->peak_time_us =
        PIDM_RESPONSE_SETTLE_US + (peak_index * PIDM_WAVE_INTERVAL_US);
    feature->area_adc_us = (uint32_t)area;
}

/*
 * brief : Update adaptive PIDM reference and debounced metal state.
 * input : feature - latest extracted pulse features.
 * output: none.
 * type  : private
 * theory: compare peak and slope against learned no-metal references, then debounce hits and misses.
 */
static void _pidm_update_detection(pidm_profile_s* feature) {
    bool was_detected = pidm_event_present;
    bool calibration_completed = false;
    uint32_t peak_excess = 0U;
    uint32_t peak_reference = 0U;
    uint32_t slope_reference = 0U;

    if (feature->peak_raw > feature->baseline_raw) {
        peak_excess = feature->peak_raw - feature->baseline_raw;
    }

    if (!pidm_ref_ready) {
        _pidm_reference_learn(peak_excess, feature->response_slope);
        calibration_completed = pidm_ref_ready;
        feature->peak_delta_raw = 0U;
        feature->slope_delta = 0U;
        feature->pulse_hit = false;
    }
    else {
        peak_reference = (uint32_t)(pidm_ref_peak_q8 >> 8U);
        slope_reference = (uint32_t)(pidm_ref_slope_q8 >> 8U);
        if (peak_excess > peak_reference) {
            feature->peak_delta_raw = peak_excess - peak_reference;
        }
        if (feature->response_slope > slope_reference) {
            feature->slope_delta = feature->response_slope - slope_reference;
        }
        feature->peak_hit = feature->peak_delta_raw >= PIDM_PEAK_DELTA_MIN;
        feature->slope_hit = feature->slope_delta >= PIDM_SLOPE_DELTA_MIN_ADC_PER_MS;
        feature->pulse_hit = feature->peak_hit && feature->slope_hit;

        if (!pidm_event_present && !feature->pulse_hit) {
            _pidm_reference_update(peak_excess, feature->response_slope);
        }
    }

    feature->hold_hit = feature->high_hold_us >= PIDM_HIGH_HOLD_MIN_US;
    feature->area_hit = feature->area_adc_us >= PIDM_AREA_MIN_ADC_US;

    if (feature->pulse_hit) {
        if (pidm_assert_streak < UINT8_MAX) {
            pidm_assert_streak++;
        }
        pidm_release_streak = 0U;
        if (pidm_assert_streak >= PIDM_DETECT_ASSERT_COUNT) {
            pidm_event_present = true;
        }
    }
    else {
        if (pidm_release_streak < UINT8_MAX) {
            pidm_release_streak++;
        }
        pidm_assert_streak = 0U;
        if (pidm_release_streak >= PIDM_DETECT_RELEASE_COUNT) {
            pidm_event_present = false;
        }
    }

    peak_reference = (uint32_t)(pidm_ref_peak_q8 >> 8U);
    slope_reference = (uint32_t)(pidm_ref_slope_q8 >> 8U);
    feature->calibrated = pidm_ref_ready;
    feature->metal_detected = pidm_event_present;
    feature->peak_excess_raw = peak_excess;
    feature->peak_reference_raw = peak_reference;
    feature->baseline_slope = slope_reference;
    feature->threshold_slope = slope_reference + PIDM_SLOPE_DELTA_MIN_ADC_PER_MS;
    feature->calibration_count = pidm_ref_count;
    feature->detect_hits = pidm_assert_streak;
    feature->release_hits = pidm_release_streak;

    if (calibration_completed) {
        ESP_LOGI(TAG,
                 "reference ready: peak=%lu slope=%lu",
                 (unsigned long)peak_reference,
                 (unsigned long)slope_reference);
    }
    if (was_detected != pidm_event_present) {
        ESP_LOGI(TAG,
                 "metal %s: dpk=%lu dsl=%lu hold=%lu area=%lu",
                 pidm_event_present ? "detected" : "released",
                 (unsigned long)feature->peak_delta_raw,
                 (unsigned long)feature->slope_delta,
                 (unsigned long)feature->high_hold_us,
                 (unsigned long)feature->area_adc_us);
    }
}

/*
 * brief : Capture and process one complete PIDM detection pulse.
 * input : feature - destination feature snapshot.
 * output: ESP_OK on success; otherwise ADC or GPIO error.
 * type  : private
 * theory: sample the live baseline, pulse the frontend, capture its response waveform, and classify it.
 */
static esp_err_t _pidm_probe(pidm_profile_s* feature) {
    uint16_t baseline[PIDM_BASELINE_SAMPLE_COUNT] = { 0 };
    uint16_t wave[PIDM_WAVE_SAMPLE_COUNT] = { 0 };
    uint32_t i = 0U;
    esp_err_t ret = ESP_OK;

    if (feature == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    // Capture the baseline samples before triggering the detection pulse.
    for (i = 0U; i < PIDM_BASELINE_SAMPLE_COUNT; ++i) {
        ret = hal_adc_read(PIDM_ADC_UNIT, PIDM_ADC_CHANNEL, &baseline[i]);
        if (ret != ESP_OK) {
            return ret;
        }
        if ((i + 1U) < PIDM_BASELINE_SAMPLE_COUNT) {
            esp_rom_delay_us(PIDM_BASELINE_INTERVAL_US);
        }
    }

    // Trigger the detection pulse and capture the response waveform.
    ret = gpio_set_level(PIDM_PULSE_IO, 1U);
    if (ret != ESP_OK) {
        return ret;
    }
    esp_rom_delay_us(PIDM_TRIGGER_PULSE_US);
    ret = gpio_set_level(PIDM_PULSE_IO, 0U);
    if (ret != ESP_OK) {
        return ret;
    }
    esp_rom_delay_us(PIDM_RESPONSE_SETTLE_US);

    for (i = 0U; i < PIDM_WAVE_SAMPLE_COUNT; ++i) {
        ret = hal_adc_read(PIDM_ADC_UNIT, PIDM_ADC_CHANNEL, &wave[i]);
        if (ret != ESP_OK) {
            return ret;
        }
        if ((i + 1U) < PIDM_WAVE_SAMPLE_COUNT) {
            esp_rom_delay_us(PIDM_WAVE_INTERVAL_US);
        }
    }

    // Extract features from the captured baseline and waveform, then update the detection status.
    _pidm_extract_feature(baseline, wave, feature);
    _pidm_update_detection(feature);
    return ESP_OK;
}

/*
 * brief : Run periodic PIDM detection independently from the UI.
 * input : arg - unused task argument.
 * output: none.
 * type  : private
 * theory: own detector cadence in the device layer and wake promptly when deinitialization requests stop.
 */
static void _pidm_detection_task(void* arg) {
    esp_err_t ret = ESP_OK;

    (void)arg;

    while (!pidm_task_stop) {
        (void)ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(PIDM_DETECTION_PERIOD_MS));
        if (pidm_task_stop) {
            break;
        }

        ret = pidm_trigger_detection();
        if ((ret != ESP_OK) && (ret != ESP_ERR_INVALID_STATE)) {
            ESP_LOGW(TAG, "detection failed: 0x%x", (unsigned)ret);
        }
    }

    pidm_task_handle = NULL;
    vTaskDelete(NULL);
}

/*
 * brief : Stop the device-owned PIDM detection task.
 * input : none.
 * output: none.
 * type  : private
 * theory: notify the task out of its period wait, then wait until any active probe finishes cleanly.
 */
static void _pidm_stop_detection_task(void) {
    TaskHandle_t task_handle = pidm_task_handle;

    if (task_handle == NULL) {
        return;
    }

    pidm_task_stop = true;
    xTaskNotifyGive(task_handle);
    while (pidm_task_handle != NULL) {
        vTaskDelay(1U);
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
    if (pidm_ready) {
        return ESP_OK;
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

    if (pidm_probe_lock == NULL) {
        pidm_probe_lock = xSemaphoreCreateMutex();
        if (pidm_probe_lock == NULL) {
            (void)pidm_deinit_runtime();
            return ESP_ERR_NO_MEM;
        }
    }

    portENTER_CRITICAL(&pidm_lock);
    pidm_ready = true;
    pidm_trigger_count = 0U;
    s_pidm_profile = (pidm_profile_s){
        .ready = true,
        .pulse_width_us = PIDM_TRIGGER_PULSE_US,
    };
    pidm_ref_peak_q8 = 0;
    pidm_ref_slope_q8 = 0;
    pidm_ref_count = 0U;
    pidm_assert_streak = 0U;
    pidm_release_streak = 0U;
    pidm_ref_ready = false;
    pidm_event_present = false;
    pidm_trigger_error = ESP_OK;
    portEXIT_CRITICAL(&pidm_lock);

    pidm_task_stop = false;
    if (xTaskCreate(_pidm_detection_task,
                    "pidm_detection",
                    PIDM_DETECTION_TASK_STACK_SIZE,
                    NULL,
                    PIDM_DETECTION_TASK_PRIORITY,
                    &pidm_task_handle)
        != pdPASS) {
        pidm_task_handle = NULL;
        (void)pidm_deinit_runtime();
        return ESP_ERR_NO_MEM;
    }

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

    _pidm_stop_detection_task();

    ret = gpio_set_level(PIDM_PULSE_IO, 0U);
    release_ret = gpba02b_write_io_level(PIDM_ENA_PORT, PIDM_ENA_PIN, 0U);

    if ((ret == ESP_OK) && (release_ret != ESP_OK)) {
        ret = release_ret;
    }

    portENTER_CRITICAL(&pidm_lock);
    pidm_ready = false;
    s_pidm_profile.ready = false;
    portEXIT_CRITICAL(&pidm_lock);

    if (pidm_probe_lock != NULL) {
        vSemaphoreDelete(pidm_probe_lock);
        pidm_probe_lock = NULL;
    }

    return ret;
}

/*
 * brief : Generate one PIDM detection pulse.
 * input : none.
 * output: ESP_OK on success; otherwise ADC or GPIO error code.
 * type  : public
 * theory: capture baseline and pulse response in the device layer, then publish one classified snapshot.
 */
esp_err_t pidm_trigger_detection(void) {
    pidm_profile_s feature = { 0 };
    bool ready = false;
    esp_err_t ret = ESP_OK;

    portENTER_CRITICAL(&pidm_lock);
    ready = pidm_ready;
    portEXIT_CRITICAL(&pidm_lock);
    if (!ready) {
        return ESP_ERR_INVALID_STATE;
    }
    if ((pidm_probe_lock == NULL)
        || (xSemaphoreTake(pidm_probe_lock, portMAX_DELAY) != pdTRUE)) {
        return ESP_ERR_INVALID_STATE;
    }

    ret = _pidm_probe(&feature);

    portENTER_CRITICAL(&pidm_lock);
    pidm_trigger_error = ret;
    if (ret == ESP_OK) {
        pidm_trigger_count++;
        feature.ready = true;
        feature.adc_valid = true;
        feature.trigger_count = pidm_trigger_count;
        feature.pulse_width_us = PIDM_TRIGGER_PULSE_US;
        feature.trigger_error = ESP_OK;
        s_pidm_profile = feature;
    }
    else {
        s_pidm_profile.trigger_error = ret;
    }
    portEXIT_CRITICAL(&pidm_lock);
    xSemaphoreGive(pidm_probe_lock);

    return ret;
}

/*
 * brief : Read the PIDM runtime snapshot.
 * input : profile - destination snapshot.
 * output: ESP_OK when ADC data is valid; otherwise argument, state, or ADC error code.
 * type  : public
 * theory: copy the last complete device-owned detection result without touching ADC hardware.
 */
esp_err_t pidm_read_profile(pidm_profile_s* profile) {
    esp_err_t ret = ESP_OK;

    if (profile == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    portENTER_CRITICAL(&pidm_lock);
    *profile = s_pidm_profile;
    profile->ready = pidm_ready;
    profile->trigger_error = pidm_trigger_error;
    portEXIT_CRITICAL(&pidm_lock);

    if (!profile->ready) {
        ret = ESP_ERR_INVALID_STATE;
    }
    else if (profile->trigger_error != ESP_OK) {
        ret = profile->trigger_error;
    }
    else if (!profile->adc_valid) {
        ret = ESP_ERR_NOT_FOUND;
    }

    return ret;
}
