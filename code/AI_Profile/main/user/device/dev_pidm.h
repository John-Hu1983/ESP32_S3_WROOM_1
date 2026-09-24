#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "user/device/dev_gpba02b.h"
#include "user/hal/hal_adc.h"
#include "user/inc/bsp_config.h"

#ifdef __cplusplus
extern "C" {
#endif

// clang-format off
#define PIDM_TRIGGER_PULSE_US                 (100U)
#define PIDM_RESPONSE_SETTLE_US               (200U)
#define PIDM_BASELINE_SAMPLE_COUNT            (12U)
#define PIDM_BASELINE_INTERVAL_US             (20U)
#define PIDM_WAVE_SAMPLE_COUNT                (60U)
#define PIDM_WAVE_INTERVAL_US                 (20U)
#define PIDM_THRESHOLD_MIN_RISE               (40U)
#define PIDM_THRESHOLD_NOISE_GAIN_Q4          (20U)
#define PIDM_HIGH_HOLD_MIN_US                 (220U)
#define PIDM_AREA_MIN_ADC_US                  (22000U)
#define PIDM_PEAK_DELTA_MIN                   (90U)
#define PIDM_SLOPE_DELTA_MIN_ADC_PER_MS       (1200U)
#define PIDM_REFERENCE_LEARN_PULSES           (10U)
#define PIDM_REFERENCE_EMA_SHIFT              (4U)
#define PIDM_DETECT_ASSERT_COUNT              (2U)
#define PIDM_DETECT_RELEASE_COUNT             (4U)
#define PIDM_DETECTION_PERIOD_MS               (300U)
#define PIDM_DETECTION_TASK_STACK_SIZE         (4096U)
#define PIDM_DETECTION_TASK_PRIORITY           (5U)
// clang-format on

typedef struct {
	bool ready;
	bool adc_valid;
	bool calibrated;
	bool metal_detected;
	uint32_t adc_latest;
	uint32_t adc_average;
	uint32_t adc_sample_count;
	uint32_t trigger_count;
	uint32_t pulse_width_us;
	uint32_t baseline_raw;
	uint32_t baseline_noise;
	uint32_t threshold_raw;
	uint32_t peak_raw;
	uint32_t peak_excess_raw;
	uint32_t peak_reference_raw;
	uint32_t peak_delta_raw;
	uint32_t peak_time_us;
	uint32_t response_slope;
	uint32_t baseline_slope;
	uint32_t threshold_slope;
	uint32_t slope_delta;
	uint32_t high_hold_us;
	uint32_t area_adc_us;
	uint16_t calibration_count;
	uint8_t detect_hits;
	uint8_t release_hits;
	bool peak_hit;
	bool slope_hit;
	bool hold_hit;
	bool area_hit;
	bool pulse_hit;
	esp_err_t trigger_error;
} pidm_profile_s;

esp_err_t pidm_init_runtime(void);
esp_err_t pidm_deinit_runtime(void);
esp_err_t pidm_trigger_detection(void);
esp_err_t pidm_read_profile(pidm_profile_s* profile);

#ifdef __cplusplus
}
#endif

