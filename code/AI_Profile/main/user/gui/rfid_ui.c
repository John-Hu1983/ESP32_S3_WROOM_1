#include "rfid_ui.h"

#define TAG "rfid_ui"

static rfid_ui_runtime_s s_rfid_runtime;
static portMUX_TYPE s_rfid_lock = portMUX_INITIALIZER_UNLOCKED;
static char s_rfid_status_cache[RFID_UI_STATUS_TEXT_LEN];
static char s_rfid_uid_cache[RFID_UI_UID_TEXT_LEN];
static char s_rfid_dump_cache[RFID_UI_DUMP_TEXT_LEN];
static uint8_t s_rfid_sector_cache;
static bool s_rfid_card_present_cache;
static bool s_rfid_card_error_cache;
static dev_rfid_snapshot_s s_rfid_snapshot_cache;

/*
 * brief : _rfid_ui_timer_cb.
 * input : see parameters.
 * output: none.
 * type  : private
 */
static void _rfid_ui_timer_cb(lv_timer_t* timer);

/*
 * brief : _rfid_obj_valid.
 * input : see parameters.
 * output: return value from this function.
 * type  : private
 */
static bool _rfid_obj_valid(lv_obj_t* obj)
{
    return (obj != NULL) && lv_obj_is_valid(obj);
}

/*
 * brief : _rfid_clamp_sector.
 * input : see parameters.
 * output: return value from this function.
 * type  : private
 */
static uint8_t _rfid_clamp_sector(uint8_t sector)
{
    if (sector > DEV_RFID_VIEW_SECTOR_MAX) {
        return DEV_RFID_VIEW_SECTOR_MAX;
    }

    return sector;
}

/*
 * brief : _rfid_next_sector.
 * input : see parameters.
 * output: return value from this function.
 * type  : private
 */
static uint8_t _rfid_next_sector(uint8_t current_sector, bool increase)
{
    uint8_t sector = _rfid_clamp_sector(current_sector);
    if (increase) {
        return (sector >= DEV_RFID_VIEW_SECTOR_MAX) ? DEV_RFID_VIEW_SECTOR_MIN
                                                    : (uint8_t)(sector + 1U);
    }

    return (sector <= DEV_RFID_VIEW_SECTOR_MIN) ? DEV_RFID_VIEW_SECTOR_MAX : (uint8_t)(sector - 1U);
}

/*
 * brief : _rfid_apply_indicator_style.
 * input : see parameters.
 * output: none.
 * type  : private
 */
static void _rfid_apply_indicator_style(lv_obj_t* indicator, bool card_present, bool card_error)
{
    if (!_rfid_obj_valid(indicator)) {
        return;
    }

    lv_obj_set_style_radius(indicator, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_shadow_width(indicator, 0, 0);
    lv_obj_set_style_shadow_opa(indicator, LV_OPA_TRANSP, 0);

    if (card_error) {
        lv_obj_set_style_bg_color(indicator, lv_color_hex(0xF6475B), 0);
        lv_obj_set_style_bg_opa(indicator, LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(indicator, lv_color_hex(0xF6475B), 0);
        lv_obj_set_style_border_width(indicator, 2, 0);
        lv_obj_set_style_shadow_color(indicator, lv_color_hex(0xF6475B), 0);
        lv_obj_set_style_shadow_width(indicator, 12, 0);
        lv_obj_set_style_shadow_opa(indicator, LV_OPA_60, 0);
        return;
    }

    if (card_present) {
        lv_obj_set_style_bg_color(indicator, lv_color_hex(0x49EA6A), 0);
        lv_obj_set_style_bg_opa(indicator, LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(indicator, lv_color_hex(0x1FAE45), 0);
        lv_obj_set_style_border_width(indicator, 2, 0);
        lv_obj_set_style_shadow_color(indicator, lv_color_hex(0x42FF6D), 0);
        lv_obj_set_style_shadow_width(indicator, 12, 0);
        lv_obj_set_style_shadow_opa(indicator, LV_OPA_60, 0);
        return;
    }

    lv_obj_set_style_bg_color(indicator, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(indicator, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(indicator, lv_color_white(), 0);
    lv_obj_set_style_border_width(indicator, 2, 0);
}

/*
 * brief : _rfid_set_status_text.
 * input : see parameters.
 * output: none.
 * type  : private
 */
static void _rfid_set_status_text(rfid_ui_runtime_s* runtime, const char* fmt, ...)
{
    char local_text[RFID_UI_STATUS_TEXT_LEN];
    va_list args;

    if ((runtime == NULL) || (fmt == NULL)) {
        return;
    }

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
static void _rfid_set_uid_text(rfid_ui_runtime_s* runtime, const char* fmt, ...)
{
    char local_text[RFID_UI_UID_TEXT_LEN];
    va_list args;

    if ((runtime == NULL) || (fmt == NULL)) {
        return;
    }

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
static void _rfid_clear_dump_text(rfid_ui_runtime_s* runtime)
{
    uint8_t sector = DEV_RFID_VIEW_SECTOR_MIN;

    if (runtime == NULL) {
        return;
    }

    sector = _rfid_clamp_sector(runtime->selected_sector);

    taskENTER_CRITICAL(&s_rfid_lock);
    snprintf(
        runtime->dump_text,
        sizeof(runtime->dump_text),
        "Place card near antenna...\n"
        "UP click: sector +1, DOWN click: sector -1.\n"
        "Current sector: S%02u",
        (unsigned)sector
    );
    runtime->dirty = true;
    runtime->dump_need_scroll_top = true;
    taskEXIT_CRITICAL(&s_rfid_lock);
}

/*
 * brief : _rfid_sync_ui.
 * input : see parameters.
 * output: none.
 * type  : private
 */
static void _rfid_sync_ui(void* param)
{
    rfid_ui_runtime_s* runtime = (rfid_ui_runtime_s*)param;
    bool dirty = false;
    bool scroll_top = false;
    char sector_text[64] = { 0 };

    if (runtime == NULL) {
        return;
    }

    if (!_rfid_obj_valid(runtime->status_label) || !_rfid_obj_valid(runtime->uid_label)
        || !_rfid_obj_valid(runtime->sector_label) || !_rfid_obj_valid(runtime->state_indicator)
        || !_rfid_obj_valid(runtime->dump_label)) {
        return;
    }

    taskENTER_CRITICAL(&s_rfid_lock);
    if (runtime->dirty) {
        snprintf(s_rfid_status_cache, sizeof(s_rfid_status_cache), "%s", runtime->status_text);
        snprintf(s_rfid_uid_cache, sizeof(s_rfid_uid_cache), "%s", runtime->uid_text);
        snprintf(s_rfid_dump_cache, sizeof(s_rfid_dump_cache), "%s", runtime->dump_text);
        s_rfid_sector_cache = _rfid_clamp_sector(runtime->selected_sector);
        s_rfid_card_present_cache = runtime->card_present;
        s_rfid_card_error_cache = runtime->card_error;
        runtime->dirty = false;
        scroll_top = runtime->dump_need_scroll_top;
        runtime->dump_need_scroll_top = false;
        dirty = true;
    }
    taskEXIT_CRITICAL(&s_rfid_lock);

    if (!dirty) {
        return;
    }

    lv_label_set_text(runtime->status_label, s_rfid_status_cache);
    lv_label_set_text(runtime->uid_label, s_rfid_uid_cache);
    (void)snprintf(
        sector_text,
        sizeof(sector_text),
        "Sector: S%02u    UP:+1  DOWN:-1",
        (unsigned)s_rfid_sector_cache
    );
    lv_label_set_text(runtime->sector_label, sector_text);

    _rfid_apply_indicator_style(
        runtime->state_indicator, s_rfid_card_present_cache, s_rfid_card_error_cache
    );
    lv_label_set_text(runtime->dump_label, s_rfid_dump_cache);
    if (scroll_top && _rfid_obj_valid(runtime->dump_panel)) {
        lv_obj_scroll_to_y(runtime->dump_panel, 0, LV_ANIM_OFF);
    }
}

/*
 * brief : _rfid_ui_timer_cb.
 * input : see parameters.
 * output: none.
 * type  : private
 */
static void _rfid_ui_timer_cb(lv_timer_t* timer)
{
    rfid_ui_runtime_s* runtime = NULL;

    if (timer == NULL) {
        return;
    }

    runtime = (rfid_ui_runtime_s*)lv_timer_get_user_data(timer);
    _rfid_sync_ui((void*)runtime);
}

/*
 * brief : _rfid_request_ui_sync.
 * input : see parameters.
 * output: none.
 * type  : private
 */
static void _rfid_request_ui_sync(rfid_ui_runtime_s* runtime)
{
    if (runtime == NULL) {
        return;
    }
}

/*
 * brief : _rfid_apply_snapshot.
 * input : see parameters.
 * output: none.
 * type  : private
 */
static void _rfid_apply_snapshot(rfid_ui_runtime_s* runtime, const dev_rfid_snapshot_s* snapshot)
{
    if ((runtime == NULL) || (snapshot == NULL)) {
        return;
    }

    taskENTER_CRITICAL(&s_rfid_lock);
    runtime->reader_ready = snapshot->reader_ready;
    runtime->card_present = snapshot->card_present;
    runtime->reader_version = snapshot->reader_version;
    runtime->selected_sector = dev_rfid_get_view_sector();
    snprintf(runtime->status_text, sizeof(runtime->status_text), "%s", snapshot->status_text);
    snprintf(runtime->uid_text, sizeof(runtime->uid_text), "%s", snapshot->uid_text);
    snprintf(runtime->dump_text, sizeof(runtime->dump_text), "%s", snapshot->dump_text);
    runtime->dump_need_scroll_top = snapshot->dump_need_scroll_top;
    runtime->dirty = true;
    taskEXIT_CRITICAL(&s_rfid_lock);
}

/*
 * brief : _rfid_set_card_error.
 * input : see parameters.
 * output: none.
 * type  : private
 */
static void _rfid_set_card_error(rfid_ui_runtime_s* runtime, bool card_error)
{
    if (runtime == NULL) {
        return;
    }

    taskENTER_CRITICAL(&s_rfid_lock);
    runtime->card_error = card_error;
    runtime->dirty = true;
    taskEXIT_CRITICAL(&s_rfid_lock);
}

/*
 * brief : _rfid_step_sector.
 * input : see parameters.
 * output: none.
 * type  : private
 */
static void _rfid_step_sector(rfid_ui_runtime_s* runtime, bool increase)
{
    bool no_card = false;
    uint8_t next_sector = DEV_RFID_VIEW_SECTOR_MIN;

    if (runtime == NULL) {
        return;
    }

    taskENTER_CRITICAL(&s_rfid_lock);
    next_sector = _rfid_next_sector(runtime->selected_sector, increase);
    runtime->selected_sector = next_sector;
    runtime->dump_need_scroll_top = true;
    runtime->dirty = true;
    no_card = !runtime->card_present;
    taskEXIT_CRITICAL(&s_rfid_lock);

    (void)dev_rfid_set_view_sector(next_sector);
    if (no_card) {
        _rfid_clear_dump_text(runtime);
    }
    _rfid_set_status_text(runtime, "Selected sector: S%02u", (unsigned)next_sector);
}

/*
 * brief : _rfid_reader_self_check.
 * input : see parameters.
 * output: return value from this function.
 * type  : private
 */
static esp_err_t _rfid_reader_self_check(rfid_ui_runtime_s* runtime)
{
    uint8_t version = 0U;
    esp_err_t ret = ESP_FAIL;

    if (runtime == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    ret = RFID_get_reader_version(&version);
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
static esp_err_t _rfid_try_init_reader(rfid_ui_runtime_s* runtime)
{
    esp_err_t ret = ESP_FAIL;

    if (runtime == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    ret = RFID_init();
    if (ret != ESP_OK) {
        runtime->reader_ready = false;
        runtime->card_error = true;
        _rfid_set_status_text(runtime, "Reader init fail: %d", (int)ret);
        return ret;
    }

    ret = _rfid_reader_self_check(runtime);
    if (ret != ESP_OK) {
        runtime->reader_ready = false;
        runtime->card_error = true;
        (void)RFID_deinit();
        return ret;
    }

    (void)dev_rfid_set_view_sector(_rfid_clamp_sector(runtime->selected_sector));

    runtime->reader_ready = true;
    runtime->card_present = false;
    runtime->card_error = false;

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
static esp_err_t _rfid_poll_card_and_refresh(rfid_ui_runtime_s* runtime)
{
    esp_err_t ret = ESP_FAIL;

    if (runtime == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    ret = dev_rfid_get_snapshot(&s_rfid_snapshot_cache);
    _rfid_apply_snapshot(runtime, &s_rfid_snapshot_cache);
    _rfid_set_card_error(runtime, (ret != ESP_OK));

    if (ret == ESP_ERR_INVALID_STATE) {
        (void)RFID_deinit();
    }

    _rfid_request_ui_sync(runtime);
    return ret;
}

/*
 * brief : _rfid_ui_task.
 * input : see parameters.
 * output: none.
 * type  : private
 */
static void _rfid_ui_task(void* param)
{
    rfid_ui_runtime_s* runtime = (rfid_ui_runtime_s*)param;
    uint32_t poll_ms = RFID_UI_POLL_PERIOD_MS;
    uint32_t retry_ms = RFID_UI_RETRY_INIT_MS;
    btn_status_e btn_val = Btn_Idle;
    bool ready = false;

    if (runtime == NULL) {
        vTaskDelete(NULL);
        return;
    }

    /* Initialize and self-check reader before entering the loop. */
    (void)_rfid_try_init_reader(runtime);
    _rfid_request_ui_sync(runtime);

    while (1) {
        btn_val = button_scan_state(&runtime->button_scan, RFID_UI_TASK_PERIOD_MS);
        if ((btn_val == Btn_Both_Click) && (runtime->home_cb != NULL)) {
            runtime->home_cb(runtime->home_user_ctx);
        } else if (btn_val == Btn_Up_Click) {
            _rfid_step_sector(runtime, true);
            poll_ms = RFID_UI_POLL_PERIOD_MS;
            _rfid_request_ui_sync(runtime);
        } else if (btn_val == Btn_Down_Click) {
            _rfid_step_sector(runtime, false);
            poll_ms = RFID_UI_POLL_PERIOD_MS;
            _rfid_request_ui_sync(runtime);
        }

        ready = runtime->reader_ready;
        if (!ready) {
            retry_ms += RFID_UI_TASK_PERIOD_MS;
            if (retry_ms >= RFID_UI_RETRY_INIT_MS) {
                retry_ms = 0U;
                if (_rfid_try_init_reader(runtime) == ESP_OK) {
                    poll_ms = RFID_UI_POLL_PERIOD_MS;
                }
                _rfid_request_ui_sync(runtime);
            }
        }

        if (ready) {
            poll_ms += RFID_UI_TASK_PERIOD_MS;
            if (poll_ms >= RFID_UI_POLL_PERIOD_MS) {
                poll_ms = 0U;
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
lv_obj_t* rfid_create_screen(
    lv_obj_t* parent,
    lv_coord_t area_w,
    lv_coord_t area_h,
    ui_menu_home_cb_t home_cb,
    void* home_user_ctx
)
{
    lv_obj_t* screen = NULL;
    lv_obj_t* frame = NULL;
    lv_obj_t* hdr_panel = NULL;
    lv_obj_t* title_label = NULL;
    lv_obj_t* info_panel = NULL;
    lv_obj_t* ctrl_panel = NULL;
    lv_coord_t usable_h = 0;
    lv_coord_t header_h = 0;
    lv_coord_t info_h = 0;
    lv_coord_t control_h = 0;
    lv_coord_t section_gap = 0;
    lv_coord_t min_dump_h = 46;
    lv_coord_t fixed_total = 0;
    lv_coord_t shortage = 0;
    lv_coord_t dump_h = 0;
    BaseType_t task_ok = pdFAIL;

    if (parent == NULL) {
        return NULL;
    }

    if (s_rfid_runtime.task_handle != NULL) {
        vTaskDelete(s_rfid_runtime.task_handle);
        s_rfid_runtime.task_handle = NULL;
    }

    /* Defensive cleanup for repeated enter/leave cycles. */
    (void)RFID_deinit();

    memset(&s_rfid_runtime, 0, sizeof(s_rfid_runtime));

    s_rfid_runtime.home_cb = home_cb;
    s_rfid_runtime.home_user_ctx = home_user_ctx;
    s_rfid_runtime.button_scan = (btn_scan_s){ 0 };

    s_rfid_runtime.selected_sector = dev_rfid_get_view_sector();
    s_rfid_runtime.card_error = false;
    (void)dev_rfid_set_view_sector(s_rfid_runtime.selected_sector);

    screen = lv_obj_create(parent);
    lv_obj_set_size(screen, area_w, area_h);
    lv_obj_set_pos(screen, 0, 0);
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x051A30), 0);
    lv_obj_set_style_bg_grad_color(screen, lv_color_hex(0x0A2E4C), 0);
    lv_obj_set_style_bg_grad_dir(screen, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(screen, 0, 0);
    lv_obj_set_style_radius(screen, 0, 0);
    lv_obj_set_style_pad_all(screen, 2, 0);

    frame = lv_obj_create(screen);
    lv_obj_set_size(frame, lv_pct(100), lv_pct(100));
    lv_obj_align(frame, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(frame, lv_color_hex(0x07223A), 0);
    lv_obj_set_style_bg_opa(frame, LV_OPA_70, 0);
    lv_obj_set_style_border_color(frame, lv_color_hex(0x2ACBFF), 0);
    lv_obj_set_style_border_width(frame, 1, 0);
    lv_obj_set_style_radius(frame, 8, 0);
    lv_obj_set_style_pad_all(frame, 3, 0);
    lv_obj_clear_flag(frame, LV_OBJ_FLAG_SCROLLABLE);

    usable_h = (lv_coord_t)(area_h - 8);
    if (usable_h < 140) {
        usable_h = 140;
    }

    header_h = (usable_h >= 220) ? 44 : 36;
    info_h = (usable_h >= 220) ? 74 : 54;
    control_h = (usable_h >= 220) ? 26 : 22;
    section_gap = (usable_h >= 220) ? 4 : 2;

    fixed_total = (lv_coord_t)(header_h + info_h + control_h + (section_gap * 3));
    if ((fixed_total + min_dump_h) > usable_h) {
        shortage = (lv_coord_t)((fixed_total + min_dump_h) - usable_h);

        while ((shortage > 0) && (info_h > 44)) {
            info_h--;
            shortage--;
        }
        while ((shortage > 0) && (header_h > 32)) {
            header_h--;
            shortage--;
        }
        while ((shortage > 0) && (control_h > 22)) {
            control_h--;
            shortage--;
        }
    }

    dump_h = (lv_coord_t)(usable_h - header_h - info_h - control_h - (section_gap * 3));
    if (dump_h < min_dump_h) {
        dump_h = min_dump_h;
    }

    hdr_panel = lv_obj_create(frame);
    lv_obj_set_size(hdr_panel, lv_pct(100), header_h);
    lv_obj_align(hdr_panel, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(hdr_panel, lv_color_hex(0x0A2238), 0);
    lv_obj_set_style_bg_opa(hdr_panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(hdr_panel, lv_color_hex(0x26CFFF), 0);
    lv_obj_set_style_border_width(hdr_panel, 1, 0);
    lv_obj_set_style_radius(hdr_panel, 6, 0);
    lv_obj_set_style_pad_left(hdr_panel, 8, 0);
    lv_obj_set_style_pad_right(hdr_panel, 8, 0);
    lv_obj_set_style_pad_top(hdr_panel, 4, 0);
    lv_obj_set_style_pad_bottom(hdr_panel, 4, 0);
    lv_obj_clear_flag(hdr_panel, LV_OBJ_FLAG_SCROLLABLE);

    title_label = lv_label_create(hdr_panel);
    lv_label_set_text(title_label, "RFID READER");
    lv_obj_set_style_text_color(title_label, lv_color_hex(0xE8F8FF), 0);
    lv_obj_set_style_text_font(title_label, &lv_font_montserrat_14, 0);
    lv_obj_align(title_label, LV_ALIGN_TOP_LEFT, 0, 0);

    s_rfid_runtime.status_label = lv_label_create(hdr_panel);
    lv_label_set_text(s_rfid_runtime.status_label, "Status: Initializing...");
    lv_obj_set_style_text_color(s_rfid_runtime.status_label, lv_color_hex(0x86DFFF), 0);
    lv_obj_set_style_text_font(s_rfid_runtime.status_label, &lv_font_montserrat_14, 0);
    lv_obj_align(s_rfid_runtime.status_label, LV_ALIGN_BOTTOM_LEFT, 0, 0);

    s_rfid_runtime.state_indicator = lv_obj_create(hdr_panel);
    lv_obj_set_size(s_rfid_runtime.state_indicator, 18, 18);
    lv_obj_align(s_rfid_runtime.state_indicator, LV_ALIGN_RIGHT_MID, -2, 0);
    lv_obj_set_style_pad_all(s_rfid_runtime.state_indicator, 0, 0);
    lv_obj_clear_flag(s_rfid_runtime.state_indicator, LV_OBJ_FLAG_SCROLLABLE);
    _rfid_apply_indicator_style(s_rfid_runtime.state_indicator, false, false);

    info_panel = lv_obj_create(frame);
    lv_obj_set_size(info_panel, lv_pct(100), info_h);
    lv_obj_align(info_panel, LV_ALIGN_TOP_MID, 0, (lv_coord_t)(header_h + section_gap));
    lv_obj_set_style_bg_color(info_panel, lv_color_hex(0x092B48), 0);
    lv_obj_set_style_bg_opa(info_panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(info_panel, lv_color_hex(0x2BCBFF), 0);
    lv_obj_set_style_border_width(info_panel, 1, 0);
    lv_obj_set_style_radius(info_panel, 6, 0);
    lv_obj_set_style_pad_all(info_panel, 6, 0);
    lv_obj_clear_flag(info_panel, LV_OBJ_FLAG_SCROLLABLE);

    s_rfid_runtime.uid_label = lv_label_create(info_panel);
    lv_obj_set_width(s_rfid_runtime.uid_label, lv_pct(100));
    lv_label_set_long_mode(s_rfid_runtime.uid_label, LV_LABEL_LONG_WRAP);
    lv_label_set_text(s_rfid_runtime.uid_label, "UID: --\nType: --\nSAK: --");
    lv_obj_set_style_text_color(s_rfid_runtime.uid_label, lv_color_hex(0xEAF7FF), 0);
    lv_obj_set_style_text_font(s_rfid_runtime.uid_label, &lv_font_montserrat_14, 0);
    lv_obj_align(s_rfid_runtime.uid_label, LV_ALIGN_TOP_LEFT, 0, 0);

    ctrl_panel = lv_obj_create(frame);
    lv_obj_set_size(ctrl_panel, lv_pct(100), control_h);
    lv_obj_align(
        ctrl_panel, LV_ALIGN_TOP_MID, 0, (lv_coord_t)(header_h + section_gap + info_h + section_gap)
    );
    lv_obj_set_style_bg_color(ctrl_panel, lv_color_hex(0x0A2238), 0);
    lv_obj_set_style_bg_opa(ctrl_panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(ctrl_panel, lv_color_hex(0x26CFFF), 0);
    lv_obj_set_style_border_width(ctrl_panel, 1, 0);
    lv_obj_set_style_radius(ctrl_panel, 5, 0);
    lv_obj_set_style_pad_all(ctrl_panel, 4, 0);
    lv_obj_clear_flag(ctrl_panel, LV_OBJ_FLAG_SCROLLABLE);

    s_rfid_runtime.sector_label = lv_label_create(ctrl_panel);
    lv_label_set_text(s_rfid_runtime.sector_label, "Sector: S00    UP:+1  DOWN:-1");
    lv_obj_set_style_text_color(s_rfid_runtime.sector_label, lv_color_hex(0x8BE8FF), 0);
    lv_obj_set_style_text_font(s_rfid_runtime.sector_label, &lv_font_montserrat_14, 0);
    lv_obj_align(s_rfid_runtime.sector_label, LV_ALIGN_LEFT_MID, 2, 0);

    s_rfid_runtime.dump_panel = lv_obj_create(frame);
    lv_obj_set_size(s_rfid_runtime.dump_panel, lv_pct(100), dump_h);
    lv_obj_align(
        s_rfid_runtime.dump_panel,
        LV_ALIGN_TOP_MID,
        0,
        (lv_coord_t)(header_h + section_gap + info_h + section_gap + control_h + section_gap)
    );
    lv_obj_set_style_bg_color(s_rfid_runtime.dump_panel, lv_color_hex(0x061C2F), 0);
    lv_obj_set_style_bg_opa(s_rfid_runtime.dump_panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(s_rfid_runtime.dump_panel, lv_color_hex(0x1FA8D5), 0);
    lv_obj_set_style_border_width(s_rfid_runtime.dump_panel, 1, 0);
    lv_obj_set_style_radius(s_rfid_runtime.dump_panel, 6, 0);
    lv_obj_set_style_pad_all(s_rfid_runtime.dump_panel, 4, 0);
    lv_obj_set_scroll_dir(s_rfid_runtime.dump_panel, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s_rfid_runtime.dump_panel, LV_SCROLLBAR_MODE_AUTO);

    s_rfid_runtime.dump_label = lv_label_create(s_rfid_runtime.dump_panel);
    lv_obj_set_width(s_rfid_runtime.dump_label, lv_pct(100));
    lv_label_set_long_mode(s_rfid_runtime.dump_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(s_rfid_runtime.dump_label, lv_color_hex(0xD6F3FF), 0);
#if CONFIG_LV_FONT_MONTSERRAT_12
    lv_obj_set_style_text_font(s_rfid_runtime.dump_label, &lv_font_montserrat_12, 0);
#else
    lv_obj_set_style_text_font(s_rfid_runtime.dump_label, &lv_font_montserrat_14, 0);
#endif
    lv_obj_set_style_text_line_space(s_rfid_runtime.dump_label, 1, 0);
    lv_obj_align(s_rfid_runtime.dump_label, LV_ALIGN_TOP_LEFT, 0, -1);

    s_rfid_runtime.ui_sync_timer =
        lv_timer_create(_rfid_ui_timer_cb, RFID_UI_TASK_PERIOD_MS, &s_rfid_runtime);
    if (s_rfid_runtime.ui_sync_timer == NULL) {
        s_rfid_runtime.root = NULL;
        s_rfid_runtime.status_label = NULL;
        s_rfid_runtime.uid_label = NULL;
        s_rfid_runtime.sector_label = NULL;
        s_rfid_runtime.state_indicator = NULL;
        s_rfid_runtime.dump_panel = NULL;
        s_rfid_runtime.dump_label = NULL;
        lv_obj_del(screen);
        ESP_LOGE(TAG, "lv_timer_create failed");
        return NULL;
    }

    s_rfid_runtime.root = screen;
    _rfid_set_status_text(&s_rfid_runtime, "Initializing reader...");
    _rfid_set_uid_text(&s_rfid_runtime, "UID: --");
    _rfid_clear_dump_text(&s_rfid_runtime);
    _rfid_sync_ui(&s_rfid_runtime);

    task_ok = xTaskCreate(
        _rfid_ui_task,
        "rfid_ui",
        RFID_UI_TASK_STACK_SIZE,
        &s_rfid_runtime,
        5,
        &s_rfid_runtime.task_handle
    );
    if (task_ok != pdPASS) {
        s_rfid_runtime.task_handle = NULL;
        if (s_rfid_runtime.ui_sync_timer != NULL) {
            lv_timer_delete(s_rfid_runtime.ui_sync_timer);
            s_rfid_runtime.ui_sync_timer = NULL;
        }
        s_rfid_runtime.root = NULL;
        s_rfid_runtime.status_label = NULL;
        s_rfid_runtime.uid_label = NULL;
        s_rfid_runtime.sector_label = NULL;
        s_rfid_runtime.state_indicator = NULL;
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
void rfid_destroy_screen(lv_obj_t* screen)
{
    esp_err_t ret = ESP_OK;

    if (s_rfid_runtime.ui_sync_timer != NULL) {
        lv_timer_delete(s_rfid_runtime.ui_sync_timer);
        s_rfid_runtime.ui_sync_timer = NULL;
    }

    if (s_rfid_runtime.task_handle != NULL) {
        vTaskDelete(s_rfid_runtime.task_handle);
        s_rfid_runtime.task_handle = NULL;
    }

    ret = RFID_deinit();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "RFID_deinit failed: %d", (int)ret);
    }

    s_rfid_runtime.home_cb = NULL;
    s_rfid_runtime.home_user_ctx = NULL;
    s_rfid_runtime.button_scan = (btn_scan_s){ 0 };
    s_rfid_runtime.reader_ready = false;
    s_rfid_runtime.card_present = false;
    s_rfid_runtime.card_error = false;
    s_rfid_runtime.dump_need_scroll_top = false;
    s_rfid_runtime.selected_sector = DEV_RFID_VIEW_SECTOR_MIN;
    s_rfid_runtime.ui_sync_timer = NULL;
    s_rfid_runtime.root = NULL;
    s_rfid_runtime.status_label = NULL;
    s_rfid_runtime.uid_label = NULL;
    s_rfid_runtime.sector_label = NULL;
    s_rfid_runtime.state_indicator = NULL;
    s_rfid_runtime.dump_panel = NULL;
    s_rfid_runtime.dump_label = NULL;
    s_rfid_runtime.status_text[0] = '\0';
    s_rfid_runtime.uid_text[0] = '\0';
    s_rfid_runtime.dump_text[0] = '\0';

    if ((screen != NULL) && lv_obj_is_valid(screen)) {
        lv_obj_del(screen);
    }
}
