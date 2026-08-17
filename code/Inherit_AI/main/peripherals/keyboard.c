#include "peripherals/keyboard.h"

static bool keyboard_is_pin_valid(keyboard_key_pin_t pin) {
    return pin.port <= GPBA02B_PORT_C && pin.pin <= 7;
}

void keyboard_get_default_config(keyboard_config_t* config) {
    if (config == NULL) {
        return;
    }

    config->up_key.port = GPBA02B_PORT_A;
    config->up_key.pin = 0;
    config->down_key.port = GPBA02B_PORT_A;
    config->down_key.pin = 1;
    config->active_low = true;
    config->poll_interval_ms = 10;
}

esp_err_t keyboard_init_obj(keyboard_t* keyboard, const keyboard_config_t* config) {
    gpba02b_io_input_mode_t input_mode;

    if (keyboard == NULL || config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!keyboard_is_pin_valid(config->up_key) || !keyboard_is_pin_valid(config->down_key)) {
        return ESP_ERR_INVALID_ARG;
    }

    keyboard->config = *config;
    if (keyboard->config.poll_interval_ms == 0) {
        keyboard->config.poll_interval_ms = 10;
    }

    input_mode = keyboard->config.active_low ? GPBA02B_IO_INPUT_PULL_HIGH
                                             : GPBA02B_IO_INPUT_PULL_LOW;

    if (gpba02b_config_io_input_mode(keyboard->config.up_key.port, keyboard->config.up_key.pin,
                                     input_mode) != ESP_OK) {
        keyboard->initialized = false;
        return ESP_FAIL;
    }

    if (gpba02b_config_io_input_mode(keyboard->config.down_key.port,
                                     keyboard->config.down_key.pin, input_mode) != ESP_OK) {
        keyboard->initialized = false;
        return ESP_FAIL;
    }

    keyboard->initialized = true;
    return ESP_OK;
}

bool keyboard_read_level(const keyboard_t* keyboard, btn_level_e* level) {
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

btn_status_e keyboard_scan_event(keyboard_t* keyboard, btn_scan_s* scan, uint8_t ms) {
    btn_level_e real_lev;
    btn_status_e status = Btn_Idle;

    enum {
        scan_step_enter = 0,
        scan_step_debounce,
        scan_step_hold,
    };

    if (keyboard == NULL || scan == NULL || ms == 0U || !keyboard->initialized) {
        return Btn_Idle;
    }

    if (!keyboard_read_level(keyboard, &real_lev)) {
        return Btn_Idle;
    }

    switch (scan->step) {
        case scan_step_enter:
            if (real_lev != Btn_Level_None) {
                scan->debounce = 0U;
                scan->hold_period = 0U;
                scan->step = scan_step_debounce;
                scan->prev_level = real_lev;
            }
            break;

        case scan_step_debounce:
            if (real_lev == scan->prev_level) {
                scan->debounce = (uint16_t)(scan->debounce + ms);
                if (scan->debounce >= KEYBOARD_HOLD_MS) {
                    scan->step = scan_step_hold;
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

                    scan->step = scan_step_enter;
                    scan->debounce = 0U;
                    scan->hold_period = 0U;
                } else if ((scan->prev_level != Btn_Level_Both) && (real_lev == Btn_Level_Both)) {
                    scan->prev_level = Btn_Level_Both;
                    scan->debounce = 0U;
                } else if ((scan->prev_level == Btn_Level_Both) &&
                           (real_lev != Btn_Level_Both)) {
                    if (scan->debounce >= KEYBOARD_CLICK_DEBOUNCE_MS) {
                        status = Btn_Both_Click;
                        scan->step = scan_step_hold;
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

        case scan_step_hold:
            if (real_lev == Btn_Level_None) {
                scan->debounce = (uint16_t)(scan->debounce + ms);
                if (scan->debounce >= KEYBOARD_RELEASE_MS) {
                    scan->step = scan_step_enter;
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
            scan->step = scan_step_enter;
            scan->debounce = 0U;
            scan->hold_period = 0U;
            break;
    }

    return status;
}
