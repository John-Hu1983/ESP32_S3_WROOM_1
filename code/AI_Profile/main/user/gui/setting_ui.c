#include "setting_ui.h"

#define TAG "setting_ui"

typedef struct {
    const char* city_name;
    const char* latitude;
    const char* longitude;
} setting_city_item_s;

static const setting_city_item_s s_setting_city_items[] = {
    { "Dongguan", "23.0207", "113.7518" },
    { "Shenzhen", "22.5431", "114.0579" },
    { "Guangzhou", "23.1291", "113.2644" },
    { "Shanghai", "31.2304", "121.4737" },
    { "Beijing", "39.9042", "116.4074" },
};

static setting_ui_runtime_s s_setting_runtime;

/*
 * brief : _setting_obj_valid.
 * input : see parameters.
 * output: return value from this function.
 * type  : private
 */
static bool _setting_obj_valid(lv_obj_t* obj) {
    return (obj != NULL) && lv_obj_is_valid(obj);
}

/*
 * brief : _setting_city_count.
 * input : none.
 * output: return value from this function.
 * type  : private
 */
static uint8_t _setting_city_count(void) {
    return (uint8_t)(sizeof(s_setting_city_items) / sizeof(s_setting_city_items[0]));
}

/*
 * brief : _setting_find_city_index.
 * input : see parameters.
 * output: return value from this function.
 * type  : private
 */
static uint8_t _setting_find_city_index(const char* city_label,
                                        const char* latitude,
                                        const char* longitude) {
    uint8_t idx = 0U;
    uint8_t count = _setting_city_count();

    for (idx = 0U; idx < count; idx++) {
        const setting_city_item_s* city = &s_setting_city_items[idx];

        if ((city_label != NULL) && (strcmp(city->city_name, city_label) == 0)) {
            return idx;
        }

        if ((latitude != NULL) && (longitude != NULL)
            && (strcmp(city->latitude, latitude) == 0)
            && (strcmp(city->longitude, longitude) == 0)) {
            return idx;
        }
    }

    return 0U;
}

/*
 * brief : _setting_show_selected_city.
 * input : see parameters.
 * output: none.
 * type  : private
 */
static void _setting_show_selected_city(setting_ui_runtime_s* runtime) {
    uint8_t count = _setting_city_count();
    char text[48];
    const setting_city_item_s* city = NULL;

    if (runtime == NULL || count == 0U) {
        return;
    }

    if (runtime->city_index >= count) {
        runtime->city_index = 0U;
    }

    city = &s_setting_city_items[runtime->city_index];
    snprintf(text, sizeof(text), "City: %s", city->city_name);

    if (_setting_obj_valid(runtime->city_value_lab)) {
        lv_label_set_text(runtime->city_value_lab, text);
    }
}

/*
 * brief : _setting_show_hint.
 * input : see parameters.
 * output: none.
 * type  : private
 */
static void _setting_show_hint(setting_ui_runtime_s* runtime, const char* hint) {
    if ((runtime == NULL) || (hint == NULL)) {
        return;
    }

    if (_setting_obj_valid(runtime->hint_lab)) {
        lv_label_set_text(runtime->hint_lab, hint);
    }
}

/*
 * brief : _setting_ui_task.
 * input : see parameters.
 * output: none.
 * type  : private
 */
static void _setting_ui_task(void* param) {
    setting_ui_runtime_s* runtime = (setting_ui_runtime_s*)param;
    btn_status_e btn_val = Btn_Idle;
    uint8_t city_count = _setting_city_count();
    const setting_city_item_s* city = NULL;

    while (1) {
        btn_val = button_scan_state(&runtime->button_scan, SETTING_UI_TASK_PERIOD_MS);

        if ((btn_val == Btn_Up_Click) && (city_count > 0U)) {
            runtime->city_index =
                (runtime->city_index == 0U) ? (uint8_t)(city_count - 1U)
                                            : (uint8_t)(runtime->city_index - 1U);
            _setting_show_selected_city(runtime);
            _setting_show_hint(runtime, "UP/DOWN: Select  BOTH: Save+Back");
        }
        else if ((btn_val == Btn_Down_Click) && (city_count > 0U)) {
            runtime->city_index = (uint8_t)((runtime->city_index + 1U) % city_count);
            _setting_show_selected_city(runtime);
            _setting_show_hint(runtime, "UP/DOWN: Select  BOTH: Save+Back");
        }
        else if (btn_val == Btn_Both_Click) {
            if (city_count == 0U) {
                _setting_show_hint(runtime, "No city options");
            }
            else {
                city = &s_setting_city_items[runtime->city_index];
                if (desktop_set_weather_location(city->city_name,
                                                 city->latitude,
                                                 city->longitude)) {
                    _setting_show_hint(runtime, "Saved, returning...");
                    if (runtime->home_cb != NULL) {
                        runtime->home_cb(runtime->home_user_ctx);
                    }
                }
                else {
                    _setting_show_hint(runtime, "Save failed");
                }
            }
        }

        delay_ms(SETTING_UI_TASK_PERIOD_MS);
    }
}

/*
 * brief : Create the setting page root screen object.
 * input : lcd_w - LCD width; lcd_h - LCD height.
 * output: Created LVGL screen object.
 * type  : public
 */
lv_obj_t* config_open_screen(lv_obj_t* parent, lv_coord_t area_w, lv_coord_t area_h,
                                ui_menu_home_cb_t home_cb, void* home_user_ctx) {
    lv_obj_t* screen = NULL;
    lv_obj_t* title_lab = NULL;
    lv_obj_t* card = NULL;
    lv_obj_t* key_lab = NULL;
    lv_coord_t card_w = 0;
    lv_coord_t card_h = 0;
    char city_label[DESKTOP_WEATHER_CITY_LABEL_LEN];
    char latitude[DESKTOP_WEATHER_COORD_LEN];
    char longitude[DESKTOP_WEATHER_COORD_LEN];
    bool has_saved_city = false;
    BaseType_t task_ok = pdFAIL;

    if (parent == NULL) {
        return NULL;
    }

    if (s_setting_runtime.task_handle != NULL) {
        vTaskDelete(s_setting_runtime.task_handle);
        s_setting_runtime.task_handle = NULL;
    }

    memset(&s_setting_runtime, 0, sizeof(s_setting_runtime));

    s_setting_runtime.home_cb = home_cb;
    s_setting_runtime.home_user_ctx = home_user_ctx;
    s_setting_runtime.button_scan = (btn_scan_s){ 0 };
    s_setting_runtime.city_index = 0U;

    has_saved_city = desktop_get_weather_location(city_label,
                                                  sizeof(city_label),
                                                  latitude,
                                                  sizeof(latitude),
                                                  longitude,
                                                  sizeof(longitude));
    if (has_saved_city) {
        s_setting_runtime.city_index =
            _setting_find_city_index(city_label, latitude, longitude);
    }

    screen = lv_obj_create(parent);
    lv_obj_set_size(screen, area_w, area_h);
    lv_obj_set_pos(screen, 0, 0);
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x10243A), 0);
    lv_obj_set_style_bg_grad_color(screen, lv_color_hex(0x1E3A5F), 0);
    lv_obj_set_style_bg_grad_dir(screen, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(screen, 0, 0);
    lv_obj_set_style_radius(screen, 0, 0);
    lv_obj_set_style_pad_all(screen, 6, 0);

    title_lab = lv_label_create(screen);
    lv_label_set_text(title_lab, "Settings");
    lv_obj_set_style_text_color(title_lab, lv_color_hex(0xEAF6FF), 0);
    lv_obj_set_style_text_font(title_lab, &DESKTOP_TEXT_FONT, 0);
    lv_obj_align(title_lab, LV_ALIGN_TOP_MID, 0, 4);

    card_w = (area_w > 20) ? (lv_coord_t)(area_w - 20) : area_w;
    card_h = (area_h > 80) ? 86 : (lv_coord_t)(area_h - 24);
    if (card_h < 48) {
        card_h = 48;
    }

    card = lv_obj_create(screen);
    lv_obj_set_size(card, card_w, card_h);
    lv_obj_align(card, LV_ALIGN_TOP_MID, 0, 28);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x0B1D31), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(card, lv_color_hex(0x5CB7FF), 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_radius(card, 8, 0);
    lv_obj_set_style_pad_all(card, 8, 0);
    lv_obj_set_style_pad_row(card, 6, 0);
    lv_obj_set_layout(card, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(card,
                          LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    key_lab = lv_label_create(card);
    lv_label_set_text(key_lab, "Weather City");
    lv_obj_set_style_text_color(key_lab, lv_color_hex(0x9FD5FF), 0);
    lv_obj_set_style_text_font(key_lab, &DESKTOP_TEXT_FONT, 0);

    s_setting_runtime.city_value_lab = lv_label_create(card);
    lv_label_set_text(s_setting_runtime.city_value_lab, "City: --");
    lv_obj_set_style_text_color(s_setting_runtime.city_value_lab, lv_color_hex(0xF4FBFF), 0);
    lv_obj_set_style_text_font(s_setting_runtime.city_value_lab, &DESKTOP_TEXT_FONT, 0);

    s_setting_runtime.hint_lab = lv_label_create(screen);
    lv_obj_set_width(s_setting_runtime.hint_lab,
                     (area_w > 12) ? (lv_coord_t)(area_w - 12) : area_w);
    lv_label_set_long_mode(s_setting_runtime.hint_lab, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(s_setting_runtime.hint_lab, lv_color_hex(0xC9E7FF), 0);
    lv_obj_set_style_text_font(s_setting_runtime.hint_lab, &DESKTOP_TEXT_FONT, 0);
    lv_obj_align(s_setting_runtime.hint_lab, LV_ALIGN_BOTTOM_LEFT, 6, -6);

    _setting_show_selected_city(&s_setting_runtime);
    _setting_show_hint(&s_setting_runtime, "UP/DOWN: Select  BOTH: Save+Back");

    task_ok = xTaskCreate(_setting_ui_task,
                          "setting_ui",
                          SETTING_UI_TASK_STACK_SIZE,
                          &s_setting_runtime,
                          5,
                          &s_setting_runtime.task_handle);
    if (task_ok != pdPASS) {
        s_setting_runtime.task_handle = NULL;
        lv_obj_del(screen);
        ESP_LOGE(TAG, "xTaskCreate failed");
        return NULL;
    }

    return screen;
}

/*
 * brief : config_close_screen.
 * input : see parameters.
 * output: none.
 * type  : public
 */
void config_close_screen(lv_obj_t* screen) {
    if (s_setting_runtime.task_handle != NULL) {
        vTaskDelete(s_setting_runtime.task_handle);
        s_setting_runtime.task_handle = NULL;
    }

    s_setting_runtime.home_cb = NULL;
    s_setting_runtime.home_user_ctx = NULL;
        s_setting_runtime.button_scan = (btn_scan_s){ 0 };
        s_setting_runtime.city_value_lab = NULL;
        s_setting_runtime.hint_lab = NULL;
        s_setting_runtime.city_index = 0U;

    if ((screen != NULL) && lv_obj_is_valid(screen)) {
        lv_obj_del(screen);
    }
}
