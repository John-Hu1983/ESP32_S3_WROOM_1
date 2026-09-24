#include "hal_adc.h"

static adc_oneshot_unit_handle_t fd_s[SOC_ADC_PERIPH_NUM] = { 0 };
static SemaphoreHandle_t lock_s[SOC_ADC_PERIPH_NUM] = { 0 };
static bool ready_s[SOC_ADC_PERIPH_NUM][SOC_ADC_MAX_CHANNEL_NUM] = { 0 };

/*
 * brief : hal_adc_init.
 * input : see parameters.
 * output: ESP_OK on success; otherwise argument, allocation, or ADC driver error.
 * type  : public
 * theory: share one oneshot handle per ADC unit while configuring each channel independently.
 */
esp_err_t hal_adc_init(adc_unit_t unit, adc_channel_t channel) {
    adc_oneshot_unit_init_cfg_t unit_cfg = { 0 };
    adc_oneshot_chan_cfg_t channel_cfg = { 0 };
    adc_oneshot_unit_handle_t handle = NULL;
    SemaphoreHandle_t lock = NULL;
    bool new_unit = false;
    esp_err_t ret = ESP_OK;

    if (((uint32_t)unit >= SOC_ADC_PERIPH_NUM)
        || ((uint32_t)channel >= SOC_ADC_CHANNEL_NUM(unit))) {
        return ESP_ERR_INVALID_ARG;
    }

    handle = fd_s[unit];
    if (handle == NULL) {
        unit_cfg.unit_id = unit;
        ret = adc_oneshot_new_unit(&unit_cfg, &handle);
        if (ret != ESP_OK) {
            return ret;
        }
        new_unit = true;

        lock = xSemaphoreCreateMutex();
        if (lock == NULL) {
            (void)adc_oneshot_del_unit(handle);
            return ESP_ERR_NO_MEM;
        }
    }

    channel_cfg.atten = HAL_ADC_ATTENUATION;
    channel_cfg.bitwidth = HAL_ADC_BITWIDTH;
    ret = adc_oneshot_config_channel(handle, channel, &channel_cfg);
    if (ret != ESP_OK) {
        if (new_unit) {
            vSemaphoreDelete(lock);
            (void)adc_oneshot_del_unit(handle);
        }
        return ret;
    }

    if (new_unit) {
        fd_s[unit] = handle;
        lock_s[unit] = lock;
    }
    ready_s[unit][channel] = true;

    return ESP_OK;
}

/*
 * brief : hal_adc_read.
 * input : see parameters.
 * output: ESP_OK with the current raw conversion value; otherwise state or driver error.
 * type  : public
 * theory: serialize each ADC unit and synchronously wait for oneshot conversion completion.
 */
esp_err_t hal_adc_read(adc_unit_t unit, adc_channel_t channel, uint16_t* raw) {
     SemaphoreHandle_t lock = NULL;
    int adc_raw = 0;
    esp_err_t ret = ESP_OK;

    if ((raw == NULL) || ((uint32_t)unit >= SOC_ADC_PERIPH_NUM)
        || ((uint32_t)channel >= SOC_ADC_CHANNEL_NUM(unit))) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!ready_s[unit][channel]) {
        return ESP_ERR_INVALID_STATE;
    }

    lock = lock_s[unit];
    if ((lock == NULL) || (xSemaphoreTake(lock, portMAX_DELAY) != pdTRUE)) {
        return ESP_ERR_INVALID_STATE;
    }

    ret = adc_oneshot_read(fd_s[unit], channel, &adc_raw);
    xSemaphoreGive(lock);
    if (ret != ESP_OK) {
        return ret;
    }

    *raw = (uint16_t)adc_raw;
    return ESP_OK;
}