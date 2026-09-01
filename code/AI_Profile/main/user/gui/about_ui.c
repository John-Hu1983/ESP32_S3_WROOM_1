#include "about_ui.h"

#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_system.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TAG "about_ui"

static about_ui_runtime_s s_about_runtime;
static portMUX_TYPE s_about_lock = portMUX_INITIALIZER_UNLOCKED;

/*
 * brief : _about_obj_valid.
 * input : see parameters.
 * output: return value from this function.
 * type  : private
 */
static bool _about_obj_valid(lv_obj_t* obj) { return (obj != NULL) && lv_obj_is_valid(obj); }

/*
 * brief : _about_task_state_name.
 * input : see parameters.
 * output: return value from this function.
 * type  : private
 */
static const char* _about_task_state_name(eTaskState state) {
    switch (state) {
        case eRunning:
            return "R";
        case eReady:
            return "Y";
        case eBlocked:
            return "B";
        case eSuspended:
            return "S";
        case eDeleted:
            return "D";
        case eInvalid:
        default:
            return "?";
    }
}

#if (configUSE_TRACE_FACILITY == 1)
/*
 * brief : _about_collect_task_snapshot.
 * input : see parameters.
 * output: return value from this function.
 * type  : private
 */
static bool _about_collect_task_snapshot(about_task_row_s* rows, uint16_t* row_count,
                                         uint64_t* total_runtime, uint64_t* idle_runtime) {
    if ((rows == NULL) || (row_count == NULL) || (total_runtime == NULL) ||
        (idle_runtime == NULL)) {
        return false;
    }

    UBaseType_t alloc_task_count = uxTaskGetNumberOfTasks() + 5U;
    TaskStatus_t* task_states = (TaskStatus_t*)malloc(sizeof(TaskStatus_t) * alloc_task_count);
    if (task_states == NULL) {
        *row_count = 0U;
        return false;
    }

    configRUN_TIME_COUNTER_TYPE run_time_counter = 0;
    UBaseType_t task_count = uxTaskGetSystemState(task_states, alloc_task_count, &run_time_counter);
    if (task_count == 0U) {
        free(task_states);
        *row_count = 0U;
        return false;
    }

    uint16_t produced = 0U;
    uint64_t idle_sum = 0U;
    for (UBaseType_t i = 0; i < task_count; i++) {
        const char* task_name = (task_states[i].pcTaskName != NULL) ? task_states[i].pcTaskName : "?";
        const char* state = _about_task_state_name(task_states[i].eCurrentState);

        if (strncmp(task_name, "IDLE", 4) == 0) {
            idle_sum += (uint64_t)task_states[i].ulRunTimeCounter;
        }

        if (produced >= ABOUT_TASKLIST_MAX_ROWS) {
            continue;
        }

        snprintf(rows[produced].name, sizeof(rows[produced].name), "%s", task_name);
        snprintf(rows[produced].state, sizeof(rows[produced].state), "%s", state);

        uint32_t prio = (uint32_t)task_states[i].uxCurrentPriority;
        if (prio > 255U) {
            prio = 255U;
        }
        rows[produced].priority = (uint8_t)prio;

        uint32_t stack = (uint32_t)task_states[i].usStackHighWaterMark;
        if (stack > 65535U) {
            stack = 65535U;
        }
        rows[produced].stack_high_watermark = (uint16_t)stack;

        produced++;
    }

    *row_count = produced;
    *total_runtime = (uint64_t)run_time_counter;
    *idle_runtime = idle_sum;
    free(task_states);
    return true;
}

/*
 * brief : _about_read_cpu_usage.
 * input : see parameters.
 * output: return value from this function.
 * type  : private
 */
static uint8_t _about_read_cpu_usage(uint64_t total_runtime, uint64_t idle_runtime) {
    static bool has_prev_sample;
    static uint64_t prev_total_runtime;
    static uint64_t prev_idle_runtime;

    if (!has_prev_sample) {
        prev_total_runtime = total_runtime;
        prev_idle_runtime = idle_runtime;
        has_prev_sample = true;
        return 0U;
    }

    uint64_t total_delta = total_runtime - prev_total_runtime;
    uint64_t idle_delta = idle_runtime - prev_idle_runtime;

    prev_total_runtime = total_runtime;
    prev_idle_runtime = idle_runtime;

    if (total_delta == 0U) {
        return 0U;
    }

    uint64_t total_capacity = total_delta * (uint64_t)CONFIG_FREERTOS_NUMBER_OF_CORES;
    if (total_capacity == 0U) {
        return 0U;
    }

    uint64_t busy_delta = (idle_delta < total_capacity) ? (total_capacity - idle_delta) : 0U;
    uint32_t percent = (uint32_t)((busy_delta * 100U) / total_capacity);
    if (percent > 100U) {
        percent = 100U;
    }
    return (uint8_t)percent;
}
#else
/*
 * brief : _about_collect_task_snapshot.
 * input : see parameters.
 * output: return value from this function.
 * type  : private
 */
static bool _about_collect_task_snapshot(about_task_row_s* rows, uint16_t* row_count,
                                         uint64_t* total_runtime, uint64_t* idle_runtime) {
    (void)rows;
    (void)total_runtime;
    (void)idle_runtime;

    if (row_count != NULL) {
        *row_count = 0U;
    }
    return false;
}

/*
 * brief : _about_read_cpu_usage.
 * input : see parameters.
 * output: return value from this function.
 * type  : private
 */
static uint8_t _about_read_cpu_usage(uint64_t total_runtime, uint64_t idle_runtime) {
    (void)total_runtime;
    (void)idle_runtime;
    return 0U;
}
#endif

/*
 * brief : _about_format_mem_value.
 * input : see parameters.
 * output: none.
 * type  : private
 */
static void _about_format_mem_value(char* out, size_t out_len, uint32_t caps) {
    size_t total = heap_caps_get_total_size(caps);
    if (total == 0U) {
        snprintf(out, out_len, "N/A");
        return;
    }

    size_t free = heap_caps_get_free_size(caps);
    size_t used = (free < total) ? (total - free) : 0U;
    uint32_t percent = (uint32_t)((used * 100U) / total);
    if (percent > 100U) {
        percent = 100U;
    }

    snprintf(out, out_len, "%u/%uKB (%u%%)", (unsigned)(used / 1024U),
             (unsigned)(total / 1024U), (unsigned)percent);
}

/*
 * brief : _about_sync_ui.
 * input : see parameters.
 * output: none.
 * type  : private
 */
static void _about_sync_ui(void* param) {
    about_ui_runtime_s* runtime = (about_ui_runtime_s*)param;
    if (runtime == NULL) {
        return;
    }

    if (!_about_obj_valid(runtime->root) || !_about_obj_valid(runtime->idf_value_label) ||
        !_about_obj_valid(runtime->cpu_value_label) || !_about_obj_valid(runtime->ram_value_label) ||
        !_about_obj_valid(runtime->psram_value_label) ||
        !_about_obj_valid(runtime->tasklist_table)) {
        return;
    }

    char idf_text[ABOUT_INFO_TEXT_LEN];
    char cpu_text[ABOUT_INFO_TEXT_LEN];
    char ram_text[ABOUT_INFO_TEXT_LEN];
    char psram_text[ABOUT_INFO_TEXT_LEN];
    about_task_row_s rows[ABOUT_TASKLIST_MAX_ROWS];
    uint16_t row_count = 0U;
    bool dirty = false;

    taskENTER_CRITICAL(&s_about_lock);
    if (runtime->dirty) {
        snprintf(idf_text, sizeof(idf_text), "%s", runtime->idf_text);
        snprintf(cpu_text, sizeof(cpu_text), "%s", runtime->cpu_text);
        snprintf(ram_text, sizeof(ram_text), "%s", runtime->ram_text);
        snprintf(psram_text, sizeof(psram_text), "%s", runtime->psram_text);

        row_count = runtime->task_row_count;
        if (row_count > ABOUT_TASKLIST_MAX_ROWS) {
            row_count = ABOUT_TASKLIST_MAX_ROWS;
        }
        if (row_count > 0U) {
            memcpy(rows, runtime->task_rows, sizeof(about_task_row_s) * row_count);
        }

        runtime->dirty = false;
        dirty = true;
    }
    taskEXIT_CRITICAL(&s_about_lock);

    if (!dirty) {
        return;
    }

    lv_label_set_text(runtime->idf_value_label, idf_text);
    lv_label_set_text(runtime->cpu_value_label, cpu_text);
    lv_label_set_text(runtime->ram_value_label, ram_text);
    lv_label_set_text(runtime->psram_value_label, psram_text);

    lv_obj_t* table = runtime->tasklist_table;
    lv_table_set_row_cnt(table, (uint16_t)(row_count + 1U));
    lv_table_set_cell_value(table, 0U, 0U, "Task");
    lv_table_set_cell_value(table, 0U, 1U, "S");
    lv_table_set_cell_value(table, 0U, 2U, "P");
    lv_table_set_cell_value(table, 0U, 3U, "Stack");

    if (row_count == 0U) {
        lv_table_set_row_cnt(table, 2U);
        lv_table_set_cell_value(table, 1U, 0U, "N/A");
        lv_table_set_cell_value(table, 1U, 1U, "-");
        lv_table_set_cell_value(table, 1U, 2U, "-");
        lv_table_set_cell_value(table, 1U, 3U, "-");
        return;
    }

    for (uint16_t i = 0; i < row_count; i++) {
        char prio_text[8];
        char stack_text[12];
        snprintf(prio_text, sizeof(prio_text), "%u", (unsigned)rows[i].priority);
        snprintf(stack_text, sizeof(stack_text), "%u", (unsigned)rows[i].stack_high_watermark);

        lv_table_set_cell_value(table, (uint16_t)(i + 1U), 0U, rows[i].name);
        lv_table_set_cell_value(table, (uint16_t)(i + 1U), 1U, rows[i].state);
        lv_table_set_cell_value(table, (uint16_t)(i + 1U), 2U, prio_text);
        lv_table_set_cell_value(table, (uint16_t)(i + 1U), 3U, stack_text);
    }
}

/*
 * brief : _about_refresh_info.
 * input : see parameters.
 * output: none.
 * type  : private
 */
static void _about_refresh_info(about_ui_runtime_s* runtime) {
    about_task_row_s rows[ABOUT_TASKLIST_MAX_ROWS] = {0};
    uint16_t row_count = 0U;
    uint64_t total_runtime = 0U;
    uint64_t idle_runtime = 0U;

    bool has_snapshot = _about_collect_task_snapshot(rows, &row_count, &total_runtime, &idle_runtime);
    uint8_t cpu_usage = has_snapshot ? _about_read_cpu_usage(total_runtime, idle_runtime) : 0U;

    taskENTER_CRITICAL(&s_about_lock);
    snprintf(runtime->idf_text, sizeof(runtime->idf_text), "%s", esp_get_idf_version());
    snprintf(runtime->cpu_text, sizeof(runtime->cpu_text), "%u%%", (unsigned)cpu_usage);
    _about_format_mem_value(runtime->ram_text, sizeof(runtime->ram_text), MALLOC_CAP_INTERNAL);
    _about_format_mem_value(runtime->psram_text, sizeof(runtime->psram_text), MALLOC_CAP_SPIRAM);

    runtime->task_row_count = row_count;
    if (row_count > 0U) {
        memcpy(runtime->task_rows, rows, sizeof(about_task_row_s) * row_count);
    }
    runtime->dirty = true;
    taskEXIT_CRITICAL(&s_about_lock);

    lv_lock();
    lv_result_t lv_res = lv_async_call(_about_sync_ui, runtime);
    lv_unlock();

    if (lv_res != LV_RESULT_OK) {
        ESP_LOGW(TAG, "lv_async_call failed while refreshing about info");
    }
}

/*
 * brief : _about_ui_task.
 * input : see parameters.
 * output: none.
 * type  : private
 */
static void _about_ui_task(void* param) {
    about_ui_runtime_s* runtime = (about_ui_runtime_s*)param;
    uint32_t refresh_elapsed_ms = ABOUT_REFRESH_PERIOD_MS;

    while (1) {
        btn_status_e btn_val = button_scan_state(&runtime->button_scan, ABOUT_TASK_PERIOD_MS);
        if ((btn_val == Btn_Both_Click) && (runtime->home_cb != NULL)) {
            runtime->home_cb(runtime->home_user_ctx);
        }

        if (refresh_elapsed_ms >= ABOUT_REFRESH_PERIOD_MS) {
            refresh_elapsed_ms = 0U;
            _about_refresh_info(runtime);
        }

        delay_ms(ABOUT_TASK_PERIOD_MS);
        refresh_elapsed_ms += ABOUT_TASK_PERIOD_MS;
    }
}

/*
 * brief : about_create_screen.
 * input : see parameters.
 * output: return value from this function.
 * type  : public
 */
lv_obj_t* about_create_screen(lv_obj_t* parent, lv_coord_t area_w, lv_coord_t area_h,
                              ui_menu_home_cb_t home_cb, void* home_user_ctx) {
    if (parent == NULL) {
        return NULL;
    }

    if (s_about_runtime.task_handle != NULL) {
        vTaskDelete(s_about_runtime.task_handle);
        s_about_runtime.task_handle = NULL;
    }

    memset(&s_about_runtime, 0, sizeof(s_about_runtime));
    s_about_runtime.home_cb = home_cb;
    s_about_runtime.home_user_ctx = home_user_ctx;

    lv_obj_t* screen = lv_obj_create(parent);
    lv_obj_set_size(screen, area_w, area_h);
    lv_obj_set_pos(screen, 0, 0);
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x1C1B1A), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(screen, 0, 0);
    lv_obj_set_style_radius(screen, 0, 0);
    lv_obj_set_style_pad_all(screen, 2, 0);

    lv_obj_t* title = lv_label_create(screen);
    lv_label_set_text(title, "About / System");
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 0, 0);

    const lv_coord_t key_x = 0;
    const lv_coord_t value_x = 72;
    const lv_coord_t row_y0 = 18;
    const lv_coord_t row_gap = 16;

    lv_obj_t* idf_key = lv_label_create(screen);
    lv_obj_set_style_text_color(idf_key, lv_color_white(), 0);
    lv_obj_set_pos(idf_key, key_x, row_y0);
    lv_label_set_text(idf_key, "ESP-IDF:");

    lv_obj_t* cpu_key = lv_label_create(screen);
    lv_obj_set_style_text_color(cpu_key, lv_color_white(), 0);
    lv_obj_set_pos(cpu_key, key_x, (lv_coord_t)(row_y0 + row_gap));
    lv_label_set_text(cpu_key, "CPU:");

    lv_obj_t* ram_key = lv_label_create(screen);
    lv_obj_set_style_text_color(ram_key, lv_color_white(), 0);
    lv_obj_set_pos(ram_key, key_x, (lv_coord_t)(row_y0 + row_gap * 2));
    lv_label_set_text(ram_key, "RAM:");

    lv_obj_t* psram_key = lv_label_create(screen);
    lv_obj_set_style_text_color(psram_key, lv_color_white(), 0);
    lv_obj_set_pos(psram_key, key_x, (lv_coord_t)(row_y0 + row_gap * 3));
    lv_label_set_text(psram_key, "PSRAM:");

    s_about_runtime.idf_value_label = lv_label_create(screen);
    lv_obj_set_style_text_color(s_about_runtime.idf_value_label, lv_color_white(), 0);
    lv_obj_set_pos(s_about_runtime.idf_value_label, value_x, row_y0);
    lv_label_set_text(s_about_runtime.idf_value_label, "--");

    s_about_runtime.cpu_value_label = lv_label_create(screen);
    lv_obj_set_style_text_color(s_about_runtime.cpu_value_label, lv_color_white(), 0);
    lv_obj_set_pos(s_about_runtime.cpu_value_label, value_x, (lv_coord_t)(row_y0 + row_gap));
    lv_label_set_text(s_about_runtime.cpu_value_label, "--");

    s_about_runtime.ram_value_label = lv_label_create(screen);
    lv_obj_set_style_text_color(s_about_runtime.ram_value_label, lv_color_white(), 0);
    lv_obj_set_pos(s_about_runtime.ram_value_label, value_x, (lv_coord_t)(row_y0 + row_gap * 2));
    lv_label_set_text(s_about_runtime.ram_value_label, "--");

    s_about_runtime.psram_value_label = lv_label_create(screen);
    lv_obj_set_style_text_color(s_about_runtime.psram_value_label, lv_color_white(), 0);
    lv_obj_set_pos(s_about_runtime.psram_value_label, value_x,
                   (lv_coord_t)(row_y0 + row_gap * 3));
    lv_label_set_text(s_about_runtime.psram_value_label, "--");

    lv_obj_t* tasklist_title = lv_label_create(screen);
    lv_obj_set_style_text_color(tasklist_title, lv_color_white(), 0);
    lv_obj_align(tasklist_title, LV_ALIGN_TOP_LEFT, 0, 84);
    lv_label_set_text(tasklist_title, "TaskList:");

    lv_coord_t panel_h = (lv_coord_t)(area_h - 106);
    if (panel_h < 36) {
        panel_h = 36;
    }

    lv_obj_t* panel = lv_obj_create(screen);
    lv_obj_set_size(panel, lv_pct(100), panel_h);
    lv_obj_align(panel, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(panel, lv_color_hex(0x202020), 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(panel, 1, 0);
    lv_obj_set_style_border_color(panel, lv_color_hex(0x555555), 0);
    lv_obj_set_style_radius(panel, 4, 0);
    lv_obj_set_style_pad_all(panel, 2, 0);

    s_about_runtime.tasklist_table = lv_table_create(panel);
    lv_obj_set_size(s_about_runtime.tasklist_table, lv_pct(100), lv_pct(100));
    lv_obj_set_style_text_color(s_about_runtime.tasklist_table, lv_color_white(), 0);
    lv_obj_set_style_text_font(s_about_runtime.tasklist_table, &lv_font_montserrat_14,
                               LV_PART_ITEMS);
    lv_obj_set_style_pad_top(s_about_runtime.tasklist_table, 3, LV_PART_ITEMS);
    lv_obj_set_style_pad_bottom(s_about_runtime.tasklist_table, 3, LV_PART_ITEMS);
    lv_obj_set_style_pad_left(s_about_runtime.tasklist_table, 2, LV_PART_ITEMS);
    lv_obj_set_style_pad_right(s_about_runtime.tasklist_table, 2, LV_PART_ITEMS);
    lv_table_set_col_cnt(s_about_runtime.tasklist_table, 4U);
    lv_table_set_row_cnt(s_about_runtime.tasklist_table, 2U);

    lv_coord_t table_w = (lv_coord_t)(area_w - 8);
    if (table_w < 120) {
        table_w = 120;
    }
    lv_table_set_column_width(s_about_runtime.tasklist_table, 0U, (lv_coord_t)(table_w * 52 / 100));
    lv_table_set_column_width(s_about_runtime.tasklist_table, 1U, (lv_coord_t)(table_w * 8 / 100));
    lv_table_set_column_width(s_about_runtime.tasklist_table, 2U, (lv_coord_t)(table_w * 8 / 100));
    lv_table_set_column_width(s_about_runtime.tasklist_table, 3U, (lv_coord_t)(table_w * 32 / 100));

    lv_table_set_cell_value(s_about_runtime.tasklist_table, 0U, 0U, "Task");
    lv_table_set_cell_value(s_about_runtime.tasklist_table, 0U, 1U, "S");
    lv_table_set_cell_value(s_about_runtime.tasklist_table, 0U, 2U, "P");
    lv_table_set_cell_value(s_about_runtime.tasklist_table, 0U, 3U, "Stack");
    lv_table_set_cell_value(s_about_runtime.tasklist_table, 1U, 0U, "collecting...");

    s_about_runtime.root = screen;
    _about_refresh_info(&s_about_runtime);

    BaseType_t task_ok = xTaskCreate(_about_ui_task, "about_ui", ABOUT_TASK_STACK_SIZE,
                                     &s_about_runtime, 5, &s_about_runtime.task_handle);
    if (task_ok != pdPASS) {
        s_about_runtime.task_handle = NULL;
        lv_obj_del(screen);
        s_about_runtime.root = NULL;
        ESP_LOGE(TAG, "xTaskCreate failed");
        return NULL;
    }

    return screen;
}

/*
 * brief : about_destroy_screen.
 * input : see parameters.
 * output: none.
 * type  : public
 */
void about_destroy_screen(lv_obj_t* screen) {
    if (s_about_runtime.task_handle != NULL) {
        vTaskDelete(s_about_runtime.task_handle);
        s_about_runtime.task_handle = NULL;
    }

    taskENTER_CRITICAL(&s_about_lock);
    s_about_runtime.home_cb = NULL;
    s_about_runtime.home_user_ctx = NULL;
    s_about_runtime.button_scan = (btn_scan_s){0};
    s_about_runtime.idf_value_label = NULL;
    s_about_runtime.cpu_value_label = NULL;
    s_about_runtime.ram_value_label = NULL;
    s_about_runtime.psram_value_label = NULL;
    s_about_runtime.tasklist_table = NULL;
    s_about_runtime.root = NULL;
    s_about_runtime.dirty = false;
    s_about_runtime.task_row_count = 0U;
    taskEXIT_CRITICAL(&s_about_lock);

    if (_about_obj_valid(screen)) {
        lv_obj_del(screen);
    }
}
