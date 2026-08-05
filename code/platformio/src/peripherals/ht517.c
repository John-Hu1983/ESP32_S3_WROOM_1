#include "ht517.h"

#define TAG "HT517"

static i2s_chan_handle_t s_tx_chan = NULL;
static bool s_ready = false;
static TaskHandle_t s_play_task = NULL;
static volatile bool s_playing = false;
static uint8_t s_gain_percent = HT517_DEFAULT_GAIN_PERCENT;
static uint32_t s_play_queue_len = 0U;
static portMUX_TYPE s_play_list_lock = portMUX_INITIALIZER_UNLOCKED;
static ht517_play_item_t *s_play_head = NULL;
static ht517_play_item_t *s_play_tail = NULL;

/*
 * brief: Allocate audio buffer from PSRAM first, then fallback to internal RAM.
 * input: bytes - requested allocation size in bytes.
 * output: Allocated buffer pointer on success; otherwise NULL.
 */
static void *_ht517_audio_malloc(size_t bytes)
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
 * brief: Validate whether path suffix is supported by HT517 playback pipeline.
 * input: file_path - full path string.
 * output: true for .ogg/.pcm paths; otherwise false.
 */
static bool _ht517_path_is_supported(const char *file_path)
{
    if (file_path == NULL)
    {
        return false;
    }

    return usr_fs_path_has_suffix(file_path, ".ogg") ||
           usr_fs_path_has_suffix(file_path, ".pcm");
}

/*
 * brief: Free one playback queue node and its owned path buffer.
 * input: item - queue node pointer.
 * output: None.
 */
static void _ht517_free_play_item(ht517_play_item_t *item)
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
static ht517_play_item_t *_ht517_pop_play_item(void)
{
    ht517_play_item_t *item;

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
 * brief: Apply configured gain scaling to signed 16-bit PCM samples in place.
 * input: pcm16 - PCM sample buffer; sample_count - number of int16 samples.
 * output: None.
 */
static void _ht517_apply_gain_pcm16(int16_t *pcm16, size_t sample_count)
{
    uint8_t gain_percent;
    size_t i;

    if ((pcm16 == NULL) || (sample_count == 0U))
    {
        return;
    }

    gain_percent = s_gain_percent;
    if (gain_percent == HT517_DEFAULT_GAIN_PERCENT)
    {
        return;
    }

    for (i = 0U; i < sample_count; i++)
    {
        int32_t scaled;

        scaled = ((int32_t)pcm16[i] * (int32_t)gain_percent) / 100;
        if (scaled > INT16_MAX)
        {
            scaled = INT16_MAX;
        }
        else if (scaled < INT16_MIN)
        {
            scaled = INT16_MIN;
        }

        pcm16[i] = (int16_t)scaled;
    }
}

/*
 * brief: Write PCM bytes to I2S channel until all data is consumed.
 * input: data - PCM byte buffer; size - byte count to send.
 * output: ESP_OK on success; otherwise argument, timeout, or I2S write error.
 */
static esp_err_t _ht517_write_pcm_bytes(const uint8_t *data, size_t size)
{
    int16_t gain_chunk_buf[256];
    size_t gain_chunk_bytes;
    size_t total_written;

    if ((data == NULL) || (size == 0U))
    {
        return ESP_ERR_INVALID_ARG;
    }

    if ((size % sizeof(int16_t)) != 0U)
    {
        return ESP_ERR_INVALID_SIZE;
    }

    gain_chunk_bytes = sizeof(gain_chunk_buf);

    total_written = 0U;
    while (total_written < size)
    {
        size_t bytes_written;
        size_t src_chunk_bytes;
        size_t chunk_written;
        uint8_t *tx_ptr;
        esp_err_t ret;

        src_chunk_bytes = size - total_written;
        if (src_chunk_bytes > gain_chunk_bytes)
        {
            src_chunk_bytes = gain_chunk_bytes;
        }

        memcpy(gain_chunk_buf, data + total_written, src_chunk_bytes);
        _ht517_apply_gain_pcm16(gain_chunk_buf, src_chunk_bytes / sizeof(int16_t));

        tx_ptr = (uint8_t *)gain_chunk_buf;
        chunk_written = 0U;

        while (chunk_written < src_chunk_bytes)
        {
            bytes_written = 0U;
            ret = i2s_channel_write(s_tx_chan,
                                    tx_ptr + chunk_written,
                                    src_chunk_bytes - chunk_written,
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

            chunk_written += bytes_written;
        }

        total_written += src_chunk_bytes;
    }

    return ESP_OK;
}

/*
 * brief: Play interleaved stereo PCM buffer after frame-alignment validation.
 * input: pcm_data - PCM byte buffer; pcm_bytes - byte length.
 * output: ESP_OK on success; otherwise argument, size, or I2S write error.
 */
static esp_err_t _ht517_play_pcm_buffer(const uint8_t *pcm_data, size_t pcm_bytes)
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

    return _ht517_write_pcm_bytes(pcm_data, aligned_bytes);
}

/*
 * brief: Expand mono PCM samples to stereo and play through I2S.
 * input: mono_data - mono PCM sample buffer; mono_samples - sample count.
 * output: ESP_OK on success; otherwise argument, memory, or playback error.
 */
static esp_err_t _ht517_play_mono_pcm_buffer(const int16_t *mono_data, size_t mono_samples)
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
    stereo_data = (int16_t *)_ht517_audio_malloc(stereo_bytes);
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

    ret = _ht517_play_pcm_buffer((const uint8_t *)stereo_data, stereo_bytes);
    free(stereo_data);
    return ret;
}

/*
 * brief: Parse OpusHead packet sample rate and fallback to configured rate when invalid.
 * input: packet - Opus packet bytes; packet_len - packet size.
 * output: Parsed sample rate in Hz.
 */
static int _ht517_parse_opus_sample_rate(const uint8_t *packet, size_t packet_len)
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

/*
 * brief: Open Opus decoder instance for playback pipeline.
 * input: out_decoder - output decoder handle pointer; hinted_sample_rate - stream rate hint.
 * output: ESP_OK on success; otherwise ESP_ERR_INVALID_ARG or ESP_FAIL.
 */
static esp_err_t _ht517_open_opus_decoder(void **out_decoder, int hinted_sample_rate)
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

/*
 * brief: Decode one Opus packet into mono PCM and play it as stereo.
 * input: decoder - opened Opus decoder; packet - Opus packet bytes; packet_len - packet size.
 * output: ESP_OK on success; otherwise argument, memory, decode, or playback error.
 */
static esp_err_t _ht517_decode_opus_packet_and_play(void *decoder,
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
    mono_buf = (int16_t *)_ht517_audio_malloc(mono_buf_bytes);
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
        ret = _ht517_play_mono_pcm_buffer(mono_buf, mono_samples);
    }

    free(mono_buf);
    return ret;
}

/*
 * brief: Process one assembled OGG packet, handling Opus headers and audio frames.
 * input: ctx - parser context; decoder - decoder handle pointer; packet - packet bytes;
 *        packet_len - packet size.
 * output: ESP_OK on success; otherwise argument or decode/playback error.
 */
static esp_err_t _ht517_process_ogg_packet(ht517_ogg_parser_ctx_t *ctx,
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
        ctx->sample_rate = _ht517_parse_opus_sample_rate(packet, packet_len);
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
        ret = _ht517_open_opus_decoder(decoder, ctx->sample_rate);
        if (ret != ESP_OK)
        {
            return ret;
        }
    }

    ret = _ht517_decode_opus_packet_and_play(*decoder, packet, packet_len);
    if (ret == ESP_OK)
    {
        ctx->audio_packet_count++;
    }

    return ret;
}

/*
 * brief: Demux OGG container into Opus packets, then decode and stream to I2S.
 * input: ogg_data - OGG container bytes; ogg_bytes - container size.
 * output: ESP_OK on success; otherwise argument, memory, format, decode, or playback error.
 */
static esp_err_t _ht517_play_ogg_buffer(const uint8_t *ogg_data, size_t ogg_bytes)
{
    ht517_ogg_parser_ctx_t ctx;
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
    ctx.packet_buf = (uint8_t *)_ht517_audio_malloc(ctx.packet_capacity);
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
                ret = _ht517_process_ogg_packet(&ctx, &decoder, ctx.packet_buf, ctx.packet_len);
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

/*
 * brief: Load one audio file by full path and play based on file suffix.
 * input: file_path - full path ending with .ogg or .pcm.
 * output: ESP_OK on success; otherwise argument, filesystem, decode, or playback error.
 */
static esp_err_t _ht517_play_file_path(const char *file_path)
{
    uint8_t *audio_data;
    size_t audio_size;
    esp_err_t ret;

    if (!_ht517_path_is_supported(file_path))
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
        ret = _ht517_play_ogg_buffer(audio_data, audio_size);
    }
    else
    {
        ret = _ht517_play_pcm_buffer(audio_data, audio_size);
    }

    free(audio_data);

    return ret;
}

/*
 * brief: Playback worker task that consumes FIFO list and plays queued files in order.
 * input: arg - unused task argument.
 * output: None.
 */
static void _ht517_playback_task(void *arg)
{
    (void)arg;

    for (;;)
    {
        ht517_play_item_t *item;
        esp_err_t ret;

        if (s_playing)
        {
            delay_ms(HT517_PLAY_TASK_IDLE_MS);
            continue;
        }

        item = _ht517_pop_play_item();
        if (item == NULL)
        {
            delay_ms(HT517_PLAY_TASK_IDLE_MS);
            continue;
        }

        s_playing = true;
        ret = _ht517_play_file_path(item->file_path);
        s_playing = false;

        if (ret != ESP_OK)
        {
            ESP_LOGW(TAG,
                     "play queue path failed, path=%s err=%d",
                     item->file_path,
                     (int)ret);
        }

        _ht517_free_play_item(item);
    }
}

/*
 * brief: Initialize HT517 playback backend and create playback task.
 * input: None.
 * output: ESP_OK on success; otherwise propagated initialization error.
 */
esp_err_t ht517_init(void)
{
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM, I2S_ROLE_MASTER);
    i2s_chan_info_t chan_info = {0};
    BaseType_t task_ret;
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

    task_ret = xTaskCreate(_ht517_playback_task,
                           "ht517_play",
                           HT517_PLAY_TASK_STACK_BYTES,
                           NULL,
                           HT517_PLAY_TASK_PRIORITY,
                           &s_play_task);
    if (task_ret != pdPASS)
    {
        (void)i2s_del_channel(s_tx_chan);
        s_tx_chan = NULL;
        s_play_task = NULL;
        return ESP_ERR_NO_MEM;
    }

    s_play_head = NULL;
    s_play_tail = NULL;
    s_play_queue_len = 0U;
    s_playing = false;
    s_gain_percent = HT517_DEFAULT_GAIN_PERCENT;

    s_ready = true;

    ESP_LOGI(TAG, "HT517 I2S ready, fs=%u, bclk=%d ws=%d dout=%d, dma_total=%u",
             HT517_SAMPLE_RATE_HZ,
             (int)I2S_BCK_IO,
             (int)I2S_WS_IO,
             (int)I2S_DO_IO,
             (unsigned)dma_total);

    return ESP_OK;
}

/*
 * brief: Append one full path into FIFO playback list.
 * input: path - full path ending with .ogg or .pcm.
 * output: ESP_OK on success; otherwise state/argument/no-mem/not-found error.
 */
esp_err_t ht517_load(const char *path)
{
    ht517_play_item_t *item;
    size_t path_len;

    if (!s_ready || (s_tx_chan == NULL) || !usr_fs_is_ready() || (s_play_task == NULL))
    {
        return ESP_ERR_INVALID_STATE;
    }

    if (!_ht517_path_is_supported(path))
    {
        return ESP_ERR_INVALID_ARG;
    }

    if (!usr_fs_path_exists(path))
    {
        return ESP_ERR_NOT_FOUND;
    }

    item = (ht517_play_item_t *)malloc(sizeof(ht517_play_item_t));
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
 * brief: Read current HT517 runtime status.
 * input: None.
 * output: Status snapshot containing ready/playing/queue/gain.
 */
ht517_info_s ht517_read_info(void)
{
    ht517_info_s info;

    info.ready = false;
    info.playing = false;
    info.queue_len = 0U;
    info.gain_percent = HT517_DEFAULT_GAIN_PERCENT;

    portENTER_CRITICAL(&s_play_list_lock);
    info.ready = s_ready;
    info.playing = s_playing;
    info.queue_len = s_play_queue_len;
    info.gain_percent = s_gain_percent;
    portEXIT_CRITICAL(&s_play_list_lock);

    return info;
}

/*
 * brief: Configure software playback gain percentage.
 * input: gain_percent - linear gain in percent (0~200).
 * output: ESP_OK on success; otherwise ESP_ERR_INVALID_ARG.
 */
esp_err_t ht517_config(uint8_t gain_percent)
{
    if (gain_percent > HT517_GAIN_PERCENT_MAX)
    {
        return ESP_ERR_INVALID_ARG;
    }

    portENTER_CRITICAL(&s_play_list_lock);
    s_gain_percent = gain_percent;
    portEXIT_CRITICAL(&s_play_list_lock);

    return ESP_OK;
}
