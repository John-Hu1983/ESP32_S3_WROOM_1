#include "desktop_app.h"

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "lvgl.h"

#include "peripherals/st7365p.h"

#define TAG "DESKTOP"

#define LVGL_TICK_PERIOD_MS 2U
#define LVGL_TASK_PERIOD_MS 5U
#define LVGL_DRAW_BUF_LINES 40U

#define DESKTOP_ICON_COLS 3U
#define DESKTOP_ICON_ROWS 4U
#define DESKTOP_ICON_COUNT (DESKTOP_ICON_COLS * DESKTOP_ICON_ROWS)

#define DESKTOP_MARGIN_X 12
#define DESKTOP_TOP_Y 58
#define DESKTOP_BOTTOM_MARGIN 42
#define DESKTOP_ICON_GAP_X 10
#define DESKTOP_ICON_GAP_Y 10

#if LV_FONT_MONTSERRAT_22
#define DESKTOP_FONT_TITLE (&lv_font_montserrat_22)
#define DESKTOP_FONT_ICON (&lv_font_montserrat_22)
#else
#define DESKTOP_FONT_TITLE LV_FONT_DEFAULT
#define DESKTOP_FONT_ICON LV_FONT_DEFAULT
#endif

#if LV_FONT_MONTSERRAT_18
#define DESKTOP_FONT_TEXT (&lv_font_montserrat_18)
#else
#define DESKTOP_FONT_TEXT LV_FONT_DEFAULT
#endif

typedef struct
{
    const char *symbol;
    const char *name;
    lv_color_t color;
} desktop_icon_s;

static const desktop_icon_s s_desktop_icons[DESKTOP_ICON_COUNT] = {
    {LV_SYMBOL_HOME, "Home", LV_COLOR_MAKE(0x2D, 0x5B, 0xFF)},
    {LV_SYMBOL_SETTINGS, "Settings", LV_COLOR_MAKE(0x00, 0xA8, 0x78)},
    {LV_SYMBOL_IMAGE, "Gallery", LV_COLOR_MAKE(0xEB, 0x4D, 0x8A)},
    {LV_SYMBOL_AUDIO, "Music", LV_COLOR_MAKE(0x6D, 0x5D, 0xF6)},
    {LV_SYMBOL_VIDEO, "Video", LV_COLOR_MAKE(0x00, 0xA1, 0xD6)},
    {LV_SYMBOL_WIFI, "WiFi", LV_COLOR_MAKE(0x22, 0xB0, 0x7D)},
    {LV_SYMBOL_BLUETOOTH, "BT", LV_COLOR_MAKE(0x1E, 0x90, 0xFF)},
    {LV_SYMBOL_SD_CARD, "Storage", LV_COLOR_MAKE(0xFF, 0x8A, 0x00)},
    {LV_SYMBOL_BATTERY_FULL, "Battery", LV_COLOR_MAKE(0x5D, 0x66, 0x7A)},
    {LV_SYMBOL_BELL, "Alerts", LV_COLOR_MAKE(0xD2, 0x4D, 0x57)},
    {LV_SYMBOL_REFRESH, "Tools", LV_COLOR_MAKE(0x7A, 0x4D, 0xD8)},
    {LV_SYMBOL_POWER, "Power", LV_COLOR_MAKE(0x1F, 0x29, 0x37)},
};

static lv_disp_draw_buf_t s_lv_draw_buf;
static lv_disp_drv_t s_lv_disp_drv;
static lv_color_t *s_lv_buf_1 = NULL;
static esp_timer_handle_t s_lv_tick_timer = NULL;
static lv_obj_t *s_status_label = NULL;
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

    if ((icon == NULL) || (s_status_label == NULL))
    {
        return;
    }

    lv_label_set_text_fmt(s_status_label, "%s opened", icon->name);
}

static void desktop_create_ui(void)
{
    static lv_coord_t col_dsc[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
    static lv_coord_t row_dsc[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
    lv_obj_t *scr = lv_scr_act();
    lv_obj_t *title;
    lv_obj_t *subtitle;
    lv_obj_t *grid;
    lv_coord_t grid_h;
    lv_coord_t i;

    grid_h = (lv_coord_t)s_lcd_height - DESKTOP_TOP_Y - DESKTOP_BOTTOM_MARGIN;
    if (grid_h < 120)
    {
        grid_h = 120;
    }

    lv_obj_set_style_bg_color(scr, lv_color_hex(0x0F172A), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    title = lv_label_create(scr);
    lv_label_set_text(title, "LVGL Desktop");
    lv_obj_set_style_text_color(title, lv_color_hex(0xE2E8F0), 0);
    lv_obj_set_style_text_font(title, DESKTOP_FONT_TITLE, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 8);

    subtitle = lv_label_create(scr);
    lv_label_set_text(subtitle, "Portrait 3x4 Launcher");
    lv_obj_set_style_text_color(subtitle, lv_color_hex(0x94A3B8), 0);
    lv_obj_set_style_text_font(subtitle, DESKTOP_FONT_TEXT, 0);
    lv_obj_align_to(subtitle, title, LV_ALIGN_OUT_BOTTOM_MID, 0, 4);

    grid = lv_obj_create(scr);
    lv_obj_set_size(grid, (lv_coord_t)s_lcd_width - (2 * DESKTOP_MARGIN_X), grid_h);
    lv_obj_set_pos(grid, DESKTOP_MARGIN_X, DESKTOP_TOP_Y);
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
        lv_obj_set_style_shadow_color(btn, lv_color_hex(0x020617), 0);
        lv_obj_set_style_shadow_width(btn, 10, 0);
        lv_obj_set_style_shadow_opa(btn, LV_OPA_30, 0);
        lv_obj_set_style_pad_all(btn, 6, 0);
        lv_obj_add_event_cb(btn, desktop_icon_click_cb, LV_EVENT_CLICKED, (void *)&s_desktop_icons[i]);

        symbol = lv_label_create(btn);
        lv_label_set_text(symbol, s_desktop_icons[i].symbol);
        lv_obj_set_style_text_color(symbol, lv_color_hex(0xF8FAFC), 0);
        lv_obj_set_style_text_font(symbol, DESKTOP_FONT_ICON, 0);
        lv_obj_align(symbol, LV_ALIGN_TOP_MID, 0, 4);

        name = lv_label_create(btn);
        lv_label_set_text(name, s_desktop_icons[i].name);
        lv_obj_set_style_text_color(name, lv_color_hex(0xF8FAFC), 0);
        lv_obj_set_style_text_font(name, DESKTOP_FONT_TEXT, 0);
        lv_obj_align(name, LV_ALIGN_BOTTOM_MID, 0, -6);
    }

    s_status_label = lv_label_create(scr);
    lv_label_set_text(s_status_label, "Desktop ready");
    lv_obj_set_style_text_color(s_status_label, lv_color_hex(0xCBD5E1), 0);
    lv_obj_set_style_text_font(s_status_label, DESKTOP_FONT_TEXT, 0);
    lv_obj_align(s_status_label, LV_ALIGN_BOTTOM_MID, 0, -10);
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

esp_err_t desktop_app_start(void)
{
    esp_err_t ret;
    BaseType_t task_ok;

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
