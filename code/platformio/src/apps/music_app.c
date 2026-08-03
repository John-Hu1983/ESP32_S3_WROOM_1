#include "music_app.h"

static lv_obj_t *s_music_screen = NULL;

static const stub_app_cfg_t s_music_stub_cfg = {
    .title = "Music",
    .screen_holder = &s_music_screen,
    .back_cb = music_app_destroy_and_return,
};

lv_obj_t *music_app_create_screen(lv_coord_t lcd_w, lv_coord_t lcd_h)
{
    return stub_app_create_screen(lcd_w, lcd_h, &s_music_stub_cfg);
}

void music_app_release_resources(void)
{
    /* Empty stub app: no extra heap resource to release yet. */
}

void music_app_destroy_and_return(void)
{
    app_home_nav_request_home();
}

