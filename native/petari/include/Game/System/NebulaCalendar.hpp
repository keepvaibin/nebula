#pragma once

// The date and time (B-01, D-07): the host's local clock, in the unit the game's saves store times in.

#include <stdint.h>

namespace NebulaCalendar {
    // A time: seconds from 2000-01-01 00:00 local.
    typedef int64_t SaveTime;

    // As the save files hold it: ticks of the console's 60.75 MHz timer, so saves written here and on a Wii (or in Dolphin) read the same.
    inline int64_t toFileTime(SaveTime time) {
        return time * 60750000;
    }

    inline SaveTime fromFileTime(int64_t ticks) {
        return ticks / 60750000;
    }

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

}  // namespace NebulaCalendar
