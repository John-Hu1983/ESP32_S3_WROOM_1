// Consolidated service implementation file.

#include "service/desktop.h"

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

static uint32_t InvertRgbHex(uint32_t color_hex) {
    return color_hex ^ 0x00FFFFFF;
}

static lv_color_t ContrastTextForHex(uint32_t color_hex) {
    uint8_t r = static_cast<uint8_t>((color_hex >> 16) & 0xFF);
    uint8_t g = static_cast<uint8_t>((color_hex >> 8) & 0xFF);
    uint8_t b = static_cast<uint8_t>(color_hex & 0xFF);

    uint32_t luma = 299U * r + 587U * g + 114U * b;
    return luma >= 140000U ? lv_color_hex(0x111111) : lv_color_hex(0xF5F5F5);
}

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

void DesktopSpiLcdDisplay::BuildGrid(lv_obj_t* parent, const GridStyle& style,
                                     std::vector<lv_obj_t*>* out_tiles) {
    if (parent == nullptr) {
        return;
    }

    const lv_color_t default_text_color = style.text_color;
    const lv_font_t* app_name_font = ResolveAppNameFont(style.text_font);
    const lv_coord_t tile_radius = 14;
    const lv_coord_t tile_padding = 6;
    const lv_style_selector_t sel_main_checked_pressed =
        MainStateSelector(MergeStates(LV_STATE_CHECKED, LV_STATE_PRESSED));
    const lv_style_selector_t sel_main = MainSelector();
    const lv_style_selector_t sel_main_checked = MainStateSelector(LV_STATE_CHECKED);
    const lv_style_selector_t sel_main_pressed = MainStateSelector(LV_STATE_PRESSED);

    if (out_tiles != nullptr) {
        out_tiles->clear();
    }

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
        uint32_t tile_color_hex = kDesktopApps[i].color_hex;
        uint32_t selected_color_hex = InvertRgbHex(tile_color_hex);
        lv_color_t tile_color = lv_color_hex(tile_color_hex);
        lv_color_t selected_color = lv_color_hex(selected_color_hex);
        lv_color_t selected_text_color = ContrastTextForHex(selected_color_hex);

        lv_obj_set_style_bg_color(tile, tile_color, sel_main);
        lv_obj_set_style_bg_color(tile, selected_color, sel_main_checked);
        lv_obj_set_style_bg_color(tile, tile_color, sel_main_pressed);
        lv_obj_set_style_bg_color(tile, selected_color, sel_main_checked_pressed);
        lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, sel_main);
        lv_obj_set_style_border_width(tile, 2, sel_main);
        lv_obj_set_style_border_color(tile, selected_text_color, sel_main_checked);
        lv_obj_set_style_border_color(tile, selected_text_color, sel_main_checked_pressed);
        lv_obj_set_style_border_opa(tile, LV_OPA_TRANSP, sel_main);
        lv_obj_set_style_border_opa(tile, LV_OPA_COVER, sel_main_checked);
        lv_obj_set_style_border_opa(tile, LV_OPA_COVER, sel_main_checked_pressed);
        lv_obj_set_style_radius(tile, tile_radius, sel_main);
        lv_obj_set_style_shadow_width(tile, 0, sel_main);
        lv_obj_set_style_shadow_opa(tile, LV_OPA_TRANSP, sel_main);
        lv_obj_set_style_pad_all(tile, tile_padding, sel_main);
        lv_obj_set_scrollbar_mode(tile, LV_SCROLLBAR_MODE_OFF);
        lv_obj_add_flag(tile, LV_OBJ_FLAG_CHECKABLE);

        int row = i / 3;
        int col = i % 3;
        lv_obj_set_grid_cell(tile, LV_GRID_ALIGN_STRETCH, col, 1, LV_GRID_ALIGN_STRETCH, row, 1);

        lv_obj_t* icon_label = lv_label_create(tile);
        if (style.icon_font != nullptr) {
            lv_obj_set_style_text_font(icon_label, style.icon_font, 0);
        }
        lv_obj_set_style_text_color(icon_label, default_text_color, sel_main);
        lv_obj_set_style_text_color(icon_label, selected_text_color, sel_main_checked);
        lv_obj_set_style_text_color(icon_label, selected_text_color, sel_main_checked_pressed);
        lv_label_set_text(icon_label, kDesktopApps[i].symbol);
        lv_obj_align(icon_label, LV_ALIGN_TOP_MID, 0, 4);

        lv_obj_t* name_label = lv_label_create(tile);
        if (app_name_font != nullptr) {
            lv_obj_set_style_text_font(name_label, app_name_font, 0);
        }
        lv_obj_set_style_text_color(name_label, default_text_color, sel_main);
        lv_obj_set_style_text_color(name_label, selected_text_color, sel_main_checked);
        lv_obj_set_style_text_color(name_label, selected_text_color, sel_main_checked_pressed);
        lv_obj_set_style_text_letter_space(name_label, 1, 0);
        lv_label_set_text(name_label, kDesktopApps[i].name);
        lv_obj_align(name_label, LV_ALIGN_BOTTOM_MID, 0, -4);

        if (out_tiles != nullptr) {
            out_tiles->push_back(tile);
        }
    }
}

void DesktopSpiLcdDisplay::RefreshSelectionLocked() {
    if (desktop_tiles_.empty()) {
        selected_tile_index_ = -1;
        return;
    }

    const int tile_count = static_cast<int>(desktop_tiles_.size());
    if (selected_tile_index_ < -1 || selected_tile_index_ >= tile_count) {
        selected_tile_index_ = -1;
    }

    for (int i = 0; i < tile_count; ++i) {
        lv_obj_t* tile = desktop_tiles_[i];
        if (tile == nullptr) {
            continue;
        }

        if (selected_tile_index_ >= 0 && i == selected_tile_index_) {
            lv_obj_add_state(tile, LV_STATE_CHECKED);
        } else {
            lv_obj_remove_state(tile, LV_STATE_CHECKED);
        }
    }
}

bool DesktopSpiLcdDisplay::HasSelectableControls() {
    DisplayLockGuard lock(this);
    return !desktop_tiles_.empty();
}

bool DesktopSpiLcdDisplay::SelectNextControl() {
    DisplayLockGuard lock(this);
    if (desktop_tiles_.empty()) {
        return false;
    }

    const int tile_count = static_cast<int>(desktop_tiles_.size());
    if (selected_tile_index_ < 0) {
        selected_tile_index_ = 0;
    } else if (selected_tile_index_ >= tile_count - 1) {
        selected_tile_index_ = -1;
    } else {
        ++selected_tile_index_;
    }

    RefreshSelectionLocked();
    return true;
}

bool DesktopSpiLcdDisplay::SelectPreviousControl() {
    DisplayLockGuard lock(this);
    if (desktop_tiles_.empty()) {
        return false;
    }

    const int tile_count = static_cast<int>(desktop_tiles_.size());
    if (selected_tile_index_ < 0) {
        selected_tile_index_ = tile_count - 1;
    } else if (selected_tile_index_ <= 0) {
        selected_tile_index_ = -1;
    } else {
        --selected_tile_index_;
    }

    RefreshSelectionLocked();
    return true;
}

int DesktopSpiLcdDisplay::GetSelectedControlIndex() {
    DisplayLockGuard lock(this);
    return selected_tile_index_;
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

    perf_label_ = lv_label_create(top_bar_);
    lv_label_set_text(perf_label_, "cpu: --% mo: --% mi: --%");
    lv_obj_set_style_text_font(perf_label_, text_font, 0);
    lv_obj_set_style_text_color(perf_label_, text_color, 0);

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

    network_label_ = lv_label_create(right_icons);
    lv_label_set_text(network_label_, "");
    lv_obj_set_style_text_font(network_label_, icon_font, 0);
    lv_obj_set_style_text_color(network_label_, text_color, 0);
    lv_obj_set_style_margin_left(network_label_, spacing, 0);

    status_label_ = lv_label_create(right_icons);
    lv_label_set_text(status_label_, "--:--");
    lv_label_set_long_mode(status_label_, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(status_label_, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_style_text_color(status_label_, text_color, 0);
    lv_obj_set_style_margin_left(status_label_, spacing, 0);

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
    DesktopSpiLcdDisplay::BuildGrid(content_, grid_style, &desktop_tiles_);
    selected_tile_index_ = -1;
    RefreshSelectionLocked();

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
    if (perf_label_ != nullptr) {
        lv_obj_set_style_text_font(perf_label_, text_font, 0);
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
    if (perf_label_ != nullptr) {
        lv_obj_set_style_text_color(perf_label_, text_color, 0);
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
        DesktopSpiLcdDisplay::BuildGrid(content_, grid_style, &desktop_tiles_);
        RefreshSelectionLocked();
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


#include "service/display_factory.h"

#include "service/desktop.h"

Display* CreatePrimaryDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel,
                              int width, int height, int offset_x, int offset_y, bool mirror_x,
                              bool mirror_y, bool swap_xy) {
    return new DesktopSpiLcdDisplay(panel_io, panel, width, height, offset_x, offset_y, mirror_x,
                                    mirror_y, swap_xy);
}


#include "service/business_service.h"

#include "display.h"
#include "service/desktop.h"

#include <esp_timer.h>

uint64_t BusinessServiceLayer::GetNowMs() {
    return static_cast<uint64_t>(esp_timer_get_time() / 1000);
}

void BusinessServiceLayer::Initialize(Display* display) {
    if (display != nullptr && display->HasSelectableControls()) {
        current_service_index_ = -1;
    } else {
        current_service_index_ = 0;
    }

    last_click_ms_[0] = 0;
    last_click_ms_[1] = 0;
}

bool BusinessServiceLayer::IsDesktopActive(Display* display) const {
    return current_service_index_ < 0 && display != nullptr && display->HasSelectableControls();
}

bool BusinessServiceLayer::IsAiServiceActive() const { return current_service_index_ == 0; }

bool BusinessServiceLayer::HandleDesktopKey(uint8_t key_index, BoardKeyEventType event_type,
                                            Display* display, int* entered_service_index) {
    if (entered_service_index != nullptr) {
        *entered_service_index = -1;
    }

    if (!IsDesktopActive(display)) {
        return false;
    }

    if (display == nullptr) {
        return true;
    }

    if (key_index == kPrimaryKey && event_type == BoardKeyEventType::Click) {
        display->SelectPreviousControl();
        return true;
    }

    if (key_index == kSecondaryKey && event_type == BoardKeyEventType::Click) {
        display->SelectNextControl();
        return true;
    }

    if (key_index == kSecondaryKey && event_type == BoardKeyEventType::LongPress) {
        int selected_index = display->GetSelectedControlIndex();
        if (selected_index < 0) {
            selected_index = 0;
        }
        if (selected_index >= kServiceCount) {
            selected_index = kServiceCount - 1;
        }

        if (entered_service_index != nullptr) {
            *entered_service_index = selected_index;
        }
        return true;
    }

    return false;
}

bool BusinessServiceLayer::HandleDualClickExit(uint8_t key_index, BoardKeyEventType event_type) {
    if (current_service_index_ < 0 || event_type != BoardKeyEventType::Click) {
        return false;
    }

    if (key_index != kPrimaryKey && key_index != kSecondaryKey) {
        return false;
    }

    uint64_t now_ms = GetNowMs();
    uint8_t other_key = key_index == kPrimaryKey ? kSecondaryKey : kPrimaryKey;
    last_click_ms_[key_index] = now_ms;

    if (last_click_ms_[other_key] != 0 &&
        (now_ms - last_click_ms_[other_key]) <= kDualClickWindowMs) {
        last_click_ms_[0] = 0;
        last_click_ms_[1] = 0;
        return true;
    }

    return false;
}

service_key_result_t BusinessServiceLayer::HandleServiceKey(uint8_t key_index,
                                                            BoardKeyEventType event_type,
                                                            Display* display) {
    service_key_result_t result = {0};
    if (current_service_index_ < 0 || current_service_index_ >= service_get_count()) {
        return result;
    }

    result = service_handle_key(current_service_index_, key_index,
                                static_cast<uint8_t>(event_type));
    if (result.consumed && display != nullptr && result.notification != nullptr &&
        result.notification[0] != '\0') {
        display->ShowNotification(result.notification, 900);
    }

    return result;
}

void BusinessServiceLayer::EnterService(int service_index, Display* display) {
    if (display == nullptr || service_index < 0 || service_index >= kServiceCount) {
        return;
    }

    const service_item_t* item = service_get_item(service_index);
    if (item == nullptr) {
        return;
    }

    current_service_index_ = service_index;
    display->SetStatus(item->status);
    display->SetChatMessage("system", item->prompt);
    display->ShowNotification(item->name, 1000);
}

void BusinessServiceLayer::EnterDesktop(Display* display, bool show_notification) {
    if (display == nullptr || !display->HasSelectableControls()) {
        return;
    }

    current_service_index_ = -1;
    if (show_notification) {
        display->ShowNotification("Desktop", 1000);
    }
    display->SetChatMessage("system", DesktopSpiLcdDisplay::DefaultPrompt());
}

