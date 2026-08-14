#pragma once

#include <esp_err.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

#include "service_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
	uint8_t key_index;
	uint8_t event_type;
} service_desktop_key_event_t;

typedef struct {
	int current_service_index;
	int selected_service_index;
	uint64_t last_click_ms[2];
} service_desktop_state_t;

typedef struct {
	void* ctx;
	void (*enter_service)(void* ctx, int service_index);
	void (*enter_desktop)(void* ctx, bool show_notification);
	void (*show_notification)(void* ctx, const char* text, uint32_t duration_ms);
	void (*run_command)(void* ctx, service_command_t command);
} service_desktop_ops_t;

typedef struct {
	QueueHandle_t key_queue;
	TaskHandle_t task_handle;
	service_desktop_ops_t ops;
	service_desktop_state_t state;
	void* desktop_layer;
	void* desktop_tiles[SERVICE_APP_COUNT - 1];
	uint8_t desktop_tile_count;
} service_desktop_runtime_t;

extern const service_item_t g_service_desktop;

void service_desktop_runtime_init(service_desktop_runtime_t* runtime);
esp_err_t service_desktop_task_start(service_desktop_runtime_t* runtime,
									 const service_desktop_ops_t* ops);
bool service_desktop_post_key_event(service_desktop_runtime_t* runtime, uint8_t key_index,
									uint8_t event_type);

int service_desktop_get_count(void);
const service_item_t* service_desktop_get_item(int service_index);
bool service_desktop_is_home(const service_desktop_runtime_t* runtime);

#ifdef __cplusplus
}
#endif
