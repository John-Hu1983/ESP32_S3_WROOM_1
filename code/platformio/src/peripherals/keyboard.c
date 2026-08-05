#include "keyboard.h"

#define TAG "KEYBOARD"

/*
 * brief: Initialize keyboard GPIO pins on GPBA02B as pull-up inputs.
 * input: None.
 * output: ESP_OK on success; otherwise propagated GPBA02B configuration error.
 */
esp_err_t keyboard_init_obj(void)
{
    USER_RETURN_ON_ERROR(gpba02b_pin_set_mode(BUTTON_UP_IO_PORT, BUTTON_UP_IO_PIN,
                                              GPBA02B_PIN_MODE_INPUT_PULLUP),
                         TAG, "gpba02b_pin_set_mode BUTTON_UP failed");
    USER_RETURN_ON_ERROR(gpba02b_pin_set_mode(BUTTON_DOWN_IO_PORT, BUTTON_DOWN_IO_PIN,
                                              GPBA02B_PIN_MODE_INPUT_PULLUP),
                         TAG, "gpba02b_pin_set_mode BUTTON_DOWN failed");
    return ESP_OK;
}

/*
 * brief: Scan current key levels and emit debounced click/hold events.
 * input: scan - key scan state context; ms - scan period in milliseconds.
 * output: Button event status for this scan cycle.
 */
btn_status_e keyboard_scan_event(btn_scan_s *scan, uint8_t ms)
{
    uint8_t io;
    btn_level_e real_lev;
    btn_status_e status = Btn_Idle;

    enum
    {
        scan_step_enter = 0,
        scan_step_debounce,
        scan_step_hold,
    };

    if (scan == NULL || ms == 0U)
    {
        return Btn_Idle;
    }

    gpba02b_port_read(BUTTON_UP_IO_PORT, &io);
    real_lev = (btn_level_e)((~io) & 0x3u);

    switch (scan->step)
    {
    case scan_step_enter:
        if (real_lev != Btn_Level_None)
        {
            scan->debounce = 0U;
            scan->step = scan_step_debounce;
            scan->prev_level = real_lev;
        }
        break;

    case scan_step_debounce:
        if (real_lev == scan->prev_level)
        {
            scan->debounce += ms;
            if (scan->debounce >= KEYBOARD_HOLD_MS)
            {
                scan->step = scan_step_hold;
                scan->debounce = 0U;
                if (scan->prev_level == Btn_Level_Up)
                {
                    status = Btn_Up_Hold;
                }
                else if (scan->prev_level == Btn_Level_Down)
                {
                    status = Btn_Down_Hold;
                }
                else if (scan->prev_level == Btn_Level_Both)
                {
                    status = Btn_Both_Hold;
                }
            }
        }
        else
        {
            if (real_lev == Btn_Level_None)
            {
                if (scan->debounce >= KEYBOARD_CLICK_DEBOUNCE_MS)
                {
                    if (scan->prev_level == Btn_Level_Up)
                    {
                        status = Btn_Up_Click;
                    }
                    else if (scan->prev_level == Btn_Level_Down)
                    {
                        status = Btn_Down_Click;
                    }
                    else if (scan->prev_level == Btn_Level_Both)
                    {
                        status = Btn_Both_Click;
                    }
                }

                scan->step = scan_step_enter;
                scan->debounce = 0U;
            }
            else if ((scan->prev_level != Btn_Level_Both) && (real_lev == Btn_Level_Both))
            {
                /* Upgrade single-key press to both-key press when second key joins. */
                scan->prev_level = Btn_Level_Both;
                scan->debounce = 0U;
            }
            else if ((scan->prev_level == Btn_Level_Both) && (real_lev != Btn_Level_Both))
            {
                /* Treat first key release as both-click release edge after valid debounce. */
                if (scan->debounce >= KEYBOARD_CLICK_DEBOUNCE_MS)
                {
                    status = Btn_Both_Click;
                    scan->step = scan_step_hold;
                    scan->debounce = 0U;
                }
                else
                {
                    scan->prev_level = real_lev;
                    scan->debounce = 0U;
                }
            }
            else
            {
                scan->prev_level = real_lev;
                scan->debounce = 0U;
            }
        }
        break;

    case scan_step_hold:
        if (real_lev == Btn_Level_None)
        {
            scan->debounce += ms;
            if (scan->debounce >= KEYBOARD_RELEASE_MS)
            {
                scan->step = scan_step_enter;
                scan->debounce = 0U;
            }
        }
        else
        {
            scan->debounce = 0;
        }
        break;
    }

    return status;
}
