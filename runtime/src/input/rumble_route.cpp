// SPDX-License-Identifier: GPL-3.0-only
#include "galaxy/input/rumble_route.h"
#if defined(_WINDOWS_) || defined(_INC_WINDOWS)
#error Rumble routing must not include Windows.h
#endif

namespace galaxy::host {
bool rumble_targets_hid_device(const RumbleTarget& target, input::DeviceKey device) noexcept {
    return target.kind == RumbleTarget::Kind::Hid && input::scheme_uses_hid(device.scheme) &&
           target.scheme == device.scheme && target.slot == device.slot;
}
}  // namespace galaxy::host
