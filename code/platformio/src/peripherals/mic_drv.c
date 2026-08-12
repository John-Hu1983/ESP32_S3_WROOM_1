#include "mic_drv.h"

#define TAG "MIC_DRV"
#define MIC_DRV_I2S_PORT (I2S_NUM_1)

static i2s_chan_handle_t s_rx_chan = NULL;
static uint32_t s_sample_rate_hz = MIC_DRV_DEFAULT_SAMPLE_RATE_HZ;
static bool s_ready = false;
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;

/*
 * brief: Close and destroy internal RX channel handle when it exists.
 * input: None.
 * output: None.
 */
static void _mic_drv_close_locked(void)
{
    if (s_rx_chan == NULL)
    {
        return;
    }

    (void)i2s_channel_disable(s_rx_chan);
    (void)i2s_del_channel(s_rx_chan);
    s_rx_chan = NULL;
}

/*
 * brief: Initialize PDM RX channel and enable capture.
 * input: sample_rate_hz - requested sample rate in Hz.
 * output: ESP_OK on success; otherwise I2S setup error.
 */
esp_err_t mic_drv_open(uint32_t sample_rate_hz)
{
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(MIC_DRV_I2S_PORT, I2S_ROLE_MASTER);
    i2s_pdm_rx_config_t pdm_cfg = {
        .clk_cfg = I2S_PDM_RX_CLK_DEFAULT_CONFIG(sample_rate_hz),
        .slot_cfg = I2S_PDM_RX_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,
                                                   I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .clk = PDM_CLK_IO,
            .din = PDM_DATA_IO,
            .invert_flags = {
                .clk_inv = false,
            },
        },
    };
    esp_err_t ret;

    if (sample_rate_hz == 0U)
    {
        return ESP_ERR_INVALID_ARG;
    }

    portENTER_CRITICAL(&s_lock);
    if (s_ready)
    {
        if (s_sample_rate_hz == sample_rate_hz)
        {
            portEXIT_CRITICAL(&s_lock);
            return ESP_OK;
        }

        _mic_drv_close_locked();
        s_ready = false;
    }
    portEXIT_CRITICAL(&s_lock);

    chan_cfg.auto_clear = true;

    ret = i2s_new_channel(&chan_cfg, NULL, &s_rx_chan);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "i2s_new_channel failed: %d", (int)ret);
        return ret;
    }

    ret = i2s_channel_init_pdm_rx_mode(s_rx_chan, &pdm_cfg);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "i2s_channel_init_pdm_rx_mode failed: %d", (int)ret);
        _mic_drv_close_locked();
        return ret;
    }

    ret = i2s_channel_enable(s_rx_chan);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "i2s_channel_enable failed: %d", (int)ret);
        _mic_drv_close_locked();
        return ret;
    }

    portENTER_CRITICAL(&s_lock);
    s_sample_rate_hz = sample_rate_hz;
    s_ready = true;
    portEXIT_CRITICAL(&s_lock);

    ESP_LOGI(TAG,
             "ready fs=%uHz clk=%d din=%d",
             (unsigned)sample_rate_hz,
             (int)PDM_CLK_IO,
             (int)PDM_DATA_IO);
    return ESP_OK;
}

/*
 * brief: Close microphone capture channel and release resources.
 * input: None.
 * output: None.
 */
void mic_drv_close(void)
{
    portENTER_CRITICAL(&s_lock);
    _mic_drv_close_locked();
    s_ready = false;
    portEXIT_CRITICAL(&s_lock);
}

/*
 * brief: Query whether microphone capture channel is ready.
 * input: None.
 * output: true when ready; otherwise false.
 */
bool mic_drv_is_ready(void)
{
    bool ready;

    portENTER_CRITICAL(&s_lock);
    ready = s_ready;
    portEXIT_CRITICAL(&s_lock);

    return ready;
}

/*
 * brief: Get active microphone sample rate.
 * input: None.
 * output: active sample rate in Hz.
 */
uint32_t mic_drv_get_sample_rate_hz(void)
{
    uint32_t sample_rate_hz;

    portENTER_CRITICAL(&s_lock);
    sample_rate_hz = s_sample_rate_hz;
    portEXIT_CRITICAL(&s_lock);

    return sample_rate_hz;
}

/*
 * brief: Read one chunk of PCM16 mono samples from microphone driver.
 * input: sample_buf - destination PCM sample buffer; sample_capacity - sample count capacity.
 *        out_samples - output valid sample count; timeout_ms - I2S read timeout.
 * output: ESP_OK on success; ESP_ERR_TIMEOUT on read timeout; other I2S/state errors.
 */
esp_err_t mic_drv_read_pcm16(int16_t *sample_buf,
                             size_t sample_capacity,
                             size_t *out_samples,
                             uint32_t timeout_ms)
{
    size_t bytes_read;
    size_t max_bytes;
    uint32_t timeout;
    esp_err_t ret;

    if ((sample_buf == NULL) || (sample_capacity == 0U) || (out_samples == NULL))
    {
        return ESP_ERR_INVALID_ARG;
    }

    if (!mic_drv_is_ready() || (s_rx_chan == NULL))
    {
        return ESP_ERR_INVALID_STATE;
    }

    timeout = (timeout_ms == 0U) ? MIC_DRV_DEFAULT_READ_TIMEOUT_MS : timeout_ms;
    max_bytes = sample_capacity * sizeof(int16_t);
    bytes_read = 0U;

    ret = i2s_channel_read(s_rx_chan,
                           sample_buf,
                           max_bytes,
                           &bytes_read,
                           timeout);
    if (ret != ESP_OK)
    {
        *out_samples = 0U;
        return ret;
    }

    *out_samples = bytes_read / sizeof(int16_t);
    return ESP_OK;
}
