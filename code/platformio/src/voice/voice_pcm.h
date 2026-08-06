#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <stdlib.h>
#include <string.h>

#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_log.h"

#include "peripherals/ht517.h"

/* Play interleaved stereo PCM bytes through HT517 output backend. */
esp_err_t voice_pcm_play_buffer(const uint8_t *pcm_data, size_t pcm_bytes);
/* Expand mono PCM16 samples to stereo and play through HT517 output backend. */
esp_err_t voice_pcm_play_mono_samples(const int16_t *mono_data, size_t mono_samples);
