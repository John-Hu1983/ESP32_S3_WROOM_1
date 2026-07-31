#include "desktop_app.h"

#define TAG "DESKTOP"

static const desktop_icon_s s_desktop_icons[DESKTOP_ICON_COUNT] = {
    {LV_SYMBOL_VOLUME_MID, "Camera", LV_COLOR_MAKE(0x2D, 0x5B, 0xFF)},
    {LV_SYMBOL_SETTINGS, "Setting", LV_COLOR_MAKE(0x00, 0xA8, 0x78)},
    {LV_SYMBOL_IMAGE, "Gallery", LV_COLOR_MAKE(0xEB, 0x4D, 0x8A)},
    {LV_SYMBOL_AUDIO, "Music", LV_COLOR_MAKE(0x6D, 0x5D, 0xF6)},
    {LV_SYMBOL_VIDEO, "Video", LV_COLOR_MAKE(0x00, 0xA1, 0xD6)},
    {LV_SYMBOL_WIFI, "WiFi", LV_COLOR_MAKE(0x22, 0xB0, 0x7D)},
    {LV_SYMBOL_BLUETOOTH, "BT", LV_COLOR_MAKE(0x1E, 0x90, 0xFF)},
    {LV_SYMBOL_SD_CARD, "SD", LV_COLOR_MAKE(0xFF, 0x8A, 0x00)},
    {LV_SYMBOL_BATTERY_FULL, "Battery", LV_COLOR_MAKE(0x5D, 0x66, 0x7A)},
    {LV_SYMBOL_BELL, "Alerts", LV_COLOR_MAKE(0xD2, 0x4D, 0x57)},
    {LV_SYMBOL_REFRESH, "Tools", LV_COLOR_MAKE(0x7A, 0x4D, 0xD8)},
    {LV_SYMBOL_POWER, "Power", LV_COLOR_MAKE(0x1F, 0x29, 0x37)},
};

static lv_disp_draw_buf_t s_lv_draw_buf;
static lv_disp_drv_t s_lv_disp_drv;
static lv_color_t *s_lv_buf_1 = NULL;
static esp_timer_handle_t s_lv_tick_timer = NULL;
static lv_obj_t *s_clock_label = NULL;
static lv_timer_t *s_clock_timer = NULL;
static uint16_t s_lcd_width = 0;
static uint16_t s_lcd_height = 0;

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

    if (icon == NULL)
    {
        return;
    }

    ESP_LOGI(TAG, "%s opened", icon->name);
}

static void desktop_update_clock_text(void)
{
    char clock_text[6];
    time_t now = time(NULL);
    struct tm local_tm;

    if ((now > 0) && (localtime_r(&now, &local_tm) != NULL) && (local_tm.tm_year >= (2020 - 1900)))
    {
        (void)snprintf(clock_text, sizeof(clock_text), "%02d:%02d", local_tm.tm_hour, local_tm.tm_min);
    }
    else
    {
        uint64_t uptime_min = (uint64_t)(esp_timer_get_time() / 1000000ULL) / 60ULL;
        uint32_t hour = (uint32_t)((uptime_min / 60ULL) % 24ULL);
        uint32_t minute = (uint32_t)(uptime_min % 60ULL);
        (void)snprintf(clock_text, sizeof(clock_text), "%02u:%02u", (unsigned)hour, (unsigned)minute);
    }

    lv_label_set_text(s_clock_label, clock_text);
}

static void desktop_clock_timer_cb(lv_timer_t *timer)
{
    (void)timer;

    if (s_clock_label == NULL)
    {
        return;
    }

    desktop_update_clock_text();
}

static void desktop_create_ui(void)
{
    static lv_coord_t col_dsc[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
    static lv_coord_t row_dsc[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
    lv_obj_t *scr = lv_scr_act();
    lv_obj_t *top_bar;
    lv_obj_t *bottom_bar;
    lv_obj_t *net_symbol;
    lv_obj_t *grid;
    lv_coord_t grid_y;
    lv_coord_t grid_h;
    lv_coord_t i;

    grid_y = DESKTOP_BAR_HEIGHT + DESKTOP_GRID_TOP_GAP;
    grid_h = (lv_coord_t)s_lcd_height - grid_y - DESKTOP_BAR_HEIGHT - DESKTOP_GRID_BOTTOM_GAP;
    if (grid_h < 80)
    {
        grid_h = 80;
    }

    lv_obj_set_style_bg_color(scr, lv_color_hex(0x0F172A), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    top_bar = lv_obj_create(scr);
    lv_obj_set_size(top_bar, (lv_coord_t)s_lcd_width, DESKTOP_BAR_HEIGHT);
    lv_obj_align(top_bar, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(top_bar, lv_color_hex(0x1D4ED8), 0);
    lv_obj_set_style_bg_opa(top_bar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(top_bar, 0, 0);
    lv_obj_set_style_radius(top_bar, 0, 0);
    lv_obj_set_style_pad_all(top_bar, 0, 0);

    net_symbol = lv_label_create(top_bar);
    lv_label_set_text(net_symbol, LV_SYMBOL_WIFI);
    lv_obj_set_style_text_color(net_symbol, lv_color_hex(0xF8FAFC), 0);
    lv_obj_set_style_text_font(net_symbol, DESKTOP_FONT_TEXT, 0);
    lv_obj_align(net_symbol, LV_ALIGN_RIGHT_MID, -8, 0);

    bottom_bar = lv_obj_create(scr);
    lv_obj_set_size(bottom_bar, (lv_coord_t)s_lcd_width, DESKTOP_BAR_HEIGHT);
    lv_obj_align(bottom_bar, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(bottom_bar, lv_color_hex(0x1D4ED8), 0);
    lv_obj_set_style_bg_opa(bottom_bar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(bottom_bar, 0, 0);
    lv_obj_set_style_radius(bottom_bar, 0, 0);
    lv_obj_set_style_pad_all(bottom_bar, 0, 0);

    s_clock_label = lv_label_create(bottom_bar);
    lv_obj_set_style_text_color(s_clock_label, lv_color_hex(0xF8FAFC), 0);
    lv_obj_set_style_text_font(s_clock_label, DESKTOP_FONT_TEXT, 0);
    lv_obj_align(s_clock_label, LV_ALIGN_CENTER, 0, 0);
    desktop_update_clock_text();

    if (s_clock_timer != NULL)
    {
        lv_timer_del(s_clock_timer);
    }
    s_clock_timer = lv_timer_create(desktop_clock_timer_cb, 1000, NULL);

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
        lv_coord_t row = i / DESKTOP_ICON_COLS;
        lv_coord_t col = i % DESKTOP_ICON_COLS;

        btn = lv_btn_create(grid);
        lv_obj_set_grid_cell(btn,
                             LV_GRID_ALIGN_STRETCH, col, 1,
                             LV_GRID_ALIGN_STRETCH, row, 1);
        lv_obj_set_style_bg_color(btn, s_desktop_icons[i].color, 0);
        lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(btn, 0, 0);
        lv_obj_set_style_radius(btn, 14, 0);
        lv_obj_set_style_shadow_width(btn, 0, 0);
        lv_obj_set_style_shadow_opa(btn, LV_OPA_TRANSP, 0);
        lv_obj_set_style_pad_all(btn, 6, 0);
        lv_obj_add_event_cb(btn, desktop_icon_click_cb, LV_EVENT_CLICKED, (void *)&s_desktop_icons[i]);

        symbol = lv_label_create(btn);
        lv_label_set_text(symbol, s_desktop_icons[i].symbol);
        lv_obj_set_style_text_color(symbol, lv_color_black(), LV_PART_MAIN | LV_STATE_ANY);
        lv_obj_set_style_text_font(symbol, DESKTOP_FONT_ICON, 0);
        lv_obj_align(symbol, LV_ALIGN_TOP_MID, 0, 4);

        name = lv_label_create(btn);
        lv_label_set_text(name, s_desktop_icons[i].name);
        lv_obj_set_style_text_color(name, lv_color_black(), LV_PART_MAIN | LV_STATE_ANY);
        lv_obj_set_style_text_font(name, DESKTOP_FONT_ICON_NAME, 0);
        lv_obj_set_style_text_letter_space(name, 1, 0);
        lv_obj_align(name, LV_ALIGN_BOTTOM_MID, 0, -6);
    }
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
