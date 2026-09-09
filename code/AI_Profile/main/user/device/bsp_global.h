#pragma once

#include <esp_log.h>
#include <esp_adc/adc_cali.h>
#include <esp_adc/adc_cali_scheme.h>
#include <esp_adc/adc_oneshot.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "user/device/dev_gpba02b.h"

#ifdef __cplusplus
extern "C" {
#endif

void delay_ms(uint32_t ms);
esp_err_t desktop_start_task(void);

// clang-format off
#define BSP_BATTERY_ADC_UNIT                  (ADC_UNIT_1)
#define BSP_BATTERY_ADC_ATTEN                 (ADC_ATTEN_DB_12)
#define BSP_BATTERY_ADC_BITWIDTH              (ADC_BITWIDTH_DEFAULT)
#define BSP_BATTERY_ADC_DISCARD_COUNT         (4U)
#define BSP_BATTERY_ADC_SAMPLE_COUNT          (8U)
#define BSP_BATTERY_ADC_SAMPLE_DELAY_US       (200U)
#define BSP_BATTERY_ADC_FALLBACK_FULL_SCALE_MV (3300U)
#define BSP_BATTERY_ADC_FALLBACK_MAX_RAW      (4095U)

#define BSP_BATTERY_DIVIDER_TOP_KOHM          (91U)
#define BSP_BATTERY_DIVIDER_BOTTOM_KOHM       (68U)
#define BSP_BATTERY_DIVIDER_TOTAL_KOHM        (BSP_BATTERY_DIVIDER_TOP_KOHM + BSP_BATTERY_DIVIDER_BOTTOM_KOHM)
// clang-format on

void bsp_init_adc_converter(void);
uint16_t bsp_read_battery_mv(void);

void bsp_reset_lcd(void);
void bsp_set_audio_ctrl(bool enable);
void bsp_init_total(void);

#ifdef __cplusplus
}
#endif