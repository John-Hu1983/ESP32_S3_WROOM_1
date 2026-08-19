#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    int32_t min_value;
    int32_t max_value;
    uint32_t avg_abs;
    uint32_t rms;
    uint32_t sample_rate;
    uint32_t sample_count;
    uint64_t update_ms;
    bool valid;
} ui_tryme_mic_snapshot_t;

#ifdef __cplusplus
extern "C" {
#endif

bool ui_tryme_mic_monitor_start(void);
void ui_tryme_mic_monitor_stop(void);
bool ui_tryme_mic_monitor_read(int16_t* samples,
                               uint32_t max_samples,
                               uint32_t* out_samples,
                               ui_tryme_mic_snapshot_t* snapshot);

#ifdef __cplusplus
}
#endif
