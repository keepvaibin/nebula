#pragma once

#include <nebula_types.h>

namespace MR {
    /// @brief Moves a note down by whole octaves until it is at most `most`, but never under its lowest octave (0 to
    /// 11). MeTrack's note ranges (MeTrack::adjustInNoteRange, adjustInStartNoteRange) took 12 off a u8 until the note
    /// was in range: with `most` under the note's lowest octave the note wrapped past 0 and, for an upper end of 0 to 2,
    /// never came in range, so the sound thread hung (N-3027). Where the console's loop ended without a wrap, the
    /// result is its result.
    template < class T >
    inline void octavesDownTo(T& rNote, s32 most) {
        s32 note = rNote;
        if (note > most) {
            note -= 12 * ((note - most + 11) / 12);
            if (note < 0) {
                note += 12 * ((11 - note) / 12);
            }
        }
        rNote = static_cast< T >(note);
    }

    /// @brief Moves a note up by whole octaves until it is at least `least`, but never over its highest octave a `T`
    /// holds. The console added 12 to a u8 until the note was in range: for a lower end of 253 to 255 the note wrapped
    /// past 255 and never came in range (N-3027).
    template < class T >
    inline void octavesUpTo(T& rNote, s32 least) {
        constexpr s32 top = static_cast< T >(~static_cast< T >(0));
        s32 note = rNote;
        if (note < least) {
            note += 12 * ((least - note + 11) / 12);
            if (note > top) {
                note -= 12 * ((note - top + 11) / 12);
            }
        }
        rNote = static_cast< T >(note);
    }
}  // namespace MR
