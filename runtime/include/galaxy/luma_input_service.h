// SPDX-License-Identifier: GPL-3.0-only
// The host's side of LumaCore's input services (docs/modding/services/input.md): the providers of `input`, `input.inject` and
// `input.dev`, which every host registers (services-registry 1), and what the host feeds them. A host that samples no
// controls (the headless host) leaves every action up and every player's device NONE.
#pragma once

#include <luma_service_provider.h>
#include <nebula_game_api.h>

#include <cstdint>
#include <span>

namespace galaxy::luma_input {

// The providers, for luma_services::provide_all (game thread, once, before the first mod). `log` says what goes
// wrong outside any mod's call (an action left out when they are declared).
[[nodiscard]] std::span<const LumaServiceProvider* const> providers(void (*log)(const char*)) noexcept;

// Game thread, at each of the game's reads of `player`'s controller (svc_input_read), the sample made: recorded for
// `game` and `game_raw`, replaced by a mod's injection, held at rest while a text request is open. `device` is the
// player's LUMA_INPUT_DEVICE_*, `motor` whether the host drives a motor for it. No mod: one atomic load.
void game_read(std::uint32_t player, NebulaInputSample* sample, std::uint32_t device, bool motor) noexcept;
// Game thread, at the same read: the strongest rumble the mods ask on `player` now, 0 to 255 (0: none). The host
// drives the stronger of it and the game's.
[[nodiscard]] std::uint8_t mod_rumble(std::uint32_t player) noexcept;

// What aims the main player's pointer (input 1.2 pointer_source), for the live controls at each sample (any thread):
// `source` a galaxy::input::PointerSource (None: the player's controls choose), `gyro_scale` the gyro's turn.
struct PointerChoice {
    std::uint8_t source = 0;
    float gyro_scale = 1.0f;
};
[[nodiscard]] PointerChoice pointer_choice() noexcept;
// The live controls, at each sample: what could aim the main player's pointer now (bits 1 << PointerSource).
void set_pointer_sources(std::uint32_t bits) noexcept;

// Text entry. The host that has it says so once (a window host at its window's creation); without it text_request
// answers LUMA_E_ABSENT. The rest from the window's thread (any thread): text_open while a request is open, then each
// typed code point (the input method's results included) and each editing key; a lost keyboard focus cancels.
void set_text_entry_available(bool available) noexcept;
[[nodiscard]] bool text_open() noexcept;
void text_char(char32_t point) noexcept;
// A host that gets typed text as UTF-8 (SDL's text input): each code point of it, as text_char; text that is not UTF-8
// is dropped whole.
void text_utf8(const char* text) noexcept;
enum class TextKey : std::uint8_t { Enter, Escape, Backspace, Delete, Left, Right, Home, End };
void text_key(TextKey key) noexcept;
void text_focus_lost() noexcept;

}  // namespace galaxy::luma_input
