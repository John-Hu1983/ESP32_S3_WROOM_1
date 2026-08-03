#pragma once

#include <stdint.h>

#include "app_home_nav.h"
#include "stub_app.h"

lv_obj_t *setting_app_create_screen(lv_coord_t lcd_w, lv_coord_t lcd_h);
void setting_app_release_resources(void);
void setting_app_destroy_and_return(void);
