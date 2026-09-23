#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_adc/adc_continuous.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "soc/soc_caps.h"

#ifdef __cplusplus
extern "C" {
#endif

// clang-format off
#define HAL_ADC_SAMPLE_FREQ_HZ      (20000U)
#define HAL_ADC_FRAME_SAMPLES       (16U)
#define HAL_ADC_AVG_SAMPLES         (8U)
#define HAL_ADC_TASK_STACK_SIZE     (2048U)
#define HAL_ADC_TASK_PRIORITY       (5U)
#define HAL_ADC_LOCK_TIMEOUT_MS     (100U)
// clang-format on

typedef struct hal_adc_link {
	adc_unit_t unit;
	adc_channel_t channel;
	adc_atten_t atten;
	adc_bitwidth_t bitwidth;
	bool enable_cali;
	uint16_t cache[HAL_ADC_AVG_SAMPLES];
	uint8_t cache_head;
	uint8_t cache_count;
	struct hal_adc_link* next;
} hal_adc_link_t;

typedef struct {
	bool valid;
	const uint16_t* raw_cache;
	uint32_t raw_latest;
	uint32_t raw_avg;
	uint32_t sample_count;
} hal_adc_sample_s;

esp_err_t hal_adc_insert(const hal_adc_link_t* cfg);
esp_err_t hal_adc_get_channel_sample(adc_unit_t unit,
									 adc_channel_t channel,
									 uint32_t avg_samples,
									 hal_adc_sample_s* sample);
esp_err_t hal_adc_deinit(const hal_adc_link_t* cfg);

#ifdef __cplusplus
}
#endif
