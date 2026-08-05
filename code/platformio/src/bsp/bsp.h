#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_heap_caps.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_psram.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "bsp/delay.h"

#include "user_config.h"
#include "filesystem/usr_fs.h"
#include "hal/usr_spi.h"
#include "peripherals/gpba02b.h"
#include "peripherals/ht517.h"
#include "peripherals/keyboard.h"
 

/* Enable board power output path (power lock high). */
esp_err_t bsp_power_on(void);
/* Disable board power output path (power lock low). */
esp_err_t bsp_power_off(void);
/* Initialize board-level peripherals and default power state. */
esp_err_t bsp_init_whole(void);