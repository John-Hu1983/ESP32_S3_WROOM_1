#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * brief : Create the camera page root screen object.
 * input : lcd_w - LCD width; lcd_h - LCD height.
 * output: Created LVGL screen object.
 * type  : public
 */
lv_obj_t* camera_create_screen(lv_coord_t lcd_w, lv_coord_t lcd_h);

#ifdef __cplusplus
}
#endif
