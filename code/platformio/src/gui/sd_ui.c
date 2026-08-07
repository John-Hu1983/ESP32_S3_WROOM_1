#include "sd_ui.h"

#define TAG "SD_APP"

static sd_app_ctx_t *s_sd_ctx = NULL;
static TaskHandle_t s_sd_input_task_handle = NULL;
static volatile bool s_sd_input_task_stop = false;
static volatile bool s_sd_input_home_requested = false;

/*
 * brief: Allocate SD app buffers from PSRAM first, then fallback to generic 8-bit heap.
 * input: bytes - requested byte count.
 * output: Allocated pointer on success; otherwise NULL.
 */
static void *_sd_app_psram_alloc(size_t bytes)
{
    void *ptr;

    if (bytes == 0U)
    {
        return NULL;
    }

    ptr = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (ptr != NULL)
    {
        return ptr;
    }

    return heap_caps_malloc(bytes, MALLOC_CAP_8BIT);
}

/*
 * brief: Resolve one fixed-size item-path slot by index.
 * input: ctx - SD app context; idx - item index.
 * output: Slot pointer on success; otherwise NULL.
 */
static char *_sd_app_item_path_slot(sd_app_ctx_t *ctx, uint16_t idx)
{
    if ((ctx == NULL) || (ctx->item_paths == NULL) || (idx >= SD_APP_MAX_ITEMS))
    {
        return NULL;
    }

    return ctx->item_paths + ((size_t)idx * USER_FS_PATH_MAX_LEN);
}

/*
 * brief: Get display prefix based on item type.
 * input: item_type - SD_APP_ITEM_FILE/SD_APP_ITEM_DIR/SD_APP_ITEM_PARENT.
 * output: Constant prefix text for UI rendering.
 */
static const char *_sd_app_item_prefix(uint8_t item_type)
{
    if (item_type == SD_APP_ITEM_DIR)
    {
        return "[DIR]";
    }

    if (item_type == SD_APP_ITEM_PARENT)
    {
        return "[UP ]";
    }

    return "[FILE]";
}

/*
 * brief: Apply visual state for one list item.
 * input: obj - target list item object; checked - true to mark selected.
 * output: None.
 */
static void _sd_app_set_checked_state(lv_obj_t *obj, bool checked)
{
    if (obj == NULL)
    {
        return;
    }

    if (checked)
    {
        if (!lv_obj_has_state(obj, LV_STATE_CHECKED))
        {
            lv_obj_add_state(obj, LV_STATE_CHECKED);
        }
    }
    else
    {
        if (lv_obj_has_state(obj, LV_STATE_CHECKED))
        {
            lv_obj_clear_state(obj, LV_STATE_CHECKED);
        }
    }
}

/*
 * brief: Update list button label text.
 * input: btn - LVGL list button; text - target text string.
 * output: None.
 */
static void _sd_app_set_btn_text(lv_obj_t *btn, const char *text)
{
    lv_obj_t *label;

    if ((btn == NULL) || (text == NULL))
    {
        return;
    }

    label = lv_obj_get_child(btn, 0);
    if (label == NULL)
    {
        return;
    }

    lv_label_set_text(label, text);
}

/*
 * brief: Apply unified style to one list button item.
 * input: btn - list item object.
 * output: None.
 */
static void _sd_app_style_item(lv_obj_t *btn)
{
    if (btn == NULL)
    {
        return;
    }

    lv_obj_set_style_bg_color(btn, lv_color_hex(APP_THEME_SURFACE_HEX), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(btn, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(btn, 4, LV_PART_MAIN);
    lv_obj_set_style_pad_left(btn, 6, LV_PART_MAIN);
    lv_obj_set_style_pad_right(btn, 6, LV_PART_MAIN);
    lv_obj_set_style_text_color(btn, lv_color_hex(APP_THEME_TEXT_PRIMARY_HEX), LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_set_style_bg_color(btn, lv_color_hex(APP_THEME_ACCENT_HEX), LV_PART_MAIN | LV_STATE_CHECKED);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_CHECKED);
    lv_obj_set_style_text_color(btn, lv_color_white(), LV_PART_MAIN | LV_STATE_CHECKED);

    lv_obj_set_style_bg_color(btn, lv_color_hex(APP_THEME_ACCENT_ACTIVE_HEX), LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_text_color(btn, lv_color_white(), LV_PART_MAIN | LV_STATE_PRESSED);
}

/*
 * brief: Refresh visible rows from current directory item cache.
 * input: ctx - SD app context.
 * output: None.
 */
static void _sd_app_refresh_visible_items(sd_app_ctx_t *ctx)
{
    uint16_t row;

    if ((ctx == NULL) || (ctx->visible_count == 0U))
    {
        return;
    }

    for (row = 0U; row < ctx->visible_count; row++)
    {
        lv_obj_t *btn;
        uint16_t item_idx;

        btn = ctx->item_btns[row];
        if (btn == NULL)
        {
            continue;
        }

        item_idx = (uint16_t)(ctx->view_start_idx + row);
        if (item_idx < ctx->item_count)
        {
            char *name_slot;
            char line_text[USER_FS_PATH_MAX_LEN + 16U];
            int n;

            name_slot = _sd_app_item_path_slot(ctx, item_idx);
            if (name_slot == NULL)
            {
                continue;
            }

            n = snprintf(line_text,
                         sizeof(line_text),
                         "%s %s",
                         _sd_app_item_prefix(ctx->item_type[item_idx]),
                         name_slot);
            if ((n <= 0) || ((size_t)n >= sizeof(line_text)))
            {
                line_text[0] = '\0';
            }

            _sd_app_set_btn_text(btn, line_text);
            lv_obj_clear_flag(btn, LV_OBJ_FLAG_HIDDEN);
            _sd_app_set_checked_state(btn, (item_idx == ctx->selected_idx));
        }
        else
        {
            _sd_app_set_btn_text(btn, "");
            _sd_app_set_checked_state(btn, false);
            lv_obj_add_flag(btn, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

/*
 * brief: Refresh selection and keep selected row in viewport.
 * input: ctx - SD app context; idx - selected item index.
 * output: None.
 */
static void _sd_app_select_item(sd_app_ctx_t *ctx, uint16_t idx)
{
    uint16_t row_idx;

    if ((ctx == NULL) || (ctx->item_count == 0U) || (idx >= ctx->item_count) || (ctx->visible_count == 0U))
    {
        return;
    }

    ctx->selected_idx = idx;
    if (idx < ctx->view_start_idx)
    {
        ctx->view_start_idx = idx;
    }
    else if (idx >= (uint16_t)(ctx->view_start_idx + ctx->visible_count))
    {
        ctx->view_start_idx = (uint16_t)(idx - ctx->visible_count + 1U);
    }

    _sd_app_refresh_visible_items(ctx);

    row_idx = (uint16_t)(ctx->selected_idx - ctx->view_start_idx);
    if ((row_idx < ctx->visible_count) && (ctx->item_btns[row_idx] != NULL))
    {
        lv_obj_scroll_to_view(ctx->item_btns[row_idx], LV_ANIM_OFF);
    }
}

/*
 * brief: Select previous row asynchronously on LVGL thread.
 * input: user_data - unused.
 * output: None.
 */
static void _sd_app_select_prev_async(void *user_data)
{
    uint16_t next_idx;

    (void)user_data;

    if ((s_sd_ctx == NULL) || (s_sd_ctx->item_count == 0U))
    {
        return;
    }

    if (s_sd_ctx->selected_idx == 0U)
    {
        next_idx = (uint16_t)(s_sd_ctx->item_count - 1U);
    }
    else
    {
        next_idx = (uint16_t)(s_sd_ctx->selected_idx - 1U);
    }

    _sd_app_select_item(s_sd_ctx, next_idx);
}

/*
 * brief: Select next row asynchronously on LVGL thread.
 * input: user_data - unused.
 * output: None.
 */
static void _sd_app_select_next_async(void *user_data)
{
    uint16_t next_idx;

    (void)user_data;

    if ((s_sd_ctx == NULL) || (s_sd_ctx->item_count == 0U))
    {
        return;
    }

    next_idx = (uint16_t)((s_sd_ctx->selected_idx + 1U) % s_sd_ctx->item_count);
    _sd_app_select_item(s_sd_ctx, next_idx);
}

/*
 * brief: Clear current directory item cache.
 * input: ctx - SD app context.
 * output: None.
 */
static void _sd_app_reset_items(sd_app_ctx_t *ctx)
{
    if (ctx == NULL)
    {
        return;
    }

    ctx->item_count = 0U;
    ctx->selected_idx = 0U;
    ctx->view_start_idx = 0U;
}

/*
 * brief: Append one entry into current item cache.
 * input: ctx - SD app context; item_type - file/dir/parent; name - entry name.
 * output: ESP_OK on success; otherwise error code.
 */
static esp_err_t _sd_app_append_entry_data(sd_app_ctx_t *ctx, uint8_t item_type, const char *name)
{
    char *path_slot;
    int n;

    if ((ctx == NULL) || (ctx->item_type == NULL) || (name == NULL))
    {
        return ESP_ERR_INVALID_ARG;
    }

    if (ctx->item_count >= SD_APP_MAX_ITEMS)
    {
        return ESP_ERR_NO_MEM;
    }

    path_slot = _sd_app_item_path_slot(ctx, ctx->item_count);
    if (path_slot == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    n = snprintf(path_slot, USER_FS_PATH_MAX_LEN, "%s", name);
    if ((n <= 0) || ((size_t)n >= USER_FS_PATH_MAX_LEN))
    {
        return ESP_ERR_INVALID_SIZE;
    }

    ctx->item_type[ctx->item_count] = item_type;
    ctx->item_count++;

    if ((ctx->item_count % SD_APP_SCAN_YIELD_EVERY_ITEMS) == 0U)
    {
        delay_ms(1U);
    }

    return ESP_OK;
}

/*
 * brief: Check whether path can be opened as directory.
 * input: root_path - absolute directory path.
 * output: true when open succeeds; otherwise false.
 */
static bool _sd_app_can_access_root(const char *root_path)
{
    DIR *dir;

    if ((root_path == NULL) || (root_path[0] == '\0'))
    {
        return false;
    }

    dir = opendir(root_path);
    if (dir == NULL)
    {
        return false;
    }

    closedir(dir);
    return true;
}

/*
 * brief: Select browser root under assets mount point.
 * input: out_root - output root path buffer; out_root_size - output buffer size.
 * output: ESP_OK on success; otherwise ESP_ERR_NOT_FOUND/INVALID_ARG/INVALID_SIZE.
 */
static esp_err_t _sd_app_select_root(char *out_root, size_t out_root_size)
{
    const char *storage_root;
    int n;

    if ((out_root == NULL) || (out_root_size == 0U))
    {
        return ESP_ERR_INVALID_ARG;
    }

    if (!usr_fs_is_ready())
    {
        return ESP_ERR_NOT_FOUND;
    }

    storage_root = usr_fs_mount_point();
    if ((storage_root == NULL) || (storage_root[0] == '\0') || !_sd_app_can_access_root(storage_root))
    {
        return ESP_ERR_NOT_FOUND;
    }

    n = snprintf(out_root, out_root_size, "%s", storage_root);
    if ((n <= 0) || ((size_t)n >= out_root_size))
    {
        return ESP_ERR_INVALID_SIZE;
    }

    return ESP_OK;
}

/*
 * brief: Build absolute path for current directory.
 * input: ctx - SD app context; out_path - output full path; out_path_size - output size.
 * output: ESP_OK on success; otherwise argument/size errors.
 */
static esp_err_t _sd_app_build_current_dir_path(sd_app_ctx_t *ctx, char *out_path, size_t out_path_size)
{
    int n;

    if ((ctx == NULL) || (out_path == NULL) || (out_path_size == 0U))
    {
        return ESP_ERR_INVALID_ARG;
    }

    if (ctx->source_root[0] == '\0')
    {
        return ESP_ERR_INVALID_STATE;
    }

    if (ctx->current_rel_dir[0] == '\0')
    {
        n = snprintf(out_path, out_path_size, "%s", ctx->source_root);
    }
    else
    {
        n = snprintf(out_path, out_path_size, "%s/%s", ctx->source_root, ctx->current_rel_dir);
    }

    if ((n <= 0) || ((size_t)n >= out_path_size))
    {
        return ESP_ERR_INVALID_SIZE;
    }

    return ESP_OK;
}

/*
 * brief: Trim current relative directory to its parent path.
 * input: ctx - SD app context.
 * output: None.
 */
static void _sd_app_enter_parent_dir(sd_app_ctx_t *ctx)
{
    char *slash;

    if ((ctx == NULL) || (ctx->current_rel_dir[0] == '\0'))
    {
        return;
    }

    slash = strrchr(ctx->current_rel_dir, '/');
    if (slash == NULL)
    {
        ctx->current_rel_dir[0] = '\0';
        return;
    }

    *slash = '\0';
}

/*
 * brief: Build file path for selected regular file.
 * input: ctx - SD app context; out_path - output full path; out_path_size - output buffer size.
 * output: true when selected row is a file and full path is generated.
 */
static bool _sd_app_get_selected_audio_path(sd_app_ctx_t *ctx, char *out_path, size_t out_path_size)
{
    char *name_slot;
    int n;

    if ((ctx == NULL) || (out_path == NULL) || (out_path_size == 0U))
    {
        return false;
    }

    if ((ctx->item_count == 0U) || (ctx->selected_idx >= ctx->item_count))
    {
        return false;
    }

    if (ctx->item_type[ctx->selected_idx] != SD_APP_ITEM_FILE)
    {
        return false;
    }

    name_slot = _sd_app_item_path_slot(ctx, ctx->selected_idx);
    if ((name_slot == NULL) || (name_slot[0] == '\0'))
    {
        return false;
    }

    if (ctx->current_rel_dir[0] == '\0')
    {
        n = snprintf(out_path, out_path_size, "%s/%s", ctx->source_root, name_slot);
    }
    else
    {
        n = snprintf(out_path,
                     out_path_size,
                     "%s/%s/%s",
                     ctx->source_root,
                     ctx->current_rel_dir,
                     name_slot);
    }

    if ((n <= 0) || ((size_t)n >= out_path_size))
    {
        return false;
    }

    if (!voice_path_is_supported(out_path))
    {
        return false;
    }

    return true;
}

/*
 * brief: Queue current selected audio file for voice playback.
 * input: None.
 * output: None.
 */
static void _sd_app_play_selected_audio(void)
{
    char full_path[USER_FS_PATH_MAX_LEN * 2U];
    esp_err_t ret;

    if (s_sd_ctx == NULL)
    {
        return;
    }

    if (!_sd_app_get_selected_audio_path(s_sd_ctx, full_path, sizeof(full_path)))
    {
        return;
    }

    ret = voice_load_file(full_path);
    if (ret != ESP_OK)
    {
        ESP_LOGW(TAG, "voice_load_file failed, path=%s err=%d", full_path, (int)ret);
    }
}

/*
 * brief: Enumerate current directory one level and refresh cached rows.
 * input: ctx - SD app context.
 * output: None.
 */
static void _sd_app_build_current_dir_data(sd_app_ctx_t *ctx)
{
    DIR *dir;
    struct dirent *entry;
    char full_dir[USER_FS_PATH_MAX_LEN * 2U];
    esp_err_t ret;

    if (ctx == NULL)
    {
        return;
    }

    _sd_app_reset_items(ctx);

    if (ctx->source_root[0] == '\0')
    {
        (void)_sd_app_append_entry_data(ctx, SD_APP_ITEM_FILE, "storage not ready");
        return;
    }

    if (ctx->current_rel_dir[0] != '\0')
    {
        (void)_sd_app_append_entry_data(ctx, SD_APP_ITEM_PARENT, "..");
    }

    ret = _sd_app_build_current_dir_path(ctx, full_dir, sizeof(full_dir));
    if (ret != ESP_OK)
    {
        (void)_sd_app_append_entry_data(ctx, SD_APP_ITEM_FILE, "path too long");
        return;
    }

    dir = opendir(full_dir);
    if (dir == NULL)
    {
        (void)_sd_app_append_entry_data(ctx, SD_APP_ITEM_FILE, "open dir failed");
        return;
    }

    while ((entry = readdir(dir)) != NULL)
    {
        char child_full[USER_FS_PATH_MAX_LEN * 2U];
        struct stat st;
        int n;

        if ((entry->d_name[0] == '.') &&
            ((entry->d_name[1] == '\0') ||
             ((entry->d_name[1] == '.') && (entry->d_name[2] == '\0'))))
        {
            continue;
        }

        n = snprintf(child_full, sizeof(child_full), "%s/%s", full_dir, entry->d_name);
        if ((n <= 0) || ((size_t)n >= sizeof(child_full)))
        {
            continue;
        }

        if (stat(child_full, &st) != 0)
        {
            continue;
        }

        if (S_ISDIR(st.st_mode))
        {
            ret = _sd_app_append_entry_data(ctx, SD_APP_ITEM_DIR, entry->d_name);
        }
        else if (S_ISREG(st.st_mode))
        {
            ret = _sd_app_append_entry_data(ctx, SD_APP_ITEM_FILE, entry->d_name);
        }
        else
        {
            ret = ESP_OK;
        }

        if (ret != ESP_OK)
        {
            break;
        }
    }

    closedir(dir);

    if (ret == ESP_ERR_NO_MEM)
    {
        if (ctx->item_count > 0U)
        {
            char *last_slot;
            last_slot = _sd_app_item_path_slot(ctx, (uint16_t)(ctx->item_count - 1U));
            if (last_slot != NULL)
            {
                (void)snprintf(last_slot, USER_FS_PATH_MAX_LEN, "... list truncated");
            }
            ctx->item_type[ctx->item_count - 1U] = SD_APP_ITEM_FILE;
        }
        return;
    }

    if (ctx->item_count == 0U)
    {
        (void)_sd_app_append_entry_data(ctx, SD_APP_ITEM_FILE, "(empty)");
    }
}

/*
 * brief: Enter selected row when holding any key.
 * input: user_data - unused.
 * output: None.
 */
static void _sd_app_enter_selected_async(void *user_data)
{
    uint8_t type;
    char *name_slot;
    char new_rel[USER_FS_PATH_MAX_LEN];
    int n;

    (void)user_data;

    if ((s_sd_ctx == NULL) || (s_sd_ctx->item_count == 0U) || (s_sd_ctx->selected_idx >= s_sd_ctx->item_count))
    {
        return;
    }

    type = s_sd_ctx->item_type[s_sd_ctx->selected_idx];
    if (type == SD_APP_ITEM_FILE)
    {
        _sd_app_play_selected_audio();
        return;
    }

    if (type == SD_APP_ITEM_PARENT)
    {
        _sd_app_enter_parent_dir(s_sd_ctx);
    }
    else if (type == SD_APP_ITEM_DIR)
    {
        name_slot = _sd_app_item_path_slot(s_sd_ctx, s_sd_ctx->selected_idx);
        if ((name_slot == NULL) || (name_slot[0] == '\0'))
        {
            return;
        }

        if (s_sd_ctx->current_rel_dir[0] == '\0')
        {
            n = snprintf(new_rel, sizeof(new_rel), "%s", name_slot);
        }
        else
        {
            n = snprintf(new_rel,
                         sizeof(new_rel),
                         "%s/%s",
                         s_sd_ctx->current_rel_dir,
                         name_slot);
        }

        if ((n <= 0) || ((size_t)n >= sizeof(new_rel)))
        {
            ESP_LOGW(TAG, "target path too long, stay current level");
            return;
        }

        (void)snprintf(s_sd_ctx->current_rel_dir, sizeof(s_sd_ctx->current_rel_dir), "%s", new_rel);
    }

    _sd_app_build_current_dir_data(s_sd_ctx);
    if (s_sd_ctx->item_count > 0U)
    {
        _sd_app_select_item(s_sd_ctx, 0U);
    }
    else
    {
        _sd_app_refresh_visible_items(s_sd_ctx);
    }
}

/*
 * brief: Create fixed list rows used as viewport into current level.
 * input: ctx - SD app context.
 * output: ESP_OK on success; otherwise ESP_ERR_INVALID_ARG/NO_MEM.
 */
static esp_err_t _sd_app_create_list_rows(sd_app_ctx_t *ctx)
{
    uint16_t i;

    if ((ctx == NULL) || (ctx->list == NULL))
    {
        return ESP_ERR_INVALID_ARG;
    }

    ctx->visible_count = SD_APP_VISIBLE_ITEM_COUNT;

    for (i = 0U; i < ctx->visible_count; i++)
    {
        lv_obj_t *btn;

        btn = lv_list_add_btn(ctx->list, NULL, "");
        if (btn == NULL)
        {
            return ESP_ERR_NO_MEM;
        }

        _sd_app_style_item(btn);
        lv_obj_clear_flag(btn, LV_OBJ_FLAG_CLICKABLE);
        ctx->item_btns[i] = btn;
    }

    return ESP_OK;
}

/*
 * brief: Input task for SD app key navigation and enter/back-home handling.
 * input: param - unused task parameter.
 * output: None.
 */
static void _sd_app_input_task(void *param)
{
    btn_scan_s btn;

    (void)param;
    lv_memset_00(&btn, sizeof(btn));
    s_sd_input_home_requested = false;

    while (!s_sd_input_task_stop)
    {
        btn_status_e btn_val;

        btn_val = keyboard_scan_event(&btn, SD_APP_INPUT_SCAN_PERIOD_MS);
        if (btn_val == Btn_Up_Click)
        {
            (void)lv_async_call(_sd_app_select_prev_async, NULL);
        }
        else if (btn_val == Btn_Down_Click)
        {
            (void)lv_async_call(_sd_app_select_next_async, NULL);
        }
        else if ((btn_val == Btn_Up_Hold) ||
                 (btn_val == Btn_Down_Hold) ||
                 (btn_val == Btn_Both_Hold))
        {
            (void)lv_async_call(_sd_app_enter_selected_async, NULL);
        }
        else if ((btn_val == Btn_Both_Click) && !s_sd_input_home_requested)
        {
            s_sd_input_home_requested = true;
            desktop_app_return_to_home();
        }

        delay_ms(SD_APP_INPUT_SCAN_PERIOD_MS);
    }

    s_sd_input_task_handle = NULL;
    vTaskDelete(NULL);
}

/*
 * brief: Start SD app input task.
 * input: None.
 * output: true on success; otherwise false.
 */
static bool _sd_app_start_input_task(void)
{
    BaseType_t task_ok;

    s_sd_input_task_stop = false;
    s_sd_input_home_requested = false;
    task_ok = xTaskCreate(_sd_app_input_task,
                          "sd_input",
                          SD_APP_INPUT_TASK_STACK_SIZE,
                          NULL,
                          SD_APP_INPUT_TASK_PRIORITY,
                          &s_sd_input_task_handle);
    if (task_ok != pdPASS)
    {
        ESP_LOGE(TAG, "xTaskCreate sd_input failed");
        s_sd_input_task_stop = true;
        s_sd_input_task_handle = NULL;
        return false;
    }

    return true;
}

/*
 * brief: Stop SD app input task and force delete on timeout.
 * input: None.
 * output: None.
 */
static void _sd_app_stop_input_task(void)
{
    TaskHandle_t handle;
    uint32_t wait_count;

    handle = s_sd_input_task_handle;
    if (handle == NULL)
    {
        return;
    }

    s_sd_input_task_stop = true;
    for (wait_count = 0U; wait_count < 20U; wait_count++)
    {
        if (s_sd_input_task_handle == NULL)
        {
            return;
        }

        delay_ms(5U);
    }

    handle = s_sd_input_task_handle;
    if (handle != NULL)
    {
        vTaskDelete(handle);
        s_sd_input_task_handle = NULL;
    }
}

/*
 * brief: Release SD app runtime context when screen object is deleted.
 * input: e - LVGL delete event.
 * output: None.
 */
static void _sd_app_delete_cb(lv_event_t *e)
{
    lv_obj_t *target;
    sd_app_ctx_t *ctx;

    target = lv_event_get_target(e);
    ctx = (sd_app_ctx_t *)lv_event_get_user_data(e);
    if (ctx == NULL)
    {
        return;
    }

    s_sd_input_task_stop = true;
    if (s_sd_input_task_handle != NULL)
    {
        vTaskDelete(s_sd_input_task_handle);
        s_sd_input_task_handle = NULL;
    }

    if ((target != NULL) && (s_sd_ctx == ctx) && (s_sd_ctx->screen == target))
    {
        s_sd_ctx = NULL;
    }

    if (ctx->item_paths != NULL)
    {
        heap_caps_free(ctx->item_paths);
        ctx->item_paths = NULL;
    }

    if (ctx->item_type != NULL)
    {
        heap_caps_free(ctx->item_type);
        ctx->item_type = NULL;
    }

    lv_mem_free(ctx);
}

/*
 * brief: Create SD filesystem browser screen.
 * input: lcd_w/lcd_h - active display resolution.
 * output: SD app screen object on success; otherwise NULL.
 */
lv_obj_t *sd_app_create_screen(lv_coord_t lcd_w, lv_coord_t lcd_h)
{
    sd_app_ctx_t *ctx;
    lv_obj_t *scr;
    lv_obj_t *list;
    lv_coord_t content_top;
    lv_coord_t content_bottom;
    lv_coord_t content_h;

    if ((lcd_w <= (2 * SD_APP_MARGIN_X)) || (lcd_h <= (2 * APP_STATUS_BAR_HEIGHT + 20)))
    {
        return NULL;
    }

    ctx = (sd_app_ctx_t *)lv_mem_alloc(sizeof(sd_app_ctx_t));
    if (ctx == NULL)
    {
        return NULL;
    }
    lv_memset_00(ctx, sizeof(sd_app_ctx_t));

    ctx->item_paths = (char *)_sd_app_psram_alloc((size_t)SD_APP_MAX_ITEMS * USER_FS_PATH_MAX_LEN);
    if (ctx->item_paths == NULL)
    {
        lv_mem_free(ctx);
        ESP_LOGE(TAG, "alloc item_paths failed");
        return NULL;
    }

    ctx->item_type = (uint8_t *)_sd_app_psram_alloc((size_t)SD_APP_MAX_ITEMS * sizeof(uint8_t));
    if (ctx->item_type == NULL)
    {
        heap_caps_free(ctx->item_paths);
        lv_mem_free(ctx);
        ESP_LOGE(TAG, "alloc item_type failed");
        return NULL;
    }

    lv_memset_00(ctx->item_type, (size_t)SD_APP_MAX_ITEMS * sizeof(uint8_t));
    ctx->current_rel_dir[0] = '\0';

    if (_sd_app_select_root(ctx->source_root, sizeof(ctx->source_root)) != ESP_OK)
    {
        ctx->source_root[0] = '\0';
    }

    scr = lv_obj_create(NULL);
    if (scr == NULL)
    {
        heap_caps_free(ctx->item_type);
        heap_caps_free(ctx->item_paths);
        lv_mem_free(ctx);
        return NULL;
    }

    lv_obj_set_style_bg_color(scr, lv_color_hex(APP_THEME_BG_HEX), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(scr, 0, 0);
    ctx->screen = scr;

    content_top = system_service_content_top();
    content_bottom = system_service_content_bottom();
    content_h = content_bottom - content_top;
    if (content_h < 40)
    {
        content_h = 40;
    }

    list = lv_list_create(scr);
    if (list == NULL)
    {
        lv_obj_del(scr);
        heap_caps_free(ctx->item_type);
        heap_caps_free(ctx->item_paths);
        lv_mem_free(ctx);
        return NULL;
    }

    lv_obj_set_size(list, lcd_w - (2 * SD_APP_MARGIN_X), content_h);
    lv_obj_set_pos(list, SD_APP_MARGIN_X, content_top);
    lv_obj_set_style_bg_color(list, lv_color_hex(APP_THEME_SURFACE_HEX), 0);
    lv_obj_set_style_bg_opa(list, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(list, 1, 0);
    lv_obj_set_style_border_color(list, lv_color_hex(APP_THEME_BORDER_HEX), 0);
    lv_obj_set_style_radius(list, 4, 0);
    lv_obj_set_style_pad_row(list, 2, 0);
    lv_obj_set_style_pad_all(list, 2, 0);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_OFF);
    ctx->list = list;

    if (_sd_app_create_list_rows(ctx) != ESP_OK)
    {
        lv_obj_del(scr);
        heap_caps_free(ctx->item_type);
        heap_caps_free(ctx->item_paths);
        lv_mem_free(ctx);
        ESP_LOGE(TAG, "create list rows failed");
        return NULL;
    }

    _sd_app_build_current_dir_data(ctx);
    if (ctx->item_count > 0U)
    {
        _sd_app_select_item(ctx, 0U);
    }
    else
    {
        _sd_app_refresh_visible_items(ctx);
    }

    if (!_sd_app_start_input_task())
    {
        lv_obj_del(scr);
        heap_caps_free(ctx->item_type);
        heap_caps_free(ctx->item_paths);
        lv_mem_free(ctx);
        return NULL;
    }

    lv_obj_add_event_cb(scr, _sd_app_delete_cb, LV_EVENT_DELETE, ctx);
    s_sd_ctx = ctx;
    return scr;
}

/*
 * brief: Stop SD app runtime resources before app is destroyed.
 * input: None.
 * output: None.
 */
void sd_app_release_resources(void)
{
    _sd_app_stop_input_task();
}

/*
 * brief: Request desktop return directly.
 * input: None.
 * output: None.
 */
void sd_app_destroy_and_return(void)
{
    desktop_app_return_to_home();
}
