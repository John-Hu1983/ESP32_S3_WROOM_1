#include "oscilloscope_ui.h"

#define TAG "SCOPE"

#define SCOPE_MARGIN_X 2

#define SCOPE_UPDATE_PERIOD_MS 40U
#define SCOPE_INPUT_SCAN_PERIOD_MS 10U
#define SCOPE_INPUT_TASK_STACK_SIZE 4096U
#define SCOPE_INPUT_TASK_PRIORITY 4U
#define SCOPE_AMPLITUDE 80.0f
#define SCOPE_WAVE_FREQ_RAD 0.18f
#define SCOPE_PHASE_STEP_RAD 0.30f
#define SCOPE_TWO_PI 6.2831853f

static scope_app_ctx_t *s_scope_ctx = NULL;
static TaskHandle_t s_scope_input_task_handle = NULL;
static volatile bool s_scope_input_task_stop = false;

/*
 * brief: Input task for scope app to own key scanning while app is active.
 * input: param - unused task parameter.
 * output: None.
 */
static void _scope_input_task(void *param)
{
    btn_scan_s btn;
    bool home_requested;

    (void)param;
    lv_memset_00(&btn, sizeof(btn));
    home_requested = false;

    while (!s_scope_input_task_stop)
    {
        btn_status_e btn_val;

        btn_val = keyboard_scan_event(&btn, SCOPE_INPUT_SCAN_PERIOD_MS);
        if ((btn_val == Btn_Both_Click) && !home_requested)
        {
            home_requested = true;
            desktop_return_to_home();
        }

        delay_ms(SCOPE_INPUT_SCAN_PERIOD_MS);
    }

    s_scope_input_task_handle = NULL;
    vTaskDelete(NULL);
}

/*
 * brief: Start scope input task that handles key events in sub-app mode.
 * input: None.
 * output: true on success; otherwise false.
 */
static bool _scope_start_input_task(void)
{
    BaseType_t task_ok;

    s_scope_input_task_stop = false;
    task_ok = xTaskCreate(_scope_input_task,
                          "scope_input",
                          SCOPE_INPUT_TASK_STACK_SIZE,
                          NULL,
                          SCOPE_INPUT_TASK_PRIORITY,
                          &s_scope_input_task_handle);
    if (task_ok != pdPASS)
    {
        ESP_LOGE(TAG, "xTaskCreate scope_input failed");
        s_scope_input_task_stop = true;
        s_scope_input_task_handle = NULL;
        return false;
    }

    return true;
}

/*
 * brief: Stop scope input task and force delete on timeout.
 * input: None.
 * output: None.
 */
static void _scope_stop_input_task(void)
{
    TaskHandle_t handle;
    uint32_t wait_count;

    handle = s_scope_input_task_handle;
    if (handle == NULL)
    {
        return;
    }

    s_scope_input_task_stop = true;
    for (wait_count = 0U; wait_count < 20U; wait_count++)
    {
        if (s_scope_input_task_handle == NULL)
        {
            return;
        }

        delay_ms(5U);
    }

    handle = s_scope_input_task_handle;
    if (handle != NULL)
    {
        vTaskDelete(handle);
        s_scope_input_task_handle = NULL;
    }
}

/*
 * brief: Fill scope waveform points based on current phase.
 * input: ctx - scope context.
 * output: None.
 */
static void _scope_fill_points(scope_app_ctx_t *ctx)
{
    uint32_t i;

    for (i = 0; i < SCOPE_POINT_COUNT; i++)
    {
        float rad = ((float)i * SCOPE_WAVE_FREQ_RAD) + ctx->phase;
        float value = sinf(rad) * SCOPE_AMPLITUDE;
        ctx->points[i] = (int16_t)value;
    }
}

/*
 * brief: Customize grid line style during chart draw-part event.
 * input: e - LVGL draw-part event.
 * output: None.
 */
static void _scope_chart_draw_part_cb(lv_event_t *e)
{
    lv_obj_draw_part_dsc_t *dsc = lv_event_get_draw_part_dsc(e);

    if ((dsc == NULL) || (dsc->line_dsc == NULL) || (dsc->part != LV_PART_MAIN))
    {
        return;
    }

    if ((dsc->type == LV_CHART_DRAW_PART_DIV_LINE_HOR) || (dsc->type == LV_CHART_DRAW_PART_DIV_LINE_VER))
    {
        dsc->line_dsc->color = lv_color_white();
        dsc->line_dsc->width = 1;
        dsc->line_dsc->opa = LV_OPA_80;
        dsc->line_dsc->dash_width = 2;
        dsc->line_dsc->dash_gap = 4;
    }
}

/*
 * brief: Periodic LVGL timer callback to update scope waveform.
 * input: timer - LVGL timer carrying scope context.
 * output: None.
 */
static void _scope_timer_cb(lv_timer_t *timer)
{
    scope_app_ctx_t *ctx = (scope_app_ctx_t *)timer->user_data;

    if ((ctx == NULL) || (ctx->chart == NULL) || (ctx->series == NULL))
    {
        return;
    }

    ctx->phase += SCOPE_PHASE_STEP_RAD;
    if (ctx->phase > SCOPE_TWO_PI)
    {
        ctx->phase -= SCOPE_TWO_PI;
    }

    _scope_fill_points(ctx);
    lv_chart_refresh(ctx->chart);
}

/*
 * brief: Cleanup callback when scope screen object is deleted.
 * input: e - LVGL delete event.
 * output: None.
 */
static void _scope_delete_cb(lv_event_t *e)
{
    lv_obj_t *target = lv_event_get_target(e);
    scope_app_ctx_t *ctx = (scope_app_ctx_t *)lv_event_get_user_data(e);

    if (ctx == NULL)
    {
        return;
    }

    _scope_stop_input_task();

    if (ctx->timer != NULL)
    {
        lv_timer_del(ctx->timer);
        ctx->timer = NULL;
    }

    if ((target != NULL) && (s_scope_ctx == ctx) && (s_scope_ctx->screen == target))
    {
        s_scope_ctx = NULL;
    }

    lv_mem_free(ctx);
}

/*
 * brief: Build scope screen and start app-local input task.
 * input: lcd_w/lcd_h - active display resolution.
 * output: Scope screen object on success; otherwise NULL.
 */
lv_obj_t *scope_create_screen(lv_coord_t lcd_w, lv_coord_t lcd_h)
{
    scope_app_ctx_t *ctx;
    lv_obj_t *scr;
    lv_obj_t *chart;
    lv_coord_t content_y;
    lv_coord_t content_h;
    lv_coord_t content_bottom;
    lv_coord_t wave_w;
    lv_coord_t wave_h;
    lv_coord_t wave_y;

    if ((lcd_w <= (2 * SCOPE_MARGIN_X)) || (lcd_h <= (2 * APP_STATUS_BAR_HEIGHT + 20)))
    {
        return NULL;
    }

    ctx = (scope_app_ctx_t *)lv_mem_alloc(sizeof(scope_app_ctx_t));
    if (ctx == NULL)
    {
        return NULL;
    }
    lv_memset_00(ctx, sizeof(scope_app_ctx_t));

    scr = lv_obj_create(NULL);
    if (scr == NULL)
    {
        lv_mem_free(ctx);
        return NULL;
    }

    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    ctx->screen = scr;

    content_y = system_service_content_top();
    content_bottom = system_service_content_bottom();
    content_h = content_bottom - content_y;
    if (content_h < 40)
    {
        content_h = 40;
    }

    wave_w = lcd_w - (2 * SCOPE_MARGIN_X);
    wave_h = content_h;
    wave_y = content_y;

    chart = lv_chart_create(scr);
    lv_obj_set_size(chart, wave_w, wave_h);
    lv_obj_set_pos(chart, SCOPE_MARGIN_X, wave_y);
    lv_obj_set_style_bg_color(chart, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(chart, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(chart, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(chart, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_radius(chart, 6, LV_PART_MAIN);
    lv_obj_set_style_line_width(chart, 1, LV_PART_MAIN);
    lv_obj_set_style_line_color(chart, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_line_opa(chart, LV_OPA_80, LV_PART_MAIN);
    lv_obj_set_style_line_dash_width(chart, 2, LV_PART_MAIN);
    lv_obj_set_style_line_dash_gap(chart, 4, LV_PART_MAIN);
    lv_obj_set_style_line_width(chart, 2, LV_PART_ITEMS);
    lv_obj_set_style_line_color(chart, lv_color_hex(0xFF2D20), LV_PART_ITEMS);
    lv_obj_set_style_size(chart, 0, LV_PART_INDICATOR);
    lv_obj_add_event_cb(chart, _scope_chart_draw_part_cb, LV_EVENT_DRAW_PART_BEGIN, NULL);

    lv_chart_set_type(chart, LV_CHART_TYPE_LINE);
    lv_chart_set_div_line_count(chart, 4, 10);
    lv_chart_set_point_count(chart, SCOPE_POINT_COUNT);
    lv_chart_set_range(chart, LV_CHART_AXIS_PRIMARY_Y, -100, 100);

    ctx->series = lv_chart_add_series(chart, lv_color_hex(0xFF2D20), LV_CHART_AXIS_PRIMARY_Y);
    if (ctx->series == NULL)
    {
        lv_obj_del(scr);
        lv_mem_free(ctx);
        return NULL;
    }

    ctx->chart = chart;
    _scope_fill_points(ctx);
    lv_chart_set_ext_y_array(chart, ctx->series, ctx->points);
    lv_chart_refresh(chart);

    ctx->timer = lv_timer_create(_scope_timer_cb, SCOPE_UPDATE_PERIOD_MS, ctx);
    if (ctx->timer == NULL)
    {
        lv_obj_del(scr);
        lv_mem_free(ctx);
        return NULL;
    }

    if (!_scope_start_input_task())
    {
        lv_timer_del(ctx->timer);
        ctx->timer = NULL;
        lv_obj_del(scr);
        lv_mem_free(ctx);
        return NULL;
    }

    lv_obj_add_event_cb(scr, _scope_delete_cb, LV_EVENT_DELETE, ctx);
    s_scope_ctx = ctx;
    return scr;
}
