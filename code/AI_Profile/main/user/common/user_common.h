#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "driver/gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    Act_Stop = 0,
    Act_Start = 1,
    Act_Pause = 2,
} Action_e;

bool user_common_gpio_is_valid(gpio_num_t io_num);
uint16_t user_common_read_u16_be(const uint8_t* data);
uint16_t user_common_read_u16_le(const uint8_t* data);

#ifdef __cplusplus
}
#endif