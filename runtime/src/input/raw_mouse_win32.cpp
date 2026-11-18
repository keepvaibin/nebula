// SPDX-License-Identifier: GPL-3.0-only
#include "galaxy/input/raw_mouse_win32.h"
#include "galaxy/launch_args.h"
#include "galaxy/windows_api.h"
#include <atomic>
#include <cstring>

namespace galaxy::host {
namespace {
std::atomic<HWND> registered_window{nullptr};
galaxy::input::RawMouseAccumulator motion;
// Only the window thread publishes; clear() comes from it too.
galaxy::input::AbsoluteMouseTracker absolute;
bool virtual_desktop = false;
bool own_window(HWND window) noexcept {
    DWORD process = 0;
    const DWORD thread = GetWindowThreadProcessId(window, &process);
    return thread == GetCurrentThreadId() && process == GetCurrentProcessId();
}
}

std::optional<galaxy::input::RawMouseMotion> decode_native_raw_mouse(std::span<const std::byte> packet) noexcept {
    constexpr auto required = offsetof(RAWINPUT, data) + sizeof(RAWMOUSE);
    if (packet.size() < required) return std::nullopt;
    RAWINPUT input{};
    std::memcpy(&input, packet.data(), required);
    if (input.header.dwType != RIM_TYPEMOUSE || input.header.dwSize < required ||
        input.header.dwSize > packet.size() || input.header.wParam != RIM_INPUT) return std::nullopt;
    const auto& mouse = input.data.mouse;
    // Remote Desktop, virtual machines and pen tablets report positions; publish turns them into motion.
    if ((mouse.usFlags & MOUSE_MOVE_ABSOLUTE) != 0) {
        return galaxy::input::RawMouseMotion{mouse.lLastX, mouse.lLastY, false, true};
    }
    return galaxy::input::RawMouseMotion{mouse.lLastX, mouse.lLastY, mouse.lLastX != 0 || mouse.lLastY != 0};
}

bool install_native_raw_mouse(std::uintptr_t window) noexcept {
    if (::galaxy::launch_args().started_hidden) return false;
    const auto hwnd = reinterpret_cast<HWND>(window);
    if (!own_window(hwnd)) return false;
    if (const auto previous = registered_window.load(); previous != nullptr && previous != hwnd) return false;
    // No INPUTSINK, NOLEGACY or cursor confinement: registration preserves the window's normal pointer/capture path.
    const RAWINPUTDEVICE device{0x01, 0x02, 0, hwnd};
    if (!RegisterRawInputDevices(&device, 1, sizeof(device))) return false;
    motion.clear();
    absolute.reset();
    registered_window.store(hwnd, std::memory_order_release);
    return true;
}

void remove_native_raw_mouse(std::uintptr_t window) noexcept {
    const auto hwnd = reinterpret_cast<HWND>(window);
    if (!own_window(hwnd) || registered_window.load(std::memory_order_acquire) != hwnd) return;
    const RAWINPUTDEVICE device{0x01, 0x02, RIDEV_REMOVE, nullptr};
    if (RegisterRawInputDevices(&device, 1, sizeof(device))) registered_window.store(nullptr, std::memory_order_release);
    motion.clear();
}

void publish_native_raw_mouse(std::uintptr_t window, std::uintptr_t input_handle) noexcept {
    const auto hwnd = reinterpret_cast<HWND>(window);
    if (registered_window.load(std::memory_order_acquire) != hwnd || !own_window(hwnd) ||
        GetForegroundWindow() != hwnd) {
        absolute.reset();  // positions seen again later do not count the way the pointer went meanwhile
        return;
    }
    RAWINPUT input{};
    UINT size = sizeof(input);
    const UINT copied = GetRawInputData(reinterpret_cast<HRAWINPUT>(input_handle), RID_INPUT,
                                        &input, &size, sizeof(RAWINPUTHEADER));
    if (copied == UINT(-1) || copied > sizeof(input)) return;
    const auto decoded = decode_native_raw_mouse({reinterpret_cast<const std::byte*>(&input), copied});
    if (decoded && decoded->absolute) {
        const bool desktop = (input.data.mouse.usFlags & MOUSE_VIRTUAL_DESKTOP) != 0;
        if (desktop != virtual_desktop) absolute.reset();
        virtual_desktop = desktop;
        const auto step = absolute.step(decoded->x, decoded->y, GetSystemMetrics(desktop ? SM_CXVIRTUALSCREEN : SM_CXSCREEN),
                                        GetSystemMetrics(desktop ? SM_CYVIRTUALSCREEN : SM_CYSCREEN));
        if (step.moved) motion.relative(static_cast<std::int32_t>(step.x), static_cast<std::int32_t>(step.y));
        return;
    }
    if (decoded && decoded->moved)
        motion.relative(static_cast<std::int32_t>(decoded->x), static_cast<std::int32_t>(decoded->y));
}
void clear_native_raw_mouse() noexcept {
    motion.clear();
    absolute.reset();
}
galaxy::input::RawMouseMotion sample_native_raw_mouse() noexcept { return motion.sample(); }

namespace {
std::atomic<bool> look_wanted{false};
std::atomic<HWND> look_window{nullptr};
}
void want_native_mouse_look(bool wanted) noexcept {
    if (look_wanted.exchange(wanted, std::memory_order_acq_rel) == wanted) return;
    if (const HWND window = look_window.load(std::memory_order_acquire); window != nullptr)
        (void)PostMessageW(window, kNativeMouseLookMessage, 0, 0);
}
bool native_mouse_look_wanted() noexcept { return look_wanted.load(std::memory_order_acquire); }
void set_native_mouse_look_window(std::uintptr_t window) noexcept {
    look_window.store(reinterpret_cast<HWND>(window), std::memory_order_release);
}
}  // namespace galaxy::host
