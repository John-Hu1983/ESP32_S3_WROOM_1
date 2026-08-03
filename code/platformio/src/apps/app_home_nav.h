#pragma once

#include <stdint.h>

typedef void (*app_home_nav_cb_t)(void);

void app_home_nav_set_callback(app_home_nav_cb_t callback);
void app_home_nav_request_home(void);
