#include "bsp/gpba02b.h"

#include <driver/gpio.h>
#include <esp_log.h>
#include <esp_rom_sys.h>

#define TAG "GPBA02B"

struct gpba02b {
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
};

static const uint8_t kRegBufBase = 0x00;
static const uint8_t kRegDirBase = 0x04;
static const uint8_t kRegAttBase = 0x08;
static const uint8_t kRegDataBase = 0x0C;

static const uint8_t kRegIntFlagEnable = 0x0B;
static const uint8_t kRegCurrentSink = 0x13;
static const uint8_t kRegPwmEnableA = 0x17;
static const uint8_t kRegPwmClock = 0x1B;
static const uint8_t kRegPwmEnableC = 0x27;
static const uint8_t kRegIntEdgeCmos = 0x23;

static const uint8_t kPwmDutyRegA[8] = {0x1C, 0x1D, 0x1E, 0x1F, 0x2B, 0x2C, 0x2D, 0x2E};
static const uint8_t kPwmDutyRegC[8] = {0x2F, 0x33, 0x37, 0x3B, 0x3C, 0x3D, 0x3E, 0x3F};

static const uint8_t kOpWrite = 0x00;
static const uint8_t kOpAnd = 0x10;
static const uint8_t kOpOr = 0x20;

static bool gpba02b_is_port_valid(gpba02b_port_t port) {
    return port <= GPBA02B_PORT_C;
}

static bool gpba02b_is_pwm_port(gpba02b_port_t port) {
    return port == GPBA02B_PORT_A || port == GPBA02B_PORT_C;
}

static esp_err_t gpba02b_ensure_initialized(gpba02b_t* dev) {
    if (dev == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (dev->spi_device == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    return ESP_OK;
}

static uint8_t gpba02b_make_write_command(const gpba02b_t* dev, uint8_t reg_addr) {
    return (uint8_t)(0x80 | ((dev->device_id & 0x01) << 6) | (reg_addr & 0x3F));
}

static uint8_t gpba02b_make_read_command(const gpba02b_t* dev, uint8_t reg_addr) {
    return (uint8_t)(((dev->device_id & 0x01) << 6) | (reg_addr & 0x3F));
}

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

static uint8_t gpba02b_io_register_base(gpba02b_io_reg_t reg) {
    switch (reg) {
        case GPBA02B_IO_REG_BUFFER:
            return kRegBufBase;
        case GPBA02B_IO_REG_DIRECTION:
            return kRegDirBase;
        case GPBA02B_IO_REG_ATTRIBUTE:
            return kRegAttBase;
        default:
            return 0xFF;
    }
}

static void IRAM_ATTR gpba02b_host_irq_handler(void* arg) {
    gpba02b_t* dev = (gpba02b_t*)arg;
    if (dev != NULL && dev->host_irq_callback != NULL) {
        dev->host_irq_callback(dev->host_irq_user_ctx);
    }
}

static esp_err_t gpba02b_write_reg(gpba02b_t* dev, uint8_t reg_addr, uint8_t value) {
    esp_err_t err = gpba02b_ensure_initialized(dev);
    if (err != ESP_OK) {
        return err;
    }

    return gpba02b_transfer_frame(dev, gpba02b_make_write_command(dev, reg_addr), value, NULL);
}

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

static esp_err_t gpba02b_io_and(gpba02b_t* dev, gpba02b_port_t port, gpba02b_io_reg_t reg,
                                uint8_t value) {
    return gpba02b_io_update(dev, port, reg, kOpAnd, value);
}

static esp_err_t gpba02b_io_or(gpba02b_t* dev, gpba02b_port_t port, gpba02b_io_reg_t reg,
                               uint8_t value) {
    return gpba02b_io_update(dev, port, reg, kOpOr, value);
}

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

gpba02b_t* gpba02b_instance(void) {
    static gpba02b_t instance = {
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
    return &instance;
}

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

esp_err_t gpba02b_init(gpba02b_t* dev, const gpba02b_config_t* config) {
    if (dev == NULL || config == NULL) {
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
        gpba02b_deinit(dev);
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

void gpba02b_deinit(gpba02b_t* dev) {
    if (dev == NULL) {
        return;
    }

    gpba02b_host_irq_uninstall(dev);

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

esp_err_t gpba02b_io_write(gpba02b_t* dev, gpba02b_port_t port, gpba02b_io_reg_t reg,
                           uint8_t value) {
    return gpba02b_io_update(dev, port, reg, kOpWrite, value);
}

esp_err_t gpba02b_io_read(gpba02b_t* dev, gpba02b_port_t port, gpba02b_io_reg_t reg,
                          uint8_t* value) {
    if (value == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!gpba02b_is_port_valid(port)) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t base = gpba02b_io_register_base(reg);
    if (base == 0xFF) {
        return ESP_ERR_INVALID_ARG;
    }

    return gpba02b_read_reg(dev, (uint8_t)(base + port), value);
}

esp_err_t gpba02b_write_io(gpba02b_t* dev, gpba02b_port_t port, uint8_t pin, bool level) {
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

esp_err_t gpba02b_read_io(gpba02b_t* dev, gpba02b_port_t port, uint8_t pin, bool* level) {
    if (level == NULL || pin > 7 || !gpba02b_is_port_valid(port)) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t port_value = 0;
    esp_err_t err = gpba02b_read_port_input(dev, port, &port_value);
    if (err != ESP_OK) {
        return err;
    }

    *level = (port_value & (uint8_t)(1U << pin)) != 0;
    return ESP_OK;
}

esp_err_t gpba02b_config_io_input_mode(gpba02b_t* dev, gpba02b_port_t port, uint8_t pin,
                                       gpba02b_io_input_mode_t mode) {
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

esp_err_t gpba02b_config_io_output_mode(gpba02b_t* dev, gpba02b_port_t port, uint8_t pin,
                                        gpba02b_io_output_mode_t mode, bool level) {
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

esp_err_t gpba02b_config_io_input(gpba02b_t* dev, gpba02b_port_t port, uint8_t pin,
                                  bool pull_up) {
    return gpba02b_config_io_input_mode(dev, port, pin,
                                        pull_up ? GPBA02B_IO_INPUT_PULL_HIGH
                                                : GPBA02B_IO_INPUT_PULL_LOW);
}

esp_err_t gpba02b_config_io_output(gpba02b_t* dev, gpba02b_port_t port, uint8_t pin,
                                   bool open_collector, bool level) {
    return gpba02b_config_io_output_mode(dev, port, pin,
                                         open_collector ? GPBA02B_IO_OUTPUT_OPEN_DRAIN_NMOS
                                                        : GPBA02B_IO_OUTPUT_PUSH_PULL,
                                         level);
}

esp_err_t gpba02b_read_port_input(gpba02b_t* dev, gpba02b_port_t port, uint8_t* value) {
    if (value == NULL || !gpba02b_is_port_valid(port)) {
        return ESP_ERR_INVALID_ARG;
    }

    return gpba02b_read_reg(dev, (uint8_t)(kRegDataBase + port), value);
}

esp_err_t gpba02b_enable_new_functions(gpba02b_t* dev) {
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

esp_err_t gpba02b_set_software_reset_disabled(gpba02b_t* dev, bool disabled) {
    esp_err_t err = gpba02b_enable_new_functions(dev);
    if (err != ESP_OK) {
        return err;
    }

    uint8_t value = 0;
    err = gpba02b_read_reg(dev, kRegCurrentSink, &value);
    if (err != ESP_OK) {
        return err;
    }

    if (disabled) {
        value |= 0x80;
    } else {
        value &= (uint8_t)(~0x80);
    }

    return gpba02b_write_reg(dev, kRegCurrentSink, value);
}

esp_err_t gpba02b_pwm_set_clock_div(gpba02b_t* dev, uint8_t pa_div, uint8_t pc_div) {
    if (pa_div > 0x07 || pc_div > 0x07) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = gpba02b_enable_new_functions(dev);
    if (err != ESP_OK) {
        return err;
    }

    uint8_t value = (uint8_t)(((pa_div & 0x07) << 4) | (pc_div & 0x07));
    return gpba02b_write_reg(dev, kRegPwmClock, value);
}

esp_err_t gpba02b_pwm_enable_channels(gpba02b_t* dev, gpba02b_port_t port, uint8_t channel_mask) {
    if (!gpba02b_is_pwm_port(port)) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = gpba02b_enable_new_functions(dev);
    if (err != ESP_OK) {
        return err;
    }

    const uint8_t reg = (port == GPBA02B_PORT_A) ? kRegPwmEnableA : kRegPwmEnableC;
    return gpba02b_write_reg(dev, reg, channel_mask);
}

esp_err_t gpba02b_pwm_set_channel_duty(gpba02b_t* dev, gpba02b_port_t port, uint8_t channel,
                                       uint8_t duty) {
    if (!gpba02b_is_pwm_port(port) || channel > 7) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = gpba02b_enable_new_functions(dev);
    if (err != ESP_OK) {
        return err;
    }

    const uint8_t reg = (port == GPBA02B_PORT_A) ? kPwmDutyRegA[channel] : kPwmDutyRegC[channel];
    return gpba02b_write_reg(dev, reg, duty);
}

esp_err_t gpba02b_current_sink_set(gpba02b_t* dev, gpba02b_port_t port, bool enable,
                                   uint8_t current_level) {
    if (!gpba02b_is_pwm_port(port) || current_level > 0x03) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = gpba02b_enable_new_functions(dev);
    if (err != ESP_OK) {
        return err;
    }

    uint8_t value = 0;
    err = gpba02b_read_reg(dev, kRegCurrentSink, &value);
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

    return gpba02b_write_reg(dev, kRegCurrentSink, value);
}

esp_err_t gpba02b_pwm_with_current_sink_setup(gpba02b_t* dev, gpba02b_port_t port,
                                              uint8_t channel_mask, uint8_t current_level) {
    esp_err_t err = gpba02b_current_sink_set(dev, port, true, current_level);
    if (err != ESP_OK) {
        return err;
    }

    // Datasheet requires a short delay before enabling PWM channels after sink setup.
    esp_rom_delay_us(200);

    return gpba02b_pwm_enable_channels(dev, port, channel_mask);
}

esp_err_t gpba02b_interrupt_configure(gpba02b_t* dev, uint8_t enable_mask,
                                      uint8_t falling_edge_mask) {
    enable_mask &= 0x0F;
    falling_edge_mask &= 0x0F;

    esp_err_t err = gpba02b_enable_new_functions(dev);
    if (err != ESP_OK) {
        return err;
    }

    const uint8_t int_edge_cmos =
        (uint8_t)((falling_edge_mask << 4) | (dev->cmos_enable_mask & 0x0F));
    err = gpba02b_write_reg(dev, kRegIntEdgeCmos, int_edge_cmos);
    if (err != ESP_OK) {
        return err;
    }

    dev->interrupt_enable_mask = enable_mask;
    return gpba02b_write_reg(dev, kRegIntFlagEnable, dev->interrupt_enable_mask);
}

esp_err_t gpba02b_interrupt_read(gpba02b_t* dev, uint8_t* flags, uint8_t* enable_mask) {
    uint8_t value = 0;
    esp_err_t err = gpba02b_read_reg(dev, kRegIntFlagEnable, &value);
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

esp_err_t gpba02b_interrupt_clear(gpba02b_t* dev, uint8_t flags_mask) {
    flags_mask &= 0x0F;
    uint8_t value = (uint8_t)((flags_mask << 4) | (dev->interrupt_enable_mask & 0x0F));
    return gpba02b_write_reg(dev, kRegIntFlagEnable, value);
}

esp_err_t gpba02b_host_irq_install(gpba02b_t* dev, const gpba02b_host_irq_gpio_config_t* config,
                                   gpba02b_host_irq_callback_t callback, void* user_ctx) {
    if (dev == NULL || config == NULL || config->gpio_num == GPIO_NUM_NC) {
        return ESP_ERR_INVALID_ARG;
    }

    gpio_config_t gpio_config_data = {0};
    gpio_config_data.pin_bit_mask = 1ULL << config->gpio_num;
    gpio_config_data.mode = GPIO_MODE_INPUT;
    gpio_config_data.pull_up_en = config->pull_up ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE;
    gpio_config_data.pull_down_en = config->pull_down ? GPIO_PULLDOWN_ENABLE : GPIO_PULLDOWN_DISABLE;
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

esp_err_t gpba02b_host_irq_uninstall(gpba02b_t* dev) {
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
