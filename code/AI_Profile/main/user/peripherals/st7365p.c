#include "st7365p.h"

#define TAG "ST7365P"

static st7365p_state_t s_st7365p = {0};

/*
 * brief: Delay for the requested milliseconds.
 * input: ms - delay time in milliseconds.
 * output: none.
 */
static void _st7365p_delay_ms(uint32_t ms) {
    if (ms == 0U) {
        return;
    }
    vTaskDelay(pdMS_TO_TICKS(ms));
}

/*
 * brief: Configure LCD RS pin as push-pull output and set default level to data mode.
 * input: none.
 * output: ESP_OK on success; otherwise GPIO configuration error or invalid argument.
 */
static esp_err_t _st7365p_init_rs_pin(void) {
    gpio_config_t io_cfg = {0};

    if ((LCD_IO_RS == GPIO_NUM_NC) || !GPIO_IS_VALID_OUTPUT_GPIO(LCD_IO_RS)) {
        return ESP_ERR_INVALID_ARG;
    }

    io_cfg.intr_type = GPIO_INTR_DISABLE;
    io_cfg.mode = GPIO_MODE_OUTPUT;
    io_cfg.pin_bit_mask = (1ULL << (uint32_t)LCD_IO_RS);
    io_cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_cfg.pull_up_en = GPIO_PULLUP_DISABLE;

    esp_err_t ret = gpio_config(&io_cfg);
    if (ret != ESP_OK) {
        return ret;
    }

    return gpio_set_level(LCD_IO_RS, 1);
}

/*
 * brief: Set LCD RS line state used to select command or data phase.
 * input: level - 0 for command, non-zero for data.
 * output: ESP_OK on success; otherwise GPIO driver error.
 */
static esp_err_t _st7365p_set_rs_level(uint32_t level) {
    return gpio_set_level(LCD_IO_RS, (int)level);
}

/*
 * brief: Write bytes to LCD over SPI in bounded chunks.
 * input: data - source byte buffer; len - number of bytes to transmit.
 * output: ESP_OK on success; otherwise ESP_ERR_INVALID_ARG/STATE or SPI driver error.
 */
static esp_err_t _st7365p_write_bytes(const void* data, size_t len) {
    const uint8_t* p = (const uint8_t*)data;

    if ((p == NULL) && (len > 0U)) {
        return ESP_ERR_INVALID_ARG;
    }

    if (len == 0U) {
        return ESP_OK;
    }

    if (!s_st7365p.spi_ready || (s_st7365p.spi == NULL)) {
        return ESP_ERR_INVALID_STATE;
    }

    size_t remain = len;
    while (remain > 0U) {
        size_t chunk = (remain > ST7365P_SPI_TX_CHUNK_BYTES) ? ST7365P_SPI_TX_CHUNK_BYTES : remain;
        spi_transaction_t t = {
            .flags = 0,
            .length = (uint32_t)(chunk * 8U),
            .rxlength = 0,
            .tx_buffer = p,
            .rx_buffer = NULL,
        };

        esp_err_t ret = spi_device_transmit(s_st7365p.spi, &t);
        if (ret != ESP_OK) {
            return ret;
        }

        p += chunk;
        remain -= chunk;
    }

    return ESP_OK;
}

/*
 * brief: Send one LCD command byte.
 * input: cmd - command value.
 * output: ESP_OK on success; otherwise GPIO/SPI driver error.
 */
static esp_err_t _st7365p_write_cmd(uint8_t cmd) {
    esp_err_t ret = _st7365p_set_rs_level(0);
    if (ret != ESP_OK) {
        return ret;
    }

    return _st7365p_write_bytes(&cmd, 1);
}

/*
 * brief: Send LCD payload bytes in data mode.
 * input: data - payload buffer; len - payload byte count.
 * output: ESP_OK on success; otherwise ESP_ERR_INVALID_ARG or GPIO/SPI driver error.
 */
static esp_err_t _st7365p_write_data(const void* data, size_t len) {
    if ((data == NULL) && (len > 0U)) {
        return ESP_ERR_INVALID_ARG;
    }

    if (len == 0U) {
        return ESP_OK;
    }

    esp_err_t ret = _st7365p_set_rs_level(1);
    if (ret != ESP_OK) {
        return ret;
    }

    return _st7365p_write_bytes(data, len);
}

/*
 * brief: Send one command followed immediately by an optional data payload.
 * input: cmd - command value; data - payload buffer; len - payload byte count.
 * output: ESP_OK on success; otherwise GPIO/SPI driver error.
 */
static esp_err_t _st7365p_write_cmd_data(uint8_t cmd, const void* data, size_t len) {
    esp_err_t ret = _st7365p_write_cmd(cmd);
    if (ret != ESP_OK) {
        return ret;
    }

    return _st7365p_write_data(data, len);
}

/*
 * brief: Release SPI device and bus resources if owned by this module.
 * input: none.
 * output: none.
 */
static void _st7365p_release_spi(void) {
    if (s_st7365p.spi != NULL) {
        (void)spi_bus_remove_device(s_st7365p.spi);
        s_st7365p.spi = NULL;
    }

    if (s_st7365p.bus_initialized) {
        (void)spi_bus_free(s_st7365p.spi_host);
        s_st7365p.bus_initialized = false;
    }

    s_st7365p.spi_ready = false;
}

/*
 * brief: Lazily initialize LCD SPI device and RS GPIO resources.
 * input: none.
 * output: ESP_OK when resources are ready; otherwise initialization error.
 */
static esp_err_t _st7365p_ensure_spi_ready(void) {
    if (s_st7365p.spi_ready) {
        return ESP_OK;
    }

    spi_bus_config_t bus_cfg = {
        .mosi_io_num = LCD_IO_MOSI,
        .miso_io_num = LCD_IO_MISO,
        .sclk_io_num = LCD_IO_CLK,
        .quadwp_io_num = GPIO_NUM_NC,
        .quadhd_io_num = GPIO_NUM_NC,
        .max_transfer_sz = ST7365P_SPI_TX_CHUNK_BYTES,
        .intr_flags = 0,
    };

    esp_err_t ret = spi_bus_initialize(LCD_SPI_HOST, &bus_cfg, SPI_DMA_CH_AUTO);
    if (ret == ESP_OK) {
        s_st7365p.bus_initialized = true;
    } else if (ret == ESP_ERR_INVALID_STATE) {
        s_st7365p.bus_initialized = false;
    } else {
        ESP_LOGE(TAG, "spi_bus_initialize failed: %d", (int)ret);
        return ret;
    }

    spi_device_interface_config_t dev_cfg = {
        .command_bits = 0,
        .address_bits = 0,
        .dummy_bits = 0,
        .mode = 0,
        .duty_cycle_pos = 128,
        .cs_ena_pretrans = 0,
        .cs_ena_posttrans = 0,
        .clock_speed_hz = LCD_DEFAULT_CLOCK_HZ,
        .input_delay_ns = 0,
        .spics_io_num = LCD_IO_CS,
        .flags = 0,
        .queue_size = ST7365P_SPI_TX_QUEUE_DEPTH,
        .pre_cb = NULL,
        .post_cb = NULL,
    };

    ret = spi_bus_add_device(LCD_SPI_HOST, &dev_cfg, &s_st7365p.spi);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "spi_bus_add_device failed: %d", (int)ret);
        if (s_st7365p.bus_initialized) {
            (void)spi_bus_free(LCD_SPI_HOST);
            s_st7365p.bus_initialized = false;
        }
        return ret;
    }

    ret = _st7365p_init_rs_pin();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "LCD RS pin init failed: %d", (int)ret);
        _st7365p_release_spi();
        return ret;
    }

    s_st7365p.spi_host = LCD_SPI_HOST;
    s_st7365p.spi_ready = true;
    return ESP_OK;
}

/*
 * brief: Convert logical rotation index to MADCTL bitfield while preserving BGR configuration.
 * input: rotation - orientation index; madctl_base - base MADCTL value from configuration.
 * output: Encoded MADCTL byte for panel register programming.
 */
static uint8_t _st7365p_rotation_to_madctl(uint8_t rotation, uint8_t madctl_base) {
    uint8_t base = (uint8_t)(madctl_base &
                             (ST7365P_MADCTL_MY | ST7365P_MADCTL_MX | ST7365P_MADCTL_MV |
                              ST7365P_MADCTL_BGR));
    uint8_t delta = 0;

    switch (rotation & 0x03U) {
        case 0:
            delta = 0;
            break;
        case 1:
            delta = (uint8_t)(ST7365P_MADCTL_MV | ST7365P_MADCTL_MX);
            break;
        case 2:
            delta = (uint8_t)(ST7365P_MADCTL_MX | ST7365P_MADCTL_MY);
            break;
        default:
            delta = (uint8_t)(ST7365P_MADCTL_MV | ST7365P_MADCTL_MY);
            break;
    }

    return (uint8_t)(base ^ delta);
}

/*
 * brief: Fill configuration structure with default panel parameters.
 * input: cfg - output configuration pointer.
 * output: none.
 */
void st7365p_get_default_cfg(st7365p_cfg_t* cfg) {
    if (cfg == NULL) {
        return;
    }

    cfg->width = LCD_DEFAULT_WIDTH;
    cfg->height = LCD_DEFAULT_HEIGHT;
    cfg->x_offset = 0;
    cfg->y_offset = 0;
    cfg->madctl = LCD_DEFAULT_MADCTL;
    cfg->colmod = 0x55;
    cfg->invert_color = LCD_DEFAULT_INVERT_COLOR;
}

/*
 * brief: Query whether panel initialization has completed.
 * input: none.
 * output: true when panel is ready; otherwise false.
 */
bool st7365p_is_ready(void) {
    return s_st7365p.panel_ready;
}

/*
 * brief: Return current logical resolution after applying active rotation.
 * input: width - output width pointer; height - output height pointer.
 * output: none.
 */
void st7365p_get_resolution(uint16_t* width, uint16_t* height) {
    if (width != NULL) {
        *width = s_st7365p.hor_res;
    }

    if (height != NULL) {
        *height = s_st7365p.ver_res;
    }
}

/*
 * brief: Execute panel hardware reset timing sequence through BSP helper.
 * input: none.
 * output: ESP_OK on success.
 */
esp_err_t st7365p_reset_sequence(void) {
    bsp_lcd_reset_sequence();
    return ESP_OK;
}

/*
 * brief: Backward-compatible wrapper for historical misspelled reset API name.
 * input: none.
 * output: ESP_OK on success; otherwise propagated reset-sequence error.
 */
esp_err_t st7365p_reset_sequency(void) {
    return st7365p_reset_sequence();
}

/*
 * brief: Send sleep-out command and wait for panel internal wake-up.
 * input: none.
 * output: ESP_OK on success; otherwise state or SPI/GPIO error.
 */
esp_err_t st7365p_sleep_out(void) {
    esp_err_t ret = _st7365p_ensure_spi_ready();
    if (ret != ESP_OK) {
        return ret;
    }

    ret = _st7365p_write_cmd(ST7365P_CMD_SLPOUT);
    if (ret != ESP_OK) {
        return ret;
    }

    _st7365p_delay_ms(120);
    return ESP_OK;
}

/*
 * brief: Enable panel display output.
 * input: none.
 * output: ESP_OK on success; otherwise state or SPI/GPIO error.
 */
esp_err_t st7365p_display_on(void) {
    esp_err_t ret = _st7365p_ensure_spi_ready();
    if (ret != ESP_OK) {
        return ret;
    }

    return _st7365p_write_cmd(ST7365P_CMD_DISPON);
}

/*
 * brief: Disable panel display output.
 * input: none.
 * output: ESP_OK on success; otherwise state or SPI/GPIO error.
 */
esp_err_t st7365p_display_off(void) {
    esp_err_t ret = _st7365p_ensure_spi_ready();
    if (ret != ESP_OK) {
        return ret;
    }

    return _st7365p_write_cmd(ST7365P_CMD_DISPOFF);
}

/*
 * brief: Apply panel rotation and update cached logical resolution.
 * input: rotation - orientation index (0..3, modulo applied internally).
 * output: ESP_OK on success; otherwise ESP_ERR_INVALID_STATE or SPI error.
 */
esp_err_t st7365p_set_rotation(uint8_t rotation) {
    if (!s_st7365p.panel_ready) {
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t madctl = _st7365p_rotation_to_madctl(rotation, s_st7365p.madctl_base);
    esp_err_t ret = _st7365p_write_cmd_data(ST7365P_CMD_MADCTL, &madctl, 1);
    if (ret != ESP_OK) {
        return ret;
    }

    if ((rotation & 0x01U) == 0U) {
        s_st7365p.hor_res = s_st7365p.cfg.width;
        s_st7365p.ver_res = s_st7365p.cfg.height;
    } else {
        s_st7365p.hor_res = s_st7365p.cfg.height;
        s_st7365p.ver_res = s_st7365p.cfg.width;
    }

    return ESP_OK;
}

/*
 * brief: Program draw window (CASET/RASET) and enter RAM write mode.
 * input: x1/y1 - top-left coordinate; x2/y2 - bottom-right coordinate.
 * output: ESP_OK on success; otherwise ESP_ERR_INVALID_ARG/STATE or SPI error.
 */
esp_err_t st7365p_set_window(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2) {
    if (!s_st7365p.panel_ready) {
        return ESP_ERR_INVALID_STATE;
    }

    if ((x1 > x2) || (y1 > y2) || (x2 >= s_st7365p.hor_res) || (y2 >= s_st7365p.ver_res)) {
        return ESP_ERR_INVALID_ARG;
    }

    uint16_t xs = (uint16_t)(x1 + s_st7365p.cfg.x_offset);
    uint16_t xe = (uint16_t)(x2 + s_st7365p.cfg.x_offset);
    uint16_t ys = (uint16_t)(y1 + s_st7365p.cfg.y_offset);
    uint16_t ye = (uint16_t)(y2 + s_st7365p.cfg.y_offset);

    uint8_t col_data[4] = {(uint8_t)(xs >> 8), (uint8_t)(xs & 0xFFU), (uint8_t)(xe >> 8),
                           (uint8_t)(xe & 0xFFU)};
    uint8_t row_data[4] = {(uint8_t)(ys >> 8), (uint8_t)(ys & 0xFFU), (uint8_t)(ye >> 8),
                           (uint8_t)(ye & 0xFFU)};

    esp_err_t ret = _st7365p_write_cmd_data(ST7365P_CMD_CASET, col_data, sizeof(col_data));
    if (ret != ESP_OK) {
        return ret;
    }

    ret = _st7365p_write_cmd_data(ST7365P_CMD_RASET, row_data, sizeof(row_data));
    if (ret != ESP_OK) {
        return ret;
    }

    return _st7365p_write_cmd(ST7365P_CMD_RAMWR);
}

/*
 * brief: Draw one RGB565 bitmap rectangle to panel GRAM.
 * input: x1/y1/x2/y2 - destination rectangle; rgb565_data - source pixel buffer.
 * output: ESP_OK on success; otherwise ESP_ERR_INVALID_ARG/STATE or SPI error.
 */
esp_err_t st7365p_draw_bitmap(uint16_t x1,
                              uint16_t y1,
                              uint16_t x2,
                              uint16_t y2,
                              const void* rgb565_data) {
    if (rgb565_data == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    uint32_t pixel_count = (uint32_t)(x2 - x1 + 1U) * (uint32_t)(y2 - y1 + 1U);
    size_t bytes = (size_t)pixel_count * ST7365P_BYTES_PER_PIXEL;

    esp_err_t ret = st7365p_set_window(x1, y1, x2, y2);
    if (ret != ESP_OK) {
        return ret;
    }

    return _st7365p_write_data(rgb565_data, bytes);
}

/*
 * brief: Fill panel GRAM stream with a repeated RGB565 color for a target pixel count.
 * input: rgb565 - color value; pixel_count - number of pixels to write.
 * output: ESP_OK on success; otherwise ESP_ERR_INVALID_STATE or SPI error.
 */
esp_err_t st7365p_fill_color(uint16_t rgb565, uint32_t pixel_count) {
    static uint8_t fill_chunk[ST7365P_FILL_TX_BYTES];
    uint8_t hi = (uint8_t)(rgb565 >> 8);
    uint8_t lo = (uint8_t)(rgb565 & 0xFFU);
    uint32_t fill_chunk_pixels = ST7365P_FILL_TX_BYTES / ST7365P_BYTES_PER_PIXEL;
    uint32_t remain = pixel_count;

    if (!s_st7365p.panel_ready) {
        return ESP_ERR_INVALID_STATE;
    }

    for (uint32_t i = 0; i < fill_chunk_pixels; i++) {
        fill_chunk[(i * 2U)] = hi;
        fill_chunk[(i * 2U) + 1U] = lo;
    }

    while (remain > 0U) {
        uint32_t chunk_pixels = (remain > fill_chunk_pixels) ? fill_chunk_pixels : remain;
        esp_err_t ret =
            _st7365p_write_data(fill_chunk, (size_t)(chunk_pixels * ST7365P_BYTES_PER_PIXEL));
        if (ret != ESP_OK) {
            return ret;
        }
        remain -= chunk_pixels;
    }

    return ESP_OK;
}

/*
 * brief: Flush LVGL draw area to panel with clipping and row-wise transfer.
 * input: x1/y1/x2/y2 - LVGL area; color_map - RGB565 source buffer.
 * output: ESP_OK on success; otherwise ESP_ERR_INVALID_ARG/STATE or SPI error.
 */
esp_err_t st7365p_lvgl_flush(int32_t x1,
                             int32_t y1,
                             int32_t x2,
                             int32_t y2,
                             const void* color_map) {
    if (!s_st7365p.panel_ready) {
        return ESP_ERR_INVALID_STATE;
    }

    if ((color_map == NULL) || (x1 > x2) || (y1 > y2)) {
        return ESP_ERR_INVALID_ARG;
    }

    int32_t clip_x1 = (x1 < 0) ? 0 : x1;
    int32_t clip_y1 = (y1 < 0) ? 0 : y1;
    int32_t clip_x2 = (x2 >= (int32_t)s_st7365p.hor_res) ? ((int32_t)s_st7365p.hor_res - 1) : x2;
    int32_t clip_y2 = (y2 >= (int32_t)s_st7365p.ver_res) ? ((int32_t)s_st7365p.ver_res - 1) : y2;

    if ((clip_x1 > clip_x2) || (clip_y1 > clip_y2)) {
        return ESP_OK;
    }

    int32_t src_w = (x2 - x1 + 1);
    int32_t copy_w = (clip_x2 - clip_x1 + 1);
    int32_t copy_h = (clip_y2 - clip_y1 + 1);
    int32_t start_src_x = (clip_x1 - x1);
    int32_t start_src_y = (clip_y1 - y1);
    size_t bytes_per_row = (size_t)copy_w * ST7365P_BYTES_PER_PIXEL;

    esp_err_t ret =
        st7365p_set_window((uint16_t)clip_x1, (uint16_t)clip_y1, (uint16_t)clip_x2, (uint16_t)clip_y2);
    if (ret != ESP_OK) {
        return ret;
    }

    const uint8_t* src = (const uint8_t*)color_map;
    for (int32_t row = 0; row < copy_h; row++) {
        const uint8_t* row_ptr =
            src +
            (size_t)(((start_src_y + row) * src_w + start_src_x) * ST7365P_BYTES_PER_PIXEL);
        ret = _st7365p_write_data(row_ptr, bytes_per_row);
        if (ret != ESP_OK) {
            return ret;
        }
    }

    return ESP_OK;
}

/*
 * brief: Perform full panel bring-up from configuration to display-on state.
 * input: cfg - optional panel configuration; NULL uses default settings.
 * output: ESP_OK on success; otherwise argument, backend, GPIO, or SPI error.
 */
esp_err_t st7365p_panel_init(const st7365p_cfg_t* cfg) {
    st7365p_cfg_t active_cfg;

    if (cfg == NULL) {
        st7365p_get_default_cfg(&active_cfg);
    } else {
        active_cfg = *cfg;
    }

    if ((active_cfg.width == 0U) || (active_cfg.height == 0U)) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t ret = _st7365p_ensure_spi_ready();
    if (ret != ESP_OK) {
        return ret;
    }

    s_st7365p.cfg = active_cfg;
    s_st7365p.hor_res = active_cfg.width;
    s_st7365p.ver_res = active_cfg.height;
    s_st7365p.madctl_base = active_cfg.madctl;
    s_st7365p.panel_ready = false;

    ret = st7365p_reset_sequence();
    if (ret != ESP_OK) {
        return ret;
    }

    ret = _st7365p_write_cmd(ST7365P_CMD_SWRESET);
    if (ret != ESP_OK) {
        return ret;
    }
    _st7365p_delay_ms(120);

    ret = st7365p_sleep_out();
    if (ret != ESP_OK) {
        return ret;
    }

    ret = _st7365p_write_cmd_data(ST7365P_CMD_COLMOD, &active_cfg.colmod, 1);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = _st7365p_write_cmd_data(ST7365P_CMD_MADCTL, &active_cfg.madctl, 1);
    if (ret != ESP_OK) {
        return ret;
    }

    if (active_cfg.invert_color) {
        ret = _st7365p_write_cmd(ST7365P_CMD_INVON);
    } else {
        ret = _st7365p_write_cmd(ST7365P_CMD_INVOFF);
    }
    if (ret != ESP_OK) {
        return ret;
    }

    ret = st7365p_display_on();
    if (ret != ESP_OK) {
        return ret;
    }

    s_st7365p.panel_ready = true;
    return ESP_OK;
}

/*
 * brief: Initialize panel using default configuration values.
 * input: none.
 * output: ESP_OK on success; otherwise propagated panel-initialization error.
 */
esp_err_t st7365p_init_device(void) {
    return st7365p_panel_init(NULL);
}
