#include "dev_printer.h"

#define TAG "dev_printer"

static printer_ctx_t s_printer = {
    .initialized = false,
    .uart_port = UART_NUM_MAX,
    .tx_io_num = GPIO_NUM_NC,
    .rx_io_num = GPIO_NUM_NC,
    .rts_io_num = GPIO_NUM_NC,
    .cts_io_num = GPIO_NUM_NC,
    .write_timeout_ms = PRINTER_WRITE_TIMEOUT_MS_DEFAULT,
    .lock_timeout_ms = PRINTER_LOCK_TIMEOUT_MS_DEFAULT,
    .lock = NULL,
};

/*
 * brief : _printer_gpio_is_valid.
 * input : see parameters.
 * output: return value from this function.
 * type  : private
 */
static bool _printer_gpio_is_valid(gpio_num_t io_num)
{
    return io_num != GPIO_NUM_NC;
}

/*
 * brief : _printer_uart_pin_num.
 * input : see parameters.
 * output: return value from this function.
 * type  : private
 */
static int _printer_uart_pin_num(gpio_num_t io_num)
{
    if (_printer_gpio_is_valid(io_num)) {
        return (int)io_num;
    }
    return UART_PIN_NO_CHANGE;
}

/*
 * brief : _printer_release_io.
 * input : none.
 * output: none.
 * type  : private
 */
static void _printer_release_io(void)
{
    if (_printer_gpio_is_valid(s_printer.tx_io_num)) {
        (void)gpio_reset_pin(s_printer.tx_io_num);
    }
    if (_printer_gpio_is_valid(s_printer.rx_io_num)) {
        (void)gpio_reset_pin(s_printer.rx_io_num);
    }
    if (_printer_gpio_is_valid(s_printer.rts_io_num)) {
        (void)gpio_reset_pin(s_printer.rts_io_num);
    }
    if (_printer_gpio_is_valid(s_printer.cts_io_num)) {
        (void)gpio_reset_pin(s_printer.cts_io_num);
    }
}

/*
 * brief : _printer_release_io_from_config.
 * input : see parameters.
 * output: none.
 * type  : private
 */
static void _printer_release_io_from_config(const printer_config_t* cfg)
{
    if (cfg == NULL) {
        return;
    }

    if (_printer_gpio_is_valid(cfg->tx_io_num)) {
        (void)gpio_reset_pin(cfg->tx_io_num);
    }
    if (_printer_gpio_is_valid(cfg->rx_io_num)) {
        (void)gpio_reset_pin(cfg->rx_io_num);
    }
    if (_printer_gpio_is_valid(cfg->rts_io_num)) {
        (void)gpio_reset_pin(cfg->rts_io_num);
    }
    if (_printer_gpio_is_valid(cfg->cts_io_num)) {
        (void)gpio_reset_pin(cfg->cts_io_num);
    }
}

/*
 * brief : _printer_take_lock.
 * input : none.
 * output: return value from this function.
 * type  : private
 */
static esp_err_t _printer_take_lock(void)
{
    TickType_t wait_ticks = pdMS_TO_TICKS((TickType_t)s_printer.lock_timeout_ms);

    if (s_printer.lock == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    if (xSemaphoreTake(s_printer.lock, wait_ticks) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    return ESP_OK;
}

/*
 * brief : _printer_give_lock.
 * input : none.
 * output: none.
 * type  : private
 */
static void _printer_give_lock(void)
{
    if (s_printer.lock != NULL) {
        (void)xSemaphoreGive(s_printer.lock);
    }
}

/*
 * brief : _printer_write_locked.
 * input : see parameters.
 * output: return value from this function.
 * type  : private
 */
static esp_err_t _printer_write_locked(const uint8_t* data, size_t data_len)
{
    int written = 0;
    TickType_t wait_ticks = pdMS_TO_TICKS((TickType_t)s_printer.write_timeout_ms);

    if (!s_printer.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if ((data == NULL) && (data_len > 0U)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (data_len == 0U) {
        return ESP_OK;
    }

    written = uart_write_bytes(s_printer.uart_port, (const char*)data, data_len);
    if ((written < 0) || ((size_t)written != data_len)) {
        return ESP_FAIL;
    }

    return uart_wait_tx_done(s_printer.uart_port, wait_ticks);
}

/*
 * brief : _printer_send_cmd.
 * input : see parameters.
 * output: return value from this function.
 * type  : private
 */
static esp_err_t _printer_send_cmd(const uint8_t* cmd, size_t cmd_len)
{
    return printer_write_raw(cmd, cmd_len);
}

/*
 * brief : _printer_wait_flow_ready.
 * input : none.
 * output: return value from this function.
 * type  : private
 */
static esp_err_t _printer_wait_flow_ready(void)
{
#ifdef PRINTER_UART_DTR_GPIO
    if (_printer_gpio_is_valid(PRINTER_UART_DTR_GPIO)) {
        while (gpio_get_level(PRINTER_UART_DTR_GPIO) != 0) {
            delay_ms(10u);
        }
    }
#endif
    return ESP_OK;
}

/*
 * brief : _printer_write_chunked_locked.
 * input : see parameters.
 * output: return value from this function.
 * type  : private
 */
static esp_err_t
_printer_write_chunked_locked(const uint8_t* data, size_t data_len, size_t chunk_bytes)
{
    esp_err_t ret = ESP_OK;
    size_t offset = 0U;
    size_t chunk_len = 0U;

    if ((data == NULL) && (data_len > 0U)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (chunk_bytes == 0U) {
        return ESP_ERR_INVALID_ARG;
    }

    while (offset < data_len) {
        chunk_len = data_len - offset;
        if (chunk_len > chunk_bytes) {
            chunk_len = chunk_bytes;
        }

        ret = _printer_wait_flow_ready();
        if (ret != ESP_OK) {
            return ret;
        }

        ret = _printer_write_locked(&data[offset], chunk_len);
        if (ret != ESP_OK) {
            return ret;
        }
        offset += chunk_len;
    }

    return ESP_OK;
}

/*
 * brief : _printer_read_u16_le.
 * input : see parameters.
 * output: return value from this function.
 * type  : private
 */
static uint16_t _printer_read_u16_le(const uint8_t* data)
{
    if (data == NULL) {
        return 0U;
    }
    return (uint16_t)(((uint16_t)data[0] << 8) | (uint16_t)data[1]);
}

/*
 * brief : _printer_cmd_detect_status_raw_locked.
 * input : see parameters.
 * output: return value from this function.
 * type  : private
 */
static esp_err_t _printer_cmd_detect_status_raw_locked(
    uint8_t out_status[PRINTER_STATUS_DETECT_RESPONSE_LEN]
)
{
    esp_err_t ret = ESP_OK;
    int rx_len = 0;
    TickType_t wait_ticks = pdMS_TO_TICKS((TickType_t)s_printer.write_timeout_ms);
    uint8_t cmd[2] = { 0x1BU, 0x76U };

    if (!s_printer.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (out_status == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    (void)uart_flush_input(s_printer.uart_port);

    ret = _printer_write_locked(cmd, sizeof(cmd));
    if (ret != ESP_OK) {
        return ret;
    }

    rx_len = uart_read_bytes(
        s_printer.uart_port,
        out_status,
        PRINTER_STATUS_DETECT_RESPONSE_LEN,
        wait_ticks
    );
    if (rx_len != (int)PRINTER_STATUS_DETECT_RESPONSE_LEN) {
        return ESP_ERR_TIMEOUT;
    }

    return ESP_OK;
}

/*
 * brief : printer_get_default_config.
 * input : see parameters.
 * output: none.
 * type  : public
 */
void printer_get_default_config(printer_config_t* out_cfg)
{
    if (out_cfg == NULL) {
        return;
    }

    *out_cfg = (printer_config_t){
        .uart_port = PRINTER_UART_HOST,
        .baud_rate = PRINTER_UART_BAUDRATE,
        .tx_io_num = PRINTER_UART_TX_GPIO,
        .rx_io_num = PRINTER_UART_RX_GPIO,
        .rts_io_num = GPIO_NUM_NC,
        .cts_io_num = GPIO_NUM_NC,
        .tx_buffer_size = PRINTER_UART_TX_BUFFER_SIZE_DEFAULT,
        .rx_buffer_size = PRINTER_UART_RX_BUFFER_SIZE_DEFAULT,
        .queue_size = PRINTER_UART_QUEUE_SIZE_DEFAULT,
        .write_timeout_ms = PRINTER_WRITE_TIMEOUT_MS_DEFAULT,
        .lock_timeout_ms = PRINTER_LOCK_TIMEOUT_MS_DEFAULT,
    };
}

/*
 * brief : printer_init.
 * input : see parameters.
 * output: return value from this function.
 * type  : public
 */
esp_err_t printer_init(const printer_config_t* config)
{
    esp_err_t ret = ESP_OK;
    printer_config_t default_cfg = { 0 };
    const printer_config_t* cfg = config;

    if (cfg == NULL) {
        printer_get_default_config(&default_cfg);
        cfg = &default_cfg;
    }

    if (s_printer.initialized) {
        return ESP_OK;
    }

    if ((cfg->uart_port < UART_NUM_0) || (cfg->uart_port >= UART_NUM_MAX)
        || (cfg->baud_rate <= 0) || !_printer_gpio_is_valid(cfg->tx_io_num)
        || (cfg->tx_buffer_size <= 0) || (cfg->rx_buffer_size < 0)
        || (cfg->queue_size < 0) || (cfg->write_timeout_ms == 0U)
        || (cfg->lock_timeout_ms == 0U)) {
        return ESP_ERR_INVALID_ARG;
    }

    if (s_printer.lock == NULL) {
        s_printer.lock = xSemaphoreCreateMutex();
        if (s_printer.lock == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }

    uart_config_t uart_cfg = {
        .baud_rate = cfg->baud_rate,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .rx_flow_ctrl_thresh = 0,
        .source_clk = UART_SCLK_DEFAULT,
    };

    ret = uart_param_config(cfg->uart_port, &uart_cfg);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = uart_set_pin(
        cfg->uart_port,
        _printer_uart_pin_num(cfg->tx_io_num),
        _printer_uart_pin_num(cfg->rx_io_num),
        _printer_uart_pin_num(cfg->rts_io_num),
        _printer_uart_pin_num(cfg->cts_io_num)
    );
    if (ret != ESP_OK) {
        _printer_release_io_from_config(cfg);
        return ret;
    }

    ret = uart_driver_install(
        cfg->uart_port,
        cfg->rx_buffer_size,
        cfg->tx_buffer_size,
        cfg->queue_size,
        NULL,
        0
    );
    if (ret != ESP_OK) {
        _printer_release_io_from_config(cfg);
        return ret;
    }

    s_printer.uart_port = cfg->uart_port;
    s_printer.tx_io_num = cfg->tx_io_num;
    s_printer.rx_io_num = cfg->rx_io_num;
    s_printer.rts_io_num = cfg->rts_io_num;
    s_printer.cts_io_num = cfg->cts_io_num;
    s_printer.write_timeout_ms = cfg->write_timeout_ms;
    s_printer.lock_timeout_ms = cfg->lock_timeout_ms;
    s_printer.initialized = true;

    return ESP_OK;
}

/*
 * brief : printer_deinit.
 * input : see parameters.
 * output: return value from this function.
 * type  : public
 */
esp_err_t printer_deinit(void)
{
    esp_err_t ret = ESP_OK;

    if (!s_printer.initialized) {
        return ESP_OK;
    }

    ret = _printer_take_lock();
    if (ret != ESP_OK) {
        return ret;
    }

    (void)uart_wait_tx_done(
        s_printer.uart_port,
        pdMS_TO_TICKS((TickType_t)s_printer.write_timeout_ms)
    );
    (void)uart_flush_input(s_printer.uart_port);

    ret = uart_driver_delete(s_printer.uart_port);

    _printer_release_io();

    s_printer.initialized = false;
    s_printer.uart_port = UART_NUM_MAX;
    s_printer.tx_io_num = GPIO_NUM_NC;
    s_printer.rx_io_num = GPIO_NUM_NC;
    s_printer.rts_io_num = GPIO_NUM_NC;
    s_printer.cts_io_num = GPIO_NUM_NC;
    s_printer.write_timeout_ms = PRINTER_WRITE_TIMEOUT_MS_DEFAULT;
    s_printer.lock_timeout_ms = PRINTER_LOCK_TIMEOUT_MS_DEFAULT;

    _printer_give_lock();
    return ret;
}

/*
 * brief : printer_is_ready.
 * input : see parameters.
 * output: return value from this function.
 * type  : public
 */
bool printer_is_ready(void)
{
    return s_printer.initialized;
}

/*
 * brief : printer_write_raw.
 * input : see parameters.
 * output: return value from this function.
 * type  : public
 */
esp_err_t printer_write_raw(const uint8_t* data, size_t data_len)
{
    esp_err_t ret = ESP_OK;

    if (!s_printer.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    ret = _printer_take_lock();
    if (ret != ESP_OK) {
        return ret;
    }

    ret = _printer_write_locked(data, data_len);
    _printer_give_lock();
    return ret;
}

/*
 * brief : printer_write_text.
 * input : see parameters.
 * output: return value from this function.
 * type  : public
 */
esp_err_t printer_write_text(const char* text)
{
    if (text == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    return printer_write_raw((const uint8_t*)text, strlen(text));
}

/*
 * brief : printer_write_line.
 * input : see parameters.
 * output: return value from this function.
 * type  : public
 */
esp_err_t printer_write_line(const char* text)
{
    static const uint8_t line_end[] = { '\r', '\n' };
    esp_err_t ret = printer_write_text(text);
    if (ret != ESP_OK) {
        return ret;
    }
    return printer_write_raw(line_end, sizeof(line_end));
}

/*
 * brief : printer_print_via_bin.
 * input : see parameters.
 * output: return value from this function.
 * type  : public
 */
esp_err_t printer_print_via_bin(const char* bin_name)
{
    esp_err_t ret = ESP_OK;
    const uint8_t* bin_data = NULL;
    size_t data_len = 0U;

    if (!s_printer.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    ret = access_assets_get_bin(bin_name, &bin_data, &data_len);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = _printer_take_lock();
    if (ret != ESP_OK) {
        return ret;
    }

    ret =
        _printer_write_chunked_locked(bin_data, data_len, PRINTER_BULK_WRITE_CHUNK_BYTES);
    _printer_give_lock();
    return ret;
}

/*
 * brief : printer_cmd_detect_status.
 * input : see parameters.
 * output: return value from this function.
 * type  : public
 */
esp_err_t printer_cmd_detect_status(printer_detect_status_t* out_status)
{
    const int calibration = 210;
    esp_err_t ret = ESP_OK;
    uint8_t raw[PRINTER_STATUS_DETECT_RESPONSE_LEN] = { 0 };

    if (!s_printer.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (out_status == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    ret = _printer_take_lock();
    if (ret != ESP_OK) {
        return ret;
    }

    ret = _printer_cmd_detect_status_raw_locked(raw);
    _printer_give_lock();
    if (ret != ESP_OK) {
        return ret;
    }

    out_status->tph_temperature_celsius = raw[1];
    out_status->paper_detect_raw = _printer_read_u16_le(&raw[2]) >> 2;
    out_status->working_voltage_raw = (uint16_t)(_printer_read_u16_le(&raw[4]) * 5 / 8);
    if (out_status->working_voltage_raw > calibration) {
        out_status->working_voltage_raw -= calibration;
    }
    return ESP_OK;
}

/* Command 1 */
esp_err_t printer_cmd_clear_printer(void)
{
    const uint8_t cmd[] = { 0x1B, 0x40 };
    return _printer_send_cmd(cmd, sizeof(cmd));
}

/* Command 2 */
esp_err_t printer_cmd_print_and_feed_lines(uint8_t lines)
{
    const uint8_t cmd[] = { 0x1B, 0x64, lines };
    return _printer_send_cmd(cmd, sizeof(cmd));
}

/* Command 3 */
esp_err_t printer_cmd_print_and_feed_dots(uint8_t dots)
{
    const uint8_t cmd[] = { 0x1B, 0x4A, dots };
    return _printer_send_cmd(cmd, sizeof(cmd));
}

/* Command 4 */
esp_err_t printer_cmd_set_justification(printer_justification_t mode)
{
    if ((mode != PRINTER_JUSTIFY_LEFT) && (mode != PRINTER_JUSTIFY_CENTER)
        && (mode != PRINTER_JUSTIFY_RIGHT)) {
        return ESP_ERR_INVALID_ARG;
    }
    const uint8_t cmd[] = { 0x1B, 0x61, (uint8_t)mode };
    return _printer_send_cmd(cmd, sizeof(cmd));
}

/* Command 5 */
esp_err_t printer_cmd_select_print_mode(uint8_t mode_mask)
{
    const uint8_t cmd[] = { 0x1B, 0x21, mode_mask };
    return _printer_send_cmd(cmd, sizeof(cmd));
}

/* Command 6 */
esp_err_t printer_cmd_set_character_size(uint8_t width, uint8_t height)
{
    uint8_t packed_size = 0;

    if ((width < 1U) || (width > 8U) || (height < 1U) || (height > 8U)) {
        return ESP_ERR_INVALID_ARG;
    }

    packed_size = (uint8_t)(((width - 1U) << 4) | (height - 1U));
    const uint8_t cmd[] = { 0x1D, 0x21, packed_size };
    return _printer_send_cmd(cmd, sizeof(cmd));
}

/* Command 7 */
esp_err_t printer_cmd_set_bold(bool enable)
{
    const uint8_t cmd[] = { 0x1B, 0x45, (uint8_t)(enable ? 1U : 0U) };
    return _printer_send_cmd(cmd, sizeof(cmd));
}

/* Command 8 */
esp_err_t printer_cmd_set_underline(printer_underline_t mode)
{
    if ((mode != PRINTER_UNDERLINE_OFF) && (mode != PRINTER_UNDERLINE_THIN)
        && (mode != PRINTER_UNDERLINE_THICK)) {
        return ESP_ERR_INVALID_ARG;
    }
    const uint8_t cmd[] = { 0x1B, 0x2D, (uint8_t)mode };
    return _printer_send_cmd(cmd, sizeof(cmd));
}

/* Command 9 */
esp_err_t printer_cmd_set_inverse(bool enable)
{
    const uint8_t cmd[] = { 0x1D, 0x42, (uint8_t)(enable ? 1U : 0U) };
    return _printer_send_cmd(cmd, sizeof(cmd));
}

/* Command 10 */
esp_err_t printer_cmd_set_upside_down(bool enable)
{
    const uint8_t cmd[] = { 0x1B, 0x7B, (uint8_t)(enable ? 1U : 0U) };
    return _printer_send_cmd(cmd, sizeof(cmd));
}

/* Command 11 */
esp_err_t printer_cmd_set_line_spacing(uint8_t spacing_dots)
{
    const uint8_t cmd[] = { 0x1B, 0x33, spacing_dots };
    return _printer_send_cmd(cmd, sizeof(cmd));
}

/* Command 12 */
esp_err_t printer_cmd_reset_line_spacing(void)
{
    const uint8_t cmd[] = { 0x1B, 0x32 };
    return _printer_send_cmd(cmd, sizeof(cmd));
}

/* Command 13 */
esp_err_t printer_cmd_cut_paper(printer_cut_mode_t mode)
{
    uint8_t cut_mode = 0;

    if ((mode != PRINTER_CUT_FULL) && (mode != PRINTER_CUT_PARTIAL)) {
        return ESP_ERR_INVALID_ARG;
    }

    cut_mode = (uint8_t)(mode == PRINTER_CUT_FULL ? 0U : 1U);
    const uint8_t cmd[] = { 0x1D, 0x56, cut_mode };
    return _printer_send_cmd(cmd, sizeof(cmd));
}

/* Command 14 */
esp_err_t printer_cmd_set_hri_position(printer_hri_position_t position)
{
    if ((position != PRINTER_HRI_NOT_PRINTED) && (position != PRINTER_HRI_ABOVE)
        && (position != PRINTER_HRI_BELOW) && (position != PRINTER_HRI_ABOVE_AND_BELOW)) {
        return ESP_ERR_INVALID_ARG;
    }

    const uint8_t cmd[] = { 0x1D, 0x48, (uint8_t)position };
    return _printer_send_cmd(cmd, sizeof(cmd));
}

/* Command 15 */
esp_err_t printer_cmd_set_hri_font(printer_hri_font_t font)
{
    if ((font != PRINTER_HRI_FONT_A) && (font != PRINTER_HRI_FONT_B)) {
        return ESP_ERR_INVALID_ARG;
    }

    const uint8_t cmd[] = { 0x1D, 0x66, (uint8_t)font };
    return _printer_send_cmd(cmd, sizeof(cmd));
}

/* Command 16 */
esp_err_t printer_cmd_set_barcode_height(uint8_t height_dots)
{
    if (height_dots == 0U) {
        return ESP_ERR_INVALID_ARG;
    }
    const uint8_t cmd[] = { 0x1D, 0x68, height_dots };
    return _printer_send_cmd(cmd, sizeof(cmd));
}

/* Command 17 */
esp_err_t printer_cmd_set_barcode_width(uint8_t width_dots)
{
    if ((width_dots < 2U) || (width_dots > 6U)) {
        return ESP_ERR_INVALID_ARG;
    }
    const uint8_t cmd[] = { 0x1D, 0x77, width_dots };
    return _printer_send_cmd(cmd, sizeof(cmd));
}

/* Command 18 */
esp_err_t printer_cmd_print_barcode_code128(const uint8_t* data, size_t data_len)
{
    uint8_t cmd[4U + PRINTER_MAX_BARCODE_BYTES] = { 0 };

    if ((data == NULL) || (data_len == 0U) || (data_len > PRINTER_MAX_BARCODE_BYTES)) {
        return ESP_ERR_INVALID_ARG;
    }

    cmd[0] = 0x1D;
    cmd[1] = 0x6B;
    cmd[2] = 0x49; /* CODE128 */
    cmd[3] = (uint8_t)data_len;
    memcpy(&cmd[4], data, data_len);

    return _printer_send_cmd(cmd, data_len + 4U);
}
