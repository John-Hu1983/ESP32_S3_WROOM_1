#include "pidm_det.h"

#define TAG "PIDM_DET"

#if defined(PIDM_IO_PULSE) && defined(PIDM_IO_ADC) && defined(PIDM_ADC_CHANNEL) && \
    defined(PIDM_EN_PORT) && defined(PIDM_EN_PIN)
#define PIDM_DET_SUPPORTED 1
#else
#define PIDM_DET_SUPPORTED 0
#endif

typedef struct {
    bool ref_ready;
    uint8_t assert_streak;
    uint8_t release_streak;
    uint8_t learn_count;
    bool metal_present;
    uint32_t peak_ref_q8;
    uint32_t slope_ref_q8;
} pidm_det_runtime_s;

static bool s_pidm_ready = false;
static bool s_pidm_enabled = false;
static adc_oneshot_unit_handle_t s_pidm_adc_handle = NULL;
static adc_unit_t s_pidm_adc_unit = ADC_UNIT_1;
static adc_channel_t s_pidm_adc_channel = ADC_CHANNEL_0;
static pidm_det_runtime_s s_pidm_runtime = {0};
static const pidm_det_feature_cfg_s s_pidm_default_feature_cfg = {
    .baseline_samples = PIDM_DET_BASELINE_SAMPLES_DEFAULT,
    .baseline_interval_us = PIDM_DET_BASELINE_INTERVAL_US_DEFAULT,
    .wave_samples = PIDM_DET_WAVE_SAMPLES_DEFAULT,
    .wave_interval_us = PIDM_DET_WAVE_INTERVAL_US_DEFAULT,
    .settle_us = PIDM_DET_SETTLE_US_DEFAULT,
    .threshold_min_rise = PIDM_DET_THRESHOLD_MIN_RISE_DEFAULT,
    .threshold_noise_gain_q4 = PIDM_DET_THRESHOLD_NOISE_GAIN_Q4_DEFAULT,
    .slope_min_adc_per_ms = PIDM_DET_SLOPE_MIN_ADC_PER_MS_DEFAULT,
    .high_hold_min_us = PIDM_DET_HIGH_HOLD_MIN_US_DEFAULT,
    .area_min_adc_us = PIDM_DET_AREA_MIN_ADC_US_DEFAULT,
    .peak_delta_min = PIDM_DET_PEAK_DELTA_MIN_DEFAULT,
    .slope_delta_min_adc_per_ms = PIDM_DET_SLOPE_DELTA_MIN_ADC_PER_MS_DEFAULT,
    .ref_learn_pulses = PIDM_DET_REF_LEARN_PULSES_DEFAULT,
    .ref_ema_shift = PIDM_DET_REF_EMA_SHIFT_DEFAULT,
    .assert_count = PIDM_DET_ASSERT_COUNT_DEFAULT,
    .release_count = PIDM_DET_RELEASE_COUNT_DEFAULT,
};

static pidm_det_feature_cfg_s s_pidm_feature_cfg = {
    .baseline_samples = PIDM_DET_BASELINE_SAMPLES_DEFAULT,
    .baseline_interval_us = PIDM_DET_BASELINE_INTERVAL_US_DEFAULT,
    .wave_samples = PIDM_DET_WAVE_SAMPLES_DEFAULT,
    .wave_interval_us = PIDM_DET_WAVE_INTERVAL_US_DEFAULT,
    .settle_us = PIDM_DET_SETTLE_US_DEFAULT,
    .threshold_min_rise = PIDM_DET_THRESHOLD_MIN_RISE_DEFAULT,
    .threshold_noise_gain_q4 = PIDM_DET_THRESHOLD_NOISE_GAIN_Q4_DEFAULT,
    .slope_min_adc_per_ms = PIDM_DET_SLOPE_MIN_ADC_PER_MS_DEFAULT,
    .high_hold_min_us = PIDM_DET_HIGH_HOLD_MIN_US_DEFAULT,
    .area_min_adc_us = PIDM_DET_AREA_MIN_ADC_US_DEFAULT,
    .peak_delta_min = PIDM_DET_PEAK_DELTA_MIN_DEFAULT,
    .slope_delta_min_adc_per_ms = PIDM_DET_SLOPE_DELTA_MIN_ADC_PER_MS_DEFAULT,
    .ref_learn_pulses = PIDM_DET_REF_LEARN_PULSES_DEFAULT,
    .ref_ema_shift = PIDM_DET_REF_EMA_SHIFT_DEFAULT,
    .assert_count = PIDM_DET_ASSERT_COUNT_DEFAULT,
    .release_count = PIDM_DET_RELEASE_COUNT_DEFAULT,
};

static int pidm_abs_int(int value) { return value < 0 ? -value : value; }

static void pidm_reset_runtime(void) { memset(&s_pidm_runtime, 0, sizeof(s_pidm_runtime)); }

static bool pidm_cfg_is_valid(const pidm_det_feature_cfg_s* cfg) {
    if (cfg == NULL) {
        return false;
    }

    if (cfg->baseline_samples == 0 || cfg->baseline_samples > PIDM_DET_BASELINE_SAMPLE_MAX) {
        return false;
    }

    if (cfg->wave_samples == 0 || cfg->wave_samples > PIDM_DET_WAVE_SAMPLE_MAX) {
        return false;
    }

    if (cfg->baseline_interval_us == 0 || cfg->wave_interval_us == 0) {
        return false;
    }

    if (cfg->threshold_noise_gain_q4 == 0 || cfg->ref_ema_shift > 7) {
        return false;
    }

    if (cfg->assert_count == 0 || cfg->release_count == 0 || cfg->ref_learn_pulses == 0) {
        return false;
    }

    return true;
}

static void pidm_update_reference(const pidm_det_feature_cfg_s* cfg, uint32_t peak_excess,
                                  uint32_t slope) {
    uint32_t peak_q8;
    uint32_t slope_q8;
    int32_t peak_delta_q8;
    int32_t slope_delta_q8;

    if (cfg == NULL) {
        return;
    }

    peak_q8 = peak_excess << 8;
    slope_q8 = slope << 8;

    if (!s_pidm_runtime.ref_ready) {
        if (s_pidm_runtime.learn_count == 0) {
            s_pidm_runtime.peak_ref_q8 = peak_q8;
            s_pidm_runtime.slope_ref_q8 = slope_q8;
        } else {
            s_pidm_runtime.peak_ref_q8 = (s_pidm_runtime.peak_ref_q8 + peak_q8) / 2U;
            s_pidm_runtime.slope_ref_q8 = (s_pidm_runtime.slope_ref_q8 + slope_q8) / 2U;
        }

        s_pidm_runtime.learn_count++;
        if (s_pidm_runtime.learn_count >= cfg->ref_learn_pulses) {
            s_pidm_runtime.ref_ready = true;
        }
        return;
    }

    peak_delta_q8 = (int32_t)peak_q8 - (int32_t)s_pidm_runtime.peak_ref_q8;
    slope_delta_q8 = (int32_t)slope_q8 - (int32_t)s_pidm_runtime.slope_ref_q8;

    {
        int32_t peak_ref_q8 = (int32_t)s_pidm_runtime.peak_ref_q8;
        int32_t slope_ref_q8 = (int32_t)s_pidm_runtime.slope_ref_q8;

        peak_ref_q8 += (peak_delta_q8 >> cfg->ref_ema_shift);
        slope_ref_q8 += (slope_delta_q8 >> cfg->ref_ema_shift);

        if (peak_ref_q8 < 0) {
            peak_ref_q8 = 0;
        }

        if (slope_ref_q8 < 0) {
            slope_ref_q8 = 0;
        }

        s_pidm_runtime.peak_ref_q8 = (uint32_t)peak_ref_q8;
        s_pidm_runtime.slope_ref_q8 = (uint32_t)slope_ref_q8;
    }
}

#if PIDM_DET_SUPPORTED
static esp_err_t pidm_config_pulse_gpio(void) {
    gpio_config_t io_cfg = {0};

    if ((int)PIDM_IO_PULSE < 0) {
        return ESP_ERR_INVALID_ARG;
    }

    io_cfg.pin_bit_mask = (1ULL << (uint32_t)PIDM_IO_PULSE);
    io_cfg.mode = GPIO_MODE_OUTPUT;
    io_cfg.pull_up_en = GPIO_PULLUP_DISABLE;
    io_cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_cfg.intr_type = GPIO_INTR_DISABLE;

    if (gpio_config(&io_cfg) != ESP_OK) {
        return ESP_FAIL;
    }

    return gpio_set_level(PIDM_IO_PULSE, 0);
}

static esp_err_t pidm_resolve_adc_mapping(adc_unit_t* unit, adc_channel_t* channel) {
    esp_err_t ret;

    if (unit == NULL || channel == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if ((int)PIDM_IO_ADC < 0) {
        return ESP_ERR_INVALID_ARG;
    }

    ret = adc_oneshot_io_to_channel((int)PIDM_IO_ADC, unit, channel);
    if (ret != ESP_OK) {
        return ret;
    }

    if (*channel != PIDM_ADC_CHANNEL) {
        ESP_LOGW(TAG, "PIDM_ADC_CHANNEL mismatch cfg=%d io=%d", (int)PIDM_ADC_CHANNEL,
                 (int)(*channel));
    }

    return ESP_OK;
}
#endif

esp_err_t pidm_det_init(void) {
#if !PIDM_DET_SUPPORTED
    ESP_LOGW(TAG, "PIDM is not supported on this board config");
    return ESP_ERR_NOT_SUPPORTED;
#else
    adc_oneshot_unit_init_cfg_t unit_cfg = {0};
    adc_oneshot_chan_cfg_t chan_cfg = {0};
    adc_unit_t adc_unit;
    adc_channel_t adc_channel;
    esp_err_t ret;

    if (s_pidm_ready) {
        return ESP_OK;
    }

    ret =
        gpba02b_config_io_output_mode(PIDM_EN_PORT, PIDM_EN_PIN, GPBA02B_IO_OUTPUT_PUSH_PULL, true);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = gpba02b_write_io(PIDM_EN_PORT, PIDM_EN_PIN, true);
    if (ret != ESP_OK) {
        return ret;
    }
    s_pidm_enabled = true;

    ret = pidm_config_pulse_gpio();
    if (ret != ESP_OK) {
        return ret;
    }

    ret = pidm_resolve_adc_mapping(&adc_unit, &adc_channel);
    if (ret != ESP_OK) {
        return ret;
    }

    unit_cfg.unit_id = adc_unit;
    unit_cfg.clk_src = 0;
    unit_cfg.ulp_mode = ADC_ULP_MODE_DISABLE;

    ret = adc_oneshot_new_unit(&unit_cfg, &s_pidm_adc_handle);
    if (ret != ESP_OK) {
        return ret;
    }

    chan_cfg.atten = PIDM_DET_ADC_ATTEN;
    chan_cfg.bitwidth = PIDM_DET_ADC_BITWIDTH;
    ret = adc_oneshot_config_channel(s_pidm_adc_handle, adc_channel, &chan_cfg);
    if (ret != ESP_OK) {
        (void)adc_oneshot_del_unit(s_pidm_adc_handle);
        s_pidm_adc_handle = NULL;
        return ret;
    }

    s_pidm_adc_unit = adc_unit;
    s_pidm_adc_channel = adc_channel;
    s_pidm_ready = true;
    pidm_reset_runtime();

    ESP_LOGI(TAG, "PIDM ready pulse=%d adc_io=%d unit=%d ch=%d", (int)PIDM_IO_PULSE,
             (int)PIDM_IO_ADC, (int)s_pidm_adc_unit, (int)s_pidm_adc_channel);
    return ESP_OK;
#endif
}

esp_err_t pidm_det_deinit(void) {
#if !PIDM_DET_SUPPORTED
    return ESP_ERR_NOT_SUPPORTED;
#else
    esp_err_t ret;

    if (!s_pidm_ready) {
        return ESP_OK;
    }

    (void)gpio_set_level(PIDM_IO_PULSE, 0);

    if (s_pidm_adc_handle != NULL) {
        ret = adc_oneshot_del_unit(s_pidm_adc_handle);
        if (ret != ESP_OK) {
            return ret;
        }
    }

    s_pidm_adc_handle = NULL;
    s_pidm_ready = false;
    s_pidm_enabled = false;
    pidm_reset_runtime();
    return ESP_OK;
#endif
}

bool pidm_det_is_ready(void) { return s_pidm_ready; }

esp_err_t pidm_det_set_enable(bool enable) {
#if !PIDM_DET_SUPPORTED
    (void)enable;
    return ESP_ERR_NOT_SUPPORTED;
#else
    esp_err_t ret;

    if (!s_pidm_ready) {
        return ESP_ERR_INVALID_STATE;
    }

    ret = gpba02b_write_io(PIDM_EN_PORT, PIDM_EN_PIN, enable);
    if (ret != ESP_OK) {
        return ret;
    }

    s_pidm_enabled = enable;
    return ESP_OK;
#endif
}

esp_err_t pidm_det_set_pulse_level(bool high) {
#if !PIDM_DET_SUPPORTED
    (void)high;
    return ESP_ERR_NOT_SUPPORTED;
#else
    if (!s_pidm_ready) {
        return ESP_ERR_INVALID_STATE;
    }

    return gpio_set_level(PIDM_IO_PULSE, high ? 1 : 0);
#endif
}

esp_err_t pidm_det_pulse_us(uint32_t pulse_us) {
#if !PIDM_DET_SUPPORTED
    (void)pulse_us;
    return ESP_ERR_NOT_SUPPORTED;
#else
    if (pulse_us == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_pidm_ready || !s_pidm_enabled) {
        return ESP_ERR_INVALID_STATE;
    }

    if (pidm_det_set_pulse_level(true) != ESP_OK) {
        return ESP_FAIL;
    }

    esp_rom_delay_us(pulse_us);

    if (pidm_det_set_pulse_level(false) != ESP_OK) {
        return ESP_FAIL;
    }

    return ESP_OK;
#endif
}

esp_err_t pidm_det_read_raw(int* raw_value) {
#if !PIDM_DET_SUPPORTED
    (void)raw_value;
    return ESP_ERR_NOT_SUPPORTED;
#else
    if (raw_value == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_pidm_ready || s_pidm_adc_handle == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    return adc_oneshot_read(s_pidm_adc_handle, s_pidm_adc_channel, raw_value);
#endif
}

void pidm_det_feature_cfg_load_default(pidm_det_feature_cfg_s* cfg) {
    if (cfg == NULL) {
        return;
    }

    *cfg = s_pidm_default_feature_cfg;
}

esp_err_t pidm_det_feature_cfg_set(const pidm_det_feature_cfg_s* cfg) {
    if (!pidm_cfg_is_valid(cfg)) {
        return ESP_ERR_INVALID_ARG;
    }

    s_pidm_feature_cfg = *cfg;
    pidm_reset_runtime();
    return ESP_OK;
}

pidm_det_feature_cfg_s pidm_det_feature_cfg_get(void) { return s_pidm_feature_cfg; }

esp_err_t pidm_det_probe_feature(uint32_t pulse_us, pidm_det_feature_s* feature) {
#if !PIDM_DET_SUPPORTED
    (void)pulse_us;
    (void)feature;
    return ESP_ERR_NOT_SUPPORTED;
#else
    pidm_det_feature_cfg_s cfg;
    int baseline_samples[PIDM_DET_BASELINE_SAMPLE_MAX];
    int i;
    int raw;
    int64_t baseline_sum;
    int64_t wave_sum;
    int baseline_raw;
    int baseline_noise;
    int threshold_offset;
    int threshold_raw;
    int peak_raw;
    int peak_index;
    int first_cross_index;
    uint32_t high_hold_us;
    uint32_t current_hold_us;
    uint32_t area_adc_us;
    uint32_t peak_time_us;
    uint32_t rise_slope_adc_per_ms;
    uint32_t peak_excess_raw;
    uint32_t slope_ref_adc_per_ms;
    int peak_ref_raw;
    int peak_delta_raw;
    uint32_t slope_delta_adc_per_ms;
    int wave_avg_raw;

    if (feature == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_pidm_ready || !s_pidm_enabled) {
        return ESP_ERR_INVALID_STATE;
    }

    cfg = s_pidm_feature_cfg;
    if (pulse_us == 0) {
        pulse_us = PIDM_DET_PULSE_US_DEFAULT;
    }

    if (!pidm_cfg_is_valid(&cfg)) {
        return ESP_ERR_INVALID_STATE;
    }

    memset(feature, 0, sizeof(*feature));

    baseline_sum = 0;
    for (i = 0; i < cfg.baseline_samples; ++i) {
        if (pidm_det_read_raw(&raw) != ESP_OK) {
            return ESP_FAIL;
        }
        baseline_samples[i] = raw;
        baseline_sum += raw;
        esp_rom_delay_us(cfg.baseline_interval_us);
    }

    baseline_raw = (int)(baseline_sum / cfg.baseline_samples);

    baseline_noise = 0;
    for (i = 0; i < cfg.baseline_samples; ++i) {
        baseline_noise += pidm_abs_int(baseline_samples[i] - baseline_raw);
    }
    baseline_noise /= cfg.baseline_samples;

    threshold_offset = (int)cfg.threshold_min_rise;
    if ((baseline_noise * (int)cfg.threshold_noise_gain_q4) / 16 > threshold_offset) {
        threshold_offset = (baseline_noise * (int)cfg.threshold_noise_gain_q4) / 16;
    }
    threshold_raw = baseline_raw + threshold_offset;

    if (pidm_det_pulse_us(pulse_us) != ESP_OK) {
        return ESP_FAIL;
    }

    if (cfg.settle_us > 0) {
        esp_rom_delay_us(cfg.settle_us);
    }

    wave_sum = 0;
    peak_raw = INT_MIN;
    peak_index = 0;
    first_cross_index = -1;
    high_hold_us = 0;
    current_hold_us = 0;
    area_adc_us = 0;

    for (i = 0; i < cfg.wave_samples; ++i) {
        int excess;

        if (pidm_det_read_raw(&raw) != ESP_OK) {
            return ESP_FAIL;
        }

        wave_sum += raw;
        if (raw > peak_raw) {
            peak_raw = raw;
            peak_index = i;
        }

        excess = raw - threshold_raw;
        if (excess > 0) {
            if (first_cross_index < 0) {
                first_cross_index = i;
            }
            current_hold_us += cfg.wave_interval_us;
            if (current_hold_us > high_hold_us) {
                high_hold_us = current_hold_us;
            }
            area_adc_us += (uint32_t)excess * cfg.wave_interval_us;
        } else {
            current_hold_us = 0;
        }

        esp_rom_delay_us(cfg.wave_interval_us);
    }

    wave_avg_raw = (int)(wave_sum / cfg.wave_samples);
    if (peak_raw <= threshold_raw) {
        peak_excess_raw = 0;
    } else {
        peak_excess_raw = (uint32_t)(peak_raw - threshold_raw);
    }

    if (first_cross_index >= 0) {
        peak_time_us = (uint32_t)first_cross_index * cfg.wave_interval_us;
    } else {
        peak_time_us = (uint32_t)peak_index * cfg.wave_interval_us;
    }
    if (peak_time_us == 0) {
        peak_time_us = cfg.wave_interval_us;
    }

    if (peak_raw > baseline_raw) {
        rise_slope_adc_per_ms = ((uint32_t)(peak_raw - baseline_raw) * 1000U) / peak_time_us;
    } else {
        rise_slope_adc_per_ms = 0;
    }

    pidm_update_reference(&cfg, peak_excess_raw, rise_slope_adc_per_ms);
    peak_ref_raw = (int)(s_pidm_runtime.peak_ref_q8 >> 8);
    slope_ref_adc_per_ms = (uint32_t)(s_pidm_runtime.slope_ref_q8 >> 8);

    peak_delta_raw = (int)peak_excess_raw - peak_ref_raw;
    if (rise_slope_adc_per_ms >= slope_ref_adc_per_ms) {
        slope_delta_adc_per_ms = rise_slope_adc_per_ms - slope_ref_adc_per_ms;
    } else {
        slope_delta_adc_per_ms = 0;
    }

    feature->baseline_raw = baseline_raw;
    feature->baseline_noise = baseline_noise;
    feature->threshold_raw = threshold_raw;
    feature->wave_avg_raw = wave_avg_raw;
    feature->peak_raw = peak_raw;
    feature->peak_excess_raw = (int)peak_excess_raw;
    feature->peak_ref_raw = peak_ref_raw;
    feature->peak_delta_raw = peak_delta_raw;
    feature->peak_time_us = peak_time_us;
    feature->rise_slope_adc_per_ms = rise_slope_adc_per_ms;
    feature->slope_ref_adc_per_ms = slope_ref_adc_per_ms;
    feature->slope_delta_adc_per_ms = slope_delta_adc_per_ms;
    feature->high_hold_us = high_hold_us;
    feature->area_adc_us = area_adc_us;
    feature->ref_ready = s_pidm_runtime.ref_ready;

    feature->peak_hit = (feature->peak_excess_raw >= (int)cfg.threshold_min_rise);
    if (feature->ref_ready) {
        feature->peak_hit = feature->peak_hit && (peak_delta_raw >= (int)cfg.peak_delta_min);
    }

    feature->slope_hit = (rise_slope_adc_per_ms >= cfg.slope_min_adc_per_ms);
    if (feature->ref_ready) {
        feature->slope_hit =
            feature->slope_hit && (slope_delta_adc_per_ms >= cfg.slope_delta_min_adc_per_ms);
    }

    feature->hold_hit = (high_hold_us >= cfg.high_hold_min_us);
    feature->area_hit = (area_adc_us >= cfg.area_min_adc_us);
    feature->pulse_hit =
        feature->peak_hit && feature->slope_hit && feature->hold_hit && feature->area_hit;

    if (feature->pulse_hit) {
        if (s_pidm_runtime.assert_streak < UINT8_MAX) {
            s_pidm_runtime.assert_streak++;
        }
        s_pidm_runtime.release_streak = 0;
        if (!s_pidm_runtime.metal_present && s_pidm_runtime.assert_streak >= cfg.assert_count) {
            s_pidm_runtime.metal_present = true;
        }
    } else {
        if (s_pidm_runtime.release_streak < UINT8_MAX) {
            s_pidm_runtime.release_streak++;
        }
        s_pidm_runtime.assert_streak = 0;
        if (s_pidm_runtime.metal_present && s_pidm_runtime.release_streak >= cfg.release_count) {
            s_pidm_runtime.metal_present = false;
        }
    }

    feature->metal_present = s_pidm_runtime.metal_present;
    return ESP_OK;
#endif
}

bool pidm_det_is_metal_present(void) { return s_pidm_runtime.metal_present; }
