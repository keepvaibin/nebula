#pragma once

// The host's steady clock (B-01: the game no longer reads the console's time base for durations and pacing).
// Nanoseconds from an arbitrary start, never 0, so 0 can mean "never".

#include <revolution/types.h>

namespace NebulaClock {
    s64 nowNs();
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
