#include "at_cmd.h"

typedef struct {
    at_cmd_send_cb_t send_cb;
    void* send_ctx;
    at_cmd_pid_s pid;
    esp_timer_handle_t reboot_timer;
    bool reboot_timer_ready;
} at_cmd_runtime_s;

static at_cmd_runtime_s s_at_cmd_runtime = {
    .send_cb = NULL,
    .send_ctx = NULL,
    .pid = {
        .kp = 20.0f,
        .ki = 5.0f,
        .kd = 1.2f,
    },
    .reboot_timer = NULL,
    .reboot_timer_ready = false,
};

static bool _at_is_space(char c) {
    return (c == ' ') || (c == '\t') || (c == '\r') || (c == '\n');
}

static bool _at_is_digit(char c) {
    return (c >= '0') && (c <= '9');
}

static char _at_to_upper(char c) {
    if ((c >= 'a') && (c <= 'z')) {
        return (char)(c - ('a' - 'A'));
    }
    return c;
}

static void _at_trim(char* text) {
    size_t len = 0U;
    size_t start = 0U;
    size_t end = 0U;

    if (text == NULL) {
        return;
    }

    len = strlen(text);
    while ((start < len) && _at_is_space(text[start])) {
        start++;
    }
    while ((len > start) && _at_is_space(text[len - 1U])) {
        len--;
    }

    end = len - start;
    if (start > 0U) {
        memmove(text, &text[start], end);
    }
    text[end] = '\0';
}

static bool _at_starts_with_at(const char* text) {
    if ((text == NULL) || (strlen(text) < 3U)) {
        return false;
    }

    return (_at_to_upper(text[0]) == 'A') && (_at_to_upper(text[1]) == 'T')
           && (text[2] == '+');
}

static bool _at_match_prefix_ci(const char* text, const char* prefix, size_t* match_len) {
    size_t i = 0U;

    if ((text == NULL) || (prefix == NULL) || (match_len == NULL)) {
        return false;
    }

    while ((prefix[i] != '\0') && (text[i] != '\0')) {
        if (_at_to_upper(text[i]) != _at_to_upper(prefix[i])) {
            return false;
        }
        i++;
    }

    if (prefix[i] != '\0') {
        return false;
    }

    *match_len = i;
    return true;
}

static bool _at_is_cmd_boundary(char c) {
    return (c == '\0') || _at_is_space(c) || (c == ':') || (c == '=') || (c == '?');
}

static const char* _at_skip_prefix_delimiters(const char* text) {
    const char* p = text;

    if (p == NULL) {
        return NULL;
    }

    while ((*p != '\0') && _at_is_space(*p)) {
        p++;
    }

    while ((*p == ':') || (*p == '=') || (*p == '?')) {
        p++;
        while ((*p != '\0') && _at_is_space(*p)) {
            p++;
        }
    }

    return p;
}

static void _at_reply(const char* text) {
    if ((text == NULL) || (text[0] == '\0')) {
        return;
    }

    if (s_at_cmd_runtime.send_cb != NULL) {
        s_at_cmd_runtime.send_cb(text, s_at_cmd_runtime.send_ctx);
    }
}

static bool _at_extract_named_float(const char* args_upper,
                                    const char* key,
                                    float* value_out) {
    const char* p = args_upper;
    size_t key_len = 0U;
    char* end_ptr = NULL;

    if ((args_upper == NULL) || (key == NULL) || (value_out == NULL)) {
        return false;
    }

    key_len = strlen(key);
    while ((p = strstr(p, key)) != NULL) {
        const char* val = p + key_len;

        while ((*val != '\0') && _at_is_space(*val)) {
            val++;
        }
        if ((*val == '=') || (*val == ':')) {
            val++;
        }
        while ((*val != '\0') && _at_is_space(*val)) {
            val++;
        }

        if ((*val == '+') || (*val == '-') || _at_is_digit(*val) || (*val == '.')) {
            float parsed = strtof(val, &end_ptr);
            if ((end_ptr != NULL) && (end_ptr != val)) {
                *value_out = parsed;
                return true;
            }
        }

        p += key_len;
    }

    return false;
}

static esp_err_t _at_parse_pid_args(const char* args,
                                    float* out_kp,
                                    float* out_ki,
                                    float* out_kd) {
    char args_upper[AT_CMD_TEXT_MAX_LEN + 1U] = { 0 };
    size_t args_len = 0U;
    size_t i = 0U;
    bool got_kp = false;
    bool got_ki = false;
    bool got_kd = false;
    float kp = 0.0f;
    float ki = 0.0f;
    float kd = 0.0f;

    if ((args == NULL) || (out_kp == NULL) || (out_ki == NULL) || (out_kd == NULL)) {
        return ESP_ERR_INVALID_ARG;
    }

    args_len = strnlen(args, AT_CMD_TEXT_MAX_LEN);
    for (i = 0U; i < args_len; ++i) {
        args_upper[i] = _at_to_upper(args[i]);
    }
    args_upper[args_len] = '\0';

    got_kp = _at_extract_named_float(args_upper, "KP", &kp);
    got_ki = _at_extract_named_float(args_upper, "KI", &ki);
    got_kd = _at_extract_named_float(args_upper, "KD", &kd);
    if (got_kp && got_ki && got_kd) {
        *out_kp = kp;
        *out_ki = ki;
        *out_kd = kd;
        return ESP_OK;
    }

    if (sscanf(args, " %f , %f , %f", &kp, &ki, &kd) == 3) {
        *out_kp = kp;
        *out_ki = ki;
        *out_kd = kd;
        return ESP_OK;
    }

    return ESP_ERR_INVALID_ARG;
}

static void _at_reboot_timer_cb(void* arg) {
    (void)arg;
    esp_restart();
}

static void _at_schedule_reboot(void) {
    if (!s_at_cmd_runtime.reboot_timer_ready) {
        esp_timer_create_args_t timer_cfg = {
            .callback = _at_reboot_timer_cb,
            .arg = NULL,
            .dispatch_method = ESP_TIMER_TASK,
            .name = "at_reboot",
            .skip_unhandled_events = true,
        };

        if (esp_timer_create(&timer_cfg, &s_at_cmd_runtime.reboot_timer) != ESP_OK) {
            esp_restart();
            return;
        }

        s_at_cmd_runtime.reboot_timer_ready = true;
    }

    if (s_at_cmd_runtime.reboot_timer != NULL) {
        (void)esp_timer_stop(s_at_cmd_runtime.reboot_timer);
        (void)esp_timer_start_once(s_at_cmd_runtime.reboot_timer, 250000U);
    }
}

static esp_err_t _at_cmd_reboot(const char* input_args,
                                size_t input_args_len,
                                void* user_ctx) {
    (void)user_ctx;

    if ((input_args != NULL) && (input_args_len > 0U)) {
        _at_reply("AT+ERR:REBOOT_BAD_FORMAT");
        return ESP_ERR_INVALID_ARG;
    }

    _at_reply("AT+OK:REBOOT_PENDING");
    _at_schedule_reboot();
    return ESP_OK;
}

static esp_err_t _at_cmd_pid(const char* input_args,
                             size_t input_args_len,
                             void* user_ctx) {
    char reply[AT_CMD_REPLY_MAX_LEN] = { 0 };
    float kp = 0.0f;
    float ki = 0.0f;
    float kd = 0.0f;
    esp_err_t ret = ESP_OK;

    (void)user_ctx;

    if ((input_args == NULL) || (input_args_len == 0U)) {
        snprintf(reply,
                 sizeof(reply),
                 "AT+PID: KP=%.4f KI=%.4f KD=%.4f",
                 (double)s_at_cmd_runtime.pid.kp,
                 (double)s_at_cmd_runtime.pid.ki,
                 (double)s_at_cmd_runtime.pid.kd);
        _at_reply(reply);
        return ESP_OK;
    }

    ret = _at_parse_pid_args(input_args, &kp, &ki, &kd);
    if (ret != ESP_OK) {
        _at_reply("AT+ERR:PID_BAD_FORMAT");
        return ret;
    }

    s_at_cmd_runtime.pid.kp = kp;
    s_at_cmd_runtime.pid.ki = ki;
    s_at_cmd_runtime.pid.kd = kd;

    snprintf(reply,
             sizeof(reply),
             "AT+PID: KP=%.4f KI=%.4f KD=%.4f",
             (double)s_at_cmd_runtime.pid.kp,
             (double)s_at_cmd_runtime.pid.ki,
             (double)s_at_cmd_runtime.pid.kd);
    _at_reply(reply);

    return ESP_OK;
}

static const at_cmd_s s_at_cmd_group[] = {
    { .cmd = "AT+REBOOT", .func = _at_cmd_reboot, .input_ctx = NULL },
    { .cmd = "AT+PID", .func = _at_cmd_pid, .input_ctx = NULL },
};

void at_cmd_set_send_callback(at_cmd_send_cb_t send_cb, void* user_ctx) {
    s_at_cmd_runtime.send_cb = send_cb;
    s_at_cmd_runtime.send_ctx = user_ctx;
}

esp_err_t at_cmd_get_pid(at_cmd_pid_s* pid_out) {
    if (pid_out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    *pid_out = s_at_cmd_runtime.pid;
    return ESP_OK;
}

esp_err_t at_cmd_parse_and_dispatch(const char* str, bool* out_handled) {
    char local[AT_CMD_TEXT_MAX_LEN + 1U] = { 0 };
    size_t text_len = 0U;
    size_t i = 0U;

    if (out_handled != NULL) {
        *out_handled = false;
    }

    if (str == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    text_len = strnlen(str, AT_CMD_TEXT_MAX_LEN + 1U);
    if (text_len == 0U) {
        return ESP_ERR_INVALID_ARG;
    }
    if (text_len > AT_CMD_TEXT_MAX_LEN) {
        return ESP_ERR_INVALID_SIZE;
    }

    memcpy(local, str, text_len);
    local[text_len] = '\0';
    _at_trim(local);

    if (!_at_starts_with_at(local)) {
        return ESP_OK;
    }

    if (out_handled != NULL) {
        *out_handled = true;
    }

    for (i = 0U; i < (sizeof(s_at_cmd_group) / sizeof(s_at_cmd_group[0])); ++i) {
        const at_cmd_s* item = &s_at_cmd_group[i];
        size_t cmd_len = 0U;
        const char* args = NULL;
        size_t args_len = 0U;

        if ((item->cmd == NULL) || (item->func == NULL)) {
            continue;
        }

        if (!_at_match_prefix_ci(local, item->cmd, &cmd_len)) {
            continue;
        }

        if (!_at_is_cmd_boundary(local[cmd_len])) {
            continue;
        }

        args = _at_skip_prefix_delimiters(&local[cmd_len]);
        args_len = (args != NULL) ? strlen(args) : 0U;
        return item->func(args, args_len, item->input_ctx);
    }

    _at_reply("AT+ERR:UNKNOWN_CMD");
    return ESP_ERR_NOT_FOUND;
}
