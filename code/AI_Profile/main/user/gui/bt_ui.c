#include "bt_ui.h"

#include <stdio.h>
#include <string.h>

#include "user/communication/ble/ble.h"

#define TAG "bt_ui"

static bt_ui_runtime_s s_bt_runtime;
static portMUX_TYPE s_bt_ui_lock = portMUX_INITIALIZER_UNLOCKED;

/*
 * brief : _bt_ui_style_section_label.
 * input : see parameters.
 * output: none.
 * type  : private
 */
static void _bt_ui_style_section_label(lv_obj_t* label)
{
    if (label == NULL) {
        return;
    }

    lv_obj_set_style_text_color(label, lv_color_hex(BT_UI_TITLE_COLOR), 0);
    lv_obj_set_style_text_font(label, &DESKTOP_TEXT_FONT, 0);
}

/*
 * brief : _bt_ui_style_text_edit.
 * input : see parameters.
 * output: none.
 * type  : private
 */
static void _bt_ui_style_text_edit(lv_obj_t* ta)
{
    if (ta == NULL) {
        return;
    }

    lv_textarea_set_one_line(ta, false);
    lv_textarea_set_cursor_click_pos(ta, false);
    lv_obj_set_scrollbar_mode(ta, LV_SCROLLBAR_MODE_AUTO);

    lv_obj_set_style_bg_color(ta, lv_color_hex(BT_UI_BG_COLOR), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(ta, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_text_color(ta, lv_color_hex(BT_UI_TEXT_COLOR), LV_PART_MAIN);
    lv_obj_set_style_text_font(ta, &DESKTOP_TEXT_FONT, LV_PART_MAIN);
    lv_obj_set_style_border_color(ta, lv_color_hex(BT_UI_BORDER_COLOR), LV_PART_MAIN);
    lv_obj_set_style_border_width(ta, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(ta, 2, LV_PART_MAIN);
    lv_obj_set_style_pad_left(ta, BT_UI_TEXT_PAD, LV_PART_MAIN);
    lv_obj_set_style_pad_right(ta, BT_UI_TEXT_PAD, LV_PART_MAIN);
    lv_obj_set_style_pad_top(ta, BT_UI_TEXT_PAD, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(ta, BT_UI_TEXT_PAD, LV_PART_MAIN);

    lv_obj_set_style_bg_color(ta, lv_color_hex(BT_UI_TEXT_COLOR), LV_PART_CURSOR);
    lv_obj_set_style_bg_opa(ta, LV_OPA_COVER, LV_PART_CURSOR);
    lv_obj_set_style_text_color(ta, lv_color_hex(BT_UI_BG_COLOR), LV_PART_CURSOR);
}

/*
 * brief : _bt_ui_push_line.
 * input : see parameters.
 * output: none.
 * type  : private
 */
static void
_bt_ui_push_line(char lines[BT_UI_IO_LINE_COUNT][BT_UI_IO_LINE_LEN], const char* line)
{
    uint32_t i = 0U;

    if (line == NULL) {
        return;
    }

    for (i = 0U; (i + 1U) < BT_UI_IO_LINE_COUNT; i++) {
        snprintf(lines[i], BT_UI_IO_LINE_LEN, "%s", lines[i + 1U]);
    }

    snprintf(lines[BT_UI_IO_LINE_COUNT - 1U], BT_UI_IO_LINE_LEN, "%s", line);
}

/*
 * brief : _bt_ui_rebuild_text.
 * input : see parameters.
 * output: none.
 * type  : private
 */
static void _bt_ui_rebuild_text(
    char* out,
    size_t out_size,
    char lines[BT_UI_IO_LINE_COUNT][BT_UI_IO_LINE_LEN],
    const char* empty_text
)
{
    uint32_t i = 0U;
    size_t used = 0U;
    int wr = 0;

    if ((out == NULL) || (out_size == 0U)) {
        return;
    }

    out[0] = '\0';
    for (i = 0U; i < BT_UI_IO_LINE_COUNT; i++) {
        if (lines[i][0] == '\0') {
            continue;
        }

        wr = snprintf(
            &out[used],
            out_size - used,
            "%s%s",
            (used == 0U) ? "" : "\n",
            lines[i]
        );
        if (wr < 0) {
            break;
        }

        if ((size_t)wr >= (out_size - used)) {
            used = out_size - 1U;
            break;
        }

        used += (size_t)wr;
    }

    if ((used == 0U) && (empty_text != NULL)) {
        snprintf(out, out_size, "%s", empty_text);
    }
}

/*
 * brief : _bt_ui_append_ble_item.
 * input : see parameters.
 * output: none.
 * type  : private
 */
static void
_bt_ui_append_ble_item(bt_ui_runtime_s* runtime, const ble_message_item_s* item)
{
    char line[BT_UI_IO_LINE_LEN];
    size_t prefix_len = 0U;
    size_t payload_max = 0U;
    uint32_t stamp = 0U;
    char (*target_lines)[BT_UI_IO_LINE_LEN] = NULL;
    bool* dirty_flag = NULL;

    if ((runtime == NULL) || (item == NULL)) {
        return;
    }

    if (item->dir == Ble_Message_Dir_Tx) {
        target_lines = runtime->tx_lines;
        dirty_flag = &runtime->tx_dirty;
    }
    else {
        target_lines = runtime->rx_lines;
        dirty_flag = &runtime->rx_dirty;
    }

    stamp = item->timestamp_ms % 100000U;
    prefix_len = (size_t)snprintf(line, sizeof(line), "[%05lu] ", (unsigned long)stamp);
    if (prefix_len >= sizeof(line)) {
        line[sizeof(line) - 1U] = '\0';
    }
    else {
        payload_max = sizeof(line) - prefix_len - 1U;
        (void)snprintf(
            &line[prefix_len],
            sizeof(line) - prefix_len,
            "%.*s",
            (int)payload_max,
            item->text
        );
    }

    _bt_ui_push_line(target_lines, line);
    if (dirty_flag != NULL) {
        *dirty_flag = true;
    }
}

/*
 * brief : _bt_ui_refresh_param_text.
 * input : see parameters.
 * output: none.
 * type  : private
 */
static void
_bt_ui_refresh_param_text(bt_ui_runtime_s* runtime, const ble_message_stats_s* stats)
{
    char param_text[BT_UI_PARAM_TEXT_LEN];
    const char* ready_text = "syncing";
    const char* conn_text = "idle";

    if ((runtime == NULL) || (stats == NULL)) {
        return;
    }

    if (ble_is_ready()) {
        ready_text = "ready";
    }
    if (ble_is_connected()) {
        conn_text = "connected";
    }

    snprintf(
        param_text,
        sizeof(param_text),
        "Name: %s\nService: %s\nRX UUID: %s\nTX UUID: %s\nState: %s\nLink: %s\nRX:%lu "
        "TX:%lu Drop:%lu\nFIFO:%u/%u",
        BLE_DEVICE_NAME,
        BLE_SERVICE_UUID,
        BLE_RX_CHAR_UUID,
        BLE_TX_CHAR_UUID,
        ready_text,
        conn_text,
        (unsigned long)stats->rx_total,
        (unsigned long)stats->tx_total,
        (unsigned long)stats->dropped_total,
        (unsigned)stats->fifo_used,
        (unsigned)stats->fifo_capacity
    );

    if (strncmp(runtime->param_text, param_text, sizeof(runtime->param_text)) != 0) {
        snprintf(runtime->param_text, sizeof(runtime->param_text), "%s", param_text);
        runtime->param_dirty = true;
    }
}

/*
 * brief : _bt_ui_sync_ui.
 * input : see parameters.
 * output: none.
 * type  : private
 */
static void _bt_ui_sync_ui(bt_ui_runtime_s* runtime)
{
    bool param_dirty = false;
    bool rx_dirty = false;
    bool tx_dirty = false;
    char param_text[BT_UI_PARAM_TEXT_LEN] = { 0 };
    char rx_text[BT_UI_IO_TEXT_LEN] = { 0 };
    char tx_text[BT_UI_IO_TEXT_LEN] = { 0 };

    if (runtime == NULL) {
        return;
    }

    taskENTER_CRITICAL(&s_bt_ui_lock);

    if (runtime->param_dirty) {
        snprintf(param_text, sizeof(param_text), "%s", runtime->param_text);
        runtime->param_dirty = false;
        param_dirty = true;
    }

    if (runtime->rx_dirty) {
        _bt_ui_rebuild_text(rx_text, sizeof(rx_text), runtime->rx_lines, "No RX data.");
        runtime->rx_dirty = false;
        rx_dirty = true;
    }

    if (runtime->tx_dirty) {
        _bt_ui_rebuild_text(tx_text, sizeof(tx_text), runtime->tx_lines, "No TX data.");
        runtime->tx_dirty = false;
        tx_dirty = true;
    }

    taskEXIT_CRITICAL(&s_bt_ui_lock);

    if (param_dirty && (runtime->param_edit != NULL) && lv_obj_is_valid(runtime->param_edit)) {
        lv_textarea_set_text(runtime->param_edit, param_text);
    }

    if (rx_dirty && (runtime->rx_edit != NULL) && lv_obj_is_valid(runtime->rx_edit)) {
        lv_textarea_set_text(runtime->rx_edit, rx_text);
    }

    if (tx_dirty && (runtime->tx_edit != NULL) && lv_obj_is_valid(runtime->tx_edit)) {
        lv_textarea_set_text(runtime->tx_edit, tx_text);
    }
}

/*
 * brief : _bt_ui_timer_cb.
 * input : see parameters.
 * output: none.
 * type  : private
 */
static void _bt_ui_timer_cb(lv_timer_t* timer)
{
    bt_ui_runtime_s* runtime = NULL;

    if (timer == NULL) {
        return;
    }

    runtime = (bt_ui_runtime_s*)lv_timer_get_user_data(timer);
    _bt_ui_sync_ui(runtime);
}

/*
 * brief : _bt_ui_refresh_message.
 * input : see parameters.
 * output: none.
 * type  : private
 */
static void _bt_ui_refresh_message(bt_ui_runtime_s* runtime)
{
    ble_message_stats_s stats = { 0 };
    ble_message_item_s item = { 0 };
    uint32_t pop_count = 0U;
    bool has_item = false;

    if (runtime == NULL) {
        return;
    }

    ble_message_get_stats(&stats);

    taskENTER_CRITICAL(&s_bt_ui_lock);

    for (pop_count = 0U; pop_count < BT_UI_MESSAGE_POP_BATCH; pop_count++) {
        has_item = ble_message_fifo_pop(&item);
        if (!has_item) {
            break;
        }
        _bt_ui_append_ble_item(runtime, &item);
    }

    _bt_ui_refresh_param_text(runtime, &stats);

    taskEXIT_CRITICAL(&s_bt_ui_lock);
}

/*
 * brief : _bt_ui_task.
 * input : see parameters.
 * output: none.
 * type  : private
 */
static void _bt_ui_task(void* param)
{
    bt_ui_runtime_s* runtime = (bt_ui_runtime_s*)param;
    btn_status_e btn_val = Btn_Idle;

    while (1) {
        btn_val = button_scan_state(&runtime->button_scan, BT_UI_TASK_PERIOD_MS);
        if ((btn_val == Btn_Both_Click) && (runtime->home_cb != NULL)) {
            runtime->home_cb(runtime->home_user_ctx);
        }

        runtime->message_refresh_elapsed_ms += BT_UI_TASK_PERIOD_MS;
        if (runtime->message_refresh_elapsed_ms >= BT_UI_MESSAGE_REFRESH_MS) {
            runtime->message_refresh_elapsed_ms = 0U;
            _bt_ui_refresh_message(runtime);
        }

        delay_ms(BT_UI_TASK_PERIOD_MS);
    }
}

/*
 * brief : Create one section title + textarea.
 * input : see parameters.
 * output: created textarea object.
 * type  : private
 */
static lv_obj_t* _bt_ui_create_section(
    lv_obj_t* parent,
    const char* title,
    lv_coord_t x,
    lv_coord_t y,
    lv_coord_t w,
    lv_coord_t h
)
{
    lv_obj_t* label = NULL;
    lv_obj_t* ta = NULL;

    label = lv_label_create(parent);
    lv_label_set_text(label, title);
    _bt_ui_style_section_label(label);
    lv_obj_set_pos(label, x, y);

    ta = lv_textarea_create(parent);
    _bt_ui_style_text_edit(ta);
    lv_obj_set_pos(ta, x, y + BT_UI_LABEL_HEIGHT + BT_UI_LABEL_TEXT_GAP);
    lv_obj_set_size(ta, w, h);

    return ta;
}

/*
 * brief : Create the Bluetooth page root screen object.
 * input : lcd_w - LCD width; lcd_h - LCD height.
 * output: Created LVGL screen object.
 * type  : public
 */
lv_obj_t* bt_open_screen(
    lv_obj_t* parent,
    lv_coord_t area_w,
    lv_coord_t area_h,
    ui_menu_home_cb_t home_cb,
    void* home_user_ctx
)
{
    lv_obj_t* screen = NULL;
    lv_coord_t inner_x = BT_UI_SECTION_MARGIN;
    lv_coord_t inner_w = area_w - (BT_UI_SECTION_MARGIN * 2);
    lv_coord_t y = BT_UI_SECTION_MARGIN;
    lv_coord_t text_total_h = 0;
    lv_coord_t text_h_a = 0;
    lv_coord_t text_h_b = 0;
    lv_coord_t text_h_c = 0;
    BaseType_t task_ok = pdFAIL;

    if (parent == NULL) {
        return NULL;
    }

    if (s_bt_runtime.task_handle != NULL) {
        vTaskDelete(s_bt_runtime.task_handle);
        s_bt_runtime.task_handle = NULL;
    }
    if (s_bt_runtime.ui_sync_timer != NULL) {
        lv_timer_delete(s_bt_runtime.ui_sync_timer);
        s_bt_runtime.ui_sync_timer = NULL;
    }

    memset(&s_bt_runtime, 0, sizeof(s_bt_runtime));

    s_bt_runtime.home_cb = home_cb;
    s_bt_runtime.home_user_ctx = home_user_ctx;
    s_bt_runtime.button_scan = (btn_scan_s){ 0 };

    screen = lv_obj_create(parent);
    lv_obj_set_size(screen, area_w, area_h);
    lv_obj_set_pos(screen, 0, 0);
    lv_obj_set_style_bg_color(screen, lv_color_hex(BT_UI_BG_COLOR), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(screen, 0, 0);
    lv_obj_set_style_radius(screen, 0, 0);
    lv_obj_set_style_pad_all(screen, 0, 0);

    if (inner_w < 20) {
        inner_w = 20;
    }

    text_total_h = area_h - (BT_UI_SECTION_MARGIN * 2)
        - ((BT_UI_LABEL_HEIGHT + BT_UI_LABEL_TEXT_GAP) * 3) - (BT_UI_SECTION_GAP * 2);
    if (text_total_h < 3) {
        text_total_h = 3;
    }

    text_h_a = text_total_h / 3;
    text_h_b = text_total_h / 3;
    text_h_c = text_total_h - text_h_a - text_h_b;

    s_bt_runtime.param_edit =
        _bt_ui_create_section(screen, "1. PARAM", inner_x, y, inner_w, text_h_a);
    y += BT_UI_LABEL_HEIGHT + BT_UI_LABEL_TEXT_GAP + text_h_a + BT_UI_SECTION_GAP;

    s_bt_runtime.rx_edit =
        _bt_ui_create_section(screen, "2. RECEIVE", inner_x, y, inner_w, text_h_b);
    y += BT_UI_LABEL_HEIGHT + BT_UI_LABEL_TEXT_GAP + text_h_b + BT_UI_SECTION_GAP;

    s_bt_runtime.tx_edit =
        _bt_ui_create_section(screen, "3. SEND", inner_x, y, inner_w, text_h_c);

    snprintf(
        s_bt_runtime.param_text,
        sizeof(s_bt_runtime.param_text),
        "BLE message monitor starting..."
    );
    _bt_ui_push_line(s_bt_runtime.rx_lines, "No RX data.");
    _bt_ui_push_line(s_bt_runtime.tx_lines, "No TX data.");

    s_bt_runtime.param_dirty = true;
    s_bt_runtime.rx_dirty = true;
    s_bt_runtime.tx_dirty = true;
    _bt_ui_sync_ui(&s_bt_runtime);
    _bt_ui_refresh_message(&s_bt_runtime);
    _bt_ui_sync_ui(&s_bt_runtime);

    s_bt_runtime.ui_sync_timer =
        lv_timer_create(_bt_ui_timer_cb, BT_UI_TASK_PERIOD_MS, &s_bt_runtime);
    if (s_bt_runtime.ui_sync_timer == NULL) {
        lv_obj_del(screen);
        ESP_LOGE(TAG, "lv_timer_create failed");
        return NULL;
    }

    desktop_post_message("BLE message assistant opened.");

    task_ok = xTaskCreate(
        _bt_ui_task,
        "bt_ui",
        BT_UI_TASK_STACK_SIZE,
        &s_bt_runtime,
        BT_UI_TASK_PRIO,
        &s_bt_runtime.task_handle
    );
    if (task_ok != pdPASS) {
        if (s_bt_runtime.ui_sync_timer != NULL) {
            lv_timer_delete(s_bt_runtime.ui_sync_timer);
            s_bt_runtime.ui_sync_timer = NULL;
        }
        s_bt_runtime.task_handle = NULL;
        lv_obj_del(screen);
        ESP_LOGE(TAG, "xTaskCreate failed");
        return NULL;
    }

    return screen;
}

/*
 * brief : bt_close_screen.
 * input : see parameters.
 * output: none.
 * type  : public
 */
void bt_close_screen(lv_obj_t* screen)
{
    if (s_bt_runtime.task_handle != NULL) {
        vTaskDelete(s_bt_runtime.task_handle);
        s_bt_runtime.task_handle = NULL;
    }
    if (s_bt_runtime.ui_sync_timer != NULL) {
        lv_timer_delete(s_bt_runtime.ui_sync_timer);
        s_bt_runtime.ui_sync_timer = NULL;
    }

    if ((screen != NULL) && lv_obj_is_valid(screen)) {
        lv_obj_del(screen);
    }

    memset(&s_bt_runtime, 0, sizeof(s_bt_runtime));
}
