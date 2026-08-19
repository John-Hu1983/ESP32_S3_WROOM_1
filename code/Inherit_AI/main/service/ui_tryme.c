#include "service/ui_tryme.h"

#include "service/ui_tryme_bridge.h"

#include <esp_log.h>
#include <esp_timer.h>
#include <stdio.h>
#include <string.h>

LV_FONT_DECLARE(font_noto_sans_basic_14_1);
LV_FONT_DECLARE(font_noto_sans_basic_16_4);
LV_FONT_DECLARE(font_noto_sans_basic_20_4);
LV_FONT_DECLARE(font_noto_sans_basic_30_4);

static tryme_ui_ctx_t* s_tryme_ctx = NULL;

static const lv_font_t* ui_tryme_get_metrics_font(void) {
    const lv_font_t* base_font = &BUILTIN_TEXT_FONT;

    if (base_font->line_height >= font_noto_sans_basic_30_4.line_height) {
        return &font_noto_sans_basic_20_4;
    }
    if (base_font->line_height >= font_noto_sans_basic_20_4.line_height) {
        return &font_noto_sans_basic_16_4;
    }
    if (base_font->line_height >= font_noto_sans_basic_16_4.line_height) {
        return &font_noto_sans_basic_14_1;
    }

    return base_font;
}

static void ui_tryme_set_label_text_if_changed(lv_obj_t* label, const char* text) {
    const char* current_text;

    if (label == NULL || text == NULL) {
        return;
    }

    current_text = lv_label_get_text(label);
    if (current_text != NULL && strcmp(current_text, text) == 0) {
        return;
    }

    lv_label_set_text(label, text);
}

static void ui_tryme_set_label_kv_i32(lv_obj_t* label, const char* key, int32_t value) {
    char text[48];

    if (label == NULL || key == NULL) {
        return;
    }

    (void)snprintf(text, sizeof(text), "%s=%ld", key, (long)value);
    ui_tryme_set_label_text_if_changed(label, text);
}

static void ui_tryme_set_label_kv_u32(lv_obj_t* label, const char* key, uint32_t value) {
    char text[48];

    if (label == NULL || key == NULL) {
        return;
    }

    (void)snprintf(text, sizeof(text), "%s=%lu", key, (unsigned long)value);
    ui_tryme_set_label_text_if_changed(label, text);
}

static void ui_tryme_set_age_alert(bool alert) {
    lv_obj_t* age_label;

    if (s_tryme_ctx == NULL ||
        (s_tryme_ctx->age_alert_valid && s_tryme_ctx->age_alert_active == alert)) {
        return;
    }

    age_label = s_tryme_ctx->metric_labels[8];
    if (age_label != NULL) {
        lv_obj_set_style_text_color(
            age_label,
            alert ? lv_color_hex(TRYME_UI_ALERT_HEX) : lv_color_hex(TRYME_UI_TEXT_HEX),
            0);
    }

    s_tryme_ctx->age_alert_active = alert;
    s_tryme_ctx->age_alert_valid = true;
}

static void ui_tryme_update_wave_points(lv_obj_t* wave,
                                        lv_point_precise_t* line_points,
                                        const int16_t* samples,
                                        uint32_t sample_count,
                                        int32_t min_value,
                                        int32_t max_value) {
    lv_coord_t w;
    lv_coord_t h;
    int32_t span;
    uint32_t i;

    if (wave == NULL || line_points == NULL || samples == NULL || sample_count == 0) {
        return;
    }

    w = lv_obj_get_width(wave);
    h = lv_obj_get_height(wave);
    if (w < 4 || h < 4) {
        return;
    }

    if (min_value >= max_value) {
        min_value = -1;
        max_value = 1;
    }

    span = max_value - min_value;
    if (span < 2) {
        span = 2;
    }

    for (i = 0; i < TRYME_UI_WAVE_POINT_COUNT; ++i) {
        uint32_t src_index = (uint32_t)((uint64_t)i * (sample_count - 1) /
                                        (TRYME_UI_WAVE_POINT_COUNT - 1));
        int32_t sample = samples[src_index];

        if (sample < min_value) {
            sample = min_value;
        }
        if (sample > max_value) {
            sample = max_value;
        }

        line_points[i].x = (lv_value_precise_t)((int32_t)i * (w - 1) /
                                                (TRYME_UI_WAVE_POINT_COUNT - 1));
        line_points[i].y = (lv_value_precise_t)((max_value - sample) * (h - 1) / span);
    }

    lv_line_set_points_mutable(wave, line_points, TRYME_UI_WAVE_POINT_COUNT);
}

static void ui_tryme_refresh_metrics(const ui_tryme_mic_snapshot_t* snapshot, bool has_data) {
    char text[48];
    uint64_t age_ms;
    uint32_t age_bucket;
    int32_t pp;

    if (s_tryme_ctx == NULL) {
        return;
    }

    ui_tryme_set_label_kv_i32(s_tryme_ctx->metric_labels[0], "run",
                              s_tryme_ctx->monitor_started ? 1 : 0);

    if (!has_data || snapshot == NULL) {
        ui_tryme_set_label_text_if_changed(s_tryme_ctx->metric_labels[1], "sr=0");
        ui_tryme_set_label_text_if_changed(s_tryme_ctx->metric_labels[2], "n=0");
        ui_tryme_set_label_text_if_changed(s_tryme_ctx->metric_labels[3], "min=0");
        ui_tryme_set_label_text_if_changed(s_tryme_ctx->metric_labels[4], "max=0");
        ui_tryme_set_label_text_if_changed(s_tryme_ctx->metric_labels[5], "pp=0");
        ui_tryme_set_label_text_if_changed(s_tryme_ctx->metric_labels[6], "avg=0");
        ui_tryme_set_label_text_if_changed(s_tryme_ctx->metric_labels[7], "rms=0");
        ui_tryme_set_label_text_if_changed(s_tryme_ctx->metric_labels[8], "age=--");
        s_tryme_ctx->last_age_bucket = UINT32_MAX;
        ui_tryme_set_age_alert(true);
        return;
    }

    ui_tryme_set_label_kv_u32(s_tryme_ctx->metric_labels[1], "sr", snapshot->sample_rate);
    ui_tryme_set_label_kv_u32(s_tryme_ctx->metric_labels[2], "n", snapshot->sample_count);
    ui_tryme_set_label_kv_i32(s_tryme_ctx->metric_labels[3], "min", snapshot->min_value);
    ui_tryme_set_label_kv_i32(s_tryme_ctx->metric_labels[4], "max", snapshot->max_value);

    pp = snapshot->max_value - snapshot->min_value;
    ui_tryme_set_label_kv_i32(s_tryme_ctx->metric_labels[5], "pp", pp);
    ui_tryme_set_label_kv_u32(s_tryme_ctx->metric_labels[6], "avg", snapshot->avg_abs);
    ui_tryme_set_label_kv_u32(s_tryme_ctx->metric_labels[7], "rms", snapshot->rms);

    age_ms = (uint64_t)(esp_timer_get_time() / 1000);
    if (age_ms > snapshot->update_ms) {
        age_ms -= snapshot->update_ms;
    } else {
        age_ms = 0;
    }

    age_bucket = (uint32_t)(age_ms / 500);
    if (s_tryme_ctx->last_age_bucket != age_bucket) {
        (void)snprintf(text, sizeof(text), "age=%llums", (unsigned long long)age_ms);
        ui_tryme_set_label_text_if_changed(s_tryme_ctx->metric_labels[8], text);
        s_tryme_ctx->last_age_bucket = age_bucket;
    }

    ui_tryme_set_age_alert(age_ms > 400);
}

static bool ui_tryme_create_metrics_grid(lv_obj_t* parent) {
    static const char* default_text[TRYME_UI_TEXT_COUNT] = {
        "run=0", "sr=0", "n=0", "min=0", "max=0", "pp=0", "avg=0", "rms=0", "age=--",
    };
    const lv_font_t* metrics_font;
    uint32_t i;

    if (parent == NULL || s_tryme_ctx == NULL) {
        return false;
    }

    metrics_font = ui_tryme_get_metrics_font();

    for (i = 0; i < TRYME_UI_TEXT_COUNT; ++i) {
        lv_obj_t* cell;
        lv_obj_t* label;
        lv_coord_t col;
        lv_coord_t row;

        cell = lv_obj_create(parent);
        if (cell == NULL) {
            return false;
        }

        col = (lv_coord_t)(i % 3U);
        row = (lv_coord_t)(i / 3U);
        lv_obj_set_grid_cell(cell, LV_GRID_ALIGN_STRETCH, col, 1, LV_GRID_ALIGN_STRETCH, row, 1);
        lv_obj_set_style_bg_color(cell, lv_color_hex(TRYME_UI_SURFACE_HEX), 0);
        lv_obj_set_style_bg_opa(cell, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(cell, 1, 0);
        lv_obj_set_style_border_color(cell, lv_color_hex(TRYME_UI_BORDER_HEX), 0);
        lv_obj_set_style_radius(cell, 4, 0);
        lv_obj_set_style_pad_left(cell, 4, 0);
        lv_obj_set_style_pad_right(cell, 2, 0);
        lv_obj_set_style_pad_top(cell, 2, 0);
        lv_obj_set_style_pad_bottom(cell, 2, 0);
        lv_obj_set_scrollbar_mode(cell, LV_SCROLLBAR_MODE_OFF);
        lv_obj_clear_flag(cell, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_clear_flag(cell, LV_OBJ_FLAG_CLICK_FOCUSABLE);
        lv_obj_clear_flag(cell, LV_OBJ_FLAG_SCROLLABLE);

        label = lv_label_create(cell);
        if (label == NULL) {
            return false;
        }
        lv_obj_set_width(label, lv_pct(100));
        lv_obj_set_style_text_color(label, lv_color_hex(TRYME_UI_TEXT_HEX), 0);
        lv_obj_set_style_text_font(label, metrics_font, 0);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_LEFT, 0);
        lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
        lv_obj_align(label, LV_ALIGN_LEFT_MID, 0, 0);
        lv_label_set_text(label, default_text[i]);

        s_tryme_ctx->metric_labels[i] = label;
    }

    return true;
}

static bool ui_tryme_create_wave_card(lv_obj_t* parent, lv_obj_t** plot_out, lv_obj_t** wave_out) {
    lv_obj_t* card;
    lv_obj_t* title_label;
    lv_obj_t* plot;
    lv_obj_t* wave;

    if (parent == NULL || plot_out == NULL || wave_out == NULL) {
        return false;
    }

    card = lv_obj_create(parent);
    if (card == NULL) {
        return false;
    }

    lv_obj_set_grid_cell(card, LV_GRID_ALIGN_STRETCH, 0, 1, LV_GRID_ALIGN_STRETCH, 0, 1);
    lv_obj_set_style_bg_color(card, lv_color_hex(TRYME_UI_SURFACE_HEX), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_border_color(card, lv_color_hex(TRYME_UI_BORDER_HEX), 0);
    lv_obj_set_style_radius(card, 6, 0);
    lv_obj_set_style_pad_all(card, 4, 0);
    lv_obj_set_scrollbar_mode(card, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);

    title_label = lv_label_create(card);
    if (title_label == NULL) {
        return false;
    }
    lv_label_set_text(title_label, "MIC SCOPE");
    lv_obj_set_style_text_color(title_label, lv_color_hex(TRYME_UI_TEXT_SECONDARY_HEX), 0);

    plot = lv_obj_create(card);
    if (plot == NULL) {
        return false;
    }

    lv_obj_set_width(plot, lv_pct(100));
    lv_obj_set_flex_grow(plot, 1);
    lv_obj_set_style_bg_color(plot, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(plot, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(plot, 1, 0);
    lv_obj_set_style_border_color(plot, lv_color_hex(TRYME_UI_BORDER_HEX), 0);
    lv_obj_set_style_radius(plot, 4, 0);
    lv_obj_set_style_pad_all(plot, 1, 0);
    lv_obj_set_scrollbar_mode(plot, LV_SCROLLBAR_MODE_OFF);

    wave = lv_line_create(plot);
    if (wave == NULL) {
        return false;
    }
    lv_obj_set_size(wave, lv_pct(100), lv_pct(100));
    lv_obj_set_style_line_width(wave, 2, 0);
    lv_obj_set_style_line_color(wave, lv_color_hex(TRYME_UI_WAVE_HEX), 0);
    lv_obj_set_style_line_opa(wave, LV_OPA_COVER, 0);
    lv_obj_clear_flag(wave, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(wave, LV_OBJ_FLAG_SCROLLABLE);

    *plot_out = plot;
    *wave_out = wave;
    return true;
}

static void ui_tryme_refresh_view(void) {
    ui_tryme_mic_snapshot_t snapshot = {0};
    static const int16_t idle_samples[2] = {0, 0};
    int16_t samples[160] = {0};
    uint32_t sample_count = 0;
    bool has_data;

    if (s_tryme_ctx == NULL) {
        return;
    }

    has_data = ui_tryme_mic_monitor_read(samples, 160, &sample_count, &snapshot);
    if (has_data && sample_count > 1) {
        ui_tryme_update_wave_points(s_tryme_ctx->wave,
                                    s_tryme_ctx->line_points,
                                    samples,
                                    sample_count,
                                    snapshot.min_value,
                                    snapshot.max_value);
        s_tryme_ctx->wave_idle = false;
    } else {
        if (!s_tryme_ctx->wave_idle) {
            ui_tryme_update_wave_points(s_tryme_ctx->wave,
                                        s_tryme_ctx->line_points,
                                        idle_samples,
                                        2,
                                        -1,
                                        1);
            s_tryme_ctx->wave_idle = true;
        }
    }

    ui_tryme_refresh_metrics(&snapshot, has_data && sample_count > 0);
}

static void ui_tryme_timer_cb(lv_timer_t* timer) {
    tryme_ui_ctx_t* ctx;

    if (timer == NULL || s_tryme_ctx == NULL) {
        return;
    }

    ctx = (tryme_ui_ctx_t*)lv_timer_get_user_data(timer);
    if (ctx == NULL || ctx != s_tryme_ctx) {
        return;
    }

    ui_tryme_refresh_view();
}

static bool ui_tryme_create_panel_locked(void) {
    static lv_coord_t chart_col_dsc[] = {LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
    static lv_coord_t chart_row_dsc[] = {LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
    static lv_coord_t metrics_col_dsc[] = {
        LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST,
    };
    static lv_coord_t metrics_row_dsc[] = {
        LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST,
    };
    lv_obj_t* screen;
    lv_obj_t* chart_grid;
    lv_obj_t* metrics_grid;
    lv_coord_t screen_w;
    lv_coord_t screen_h;

    if (s_tryme_ctx != NULL) {
        return true;
    }

    screen = lv_screen_active();
    if (screen == NULL) {
        return false;
    }

    s_tryme_ctx = (tryme_ui_ctx_t*)lv_malloc(sizeof(tryme_ui_ctx_t));
    if (s_tryme_ctx == NULL) {
        return false;
    }
    memset(s_tryme_ctx, 0, sizeof(tryme_ui_ctx_t));
    s_tryme_ctx->last_age_bucket = UINT32_MAX;

    screen_w = lv_obj_get_width(screen);
    screen_h = lv_obj_get_height(screen);
    if (screen_w < 80 || screen_h < 80) {
        lv_free(s_tryme_ctx);
        s_tryme_ctx = NULL;
        return false;
    }

    s_tryme_ctx->panel = lv_obj_create(screen);
    if (s_tryme_ctx->panel == NULL) {
        lv_free(s_tryme_ctx);
        s_tryme_ctx = NULL;
        return false;
    }

    lv_obj_set_size(s_tryme_ctx->panel, screen_w - (2 * TRYME_UI_MARGIN), screen_h - (2 * TRYME_UI_MARGIN));
    lv_obj_set_pos(s_tryme_ctx->panel, TRYME_UI_MARGIN, TRYME_UI_MARGIN);
    lv_obj_set_style_bg_color(s_tryme_ctx->panel, lv_color_hex(TRYME_UI_BG_HEX), 0);
    lv_obj_set_style_bg_opa(s_tryme_ctx->panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_tryme_ctx->panel, 1, 0);
    lv_obj_set_style_border_color(s_tryme_ctx->panel, lv_color_hex(TRYME_UI_BORDER_HEX), 0);
    lv_obj_set_style_radius(s_tryme_ctx->panel, 8, 0);
    lv_obj_set_style_pad_all(s_tryme_ctx->panel, 4, 0);
    lv_obj_set_style_pad_row(s_tryme_ctx->panel, TRYME_UI_GAP, 0);
    lv_obj_set_scrollbar_mode(s_tryme_ctx->panel, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(s_tryme_ctx->panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(s_tryme_ctx->panel, LV_FLEX_FLOW_COLUMN);

    chart_grid = lv_obj_create(s_tryme_ctx->panel);
    if (chart_grid == NULL) {
        goto fail;
    }
    lv_obj_set_width(chart_grid, lv_pct(100));
    lv_obj_set_flex_grow(chart_grid, 12);
    lv_obj_set_style_bg_opa(chart_grid, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(chart_grid, 0, 0);
    lv_obj_set_style_pad_all(chart_grid, 0, 0);
    lv_obj_set_scrollbar_mode(chart_grid, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_grid_dsc_array(chart_grid, chart_col_dsc, chart_row_dsc);

    if (!ui_tryme_create_wave_card(chart_grid, &s_tryme_ctx->plot, &s_tryme_ctx->wave)) {
        goto fail;
    }

    metrics_grid = lv_obj_create(s_tryme_ctx->panel);
    if (metrics_grid == NULL) {
        goto fail;
    }
    lv_obj_set_width(metrics_grid, lv_pct(100));
    lv_obj_set_flex_grow(metrics_grid, 8);
    lv_obj_set_style_bg_opa(metrics_grid, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(metrics_grid, 0, 0);
    lv_obj_set_style_pad_all(metrics_grid, 0, 0);
    lv_obj_set_style_pad_row(metrics_grid, 2, 0);
    lv_obj_set_style_pad_column(metrics_grid, 2, 0);
    lv_obj_set_scrollbar_mode(metrics_grid, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_grid_dsc_array(metrics_grid, metrics_col_dsc, metrics_row_dsc);

    if (!ui_tryme_create_metrics_grid(metrics_grid)) {
        goto fail;
    }

    s_tryme_ctx->timer = lv_timer_create(ui_tryme_timer_cb, TRYME_UI_REFRESH_MS, s_tryme_ctx);
    if (s_tryme_ctx->timer == NULL) {
        goto fail;
    }

    return true;

fail:
    if (s_tryme_ctx != NULL) {
        if (s_tryme_ctx->timer != NULL) {
            lv_timer_del(s_tryme_ctx->timer);
            s_tryme_ctx->timer = NULL;
        }
        if (s_tryme_ctx->panel != NULL && lv_obj_is_valid(s_tryme_ctx->panel)) {
            lv_obj_del(s_tryme_ctx->panel);
        }
        lv_free(s_tryme_ctx);
        s_tryme_ctx = NULL;
    }
    return false;
}

static void ui_tryme_destroy_panel_locked(void) {
    if (s_tryme_ctx == NULL) {
        return;
    }

    if (s_tryme_ctx->timer != NULL) {
        lv_timer_del(s_tryme_ctx->timer);
        s_tryme_ctx->timer = NULL;
    }

    if (s_tryme_ctx->panel != NULL && lv_obj_is_valid(s_tryme_ctx->panel)) {
        lv_obj_del(s_tryme_ctx->panel);
    }

    lv_free(s_tryme_ctx);
    s_tryme_ctx = NULL;
}

static const service_key_binding_t k_bindings[] = {
    {0, SERVICE_KEY_EVENT_CLICK, SERVICE_CMD_NONE, "TryMe: previous"},
    {1, SERVICE_KEY_EVENT_CLICK, SERVICE_CMD_NONE, "TryMe: refresh"},
    {1, SERVICE_KEY_EVENT_LONG_PRESS, SERVICE_CMD_ENTER_DESKTOP, "Desktop"},
};

const service_item_t g_service_tryme = {
    9,
    "TryMe",
    "TryMe Service",
    "Mic waveform and live metrics",
    k_bindings,
    3,
};

void ui_tryme_on_enter(void) {
    if (!ui_tryme_mic_monitor_start()) {
        ESP_LOGW(UI_TRYME_LOG_TAG, "Mic monitor start failed");
    }

    if (!lvgl_port_lock(DESKTOP_LVGL_LOCK_TIMEOUT_MS)) {
        ui_tryme_mic_monitor_stop();
        return;
    }

    if (!ui_tryme_create_panel_locked()) {
        lvgl_port_unlock();
        ESP_LOGE(UI_TRYME_LOG_TAG, "TryMe panel create failed");
        ui_tryme_mic_monitor_stop();
        return;
    }

    s_tryme_ctx->monitor_started = true;
    ui_tryme_refresh_view();
    lvgl_port_unlock();
}

void ui_tryme_on_leave(void) {
    if (s_tryme_ctx != NULL) {
        s_tryme_ctx->monitor_started = false;
    }

    ui_tryme_mic_monitor_stop();

    if (!lvgl_port_lock(DESKTOP_LVGL_LOCK_TIMEOUT_MS)) {
        return;
    }
    ui_tryme_destroy_panel_locked();
    lvgl_port_unlock();
}

void ui_tryme_on_key_event(uint8_t key_index, uint8_t event_type) {
    (void)key_index;

    if (event_type != SERVICE_KEY_EVENT_CLICK) {
        return;
    }

    if (!lvgl_port_lock(DESKTOP_LVGL_LOCK_TIMEOUT_MS)) {
        return;
    }

    ui_tryme_refresh_view();
    lvgl_port_unlock();
}
