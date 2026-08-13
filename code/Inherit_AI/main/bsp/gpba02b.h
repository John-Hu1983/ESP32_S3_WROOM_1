#pragma once

#include <stdint.h>

#include <driver/gpio.h>
#include <driver/spi_master.h>
#include <esp_err.h>

class Gpba02b {
public:
    using Port = uint8_t;

    enum IoReg : uint8_t {
        kIoRegBuffer = 0,
        kIoRegDirection = 1,
        kIoRegAttribute = 2,
    };

    enum IoInputMode : uint8_t {
        kIoInputFloating = 0,
        kIoInputPullLow = 1,
        kIoInputPullHigh = 2,
    };

    enum IoOutputMode : uint8_t {
        kIoOutputPushPull = 0,
        kIoOutputOpenDrainNmos = 1,
        kIoOutputOpenDrainPmos = 2,
    };

    struct Config {
        spi_host_device_t spi_host;
        gpio_num_t miso_io;
        gpio_num_t mosi_io;
        gpio_num_t sclk_io;
        gpio_num_t cs_io;
        int clock_hz;
        uint8_t device_id;
        int queue_size;
    };

    struct HostIrqGpioConfig {
        gpio_num_t gpio_num;
        gpio_int_type_t intr_type;
        bool pull_up;
        bool pull_down;
    };

    using HostIrqCallback = void (*)(void* user_ctx);

    static constexpr Port kPortA = 0;
    static constexpr Port kPortB = 1;
    static constexpr Port kPortC = 2;

    static Gpba02b& Instance();

    void GetDefaultConfig(Config* config);

    esp_err_t Init(const Config* config);
    void Deinit();

    esp_err_t IoWrite(Port port, IoReg reg, uint8_t value);
    esp_err_t IoRead(Port port, IoReg reg, uint8_t* value);
    esp_err_t write_io(Port port, uint8_t pin, bool level);
    esp_err_t read_io(Port port, uint8_t pin, bool* level);
    esp_err_t config_io_input(Port port, uint8_t pin, IoInputMode mode);
    esp_err_t config_io_output(Port port, uint8_t pin, IoOutputMode mode, bool level);

    esp_err_t config_io_input(Port port, uint8_t pin, bool pull_up);
    esp_err_t config_io_output(Port port, uint8_t pin, bool open_collector, bool level);

    esp_err_t ReadPortInput(Port port, uint8_t* value);

    esp_err_t EnableNewFunctions();
    esp_err_t SetSoftwareResetDisabled(bool disabled);

    esp_err_t PwmSetClockDiv(uint8_t pa_div, uint8_t pc_div);
    esp_err_t PwmEnableChannels(Port port, uint8_t channel_mask);
    esp_err_t PwmSetChannelDuty(Port port, uint8_t channel, uint8_t duty);
    esp_err_t CurrentSinkSet(Port port, bool enable, uint8_t current_level);
    esp_err_t PwmWithCurrentSinkSetup(Port port, uint8_t channel_mask, uint8_t current_level);

    esp_err_t InterruptConfigure(uint8_t enable_mask, uint8_t falling_edge_mask);
    esp_err_t InterruptRead(uint8_t* flags, uint8_t* enable_mask);
    esp_err_t InterruptClear(uint8_t flags_mask);

    esp_err_t HostIrqInstall(const HostIrqGpioConfig* config, HostIrqCallback callback,
                             void* user_ctx);
    esp_err_t HostIrqUninstall();

private:
    bool IsPortValid(Port port);
    bool IsPwmPort(Port port);
    esp_err_t EnsureInitialized();
    uint8_t MakeWriteCommand(uint8_t reg_addr);
    uint8_t MakeReadCommand(uint8_t reg_addr);
    esp_err_t TransferFrame(uint8_t command, uint8_t write_data, uint8_t* read_data);
    uint8_t IoRegisterBase(IoReg reg);
    esp_err_t IoUpdate(Port port, IoReg reg, uint8_t operation, uint8_t value);
    esp_err_t config_io_reg_bit(Port port, IoReg reg, uint8_t pin, bool bit_value);
    esp_err_t IoAnd(Port port, IoReg reg, uint8_t value);
    esp_err_t IoOr(Port port, IoReg reg, uint8_t value);
    esp_err_t IoXor(Port port, IoReg reg, uint8_t value);
    static void HostIrqHandler(void* arg);

    static constexpr uint8_t kRegBufBase = 0x00;
    static constexpr uint8_t kRegDirBase = 0x04;
    static constexpr uint8_t kRegAttBase = 0x08;
    static constexpr uint8_t kRegDataBase = 0x0C;

    static constexpr uint8_t kRegIntFlagEnable = 0x0B;
    static constexpr uint8_t kRegCurrentSink = 0x13;
    static constexpr uint8_t kRegPwmEnableA = 0x17;
    static constexpr uint8_t kRegPwmClock = 0x1B;
    static constexpr uint8_t kRegPwmEnableC = 0x27;
    static constexpr uint8_t kRegIntEdgeCmos = 0x23;

    static constexpr uint8_t kPwmDutyRegA[8] = {0x1C, 0x1D, 0x1E, 0x1F, 0x2B, 0x2C, 0x2D, 0x2E};
    static constexpr uint8_t kPwmDutyRegC[8] = {0x2F, 0x33, 0x37, 0x3B, 0x3C, 0x3D, 0x3E, 0x3F};

    static constexpr uint8_t kOpWrite = 0x00;
    static constexpr uint8_t kOpAnd = 0x10;
    static constexpr uint8_t kOpOr = 0x20;
    static constexpr uint8_t kOpXor = 0x30;

    esp_err_t WriteReg(uint8_t reg_addr, uint8_t value);
    esp_err_t ReadReg(uint8_t reg_addr, uint8_t* value);

    spi_device_handle_t spi_device_ = nullptr;
    spi_host_device_t spi_host_ = SPI3_HOST;
    bool bus_owned_ = false;
    uint8_t device_id_ = 0;
    bool new_function_enabled_ = false;
    uint8_t interrupt_enable_mask_ = 0;
    uint8_t cmos_enable_mask_ = 0;
    uint8_t od_nmos_mask_[3] = {0, 0, 0};
    uint8_t od_pmos_mask_[3] = {0, 0, 0};
    gpio_num_t host_irq_gpio_ = GPIO_NUM_NC;
    HostIrqCallback host_irq_callback_ = nullptr;
    void* host_irq_user_ctx_ = nullptr;

    Gpba02b() = default;
    Gpba02b(const Gpba02b&) = delete;
    Gpba02b& operator=(const Gpba02b&) = delete;
};
