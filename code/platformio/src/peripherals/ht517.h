#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <limits.h>

#include <string.h>

#include "driver/i2s_std.h"
#include "esp_err.h"
#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "user_config.h"

#define HT517_STEREO_CHANNELS 2U
#define HT517_PCM_FRAME_BYTES (sizeof(int16_t) * HT517_STEREO_CHANNELS)

#define HT517_SAMPLE_RATE_HZ USER_AUDIO_SAMPLE_RATE_HZ
#define HT517_WRITE_TIMEOUT_MS 300U
#define HT517_DEFAULT_GAIN_PERCENT 100U
#define HT517_GAIN_PERCENT_MAX 200U

typedef struct
{
    bool ready;
    uint8_t gain_percent;
    uint32_t sample_rate_hz;
} ht517_info_s;

/* Initialize HT517 I2S TX hardware backend. */
esp_err_t ht517_init(void);
/* Query whether HT517 hardware backend is initialized. */
bool ht517_is_ready(void);
/* Query current HT517 I2S sample rate in Hz. */
uint32_t ht517_get_sample_rate(void);
/* Reconfigure HT517 I2S sample rate in Hz. */
esp_err_t ht517_set_sample_rate(uint32_t sample_rate_hz);
/* Write 16-bit stereo PCM bytes to HT517 I2S output. */
esp_err_t ht517_write_pcm_stereo(const uint8_t *pcm_data, size_t pcm_bytes);
/* Read current HT517 runtime status (ready/gain). */
ht517_info_s ht517_read_info(void);
/* Configure playback gain in percentage (0~200). */
esp_err_t ht517_config(uint8_t gain_percent);
