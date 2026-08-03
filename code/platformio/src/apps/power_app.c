#include "power_app.h"

static lv_obj_t *s_power_screen = NULL;

static const stub_app_cfg_t s_power_stub_cfg = {
    .title = "Power",
    .screen_holder = &s_power_screen,
    .back_cb = power_app_destroy_and_return,
};

lv_obj_t *power_app_create_screen(lv_coord_t lcd_w, lv_coord_t lcd_h)
{
    return stub_app_create_screen(lcd_w, lcd_h, &s_power_stub_cfg);
}

void power_app_release_resources(void)
{
    /* Empty stub app: no extra heap resource to release yet. */
}

void power_app_destroy_and_return(void)
{
    app_home_nav_request_home();
}

