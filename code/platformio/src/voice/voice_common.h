#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_err.h"
#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "bsp/delay.h"
#include "filesystem/usr_fs.h"
#include "peripherals/ht517.h"

#include "voice_ogg.h"
#include "voice_pcm.h"
#include "voice_wav.h"

#define VOICE_PLAY_TASK_STACK_BYTES 12288U
#define VOICE_PLAY_TASK_PRIORITY 5U
#define VOICE_PLAY_TASK_IDLE_MS 20U

typedef struct
{
    bool ready;
    bool playing;
    uint32_t queue_len;
} voice_info_s;

typedef struct voice_play_item_s
{
    char *file_path;
    struct voice_play_item_s *next;
} voice_play_item_t;

/* Initialize voice service task and queue. */
esp_err_t voice_init_player(void);
/* Append one audio path (.ogg/.pcm/.wav) into FIFO playback list. */
esp_err_t voice_load_file(const char *path);
/* Read current voice service runtime status (ready/playing/queue). */
voice_info_s voice_read_info(void);

/* Check whether full path suffix is supported by voice pipeline. */
bool voice_path_is_supported(const char *file_path);
/* Resolve prompt path in locales/<locale>, locales/default, then common, supporting wav. */
esp_err_t voice_resolve_prompt_path(const char *locale,
                                    const char *prompt_name,
                                    char *out_path,
                                    size_t out_path_size);
