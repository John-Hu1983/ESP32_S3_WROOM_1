#include "pic_bmp.h"

#define TAG "PIC_BMP"

/*
 * brief: Allocate decode buffers from PSRAM first, then fallback to generic 8-bit heap.
 * input: bytes - requested bytes.
 * output: Allocated pointer on success; otherwise NULL.
 */
static void *_pic_bmp_alloc(size_t bytes)
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
 * brief: Compare path suffix with ASCII case-insensitive semantics.
 * input: path - source path; suffix - expected extension starting with '.'.
 * output: true when suffix matches.
 */
static bool _pic_bmp_suffix_match_ci(const char *path, const char *suffix)
{
    size_t path_len;
    size_t suffix_len;
    size_t i;

    if ((path == NULL) || (suffix == NULL))
    {
        return false;
    }

    path_len = strlen(path);
    suffix_len = strlen(suffix);
    if ((suffix_len == 0U) || (path_len < suffix_len))
    {
        return false;
    }

    for (i = 0U; i < suffix_len; i++)
    {
        char a;
        char b;

        a = path[path_len - suffix_len + i];
        b = suffix[i];

        if ((a >= 'A') && (a <= 'Z'))
        {
            a = (char)(a - 'A' + 'a');
        }
        if ((b >= 'A') && (b <= 'Z'))
        {
            b = (char)(b - 'A' + 'a');
        }

        if (a != b)
        {
            return false;
        }
    }

    return true;
}

/*
 * brief: Validate BMP signature.
 * input: data - file bytes; size - byte length.
 * output: true when magic bytes are valid.
 */
static bool _pic_bmp_is_magic_valid(const uint8_t *data, size_t size)
{
    if ((data == NULL) || (size < 2U))
    {
        return false;
    }

    return (data[0] == 'B') && (data[1] == 'M');
}

bool pic_bmp_path_is_match(const char *path)
{
    return _pic_bmp_suffix_match_ci(path, ".bmp");
}

bool pic_bmp_decoder_enabled(void)
{
#if defined(CONFIG_LV_USE_BMP) && CONFIG_LV_USE_BMP
    return true;
#else
    return false;
#endif
}

esp_err_t pic_bmp_decode_item(pic_item_t *item)
{
#if defined(CONFIG_LV_USE_BMP) && CONFIG_LV_USE_BMP
    uint8_t *src_data;
    size_t src_size;
    lv_img_dsc_t src_dsc;
    lv_img_header_t header;
    lv_img_decoder_dsc_t dec_dsc;
    lv_res_t res;
    uint32_t decoded_size;
    uint8_t *decoded_copy;
    uint32_t cf;
    lv_coord_t w;
    lv_coord_t h;
    esp_err_t ret;

    if ((item == NULL) || (item->path == NULL) || !pic_bmp_path_is_match(item->path))
    {
        return ESP_ERR_INVALID_ARG;
    }

    if (item->decoded_ready)
    {
        return ESP_OK;
    }

    src_data = NULL;
    src_size = 0U;
    ret = usr_fs_read_file(item->path, &src_data, &src_size);
    if (ret != ESP_OK)
    {
        return ret;
    }

    if (!_pic_bmp_is_magic_valid(src_data, src_size))
    {
        free(src_data);
        return ESP_ERR_INVALID_RESPONSE;
    }

    memset(&src_dsc, 0, sizeof(src_dsc));
    src_dsc.data = src_data;
    src_dsc.data_size = src_size;

    memset(&header, 0, sizeof(header));
    res = lv_img_decoder_get_info(&src_dsc, &header);
    if (res != LV_RES_OK)
    {
        free(src_data);
        return ESP_ERR_NOT_SUPPORTED;
    }

    res = lv_img_decoder_open(&dec_dsc, &src_dsc, lv_color_black(), 0);
    if (res != LV_RES_OK)
    {
        ESP_LOGW(TAG, "bmp decode open failed, path=%s", item->path);
        free(src_data);
        return ESP_ERR_NOT_SUPPORTED;
    }

    cf = (uint32_t)dec_dsc.header.cf;
    w = dec_dsc.header.w;
    h = dec_dsc.header.h;

    decoded_size = lv_img_buf_get_img_size(w, h, (lv_img_cf_t)cf);
    if ((decoded_size == 0U) || (dec_dsc.img_data == NULL))
    {
        lv_img_decoder_close(&dec_dsc);
        free(src_data);
        return ESP_ERR_INVALID_SIZE;
    }

    decoded_copy = (uint8_t *)_pic_bmp_alloc((size_t)decoded_size);
    if (decoded_copy == NULL)
    {
        lv_img_decoder_close(&dec_dsc);
        free(src_data);
        return ESP_ERR_NO_MEM;
    }

    memcpy(decoded_copy, dec_dsc.img_data, decoded_size);
    lv_img_decoder_close(&dec_dsc);
    free(src_data);

    pic_free_decoded_item(item);

    memset(&item->img_dsc, 0, sizeof(item->img_dsc));
    item->img_dsc.header.always_zero = 0U;
    item->img_dsc.header.cf = (uint32_t)cf;
    item->img_dsc.header.w = w;
    item->img_dsc.header.h = h;
    item->img_dsc.data = decoded_copy;
    item->img_dsc.data_size = decoded_size;

    item->decoded_data = decoded_copy;
    item->decoded_size = decoded_size;
    item->decoded_ready = true;
    item->format = PIC_FORMAT_BMP;

    return ESP_OK;
#else
    (void)item;
    return ESP_ERR_NOT_SUPPORTED;
#endif
}
