// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "galaxy/input_bindings.h"

namespace galaxy::input {
// The key ID published by the device adapter at each US letter position.
struct KeyPositionMap {
    std::array<std::uint8_t, 26> keys{};
    constexpr KeyPositionMap() {
        for (std::size_t i = 0; i < keys.size(); ++i) keys[i] = static_cast<std::uint8_t>('A' + i);
    }
    [[nodiscard]] constexpr std::uint8_t source(std::uint8_t position) const noexcept {
        return position >= 'A' && position <= 'Z' ? keys[position - 'A'] : 0;
    }
};

[[nodiscard]] inline KeyboardMouseLevels project_key_positions(KeyboardMouseLevels levels,
                                                               const KeyPositionMap& map) noexcept {
    levels.letter_positions = 0;
    for (unsigned position = 'A'; position <= 'Z'; ++position) {
        const auto key = map.source(static_cast<std::uint8_t>(position));
        levels.set_position(static_cast<std::uint8_t>(position), key != 0 && levels.key(key));
    }
    return levels;
}

[[nodiscard]] inline BindingTable key_position_labels(BindingTable table, const KeyPositionMap& map) noexcept {
    for (auto& bindings : table.targets) {
        for (auto& source : bindings) {
            if (source.kind == SourceKind::KeyPosition) {
                const auto key = map.source(source.code);
                source = key != 0 ? InputSource{SourceKind::Key, key} : InputSource{};
            }
        }
    }
    return table;
}

// A key event of a host whose keys are physical places (SDL's scancodes): the level of the key as a virtual key and, for a letter
// key, of the letter's place. The default keyboard layout binds movement, Z, C, the shake and the pointer keys to places
// (KeyPosition), so an event that sets the key alone moves nothing. `position`: the letter at the key's place on a US keyboard,
// 0 for a key that is not a letter. The generic modifier keys follow the left and right ones, as the bindings name them.
inline void apply_key_event(KeyboardMouseLevels& levels, int vk, int position, bool down) noexcept {
    // The virtual-key numbers of the shift, control and alt keys (input/key_codes.h names them; that file is not included here, as
    // a Windows host includes this one after windows.h, whose DELETE it cannot take).
    constexpr int kShift = 0x10, kControl = 0x11, kMenu = 0x12;
    constexpr int kLeftShift = 0xA0, kRightShift = 0xA1, kLeftControl = 0xA2, kRightControl = 0xA3, kLeftMenu = 0xA4, kRightMenu = 0xA5;
    if (position >= 'A' && position <= 'Z') levels.set_position(static_cast<std::uint8_t>(position), down);
    if (vk <= 0 || vk > 0xFF) return;
    levels.set_key(static_cast<std::uint8_t>(vk), down);
    if (vk == kLeftShift || vk == kRightShift) levels.set_key(kShift, down);
    if (vk == kLeftControl || vk == kRightControl) levels.set_key(kControl, down);
    if (vk == kLeftMenu || vk == kRightMenu) levels.set_key(kMenu, down);
}


// The same for a host whose virtual keys are themselves the physical places (SDL: a scancode names the place, and its letter
// is the letter at that place on a US keyboard): the position of a letter key is its own letter.
inline void apply_physical_key_event(KeyboardMouseLevels& levels, int vk, bool down) noexcept {
    apply_key_event(levels, vk, vk >= 'A' && vk <= 'Z' ? vk : 0, down);
}

}  // namespace galaxy::input
