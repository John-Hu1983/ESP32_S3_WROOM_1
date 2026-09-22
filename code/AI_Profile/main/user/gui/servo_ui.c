#include "servo_ui.h"

#define TAG "servo_ui"
#define SERVO_UI_REFRESH_PERIOD_MS (100U)
#define SERVO_UI_SCOPE_Y_MIN_ADC (0)
#define SERVO_UI_SCOPE_Y_MAX_ADC (4095)

static servo_ui_runtime_s s_servo_runtime;

#if LV_USE_LINE != 0
static lv_point_precise_t* s_scope_sp_points = NULL;
static lv_point_precise_t* s_scope_pv_points = NULL;
static int32_t* s_scope_sp_history = NULL;
static int32_t* s_scope_pv_history = NULL;
#endif

static void _servo_ui_refresh_profile_view(servo_ui_runtime_s* runtime);

#if LV_USE_LINE != 0
static bool _servo_scope_alloc_buffers(void);
static void _servo_scope_free_buffers(void);
static void _servo_scope_reset_samples(lv_coord_t plot_w,
                                       lv_coord_t plot_h,
                                       int32_t sp_init,
                                       int32_t pv_init);
static void _servo_scope_push_sample(servo_ui_runtime_s* runtime,
                                     int32_t sp_value,
                                     int32_t pv_value);
#endif

static const char* _servo_ui_state_text(e_motor_direction dir) {
    switch (dir) {
    case MOTOR_DIRECTION_STOP:
        return "STOP";
    case MOTOR_DIRECTION_CW:
        return "CW";
    case MOTOR_DIRECTION_CCW:
        return "CCW";
    case MOTOR_DIRECTION_BRAKE:
        return "BRAKE";
    default:
        return "UNKNOWN";
    }
}

#if LV_USE_LINE != 0
/*
 * brief : _servo_scope_alloc_buffers.
 * input : none.
 * output: return value from this function.
 * type  : private
 * theory: allocate scope point/history buffers in PSRAM to reduce internal RAM pressure.
 */
static bool _servo_scope_alloc_buffers(void) {
    size_t points_bytes = sizeof(lv_point_precise_t) * SERVO_UI_SCOPE_POINT_COUNT;
    size_t history_bytes = sizeof(int32_t) * SERVO_UI_SCOPE_POINT_COUNT;

    if ((s_scope_sp_points != NULL) && (s_scope_pv_points != NULL)
        && (s_scope_sp_history != NULL) && (s_scope_pv_history != NULL)) {
        return true;
    }

    if ((s_scope_sp_points != NULL) || (s_scope_pv_points != NULL)
        || (s_scope_sp_history != NULL) || (s_scope_pv_history != NULL)) {
        _servo_scope_free_buffers();
    }

    s_scope_sp_points = (lv_point_precise_t*)heap_caps_malloc(points_bytes,
                                                              MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s_scope_pv_points = (lv_point_precise_t*)heap_caps_malloc(points_bytes,
                                                              MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s_scope_sp_history = (int32_t*)heap_caps_malloc(history_bytes,
                                                    MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s_scope_pv_history = (int32_t*)heap_caps_malloc(history_bytes,
                                                    MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);

    if ((s_scope_sp_points == NULL) || (s_scope_pv_points == NULL)
        || (s_scope_sp_history == NULL) || (s_scope_pv_history == NULL)) {
        ESP_LOGE(TAG, "scope buffer alloc in PSRAM failed");
        _servo_scope_free_buffers();
        return false;
    }

    memset(s_scope_sp_points, 0, points_bytes);
    memset(s_scope_pv_points, 0, points_bytes);
    memset(s_scope_sp_history, 0, history_bytes);
    memset(s_scope_pv_history, 0, history_bytes);
    return true;
}

/*
 * brief : _servo_scope_free_buffers.
 * input : none.
 * output: none.
 * type  : private
 * theory: release PSRAM scope buffers and reset pointers to avoid stale accesses.
 */
static void _servo_scope_free_buffers(void) {
    if (s_scope_sp_points != NULL) {
        heap_caps_free(s_scope_sp_points);
        s_scope_sp_points = NULL;
    }
    if (s_scope_pv_points != NULL) {
        heap_caps_free(s_scope_pv_points);
        s_scope_pv_points = NULL;
    }
    if (s_scope_sp_history != NULL) {
        heap_caps_free(s_scope_sp_history);
        s_scope_sp_history = NULL;
    }
    if (s_scope_pv_history != NULL) {
        heap_caps_free(s_scope_pv_history);
        s_scope_pv_history = NULL;
    }
}
#endif

/*
 * brief : _servo_ui_timer_cb.
 * input : see parameters.
 * output: none.
 * type  : private
 * theory: execute UI text updates in LVGL timer context to keep LVGL access thread-safe.
 */
static void _servo_ui_timer_cb(lv_timer_t* timer) {
    servo_ui_runtime_s* runtime = NULL;

    if (timer == NULL) {
        return;
    }

    runtime = (servo_ui_runtime_s*)lv_timer_get_user_data(timer);
    if (runtime == NULL) {
        return;
    }

    _servo_ui_refresh_profile_view(runtime);
}

/*
 * brief : _servo_ui_refresh_profile_view.
 * input : see parameters.
 * output: none.
 * type  : private
 * theory: pull latest servo profile snapshots and synchronize dashboard value labels.
 */
static void _servo_ui_refresh_profile_view(servo_ui_runtime_s* runtime) {
    algo_pid_s* pid = NULL;
    t_servo_ctr* motor = NULL;
    const char* state_text = "IDLE";

    if (runtime == NULL) {
        return;
    }

    pid = servo_read_pid_profile();
    motor = servo_read_motor_profile();
    if (pid == NULL) {
        return;
    }

    if (runtime->kp_val_lab != NULL) {
        lv_label_set_text_fmt(runtime->kp_val_lab, "%.3f", (double)pid->cfg.kp);
    }
    if (runtime->ki_val_lab != NULL) {
        lv_label_set_text_fmt(runtime->ki_val_lab, "%.3f", (double)pid->cfg.ki);
    }
    if (runtime->kd_val_lab != NULL) {
        lv_label_set_text_fmt(runtime->kd_val_lab, "%.3f", (double)pid->cfg.kd);
    }
    if (runtime->i_limit_val_lab != NULL) {
        lv_label_set_text_fmt(runtime->i_limit_val_lab, "%.1f", (double)pid->cfg.i_max);
    }
    if (runtime->deadband_val_lab != NULL) {
        lv_label_set_text_fmt(runtime->deadband_val_lab,
                              "%.1f",
                              (double)pid->cfg.err_deadband);
    }

    if (runtime->target_val_lab != NULL) {
        lv_label_set_text_fmt(runtime->target_val_lab, "%.0f", (double)pid->target);
    }
    if (runtime->feedback_val_lab != NULL) {
        lv_label_set_text_fmt(runtime->feedback_val_lab, "%.0f", (double)pid->feedback);
    }

#if LV_USE_LINE != 0
    _servo_scope_push_sample(runtime, (int32_t)pid->target, (int32_t)pid->feedback);
#endif

    if (motor != NULL) {
        if (runtime->pwm_a_val_lab != NULL) {
            lv_label_set_text_fmt(runtime->pwm_a_val_lab, "%u", (unsigned)motor->pwm_a);
        }
        if (runtime->pwm_b_val_lab != NULL) {
            lv_label_set_text_fmt(runtime->pwm_b_val_lab, "%u", (unsigned)motor->pwm_b);
        }
        state_text = _servo_ui_state_text(motor->dir);
    }
    else {
        if (runtime->pwm_a_val_lab != NULL) {
            lv_label_set_text(runtime->pwm_a_val_lab, "--");
        }
        if (runtime->pwm_b_val_lab != NULL) {
            lv_label_set_text(runtime->pwm_b_val_lab, "--");
        }

        if (pid->curr_err == 0.0f) {
            state_text = "BRAKE";
        }
        else if (pid->curr_err > 0.0f) {
            state_text = "CW";
        }
        else {
            state_text = "CCW";
        }
    }

    if (runtime->state_val_lab != NULL) {
        lv_label_set_text(runtime->state_val_lab, state_text);
    }
}

/*
 * brief : _servo_ui_task.
 * input : see parameters.
 * output: none.
 * type  : private
 */
static void _servo_ui_task(void* param) {
    servo_ui_runtime_s* runtime = (servo_ui_runtime_s*)param;
    btn_status_e btn_val = Btn_Idle;

    while (1) {
        delay_ms(SERVO_UI_TASK_PERIOD_MS);

        btn_val = button_scan_state(&runtime->button_scan, SERVO_UI_TASK_PERIOD_MS);
        if ((btn_val == Btn_Both_Click) && (runtime->home_cb != NULL)) {
            runtime->home_cb(runtime->home_user_ctx);
        }

        servo_compute_via_pid(SERVO_UI_TASK_PERIOD_MS);
    }
}

/*
 * brief : _servo_create_card.
 * input : see parameters.
 * output: return value from this function.
 * type  : private
 */
static lv_obj_t* _servo_create_card(lv_obj_t* parent, lv_coord_t w, lv_coord_t h) {
    lv_obj_t* card = lv_obj_create(parent);

    if (card == NULL) {
        return NULL;
    }

    lv_obj_set_size(card, w, h);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x08172D), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(card, lv_color_hex(0x0D86D9), 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_radius(card, SERVO_UI_CARD_RADIUS_PX, 0);
    lv_obj_set_style_pad_left(card, 5, 0);
    lv_obj_set_style_pad_right(card, 5, 0);
    lv_obj_set_style_pad_top(card, 4, 0);
    lv_obj_set_style_pad_bottom(card, 4, 0);
    lv_obj_set_style_pad_row(card, 2, 0);
    lv_obj_set_layout(card, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(card,
                          LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    return card;
}

/*
 * brief : _servo_add_card_title.
 * input : see parameters.
 * output: none.
 * type  : private
 */
static void _servo_add_card_title(lv_obj_t* card, const char* text) {
    lv_obj_t* title_lab = lv_label_create(card);
    lv_obj_t* sep = lv_obj_create(card);

    lv_label_set_text(title_lab, text);
    lv_obj_set_style_text_color(title_lab, lv_color_hex(0x35D5FF), 0);
    lv_obj_set_style_text_font(title_lab, &SERVO_UI_TITLE_FONT, 0);

    lv_obj_set_size(sep, lv_pct(100), 1);
    lv_obj_set_style_bg_color(sep, lv_color_hex(0x1D3857), 0);
    lv_obj_set_style_bg_opa(sep, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(sep, 0, 0);
    lv_obj_set_style_radius(sep, 0, 0);
    lv_obj_clear_flag(sep, LV_OBJ_FLAG_SCROLLABLE);
}

/*
 * brief : _servo_add_kv_row.
 * input : see parameters.
 * output: none.
 * type  : private
 */
static lv_obj_t* _servo_add_kv_row(lv_obj_t* card,
                                   const char* key_text,
                                   const char* val_text,
                                   lv_color_t val_color) {
    lv_obj_t* row = lv_obj_create(card);
    lv_obj_t* key_lab = lv_label_create(row);
    lv_obj_t* val_box = lv_obj_create(row);
    lv_obj_t* val_lab = lv_label_create(val_box);

    lv_obj_set_size(row, lv_pct(100), 22);
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
    lv_obj_set_style_text_color(key_lab, lv_color_hex(0xE6EEF9), 0);
    lv_obj_set_style_text_font(key_lab, &SERVO_UI_TEXT_FONT, 0);

    lv_obj_set_size(val_box, 56, 20);
    lv_obj_set_style_bg_color(val_box, lv_color_hex(0x0B213E), 0);
    lv_obj_set_style_bg_opa(val_box, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(val_box, lv_color_hex(0x1479C9), 0);
    lv_obj_set_style_border_width(val_box, 1, 0);
    lv_obj_set_style_radius(val_box, 3, 0);
    lv_obj_set_style_pad_all(val_box, 0, 0);
    lv_obj_set_layout(val_box, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(val_box, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(val_box,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(val_box, LV_OBJ_FLAG_SCROLLABLE);

    lv_label_set_text(val_lab, val_text);
    lv_obj_set_style_text_color(val_lab, val_color, 0);
    lv_obj_set_style_text_font(val_lab, &SERVO_UI_TEXT_FONT, 0);

    return val_lab;
}

/*
 * brief : _servo_add_curve_grid.
 * input : see parameters.
 * output: none.
 * type  : private
 */
static void _servo_add_curve_grid(lv_obj_t* plot,
                                  lv_coord_t plot_w,
                                  lv_coord_t plot_h) {
    uint32_t i = 0U;
    lv_obj_t* grid = NULL;
    int32_t w = plot_w;
    int32_t h = plot_h;

    if (w < 2) {
        w = 2;
    }
    if (h < 2) {
        h = 2;
    }

    for (i = 1U; i < SERVO_UI_SCOPE_GRID_H_LINE_CNT; ++i) {
        grid = lv_obj_create(plot);
        lv_obj_set_size(grid, w, 1);
        lv_obj_set_pos(
            grid,
            0,
            (lv_coord_t)((i * (uint32_t)(h - 1)) / SERVO_UI_SCOPE_GRID_H_LINE_CNT));
        lv_obj_set_style_bg_color(grid, lv_color_hex(0x22384D), 0);
        lv_obj_set_style_bg_opa(grid, LV_OPA_60, 0);
        lv_obj_set_style_border_width(grid, 0, 0);
        lv_obj_set_style_radius(grid, 0, 0);
        lv_obj_clear_flag(grid, LV_OBJ_FLAG_SCROLLABLE);
    }

    for (i = 1U; i < SERVO_UI_SCOPE_GRID_V_LINE_CNT; ++i) {
        grid = lv_obj_create(plot);
        lv_obj_set_size(grid, 1, h);
        lv_obj_set_pos(
            grid,
            (lv_coord_t)((i * (uint32_t)(w - 1)) / SERVO_UI_SCOPE_GRID_V_LINE_CNT),
            0);
        lv_obj_set_style_bg_color(grid, lv_color_hex(0x22384D), 0);
        lv_obj_set_style_bg_opa(grid, LV_OPA_60, 0);
        lv_obj_set_style_border_width(grid, 0, 0);
        lv_obj_set_style_radius(grid, 0, 0);
        lv_obj_clear_flag(grid, LV_OBJ_FLAG_SCROLLABLE);
    }
}

#if LV_USE_LINE != 0
/*
 * brief : _servo_scope_value_to_y.
 * input : see parameters.
 * output: return value from this function.
 * type  : private
 */
static lv_coord_t _servo_scope_value_to_y(int32_t deg_value, lv_coord_t plot_h) {
    int32_t val = deg_value;
    int32_t den = SERVO_UI_SCOPE_Y_MAX_ADC - SERVO_UI_SCOPE_Y_MIN_ADC;
    int32_t num = 0;

    if (plot_h <= 1) {
        return 0;
    }

    if (val < SERVO_UI_SCOPE_Y_MIN_ADC) {
        val = SERVO_UI_SCOPE_Y_MIN_ADC;
    }
    else if (val > SERVO_UI_SCOPE_Y_MAX_ADC) {
        val = SERVO_UI_SCOPE_Y_MAX_ADC;
    }

    num = (val - SERVO_UI_SCOPE_Y_MIN_ADC) * (plot_h - 1);
    if (den <= 0) {
        return 0;
    }

    return (lv_coord_t)(num / den);
}

/*
 * brief : _servo_scope_reset_samples.
 * input : see parameters.
 * output: none.
 * type  : private
 */
static void _servo_scope_reset_samples(lv_coord_t plot_w,
                                       lv_coord_t plot_h,
                                       int32_t sp_init,
                                       int32_t pv_init) {
    uint32_t i = 0U;

    if ((s_scope_sp_points == NULL) || (s_scope_pv_points == NULL)
        || (s_scope_sp_history == NULL) || (s_scope_pv_history == NULL)) {
        return;
    }

    if (plot_w < 2) {
        plot_w = 2;
    }
    if (plot_h < 2) {
        plot_h = 2;
    }

    if (sp_init < SERVO_UI_SCOPE_Y_MIN_ADC) {
        sp_init = SERVO_UI_SCOPE_Y_MIN_ADC;
    }
    else if (sp_init > SERVO_UI_SCOPE_Y_MAX_ADC) {
        sp_init = SERVO_UI_SCOPE_Y_MAX_ADC;
    }

    if (pv_init < SERVO_UI_SCOPE_Y_MIN_ADC) {
        pv_init = SERVO_UI_SCOPE_Y_MIN_ADC;
    }
    else if (pv_init > SERVO_UI_SCOPE_Y_MAX_ADC) {
        pv_init = SERVO_UI_SCOPE_Y_MAX_ADC;
    }

    for (i = 0U; i < SERVO_UI_SCOPE_POINT_COUNT; ++i) {
        s_scope_sp_history[i] = sp_init;
        s_scope_pv_history[i] = pv_init;

        s_scope_sp_points[i].x = (lv_coord_t)((i * (uint32_t)(plot_w - 1))
                                              / (SERVO_UI_SCOPE_POINT_COUNT - 1U));
        s_scope_sp_points[i].y = _servo_scope_value_to_y(sp_init, plot_h);

        s_scope_pv_points[i].x = s_scope_sp_points[i].x;
        s_scope_pv_points[i].y = _servo_scope_value_to_y(pv_init, plot_h);
    }
}

/*
 * brief : _servo_scope_push_sample.
 * input : see parameters.
 * output: none.
 * type  : private
 */
static void _servo_scope_push_sample(servo_ui_runtime_s* runtime,
                                     int32_t sp_value,
                                     int32_t pv_value) {
    uint32_t i = 0U;
    lv_coord_t plot_h = 0;

    if ((runtime == NULL) || (runtime->scope_sp_line == NULL)
        || (runtime->scope_pv_line == NULL)) {
        return;
    }

    if ((s_scope_sp_points == NULL) || (s_scope_pv_points == NULL)
        || (s_scope_sp_history == NULL) || (s_scope_pv_history == NULL)) {
        return;
    }

    plot_h = runtime->scope_plot_h;
    if (plot_h < 2) {
        return;
    }

    if (sp_value < SERVO_UI_SCOPE_Y_MIN_ADC) {
        sp_value = SERVO_UI_SCOPE_Y_MIN_ADC;
    }
    else if (sp_value > SERVO_UI_SCOPE_Y_MAX_ADC) {
        sp_value = SERVO_UI_SCOPE_Y_MAX_ADC;
    }

    if (pv_value < SERVO_UI_SCOPE_Y_MIN_ADC) {
        pv_value = SERVO_UI_SCOPE_Y_MIN_ADC;
    }
    else if (pv_value > SERVO_UI_SCOPE_Y_MAX_ADC) {
        pv_value = SERVO_UI_SCOPE_Y_MAX_ADC;
    }

    for (i = 0U; i < (SERVO_UI_SCOPE_POINT_COUNT - 1U); ++i) {
        s_scope_sp_history[i] = s_scope_sp_history[i + 1U];
        s_scope_pv_history[i] = s_scope_pv_history[i + 1U];
    }

    s_scope_sp_history[SERVO_UI_SCOPE_POINT_COUNT - 1U] = sp_value;
    s_scope_pv_history[SERVO_UI_SCOPE_POINT_COUNT - 1U] = pv_value;

    for (i = 0U; i < SERVO_UI_SCOPE_POINT_COUNT; ++i) {
        s_scope_sp_points[i].y = _servo_scope_value_to_y(s_scope_sp_history[i], plot_h);
        s_scope_pv_points[i].y = _servo_scope_value_to_y(s_scope_pv_history[i], plot_h);
    }

    lv_obj_invalidate(runtime->scope_sp_line);
    lv_obj_invalidate(runtime->scope_pv_line);
}
#endif

/*
 * brief : _servo_build_scope_panel.
 * input : see parameters.
 * output: return value from this function.
 * type  : private
 */
static lv_obj_t* _servo_build_scope_panel(lv_obj_t* parent,
                                          lv_coord_t area_w,
                                          lv_coord_t panel_h) {
    lv_obj_t* panel = lv_obj_create(parent);
    lv_obj_t* plot = NULL;
#if LV_USE_LINE != 0
    lv_obj_t* sp_line = NULL;
    lv_obj_t* pv_line = NULL;
#endif
    lv_coord_t plot_w = 0;
    lv_coord_t plot_h = 0;

    if (panel == NULL) {
        return NULL;
    }

    plot_w = area_w - (2 * SERVO_UI_CARD_GAP_PX) - 2;
    if (plot_w < 180) {
        plot_w = 180;
    }

    plot_h = panel_h;
    if (plot_h < 100) {
        plot_h = 100;
    }

    lv_obj_set_size(panel, lv_pct(100), panel_h);
    lv_obj_set_style_bg_opa(panel, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(panel, 0, 0);
    lv_obj_set_style_pad_all(panel, 0, 0);
    lv_obj_set_style_pad_row(panel, 2, 0);
    lv_obj_set_layout(panel, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(panel,
                          LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

    plot = lv_obj_create(panel);
    lv_obj_set_size(plot, plot_w, plot_h);
    lv_obj_set_style_bg_color(plot, lv_color_hex(0x061325), 0);
    lv_obj_set_style_bg_opa(plot, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(plot, lv_color_hex(0x4A6178), 0);
    lv_obj_set_style_border_width(plot, 1, 0);
    lv_obj_set_style_radius(plot, 2, 0);
    lv_obj_set_style_pad_all(plot, 0, 0);
    lv_obj_clear_flag(plot, LV_OBJ_FLAG_SCROLLABLE);

    _servo_add_curve_grid(plot, plot_w, plot_h);

#if LV_USE_LINE != 0
    if ((s_scope_sp_points != NULL) && (s_scope_pv_points != NULL)
        && (s_scope_sp_history != NULL) && (s_scope_pv_history != NULL)) {
        _servo_scope_reset_samples(plot_w - 2, plot_h - 2, 0, 0);

        sp_line = lv_line_create(plot);
        lv_obj_set_size(sp_line, plot_w - 2, plot_h - 2);
        lv_obj_set_pos(sp_line, 1, 1);
        lv_obj_set_style_bg_opa(sp_line, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(sp_line, 0, 0);
        lv_obj_set_style_line_color(sp_line, lv_color_hex(0xF1DD18), 0);
        lv_obj_set_style_line_width(sp_line, 2, 0);
        lv_line_set_y_invert(sp_line, true);
        lv_line_set_points(sp_line, s_scope_sp_points, SERVO_UI_SCOPE_POINT_COUNT);
        s_servo_runtime.scope_sp_line = sp_line;

        pv_line = lv_line_create(plot);
        lv_obj_set_size(pv_line, plot_w - 2, plot_h - 2);
        lv_obj_set_pos(pv_line, 1, 1);
        lv_obj_set_style_bg_opa(pv_line, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(pv_line, 0, 0);
        lv_obj_set_style_line_color(pv_line, lv_color_hex(0x57D844), 0);
        lv_obj_set_style_line_width(pv_line, 2, 0);
        lv_line_set_y_invert(pv_line, true);
        lv_line_set_points(pv_line, s_scope_pv_points, SERVO_UI_SCOPE_POINT_COUNT);
        s_servo_runtime.scope_pv_line = pv_line;

        s_servo_runtime.scope_plot_w = (lv_coord_t)(plot_w - 2);
        s_servo_runtime.scope_plot_h = (lv_coord_t)(plot_h - 2);
    }
    else {
        s_servo_runtime.scope_sp_line = NULL;
        s_servo_runtime.scope_pv_line = NULL;
        s_servo_runtime.scope_plot_w = 0;
        s_servo_runtime.scope_plot_h = 0;
    }
#endif

    return panel;
}

/*
 * brief : _servo_build_dashboard.
 * input : see parameters.
 * output: none.
 * type  : private
 */
static void _servo_build_dashboard(lv_obj_t* screen,
                                   lv_coord_t area_w,
                                   lv_coord_t area_h) {
    lv_obj_t* scope_panel = NULL;
    lv_obj_t* card_row = NULL;
    lv_obj_t* card = NULL;

    int32_t inner_w = area_w - (2 * SERVO_UI_CARD_GAP_PX);
    int32_t inner_h = area_h - (2 * SERVO_UI_CARD_GAP_PX);
    int32_t v_gap = SERVO_UI_CARD_GAP_PX;
    int32_t h_gap = SERVO_UI_CARD_GAP_PX;
    int32_t scope_pref_h = 0;
    int32_t scope_min_h = 120;
    int32_t card_min_h = 136;
    int32_t scope_max_h = 0;
    int32_t scope_h = 0;
    int32_t card_h = 0;
    int32_t card_w = 0;

    if (inner_w < 220) {
        inner_w = 220;
    }
    if (inner_h < 280) {
        inner_h = 280;
    }

    scope_pref_h = (inner_h * 62) / 100;
    scope_max_h = inner_h - v_gap - card_min_h;
    if (scope_max_h < scope_min_h) {
        scope_max_h = scope_min_h;
    }

    scope_h = scope_pref_h;
    if (scope_h < scope_min_h) {
        scope_h = scope_min_h;
    }
    if (scope_h > scope_max_h) {
        scope_h = scope_max_h;
    }

    card_h = inner_h - v_gap - scope_h;
    if (card_h < card_min_h) {
        card_h = card_min_h;
        scope_h = inner_h - v_gap - card_h;
    }

    card_w = (inner_w - h_gap) / 2;
    if (card_w < 120) {
        card_w = 120;
    }

    scope_panel = _servo_build_scope_panel(screen, area_w, (lv_coord_t)scope_h);
    if (scope_panel == NULL) {
        return;
    }

    card_row = lv_obj_create(screen);
    lv_obj_set_size(card_row, lv_pct(100), (lv_coord_t)card_h);
    lv_obj_set_style_bg_opa(card_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(card_row, 0, 0);
    lv_obj_set_style_pad_all(card_row, 0, 0);
    lv_obj_set_style_pad_column(card_row, h_gap, 0);
    lv_obj_set_layout(card_row, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(card_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(card_row,
                          LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);
    lv_obj_clear_flag(card_row, LV_OBJ_FLAG_SCROLLABLE);

    card = _servo_create_card(card_row, (lv_coord_t)card_w, (lv_coord_t)card_h);
    _servo_add_card_title(card, "PID Parameters");
    s_servo_runtime.kp_val_lab = _servo_add_kv_row(card, "Kp", "--", lv_color_hex(0xF4FBFF));
    s_servo_runtime.ki_val_lab = _servo_add_kv_row(card, "Ki", "--", lv_color_hex(0xF4FBFF));
    s_servo_runtime.kd_val_lab = _servo_add_kv_row(card, "Kd", "--", lv_color_hex(0xF4FBFF));
    s_servo_runtime.i_limit_val_lab = _servo_add_kv_row(card,
                                                        "I-Limit",
                                                        "--",
                                                        lv_color_hex(0xF4FBFF));
    s_servo_runtime.deadband_val_lab = _servo_add_kv_row(card,
                                                         "Deadband",
                                                         "--",
                                                         lv_color_hex(0xF4FBFF));

    card = _servo_create_card(card_row, (lv_coord_t)card_w, (lv_coord_t)card_h);
    _servo_add_card_title(card, "Target & Feedback");
    s_servo_runtime.pwm_a_val_lab = _servo_add_kv_row(card,
                                                       "PWM-A",
                                                       "--",
                                                       lv_color_hex(0x35D5FF));
    s_servo_runtime.pwm_b_val_lab = _servo_add_kv_row(card,
                                                       "PWM-B",
                                                       "--",
                                                       lv_color_hex(0x35D5FF));
    s_servo_runtime.target_val_lab = _servo_add_kv_row(card,
                                                        "Target",
                                                        "--",
                                                        lv_color_hex(0x35D5FF));
    s_servo_runtime.feedback_val_lab = _servo_add_kv_row(card,
                                                          "Feedback",
                                                          "--",
                                                          lv_color_hex(0x35D5FF));
    s_servo_runtime.state_val_lab = _servo_add_kv_row(card,
                                                       "State",
                                                       "--",
                                                       lv_color_hex(0x35D5FF));

    ESP_LOGI(TAG,
             "layout area=%dx%d inner=%dx%d scope_h=%d card_h=%d card_w=%d",
             (int)area_w,
             (int)area_h,
             inner_w,
             inner_h,
             scope_h,
             card_h,
             card_w);
}

/*
 * brief : Create the servo page root screen object.
 * input : lcd_w - LCD width; lcd_h - LCD height.
 * output: Created LVGL screen object.
 * type  : public
 */
lv_obj_t* servo_open_screen(lv_obj_t* parent,
                            lv_coord_t area_w,
                            lv_coord_t area_h,
                            ui_menu_home_cb_t home_cb,
                            void* home_user_ctx) {
    lv_obj_t* screen = NULL;
    BaseType_t task_ok = pdFAIL;
    esp_err_t ret = ESP_OK;

    if (parent == NULL) {
        return NULL;
    }

    if (s_servo_runtime.task_handle != NULL) {
        vTaskDelete(s_servo_runtime.task_handle);
        s_servo_runtime.task_handle = NULL;
    }
    if (s_servo_runtime.ui_sync_timer != NULL) {
        lv_timer_delete(s_servo_runtime.ui_sync_timer);
        s_servo_runtime.ui_sync_timer = NULL;
    }

    memset(&s_servo_runtime, 0, sizeof(s_servo_runtime));
    s_servo_runtime.home_cb = home_cb;
    s_servo_runtime.home_user_ctx = home_user_ctx;
    s_servo_runtime.button_scan = (btn_scan_s){ 0 };

#if LV_USE_LINE != 0
    if (!_servo_scope_alloc_buffers()) {
        ESP_LOGW(TAG, "scope buffers unavailable, waveform drawing disabled");
    }
#endif

    ret = servo_init_hw();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "servo_init_hw failed: 0x%x", (unsigned)ret);
    }

    screen = lv_obj_create(parent);
    lv_obj_set_size(screen, area_w, area_h);
    lv_obj_set_pos(screen, 0, 0);
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x020A16), 0);
    lv_obj_set_style_bg_grad_color(screen, lv_color_hex(0x0A1E36), 0);
    lv_obj_set_style_bg_grad_dir(screen, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(screen, 0, 0);
    lv_obj_set_style_radius(screen, 0, 0);
    lv_obj_set_style_pad_all(screen, SERVO_UI_CARD_GAP_PX, 0);
    lv_obj_set_style_pad_row(screen, SERVO_UI_CARD_GAP_PX, 0);
    lv_obj_set_layout(screen, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(screen, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(screen,
                          LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_START);
    lv_obj_set_scroll_dir(screen, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(screen, LV_SCROLLBAR_MODE_OFF);

    _servo_build_dashboard(screen, area_w, area_h);
    _servo_ui_refresh_profile_view(&s_servo_runtime);

    s_servo_runtime.ui_sync_timer = lv_timer_create(_servo_ui_timer_cb,
                                                    SERVO_UI_REFRESH_PERIOD_MS,
                                                    &s_servo_runtime);
    if (s_servo_runtime.ui_sync_timer == NULL) {
#if LV_USE_LINE != 0
        _servo_scope_free_buffers();
#endif
        (void)servo_deinit_hw();
        if ((screen != NULL) && lv_obj_is_valid(screen)) {
            lv_obj_del(screen);
        }
        ESP_LOGE(TAG, "lv_timer_create failed");
        return NULL;
    }

    task_ok = xTaskCreate(_servo_ui_task,
                          "servo_ui",
                          SERVO_UI_TASK_STACK_SIZE,
                          &s_servo_runtime,
                          5,
                          &s_servo_runtime.task_handle);
    if (task_ok != pdPASS) {
        s_servo_runtime.task_handle = NULL;
        if (s_servo_runtime.ui_sync_timer != NULL) {
            lv_timer_delete(s_servo_runtime.ui_sync_timer);
            s_servo_runtime.ui_sync_timer = NULL;
        }
#if LV_USE_LINE != 0
        _servo_scope_free_buffers();
#endif
        (void)servo_deinit_hw();
        if ((screen != NULL) && lv_obj_is_valid(screen)) {
            lv_obj_del(screen);
        }
        ESP_LOGE(TAG, "xTaskCreate failed");
        return NULL;
    }

    return screen;
}

/*
 * brief : servo_close_screen.
 * input : see parameters.
 * output: none.
 * type  : public
 */
void servo_close_screen(lv_obj_t* screen) {
    if (s_servo_runtime.task_handle != NULL) {
        vTaskDelete(s_servo_runtime.task_handle);
        s_servo_runtime.task_handle = NULL;
    }

    if (s_servo_runtime.ui_sync_timer != NULL) {
        lv_timer_delete(s_servo_runtime.ui_sync_timer);
        s_servo_runtime.ui_sync_timer = NULL;
    }

#if LV_USE_LINE != 0
    _servo_scope_free_buffers();
#endif

    s_servo_runtime.home_cb = NULL;
    s_servo_runtime.home_user_ctx = NULL;
    s_servo_runtime.button_scan = (btn_scan_s){ 0 };

    (void)servo_deinit_hw();

    if ((screen != NULL) && lv_obj_is_valid(screen)) {
        lv_obj_del(screen);
    }

    memset(&s_servo_runtime, 0, sizeof(s_servo_runtime));
}
