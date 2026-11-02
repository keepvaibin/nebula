// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "galaxy/input/key_positions.h"

namespace galaxy::host {
// The native keyboard-layout handle stays in the adapter.
const input::KeyPositionMap& key_positions_for_win32_layout(std::uintptr_t layout);
std::uint8_t key_position_from_win32_scan(std::uint8_t scan, bool extended) noexcept;
}
