#pragma once

#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_freertos_hooks.h"

#include "app_status_bar.h"

#if defined(portNUM_PROCESSORS)
#define APPS_IDLE_CORE_COUNT portNUM_PROCESSORS
#elif defined(configNUMBER_OF_CORES)
#define APPS_IDLE_CORE_COUNT configNUMBER_OF_CORES
#else
#define APPS_IDLE_CORE_COUNT 1U
#endif

typedef struct
{
	bool connected;
	int8_t rssi_dbm;
} network_status_t;

/* Start idle hooks and the 1000ms status sampling task. */
esp_err_t apps_idle_task_start(void);
/* Update network state used by status-bar snapshots and immediate display. */
void apps_idle_task_set_network_state(bool connected, int8_t rssi_dbm);
