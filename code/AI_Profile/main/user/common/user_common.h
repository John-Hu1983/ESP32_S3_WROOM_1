#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "driver/gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

bool user_common_gpio_is_valid(gpio_num_t io_num);
uint16_t user_common_read_u16_be(const uint8_t* data);
uint16_t user_common_read_u16_le(const uint8_t* data);

#ifdef __cplusplus
}
#endif