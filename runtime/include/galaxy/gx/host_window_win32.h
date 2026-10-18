// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// The game's Win32 window, shared by the renderers (host_window_win32.cpp): its window procedure (keys, mouse, the
// pointer, focus, size, placement), the settings overlay's and the HOME Menu's state, keys and drawing, and the state
// the renderer and the window thread share.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include "galaxy/gx/display_modes.h"
#include "galaxy/gx/ui_canvas.h"
#include "galaxy/gx/window_owner.h"

#include <atomic>
#include <cstdint>
#include <string>

namespace galaxy::gx::host_window {

inline constexpr wchar_t kWindowClass[] = L"NebulaRuntimeWndR";
inline constexpr DWORD kWindowStyle = WS_OVERLAPPEDWINDOW;
// An overlay key from the controller (wParam: virtual-key code).
inline constexpr UINT kSettingsKeyMessage = WM_USER + 0x43;
// Render-thread display change (lParam: const WindowPlacement*), sent and completed on the window thread.
inline constexpr UINT kPlaceWindowMessage = WM_USER + 0x44;
struct WindowPlacement {
    DWORD style = 0;
    RECT rect{};
    bool borderless = false;
};

// The swap chain's size (the client area), set by the renderer at start and on resize.
extern UINT backbuffer_width;
extern UINT backbuffer_height;
// Swap-chain presents per second (any source), for the FRAME RATE row.
extern std::atomic<double> measured_present_hz;
// The game's files (set_native_settings_content_root in gx_d3d12.h).
extern std::string native_settings_content_root;
// The game window, which the controller's overlay keys are posted to.
extern std::atomic<std::uintptr_t> settings_window;
extern std::atomic<bool> borderless_active;
extern std::atomic<bool> exclusive_fullscreen_active;
// The last internal resolution refused for the video-memory budget (0: none).
extern std::atomic<unsigned> efb_scale_rejected;
// The window the pointer belongs to (from WM_CREATE to WM_DESTROY).
extern std::atomic<std::uintptr_t> pointer_hwnd;
// The window is being moved or sized.
extern std::atomic<bool> move_size_active;
// The window was closed.
extern std::atomic<bool> quit_request;
// Live window resize (any size and aspect ratio without a restart): the window procedure records the latest client
// size, width << 32 | height; the render thread resizes the swap chain at the top of begin_frame, the one place
// already between frames with no command list recording, so every GPU object changes on the thread that owns it.
extern std::atomic<std::uint64_t> pending_resize_extent;
// Set by the overlay when Match Output is switched on, so the render thread derives the scale for the current
// window right away.
extern std::atomic<bool> follow_window_rearm;

// The internal resolution that fills the display.
[[nodiscard]] unsigned display_efb_scale_cap();
// Saves changed settings; false (kept dirty for a retry) when a file could not be written.
bool flush_native_settings_if_dirty();
// Takes a config.ini that Nebula Settings changed while the game runs.
void poll_render_config_file();
// GALAXY_HIDE_WINDOW: a hidden test window.
[[nodiscard]] bool hidden_window_mode_enabled();
// The window's outer rect for a windowed client size on a monitor.
[[nodiscard]] RECT windowed_rect_on_monitor(const MonitorInfo& monitor, unsigned width, unsigned height);
LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
// The game window at the renderer's start: the configured monitor and display mode (a hidden test window stays
// windowed), owned by `owner`'s thread. Sets backbuffer_width/height, settings_window and borderless_active; a null
// window (logged) when it cannot be created.
struct OpenedWindow {
    HWND window = nullptr;
    bool shown = false;
};
[[nodiscard]] OpenedWindow open_game_window(WindowOwner& owner, int window_width, int window_height);
// Ends the window's thread and unregisters its class (the renderer's shutdown).
void close_game_window(WindowOwner& owner) noexcept;
void draw_native_settings_overlay(UiCanvas& renderer);
void draw_home_menu(UiCanvas& renderer);
void draw_native_settings_hint(UiCanvas& renderer);

}  // namespace galaxy::gx::host_window
