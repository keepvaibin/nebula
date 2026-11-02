// SPDX-License-Identifier: GPL-3.0-only
#include "galaxy/input/key_positions_win32.h"
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <iostream>
#include <utility>

int main() {
    // Read the test thread's existing layout; never load, activate or unload one.
    const HKL layout = GetKeyboardLayout(0);
    if (layout == nullptr) return 1;
    const auto map = galaxy::host::key_positions_for_win32_layout(reinterpret_cast<std::uintptr_t>(layout));
    constexpr std::pair<char, UINT> positions[] = {
        {'W',0x11},{'A',0x1e},{'S',0x1f},{'D',0x20},{'I',0x17},{'J',0x24},{'K',0x25},{'L',0x26},
        {'C',0x2e},{'Z',0x2c},{'Q',0x10},{'E',0x12},{'R',0x13}};
    for (const auto [position, scan] : positions) {
        const auto key = map.source(static_cast<std::uint8_t>(position));
        if (key == 0 || MapVirtualKeyExW(key, MAPVK_VK_TO_VSC_EX, layout) != scan ||
            galaxy::host::key_position_from_win32_scan(static_cast<std::uint8_t>(scan), false) != position ||
            galaxy::host::key_position_from_win32_scan(static_cast<std::uint8_t>(scan), true) != 0) {
            std::cerr << "physical position round trip failed: " << position << '\n'; return 1;
        }
    }
    const auto identity = galaxy::host::key_positions_for_win32_layout(0);
    if (identity.source('W') != 'W' || identity.source('A') != 'A') return 1;
    std::cout << "13 physical positions round trip through the existing Windows layout; identity fallback passed\n";
    return 0;
}
