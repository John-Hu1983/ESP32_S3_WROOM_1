#include "stub_app.h"

#define STUB_MARGIN_X 12
#define STUB_TOP_GAP 8
#define STUB_BOTTOM_GAP 8

static void _stub_app_back_click_cb(lv_event_t *e)
{
    const stub_app_cfg_t *cfg = (const stub_app_cfg_t *)lv_event_get_user_data(e);

    if ((cfg != NULL) && (cfg->back_cb != NULL))
    {
        cfg->back_cb();
    }
}

static void _stub_app_screen_delete_cb(lv_event_t *e)
{
    lv_obj_t **screen_holder = (lv_obj_t **)lv_event_get_user_data(e);

    if (screen_holder != NULL)
    {
        *screen_holder = NULL;
    }
}

lv_obj_t *stub_app_create_screen(lv_coord_t lcd_w, lv_coord_t lcd_h, const stub_app_cfg_t *cfg)
{
    lv_obj_t *scr;
    lv_obj_t *title;
    lv_obj_t *msg;
    lv_obj_t *back_btn;
    lv_obj_t *back_text;
    lv_coord_t content_top;
    lv_coord_t content_bottom;

    if ((cfg == NULL) || (cfg->title == NULL) || (cfg->screen_holder == NULL) ||
        (lcd_w <= (2 * STUB_MARGIN_X)) || (lcd_h <= (2 * APP_STATUS_BAR_HEIGHT + 20)))
    {
        return NULL;
    }

    scr = lv_obj_create(NULL);
    if (scr == NULL)
    {
        return NULL;
    }

    lv_obj_set_style_bg_color(scr, lv_color_hex(APP_THEME_BG_HEX), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    content_top = app_status_bar_content_top() + STUB_TOP_GAP;
    content_bottom = app_status_bar_content_bottom() - STUB_BOTTOM_GAP;
    title = lv_label_create(scr);
    lv_label_set_text_fmt(title, "%s", cfg->title);
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, STUB_MARGIN_X, content_top);

    msg = lv_label_create(scr);
    lv_label_set_text(msg, "App stub: to be implemented");
    lv_obj_set_style_text_color(msg, lv_color_hex(APP_THEME_TEXT_SECONDARY_HEX), 0);
    lv_obj_align(msg, LV_ALIGN_TOP_LEFT, STUB_MARGIN_X, content_top + 28);

    back_btn = lv_btn_create(scr);
    lv_obj_set_size(back_btn, 110, 42);
    lv_obj_align(back_btn, LV_ALIGN_BOTTOM_RIGHT, -STUB_MARGIN_X, -((lcd_h - content_bottom) + 4));
    lv_obj_set_style_bg_color(back_btn, lv_color_hex(APP_THEME_ACCENT_HEX), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(back_btn, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(back_btn, 10, LV_PART_MAIN);

    back_text = lv_label_create(back_btn);
    lv_label_set_text(back_text, LV_SYMBOL_LEFT " Home");
    lv_obj_set_style_text_color(back_text, lv_color_white(), 0);
    lv_obj_center(back_text);

    lv_obj_add_event_cb(back_btn, _stub_app_back_click_cb, LV_EVENT_CLICKED, (void *)cfg);
    lv_obj_add_event_cb(scr, _stub_app_screen_delete_cb, LV_EVENT_DELETE, cfg->screen_holder);

    *cfg->screen_holder = scr;

    return scr;
}

void stub_app_destroy_screen(lv_obj_t **screen_holder)
{
    lv_obj_t *screen;

    if ((screen_holder == NULL) || (*screen_holder == NULL))
    {
        return;
    }

    screen = *screen_holder;
    *screen_holder = NULL;

    if (lv_obj_is_valid(screen))
    {
        lv_obj_del_async(screen);
    }
}
