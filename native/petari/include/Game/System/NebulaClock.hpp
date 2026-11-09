#pragma once

#include "Game/System/NebulaHost.hpp"

// The host's steady clock.
// Nanoseconds from an arbitrary start, never 0, so 0 can mean "never".

namespace NebulaClock {
    inline s64 nowNs() {
        return static_cast< s64 >(NebulaHost::clockNs());
    }
    inline s64 nowUs() {
        return nowNs() / 1000;
    }
    inline s64 nowMs() {
        return nowNs() / 1000000;
    }
}  // namespace NebulaClock

// `value` rounded up to a multiple of 32 (buffers the game sizes and places on 32-byte boundaries).
template < typename T >
inline T NebulaAlignUp32(T value) {
    return (T)(((unsigned long long)(value) + 31u) & ~31ull);
}
