#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_err.h"

#include "freertos/semphr.h"

#include "bsp_global.h"
#include "user/common/user_common.h"
#include "user/inc/user_config.h"
#include "user/assets/access_assets.h"

#ifdef __cplusplus
extern "C" {
#endif

// clang-format off
#define PRINTER_UART_TX_BUFFER_SIZE_DEFAULT (2048)
#define PRINTER_UART_RX_BUFFER_SIZE_DEFAULT (256)
#define PRINTER_UART_QUEUE_SIZE_DEFAULT     (0)
#define PRINTER_WRITE_TIMEOUT_MS_DEFAULT    (300)
#define PRINTER_LOCK_TIMEOUT_MS_DEFAULT     (1000)
#define PRINTER_MAX_BARCODE_BYTES           (255U)
#define PRINTER_STATUS_DETECT_RESPONSE_LEN  (6U)
#define PRINTER_BULK_WRITE_CHUNK_BYTES      (256U)

#define PRINTER_PRINT_MODE_FONT_B      (0x01U)
#define PRINTER_PRINT_MODE_EMPHASIZED  (0x08U)
#define PRINTER_PRINT_MODE_DOUBLE_H    (0x10U)
#define PRINTER_PRINT_MODE_DOUBLE_W    (0x20U)
#define PRINTER_PRINT_MODE_UNDERLINE   (0x80U)
// clang-format on

typedef enum {
    PRINTER_JUSTIFY_LEFT = 0,
    PRINTER_JUSTIFY_CENTER,
    PRINTER_JUSTIFY_RIGHT,
} printer_justification_t;

typedef enum {
    PRINTER_UNDERLINE_OFF = 0,
    PRINTER_UNDERLINE_THIN,
    PRINTER_UNDERLINE_THICK,
} printer_underline_t;

typedef enum {
    PRINTER_CUT_FULL = 0,
    PRINTER_CUT_PARTIAL,
} printer_cut_mode_t;

typedef enum {
    PRINTER_HRI_NOT_PRINTED = 0,
    PRINTER_HRI_ABOVE,
    PRINTER_HRI_BELOW,
    PRINTER_HRI_ABOVE_AND_BELOW,
} printer_hri_position_t;

typedef enum {
    PRINTER_HRI_FONT_A = 0,
    PRINTER_HRI_FONT_B,
} printer_hri_font_t;

typedef struct {
    bool initialized;
    uart_port_t uart_port;
    gpio_num_t tx_io_num;
    gpio_num_t rx_io_num;
    gpio_num_t rts_io_num;
    gpio_num_t cts_io_num;
    uint32_t write_timeout_ms;
    uint32_t lock_timeout_ms;
    SemaphoreHandle_t lock;
} printer_ctx_t;

typedef struct {
    uint8_t tph_temperature_celsius;
    uint16_t paper_detect_raw;
    uint16_t working_voltage_raw;
} printer_detect_status_t;

typedef struct {
    uart_port_t uart_port;
    int baud_rate;
    gpio_num_t tx_io_num;
    gpio_num_t rx_io_num;
    gpio_num_t rts_io_num;
    gpio_num_t cts_io_num;
    int tx_buffer_size;
    int rx_buffer_size;
    int queue_size;
    uint32_t write_timeout_ms;
    uint32_t lock_timeout_ms;
} printer_cfg_t;

void printer_get_default_config(printer_cfg_t* out_cfg);

esp_err_t printer_init(const printer_cfg_t* config);
esp_err_t printer_deinit(void);
bool printer_is_ready(void);

esp_err_t printer_write_string(const char* text);

esp_err_t printer_detect_status(printer_detect_status_t* out_status);

/* printer command APIs */
esp_err_t printer_clear_cache(void);
esp_err_t printer_feed_lines(uint8_t lines);
esp_err_t printer_print_and_feed_dots(uint8_t dots);
esp_err_t printer_set_justification(printer_justification_t mode);
esp_err_t printer_select_print_mode(uint8_t mode_mask);
esp_err_t printer_set_character_size(uint8_t width, uint8_t height);
esp_err_t printer_set_bold(bool enable);
esp_err_t printer_set_underline(printer_underline_t mode);
esp_err_t printer_set_inverse(bool enable);
esp_err_t printer_set_upside_down(bool enable);
esp_err_t printer_set_line_spacing(uint8_t spacing_dots);
esp_err_t printer_reset_line_spacing(void);
esp_err_t printer_cut_paper(printer_cut_mode_t mode);
esp_err_t printer_set_hri_position(printer_hri_position_t position);
esp_err_t printer_set_hri_font(printer_hri_font_t font);
esp_err_t printer_set_barcode_height(uint8_t height_dots);
esp_err_t printer_set_barcode_width(uint8_t width_dots);
esp_err_t printer_barcode_code128(const uint8_t* data, size_t data_len);
esp_err_t printer_image_via_bin(const char* bin_name);

#ifdef __cplusplus
}
#endif
