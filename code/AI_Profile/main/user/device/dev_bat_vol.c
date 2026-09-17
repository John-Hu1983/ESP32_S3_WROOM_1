#include "dev_bat_vol.h"

#define TAG "dev_bat_vol"

static hal_adc_cfg_t* bat_cfg = NULL;
static adc_oneshot_unit_handle_t s_bat_adc_unit_handle = NULL;
static adc_cali_handle_t s_bat_adc_cali_handle = NULL;
static adc_unit_t s_bat_adc_unit = SERVO_ADC_UNIT;
static adc_channel_t s_bat_adc_channel = SERVO_ADC_CHANNEL;
static bool s_bat_adc_ready = false;
static bool s_bat_adc_cali_enabled = false;
#if BATVOL_DIAG_ENABLE
static uint32_t s_batvol_diag_read_count = 0U;
#endif

/*
 * brief : _batvol_alloc_cfg.
 * input : see parameters.
 * output: return value from this function.
 * type  : private
 */
static void* _batvol_alloc_cfg(size_t size) {
    void* ptr = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (ptr == NULL) {
        ptr = heap_caps_malloc(size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    return ptr;
}

/*
 * brief : _batvol_diag_pre_discharge.
 * input : none.
 * output: none.
 * type  : private
 */
static void _batvol_diag_pre_discharge(void) {
#if BATVOL_DIAG_PRE_DISCHARGE_ENABLE
    (void)gpio_set_direction(SERVO_ADC_IO, GPIO_MODE_INPUT);
    (void)gpio_set_pull_mode(SERVO_ADC_IO, GPIO_PULLDOWN_ONLY);

    if (rtc_gpio_is_valid_gpio(SERVO_ADC_IO)) {
        (void)rtc_gpio_pullup_dis(SERVO_ADC_IO);
        (void)rtc_gpio_pulldown_en(SERVO_ADC_IO);
    }

    if (BATVOL_DIAG_PRE_DISCHARGE_US > 0U) {
        ets_delay_us(BATVOL_DIAG_PRE_DISCHARGE_US);
    }

    (void)gpio_set_pull_mode(SERVO_ADC_IO, GPIO_FLOATING);
    if (rtc_gpio_is_valid_gpio(SERVO_ADC_IO)) {
        (void)rtc_gpio_pullup_dis(SERVO_ADC_IO);
        (void)rtc_gpio_pulldown_dis(SERVO_ADC_IO);
    }

    if (BATVOL_DIAG_POST_DISCHARGE_US > 0U) {
        ets_delay_us(BATVOL_DIAG_POST_DISCHARGE_US);
    }
#endif
}

/*
 * brief : _batvol_deinit_adc_cali.
 * input : none.
 * output: none.
 * type  : private
 */
static void _batvol_deinit_adc_cali(void) {
    if (s_bat_adc_cali_handle == NULL) {
        s_bat_adc_cali_enabled = false;
        return;
    }

#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    (void)adc_cali_delete_scheme_curve_fitting(s_bat_adc_cali_handle);
#elif ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
    (void)adc_cali_delete_scheme_line_fitting(s_bat_adc_cali_handle);
#endif

    s_bat_adc_cali_handle = NULL;
    s_bat_adc_cali_enabled = false;
}

/*
 * brief : _batvol_init_adc_cali.
 * input : none.
 * output: return value from this function.
 * type  : private
 */
static esp_err_t _batvol_init_adc_cali(void) {
    esp_err_t ret = ESP_ERR_NOT_SUPPORTED;

    _batvol_deinit_adc_cali();

#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    adc_cali_curve_fitting_config_t cali_cfg = {
        .unit_id = s_bat_adc_unit,
        .chan = s_bat_adc_channel,
        .atten = bat_cfg->atten,
        .bitwidth = bat_cfg->bitwidth,
    };
    ret = adc_cali_create_scheme_curve_fitting(&cali_cfg, &s_bat_adc_cali_handle);
#elif ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
    adc_cali_line_fitting_config_t cali_cfg = {
        .unit_id = s_bat_adc_unit,
        .atten = bat_cfg->atten,
        .bitwidth = bat_cfg->bitwidth,
        .default_vref = 0,
    };
    ret = adc_cali_create_scheme_line_fitting(&cali_cfg, &s_bat_adc_cali_handle);
#endif

    if (ret == ESP_OK) {
        s_bat_adc_cali_enabled = true;
    }
    else {
        s_bat_adc_cali_enabled = false;
        s_bat_adc_cali_handle = NULL;
    }

    return ret;
}

/*
 * brief : batvol_init_cfg.
 * input : none.
 * output: return value from this function.
 * type  : public
 */
esp_err_t batvol_init_cfg(void) {
    esp_err_t ret = ESP_OK;
    adc_unit_t detected_unit = SERVO_ADC_UNIT;
    adc_channel_t detected_channel = SERVO_ADC_CHANNEL;
    adc_oneshot_unit_init_cfg_t init_cfg = {
        .unit_id = SERVO_ADC_UNIT,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    adc_oneshot_chan_cfg_t chan_cfg = {
        .bitwidth = SERVO_ADC_BITWIDTH,
        .atten = SERVO_ADC_ATTENUATION,
    };
    bool cfg_new = false;

    if (bat_cfg == NULL) {
        gpio_config_t io_cfg = {
            .pin_bit_mask = (1ULL << SERVO_ADC_IO),
            .mode = GPIO_MODE_INPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };

        bat_cfg = (hal_adc_cfg_t*)_batvol_alloc_cfg(sizeof(hal_adc_cfg_t));
        if (bat_cfg == NULL) {
            return ESP_ERR_NO_MEM;
        }
        cfg_new = true;

        (void)gpio_config(&io_cfg);
        (void)gpio_set_pull_mode(SERVO_ADC_IO, GPIO_FLOATING);
        (void)gpio_hold_dis(SERVO_ADC_IO);
        if (rtc_gpio_is_valid_gpio(SERVO_ADC_IO)) {
            (void)rtc_gpio_deinit(SERVO_ADC_IO);
            (void)rtc_gpio_pullup_dis(SERVO_ADC_IO);
            (void)rtc_gpio_pulldown_dis(SERVO_ADC_IO);
            (void)rtc_gpio_hold_dis(SERVO_ADC_IO);
        }

        bat_cfg->unit = SERVO_ADC_UNIT;
        bat_cfg->channel = SERVO_ADC_CHANNEL;
        bat_cfg->atten = SERVO_ADC_ATTENUATION;
        bat_cfg->bitwidth = SERVO_ADC_BITWIDTH;
        bat_cfg->enable_cali = true;
    }

    if (s_bat_adc_ready && (s_bat_adc_unit_handle != NULL)) {
        return ESP_OK;
    }

    ret = adc_oneshot_io_to_channel(SERVO_ADC_IO, &detected_unit, &detected_channel);
    if (ret == ESP_OK) {
        s_bat_adc_unit = detected_unit;
        s_bat_adc_channel = detected_channel;
        bat_cfg->unit = detected_unit;
        bat_cfg->channel = detected_channel;
    }
    else {
        s_bat_adc_unit = bat_cfg->unit;
        s_bat_adc_channel = bat_cfg->channel;
    }

    init_cfg.unit_id = s_bat_adc_unit;

    ret = adc_oneshot_new_unit(&init_cfg, &s_bat_adc_unit_handle);
    if (ret != ESP_OK) {
        if (cfg_new) {
            heap_caps_free(bat_cfg);
            bat_cfg = NULL;
        }
        return ret;
    }

    chan_cfg.bitwidth = bat_cfg->bitwidth;
    chan_cfg.atten = bat_cfg->atten;
    ret =
        adc_oneshot_config_channel(s_bat_adc_unit_handle, s_bat_adc_channel, &chan_cfg);
    if (ret != ESP_OK) {
        (void)adc_oneshot_del_unit(s_bat_adc_unit_handle);
        s_bat_adc_unit_handle = NULL;

        if (cfg_new) {
            heap_caps_free(bat_cfg);
            bat_cfg = NULL;
        }
        return ret;
    }

    if (bat_cfg->enable_cali) {
        (void)_batvol_init_adc_cali();
    }
    else {
        _batvol_deinit_adc_cali();
    }

    s_bat_adc_ready = true;

#if BATVOL_DIAG_ENABLE
    ESP_LOGI(TAG,
             "diag init io=%d unit=%d ch=%d atten=%d bitwidth=%d cali=%s",
             (int)SERVO_ADC_IO,
             (int)s_bat_adc_unit,
             (int)s_bat_adc_channel,
             (int)bat_cfg->atten,
             (int)bat_cfg->bitwidth,
             s_bat_adc_cali_enabled ? "on" : "off");
#endif

    return ESP_OK;
}

/*
 * brief : batvol_deinit_cfg.
 * input : none.
 * output: return value from this function.
 * type  : public
 */
esp_err_t batvol_deinit_cfg(void) {
    esp_err_t ret = ESP_OK;

    if ((bat_cfg == NULL) && (s_bat_adc_unit_handle == NULL)) {
        return ESP_OK;
    }

    if (s_bat_adc_unit_handle != NULL) {
        _batvol_deinit_adc_cali();

        ret = adc_oneshot_del_unit(s_bat_adc_unit_handle);
        if (ret != ESP_OK) {
            return ret;
        }

        s_bat_adc_unit_handle = NULL;
    }

    s_bat_adc_ready = false;
    s_bat_adc_unit = SERVO_ADC_UNIT;
    s_bat_adc_channel = SERVO_ADC_CHANNEL;

    if (bat_cfg != NULL) {
        heap_caps_free(bat_cfg);
        bat_cfg = NULL;
    }

    return ret;
}

esp_err_t batvol_read_iovol(uint16_t* io_vol) {
    esp_err_t ret = ESP_OK;
    int raw = 0;
    int adc_mv = 0;
    uint32_t i = 0U;
    uint32_t sample_sum_raw = 0U;
    uint32_t sample_ok_cnt = 0U;
    uint32_t total_samples = BATVOL_ADC_DISCARD_COUNT + BATVOL_ADC_SAMPLE_COUNT;
    uint32_t adc_mv_u32 = 0U;
#if BATVOL_DIAG_ENABLE
    uint32_t raw_avg_u32 = 0U;
#endif

    if (io_vol == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (bat_cfg == NULL) {
        ret = batvol_init_cfg();
        if (ret != ESP_OK) {
            return ret;
        }
    }
    if (!s_bat_adc_ready || (s_bat_adc_unit_handle == NULL)) {
        return ESP_ERR_INVALID_STATE;
    }

    _batvol_diag_pre_discharge();

    if (total_samples == 0U) {
        total_samples = 1U;
    }

    for (i = 0U; i < total_samples; i++) {
        ret = adc_oneshot_read(s_bat_adc_unit_handle, s_bat_adc_channel, &raw);
        if (ret != ESP_OK) {
            continue;
        }

        if (i >= BATVOL_ADC_DISCARD_COUNT) {
            sample_sum_raw += (uint32_t)raw;
            sample_ok_cnt++;
        }

        if (BATVOL_ADC_SAMPLE_DELAY_US > 0U) {
            ets_delay_us(BATVOL_ADC_SAMPLE_DELAY_US);
        }
    }

    if (sample_ok_cnt == 0U) {
        return ESP_FAIL;
    }

    raw = (int)(sample_sum_raw / sample_ok_cnt);
#if BATVOL_DIAG_ENABLE
    raw_avg_u32 = sample_sum_raw / sample_ok_cnt;
#endif

    if (bat_cfg->enable_cali && s_bat_adc_cali_enabled
        && (s_bat_adc_cali_handle != NULL)) {
        ret = adc_cali_raw_to_voltage(s_bat_adc_cali_handle, raw, &adc_mv);
        if (ret != ESP_OK) {
            _batvol_deinit_adc_cali();
        }
    }

    if (!s_bat_adc_cali_enabled || (s_bat_adc_cali_handle == NULL)) {
        adc_mv = (int)((((uint32_t)raw) * BATVOL_ADC_FALLBACK_FULL_SCALE_MV)
                       / BATVOL_ADC_FALLBACK_MAX_RAW);
    }

    if (adc_mv < 0) {
        adc_mv = 0;
    }

    adc_mv_u32 = (uint32_t)adc_mv;
    if (adc_mv_u32 > UINT16_MAX) {
        adc_mv_u32 = UINT16_MAX;
    }

    *io_vol = (uint16_t)adc_mv_u32;

#if BATVOL_DIAG_ENABLE
    s_batvol_diag_read_count++;
    if ((BATVOL_DIAG_EVERY_N_READS > 0U)
        && ((s_batvol_diag_read_count % BATVOL_DIAG_EVERY_N_READS) == 0U)) {
        ESP_LOGI(
            TAG,
            "diag io_num=%d io_lv=%d unit=%d ch=%d raw_avg=%lu n=%lu io_mv=%u cali=%s",
            (int)SERVO_ADC_IO,
            gpio_get_level(SERVO_ADC_IO),
            (int)s_bat_adc_unit,
            (int)s_bat_adc_channel,
            (unsigned long)raw_avg_u32,
            (unsigned long)sample_ok_cnt,
            (unsigned)*io_vol,
            s_bat_adc_cali_enabled ? "on" : "off");
    }
#endif

    return ESP_OK;
}

/*
 * brief : batvol_read_mv.
 * input : see parameters.
 * output: return value from this function.
 * type  : public
 */
esp_err_t batvol_read_mv(uint16_t up_r, uint16_t low_r, uint16_t* mv) {
    esp_err_t ret = ESP_OK;
    uint16_t io_mv = 0U;
    uint32_t bat_mv_u32 = 0U;
#if !BATVOL_DIAG_DIRECT_ADC_MV_OUTPUT
    uint32_t divider_sum = 0U;
#endif

#if BATVOL_DIAG_DIRECT_ADC_MV_OUTPUT
    (void)up_r;
    (void)low_r;
    if (mv == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
#else
    if ((mv == NULL) || (low_r == 0U)) {
        return ESP_ERR_INVALID_ARG;
    }
#endif

    ret = batvol_read_iovol(&io_mv);
    if (ret != ESP_OK) {
        return ret;
    }

#if BATVOL_DIAG_DIRECT_ADC_MV_OUTPUT
    bat_mv_u32 = (uint32_t)io_mv;
#else
    divider_sum = (uint32_t)up_r + (uint32_t)low_r;
    bat_mv_u32 =
        ((((uint32_t)io_mv) * divider_sum) + ((uint32_t)low_r / 2U)) / (uint32_t)low_r;
#endif

    if (bat_mv_u32 > UINT16_MAX) {
        bat_mv_u32 = UINT16_MAX;
    }

    *mv = (uint16_t)bat_mv_u32;

    return ESP_OK;
}
