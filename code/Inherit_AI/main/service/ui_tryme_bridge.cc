#include "service/ui_tryme_bridge.h"

#include "application.h"

#include <cstring>

extern "C" bool ui_tryme_mic_monitor_start(void) {
    Application::GetInstance().GetAudioService().EnableMicMonitor(true);
    return true;
}

extern "C" void ui_tryme_mic_monitor_stop(void) {
    Application::GetInstance().GetAudioService().EnableMicMonitor(false);
}

extern "C" bool ui_tryme_mic_monitor_read(int16_t* samples,
                                            uint32_t max_samples,
                                            uint32_t* out_samples,
                                            ui_tryme_mic_snapshot_t* snapshot) {
    MicMonitorSnapshot monitor_snapshot;
    uint32_t copy_samples;

    if (samples == nullptr || out_samples == nullptr || snapshot == nullptr || max_samples == 0) {
        return false;
    }

    if (!Application::GetInstance().GetAudioService().GetMicMonitorSnapshot(monitor_snapshot)) {
        return false;
    }

    copy_samples = monitor_snapshot.sample_count;
    if (copy_samples > max_samples) {
        copy_samples = max_samples;
    }

    std::memcpy(samples, monitor_snapshot.samples.data(), copy_samples * sizeof(samples[0]));

    *out_samples = copy_samples;
    snapshot->min_value = monitor_snapshot.min_value;
    snapshot->max_value = monitor_snapshot.max_value;
    snapshot->avg_abs = monitor_snapshot.avg_abs;
    snapshot->rms = monitor_snapshot.rms;
    snapshot->sample_rate = monitor_snapshot.sample_rate;
    snapshot->sample_count = monitor_snapshot.sample_count;
    snapshot->update_ms = monitor_snapshot.update_ms;
    snapshot->valid = monitor_snapshot.valid;

    return monitor_snapshot.valid;
}
