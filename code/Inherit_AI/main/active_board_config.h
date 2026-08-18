#pragma once

// Prefer generated active board config, and fall back to include-path config.h.
#if defined(__has_include)
#if defined(CONFIG_BOARD_TYPE_ESP32_S3_WROOM_1_N16R8)
#include "boards/esp32-s3-wroom-1-n16r8/config.h"
#elif defined(CONFIG_BOARD_TYPE_ESP32_S3_WROOM_1_N8R2)
#include "boards/esp32-s3-wroom-1-n8r2/config.h"
#elif __has_include("active_board_config_gen.h")
#include "active_board_config_gen.h"
#elif __has_include("config.h")
#include "config.h"
#else
#error "No board config header found. Please configure/build the project for a board."
#endif
#else
#include "config.h"
#endif
