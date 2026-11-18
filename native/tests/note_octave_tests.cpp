// SPDX-License-Identifier: GPL-3.0-only
// MR::octavesDownTo and MR::octavesUpTo (Game/Sound/NoteOctave.hpp, N-3027): MeTrack's note ranges. Where the console's
// u8 loop ended without wrapping, the result is the console's; where it wrapped, the note keeps its pitch, and where it
// never ended (an upper end of 0 to 2, a lower end of 253 to 255) the move ends.
#include "Game/Sound/NoteOctave.hpp"

#include <stdio.h>

namespace {
    int sFailures = 0;

    void expect(bool ok, const char* what) {
        if (!ok) {
            printf("FAILED: %s\n", what);
            sFailures++;
        }
    }
}  // namespace

int main() {
    u32 differ = 0;
    for (s32 start = 0; start < 256; start++) {
        // The console's loops, where they cannot wrap: an upper end of 11 or more, a lower end of 244 or less.
        for (s32 most = 11; most < 256; most++) {
            u8 console = static_cast< u8 >(start);
            while (console > most) {
                console -= 12;
            }
            u8 note = static_cast< u8 >(start);
            MR::octavesDownTo(note, most);
            differ += note != console;
        }
        for (s32 least = 0; least <= 244; least++) {
            u8 console = static_cast< u8 >(start);
            while (console < least) {
                console += 12;
            }
            u8 note = static_cast< u8 >(start);
            MR::octavesUpTo(note, least);
            differ += note != console;
        }
    }
    expect(differ == 0, "every range the console's loop ends in without a wrap gives its note");

    // An upper end under the note's lowest octave: the console wrapped for ever.
    u8 note = 5;
    MR::octavesDownTo(note, 0);
    expect(note == 5, "a note with no octave under an upper end of 0 keeps its lowest octave");
    note = 31;
    MR::octavesDownTo(note, 2);
    expect(note == 7, "the lowest octave of 31 is 7");
    note = 250;
    MR::octavesUpTo(note, 255);
    expect(note == 250, "a note with no octave over a lower end of 255 keeps its highest octave");
    note = 10;
    MR::octavesUpTo(note, 254);
    expect(note == 250, "the highest octave of 10 a u8 holds is 250");
    // The start range takes its upper end 12 over the start (as the game's int sum): it never wraps.
    note = 200;
    MR::octavesDownTo(note, 255 + 12);
    expect(note == 200, "an upper end past 255 moves nothing");
    if (sFailures == 0) {
        printf("native_note_octave_tests: all passed\n");
    }
    return sFailures == 0 ? 0 : 1;
}
