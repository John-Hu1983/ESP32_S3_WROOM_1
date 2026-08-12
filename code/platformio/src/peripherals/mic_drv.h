#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "driver/i2s_pdm.h"
#include "esp_err.h"
#include "esp_log.h"

#include "freertos/FreeRTOS.h"

#include "user_config.h"

#define MIC_DRV_DEFAULT_SAMPLE_RATE_HZ USER_AUDIO_SAMPLE_RATE_HZ
#define MIC_DRV_DEFAULT_READ_TIMEOUT_MS 20U

/* Initialize PDM RX channel and enable microphone capture path. */
esp_err_t mic_drv_open(uint32_t sample_rate_hz);
/* Stop and release PDM RX channel resources. */
void mic_drv_close(void);
/* Query whether MIC driver is ready for capture. */
bool mic_drv_is_ready(void);
/* Get active microphone sample rate in Hz. */
uint32_t mic_drv_get_sample_rate_hz(void);
/* Read signed PCM16 mono samples from MIC PDM channel. */
esp_err_t mic_drv_read_pcm16(int16_t *sample_buf,
                             size_t sample_capacity,
                             size_t *out_samples,
                             uint32_t timeout_ms);
