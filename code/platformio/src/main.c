#include "sdkconfig.h"

#include "esp_heap_caps.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_psram.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "hal/usr_spi.h"
#include "peripherals/gpba02b.h"
#include "bsp/bsp.h"

#define TAG "MAIN"

static void print_heap_info(void)
{
#if CONFIG_SPIRAM
	size_t psram_chip_bytes = esp_psram_get_size();
	size_t psram_heap_total = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
	size_t psram_heap_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);

	ESP_LOGI(TAG, "PSRAM chip: %u bytes (%.2f MB)", (unsigned)psram_chip_bytes,
			 (double)psram_chip_bytes / (1024.0 * 1024.0));
	ESP_LOGI(TAG, "PSRAM heap: total=%u, free=%u bytes",
			 (unsigned)psram_heap_total, (unsigned)psram_heap_free);
#else
	ESP_LOGW(TAG, "CONFIG_SPIRAM is disabled.");
#endif
}

void app_main(void)
{
	print_heap_info();
	gpba02b_init_device();
	bsp_power_on();

	gpba02b_pin_set_mode(LCD_IO_RESET_PORT, LCD_IO_RESET_PIN, GPBA02B_PIN_MODE_OUTPUT);
	while (1)
	{
		gpba02b_pin_write(LCD_IO_RESET_PORT, LCD_IO_RESET_PIN, true);
		vTaskDelay(1);
		gpba02b_pin_write(LCD_IO_RESET_PORT, LCD_IO_RESET_PIN, false);
		vTaskDelay(1);
	}
}