#include "dev_bat_vol.h"

#define TAG "dev_bat_vol"

static bool s_bat_adc_ready = false;

/*
 * brief : batvol_init_cfg.
 * input : none.
 * output: return value from this function.
 * type  : public
 * theory: configure the battery channel for synchronous oneshot conversion.
 */
esp_err_t batvol_init_cfg(void) {
    esp_err_t ret = ESP_OK;

    if (s_bat_adc_ready) {
        return ESP_OK;
    }

    ret = hal_adc_init(SERVO_ADC_UNIT, SERVO_ADC_CHANNEL);
    if (ret != ESP_OK) {
        return ret;
    }

    s_bat_adc_ready = true;

    return ESP_OK;
}

/*
 * brief : batvol_read_mv.
 * input : see parameters.
 * output: return value from this function.
 * type  : public
 * theory: average current oneshot samples, then scale the divider input to battery voltage.
 */
esp_err_t batvol_read_mv(uint16_t up_r, uint16_t low_r, uint16_t* mv) {
     uint16_t raw_value = 0U;
     uint32_t raw_sum = 0U;
     uint32_t divider_sum = 0U;
     uint32_t i = 0U;
    esp_err_t ret = ESP_OK;
    int raw = 0;
    int adc_mv = 0;
    uint32_t io_mv_u32 = 0U;
    uint32_t bat_mv_u32 = 0U;

    if (!s_bat_adc_ready) {
        return ESP_ERR_INVALID_STATE;
    }

    for (i = 0U; i < BATVOL_ADC_SAMPLE_COUNT; ++i) {
        ret = hal_adc_read(SERVO_ADC_UNIT, SERVO_ADC_CHANNEL, &raw_value);
        if (ret != ESP_OK) {
            return ret;
        }
        raw_sum += raw_value;
    }

    raw = (int)(raw_sum / BATVOL_ADC_SAMPLE_COUNT);
    adc_mv = (int)((((uint32_t)raw) * BATVOL_ADC_FALLBACK_FULL_SCALE_MV)
                   / BATVOL_ADC_FALLBACK_MAX_RAW);

    if (adc_mv < 0) {
        adc_mv = 0;
    }
    io_mv_u32 = (uint32_t)adc_mv;

    divider_sum = (uint32_t)up_r + (uint32_t)low_r;
    bat_mv_u32 = ((io_mv_u32 * divider_sum) + ((uint32_t)low_r / 2U)) / (uint32_t)low_r;

    if (bat_mv_u32 > UINT16_MAX) {
        bat_mv_u32 = UINT16_MAX;
    }

    *mv = (uint16_t)bat_mv_u32;

    return ESP_OK;
}
