#include "desktop_app.h"

#include <material_symbols.h>

LV_FONT_DECLARE(DESKTOP_TEXT_FONT);
LV_FONT_DECLARE(DESKTOP_SYMBOL_FONT);
LV_FONT_DECLARE(lv_font_montserrat_14);

static const desktop_icon_s s_desktop_icons[DESKTOP_ICON_COUNT] = {
    {LV_SYMBOL_VIDEO, "Camera", 0xE95420},     {LV_SYMBOL_IMAGE, "Gallery", 0xD94B3D},
    {LV_SYMBOL_AUDIO, "Music", 0x77216F},      {LV_SYMBOL_LIST, "Scope", 0xF27C38},
    {LV_SYMBOL_WIFI, "WiFi", 0xC0563F},        {LV_SYMBOL_BLUETOOTH, "BT", 0xB65C2C},
    {LV_SYMBOL_FILE, "File", 0xE19A35},        {LV_SYMBOL_VOLUME_MAX, "Mic", 0x8F6745},
    {LV_SYMBOL_BELL, "PIDM", 0xC23B4A},        {LV_SYMBOL_REFRESH, "Tools", 0x8A3D5D},
    {LV_SYMBOL_SETTINGS, "Setting", 0xA8703A}, {LV_SYMBOL_WARNING, "About", 0x6F4A34},
};

static lv_display_t* s_lv_display;
static lv_color_t* s_lv_buf_1;
static lv_color_t* s_lv_buf_2;
static esp_timer_handle_t s_lv_tick_timer;
static TaskHandle_t s_lv_task_handle;
static uint16_t s_lcd_width;
static uint16_t s_lcd_height;
static lv_obj_t* s_desktop_screen;
static lv_obj_t* s_net_label;
static lv_obj_t* s_cpu_label;
static bool s_desktop_started;
static bool s_lvgl_ready;

/*
 * brief : Build desktop grid UI and load it as the active LVGL screen.
 * input : none.
 * output: none.
 * type  : private
 */
static void _desktop_create_ui(void) {
    static lv_coord_t col_dsc[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1),
                                   LV_GRID_TEMPLATE_LAST};
    static lv_coord_t row_dsc[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1),
                                   LV_GRID_TEMPLATE_LAST};

    lv_coord_t grid_x = (lv_coord_t)DESKTOP_MARGIN_X;
    lv_coord_t grid_y = (lv_coord_t)(DESKTOP_TOP_BAR_HEIGHT + DESKTOP_MARGIN_Y);
    lv_coord_t grid_w = (lv_coord_t)((int32_t)s_lcd_width - (2 * DESKTOP_MARGIN_X));
    lv_coord_t grid_h = (lv_coord_t)((int32_t)s_lcd_height - DESKTOP_TOP_BAR_HEIGHT -
                                     DESKTOP_BOTTOM_BAR_HEIGHT - (2 * DESKTOP_MARGIN_Y));

    if (grid_w < 0) {
        grid_w = 0;
    }
    if (grid_h < 0) {
        grid_h = 0;
    }

    lv_obj_t* scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x1C1B1A), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    lv_obj_t* top_bar = lv_obj_create(scr);
    lv_obj_set_size(top_bar, (lv_coord_t)s_lcd_width, (lv_coord_t)DESKTOP_TOP_BAR_HEIGHT);
    lv_obj_set_pos(top_bar, 0, 0);
    lv_obj_set_style_bg_color(top_bar, lv_color_hex(DESKTOP_TOOLBAR_COLOR_HEX), 0);
    lv_obj_set_style_bg_opa(top_bar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(top_bar, 0, 0);
    lv_obj_set_style_radius(top_bar, 0, 0);
    lv_obj_set_style_pad_all(top_bar, 0, 0);

    s_net_label = lv_label_create(top_bar);
    lv_obj_set_style_text_color(s_net_label, lv_color_black(), 0);
    lv_obj_set_style_text_font(s_net_label, &DESKTOP_SYMBOL_FONT, 0);
    lv_obj_align(s_net_label, LV_ALIGN_LEFT_MID, 4, 0);
    lv_label_set_text(s_net_label, MATERIAL_SYMBOLS_WIFI_OFF);

    s_cpu_label = lv_label_create(top_bar);
    lv_obj_set_style_text_color(s_cpu_label, lv_color_black(), 0);
    lv_obj_set_style_text_font(s_cpu_label, &CPU_LABEL_FONT, 0);
    lv_obj_set_width(s_cpu_label, (lv_coord_t)(s_lcd_width - 28U));
    lv_label_set_long_mode(s_cpu_label, LV_LABEL_LONG_CLIP);
    lv_obj_align(s_cpu_label, LV_ALIGN_LEFT_MID, 24, 0);
    lv_label_set_text(s_cpu_label, "c:--% | m:--% | p:--%");

    lv_obj_t* bottom_bar = lv_obj_create(scr);
    lv_obj_set_size(bottom_bar, (lv_coord_t)s_lcd_width, (lv_coord_t)DESKTOP_BOTTOM_BAR_HEIGHT);
    lv_obj_set_pos(bottom_bar, 0, (lv_coord_t)((int32_t)s_lcd_height - DESKTOP_BOTTOM_BAR_HEIGHT));
    lv_obj_set_style_bg_color(bottom_bar, lv_color_hex(DESKTOP_TOOLBAR_COLOR_HEX), 0);
    lv_obj_set_style_bg_opa(bottom_bar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(bottom_bar, 0, 0);
    lv_obj_set_style_radius(bottom_bar, 0, 0);
    lv_obj_set_style_pad_all(bottom_bar, 0, 0);

    lv_obj_t* grid = lv_obj_create(scr);
    lv_obj_set_size(grid, grid_w, grid_h);
    lv_obj_set_pos(grid, grid_x, grid_y);
    lv_obj_set_style_bg_opa(grid, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(grid, 0, 0);
    lv_obj_set_style_pad_all(grid, 0, 0);
    lv_obj_set_style_pad_row(grid, DESKTOP_ICON_GAP_Y, 0);
    lv_obj_set_style_pad_column(grid, DESKTOP_ICON_GAP_X, 0);
    lv_obj_set_grid_dsc_array(grid, col_dsc, row_dsc);

    for (uint32_t i = 0; i < DESKTOP_ICON_COUNT; i++) {
        lv_coord_t row = (lv_coord_t)(i / DESKTOP_ICON_COLS);
        lv_coord_t col = (lv_coord_t)(i % DESKTOP_ICON_COLS);

        lv_obj_t* btn = lv_btn_create(grid);
        lv_obj_set_grid_cell(btn, LV_GRID_ALIGN_STRETCH, col, 1, LV_GRID_ALIGN_STRETCH, row, 1);
        lv_obj_set_style_bg_color(btn, lv_color_hex(s_desktop_icons[i].color_hex), 0);
        lv_obj_set_style_radius(btn, 12, 0);
        lv_obj_set_style_border_width(btn, 0, 0);
        lv_obj_set_style_pad_all(btn, 6, 0);

        lv_obj_t* symbol = lv_label_create(btn);
        lv_label_set_text(symbol, s_desktop_icons[i].symbol);
        lv_obj_set_style_text_color(symbol, lv_color_white(), 0);
        lv_obj_align(symbol, LV_ALIGN_TOP_MID, 0, 2);

        lv_obj_t* name = lv_label_create(btn);
        lv_label_set_text(name, s_desktop_icons[i].name);
        lv_obj_set_style_text_color(name, lv_color_white(), 0);
        lv_obj_set_style_text_font(name, &DESKTOP_TEXT_FONT, 0);
        lv_obj_align(name, LV_ALIGN_BOTTOM_MID, 0, -4);
    }

    s_desktop_screen = scr;
    lv_scr_load(scr);
}

/*
 * brief : Initialize ST7365 panel and cache effective resolution values.
 * input : none.
 * output: ESP_OK on success; otherwise panel initialization/configuration error.
 * type  : private
 */
static esp_err_t _desktop_prepare_monitor(void) {
    st7365p_cfg_t panel_cfg;
    st7365p_get_default_cfg(&panel_cfg);

    esp_err_t ret = st7365p_panel_init(&panel_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(DESKTOP_APP_TAG, "st7365p_panel_init failed: %d", (int)ret);
        return ret;
    }

    ret = st7365p_set_rotation(2);
    if (ret != ESP_OK) {
        ESP_LOGE(DESKTOP_APP_TAG, "st7365p_set_rotation failed: %d", (int)ret);
        return ret;
    }

    st7365p_get_resolution(&s_lcd_width, &s_lcd_height);
    if ((s_lcd_width == 0U) || (s_lcd_height == 0U)) {
        ESP_LOGE(DESKTOP_APP_TAG, "invalid LCD resolution");
        return ESP_ERR_INVALID_SIZE;
    }

    ESP_LOGI(DESKTOP_APP_TAG, "desktop init on %ux%u", (unsigned)s_lcd_width,
             (unsigned)s_lcd_height);
    return ESP_OK;
}

/*
 * brief : Initialize LVGL core, display driver, draw buffer, and tick timer.
 * input : none.
 * output: ESP_OK on success; otherwise startup error.
 * type  : private
 */
static esp_err_t _desktop_lvgl_init(void) {
    if (s_lvgl_ready) {
        return ESP_OK;
    }

    lv_init();

    size_t draw_buf_pixels = (size_t)s_lcd_width * LVGL_DRAW_BUF_LINES;
    s_lv_buf_1 = (lv_color_t*)heap_caps_malloc(draw_buf_pixels * sizeof(lv_color_t),
                                               MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (s_lv_buf_1 == NULL) {
        ESP_LOGE(DESKTOP_APP_TAG, "LVGL buf1 PSRAM allocation failed");
        return ESP_ERR_NO_MEM;
    }

    s_lv_buf_2 = (lv_color_t*)heap_caps_malloc(draw_buf_pixels * sizeof(lv_color_t),
                                               MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (s_lv_buf_2 == NULL) {
        ESP_LOGE(DESKTOP_APP_TAG, "LVGL buf2 PSRAM allocation failed");
        heap_caps_free(s_lv_buf_1);
        s_lv_buf_1 = NULL;
        return ESP_ERR_NO_MEM;
    }

    s_lv_display = lv_display_create((int32_t)s_lcd_width, (int32_t)s_lcd_height);
    if (s_lv_display == NULL) {
        heap_caps_free(s_lv_buf_2);
        heap_caps_free(s_lv_buf_1);
        s_lv_buf_2 = NULL;
        s_lv_buf_1 = NULL;
        return ESP_FAIL;
    }

    lv_display_set_color_format(s_lv_display, LV_COLOR_FORMAT_RGB565_SWAPPED);
    lv_display_set_flush_cb(s_lv_display, desktop_flush_event);
    lv_display_set_buffers(s_lv_display, s_lv_buf_1, s_lv_buf_2,
                           draw_buf_pixels * sizeof(lv_color_t), LV_DISPLAY_RENDER_MODE_PARTIAL);

    esp_timer_create_args_t tick_timer_args = {
        .callback = desktop_tick_event,
        .arg = NULL,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "desktop_lvgl_tick",
    };

    esp_err_t ret = esp_timer_create(&tick_timer_args, &s_lv_tick_timer);
    if (ret != ESP_OK) {
        heap_caps_free(s_lv_buf_2);
        heap_caps_free(s_lv_buf_1);
        s_lv_buf_2 = NULL;
        s_lv_buf_1 = NULL;
        return ret;
    }

    ret = esp_timer_start_periodic(s_lv_tick_timer, LVGL_TICK_PERIOD_MS * 1000U);
    if (ret != ESP_OK) {
        heap_caps_free(s_lv_buf_2);
        heap_caps_free(s_lv_buf_1);
        s_lv_buf_2 = NULL;
        s_lv_buf_1 = NULL;
        return ret;
    }

    s_lvgl_ready = true;
    return ESP_OK;
}

/*
 * brief : Main desktop LVGL task loop.
 * input : param - unused task parameter.
 * output: none.
 * type  : private
 */
static void _desktop_lvgl_task(void* param) {
    (void)param;

    while (1) {
        lv_timer_handler();
        vTaskDelay(pdMS_TO_TICKS(LVGL_TASK_PERIOD_MS));
    }
}

/*
 * brief : Get the desktop top-bar CPU text label handle.
 * input : none.
 * output: Valid label handle, or NULL when unavailable.
 * type  : public
 */
lv_obj_t* desktop_get_cpu_label(void) {
    if ((s_cpu_label != NULL) && lv_obj_is_valid(s_cpu_label)) {
        return s_cpu_label;
    }
    return NULL;
}

/*
 * brief : Get the desktop top-bar network icon label handle.
 * input : none.
 * output: Valid label handle, or NULL when unavailable.
 * type  : public
 */
lv_obj_t* desktop_get_net_label(void) {
    if ((s_net_label != NULL) && lv_obj_is_valid(s_net_label)) {
        return s_net_label;
    }
    return NULL;
}

/*
 * brief : Public entry to request returning to desktop.
 * input : none.
 * output: none.
 * type  : public
 */
void desktop_return_to_home(void) {
    if (!s_lvgl_ready) {
        return;
    }

    if ((s_desktop_screen != NULL) && lv_obj_is_valid(s_desktop_screen)) {
        lv_scr_load(s_desktop_screen);
    }
}

/*
 * brief : Start desktop subsystem including panel, LVGL, and task loop.
 * input : none.
 * output: ESP_OK on success; otherwise propagated startup error.
 * type  : public
 */
esp_err_t desktop_start(void) {
#if 1
    if (s_desktop_started) {
        ESP_LOGI(DESKTOP_APP_TAG, "desktop already started");
        return ESP_OK;
    }

    esp_err_t ret = _desktop_prepare_monitor();
    if (ret != ESP_OK) {
        return ret;
    }

    ret = _desktop_lvgl_init();
    if (ret != ESP_OK) {
        return ret;
    }

    _desktop_create_ui();

    BaseType_t task_ok =
        xTaskCreate(_desktop_lvgl_task, "desktop_lvgl", 10240, NULL, 5, &s_lv_task_handle);
    if (task_ok != pdPASS) {
        ESP_LOGE(DESKTOP_APP_TAG, "xTaskCreate desktop_lvgl failed");
        return ESP_FAIL;
    }

    s_desktop_started = true;
#else
    esp_err_t ret = _desktop_prepare_monitor();
    if (ret != ESP_OK) {
        return ret;
    }

    ret = st7365p_set_window(0, 0, (uint16_t)(s_lcd_width - 1U), (uint16_t)(s_lcd_height - 1U));
    if (ret != ESP_OK) {
        ESP_LOGE(DESKTOP_APP_TAG, "st7365p_set_window failed: %d", (int)ret);
        return ret;
    }

    ret = st7365p_fill_color(RGB565_RED, (uint32_t)s_lcd_width * (uint32_t)s_lcd_height);
    if (ret != ESP_OK) {
        ESP_LOGE(DESKTOP_APP_TAG, "st7365p_fill_color failed: %d", (int)ret);
        return ret;
    }
#endif
    return ESP_OK;
}
