#pragma once

#include "pic_common.h"

bool pic_bmp_path_is_match(const char *path);
bool pic_bmp_decoder_enabled(void);
esp_err_t pic_bmp_decode_item(pic_item_t *item);
