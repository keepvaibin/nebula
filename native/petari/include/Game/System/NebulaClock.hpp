#pragma once

#include "Game/System/NebulaHost.hpp"

// The host's steady clock.
// Nanoseconds from an arbitrary start, never 0, so 0 can mean "never".

namespace NebulaClock {
    inline s64 nowNs() {
        return static_cast< s64 >(NebulaHost::clockNs());
    }
    inline s64 nowMs() {
        return nowNs() / 1000000;
    }
}  // namespace NebulaClock

