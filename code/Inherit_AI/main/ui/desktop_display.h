#pragma once

#include "display/lcd_display.h"

#include <cstdint>

class DesktopSpiLcdDisplay : public SpiLcdDisplay {
public:
    struct GridStyle {
        const lv_font_t* icon_font;
        const lv_font_t* text_font;
        lv_color_t text_color;
        lv_color_t tile_color;
        int spacing;
    };

    DesktopSpiLcdDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel,
                         int width, int height, int offset_x, int offset_y, bool mirror_x,
                         bool mirror_y, bool swap_xy)
        : SpiLcdDisplay(panel_io, panel, width, height, offset_x, offset_y, mirror_x, mirror_y,
                        swap_xy) {}

    static bool IsEnabled();
    static const char* DefaultPrompt();
    static bool ShouldDisplayRole(const char* role);
    static void BuildGrid(lv_obj_t* parent, const GridStyle& style,
                          std::vector<lv_obj_t*>* out_tiles = nullptr);

    void SetupUI() override;
    void SetChatMessage(const char* role, const char* content) override;
    void ClearChatMessages() override;
    void SetEmotion(const char* emotion) override;
    void SetPreviewImage(std::unique_ptr<LvglImage> image) override;
    void SetTheme(Theme* theme) override;
    bool HasSelectableControls() override;
    bool SelectNextControl() override;
    bool SelectPreviousControl() override;

private:
    struct DesktopAppItem {
        const char* symbol;
        const char* name;
        uint32_t color_hex;
    };

    static constexpr bool kDesktopHomeEnabled = false;
    static constexpr const char* kDefaultPrompt = "System ready. Select app.";

    static lv_style_selector_t MainSelector();
    static lv_state_t MergeStates(lv_state_t a, lv_state_t b);
    static lv_style_selector_t MainStateSelector(lv_state_t state);
    static const lv_font_t* ResolveAppNameFont(const lv_font_t* fallback_font);
    void RefreshSelectionLocked();

    static const DesktopAppItem kDesktopApps[];
    std::vector<lv_obj_t*> desktop_tiles_;
    int selected_tile_index_ = -1;
};
