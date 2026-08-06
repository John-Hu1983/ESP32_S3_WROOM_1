#include "voice_ogg.h"

#define TAG "VOICE_OGG"

#define VOICE_OGG_OPUS_MAX_PACKET_BYTES 8192U
#define VOICE_OGG_OPUS_MAX_MONO_SAMPLES 5760U

/*
 * brief: Allocate audio buffer from PSRAM first, then fallback to internal RAM.
 * input: bytes - requested allocation size in bytes.
 * output: Allocated buffer pointer on success; otherwise NULL.
 */
static void *_voice_ogg_audio_malloc(size_t bytes)
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
 * brief: Parse OpusHead packet sample rate and fallback to configured rate when invalid.
 * input: packet - Opus packet bytes; packet_len - packet size.
 * output: Parsed sample rate in Hz.
 */
static int _voice_ogg_parse_opus_sample_rate(const uint8_t *packet, size_t packet_len)
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
static esp_err_t _voice_ogg_open_opus_decoder(void **out_decoder, int hinted_sample_rate)
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
 * brief: Decode one Opus packet into mono PCM and play it through PCM backend.
 * input: decoder - opened Opus decoder; packet - Opus packet bytes; packet_len - packet size.
 * output: ESP_OK on success; otherwise argument, memory, decode, or playback error.
 */
static esp_err_t _voice_ogg_decode_opus_packet_and_play(void *decoder,
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

    mono_buf_bytes = VOICE_OGG_OPUS_MAX_MONO_SAMPLES * sizeof(int16_t);
    mono_buf = (int16_t *)_voice_ogg_audio_malloc(mono_buf_bytes);
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
        ret = voice_pcm_play_mono_samples(mono_buf, mono_samples);
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
static esp_err_t _voice_ogg_process_packet(voice_ogg_parser_ctx_t *ctx,
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
        ctx->sample_rate = _voice_ogg_parse_opus_sample_rate(packet, packet_len);
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
        ret = _voice_ogg_open_opus_decoder(decoder, ctx->sample_rate);
        if (ret != ESP_OK)
        {
            return ret;
        }
    }

    ret = _voice_ogg_decode_opus_packet_and_play(*decoder, packet, packet_len);
    if (ret == ESP_OK)
    {
        ctx->audio_packet_count++;
    }

    return ret;
}

/*
 * brief: Demux OGG container into Opus packets, decode and play through PCM backend.
 * input: ogg_data - OGG container bytes; ogg_bytes - container size.
 * output: ESP_OK on success; otherwise argument, memory, format, decode, or playback error.
 */
esp_err_t voice_ogg_play_buffer(const uint8_t *ogg_data, size_t ogg_bytes)
{
    voice_ogg_parser_ctx_t ctx;
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

    ctx.packet_capacity = VOICE_OGG_OPUS_MAX_PACKET_BYTES;
    ctx.packet_buf = (uint8_t *)_voice_ogg_audio_malloc(ctx.packet_capacity);
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
                ret = _voice_ogg_process_packet(&ctx, &decoder, ctx.packet_buf, ctx.packet_len);
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
