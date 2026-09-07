#include "print_ui.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "user/device/dev_zzjx2r.h"

#define TAG "print_ui"

typedef esp_err_t (*print_ui_cmd_fn_t)(void);

typedef struct {
    const char* name;
    print_ui_cmd_fn_t exec;
} print_ui_cmd_s;

static print_ui_runtime_s s_print_runtime;
static portMUX_TYPE s_print_lock = portMUX_INITIALIZER_UNLOCKED;

/*
 * brief : _print_cmd_clear.
 * input : none.
 * output: return value from this function.
 * type  : private
 */
static esp_err_t _print_cmd_clear(void)
{
    return zzjx2r_cmd_clear_printer();
}

/*
 * brief : _print_cmd_feed_1_line.
 * input : none.
 * output: return value from this function.
 * type  : private
 */
static esp_err_t _print_cmd_feed_1_line(void)
{
    return zzjx2r_cmd_print_and_feed_lines(1U);
}

/*
 * brief : _print_cmd_feed_3_lines.
 * input : none.
 * output: return value from this function.
 * type  : private
 */
static esp_err_t _print_cmd_feed_3_lines(void)
{
    return zzjx2r_cmd_print_and_feed_lines(3U);
}

/*
 * brief : _print_cmd_print_sample_text.
 * input : none.
 * output: return value from this function.
 * type  : private
 */
static esp_err_t _print_cmd_print_sample_text(void)
{
    esp_err_t ret = ESP_OK;

    ret = zzjx2r_cmd_set_justification(ZZJX2R_JUSTIFY_LEFT);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = zzjx2r_write_line("--- PRINTER TEST ---");
    if (ret != ESP_OK) {
        return ret;
    }

    ret = zzjx2r_write_line("Status path is running.");
    if (ret != ESP_OK) {
        return ret;
    }

    return zzjx2r_cmd_print_and_feed_lines(2U);
}

/*
 * brief : _print_cmd_style_demo.
 * input : none.
 * output: return value from this function.
 * type  : private
 */
static esp_err_t _print_cmd_style_demo(void)
{
    esp_err_t ret = ESP_OK;

    ret = zzjx2r_cmd_set_bold(true);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = zzjx2r_write_line("BOLD TEXT SAMPLE");
    if (ret != ESP_OK) {
        (void)zzjx2r_cmd_set_bold(false);
        return ret;
    }

    ret = zzjx2r_cmd_set_bold(false);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = zzjx2r_cmd_set_underline(ZZJX2R_UNDERLINE_THIN);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = zzjx2r_write_line("Underline sample");
    (void)zzjx2r_cmd_set_underline(ZZJX2R_UNDERLINE_OFF);
    if (ret != ESP_OK) {
        return ret;
    }

    return zzjx2r_cmd_print_and_feed_lines(1U);
}

static const print_ui_cmd_s s_print_cmds[PRINT_UI_CMD_COUNT] = {
    { "Clear printer", _print_cmd_clear },
    { "Feed 1 line", _print_cmd_feed_1_line },
    { "Feed 3 lines", _print_cmd_feed_3_lines },
    { "Print sample text", _print_cmd_print_sample_text },
    { "Print style demo", _print_cmd_style_demo },
};

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

    ret = zzjx2r_init(NULL);

    taskENTER_CRITICAL(&s_print_lock);
    runtime->printer_ready = (ret == ESP_OK);
    runtime->status_valid = false;
    runtime->last_status_err = ret;
    runtime->dirty = true;
    taskEXIT_CRITICAL(&s_print_lock);

    if (ret == ESP_OK) {
        _print_set_status_text(runtime, "Printer initialized");
    } else {
        _print_set_status_text(runtime, "Init failed: err=%d", (int)ret);
    }

    return ret;
}

/*
 * brief : _print_poll_printer_status.
 * input : see parameters.
 * output: none.
 * type  : private
 */
static void _print_poll_printer_status(print_ui_runtime_s* runtime)
{
    esp_err_t ret = ESP_FAIL;
    zzjx2r_detect_status_t status = { 0 };

    if (runtime == NULL) {
        return;
    }

    ret = zzjx2r_cmd_detect_status(&status);
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
    } else {
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
        } else {
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
    bool printer_ready = false;

    if (runtime == NULL) {
        return;
    }

    taskENTER_CRITICAL(&s_print_lock);
    cmd_count = runtime->cmd_count;
    selected = runtime->selected_cmd;
    printer_ready = runtime->printer_ready;
    taskEXIT_CRITICAL(&s_print_lock);

    if ((cmd_count == 0U) || (selected >= cmd_count)) {
        return;
    }

    if (!printer_ready) {
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

    taskENTER_CRITICAL(&s_print_lock);
    runtime->last_cmd_err = ret;
    runtime->dirty = true;
    taskEXIT_CRITICAL(&s_print_lock);

    if (ret == ESP_OK) {
        _print_set_action_text(runtime, "Executed: %s", s_print_cmds[selected].name);
    } else {
        _print_set_action_text(
            runtime,
            "Exec fail: %s (%d)",
            s_print_cmds[selected].name,
            (int)ret
        );
    }

    _print_poll_printer_status(runtime);
}

/*
 * brief : _print_apply_indicator_style.
 * input : see parameters.
 * output: none.
 * type  : private
 */
static void
_print_apply_indicator_style(lv_obj_t* indicator, bool ready, bool status_valid)
{
    if (!_print_obj_valid(indicator)) {
        return;
    }

    lv_obj_set_style_radius(indicator, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_shadow_width(indicator, 0, 0);
    lv_obj_set_style_shadow_opa(indicator, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(indicator, 2, 0);

    if (!ready) {
        lv_obj_set_style_bg_color(indicator, lv_color_hex(0xF85A68), 0);
        lv_obj_set_style_border_color(indicator, lv_color_hex(0xF85A68), 0);
        lv_obj_set_style_shadow_color(indicator, lv_color_hex(0xF85A68), 0);
        lv_obj_set_style_shadow_width(indicator, 12, 0);
        lv_obj_set_style_shadow_opa(indicator, LV_OPA_50, 0);
        return;
    }

    if (status_valid) {
        lv_obj_set_style_bg_color(indicator, lv_color_hex(0x53E37C), 0);
        lv_obj_set_style_border_color(indicator, lv_color_hex(0x2AAE52), 0);
        lv_obj_set_style_shadow_color(indicator, lv_color_hex(0x53E37C), 0);
        lv_obj_set_style_shadow_width(indicator, 12, 0);
        lv_obj_set_style_shadow_opa(indicator, LV_OPA_50, 0);
        return;
    }

    lv_obj_set_style_bg_color(indicator, lv_color_hex(0xFFC04D), 0);
    lv_obj_set_style_border_color(indicator, lv_color_hex(0xD4941F), 0);
    lv_obj_set_style_shadow_color(indicator, lv_color_hex(0xFFC04D), 0);
    lv_obj_set_style_shadow_width(indicator, 10, 0);
    lv_obj_set_style_shadow_opa(indicator, LV_OPA_40, 0);
}

/*
 * brief : _print_apply_command_style.
 * input : see parameters.
 * output: none.
 * type  : private
 */
static void _print_apply_command_style(lv_obj_t* label, bool selected)
{
    if (!_print_obj_valid(label)) {
        return;
    }

    lv_obj_set_style_radius(label, 5, 0);
    lv_obj_set_style_pad_left(label, 6, 0);
    lv_obj_set_style_pad_right(label, 6, 0);
    lv_obj_set_style_pad_top(label, 3, 0);
    lv_obj_set_style_pad_bottom(label, 3, 0);

    if (selected) {
        lv_obj_set_style_bg_opa(label, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(label, lv_color_hex(0x274969), 0);
        lv_obj_set_style_text_color(label, lv_color_hex(0xF6FBFF), 0);
    } else {
        lv_obj_set_style_bg_opa(label, LV_OPA_30, 0);
        lv_obj_set_style_bg_color(label, lv_color_hex(0x10273D), 0);
        lv_obj_set_style_text_color(label, lv_color_hex(0x9FD9FF), 0);
    }
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
    bool printer_ready = false;
    bool status_valid = false;
    uint8_t selected = 0U;
    uint8_t cmd_count = 0U;
    uint8_t temp_c = 0U;
    uint16_t paper_raw = 0U;
    uint16_t voltage_raw = 0U;
    char status_text[PRINT_UI_TEXT_LEN] = { 0 };
    char action_text[PRINT_UI_TEXT_LEN] = { 0 };
    char text_buf[64] = { 0 };
    uint8_t i = 0U;

    if (runtime == NULL) {
        return;
    }

    if (!_print_obj_valid(runtime->status_label)
        || !_print_obj_valid(runtime->state_indicator)
        || !_print_obj_valid(runtime->temp_value_label)
        || !_print_obj_valid(runtime->paper_value_label)
        || !_print_obj_valid(runtime->voltage_value_label)
        || !_print_obj_valid(runtime->action_label)) {
        return;
    }

    taskENTER_CRITICAL(&s_print_lock);
    if (runtime->dirty) {
        printer_ready = runtime->printer_ready;
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

    lv_label_set_text(runtime->status_label, status_text);
    _print_apply_indicator_style(runtime->state_indicator, printer_ready, status_valid);

    if (status_valid) {
        (void)snprintf(text_buf, sizeof(text_buf), "%u C", (unsigned)temp_c);
        lv_label_set_text(runtime->temp_value_label, text_buf);

        (void)snprintf(text_buf, sizeof(text_buf), "%u", (unsigned)paper_raw);
        lv_label_set_text(runtime->paper_value_label, text_buf);

        (void)snprintf(text_buf, sizeof(text_buf), "%u", (unsigned)voltage_raw);
        lv_label_set_text(runtime->voltage_value_label, text_buf);
    } else {
        lv_label_set_text(runtime->temp_value_label, "--");
        lv_label_set_text(runtime->paper_value_label, "--");
        lv_label_set_text(runtime->voltage_value_label, "--");
    }

    lv_label_set_text(runtime->action_label, action_text);

    for (i = 0U; i < PRINT_UI_CMD_COUNT; ++i) {
        if (!_print_obj_valid(runtime->command_labels[i])) {
            continue;
        }

        if (i < cmd_count) {
            (void)snprintf(
                text_buf,
                sizeof(text_buf),
                "%c %s",
                (i == selected) ? '>' : ' ',
                s_print_cmds[i].name
            );
            lv_label_set_text(runtime->command_labels[i], text_buf);
            _print_apply_command_style(runtime->command_labels[i], (i == selected));
        } else {
            lv_label_set_text(runtime->command_labels[i], "");
            _print_apply_command_style(runtime->command_labels[i], false);
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
 * brief : _print_create_metric_card.
 * input : see parameters.
 * output: return value from this function.
 * type  : private
 */
static lv_obj_t* _print_create_metric_card(
    lv_obj_t* parent,
    lv_align_t align,
    lv_coord_t x_ofs,
    const char* title,
    lv_obj_t** out_value_label
)
{
    lv_obj_t* card = NULL;
    lv_obj_t* title_label = NULL;
    lv_obj_t* value_label = NULL;

    if ((parent == NULL) || (title == NULL) || (out_value_label == NULL)) {
        return NULL;
    }

    card = lv_obj_create(parent);
    lv_obj_set_size(card, lv_pct(32), lv_pct(100));
    lv_obj_align(card, align, x_ofs, 0);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x0F2B43), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(card, lv_color_hex(0x2E6B95), 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_radius(card, 7, 0);
    lv_obj_set_style_pad_all(card, 4, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    title_label = lv_label_create(card);
    lv_label_set_text(title_label, title);
    lv_obj_set_style_text_color(title_label, lv_color_hex(0x83D0FF), 0);
    lv_obj_set_style_text_font(title_label, &DESKTOP_TEXT_FONT, 0);
    lv_obj_align(title_label, LV_ALIGN_TOP_LEFT, 0, 0);

    value_label = lv_label_create(card);
    lv_label_set_text(value_label, "--");
    lv_obj_set_style_text_color(value_label, lv_color_hex(0xF2FBFF), 0);
    lv_obj_set_style_text_font(value_label, &DESKTOP_TEXT_FONT, 0);
    lv_obj_align(value_label, LV_ALIGN_BOTTOM_LEFT, 0, 0);

    *out_value_label = value_label;
    return card;
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

    while (1) {
        btn_val = button_scan_state(&runtime->button_scan, PRINT_UI_TASK_PERIOD_MS);
        if ((btn_val == Btn_Both_Click) && (runtime->home_cb != NULL)) {
            runtime->home_cb(runtime->home_user_ctx);
        } else if (btn_val == Btn_Up_Click) {
            _print_select_command(runtime, false);
        } else if (btn_val == Btn_Down_Click) {
            _print_select_command(runtime, true);
        } else if ((btn_val == Btn_Up_Hold_Enter) || (btn_val == Btn_Down_Hold_Enter)) {
            _print_execute_selected_command(runtime);
        }

        poll_elapsed_ms += PRINT_UI_TASK_PERIOD_MS;
        if (poll_elapsed_ms >= PRINT_UI_STATUS_PERIOD_MS) {
            poll_elapsed_ms = 0U;
            _print_poll_printer_status(runtime);
        }

        taskENTER_CRITICAL(&s_print_lock);
        if (!runtime->printer_ready) {
            retry_elapsed_ms += PRINT_UI_TASK_PERIOD_MS;
        } else {
            retry_elapsed_ms = 0U;
        }
        taskEXIT_CRITICAL(&s_print_lock);

        if (retry_elapsed_ms >= PRINT_UI_RETRY_INIT_MS) {
            retry_elapsed_ms = 0U;
            (void)_print_try_init_printer(runtime);
        }

        delay_ms(PRINT_UI_TASK_PERIOD_MS);
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
    lv_obj_t* header = NULL;
    lv_obj_t* title_label = NULL;
    lv_obj_t* metric_panel = NULL;
    lv_obj_t* command_panel = NULL;
    lv_obj_t* footer_panel = NULL;
    lv_obj_t* hint_label = NULL;
    lv_coord_t usable_h = 0;
    lv_coord_t header_h = 42;
    lv_coord_t metric_h = 62;
    lv_coord_t footer_h = 44;
    lv_coord_t gap = 4;
    lv_coord_t cmd_h = 90;
    lv_coord_t row_h = 20;
    lv_coord_t row_y = 0;
    BaseType_t task_ok = pdFAIL;
    esp_err_t ret = ESP_OK;
    uint8_t i = 0U;

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

    (void)zzjx2r_deinit();

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

    screen = lv_obj_create(parent);
    lv_obj_set_size(screen, area_w, area_h);
    lv_obj_set_pos(screen, 0, 0);
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x10273E), 0);
    lv_obj_set_style_bg_grad_color(screen, lv_color_hex(0x274968), 0);
    lv_obj_set_style_bg_grad_dir(screen, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(screen, 0, 0);
    lv_obj_set_style_radius(screen, 0, 0);
    lv_obj_set_style_pad_all(screen, 2, 0);

    frame = lv_obj_create(screen);
    lv_obj_set_size(frame, lv_pct(100), lv_pct(100));
    lv_obj_align(frame, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(frame, lv_color_hex(0x0D1C2B), 0);
    lv_obj_set_style_bg_opa(frame, LV_OPA_70, 0);
    lv_obj_set_style_border_color(frame, lv_color_hex(0x65B9F0), 0);
    lv_obj_set_style_border_width(frame, 1, 0);
    lv_obj_set_style_radius(frame, 8, 0);
    lv_obj_set_style_pad_all(frame, 3, 0);
    lv_obj_clear_flag(frame, LV_OBJ_FLAG_SCROLLABLE);

    usable_h = (lv_coord_t)(area_h - 10);
    if (usable_h < 150) {
        usable_h = 150;
    }

    if (usable_h < 210) {
        header_h = 38;
        metric_h = 54;
        footer_h = 40;
        gap = 3;
    }

    cmd_h = (lv_coord_t)(usable_h - header_h - metric_h - footer_h - (gap * 3));
    if (cmd_h < 70) {
        cmd_h = 70;
    }

    header = lv_obj_create(frame);
    lv_obj_set_size(header, lv_pct(100), header_h);
    lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(header, lv_color_hex(0x15324A), 0);
    lv_obj_set_style_bg_opa(header, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(header, lv_color_hex(0x66C7FF), 0);
    lv_obj_set_style_border_width(header, 1, 0);
    lv_obj_set_style_radius(header, 6, 0);
    lv_obj_set_style_pad_left(header, 8, 0);
    lv_obj_set_style_pad_right(header, 8, 0);
    lv_obj_set_style_pad_top(header, 4, 0);
    lv_obj_set_style_pad_bottom(header, 4, 0);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);

    title_label = lv_label_create(header);
    lv_label_set_text(title_label, "THERMAL PRINTER");
    lv_obj_set_style_text_color(title_label, lv_color_hex(0xEAF7FF), 0);
    lv_obj_set_style_text_font(title_label, &DESKTOP_TEXT_FONT, 0);
    lv_obj_align(title_label, LV_ALIGN_TOP_LEFT, 0, 0);

    s_print_runtime.status_label = lv_label_create(header);
    lv_label_set_text(s_print_runtime.status_label, "Status: --");
    lv_obj_set_style_text_color(
        s_print_runtime.status_label,
        lv_color_hex(0x9FDDFF),
        0
    );
    lv_obj_set_style_text_font(s_print_runtime.status_label, &DESKTOP_TEXT_FONT, 0);
    lv_obj_align(s_print_runtime.status_label, LV_ALIGN_BOTTOM_LEFT, 0, 0);

    s_print_runtime.state_indicator = lv_obj_create(header);
    lv_obj_set_size(s_print_runtime.state_indicator, 16, 16);
    lv_obj_align(s_print_runtime.state_indicator, LV_ALIGN_RIGHT_MID, -2, 0);
    lv_obj_set_style_pad_all(s_print_runtime.state_indicator, 0, 0);
    lv_obj_clear_flag(s_print_runtime.state_indicator, LV_OBJ_FLAG_SCROLLABLE);

    metric_panel = lv_obj_create(frame);
    lv_obj_set_size(metric_panel, lv_pct(100), metric_h);
    lv_obj_align(metric_panel, LV_ALIGN_TOP_MID, 0, (lv_coord_t)(header_h + gap));
    lv_obj_set_style_bg_color(metric_panel, lv_color_hex(0x132A40), 0);
    lv_obj_set_style_bg_opa(metric_panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(metric_panel, lv_color_hex(0x4AA8E0), 0);
    lv_obj_set_style_border_width(metric_panel, 1, 0);
    lv_obj_set_style_radius(metric_panel, 6, 0);
    lv_obj_set_style_pad_all(metric_panel, 3, 0);
    lv_obj_clear_flag(metric_panel, LV_OBJ_FLAG_SCROLLABLE);

    if (_print_create_metric_card(
            metric_panel,
            LV_ALIGN_LEFT_MID,
            0,
            "Temp",
            &s_print_runtime.temp_value_label
        )
        == NULL) {
        lv_obj_del(screen);
        return NULL;
    }

    if (_print_create_metric_card(
            metric_panel,
            LV_ALIGN_CENTER,
            0,
            "Paper",
            &s_print_runtime.paper_value_label
        )
        == NULL) {
        lv_obj_del(screen);
        return NULL;
    }

    if (_print_create_metric_card(
            metric_panel,
            LV_ALIGN_RIGHT_MID,
            0,
            "Voltage",
            &s_print_runtime.voltage_value_label
        )
        == NULL) {
        lv_obj_del(screen);
        return NULL;
    }

    command_panel = lv_obj_create(frame);
    lv_obj_set_size(command_panel, lv_pct(100), cmd_h);
    lv_obj_align(
        command_panel,
        LV_ALIGN_TOP_MID,
        0,
        (lv_coord_t)(header_h + gap + metric_h + gap)
    );
    lv_obj_set_style_bg_color(command_panel, lv_color_hex(0x11243A), 0);
    lv_obj_set_style_bg_opa(command_panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(command_panel, lv_color_hex(0x4AA8E0), 0);
    lv_obj_set_style_border_width(command_panel, 1, 0);
    lv_obj_set_style_radius(command_panel, 6, 0);
    lv_obj_set_style_pad_left(command_panel, 4, 0);
    lv_obj_set_style_pad_right(command_panel, 4, 0);
    lv_obj_set_style_pad_top(command_panel, 4, 0);
    lv_obj_set_style_pad_bottom(command_panel, 4, 0);
    lv_obj_clear_flag(command_panel, LV_OBJ_FLAG_SCROLLABLE);

    title_label = lv_label_create(command_panel);
    lv_label_set_text(title_label, "Test Commands");
    lv_obj_set_style_text_color(title_label, lv_color_hex(0xCDEEFF), 0);
    lv_obj_set_style_text_font(title_label, &DESKTOP_TEXT_FONT, 0);
    lv_obj_align(title_label, LV_ALIGN_TOP_LEFT, 0, 0);

    row_h = (lv_coord_t)((cmd_h - 20) / PRINT_UI_CMD_COUNT);
    if (row_h < 16) {
        row_h = 16;
    }
    row_y = 18;

    for (i = 0U; i < PRINT_UI_CMD_COUNT; ++i) {
        s_print_runtime.command_labels[i] = lv_label_create(command_panel);
        lv_obj_set_width(s_print_runtime.command_labels[i], lv_pct(100));
        lv_obj_set_height(s_print_runtime.command_labels[i], row_h);
        lv_obj_set_pos(s_print_runtime.command_labels[i], 0, row_y);
        lv_obj_set_style_text_font(
            s_print_runtime.command_labels[i],
            &DESKTOP_TEXT_FONT,
            0
        );
        lv_label_set_text(s_print_runtime.command_labels[i], "");
        row_y = (lv_coord_t)(row_y + row_h + 1);
    }

    footer_panel = lv_obj_create(frame);
    lv_obj_set_size(footer_panel, lv_pct(100), footer_h);
    lv_obj_align(
        footer_panel,
        LV_ALIGN_TOP_MID,
        0,
        (lv_coord_t)(header_h + gap + metric_h + gap + cmd_h + gap)
    );
    lv_obj_set_style_bg_color(footer_panel, lv_color_hex(0x15324A), 0);
    lv_obj_set_style_bg_opa(footer_panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(footer_panel, lv_color_hex(0x66C7FF), 0);
    lv_obj_set_style_border_width(footer_panel, 1, 0);
    lv_obj_set_style_radius(footer_panel, 6, 0);
    lv_obj_set_style_pad_left(footer_panel, 6, 0);
    lv_obj_set_style_pad_right(footer_panel, 6, 0);
    lv_obj_set_style_pad_top(footer_panel, 3, 0);
    lv_obj_set_style_pad_bottom(footer_panel, 3, 0);
    lv_obj_clear_flag(footer_panel, LV_OBJ_FLAG_SCROLLABLE);

    hint_label = lv_label_create(footer_panel);
    lv_obj_set_width(hint_label, lv_pct(100));
    lv_label_set_long_mode(hint_label, LV_LABEL_LONG_WRAP);
    lv_label_set_text(hint_label, "UP/DOWN click: select  HOLD: run  BOTH click: exit");
    lv_obj_set_style_text_color(hint_label, lv_color_hex(0x8EDCFF), 0);
    lv_obj_set_style_text_font(hint_label, &DESKTOP_TEXT_FONT, 0);
    lv_obj_align(hint_label, LV_ALIGN_TOP_LEFT, 0, 0);

    s_print_runtime.action_label = lv_label_create(footer_panel);
    lv_obj_set_width(s_print_runtime.action_label, lv_pct(100));
    lv_label_set_long_mode(s_print_runtime.action_label, LV_LABEL_LONG_WRAP);
    lv_label_set_text(s_print_runtime.action_label, "Action: --");
    lv_obj_set_style_text_color(
        s_print_runtime.action_label,
        lv_color_hex(0xF4FCFF),
        0
    );
    lv_obj_set_style_text_font(s_print_runtime.action_label, &DESKTOP_TEXT_FONT, 0);
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
    _print_apply_indicator_style(s_print_runtime.state_indicator, false, false);

    ret = _print_try_init_printer(&s_print_runtime);
    if (ret == ESP_OK) {
        _print_poll_printer_status(&s_print_runtime);
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
        (void)zzjx2r_deinit();
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

    ret = zzjx2r_deinit();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "zzjx2r_deinit failed: %d", (int)ret);
    }

    if ((screen != NULL) && lv_obj_is_valid(screen)) {
        lv_obj_del(screen);
    }

    memset(&s_print_runtime, 0, sizeof(s_print_runtime));
}