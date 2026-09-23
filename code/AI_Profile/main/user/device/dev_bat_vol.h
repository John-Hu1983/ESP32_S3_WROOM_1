#pragma once

#include <stdint.h>

#include "esp_err.h"
#include "esp_log.h"

#include "hal/hal_adc.h"
#include "user/inc/bsp_config.h"

#ifdef __cplusplus
extern "C" {
#endif

// clang-format off
#define BATVOL_ADC_SAMPLE_COUNT              (12U)
#define BATVOL_ADC_FALLBACK_FULL_SCALE_MV    (3300U)
#define BATVOL_ADC_FALLBACK_MAX_RAW          (4095U)
#define BATVOL_DIAG_ENABLE                   (1U)
#define BATVOL_DIAG_EVERY_N_READS            (1U)
#define BATVOL_DIAG_DIRECT_ADC_MV_OUTPUT     (0U)
#define BATVOL_UP_RESISTER                   (91U)
#define BATVOL_LOW_RESISTER                  (68U)
// clang-format on

esp_err_t batvol_init_cfg(void);
esp_err_t batvol_read_mv(uint16_t up_r, uint16_t low_r, uint16_t* mv);

#ifdef __cplusplus
}
#endif
