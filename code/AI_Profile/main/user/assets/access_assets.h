#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// clang-format off
 
// clang-format on

typedef struct {
    const char* bin_name;
    const uint8_t* start;
    const uint8_t* end;
} access_assets_bin_item_t;

esp_err_t access_assets_get_bin(
    const char* bin_name,
    const uint8_t** out_data,
    size_t* out_data_len
);

#ifdef __cplusplus
}
#endif
