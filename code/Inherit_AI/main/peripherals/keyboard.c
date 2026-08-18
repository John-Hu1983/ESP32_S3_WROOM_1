#include "keyboard.h"
#include "service/desktop.h"

#include <esp_log.h>

#define TAG "Keyboard"

static esp_err_t keyboard_init_obj(keyboard_t* keyboard, const keyboard_config_t* config);
static bool keyboard_read_level(const keyboard_t* keyboard, btn_level_e* level);
static btn_status_e keyboard_scan_event(keyboard_t* keyboard, btn_scan_s* scan, uint8_t ms);
static void keyboard_set_notify_callback(keyboard_t* keyboard,
                                         keyboard_notify_callback_t notify_callback,
                                         void* user_ctx);

static keyboard_t g_keyboard_service = {0};
static keyboard_event_router_t g_keyboard_service_router = {0};
static bool g_keyboard_service_started = false;

static bool keyboard_service_post_to_desktop(void* runtime, uint8_t key_index, uint8_t event_type) {
    (void)runtime;
    return desktop_service_post_key_event(key_index, event_type);
}

/* ==================== private functions (static) ==================== */

/*
 * brief  : Validate keyboard pin tuple before touching GPBA02B.
 * input  : pin - Keyboard pin descriptor.
 * output : true if port/pin is in supported range; otherwise false.
 * type   : private
 */
static bool keyboard_is_pin_valid(keyboard_key_pin_t pin) {
    return pin.port <= GPBA02B_PORT_C && pin.pin <= 7;
}

/*
 * brief  : Convert scan status to notify enum for upper layers.
 * input  : status - Raw scan output status.
 * output : Mapped keyboard_notify_e value.
 * type   : private
 */
static keyboard_notify_e keyboard_status_to_notify(btn_status_e status) {
    switch (status) {
        case Btn_Idle:
            return Keyboard_Notify_Idle;
        case Btn_Up_Click:
            return Keyboard_Notify_Up_Click;
        case Btn_Up_Double:
            return Keyboard_Notify_Up_Double;
        case Btn_Up_Hold_Enter:
            return Keyboard_Notify_Up_Hold_Enter;
        case Btn_Up_Hold_Continue:
            return Keyboard_Notify_Up_Hold_Continue;
        case Btn_Down_Click:
            return Keyboard_Notify_Down_Click;
        case Btn_Down_Double:
            return Keyboard_Notify_Down_Double;
        case Btn_Down_Hold_Enter:
            return Keyboard_Notify_Down_Hold_Enter;
        case Btn_Down_Hold_Continue:
            return Keyboard_Notify_Down_Hold_Continue;
        case Btn_Both_Click:
            return Keyboard_Notify_Both_Click;
        case Btn_Both_Double:
            return Keyboard_Notify_Both_Double;
        case Btn_Both_Hold_Enter:
            return Keyboard_Notify_Both_Hold_Enter;
        case Btn_Both_Hold_Continue:
            return Keyboard_Notify_Both_Hold_Continue;
        default:
            return Keyboard_Notify_Idle;
    }
}

/*
 * brief  : Emit one application-facing key event via registered callback.
 * input  : keyboard - Keyboard object; key_index/event_type - event payload.
 * output : None.
 * type   : private
 */
static void keyboard_emit_app_event(keyboard_t* keyboard, uint8_t key_index, uint8_t event_type) {
    if (keyboard->app_event_callback != NULL) {
        keyboard->app_event_callback(key_index, event_type, keyboard->app_event_ctx);
        return;
    }

    ESP_LOGW(TAG, "Drop key event: key=%u type=%u (callback not set)", key_index,
             (unsigned int)event_type);
}

/*
 * brief  : Dispatch one notify event and translate it to app-level key events.
 * input  : keyboard - Keyboard object; notify - Notify event.
 * output : None.
 * type   : private
 */
static void keyboard_dispatch_notify(keyboard_t* keyboard, keyboard_notify_e notify) {
    if (notify == Keyboard_Notify_Idle) {
        return;
    }

    if (keyboard->notify_callback != NULL) {
        keyboard->notify_callback(notify, keyboard->notify_callback_ctx);
    }

    switch (notify) {
        case Keyboard_Notify_Up_Click:
            keyboard_emit_app_event(keyboard, 0, (uint8_t)Keyboard_App_Event_Click);
            break;
        case Keyboard_Notify_Down_Click:
            keyboard_emit_app_event(keyboard, 1, (uint8_t)Keyboard_App_Event_Click);
            break;
        case Keyboard_Notify_Up_Hold_Enter:
            keyboard_emit_app_event(keyboard, 0, (uint8_t)Keyboard_App_Event_LongPress);
            break;
        case Keyboard_Notify_Down_Hold_Enter:
            keyboard_emit_app_event(keyboard, 1, (uint8_t)Keyboard_App_Event_LongPress);
            break;
        case Keyboard_Notify_Both_Click:
            keyboard_emit_app_event(keyboard, KEYBOARD_KEY_INDEX_BOTH,
                                    (uint8_t)Keyboard_App_Event_DualClick);
            break;
        case Keyboard_Notify_Both_Hold_Enter:
            keyboard_emit_app_event(keyboard, 0, (uint8_t)Keyboard_App_Event_LongPress);
            break;
        case Keyboard_Notify_Up_Double:
        case Keyboard_Notify_Up_Hold_Continue:
        case Keyboard_Notify_Down_Double:
        case Keyboard_Notify_Down_Hold_Continue:
        case Keyboard_Notify_Both_Double:
        case Keyboard_Notify_Both_Hold_Continue:
        case Keyboard_Notify_Idle:
        default:
            break;
    }
}

/*
 * brief  : Fill keyboard configuration with board defaults.
 * input  : config - Output config pointer.
 * output : None.
 * type   : private
 */
static void keyboard_get_default_config(keyboard_config_t* config) {
    if (config == NULL) {
        return;
    }

    config->up_key.port = BUTTON_UP_IO_PORT;
    config->up_key.pin = BUTTON_UP_IO_PIN;
    config->down_key.port = BUTTON_DOWN_IO_PORT;
    config->down_key.pin = BUTTON_DOWN_IO_PIN;
    config->active_low = true;
    config->poll_interval_ms = 10;
}

/*
 * brief  : Register notify callback for raw keyboard notify events.
 * input  : keyboard - Keyboard object; notify_callback - callback; user_ctx - user context.
 * output : None.
 * type   : private
 */
static void keyboard_set_notify_callback(keyboard_t* keyboard,
                                         keyboard_notify_callback_t notify_callback,
                                         void* user_ctx) {
    if (keyboard == NULL) {
        return;
    }

    keyboard->notify_callback = notify_callback;
    keyboard->notify_callback_ctx = user_ctx;
}

/*
 * brief  : Initialize keyboard object and configure GPBA02B input pins.
 * input  : keyboard - Keyboard object; config - runtime configuration.
 * output : ESP_OK on success; error code otherwise.
 * type   : private
 */
static esp_err_t keyboard_init_obj(keyboard_t* keyboard, const keyboard_config_t* config) {
    gpba02b_io_input_mode_t input_mode;

    if (keyboard == NULL || config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (keyboard->running) {
        return ESP_ERR_INVALID_STATE;
    }

    if (!keyboard_is_pin_valid(config->up_key) || !keyboard_is_pin_valid(config->down_key)) {
        return ESP_ERR_INVALID_ARG;
    }

    keyboard->config = *config;
    if (keyboard->config.poll_interval_ms == 0) {
        keyboard->config.poll_interval_ms = 10;
    }

    keyboard->scan = (btn_scan_s){0};
    keyboard->task_handle = NULL;
    keyboard_set_notify_callback(keyboard, NULL, NULL);
    keyboard->app_event_callback = NULL;
    keyboard->app_event_ctx = NULL;
    keyboard->running = false;

    input_mode =
        keyboard->config.active_low ? GPBA02B_IO_INPUT_PULL_HIGH : GPBA02B_IO_INPUT_PULL_LOW;

    if (gpba02b_config_io_input_mode(keyboard->config.up_key.port, keyboard->config.up_key.pin,
                                     input_mode) != ESP_OK) {
        keyboard->initialized = false;
        return ESP_FAIL;
    }

    if (gpba02b_config_io_input_mode(keyboard->config.down_key.port, keyboard->config.down_key.pin,
                                     input_mode) != ESP_OK) {
        keyboard->initialized = false;
        return ESP_FAIL;
    }

    keyboard->initialized = true;
    return ESP_OK;
}

/*
 * brief  : Read raw keyboard level and normalize to logical key-level enum.
 * input  : keyboard - Keyboard object; level - output key-level value.
 * output : true on success; false when read fails.
 * type   : private
 */
static bool keyboard_read_level(const keyboard_t* keyboard, btn_level_e* level) {
    bool up_level = false;
    bool down_level = false;
    bool up_pressed;
    bool down_pressed;

    if (keyboard == NULL || level == NULL || !keyboard->initialized) {
        return false;
    }

    if (gpba02b_read_io(keyboard->config.up_key.port, keyboard->config.up_key.pin, &up_level) !=
        ESP_OK) {
        return false;
    }

    if (gpba02b_read_io(keyboard->config.down_key.port, keyboard->config.down_key.pin,
                        &down_level) != ESP_OK) {
        return false;
    }

    up_pressed = keyboard->config.active_low ? !up_level : up_level;
    down_pressed = keyboard->config.active_low ? !down_level : down_level;

    if (up_pressed && down_pressed) {
        *level = Btn_Level_Both;
    } else if (up_pressed) {
        *level = Btn_Level_Up;
    } else if (down_pressed) {
        *level = Btn_Level_Down;
    } else {
        *level = Btn_Level_None;
    }

    return true;
}

/*
 * brief  : Run one scan cycle of debounce/hold state machine.
 * input  : keyboard - Keyboard object; scan - scan state; ms - poll interval ms.
 * output : btn_status_e representing this cycle's event.
 * type   : private
 */
static btn_status_e keyboard_scan_event(keyboard_t* keyboard, btn_scan_s* scan, uint8_t ms) {
    btn_level_e real_lev;
    btn_status_e status = Btn_Idle;

    if (keyboard == NULL || scan == NULL || ms == 0U || !keyboard->initialized) {
        return Btn_Idle;
    }

    if (!keyboard_read_level(keyboard, &real_lev)) {
        return Btn_Idle;
    }

    switch (scan->step) {
        case Keyboard_Scan_Step_Enter:
            if (real_lev != Btn_Level_None) {
                scan->debounce = 0U;
                scan->hold_period = 0U;
                scan->step = Keyboard_Scan_Step_Debounce;
                scan->prev_level = real_lev;
            }
            break;

        case Keyboard_Scan_Step_Debounce:
            if (real_lev == scan->prev_level) {
                scan->debounce = (uint16_t)(scan->debounce + ms);
                if (scan->debounce >= KEYBOARD_HOLD_MS) {
                    scan->step = Keyboard_Scan_Step_Hold;
                    scan->debounce = 0U;
                    if (scan->prev_level == Btn_Level_Up) {
                        status = Btn_Up_Hold_Enter;
                    } else if (scan->prev_level == Btn_Level_Down) {
                        status = Btn_Down_Hold_Enter;
                    } else if (scan->prev_level == Btn_Level_Both) {
                        status = Btn_Both_Hold_Enter;
                    }
                }
            } else {
                if (real_lev == Btn_Level_None) {
                    if (scan->debounce >= KEYBOARD_CLICK_DEBOUNCE_MS) {
                        if (scan->prev_level == Btn_Level_Up) {
                            status = Btn_Up_Click;
                        } else if (scan->prev_level == Btn_Level_Down) {
                            status = Btn_Down_Click;
                        } else if (scan->prev_level == Btn_Level_Both) {
                            status = Btn_Both_Click;
                        }
                    }

                    scan->step = Keyboard_Scan_Step_Enter;
                    scan->debounce = 0U;
                    scan->hold_period = 0U;
                } else if ((scan->prev_level != Btn_Level_Both) && (real_lev == Btn_Level_Both)) {
                    scan->prev_level = Btn_Level_Both;
                    scan->debounce = 0U;
                } else if ((scan->prev_level == Btn_Level_Both) && (real_lev != Btn_Level_Both)) {
                    if (scan->debounce >= KEYBOARD_CLICK_DEBOUNCE_MS) {
                        status = Btn_Both_Click;
                        scan->step = Keyboard_Scan_Step_Hold;
                        scan->debounce = 0U;
                        scan->hold_period = 0U;
                    } else {
                        scan->prev_level = real_lev;
                        scan->debounce = 0U;
                    }
                } else {
                    scan->prev_level = real_lev;
                    scan->debounce = 0U;
                }
            }
            break;

        case Keyboard_Scan_Step_Hold:
            if (real_lev == Btn_Level_None) {
                scan->debounce = (uint16_t)(scan->debounce + ms);
                if (scan->debounce >= KEYBOARD_RELEASE_MS) {
                    scan->step = Keyboard_Scan_Step_Enter;
                    scan->debounce = 0U;
                    scan->hold_period = 0U;
                }
            } else {
                scan->debounce = 0U;
                scan->hold_period = (uint16_t)(scan->hold_period + ms);
                if (scan->hold_period >= KEYBOARD_HOLD_MS) {
                    if (scan->prev_level == Btn_Level_Up) {
                        status = Btn_Up_Hold_Continue;
                    } else if (scan->prev_level == Btn_Level_Down) {
                        status = Btn_Down_Hold_Continue;
                    } else if (scan->prev_level == Btn_Level_Both) {
                        status = Btn_Both_Hold_Continue;
                    }
                    scan->hold_period = 0U;
                }
            }
            break;

        default:
            scan->step = Keyboard_Scan_Step_Enter;
            scan->debounce = 0U;
            scan->hold_period = 0U;
            break;
    }

    return status;
}

/*
 * brief  : Poll keyboard state and dispatch notify/app events in task context.
 * input  : arg - Keyboard object pointer.
 * output : None.
 * type   : private
 */
static void keyboard_task_entry(void* arg) {
    keyboard_t* keyboard = (keyboard_t*)arg;

    while (keyboard->running) {
        btn_status_e status = keyboard_scan_event(keyboard, &keyboard->scan,
                                                  (uint8_t)keyboard->config.poll_interval_ms);
        keyboard_dispatch_notify(keyboard, keyboard_status_to_notify(status));
        vTaskDelay(pdMS_TO_TICKS(keyboard->config.poll_interval_ms));
    }

    keyboard->task_handle = NULL;
    vTaskDelete(NULL);
}

/* ==================== public functions ==================== */

/*
 * brief  : Register application event callback for mapped key events.
 * input  : keyboard - Keyboard object; app_event_callback - callback; user_ctx - user context.
 * output : None.
 * type   : public
 */
void keyboard_set_app_event_callback(keyboard_t* keyboard,
                                     keyboard_app_event_callback_t app_event_callback,
                                     void* user_ctx) {
    if (keyboard == NULL) {
        return;
    }

    keyboard->app_event_callback = app_event_callback;
    keyboard->app_event_ctx = user_ctx;
}

/*
 * brief  : Initialize keyboard event router context.
 * input  : router - router output pointer; event_runtime - runtime object;
 *          post_event - event post callback; enabled_flag - enable switch pointer.
 * output : None.
 * type   : public
 */
void keyboard_event_router_init(keyboard_event_router_t* router, void* event_runtime,
                                keyboard_event_post_fn_t post_event, bool* enabled_flag) {
    if (router == NULL) {
        return;
    }

    router->event_runtime = event_runtime;
    router->post_event = post_event;
    router->enabled_flag = enabled_flag;
    router->app_event_callback = NULL;
    router->app_event_ctx = NULL;
}

/*
 * brief  : Set secondary app callback for keyboard event router.
 * input  : router - router pointer; app_event_callback - callback; app_event_ctx - user context.
 * output : None.
 * type   : public
 */
void keyboard_event_router_set_app_callback(keyboard_event_router_t* router,
                                            keyboard_app_event_callback_t app_event_callback,
                                            void* app_event_ctx) {
    if (router == NULL) {
        return;
    }

    router->app_event_callback = app_event_callback;
    router->app_event_ctx = app_event_ctx;
}

/*
 * brief  : Dispatch one key event to desktop queue and optional app callback.
 * input  : key_index/event_type - event payload; user_ctx - keyboard_event_router_t pointer.
 * output : None.
 * type   : public
 */
void keyboard_event_router_callback(uint8_t key_index, uint8_t event_type, void* user_ctx) {
    keyboard_event_router_t* router = (keyboard_event_router_t*)user_ctx;
    bool enabled = true;

    if (router == NULL) {
        return;
    }

    if (router->enabled_flag != NULL) {
        enabled = *router->enabled_flag;
    }

    if (enabled && router->post_event != NULL) {
        if (!router->post_event(router->event_runtime, key_index, event_type)) {
            ESP_LOGW(TAG, "Drop key event: key=%u type=%u", key_index, (unsigned int)event_type);
        }
    }

    if (router->app_event_callback != NULL) {
        router->app_event_callback(key_index, event_type, router->app_event_ctx);
    }
}

/*
 * brief  : Start keyboard singleton service and route events to desktop service.
 * input  : config - optional keyboard config, uses defaults when null.
 * output : ESP_OK on success; error code otherwise.
 * type   : public
 */
esp_err_t keyboard_service_start_for_desktop(const keyboard_config_t* config) {
    esp_err_t err;

    if (g_keyboard_service_started && g_keyboard_service.running) {
        return ESP_OK;
    }

    keyboard_event_router_init(&g_keyboard_service_router, NULL, keyboard_service_post_to_desktop,
                               desktop_service_started_flag());

    err = start_keyboard(&g_keyboard_service, config);
    if (err != ESP_OK) {
        return err;
    }

    keyboard_set_app_event_callback(&g_keyboard_service, keyboard_event_router_callback,
                                    &g_keyboard_service_router);
    g_keyboard_service_started = true;
    return ESP_OK;
}

/*
 * brief  : Set optional app callback for keyboard singleton service.
 * input  : app_event_callback - callback; app_event_ctx - callback context.
 * output : None.
 * type   : public
 */
void keyboard_service_set_app_event_callback(keyboard_app_event_callback_t app_event_callback,
                                             void* app_event_ctx) {
    keyboard_event_router_set_app_callback(&g_keyboard_service_router, app_event_callback,
                                           app_event_ctx);
}

/*
 * brief  : Stop keyboard singleton service.
 * input  : None.
 * output : None.
 * type   : public
 */
void keyboard_service_stop(void) {
    if (!g_keyboard_service_started) {
        return;
    }

    stop_keyboard(&g_keyboard_service);
    g_keyboard_service_started = false;
}

/*
 * brief  : Initialize and start keyboard polling task.
 * input  : keyboard - Keyboard object; config - optional runtime config.
 * output : ESP_OK on success; error code otherwise.
 * type   : public
 */
esp_err_t start_keyboard(keyboard_t* keyboard, const keyboard_config_t* config) {
    keyboard_config_t local_config;
    BaseType_t created;
    esp_err_t err;

    if (keyboard == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (keyboard->running) {
        return ESP_ERR_INVALID_STATE;
    }

    if (config == NULL) {
        keyboard_get_default_config(&local_config);
        config = &local_config;
    }

    err = keyboard_init_obj(keyboard, config);
    if (err != ESP_OK) {
        return err;
    }

    keyboard->scan = (btn_scan_s){0};
    keyboard->running = true;

    created = xTaskCreate(keyboard_task_entry, "keyboard_poll", KEYBOARD_TASK_STACK_SIZE, keyboard,
                          KEYBOARD_TASK_PRIORITY, &keyboard->task_handle);
    if (created != pdPASS) {
        keyboard->running = false;
        keyboard->initialized = false;
        keyboard->task_handle = NULL;
        return ESP_FAIL;
    }

    return ESP_OK;
}

/*
 * brief  : Stop keyboard task and mark keyboard object as uninitialized.
 * input  : keyboard - Keyboard object.
 * output : None.
 * type   : public
 */
void stop_keyboard(keyboard_t* keyboard) {
    TaskHandle_t task_handle;

    if (keyboard == NULL) {
        return;
    }

    keyboard->running = false;
    task_handle = keyboard->task_handle;
    if (task_handle != NULL) {
        vTaskDelete(task_handle);
        keyboard->task_handle = NULL;
    }
    keyboard->initialized = false;
}
