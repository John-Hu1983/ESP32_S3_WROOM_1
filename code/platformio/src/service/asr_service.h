#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "esp_err.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_partition.h"
#include "esp_wifi.h"

#include "service/system_service.h"
#include "user_config.h"

#if __has_include("esp_mn_iface.h") && __has_include("esp_mn_models.h")
#define ASR_SERVICE_HAVE_ESP_SR 1
#include "esp_mn_iface.h"
#include "esp_mn_models.h"
#else
#define ASR_SERVICE_HAVE_ESP_SR 0
#endif

#define ASR_SERVICE_TEXT_MAX_LEN 96U

typedef enum
{
    ASR_MODE_OFFLINE = 0,
    ASR_MODE_CLOUD = 1,
} asr_mode_e;

typedef struct
{
    asr_mode_e mode;
    esp_err_t ret;
    char text[ASR_SERVICE_TEXT_MAX_LEN];
} asr_output_s;

/* Initialize ASR service runtime and optional offline model state. */
esp_err_t asr_service_init(void);
/* Recognize one 16kHz mono PCM segment and auto-select cloud/offline mode. */
esp_err_t asr_service_recognize_pcm16(const int16_t *pcm_data,
                                      size_t sample_count,
                                      asr_output_s *output);
/* Convert ASR mode enum to readable name. */
const char *asr_service_mode_name(asr_mode_e mode);
