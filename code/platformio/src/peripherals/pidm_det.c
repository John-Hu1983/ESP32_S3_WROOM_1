#include "pidm_det.h"

#define TAG "PIDM_DET"

static bool s_pidm_ready = false;
static bool s_pidm_enabled = false;
static adc_oneshot_unit_handle_t s_pidm_adc_handle = NULL;
static adc_unit_t s_pidm_adc_unit = ADC_UNIT_1;
static adc_channel_t s_pidm_adc_channel = PIDM_ADC_CHANNEL;
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
static bool s_pidm_metal_present = false;
static uint8_t s_pidm_assert_streak = 0U;
static uint8_t s_pidm_release_streak = 0U;
static bool s_pidm_ref_ready = false;
static uint16_t s_pidm_ref_count = 0U;
static int32_t s_pidm_ref_peak_excess_q8 = 0;
static int32_t s_pidm_ref_slope_q8 = 0;

/*
 * brief: Return absolute value for signed 32-bit integer.
 * input: value - source value.
 * output: Absolute value of source.
 */
static int32_t _pidm_abs_i32(int32_t value)
{
    return (value >= 0) ? value : -value;
}

/*
 * brief: Reset reference-learning state used by relative peak/slope detection.
 * input: None.
 * output: None.
 */
static void _pidm_ref_reset(void)
{
    s_pidm_ref_ready = false;
    s_pidm_ref_count = 0U;
    s_pidm_ref_peak_excess_q8 = 0;
    s_pidm_ref_slope_q8 = 0;
}

/*
 * brief: Learn initial no-metal reference from first N pulses.
 * input: peak_excess - current pulse peak above baseline; slope - current pulse max rise slope.
 * output: None.
 */
static void _pidm_ref_learn(uint32_t peak_excess, uint32_t slope)
{
    int32_t peak_q8;
    int32_t slope_q8;

    peak_q8 = (int32_t)(peak_excess << 8);
    slope_q8 = (int32_t)(slope << 8);

    if (s_pidm_ref_count == 0U)
    {
        s_pidm_ref_peak_excess_q8 = peak_q8;
        s_pidm_ref_slope_q8 = slope_q8;
    }
    else
    {
        int32_t den;

        den = (int32_t)s_pidm_ref_count + 1;
        s_pidm_ref_peak_excess_q8 += (peak_q8 - s_pidm_ref_peak_excess_q8) / den;
        s_pidm_ref_slope_q8 += (slope_q8 - s_pidm_ref_slope_q8) / den;
    }

    if (s_pidm_ref_count < 0xFFFFU)
    {
        s_pidm_ref_count++;
    }

    if (s_pidm_ref_count >= s_pidm_feature_cfg.ref_learn_pulses)
    {
        s_pidm_ref_ready = true;
    }
}

/*
 * brief: Update no-metal reference by EMA to track slow drift.
 * input: peak_excess - current pulse peak above baseline; slope - current pulse max rise slope.
 * output: None.
 */
static void _pidm_ref_ema_update(uint32_t peak_excess, uint32_t slope)
{
    uint8_t shift;
    int32_t peak_q8;
    int32_t slope_q8;

    shift = s_pidm_feature_cfg.ref_ema_shift;
    if (shift == 0U)
    {
        shift = 1U;
    }

    peak_q8 = (int32_t)(peak_excess << 8);
    slope_q8 = (int32_t)(slope << 8);

    s_pidm_ref_peak_excess_q8 += (peak_q8 - s_pidm_ref_peak_excess_q8) >> shift;
    s_pidm_ref_slope_q8 += (slope_q8 - s_pidm_ref_slope_q8) >> shift;
}

/*
 * brief: Validate whether feature extraction and detection config is usable.
 * input: cfg - config pointer.
 * output: true when config values are within supported ranges.
 */
static bool _pidm_cfg_is_valid(const pidm_det_feature_cfg_s *cfg)
{
    if (cfg == NULL)
    {
        return false;
    }

    if ((cfg->baseline_samples == 0U) ||
        (cfg->baseline_samples > PIDM_DET_BASELINE_SAMPLE_MAX))
    {
        return false;
    }

    if ((cfg->wave_samples == 0U) ||
        (cfg->wave_samples > PIDM_DET_WAVE_SAMPLE_MAX))
    {
        return false;
    }

    if ((cfg->baseline_interval_us == 0U) || (cfg->wave_interval_us == 0U))
    {
        return false;
    }

    if ((cfg->assert_count == 0U) || (cfg->release_count == 0U))
    {
        return false;
    }

    if ((cfg->ref_learn_pulses == 0U) || (cfg->ref_ema_shift == 0U) ||
        (cfg->ref_ema_shift > 7U))
    {
        return false;
    }

    return true;
}

/*
 * brief: Update debounced metal-present state based on current pulse-level match.
 * input: pulse_hit - true if current pulse matches detector pattern.
 * output: None.
 */
static void _pidm_update_metal_state(bool pulse_hit)
{
    if (pulse_hit)
    {
        if (s_pidm_assert_streak < 0xFFU)
        {
            s_pidm_assert_streak++;
        }

        s_pidm_release_streak = 0U;
        if (s_pidm_assert_streak >= s_pidm_feature_cfg.assert_count)
        {
            s_pidm_metal_present = true;
        }
        return;
    }

    if (s_pidm_release_streak < 0xFFU)
    {
        s_pidm_release_streak++;
    }

    s_pidm_assert_streak = 0U;
    if (s_pidm_release_streak >= s_pidm_feature_cfg.release_count)
    {
        s_pidm_metal_present = false;
    }
}

/*
 * brief: Validate configured PIDM ADC GPIO and resolve its ADC unit/channel mapping.
 * input: unit - output ADC unit; channel - output ADC channel.
 * output: ESP_OK on success; otherwise invalid argument or unsupported GPIO mapping.
 */
static esp_err_t _pidm_resolve_adc_mapping(adc_unit_t *unit, adc_channel_t *channel)
{
    esp_err_t ret;

    if ((unit == NULL) || (channel == NULL))
    {
        return ESP_ERR_INVALID_ARG;
    }

    if ((int)PIDM_IO_ADC < 0)
    {
        ESP_LOGE(TAG, "PIDM_IO_ADC is invalid: %d", (int)PIDM_IO_ADC);
        return ESP_ERR_INVALID_ARG;
    }

    ret = adc_oneshot_io_to_channel((int)PIDM_IO_ADC, unit, channel);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG,
                 "adc io map failed io=%d ret=%d",
                 (int)PIDM_IO_ADC,
                 (int)ret);
        return ret;
    }

    if (*channel != PIDM_ADC_CHANNEL)
    {
        ESP_LOGE(TAG,
                 "PIDM_ADC_CHANNEL mismatch cfg=%d io_map=%d",
                 (int)PIDM_ADC_CHANNEL,
                 (int)(*channel));
        return ESP_ERR_INVALID_ARG;
    }

    return ESP_OK;
}

/*
 * brief: Configure pulse GPIO as push-pull output and drive it to low level.
 * input: None.
 * output: ESP_OK on success; otherwise invalid argument or GPIO driver error.
 */
static esp_err_t _pidm_config_pulse_gpio(void)
{
    gpio_config_t io_cfg = {0};

    if ((int)PIDM_IO_PULSE < 0)
    {
        ESP_LOGE(TAG, "PIDM_IO_PULSE is invalid: %d", (int)PIDM_IO_PULSE);
        return ESP_ERR_INVALID_ARG;
    }

    io_cfg.pin_bit_mask = (1ULL << (uint32_t)PIDM_IO_PULSE);
    io_cfg.mode = GPIO_MODE_OUTPUT;
    io_cfg.pull_up_en = GPIO_PULLUP_DISABLE;
    io_cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_cfg.intr_type = GPIO_INTR_DISABLE;

    USER_RETURN_ON_ERROR(gpio_config(&io_cfg), TAG, "gpio_config PIDM_IO_PULSE failed");
    USER_RETURN_ON_ERROR(gpio_set_level(PIDM_IO_PULSE, 0), TAG, "gpio_set_level PIDM_IO_PULSE low failed");

    return ESP_OK;
}

/*
 * brief: Initialize PIDM detector backend: EN pin, pulse GPIO, and ADC oneshot channel.
 * input: None.
 * output: ESP_OK on success; otherwise propagated setup error.
 */
esp_err_t pidm_det_init(void)
{
    adc_oneshot_unit_init_cfg_t unit_cfg = {0};
    adc_oneshot_chan_cfg_t chan_cfg = {0};
    adc_unit_t io_unit;
    adc_channel_t io_channel;
    esp_err_t ret;

    if (s_pidm_ready)
    {
        return ESP_OK;
    }

    USER_RETURN_ON_ERROR(gpba02b_pin_set_mode(PIDM_EN_PORT,
                                              PIDM_EN_PIN,
                                              GPBA02B_PIN_MODE_OUTPUT),
                         TAG,
                         "gpba02b_pin_set_mode PIDM_EN failed");

    USER_RETURN_ON_ERROR(gpba02b_pin_write(PIDM_EN_PORT,
                                           PIDM_EN_PIN,
                                           true),
                         TAG,
                         "gpba02b_pin_write PIDM_EN high failed");
    s_pidm_enabled = true;

    USER_RETURN_ON_ERROR(_pidm_config_pulse_gpio(), TAG, "config pulse gpio failed");

    USER_RETURN_ON_ERROR(_pidm_resolve_adc_mapping(&io_unit, &io_channel), TAG, "resolve adc mapping failed");

    unit_cfg.unit_id = io_unit;
    unit_cfg.clk_src = 0;
    unit_cfg.ulp_mode = ADC_ULP_MODE_DISABLE;

    ret = adc_oneshot_new_unit(&unit_cfg, &s_pidm_adc_handle);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "adc_oneshot_new_unit failed: %d", (int)ret);
        return ret;
    }

    chan_cfg.atten = PIDM_DET_ADC_ATTEN;
    chan_cfg.bitwidth = PIDM_DET_ADC_BITWIDTH;
    ret = adc_oneshot_config_channel(s_pidm_adc_handle, io_channel, &chan_cfg);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "adc_oneshot_config_channel failed: %d", (int)ret);
        (void)adc_oneshot_del_unit(s_pidm_adc_handle);
        s_pidm_adc_handle = NULL;
        return ret;
    }

    s_pidm_adc_unit = io_unit;
    s_pidm_adc_channel = io_channel;
    s_pidm_ready = true;
    s_pidm_metal_present = false;
    s_pidm_assert_streak = 0U;
    s_pidm_release_streak = 0U;
    _pidm_ref_reset();

    ESP_LOGI(TAG,
             "pidm_det ready en=%d pulse_io=%d adc_io=%d unit=%d ch=%d",
             (int)s_pidm_enabled,
             (int)PIDM_IO_PULSE,
             (int)PIDM_IO_ADC,
             (int)s_pidm_adc_unit,
             (int)s_pidm_adc_channel);

    return ESP_OK;
}

/*
 * brief: Deinitialize PIDM detector backend and release ADC resources.
 * input: None.
 * output: ESP_OK on success; otherwise propagated resource release error.
 */
esp_err_t pidm_det_deinit(void)
{
    esp_err_t ret;

    if (!s_pidm_ready)
    {
        return ESP_OK;
    }

    (void)gpio_set_level(PIDM_IO_PULSE, 0);

    if (s_pidm_adc_handle != NULL)
    {
        ret = adc_oneshot_del_unit(s_pidm_adc_handle);
        if (ret != ESP_OK)
        {
            ESP_LOGE(TAG, "adc_oneshot_del_unit failed: %d", (int)ret);
            return ret;
        }
    }

    s_pidm_adc_handle = NULL;
    s_pidm_ready = false;
    s_pidm_enabled = false;
    s_pidm_adc_unit = ADC_UNIT_1;
    s_pidm_adc_channel = PIDM_ADC_CHANNEL;
    s_pidm_metal_present = false;
    s_pidm_assert_streak = 0U;
    s_pidm_release_streak = 0U;
    _pidm_ref_reset();
    return ESP_OK;
}

/*
 * brief: Query whether PIDM detector backend is initialized.
 * input: None.
 * output: true when initialized; otherwise false.
 */
bool pidm_det_is_ready(void)
{
    return s_pidm_ready;
}

/*
 * brief: Enable or disable PIDM detector analog frontend power.
 * input: enable - true to power-on frontend, false to power-off.
 * output: ESP_OK on success; otherwise invalid state or GPBA write error.
 */
esp_err_t pidm_det_set_enable(bool enable)
{
    if (!s_pidm_ready)
    {
        return ESP_ERR_INVALID_STATE;
    }

    USER_RETURN_ON_ERROR(gpba02b_pin_write(PIDM_EN_PORT,
                                           PIDM_EN_PIN,
                                           enable),
                         TAG,
                         "gpba02b_pin_write PIDM_EN failed");

    s_pidm_enabled = enable;
    return ESP_OK;
}

/*
 * brief: Set PIDM pulse output level directly.
 * input: high - true to drive high, false to drive low.
 * output: ESP_OK on success; otherwise invalid state or GPIO set-level error.
 */
esp_err_t pidm_det_set_pulse_level(bool high)
{
    if (!s_pidm_ready)
    {
        return ESP_ERR_INVALID_STATE;
    }

    return gpio_set_level(PIDM_IO_PULSE, high ? 1 : 0);
}

/*
 * brief: Generate one active-high pulse on PIDM pulse pin.
 * input: pulse_us - pulse width in microseconds.
 * output: ESP_OK on success; otherwise argument/state/GPIO errors.
 */
esp_err_t pidm_det_pulse_us(uint32_t pulse_us)
{
    if (pulse_us == 0U)
    {
        return ESP_ERR_INVALID_ARG;
    }

    USER_RETURN_ON_ERROR(pidm_det_set_pulse_level(true), TAG, "set pulse high failed");
    esp_rom_delay_us(pulse_us);
    USER_RETURN_ON_ERROR(pidm_det_set_pulse_level(false), TAG, "set pulse low failed");

    return ESP_OK;
}

/*
 * brief: Read one raw ADC sample from PIDM detector output node.
 * input: raw_value - output pointer for ADC raw value.
 * output: ESP_OK on success; otherwise invalid argument/state or ADC read error.
 */
esp_err_t pidm_det_read_raw(int *raw_value)
{
    if (raw_value == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_pidm_ready || (s_pidm_adc_handle == NULL))
    {
        return ESP_ERR_INVALID_STATE;
    }

    return adc_oneshot_read(s_pidm_adc_handle, s_pidm_adc_channel, raw_value);
}

/*
 * brief: Read averaged raw ADC value over N samples with optional inter-sample delay.
 * input: sample_count - number of samples to average; sample_interval_ms - delay between samples; raw_avg - output average.
 * output: ESP_OK on success; otherwise invalid argument/state or ADC read error.
 */
esp_err_t pidm_det_read_average(uint8_t sample_count,
                                uint16_t sample_interval_ms,
                                int *raw_avg)
{
    uint32_t i;
    int32_t sum;

    if ((raw_avg == NULL) || (sample_count == 0U))
    {
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_pidm_ready)
    {
        return ESP_ERR_INVALID_STATE;
    }

    sum = 0;
    for (i = 0U; i < sample_count; i++)
    {
        int raw;

        USER_RETURN_ON_ERROR(pidm_det_read_raw(&raw), TAG, "pidm_det_read_raw failed");
        sum += raw;

        if ((sample_interval_ms > 0U) && (i + 1U < sample_count))
        {
            delay_ms(sample_interval_ms);
        }
    }

    *raw_avg = (int)(sum / (int32_t)sample_count);
    return ESP_OK;
}

/*
 * brief: Fill one feature extraction configuration with project default values.
 * input: cfg - output config pointer.
 * output: None.
 */
void pidm_det_feature_cfg_load_default(pidm_det_feature_cfg_s *cfg)
{
    if (cfg == NULL)
    {
        return;
    }

    cfg->baseline_samples = PIDM_DET_BASELINE_SAMPLES_DEFAULT;
    cfg->baseline_interval_us = PIDM_DET_BASELINE_INTERVAL_US_DEFAULT;
    cfg->wave_samples = PIDM_DET_WAVE_SAMPLES_DEFAULT;
    cfg->wave_interval_us = PIDM_DET_WAVE_INTERVAL_US_DEFAULT;
    cfg->settle_us = PIDM_DET_SETTLE_US_DEFAULT;
    cfg->threshold_min_rise = PIDM_DET_THRESHOLD_MIN_RISE_DEFAULT;
    cfg->threshold_noise_gain_q4 = PIDM_DET_THRESHOLD_NOISE_GAIN_Q4_DEFAULT;
    cfg->slope_min_adc_per_ms = PIDM_DET_SLOPE_MIN_ADC_PER_MS_DEFAULT;
    cfg->high_hold_min_us = PIDM_DET_HIGH_HOLD_MIN_US_DEFAULT;
    cfg->area_min_adc_us = PIDM_DET_AREA_MIN_ADC_US_DEFAULT;
    cfg->peak_delta_min = PIDM_DET_PEAK_DELTA_MIN_DEFAULT;
    cfg->slope_delta_min_adc_per_ms = PIDM_DET_SLOPE_DELTA_MIN_ADC_PER_MS_DEFAULT;
    cfg->ref_learn_pulses = PIDM_DET_REF_LEARN_PULSES_DEFAULT;
    cfg->ref_ema_shift = PIDM_DET_REF_EMA_SHIFT_DEFAULT;
    cfg->assert_count = PIDM_DET_ASSERT_COUNT_DEFAULT;
    cfg->release_count = PIDM_DET_RELEASE_COUNT_DEFAULT;
}

/*
 * brief: Apply feature extraction and detection config for subsequent probe calls.
 * input: cfg - new config values.
 * output: ESP_OK on success; otherwise ESP_ERR_INVALID_ARG.
 */
esp_err_t pidm_det_feature_cfg_set(const pidm_det_feature_cfg_s *cfg)
{
    if (!_pidm_cfg_is_valid(cfg))
    {
        return ESP_ERR_INVALID_ARG;
    }

    s_pidm_feature_cfg = *cfg;
    s_pidm_metal_present = false;
    s_pidm_assert_streak = 0U;
    s_pidm_release_streak = 0U;
    _pidm_ref_reset();
    return ESP_OK;
}

/*
 * brief: Return current feature extraction and detection config snapshot.
 * input: None.
 * output: Current config values.
 */
pidm_det_feature_cfg_s pidm_det_feature_cfg_get(void)
{
    return s_pidm_feature_cfg;
}

/*
 * brief: Probe one pulse and extract edge/hold/area features with debounced state.
 * input: pulse_us - pulse width in microseconds; feature - output feature snapshot.
 * output: ESP_OK on success; otherwise invalid argument/state or ADC read error.
 */
esp_err_t pidm_det_probe_feature(uint32_t pulse_us, pidm_det_feature_s *feature)
{
    pidm_det_feature_cfg_s cfg;
    int baseline_buf[PIDM_DET_BASELINE_SAMPLE_MAX];
    int32_t baseline_sum;
    int32_t noise_sum;
    int32_t wave_sum;
    uint64_t area_acc;
    uint32_t i;
    int baseline_raw;
    int baseline_noise;
    uint32_t threshold_rise_noise;
    uint32_t threshold_rise;
    int threshold_raw;
    int prev_raw;
    int peak_raw;
    int peak_excess_raw;
    int peak_ref_raw;
    int peak_delta_raw;
    uint32_t peak_idx;
    uint32_t rise_slope_adc_per_ms;
    uint32_t slope_ref_adc_per_ms;
    uint32_t slope_delta_adc_per_ms;
    uint32_t high_hold_us;
    uint32_t cur_hold_us;
    bool peak_hit;
    bool slope_hit;
    bool hold_hit;
    bool area_hit;
    bool pulse_hit;

    if ((feature == NULL) || (pulse_us == 0U))
    {
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_pidm_ready || (s_pidm_adc_handle == NULL))
    {
        return ESP_ERR_INVALID_STATE;
    }

    cfg = s_pidm_feature_cfg;
    if (!_pidm_cfg_is_valid(&cfg))
    {
        return ESP_ERR_INVALID_STATE;
    }

    baseline_sum = 0;
    for (i = 0U; i < cfg.baseline_samples; i++)
    {
        int raw;

        USER_RETURN_ON_ERROR(pidm_det_read_raw(&raw), TAG, "baseline sample failed");
        baseline_buf[i] = raw;
        baseline_sum += raw;

        if ((i + 1U < cfg.baseline_samples) && (cfg.baseline_interval_us > 0U))
        {
            esp_rom_delay_us(cfg.baseline_interval_us);
        }
    }

    baseline_raw = (int)(baseline_sum / (int32_t)cfg.baseline_samples);

    noise_sum = 0;
    for (i = 0U; i < cfg.baseline_samples; i++)
    {
        noise_sum += _pidm_abs_i32((int32_t)baseline_buf[i] - (int32_t)baseline_raw);
    }
    baseline_noise = (int)(noise_sum / (int32_t)cfg.baseline_samples);

    threshold_rise_noise = ((uint32_t)baseline_noise * (uint32_t)cfg.threshold_noise_gain_q4 + 8U) / 16U;
    threshold_rise = threshold_rise_noise;
    if (threshold_rise < (uint32_t)cfg.threshold_min_rise)
    {
        threshold_rise = (uint32_t)cfg.threshold_min_rise;
    }

    threshold_raw = baseline_raw + (int)threshold_rise;

    USER_RETURN_ON_ERROR(pidm_det_pulse_us(pulse_us), TAG, "pidm_det_pulse_us failed");

    if (cfg.settle_us > 0U)
    {
        esp_rom_delay_us(cfg.settle_us);
    }

    wave_sum = 0;
    area_acc = 0U;
    prev_raw = baseline_raw;
    peak_raw = baseline_raw;
    peak_idx = 0U;
    rise_slope_adc_per_ms = 0U;
    high_hold_us = 0U;
    cur_hold_us = 0U;

    for (i = 0U; i < cfg.wave_samples; i++)
    {
        int raw;
        int delta;

        USER_RETURN_ON_ERROR(pidm_det_read_raw(&raw), TAG, "wave sample failed");
        wave_sum += raw;

        if (raw > peak_raw)
        {
            peak_raw = raw;
            peak_idx = i;
        }

        delta = raw - prev_raw;
        if (delta > 0)
        {
            uint32_t slope;

            slope = ((uint32_t)delta * 1000U) / (uint32_t)cfg.wave_interval_us;
            if (slope > rise_slope_adc_per_ms)
            {
                rise_slope_adc_per_ms = slope;
            }
        }

        if (raw > threshold_raw)
        {
            uint32_t above;

            above = (uint32_t)(raw - threshold_raw);
            area_acc += ((uint64_t)above * (uint64_t)cfg.wave_interval_us);
            if (area_acc > 0xFFFFFFFFULL)
            {
                area_acc = 0xFFFFFFFFULL;
            }

            cur_hold_us += (uint32_t)cfg.wave_interval_us;
            if (cur_hold_us > high_hold_us)
            {
                high_hold_us = cur_hold_us;
            }
        }
        else
        {
            cur_hold_us = 0U;
        }

        prev_raw = raw;
        if ((i + 1U < cfg.wave_samples) && (cfg.wave_interval_us > 0U))
        {
            esp_rom_delay_us(cfg.wave_interval_us);
        }
    }

    hold_hit = (high_hold_us >= (uint32_t)cfg.high_hold_min_us);
    area_hit = ((uint32_t)area_acc >= cfg.area_min_adc_us);

    peak_excess_raw = peak_raw - baseline_raw;
    if (peak_excess_raw < 0)
    {
        peak_excess_raw = 0;
    }

    if (!s_pidm_ref_ready)
    {
        _pidm_ref_learn((uint32_t)peak_excess_raw, rise_slope_adc_per_ms);
        peak_ref_raw = (int)(s_pidm_ref_peak_excess_q8 >> 8);
        if (peak_ref_raw < 0)
        {
            peak_ref_raw = 0;
        }

        slope_ref_adc_per_ms = (uint32_t)((s_pidm_ref_slope_q8 > 0) ?
                                          (s_pidm_ref_slope_q8 >> 8) :
                                          0);
        peak_delta_raw = 0;
        slope_delta_adc_per_ms = 0U;
        peak_hit = false;
        slope_hit = false;
        pulse_hit = false;
    }
    else
    {
        peak_ref_raw = (int)(s_pidm_ref_peak_excess_q8 >> 8);
        if (peak_ref_raw < 0)
        {
            peak_ref_raw = 0;
        }

        slope_ref_adc_per_ms = (uint32_t)((s_pidm_ref_slope_q8 > 0) ?
                                          (s_pidm_ref_slope_q8 >> 8) :
                                          0);

        peak_delta_raw = peak_excess_raw - peak_ref_raw;
        if (peak_delta_raw < 0)
        {
            peak_delta_raw = 0;
        }

        if (rise_slope_adc_per_ms > slope_ref_adc_per_ms)
        {
            slope_delta_adc_per_ms = rise_slope_adc_per_ms - slope_ref_adc_per_ms;
        }
        else
        {
            slope_delta_adc_per_ms = 0U;
        }

        peak_hit = ((uint32_t)peak_delta_raw >= (uint32_t)cfg.peak_delta_min);
        slope_hit = (slope_delta_adc_per_ms >= (uint32_t)cfg.slope_delta_min_adc_per_ms);

        /* For this board, relative peak+slope separation is much cleaner than hold/area. */
        pulse_hit = peak_hit && slope_hit;

        if (!s_pidm_metal_present && !pulse_hit)
        {
            _pidm_ref_ema_update((uint32_t)peak_excess_raw, rise_slope_adc_per_ms);
        }
    }

    _pidm_update_metal_state(pulse_hit);

    feature->baseline_raw = baseline_raw;
    feature->baseline_noise = baseline_noise;
    feature->threshold_raw = threshold_raw;
    feature->wave_avg_raw = (int)(wave_sum / (int32_t)cfg.wave_samples);
    feature->peak_raw = peak_raw;
    feature->peak_excess_raw = peak_excess_raw;
    feature->peak_ref_raw = peak_ref_raw;
    feature->peak_delta_raw = peak_delta_raw;
    feature->peak_time_us = peak_idx * (uint32_t)cfg.wave_interval_us;
    feature->rise_slope_adc_per_ms = rise_slope_adc_per_ms;
    feature->slope_ref_adc_per_ms = slope_ref_adc_per_ms;
    feature->slope_delta_adc_per_ms = slope_delta_adc_per_ms;
    feature->high_hold_us = high_hold_us;
    feature->area_adc_us = (uint32_t)area_acc;
    feature->ref_ready = s_pidm_ref_ready;
    feature->peak_hit = peak_hit;
    feature->slope_hit = slope_hit;
    feature->hold_hit = hold_hit;
    feature->area_hit = area_hit;
    feature->pulse_hit = pulse_hit;
    feature->metal_present = s_pidm_metal_present;

    return ESP_OK;
}

/*
 * brief: Read current debounced metal-present state from detection state machine.
 * input: None.
 * output: true when detector state is metal-present.
 */
bool pidm_det_is_metal_present(void)
{
    return s_pidm_metal_present;
}

/*
 * brief: Generate one pulse, wait settle time, then read averaged detector value.
 * input: pulse_us - pulse width in microseconds; settle_us - delay after pulse in microseconds; sample_count - ADC average sample count; raw_avg - output average.
 * output: ESP_OK on success; otherwise propagated pulse/read errors.
 */
esp_err_t pidm_det_probe_once(uint32_t pulse_us,
                              uint32_t settle_us,
                              uint8_t sample_count,
                              int *raw_avg)
{
    USER_RETURN_ON_ERROR(pidm_det_pulse_us(pulse_us), TAG, "pidm_det_pulse_us failed");

    if (settle_us > 0U)
    {
        esp_rom_delay_us(settle_us);
    }

    return pidm_det_read_average(sample_count, 0U, raw_avg);
}

/*
 * brief: Read current PIDM detector runtime status snapshot.
 * input: None.
 * output: Status snapshot containing ready/enabled/ADC unit/channel.
 */
pidm_det_info_s pidm_det_read_info(void)
{
    pidm_det_info_s info;

    info.ready = s_pidm_ready;
    info.enabled = s_pidm_enabled;
    info.adc_unit = s_pidm_adc_unit;
    info.adc_channel = s_pidm_adc_channel;
    return info;
}
