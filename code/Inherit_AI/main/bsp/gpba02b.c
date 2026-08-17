#include "gpba02b.h"

#define TAG "GPBA02B"

typedef struct {
    spi_device_handle_t spi_device;
    spi_host_device_t spi_host;
    bool bus_owned;
    uint8_t device_id;
    bool new_function_enabled;
    uint8_t interrupt_enable_mask;
    uint8_t cmos_enable_mask;
    uint8_t od_nmos_mask[3];
    uint8_t od_pmos_mask[3];
    gpio_num_t host_irq_gpio;
    gpba02b_host_irq_callback_t host_irq_callback;
    void* host_irq_user_ctx;
} gpba02b_t;

typedef struct {
    uint8_t kRegBufBase;
    uint8_t kRegDirBase;
    uint8_t kRegAttBase;
    uint8_t kRegDataBase;

    uint8_t kRegIntFlagEnable;
    uint8_t kRegCurrentSink;
    uint8_t kRegPwmEnableA;
    uint8_t kRegPwmClock;
    uint8_t kRegPwmEnableC;
    uint8_t kRegIntEdgeCmos;

    uint8_t kPwmDutyRegA[8];
    uint8_t kPwmDutyRegC[8];

    uint8_t kOpWrite;
    uint8_t kOpAnd;
    uint8_t kOpOr;
} gpba02b_regs_t;

static const gpba02b_regs_t kDefaultRegs = {
    .kRegBufBase = 0x00,
    .kRegDirBase = 0x04,
    .kRegAttBase = 0x08,
    .kRegDataBase = 0x0C,

    .kRegIntFlagEnable = 0x0B,
    .kRegCurrentSink = 0x13,
    .kRegPwmEnableA = 0x17,
    .kRegPwmClock = 0x1B,
    .kRegPwmEnableC = 0x27,
    .kRegIntEdgeCmos = 0x23,

    .kPwmDutyRegA = {0x1C, 0x1D, 0x1E, 0x1F, 0x2B, 0x2C, 0x2D, 0x2E},
    .kPwmDutyRegC = {0x2F, 0x33, 0x37, 0x3B, 0x3C, 0x3D, 0x3E, 0x3F},

    .kOpWrite = 0x00,
    .kOpAnd = 0x10,
    .kOpOr = 0x20,
};

static gpba02b_t s_gpba02b = {
    .spi_device = NULL,
    .spi_host = SPI3_HOST,
    .bus_owned = false,
    .device_id = 0,
    .new_function_enabled = false,
    .interrupt_enable_mask = 0,
    .cmos_enable_mask = 0,
    .od_nmos_mask = {0, 0, 0},
    .od_pmos_mask = {0, 0, 0},
    .host_irq_gpio = GPIO_NUM_NC,
    .host_irq_callback = NULL,
    .host_irq_user_ctx = NULL,
};

static esp_err_t gpba02b_host_irq_install_with_dev(gpba02b_t* dev,
                                                   const gpba02b_host_irq_gpio_config_t* config,
                                                   gpba02b_host_irq_callback_t callback,
                                                   void* user_ctx);
static esp_err_t gpba02b_host_irq_uninstall_with_dev(gpba02b_t* dev);

/* ==================== private functions (static) ==================== */

/*
 * brief  : Validate whether the port enum is supported by GPBA02B.
 * input  : port - Candidate IO port value.
 * output : true if the port is valid; otherwise false.
 * type   : private
 */
static bool gpba02b_is_port_valid(gpba02b_port_t port) { return port <= GPBA02B_PORT_C; }

/*
 * brief  : Check whether a port supports PWM channels.
 * input  : port - Candidate IO port value.
 * output : true for PWM-capable ports; otherwise false.
 * type   : private
 */
static bool gpba02b_is_pwm_port(gpba02b_port_t port) {
    return port == GPBA02B_PORT_A || port == GPBA02B_PORT_C;
}

/*
 * brief  : Ensure the singleton device has been initialized.
 * input  : dev - Device context pointer.
 * output : ESP_OK if initialized; error code otherwise.
 * type   : private
 */
static esp_err_t gpba02b_ensure_initialized(gpba02b_t* dev) {
    if (dev == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (dev->spi_device == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    return ESP_OK;
}

/*
 * brief  : Build a write command byte for one register access.
 * input  : dev - Device context pointer; reg_addr - 6-bit register address.
 * output : Encoded SPI command byte for write operation.
 * type   : private
 */
static uint8_t gpba02b_make_write_command(const gpba02b_t* dev, uint8_t reg_addr) {
    return (uint8_t)(0x80 | ((dev->device_id & 0x01) << 6) | (reg_addr & 0x3F));
}

/*
 * brief  : Build a read command byte for one register access.
 * input  : dev - Device context pointer; reg_addr - 6-bit register address.
 * output : Encoded SPI command byte for read operation.
 * type   : private
 */
static uint8_t gpba02b_make_read_command(const gpba02b_t* dev, uint8_t reg_addr) {
    return (uint8_t)(((dev->device_id & 0x01) << 6) | (reg_addr & 0x3F));
}

/*
 * brief  : Transfer one 2-byte command/data frame over SPI.
 * input  : dev - Device context; command - SPI command byte; write_data - data to send;
 *          read_data - optional output pointer for received byte.
 * output : ESP_OK on success; SPI error code on failure.
 * type   : private
 */
static esp_err_t gpba02b_transfer_frame(gpba02b_t* dev, uint8_t command, uint8_t write_data,
                                        uint8_t* read_data) {
    uint8_t tx_buffer[2] = {command, write_data};
    uint8_t rx_buffer[2] = {0, 0};

    spi_transaction_t transaction = {0};
    transaction.length = 16;
    transaction.tx_buffer = tx_buffer;
    transaction.rx_buffer = rx_buffer;

    esp_err_t err = spi_device_transmit(dev->spi_device, &transaction);
    if (err != ESP_OK) {
        return err;
    }

    if (read_data != NULL) {
        *read_data = rx_buffer[1];
    }
    return ESP_OK;
}

/*
 * brief  : Map abstract IO register kind to GPBA02B register base address.
 * input  : reg - Logical IO register group.
 * output : Register base address or 0xFF if unsupported.
 * type   : private
 */
static uint8_t gpba02b_io_register_base(gpba02b_io_reg_t reg) {
    switch (reg) {
        case GPBA02B_IO_REG_BUFFER:
            return kDefaultRegs.kRegBufBase;
        case GPBA02B_IO_REG_DIRECTION:
            return kDefaultRegs.kRegDirBase;
        case GPBA02B_IO_REG_ATTRIBUTE:
            return kDefaultRegs.kRegAttBase;
        default:
            return 0xFF;
    }
}

/*
 * brief  : ISR trampoline that forwards host GPIO interrupt callback.
 * input  : arg - Device context passed when installing ISR.
 * output : None.
 * type   : private
 */
static void IRAM_ATTR gpba02b_host_irq_handler(void* arg) {
    gpba02b_t* dev = (gpba02b_t*)arg;
    if (dev != NULL && dev->host_irq_callback != NULL) {
        dev->host_irq_callback(dev->host_irq_user_ctx);
    }
}

/*
 * brief  : Write one GPBA02B register.
 * input  : dev - Device context; reg_addr - register address; value - data byte.
 * output : ESP_OK on success; error code otherwise.
 * type   : private
 */
static esp_err_t gpba02b_write_reg(gpba02b_t* dev, uint8_t reg_addr, uint8_t value) {
    esp_err_t err = gpba02b_ensure_initialized(dev);
    if (err != ESP_OK) {
        return err;
    }

    return gpba02b_transfer_frame(dev, gpba02b_make_write_command(dev, reg_addr), value, NULL);
}

/*
 * brief  : Read one GPBA02B register.
 * input  : dev - Device context; reg_addr - register address; value - output byte pointer.
 * output : ESP_OK on success; error code otherwise.
 * type   : private
 */
static esp_err_t gpba02b_read_reg(gpba02b_t* dev, uint8_t reg_addr, uint8_t* value) {
    if (value == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = gpba02b_ensure_initialized(dev);
    if (err != ESP_OK) {
        return err;
    }

    return gpba02b_transfer_frame(dev, gpba02b_make_read_command(dev, reg_addr), 0x00, value);
}

/*
 * brief  : Apply bitwise IO operation (write/and/or) on one IO register bank.
 * input  : dev - Device context; port - Port id; reg - IO register group;
 *          operation - operation opcode offset; value - operation mask.
 * output : ESP_OK on success; error code otherwise.
 * type   : private
 */
static esp_err_t gpba02b_io_update(gpba02b_t* dev, gpba02b_port_t port, gpba02b_io_reg_t reg,
                                   uint8_t operation, uint8_t value) {
    if (!gpba02b_is_port_valid(port)) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t base = gpba02b_io_register_base(reg);
    if (base == 0xFF) {
        return ESP_ERR_INVALID_ARG;
    }

    return gpba02b_write_reg(dev, (uint8_t)(base + operation + port), value);
}

/*
 * brief  : Clear selected bits in one IO register group.
 * input  : dev - Device context; port - Port id; reg - IO register group; value - clear mask.
 * output : ESP_OK on success; error code otherwise.
 * type   : private
 */
static esp_err_t gpba02b_io_and(gpba02b_t* dev, gpba02b_port_t port, gpba02b_io_reg_t reg,
                                uint8_t value) {
    return gpba02b_io_update(dev, port, reg, kDefaultRegs.kOpAnd, value);
}

/*
 * brief  : Set selected bits in one IO register group.
 * input  : dev - Device context; port - Port id; reg - IO register group; value - set mask.
 * output : ESP_OK on success; error code otherwise.
 * type   : private
 */
static esp_err_t gpba02b_io_or(gpba02b_t* dev, gpba02b_port_t port, gpba02b_io_reg_t reg,
                               uint8_t value) {
    return gpba02b_io_update(dev, port, reg, kDefaultRegs.kOpOr, value);
}

/*
 * brief  : Set or clear one bit in a selected IO register group.
 * input  : dev - Device context; port - Port id; reg - IO register group;
 *          pin - Pin index 0..7; bit_value - target bit value.
 * output : ESP_OK on success; error code otherwise.
 * type   : private
 */
static esp_err_t gpba02b_config_io_reg_bit(gpba02b_t* dev, gpba02b_port_t port,
                                           gpba02b_io_reg_t reg, uint8_t pin, bool bit_value) {
    if (pin > 7) {
        return ESP_ERR_INVALID_ARG;
    }

    const uint8_t bit_mask = (uint8_t)(1U << pin);
    if (bit_value) {
        return gpba02b_io_or(dev, port, reg, bit_mask);
    }

    return gpba02b_io_and(dev, port, reg, (uint8_t)(~bit_mask));
}

/* ==================== public functions ==================== */

/*
 * brief  : Fill a GPBA02B config structure with default values.
 * input  : config - Output configuration structure pointer.
 * output : None.
 * type   : public
 */
void gpba02b_get_default_config(gpba02b_config_t* config) {
    if (config == NULL) {
        return;
    }

    config->spi_host = SPI3_HOST;
    config->miso_io = GPIO_NUM_NC;
    config->mosi_io = GPIO_NUM_NC;
    config->sclk_io = GPIO_NUM_NC;
    config->cs_io = GPIO_NUM_NC;
    config->clock_hz = 8 * 1000 * 1000;
    config->device_id = 0;
    config->queue_size = 4;
}

/*
 * brief  : Initialize GPBA02B singleton and attach it to SPI bus.
 * input  : config - Initialization parameters.
 * output : ESP_OK on success; error code otherwise.
 * type   : public
 */
esp_err_t gpba02b_init(const gpba02b_config_t* config) {
    gpba02b_t* dev = &s_gpba02b;

    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (config->mosi_io == GPIO_NUM_NC || config->sclk_io == GPIO_NUM_NC ||
        config->cs_io == GPIO_NUM_NC) {
        return ESP_ERR_INVALID_ARG;
    }
    if (config->clock_hz <= 0) {
        return ESP_ERR_INVALID_ARG;
    }

    if (dev->spi_device != NULL) {
        gpba02b_deinit();
    }

    spi_bus_config_t bus_config = {0};
    bus_config.miso_io_num = config->miso_io;
    bus_config.mosi_io_num = config->mosi_io;
    bus_config.sclk_io_num = config->sclk_io;
    bus_config.quadwp_io_num = GPIO_NUM_NC;
    bus_config.quadhd_io_num = GPIO_NUM_NC;

    esp_err_t err = spi_bus_initialize(config->spi_host, &bus_config, SPI_DMA_CH_AUTO);
    if (err == ESP_OK) {
        dev->bus_owned = true;
    } else if (err == ESP_ERR_INVALID_STATE) {
        dev->bus_owned = false;
        err = ESP_OK;
    }
    if (err != ESP_OK) {
        return err;
    }

    spi_device_interface_config_t device_config = {0};
    device_config.mode = 0;
    device_config.clock_speed_hz = config->clock_hz;
    device_config.spics_io_num = config->cs_io;
    device_config.queue_size = config->queue_size > 0 ? config->queue_size : 4;

    err = spi_bus_add_device(config->spi_host, &device_config, &dev->spi_device);
    if (err != ESP_OK) {
        if (dev->bus_owned) {
            spi_bus_free(config->spi_host);
            dev->bus_owned = false;
        }
        return err;
    }

    dev->spi_host = config->spi_host;
    dev->device_id = config->device_id & 0x01;
    dev->new_function_enabled = false;
    dev->interrupt_enable_mask = 0;
    dev->cmos_enable_mask = 0;
    dev->od_nmos_mask[0] = 0;
    dev->od_nmos_mask[1] = 0;
    dev->od_nmos_mask[2] = 0;
    dev->od_pmos_mask[0] = 0;
    dev->od_pmos_mask[1] = 0;
    dev->od_pmos_mask[2] = 0;

    ESP_LOGI(TAG, "GPBA02B initialized on SPI host %d, device_id=%u", (int)config->spi_host,
             (unsigned)dev->device_id);
    return ESP_OK;
}

/*
 * brief  : Deinitialize GPBA02B singleton and release SPI resources.
 * input  : None.
 * output : None.
 * type   : public
 */
void gpba02b_deinit(void) {
    gpba02b_t* dev = &s_gpba02b;

    gpba02b_host_irq_uninstall_with_dev(dev);

    if (dev->spi_device != NULL) {
        spi_bus_remove_device(dev->spi_device);
        dev->spi_device = NULL;
    }

    if (dev->bus_owned) {
        spi_bus_free(dev->spi_host);
        dev->bus_owned = false;
    }

    dev->new_function_enabled = false;
    dev->interrupt_enable_mask = 0;
    dev->cmos_enable_mask = 0;
    dev->od_nmos_mask[0] = 0;
    dev->od_nmos_mask[1] = 0;
    dev->od_nmos_mask[2] = 0;
    dev->od_pmos_mask[0] = 0;
    dev->od_pmos_mask[1] = 0;
    dev->od_pmos_mask[2] = 0;
}

/*
 * brief  : Write logic level to a single IO pin.
 * input  : port - Port id; pin - Pin index 0..7; level - Output level.
 * output : ESP_OK on success; error code otherwise.
 * type   : public
 */
esp_err_t gpba02b_write_io(gpba02b_port_t port, uint8_t pin, bool level) {
    gpba02b_t* dev = &s_gpba02b;

    if (pin > 7 || !gpba02b_is_port_valid(port)) {
        return ESP_ERR_INVALID_ARG;
    }

    const uint8_t bit_mask = (uint8_t)(1U << pin);
    int port_index = (int)port;

    if ((dev->od_nmos_mask[port_index] & bit_mask) != 0) {
        return gpba02b_config_io_reg_bit(dev, port, GPBA02B_IO_REG_DIRECTION, pin, !level);
    }
    if ((dev->od_pmos_mask[port_index] & bit_mask) != 0) {
        return gpba02b_config_io_reg_bit(dev, port, GPBA02B_IO_REG_DIRECTION, pin, level);
    }

    return gpba02b_config_io_reg_bit(dev, port, GPBA02B_IO_REG_BUFFER, pin, level);
}

/*
 * brief  : Read logic level from a single IO pin.
 * input  : port - Port id; pin - Pin index 0..7; level - Output level pointer.
 * output : ESP_OK on success; error code otherwise.
 * type   : public
 */
esp_err_t gpba02b_read_io(gpba02b_port_t port, uint8_t pin, bool* level) {
    if (level == NULL || pin > 7 || !gpba02b_is_port_valid(port)) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t port_value = 0;
    esp_err_t err = gpba02b_read_port_input(port, &port_value);
    if (err != ESP_OK) {
        return err;
    }

    *level = (port_value & (uint8_t)(1U << pin)) != 0;
    return ESP_OK;
}

/*
 * brief  : Configure one IO pin as input with selected pull mode.
 * input  : port - Port id; pin - Pin index 0..7; mode - Input mode enum.
 * output : ESP_OK on success; error code otherwise.
 * type   : public
 */
esp_err_t gpba02b_config_io_input_mode(gpba02b_port_t port, uint8_t pin,
                                       gpba02b_io_input_mode_t mode) {
    gpba02b_t* dev = &s_gpba02b;

    if (pin > 7 || !gpba02b_is_port_valid(port)) {
        return ESP_ERR_INVALID_ARG;
    }

    if (mode != GPBA02B_IO_INPUT_FLOATING && mode != GPBA02B_IO_INPUT_PULL_LOW &&
        mode != GPBA02B_IO_INPUT_PULL_HIGH) {
        return ESP_ERR_INVALID_ARG;
    }

    const uint8_t bit_mask = (uint8_t)(1U << pin);
    int port_index = (int)port;
    dev->od_nmos_mask[port_index] &= (uint8_t)(~bit_mask);
    dev->od_pmos_mask[port_index] &= (uint8_t)(~bit_mask);

    esp_err_t err = gpba02b_config_io_reg_bit(dev, port, GPBA02B_IO_REG_DIRECTION, pin, false);
    if (err != ESP_OK) {
        return err;
    }

    switch (mode) {
        case GPBA02B_IO_INPUT_FLOATING:
            err = gpba02b_config_io_reg_bit(dev, port, GPBA02B_IO_REG_BUFFER, pin, false);
            if (err != ESP_OK) {
                return err;
            }
            return gpba02b_config_io_reg_bit(dev, port, GPBA02B_IO_REG_ATTRIBUTE, pin, false);
        case GPBA02B_IO_INPUT_PULL_LOW:
            err = gpba02b_config_io_reg_bit(dev, port, GPBA02B_IO_REG_ATTRIBUTE, pin, false);
            if (err != ESP_OK) {
                return err;
            }
            return gpba02b_config_io_reg_bit(dev, port, GPBA02B_IO_REG_BUFFER, pin, true);
        case GPBA02B_IO_INPUT_PULL_HIGH:
            err = gpba02b_config_io_reg_bit(dev, port, GPBA02B_IO_REG_ATTRIBUTE, pin, true);
            if (err != ESP_OK) {
                return err;
            }
            return gpba02b_config_io_reg_bit(dev, port, GPBA02B_IO_REG_BUFFER, pin, true);
        default:
            return ESP_ERR_INVALID_ARG;
    }
}

/*
 * brief  : Configure one IO pin as output with selected output mode.
 * input  : port - Port id; pin - Pin index 0..7; mode - Output mode enum;
 *          level - Initial output level.
 * output : ESP_OK on success; error code otherwise.
 * type   : public
 */
esp_err_t gpba02b_config_io_output_mode(gpba02b_port_t port, uint8_t pin,
                                        gpba02b_io_output_mode_t mode, bool level) {
    gpba02b_t* dev = &s_gpba02b;

    if (pin > 7 || !gpba02b_is_port_valid(port)) {
        return ESP_ERR_INVALID_ARG;
    }

    if (mode != GPBA02B_IO_OUTPUT_PUSH_PULL && mode != GPBA02B_IO_OUTPUT_OPEN_DRAIN_NMOS &&
        mode != GPBA02B_IO_OUTPUT_OPEN_DRAIN_PMOS) {
        return ESP_ERR_INVALID_ARG;
    }

    const uint8_t bit_mask = (uint8_t)(1U << pin);
    int port_index = (int)port;
    dev->od_nmos_mask[port_index] &= (uint8_t)(~bit_mask);
    dev->od_pmos_mask[port_index] &= (uint8_t)(~bit_mask);

    if (mode == GPBA02B_IO_OUTPUT_OPEN_DRAIN_NMOS) {
        dev->od_nmos_mask[port_index] |= bit_mask;
    } else if (mode == GPBA02B_IO_OUTPUT_OPEN_DRAIN_PMOS) {
        dev->od_pmos_mask[port_index] |= bit_mask;
    }

    esp_err_t err = ESP_OK;

    switch (mode) {
        case GPBA02B_IO_OUTPUT_PUSH_PULL:
            err = gpba02b_config_io_reg_bit(dev, port, GPBA02B_IO_REG_ATTRIBUTE, pin, false);
            if (err != ESP_OK) {
                return err;
            }
            err = gpba02b_config_io_reg_bit(dev, port, GPBA02B_IO_REG_BUFFER, pin, level);
            if (err != ESP_OK) {
                return err;
            }
            return gpba02b_config_io_reg_bit(dev, port, GPBA02B_IO_REG_DIRECTION, pin, true);
        case GPBA02B_IO_OUTPUT_OPEN_DRAIN_NMOS:
            err = gpba02b_config_io_reg_bit(dev, port, GPBA02B_IO_REG_ATTRIBUTE, pin, false);
            if (err != ESP_OK) {
                return err;
            }
            err = gpba02b_config_io_reg_bit(dev, port, GPBA02B_IO_REG_BUFFER, pin, false);
            if (err != ESP_OK) {
                return err;
            }
            return gpba02b_config_io_reg_bit(dev, port, GPBA02B_IO_REG_DIRECTION, pin, !level);
        case GPBA02B_IO_OUTPUT_OPEN_DRAIN_PMOS:
            err = gpba02b_config_io_reg_bit(dev, port, GPBA02B_IO_REG_ATTRIBUTE, pin, true);
            if (err != ESP_OK) {
                return err;
            }
            err = gpba02b_config_io_reg_bit(dev, port, GPBA02B_IO_REG_BUFFER, pin, false);
            if (err != ESP_OK) {
                return err;
            }
            return gpba02b_config_io_reg_bit(dev, port, GPBA02B_IO_REG_DIRECTION, pin, level);
        default:
            return ESP_ERR_INVALID_ARG;
    }
}

/*
 * brief  : Configure one IO pin as input using compatibility API.
 * input  : port - Port id; pin - Pin index 0..7; pull_up - true for pull-high.
 * output : ESP_OK on success; error code otherwise.
 * type   : public
 */
esp_err_t gpba02b_config_io_input(gpba02b_port_t port, uint8_t pin, bool pull_up) {
    return gpba02b_config_io_input_mode(
        port, pin, pull_up ? GPBA02B_IO_INPUT_PULL_HIGH : GPBA02B_IO_INPUT_PULL_LOW);
}

/*
 * brief  : Configure one IO pin as output using compatibility API.
 * input  : port - Port id; pin - Pin index 0..7; open_collector - compatibility flag;
 *          level - Initial output level.
 * output : ESP_OK on success; error code otherwise.
 * type   : public
 */
esp_err_t gpba02b_config_io_output(gpba02b_port_t port, uint8_t pin, bool open_collector,
                                   bool level) {
    return gpba02b_config_io_output_mode(
        port, pin, open_collector ? GPBA02B_IO_OUTPUT_OPEN_DRAIN_NMOS : GPBA02B_IO_OUTPUT_PUSH_PULL,
        level);
}

/*
 * brief  : Read full input register byte of one port.
 * input  : port - Port id; value - Output byte pointer.
 * output : ESP_OK on success; error code otherwise.
 * type   : public
 */
esp_err_t gpba02b_read_port_input(gpba02b_port_t port, uint8_t* value) {
    gpba02b_t* dev = &s_gpba02b;

    if (value == NULL || !gpba02b_is_port_valid(port)) {
        return ESP_ERR_INVALID_ARG;
    }

    return gpba02b_read_reg(dev, (uint8_t)(kDefaultRegs.kRegDataBase + port), value);
}

/*
 * brief  : Unlock GPBA02B extended function registers.
 * input  : None.
 * output : ESP_OK on success; error code otherwise.
 * type   : public
 */
esp_err_t gpba02b_enable_new_functions(void) {
    gpba02b_t* dev = &s_gpba02b;

    if (dev->new_function_enabled) {
        return ESP_OK;
    }

    esp_err_t err = gpba02b_ensure_initialized(dev);
    if (err != ESP_OK) {
        return err;
    }

    err = gpba02b_write_reg(dev, 0x03, 0x55);
    if (err != ESP_OK) {
        return err;
    }
    err = gpba02b_write_reg(dev, 0x07, 0xAA);
    if (err != ESP_OK) {
        return err;
    }
    err = gpba02b_write_reg(dev, 0x03, 0x55);
    if (err != ESP_OK) {
        return err;
    }
    err = gpba02b_write_reg(dev, 0x07, 0xAA);
    if (err != ESP_OK) {
        return err;
    }

    dev->new_function_enabled = true;
    return ESP_OK;
}

/*
 * brief  : Enable or disable software reset function in chip register.
 * input  : disabled - true to disable software reset.
 * output : ESP_OK on success; error code otherwise.
 * type   : public
 */
esp_err_t gpba02b_set_software_reset_disabled(bool disabled) {
    gpba02b_t* dev = &s_gpba02b;

    esp_err_t err = gpba02b_enable_new_functions();
    if (err != ESP_OK) {
        return err;
    }

    uint8_t value = 0;
    err = gpba02b_read_reg(dev, kDefaultRegs.kRegCurrentSink, &value);
    if (err != ESP_OK) {
        return err;
    }

    if (disabled) {
        value |= 0x80;
    } else {
        value &= (uint8_t)(~0x80);
    }

    return gpba02b_write_reg(dev, kDefaultRegs.kRegCurrentSink, value);
}

/*
 * brief  : Configure PWM clock divider for port A and port C domains.
 * input  : pa_div - Divider 0..7 for port A; pc_div - Divider 0..7 for port C.
 * output : ESP_OK on success; error code otherwise.
 * type   : public
 */
esp_err_t gpba02b_pwm_set_clock_div(uint8_t pa_div, uint8_t pc_div) {
    gpba02b_t* dev = &s_gpba02b;

    if (pa_div > 0x07 || pc_div > 0x07) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = gpba02b_enable_new_functions();
    if (err != ESP_OK) {
        return err;
    }

    uint8_t value = (uint8_t)(((pa_div & 0x07) << 4) | (pc_div & 0x07));
    return gpba02b_write_reg(dev, kDefaultRegs.kRegPwmClock, value);
}

/*
 * brief  : Enable PWM channels on a PWM-capable port.
 * input  : port - PWM port id; channel_mask - 8-bit enable mask.
 * output : ESP_OK on success; error code otherwise.
 * type   : public
 */
esp_err_t gpba02b_pwm_enable_channels(gpba02b_port_t port, uint8_t channel_mask) {
    gpba02b_t* dev = &s_gpba02b;

    if (!gpba02b_is_pwm_port(port)) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = gpba02b_enable_new_functions();
    if (err != ESP_OK) {
        return err;
    }

    const uint8_t reg =
        (port == GPBA02B_PORT_A) ? kDefaultRegs.kRegPwmEnableA : kDefaultRegs.kRegPwmEnableC;
    return gpba02b_write_reg(dev, reg, channel_mask);
}

/*
 * brief  : Set PWM duty for one channel.
 * input  : port - PWM port id; channel - Channel index 0..7; duty - Duty byte.
 * output : ESP_OK on success; error code otherwise.
 * type   : public
 */
esp_err_t gpba02b_pwm_set_channel_duty(gpba02b_port_t port, uint8_t channel, uint8_t duty) {
    gpba02b_t* dev = &s_gpba02b;

    if (!gpba02b_is_pwm_port(port) || channel > 7) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = gpba02b_enable_new_functions();
    if (err != ESP_OK) {
        return err;
    }

    const uint8_t reg = (port == GPBA02B_PORT_A) ? kDefaultRegs.kPwmDutyRegA[channel]
                                                 : kDefaultRegs.kPwmDutyRegC[channel];
    return gpba02b_write_reg(dev, reg, duty);
}

/*
 * brief  : Configure current sink mode and level for one PWM port.
 * input  : port - PWM port id; enable - Enable sink mode; current_level - 2-bit level.
 * output : ESP_OK on success; error code otherwise.
 * type   : public
 */
esp_err_t gpba02b_current_sink_set(gpba02b_port_t port, bool enable, uint8_t current_level) {
    gpba02b_t* dev = &s_gpba02b;

    if (!gpba02b_is_pwm_port(port) || current_level > 0x03) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = gpba02b_enable_new_functions();
    if (err != ESP_OK) {
        return err;
    }

    uint8_t value = 0;
    err = gpba02b_read_reg(dev, kDefaultRegs.kRegCurrentSink, &value);
    if (err != ESP_OK) {
        return err;
    }

    if (port == GPBA02B_PORT_A) {
        if (enable) {
            value |= 0x04;
        } else {
            value &= (uint8_t)(~0x04);
        }
        value = (uint8_t)((value & ~0x03) | (current_level & 0x03));
    } else {
        if (enable) {
            value |= 0x40;
        } else {
            value &= (uint8_t)(~0x40);
        }
        value = (uint8_t)((value & ~0x30) | ((current_level & 0x03) << 4));
    }

    return gpba02b_write_reg(dev, kDefaultRegs.kRegCurrentSink, value);
}

/*
 * brief  : Helper to set current sink then enable PWM channels safely.
 * input  : port - PWM port id; channel_mask - Channels to enable; current_level - Sink level.
 * output : ESP_OK on success; error code otherwise.
 * type   : public
 */
esp_err_t gpba02b_pwm_with_current_sink_setup(gpba02b_port_t port, uint8_t channel_mask,
                                              uint8_t current_level) {
    esp_err_t err = gpba02b_current_sink_set(port, true, current_level);
    if (err != ESP_OK) {
        return err;
    }

    // Datasheet requires a short delay before enabling PWM channels after sink setup.
    esp_rom_delay_us(200);

    return gpba02b_pwm_enable_channels(port, channel_mask);
}

/*
 * brief  : Configure interrupt enable bits and edge polarity bits.
 * input  : enable_mask - Interrupt enable mask; falling_edge_mask - Edge selection mask.
 * output : ESP_OK on success; error code otherwise.
 * type   : public
 */
esp_err_t gpba02b_interrupt_configure(uint8_t enable_mask, uint8_t falling_edge_mask) {
    gpba02b_t* dev = &s_gpba02b;

    enable_mask &= 0x0F;
    falling_edge_mask &= 0x0F;

    esp_err_t err = gpba02b_enable_new_functions();
    if (err != ESP_OK) {
        return err;
    }

    const uint8_t int_edge_cmos =
        (uint8_t)((falling_edge_mask << 4) | (dev->cmos_enable_mask & 0x0F));
    err = gpba02b_write_reg(dev, kDefaultRegs.kRegIntEdgeCmos, int_edge_cmos);
    if (err != ESP_OK) {
        return err;
    }

    dev->interrupt_enable_mask = enable_mask;
    return gpba02b_write_reg(dev, kDefaultRegs.kRegIntFlagEnable, dev->interrupt_enable_mask);
}

/*
 * brief  : Read interrupt flags and current enable mask.
 * input  : flags - Optional output flags pointer; enable_mask - Optional output enable pointer.
 * output : ESP_OK on success; error code otherwise.
 * type   : public
 */
esp_err_t gpba02b_interrupt_read(uint8_t* flags, uint8_t* enable_mask) {
    gpba02b_t* dev = &s_gpba02b;

    uint8_t value = 0;
    esp_err_t err = gpba02b_read_reg(dev, kDefaultRegs.kRegIntFlagEnable, &value);
    if (err != ESP_OK) {
        return err;
    }

    if (flags != NULL) {
        *flags = (uint8_t)((value >> 4) & 0x0F);
    }
    if (enable_mask != NULL) {
        *enable_mask = (uint8_t)(value & 0x0F);
    }

    dev->interrupt_enable_mask = (uint8_t)(value & 0x0F);
    return ESP_OK;
}

/*
 * brief  : Clear selected interrupt flags while preserving enable bits.
 * input  : flags_mask - Flags to clear (low 4 bits used).
 * output : ESP_OK on success; error code otherwise.
 * type   : public
 */
esp_err_t gpba02b_interrupt_clear(uint8_t flags_mask) {
    gpba02b_t* dev = &s_gpba02b;

    flags_mask &= 0x0F;
    uint8_t value = (uint8_t)((flags_mask << 4) | (dev->interrupt_enable_mask & 0x0F));
    return gpba02b_write_reg(dev, kDefaultRegs.kRegIntFlagEnable, value);
}

/*
 * brief  : Install host GPIO ISR and bind callback to device context.
 * input  : dev - Device context; config - Host IRQ GPIO config;
 *          callback - IRQ callback; user_ctx - Callback context.
 * output : ESP_OK on success; error code otherwise.
 * type   : private
 */
static esp_err_t gpba02b_host_irq_install_with_dev(gpba02b_t* dev,
                                                   const gpba02b_host_irq_gpio_config_t* config,
                                                   gpba02b_host_irq_callback_t callback,
                                                   void* user_ctx) {
    if (dev == NULL || config == NULL || config->gpio_num == GPIO_NUM_NC) {
        return ESP_ERR_INVALID_ARG;
    }

    gpio_config_t gpio_config_data = {0};
    gpio_config_data.pin_bit_mask = 1ULL << config->gpio_num;
    gpio_config_data.mode = GPIO_MODE_INPUT;
    gpio_config_data.pull_up_en = config->pull_up ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE;
    gpio_config_data.pull_down_en =
        config->pull_down ? GPIO_PULLDOWN_ENABLE : GPIO_PULLDOWN_DISABLE;
    gpio_config_data.intr_type = config->intr_type;

    esp_err_t err = gpio_config(&gpio_config_data);
    if (err != ESP_OK) {
        return err;
    }

    err = gpio_install_isr_service(ESP_INTR_FLAG_IRAM);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }

    if (dev->host_irq_gpio != GPIO_NUM_NC) {
        gpio_isr_handler_remove(dev->host_irq_gpio);
    }

    dev->host_irq_gpio = config->gpio_num;
    dev->host_irq_callback = callback;
    dev->host_irq_user_ctx = user_ctx;

    return gpio_isr_handler_add(dev->host_irq_gpio, gpba02b_host_irq_handler, dev);
}

/*
 * brief  : Remove host GPIO ISR binding from device context.
 * input  : dev - Device context pointer.
 * output : ESP_OK on success; error code otherwise.
 * type   : private
 */
static esp_err_t gpba02b_host_irq_uninstall_with_dev(gpba02b_t* dev) {
    if (dev == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (dev->host_irq_gpio != GPIO_NUM_NC) {
        gpio_isr_handler_remove(dev->host_irq_gpio);
        dev->host_irq_gpio = GPIO_NUM_NC;
    }

    dev->host_irq_callback = NULL;
    dev->host_irq_user_ctx = NULL;
    return ESP_OK;
}

/*
 * brief  : Public wrapper to install host IRQ callback on singleton device.
 * input  : config - Host IRQ GPIO config; callback - IRQ callback; user_ctx - callback context.
 * output : ESP_OK on success; error code otherwise.
 * type   : public
 */
esp_err_t gpba02b_host_irq_install(const gpba02b_host_irq_gpio_config_t* config,
                                   gpba02b_host_irq_callback_t callback, void* user_ctx) {
    return gpba02b_host_irq_install_with_dev(&s_gpba02b, config, callback, user_ctx);
}

/*
 * brief  : Public wrapper to uninstall host IRQ callback on singleton device.
 * input  : None.
 * output : ESP_OK on success; error code otherwise.
 * type   : public
 */
esp_err_t gpba02b_host_irq_uninstall(void) {
    return gpba02b_host_irq_uninstall_with_dev(&s_gpba02b);
}
