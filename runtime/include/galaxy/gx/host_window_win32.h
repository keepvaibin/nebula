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
#include "galaxy/gx/host_window.h"
#include "galaxy/gx/render_config.h"
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

// The window's outer rect for a windowed client size on a monitor.
[[nodiscard]] RECT windowed_rect_on_monitor(const MonitorInfo& monitor, unsigned width, unsigned height);

// The settings the D3D12 renderer reads (temporal upscaling and interpolation, ray tracing).
extern std::atomic<bool> temporal_interp_suspended;  // RendererD3D12::suspend_temporal_interpolation
extern std::atomic<bool> ray_tracing_offered;        // the RAY TRACING row is offered
extern std::atomic<bool> rtx_adapter;                // the session's GPU is an NVIDIA RTX (DLSS)
[[nodiscard]] unsigned upscaler_render_scale(const RenderConfig& render, unsigned internal);
void save_native_settings_now();
LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
// The refresh rate the settings menu assumes in place of the display's (0: the display's own).
void native_settings_set_refresh_override_for_tests(unsigned hz) noexcept;

}  // namespace galaxy::gx::host_window
