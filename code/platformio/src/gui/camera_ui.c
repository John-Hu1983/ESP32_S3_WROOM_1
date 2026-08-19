#include "camera_ui.h"

#if CONFIG_IDF_TARGET_ESP32S3
#include "soc/lcd_cam_struct.h"
#endif

#define TAG "CAMERA"
#define CAMERA_INIT_RETRY_MAX 6U
#define CAMERA_INIT_RETRY_DELAY_MS 220U
#define CAMERA_HARD_RESET_HOLD_MS 260U
#define CAMERA_REINIT_ROUND_MAX 2U
#define CAMERA_FIRST_FRAME_RETRY_MAX 10U
#define CAMERA_FIRST_FRAME_RETRY_DELAY_MS 40U
#define CAMERA_FIRST_FRAME_SUCCESS_MIN 6U
#define CAMERA_FRAME_INTERVAL_MIN_US 8000ULL
#define CAMERA_BF20A6_PLL_CTRL_DEFAULT 0x12U
#define CAMERA_BF20A6_PLL_CTRL_LOW 0x82U
#define CAMERA_BF20A6_INTG_CTRL_DEFAULT 0x45U
#define CAMERA_BF20A6_INTG_CTRL_LOW 0x44U
#define CAMERA_RUNTIME_TIMEOUT_RECOVER_MAX 3U
#define CAMERA_WORKER_TASK_STACK_SIZE 8192U
#define CAMERA_WORKER_TASK_PRIORITY 4U

#if CONFIG_FREERTOS_UNICORE
#define CAMERA_WORKER_TASK_CORE tskNO_AFFINITY
#else
#define CAMERA_WORKER_TASK_CORE 1
#endif

static camera_app_ctx_t *s_camera_ctx = NULL;
static TaskHandle_t s_camera_input_task_handle = NULL;
static volatile bool s_camera_input_task_stop = false;

#ifdef CAMERA_OBJECT
static esp_err_t _camera_ensure_psram_dma_enabled(void)
{
#if CONFIG_IDF_TARGET_ESP32S2 || CONFIG_IDF_TARGET_ESP32S3
    if (esp_camera_get_psram_mode())
    {
        return ESP_OK;
    }

    USER_RETURN_ON_ERROR(esp_camera_set_psram_mode(true),
                         TAG,
                         "enable PSRAM DMA mode failed");

    if (!esp_camera_get_psram_mode())
    {
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "PSRAM DMA mode enabled");
#endif

    return ESP_OK;
}

struct camera_bf20a6_sync_profile
{
    uint8_t host_clk_inv;
    uint8_t host_hsync_inv;
    uint8_t host_vsync_inv;
    uint8_t com1;
    uint8_t com2;
    const char *name;
};

static const struct camera_bf20a6_sync_profile s_bf20a6_sync_profiles[] = {
    {1U, 0U, 0U, 0x00U, 0x70U, "pclk_inv_default"},
    {0U, 0U, 0U, 0x00U, 0x70U, "default"},
    {1U, 1U, 0U, 0x00U, 0x70U, "pclk_inv_hsync_inv"},
    {0U, 1U, 0U, 0x00U, 0x70U, "hsync_inv"},
    {1U, 0U, 1U, 0x00U, 0x70U, "pclk_inv_vsync_inv"},
    {0U, 0U, 1U, 0x00U, 0x70U, "vsync_inv"},
    {1U, 1U, 1U, 0x00U, 0x70U, "pclk_inv_hv_inv"},
    {0U, 1U, 1U, 0x00U, 0x70U, "hv_inv"},
    {1U, 0U, 0U, 0x10U, 0x70U, "pclk_inv_com1_bit4"},
    {0U, 0U, 0U, 0x10U, 0x70U, "com1_bit4"},
    {1U, 0U, 0U, 0x80U, 0x70U, "pclk_inv_com1_bit7"},
    {0U, 0U, 0U, 0x80U, 0x70U, "com1_bit7"},
};
#endif

/*
 * brief: Ensure preview frame buffer has enough bytes for one RGB565 frame copy.
 * input: ctx - camera app context; bytes - required frame payload size.
 * output: ESP_OK on success; otherwise ESP_ERR_INVALID_ARG or ESP_ERR_NO_MEM.
 */
static esp_err_t _camera_ensure_frame_buffer(camera_app_ctx_t *ctx, size_t bytes)
{
    uint8_t *buf;

    if ((ctx == NULL) || (bytes == 0U))
    {
        return ESP_ERR_INVALID_ARG;
    }

    if ((ctx->frame_buf != NULL) && (ctx->frame_buf_size >= bytes))
    {
        return ESP_OK;
    }

    buf = (uint8_t *)heap_caps_realloc(ctx->frame_buf, bytes,
                                       MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (buf == NULL)
    {
        buf = (uint8_t *)heap_caps_realloc(ctx->frame_buf, bytes, MALLOC_CAP_8BIT);
    }

    if (buf == NULL)
    {
        return ESP_ERR_NO_MEM;
    }

    ctx->frame_buf = buf;
    ctx->frame_buf_size = bytes;
    return ESP_OK;
}

/*
 * brief: Convert one YUV422 (YUYV) frame into RGB565 buffer for LVGL rendering.
 * input: src/src_len - source YUV422 bytes; dst - destination RGB565 bytes.
 * output: number of bytes written into destination buffer.
 */
static size_t _camera_convert_yuv422_to_rgb565(const uint8_t *src,
                                               size_t src_len,
                                               uint8_t *dst)
{
    size_t pair_count;
    size_t i;
    uint16_t *dst16;

    if ((src == NULL) || (dst == NULL) || (src_len < 4U))
    {
        return 0U;
    }

    pair_count = src_len / 4U;
    dst16 = (uint16_t *)dst;

    for (i = 0U; i < pair_count; i++)
    {
        size_t si = i * 4U;
        uint8_t y0 = src[si + 0U];
        uint8_t u = src[si + 1U];
        uint8_t y1 = src[si + 2U];
        uint8_t v = src[si + 3U];
        int32_t d = (int32_t)u - 128;
        int32_t e = (int32_t)v - 128;
        int32_t c;
        int32_t r;
        int32_t g;
        int32_t b;
        uint8_t r8;
        uint8_t g8;
        uint8_t b8;

        c = (int32_t)y0 - 16;
        if (c < 0)
        {
            c = 0;
        }
        r = (298 * c + 409 * e + 128) >> 8;
        g = (298 * c - 100 * d - 208 * e + 128) >> 8;
        b = (298 * c + 516 * d + 128) >> 8;
        r8 = (uint8_t)((r < 0) ? 0 : ((r > 255) ? 255 : r));
        g8 = (uint8_t)((g < 0) ? 0 : ((g > 255) ? 255 : g));
        b8 = (uint8_t)((b < 0) ? 0 : ((b > 255) ? 255 : b));
        dst16[(i * 2U) + 0U] = (uint16_t)(((uint16_t)(r8 & 0xF8U) << 8U) |
                                          ((uint16_t)(g8 & 0xFCU) << 3U) |
                                          ((uint16_t)b8 >> 3U));

        c = (int32_t)y1 - 16;
        if (c < 0)
        {
            c = 0;
        }
        r = (298 * c + 409 * e + 128) >> 8;
        g = (298 * c - 100 * d - 208 * e + 128) >> 8;
        b = (298 * c + 516 * d + 128) >> 8;
        r8 = (uint8_t)((r < 0) ? 0 : ((r > 255) ? 255 : r));
        g8 = (uint8_t)((g < 0) ? 0 : ((g > 255) ? 255 : g));
        b8 = (uint8_t)((b < 0) ? 0 : ((b > 255) ? 255 : b));
        dst16[(i * 2U) + 1U] = (uint16_t)(((uint16_t)(r8 & 0xF8U) << 8U) |
                                          ((uint16_t)(g8 & 0xFCU) << 3U) |
                                          ((uint16_t)b8 >> 3U));
    }

    return pair_count * 4U;
}

/*
 * brief: Stop camera driver and release runtime ownership flag.
 * input: ctx - camera app context pointer.
 * output: None.
 */
static void _camera_stop_driver(camera_app_ctx_t *ctx)
{
#ifdef CAMERA_OBJECT
    if ((ctx != NULL) && ctx->camera_started)
    {
        esp_err_t ret = esp_camera_deinit();
        if ((ret != ESP_OK) && (ret != ESP_ERR_INVALID_STATE))
        {
            ESP_LOGW(TAG, "esp_camera_deinit failed: %d", (int)ret);
        }

        ctx->camera_started = false;
    }
#else
    (void)ctx;
#endif
}

/*
 * brief: Power down BF20A6 and release private low-level resources on app exit.
 * input: none.
 * output: none.
 */
static void _camera_shutdown_sensor(void)
{
#ifdef CAMERA_OBJECT
    esp_err_t ret;

    ret = bf20a6_deinit_device();
    if ((ret != ESP_OK) && (ret != ESP_ERR_INVALID_STATE))
    {
        ESP_LOGW(TAG, "bf20a6_deinit_device failed: %d", (int)ret);
    }

    ret = gpba02b_pin_set_mode(CAM_IO_RESET_PORT,
                               CAM_IO_RESET_PIN,
                               GPBA02B_PIN_MODE_OUTPUT);
    if (ret != ESP_OK)
    {
        ESP_LOGW(TAG, "set CAM RESET pin mode failed: %d", (int)ret);
        return;
    }

    ret = gpba02b_pin_set_mode(CAM_IO_PWDN_PORT,
                               CAM_IO_PWDN_PIN,
                               GPBA02B_PIN_MODE_OUTPUT);
    if (ret != ESP_OK)
    {
        ESP_LOGW(TAG, "set CAM PWDN pin mode failed: %d", (int)ret);
        return;
    }

    ret = gpba02b_pin_set_mode(CAM_IO_LIGHT_PORT,
                               CAM_IO_LIGHT_PIN,
                               GPBA02B_PIN_MODE_OUTPUT);
    if (ret != ESP_OK)
    {
        ESP_LOGW(TAG, "set CAM LIGHT pin mode failed: %d", (int)ret);
        return;
    }

    (void)gpba02b_pin_write(CAM_IO_LIGHT_PORT, CAM_IO_LIGHT_PIN, false);
    (void)gpba02b_pin_write(CAM_IO_RESET_PORT, CAM_IO_RESET_PIN, false);
    (void)gpba02b_pin_write(CAM_IO_PWDN_PORT, CAM_IO_PWDN_PIN, true);
    delay_ms(2U);
#endif
}

#ifdef CAMERA_OBJECT
/*
 * brief: Prepare camera control pins through GPBA and apply a hard-reset pulse.
 * input: none.
 * output: ESP_OK on success; otherwise GPBA pin control error.
 */
static esp_err_t _camera_prepare_control_pins(void)
{
    USER_RETURN_ON_ERROR(gpba02b_pin_set_mode(CAM_IO_RESET_PORT,
                                              CAM_IO_RESET_PIN,
                                              GPBA02B_PIN_MODE_OUTPUT),
                         TAG,
                         "set CAM RESET pin mode failed");
    USER_RETURN_ON_ERROR(gpba02b_pin_set_mode(CAM_IO_PWDN_PORT,
                                              CAM_IO_PWDN_PIN,
                                              GPBA02B_PIN_MODE_OUTPUT),
                         TAG,
                         "set CAM PWDN pin mode failed");
    USER_RETURN_ON_ERROR(gpba02b_pin_set_mode(CAM_IO_LIGHT_PORT,
                                              CAM_IO_LIGHT_PIN,
                                              GPBA02B_PIN_MODE_OUTPUT),
                         TAG,
                         "set CAM LIGHT pin mode failed");

    USER_RETURN_ON_ERROR(gpba02b_pin_write(CAM_IO_LIGHT_PORT,
                                           CAM_IO_LIGHT_PIN,
                                           false),
                         TAG,
                         "set CAM LIGHT low failed");

    USER_RETURN_ON_ERROR(gpba02b_pin_write(CAM_IO_RESET_PORT,
                                           CAM_IO_RESET_PIN,
                                           false),
                         TAG,
                         "set CAM RESET low failed");

    USER_RETURN_ON_ERROR(gpba02b_pin_write(CAM_IO_PWDN_PORT,
                                           CAM_IO_PWDN_PIN,
                                           true),
                         TAG,
                         "set CAM PWDN high failed");
    delay_ms(80U);

    USER_RETURN_ON_ERROR(gpba02b_pin_write(CAM_IO_PWDN_PORT,
                                           CAM_IO_PWDN_PIN,
                                           false),
                         TAG,
                         "set CAM PWDN low failed");
    delay_ms(80U);

    USER_RETURN_ON_ERROR(gpba02b_pin_write(CAM_IO_RESET_PORT,
                                           CAM_IO_RESET_PIN,
                                           true),
                         TAG,
                         "set CAM RESET high failed");
    delay_ms(220U);

    return ESP_OK;
}

/*
 * brief: Force one long hardware reset cycle for stubborn SCCB/I2C failures.
 * input: none.
 * output: ESP_OK on success; otherwise GPBA pin control error.
 */
static esp_err_t _camera_force_hard_reset(void)
{
    USER_RETURN_ON_ERROR(gpba02b_pin_set_mode(CAM_IO_RESET_PORT,
                                              CAM_IO_RESET_PIN,
                                              GPBA02B_PIN_MODE_OUTPUT),
                         TAG,
                         "set CAM RESET pin mode failed");
    USER_RETURN_ON_ERROR(gpba02b_pin_set_mode(CAM_IO_PWDN_PORT,
                                              CAM_IO_PWDN_PIN,
                                              GPBA02B_PIN_MODE_OUTPUT),
                         TAG,
                         "set CAM PWDN pin mode failed");
    USER_RETURN_ON_ERROR(gpba02b_pin_set_mode(CAM_IO_LIGHT_PORT,
                                              CAM_IO_LIGHT_PIN,
                                              GPBA02B_PIN_MODE_OUTPUT),
                         TAG,
                         "set CAM LIGHT pin mode failed");

    USER_RETURN_ON_ERROR(gpba02b_pin_write(CAM_IO_LIGHT_PORT,
                                           CAM_IO_LIGHT_PIN,
                                           false),
                         TAG,
                         "set CAM LIGHT low failed");
    USER_RETURN_ON_ERROR(gpba02b_pin_write(CAM_IO_RESET_PORT,
                                           CAM_IO_RESET_PIN,
                                           false),
                         TAG,
                         "set CAM RESET low failed");
    USER_RETURN_ON_ERROR(gpba02b_pin_write(CAM_IO_PWDN_PORT,
                                           CAM_IO_PWDN_PIN,
                                           true),
                         TAG,
                         "set CAM PWDN high failed");

    delay_ms(CAMERA_HARD_RESET_HOLD_MS);

    USER_RETURN_ON_ERROR(gpba02b_pin_write(CAM_IO_PWDN_PORT,
                                           CAM_IO_PWDN_PIN,
                                           false),
                         TAG,
                         "set CAM PWDN low failed");
    delay_ms(CAMERA_HARD_RESET_HOLD_MS);

    USER_RETURN_ON_ERROR(gpba02b_pin_write(CAM_IO_RESET_PORT,
                                           CAM_IO_RESET_PIN,
                                           true),
                         TAG,
                         "set CAM RESET high failed");
    delay_ms(CAMERA_HARD_RESET_HOLD_MS);

    return ESP_OK;
}

/*
 * brief: Verify camera stream is alive immediately after esp_camera_init.
 * input: expected payload bytes and frame geometry.
 * output: true when at least one frame can be fetched and returned.
 */
static bool _camera_probe_first_frame(size_t expected_bytes,
                                      uint16_t expected_width,
                                      uint16_t expected_height)
{
    uint32_t attempt;
    uint32_t success_count;
    uint64_t last_ts_us;

    success_count = 0U;
    last_ts_us = 0ULL;

    for (attempt = 0U; attempt < CAMERA_FIRST_FRAME_RETRY_MAX; attempt++)
    {
        camera_fb_t *fb;

        fb = esp_camera_fb_get();
        if (fb != NULL)
        {
            bool frame_ok;
            uint64_t cur_ts_us;

            frame_ok = (fb->buf != NULL) &&
                       (fb->len == expected_bytes) &&
                       (fb->width == expected_width) &&
                       (fb->height == expected_height) &&
                       ((fb->format == PIXFORMAT_YUV422) ||
                        (fb->format == PIXFORMAT_RGB565));

            cur_ts_us = ((uint64_t)fb->timestamp.tv_sec * 1000000ULL) +
                        (uint64_t)fb->timestamp.tv_usec;

            /* Reject suspiciously fast frame cadence, typical of sync polarity mismatch. */
            if (frame_ok && (last_ts_us != 0ULL) && (cur_ts_us > last_ts_us) &&
                ((cur_ts_us - last_ts_us) < CAMERA_FRAME_INTERVAL_MIN_US))
            {
                frame_ok = false;
            }

            if (cur_ts_us > 0ULL)
            {
                last_ts_us = cur_ts_us;
            }

            esp_camera_fb_return(fb);

            if (frame_ok)
            {
                success_count++;
                if (success_count >= CAMERA_FIRST_FRAME_SUCCESS_MIN)
                {
                    return true;
                }
            }
            else
            {
                success_count = 0U;
            }

            delay_ms(CAMERA_FIRST_FRAME_RETRY_DELAY_MS);
            continue;
        }

        success_count = 0U;
        delay_ms(CAMERA_FIRST_FRAME_RETRY_DELAY_MS);
    }

    return false;
}

#ifdef CAMERA_OBJECT
/*
 * brief: Apply BF20A6 timing profile through sensor registers after esp_camera_init.
 * input: profile_idx - index to rotate timing presets during retries.
 * output: ESP_OK on success; otherwise ESP_FAIL when register programming fails.
 */
static esp_err_t _camera_apply_bf20a6_sync_profile(uint32_t profile_idx)
{
    sensor_t *sensor;
    const struct camera_bf20a6_sync_profile *profile;
    uint8_t pll_ctrl;
    uint8_t intg_ctrl;

    sensor = esp_camera_sensor_get();
    if ((sensor == NULL) || (sensor->id.PID != BF20A6_PID) || (sensor->set_reg == NULL))
    {
        return ESP_OK;
    }

    profile = &s_bf20a6_sync_profiles[profile_idx %
                                      (sizeof(s_bf20a6_sync_profiles) /
                                       sizeof(s_bf20a6_sync_profiles[0]))];

    /* Round 1 validates reduced BF20A6 internal clock from fixed 24MHz XCLK. */
    if (profile_idx < CAMERA_INIT_RETRY_MAX)
    {
        pll_ctrl = CAMERA_BF20A6_PLL_CTRL_LOW;
        intg_ctrl = CAMERA_BF20A6_INTG_CTRL_LOW;
    }
    else
    {
        pll_ctrl = CAMERA_BF20A6_PLL_CTRL_DEFAULT;
        intg_ctrl = CAMERA_BF20A6_INTG_CTRL_DEFAULT;
    }

#if CONFIG_IDF_TARGET_ESP32S3
    /* Align ESP32-S3 LCD_CAM capture polarity with sensor DVP timing candidate. */
    LCD_CAM.cam_ctrl1.cam_clk_inv = profile->host_clk_inv;
    LCD_CAM.cam_ctrl1.cam_hsync_inv = profile->host_hsync_inv;
    LCD_CAM.cam_ctrl1.cam_vsync_inv = profile->host_vsync_inv;
    LCD_CAM.cam_ctrl.cam_update = 1;
#endif

    ESP_LOGI(TAG,
             "apply BF20A6 sync profile: %s (clk_inv=%u hsync_inv=%u vsync_inv=%u COM1=0x%02X COM2=0x%02X PLL=0x%02X F0=0x%02X)",
             profile->name,
             (unsigned)profile->host_clk_inv,
             (unsigned)profile->host_hsync_inv,
             (unsigned)profile->host_vsync_inv,
             profile->com1,
             profile->com2,
             pll_ctrl,
             intg_ctrl);

    if (sensor->set_reg(sensor, 0xE3, 0xFF, pll_ctrl) != 0)
    {
        return ESP_FAIL;
    }
    if (sensor->set_reg(sensor, 0xF0, 0xFF, intg_ctrl) != 0)
    {
        return ESP_FAIL;
    }

    /* Allow internal PLL/divider and exposure timing path to settle. */
    delay_ms(20U);

    if (sensor->set_reg(sensor, 0x15, 0xFF, profile->com1) != 0)
    {
        return ESP_FAIL;
    }
    if (sensor->set_reg(sensor, 0x16, 0xFF, profile->com2) != 0)
    {
        return ESP_FAIL;
    }

    /* Keep DVP serial mode at 8-bit parallel output (TSLB[1:0] = 0). */
    if (sensor->set_reg(sensor, 0x3A, 0x03, 0x00) != 0)
    {
        return ESP_FAIL;
    }

    return ESP_OK;
}
#endif
#endif

/*
 * brief: Initialize esp32-camera runtime when entering camera app.
 * input: ctx - camera app context pointer.
 * output: ESP_OK on success; otherwise propagated camera init error.
 */
static esp_err_t _camera_start_driver(camera_app_ctx_t *ctx)
{
#ifndef CAMERA_OBJECT
    (void)ctx;
    return ESP_ERR_NOT_SUPPORTED;
#else
    camera_config_t cfg = {0};
    esp_err_t ret;
    uint32_t attempt;
    uint32_t round;
    size_t expected_frame_bytes;
    uint16_t expected_width;
    uint16_t expected_height;

    if ((ctx != NULL) && ctx->camera_started)
    {
        return ESP_OK;
    }

    /* Preflight BF20A6 SCCB path and then release private ownership for esp_camera_init. */
    ret = bf20a6_init_device();
    if ((ret != ESP_OK) && (ret != ESP_ERR_NOT_SUPPORTED))
    {
        ESP_LOGW(TAG, "bf20a6_init_device failed: %d", (int)ret);
    }

    ret = bf20a6_prepare_preview_start();
    if ((ret != ESP_OK) && (ret != ESP_ERR_NOT_SUPPORTED))
    {
        ESP_LOGW(TAG, "bf20a6_prepare_preview_start failed: %d", (int)ret);
    }

    cfg.pin_pwdn = -1;
    cfg.pin_reset = -1;
#if CAM_XCLK_EXTERNAL_OSC
    cfg.pin_xclk = -1;
    cfg.xclk_freq_hz = (int)CAM_XCLK_EXTERNAL_HZ;
#else
    cfg.pin_xclk = (int)CAM_IO_XCLK;
    cfg.xclk_freq_hz = 20000000;
#endif
    cfg.pin_sccb_sda = (int)CAM_IO_SCCB_SDA;
    cfg.pin_sccb_scl = (int)CAM_IO_SCCB_SCL;
    cfg.pin_d7 = (int)CAM_IO_D7;
    cfg.pin_d6 = (int)CAM_IO_D6;
    cfg.pin_d5 = (int)CAM_IO_D5;
    cfg.pin_d4 = (int)CAM_IO_D4;
    cfg.pin_d3 = (int)CAM_IO_D3;
    cfg.pin_d2 = (int)CAM_IO_D2;
    cfg.pin_d1 = (int)CAM_IO_D1;
    cfg.pin_d0 = (int)CAM_IO_D0;
    cfg.pin_vsync = (int)CAM_IO_VSYNC;
    cfg.pin_href = (int)CAM_IO_HREF;
    cfg.pin_pclk = (int)CAM_IO_PCLK;
    cfg.ledc_timer = LEDC_TIMER_0;
    cfg.ledc_channel = LEDC_CHANNEL_0;
    cfg.pixel_format = PIXFORMAT_YUV422;
    cfg.frame_size = FRAMESIZE_QQVGA;
    cfg.jpeg_quality = 12;
    cfg.fb_count = 2;
    cfg.fb_location = CAMERA_FB_IN_PSRAM;
    cfg.grab_mode = CAMERA_GRAB_LATEST;
    cfg.sccb_i2c_port = CAM_SCCB_I2C_PORT;

    expected_frame_bytes =
        (size_t)resolution[cfg.frame_size].width *
        (size_t)resolution[cfg.frame_size].height *
        2U;
    expected_width = (uint16_t)resolution[cfg.frame_size].width;
    expected_height = (uint16_t)resolution[cfg.frame_size].height;

    ret = ESP_FAIL;
    for (round = 0U; round < CAMERA_REINIT_ROUND_MAX; round++)
    {
        for (attempt = 0U; attempt < CAMERA_INIT_RETRY_MAX; attempt++)
        {
            _camera_shutdown_sensor();
            delay_ms(10U);

            ret = _camera_prepare_control_pins();
            if (ret != ESP_OK)
            {
                ESP_LOGW(TAG,
                         "prepare control pins failed (attempt %u/%u): %d",
                         (unsigned)(attempt + 1U),
                         (unsigned)CAMERA_INIT_RETRY_MAX,
                         (int)ret);
                delay_ms(CAMERA_INIT_RETRY_DELAY_MS);
                continue;
            }

            ret = esp_camera_init(&cfg);
            if (ret == ESP_OK)
            {
                ret = _camera_ensure_psram_dma_enabled();
                if (ret != ESP_OK)
                {
                    ESP_LOGW(TAG,
                             "PSRAM DMA mode is not available (attempt %u/%u)",
                             (unsigned)(attempt + 1U),
                             (unsigned)CAMERA_INIT_RETRY_MAX);
                }

                if (ret == ESP_OK)
                {
                    ret = _camera_apply_bf20a6_sync_profile(round * CAMERA_INIT_RETRY_MAX + attempt);
                    if (ret != ESP_OK)
                    {
                        ESP_LOGW(TAG,
                                 "apply BF20A6 sync profile failed (attempt %u/%u)",
                                 (unsigned)(attempt + 1U),
                                 (unsigned)CAMERA_INIT_RETRY_MAX);
                    }
                }

                if (ret == ESP_OK)
                {
                    if (_camera_probe_first_frame(expected_frame_bytes,
                                                  expected_width,
                                                  expected_height))
                    {
                        break;
                    }

                    ESP_LOGW(TAG,
                             "camera init succeeded but first frame probe failed (attempt %u/%u)",
                             (unsigned)(attempt + 1U),
                             (unsigned)CAMERA_INIT_RETRY_MAX);
                    ret = ESP_FAIL;
                }
            }

            if (ret != ESP_OK)
            {
                ESP_LOGW(TAG,
                         "esp_camera_init failed (attempt %u/%u): %d",
                         (unsigned)(attempt + 1U),
                         (unsigned)CAMERA_INIT_RETRY_MAX,
                         (int)ret);
            }

            (void)esp_camera_deinit();
            delay_ms(CAMERA_INIT_RETRY_DELAY_MS);
        }

        if (ret == ESP_OK)
        {
            break;
        }

        if ((round + 1U) < CAMERA_REINIT_ROUND_MAX)
        {
            esp_err_t reset_ret;

            ESP_LOGW(TAG,
                     "camera I2C access keeps failing, force hard reset then reinit (round %u/%u)",
                     (unsigned)(round + 1U),
                     (unsigned)CAMERA_REINIT_ROUND_MAX);

            _camera_shutdown_sensor();
            delay_ms(CAMERA_INIT_RETRY_DELAY_MS);

            reset_ret = _camera_force_hard_reset();
            if (reset_ret != ESP_OK)
            {
                ESP_LOGW(TAG, "force hard reset failed: %d", (int)reset_ret);
            }

            (void)esp_camera_deinit();
            delay_ms(CAMERA_INIT_RETRY_DELAY_MS);
        }
    }

    if (ret != ESP_OK)
    {
        return ret;
    }

    if (ctx != NULL)
    {
        ctx->camera_started = true;
    }

    ESP_LOGI(TAG, "camera preview started");
    return ESP_OK;
#endif
}

/*
 * brief: Background worker to run camera start/recovery without blocking LVGL task.
 * input: param - camera app context pointer.
 * output: None.
 */
static void _camera_worker_task(void *param)
{
    camera_app_ctx_t *ctx;
    esp_err_t ret;

    ctx = (camera_app_ctx_t *)param;
    if (ctx == NULL)
    {
        vTaskDelete(NULL);
        return;
    }

    ret = ESP_ERR_INVALID_STATE;
    if (ctx->worker_is_recovery)
    {
        _camera_stop_driver(ctx);
        _camera_shutdown_sensor();
        delay_ms(CAMERA_INIT_RETRY_DELAY_MS);
    }

    if (!ctx->worker_stop)
    {
        ret = _camera_start_driver(ctx);
    }

    ctx->worker_ret = ret;
    ctx->worker_done = true;
    ctx->worker_busy = false;
    ctx->worker_task_handle = NULL;

    vTaskDelete(NULL);
}

/*
 * brief: Launch background worker for camera startup or timeout recovery.
 * input: ctx - camera app context pointer; recovery - true for recovery mode.
 * output: true on task start success; otherwise false.
 */
static bool _camera_start_worker_task(camera_app_ctx_t *ctx, bool recovery)
{
    BaseType_t task_ok;

    if ((ctx == NULL) || ctx->worker_busy)
    {
        return false;
    }

    ctx->worker_stop = false;
    ctx->worker_busy = true;
    ctx->worker_done = false;
    ctx->worker_is_recovery = recovery;
    ctx->worker_ret = ESP_OK;

    task_ok = xTaskCreatePinnedToCore(_camera_worker_task,
                                      "camera_worker",
                                      CAMERA_WORKER_TASK_STACK_SIZE,
                                      ctx,
                                      CAMERA_WORKER_TASK_PRIORITY,
                                      &ctx->worker_task_handle,
                                      CAMERA_WORKER_TASK_CORE);
    if (task_ok != pdPASS)
    {
        task_ok = xTaskCreate(_camera_worker_task,
                              "camera_worker",
                              CAMERA_WORKER_TASK_STACK_SIZE,
                              ctx,
                              CAMERA_WORKER_TASK_PRIORITY,
                              &ctx->worker_task_handle);
    }

    if (task_ok != pdPASS)
    {
        ctx->worker_task_handle = NULL;
        ctx->worker_busy = false;
        ctx->worker_done = false;
        return false;
    }

    return true;
}

/*
 * brief: Stop camera worker task safely before releasing UI context.
 * input: ctx - camera app context pointer.
 * output: None.
 */
static void _camera_stop_worker_task(camera_app_ctx_t *ctx)
{
    TaskHandle_t handle;
    uint32_t wait_count;

    if (ctx == NULL)
    {
        return;
    }

    handle = ctx->worker_task_handle;
    if (handle == NULL)
    {
        return;
    }

    ctx->worker_stop = true;
    for (wait_count = 0U; wait_count < 20U; wait_count++)
    {
        if (ctx->worker_task_handle == NULL)
        {
            return;
        }

        delay_ms(5U);
    }

    handle = ctx->worker_task_handle;
    if (handle != NULL)
    {
        vTaskDelete(handle);
        ctx->worker_task_handle = NULL;
    }

    ctx->worker_busy = false;
    ctx->worker_done = false;
}

/*
 * brief: Input task for camera app to own key scanning while app is active.
 * input: param - unused task parameter.
 * output: None.
 */
static void _camera_input_task(void *param)
{
    btn_scan_s btn;
    bool home_requested;

    (void)param;
    lv_memset_00(&btn, sizeof(btn));
    home_requested = false;

    while (!s_camera_input_task_stop)
    {
        btn_status_e btn_val;

        btn_val = keyboard_scan_event(&btn, CAMERA_INPUT_SCAN_PERIOD_MS);
        switch (btn_val)
        {

        case Btn_Both_Click:
            if (!home_requested)
            {
                home_requested = true;
                desktop_return_to_home();
            }
            break;
        default:
            break;
        }

        delay_ms(CAMERA_INPUT_SCAN_PERIOD_MS);
    }

    s_camera_input_task_handle = NULL;
    vTaskDelete(NULL);
}

/*
 * brief: Start camera input task that handles key events in sub-app mode.
 * input: None.
 * output: true on success; otherwise false.
 */
static bool _camera_start_input_task(void)
{
    BaseType_t task_ok;

    s_camera_input_task_stop = false;
    task_ok = xTaskCreate(_camera_input_task,
                          "camera_input",
                          CAMERA_INPUT_TASK_STACK_SIZE,
                          NULL,
                          CAMERA_INPUT_TASK_PRIORITY,
                          &s_camera_input_task_handle);
    if (task_ok != pdPASS)
    {
        ESP_LOGE(TAG, "xTaskCreate camera_input failed");
        s_camera_input_task_stop = true;
        s_camera_input_task_handle = NULL;
        return false;
    }

    return true;
}

/*
 * brief: Stop camera input task and force delete on timeout.
 * input: None.
 * output: None.
 */
static void _camera_stop_input_task(void)
{
    TaskHandle_t handle;
    uint32_t wait_count;

    handle = s_camera_input_task_handle;
    if (handle == NULL)
    {
        return;
    }

    s_camera_input_task_stop = true;
    for (wait_count = 0U; wait_count < 20U; wait_count++)
    {
        if (s_camera_input_task_handle == NULL)
        {
            return;
        }

        delay_ms(5U);
    }

    handle = s_camera_input_task_handle;
    if (handle != NULL)
    {
        vTaskDelete(handle);
        s_camera_input_task_handle = NULL;
    }
}

/*
 * brief: Periodic callback to fetch one frame and refresh LVGL image widget.
 * input: timer - LVGL timer carrying camera context.
 * output: None.
 */
static void _camera_preview_timer_cb(lv_timer_t *timer)
{
    camera_app_ctx_t *ctx;

    if (timer == NULL)
    {
        return;
    }

    ctx = (camera_app_ctx_t *)timer->user_data;
    if ((ctx == NULL) || (ctx->img == NULL))
    {
        return;
    }

    if (ctx->worker_done)
    {
        esp_err_t worker_ret;
        bool was_recovery;

        worker_ret = ctx->worker_ret;
        was_recovery = ctx->worker_is_recovery;
        ctx->worker_done = false;

        if (worker_ret == ESP_OK)
        {
            ctx->frame_timeout_streak = 0U;
            ctx->preview_started = false;
            lv_label_set_text(ctx->hint_label, "camera ready");
        }
        else
        {
            if (was_recovery)
            {
                lv_label_set_text_fmt(ctx->hint_label,
                                      "camera recover failed (%d)",
                                      (int)worker_ret);
            }
            else
            {
                lv_label_set_text_fmt(ctx->hint_label,
                                      "camera init failed (%d)",
                                      (int)worker_ret);
            }
            _camera_shutdown_sensor();
        }
    }

    if (ctx->worker_busy || !ctx->camera_started)
    {
        return;
    }

#ifdef CAMERA_OBJECT
    {
        camera_fb_t *fb;
        size_t frame_bytes;
        size_t expected_bytes;
        esp_err_t ret;

        fb = esp_camera_fb_get();
        if (fb == NULL)
        {
            if (ctx->frame_timeout_streak < UINT8_MAX)
            {
                ctx->frame_timeout_streak++;
            }

            lv_label_set_text_fmt(ctx->hint_label,
                                  "camera frame timeout (%u/%u)",
                                  (unsigned)ctx->frame_timeout_streak,
                                  (unsigned)CAMERA_RUNTIME_TIMEOUT_RECOVER_MAX);

            if (ctx->frame_timeout_streak >= CAMERA_RUNTIME_TIMEOUT_RECOVER_MAX)
            {
                if (_camera_start_worker_task(ctx, true))
                {
                    lv_label_set_text(ctx->hint_label, "camera recovering...");
                }
                else if (!ctx->worker_busy)
                {
                    lv_label_set_text(ctx->hint_label, "camera recover task failed");
                }
            }

            return;
        }

        ctx->frame_timeout_streak = 0U;

        if ((fb->buf == NULL) || (fb->len == 0U))
        {
            esp_camera_fb_return(fb);
            lv_label_set_text(ctx->hint_label, "unsupported frame format");
            return;
        }

        expected_bytes = fb->width * fb->height * 2U;
        frame_bytes = expected_bytes;
        if (frame_bytes > fb->len)
        {
            if (ctx->frame_timeout_streak < UINT8_MAX)
            {
                ctx->frame_timeout_streak++;
            }

            lv_label_set_text_fmt(ctx->hint_label,
                                  "camera frame short (%u/%u)",
                                  (unsigned)ctx->frame_timeout_streak,
                                  (unsigned)CAMERA_RUNTIME_TIMEOUT_RECOVER_MAX);
            esp_camera_fb_return(fb);

            if (ctx->frame_timeout_streak >= CAMERA_RUNTIME_TIMEOUT_RECOVER_MAX)
            {
                if (_camera_start_worker_task(ctx, true))
                {
                    lv_label_set_text(ctx->hint_label, "camera recovering...");
                }
                else if (!ctx->worker_busy)
                {
                    lv_label_set_text(ctx->hint_label, "camera recover task failed");
                }
            }
            return;
        }

        ret = _camera_ensure_frame_buffer(ctx, frame_bytes);
        if (ret != ESP_OK)
        {
            esp_camera_fb_return(fb);
            lv_label_set_text(ctx->hint_label, "frame buffer alloc failed");
            return;
        }

        if (fb->format == PIXFORMAT_RGB565)
        {
            memcpy(ctx->frame_buf, fb->buf, frame_bytes);
        }
        else if (fb->format == PIXFORMAT_YUV422)
        {
            frame_bytes = _camera_convert_yuv422_to_rgb565(fb->buf, frame_bytes, ctx->frame_buf);
            if (frame_bytes == 0U)
            {
                esp_camera_fb_return(fb);
                lv_label_set_text(ctx->hint_label, "yuv422 convert failed");
                return;
            }
        }
        else
        {
            esp_camera_fb_return(fb);
            lv_label_set_text_fmt(ctx->hint_label, "unsupported frame format: %u", (unsigned)fb->format);
            return;
        }

        ctx->frame_dsc.header.always_zero = 0;
        ctx->frame_dsc.header.w = (uint32_t)fb->width;
        ctx->frame_dsc.header.h = (uint32_t)fb->height;
        ctx->frame_dsc.header.cf = LV_IMG_CF_TRUE_COLOR;
        ctx->frame_dsc.data = ctx->frame_buf;
        ctx->frame_dsc.data_size = frame_bytes;

        if (!ctx->preview_started)
        {
            lv_img_set_src(ctx->img, &ctx->frame_dsc);
            lv_obj_center(ctx->img);
            lv_img_set_pivot(ctx->img,
                             (lv_coord_t)(fb->width / 2U),
                             (lv_coord_t)(fb->height / 2U));
            ctx->preview_started = true;
            lv_label_set_text(ctx->hint_label, "LIVE");
        }
        else
        {
            lv_obj_invalidate(ctx->img);
        }

        esp_camera_fb_return(fb);
    }
#endif
}

/*
 * brief: Cleanup callback when camera screen object is deleted.
 * input: e - LVGL delete event.
 * output: None.
 */
static void _camera_delete_cb(lv_event_t *e)
{
    lv_obj_t *target;
    camera_app_ctx_t *ctx;

    target = lv_event_get_target(e);
    ctx = (camera_app_ctx_t *)lv_event_get_user_data(e);
    if (ctx == NULL)
    {
        return;
    }

    _camera_stop_input_task();
    _camera_stop_worker_task(ctx);

    if (ctx->preview_timer != NULL)
    {
        lv_timer_del(ctx->preview_timer);
        ctx->preview_timer = NULL;
    }

    _camera_stop_driver(ctx);
    _camera_shutdown_sensor();

    if (ctx->frame_buf != NULL)
    {
        heap_caps_free(ctx->frame_buf);
        ctx->frame_buf = NULL;
        ctx->frame_buf_size = 0U;
    }

    if ((target != NULL) && (s_camera_ctx == ctx) && (s_camera_ctx->screen == target))
    {
        s_camera_ctx = NULL;
    }

    lv_mem_free(ctx);
}

/*
 * brief: Build camera screen and start preview runtime.
 * input: lcd_w/lcd_h - active display resolution.
 * output: Camera screen object on success; otherwise NULL.
 */
lv_obj_t *camera_create_screen(lv_coord_t lcd_w, lv_coord_t lcd_h)
{
    camera_app_ctx_t *ctx;
    lv_obj_t *scr;
    lv_obj_t *view;
    lv_obj_t *img;
    lv_obj_t *hint;
    lv_coord_t content_top;
    lv_coord_t content_bottom;
    lv_coord_t content_h;

    if ((lcd_w <= (2 * CAMERA_MARGIN_X)) || (lcd_h <= (2 * APP_STATUS_BAR_HEIGHT + 20)))
    {
        return NULL;
    }

    ctx = (camera_app_ctx_t *)lv_mem_alloc(sizeof(camera_app_ctx_t));
    if (ctx == NULL)
    {
        return NULL;
    }
    lv_memset_00(ctx, sizeof(camera_app_ctx_t));

    scr = lv_obj_create(NULL);
    if (scr == NULL)
    {
        lv_mem_free(ctx);
        return NULL;
    }

    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(scr, 0, 0);
    ctx->screen = scr;

    content_top = system_service_content_top();
    content_bottom = system_service_content_bottom();
    content_h = content_bottom - content_top;
    if (content_h < 40)
    {
        content_h = 40;
    }

    view = lv_obj_create(scr);
    lv_obj_set_size(view, lcd_w - (2 * CAMERA_MARGIN_X), content_h);
    lv_obj_set_pos(view, CAMERA_MARGIN_X, content_top);
    lv_obj_set_style_bg_color(view, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(view, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(view, 1, 0);
    lv_obj_set_style_border_color(view, lv_color_white(), 0);
    lv_obj_set_style_radius(view, 4, 0);
    lv_obj_set_style_pad_all(view, 0, 0);
    lv_obj_set_scrollbar_mode(view, LV_SCROLLBAR_MODE_OFF);
    ctx->view = view;

    img = lv_img_create(view);
    lv_obj_center(img);
    lv_img_set_size_mode(img, LV_IMG_SIZE_MODE_REAL);
    ctx->img = img;

    hint = lv_label_create(view);
    lv_obj_set_style_text_color(hint, lv_color_hex(APP_THEME_TEXT_SECONDARY_HEX), 0);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -2);
    lv_label_set_text(hint, "starting camera...");
    ctx->hint_label = hint;

    ctx->preview_timer = lv_timer_create(_camera_preview_timer_cb,
                                         CAMERA_PREVIEW_PERIOD_MS,
                                         ctx);
    if (ctx->preview_timer == NULL)
    {
        _camera_stop_driver(ctx);
        _camera_shutdown_sensor();
        lv_obj_del(scr);
        lv_mem_free(ctx);
        return NULL;
    }

    if (!_camera_start_worker_task(ctx, false))
    {
        lv_label_set_text(hint, "camera worker start failed");
    }

    if (!_camera_start_input_task())
    {
        if (ctx->preview_timer != NULL)
        {
            lv_timer_del(ctx->preview_timer);
            ctx->preview_timer = NULL;
        }

        _camera_stop_driver(ctx);
        _camera_shutdown_sensor();
        lv_obj_del(scr);
        lv_mem_free(ctx);
        return NULL;
    }

    lv_obj_add_event_cb(scr, _camera_delete_cb, LV_EVENT_DELETE, ctx);
    s_camera_ctx = ctx;
    return scr;
}
