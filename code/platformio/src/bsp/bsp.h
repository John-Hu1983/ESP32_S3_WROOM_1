#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_heap_caps.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_psram.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "user_config.h"
#include "hal/usr_spi.h"
#include "peripherals/gpba02b.h"

esp_err_t bsp_power_on(void);
esp_err_t bsp_power_off(void);
esp_err_t bsp_init_whole(void);
