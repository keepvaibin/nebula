// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "galaxy/input_bindings.h"
#include <algorithm>
#include <mutex>
#include <vector>

namespace galaxy::input {
struct KeyboardSnapshot {
    KeyboardMouseLevels all{};
    KeyboardMouseLevels physical{};
    bool positions_available = false;
};

// Each native source token keeps the logical key it pressed until release, even if the layout changes.
class KeyboardEventState {
public:
    void legacy_level(std::uint8_t key, bool down) {
        const std::lock_guard lock(mutex_);
        if (!native_ || key <= 6) legacy_held_[key] = down;
    }
    void legacy_press(std::uint8_t key) {
        const std::lock_guard lock(mutex_);
        if (!native_ || key <= 6) legacy_pressed_[key] = true;
    }
    void legacy_origin(std::uint8_t key, bool injected) {
        const std::lock_guard lock(mutex_);
        legacy_injected_[key] = injected;
    }
    void event(std::uint32_t token, std::uint8_t key, std::uint8_t position,
               bool down, bool repeat, bool injected) {
        const std::lock_guard lock(mutex_);
        (void)repeat;  // The source token distinguishes repeats from another key's first press.
        if (!native_) {
            native_ = true;
            for (std::size_t i = 7; i < legacy_held_.size(); ++i) legacy_held_[i] = legacy_pressed_[i] = false;
        }
        const auto found = std::find_if(held_.begin(), held_.end(), [&](const Entry& entry) {
            return entry.token == token && entry.injected == injected;
        });
        if (down) {
            if (key == 0 || found != held_.end()) return;
            const bool letter = position >= 'A' && position <= 'Z';
            held_.push_back({token, key, letter ? position : std::uint8_t{0}, injected});
            const unsigned source = injected ? 1u : 0u;
            ++keys_[source][key];
            pressed_[source][key] = true;
            if (letter) {
                ++positions_[source][position-'A'];
                position_pressed_[source][position-'A'] = true;
            }
        } else if (found != held_.end()) {
            const unsigned source = found->injected ? 1u : 0u;
            --keys_[source][found->key];
            if (found->position != 0) --positions_[source][found->position-'A'];
            held_.erase(found);
        }
    }
    KeyboardSnapshot sample() {
        const std::lock_guard lock(mutex_);
        KeyboardSnapshot result;
        result.positions_available = native_;
        for (unsigned key = 1; key < 256; ++key) {
            const bool physical = keys_[0][key] != 0 || pressed_[0][key] ||
                (!legacy_injected_[key] && (legacy_held_[key] || legacy_pressed_[key]));
            result.physical.set_key(static_cast<std::uint8_t>(key), physical);
            result.all.set_key(static_cast<std::uint8_t>(key), physical || keys_[1][key] != 0 ||
                pressed_[1][key] || legacy_held_[key] || legacy_pressed_[key]);
        }
        for (unsigned i = 0; i < 26; ++i) {
            const bool physical = positions_[0][i] != 0 || position_pressed_[0][i];
            result.physical.set_position(static_cast<std::uint8_t>('A'+i), physical);
            result.all.set_position(static_cast<std::uint8_t>('A'+i), physical || positions_[1][i] != 0 || position_pressed_[1][i]);
        }
        pressed_ = {}; position_pressed_ = {}; legacy_pressed_ = {};
        return result;
    }
    void clear() {
        const std::lock_guard lock(mutex_);
        held_.clear(); keys_ = {}; positions_ = {}; pressed_ = {}; position_pressed_ = {};
        legacy_held_ = {}; legacy_pressed_ = {}; legacy_injected_ = {};
    }
private:
    struct Entry { std::uint32_t token; std::uint8_t key; std::uint8_t position; bool injected; };
    std::mutex mutex_;
    std::vector<Entry> held_;
    std::array<std::array<std::size_t,256>,2> keys_{};
    std::array<std::array<std::size_t,26>,2> positions_{};
    std::array<std::array<bool,256>,2> pressed_{};
    std::array<std::array<bool,26>,2> position_pressed_{};
    std::array<bool,256> legacy_held_{}, legacy_pressed_{}, legacy_injected_{};
    bool native_ = false;
};
}  // namespace galaxy::input
