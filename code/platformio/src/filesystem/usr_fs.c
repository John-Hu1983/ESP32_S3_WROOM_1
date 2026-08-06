#include "usr_fs.h"

#define TAG "USR_FS"

#ifndef USER_FS_PRINT_ALL_FILES_ON_INIT
#define USER_FS_PRINT_ALL_FILES_ON_INIT 0
#endif

static bool s_ready = false;

/*
 * brief: Allocate filesystem buffer from PSRAM first, then fallback to internal RAM.
 * input: bytes - requested allocation size in bytes.
 * output: Allocated buffer pointer on success; otherwise NULL.
 */
static void *_usr_fs_malloc(size_t bytes)
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
 * brief: Copy one string into caller buffer with bounds checking.
 * input: src - source C string; out_str - destination buffer; out_str_size - destination capacity.
 * output: ESP_OK on success; otherwise ESP_ERR_INVALID_ARG or ESP_ERR_INVALID_SIZE.
 */
static esp_err_t _usr_fs_copy_string(const char *src,
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

/*
 * brief: Resolve a prompt path by trying locale, default locale, then common directory.
 * input: locale - preferred locale string (optional); prompt_file_name - prompt file name;
 *        out_path - output path buffer; out_path_size - output buffer capacity.
 * output: ESP_OK on success; otherwise ESP_ERR_INVALID_ARG or ESP_ERR_NOT_FOUND.
 */
static esp_err_t _usr_fs_try_prompt_path(const char *locale,
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
        return _usr_fs_copy_string(path_buf, out_path, out_path_size);
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
            return _usr_fs_copy_string(path_buf, out_path, out_path_size);
        }
    }

    ret = usr_fs_format_asset_path("common",
                                   NULL,
                                   prompt_file_name,
                                   path_buf,
                                   sizeof(path_buf));
    if ((ret == ESP_OK) && usr_fs_path_exists(path_buf))
    {
        return _usr_fs_copy_string(path_buf, out_path, out_path_size);
    }

    return ESP_ERR_NOT_FOUND;
}

/*
 * brief: Write or append bytes to a file using the provided fopen mode.
 * input: full_path - target file path; data - source buffer; size - byte count; mode - fopen mode.
 * output: ESP_OK on success; otherwise ESP_ERR_INVALID_STATE/ARG or ESP_FAIL.
 */
static esp_err_t _usr_fs_write_file_internal(const char *full_path,
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

/*
 * brief: Mount SPIFFS assets partition and mark filesystem service ready.
 * input: None.
 * output: ESP_OK on success; otherwise propagated SPIFFS initialization error.
 */
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
#if USER_FS_PRINT_ALL_FILES_ON_INIT
    size_t all_count;
    size_t i;
    char **all_paths;
    esp_err_t search_ret;
#endif
    double used_percent;
    double remain_percent;

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

    if (total == 0U)
    {
        used_percent = 0.0;
        remain_percent = 0.0;
    }
    else
    {
        used_percent = ((double)used * 100.0) / (double)total;
        remain_percent = ((double)(total - used) * 100.0) / (double)total;
    }

    s_ready = true;
    ESP_LOGI(TAG,
             "assets mounted at %s, partition=%s, used=%u/%u, used=%.2f%%, remain=%.2f%%",
             USER_ASSETS_MOUNT_POINT,
             USER_ASSETS_PARTITION_LABEL,
             (unsigned)used,
             (unsigned)total,
             used_percent,
             remain_percent);

#if USER_FS_PRINT_ALL_FILES_ON_INIT
    all_paths = NULL;
    all_count = 0U;
    search_ret = usr_fs_search_all_files(NULL, &all_paths, &all_count);
    if (search_ret == ESP_OK)
    {
        ESP_LOGI(TAG,
                 "assets file count: %u",
                 (unsigned)all_count);
        for (i = 0U; i < all_count; i++)
        {
            ESP_LOGI(TAG,
                     "asset[%u]: %s",
                     (unsigned)i,
                     all_paths[i]);
        }
        usr_fs_free_file_paths(all_paths, all_count);
    }
    else if (search_ret == ESP_ERR_NOT_FOUND)
    {
        ESP_LOGW(TAG, "no files found under %s", USER_ASSETS_MOUNT_POINT);
    }
    else
    {
        ESP_LOGW(TAG, "usr_fs_search_all_files failed: %d", (int)search_ret);
    }
#endif

    return ESP_OK;
}

/*
 * brief: Query whether filesystem service has been initialized.
 * input: None.
 * output: true when assets filesystem is mounted; otherwise false.
 */
bool usr_fs_is_ready(void)
{
    return s_ready;
}

/*
 * brief: Return the configured assets mount point.
 * input: None.
 * output: Constant mount-point string.
 */
const char *usr_fs_mount_point(void)
{
    return USER_ASSETS_MOUNT_POINT;
}

/*
 * brief: Check whether a path exists and is a regular file.
 * input: path - full path string.
 * output: true when path exists and points to a regular file; otherwise false.
 */
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

/*
 * brief: Check whether a file path ends with the specified suffix.
 * input: path - source path string; suffix - suffix to match.
 * output: true when suffix matches; otherwise false.
 */
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

/*
 * brief: Free list memory that stores dynamically allocated file paths.
 * input: paths - array of file-path pointers; count - number of valid entries.
 * output: None.
 */
static void _usr_fs_free_file_paths_internal(char **paths, size_t count)
{
    size_t i;

    if (paths == NULL)
    {
        return;
    }

    for (i = 0U; i < count; i++)
    {
        free(paths[i]);
    }

    free(paths);
}

/*
 * brief: Append one file path into dynamic path array with capacity growth.
 * input: paths - pointer to path-array pointer; count - item count pointer;
 *        capacity - capacity pointer; path - full file path to append.
 * output: ESP_OK on success; otherwise ESP_ERR_INVALID_ARG or ESP_ERR_NO_MEM.
 */
static esp_err_t _usr_fs_append_file_path(char ***paths,
                                         size_t *count,
                                         size_t *capacity,
                                         const char *path)
{
    char **new_paths;
    char *path_copy;
    size_t path_len;
    size_t new_capacity;

    if ((paths == NULL) || (count == NULL) || (capacity == NULL) || (path == NULL))
    {
        return ESP_ERR_INVALID_ARG;
    }

    if (*count >= *capacity)
    {
        new_capacity = (*capacity == 0U) ? 8U : (*capacity * 2U);
        new_paths = (char **)realloc(*paths, new_capacity * sizeof(char *));
        if (new_paths == NULL)
        {
            return ESP_ERR_NO_MEM;
        }

        *paths = new_paths;
        *capacity = new_capacity;
    }

    path_len = strlen(path);
    path_copy = (char *)malloc(path_len + 1U);
    if (path_copy == NULL)
    {
        return ESP_ERR_NO_MEM;
    }

    memcpy(path_copy, path, path_len + 1U);
    (*paths)[*count] = path_copy;
    (*count)++;

    return ESP_OK;
}

/*
 * brief: Recursively collect all regular-file paths from a directory.
 * input: dir_path - root directory for recursive walk; paths/count/capacity - dynamic list state.
 * output: ESP_OK on success; otherwise ESP_ERR_NOT_FOUND/INVALID_SIZE/NO_MEM.
 */
static esp_err_t _usr_fs_collect_files_recursive(const char *dir_path,
                                                char ***paths,
                                                size_t *count,
                                                size_t *capacity)
{
    DIR *dir;
    struct dirent *entry;
    esp_err_t ret;

    if ((dir_path == NULL) || (paths == NULL) || (count == NULL) || (capacity == NULL))
    {
        return ESP_ERR_INVALID_ARG;
    }

    dir = opendir(dir_path);
    if (dir == NULL)
    {
        return ESP_ERR_NOT_FOUND;
    }

    ret = ESP_OK;
    while ((entry = readdir(dir)) != NULL)
    {
        char full_path[USER_FS_PATH_MAX_LEN];
        struct stat st;
        int n;

        if ((entry->d_name[0] == '.') &&
            ((entry->d_name[1] == '\0') ||
             ((entry->d_name[1] == '.') && (entry->d_name[2] == '\0'))))
        {
            continue;
        }

        n = snprintf(full_path, sizeof(full_path), "%s/%s", dir_path, entry->d_name);
        if ((n <= 0) || ((size_t)n >= sizeof(full_path)))
        {
            ret = ESP_ERR_INVALID_SIZE;
            break;
        }

        if (stat(full_path, &st) != 0)
        {
            continue;
        }

        if (S_ISDIR(st.st_mode))
        {
            ret = _usr_fs_collect_files_recursive(full_path, paths, count, capacity);
            if (ret != ESP_OK)
            {
                break;
            }
            continue;
        }

        if (S_ISREG(st.st_mode))
        {
            ret = _usr_fs_append_file_path(paths, count, capacity, full_path);
            if (ret != ESP_OK)
            {
                break;
            }
        }
    }

    closedir(dir);
    return ret;
}

/*
 * brief: Compose an absolute assets path from optional group, locale, and file segments.
 * input: group/locale/file_name - optional path segments; out_path - output buffer;
 *        out_path_size - output buffer capacity.
 * output: ESP_OK on success; otherwise ESP_ERR_INVALID_ARG or ESP_ERR_INVALID_SIZE.
 */
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

/*
 * brief: Enumerate directory file names with optional suffix filtering.
 * input: dir_path - directory path; suffix - optional suffix filter; out_names - output slots;
 *        max_files - slot count; name_capacity - bytes per slot; out_count - result file count.
 * output: ESP_OK on success; otherwise state/argument/not-found error.
 */
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

/*
 * brief: Resolve a prompt file path, probing extension variants when needed.
 * input: locale - preferred locale string; prompt_name - prompt base name or file name;
 *        out_path - output path buffer; out_path_size - output buffer capacity.
 * output: ESP_OK on success; otherwise ESP_ERR_INVALID_ARG/SIZE or ESP_ERR_NOT_FOUND.
 */
esp_err_t usr_fs_resolve_prompt_path(const char *locale,
                                     const char *prompt_name,
                                     char *out_path,
                                     size_t out_path_size)
{
    static const char *kExtensions[] = {".pcm", ".ogg", ".wav"};
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
        return _usr_fs_try_prompt_path(locale,
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

        ret = _usr_fs_try_prompt_path(locale,
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

/*
 * brief: Recursively search all regular files and return malloc-allocated full-path list.
 * input: root_dir - search root path (NULL or empty means mount point);
 *        out_paths - output path-array pointer; out_count - output item count.
 * output: ESP_OK on success; otherwise state/argument/not-found/no-mem/size error.
 */
esp_err_t usr_fs_search_all_files(const char *root_dir,
                                  char ***out_paths,
                                  size_t *out_count)
{
    const char *search_root;
    char **paths;
    char **shrink_paths;
    size_t count;
    size_t capacity;
    esp_err_t ret;

    if (!s_ready)
    {
        return ESP_ERR_INVALID_STATE;
    }

    if ((out_paths == NULL) || (out_count == NULL))
    {
        return ESP_ERR_INVALID_ARG;
    }

    *out_paths = NULL;
    *out_count = 0U;

    search_root = ((root_dir != NULL) && (root_dir[0] != '\0')) ? root_dir : USER_ASSETS_MOUNT_POINT;

    paths = NULL;
    count = 0U;
    capacity = 0U;

    ret = _usr_fs_collect_files_recursive(search_root, &paths, &count, &capacity);
    if (ret != ESP_OK)
    {
        _usr_fs_free_file_paths_internal(paths, count);
        return ret;
    }

    if (count == 0U)
    {
        _usr_fs_free_file_paths_internal(paths, count);
        return ESP_ERR_NOT_FOUND;
    }

    shrink_paths = (char **)realloc(paths, count * sizeof(char *));
    if (shrink_paths != NULL)
    {
        paths = shrink_paths;
    }

    *out_paths = paths;
    *out_count = count;
    return ESP_OK;
}

/*
 * brief: Free file-path list returned by usr_fs_search_all_files.
 * input: paths - path array pointer; count - number of path entries.
 * output: None.
 */
void usr_fs_free_file_paths(char **paths, size_t count)
{
    _usr_fs_free_file_paths_internal(paths, count);
}

/*
 * brief: Read the entire file into a newly allocated byte buffer.
 * input: full_path - target file path; out_data - output pointer to buffer; out_size - output byte size.
 * output: ESP_OK on success; otherwise state/argument/not-found/no-mem or IO error.
 */
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

    buffer = (uint8_t *)_usr_fs_malloc((size_t)file_size);
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

/*
 * brief: Overwrite file content with provided bytes.
 * input: full_path - target file path; data - source buffer; size - byte count.
 * output: ESP_OK on success; otherwise propagated write failure.
 */
esp_err_t usr_fs_write_file(const char *full_path,
                            const uint8_t *data,
                            size_t size)
{
    return _usr_fs_write_file_internal(full_path, data, size, "wb");
}

/*
 * brief: Append provided bytes to file content.
 * input: full_path - target file path; data - source buffer; size - byte count.
 * output: ESP_OK on success; otherwise propagated append failure.
 */
esp_err_t usr_fs_append_file(const char *full_path,
                             const uint8_t *data,
                             size_t size)
{
    return _usr_fs_write_file_internal(full_path, data, size, "ab");
}
