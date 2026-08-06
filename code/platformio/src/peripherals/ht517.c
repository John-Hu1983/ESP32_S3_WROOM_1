#include "ht517.h"

#define TAG "HT517"
#define HT517_GAIN_CHUNK_SAMPLES 256U

static i2s_chan_handle_t s_tx_chan = NULL;
static bool s_ready = false;
static uint8_t s_gain_percent = HT517_DEFAULT_GAIN_PERCENT;
static uint32_t s_sample_rate_hz = HT517_SAMPLE_RATE_HZ;
static portMUX_TYPE s_state_lock = portMUX_INITIALIZER_UNLOCKED;

/*
 * brief: Apply configured gain scaling to signed 16-bit PCM samples in place.
 * input: pcm16 - PCM sample buffer; sample_count - number of int16 samples.
 * output: None.
 */
static void _ht517_apply_gain_pcm16(int16_t *pcm16, size_t sample_count, uint8_t gain_percent)
{
    size_t i;

    if ((pcm16 == NULL) || (sample_count == 0U))
    {
        return;
    }

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
    int16_t gain_chunk_buf[HT517_GAIN_CHUNK_SAMPLES];
    size_t gain_chunk_bytes;
    size_t total_written;
    uint8_t gain_percent;

    if ((data == NULL) || (size == 0U))
    {
        return ESP_ERR_INVALID_ARG;
    }

    if ((size % sizeof(int16_t)) != 0U)
    {
        return ESP_ERR_INVALID_SIZE;
    }

    portENTER_CRITICAL(&s_state_lock);
    gain_percent = s_gain_percent;
    portEXIT_CRITICAL(&s_state_lock);

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
        _ht517_apply_gain_pcm16(gain_chunk_buf,
                                src_chunk_bytes / sizeof(int16_t),
                                gain_percent);

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
 * brief: Initialize HT517 I2S TX hardware backend.
 * input: None.
 * output: ESP_OK on success; otherwise propagated initialization error.
 */
esp_err_t ht517_init(void)
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

    portENTER_CRITICAL(&s_state_lock);
    s_gain_percent = HT517_DEFAULT_GAIN_PERCENT;
    s_sample_rate_hz = HT517_SAMPLE_RATE_HZ;
    s_ready = true;
    portEXIT_CRITICAL(&s_state_lock);

    ESP_LOGI(TAG,
             "HT517 I2S ready, fs=%u, bclk=%d ws=%d dout=%d, dma_total=%u",
             HT517_SAMPLE_RATE_HZ,
             (int)I2S_BCK_IO,
             (int)I2S_WS_IO,
             (int)I2S_DO_IO,
             (unsigned)dma_total);

    return ESP_OK;
}

/*
 * brief: Query whether HT517 hardware backend is initialized.
 * input: None.
 * output: true when initialized; otherwise false.
 */
bool ht517_is_ready(void)
{
    bool ready;

    portENTER_CRITICAL(&s_state_lock);
    ready = s_ready;
    portEXIT_CRITICAL(&s_state_lock);

    return ready;
}

/*
 * brief: Query current HT517 I2S sample rate in Hz.
 * input: None.
 * output: Current runtime sample rate in Hz.
 */
uint32_t ht517_get_sample_rate(void)
{
    uint32_t sample_rate_hz;

    portENTER_CRITICAL(&s_state_lock);
    sample_rate_hz = s_sample_rate_hz;
    portEXIT_CRITICAL(&s_state_lock);

    return sample_rate_hz;
}

/*
 * brief: Reconfigure HT517 I2S sample rate in Hz.
 * input: sample_rate_hz - new sample rate in Hz.
 * output: ESP_OK on success; otherwise state/argument/I2S error.
 */
esp_err_t ht517_set_sample_rate(uint32_t sample_rate_hz)
{
    i2s_std_clk_config_t clk_cfg;
    uint32_t current_rate_hz;
    esp_err_t ret;

    if (sample_rate_hz == 0U)
    {
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_ready || (s_tx_chan == NULL))
    {
        return ESP_ERR_INVALID_STATE;
    }

    current_rate_hz = ht517_get_sample_rate();
    if (current_rate_hz == sample_rate_hz)
    {
        return ESP_OK;
    }

    ret = i2s_channel_disable(s_tx_chan);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "i2s_channel_disable failed: %d", (int)ret);
        return ret;
    }

    clk_cfg = (i2s_std_clk_config_t)I2S_STD_CLK_DEFAULT_CONFIG(sample_rate_hz);
    ret = i2s_channel_reconfig_std_clock(s_tx_chan, &clk_cfg);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "i2s_channel_reconfig_std_clock failed, fs=%u err=%d",
                 (unsigned)sample_rate_hz,
                 (int)ret);
        (void)i2s_channel_enable(s_tx_chan);
        return ret;
    }

    ret = i2s_channel_enable(s_tx_chan);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "i2s_channel_enable after reconfig failed: %d", (int)ret);
        return ret;
    }

    portENTER_CRITICAL(&s_state_lock);
    s_sample_rate_hz = sample_rate_hz;
    portEXIT_CRITICAL(&s_state_lock);

    ESP_LOGI(TAG, "HT517 I2S sample rate set to %uHz", (unsigned)sample_rate_hz);
    return ESP_OK;
}

/*
 * brief: Write 16-bit stereo PCM bytes to HT517 I2S output.
 * input: pcm_data - interleaved stereo PCM bytes; pcm_bytes - byte count.
 * output: ESP_OK on success; otherwise state/argument/size/write error.
 */
esp_err_t ht517_write_pcm_stereo(const uint8_t *pcm_data, size_t pcm_bytes)
{
    size_t aligned_bytes;

    if (!s_ready || (s_tx_chan == NULL))
    {
        return ESP_ERR_INVALID_STATE;
    }

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
 * brief: Read current HT517 runtime status.
 * input: None.
 * output: Status snapshot containing ready/gain/sample_rate.
 */
ht517_info_s ht517_read_info(void)
{
    ht517_info_s info;

    info.ready = false;
    info.gain_percent = HT517_DEFAULT_GAIN_PERCENT;
    info.sample_rate_hz = HT517_SAMPLE_RATE_HZ;

    portENTER_CRITICAL(&s_state_lock);
    info.ready = s_ready;
    info.gain_percent = s_gain_percent;
    info.sample_rate_hz = s_sample_rate_hz;
    portEXIT_CRITICAL(&s_state_lock);

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

    portENTER_CRITICAL(&s_state_lock);
    s_gain_percent = gain_percent;
    portEXIT_CRITICAL(&s_state_lock);

    return ESP_OK;
}
