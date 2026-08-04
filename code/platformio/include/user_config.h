#pragma once

#include "esp_err.h"
#include "esp_log.h"

/* Common helper for concise esp_err_t propagation in functions returning esp_err_t. */
#ifndef USER_RETURN_ON_ERROR
#define USER_RETURN_ON_ERROR(expr, tag, msg)                   \
	do                                                         \
	{                                                          \
		esp_err_t __user_ret = (expr);                         \
		if (__user_ret != ESP_OK)                              \
		{                                                      \
			ESP_LOGE((tag), "%s: %d", (msg), (int)__user_ret); \
			return __user_ret;                                 \
		}                                                      \
	} while (0)
#endif



#if defined(USER_CONFIG_BOARD_N8R2)
#include "user_config_n8r2.h"
#elif defined(USER_CONFIG_BOARD_N16R8)
#include "user_config_n16r8.h"
#else
#error "No board user config selected. Define USER_CONFIG_BOARD_N8R2 or USER_CONFIG_BOARD_N16R8 in platformio.ini build_flags."
#endif