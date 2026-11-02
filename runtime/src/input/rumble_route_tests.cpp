// SPDX-License-Identifier: GPL-3.0-only
#include "galaxy/input/rumble_route.h"
#if defined(_WINDOWS_) || defined(_INC_WINDOWS)
#error Rumble routing tests must not include Windows.h
#endif
#include <array>
#include <iostream>

int main() {
    using namespace galaxy::host;
    using galaxy::input::ControlScheme;
    const std::array families{ControlScheme::SwitchPro,ControlScheme::SwitchPro2,ControlScheme::PlayStation,
                              ControlScheme::Steam,ControlScheme::Generic};
    const std::array<std::uint8_t,3> slots{0,1,15};
    unsigned cases = 0, failures = 0;
    for (const auto requested : families) for (const auto connected : families)
        for (const auto wanted : slots) for (const auto actual : slots) {
            const RumbleTarget target{RumbleTarget::Kind::Hid,wanted,requested};
            const bool selected = rumble_targets_hid_device(target,{connected,actual});
            if (selected != (requested == connected && wanted == actual)) ++failures;
            ++cases;
        }
    for (const auto kind : {RumbleTarget::Kind::None,RumbleTarget::Kind::XInput}) {
        if (rumble_targets_hid_device({kind,0,ControlScheme::SwitchPro},{ControlScheme::SwitchPro,0})) ++failures;
        ++cases;
    }
    std::cout << "Native rumble family/slot matrix cases=" << cases << " mismatches=" << failures
              << "; non-HID targets checked\n";
    return failures ? 1 : 0;
}
