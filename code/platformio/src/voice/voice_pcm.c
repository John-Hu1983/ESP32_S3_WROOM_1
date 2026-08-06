#include "voice_pcm.h"

#define TAG "VOICE_PCM"

/*
 * brief: Allocate audio buffer from PSRAM first, then fallback to internal RAM.
 * input: bytes - requested allocation size in bytes.
 * output: Allocated buffer pointer on success; otherwise NULL.
 */
static void *_voice_pcm_audio_malloc(size_t bytes)
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
 * brief: Play interleaved stereo PCM buffer through HT517 backend.
 * input: pcm_data - PCM byte buffer; pcm_bytes - byte length.
 * output: ESP_OK on success; otherwise argument/state/size/write error.
 */
esp_err_t voice_pcm_play_buffer(const uint8_t *pcm_data, size_t pcm_bytes)
{
    if (!ht517_is_ready())
    {
        return ESP_ERR_INVALID_STATE;
    }

    return ht517_write_pcm_stereo(pcm_data, pcm_bytes);
}

/*
 * brief: Expand mono PCM16 samples to stereo and play through HT517 backend.
 * input: mono_data - mono sample buffer; mono_samples - number of mono samples.
 * output: ESP_OK on success; otherwise argument/memory/state/write error.
 */
esp_err_t voice_pcm_play_mono_samples(const int16_t *mono_data, size_t mono_samples)
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
    stereo_data = (int16_t *)_voice_pcm_audio_malloc(stereo_bytes);
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

    ret = voice_pcm_play_buffer((const uint8_t *)stereo_data, stereo_bytes);
    free(stereo_data);

    if (ret != ESP_OK)
    {
        ESP_LOGW(TAG, "voice_pcm_play_buffer failed: %d", (int)ret);
    }

    return ret;
}
