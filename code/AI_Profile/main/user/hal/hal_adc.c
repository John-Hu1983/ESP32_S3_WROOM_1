#include "hal_adc.h"

static const uint32_t s_hal_adc_approx_0db_mv = 950U;
#ifdef ADC_ATTEN_DB_2_5
static const uint32_t s_hal_adc_approx_2p5db_mv = 1250U;
#endif
static const uint32_t s_hal_adc_approx_6db_mv = 1750U;
static const uint32_t s_hal_adc_approx_11db_mv = 2450U;
#ifdef ADC_ATTEN_DB_12
static const uint32_t s_hal_adc_approx_12db_mv = 3300U;
#endif

static hal_adc_runtime_s s_hal_adc_runtime[HAL_ADC_UNIT_MAX] = { 0 };

/*
 * brief : _hal_adc_unit_to_index.
 * input : see parameters.
 * output: return value from this function.
 * type  : private
 */
static esp_err_t _hal_adc_unit_to_index(adc_unit_t unit, uint8_t* index) {
    if (index == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (unit == ADC_UNIT_1) {
        *index = 0U;
        return ESP_OK;
    }

#if SOC_ADC_PERIPH_NUM >= 2
    if (unit == ADC_UNIT_2) {
        *index = 1U;
        return ESP_OK;
    }
#endif

    return ESP_ERR_NOT_SUPPORTED;
}

/*
 * brief : _hal_adc_get_runtime.
 * input : see parameters.
 * output: return value from this function.
 * type  : private
 */
static esp_err_t _hal_adc_get_runtime(adc_unit_t unit, hal_adc_runtime_s** runtime) {
    uint8_t index = 0U;
    esp_err_t ret = _hal_adc_unit_to_index(unit, &index);

    if (ret != ESP_OK) {
        return ret;
    }
    if ((runtime == NULL) || (index >= HAL_ADC_UNIT_MAX)) {
        return ESP_ERR_INVALID_ARG;
    }

    *runtime = &s_hal_adc_runtime[index];
    return ESP_OK;
}

/*
 * brief : _hal_adc_bitwidth_to_max_raw.
 * input : see parameters.
 * output: return value from this function.
 * type  : private
 */
static uint32_t _hal_adc_bitwidth_to_max_raw(adc_bitwidth_t bitwidth) {
    switch (bitwidth) {
    case ADC_BITWIDTH_9:
        return 511U;
    case ADC_BITWIDTH_10:
        return 1023U;
    case ADC_BITWIDTH_11:
        return 2047U;
    case ADC_BITWIDTH_12:
        return 4095U;
#ifdef ADC_BITWIDTH_13
    case ADC_BITWIDTH_13:
        return 8191U;
#endif
    case ADC_BITWIDTH_DEFAULT:
    default:
        return 4095U;
    }
}

/*
 * brief : _hal_adc_atten_to_approx_full_scale_mv.
 * input : see parameters.
 * output: return value from this function.
 * type  : private
 */
static uint32_t _hal_adc_atten_to_approx_full_scale_mv(adc_atten_t atten) {
    switch (atten) {
    case ADC_ATTEN_DB_0:
        return s_hal_adc_approx_0db_mv;
#ifdef ADC_ATTEN_DB_2_5
    case ADC_ATTEN_DB_2_5:
        return s_hal_adc_approx_2p5db_mv;
#endif
    case ADC_ATTEN_DB_6:
        return s_hal_adc_approx_6db_mv;
#ifdef ADC_ATTEN_DB_11
    case ADC_ATTEN_DB_11:
        return s_hal_adc_approx_11db_mv;
#endif
#ifdef ADC_ATTEN_DB_12
    case ADC_ATTEN_DB_12:
        return s_hal_adc_approx_12db_mv;
#endif
    default:
        return s_hal_adc_approx_11db_mv;
    }
}

/*
 * brief : _hal_adc_destroy_cali.
 * input : see parameters.
 * output: none.
 * type  : private
 */
static void _hal_adc_destroy_cali(hal_adc_runtime_s* runtime) {
    if ((runtime == NULL) || (runtime->cali_handle == NULL)) {
        return;
    }

#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    (void)adc_cali_delete_scheme_curve_fitting(runtime->cali_handle);
#elif ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
    (void)adc_cali_delete_scheme_line_fitting(runtime->cali_handle);
#endif

    runtime->cali_enabled = false;
    runtime->cali_handle = NULL;
}

/*
 * brief : _hal_adc_create_cali.
 * input : see parameters.
 * output: return value from this function.
 * type  : private
 */
static esp_err_t _hal_adc_create_cali(hal_adc_runtime_s* runtime,
                                      adc_channel_t channel) {
    esp_err_t ret = ESP_ERR_NOT_SUPPORTED;

    if ((runtime == NULL) || (runtime->unit_handle == NULL)) {
        return ESP_ERR_INVALID_STATE;
    }

    _hal_adc_destroy_cali(runtime);

#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    adc_cali_curve_fitting_config_t cali_cfg = {
        .unit_id = runtime->unit,
        .chan = channel,
        .atten = runtime->atten,
        .bitwidth = runtime->bitwidth,
    };
    ret = adc_cali_create_scheme_curve_fitting(&cali_cfg, &runtime->cali_handle);
#elif ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
    adc_cali_line_fitting_config_t cali_cfg = {
        .unit_id = runtime->unit,
        .atten = runtime->atten,
        .bitwidth = runtime->bitwidth,
        .default_vref = 0,
    };
    ret = adc_cali_create_scheme_line_fitting(&cali_cfg, &runtime->cali_handle);
#endif

    if (ret == ESP_OK) {
        runtime->cali_enabled = true;
        runtime->cali_channel = channel;
    }
    else {
        runtime->cali_enabled = false;
        runtime->cali_handle = NULL;
    }

    return ret;
}

/*
 * brief : _hal_adc_init_unit_if_needed.
 * input : see parameters.
 * output: return value from this function.
 * type  : private
 */
static esp_err_t _hal_adc_init_unit_if_needed(hal_adc_runtime_s* runtime) {
    adc_oneshot_unit_init_cfg_t init_cfg = { 0 };

    if (runtime == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (runtime->inited && (runtime->unit_handle != NULL)) {
        return ESP_OK;
    }

    init_cfg.unit_id = runtime->unit;
    init_cfg.ulp_mode = ADC_ULP_MODE_DISABLE;

    return adc_oneshot_new_unit(&init_cfg, &runtime->unit_handle);
}

/*
 * brief : hal_adc_init.
 * input : cfg points to unit/channel/atten/bitwidth and calibration switch.
 * output: return value from this function.
 * type  : public
 */

esp_err_t hal_adc_init(const hal_adc_cfg_t* cfg) {
    adc_oneshot_chan_cfg_t chan_cfg = { 0 };
    hal_adc_runtime_s* runtime = NULL;
    esp_err_t ret = ESP_OK;

    if (cfg == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    ret = _hal_adc_get_runtime(cfg->unit, &runtime);
    if (ret != ESP_OK) {
        return ret;
    }

    runtime->unit = cfg->unit;
    runtime->atten = cfg->atten;
    runtime->bitwidth = cfg->bitwidth;

    ret = _hal_adc_init_unit_if_needed(runtime);
    if (ret != ESP_OK) {
        runtime->inited = false;
        runtime->unit_handle = NULL;
        return ret;
    }

    chan_cfg.atten = cfg->atten;
    chan_cfg.bitwidth = cfg->bitwidth;
    ret = adc_oneshot_config_channel(runtime->unit_handle, cfg->channel, &chan_cfg);
    if (ret != ESP_OK) {
        return ret;
    }

    runtime->inited = true;

    if (cfg->enable_cali) {
        (void)_hal_adc_create_cali(runtime, cfg->channel);
    }
    else {
        _hal_adc_destroy_cali(runtime);
    }

    return ESP_OK;
}

/*
 * brief : hal_adc_deinit.
 * input : cfg points to the ADC unit to release.
 * output: return value from this function.
 * type  : public
 */

esp_err_t hal_adc_deinit(const hal_adc_cfg_t* cfg) {
    hal_adc_runtime_s* runtime = NULL;
    esp_err_t ret = ESP_OK;

    if (cfg == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    ret = _hal_adc_get_runtime(cfg->unit, &runtime);

    if (ret != ESP_OK) {
        return ret;
    }
    if (!runtime->inited || (runtime->unit_handle == NULL)) {
        return ESP_OK;
    }

    _hal_adc_destroy_cali(runtime);

    ret = adc_oneshot_del_unit(runtime->unit_handle);
    if (ret != ESP_OK) {
        return ret;
    }

    runtime->inited = false;
    runtime->cali_enabled = false;
    runtime->unit_handle = NULL;
    runtime->cali_handle = NULL;

    return ESP_OK;
}

/*
 * brief : hal_adc_config_channel.
 * input : cfg points to target channel configuration and calibration switch.
 * output: return value from this function.
 * type  : public
 */

esp_err_t hal_adc_config_channel(const hal_adc_cfg_t* cfg) {
    adc_oneshot_chan_cfg_t chan_cfg = { 0 };
    hal_adc_runtime_s* runtime = NULL;
    esp_err_t ret = ESP_OK;

    if (cfg == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    ret = _hal_adc_get_runtime(cfg->unit, &runtime);

    if (ret != ESP_OK) {
        return ret;
    }
    if (!runtime->inited || (runtime->unit_handle == NULL)) {
        return ESP_ERR_INVALID_STATE;
    }

    chan_cfg.atten = cfg->atten;
    chan_cfg.bitwidth = cfg->bitwidth;
    ret = adc_oneshot_config_channel(runtime->unit_handle, cfg->channel, &chan_cfg);
    if (ret != ESP_OK) {
        return ret;
    }

    runtime->atten = cfg->atten;
    runtime->bitwidth = cfg->bitwidth;

    if (cfg->enable_cali) {
        (void)_hal_adc_create_cali(runtime, cfg->channel);
    }
    else {
        _hal_adc_destroy_cali(runtime);
    }

    return ESP_OK;
}

/*
 * brief : hal_adc_read_raw.
 * input : cfg points to ADC unit/channel; value returns raw sample code.
 * output: return value from this function.
 * type  : public
 */

esp_err_t hal_adc_read_raw(const hal_adc_cfg_t* cfg, int* value) {
    hal_adc_runtime_s* runtime = NULL;
    esp_err_t ret = ESP_OK;

    if ((cfg == NULL) || (value == NULL)) {
        return ESP_ERR_INVALID_ARG;
    }

    ret = _hal_adc_get_runtime(cfg->unit, &runtime);
    if (ret != ESP_OK) {
        return ret;
    }
    if (!runtime->inited || (runtime->unit_handle == NULL)) {
        return ESP_ERR_INVALID_STATE;
    }

    return adc_oneshot_read(runtime->unit_handle, cfg->channel, value);
}

/*
 * brief : hal_adc_read_mv.
 * input : cfg points to ADC unit/channel and calibration preference.
 * output: return value from this function; mv returns voltage in millivolts.
 * type  : public
 */
esp_err_t hal_adc_read_mv(const hal_adc_cfg_t* cfg, int* mv) {
    hal_adc_runtime_s* runtime = NULL;
    int raw = 0;
    uint32_t max_raw = 0U;
    uint32_t full_scale_mv = 0U;
    esp_err_t ret = ESP_OK;

    if ((cfg == NULL) || (mv == NULL)) {
        return ESP_ERR_INVALID_ARG;
    }

    ret = _hal_adc_get_runtime(cfg->unit, &runtime);
    if (ret != ESP_OK) {
        return ret;
    }
    if (!runtime->inited || (runtime->unit_handle == NULL)) {
        return ESP_ERR_INVALID_STATE;
    }

    ret = adc_oneshot_read(runtime->unit_handle, cfg->channel, &raw);
    if (ret != ESP_OK) {
        return ret;
    }

    if (cfg->enable_cali) {
        if ((runtime->cali_channel != cfg->channel) || (runtime->cali_handle == NULL)) {
            (void)_hal_adc_create_cali(runtime, cfg->channel);
        }
    }
    else {
        _hal_adc_destroy_cali(runtime);
    }

    if (runtime->cali_enabled && (runtime->cali_handle != NULL)) {
        ret = adc_cali_raw_to_voltage(runtime->cali_handle, raw, mv);
        if (ret == ESP_OK) {
            return ESP_OK;
        }

        _hal_adc_destroy_cali(runtime);
    }

    max_raw = _hal_adc_bitwidth_to_max_raw(runtime->bitwidth);
    full_scale_mv = _hal_adc_atten_to_approx_full_scale_mv(runtime->atten);
    if (max_raw == 0U) {
        return ESP_ERR_INVALID_STATE;
    }

    *mv = (int)((((uint32_t)raw) * full_scale_mv) / max_raw);
    return ESP_OK;
}

/*
 * brief : hal_adc_is_ready.
 * input : unit selects ADC runtime slot to query.
 * output: true when the ADC unit has been initialized.
 * type  : public
 */

bool hal_adc_is_ready(adc_unit_t unit) {
    hal_adc_runtime_s* runtime = NULL;
    esp_err_t ret = _hal_adc_get_runtime(unit, &runtime);

    if (ret != ESP_OK) {
        return false;
    }

    return runtime->inited && (runtime->unit_handle != NULL);
}
