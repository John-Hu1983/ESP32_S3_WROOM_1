#include "dev_button.h"

enum {
    scan_step_enter = 0,
    scan_step_debounce,
    scan_step_hold,
};

btn_status_e button_scan_state(btn_scan_s* scan, uint8_t ms) {
    uint8_t io;
    btn_level_e real_lev;
    btn_status_e status = Btn_Idle;

    if (scan == NULL || ms == 0U) {
        return Btn_Idle;
    }

    gpba02b_port_read(BUTTON_UP_IO_PORT, &io);
    real_lev = (btn_level_e)((~io) & 0x3u);

    switch (scan->step) {
        case scan_step_enter:
            if (real_lev != Btn_Level_None) {
                scan->debounce = 0U;
                scan->step = scan_step_debounce;
                scan->prev_level = real_lev;
            }
            break;

        case scan_step_debounce:
            if (real_lev == scan->prev_level) {
                scan->debounce += ms;
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
                } else if ((scan->prev_level != Btn_Level_Both) && (real_lev == Btn_Level_Both)) {
                    /* Upgrade single-key press to both-key press when second key joins. */
                    scan->prev_level = Btn_Level_Both;
                    scan->debounce = 0U;
                } else if ((scan->prev_level == Btn_Level_Both) && (real_lev != Btn_Level_Both)) {
                    /* Treat first key release as both-click release edge after valid debounce. */
                    if (scan->debounce >= KEYBOARD_CLICK_DEBOUNCE_MS) {
                        status = Btn_Both_Click;
                        scan->step = scan_step_hold;
                        scan->debounce = 0U;
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
                scan->debounce += ms;
                if (scan->debounce >= KEYBOARD_RELEASE_MS) {
                    scan->step = scan_step_enter;
                    scan->debounce = 0U;
                }
            } else {
                scan->debounce = 0;
                scan->hold_period += ms;
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
    }

    return status;
}
