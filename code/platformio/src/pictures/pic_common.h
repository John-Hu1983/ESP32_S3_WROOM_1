#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <stdlib.h>
#include <string.h>

#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_log.h"

#include "filesystem/usr_fs.h"
#include "lvgl.h"

typedef enum
{
    PIC_FORMAT_UNKNOWN = 0,
    PIC_FORMAT_PNG,
    PIC_FORMAT_BMP,
    PIC_FORMAT_JPG,
    PIC_FORMAT_GIF,
} pic_format_e;

typedef struct
{
    char *path;
    pic_format_e format;
    size_t decoded_size;
    uint8_t *decoded_data;
    bool decoded_ready;
    lv_img_dsc_t img_dsc;
} pic_item_t;

/* Return true when one file path suffix matches any supported image format. */
bool pic_path_is_supported(const char *path);
/* Return short readable format name for logs/UI text. */
const char *pic_format_name(pic_format_e format);
/* Return true when corresponding LVGL decoder is enabled in sdkconfig. */
bool pic_format_decoder_enabled(pic_format_e format);

/* Scan SPIFFS for supported image files and allocate item/path pools. */
esp_err_t pic_collect_paths(pic_item_t **out_items,
                            size_t *out_count,
                            char **out_path_pool,
                            size_t *out_path_pool_size);
/* Decode one item on demand and update loaded index for LVGL cache invalidation. */
esp_err_t pic_decode_item(pic_item_t *items,
                          size_t item_count,
                          size_t idx,
                          size_t *loaded_idx);

/* Invalidate current loaded-source cache and reset loaded index. */
void pic_release_loaded(pic_item_t *items, size_t item_count, size_t *loaded_idx);
/* Free one decoded image payload and reset descriptor fields. */
void pic_free_decoded_item(pic_item_t *item);
/* Free full item/path pools and clear caller state holders. */
void pic_free_pool(pic_item_t **items,
                   size_t *item_count,
                   char **path_pool,
                   size_t *path_pool_size,
                   size_t *loaded_idx);
