#pragma once

// The date and time: the host's local clock, in seconds, as the saves hold it.

#include <stdint.h>

namespace NebulaCalendar {
    // A time: seconds from 2000-01-01 00:00 local.
    typedef int64_t SaveTime;

    // A time read from a save. Older saves hold ticks of a 60.75 MHz timer: from
    // 2^40 up (five hours into 2000 in ticks, 34,000 years in seconds) a value is ticks.
    inline SaveTime fromSaved(int64_t value) {
        return value >= (int64_t(1) << 40) ? value / 60750000 : value;
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
