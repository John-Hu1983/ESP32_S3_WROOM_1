#include "desktop_app.h"

#define TAG "DESKTOP"

static const desktop_icon_s s_desktop_icons[DESKTOP_ICON_COUNT] = {
    {LV_SYMBOL_VOLUME_MID, "Camera", LV_COLOR_MAKE(0x2D, 0x5B, 0xFF), camera_app_create_screen, camera_app_release_resources},
    {LV_SYMBOL_IMAGE, "Gallery", LV_COLOR_MAKE(0xEB, 0x4D, 0x8A), gallery_app_create_screen, gallery_app_release_resources},
    {LV_SYMBOL_AUDIO, "Music", LV_COLOR_MAKE(0x6D, 0x5D, 0xF6), music_app_create_screen, music_app_release_resources},
    {LV_SYMBOL_VIDEO, "Scope", LV_COLOR_MAKE(0x00, 0xA1, 0xD6), scope_app_create_screen, scope_app_release_resources},
    {LV_SYMBOL_WIFI, "WiFi", LV_COLOR_MAKE(0x22, 0xB0, 0x7D), wifi_app_create_screen, wifi_app_release_resources},
    {LV_SYMBOL_BLUETOOTH, "BT", LV_COLOR_MAKE(0x1E, 0x90, 0xFF), bt_app_create_screen, bt_app_release_resources},
    {LV_SYMBOL_SD_CARD, "SD", LV_COLOR_MAKE(0xFF, 0x8A, 0x00), sd_app_create_screen, sd_app_release_resources},
    {LV_SYMBOL_BATTERY_FULL, "Battery", LV_COLOR_MAKE(0x5D, 0x66, 0x7A), battery_app_create_screen, battery_app_release_resources},
    {LV_SYMBOL_BELL, "Alerts", LV_COLOR_MAKE(0xD2, 0x4D, 0x57), alerts_app_create_screen, alerts_app_release_resources},
    {LV_SYMBOL_REFRESH, "Tools", LV_COLOR_MAKE(0x7A, 0x4D, 0xD8), tools_app_create_screen, tools_app_release_resources},
    {LV_SYMBOL_SETTINGS, "Setting", LV_COLOR_MAKE(0x00, 0xA8, 0x78), setting_app_create_screen, setting_app_release_resources},
    {LV_SYMBOL_POWER, "Power", LV_COLOR_MAKE(0x1F, 0x29, 0x37), power_app_create_screen, power_app_release_resources},
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

static void desktop_open_app_async(void *user_data);
static void desktop_return_home_async(void *user_data);
static esp_err_t desktop_request_open_app(uint32_t idx);
static esp_err_t desktop_request_return_home(void);
static void desktop_release_active_app_resources(void);
static void desktop_reset_icon_refs(void);
static void desktop_create_ui(void);

static void desktop_set_icon_checked(uint32_t idx, uint8_t checked)
{
    lv_obj_t *btn;
    lv_obj_t *symbol;
    lv_obj_t *name;

    if (idx >= DESKTOP_ICON_COUNT)
    {
        return;
    }

    btn = s_app_select.icon_btns[idx];
    if (btn == NULL)
    {
        return;
    }

    symbol = s_app_select.icon_symbols[idx];
    name = s_app_select.icon_names[idx];

    if (checked != 0U)
    {
        if (!lv_obj_has_state(btn, LV_STATE_CHECKED))
        {
            lv_obj_add_state(btn, LV_STATE_CHECKED);
        }
        if ((symbol != NULL) && !lv_obj_has_state(symbol, LV_STATE_CHECKED))
        {
            lv_obj_add_state(symbol, LV_STATE_CHECKED);
        }
        if ((name != NULL) && !lv_obj_has_state(name, LV_STATE_CHECKED))
        {
            lv_obj_add_state(name, LV_STATE_CHECKED);
        }
    }
    else
    {
        if (lv_obj_has_state(btn, LV_STATE_CHECKED))
        {
            lv_obj_clear_state(btn, LV_STATE_CHECKED);
        }
        if ((symbol != NULL) && lv_obj_has_state(symbol, LV_STATE_CHECKED))
        {
            lv_obj_clear_state(symbol, LV_STATE_CHECKED);
        }
        if ((name != NULL) && lv_obj_has_state(name, LV_STATE_CHECKED))
        {
            lv_obj_clear_state(name, LV_STATE_CHECKED);
        }
    }
}

static lv_color_t desktop_invert_color(lv_color_t color)
{
    lv_color32_t color32;

    color32.full = lv_color_to32(color);

    return lv_color_make((uint8_t)(0xFFU - color32.ch.red),
                         (uint8_t)(0xFFU - color32.ch.green),
                         (uint8_t)(0xFFU - color32.ch.blue));
}

static void desktop_select_icon(uint32_t idx)
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
        desktop_set_icon_checked(prev_idx, 0U);
    }

    desktop_set_icon_checked(idx, 1U);
    s_app_select.icon_selected_idx = idx;
}

static uint32_t desktop_find_icon_index(const desktop_icon_s *icon)
{
    uint32_t i;

    for (i = 0; i < DESKTOP_ICON_COUNT; i++)
    {
        if (&s_desktop_icons[i] == icon)
        {
            return i;
        }
    }

    return DESKTOP_ICON_COUNT;
}

static void lvgl_tick_cb(void *arg)
{
    (void)arg;
    lv_tick_inc(LVGL_TICK_PERIOD_MS);
}

static void lvgl_flush_cb(lv_disp_drv_t *disp_drv, const lv_area_t *area, lv_color_t *color_p)
{
    esp_err_t ret;

    ret = st7365p_lvgl_flush(area->x1, area->y1, area->x2, area->y2, color_p);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "st7365p_lvgl_flush failed: %d", (int)ret);
    }
    lv_disp_flush_ready(disp_drv);
}

static void desktop_icon_click_cb(lv_event_t *e)
{
    const desktop_icon_s *icon = (const desktop_icon_s *)lv_event_get_user_data(e);
    uint32_t idx;

    if (icon == NULL)
    {
        return;
    }

    idx = desktop_find_icon_index(icon);
    if (idx < DESKTOP_ICON_COUNT)
    {
        desktop_select_icon(idx);
        (void)desktop_request_open_app(idx);
    }

    ESP_LOGI(TAG, "%s opened", icon->name);
}

static void desktop_release_active_app_resources(void)
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

static esp_err_t desktop_request_open_app(uint32_t idx)
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
    if (lv_async_call(desktop_open_app_async, NULL) != LV_RES_OK)
    {
        s_app_select.pending_app_idx = DESKTOP_ICON_COUNT;
        s_app_select.app_switching = 0U;
        ESP_LOGE(TAG, "lv_async_call desktop_open_app_async failed");
        return ESP_FAIL;
    }

    return ESP_OK;
}

static esp_err_t desktop_request_return_home(void)
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
    if (lv_async_call(desktop_return_home_async, NULL) != LV_RES_OK)
    {
        s_app_select.app_switching = 0U;
        ESP_LOGE(TAG, "lv_async_call desktop_return_home_async failed");
        return ESP_FAIL;
    }

    return ESP_OK;
}

static void desktop_open_app_async(void *user_data)
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
        desktop_release_active_app_resources();
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
            desktop_create_ui();
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
    desktop_reset_icon_refs();
    s_app_select.app_switching = 0U;
}

static void desktop_return_home_async(void *user_data)
{
    lv_obj_t *app_screen;

    (void)user_data;

    app_screen = s_app_select.active_app_screen;
    desktop_release_active_app_resources();

    if ((app_screen != NULL) && lv_obj_is_valid(app_screen))
    {
        lv_obj_del_async(app_screen);
    }

    s_app_select.active_app_screen = NULL;
    s_app_select.active_app_idx = DESKTOP_ICON_COUNT;

    desktop_create_ui();
    s_app_select.app_switching = 0U;
}

static void desktop_reset_icon_refs(void)
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

static void desktop_create_ui(void)
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

    scr = lv_obj_create(NULL);
    s_app_select.desktop_screen = scr;

    grid_y = app_status_bar_content_top() + DESKTOP_GRID_TOP_GAP;
    grid_h = app_status_bar_content_bottom() - grid_y - DESKTOP_GRID_BOTTOM_GAP;
    if (grid_h < 80)
    {
        grid_h = 80;
    }

    lv_obj_set_style_bg_color(scr, lv_color_hex(0x0F172A), 0);
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
        selected_color = desktop_invert_color(s_desktop_icons[i].color);

        lv_obj_set_grid_cell(btn,
                             LV_GRID_ALIGN_STRETCH, col, 1,
                             LV_GRID_ALIGN_STRETCH, row, 1);
        lv_obj_set_style_bg_color(btn, s_desktop_icons[i].color, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_color(btn, selected_color, LV_PART_MAIN | LV_STATE_CHECKED);
        lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_bg_color(btn, s_desktop_icons[i].color, LV_PART_MAIN | LV_STATE_PRESSED);
        lv_obj_set_style_bg_color(btn, selected_color, LV_PART_MAIN | LV_STATE_CHECKED | LV_STATE_PRESSED);
        lv_obj_set_style_text_color(btn, lv_color_black(), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_color(btn, lv_color_white(), LV_PART_MAIN | LV_STATE_CHECKED);
        lv_obj_set_style_text_color(btn, lv_color_black(), LV_PART_MAIN | LV_STATE_PRESSED);
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
        lv_obj_add_event_cb(btn, desktop_icon_click_cb, LV_EVENT_CLICKED, (void *)&s_desktop_icons[i]);
        s_app_select.icon_btns[i] = btn;

        symbol = lv_label_create(btn);
        lv_label_set_text(symbol, s_desktop_icons[i].symbol);
        lv_obj_set_style_text_color(symbol, lv_color_black(), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_color(symbol, lv_color_white(), LV_PART_MAIN | LV_STATE_CHECKED);
        lv_obj_set_style_text_font(symbol, DESKTOP_FONT_ICON, 0);
        lv_obj_align(symbol, LV_ALIGN_TOP_MID, 0, 4);
        s_app_select.icon_symbols[i] = symbol;

        name = lv_label_create(btn);
        lv_label_set_text(name, s_desktop_icons[i].name);
        lv_obj_set_style_text_color(name, lv_color_black(), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_color(name, lv_color_white(), LV_PART_MAIN | LV_STATE_CHECKED);
        lv_obj_set_style_text_font(name, DESKTOP_FONT_ICON_NAME, 0);
        lv_obj_set_style_text_letter_space(name, 1, 0);
        lv_obj_align(name, LV_ALIGN_BOTTOM_MID, 0, -6);
        s_app_select.icon_names[i] = name;
    }
    desktop_reset_icon_refs();
    s_app_select.app_switching = 0U;
    lv_scr_load(scr);
}

static esp_err_t desktop_lvgl_init(void)
{
    esp_timer_create_args_t tick_timer_args = {
        .callback = lvgl_tick_cb,
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
    s_lv_disp_drv.flush_cb = lvgl_flush_cb;
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

static void desktop_lvgl_task(void *param)
{
    uint32_t wait_ms;
    TickType_t sleep_ticks;

    uint32_t start_time = 0;
    uint8_t scope_flag = 0;

    (void)param;

    while (1)
    {
        wait_ms = lv_timer_handler();
        if ((wait_ms == 0U) || (wait_ms > LVGL_TASK_PERIOD_MS))
        {
            wait_ms = LVGL_TASK_PERIOD_MS;
        }

        sleep_ticks = pdMS_TO_TICKS(wait_ms);
        if (sleep_ticks < 1)
        {
            sleep_ticks = 1;
        }

        vTaskDelay(sleep_ticks);

        if (scope_flag == 0)
        {
            start_time += wait_ms;
            if (start_time >= 3000)
            {
                (void)desktop_app_open_by_name("Scope");
                scope_flag = 1;
            }
        }
    }
}

static esp_err_t desktop_prepare_monitor(void)
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

esp_err_t desktop_app_open_by_index(uint32_t idx)
{
    return desktop_request_open_app(idx);
}

esp_err_t desktop_app_open_by_name(const char *name)
{
    uint32_t i;

    if (name == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    for (i = 0; i < DESKTOP_ICON_COUNT; i++)
    {
        if (strcmp(s_desktop_icons[i].name, name) == 0)
        {
            return desktop_request_open_app(i);
        }
    }

    return ESP_ERR_NOT_FOUND;
}

void desktop_app_return_to_home(void)
{
    (void)desktop_request_return_home();
}

esp_err_t desktop_app_start(void)
{
    esp_err_t ret;
    BaseType_t task_ok;

    ret = desktop_prepare_monitor();
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "desktop_prepare_monitor failed: %d", (int)ret);
        return ret;
    }

    ret = desktop_lvgl_init();
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "desktop_lvgl_init failed: %d", (int)ret);
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

    app_home_nav_set_callback(desktop_app_return_to_home);

    desktop_create_ui();

    task_ok = xTaskCreate(desktop_lvgl_task,
                          "desktop_lvgl",
                          6144,
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
