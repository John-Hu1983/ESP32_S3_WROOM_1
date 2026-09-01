#include "dev_mfrc522.h"

#define TAG "mfrc522"

static mfrc522_ctx_t s_mfrc522;

/*
 * brief : _mfrc522_delay_ms.
 * input : see parameters.
 * output: none.
 * type  : private
 */
static void _mfrc522_delay_ms(uint32_t ms) {
    if (ms == 0U) {
        return;
    }

    TickType_t ticks = pdMS_TO_TICKS(ms);
    if (ticks == 0U) {
        ticks = 1U;
    }
    vTaskDelay(ticks);
}

/*
 * brief : _mfrc522_write_reg.
 * input : see parameters.
 * output: return value from this function.
 * type  : private
 */
static esp_err_t _mfrc522_write_reg(uint8_t reg, uint8_t value) {
    if (!s_mfrc522.initialized || (s_mfrc522.spi == NULL)) {
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t tx_buf[2] = {(uint8_t)((reg << 1U) & 0x7EU), value};

    spi_transaction_t trans = {0};
    trans.length = 16;
    trans.tx_buffer = tx_buf;

    return spi_device_transmit(s_mfrc522.spi, &trans);
}

/*
 * brief : _mfrc522_read_reg.
 * input : see parameters.
 * output: return value from this function.
 * type  : private
 */
static esp_err_t _mfrc522_read_reg(uint8_t reg, uint8_t* value) {
    if (value == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_mfrc522.initialized || (s_mfrc522.spi == NULL)) {
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t tx_buf[2] = {(uint8_t)(((reg << 1U) & 0x7EU) | 0x80U), 0x00U};
    uint8_t rx_buf[2] = {0x00U, 0x00U};

    spi_transaction_t trans = {0};
    trans.length = 16;
    trans.rxlength = 16;
    trans.tx_buffer = tx_buf;
    trans.rx_buffer = rx_buf;

    esp_err_t ret = spi_device_transmit(s_mfrc522.spi, &trans);
    if (ret != ESP_OK) {
        return ret;
    }

    *value = rx_buf[1];
    return ESP_OK;
}

/*
 * brief : _mfrc522_set_bits.
 * input : see parameters.
 * output: return value from this function.
 * type  : private
 */
static esp_err_t _mfrc522_set_bits(uint8_t reg, uint8_t mask) {
    uint8_t reg_val = 0;
    esp_err_t ret = _mfrc522_read_reg(reg, &reg_val);
    if (ret != ESP_OK) {
        return ret;
    }
    return _mfrc522_write_reg(reg, (uint8_t)(reg_val | mask));
}

/*
 * brief : _mfrc522_clear_bits.
 * input : see parameters.
 * output: return value from this function.
 * type  : private
 */
static esp_err_t _mfrc522_clear_bits(uint8_t reg, uint8_t mask) {
    uint8_t reg_val = 0;
    esp_err_t ret = _mfrc522_read_reg(reg, &reg_val);
    if (ret != ESP_OK) {
        return ret;
    }
    return _mfrc522_write_reg(reg, (uint8_t)(reg_val & (uint8_t)(~mask)));
}

/*
 * brief : _mfrc522_flush_fifo.
 * input : see parameters.
 * output: return value from this function.
 * type  : private
 */
static esp_err_t _mfrc522_flush_fifo(void) {
    return _mfrc522_write_reg(MFRC522_REG_FIFO_LEVEL, MFRC522_FIFO_FLUSH);
}

/*
 * brief : _mfrc522_write_fifo.
 * input : see parameters.
 * output: return value from this function.
 * type  : private
 */
static esp_err_t _mfrc522_write_fifo(const uint8_t* data, uint8_t len) {
    if ((data == NULL) && (len > 0U)) {
        return ESP_ERR_INVALID_ARG;
    }

    for (uint8_t i = 0; i < len; i++) {
        esp_err_t ret = _mfrc522_write_reg(MFRC522_REG_FIFO_DATA, data[i]);
        if (ret != ESP_OK) {
            return ret;
        }
    }

    return ESP_OK;
}

/*
 * brief : _mfrc522_read_fifo.
 * input : see parameters.
 * output: return value from this function.
 * type  : private
 */
static esp_err_t _mfrc522_read_fifo(uint8_t* data, uint8_t len) {
    if ((data == NULL) && (len > 0U)) {
        return ESP_ERR_INVALID_ARG;
    }

    for (uint8_t i = 0; i < len; i++) {
        esp_err_t ret = _mfrc522_read_reg(MFRC522_REG_FIFO_DATA, &data[i]);
        if (ret != ESP_OK) {
            return ret;
        }
    }

    return ESP_OK;
}

/*
 * brief : _mfrc522_wait_com_irq.
 * input : see parameters.
 * output: return value from this function.
 * type  : private
 */
static esp_err_t _mfrc522_wait_com_irq(uint8_t wait_mask, uint16_t timeout_ms) {
    TickType_t timeout_tick = pdMS_TO_TICKS(timeout_ms);
    TickType_t start_tick = xTaskGetTickCount();
    if (timeout_tick == 0U) {
        timeout_tick = 1U;
    }

    while (1) {
        uint8_t irq_val = 0;
        esp_err_t ret = _mfrc522_read_reg(MFRC522_REG_COM_IRQ, &irq_val);
        if (ret != ESP_OK) {
            return ret;
        }

        if ((irq_val & wait_mask) != 0U) {
            return ESP_OK;
        }

        if ((irq_val & MFRC522_COM_IRQ_TIMER) != 0U) {
            return ESP_ERR_TIMEOUT;
        }

        if ((xTaskGetTickCount() - start_tick) >= timeout_tick) {
            return ESP_ERR_TIMEOUT;
        }

        _mfrc522_delay_ms(1U);
    }
}

/*
 * brief : _mfrc522_wait_div_irq.
 * input : see parameters.
 * output: return value from this function.
 * type  : private
 */
static esp_err_t _mfrc522_wait_div_irq(uint8_t wait_mask, uint16_t timeout_ms) {
    TickType_t timeout_tick = pdMS_TO_TICKS(timeout_ms);
    TickType_t start_tick = xTaskGetTickCount();
    if (timeout_tick == 0U) {
        timeout_tick = 1U;
    }

    while (1) {
        uint8_t irq_val = 0;
        esp_err_t ret = _mfrc522_read_reg(MFRC522_REG_DIV_IRQ, &irq_val);
        if (ret != ESP_OK) {
            return ret;
        }

        if ((irq_val & wait_mask) != 0U) {
            return ESP_OK;
        }

        if ((xTaskGetTickCount() - start_tick) >= timeout_tick) {
            return ESP_ERR_TIMEOUT;
        }

        _mfrc522_delay_ms(1U);
    }
}

/*
 * brief : _mfrc522_calc_crc.
 * input : see parameters.
 * output: return value from this function.
 * type  : private
 */
static esp_err_t _mfrc522_calc_crc(const uint8_t* data, uint8_t len, uint8_t out_crc[2]) {
    if ((data == NULL && len > 0U) || (out_crc == NULL)) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t ret = _mfrc522_write_reg(MFRC522_REG_COMMAND, MFRC522_CMD_IDLE);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = _mfrc522_write_reg(MFRC522_REG_DIV_IRQ, MFRC522_DIV_IRQ_CRC);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = _mfrc522_flush_fifo();
    if (ret != ESP_OK) {
        return ret;
    }

    ret = _mfrc522_write_fifo(data, len);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = _mfrc522_write_reg(MFRC522_REG_COMMAND, MFRC522_CMD_CALC_CRC);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = _mfrc522_wait_div_irq(MFRC522_DIV_IRQ_CRC, MFRC522_CMD_TIMEOUT_MS);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = _mfrc522_write_reg(MFRC522_REG_COMMAND, MFRC522_CMD_IDLE);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = _mfrc522_read_reg(MFRC522_REG_CRC_RESULT_L, &out_crc[0]);
    if (ret != ESP_OK) {
        return ret;
    }

    return _mfrc522_read_reg(MFRC522_REG_CRC_RESULT_H, &out_crc[1]);
}

/*
 * brief : _mfrc522_transceive.
 * input : see parameters.
 * output: return value from this function.
 * type  : private
 */
static esp_err_t _mfrc522_transceive(const uint8_t* send_data, uint8_t send_len,
                                     uint8_t tx_last_bits, uint8_t* back_data, uint8_t* back_len,
                                     uint8_t* back_last_bits) {
    if ((send_data == NULL && send_len > 0U) ||
        (back_len != NULL && *back_len > 0U && back_data == NULL)) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t ret = _mfrc522_write_reg(MFRC522_REG_COMMAND, MFRC522_CMD_IDLE);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = _mfrc522_write_reg(MFRC522_REG_COM_IRQ, 0x7FU);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = _mfrc522_flush_fifo();
    if (ret != ESP_OK) {
        return ret;
    }

    ret = _mfrc522_write_reg(MFRC522_REG_BIT_FRAMING, (uint8_t)(tx_last_bits & 0x07U));
    if (ret != ESP_OK) {
        return ret;
    }

    ret = _mfrc522_write_fifo(send_data, send_len);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = _mfrc522_write_reg(MFRC522_REG_COMMAND, MFRC522_CMD_TRANSCEIVE);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = _mfrc522_set_bits(MFRC522_REG_BIT_FRAMING, MFRC522_BIT_START_SEND);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = _mfrc522_wait_com_irq((uint8_t)(MFRC522_COM_IRQ_RX | MFRC522_COM_IRQ_IDLE),
                                MFRC522_CMD_TIMEOUT_MS);

    (void)_mfrc522_clear_bits(MFRC522_REG_BIT_FRAMING, MFRC522_BIT_START_SEND);

    if (ret != ESP_OK) {
        return ret;
    }

    uint8_t error_reg = 0;
    ret = _mfrc522_read_reg(MFRC522_REG_ERROR, &error_reg);
    if (ret != ESP_OK) {
        return ret;
    }

    if ((error_reg & 0x13U) != 0U) {
        return ESP_FAIL;
    }

    if ((error_reg & MFRC522_COLL_ERR_BIT) != 0U) {
        return ESP_ERR_INVALID_RESPONSE;
    }

    if (back_len != NULL) {
        uint8_t fifo_len = 0;
        ret = _mfrc522_read_reg(MFRC522_REG_FIFO_LEVEL, &fifo_len);
        if (ret != ESP_OK) {
            return ret;
        }

        if (fifo_len > *back_len) {
            return ESP_ERR_INVALID_SIZE;
        }

        ret = _mfrc522_read_fifo(back_data, fifo_len);
        if (ret != ESP_OK) {
            return ret;
        }

        *back_len = fifo_len;

        if (back_last_bits != NULL) {
            uint8_t control_reg = 0;
            ret = _mfrc522_read_reg(MFRC522_REG_CONTROL, &control_reg);
            if (ret != ESP_OK) {
                return ret;
            }
            *back_last_bits = (uint8_t)(control_reg & 0x07U);
        }
    }

    return ESP_OK;
}

/*
 * brief : _mfrc522_auth_cmd.
 * input : see parameters.
 * output: return value from this function.
 * type  : private
 */
static esp_err_t _mfrc522_auth_cmd(const uint8_t* send_data, uint8_t send_len) {
    if ((send_data == NULL) || (send_len == 0U)) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t ret = _mfrc522_write_reg(MFRC522_REG_COMMAND, MFRC522_CMD_IDLE);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = _mfrc522_write_reg(MFRC522_REG_COM_IRQ, 0x7FU);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = _mfrc522_flush_fifo();
    if (ret != ESP_OK) {
        return ret;
    }

    ret = _mfrc522_write_fifo(send_data, send_len);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = _mfrc522_write_reg(MFRC522_REG_COMMAND, MFRC522_CMD_MF_AUTHENT);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = _mfrc522_wait_com_irq(MFRC522_COM_IRQ_IDLE, MFRC522_CMD_TIMEOUT_MS);
    if (ret != ESP_OK) {
        return ret;
    }

    uint8_t error_reg = 0;
    ret = _mfrc522_read_reg(MFRC522_REG_ERROR, &error_reg);
    if (ret != ESP_OK) {
        return ret;
    }

    if ((error_reg & 0x13U) != 0U) {
        return ESP_FAIL;
    }

    return ESP_OK;
}

/*
 * brief : _mfrc522_request.
 * input : see parameters.
 * output: return value from this function.
 * type  : private
 */
static esp_err_t _mfrc522_request(uint8_t req_cmd, uint8_t atqa[2]) {
    if (atqa == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t rx_buf[10] = {0};
    uint8_t rx_len = sizeof(rx_buf);
    uint8_t rx_last_bits = 0;

    esp_err_t ret = _mfrc522_transceive(&req_cmd, 1U, 7U, rx_buf, &rx_len, &rx_last_bits);
    if (ret == ESP_ERR_TIMEOUT) {
        return ESP_ERR_NOT_FOUND;
    }
    if (ret != ESP_OK) {
        return ret;
    }

    if ((rx_len != 2U) || (rx_last_bits != 0U)) {
        return ESP_ERR_INVALID_RESPONSE;
    }

    atqa[0] = rx_buf[0];
    atqa[1] = rx_buf[1];
    return ESP_OK;
}

/*
 * brief : _mfrc522_select_level.
 * input : see parameters.
 * output: return value from this function.
 * type  : private
 */
static esp_err_t _mfrc522_select_level(uint8_t sel_cmd, const uint8_t uid_part[5], uint8_t* sak) {
    uint8_t tx_buf[9] = {0};
    uint8_t crc_buf[2] = {0};

    tx_buf[0] = sel_cmd;
    tx_buf[1] = 0x70U;
    memcpy(&tx_buf[2], uid_part, 5U);

    esp_err_t ret = _mfrc522_calc_crc(tx_buf, 7U, crc_buf);
    if (ret != ESP_OK) {
        return ret;
    }
    tx_buf[7] = crc_buf[0];
    tx_buf[8] = crc_buf[1];

    uint8_t rx_buf[3] = {0};
    uint8_t rx_len = sizeof(rx_buf);
    uint8_t rx_last_bits = 0;

    ret = _mfrc522_transceive(tx_buf, sizeof(tx_buf), 0U, rx_buf, &rx_len, &rx_last_bits);
    if (ret != ESP_OK) {
        return ret;
    }
    if ((rx_len != 3U) || (rx_last_bits != 0U)) {
        return ESP_ERR_INVALID_RESPONSE;
    }

    ret = _mfrc522_calc_crc(rx_buf, 1U, crc_buf);
    if (ret != ESP_OK) {
        return ret;
    }
    if ((crc_buf[0] != rx_buf[1]) || (crc_buf[1] != rx_buf[2])) {
        return ESP_ERR_INVALID_RESPONSE;
    }

    if (sak != NULL) {
        *sak = rx_buf[0];
    }
    return ESP_OK;
}

/*
 * brief : _mfrc522_is_ack.
 * input : see parameters.
 * output: return value from this function.
 * type  : private
 */
static bool _mfrc522_is_ack(const uint8_t* data, uint8_t data_len, uint8_t last_bits) {
    if ((data == NULL) || (data_len != 1U) || (last_bits != 4U)) {
        return false;
    }
    return (data[0] & 0x0FU) == MFRC522_PICC_ACK;
}

/*
 * brief : _mfrc522_get_sector_layout.
 * input : see parameters.
 * output: return value from this function.
 * type  : private
 */
static esp_err_t _mfrc522_get_sector_layout(uint8_t sector, uint8_t* first_block,
                                            uint8_t* block_count) {
    if ((first_block == NULL) || (block_count == NULL)) {
        return ESP_ERR_INVALID_ARG;
    }

    if (sector < 32U) {
        *first_block = (uint8_t)(sector * 4U);
        *block_count = 4U;
        return ESP_OK;
    }

    if (sector < 40U) {
        *first_block = (uint8_t)(128U + ((sector - 32U) * 16U));
        *block_count = 16U;
        return ESP_OK;
    }

    return ESP_ERR_INVALID_ARG;
}

/*
 * brief : _mfrc522_antenna_on.
 * input : see parameters.
 * output: return value from this function.
 * type  : private
 */
static esp_err_t _mfrc522_antenna_on(void) {
    uint8_t tx_ctrl = 0;
    esp_err_t ret = _mfrc522_read_reg(MFRC522_REG_TX_CONTROL, &tx_ctrl);
    if (ret != ESP_OK) {
        return ret;
    }

    if ((tx_ctrl & MFRC522_TX_ANTENNA_ON_MASK) != MFRC522_TX_ANTENNA_ON_MASK) {
        ret = _mfrc522_write_reg(MFRC522_REG_TX_CONTROL,
                                 (uint8_t)(tx_ctrl | MFRC522_TX_ANTENNA_ON_MASK));
    }

    return ret;
}

/*
 * brief : _mfrc522_hw_reset.
 * input : see parameters.
 * output: return value from this function.
 * type  : private
 */
static esp_err_t _mfrc522_hw_reset(void) {
    esp_err_t ret =
        gpba02b_set_io_mode(MFRC522_RESET_PORT, MFRC522_RESET_PIN, GPBA02B_IO_STYLE_OUTPUT_CMOS);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = gpba02b_write_io_level(MFRC522_RESET_PORT, MFRC522_RESET_PIN, 0U);
    if (ret != ESP_OK) {
        return ret;
    }
    _mfrc522_delay_ms(2U);

    ret = gpba02b_write_io_level(MFRC522_RESET_PORT, MFRC522_RESET_PIN, 1U);
    if (ret != ESP_OK) {
        return ret;
    }
    _mfrc522_delay_ms(50U);

    return ESP_OK;
}

/*
 * brief : mfrc522_init.
 * input : see parameters.
 * output: return value from this function.
 * type  : public
 */
esp_err_t mfrc522_init(void) {
    if (s_mfrc522.initialized) {
        return ESP_OK;
    }

    if ((MFRC522_IO_CS == GPIO_NUM_NC) || (MFRC522_IO_CLK == GPIO_NUM_NC) ||
        (MFRC522_IO_MOSI == GPIO_NUM_NC) || (MFRC522_IO_MISO == GPIO_NUM_NC) ||
        (MFRC522_DEFAULT_CLOCK_HZ <= 0)) {
        return ESP_ERR_INVALID_ARG;
    }

    spi_device_interface_config_t dev_cfg = {
        .command_bits = 0,
        .address_bits = 0,
        .dummy_bits = 0,
        .mode = 0,
        .duty_cycle_pos = 128,
        .cs_ena_pretrans = 0,
        .cs_ena_posttrans = 0,
        .clock_speed_hz = MFRC522_DEFAULT_CLOCK_HZ,
        .input_delay_ns = 0,
        .spics_io_num = MFRC522_IO_CS,
        .flags = 0,
        .queue_size = 1,
        .pre_cb = NULL,
        .post_cb = NULL,
    };

    bool bus_inited_here = false;
    esp_err_t ret = spi_bus_add_device(MFRC522_SPI_HOST, &dev_cfg, &s_mfrc522.spi);
    if (ret == ESP_ERR_INVALID_STATE) {
        spi_bus_config_t bus_cfg = {
            .mosi_io_num = MFRC522_IO_MOSI,
            .miso_io_num = MFRC522_IO_MISO,
            .sclk_io_num = MFRC522_IO_CLK,
            .quadwp_io_num = GPIO_NUM_NC,
            .quadhd_io_num = GPIO_NUM_NC,
            .max_transfer_sz = MFRC522_FIFO_MAX_LEN,
            .intr_flags = 0,
        };

        ret = spi_bus_initialize(MFRC522_SPI_HOST, &bus_cfg, SPI_DMA_CH_AUTO);
        if (ret != ESP_OK) {
            return ret;
        }
        bus_inited_here = true;

        ret = spi_bus_add_device(MFRC522_SPI_HOST, &dev_cfg, &s_mfrc522.spi);
    }

    if (ret != ESP_OK) {
        if (bus_inited_here) {
            (void)spi_bus_free(MFRC522_SPI_HOST);
        }
        return ret;
    }

    s_mfrc522.spi_host = MFRC522_SPI_HOST;
    s_mfrc522.initialized = true;
    s_mfrc522.bus_initialized_here = bus_inited_here;

    ret = _mfrc522_hw_reset();
    if (ret != ESP_OK) {
        (void)mfrc522_deinit();
        return ret;
    }

    ret = _mfrc522_write_reg(MFRC522_REG_COMMAND, MFRC522_CMD_SOFT_RESET);
    if (ret != ESP_OK) {
        (void)mfrc522_deinit();
        return ret;
    }
    _mfrc522_delay_ms(50U);

    ret = _mfrc522_write_reg(MFRC522_REG_T_MODE, 0x8DU);
    if (ret != ESP_OK) {
        (void)mfrc522_deinit();
        return ret;
    }
    ret = _mfrc522_write_reg(MFRC522_REG_T_PRESCALER, 0x3EU);
    if (ret != ESP_OK) {
        (void)mfrc522_deinit();
        return ret;
    }
    ret = _mfrc522_write_reg(MFRC522_REG_T_RELOAD_H, 0x00U);
    if (ret != ESP_OK) {
        (void)mfrc522_deinit();
        return ret;
    }
    ret = _mfrc522_write_reg(MFRC522_REG_T_RELOAD_L, 30U);
    if (ret != ESP_OK) {
        (void)mfrc522_deinit();
        return ret;
    }
    ret = _mfrc522_write_reg(MFRC522_REG_TX_ASK, 0x40U);
    if (ret != ESP_OK) {
        (void)mfrc522_deinit();
        return ret;
    }
    ret = _mfrc522_write_reg(MFRC522_REG_MODE, 0x3DU);
    if (ret != ESP_OK) {
        (void)mfrc522_deinit();
        return ret;
    }
    ret = _mfrc522_write_reg(MFRC522_REG_RF_CFG, 0x70U);
    if (ret != ESP_OK) {
        (void)mfrc522_deinit();
        return ret;
    }

    ret = _mfrc522_clear_bits(MFRC522_REG_COLL, 0x80U);
    if (ret != ESP_OK) {
        (void)mfrc522_deinit();
        return ret;
    }

    return _mfrc522_antenna_on();
}

/*
 * brief : mfrc522_deinit.
 * input : see parameters.
 * output: return value from this function.
 * type  : public
 */
esp_err_t mfrc522_deinit(void) {
    if (!s_mfrc522.initialized) {
        return ESP_OK;
    }

    esp_err_t ret = ESP_OK;

    if (s_mfrc522.spi != NULL) {
        ret = spi_bus_remove_device(s_mfrc522.spi);
        s_mfrc522.spi = NULL;
    }

    /* Shared SPI3 host with GPBA02B, keep bus alive to avoid side effects. */
    s_mfrc522.spi_host = 0;
    s_mfrc522.initialized = false;
    s_mfrc522.bus_initialized_here = false;

    return ret;
}

/*
 * brief : mfrc522_is_ready.
 * input : see parameters.
 * output: return value from this function.
 * type  : public
 */
bool mfrc522_is_ready(void) { return s_mfrc522.initialized && (s_mfrc522.spi != NULL); }

/*
 * brief : mfrc522_get_version.
 * input : see parameters.
 * output: return value from this function.
 * type  : public
 */
esp_err_t mfrc522_get_version(uint8_t* version) {
    if (version == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    return _mfrc522_read_reg(MFRC522_REG_VERSION, version);
}

/*
 * brief : mfrc522_poll_card.
 * input : see parameters.
 * output: return value from this function.
 * type  : public
 */
esp_err_t mfrc522_poll_card(bool* present) {
    if (present == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t atqa[2] = {0};
    esp_err_t ret = mfrc522_request_a(atqa);
    if (ret == ESP_OK) {
        *present = true;
        return ESP_OK;
    }
    if (ret == ESP_ERR_NOT_FOUND) {
        *present = false;
        return ESP_OK;
    }

    return ret;
}

/*
 * brief : mfrc522_request_a.
 * input : see parameters.
 * output: return value from this function.
 * type  : public
 */
esp_err_t mfrc522_request_a(uint8_t atqa[2]) {
    return _mfrc522_request(MFRC522_PICC_CMD_REQA, atqa);
}

/*
 * brief : mfrc522_wakeup_a.
 * input : see parameters.
 * output: return value from this function.
 * type  : public
 */
esp_err_t mfrc522_wakeup_a(uint8_t atqa[2]) {
    return _mfrc522_request(MFRC522_PICC_CMD_WUPA, atqa);
}

/*
 * brief : mfrc522_read_uid.
 * input : see parameters.
 * output: return value from this function.
 * type  : public
 */
esp_err_t mfrc522_read_uid(mfrc522_uid_t* uid) {
    if (uid == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(uid, 0, sizeof(*uid));

    esp_err_t ret = mfrc522_request_a(uid->atqa);
    if (ret == ESP_ERR_NOT_FOUND) {
        ret = mfrc522_wakeup_a(uid->atqa);
    }
    if (ret != ESP_OK) {
        return ret;
    }

    const uint8_t cascade_cmds[3] = {MFRC522_PICC_CMD_SEL_CL1, MFRC522_PICC_CMD_SEL_CL2,
                                     MFRC522_PICC_CMD_SEL_CL3};

    uint8_t uid_index = 0;
    uint8_t sak = 0;

    for (uint8_t level = 0; level < 3U; level++) {
        uint8_t anticoll_cmd[2] = {cascade_cmds[level], 0x20U};
        uint8_t uid_part[5] = {0};
        uint8_t rx_len = sizeof(uid_part);
        uint8_t rx_last_bits = 0;

        ret = _mfrc522_transceive(anticoll_cmd, sizeof(anticoll_cmd), 0U, uid_part, &rx_len,
                                  &rx_last_bits);
        if (ret != ESP_OK) {
            return ret;
        }
        if ((rx_len != 5U) || (rx_last_bits != 0U)) {
            return ESP_ERR_INVALID_RESPONSE;
        }
        if ((uint8_t)(uid_part[0] ^ uid_part[1] ^ uid_part[2] ^ uid_part[3]) != uid_part[4]) {
            return ESP_ERR_INVALID_RESPONSE;
        }

        ret = _mfrc522_select_level(cascade_cmds[level], uid_part, &sak);
        if (ret != ESP_OK) {
            return ret;
        }

        if (uid_part[0] == MFRC522_PICC_CMD_CT) {
            if ((uid_index + 3U) > MFRC522_UID_MAX_LEN) {
                return ESP_ERR_INVALID_SIZE;
            }
            uid->uid[uid_index++] = uid_part[1];
            uid->uid[uid_index++] = uid_part[2];
            uid->uid[uid_index++] = uid_part[3];
        } else {
            if ((uid_index + 4U) > MFRC522_UID_MAX_LEN) {
                return ESP_ERR_INVALID_SIZE;
            }
            uid->uid[uid_index++] = uid_part[0];
            uid->uid[uid_index++] = uid_part[1];
            uid->uid[uid_index++] = uid_part[2];
            uid->uid[uid_index++] = uid_part[3];
        }

        if ((sak & MFRC522_SAK_CASCADE_BIT) == 0U) {
            uid->sak = sak;
            uid->size = uid_index;
            return ESP_OK;
        }
    }

    return ESP_ERR_INVALID_RESPONSE;
}

/*
 * brief : mfrc522_authenticate.
 * input : see parameters.
 * output: return value from this function.
 * type  : public
 */
esp_err_t mfrc522_authenticate(mfrc522_key_type_t key_type, uint8_t block_addr,
                               const uint8_t key[MFRC522_KEY_LEN], const mfrc522_uid_t* uid) {
    if ((uid == NULL) || (uid->size < 4U)) {
        return ESP_ERR_INVALID_ARG;
    }
    if ((key_type != MFRC522_KEY_A) && (key_type != MFRC522_KEY_B)) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t auth_buf[12] = {0};
    auth_buf[0] = (uint8_t)key_type;
    auth_buf[1] = block_addr;
    if (key != NULL) {
        memcpy(&auth_buf[2], key, MFRC522_KEY_LEN);
    } else {
        memset(&auth_buf[2], MFRC522_DEFAULT_KEY_BYTE, MFRC522_KEY_LEN);
    }
    memcpy(&auth_buf[8], uid->uid, 4U);

    esp_err_t ret = _mfrc522_auth_cmd(auth_buf, sizeof(auth_buf));
    if (ret != ESP_OK) {
        return ret;
    }

    uint8_t status2 = 0;
    ret = _mfrc522_read_reg(MFRC522_REG_STATUS2, &status2);
    if (ret != ESP_OK) {
        return ret;
    }

    if ((status2 & MFRC522_STATUS2_CRYPTO1_ON) == 0U) {
        return ESP_FAIL;
    }

    return ESP_OK;
}

/*
 * brief : mfrc522_stop_crypto.
 * input : see parameters.
 * output: none.
 * type  : public
 */
void mfrc522_stop_crypto(void) {
    if (!mfrc522_is_ready()) {
        return;
    }
    (void)_mfrc522_clear_bits(MFRC522_REG_STATUS2, MFRC522_STATUS2_CRYPTO1_ON);
}

/*
 * brief : mfrc522_halt.
 * input : see parameters.
 * output: return value from this function.
 * type  : public
 */
esp_err_t mfrc522_halt(void) {
    uint8_t tx_buf[4] = {MFRC522_PICC_CMD_HLTA, 0x00U, 0x00U, 0x00U};
    uint8_t crc_buf[2] = {0};

    esp_err_t ret = _mfrc522_calc_crc(tx_buf, 2U, crc_buf);
    if (ret != ESP_OK) {
        return ret;
    }
    tx_buf[2] = crc_buf[0];
    tx_buf[3] = crc_buf[1];

    uint8_t rx_buf[4] = {0};
    uint8_t rx_len = sizeof(rx_buf);
    ret = _mfrc522_transceive(tx_buf, sizeof(tx_buf), 0U, rx_buf, &rx_len, NULL);

    if (ret == ESP_ERR_TIMEOUT) {
        return ESP_OK;
    }
    return ret;
}

/*
 * brief : mfrc522_read_block.
 * input : see parameters.
 * output: return value from this function.
 * type  : public
 */
esp_err_t mfrc522_read_block(uint8_t block_addr, uint8_t out_block[MFRC522_BLOCK_LEN]) {
    if (out_block == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t tx_buf[4] = {MFRC522_PICC_CMD_MF_READ, block_addr, 0x00U, 0x00U};
    uint8_t crc_buf[2] = {0};

    esp_err_t ret = _mfrc522_calc_crc(tx_buf, 2U, crc_buf);
    if (ret != ESP_OK) {
        return ret;
    }
    tx_buf[2] = crc_buf[0];
    tx_buf[3] = crc_buf[1];

    uint8_t rx_buf[18] = {0};
    uint8_t rx_len = sizeof(rx_buf);
    uint8_t rx_last_bits = 0;

    ret = _mfrc522_transceive(tx_buf, sizeof(tx_buf), 0U, rx_buf, &rx_len, &rx_last_bits);
    if (ret != ESP_OK) {
        return ret;
    }
    if ((rx_len != 18U) || (rx_last_bits != 0U)) {
        return ESP_ERR_INVALID_RESPONSE;
    }

    ret = _mfrc522_calc_crc(rx_buf, MFRC522_BLOCK_LEN, crc_buf);
    if (ret != ESP_OK) {
        return ret;
    }

    if ((crc_buf[0] != rx_buf[16]) || (crc_buf[1] != rx_buf[17])) {
        return ESP_ERR_INVALID_RESPONSE;
    }

    memcpy(out_block, rx_buf, MFRC522_BLOCK_LEN);
    return ESP_OK;
}

/*
 * brief : mfrc522_write_block.
 * input : see parameters.
 * output: return value from this function.
 * type  : public
 */
esp_err_t mfrc522_write_block(uint8_t block_addr, const uint8_t in_block[MFRC522_BLOCK_LEN]) {
    if (in_block == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t tx_cmd[4] = {MFRC522_PICC_CMD_MF_WRITE, block_addr, 0x00U, 0x00U};
    uint8_t crc_buf[2] = {0};

    esp_err_t ret = _mfrc522_calc_crc(tx_cmd, 2U, crc_buf);
    if (ret != ESP_OK) {
        return ret;
    }
    tx_cmd[2] = crc_buf[0];
    tx_cmd[3] = crc_buf[1];

    uint8_t ack_buf[3] = {0};
    uint8_t ack_len = sizeof(ack_buf);
    uint8_t ack_last_bits = 0;

    ret = _mfrc522_transceive(tx_cmd, sizeof(tx_cmd), 0U, ack_buf, &ack_len, &ack_last_bits);
    if (ret != ESP_OK) {
        return ret;
    }
    if (!_mfrc522_is_ack(ack_buf, ack_len, ack_last_bits)) {
        return ESP_ERR_INVALID_RESPONSE;
    }

    uint8_t tx_data[18] = {0};
    memcpy(tx_data, in_block, MFRC522_BLOCK_LEN);
    ret = _mfrc522_calc_crc(in_block, MFRC522_BLOCK_LEN, crc_buf);
    if (ret != ESP_OK) {
        return ret;
    }
    tx_data[16] = crc_buf[0];
    tx_data[17] = crc_buf[1];

    ack_len = sizeof(ack_buf);
    ack_last_bits = 0;
    ret = _mfrc522_transceive(tx_data, sizeof(tx_data), 0U, ack_buf, &ack_len, &ack_last_bits);
    if (ret != ESP_OK) {
        return ret;
    }
    if (!_mfrc522_is_ack(ack_buf, ack_len, ack_last_bits)) {
        return ESP_ERR_INVALID_RESPONSE;
    }

    return ESP_OK;
}

/*
 * brief : mfrc522_read_sector.
 * input : see parameters.
 * output: return value from this function.
 * type  : public
 */
esp_err_t mfrc522_read_sector(uint8_t sector, mfrc522_key_type_t key_type,
                              const uint8_t key[MFRC522_KEY_LEN], const mfrc522_uid_t* uid,
                              mfrc522_sector_data_t* out_sector) {
    if ((uid == NULL) || (out_sector == NULL)) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t first_block = 0;
    uint8_t block_count = 0;
    esp_err_t ret = _mfrc522_get_sector_layout(sector, &first_block, &block_count);
    if (ret != ESP_OK) {
        return ret;
    }

    memset(out_sector, 0, sizeof(*out_sector));
    out_sector->sector = sector;
    out_sector->first_block = first_block;
    out_sector->block_count = block_count;

    uint8_t trailer_block = (uint8_t)(first_block + block_count - 1U);
    ret = mfrc522_authenticate(key_type, trailer_block, key, uid);
    if (ret != ESP_OK) {
        return ret;
    }

    for (uint8_t i = 0; i < block_count; i++) {
        ret = mfrc522_read_block((uint8_t)(first_block + i), out_sector->block_data[i]);
        if (ret != ESP_OK) {
            mfrc522_stop_crypto();
            return ret;
        }
    }

    mfrc522_stop_crypto();
    return ESP_OK;
}
