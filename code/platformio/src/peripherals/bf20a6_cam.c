#include "bf20a6_cam.h"

#include "esp_log.h"

#if CONFIG_IDF_TARGET_ESP32S3
#include "soc/lcd_cam_struct.h"
#endif

#include "bsp/delay.h"
#include "peripherals/gpba02b.h"

#define TAG "BF20A6_CAM"

static const cam_profile_s s_profiles[6] = {
    {true, 0x32U, 0x44U, false, false, false, true},  /* Profile 21 */
    {true, 0x32U, 0x46U, false, true, false, true},   /* Profile 31 */
    {true, 0x22U, 0x44U, false, false, false, true},  /* Profile 37 */
    {true, 0x22U, 0x46U, false, true, false, true},   /* Profile 39 */
    {false, 0x12U, 0x46U, false, false, false, true}, /* Profile 45 */
    {false, 0x12U, 0x46U, false, true, false, true}   /* Profile 47 */
};

/*
 * brief     : Convert configured profile index to a safe slot value.
 * input     : None.
 * output    : Slot index in s_profiles range.
 * type      : private
 */
static uint8_t _bf20a6_profile_slot(void)
{
    uint8_t slot = (uint8_t)CAM_BF20A6_PROFILE_ID;

    if (slot >= (uint8_t)(sizeof(s_profiles) / sizeof(s_profiles[0])))
    {
        slot = 0U;
    }

    return slot;
}

#ifdef CAMERA_OBJECT

static bool s_bf20a6_ready = false;

/*
 * brief     : Initialize GPBA02B and configure camera control pins as outputs.
 * input     : None.
 * output    : ESP_OK on success, otherwise error code.
 * type      : private
 */
static esp_err_t _bf20a6_prepare_ctrl_pins(void)
{
    esp_err_t ret;

    ret = gpba02b_init_device();
    if (ret != ESP_OK)
    {
        return ret;
    }

    ret = gpba02b_pin_set_mode(CAM_IO_RESET_PORT, CAM_IO_RESET_PIN, GPBA02B_PIN_MODE_OUTPUT);
    if (ret != ESP_OK)
    {
        return ret;
    }

    ret = gpba02b_pin_set_mode(CAM_IO_PWDN_PORT, CAM_IO_PWDN_PIN, GPBA02B_PIN_MODE_OUTPUT);
    if (ret != ESP_OK)
    {
        return ret;
    }

    ret = gpba02b_pin_set_mode(CAM_IO_LIGHT_PORT, CAM_IO_LIGHT_PIN, GPBA02B_PIN_MODE_OUTPUT);
    if (ret != ESP_OK)
    {
        return ret;
    }

    return gpba02b_pin_write(CAM_IO_LIGHT_PORT, CAM_IO_LIGHT_PIN, false);
}

/*
 * brief     : Perform camera hardware power-cycle before esp_camera_init.
 * input     : None.
 * output    : ESP_OK on success, otherwise error code.
 * type      : private
 */
static esp_err_t _bf20a6_power_cycle(void)
{
    esp_err_t ret;

    ret = gpba02b_pin_write(CAM_IO_RESET_PORT, CAM_IO_RESET_PIN, false);
    if (ret != ESP_OK)
    {
        return ret;
    }

    ret = gpba02b_pin_write(CAM_IO_PWDN_PORT, CAM_IO_PWDN_PIN, true);
    if (ret != ESP_OK)
    {
        return ret;
    }
    delay_ms(20);

    ret = gpba02b_pin_write(CAM_IO_PWDN_PORT, CAM_IO_PWDN_PIN, false);
    if (ret != ESP_OK)
    {
        return ret;
    }
    delay_ms(20);

    ret = gpba02b_pin_write(CAM_IO_RESET_PORT, CAM_IO_RESET_PIN, true);
    if (ret != ESP_OK)
    {
        return ret;
    }
    delay_ms(30);

    return ESP_OK;
}

/*
 * brief     : Drive camera pins to low-power state.
 * input     : None.
 * output    : None.
 * type      : private
 */
static void _bf20a6_power_down(void)
{
    (void)gpba02b_pin_write(CAM_IO_LIGHT_PORT, CAM_IO_LIGHT_PIN, false);
    (void)gpba02b_pin_write(CAM_IO_RESET_PORT, CAM_IO_RESET_PIN, false);
    (void)gpba02b_pin_write(CAM_IO_PWDN_PORT, CAM_IO_PWDN_PIN, true);
}

/*
 * brief     : Apply fixed sensor-side BF20A6 DVP register profile.
 * input     : None.
 * output    : ESP_OK on success, otherwise error code.
 * type      : private
 */
static esp_err_t _bf20a6_apply_sensor_fixed_profile(void)
{
    sensor_t *sensor;
    const cam_profile_s *profile;
    uint8_t slot;
    uint8_t yuv_seq;
    uint8_t com0_value;
    uint8_t com1_value;
    uint8_t reg_div_value;
    int ret;

    sensor = esp_camera_sensor_get();
    if ((sensor == NULL) || (sensor->set_reg == NULL))
    {
        return ESP_ERR_NOT_SUPPORTED;
    }

    slot = _bf20a6_profile_slot();
    profile = &s_profiles[slot];
    yuv_seq = (uint8_t)(CAM_BF20A6_SENSOR_YUV_SEQ & 0x03U);
    com0_value = (CAM_BF20A6_SENSOR_COM0_BYTE_SWAP != 0) ? 0x40U : 0x00U;
    reg_div_value = profile->reg_div_vclk_inv ? 0x10U : 0x00U;

    com1_value = 0U;
    if (profile->vclk_rev_before)
    {
        com1_value |= 0x80U;
    }
    if (profile->vclk_rev_after)
    {
        com1_value |= 0x10U;
    }
    if (profile->hsync_as_href)
    {
        com1_value |= 0x08U;
    }

    ret = sensor->set_reg(sensor, 0x12, 0x40, com0_value);
    if (ret != 0)
    {
        return ESP_FAIL;
    }

    ret = sensor->set_reg(sensor, 0x16, 0x0C, (int)(yuv_seq << 2));
    if (ret != 0)
    {
        return ESP_FAIL;
    }

    ret = sensor->set_reg(sensor, 0x15, 0x98, com1_value);
    if (ret != 0)
    {
        return ESP_FAIL;
    }

    ret = sensor->set_reg(sensor, 0xE1, 0x10, reg_div_value);
    if (ret != 0)
    {
        return ESP_FAIL;
    }

    ret = sensor->set_reg(sensor, 0xB6, 0xFF, (int)CAM_BF20A6_TEST_MODE);
    if (ret != 0)
    {
        return ESP_FAIL;
    }

    if (sensor->set_hmirror != NULL)
    {
        ret = sensor->set_hmirror(sensor, (CAM_BF20A6_HMIRROR != 0) ? 1 : 0);
        if (ret != 0)
        {
            ESP_LOGW(TAG, "set_hmirror failed: %d", ret);
        }
    }

    if (sensor->set_vflip != NULL)
    {
        ret = sensor->set_vflip(sensor, (CAM_BF20A6_VFLIP != 0) ? 1 : 0);
        if (ret != 0)
        {
            ESP_LOGW(TAG, "set_vflip failed: %d", ret);
        }
    }

    ESP_LOGI(TAG,
             "fixed sensor cfg idx=%u yuv_seq=%u href=%u vclk_b=%u vclk_a=%u div_inv=%u test=0x%02X hm=%u vf=%u",
             (unsigned)slot,
             (unsigned)yuv_seq,
             (unsigned)(profile->hsync_as_href ? 1U : 0U),
             (unsigned)(profile->vclk_rev_before ? 1U : 0U),
             (unsigned)(profile->vclk_rev_after ? 1U : 0U),
             (unsigned)(profile->reg_div_vclk_inv ? 1U : 0U),
             (unsigned)CAM_BF20A6_TEST_MODE,
             (unsigned)(CAM_BF20A6_HMIRROR ? 1U : 0U),
             (unsigned)(CAM_BF20A6_VFLIP ? 1U : 0U));

    return ESP_OK;
}

/*
 * brief     : Apply fixed host-side LCD_CAM capture polarity settings.
 * input     : None.
 * output    : None.
 * type      : private
 */
static void _bf20a6_apply_host_capture_polarity(void)
{
#if CONFIG_IDF_TARGET_ESP32S3
    LCD_CAM.cam_ctrl1.cam_clk_inv = (CAM_BF20A6_PCLK_INVERT != 0) ? 1U : 0U;
    LCD_CAM.cam_ctrl1.cam_hsync_inv = (CAM_BF20A6_HSYNC_INVERT != 0) ? 1U : 0U;
    LCD_CAM.cam_ctrl1.cam_vsync_inv = (CAM_BF20A6_VSYNC_INVERT != 0) ? 1U : 0U;
    LCD_CAM.cam_ctrl.cam_update = 1;

    ESP_LOGI(TAG,
             "host polarity pclk_inv=%u hsync_inv=%u vsync_inv=%u",
             (unsigned)(CAM_BF20A6_PCLK_INVERT ? 1U : 0U),
             (unsigned)(CAM_BF20A6_HSYNC_INVERT ? 1U : 0U),
             (unsigned)(CAM_BF20A6_VSYNC_INVERT ? 1U : 0U));
#endif
}

/*
 * brief     : Apply fixed BF20A6 sensor clock profile (E3/F0).
 * input     : None.
 * output    : ESP_OK on success, otherwise error code.
 * type      : private
 */
static esp_err_t _bf20a6_apply_clock_fixed_profile(void)
{
    sensor_t *sensor;
    const cam_profile_s *profile;
    uint8_t slot;
    int ret;

    slot = _bf20a6_profile_slot();
    profile = &s_profiles[slot];

    if (!profile->sensor_div)
    {
        ESP_LOGI(TAG,
                 "fixed clk cfg idx=%u use_div=0 (skip E3/F0)",
                 (unsigned)slot);
        return ESP_OK;
    }

    sensor = esp_camera_sensor_get();
    if ((sensor == NULL) || (sensor->set_reg == NULL))
    {
        return ESP_ERR_NOT_SUPPORTED;
    }

    ret = sensor->set_reg(sensor, 0xE3, 0xFF, (int)profile->pll_ctrl);
    if (ret != 0)
    {
        return ESP_FAIL;
    }

    ret = sensor->set_reg(sensor, 0xF0, 0xFF, (int)profile->int_time_ctrl);
    if (ret != 0)
    {
        return ESP_FAIL;
    }

    ESP_LOGI(TAG,
             "fixed clk cfg idx=%u use_div=1 e3=0x%02X f0=0x%02X",
             (unsigned)slot,
             (unsigned)profile->pll_ctrl,
             (unsigned)profile->int_time_ctrl);

    return ESP_OK;
}

/*
 * brief     : Control camera light output pin.
 * input     : enable true to turn on, false to turn off.
 * output    : ESP_OK on success, otherwise error code.
 * type      : public
 */
esp_err_t bf20a6_cam_set_light(bool enable)
{
    esp_err_t ret;

    ret = _bf20a6_prepare_ctrl_pins();
    if (ret != ESP_OK)
    {
        return ret;
    }

    return gpba02b_pin_write(CAM_IO_LIGHT_PORT, CAM_IO_LIGHT_PIN, enable);
}

/*
 * brief     : Initialize camera driver and apply fixed sensor/host profiles.
 * input     : None.
 * output    : ESP_OK on success, otherwise error code.
 * type      : public
 */
esp_err_t bf20a6_cam_open(void)
{
    camera_config_t cfg = {
        .pin_pwdn = -1,
        .pin_reset = -1,
        .pin_xclk = CAM_IO_XCLK,
        .pin_sccb_sda = CAM_IO_SCCB_SDA,
        .pin_sccb_scl = CAM_IO_SCCB_SCL,
        .pin_d7 = CAM_IO_D7,
        .pin_d6 = CAM_IO_D6,
        .pin_d5 = CAM_IO_D5,
        .pin_d4 = CAM_IO_D4,
        .pin_d3 = CAM_IO_D3,
        .pin_d2 = CAM_IO_D2,
        .pin_d1 = CAM_IO_D1,
        .pin_d0 = CAM_IO_D0,
        .pin_vsync = CAM_IO_VSYNC,
        .pin_href = CAM_IO_HREF,
        .pin_pclk = CAM_IO_PCLK,
        .xclk_freq_hz = CAM_XCLK_EXTERNAL_OSC ? (int)CAM_XCLK_EXTERNAL_HZ : 20000000,
        .ledc_timer = LEDC_TIMER_0,
        .ledc_channel = LEDC_CHANNEL_0,
        .pixel_format = PIXFORMAT_YUV422,
        .frame_size = CAM_BF20A6_FRAME_SIZE,
        .jpeg_quality = 12,
        .fb_count = CAM_BF20A6_FB_COUNT,
        .fb_location = CAMERA_FB_IN_PSRAM,
        .grab_mode = CAMERA_GRAB_WHEN_EMPTY,
        .sccb_i2c_port = CAM_SCCB_I2C_PORT,
    };
    sensor_t *sensor;
    esp_err_t ret;

    if (s_bf20a6_ready)
    {
        return ESP_OK;
    }

    ret = _bf20a6_prepare_ctrl_pins();
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "prepare ctrl pins failed: %d", (int)ret);
        return ret;
    }

    ret = _bf20a6_power_cycle();
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "power cycle failed: %d", (int)ret);
        return ret;
    }

    ret = esp_camera_init(&cfg);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "esp_camera_init failed: %d", (int)ret);
        return ret;
    }

    s_bf20a6_ready = true;

    sensor = esp_camera_sensor_get();
    if (sensor != NULL)
    {
        if ((sensor->id.PID != BF20A6_PID) && (sensor->id.PID != 0U))
        {
            ESP_LOGW(TAG, "detected PID=0x%04X (not BF20A6)", (unsigned)sensor->id.PID);
        }

        if (sensor->set_framesize != NULL)
        {
            (void)sensor->set_framesize(sensor, CAM_BF20A6_FRAME_SIZE);
        }
    }

    ret = _bf20a6_apply_sensor_fixed_profile();
    if (ret != ESP_OK)
    {
        ESP_LOGW(TAG, "sensor fixed profile apply failed: %d", (int)ret);
    }

    _bf20a6_apply_host_capture_polarity();

    ret = _bf20a6_apply_clock_fixed_profile();
    if (ret != ESP_OK)
    {
        ESP_LOGW(TAG, "clock fixed profile apply failed: %d", (int)ret);
    }

    ESP_LOGI(TAG,
             "camera ready xclk_mode=%s xclk_hz=%d",
             CAM_XCLK_EXTERNAL_OSC ? "external" : "internal",
             cfg.xclk_freq_hz);

    return ESP_OK;
}

/*
 * brief     : Deinitialize camera driver and power down camera pins.
 * input     : None.
 * output    : None.
 * type      : public
 */
void bf20a6_cam_close(void)
{
    if (s_bf20a6_ready)
    {
        (void)esp_camera_deinit();
        s_bf20a6_ready = false;
    }

    _bf20a6_power_down();
}

/*
 * brief     : Query camera open state.
 * input     : None.
 * output    : true when camera driver is ready, otherwise false.
 * type      : public
 */
bool bf20a6_cam_is_open(void)
{
    return s_bf20a6_ready;
}

/*
 * brief     : Acquire one frame buffer from camera driver.
 * input     : None.
 * output    : Frame buffer pointer, or NULL if not available.
 * type      : public
 */
camera_fb_t *bf20a6_cam_fb_get(void)
{
    if (!s_bf20a6_ready)
    {
        return NULL;
    }

    return esp_camera_fb_get();
}

/*
 * brief     : Return one frame buffer to camera driver.
 * input     : fb frame buffer pointer.
 * output    : None.
 * type      : public
 */
void bf20a6_cam_fb_return(camera_fb_t *fb)
{
    if (fb == NULL)
    {
        return;
    }

    esp_camera_fb_return(fb);
}

#else

/*
 * brief     : Fallback open implementation when CAMERA_OBJECT is disabled.
 * input     : None.
 * output    : ESP_ERR_NOT_SUPPORTED.
 * type      : public
 */
esp_err_t bf20a6_cam_open(void)
{
    return ESP_ERR_NOT_SUPPORTED;
}

/*
 * brief     : Fallback close implementation when CAMERA_OBJECT is disabled.
 * input     : None.
 * output    : None.
 * type      : public
 */
void bf20a6_cam_close(void)
{
}

/*
 * brief     : Fallback open-state query when CAMERA_OBJECT is disabled.
 * input     : None.
 * output    : Always false.
 * type      : public
 */
bool bf20a6_cam_is_open(void)
{
    return false;
}

/*
 * brief     : Fallback frame acquire when CAMERA_OBJECT is disabled.
 * input     : None.
 * output    : Always NULL.
 * type      : public
 */
camera_fb_t *bf20a6_cam_fb_get(void)
{
    return NULL;
}

/*
 * brief     : Fallback frame return when CAMERA_OBJECT is disabled.
 * input     : fb frame buffer pointer.
 * output    : None.
 * type      : public
 */
void bf20a6_cam_fb_return(camera_fb_t *fb)
{
    (void)fb;
}

/*
 * brief     : Fallback light control when CAMERA_OBJECT is disabled.
 * input     : enable true/false.
 * output    : ESP_ERR_NOT_SUPPORTED.
 * type      : public
 */
esp_err_t bf20a6_cam_set_light(bool enable)
{
    (void)enable;
    return ESP_ERR_NOT_SUPPORTED;
}

#endif
