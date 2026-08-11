#include "cfar_feature_detector.h"

#define TAG "SIG_ALGO"

/*
 * abs(x) helper for signed 32-bit integer.
 */
static int32_t _algo_abs_i32(int32_t value)
{
    return (value >= 0) ? value : -value;
}

/*
 * Clear reference-learning accumulators.
 */
static void _algo_ref_reset(signal_event_algo_ctx_s *ctx)
{
    if (ctx == NULL)
    {
        return;
    }

    ctx->ref_ready = false;
    ctx->ref_count = 0U;
    ctx->ref_peak_excess_q8 = 0;
    ctx->ref_slope_q8 = 0;
}

/*
 * Learn initial no-event reference from first N frames.
 * Q8 means fixed-point with 8 fractional bits: value_q8 = value * 256.
 */
static void _algo_ref_learn(signal_event_algo_ctx_s *ctx,
                            uint32_t peak_excess,
                            uint32_t slope)
{
    int32_t peak_q8;
    int32_t slope_q8;

    if (ctx == NULL)
    {
        return;
    }

    peak_q8 = (int32_t)(peak_excess << 8);
    slope_q8 = (int32_t)(slope << 8);

    if (ctx->ref_count == 0U)
    {
        ctx->ref_peak_excess_q8 = peak_q8;
        ctx->ref_slope_q8 = slope_q8;
    }
    else
    {
        int32_t den;

        den = (int32_t)ctx->ref_count + 1;
        ctx->ref_peak_excess_q8 += (peak_q8 - ctx->ref_peak_excess_q8) / den;
        ctx->ref_slope_q8 += (slope_q8 - ctx->ref_slope_q8) / den;
    }

    if (ctx->ref_count < 0xFFFFU)
    {
        ctx->ref_count++;
    }

    if (ctx->ref_count >= ctx->cfg.ref_learn_frames)
    {
        ctx->ref_ready = true;
    }
}

/*
 * Exponential moving average update for slow baseline drift.
 * new_ref = old_ref + (sample - old_ref) / 2^shift
 */
static void _algo_ref_ema_update(signal_event_algo_ctx_s *ctx,
                                 uint32_t peak_excess,
                                 uint32_t slope)
{
    uint8_t shift;
    int32_t peak_q8;
    int32_t slope_q8;

    if (ctx == NULL)
    {
        return;
    }

    shift = ctx->cfg.ref_ema_shift;
    if (shift == 0U)
    {
        shift = 1U;
    }

    peak_q8 = (int32_t)(peak_excess << 8);
    slope_q8 = (int32_t)(slope << 8);

    ctx->ref_peak_excess_q8 += (peak_q8 - ctx->ref_peak_excess_q8) >> shift;
    ctx->ref_slope_q8 += (slope_q8 - ctx->ref_slope_q8) >> shift;
}

/*
 * Debounce event state from per-frame hit/miss.
 */
static void _algo_update_event_state(signal_event_algo_ctx_s *ctx, bool pulse_hit)
{
    if (ctx == NULL)
    {
        return;
    }

    if (pulse_hit)
    {
        if (ctx->assert_streak < 0xFFU)
        {
            ctx->assert_streak++;
        }

        ctx->release_streak = 0U;
        if (ctx->assert_streak >= ctx->cfg.assert_count)
        {
            ctx->event_present = true;
        }
        return;
    }

    if (ctx->release_streak < 0xFFU)
    {
        ctx->release_streak++;
    }

    ctx->assert_streak = 0U;
    if (ctx->release_streak >= ctx->cfg.release_count)
    {
        ctx->event_present = false;
    }
}

void signal_event_algo_load_default_cfg(signal_event_algo_cfg_s *cfg)
{
    if (cfg == NULL)
    {
        return;
    }

    cfg->baseline_samples = SIGNAL_EVENT_ALGO_BASELINE_SAMPLES_DEFAULT;
    cfg->wave_samples = SIGNAL_EVENT_ALGO_WAVE_SAMPLES_DEFAULT;
    cfg->wave_interval_us = SIGNAL_EVENT_ALGO_WAVE_INTERVAL_US_DEFAULT;
    cfg->threshold_min_rise = SIGNAL_EVENT_ALGO_THRESHOLD_MIN_RISE_DEFAULT;
    cfg->threshold_noise_gain_q4 = SIGNAL_EVENT_ALGO_THRESHOLD_NOISE_GAIN_Q4_DEFAULT;
    cfg->high_hold_min_us = SIGNAL_EVENT_ALGO_HIGH_HOLD_MIN_US_DEFAULT;
    cfg->area_min_adc_us = SIGNAL_EVENT_ALGO_AREA_MIN_ADC_US_DEFAULT;
    cfg->peak_delta_min = SIGNAL_EVENT_ALGO_PEAK_DELTA_MIN_DEFAULT;
    cfg->slope_delta_min_adc_per_ms = SIGNAL_EVENT_ALGO_SLOPE_DELTA_MIN_ADC_PER_MS_DEFAULT;
    cfg->ref_learn_frames = SIGNAL_EVENT_ALGO_REF_LEARN_FRAMES_DEFAULT;
    cfg->ref_ema_shift = SIGNAL_EVENT_ALGO_REF_EMA_SHIFT_DEFAULT;
    cfg->assert_count = SIGNAL_EVENT_ALGO_ASSERT_COUNT_DEFAULT;
    cfg->release_count = SIGNAL_EVENT_ALGO_RELEASE_COUNT_DEFAULT;
}

bool signal_event_algo_cfg_is_valid(const signal_event_algo_cfg_s *cfg)
{
    if (cfg == NULL)
    {
        return false;
    }

    if ((cfg->baseline_samples == 0U) ||
        (cfg->baseline_samples > SIGNAL_EVENT_ALGO_BASELINE_SAMPLE_MAX))
    {
        return false;
    }

    if ((cfg->wave_samples == 0U) ||
        (cfg->wave_samples > SIGNAL_EVENT_ALGO_WAVE_SAMPLE_MAX))
    {
        return false;
    }

    if (cfg->wave_interval_us == 0U)
    {
        return false;
    }

    if ((cfg->assert_count == 0U) || (cfg->release_count == 0U))
    {
        return false;
    }

    if ((cfg->ref_learn_frames == 0U) ||
        (cfg->ref_ema_shift == 0U) ||
        (cfg->ref_ema_shift > 7U))
    {
        return false;
    }

    return true;
}

esp_err_t signal_event_algo_init(signal_event_algo_ctx_s *ctx,
                                 const signal_event_algo_cfg_s *cfg)
{
    if ((ctx == NULL) || !signal_event_algo_cfg_is_valid(cfg))
    {
        return ESP_ERR_INVALID_ARG;
    }

    *ctx = (signal_event_algo_ctx_s){0};
    ctx->cfg = *cfg;
    _algo_ref_reset(ctx);
    return ESP_OK;
}

esp_err_t signal_event_algo_set_cfg(signal_event_algo_ctx_s *ctx,
                                    const signal_event_algo_cfg_s *cfg)
{
    if ((ctx == NULL) || !signal_event_algo_cfg_is_valid(cfg))
    {
        return ESP_ERR_INVALID_ARG;
    }

    ctx->cfg = *cfg;
    signal_event_algo_reset_state(ctx);
    return ESP_OK;
}

void signal_event_algo_reset_state(signal_event_algo_ctx_s *ctx)
{
    if (ctx == NULL)
    {
        return;
    }

    ctx->event_present = false;
    ctx->assert_streak = 0U;
    ctx->release_streak = 0U;
    _algo_ref_reset(ctx);
}

bool signal_event_algo_is_event_present(const signal_event_algo_ctx_s *ctx)
{
    if (ctx == NULL)
    {
        return false;
    }

    return ctx->event_present;
}

esp_err_t signal_event_algo_process(signal_event_algo_ctx_s *ctx,
                                    const signal_event_algo_frame_s *frame,
                                    signal_event_algo_result_s *result)
{
    uint8_t baseline_count;
    uint8_t wave_count;
    uint16_t wave_interval_us;
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

    if ((ctx == NULL) || (frame == NULL) || (result == NULL))
    {
        return ESP_ERR_INVALID_ARG;
    }

    if (!signal_event_algo_cfg_is_valid(&ctx->cfg))
    {
        return ESP_ERR_INVALID_STATE;
    }

    baseline_count = (frame->baseline_count > 0U) ?
                         frame->baseline_count :
                         ctx->cfg.baseline_samples;
    wave_count = (frame->wave_count > 0U) ?
                     frame->wave_count :
                     ctx->cfg.wave_samples;
    wave_interval_us = (frame->wave_interval_us > 0U) ?
                           frame->wave_interval_us :
                           ctx->cfg.wave_interval_us;

    if ((frame->baseline_data == NULL) || (frame->wave_data == NULL) ||
        (baseline_count == 0U) ||
        (baseline_count > SIGNAL_EVENT_ALGO_BASELINE_SAMPLE_MAX) ||
        (wave_count == 0U) ||
        (wave_count > SIGNAL_EVENT_ALGO_WAVE_SAMPLE_MAX) ||
        (wave_interval_us == 0U))
    {
        return ESP_ERR_INVALID_ARG;
    }

    baseline_sum = 0;
    for (i = 0U; i < baseline_count; i++)
    {
        baseline_sum += frame->baseline_data[i];
    }
    baseline_raw = (int)(baseline_sum / (int32_t)baseline_count);

    noise_sum = 0;
    for (i = 0U; i < baseline_count; i++)
    {
        noise_sum += _algo_abs_i32((int32_t)frame->baseline_data[i] - (int32_t)baseline_raw);
    }
    baseline_noise = (int)(noise_sum / (int32_t)baseline_count);

    threshold_rise_noise = ((uint32_t)baseline_noise * (uint32_t)ctx->cfg.threshold_noise_gain_q4 + 8U) / 16U;
    threshold_rise = threshold_rise_noise;
    if (threshold_rise < (uint32_t)ctx->cfg.threshold_min_rise)
    {
        threshold_rise = (uint32_t)ctx->cfg.threshold_min_rise;
    }
    threshold_raw = baseline_raw + (int)threshold_rise;

    wave_sum = 0;
    area_acc = 0U;
    prev_raw = baseline_raw;
    peak_raw = baseline_raw;
    peak_idx = 0U;
    rise_slope_adc_per_ms = 0U;
    high_hold_us = 0U;
    cur_hold_us = 0U;

    for (i = 0U; i < wave_count; i++)
    {
        int raw;
        int delta;

        raw = frame->wave_data[i];
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

            slope = ((uint32_t)delta * 1000U) / (uint32_t)wave_interval_us;
            if (slope > rise_slope_adc_per_ms)
            {
                rise_slope_adc_per_ms = slope;
            }
        }

        if (raw > threshold_raw)
        {
            uint32_t above;

            above = (uint32_t)(raw - threshold_raw);
            area_acc += ((uint64_t)above * (uint64_t)wave_interval_us);
            if (area_acc > 0xFFFFFFFFULL)
            {
                area_acc = 0xFFFFFFFFULL;
            }

            cur_hold_us += (uint32_t)wave_interval_us;
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
    }

    hold_hit = (high_hold_us >= (uint32_t)ctx->cfg.high_hold_min_us);
    area_hit = ((uint32_t)area_acc >= ctx->cfg.area_min_adc_us);

    peak_excess_raw = peak_raw - baseline_raw;
    if (peak_excess_raw < 0)
    {
        peak_excess_raw = 0;
    }

    if (!ctx->ref_ready)
    {
        _algo_ref_learn(ctx, (uint32_t)peak_excess_raw, rise_slope_adc_per_ms);

        peak_ref_raw = (int)(ctx->ref_peak_excess_q8 >> 8);
        if (peak_ref_raw < 0)
        {
            peak_ref_raw = 0;
        }

        slope_ref_adc_per_ms = (uint32_t)((ctx->ref_slope_q8 > 0) ?
                                          (ctx->ref_slope_q8 >> 8) :
                                          0);
        peak_delta_raw = 0;
        slope_delta_adc_per_ms = 0U;
        peak_hit = false;
        slope_hit = false;
        pulse_hit = false;
    }
    else
    {
        peak_ref_raw = (int)(ctx->ref_peak_excess_q8 >> 8);
        if (peak_ref_raw < 0)
        {
            peak_ref_raw = 0;
        }

        slope_ref_adc_per_ms = (uint32_t)((ctx->ref_slope_q8 > 0) ?
                                          (ctx->ref_slope_q8 >> 8) :
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

        peak_hit = ((uint32_t)peak_delta_raw >= (uint32_t)ctx->cfg.peak_delta_min);
        slope_hit = (slope_delta_adc_per_ms >= (uint32_t)ctx->cfg.slope_delta_min_adc_per_ms);
        pulse_hit = peak_hit && slope_hit;

        if (!ctx->event_present && !pulse_hit)
        {
            _algo_ref_ema_update(ctx, (uint32_t)peak_excess_raw, rise_slope_adc_per_ms);
        }
    }

    _algo_update_event_state(ctx, pulse_hit);

    result->baseline_raw = baseline_raw;
    result->baseline_noise = baseline_noise;
    result->threshold_raw = threshold_raw;
    result->wave_avg_raw = (int)(wave_sum / (int32_t)wave_count);
    result->peak_raw = peak_raw;
    result->peak_excess_raw = peak_excess_raw;
    result->peak_ref_raw = peak_ref_raw;
    result->peak_delta_raw = peak_delta_raw;
    result->peak_time_us = peak_idx * (uint32_t)wave_interval_us;
    result->rise_slope_adc_per_ms = rise_slope_adc_per_ms;
    result->slope_ref_adc_per_ms = slope_ref_adc_per_ms;
    result->slope_delta_adc_per_ms = slope_delta_adc_per_ms;
    result->high_hold_us = high_hold_us;
    result->area_adc_us = (uint32_t)area_acc;
    result->ref_ready = ctx->ref_ready;
    result->peak_hit = peak_hit;
    result->slope_hit = slope_hit;
    result->hold_hit = hold_hit;
    result->area_hit = area_hit;
    result->pulse_hit = pulse_hit;
    result->event_present = ctx->event_present;

    return ESP_OK;
}