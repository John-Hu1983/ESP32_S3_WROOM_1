#include "pic_common.h"

#define TAG "PIC_COMMON"

/* Driver entrypoints implemented in pic_*.c modules. */
bool pic_png_path_is_match(const char *path);
bool pic_png_decoder_enabled(void);
esp_err_t pic_png_decode_item(pic_item_t *item);

bool pic_bmp_path_is_match(const char *path);
bool pic_bmp_decoder_enabled(void);
esp_err_t pic_bmp_decode_item(pic_item_t *item);

bool pic_jpg_path_is_match(const char *path);
bool pic_jpg_decoder_enabled(void);
esp_err_t pic_jpg_decode_item(pic_item_t *item);

bool pic_gif_path_is_match(const char *path);
bool pic_gif_decoder_enabled(void);
esp_err_t pic_gif_decode_item(pic_item_t *item);

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

    switch (items[idx].format)
    {
    case PIC_FORMAT_PNG:
        ret = pic_png_decode_item(&items[idx]);
        break;
    case PIC_FORMAT_BMP:
        ret = pic_bmp_decode_item(&items[idx]);
        break;
    case PIC_FORMAT_JPG:
        ret = pic_jpg_decode_item(&items[idx]);
        break;
    case PIC_FORMAT_GIF:
        ret = pic_gif_decode_item(&items[idx]);
        break;
    default:
        ret = ESP_ERR_NOT_SUPPORTED;
        break;
    }

    if (ret == ESP_OK)
    {
        _pic_set_loaded_idx(items, item_count, idx, loaded_idx);
    }

    return ret;
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
