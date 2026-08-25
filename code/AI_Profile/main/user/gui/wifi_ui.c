#include "wifi_ui.h"

/*
 * brief : Create the Wi-Fi page root screen object.
 * input : lcd_w - LCD width; lcd_h - LCD height.
 * output: Created LVGL screen object.
 * type  : public
 */
lv_obj_t* wifi_create_screen(lv_coord_t lcd_w, lv_coord_t lcd_h) {
    (void)lcd_w;
    (void)lcd_h;
    return lv_obj_create(NULL);
}
