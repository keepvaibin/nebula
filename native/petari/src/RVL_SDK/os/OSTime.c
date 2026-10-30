#include "revolution/os/OSTime.h"
#include "revolution/os.h"

static int YearDays[] = { 0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334 };
static int LeapYearDays[] = { 0, 31, 60, 91, 121, 152, 182, 213, 244, 274, 305, 335 };

// The system time is the time base (OSGetTime, the platform layer's), which has no bias: the console kept one in low
// memory.
OSTime __OSGetSystemTime(void) {
    return OSGetTime();
}

OSTime __OSTimeToSystemTime(OSTime time) {
    return time;
}

static BOOL IsLeapYear(int year) {
    return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
}

static int GetLeapDays(int year) {
    if (year < 1) {
        return 0;
    }

    return (year + 3) / 4 - (year - 1) / 100 + (year - 1) / 400; 
}

static void GetDates(s32 days, OSCalendarTime *pTime) NO_INLINE {
    int year;
    int dayCount;
    int month;
    int* monthArr;

    pTime->wday = (days + 6) % 7;

    for (year = days / 365; days < (dayCount = GetLeapDays(year) + 365 * year); --year)
        ;

    days -= dayCount;
    pTime->year = year;
    pTime->yday = days;

    monthArr = IsLeapYear(year) ? LeapYearDays : YearDays;

    for (month = 12; days < monthArr[--month];)
        ;

    pTime->mon = month;
    pTime->mday = days - monthArr[month] + 1;
}

void OSTicksToCalendarTime(OSTime ticks, OSCalendarTime *pTime) {
    int numDays;
    int numSecs;
    OSTime ticksAfter;

    ticksAfter = ticks % OSSecondsToTicks(1);

    if (ticksAfter < 0) {
        ticksAfter += OSSecondsToTicks(1);
    }

    pTime->usec = (int)(OSTicksToMicroseconds(ticksAfter) % 1000);
    pTime->msec = (int)(OSTicksToMilliseconds(ticksAfter) % 1000);

    ticks -= ticksAfter;

    numDays = (int)(OSTicksToSeconds(ticks) / 86400 + 0xB2575);
    numSecs = (int)(OSTicksToSeconds(ticks) % 86400);

    if (numSecs < 0) {
        numDays -= 1;
        numSecs += 86400;
    }

    GetDates(numDays, pTime);
    pTime->hour = numSecs / 60 / 60;
    pTime->min = (numSecs / 60) % 60;
    pTime->sec = numSecs % 60;
}
