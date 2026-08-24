#include "desktop_app.h"

#define TAG "desktop"

#define DESKTOP_MARGIN_X 12
#define DESKTOP_MARGIN_Y 12
#define DESKTOP_ICON_GAP_X 10
#define DESKTOP_ICON_GAP_Y 10

typedef struct {
    const char* symbol;
    const char* name;
    uint32_t color_hex;
} desktop_icon_s;

static const desktop_icon_s s_desktop_icons[DESKTOP_ICON_COUNT] = {
    {LV_SYMBOL_VIDEO, "Camera", 0xE95420},
    {LV_SYMBOL_IMAGE, "Gallery", 0xD94B3D},
    {LV_SYMBOL_AUDIO, "Music", 0x77216F},
    {LV_SYMBOL_LIST, "Scope", 0xF27C38},
    {LV_SYMBOL_WIFI, "WiFi", 0xC0563F},
    {LV_SYMBOL_BLUETOOTH, "BT", 0xB65C2C},
    {LV_SYMBOL_FILE, "File", 0xE19A35},
    {LV_SYMBOL_VOLUME_MAX, "Mic", 0x8F6745},
    {LV_SYMBOL_BELL, "PIDM", 0xC23B4A},
    {LV_SYMBOL_REFRESH, "Tools", 0x8A3D5D},
    {LV_SYMBOL_SETTINGS, "Setting", 0xA8703A},
    {LV_SYMBOL_POWER, "Power", 0x6F4A34},
};

static lv_display_t* s_lv_display;
static lv_color_t* s_lv_buf_1;
static lv_color_t* s_lv_buf_2;
static esp_timer_handle_t s_lv_tick_timer;
static TaskHandle_t s_lv_task_handle;
static uint16_t s_lcd_width;
static uint16_t s_lcd_height;
static lv_obj_t* s_desktop_screen;
static bool s_desktop_started;
static bool s_lvgl_ready;

/*
 * brief: Build desktop grid UI and load it as the active LVGL screen.
 * input: none.
 * output: none.
 */
static void _desktop_create_ui(void) {
    static lv_coord_t col_dsc[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
    static lv_coord_t row_dsc[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};

    lv_obj_t* scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x1C1B1A), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    lv_obj_t* title = lv_label_create(scr);
    lv_label_set_text(title, "Desktop");
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_set_style_text_font(title, LV_FONT_DEFAULT, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 4);

    lv_obj_t* grid = lv_obj_create(scr);
    lv_obj_set_size(grid, (lv_coord_t)s_lcd_width - (2 * DESKTOP_MARGIN_X),
                    (lv_coord_t)s_lcd_height - (2 * DESKTOP_MARGIN_Y) - 20);
    lv_obj_set_pos(grid, DESKTOP_MARGIN_X, DESKTOP_MARGIN_Y + 18);
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
        lv_obj_set_grid_cell(btn,
                             LV_GRID_ALIGN_STRETCH,
                             col,
                             1,
                             LV_GRID_ALIGN_STRETCH,
                             row,
                             1);
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
        lv_obj_align(name, LV_ALIGN_BOTTOM_MID, 0, -4);
    }

    s_desktop_screen = scr;
    lv_scr_load(scr);
}

/*
 * brief: Initialize ST7365 panel and cache effective resolution values.
 * input: none.
 * output: ESP_OK on success; otherwise panel initialization/configuration error.
 */
static esp_err_t _desktop_prepare_monitor(void) {
    esp_err_t ret = st7365p_panel_init(NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "st7365p_panel_init failed: %d", (int)ret);
        return ret;
    }

    ret = st7365p_set_rotation(2);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "st7365p_set_rotation failed: %d", (int)ret);
        return ret;
    }

    st7365p_get_resolution(&s_lcd_width, &s_lcd_height);
    if ((s_lcd_width == 0U) || (s_lcd_height == 0U)) {
        ESP_LOGE(TAG, "invalid LCD resolution");
        return ESP_ERR_INVALID_SIZE;
    }

    ESP_LOGI(TAG, "desktop init on %ux%u", (unsigned)s_lcd_width, (unsigned)s_lcd_height);
    return ESP_OK;
}

/*
 * brief: Initialize LVGL core, display driver, draw buffer, and tick timer.
 * input: none.
 * output: ESP_OK on success; otherwise startup error.
 */
static esp_err_t _desktop_lvgl_init(void) {
    if (s_lvgl_ready) {
        return ESP_OK;
    }

    lv_init();

    size_t draw_buf_pixels = (size_t)s_lcd_width * LVGL_DRAW_BUF_LINES;
    s_lv_buf_1 = (lv_color_t*)heap_caps_malloc(draw_buf_pixels * sizeof(lv_color_t),
                                                MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (s_lv_buf_1 == NULL) {
        return ESP_ERR_NO_MEM;
    }

    s_lv_buf_2 = (lv_color_t*)heap_caps_malloc(draw_buf_pixels * sizeof(lv_color_t),
                                                MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (s_lv_buf_2 == NULL) {
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

    lv_display_set_color_format(s_lv_display, LV_COLOR_FORMAT_RGB565);
    lv_display_set_flush_cb(s_lv_display, desktop_common_lvgl_flush_cb);
    lv_display_set_buffers(s_lv_display,
                           s_lv_buf_1,
                           s_lv_buf_2,
                           draw_buf_pixels * sizeof(lv_color_t),
                           LV_DISPLAY_RENDER_MODE_PARTIAL);

    esp_timer_create_args_t tick_timer_args = {
        .callback = desktop_common_lvgl_tick_cb,
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
 * brief: Main desktop LVGL task loop.
 * input: param - unused task parameter.
 * output: none.
 */
static void _desktop_lvgl_task(void* param) {
    (void)param;

    while (1) {
        lv_timer_handler();
        vTaskDelay(pdMS_TO_TICKS(LVGL_TASK_PERIOD_MS));
    }
}

/*
 * brief: Public entry to request returning to desktop.
 * input: none.
 * output: none.
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
 * brief: Start desktop subsystem including panel, LVGL, and task loop.
 * input: none.
 * output: ESP_OK on success; otherwise propagated startup error.
 */
esp_err_t desktop_app_start(void) {
    if (s_desktop_started) {
        ESP_LOGI(TAG, "desktop already started");
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

    BaseType_t task_ok = xTaskCreate(_desktop_lvgl_task, "desktop_lvgl", 10240, NULL, 5,
                                     &s_lv_task_handle);
    if (task_ok != pdPASS) {
        ESP_LOGE(TAG, "xTaskCreate desktop_lvgl failed");
        return ESP_FAIL;
    }

    s_desktop_started = true;
    return ESP_OK;
}
