#include <driver/gpio.h>
#include <esp_err.h>
#include <esp_event.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <nvs.h>
#include <nvs_flash.h>

#include "application.h"
#include "user/peripherals/gpba02b.h"

#define TAG "main"

extern "C" void bsp_init_total(void) {
    ESP_LOGI(TAG, "Initializing BSP...");
    ESP_ERROR_CHECK(gpba02b_init_object());
    gpba02b_config_pwm_mode(GPBA02B_PORT_C, 1);
    gpba02b_set_pwm_frequency(GPBA02B_PORT_C, 1000);
    gpba02b_set_pwm_duty(GPBA02B_PORT_C, 1, 128);
}

extern "C" void app_main(void) {
    // Initialize NVS flash for WiFi configuration
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "Erasing NVS flash to fix corruption");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Initialize and run the application
    auto& app = Application::GetInstance();
    app.Initialize();

    bsp_init_total();

    app.Run();
}
