#include "rfid_ui.h"

#define TAG "rfid_ui"

static rfid_ui_runtime_s s_rfid_runtime;
static portMUX_TYPE s_rfid_lock = portMUX_INITIALIZER_UNLOCKED;
static char s_rfid_status_cache[RFID_UI_STATUS_TEXT_LEN];
static char s_rfid_uid_cache[RFID_UI_UID_TEXT_LEN];
static char s_rfid_dump_cache[RFID_UI_DUMP_TEXT_LEN];

/*
 * brief : _rfid_obj_valid.
 * input : see parameters.
 * output: return value from this function.
 * type  : private
 */
static bool _rfid_obj_valid(lv_obj_t* obj) { return (obj != NULL) && lv_obj_is_valid(obj); }

/*
 * brief : _rfid_set_status_text.
 * input : see parameters.
 * output: none.
 * type  : private
 */
static void _rfid_set_status_text(rfid_ui_runtime_s* runtime, const char* fmt, ...) {
    if ((runtime == NULL) || (fmt == NULL)) {
        return;
    }

    char local_text[RFID_UI_STATUS_TEXT_LEN];
    va_list args;
    va_start(args, fmt);
    vsnprintf(local_text, sizeof(local_text), fmt, args);
    va_end(args);

    taskENTER_CRITICAL(&s_rfid_lock);
    snprintf(runtime->status_text, sizeof(runtime->status_text), "%s", local_text);
    runtime->dirty = true;
    taskEXIT_CRITICAL(&s_rfid_lock);
}

/*
 * brief : _rfid_set_uid_text.
 * input : see parameters.
 * output: none.
 * type  : private
 */
static void _rfid_set_uid_text(rfid_ui_runtime_s* runtime, const char* fmt, ...) {
    if ((runtime == NULL) || (fmt == NULL)) {
        return;
    }

    char local_text[RFID_UI_UID_TEXT_LEN];
    va_list args;
    va_start(args, fmt);
    vsnprintf(local_text, sizeof(local_text), fmt, args);
    va_end(args);

    taskENTER_CRITICAL(&s_rfid_lock);
    snprintf(runtime->uid_text, sizeof(runtime->uid_text), "%s", local_text);
    runtime->dirty = true;
    taskEXIT_CRITICAL(&s_rfid_lock);
}

/*
 * brief : _rfid_clear_dump_text.
 * input : see parameters.
 * output: none.
 * type  : private
 */
static void _rfid_clear_dump_text(rfid_ui_runtime_s* runtime) {
    if (runtime == NULL) {
        return;
    }

    taskENTER_CRITICAL(&s_rfid_lock);
    snprintf(runtime->dump_text, sizeof(runtime->dump_text),
             "Place card near antenna...\nSectors 00-15 will be dumped after UID lock.");
    runtime->dirty = true;
    taskEXIT_CRITICAL(&s_rfid_lock);
}

/*
 * brief : _rfid_append_dump_text.
 * input : see parameters.
 * output: none.
 * type  : private
 */
static void _rfid_append_dump_text(rfid_ui_runtime_s* runtime, const char* fmt, ...) {
    if ((runtime == NULL) || (fmt == NULL)) {
        return;
    }

    char line[192];
    va_list args;
    va_start(args, fmt);
    vsnprintf(line, sizeof(line), fmt, args);
    va_end(args);

    taskENTER_CRITICAL(&s_rfid_lock);
    size_t used_len = strlen(runtime->dump_text);
    if (used_len < (sizeof(runtime->dump_text) - 1U)) {
        strncat(runtime->dump_text, line, sizeof(runtime->dump_text) - used_len - 1U);
    }
    runtime->dirty = true;
    taskEXIT_CRITICAL(&s_rfid_lock);
}

/*
 * brief : _rfid_sync_ui.
 * input : see parameters.
 * output: none.
 * type  : private
 */
static void _rfid_sync_ui(void* param) {
    rfid_ui_runtime_s* runtime = (rfid_ui_runtime_s*)param;
    if (runtime == NULL) {
        return;
    }

    if (!_rfid_obj_valid(runtime->status_label) || !_rfid_obj_valid(runtime->uid_label) ||
        !_rfid_obj_valid(runtime->dump_label)) {
        return;
    }

    bool dirty = false;
    taskENTER_CRITICAL(&s_rfid_lock);
    if (runtime->dirty) {
        snprintf(s_rfid_status_cache, sizeof(s_rfid_status_cache), "%s", runtime->status_text);
        snprintf(s_rfid_uid_cache, sizeof(s_rfid_uid_cache), "%s", runtime->uid_text);
        snprintf(s_rfid_dump_cache, sizeof(s_rfid_dump_cache), "%s", runtime->dump_text);
        runtime->dirty = false;
        dirty = true;
    }
    taskEXIT_CRITICAL(&s_rfid_lock);

    if (!dirty) {
        return;
    }

    lv_label_set_text(runtime->status_label, s_rfid_status_cache);
    lv_label_set_text(runtime->uid_label, s_rfid_uid_cache);
    lv_label_set_text(runtime->dump_label, s_rfid_dump_cache);
}

/*
 * brief : _rfid_request_ui_sync.
 * input : see parameters.
 * output: none.
 * type  : private
 */
static void _rfid_request_ui_sync(rfid_ui_runtime_s* runtime) {
    if (runtime == NULL) {
        return;
    }

    lv_lock();
    lv_result_t lv_res = lv_async_call(_rfid_sync_ui, runtime);
    lv_unlock();

    if (lv_res != LV_RESULT_OK) {
        ESP_LOGW(TAG, "lv_async_call failed in RFID UI");
    }
}

/*
 * brief : _rfid_uid_equal.
 * input : see parameters.
 * output: return value from this function.
 * type  : private
 */
static bool _rfid_uid_equal(const rfid_ui_runtime_s* runtime, const mfrc522_uid_t* uid) {
    if ((runtime == NULL) || (uid == NULL)) {
        return false;
    }
    if (runtime->last_uid_len != uid->size) {
        return false;
    }
    return memcmp(runtime->last_uid, uid->uid, uid->size) == 0;
}

/*
 * brief : _rfid_cache_uid.
 * input : see parameters.
 * output: none.
 * type  : private
 */
static void _rfid_cache_uid(rfid_ui_runtime_s* runtime, const mfrc522_uid_t* uid) {
    if ((runtime == NULL) || (uid == NULL)) {
        return;
    }

    runtime->last_uid_len = uid->size;
    if (runtime->last_uid_len > MFRC522_UID_MAX_LEN) {
        runtime->last_uid_len = MFRC522_UID_MAX_LEN;
    }
    memset(runtime->last_uid, 0, sizeof(runtime->last_uid));
    memcpy(runtime->last_uid, uid->uid, runtime->last_uid_len);
}

/*
 * brief : _rfid_format_uid_text.
 * input : see parameters.
 * output: none.
 * type  : private
 */
static void _rfid_format_uid_text(rfid_ui_runtime_s* runtime, const mfrc522_uid_t* uid) {
    if ((runtime == NULL) || (uid == NULL)) {
        return;
    }

    char uid_line[RFID_UI_UID_TEXT_LEN];
    size_t used_len = 0U;
    int print_len = snprintf(uid_line, sizeof(uid_line), "UID[%u]:", (unsigned)uid->size);
    if (print_len < 0) {
        return;
    }
    used_len = (size_t)print_len;
    if (used_len >= sizeof(uid_line)) {
        used_len = sizeof(uid_line) - 1U;
    }

    for (uint8_t i = 0; i < uid->size; i++) {
        if (used_len >= (sizeof(uid_line) - 1U)) {
            break;
        }

        print_len = snprintf(uid_line + used_len, sizeof(uid_line) - used_len, " %02X", uid->uid[i]);
        if (print_len < 0) {
            break;
        }

        if ((size_t)print_len >= (sizeof(uid_line) - used_len)) {
            used_len = sizeof(uid_line) - 1U;
        } else {
            used_len += (size_t)print_len;
        }
    }

    if (used_len < (sizeof(uid_line) - 1U)) {
        (void)snprintf(uid_line + used_len, sizeof(uid_line) - used_len, "  SAK:%02X", uid->sak);
    }

    _rfid_set_uid_text(runtime, "%s", uid_line);
}

/*
 * brief : _rfid_dump_card_sectors.
 * input : see parameters.
 * output: return value from this function.
 * type  : private
 */
static esp_err_t _rfid_dump_card_sectors(rfid_ui_runtime_s* runtime, const mfrc522_uid_t* uid) {
    if ((runtime == NULL) || (uid == NULL)) {
        return ESP_ERR_INVALID_ARG;
    }

    taskENTER_CRITICAL(&s_rfid_lock);
    runtime->dump_text[0] = '\0';
    runtime->dirty = true;
    taskEXIT_CRITICAL(&s_rfid_lock);

    _rfid_append_dump_text(runtime, "Card data dump (sector 00-15)\n");

    esp_err_t first_error = ESP_OK;
    for (uint8_t sector = 0; sector < RFID_UI_SECTOR_COUNT_TO_SHOW; sector++) {
        mfrc522_sector_data_t sector_data;
        esp_err_t ret = mfrc522_read_sector(sector, MFRC522_KEY_A, NULL, uid, &sector_data);
        if (ret != ESP_OK) {
            if (first_error == ESP_OK) {
                first_error = ret;
            }
            _rfid_append_dump_text(runtime, "S%02u: read/auth fail (%d)\n", (unsigned)sector,
                                   (int)ret);
            continue;
        }

        _rfid_append_dump_text(runtime, "S%02u\n", (unsigned)sector);
        for (uint8_t i = 0; i < sector_data.block_count; i++) {
            uint8_t block_index = (uint8_t)(sector_data.first_block + i);
            _rfid_append_dump_text(runtime, "  B%03u:", (unsigned)block_index);

            for (uint8_t j = 0; j < MFRC522_BLOCK_LEN; j++) {
                _rfid_append_dump_text(runtime, " %02X", sector_data.block_data[i][j]);
                if (j == 7U) {
                    _rfid_append_dump_text(runtime, " |");
                }
            }
            _rfid_append_dump_text(runtime, "\n");
        }
    }

    return first_error;
}

/*
 * brief : _rfid_reader_self_check.
 * input : see parameters.
 * output: return value from this function.
 * type  : private
 */
static esp_err_t _rfid_reader_self_check(rfid_ui_runtime_s* runtime) {
    if (runtime == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t version = 0U;
    esp_err_t ret = mfrc522_get_version(&version);
    if (ret != ESP_OK) {
        _rfid_set_status_text(runtime, "Self-check fail: version read err=%d", (int)ret);
        return ret;
    }

    runtime->reader_version = version;
    if ((version == 0x00U) || (version == 0xFFU)) {
        _rfid_set_status_text(runtime, "Self-check fail: version=0x%02X", version);
        return ESP_FAIL;
    }

    _rfid_set_status_text(runtime, "Reader ready, version=0x%02X", version);
    return ESP_OK;
}

/*
 * brief : _rfid_try_init_reader.
 * input : see parameters.
 * output: return value from this function.
 * type  : private
 */
static esp_err_t _rfid_try_init_reader(rfid_ui_runtime_s* runtime) {
    if (runtime == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t ret = mfrc522_init();
    if (ret != ESP_OK) {
        runtime->reader_ready = false;
        _rfid_set_status_text(runtime, "Reader init fail: %d", (int)ret);
        return ret;
    }

    ret = _rfid_reader_self_check(runtime);
    if (ret != ESP_OK) {
        runtime->reader_ready = false;
        (void)mfrc522_deinit();
        return ret;
    }

    runtime->reader_ready = true;
    runtime->card_present = false;
    runtime->last_uid_len = 0U;
    memset(runtime->last_uid, 0, sizeof(runtime->last_uid));

    _rfid_set_uid_text(runtime, "UID: --");
    _rfid_clear_dump_text(runtime);
    return ESP_OK;
}

/*
 * brief : _rfid_poll_card_and_refresh.
 * input : see parameters.
 * output: return value from this function.
 * type  : private
 */
static esp_err_t _rfid_poll_card_and_refresh(rfid_ui_runtime_s* runtime) {
    if (runtime == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    mfrc522_uid_t uid = {0};
    esp_err_t ret = mfrc522_read_uid(&uid);
    if (ret == ESP_ERR_NOT_FOUND) {
        if (runtime->card_present) {
            runtime->card_present = false;
            runtime->last_uid_len = 0U;
            memset(runtime->last_uid, 0, sizeof(runtime->last_uid));

            _rfid_set_status_text(runtime, "No card, waiting...");
            _rfid_set_uid_text(runtime, "UID: --");
            _rfid_clear_dump_text(runtime);
            _rfid_request_ui_sync(runtime);
        }
        return ESP_OK;
    }

    if (ret == ESP_ERR_INVALID_STATE) {
        runtime->reader_ready = false;
        runtime->card_present = false;
        runtime->last_uid_len = 0U;
        memset(runtime->last_uid, 0, sizeof(runtime->last_uid));

        _rfid_set_status_text(runtime, "Reader state error, reinit...");
        _rfid_set_uid_text(runtime, "UID: --");
        _rfid_clear_dump_text(runtime);
        (void)mfrc522_deinit();
        _rfid_request_ui_sync(runtime);
        return ret;
    }

    if (ret != ESP_OK) {
        _rfid_set_status_text(runtime, "UID read transient err: %d", (int)ret);
        _rfid_request_ui_sync(runtime);
        return ret;
    }

    if (runtime->card_present && _rfid_uid_equal(runtime, &uid)) {
        return ESP_OK;
    }

    runtime->card_present = true;
    _rfid_cache_uid(runtime, &uid);
    _rfid_format_uid_text(runtime, &uid);
    _rfid_set_status_text(runtime, "Card detected, reading sectors...");

    ret = _rfid_dump_card_sectors(runtime, &uid);
    if (ret == ESP_OK) {
        _rfid_set_status_text(runtime, "Card read OK, version=0x%02X", runtime->reader_version);
    } else {
        _rfid_set_status_text(runtime, "Card partial read, err=%d", (int)ret);
    }

    (void)mfrc522_halt();
    _rfid_request_ui_sync(runtime);
    return ret;
}

/*
 * brief : _rfid_ui_task.
 * input : see parameters.
 * output: none.
 * type  : private
 */
static void _rfid_ui_task(void* param) {
    rfid_ui_runtime_s* runtime = (rfid_ui_runtime_s*)param;
    if (runtime == NULL) {
        vTaskDelete(NULL);
        return;
    }

    uint32_t poll_elapsed_ms = RFID_UI_POLL_PERIOD_MS;
    uint32_t retry_elapsed_ms = RFID_UI_RETRY_INIT_MS;

    /* Initialize and self-check reader before entering the loop. */
    (void)_rfid_try_init_reader(runtime);
    _rfid_request_ui_sync(runtime);

    while (1) {
        btn_status_e btn_val = button_scan_state(&runtime->button_scan, RFID_UI_TASK_PERIOD_MS);
        if ((btn_val == Btn_Both_Click) && (runtime->home_cb != NULL)) {
            runtime->home_cb(runtime->home_user_ctx);
        }

        if (!runtime->reader_ready) {
            retry_elapsed_ms += RFID_UI_TASK_PERIOD_MS;
            if (retry_elapsed_ms >= RFID_UI_RETRY_INIT_MS) {
                retry_elapsed_ms = 0U;
                if (_rfid_try_init_reader(runtime) == ESP_OK) {
                    poll_elapsed_ms = RFID_UI_POLL_PERIOD_MS;
                }
                _rfid_request_ui_sync(runtime);
            }
        } else {
            poll_elapsed_ms += RFID_UI_TASK_PERIOD_MS;
            if (poll_elapsed_ms >= RFID_UI_POLL_PERIOD_MS) {
                poll_elapsed_ms = 0U;
                (void)_rfid_poll_card_and_refresh(runtime);
            }
        }

        delay_ms(RFID_UI_TASK_PERIOD_MS);
    }
}

/*
 * brief : Create the RFID page root screen object.
 * input : lcd_w - LCD width; lcd_h - LCD height.
 * output: Created LVGL screen object.
 * type  : public
 */
lv_obj_t* rfid_create_screen(lv_obj_t* parent, lv_coord_t area_w, lv_coord_t area_h,
                             ui_menu_home_cb_t home_cb, void* home_user_ctx) {
    if (parent == NULL) {
        return NULL;
    }

    if (s_rfid_runtime.task_handle != NULL) {
        vTaskDelete(s_rfid_runtime.task_handle);
        s_rfid_runtime.task_handle = NULL;
    }

    /* Defensive cleanup for repeated enter/leave cycles. */
    (void)mfrc522_deinit();

    memset(&s_rfid_runtime, 0, sizeof(s_rfid_runtime));

    s_rfid_runtime.home_cb = home_cb;
    s_rfid_runtime.home_user_ctx = home_user_ctx;
    s_rfid_runtime.button_scan = (btn_scan_s){0};

    lv_obj_t* screen = lv_obj_create(parent);
    lv_obj_set_size(screen, area_w, area_h);
    lv_obj_set_pos(screen, 0, 0);
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x1C1B1A), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(screen, 0, 0);
    lv_obj_set_style_radius(screen, 0, 0);
    lv_obj_set_style_pad_all(screen, 4, 0);

    lv_obj_t* title_label = lv_label_create(screen);
    lv_label_set_text(title_label, "RFID Reader");
    lv_obj_set_style_text_color(title_label, lv_color_hex(0xDCE7EF), 0);
    lv_obj_set_style_text_font(title_label, &lv_font_montserrat_14, 0);
    lv_obj_align(title_label, LV_ALIGN_TOP_LEFT, 0, 0);

    s_rfid_runtime.status_label = lv_label_create(screen);
    lv_label_set_text(s_rfid_runtime.status_label, "Initializing...");
    lv_obj_set_style_text_color(s_rfid_runtime.status_label, lv_color_hex(0x8ED2FF), 0);
    lv_obj_set_style_text_font(s_rfid_runtime.status_label, &lv_font_montserrat_14, 0);
    lv_obj_align(s_rfid_runtime.status_label, LV_ALIGN_TOP_LEFT, 0, 18);

    s_rfid_runtime.uid_label = lv_label_create(screen);
    lv_label_set_text(s_rfid_runtime.uid_label, "UID: --");
    lv_obj_set_style_text_color(s_rfid_runtime.uid_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(s_rfid_runtime.uid_label, &lv_font_montserrat_14, 0);
    lv_obj_align(s_rfid_runtime.uid_label, LV_ALIGN_TOP_LEFT, 0, 36);

    lv_coord_t panel_h = (lv_coord_t)(area_h - 58);
    if (panel_h < 40) {
        panel_h = 40;
    }

    s_rfid_runtime.dump_panel = lv_obj_create(screen);
    lv_obj_set_size(s_rfid_runtime.dump_panel, lv_pct(100), panel_h);
    lv_obj_align(s_rfid_runtime.dump_panel, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(s_rfid_runtime.dump_panel, lv_color_hex(0x202326), 0);
    lv_obj_set_style_bg_opa(s_rfid_runtime.dump_panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(s_rfid_runtime.dump_panel, lv_color_hex(0x46505A), 0);
    lv_obj_set_style_border_width(s_rfid_runtime.dump_panel, 1, 0);
    lv_obj_set_style_radius(s_rfid_runtime.dump_panel, 4, 0);
    lv_obj_set_style_pad_all(s_rfid_runtime.dump_panel, 4, 0);

    s_rfid_runtime.dump_label = lv_label_create(s_rfid_runtime.dump_panel);
    lv_obj_set_width(s_rfid_runtime.dump_label, lv_pct(100));
    lv_label_set_long_mode(s_rfid_runtime.dump_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(s_rfid_runtime.dump_label, lv_color_hex(0xD7DBE0), 0);
    lv_obj_set_style_text_font(s_rfid_runtime.dump_label, &lv_font_montserrat_14, 0);
    lv_obj_align(s_rfid_runtime.dump_label, LV_ALIGN_TOP_LEFT, 0, 0);

    s_rfid_runtime.root = screen;
    _rfid_set_status_text(&s_rfid_runtime, "Initializing reader...");
    _rfid_set_uid_text(&s_rfid_runtime, "UID: --");
    _rfid_clear_dump_text(&s_rfid_runtime);
    _rfid_request_ui_sync(&s_rfid_runtime);

    BaseType_t task_ok = xTaskCreate(_rfid_ui_task, "rfid_ui", RFID_UI_TASK_STACK_SIZE,
                                     &s_rfid_runtime, 5, &s_rfid_runtime.task_handle);
    if (task_ok != pdPASS) {
        s_rfid_runtime.task_handle = NULL;
        s_rfid_runtime.root = NULL;
        s_rfid_runtime.status_label = NULL;
        s_rfid_runtime.uid_label = NULL;
        s_rfid_runtime.dump_panel = NULL;
        s_rfid_runtime.dump_label = NULL;
        lv_obj_del(screen);
        ESP_LOGE(TAG, "xTaskCreate failed");
        return NULL;
    }

    return screen;
}

/*
 * brief : rfid_destroy_screen.
 * input : see parameters.
 * output: none.
 * type  : public
 */
void rfid_destroy_screen(lv_obj_t* screen) {
    if (s_rfid_runtime.task_handle != NULL) {
        vTaskDelete(s_rfid_runtime.task_handle);
        s_rfid_runtime.task_handle = NULL;
    }

    esp_err_t ret = mfrc522_deinit();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "mfrc522_deinit failed: %d", (int)ret);
    }

    s_rfid_runtime.home_cb = NULL;
    s_rfid_runtime.home_user_ctx = NULL;
    s_rfid_runtime.button_scan = (btn_scan_s){0};
    s_rfid_runtime.reader_ready = false;
    s_rfid_runtime.card_present = false;
    s_rfid_runtime.last_uid_len = 0U;
    s_rfid_runtime.root = NULL;
    s_rfid_runtime.status_label = NULL;
    s_rfid_runtime.uid_label = NULL;
    s_rfid_runtime.dump_panel = NULL;
    s_rfid_runtime.dump_label = NULL;
    s_rfid_runtime.status_text[0] = '\0';
    s_rfid_runtime.uid_text[0] = '\0';
    s_rfid_runtime.dump_text[0] = '\0';
    memset(s_rfid_runtime.last_uid, 0, sizeof(s_rfid_runtime.last_uid));

    if ((screen != NULL) && lv_obj_is_valid(screen)) {
        lv_obj_del(screen);
    }
}
