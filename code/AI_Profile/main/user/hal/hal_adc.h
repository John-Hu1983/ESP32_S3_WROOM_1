#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "soc/soc_caps.h"

#ifdef __cplusplus
extern "C" {
#endif

// clang-format off
#define HAL_ADC_ATTENUATION  (ADC_ATTEN_DB_12)
#define HAL_ADC_BITWIDTH     (ADC_BITWIDTH_12)
// clang-format on

esp_err_t hal_adc_init(adc_unit_t unit, adc_channel_t channel);
esp_err_t hal_adc_read(adc_unit_t unit, adc_channel_t channel, uint16_t* raw);
esp_err_t hal_adc_raw_to_mv(adc_unit_t unit, adc_channel_t channel, uint16_t raw, uint16_t* mv);

#ifdef __cplusplus
}
#endif