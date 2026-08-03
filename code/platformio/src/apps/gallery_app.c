#include "gallery_app.h"

static lv_obj_t *s_gallery_screen = NULL;

static const stub_app_cfg_t s_gallery_stub_cfg = {
    .title = "Gallery",
    .screen_holder = &s_gallery_screen,
    .back_cb = gallery_app_destroy_and_return,
};

lv_obj_t *gallery_app_create_screen(lv_coord_t lcd_w, lv_coord_t lcd_h)
{
    return stub_app_create_screen(lcd_w, lcd_h, &s_gallery_stub_cfg);
}

void gallery_app_release_resources(void)
{
    /* Empty stub app: no extra heap resource to release yet. */
}

void gallery_app_destroy_and_return(void)
{
    app_home_nav_request_home();
}

