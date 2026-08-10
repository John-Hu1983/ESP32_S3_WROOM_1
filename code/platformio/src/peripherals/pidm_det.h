#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_rom_sys.h"

#include "bsp/delay.h"
#include "peripherals/gpba02b.h"
#include "user_config.h"

#define PIDM_DET_ADC_ATTEN (ADC_ATTEN_DB_12)
#define PIDM_DET_ADC_BITWIDTH (ADC_BITWIDTH_DEFAULT)

#define PIDM_DET_BASELINE_SAMPLE_MAX 32U
#define PIDM_DET_WAVE_SAMPLE_MAX 96U

#define PIDM_DET_BASELINE_SAMPLES_DEFAULT 12U
#define PIDM_DET_BASELINE_INTERVAL_US_DEFAULT 20U
#define PIDM_DET_WAVE_SAMPLES_DEFAULT 60U
#define PIDM_DET_WAVE_INTERVAL_US_DEFAULT 20U
#define PIDM_DET_SETTLE_US_DEFAULT 0U
#define PIDM_DET_THRESHOLD_MIN_RISE_DEFAULT 40U
#define PIDM_DET_THRESHOLD_NOISE_GAIN_Q4_DEFAULT 20U
#define PIDM_DET_SLOPE_MIN_ADC_PER_MS_DEFAULT 450U
#define PIDM_DET_HIGH_HOLD_MIN_US_DEFAULT 220U
#define PIDM_DET_AREA_MIN_ADC_US_DEFAULT 22000U
#define PIDM_DET_PEAK_DELTA_MIN_DEFAULT 90U
#define PIDM_DET_SLOPE_DELTA_MIN_ADC_PER_MS_DEFAULT 1200U
#define PIDM_DET_REF_LEARN_PULSES_DEFAULT 10U
#define PIDM_DET_REF_EMA_SHIFT_DEFAULT 4U
#define PIDM_DET_ASSERT_COUNT_DEFAULT 2U
#define PIDM_DET_RELEASE_COUNT_DEFAULT 4U

typedef struct
{
    bool ready;
    bool enabled;
    adc_unit_t adc_unit;
    adc_channel_t adc_channel;
} pidm_det_info_s;

typedef struct
{
    uint8_t baseline_samples;
    uint16_t baseline_interval_us;
    uint8_t wave_samples;
    uint16_t wave_interval_us;
    uint32_t settle_us;
    uint16_t threshold_min_rise;
    uint8_t threshold_noise_gain_q4;
    uint16_t slope_min_adc_per_ms;
    uint16_t high_hold_min_us;
    uint32_t area_min_adc_us;
    uint16_t peak_delta_min;
    uint16_t slope_delta_min_adc_per_ms;
    uint8_t ref_learn_pulses;
    uint8_t ref_ema_shift;
    uint8_t assert_count;
    uint8_t release_count;
} pidm_det_feature_cfg_s;

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
    bool metal_present;
} pidm_det_feature_s;

/* Initialize PIDM detector backend: EN pin, pulse GPIO, and ADC oneshot channel. */
esp_err_t pidm_det_init(void);
/* Deinitialize PIDM detector backend and release ADC resources. */
esp_err_t pidm_det_deinit(void);
/* Query whether PIDM detector backend is initialized. */
bool pidm_det_is_ready(void);
/* Enable or disable PIDM detector analog frontend power. */
esp_err_t pidm_det_set_enable(bool enable);
/* Set PIDM pulse output level directly. */
esp_err_t pidm_det_set_pulse_level(bool high);
/* Generate one active-high pulse with specified width in microseconds. */
esp_err_t pidm_det_pulse_us(uint32_t pulse_us);
/* Read one raw ADC sample from PIDM detector output node. */
esp_err_t pidm_det_read_raw(int *raw_value);
/* Read averaged raw ADC value over N samples with optional inter-sample delay. */
esp_err_t pidm_det_read_average(uint8_t sample_count,
                                uint16_t sample_interval_ms,
                                int *raw_avg);
/* Fill one feature-configuration object with default values. */
void pidm_det_feature_cfg_load_default(pidm_det_feature_cfg_s *cfg);
/* Apply feature extraction and detection configuration. */
esp_err_t pidm_det_feature_cfg_set(const pidm_det_feature_cfg_s *cfg);
/* Get current feature extraction and detection configuration. */
pidm_det_feature_cfg_s pidm_det_feature_cfg_get(void);
/* Probe one pulse and extract edge/hold/area features with state-machine result. */
esp_err_t pidm_det_probe_feature(uint32_t pulse_us, pidm_det_feature_s *feature);
/* Read current debounced metal-present state from detector state machine. */
bool pidm_det_is_metal_present(void);
/* Generate one pulse, wait settle time (microseconds), then read averaged detector value. */
esp_err_t pidm_det_probe_once(uint32_t pulse_us,
                              uint32_t settle_us,
                              uint8_t sample_count,
                              int *raw_avg);
/* Read current PIDM detector runtime status snapshot. */
pidm_det_info_s pidm_det_read_info(void);
