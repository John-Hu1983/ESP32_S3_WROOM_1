#include "asr_service.h"

#include <ctype.h>

#include "freertos/FreeRTOS.h"

#if ASR_SERVICE_HAVE_ESP_SR && __has_include("esp_process_sdkconfig.h")
#include "esp_process_sdkconfig.h"
#define ASR_SERVICE_HAVE_MN_SDKCONFIG 1
#else
#define ASR_SERVICE_HAVE_MN_SDKCONFIG 0
#endif

#define TAG "ASR_SERVICE"

#define ASR_HTTP_RX_BUF_LEN 192U

static bool s_inited = false;

#if ASR_SERVICE_HAVE_ESP_SR
static esp_mn_iface_t *s_mn_iface = NULL;
static model_iface_data_t *s_mn_model = NULL;
static int s_mn_chunk_samples = 0;
#endif

/*
 * brief: Copy source string into fixed destination buffer and ensure NUL tail.
 * input: dst - destination buffer; dst_size - destination capacity; src - source text.
 * output: None.
 */
static void _asr_copy_text(char *dst, size_t dst_size, const char *src)
{
    if ((dst == NULL) || (dst_size == 0U))
    {
        return;
    }

    if (src == NULL)
    {
        dst[0] = '\0';
        return;
    }

    (void)snprintf(dst, dst_size, "%s", src);
}

/*
 * brief: Trim leading and trailing ASCII whitespace in place.
 * input: text - mutable text buffer.
 * output: None.
 */
static void _asr_trim_ascii_space(char *text)
{
    char *start;
    char *end;

    if ((text == NULL) || (text[0] == '\0'))
    {
        return;
    }

    start = text;
    while ((*start != '\0') && isspace((unsigned char)*start))
    {
        start++;
    }

    if (start != text)
    {
        (void)memmove(text, start, strlen(start) + 1U);
    }

    end = text + strlen(text);
    while ((end > text) && isspace((unsigned char)*(end - 1)))
    {
        end--;
    }
    *end = '\0';
}

#if ASR_SERVICE_HAVE_ESP_SR
/*
 * brief: Resolve selected MultiNet model name from sdkconfig symbols.
 * input: None.
 * output: model name string, or NULL when no offline model is selected.
 */
static const char *_asr_get_multinet_model_name(void)
{
#if defined(CONFIG_SR_MN_CN_MULTINET7_AC_QUANT)
    return "mn7_cn_ac";
#elif defined(CONFIG_SR_MN_CN_MULTINET7_QUANT)
    return "mn7_cn";
#elif defined(CONFIG_SR_MN_CN_MULTINET6_AC_QUANT)
    return "mn6_cn_ac";
#elif defined(CONFIG_SR_MN_CN_MULTINET6_QUANT)
    return "mn6_cn";
#elif defined(CONFIG_SR_MN_CN_MULTINET5_RECOGNITION_QUANT8)
    return "mn5q8_cn";
#elif defined(CONFIG_SR_MN_EN_MULTINET7_QUANT)
    return "mn7_en";
#elif defined(CONFIG_SR_MN_EN_MULTINET6_QUANT)
    return "mn6_en";
#elif defined(CONFIG_SR_MN_EN_MULTINET5_SINGLE_RECOGNITION_QUANT8)
    return "mn5q8_en";
#elif defined(MULTINET_MODEL_NAME)
    if ((MULTINET_MODEL_NAME[0] != '\0') && (strcmp(MULTINET_MODEL_NAME, "NULL") != 0))
    {
        return MULTINET_MODEL_NAME;
    }
    return NULL;
#else
    return NULL;
#endif
}

/*
 * brief: Check whether sdkconfig command list should be applied to MultiNet.
 * input: None.
 * output: true when command graph update is supported by selected model.
 */
static bool _asr_need_sdkconfig_command_update(void)
{
#if defined(CONFIG_SR_MN_CN_MULTINET2_SINGLE_RECOGNITION) || \
    defined(CONFIG_SR_MN_CN_MULTINET5_RECOGNITION_QUANT8) || \
    defined(CONFIG_SR_MN_EN_MULTINET5_SINGLE_RECOGNITION_QUANT8)
    return true;
#else
    return false;
#endif
}
#endif

/*
 * brief: Parse a simple JSON field value from response text when available.
 * input: src - source response text; key - JSON field key; out/out_size - parsed output.
 * output: true when key string is parsed successfully; otherwise false.
 */
static bool _asr_json_extract_string(const char *src,
                                     const char *key,
                                     char *out,
                                     size_t out_size)
{
    char pattern[64];
    const char *p;
    const char *q;
    size_t len;

    if ((src == NULL) || (key == NULL) || (out == NULL) || (out_size < 2U))
    {
        return false;
    }

    (void)snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    p = strstr(src, pattern);
    if (p == NULL)
    {
        return false;
    }

    p = strchr(p + strlen(pattern), ':');
    if (p == NULL)
    {
        return false;
    }

    p++;
    while ((*p != '\0') && ((*p == ' ') || (*p == '\t') || (*p == '\r') || (*p == '\n')))
    {
        p++;
    }

    if (*p != '"')
    {
        return false;
    }
    p++;

    q = strchr(p, '"');
    if (q == NULL)
    {
        return false;
    }

    len = (size_t)(q - p);
    if (len >= out_size)
    {
        len = out_size - 1U;
    }

    memcpy(out, p, len);
    out[len] = '\0';
    return true;
}

/*
 * brief: Read current network connectivity from system service plus wifi driver state.
 * input: None.
 * output: true when network appears connected; otherwise false.
 */
static bool _asr_is_network_connected(void)
{
    system_network_status_t net;
    wifi_ap_record_t ap;

    if (system_service_get_network_state(&net) == ESP_OK)
    {
        if (net.connected)
        {
            return true;
        }
    }

    if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK)
    {
        return true;
    }

    return false;
}

/*
 * brief: Run one cloud ASR request using configured endpoint and return text.
 * input: pcm_data/sample_count - input 16kHz mono PCM; out_text/out_size - output text buffer.
 * output: ESP_OK when text is returned; otherwise error code.
 */
static esp_err_t _asr_cloud_recognize(const int16_t *pcm_data,
                                      size_t sample_count,
                                      char *out_text,
                                      size_t out_size)
{
    esp_http_client_handle_t client;
    esp_http_client_config_t cfg;
    int status_code;
    int body_len;
    int body_read;
    size_t pcm_bytes;
    esp_err_t ret;
    char rx_buf[ASR_HTTP_RX_BUF_LEN];

    if ((pcm_data == NULL) || (sample_count == 0U) || (out_text == NULL) || (out_size == 0U))
    {
        return ESP_ERR_INVALID_ARG;
    }

    if (USER_ASR_CLOUD_ENDPOINT[0] == '\0')
    {
        return ESP_ERR_NOT_SUPPORTED;
    }

    pcm_bytes = sample_count * sizeof(int16_t);
    if (pcm_bytes > INT32_MAX)
    {
        return ESP_ERR_INVALID_SIZE;
    }

    memset(&cfg, 0, sizeof(cfg));
    cfg.url = USER_ASR_CLOUD_ENDPOINT;
    cfg.method = HTTP_METHOD_POST;
    cfg.timeout_ms = USER_ASR_CLOUD_TIMEOUT_MS;

    client = esp_http_client_init(&cfg);
    if (client == NULL)
    {
        return ESP_ERR_NO_MEM;
    }

    (void)esp_http_client_set_header(client, "Content-Type", "application/octet-stream");
    (void)esp_http_client_set_header(client, "X-Audio-Format", "pcm_s16le");
    (void)esp_http_client_set_header(client, "X-Sample-Rate", "16000");
    (void)esp_http_client_set_header(client, "X-Channels", "1");

    if (USER_ASR_CLOUD_API_KEY[0] != '\0')
    {
        (void)esp_http_client_set_header(client, "Authorization", USER_ASR_CLOUD_API_KEY);
    }

    ret = esp_http_client_open(client, (int)pcm_bytes);
    if (ret != ESP_OK)
    {
        esp_http_client_cleanup(client);
        return ret;
    }

    if (esp_http_client_write(client, (const char *)pcm_data, (int)pcm_bytes) < 0)
    {
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return ESP_FAIL;
    }

    body_len = esp_http_client_fetch_headers(client);
    (void)body_len;

    status_code = esp_http_client_get_status_code(client);
    if (status_code != 200)
    {
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return ESP_FAIL;
    }

    memset(rx_buf, 0, sizeof(rx_buf));
    body_read = esp_http_client_read_response(client, rx_buf, (int)(sizeof(rx_buf) - 1U));
    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    if (body_read <= 0)
    {
        return ESP_ERR_NOT_FOUND;
    }

    rx_buf[body_read] = '\0';
    if (!_asr_json_extract_string(rx_buf, "text", out_text, out_size) &&
        !_asr_json_extract_string(rx_buf, "result", out_text, out_size))
    {
        _asr_copy_text(out_text, out_size, rx_buf);
    }

    _asr_trim_ascii_space(out_text);
    if (out_text[0] == '\0')
    {
        return ESP_ERR_NOT_FOUND;
    }

    return ESP_OK;
}

#if ASR_SERVICE_HAVE_ESP_SR
/*
 * brief: Initialize offline MultiNet model when available in build config.
 * input: None.
 * output: ESP_OK when model initialized; otherwise error.
 */
static esp_err_t _asr_offline_init(void)
{
    const char *model_name;
    const esp_partition_t *model_partition;

    model_name = _asr_get_multinet_model_name();
    if (model_name == NULL)
    {
        return ESP_ERR_NOT_SUPPORTED;
    }

    model_partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
                                               ESP_PARTITION_SUBTYPE_ANY,
                                               USER_ASR_MODEL_PARTITION_LABEL);
    if (model_partition == NULL)
    {
        ESP_LOGE(TAG,
                 "model partition '%s' not found; offline ASR unavailable",
                 USER_ASR_MODEL_PARTITION_LABEL);
        return ESP_ERR_NOT_FOUND;
    }

    s_mn_iface = esp_mn_handle_from_name((char *)model_name);
    if (s_mn_iface == NULL)
    {
        return ESP_ERR_NOT_FOUND;
    }

    s_mn_model = s_mn_iface->create(model_name, 2600);
    if (s_mn_model == NULL)
    {
        s_mn_iface = NULL;
        return ESP_ERR_NO_MEM;
    }

    s_mn_chunk_samples = s_mn_iface->get_samp_chunksize(s_mn_model);
    if (s_mn_chunk_samples <= 0)
    {
        s_mn_iface->destroy(s_mn_model);
        s_mn_model = NULL;
        s_mn_iface = NULL;
        return ESP_FAIL;
    }

#if ASR_SERVICE_HAVE_MN_SDKCONFIG
    if (_asr_need_sdkconfig_command_update())
    {
        esp_mn_error_t *mn_error = esp_mn_commands_update_from_sdkconfig(s_mn_iface, s_mn_model);
        if ((mn_error != NULL) && (mn_error->num > 0))
        {
            ESP_LOGW(TAG, "offline command graph has %d invalid phrase(s)", mn_error->num);
        }
    }
#endif

    if (s_mn_iface->print_active_speech_commands != NULL)
    {
        s_mn_iface->print_active_speech_commands(s_mn_model);
    }

    ESP_LOGI(TAG,
             "offline multinet ready: model=%s chunk=%d",
             model_name,
             s_mn_chunk_samples);
    return ESP_OK;
}

/*
 * brief: Run offline command recognition over one PCM segment.
 * input: pcm_data/sample_count - input 16kHz mono PCM; out_text/out_size - output text buffer.
 * output: ESP_OK when phrase is detected; ESP_ERR_NOT_FOUND on no result.
 */
static esp_err_t _asr_offline_recognize(const int16_t *pcm_data,
                                        size_t sample_count,
                                        char *out_text,
                                        size_t out_size)
{
    size_t offset;

    if ((pcm_data == NULL) || (sample_count == 0U) || (out_text == NULL) || (out_size == 0U))
    {
        return ESP_ERR_INVALID_ARG;
    }

    if ((s_mn_iface == NULL) || (s_mn_model == NULL) || (s_mn_chunk_samples <= 0))
    {
        return ESP_ERR_NOT_SUPPORTED;
    }

    _asr_copy_text(out_text, out_size, "");
    if (s_mn_iface->clean != NULL)
    {
        s_mn_iface->clean(s_mn_model);
    }

    for (offset = 0U; (offset + (size_t)s_mn_chunk_samples) <= sample_count; offset += (size_t)s_mn_chunk_samples)
    {
        esp_mn_state_t state;
        esp_mn_results_t *res;

        state = s_mn_iface->detect(s_mn_model, (int16_t *)(pcm_data + offset));
        if ((state != ESP_MN_STATE_DETECTED) && (state != ESP_MN_STATE_TIMEOUT))
        {
            continue;
        }

        res = s_mn_iface->get_results(s_mn_model);
        if ((res != NULL) && (res->string[0] != '\0'))
        {
            _asr_copy_text(out_text, out_size, res->string);
            _asr_trim_ascii_space(out_text);
            if (out_text[0] != '\0')
            {
                return ESP_OK;
            }
        }

        if (state == ESP_MN_STATE_TIMEOUT)
        {
            break;
        }
    }

    return ESP_ERR_NOT_FOUND;
}
#else
static esp_err_t _asr_offline_init(void)
{
    return ESP_ERR_NOT_SUPPORTED;
}

static esp_err_t _asr_offline_recognize(const int16_t *pcm_data,
                                        size_t sample_count,
                                        char *out_text,
                                        size_t out_size)
{
    (void)pcm_data;
    (void)sample_count;
    (void)out_text;
    (void)out_size;
    return ESP_ERR_NOT_SUPPORTED;
}
#endif

esp_err_t asr_service_init(void)
{
    esp_err_t offline_ret;

    if (s_inited)
    {
        return ESP_OK;
    }

    offline_ret = _asr_offline_init();
    if (offline_ret != ESP_OK)
    {
        ESP_LOGW(TAG, "offline init skipped: %d", (int)offline_ret);
    }

    s_inited = true;
    return ESP_OK;
}

esp_err_t asr_service_recognize_pcm16(const int16_t *pcm_data,
                                      size_t sample_count,
                                      asr_output_s *output)
{
    esp_err_t ret;
    bool can_cloud;

    if ((pcm_data == NULL) || (sample_count == 0U) || (output == NULL))
    {
        return ESP_ERR_INVALID_ARG;
    }

    (void)asr_service_init();

    output->mode = ASR_MODE_OFFLINE;
    output->ret = ESP_FAIL;
    _asr_copy_text(output->text, sizeof(output->text), "");

    can_cloud = (USER_ASR_CLOUD_ENDPOINT[0] != '\0') && _asr_is_network_connected();
    if (can_cloud)
    {
        output->mode = ASR_MODE_CLOUD;
        ret = _asr_cloud_recognize(pcm_data, sample_count, output->text, sizeof(output->text));
        output->ret = ret;
        if (ret == ESP_OK)
        {
            return ESP_OK;
        }

        ESP_LOGW(TAG, "cloud ASR failed, fallback offline: %d", (int)ret);
        output->mode = ASR_MODE_OFFLINE;
    }

    ret = _asr_offline_recognize(pcm_data, sample_count, output->text, sizeof(output->text));
    output->ret = ret;
    return ret;
}

const char *asr_service_mode_name(asr_mode_e mode)
{
    if (mode == ASR_MODE_CLOUD)
    {
        return "cloud";
    }

    return "offline";
}
