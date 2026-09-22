#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "esp_system.h"

#include "user/device/dev_servo.h"

typedef void (*cmd_parse_code)(const char* para);
typedef struct {
    const char* cmd;
    cmd_parse_code parse;
} at_cmd_t;

esp_err_t at_cmd_parse(const char* cmd);
#ifdef __cplusplus
}
#endif
