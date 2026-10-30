#pragma once

// The date and time (B-01, D-07): the host's local clock, in the unit the game's saves store times in.

#include <revolution/types.h>

namespace NebulaCalendar {
    // A time as the saves store it: ticks of the console's 60.75 MHz timer from 2000-01-01 00:00 local time, so
    // saves written here and on a Wii (or in Dolphin) read the same.
    typedef s64 SaveTime;

    const s64 cTicksPerSecond = 60750000;

    struct Date {
        s32 mYear;
        s32 mMonth;  // 1-12
        s32 mDay;    // 1-31
        s32 mHour;
        s32 mMinute;
        s32 mSecond;
    };

    SaveTime now();
    Date toDate(SaveTime time);

    inline s64 toSeconds(SaveTime time) {
        return time / cTicksPerSecond;
    }
}  // namespace NebulaCalendar
