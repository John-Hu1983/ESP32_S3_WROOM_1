#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <stdlib.h>
#include <string.h>

#include "esp_audio_simple_dec.h"
#include "esp_audio_simple_dec_default.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_log.h"

#include "voice_pcm.h"

typedef struct
{
	bool enabled;
	uint32_t src_rate_hz;
	uint32_t dst_rate_hz;
	uint8_t channels;
	double next_src_pos;
	uint64_t consumed_frames;
	bool has_prev;
	int16_t prev_frame[2];
} voice_wav_resampler_s;

/* Decode WAV container bytes and play as PCM through HT517 backend. */
esp_err_t voice_wav_play_buffer(const uint8_t *wav_data, size_t wav_bytes);
