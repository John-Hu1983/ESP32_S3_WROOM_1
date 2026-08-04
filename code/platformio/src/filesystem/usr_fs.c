#include "usr_fs.h"

#define TAG "USR_FS"

static bool s_ready = false;

static void *usr_fs_malloc(size_t bytes)
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

static esp_err_t usr_fs_copy_string(const char *src,
                                    char *out_str,
                                    size_t out_str_size)
{
    int n;

    if ((src == NULL) || (out_str == NULL) || (out_str_size == 0U))
    {
        return ESP_ERR_INVALID_ARG;
    }

    n = snprintf(out_str, out_str_size, "%s", src);
    if ((n <= 0) || ((size_t)n >= out_str_size))
    {
        return ESP_ERR_INVALID_SIZE;
    }

    return ESP_OK;
}

static esp_err_t usr_fs_try_prompt_path(const char *locale,
                                        const char *prompt_file_name,
                                        char *out_path,
                                        size_t out_path_size)
{
    char path_buf[USER_FS_PATH_MAX_LEN];
    const char *target_locale;
    esp_err_t ret;

    if ((prompt_file_name == NULL) || (out_path == NULL) || (out_path_size == 0U))
    {
        return ESP_ERR_INVALID_ARG;
    }

    target_locale = ((locale != NULL) && (locale[0] != '\0')) ? locale : USER_ASSETS_DEFAULT_LOCALE;

    ret = usr_fs_format_asset_path("locales",
                                   target_locale,
                                   prompt_file_name,
                                   path_buf,
                                   sizeof(path_buf));
    if ((ret == ESP_OK) && usr_fs_path_exists(path_buf))
    {
        return usr_fs_copy_string(path_buf, out_path, out_path_size);
    }

    if (strcmp(target_locale, USER_ASSETS_DEFAULT_LOCALE) != 0)
    {
        ret = usr_fs_format_asset_path("locales",
                                       USER_ASSETS_DEFAULT_LOCALE,
                                       prompt_file_name,
                                       path_buf,
                                       sizeof(path_buf));
        if ((ret == ESP_OK) && usr_fs_path_exists(path_buf))
        {
            return usr_fs_copy_string(path_buf, out_path, out_path_size);
        }
    }

    ret = usr_fs_format_asset_path("common",
                                   NULL,
                                   prompt_file_name,
                                   path_buf,
                                   sizeof(path_buf));
    if ((ret == ESP_OK) && usr_fs_path_exists(path_buf))
    {
        return usr_fs_copy_string(path_buf, out_path, out_path_size);
    }

    return ESP_ERR_NOT_FOUND;
}

static esp_err_t usr_fs_write_file_internal(const char *full_path,
                                            const uint8_t *data,
                                            size_t size,
                                            const char *mode)
{
    FILE *fp;
    size_t written;

    if (!s_ready)
    {
        return ESP_ERR_INVALID_STATE;
    }

    if ((full_path == NULL) || (mode == NULL) || ((size > 0U) && (data == NULL)))
    {
        return ESP_ERR_INVALID_ARG;
    }

    fp = fopen(full_path, mode);
    if (fp == NULL)
    {
        return ESP_FAIL;
    }

    if (size > 0U)
    {
        written = fwrite(data, 1U, size, fp);
        if (written != size)
        {
            fclose(fp);
            return ESP_FAIL;
        }
    }

    if (fflush(fp) != 0)
    {
        fclose(fp);
        return ESP_FAIL;
    }

    fclose(fp);
    return ESP_OK;
}

esp_err_t usr_fs_init(void)
{
    esp_vfs_spiffs_conf_t conf = {
        .base_path = USER_ASSETS_MOUNT_POINT,
        .partition_label = USER_ASSETS_PARTITION_LABEL,
        .max_files = USER_ASSETS_MAX_OPEN_FILES,
        .format_if_mount_failed = false,
    };
    size_t total;
    size_t used;
    esp_err_t ret;

    if (s_ready)
    {
        return ESP_OK;
    }

    ret = esp_vfs_spiffs_register(&conf);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG,
                 "esp_vfs_spiffs_register failed, label=%s base=%s err=%d",
                 USER_ASSETS_PARTITION_LABEL,
                 USER_ASSETS_MOUNT_POINT,
                 (int)ret);
        return ret;
    }

    total = 0U;
    used = 0U;
    ret = esp_spiffs_info(USER_ASSETS_PARTITION_LABEL, &total, &used);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "esp_spiffs_info failed: %d", (int)ret);
        (void)esp_vfs_spiffs_unregister(USER_ASSETS_PARTITION_LABEL);
        return ret;
    }

    s_ready = true;
    ESP_LOGI(TAG,
             "assets mounted at %s, partition=%s, used=%u/%u",
             USER_ASSETS_MOUNT_POINT,
             USER_ASSETS_PARTITION_LABEL,
             (unsigned)used,
             (unsigned)total);

    return ESP_OK;
}

bool usr_fs_is_ready(void)
{
    return s_ready;
}

const char *usr_fs_mount_point(void)
{
    return USER_ASSETS_MOUNT_POINT;
}

bool usr_fs_path_exists(const char *path)
{
    struct stat st;

    if (path == NULL)
    {
        return false;
    }

    if (stat(path, &st) != 0)
    {
        return false;
    }

    return S_ISREG(st.st_mode);
}

bool usr_fs_path_has_suffix(const char *path, const char *suffix)
{
    size_t path_len;
    size_t suffix_len;

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

    return (strcmp(path + (path_len - suffix_len), suffix) == 0);
}

esp_err_t usr_fs_format_asset_path(const char *group,
                                   const char *locale,
                                   const char *file_name,
                                   char *out_path,
                                   size_t out_path_size)
{
    int n;
    bool has_group;
    bool has_locale;
    bool has_file;

    if ((out_path == NULL) || (out_path_size == 0U))
    {
        return ESP_ERR_INVALID_ARG;
    }

    has_group = ((group != NULL) && (group[0] != '\0'));
    has_locale = ((locale != NULL) && (locale[0] != '\0'));
    has_file = ((file_name != NULL) && (file_name[0] != '\0'));

    if (has_group && has_locale && has_file)
    {
        n = snprintf(out_path,
                     out_path_size,
                     "%s/%s/%s/%s",
                     USER_ASSETS_MOUNT_POINT,
                     group,
                     locale,
                     file_name);
    }
    else if (has_group && has_file)
    {
        n = snprintf(out_path,
                     out_path_size,
                     "%s/%s/%s",
                     USER_ASSETS_MOUNT_POINT,
                     group,
                     file_name);
    }
    else if (has_group && has_locale)
    {
        n = snprintf(out_path,
                     out_path_size,
                     "%s/%s/%s",
                     USER_ASSETS_MOUNT_POINT,
                     group,
                     locale);
    }
    else if (has_group)
    {
        n = snprintf(out_path,
                     out_path_size,
                     "%s/%s",
                     USER_ASSETS_MOUNT_POINT,
                     group);
    }
    else if (has_file)
    {
        n = snprintf(out_path,
                     out_path_size,
                     "%s/%s",
                     USER_ASSETS_MOUNT_POINT,
                     file_name);
    }
    else
    {
        n = snprintf(out_path, out_path_size, "%s", USER_ASSETS_MOUNT_POINT);
    }

    if ((n <= 0) || ((size_t)n >= out_path_size))
    {
        return ESP_ERR_INVALID_SIZE;
    }

    return ESP_OK;
}

esp_err_t usr_fs_list_dir_files_with_suffix(const char *dir_path,
                                            const char *suffix,
                                            char *out_names,
                                            size_t max_files,
                                            size_t name_capacity,
                                            size_t *out_count)
{
    DIR *dir;
    struct dirent *entry;
    size_t count;

    if (!s_ready)
    {
        return ESP_ERR_INVALID_STATE;
    }

    if ((dir_path == NULL) || (out_names == NULL) || (max_files == 0U) ||
        (name_capacity < 2U) || (out_count == NULL))
    {
        return ESP_ERR_INVALID_ARG;
    }

    *out_count = 0U;
    dir = opendir(dir_path);
    if (dir == NULL)
    {
        return ESP_ERR_NOT_FOUND;
    }

    count = 0U;
    while ((entry = readdir(dir)) != NULL)
    {
        int name_len;
        char *name_slot;

        if (entry->d_name[0] == '.')
        {
            continue;
        }

        if ((suffix != NULL) && (suffix[0] != '\0') && !usr_fs_path_has_suffix(entry->d_name, suffix))
        {
            continue;
        }

        if (count >= max_files)
        {
            ESP_LOGW(TAG,
                     "too many files in %s, max=%u",
                     dir_path,
                     (unsigned)max_files);
            break;
        }

        name_slot = out_names + (count * name_capacity);
        name_len = snprintf(name_slot, name_capacity, "%s", entry->d_name);
        if ((name_len <= 0) || ((size_t)name_len >= name_capacity))
        {
            ESP_LOGW(TAG, "skip long filename: %s", entry->d_name);
            continue;
        }

        count++;
    }

    closedir(dir);
    *out_count = count;

    if (count == 0U)
    {
        return ESP_ERR_NOT_FOUND;
    }

    return ESP_OK;
}

esp_err_t usr_fs_resolve_prompt_path(const char *locale,
                                     const char *prompt_name,
                                     char *out_path,
                                     size_t out_path_size)
{
    static const char *kExtensions[] = {".pcm", ".ogg"};
    char prompt_variant[USER_FS_PATH_MAX_LEN];
    bool has_extension;
    uint32_t i;
    int n;
    esp_err_t ret;

    if ((prompt_name == NULL) || (out_path == NULL) || (out_path_size == 0U))
    {
        return ESP_ERR_INVALID_ARG;
    }

    has_extension = (strchr(prompt_name, '.') != NULL);
    if (has_extension)
    {
        return usr_fs_try_prompt_path(locale,
                                      prompt_name,
                                      out_path,
                                      out_path_size);
    }

    for (i = 0U; i < (sizeof(kExtensions) / sizeof(kExtensions[0])); i++)
    {
        n = snprintf(prompt_variant,
                     sizeof(prompt_variant),
                     "%s%s",
                     prompt_name,
                     kExtensions[i]);
        if ((n <= 0) || ((size_t)n >= sizeof(prompt_variant)))
        {
            return ESP_ERR_INVALID_SIZE;
        }

        ret = usr_fs_try_prompt_path(locale,
                                     prompt_variant,
                                     out_path,
                                     out_path_size);
        if (ret == ESP_OK)
        {
            return ESP_OK;
        }
    }

    return ESP_ERR_NOT_FOUND;
}

esp_err_t usr_fs_read_file(const char *full_path,
                           uint8_t **out_data,
                           size_t *out_size)
{
    FILE *fp;
    long file_size;
    size_t read_size;
    uint8_t *buffer;

    if (!s_ready)
    {
        return ESP_ERR_INVALID_STATE;
    }

    if ((full_path == NULL) || (out_data == NULL) || (out_size == NULL))
    {
        return ESP_ERR_INVALID_ARG;
    }

    fp = fopen(full_path, "rb");
    if (fp == NULL)
    {
        return ESP_ERR_NOT_FOUND;
    }

    if (fseek(fp, 0, SEEK_END) != 0)
    {
        fclose(fp);
        return ESP_FAIL;
    }

    file_size = ftell(fp);
    if (file_size < 0)
    {
        fclose(fp);
        return ESP_FAIL;
    }

    if (fseek(fp, 0, SEEK_SET) != 0)
    {
        fclose(fp);
        return ESP_FAIL;
    }

    buffer = (uint8_t *)usr_fs_malloc((size_t)file_size);
    if ((buffer == NULL) && (file_size > 0))
    {
        fclose(fp);
        return ESP_ERR_NO_MEM;
    }

    read_size = fread(buffer, 1U, (size_t)file_size, fp);
    fclose(fp);
    if (read_size != (size_t)file_size)
    {
        free(buffer);
        return ESP_FAIL;
    }

    *out_data = buffer;
    *out_size = (size_t)file_size;

    return ESP_OK;
}

esp_err_t usr_fs_write_file(const char *full_path,
                            const uint8_t *data,
                            size_t size)
{
    return usr_fs_write_file_internal(full_path, data, size, "wb");
}

esp_err_t usr_fs_append_file(const char *full_path,
                             const uint8_t *data,
                             size_t size)
{
    return usr_fs_write_file_internal(full_path, data, size, "ab");
}
