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
}  // namespace galaxy::input
