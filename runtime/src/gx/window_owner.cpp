#include "galaxy/gx/window_owner.h"
#include <future>
#include <stdexcept>
#include <utility>

namespace galaxy::gx {

WindowOwner::~WindowOwner() { stop(); }

HWND WindowOwner::start(WindowOwnerConfig config) {
    if (thread_.joinable()) {
        throw std::logic_error("Win32 window owner already started");
    }
    struct Started { HWND window; DWORD thread; };
    std::promise<Started> ready;
    auto startup = ready.get_future();
    thread_ = std::thread([config = std::move(config), ready = std::move(ready)]() mutable {
        HWND window = CreateWindowExW(
            config.extended_style, config.class_name.c_str(), config.title.c_str(),
            config.style, config.x, config.y, config.width, config.height,
            nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        if (window != nullptr && config.show) {
            ShowWindow(window, SW_SHOWNORMAL);
            UpdateWindow(window);
            SetForegroundWindow(window);
            SetActiveWindow(window);
            SetFocus(window);
        }
        ready.set_value({window, GetCurrentThreadId()});
        if (window == nullptr) return;
        MSG message{};
        while (GetMessageW(&message, nullptr, 0, 0) > 0) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        // DestroyWindow must run on the creating thread, including startup
        // failure cleanup and externally requested shutdown during a modal loop.
        if (IsWindow(window)) DestroyWindow(window);
    });
    Started started{};
    try {
        started = startup.get();
    } catch (...) {
        thread_.join();
        throw;
    }
    thread_id_ = started.thread;
    window_ = started.window;
    if (started.window == nullptr) stop();
    return started.window;
}

void WindowOwner::stop() {
    if (!thread_.joinable()) return;
    if (GetCurrentThreadId() == thread_id_) {
        throw std::logic_error("Win32 window owner cannot join itself");
    }
    // An HWND message is dispatched by nested native loops too. Request
    // normal window cleanup before terminating the outer message loop.
    if (window_ != nullptr) (void)PostMessageW(window_, WM_CLOSE, 0, 0);
    // WM_QUIT also terminates Win32's nested move/size/menu message loops;
    // the outer owner loop then destroys its HWND on the same thread.
    (void)PostThreadMessageW(thread_id_, WM_QUIT, 0, 0);
    thread_.join();
    thread_id_ = 0;
    window_ = nullptr;
}

} // namespace galaxy::gx
