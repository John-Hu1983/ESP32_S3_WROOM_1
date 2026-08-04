#include "gpba02b.h"

#define TAG "GPBA02B"

static usr_spi_s s_gpba02_spi;
static bool s_gpba02b_ready = false;
static uint8_t s_reg23_shadow = 0;
static gpba02b_int_callback_t s_int_callback = NULL;
static void *s_int_callback_ctx = NULL;
static const gpba02b_gpio_s s_gpio_regs[3] = {
    {GPBA02B_REG_BUFA, GPBA02B_REG_DIRA, GPBA02B_REG_ATTA, GPBA02B_REG_DATAA},
    {GPBA02B_REG_BUFB, GPBA02B_REG_DIRB, GPBA02B_REG_ATTB, GPBA02B_REG_DATAB},
    {GPBA02B_REG_BUFC, GPBA02B_REG_DIRC, GPBA02B_REG_ATTC, GPBA02B_REG_DATAC},
};

/*
 * brief: Build one GPBA02B SPI command byte from direction and register address.
 * input: is_write - true for write command, false for read command; reg_addr - register address.
 * output: Encoded command byte for SPI transfer.
 */
static uint8_t gpba02b_build_command(bool is_write, uint8_t reg_addr)
{
    return (uint8_t)(((is_write ? 1U : 0U) << 7) |
                     ((GPBA02B_DEVICE_SELECT_BIT & 0x01U) << 6) |
                     (reg_addr & GPBA02B_REG_ADDR_MASK));
}

/*
 * brief: Validate whether the selected GPBA02B port enum is supported.
 * input: port - target port identifier.
 * output: true for valid port A/B/C; otherwise false.
 */
static bool gpba02b_port_is_valid(gpba02b_port_t port)
{
    return (port == GPBA02B_PORT_A) || (port == GPBA02B_PORT_B) || (port == GPBA02B_PORT_C);
}

/*
 * brief: Get GPIO register mapping (buf/dir/att/data) for a specified port.
 * input: port - target port; gpio_regs - output pointer to register mapping.
 * output: ESP_OK on success; otherwise ESP_ERR_INVALID_ARG.
 */
esp_err_t gpba02b_get_gpio_regs(gpba02b_port_t port, const gpba02b_gpio_s **gpio_regs)
{
    if ((gpio_regs == NULL) || !gpba02b_port_is_valid(port))
    {
        return ESP_ERR_INVALID_ARG;
    }

    *gpio_regs = &s_gpio_regs[(int)port];
    return ESP_OK;
}

/*
 * brief: Set or clear one bit in an 8-bit value.
 * input: value - source byte; bit - bit index 0..7; set - desired bit state.
 * output: Updated byte with target bit modified.
 */
static uint8_t gpba02b_set_bit(uint8_t value, uint8_t bit, bool set)
{
    uint8_t mask = (uint8_t)(1U << bit);
    if (set)
    {
        return (uint8_t)(value | mask);
    }
    return (uint8_t)(value & (uint8_t)(~mask));
}

/*
 * brief: Get PWM enable register address for Port A or Port C.
 * input: port - target PWM port; reg - output register address pointer.
 * output: ESP_OK on success; otherwise ESP_ERR_INVALID_ARG.
 */
static esp_err_t gpba02b_get_pwm_enable_reg(gpba02b_port_t port, uint8_t *reg)
{
    if (reg == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    if (port == GPBA02B_PORT_A)
    {
        *reg = GPBA02B_REG_PA_PWM_ENABLE;
        return ESP_OK;
    }
    if (port == GPBA02B_PORT_C)
    {
        *reg = GPBA02B_REG_PC_PWM_ENABLE;
        return ESP_OK;
    }
    return ESP_ERR_INVALID_ARG;
}

/*
 * brief: Convert PWM port+channel to the corresponding duty register address.
 * input: port - target PWM port; channel - channel index 0..7; reg - output register address pointer.
 * output: ESP_OK on success; otherwise ESP_ERR_INVALID_ARG.
 */
static esp_err_t gpba02b_get_pwm_duty_reg(gpba02b_port_t port, uint8_t channel, uint8_t *reg)
{
    static const uint8_t pc_duty_map[GPBA02B_PORT_PIN_COUNT] = {0x2F, 0x33, 0x37, 0x3B,
                                                                0x3C, 0x3D, 0x3E, 0x3F};

    if ((reg == NULL) || (channel >= GPBA02B_PORT_PIN_COUNT))
    {
        return ESP_ERR_INVALID_ARG;
    }

    if (port == GPBA02B_PORT_A)
    {
        if (channel < 4U)
        {
            *reg = (uint8_t)(GPBA02B_REG_PA0_DUTY + channel);
        }
        else
        {
            *reg = (uint8_t)(GPBA02B_REG_PA4_DUTY + (channel - 4U));
        }
        return ESP_OK;
    }

    if (port == GPBA02B_PORT_C)
    {
        *reg = pc_duty_map[channel];
        return ESP_OK;
    }

    return ESP_ERR_INVALID_ARG;
}

/*
 * brief: Write one GPBA02B register over SPI using a 2-byte command+data frame.
 * input: reg_addr - register address; value - register value to write.
 * output: ESP_OK on success; otherwise SPI driver error code.
 */
static esp_err_t _write_reg_via_spi(uint8_t reg_addr, uint8_t value)
{
    uint8_t frame[2];
    esp_err_t ret;
    esp_err_t unlock_ret;

    frame[0] = gpba02b_build_command(true, reg_addr);
    frame[1] = value;

    ret = spi_bus_acquire(s_gpba02_spi.host, portMAX_DELAY);
    if (ret != ESP_OK)
    {
        return ret;
    }

    ret = spi_write_nbyte(&s_gpba02_spi, frame, sizeof(frame));
    unlock_ret = spi_bus_release(s_gpba02_spi.host);
    if (ret != ESP_OK)
    {
        return ret;
    }

    return unlock_ret;
}

/*
 * brief: Read one GPBA02B register over SPI by issuing command then reading one byte.
 * input: reg_addr - register address; value - output pointer for read value.
 * output: ESP_OK on success; otherwise ESP_ERR_INVALID_ARG or SPI driver error.
 */
static esp_err_t _read_reg_via_spi(uint8_t reg_addr, uint8_t *value)
{
    uint8_t cmd;
    esp_err_t ret;
    esp_err_t unlock_ret;

    if (value == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    ret = spi_bus_acquire(s_gpba02_spi.host, portMAX_DELAY);
    if (ret != ESP_OK)
    {
        return ret;
    }

    cmd = gpba02b_build_command(false, reg_addr);
    ret = spi_write_read_nbyte(&s_gpba02_spi, &cmd, 1, value, 1);
    unlock_ret = spi_bus_release(s_gpba02_spi.host);
    if (ret != ESP_OK)
    {
        return ret;
    }

    return unlock_ret;
}

/*
 * brief: Send GPBA02B new-function unlock sequence after reset.
 * input: None.
 * output: ESP_OK on success; otherwise SPI write error.
 */
static esp_err_t gpba02b_enable_new_functions(void)
{
    esp_err_t ret;

    ret = _write_reg_via_spi(GPBA02B_REG_FUNC_ENABLE_03, 0x55);
    if (ret != ESP_OK)
    {
        return ret;
    }

    ret = _write_reg_via_spi(GPBA02B_REG_FUNC_ENABLE_07, 0xAA);
    if (ret != ESP_OK)
    {
        return ret;
    }

    ret = _write_reg_via_spi(GPBA02B_REG_FUNC_ENABLE_03, 0x55);
    if (ret != ESP_OK)
    {
        return ret;
    }

    ret = _write_reg_via_spi(GPBA02B_REG_FUNC_ENABLE_07, 0xAA);
    return ret;
}

/*
 * brief: Initialize SPI backend and GPBA02B extended function state.
 * input: None.
 * output: ESP_OK on success; otherwise SPI/device initialization error.
 */
esp_err_t gpba02b_init_device(void)
{
    esp_err_t ret;

    if (s_gpba02b_ready)
    {
        return ESP_OK;
    }

    ret = spi_create_device(&s_gpba02_spi,
                            GPBA02B_SPI_HOST,
                            GPBA02_IO_MISO,
                            GPBA02_IO_MOSI,
                            GPBA02_IO_CLK,
                            GPBA02_IO_CS,
                            GPBA02_DEFAULT_CLOCK_HZ);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "spi_create_device failed: %d", (int)ret);
        return ret;
    }

    ret = gpba02b_enable_new_functions();
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "enable new functions failed: %d", (int)ret);
        return ret;
    }

    s_gpba02b_ready = true;
    s_reg23_shadow = 0;
    return ESP_OK;
}

/*
 * brief: Public API to write one GPBA02B register.
 * input: reg_addr - register address; value - data byte.
 * output: ESP_OK on success; otherwise ESP_ERR_INVALID_STATE or SPI error.
 */
esp_err_t gpba02b_reg_write(uint8_t reg_addr, uint8_t value)
{
    if (!s_gpba02b_ready)
    {
        return ESP_ERR_INVALID_STATE;
    }
    return _write_reg_via_spi(reg_addr, value);
}

/*
 * brief: Public API to read one GPBA02B register.
 * input: reg_addr - register address; value - output pointer for data byte.
 * output: ESP_OK on success; otherwise ESP_ERR_INVALID_STATE or SPI error.
 */
esp_err_t gpba02b_reg_read(uint8_t reg_addr, uint8_t *value)
{
    if (!s_gpba02b_ready)
    {
        return ESP_ERR_INVALID_STATE;
    }
    return _read_reg_via_spi(reg_addr, value);
}

/*
 * brief: Configure one GPIO pin mode by updating BUF/DIR/ATT registers.
 * input: port - target port; pin - pin index 0..7; mode - desired pin mode.
 * output: ESP_OK on success; otherwise parameter/state/SPI error.
 */
esp_err_t gpba02b_pin_set_mode(gpba02b_port_t port, uint8_t pin, gpba02b_pin_mode_t mode)
{
    const gpba02b_gpio_s *gpio_regs;
    uint8_t buf_val;
    uint8_t dir_val;
    uint8_t att_val;
    esp_err_t ret;

    if (!s_gpba02b_ready || (pin >= GPBA02B_PORT_PIN_COUNT) || !gpba02b_port_is_valid(port))
    {
        return ESP_ERR_INVALID_ARG;
    }

    ret = gpba02b_get_gpio_regs(port, &gpio_regs);
    if (ret != ESP_OK)
    {
        return ret;
    }

    ret = _read_reg_via_spi(gpio_regs->buf, &buf_val);
    if (ret != ESP_OK)
    {
        return ret;
    }
    ret = _read_reg_via_spi(gpio_regs->dir, &dir_val);
    if (ret != ESP_OK)
    {
        return ret;
    }
    ret = _read_reg_via_spi(gpio_regs->att, &att_val);
    if (ret != ESP_OK)
    {
        return ret;
    }

    switch (mode)
    {
    case GPBA02B_PIN_MODE_INPUT_FLOATING:
        buf_val = gpba02b_set_bit(buf_val, pin, false);
        dir_val = gpba02b_set_bit(dir_val, pin, false);
        att_val = gpba02b_set_bit(att_val, pin, false);
        break;
    case GPBA02B_PIN_MODE_INPUT_PULLDOWN:
        buf_val = gpba02b_set_bit(buf_val, pin, true);
        dir_val = gpba02b_set_bit(dir_val, pin, false);
        att_val = gpba02b_set_bit(att_val, pin, false);
        break;
    case GPBA02B_PIN_MODE_INPUT_PULLUP:
        buf_val = gpba02b_set_bit(buf_val, pin, true);
        dir_val = gpba02b_set_bit(dir_val, pin, false);
        att_val = gpba02b_set_bit(att_val, pin, true);
        break;
    case GPBA02B_PIN_MODE_OUTPUT:
        dir_val = gpba02b_set_bit(dir_val, pin, true);
        att_val = gpba02b_set_bit(att_val, pin, false);
        break;
    case GPBA02B_PIN_MODE_OUTPUT_INVERTED:
        dir_val = gpba02b_set_bit(dir_val, pin, true);
        att_val = gpba02b_set_bit(att_val, pin, true);
        break;
    case GPBA02B_PIN_MODE_OPEN_DRAIN_NMOS:
        buf_val = gpba02b_set_bit(buf_val, pin, false);
        dir_val = gpba02b_set_bit(dir_val, pin, false);
        att_val = gpba02b_set_bit(att_val, pin, false);
        break;
    case GPBA02B_PIN_MODE_OPEN_DRAIN_PMOS:
        buf_val = gpba02b_set_bit(buf_val, pin, false);
        dir_val = gpba02b_set_bit(dir_val, pin, false);
        att_val = gpba02b_set_bit(att_val, pin, true);
        break;
    default:
        return ESP_ERR_INVALID_ARG;
    }

    ret = _write_reg_via_spi(gpio_regs->buf, buf_val);
    if (ret != ESP_OK)
    {
        return ret;
    }
    ret = _write_reg_via_spi(gpio_regs->att, att_val);
    if (ret != ESP_OK)
    {
        return ret;
    }
    ret = _write_reg_via_spi(gpio_regs->dir, dir_val);
    return ret;
}

/*
 * brief: Write full 8-bit output buffer value for one GPBA02B port.
 * input: port - target port; value - output byte.
 * output: ESP_OK on success; otherwise parameter/state/SPI error.
 */
esp_err_t gpba02b_port_write(gpba02b_port_t port, uint8_t value)
{
    const gpba02b_gpio_s *gpio_regs;
    esp_err_t ret;

    if (!s_gpba02b_ready || !gpba02b_port_is_valid(port))
    {
        return ESP_ERR_INVALID_ARG;
    }

    ret = gpba02b_get_gpio_regs(port, &gpio_regs);
    if (ret != ESP_OK)
    {
        return ret;
    }
    return _write_reg_via_spi(gpio_regs->buf, value);
}

/*
 * brief: Read full 8-bit DATA register value of one GPBA02B port.
 * input: port - target port; value - output pointer for input status byte.
 * output: ESP_OK on success; otherwise parameter/state/SPI error.
 */
esp_err_t gpba02b_port_read(gpba02b_port_t port, uint8_t *value)
{
    const gpba02b_gpio_s *gpio_regs;
    esp_err_t ret;

    if (!s_gpba02b_ready || !gpba02b_port_is_valid(port) || (value == NULL))
    {
        return ESP_ERR_INVALID_ARG;
    }

    ret = gpba02b_get_gpio_regs(port, &gpio_regs);
    if (ret != ESP_OK)
    {
        return ret;
    }
    return _read_reg_via_spi(gpio_regs->data, value);
}

/*
 * brief: Modify one output bit in BUF register for a specific GPIO pin.
 * input: port - target port; pin - pin index 0..7; level - desired output level.
 * output: ESP_OK on success; otherwise parameter/state/SPI error.
 */
esp_err_t gpba02b_pin_write(gpba02b_port_t port, uint8_t pin, bool level)
{
    const gpba02b_gpio_s *gpio_regs;
    uint8_t value;
    esp_err_t ret;

    if (!s_gpba02b_ready || !gpba02b_port_is_valid(port) || (pin >= GPBA02B_PORT_PIN_COUNT))
    {
        return ESP_ERR_INVALID_ARG;
    }

    ret = gpba02b_get_gpio_regs(port, &gpio_regs);
    if (ret != ESP_OK)
    {
        return ret;
    }

    ret = _read_reg_via_spi(gpio_regs->buf, &value);
    if (ret != ESP_OK)
    {
        return ret;
    }

    value = gpba02b_set_bit(value, pin, level);
    return _write_reg_via_spi(gpio_regs->buf, value);
}

/*
 * brief: Read one input bit from DATA register for a specific GPIO pin.
 * input: port - target port; pin - pin index 0..7; level - output pointer for pin level.
 * output: ESP_OK on success; otherwise parameter/state/SPI error.
 */
esp_err_t gpba02b_pin_read(gpba02b_port_t port, uint8_t pin, bool *level)
{
    uint8_t value;
    esp_err_t ret;

    if (!s_gpba02b_ready || !gpba02b_port_is_valid(port) || (pin >= GPBA02B_PORT_PIN_COUNT) || (level == NULL))
    {
        return ESP_ERR_INVALID_ARG;
    }

    ret = gpba02b_port_read(port, &value);
    if (ret != ESP_OK)
    {
        return ret;
    }

    *level = ((value >> pin) & 0x01U) != 0U;
    return ESP_OK;
}

/*
 * brief: Enable or disable PWM channels by bitmask on Port A or Port C.
 * input: port - PWM port A/C; channel_mask - channel bit mask; enable - true enable, false disable.
 * output: ESP_OK on success; otherwise parameter/state/SPI error.
 */
esp_err_t gpba02b_pwm_enable_mask(gpba02b_port_t port, uint8_t channel_mask, bool enable)
{
    uint8_t reg;
    uint8_t value;
    esp_err_t ret;

    if (!s_gpba02b_ready)
    {
        return ESP_ERR_INVALID_STATE;
    }

    ret = gpba02b_get_pwm_enable_reg(port, &reg);
    if (ret != ESP_OK)
    {
        return ret;
    }

    ret = _read_reg_via_spi(reg, &value);
    if (ret != ESP_OK)
    {
        return ret;
    }

    if (enable)
    {
        value = (uint8_t)(value | channel_mask);
    }
    else
    {
        value = (uint8_t)(value & (uint8_t)(~channel_mask));
    }

    return _write_reg_via_spi(reg, value);
}

/*
 * brief: Set one PWM channel duty register value.
 * input: port - PWM port A/C; channel - channel index 0..7; duty - 8-bit duty value.
 * output: ESP_OK on success; otherwise parameter/state/SPI error.
 */
esp_err_t gpba02b_pwm_set_duty(gpba02b_port_t port, uint8_t channel, uint8_t duty)
{
    uint8_t reg;
    esp_err_t ret;

    if (!s_gpba02b_ready)
    {
        return ESP_ERR_INVALID_STATE;
    }

    ret = gpba02b_get_pwm_duty_reg(port, channel, &reg);
    if (ret != ESP_OK)
    {
        return ret;
    }

    return _write_reg_via_spi(reg, duty);
}

/*
 * brief: Set PWM clock divider for Port A or Port C group.
 * input: port - PWM port A/C; div - divider enum value.
 * output: ESP_OK on success; otherwise parameter/state/SPI error.
 */
esp_err_t gpba02b_pwm_set_clock_div(gpba02b_port_t port, gpba02b_pwm_div_t div)
{
    uint8_t value;
    esp_err_t ret;

    if (!s_gpba02b_ready || (div > GPBA02B_PWM_DIV_256))
    {
        return ESP_ERR_INVALID_ARG;
    }

    if ((port != GPBA02B_PORT_A) && (port != GPBA02B_PORT_C))
    {
        return ESP_ERR_INVALID_ARG;
    }

    ret = _read_reg_via_spi(GPBA02B_REG_PWM_CLOCK, &value);
    if (ret != ESP_OK)
    {
        return ret;
    }

    if (port == GPBA02B_PORT_A)
    {
        value = (uint8_t)((value & (uint8_t)(~0x70U)) | (((uint8_t)div & 0x07U) << 4));
    }
    else
    {
        value = (uint8_t)((value & (uint8_t)(~0x07U)) | ((uint8_t)div & 0x07U));
    }

    return _write_reg_via_spi(GPBA02B_REG_PWM_CLOCK, value);
}

/*
 * brief: Configure interrupt enable bits for INT sources [3:0].
 * input: enable_mask - low 4 bits control INT enable state.
 * output: ESP_OK on success; otherwise state/SPI error.
 */
esp_err_t gpba02b_int_set_enable_mask(uint8_t enable_mask)
{
    if (!s_gpba02b_ready)
    {
        return ESP_ERR_INVALID_STATE;
    }

    return _write_reg_via_spi(GPBA02B_REG_INT_FLAG_ENABLE, (uint8_t)(enable_mask & GPBA02B_INT_MASK));
}

/*
 * brief: Configure interrupt edge type mask, 1 for falling edge and 0 for rising edge.
 * input: falling_edge_mask - low 4 bits map to INT_EDGE[3:0].
 * output: ESP_OK on success; otherwise state/SPI error.
 */
esp_err_t gpba02b_int_set_falling_edge_mask(uint8_t falling_edge_mask)
{
    if (!s_gpba02b_ready)
    {
        return ESP_ERR_INVALID_STATE;
    }

    s_reg23_shadow = (uint8_t)((s_reg23_shadow & 0x0FU) | ((falling_edge_mask & GPBA02B_INT_MASK) << 4));
    return _write_reg_via_spi(GPBA02B_REG_INT_EDGE_CMOS, s_reg23_shadow);
}

/*
 * brief: Read current latched interrupt flags from GPBA02B.
 * input: int_flags - output pointer for low 4-bit interrupt flag bitmap.
 * output: ESP_OK on success; otherwise parameter/state/SPI error.
 */
esp_err_t gpba02b_int_get_flags(uint8_t *int_flags)
{
    uint8_t reg_val;
    esp_err_t ret;

    if (!s_gpba02b_ready || (int_flags == NULL))
    {
        return ESP_ERR_INVALID_ARG;
    }

    ret = _read_reg_via_spi(GPBA02B_REG_INT_FLAG_ENABLE, &reg_val);
    if (ret != ESP_OK)
    {
        return ret;
    }

    *int_flags = (uint8_t)((reg_val >> 4) & GPBA02B_INT_MASK);
    return ESP_OK;
}

/*
 * brief: Clear specified interrupt flags while preserving enable bits.
 * input: int_flags - low 4-bit mask of flags to clear.
 * output: ESP_OK on success; otherwise state/SPI error.
 */
esp_err_t gpba02b_int_clear_flags(uint8_t int_flags)
{
    uint8_t reg_val;
    uint8_t write_val;
    esp_err_t ret;

    if (!s_gpba02b_ready)
    {
        return ESP_ERR_INVALID_STATE;
    }

    ret = _read_reg_via_spi(GPBA02B_REG_INT_FLAG_ENABLE, &reg_val);
    if (ret != ESP_OK)
    {
        return ret;
    }

    write_val = (uint8_t)((reg_val & GPBA02B_INT_MASK) | ((int_flags & GPBA02B_INT_MASK) << 4));
    return _write_reg_via_spi(GPBA02B_REG_INT_FLAG_ENABLE, write_val);
}

/*
 * brief: Register or clear the application callback for external interrupt handling.
 * input: cb - callback function pointer; user_ctx - user context passed to callback.
 * output: None.
 */
void gpba02b_int_set_callback(gpba02b_int_callback_t cb, void *user_ctx)
{
    s_int_callback = cb;
    s_int_callback_ctx = user_ctx;
}

/*
 * brief: Service one external interrupt event by fetch/clear flags and calling callback.
 * input: None.
 * output: ESP_OK on success; otherwise state/SPI error.
 */
esp_err_t gpba02b_int_handle_external_event(void)
{
    uint8_t int_flags;
    esp_err_t ret;

    if (!s_gpba02b_ready)
    {
        return ESP_ERR_INVALID_STATE;
    }

    ret = gpba02b_int_get_flags(&int_flags);
    if (ret != ESP_OK)
    {
        return ret;
    }

    if ((int_flags & GPBA02B_INT_MASK) == 0)
    {
        return ESP_OK;
    }

    ret = gpba02b_int_clear_flags(int_flags);
    if (ret != ESP_OK)
    {
        return ret;
    }

    if (s_int_callback != NULL)
    {
        s_int_callback((uint8_t)(int_flags & GPBA02B_INT_MASK), s_int_callback_ctx);
    }

    return ESP_OK;
}