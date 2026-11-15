// SPDX-License-Identifier: GPL-3.0-only
// Co-star mode (runtime_settings co_star): with two or more controllers connected, the one attached last is player 2.
// It aims a second pointer that collects Star Bits, and player 1 no longer hears it. Every host runs it the same way:
// update_co_star once per input sample, before player 1's input is made from the same pads; the game reads player 2
// through galaxy::host::native_game_co_star (host_input.h).
#pragma once

#include "galaxy/input/live_controls_engine.h"

#include <cstddef>
#include <span>

namespace galaxy::host::controls {

// `pads`: this sample's controllers (entries not connected are skipped); `newest`: the index in `pads` of the
// controller attached last. With co-star mode on and two or more connected, that controller becomes player 2 and its
// entry is cleared, so player 1's input no longer hears it. Its pointer moves at the speed of the stick its bindings
// aim the pointer with, and stays where it was while co-star mode is off.
void update_co_star(std::span<LivePad> pads, std::size_t newest, const galaxy::input::ControlsConfig& config);

}  // namespace galaxy::host::controls
