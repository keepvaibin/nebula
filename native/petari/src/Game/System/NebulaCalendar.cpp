#include "Game/System/NebulaCalendar.hpp"

#include <nebula_game_api.h>

extern "C" const NebulaHostServices* nebula_host(void);

namespace NebulaCalendar {
    SaveTime now() {
        const NebulaHostServices* const host = nebula_host();
        return host->calendar_ns(host->user) / 1000000000;
    }

    // Days from 2000-01-01 to a civil date: the proleptic Gregorian calendar in eras of 400 years (146,097 days),
    // counted from March so that the leap day ends a year.
    Date toDate(SaveTime time) {
        int64_t seconds = time;
        int64_t days = seconds / 86400;
        int64_t secondOfDay = seconds % 86400;
        if (secondOfDay < 0) {
            secondOfDay += 86400;
            days--;
        }

        const int64_t fromEpoch = days + 10957 + 719468;  // 2000-01-01 is day 10,957 after 1970-01-01
        const int64_t era = (fromEpoch >= 0 ? fromEpoch : fromEpoch - 146096) / 146097;
        const int64_t dayOfEra = fromEpoch - era * 146097;
        const int64_t yearOfEra = (dayOfEra - dayOfEra / 1460 + dayOfEra / 36524 - dayOfEra / 146096) / 365;
        const int64_t dayOfYear = dayOfEra - (365 * yearOfEra + yearOfEra / 4 - yearOfEra / 100);
        const int64_t monthFromMarch = (5 * dayOfYear + 2) / 153;

        Date date;
        date.mDay = static_cast< int32_t >(dayOfYear - (153 * monthFromMarch + 2) / 5 + 1);
        date.mMonth = static_cast< int32_t >(monthFromMarch < 10 ? monthFromMarch + 3 : monthFromMarch - 9);
        date.mYear = static_cast< int32_t >(yearOfEra + era * 400 + (date.mMonth <= 2 ? 1 : 0));
        date.mHour = static_cast< int32_t >(secondOfDay / 3600);
        date.mMinute = static_cast< int32_t >(secondOfDay / 60 % 60);
        date.mSecond = static_cast< int32_t >(secondOfDay % 60);
        return date;
    }
}  // namespace NebulaCalendar
