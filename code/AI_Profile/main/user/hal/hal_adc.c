#include "hal_adc.h"

static adc_continuous_handle_t adc_handle = NULL;
static TaskHandle_t adc_task_handle = NULL;
static SemaphoreHandle_t adc_command_lock = NULL;
static SemaphoreHandle_t adc_command_done = NULL;
static hal_adc_link_t* adc_link_head = NULL;
static volatile bool adc_task_run = false;
static bool adc_started = false;
static volatile adc_command_e adc_command = ADC_COMMAND_NONE;
static esp_err_t adc_command_result = ESP_OK;
static portMUX_TYPE adc_lock = portMUX_INITIALIZER_UNLOCKED;

static void _adc_task(void* arg);
static esp_err_t _adc_apply_config(void);
static esp_err_t _adc_stop_driver(void);
static esp_err_t _adc_request_command(adc_command_e command);
static void _adc_process_command(void);
static hal_adc_link_t* _adc_find_link(adc_unit_t unit, adc_channel_t channel);
static bool _adc_unlink(hal_adc_link_t* link);

/*
 * brief : _adc_find_link.
 * input : see parameters.
 * output: matching ADC configuration entity.
 * type  : private
 * theory: walk the registered entity list and match one ADC unit/channel pair.
 */
static hal_adc_link_t* _adc_find_link(adc_unit_t unit, adc_channel_t channel) {
    hal_adc_link_t* link = adc_link_head;

    while (link != NULL) {
        if ((link->unit == unit) && (link->channel == channel)) {
            return link;
        }
        link = link->next;
    }

    return NULL;
}

/*
 * brief : _adc_unlink.
 * input : see parameters.
 * output: true when the entity was linked.
 * type  : private
 * theory: unlink under the cache lock so the reader task cannot retain a removed entity.
 */
static bool _adc_unlink(hal_adc_link_t* link) {
    hal_adc_link_t* current = NULL;
    hal_adc_link_t* previous = NULL;
    bool found = false;

    portENTER_CRITICAL(&adc_lock);
    current = adc_link_head;
    while (current != NULL) {
        if (current == link) {
            if (previous == NULL) {
                adc_link_head = current->next;
            }
            else {
                previous->next = current->next;
            }
            found = true;
            break;
        }
        previous = current;
        current = current->next;
    }
    portEXIT_CRITICAL(&adc_lock);

    return found;
}

/*
 * brief : _adc_apply_config.
 * input : none.
 * output: return value from this function.
 * type  : private
 * theory: stop DMA, rebuild its complete scan pattern from the entity list, and restart it.
 */
static esp_err_t _adc_apply_config(void) {
    adc_digi_pattern_config_t patterns[SOC_ADC_PATT_LEN_MAX] = { 0 };
    adc_continuous_config_t continuous_cfg = { 0 };
    hal_adc_link_t* link = NULL;
    uint32_t pattern_count = 0U;
    esp_err_t ret = ESP_OK;

    portENTER_CRITICAL(&adc_lock);
    link = adc_link_head;
    while ((link != NULL) && (pattern_count < SOC_ADC_PATT_LEN_MAX)) {
        patterns[pattern_count].unit = link->unit;
        patterns[pattern_count].channel = link->channel;
        patterns[pattern_count].atten = HAL_ADC_ATTENUATION;
        patterns[pattern_count].bit_width = HAL_ADC_BITWIDTH;
        pattern_count++;
        link = link->next;
    }
    portEXIT_CRITICAL(&adc_lock);
    if ((pattern_count == 0U) || (link != NULL)) {
        return ESP_ERR_INVALID_SIZE;
    }

    if (adc_started) {
        ret = adc_continuous_stop(adc_handle);
        if ((ret != ESP_OK) && (ret != ESP_ERR_INVALID_STATE)) {
            return ret;
        }
        adc_started = false;
    }

    continuous_cfg.pattern_num = pattern_count;
    continuous_cfg.adc_pattern = patterns;
    continuous_cfg.sample_freq_hz = HAL_ADC_SAMPLE_FREQ_HZ;
    continuous_cfg.conv_mode = ADC_CONV_SINGLE_UNIT_1;

    ret = adc_continuous_config(adc_handle, &continuous_cfg);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = adc_continuous_start(adc_handle);
    if (ret == ESP_OK) {
        adc_started = true;
    }

    return ret;
}

/*
 * brief : _adc_stop_driver.
 * input : none.
 * output: return value from this function.
 * type  : private
 * theory: stop and release the continuous driver in the task that acquired its hardware lock.
 */
static esp_err_t _adc_stop_driver(void) {
    esp_err_t ret = ESP_OK;

    if (adc_started) {
        ret = adc_continuous_stop(adc_handle);
        if ((ret != ESP_OK) && (ret != ESP_ERR_INVALID_STATE)) {
            return ret;
        }
        adc_started = false;
    }

    ret = adc_continuous_deinit(adc_handle);
    if (ret == ESP_OK) {
        adc_handle = NULL;
    }

    return ret;
}

/*
 * brief : _adc_request_command.
 * input : see parameters.
 * output: return value from this function.
 * type  : private
 * theory: serialize callers and synchronously delegate driver ownership operations to the ADC task.
 */
static esp_err_t _adc_request_command(adc_command_e command) {
    esp_err_t ret = ESP_OK;

    if ((adc_task_handle == NULL) || (adc_command_lock == NULL)
        || (adc_command_done == NULL)) {
        return ESP_ERR_INVALID_STATE;
    }
    if (xSemaphoreTake(adc_command_lock, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    (void)xSemaphoreTake(adc_command_done, 0U);
    adc_command_result = ESP_FAIL;
    adc_command = command;
    xTaskNotifyGive(adc_task_handle);

    if (xSemaphoreTake(adc_command_done, portMAX_DELAY) == pdTRUE) {
        ret = adc_command_result;
    }
    else {
        ret = ESP_ERR_TIMEOUT;
    }

    xSemaphoreGive(adc_command_lock);
    return ret;
}

/*
 * brief : _adc_process_command.
 * input : none.
 * output: none.
 * type  : private
 * theory: execute start, stop, and reconfiguration only from the ADC driver-owner task.
 */
static void _adc_process_command(void) {
    adc_command_e command = adc_command;
    esp_err_t ret = ESP_ERR_INVALID_ARG;

    adc_command = ADC_COMMAND_NONE;
    if (command == ADC_COMMAND_APPLY_CONFIG) {
        ret = _adc_apply_config();
    }
    else if (command == ADC_COMMAND_STOP) {
        ret = _adc_stop_driver();
        adc_task_run = false;
        adc_task_handle = NULL;
    }

    adc_command_result = ret;
    xSemaphoreGive(adc_command_done);
}

/*
 * brief : hal_adc_insert.
 * input : see parameters.
 * output: return value from this function.
 * type  : public
 * theory: allocate a private PSRAM link from unit/channel and apply the DMA scan pattern.
 */
esp_err_t hal_adc_insert(adc_unit_t unit, adc_channel_t channel) {
    adc_continuous_handle_cfg_t handle_cfg = {
        .max_store_buf_size = HAL_ADC_FRAME_SAMPLES * sizeof(adc_digi_output_data_t) * 4U,
        .conv_frame_size = HAL_ADC_FRAME_SAMPLES * sizeof(adc_digi_output_data_t),
        .flags = {
            .flush_pool = 1,
        },
    };
    hal_adc_link_t* current = NULL;
    hal_adc_link_t* tail = NULL;
    hal_adc_link_t* link = NULL;
    uint32_t cfg_count = 0U;
    esp_err_t ret = ESP_OK;

    if (unit != ADC_UNIT_1) {
        return ESP_ERR_INVALID_ARG;
    }

    portENTER_CRITICAL(&adc_lock);
    current = adc_link_head;
    while (current != NULL) {
        if ((current->unit == unit) && (current->channel == channel)) {
            portEXIT_CRITICAL(&adc_lock);
            return ESP_OK;
        }
        cfg_count++;
        current = current->next;
    }
    portEXIT_CRITICAL(&adc_lock);

    if (cfg_count >= SOC_ADC_PATT_LEN_MAX) {
        return ESP_ERR_INVALID_SIZE;
    }

    link = (hal_adc_link_t*)heap_caps_calloc(1,
                                             sizeof(hal_adc_link_t),
                                             MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (link == NULL) {
        return ESP_ERR_NO_MEM;
    }
    link->unit = unit;
    link->channel = channel;

    if (adc_command_lock == NULL) {
        adc_command_lock = xSemaphoreCreateMutex();
        if (adc_command_lock == NULL) {
            heap_caps_free(link);
            return ESP_ERR_NO_MEM;
        }
    }
    if (adc_command_done == NULL) {
        adc_command_done = xSemaphoreCreateBinary();
        if (adc_command_done == NULL) {
            heap_caps_free(link);
            return ESP_ERR_NO_MEM;
        }
    }

    if (adc_handle == NULL) {
        ret = adc_continuous_new_handle(&handle_cfg, &adc_handle);
        if (ret != ESP_OK) {
            heap_caps_free(link);
            return ret;
        }
    }

    portENTER_CRITICAL(&adc_lock);
    if (adc_link_head == NULL) {
        adc_link_head = link;
    }
    else {
        tail = adc_link_head;
        while (tail->next != NULL) {
            tail = tail->next;
        }
        tail->next = link;
    }
    portEXIT_CRITICAL(&adc_lock);

    if (adc_task_handle == NULL) {
        adc_task_run = true;
        if (xTaskCreate(_adc_task,
                        "adc_task",
                        HAL_ADC_TASK_STACK_SIZE,
                        NULL,
                        HAL_ADC_TASK_PRIORITY,
                        &adc_task_handle)
            != pdPASS) {
            adc_task_run = false;
            (void)_adc_unlink(link);
            heap_caps_free(link);
            if (adc_link_head == NULL) {
                (void)adc_continuous_deinit(adc_handle);
                adc_handle = NULL;
            }
            return ESP_ERR_NO_MEM;
        }
    }

    ret = _adc_request_command(ADC_COMMAND_APPLY_CONFIG);
    if (ret != ESP_OK) {
        (void)_adc_unlink(link);
        heap_caps_free(link);
        if (adc_link_head != NULL) {
            (void)_adc_request_command(ADC_COMMAND_APPLY_CONFIG);
        }
        else {
            (void)_adc_request_command(ADC_COMMAND_STOP);
        }
        return ret;
    }

    return ESP_OK;
}

/*
 * brief : _adc_task.
 * input : see parameters.
 * output: none.
 * type  : private
 * theory: let DMA collect samples continuously and only copy completed data into a small cache.
 */
static void _adc_task(void* arg) {
    adc_digi_output_data_t frame[HAL_ADC_FRAME_SAMPLES] = { 0 };
    hal_adc_link_t* cfg = NULL;
    TickType_t update_tick = 0U;
    uint32_t read_len = 0U;
    uint32_t sample_count = 0U;
    uint32_t matched_count = 0U;
    uint32_t failure_count = 0U;
    uint32_t i = 0U;
    esp_err_t ret = ESP_OK;

    (void)arg;

    while (adc_task_run) {
        if (ulTaskNotifyTake(pdTRUE, adc_started ? 0U : portMAX_DELAY) > 0U) {
            _adc_process_command();
            if (!adc_task_run) {
                break;
            }
        }
        if (!adc_started) {
            continue;
        }

        ret = adc_continuous_read(adc_handle,
                                  (uint8_t*)frame,
                                  sizeof(frame),
                                  &read_len,
                                  20U);
        if (ret != ESP_OK) {
            failure_count++;
            if (failure_count >= HAL_ADC_RECOVERY_FAILURES) {
                (void)_adc_apply_config();
                failure_count = 0U;
            }
            continue;
        }

        sample_count = read_len / sizeof(adc_digi_output_data_t);
        matched_count = 0U;
        update_tick = xTaskGetTickCount();
        portENTER_CRITICAL(&adc_lock);
        for (i = 0U; i < sample_count; i++) {
            cfg = _adc_find_link((adc_unit_t)frame[i].type2.unit,
                                 (adc_channel_t)frame[i].type2.channel);
            if (cfg == NULL) {
                continue;
            }

            cfg->cache[cfg->cache_head] = (uint16_t)frame[i].type2.data;
            cfg->cache_head = (uint8_t)((cfg->cache_head + 1U) % HAL_ADC_AVG_SAMPLES);
            if (cfg->cache_count < HAL_ADC_AVG_SAMPLES) {
                cfg->cache_count++;
            }
            cfg->last_update_tick = update_tick;
            matched_count++;
        }
        portEXIT_CRITICAL(&adc_lock);

        if (matched_count > 0U) {
            failure_count = 0U;
        }
        else {
            failure_count++;
            if (failure_count >= HAL_ADC_RECOVERY_FAILURES) {
                (void)_adc_apply_config();
                failure_count = 0U;
            }
        }
    }

    adc_task_handle = NULL;
    vTaskDelete(NULL);
}

/*
 * brief : hal_adc_get_channel_sample.
 * input : see parameters.
 * output: return value from this function.
 * type  : public
 * theory: reject stale data, expose the live raw cache, and average a locked snapshot.
 */
esp_err_t hal_adc_get_channel_sample(adc_unit_t unit,
                                     adc_channel_t channel,
                                     uint32_t avg_samples,
                                     hal_adc_sample_s* sample) {
    uint16_t cache[HAL_ADC_AVG_SAMPLES] = { 0 };
    hal_adc_link_t* cfg = NULL;
    TickType_t now_tick = 0U;
    TickType_t last_update_tick = 0U;
    uint32_t sample_count = 0U;
    uint32_t idx = 0U;
    uint32_t sum = 0U;
    uint32_t i = 0U;

    if (sample == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    *sample = (hal_adc_sample_s){ 0 };
    now_tick = xTaskGetTickCount();
    portENTER_CRITICAL(&adc_lock);
    cfg = _adc_find_link(unit, channel);
    if (cfg == NULL) {
        portEXIT_CRITICAL(&adc_lock);
        return ESP_ERR_NOT_FOUND;
    }
    sample->raw_cache = cfg->cache;
    last_update_tick = cfg->last_update_tick;
    sample_count = cfg->cache_count;
    idx = cfg->cache_head;
    for (i = 0U; i < sample_count; i++) {
        idx = (idx == 0U) ? HAL_ADC_AVG_SAMPLES : idx;
        idx--;
        cache[i] = cfg->cache[idx];
    }
    portEXIT_CRITICAL(&adc_lock);

    if (sample_count == 0U) {
        return ESP_ERR_NOT_FOUND;
    }
    if ((now_tick - last_update_tick) > pdMS_TO_TICKS(HAL_ADC_STALE_TIMEOUT_MS)) {
        return ESP_ERR_TIMEOUT;
    }
    if ((avg_samples > 0U) && (avg_samples < sample_count)) {
        sample_count = avg_samples;
    }

    for (i = 0U; i < sample_count; i++) {
        sum += cache[i];
    }

    sample->valid = true;
    sample->raw_latest = cache[0];
    sample->raw_avg = sum / sample_count;
    sample->sample_count = sample_count;

    return ESP_OK;
}

