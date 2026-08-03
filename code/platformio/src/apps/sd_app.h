#pragma once

#include <stdint.h>

#include "app_home_nav.h"
#include "stub_app.h"

lv_obj_t *sd_app_create_screen(lv_coord_t lcd_w, lv_coord_t lcd_h);
void sd_app_release_resources(void);
void sd_app_destroy_and_return(void);
