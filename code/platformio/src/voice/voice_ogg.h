#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <stdlib.h>
#include <string.h>

#include "esp_audio_dec.h"
#include "esp_audio_types.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_opus_dec.h"

#include "voice_pcm.h"

typedef struct
{
	bool head_seen;
	bool tags_seen;
	int sample_rate;
	uint32_t audio_packet_count;
	size_t packet_len;
	size_t packet_capacity;
	uint8_t *packet_buf;
} voice_ogg_parser_ctx_t;

/* Decode OGG/Opus container bytes and play as PCM through HT517 backend. */
esp_err_t voice_ogg_play_buffer(const uint8_t *ogg_data, size_t ogg_bytes);
