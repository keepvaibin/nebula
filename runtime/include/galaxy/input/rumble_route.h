// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "galaxy/controller_hid.h"

namespace galaxy::host {
// A paired Joy-Con half supplies its pair's logical slot.
bool rumble_targets_hid_device(const RumbleTarget& target, input::DeviceKey device) noexcept;
}  // namespace galaxy::host
