#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <string>
#include <thread>

namespace galaxy::gx {

struct WindowOwnerConfig {
    std::wstring class_name;
    std::wstring title;
    DWORD style{};
    DWORD extended_style{};
    int x{};
    int y{};
    int width{};
    int height{};
    bool show{};
};

// Owns HWND creation, native modal loops and destruction, independent of guest
// execution and GPU recording. The consumer calls start/stop.
class WindowOwner {
public:
    WindowOwner() = default;
    ~WindowOwner();
    WindowOwner(const WindowOwner&) = delete;
    WindowOwner& operator=(const WindowOwner&) = delete;
    [[nodiscard]] HWND start(WindowOwnerConfig config);
    void stop();
private:
    std::thread thread_;
    DWORD thread_id_{};
    HWND window_{};
};

} // namespace galaxy::gx
