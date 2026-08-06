#include "gallery_app.h"

#define TAG "GALLERY"

static gallery_app_ctx_t *s_gallery_ctx = NULL;
static TaskHandle_t s_gallery_input_task_handle = NULL;
static volatile bool s_gallery_input_task_stop = false;
static volatile bool s_gallery_home_requested = false;
static volatile bool s_gallery_nav_pending = false;

/*
 * brief: Allocate gallery memory from PSRAM first, then fallback to internal RAM.
 * input: bytes - requested byte count.
 * output: Allocated pointer on success; otherwise NULL.
 */
static void *_gallery_psram_alloc(size_t bytes)
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
 * brief: Check whether path ends with .png (case-insensitive for ASCII letters).
 * input: path - full file path.
 * output: true when suffix matches .png/.PNG.
 */
static bool _gallery_path_is_png(const char *path)
{
    size_t len;
    const char *suffix;

    if (path == NULL)
    {
        return false;
    }

    len = strlen(path);
    if (len < 4U)
    {
        return false;
    }

    suffix = path + (len - 4U);
    if (suffix[0] != '.')
    {
        return false;
    }

    return ((suffix[1] == 'p') || (suffix[1] == 'P')) &&
           ((suffix[2] == 'n') || (suffix[2] == 'N')) &&
           ((suffix[3] == 'g') || (suffix[3] == 'G'));
}

/*
 * brief: Validate PNG magic bytes.
 * input: data - image bytes; data_size - byte length.
 * output: true when data starts with PNG signature.
 */
static bool _gallery_is_png_magic(const uint8_t *data, size_t data_size)
{
    static const uint8_t kPngMagic[8] = {0x89U, 0x50U, 0x4EU, 0x47U, 0x0DU, 0x0AU, 0x1AU, 0x0AU};

    if ((data == NULL) || (data_size < sizeof(kPngMagic)))
    {
        return false;
    }

    return (memcmp(data, kPngMagic, sizeof(kPngMagic)) == 0);
}

/*
 * brief: Release currently loaded PNG decode source buffer and cache reference.
 * input: ctx - gallery context.
 * output: None.
 */
static void _gallery_release_loaded_png(gallery_app_ctx_t *ctx)
{
    if (ctx == NULL)
    {
        return;
    }

    if ((ctx->items != NULL) && (ctx->loaded_idx < ctx->item_count))
    {
        lv_img_cache_invalidate_src(&ctx->items[ctx->loaded_idx].img_dsc);
    }

    ctx->loaded_idx = (size_t)-1;
}

/*
 * brief: Release one decoded PNG cache entry and clear its descriptor.
 * input: item - gallery image cache item.
 * output: None.
 */
static void _gallery_free_decoded_item(gallery_png_item_t *item)
{
    if (item == NULL)
    {
        return;
    }

    if (item->decoded_data != NULL)
    {
        heap_caps_free(item->decoded_data);
        item->decoded_data = NULL;
    }

    item->decoded_size = 0U;
    item->decoded_ready = false;
    memset(&item->img_dsc, 0, sizeof(lv_img_dsc_t));
}

/*
 * brief: Free PNG pool allocations inside gallery context.
 * input: ctx - gallery context pointer.
 * output: None.
 */
static void _gallery_free_png_pool(gallery_app_ctx_t *ctx)
{
    size_t i;

    if (ctx == NULL)
    {
        return;
    }

    _gallery_release_loaded_png(ctx);

    if (ctx->items != NULL)
    {
        for (i = 0U; i < ctx->item_count; i++)
        {
            _gallery_free_decoded_item(&ctx->items[i]);
        }
    }

    if (ctx->path_pool != NULL)
    {
        heap_caps_free(ctx->path_pool);
        ctx->path_pool = NULL;
    }

    if (ctx->items != NULL)
    {
        heap_caps_free(ctx->items);
        ctx->items = NULL;
    }

    ctx->item_count = 0U;
    ctx->selected_idx = 0U;
    ctx->path_pool_size = 0U;
}

/*
 * brief: Build a dynamic path pool for all PNG resources under assets root.
 * input: ctx - gallery context.
 * output: ESP_OK on success; otherwise state/not-found/no-mem errors.
 */
static esp_err_t _gallery_collect_png_paths(gallery_app_ctx_t *ctx)
{
    char **all_paths;
    size_t all_count;
    size_t png_count;
    size_t path_pool_bytes;
    size_t i;
    size_t cursor_offset;

    if (ctx == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    all_paths = NULL;
    all_count = 0U;
    png_count = 0U;
    path_pool_bytes = 0U;

    if (!usr_fs_is_ready())
    {
        return ESP_ERR_INVALID_STATE;
    }

    if (usr_fs_search_all_files(NULL, &all_paths, &all_count) != ESP_OK)
    {
        return ESP_ERR_NOT_FOUND;
    }

    for (i = 0U; i < all_count; i++)
    {
        if (_gallery_path_is_png(all_paths[i]))
        {
            png_count++;
            path_pool_bytes += strlen(all_paths[i]) + 1U;
        }
    }

    if (png_count == 0U)
    {
        usr_fs_free_file_paths(all_paths, all_count);
        return ESP_ERR_NOT_FOUND;
    }

    ctx->items = (gallery_png_item_t *)_gallery_psram_alloc(png_count * sizeof(gallery_png_item_t));
    if (ctx->items == NULL)
    {
        usr_fs_free_file_paths(all_paths, all_count);
        return ESP_ERR_NO_MEM;
    }
    memset(ctx->items, 0, png_count * sizeof(gallery_png_item_t));

    ctx->path_pool = (char *)_gallery_psram_alloc(path_pool_bytes);
    if (ctx->path_pool == NULL)
    {
        usr_fs_free_file_paths(all_paths, all_count);
        _gallery_free_png_pool(ctx);
        return ESP_ERR_NO_MEM;
    }

    cursor_offset = 0U;
    ctx->item_count = 0U;

    for (i = 0U; i < all_count; i++)
    {
        char *dst;
        size_t path_len;

        if (!_gallery_path_is_png(all_paths[i]))
        {
            continue;
        }

        path_len = strlen(all_paths[i]) + 1U;
        dst = ctx->path_pool + cursor_offset;
        memcpy(dst, all_paths[i], path_len);

        ctx->items[ctx->item_count].path = dst;
        cursor_offset += path_len;
        ctx->item_count++;
    }

    ctx->path_pool_size = path_pool_bytes;
    usr_fs_free_file_paths(all_paths, all_count);

    return ESP_OK;
}

/*
 * brief: Load one selected PNG file into dynamic decode source buffer.
 * input: ctx - gallery context; idx - selected PNG index.
 * output: ESP_OK on success; otherwise file/decode validation errors.
 */
static esp_err_t _gallery_load_selected_png(gallery_app_ctx_t *ctx, size_t idx)
{
    uint8_t *png_data;
    size_t png_size;
    lv_img_dsc_t src_dsc;
    lv_img_header_t header;
    uint32_t decoded_size;
    uint8_t *decoded_copy;
    lv_res_t dec_info_ret;
    lv_img_decoder_dsc_t dec_dsc;
    lv_res_t dec_open_ret;
    esp_err_t ret;

    if ((ctx == NULL) || (ctx->items == NULL) || (ctx->item_count == 0U) || (idx >= ctx->item_count))
    {
        return ESP_ERR_INVALID_ARG;
    }

    if (ctx->items[idx].decoded_ready)
    {
        _gallery_release_loaded_png(ctx);
        ctx->loaded_idx = idx;
        return ESP_OK;
    }

    png_data = NULL;
    png_size = 0U;
    ret = usr_fs_read_file(ctx->items[idx].path, &png_data, &png_size);
    if (ret != ESP_OK)
    {
        return ret;
    }

    if (!_gallery_is_png_magic(png_data, png_size))
    {
        free(png_data);
        return ESP_ERR_INVALID_RESPONSE;
    }

    memset(&src_dsc, 0, sizeof(src_dsc));
    src_dsc.header.always_zero = 0U;
    src_dsc.header.cf = 0U;
    src_dsc.header.w = 0U;
    src_dsc.header.h = 0U;
    src_dsc.data = png_data;
    src_dsc.data_size = png_size;

    memset(&header, 0, sizeof(header));
    dec_info_ret = lv_img_decoder_get_info(&src_dsc, &header);
    if (dec_info_ret != LV_RES_OK)
    {
        free(png_data);
        return ESP_ERR_NOT_SUPPORTED;
    }

    dec_open_ret = lv_img_decoder_open(&dec_dsc, &src_dsc, lv_color_black(), 0);
    if (dec_open_ret != LV_RES_OK)
    {
        ESP_LOGW(TAG,
                 "png decode probe failed idx=%u w=%d h=%d bytes=%u",
                 (unsigned)idx,
                 (int)header.w,
                 (int)header.h,
                 (unsigned)src_dsc.data_size);
        free(png_data);
        return ESP_ERR_NO_MEM;
    }

    decoded_size = lv_img_buf_get_img_size(dec_dsc.header.w, dec_dsc.header.h, dec_dsc.header.cf);
    if ((decoded_size == 0U) || (dec_dsc.img_data == NULL))
    {
        lv_img_decoder_close(&dec_dsc);
        free(png_data);
        return ESP_ERR_INVALID_SIZE;
    }

    decoded_copy = (uint8_t *)_gallery_psram_alloc((size_t)decoded_size);
    if (decoded_copy == NULL)
    {
        lv_img_decoder_close(&dec_dsc);
        free(png_data);
        return ESP_ERR_NO_MEM;
    }

    memcpy(decoded_copy, dec_dsc.img_data, decoded_size);
    lv_img_decoder_close(&dec_dsc);
    free(png_data);

    memset(&ctx->items[idx].img_dsc, 0, sizeof(lv_img_dsc_t));
    ctx->items[idx].img_dsc.header.always_zero = 0U;
    ctx->items[idx].img_dsc.header.cf = dec_dsc.header.cf;
    ctx->items[idx].img_dsc.header.w = dec_dsc.header.w;
    ctx->items[idx].img_dsc.header.h = dec_dsc.header.h;
    ctx->items[idx].img_dsc.data = decoded_copy;
    ctx->items[idx].img_dsc.data_size = decoded_size;

    ctx->items[idx].decoded_data = decoded_copy;
    ctx->items[idx].decoded_size = decoded_size;
    ctx->items[idx].decoded_ready = true;

    _gallery_release_loaded_png(ctx);
    ctx->loaded_idx = idx;

    ESP_LOGI(TAG,
             "png decoded idx=%u %dx%d cf=%d out=%u",
             (unsigned)idx,
             (int)ctx->items[idx].img_dsc.header.w,
             (int)ctx->items[idx].img_dsc.header.h,
             (int)ctx->items[idx].img_dsc.header.cf,
             (unsigned)ctx->items[idx].decoded_size);

    return ESP_OK;
}

/*
 * brief: Update gallery image widget according to current selected PNG index.
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
        lv_label_set_text(ctx->hint_label, "no png found");
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
        load_ret = _gallery_load_selected_png(ctx, try_idx);
        if (load_ret == ESP_OK)
        {
            ctx->selected_idx = try_idx;
            loaded = true;
            break;
        }

        ESP_LOGW(TAG,
                 "skip png idx=%u path=%s err=%d",
                 (unsigned)try_idx,
                 ctx->items[try_idx].path,
                 (int)load_ret);
    }

    if (!loaded)
    {
        lv_label_set_text(ctx->hint_label, "no decodable png");
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
        lv_label_set_text(ctx->hint_label, "png view");
    }
    else
    {
        lv_label_set_text(ctx->hint_label, status_text);
    }
}

/*
 * brief: Select previous PNG and refresh image asynchronously.
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
 * brief: Select next PNG and refresh image asynchronously.
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
            app_home_nav_request_home();
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

    _gallery_free_png_pool(ctx);
    lv_mem_free(ctx);
}

/*
 * brief: Build gallery UI in content area and load PNG path pool.
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
    lv_obj_set_style_text_color(hint, lv_color_hex(0xE2E8F0), 0);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -2);
    lv_label_set_text(hint, "scanning png...");
    ctx->hint_label = hint;

    ret = _gallery_collect_png_paths(ctx);
    if (ret == ESP_OK)
    {
        ctx->selected_idx = 0U;
        _gallery_refresh_image(ctx);
        ESP_LOGI(TAG,
                 "png list ready, count=%u, path_pool=%u",
                 (unsigned)ctx->item_count,
                 (unsigned)ctx->path_pool_size);
    }
    else
    {
        _gallery_free_png_pool(ctx);
        lv_label_set_text(hint, "no png found in assets");
        ESP_LOGW(TAG, "png scan failed: %d", (int)ret);
    }

    if (!_gallery_start_input_task())
    {
        lv_obj_del(scr);
        _gallery_free_png_pool(ctx);
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
 * brief: Request desktop return through shared home navigation callback.
 * input: None.
 * output: None.
 */
void gallery_app_destroy_and_return(void)
{
    app_home_nav_request_home();
}
