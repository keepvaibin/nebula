#pragma once

#include <nebula_types.h>

namespace MR {
    /// @brief Adds `add` to a count of frames held in a small unsigned integer, stopping at the type's largest value
    /// instead of starting over at 0. A timer that tells how long a state has lasted (on the ground, in the air, hanging,
    /// in the dark) keeps reading "long" however long the state lasts; on the console a 16-bit one read 0 again after
    /// 65536 steps (18 minutes), as if the state had just begun (N-3011). Counters read as a phase or compared for a
    /// change only (MarioActor::mFogWaveTimer, mMovementTimer) keep their plain increment.
    template < class T >
    inline void countUp(T& rCount, u32 add = 1) {
        static_assert(static_cast< T >(-1) > static_cast< T >(0), "a count of frames is unsigned");
        const T most = static_cast< T >(-1);
        rCount = add >= static_cast< u32 >(most - rCount) ? most : static_cast< T >(rCount + add);
    }
}  // namespace MR
