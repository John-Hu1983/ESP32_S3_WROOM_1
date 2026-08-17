#pragma once

#include "display/lcd_display.h"

#include <cstdint>
#include <vector>

class St7365pLcdDisplay : public SpiLcdDisplay {
public:
    struct GridStyle {
        const lv_font_t* icon_font;
        const lv_font_t* text_font;
        lv_color_t text_color;
        lv_color_t tile_color;
        int spacing;
    };

    St7365pLcdDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel, int width,
                      int height, int offset_x, int offset_y, bool mirror_x, bool mirror_y,
                      bool swap_xy)
        : SpiLcdDisplay(panel_io, panel, width, height, offset_x, offset_y, mirror_x, mirror_y,
                        swap_xy) {}

    static bool IsEnabled();
    static const char* DefaultPrompt();
    static bool ShouldDisplayRole(const char* role);
    static void BuildGrid(lv_obj_t* parent, const GridStyle& style,
                          std::vector<lv_obj_t*>* out_tiles = nullptr);

    void SetupUI() override;
    void SetTheme(Theme* theme) override;
    void SetChatMessage(const char* role, const char* content) override;
    void ClearChatMessages() override;
    void SetPreviewImage(std::unique_ptr<LvglImage> image) override;
    void SetEmotion(const char* emotion) override;

private:
    void ApplyDesktopChromeLocked();
};

// Keep compatibility with existing LCD code while removing desktop-specific implementation.
using DesktopSpiLcdDisplay = St7365pLcdDisplay;
