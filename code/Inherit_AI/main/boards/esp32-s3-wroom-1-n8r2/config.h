#pragma once

#include <driver/gpio.h>
#include "bsp/gpba02b.h"

#define USER_AUDIO_SAMPLE_RATE_HZ (16000U)

/*
    filesystem
*/
#define USER_ASSETS_PARTITION_LABEL ("storage")
#define USER_ASSETS_MOUNT_POINT ("/storage")
#define USER_ASSETS_DEFAULT_LOCALE ("en-US")
#define USER_ASSETS_MAX_OPEN_FILES (8)
#define USER_FS_PRINT_ALL_FILES_ON_INIT (true)
#define USER_DESKTOP_BATCH_ENQUEUE_TEST_ON_START (true)
#define USER_DESKTOP_BATCH_ENQUEUE_TEST_DIR ("/storage/common")

/*
    power control
*/
#define POWER_LOCK_IO_PORT (Gpba02b::kPortB)
#define POWER_LOCK_IO_PIN (3)

/*
    button
*/
#define BUTTON_UP_IO_PORT (Gpba02b::kPortA)
#define BUTTON_UP_IO_PIN (0)
#define BUTTON_DOWN_IO_PORT (Gpba02b::kPortA)
#define BUTTON_DOWN_IO_PIN (1)

/*
    LCD monitor
*/
#define LCD_SPI_HOST (SPI2_HOST)
#define LCD_IO_RS (GPIO_NUM_14)
#define LCD_IO_CS (GPIO_NUM_21)
#define LCD_IO_MISO (GPIO_NUM_NC)
#define LCD_IO_MOSI (GPIO_NUM_35)
#define LCD_IO_CLK (GPIO_NUM_36)
#define LCD_DEFAULT_CLOCK_HZ (40 * 1000 * 1000)
#define LCD_IO_RESET_PORT (Gpba02b::kPortA)
#define LCD_IO_RESET_PIN (5)
#define LCD_DEFAULT_WIDTH (320)
#define LCD_DEFAULT_HEIGHT (480)
#define LCD_DEFAULT_MADCTL (0x88)
#define LCD_DEFAULT_INVERT_COLOR (true)

/*
    GPBA02B peripheral
*/
#define GPBA02B_SPI_HOST (SPI3_HOST)
#define GPBA02B_IO_MISO (GPIO_NUM_47)
#define GPBA02B_IO_MOSI (GPIO_NUM_48)
#define GPBA02B_IO_CLK (GPIO_NUM_45)
#define GPBA02B_IO_CS (GPIO_NUM_46)
#define GPBA02B_DEFAULT_CLOCK_HZ (8 * 1000 * 1000)
#define GPBA02B_DEVICE_ID (0)
#define GPBA02B_HOST_IRQ_GPIO (GPIO_NUM_NC)

/*
PDM
*/
#define PDM_CLK_IO (GPIO_NUM_41)
#define PDM_DATA_IO (GPIO_NUM_42)
#define PDM_EN_PORT (Gpba02b::kPortB)
#define PDM_EN_PIN (0)

/*
I2S
*/
#define I2S_NUM (I2S_NUM_0)
#define I2S_BCK_IO (GPIO_NUM_40)
#define I2S_WS_IO (GPIO_NUM_39)
#define I2S_DO_IO (GPIO_NUM_38)
#define I2S_DI_IO (GPIO_NUM_NC)
#define I2S_EN_PORT (Gpba02b::kPortB)
#define I2S_EN_PIN (1)

/*
PIDM
*/
#define PIDM_IO_PULSE (GPIO_NUM_2)
#define PIDM_IO_ADC  (GPIO_NUM_1)
#define PIDM_ADC_CHANNEL (ADC_CHANNEL_0)
#define PIDM_EN_PORT (Gpba02b::kPortB)
#define PIDM_EN_PIN (2)

/*
RC522
*/
#define RC522_SPI_HOST (SPI3_HOST)
#define RC522_CS_IO (GPIO_NUM_0)
#define RC522_CLK_IO (GPIO_NUM_45)
#define RC522_MOSI_IO (GPIO_NUM_48)
#define RC522_MISO_IO (GPIO_NUM_47)
#define RC522_RST_PORT (Gpba02b::kPortA)
#define RC522_RST_PIN (6)
#define RC522_IRQ_PORT (Gpba02b::kPortB)
#define RC522_IRQ_PIN (4)

/*
PWM-GPBA02B
*/
#define PWM_GPBA02B_PA_CLOCK_DIV (3)
#define PWM_GPBA02B_PC_CLOCK_DIV (3)
#define PWM_GPBA02B_DUTY_10_PERCENT (26)
#define PWM_GPBA02B_07_PORT (Gpba02b::kPortA)
#define PWM_GPBA02B_07_PIN (7)
#define PWM_GPBA02B_08_PORT (Gpba02b::kPortC)
#define PWM_GPBA02B_08_PIN (0)
#define PWM_GPBA02B_09_PORT (Gpba02b::kPortC)
#define PWM_GPBA02B_09_PIN (1)
#define PWM_GPBA02B_10_PORT (Gpba02b::kPortC)
#define PWM_GPBA02B_10_PIN (2)  
#define PWM_GPBA02B_11_PORT (Gpba02b::kPortC)
#define PWM_GPBA02B_11_PIN (3)  
#define PWM_GPBA02B_12_PORT (Gpba02b::kPortC)
#define PWM_GPBA02B_12_PIN (4)
#define PWM_GPBA02B_13_PORT (Gpba02b::kPortC)
#define PWM_GPBA02B_13_PIN (5)
#define PWM_GPBA02B_14_PORT (Gpba02b::kPortC)
#define PWM_GPBA02B_14_PIN (6)
#define PWM_GPBA02B_15_PORT (Gpba02b::kPortC)
#define PWM_GPBA02B_15_PIN (7)



