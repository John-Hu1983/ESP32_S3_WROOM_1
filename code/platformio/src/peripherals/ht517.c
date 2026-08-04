#include "ht517.h"

#define TAG "HT517"
#define HT517_PI 3.14159265359f
#define HT517_STEREO_CHANNELS 2U
#define HT517_PCM_FRAME_BYTES (sizeof(int16_t) * HT517_STEREO_CHANNELS)

static i2s_chan_handle_t s_tx_chan = NULL;
static bool s_ready = false;
static int16_t s_dong_pcm[HT517_DONG_FRAME_COUNT * HT517_STEREO_CHANNELS];

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
            return ret;
        }

        if (bytes_written == 0U)
        {
            return ESP_ERR_TIMEOUT;
        }

        total_written += bytes_written;
    }

    return ESP_OK;
}

/* Build a short decaying low-frequency tone to audibly validate output path. */
static void ht517_build_dong_pcm(void)
{
    uint32_t i;
    uint32_t attack_samples;
    float phase;
    float phase_step;
    float sample_rate;
    float duration_sec;

    sample_rate = (float)HT517_SAMPLE_RATE_HZ;
    duration_sec = (float)HT517_TONE_DURATION_MS / 1000.0f;
    phase = 0.0f;
    phase_step = (2.0f * HT517_PI * (float)HT517_TONE_FREQ_HZ) / sample_rate;
    attack_samples = HT517_SAMPLE_RATE_HZ / 200U; /* ~5 ms */
    if (attack_samples == 0U)
    {
        attack_samples = 1U;
    }

    for (i = 0U; i < HT517_DONG_FRAME_COUNT; i++)
    {
        float t;
        float env;
        float tone;
        float sample_f;
        int16_t sample;
        size_t idx;

        t = (float)i / sample_rate;
        env = expf(-5.5f * t / duration_sec);
        if (i < attack_samples)
        {
            env *= (float)i / (float)attack_samples;
        }

        tone = (0.82f * sinf(phase)) + (0.18f * sinf(2.0f * phase));
        sample_f = tone * env * 20000.0f;

        if (sample_f > 32767.0f)
        {
            sample_f = 32767.0f;
        }
        else if (sample_f < -32768.0f)
        {
            sample_f = -32768.0f;
        }

        sample = (int16_t)sample_f;
        idx = (size_t)i * HT517_STEREO_CHANNELS;
        s_dong_pcm[idx] = sample;
        s_dong_pcm[idx + 1U] = sample;

        phase += phase_step;
    }
}

bool ht517_is_ready(void)
{
    return s_ready;
}

esp_err_t ht517_init_device(void)
{
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM, I2S_ROLE_MASTER);
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

    ht517_build_dong_pcm();
    s_ready = true;

    ESP_LOGI(TAG, "HT517 I2S ready, fs=%u, bclk=%d ws=%d dout=%d",
             HT517_SAMPLE_RATE_HZ,
             (int)I2S_BCK_IO,
             (int)I2S_WS_IO,
             (int)I2S_DO_IO);

    return ESP_OK;
}

esp_err_t ht517_play_dong(void)
{
    if (!s_ready || (s_tx_chan == NULL))
    {
        return ESP_ERR_INVALID_STATE;
    }

    return ht517_write_pcm_bytes((const uint8_t *)s_dong_pcm, sizeof(s_dong_pcm));
}

esp_err_t ht517_play_success_prompt(void)
{
    size_t pcm_bytes;
    size_t aligned_bytes;

    if (!s_ready || (s_tx_chan == NULL))
    {
        return ESP_ERR_INVALID_STATE;
    }

    pcm_bytes = g_ht517_success_prompt_pcm_len;
    if (pcm_bytes < HT517_PCM_FRAME_BYTES)
    {
        return ESP_ERR_INVALID_SIZE;
    }

    aligned_bytes = pcm_bytes - (pcm_bytes % HT517_PCM_FRAME_BYTES);
    if (aligned_bytes == 0U)
    {
        return ESP_ERR_INVALID_SIZE;
    }

    if (aligned_bytes != pcm_bytes)
    {
        ESP_LOGW(TAG, "success prompt size not frame-aligned, truncating %u bytes",
                 (unsigned)(pcm_bytes - aligned_bytes));
    }

    return ht517_write_pcm_bytes(g_ht517_success_prompt_pcm, aligned_bytes);
}

