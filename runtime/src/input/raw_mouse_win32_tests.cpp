// SPDX-License-Identifier: GPL-3.0-only
#include "galaxy/input/raw_mouse_win32.h"
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
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
    sample = decode_native_raw_mouse(packet()); check(sample && !sample->moved && sample->x == 0 && sample->y == 0);
    std::cout << "Native relative packet exact; unaligned/truncated/type/size/background guarded; absolute channel preserved\n";
}
