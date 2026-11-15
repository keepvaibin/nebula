// SPDX-License-Identifier: GPL-3.0-only
// Co-star mode: player 2 from the controller attached last (co_star.h), for every host.

#include "galaxy/input/co_star.h"

#include "galaxy/input/host_input.h"
#include "galaxy/input_bindings.h"
#include "galaxy/native_input.h"
#include "galaxy/runtime_settings.h"

#include <algorithm>
#include <chrono>
#include <mutex>

namespace galaxy::host {

namespace {

std::mutex g_co_star_lock;
bool g_co_star_on = false;                    // guarded by g_co_star_lock
galaxy::input::PlayerInput g_co_star_input{};  // guarded by g_co_star_lock

// Seconds since the last call, at most 0.1 s: a pause in the samples (a stall, the menu) does not throw the pointer.
float seconds_since_last_sample() noexcept {
    static auto last = std::chrono::steady_clock::now();
    const auto now = std::chrono::steady_clock::now();
    const double seconds = std::chrono::duration<double>(now - last).count();
    last = now;
    return static_cast<float>(std::min(seconds, 0.1));
}

}  // namespace

namespace controls {

void update_co_star(std::span<LivePad> pads, std::size_t newest, const galaxy::input::ControlsConfig& config) {
    namespace in = galaxy::input;
    using in::GameInput;
    const float seconds = seconds_since_last_sample();
    in::PlayerInput player{};
    bool on = false;
    if (galaxy::get_runtime_settings().co_star && newest < pads.size() && pads[newest].sample.connected) {
        const auto connected = std::count_if(pads.begin(), pads.end(), [](const LivePad& pad) { return pad.sample.connected; });
        if (connected >= 2) {
            static float s_x = 0.0f;
            static float s_y = 0.0f;
            LivePad& second = pads[newest];
            const in::LogicalInput logic = in::evaluate_pad_bindings(config.table(second.sample.scheme), second.sample, 0.5f);
            const auto held = [&logic](GameInput target) { return logic.held(target); };
            s_x = std::clamp(s_x + logic.pointer_x * seconds * 1.5f, -1.0f, 1.0f);
            s_y = std::clamp(s_y - logic.pointer_y * seconds * 1.5f, -1.0f, 1.0f);
            in::set_pointer(player, s_x, s_y, true);
            player.buttons.a = held(GameInput::A);
            player.buttons.b = held(GameInput::B);
            player.buttons.one = held(GameInput::One);
            player.buttons.two = held(GameInput::Two);
            player.buttons.plus = held(GameInput::Plus);
            player.buttons.minus = held(GameInput::Minus);
            player.buttons.up = held(GameInput::DpadUp);
            player.buttons.down = held(GameInput::DpadDown);
            player.buttons.left = held(GameInput::DpadLeft);
            player.buttons.right = held(GameInput::DpadRight);
            second = {};
            on = true;
        }
    }
    const std::lock_guard<std::mutex> lock(g_co_star_lock);
    g_co_star_on = on;
    g_co_star_input = player;
}

}  // namespace controls

bool native_game_co_star(galaxy::input::PlayerInput* input) noexcept {
    const std::lock_guard<std::mutex> lock(g_co_star_lock);
    if (!g_co_star_on) {
        return false;
    }
    *input = g_co_star_input;
    return true;
}

}  // namespace galaxy::host
