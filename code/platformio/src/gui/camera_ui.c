#include "camera_ui.h"

#define TAG "CAMERA"

static camera_app_ctx_t *s_camera_ctx = NULL;
static TaskHandle_t s_camera_input_task_handle = NULL;
static volatile bool s_camera_input_task_stop = false;

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

#ifdef CAMERA_OBJECT
/*
 * brief: Prepare OV2640 control pins through GPBA and apply a hard-reset pulse.
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

    USER_RETURN_ON_ERROR(gpba02b_pin_write(CAM_IO_PWDN_PORT,
                                           CAM_IO_PWDN_PIN,
                                           true),
                         TAG,
                         "set CAM PWDN high failed");
    delay_ms(2U);

    USER_RETURN_ON_ERROR(gpba02b_pin_write(CAM_IO_PWDN_PORT,
                                           CAM_IO_PWDN_PIN,
                                           false),
                         TAG,
                         "set CAM PWDN low failed");
    delay_ms(2U);

    USER_RETURN_ON_ERROR(gpba02b_pin_write(CAM_IO_RESET_PORT,
                                           CAM_IO_RESET_PIN,
                                           false),
                         TAG,
                         "set CAM RESET low failed");
    delay_ms(2U);

    USER_RETURN_ON_ERROR(gpba02b_pin_write(CAM_IO_RESET_PORT,
                                           CAM_IO_RESET_PIN,
                                           true),
                         TAG,
                         "set CAM RESET high failed");
    delay_ms(20U);

    return ESP_OK;
}
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

    if ((ctx != NULL) && ctx->camera_started)
    {
        return ESP_OK;
    }

    /* Release OV2640 private SCCB ownership before esp_camera_init takes control. */
    ret = ov2640_prepare_preview_start();
    if ((ret != ESP_OK) && (ret != ESP_ERR_NOT_SUPPORTED))
    {
        ESP_LOGW(TAG, "ov2640_prepare_preview_start failed: %d", (int)ret);
    }

    USER_RETURN_ON_ERROR(_camera_prepare_control_pins(),
                         TAG,
                         "_camera_prepare_control_pins failed");

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
    cfg.pixel_format = PIXFORMAT_RGB565;
    cfg.frame_size = FRAMESIZE_QVGA;
    cfg.jpeg_quality = 12;
    cfg.fb_count = 1;
    cfg.fb_location = CAMERA_FB_IN_PSRAM;
    cfg.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
    cfg.sccb_i2c_port = CAM_SCCB_I2C_PORT;

    ret = esp_camera_init(&cfg);
    if (ret != ESP_OK)
    {
        ESP_LOGW(TAG, "esp_camera_init failed: %d", (int)ret);
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
    if ((ctx == NULL) || (ctx->img == NULL) || !ctx->camera_started)
    {
        return;
    }

#ifdef CAMERA_OBJECT
    {
        camera_fb_t *fb;
        size_t frame_bytes;
        esp_err_t ret;

        fb = esp_camera_fb_get();
        if (fb == NULL)
        {
            lv_label_set_text(ctx->hint_label, "camera frame timeout");
            return;
        }

        if ((fb->buf == NULL) || (fb->len == 0U) || (fb->format != PIXFORMAT_RGB565))
        {
            esp_camera_fb_return(fb);
            lv_label_set_text(ctx->hint_label, "unsupported frame format");
            return;
        }

        frame_bytes = fb->width * fb->height * 2U;
        if (frame_bytes > fb->len)
        {
            frame_bytes = fb->len;
        }

        ret = _camera_ensure_frame_buffer(ctx, frame_bytes);
        if (ret != ESP_OK)
        {
            esp_camera_fb_return(fb);
            lv_label_set_text(ctx->hint_label, "frame buffer alloc failed");
            return;
        }

        memcpy(ctx->frame_buf, fb->buf, frame_bytes);

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

    s_camera_input_task_stop = true;
    if (s_camera_input_task_handle != NULL)
    {
        vTaskDelete(s_camera_input_task_handle);
        s_camera_input_task_handle = NULL;
    }

    if (ctx->preview_timer != NULL)
    {
        lv_timer_del(ctx->preview_timer);
        ctx->preview_timer = NULL;
    }

    _camera_stop_driver(ctx);

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
    esp_err_t cam_ret;

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

    cam_ret = _camera_start_driver(ctx);
    if (cam_ret == ESP_OK)
    {
        ctx->preview_timer = lv_timer_create(_camera_preview_timer_cb,
                                             CAMERA_PREVIEW_PERIOD_MS,
                                             ctx);
        if (ctx->preview_timer == NULL)
        {
            _camera_stop_driver(ctx);
            lv_obj_del(scr);
            lv_mem_free(ctx);
            return NULL;
        }
    }
    else
    {
        lv_label_set_text_fmt(hint, "camera init failed (%d)", (int)cam_ret);
    }

    if (!_camera_start_input_task())
    {
        if (ctx->preview_timer != NULL)
        {
            lv_timer_del(ctx->preview_timer);
            ctx->preview_timer = NULL;
        }

        _camera_stop_driver(ctx);
        lv_obj_del(scr);
        lv_mem_free(ctx);
        return NULL;
    }

    lv_obj_add_event_cb(scr, _camera_delete_cb, LV_EVENT_DELETE, ctx);
    s_camera_ctx = ctx;
    return scr;
}

/*
 * brief: Stop camera runtime resources before app is destroyed.
 * input: None.
 * output: None.
 */
void camera_release_resources(void)
{
    _camera_stop_input_task();

    if ((s_camera_ctx != NULL) && (s_camera_ctx->preview_timer != NULL))
    {
        lv_timer_pause(s_camera_ctx->preview_timer);
    }
}

/*
 * brief: Request desktop return directly.
 * input: None.
 * output: None.
 */
void camera_destroy_and_return(void)
{
    desktop_return_to_home();
}
