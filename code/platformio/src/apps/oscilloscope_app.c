#include "oscilloscope_app.h"

#define SCOPE_MARGIN_X 12
#define SCOPE_TOP_GAP 8
#define SCOPE_BOTTOM_GAP 8

#define SCOPE_UPDATE_PERIOD_MS 40U
#define SCOPE_AMPLITUDE 80.0f
#define SCOPE_WAVE_FREQ_RAD 0.18f
#define SCOPE_PHASE_STEP_RAD 0.30f
#define SCOPE_TWO_PI 6.2831853f

static void scope_app_fill_points(scope_app_ctx_t *ctx)
{
    uint32_t i;

    for (i = 0; i < SCOPE_POINT_COUNT; i++)
    {
        float rad = ((float)i * SCOPE_WAVE_FREQ_RAD) + ctx->phase;
        float value = sinf(rad) * SCOPE_AMPLITUDE;
        ctx->points[i] = (int16_t)value;
    }
}

static void scope_chart_draw_part_cb(lv_event_t *e)
{
    lv_obj_draw_part_dsc_t *dsc = lv_event_get_draw_part_dsc(e);

    if ((dsc == NULL) || (dsc->line_dsc == NULL) || (dsc->part != LV_PART_MAIN))
    {
        return;
    }

    if ((dsc->type == LV_CHART_DRAW_PART_DIV_LINE_HOR) || (dsc->type == LV_CHART_DRAW_PART_DIV_LINE_VER))
    {
        dsc->line_dsc->color = lv_color_hex(0xFACC15);
        dsc->line_dsc->width = 1;
        dsc->line_dsc->opa = LV_OPA_80;
        dsc->line_dsc->dash_width = 2;
        dsc->line_dsc->dash_gap = 4;
    }
}

static void scope_app_timer_cb(lv_timer_t *timer)
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

    scope_app_fill_points(ctx);
    lv_chart_refresh(ctx->chart);
}

static void scope_app_delete_cb(lv_event_t *e)
{
    scope_app_ctx_t *ctx = (scope_app_ctx_t *)lv_event_get_user_data(e);

    if (ctx == NULL)
    {
        return;
    }

    if (ctx->timer != NULL)
    {
        lv_timer_del(ctx->timer);
        ctx->timer = NULL;
    }

    lv_mem_free(ctx);
}

lv_obj_t *scope_app_create_screen(lv_coord_t lcd_w, lv_coord_t lcd_h)
{
    scope_app_ctx_t *ctx;
    lv_obj_t *scr;
    lv_obj_t *title;
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

    lv_obj_set_style_bg_color(scr, lv_color_hex(0x0F172A), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    title = lv_label_create(scr);
    lv_label_set_text(title, "Scope");
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 10, app_status_bar_content_top() + 6);

    content_y = app_status_bar_content_top() + SCOPE_TOP_GAP;
    content_bottom = app_status_bar_content_bottom() - SCOPE_BOTTOM_GAP;
    content_h = content_bottom - content_y;
    if (content_h < 90)
    {
        content_h = 90;
    }

    wave_h = content_h / 3;
    if (wave_h < 90)
    {
        wave_h = 90;
    }
    if (wave_h > content_h)
    {
        wave_h = content_h;
    }

    wave_w = lcd_w - (2 * SCOPE_MARGIN_X);
    wave_y = content_y + ((content_h - wave_h) / 2);

    chart = lv_chart_create(scr);
    lv_obj_set_size(chart, wave_w, wave_h);
    lv_obj_set_pos(chart, SCOPE_MARGIN_X, wave_y);
    lv_obj_set_style_bg_color(chart, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(chart, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(chart, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(chart, lv_color_hex(0x334155), LV_PART_MAIN);
    lv_obj_set_style_radius(chart, 6, LV_PART_MAIN);
    lv_obj_set_style_line_width(chart, 1, LV_PART_MAIN);
    lv_obj_set_style_line_color(chart, lv_color_hex(0xFACC15), LV_PART_MAIN);
    lv_obj_set_style_line_opa(chart, LV_OPA_80, LV_PART_MAIN);
    lv_obj_set_style_line_dash_width(chart, 2, LV_PART_MAIN);
    lv_obj_set_style_line_dash_gap(chart, 4, LV_PART_MAIN);
    lv_obj_set_style_line_width(chart, 2, LV_PART_ITEMS);
    lv_obj_set_style_line_color(chart, lv_color_white(), LV_PART_ITEMS);
    lv_obj_set_style_size(chart, 0, LV_PART_INDICATOR);
    lv_obj_add_event_cb(chart, scope_chart_draw_part_cb, LV_EVENT_DRAW_PART_BEGIN, NULL);

    lv_chart_set_type(chart, LV_CHART_TYPE_LINE);
    lv_chart_set_div_line_count(chart, 4, 10);
    lv_chart_set_point_count(chart, SCOPE_POINT_COUNT);
    lv_chart_set_range(chart, LV_CHART_AXIS_PRIMARY_Y, -100, 100);

    ctx->series = lv_chart_add_series(chart, lv_color_white(), LV_CHART_AXIS_PRIMARY_Y);
    if (ctx->series == NULL)
    {
        lv_obj_del(scr);
        lv_mem_free(ctx);
        return NULL;
    }

    ctx->chart = chart;
    scope_app_fill_points(ctx);
    lv_chart_set_ext_y_array(chart, ctx->series, ctx->points);
    lv_chart_refresh(chart);

    ctx->timer = lv_timer_create(scope_app_timer_cb, SCOPE_UPDATE_PERIOD_MS, ctx);

    lv_obj_add_event_cb(scr, scope_app_delete_cb, LV_EVENT_DELETE, ctx);
    return scr;
}
