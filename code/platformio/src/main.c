#include "main.h"

#define TAG "MAIN"

/*
 * brief: Main entry that initializes BSP/LCD, starts LVGL, and creates desktop UI task.
 * input: none.
 * output: none.
 */
void app_main(void)
{
	esp_err_t ret;

	ret = bsp_init_whole();
	if (ret != ESP_OK)
	{
		ESP_LOGE(TAG, "bsp_init_whole failed: %d", (int)ret);
		return;
	}

	ret = desktop_app_start();
	if (ret != ESP_OK)
	{
		ESP_LOGE(TAG, "desktop_app_start failed: %d", (int)ret);
		return;
	}
}