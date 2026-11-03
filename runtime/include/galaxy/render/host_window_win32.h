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

#include "galaxy/render/display_modes.h"
#include "galaxy/render/host_window.h"
#include "galaxy/render/render_config.h"
#include "galaxy/render/ui_canvas.h"
#include "galaxy/render/window_owner.h"

#include <atomic>
#include <cstdint>
#include <string>

namespace galaxy::render::host_window {

inline constexpr wchar_t kWindowClass[] = L"NebulaRuntimeWndR";
inline constexpr DWORD kWindowStyle = WS_OVERLAPPEDWINDOW;
// An overlay key from the controller (wParam: virtual-key code).
inline constexpr UINT kSettingsKeyMessage = WM_USER + 0x43;
// Render-thread display change (lParam: const WindowPlacement*), sent and completed on the window thread.
inline constexpr UINT kPlaceWindowMessage = WM_USER + 0x44;
// Tests (GALAXY_TEST_WINDOW_SIZES): the client area resized to wParam x lParam, as a drag of the window's edge would.
inline constexpr UINT kTestResizeMessage = WM_USER + 0x45;
struct WindowPlacement {
    DWORD style = 0;
    RECT rect{};
    bool borderless = false;
};

// The game window, which the controller's overlay keys are posted to.
extern std::atomic<std::uintptr_t> settings_window;
// The window the pointer belongs to (from WM_CREATE to WM_DESTROY).
extern std::atomic<std::uintptr_t> pointer_hwnd;
// The desktop's monitors changed: a borderless window's bounds are out of date.
extern std::atomic<bool> display_changed;
// The window is minimised, as the last message said.
[[nodiscard]] bool is_minimized() noexcept;

// The window's outer rect for a windowed client size on a monitor.
[[nodiscard]] RECT windowed_rect_on_monitor(const MonitorInfo& monitor, unsigned width, unsigned height);

// The settings the D3D12 renderer reads (temporal upscaling and interpolation, ray tracing).
extern std::atomic<bool> temporal_interp_suspended;  // RendererD3D12::suspend_temporal_interpolation
LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

}  // namespace galaxy::render::host_window
