#include "voice_common.h"

#include "esp_heap_caps.h"

#define TAG "VOICE_COMMON"

static bool s_ready = false;
static TaskHandle_t s_play_task = NULL;
#if CONFIG_SPIRAM_ALLOW_STACK_EXTERNAL_MEMORY
static StaticTask_t s_play_task_tcb;
static StackType_t *s_play_task_stack = NULL;
#endif
static volatile bool s_playing = false;
static uint32_t s_play_queue_len = 0U;
static portMUX_TYPE s_play_list_lock = portMUX_INITIALIZER_UNLOCKED;
static voice_play_item_t *s_play_head = NULL;
static voice_play_item_t *s_play_tail = NULL;

/*
 * brief: Free one playback queue node and its owned path buffer.
 * input: item - queue node pointer.
 * output: None.
 */
static void _voice_free_play_item(voice_play_item_t *item)
{
    if (item == NULL)
    {
        return;
    }

    free(item->file_path);
    free(item);
}

/*
 * brief: Pop one queued playback path from FIFO linked list.
 * input: None.
 * output: Queue node pointer when available; otherwise NULL.
 */
static voice_play_item_t *_voice_pop_play_item(void)
{
    voice_play_item_t *item;

    portENTER_CRITICAL(&s_play_list_lock);

    item = s_play_head;
    if (item != NULL)
    {
        s_play_head = item->next;
        if (s_play_head == NULL)
        {
            s_play_tail = NULL;
        }
        item->next = NULL;
        if (s_play_queue_len > 0U)
        {
            s_play_queue_len--;
        }
    }

    portEXIT_CRITICAL(&s_play_list_lock);

    return item;
}

/*
 * brief: Resolve one prompt filename through locale/default/common search order.
 * input: locale - preferred locale; prompt_name - prompt filename with extension;
 *        out_path - output buffer; out_path_size - output buffer size.
 * output: ESP_OK when found; otherwise ESP_ERR_NOT_FOUND or formatter errors.
 */
static esp_err_t _voice_try_prompt_path(const char *locale,
                                        const char *prompt_name,
                                        char *out_path,
                                        size_t out_path_size)
{
    esp_err_t ret;

    if ((prompt_name == NULL) || (out_path == NULL) || (out_path_size == 0U))
    {
        return ESP_ERR_INVALID_ARG;
    }

    if ((locale != NULL) && (locale[0] != '\0'))
    {
        ret = usr_fs_format_asset_path("locales", locale, prompt_name, out_path, out_path_size);
        if ((ret == ESP_OK) && usr_fs_path_exists(out_path))
        {
            return ESP_OK;
        }
    }

    ret = usr_fs_format_asset_path("locales", USER_ASSETS_DEFAULT_LOCALE, prompt_name, out_path, out_path_size);
    if ((ret == ESP_OK) && usr_fs_path_exists(out_path))
    {
        return ESP_OK;
    }

    ret = usr_fs_format_asset_path("common", NULL, prompt_name, out_path, out_path_size);
    if ((ret == ESP_OK) && usr_fs_path_exists(out_path))
    {
        return ESP_OK;
    }

    return ESP_ERR_NOT_FOUND;
}

/*
 * brief: Check whether full path suffix is supported by voice playback pipeline.
 * input: file_path - full path string.
 * output: true for .ogg/.pcm/.wav paths; otherwise false.
 */
bool voice_path_is_supported(const char *file_path)
{
    if (file_path == NULL)
    {
        return false;
    }

    return usr_fs_path_has_suffix(file_path, ".ogg") ||
           usr_fs_path_has_suffix(file_path, ".pcm") ||
           usr_fs_path_has_suffix(file_path, ".wav");
}

/*
 * brief: Resolve prompt file path and include wav extension probing.
 * input: locale - preferred locale string; prompt_name - prompt base name or file name;
 *        out_path - output path buffer; out_path_size - output buffer capacity.
 * output: ESP_OK on success; otherwise ESP_ERR_INVALID_ARG/SIZE or ESP_ERR_NOT_FOUND.
 */
esp_err_t voice_resolve_prompt_path(const char *locale,
                                    const char *prompt_name,
                                    char *out_path,
                                    size_t out_path_size)
{
    char prompt_variant[USER_FS_PATH_MAX_LEN];
    bool has_extension;
    int n;
    esp_err_t ret;

    if ((prompt_name == NULL) || (out_path == NULL) || (out_path_size == 0U))
    {
        return ESP_ERR_INVALID_ARG;
    }

    has_extension = (strchr(prompt_name, '.') != NULL);
    if (has_extension)
    {
        return _voice_try_prompt_path(locale, prompt_name, out_path, out_path_size);
    }

    ret = usr_fs_resolve_prompt_path(locale, prompt_name, out_path, out_path_size);
    if (ret == ESP_OK)
    {
        return ESP_OK;
    }

    n = snprintf(prompt_variant, sizeof(prompt_variant), "%s.wav", prompt_name);
    if ((n <= 0) || ((size_t)n >= sizeof(prompt_variant)))
    {
        return ESP_ERR_INVALID_SIZE;
    }

    return _voice_try_prompt_path(locale, prompt_variant, out_path, out_path_size);
}

/*
 * brief: Load one audio file by full path and play according to file suffix.
 * input: file_path - full path ending with .ogg/.pcm/.wav.
 * output: ESP_OK on success; otherwise argument, filesystem, decode, or playback error.
 */
static esp_err_t _voice_play_file_path(const char *file_path)
{
    uint8_t *audio_data;
    size_t audio_size;
    esp_err_t ret;

    if (!voice_path_is_supported(file_path))
    {
        return ESP_ERR_INVALID_ARG;
    }

    audio_data = NULL;
    audio_size = 0U;
    ret = usr_fs_read_file(file_path, &audio_data, &audio_size);
    if (ret != ESP_OK)
    {
        return ret;
    }

    if (audio_size == 0U)
    {
        free(audio_data);
        return ESP_ERR_INVALID_SIZE;
    }

    if (usr_fs_path_has_suffix(file_path, ".ogg"))
    {
        ret = ht517_set_sample_rate(HT517_SAMPLE_RATE_HZ);
        if (ret != ESP_OK)
        {
            free(audio_data);
            return ret;
        }
        ret = voice_ogg_play_buffer(audio_data, audio_size);
    }
    else if (usr_fs_path_has_suffix(file_path, ".wav"))
    {
        ret = voice_wav_play_buffer(audio_data, audio_size);
    }
    else
    {
        ret = ht517_set_sample_rate(HT517_SAMPLE_RATE_HZ);
        if (ret != ESP_OK)
        {
            free(audio_data);
            return ret;
        }
        ret = voice_pcm_play_buffer(audio_data, audio_size);
    }

    free(audio_data);
    return ret;
}

/*
 * brief: Playback worker task that consumes FIFO list and plays queued files in order.
 * input: arg - unused task argument.
 * output: None.
 */
static void _voice_playback_task(void *arg)
{
    (void)arg;

    for (;;)
    {
        voice_play_item_t *item;
        esp_err_t ret;

        if (s_playing)
        {
            delay_ms(VOICE_PLAY_TASK_IDLE_MS);
            continue;
        }

        item = _voice_pop_play_item();
        if (item == NULL)
        {
            delay_ms(VOICE_PLAY_TASK_IDLE_MS);
            continue;
        }

        s_playing = true;
        ret = _voice_play_file_path(item->file_path);
        s_playing = false;

        if (ret != ESP_OK)
        {
            ESP_LOGW(TAG,
                     "play queue path failed, path=%s err=%d",
                     item->file_path,
                     (int)ret);
        }

        _voice_free_play_item(item);
    }
}

/*
 * brief: Initialize voice service and create playback task.
 * input: None.
 * output: ESP_OK on success; otherwise propagated initialization error.
 */
esp_err_t voice_init_player(void)
{
    BaseType_t task_ret;

    if (s_ready)
    {
        return ESP_OK;
    }

    if (!usr_fs_is_ready() || !ht517_is_ready())
    {
        return ESP_ERR_INVALID_STATE;
    }

#if CONFIG_SPIRAM_ALLOW_STACK_EXTERNAL_MEMORY
    if (s_play_task_stack == NULL)
    {
        s_play_task_stack = (StackType_t *)heap_caps_malloc(VOICE_PLAY_TASK_STACK_BYTES,
                                                             MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }

    if (s_play_task_stack != NULL)
    {
        s_play_task = xTaskCreateStatic(_voice_playback_task,
                                        "voice_play",
                                        VOICE_PLAY_TASK_STACK_BYTES / sizeof(StackType_t),
                                        NULL,
                                        VOICE_PLAY_TASK_PRIORITY,
                                        s_play_task_stack,
                                        &s_play_task_tcb);
    }
#endif

    if (s_play_task == NULL)
    {
        task_ret = xTaskCreate(_voice_playback_task,
                               "voice_play",
                               VOICE_PLAY_TASK_STACK_BYTES,
                               NULL,
                               VOICE_PLAY_TASK_PRIORITY,
                               &s_play_task);
        if (task_ret != pdPASS)
        {
            s_play_task = NULL;
            return ESP_ERR_NO_MEM;
        }
    }

    portENTER_CRITICAL(&s_play_list_lock);
    s_play_head = NULL;
    s_play_tail = NULL;
    s_play_queue_len = 0U;
    s_playing = false;
    s_ready = true;
    portEXIT_CRITICAL(&s_play_list_lock);

    ESP_LOGI(TAG, "voice service ready");
    return ESP_OK;
}

/*
 * brief: Append one full path into FIFO playback list.
 * input: path - full path ending with .ogg/.pcm/.wav.
 * output: ESP_OK on success; otherwise state/argument/no-mem/not-found error.
 */
esp_err_t voice_load_file(const char *path)
{
    voice_play_item_t *item;
    size_t path_len;

    if (!s_ready || (s_play_task == NULL) || !usr_fs_is_ready() || !ht517_is_ready())
    {
        return ESP_ERR_INVALID_STATE;
    }

    if (!voice_path_is_supported(path))
    {
        return ESP_ERR_INVALID_ARG;
    }

    if (!usr_fs_path_exists(path))
    {
        return ESP_ERR_NOT_FOUND;
    }

    item = (voice_play_item_t *)malloc(sizeof(voice_play_item_t));
    if (item == NULL)
    {
        return ESP_ERR_NO_MEM;
    }

    path_len = strlen(path);
    item->file_path = (char *)malloc(path_len + 1U);
    if (item->file_path == NULL)
    {
        free(item);
        return ESP_ERR_NO_MEM;
    }

    memcpy(item->file_path, path, path_len + 1U);
    item->next = NULL;

    portENTER_CRITICAL(&s_play_list_lock);
    if (s_play_tail == NULL)
    {
        s_play_head = item;
        s_play_tail = item;
    }
    else
    {
        s_play_tail->next = item;
        s_play_tail = item;
    }
    s_play_queue_len++;
    portEXIT_CRITICAL(&s_play_list_lock);

    return ESP_OK;
}

/*
 * brief: Read current voice service runtime status.
 * input: None.
 * output: Status snapshot containing ready/playing/queue.
 */
voice_info_s voice_read_info(void)
{
    voice_info_s info;

    portENTER_CRITICAL(&s_play_list_lock);
    info.ready = s_ready;
    info.playing = s_playing;
    info.queue_len = s_play_queue_len;
    portEXIT_CRITICAL(&s_play_list_lock);

    return info;
}
