#include "Game/System/NebulaCalendar.hpp"

#include <nebula_game_api.h>

extern "C" const NebulaHostServices* nebula_host(void);

namespace NebulaCalendar {
    SaveTime now() {
        const NebulaHostServices* const host = nebula_host();
        // 60.75 MHz is 243/4 ticks per microsecond.
        return host->calendar_ns(host->user) / 1000 * 243 / 4;
    }

    // Days from 2000-01-01 to a civil date: the proleptic Gregorian calendar in eras of 400 years (146,097 days),
    // counted from March so that the leap day ends a year.
    Date toDate(SaveTime time) {
        s64 seconds = toSeconds(time);
        s64 days = seconds / 86400;
        s64 secondOfDay = seconds % 86400;
        if (secondOfDay < 0) {
            secondOfDay += 86400;
            days--;
        }

        const s64 fromEpoch = days + 10957 + 719468;  // 2000-01-01 is day 10,957 after 1970-01-01
        const s64 era = (fromEpoch >= 0 ? fromEpoch : fromEpoch - 146096) / 146097;
        const s64 dayOfEra = fromEpoch - era * 146097;
        const s64 yearOfEra = (dayOfEra - dayOfEra / 1460 + dayOfEra / 36524 - dayOfEra / 146096) / 365;
        const s64 dayOfYear = dayOfEra - (365 * yearOfEra + yearOfEra / 4 - yearOfEra / 100);
        const s64 monthFromMarch = (5 * dayOfYear + 2) / 153;

        Date date;
        date.mDay = static_cast< s32 >(dayOfYear - (153 * monthFromMarch + 2) / 5 + 1);
        date.mMonth = static_cast< s32 >(monthFromMarch < 10 ? monthFromMarch + 3 : monthFromMarch - 9);
        date.mYear = static_cast< s32 >(yearOfEra + era * 400 + (date.mMonth <= 2 ? 1 : 0));
        date.mHour = static_cast< s32 >(secondOfDay / 3600);
        date.mMinute = static_cast< s32 >(secondOfDay / 60 % 60);
        date.mSecond = static_cast< s32 >(secondOfDay % 60);
        return date;
    }
}  // namespace NebulaCalendar
