#pragma once

#include <stdint.h>

#include "app_status_bar.h"
#include "lvgl.h"

typedef void (*stub_app_back_cb_t)(void);

typedef struct
{
    const char *title;
    lv_obj_t **screen_holder;
    stub_app_back_cb_t back_cb;
} stub_app_cfg_t;

lv_obj_t *stub_app_create_screen(lv_coord_t lcd_w, lv_coord_t lcd_h, const stub_app_cfg_t *cfg);
void stub_app_destroy_screen(lv_obj_t **screen_holder);
