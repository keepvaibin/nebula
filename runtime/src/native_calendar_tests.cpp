// Tests for the game's NebulaCalendar (native/petari/src/Game/System/NebulaCalendar.cpp), compiled here against a host
// whose clock the test sets: the save-time unit (the console's 60.75 MHz ticks from 2000-01-01) and dates across leap
// days, centuries and before 2000.
#include "Game/System/NebulaCalendar.hpp"

#include <nebula_game_api.h>

#include <cstdint>
#include <iostream>

namespace {

int64_t gCalendarNs;

int64_t calendar_ns(void*) {
    return gCalendarNs;
}

NebulaHostServices make_host() {
    NebulaHostServices host{};
    host.calendar_ns = calendar_ns;
    return host;
}

const NebulaHostServices gHost = make_host();

bool expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
    }
    return condition;
}

bool date_is(int64_t seconds, int year, int month, int day, int hour, int minute, int second) {
    const NebulaCalendar::Date d = NebulaCalendar::toDate(seconds * NebulaCalendar::cTicksPerSecond);
    return d.mYear == year && d.mMonth == month && d.mDay == day && d.mHour == hour && d.mMinute == minute &&
        d.mSecond == second;
}

}  // namespace

extern "C" const NebulaHostServices* nebula_host(void) {
    return &gHost;
}

int main() {
    bool passed = true;

    gCalendarNs = 1000000000;
    passed &= expect(NebulaCalendar::now() == 60750000, "one second is 60,750,000 save ticks");
    gCalendarNs = 846678896LL * 1000000000 + 500000000;
    passed &= expect(NebulaCalendar::now() == 846678896LL * 60750000 + 30375000, "fractions of a second are kept");
    passed &= expect(NebulaCalendar::toSeconds(NebulaCalendar::now()) == 846678896, "back to seconds");

    passed &= expect(date_is(0, 2000, 1, 1, 0, 0, 0), "2000-01-01, the clock's start");
    passed &= expect(date_is(5183999, 2000, 2, 29, 23, 59, 59), "2000's leap day (a century divisible by 400)");
    passed &= expect(date_is(5184000, 2000, 3, 1, 0, 0, 0), "the day after it");
    passed &= expect(date_is(31665600, 2001, 1, 1, 12, 0, 0), "2001");
    passed &= expect(date_is(762566399, 2024, 2, 29, 23, 59, 59), "2024's leap day");
    passed &= expect(date_is(846678896, 2026, 10, 30, 12, 34, 56), "a date and a time of day");
    passed &= expect(date_is(3155759999LL, 2099, 12, 31, 23, 59, 59), "the end of 2099");
    passed &= expect(date_is(-1, 1999, 12, 31, 23, 59, 59), "a second before the clock's start");

    if (!passed) {
        return 1;
    }
    std::cout << "Native calendar tests passed\n";
    return 0;
}
