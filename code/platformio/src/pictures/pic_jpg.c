#include "pic_jpg.h"

#define TAG "PIC_JPG"

/*
 * brief: Compare path suffix with ASCII case-insensitive semantics.
 * input: path - source path; suffix - expected extension starting with '.'.
 * output: true when suffix matches.
 */
static bool _pic_jpg_suffix_match_ci(const char *path, const char *suffix)
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
 * brief: Validate JPEG signature.
 * input: data - file bytes; size - byte length.
 * output: true when SOI marker is valid.
 */
static bool _pic_jpg_is_magic_valid(const uint8_t *data, size_t size)
{
    if ((data == NULL) || (size < 4U))
    {
        return false;
    }

    if ((data[0] != 0xFFU) || (data[1] != 0xD8U))
    {
        return false;
    }

    return true;
}

bool pic_jpg_path_is_match(const char *path)
{
    return _pic_jpg_suffix_match_ci(path, ".jpg") ||
           _pic_jpg_suffix_match_ci(path, ".jpeg");
}

bool pic_jpg_decoder_enabled(void)
{
#if (defined(CONFIG_LV_USE_SJPG) && CONFIG_LV_USE_SJPG) || (defined(CONFIG_LV_USE_JPG) && CONFIG_LV_USE_JPG)
    return true;
#else
    return false;
#endif
}

esp_err_t pic_jpg_decode_item(pic_item_t *item)
{
#if (defined(CONFIG_LV_USE_SJPG) && CONFIG_LV_USE_SJPG) || (defined(CONFIG_LV_USE_JPG) && CONFIG_LV_USE_JPG)
    uint8_t *src_data;
    size_t src_size;
    lv_img_dsc_t src_dsc;
    lv_img_header_t header;
    lv_res_t res;
    esp_err_t ret;

    if ((item == NULL) || (item->path == NULL) || !pic_jpg_path_is_match(item->path))
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

    if (!_pic_jpg_is_magic_valid(src_data, src_size))
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
        ESP_LOGW(TAG, "jpg decoder info failed, path=%s", item->path);
        free(src_data);
        return ESP_ERR_NOT_SUPPORTED;
    }

    pic_free_decoded_item(item);

    memset(&item->img_dsc, 0, sizeof(item->img_dsc));
    item->img_dsc.header.always_zero = 0U;
    item->img_dsc.header.cf = (uint32_t)LV_IMG_CF_RAW;
    item->img_dsc.header.w = header.w;
    item->img_dsc.header.h = header.h;
    item->img_dsc.data = src_data;
    item->img_dsc.data_size = src_size;

    item->decoded_data = src_data;
    item->decoded_size = src_size;
    item->decoded_ready = true;
    item->format = PIC_FORMAT_JPG;

    return ESP_OK;
#else
    (void)item;
    return ESP_ERR_NOT_SUPPORTED;
#endif
}
