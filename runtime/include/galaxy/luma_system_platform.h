// SPDX-License-Identifier: GPL-3.0-only
// LumaCore's system services, host side (contract services-system): the tables `system`, `system.clipboard` and
// `system.links` (luma_system.cpp) and what a platform layer gives them (section 7). The platform layer is a table the
// window layer installs when it makes the game's window, so a host with no window links with none: then the clipboard
// and links answer LUMA_E_ABSENT and the display is the empty snapshot.
#pragma once

#include <luma_service_provider.h>
#include <luma_system.h>

#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace galaxy::luma_system {

// Pixels as the services carry them (section 1 item 4): RGBA, 8 bits a channel, straight alpha, rows top to bottom.
// `encoded`: a PNG, BMP or JPEG file's bytes, decoded by luma_system.cpp when `rgba` is empty.
struct Image {
    std::uint32_t width = 0, height = 0;
    std::vector<std::uint8_t> rgba;
    std::vector<std::uint8_t> encoded;
};

// What a platform layer gives (section 7). Each request function calls `done` exactly once, from the platform's own
// worker thread or inside the call, never from the window's thread while it waits on the game; `status` is an event's
// status (luma_system.h).
struct Platform {
    // Any thread: platform, arch, locale, os and clipboard_images (the rest is luma_system.cpp's).
    void (*facts)(LumaSystemFacts& out) noexcept;
    void (*read_text)(std::function<void(std::int32_t status, std::string text)> done);
    void (*read_image)(std::function<void(std::int32_t status, Image image)> done);
    void (*write_text)(std::string text, std::function<void(std::int32_t status)> done);
    // `image` carries rgba; the platform encodes what its clipboard takes (encode_png) on its own worker thread.
    void (*write_image)(Image image, std::function<void(std::int32_t status)> done);
    void (*open_link)(std::string url, std::function<void(std::int32_t status)> done);
};

// The window layer, once, when it makes the game's window (before the first mod loads). The table lives for the
// process's life.
void set_platform(const Platform* platform) noexcept;
// Each platform layer's table (luma_system_win32.cpp, platform/sdl/luma_system_sdl.cpp,
// platform/apple/luma_system_apple.mm): the window layer of that platform passes it to set_platform.
const Platform* win32_platform() noexcept;
const Platform* sdl_platform() noexcept;
const Platform* apple_platform() noexcept;
// Windows: the game window's procedure, on the messages that change the snapshot (section 4). `window` the HWND;
// `active` 1 or 0 from WM_ACTIVATE, -1 to ask the system; `settings_changed` on WM_DISPLAYCHANGE and WM_DPICHANGED (the
// refresh rate is read again).
void win32_window_changed(void* window, int active, bool settings_changed) noexcept;
// SDL: true for the events that change the snapshot (F-Y1: the display's mode, orientation and scale, the window's
// display, scale, safe area, fullscreen, pixel size, focus, minimised, shown); the event loop then calls
// sdl_window_changed(window) once after its events (the SDL_Window, main thread).
bool sdl_display_event(std::uint32_t type) noexcept;
void sdl_window_changed(void* window) noexcept;
// macOS: the game window's delegate (the NSWindow, main thread), on its size, screen, full screen, key and minimised
// changes.
void apple_window_changed(void* window) noexcept;
// The platform's window thread, with each new snapshot (section 4); the same values as the last: nothing happens.
// `changes` is counted here (the caller's is ignored).
void display_changed(const LumaSystemDisplay& now) noexcept;
// The renderer, once its device is made (render_config's record_render_adapter): LumaSystemRenderer and the device's
// name.
void set_renderer(std::uint32_t renderer, std::string_view gpu) noexcept;

// Codecs for the platform layers (stb, static in luma_system.cpp), any thread. encode_png fills image.encoded from
// image.rgba; false when it cannot. decode fills image.rgba, width and height from image.encoded and clears it:
// LUMA_OK, LUMA_E_FORMAT (not a PNG, BMP or JPEG it reads) or LUMA_E_TOO_LARGE (over 8192 a side).
bool encode_png(Image& image) noexcept;
std::int32_t decode(Image& image) noexcept;

// luma_services_host's provide_all, game thread, once: the three providers in catalog order, after the host's own.
// `log` writes a line to the game's log (the whole URL of each link opened); `monotonic_ns` is the game's steady clock
// as the module reads it (luma_services_facts.h: frames in deterministic mode); either may be null.
std::span<const LumaServiceProvider* const> providers(void (*log)(const char*), std::uint64_t (*monotonic_ns)()) noexcept;

}  // namespace galaxy::luma_system
