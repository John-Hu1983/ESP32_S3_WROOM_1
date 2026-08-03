#include "battery_app.h"

static lv_obj_t *s_battery_screen = NULL;

static const stub_app_cfg_t s_battery_stub_cfg = {
    .title = "Battery",
    .screen_holder = &s_battery_screen,
    .back_cb = battery_app_destroy_and_return,
};

lv_obj_t *battery_app_create_screen(lv_coord_t lcd_w, lv_coord_t lcd_h)
{
    return stub_app_create_screen(lcd_w, lcd_h, &s_battery_stub_cfg);
}

void battery_app_release_resources(void)
{
    /* Empty stub app: no extra heap resource to release yet. */
}

void battery_app_destroy_and_return(void)
{
    app_home_nav_request_home();
}

