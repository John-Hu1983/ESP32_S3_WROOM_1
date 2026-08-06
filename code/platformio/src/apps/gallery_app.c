#include "gallery_app.h"

#define TAG "GALLERY"

static gallery_app_ctx_t *s_gallery_ctx = NULL;
static TaskHandle_t s_gallery_input_task_handle = NULL;
static volatile bool s_gallery_input_task_stop = false;
static volatile bool s_gallery_home_requested = false;
static volatile bool s_gallery_nav_pending = false;

/*
 * brief: Release gallery picture pools and reset selection state.
 * input: ctx - gallery context pointer.
 * output: None.
 */
static void _gallery_free_pic_pool(gallery_app_ctx_t *ctx)
{
    if (ctx == NULL)
    {
        return;
    }

    pic_free_pool(&ctx->items,
                  &ctx->item_count,
                  &ctx->path_pool,
                  &ctx->path_pool_size,
                  &ctx->loaded_idx);
    ctx->selected_idx = 0U;
}

/*
 * brief: Update gallery image widget according to current selected image index.
 * input: ctx - gallery context.
 * output: None.
 */
static void _gallery_refresh_image(gallery_app_ctx_t *ctx)
{
    const char *file_name;
    const char *last_sep;
    char status_text[64];
    size_t try_idx;
    size_t i;
    bool loaded;
    int n;

    if ((ctx == NULL) || (ctx->img == NULL) || (ctx->hint_label == NULL))
    {
        return;
    }

    if ((ctx->items == NULL) || (ctx->item_count == 0U))
    {
        lv_label_set_text(ctx->hint_label, "no image found");
        lv_img_set_src(ctx->img, NULL);
        return;
    }

    if (ctx->selected_idx >= ctx->item_count)
    {
        ctx->selected_idx = 0U;
    }

    loaded = false;
    for (i = 0U; i < ctx->item_count; i++)
    {
        esp_err_t load_ret;

        try_idx = (ctx->selected_idx + i) % ctx->item_count;
        load_ret = pic_decode_item(ctx->items,
                                   ctx->item_count,
                                   try_idx,
                                   &ctx->loaded_idx);
        if (load_ret == ESP_OK)
        {
            ctx->selected_idx = try_idx;
            loaded = true;
            break;
        }

        ESP_LOGW(TAG,
                 "skip image idx=%u fmt=%s path=%s err=%d",
                 (unsigned)try_idx,
                 pic_format_name(ctx->items[try_idx].format),
                 ctx->items[try_idx].path,
                 (int)load_ret);
    }

    if (!loaded)
    {
        lv_label_set_text(ctx->hint_label, "no decodable image");
        lv_img_set_src(ctx->img, NULL);
        return;
    }

    lv_img_set_src(ctx->img, &ctx->items[ctx->selected_idx].img_dsc);
    lv_obj_center(ctx->img);
    lv_obj_invalidate(ctx->view);

    last_sep = strrchr(ctx->items[ctx->selected_idx].path, '/');
    if (last_sep == NULL)
    {
        file_name = ctx->items[ctx->selected_idx].path;
    }
    else
    {
        file_name = last_sep + 1;
    }

    n = snprintf(status_text,
                 sizeof(status_text),
                 "%u/%u %s",
                 (unsigned)(ctx->selected_idx + 1U),
                 (unsigned)ctx->item_count,
                 file_name);
    if ((n <= 0) || ((size_t)n >= sizeof(status_text)))
    {
        lv_label_set_text(ctx->hint_label, "image view");
    }
    else
    {
        lv_label_set_text(ctx->hint_label, status_text);
    }
}

/*
 * brief: Select previous image and refresh asynchronously.
 * input: user_data - unused.
 * output: None.
 */
static void _gallery_prev_async(void *user_data)
{
    (void)user_data;

    if ((s_gallery_ctx == NULL) || (s_gallery_ctx->item_count == 0U))
    {
        return;
    }

    if (s_gallery_ctx->selected_idx == 0U)
    {
        s_gallery_ctx->selected_idx = s_gallery_ctx->item_count - 1U;
    }
    else
    {
        s_gallery_ctx->selected_idx--;
    }

    _gallery_refresh_image(s_gallery_ctx);
    s_gallery_nav_pending = false;
}

/*
 * brief: Select next image and refresh asynchronously.
 * input: user_data - unused.
 * output: None.
 */
static void _gallery_next_async(void *user_data)
{
    (void)user_data;

    if ((s_gallery_ctx == NULL) || (s_gallery_ctx->item_count == 0U))
    {
        return;
    }

    s_gallery_ctx->selected_idx = (s_gallery_ctx->selected_idx + 1U) % s_gallery_ctx->item_count;
    _gallery_refresh_image(s_gallery_ctx);
    s_gallery_nav_pending = false;
}

/*
 * brief: Input task for gallery app key navigation and return-home handling.
 * input: param - unused task parameter.
 * output: None.
 */
static void _gallery_input_task(void *param)
{
    btn_scan_s btn;

    (void)param;
    lv_memset_00(&btn, sizeof(btn));
    s_gallery_home_requested = false;
    s_gallery_nav_pending = false;

    while (!s_gallery_input_task_stop)
    {
        btn_status_e btn_val;

        btn_val = keyboard_scan_event(&btn, GALLERY_INPUT_SCAN_PERIOD_MS);
        if (btn_val == Btn_Up_Click)
        {
            if (!s_gallery_nav_pending)
            {
                s_gallery_nav_pending = true;
                if (lv_async_call(_gallery_prev_async, NULL) != LV_RES_OK)
                {
                    s_gallery_nav_pending = false;
                }
            }
        }
        else if (btn_val == Btn_Down_Click)
        {
            if (!s_gallery_nav_pending)
            {
                s_gallery_nav_pending = true;
                if (lv_async_call(_gallery_next_async, NULL) != LV_RES_OK)
                {
                    s_gallery_nav_pending = false;
                }
            }
        }
        else if ((btn_val == Btn_Both_Click) && !s_gallery_home_requested)
        {
            s_gallery_home_requested = true;
            desktop_app_return_to_home();
        }

        delay_ms(GALLERY_INPUT_SCAN_PERIOD_MS);
    }

    s_gallery_input_task_handle = NULL;
    vTaskDelete(NULL);
}

/*
 * brief: Start gallery input task that handles key events in sub-app mode.
 * input: None.
 * output: true on success; otherwise false.
 */
static bool _gallery_start_input_task(void)
{
    BaseType_t task_ok;

    s_gallery_input_task_stop = false;
    s_gallery_home_requested = false;

    task_ok = xTaskCreate(_gallery_input_task,
                          "gallery_input",
                          GALLERY_INPUT_TASK_STACK_SIZE,
                          NULL,
                          GALLERY_INPUT_TASK_PRIORITY,
                          &s_gallery_input_task_handle);
    if (task_ok != pdPASS)
    {
        ESP_LOGE(TAG, "xTaskCreate gallery_input failed");
        s_gallery_input_task_stop = true;
        s_gallery_input_task_handle = NULL;
        return false;
    }

    return true;
}

/*
 * brief: Stop gallery input task and force delete on timeout.
 * input: None.
 * output: None.
 */
static void _gallery_stop_input_task(void)
{
    TaskHandle_t handle;
    uint32_t wait_count;

    handle = s_gallery_input_task_handle;
    if (handle == NULL)
    {
        return;
    }

    s_gallery_input_task_stop = true;
    for (wait_count = 0U; wait_count < 20U; wait_count++)
    {
        if (s_gallery_input_task_handle == NULL)
        {
            return;
        }

        delay_ms(5U);
    }

    handle = s_gallery_input_task_handle;
    if (handle != NULL)
    {
        vTaskDelete(handle);
        s_gallery_input_task_handle = NULL;
    }
}

/*
 * brief: Cleanup callback when gallery screen object is deleted.
 * input: e - LVGL delete event.
 * output: None.
 */
static void _gallery_delete_cb(lv_event_t *e)
{
    lv_obj_t *target;
    gallery_app_ctx_t *ctx;

    target = lv_event_get_target(e);
    ctx = (gallery_app_ctx_t *)lv_event_get_user_data(e);
    if (ctx == NULL)
    {
        return;
    }

    s_gallery_input_task_stop = true;
    if (s_gallery_input_task_handle != NULL)
    {
        vTaskDelete(s_gallery_input_task_handle);
        s_gallery_input_task_handle = NULL;
    }

    if ((target != NULL) && (s_gallery_ctx == ctx) && (s_gallery_ctx->screen == target))
    {
        s_gallery_ctx = NULL;
    }

    _gallery_free_pic_pool(ctx);
    lv_mem_free(ctx);
}

/*
 * brief: Build gallery UI in content area and load picture path pool.
 * input: lcd_w/lcd_h - active display resolution.
 * output: Gallery screen object on success; otherwise NULL.
 */
lv_obj_t *gallery_app_create_screen(lv_coord_t lcd_w, lv_coord_t lcd_h)
{
    gallery_app_ctx_t *ctx;
    lv_obj_t *scr;
    lv_obj_t *view;
    lv_obj_t *img;
    lv_obj_t *hint;
    lv_coord_t content_top;
    lv_coord_t content_bottom;
    lv_coord_t content_h;
    esp_err_t ret;

    if ((lcd_w <= (2 * GALLERY_MARGIN_X)) || (lcd_h <= (2 * APP_STATUS_BAR_HEIGHT + 20)))
    {
        return NULL;
    }

    ctx = (gallery_app_ctx_t *)lv_mem_alloc(sizeof(gallery_app_ctx_t));
    if (ctx == NULL)
    {
        return NULL;
    }
    lv_memset_00(ctx, sizeof(gallery_app_ctx_t));
    ctx->loaded_idx = (size_t)-1;

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

    content_top = app_status_bar_content_top();
    content_bottom = app_status_bar_content_bottom();
    content_h = content_bottom - content_top;
    if (content_h < 40)
    {
        content_h = 40;
    }

    view = lv_obj_create(scr);
    lv_obj_set_size(view, lcd_w - (2 * GALLERY_MARGIN_X), content_h);
    lv_obj_set_pos(view, GALLERY_MARGIN_X, content_top);
    lv_obj_set_style_bg_color(view, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(view, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(view, 1, 0);
    lv_obj_set_style_border_color(view, lv_color_white(), 0);
    lv_obj_set_style_radius(view, 4, 0);
    lv_obj_set_style_pad_all(view, 4, 0);
    lv_obj_set_scrollbar_mode(view, LV_SCROLLBAR_MODE_OFF);
    ctx->view = view;

    img = lv_img_create(view);
    lv_obj_center(img);
    lv_img_set_size_mode(img, LV_IMG_SIZE_MODE_REAL);
    ctx->img = img;

    hint = lv_label_create(view);
    lv_obj_set_style_text_color(hint, lv_color_hex(APP_THEME_TEXT_SECONDARY_HEX), 0);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -2);
    lv_label_set_text(hint, "scanning images...");
    ctx->hint_label = hint;

    ret = pic_collect_paths(&ctx->items,
                            &ctx->item_count,
                            &ctx->path_pool,
                            &ctx->path_pool_size);
    if (ret == ESP_OK)
    {
        ctx->selected_idx = 0U;
        _gallery_refresh_image(ctx);
        ESP_LOGI(TAG,
                 "image list ready, count=%u, path_pool=%u",
                 (unsigned)ctx->item_count,
                 (unsigned)ctx->path_pool_size);
    }
    else
    {
        _gallery_free_pic_pool(ctx);
        lv_label_set_text(hint, "no image found in assets");
        ESP_LOGW(TAG, "image scan failed: %d", (int)ret);
    }

    if (!_gallery_start_input_task())
    {
        lv_obj_del(scr);
        _gallery_free_pic_pool(ctx);
        lv_mem_free(ctx);
        return NULL;
    }

    lv_obj_add_event_cb(scr, _gallery_delete_cb, LV_EVENT_DELETE, ctx);
    s_gallery_ctx = ctx;
    return scr;
}

/*
 * brief: Stop gallery runtime resources before app is destroyed.
 * input: None.
 * output: None.
 */
void gallery_app_release_resources(void)
{
    _gallery_stop_input_task();
}

/*
 * brief: Request desktop return directly.
 * input: None.
 * output: None.
 */
void gallery_app_destroy_and_return(void)
{
    desktop_app_return_to_home();
}
