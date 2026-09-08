#include "access_assets.h"

extern const uint8_t butterfly_bin_start[] asm("_binary_butterfly_bin_start");
extern const uint8_t butterfly_bin_end[] asm("_binary_butterfly_bin_end");
extern const uint8_t cat_sit_bin_start[] asm("_binary_cat_sit_bin_start");
extern const uint8_t cat_sit_bin_end[] asm("_binary_cat_sit_bin_end");
extern const uint8_t goodluck_bin_start[] asm("_binary_goodluck_bin_start");
extern const uint8_t goodluck_bin_end[] asm("_binary_goodluck_bin_end");
extern const uint8_t landscape_bin_start[] asm("_binary_landscape_bin_start");
extern const uint8_t landscape_bin_end[] asm("_binary_landscape_bin_end");
extern const uint8_t lovable_dog_bin_start[] asm("_binary_lovable_dog_bin_start");
extern const uint8_t lovable_dog_bin_end[] asm("_binary_lovable_dog_bin_end");
extern const uint8_t maneki_neko_bin_start[] asm("_binary_maneki_neko_bin_start");
extern const uint8_t maneki_neko_bin_end[] asm("_binary_maneki_neko_bin_end");
extern const uint8_t panda_bin_start[] asm("_binary_panda_bin_start");
extern const uint8_t panda_bin_end[] asm("_binary_panda_bin_end");
extern const uint8_t robot_bin_start[] asm("_binary_robot_bin_start");
extern const uint8_t robot_bin_end[] asm("_binary_robot_bin_end");
extern const uint8_t run_rabbit_bin_start[] asm("_binary_run_rabbit_bin_start");
extern const uint8_t run_rabbit_bin_end[] asm("_binary_run_rabbit_bin_end");
extern const uint8_t thankyou_bin_start[] asm("_binary_thankyou_bin_start");
extern const uint8_t thankyou_bin_end[] asm("_binary_thankyou_bin_end");

static const access_assets_bin_item_t s_access_assets_bins[] = {
    { "butterfly.bin", butterfly_bin_start, butterfly_bin_end },
    { "cat_sit.bin", cat_sit_bin_start, cat_sit_bin_end },
    { "goodluck.bin", goodluck_bin_start, goodluck_bin_end },
    { "landscape.bin", landscape_bin_start, landscape_bin_end },
    { "lovable_dog.bin", lovable_dog_bin_start, lovable_dog_bin_end },
    { "maneki_neko.bin", maneki_neko_bin_start, maneki_neko_bin_end },
    { "panda.bin", panda_bin_start, panda_bin_end },
    { "robot.bin", robot_bin_start, robot_bin_end },
    { "run_rabbit.bin", run_rabbit_bin_start, run_rabbit_bin_end },
    { "thankyou.bin", thankyou_bin_start, thankyou_bin_end },
};

esp_err_t access_assets_get_bin(
    const char* bin_name,
    const uint8_t** out_data,
    size_t* out_data_len
)
{
    size_t idx = 0U;
    uintptr_t start_addr = 0U;
    uintptr_t end_addr = 0U;

    if ((bin_name == NULL) || (out_data == NULL) || (out_data_len == NULL)) {
        return ESP_ERR_INVALID_ARG;
    }

    *out_data = NULL;
    *out_data_len = 0U;

    for (idx = 0U;
         idx < (sizeof(s_access_assets_bins) / sizeof(s_access_assets_bins[0]));
         idx++) {
        if (strcmp(s_access_assets_bins[idx].bin_name, bin_name) != 0) {
            continue;
        }

        start_addr = (uintptr_t)s_access_assets_bins[idx].start;
        end_addr = (uintptr_t)s_access_assets_bins[idx].end;
        if (end_addr < start_addr) {
            return ESP_FAIL;
        }

        *out_data = s_access_assets_bins[idx].start;
        *out_data_len = (size_t)(end_addr - start_addr);
        if (*out_data_len == 0U) {
            *out_data = NULL;
            return ESP_ERR_NOT_FOUND;
        }

        return ESP_OK;
    }

    return ESP_ERR_NOT_FOUND;
}
