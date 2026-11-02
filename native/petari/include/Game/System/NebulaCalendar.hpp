#pragma once

// The date and time (B-01, D-07): the host's local clock, in the unit the game's saves store times in.

#include <stdint.h>

namespace NebulaCalendar {
    // A time as the saves store it: ticks of the console's 60.75 MHz timer from 2000-01-01 00:00 local time, so
    // saves written here and on a Wii (or in Dolphin) read the same.
    typedef int64_t SaveTime;

    const int64_t cTicksPerSecond = 60750000;

    struct Date {
        int32_t mYear;
        int32_t mMonth;  // 1-12
        int32_t mDay;    // 1-31
        int32_t mHour;
        int32_t mMinute;
        int32_t mSecond;
    };

    SaveTime now();
    Date toDate(SaveTime time);

    inline int64_t toSeconds(SaveTime time) {
        return time / cTicksPerSecond;
    }
}  // namespace NebulaCalendar
