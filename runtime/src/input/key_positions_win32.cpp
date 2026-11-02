// SPDX-License-Identifier: GPL-3.0-only
#include "galaxy/input/key_positions_win32.h"
#include <algorithm>
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

namespace galaxy::host {
namespace {
constexpr std::array<UINT, 26> kLetterScans{
    0x1e,0x30,0x2e,0x20,0x12,0x21,0x22,0x23,0x17,0x24,0x25,0x26,0x32,
    0x31,0x18,0x19,0x10,0x13,0x1f,0x14,0x16,0x2f,0x11,0x2d,0x15,0x2c};
}
std::uint8_t key_position_from_win32_scan(std::uint8_t scan, bool extended) noexcept {
    if (extended) return 0;
    const auto found = std::find(kLetterScans.begin(), kLetterScans.end(), scan);
    return found != kLetterScans.end() ? static_cast<std::uint8_t>('A' + (found-kLetterScans.begin())) : 0u;
}
const input::KeyPositionMap& key_positions_for_win32_layout(std::uintptr_t layout) {
    thread_local std::uintptr_t cached_layout = 0;
    thread_local input::KeyPositionMap map;
    if (layout == cached_layout) return map;
    cached_layout = layout;
    if (layout == 0) { map = {}; return map; }
    for (std::size_t i = 0; i < kLetterScans.size(); ++i) {
        const UINT key = MapVirtualKeyExW(kLetterScans[i], MAPVK_VSC_TO_VK_EX, reinterpret_cast<HKL>(layout));
        map.keys[i] = key <= 0xffu ? static_cast<std::uint8_t>(key) : 0u;
    }
    return map;
}
}  // namespace galaxy::host
