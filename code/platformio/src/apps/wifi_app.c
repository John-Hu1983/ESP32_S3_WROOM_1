#include "wifi_app.h"

static lv_obj_t *s_wifi_screen = NULL;

static const stub_app_cfg_t s_wifi_stub_cfg = {
    .title = "WiFi",
    .screen_holder = &s_wifi_screen,
    .back_cb = wifi_app_destroy_and_return,
};

lv_obj_t *wifi_app_create_screen(lv_coord_t lcd_w, lv_coord_t lcd_h)
{
    return stub_app_create_screen(lcd_w, lcd_h, &s_wifi_stub_cfg);
}

void wifi_app_release_resources(void)
{
    /* Empty stub app: no extra heap resource to release yet. */
}

void wifi_app_destroy_and_return(void)
{
    desktop_app_return_to_home();
}

