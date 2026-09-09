#include "print_ui.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "user/common/user_app_notify.h"
#include "user/device/dev_printer.h"

#define TAG "print_ui"

static print_ui_runtime_s s_print_runtime;
static portMUX_TYPE s_print_lock = portMUX_INITIALIZER_UNLOCKED;
static uint32_t s_print_image_count = 0U;

static print_auto_test_s automatic_mode = { 0 };

static esp_err_t _print_cmd_clear(void);
static esp_err_t _print_cmd_feed(void);
static esp_err_t _print_cmd_image(void);
static esp_err_t _print_cmd_text(void);
static esp_err_t _print_cmd_qrcode(void);
static esp_err_t _print_cmd_Auto(void);

static const print_ui_cmd_s s_print_cmds[PRINT_UI_CMD_COUNT] = {
    { LV_SYMBOL_WARNING, "Clear", 0xF4D3D9, _print_cmd_clear },
    { LV_SYMBOL_LIST, "Feed", 0xD8F0C6, _print_cmd_feed },
    { LV_SYMBOL_REFRESH, "Image", 0xCDEBFF, _print_cmd_image },
    { LV_SYMBOL_FILE, "Text", 0xFCE4B7, _print_cmd_text },
    { LV_SYMBOL_WIFI, "QR", 0xD6F2F6, _print_cmd_qrcode },
    { LV_SYMBOL_SETTINGS, "Automatic", 0xE4D8FB, _print_cmd_Auto },
};

/*
 * brief : _print_cmd_clear.
 * input : none.
 * output: return value from this function.
 * type  : private
 */
static esp_err_t _print_cmd_clear(void)
{
    return printer_clear_cache();
}

/*
 * brief : _print_cmd_feed.
 * input : none.
 * output: return value from this function.
 * type  : private
 */
static esp_err_t _print_cmd_feed(void)
{
    return printer_feed_lines(10U);
}

/*
 * brief : _print_cmd_image.
 * input : none.
 * output: return value from this function.
 * type  : private
 */
static esp_err_t _print_cmd_image(void)
{
    // const char* bin[] = {
    //     "animation_girl.bin",
    //     "dragon.bin",
    //     "pirate_ship.bin",
    // };
    // esp_err_t ret = ESP_OK;
    // static size_t bin_index = 0U;

    // ret = printer_image_via_bin(bin[bin_index]);
    // bin_index = (bin_index + 1U) % (sizeof(bin) / sizeof(bin[0]));
    // if (ret != ESP_OK) {
    //     return ret;
    // }

    printer_image_via_bin("dragon.bin");
    printer_image_via_bin("pirate_ship.bin");
    return ESP_OK;
}

/*
 * brief : _print_cmd_text.
 * input : none.
 * output: return value from this function.
 * type  : private
 */
static esp_err_t _print_cmd_text(void)
{
    esp_err_t ret = ESP_OK;
    uint16_t battery_mv = 0U;
    time_t now_sec = 0;
    struct tm tm_now = { 0 };
    bool tm_valid = false;
    char line[48] = { 0 };

    ret = printer_set_justification(PRINTER_JUSTIFY_LEFT);
    if (ret != ESP_OK) {
        return ret;
    }

    battery_mv = bsp_read_battery_mv();
    now_sec = time(NULL);
    tm_valid = (localtime_r(&now_sec, &tm_now) != NULL);

    (void)snprintf(
        line,
        sizeof(line),
        " Battery       : %4u mv\r\n",
        (unsigned)battery_mv
    );
    ret = printer_write_string(line);
    if (ret != ESP_OK) {
        return ret;
    }

    if (tm_valid) {
        (void)snprintf(
            line,
            sizeof(line),
            " RealTime    : %02d:%02d:%02d\r\n",
            tm_now.tm_hour,
            tm_now.tm_min,
            tm_now.tm_sec
        );
    }
    else {
        (void)snprintf(line, sizeof(line), " RealTime    : --:--:--\r\n");
    }
    ret = printer_write_string(line);
    if (ret != ESP_OK) {
        return ret;
    }

    (void)snprintf(
        line,
        sizeof(line),
        " Image         : %4u \r\n",
        (unsigned)s_print_image_count
    );
    ret = printer_write_string(line);
    if (ret != ESP_OK) {
        return ret;
    }

    return printer_feed_lines(2U);
}

/*
 * brief : _print_cmd_qrcode.
 * input : none.
 * output: return value from this function.
 * type  : private
 */
static esp_err_t _print_cmd_qrcode(void)
{
    static const uint8_t qr_payload[] = "https://xiao-zhi.local/qr-demo";
    esp_err_t ret = ESP_OK;

    ret = printer_set_justification(PRINTER_JUSTIFY_CENTER);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = printer_set_barcode_width(2U);
    if (ret != ESP_OK) {
        (void)printer_set_justification(PRINTER_JUSTIFY_LEFT);
        return ret;
    }

    ret = printer_set_barcode_height(72U);
    if (ret != ESP_OK) {
        (void)printer_set_justification(PRINTER_JUSTIFY_LEFT);
        return ret;
    }

    ret = printer_barcode_code128(qr_payload, sizeof(qr_payload) - 1U);
    if (ret != ESP_OK) {
        (void)printer_set_justification(PRINTER_JUSTIFY_LEFT);
        return ret;
    }

    ret = printer_write_string("QR event demo\r\n");
    (void)printer_set_justification(PRINTER_JUSTIFY_LEFT);
    if (ret != ESP_OK) {
        return ret;
    }

    return printer_feed_lines(2U);
}

/*
 * brief : _print_cmd_Auto.
 * input : none.
 * output: return value from this function.
 * type  : private
 */
static esp_err_t _print_cmd_Auto(void)
{
    automatic_mode.en = (automatic_mode.en == true) ? false : true;
    automatic_mode.interval_ms = AUTO_PRINT_INTERVAL_MS;
    return ESP_OK;
}

/*
 * brief : _print_obj_valid.
 * input : see parameters.
 * output: return value from this function.
 * type  : private
 */
static bool _print_obj_valid(lv_obj_t* obj)
{
    return (obj != NULL) && lv_obj_is_valid(obj);
}

/*
 * brief : _print_apply_command_tile_style.
 * input : see parameters.
 * output: none.
 * type  : private
 */
static void _print_apply_command_tile_style(
    lv_obj_t* btn,
    lv_obj_t* symbol_label,
    lv_obj_t* name_label,
    uint32_t color_hex,
    bool selected
)
{
    lv_color_t base_color = lv_color_hex(color_hex);
    lv_color_t text_color = lv_color_hex(0x1D3247);
    lv_color_t border_color = lv_color_hex(0xB6D2E8);

    if (!_print_obj_valid(btn) || !_print_obj_valid(symbol_label)
        || !_print_obj_valid(name_label)) {
        return;
    }

    if (selected) {
        border_color = lv_color_hex(0x2D76AC);
        lv_obj_set_style_shadow_width(btn, 10, 0);
        lv_obj_set_style_shadow_opa(btn, LV_OPA_30, 0);
    }
    else {
        lv_obj_set_style_shadow_width(btn, 3, 0);
        lv_obj_set_style_shadow_opa(btn, LV_OPA_20, 0);
    }

    lv_obj_set_style_bg_color(btn, base_color, 0);
    lv_obj_set_style_bg_opa(btn, selected ? LV_OPA_80 : LV_OPA_70, 0);
    lv_obj_set_style_border_color(btn, border_color, 0);
    lv_obj_set_style_border_width(btn, selected ? 3 : 1, 0);
    lv_obj_set_style_shadow_color(btn, lv_color_hex(0x3D84BD), 0);
    lv_obj_set_style_text_color(symbol_label, text_color, 0);
    lv_obj_set_style_text_color(name_label, text_color, 0);
}

/*
 * brief : _print_apply_status_title_style.
 * input : see parameters.
 * output: none.
 * type  : private
 */
static void
_print_apply_status_title_style(lv_obj_t* label, bool printer_ready, bool status_valid)
{
    if (!_print_obj_valid(label)) {
        return;
    }

    if (!printer_ready) {
        lv_obj_set_style_text_color(label, lv_color_hex(0xC53B3B), 0);
    }
    else if (!status_valid) {
        lv_obj_set_style_text_color(label, lv_color_hex(0xA56700), 0);
    }
    else {
        lv_obj_set_style_text_color(label, lv_color_hex(0x1C8F39), 0);
    }
}

/*
 * brief : _print_create_metric_card.
 * input : see parameters.
 * output: return value from this function.
 * type  : private
 */
static lv_obj_t* _print_create_metric_card(
    lv_obj_t* parent,
    lv_align_t align,
    lv_coord_t x_ofs,
    const char* metric_icon,
    const char* metric_name,
    lv_color_t card_bg_color,
    lv_color_t icon_color,
    bool draw_right_sep,
    lv_obj_t** out_value_label
)
{
    lv_obj_t* card = NULL;
    lv_obj_t* icon_lab = NULL;
    lv_obj_t* name_lab = NULL;
    lv_obj_t* sep = NULL;
    lv_obj_t* value_label = NULL;

    if ((parent == NULL) || (metric_icon == NULL) || (metric_name == NULL)
        || (out_value_label == NULL)) {
        return NULL;
    }

    card = lv_obj_create(parent);
    lv_obj_set_size(card, lv_pct(32), lv_pct(100));
    lv_obj_align(card, align, x_ofs, 0);
    lv_obj_set_style_bg_color(card, card_bg_color, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_70, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_style_radius(card, 0, 0);
    lv_obj_set_style_pad_left(card, 4, 0);
    lv_obj_set_style_pad_right(card, 4, 0);
    lv_obj_set_style_pad_top(card, 5, 0);
    lv_obj_set_style_pad_bottom(card, 5, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    icon_lab = lv_label_create(card);
    lv_label_set_text(icon_lab, metric_icon);
    lv_obj_set_style_text_color(icon_lab, icon_color, 0);
    lv_obj_set_style_text_font(icon_lab, &DESKTOP_TEXT_FONT, 0);
    lv_obj_align(icon_lab, LV_ALIGN_TOP_LEFT, 0, 0);

    name_lab = lv_label_create(card);
    lv_label_set_text(name_lab, metric_name);
    lv_obj_set_style_text_color(name_lab, lv_color_hex(0x1A2733), 0);
    lv_obj_set_style_text_font(name_lab, &DESKTOP_TEXT_FONT, 0);
    lv_obj_align_to(name_lab, icon_lab, LV_ALIGN_OUT_RIGHT_MID, 6, 0);

    value_label = lv_label_create(card);
    lv_label_set_text(value_label, "--");
    lv_obj_set_style_text_color(value_label, lv_color_hex(0x202A35), 0);
#if CONFIG_LV_FONT_MONTSERRAT_22
    lv_obj_set_style_text_font(value_label, &lv_font_montserrat_22, 0);
#elif CONFIG_LV_FONT_MONTSERRAT_20
    lv_obj_set_style_text_font(value_label, &lv_font_montserrat_20, 0);
#elif CONFIG_LV_FONT_MONTSERRAT_18
    lv_obj_set_style_text_font(value_label, &lv_font_montserrat_18, 0);
#else
    lv_obj_set_style_text_font(value_label, &DESKTOP_TEXT_FONT, 0);
#endif
    lv_obj_align(value_label, LV_ALIGN_BOTTOM_MID, 0, -1);

    if (draw_right_sep) {
        sep = lv_obj_create(card);
        lv_obj_set_size(sep, 1, lv_pct(78));
        lv_obj_align(sep, LV_ALIGN_RIGHT_MID, -1, 0);
        lv_obj_set_style_bg_color(sep, lv_color_hex(0xC6D8E8), 0);
        lv_obj_set_style_bg_opa(sep, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(sep, 0, 0);
        lv_obj_set_style_radius(sep, 0, 0);
        lv_obj_clear_flag(sep, LV_OBJ_FLAG_SCROLLABLE);
    }

    *out_value_label = value_label;
    return card;
}

/*
 * brief : _print_set_status_text.
 * input : see parameters.
 * output: none.
 * type  : private
 */
static void _print_set_status_text(print_ui_runtime_s* runtime, const char* fmt, ...)
{
    char local_text[PRINT_UI_TEXT_LEN] = { 0 };
    va_list args;

    if ((runtime == NULL) || (fmt == NULL)) {
        return;
    }

    va_start(args, fmt);
    vsnprintf(local_text, sizeof(local_text), fmt, args);
    va_end(args);

    taskENTER_CRITICAL(&s_print_lock);
    snprintf(runtime->status_text, sizeof(runtime->status_text), "%s", local_text);
    runtime->dirty = true;
    taskEXIT_CRITICAL(&s_print_lock);
}

/*
 * brief : _print_set_action_text.
 * input : see parameters.
 * output: none.
 * type  : private
 */
static void _print_set_action_text(print_ui_runtime_s* runtime, const char* fmt, ...)
{
    char local_text[PRINT_UI_TEXT_LEN] = { 0 };
    va_list args;

    if ((runtime == NULL) || (fmt == NULL)) {
        return;
    }

    va_start(args, fmt);
    vsnprintf(local_text, sizeof(local_text), fmt, args);
    va_end(args);

    taskENTER_CRITICAL(&s_print_lock);
    snprintf(runtime->action_text, sizeof(runtime->action_text), "%s", local_text);
    runtime->dirty = true;
    taskEXIT_CRITICAL(&s_print_lock);
}

/*
 * brief : _print_try_init_printer.
 * input : see parameters.
 * output: return value from this function.
 * type  : private
 */
static esp_err_t _print_try_init_printer(print_ui_runtime_s* runtime)
{
    esp_err_t ret = ESP_FAIL;

    if (runtime == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    ret = printer_init(NULL);

    taskENTER_CRITICAL(&s_print_lock);
    runtime->printer_ready = (ret == ESP_OK);
    runtime->status_valid = false;
    runtime->last_status_err = ret;
    runtime->dirty = true;
    taskEXIT_CRITICAL(&s_print_lock);

    if (ret == ESP_OK) {
        _print_set_status_text(runtime, "Printer initialized");
    }
    else {
        _print_set_status_text(runtime, "Init failed: err=%d", (int)ret);
    }

    return ret;
}

/*
 * brief : _obtain_print_profile.
 * input : see parameters.
 * output: none.
 * type  : private
 */
static void _obtain_print_profile(print_ui_runtime_s* runtime)
{
    esp_err_t ret = ESP_FAIL;
    printer_detect_status_t status = { 0 };

    if (runtime == NULL) {
        return;
    }

    ret = printer_detect_status(&status);
    if (ret == ESP_OK) {
        taskENTER_CRITICAL(&s_print_lock);
        runtime->printer_ready = true;
        runtime->status_valid = true;
        runtime->temperature_celsius = status.tph_temperature_celsius;
        runtime->paper_detect_raw = status.paper_detect_raw;
        runtime->working_voltage_raw = status.working_voltage_raw;
        runtime->last_status_err = ESP_OK;
        runtime->dirty = true;
        taskEXIT_CRITICAL(&s_print_lock);

        _print_set_status_text(runtime, "Printer online, refresh every 1s");
        return;
    }

    taskENTER_CRITICAL(&s_print_lock);
    runtime->status_valid = false;
    runtime->last_status_err = ret;
    if (ret == ESP_ERR_INVALID_STATE) {
        runtime->printer_ready = false;
    }
    runtime->dirty = true;
    taskEXIT_CRITICAL(&s_print_lock);

    if (ret == ESP_ERR_INVALID_STATE) {
        _print_set_status_text(runtime, "Printer not ready, retrying init");
    }
    else {
        _print_set_status_text(runtime, "Status read failed: err=%d", (int)ret);
    }
}

/*
 * brief : _print_select_command.
 * input : see parameters.
 * output: none.
 * type  : private
 */
static void _print_select_command(print_ui_runtime_s* runtime, bool next)
{
    uint8_t cmd_count = 0U;
    uint8_t selected = 0U;

    if (runtime == NULL) {
        return;
    }

    taskENTER_CRITICAL(&s_print_lock);
    cmd_count = runtime->cmd_count;
    if (cmd_count > 0U) {
        if (next) {
            selected = (uint8_t)((runtime->selected_cmd + 1U) % cmd_count);
        }
        else {
            selected = (runtime->selected_cmd == 0U)
                ? (uint8_t)(cmd_count - 1U)
                : (uint8_t)(runtime->selected_cmd - 1U);
        }
        runtime->selected_cmd = selected;
        runtime->dirty = true;
    }
    taskEXIT_CRITICAL(&s_print_lock);

    if (cmd_count > 0U) {
        _print_set_action_text(runtime, "Selected: %s", s_print_cmds[selected].name);
    }
}

/*
 * brief : _print_execute_selected_command.
 * input : see parameters.
 * output: none.
 * type  : private
 */
static void _print_execute_selected_command(print_ui_runtime_s* runtime)
{
    esp_err_t ret = ESP_OK;
    uint8_t cmd_count = 0U;
    uint8_t selected = 0U;
    bool _ready_ = false;

    if (runtime == NULL) {
        return;
    }

    taskENTER_CRITICAL(&s_print_lock);
    cmd_count = runtime->cmd_count;
    selected = runtime->selected_cmd;
    _ready_ = runtime->printer_ready;
    taskEXIT_CRITICAL(&s_print_lock);

    if ((cmd_count == 0U) || (selected >= cmd_count)) {
        return;
    }

    if (!_ready_) {
        ret = _print_try_init_printer(runtime);
        if (ret != ESP_OK) {
            _print_set_action_text(runtime, "Run blocked, init err=%d", (int)ret);
            taskENTER_CRITICAL(&s_print_lock);
            runtime->last_cmd_err = ret;
            runtime->dirty = true;
            taskEXIT_CRITICAL(&s_print_lock);
            return;
        }
    }

    ret = s_print_cmds[selected].exec();

    if ((ret == ESP_OK) && (s_print_cmds[selected].exec == _print_cmd_image)) {
        s_print_image_count += 2U;
    }

    taskENTER_CRITICAL(&s_print_lock);
    runtime->last_cmd_err = ret;
    runtime->dirty = true;
    taskEXIT_CRITICAL(&s_print_lock);

    if (ret == ESP_OK) {
        _print_set_action_text(runtime, "Executed: %s", s_print_cmds[selected].name);
    }
    else {
        _print_set_action_text(
            runtime,
            "Exec fail: %s (%d)",
            s_print_cmds[selected].name,
            (int)ret
        );
    }

    _obtain_print_profile(runtime);
}

/*
 * brief : _print_sync_ui.
 * input : see parameters.
 * output: none.
 * type  : private
 */
static void _print_sync_ui(void* param)
{
    print_ui_runtime_s* runtime = (print_ui_runtime_s*)param;
    bool dirty = false;
    bool _ready_ = false;
    bool status_valid = false;
    uint8_t selected = 0U;
    uint8_t cmd_count = 0U;
    uint8_t temp_c = 0U;
    uint16_t paper_raw = 0U;
    uint16_t voltage_raw = 0U;
    char status_text[PRINT_UI_TEXT_LEN] = { 0 };
    char action_text[PRINT_UI_TEXT_LEN] = { 0 };
    char value_text[32] = { 0 };
    lv_color_t metric_value_color = lv_color_hex(0x7F95A9);
    uint8_t i = 0U;

    if (runtime == NULL) {
        return;
    }

    if (!_print_obj_valid(runtime->temp_value_label)
        || !_print_obj_valid(runtime->paper_value_label)
        || !_print_obj_valid(runtime->voltage_value_label)
        || !_print_obj_valid(runtime->status_title_label)
        || !_print_obj_valid(runtime->status_detail_label)
        || !_print_obj_valid(runtime->action_label)) {
        return;
    }

    taskENTER_CRITICAL(&s_print_lock);
    if (runtime->dirty) {
        _ready_ = runtime->printer_ready;
        status_valid = runtime->status_valid;
        selected = runtime->selected_cmd;
        cmd_count = runtime->cmd_count;
        temp_c = runtime->temperature_celsius;
        paper_raw = runtime->paper_detect_raw;
        voltage_raw = runtime->working_voltage_raw;
        snprintf(status_text, sizeof(status_text), "%s", runtime->status_text);
        snprintf(action_text, sizeof(action_text), "%s", runtime->action_text);
        runtime->dirty = false;
        dirty = true;
    }
    taskEXIT_CRITICAL(&s_print_lock);

    if (!dirty) {
        return;
    }

    lv_label_set_text(runtime->status_title_label, _ready_ ? "Ready" : "Offline");
    _print_apply_status_title_style(runtime->status_title_label, _ready_, status_valid);
    lv_label_set_text(runtime->status_detail_label, status_text);
    lv_label_set_text(runtime->action_label, action_text);

    if (status_valid) {
        metric_value_color =
            (paper_raw == 0U) ? lv_color_hex(0x1D8D35) : lv_color_hex(0xC2651A);

        (void)snprintf(value_text, sizeof(value_text), "%u C", (unsigned)temp_c);
        lv_label_set_text(runtime->temp_value_label, value_text);
        lv_obj_set_style_text_color(runtime->temp_value_label, metric_value_color, 0);

        (void)snprintf(value_text, sizeof(value_text), "%u", (unsigned)paper_raw);
        lv_label_set_text(runtime->paper_value_label, value_text);
        lv_obj_set_style_text_color(runtime->paper_value_label, metric_value_color, 0);

        (void)snprintf(value_text, sizeof(value_text), "%u mV", (unsigned)voltage_raw);
        lv_label_set_text(runtime->voltage_value_label, value_text);
        lv_obj_set_style_text_color(
            runtime->voltage_value_label,
            metric_value_color,
            0
        );
    }
    else {
        lv_label_set_text(runtime->temp_value_label, "--");
        lv_label_set_text(runtime->paper_value_label, "--");
        lv_label_set_text(runtime->voltage_value_label, "--");
        lv_obj_set_style_text_color(
            runtime->temp_value_label,
            lv_color_hex(0x7F95A9),
            0
        );
        lv_obj_set_style_text_color(
            runtime->paper_value_label,
            lv_color_hex(0x7F95A9),
            0
        );
        lv_obj_set_style_text_color(
            runtime->voltage_value_label,
            lv_color_hex(0x7F95A9),
            0
        );
    }

    for (i = 0U; i < PRINT_UI_CMD_COUNT; ++i) {
        if (_print_obj_valid(runtime->command_btn[i])
            && _print_obj_valid(runtime->command_symbol_label[i])
            && _print_obj_valid(runtime->command_name_label[i])) {
            _print_apply_command_tile_style(
                runtime->command_btn[i],
                runtime->command_symbol_label[i],
                runtime->command_name_label[i],
                s_print_cmds[i].color_hex,
                (i < cmd_count) && (i == selected)
            );
        }
    }
}

/*
 * brief : _print_ui_timer_cb.
 * input : see parameters.
 * output: none.
 * type  : private
 */
static void _print_ui_timer_cb(lv_timer_t* timer)
{
    print_ui_runtime_s* runtime = NULL;

    if (timer == NULL) {
        return;
    }

    runtime = (print_ui_runtime_s*)lv_timer_get_user_data(timer);
    _print_sync_ui((void*)runtime);
}

/*
 * brief : _print_ui_task.
 * input : see parameters.
 * output: none.
 * type  : private
 */
static void _print_ui_task(void* param)
{
    print_ui_runtime_s* runtime = (print_ui_runtime_s*)param;
    uint32_t poll_elapsed_ms = PRINT_UI_STATUS_PERIOD_MS;
    uint32_t retry_elapsed_ms = 0U;
    btn_status_e btn_val = Btn_Idle;

    uint16_t alarm_tick_ = 0u;

#ifdef PRINTER_UART_DTR_GPIO
    (void)gpio_set_direction(PRINTER_UART_DTR_GPIO, GPIO_MODE_INPUT);
    (void)gpio_set_pull_mode(PRINTER_UART_DTR_GPIO, GPIO_PULLUP_ONLY);
#endif

    while (1) {
        delay_ms(PRINT_UI_TASK_PERIOD_MS);

        /* scan button state */
        btn_val = button_scan_state(&runtime->button_scan, PRINT_UI_TASK_PERIOD_MS);
        if ((btn_val == Btn_Both_Click) && (runtime->home_cb != NULL)) {
            runtime->home_cb(runtime->home_user_ctx);
        }
        else if (btn_val == Btn_Up_Click) {
            _print_select_command(runtime, false);
        }
        else if (btn_val == Btn_Down_Click) {
            _print_select_command(runtime, true);
        }
        else if ((btn_val == Btn_Up_Hold_Enter) || (btn_val == Btn_Down_Hold_Enter)) {
            _print_execute_selected_command(runtime);
        }

        /* obtain print profile */
        poll_elapsed_ms += PRINT_UI_TASK_PERIOD_MS;
        if (poll_elapsed_ms >= PRINT_UI_STATUS_PERIOD_MS) {
            poll_elapsed_ms = 0U;
            _obtain_print_profile(runtime);
        }

        /* alarm no paper */
        alarm_tick_ += PRINT_UI_TASK_PERIOD_MS;
        if (alarm_tick_ >= 5000) {
            bool status_valid = false;
            bool paper_out = false;

            alarm_tick_ = 0;

            taskENTER_CRITICAL(&s_print_lock);
            status_valid = runtime->status_valid;
            paper_out = (runtime->paper_detect_raw == 0U);
            taskEXIT_CRITICAL(&s_print_lock);

            if (status_valid && paper_out) {
                speaker_alarm_no_paper();
            }
        }

        /* automatic mode handling */
        if (automatic_mode.en) {
            automatic_mode.interval_ms += PRINT_UI_TASK_PERIOD_MS;
            if (automatic_mode.interval_ms >= AUTO_PRINT_INTERVAL_MS) {
                if (runtime->status_valid && runtime->paper_detect_raw) {
                    printer_image_via_bin("dragon.bin");
                    s_print_image_count++;
                    _print_cmd_text();
                }
                automatic_mode.interval_ms = 0;
            }
        }

        /* retry printer initialization */
        taskENTER_CRITICAL(&s_print_lock);
        if (!runtime->printer_ready) {
            retry_elapsed_ms += PRINT_UI_TASK_PERIOD_MS;
        }
        else {
            retry_elapsed_ms = 0U;
        }
        taskEXIT_CRITICAL(&s_print_lock);

        if (retry_elapsed_ms >= PRINT_UI_RETRY_INIT_MS) {
            retry_elapsed_ms = 0U;
            (void)_print_try_init_printer(runtime);
        }
    }
}

/*
 * brief : Create the print page root screen object.
 * input : lcd_w - LCD width; lcd_h - LCD height.
 * output: Created LVGL screen object.
 * type  : public
 */
lv_obj_t* print_create_screen(
    lv_obj_t* parent,
    lv_coord_t area_w,
    lv_coord_t area_h,
    ui_menu_home_cb_t home_cb,
    void* home_user_ctx
)
{
    lv_obj_t* screen = NULL;
    lv_obj_t* frame = NULL;
    lv_obj_t* metrics_panel = NULL;
    lv_obj_t* cmd_panel = NULL;
    lv_obj_t* cmd_grid = NULL;
    lv_obj_t* status_panel = NULL;
    lv_coord_t usable_h = 0;
    lv_coord_t metrics_h = 0;
    lv_coord_t status_h = 0;
    lv_coord_t cmd_h = 0;
    lv_coord_t cmd_min = 0;
    lv_coord_t gap = 0;
    lv_coord_t need = 0;
    BaseType_t task_ok = pdFAIL;
    esp_err_t ret = ESP_OK;
    uint8_t i = 0U;

    static lv_coord_t cmd_col_dsc[] = { LV_GRID_FR(1),
                                        LV_GRID_FR(1),
                                        LV_GRID_TEMPLATE_LAST };
    static lv_coord_t cmd_row_dsc[] = { LV_GRID_FR(1),
                                        LV_GRID_FR(1),
                                        LV_GRID_FR(1),
                                        LV_GRID_TEMPLATE_LAST };

    if (parent == NULL) {
        return NULL;
    }

    if (s_print_runtime.ui_sync_timer != NULL) {
        lv_timer_delete(s_print_runtime.ui_sync_timer);
        s_print_runtime.ui_sync_timer = NULL;
    }

    if (s_print_runtime.task_handle != NULL) {
        vTaskDelete(s_print_runtime.task_handle);
        s_print_runtime.task_handle = NULL;
    }

    (void)printer_deinit();

    memset(&s_print_runtime, 0, sizeof(s_print_runtime));

    s_print_runtime.home_cb = home_cb;
    s_print_runtime.home_user_ctx = home_user_ctx;
    s_print_runtime.button_scan = (btn_scan_s){ 0 };
    s_print_runtime.cmd_count = PRINT_UI_CMD_COUNT;
    s_print_runtime.selected_cmd = 0U;
    s_print_runtime.last_status_err = ESP_OK;
    s_print_runtime.last_cmd_err = ESP_OK;

    _print_set_status_text(&s_print_runtime, "Initializing printer...");
    _print_set_action_text(&s_print_runtime, "Selected: %s", s_print_cmds[0].name);

    usable_h = (lv_coord_t)(area_h - 10);
    if (usable_h < 190) {
        usable_h = 190;
    }

    metrics_h = (usable_h >= 240) ? 78 : 64;
    status_h = (usable_h >= 240) ? 64 : 50;
    cmd_min = (usable_h >= 240) ? 126 : 104;
    gap = (usable_h >= 240) ? 6 : 4;

    cmd_h = (lv_coord_t)(usable_h - metrics_h - status_h - (gap * 2));
    if (cmd_h < cmd_min) {
        need = (lv_coord_t)(cmd_min - cmd_h);
        while ((need > 0) && (metrics_h > 50)) {
            metrics_h--;
            need--;
        }
        while ((need > 0) && (status_h > 48)) {
            status_h--;
            need--;
        }
        cmd_h = (lv_coord_t)(usable_h - metrics_h - status_h - (gap * 2));
        if (cmd_h < 72) {
            cmd_h = 72;
        }
    }

    screen = lv_obj_create(parent);
    lv_obj_set_size(screen, area_w, area_h);
    lv_obj_set_pos(screen, 0, 0);
    lv_obj_set_style_bg_color(screen, lv_color_hex(0xDCEFFF), 0);
    lv_obj_set_style_bg_grad_color(screen, lv_color_hex(0xC2E0FF), 0);
    lv_obj_set_style_bg_grad_dir(screen, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(screen, 0, 0);
    lv_obj_set_style_radius(screen, 0, 0);
    lv_obj_set_style_pad_all(screen, 4, 0);

    frame = lv_obj_create(screen);
    lv_obj_set_size(frame, lv_pct(100), lv_pct(100));
    lv_obj_align(frame, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(frame, lv_color_hex(0xF8FBFF), 0);
    lv_obj_set_style_bg_opa(frame, LV_OPA_90, 0);
    lv_obj_set_style_border_color(frame, lv_color_hex(0x89B6DC), 0);
    lv_obj_set_style_border_width(frame, 1, 0);
    lv_obj_set_style_radius(frame, 10, 0);
    lv_obj_set_style_pad_all(frame, 6, 0);
    lv_obj_clear_flag(frame, LV_OBJ_FLAG_SCROLLABLE);

    metrics_panel = lv_obj_create(frame);
    lv_obj_set_size(metrics_panel, lv_pct(100), metrics_h);
    lv_obj_align(metrics_panel, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(metrics_panel, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(metrics_panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(metrics_panel, lv_color_hex(0xB5CEE3), 0);
    lv_obj_set_style_border_width(metrics_panel, 1, 0);
    lv_obj_set_style_radius(metrics_panel, 10, 0);
    lv_obj_set_style_pad_left(metrics_panel, 6, 0);
    lv_obj_set_style_pad_right(metrics_panel, 6, 0);
    lv_obj_set_style_pad_top(metrics_panel, 4, 0);
    lv_obj_set_style_pad_bottom(metrics_panel, 4, 0);
    lv_obj_clear_flag(metrics_panel, LV_OBJ_FLAG_SCROLLABLE);

    if (_print_create_metric_card(
            metrics_panel,
            LV_ALIGN_LEFT_MID,
            0,
            LV_SYMBOL_WARNING,
            "TPH",
            lv_color_hex(0xF0DDD2),
            lv_color_hex(0xE06A1D),
            true,
            &s_print_runtime.temp_value_label
        )
        == NULL) {
        lv_obj_del(screen);
        return NULL;
    }

    if (_print_create_metric_card(
            metrics_panel,
            LV_ALIGN_CENTER,
            0,
            LV_SYMBOL_FILE,
            "Paper",
            lv_color_hex(0xD8EBD8),
            lv_color_hex(0x1FA53A),
            true,
            &s_print_runtime.paper_value_label
        )
        == NULL) {
        lv_obj_del(screen);
        return NULL;
    }

    if (_print_create_metric_card(
            metrics_panel,
            LV_ALIGN_RIGHT_MID,
            0,
            LV_SYMBOL_POWER,
            "Bat-vol",
            lv_color_hex(0xD9E4F5),
            lv_color_hex(0x2276DA),
            false,
            &s_print_runtime.voltage_value_label
        )
        == NULL) {
        lv_obj_del(screen);
        return NULL;
    }

    cmd_panel = lv_obj_create(frame);
    lv_obj_set_size(cmd_panel, lv_pct(100), cmd_h);
    lv_obj_align(cmd_panel, LV_ALIGN_TOP_MID, 0, (lv_coord_t)(metrics_h + gap));
    lv_obj_set_style_bg_color(cmd_panel, lv_color_hex(0xFF00FF), 0);
    lv_obj_set_style_bg_opa(cmd_panel, LV_OPA_70, 0);
    lv_obj_set_style_border_color(cmd_panel, lv_color_hex(0x9CC4E3), 0);
    lv_obj_set_style_border_width(cmd_panel, 1, 0);
    lv_obj_set_style_radius(cmd_panel, 10, 0);
    lv_obj_set_style_pad_all(cmd_panel, 4, 0);
    lv_obj_clear_flag(cmd_panel, LV_OBJ_FLAG_SCROLLABLE);

    cmd_grid = lv_obj_create(cmd_panel);
    lv_obj_set_size(cmd_grid, lv_pct(100), lv_pct(100));
    lv_obj_align(cmd_grid, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_opa(cmd_grid, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(cmd_grid, 0, 0);
    lv_obj_set_style_pad_all(cmd_grid, 0, 0);
    lv_obj_set_style_pad_row(cmd_grid, 8, 0);
    lv_obj_set_style_pad_column(cmd_grid, 8, 0);
    lv_obj_set_grid_dsc_array(cmd_grid, cmd_col_dsc, cmd_row_dsc);

    for (i = 0U; i < PRINT_UI_CMD_GRID_SLOTS; ++i) {
        lv_obj_t* btn = NULL;
        lv_coord_t row = (lv_coord_t)(i / PRINT_UI_CMD_GRID_COLS);
        lv_coord_t col = (lv_coord_t)(i % PRINT_UI_CMD_GRID_COLS);

        btn = lv_btn_create(cmd_grid);
        lv_obj_set_grid_cell(
            btn,
            LV_GRID_ALIGN_STRETCH,
            col,
            1,
            LV_GRID_ALIGN_STRETCH,
            row,
            1
        );
        lv_obj_set_style_radius(btn, 10, 0);
        lv_obj_set_style_border_width(btn, 1, 0);
        lv_obj_set_style_pad_top(btn, 8, 0);
        lv_obj_set_style_pad_bottom(btn, 8, 0);
        lv_obj_set_style_pad_left(btn, 4, 0);
        lv_obj_set_style_pad_right(btn, 4, 0);
        lv_obj_clear_flag(btn, LV_OBJ_FLAG_CLICKABLE);

        if (i < PRINT_UI_CMD_COUNT) {
            lv_obj_t* symbol_label = lv_label_create(btn);
            lv_obj_t* name_label = lv_label_create(btn);

            lv_label_set_text(symbol_label, s_print_cmds[i].symbol);
            lv_obj_align(symbol_label, LV_ALIGN_TOP_MID, 0, 0);

            lv_label_set_text(name_label, s_print_cmds[i].name);
            lv_obj_set_style_text_font(name_label, &DESKTOP_TEXT_FONT, 0);
            lv_obj_align(name_label, LV_ALIGN_BOTTOM_MID, 0, -2);

            s_print_runtime.command_btn[i] = btn;
            s_print_runtime.command_symbol_label[i] = symbol_label;
            s_print_runtime.command_name_label[i] = name_label;
            _print_apply_command_tile_style(
                btn,
                symbol_label,
                name_label,
                s_print_cmds[i].color_hex,
                (i == s_print_runtime.selected_cmd)
            );
        }
        else {
            lv_obj_set_style_bg_color(btn, lv_color_hex(0xDDEAF6), 0);
            lv_obj_set_style_bg_opa(btn, LV_OPA_30, 0);
            lv_obj_set_style_border_color(btn, lv_color_hex(0xB7CCE0), 0);
            lv_obj_set_style_border_width(btn, 1, 0);
        }
    }

    status_panel = lv_obj_create(frame);
    lv_obj_set_size(status_panel, lv_pct(100), status_h);
    lv_obj_align(
        status_panel,
        LV_ALIGN_TOP_MID,
        0,
        (lv_coord_t)(metrics_h + gap + cmd_h + gap)
    );
    lv_obj_set_style_bg_color(status_panel, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(status_panel, LV_OPA_80, 0);
    lv_obj_set_style_border_color(status_panel, lv_color_hex(0x9CC4E3), 0);
    lv_obj_set_style_border_width(status_panel, 1, 0);
    lv_obj_set_style_radius(status_panel, 10, 0);
    lv_obj_set_style_pad_left(status_panel, 10, 0);
    lv_obj_set_style_pad_right(status_panel, 10, 0);
    lv_obj_set_style_pad_top(status_panel, 8, 0);
    lv_obj_set_style_pad_bottom(status_panel, 8, 0);
    lv_obj_clear_flag(status_panel, LV_OBJ_FLAG_SCROLLABLE);

    s_print_runtime.status_title_label = lv_label_create(status_panel);
    lv_label_set_text(s_print_runtime.status_title_label, "Offline");
    lv_obj_set_style_text_font(
        s_print_runtime.status_title_label,
        &DESKTOP_TEXT_FONT,
        0
    );
    lv_obj_align(s_print_runtime.status_title_label, LV_ALIGN_TOP_LEFT, 0, 0);

    s_print_runtime.status_detail_label = lv_label_create(status_panel);
    lv_obj_set_width(s_print_runtime.status_detail_label, lv_pct(100));
    lv_label_set_long_mode(s_print_runtime.status_detail_label, LV_LABEL_LONG_WRAP);
    lv_label_set_text(s_print_runtime.status_detail_label, "Initializing printer...");
    lv_obj_set_style_text_color(
        s_print_runtime.status_detail_label,
        lv_color_hex(0x123A5A),
        0
    );
    lv_obj_set_style_text_font(s_print_runtime.status_detail_label, LV_FONT_DEFAULT, 0);
    lv_obj_align(s_print_runtime.status_detail_label, LV_ALIGN_TOP_LEFT, 0, 20);

    s_print_runtime.action_label = lv_label_create(status_panel);
    lv_obj_set_width(s_print_runtime.action_label, lv_pct(100));
    lv_label_set_long_mode(s_print_runtime.action_label, LV_LABEL_LONG_WRAP);
    lv_label_set_text(s_print_runtime.action_label, "Selected: --");
    lv_obj_set_style_text_color(
        s_print_runtime.action_label,
        lv_color_hex(0x123A5A),
        0
    );
    lv_obj_set_style_text_font(s_print_runtime.action_label, LV_FONT_DEFAULT, 0);
    lv_obj_align(s_print_runtime.action_label, LV_ALIGN_BOTTOM_LEFT, 0, 0);

    s_print_runtime.ui_sync_timer =
        lv_timer_create(_print_ui_timer_cb, PRINT_UI_TASK_PERIOD_MS, &s_print_runtime);
    if (s_print_runtime.ui_sync_timer == NULL) {
        lv_obj_del(screen);
        ESP_LOGE(TAG, "lv_timer_create failed");
        return NULL;
    }

    s_print_runtime.root = screen;
    s_print_runtime.dirty = true;

    ret = _print_try_init_printer(&s_print_runtime);
    if (ret == ESP_OK) {
        _obtain_print_profile(&s_print_runtime);
    }
    _print_sync_ui(&s_print_runtime);

    task_ok = xTaskCreate(
        _print_ui_task,
        "print_ui",
        PRINT_UI_TASK_STACK_SIZE,
        &s_print_runtime,
        5,
        &s_print_runtime.task_handle
    );
    if (task_ok != pdPASS) {
        s_print_runtime.task_handle = NULL;
        if (s_print_runtime.ui_sync_timer != NULL) {
            lv_timer_delete(s_print_runtime.ui_sync_timer);
            s_print_runtime.ui_sync_timer = NULL;
        }
        (void)printer_deinit();
        lv_obj_del(screen);
        ESP_LOGE(TAG, "xTaskCreate failed");
        return NULL;
    }

    return screen;
}

/*
 * brief : print_destroy_screen.
 * input : see parameters.
 * output: none.
 * type  : public
 */
void print_destroy_screen(lv_obj_t* screen)
{
    esp_err_t ret = ESP_OK;

    if (s_print_runtime.ui_sync_timer != NULL) {
        lv_timer_delete(s_print_runtime.ui_sync_timer);
        s_print_runtime.ui_sync_timer = NULL;
    }

    if (s_print_runtime.task_handle != NULL) {
        vTaskDelete(s_print_runtime.task_handle);
        s_print_runtime.task_handle = NULL;
    }

    ret = printer_deinit();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "printer_deinit failed: %d", (int)ret);
    }

    if ((screen != NULL) && lv_obj_is_valid(screen)) {
        lv_obj_del(screen);
    }

    memset(&s_print_runtime, 0, sizeof(s_print_runtime));
}