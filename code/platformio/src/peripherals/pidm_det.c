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
static signal_event_algo_ctx_s s_pidm_algo_ctx = {0};

/*
 * brief: Map detector feature config into reusable CFAR feature-detector config.
 * input: src - detector config; dst - algorithm config output.
 * output: None.
 */
static void _pidm_fill_algo_cfg(const pidm_det_feature_cfg_s *src,
                                signal_event_algo_cfg_s *dst)
{
    if ((src == NULL) || (dst == NULL))
    {
        return;
    }

    dst->baseline_samples = src->baseline_samples;
    dst->wave_samples = src->wave_samples;
    dst->wave_interval_us = src->wave_interval_us;
    dst->threshold_min_rise = src->threshold_min_rise;
    dst->threshold_noise_gain_q4 = src->threshold_noise_gain_q4;
    dst->high_hold_min_us = src->high_hold_min_us;
    dst->area_min_adc_us = src->area_min_adc_us;
    dst->peak_delta_min = src->peak_delta_min;
    dst->slope_delta_min_adc_per_ms = src->slope_delta_min_adc_per_ms;
    dst->ref_learn_frames = src->ref_learn_pulses;
    dst->ref_ema_shift = src->ref_ema_shift;
    dst->assert_count = src->assert_count;
    dst->release_count = src->release_count;
}

/*
 * brief: Validate whether feature extraction and detection config is usable.
 * input: cfg - config pointer.
 * output: true when config values are within supported ranges.
 */
static bool _pidm_cfg_is_valid(const pidm_det_feature_cfg_s *cfg)
{
    signal_event_algo_cfg_s algo_cfg;

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

    _pidm_fill_algo_cfg(cfg, &algo_cfg);
    return signal_event_algo_cfg_is_valid(&algo_cfg);
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
    signal_event_algo_cfg_s algo_cfg;
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

    _pidm_fill_algo_cfg(&s_pidm_feature_cfg, &algo_cfg);
    ret = signal_event_algo_init(&s_pidm_algo_ctx, &algo_cfg);
    if (ret != ESP_OK)
    {
        (void)adc_oneshot_del_unit(s_pidm_adc_handle);
        s_pidm_adc_handle = NULL;
        s_pidm_ready = false;
        s_pidm_enabled = false;
        ESP_LOGE(TAG, "signal_event_algo_init failed: %d", (int)ret);
        return ret;
    }

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
    (void)signal_event_algo_reset_state(&s_pidm_algo_ctx);
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
    signal_event_algo_cfg_s algo_cfg;

    if (!_pidm_cfg_is_valid(cfg))
    {
        return ESP_ERR_INVALID_ARG;
    }

    s_pidm_feature_cfg = *cfg;

    _pidm_fill_algo_cfg(&s_pidm_feature_cfg, &algo_cfg);
    if (!signal_event_algo_cfg_is_valid(&algo_cfg))
    {
        return ESP_ERR_INVALID_ARG;
    }

    if (signal_event_algo_cfg_is_valid(&s_pidm_algo_ctx.cfg))
    {
        return signal_event_algo_set_cfg(&s_pidm_algo_ctx, &algo_cfg);
    }

    return signal_event_algo_init(&s_pidm_algo_ctx, &algo_cfg);
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
    signal_event_algo_frame_s frame;
    signal_event_algo_result_s algo_result;
    int baseline_buf[PIDM_DET_BASELINE_SAMPLE_MAX];
    int wave_buf[PIDM_DET_WAVE_SAMPLE_MAX];
    uint32_t i;
    esp_err_t ret;

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

    for (i = 0U; i < cfg.baseline_samples; i++)
    {
        int raw;

        USER_RETURN_ON_ERROR(pidm_det_read_raw(&raw), TAG, "baseline sample failed");
        baseline_buf[i] = raw;

        if ((i + 1U < cfg.baseline_samples) && (cfg.baseline_interval_us > 0U))
        {
            esp_rom_delay_us(cfg.baseline_interval_us);
        }
    }

    USER_RETURN_ON_ERROR(pidm_det_pulse_us(pulse_us), TAG, "pidm_det_pulse_us failed");

    if (cfg.settle_us > 0U)
    {
        esp_rom_delay_us(cfg.settle_us);
    }

    for (i = 0U; i < cfg.wave_samples; i++)
    {
        int raw;

        USER_RETURN_ON_ERROR(pidm_det_read_raw(&raw), TAG, "wave sample failed");
        wave_buf[i] = raw;
        if ((i + 1U < cfg.wave_samples) && (cfg.wave_interval_us > 0U))
        {
            esp_rom_delay_us(cfg.wave_interval_us);
        }
    }

    frame.baseline_data = baseline_buf;
    frame.baseline_count = cfg.baseline_samples;
    frame.wave_data = wave_buf;
    frame.wave_count = cfg.wave_samples;
    frame.wave_interval_us = cfg.wave_interval_us;

    ret = signal_event_algo_process(&s_pidm_algo_ctx, &frame, &algo_result);
    if (ret != ESP_OK)
    {
        return ret;
    }

    feature->baseline_raw = algo_result.baseline_raw;
    feature->baseline_noise = algo_result.baseline_noise;
    feature->threshold_raw = algo_result.threshold_raw;
    feature->wave_avg_raw = algo_result.wave_avg_raw;
    feature->peak_raw = algo_result.peak_raw;
    feature->peak_excess_raw = algo_result.peak_excess_raw;
    feature->peak_ref_raw = algo_result.peak_ref_raw;
    feature->peak_delta_raw = algo_result.peak_delta_raw;
    feature->peak_time_us = algo_result.peak_time_us;
    feature->rise_slope_adc_per_ms = algo_result.rise_slope_adc_per_ms;
    feature->slope_ref_adc_per_ms = algo_result.slope_ref_adc_per_ms;
    feature->slope_delta_adc_per_ms = algo_result.slope_delta_adc_per_ms;
    feature->high_hold_us = algo_result.high_hold_us;
    feature->area_adc_us = algo_result.area_adc_us;
    feature->ref_ready = algo_result.ref_ready;
    feature->peak_hit = algo_result.peak_hit;
    feature->slope_hit = algo_result.slope_hit;
    feature->hold_hit = algo_result.hold_hit;
    feature->area_hit = algo_result.area_hit;
    feature->pulse_hit = algo_result.pulse_hit;
    feature->metal_present = algo_result.event_present;

    return ESP_OK;
}

/*
 * brief: Read current debounced metal-present state from detection state machine.
 * input: None.
 * output: true when detector state is metal-present.
 */
bool pidm_det_is_metal_present(void)
{
    return signal_event_algo_is_event_present(&s_pidm_algo_ctx);
}
