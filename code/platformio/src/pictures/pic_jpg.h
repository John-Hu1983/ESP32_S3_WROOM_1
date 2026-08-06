#pragma once

#include "pic_common.h"

bool pic_jpg_path_is_match(const char *path);
bool pic_jpg_decoder_enabled(void);
esp_err_t pic_jpg_decode_item(pic_item_t *item);
