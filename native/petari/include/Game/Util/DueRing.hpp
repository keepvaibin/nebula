#pragma once

#include "Game/Util/Array.hpp"

namespace MR {
    /// @brief The update steps some events are due at, oldest first, in a ring that grows when it is full. Zero-initialised
    /// is empty. Steps are taken in order, so a step is due when the clock has reached it.
    struct DueRing {
        GrowBuffer< u32 > mItems;
        u32 mHead;
        u32 mCount;

        void clear() {
            mHead = 0;
            mCount = 0;
        }

        u32 count() const {
            return mCount;
        }

        /// @brief The oldest step is at or before `step`.
        bool isDue(u32 step) const {
            return mCount != 0 && mItems[mHead] <= step;
        }

        /// @brief Takes the oldest step off.
        void pop() {
            mHead = (mHead + 1) % mItems.mRoom;
            mCount--;
        }

        /// @brief Adds a step after the others.
        void push(u32 step) {
            if (mCount == mItems.mRoom) {
                // Full: the ring doubles and the part that wrapped follows the end of the old one.
                const u32 had = mItems.mRoom;
                u32* const pRing = mItems.room(had + 1);
                for (u32 i = 0; i < mHead; i++) {
                    pRing[had + i] = pRing[i];
                }
            }
            mItems[(mHead + mCount) % mItems.mRoom] = step;
            mCount++;
        }
    };
}  // namespace MR
