#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "driver/gpio.h"

#include "user_def.h"

#define CAMERA_APP_EN EN

#define SPI2_PIN_MISO GPIO_NUM_NC
#define SPI2_PIN_MOSI GPIO_NUM_35
#define SPI2_PIN_CLK GPIO_NUM_36
#define SPI2_DEFAULT_CLOCK_HZ (40 * 1000 * 1000)

#define GPBA02_IO_MISO GPIO_NUM_47
#define GPBA02_IO_MOSI GPIO_NUM_48
#define GPBA02_IO_CLK GPIO_NUM_45
#define GPBA02_IO_CS GPIO_NUM_46
#define GPBA02_DEFAULT_CLOCK_HZ (10 * 1000 * 1000)