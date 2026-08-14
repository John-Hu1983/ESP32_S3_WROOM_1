#include "bsp/bsp_env.h"
#include "bsp/gpba02b.h"
#include "codecs/no_audio_codec.h"
#include "config.h"
#include "display/lcd_display.h"
#include "mcp_server.h"
#include "peripherals/keyboard.h"
#include "service/display_factory.h"
#include "system_reset.h"
#include "wifi_board.h"

#include <driver/spi_common.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_vendor.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "esp_lcd_st7796.h"

#include <memory>
#include <utility>

#define TAG "Esp32S3Wroom1N16r8Board"

static BspEnv::Config CreateBspEnvConfig() {
    BspEnv::Config config = {};
    BspEnv::GetDefaultConfig(&config);

    config.power_lock_pin = {POWER_LOCK_IO_PORT, POWER_LOCK_IO_PIN};
    config.pdm_enable_pin = {PDM_EN_PORT, PDM_EN_PIN};
    config.i2s_enable_pin = {I2S_EN_PORT, I2S_EN_PIN};
    config.pidm_enable_pin = {PIDM_EN_PORT, PIDM_EN_PIN};
    config.button_up_pin = {BUTTON_UP_IO_PORT, BUTTON_UP_IO_PIN};
    config.button_down_pin = {BUTTON_DOWN_IO_PORT, BUTTON_DOWN_IO_PIN};
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

    config.pwm_enable_mask_port_a = static_cast<uint8_t>((1U << PWM_GPBA02B_07_PIN));
    config.pwm_enable_mask_port_c = static_cast<uint8_t>(
        (1U << PWM_GPBA02B_08_PIN) | (1U << PWM_GPBA02B_09_PIN) | (1U << PWM_GPBA02B_10_PIN) |
        (1U << PWM_GPBA02B_11_PIN) | (1U << PWM_GPBA02B_12_PIN) | (1U << PWM_GPBA02B_13_PIN));
    config.pwm_clock_div_port_a = PWM_GPBA02B_PA_CLOCK_DIV;
    config.pwm_clock_div_port_c = PWM_GPBA02B_PC_CLOCK_DIV;
    config.pwm_duty = PWM_GPBA02B_DUTY_10_PERCENT;

    return config;
}

static Keyboard::Config CreateKeyboardConfig() {
    Keyboard::Config config = {};
    config.keys[Keyboard::kKey0] = {BUTTON_UP_IO_PORT, BUTTON_UP_IO_PIN};
    config.keys[Keyboard::kKey1] = {BUTTON_DOWN_IO_PORT, BUTTON_DOWN_IO_PIN};
    config.active_low = true;
    config.poll_interval_ms = 10;
    config.debounce_ms = 30;
    config.long_press_ms = 700;
    return config;
}

class Esp32S3Wroom1N16r8Board : public WifiBoard {
private:
    esp_lcd_panel_io_handle_t panel_io_ = nullptr;
    esp_lcd_panel_handle_t panel_ = nullptr;
    Display* display_ = nullptr;
    Gpba02b& extend = Gpba02b::Instance();
    std::unique_ptr<BspEnv> bsp_env_;
    std::unique_ptr<Keyboard> keyboard_;
    BoardKeyEventCallback key_event_callback_ = nullptr;

    void NotifyKeyEvent(uint8_t key_index, BoardKeyEventType event_type) {
        if (key_event_callback_) {
            key_event_callback_(key_index, event_type);
            return;
        }

        ESP_LOGW(TAG, "Drop key event: key=%u type=%d (callback not set)", key_index,
                 static_cast<int>(event_type));
    }

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

        display_ = CreatePrimaryDisplay(panel_io_, panel_, LCD_DEFAULT_WIDTH, LCD_DEFAULT_HEIGHT,
                        0, 0, mirror_x, mirror_y, swap_xy);
    }

    void InitializeBspEnv() {
        bsp_env_ = std::make_unique<BspEnv>(CreateBspEnvConfig());
        bsp_env_->Initialize();
    }

    void InitializeKeyboard() {
        keyboard_ = std::make_unique<Keyboard>(CreateKeyboardConfig());

        keyboard_->OnClick(Keyboard::kKey0,
                           [this]() { NotifyKeyEvent(Keyboard::kKey0, BoardKeyEventType::Click); });
        keyboard_->OnLongPress(
            Keyboard::kKey0,
            [this]() { NotifyKeyEvent(Keyboard::kKey0, BoardKeyEventType::LongPress); });

        keyboard_->OnClick(Keyboard::kKey1,
                           [this]() { NotifyKeyEvent(Keyboard::kKey1, BoardKeyEventType::Click); });
        keyboard_->OnLongPress(
            Keyboard::kKey1,
            [this]() { NotifyKeyEvent(Keyboard::kKey1, BoardKeyEventType::LongPress); });
        keyboard_->OnPressDown(
            Keyboard::kKey1,
            [this]() { NotifyKeyEvent(Keyboard::kKey1, BoardKeyEventType::PressDown); });
        keyboard_->OnPressUp(Keyboard::kKey1,
                             [this]() { NotifyKeyEvent(Keyboard::kKey1, BoardKeyEventType::PressUp); });

        ESP_ERROR_CHECK(keyboard_->Start());
    }

public:
    Esp32S3Wroom1N16r8Board() {
        InitializeGpba02b();
        InitializeBspEnv();
        InitializeDisplaySpiBus();
        InitializeSt7365pDisplay();
        InitializeKeyboard();
    }

    virtual void SetKeyEventCallback(BoardKeyEventCallback callback) override {
        key_event_callback_ = std::move(callback);
    }

    // Speaker uses standard I2S, microphone uses PDM.
    virtual AudioCodec* GetAudioCodec() override {
        static NoAudioCodecSimplexPdm audio_codec(USER_AUDIO_SAMPLE_RATE_HZ,
                                                  USER_AUDIO_SAMPLE_RATE_HZ, I2S_BCK_IO, I2S_WS_IO,
                                                  I2S_DO_IO, PDM_CLK_IO, PDM_DATA_IO);
        return &audio_codec;
    }

    virtual Display* GetDisplay() override { return display_; }
};

DECLARE_BOARD(Esp32S3Wroom1N16r8Board);
