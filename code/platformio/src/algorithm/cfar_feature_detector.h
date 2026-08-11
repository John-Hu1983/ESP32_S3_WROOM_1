#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

/*
 * Producer-side windows are passed by pointer. The algorithm never touches hardware,
 * it only consumes numeric samples and updates a state machine.
 */
#define SIGNAL_EVENT_ALGO_BASELINE_SAMPLE_MAX 32U
#define SIGNAL_EVENT_ALGO_WAVE_SAMPLE_MAX 96U

#define SIGNAL_EVENT_ALGO_BASELINE_SAMPLES_DEFAULT 12U
#define SIGNAL_EVENT_ALGO_WAVE_SAMPLES_DEFAULT 60U
#define SIGNAL_EVENT_ALGO_WAVE_INTERVAL_US_DEFAULT 20U

#define SIGNAL_EVENT_ALGO_THRESHOLD_MIN_RISE_DEFAULT 40U
#define SIGNAL_EVENT_ALGO_THRESHOLD_NOISE_GAIN_Q4_DEFAULT 20U
#define SIGNAL_EVENT_ALGO_HIGH_HOLD_MIN_US_DEFAULT 220U
#define SIGNAL_EVENT_ALGO_AREA_MIN_ADC_US_DEFAULT 22000U
#define SIGNAL_EVENT_ALGO_PEAK_DELTA_MIN_DEFAULT 90U
#define SIGNAL_EVENT_ALGO_SLOPE_DELTA_MIN_ADC_PER_MS_DEFAULT 1200U

#define SIGNAL_EVENT_ALGO_REF_LEARN_FRAMES_DEFAULT 10U
#define SIGNAL_EVENT_ALGO_REF_EMA_SHIFT_DEFAULT 4U
#define SIGNAL_EVENT_ALGO_ASSERT_COUNT_DEFAULT 2U
#define SIGNAL_EVENT_ALGO_RELEASE_COUNT_DEFAULT 4U

typedef struct
{
    uint8_t baseline_samples;
    uint8_t wave_samples;
    uint16_t wave_interval_us;
    uint16_t threshold_min_rise;
    uint8_t threshold_noise_gain_q4;
    uint16_t high_hold_min_us;
    uint32_t area_min_adc_us;
    uint16_t peak_delta_min;
    uint16_t slope_delta_min_adc_per_ms;
    uint8_t ref_learn_frames;
    uint8_t ref_ema_shift;
    uint8_t assert_count;
    uint8_t release_count;
} signal_event_algo_cfg_s;

typedef struct
{
    const int *baseline_data;
    uint8_t baseline_count;
    const int *wave_data;
    uint8_t wave_count;
    uint16_t wave_interval_us;
} signal_event_algo_frame_s;

typedef struct
{
    int baseline_raw;
    int baseline_noise;
    int threshold_raw;
    int wave_avg_raw;
    int peak_raw;
    int peak_excess_raw;
    int peak_ref_raw;
    int peak_delta_raw;
    uint32_t peak_time_us;
    uint32_t rise_slope_adc_per_ms;
    uint32_t slope_ref_adc_per_ms;
    uint32_t slope_delta_adc_per_ms;
    uint32_t high_hold_us;
    uint32_t area_adc_us;
    bool ref_ready;
    bool peak_hit;
    bool slope_hit;
    bool hold_hit;
    bool area_hit;
    bool pulse_hit;
    bool event_present;
} signal_event_algo_result_s;

typedef struct
{
    signal_event_algo_cfg_s cfg;
    bool event_present;
    uint8_t assert_streak;
    uint8_t release_streak;
    bool ref_ready;
    uint16_t ref_count;
    int32_t ref_peak_excess_q8;
    int32_t ref_slope_q8;
} signal_event_algo_ctx_s;

/* Fill one algorithm configuration with defaults. */
void signal_event_algo_load_default_cfg(signal_event_algo_cfg_s *cfg);
/* Check whether one algorithm configuration is valid. */
bool signal_event_algo_cfg_is_valid(const signal_event_algo_cfg_s *cfg);
/* Initialize context with one valid configuration. */
esp_err_t signal_event_algo_init(signal_event_algo_ctx_s *ctx,
                                 const signal_event_algo_cfg_s *cfg);
/* Apply a new configuration and reset state machine/reference learning. */
esp_err_t signal_event_algo_set_cfg(signal_event_algo_ctx_s *ctx,
                                    const signal_event_algo_cfg_s *cfg);
/* Reset event state and learned reference while keeping configuration. */
void signal_event_algo_reset_state(signal_event_algo_ctx_s *ctx);
/* Read current debounced event-present state. */
bool signal_event_algo_is_event_present(const signal_event_algo_ctx_s *ctx);

/*
 * Core interface:
 * Producer provides sample pointers through frame->baseline_data / frame->wave_data.
 * Algorithm computes slope, dpk-like deltas, and final debounced event state.
 */
esp_err_t signal_event_algo_process(signal_event_algo_ctx_s *ctx,
                                    const signal_event_algo_frame_s *frame,
                                    signal_event_algo_result_s *result);