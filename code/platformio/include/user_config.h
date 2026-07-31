#pragma once

#if defined(USER_CONFIG_BOARD_N8R2)
#include "user_config_n8r2.h"
#elif defined(USER_CONFIG_BOARD_N16R8)
#include "user_config_n16r8.h"
#else
#error "No board user config selected. Define USER_CONFIG_BOARD_N8R2 or USER_CONFIG_BOARD_N16R8 in platformio.ini build_flags."
#endif