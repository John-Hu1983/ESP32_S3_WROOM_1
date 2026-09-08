#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_err.h"

#include "freertos/semphr.h"

#include "user/inc/user_config.h"
#include "user/assets/access_assets.h"

#ifdef __cplusplus
extern "C" {
#endif

// clang-format off
#define ZZJX2R_UART_TX_BUFFER_SIZE_DEFAULT (2048)
#define ZZJX2R_UART_RX_BUFFER_SIZE_DEFAULT (256)
#define ZZJX2R_UART_QUEUE_SIZE_DEFAULT     (0)
#define ZZJX2R_WRITE_TIMEOUT_MS_DEFAULT    (300)
#define ZZJX2R_LOCK_TIMEOUT_MS_DEFAULT     (1000)
#define ZZJX2R_MAX_BARCODE_BYTES           (255U)
#define ZZJX2R_STATUS_DETECT_RESPONSE_LEN  (6U)
#define ZZJX2R_BULK_WRITE_CHUNK_BYTES      (256U)

#define ZZJX2R_PRINT_MODE_FONT_B      (0x01U)
#define ZZJX2R_PRINT_MODE_EMPHASIZED  (0x08U)
#define ZZJX2R_PRINT_MODE_DOUBLE_H    (0x10U)
#define ZZJX2R_PRINT_MODE_DOUBLE_W    (0x20U)
#define ZZJX2R_PRINT_MODE_UNDERLINE   (0x80U)
// clang-format on

typedef enum {
    ZZJX2R_JUSTIFY_LEFT = 0,
    ZZJX2R_JUSTIFY_CENTER,
    ZZJX2R_JUSTIFY_RIGHT,
} zzjx2r_justification_t;

typedef enum {
    ZZJX2R_UNDERLINE_OFF = 0,
    ZZJX2R_UNDERLINE_THIN,
    ZZJX2R_UNDERLINE_THICK,
} zzjx2r_underline_t;

typedef enum {
    ZZJX2R_CUT_FULL = 0,
    ZZJX2R_CUT_PARTIAL,
} zzjx2r_cut_mode_t;

typedef enum {
    ZZJX2R_HRI_NOT_PRINTED = 0,
    ZZJX2R_HRI_ABOVE,
    ZZJX2R_HRI_BELOW,
    ZZJX2R_HRI_ABOVE_AND_BELOW,
} zzjx2r_hri_position_t;

typedef enum {
    ZZJX2R_HRI_FONT_A = 0,
    ZZJX2R_HRI_FONT_B,
} zzjx2r_hri_font_t;

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
} zzjx2r_ctx_t;

typedef struct {
    uint8_t tph_temperature_celsius;
    uint16_t paper_detect_raw;
    uint16_t working_voltage_raw;
} zzjx2r_detect_status_t;

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
} zzjx2r_config_t;

void zzjx2r_get_default_config(zzjx2r_config_t* out_cfg);

esp_err_t zzjx2r_init(const zzjx2r_config_t* config);
esp_err_t zzjx2r_deinit(void);
bool zzjx2r_is_ready(void);

esp_err_t zzjx2r_write_raw(const uint8_t* data, size_t data_len);
esp_err_t zzjx2r_write_text(const char* text);
esp_err_t zzjx2r_write_line(const char* text);

esp_err_t zzjx2r_cmd_detect_status(zzjx2r_detect_status_t* out_status);

/* printer command APIs */
esp_err_t zzjx2r_cmd_clear_printer(void);
esp_err_t zzjx2r_cmd_print_and_feed_lines(uint8_t lines);
esp_err_t zzjx2r_cmd_print_and_feed_dots(uint8_t dots);
esp_err_t zzjx2r_cmd_set_justification(zzjx2r_justification_t mode);
esp_err_t zzjx2r_cmd_select_print_mode(uint8_t mode_mask);
esp_err_t zzjx2r_cmd_set_character_size(uint8_t width, uint8_t height);
esp_err_t zzjx2r_cmd_set_bold(bool enable);
esp_err_t zzjx2r_cmd_set_underline(zzjx2r_underline_t mode);
esp_err_t zzjx2r_cmd_set_inverse(bool enable);
esp_err_t zzjx2r_cmd_set_upside_down(bool enable);
esp_err_t zzjx2r_cmd_set_line_spacing(uint8_t spacing_dots);
esp_err_t zzjx2r_cmd_reset_line_spacing(void);
esp_err_t zzjx2r_cmd_cut_paper(zzjx2r_cut_mode_t mode);
esp_err_t zzjx2r_cmd_set_hri_position(zzjx2r_hri_position_t position);
esp_err_t zzjx2r_cmd_set_hri_font(zzjx2r_hri_font_t font);
esp_err_t zzjx2r_cmd_set_barcode_height(uint8_t height_dots);
esp_err_t zzjx2r_cmd_set_barcode_width(uint8_t width_dots);
esp_err_t zzjx2r_cmd_print_barcode_code128(const uint8_t* data, size_t data_len);
esp_err_t zzjx2r_print_via_bin(const char* bin_name);

#ifdef __cplusplus
}
#endif
