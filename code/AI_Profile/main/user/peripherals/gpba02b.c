#include "gpba02b.h"

#define TAG "gpba02b"
#define GPBA02B_PWM_PERIOD_STEPS (256u)

static gpba02b_ctx_t s_ctx;

gpba02b_config_t g_gpba02b_config = {
    .spi_host = GPBA02B_SPI_HOST,
    .sclk_io_num = GPBA02B_IO_CLK,
    .mosi_io_num = GPBA02B_IO_MOSI,
    .miso_io_num = GPBA02B_IO_MISO,
    .cs_io_num = GPBA02B_IO_CS,
    .clock_hz = GPBA02B_DEFAULT_CLOCK_HZ,
    .queue_size = 1,
    .device_bit = GPBA02B_DEVICE_ID,
    .initialize_bus = true,
};

/*
 * brief  : Map a logical port to BUFx register address.
 * input  : port - target port (A/B/C).
 * output : BUFx register address.
 * type   : private
 */
static uint8_t gpba02b_get_buf_reg(gpba02b_port_t port) {
    switch (port) {
        case GPBA02B_PORT_A:
            return GPBA02B_REG_BUFA;
        case GPBA02B_PORT_B:
            return GPBA02B_REG_BUFB;
        case GPBA02B_PORT_C:
            return GPBA02B_REG_BUFC;
        default:
            return GPBA02B_REG_BUFA;
    }
}

/*
 * brief  : Map a logical port to DIRx register address.
 * input  : port - target port (A/B/C).
 * output : DIRx register address.
 * type   : private
 */
static uint8_t gpba02b_get_dir_reg(gpba02b_port_t port) {
    switch (port) {
        case GPBA02B_PORT_A:
            return GPBA02B_REG_DIRA;
        case GPBA02B_PORT_B:
            return GPBA02B_REG_DIRB;
        case GPBA02B_PORT_C:
            return GPBA02B_REG_DIRC;
        default:
            return GPBA02B_REG_DIRA;
    }
}

/*
 * brief  : Map a logical port to ATTx register address.
 * input  : port - target port (A/B/C).
 * output : ATTx register address.
 * type   : private
 */
static uint8_t gpba02b_get_att_reg(gpba02b_port_t port) {
    switch (port) {
        case GPBA02B_PORT_A:
            return GPBA02B_REG_ATTA;
        case GPBA02B_PORT_B:
            return GPBA02B_REG_ATTB;
        case GPBA02B_PORT_C:
            return GPBA02B_REG_ATTC;
        default:
            return GPBA02B_REG_ATTA;
    }
}

/*
 * brief  : Map a logical port to DATx register address.
 * input  : port - target port (A/B/C).
 * output : DATx register address.
 * type   : private
 */
static uint8_t gpba02b_get_data_reg(gpba02b_port_t port) {
    switch (port) {
        case GPBA02B_PORT_A:
            return GPBA02B_REG_DATAA;
        case GPBA02B_PORT_B:
            return GPBA02B_REG_DATAB;
        case GPBA02B_PORT_C:
            return GPBA02B_REG_DATAC;
        default:
            return GPBA02B_REG_DATAA;
    }
}

/*
 * brief  : Decode PWM divider index to divider value.
 * input  : div_sel - divider selector index.
 * output : divider value used by PWM clock.
 * type   : private
 */
static uint32_t gpba02b_get_pwm_divider(uint8_t div_sel) {
    switch (div_sel & GPBA02B_PWM_DIV_SEL_MASK) {
        case 0u:
            return 1u;
        case 1u:
            return 2u;
        case 2u:
            return 4u;
        case 3u:
            return 16u;
        case 4u:
            return 32u;
        case 5u:
            return 64u;
        case 6u:
            return 128u;
        case 7u:
            return 256u;
        default:
            return 1u;
    }
}

/*
 * brief  : Build one SPI command byte for GPBA02B.
 * input  : rw - read/write bit, reg - register address.
 * output : command byte.
 * type   : private
 */
static inline uint8_t gpba02b_cmd(uint8_t rw, uint8_t reg) {
    return (uint8_t)(rw | ((s_ctx.device_bit & 0x01u) << 6) | (reg & 0x3Fu));
}

/*
 * brief  : Check whether a port value is valid.
 * input  : port - target port.
 * output : true for valid port, false for invalid.
 * type   : private
 */
static bool gpba02b_port_valid(gpba02b_port_t port) {
    return port == GPBA02B_PORT_A || port == GPBA02B_PORT_B || port == GPBA02B_PORT_C;
}

/*
 * brief  : Validate port and pin range.
 * input  : port - target port, pin - target pin.
 * output : ESP_OK when valid, error code otherwise.
 * type   : private
 */
static esp_err_t gpba02b_validate_port_pin(gpba02b_port_t port, uint8_t pin) {
    if (!gpba02b_port_valid(port) || pin > 7u) {
        return ESP_ERR_INVALID_ARG;
    }
    return ESP_OK;
}

/*
 * brief  : Execute one 16-bit SPI transfer.
 * input  : tx0/tx1 - transmit bytes, rx1 - optional receive byte pointer.
 * output : ESP_OK on success, error code on failure.
 * type   : private
 */
static esp_err_t gpba02b_xfer(uint8_t tx0, uint8_t tx1, uint8_t* rx1) {
    if (!s_ctx.initialized || s_ctx.spi == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t tx_buf[2] = {tx0, tx1};
    uint8_t rx_buf[2] = {0, 0};

    spi_transaction_t t = {
        .flags = 0,
        .length = 16,
        .rxlength = 16,
        .tx_buffer = tx_buf,
        .rx_buffer = rx_buf,
    };
    esp_err_t err = spi_device_transmit(s_ctx.spi, &t);

    if (err != ESP_OK) {
        return err;
    }

    if (rx1 != NULL) {
        *rx1 = rx_buf[1];
    }

    return ESP_OK;
}

/*
 * brief  : Write one register.
 * input  : reg - register address, value - value to write.
 * output : ESP_OK on success, error code on failure.
 * type   : private
 */
static esp_err_t gpba02b_write_reg(uint8_t reg, uint8_t value) {
    return gpba02b_xfer(gpba02b_cmd(GPBA02B_CMD_WRITE, reg), value, NULL);
}

/*
 * brief  : Read one register.
 * input  : reg - register address, value - output buffer.
 * output : ESP_OK on success, error code on failure.
 * type   : private
 */
static esp_err_t gpba02b_read_reg(uint8_t reg, uint8_t* value) {
    if (value == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    return gpba02b_xfer(gpba02b_cmd(GPBA02B_CMD_READ, reg), 0x00u, value);
}

/*
 * brief  : Unlock new-function register bank.
 * input  : none.
 * output : ESP_OK on success, error code on failure.
 * type   : private
 */
static esp_err_t gpba02b_unlock_new_function_regs(void) {
    if (s_ctx.new_function_enabled) {
        return ESP_OK;
    }

    /*
     * GPBA02B datasheet requires each 16-bit frame to have its own CS pulse.
     * Therefore the unlock key sequence must be sent as 4 independent writes.
     */
    esp_err_t err = gpba02b_write_reg(GPBA02B_REG_NEW_FUNC_03, 0x55u);
    if (err != ESP_OK) {
        return err;
    }

    err = gpba02b_write_reg(GPBA02B_REG_NEW_FUNC_07, 0xAAu);
    if (err != ESP_OK) {
        return err;
    }

    err = gpba02b_write_reg(GPBA02B_REG_NEW_FUNC_03, 0x55u);
    if (err != ESP_OK) {
        return err;
    }

    err = gpba02b_write_reg(GPBA02B_REG_NEW_FUNC_07, 0xAAu);
    if (err != ESP_OK) {
        return err;
    }

    s_ctx.new_function_enabled = true;
    return ESP_OK;
}

/*
 * brief  : Pick nearest PWM divider for target frequency.
 * input  : target_hz - desired PWM frequency.
 * output : best divider selector index.
 * type   : private
 */
static uint8_t gpba02b_pick_pwm_divider(uint32_t target_hz) {
    uint8_t best_index = 0u;
    uint32_t best_diff = UINT32_MAX;

    for (uint8_t i = 0u; i < 8u; ++i) {
        uint32_t actual =
            GPBA02B_PWM_BASE_CLOCK_HZ / (gpba02b_get_pwm_divider(i) * GPBA02B_PWM_PERIOD_STEPS);
        uint32_t diff = (actual > target_hz) ? (actual - target_hz) : (target_hz - actual);
        if (diff < best_diff) {
            best_diff = diff;
            best_index = i;
        }
    }

    return best_index;
}

/*
 * brief  : Resolve duty register address for one PWM pin.
 * input  : port - PWM port, pin - PWM pin, reg - output register pointer.
 * output : ESP_OK on success, error code on failure.
 * type   : private
 */
static esp_err_t gpba02b_resolve_pwm_duty_reg(gpba02b_port_t port, uint8_t pin, uint8_t* reg) {
    if (reg == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (pin > 7u) {
        return ESP_ERR_INVALID_ARG;
    }

    if (port == GPBA02B_PORT_A) {
        *reg = (pin <= 3u) ? (uint8_t)(GPBA02B_REG_PA_DUTY0 + pin)
                           : (uint8_t)(GPBA02B_REG_PA_DUTY4 + (pin - 4u));
        return ESP_OK;
    }

    if (port == GPBA02B_PORT_C) {
        if (pin == 0u) {
            *reg = GPBA02B_REG_PC_DUTY0;
        } else if (pin == 1u) {
            *reg = GPBA02B_REG_PC_DUTY1;
        } else if (pin == 2u) {
            *reg = GPBA02B_REG_PC_DUTY2;
        } else {
            *reg = (uint8_t)(GPBA02B_REG_PC_DUTY3 + (pin - 3u));
        }
        return ESP_OK;
    }

    return ESP_ERR_NOT_SUPPORTED;
}

/*
 * brief  : Initialize GPBA02B SPI driver with shared global object.
 * input  : none.
 * output : ESP_OK on success, error code on failure.
 * type   : public
 */
esp_err_t gpba02b_init_object(void) {
    const gpba02b_config_t* config = &g_gpba02b_config;

    if (s_ctx.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (config->clock_hz <= 0 || config->clock_hz > 8000000) {
        return ESP_ERR_INVALID_ARG;
    }

    if (config->queue_size <= 0) {
        return ESP_ERR_INVALID_ARG;
    }

    if (config->device_bit > 1u) {
        return ESP_ERR_INVALID_ARG;
    }

    if (config->cs_io_num == GPIO_NUM_NC) {
        return ESP_ERR_INVALID_ARG;
    }

    if (config->initialize_bus) {
        if (config->sclk_io_num == GPIO_NUM_NC || config->mosi_io_num == GPIO_NUM_NC ||
            config->miso_io_num == GPIO_NUM_NC) {
            return ESP_ERR_INVALID_ARG;
        }

        spi_bus_config_t bus_cfg = {
            .mosi_io_num = config->mosi_io_num,
            .miso_io_num = config->miso_io_num,
            .sclk_io_num = config->sclk_io_num,
            .quadwp_io_num = GPIO_NUM_NC,
            .quadhd_io_num = GPIO_NUM_NC,
            .max_transfer_sz = 0,
            .intr_flags = 0,
        };

        esp_err_t err = spi_bus_initialize(config->spi_host, &bus_cfg, SPI_DMA_CH_AUTO);
        if (err != ESP_OK) {
            return err;
        }
        s_ctx.bus_initialized = true;
    }

    spi_device_interface_config_t dev_cfg = {
        .command_bits = 0,
        .address_bits = 0,
        .dummy_bits = 0,
        .mode = 0,
        .duty_cycle_pos = 128,
        .cs_ena_pretrans = 0,
        .cs_ena_posttrans = 0,
        .clock_speed_hz = config->clock_hz,
        .input_delay_ns = 0,
        .spics_io_num = config->cs_io_num,
        .flags = 0,
        .queue_size = config->queue_size,
        .pre_cb = NULL,
        .post_cb = NULL,
    };

    esp_err_t err = spi_bus_add_device(config->spi_host, &dev_cfg, &s_ctx.spi);
    if (err != ESP_OK) {
        if (s_ctx.bus_initialized) {
            spi_bus_free(config->spi_host);
            s_ctx.bus_initialized = false;
        }
        return err;
    }

    s_ctx.spi_host = config->spi_host;
    s_ctx.device_bit = config->device_bit;
    s_ctx.new_function_enabled = false;
    s_ctx.pa_pwm_enable_shadow = 0x00u;
    s_ctx.pc_pwm_enable_shadow = 0x00u;
    s_ctx.pwmck_shadow = 0x00u;
    s_ctx.initialized = true;

    return ESP_OK;
}

/*
 * brief  : Deinitialize GPBA02B SPI driver.
 * input  : none.
 * output : ESP_OK on success, error code on failure.
 * type   : public
 */
esp_err_t gpba02b_deinit(void) {
    if (!s_ctx.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = spi_bus_remove_device(s_ctx.spi);
    if (err != ESP_OK) {
        return err;
    }

    s_ctx.spi = NULL;
    s_ctx.initialized = false;
    s_ctx.new_function_enabled = false;

    if (s_ctx.bus_initialized) {
        err = spi_bus_free(s_ctx.spi_host);
        if (err != ESP_OK) {
            return err;
        }
        s_ctx.bus_initialized = false;
    }

    return ESP_OK;
}

/*
 * brief  : Configure GPIO mode style for one pin.
 * input  : port - target port, pin - target pin, style - IO style.
 * output : ESP_OK on success, error code on failure.
 * type   : public
 */
esp_err_t gpba02b_set_io_mode(gpba02b_port_t port, uint8_t pin, gpba02b_io_style_t style) {
    esp_err_t err = gpba02b_validate_port_pin(port, pin);
    if (err != ESP_OK) {
        return err;
    }

    uint8_t buf_reg = gpba02b_get_buf_reg(port);
    uint8_t dir_reg = gpba02b_get_dir_reg(port);
    uint8_t att_reg = gpba02b_get_att_reg(port);
    uint8_t mask = (uint8_t)(1u << pin);

    uint8_t buf_val = 0;
    uint8_t dir_val = 0;
    uint8_t att_val = 0;

    err = gpba02b_read_reg(buf_reg, &buf_val);
    if (err != ESP_OK) {
        return err;
    }
    err = gpba02b_read_reg(dir_reg, &dir_val);
    if (err != ESP_OK) {
        return err;
    }
    err = gpba02b_read_reg(att_reg, &att_val);
    if (err != ESP_OK) {
        return err;
    }

    switch (style) {
        case GPBA02B_IO_STYLE_INPUT_HIGH_Z:
            buf_val &= (uint8_t)~mask;
            dir_val &= (uint8_t)~mask;
            att_val &= (uint8_t)~mask;
            break;
        case GPBA02B_IO_STYLE_INPUT_PULL_LOW:
            buf_val |= mask;
            dir_val &= (uint8_t)~mask;
            att_val &= (uint8_t)~mask;
            break;
        case GPBA02B_IO_STYLE_INPUT_PULL_HIGH:
            buf_val |= mask;
            dir_val &= (uint8_t)~mask;
            att_val |= mask;
            break;
        case GPBA02B_IO_STYLE_OUTPUT_CMOS:
            dir_val |= mask;
            att_val &= (uint8_t)~mask;
            break;
        case GPBA02B_IO_STYLE_OUTPUT_CMOS_INVERTED:
            dir_val |= mask;
            att_val |= mask;
            break;
        case GPBA02B_IO_STYLE_OUTPUT_OPEN_DRAIN_NMOS:
            buf_val &= (uint8_t)~mask;
            dir_val &= (uint8_t)~mask;
            att_val &= (uint8_t)~mask;
            break;
        case GPBA02B_IO_STYLE_OUTPUT_OPEN_DRAIN_PMOS:
            buf_val &= (uint8_t)~mask;
            dir_val &= (uint8_t)~mask;
            att_val |= mask;
            break;
        default:
            return ESP_ERR_INVALID_ARG;
    }

    /* Write order avoids glitches for styles that depend on BUFx first. */
    err = gpba02b_write_reg(buf_reg, buf_val);
    if (err != ESP_OK) {
        return err;
    }
    err = gpba02b_write_reg(att_reg, att_val);
    if (err != ESP_OK) {
        return err;
    }
    return gpba02b_write_reg(dir_reg, dir_val);
}

/*
 * brief  : Read GPIO level from one pin.
 * input  : port - target port, pin - target pin, level - output level pointer.
 * output : ESP_OK on success, error code on failure.
 * type   : public
 */
esp_err_t gpba02b_read_io_level(gpba02b_port_t port, uint8_t pin, uint8_t* level) {
    if (level == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = gpba02b_validate_port_pin(port, pin);
    if (err != ESP_OK) {
        return err;
    }

    uint8_t data = 0;
    err = gpba02b_read_reg(gpba02b_get_data_reg(port), &data);
    if (err != ESP_OK) {
        return err;
    }

    *level = (uint8_t)((data >> pin) & 0x01u);
    return ESP_OK;
}

/*
 * brief  : Write GPIO level to one pin.
 * input  : port - target port, pin - target pin, level - logic level.
 * output : ESP_OK on success, error code on failure.
 * type   : public
 */
esp_err_t gpba02b_write_io_level(gpba02b_port_t port, uint8_t pin, uint8_t level) {
    esp_err_t err = gpba02b_validate_port_pin(port, pin);
    if (err != ESP_OK) {
        return err;
    }

    uint8_t normalized_level = (level != 0u) ? 1u : 0u;
    uint8_t mask = (uint8_t)(1u << pin);

    uint8_t buf_reg = gpba02b_get_buf_reg(port);
    uint8_t dir_reg = gpba02b_get_dir_reg(port);
    uint8_t att_reg = gpba02b_get_att_reg(port);

    uint8_t buf_val = 0;
    uint8_t dir_val = 0;
    uint8_t att_val = 0;

    err = gpba02b_read_reg(buf_reg, &buf_val);
    if (err != ESP_OK) {
        return err;
    }
    err = gpba02b_read_reg(dir_reg, &dir_val);
    if (err != ESP_OK) {
        return err;
    }
    err = gpba02b_read_reg(att_reg, &att_val);
    if (err != ESP_OK) {
        return err;
    }

    bool dir_bit = (dir_val & mask) != 0u;
    bool att_bit = (att_val & mask) != 0u;
    bool buf_bit = (buf_val & mask) != 0u;

    if (dir_bit) {
        if (!att_bit) {
            /* Normal CMOS output: BUFx bit is output level. */
            if (normalized_level != 0u) {
                buf_val |= mask;
            } else {
                buf_val &= (uint8_t)~mask;
            }
        } else {
            /* Inverted CMOS output: BUFx bit is inverted level. */
            if (normalized_level != 0u) {
                buf_val &= (uint8_t)~mask;
            } else {
                buf_val |= mask;
            }
        }
        return gpba02b_write_reg(buf_reg, buf_val);
    }

    if (!buf_bit) {
        /* Open-drain modes use DIRx as data selector. */
        if (!att_bit) {
            /* NMOS: DIR=1 drives low, DIR=0 floating. */
            if (normalized_level != 0u) {
                dir_val &= (uint8_t)~mask;
            } else {
                dir_val |= mask;
            }
        } else {
            /* PMOS: DIR=1 drives high, DIR=0 floating. */
            if (normalized_level != 0u) {
                dir_val |= mask;
            } else {
                dir_val &= (uint8_t)~mask;
            }
        }
        return gpba02b_write_reg(dir_reg, dir_val);
    }

    return ESP_ERR_INVALID_STATE;
}

/*
 * brief  : Configure one pin to PWM mode.
 * input  : port - PWM port (A/C), pin - PWM pin.
 * output : ESP_OK on success, error code on failure.
 * type   : public
 */
esp_err_t gpba02b_config_pwm_mode(gpba02b_port_t port, uint8_t pin) {
    if ((port != GPBA02B_PORT_A && port != GPBA02B_PORT_C) || pin > 7u) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = gpba02b_unlock_new_function_regs();
    if (err != ESP_OK) {
        return err;
    }

    uint8_t mask = (uint8_t)(1u << pin);
    uint8_t att_reg = gpba02b_get_att_reg(port);

    uint8_t att_val = 0;

    err = gpba02b_read_reg(att_reg, &att_val);
    if (err != ESP_OK) {
        return err;
    }

    /* Keep default PWM polarity as non-inverted. */
    att_val &= (uint8_t)~mask;

    err = gpba02b_write_reg(att_reg, att_val);
    if (err != ESP_OK) {
        return err;
    }

    if (port == GPBA02B_PORT_A) {
        s_ctx.pa_pwm_enable_shadow |= mask;
        return gpba02b_write_reg(GPBA02B_REG_PA_PWM_ENABLE, s_ctx.pa_pwm_enable_shadow);
    }

    s_ctx.pc_pwm_enable_shadow |= mask;
    return gpba02b_write_reg(GPBA02B_REG_PC_PWM_ENABLE, s_ctx.pc_pwm_enable_shadow);
}

/*
 * brief  : Set PWM base frequency for one port.
 * input  : port - PWM port (A/C), frequency_hz - target frequency.
 * output : ESP_OK on success, error code on failure.
 * type   : public
 */
esp_err_t gpba02b_set_pwm_frequency(gpba02b_port_t port, uint32_t frequency_hz) {
    if ((port != GPBA02B_PORT_A && port != GPBA02B_PORT_C) || frequency_hz == 0u) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = gpba02b_unlock_new_function_regs();
    if (err != ESP_OK) {
        return err;
    }

    uint8_t div_sel = gpba02b_pick_pwm_divider(frequency_hz) & GPBA02B_PWM_DIV_SEL_MASK;

    if (port == GPBA02B_PORT_A) {
        s_ctx.pwmck_shadow = (uint8_t)((s_ctx.pwmck_shadow & (uint8_t)~GPBA02B_PWMCK_PA_DIV_MASK) |
                                       (uint8_t)(div_sel << GPBA02B_PWMCK_PA_DIV_SHIFT));
    } else {
        s_ctx.pwmck_shadow =
            (uint8_t)((s_ctx.pwmck_shadow & (uint8_t)~GPBA02B_PWMCK_PC_DIV_MASK) | div_sel);
    }

    return gpba02b_write_reg(GPBA02B_REG_PWMCK, s_ctx.pwmck_shadow);
}

/*
 * brief  : Set PWM duty for one pin.
 * input  : port - PWM port (A/C), pin - PWM pin, duty - duty value.
 * output : ESP_OK on success, error code on failure.
 * type   : public
 */
esp_err_t gpba02b_set_pwm_duty(gpba02b_port_t port, uint8_t pin, uint8_t duty) {
    uint8_t reg = 0;
    esp_err_t err = gpba02b_resolve_pwm_duty_reg(port, pin, &reg);
    if (err != ESP_OK) {
        return err;
    }

    err = gpba02b_unlock_new_function_regs();
    if (err != ESP_OK) {
        return err;
    }

    return gpba02b_write_reg(reg, duty);
}

esp_err_t gpba02b_read_register(uint8_t reg, uint8_t* value) {
    if (value == NULL || reg > 0x3Fu) {
        return ESP_ERR_INVALID_ARG;
    }

    /*
     * Per GPBA02B datasheet, non-0xH address space is write-only/special command.
     * Also 03H/07H are write-only new-function-enable registers.
     */
    if ((reg & 0x30u) != 0u || reg == GPBA02B_REG_NEW_FUNC_03 || reg == GPBA02B_REG_NEW_FUNC_07) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    return gpba02b_read_reg(reg, value);
}

esp_err_t gpba02b_set_device_id(uint8_t device_bit) {
    if (device_bit > 1u) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_ctx.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    s_ctx.device_bit = device_bit;
    s_ctx.new_function_enabled = false;
    return ESP_OK;
}

uint8_t gpba02b_get_device_id(void) { return s_ctx.device_bit; }
