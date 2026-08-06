#pragma once

#include "pic_common.h"

bool pic_png_path_is_match(const char *path);
bool pic_png_decoder_enabled(void);
esp_err_t pic_png_decode_item(pic_item_t *item);
