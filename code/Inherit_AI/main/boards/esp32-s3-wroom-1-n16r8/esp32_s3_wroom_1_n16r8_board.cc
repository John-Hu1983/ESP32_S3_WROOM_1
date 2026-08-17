#include "bsp/gpba02b.h"
#include "codecs/no_audio_codec.h"
#include "config.h"
#include "display/lcd_display.h"
#include "display/st7365p_lcd_display.h"
#include "mcp_server.h"
#include "peripherals/keyboard.h"
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

#include <utility>

#define TAG "Esp32S3Wroom1N16r8Board"

static constexpr uint32_t kKeyboardTaskStackSize = 3072;
static constexpr UBaseType_t kKeyboardTaskPriority = 2;

static keyboard_config_t CreateKeyboardConfig() {
    keyboard_config_t config = {};
    keyboard_get_default_config(&config);
    config.up_key = {BUTTON_UP_IO_PORT, BUTTON_UP_IO_PIN};
    config.down_key = {BUTTON_DOWN_IO_PORT, BUTTON_DOWN_IO_PIN};
    config.active_low = true;
    config.poll_interval_ms = 10;
    return config;
}

class Esp32S3Wroom1N16r8Board : public WifiBoard {
private:
    esp_lcd_panel_io_handle_t panel_io_ = nullptr;
    esp_lcd_panel_handle_t panel_ = nullptr;
    Display* display_ = nullptr;
    keyboard_t keyboard_ = {};
    btn_scan_s keyboard_scan_ = {};
    TaskHandle_t keyboard_task_handle_ = nullptr;
    BoardKeyEventCallback key_event_callback_ = nullptr;

    static void KeyboardTaskEntry(void* arg) {
        auto* board = static_cast<Esp32S3Wroom1N16r8Board*>(arg);
        board->KeyboardTaskLoop();
    }

    void KeyboardTaskLoop() {
        while (true) {
            btn_status_e status =
                keyboard_scan_event(&keyboard_, &keyboard_scan_,
                                    static_cast<uint8_t>(keyboard_.config.poll_interval_ms));

            switch (status) {
                case Btn_Up_Click:
                    NotifyKeyEvent(0, BoardKeyEventType::Click);
                    break;
                case Btn_Down_Click:
                    NotifyKeyEvent(1, BoardKeyEventType::Click);
                    break;
                case Btn_Up_Hold_Enter:
                    NotifyKeyEvent(0, BoardKeyEventType::LongPress);
                    break;
                case Btn_Down_Hold_Enter:
                    NotifyKeyEvent(1, BoardKeyEventType::LongPress);
                    break;
                case Btn_Both_Click:
                    NotifyKeyEvent(0, BoardKeyEventType::Click);
                    NotifyKeyEvent(1, BoardKeyEventType::Click);
                    break;
                case Btn_Both_Hold_Enter:
                    NotifyKeyEvent(0, BoardKeyEventType::LongPress);
                    break;
                case Btn_Idle:
                case Btn_Up_Double:
                case Btn_Up_Hold_Continue:
                case Btn_Down_Double:
                case Btn_Down_Hold_Continue:
                case Btn_Both_Double:
                case Btn_Both_Hold_Continue:
                default:
                    break;
            }

            vTaskDelay(pdMS_TO_TICKS(keyboard_.config.poll_interval_ms));
        }
    }

    void NotifyKeyEvent(uint8_t key_index, BoardKeyEventType event_type) {
        if (key_event_callback_) {
            key_event_callback_(key_index, event_type);
            return;
        }

        ESP_LOGW(TAG, "Drop key event: key=%u type=%d (callback not set)", key_index,
                 static_cast<int>(event_type));
    }

    esp_err_t InitializeGpba02b() {
        gpba02b_config_t gpba02b_config = {};
        gpba02b_get_default_config(&gpba02b_config);
        gpba02b_config.spi_host = GPBA02B_SPI_HOST;
        gpba02b_config.miso_io = GPBA02B_IO_MISO;
        gpba02b_config.mosi_io = GPBA02B_IO_MOSI;
        gpba02b_config.sclk_io = GPBA02B_IO_CLK;
        gpba02b_config.cs_io = GPBA02B_IO_CS;
        gpba02b_config.clock_hz = GPBA02B_DEFAULT_CLOCK_HZ;
        gpba02b_config.device_id = GPBA02B_DEVICE_ID;

        esp_err_t err = gpba02b_init(&gpba02b_config);
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

        display_ = new St7365pLcdDisplay(panel_io_, panel_, LCD_DEFAULT_WIDTH, LCD_DEFAULT_HEIGHT,
                                         0, 0, mirror_x, mirror_y, swap_xy);
    }

    void InitializeBspEnv() {
        auto configure_output = [](gpba02b_port_t port, uint8_t pin, bool level) {
            ESP_ERROR_CHECK(
                gpba02b_config_io_output_mode(port, pin, GPBA02B_IO_OUTPUT_PUSH_PULL, level));
            ESP_ERROR_CHECK(gpba02b_write_io(port, pin, level));
        };

        configure_output(POWER_LOCK_IO_PORT, POWER_LOCK_IO_PIN, true);
        configure_output(PDM_EN_PORT, PDM_EN_PIN, true);
        configure_output(I2S_EN_PORT, I2S_EN_PIN, true);
        configure_output(PIDM_EN_PORT, PIDM_EN_PIN, true);
        configure_output(LCD_IO_RESET_PORT, LCD_IO_RESET_PIN, true);

#if defined(CAM_IO_RESET_PORT) && defined(CAM_IO_RESET_PIN) && defined(CAM_IO_PWDN_PORT) && \
    defined(CAM_IO_PWDN_PIN)
        configure_output(CAM_IO_RESET_PORT, CAM_IO_RESET_PIN, true);
        configure_output(CAM_IO_PWDN_PORT, CAM_IO_PWDN_PIN, true);
#if defined(CAM_IO_LIGHT_PORT) && defined(CAM_IO_LIGHT_PIN)
        configure_output(CAM_IO_LIGHT_PORT, CAM_IO_LIGHT_PIN, false);
#endif
#endif

#if defined(RC522_RST_PORT) && defined(RC522_RST_PIN)
        configure_output(RC522_RST_PORT, RC522_RST_PIN, true);
#endif

        ESP_ERROR_CHECK(
            gpba02b_pwm_set_clock_div(PWM_GPBA02B_PA_CLOCK_DIV, PWM_GPBA02B_PC_CLOCK_DIV));

        const uint8_t pwm_mask_a = static_cast<uint8_t>((1U << PWM_GPBA02B_07_PIN));
        const uint8_t pwm_mask_c = static_cast<uint8_t>(
            (1U << PWM_GPBA02B_08_PIN) | (1U << PWM_GPBA02B_09_PIN) | (1U << PWM_GPBA02B_10_PIN) |
            (1U << PWM_GPBA02B_11_PIN) | (1U << PWM_GPBA02B_12_PIN) | (1U << PWM_GPBA02B_13_PIN));

        for (uint8_t channel = 0; channel < 8; ++channel) {
            if ((pwm_mask_a & (1U << channel)) != 0) {
                ESP_ERROR_CHECK(gpba02b_pwm_set_channel_duty(GPBA02B_PORT_A, channel,
                                                             PWM_GPBA02B_DUTY_10_PERCENT));
            }
            if ((pwm_mask_c & (1U << channel)) != 0) {
                ESP_ERROR_CHECK(gpba02b_pwm_set_channel_duty(GPBA02B_PORT_C, channel,
                                                             PWM_GPBA02B_DUTY_10_PERCENT));
            }
        }
    }

    void InitializeKeyboard() {
        keyboard_config_t keyboard_config = CreateKeyboardConfig();
        ESP_ERROR_CHECK(keyboard_init_obj(&keyboard_, &keyboard_config));
        keyboard_scan_ = {};

        BaseType_t created = xTaskCreate(KeyboardTaskEntry, "keyboard_poll", kKeyboardTaskStackSize,
                                         this, kKeyboardTaskPriority, &keyboard_task_handle_);
        if (created != pdPASS) {
            ESP_LOGE(TAG, "Failed to create keyboard task");
            ESP_ERROR_CHECK(ESP_FAIL);
        }
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
                                                  I2S_DO_IO, I2S_STD_SLOT_BOTH, PDM_CLK_IO,
                                                  PDM_DATA_IO);
        return &audio_codec;
    }

    virtual Display* GetDisplay() override { return display_; }
};

DECLARE_BOARD(Esp32S3Wroom1N16r8Board);
