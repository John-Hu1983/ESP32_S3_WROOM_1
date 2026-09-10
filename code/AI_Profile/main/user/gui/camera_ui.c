#include "camera_ui.h"

#define TAG "camera_ui"

static camera_ui_runtime_s s_camera_runtime;
static portMUX_TYPE s_camera_lock = portMUX_INITIALIZER_UNLOCKED;

typedef enum {
    CAMERA_YUV_ORDER_AUTO = 0,
    CAMERA_YUV_ORDER_YUYV = 1,
    CAMERA_YUV_ORDER_UYVY = 2,
    CAMERA_YUV_ORDER_YVYU = 3,
    CAMERA_YUV_ORDER_VYUY = 4,
} camera_yuv_order_e;

/*
 * brief : _camera_ui_timer_cb.
 * input : see parameters.
 * output: none.
 * type  : private
 */
static void _camera_ui_timer_cb(lv_timer_t* timer);

/*
 * brief : _camera_obj_valid.
 * input : see parameters.
 * output: return value from this function.
 * type  : private
 */
static bool _camera_obj_valid(lv_obj_t* obj)
{
    return (obj != NULL) && lv_obj_is_valid(obj);
}

/*
 * brief : _camera_set_status.
 * input : see parameters.
 * output: none.
 * type  : private
 */
static void _camera_set_status(camera_ui_runtime_s* runtime, const char* text)
{
    if (runtime == NULL) {
        return;
    }

    taskENTER_CRITICAL(&s_camera_lock);
    snprintf(
        runtime->status_text,
        sizeof(runtime->status_text),
        "%s",
        (text != NULL) ? text : ""
    );
    runtime->status_dirty = true;
    taskEXIT_CRITICAL(&s_camera_lock);
}

/*
 * brief : _camera_calc_preview_size.
 * input : see parameters.
 * output: none.
 * type  : private
 */
static void _camera_calc_preview_size(
    lv_coord_t area_w,
    lv_coord_t area_h,
    uint16_t* out_w,
    uint16_t* out_h
)
{
    uint32_t w = (area_w > 0) ? (uint32_t)area_w : 1U;
    uint32_t h = (area_h > 0) ? (uint32_t)area_h : 1U;
    uint32_t preview_h;

    if (w > CAMERA_UI_PREVIEW_MAX_WIDTH) {
        w = CAMERA_UI_PREVIEW_MAX_WIDTH;
    }

    preview_h = (w * 3U) / 4U;
    if (preview_h > h) {
        preview_h = h;
        w = (preview_h * 4U) / 3U;
    }

    if (w == 0U) {
        w = 1U;
    }
    if (preview_h == 0U) {
        preview_h = 1U;
    }

    *out_w = (uint16_t)w;
    *out_h = (uint16_t)preview_h;
}

/*
 * brief : _camera_alloc_preview_buf.
 * input : see parameters.
 * output: return value from this function.
 * type  : private
 */
static bool _camera_alloc_preview_buf(camera_ui_runtime_s* runtime)
{
    size_t bytes;

    if ((runtime == NULL) || (runtime->preview_w == 0U) || (runtime->preview_h == 0U)) {
        return false;
    }

    bytes = (size_t)runtime->preview_w * (size_t)runtime->preview_h * sizeof(uint16_t);
    runtime->preview_rgb565 =
        (uint16_t*)heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (runtime->preview_rgb565 == NULL) {
        runtime->preview_rgb565 =
            (uint16_t*)heap_caps_malloc(bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    if (runtime->preview_rgb565 == NULL) {
        return false;
    }

    memset(runtime->preview_rgb565, 0, bytes);
    return true;
}

/*
 * brief : _camera_free_preview_buf.
 * input : see parameters.
 * output: none.
 * type  : private
 */
static void _camera_free_preview_buf(camera_ui_runtime_s* runtime)
{
    if ((runtime != NULL) && (runtime->preview_rgb565 != NULL)) {
        heap_caps_free(runtime->preview_rgb565);
        runtime->preview_rgb565 = NULL;
    }
}

/*
 * brief : _camera_stop_task.
 * input : see parameters.
 * output: none.
 * type  : private
 */
static void _camera_stop_task(camera_ui_runtime_s* runtime)
{
    if ((runtime == NULL) || (runtime->task_handle == NULL)) {
        return;
    }

    runtime->stop_requested = true;
    for (uint32_t i = 0; i < 20U; ++i) {
        if (runtime->task_handle == NULL) {
            break;
        }
        delay_ms(CAMERA_UI_TASK_PERIOD_MS);
    }

    if (runtime->task_handle != NULL) {
        vTaskDelete(runtime->task_handle);
        runtime->task_handle = NULL;
    }

    runtime->stop_requested = false;
}

/*
 * brief : _camera_clip_u8.
 * input : see parameters.
 * output: return value from this function.
 * type  : private
 */
static uint8_t _camera_clip_u8(int32_t val)
{
    if (val < 0) {
        return 0U;
    }
    if (val > 255) {
        return 255U;
    }
    return (uint8_t)val;
}

/*
 * brief : _camera_yuv_to_rgb565_swapped.
 * input : see parameters.
 * output: return value from this function.
 * type  : private
 */
static uint16_t _camera_yuv_to_rgb565_swapped(uint8_t y, uint8_t u, uint8_t v)
{
    int32_t c = (int32_t)y - 16;
    int32_t d = (int32_t)u - 128;
    int32_t e = (int32_t)v - 128;
    int32_t r;
    int32_t g;
    int32_t b;
    uint16_t rgb565;

    if (c < 0) {
        c = 0;
    }

    r = (298 * c + 409 * e + 128) >> 8;
    g = (298 * c - 100 * d - 208 * e + 128) >> 8;
    b = (298 * c + 516 * d + 128) >> 8;

    rgb565 =
        (uint16_t)(((_camera_clip_u8(r) & 0xF8U) << 8)
                   | ((_camera_clip_u8(g) & 0xFCU) << 3) | (_camera_clip_u8(b) >> 3));

    return (uint16_t)((rgb565 << 8) | (rgb565 >> 8));
}

/*
 * brief : _camera_unpack_yuv422.
 * input : see parameters.
 * output: none.
 * type  : private
 */
static void _camera_unpack_yuv422(
    uint8_t order,
    const uint8_t* src4,
    bool is_even,
    uint8_t* y,
    uint8_t* u,
    uint8_t* v
)
{
    uint8_t b0 = src4[0];
    uint8_t b1 = src4[1];
    uint8_t b2 = src4[2];
    uint8_t b3 = src4[3];

    switch (order) {
    case CAMERA_YUV_ORDER_YUYV:
        *y = is_even ? b0 : b2;
        *u = b1;
        *v = b3;
        break;
    case CAMERA_YUV_ORDER_UYVY:
        *y = is_even ? b1 : b3;
        *u = b0;
        *v = b2;
        break;
    case CAMERA_YUV_ORDER_VYUY:
        *y = is_even ? b1 : b3;
        *u = b2;
        *v = b0;
        break;
    case CAMERA_YUV_ORDER_AUTO:
    case CAMERA_YUV_ORDER_YVYU:
    default:
        *y = is_even ? b0 : b2;
        *u = b3;
        *v = b1;
        break;
    }
}

/*
 * brief : _camera_convert_yuv422_to_preview.
 * input : see parameters.
 * output: return value from this function.
 * type  : private
 */
static esp_err_t
_camera_convert_yuv422_to_preview(camera_ui_runtime_s* runtime, const camera_fb_t* fb)
{
    uint32_t src_w;
    uint32_t src_h;
    uint32_t dst_w;
    uint32_t dst_h;
    uint8_t order;

    if ((runtime == NULL) || (fb == NULL) || (fb->buf == NULL)
        || (runtime->preview_rgb565 == NULL)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (fb->format != PIXFORMAT_YUV422) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    src_w = (uint32_t)fb->width;
    src_h = (uint32_t)fb->height;
    dst_w = (uint32_t)runtime->preview_w;
    dst_h = (uint32_t)runtime->preview_h;

    if ((src_w < 2U) || (src_h == 0U) || ((src_w & 0x01U) != 0U) || (dst_w == 0U)
        || (dst_h == 0U)) {
        return ESP_ERR_INVALID_SIZE;
    }

    order = (uint8_t)CAM_BF20A6_YUV_ORDER;
    if (order > (uint8_t)CAMERA_YUV_ORDER_VYUY) {
        order = (uint8_t)CAMERA_YUV_ORDER_YVYU;
    }
    if (order == (uint8_t)CAMERA_YUV_ORDER_AUTO) {
        order = (uint8_t)CAMERA_YUV_ORDER_YVYU;
    }

    for (uint32_t dy = 0; dy < dst_h; ++dy) {
        uint32_t sy = (dy * src_h) / dst_h;
        const uint8_t* src_row = fb->buf + (sy * src_w * 2U);
        uint16_t* dst_row = runtime->preview_rgb565 + (dy * dst_w);

        for (uint32_t dx = 0; dx < dst_w; ++dx) {
            uint32_t sx = (dx * src_w) / dst_w;
            uint32_t sx_pair = sx & ~1U;
            const uint8_t* src4 = src_row + (sx_pair * 2U);
            bool is_even = ((sx & 1U) == 0U);
            uint8_t y;
            uint8_t u;
            uint8_t v;

            _camera_unpack_yuv422(order, src4, is_even, &y, &u, &v);
            dst_row[dx] = _camera_yuv_to_rgb565_swapped(y, u, v);
        }
    }

    return ESP_OK;
}

/*
 * brief : _camera_capture_once.
 * input : see parameters.
 * output: return value from this function.
 * type  : private
 */
static esp_err_t _camera_capture_once(camera_ui_runtime_s* runtime)
{
    camera_fb_t* fb;
    esp_err_t ret;

    if (runtime == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    fb = bf20a6_cam_fb_get();
    if (fb == NULL) {
        return ESP_ERR_NOT_FOUND;
    }

    ret = _camera_convert_yuv422_to_preview(runtime, fb);
    bf20a6_cam_fb_return(fb);

    if (ret == ESP_OK) {
        taskENTER_CRITICAL(&s_camera_lock);
        runtime->frame_dirty = true;
        taskEXIT_CRITICAL(&s_camera_lock);
    }

    return ret;
}

/*
 * brief : _camera_ui_task.
 * input : see parameters.
 * output: none.
 * type  : private
 */
static void _camera_ui_task(void* param)
{
    camera_ui_runtime_s* runtime = (camera_ui_runtime_s*)param;
    uint32_t capture_elapsed_ms = CAMERA_UI_CAPTURE_PERIOD_MS;
    bool format_warned = false;
    esp_err_t ret;

    _camera_set_status(runtime, "Initializing camera...");
    ret = bf20a6_cam_open();
    if (ret != ESP_OK) {
        char msg[CAMERA_UI_STATUS_TEXT_LEN];
        snprintf(msg, sizeof(msg), "Camera init failed (%d)", (int)ret);
        _camera_set_status(runtime, msg);
    }
    else {
        runtime->camera_ready = true;
        _camera_set_status(runtime, "Camera preview running");
    }

    while (!runtime->stop_requested) {
        btn_status_e btn_val =
            button_scan_state(&runtime->button_scan, CAMERA_UI_TASK_PERIOD_MS);
        if ((btn_val == Btn_Both_Click) && (runtime->home_cb != NULL)) {
            runtime->home_cb(runtime->home_user_ctx);
        }

        if (runtime->camera_ready
            && (capture_elapsed_ms >= CAMERA_UI_CAPTURE_PERIOD_MS)) {
            ret = _camera_capture_once(runtime);
            capture_elapsed_ms = 0U;

            if ((ret == ESP_ERR_NOT_SUPPORTED) && !format_warned) {
                format_warned = true;
                _camera_set_status(runtime, "Unsupported camera format");
            }
        }

        delay_ms(CAMERA_UI_TASK_PERIOD_MS);
        capture_elapsed_ms += CAMERA_UI_TASK_PERIOD_MS;
    }

    if (runtime->camera_ready) {
        bf20a6_cam_close();
        runtime->camera_ready = false;
    }

    runtime->task_handle = NULL;
    vTaskDelete(NULL);
}

/*
 * brief : _camera_ui_timer_cb.
 * input : see parameters.
 * output: none.
 * type  : private
 */
static void _camera_ui_timer_cb(lv_timer_t* timer)
{
    camera_ui_runtime_s* runtime;
    bool frame_dirty = false;
    bool status_dirty = false;
    char status_text[CAMERA_UI_STATUS_TEXT_LEN];

    if (timer == NULL) {
        return;
    }

    runtime = (camera_ui_runtime_s*)lv_timer_get_user_data(timer);
    if (runtime == NULL) {
        return;
    }

    if (!_camera_obj_valid(runtime->root) || !_camera_obj_valid(runtime->preview_canvas)
        || !_camera_obj_valid(runtime->status_label)) {
        return;
    }

    taskENTER_CRITICAL(&s_camera_lock);
    frame_dirty = runtime->frame_dirty;
    runtime->frame_dirty = false;
    if (runtime->status_dirty) {
        snprintf(status_text, sizeof(status_text), "%s", runtime->status_text);
        runtime->status_dirty = false;
        status_dirty = true;
    }
    taskEXIT_CRITICAL(&s_camera_lock);

    if (status_dirty) {
        lv_label_set_text(runtime->status_label, status_text);
    }

    if (frame_dirty) {
        lv_obj_invalidate(runtime->preview_canvas);
    }
}

/*
 * brief : Create the camera page root screen object.
 * input : lcd_w - LCD width; lcd_h - LCD height.
 * output: Created LVGL screen object.
 * type  : public
 */
lv_obj_t* camera_create_screen(
    lv_obj_t* parent,
    lv_coord_t area_w,
    lv_coord_t area_h,
    ui_menu_home_cb_t home_cb,
    void* home_user_ctx
)
{
    lv_obj_t* screen;
    BaseType_t task_ok;

    if (parent == NULL) {
        return NULL;
    }

    if (s_camera_runtime.ui_sync_timer != NULL) {
        lv_timer_delete(s_camera_runtime.ui_sync_timer);
        s_camera_runtime.ui_sync_timer = NULL;
    }
    _camera_stop_task(&s_camera_runtime);
    bf20a6_cam_close();
    _camera_free_preview_buf(&s_camera_runtime);
    if (_camera_obj_valid(s_camera_runtime.root)) {
        lv_obj_del(s_camera_runtime.root);
    }

    memset(&s_camera_runtime, 0, sizeof(s_camera_runtime));
    s_camera_runtime.home_cb = home_cb;
    s_camera_runtime.home_user_ctx = home_user_ctx;
    s_camera_runtime.button_scan = (btn_scan_s){ 0 };
    _camera_calc_preview_size(
        area_w,
        area_h,
        &s_camera_runtime.preview_w,
        &s_camera_runtime.preview_h
    );

    screen = lv_obj_create(parent);
    lv_obj_set_size(screen, area_w, area_h);
    lv_obj_set_pos(screen, 0, 0);
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(screen, 0, 0);
    lv_obj_set_style_radius(screen, 0, 0);
    lv_obj_set_style_pad_all(screen, 0, 0);

    s_camera_runtime.preview_canvas = lv_canvas_create(screen);
    lv_obj_set_size(
        s_camera_runtime.preview_canvas,
        s_camera_runtime.preview_w,
        s_camera_runtime.preview_h
    );
    lv_obj_align(s_camera_runtime.preview_canvas, LV_ALIGN_CENTER, 0, 0);

    if (!_camera_alloc_preview_buf(&s_camera_runtime)) {
        lv_obj_del(screen);
        ESP_LOGE(TAG, "preview buffer alloc failed");
        return NULL;
    }

    lv_canvas_set_buffer(
        s_camera_runtime.preview_canvas,
        s_camera_runtime.preview_rgb565,
        s_camera_runtime.preview_w,
        s_camera_runtime.preview_h,
        LV_COLOR_FORMAT_RGB565_SWAPPED
    );
    lv_canvas_fill_bg(s_camera_runtime.preview_canvas, lv_color_black(), LV_OPA_COVER);

    s_camera_runtime.status_label = lv_label_create(screen);
    lv_obj_set_style_text_color(s_camera_runtime.status_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(s_camera_runtime.status_label, &DESKTOP_TEXT_FONT, 0);
    lv_obj_align(s_camera_runtime.status_label, LV_ALIGN_BOTTOM_MID, 0, -2);
    lv_label_set_text(s_camera_runtime.status_label, "Initializing camera...");

    snprintf(
        s_camera_runtime.status_text,
        sizeof(s_camera_runtime.status_text),
        "%s",
        "Initializing camera..."
    );
    s_camera_runtime.root = screen;

    s_camera_runtime.ui_sync_timer = lv_timer_create(
        _camera_ui_timer_cb,
        CAMERA_UI_SYNC_PERIOD_MS,
        &s_camera_runtime
    );
    if (s_camera_runtime.ui_sync_timer == NULL) {
        s_camera_runtime.root = NULL;
        s_camera_runtime.preview_canvas = NULL;
        s_camera_runtime.status_label = NULL;
        _camera_free_preview_buf(&s_camera_runtime);
        lv_obj_del(screen);
        ESP_LOGE(TAG, "lv_timer_create failed");
        return NULL;
    }

    task_ok = xTaskCreate(
        _camera_ui_task,
        "camera_ui",
        CAMERA_UI_TASK_STACK_SIZE,
        &s_camera_runtime,
        5,
        &s_camera_runtime.task_handle
    );
    if (task_ok != pdPASS) {
        s_camera_runtime.task_handle = NULL;
        if (s_camera_runtime.ui_sync_timer != NULL) {
            lv_timer_delete(s_camera_runtime.ui_sync_timer);
            s_camera_runtime.ui_sync_timer = NULL;
        }
        s_camera_runtime.root = NULL;
        s_camera_runtime.preview_canvas = NULL;
        s_camera_runtime.status_label = NULL;
        _camera_free_preview_buf(&s_camera_runtime);
        lv_obj_del(screen);
        ESP_LOGE(TAG, "xTaskCreate failed");
        return NULL;
    }

    return screen;
}

/*
 * brief : camera_destroy_screen.
 * input : see parameters.
 * output: none.
 * type  : public
 */
void camera_destroy_screen(lv_obj_t* screen)
{
    if (s_camera_runtime.ui_sync_timer != NULL) {
        lv_timer_delete(s_camera_runtime.ui_sync_timer);
        s_camera_runtime.ui_sync_timer = NULL;
    }

    _camera_stop_task(&s_camera_runtime);
    bf20a6_cam_close();
    _camera_free_preview_buf(&s_camera_runtime);

    taskENTER_CRITICAL(&s_camera_lock);
    s_camera_runtime.home_cb = NULL;
    s_camera_runtime.home_user_ctx = NULL;
    s_camera_runtime.button_scan = (btn_scan_s){ 0 };
    s_camera_runtime.root = NULL;
    s_camera_runtime.preview_canvas = NULL;
    s_camera_runtime.status_label = NULL;
    s_camera_runtime.preview_w = 0U;
    s_camera_runtime.preview_h = 0U;
    s_camera_runtime.stop_requested = false;
    s_camera_runtime.camera_ready = false;
    s_camera_runtime.frame_dirty = false;
    s_camera_runtime.status_dirty = false;
    s_camera_runtime.status_text[0] = '\0';
    taskEXIT_CRITICAL(&s_camera_lock);

    if ((screen != NULL) && lv_obj_is_valid(screen)) {
        lv_obj_del(screen);
    }
}
