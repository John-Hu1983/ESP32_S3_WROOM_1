#pragma once

#include "esp_err.h"

/* Initialize ST7365 panel, start LVGL task, and create desktop UI. */
esp_err_t desktop_app_start(void);
