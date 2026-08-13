#include "application.h"
#include "assets/lang_config.h"
#include "bsp/bsp_env.h"
#include "bsp/gpba02b.h"
#include "button.h"
#include "codecs/no_audio_codec.h"
#include "config.h"
#include "display/lcd_display.h"
#include "lamp_controller.h"
#include "led/single_led.h"
#include "mcp_server.h"
#include "system_reset.h"
#include "wifi_board.h"

#include <driver/spi_common.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_vendor.h>
#include <esp_log.h>
#include "esp_lcd_st7796.h"

#define TAG "Esp32S3Wroom1N16r8Board"

#ifndef BOOT_BUTTON_GPIO
#define BOOT_BUTTON_GPIO GPIO_NUM_NC
#endif

#ifndef TOUCH_BUTTON_GPIO
#define TOUCH_BUTTON_GPIO GPIO_NUM_NC
#endif

#ifndef VOLUME_UP_BUTTON_GPIO
#define VOLUME_UP_BUTTON_GPIO GPIO_NUM_NC
#endif

#ifndef VOLUME_DOWN_BUTTON_GPIO
#define VOLUME_DOWN_BUTTON_GPIO GPIO_NUM_NC
#endif

#ifndef BUILTIN_LED_GPIO
#define BUILTIN_LED_GPIO GPIO_NUM_NC
#endif

#ifndef LAMP_GPIO
#define LAMP_GPIO GPIO_NUM_NC
#endif

namespace {
BspEnv::Config CreateBspEnvConfig() {
    BspEnv::Config config = {};
    BspEnv::GetDefaultConfig(&config);

    config.power_lock_pin = {POWER_LOCK_IO_PORT, POWER_LOCK_IO_PIN};
    config.pdm_enable_pin = {PDM_EN_PORT, PDM_EN_PIN};
    config.i2s_enable_pin = {I2S_EN_PORT, I2S_EN_PIN};
    config.pidm_enable_pin = {PIDM_EN_PORT, PIDM_EN_PIN};
    config.lcd_reset_pin = {LCD_IO_RESET_PORT, LCD_IO_RESET_PIN};

#if defined(CAM_IO_RESET_PORT) && defined(CAM_IO_RESET_PIN) && defined(CAM_IO_PWDN_PORT) && \
    defined(CAM_IO_PWDN_PIN)
    config.camera_present = true;
    config.camera_reset_pin = {CAM_IO_RESET_PORT, CAM_IO_RESET_PIN};
    config.camera_pwdn_pin = {CAM_IO_PWDN_PORT, CAM_IO_PWDN_PIN};
#if defined(CAM_IO_LIGHT_PORT) && defined(CAM_IO_LIGHT_PIN)
    config.camera_light_pin = {CAM_IO_LIGHT_PORT, CAM_IO_LIGHT_PIN};
#endif
#endif

#if defined(RC522_RST_PORT) && defined(RC522_RST_PIN)
    config.rc522_present = true;
    config.rc522_reset_pin = {RC522_RST_PORT, RC522_RST_PIN};
#endif

#if defined(RC522_ENABLE_PORT) && defined(RC522_ENABLE_PIN)
    config.rc522_enable_pin = {RC522_ENABLE_PORT, RC522_ENABLE_PIN};
#endif

    config.pwm_enable_mask_port_a = static_cast<uint8_t>((1U << PWM_GPBA02B_07_PIN));
    config.pwm_enable_mask_port_c =
        static_cast<uint8_t>((1U << PWM_GPBA02B_08_PIN) | (1U << PWM_GPBA02B_09_PIN) |
                             (1U << PWM_GPBA02B_10_PIN) | (1U << PWM_GPBA02B_11_PIN) |
                             (1U << PWM_GPBA02B_12_PIN) | (1U << PWM_GPBA02B_13_PIN));
    config.pwm_clock_div_port_a = PWM_GPBA02B_PA_CLOCK_DIV;
    config.pwm_clock_div_port_c = PWM_GPBA02B_PC_CLOCK_DIV;
    config.pwm_duty = PWM_GPBA02B_DUTY_10_PERCENT;

    return config;
}
}  // namespace

class Esp32S3Wroom1N16r8Board : public WifiBoard {
private:
    esp_lcd_panel_io_handle_t panel_io_ = nullptr;
    esp_lcd_panel_handle_t panel_ = nullptr;
    Display* display_ = nullptr;
    Gpba02b& extend = Gpba02b::Instance();
    BspEnv bsp_env_;
    Button boot_button_;
    Button touch_button_;
    Button volume_up_button_;
    Button volume_down_button_;

    esp_err_t InitializeGpba02b() {
        Gpba02b::Config gpba02b_config = {};
        extend.GetDefaultConfig(&gpba02b_config);
        gpba02b_config.spi_host = GPBA02B_SPI_HOST;
        gpba02b_config.miso_io = GPBA02B_IO_MISO;
        gpba02b_config.mosi_io = GPBA02B_IO_MOSI;
        gpba02b_config.sclk_io = GPBA02B_IO_CLK;
        gpba02b_config.cs_io = GPBA02B_IO_CS;
        gpba02b_config.clock_hz = GPBA02B_DEFAULT_CLOCK_HZ;
        gpba02b_config.device_id = GPBA02B_DEVICE_ID;

        esp_err_t err = extend.Init(&gpba02b_config);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to initialize GPBA02B: %s", esp_err_to_name(err));
            return err;
        }

        ESP_LOGI(TAG, "GPBA02B initialized");
        return ESP_OK;
    }

    void InitializeDisplaySpiBus() {
        spi_bus_config_t bus_config = {};
        bus_config.mosi_io_num = LCD_IO_MOSI;
        bus_config.miso_io_num = LCD_IO_MISO;
        bus_config.sclk_io_num = LCD_IO_CLK;
        bus_config.quadwp_io_num = GPIO_NUM_NC;
        bus_config.quadhd_io_num = GPIO_NUM_NC;
        bus_config.max_transfer_sz = LCD_DEFAULT_WIDTH * LCD_DEFAULT_HEIGHT * sizeof(uint16_t);
        ESP_ERROR_CHECK(spi_bus_initialize(LCD_SPI_HOST, &bus_config, SPI_DMA_CH_AUTO));
    }

    void InitializeSt7365pDisplay() {
        esp_lcd_panel_io_spi_config_t io_config = {};
        io_config.cs_gpio_num = LCD_IO_CS;
        io_config.dc_gpio_num = LCD_IO_RS;
        io_config.spi_mode = 0;
        io_config.pclk_hz = LCD_DEFAULT_CLOCK_HZ;
        io_config.trans_queue_depth = 10;
        io_config.lcd_cmd_bits = 8;
        io_config.lcd_param_bits = 8;
        ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(LCD_SPI_HOST, &io_config, &panel_io_));

        ESP_LOGI(TAG, "Install ST7365P-compatible LCD driver");
        esp_lcd_panel_dev_config_t panel_config = {};
        // Reset is currently handled outside direct GPIO in this board config.
        panel_config.reset_gpio_num = GPIO_NUM_NC;
        panel_config.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR;
        panel_config.bits_per_pixel = 16;

        // ST7365P has no dedicated component in this project yet; use the
        // closest supported ST7796 command-set backend first.
        ESP_ERROR_CHECK(esp_lcd_new_panel_st7796(panel_io_, &panel_config, &panel_));

        ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_));
        if (esp_lcd_panel_init(panel_) != ESP_OK) {
            ESP_LOGE(TAG, "Failed to initialize display");
            display_ = new NoDisplay();
            return;
        }

        const bool swap_xy = (LCD_DEFAULT_MADCTL & 0x20) != 0;
        const bool mirror_x = (LCD_DEFAULT_MADCTL & 0x40) != 0;
        const bool mirror_y = (LCD_DEFAULT_MADCTL & 0x80) != 0;
        ESP_ERROR_CHECK(esp_lcd_panel_swap_xy(panel_, swap_xy));
        ESP_ERROR_CHECK(esp_lcd_panel_mirror(panel_, mirror_x, mirror_y));
        ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel_, LCD_DEFAULT_INVERT_COLOR));

        ESP_LOGI(TAG, "Turning display on");
        ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_, true));

        display_ = new SpiLcdDisplay(panel_io_, panel_, LCD_DEFAULT_WIDTH, LCD_DEFAULT_HEIGHT, 0,
                                     0, mirror_x, mirror_y, swap_xy);
    }

    void InitializeButtons() {
        boot_button_.OnClick([this]() {
            auto& app = Application::GetInstance();
            if (app.GetDeviceState() == kDeviceStateStarting) {
                EnterWifiConfigMode();
                return;
            }
            app.ToggleChatState();
        });
        touch_button_.OnPressDown([this]() { Application::GetInstance().StartListening(); });
        touch_button_.OnPressUp([this]() { Application::GetInstance().StopListening(); });

        volume_up_button_.OnClick([this]() {
            auto codec = GetAudioCodec();
            auto volume = codec->output_volume() + 10;
            if (volume > 100) {
                volume = 100;
            }
            codec->SetOutputVolume(volume);
            GetDisplay()->ShowNotification(Lang::Strings::VOLUME + std::to_string(volume));
        });

        volume_up_button_.OnLongPress([this]() {
            GetAudioCodec()->SetOutputVolume(100);
            GetDisplay()->ShowNotification(Lang::Strings::MAX_VOLUME);
        });

        volume_down_button_.OnClick([this]() {
            auto codec = GetAudioCodec();
            auto volume = codec->output_volume() - 10;
            if (volume < 0) {
                volume = 0;
            }
            codec->SetOutputVolume(volume);
            GetDisplay()->ShowNotification(Lang::Strings::VOLUME + std::to_string(volume));
        });

        volume_down_button_.OnLongPress([this]() {
            GetAudioCodec()->SetOutputVolume(0);
            GetDisplay()->ShowNotification(Lang::Strings::MUTED);
        });
    }

    void InitializeTools() {
        static LampController lamp(LAMP_GPIO);
        (void)lamp;
    }

public:
        Esp32S3Wroom1N16r8Board()
                : bsp_env_(CreateBspEnvConfig()),
                    boot_button_(BOOT_BUTTON_GPIO),
                    touch_button_(TOUCH_BUTTON_GPIO),
                    volume_up_button_(VOLUME_UP_BUTTON_GPIO),
                    volume_down_button_(VOLUME_DOWN_BUTTON_GPIO) {
                ESP_ERROR_CHECK(InitializeGpba02b());
                ESP_ERROR_CHECK(bsp_env_.Initialize());
        InitializeDisplaySpiBus();
        InitializeSt7365pDisplay();
        InitializeButtons();
        InitializeTools();
    }

    virtual Led* GetLed() override {
        static SingleLed led(BUILTIN_LED_GPIO);
        return &led;
    }

    virtual AudioCodec* GetAudioCodec() override {
        // Speaker uses standard I2S, microphone uses PDM.
        static NoAudioCodecSimplexPdm audio_codec(
            USER_AUDIO_SAMPLE_RATE_HZ, USER_AUDIO_SAMPLE_RATE_HZ, I2S_BCK_IO, I2S_WS_IO,
            I2S_DO_IO, PDM_CLK_IO, PDM_DATA_IO);
        return &audio_codec;
    }

    virtual Display* GetDisplay() override { return display_; }
};

DECLARE_BOARD(Esp32S3Wroom1N16r8Board);

