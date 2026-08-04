#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <math.h>

#include "driver/i2s_std.h"
#include "esp_err.h"
#include "esp_log.h"

#include "user_config.h"

#define HT517_SAMPLE_RATE_HZ 16000U
#define HT517_TONE_FREQ_HZ 180U
#define HT517_TONE_DURATION_MS 140U
#define HT517_WRITE_TIMEOUT_MS 300U
#define HT517_DONG_FRAME_COUNT ((HT517_SAMPLE_RATE_HZ * HT517_TONE_DURATION_MS) / 1000U)

extern const uint8_t g_ht517_success_prompt_pcm[];
extern const size_t g_ht517_success_prompt_pcm_len;

/* Return true after I2S TX channel has been initialized and enabled. */
bool ht517_is_ready(void);
/* Configure I2S pins/format for HT517 stereo input mode and prepare test PCM. */
esp_err_t ht517_init_device(void);
/* Play one short "dong" tone on both channels for bring-up testing. */
esp_err_t ht517_play_dong(void);
/* Play embedded 16 kHz / 16-bit / stereo PCM prompt converted from success.ogg. */
esp_err_t ht517_play_success_prompt(void);
