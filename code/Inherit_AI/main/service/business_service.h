#pragma once

#include "board.h"
#include "service/service_logic.h"

#include <cstdint>

class Display;

class BusinessServiceLayer {
public:
    static constexpr int kServiceCount = SERVICE_APP_COUNT;

    void Initialize(Display* display);
    bool IsDesktopActive(Display* display) const;
    bool IsAiServiceActive() const;

    // Returns true when the key event is consumed by desktop navigation logic.
    // When a service should be entered, entered_service_index is set to [0, kServiceCount).
    bool HandleDesktopKey(uint8_t key_index, BoardKeyEventType event_type, Display* display,
                          int* entered_service_index);

    // Returns true if a dual-click gesture is detected and caller should return to desktop.
    bool HandleDualClickExit(uint8_t key_index, BoardKeyEventType event_type);

    service_key_result_t HandleServiceKey(uint8_t key_index, BoardKeyEventType event_type,
                                          Display* display);

    void EnterService(int service_index, Display* display);
    void EnterDesktop(Display* display, bool show_notification = true);

private:
    static constexpr uint8_t kPrimaryKey = 0;
    static constexpr uint8_t kSecondaryKey = 1;
    static constexpr uint32_t kDualClickWindowMs = 260;

    int current_service_index_ = -1;  // -1 means desktop layer
    uint64_t last_click_ms_[2] = {0, 0};

    static uint64_t GetNowMs();
};
