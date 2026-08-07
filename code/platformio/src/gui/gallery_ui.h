#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "desktop/desktop_app.h"
#include "service/system_service.h"
#include "bsp/delay.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "peripherals/keyboard.h"
#include "pictures/pic_common.h"

#define GALLERY_MARGIN_X 2
#define GALLERY_INPUT_SCAN_PERIOD_MS 10U
#define GALLERY_INPUT_TASK_STACK_SIZE 4096U
#define GALLERY_INPUT_TASK_PRIORITY 4U

typedef struct
{
    lv_obj_t *screen;
    lv_obj_t *view;
    lv_obj_t *img;
    lv_obj_t *hint_label;
    pic_item_t *items;
    size_t item_count;
    size_t selected_idx;
    size_t loaded_idx;
    char *path_pool;
    size_t path_pool_size;
} gallery_app_ctx_t;

/* Create gallery app screen and runtime resources. */
lv_obj_t *gallery_app_create_screen(lv_coord_t lcd_w, lv_coord_t lcd_h);
/* Release gallery runtime resources before returning home. */
void gallery_app_release_resources(void);
/* Request return-to-home navigation from gallery app. */
void gallery_app_destroy_and_return(void);
