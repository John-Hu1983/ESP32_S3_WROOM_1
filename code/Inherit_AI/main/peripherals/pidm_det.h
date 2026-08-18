#pragma once

#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "bsp/gpba02b.h"
#include "driver/gpio.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "active_board_config.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PIDM_DET_ADC_ATTEN (ADC_ATTEN_DB_12)
#define PIDM_DET_ADC_BITWIDTH (ADC_BITWIDTH_DEFAULT)

#define PIDM_DET_BASELINE_SAMPLE_MAX 32U
#define PIDM_DET_WAVE_SAMPLE_MAX 96U

#define PIDM_DET_BASELINE_SAMPLES_DEFAULT 12U
#define PIDM_DET_BASELINE_INTERVAL_US_DEFAULT 20U
#define PIDM_DET_WAVE_SAMPLES_DEFAULT 60U
#define PIDM_DET_WAVE_INTERVAL_US_DEFAULT 20U
#define PIDM_DET_PULSE_US_DEFAULT 100U
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

typedef struct {
    bool ready;
    bool enabled;
    adc_unit_t adc_unit;
    adc_channel_t adc_channel;
} pidm_det_info_s;

typedef struct {
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

typedef struct {
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

esp_err_t pidm_det_init(void);
esp_err_t pidm_det_deinit(void);
bool pidm_det_is_ready(void);
esp_err_t pidm_det_set_enable(bool enable);
esp_err_t pidm_det_set_pulse_level(bool high);
esp_err_t pidm_det_pulse_us(uint32_t pulse_us);
esp_err_t pidm_det_read_raw(int* raw_value);
void pidm_det_feature_cfg_load_default(pidm_det_feature_cfg_s* cfg);
esp_err_t pidm_det_feature_cfg_set(const pidm_det_feature_cfg_s* cfg);
pidm_det_feature_cfg_s pidm_det_feature_cfg_get(void);
esp_err_t pidm_det_probe_feature(uint32_t pulse_us, pidm_det_feature_s* feature);
bool pidm_det_is_metal_present(void);

#ifdef __cplusplus
}
#endif
