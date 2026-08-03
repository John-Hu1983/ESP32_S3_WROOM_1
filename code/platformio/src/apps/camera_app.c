#include "camera_app.h"

static lv_obj_t *s_camera_screen = NULL;

static const stub_app_cfg_t s_camera_stub_cfg = {
    .title = "Camera",
    .screen_holder = &s_camera_screen,
    .back_cb = camera_app_destroy_and_return,
};

lv_obj_t *camera_app_create_screen(lv_coord_t lcd_w, lv_coord_t lcd_h)
{
    return stub_app_create_screen(lcd_w, lcd_h, &s_camera_stub_cfg);
}

void camera_app_release_resources(void)
{
    /* Empty stub app: no extra heap resource to release yet. */
}

void camera_app_destroy_and_return(void)
{
    app_home_nav_request_home();
}

