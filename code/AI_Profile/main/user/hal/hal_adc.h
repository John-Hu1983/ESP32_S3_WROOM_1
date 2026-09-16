#pragma once

#include <stdbool.h>
#include <stdint.h>

#include <esp_adc/adc_cali.h>
#include <esp_adc/adc_cali_scheme.h>
#include <esp_adc/adc_oneshot.h>
#include <esp_err.h>
#include <soc/soc_caps.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    HAL_ADC_UNIT_MAX = 2,
};

typedef struct {
    adc_unit_t unit;
    adc_channel_t channel;
    adc_atten_t atten;
    adc_bitwidth_t bitwidth;
    bool enable_cali;
} hal_adc_cfg_t;

typedef struct {
    bool inited;
    bool cali_enabled;
    adc_unit_t unit;
    adc_channel_t cali_channel;
    adc_atten_t atten;
    adc_bitwidth_t bitwidth;
    adc_oneshot_unit_handle_t unit_handle;
    adc_cali_handle_t cali_handle;
} hal_adc_runtime_s;

esp_err_t hal_adc_init(const hal_adc_cfg_t* cfg);

esp_err_t hal_adc_deinit(const hal_adc_cfg_t* cfg);

esp_err_t hal_adc_config_channel(const hal_adc_cfg_t* cfg);

esp_err_t hal_adc_read_raw(const hal_adc_cfg_t* cfg, int* value);

esp_err_t hal_adc_read_mv(const hal_adc_cfg_t* cfg, int* mv);

bool hal_adc_is_ready(adc_unit_t unit);

#ifdef __cplusplus
}
#endif
