#pragma once

struct esp_lcd_panel_io_t;
using esp_lcd_panel_io_handle_t = esp_lcd_panel_io_t*;

struct esp_lcd_panel_t;
using esp_lcd_panel_handle_t = esp_lcd_panel_t*;

class Display;

Display* CreatePrimaryDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel,
                              int width, int height, int offset_x, int offset_y, bool mirror_x,
                              bool mirror_y, bool swap_xy);
