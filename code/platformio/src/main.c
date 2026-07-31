#include "sdkconfig.h"

#include "esp_heap_caps.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_psram.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "bsp/bsp.h"
#include "peripherals/gpba02b.h"
#include "peripherals/st7365p.h"

#define TAG "MAIN"

static const uint16_t s_lcd_test_colors[] = {
	0xF800, // red
	0x07E0, // green
	0x001F, // blue
	0xFFE0, // yellow
	0xF81F, // magenta
	0x07FF, // cyan
	0xFFFF, // white
	0x0000	// black
};

void app_main(void)
{
	esp_err_t ret;
	uint16_t width = 0;
	uint16_t height = 0;
	uint32_t pixel_count;
	size_t color_idx = 0;
	size_t color_count = sizeof(s_lcd_test_colors) / sizeof(s_lcd_test_colors[0]);

	ret = bsp_init_whole();
	if (ret != ESP_OK)
	{
		ESP_LOGE(TAG, "bsp_init_whole failed: %d", (int)ret);
		return;
	}

	ret = st7365p_panel_init(NULL);
	if (ret != ESP_OK)
	{
		ESP_LOGE(TAG, "st7365p_panel_init failed: %d", (int)ret);
		return;
	}

	st7365p_get_resolution(&width, &height);
	if ((width == 0U) || (height == 0U))
	{
		ESP_LOGE(TAG, "invalid LCD resolution");
		return;
	}

	pixel_count = (uint32_t)width * (uint32_t)height;
	ESP_LOGI(TAG, "LCD color test start: %ux%u", (unsigned)width, (unsigned)height);

	while (1)
	{
		ret = st7365p_set_window(0, 0, (uint16_t)(width - 1U), (uint16_t)(height - 1U));
		if (ret == ESP_OK)
		{
			ret = st7365p_fill_color(s_lcd_test_colors[color_idx], pixel_count);
		}

		if (ret != ESP_OK)
		{
			ESP_LOGE(TAG, "LCD fill failed: %d", (int)ret);
		}

		color_idx++;
		if (color_idx >= color_count)
		{
			color_idx = 0;
		}

		vTaskDelay(pdMS_TO_TICKS(500));
	}
}