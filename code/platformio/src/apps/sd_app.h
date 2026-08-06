#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <dirent.h>
#include <sys/stat.h>

#include "desktop/desktop_app.h"
#include "app_status_bar.h"
#include "bsp/delay.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "filesystem/usr_fs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "peripherals/keyboard.h"
#include "voice/voice_common.h"

#define SD_APP_MAX_ITEMS 512U
#define SD_APP_MARGIN_X 2
#define SD_APP_INPUT_SCAN_PERIOD_MS 10U
#define SD_APP_INPUT_TASK_STACK_SIZE 4096U
#define SD_APP_INPUT_TASK_PRIORITY 4U
#define SD_APP_VISIBLE_ITEM_COUNT 12U
#define SD_APP_SCAN_YIELD_EVERY_ITEMS 16U

#define SD_APP_ITEM_FILE 0U
#define SD_APP_ITEM_DIR 1U
#define SD_APP_ITEM_PARENT 2U

typedef struct
{
	lv_obj_t *screen;
	lv_obj_t *list;
	lv_obj_t *item_btns[SD_APP_VISIBLE_ITEM_COUNT];
	char *item_paths;
	uint8_t *item_type;
	char source_root[USER_FS_PATH_MAX_LEN];
	char current_rel_dir[USER_FS_PATH_MAX_LEN];
	uint16_t item_count;
	uint16_t selected_idx;
	uint16_t view_start_idx;
	uint16_t visible_count;
} sd_app_ctx_t;

lv_obj_t *sd_app_create_screen(lv_coord_t lcd_w, lv_coord_t lcd_h);
void sd_app_release_resources(void);
void sd_app_destroy_and_return(void);
