#include "ui/desktop_display.h"

#include "assets/lang_config.h"
#include "lvgl_theme.h"

#include <esp_log.h>
#include <material_symbols.h>
#include <cstring>

#define TAG "DesktopDisplay"

static constexpr int kDesktopCardRadius = 16;
static constexpr lv_opa_t kDesktopCardOpacity = LV_OPA_80;

static lv_color_t DesktopOrangeTop() { return lv_color_hex(0x2C001E); }

static lv_color_t DesktopOrangeBottom() { return lv_color_hex(0x772953); }

static lv_color_t DesktopPanelColor() { return lv_color_hex(0x5E2750); }

static lv_color_t DesktopPanelBorderColor() { return lv_color_hex(0xE3A183); }

static lv_color_t DesktopTextColor() { return lv_color_hex(0xF6EDE8); }

static lv_color_t DesktopShadowColor() { return lv_color_hex(0x1F0A18); }

static void ApplyDesktopBackdrop(lv_obj_t* obj) {
    if (obj == nullptr) {
        return;
    }

    lv_obj_set_style_bg_color(obj, DesktopOrangeTop(), 0);
    lv_obj_set_style_bg_grad_color(obj, DesktopOrangeBottom(), 0);
    lv_obj_set_style_bg_grad_dir(obj, LV_GRAD_DIR_VER, 0);
}

static void StyleRoundedCard(lv_obj_t* obj, lv_color_t bg_color, lv_color_t border_color) {
    if (obj == nullptr) {
        return;
    }

    lv_obj_set_style_radius(obj, kDesktopCardRadius, 0);
    lv_obj_set_style_bg_color(obj, bg_color, 0);
    lv_obj_set_style_bg_opa(obj, kDesktopCardOpacity, 0);
    lv_obj_set_style_border_width(obj, 1, 0);
    lv_obj_set_style_border_color(obj, border_color, 0);
    lv_obj_set_style_shadow_width(obj, 8, 0);
    lv_obj_set_style_shadow_opa(obj, LV_OPA_20, 0);
    lv_obj_set_style_shadow_color(obj, DesktopShadowColor(), 0);
}

static void StyleRectBar(lv_obj_t* obj, lv_color_t bg_color, lv_color_t border_color) {
    if (obj == nullptr) {
        return;
    }

    lv_obj_set_style_radius(obj, 0, 0);
    lv_obj_set_style_bg_color(obj, bg_color, 0);
    lv_obj_set_style_bg_opa(obj, kDesktopCardOpacity, 0);
    lv_obj_set_style_border_width(obj, 1, 0);
    lv_obj_set_style_border_color(obj, border_color, 0);
    lv_obj_set_style_shadow_width(obj, 0, 0);
    lv_obj_set_style_shadow_opa(obj, LV_OPA_TRANSP, 0);
}

lv_style_selector_t DesktopSpiLcdDisplay::MainSelector() {
    return static_cast<lv_style_selector_t>(LV_PART_MAIN);
}

lv_state_t DesktopSpiLcdDisplay::MergeStates(lv_state_t a, lv_state_t b) {
    return static_cast<lv_state_t>(static_cast<lv_style_selector_t>(a) |
                                   static_cast<lv_style_selector_t>(b));
}

lv_style_selector_t DesktopSpiLcdDisplay::MainStateSelector(lv_state_t state) {
    return static_cast<lv_style_selector_t>(static_cast<lv_style_selector_t>(LV_PART_MAIN) |
                                            static_cast<lv_style_selector_t>(state));
}

const lv_font_t* DesktopSpiLcdDisplay::ResolveAppNameFont(const lv_font_t* fallback_font) {
#if LV_FONT_MONTSERRAT_20
    return &lv_font_montserrat_20;
#elif LV_FONT_MONTSERRAT_18
    return &lv_font_montserrat_18;
#elif LV_FONT_MONTSERRAT_16
    return &lv_font_montserrat_16;
#else
    return fallback_font;
#endif
}

const DesktopSpiLcdDisplay::DesktopAppItem DesktopSpiLcdDisplay::kDesktopApps[] = {
    {MATERIAL_SYMBOLS_ROBOT_2, "AI", 0xE95420},
    {MATERIAL_SYMBOLS_WIFI, "WiFi", 0xD94B3D},
    {MATERIAL_SYMBOLS_WIFI_2_BAR, "Scan", 0xC0563F},
    {MATERIAL_SYMBOLS_WIFI_1_BAR, "Link", 0xB65C2C},
    {MATERIAL_SYMBOLS_WIFI_OFF, "Offline", 0x6F4A34},
    {MATERIAL_SYMBOLS_ANDROID_CELL_4_BAR, "Cell", 0xF27C38},
    {MATERIAL_SYMBOLS_SIGNAL_CELLULAR_ALT, "Signal", 0xA8703A},
    {MATERIAL_SYMBOLS_SIGNAL_CELLULAR_ALT_2_BAR, "Node", 0x8A3D5D},
    {MATERIAL_SYMBOLS_SIGNAL_CELLULAR_ALT_1_BAR, "Debug", 0x8F6745},
    {MATERIAL_SYMBOLS_ANDROID_CELL_4_BAR_OFF, "Tools", 0x77216F},
    {MATERIAL_SYMBOLS_VOLUME_OFF, "Mute", 0xC23B4A},
    {MATERIAL_SYMBOLS_BATTERY_ANDROID_FRAME_FULL, "Power", 0xE19A35},
};

bool DesktopSpiLcdDisplay::IsEnabled() { return kDesktopHomeEnabled; }

const char* DesktopSpiLcdDisplay::DefaultPrompt() { return kDefaultPrompt; }

bool DesktopSpiLcdDisplay::ShouldDisplayRole(const char* role) {
    return role != nullptr && std::strcmp(role, "system") == 0;
}

void DesktopSpiLcdDisplay::BuildGrid(lv_obj_t* parent, const GridStyle& style) {
    if (parent == nullptr) {
        return;
    }

    const lv_color_t selected_color = lv_color_hex(0xE95420);
    const lv_color_t default_text_color = style.text_color;
    const lv_color_t checked_text_color = lv_color_white();
    const lv_font_t* app_name_font = ResolveAppNameFont(style.text_font);
    const lv_coord_t tile_radius = 14;
    const lv_coord_t tile_padding = 6;
    const lv_style_selector_t sel_main = MainSelector();
    const lv_style_selector_t sel_main_checked = MainStateSelector(LV_STATE_CHECKED);
    const lv_style_selector_t sel_main_pressed = MainStateSelector(LV_STATE_PRESSED);
    const lv_style_selector_t sel_main_checked_pressed =
        MainStateSelector(MergeStates(LV_STATE_CHECKED, LV_STATE_PRESSED));

    static lv_coord_t col_dsc[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1),
                                   LV_GRID_TEMPLATE_LAST};
    static lv_coord_t row_dsc[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1),
                                   LV_GRID_TEMPLATE_LAST};
    lv_obj_set_layout(parent, LV_LAYOUT_GRID);
    lv_obj_set_grid_dsc_array(parent, col_dsc, row_dsc);
    lv_obj_set_style_pad_row(parent, 10, 0);
    lv_obj_set_style_pad_column(parent, 10, 0);

    const int app_count = static_cast<int>(sizeof(kDesktopApps) / sizeof(kDesktopApps[0]));
    for (int i = 0; i < app_count; ++i) {
        lv_obj_t* tile = lv_btn_create(parent);
        lv_obj_remove_style_all(tile);
        lv_color_t tile_color = lv_color_hex(kDesktopApps[i].color_hex);

        lv_obj_set_style_bg_color(tile, tile_color, sel_main);
        lv_obj_set_style_bg_color(tile, selected_color, sel_main_checked);
        lv_obj_set_style_bg_color(tile, tile_color, sel_main_pressed);
        lv_obj_set_style_bg_color(tile, selected_color, sel_main_checked_pressed);
        lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, sel_main);
        lv_obj_set_style_border_width(tile, 2, sel_main);
        lv_obj_set_style_border_color(tile, lv_color_white(), sel_main_checked);
        lv_obj_set_style_border_color(tile, lv_color_white(), sel_main_checked_pressed);
        lv_obj_set_style_border_opa(tile, LV_OPA_TRANSP, sel_main);
        lv_obj_set_style_border_opa(tile, LV_OPA_COVER, sel_main_checked);
        lv_obj_set_style_border_opa(tile, LV_OPA_COVER, sel_main_checked_pressed);
        lv_obj_set_style_radius(tile, tile_radius, sel_main);
        lv_obj_set_style_shadow_width(tile, 0, sel_main);
        lv_obj_set_style_shadow_opa(tile, LV_OPA_TRANSP, sel_main);
        lv_obj_set_style_pad_all(tile, tile_padding, sel_main);
        lv_obj_set_scrollbar_mode(tile, LV_SCROLLBAR_MODE_OFF);

        int row = i / 3;
        int col = i % 3;
        lv_obj_set_grid_cell(tile, LV_GRID_ALIGN_STRETCH, col, 1, LV_GRID_ALIGN_STRETCH, row, 1);

        lv_obj_t* icon_label = lv_label_create(tile);
        if (style.icon_font != nullptr) {
            lv_obj_set_style_text_font(icon_label, style.icon_font, 0);
        }
        lv_obj_set_style_text_color(icon_label, default_text_color, sel_main);
        lv_obj_set_style_text_color(icon_label, checked_text_color, sel_main_checked);
        lv_label_set_text(icon_label, kDesktopApps[i].symbol);
        lv_obj_align(icon_label, LV_ALIGN_TOP_MID, 0, 4);

        lv_obj_t* name_label = lv_label_create(tile);
        if (app_name_font != nullptr) {
            lv_obj_set_style_text_font(name_label, app_name_font, 0);
        }
        lv_obj_set_style_text_color(name_label, default_text_color, sel_main);
        lv_obj_set_style_text_color(name_label, checked_text_color, sel_main_checked);
        lv_obj_set_style_text_letter_space(name_label, 1, 0);
        lv_label_set_text(name_label, kDesktopApps[i].name);
        lv_obj_align(name_label, LV_ALIGN_BOTTOM_MID, 0, -4);
    }
}

void DesktopSpiLcdDisplay::SetupUI() {
    if (setup_ui_called_) {
        ESP_LOGW(TAG, "SetupUI() called multiple times, skipping duplicate call");
        return;
    }

    Display::SetupUI();
    DisplayLockGuard lock(this);

    auto lvgl_theme = static_cast<LvglTheme*>(current_theme_);
    const lv_font_t* text_font = lvgl_theme->text_font()->font();
    const lv_font_t* icon_font = lvgl_theme->icon_font()->font();
    const lv_font_t* large_icon_font = lvgl_theme->large_icon_font()->font();
    int spacing = static_cast<int>(lvgl_theme->spacing(2));
    if (spacing < 4) {
        spacing = 4;
    }

    const lv_color_t text_color = DesktopTextColor();
    const lv_color_t panel_color = DesktopPanelColor();
    const lv_color_t panel_border_color = DesktopPanelBorderColor();

    lv_obj_t* screen = lv_screen_active();
    lv_obj_set_style_text_font(screen, text_font, 0);
    lv_obj_set_style_text_color(screen, text_color, 0);
    ApplyDesktopBackdrop(screen);

    container_ = lv_obj_create(screen);
    lv_obj_set_size(container_, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_style_radius(container_, 0, 0);
    lv_obj_set_flex_flow(container_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(container_, spacing, 0);
    lv_obj_set_style_border_width(container_, 0, 0);
    lv_obj_set_style_pad_row(container_, spacing, 0);
    lv_obj_set_style_bg_opa(container_, LV_OPA_TRANSP, 0);

    top_bar_ = lv_obj_create(container_);
    lv_obj_set_size(top_bar_, LV_PCT(100), LV_SIZE_CONTENT);
    StyleRectBar(top_bar_, panel_color, panel_border_color);
    lv_obj_set_style_pad_top(top_bar_, spacing, 0);
    lv_obj_set_style_pad_bottom(top_bar_, spacing, 0);
    lv_obj_set_style_pad_left(top_bar_, spacing, 0);
    lv_obj_set_style_pad_right(top_bar_, spacing, 0);
    lv_obj_set_flex_flow(top_bar_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(top_bar_, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scrollbar_mode(top_bar_, LV_SCROLLBAR_MODE_OFF);

    network_label_ = lv_label_create(top_bar_);
    lv_label_set_text(network_label_, "");
    lv_obj_set_style_text_font(network_label_, icon_font, 0);
    lv_obj_set_style_text_color(network_label_, text_color, 0);

    lv_obj_t* right_icons = lv_obj_create(top_bar_);
    lv_obj_set_size(right_icons, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(right_icons, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(right_icons, 0, 0);
    lv_obj_set_style_pad_all(right_icons, 0, 0);
    lv_obj_set_flex_flow(right_icons, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(right_icons, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    mute_label_ = lv_label_create(right_icons);
    lv_label_set_text(mute_label_, "");
    lv_obj_set_style_text_font(mute_label_, icon_font, 0);
    lv_obj_set_style_text_color(mute_label_, text_color, 0);

    battery_label_ = lv_label_create(right_icons);
    lv_label_set_text(battery_label_, "");
    lv_obj_set_style_text_font(battery_label_, icon_font, 0);
    lv_obj_set_style_text_color(battery_label_, text_color, 0);
    lv_obj_set_style_margin_left(battery_label_, spacing, 0);

    status_bar_ = lv_obj_create(top_bar_);
    lv_obj_set_size(status_bar_, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(status_bar_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(status_bar_, 0, 0);
    lv_obj_set_style_pad_all(status_bar_, 0, 0);
    lv_obj_set_scrollbar_mode(status_bar_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_layout(status_bar_, LV_LAYOUT_NONE, 0);
    lv_obj_add_flag(status_bar_, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_align(status_bar_, LV_ALIGN_CENTER, 0, 0);

    notification_label_ = lv_label_create(status_bar_);
    lv_obj_set_width(notification_label_, LV_HOR_RES * 0.56);
    lv_obj_set_style_text_align(notification_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(notification_label_, text_color, 0);
    lv_label_set_text(notification_label_, "");
    lv_obj_align(notification_label_, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_flag(notification_label_, LV_OBJ_FLAG_HIDDEN);

    status_label_ = lv_label_create(status_bar_);
    lv_obj_set_width(status_label_, LV_HOR_RES * 0.56);
    lv_label_set_long_mode(status_label_, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_style_text_align(status_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(status_label_, text_color, 0);
    lv_label_set_text(status_label_, Lang::Strings::INITIALIZING);
    lv_obj_align(status_label_, LV_ALIGN_CENTER, 0, 0);

    content_ = lv_obj_create(container_);
    lv_obj_set_width(content_, LV_PCT(100));
    lv_obj_set_flex_grow(content_, 1);
    StyleRoundedCard(content_, panel_color, panel_border_color);
    lv_obj_set_style_bg_opa(content_, LV_OPA_30, 0);
    lv_obj_set_style_pad_all(content_, spacing, 0);
    lv_obj_set_style_pad_row(content_, spacing, 0);
    lv_obj_set_style_pad_column(content_, spacing, 0);
    lv_obj_set_scrollbar_mode(content_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(content_, LV_OBJ_FLAG_SCROLLABLE);

    DesktopSpiLcdDisplay::GridStyle grid_style;
    grid_style.icon_font = large_icon_font;
    grid_style.text_font = text_font;
    grid_style.text_color = text_color;
    grid_style.tile_color = panel_color;
    grid_style.spacing = spacing;
    DesktopSpiLcdDisplay::BuildGrid(content_, grid_style);

    bottom_bar_ = lv_obj_create(container_);
    lv_obj_set_size(bottom_bar_, LV_PCT(100), text_font->line_height + spacing * 3);
    StyleRectBar(bottom_bar_, panel_color, panel_border_color);
    lv_obj_set_style_text_color(bottom_bar_, text_color, 0);
    lv_obj_set_style_pad_left(bottom_bar_, spacing, 0);
    lv_obj_set_style_pad_right(bottom_bar_, spacing, 0);
    lv_obj_set_scrollbar_mode(bottom_bar_, LV_SCROLLBAR_MODE_OFF);

    chat_message_label_ = lv_label_create(bottom_bar_);
    lv_obj_set_width(chat_message_label_, LV_HOR_RES - spacing * 8);
    lv_label_set_long_mode(chat_message_label_, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_style_text_align(chat_message_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(chat_message_label_, text_color, 0);
    lv_obj_align(chat_message_label_, LV_ALIGN_CENTER, 0, 0);
    lv_label_set_text(chat_message_label_, DesktopSpiLcdDisplay::DefaultPrompt());

    low_battery_popup_ = lv_obj_create(screen);
    lv_obj_set_scrollbar_mode(low_battery_popup_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_size(low_battery_popup_, LV_HOR_RES * 0.9, text_font->line_height * 2);
    lv_obj_align(low_battery_popup_, LV_ALIGN_BOTTOM_MID, 0, -spacing * 2);
    StyleRoundedCard(low_battery_popup_, lv_color_hex(0xA02B13), panel_border_color);

    low_battery_label_ = lv_label_create(low_battery_popup_);
    lv_label_set_text(low_battery_label_, Lang::Strings::BATTERY_NEED_CHARGE);
    lv_obj_set_style_text_color(low_battery_label_, lv_color_white(), 0);
    lv_obj_center(low_battery_label_);
    lv_obj_add_flag(low_battery_popup_, LV_OBJ_FLAG_HIDDEN);

    preview_image_ = nullptr;
    emoji_image_ = nullptr;
    emoji_label_ = nullptr;
    emoji_box_ = nullptr;

    ESP_LOGI(TAG, "Desktop display initialized");
}

void DesktopSpiLcdDisplay::SetChatMessage(const char* role, const char* content) {
    if (!setup_ui_called_) {
        ESP_LOGW(TAG, "SetChatMessage called before SetupUI");
    }

    DisplayLockGuard lock(this);
    if (chat_message_label_ == nullptr) {
        return;
    }

    if (!DesktopSpiLcdDisplay::ShouldDisplayRole(role)) {
        return;
    }

    const char* text = (content != nullptr && content[0] != '\0')
                           ? content
                           : DesktopSpiLcdDisplay::DefaultPrompt();
    lv_label_set_text(chat_message_label_, text);
}

void DesktopSpiLcdDisplay::ClearChatMessages() {
    DisplayLockGuard lock(this);
    if (chat_message_label_ != nullptr) {
        lv_label_set_text(chat_message_label_, DesktopSpiLcdDisplay::DefaultPrompt());
    }
}

void DesktopSpiLcdDisplay::SetEmotion(const char* emotion) { (void)emotion; }

void DesktopSpiLcdDisplay::SetPreviewImage(std::unique_ptr<LvglImage> image) { (void)image; }

void DesktopSpiLcdDisplay::SetTheme(Theme* theme) {
    DisplayLockGuard lock(this);

    auto lvgl_theme = static_cast<LvglTheme*>(theme);
    lv_obj_t* screen = lv_screen_active();

    const lv_font_t* text_font = lvgl_theme->text_font()->font();
    const lv_font_t* icon_font = lvgl_theme->icon_font()->font();
    const lv_font_t* large_icon_font = lvgl_theme->large_icon_font()->font();
    int spacing = static_cast<int>(lvgl_theme->spacing(2));
    if (spacing < 4) {
        spacing = 4;
    }

    const lv_color_t text_color = DesktopTextColor();
    const lv_color_t panel_color = DesktopPanelColor();
    const lv_color_t panel_border_color = DesktopPanelBorderColor();

    if (text_font->line_height >= 40) {
        lv_obj_set_style_text_font(mute_label_, large_icon_font, 0);
        lv_obj_set_style_text_font(battery_label_, large_icon_font, 0);
        lv_obj_set_style_text_font(network_label_, large_icon_font, 0);
    } else {
        lv_obj_set_style_text_font(mute_label_, icon_font, 0);
        lv_obj_set_style_text_font(battery_label_, icon_font, 0);
        lv_obj_set_style_text_font(network_label_, icon_font, 0);
    }

    lv_obj_set_style_text_font(screen, text_font, 0);
    lv_obj_set_style_text_color(screen, text_color, 0);
    ApplyDesktopBackdrop(screen);

    if (container_ != nullptr) {
        lv_obj_set_style_bg_image_src(container_, nullptr, 0);
        lv_obj_set_style_bg_opa(container_, LV_OPA_TRANSP, 0);
        lv_obj_set_style_pad_all(container_, spacing, 0);
        lv_obj_set_style_pad_row(container_, spacing, 0);
    }

    if (top_bar_ != nullptr) {
        StyleRectBar(top_bar_, panel_color, panel_border_color);
        lv_obj_set_style_pad_top(top_bar_, spacing, 0);
        lv_obj_set_style_pad_bottom(top_bar_, spacing, 0);
        lv_obj_set_style_pad_left(top_bar_, spacing, 0);
        lv_obj_set_style_pad_right(top_bar_, spacing, 0);
    }

    if (status_bar_ != nullptr) {
        lv_obj_set_style_bg_opa(status_bar_, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(status_bar_, 0, 0);
        lv_obj_set_style_pad_all(status_bar_, 0, 0);
        lv_obj_add_flag(status_bar_, LV_OBJ_FLAG_IGNORE_LAYOUT);
        lv_obj_align(status_bar_, LV_ALIGN_CENTER, 0, 0);
    }

    if (network_label_ != nullptr) {
        lv_obj_set_style_text_color(network_label_, text_color, 0);
    }
    if (status_label_ != nullptr) {
        lv_obj_set_style_text_color(status_label_, text_color, 0);
    }
    if (notification_label_ != nullptr) {
        lv_obj_set_style_text_color(notification_label_, text_color, 0);
    }
    if (mute_label_ != nullptr) {
        lv_obj_set_style_text_color(mute_label_, text_color, 0);
    }
    if (battery_label_ != nullptr) {
        lv_obj_set_style_text_color(battery_label_, text_color, 0);
    }

    if (content_ != nullptr) {
        StyleRoundedCard(content_, panel_color, panel_border_color);
        lv_obj_set_style_bg_opa(content_, LV_OPA_30, 0);
        lv_obj_set_style_pad_all(content_, spacing, 0);
        lv_obj_set_style_pad_row(content_, spacing, 0);
        lv_obj_set_style_pad_column(content_, spacing, 0);
        lv_obj_clean(content_);
        DesktopSpiLcdDisplay::GridStyle grid_style;
        grid_style.icon_font = large_icon_font;
        grid_style.text_font = text_font;
        grid_style.text_color = text_color;
        grid_style.tile_color = panel_color;
        grid_style.spacing = spacing;
        DesktopSpiLcdDisplay::BuildGrid(content_, grid_style);
        lv_obj_clear_flag(content_, LV_OBJ_FLAG_SCROLLABLE);
    }

    if (chat_message_label_ != nullptr) {
        lv_obj_set_style_text_color(chat_message_label_, text_color, 0);
    }

    if (bottom_bar_ != nullptr) {
        StyleRectBar(bottom_bar_, panel_color, panel_border_color);
        lv_obj_set_style_text_color(bottom_bar_, text_color, 0);
        lv_obj_set_style_pad_left(bottom_bar_, spacing, 0);
        lv_obj_set_style_pad_right(bottom_bar_, spacing, 0);
    }

    if (low_battery_popup_ != nullptr) {
        StyleRoundedCard(low_battery_popup_, lv_color_hex(0xA02B13), panel_border_color);
    }

    Display::SetTheme(lvgl_theme);
}
