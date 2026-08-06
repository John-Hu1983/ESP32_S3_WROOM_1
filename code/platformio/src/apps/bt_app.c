#include "bt_app.h"

static lv_obj_t *s_bt_screen = NULL;

static const stub_app_cfg_t s_bt_stub_cfg = {
    .title = "BT",
    .screen_holder = &s_bt_screen,
    .back_cb = bt_app_destroy_and_return,
};

lv_obj_t *bt_app_create_screen(lv_coord_t lcd_w, lv_coord_t lcd_h)
{
    return stub_app_create_screen(lcd_w, lcd_h, &s_bt_stub_cfg);
}

void bt_app_release_resources(void)
{
    /* Empty stub app: no extra heap resource to release yet. */
}

void bt_app_destroy_and_return(void)
{
    desktop_app_return_to_home();
}

