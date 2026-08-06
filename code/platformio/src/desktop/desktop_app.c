#include "desktop_app.h"

#define TAG "DESKTOP"

static const desktop_icon_s s_desktop_icons[DESKTOP_ICON_COUNT] = {
    {LV_SYMBOL_VOLUME_MID, "Camera", LV_COLOR_MAKE(0xE9, 0x54, 0x20),
     camera_app_create_screen, camera_app_release_resources},

    {LV_SYMBOL_IMAGE, "Gallery", LV_COLOR_MAKE(0xD9, 0x4B, 0x3D),
     gallery_app_create_screen, gallery_app_release_resources},

    {LV_SYMBOL_AUDIO, "Music", LV_COLOR_MAKE(0x77, 0x21, 0x6F),
     music_app_create_screen, music_app_release_resources},

    {LV_SYMBOL_VIDEO, "Scope", LV_COLOR_MAKE(0xF2, 0x7C, 0x38),
     scope_app_create_screen, scope_app_release_resources},

    {LV_SYMBOL_WIFI, "WiFi", LV_COLOR_MAKE(0xC0, 0x56, 0x3F),
     wifi_app_create_screen, wifi_app_release_resources},

    {LV_SYMBOL_BLUETOOTH, "BT", LV_COLOR_MAKE(0xB6, 0x5C, 0x2C),
     bt_app_create_screen, bt_app_release_resources},

    {LV_SYMBOL_SD_CARD, "File", LV_COLOR_MAKE(0xE1, 0x9A, 0x35),
     sd_app_create_screen, sd_app_release_resources},

    {LV_SYMBOL_BATTERY_FULL, "Battery", LV_COLOR_MAKE(0x8F, 0x67, 0x45),
     battery_app_create_screen, battery_app_release_resources},

    {LV_SYMBOL_BELL, "Alerts", LV_COLOR_MAKE(0xC2, 0x3B, 0x4A),
     alerts_app_create_screen, alerts_app_release_resources},
        
    {LV_SYMBOL_REFRESH, "Tools", LV_COLOR_MAKE(0x8A, 0x3D, 0x5D),
     tools_app_create_screen, tools_app_release_resources},

    {LV_SYMBOL_SETTINGS, "Setting", LV_COLOR_MAKE(0xA8, 0x70, 0x3A),
     setting_app_create_screen, setting_app_release_resources},

    {LV_SYMBOL_POWER, "Power", LV_COLOR_MAKE(0x6F, 0x4A, 0x34),
     power_app_create_screen, power_app_release_resources},
};

static lv_disp_draw_buf_t s_lv_draw_buf;
static lv_disp_drv_t s_lv_disp_drv;
static lv_color_t *s_lv_buf_1 = NULL;
static esp_timer_handle_t s_lv_tick_timer = NULL;
static uint16_t s_lcd_width = 0;
static uint16_t s_lcd_height = 0;
static desktop_app_select_s s_app_select = {
    .desktop_screen = NULL,
    .active_app_screen = NULL,
    .icon_selected_idx = DESKTOP_ICON_COUNT,
    .active_app_idx = DESKTOP_ICON_COUNT,
    .pending_app_idx = DESKTOP_ICON_COUNT,
    .app_switching = 0U,
};

/*
 * brief: Apply or clear LVGL checked state on an object.
 * input: obj - target LVGL object; checked - nonzero to set checked state.
 * output: None.
 */
static void _desktop_set_checked_state(lv_obj_t *obj, uint8_t checked)
{
    if (obj == NULL)
    {
        return;
    }

    if (!lv_obj_has_state(obj, LV_STATE_CHECKED) && checked != 0U)
    {
        lv_obj_add_state(obj, LV_STATE_CHECKED);
    }
    else if (lv_obj_has_state(obj, LV_STATE_CHECKED) && checked == 0U)
    {
        lv_obj_clear_state(obj, LV_STATE_CHECKED);
    }
}

/*
 * brief: Update checked state for one icon and its child labels.
 * input: idx - icon index; checked - nonzero for checked state.
 * output: None.
 */
static void _desktop_set_icon_checked(uint32_t idx, uint8_t checked)
{
    lv_obj_t *btn;

    if (idx >= DESKTOP_ICON_COUNT)
    {
        return;
    }

    btn = s_app_select.icon_btns[idx];
    if (btn == NULL)
    {
        return;
    }

    _desktop_set_checked_state(btn, checked);
    _desktop_set_checked_state(s_app_select.icon_symbols[idx], checked);
    _desktop_set_checked_state(s_app_select.icon_names[idx], checked);
}

/*
 * brief: Set current selected icon and refresh visual checked state.
 * input: idx - target icon index.
 * output: None.
 */
static void _desktop_select_icon(uint32_t idx)
{
    uint32_t prev_idx;

    if (idx >= DESKTOP_ICON_COUNT)
    {
        return;
    }

    prev_idx = s_app_select.icon_selected_idx;
    if (prev_idx == idx)
    {
        return;
    }

    if (prev_idx < DESKTOP_ICON_COUNT)
    {
        _desktop_set_icon_checked(prev_idx, 0U);
    }

    _desktop_set_icon_checked(idx, 1U);
    s_app_select.icon_selected_idx = idx;
}

/*
 * brief: Release resources of the currently active sub-app.
 * input: None.
 * output: None.
 */
static void _desktop_release_active_app_resources(void)
{
    uint32_t app_idx;

    app_idx = s_app_select.active_app_idx;
    if (app_idx >= DESKTOP_ICON_COUNT)
    {
        return;
    }

    if (s_desktop_icons[app_idx].release_cb != NULL)
    {
        s_desktop_icons[app_idx].release_cb();
    }
}

/*
 * brief: Clear icon object references after desktop screen is destroyed.
 * input: None.
 * output: None.
 */
static void _desktop_reset_icon_refs(void)
{
    uint32_t i;

    s_app_select.icon_selected_idx = DESKTOP_ICON_COUNT;
    for (i = 0; i < DESKTOP_ICON_COUNT; i++)
    {
        s_app_select.icon_btns[i] = NULL;
        s_app_select.icon_symbols[i] = NULL;
        s_app_select.icon_names[i] = NULL;
    }
}

/*
 * brief: Build desktop grid UI and load it as the active LVGL screen.
 * input: None.
 * output: None.
 */
static void _desktop_create_ui(void)
{
    static lv_coord_t col_dsc[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
    static lv_coord_t row_dsc[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
    lv_obj_t *scr;
    lv_obj_t *grid;
    lv_coord_t grid_y;
    lv_coord_t grid_h;
    lv_coord_t i;

    if ((s_app_select.desktop_screen != NULL) && lv_obj_is_valid(s_app_select.desktop_screen))
    {
        lv_obj_del(s_app_select.desktop_screen);
        s_app_select.desktop_screen = NULL;
    }

    s_app_select.active_app_screen = NULL;
    s_app_select.active_app_idx = DESKTOP_ICON_COUNT;
    s_app_select.icon_selected_idx = DESKTOP_ICON_COUNT;

    scr = lv_obj_create(NULL);
    s_app_select.desktop_screen = scr;

    grid_y = app_status_bar_content_top() + DESKTOP_GRID_TOP_GAP;
    grid_h = app_status_bar_content_bottom() - grid_y - DESKTOP_GRID_BOTTOM_GAP;
    if (grid_h < 80)
    {
        grid_h = 80;
    }

    lv_obj_set_style_bg_color(scr, lv_color_hex(APP_THEME_BG_HEX), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    grid = lv_obj_create(scr);
    lv_obj_set_size(grid, (lv_coord_t)s_lcd_width - (2 * DESKTOP_MARGIN_X), grid_h);
    lv_obj_set_pos(grid, DESKTOP_MARGIN_X, grid_y);
    lv_obj_set_style_bg_opa(grid, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(grid, 0, 0);
    lv_obj_set_style_radius(grid, 0, 0);
    lv_obj_set_style_pad_all(grid, 0, 0);
    lv_obj_set_style_pad_row(grid, DESKTOP_ICON_GAP_Y, 0);
    lv_obj_set_style_pad_column(grid, DESKTOP_ICON_GAP_X, 0);
    lv_obj_set_grid_dsc_array(grid, col_dsc, row_dsc);

    for (i = 0; i < DESKTOP_ICON_COUNT; i++)
    {
        lv_obj_t *btn;
        lv_obj_t *symbol;
        lv_obj_t *name;
        lv_color_t selected_color;
        lv_coord_t row = i / DESKTOP_ICON_COLS;
        lv_coord_t col = i % DESKTOP_ICON_COLS;

        s_app_select.icon_btns[i] = NULL;
        s_app_select.icon_symbols[i] = NULL;
        s_app_select.icon_names[i] = NULL;

        btn = lv_btn_create(grid);
        lv_obj_remove_style_all(btn);
        selected_color = lv_color_hex(APP_THEME_ACCENT_ACTIVE_HEX);

        lv_obj_set_grid_cell(btn,
                             LV_GRID_ALIGN_STRETCH, col, 1,
                             LV_GRID_ALIGN_STRETCH, row, 1);
        lv_obj_set_style_bg_color(btn, s_desktop_icons[i].color, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_color(btn, selected_color, LV_PART_MAIN | LV_STATE_CHECKED);
        lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_bg_color(btn, s_desktop_icons[i].color, LV_PART_MAIN | LV_STATE_PRESSED);
        lv_obj_set_style_bg_color(btn, selected_color, LV_PART_MAIN | LV_STATE_CHECKED | LV_STATE_PRESSED);
        lv_obj_set_style_text_color(btn, lv_color_hex(APP_THEME_TEXT_PRIMARY_HEX), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_color(btn, lv_color_white(), LV_PART_MAIN | LV_STATE_CHECKED);
        lv_obj_set_style_text_color(btn, lv_color_hex(APP_THEME_TEXT_PRIMARY_HEX), LV_PART_MAIN | LV_STATE_PRESSED);
        lv_obj_set_style_text_color(btn, lv_color_white(), LV_PART_MAIN | LV_STATE_CHECKED | LV_STATE_PRESSED);
        lv_obj_set_style_border_width(btn, 2, LV_PART_MAIN);
        lv_obj_set_style_border_color(btn, lv_color_white(), LV_PART_MAIN | LV_STATE_CHECKED);
        lv_obj_set_style_border_color(btn, lv_color_white(), LV_PART_MAIN | LV_STATE_CHECKED | LV_STATE_PRESSED);
        lv_obj_set_style_border_opa(btn, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_border_opa(btn, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_CHECKED);
        lv_obj_set_style_border_opa(btn, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_CHECKED | LV_STATE_PRESSED);
        lv_obj_set_style_outline_width(btn, 0, LV_PART_MAIN);
        lv_obj_set_style_radius(btn, 14, LV_PART_MAIN);
        lv_obj_set_style_shadow_width(btn, 0, LV_PART_MAIN);
        lv_obj_set_style_shadow_opa(btn, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_transform_width(btn, 0, LV_PART_MAIN);
        lv_obj_set_style_transform_height(btn, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(btn, 6, LV_PART_MAIN);
        s_app_select.icon_btns[i] = btn;

        symbol = lv_label_create(btn);
        lv_label_set_text(symbol, s_desktop_icons[i].symbol);
        lv_obj_set_style_text_color(symbol, lv_color_hex(APP_THEME_TEXT_PRIMARY_HEX), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_color(symbol, lv_color_white(), LV_PART_MAIN | LV_STATE_CHECKED);
        lv_obj_set_style_text_font(symbol, DESKTOP_FONT_ICON, 0);
        lv_obj_align(symbol, LV_ALIGN_TOP_MID, 0, 4);
        s_app_select.icon_symbols[i] = symbol;

        name = lv_label_create(btn);
        lv_label_set_text(name, s_desktop_icons[i].name);
        lv_obj_set_style_text_color(name, lv_color_hex(APP_THEME_TEXT_PRIMARY_HEX), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_color(name, lv_color_white(), LV_PART_MAIN | LV_STATE_CHECKED);
        lv_obj_set_style_text_font(name, DESKTOP_FONT_ICON_NAME, 0);
        lv_obj_set_style_text_letter_space(name, 1, 0);
        lv_obj_align(name, LV_ALIGN_BOTTOM_MID, 0, -6);
        s_app_select.icon_names[i] = name;
    }
    s_app_select.app_switching = 0U;
    lv_scr_load(scr);
}

/*
 * brief: Execute asynchronous sub-app open request on LVGL context.
 * input: user_data - unused async payload.
 * output: None.
 */
static void _desktop_open_app_async(void *user_data)
{
    lv_obj_t *app_screen;
    lv_obj_t *old_app_screen;
    const char *app_name;
    uint32_t app_idx;

    (void)user_data;

    app_idx = s_app_select.pending_app_idx;
    s_app_select.pending_app_idx = DESKTOP_ICON_COUNT;
    if (app_idx >= DESKTOP_ICON_COUNT)
    {
        s_app_select.app_switching = 0U;
        return;
    }

    app_name = s_desktop_icons[app_idx].name;
    if (s_desktop_icons[app_idx].create_screen_cb == NULL)
    {
        ESP_LOGE(TAG, "create callback missing for %s", app_name);
        s_app_select.app_switching = 0U;
        return;
    }

    old_app_screen = s_app_select.active_app_screen;
    if ((old_app_screen != NULL) && lv_obj_is_valid(old_app_screen))
    {
        _desktop_release_active_app_resources();
        lv_obj_del_async(old_app_screen);
    }
    s_app_select.active_app_screen = NULL;
    s_app_select.active_app_idx = DESKTOP_ICON_COUNT;

    app_screen = s_desktop_icons[app_idx].create_screen_cb((lv_coord_t)s_lcd_width,
                                                           (lv_coord_t)s_lcd_height);
    if (app_screen == NULL)
    {
        ESP_LOGE(TAG, "create app screen failed for %s", app_name);

        if ((s_app_select.desktop_screen == NULL) || !lv_obj_is_valid(s_app_select.desktop_screen))
        {
            _desktop_create_ui();
        }

        s_app_select.app_switching = 0U;
        return;
    }

    lv_scr_load(app_screen);

    s_app_select.active_app_screen = app_screen;
    s_app_select.active_app_idx = app_idx;

    if ((s_app_select.desktop_screen != NULL) && lv_obj_is_valid(s_app_select.desktop_screen))
    {
        lv_obj_del_async(s_app_select.desktop_screen);
    }

    s_app_select.desktop_screen = NULL;
    _desktop_reset_icon_refs();
    s_app_select.app_switching = 0U;
}

/*
 * brief: Execute asynchronous return-home request on LVGL context.
 * input: user_data - unused async payload.
 * output: None.
 */
static void _desktop_return_home_async(void *user_data)
{
    lv_obj_t *app_screen;

    (void)user_data;

    app_screen = s_app_select.active_app_screen;
    _desktop_release_active_app_resources();

    if ((app_screen != NULL) && lv_obj_is_valid(app_screen))
    {
        lv_obj_del_async(app_screen);
    }

    s_app_select.active_app_screen = NULL;
    s_app_select.active_app_idx = DESKTOP_ICON_COUNT;

    _desktop_create_ui();
    s_app_select.app_switching = 0U;
}

/*
 * brief: Queue a request to open one desktop app by icon index.
 * input: idx - desktop icon index.
 * output: ESP_OK on success; otherwise ESP_ERR_INVALID_ARG/STATE or ESP_FAIL.
 */
static esp_err_t _desktop_request_open_app(uint32_t idx)
{
    if (idx >= DESKTOP_ICON_COUNT)
    {
        return ESP_ERR_INVALID_ARG;
    }

    if (s_app_select.app_switching != 0U)
    {
        return ESP_ERR_INVALID_STATE;
    }

    s_app_select.pending_app_idx = idx;
    s_app_select.app_switching = 1U;
    if (lv_async_call(_desktop_open_app_async, NULL) != LV_RES_OK)
    {
        s_app_select.pending_app_idx = DESKTOP_ICON_COUNT;
        s_app_select.app_switching = 0U;
        ESP_LOGE(TAG, "lv_async_call _desktop_open_app_async failed");
        return ESP_FAIL;
    }

    return ESP_OK;
}

/*
 * brief: Queue a request to close current sub-app and return home.
 * input: None.
 * output: ESP_OK on success; otherwise ESP_ERR_INVALID_STATE or ESP_FAIL.
 */
static esp_err_t _desktop_request_return_home(void)
{
    if (s_app_select.app_switching != 0U)
    {
        return ESP_ERR_INVALID_STATE;
    }

    if ((s_app_select.active_app_screen == NULL) ||
        (s_app_select.active_app_idx >= DESKTOP_ICON_COUNT))
    {
        return ESP_ERR_INVALID_STATE;
    }

    s_app_select.app_switching = 1U;
    if (lv_async_call(_desktop_return_home_async, NULL) != LV_RES_OK)
    {
        s_app_select.app_switching = 0U;
        ESP_LOGE(TAG, "lv_async_call _desktop_return_home_async failed");
        return ESP_FAIL;
    }

    return ESP_OK;
}

/*
 * brief: Resolve next icon index for DOWN navigation.
 * input: None.
 * output: Next icon index with wrap-around.
 */
static uint32_t _desktop_next_icon_idx(void)
{
    uint32_t idx;

    idx = s_app_select.icon_selected_idx;
    if (idx >= DESKTOP_ICON_COUNT)
    {
        return 0U;
    }

    return (idx + 1U) % DESKTOP_ICON_COUNT;
}

/*
 * brief: Resolve previous icon index for UP navigation.
 * input: None.
 * output: Previous icon index with wrap-around.
 */
static uint32_t _desktop_prev_icon_idx(void)
{
    uint32_t idx;

    idx = s_app_select.icon_selected_idx;
    if (idx >= DESKTOP_ICON_COUNT)
    {
        return (DESKTOP_ICON_COUNT - 1U);
    }

    return (idx + DESKTOP_ICON_COUNT - 1U) % DESKTOP_ICON_COUNT;
}

/*
 * brief: Check whether desktop keyboard navigation can be handled now.
 * input: None.
 * output: true when desktop screen is active and not switching apps.
 */
static bool _desktop_can_handle_key_nav(void)
{
    if (s_app_select.app_switching != 0U)
    {
        return false;
    }

    if (s_app_select.active_app_idx < DESKTOP_ICON_COUNT)
    {
        return false;
    }

    if ((s_app_select.desktop_screen == NULL) || !lv_obj_is_valid(s_app_select.desktop_screen))
    {
        return false;
    }

    return true;
}

/*
 * brief: Decide whether desktop task is allowed to scan keyboard right now.
 * input: None.
 * output: true only when desktop screen is active and no app switch is in progress.
 */
static bool _desktop_can_scan_keyboard(void)
{
    return _desktop_can_handle_key_nav();
}

/*
 * brief: Handle one keyboard event for desktop navigation and app enter/exit.
 * input: btn_val - decoded keyboard event status.
 * output: None.
 */
static void _desktop_handle_key_event(btn_status_e btn_val)
{
    uint32_t target_idx;

    if ((btn_val == Btn_Both_Click) &&
        (s_app_select.app_switching == 0U) &&
        (s_app_select.active_app_idx < DESKTOP_ICON_COUNT) &&
        (s_app_select.active_app_screen != NULL) &&
        lv_obj_is_valid(s_app_select.active_app_screen))
    {
        (void)_desktop_request_return_home();
        return;
    }

    if (!_desktop_can_handle_key_nav())
    {
        return;
    }

    switch (btn_val)
    {
    case Btn_Up_Click:
        target_idx = _desktop_prev_icon_idx();
        _desktop_select_icon(target_idx);
        break;

    case Btn_Down_Click:
        target_idx = _desktop_next_icon_idx();
        _desktop_select_icon(target_idx);
        break;

    case Btn_Up_Hold:
    case Btn_Down_Hold:
    case Btn_Both_Hold:
        if (s_app_select.icon_selected_idx < DESKTOP_ICON_COUNT)
        {
            (void)_desktop_request_open_app(s_app_select.icon_selected_idx);
        }
        break;
    default:
        break;
    }
}

/*
 * brief: Initialize LVGL core, display driver, draw buffer, and tick timer.
 * input: None.
 * output: ESP_OK on success; otherwise ESP_ERR_NO_MEM or timer error code.
 */
static esp_err_t _desktop_lvgl_init(void)
{
    esp_timer_create_args_t tick_timer_args = {
        .callback = desktop_common_lvgl_tick_cb,
        .arg = NULL,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "lvgl_tick",
    };
    size_t draw_buf_pixels;
    esp_err_t ret;

    lv_init();

    draw_buf_pixels = (size_t)s_lcd_width * LVGL_DRAW_BUF_LINES;
    s_lv_buf_1 = (lv_color_t *)heap_caps_malloc(draw_buf_pixels * sizeof(lv_color_t),
                                                MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (s_lv_buf_1 == NULL)
    {
        return ESP_ERR_NO_MEM;
    }

    lv_disp_draw_buf_init(&s_lv_draw_buf, s_lv_buf_1, NULL, draw_buf_pixels);

    lv_disp_drv_init(&s_lv_disp_drv);
    s_lv_disp_drv.hor_res = s_lcd_width;
    s_lv_disp_drv.ver_res = s_lcd_height;
    s_lv_disp_drv.flush_cb = desktop_common_lvgl_flush_cb;
    s_lv_disp_drv.draw_buf = &s_lv_draw_buf;
    s_lv_disp_drv.full_refresh = 0;
    s_lv_disp_drv.antialiasing = 0;
    lv_disp_drv_register(&s_lv_disp_drv);

    ret = esp_timer_create(&tick_timer_args, &s_lv_tick_timer);
    if (ret != ESP_OK)
    {
        return ret;
    }

    ret = esp_timer_start_periodic(s_lv_tick_timer, LVGL_TICK_PERIOD_MS * 1000U);
    if (ret != ESP_OK)
    {
        return ret;
    }

    return ESP_OK;
}

/*
 * brief: Initialize ST7365 panel and cache effective resolution values.
 * input: None.
 * output: ESP_OK on success; otherwise panel initialization/configuration error.
 */
static esp_err_t _desktop_prepare_monitor(void)
{
    esp_err_t ret;
    ret = st7365p_panel_init(NULL);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "st7365p_panel_init failed: %d", (int)ret);
        return ret;
    }

    st7365p_get_resolution(&s_lcd_width, &s_lcd_height);
    if ((s_lcd_width == 0U) || (s_lcd_height == 0U))
    {
        ESP_LOGE(TAG, "invalid LCD resolution");
        return ESP_ERR_INVALID_SIZE;
    }

    ret = st7365p_set_rotation(2);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "st7365p_set_rotation failed: %d", (int)ret);
        return ret;
    }
    st7365p_get_resolution(&s_lcd_width, &s_lcd_height);

    ESP_LOGI(TAG, "LVGL desktop init on %ux%u", (unsigned)s_lcd_width, (unsigned)s_lcd_height);
    return ESP_OK;
}

/*
 * brief: Public entry to request returning from current sub-app to desktop.
 * input: None.
 * output: None.
 */
void desktop_app_return_to_home(void)
{
    (void)_desktop_request_return_home();
}

/*
 * brief: Main desktop LVGL task loop handling rendering and keyboard events.
 * input: param - unused task parameter.
 * output: None.
 */
static void _desktop_lvgl_task(void *param)
{
    (void)param;
    btn_scan_s btn = {0};
    btn_status_e btn_val;

    while (1)
    {
        lv_timer_handler();
        delay_ms(LVGL_TASK_PERIOD_MS);

        if (!_desktop_can_scan_keyboard())
        {
            continue;
        }

        btn_val = keyboard_scan_event(&btn, LVGL_TASK_PERIOD_MS);
        _desktop_handle_key_event(btn_val);
    }
}

/*
 * brief: Start desktop subsystem including panel, LVGL, status bar, and task loop.
 * input: None.
 * output: ESP_OK on success; otherwise propagated startup error.
 */
esp_err_t desktop_app_start(void)
{
    esp_err_t ret;
    BaseType_t task_ok;

    ret = _desktop_prepare_monitor();
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "_desktop_prepare_monitor failed: %d", (int)ret);
        return ret;
    }

    ret = _desktop_lvgl_init();
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "_desktop_lvgl_init failed: %d", (int)ret);
        return ret;
    }

    ret = app_status_bar_init((lv_coord_t)s_lcd_width, (lv_coord_t)s_lcd_height);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "app_status_bar_init failed: %d", (int)ret);
        return ret;
    }

    ret = apps_idle_task_start();
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "apps_idle_task_start failed: %d", (int)ret);
        return ret;
    }

    _desktop_create_ui();

    task_ok = xTaskCreate(_desktop_lvgl_task,
                          "desktop_lvgl",
                          10240,
                          NULL,
                          5,
                          NULL);
    if (task_ok != pdPASS)
    {
        ESP_LOGE(TAG, "xTaskCreate desktop_lvgl failed");
        return ESP_FAIL;
    }

    return ESP_OK;
}
