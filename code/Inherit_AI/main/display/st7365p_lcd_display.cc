#include "display/st7365p_lcd_display.h"

#include <cstring>

bool St7365pLcdDisplay::IsEnabled() { return true; }

const char* St7365pLcdDisplay::DefaultPrompt() { return "System ready. Select app."; }

bool St7365pLcdDisplay::ShouldDisplayRole(const char* role) {
    return role != nullptr && std::strcmp(role, "system") == 0;
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
    if (bottom_bar_ != nullptr) {
        lv_obj_remove_flag(bottom_bar_, LV_OBJ_FLAG_HIDDEN);
    }
    if (chat_message_label_ != nullptr) {
        lv_label_set_long_mode(chat_message_label_, LV_LABEL_LONG_SCROLL_CIRCULAR);
        lv_obj_set_style_anim_duration(chat_message_label_, 4500, 0);
        lv_label_set_text(chat_message_label_, DefaultPrompt());
    }
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
