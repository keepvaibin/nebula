// SPDX-License-Identifier: GPL-3.0-only
// The game's Win32 window on both renderers (host_window_win32.h): the window procedure, the pointer, and what the
// settings menu asks of its platform (host_window.h).

#include "galaxy/achievements.h"
#include "galaxy/luma_input_service.h"
#include "galaxy/stats.h"
#include "galaxy/vendor_download.h"
#include "galaxy/render/host_window_win32.h"
#include "galaxy/render/xess_library.h"
#include "galaxy/windows_paths.h"
#include "galaxy/windows_power.h"
#include "galaxy/path_text.h"
#include "galaxy/controller_hid.h"
#include "galaxy/input/raw_mouse_win32.h"

#include "galaxy/render/display_modes.h"
#include "galaxy/render/galaxy_select.h"
#include "galaxy/render/post_process_settings.h"
#include "galaxy/render/render_config.h"
#include "galaxy/render/renderer_common.h"
#include "galaxy/render/streamline_bridge.h"
#include "galaxy/render/texture_pack.h"
#include "galaxy/render_api.h"
#include "galaxy/mii_import.h"
#include "galaxy/mod_import.h"
#include "galaxy/launch_args.h"
#include "galaxy/mods.h"
#include "galaxy/luma_system_platform.h"
#include "galaxy/game_paths.h"
#include "galaxy/native_settings_pad.h"
#include "galaxy/platform_time.h"
#include "galaxy/runtime_settings.h"
#include "galaxy/ui/home_menu_model.h"
#include "galaxy/ui/settings_model.h"
#include "galaxy/ui/settings_graphics_page.h"
#include "galaxy/ui/settings_general_page.h"
#include "galaxy/ui/settings_display_page.h"
#include "galaxy/ui/settings_advanced_page.h"
#include "galaxy/ui/settings_system_page.h"
#include "galaxy/ui/settings_save_model.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <condition_variable>
#include <functional>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <filesystem>
#include <iostream>
#include <iterator>
#include <limits>
#include <mutex>
#include <thread>
#include <string>
#include <utility>
#include <vector>

#include <shellapi.h>
#include <shobjidl.h>

namespace galaxy::render {

namespace host_window {

UINT backbuffer_width = 1280;
UINT backbuffer_height = 720;
std::atomic<std::uintptr_t> settings_window{0};
std::atomic<bool> borderless_active{false};
std::atomic<std::uintptr_t> pointer_hwnd{0};
std::atomic<bool> display_changed{false};
std::atomic<bool> monitor_changed{false};
namespace {
HMONITOR s_window_monitor = nullptr;  // the window's thread only
std::atomic<bool> s_minimized{false};
std::atomic<bool> s_background{false};  // the program is not the active one (WM_ACTIVATEAPP)
}
std::atomic<HANDLE> s_state_wake{nullptr};

void wake_main_thread() noexcept {
    if (const HANDLE event = s_state_wake.load(std::memory_order_acquire); event != nullptr) {
        SetEvent(event);
    }
}

void set_state_wake(void* event) noexcept {
    s_state_wake.store(static_cast<HANDLE>(event), std::memory_order_release);
}

bool is_minimized() noexcept {
    return s_minimized.load(std::memory_order_relaxed);
}

bool is_background() noexcept {
    return s_background.load(std::memory_order_relaxed);
}

namespace {
std::atomic<std::uint64_t> s_instance_key{0u};
constexpr wchar_t kInstanceProperty[] = L"NebulaInstanceKey";

struct InstanceSearch {
    std::uintptr_t key = 0u;
    HWND found = nullptr;
};

BOOL CALLBACK find_instance_window(HWND window, LPARAM parameter) {
    auto& search = *reinterpret_cast<InstanceSearch*>(parameter);
    wchar_t name[64]{};
    if (GetClassNameW(window, name, static_cast<int>(std::size(name))) == 0 || std::wcscmp(name, kWindowClass) != 0) {
        return TRUE;
    }
    if (reinterpret_cast<std::uintptr_t>(GetPropW(window, kInstanceProperty)) != search.key) {
        return TRUE;
    }
    search.found = window;
    return FALSE;
}
}  // namespace

void set_instance_key(std::uint64_t key) noexcept {
    s_instance_key.store(key, std::memory_order_relaxed);
}

bool show_existing_instance(std::uint64_t key, unsigned wait_ms, const std::vector<std::filesystem::path>& imports) noexcept {
    if (key == 0u) {
        return false;
    }
    const ULONGLONG deadline = GetTickCount64() + wait_ms;
    for (;;) {
        InstanceSearch search;
        search.key = static_cast<std::uintptr_t>(key);
        EnumWindows(find_instance_window, reinterpret_cast<LPARAM>(&search));
        if (search.found != nullptr) {
            // The user started this process last, so it may hand the foreground over; the running game's window takes it.
            DWORD process = 0u;
            GetWindowThreadProcessId(search.found, &process);
            if (process != 0u) {
                (void)AllowSetForegroundWindow(process);
            }
            if (!imports.empty()) {
                // Copied by the system into the running game before SendMessage returns; a game that hangs is left.
                std::wstring paths;
                try {
                    for (const std::filesystem::path& path : imports) {
                        paths += std::filesystem::absolute(path).wstring();
                        paths.push_back(L'\0');
                    }
                } catch (...) {
                    paths.clear();
                }
                COPYDATASTRUCT data{};
                data.dwData = kImportCopyData;
                data.cbData = static_cast<DWORD>(paths.size() * sizeof(wchar_t));
                data.lpData = paths.data();
                DWORD_PTR answer = 0u;
                if (!paths.empty() && SendMessageTimeoutW(search.found, WM_COPYDATA, 0, reinterpret_cast<LPARAM>(&data),
                                                          SMTO_ABORTIFHUNG, 5000u, &answer) == 0) {
                    std::cerr << "[host] the running game did not take the files to import\n";
                }
            }
            return PostMessageW(search.found, kShowInstanceMessage, 0, 0) != FALSE;
        }
        if (GetTickCount64() >= deadline) {
            return false;
        }
        Sleep(50);
    }
}
static UINT monitor_dpi(const MonitorInfo& monitor) noexcept {
    const HMONITOR handle = MonitorFromPoint(
        POINT{monitor.bounds.left + 1, monitor.bounds.top + 1}, MONITOR_DEFAULTTONEAREST);
    UINT dpi_x = 96u;
    UINT dpi_y = 96u;
    using GetDpiForMonitorFn = HRESULT(WINAPI*)(HMONITOR, int, UINT*, UINT*);
    static const auto get_dpi = reinterpret_cast<GetDpiForMonitorFn>(
        GetProcAddress(LoadLibraryW(L"shcore.dll"), "GetDpiForMonitor"));
    if (get_dpi == nullptr || FAILED(get_dpi(handle, 0, &dpi_x, &dpi_y))) {
        dpi_x = GetDpiForSystem();
    }
    return std::max<UINT>(96u, dpi_x);
}
std::atomic<bool> move_size_active{false};
std::atomic<bool> quit_request{false};
std::atomic<std::uint64_t> pending_resize_extent{0};
std::atomic<bool> surface_withdrawn{false};
std::timed_mutex surface_use;
// RendererD3D12::suspend_temporal_interpolation (the FRAME RATE row says so).

}  // namespace host_window

using host_window::hidden_window_mode_enabled;
using host_window::windowed_rect_on_monitor;
using host_window::wnd_proc;
using host_window::kSettingsKeyMessage;
using host_window::kWindowStyle;
using host_window::kPlaceWindowMessage;
using host_window::kResizeClientMessage;
using host_window::WindowPlacement;

namespace {

std::atomic<bool> s_pointer_focused{false};
galaxy::host::HostPointerSnapshotCache s_pointer_snapshot;
std::atomic<int> s_pointer_last_cursor_poll_x{std::numeric_limits<int>::min()};
std::atomic<int> s_pointer_last_cursor_poll_y{std::numeric_limits<int>::min()};
std::atomic<bool> s_pointer_left_button{false};
std::atomic<bool> s_pointer_right_button{false};
std::atomic<bool> s_pointer_focus_valid{false};
galaxy::host::HostPointerEventRing s_pointer_transitions;
bool s_tracking_mouse_leave = false;
std::atomic<bool> s_system_menu_active{false};
// The display driver's monitors and modes, read at start and on WM_DISPLAYCHANGE: enumerating them can take tens of
// milliseconds, which opening the settings menu (under the lock the render thread takes) must not wait on.
std::mutex s_monitor_cache_mutex;
std::vector<MonitorInfo> s_monitor_cache;

void store_monitor_cache(std::vector<MonitorInfo> monitors) {
    const std::lock_guard<std::mutex> lock(s_monitor_cache_mutex);
    s_monitor_cache = std::move(monitors);
}

std::vector<MonitorInfo> cached_monitors() {
    const std::lock_guard<std::mutex> lock(s_monitor_cache_mutex);
    if (s_monitor_cache.empty()) {
        // Before the renderer has started (tests), the list is read now.
        s_monitor_cache = enumerate_monitors();
    }
    return s_monitor_cache;
}

int signed_lparam_x(LPARAM lp) {
    return static_cast<int>(static_cast<short>(lp & 0xFFFF));
}

int signed_lparam_y(LPARAM lp) {
    return static_cast<int>(static_cast<short>((lp >> 16) & 0xFFFF));
}

constexpr galaxy::host::HostPointerCursorPollMode kCursorPollMode =
    galaxy::host::HostPointerCursorPollMode::Adaptive;
// A client-area mouse message counts as recent for 120 ms; a centered cursor sample after one is rejected as stale for
// 5 s (classify_host_pointer_client_message_timing).
constexpr std::uint64_t kPointerMessageRecentMs = 120u;
constexpr std::uint64_t kStaleCenterRejectMs = 5000u;

void maybe_hide_os_cursor() {
    const HWND hwnd = reinterpret_cast<HWND>(
        host_window::pointer_hwnd.load(std::memory_order_relaxed));
    const bool focused =
        s_pointer_focused.load(std::memory_order_relaxed) ||
        (hwnd != nullptr && GetForegroundWindow() == hwnd);
    if (focused) {
        SetCursor(nullptr);
    }
}

galaxy::host::HostPointerCoordinates cached_pointer_coordinates(
    bool inside_override) noexcept {
    const galaxy::host::HostPointerSnapshot snapshot =
        s_pointer_snapshot.snapshot();
    galaxy::host::HostPointerCoordinates coordinates = snapshot.coordinates;
    coordinates.inside_client = inside_override && coordinates.absolute_valid;
    return coordinates;
}

struct AcquiredHostPointerCoordinates {
    galaxy::host::HostPointerCoordinates coordinates{};
    std::uint64_t absolute_acquired_ms{};
};

void mark_pointer_outside() noexcept {
    (void)s_pointer_snapshot.update(
        [](galaxy::host::HostPointerSnapshot& snapshot) noexcept {
            galaxy::host::publish_host_pointer_window_visibility_loss(snapshot);
        });
}

void clear_pointer_buttons() noexcept {
    s_pointer_left_button.store(false, std::memory_order_relaxed);
    s_pointer_right_button.store(false, std::memory_order_relaxed);
}

void invalidate_pointer_focus_once() noexcept {
    if (s_pointer_focus_valid.exchange(false, std::memory_order_acq_rel)) {
        s_pointer_transitions.invalidate_focus(
            cached_pointer_coordinates(true),
            static_cast<std::uint64_t>(GetTickCount64()));
    }
}

void clear_pointer_window_state(HWND hwnd) noexcept {
    // Losing focus or capture releases the buttons, not the pointer: the cursor poll stays authoritative while the
    // window is unfocused, and only WM_MOUSELEAVE or a sample outside the window reports the pointer lost.
    clear_pointer_buttons();
    if (hwnd != nullptr && GetCapture() == hwnd) {
        ReleaseCapture();
    }
}

bool seed_pointer_from_client(
    HWND hwnd,
    bool force_center) {
    RECT client{};
    if (!GetClientRect(hwnd, &client)) {
        return false;
    }
    const int width = client.right - client.left;
    const int height = client.bottom - client.top;
    if (width <= 1 || height <= 1) {
        return false;
    }

    const std::uint64_t pointer_now_ms =
        static_cast<std::uint64_t>(GetTickCount64());
    const galaxy::host::HostPointerSnapshot seeded_snapshot =
        s_pointer_snapshot.update(
        [=](galaxy::host::HostPointerSnapshot& snapshot) noexcept {
            galaxy::host::publish_host_pointer_seed(
                snapshot,
                {
                    width,
                    height,
                    force_center,
                    pointer_now_ms,
                });
        });

    // A stationary cursor inside the window sends no WM_MOUSEMOVE after launch, resize or refocus, and the seed above
    // is inactive: publish the real cursor now, with the first-poll fallback's compare-before-publish rule, so the
    // synthetic seed does not stand in for it.
    const bool remote_session = GetSystemMetrics(SM_REMOTESESSION) != 0;
    const auto poll_authority =
        galaxy::host::select_host_pointer_cursor_poll_authority(
            kCursorPollMode, remote_session, false);
    POINT cursor{};
    if (poll_authority.poll_enabled &&
        poll_authority.authoritative && GetCursorPos(&cursor) &&
        ScreenToClient(hwnd, &cursor) && cursor.x >= 0 && cursor.y >= 0 &&
        cursor.x < width && cursor.y < height) {
        const int cursor_x =
            std::clamp(static_cast<int>(cursor.x), 0, width - 1);
        const int cursor_y =
            std::clamp(static_cast<int>(cursor.y), 0, height - 1);
        bool publication_accepted = false;
        (void)s_pointer_snapshot.update(
            [&](galaxy::host::HostPointerSnapshot& current) noexcept {
                publication_accepted =
                    galaxy::host::apply_host_pointer_fallback_inside_publication(
                        current,
                        {
                            cursor_x,
                            cursor_y,
                            width,
                            height,
                            pointer_now_ms,
                            seeded_snapshot.event_sequence,
                            seeded_snapshot.absolute_sequence,
                        }) == galaxy::host::
                            HostPointerFallbackInsidePublicationResult::
                                PublishedInside;
            });
        if (publication_accepted) {
            s_pointer_last_cursor_poll_x.store(
                cursor_x, std::memory_order_relaxed);
            s_pointer_last_cursor_poll_y.store(
                cursor_y, std::memory_order_relaxed);
        }
    }
    return true;
}

AcquiredHostPointerCoordinates update_pointer_absolute(
    HWND hwnd,
    LPARAM lp) {
    galaxy::host::HostPointerCoordinates coordinates{};
    RECT client{};
    if (!GetClientRect(hwnd, &client)) {
        return {};
    }
    const int width = client.right - client.left;
    const int height = client.bottom - client.top;
    if (width <= 1 || height <= 1) {
        return {};
    }

    const int raw_x = signed_lparam_x(lp);
    const int raw_y = signed_lparam_y(lp);
    const bool inside =
        raw_x >= 0 && raw_y >= 0 && raw_x < width && raw_y < height;
    coordinates = {
        true,
        inside,
        raw_x,
        raw_y,
        width,
        height,
    };
    const std::uint64_t now_ms = static_cast<std::uint64_t>(GetTickCount64());
    // The time the message was queued, not dispatched.
    const DWORD message_age = static_cast<DWORD>(now_ms) -
        static_cast<DWORD>(GetMessageTime());
    const std::uint64_t pointer_now_ms = message_age <= 0x7fffffffu && message_age <= now_ms
        ? now_ms - message_age : 0u;
    (void)s_pointer_snapshot.update(
        [=](galaxy::host::HostPointerSnapshot& snapshot) noexcept {
            galaxy::host::publish_host_pointer_client_coordinate(
                snapshot,
                {
                    raw_x,
                    raw_y,
                    width,
                    height,
                    pointer_now_ms,
                });
        });

    if (inside && !s_tracking_mouse_leave) {
        TRACKMOUSEEVENT tme{};
        tme.cbSize = sizeof(tme);
        tme.dwFlags = TME_LEAVE;
        tme.hwndTrack = hwnd;
        s_tracking_mouse_leave = TrackMouseEvent(&tme) != FALSE;
    }
    return {coordinates, pointer_now_ms};
}

void update_pointer_button(HWND hwnd, LPARAM lp, bool left, bool down) {
    const AcquiredHostPointerCoordinates publication =
        update_pointer_absolute(hwnd, lp);
    if (left) {
        s_pointer_left_button.store(down, std::memory_order_relaxed);
    } else {
        s_pointer_right_button.store(down, std::memory_order_relaxed);
    }
    s_pointer_transitions.accept_button_transition(
        left ? galaxy::host::HostPointerButton::Left
             : galaxy::host::HostPointerButton::Right,
        down,
        publication.coordinates,
        publication.absolute_acquired_ms);
}

// The input message being handled came from SendInput, not a device.
bool input_message_injected() noexcept {
    INPUT_MESSAGE_SOURCE source{};
    return GetCurrentInputMessageSource(&source) && source.originId == IMO_INJECTED;
}

std::wstring launcher_path() {
    std::wstring launcher = ::galaxy::windows::executable_path();
    if (launcher.empty()) {
        return {};
    }
    const std::size_t slash = launcher.find_last_of(L"\\/");
    if (slash == std::wstring::npos) {
        return {};
    }
    launcher.resize(slash + 1u);
    launcher += L"Nebula.exe";
    return GetFileAttributesW(launcher.c_str()) == INVALID_FILE_ATTRIBUTES ? std::wstring{} : launcher;
}

bool start_launcher(const std::wstring& arguments, HANDLE* process_out) {
    const std::wstring launcher = launcher_path();
    if (launcher.empty()) {
        return false;
    }
    std::wstring command = L"\"" + launcher + L"\" " + arguments;
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(launcher.c_str(), command.data(), nullptr, nullptr, FALSE, 0u, nullptr, nullptr,
                        &startup, &process)) {
        std::cerr << "[launcher] could not start Nebula.exe: win32=" << GetLastError() << '\n';
        return false;
    }
    // Its window comes to the front over the game.
    AllowSetForegroundWindow(process.dwProcessId);
    CloseHandle(process.hThread);
    if (process_out != nullptr) {
        *process_out = process.hProcess;
    } else {
        CloseHandle(process.hProcess);
    }
    return true;
}


}  // namespace

namespace host_window {

// The window's outer rect for a windowed client size on a monitor: limited to
// the monitor's work area and centered in it.
RECT windowed_rect_on_monitor(const MonitorInfo& monitor, unsigned width,
                              unsigned height) {
    RECT frame{0, 0, 0, 0};
    (void)AdjustWindowRectExForDpi(&frame, kWindowStyle, FALSE, WS_EX_APPWINDOW, monitor_dpi(monitor));
    const long frame_w = frame.right - frame.left;
    const long frame_h = frame.bottom - frame.top;
    const long work_w = monitor.work_area.right - monitor.work_area.left;
    const long work_h = monitor.work_area.bottom - monitor.work_area.top;
    const long client_w = std::clamp<long>(static_cast<long>(width), 320, std::max(320L, work_w - frame_w));
    const long client_h = std::clamp<long>(static_cast<long>(height), 240, std::max(240L, work_h - frame_h));
    const long outer_w = client_w + frame_w;
    const long outer_h = client_h + frame_h;
    const long left = monitor.work_area.left + std::max(0L, (work_w - outer_w) / 2);
    const long top = monitor.work_area.top + std::max(0L, (work_h - outer_h) / 2);
    return RECT{left, top, left + outer_w, top + outer_h};
}

OpenedWindow open_game_window(WindowOwner& owner, int window_width, int window_height) {
    OpenedWindow opened;
    quit_request.store(false, std::memory_order_release);
    const bool hidden_window = hidden_window_mode_enabled();
    // The configured monitor and display mode. Hidden test windows stay windowed.
    const RenderConfig start_config = get_render_config();
    const unsigned start_mode = hidden_window
        ? kDisplayWindowed : std::min(start_config.display_mode, kDisplayBorderless);
    const bool fullscreen_window = start_mode != kDisplayWindowed;
    const std::vector<MonitorInfo> start_monitors = enumerate_monitors();
    store_monitor_cache(start_monitors);
    const MonitorInfo* start_monitor =
        find_monitor(start_monitors, widen_utf8(start_config.monitor.data()));
    // The window is made on that monitor: its frame is the one of that monitor's scale.
    const UINT frame_dpi = start_monitor != nullptr ? monitor_dpi(*start_monitor) : GetDpiForSystem();
    int actual_window_width = window_width;
    int actual_window_height = window_height;
    int window_x = CW_USEDEFAULT;
    int window_y = CW_USEDEFAULT;
    DWORD window_style = kWindowStyle;

    if (fullscreen_window && start_monitor != nullptr) {
        const RECT& bounds = start_monitor->bounds;
        window_x = bounds.left;
        window_y = bounds.top;
        actual_window_width = static_cast<int>(bounds.right - bounds.left);
        actual_window_height = static_cast<int>(bounds.bottom - bounds.top);
        if (actual_window_width <= 0 || actual_window_height <= 0) {
            std::cerr << "[Renderer] invalid monitor dimensions\n";
            return {};
        }
        window_style = WS_POPUP;
    } else if (!hidden_window && start_monitor != nullptr && window_width > 0 &&
               window_height > 0) {
        // Windowed: limited to the monitor's work area and centered on it.
        const RECT outer = windowed_rect_on_monitor(
            *start_monitor, static_cast<unsigned>(window_width),
            static_cast<unsigned>(window_height));
        RECT frame{0, 0, 0, 0};
        (void)AdjustWindowRectExForDpi(&frame, kWindowStyle, FALSE, WS_EX_APPWINDOW, frame_dpi);
        window_x = outer.left;
        window_y = outer.top;
        actual_window_width = static_cast<int>(
            (outer.right - outer.left) - (frame.right - frame.left));
        actual_window_height = static_cast<int>(
            (outer.bottom - outer.top) - (frame.bottom - frame.top));
    }

    if (actual_window_width <= 0 || actual_window_height <= 0) {
        std::cerr << "[Renderer] invalid client dimensions\n";
        return {};
    }

    backbuffer_width   = static_cast<UINT>(actual_window_width);
    backbuffer_height  = static_cast<UINT>(actual_window_height);

    {
        WNDCLASSEXW wc{};
        wc.cbSize        = sizeof(wc);
        wc.style         = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc   = wnd_proc;
        wc.hInstance     = GetModuleHandleW(nullptr);
        // The arrow while the window is unfocused; WM_SETCURSOR hides it while the game has focus.
        wc.hCursor       = LoadCursorA(nullptr, IDC_ARROW);
        wc.hIcon         = static_cast<HICON>(LoadImageW(wc.hInstance, MAKEINTRESOURCEW(1), IMAGE_ICON,
            GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), LR_DEFAULTCOLOR));
        wc.hIconSm       = static_cast<HICON>(LoadImageW(wc.hInstance, MAKEINTRESOURCEW(1), IMAGE_ICON,
            GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR));
        wc.lpszClassName = kWindowClass;
        RegisterClassExW(&wc);

        RECT frame{0, 0, 0, 0};
        if (!fullscreen_window) {
            if (!AdjustWindowRectExForDpi(&frame, window_style, FALSE, WS_EX_APPWINDOW, frame_dpi)) {
                std::cerr << "[Renderer] AdjustWindowRect failed\n";
                return {};
            }
        }
        const std::int64_t outer_width =
            static_cast<std::int64_t>(actual_window_width) +
            static_cast<std::int64_t>(frame.right) - frame.left;
        const std::int64_t outer_height =
            static_cast<std::int64_t>(actual_window_height) +
            static_cast<std::int64_t>(frame.bottom) - frame.top;
        if (outer_width <= 0 || outer_height <= 0 ||
            outer_width > std::numeric_limits<int>::max() ||
            outer_height > std::numeric_limits<int>::max()) {
            std::cerr << "[Renderer] adjusted window dimensions exceed Win32 limits\n";
            return {};
        }
        // Files dropped on the window import (WM_DROPFILES).
        const DWORD ex_style =
            hidden_window ? (WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE)
                          : (WS_EX_APPWINDOW | WS_EX_ACCEPTFILES);
        opened.window = owner.start({
            kWindowClass, L"Nebula", window_style, ex_style,
            window_x, window_y, static_cast<int>(outer_width),
            static_cast<int>(outer_height), !hidden_window});
        if (opened.window == nullptr) {
            std::cerr << "[Renderer] CreateWindow failed\n";
            return {};
        }
        settings_window.store(
            reinterpret_cast<std::uintptr_t>(opened.window), std::memory_order_release);
        // Files the game was started to import (Open with, --import): as a drop on the window.
        if (!::galaxy::launch_args().imports.empty()) {
            native_settings_files_dropped(::galaxy::launch_args().imports);
        }
        // LumaCore's clipboard and links on Windows (services-system 5, 6).
        galaxy::luma_system::set_platform(galaxy::luma_system::win32_platform());
        // The display's first snapshot (services-system 4) from the window as made: a window made hidden (a test run,
        // a start behind another window) gets no WM_SIZE until it is shown, and changes 0 would read as no window.
        galaxy::luma_system::win32_window_changed(opened.window, -1, true);
        if (const std::uint64_t key = s_instance_key.load(std::memory_order_relaxed); key != 0u) {
            (void)SetPropW(opened.window, kInstanceProperty, reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(key)));
        }
        borderless_active.store(fullscreen_window, std::memory_order_release);
        // No pointer seed from this thread: the window thread publishes geometry and input through WM_SIZE and
        // WM_SETFOCUS, and the first real message or cursor poll places the pointer.
        opened.shown = !hidden_window;
    }
    return opened;
}

void close_game_window(WindowOwner& owner) noexcept {
    owner.stop();
    pointer_hwnd.store(0u, std::memory_order_release);
    UnregisterClassW(kWindowClass, GetModuleHandleW(nullptr));
}

void show_window(NativeWindow window) noexcept {
    ShowWindowAsync(window, SW_SHOWNORMAL);
}

bool present_grid(std::int64_t&, std::int64_t&, std::int64_t&, std::int64_t&) noexcept {
    return false;  // the presents pace themselves (present_pacer)
}

// The vblanks' own rate when they are timed (the mode reports whole hertz: 239 or 240 for 239.76).
double display_refresh_hz(NativeWindow window) noexcept {
    std::int64_t vblank = 0;
    std::int64_t period = 0;
    (void)window;
    if (vblank_grid_known(vblank, period)) {
        return std::round(static_cast<double>(galaxy::qpc_frequency()) / static_cast<double>(period) * 100.0) / 100.0;
    }
    const HMONITOR monitor = MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST);
    MONITORINFOEXW info{};
    info.cbSize = sizeof(info);
    DEVMODEW mode{};
    mode.dmSize = sizeof(mode);
    if (monitor == nullptr || !GetMonitorInfoW(monitor, &info) ||
        !EnumDisplaySettingsW(info.szDevice, ENUM_CURRENT_SETTINGS, &mode) || mode.dmDisplayFrequency <= 1u) {
        return 0.0;
    }
    return static_cast<double>(mode.dmDisplayFrequency);
}

// Raw relative mouse motion (WM_INPUT) for the window while it is in front: no input sink and no legacy suppression, so
// the cursor and the settings menu work as without it.
static void raw_mouse_attach(HWND hwnd) noexcept {
    (void)galaxy::host::install_native_raw_mouse(reinterpret_cast<std::uintptr_t>(hwnd));
}

static void raw_mouse_detach(HWND hwnd) noexcept {
    galaxy::host::remove_native_raw_mouse(reinterpret_cast<std::uintptr_t>(hwnd));
}

LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_NCDESTROY) {
        (void)RemovePropW(hwnd, kInstanceProperty);
    }
    if (msg == WM_SIZE || msg == WM_MOVE || msg == WM_WINDOWPOSCHANGED || msg == WM_DPICHANGED || msg == WM_DISPLAYCHANGE ||
        msg == WM_ACTIVATE || msg == WM_SETFOCUS || msg == WM_KILLFOCUS) {
        // LumaCore's display snapshot (services-system 4), where the window's changes arrive.
        galaxy::luma_system::win32_window_changed(hwnd, msg == WM_ACTIVATE ? (LOWORD(wp) != WA_INACTIVE ? 1 : 0) : -1,
                                                  msg == WM_DISPLAYCHANGE || msg == WM_DPICHANGED);
    }
    if (msg == kShowInstanceMessage) {
        ShowWindow(hwnd, IsIconic(hwnd) ? SW_RESTORE : SW_SHOW);
        (void)SetForegroundWindow(hwnd);
        return 0;
    }
    if (msg == WM_COPYDATA) {
        // Files a second start was asked to import (show_existing_instance): imported as dropped files.
        const auto* const data = reinterpret_cast<const COPYDATASTRUCT*>(lp);
        if (data == nullptr || data->dwData != kImportCopyData || data->lpData == nullptr) {
            return FALSE;
        }
        const auto* const text = static_cast<const wchar_t*>(data->lpData);
        const std::size_t count = data->cbData / sizeof(wchar_t);
        std::vector<std::filesystem::path> files;
        for (std::size_t at = 0; at < count;) {
            std::size_t end = at;
            while (end < count && text[end] != L'\0') ++end;
            if (end > at) files.emplace_back(std::wstring(text + at, end - at));
            at = end + 1u;
        }
        if (!files.empty()) native_settings_files_dropped(std::move(files));
        return TRUE;
    }
    if (msg == WM_DROPFILES) {
        // Dropped files: mod archives and folders into the mods folder (the MODS page opens on the import),
        // anything else to the Mii import.
        const auto drop = reinterpret_cast<HDROP>(wp);
        std::vector<std::filesystem::path> files;
        const UINT count = DragQueryFileW(drop, 0xFFFFFFFFu, nullptr, 0u);
        for (UINT i = 0; i < count; ++i) {
            const UINT length = DragQueryFileW(drop, i, nullptr, 0u);
            std::wstring name(length + 1u, L'\0');
            DragQueryFileW(drop, i, name.data(), length + 1u);
            name.resize(length);
            files.emplace_back(name);
        }
        DragFinish(drop);
        native_settings_files_dropped(std::move(files));
        return 0;
    }
    if (msg == kSettingsKeyMessage) {
        (void)native_settings_pad_key(wp);
        return 0;
    }
    if (msg == WM_DISPLAYCHANGE) {
        store_monitor_cache(enumerate_monitors());
        display_changed.store(true, std::memory_order_release);
        monitor_changed.store(true, std::memory_order_release);
    }
    if (msg == WM_WINDOWPOSCHANGED || msg == WM_DPICHANGED) {
        // The message goes on to the default handling below. A new monitor, or a new density on the same one (the user's scale
        // setting): the renderer reads the output's rate and colour space again, and the menus' size follows the window's width
        // in density-independent units (window_width_dp, read as the window is drawn).
        const HMONITOR now = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
        if (now != s_window_monitor || msg == WM_DPICHANGED) {
            s_window_monitor = now;
            monitor_changed.store(true, std::memory_order_release);
        }
    }
    if (msg == kResizeClientMessage) {
        RECT rect{0, 0, static_cast<LONG>(wp), static_cast<LONG>(lp)};
        (void)AdjustWindowRectEx(&rect, static_cast<DWORD>(GetWindowLongPtrW(hwnd, GWL_STYLE)), FALSE,
                                 static_cast<DWORD>(GetWindowLongPtrW(hwnd, GWL_EXSTYLE)));
        (void)SetWindowPos(hwnd, nullptr, 0, 0, rect.right - rect.left, rect.bottom - rect.top,
                           SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    }
    if (msg == kPlaceWindowMessage) {
        const auto* placement = reinterpret_cast<const WindowPlacement*>(lp);
        if (placement != nullptr) {
            // Keep the window's visibility (hidden test windows stay hidden).
            const LONG_PTR visible = GetWindowLongPtrW(hwnd, GWL_STYLE) & WS_VISIBLE;
            (void)SetWindowLongPtrW(
                hwnd, GWL_STYLE, static_cast<LONG_PTR>(placement->style) | visible);
            (void)SetWindowPos(
                hwnd, placement->borderless ? HWND_TOP : nullptr,
                placement->rect.left, placement->rect.top,
                placement->rect.right - placement->rect.left,
                placement->rect.bottom - placement->rect.top,
                SWP_FRAMECHANGED | SWP_NOACTIVATE |
                    (placement->borderless ? 0u : SWP_NOZORDER));
            host_window::borderless_active.store(placement->borderless, std::memory_order_release);
        }
        return 0;
    }
    if (msg == WM_CREATE) {
        // The window types text for LumaCore's text requests (above).
        galaxy::luma_input::set_text_entry_available(true);
        host_window::pointer_hwnd.store(reinterpret_cast<std::uintptr_t>(hwnd),
                             std::memory_order_release);
        raw_mouse_attach(hwnd);
    }
    if (msg == WM_INPUT) {
        // Relative motion for the pointer paths that ask for raw movement; DefWindowProc releases the packet.
        galaxy::host::publish_native_raw_mouse(reinterpret_cast<std::uintptr_t>(hwnd), static_cast<std::uintptr_t>(lp));
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
    if (msg == WM_ENTERMENULOOP || msg == WM_ENTERSIZEMOVE) {
        (msg == WM_ENTERMENULOOP ? s_system_menu_active : host_window::move_size_active)
            .store(true, std::memory_order_release);
        galaxy::host::clear_native_window_key_presses();
        invalidate_pointer_focus_once();
        clear_pointer_buttons();
    }
    if (msg == WM_EXITSIZEMOVE &&
        !host_window::borderless_active.load(std::memory_order_acquire)) {
        // A size the player drags the window to becomes the windowed size.
        RECT client{};
        if (GetClientRect(hwnd, &client) && client.right > 0 && client.bottom > 0) {
            native_settings_window_resized(static_cast<unsigned>(client.right), static_cast<unsigned>(client.bottom));
        }
    }
    if (msg == WM_EXITMENULOOP || msg == WM_EXITSIZEMOVE) {
        (msg == WM_EXITMENULOOP ? s_system_menu_active : host_window::move_size_active)
            .store(false, std::memory_order_release);
        s_pointer_focus_valid.store(GetForegroundWindow() == hwnd,
                                    std::memory_order_release);
        seed_pointer_from_client(hwnd, false);
    }
    // LumaCore's text entry (input.text_request): while a request is open the keyboard types into it, not into the game.
    // The system's input method composes in its own window and hands each result here as WM_CHAR (TranslateMessage
    // makes the plain keys' WM_CHAR too); Enter, Escape and the editing keys arrive as WM_KEYDOWN. Key releases still
    // reach the game below, so a key held when the request opened is not left down.
    if (galaxy::luma_input::text_open()) {
        if (msg == WM_KEYDOWN) {
            using galaxy::luma_input::TextKey;
            switch (wp) {
            case VK_RETURN: galaxy::luma_input::text_key(TextKey::Enter); break;
            case VK_ESCAPE: galaxy::luma_input::text_key(TextKey::Escape); break;
            case VK_BACK: galaxy::luma_input::text_key(TextKey::Backspace); break;
            case VK_DELETE: galaxy::luma_input::text_key(TextKey::Delete); break;
            case VK_LEFT: galaxy::luma_input::text_key(TextKey::Left); break;
            case VK_RIGHT: galaxy::luma_input::text_key(TextKey::Right); break;
            case VK_HOME: galaxy::luma_input::text_key(TextKey::Home); break;
            case VK_END: galaxy::luma_input::text_key(TextKey::End); break;
            default: break;
            }
            return 0;
        }
        if (msg == WM_CHAR) {
            // UTF-16: a high surrogate waits for its low one.
            static wchar_t high = 0;
            const auto unit = static_cast<wchar_t>(wp);
            if (unit >= 0xD800 && unit <= 0xDBFF) {
                high = unit;
                return 0;
            }
            if (unit >= 0xDC00 && unit <= 0xDFFF) {
                if (high != 0) {
                    galaxy::luma_input::text_char(0x10000u + ((static_cast<char32_t>(high) - 0xD800u) << 10u) +
                                                  (static_cast<char32_t>(unit) - 0xDC00u));
                }
                high = 0;
                return 0;
            }
            high = 0;
            galaxy::luma_input::text_char(static_cast<char32_t>(unit));
            return 0;
        }
        if (msg == WM_KILLFOCUS) {
            galaxy::luma_input::text_focus_lost();
        }
    }
    if (msg == WM_CHAR) {
        // A mod's text option being typed in (services-mods 8.4): UTF-16 units, a surrogate pair in two messages.
        static wchar_t high = 0;
        const auto unit = static_cast<wchar_t>(wp);
        if (unit >= 0xD800 && unit <= 0xDBFF) {
            high = unit;
            return 0;
        }
        wchar_t units[2] = {unit, 0};
        int count = 1;
        if (unit >= 0xDC00 && unit <= 0xDFFF) {
            if (high == 0) return 0;
            units[0] = high;
            units[1] = unit;
            count = 2;
        }
        high = 0;
        char utf8[8]{};
        const int bytes = WideCharToMultiByte(CP_UTF8, 0, units, count, utf8, sizeof(utf8), nullptr, nullptr);
        if (bytes > 0 && native_settings_handle_window_text(std::string_view(utf8, static_cast<std::size_t>(bytes)))) {
            return 0;
        }
    }
    if (msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN) {
        // Alt+Space belongs to the native system menu, not the game's A key.
        if (msg == WM_SYSKEYDOWN && wp == VK_SPACE) {
            return DefWindowProcW(hwnd, msg, wp, lp);
        }
        const bool was_down =
            (static_cast<std::uintptr_t>(lp) & (std::uintptr_t{1} << 30u)) !=
            0u;
        std::uint32_t handled_key = 0u;
        {
            const UINT raw = static_cast<UINT>(wp);
            const UINT shift = raw == VK_SHIFT
                ? MapVirtualKeyA((static_cast<UINT>(lp) >> 16u) & 0xffu, MAPVK_VSC_TO_VK_EX) : 0u;
            handled_key = galaxy::input::normalize_native_window_key(raw, shift, (static_cast<UINT>(lp) & (1u << 24u)) != 0u);
        }
        if (native_settings_handle_window_key(wp, was_down, handled_key)) {
            return 0;
        }
        if (!galaxy::host::native_keyboard_capture_active()) {
            const UINT key = static_cast<UINT>(wp);
            const UINT mapped_shift = key == VK_SHIFT
                ? MapVirtualKeyA(
                    (static_cast<UINT>(lp) >> 16u) & 0xffu,
                    MAPVK_VSC_TO_VK_EX) : 0u;
            const std::uint32_t normalized =
                galaxy::input::normalize_native_window_key(key, mapped_shift,
                    (static_cast<UINT>(lp) & (1u << 24u)) != 0u);
            if (!was_down) {
                galaxy::host::publish_native_window_input_origin(normalized, input_message_injected());
                galaxy::host::publish_native_window_key_press(normalized);
            }
            galaxy::host::publish_native_window_key_state(normalized, true);
            // The scan code too: the movement keys are bound by position (a letter's place on a US keyboard), whatever
            // the layout calls the key.
            galaxy::host::publish_native_window_key_event(normalized, static_cast<std::uintptr_t>(lp), true, was_down,
                                                          input_message_injected());
        }
    }
    if (msg == WM_KEYUP || msg == WM_SYSKEYUP) {
        const UINT key = static_cast<UINT>(wp);
        const UINT mapped_shift = key == VK_SHIFT
            ? MapVirtualKeyA(
                (static_cast<UINT>(lp) >> 16u) & 0xffu,
                MAPVK_VSC_TO_VK_EX) : 0u;
        const std::uint32_t normalized = galaxy::input::normalize_native_window_key(
            key, mapped_shift, (static_cast<UINT>(lp) & (1u << 24u)) != 0u);
        galaxy::host::publish_native_window_key_state(normalized, false);
        galaxy::host::publish_native_window_key_event(normalized, static_cast<std::uintptr_t>(lp), false, false,
                                                      input_message_injected());
    }
    if (msg == WM_SETCURSOR && LOWORD(lp) == HTCLIENT) {
        const bool focused =
            s_pointer_focused.load(std::memory_order_relaxed) ||
            GetForegroundWindow() == hwnd;
        if (focused) {
            SetCursor(nullptr);
            return TRUE;
        }
    }
    if (msg == WM_MOUSEMOVE) {
        galaxy::host::publish_native_pointer_input_origin(false, input_message_injected());
        update_pointer_absolute(hwnd, lp);
        maybe_hide_os_cursor();
        return 0;
    }
    if ((msg == WM_LBUTTONDOWN && native_settings_capture_mouse(galaxy::input::MouseButton::Left)) ||
        (msg == WM_RBUTTONDOWN && native_settings_capture_mouse(galaxy::input::MouseButton::Right)) ||
        (msg == WM_MBUTTONDOWN && native_settings_capture_mouse(galaxy::input::MouseButton::Middle)) ||
        (msg == WM_XBUTTONDOWN &&
         native_settings_capture_mouse(GET_XBUTTON_WPARAM(wp) == XBUTTON1
                                                  ? galaxy::input::MouseButton::X1
                                                  : galaxy::input::MouseButton::X2))) {
        return msg == WM_XBUTTONDOWN ? TRUE : 0;
    }
    if (msg == WM_MBUTTONDOWN || msg == WM_MBUTTONUP || msg == WM_XBUTTONDOWN || msg == WM_XBUTTONUP) {
        // Middle and side buttons are bindable like keys (VK_MBUTTON, VK_XBUTTON*).
        const bool down = msg == WM_MBUTTONDOWN || msg == WM_XBUTTONDOWN;
        const std::uint32_t vk = (msg == WM_MBUTTONDOWN || msg == WM_MBUTTONUP)
            ? VK_MBUTTON
            : (GET_XBUTTON_WPARAM(wp) == XBUTTON1 ? VK_XBUTTON1 : VK_XBUTTON2);
        if (!galaxy::host::native_keyboard_capture_active()) {
            if (down) galaxy::host::publish_native_window_input_origin(vk, input_message_injected());
            if (down) galaxy::host::publish_native_window_key_press(vk);
            galaxy::host::publish_native_window_key_state(vk, down);
        }
        return (msg == WM_XBUTTONDOWN || msg == WM_XBUTTONUP) ? TRUE : 0;
    }
    if (msg == WM_MOUSEWHEEL && native_settings_wheel(GET_WHEEL_DELTA_WPARAM(wp))) {
        return 0;
    }
    if (msg == WM_MOUSEWHEEL) {
        // The game's own screens scroll by it (the achievement board).
        host_window::pointer_wheel.fetch_add(GET_WHEEL_DELTA_WPARAM(wp), std::memory_order_relaxed);
        return 0;
    }
    if (msg == WM_LBUTTONDOWN || msg == WM_LBUTTONDBLCLK) {
        galaxy::host::publish_native_pointer_input_origin(true, input_message_injected());
        update_pointer_button(hwnd, lp, true, true);
        // Win32 delivers a release outside the client only to the window that captured the mouse; a lost release
        // would leave the Wii Remote's A or B held.
        SetCapture(hwnd);
        maybe_hide_os_cursor();
        return 0;
    }
    if (msg == WM_LBUTTONUP) {
        update_pointer_button(hwnd, lp, true, false);
        if (!s_pointer_right_button.load(std::memory_order_relaxed) &&
            GetCapture() == hwnd) {
            ReleaseCapture();
        }
        maybe_hide_os_cursor();
        return 0;
    }
    if (msg == WM_RBUTTONDOWN || msg == WM_RBUTTONDBLCLK) {
        galaxy::host::publish_native_pointer_input_origin(true, input_message_injected());
        update_pointer_button(hwnd, lp, false, true);
        SetCapture(hwnd);
        maybe_hide_os_cursor();
        return 0;
    }
    if (msg == WM_RBUTTONUP) {
        update_pointer_button(hwnd, lp, false, false);
        if (!s_pointer_left_button.load(std::memory_order_relaxed) &&
            GetCapture() == hwnd) {
            ReleaseCapture();
        }
        maybe_hide_os_cursor();
        return 0;
    }
    if (msg == WM_CAPTURECHANGED || msg == WM_CANCELMODE) {
        const bool had_pressed_button =
            s_pointer_left_button.load(std::memory_order_relaxed) ||
            s_pointer_right_button.load(std::memory_order_relaxed);
        const galaxy::host::HostPointerCaptureCleanupDecision cleanup =
            galaxy::host::select_host_pointer_capture_cleanup(
                msg == WM_CANCELMODE,
                had_pressed_button);
        if (cleanup.publish_cancel_all) {
            s_pointer_transitions.cancel_unexpected_capture(
                cached_pointer_coordinates(false),
                static_cast<std::uint64_t>(GetTickCount64()));
        }
        if (cleanup.clear_window_state) {
            clear_pointer_window_state(hwnd);
        }
        return 0;
    }
    if (msg == WM_MOUSELEAVE) {
        s_tracking_mouse_leave = false;
        mark_pointer_outside();
        return 0;
    }
    if (msg == WM_DPICHANGED) {
        // Borderless fills its monitor: only a window takes the rectangle suggested for the new scale.
        const auto* suggested=reinterpret_cast<const RECT*>(lp);
        if (suggested && !borderless_active.load(std::memory_order_relaxed)) {
            SetWindowPos(hwnd,nullptr,suggested->left,suggested->top,
                suggested->right-suggested->left,suggested->bottom-suggested->top,
                SWP_NOZORDER|SWP_NOACTIVATE);
        }
        return 0;
    }
    if (msg == WM_ACTIVATEAPP) {
        // The 1 ms timer only while in front: it costs a laptop's battery for every program.
        galaxy::host::hold_high_resolution_timer(wp != FALSE);
        // What the game does without focus is the host's to decide (runtime_settings focus_loss): it is woken to look.
        s_background.store(wp == FALSE, std::memory_order_relaxed);
        wake_main_thread();
    }
    if (msg == WM_SIZE) {
        s_minimized.store(wp == SIZE_MINIMIZED, std::memory_order_relaxed);
        wake_main_thread();
        seed_pointer_from_client(hwnd, false);
        // Minimizing reports a 0x0 (or near-0) client, which ResizeBuffers would refuse.
        if (wp != SIZE_MINIMIZED) {
            const int new_width = LOWORD(lp);
            const int new_height = HIWORD(lp);
            if (new_width > 0 && new_height > 0) {
                host_window::pending_resize_extent.store(
                    (static_cast<std::uint64_t>(new_width) << 32u) |
                    static_cast<std::uint32_t>(new_height), std::memory_order_release);
            }
        }
        return 0;
    }
    if (msg == WM_ERASEBKGND) {
        RECT client{};
        if (GetClientRect(hwnd, &client)) {
            FillRect(
                reinterpret_cast<HDC>(wp),
                &client,
                static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
        }
        return 1;
    }
    if (msg == WM_SETFOCUS) {
        s_pointer_focused.store(true, std::memory_order_relaxed);
        s_pointer_focus_valid.store(true, std::memory_order_release);
        raw_mouse_attach(hwnd);
        seed_pointer_from_client(hwnd, false);
        maybe_hide_os_cursor();
        return 0;
    }
    if (msg == WM_MOVE) {
        (void)s_pointer_snapshot.update([](galaxy::host::HostPointerSnapshot& snapshot) noexcept {
            galaxy::host::invalidate_host_pointer_geometry(snapshot);
        });
        return 0;
    }
    if (msg == WM_KILLFOCUS) {
        galaxy::host::clear_native_window_key_presses();
        raw_mouse_detach(hwnd);
        s_pointer_focused.store(false, std::memory_order_relaxed);
        invalidate_pointer_focus_once();
        clear_pointer_window_state(hwnd);
        SetCursor(LoadCursorA(nullptr, IDC_ARROW));
        return 0;
    }
    if (msg == WM_ACTIVATEAPP) {
        s_pointer_focused.store(wp != 0, std::memory_order_relaxed);
        if (wp != 0) {
            s_pointer_focus_valid.store(true, std::memory_order_release);
            raw_mouse_attach(hwnd);
            seed_pointer_from_client(hwnd, false);
            maybe_hide_os_cursor();
        } else {
            galaxy::host::clear_native_window_key_presses();
            raw_mouse_detach(hwnd);
            invalidate_pointer_focus_once();
            clear_pointer_window_state(hwnd);
            SetCursor(LoadCursorA(nullptr, IDC_ARROW));
        }
        return 0;
    }
    if (msg == WM_QUERYENDSESSION || msg == WM_ENDSESSION) {
        // Windows is ending the session (shutdown, restart, sign-out): the unlocks and counts waiting out their write delay
        // (galaxy::achievements, galaxy::stats: 0.25 s and 1.5 s) are written now, on the window's thread, before the process
        // can be ended under them. WM_ENDSESSION's wp says the session is ending; the query allows it (and writes early: a
        // session another program refuses loses nothing by it).
        if (msg == WM_QUERYENDSESSION || wp != FALSE) {
            galaxy::achievements::flush();
            galaxy::stats::flush();
        }
        if (msg == WM_ENDSESSION && wp != FALSE) {
            save_native_settings_now();
        }
        return msg == WM_QUERYENDSESSION ? TRUE : 0;
    }
    if (msg == WM_CLOSE) {
        save_native_settings_now();
        host_window::quit_request.store(true, std::memory_order_release);
        DestroyWindow(hwnd);
        return 0;
    }
    if (msg == WM_DESTROY) {
        save_native_settings_now();
        host_window::quit_request.store(true, std::memory_order_release);
        wake_main_thread();
        raw_mouse_detach(hwnd);
        invalidate_pointer_focus_once();
        clear_pointer_buttons();
        host_window::pointer_hwnd.store(0u, std::memory_order_release);
        s_system_menu_active.store(false, std::memory_order_release);
        host_window::move_size_active.store(false, std::memory_order_release);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

}  // namespace host_window

HostClientExtent current_host_client_extent() noexcept {
    const HWND hwnd = reinterpret_cast<HWND>(
        host_window::pointer_hwnd.load(std::memory_order_acquire));
    RECT client{};
    if (hwnd == nullptr || !GetClientRect(hwnd, &client)) {
        return {};
    }
    return {client.right - client.left, client.bottom - client.top};
}

HostPointerState poll_host_pointer_state() noexcept {
    constexpr std::uint32_t kPointerDebugCachedEvent = 1u << 8u;
    constexpr std::uint32_t kPointerDebugCursorPollAttempted = 1u << 9u;
    constexpr std::uint32_t kPointerDebugCursorPollInside = 1u << 10u;
    constexpr std::uint32_t kPointerDebugSelectedCursorPoll = 1u << 11u;
    constexpr std::uint32_t kPointerDebugRejectedStaleCenter = 1u << 12u;
    constexpr std::uint32_t kPointerDebugRecentClientMove = 1u << 13u;
    constexpr std::uint32_t kPointerDebugFallbackCursorPoll = 1u << 14u;
    constexpr std::uint32_t kPointerDebugLocalPollAuthority = 1u << 16u;
    constexpr std::uint32_t kPointerDebugRemoteSession = 1u << 17u;
    constexpr std::uint32_t kPointerDebugSelectedCursorPollOutside = 1u << 18u;
    const bool message_left_button =
        s_pointer_left_button.load(std::memory_order_relaxed);
    const bool message_right_button =
        s_pointer_right_button.load(std::memory_order_relaxed);
    const HWND hwnd =
        reinterpret_cast<HWND>(host_window::pointer_hwnd.load(std::memory_order_relaxed));
    const galaxy::host::HostPointerSnapshot cached_snapshot =
        s_pointer_snapshot.snapshot();
    HostPointerState state{};
    state.debug_window = reinterpret_cast<std::uintptr_t>(hwnd);
    state.left_button = message_left_button;
    state.right_button = message_right_button;
    state.absolute_acquired_ms = cached_snapshot.absolute_acquired_ms;
    if (s_system_menu_active.load(std::memory_order_acquire) ||
        host_window::move_size_active.load(std::memory_order_acquire)) {
        // A system menu or move/size loop owns the mouse: the game sees no buttons, and the cursor poll does not take
        // the pointer back meanwhile.
        state.left_button = false;
        state.right_button = false;
        return state;
    }

    // Client-area mouse messages win over the per-frame cursor poll: on Remote Desktop and with a hidden cursor,
    // GetCursorPos can report a stale centered position for long stretches while WM_MOUSEMOVE carries the live
    // coordinates. The seed does not advance the event sequence, so startup centering stays inactive until a real
    // cursor sample arrives.
    const std::uint64_t event_seq = cached_snapshot.event_sequence;
    const std::uint64_t leave_seq = cached_snapshot.leave_sequence;
    const bool cached_inside = cached_snapshot.coordinates.inside_client;
    const std::uint64_t pointer_now_ms =
        static_cast<std::uint64_t>(GetTickCount64());
    const galaxy::host::HostPointerClientMessageTiming client_message_timing =
        galaxy::host::classify_host_pointer_client_message_timing(
            pointer_now_ms, cached_snapshot.last_client_message_ms,
            kPointerMessageRecentMs, kStaleCenterRejectMs);
    const bool recent_client_move = client_message_timing.recent;
    const bool remote_session = GetSystemMetrics(SM_REMOTESESSION) != 0;
    const galaxy::host::HostPointerCursorPollAuthorityDecision
        cursor_poll_authority =
            galaxy::host::select_host_pointer_cursor_poll_authority(
                kCursorPollMode, remote_session,
                recent_client_move);
    if (remote_session) {
        state.debug_flags |= kPointerDebugRemoteSession;
    }
    if (cursor_poll_authority.poll_enabled &&
        cursor_poll_authority.authoritative && !remote_session) {
        state.debug_flags |= kPointerDebugLocalPollAuthority;
    }

    state.window_focused = s_pointer_focused.load(std::memory_order_relaxed) ||
                           (hwnd != nullptr && GetForegroundWindow() == hwnd);

    if (event_seq != 0) {
        const int width = cached_snapshot.coordinates.client_width;
        const int height = cached_snapshot.coordinates.client_height;
        if (width > 1 && height > 1) {
            state.debug_flags |= kPointerDebugCachedEvent;
            if (recent_client_move) {
                state.debug_flags |= kPointerDebugRecentClientMove;
            }
            const int cached_x =
                std::clamp(cached_snapshot.coordinates.client_x, 0, width - 1);
            const int cached_y =
                std::clamp(cached_snapshot.coordinates.client_y, 0, height - 1);
            POINT cursor{};
            if (cursor_poll_authority.poll_enabled && hwnd != nullptr &&
                GetCursorPos(&cursor) && ScreenToClient(hwnd, &cursor)) {
                state.debug_flags |= kPointerDebugCursorPollAttempted;
                const bool cursor_inside = cursor.x >= 0 && cursor.y >= 0 &&
                                           cursor.x < width &&
                                           cursor.y < height;
                if (cursor_inside) {
                    state.debug_flags |= kPointerDebugCursorPollInside;
                    const int cursor_x =
                        std::clamp(static_cast<int>(cursor.x), 0, width - 1);
                    const int cursor_y =
                        std::clamp(static_cast<int>(cursor.y), 0, height - 1);
                    const int previous_cursor_x =
                        s_pointer_last_cursor_poll_x.load(
                            std::memory_order_relaxed);
                    const int previous_cursor_y =
                        s_pointer_last_cursor_poll_y.load(
                            std::memory_order_relaxed);
                    const int center_x = (width - 1) / 2;
                    const int center_y = (height - 1) / 2;
                    const bool cursor_near_center =
                        std::abs(cursor_x - center_x) <= 2 &&
                        std::abs(cursor_y - center_y) <= 2;
                    const bool cached_far_from_center =
                        std::abs(cached_x - center_x) > width / 8 ||
                        std::abs(cached_y - center_y) > height / 8;
                    const bool client_move_recent_for_stale_center =
                        cursor_poll_authority.stale_center_guard_allowed &&
                        client_message_timing.stale_center_guard_active;
                    const bool likely_stale_center_poll =
                        client_move_recent_for_stale_center && cached_inside &&
                        cursor_near_center && cached_far_from_center;
                    if (likely_stale_center_poll) {
                        state.debug_flags |= kPointerDebugRejectedStaleCenter;
                    }
                    // Under Remote Desktop GetCursorPos trails WM_MOUSEMOVE on some hosts while on others the
                    // messages stall: the poll takes over once the last client message is no longer fresh, still
                    // rejecting the stale centered sample.
                    const bool cursor_poll_recovers_visibility =
                        (!cached_inside && !recent_client_move) ||
                        event_seq <= leave_seq;
                    // Local polling is authoritative at the input cadence; adaptive remote polling only once the
                    // client-message coordinate has aged out.
                    const galaxy::host::HostPointerCursorPollSelection
                        cursor_poll_selection =
                            galaxy::host::select_host_pointer_cursor_poll({
                                previous_cursor_x !=
                                        std::numeric_limits<int>::min() &&
                                    previous_cursor_y !=
                                        std::numeric_limits<int>::min(),
                                previous_cursor_x,
                                previous_cursor_y,
                                cursor_x,
                                cursor_y,
                                likely_stale_center_poll,
                                cursor_poll_recovers_visibility,
                                cursor_poll_authority.authoritative,
                                cached_snapshot.coordinates.absolute_valid,
                                cached_snapshot.coordinates.client_x,
                                cached_snapshot.coordinates.client_y,
                            });
                    if (cursor_poll_selection.should_publish) {
                        bool publication_accepted = false;
                        (void)s_pointer_snapshot.update(
                                [&](galaxy::host::HostPointerSnapshot&
                                        current) noexcept {
                                    const auto result = galaxy::host::
                                        apply_host_pointer_cursor_inside_publication(
                                            current,
                                            {
                                                true,
                                                cursor_x,
                                                cursor_y,
                                                pointer_now_ms,
                                                event_seq,
                                                width,
                                                height,
                                                cached_snapshot.absolute_sequence,
                                            });
                                    publication_accepted =
                                        result == galaxy::host::
                                                      HostPointerCursorInsidePublicationResult::
                                                          PublishedInside;
                                });
                        if (publication_accepted) {
                            // Only a poll that won the publication becomes the baseline; rejected or
                            // superseded samples stay eligible once the authority/stale-center timeout expires.
                            s_pointer_last_cursor_poll_x.store(
                                cursor_x, std::memory_order_relaxed);
                            s_pointer_last_cursor_poll_y.store(
                                cursor_y, std::memory_order_relaxed);
                            state.debug_flags |=
                                kPointerDebugSelectedCursorPoll;
                        }
                    }
                } else {
                    bool outside_publication_selected = false;
                    (void)s_pointer_snapshot.update(
                            [&](galaxy::host::HostPointerSnapshot& current)
                                noexcept {
                                const auto result = galaxy::host::
                                    apply_host_pointer_cursor_outside_publication(
                                        current,
                                        {
                                            true,
                                            false,
                                            cursor_poll_authority.authoritative,
                                            event_seq,
                                            width,
                                            height,
                                            cached_snapshot.absolute_sequence,
                                        });
                                outside_publication_selected =
                                    result != galaxy::host::
                                                  HostPointerCursorOutsidePublicationResult::
                                                      Rejected;
                            });
                    if (outside_publication_selected) {
                        state.debug_flags |=
                            kPointerDebugSelectedCursorPollOutside;
                    }
                }
            }
            // The window procedure may have published a newer move, leave or resize during
            // GetCursorPos/ScreenToClient: report the latest snapshot, not the older cached one.
            const galaxy::host::HostPointerSnapshot final_snapshot =
                s_pointer_snapshot.snapshot();
            state.client_width = final_snapshot.coordinates.client_width;
            state.client_height = final_snapshot.coordinates.client_height;
            state.client_x = final_snapshot.coordinates.client_x;
            state.client_y = final_snapshot.coordinates.client_y;
            state.inside_client =
                galaxy::host::select_host_pointer_ir_availability(
                    state.window_focused,
                    final_snapshot.coordinates.absolute_valid,
                    final_snapshot.coordinates.inside_client)
                    .acquired_inside_available;
            if (state.inside_client) {
                maybe_hide_os_cursor();
            }
            state.absolute_valid = final_snapshot.coordinates.absolute_valid;
            state.absolute_sequence = final_snapshot.absolute_sequence;
            state.absolute_acquired_ms = final_snapshot.absolute_acquired_ms;
            return state;
        }
    }

    if (hwnd != nullptr && cursor_poll_authority.poll_enabled &&
        cursor_poll_authority.authoritative) {
        RECT client{};
        POINT cursor{};
        if (GetClientRect(hwnd, &client) && GetCursorPos(&cursor) &&
            ScreenToClient(hwnd, &cursor)) {
            state.debug_flags |= kPointerDebugFallbackCursorPoll;
            const int width = client.right - client.left;
            const int height = client.bottom - client.top;
            const bool inside = cursor.x >= 0 && cursor.y >= 0 &&
                                cursor.x < width && cursor.y < height;
            if (width > 1 && height > 1 && inside) {
                const int client_x =
                    std::clamp(static_cast<int>(cursor.x), 0, width - 1);
                const int client_y =
                    std::clamp(static_cast<int>(cursor.y), 0, height - 1);
                const std::uint64_t fallback_now_ms =
                    static_cast<std::uint64_t>(GetTickCount64());
                bool publication_accepted = false;
                const galaxy::host::HostPointerSnapshot publication =
                    s_pointer_snapshot.update(
                        [&](galaxy::host::HostPointerSnapshot&
                                current) noexcept {
                            const auto result = galaxy::host::
                                apply_host_pointer_fallback_inside_publication(
                                    current,
                                    {
                                        client_x,
                                        client_y,
                                        width,
                                        height,
                                        fallback_now_ms,
                                        event_seq,
                                        cached_snapshot.absolute_sequence,
                                    });
                            publication_accepted =
                                result == galaxy::host::
                                              HostPointerFallbackInsidePublicationResult::
                                                  PublishedInside;
                        });
                if (publication_accepted) {
                    state.client_width = publication.coordinates.client_width;
                    state.client_height = publication.coordinates.client_height;
                    state.client_x = publication.coordinates.client_x;
                    state.client_y = publication.coordinates.client_y;
                    state.inside_client = true;
                    state.absolute_valid = true;
                    state.absolute_sequence = publication.absolute_sequence;
                    state.absolute_acquired_ms =
                        publication.absolute_acquired_ms;
                    maybe_hide_os_cursor();
                    return state;
                }
            }
        }
    }

    if (hwnd != nullptr) {
        seed_pointer_from_client(hwnd, false);
    }
    const galaxy::host::HostPointerSnapshot final_snapshot =
        s_pointer_snapshot.snapshot();
    state.inside_client =
        galaxy::host::select_host_pointer_ir_availability(
            state.window_focused, final_snapshot.coordinates.absolute_valid,
            final_snapshot.coordinates.inside_client)
            .acquired_inside_available;
    state.client_x = final_snapshot.coordinates.client_x;
    state.client_y = final_snapshot.coordinates.client_y;
    state.client_width = final_snapshot.coordinates.client_width;
    state.client_height = final_snapshot.coordinates.client_height;
    state.absolute_valid = final_snapshot.coordinates.absolute_valid;
    state.absolute_sequence = final_snapshot.absolute_sequence;
    state.absolute_acquired_ms = final_snapshot.absolute_acquired_ms;
    return state;
}

host::HostPointerTransitionPoll
poll_host_pointer_transition(std::uint64_t after_sequence) noexcept {
    return s_pointer_transitions.read_after(after_sequence);
}

void native_settings_post_key(std::uintptr_t virtual_key) noexcept {
    const HWND window = reinterpret_cast<HWND>(
        host_window::settings_window.load(std::memory_order_acquire));
    if (window != nullptr) {
        (void)PostMessageW(window, kSettingsKeyMessage,
                           static_cast<WPARAM>(virtual_key), 0);
    }
}

namespace host_window {

std::vector<SettingsMonitor> settings_monitors() {
    std::vector<SettingsMonitor> out;
    for (const MonitorInfo& monitor : cached_monitors()) {
        RECT frame{0, 0, 0, 0};
        (void)AdjustWindowRectExForDpi(&frame, kWindowStyle, FALSE, WS_EX_APPWINDOW, monitor_dpi(monitor));
        SettingsMonitor entry;
        entry.id = narrow_utf8(monitor.device);
        entry.name = monitor.name;
        entry.width = monitor.current.width;
        entry.height = monitor.current.height;
        entry.refresh_hz = monitor.current.refresh_hz;
        entry.max_client_width = (monitor.work_area.right - monitor.work_area.left) - (frame.right - frame.left);
        entry.max_client_height = (monitor.work_area.bottom - monitor.work_area.top) - (frame.bottom - frame.top);
        entry.primary = monitor.primary;
        for (const auto& size : monitor_resolutions(monitor)) {
            entry.resolutions.push_back(size);
        }
        out.push_back(std::move(entry));
    }
    return out;
}

bool window_modal_active() noexcept {
    return s_system_menu_active.load(std::memory_order_acquire) || move_size_active.load(std::memory_order_acquire);
}

void resize_window_client(unsigned width, unsigned height) noexcept {
    const HWND window = reinterpret_cast<HWND>(pointer_hwnd.load(std::memory_order_acquire));
    if (window != nullptr) {
        (void)PostMessageW(window, kResizeClientMessage, width, static_cast<LPARAM>(height));
    }
}

// The file picker on its own thread, a COM apartment of its own.
void pick_files(FilePick kind, std::function<void(std::vector<std::filesystem::path>)> done) {
    const HWND owner = reinterpret_cast<HWND>(settings_window.load(std::memory_order_acquire));
    std::thread([owner, kind, done = std::move(done)] {
        const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
        std::vector<std::filesystem::path> files;
        IFileOpenDialog* dialog = nullptr;
        if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) {
            DWORD options = 0u;
            dialog->GetOptions(&options);
            options |= FOS_ALLOWMULTISELECT | FOS_FORCEFILESYSTEM;
            if (kind == FilePick::Miis) {
                // Every kind of Mii file (galaxy::host::read_mii_file); the Mii Studio code as text.
                const COMDLG_FILTERSPEC filters[] = {
                    {L"Mii files (Wii, DS, 3DS, Wii U, Switch, Mii Studio, Mii Creator)",
                     L"*.mii;*.rcd;*.rsd;*.mae;*.miigx;*.ncd;*.nsd;*.cfcd;*.cfsd;*.ffcd;*.ffsd;*.charinfo;*.nfsd;*.nfcd;"
                     L"*.mnms;*.emnms;*.miic;*.dat;*.bin;*.txt"},
                    {L"All files", L"*.*"}};
                dialog->SetFileTypes(2u, filters);
                dialog->SetTitle(L"Import Miis");
                options |= FOS_FILEMUSTEXIST;
            } else if (kind == FilePick::ModFolders) {
                options |= FOS_PICKFOLDERS | FOS_PATHMUSTEXIST;
                dialog->SetTitle(L"Import mod folders");
            } else {
                const COMDLG_FILTERSPEC filters[] = {
                    {L"Mods and texture packs (luma, zip, 7z, rar, tar)",
                     L"*.luma;*.zip;*.7z;*.rar;*.tar;*.tgz;*.gz;*.txz;*.xz;*.tbz2;*.bz2;*.tzst;*.zst"},
                    {L"All files", L"*.*"}};
                dialog->SetFileTypes(2u, filters);
                dialog->SetTitle(L"Import mods");
                options |= FOS_FILEMUSTEXIST | FOS_PATHMUSTEXIST;
            }
            dialog->SetOptions(options);
            IShellItemArray* items = nullptr;
            if (SUCCEEDED(dialog->Show(owner)) && SUCCEEDED(dialog->GetResults(&items))) {
                DWORD count = 0u;
                items->GetCount(&count);
                for (DWORD i = 0; i < count; ++i) {
                    IShellItem* item = nullptr;
                    if (SUCCEEDED(items->GetItemAt(i, &item))) {
                        PWSTR path = nullptr;
                        if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
                            files.emplace_back(path);
                            CoTaskMemFree(path);
                        }
                        item->Release();
                    }
                }
                items->Release();
            }
            dialog->Release();
        }
        if (SUCCEEDED(com)) {
            CoUninitialize();
        }
        done(std::move(files));
    }).detach();
}

// Explorer at a folder (made if missing), off the window's thread.
void open_folder(const std::filesystem::path& folder) {
    std::thread([folder] {
        std::error_code error;
        std::filesystem::create_directories(folder, error);
        const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
        ShellExecuteW(nullptr, L"open", folder.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        if (SUCCEEDED(com)) {
            CoUninitialize();
        }
    }).detach();
}

bool folders_open() noexcept {
    return true;
}

UpscalerCaps upscaler_caps() noexcept {
    UpscalerCaps caps;
    caps.xess = xess_library::available();
    caps.dlss = streamline::dlss_supported();
    caps.frame_generation = streamline::frame_generation_supported();
    caps.frame_generation_dynamic = streamline::frame_generation_dynamic();
    caps.frame_generation_max = streamline::frame_generation_max();
    caps.frames_presented = streamline::frames_presented();
    caps.fsr_frame_generation = fsr_frame_generation_compiled();
    if (get_render_config().frame_generation == 2u) {
        caps.frames_presented = frame_generation_running() && frame_generation_allowed() ? 2u : 1u;
    }
    return caps;
}

unsigned dlss_min_width(unsigned quality, unsigned width, unsigned height) noexcept {
    return dlss_min_render_width(quality, width, height);
}

bool launcher_available() {
    return !launcher_path().empty();
}

bool dlss_files_present() {
    const std::wstring interposer = streamline::default_directory() + L"\\sl.interposer.dll";
    return GetFileAttributesW(interposer.c_str()) != INVALID_FILE_ATTRIBUTES;
}

bool frame_generation_files_present() {
    const std::wstring directory = streamline::default_directory();
    for (const wchar_t* name : {L"sl.dlss_g.dll", L"nvngx_dlssg.dll", L"sl.reflex.dll", L"sl.pcl.dll"}) {
        if (GetFileAttributesW((directory + L"\\" + name).c_str()) == INVALID_FILE_ATTRIBUTES) {
            return false;
        }
    }
    return true;
}

bool xess_files_present() {
    return vendor_download::installed(vendor_download::Package::Xess, xess_library::directory());
}

namespace {

vendor_download::Package vendor_package(unsigned upscaler) {
    return upscaler == 4u ? vendor_download::Package::Xess : vendor_download::Package::Dlss;
}

}  // namespace

unsigned vendor_megabytes(unsigned upscaler) {
    return vendor_download::download_megabytes(vendor_package(upscaler));
}

bool start_vendor_install(unsigned upscaler) {
    const std::wstring directory = upscaler == 4u ? xess_library::directory() : streamline::default_directory();
    return !directory.empty() && vendor_download::start(vendor_package(upscaler), directory);
}

void cancel_vendor_install() {
    vendor_download::cancel();
}

VendorInstall poll_vendor_install(unsigned& percent) {
    const vendor_download::Progress progress = vendor_download::progress();
    percent = progress.total_bytes != 0u ? static_cast<unsigned>(progress.done_bytes * 100u / progress.total_bytes) : 0u;
    switch (progress.state) {
    case vendor_download::State::Done:
        vendor_download::acknowledge();
        return VendorInstall::Done;
    case vendor_download::State::Canceled:
        vendor_download::acknowledge();
        return VendorInstall::Canceled;
    case vendor_download::State::Failed:
        vendor_download::acknowledge();
        return VendorInstall::Failed;
    default:
        return VendorInstall::Running;
    }
}

void fetch_vendor_license(unsigned upscaler) {
    vendor_download::fetch_license(vendor_package(upscaler));
}

VendorLicense vendor_license(std::string& text) {
    vendor_download::License license = vendor_download::license();
    if (license.failed) {
        return VendorLicense::Failed;
    }
    if (!license.ready) {
        return VendorLicense::Loading;
    }
    text = std::move(license.text);
    return VendorLicense::Ready;
}

// The launcher starts the game again once this process has exited (`record`: a diagnostics run); this one closes.
bool restart_game(bool record) {
    if (!start_launcher(L"--restart-after " + std::to_wstring(GetCurrentProcessId()) + (record ? L" --record" : L""),
                        nullptr)) {
        return false;
    }
    PostMessageW(reinterpret_cast<HWND>(settings_window.load(std::memory_order_acquire)), WM_CLOSE, 0, 0);
    return true;
}

}  // namespace host_window

}  // namespace galaxy::render

namespace galaxy::host {

unsigned touch_pointer_state() noexcept {
    return 0u;
}

void note_touch_target(std::uint32_t) noexcept {}

bool touch_target_at(float, float) noexcept {
    return false;
}

unsigned window_width_dp() noexcept {
    const HWND window = reinterpret_cast<HWND>(render::host_window::pointer_hwnd.load(std::memory_order_relaxed));
    RECT area{};
    if (window == nullptr || !GetClientRect(window, &area)) {
        return 0u;
    }
    const UINT dpi = GetDpiForWindow(window);
    const int width = area.right - area.left;
    return static_cast<unsigned>(dpi != 0u ? MulDiv(width, 96, static_cast<int>(dpi)) : width);
}

}  // namespace galaxy::host
