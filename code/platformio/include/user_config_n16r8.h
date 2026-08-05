#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "driver/gpio.h"

#define CAMERA_APP_EN (true)

#define USER_AUDIO_SAMPLE_RATE_HZ (16000U)

#define USER_ASSETS_PARTITION_LABEL ("storage")
#define USER_ASSETS_MOUNT_POINT ("/storage")
#define USER_ASSETS_DEFAULT_LOCALE ("en-US")
#define USER_ASSETS_MAX_OPEN_FILES (8)
#define USER_FS_PRINT_ALL_FILES_ON_INIT (false)
#define USER_DESKTOP_BATCH_ENQUEUE_TEST_ON_START (true)
#define USER_DESKTOP_BATCH_ENQUEUE_TEST_DIR ("/storage/common")

/*
    power control
*/
#define POWER_LOCK_IO_PORT (GPBA02B_PORT_B)
#define POWER_LOCK_IO_PIN (3)

/*
    button
*/
#define BUTTON_UP_IO_PORT (GPBA02B_PORT_A)
#define BUTTON_UP_IO_PIN (0)
#define BUTTON_DOWN_IO_PORT (GPBA02B_PORT_A)
#define BUTTON_DOWN_IO_PIN (1)

/*
    LCD monitor
*/
#define LCD_SPI_HOST (SPI2_HOST)
#define LCD_IO_RS (GPIO_NUM_14)
#define LCD_IO_CS (GPIO_NUM_21)
#define LCD_IO_MISO (GPIO_NUM_NC)
#define LCD_IO_MOSI (GPIO_NUM_12)
#define LCD_IO_CLK (GPIO_NUM_13)
#define LCD_DEFAULT_CLOCK_HZ (40 * 1000 * 1000)
#define LCD_IO_RESET_PORT (GPBA02B_PORT_A)
#define LCD_IO_RESET_PIN (5)
#define LCD_DEFAULT_WIDTH (320)
#define LCD_DEFAULT_HEIGHT (480)
#define LCD_DEFAULT_MADCTL (0x40)

/*
    GPBA02B peripheral
*/
#define GPBA02B_SPI_HOST (SPI3_HOST)
#define GPBA02_IO_MISO (GPIO_NUM_47)
#define GPBA02_IO_MOSI (GPIO_NUM_48)
#define GPBA02_IO_CLK (GPIO_NUM_45)
#define GPBA02_IO_CS (GPIO_NUM_46)
#define GPBA02_DEFAULT_CLOCK_HZ (8 * 1000 * 1000)

/*
PDM
*/
#define PDM_CLK_IO (GPIO_NUM_41)
#define PDM_DATA_IO (GPIO_NUM_42)
#define PDM_EN_PORT (GPBA02B_PORT_B)
#define PDM_EN_PIN (0)

/*
I2S
*/
#define I2S_NUM (I2S_NUM_0)
#define I2S_BCK_IO (GPIO_NUM_40)
#define I2S_WS_IO (GPIO_NUM_39)
#define I2S_DO_IO (GPIO_NUM_38)
#define I2S_DI_IO (GPIO_NUM_NC)
#define I2S_EN_PORT (GPBA02B_PORT_B)
#define I2S_EN_PIN (1)
