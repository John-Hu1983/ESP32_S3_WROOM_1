#include "ht517.h"

#define TAG "HT517"

static i2s_chan_handle_t s_tx_chan = NULL;
static bool s_ready = false;
static uint32_t s_common_ogg_count = 0U;
static uint32_t s_common_ogg_index = 0U;
static char s_common_ogg_names[HT517_COMMON_OGG_MAX_FILES][HT517_COMMON_OGG_NAME_MAX_LEN];

static esp_err_t ht517_play_ogg_buffer(const uint8_t *ogg_data, size_t ogg_bytes);

static void *ht517_audio_malloc(size_t bytes)
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

static int ht517_common_ogg_name_compare(const void *lhs, const void *rhs)
{
    const char *name_lhs = (const char *)lhs;
    const char *name_rhs = (const char *)rhs;

    return strcmp(name_lhs, name_rhs);
}

static esp_err_t ht517_scan_common_ogg_files(void)
{
    char common_dir[USER_FS_PATH_MAX_LEN];
    size_t file_count;
    esp_err_t ret;

    ret = usr_fs_format_asset_path("common",
                                   NULL,
                                   NULL,
                                   common_dir,
                                   sizeof(common_dir));
    if (ret != ESP_OK)
    {
        return ret;
    }

    ret = usr_fs_list_dir_files_with_suffix(common_dir,
                                            ".ogg",
                                            (char *)s_common_ogg_names,
                                            HT517_COMMON_OGG_MAX_FILES,
                                            HT517_COMMON_OGG_NAME_MAX_LEN,
                                            &file_count);
    if (ret != ESP_OK)
    {
        s_common_ogg_count = 0U;
        s_common_ogg_index = 0U;
        return ret;
    }

    s_common_ogg_count = (uint32_t)file_count;

    qsort(s_common_ogg_names,
          s_common_ogg_count,
          sizeof(s_common_ogg_names[0]),
          ht517_common_ogg_name_compare);

    if (s_common_ogg_index >= s_common_ogg_count)
    {
        s_common_ogg_index = 0U;
    }

    ESP_LOGI(TAG,
             "common .ogg indexed, count=%u",
             (unsigned)s_common_ogg_count);

    return ESP_OK;
}

static esp_err_t ht517_play_common_ogg_file(const char *file_name)
{
    char file_path[USER_FS_PATH_MAX_LEN];
    uint8_t *ogg_data;
    size_t ogg_size;
    esp_err_t ret;

    if ((file_name == NULL) || !usr_fs_path_has_suffix(file_name, ".ogg"))
    {
        return ESP_ERR_INVALID_ARG;
    }

    ret = usr_fs_format_asset_path("common",
                                   NULL,
                                   file_name,
                                   file_path,
                                   sizeof(file_path));
    if (ret != ESP_OK)
    {
        return ret;
    }

    ogg_data = NULL;
    ogg_size = 0U;
    ret = usr_fs_read_file(file_path, &ogg_data, &ogg_size);
    if (ret != ESP_OK)
    {
        return ret;
    }

    if (ogg_size == 0U)
    {
        free(ogg_data);
        return ESP_ERR_INVALID_SIZE;
    }

    ret = ht517_play_ogg_buffer(ogg_data, ogg_size);
    free(ogg_data);

    return ret;
}

static esp_err_t ht517_write_pcm_bytes(const uint8_t *data, size_t size)
{
    size_t total_written;

    if ((data == NULL) || (size == 0U))
    {
        return ESP_ERR_INVALID_ARG;
    }

    total_written = 0U;
    while (total_written < size)
    {
        size_t bytes_written = 0U;
        esp_err_t ret;

        ret = i2s_channel_write(s_tx_chan,
                                data + total_written,
                                size - total_written,
                                &bytes_written,
                                HT517_WRITE_TIMEOUT_MS);
        if (ret != ESP_OK)
        {
            ESP_LOGE(TAG,
                     "i2s_channel_write failed, err=%d total=%u remain=%u chunk=%u",
                     (int)ret,
                     (unsigned)total_written,
                     (unsigned)(size - total_written),
                     (unsigned)bytes_written);
            return ret;
        }

        if (bytes_written == 0U)
        {
            ESP_LOGE(TAG,
                     "i2s_channel_write timeout, err=%d total=%u remain=%u",
                     (int)ret,
                     (unsigned)total_written,
                     (unsigned)(size - total_written));
            return ESP_ERR_TIMEOUT;
        }

        total_written += bytes_written;
    }

    return ESP_OK;
}

static esp_err_t ht517_play_pcm_buffer(const uint8_t *pcm_data, size_t pcm_bytes)
{
    size_t aligned_bytes;

    if ((pcm_data == NULL) || (pcm_bytes < HT517_PCM_FRAME_BYTES))
    {
        return ESP_ERR_INVALID_ARG;
    }

    aligned_bytes = pcm_bytes - (pcm_bytes % HT517_PCM_FRAME_BYTES);
    if (aligned_bytes == 0U)
    {
        return ESP_ERR_INVALID_SIZE;
    }

    if (aligned_bytes != pcm_bytes)
    {
        ESP_LOGW(TAG,
                 "pcm data is not frame-aligned, truncating %u bytes",
                 (unsigned)(pcm_bytes - aligned_bytes));
    }

    return ht517_write_pcm_bytes(pcm_data, aligned_bytes);
}

static esp_err_t ht517_play_mono_pcm_buffer(const int16_t *mono_data, size_t mono_samples)
{
    int16_t *stereo_data;
    size_t stereo_bytes;
    size_t i;
    esp_err_t ret;

    if ((mono_data == NULL) || (mono_samples == 0U))
    {
        return ESP_ERR_INVALID_ARG;
    }

    stereo_bytes = mono_samples * HT517_STEREO_CHANNELS * sizeof(int16_t);
    stereo_data = (int16_t *)ht517_audio_malloc(stereo_bytes);
    if (stereo_data == NULL)
    {
        return ESP_ERR_NO_MEM;
    }

    for (i = 0U; i < mono_samples; i++)
    {
        size_t idx;

        idx = i * HT517_STEREO_CHANNELS;
        stereo_data[idx] = mono_data[i];
        stereo_data[idx + 1U] = mono_data[i];
    }

    ret = ht517_play_pcm_buffer((const uint8_t *)stereo_data, stereo_bytes);
    free(stereo_data);
    return ret;
}

static int ht517_parse_opus_sample_rate(const uint8_t *packet, size_t packet_len)
{
    uint32_t rate;

    if ((packet == NULL) || (packet_len < 19U))
    {
        return (int)HT517_SAMPLE_RATE_HZ;
    }

    rate = ((uint32_t)packet[12]) |
           (((uint32_t)packet[13]) << 8) |
           (((uint32_t)packet[14]) << 16) |
           (((uint32_t)packet[15]) << 24);
    if (rate == 0U)
    {
        return (int)HT517_SAMPLE_RATE_HZ;
    }

    return (int)rate;
}

static esp_err_t ht517_open_opus_decoder(void **out_decoder, int hinted_sample_rate)
{
    esp_opus_dec_cfg_t dec_cfg = {
        .sample_rate = HT517_SAMPLE_RATE_HZ,
        .channel = ESP_AUDIO_MONO,
        .frame_duration = ESP_OPUS_DEC_FRAME_DURATION_60_MS,
        .self_delimited = false,
    };
    esp_audio_err_t dec_ret;

    if (out_decoder == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    if ((hinted_sample_rate > 0) && (hinted_sample_rate != (int)HT517_SAMPLE_RATE_HZ))
    {
        ESP_LOGW(TAG,
                 "Opus sample_rate=%d, force decode/play at %uHz",
                 hinted_sample_rate,
                 (unsigned)HT517_SAMPLE_RATE_HZ);
    }

    *out_decoder = NULL;
    dec_ret = esp_opus_dec_open(&dec_cfg, sizeof(dec_cfg), out_decoder);
    if ((*out_decoder == NULL) || (dec_ret != ESP_AUDIO_ERR_OK))
    {
        ESP_LOGE(TAG, "esp_opus_dec_open failed: %d", (int)dec_ret);
        return ESP_FAIL;
    }

    return ESP_OK;
}

static esp_err_t ht517_decode_opus_packet_and_play(void *decoder,
                                                   const uint8_t *packet,
                                                   size_t packet_len)
{
    int16_t *mono_buf;
    size_t mono_buf_bytes;
    esp_audio_dec_in_raw_t raw;
    esp_audio_dec_out_frame_t out_frame;
    esp_audio_dec_info_t dec_info;
    esp_audio_err_t dec_ret;
    size_t mono_samples;
    esp_err_t ret;

    if ((decoder == NULL) || (packet == NULL) || (packet_len == 0U))
    {
        return ESP_ERR_INVALID_ARG;
    }

    mono_buf_bytes = HT517_OPUS_MAX_MONO_SAMPLES * sizeof(int16_t);
    mono_buf = (int16_t *)ht517_audio_malloc(mono_buf_bytes);
    if (mono_buf == NULL)
    {
        return ESP_ERR_NO_MEM;
    }

    raw.buffer = (uint8_t *)packet;
    raw.len = (uint32_t)packet_len;
    raw.consumed = 0U;
    raw.frame_recover = ESP_AUDIO_DEC_RECOVERY_NONE;

    out_frame.buffer = (uint8_t *)mono_buf;
    out_frame.len = (uint32_t)mono_buf_bytes;
    out_frame.decoded_size = 0U;

    memset(&dec_info, 0, sizeof(dec_info));
    dec_ret = esp_opus_dec_decode(decoder, &raw, &out_frame, &dec_info);
    if (dec_ret != ESP_AUDIO_ERR_OK)
    {
        ESP_LOGE(TAG,
                 "esp_opus_dec_decode failed, err=%d packet=%u",
                 (int)dec_ret,
                 (unsigned)packet_len);
        free(mono_buf);
        return ESP_FAIL;
    }

    if ((out_frame.decoded_size % sizeof(int16_t)) != 0U)
    {
        ESP_LOGW(TAG, "decoded_size not aligned: %u", (unsigned)out_frame.decoded_size);
    }

    mono_samples = out_frame.decoded_size / sizeof(int16_t);
    ret = ESP_OK;
    if (mono_samples > 0U)
    {
        ret = ht517_play_mono_pcm_buffer(mono_buf, mono_samples);
    }

    free(mono_buf);
    return ret;
}

static esp_err_t ht517_process_ogg_packet(struct ht517_ogg_parser_ctx *ctx,
                                          void **decoder,
                                          const uint8_t *packet,
                                          size_t packet_len)
{
    esp_err_t ret;

    if ((ctx == NULL) || (decoder == NULL) || (packet == NULL))
    {
        return ESP_ERR_INVALID_ARG;
    }

    if (packet_len == 0U)
    {
        return ESP_OK;
    }

    if ((!ctx->head_seen) && (packet_len >= 8U) && (memcmp(packet, "OpusHead", 8U) == 0))
    {
        ctx->sample_rate = ht517_parse_opus_sample_rate(packet, packet_len);
        ctx->head_seen = true;
        return ESP_OK;
    }

    if ((!ctx->tags_seen) && (packet_len >= 8U) && (memcmp(packet, "OpusTags", 8U) == 0))
    {
        ctx->tags_seen = true;
        return ESP_OK;
    }

    if ((!ctx->head_seen) || (!ctx->tags_seen))
    {
        ESP_LOGW(TAG, "drop ogg packet before OpusHead/OpusTags ready");
        return ESP_OK;
    }

    if (*decoder == NULL)
    {
        ret = ht517_open_opus_decoder(decoder, ctx->sample_rate);
        if (ret != ESP_OK)
        {
            return ret;
        }
    }

    ret = ht517_decode_opus_packet_and_play(*decoder, packet, packet_len);
    if (ret == ESP_OK)
    {
        ctx->audio_packet_count++;
    }

    return ret;
}

/* Follow Xiaozhi path: demux OGG container into Opus packets, then decode to PCM for I2S playback. */
static esp_err_t ht517_play_ogg_buffer(const uint8_t *ogg_data, size_t ogg_bytes)
{
    struct ht517_ogg_parser_ctx ctx;
    size_t offset;
    void *decoder;
    esp_err_t ret;

    if ((ogg_data == NULL) || (ogg_bytes == 0U))
    {
        return ESP_ERR_INVALID_ARG;
    }

    memset(&ctx, 0, sizeof(ctx));
    ctx.sample_rate = (int)HT517_SAMPLE_RATE_HZ;
    offset = 0U;
    decoder = NULL;
    ret = ESP_OK;

    ctx.packet_capacity = HT517_OPUS_MAX_PACKET_BYTES;
    ctx.packet_buf = (uint8_t *)ht517_audio_malloc(ctx.packet_capacity);
    if (ctx.packet_buf == NULL)
    {
        return ESP_ERR_NO_MEM;
    }

    while ((offset + 27U) <= ogg_bytes)
    {
        const uint8_t *header;
        uint8_t seg_count;
        size_t seg_table_offset;
        const uint8_t *seg_table;
        size_t body_offset;
        size_t body_size;
        size_t page_data_offset;
        uint32_t i;

        if (memcmp(ogg_data + offset, "OggS", 4U) != 0)
        {
            offset++;
            continue;
        }

        header = ogg_data + offset;
        if (header[4] != 0U)
        {
            ESP_LOGW(TAG, "invalid ogg version=%u", (unsigned)header[4]);
            offset++;
            continue;
        }

        seg_count = header[26];
        seg_table_offset = offset + 27U;
        if ((seg_table_offset + (size_t)seg_count) > ogg_bytes)
        {
            ret = ESP_ERR_INVALID_SIZE;
            break;
        }

        seg_table = ogg_data + seg_table_offset;
        body_offset = seg_table_offset + (size_t)seg_count;
        body_size = 0U;
        for (i = 0U; i < (uint32_t)seg_count; i++)
        {
            body_size += seg_table[i];
        }

        if ((body_offset + body_size) > ogg_bytes)
        {
            ret = ESP_ERR_INVALID_SIZE;
            break;
        }

        page_data_offset = body_offset;
        for (i = 0U; i < (uint32_t)seg_count; i++)
        {
            uint8_t seg_len;

            seg_len = seg_table[i];
            if ((ctx.packet_len + (size_t)seg_len) > ctx.packet_capacity)
            {
                ESP_LOGE(TAG,
                         "ogg packet too large: %u + %u > %u",
                         (unsigned)ctx.packet_len,
                         (unsigned)seg_len,
                         (unsigned)ctx.packet_capacity);
                ret = ESP_ERR_INVALID_SIZE;
                break;
            }

            if (seg_len > 0U)
            {
                memcpy(ctx.packet_buf + ctx.packet_len, ogg_data + page_data_offset, seg_len);
                page_data_offset += seg_len;
                ctx.packet_len += seg_len;
            }

            if (seg_len < 255U)
            {
                ret = ht517_process_ogg_packet(&ctx, &decoder, ctx.packet_buf, ctx.packet_len);
                ctx.packet_len = 0U;
                if (ret != ESP_OK)
                {
                    break;
                }
            }
        }

        if (ret != ESP_OK)
        {
            break;
        }

        offset = body_offset + body_size;
    }

    if ((ret == ESP_OK) && (ctx.audio_packet_count == 0U))
    {
        ESP_LOGW(TAG, "no opus audio packet found in ogg stream");
        ret = ESP_ERR_NOT_FOUND;
    }

    free(ctx.packet_buf);

    if (decoder != NULL)
    {
        (void)esp_opus_dec_close(decoder);
    }

    return ret;
}

bool ht517_is_ready(void)
{
    return s_ready;
}

esp_err_t ht517_init_device(void)
{
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM, I2S_ROLE_MASTER);
    i2s_chan_info_t chan_info = {0};
    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(HT517_SAMPLE_RATE_HZ),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,
                                                        I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = I2S_BCK_IO,
            .ws = I2S_WS_IO,
            .dout = I2S_DO_IO,
            .din = I2S_GPIO_UNUSED,
            .invert_flags = {
                .mclk_inv = 0,
                .bclk_inv = 0,
                .ws_inv = 0,
            },
        },
    };
    uint32_t dma_total;
    esp_err_t ret;

    if (s_ready)
    {
        return ESP_OK;
    }

    chan_cfg.auto_clear = true;

    ret = i2s_new_channel(&chan_cfg, &s_tx_chan, NULL);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "i2s_new_channel failed: %d", (int)ret);
        return ret;
    }

    ret = i2s_channel_init_std_mode(s_tx_chan, &std_cfg);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "i2s_channel_init_std_mode failed: %d", (int)ret);
        (void)i2s_del_channel(s_tx_chan);
        s_tx_chan = NULL;
        return ret;
    }

    ret = i2s_channel_enable(s_tx_chan);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "i2s_channel_enable failed: %d", (int)ret);
        (void)i2s_del_channel(s_tx_chan);
        s_tx_chan = NULL;
        return ret;
    }

    ret = i2s_channel_get_info(s_tx_chan, &chan_info);
    dma_total = 0U;
    if (ret != ESP_OK)
    {
        ESP_LOGW(TAG, "i2s_channel_get_info failed: %d", (int)ret);
    }
    else
    {
        dma_total = chan_info.total_dma_buf_size;
    }

    s_ready = true;

    ESP_LOGI(TAG, "HT517 I2S ready, fs=%u, bclk=%d ws=%d dout=%d, dma_total=%u",
             HT517_SAMPLE_RATE_HZ,
             (int)I2S_BCK_IO,
             (int)I2S_WS_IO,
             (int)I2S_DO_IO,
             (unsigned)dma_total);

    return ESP_OK;
}

esp_err_t ht517_play_next_common_ogg(void)
{
    esp_err_t ret;
    uint32_t current_index;

    if (!s_ready || (s_tx_chan == NULL) || !usr_fs_is_ready())
    {
        return ESP_ERR_INVALID_STATE;
    }

    if (s_common_ogg_count == 0U)
    {
        ret = ht517_scan_common_ogg_files();
        if (ret != ESP_OK)
        {
            return ret;
        }
    }

    current_index = s_common_ogg_index;
    ret = ht517_play_common_ogg_file(s_common_ogg_names[current_index]);
    if (ret != ESP_OK)
    {
        if (ret == ESP_ERR_NOT_FOUND)
        {
            ret = ht517_scan_common_ogg_files();
            if (ret == ESP_OK)
            {
                current_index = s_common_ogg_index;
                ret = ht517_play_common_ogg_file(s_common_ogg_names[current_index]);
            }
        }

        if (ret != ESP_OK)
        {
            ESP_LOGW(TAG,
                     "play common .ogg failed, idx=%u err=%d",
                     (unsigned)current_index,
                     (int)ret);
            return ret;
        }
    }

    ESP_LOGI(TAG,
             "played common .ogg [%u/%u]: %s",
             (unsigned)(current_index + 1U),
             (unsigned)s_common_ogg_count,
             s_common_ogg_names[current_index]);

    s_common_ogg_index = (current_index + 1U) % s_common_ogg_count;
    return ESP_OK;
}

esp_err_t ht517_play_prompt_from_storage(const char *locale, const char *prompt_name)
{
    char prompt_path[USER_FS_PATH_MAX_LEN];
    uint8_t *prompt_data;
    size_t prompt_size;
    bool is_pcm;
    esp_err_t ret;

    if (!s_ready || (s_tx_chan == NULL) || !usr_fs_is_ready())
    {
        return ESP_ERR_INVALID_STATE;
    }

    prompt_data = NULL;
    prompt_size = 0U;
    is_pcm = false;

    ret = usr_fs_resolve_prompt_path(locale,
                                     prompt_name,
                                     prompt_path,
                                     sizeof(prompt_path));
    if (ret != ESP_OK)
    {
        ESP_LOGW(TAG,
                 "resolve prompt path failed, locale=%s name=%s err=%d",
                 (locale != NULL) ? locale : "(null)",
                 (prompt_name != NULL) ? prompt_name : "(null)",
                 (int)ret);
        return ret;
    }

    ret = usr_fs_read_file(prompt_path,
                           &prompt_data,
                           &prompt_size);
    if (ret != ESP_OK)
    {
        ESP_LOGW(TAG,
                 "read prompt failed, path=%s err=%d",
                 prompt_path,
                 (int)ret);
        return ret;
    }

    is_pcm = usr_fs_path_has_suffix(prompt_path, ".pcm");

    if (prompt_size == 0U)
    {
        free(prompt_data);
        return ESP_ERR_INVALID_SIZE;
    }

    if (!is_pcm)
    {
        ret = ht517_play_ogg_buffer(prompt_data, prompt_size);
        free(prompt_data);
        return ret;
    }

    ret = ht517_play_pcm_buffer(prompt_data, prompt_size);
    free(prompt_data);

    return ret;
}
