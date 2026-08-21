#include "camera_ui.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "bsp/delay.h"
#include "desktop/desktop_app.h"
#include "peripherals/bf20a6_cam.h"
#include "peripherals/keyboard.h"
#include "service/system_service.h"

#define TAG "CAM_UI"

#define CAMERA_UI_YUV_ORDER_AUTO (0U)
#define CAMERA_UI_YUV_ORDER_YUYV (1U)
#define CAMERA_UI_YUV_ORDER_UYVY (2U)
#define CAMERA_UI_YUV_ORDER_YVYU (3U)
#define CAMERA_UI_YUV_ORDER_VYUY (4U)

#define CAMERA_UI_MARGIN_X 2

#define CAMERA_UI_REFRESH_PERIOD_MS 20U
#define CAMERA_UI_INPUT_SCAN_PERIOD_MS 10U
#define CAMERA_UI_CAPTURE_BACKPRESSURE_DELAY_MS 1U

#define CAMERA_UI_CAPTURE_TASK_STACK_SIZE 6144U
#define CAMERA_UI_CAPTURE_TASK_PRIORITY 5U
#define CAMERA_UI_INPUT_TASK_STACK_SIZE 4096U
#define CAMERA_UI_INPUT_TASK_PRIORITY 4U

#define CAMERA_UI_TASK_STOP_WAIT_RETRY 20U
#define CAMERA_UI_TASK_STOP_WAIT_DELAY_MS 5U

/*
 * brief     : Stop one local task cooperatively, then force-delete on timeout.
 * input     : task_handle/stop_flag pointers.
 * output    : None.
 * type      : private
 */
static void _camera_ui_stop_task(TaskHandle_t *task_handle, volatile bool *stop_flag)
{
    TaskHandle_t handle;
    uint32_t wait_count;

    if ((task_handle == NULL) || (stop_flag == NULL))
    {
        return;
    }

    handle = *task_handle;
    if (handle == NULL)
    {
        return;
    }

    *stop_flag = true;
    for (wait_count = 0U; wait_count < CAMERA_UI_TASK_STOP_WAIT_RETRY; wait_count++)
    {
        if (*task_handle == NULL)
        {
            return;
        }

        delay_ms(CAMERA_UI_TASK_STOP_WAIT_DELAY_MS);
    }

    handle = *task_handle;
    if (handle != NULL)
    {
        vTaskDelete(handle);
        *task_handle = NULL;
    }
}

/*
 * brief     : Allocate buffer memory, preferring PSRAM first.
 * input     : bytes allocation size.
 * output    : Allocated pointer, or NULL on failure.
 * type      : private
 */
static void *_camera_ui_alloc(size_t bytes)
{
    void *ptr;

    if (bytes == 0U)
    {
        return NULL;
    }

    ptr = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (ptr != NULL)
    {
        return ptr;
    }

    return heap_caps_malloc(bytes, MALLOC_CAP_8BIT);
}

/*
 * brief     : Update status label text only when content changes.
 * input     : ctx camera context, text target status string.
 * output    : None.
 * type      : private
 */
static void _camera_ui_set_status(camera_app_ctx_t *ctx, const char *text)
{
    if ((ctx == NULL) || (ctx->status_label == NULL) || (text == NULL))
    {
        return;
    }

    if (strcmp(ctx->status_text, text) == 0)
    {
        return;
    }

    (void)snprintf(ctx->status_text, sizeof(ctx->status_text), "%s", text);
    lv_label_set_text(ctx->status_label, ctx->status_text);
}

/*
 * brief     : Clamp integer value into uint8 range.
 * input     : v signed integer value.
 * output    : Clamped uint8 value.
 * type      : private
 */
static uint8_t _camera_ui_clamp_u8(int v)
{
    if (v < 0)
    {
        return 0U;
    }

    if (v > 255)
    {
        return 255U;
    }

    return (uint8_t)v;
}

/*
 * brief     : Convert one YUV pixel sample to LVGL color.
 * input     : y/u/v components.
 * output    : Converted lv_color_t value.
 * type      : private
 */
static lv_color_t _camera_ui_yuv_to_color(uint8_t y, uint8_t u, uint8_t v)
{
    int c = (int)y - 16;
    int d = (int)u - 128;
    int e = (int)v - 128;
    uint8_t r;
    uint8_t g;
    uint8_t b;

    if (c < 0)
    {
        c = 0;
    }

    r = _camera_ui_clamp_u8((298 * c + 409 * e + 128) >> 8);
    g = _camera_ui_clamp_u8((298 * c - 100 * d - 208 * e + 128) >> 8);
    b = _camera_ui_clamp_u8((298 * c + 516 * d + 128) >> 8);

    return lv_color_make(r, g, b);
}

/*
 * brief     : Map YUV order value to readable name.
 * input     : order YUV order enum value.
 * output    : Constant order name string.
 * type      : private
 */
static const char *_camera_ui_yuv_order_name(uint8_t order)
{
    switch (order)
    {
    case CAMERA_UI_YUV_ORDER_YUYV:
        return "YUYV";
    case CAMERA_UI_YUV_ORDER_UYVY:
        return "UYVY";
    case CAMERA_UI_YUV_ORDER_YVYU:
        return "YVYU";
    case CAMERA_UI_YUV_ORDER_VYUY:
        return "VYUY";
    default:
        return "AUTO";
    }
}

/*
 * brief     : Resolve AUTO parse mode to a concrete YUV order.
 * input     : None.
 * output    : Concrete parser order value.
 * type      : private
 */
static uint8_t _camera_ui_auto_parse_order(void)
{
    uint8_t seq = (uint8_t)(CAM_BF20A6_SENSOR_YUV_SEQ & 0x03U);

    switch (seq)
    {
    case 0U:
        return CAMERA_UI_YUV_ORDER_YUYV;
    case 1U:
        return CAMERA_UI_YUV_ORDER_YVYU;
    case 2U:
        return CAMERA_UI_YUV_ORDER_UYVY;
    case 3U:
        return CAMERA_UI_YUV_ORDER_VYUY;
    default:
        return CAMERA_UI_YUV_ORDER_YUYV;
    }
}

/*
 * brief     : Resolve source crop window by configured cut style.
 * input     : src/dst sizes and cut_style.
 * output    : src_x/src_y copy_w/copy_h values for conversion.
 * type      : private
 */
static void _camera_ui_resolve_crop_window(uint16_t src_w,
                                           uint16_t src_h,
                                           uint16_t dst_w,
                                           uint16_t dst_h,
                                           camera_ui_cut_style_e cut_style,
                                           uint16_t *src_x,
                                           uint16_t *src_y,
                                           uint16_t *copy_w,
                                           uint16_t *copy_h)
{
    uint16_t w;
    uint16_t h;
    uint16_t x;
    uint16_t y;

    w = (src_w < dst_w) ? src_w : dst_w;
    h = (src_h < dst_h) ? src_h : dst_h;
    x = 0U;
    y = 0U;

    if (cut_style == CAMERA_UI_CUT_STYLE_CENTER)
    {
        if (src_w > w)
        {
            x = (uint16_t)((src_w - w) / 2U);
        }
        if (src_h > h)
        {
            y = (uint16_t)((src_h - h) / 2U);
        }
    }

    if (src_x != NULL)
    {
        *src_x = x;
    }
    if (src_y != NULL)
    {
        *src_y = y;
    }
    if (copy_w != NULL)
    {
        *copy_w = w;
    }
    if (copy_h != NULL)
    {
        *copy_h = h;
    }
}

/*
 * brief     : Convert camera frame into LVGL true-color buffer.
 * input     : ctx camera context, fb source frame, dst output buffer, cut_style crop mode.
 * output    : true on success, false on failure.
 * type      : private
 */
static bool _camera_ui_convert_frame(camera_app_ctx_t *ctx,
                                     const camera_fb_t *fb,
                                     lv_color_t *dst,
                                     camera_ui_cut_style_e cut_style)
{
    uint16_t src_x;
    uint16_t src_y;
    uint16_t copy_w;
    uint16_t copy_h;
    uint16_t y;

    if ((ctx == NULL) || (fb == NULL) || (dst == NULL))
    {
        return false;
    }

    _camera_ui_resolve_crop_window((uint16_t)fb->width,
                                   (uint16_t)fb->height,
                                   ctx->frame_w,
                                   ctx->frame_h,
                                   cut_style,
                                   &src_x,
                                   &src_y,
                                   &copy_w,
                                   &copy_h);

    if ((copy_w == 0U) || (copy_h == 0U))
    {
        return false;
    }

    /* Skip full clear when source fully covers destination to reduce per-frame overhead. */
    if ((copy_w < ctx->frame_w) || (copy_h < ctx->frame_h))
    {
        size_t pixel_count = (size_t)ctx->frame_w * (size_t)ctx->frame_h;
        lv_memset_00(dst, pixel_count * sizeof(lv_color_t));
    }

    if (fb->format == PIXFORMAT_YUV422)
    {
        size_t expected = (size_t)fb->width * (size_t)fb->height * 2U;
        uint8_t yuv_order;

        if ((fb->buf == NULL) || (fb->len < expected))
        {
            return false;
        }

        yuv_order = ctx->yuv422_order_cfg;
        if (yuv_order == CAMERA_UI_YUV_ORDER_AUTO)
        {
            yuv_order = _camera_ui_auto_parse_order();
            if (ctx->yuv422_order_detected != yuv_order)
            {
                ctx->yuv422_order_detected = yuv_order;
                ESP_LOGI(TAG, "YUV422 order detected=%s", _camera_ui_yuv_order_name(yuv_order));
            }
            else
            {
                ctx->yuv422_order_detected = yuv_order;
            }
        }
        else
        {
            ctx->yuv422_order_detected = yuv_order;
        }

        for (y = 0U; y < copy_h; y++)
        {
            const uint8_t *src_line = fb->buf + (((size_t)(src_y + y) * (size_t)fb->width + (size_t)src_x) * 2U);
            lv_color_t *dst_line = dst + ((size_t)y * (size_t)ctx->frame_w);
            uint16_t x = 0U;

            for (; (x + 1U) < copy_w; x += 2U)
            {
                uint8_t y0;
                uint8_t y1;
                uint8_t u;
                uint8_t v;

                switch (yuv_order)
                {
                case CAMERA_UI_YUV_ORDER_UYVY:
                    u = src_line[0];
                    y0 = src_line[1];
                    v = src_line[2];
                    y1 = src_line[3];
                    break;
                case CAMERA_UI_YUV_ORDER_YVYU:
                    y0 = src_line[0];
                    v = src_line[1];
                    y1 = src_line[2];
                    u = src_line[3];
                    break;
                case CAMERA_UI_YUV_ORDER_VYUY:
                    v = src_line[0];
                    y0 = src_line[1];
                    u = src_line[2];
                    y1 = src_line[3];
                    break;
                case CAMERA_UI_YUV_ORDER_YUYV:
                default:
                    y0 = src_line[0];
                    u = src_line[1];
                    y1 = src_line[2];
                    v = src_line[3];
                    break;
                }

                dst_line[x] = _camera_ui_yuv_to_color(y0, u, v);
                dst_line[x + 1U] = _camera_ui_yuv_to_color(y1, u, v);
                src_line += 4U;
            }

            if (x < copy_w)
            {
                uint8_t y0;
                uint8_t u;
                uint8_t v;

                switch (yuv_order)
                {
                case CAMERA_UI_YUV_ORDER_UYVY:
                    u = src_line[0];
                    y0 = src_line[1];
                    v = src_line[2];
                    break;
                case CAMERA_UI_YUV_ORDER_YVYU:
                    y0 = src_line[0];
                    v = src_line[1];
                    u = src_line[3];
                    break;
                case CAMERA_UI_YUV_ORDER_VYUY:
                    v = src_line[0];
                    y0 = src_line[1];
                    u = src_line[2];
                    break;
                case CAMERA_UI_YUV_ORDER_YUYV:
                default:
                    y0 = src_line[0];
                    u = src_line[1];
                    v = src_line[3];
                    break;
                }

                dst_line[x] = _camera_ui_yuv_to_color(y0, u, v);
            }
        }

        return true;
    }

    if (fb->format == PIXFORMAT_GRAYSCALE)
    {
        size_t expected = (size_t)fb->width * (size_t)fb->height;

        if ((fb->buf == NULL) || (fb->len < expected))
        {
            return false;
        }

        for (y = 0U; y < copy_h; y++)
        {
            const uint8_t *src_line = fb->buf + ((size_t)(src_y + y) * (size_t)fb->width + (size_t)src_x);
            lv_color_t *dst_line = dst + ((size_t)y * (size_t)ctx->frame_w);
            uint16_t x;

            for (x = 0U; x < copy_w; x++)
            {
                uint8_t gray = src_line[x];
                dst_line[x] = lv_color_make(gray, gray, gray);
            }
        }

        return true;
    }

    if (fb->format == PIXFORMAT_RGB565)
    {
        size_t expected = (size_t)fb->width * (size_t)fb->height * 2U;

        if ((fb->buf == NULL) || (fb->len < expected))
        {
            return false;
        }

        for (y = 0U; y < copy_h; y++)
        {
            const uint16_t *src_line = (const uint16_t *)(fb->buf + (((size_t)(src_y + y) * (size_t)fb->width + (size_t)src_x) * 2U));
            lv_color_t *dst_line = dst + ((size_t)y * (size_t)ctx->frame_w);
            uint16_t x;

            for (x = 0U; x < copy_w; x++)
            {
                uint16_t p = src_line[x];
                uint8_t r = (uint8_t)(((p >> 11) & 0x1FU) << 3);
                uint8_t g = (uint8_t)(((p >> 5) & 0x3FU) << 2);
                uint8_t b = (uint8_t)((p & 0x1FU) << 3);
                dst_line[x] = lv_color_make(r, g, b);
            }
        }

        return true;
    }

    return false;
}

/*
 * brief     : Capture worker task that opens camera and converts frames.
 * input     : param camera_app_ctx_t pointer.
 * output    : None.
 * type      : private
 */
static void _camera_ui_capture_task(void *param)
{
    camera_app_ctx_t *ctx = (camera_app_ctx_t *)param;
    esp_err_t open_ret;

    open_ret = bf20a6_cam_open();
    portENTER_CRITICAL(&ctx->frame_lock);
    ctx->camera_open_ret = open_ret;
    ctx->camera_ready = (open_ret == ESP_OK) ? 1U : 0U;
    ctx->camera_start_done = 1U;
    portEXIT_CRITICAL(&ctx->frame_lock);

    while (!ctx->capture_task_stop)
    {
        camera_fb_t *fb;
        uint8_t back_idx;
        bool frame_pending;
        bool convert_ok;

        if (open_ret != ESP_OK)
        {
            delay_ms(200U);
            open_ret = bf20a6_cam_open();
            portENTER_CRITICAL(&ctx->frame_lock);
            ctx->camera_open_ret = open_ret;
            ctx->camera_ready = (open_ret == ESP_OK) ? 1U : 0U;
            portEXIT_CRITICAL(&ctx->frame_lock);
            continue;
        }

        portENTER_CRITICAL(&ctx->frame_lock);
        frame_pending = (ctx->frame_seq != ctx->frame_seq_shown);
        back_idx = (ctx->newest_idx == 0U) ? 1U : 0U;
        portEXIT_CRITICAL(&ctx->frame_lock);

        /* If UI has not consumed the previous frame, drop work to avoid buffer races. */
        if (frame_pending)
        {
            delay_ms(CAMERA_UI_CAPTURE_BACKPRESSURE_DELAY_MS);
            continue;
        }

        fb = bf20a6_cam_fb_get();
        if (fb == NULL)
        {
            delay_ms(5U);
            continue;
        }

        convert_ok = _camera_ui_convert_frame(ctx, fb, ctx->frame_buf[back_idx], ctx->cut_style);
        bf20a6_cam_fb_return(fb);

        if (convert_ok)
        {
            portENTER_CRITICAL(&ctx->frame_lock);
            ctx->newest_idx = back_idx;
            ctx->frame_ready = 1U;
            ctx->frame_seq++;
            portEXIT_CRITICAL(&ctx->frame_lock);
        }
        else
        {
            delay_ms(5U);
        }
    }

    bf20a6_cam_close();

    portENTER_CRITICAL(&ctx->frame_lock);
    ctx->camera_ready = 0U;
    portEXIT_CRITICAL(&ctx->frame_lock);

    ctx->capture_task_handle = NULL;
    vTaskDelete(NULL);
}

/*
 * brief     : Input worker task handling keyboard events.
 * input     : param camera_app_ctx_t pointer.
 * output    : None.
 * type      : private
 */
static void _camera_ui_input_task(void *param)
{
    camera_app_ctx_t *ctx = (camera_app_ctx_t *)param;
    btn_scan_s btn;
    bool home_requested;

    lv_memset_00(&btn, sizeof(btn));
    home_requested = false;

    while (!ctx->input_task_stop)
    {
        btn_status_e btn_val;

        btn_val = keyboard_scan_event(&btn, CAMERA_UI_INPUT_SCAN_PERIOD_MS);
        if ((btn_val == Btn_Both_Click) && !home_requested)
        {
            home_requested = true;
            desktop_return_to_home();
        }

        delay_ms(CAMERA_UI_INPUT_SCAN_PERIOD_MS);
    }

    ctx->input_task_handle = NULL;
    vTaskDelete(NULL);
}

/*
 * brief     : Start capture worker task.
 * input     : ctx camera context.
 * output    : true if task created, else false.
 * type      : private
 */
static bool _camera_ui_start_capture_task(camera_app_ctx_t *ctx)
{
    BaseType_t task_ok;

    ctx->capture_task_stop = false;
    task_ok = xTaskCreate(_camera_ui_capture_task,
                          "cam_capture",
                          CAMERA_UI_CAPTURE_TASK_STACK_SIZE,
                          ctx,
                          CAMERA_UI_CAPTURE_TASK_PRIORITY,
                          &ctx->capture_task_handle);
    if (task_ok != pdPASS)
    {
        ctx->capture_task_stop = true;
        ctx->capture_task_handle = NULL;
        return false;
    }

    return true;
}

/*
 * brief     : Start input worker task.
 * input     : ctx camera context.
 * output    : true if task created, else false.
 * type      : private
 */
static bool _camera_ui_start_input_task(camera_app_ctx_t *ctx)
{
    BaseType_t task_ok;

    ctx->input_task_stop = false;
    task_ok = xTaskCreate(_camera_ui_input_task,
                          "cam_input",
                          CAMERA_UI_INPUT_TASK_STACK_SIZE,
                          ctx,
                          CAMERA_UI_INPUT_TASK_PRIORITY,
                          &ctx->input_task_handle);
    if (task_ok != pdPASS)
    {
        ctx->input_task_stop = true;
        ctx->input_task_handle = NULL;
        return false;
    }

    return true;
}

/*
 * brief     : UI timer callback that updates image and status text.
 * input     : timer LVGL timer object.
 * output    : None.
 * type      : private
 */
static void _camera_ui_timer_cb(lv_timer_t *timer)
{
    camera_app_ctx_t *ctx = (camera_app_ctx_t *)timer->user_data;
    uint32_t frame_seq;
    uint8_t frame_ready;
    uint8_t start_done;
    uint8_t camera_ready;
    esp_err_t open_ret;
    uint8_t newest_idx;
    uint8_t yuv_cfg;
    uint8_t yuv_detected;
    bool new_frame = false;

    if ((ctx == NULL) || (ctx->img == NULL))
    {
        return;
    }

    portENTER_CRITICAL(&ctx->frame_lock);
    frame_seq = ctx->frame_seq;
    frame_ready = ctx->frame_ready;
    start_done = ctx->camera_start_done;
    camera_ready = ctx->camera_ready;
    open_ret = ctx->camera_open_ret;
    newest_idx = ctx->newest_idx;
    yuv_cfg = ctx->yuv422_order_cfg;
    yuv_detected = ctx->yuv422_order_detected;

    if ((frame_ready != 0U) && (frame_seq != ctx->frame_seq_shown))
    {
        ctx->frame_seq_shown = frame_seq;
        ctx->display_idx = newest_idx;
        new_frame = true;
    }
    portEXIT_CRITICAL(&ctx->frame_lock);

    if (new_frame)
    {
        ctx->img_dsc.data = (const uint8_t *)ctx->frame_buf[ctx->display_idx];
        lv_img_set_src(ctx->img, &ctx->img_dsc);
        lv_obj_invalidate(ctx->img);
    }

    if (ctx->status_label == NULL)
    {
        return;
    }

    if (start_done == 0U)
    {
        _camera_ui_set_status(ctx, "Camera opening... BOTH:HOME");
        return;
    }

    if (camera_ready == 0U)
    {
        char text[96];
        (void)snprintf(text,
                       sizeof(text),
                       "Camera open failed: %d BOTH:HOME",
                       (int)open_ret);
        _camera_ui_set_status(ctx, text);
        return;
    }

    if ((frame_seq == 0U) || ((frame_seq % 12U) != 0U))
    {
        return;
    }

    {
        char order_text[24];
        char text[96];

        if ((yuv_cfg == CAMERA_UI_YUV_ORDER_AUTO) &&
            (yuv_detected != CAMERA_UI_YUV_ORDER_AUTO))
        {
            (void)snprintf(order_text,
                           sizeof(order_text),
                           "AUTO/%s",
                           _camera_ui_yuv_order_name(yuv_detected));
        }
        else
        {
            (void)snprintf(order_text,
                           sizeof(order_text),
                           "%s",
                           _camera_ui_yuv_order_name(yuv_cfg));
        }

        (void)snprintf(text,
                       sizeof(text),
                       "Fixed cfg parse=%s frame=%lu BOTH:HOME",
                       order_text,
                       (unsigned long)frame_seq);
        _camera_ui_set_status(ctx, text);
    }
}

/*
 * brief     : Release camera UI resources on screen delete event.
 * input     : e LVGL event object.
 * output    : None.
 * type      : private
 */
static void _camera_ui_delete_cb(lv_event_t *e)
{
    camera_app_ctx_t *ctx = (camera_app_ctx_t *)lv_event_get_user_data(e);

    if (ctx == NULL)
    {
        return;
    }

    if (ctx->ui_timer != NULL)
    {
        lv_timer_del(ctx->ui_timer);
        ctx->ui_timer = NULL;
    }

    _camera_ui_stop_task(&ctx->input_task_handle, &ctx->input_task_stop);
    _camera_ui_stop_task(&ctx->capture_task_handle, &ctx->capture_task_stop);

    bf20a6_cam_close();

    if (ctx->frame_buf[0] != NULL)
    {
        heap_caps_free(ctx->frame_buf[0]);
        ctx->frame_buf[0] = NULL;
    }

    if (ctx->frame_buf[1] != NULL)
    {
        heap_caps_free(ctx->frame_buf[1]);
        ctx->frame_buf[1] = NULL;
    }

    lv_mem_free(ctx);
}

/*
 * brief     : Create camera screen and start preview workers.
 * input     : lcd_w/lcd_h display resolution.
 * output    : Created LVGL screen object, or NULL on failure.
 * type      : public
 */
lv_obj_t *camera_create_screen(lv_coord_t lcd_w, lv_coord_t lcd_h)
{
    camera_app_ctx_t *ctx;
    lv_obj_t *scr;
    lv_obj_t *panel;
    lv_obj_t *img;
    lv_coord_t content_top;
    lv_coord_t content_bottom;
    lv_coord_t content_h;
    lv_coord_t panel_w;
    lv_coord_t panel_h;
    lv_coord_t panel_y;
    lv_coord_t img_x;
    lv_coord_t img_y;
    size_t frame_bytes;
    uint8_t yuv_cfg;

    if ((lcd_w <= (2 * CAMERA_UI_MARGIN_X)) || (lcd_h <= (2 * APP_STATUS_BAR_HEIGHT + 20)))
    {
        return NULL;
    }

    ctx = (camera_app_ctx_t *)lv_mem_alloc(sizeof(camera_app_ctx_t));
    if (ctx == NULL)
    {
        return NULL;
    }
    lv_memset_00(ctx, sizeof(camera_app_ctx_t));

    ctx->frame_lock = (portMUX_TYPE)portMUX_INITIALIZER_UNLOCKED;
    ctx->frame_w = (uint16_t)CAMERA_UI_FRAME_WIDTH;
    ctx->frame_h = (uint16_t)CAMERA_UI_FRAME_HEIGHT;
    ctx->cut_style = CAMERA_UI_CUT_STYLE_DEFAULT;

    yuv_cfg = (uint8_t)CAM_BF20A6_YUV_ORDER;
    if (yuv_cfg > CAMERA_UI_YUV_ORDER_VYUY)
    {
        yuv_cfg = CAMERA_UI_YUV_ORDER_AUTO;
    }
    ctx->yuv422_order_cfg = yuv_cfg;
    ctx->yuv422_order_detected = CAMERA_UI_YUV_ORDER_AUTO;

    frame_bytes = (size_t)ctx->frame_w * (size_t)ctx->frame_h * sizeof(lv_color_t);

    ctx->frame_buf[0] = (lv_color_t *)_camera_ui_alloc(frame_bytes);
    ctx->frame_buf[1] = (lv_color_t *)_camera_ui_alloc(frame_bytes);
    if ((ctx->frame_buf[0] == NULL) || (ctx->frame_buf[1] == NULL))
    {
        if (ctx->frame_buf[0] != NULL)
        {
            heap_caps_free(ctx->frame_buf[0]);
        }
        if (ctx->frame_buf[1] != NULL)
        {
            heap_caps_free(ctx->frame_buf[1]);
        }
        lv_mem_free(ctx);
        return NULL;
    }

    lv_memset_00(ctx->frame_buf[0], frame_bytes);
    lv_memset_00(ctx->frame_buf[1], frame_bytes);

    scr = lv_obj_create(NULL);
    if (scr == NULL)
    {
        heap_caps_free(ctx->frame_buf[0]);
        heap_caps_free(ctx->frame_buf[1]);
        lv_mem_free(ctx);
        return NULL;
    }

    ctx->screen = scr;
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(scr, 0, 0);
    lv_obj_set_style_pad_all(scr, 0, 0);

    content_top = system_service_content_top();
    content_bottom = system_service_content_bottom();
    content_h = content_bottom - content_top;
    if (content_h < (lv_coord_t)ctx->frame_h)
    {
        content_h = (lv_coord_t)ctx->frame_h;
    }

    panel_w = lcd_w - (2 * CAMERA_UI_MARGIN_X);
    panel_h = content_h;
    panel_y = content_top;

    panel = lv_obj_create(scr);
    ctx->frame_panel = panel;
    lv_obj_set_size(panel, panel_w, panel_h);
    lv_obj_set_pos(panel, CAMERA_UI_MARGIN_X, panel_y);
    lv_obj_set_style_bg_color(panel, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(panel, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(panel, lv_color_hex(APP_THEME_BORDER_HEX), LV_PART_MAIN);
    lv_obj_set_style_radius(panel, 6, LV_PART_MAIN);
    lv_obj_set_style_pad_all(panel, 0, LV_PART_MAIN);
    lv_obj_set_scrollbar_mode(panel, LV_SCROLLBAR_MODE_OFF);

    img = lv_img_create(panel);
    ctx->img = img;
    lv_obj_set_size(img, (lv_coord_t)ctx->frame_w, (lv_coord_t)ctx->frame_h);

    img_x = (panel_w - (lv_coord_t)ctx->frame_w) / 2;
    img_y = (panel_h - (lv_coord_t)ctx->frame_h) / 2;
    if (img_x < 0)
    {
        img_x = 0;
    }
    if (img_y < 0)
    {
        img_y = 0;
    }
    lv_obj_set_pos(img, img_x, img_y);

    ctx->img_dsc.header.cf = LV_IMG_CF_TRUE_COLOR;
    ctx->img_dsc.header.w = ctx->frame_w;
    ctx->img_dsc.header.h = ctx->frame_h;
    ctx->img_dsc.data_size = frame_bytes;
    ctx->img_dsc.data = (const uint8_t *)ctx->frame_buf[0];
    lv_img_set_src(img, &ctx->img_dsc);

    ESP_LOGI(TAG, "camera ui fixed mode, parse=%s", _camera_ui_yuv_order_name(ctx->yuv422_order_cfg));

    ctx->ui_timer = lv_timer_create(_camera_ui_timer_cb, CAMERA_UI_REFRESH_PERIOD_MS, ctx);
    if (ctx->ui_timer == NULL)
    {
        lv_obj_del(scr);
        heap_caps_free(ctx->frame_buf[0]);
        heap_caps_free(ctx->frame_buf[1]);
        lv_mem_free(ctx);
        return NULL;
    }

    lv_obj_add_event_cb(scr, _camera_ui_delete_cb, LV_EVENT_DELETE, ctx);

    if (!_camera_ui_start_capture_task(ctx))
    {
        lv_obj_del(scr);
        return NULL;
    }

    if (!_camera_ui_start_input_task(ctx))
    {
        lv_obj_del(scr);
        return NULL;
    }

    return scr;
}
