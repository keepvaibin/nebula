// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// Mods' input actions (docs/modding/services/input.md): each `key` option of a running mod's luma.json is an action. It
// follows its default game control until the player binds it in Controls; its own bindings, per scheme, are kept in
// runtime_settings.ini as `modbind.<scheme>.<mod id>/<action id>=<sources>` lines (the same source text as `bind.`),
// read and written whole with the rest of the settings, kept while the mod is off or gone.
//
// Threads: `declare` runs on the game thread before the first mod starts; `sample` on the thread that samples the
// controls, once a game frame (the game's own read of its pad, native_game_input); `read` on the game thread.

#include "galaxy/input_bindings.h"

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace galaxy::input::mod_actions {

// The most actions one mod has (luma_input.h LUMA_INPUT_ACTIONS_MOST) and in all.
inline constexpr std::size_t kPerModMost = 64u;
inline constexpr std::size_t kMost = 1024u;

struct Declared {
    std::string mod_id;
    std::string id;           // the key option's key
    std::string name;         // what Controls shows, in the player's language as the Mods page reads it
    GameInput follows{};      // its default
};

// The session's actions, in the Mods list's order; replaces what was declared. Past kPerModMost for one mod or kMost
// in all, the rest are left out and named in the log (the validator keeps a mod to 64 options).
void declare(std::vector<Declared> actions, void (*log)(const char*));

// What Controls lists: every declared action, and its bindings for `scheme`.
struct Listed {
    std::string mod_id;
    std::string id;
    std::string name;
    GameInput follows{};
    TargetBindings bindings{};  // empty: it follows its game control on this scheme
};
[[nodiscard]] std::vector<Listed> list(ControlScheme scheme);
// Sets one action's bindings for a scheme (empty: back to its game control). False: no such action, or a source
// binding_source_allowed refuses for its default's target.
bool bind(ControlScheme scheme, std::string_view mod_id, std::string_view id, const TargetBindings& bindings);

// runtime_settings.ini: true when `key` is a `modbind.` line (then taken, whatever the value); the lines to write.
bool apply_setting(std::string_view key, std::string_view value);
[[nodiscard]] std::string format_settings();

// One controls sample: the keyboard (when the game reads it), the connected pads, and the game controls as the
// player's bindings and the extensions' input made them (`game_held`: GameInput bits).
struct PadLevels {
    ControlScheme scheme{};
    PadSample sample{};
};
// True when any action is declared (the sampler's one check with no mod).
[[nodiscard]] bool any() noexcept;
// `host_menu`: a menu of the host is open, and every action reads up (its presses wait for the menu to close).
void sample(bool host_menu, bool keyboard, const KeyboardMouseLevels& levels,
            std::span<const PadLevels> pads, std::uint32_t game_held);

// The game thread's view. `index` is the action's place in the declared list (luma_input.cpp's handle).
struct State {
    bool down = false;
    bool pressed = false;
    bool released = false;
    bool bound = false;
    float value = 0.0f;
    std::uint64_t frames = 0;
};
// The action of `mod_id` named `id`: its index, or -1.
[[nodiscard]] std::int32_t find(std::string_view mod_id, std::string_view id);
[[nodiscard]] std::string_view owner(std::int32_t index);
// This game frame's state of action `index`. The first read after a new sample takes it for every action (its presses
// and releases since the last take), so every read until the next sample agrees.
[[nodiscard]] State read(std::int32_t index);

}  // namespace galaxy::input::mod_actions
