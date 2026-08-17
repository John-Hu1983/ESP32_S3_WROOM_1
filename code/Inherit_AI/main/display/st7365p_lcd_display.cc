#include "display/st7365p_lcd_display.h"

#include <cstring>

bool St7365pLcdDisplay::IsEnabled() { return true; }

const char* St7365pLcdDisplay::DefaultPrompt() { return "System ready. Select app."; }

bool St7365pLcdDisplay::ShouldDisplayRole(const char* role) {
    return role != nullptr && std::strcmp(role, "system") == 0;
}

void St7365pLcdDisplay::ApplyDesktopChromeLocked() {
    lv_color_t ubuntu_bar = lv_color_hex(0x2C001E);
    lv_color_t ubuntu_text = lv_color_hex(0xF5F5F5);
    lv_color_t ubuntu_accent = lv_color_hex(0xE95420);

    if (top_bar_ != nullptr) {
        lv_obj_set_style_bg_opa(top_bar_, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(top_bar_, ubuntu_bar, 0);
        lv_obj_set_style_border_width(top_bar_, 1, 0);
        lv_obj_set_style_border_color(top_bar_, ubuntu_accent, 0);
        lv_obj_set_style_border_opa(top_bar_, LV_OPA_40, 0);
        lv_obj_remove_flag(top_bar_, LV_OBJ_FLAG_HIDDEN);
    }

    if (bottom_bar_ != nullptr) {
        lv_obj_set_style_bg_opa(bottom_bar_, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(bottom_bar_, ubuntu_bar, 0);
        lv_obj_set_style_border_width(bottom_bar_, 1, 0);
        lv_obj_set_style_border_color(bottom_bar_, ubuntu_accent, 0);
        lv_obj_set_style_border_opa(bottom_bar_, LV_OPA_40, 0);
        lv_obj_remove_flag(bottom_bar_, LV_OBJ_FLAG_HIDDEN);
    }

    if (status_label_ != nullptr) {
        lv_obj_set_style_text_color(status_label_, ubuntu_text, 0);
    }
    if (notification_label_ != nullptr) {
        lv_obj_set_style_text_color(notification_label_, ubuntu_text, 0);
    }
    if (network_label_ != nullptr) {
        lv_obj_set_style_text_color(network_label_, ubuntu_text, 0);
    }
    if (mute_label_ != nullptr) {
        lv_obj_set_style_text_color(mute_label_, ubuntu_text, 0);
    }
    if (battery_label_ != nullptr) {
        lv_obj_set_style_text_color(battery_label_, ubuntu_text, 0);
    }
    if (perf_label_ != nullptr) {
        lv_obj_set_style_text_color(perf_label_, ubuntu_text, 0);
    }
    if (chat_message_label_ != nullptr) {
        lv_obj_set_style_text_color(chat_message_label_, ubuntu_text, 0);
    }
}

void St7365pLcdDisplay::BuildGrid(lv_obj_t* parent, const GridStyle& style,
                                  std::vector<lv_obj_t*>* out_tiles) {
    if (parent == nullptr) {
        return;
    }

    (void)style;

    if (out_tiles != nullptr) {
        out_tiles->clear();
    }
}

void St7365pLcdDisplay::SetupUI() {
    SpiLcdDisplay::SetupUI();

    DisplayLockGuard lock(this);
    ApplyDesktopChromeLocked();

    if (chat_message_label_ != nullptr) {
        lv_label_set_long_mode(chat_message_label_, LV_LABEL_LONG_SCROLL_CIRCULAR);
        lv_obj_set_style_anim_duration(chat_message_label_, 4500, 0);
        lv_label_set_text(chat_message_label_, DefaultPrompt());
    }
}

void St7365pLcdDisplay::SetTheme(Theme* theme) {
    SpiLcdDisplay::SetTheme(theme);

    DisplayLockGuard lock(this);
    ApplyDesktopChromeLocked();
}

void St7365pLcdDisplay::SetChatMessage(const char* role, const char* content) {
    DisplayLockGuard lock(this);
    if (chat_message_label_ == nullptr) {
        return;
    }

    if (!ShouldDisplayRole(role)) {
        return;
    }

    const char* text = (content != nullptr && content[0] != '\0') ? content : DefaultPrompt();
    lv_label_set_text(chat_message_label_, text);

    if (bottom_bar_ != nullptr) {
        lv_obj_remove_flag(bottom_bar_, LV_OBJ_FLAG_HIDDEN);
    }
}

void St7365pLcdDisplay::ClearChatMessages() {
    DisplayLockGuard lock(this);
    if (chat_message_label_ != nullptr) {
        lv_label_set_text(chat_message_label_, DefaultPrompt());
    }

    if (bottom_bar_ != nullptr) {
        lv_obj_remove_flag(bottom_bar_, LV_OBJ_FLAG_HIDDEN);
    }
}

void St7365pLcdDisplay::SetPreviewImage(std::unique_ptr<LvglImage> image) { (void)image; }

void St7365pLcdDisplay::SetEmotion(const char* emotion) { (void)emotion; }
