#include "service/ui_pidm.h"

#include "peripherals/pidm_det.h"

#include <esp_log.h>
#include <stdio.h>
#include <string.h>

static bool s_pidm_enabled = false;
static pidm_ui_ctx_t* s_pidm_ctx = NULL;

static esp_err_t ui_pidm_ensure_ready(void) {
    if (pidm_det_is_ready()) {
        return ESP_OK;
    }

    return pidm_det_init();
}

static void ui_pidm_set_text_kv_i32(lv_obj_t* textedit, const char* key, int32_t value) {
    char text[48];

    if (textedit == NULL || key == NULL) {
        return;
    }

    (void)snprintf(text, sizeof(text), "%s=%ld", key, (long)value);
    lv_textarea_set_text(textedit, text);
}

static void ui_pidm_set_text_kv_u32(lv_obj_t* textedit, const char* key, uint32_t value) {
    char text[48];

    if (textedit == NULL || key == NULL) {
        return;
    }

    (void)snprintf(text, sizeof(text), "%s=%lu", key, (unsigned long)value);
    lv_textarea_set_text(textedit, text);
}

static void ui_pidm_push_point(int16_t* points, int32_t sample) {
    if (points == NULL) {
        return;
    }

    memmove(points, points + 1, (PIDM_UI_POINT_COUNT - 1) * sizeof(points[0]));
    if (sample > 32767) {
        sample = 32767;
    }
    if (sample < -32768) {
        sample = -32768;
    }
    points[PIDM_UI_POINT_COUNT - 1] = (int16_t)sample;
}

static void ui_pidm_get_range(const int16_t* points, int32_t* min_out, int32_t* max_out) {
    int32_t min_value;
    int32_t max_value;
    int32_t span;
    int32_t margin;
    uint32_t i;

    if (points == NULL || min_out == NULL || max_out == NULL) {
        return;
    }

    min_value = points[0];
    max_value = points[0];
    for (i = 1; i < PIDM_UI_POINT_COUNT; ++i) {
        if (points[i] < min_value) {
            min_value = points[i];
        }
        if (points[i] > max_value) {
            max_value = points[i];
        }
    }

    span = max_value - min_value;
    if (span < 20) {
        int32_t center = (min_value + max_value) / 2;
        min_value = center - 10;
        max_value = center + 10;
        span = 20;
    }

    margin = span / 8;
    if (margin < 2) {
        margin = 2;
    }

    *min_out = min_value - margin;
    *max_out = max_value + margin;
}

static void ui_pidm_update_wave_points(lv_obj_t* wave,
                                       const int16_t* samples,
                                       lv_point_precise_t* line_points) {
    lv_coord_t w;
    lv_coord_t h;
    int32_t min_value;
    int32_t max_value;
    int32_t span;
    uint32_t i;

    if (wave == NULL || samples == NULL || line_points == NULL) {
        return;
    }

    w = lv_obj_get_width(wave);
    h = lv_obj_get_height(wave);
    if (w < 4 || h < 4) {
        return;
    }

    ui_pidm_get_range(samples, &min_value, &max_value);
    span = max_value - min_value;
    if (span < 1) {
        span = 1;
    }

    for (i = 0; i < PIDM_UI_POINT_COUNT; ++i) {
        int32_t sample;

        sample = samples[i];
        if (sample < min_value) {
            sample = min_value;
        }
        if (sample > max_value) {
            sample = max_value;
        }

        line_points[i].x = (lv_value_precise_t)((int32_t)i * (w - 1) / (PIDM_UI_POINT_COUNT - 1));
        line_points[i].y = (lv_value_precise_t)((max_value - sample) * (h - 1) / span);
    }

    lv_line_set_points_mutable(wave, line_points, PIDM_UI_POINT_COUNT);
    lv_obj_invalidate(wave);
}

static void ui_pidm_refresh_metrics(const pidm_det_feature_s* feature, esp_err_t probe_ret) {
    char text[64];

    if (s_pidm_ctx == NULL) {
        return;
    }

    ui_pidm_set_text_kv_i32(s_pidm_ctx->text_edits[0], "run", s_pidm_enabled ? 1 : 0);

    if (probe_ret != ESP_OK) {
        (void)snprintf(text, sizeof(text), "probe_err=%d", (int)probe_ret);
        lv_textarea_set_text(s_pidm_ctx->text_edits[8], text);
        return;
    }

    if (feature == NULL) {
        return;
    }

    ui_pidm_set_text_kv_i32(s_pidm_ctx->text_edits[1], "metal", feature->metal_present ? 1 : 0);
    lv_obj_set_style_text_color(
        s_pidm_ctx->text_edits[1],
        feature->metal_present ? lv_color_hex(PIDM_UI_ALERT_HEX) : lv_color_hex(PIDM_UI_TEXT_HEX),
        0);
    ui_pidm_set_text_kv_i32(s_pidm_ctx->text_edits[2], "peak", feature->peak_raw);
    ui_pidm_set_text_kv_i32(s_pidm_ctx->text_edits[3], "dpk", feature->peak_delta_raw);
    ui_pidm_set_text_kv_u32(s_pidm_ctx->text_edits[4], "slope", feature->rise_slope_adc_per_ms);
    ui_pidm_set_text_kv_u32(s_pidm_ctx->text_edits[5], "dsl", feature->slope_delta_adc_per_ms);
    ui_pidm_set_text_kv_u32(s_pidm_ctx->text_edits[6], "hold", feature->high_hold_us);
    ui_pidm_set_text_kv_u32(s_pidm_ctx->text_edits[7], "area", feature->area_adc_us);
    (void)snprintf(text, sizeof(text), "base=%d thr=%d", feature->baseline_raw, feature->threshold_raw);
    lv_textarea_set_text(s_pidm_ctx->text_edits[8], text);
}

static void ui_pidm_refresh_charts(const pidm_det_feature_s* feature) {
    int32_t slope_sample;

    if (s_pidm_ctx == NULL || feature == NULL) {
        return;
    }

    ui_pidm_push_point(s_pidm_ctx->dpk_points, feature->peak_delta_raw);
    slope_sample = (int32_t)(feature->rise_slope_adc_per_ms / 20U);
    if (slope_sample > 2000) {
        slope_sample = 2000;
    }
    ui_pidm_push_point(s_pidm_ctx->slope_points, slope_sample);

    ui_pidm_update_wave_points(s_pidm_ctx->wave_dpk,
                               s_pidm_ctx->dpk_points,
                               s_pidm_ctx->dpk_line_points);
    ui_pidm_update_wave_points(s_pidm_ctx->wave_slope,
                               s_pidm_ctx->slope_points,
                               s_pidm_ctx->slope_line_points);
}

static void ui_pidm_probe_and_render(void) {
    pidm_det_feature_s feature = {0};
    esp_err_t ret = ESP_ERR_INVALID_STATE;

    if (!s_pidm_enabled) {
        ui_pidm_refresh_metrics(NULL, ESP_ERR_INVALID_STATE);
        return;
    }

    ret = pidm_det_probe_feature(PIDM_DET_PULSE_US_DEFAULT, &feature);
    if (ret != ESP_OK) {
        ui_pidm_refresh_metrics(NULL, ret);
        return;
    }

    ui_pidm_refresh_charts(&feature);
    ui_pidm_refresh_metrics(&feature, ESP_OK);
}

static void ui_pidm_timer_cb(lv_timer_t* timer) {
    pidm_ui_ctx_t* ctx;

    if (timer == NULL) {
        return;
    }

    ctx = (pidm_ui_ctx_t*)lv_timer_get_user_data(timer);
    if (ctx == NULL || ctx != s_pidm_ctx) {
        return;
    }

    ui_pidm_probe_and_render();
}

static bool ui_pidm_create_wave_card(lv_obj_t* parent, lv_coord_t col, const char* title,
                                     lv_color_t line_color, lv_obj_t** plot_out,
                                     lv_obj_t** wave_out) {
    lv_obj_t* card;
    lv_obj_t* title_label;
    lv_obj_t* plot;
    lv_obj_t* wave;

    if (parent == NULL || title == NULL || plot_out == NULL || wave_out == NULL) {
        return false;
    }

    card = lv_obj_create(parent);
    if (card == NULL) {
        return false;
    }

    lv_obj_set_grid_cell(card, LV_GRID_ALIGN_STRETCH, col, 1, LV_GRID_ALIGN_STRETCH, 0, 1);
    lv_obj_set_style_bg_color(card, lv_color_hex(PIDM_UI_SURFACE_HEX), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_border_color(card, lv_color_hex(PIDM_UI_BORDER_HEX), 0);
    lv_obj_set_style_radius(card, 6, 0);
    lv_obj_set_style_pad_all(card, 2, 0);
    lv_obj_set_style_pad_row(card, 2, 0);
    lv_obj_set_scrollbar_mode(card, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);

    title_label = lv_label_create(card);
    lv_label_set_text(title_label, title);
    lv_obj_set_style_text_color(title_label, lv_color_hex(PIDM_UI_TEXT_SECONDARY_HEX), 0);

    plot = lv_obj_create(card);
    if (plot == NULL) {
        return false;
    }

    lv_obj_set_width(plot, lv_pct(100));
    lv_obj_set_flex_grow(plot, 1);
    lv_obj_set_style_bg_color(plot, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(plot, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(plot, 1, 0);
    lv_obj_set_style_border_color(plot, lv_color_hex(PIDM_UI_BORDER_HEX), 0);
    lv_obj_set_style_radius(plot, 4, 0);
    lv_obj_set_style_pad_all(plot, 1, 0);
    lv_obj_set_scrollbar_mode(plot, LV_SCROLLBAR_MODE_OFF);

    wave = lv_line_create(plot);
    if (wave == NULL) {
        return false;
    }
    lv_obj_set_size(wave, lv_pct(100), lv_pct(100));
    lv_obj_set_style_line_width(wave, 2, 0);
    lv_obj_set_style_line_color(wave, line_color, 0);
    lv_obj_set_style_line_opa(wave, LV_OPA_COVER, 0);
    lv_obj_clear_flag(wave, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(wave, LV_OBJ_FLAG_SCROLLABLE);

    *plot_out = plot;
    *wave_out = wave;
    return true;
}

static bool ui_pidm_create_metrics_grid(lv_obj_t* parent) {
    static const char* default_text[PIDM_UI_TEXT_COUNT] = {
        "run=0", "metal=0", "peak=0", "dpk=0", "slope=0", "dsl=0", "hold=0", "area=0", "base=0 thr=0",
    };
    uint32_t i;

    if (parent == NULL || s_pidm_ctx == NULL) {
        return false;
    }

    for (i = 0; i < PIDM_UI_TEXT_COUNT; ++i) {
        lv_obj_t* textedit;
        lv_coord_t col;
        lv_coord_t row;

        textedit = lv_textarea_create(parent);
        if (textedit == NULL) {
            return false;
        }

        col = (lv_coord_t)(i % 3U);
        row = (lv_coord_t)(i / 3U);
        lv_obj_set_grid_cell(textedit, LV_GRID_ALIGN_STRETCH, col, 1, LV_GRID_ALIGN_STRETCH, row, 1);
        lv_obj_set_style_bg_color(textedit, lv_color_hex(PIDM_UI_SURFACE_HEX), 0);
        lv_obj_set_style_bg_opa(textedit, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(textedit, 1, 0);
        lv_obj_set_style_border_color(textedit, lv_color_hex(PIDM_UI_BORDER_HEX), 0);
        lv_obj_set_style_radius(textedit, 4, 0);
        lv_obj_set_style_text_color(textedit, lv_color_hex(PIDM_UI_TEXT_HEX), 0);
        lv_obj_set_style_pad_all(textedit, 3, 0);
        lv_obj_set_scrollbar_mode(textedit, LV_SCROLLBAR_MODE_OFF);
        lv_textarea_set_one_line(textedit, true);
        lv_obj_clear_flag(textedit, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_clear_flag(textedit, LV_OBJ_FLAG_CLICK_FOCUSABLE);
        lv_obj_clear_flag(textedit, LV_OBJ_FLAG_SCROLLABLE);
        lv_textarea_set_text(textedit, default_text[i]);

        s_pidm_ctx->text_edits[i] = textedit;
    }

    return true;
}

static bool ui_pidm_create_panel_locked(void) {
    static lv_coord_t chart_col_dsc[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
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
    bool ok;

    if (s_pidm_ctx != NULL) {
        return true;
    }

    screen = lv_screen_active();
    if (screen == NULL) {
        return false;
    }

    s_pidm_ctx = (pidm_ui_ctx_t*)lv_malloc(sizeof(pidm_ui_ctx_t));
    if (s_pidm_ctx == NULL) {
        return false;
    }
    memset(s_pidm_ctx, 0, sizeof(pidm_ui_ctx_t));

    screen_w = lv_obj_get_width(screen);
    screen_h = lv_obj_get_height(screen);
    if (screen_w < 80 || screen_h < 80) {
        lv_free(s_pidm_ctx);
        s_pidm_ctx = NULL;
        return false;
    }

    s_pidm_ctx->panel = lv_obj_create(screen);
    if (s_pidm_ctx->panel == NULL) {
        lv_free(s_pidm_ctx);
        s_pidm_ctx = NULL;
        return false;
    }

    lv_obj_set_size(s_pidm_ctx->panel, screen_w - (2 * PIDM_UI_MARGIN), screen_h - (2 * PIDM_UI_MARGIN));
    lv_obj_set_pos(s_pidm_ctx->panel, PIDM_UI_MARGIN, PIDM_UI_MARGIN);
    lv_obj_set_style_bg_color(s_pidm_ctx->panel, lv_color_hex(PIDM_UI_BG_HEX), 0);
    lv_obj_set_style_bg_opa(s_pidm_ctx->panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_pidm_ctx->panel, 1, 0);
    lv_obj_set_style_border_color(s_pidm_ctx->panel, lv_color_hex(PIDM_UI_BORDER_HEX), 0);
    lv_obj_set_style_radius(s_pidm_ctx->panel, 8, 0);
    lv_obj_set_style_pad_all(s_pidm_ctx->panel, 4, 0);
    lv_obj_set_style_pad_row(s_pidm_ctx->panel, PIDM_UI_GAP, 0);
    lv_obj_set_scrollbar_mode(s_pidm_ctx->panel, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(s_pidm_ctx->panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(s_pidm_ctx->panel, LV_FLEX_FLOW_COLUMN);

    chart_grid = lv_obj_create(s_pidm_ctx->panel);
    if (chart_grid == NULL) {
        return false;
    }
    lv_obj_set_width(chart_grid, lv_pct(100));
    lv_obj_set_flex_grow(chart_grid, 11);
    lv_obj_set_style_bg_opa(chart_grid, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(chart_grid, 0, 0);
    lv_obj_set_style_pad_all(chart_grid, 0, 0);
    lv_obj_set_style_pad_column(chart_grid, PIDM_UI_GAP, 0);
    lv_obj_set_scrollbar_mode(chart_grid, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_grid_dsc_array(chart_grid, chart_col_dsc, chart_row_dsc);

    ok = ui_pidm_create_wave_card(chart_grid,
                                  0,
                                  "DPK",
                                  lv_color_hex(PIDM_UI_DPK_HEX),
                                  &s_pidm_ctx->plot_dpk,
                                  &s_pidm_ctx->wave_dpk);
    if (!ok) {
        goto fail;
    }

    ok = ui_pidm_create_wave_card(chart_grid,
                                  1,
                                  "SLOPE/20",
                                  lv_color_hex(PIDM_UI_SLOPE_HEX),
                                  &s_pidm_ctx->plot_slope,
                                  &s_pidm_ctx->wave_slope);
    if (!ok) {
        goto fail;
    }

    metrics_grid = lv_obj_create(s_pidm_ctx->panel);
    if (metrics_grid == NULL) {
        return false;
    }
    lv_obj_set_width(metrics_grid, lv_pct(100));
    lv_obj_set_flex_grow(metrics_grid, 9);
    lv_obj_set_style_bg_opa(metrics_grid, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(metrics_grid, 0, 0);
    lv_obj_set_style_pad_all(metrics_grid, 0, 0);
    lv_obj_set_style_pad_row(metrics_grid, 2, 0);
    lv_obj_set_style_pad_column(metrics_grid, 2, 0);
    lv_obj_set_scrollbar_mode(metrics_grid, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_grid_dsc_array(metrics_grid, metrics_col_dsc, metrics_row_dsc);

    if (!ui_pidm_create_metrics_grid(metrics_grid)) {
        goto fail;
    }

    s_pidm_ctx->timer = lv_timer_create(ui_pidm_timer_cb, PIDM_UI_REFRESH_MS, s_pidm_ctx);
    if (s_pidm_ctx->timer == NULL) {
        goto fail;
    }

    return true;

fail:
    if (s_pidm_ctx != NULL) {
        if (s_pidm_ctx->timer != NULL) {
            lv_timer_del(s_pidm_ctx->timer);
            s_pidm_ctx->timer = NULL;
        }
        if (s_pidm_ctx->panel != NULL && lv_obj_is_valid(s_pidm_ctx->panel)) {
            lv_obj_del(s_pidm_ctx->panel);
        }
        lv_free(s_pidm_ctx);
        s_pidm_ctx = NULL;
    }
    return false;
}

static void ui_pidm_destroy_panel_locked(void) {
    if (s_pidm_ctx == NULL) {
        return;
    }

    if (s_pidm_ctx->timer != NULL) {
        lv_timer_del(s_pidm_ctx->timer);
        s_pidm_ctx->timer = NULL;
    }

    if (s_pidm_ctx->panel != NULL && lv_obj_is_valid(s_pidm_ctx->panel)) {
        lv_obj_del(s_pidm_ctx->panel);
    }

    lv_free(s_pidm_ctx);
    s_pidm_ctx = NULL;
}

static const service_key_binding_t k_bindings[] = {
    {0, SERVICE_KEY_EVENT_CLICK, SERVICE_CMD_NONE, "PIDM: probe"},
    {1, SERVICE_KEY_EVENT_CLICK, SERVICE_CMD_NONE, "PIDM: toggle"},
    {1, SERVICE_KEY_EVENT_LONG_PRESS, SERVICE_CMD_ENTER_DESKTOP, "Desktop"},
};

const service_item_t g_service_pidm = {
    11,
    "PIDM",
    "PIDM Service",
    "PI metal detector",
    k_bindings,
    3,
};

void ui_pidm_on_enter(void) {
    esp_err_t ret;

    ret = ui_pidm_ensure_ready();
    if (ret == ESP_OK) {
        ret = pidm_det_set_enable(true);
        if (ret == ESP_OK) {
            s_pidm_enabled = true;
        } else {
            s_pidm_enabled = false;
            ESP_LOGW(UI_PIDM_LOG_TAG, "PIDM enable failed on enter: %d", (int)ret);
        }
    } else {
        s_pidm_enabled = false;
        ESP_LOGW(UI_PIDM_LOG_TAG, "PIDM init failed on enter: %d", (int)ret);
    }

    if (!lvgl_port_lock(DESKTOP_LVGL_LOCK_TIMEOUT_MS)) {
        return;
    }

    if (!ui_pidm_create_panel_locked()) {
        lvgl_port_unlock();
        return;
    }

    ui_pidm_refresh_metrics(NULL, s_pidm_enabled ? ESP_OK : ret);
    if (s_pidm_enabled) {
        ui_pidm_probe_and_render();
    }

    lvgl_port_unlock();
}

void ui_pidm_on_leave(void) {
    if (pidm_det_is_ready()) {
        (void)pidm_det_set_enable(false);
    }

    s_pidm_enabled = false;

    if (!lvgl_port_lock(DESKTOP_LVGL_LOCK_TIMEOUT_MS)) {
        return;
    }
    ui_pidm_destroy_panel_locked();
    lvgl_port_unlock();
}

void ui_pidm_on_key_event(uint8_t key_index, uint8_t event_type) {
    esp_err_t ret;
    pidm_det_feature_s feature = {0};

    if (event_type != SERVICE_KEY_EVENT_CLICK) {
        return;
    }

    ret = ui_pidm_ensure_ready();
    if (ret != ESP_OK) {
        ESP_LOGW(UI_PIDM_LOG_TAG, "PIDM init failed: %d", (int)ret);
        return;
    }

    if (key_index == 1) {
        s_pidm_enabled = !s_pidm_enabled;
        ret = pidm_det_set_enable(s_pidm_enabled);
        if (ret != ESP_OK) {
            s_pidm_enabled = false;
            ESP_LOGW(UI_PIDM_LOG_TAG, "PIDM toggle failed: %d", (int)ret);
        }

        if (lvgl_port_lock(DESKTOP_LVGL_LOCK_TIMEOUT_MS)) {
            ui_pidm_refresh_metrics(NULL, s_pidm_enabled ? ESP_OK : ret);
            lvgl_port_unlock();
        }

        ESP_LOGI(UI_PIDM_LOG_TAG, "PIDM %s", s_pidm_enabled ? "ON" : "OFF");
        return;
    }

    if (key_index == 0) {
        if (!s_pidm_enabled) {
            ret = pidm_det_set_enable(true);
            if (ret != ESP_OK) {
                ESP_LOGW(UI_PIDM_LOG_TAG, "PIDM enable failed: %d", (int)ret);
                return;
            }
            s_pidm_enabled = true;
        }

        ret = pidm_det_probe_feature(PIDM_DET_PULSE_US_DEFAULT, &feature);
        if (ret != ESP_OK) {
            ESP_LOGW(UI_PIDM_LOG_TAG, "PIDM probe failed: %d", (int)ret);
            if (lvgl_port_lock(DESKTOP_LVGL_LOCK_TIMEOUT_MS)) {
                ui_pidm_refresh_metrics(NULL, ret);
                lvgl_port_unlock();
            }
            return;
        }

        if (lvgl_port_lock(DESKTOP_LVGL_LOCK_TIMEOUT_MS)) {
            ui_pidm_refresh_charts(&feature);
            ui_pidm_refresh_metrics(&feature, ESP_OK);
            lvgl_port_unlock();
        }

        ESP_LOGI(UI_PIDM_LOG_TAG,
                 "PIDM metal=%d peak=%d dpk=%d slope=%u hold=%u area=%u",
                 feature.metal_present ? 1 : 0,
                 feature.peak_raw,
                 feature.peak_delta_raw,
                 (unsigned)feature.rise_slope_adc_per_ms,
                 (unsigned)feature.high_hold_us,
                 (unsigned)feature.area_adc_us);
    }
}
