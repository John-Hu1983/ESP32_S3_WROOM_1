#include "voice_wav.h"

#define TAG "VOICE_WAV"

#define VOICE_WAV_INPUT_CHUNK_BYTES 1024U
#define VOICE_WAV_OUTPUT_INIT_BYTES 4096U

static bool s_simple_decoder_registered = false;

/*
 * brief: Allocate audio buffer from PSRAM first, then fallback to internal RAM.
 * input: bytes - requested allocation size in bytes.
 * output: Allocated buffer pointer on success; otherwise NULL.
 */
static void *_voice_wav_audio_malloc(size_t bytes)
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
 * brief: Reallocate audio buffer with PSRAM priority and internal RAM fallback.
 * input: ptr - existing allocation pointer; bytes - new allocation size in bytes.
 * output: Reallocated buffer pointer on success; otherwise NULL.
 */
static void *_voice_wav_audio_realloc(void *ptr, size_t bytes)
{
    void *new_ptr;

    if (bytes == 0U)
    {
        free(ptr);
        return NULL;
    }

    new_ptr = heap_caps_realloc(ptr, bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (new_ptr != NULL)
    {
        return new_ptr;
    }

    return heap_caps_realloc(ptr, bytes, MALLOC_CAP_8BIT);
}

/*
 * brief: Ensure default simple decoders are registered once.
 * input: None.
 * output: ESP_OK on success; otherwise ESP_FAIL.
 */
static esp_err_t _voice_wav_ensure_decoder_registered(void)
{
    esp_audio_err_t dec_ret;

    if (s_simple_decoder_registered)
    {
        return ESP_OK;
    }

    dec_ret = esp_audio_simple_dec_register_default();
    if (dec_ret != ESP_AUDIO_ERR_OK)
    {
        ESP_LOGE(TAG, "esp_audio_simple_dec_register_default failed: %d", (int)dec_ret);
        return ESP_FAIL;
    }

    s_simple_decoder_registered = true;
    return ESP_OK;
}

/*
 * brief: Play one decoded PCM frame according to channel count.
 * input: pcm_data - decoded PCM bytes; pcm_bytes - byte size; channels - channel count.
 * output: ESP_OK on success; otherwise invalid argument or playback error.
 */
static esp_err_t _voice_wav_play_decoded_frame(const uint8_t *pcm_data,
                                               size_t pcm_bytes,
                                               uint8_t channels)
{
    if ((pcm_data == NULL) || (pcm_bytes == 0U))
    {
        return ESP_ERR_INVALID_ARG;
    }

    if ((pcm_bytes % sizeof(int16_t)) != 0U)
    {
        return ESP_ERR_INVALID_SIZE;
    }

    if (channels == 1U)
    {
        const int16_t *mono_data;
        size_t mono_samples;

        mono_data = (const int16_t *)pcm_data;
        mono_samples = pcm_bytes / sizeof(int16_t);
        return voice_pcm_play_mono_samples(mono_data, mono_samples);
    }

    if (channels == 2U)
    {
        return voice_pcm_play_buffer(pcm_data, pcm_bytes);
    }

    ESP_LOGE(TAG, "unsupported wav channels: %u", (unsigned)channels);
    return ESP_ERR_NOT_SUPPORTED;
}

/*
 * brief: Decode WAV bytes with simple decoder and play through PCM backend.
 * input: wav_data - WAV container bytes; wav_bytes - byte length.
 * output: ESP_OK on success; otherwise argument, decode, format, or playback error.
 */
esp_err_t voice_wav_play_buffer(const uint8_t *wav_data, size_t wav_bytes)
{
    uint8_t *in_chunk;
    uint8_t *out_buf;
    size_t out_buf_size;
    size_t offset;
    size_t total_decoded;
    bool info_ready;
    esp_audio_simple_dec_handle_t decoder;
    esp_audio_simple_dec_info_t dec_info;
    esp_audio_simple_dec_cfg_t dec_cfg;
    esp_audio_simple_dec_raw_t raw;
    esp_err_t ret;

    if ((wav_data == NULL) || (wav_bytes == 0U))
    {
        return ESP_ERR_INVALID_ARG;
    }

    ret = _voice_wav_ensure_decoder_registered();
    if (ret != ESP_OK)
    {
        return ret;
    }

    in_chunk = (uint8_t *)_voice_wav_audio_malloc(VOICE_WAV_INPUT_CHUNK_BYTES);
    out_buf_size = VOICE_WAV_OUTPUT_INIT_BYTES;
    out_buf = (uint8_t *)_voice_wav_audio_malloc(out_buf_size);
    if ((in_chunk == NULL) || (out_buf == NULL))
    {
        free(in_chunk);
        free(out_buf);
        return ESP_ERR_NO_MEM;
    }

    memset(&dec_cfg, 0, sizeof(dec_cfg));
    dec_cfg.dec_type = ESP_AUDIO_SIMPLE_DEC_TYPE_WAV;
    dec_cfg.use_frame_dec = false;

    decoder = NULL;
    if (esp_audio_simple_dec_open(&dec_cfg, &decoder) != ESP_AUDIO_ERR_OK)
    {
        ESP_LOGE(TAG, "esp_audio_simple_dec_open WAV failed");
        free(in_chunk);
        free(out_buf);
        return ESP_FAIL;
    }

    memset(&dec_info, 0, sizeof(dec_info));
    memset(&raw, 0, sizeof(raw));
    offset = 0U;
    total_decoded = 0U;
    info_ready = false;
    ret = ESP_OK;

    while (offset < wav_bytes)
    {
        size_t chunk_bytes;

        chunk_bytes = wav_bytes - offset;
        if (chunk_bytes > VOICE_WAV_INPUT_CHUNK_BYTES)
        {
            chunk_bytes = VOICE_WAV_INPUT_CHUNK_BYTES;
        }

        memcpy(in_chunk, wav_data + offset, chunk_bytes);
        offset += chunk_bytes;

        raw.buffer = in_chunk;
        raw.len = (uint32_t)chunk_bytes;
        raw.eos = (offset >= wav_bytes);
        raw.consumed = 0U;
        raw.frame_recover = ESP_AUDIO_SIMPLE_DEC_RECOVERY_NONE;

        while (raw.len > 0U)
        {
            esp_audio_simple_dec_out_t out_frame;
            esp_audio_err_t dec_ret;

            memset(&out_frame, 0, sizeof(out_frame));
            out_frame.buffer = out_buf;
            out_frame.len = (uint32_t)out_buf_size;

            dec_ret = esp_audio_simple_dec_process(decoder, &raw, &out_frame);
            if (dec_ret == ESP_AUDIO_ERR_BUFF_NOT_ENOUGH)
            {
                uint8_t *new_buf;
                size_t needed_size;

                needed_size = out_frame.needed_size;
                if (needed_size <= out_buf_size)
                {
                    needed_size = out_buf_size * 2U;
                }

                new_buf = (uint8_t *)_voice_wav_audio_realloc(out_buf, needed_size);
                if (new_buf == NULL)
                {
                    ret = ESP_ERR_NO_MEM;
                    break;
                }

                out_buf = new_buf;
                out_buf_size = needed_size;
                continue;
            }

            if (dec_ret != ESP_AUDIO_ERR_OK)
            {
                ESP_LOGE(TAG, "esp_audio_simple_dec_process WAV failed: %d", (int)dec_ret);
                ret = ESP_FAIL;
                break;
            }

            if ((raw.consumed == 0U) && (out_frame.decoded_size == 0U))
            {
                ESP_LOGE(TAG, "wav decoder made no progress");
                ret = ESP_FAIL;
                break;
            }

            if (raw.consumed > raw.len)
            {
                ESP_LOGE(TAG, "wav decoder consumed overflow: %u > %u",
                         (unsigned)raw.consumed,
                         (unsigned)raw.len);
                ret = ESP_FAIL;
                break;
            }

            raw.len -= raw.consumed;
            raw.buffer += raw.consumed;

            if (out_frame.decoded_size == 0U)
            {
                continue;
            }

            if (!info_ready)
            {
                esp_audio_err_t info_ret;

                info_ret = esp_audio_simple_dec_get_info(decoder, &dec_info);
                if (info_ret != ESP_AUDIO_ERR_OK)
                {
                    ESP_LOGE(TAG, "esp_audio_simple_dec_get_info WAV failed: %d", (int)info_ret);
                    ret = ESP_FAIL;
                    break;
                }

                if (dec_info.sample_rate != HT517_SAMPLE_RATE_HZ)
                {
                    ESP_LOGW(TAG,
                             "WAV sample_rate=%u, auto adapt I2S sample rate",
                             (unsigned)dec_info.sample_rate);
                }

                if (dec_info.sample_rate == 0U)
                {
                    ESP_LOGE(TAG, "invalid wav sample_rate=0");
                    ret = ESP_ERR_INVALID_RESPONSE;
                    break;
                }

                ret = ht517_set_sample_rate(dec_info.sample_rate);
                if (ret != ESP_OK)
                {
                    ESP_LOGE(TAG,
                             "ht517_set_sample_rate failed, fs=%u err=%d",
                             (unsigned)dec_info.sample_rate,
                             (int)ret);
                    break;
                }

                if (dec_info.bits_per_sample != 16U)
                {
                    ESP_LOGE(TAG,
                             "unsupported wav bits_per_sample=%u",
                             (unsigned)dec_info.bits_per_sample);
                    ret = ESP_ERR_NOT_SUPPORTED;
                    break;
                }

                if ((dec_info.channel != 1U) && (dec_info.channel != 2U))
                {
                    ESP_LOGE(TAG,
                             "unsupported wav channels=%u",
                             (unsigned)dec_info.channel);
                    ret = ESP_ERR_NOT_SUPPORTED;
                    break;
                }

                info_ready = true;
            }

            ret = _voice_wav_play_decoded_frame(out_frame.buffer,
                                                out_frame.decoded_size,
                                                dec_info.channel);
            if (ret != ESP_OK)
            {
                break;
            }

            total_decoded += out_frame.decoded_size;
        }

        if (ret != ESP_OK)
        {
            break;
        }
    }

    if ((ret == ESP_OK) && (total_decoded == 0U))
    {
        ret = ESP_ERR_NOT_FOUND;
    }

    esp_audio_simple_dec_close(decoder);
    free(out_buf);
    free(in_chunk);

    return ret;
}
