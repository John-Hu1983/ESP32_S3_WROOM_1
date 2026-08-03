#include "app_home_nav.h"

#include <stddef.h>

static app_home_nav_cb_t s_home_nav_cb = NULL;

void app_home_nav_set_callback(app_home_nav_cb_t callback)
{
    s_home_nav_cb = callback;
}

void app_home_nav_request_home(void)
{
    if (s_home_nav_cb != NULL)
    {
        s_home_nav_cb();
    }
}
