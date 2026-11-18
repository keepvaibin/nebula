// SPDX-License-Identifier: GPL-3.0-only
#include "galaxy/input/raw_mouse_win32.h"
#include "galaxy/windows_api.h"
#include <array>
#include <cstring>
#include <iostream>
#include <stdexcept>
int main() {
    using galaxy::host::decode_native_raw_mouse;
    const auto check = [](bool value) { if (!value) throw std::runtime_error("raw packet invariant"); };
    RAWINPUT input{};
    input.header.dwType = RIM_TYPEMOUSE; input.header.dwSize = sizeof(input); input.header.wParam = RIM_INPUT;
    input.data.mouse.lLastX = -123456; input.data.mouse.lLastY = 789012;
    std::array<std::byte, sizeof(input) + 1> bytes{};
    const auto packet = [&] {
        std::memcpy(bytes.data() + 1, &input, sizeof(input));
        return std::span<const std::byte>(bytes).subspan(1);
    };
    auto sample = decode_native_raw_mouse(packet());
    check(sample && sample->x == -123456 && sample->y == 789012 && sample->moved);
    for (std::size_t i = 0; i < offsetof(RAWINPUT, data) + sizeof(RAWMOUSE); ++i)
        check(!decode_native_raw_mouse(packet().first(i)));
    input.header.dwSize = sizeof(input) + 1; check(!decode_native_raw_mouse(packet()));
    input.header.dwSize = sizeof(RAWINPUTHEADER); check(!decode_native_raw_mouse(packet()));
    input.header.dwSize = sizeof(input); input.header.dwType = RIM_TYPEKEYBOARD; check(!decode_native_raw_mouse(packet()));
    input.header.dwType = RIM_TYPEMOUSE; input.header.wParam = RIM_INPUTSINK; check(!decode_native_raw_mouse(packet()));
    input.header.wParam = RIM_INPUT; input.data.mouse.usFlags = MOUSE_MOVE_ABSOLUTE | MOUSE_VIRTUAL_DESKTOP;
    // P-076: a position, not a motion; the tracker makes motion of successive positions, in pixels.
    sample = decode_native_raw_mouse(packet());
    check(sample && sample->absolute && !sample->moved && sample->x == -123456 && sample->y == 789012);
    galaxy::input::AbsoluteMouseTracker tracker;
    check(!tracker.step(32768, 32768, 1920, 1080).moved);
    auto step = tracker.step(32768 + 3414, 32768 - 600, 1920, 1080);
    check(step.moved && step.x == 100 && step.y == -10);
    tracker.reset();
    check(!tracker.step(0, 0, 1920, 1080).moved);
    step = tracker.step(70000, 65535, 1920, 1080);
    check(step.moved && step.x == 1919 && step.y == 1079);
    std::cout << "Native relative packet exact; unaligned/truncated/type/size/background guarded; absolute positions move\n";
}
