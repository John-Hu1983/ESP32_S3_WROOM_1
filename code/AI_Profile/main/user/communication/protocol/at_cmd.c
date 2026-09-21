#include "at_cmd.h"

#define TAG "AT_CMD"

static float s_pid_kp = 0.0f;
static float s_pid_ki = 0.0f;
static float s_pid_kd = 0.0f;
static int32_t s_set_point_angle = 0;
static int32_t s_set_point_adc = 0;

static bool _atcmd_is_space(char c) {
    return (c == ' ') || (c == '\t') || (c == '\r') || (c == '\n');
}

static const char* _atcmd_skip_spaces(const char* p) {
    if (p == NULL) {
        return NULL;
    }

    while ((*p != '\0') && _atcmd_is_space(*p)) {
        p++;
    }

    return p;
}

static void _atcmd_reboot_mcu(const char* str) {
    (void)str;
    esp_restart();
}

static void _atcmd_config_pid(const char* str) {
    const char* p = NULL;
    char* end = NULL;
    float kp = 0.0f;
    float ki = 0.0f;
    float kd = 0.0f;

    if (str == NULL) {
        return;
    }

    p = _atcmd_skip_spaces(str);

    kp = strtof(p, &end);
    if (end == p) {
        return;
    }
    p = end;

    p = _atcmd_skip_spaces(p);
    if (*p != ',') {
        return;
    }
    p++;

    p = _atcmd_skip_spaces(p);

    ki = strtof(p, &end);
    if (end == p) {
        return;
    }
    p = end;

    p = _atcmd_skip_spaces(p);
    if (*p != ',') {
        return;
    }
    p++;

    p = _atcmd_skip_spaces(p);

    kd = strtof(p, &end);
    if (end == p) {
        return;
    }
    p = end;

    p = _atcmd_skip_spaces(p);
    if (*p != '\0') {
        return;
    }

    s_pid_kp = kp;
    s_pid_ki = ki;
    s_pid_kd = kd;
    ESP_LOGI(TAG,
             "PID configured: Kp=%.3f, Ki=%.3f, Kd=%.3f",
             s_pid_kp,
             s_pid_ki,
             s_pid_kd);
}

static void _atcmd_set_point(const char* str) {
    const char* p = NULL;
    char* end = NULL;
    long angle = 0;
    long adc = 0;

    if (str == NULL) {
        return;
    }

    p = _atcmd_skip_spaces(str);

    angle = strtol(p, &end, 10);
    if (end == p) {
        return;
    }
    p = end;

    p = _atcmd_skip_spaces(p);
    if (*p != ',') {
        return;
    }
    p++;

    p = _atcmd_skip_spaces(p);

    adc = strtol(p, &end, 10);
    if (end == p) {
        return;
    }
    p = end;

    p = _atcmd_skip_spaces(p);
    if (*p != '\0') {
        return;
    }

    s_set_point_angle = (int32_t)angle;
    s_set_point_adc = (int32_t)adc;

    ESP_LOGI(TAG,
             "Set point configured: angle=%ld, adc=%ld",
             (long)s_set_point_angle,
             (long)s_set_point_adc);
}

const at_cmd_t at_cmds[] = {
    { "AT+REBOOT:", _atcmd_reboot_mcu },
    { "AT+PID:", _atcmd_config_pid },
    { "AT+SETPOINT:", _atcmd_set_point },
};

const size_t at_cmds_count = sizeof(at_cmds) / sizeof(at_cmds[0]);

/*
 * brief : Parse AT command and execute corresponding handler.
 * input : cmd - The AT command string to be parsed.
 * return: ESP_OK if the command is successfully parsed and executed,
 *         ESP_ERR_NOT_FOUND if the command is not found.
 * type  : public
 * theme : AT command parsing and execution.
 */
esp_err_t at_cmd_parse(const char* cmd) {
    const char* text = NULL;
    const char* para = NULL;
    size_t text_len = 0U;
    size_t i = 0U;

    if (cmd == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    text = cmd;
    text = _atcmd_skip_spaces(cmd);

    text_len = strlen(text);
    while ((text_len > 0U) && _atcmd_is_space(text[text_len - 1U])) {
        text_len--;
    }

    if (text_len == 0U) {
        return ESP_ERR_INVALID_ARG;
    }

    for (i = 0U; i < at_cmds_count; i++) {
        const char* prefix = NULL;
        size_t cmd_len = 0U;
        size_t j = 0U;

        if ((at_cmds[i].cmd == NULL) || (at_cmds[i].parse == NULL)) {
            continue;
        }

        prefix = at_cmds[i].cmd;
        cmd_len = strlen(prefix);
        if (text_len < cmd_len) {
            continue;
        }

        for (j = 0U; j < cmd_len; j++) {
            char left = text[j];
            char right = prefix[j];

            if ((left >= 'a') && (left <= 'z')) {
                left = (char)(left - ('a' - 'A'));
            }
            if ((right >= 'a') && (right <= 'z')) {
                right = (char)(right - ('a' - 'A'));
            }

            if (left != right) {
                break;
            }
        }

        if (j == cmd_len) {
            para = text + cmd_len;
            at_cmds[i].parse(para);
            return ESP_OK;
        }
    }

    return ESP_ERR_NOT_FOUND;
}
