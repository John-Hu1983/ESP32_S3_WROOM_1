#include "camera_ui.h"

lv_obj_t* camera_create_screen(lv_coord_t lcd_w, lv_coord_t lcd_h) {
    (void)lcd_w;
    (void)lcd_h;
    return lv_obj_create(NULL);
}
