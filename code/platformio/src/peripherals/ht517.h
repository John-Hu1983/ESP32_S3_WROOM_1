#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <limits.h>

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

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "filesystem/usr_fs.h"
#include "user_config.h"

#define HT517_STEREO_CHANNELS 2U
#define HT517_PCM_FRAME_BYTES (sizeof(int16_t) * HT517_STEREO_CHANNELS)

#define HT517_SAMPLE_RATE_HZ USER_AUDIO_SAMPLE_RATE_HZ
#define HT517_WRITE_TIMEOUT_MS 300U
#define HT517_OPUS_FRAME_DURATION_MS 60U
#define HT517_OPUS_MAX_PACKET_BYTES 8192U
#define HT517_OPUS_MAX_MONO_SAMPLES 5760U

#define HT517_PLAY_TASK_STACK_BYTES 12288U
#define HT517_PLAY_TASK_PRIORITY 5U
#define HT517_PLAY_TASK_IDLE_MS 20U
#define HT517_DEFAULT_GAIN_PERCENT 100U
#define HT517_GAIN_PERCENT_MAX 200U

typedef struct ht517_play_item_s
{
    char *file_path;
    struct ht517_play_item_s *next;
} ht517_play_item_t;

typedef struct
{
    bool head_seen;
    bool tags_seen;
    int sample_rate;
    uint32_t audio_packet_count;
    size_t packet_len;
    size_t packet_capacity;
    uint8_t *packet_buf;
} ht517_ogg_parser_ctx_t;

typedef struct
{
    bool ready;
    bool playing;
    uint32_t queue_len;
    uint8_t gain_percent;
} ht517_info_s;

/* Initialize HT517 playback backend and create playback task. */
esp_err_t ht517_init(void);
/* Append one audio path (.ogg/.pcm) into FIFO playback list. */
esp_err_t ht517_load(const char *path);
/* Read current HT517 runtime status (ready/playing/queue/gain). */
ht517_info_s ht517_read_info(void);
/* Configure playback gain in percentage (0~200). */
esp_err_t ht517_config(uint8_t gain_percent);
