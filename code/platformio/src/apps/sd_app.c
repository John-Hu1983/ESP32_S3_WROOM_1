#include "sd_app.h"

static lv_obj_t *s_sd_screen = NULL;

static const stub_app_cfg_t s_sd_stub_cfg = {
    .title = "SD",
    .screen_holder = &s_sd_screen,
    .back_cb = sd_app_destroy_and_return,
};

lv_obj_t *sd_app_create_screen(lv_coord_t lcd_w, lv_coord_t lcd_h)
{
    return stub_app_create_screen(lcd_w, lcd_h, &s_sd_stub_cfg);
}

void sd_app_release_resources(void)
{
    /* Empty stub app: no extra heap resource to release yet. */
}

void sd_app_destroy_and_return(void)
{
    app_home_nav_request_home();
}

