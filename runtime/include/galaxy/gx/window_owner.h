#pragma once

// The owner of the game window: a thread that creates it, runs its message loop and destroys it (Win32), or the
// window the SDL host lends the renderer.

#if !defined(_WIN32)
namespace galaxy::gx {
// The game window's owner on SDL platforms: the host creates the window on its main thread (platform/sdl) and lends
// it to the renderer.
class WindowOwner {
public:
    void* window = nullptr;  // SDL_Window*
};
}  // namespace galaxy::gx
#else

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

// Creates the window on its own thread and runs its message loop there (native modal loops included), independent of
// the game and render threads; stop destroys it on that thread.
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

#endif
