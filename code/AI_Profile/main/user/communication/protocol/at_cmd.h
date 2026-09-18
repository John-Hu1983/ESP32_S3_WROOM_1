#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_err.h"
#include "esp_system.h"
#include "esp_timer.h"

#ifdef __cplusplus
extern "C" {
#endif

// clang-format off
#define AT_CMD_TEXT_MAX_LEN      (192U)
#define AT_CMD_REPLY_MAX_LEN     (192U)
// clang-format on

typedef struct {
    float kp;
    float ki;
    float kd;
} at_cmd_pid_s;

typedef esp_err_t (*at_cmd_handler_fn_t)(const char* input_args,
                                         size_t input_args_len,
                                         void* user_ctx);
typedef void (*at_cmd_send_cb_t)(const char* text, void* user_ctx);

typedef struct {
    const char* cmd;
    at_cmd_handler_fn_t func;
    void* input_ctx;
} at_cmd_s;

void at_cmd_set_send_callback(at_cmd_send_cb_t send_cb, void* user_ctx);
esp_err_t at_cmd_get_pid(at_cmd_pid_s* pid_out);
esp_err_t at_cmd_parse_and_dispatch(const char* str, bool* out_handled);

#ifdef __cplusplus
}
#endif
