#include "pic_common.h"

#define TAG "PIC_COMMON"

/* Driver entrypoints implemented in pic_*.c modules. */
bool pic_png_path_is_match(const char *path);
bool pic_png_decoder_enabled(void);

bool pic_bmp_path_is_match(const char *path);
bool pic_bmp_decoder_enabled(void);

bool pic_jpg_path_is_match(const char *path);
bool pic_jpg_decoder_enabled(void);

bool pic_gif_path_is_match(const char *path);
bool pic_gif_decoder_enabled(void);

/*
 * brief: Allocate buffer from PSRAM first, then fallback to generic 8-bit heap.
 * input: bytes - requested byte count.
 * output: Allocated pointer on success; otherwise NULL.
 */
static void *_pic_common_alloc(size_t bytes)
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
 * brief: Validate source file signature against expected format magic.
 * input: format - expected format type; data/size - source bytes.
 * output: true when magic bytes are valid.
 */
static bool _pic_magic_is_valid(pic_format_e format, const uint8_t *data, size_t size)
{
    static const uint8_t kPngMagic[8] = {0x89U, 0x50U, 0x4EU, 0x47U, 0x0DU, 0x0AU, 0x1AU, 0x0AU};
    static const uint8_t kGif87a[6] = {'G', 'I', 'F', '8', '7', 'a'};
    static const uint8_t kGif89a[6] = {'G', 'I', 'F', '8', '9', 'a'};

    if ((data == NULL) || (size == 0U))
    {
        return false;
    }

    switch (format)
    {
    case PIC_FORMAT_PNG:
        if (size < sizeof(kPngMagic))
        {
            return false;
        }
        return (memcmp(data, kPngMagic, sizeof(kPngMagic)) == 0);
    case PIC_FORMAT_BMP:
        if (size < 2U)
        {
            return false;
        }
        return (data[0] == 'B') && (data[1] == 'M');
    case PIC_FORMAT_JPG:
        if (size < 4U)
        {
            return false;
        }
        return (data[0] == 0xFFU) && (data[1] == 0xD8U);
    case PIC_FORMAT_GIF:
        if (size < sizeof(kGif87a))
        {
            return false;
        }
        if (memcmp(data, kGif87a, sizeof(kGif87a)) == 0)
        {
            return true;
        }
        return (memcmp(data, kGif89a, sizeof(kGif89a)) == 0);
    default:
        return false;
    }
}

/*
 * brief: Map decoder header color format to in-memory display format.
 * input: cf - decoder output format.
 * output: Compatible LVGL true-color format for persistent cache.
 */
static lv_img_cf_t _pic_cache_target_cf(lv_img_cf_t cf)
{
    switch (cf)
    {
    case LV_IMG_CF_RAW:
    case LV_IMG_CF_RAW_CHROMA_KEYED:
        return LV_IMG_CF_TRUE_COLOR;
    case LV_IMG_CF_RAW_ALPHA:
        return LV_IMG_CF_TRUE_COLOR_ALPHA;
    default:
        return cf;
    }
}

/*
 * brief: Compute one decoded line byte size for read_line fallback.
 * input: width - image width in pixels; cf - cached target color format.
 * output: Byte count per line; 0 means unsupported format.
 */
static uint32_t _pic_line_bytes(lv_coord_t width, lv_img_cf_t cf)
{
    uint8_t px_bits;
    uint32_t bits_total;

    if (width <= 0)
    {
        return 0U;
    }

    px_bits = lv_img_cf_get_px_size(cf);
    if (px_bits == 0U)
    {
        return 0U;
    }

    bits_total = (uint32_t)width * (uint32_t)px_bits;
    return (bits_total + 7U) / 8U;
}

/*
 * brief: Detect picture format from file path suffix.
 * input: path - full file path string.
 * output: Detected picture format enum.
 */
static pic_format_e _pic_detect_format(const char *path)
{
    if (pic_png_path_is_match(path))
    {
        return PIC_FORMAT_PNG;
    }

    if (pic_bmp_path_is_match(path))
    {
        return PIC_FORMAT_BMP;
    }

    if (pic_jpg_path_is_match(path))
    {
        return PIC_FORMAT_JPG;
    }

    if (pic_gif_path_is_match(path))
    {
        return PIC_FORMAT_GIF;
    }

    return PIC_FORMAT_UNKNOWN;
}

/*
 * brief: Invalidate previously loaded LVGL source and update active loaded index.
 * input: items/item_count - picture list; idx - new loaded index; loaded_idx - state holder.
 * output: None.
 */
static void _pic_set_loaded_idx(pic_item_t *items,
                                size_t item_count,
                                size_t idx,
                                size_t *loaded_idx)
{
    if ((items == NULL) || (loaded_idx == NULL) || (idx >= item_count))
    {
        return;
    }

    if ((*loaded_idx < item_count) && (*loaded_idx != idx))
    {
        lv_img_cache_invalidate_src(&items[*loaded_idx].img_dsc);
    }

    *loaded_idx = idx;
}

/*
 * brief: Return true when index is inside circular window [start, start+len).
 * input: idx/start/window_len/item_count - index and circular window context.
 * output: true when idx belongs to the current predecode window.
 */
static bool _pic_index_in_window(size_t idx,
                                 size_t start,
                                 size_t window_len,
                                 size_t item_count)
{
    size_t end;

    if ((item_count == 0U) || (window_len == 0U))
    {
        return false;
    }

    end = (start + window_len - 1U) % item_count;
    if (start <= end)
    {
        return (idx >= start) && (idx <= end);
    }

    return (idx >= start) || (idx <= end);
}

/*
 * brief: Decode one source file through LVGL decoder and keep full frame in PSRAM.
 * input: item - target item with path/format metadata.
 * output: ESP_OK on success; otherwise decode/open/read errors.
 */
static esp_err_t _pic_decode_item_to_psram(pic_item_t *item)
{
    uint8_t *src_data;
    size_t src_size;
    lv_img_dsc_t src_dsc;
    lv_img_header_t info_header;
    lv_img_decoder_dsc_t dec_dsc;
    lv_img_cf_t cache_cf;
    lv_coord_t w;
    lv_coord_t h;
    uint32_t decoded_size;
    uint8_t *decoded_copy;
    uint8_t *line_buf;
    uint32_t line_bytes;
    lv_coord_t y;
    lv_res_t res;
    bool decoder_opened;
    esp_err_t ret;

    if ((item == NULL) || (item->path == NULL))
    {
        return ESP_ERR_INVALID_ARG;
    }

    if (item->decoded_ready)
    {
        return ESP_OK;
    }

    if (!pic_format_decoder_enabled(item->format))
    {
        return ESP_ERR_NOT_SUPPORTED;
    }

    src_data = NULL;
    src_size = 0U;
    decoded_copy = NULL;
    line_buf = NULL;
    decoder_opened = false;
    ret = usr_fs_read_file(item->path, &src_data, &src_size);
    if (ret != ESP_OK)
    {
        return ret;
    }

    if (!_pic_magic_is_valid(item->format, src_data, src_size))
    {
        ret = ESP_ERR_INVALID_RESPONSE;
        goto cleanup;
    }

    memset(&src_dsc, 0, sizeof(src_dsc));
    src_dsc.data = src_data;
    src_dsc.data_size = src_size;

    memset(&info_header, 0, sizeof(info_header));
    res = lv_img_decoder_get_info(&src_dsc, &info_header);
    if (res != LV_RES_OK)
    {
        ret = ESP_ERR_NOT_SUPPORTED;
        goto cleanup;
    }

    memset(&dec_dsc, 0, sizeof(dec_dsc));
    res = lv_img_decoder_open(&dec_dsc, &src_dsc, lv_color_black(), LV_OPA_COVER);
    if (res != LV_RES_OK)
    {
        ret = ESP_ERR_NOT_SUPPORTED;
        goto cleanup;
    }
    decoder_opened = true;

    w = dec_dsc.header.w;
    h = dec_dsc.header.h;
    if ((w <= 0) || (h <= 0))
    {
        ret = ESP_ERR_INVALID_SIZE;
        goto cleanup;
    }

    cache_cf = _pic_cache_target_cf((lv_img_cf_t)dec_dsc.header.cf);
    decoded_size = lv_img_buf_get_img_size(w, h, cache_cf);
    if (decoded_size == 0U)
    {
        ret = ESP_ERR_INVALID_SIZE;
        goto cleanup;
    }

    decoded_copy = (uint8_t *)_pic_common_alloc((size_t)decoded_size);
    if (decoded_copy == NULL)
    {
        ret = ESP_ERR_NO_MEM;
        goto cleanup;
    }

    if (dec_dsc.img_data != NULL)
    {
        uint32_t src_decoded_size;

        src_decoded_size = lv_img_buf_get_img_size(w, h, (lv_img_cf_t)dec_dsc.header.cf);
        if (src_decoded_size == 0U)
        {
            src_decoded_size = decoded_size;
        }

        if (src_decoded_size < decoded_size)
        {
            ret = ESP_ERR_INVALID_SIZE;
            goto cleanup;
        }

        memcpy(decoded_copy, dec_dsc.img_data, decoded_size);
    }
    else
    {
        line_bytes = _pic_line_bytes(w, cache_cf);
        if ((line_bytes == 0U) || (line_bytes > decoded_size))
        {
            ret = ESP_ERR_INVALID_SIZE;
            goto cleanup;
        }

        line_buf = (uint8_t *)_pic_common_alloc(line_bytes);
        if (line_buf == NULL)
        {
            ret = ESP_ERR_NO_MEM;
            goto cleanup;
        }

        for (y = 0; y < h; y++)
        {
            res = lv_img_decoder_read_line(&dec_dsc, 0, y, w, line_buf);
            if (res != LV_RES_OK)
            {
                ret = ESP_ERR_NOT_SUPPORTED;
                goto cleanup;
            }

            memcpy(decoded_copy + ((uint32_t)y * line_bytes), line_buf, line_bytes);
        }
    }

    pic_free_decoded_item(item);

    memset(&item->img_dsc, 0, sizeof(item->img_dsc));
    item->img_dsc.header.always_zero = 0U;
    item->img_dsc.header.cf = (uint32_t)cache_cf;
    item->img_dsc.header.w = w;
    item->img_dsc.header.h = h;
    item->img_dsc.data = decoded_copy;
    item->img_dsc.data_size = decoded_size;

    item->decoded_data = decoded_copy;
    item->decoded_size = decoded_size;
    item->decoded_ready = true;
    ret = ESP_OK;

cleanup:
    if (line_buf != NULL)
    {
        heap_caps_free(line_buf);
    }

    if (decoder_opened)
    {
        lv_img_decoder_close(&dec_dsc);
    }

    if ((ret != ESP_OK) && (decoded_copy != NULL))
    {
        heap_caps_free(decoded_copy);
    }

    if (src_data != NULL)
    {
        free(src_data);
    }

    return ret;
}

/*
 * brief: Return true when one file path suffix matches any supported image format.
 * input: path - full file path.
 * output: true when suffix is png/bmp/jpg/gif.
 */
bool pic_path_is_supported(const char *path)
{
    return (_pic_detect_format(path) != PIC_FORMAT_UNKNOWN);
}

/*
 * brief: Return short format name string.
 * input: format - picture format enum.
 * output: Constant readable name.
 */
const char *pic_format_name(pic_format_e format)
{
    switch (format)
    {
    case PIC_FORMAT_PNG:
        return "png";
    case PIC_FORMAT_BMP:
        return "bmp";
    case PIC_FORMAT_JPG:
        return "jpg";
    case PIC_FORMAT_GIF:
        return "gif";
    default:
        return "unknown";
    }
}

/*
 * brief: Return whether LVGL decoder is enabled for one format in sdkconfig.
 * input: format - target format enum.
 * output: true when decoder is enabled.
 */
bool pic_format_decoder_enabled(pic_format_e format)
{
    switch (format)
    {
    case PIC_FORMAT_PNG:
        return pic_png_decoder_enabled();
    case PIC_FORMAT_BMP:
        return pic_bmp_decoder_enabled();
    case PIC_FORMAT_JPG:
        return pic_jpg_decoder_enabled();
    case PIC_FORMAT_GIF:
        return pic_gif_decoder_enabled();
    default:
        return false;
    }
}

/*
 * brief: Return effective predecode window limit used by common layer.
 * input: None.
 * output: Runtime max decoded item count kept in PSRAM cache.
 */
size_t pic_predecode_max_count(void)
{
    size_t max_count;

    max_count = (size_t)PIC_PREDECODE_MAX_COUNT;
    if (max_count == 0U)
    {
        max_count = 1U;
    }

    return max_count;
}

/*
 * brief: Scan SPIFFS and collect all supported picture paths.
 * input: out_items/out_count/out_path_pool/out_path_pool_size - output holders.
 * output: ESP_OK on success; otherwise state/not-found/no-mem errors.
 */
esp_err_t pic_collect_paths(pic_item_t **out_items,
                            size_t *out_count,
                            char **out_path_pool,
                            size_t *out_path_pool_size)
{
    char **all_paths;
    size_t all_count;
    size_t match_count;
    size_t pool_bytes;
    pic_item_t *items;
    char *path_pool;
    size_t cursor;
    size_t i;

    if ((out_items == NULL) || (out_count == NULL) ||
        (out_path_pool == NULL) || (out_path_pool_size == NULL))
    {
        return ESP_ERR_INVALID_ARG;
    }

    *out_items = NULL;
    *out_count = 0U;
    *out_path_pool = NULL;
    *out_path_pool_size = 0U;

    if (!usr_fs_is_ready())
    {
        return ESP_ERR_INVALID_STATE;
    }

    all_paths = NULL;
    all_count = 0U;
    match_count = 0U;
    pool_bytes = 0U;

    if (usr_fs_search_all_files(NULL, &all_paths, &all_count) != ESP_OK)
    {
        return ESP_ERR_NOT_FOUND;
    }

    for (i = 0U; i < all_count; i++)
    {
        if (_pic_detect_format(all_paths[i]) != PIC_FORMAT_UNKNOWN)
        {
            match_count++;
            pool_bytes += strlen(all_paths[i]) + 1U;
        }
    }

    if (match_count == 0U)
    {
        usr_fs_free_file_paths(all_paths, all_count);
        return ESP_ERR_NOT_FOUND;
    }

    items = (pic_item_t *)_pic_common_alloc(match_count * sizeof(pic_item_t));
    if (items == NULL)
    {
        usr_fs_free_file_paths(all_paths, all_count);
        return ESP_ERR_NO_MEM;
    }
    memset(items, 0, match_count * sizeof(pic_item_t));

    path_pool = (char *)_pic_common_alloc(pool_bytes);
    if (path_pool == NULL)
    {
        heap_caps_free(items);
        usr_fs_free_file_paths(all_paths, all_count);
        return ESP_ERR_NO_MEM;
    }

    cursor = 0U;
    match_count = 0U;

    for (i = 0U; i < all_count; i++)
    {
        pic_format_e fmt;
        size_t path_len;
        char *dst;

        fmt = _pic_detect_format(all_paths[i]);
        if (fmt == PIC_FORMAT_UNKNOWN)
        {
            continue;
        }

        path_len = strlen(all_paths[i]) + 1U;
        dst = path_pool + cursor;
        memcpy(dst, all_paths[i], path_len);

        items[match_count].path = dst;
        items[match_count].format = fmt;
        cursor += path_len;
        match_count++;
    }

    usr_fs_free_file_paths(all_paths, all_count);

    *out_items = items;
    *out_count = match_count;
    *out_path_pool = path_pool;
    *out_path_pool_size = pool_bytes;

    ESP_LOGI(TAG, "picture paths ready, count=%u", (unsigned)match_count);
    return ESP_OK;
}

/*
 * brief: Decode one picture item and keep it as currently loaded source.
 * input: items/item_count - picture list; idx - target index; loaded_idx - active source holder.
 * output: ESP_OK on success; otherwise propagated file/decode errors.
 */
esp_err_t pic_decode_item(pic_item_t *items,
                          size_t item_count,
                          size_t idx,
                          size_t *loaded_idx)
{
    esp_err_t ret;

    if ((items == NULL) || (item_count == 0U) || (idx >= item_count))
    {
        return ESP_ERR_INVALID_ARG;
    }

    if (items[idx].decoded_ready)
    {
        _pic_set_loaded_idx(items, item_count, idx, loaded_idx);
        return ESP_OK;
    }

    ret = _pic_decode_item_to_psram(&items[idx]);

    if (ret == ESP_OK)
    {
        _pic_set_loaded_idx(items, item_count, idx, loaded_idx);
    }

    return ret;
}

/*
 * brief: Prepare selected image and keep a circular predecode window in PSRAM.
 * input: items/item_count - image list; selected_idx - focus index; loaded_idx - active source holder.
 * output: ESP_OK when selected image is decodable; optional ready count in current window.
 */
esp_err_t pic_prepare_window(pic_item_t *items,
                             size_t item_count,
                             size_t selected_idx,
                             size_t *loaded_idx,
                             size_t *out_ready_count)
{
    esp_err_t ret;
    size_t window_count;
    size_t i;
    size_t ready_count;

    if ((items == NULL) || (item_count == 0U))
    {
        return ESP_ERR_INVALID_ARG;
    }

    selected_idx %= item_count;
    window_count = pic_predecode_max_count();
    if (window_count > item_count)
    {
        window_count = item_count;
    }

    ret = pic_decode_item(items, item_count, selected_idx, loaded_idx);
    if (ret != ESP_OK)
    {
        if (out_ready_count != NULL)
        {
            *out_ready_count = 0U;
        }
        return ret;
    }

    ready_count = 1U;
    for (i = 1U; i < window_count; i++)
    {
        size_t idx;
        esp_err_t preload_ret;

        idx = (selected_idx + i) % item_count;
        if (!items[idx].decoded_ready)
        {
            preload_ret = pic_decode_item(items, item_count, idx, NULL);
            if (preload_ret != ESP_OK)
            {
                ESP_LOGW(TAG,
                         "window preload skip idx=%u fmt=%s path=%s err=%d",
                         (unsigned)idx,
                         pic_format_name(items[idx].format),
                         items[idx].path,
                         (int)preload_ret);
            }
        }

        if (items[idx].decoded_ready)
        {
            ready_count++;
        }
    }

    for (i = 0U; i < item_count; i++)
    {
        if (_pic_index_in_window(i, selected_idx, window_count, item_count))
        {
            continue;
        }

        if (!items[i].decoded_ready)
        {
            continue;
        }

        if ((loaded_idx != NULL) && (*loaded_idx == i))
        {
            *loaded_idx = (size_t)-1;
        }

        lv_img_cache_invalidate_src(&items[i].img_dsc);
        pic_free_decoded_item(&items[i]);
    }

    if (out_ready_count != NULL)
    {
        *out_ready_count = ready_count;
    }

    return ESP_OK;
}

/*
 * brief: Invalidate current loaded-source cache and reset loaded index.
 * input: items/item_count - picture list; loaded_idx - loaded source index holder.
 * output: None.
 */
void pic_release_loaded(pic_item_t *items, size_t item_count, size_t *loaded_idx)
{
    if ((items == NULL) || (loaded_idx == NULL))
    {
        return;
    }

    if (*loaded_idx < item_count)
    {
        lv_img_cache_invalidate_src(&items[*loaded_idx].img_dsc);
    }

    *loaded_idx = (size_t)-1;
}

/*
 * brief: Free one decoded item payload and clear descriptor fields.
 * input: item - target picture item.
 * output: None.
 */
void pic_free_decoded_item(pic_item_t *item)
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
 * brief: Free full picture item and path pools.
 * input: items/item_count/path_pool/path_pool_size/loaded_idx - caller state holders.
 * output: None.
 */
void pic_free_pool(pic_item_t **items,
                   size_t *item_count,
                   char **path_pool,
                   size_t *path_pool_size,
                   size_t *loaded_idx)
{
    pic_item_t *item_arr;
    size_t count;
    size_t i;

    item_arr = (items != NULL) ? *items : NULL;
    count = (item_count != NULL) ? *item_count : 0U;

    pic_release_loaded(item_arr, count, loaded_idx);

    if (item_arr != NULL)
    {
        for (i = 0U; i < count; i++)
        {
            if (item_arr[i].decoded_ready)
            {
                lv_img_cache_invalidate_src(&item_arr[i].img_dsc);
            }
            pic_free_decoded_item(&item_arr[i]);
        }

        heap_caps_free(item_arr);
    }

    if ((path_pool != NULL) && (*path_pool != NULL))
    {
        heap_caps_free(*path_pool);
        *path_pool = NULL;
    }

    if (items != NULL)
    {
        *items = NULL;
    }

    if (item_count != NULL)
    {
        *item_count = 0U;
    }

    if (path_pool_size != NULL)
    {
        *path_pool_size = 0U;
    }
}
