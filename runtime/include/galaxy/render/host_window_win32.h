// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// The game's Win32 window, shared by the renderers (host_window_win32.cpp): its window procedure (keys, mouse, the
// pointer, focus, size, placement), the settings overlay's and the HOME Menu's state, keys and drawing, and the state
// the renderer and the window thread share.

#include "galaxy/windows_api.h"

#include "galaxy/render/display_modes.h"
#include "galaxy/render/host_window.h"
#include "galaxy/render/render_config.h"
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
// resize_window_client: the client area resized to wParam x lParam, as a drag of the window's edge would.
inline constexpr UINT kResizeClientMessage = WM_USER + 0x45;
// A second start of the game asks the running one to come forward (show_existing_instance): the window restores itself if it is
// minimised and takes the foreground.
inline constexpr UINT kShowInstanceMessage = WM_USER + 0x46;
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
// The window moved onto another monitor (its refresh rate may differ): the renderer measures the rate at once instead of at
// its next periodic check. Set by the window's thread; the renderer clears it.
extern std::atomic<bool> monitor_changed;
// The window is minimised, as the last message said.
[[nodiscard]] bool is_minimized() noexcept;
// Another program is in front (WM_ACTIVATEAPP said this one was deactivated). A dialog of this program's own does not count.
// The host's wake event is set when it changes.
[[nodiscard]] bool is_background() noexcept;

// One game per data folder. `key` names this process's folder: the window made afterwards carries it, and a second start finds
// the first's window by it (another folder's game is not disturbed). show_existing_instance waits up to `wait_ms` for the
// window to exist (the first start may still be opening it), lets that process take the foreground, and asks it to come
// forward (restoring a minimised window). False: no window carries the key.
void set_instance_key(std::uint64_t key) noexcept;
[[nodiscard]] bool show_existing_instance(std::uint64_t key, unsigned wait_ms) noexcept;

// The window's outer rect for a windowed client size on a monitor.
[[nodiscard]] RECT windowed_rect_on_monitor(const MonitorInfo& monitor, unsigned width, unsigned height);

// The settings the D3D12 renderer reads (temporal upscaling and interpolation, ray tracing).
LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

}  // namespace galaxy::render::host_window
