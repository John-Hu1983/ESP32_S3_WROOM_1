#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include <string.h>

#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "hal/usr_spi.h"
#include "peripherals/gpba02b.h"
#include "user_config.h"

#define ST7365P_CMD_SWRESET 0x01U
#define ST7365P_CMD_SLPIN 0x10U
#define ST7365P_CMD_SLPOUT 0x11U
#define ST7365P_CMD_INVOFF 0x20U
#define ST7365P_CMD_INVON 0x21U
#define ST7365P_CMD_DISPOFF 0x28U
#define ST7365P_CMD_DISPON 0x29U
#define ST7365P_CMD_CASET 0x2AU
#define ST7365P_CMD_RASET 0x2BU
#define ST7365P_CMD_RAMWR 0x2CU
#define ST7365P_CMD_MADCTL 0x36U
#define ST7365P_CMD_COLMOD 0x3AU

#define ST7365P_MADCTL_MY 0x80U
#define ST7365P_MADCTL_MX 0x40U
#define ST7365P_MADCTL_MV 0x20U
#define ST7365P_MADCTL_BGR 0x08U

#define ST7365P_BYTES_PER_PIXEL 2U
#define ST7365P_SPI_TX_CHUNK_BYTES 4096U
#define ST7365P_SPI_TX_QUEUE_DEPTH 4U
#define ST7365P_FILL_TX_BYTES 2048u

typedef struct
{
    uint16_t width;
    uint16_t height;
    uint16_t x_offset;
    uint16_t y_offset;
    uint8_t madctl;
    uint8_t colmod;
    bool invert_color;
} st7365p_cfg_t;

typedef struct
{
    usr_spi_s spi;
    st7365p_cfg_t cfg;
    uint16_t hor_res;
    uint16_t ver_res;
    uint8_t madctl_base;
    bool spi_ready;
    bool panel_ready;
} st7365p_state_t;

/* Fill cfg with default LCD panel parameters from board config. */
void st7365p_get_default_cfg(st7365p_cfg_t *cfg);

/* Fully initialize panel, SPI link, reset sequence, and basic display settings. */
esp_err_t st7365p_panel_init(const st7365p_cfg_t *cfg);
/* Toggle panel reset pin with required timing delays. */
esp_err_t st7365p_reset_sequence(void);
/* Exit sleep mode and wait until the panel is ready. */
esp_err_t st7365p_sleep_out(void);
/* Turn panel display output on. */
esp_err_t st7365p_display_on(void);
/* Turn panel display output off. */
esp_err_t st7365p_display_off(void);
/* Apply one of four rotation states and update logical resolution. */
esp_err_t st7365p_set_rotation(uint8_t rotation);
/* Set active draw window and prepare panel RAM write mode. */
esp_err_t st7365p_set_window(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2);
/* Draw an RGB565 bitmap into the selected rectangle. */
esp_err_t st7365p_draw_bitmap(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, const void *rgb565_data);
/* Fill panel RAM with one RGB565 color for a given pixel count. */
esp_err_t st7365p_fill_color(uint16_t rgb565, uint32_t pixel_count);
/* LVGL flush helper that clips area and streams pixel rows to the panel. */
esp_err_t st7365p_lvgl_flush(int32_t x1, int32_t y1, int32_t x2, int32_t y2, const void *color_map);
/* Return current logical panel resolution after rotation. */
void st7365p_get_resolution(uint16_t *width, uint16_t *height);
/* Return true when panel initialization has completed successfully. */
bool st7365p_is_ready(void);

/* Backward-compatible alias that initializes panel with default config. */
esp_err_t st7365p_init_device(void);
/* Backward-compatible alias for reset sequence API. */
esp_err_t st7365p_reset_sequency(void);
