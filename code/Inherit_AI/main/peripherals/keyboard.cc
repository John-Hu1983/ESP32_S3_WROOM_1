#include "peripherals/keyboard.h"

#include <esp_log.h>
#include <esp_timer.h>

#define TAG "Keyboard"

static constexpr uint32_t kMinPollIntervalMs = 5;
static constexpr uint32_t kDefaultTaskStackSize = 3072;
static constexpr UBaseType_t kDefaultTaskPriority = 2;

Keyboard::Keyboard(const Config& config) : config_(config) {
    if (config_.poll_interval_ms < kMinPollIntervalMs) {
        config_.poll_interval_ms = kMinPollIntervalMs;
    }

    if (config_.debounce_ms == 0) {
        config_.debounce_ms = 1;
    }

    for (uint8_t i = 0; i < kKeyCount; ++i) {
        key_states_[i].valid = IsPinValid(config_.keys[i]);
    }
}

Keyboard::~Keyboard() { Stop(); }

void Keyboard::OnPressDown(uint8_t key_index, std::function<void()> callback) {
    if (!IsKeyIndexValid(key_index)) {
        return;
    }
    key_states_[key_index].callbacks.on_press_down = callback;
}

void Keyboard::OnPressUp(uint8_t key_index, std::function<void()> callback) {
    if (!IsKeyIndexValid(key_index)) {
        return;
    }
    key_states_[key_index].callbacks.on_press_up = callback;
}

void Keyboard::OnClick(uint8_t key_index, std::function<void()> callback) {
    if (!IsKeyIndexValid(key_index)) {
        return;
    }
    key_states_[key_index].callbacks.on_click = callback;
}

void Keyboard::OnLongPress(uint8_t key_index, std::function<void()> callback) {
    if (!IsKeyIndexValid(key_index)) {
        return;
    }
    key_states_[key_index].callbacks.on_long_press = callback;
}

esp_err_t Keyboard::Start() {
    if (running_.load()) {
        return ESP_OK;
    }

    bool has_valid_key = false;
    for (uint8_t i = 0; i < kKeyCount; ++i) {
        if (!key_states_[i].valid) {
            continue;
        }

        has_valid_key = true;
        auto pull_mode = config_.active_low ? Gpba02b::kIoInputPullHigh : Gpba02b::kIoInputPullLow;
        esp_err_t err = Gpba02b::Instance().config_io_input(config_.keys[i].port,
                                                            config_.keys[i].pin, pull_mode);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to configure key %u input: %s", i, esp_err_to_name(err));
            return err;
        }
    }

    if (!has_valid_key) {
        ESP_LOGW(TAG, "No valid keyboard keys configured");
        return ESP_ERR_INVALID_ARG;
    }

    uint64_t now_ms = GetNowMs();
    for (uint8_t i = 0; i < kKeyCount; ++i) {
        if (!key_states_[i].valid) {
            continue;
        }

        bool pressed = false;
        if (!ReadPressed(i, &pressed)) {
            pressed = false;
        }

        key_states_[i].raw_pressed = pressed;
        key_states_[i].stable_pressed = pressed;
        key_states_[i].raw_changed_ms = now_ms;
        key_states_[i].pressed_since_ms = pressed ? now_ms : 0;
        key_states_[i].long_press_fired = false;
    }

    running_.store(true);
    BaseType_t created = xTaskCreate(PollTaskEntry, "keyboard_poll", kDefaultTaskStackSize, this,
                                     kDefaultTaskPriority, &task_handle_);
    if (created != pdPASS) {
        running_.store(false);
        task_handle_ = nullptr;
        ESP_LOGE(TAG, "Failed to create keyboard polling task");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Keyboard polling started");
    return ESP_OK;
}

void Keyboard::Stop() {
    if (!running_.load()) {
        return;
    }

    running_.store(false);

    for (int i = 0; i < 50 && task_handle_ != nullptr; ++i) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    if (task_handle_ != nullptr) {
        vTaskDelete(task_handle_);
        task_handle_ = nullptr;
    }

    ESP_LOGI(TAG, "Keyboard polling stopped");
}

void Keyboard::PollTaskEntry(void* arg) {
    auto* keyboard = static_cast<Keyboard*>(arg);
    keyboard->PollTaskLoop();
}

void Keyboard::PollTaskLoop() {
    while (running_.load()) {
        PollOnce();
        vTaskDelay(pdMS_TO_TICKS(config_.poll_interval_ms));
    }

    task_handle_ = nullptr;
    vTaskDelete(nullptr);
}

void Keyboard::PollOnce() {
    uint64_t now_ms = GetNowMs();

    for (uint8_t i = 0; i < kKeyCount; ++i) {
        if (!key_states_[i].valid) {
            continue;
        }

        bool pressed = false;
        if (!ReadPressed(i, &pressed)) {
            continue;
        }

        if (pressed != key_states_[i].raw_pressed) {
            key_states_[i].raw_pressed = pressed;
            key_states_[i].raw_changed_ms = now_ms;
        }

        if (key_states_[i].raw_pressed != key_states_[i].stable_pressed) {
            if ((now_ms - key_states_[i].raw_changed_ms) >= config_.debounce_ms) {
                key_states_[i].stable_pressed = key_states_[i].raw_pressed;
                HandleStableChange(i, key_states_[i].stable_pressed, now_ms);
            }
        }

        if (key_states_[i].stable_pressed && !key_states_[i].long_press_fired &&
            config_.long_press_ms > 0 &&
            (now_ms - key_states_[i].pressed_since_ms) >= config_.long_press_ms) {
            key_states_[i].long_press_fired = true;
            if (key_states_[i].callbacks.on_long_press) {
                key_states_[i].callbacks.on_long_press();
            }
        }
    }
}

bool Keyboard::IsKeyIndexValid(uint8_t key_index) const { return key_index < kKeyCount; }

bool Keyboard::IsPinValid(const KeyPin& key_pin) const {
    return key_pin.port <= Gpba02b::kPortC && key_pin.pin <= 7;
}

bool Keyboard::ReadPressed(uint8_t key_index, bool* pressed) const {
    if (pressed == nullptr || !IsKeyIndexValid(key_index) || !key_states_[key_index].valid) {
        return false;
    }

    bool level = false;
    esp_err_t err = Gpba02b::Instance().read_io(config_.keys[key_index].port,
                                                config_.keys[key_index].pin, &level);
    if (err != ESP_OK) {
        return false;
    }

    *pressed = config_.active_low ? !level : level;
    return true;
}

void Keyboard::HandleStableChange(uint8_t key_index, bool pressed, uint64_t now_ms) {
    if (!IsKeyIndexValid(key_index)) {
        return;
    }

    auto& state = key_states_[key_index];
    if (pressed) {
        state.pressed_since_ms = now_ms;
        state.long_press_fired = false;
        if (state.callbacks.on_press_down) {
            state.callbacks.on_press_down();
        }
        return;
    }

    if (state.callbacks.on_press_up) {
        state.callbacks.on_press_up();
    }

    if (!state.long_press_fired && state.callbacks.on_click) {
        state.callbacks.on_click();
    }
}

uint64_t Keyboard::GetNowMs() { return static_cast<uint64_t>(esp_timer_get_time() / 1000); }
