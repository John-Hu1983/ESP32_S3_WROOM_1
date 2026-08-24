#pragma once

#include "../../../build/config/sdkconfig.h"

#if defined(CONFIG_BOARD_TYPE_ESP32_S3_WROOM_1_N16R8)
#include "../../boards/esp32-s3-wroom-1-n16r8/config.h"
#define USER_ACTIVE_BOARD_NAME "esp32-s3-wroom-1-n16r8"
#elif defined(CONFIG_BOARD_TYPE_ESP32_S3_WROOM_1_N8R2)
#include "../../boards/esp32-s3-wroom-1-n8r2/config.h"
#define USER_ACTIVE_BOARD_NAME "esp32-s3-wroom-1-n8r2"
#endif
