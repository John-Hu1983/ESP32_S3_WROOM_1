#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_heap_caps.h"

#include "driver/i2s_std.h"
#include "esp_audio_dec.h"
#include "esp_audio_types.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_opus_dec.h"

#include "filesystem/usr_fs.h"
#include "user_config.h"

#define HT517_STEREO_CHANNELS 2U
#define HT517_PCM_FRAME_BYTES (sizeof(int16_t) * HT517_STEREO_CHANNELS)
#define HT517_COMMON_OGG_MAX_FILES 32U
#define HT517_COMMON_OGG_NAME_MAX_LEN 64U

#define HT517_SAMPLE_RATE_HZ USER_AUDIO_SAMPLE_RATE_HZ
#define HT517_WRITE_TIMEOUT_MS 300U
#define HT517_OPUS_FRAME_DURATION_MS 60U
#define HT517_OPUS_MAX_PACKET_BYTES 8192U
#define HT517_OPUS_MAX_MONO_SAMPLES 5760U

struct ht517_ogg_parser_ctx
{
    bool head_seen;
    bool tags_seen;
    int sample_rate;
    uint32_t audio_packet_count;
    size_t packet_len;
    size_t packet_capacity;
    uint8_t *packet_buf;
};

/* Return true after I2S TX channel has been initialized and enabled. */
bool ht517_is_ready(void);
/* Configure I2S pins/format for HT517 stereo playback mode. */
esp_err_t ht517_init_device(void);
/* Scan /storage/common and play next .ogg file in sequence. */
esp_err_t ht517_play_next_common_ogg(void);
/* Play a prompt from assets storage (.pcm or .ogg/Opus). */
esp_err_t ht517_play_prompt_from_storage(const char *locale, const char *prompt_name);
