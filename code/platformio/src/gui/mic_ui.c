#include "mic_ui.h"

#include <limits.h>
#include <stdlib.h>

#define TAG "MIC_UI"

#if LV_FONT_SIMSUN_16_CJK
#define MIC_FONT_TEXT (&lv_font_simsun_16_cjk)
#elif LV_FONT_MONTSERRAT_16
#define MIC_FONT_TEXT (&lv_font_montserrat_16)
#else
#define MIC_FONT_TEXT LV_FONT_DEFAULT
#endif

static mic_app_ctx_t *s_mic_ctx = NULL;

/*
 * brief: Copy source text into fixed buffer with guaranteed NUL termination.
 * input: dst - destination char buffer; dst_size - destination capacity; src - source text.
 * output: None.
 */
static void _mic_copy_text(char *dst, size_t dst_size, const char *src)
{
    if ((dst == NULL) || (dst_size == 0U))
    {
        return;
    }

    if (src == NULL)
    {
        dst[0] = '\0';
        return;
    }

    (void)snprintf(dst, dst_size, "%s", src);
}

/*
 * brief: Stop one task cooperatively first, then force delete when it does not exit.
 * input: task_handle - target task handle pointer; stop_flag - task stop flag pointer.
 * output: None.
 */
static void _mic_stop_task(TaskHandle_t *task_handle, volatile bool *stop_flag)
{
    TaskHandle_t handle;
    uint32_t wait_count;

    if ((task_handle == NULL) || (stop_flag == NULL))
    {
        return;
    }

    handle = *task_handle;
    if (handle == NULL)
    {
        return;
    }

    *stop_flag = true;
    for (wait_count = 0U; wait_count < MIC_TASK_STOP_WAIT_RETRY; wait_count++)
    {
        if (*task_handle == NULL)
        {
            return;
        }

        delay_ms(MIC_TASK_STOP_WAIT_DELAY_MS);
    }

    handle = *task_handle;
    if (handle != NULL)
    {
        vTaskDelete(handle);
        *task_handle = NULL;
    }
}

/*
 * brief: Convert PCM frame to a coarse level percentage by peak absolute value.
 * input: samples - signed PCM samples; sample_count - sample count in frame.
 * output: Level percentage in [0, 100].
 */
static uint16_t _mic_calc_level_percent(const int16_t *samples, size_t sample_count)
{
    uint32_t peak_abs = 0U;
    size_t i;

    if ((samples == NULL) || (sample_count == 0U))
    {
        return 0U;
    }

    for (i = 0U; i < sample_count; i++)
    {
        int32_t sample_abs = (int32_t)samples[i];
        if (sample_abs < 0)
        {
            sample_abs = -sample_abs;
        }

        if ((uint32_t)sample_abs > peak_abs)
        {
            peak_abs = (uint32_t)sample_abs;
        }
    }

    if (peak_abs > (uint32_t)INT16_MAX)
    {
        peak_abs = (uint32_t)INT16_MAX;
    }

    return (uint16_t)((peak_abs * 100U) / (uint32_t)INT16_MAX);
}

/*
 * brief: Reset captured speech segment state.
 * input: ctx - Mic app context.
 * output: None.
 */
static void _mic_reset_segment(mic_app_ctx_t *ctx)
{
    if (ctx == NULL)
    {
        return;
    }

    ctx->segment_samples = 0U;
}

/*
 * brief: Append one audio chunk into current speech segment buffer.
 * input: ctx - Mic app context; samples - source PCM chunk; sample_count - source sample count.
 * output: None.
 */
static void _mic_append_segment(mic_app_ctx_t *ctx, const int16_t *samples, size_t sample_count)
{
    size_t append_count;

    if ((ctx == NULL) || (samples == NULL) || (sample_count == 0U) ||
        (ctx->segment_buf == NULL) || (ctx->segment_capacity == 0U))
    {
        return;
    }

    if (ctx->segment_samples >= ctx->segment_capacity)
    {
        return;
    }

    append_count = ctx->segment_capacity - ctx->segment_samples;
    if (append_count > sample_count)
    {
        append_count = sample_count;
    }

    memcpy(ctx->segment_buf + ctx->segment_samples,
           samples,
           append_count * sizeof(int16_t));
    ctx->segment_samples += append_count;
}

/*
 * brief: Run ASR on captured segment and update displayed recognized text.
 * input: ctx - Mic app context; speech_seq - current speech segment index.
 * output: None.
 */
static void _mic_run_asr_for_segment(mic_app_ctx_t *ctx, uint16_t speech_seq)
{
    asr_output_s output;
    esp_err_t ret;

    if ((ctx == NULL) || (ctx->segment_buf == NULL) || (ctx->segment_samples == 0U))
    {
        return;
    }

    memset(&output, 0, sizeof(output));
    output.mode = ASR_MODE_OFFLINE;
    ret = asr_service_recognize_pcm16(ctx->segment_buf, ctx->segment_samples, &output);

    portENTER_CRITICAL(&ctx->state_lock);
    ctx->asr_mode = output.mode;
    if ((ret == ESP_OK) && (output.text[0] != '\0'))
    {
        _mic_copy_text(ctx->recognized_text, sizeof(ctx->recognized_text), output.text);
    }
    else if (ret == ESP_ERR_NOT_SUPPORTED)
    {
        _mic_copy_text(ctx->recognized_text,
                       sizeof(ctx->recognized_text),
                       "ASR unavailable. Configure cloud endpoint or enable offline model.");
    }
    else if (ret == ESP_ERR_NOT_FOUND)
    {
        char text[MIC_RECOGNIZED_TEXT_MAX_LEN];
        (void)snprintf(text,
                       sizeof(text),
                       "No command recognized (segment %u, mode=%s)",
                       (unsigned)speech_seq,
                       asr_service_mode_name(output.mode));
        _mic_copy_text(ctx->recognized_text, sizeof(ctx->recognized_text), text);
    }
    else
    {
        char text[MIC_RECOGNIZED_TEXT_MAX_LEN];
        (void)snprintf(text,
                       sizeof(text),
                       "ASR failed: %d (mode=%s)",
                       (int)ret,
                       asr_service_mode_name(output.mode));
        _mic_copy_text(ctx->recognized_text, sizeof(ctx->recognized_text), text);
    }
    portEXIT_CRITICAL(&ctx->state_lock);

    _mic_reset_segment(ctx);
}

/*
 * brief: Capture task that reads microphone samples and updates level/VAD/ASR state.
 * input: param - Mic app context pointer.
 * output: None.
 */
static void _mic_sample_task(void *param)
{
    mic_app_ctx_t *ctx = (mic_app_ctx_t *)param;
    int16_t sample_buf[MIC_READ_FRAME_SAMPLES];
    uint16_t smooth_level = 0U;
    uint16_t speech_frames = 0U;
    uint16_t silence_frames = 0U;
    uint16_t speech_seq = 0U;
    bool speech_active = false;

    while ((ctx != NULL) && !ctx->sample_task_stop)
    {
        size_t samples_read;
        esp_err_t ret;

        samples_read = 0U;
        ret = mic_drv_read_pcm16(sample_buf,
                                 MIC_READ_FRAME_SAMPLES,
                                 &samples_read,
                                 MIC_READ_TIMEOUT_MS);
        if ((ret == ESP_OK) && (samples_read > 0U))
        {
            bool speech_start_event = false;
            bool speech_end_event = false;
            uint16_t level_now;

            level_now = _mic_calc_level_percent(sample_buf, samples_read);
            smooth_level = (uint16_t)(((uint32_t)smooth_level * 3U + (uint32_t)level_now) / 4U);

            if (speech_active)
            {
                if (smooth_level >= MIC_VAD_END_THRESHOLD_PERCENT)
                {
                    silence_frames = 0U;
                }
                else
                {
                    silence_frames++;
                    if (silence_frames >= MIC_VAD_END_FRAMES)
                    {
                        speech_active = false;
                        speech_frames = 0U;
                        silence_frames = 0U;
                        speech_seq++;
                        speech_end_event = true;
                    }
                }
            }
            else
            {
                if (smooth_level >= MIC_VAD_START_THRESHOLD_PERCENT)
                {
                    speech_frames++;
                    if (speech_frames >= MIC_VAD_START_FRAMES)
                    {
                        speech_active = true;
                        silence_frames = 0U;
                        speech_start_event = true;
                    }
                }
                else
                {
                    speech_frames = 0U;
                }
            }

            if (speech_start_event)
            {
                _mic_reset_segment(ctx);
            }

            if (speech_active)
            {
                _mic_append_segment(ctx, sample_buf, samples_read);
            }

            portENTER_CRITICAL(&ctx->state_lock);
            ctx->level_percent = smooth_level;
            ctx->speech_active = speech_active ? 1U : 0U;
            ctx->speech_seq = speech_seq;
            ctx->mic_state_ret = ESP_OK;
            if (speech_start_event)
            {
                _mic_copy_text(ctx->recognized_text,
                               sizeof(ctx->recognized_text),
                               "Listening...");
            }
            portEXIT_CRITICAL(&ctx->state_lock);

            if (speech_end_event)
            {
                _mic_run_asr_for_segment(ctx, speech_seq);
            }

            continue;
        }

        if (ret == ESP_ERR_TIMEOUT)
        {
            continue;
        }

        portENTER_CRITICAL(&ctx->state_lock);
        ctx->mic_state_ret = ret;
        ctx->speech_active = 0U;
        _mic_copy_text(ctx->recognized_text,
                       sizeof(ctx->recognized_text),
                       "Mic read failed. Check PDM wiring.");
        portEXIT_CRITICAL(&ctx->state_lock);
        break;
    }

    if (ctx != NULL)
    {
        ctx->sample_task_handle = NULL;
    }
    vTaskDelete(NULL);
}

/*
 * brief: Refresh Mic UI labels and level bar using latest capture snapshot.
 * input: ctx - Mic app context.
 * output: None.
 */
static void _mic_refresh_ui(mic_app_ctx_t *ctx)
{
    char status_text[64];
    char level_text[40];
    char speech_text[64];
    char recognized_text[MIC_RECOGNIZED_TEXT_MAX_LEN];
    esp_err_t mic_state_ret;
    uint16_t level_percent;
    uint16_t speech_seq;
    uint8_t speech_active;
    asr_mode_e asr_mode;

    if (ctx == NULL)
    {
        return;
    }

    portENTER_CRITICAL(&ctx->state_lock);
    mic_state_ret = ctx->mic_state_ret;
    level_percent = ctx->level_percent;
    speech_seq = ctx->speech_seq;
    speech_active = ctx->speech_active;
    asr_mode = ctx->asr_mode;
    _mic_copy_text(recognized_text, sizeof(recognized_text), ctx->recognized_text);
    portEXIT_CRITICAL(&ctx->state_lock);

    if (mic_state_ret == ESP_OK)
    {
        (void)snprintf(status_text,
                       sizeof(status_text),
                       "Mic sampling active (%u Hz)",
                       (unsigned)mic_drv_get_sample_rate_hz());
    }
    else
    {
        (void)snprintf(status_text, sizeof(status_text), "Mic error: %d", (int)mic_state_ret);
    }

    (void)snprintf(level_text, sizeof(level_text), "Signal level: %u%%", (unsigned)level_percent);
    if (speech_active != 0U)
    {
        (void)snprintf(speech_text,
                       sizeof(speech_text),
                       "Speech: active, ASR:%s",
                       asr_service_mode_name(asr_mode));
    }
    else
    {
        (void)snprintf(speech_text,
                       sizeof(speech_text),
                       "Speech: idle (segments %u), ASR:%s",
                       (unsigned)speech_seq,
                       asr_service_mode_name(asr_mode));
    }

    lv_label_set_text(ctx->status_label, status_text);
    lv_label_set_text(ctx->level_label, level_text);
    lv_label_set_text(ctx->speech_label, speech_text);
    lv_label_set_text(ctx->recognized_label, recognized_text);
    lv_bar_set_value(ctx->level_bar, level_percent, LV_ANIM_OFF);
}

/*
 * brief: Periodic LVGL timer callback for Mic UI refresh.
 * input: timer - LVGL timer object with Mic context as user_data.
 * output: None.
 */
static void _mic_ui_timer_cb(lv_timer_t *timer)
{
    mic_app_ctx_t *ctx;

    if (timer == NULL)
    {
        return;
    }

    ctx = (mic_app_ctx_t *)timer->user_data;
    _mic_refresh_ui(ctx);
}

/*
 * brief: Build Mic app widgets on one full-screen LVGL root object.
 * input: ctx - Mic app context; lcd_w/lcd_h - display resolution.
 * output: Root screen object, or NULL when allocation fails.
 */
static lv_obj_t *_mic_build_screen(mic_app_ctx_t *ctx, lv_coord_t lcd_w, lv_coord_t lcd_h)
{
    lv_obj_t *scr;
    lv_obj_t *title_label;
    lv_coord_t card_w;
    lv_coord_t card_h;
    lv_coord_t card_y;

    (void)lcd_h;

    if (ctx == NULL)
    {
        return NULL;
    }

    scr = lv_obj_create(NULL);
    if (scr == NULL)
    {
        return NULL;
    }

    lv_obj_set_style_bg_color(scr, lv_color_hex(APP_THEME_BG_HEX), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(scr, 0, 0);
    lv_obj_set_style_pad_all(scr, 0, 0);

    card_w = lcd_w - (2 * MIC_MARGIN_X);
    if (card_w < 120)
    {
        card_w = lcd_w;
    }

    card_y = system_service_content_top() + MIC_LAYOUT_GAP;
    card_h = system_service_content_bottom() - card_y - MIC_LAYOUT_GAP;
    if (card_h < 140)
    {
        card_h = 140;
    }

    ctx->card = lv_obj_create(scr);
    lv_obj_set_size(ctx->card, card_w, card_h);
    lv_obj_set_pos(ctx->card, MIC_MARGIN_X, card_y);
    lv_obj_set_style_bg_color(ctx->card, lv_color_hex(APP_THEME_SURFACE_HEX), 0);
    lv_obj_set_style_bg_opa(ctx->card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(ctx->card, lv_color_hex(APP_THEME_BORDER_HEX), 0);
    lv_obj_set_style_border_width(ctx->card, 2, 0);
    lv_obj_set_style_radius(ctx->card, 12, 0);
    lv_obj_set_style_pad_all(ctx->card, 10, 0);
    lv_obj_set_style_pad_row(ctx->card, 8, 0);
    lv_obj_set_layout(ctx->card, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(ctx->card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(ctx->card,
                          LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);

    title_label = lv_label_create(ctx->card);
    lv_label_set_text(title_label, "Mic Monitor");
    lv_obj_set_style_text_font(title_label, MIC_FONT_TEXT, 0);
    lv_obj_set_style_text_color(title_label, lv_color_hex(APP_THEME_TEXT_PRIMARY_HEX), 0);

    ctx->status_label = lv_label_create(ctx->card);
    lv_label_set_text(ctx->status_label, "Mic init...");
    lv_obj_set_style_text_font(ctx->status_label, MIC_FONT_TEXT, 0);
    lv_obj_set_style_text_color(ctx->status_label, lv_color_hex(APP_THEME_TEXT_SECONDARY_HEX), 0);

    ctx->level_label = lv_label_create(ctx->card);
    lv_label_set_text(ctx->level_label, "Signal level: 0%");
    lv_obj_set_style_text_font(ctx->level_label, MIC_FONT_TEXT, 0);
    lv_obj_set_style_text_color(ctx->level_label, lv_color_hex(APP_THEME_TEXT_PRIMARY_HEX), 0);

    ctx->level_bar = lv_bar_create(ctx->card);
    lv_obj_set_width(ctx->level_bar, lv_pct(100));
    lv_obj_set_height(ctx->level_bar, 16);
    lv_bar_set_range(ctx->level_bar, 0, 100);
    lv_bar_set_value(ctx->level_bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(ctx->level_bar, lv_color_hex(APP_THEME_BAR_HEX), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(ctx->level_bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(ctx->level_bar,
                              lv_color_hex(APP_THEME_ACCENT_HEX),
                              LV_PART_INDICATOR);

    ctx->speech_label = lv_label_create(ctx->card);
    lv_label_set_text(ctx->speech_label, "Speech: idle");
    lv_obj_set_style_text_font(ctx->speech_label, MIC_FONT_TEXT, 0);
    lv_obj_set_style_text_color(ctx->speech_label, lv_color_hex(APP_THEME_TEXT_PRIMARY_HEX), 0);

    ctx->recognized_title_label = lv_label_create(ctx->card);
    lv_label_set_text(ctx->recognized_title_label, "Recognized text");
    lv_obj_set_style_text_font(ctx->recognized_title_label, MIC_FONT_TEXT, 0);
    lv_obj_set_style_text_color(ctx->recognized_title_label,
                                lv_color_hex(APP_THEME_TEXT_SECONDARY_HEX),
                                0);

    ctx->recognized_label = lv_label_create(ctx->card);
    lv_obj_set_width(ctx->recognized_label, lv_pct(100));
    lv_label_set_long_mode(ctx->recognized_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(ctx->recognized_label, MIC_FONT_TEXT, 0);
    lv_obj_set_style_text_color(ctx->recognized_label,
                                lv_color_hex(APP_THEME_TEXT_PRIMARY_HEX),
                                0);

    ctx->screen = scr;
    return scr;
}

/*
 * brief: Release Mic screen runtime resources when LVGL deletes the screen object.
 * input: e - LVGL delete event with Mic context as user_data.
 * output: None.
 */
static void _mic_on_screen_delete(lv_event_t *e)
{
    mic_app_ctx_t *ctx;

    if (e == NULL)
    {
        return;
    }

    ctx = (mic_app_ctx_t *)lv_event_get_user_data(e);
    if (ctx == NULL)
    {
        return;
    }

    if (ctx->ui_timer != NULL)
    {
        lv_timer_del(ctx->ui_timer);
        ctx->ui_timer = NULL;
    }

    _mic_stop_task(&ctx->sample_task_handle, &ctx->sample_task_stop);
    mic_drv_close();

    if (ctx->segment_buf != NULL)
    {
        heap_caps_free(ctx->segment_buf);
        ctx->segment_buf = NULL;
    }

    if (s_mic_ctx == ctx)
    {
        s_mic_ctx = NULL;
    }

    free(ctx);
}

/*
 * brief: Update recognized text shown on Mic UI from external ASR pipeline.
 * input: text - recognized text line to display.
 * output: None.
 */
void mic_ui_set_recognized_text(const char *text)
{
    mic_app_ctx_t *ctx;

    ctx = s_mic_ctx;
    if (ctx == NULL)
    {
        return;
    }

    portENTER_CRITICAL(&ctx->state_lock);
    _mic_copy_text(ctx->recognized_text,
                   sizeof(ctx->recognized_text),
                   (text != NULL) ? text : "");
    portEXIT_CRITICAL(&ctx->state_lock);
}

/*
 * brief: Create microphone monitor app screen and runtime resources.
 * input: lcd_w/lcd_h - display resolution.
 * output: LVGL screen object on success; otherwise NULL.
 */
lv_obj_t *mic_create_screen(lv_coord_t lcd_w, lv_coord_t lcd_h)
{
    BaseType_t task_ok;
    esp_err_t ret;
    size_t segment_capacity;
    mic_app_ctx_t *ctx;

    if (s_mic_ctx != NULL)
    {
        return s_mic_ctx->screen;
    }

    ctx = (mic_app_ctx_t *)calloc(1, sizeof(mic_app_ctx_t));
    if (ctx == NULL)
    {
        return NULL;
    }

    ctx->state_lock = (portMUX_TYPE)portMUX_INITIALIZER_UNLOCKED;
    ctx->mic_state_ret = ESP_FAIL;
    ctx->asr_mode = ASR_MODE_OFFLINE;

    segment_capacity = ((size_t)MIC_SAMPLE_RATE_HZ * (size_t)MIC_SEGMENT_MAX_MS) / 1000U;
    ctx->segment_capacity = segment_capacity;
    ctx->segment_samples = 0U;

    ctx->segment_buf = (int16_t *)heap_caps_malloc(segment_capacity * sizeof(int16_t),
                                                   MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (ctx->segment_buf == NULL)
    {
        ctx->segment_buf = (int16_t *)heap_caps_malloc(segment_capacity * sizeof(int16_t),
                                                       MALLOC_CAP_8BIT);
    }

    if (ctx->segment_buf == NULL)
    {
        free(ctx);
        return NULL;
    }

    _mic_copy_text(ctx->recognized_text,
                   sizeof(ctx->recognized_text),
                   "Say command words. Auto choose cloud/offline ASR.");

    if (_mic_build_screen(ctx, lcd_w, lcd_h) == NULL)
    {
        heap_caps_free(ctx->segment_buf);
        free(ctx);
        return NULL;
    }

    lv_obj_add_event_cb(ctx->screen, _mic_on_screen_delete, LV_EVENT_DELETE, ctx);
    s_mic_ctx = ctx;

    ret = mic_drv_open(MIC_SAMPLE_RATE_HZ);
    if (ret != ESP_OK)
    {
        portENTER_CRITICAL(&ctx->state_lock);
        ctx->mic_state_ret = ret;
        _mic_copy_text(ctx->recognized_text,
                       sizeof(ctx->recognized_text),
                       "Mic init failed. Check PDM pins/power.");
        portEXIT_CRITICAL(&ctx->state_lock);
    }
    else
    {
        ctx->sample_task_stop = false;
        task_ok = xTaskCreate(_mic_sample_task,
                              "mic_sample",
                              MIC_SAMPLE_TASK_STACK_SIZE,
                              ctx,
                              MIC_SAMPLE_TASK_PRIORITY,
                              &ctx->sample_task_handle);
        if (task_ok != pdPASS)
        {
            mic_drv_close();
            portENTER_CRITICAL(&ctx->state_lock);
            ctx->mic_state_ret = ESP_FAIL;
            _mic_copy_text(ctx->recognized_text,
                           sizeof(ctx->recognized_text),
                           "Mic task create failed.");
            portEXIT_CRITICAL(&ctx->state_lock);
        }
        else
        {
            portENTER_CRITICAL(&ctx->state_lock);
            ctx->mic_state_ret = ESP_OK;
            portEXIT_CRITICAL(&ctx->state_lock);
        }
    }

    ctx->ui_timer = lv_timer_create(_mic_ui_timer_cb, MIC_UI_UPDATE_PERIOD_MS, ctx);
    if (ctx->ui_timer == NULL)
    {
        lv_obj_del(ctx->screen);
        return NULL;
    }

    _mic_refresh_ui(ctx);
    return ctx->screen;
}
