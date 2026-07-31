#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "driver/gpio.h"

#define CAMERA_APP_EN true

/*
    power control
*/
#define POWER_LOCK_IO_PORT GPBA02B_PORT_B
#define POWER_LOCK_IO_PIN 3

/*
    LCD monitor
*/
#define LCD_SPI_HOST SPI2_HOST
#define LCD_IO_RS GPIO_NUM_14
#define LCD_IO_CS GPIO_NUM_21
#define LCD_IO_MISO GPIO_NUM_NC
#define LCD_IO_MOSI GPIO_NUM_12
#define LCD_IO_CLK GPIO_NUM_13
#define LCD_DEFAULT_CLOCK_HZ (40 * 1000 * 1000)
#define LCD_IO_RESET_PORT GPBA02B_PORT_A
#define LCD_IO_RESET_PIN 5
#define LCD_DEFAULT_WIDTH 320
#define LCD_DEFAULT_HEIGHT 480
#define LCD_DEFAULT_MADCTL 0x40

/*
    GPBA02B peripheral
*/
#define GPBA02B_SPI_HOST SPI3_HOST
#define GPBA02_IO_MISO GPIO_NUM_47
#define GPBA02_IO_MOSI GPIO_NUM_48
#define GPBA02_IO_CLK GPIO_NUM_45
#define GPBA02_IO_CS GPIO_NUM_46
#define GPBA02_DEFAULT_CLOCK_HZ (10 * 1000 * 1000)
