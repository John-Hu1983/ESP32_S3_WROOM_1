#include "bsp/gpba02b.h"

#include <cstring>

#include <driver/gpio.h>
#include <esp_log.h>
#include <esp_rom_sys.h>

#define TAG "GPBA02B"

bool Gpba02b::IsPortValid(Gpba02b::Port port) { return port <= Gpba02b::kPortC; }

bool Gpba02b::IsPwmPort(Gpba02b::Port port) {
    return port == Gpba02b::kPortA || port == Gpba02b::kPortC;
}

esp_err_t Gpba02b::EnsureInitialized() {
    if (spi_device_ == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }
    return ESP_OK;
}

uint8_t Gpba02b::MakeWriteCommand(uint8_t reg_addr) {
    return static_cast<uint8_t>(0x80 | ((device_id_ & 0x01) << 6) | (reg_addr & 0x3F));
}

uint8_t Gpba02b::MakeReadCommand(uint8_t reg_addr) {
    return static_cast<uint8_t>(((device_id_ & 0x01) << 6) | (reg_addr & 0x3F));
}

esp_err_t Gpba02b::TransferFrame(uint8_t command, uint8_t write_data, uint8_t* read_data) {
    uint8_t tx_buffer[2] = {command, write_data};
    uint8_t rx_buffer[2] = {0, 0};

    spi_transaction_t transaction = {};
    transaction.length = 16;
    transaction.tx_buffer = tx_buffer;
    transaction.rx_buffer = rx_buffer;

    esp_err_t err = spi_device_transmit(spi_device_, &transaction);
    if (err != ESP_OK) {
        return err;
    }

    if (read_data != nullptr) {
        *read_data = rx_buffer[1];
    }
    return ESP_OK;
}

uint8_t Gpba02b::IoRegisterBase(Gpba02b::IoReg reg) {
    switch (reg) {
        case Gpba02b::kIoRegBuffer:
            return kRegBufBase;
        case Gpba02b::kIoRegDirection:
            return kRegDirBase;
        case Gpba02b::kIoRegAttribute:
            return kRegAttBase;
        default:
            return 0xFF;
    }
}

void IRAM_ATTR Gpba02b::HostIrqHandler(void*) {
    Gpba02b& driver = Gpba02b::Instance();
    if (driver.host_irq_callback_ != nullptr) {
        driver.host_irq_callback_(driver.host_irq_user_ctx_);
    }
}

void Gpba02b::GetDefaultConfig(Gpba02b::Config* config) {
    if (config == nullptr) {
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

esp_err_t Gpba02b::Init(const Gpba02b::Config* config) {
    if (config == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }
    if (config->mosi_io == GPIO_NUM_NC || config->sclk_io == GPIO_NUM_NC ||
        config->cs_io == GPIO_NUM_NC) {
        return ESP_ERR_INVALID_ARG;
    }
    if (config->clock_hz <= 0) {
        return ESP_ERR_INVALID_ARG;
    }

    if (spi_device_ != nullptr) {
        Deinit();
    }

    spi_bus_config_t bus_config = {};
    bus_config.miso_io_num = config->miso_io;
    bus_config.mosi_io_num = config->mosi_io;
    bus_config.sclk_io_num = config->sclk_io;
    bus_config.quadwp_io_num = GPIO_NUM_NC;
    bus_config.quadhd_io_num = GPIO_NUM_NC;

    esp_err_t err = spi_bus_initialize(config->spi_host, &bus_config, SPI_DMA_CH_AUTO);
    if (err == ESP_OK) {
        bus_owned_ = true;
    } else if (err == ESP_ERR_INVALID_STATE) {
        bus_owned_ = false;
        err = ESP_OK;
    }
    if (err != ESP_OK) {
        return err;
    }

    spi_device_interface_config_t device_config = {};
    device_config.mode = 0;
    device_config.clock_speed_hz = config->clock_hz;
    device_config.spics_io_num = config->cs_io;
    device_config.queue_size = config->queue_size > 0 ? config->queue_size : 4;

    err = spi_bus_add_device(config->spi_host, &device_config, &spi_device_);
    if (err != ESP_OK) {
        if (bus_owned_) {
            spi_bus_free(config->spi_host);
            bus_owned_ = false;
        }
        return err;
    }

    spi_host_ = config->spi_host;
    device_id_ = config->device_id & 0x01;
    new_function_enabled_ = false;
    interrupt_enable_mask_ = 0;
    cmos_enable_mask_ = 0;
    od_nmos_mask_[0] = 0;
    od_nmos_mask_[1] = 0;
    od_nmos_mask_[2] = 0;
    od_pmos_mask_[0] = 0;
    od_pmos_mask_[1] = 0;
    od_pmos_mask_[2] = 0;

    ESP_LOGI(TAG, "GPBA02B initialized on SPI host %d, device_id=%u",
             static_cast<int>(config->spi_host), static_cast<unsigned>(device_id_));
    return ESP_OK;
}

void Gpba02b::Deinit() {
    HostIrqUninstall();

    if (spi_device_ != nullptr) {
        spi_bus_remove_device(spi_device_);
        spi_device_ = nullptr;
    }

    if (bus_owned_) {
        spi_bus_free(spi_host_);
        bus_owned_ = false;
    }

    new_function_enabled_ = false;
    interrupt_enable_mask_ = 0;
    cmos_enable_mask_ = 0;
    od_nmos_mask_[0] = 0;
    od_nmos_mask_[1] = 0;
    od_nmos_mask_[2] = 0;
    od_pmos_mask_[0] = 0;
    od_pmos_mask_[1] = 0;
    od_pmos_mask_[2] = 0;
}

esp_err_t Gpba02b::WriteReg(uint8_t reg_addr, uint8_t value) {
    esp_err_t err = EnsureInitialized();
    if (err != ESP_OK) {
        return err;
    }

    return TransferFrame(MakeWriteCommand(reg_addr), value, nullptr);
}

esp_err_t Gpba02b::ReadReg(uint8_t reg_addr, uint8_t* value) {
    if (value == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = EnsureInitialized();
    if (err != ESP_OK) {
        return err;
    }

    return TransferFrame(MakeReadCommand(reg_addr), 0x00, value);
}

esp_err_t Gpba02b::IoUpdate(Gpba02b::Port port, Gpba02b::IoReg reg, uint8_t operation,
                            uint8_t value) {
    if (!IsPortValid(port)) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t base = IoRegisterBase(reg);
    if (base == 0xFF) {
        return ESP_ERR_INVALID_ARG;
    }

    return WriteReg(static_cast<uint8_t>(base + operation + port), value);
}

esp_err_t Gpba02b::IoWrite(Gpba02b::Port port, Gpba02b::IoReg reg, uint8_t value) {
    return IoUpdate(port, reg, kOpWrite, value);
}

esp_err_t Gpba02b::IoRead(Gpba02b::Port port, Gpba02b::IoReg reg, uint8_t* value) {
    if (value == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!IsPortValid(port)) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t base = IoRegisterBase(reg);
    if (base == 0xFF) {
        return ESP_ERR_INVALID_ARG;
    }

    return ReadReg(static_cast<uint8_t>(base + port), value);
}

esp_err_t Gpba02b::IoAnd(Gpba02b::Port port, Gpba02b::IoReg reg, uint8_t value) {
    return IoUpdate(port, reg, kOpAnd, value);
}

esp_err_t Gpba02b::IoOr(Gpba02b::Port port, Gpba02b::IoReg reg, uint8_t value) {
    return IoUpdate(port, reg, kOpOr, value);
}

esp_err_t Gpba02b::IoXor(Gpba02b::Port port, Gpba02b::IoReg reg, uint8_t value) {
    return IoUpdate(port, reg, kOpXor, value);
}

esp_err_t Gpba02b::config_io_reg_bit(Gpba02b::Port port, Gpba02b::IoReg reg,
                                     uint8_t pin, bool bit_value) {
    if (pin > 7) {
        return ESP_ERR_INVALID_ARG;
    }

    const uint8_t bit_mask = static_cast<uint8_t>(1U << pin);
    if (bit_value) {
        return IoOr(port, reg, bit_mask);
    }

    return IoAnd(port, reg, static_cast<uint8_t>(~bit_mask));
}

esp_err_t Gpba02b::write_io(Gpba02b::Port port, uint8_t pin, bool level) {
    if (pin > 7 || !IsPortValid(port)) {
        return ESP_ERR_INVALID_ARG;
    }

    const uint8_t bit_mask = static_cast<uint8_t>(1U << pin);
    int port_index = static_cast<int>(port);

    if ((od_nmos_mask_[port_index] & bit_mask) != 0) {
        return config_io_reg_bit(port, kIoRegDirection, pin, !level);
    }
    if ((od_pmos_mask_[port_index] & bit_mask) != 0) {
        return config_io_reg_bit(port, kIoRegDirection, pin, level);
    }

    return config_io_reg_bit(port, kIoRegBuffer, pin, level);
}

esp_err_t Gpba02b::read_io(Gpba02b::Port port, uint8_t pin, bool* level) {
    if (level == nullptr || pin > 7 || !IsPortValid(port)) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t port_value = 0;
    esp_err_t err = ReadPortInput(port, &port_value);
    if (err != ESP_OK) {
        return err;
    }

    *level = (port_value & static_cast<uint8_t>(1U << pin)) != 0;
    return ESP_OK;
}

esp_err_t Gpba02b::config_io_input(Gpba02b::Port port, uint8_t pin, Gpba02b::IoInputMode mode) {
    if (pin > 7 || !IsPortValid(port)) {
        return ESP_ERR_INVALID_ARG;
    }

    if (mode != Gpba02b::kIoInputFloating && mode != Gpba02b::kIoInputPullLow &&
        mode != Gpba02b::kIoInputPullHigh) {
        return ESP_ERR_INVALID_ARG;
    }

    const uint8_t bit_mask = static_cast<uint8_t>(1U << pin);
    int port_index = static_cast<int>(port);
    od_nmos_mask_[port_index] &= static_cast<uint8_t>(~bit_mask);
    od_pmos_mask_[port_index] &= static_cast<uint8_t>(~bit_mask);

    esp_err_t err = config_io_reg_bit(port, kIoRegDirection, pin, false);
    if (err != ESP_OK) {
        return err;
    }

    switch (mode) {
        case Gpba02b::kIoInputFloating:
            err = config_io_reg_bit(port, kIoRegBuffer, pin, false);
            if (err != ESP_OK) {
                return err;
            }
            return config_io_reg_bit(port, kIoRegAttribute, pin, false);
        case Gpba02b::kIoInputPullLow:
            err = config_io_reg_bit(port, kIoRegAttribute, pin, false);
            if (err != ESP_OK) {
                return err;
            }
            return config_io_reg_bit(port, kIoRegBuffer, pin, true);
        case Gpba02b::kIoInputPullHigh:
            err = config_io_reg_bit(port, kIoRegAttribute, pin, true);
            if (err != ESP_OK) {
                return err;
            }
            return config_io_reg_bit(port, kIoRegBuffer, pin, true);
        default:
            return ESP_ERR_INVALID_ARG;
    }
}

esp_err_t Gpba02b::config_io_output(Gpba02b::Port port, uint8_t pin, Gpba02b::IoOutputMode mode,
                                    bool level) {
    if (pin > 7 || !IsPortValid(port)) {
        return ESP_ERR_INVALID_ARG;
    }

    if (mode != Gpba02b::kIoOutputPushPull && mode != Gpba02b::kIoOutputOpenDrainNmos &&
        mode != Gpba02b::kIoOutputOpenDrainPmos) {
        return ESP_ERR_INVALID_ARG;
    }

    const uint8_t bit_mask = static_cast<uint8_t>(1U << pin);
    int port_index = static_cast<int>(port);
    od_nmos_mask_[port_index] &= static_cast<uint8_t>(~bit_mask);
    od_pmos_mask_[port_index] &= static_cast<uint8_t>(~bit_mask);

    if (mode == Gpba02b::kIoOutputOpenDrainNmos) {
        od_nmos_mask_[port_index] |= bit_mask;
    } else if (mode == Gpba02b::kIoOutputOpenDrainPmos) {
        od_pmos_mask_[port_index] |= bit_mask;
    }

    esp_err_t err = ESP_OK;

    switch (mode) {
        case Gpba02b::kIoOutputPushPull:
            err = config_io_reg_bit(port, kIoRegAttribute, pin, false);
            if (err != ESP_OK) {
                return err;
            }
            err = config_io_reg_bit(port, kIoRegBuffer, pin, level);
            if (err != ESP_OK) {
                return err;
            }
            return config_io_reg_bit(port, kIoRegDirection, pin, true);
        case Gpba02b::kIoOutputOpenDrainNmos:
            err = config_io_reg_bit(port, kIoRegAttribute, pin, false);
            if (err != ESP_OK) {
                return err;
            }
            err = config_io_reg_bit(port, kIoRegBuffer, pin, false);
            if (err != ESP_OK) {
                return err;
            }
            return config_io_reg_bit(port, kIoRegDirection, pin, !level);
        case Gpba02b::kIoOutputOpenDrainPmos:
            err = config_io_reg_bit(port, kIoRegAttribute, pin, true);
            if (err != ESP_OK) {
                return err;
            }
            err = config_io_reg_bit(port, kIoRegBuffer, pin, false);
            if (err != ESP_OK) {
                return err;
            }
            return config_io_reg_bit(port, kIoRegDirection, pin, level);
        default:
            return ESP_ERR_INVALID_ARG;
    }
}

esp_err_t Gpba02b::config_io_input(Gpba02b::Port port, uint8_t pin, bool pull_up) {
    return config_io_input(port, pin,
                           pull_up ? Gpba02b::kIoInputPullHigh : Gpba02b::kIoInputPullLow);
}

esp_err_t Gpba02b::config_io_output(Gpba02b::Port port, uint8_t pin, bool open_collector,
                                    bool level) {
    return config_io_output(port,
                            pin,
                            open_collector ? Gpba02b::kIoOutputOpenDrainNmos
                                           : Gpba02b::kIoOutputPushPull,
                            level);
}

esp_err_t Gpba02b::ReadPortInput(Gpba02b::Port port, uint8_t* value) {
    if (value == nullptr || !IsPortValid(port)) {
        return ESP_ERR_INVALID_ARG;
    }

    return ReadReg(static_cast<uint8_t>(kRegDataBase + port), value);
}

esp_err_t Gpba02b::EnableNewFunctions() {
    if (new_function_enabled_) {
        return ESP_OK;
    }

    esp_err_t err = EnsureInitialized();
    if (err != ESP_OK) {
        return err;
    }

    err = WriteReg(0x03, 0x55);
    if (err != ESP_OK) {
        return err;
    }
    err = WriteReg(0x07, 0xAA);
    if (err != ESP_OK) {
        return err;
    }
    err = WriteReg(0x03, 0x55);
    if (err != ESP_OK) {
        return err;
    }
    err = WriteReg(0x07, 0xAA);
    if (err != ESP_OK) {
        return err;
    }

    new_function_enabled_ = true;
    return ESP_OK;
}

esp_err_t Gpba02b::SetSoftwareResetDisabled(bool disabled) {
    esp_err_t err = EnableNewFunctions();
    if (err != ESP_OK) {
        return err;
    }

    uint8_t value = 0;
    err = ReadReg(kRegCurrentSink, &value);
    if (err != ESP_OK) {
        return err;
    }

    if (disabled) {
        value |= 0x80;
    } else {
        value &= static_cast<uint8_t>(~0x80);
    }

    return WriteReg(kRegCurrentSink, value);
}

esp_err_t Gpba02b::PwmSetClockDiv(uint8_t pa_div, uint8_t pc_div) {
    if (pa_div > 0x07 || pc_div > 0x07) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = EnableNewFunctions();
    if (err != ESP_OK) {
        return err;
    }

    uint8_t value = static_cast<uint8_t>(((pa_div & 0x07) << 4) | (pc_div & 0x07));
    return WriteReg(kRegPwmClock, value);
}

esp_err_t Gpba02b::PwmEnableChannels(Gpba02b::Port port, uint8_t channel_mask) {
    if (!IsPwmPort(port)) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = EnableNewFunctions();
    if (err != ESP_OK) {
        return err;
    }

    const uint8_t reg = (port == Gpba02b::kPortA) ? kRegPwmEnableA : kRegPwmEnableC;
    return WriteReg(reg, channel_mask);
}

esp_err_t Gpba02b::PwmSetChannelDuty(Gpba02b::Port port, uint8_t channel, uint8_t duty) {
    if (!IsPwmPort(port) || channel > 7) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = EnableNewFunctions();
    if (err != ESP_OK) {
        return err;
    }

    const uint8_t reg = (port == Gpba02b::kPortA) ? kPwmDutyRegA[channel] : kPwmDutyRegC[channel];
    return WriteReg(reg, duty);
}

esp_err_t Gpba02b::CurrentSinkSet(Gpba02b::Port port, bool enable,
                                  uint8_t current_level) {
    if (!IsPwmPort(port) || current_level > 0x03) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = EnableNewFunctions();
    if (err != ESP_OK) {
        return err;
    }

    uint8_t value = 0;
    err = ReadReg(kRegCurrentSink, &value);
    if (err != ESP_OK) {
        return err;
    }

    if (port == Gpba02b::kPortA) {
        if (enable) {
            value |= 0x04;
        } else {
            value &= static_cast<uint8_t>(~0x04);
        }
        value = static_cast<uint8_t>((value & ~0x03) | (current_level & 0x03));
    } else {
        if (enable) {
            value |= 0x40;
        } else {
            value &= static_cast<uint8_t>(~0x40);
        }
        value = static_cast<uint8_t>((value & ~0x30) | ((current_level & 0x03) << 4));
    }

    return WriteReg(kRegCurrentSink, value);
}

esp_err_t Gpba02b::PwmWithCurrentSinkSetup(Gpba02b::Port port, uint8_t channel_mask,
                                           uint8_t current_level) {
    esp_err_t err = CurrentSinkSet(port, true, current_level);
    if (err != ESP_OK) {
        return err;
    }

    // Datasheet requires a short delay before enabling PWM channels after sink setup.
    esp_rom_delay_us(200);

    return PwmEnableChannels(port, channel_mask);
}

esp_err_t Gpba02b::InterruptConfigure(uint8_t enable_mask, uint8_t falling_edge_mask) {
    enable_mask &= 0x0F;
    falling_edge_mask &= 0x0F;

    esp_err_t err = EnableNewFunctions();
    if (err != ESP_OK) {
        return err;
    }

    const uint8_t int_edge_cmos =
        static_cast<uint8_t>((falling_edge_mask << 4) | (cmos_enable_mask_ & 0x0F));
    err = WriteReg(kRegIntEdgeCmos, int_edge_cmos);
    if (err != ESP_OK) {
        return err;
    }

    interrupt_enable_mask_ = enable_mask;
    return WriteReg(kRegIntFlagEnable, interrupt_enable_mask_);
}

esp_err_t Gpba02b::InterruptRead(uint8_t* flags, uint8_t* enable_mask) {
    uint8_t value = 0;
    esp_err_t err = ReadReg(kRegIntFlagEnable, &value);
    if (err != ESP_OK) {
        return err;
    }

    if (flags != nullptr) {
        *flags = static_cast<uint8_t>((value >> 4) & 0x0F);
    }
    if (enable_mask != nullptr) {
        *enable_mask = static_cast<uint8_t>(value & 0x0F);
    }

    interrupt_enable_mask_ = static_cast<uint8_t>(value & 0x0F);
    return ESP_OK;
}

esp_err_t Gpba02b::InterruptClear(uint8_t flags_mask) {
    flags_mask &= 0x0F;
    uint8_t value = static_cast<uint8_t>((flags_mask << 4) | (interrupt_enable_mask_ & 0x0F));
    return WriteReg(kRegIntFlagEnable, value);
}

esp_err_t Gpba02b::HostIrqInstall(const Gpba02b::HostIrqGpioConfig* config,
                                  Gpba02b::HostIrqCallback callback, void* user_ctx) {
    if (config == nullptr || config->gpio_num == GPIO_NUM_NC) {
        return ESP_ERR_INVALID_ARG;
    }

    gpio_config_t gpio_config_data = {};
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

    if (host_irq_gpio_ != GPIO_NUM_NC) {
        gpio_isr_handler_remove(host_irq_gpio_);
    }

    host_irq_gpio_ = config->gpio_num;
    host_irq_callback_ = callback;
    host_irq_user_ctx_ = user_ctx;

    return gpio_isr_handler_add(host_irq_gpio_, HostIrqHandler, nullptr);
}

esp_err_t Gpba02b::HostIrqUninstall() {
    if (host_irq_gpio_ != GPIO_NUM_NC) {
        gpio_isr_handler_remove(host_irq_gpio_);
        host_irq_gpio_ = GPIO_NUM_NC;
    }

    host_irq_callback_ = nullptr;
    host_irq_user_ctx_ = nullptr;
    return ESP_OK;
}

Gpba02b& Gpba02b::Instance() {
    static Gpba02b instance;
    return instance;
}
