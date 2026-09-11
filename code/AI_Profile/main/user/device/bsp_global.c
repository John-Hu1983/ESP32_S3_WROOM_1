#include "bsp_global.h"

#include <rom/ets_sys.h>

#include "user/common/user_facility.h"

#define TAG "bsp_global"

static adc_oneshot_unit_handle_t s_bsp_adc_unit_handle = NULL;
static adc_cali_handle_t s_bsp_adc_cali_handle = NULL;
static adc_unit_t s_bsp_adc_unit = BSP_BATTERY_ADC_UNIT;
static adc_channel_t s_bsp_adc_channel = VR_ADC_CHANNEL;
static bool s_bsp_adc_ready = false;
static bool s_bsp_adc_cali_enabled = false;

/*
 * brief : Try to create ADC calibration handle for battery channel.
 * input : none.
 * output: none.
 * type  : private
 */
static void _bsp_init_adc_cali(void)
{
    esp_err_t ret = ESP_ERR_NOT_SUPPORTED;

#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    adc_cali_curve_fitting_config_t cali_cfg = {
        .unit_id = s_bsp_adc_unit,
        .chan = s_bsp_adc_channel,
        .atten = BSP_BATTERY_ADC_ATTEN,
        .bitwidth = BSP_BATTERY_ADC_BITWIDTH,
    };
    ret = adc_cali_open_scheme_curve_fitting(&cali_cfg, &s_bsp_adc_cali_handle);
#elif ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
    adc_cali_line_fitting_config_t cali_cfg = {
        .unit_id = s_bsp_adc_unit,
        .atten = BSP_BATTERY_ADC_ATTEN,
        .bitwidth = BSP_BATTERY_ADC_BITWIDTH,
        .default_vref = 0,
    };
    ret = adc_cali_open_scheme_line_fitting(&cali_cfg, &s_bsp_adc_cali_handle);
#endif

    if (ret == ESP_OK) {
        s_bsp_adc_cali_enabled = true;
    }
    else {
        s_bsp_adc_cali_enabled = false;
        s_bsp_adc_cali_handle = NULL;
    }
}

/*
 * brief : Set power-lock output level for board power domain hold.
 * input : enable - true to lock power on, false to release.
 * output: none.
 * type  : private
 */
static void _bsp_set_power(bool enable)
{
    gpba02b_set_io_mode(
        POWER_LOCK_IO_PORT,
        POWER_LOCK_IO_PIN,
        GPBA02B_IO_STYLE_OUTPUT_CMOS
    );
    gpba02b_write_io_level(POWER_LOCK_IO_PORT, POWER_LOCK_IO_PIN, enable ? 1 : 0);
}

/*
 * brief : Initialize one PWM output with default frequency and duty.
 * input : port - PWM port; pin - PWM pin; fre - frequency enum; percent - duty percent.
 * output: none.
 * type  : private
 */
static void
_bsp_init_pwm(gpba02b_port_t port, uint8_t pin, gpba02b_pwm_freq_t fre, uint8_t percent)
{
    gpba02b_config_pwm_mode(port, pin);
    gpba02b_set_pwm_frequency(port, fre);
    gpba02b_set_pwm_duty(port, pin, percent);
}

/*
 * brief : Initialize board button GPIO pins as pull-up inputs.
 * input : none.
 * output: none.
 * type  : private
 */
static void _bsp_init_button(void)
{
    gpba02b_set_io_mode(
        BUTTON_UP_IO_PORT,
        BUTTON_UP_IO_PIN,
        GPBA02B_IO_STYLE_INPUT_PULL_HIGH
    );
    gpba02b_set_io_mode(
        BUTTON_DOWN_IO_PORT,
        BUTTON_DOWN_IO_PIN,
        GPBA02B_IO_STYLE_INPUT_PULL_HIGH
    );
}

/*
 * brief: Delay execution for at least the requested millisecond duration.
 * input: ms - delay time in milliseconds.
 * output: None.
 */
void delay_ms(uint32_t ms)
{
    TickType_t ticks = pdMS_TO_TICKS(ms) < 1 ? 1 : pdMS_TO_TICKS(ms);
    vTaskDelay(ticks);
}

/*
 * brief : Enable or disable audio power-control GPIOs.
 * input : enable - true to enable audio hardware, false to disable.
 * output: none.
 * type  : public
 */
void bsp_set_audio_ctrl(bool enable)
{
    gpba02b_set_io_mode(PDM_ENABLE_PORT, PDM_ENABLE_PIN, GPBA02B_IO_STYLE_OUTPUT_CMOS);
    gpba02b_write_io_level(PDM_ENABLE_PORT, PDM_ENABLE_PIN, enable ? 1 : 0);

    gpba02b_set_io_mode(I2S_ENABLE_PORT, I2S_ENABLE_PIN, GPBA02B_IO_STYLE_OUTPUT_CMOS);
    gpba02b_write_io_level(I2S_ENABLE_PORT, I2S_ENABLE_PIN, enable ? 1 : 0);
}

/*
 * brief : Initialize ADC one-shot converter on IO1 battery-sense channel.
 * input : none.
 * output: none.
 * type  : public
 */
void bsp_init_adc_converter(void)
{
    esp_err_t ret = ESP_OK;
    adc_unit_t detected_unit = BSP_BATTERY_ADC_UNIT;
    adc_channel_t detected_channel = VR_ADC_CHANNEL;
    adc_oneshot_unit_init_cfg_t init_cfg = {
        .unit_id = BSP_BATTERY_ADC_UNIT,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    adc_oneshot_chan_cfg_t chan_cfg = {
        .bitwidth = BSP_BATTERY_ADC_BITWIDTH,
        .atten = BSP_BATTERY_ADC_ATTEN,
    };

    if (s_bsp_adc_ready) {
        return;
    }

    ret = adc_oneshot_io_to_channel(VR_ADC_IO, &detected_unit, &detected_channel);
    if (ret == ESP_OK) {
        s_bsp_adc_unit = detected_unit;
        s_bsp_adc_channel = detected_channel;
    }
    else {
        s_bsp_adc_unit = BSP_BATTERY_ADC_UNIT;
        s_bsp_adc_channel = VR_ADC_CHANNEL;
        ESP_LOGW(
            TAG,
            "adc io->channel map failed, use fallback unit=%d channel=%d err=%s",
            (int)s_bsp_adc_unit,
            (int)s_bsp_adc_channel,
            esp_err_to_name(ret)
        );
    }

    (void)gpio_reset_pin(VR_ADC_IO);
    (void)gpio_set_direction(VR_ADC_IO, GPIO_MODE_INPUT);
    (void)gpio_pullup_dis(VR_ADC_IO);
    (void)gpio_pulldown_dis(VR_ADC_IO);

    init_cfg.unit_id = s_bsp_adc_unit;

    ret = adc_oneshot_new_unit(&init_cfg, &s_bsp_adc_unit_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "adc new unit failed: %s", esp_err_to_name(ret));
        return;
    }

    ret = adc_oneshot_config_channel(s_bsp_adc_unit_handle, s_bsp_adc_channel, &chan_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "adc config channel failed: %s", esp_err_to_name(ret));
        (void)adc_oneshot_del_unit(s_bsp_adc_unit_handle);
        s_bsp_adc_unit_handle = NULL;
        return;
    }

    _bsp_init_adc_cali();
    s_bsp_adc_ready = true;

    ESP_LOGI(
        TAG,
        "battery adc ready: io=%d unit=%d channel=%d cali=%s",
        (int)VR_ADC_IO,
        (int)s_bsp_adc_unit,
        (int)s_bsp_adc_channel,
        s_bsp_adc_cali_enabled ? "on" : "off"
    );
}

/*
 * brief : Read battery voltage in millivolts from ADC channel with divider restore.
 * input : none.
 * output: battery voltage (mV).
 * type  : public
 */
uint16_t bsp_read_battery_mv(void)
{
    esp_err_t ret = ESP_OK;
    uint32_t i = 0U;
    uint32_t raw_sum = 0U;
    uint32_t ok_count = 0U;
    uint32_t total_samples = BSP_BATTERY_ADC_DISCARD_COUNT + BSP_BATTERY_ADC_SAMPLE_COUNT;
    uint32_t battery_mv = 0U;
    int raw = 0;
    int adc_mv = 0;

    if (!s_bsp_adc_ready) {
        bsp_init_adc_converter();
    }
    if (!s_bsp_adc_ready || (s_bsp_adc_unit_handle == NULL)) {
        return 0U;
    }

    for (i = 0U; i < total_samples; i++) {
        ret = adc_oneshot_read(s_bsp_adc_unit_handle, s_bsp_adc_channel, &raw);
        if (ret == ESP_OK) {
            if (i >= BSP_BATTERY_ADC_DISCARD_COUNT) {
                raw_sum += (uint32_t)raw;
                ok_count++;
            }
        }

        if (BSP_BATTERY_ADC_SAMPLE_DELAY_US > 0U) {
            ets_delay_us(BSP_BATTERY_ADC_SAMPLE_DELAY_US);
        }
    }

    if (ok_count == 0U) {
        ESP_LOGW(TAG, "adc read failed");
        return 0U;
    }

    raw = (int)(raw_sum / ok_count);

    if (s_bsp_adc_cali_enabled) {
        ret = adc_cali_raw_to_voltage(s_bsp_adc_cali_handle, raw, &adc_mv);
        if (ret != ESP_OK) {
            s_bsp_adc_cali_enabled = false;
        }
    }

    if (!s_bsp_adc_cali_enabled) {
        adc_mv = (int)((((uint32_t)raw) * BSP_BATTERY_ADC_FALLBACK_FULL_SCALE_MV)
                       / BSP_BATTERY_ADC_FALLBACK_MAX_RAW);
    }

    /* Restore battery-side voltage: Vbat = Vadc * (Rtop + Rbottom) / Rbottom. */
    battery_mv = ((((uint32_t)adc_mv) * BSP_BATTERY_DIVIDER_TOTAL_KOHM)
                  + (BSP_BATTERY_DIVIDER_BOTTOM_KOHM / 2U))
        / BSP_BATTERY_DIVIDER_BOTTOM_KOHM;

    if (battery_mv > UINT16_MAX) {
        battery_mv = UINT16_MAX;
    }

    return (uint16_t)battery_mv;
}

/*
 * brief : Execute LCD reset pulse sequence.
 * input : none.
 * output: none.
 * type  : public
 */
void bsp_reset_lcd(void)
{
    gpba02b_set_io_mode(
        LCD_IO_RESET_PORT,
        LCD_IO_RESET_PIN,
        GPBA02B_IO_STYLE_OUTPUT_CMOS
    );
    gpba02b_write_io_level(LCD_IO_RESET_PORT, LCD_IO_RESET_PIN, 1);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpba02b_write_io_level(LCD_IO_RESET_PORT, LCD_IO_RESET_PIN, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpba02b_write_io_level(LCD_IO_RESET_PORT, LCD_IO_RESET_PIN, 1);
    vTaskDelay(pdMS_TO_TICKS(10));
}

/*
 * brief : Initialize board-level peripheral chain and user desktop services.
 * input : none.
 * output: none.
 * type  : public
 */
void bsp_init_total(void)
{
    ESP_LOGI(TAG, "Initializing BSP...");
    ESP_ERROR_CHECK(gpba02b_init_object());
    _bsp_set_power(true);
    bsp_set_audio_ctrl(true);
    bsp_init_adc_converter();
    _bsp_init_pwm(GPBA02B_PORT_C, 1, GPBA02B_PWM_FREQ_1343HZ_DIV32, 12);
    _bsp_init_button();
    bsp_reset_lcd();
    speaker_set_volume(90);
    desktop_start_task();
}
