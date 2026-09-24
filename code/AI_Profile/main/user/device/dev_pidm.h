#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"

#include "user/device/dev_gpba02b.h"
#include "user/hal/hal_adc.h"
#include "user/inc/bsp_config.h"

#ifdef __cplusplus
extern "C" {
#endif

// clang-format off
#define PIDM_TRIGGER_PULSE_US              (100U)
#define PIDM_RESPONSE_START_US             (200U)
#define PIDM_RESPONSE_END_US               (1000U)
#define PIDM_ADC_SAMPLE_COUNT              (8U)
#define PIDM_DETECT_CALIBRATION_COUNT      (32U)
#define PIDM_DETECT_BASELINE_FILTER_SHIFT  (5U)
#define PIDM_DETECT_NOISE_FILTER_SHIFT     (4U)
#define PIDM_DETECT_NOISE_MULTIPLIER       (4U)
#define PIDM_DETECT_MIN_MARGIN             (20U)
#define PIDM_DETECT_ASSERT_COUNT           (3U)
#define PIDM_DETECT_RELEASE_COUNT          (5U)
#define PIDM_DETECT_RELEASE_HYST_PERCENT   (50U)
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
	uint32_t response_slope;
	uint32_t baseline_slope;
	uint32_t threshold_slope;
	uint16_t calibration_count;
	uint8_t detect_hits;
	esp_err_t trigger_error;
} pidm_profile_s;

esp_err_t pidm_init_runtime(void);
esp_err_t pidm_deinit_runtime(void);
esp_err_t pidm_trigger_detection(void);
esp_err_t pidm_read_profile(pidm_profile_s* profile);

#ifdef __cplusplus
}
#endif

