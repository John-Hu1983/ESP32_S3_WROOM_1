#pragma once

#include <esp_err.h>

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t bsp_env_initialize(void);
esp_err_t bsp_env_pwm_init(void);
esp_err_t bsp_env_lcd_init(void);
esp_err_t bsp_env_camera_init(void);
esp_err_t bsp_env_lcd_reset(void);
esp_err_t bsp_env_camera_reset(void);
esp_err_t bsp_env_rc522_reset(void);

#ifdef __cplusplus
}
#endif
