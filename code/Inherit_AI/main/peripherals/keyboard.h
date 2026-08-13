#pragma once

#include "bsp/gpba02b.h"

#include <esp_err.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <atomic>
#include <cstdint>
#include <functional>

class Keyboard {
public:
    static constexpr uint8_t kKeyCount = 2;
    static constexpr uint8_t kKey0 = 0;
    static constexpr uint8_t kKey1 = 1;

    struct KeyPin {
        Gpba02b::Port port;
        uint8_t pin;
    };

    struct Config {
        KeyPin keys[kKeyCount];
        bool active_low = true;
        uint32_t poll_interval_ms = 10;
        uint32_t debounce_ms = 30;
        uint32_t long_press_ms = 700;
    };

    explicit Keyboard(const Config& config);
    ~Keyboard();

    void OnPressDown(uint8_t key_index, std::function<void()> callback);
    void OnPressUp(uint8_t key_index, std::function<void()> callback);
    void OnClick(uint8_t key_index, std::function<void()> callback);
    void OnLongPress(uint8_t key_index, std::function<void()> callback);

    esp_err_t Start();
    void Stop();

private:
    struct KeyCallbacks {
        std::function<void()> on_press_down;
        std::function<void()> on_press_up;
        std::function<void()> on_click;
        std::function<void()> on_long_press;
    };

    struct KeyState {
        bool valid = false;
        bool raw_pressed = false;
        bool stable_pressed = false;
        uint64_t raw_changed_ms = 0;
        uint64_t pressed_since_ms = 0;
        bool long_press_fired = false;
        KeyCallbacks callbacks;
    };

    Config config_;
    KeyState key_states_[kKeyCount];
    TaskHandle_t task_handle_ = nullptr;
    std::atomic<bool> running_{false};

    static void PollTaskEntry(void* arg);
    void PollTaskLoop();
    void PollOnce();

    bool IsKeyIndexValid(uint8_t key_index) const;
    bool IsPinValid(const KeyPin& key_pin) const;
    bool ReadPressed(uint8_t key_index, bool* pressed) const;
    void HandleStableChange(uint8_t key_index, bool pressed, uint64_t now_ms);

    static uint64_t GetNowMs();
};
