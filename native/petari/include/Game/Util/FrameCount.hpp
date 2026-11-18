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

    /// @brief Adds `add` to a count that stops at `most` (no more than the type's largest value) and tells whether the
    /// sum went past `most`. The sum is taken in 64 bits, so a count near its type's top never starts over at 0.
    /// Mario's oxygen (a u16, its most MarioConstTable::mOxygenMax) recovers by 32 a step at the surface and by an
    /// eighth of the most for a coin; with a mod's most near 65535 the console's 16-bit sum wrapped and the oxygen fell
    /// to near 0 instead of full (N-3022).
    template < class T >
    inline bool countUpTo(T& rCount, u32 add, u32 most) {
        const u64 sum = static_cast< u64 >(rCount) + add;
        if (sum > most) {
            rCount = static_cast< T >(most);
            return true;
        }

        rCount = static_cast< T >(sum);
        return false;
    }

    /// @brief One step of a countdown that ends at 0: takes one off unless the count is 0 already, and tells whether
    /// any is left. Mario's slide task (Mario::taskOnSlide) took one off before it asked, so a slide started with 0
    /// frames started over at 65535 and slid for 18 minutes (N-3024); 0 frames now slide one step, as 1 does.
    template < class T >
    inline bool countDownLeft(T& rCount) {
        if (rCount != 0) {
            rCount--;
        }

        return rCount != 0;
    }
}  // namespace MR
