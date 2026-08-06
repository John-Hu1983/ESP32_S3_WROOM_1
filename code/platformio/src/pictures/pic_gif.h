#pragma once

#include "pic_common.h"

bool pic_gif_path_is_match(const char *path);
bool pic_gif_decoder_enabled(void);
esp_err_t pic_gif_decode_item(pic_item_t *item);
