#pragma once

#include <nebula_types.h>

#include <math.h>

namespace MR {
    /// @brief Whether `Count` bytes at `offset` are all in a sequence program of `bytes` bytes.
    template < u64 Count >
    inline bool holdsSeqData(u32 bytes, u64 offset) {
        return offset <= bytes && bytes - offset >= Count;
    }

    /// @brief Reads `Width` bytes (1 to 4) big-endian at `offset` of a sequence program's `bytes` bytes, if they are all
    /// in it. A program names its tables' addresses and indices in registers (SeqTrack::step's register tables, branch
    /// tables, interrupt table and filter), so the cook cannot bound them; the console read whatever memory followed
    /// the program (N-3025). The offset is 64 bits, so an address and an index near 2^32 never wrap to a low byte.
    template < u32 Width >
    inline bool readSeqData(const u8* pData, u32 bytes, u64 offset, u32& rValue) {
        static_assert(Width >= 1 && Width <= 4, "a sequence reads 1 to 4 bytes");
        if (!holdsSeqData< Width >(bytes, offset)) {
            return false;
        }

        u32 value = 0;
        for (u32 i = 0; i < Width; i++) {
            value = value << 8 | pData[offset + i];
        }
        rValue = value;
        return true;
    }

    /// @brief The most operations one track runs in one tick. A tick's operations run until one waits (a wait, a note
    /// that holds the track). A program whose loop never waits ran for ever on the console, and its sound thread with it
    /// (N-3026). 2^20 operations take milliseconds, as long as a whole tick of the driver (SoundEngine::cTicksPerSecond,
    /// 400 a second) or longer, so a program that reaches the bound has already lost its tick.
    constexpr u32 cSeqTickStepMost = 1u << 20;

    /// @brief One tick of a track: `rStep()` while `rReady()`, at most cSeqTickStepMost times. Returns 0 when the track
    /// waits, -1 when a step ended it, -2 when it ran cSeqTickStepMost operations without waiting.
    template < class Ready, class Step >
    inline s32 runSeqTick(Ready&& rReady, Step&& rStep) {
        for (u32 steps = 0; rReady(); steps++) {
            if (steps == cSeqTickStepMost) {
                return -2;
            }
            if (rStep() < 0) {
                return -1;
            }
        }

        return 0;
    }

    /// @brief Whether a track's seconds clock (SeqTrack::setSecondsClock: the rhythm's beat clock, TempoAdjuster::
    /// sequenceSeconds) gives a time: one at or over 0 and finite. A rhythm tempo of 0 made the beat clock infinite (60 / 0
    /// seconds a beat), and SeqTrack::seqMain then ticked until an infinite count came due: the sound thread hung
    /// (N-3029). With no time the track keeps its own clock, as with no beat clock at all.
    template < class Seconds >
    inline bool isSeqClockTime(Seconds seconds) {
        return seconds >= 0 && isfinite(seconds);
    }
}  // namespace MR
