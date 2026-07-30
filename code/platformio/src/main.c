#include "sdkconfig.h"

#include "esp_heap_caps.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_psram.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "hal/usr_spi.h"

#define TAG "MAIN"
#define SPI_TEST_INTERVAL_MS 5

usr_spi_s gpba02_spi;
void app_main(void)
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

	esp_err_t ret = spi_create_device(&gpba02_spi,
									  SPI2_HOST,
									  GPBA02_IO_MISO,
									  GPBA02_IO_MOSI,
									  GPBA02_IO_CLK,
									  GPBA02_IO_CS,
									  GPBA02_DEFAULT_CLOCK_HZ);
	if (ret != ESP_OK)
	{
		ESP_LOGE(TAG, "spi_create_device failed: %d", (int)ret);
		return;
	}

	const TickType_t interval_ticks_raw = pdMS_TO_TICKS(SPI_TEST_INTERVAL_MS);
	const TickType_t interval_ticks = (interval_ticks_raw == 0) ? 1 : interval_ticks_raw;
	uint8_t data[4] = {0x01, 0x02, 0x03, 0x04};
	while (1)
	{
		ret = spi_write_nbyte(&gpba02_spi, data, sizeof(data));
		if (ret != ESP_OK)
		{
			ESP_LOGE(TAG, "spi_write_nbyte failed: %d", (int)ret);
			vTaskDelay(pdMS_TO_TICKS(1000));
			continue;
		}

		vTaskDelay(interval_ticks);
	}
}