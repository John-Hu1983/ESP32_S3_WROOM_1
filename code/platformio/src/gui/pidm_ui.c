#include "pidm_ui.h"

#define TAG "PIDM"

#define PIDM_CHART_DPK_COLOR_HEX 0xE95420
#define PIDM_CHART_SLOPE_COLOR_HEX 0x2FB5E2
#define PIDM_METAL_TEXT_ALERT_HEX 0xFF3B30

#define PIDM_TEXT_BUF_LEN 64U
#define PIDM_BEEP_DPK_NORM_MAX 800.0f
#define PIDM_BEEP_SLOPE_NORM_MAX 5000.0f
#define PIDM_BEEP_LEVEL_COUNT 5U

static pidm_app_ctx_t *s_pidm_ctx = NULL;
static TaskHandle_t s_pidm_input_task_handle = NULL;
static TaskHandle_t s_pidm_probe_task_handle = NULL;
static volatile bool s_pidm_input_task_stop = false;
static volatile bool s_pidm_probe_task_stop = false;
static portMUX_TYPE s_pidm_feature_lock = portMUX_INITIALIZER_UNLOCKED;

/*
 * brief: Clamp one float value into [0.0, 1.0] interval.
 * input: value - source float value.
 * output: clamped float value.
 */
static float _pidm_clamp_unit(float value)
{
    if (value < 0.0f)
    {
        return 0.0f;
    }

    if (value > 1.0f)
    {
        return 1.0f;
    }

    return value;
}

/*
 * brief: Calculate parking-radar-like stepped beep interval from dpk and slope.
 * input: ctx - PIDM app context; feature - PIDM feature snapshot.
 * output: stepped interval milliseconds with level hysteresis for stable cadence.
 */
static uint32_t _pidm_calc_beep_interval_ms(pidm_app_ctx_t *ctx, const pidm_det_feature_s *feature)
{
    static const uint32_t s_beep_interval_ms[PIDM_BEEP_LEVEL_COUNT] = {
        2000U,
        1200U,
        700U,
        350U,
        100U,
    };
    static const float s_level_up_th[PIDM_BEEP_LEVEL_COUNT - 1U] = {
        0.20f,
        0.40f,
        0.60f,
        0.80f,
    };
    static const float s_level_down_th[PIDM_BEEP_LEVEL_COUNT - 1U] = {
        0.12f,
        0.32f,
        0.52f,
        0.72f,
    };
    float dpk_norm;
    float slope_norm;
    float strength;
    uint8_t level;
    int32_t dpk_raw;

    if ((ctx == NULL) || (feature == NULL))
    {
        return PIDM_BEEP_INTERVAL_MAX_MS;
    }

    dpk_raw = feature->peak_delta_raw;
    if (dpk_raw < 0)
    {
        dpk_raw = 0;
    }

    dpk_norm = _pidm_clamp_unit((float)dpk_raw / PIDM_BEEP_DPK_NORM_MAX);
    slope_norm = _pidm_clamp_unit((float)feature->rise_slope_adc_per_ms / PIDM_BEEP_SLOPE_NORM_MAX);

    strength = (0.60f * dpk_norm) + (0.40f * slope_norm);
    level = ctx->beep_level;
    if (level >= PIDM_BEEP_LEVEL_COUNT)
    {
        level = (PIDM_BEEP_LEVEL_COUNT - 1U);
    }

    while ((level < (PIDM_BEEP_LEVEL_COUNT - 1U)) && (strength >= s_level_up_th[level]))
    {
        level++;
    }

    while ((level > 0U) && (strength < s_level_down_th[level - 1U]))
    {
        level--;
    }

    ctx->beep_level = level;
    return s_beep_interval_ms[level];
}

/*
 * brief: Schedule Di.ogg playback when metal is present using adaptive interval.
 * input: ctx - PIDM app context; feature - latest feature snapshot.
 * output: None.
 */
static void _pidm_try_play_metal_beep(pidm_app_ctx_t *ctx, const pidm_det_feature_s *feature)
{
    int64_t now_ms;
    uint32_t interval_ms;
    voice_info_s voice_info;
    esp_err_t voice_ret;

    if ((ctx == NULL) || (feature == NULL))
    {
        return;
    }

    if (!feature->metal_present)
    {
        ctx->last_beep_ts_ms = 0;
        ctx->beep_level = 0U;
        return;
    }

    if (ctx->di_ogg_path[0] == '\0')
    {
        return;
    }

    interval_ms = _pidm_calc_beep_interval_ms(ctx, feature);
    now_ms = (int64_t)(esp_timer_get_time() / 1000LL);

    if ((ctx->last_beep_ts_ms > 0) &&
        ((now_ms - ctx->last_beep_ts_ms) < (int64_t)interval_ms))
    {
        return;
    }

    voice_info = voice_read_info();
    if (!voice_info.ready || voice_info.playing || (voice_info.queue_len > 0U))
    {
        return;
    }

    voice_ret = voice_load_file(ctx->di_ogg_path);
    if (voice_ret != ESP_OK)
    {
        ESP_LOGW(TAG,
                 "voice_load_file failed: %d path=%s",
                 (int)voice_ret,
                 ctx->di_ogg_path);
    }

    ctx->last_beep_ts_ms = now_ms;
}

/*
 * brief: Input task for PIDM app to own key scanning while app is active.
 * input: param - unused task parameter.
 * output: None.
 */
static void _pidm_input_task(void *param)
{
    btn_scan_s btn;
    bool home_requested;

    (void)param;
    lv_memset_00(&btn, sizeof(btn));
    home_requested = false;

    while (!s_pidm_input_task_stop)
    {
        btn_status_e btn_val;

        btn_val = keyboard_scan_event(&btn, PIDM_INPUT_SCAN_PERIOD_MS);
        if ((btn_val == Btn_Both_Click) && !home_requested)
        {
            home_requested = true;
            desktop_return_to_home();
        }

        delay_ms(PIDM_INPUT_SCAN_PERIOD_MS);
    }

    s_pidm_input_task_handle = NULL;
    vTaskDelete(NULL);
}

/*
 * brief: Start PIDM input task that handles key events in sub-app mode.
 * input: None.
 * output: true on success; otherwise false.
 */
static bool _pidm_start_input_task(void)
{
    BaseType_t task_ok;

    s_pidm_input_task_stop = false;
    task_ok = xTaskCreate(_pidm_input_task,
                          "pidm_input",
                          PIDM_INPUT_TASK_STACK_SIZE,
                          NULL,
                          PIDM_INPUT_TASK_PRIORITY,
                          &s_pidm_input_task_handle);
    if (task_ok != pdPASS)
    {
        ESP_LOGE(TAG, "xTaskCreate pidm_input failed");
        s_pidm_input_task_stop = true;
        s_pidm_input_task_handle = NULL;
        return false;
    }

    return true;
}

/*
 * brief: Stop PIDM input task and force delete on timeout.
 * input: None.
 * output: None.
 */
static void _pidm_stop_input_task(void)
{
    TaskHandle_t handle;
    uint32_t wait_count;

    handle = s_pidm_input_task_handle;
    if (handle == NULL)
    {
        return;
    }

    s_pidm_input_task_stop = true;
    for (wait_count = 0U; wait_count < 20U; wait_count++)
    {
        if (s_pidm_input_task_handle == NULL)
        {
            return;
        }

        delay_ms(5U);
    }

    handle = s_pidm_input_task_handle;
    if (handle != NULL)
    {
        vTaskDelete(handle);
        s_pidm_input_task_handle = NULL;
    }
}

/*
 * brief: Probe task for PIDM app to own feature detection while app is active.
 * input: param - PIDM app context pointer.
 * output: None.
 */
static void _pidm_probe_task(void *param)
{
    pidm_app_ctx_t *ctx;
    pidm_det_feature_cfg_s pidm_cfg;
    esp_err_t pidm_cfg_ret;
    int pidm_period;

    ctx = (pidm_app_ctx_t *)param;
    pidm_period = 0;

    pidm_det_feature_cfg_load_default(&pidm_cfg);
    pidm_cfg_ret = pidm_det_feature_cfg_set(&pidm_cfg);
    if (pidm_cfg_ret != ESP_OK)
    {
        ESP_LOGW(TAG, "pidm_det_feature_cfg_set failed: %d", (int)pidm_cfg_ret);
    }

    while (!s_pidm_probe_task_stop)
    {
        pidm_det_feature_s pidm_feature;
        esp_err_t pidm_ret;

        delay_ms(PIDM_PROBE_SCAN_PERIOD_MS);
        pidm_period += PIDM_PROBE_SCAN_PERIOD_MS;
        if (pidm_period < PIDM_PROBE_PERIOD_MS)
        {
            continue;
        }

        pidm_period = 0;
        lv_memset_00(&pidm_feature, sizeof(pidm_feature));
        pidm_ret = pidm_det_probe_feature(PIDM_PROBE_PULSE_US, &pidm_feature);
        if (pidm_ret != ESP_OK)
        {
            ESP_LOGW(TAG, "pidm_det_probe_feature failed: %d", (int)pidm_ret);
        }
        else
        {
            ESP_LOGI(TAG,
                     "pidm metal=%d hit=%d ref=%d peak=%d dpk=%d slope=%u dsl=%u hold=%uus area=%u",
                     pidm_feature.metal_present ? 1 : 0,
                     pidm_feature.pulse_hit ? 1 : 0,
                     pidm_feature.ref_ready ? 1 : 0,
                     pidm_feature.peak_raw,
                     pidm_feature.peak_delta_raw,
                     (unsigned)pidm_feature.rise_slope_adc_per_ms,
                     (unsigned)pidm_feature.slope_delta_adc_per_ms,
                     (unsigned)pidm_feature.high_hold_us,
                     (unsigned)pidm_feature.area_adc_us);
        }

        portENTER_CRITICAL(&s_pidm_feature_lock);
        if (ctx != NULL)
        {
            ctx->latest_probe_ret = pidm_ret;
            if (pidm_ret == ESP_OK)
            {
                ctx->latest_feature = pidm_feature;
                ctx->latest_feature_valid = 1U;
            }
            else
            {
                ctx->latest_feature_valid = 0U;
            }

            ctx->feature_seq++;
        }
        portEXIT_CRITICAL(&s_pidm_feature_lock);
    }

    s_pidm_probe_task_handle = NULL;
    vTaskDelete(NULL);
}

/*
 * brief: Start PIDM probe task that handles detector sampling in sub-app mode.
 * input: ctx - PIDM app context pointer.
 * output: true on success; otherwise false.
 */
static bool _pidm_start_probe_task(pidm_app_ctx_t *ctx)
{
    BaseType_t task_ok;

    if (ctx == NULL)
    {
        return false;
    }

    s_pidm_probe_task_stop = false;
    task_ok = xTaskCreate(_pidm_probe_task,
                          "pidm_probe",
                          PIDM_PROBE_TASK_STACK_SIZE,
                          ctx,
                          PIDM_PROBE_TASK_PRIORITY,
                          &s_pidm_probe_task_handle);
    if (task_ok != pdPASS)
    {
        ESP_LOGE(TAG, "xTaskCreate pidm_probe failed");
        s_pidm_probe_task_stop = true;
        s_pidm_probe_task_handle = NULL;
        return false;
    }

    return true;
}

/*
 * brief: Stop PIDM probe task and force delete on timeout.
 * input: None.
 * output: None.
 */
static void _pidm_stop_probe_task(void)
{
    TaskHandle_t handle;
    uint32_t wait_count;

    handle = s_pidm_probe_task_handle;
    if (handle == NULL)
    {
        return;
    }

    s_pidm_probe_task_stop = true;
    for (wait_count = 0U; wait_count < 20U; wait_count++)
    {
        if (s_pidm_probe_task_handle == NULL)
        {
            return;
        }

        delay_ms(5U);
    }

    handle = s_pidm_probe_task_handle;
    if (handle != NULL)
    {
        vTaskDelete(handle);
        s_pidm_probe_task_handle = NULL;
    }
}

/*
 * brief: Customize grid line style during chart draw-part event.
 * input: e - LVGL draw-part event.
 * output: None.
 */
static void _pidm_chart_draw_part_cb(lv_event_t *e)
{
    lv_obj_draw_part_dsc_t *dsc;

    dsc = lv_event_get_draw_part_dsc(e);
    if ((dsc == NULL) || (dsc->line_dsc == NULL) || (dsc->part != LV_PART_MAIN))
    {
        return;
    }

    if ((dsc->type == LV_CHART_DRAW_PART_DIV_LINE_HOR) ||
        (dsc->type == LV_CHART_DRAW_PART_DIV_LINE_VER))
    {
        dsc->line_dsc->color = lv_color_white();
        dsc->line_dsc->width = 1;
        dsc->line_dsc->opa = LV_OPA_70;
        dsc->line_dsc->dash_width = 2;
        dsc->line_dsc->dash_gap = 4;
    }
}

/*
 * brief: Shift one waveform ring-array left and append latest sample.
 * input: points - waveform points array; sample - new sample value.
 * output: None.
 */
static void _pidm_push_point(int16_t *points, int32_t sample)
{
    uint32_t i;

    if (points == NULL)
    {
        return;
    }

    for (i = 0U; i < (PIDM_WAVE_POINT_COUNT - 1U); i++)
    {
        points[i] = points[i + 1U];
    }

    if (sample > 32767)
    {
        sample = 32767;
    }
    else if (sample < -32768)
    {
        sample = -32768;
    }

    points[PIDM_WAVE_POINT_COUNT - 1U] = (int16_t)sample;
}

/*
 * brief: Apply adaptive Y-axis range for chart based on current points.
 * input: chart - target chart; points - point array backing the series.
 * output: None.
 */
static void _pidm_apply_chart_range(lv_obj_t *chart, const int16_t *points)
{
    int32_t coord_min;
    int32_t coord_max;
    int32_t min_value;
    int32_t max_value;
    int32_t span;
    int32_t margin;
    int32_t center;
    uint32_t i;

    if ((chart == NULL) || (points == NULL))
    {
        return;
    }

    coord_min = (int32_t)LV_COORD_MIN;
    coord_max = (int32_t)LV_COORD_MAX;

    min_value = points[0];
    max_value = points[0];
    for (i = 1U; i < PIDM_WAVE_POINT_COUNT; i++)
    {
        if (points[i] < min_value)
        {
            min_value = points[i];
        }

        if (points[i] > max_value)
        {
            max_value = points[i];
        }
    }

    span = max_value - min_value;
    if (span < 8)
    {
        center = (max_value + min_value) / 2;
        min_value = center - 4;
        max_value = center + 4;
        span = max_value - min_value;
    }

    margin = span / 5;
    if (margin < 2)
    {
        margin = 2;
    }

    min_value -= margin;
    max_value += margin;

    if (min_value < coord_min)
    {
        min_value = coord_min;
    }

    if (max_value > coord_max)
    {
        max_value = coord_max;
    }

    if (min_value >= max_value)
    {
        if (max_value < coord_max)
        {
            max_value = min_value + 1;
        }
        else
        {
            min_value = max_value - 1;
        }
    }

    lv_chart_set_range(chart, LV_CHART_AXIS_PRIMARY_Y, min_value, max_value);
}

/*
 * brief: Set one metric textedit to signed integer key/value text.
 * input: textedit - target textedit object; key - metric key; value - integer value.
 * output: None.
 */
static void _pidm_set_textedit_value(lv_obj_t *textedit, const char *key, int32_t value)
{
    char text_buf[PIDM_TEXT_BUF_LEN];

    if ((textedit == NULL) || (key == NULL))
    {
        return;
    }

    (void)lv_snprintf(text_buf, sizeof(text_buf), "%s=%ld", key, (long)value);
    lv_textarea_set_text(textedit, text_buf);
}

/*
 * brief: Set one metric textedit to unsigned integer key/value text.
 * input: textedit - target textedit object; key - metric key; value - unsigned value.
 * output: None.
 */
static void _pidm_set_textedit_u32(lv_obj_t *textedit, const char *key, uint32_t value)
{
    char text_buf[PIDM_TEXT_BUF_LEN];

    if ((textedit == NULL) || (key == NULL))
    {
        return;
    }

    (void)lv_snprintf(text_buf, sizeof(text_buf), "%s=%lu", key, (unsigned long)value);
    lv_textarea_set_text(textedit, text_buf);
}

/*
 * brief: Display probe error in first metric textedit.
 * input: textedit - target textedit object; err - esp error code.
 * output: None.
 */
static void _pidm_set_textedit_error(lv_obj_t *textedit, esp_err_t err)
{
    char text_buf[PIDM_TEXT_BUF_LEN];

    if (textedit == NULL)
    {
        return;
    }

    lv_obj_set_style_text_color(textedit, lv_color_hex(APP_THEME_TEXT_PRIMARY_HEX), 0);
    (void)lv_snprintf(text_buf, sizeof(text_buf), "probe_err=%d", (int)err);
    lv_textarea_set_text(textedit, text_buf);
}

/*
 * brief: Refresh 9 metric textedits from one PIDM feature snapshot.
 * input: ctx - PIDM app context; feature - latest feature snapshot.
 * output: None.
 */
static void _pidm_refresh_metrics(pidm_app_ctx_t *ctx, const pidm_det_feature_s *feature)
{
    if ((ctx == NULL) || (feature == NULL))
    {
        return;
    }

    _pidm_set_textedit_value(ctx->text_edits[0], "metal", feature->metal_present ? 1 : 0);
    lv_obj_set_style_text_color(ctx->text_edits[0],
                                feature->metal_present ? lv_color_hex(PIDM_METAL_TEXT_ALERT_HEX)
                                                       : lv_color_hex(APP_THEME_TEXT_PRIMARY_HEX),
                                0);
    _pidm_set_textedit_value(ctx->text_edits[1], "hit", feature->pulse_hit ? 1 : 0);
    _pidm_set_textedit_value(ctx->text_edits[2], "ref", feature->ref_ready ? 1 : 0);
    _pidm_set_textedit_value(ctx->text_edits[3], "peak", feature->peak_raw);
    _pidm_set_textedit_value(ctx->text_edits[4], "dpk", feature->peak_delta_raw);
    _pidm_set_textedit_u32(ctx->text_edits[5], "slope", feature->rise_slope_adc_per_ms);
    _pidm_set_textedit_u32(ctx->text_edits[6], "dsl", feature->slope_delta_adc_per_ms);
    _pidm_set_textedit_u32(ctx->text_edits[7], "hold", feature->high_hold_us);
    _pidm_set_textedit_u32(ctx->text_edits[8], "area", feature->area_adc_us);
}

/*
 * brief: Append latest sample values to chart arrays and refresh charts.
 * input: ctx - PIDM app context; feature - latest feature snapshot.
 * output: None.
 */
static void _pidm_refresh_charts(pidm_app_ctx_t *ctx, const pidm_det_feature_s *feature)
{
    if ((ctx == NULL) || (feature == NULL))
    {
        return;
    }

    _pidm_push_point(ctx->dpk_points, feature->peak_delta_raw);
    _pidm_push_point(ctx->slope_points, (int32_t)feature->rise_slope_adc_per_ms);

    _pidm_apply_chart_range(ctx->chart_dpk, ctx->dpk_points);
    _pidm_apply_chart_range(ctx->chart_slope, ctx->slope_points);
    lv_chart_refresh(ctx->chart_dpk);
    lv_chart_refresh(ctx->chart_slope);
}

/*
 * brief: Periodic timer callback to consume sampled PIDM feature and refresh view.
 * input: timer - LVGL timer carrying PIDM context.
 * output: None.
 */
static void _pidm_update_timer_cb(lv_timer_t *timer)
{
    pidm_app_ctx_t *ctx;
    pidm_det_feature_s feature;
    esp_err_t probe_ret;
    uint8_t feature_valid;
    uint32_t feature_seq;

    if (timer == NULL)
    {
        return;
    }

    ctx = (pidm_app_ctx_t *)timer->user_data;
    if (ctx == NULL)
    {
        return;
    }

    feature_seq = 0U;
    probe_ret = ESP_ERR_INVALID_STATE;
    feature_valid = 0U;
    lv_memset_00(&feature, sizeof(feature));

    portENTER_CRITICAL(&s_pidm_feature_lock);
    probe_ret = ctx->latest_probe_ret;
    feature_valid = ctx->latest_feature_valid;
    feature_seq = ctx->feature_seq;
    if (feature_valid != 0U)
    {
        feature = ctx->latest_feature;
    }
    portEXIT_CRITICAL(&s_pidm_feature_lock);

    if (feature_seq == ctx->rendered_feature_seq)
    {
        if (feature_valid != 0U)
        {
            _pidm_try_play_metal_beep(ctx, &feature);
        }
        return;
    }
    ctx->rendered_feature_seq = feature_seq;

    if ((probe_ret != ESP_OK) || (feature_valid == 0U))
    {
        _pidm_set_textedit_error(ctx->text_edits[0], probe_ret);
        return;
    }

    _pidm_refresh_charts(ctx, &feature);
    _pidm_refresh_metrics(ctx, &feature);
    _pidm_try_play_metal_beep(ctx, &feature);
}

/*
 * brief: Create one wave card with title and line chart.
 * input: parent - chart grid parent; col - target grid column; title - card title;
 *        line_color - chart line color; point_array - external y points;
 *        chart_out/series_out - output chart and series pointers.
 * output: true on success; otherwise false.
 */
static bool _pidm_create_wave_card(lv_obj_t *parent,
                                   lv_coord_t col,
                                   const char *title,
                                   lv_color_t line_color,
                                   int16_t *point_array,
                                   lv_obj_t **chart_out,
                                   lv_chart_series_t **series_out)
{
    lv_obj_t *card;
    lv_obj_t *title_label;
    lv_obj_t *chart;
    lv_chart_series_t *series;

    if ((parent == NULL) || (title == NULL) ||
        (point_array == NULL) || (chart_out == NULL) || (series_out == NULL))
    {
        return false;
    }

    card = lv_obj_create(parent);
    if (card == NULL)
    {
        return false;
    }

    lv_obj_set_grid_cell(card,
                         LV_GRID_ALIGN_STRETCH,
                         col,
                         1,
                         LV_GRID_ALIGN_STRETCH,
                         0,
                         1);
    lv_obj_set_style_bg_color(card, lv_color_hex(APP_THEME_SURFACE_HEX), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_border_color(card, lv_color_hex(APP_THEME_BORDER_HEX), 0);
    lv_obj_set_style_radius(card, 6, 0);
    lv_obj_set_style_pad_all(card, 2, 0);
    lv_obj_set_style_pad_row(card, 2, 0);
    lv_obj_set_scrollbar_mode(card, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);

    title_label = lv_label_create(card);
    if (title_label == NULL)
    {
        return false;
    }
    lv_label_set_text(title_label, title);
    lv_obj_set_style_text_color(title_label, lv_color_hex(APP_THEME_TEXT_SECONDARY_HEX), 0);

    chart = lv_chart_create(card);
    if (chart == NULL)
    {
        return false;
    }

    lv_obj_set_width(chart, lv_pct(100));
    lv_obj_set_flex_grow(chart, 1);
    lv_obj_set_style_bg_color(chart, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(chart, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(chart, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(chart, lv_color_hex(APP_THEME_BORDER_HEX), LV_PART_MAIN);
    lv_obj_set_style_radius(chart, 4, LV_PART_MAIN);
    lv_obj_set_style_line_width(chart, 1, LV_PART_MAIN);
    lv_obj_set_style_line_color(chart, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_line_opa(chart, LV_OPA_70, LV_PART_MAIN);
    lv_obj_set_style_line_dash_width(chart, 2, LV_PART_MAIN);
    lv_obj_set_style_line_dash_gap(chart, 4, LV_PART_MAIN);
    lv_obj_set_style_line_width(chart, 2, LV_PART_ITEMS);
    lv_obj_set_style_line_color(chart, line_color, LV_PART_ITEMS);
    lv_obj_set_style_size(chart, 0, LV_PART_INDICATOR);
    lv_obj_add_event_cb(chart, _pidm_chart_draw_part_cb, LV_EVENT_DRAW_PART_BEGIN, NULL);

    lv_chart_set_type(chart, LV_CHART_TYPE_LINE);
    lv_chart_set_div_line_count(chart, 3, 6);
    lv_chart_set_point_count(chart, PIDM_WAVE_POINT_COUNT);
    lv_chart_set_range(chart, LV_CHART_AXIS_PRIMARY_Y, -10, 10);

    series = lv_chart_add_series(chart, line_color, LV_CHART_AXIS_PRIMARY_Y);
    if (series == NULL)
    {
        return false;
    }

    lv_chart_set_ext_y_array(chart, series, point_array);
    lv_chart_refresh(chart);

    *chart_out = chart;
    *series_out = series;
    return true;
}

/*
 * brief: Create 9 read-only textedit widgets for PIDM metrics.
 * input: ctx - PIDM app context; parent - metrics grid parent.
 * output: true on success; otherwise false.
 */
static bool _pidm_create_metrics_grid(pidm_app_ctx_t *ctx, lv_obj_t *parent)
{
    static const char *s_default_text[PIDM_TEXTEDIT_COUNT] = {
        "metal=0",
        "hit=0",
        "ref=0",
        "peak=0",
        "dpk=0",
        "slope=0",
        "dsl=0",
        "hold=0",
        "area=0",
    };
    uint32_t i;

    if ((ctx == NULL) || (parent == NULL))
    {
        return false;
    }

    for (i = 0U; i < PIDM_TEXTEDIT_COUNT; i++)
    {
        lv_obj_t *textedit;
        lv_coord_t col;
        lv_coord_t row;

        textedit = lv_textarea_create(parent);
        if (textedit == NULL)
        {
            return false;
        }

        col = (lv_coord_t)(i % 3U);
        row = (lv_coord_t)(i / 3U);
        lv_obj_set_grid_cell(textedit,
                             LV_GRID_ALIGN_STRETCH,
                             col,
                             1,
                             LV_GRID_ALIGN_STRETCH,
                             row,
                             1);
        lv_obj_set_style_bg_color(textedit, lv_color_hex(APP_THEME_SURFACE_HEX), 0);
        lv_obj_set_style_bg_opa(textedit, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(textedit, 1, 0);
        lv_obj_set_style_border_color(textedit, lv_color_hex(APP_THEME_BORDER_HEX), 0);
        lv_obj_set_style_radius(textedit, 4, 0);
        lv_obj_set_style_text_color(textedit, lv_color_hex(APP_THEME_TEXT_PRIMARY_HEX), 0);
        lv_obj_set_style_pad_all(textedit, 3, 0);
        lv_obj_set_scrollbar_mode(textedit, LV_SCROLLBAR_MODE_OFF);
        lv_textarea_set_one_line(textedit, false);
        lv_textarea_set_cursor_click_pos(textedit, false);
        lv_textarea_set_max_length(textedit, PIDM_TEXT_BUF_LEN - 1U);
        lv_obj_clear_flag(textedit, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_clear_flag(textedit, LV_OBJ_FLAG_CLICK_FOCUSABLE);
        lv_obj_clear_flag(textedit, LV_OBJ_FLAG_SCROLLABLE);
        lv_textarea_set_text(textedit, s_default_text[i]);

        ctx->text_edits[i] = textedit;
    }

    return true;
}

/*
 * brief: Cleanup callback when PIDM screen object is deleted.
 * input: e - LVGL delete event.
 * output: None.
 */
static void _pidm_delete_cb(lv_event_t *e)
{
    lv_obj_t *target;
    pidm_app_ctx_t *ctx;

    target = lv_event_get_target(e);
    ctx = (pidm_app_ctx_t *)lv_event_get_user_data(e);
    if (ctx == NULL)
    {
        return;
    }

    _pidm_stop_input_task();
    _pidm_stop_probe_task();

    if (ctx->update_timer != NULL)
    {
        lv_timer_del(ctx->update_timer);
        ctx->update_timer = NULL;
    }

    if ((target != NULL) && (s_pidm_ctx == ctx) && (s_pidm_ctx->screen == target))
    {
        s_pidm_ctx = NULL;
    }

    lv_mem_free(ctx);
}

/*
 * brief: Build PIDM monitor screen and start app-local runtime tasks.
 * input: lcd_w/lcd_h - active display resolution.
 * output: PIDM screen object on success; otherwise NULL.
 */
lv_obj_t *pidm_create_screen(lv_coord_t lcd_w, lv_coord_t lcd_h)
{
    static lv_coord_t chart_col_dsc[] = {
        LV_GRID_FR(1),
        LV_GRID_FR(1),
        LV_GRID_TEMPLATE_LAST,
    };
    static lv_coord_t chart_row_dsc[] = {
        LV_GRID_FR(1),
        LV_GRID_TEMPLATE_LAST,
    };
    static lv_coord_t metrics_col_dsc[] = {
        LV_GRID_FR(1),
        LV_GRID_FR(1),
        LV_GRID_FR(1),
        LV_GRID_TEMPLATE_LAST,
    };
    static lv_coord_t metrics_row_dsc[] = {
        LV_GRID_FR(1),
        LV_GRID_FR(1),
        LV_GRID_FR(1),
        LV_GRID_TEMPLATE_LAST,
    };
    pidm_app_ctx_t *ctx;
    lv_obj_t *scr;
    lv_obj_t *body;
    lv_obj_t *chart_grid;
    lv_obj_t *metrics_grid;
    lv_coord_t content_top;
    lv_coord_t content_bottom;
    lv_coord_t content_h;

    if ((lcd_w <= (2 * PIDM_MARGIN_X)) || (lcd_h <= (2 * APP_STATUS_BAR_HEIGHT + 20)))
    {
        return NULL;
    }

    ctx = (pidm_app_ctx_t *)lv_mem_alloc(sizeof(pidm_app_ctx_t));
    if (ctx == NULL)
    {
        return NULL;
    }
    lv_memset_00(ctx, sizeof(pidm_app_ctx_t));

    scr = lv_obj_create(NULL);
    if (scr == NULL)
    {
        lv_mem_free(ctx);
        return NULL;
    }

    lv_obj_set_style_bg_color(scr, lv_color_hex(APP_THEME_BG_HEX), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(scr, 0, 0);
    lv_obj_set_scrollbar_mode(scr, LV_SCROLLBAR_MODE_OFF);
    ctx->screen = scr;

    content_top = system_service_content_top();
    content_bottom = system_service_content_bottom();
    content_h = content_bottom - content_top;
    if (content_h < 40)
    {
        content_h = 40;
    }

    body = lv_obj_create(scr);
    if (body == NULL)
    {
        lv_obj_del(scr);
        lv_mem_free(ctx);
        return NULL;
    }
    lv_obj_set_size(body, lcd_w - (2 * PIDM_MARGIN_X), content_h);
    lv_obj_set_pos(body, PIDM_MARGIN_X, content_top);
    lv_obj_set_style_bg_opa(body, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(body, 0, 0);
    lv_obj_set_style_radius(body, 0, 0);
    lv_obj_set_style_pad_all(body, 0, 0);
    lv_obj_set_style_pad_row(body, PIDM_LAYOUT_GAP, 0);
    lv_obj_set_scrollbar_mode(body, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);

    chart_grid = lv_obj_create(body);
    if (chart_grid == NULL)
    {
        lv_obj_del(scr);
        lv_mem_free(ctx);
        return NULL;
    }
    lv_obj_set_width(chart_grid, lv_pct(100));
    lv_obj_set_flex_grow(chart_grid, 11);
    lv_obj_set_style_bg_opa(chart_grid, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(chart_grid, 0, 0);
    lv_obj_set_style_radius(chart_grid, 0, 0);
    lv_obj_set_style_pad_all(chart_grid, 0, 0);
    lv_obj_set_style_pad_column(chart_grid, PIDM_LAYOUT_GAP, 0);
    lv_obj_set_scrollbar_mode(chart_grid, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_grid_dsc_array(chart_grid, chart_col_dsc, chart_row_dsc);

    if (!_pidm_create_wave_card(chart_grid,
                                0,
                                "DPK",
                                lv_color_hex(PIDM_CHART_DPK_COLOR_HEX),
                                ctx->dpk_points,
                                &ctx->chart_dpk,
                                &ctx->series_dpk))
    {
        lv_obj_del(scr);
        lv_mem_free(ctx);
        return NULL;
    }

    if (!_pidm_create_wave_card(chart_grid,
                                1,
                                "SLOPE",
                                lv_color_hex(PIDM_CHART_SLOPE_COLOR_HEX),
                                ctx->slope_points,
                                &ctx->chart_slope,
                                &ctx->series_slope))
    {
        lv_obj_del(scr);
        lv_mem_free(ctx);
        return NULL;
    }

    metrics_grid = lv_obj_create(body);
    if (metrics_grid == NULL)
    {
        lv_obj_del(scr);
        lv_mem_free(ctx);
        return NULL;
    }
    lv_obj_set_width(metrics_grid, lv_pct(100));
    lv_obj_set_flex_grow(metrics_grid, 9);
    lv_obj_set_style_bg_opa(metrics_grid, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(metrics_grid, 0, 0);
    lv_obj_set_style_radius(metrics_grid, 0, 0);
    lv_obj_set_style_pad_all(metrics_grid, 0, 0);
    lv_obj_set_style_pad_row(metrics_grid, 2, 0);
    lv_obj_set_style_pad_column(metrics_grid, 2, 0);
    lv_obj_set_scrollbar_mode(metrics_grid, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_grid_dsc_array(metrics_grid, metrics_col_dsc, metrics_row_dsc);

    if (!_pidm_create_metrics_grid(ctx, metrics_grid))
    {
        lv_obj_del(scr);
        lv_mem_free(ctx);
        return NULL;
    }

    ctx->latest_probe_ret = ESP_ERR_INVALID_STATE;
    ctx->latest_feature_valid = 0U;
    ctx->feature_seq = 0U;
    ctx->rendered_feature_seq = 0U;
    ctx->last_beep_ts_ms = 0;
    ctx->beep_level = 0U;

    if ((usr_fs_format_asset_path("voice/common",
                                  NULL,
                                  "Di.ogg",
                                  ctx->di_ogg_path,
                                  sizeof(ctx->di_ogg_path)) != ESP_OK) ||
        !usr_fs_path_exists(ctx->di_ogg_path))
    {
        ctx->di_ogg_path[0] = '\0';
        ESP_LOGW(TAG, "Di.ogg asset not found under voice/common");
    }

    ctx->update_timer = lv_timer_create(_pidm_update_timer_cb, PIDM_UPDATE_PERIOD_MS, ctx);
    if (ctx->update_timer == NULL)
    {
        lv_obj_del(scr);
        lv_mem_free(ctx);
        return NULL;
    }

    if (!_pidm_start_input_task())
    {
        lv_timer_del(ctx->update_timer);
        ctx->update_timer = NULL;
        lv_obj_del(scr);
        lv_mem_free(ctx);
        return NULL;
    }

    if (!_pidm_start_probe_task(ctx))
    {
        _pidm_stop_input_task();
        lv_timer_del(ctx->update_timer);
        ctx->update_timer = NULL;
        lv_obj_del(scr);
        lv_mem_free(ctx);
        return NULL;
    }

    lv_obj_add_event_cb(scr, _pidm_delete_cb, LV_EVENT_DELETE, ctx);
    s_pidm_ctx = ctx;
    return scr;
}

/*
 * brief: Request desktop return directly.
 * input: None.
 * output: None.
 */
void pidm_destroy_and_return(void)
{
    desktop_return_to_home();
}
