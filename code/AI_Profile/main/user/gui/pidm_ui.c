#include "pidm_ui.h"

#define TAG "pidm_ui"

static pidm_ui_runtime_s s_pidm_ui_runtime;

#if LV_USE_LINE != 0
typedef struct {
    lv_point_precise_t points[PIDM_UI_SCOPE_POINT_COUNT];
    int32_t history[PIDM_UI_SCOPE_POINT_COUNT];
} pidm_scope_buffer_s;

static pidm_scope_buffer_s* s_pidm_scope_buffer = NULL;

static bool _pidm_scope_alloc_buffer(void);
static void _pidm_scope_free_buffer(void);
static void _pidm_scope_reset_samples(lv_coord_t plot_w, lv_coord_t plot_h);
static void _pidm_scope_push_sample(pidm_ui_runtime_s* runtime, int32_t value);
#endif

static void _pidm_ui_refresh(pidm_ui_runtime_s* runtime);

#if LV_USE_LINE != 0
/*
 * brief : Allocate the PIDM oscilloscope working buffer.
 * input : none.
 * output: true when the PSRAM buffer is available.
 * type  : private
 * theory: keep points and history in one PSRAM allocation to reduce internal RAM use and fragmentation.
 */
static bool _pidm_scope_alloc_buffer(void) {
    if (s_pidm_scope_buffer != NULL) {
        return true;
    }

    s_pidm_scope_buffer = (pidm_scope_buffer_s*)heap_caps_calloc(
        1,
        sizeof(pidm_scope_buffer_s),
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (s_pidm_scope_buffer == NULL) {
        ESP_LOGE(TAG, "scope buffer alloc in PSRAM failed");
        return false;
    }

    return true;
}

/*
 * brief : Release the PIDM oscilloscope working buffer.
 * input : none.
 * output: none.
 * type  : private
 * theory: return the single PSRAM block when the page closes and clear its owner pointer.
 */
static void _pidm_scope_free_buffer(void) {
    if (s_pidm_scope_buffer != NULL) {
        heap_caps_free(s_pidm_scope_buffer);
        s_pidm_scope_buffer = NULL;
    }
}

/*
 * brief : Convert an ADC reading into the oscilloscope Y coordinate.
 * input : value - raw ADC value; plot_h - drawable height.
 * output: mapped Y coordinate.
 * type  : private
 * theory: clamp the 12-bit ADC range before applying a linear map to the plot height.
 */
static lv_coord_t _pidm_scope_value_to_y(int32_t value, lv_coord_t plot_h) {
    int32_t clamped = value;
    int32_t range = PIDM_UI_SCOPE_Y_MAX_ADC - PIDM_UI_SCOPE_Y_MIN_ADC;

    if (plot_h <= 1) {
        return 0;
    }
    if (clamped < PIDM_UI_SCOPE_Y_MIN_ADC) {
        clamped = PIDM_UI_SCOPE_Y_MIN_ADC;
    }
    else if (clamped > PIDM_UI_SCOPE_Y_MAX_ADC) {
        clamped = PIDM_UI_SCOPE_Y_MAX_ADC;
    }

    return (lv_coord_t)(((clamped - PIDM_UI_SCOPE_Y_MIN_ADC) * (plot_h - 1)) / range);
}

/*
 * brief : Reset oscilloscope history and point coordinates.
 * input : plot_w - drawable width; plot_h - drawable height.
 * output: none.
 * type  : private
 * theory: initialize a stable zero trace and precompute X positions once per page open.
 */
static void _pidm_scope_reset_samples(lv_coord_t plot_w, lv_coord_t plot_h) {
    uint32_t i = 0U;

    if (s_pidm_scope_buffer == NULL) {
        return;
    }
    if (plot_w < 2) {
        plot_w = 2;
    }
    if (plot_h < 2) {
        plot_h = 2;
    }

    for (i = 0U; i < PIDM_UI_SCOPE_POINT_COUNT; ++i) {
        s_pidm_scope_buffer->history[i] = 0;
        s_pidm_scope_buffer->points[i].x =
            (lv_coord_t)((i * (uint32_t)(plot_w - 1))
                         / (PIDM_UI_SCOPE_POINT_COUNT - 1U));
        s_pidm_scope_buffer->points[i].y = 0;
    }
}

/*
 * brief : Append one ADC sample to the oscilloscope trace.
 * input : runtime - PIDM UI state; value - raw ADC value.
 * output: none.
 * type  : private
 * theory: shift the fixed PSRAM history in place and reuse the same LVGL point array.
 */
static void _pidm_scope_push_sample(pidm_ui_runtime_s* runtime, int32_t value) {
    uint32_t i = 0U;

    if ((runtime == NULL) || (runtime->scope_line == NULL)
        || (s_pidm_scope_buffer == NULL) || (runtime->scope_plot_h < 2)) {
        return;
    }

    if (value < PIDM_UI_SCOPE_Y_MIN_ADC) {
        value = PIDM_UI_SCOPE_Y_MIN_ADC;
    }
    else if (value > PIDM_UI_SCOPE_Y_MAX_ADC) {
        value = PIDM_UI_SCOPE_Y_MAX_ADC;
    }

    for (i = 0U; i < (PIDM_UI_SCOPE_POINT_COUNT - 1U); ++i) {
        s_pidm_scope_buffer->history[i] = s_pidm_scope_buffer->history[i + 1U];
    }
    s_pidm_scope_buffer->history[PIDM_UI_SCOPE_POINT_COUNT - 1U] = value;

    for (i = 0U; i < PIDM_UI_SCOPE_POINT_COUNT; ++i) {
        s_pidm_scope_buffer->points[i].y = _pidm_scope_value_to_y(
            s_pidm_scope_buffer->history[i],
            runtime->scope_plot_h);
    }

    lv_obj_invalidate(runtime->scope_line);
}
#endif

/*
 * brief : Update one read-only PIDM text edit.
 * input : edit - textarea object; text - new text.
 * output: none.
 * type  : private
 * theory: update existing widgets in place so periodic refreshes do not allocate replacement objects.
 */
static void _pidm_set_edit_text(lv_obj_t* edit, const char* text) {
    if ((edit != NULL) && (text != NULL)) {
        lv_textarea_set_text(edit, text);
    }
}

/*
 * brief : Refresh PIDM text fields and oscilloscope trace.
 * input : runtime - PIDM UI state.
 * output: none.
 * type  : private
 * theory: copy one device snapshot in LVGL context, then update all widgets from that coherent view.
 */
static void _pidm_ui_refresh(pidm_ui_runtime_s* runtime) {
    pidm_profile_s profile = { 0 };
    char text[24] = { 0 };
    const char* status = "OFFLINE";
    esp_err_t ret = ESP_OK;

    if (runtime == NULL) {
        return;
    }

    ret = pidm_read_profile(&profile);
    if (profile.adc_valid) {
        snprintf(text, sizeof(text), "%lu", (unsigned long)profile.adc_latest);
        _pidm_set_edit_text(runtime->latest_edit, text);
        snprintf(text, sizeof(text), "%lu", (unsigned long)profile.adc_average);
        _pidm_set_edit_text(runtime->average_edit, text);
        snprintf(text, sizeof(text), "%lu", (unsigned long)profile.adc_sample_count);
        _pidm_set_edit_text(runtime->samples_edit, text);
#if LV_USE_LINE != 0
        _pidm_scope_push_sample(runtime, (int32_t)profile.adc_latest);
#endif
    }
    else {
        _pidm_set_edit_text(runtime->latest_edit, "--");
        _pidm_set_edit_text(runtime->average_edit, "--");
        _pidm_set_edit_text(runtime->samples_edit, "0");
    }

    snprintf(text, sizeof(text), "%lu", (unsigned long)profile.trigger_count);
    _pidm_set_edit_text(runtime->triggers_edit, text);
    snprintf(text, sizeof(text), "%lu us", (unsigned long)profile.pulse_width_us);
    _pidm_set_edit_text(runtime->pulse_edit, text);

    if (profile.ready && (ret == ESP_OK) && (profile.trigger_error == ESP_OK)) {
        status = "RUNNING";
    }
    else if (profile.ready
             && ((ret == ESP_ERR_NOT_FOUND) || (ret == ESP_ERR_TIMEOUT))) {
        status = "ADC WAIT";
    }
    else if (profile.ready) {
        status = "ERROR";
    }
    _pidm_set_edit_text(runtime->status_edit, status);
}

/*
 * brief : Handle periodic PIDM UI synchronization.
 * input : timer - LVGL timer carrying the PIDM runtime pointer.
 * output: none.
 * type  : private
 * theory: perform every LVGL object mutation from the LVGL timer context.
 */
static void _pidm_ui_timer_cb(lv_timer_t* timer) {
    pidm_ui_runtime_s* runtime = NULL;

    if (timer == NULL) {
        return;
    }

    runtime = (pidm_ui_runtime_s*)lv_timer_get_user_data(timer);
    _pidm_ui_refresh(runtime);
}

/*
 * brief : Run PIDM button scanning and periodic detection pulses.
 * input : param - PIDM UI runtime pointer.
 * output: none.
 * type  : private
 * theory: keep blocking pulse generation outside the LVGL timer while UI drawing stays on LVGL context.
 */
static void _pidm_ui_task(void* param) {
    pidm_ui_runtime_s* runtime = (pidm_ui_runtime_s*)param;
    btn_status_e btn_val = Btn_Idle;
    uint16_t det_tick = 0U;

    while (1) {
        delay_ms(PIDM_UI_TASK_PERIOD_MS);

        btn_val = button_scan_state(&runtime->button_scan, PIDM_UI_TASK_PERIOD_MS);
        if ((btn_val == Btn_Both_Click) && (runtime->home_cb != NULL)) {
            runtime->home_cb(runtime->home_user_ctx);
        }

        det_tick += PIDM_UI_TASK_PERIOD_MS;
        if (det_tick >= PIDM_UI_DETECTION_PERIOD_MS) {
            det_tick = 0U;
            (void)pidm_trigger_detection();
        }
    }
}

/*
 * brief : Create a styled PIDM panel.
 * input : parent - owner object; width/height - panel dimensions.
 * output: created panel object.
 * type  : private
 * theory: centralize panel styling so scope and parameter sections remain visually consistent.
 */
static lv_obj_t* _pidm_create_panel(lv_obj_t* parent, lv_coord_t width, lv_coord_t height) {
    lv_obj_t* panel = lv_obj_create(parent);

    if (panel == NULL) {
        return NULL;
    }

    lv_obj_set_size(panel, width, height);
    lv_obj_set_style_bg_color(panel, lv_color_hex(0x171B1D), 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(panel, lv_color_hex(0x526167), 0);
    lv_obj_set_style_border_width(panel, 1, 0);
    lv_obj_set_style_radius(panel, PIDM_UI_CARD_RADIUS_PX, 0);
    lv_obj_set_style_pad_all(panel, 5, 0);
    lv_obj_set_style_pad_row(panel, 3, 0);
    lv_obj_set_layout(panel, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(panel,
                          LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

    return panel;
}

/*
 * brief : Add a title and divider to a PIDM panel.
 * input : panel - owner panel; text - title text.
 * output: none.
 * type  : private
 * theory: use a compact heading so the live data keeps most of the available screen area.
 */
static void _pidm_add_panel_title(lv_obj_t* panel, const char* text) {
    lv_obj_t* title_lab = lv_label_create(panel);
    lv_obj_t* divider = lv_obj_create(panel);

    lv_label_set_text(title_lab, text);
    lv_obj_set_style_text_color(title_lab, lv_color_hex(0x55E6A5), 0);
    lv_obj_set_style_text_font(title_lab, &PIDM_UI_TEXT_FONT, 0);

    lv_obj_set_size(divider, lv_pct(100), 1);
    lv_obj_set_style_bg_color(divider, lv_color_hex(0x394448), 0);
    lv_obj_set_style_bg_opa(divider, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(divider, 0, 0);
    lv_obj_set_style_radius(divider, 0, 0);
    lv_obj_clear_flag(divider, LV_OBJ_FLAG_SCROLLABLE);
}

/*
 * brief : Add one parameter label and read-only text edit.
 * input : panel - owner panel; key_text/value_text - initial strings; value_color - text color.
 * output: created textarea object.
 * type  : private
 * theory: keep fixed textarea widgets and only replace their short text during timer refreshes.
 */
static lv_obj_t* _pidm_add_edit_row(lv_obj_t* panel,
                                    const char* key_text,
                                    const char* value_text,
                                    lv_color_t value_color) {
    lv_obj_t* row = lv_obj_create(panel);
    lv_obj_t* key_lab = lv_label_create(row);
    lv_obj_t* edit = lv_textarea_create(row);

    lv_obj_set_size(row, lv_pct(100), 25);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_layout(row, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row,
                          LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    lv_label_set_text(key_lab, key_text);
    lv_label_set_long_mode(key_lab, LV_LABEL_LONG_CLIP);
    lv_obj_set_flex_grow(key_lab, 1);
    lv_obj_set_style_text_color(key_lab, lv_color_hex(0xDCE3E5), 0);
    lv_obj_set_style_text_font(key_lab, &PIDM_UI_TEXT_FONT, 0);

    lv_obj_set_size(edit, 68, 22);
    lv_textarea_set_one_line(edit, true);
    lv_textarea_set_cursor_click_pos(edit, false);
    lv_textarea_set_text(edit, value_text);
    lv_obj_clear_flag(edit, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(edit, lv_color_hex(0x0A1012), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(edit, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(edit, lv_color_hex(0x526167), LV_PART_MAIN);
    lv_obj_set_style_border_width(edit, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(edit, 3, LV_PART_MAIN);
    lv_obj_set_style_pad_left(edit, 3, LV_PART_MAIN);
    lv_obj_set_style_pad_right(edit, 3, LV_PART_MAIN);
    lv_obj_set_style_pad_top(edit, 2, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(edit, 2, LV_PART_MAIN);
    lv_obj_set_style_text_align(edit, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
    lv_obj_set_style_text_color(edit, value_color, LV_PART_MAIN);
    lv_obj_set_style_text_font(edit, &PIDM_UI_TEXT_FONT, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(edit, LV_OPA_TRANSP, LV_PART_CURSOR);

    return edit;
}

/*
 * brief : Add oscilloscope grid lines.
 * input : plot - plot object; width/height - drawable dimensions.
 * output: none.
 * type  : private
 * theory: use lightweight one-pixel objects for a fixed grid that needs no periodic redraw logic.
 */
static void _pidm_add_scope_grid(lv_obj_t* plot, lv_coord_t width, lv_coord_t height) {
    lv_obj_t* grid = NULL;
    uint32_t i = 0U;

    for (i = 1U; i < PIDM_UI_SCOPE_GRID_H_LINE_CNT; ++i) {
        grid = lv_obj_create(plot);
        lv_obj_set_size(grid, width, 1);
        lv_obj_set_pos(grid,
                       0,
                       (lv_coord_t)((i * (uint32_t)(height - 1))
                                    / PIDM_UI_SCOPE_GRID_H_LINE_CNT));
        lv_obj_set_style_bg_color(grid, lv_color_hex(0x314044), 0);
        lv_obj_set_style_bg_opa(grid, LV_OPA_70, 0);
        lv_obj_set_style_border_width(grid, 0, 0);
        lv_obj_clear_flag(grid, LV_OBJ_FLAG_SCROLLABLE);
    }

    for (i = 1U; i < PIDM_UI_SCOPE_GRID_V_LINE_CNT; ++i) {
        grid = lv_obj_create(plot);
        lv_obj_set_size(grid, 1, height);
        lv_obj_set_pos(grid,
                       (lv_coord_t)((i * (uint32_t)(width - 1))
                                    / PIDM_UI_SCOPE_GRID_V_LINE_CNT),
                       0);
        lv_obj_set_style_bg_color(grid, lv_color_hex(0x314044), 0);
        lv_obj_set_style_bg_opa(grid, LV_OPA_70, 0);
        lv_obj_set_style_border_width(grid, 0, 0);
        lv_obj_clear_flag(grid, LV_OBJ_FLAG_SCROLLABLE);
    }
}

/*
 * brief : Build the PIDM ADC oscilloscope panel.
 * input : parent - page root; width/height - panel dimensions.
 * output: created scope panel.
 * type  : private
 * theory: bind one LVGL line to the persistent PSRAM point array and redraw it in place.
 */
static lv_obj_t* _pidm_build_scope_panel(lv_obj_t* parent,
                                         lv_coord_t width,
                                         lv_coord_t height) {
    lv_obj_t* panel = NULL;
    lv_obj_t* plot = NULL;
#if LV_USE_LINE != 0
    lv_obj_t* line = NULL;
#endif
    lv_coord_t plot_w = width - 12;
    lv_coord_t plot_h = height - 31;

    if (plot_w < 180) {
        plot_w = 180;
    }
    if (plot_h < 80) {
        plot_h = 80;
    }

    panel = _pidm_create_panel(parent, width, height);
    if (panel == NULL) {
        return NULL;
    }
    _pidm_add_panel_title(panel, "PIDM ADC Scope  0 - 4095");

    plot = lv_obj_create(panel);
    lv_obj_set_size(plot, plot_w, plot_h);
    lv_obj_set_style_bg_color(plot, lv_color_hex(0x050809), 0);
    lv_obj_set_style_bg_opa(plot, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(plot, lv_color_hex(0x6D7B80), 0);
    lv_obj_set_style_border_width(plot, 1, 0);
    lv_obj_set_style_radius(plot, 2, 0);
    lv_obj_set_style_pad_all(plot, 0, 0);
    lv_obj_clear_flag(plot, LV_OBJ_FLAG_SCROLLABLE);
    _pidm_add_scope_grid(plot, plot_w, plot_h);

#if LV_USE_LINE != 0
    if (s_pidm_scope_buffer != NULL) {
        _pidm_scope_reset_samples(plot_w - 2, plot_h - 2);
        line = lv_line_create(plot);
        lv_obj_set_size(line, plot_w - 2, plot_h - 2);
        lv_obj_set_pos(line, 1, 1);
        lv_obj_set_style_bg_opa(line, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(line, 0, 0);
        lv_obj_set_style_line_color(line, lv_color_hex(0x55E6A5), 0);
        lv_obj_set_style_line_width(line, 2, 0);
        lv_line_set_y_invert(line, true);
        lv_line_set_points(line,
                           s_pidm_scope_buffer->points,
                           PIDM_UI_SCOPE_POINT_COUNT);
        s_pidm_ui_runtime.scope_line = line;
        s_pidm_ui_runtime.scope_plot_w = plot_w - 2;
        s_pidm_ui_runtime.scope_plot_h = plot_h - 2;
    }
#endif

    return panel;
}

/*
 * brief : Build the PIDM parameter panels.
 * input : parent - page root; width/height - row dimensions.
 * output: none.
 * type  : private
 * theory: split six read-only values across two compact columns for fast scanning.
 */
static void _pidm_build_parameter_row(lv_obj_t* parent,
                                      lv_coord_t width,
                                      lv_coord_t height) {
    lv_obj_t* row = lv_obj_create(parent);
    lv_obj_t* panel = NULL;
    lv_coord_t panel_w = (width - PIDM_UI_CARD_GAP_PX) / 2;

    lv_obj_set_size(row, width, height);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_style_pad_column(row, PIDM_UI_CARD_GAP_PX, 0);
    lv_obj_set_layout(row, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row,
                          LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    panel = _pidm_create_panel(row, panel_w, height);
    _pidm_add_panel_title(panel, "ADC Values");
    s_pidm_ui_runtime.latest_edit = _pidm_add_edit_row(
        panel,
        "Latest",
        "--",
        lv_color_hex(0x55E6A5));
    s_pidm_ui_runtime.average_edit = _pidm_add_edit_row(
        panel,
        "Average",
        "--",
        lv_color_hex(0x55E6A5));
    s_pidm_ui_runtime.samples_edit = _pidm_add_edit_row(
        panel,
        "Samples",
        "0",
        lv_color_hex(0x55E6A5));

    panel = _pidm_create_panel(row, panel_w, height);
    _pidm_add_panel_title(panel, "Detection");
    s_pidm_ui_runtime.triggers_edit = _pidm_add_edit_row(
        panel,
        "Triggers",
        "0",
        lv_color_hex(0xFFD166));
    s_pidm_ui_runtime.pulse_edit = _pidm_add_edit_row(
        panel,
        "Pulse",
        "--",
        lv_color_hex(0xFFD166));
    s_pidm_ui_runtime.status_edit = _pidm_add_edit_row(
        panel,
        "Status",
        "OFFLINE",
        lv_color_hex(0x55C7FF));
}

/*
 * brief : Create the PIDM oscilloscope and runtime dashboard.
 * input : parent/area - page owner and size; home callback - desktop return hook.
 * output: Created LVGL screen object.
 * type  : public
 * theory: initialize hardware once, allocate scope storage in PSRAM, then refresh widgets with an LVGL timer.
 */
lv_obj_t* pidm_open_screen(lv_obj_t* parent,
                           lv_coord_t area_w,
                           lv_coord_t area_h,
                           ui_menu_home_cb_t home_cb,
                           void* home_user_ctx) {
    lv_obj_t* screen = NULL;
    int32_t inner_w = area_w - (2 * PIDM_UI_CARD_GAP_PX);
    int32_t inner_h = area_h - (2 * PIDM_UI_CARD_GAP_PX);
    int32_t scope_h = 0;
    int32_t parameter_h = 0;
    BaseType_t task_ok = pdFAIL;
    esp_err_t ret = ESP_OK;

    if (parent == NULL) {
        return NULL;
    }

    if (s_pidm_ui_runtime.task_handle != NULL) {
        vTaskDelete(s_pidm_ui_runtime.task_handle);
        s_pidm_ui_runtime.task_handle = NULL;
    }
    if (s_pidm_ui_runtime.ui_sync_timer != NULL) {
        lv_timer_delete(s_pidm_ui_runtime.ui_sync_timer);
        s_pidm_ui_runtime.ui_sync_timer = NULL;
    }

    memset(&s_pidm_ui_runtime, 0, sizeof(s_pidm_ui_runtime));
    s_pidm_ui_runtime.home_cb = home_cb;
    s_pidm_ui_runtime.home_user_ctx = home_user_ctx;

#if LV_USE_LINE != 0
    if (!_pidm_scope_alloc_buffer()) {
        ESP_LOGW(TAG, "scope unavailable, parameter view remains active");
    }
#endif

    ret = pidm_init_runtime();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "pidm_init_runtime failed: 0x%x", (unsigned)ret);
    }

    if (inner_w < 220) {
        inner_w = 220;
    }
    if (inner_h < 280) {
        inner_h = 280;
    }
    parameter_h = 116;
    scope_h = inner_h - PIDM_UI_CARD_GAP_PX - parameter_h;
    if (scope_h < 150) {
        scope_h = 150;
    }

    screen = lv_obj_create(parent);
    lv_obj_set_size(screen, area_w, area_h);
    lv_obj_set_pos(screen, 0, 0);
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x0B0D0E), 0);
    lv_obj_set_style_bg_grad_color(screen, lv_color_hex(0x182125), 0);
    lv_obj_set_style_bg_grad_dir(screen, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(screen, 0, 0);
    lv_obj_set_style_radius(screen, 0, 0);
    lv_obj_set_style_pad_all(screen, PIDM_UI_CARD_GAP_PX, 0);
    lv_obj_set_style_pad_row(screen, PIDM_UI_CARD_GAP_PX, 0);
    lv_obj_set_layout(screen, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(screen, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(screen,
                          LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_START);
    lv_obj_set_scroll_dir(screen, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(screen, LV_SCROLLBAR_MODE_OFF);

    (void)_pidm_build_scope_panel(screen, (lv_coord_t)inner_w, (lv_coord_t)scope_h);
    _pidm_build_parameter_row(screen,
                              (lv_coord_t)inner_w,
                              (lv_coord_t)parameter_h);
    _pidm_ui_refresh(&s_pidm_ui_runtime);

    s_pidm_ui_runtime.ui_sync_timer = lv_timer_create(
        _pidm_ui_timer_cb,
        PIDM_UI_REFRESH_PERIOD_MS,
        &s_pidm_ui_runtime);
    if (s_pidm_ui_runtime.ui_sync_timer == NULL) {
        (void)pidm_deinit_runtime();
#if LV_USE_LINE != 0
        _pidm_scope_free_buffer();
#endif
        lv_obj_del(screen);
        ESP_LOGE(TAG, "lv_timer_create failed");
        return NULL;
    }

    task_ok = xTaskCreate(_pidm_ui_task,
                          "pidm_ui",
                          PIDM_UI_TASK_STACK_SIZE,
                          &s_pidm_ui_runtime,
                          5,
                          &s_pidm_ui_runtime.task_handle);
    if (task_ok != pdPASS) {
        s_pidm_ui_runtime.task_handle = NULL;
        lv_timer_delete(s_pidm_ui_runtime.ui_sync_timer);
        s_pidm_ui_runtime.ui_sync_timer = NULL;
        (void)pidm_deinit_runtime();
#if LV_USE_LINE != 0
        _pidm_scope_free_buffer();
#endif
        lv_obj_del(screen);
        ESP_LOGE(TAG, "xTaskCreate failed");
        return NULL;
    }

    return screen;
}

/*
 * brief : Close the PIDM page and release its runtime resources.
 * input : screen - PIDM page root.
 * output: none.
 * type  : public
 * theory: stop producers and LVGL refresh first, then release hardware, objects, and PSRAM in order.
 */
void pidm_close_screen(lv_obj_t* screen) {
    if (s_pidm_ui_runtime.task_handle != NULL) {
        vTaskDelete(s_pidm_ui_runtime.task_handle);
        s_pidm_ui_runtime.task_handle = NULL;
    }
    if (s_pidm_ui_runtime.ui_sync_timer != NULL) {
        lv_timer_delete(s_pidm_ui_runtime.ui_sync_timer);
        s_pidm_ui_runtime.ui_sync_timer = NULL;
    }

    (void)pidm_deinit_runtime();

    if ((screen != NULL) && lv_obj_is_valid(screen)) {
        lv_obj_del(screen);
    }

#if LV_USE_LINE != 0
    _pidm_scope_free_buffer();
#endif

    memset(&s_pidm_ui_runtime, 0, sizeof(s_pidm_ui_runtime));
}
