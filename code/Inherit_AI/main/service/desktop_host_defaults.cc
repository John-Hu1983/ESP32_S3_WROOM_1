#include "service/desktop.h"

#include "application.h"
#include "board.h"
#include "display/display.h"

static Display* desktop_default_get_display(void* ctx) {
    Board* board = static_cast<Board*>(ctx);

    if (board == nullptr) {
        return nullptr;
    }

    return board->GetDisplay();
}

static void desktop_default_set_status(void* ctx, const char* status) {
    Display* display;

    if (status == nullptr) {
        return;
    }

    display = desktop_default_get_display(ctx);
    if (display != nullptr) {
        display->SetStatus(status);
    }
}

static void desktop_default_set_prompt(void* ctx, const char* prompt) {
    Display* display = desktop_default_get_display(ctx);

    if (display != nullptr) {
        display->SetChatMessage("system", prompt != nullptr ? prompt : "");
    }
}

static void desktop_default_show_notification(void* ctx, const char* text, uint32_t duration_ms) {
    Display* display;

    if (text == nullptr) {
        return;
    }

    display = desktop_default_get_display(ctx);
    if (display != nullptr) {
        display->ShowNotification(text, duration_ms);
    }
}

static void desktop_default_toggle_chat(void* ctx) {
    (void)ctx;
    Application::GetInstance().ToggleChatState();
}

static void desktop_default_start_listening(void* ctx) {
    (void)ctx;
    Application::GetInstance().StartListening();
}

static void desktop_default_stop_listening(void* ctx) {
    (void)ctx;
    Application::GetInstance().StopListening();
}

static void desktop_default_enter_network_config(void* ctx) {
    Board* board = static_cast<Board*>(ctx);

    if (board != nullptr) {
        board->EnterNetworkConfigMode();
    }
}

extern "C" void desktop_service_fill_default_host_ops(desktop_host_ops_t* host_ops) {
    if (host_ops == nullptr || host_ops->ctx == nullptr) {
        return;
    }

    if (host_ops->set_status == nullptr) {
        host_ops->set_status = desktop_default_set_status;
    }
    if (host_ops->set_prompt == nullptr) {
        host_ops->set_prompt = desktop_default_set_prompt;
    }
    if (host_ops->show_notification == nullptr) {
        host_ops->show_notification = desktop_default_show_notification;
    }
    if (host_ops->toggle_chat == nullptr) {
        host_ops->toggle_chat = desktop_default_toggle_chat;
    }
    if (host_ops->start_listening == nullptr) {
        host_ops->start_listening = desktop_default_start_listening;
    }
    if (host_ops->stop_listening == nullptr) {
        host_ops->stop_listening = desktop_default_stop_listening;
    }
    if (host_ops->enter_network_config == nullptr) {
        host_ops->enter_network_config = desktop_default_enter_network_config;
    }
}